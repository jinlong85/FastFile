#include "MainWndInternal.h"
#include "FavoriteStarUI.h"
#include "ShellMenuUtil.h"
#include "ExplorerAgentProtocol.h"
#include "FastFileAbout.h"
namespace { bool ForceNewWindowRequested(); }
class FastFileActivationWindow : public CMainWnd {
public:
    // HKCU overrides are process-local; do not spawn an unisolated agent here.
    void EnableExplorerAgent() {}
    HWND Create(HWND parent,LPCTSTR title,DWORD style,DWORD extendedStyle) {
        HWND window=CMainWnd::Create(parent,title,style,extendedStyle);
        if(window) {
            SetPropW(window,L"FastFile.Test.Initialized",reinterpret_cast<HANDLE>(1));
            if(ForceNewWindowRequested())SetPropW(window,L"FastFile.Test.ForcedNewWindow",reinterpret_cast<HANDLE>(1));
        }
        return window;
    }
};
// Compile the production entry point into the process-launch regression. This
// avoids a test-only substitute for command parsing and single-instance IPC.
#define wWinMain FastFileActivationEntry
#define CMainWnd FastFileActivationWindow
#include "../src/main.cpp"
#undef CMainWnd
#undef wWinMain
#include <iostream>
#include <functional>
#include <atomic>
#include <thread>
#include <exdisp.h>
#include <shlguid.h>
#include <propvarutil.h>
#include "ShellWindowRegistration.h"
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")
#include <shldisp.h>

namespace {
// A live OleGetClipboard proxy cannot be put back after clearing its backing
// clipboard: flushing that proxy can recursively read itself. Snapshot data first.
IDataObject* SnapshotClipboard() {
    IDataObject* source=nullptr;
    if (FAILED(OleGetClipboard(&source)) || !source) return nullptr;
    IDataObject* snapshot=nullptr;
    if (FAILED(SHCreateDataObject(nullptr,0,nullptr,nullptr,IID_PPV_ARGS(&snapshot)))) {
        source->Release();return nullptr;
    }
    IEnumFORMATETC* formats=nullptr;
    if (SUCCEEDED(source->EnumFormatEtc(DATADIR_GET,&formats))) {
        FORMATETC format{};
        while (formats->Next(1,&format,nullptr)==S_OK) {
            STGMEDIUM medium{};
            if (SUCCEEDED(source->GetData(&format,&medium))) {
                if (FAILED(snapshot->SetData(&format,&medium,TRUE))) ReleaseStgMedium(&medium);
            }
            CoTaskMemFree(format.ptd);
        }
        formats->Release();
    }
    source->Release();return snapshot;
}
class ScrollBarDragFixture : public CFluentScrollBarUI {
public:
    bool Captured() const { return (m_uThumbState & UISTATE_CAPTURED) != 0; }
    POINT ThumbPoint() const { return {m_rcThumb.left+1,m_rcThumb.top+2}; }
    WPARAM TimerId() const { return DEFAULT_TIMERID; }
};
// Counts files and the newest write time in the user's real icon cache tree
// (%TEMP%\FastFileIconCache), read-only, to prove a test run never touches it.
void UserIconCacheSnapshot(size_t& files, ULONGLONG& newest) {
    files = 0; newest = 0;
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    std::vector<std::wstring> pending{ std::wstring(temp) + L"FastFileIconCache" };
    while (!pending.empty()) {
        const std::wstring dir = pending.back(); pending.pop_back();
        WIN32_FIND_DATAW fd{}; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { pending.push_back(dir + L"\\" + fd.cFileName); continue; }
            ++files;
            newest = (std::max)(newest, (ULONGLONG(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
bool AppIconMatchesSource() {
    wchar_t module[MAX_PATH]{}; GetModuleFileNameW(nullptr,module,_countof(module));
    PathRemoveFileSpecW(module);
    const std::wstring directory=module;
    std::ifstream input(directory+L"\\..\\..\\res\\FastFile.ico",std::ios::binary);
    std::vector<char> source((std::istreambuf_iterator<char>(input)),{});
    if(source.size()<6) return false;
    auto word=[](const char* p) {WORD value;memcpy(&value,p,2);return value;};
    auto dword=[](const char* p) {DWORD value;memcpy(&value,p,4);return value;};
    const WORD count=word(source.data()+4);
    if(!count || source.size()<6+size_t(count)*16) return false;
    HMODULE app=LoadLibraryExW((directory+L"\\FastFile.exe").c_str(),nullptr,
        LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(!app) return false;
    HRSRC group=FindResourceW(app,MAKEINTRESOURCEW(1),RT_GROUP_ICON);
    const char* data=group ? static_cast<const char*>(LockResource(LoadResource(app,group))) : nullptr;
    bool ok=data && SizeofResource(app,group)>=6+DWORD(count)*14 && word(data+4)==count;
    if(!ok) std::cerr<<"icon diagnostic source="<<count<<" resource="<<(data?word(data+4):0)<<"\n";
    for(int i=0;ok && i<count;++i) {
        const char* entry=source.data()+6+i*16;
        const char* compiled=data+6+i*14;
        const DWORD bytes=dword(entry+8),offset=dword(entry+12);
        HRSRC icon=FindResourceW(app,MAKEINTRESOURCEW(word(compiled+12)),RT_ICON);
        const void* bitmap=icon ? LockResource(LoadResource(app,icon)) : nullptr;
        // rc.exe can fill planes/bit-depth metadata for PNG-compressed entries;
        // pixel dimensions and the actual embedded image bytes must match.
        ok=memcmp(entry,compiled,4)==0 && dword(compiled+8)==bytes && size_t(offset)+bytes<=source.size() &&
            bitmap && SizeofResource(app,icon)==bytes && memcmp(bitmap,source.data()+offset,bytes)==0;
        if(!ok)std::cerr<<"icon diagnostic entry="<<i<<" source bytes="<<bytes<<" embedded bytes="<<(icon?SizeofResource(app,icon):0)<<"\n";
    }
    FreeLibrary(app);return ok;
}
// Dismisses (and counts) any dialog box this process opens on the harness's hidden desktop,
// so a native confirmation can only fail a check, never block the suite or reach the user.
class NativeDialogGuard {
public:
    NativeDialogGuard() : m_desktop(GetThreadDesktop(GetCurrentThreadId())) {
        m_worker = std::thread([this] {
            if (m_desktop) SetThreadDesktop(m_desktop);
            while (!m_stop.load()) {
                EnumDesktopWindows(m_desktop, [](HWND window, LPARAM data) -> BOOL {
                    auto* self = reinterpret_cast<NativeDialogGuard*>(data);
                    DWORD process = 0; GetWindowThreadProcessId(window, &process);
                    wchar_t name[64]{}; GetClassNameW(window, name, _countof(name));
                    if (process == GetCurrentProcessId() && IsWindowVisible(window) && wcscmp(name, L"#32770") == 0) {
                        wchar_t title[256]{}; GetWindowTextW(window, title, _countof(title));
                        char utf8[768]{}; WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8, sizeof(utf8), nullptr, nullptr);
                        std::cerr << "  dialog dismissed on the hidden desktop: " << utf8 << '\n';
                        ++self->m_dismissed;
                        PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
                        PostMessageW(window, WM_CLOSE, 0, 0);
                    }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(this));
                Sleep(100);
            }
        });
    }
    ~NativeDialogGuard() { m_stop.store(true); if (m_worker.joinable()) m_worker.join(); }
    int Dismissed() const { return m_dismissed.load(); }
private:
    HDESK m_desktop = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_dismissed{0};
    std::thread m_worker;
};
// Item-by-item comparison (ids, types, states, texts, submenu shape) of two queried menus.
bool SameShellMenu(HMENU left, HMENU right, std::string* why = nullptr, bool compareIds = true) {
    const int count = GetMenuItemCount(left);
    if (count != GetMenuItemCount(right)) {
        if (why) *why = "count " + std::to_string(count) + " vs " + std::to_string(GetMenuItemCount(right));
        return false;
    }
    for (int i = 0; i < count; ++i) {
        wchar_t a[512]{}, b[512]{};
        MENUITEMINFOW x{}, y{}; x.cbSize = y.cbSize = sizeof(MENUITEMINFOW);
        x.fMask = y.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU | MIIM_STRING;
        x.dwTypeData = a; x.cch = _countof(a); y.dwTypeData = b; y.cch = _countof(b);
        if (!GetMenuItemInfoW(left, i, TRUE, &x) || !GetMenuItemInfoW(right, i, TRUE, &y)) return false;
        // A cascading item's wID is its HMENU handle, which differs per query.
        if ((compareIds && !x.hSubMenu && x.wID != y.wID) || x.fType != y.fType || (x.fState & ~MFS_HILITE) != (y.fState & ~MFS_HILITE)
            || wcscmp(a, b) != 0 || (x.hSubMenu != nullptr) != (y.hSubMenu != nullptr)) {
            if (why) {
                char ua[512]{}, ub[512]{};
                WideCharToMultiByte(CP_UTF8, 0, a, -1, ua, sizeof(ua), nullptr, nullptr);
                WideCharToMultiByte(CP_UTF8, 0, b, -1, ub, sizeof(ub), nullptr, nullptr);
                char line[1400]{};
                sprintf_s(line, "item %d shown{id=%u type=0x%x state=0x%x sub=%d \"%s\"} raw{id=%u type=0x%x state=0x%x sub=%d \"%s\"}",
                    i, x.wID, x.fType, x.fState, x.hSubMenu != nullptr, ua, y.wID, y.fType, y.fState, y.hSubMenu != nullptr, ub);
                *why = line;
            }
            return false;
        }
    }
    return true;
}
// The raw Windows menu for comparison: GetUIObjectOf / CreateViewObject + QueryContextMenu
// with no host involvement (callers may site it the way FastFile sites its own menus).
struct RawShellMenu {
    IContextMenu* menu = nullptr; HMENU popup = nullptr; UINT last = 0;
    RawShellMenu() = default;
    RawShellMenu(const RawShellMenu&) = delete;
    RawShellMenu& operator=(const RawShellMenu&) = delete;
    ~RawShellMenu() { if (popup) DestroyMenu(popup); if (menu) menu->Release(); }
    bool Query(UINT flags) {
        if (!menu) return false;
        if (popup) DestroyMenu(popup);
        popup = CreatePopupMenu();
        const HRESULT hr = menu->QueryContextMenu(popup, 0, 1, 0x7FFF, flags);
        last = SUCCEEDED(hr) ? 1 + HRESULT_CODE(hr) : 0;
        return SUCCEEDED(hr);
    }
    bool Item(PCIDLIST_ABSOLUTE item, HWND owner) {
        IShellFolder* parent = nullptr; PCUITEMID_CHILD child = nullptr;
        if (!item || FAILED(SHBindToParent(item, IID_PPV_ARGS(&parent), &child))) return false;
        parent->GetUIObjectOf(owner, 1, &child, IID_IContextMenu, nullptr, reinterpret_cast<void**>(&menu));
        parent->Release();
        return menu != nullptr;
    }
    bool Path(const std::wstring& path, HWND owner) {
        PIDLIST_ABSOLUTE item = nullptr;
        if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr))) return false;
        const bool ok = Item(item, owner); CoTaskMemFree(item); return ok;
    }
    bool Computer(HWND owner) {
        PIDLIST_ABSOLUTE item = nullptr;
        if (FAILED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &item))) return false;
        const bool ok = Item(item, owner); CoTaskMemFree(item); return ok;
    }
    bool Background(const std::wstring& folder, HWND owner) {
        IShellFolder* shellFolder = nullptr; PIDLIST_ABSOLUTE item = nullptr;
        if (FAILED(SHParseDisplayName(folder.c_str(), nullptr, &item, 0, nullptr))) return false;
        SHBindToObject(nullptr, item, nullptr, IID_PPV_ARGS(&shellFolder)); CoTaskMemFree(item);
        if (!shellFolder) return false;
        shellFolder->CreateViewObject(owner, IID_PPV_ARGS(&menu)); shellFolder->Release();
        return menu != nullptr;
    }
};
// Counts (and blocks) popup-menu windows (#32768) created on this thread, so a native
// menu that something tries to show is observed without ever appearing.
int blockedMenuWindows = 0;
LRESULT CALLBACK BlockMenuWindows(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_CREATEWND) {
        wchar_t name[32]{}; GetClassNameW(reinterpret_cast<HWND>(wParam), name, _countof(name));
        if (wcscmp(name, L"#32768") == 0) { ++blockedMenuWindows; return 1; }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
// Menu id of the canonical verb in a queried menu (0 if absent).
UINT VerbCommand(IContextMenu* menu, HMENU popup, UINT last, const wchar_t* verb) {
    const int pos = ShellMenuUtil::FindVerb(menu, popup, 1, last, verb);
    return pos >= 0 ? GetMenuItemID(popup, pos) : 0;
}
}

struct ShellBrowserHostTestAccess {
    static HWND EditControl(ShellBrowserHost& host) {
        return host.m_listWindow ? ListView_GetEditControl(host.m_listWindow) : nullptr;
    }
    static void CancelEdit(ShellBrowserHost& host) {
        if (host.m_listWindow) ListView_CancelEditLabel(host.m_listWindow);
    }
    static HWND ListWindow(ShellBrowserHost& host) { return host.m_listWindow; }
    static ShellBrowserHost::ViewCounters Counters(ShellBrowserHost& host) { return host.m_counters; }
    static bool HasListSpacer(ShellBrowserHost& host) { return host.m_listSpacer != nullptr; }
    static int ResolveItemIcon(ShellBrowserHost& host, int index) { return host.ResolveItemIcon(index); }
    static void Probe(ShellBrowserHost& host, int item) { host.m_probeItem = item; host.m_probeTick = 0; }
    static LONGLONG ProbeTick(ShellBrowserHost& host) { return host.m_probeTick; }
    static bool MediaAspectRatio(ShellBrowserHost& host,const std::wstring& path,int sourceW,int sourceH) {
        // These drawing tests use a synthetic root layout; view switches may
        // recreate a native child before the host's normal layout pass.
        host.SetBounds({0,0,640,640});
        IFolderView2* view=View(host);if(!view)return false;
        int count=0,index=-1;view->ItemCount(SVGIO_ALLVIEW,&count);
        for(int i=0;i<count;++i) {
            IShellItem* item=nullptr;PWSTR name=nullptr;
            if(SUCCEEDED(view->GetItem(i,IID_PPV_ARGS(&item)))) {
                if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))) {
                    if(_wcsicmp(name,path.c_str())==0)index=i;
                    CoTaskMemFree(name);
                }item->Release();
            }
        }view->Release();if(index<0)return false;
        RECT client{};GetClientRect(host.m_listWindow,&client);
        HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,client.right,client.bottom);auto old=SelectObject(dc,bitmap);
        FillRect(dc,&client,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        NMLVCUSTOMDRAW draw{};draw.nmcd.hdc=dc;draw.nmcd.dwItemSpec=index;
        draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;
        host.DrawIconItem(&draw);           // placeholder + async request
        SettleThumbs(host);
        FillRect(dc,&client,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        host.DrawIconItem(&draw);           // cached thumbnail
        POINT point{};ListView_GetItemPosition(host.m_listWindow,index,&point);
        const int pad=MulDiv(8,host.m_dpi,96),size=host.m_iconSlot;
        RECT content{size,size,0,0};int colored=0;
        for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
            const COLORREF color=GetPixel(dc,point.x+pad+x,point.y+pad+y);
            if(GetRValue(color)<80 && GetGValue(color)>60 && GetGValue(color)<150 && GetBValue(color)>140) {
                ++colored;
                content.left=(std::min)(content.left,LONG(x));content.top=(std::min)(content.top,LONG(y));
                content.right=(std::max)(content.right,LONG(x+1));content.bottom=(std::max)(content.bottom,LONG(y+1));
            }
        }
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
        const int width=content.right-content.left,height=content.bottom-content.top;
        if(!sourceW || !sourceH)return colored>100;
        return width>0 && height>0 &&
            abs(width*sourceH-height*sourceW)<=3*(sourceW+sourceH) &&
            abs(content.left-(size-content.right))<=4 && abs(content.top-(size-content.bottom))<=4;
    }
    // Async thumbnails: pump until the worker has delivered every queued thumbnail.
    static bool SettleThumbs(ShellBrowserHost& host, DWORD timeout = 6000) {
        const DWORD deadline = GetTickCount() + timeout;
        DWORD quietSince = GetTickCount();
        while (GetTickCount() < deadline) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) continue;
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if (!host.m_thumbPending.empty()) quietSince = GetTickCount();
            else if (GetTickCount() - quietSince >= 150) return true;
            Sleep(5);
        }
        return host.m_thumbPending.empty();
    }
    static size_t PendingThumbs(ShellBrowserHost& host) { return host.m_thumbPending.size(); }
    static size_t CachedThumbs(ShellBrowserHost& host) { return host.m_thumbLru.size(); }
    static std::vector<std::wstring> CachedKeys(ShellBrowserHost& host) {
        std::vector<std::wstring> keys; for (const auto& entry : host.m_thumbLru) keys.push_back(entry.key); return keys;
    }
    static UINT ThumbGeneration(ShellBrowserHost& host) { return host.m_thumbGeneration.load(); }
    static bool IconCell(ShellBrowserHost& host, int index, RECT& cell) { return host.IconCell(index, cell); }
    static LRESULT DrawItem(ShellBrowserHost& host, HDC dc, int index) {
        NMLVCUSTOMDRAW draw{}; draw.nmcd.hdc = dc; draw.nmcd.dwItemSpec = index; draw.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
        return host.DrawIconItem(&draw);
    }
    // Hands the UI thread a finished thumbnail result as the worker would.
    static void Deliver(ShellBrowserHost& host, int item, const std::wstring& path, const std::wstring& key, UINT generation) {
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 8; info.bmiHeader.biHeight = -8; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        {
            std::lock_guard<std::mutex> lock(host.m_thumbMutex);
            host.m_thumbResults.push_back({key, path, bitmap, item, generation});
        }
        host.OnThumbsReady();
    }
    static bool HasThumb(ShellBrowserHost& host, const std::wstring& key) { return host.m_thumbIndex.count(key) != 0; }
    static std::wstring Key(ShellBrowserHost& host, int index, std::wstring* pathOut = nullptr) {
        IFolderView2* view = View(host); std::wstring key;
        IShellItem* item = nullptr;
        if (view && SUCCEEDED(view->GetItem(index, IID_PPV_ARGS(&item)))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &path))) {
                key = ShellBrowserHost::ThumbKey(item, path, host.m_iconSlot);
                if (pathOut) *pathOut = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
        if (view) view->Release();
        return key;
    }
    static void Request(ShellBrowserHost& host, int index) {
        IFolderView2* view = View(host); IShellItem* item = nullptr;
        if (view && SUCCEEDED(view->GetItem(index, IID_PPV_ARGS(&item)))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &path))) {
                host.RequestThumb(item, ShellBrowserHost::ThumbKey(item, path, host.m_iconSlot), path, index);
                CoTaskMemFree(path);
            }
            item->Release();
        }
        if (view) view->Release();
    }
    static void Cancel(ShellBrowserHost& host) { host.CancelThumbRequests(); }
    static void ClearThumbs(ShellBrowserHost& host) { host.ClearItemImages(); }
    // LRU on a detached host with synthetic bitmaps (bytes = 64 + w*h*4).
    static bool LruEviction() {
        ShellBrowserHost host;
        auto make = [] {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = 10; info.bmiHeader.biHeight = -10; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            void* bits = nullptr; return CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        };
        host.SetThumbnailCacheLimits(1u << 20, 3);
        for (const wchar_t* key : {L"a", L"b", L"c"}) host.StoreThumb(key, make());
        HBITMAP touched = nullptr;
        bool ok = host.LookupThumb(L"a", touched) && touched;      // a becomes most recent
        host.StoreThumb(L"d", make());                             // evicts b (least recent)
        HBITMAP probe = nullptr;
        ok = ok && !host.LookupThumb(L"b", probe) && host.LookupThumb(L"a", probe) && host.LookupThumb(L"c", probe)
            && host.LookupThumb(L"d", probe) && host.m_counters.thumbEvictions == 1 && host.m_thumbLru.size() == 3;
        // Byte cap: 464 bytes each, 1000-byte cap keeps the two most recent.
        host.SetThumbnailCacheLimits(1000, 100);
        ok = ok && host.m_thumbLru.size() == 2 && host.m_thumbBytes <= 1000 && host.LookupThumb(L"d", probe)
            && host.m_counters.thumbEvictions == 2;
        // Never a clear-all: 300 entries under a generous cap all stay.
        host.SetThumbnailCacheLimits(64u << 20, 1000);
        for (int i = 0; i < 300; ++i) host.StoreThumb(L"k" + std::to_wstring(i), make());
        ok = ok && host.m_thumbLru.size() == 302 && host.LookupThumb(L"k0", probe);
        host.ClearItemImages();
        return ok && host.m_thumbLru.empty() && host.m_thumbBytes == 0;
    }
    static bool ShellMediaThumbnail(ShellBrowserHost& host,const std::wstring& path) {
        return MediaAspectRatio(host,path,0,0);
    }
    static bool SkipsGroupThumbnail(ShellBrowserHost& host,int index) {
        host.ClearItemImages();
        NMLVCUSTOMDRAW draw{};
        draw.nmcd.dwItemSpec=index;draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;
        draw.dwItemType=LVCDI_GROUP;
        const auto before=host.m_counters;
        return host.DrawIconItem(&draw)==CDRF_DODEFAULT && host.m_thumbLru.empty()
            && host.m_counters.thumbRequests==before.thumbRequests && host.m_counters.syncExtractions==before.syncExtractions;
    }
    static bool SkipsClippedThumbnail(ShellBrowserHost& host,int index) {
        HWND list=host.m_listWindow;
        RECT bounds{};GetWindowRect(list,&bounds);
        host.ClearItemImages();
        SetWindowPos(list,nullptr,0,0,bounds.right-bounds.left,0,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        HDC dc=GetDC(list);
        NMLVCUSTOMDRAW draw{};draw.nmcd.hdc=dc;draw.nmcd.dwItemSpec=index;draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;
        const auto before=host.m_counters;
        host.DrawIconItem(&draw);
        const bool skipped=host.m_thumbLru.empty() && host.m_thumbPending.empty()
            && host.m_counters.thumbRequests==before.thumbRequests && host.m_counters.syncExtractions==before.syncExtractions;
        ReleaseDC(list,dc);
        SetWindowPos(list,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        return skipped;
    }
    static bool ContainsThumbnail(ShellBrowserHost& host,int index) {
        HWND list=host.m_listWindow;
        if(!list || !host.m_iconSlot) return false;
        RECT bounds{};GetClientRect(list,&bounds);
        HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,bounds.right,bounds.bottom);
        auto old=SelectObject(dc,bitmap);
        FillRect(dc,&bounds,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        NMLVCUSTOMDRAW draw{};draw.nmcd.hdc=dc;draw.nmcd.dwItemSpec=index;draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;
        host.DrawIconItem(&draw);
        SettleThumbs(host);
        FillRect(dc,&bounds,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        host.DrawIconItem(&draw);
        int pixels=0;
        for(int y=0;y<bounds.bottom;y+=2) for(int x=0;x<bounds.right;x+=2)
            { const COLORREF color=GetPixel(dc,x,y);
              pixels+=GetRValue(color)<80 && GetGValue(color)>60 && GetGValue(color)<150 && GetBValue(color)>140; }
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
        return pixels>100;
    }
    static void DumpVisibleView(ShellBrowserHost& host) {
        RECT bounds{};GetClientRect(host.m_listWindow,&bounds);
        std::cerr<<"native list="<<host.m_listWindow<<" visible="<<IsWindowVisible(host.m_listWindow)
            <<" size="<<bounds.right<<","<<bounds.bottom<<" count="<<(host.m_listWindow?ListView_GetItemCount(host.m_listWindow):-1)<<'\n';
        IShellView* view=nullptr;HWND root=nullptr;
        if(SUCCEEDED(host.m_browser->GetCurrentView(IID_PPV_ARGS(&view)))){view->GetWindow(&root);view->Release();}
        std::cerr<<"view root="<<root<<" visible="<<IsWindowVisible(root)<<'\n';
        for(HWND frame=root;frame;frame=GetParent(frame)) {
            wchar_t name[64]{};GetClassNameW(frame,name,_countof(name));RECT rc{};GetClientRect(frame,&rc);
            std::wcerr<<L"parent="<<name<<L" size="<<rc.right<<L","<<rc.bottom<<L" visible="<<IsWindowVisible(frame)<<L"\n";
            if(frame==host.m_parent)break;
        }
        EnumChildWindows(root,[](HWND child,LPARAM)->BOOL {
            wchar_t name[64]{};GetClassNameW(child,name,_countof(name));RECT rectangle{};GetClientRect(child,&rectangle);
            std::wcerr<<name<<L" visible="<<IsWindowVisible(child)<<L" size="<<rectangle.right<<L","<<rectangle.bottom<<L"\n";return TRUE;
        },0);
    }
    static bool ContainerVisible(ShellBrowserHost& host) {
        IShellView* view = nullptr;
        if (FAILED(host.m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) return false;
        HWND handle = nullptr; view->GetWindow(&handle); view->Release();
        if (!handle) return false;
        while (GetParent(handle) && GetParent(handle) != host.m_parent) handle = GetParent(handle);
        return (GetWindowLongPtrW(handle, GWL_STYLE) & WS_VISIBLE) != 0;
    }
    static IFolderView2* View(ShellBrowserHost& host) {
        IFolderView2* view = nullptr;
        host.m_browser->GetCurrentView(IID_PPV_ARGS(&view));
        return view;
    }
    static HRESULT ActivateSelection(ShellBrowserHost& host) {
        IShellView* view=nullptr;
        if(FAILED(host.m_browser->GetCurrentView(IID_PPV_ARGS(&view))))return E_FAIL;
        const HRESULT result=host.DefaultCommand(view);view->Release();return result;
    }
};
namespace DuiLib {
struct TabStripRegressionAccess {
    static bool RoundedIcon(const std::wstring& png) {
        CTabStripUI strip;strip.SetMetrics(144);strip.Add(L"fixture",L"图片",png,24,true);
        Gdiplus::Bitmap canvas(80,54,PixelFormat32bppARGB);
        {Gdiplus::Graphics graphics(&canvas);graphics.Clear(Gdiplus::Color(0,0,0,0));
            strip.DrawTabIcon(graphics,0,{0,0,200,54});}
        Gdiplus::Color corner,center;canvas.GetPixel(15,15,&corner);canvas.GetPixel(27,27,&center);
        return corner.GetAlpha()==0 && center.GetAlpha()==255;
    }
    static std::vector<LONG> LayoutSnapshot(DuiLib::CTabStripUI& strip) {
        std::vector<LONG> result{strip.m_scrollX, strip.m_plus.left, strip.m_plus.right};
        for (auto& tab : strip.m_tabs) {
            result.insert(result.end(), {tab.body.left, tab.body.right, tab.width});
        }
        return result;
    }
    static std::vector<DWORD> PaintPixels(CTabStripUI& strip) {
        const RECT bounds=strip.GetPos();const int width=bounds.right,height=bounds.bottom;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);void* pixels=nullptr;
        HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        std::vector<DWORD> result;
        if(bitmap && pixels) {
            auto old=SelectObject(dc,bitmap);RECT canvas{0,0,width,height};
            FillRect(dc,&canvas,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            strip.DoPaint(dc,canvas,nullptr);GdiFlush();
            result.assign(static_cast<DWORD*>(pixels),static_cast<DWORD*>(pixels)+width*height);
            SelectObject(dc,old);
        }
        if(bitmap)DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return result;
    }
    static bool NoAutomaticMotion(CTabStripUI& strip,UINT dpi) {
        strip.Clear();strip.SetMetrics(dpi);strip.SetDarkMode(false);
        strip.SetPos({0,0,1000,MulDiv(29,dpi,96)},false);
        auto stable=[&] {
            const auto layout=LayoutSnapshot(strip);
            const auto first=PaintPixels(strip);
            Sleep(180);TEventUI tick{};tick.Type=UIEVENT_TIMER;tick.wParam=1;strip.DoEvent(tick);
            return !first.empty() && first==PaintPixels(strip) && layout==LayoutSnapshot(strip);
        };
        strip.Add(L"C:\\",L"C:",L"",MulDiv(16,dpi,96),true);
        strip.Add(L"D:\\",L"Program Files",L"",MulDiv(16,dpi,96),false);
        bool ok=stable();
        strip.Insert(1,L"E:\\",L"新磁盘",L"",MulDiv(16,dpi,96));ok=stable() && ok;
        strip.SetActiveTab(1);ok=stable() && ok;
        strip.SetTabTitle(1,L"新文件夹 — longer title");ok=stable() && ok;
        strip.Reorder(1,2);ok=stable() && ok;
        strip.RemoveAt(1);ok=stable() && ok;
        const POINT plus{(strip.m_plus.left+strip.m_plus.right)/2,(strip.m_plus.top+strip.m_plus.bottom)/2};
        ok=strip.HitTest(plus).part==CTabStripUI::Part::Plus && strip.m_plus.left>strip.m_tabs.back().body.right && ok;
        for(int i=0;i<12;++i)strip.Add(std::to_wstring(i),L"Overflow folder",L"",MulDiv(16,dpi,96),true);
        strip.EnsureTabVisible(0);const int scroll=strip.m_scrollX;
        TEventUI wheel{};wheel.Type=UIEVENT_SCROLLWHEEL;wheel.wParam=SB_LINEDOWN;strip.DoEvent(wheel);
        ok=strip.m_scrollX>scroll && stable() && ok;
        return ok;
    }
    static bool SeparatorPixels(DuiLib::CTabStripUI& strip,UINT dpi,bool dark) {
        strip.Clear();strip.SetMetrics(dpi);strip.SetDarkMode(dark);
        for(int i=0;i<3;++i)strip.Add(std::to_wstring(i),L"标签",L"",MulDiv(16,dpi,96),i==2);
        const int width=MulDiv(1000,dpi,96),height=MulDiv(29,dpi,96);strip.SetPos({0,0,width,height},false);
        HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);auto old=SelectObject(dc,bitmap);
        RECT canvas{0,0,width,height};FillRect(dc,&canvas,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        strip.DoPaint(dc,canvas,nullptr);
        const int x=strip.m_tabs[0].body.right+MulDiv(UiTokens::TabCardGap,dpi,96)/2;
        const COLORREF expected=dark ? RGB(0x50,0x50,0x50):RGB(0xcf,0xcf,0xcf);
        const bool line=GetPixel(dc,x,height/2)==expected && GetPixel(dc,x,height/2- MulDiv(10,dpi,96))!=expected;
        const bool seam=strip.m_tabs[0].body.left==0 && GetPixel(dc,0,height/2)==GetPixel(dc,1,height/2);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
        strip.SetDarkMode(false);return line && seam && strip.GetFixedHeight()==height && strip.m_plus.top>=0 && strip.m_plus.bottom<=height;
    }
    static bool Check(DuiLib::CTabStripUI& strip) {
        const std::wstring longName(80,L'长');
        for(UINT dpi:{96u,144u,192u}) {
            strip.Clear();strip.SetMetrics(dpi);
            strip.SetTabWidthRange(UiTokens::TabMinW,UiTokens::TabSelMinW,UiTokens::TabMaxW);
            strip.Add(L"D:\\"+longName,L"中文 "+longName+L"尾部甲",L"",MulDiv(16,dpi,96),true);
            strip.Add(L"D:\\短",L"短",L"",MulDiv(16,dpi,96),false);
            strip.SetPos({0,0,1200,MulDiv(29,dpi,96)},false);
            const int expected=MulDiv(180,dpi,96);
            if(strip.m_tabs[0].width!=expected || strip.m_tabs[1].width!=expected)return false;
            const auto before=PaintPixels(strip);
            strip.SetTabTitle(0,L"中文 "+longName+L"尾部乙");
            if(before.empty() || before!=PaintPixels(strip))return false; // hidden suffix changes no pixels
            strip.SetActiveTab(1);
            if(strip.m_tabs[0].width!=expected || strip.m_tabs[1].width!=expected)return false;
            const auto close=strip.m_tabs[0].close;
            const auto hit=strip.HitTest({(close.left+close.right)/2,(close.top+close.bottom)/2});
            if(hit.part!=CTabStripUI::Part::Close || hit.index!=0)return false;
            TEventUI hover{};hover.Type=UIEVENT_MOUSEMOVE;
            hover.ptMouse={strip.m_tabs[0].body.left+MulDiv(15,dpi,96),strip.GetPos().bottom/2};
            strip.DoEvent(hover);
            if(std::wstring(strip.GetToolTip())!=strip.m_tabs[0].path)return false;
            strip.SetPos({0,0,MulDiv(220,dpi,96),MulDiv(29,dpi,96)},false);
            strip.SetActiveTab(0);strip.SetActiveTab(1);
            if(strip.m_tabs[0].width!=expected || strip.m_tabs[1].width!=expected
                || strip.m_scrollX<=0 || strip.m_plus.right>strip.GetPos().right)return false;
        }
        return true;
    }
};

}

// A provider that implements owner drawing through IContextMenu3 only, plus
// a legacy mode. Deliberately leaves GDI state changed like an extension can.
class ShellMenuDrawingFixture : public IContextMenu3 {
public:
    bool modern = true;
    bool modernUnavailable = false;
    HRESULT status = S_OK;
    int modernCalls = 0, legacyCalls = 0, measures = 0, draws = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IContextMenu || iid == IID_IContextMenu2 || (modern && iid == IID_IContextMenu3)) {
            *out = static_cast<IContextMenu3*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT pos, UINT first, UINT, UINT) override {
        InsertMenuW(menu, pos, MF_BYPOSITION | MF_OWNERDRAW, first, reinterpret_cast<LPCWSTR>(this));
        InsertMenuW(menu, pos + 1, MF_BYPOSITION | MF_STRING, first + 1, L"正常中文菜单");
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 2);
    }
    HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR, UINT, UINT*, LPSTR, UINT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE HandleMenuMsg(UINT message, WPARAM, LPARAM data) override {
        ++legacyCalls;
        return modern && !modernUnavailable ? E_NOTIMPL : Process(message, data, nullptr);
    }
    HRESULT STDMETHODCALLTYPE HandleMenuMsg2(UINT message, WPARAM, LPARAM data, LRESULT* result) override {
        ++modernCalls;
        return modernUnavailable ? E_NOTIMPL : Process(message, data, result);
    }
private:
    ULONG refs = 1;
    HRESULT Process(UINT message, LPARAM data, LRESULT* result) {
        if (status != S_OK) return status;
        if (result) *result = 0;
        if (message == WM_MEASUREITEM) {
            auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(data);
            item->itemWidth = 240; item->itemHeight = 36; ++measures;
            if (result) *result = TRUE;
        } else if (message == WM_DRAWITEM) {
            auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(data);
            RECT text = item->rcItem;
            DrawTextW(item->hDC, L"第三方中文菜单", -1, &text, DT_SINGLELINE | DT_VCENTER);
            ++draws;
            // The next standard item must not inherit this font, color or clip.
            SelectObject(item->hDC, GetStockObject(SYSTEM_FIXED_FONT));
            SetTextColor(item->hDC, RGB(12, 34, 56));
            SetBkMode(item->hDC, OPAQUE);
            IntersectClipRect(item->hDC, 0, 0, 1, 1);
            if (result) *result = TRUE;
        } else if (message == WM_MENUCHAR && result) {
            *result = MAKELRESULT(1, MNC_EXECUTE);
        }
        return S_OK;
    }
};

struct MainWndRegressionAccess {
    static void ClearUndoStacks(CMainWnd& window) {
        window.m_undoStack.clear();
        window.m_redoStack.clear();
    }
    inline static CMainWnd* menuCaptureOwner = nullptr;
    inline static std::wstring menuCapturePath;
    static void CALLBACK CaptureOwnedMenu(HWND, UINT, UINT_PTR timer, DWORD) {
        KillTimer(nullptr, timer);
        HWND popup = nullptr;
        EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM out)->BOOL {
            wchar_t cls[64]{}; GetClassNameW(candidate, cls, _countof(cls));
            if (IsWindowVisible(candidate) && wcscmp(cls, L"#32768") == 0) {
                *reinterpret_cast<HWND*>(out) = candidate; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&popup));
        if (popup && menuCaptureOwner) {
            RECT rect{}; GetWindowRect(popup, &rect);
            HDC screen = GetDC(popup), dc = CreateCompatibleDC(screen);
            HBITMAP bitmap = CreateCompatibleBitmap(screen, rect.right - rect.left, rect.bottom - rect.top);
            HGDIOBJ previous = SelectObject(dc, bitmap);
            const BOOL printed = PrintWindow(popup, dc, 0);
            CLSID png{};
            if (printed && menuCaptureOwner->GetPngEncoderClsid(&png)) {
                Gdiplus::Bitmap image(bitmap, nullptr);
                image.Save(menuCapturePath.c_str(), &png, nullptr);
            }
            SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(popup, screen);
        }
        EndMenu();
    }
    static int DiagnoseQuickMenu(CMainWnd& window, const std::vector<CMainWnd::QuickRow>& rows, const std::wstring& directory) {
        if (rows.empty()) return 1;
        window.StopQuickAccessSync();
        ShowWindow(window.m_hWnd, SW_SHOWNOACTIVATE);
        RECT owner{}; GetWindowRect(window.m_hWnd, &owner);
        const POINT point{owner.left + 80, owner.top + 80};
        const auto& row = rows.front();
        auto dump = [&](const char* tag, HMENU popup) {
            MENUINFO info{}; info.cbSize = sizeof(info); info.fMask = MIM_STYLE; GetMenuInfo(popup, &info);
            std::cout << tag << " style=" << info.dwStyle << " rows=" << GetMenuItemCount(popup) << '\n';
            for (int i = 0; i < GetMenuItemCount(popup); ++i) {
                wchar_t text[512]{}; MENUITEMINFOW item{}; item.cbSize = sizeof(item);
                item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_BITMAP | MIIM_STRING;
                item.dwTypeData = text; item.cch = _countof(text);
                GetMenuItemInfoW(popup, i, TRUE, &item);
                char utf8[2048]{}; WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
                std::cout << "  " << i << " id=" << item.wID << " type=" << item.fType
                    << " callback=" << (item.hbmpItem == HBMMENU_CALLBACK) << " text=" << utf8 << '\n';
            }
        };
        menuCaptureOwner = &window;
        for (int variant = 0; variant < 3; ++variant) {
            IContextMenu* menu = nullptr; HMENU popup = nullptr; UINT last = 0;
            if (variant < 2) {
                auto* absolute = reinterpret_cast<PCIDLIST_ABSOLUTE>(row.shellId.data());
                IShellFolder* parent = nullptr; PCUITEMID_CHILD child = nullptr;
                if (FAILED(SHBindToParent(absolute, IID_PPV_ARGS(&parent), &child))) return 1;
                const HRESULT ui = parent->GetUIObjectOf(window.m_hWnd, 1, &child, IID_IContextMenu, nullptr, reinterpret_cast<void**>(&menu));
                parent->Release(); if (FAILED(ui)) return 1;
                popup = CreatePopupMenu();
                const HRESULT queried = menu->QueryContextMenu(popup, 0, 1, 0x7fff, CMF_NORMAL);
                if (FAILED(queried)) { DestroyMenu(popup); menu->Release(); return 1; }
                last = 1 + HRESULT_CODE(queried);
            } else if (!window.BuildShellItemMenu({row.path}, &menu, &popup, &last)) return 1;
            const char* tag = variant == 0 ? "home-raw" : variant == 1 ? "home-current" : "filesystem";
            dump(tag, popup);
            menuCapturePath = directory + (variant == 0 ? L"\\home-raw.png" : variant == 1 ? L"\\home-current.png" : L"\\filesystem.png");
            const UINT_PTR timer = SetTimer(nullptr, 0, 350, CaptureOwnedMenu);
            if (variant == 0) {
                // Untouched provider layout: no separator normalization or additions.
                window.m_pCtxMenu = menu;
                menu->QueryInterface(IID_PPV_ARGS(&window.m_pCtxMenu2));
                menu->QueryInterface(IID_PPV_ARGS(&window.m_pCtxMenu3));
                TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, window.m_hWnd, nullptr);
                if (window.m_pCtxMenu3) window.m_pCtxMenu3->Release();
                if (window.m_pCtxMenu2) window.m_pCtxMenu2->Release();
                window.m_pCtxMenu = nullptr; window.m_pCtxMenu2 = nullptr; window.m_pCtxMenu3 = nullptr;
            } else {
                window.TrackPopupShellMenu(menu, popup, point, 1, last);
            }
            KillTimer(nullptr, timer); dump(tag, popup);
            DestroyMenu(popup); menu->Release();
        }
        menuCaptureOwner = nullptr;
        return 0;
    }
    static int CheckShellMenuDrawing(CMainWnd& window) {
        int failures = 0;
        auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
        HDC screen = GetDC(window.m_hWnd), dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 260, 48);
        HGDIOBJ previous = SelectObject(dc, bitmap);
        for (bool modern : {true, false}) {
            ShellMenuDrawingFixture menu; menu.modern = modern;
            window.m_pCtxMenu2 = &menu;
            window.m_pCtxMenu3 = modern ? &menu : nullptr;
            MEASUREITEMSTRUCT measure{}; measure.CtlType = ODT_MENU;
            LRESULT result = -7; bool handled = false;
            window.ForwardShellMenuMessage(WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure), &result, &handled);
            check(handled && result == TRUE && measure.itemHeight == 36 && measure.itemWidth == 240,
                modern ? "IContextMenu3 measures owner-drawn rows and returns its result" : "IContextMenu2 measurement returns TRUE");
            DRAWITEMSTRUCT draw{}; draw.CtlType = ODT_MENU; draw.hDC = dc; draw.rcItem = {0, 0, 260, 48};
            RECT clipBefore{}, clipAfter{}; GetClipBox(dc, &clipBefore);
            const auto font = GetCurrentObject(dc, OBJ_FONT);
            const auto color = GetTextColor(dc); const int mode = GetBkMode(dc);
            result = -7; handled = false;
            window.ForwardShellMenuMessage(WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw), &result, &handled);
            GetClipBox(dc, &clipAfter);
            check(handled && result == TRUE && menu.draws == 1, "menu drawing is handled exactly once");
            check(GetCurrentObject(dc, OBJ_FONT) == font && GetTextColor(dc) == color && GetBkMode(dc) == mode
                && EqualRect(&clipBefore, &clipAfter), "extension drawing cannot leak GDI state into following rows");
            if (modern) {
                result = -7; handled = false;
                window.ForwardShellMenuMessage(WM_MENUCHAR, 0, 0, &result, &handled);
                check(handled && result == MAKELRESULT(1, MNC_EXECUTE) && menu.legacyCalls == 0,
                    "modern menu keyboard result is preserved without dispatching legacy messages");
            }
            const int before = menu.modernCalls + menu.legacyCalls;
            measure.CtlType = ODT_BUTTON; handled = false;
            window.ForwardShellMenuMessage(WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure), &result, &handled);
            check(!handled && before == menu.modernCalls + menu.legacyCalls, "menu routing leaves owner-drawn controls alone");
            measure.CtlType = ODT_MENU; menu.status = S_FALSE; handled = false;
            window.ForwardShellMenuMessage(WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure), &result, &handled);
            check(!handled, "S_FALSE is not reported as a handled menu message");
            if (modern) {
                menu.status = S_OK; menu.modernUnavailable = true;
                measure.itemHeight = 0; handled = false; result = -7;
                window.ForwardShellMenuMessage(WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure), &result, &handled);
                check(handled && result == TRUE && measure.itemHeight == 36,
                    "unimplemented modern message falls back to the available legacy handler");
            }
            window.m_pCtxMenu2 = nullptr; window.m_pCtxMenu3 = nullptr;
            // Baseline implementation intentionally leaks state; reset the test DC
            // so the next case still starts with a valid normal drawing context.
            SelectClipRgn(dc, nullptr); SelectObject(dc, font); SetTextColor(dc, color); SetBkMode(dc, mode);
        }
        // Exercise the actual TrackPopupMenu message loop with the modern-only
        // provider: cancelled popup never invokes a user command.
        const bool shown = IsWindowVisible(window.m_hWnd) != FALSE;
        if (!shown) ShowWindow(window.m_hWnd, SW_SHOWNOACTIVATE);
        ShellMenuDrawingFixture popupMenu;
        HMENU popup = CreatePopupMenu(); popupMenu.QueryContextMenu(popup, 0, 1, 0x7fff, CMF_NORMAL);
        // Shell extensions may replace separator-only lazy submenu placeholders
        // on WM_INITMENUPOPUP. The host must not empty those provider-owned menus.
        HMENU lazySubmenu = CreatePopupMenu();
        AppendMenuW(lazySubmenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(lazySubmenu), L"延迟加载子菜单");
        RECT owner{}; GetWindowRect(window.m_hWnd, &owner);
        const UINT_PTR timer = SetTimer(nullptr, 0, 250, [](HWND, UINT, UINT_PTR id, DWORD) { EndMenu(); KillTimer(nullptr, id); });
        check(timer != 0, "owner-draw popup cancellation timer starts");
        if (timer) {
            window.TrackPopupShellMenu(&popupMenu, popup, {owner.left + 80, owner.top + 80}, 1, 3);
            KillTimer(nullptr, timer);
            check(popupMenu.measures > 0 && popupMenu.draws > 0 && popupMenu.legacyCalls == 0,
                "real popup measures and paints modern-only Chinese owner-drawn entries");
            check(!window.m_pCtxMenu2 && !window.m_pCtxMenu3, "popup releases active drawing interfaces on close");
            check(GetMenuItemCount(lazySubmenu) == 1,
                "popup preserves provider-owned lazy submenu placeholders");
        }
        DestroyMenu(popup);
        if (!shown) ShowWindow(window.m_hWnd, SW_HIDE);
        SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window.m_hWnd, screen);
        return failures;
    }
    static void ConfigureQuickFixture(CMainWnd& window, const std::wstring& source) {
        window.m_quickReadSource = source;
    }
    static bool WaitQuickFixture(CMainWnd& window) {
        const ULONGLONG deadline = GetTickCount64() + 10000;
        while (window.m_quickReadPending && GetTickCount64() < deadline) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message != WM_QUIT && !CPaintManagerUI::TranslateMessage(&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            }
            Sleep(10);
        }
        return !window.m_quickReadPending;
    }
    static int CheckSystemQuickAccess(CMainWnd& window, const std::wstring& fixture) {
        int failures = 0;
        auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
        const ULONGLONG deadline = GetTickCount64() + 10000;
        while (window.m_quickReadPending && GetTickCount64() < deadline) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            Sleep(10);
        }
        check(!window.m_quickReadPending, "background Shell snapshot completes");
        window.StopQuickAccessSync();
        std::vector<CMainWnd::QuickRow> systemRows;
        const HRESULT readSystem = CMainWnd::ReadSystemQuickRows(systemRows);
        check(SUCCEEDED(readSystem), "read live Windows Home namespace without changing pins or privacy");
        wchar_t captureDirectory[32768]{};
        if (GetEnvironmentVariableW(L"FASTFILE_QUICK_MENU_DIAGNOSTICS", captureDirectory, _countof(captureDirectory)))
            return DiagnoseQuickMenu(window, systemRows, captureDirectory);
        IShellDispatch* systemShell = nullptr;
        Folder* systemFolder = nullptr;
        FolderItems* systemItems = nullptr;
        VARIANT home{}; home.vt = VT_BSTR;
        home.bstrVal = SysAllocString(L"shell:::{f874310e-b6b7-47dc-bc84-b9e6b38f5903}");
        HRESULT automation = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&systemShell));
        if (SUCCEEDED(automation)) automation = systemShell->NameSpace(home, &systemFolder);
        if (SUCCEEDED(automation) && systemFolder) automation = systemFolder->Items(&systemItems);
        long systemCount = -1;
        if (systemItems) automation = systemItems->get_Count(&systemCount);
        check(SUCCEEDED(automation) && systemCount == long(systemRows.size()),
            "live snapshot count matches independent Shell.Application Home enumeration");
        if (systemItems && systemCount == long(systemRows.size())) {
            for (long i = 0; i < systemCount; ++i) {
                VARIANT index{}; index.vt = VT_I4; index.lVal = i;
                FolderItem* item = nullptr;
                BSTR name = nullptr; VARIANT_BOOL isFolder = VARIANT_FALSE;
                HRESULT itemHr = systemItems->Item(index, &item);
                if (SUCCEEDED(itemHr) && item) itemHr = item->get_Name(&name);
                if (item) item->get_IsFolder(&isFolder);
                check(SUCCEEDED(itemHr) && name && systemRows[size_t(i)].label == name
                    && systemRows[size_t(i)].isFolder == (isFolder != VARIANT_FALSE),
                    "live ordering, names and item types match the independent system enumeration");
                if (name) SysFreeString(name);
                if (item) item->Release();
            }
        }
        std::cout << "System Home snapshot: " << systemRows.size() << " entries (paths omitted)\n";
        if (systemItems) systemItems->Release();
        if (systemFolder) systemFolder->Release();
        if (systemShell) systemShell->Release();
        VariantClear(&home);
        if (!systemRows.empty()) {
            CMainWnd::QuickSnapshot live; live.result = S_OK; live.rows = systemRows;
            window.ApplyQuickSnapshot(live);
            check(!window.m_quickRows.empty() && window.m_quickRows.front().shellId == systemRows.front().shellId,
                "live quick row popup keeps the Windows Home parent identity");
            const bool shown = IsWindowVisible(window.m_hWnd) != FALSE;
            if (!shown) ShowWindow(window.m_hWnd, SW_SHOWNOACTIVATE);
            RECT owner{}; GetWindowRect(window.m_hWnd, &owner);
            const UINT_PTR timer = SetTimer(nullptr, 0, 250, [](HWND, UINT, UINT_PTR id, DWORD) { EndMenu(); KillTimer(nullptr, id); });
            check(timer != 0, "live Home popup cancellation timer starts");
            if (timer) {
                // Query and paint only; never choose a verb on a user item.
                window.ShowQuickRowContextMenu(0, {owner.left + 80, owner.top + 80});
                KillTimer(nullptr, timer);
                check(!window.m_pCtxMenu2 && !window.m_pCtxMenu3, "real Home row menu closes with drawing interfaces cleared");
            }
            {
                // Same row through the track seam: exactly the Windows Home item menu, no extras.
                RawShellMenu raw; bool same = false; int calls = 0;
                const UINT flags = CMainWnd::ShellItemMenuFlags(false, false);
                if (raw.Item(reinterpret_cast<PCIDLIST_ABSOLUTE>(window.m_quickRows.front().shellId.data()), window.m_hWnd)) {
                    if (window.m_shellBrowser) window.m_shellBrowser->SiteContextMenu(raw.menu);
                    raw.Query(flags);
                }
                window.m_trackMenuHook = [&](IContextMenu*, HMENU shownPopup) -> UINT {
                    ++calls; same = raw.popup && SameShellMenu(shownPopup, raw.popup); return 0u;
                };
                window.ShowQuickRowContextMenu(0, {owner.left + 80, owner.top + 80});
                window.m_trackMenuHook = nullptr;
                check(calls == 1 && same && window.m_lastShellMenuFlags == flags,
                    "live Quick Access row menu is the unmodified Windows Home item menu (no FastFile extras)");
            }
            if (!shown) ShowWindow(window.m_hWnd, SW_HIDE);
        }
        // Only this run's freshly-created temporary directory is changed. Never
        // unpin existing user items; the guard removes our own pin on every exit.
        const std::wstring pinFixture = fixture + L"\\FastFile-QuickAccess-Pin-Test";
        CreateDirectoryW(pinFixture.c_str(), nullptr);
        auto findPin = [&](std::vector<CMainWnd::QuickRow>& entries) -> int {
            for (size_t i = 0; i < entries.size(); ++i)
                if (CMainWnd::PathEquals(entries[i].path, pinFixture) && entries[i].pinned) return int(i);
            return -1;
        };
        auto removeOwnedPin = [&] {
            std::vector<CMainWnd::QuickRow> entries;
            if (SUCCEEDED(CMainWnd::ReadSystemQuickRows(entries))) {
                const int index = findPin(entries);
                if (index >= 0) return CMainWnd::InvokeQuickVerb(nullptr, entries[size_t(index)].shellPath,
                    "unpinfromhome", entries[size_t(index)].shellId);
            }
            return S_FALSE;
        };
        auto cleanup = std::unique_ptr<int, std::function<void(int*)>>(reinterpret_cast<int*>(1), [&](int*) { removeOwnedPin(); });
        const HRESULT pinResult = CMainWnd::InvokeQuickVerb(nullptr, pinFixture, "pintohome");
        check(SUCCEEDED(pinResult), "real system pin verb accepts only the owned temporary fixture");
        int pinnedIndex = -1;
        const ULONGLONG pinDeadline = GetTickCount64() + 8000;
        do {
            std::vector<CMainWnd::QuickRow> entries;
            if (SUCCEEDED(CMainWnd::ReadSystemQuickRows(entries))) pinnedIndex = findPin(entries);
            if (pinnedIndex >= 0 || FAILED(pinResult)) break;
            Sleep(50);
        } while (GetTickCount64() < pinDeadline);
        check(pinnedIndex >= 0, "pin is visible through the system snapshot with its native pinned state");
        check(SUCCEEDED(removeOwnedPin()), "native Home identity can unpin the owned fixture");
        std::vector<CMainWnd::QuickRow> afterUnpin;
        CMainWnd::ReadSystemQuickRows(afterUnpin);
        check(findPin(afterUnpin) < 0, "owned pin is removed from system state");
        RemoveDirectoryW(pinFixture.c_str());
        std::vector<CMainWnd::QuickRow> native;
        check(SUCCEEDED(CMainWnd::ReadSystemQuickRows(native, fixture)), "enumerate real Shell fixture");
        bool folder = false, file = false;
        for (const auto& row : native) {
            folder = folder || row.isFolder;
            file = file || !row.isFolder;
            check(!row.path.empty() && !row.label.empty() && !row.shellPath.empty() && !row.shellId.empty(), "retain Shell identity and display name");
        }
        check(folder && file, "one snapshot contains folders and recent-file-shaped entries");
        auto before = native;
        check(FAILED(CMainWnd::ReadSystemQuickRows(native, fixture + L"\\missing")) && native.size() == before.size(),
            "read failure leaves caller's valid snapshot intact");
        CMainWnd::QuickSnapshot snapshot; snapshot.result = S_OK; snapshot.rows = before;
        window.ApplyQuickSnapshot(snapshot);
        check(window.m_quickRows.size() == before.size() && window.m_pLeftQuickRows->GetCount() == int(before.size()),
            "UI has exactly Shell rows without mandatory default folders");
        for (size_t i = 0; i < before.size(); ++i)
            check(window.m_quickRows[i].shellPath == before[i].shellPath
                && std::wstring(window.m_pLeftQuickRows->GetItemAt(int(i))->GetText()) == before[i].label,
                "Shell order and localized names survive UI projection");
        auto* first = before.empty() ? nullptr : window.m_pLeftQuickRows->GetItemAt(0);
        window.ApplyQuickSnapshot(snapshot);
        check(before.empty() || first == window.m_pLeftQuickRows->GetItemAt(0), "unchanged snapshot does not rebuild controls");
        window.MoveQuickRow(0, 1);
        check(before.empty() || window.m_quickRows[0].shellPath == before[0].shellPath, "local dragging cannot change Shell order");
        window.m_quickDragIndex = 0;
        snapshot.rows.clear(); window.ApplyQuickSnapshot(snapshot);
        check(window.m_quickRows.size() == before.size(), "background refresh cannot destroy a pressed row or change its click target");
        window.m_quickDragIndex = -1;
        window.m_inDoDragDrop = true; window.ApplyQuickSnapshot(snapshot);
        check(window.m_quickRows.size() == before.size(), "background refresh waits for nested OLE interaction to finish");
        window.m_inDoDragDrop = false;
        snapshot.result = E_FAIL; snapshot.rows.clear(); window.ApplyQuickSnapshot(snapshot);
        check(window.m_quickRows.size() == before.size(), "failure does not clear rendered snapshot");
        snapshot.result = S_FALSE; window.ApplyQuickSnapshot(snapshot);
        window.EnsureDefaultQuickRows();
        check(window.m_quickRows.empty() && window.m_pLeftQuickRows->GetCount() == 0,
            "privacy-filtered empty results stay empty without resurrecting default pins");
        snapshot.result = S_OK; snapshot.rows = before;
        if (!snapshot.rows.empty()) snapshot.rows[0].pinned = true;
        window.ApplyQuickSnapshot(snapshot);
        if (!snapshot.rows.empty()) check(window.IsQuickAccessPinned(snapshot.rows[0].path), "only explicitly pinned entries are pinned");
        snapshot.rows.insert(snapshot.rows.end(), before.begin(), before.end());
        snapshot.rows.insert(snapshot.rows.end(), before.begin(), before.end());
        snapshot.rows.insert(snapshot.rows.end(), before.begin(), before.end());
        window.ApplyQuickSnapshot(snapshot);
        check(window.m_pLeftQuick->GetMinHeight() <= window.DpiScale(256), "recent files cannot grow sidebar beyond scrollable region");
        check(FAILED(window.InvokeQuickVerb(nullptr, fixture, "fastfile_missing_verb")), "unavailable canonical verbs are rejected");
        snapshot.rows = before; window.ApplyQuickSnapshot(snapshot);
        window.m_quickReadStopping = false;
        window.LoadQuickAccess();
        const ULONGLONG end = GetTickCount64() + 10000;
        while (window.m_quickReadPending && GetTickCount64() < end) {
            MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            Sleep(10);
        }
        check(!window.m_quickReadPending && window.m_quickRows.size() == before.size(), "async refresh restores the authoritative Shell snapshot");
        return failures;
    }
    static int CheckTabs(CMainWnd& window) { return DuiLib::TabStripRegressionAccess::Check(*window.m_pTabStrip)?0:1; }
    static int RunIsolatedAgent() {
        // Real Explorer lives on the interactive default desktop; fixture GUI is isolated.
        HDESK sourceDesktop=OpenDesktopW(L"Default",0,FALSE,GENERIC_ALL);
        if(!sourceDesktop || !SetThreadDesktop(sourceDesktop))return 4;
        wchar_t root[32768]{};GetEnvironmentVariableW(L"APPDATA",root,_countof(root));
        CMainWnd::s_explorerTestRoot=root;
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,_countof(executable));
        CMainWnd::s_agentInterfaceForTest=executable;
        CMainWnd::s_agentArgumentsForTest=L" --agent-ui-launcher";
        wchar_t desktop[256]{};GetEnvironmentVariableW(L"FASTFILE_TEST_DESKTOP",desktop,_countof(desktop));
        CMainWnd::s_agentDesktopForTest=OpenDesktopW(desktop,0,FALSE,GENERIC_ALL);
        const int result=CMainWnd::RunExplorerAgent();
        if(CMainWnd::s_agentDesktopForTest)CloseDesktop(CMainWnd::s_agentDesktopForTest);
        CMainWnd::s_agentDesktopForTest=nullptr;CloseDesktop(sourceDesktop);return result;
    }
    static int CheckShellActivation(CMainWnd& window,const std::wstring& fixture) {
        int failures=0;
        auto check=[&](bool value,const char* name){if(!value){++failures;std::cerr<<"FAIL "<<name<<"\n";}};
        wchar_t hiveName[256]{};GetEnvironmentVariableW(L"FASTFILE_TEST_HIVE",hiveName,_countof(hiveName));
        HKEY user=nullptr,classes=nullptr;
        check(RegCreateKeyExW(HKEY_CURRENT_USER,(std::wstring(hiveName)+L"\\User").c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&user,nullptr)==ERROR_SUCCESS,
            "activation test creates an isolated user hive");
        check(RegCreateKeyExW(HKEY_CURRENT_USER,(std::wstring(hiveName)+L"\\Classes").c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&classes,nullptr)==ERROR_SUCCESS,
            "activation test creates isolated Shell verbs");
        if(!user || !classes)return failures+1;
        if(RegOverridePredefKey(HKEY_CURRENT_USER,user)!=ERROR_SUCCESS) {
            RegCloseKey(classes);RegCloseKey(user);return failures+1;
        }
        FastFileSettings prefs=window.m_settings;prefs.contextMenu=true;prefs.defaultFolders=true;prefs.confirmClose=false;prefs.externalNewWindow=false;
        check(CMainWnd::ApplySystemIntegration(prefs),"production integration registers the explicit FastFile verb");
        // FastFile is the default folder handler in this isolated hive, so the window
        // registers itself in the (real) Shell window list; revoked when it closes.
        CMainWnd::s_shellWindowRegistrationAllowed=true;
        window.UpdateShellWindowRegistration();
        check(window.m_shellWindow && window.m_shellWindow->IsRegistered(),"default folder handler window registers as a Shell window");
        window.m_settings=prefs;prefs.Save(FastFileSettings::FilePath());
        DWORD disabled=0;HKEY state=nullptr;
        RegCreateKeyExW(user,L"Software\\FastFile",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&state,nullptr);
        RegSetValueExW(state,L"FolderHandlerEnabled",0,REG_DWORD,reinterpret_cast<const BYTE*>(&disabled),sizeof(disabled));RegCloseKey(state);
        // Resolve the real production registration; only add the test harness
        // switch so the subprocess isolates its desktop/registry before entry.
        for(const auto* cls:{L"Directory",L"Drive"})for(const auto* action:{L"FastFile.SettingsOpen",L"open",L"explore",L"opennewwindow"}) {
            const auto key=std::wstring(cls)+L"\\shell\\"+action;
            wchar_t command[32768]{};DWORD bytes=sizeof(command);
            check(RegGetValueW(user,(L"Software\\Classes\\"+key+L"\\command").c_str(),nullptr,RRF_RT_REG_SZ,nullptr,command,&bytes)==ERROR_SUCCESS,
                "explicit verb has a registered launch command");
            std::wstring launch=command;const auto quote=launch.find(L'"',1);
            if(quote!=std::wstring::npos)launch.insert(quote+1,L" --shell-activation-launcher");
            HKEY verb=nullptr;RegCreateKeyExW(classes,(key+L"\\command").c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&verb,nullptr);
            RegSetValueExW(verb,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(launch.c_str()),DWORD((launch.size()+1)*sizeof(wchar_t)));
            const wchar_t empty[]=L"";RegSetValueExW(verb,L"DelegateExecute",0,REG_SZ,reinterpret_cast<const BYTE*>(empty),sizeof(empty));RegCloseKey(verb);
            // Same default verb as the production registration (FastFile opens folders).
            const std::wstring defaultVerb=L"FastFile.SettingsOpen";HKEY shell=nullptr;
            RegCreateKeyExW(classes,(std::wstring(cls)+L"\\shell").c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&shell,nullptr);
            RegSetValueExW(shell,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(defaultVerb.c_str()),DWORD((defaultVerb.size()+1)*sizeof(wchar_t)));RegCloseKey(shell);
        }
        if(RegOverridePredefKey(HKEY_CLASSES_ROOT,classes)!=ERROR_SUCCESS) {
            RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);RegCloseKey(classes);RegCloseKey(user);return failures+1;
        }
        auto pump=[](){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
        }};
        auto invokeAction=[&](const std::wstring& path,const wchar_t* cls,const wchar_t* action) {
            SHELLEXECUTEINFOW call{};call.cbSize=sizeof(call);call.fMask=SEE_MASK_CLASSNAME|SEE_MASK_NOASYNC|SEE_MASK_NOCLOSEPROCESS;
            call.lpClass=cls;call.lpVerb=action;call.lpFile=path.c_str();call.nShow=SW_HIDE;
            check(ShellExecuteExW(&call)!=FALSE && call.hProcess,"Windows executes the registered FastFile context-menu verb");
            return call.hProcess;
        };
        auto invoke=[&](const std::wstring& path,const wchar_t* cls) {
            return invokeAction(path,cls,L"FastFile.SettingsOpen");
        };
        for(const auto* cls:{L"Directory",L"Drive"})for(const auto* action:{L"open",L"explore",L"opennewwindow"}) {
            const auto target=wcscmp(cls,L"Drive")==0?fixture.substr(0,3):fixture;
            HANDLE child=invokeAction(target,cls,action);if(!child)continue;
            const DWORD deadline=GetTickCount()+6000;
            while(GetTickCount()<deadline && (WaitForSingleObject(child,0)==WAIT_TIMEOUT || !CMainWnd::PathEquals(window.m_currentPath,target))) {pump();Sleep(5);}
            DWORD result=STILL_ACTIVE;GetExitCodeProcess(child,&result);
            check(result==0 && CMainWnd::PathEquals(window.m_currentPath,target),"explicit Windows open/explore/opennewwindow reaches the FastFile instance");
            if(result==STILL_ACTIVE)TerminateProcess(child,3);CloseHandle(child);
        }
        const DWORD observeUntil=GetTickCount()+25000;
        int duplicateWindows=0;
        do {
            pump();const int observed=ExplorerWindowsUnder(fixture,window.m_hWnd);
            if(observed<0){duplicateWindows=-1;break;}
            duplicateWindows+=observed;Sleep(250);
        }while(GetTickCount()<observeUntil);
        check(duplicateWindows==0,"explicit standard actions do not open a delayed Explorer window during 25 seconds");
        for(const auto& sample:{std::pair<std::wstring,const wchar_t*>{fixture,L"Directory"},
            {fixture.substr(0,3),L"Drive"},{fixture+L"\\Battle.net",L"Directory"}}) {
            HANDLE child=invoke(sample.first,sample.second);if(!child)continue;
            const DWORD deadline=GetTickCount()+6000;
            while(GetTickCount()<deadline && (WaitForSingleObject(child,0)==WAIT_TIMEOUT || !CMainWnd::PathEquals(window.m_currentPath,sample.first))) {pump();Sleep(5);}
            DWORD result=STILL_ACTIVE;GetExitCodeProcess(child,&result);
            check(result==0,"context-menu launcher forwards to the existing instance and exits");
            check(CMainWnd::PathEquals(window.m_currentPath,sample.first),"explicit FastFile menu reaches FastFile with the legacy disable flag set");
            if(result==STILL_ACTIVE)TerminateProcess(child,3);CloseHandle(child);
        }
        failures+=CheckBusyAndSlowActivation(window,fixture,invoke,pump);
        // Cold start: no receiver exists. Verify the production entry creates a
        // window for exactly the requested folder and saves that tab on close.
        RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr);
        DestroyWindow(window.m_hWnd);
        pump(); // Consume the closed host's WM_QUIT before ShellExecuteEx waits.
        const auto coldFile=fixture+L"\\115Chrome\\cold-visible.txt";
        HANDLE coldFixture=CreateFileW(coldFile.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(coldFixture!=INVALID_HANDLE_VALUE,"cold-start enumeration fixture is created");
        if(coldFixture!=INVALID_HANDLE_VALUE)CloseHandle(coldFixture);
        RegOverridePredefKey(HKEY_CLASSES_ROOT,classes);
        HANDLE cold=invokeAction(fixture+L"\\115Chrome",L"Directory",L"opennewwindow");
        if(cold) {
            HWND opened=nullptr;const DWORD deadline=GetTickCount()+6000;
            while(GetTickCount()<deadline && !opened){
                pump();HWND candidate=FindWindowW(L"FastFile_MainWnd",nullptr);DWORD pid=0;
                if(candidate)GetWindowThreadProcessId(candidate,&pid);
                // The startup folder resolves off the UI thread; close only after it opened.
                if(candidate && pid==GetProcessId(cold) && IsWindowVisible(candidate)
                    && GetPropW(candidate,L"FastFile.Test.Initialized")
                    && !GetPropW(candidate,L"FastFile.ExternalOpenPending"))opened=candidate;
                Sleep(5);
            }
            check(opened!=nullptr,"explicit menu cold-starts a FastFile window");
            bool listed=false;
            if(opened) {
                const ULONGLONG listedDeadline=GetTickCount64()+5000;
                do {
                    pump();HWND list=nullptr;
                    EnumChildWindows(opened,[](HWND child,LPARAM data)->BOOL {
                        wchar_t type[64]{};GetClassNameW(child,type,_countof(type));
                        if(wcscmp(type,WC_LISTVIEWW)==0){*reinterpret_cast<HWND*>(data)=child;return FALSE;}
                        return TRUE;
                    },reinterpret_cast<LPARAM>(&list));
                    RECT bounds{};if(list)GetWindowRect(list,&bounds);
                    listed=list && IsWindowVisible(list) && bounds.right>bounds.left && bounds.bottom>bounds.top
                        && SendMessageW(list,LVM_GETITEMCOUNT,0,0)==1;
                    if(listed)break;Sleep(5);
                }while(GetTickCount64()<listedDeadline);
                // Closing exits the UI even while default integration is enabled.
                PostMessageW(opened,WM_CLOSE,0,0);

            }
            check(listed,"cold-started native file view actually displays the file, not only the requested path");
            const DWORD closeDeadline=GetTickCount()+6000;
            while(GetTickCount()<closeDeadline && WaitForSingleObject(cold,0)==WAIT_TIMEOUT){pump();Sleep(5);}
            DWORD result=STILL_ACTIVE;GetExitCodeProcess(cold,&result);
            check(result==0,"cold-started FastFile closes normally");
            const DWORD coldPid=GetProcessId(cold);
            if(result==STILL_ACTIVE)TerminateProcess(cold,3);CloseHandle(cold);
            wchar_t explicitLog[32768]{};GetEnvironmentVariableW(L"FASTFILE_NAV_LOG",explicitLog,_countof(explicitLog));
            auto logPath=CMainWnd::GetSessionFilePath();logPath=logPath.substr(0,logPath.find_last_of(L'\\')+1)+L"navigation.log";
            if(explicitLog[0])logPath=explicitLog;
            std::ifstream navigationLog(logPath,std::ios::binary);std::string line;int requests=0;
            const std::string pidToken="pid="+std::to_string(coldPid)+" ";
            while(std::getline(navigationLog,line))
                if(line.find(pidToken)!=std::string::npos && line.find("event=request ")!=std::string::npos)++requests;
            check(requests==1,"external cold start issues only one navigation, directly to the requested folder");
            wchar_t saved[32768]{};GetPrivateProfileStringW(L"Tabs",L"Path0",L"",saved,_countof(saved),CMainWnd::GetSessionFilePath().c_str());
            check(CMainWnd::PathEquals(saved,fixture+L"\\115Chrome"),"cold start opens the requested folder rather than Explorer or the default location");
        }
        DeleteFileW(coldFile.c_str());
        RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr);
        FastFileSettings agentSettings;agentSettings.SetDefaultManager(true);
        check(CMainWnd::ApplySystemIntegration(agentSettings),"enable isolated agent lifecycle flags");
        auto spawnAgent=[]() -> HANDLE {
            wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,_countof(path));
            auto command=L"\""+std::wstring(path)+L"\" --agent-lifecycle-launcher";
            STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
            if(!CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child))return nullptr;
            CloseHandle(child.hThread);return child.hProcess;
        };
        HANDLE agent=spawnAgent();
        check(agent!=nullptr,"headless agent starts in isolated profile");
        if(agent) {
            Sleep(1500);
            check(WaitForSingleObject(agent,0)==WAIT_TIMEOUT,"enabled agent remains running without GUI");
            struct AgentWindows { DWORD pid;int count=0; } owned{GetProcessId(agent)};
            EnumWindows([](HWND hwnd,LPARAM value)->BOOL {
                auto& owned=*reinterpret_cast<AgentWindows*>(value);DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
                if(pid==owned.pid)++owned.count;return TRUE;
            },reinterpret_cast<LPARAM>(&owned));
            check(owned.count==0,"agent creates no top-level interaction window");
            HANDLE duplicate=spawnAgent();
            if(duplicate) {
                check(WaitForSingleObject(duplicate,5000)==WAIT_OBJECT_0,"second agent exits under profile singleton");
                DWORD code=1;GetExitCodeProcess(duplicate,&code);check(code==0,"duplicate agent exits successfully");
                if(code==STILL_ACTIVE)TerminateProcess(duplicate,3);CloseHandle(duplicate);
            } else check(false,"duplicate agent starts");
            FastFileSettings off;check(CMainWnd::ApplySystemIntegration(off),"disable isolated agent flags");
            check(WaitForSingleObject(agent,6000)==WAIT_OBJECT_0,"agent exits after integration is disabled");
            DWORD code=1;GetExitCodeProcess(agent,&code);check(code==0,"agent shuts down cleanly");
            if(code==STILL_ACTIVE)TerminateProcess(agent,3);CloseHandle(agent);
        }

        RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr);RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);
        RegCloseKey(classes);RegCloseKey(user);RegDeleteTreeW(HKEY_CURRENT_USER,hiveName);
        return failures;
    }
    // Counts Explorer windows (other than FastFile's own registration) showing a folder
    // below root. Read-only enumeration of the Shell window list.
    static int ExplorerWindowsUnder(const std::wstring& root,HWND own,bool cleanupOwned=false) {
        IShellWindows* windows=nullptr;int found=0;
        if(FAILED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&windows))))return -1;
        long count=0;if(FAILED(windows->get_Count(&count))){windows->Release();return -1;}
        for(long i=0;i<count;++i) {
            VARIANT index{};index.vt=VT_I4;index.lVal=i;IDispatch* item=nullptr;
            if(FAILED(windows->Item(index,&item)) || !item)continue;
            IWebBrowserApp* app=nullptr;SHANDLE_PTR hwnd=0;
            if(SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&app)))){app->get_HWND(&hwnd);app->Release();}
            IServiceProvider* provider=nullptr;IShellBrowser* browser=nullptr;IShellView* view=nullptr;
            IFolderView* folderView=nullptr;IPersistFolder2* folder=nullptr;PIDLIST_ABSOLUTE pidl=nullptr;
            if(reinterpret_cast<HWND>(hwnd)!=own && SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&provider)))
                && SUCCEEDED(provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser)))
                && SUCCEEDED(browser->QueryActiveShellView(&view))
                && SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&folderView)))
                && SUCCEEDED(folderView->GetFolder(IID_PPV_ARGS(&folder)))
                && SUCCEEDED(folder->GetCurFolder(&pidl))) {
                wchar_t path[MAX_PATH*4]{};
                if(SHGetPathFromIDListEx(pidl,path,_countof(path),GPFIDL_DEFAULT)
                    && _wcsnicmp(path,root.c_str(),root.size())==0) {
                    ++found;if(cleanupOwned && root.find(L"FastFileUIRegression_")!=std::wstring::npos)
                        PostMessageW(reinterpret_cast<HWND>(hwnd),WM_CLOSE,0,0);
                }
            }
            if(pidl)CoTaskMemFree(pidl);if(folder)folder->Release();if(folderView)folderView->Release();
            if(view)view->Release();if(browser)browser->Release();if(provider)provider->Release();item->Release();
        }
        windows->Release();return found;
    }
    static std::atomic<int> slowProbeCalls;
    static std::wstring slowRoot;
    // Simulated spun-down disk: the first probe below slowRoot waits like a disk spin-up.
    static DWORD SlowProbe(const std::wstring& path) {
        if(!slowRoot.empty() && _wcsnicmp(path.c_str(),slowRoot.c_str(),slowRoot.size())==0 && slowProbeCalls++==0)
            Sleep(2500);
        return GetFileAttributesW(path.c_str());
    }
    template<typename Invoke,typename Pump>
    static int CheckBusyAndSlowActivation(CMainWnd& window,const std::wstring& fixture,Invoke& invoke,Pump& pump) {
        int failures=0;
        auto check=[&](bool value,const char* name){if(!value){++failures;std::cerr<<"FAIL "<<name<<"\n";}};
        auto settle=[&](const std::wstring& path,DWORD ms,DWORD* longest=nullptr) {
            const DWORD deadline=GetTickCount()+ms;
            while(GetTickCount()<deadline && (!CMainWnd::PathEquals(window.m_currentPath,path)
                || GetPropW(window.m_hWnd,L"FastFile.ExternalOpenPending"))) {
                MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                    if(message.message==WM_QUIT)continue;
                    const DWORD start=GetTickCount();
                    TranslateMessage(&message);DispatchMessageW(&message);
                    if(longest)*longest=(std::max)(*longest,GetTickCount()-start);
                }
                Sleep(5);
            }
            return CMainWnd::PathEquals(window.m_currentPath,path);
        };
        const HWND own=window.m_hWnd;
        // 1. Busy receiver: the UI thread does not pump while the launcher forwards.
        // The launcher must exit successfully without starting Explorer, and the folder
        // must open once the window is free again.
        window.NavigateToNow(fixture,true);settle(fixture,3000);
        const std::wstring busyTarget=fixture+L"\\115Chrome";
        HANDLE child=invoke(busyTarget,L"Directory");
        if(child) {
            Sleep(4000); // busy: no messages are processed
            const bool exited=WaitForSingleObject(child,4000)==WAIT_OBJECT_0;
            DWORD code=STILL_ACTIVE;GetExitCodeProcess(child,&code);
            check(exited && code==0,"launcher hands off to a busy FastFile window and exits");
            check(!CMainWnd::PathEquals(window.m_currentPath,busyTarget),"busy window did not process the request yet");
            check(settle(busyTarget,6000),"busy window opens the forwarded folder once it is free");
            check(ExplorerWindowsUnder(fixture,own)==0,"busy forwarding never opens an Explorer window");
            // A waiting show-in-folder request looks for a registered window at the folder.
            check(window.m_shellWindow && window.m_shellWindow->IsRegistered()
                && CMainWnd::PathEquals(window.m_shellWindowPath,busyTarget),"forwarded folder is announced as the Shell window location");
            if(code==STILL_ACTIVE)TerminateProcess(child,3);CloseHandle(child);
        }
        // 2. Slow folder: the first touch of the target volume takes 2.5 s. The window must
        // keep processing messages (no single message handler blocks) and still open it.
        const std::wstring slow=fixture+L"\\Slow";CreateDirectoryW(slow.c_str(),nullptr);
        const std::wstring selectedPath=slow+L"\\select-me.txt";
        HANDLE selectedFile=CreateFileW(selectedPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        check(selectedFile!=INVALID_HANDLE_VALUE,"selection fixture is created");
        if(selectedFile!=INVALID_HANDLE_VALUE)CloseHandle(selectedFile);
        slowRoot=slow;slowProbeCalls=0;CMainWnd::s_folderProbe=&SlowProbe;
        window.NavigateToNow(fixture,true);settle(fixture,3000);
        PIDLIST_ABSOLUTE selectedItem=nullptr;
        SHParseDisplayName(selectedPath.c_str(),nullptr,&selectedItem,0,nullptr);
        check(selectedItem && SUCCEEDED(window.OnShellWindowSelect(selectedItem,
            SVSI_SELECT|SVSI_DESELECTOTHERS|SVSI_ENSUREVISIBLE|SVSI_FOCUSED)),
            "show-in-folder selection is accepted before navigation completes");
        if(selectedItem)CoTaskMemFree(selectedItem);
        check(window.m_pendingShellSelect!=nullptr,"selection waits while the wrong folder is displayed");
        child=invoke(slow,L"Directory");
        // The launcher has resolved the isolated verb. Restore COM registration before
        // the real Shell view enumerates and selects the file (HKCR also contains its
        // interfaces and file-type providers). The caller restores its own test hive
        // before the subsequent cold-start launch.
        RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr);
        if(child) {
            DWORD longest=0;
            const bool opened=settle(slow,10000,&longest);
            check(opened,"slow folder opens after the simulated spin-up");
            check(slowProbeCalls>=1,"slow folder probe ran");
            check(longest<700,"slow folder activation never blocks the UI thread");
            if(longest>=700)std::cerr<<"  longest message "<<longest<<" ms\n";
            DWORD code=STILL_ACTIVE;WaitForSingleObject(child,3000);GetExitCodeProcess(child,&code);
            check(code==0,"slow folder launcher exits after forwarding");
            if(code==STILL_ACTIVE)TerminateProcess(child,3);CloseHandle(child);
            const DWORD selectedDeadline=GetTickCount()+3000;
            while(window.m_pendingShellSelect && GetTickCount()<selectedDeadline){pump();Sleep(5);}
            std::vector<std::pair<std::wstring,bool>> selection;
            check(window.m_shellBrowser->GetSelection(selection) && selection.size()==1
                && CMainWnd::PathEquals(selection[0].first,selectedPath),
                "forwarded folder selects and reveals the requested file in the real Shell view");
            check(window.m_pendingShellSelect==nullptr,"successful selection clears its pending request");
        }
        CMainWnd::s_folderProbe=nullptr;slowRoot.clear();
        // 3. "Show in folder" (SHOpenFolderAndSelectItems) needs the Shell to find this
        //    window, and the Shell ignores windows on other desktops such as this
        //    harness's isolated one; FastFileShellWindowTests covers it on the normal
        //    desktop. Here only the registration itself is checked (see CheckShellActivation).
        return failures;
    }
    // Interactive runtime check: a disposable ACL-protected file reproduces
    // Explorer's administrator-permission dialog without touching user files.
    static int CheckDeletePermissionDialog(CMainWnd& window, const std::wstring& root, bool partial = false) {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 1;
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        std::vector<BYTE> storage(bytes);
        const bool tokenOk = GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes) != FALSE;
        CloseHandle(token);
        wchar_t* sid = nullptr;
        if (!tokenOk || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid)) return 1;
        const std::wstring userSid = sid; LocalFree(sid);
        const auto folder = root + L"\\权限交互测试";
        const auto file = folder + L"\\仅测试管理员权限.txt";
        const auto ordinary = root + L"\\仅测试普通删除.txt";
        CreateDirectoryW(folder.c_str(), nullptr);
        HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return 1;
        CloseHandle(handle);
        if (partial) {
            handle = CreateFileW(ordinary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return 1;
            CloseHandle(handle);
        }
        auto setAcl = [&](const std::wstring& path, const std::wstring& sddl) {
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return false;
            const bool ok = SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor) != FALSE;
            LocalFree(descriptor); return ok;
        };
        const auto protectedAcl = L"D:P(A;;FA;;;BA)(A;;FA;;;SY)(A;;0x1201bf;;;" + userSid + L")";
        const auto restoreAcl = L"D:P(A;;FA;;;BA)(A;;FA;;;SY)(A;;FA;;;" + userSid + L")";
        const bool aclOk = setAcl(folder, protectedAcl) && setAcl(file, protectedAcl);
        ShowWindow(window.m_hWnd, SW_SHOW);
        SetForegroundWindow(window.m_hWnd);
        std::vector<std::wstring> completed;
        std::vector<CMainWnd::ClipboardItem> items;
        if (partial) items.push_back({ordinary, false});
        items.push_back({file, false});
        // Windows' own delete engine with the production (interactive) flags.
        bool deleted = false;
        if (aclOk) {
            ShellFileOps::Request request;
            request.kind = ShellFileOps::Kind::Recycle;
            request.owner = window.m_hWnd;
            request.interactive = true;
            for (const auto& item : items) request.sources.push_back(item.path);
            const ShellFileOps::Result result = ShellFileOps::Perform(request);
            for (const auto& done : result.completed) completed.push_back(done.first);
            deleted = SUCCEEDED(result.hr) && !result.aborted && completed.size() == items.size();
        }
        const bool retained = GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES;
        const bool restored = setAcl(folder, restoreAcl) && setAcl(file, restoreAcl);
        std::cout << "permission check: ACL=" << aclOk << " canceled=" << !deleted
            << " retained=" << retained << " completed=" << completed.size() << " restored=" << restored << '\n';
        const bool accurate = partial ? completed.size() == 1 && completed[0] == ordinary &&
            GetFileAttributesW(ordinary.c_str()) == INVALID_FILE_ATTRIBUTES : completed.empty();
        return aclOk && !deleted && retained && accurate && restored ? 0 : 1;
    }
    static int CheckShortcuts(CMainWnd& window, const std::wstring& root) {
        int failures = 0;
        auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
        // A hidden parent cannot give its native child keyboard focus. Show the window
        // on this harness's isolated desktop so shortcuts target the file view, rather
        // than the stale DuiLib tree focus retained by an earlier check.
        ShowWindow(window.m_hWnd, SW_SHOW);
        if (!OpenClipboard(window.m_hWnd)) {
            std::cerr << "FAIL keyboard/file-operation prerequisite: system clipboard is inaccessible (Win32 error "
                << GetLastError() << ", open window " << GetOpenClipboardWindow() << ")\n";
            return failures + 1;
        }
        CloseClipboard();
        // 1.0.21: every file command is Windows' own. Inside the hosted view DefView handles the
        // key itself; elsewhere FastFile invokes the same verb on the item's native menu. There is
        // one undo history, the per-process Windows one (FastFile keeps none of its own).
        NativeDialogGuard dialogs;
        {
            using FC = CMainWnd::FileCommand;
            struct Row { WPARAM key; bool ctrl, shift, alt; FC command; };
            const Row rows[] = {
                {'C',true,false,false,FC::Copy}, {VK_INSERT,true,false,false,FC::Copy}, {'C',true,true,false,FC::None},
                {'X',true,false,false,FC::Cut}, {'V',true,false,false,FC::Paste}, {VK_INSERT,false,true,false,FC::Paste},
                {'Z',true,false,false,FC::Undo}, {'Y',true,false,false,FC::Redo}, {'Z',true,true,false,FC::Redo},
                {'A',true,false,false,FC::SelectAll}, {'N',true,true,false,FC::NewFolder}, {'N',true,false,false,FC::None},
                {VK_DELETE,false,false,false,FC::Delete}, {VK_DELETE,false,true,false,FC::DeletePermanent},
                {'D',true,false,false,FC::Delete}, {VK_F2,false,false,false,FC::Rename},
                {VK_RETURN,false,false,true,FC::Properties}, {VK_LEFT,false,false,true,FC::None},
                {VK_BACK,false,false,false,FC::None}, {VK_F5,false,false,false,FC::None}, {'L',true,false,false,FC::None}};
            bool table = true;
            for (const auto& row : rows)
                table = CMainWnd::FileCommandForKey(row.key, row.ctrl, row.shift, row.alt) == row.command && table;
            check(table, "file shortcuts map to the Windows file commands; navigation keys stay FastFile's");
        }
        auto pumpFor = [&](DWORD ms) {
            const DWORD until = GetTickCount() + ms;
            do {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message != WM_QUIT && !CPaintManagerUI::TranslateMessage(&message)) {
                        ::TranslateMessage(&message); DispatchMessageW(&message);
                    }
                }
                Sleep(5);
            } while (GetTickCount() < until);
        };
        auto pump = [&]() { pumpFor(700); };
        auto waitFor = [&](const std::function<bool()>& ready, DWORD timeout = 8000) {
            const DWORD deadline = GetTickCount() + timeout;
            while (!ready() && GetTickCount() < deadline) pumpFor(20);
            return ready();
        };
        auto exists = [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
        const auto source = root + L"\\Keyboard Source";
        const auto target = root + L"\\Keyboard Target";
        const auto batchSource = root + L"\\Batch Source";
        const auto batchDestination = root + L"\\Batch Destination";
        for (const auto& path : {source, target, batchSource, batchDestination}) CreateDirectoryW(path.c_str(), nullptr);
        const auto original = source + L"\\keyboard.txt";
        HANDLE file = CreateFileW(original.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        const char payload[] = "FastFile shortcut regression";
        DWORD written = 0; WriteFile(file, payload, sizeof(payload), &written, nullptr); CloseHandle(file);
        IDataObject* savedClipboard = SnapshotClipboard();
        auto navigate = [&](const std::wstring& path) { window.NavigateToNow(path, true); pump(); };
        auto key = [&](WPARAM value, bool ctrl, bool shift = false, bool alt = false) {
            IFolderView2* view = ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
            IShellView* shellView = nullptr; HWND hwnd = nullptr;
            if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&shellView)))) {
                shellView->GetWindow(&hwnd); shellView->Release();
            }
            if (view) view->Release();
            SetFocus(hwnd);
            check(window.m_shellBrowser->OwnsWindow(GetFocus()), "keyboard shortcut focuses the native file view");
            BYTE saved[256]{}, keys[256]{}; GetKeyboardState(saved);
            if (ctrl) keys[VK_CONTROL] = keys[VK_LCONTROL] = 0x80;
            if (shift) keys[VK_SHIFT] = keys[VK_LSHIFT] = 0x80;
            if (alt) keys[VK_MENU] = keys[VK_LMENU] = 0x80;
            SetKeyboardState(keys);
            check(((GetKeyState(VK_CONTROL)&0x8000)!=0)==ctrl &&
                ((GetKeyState(VK_SHIFT)&0x8000)!=0)==shift &&
                ((GetKeyState(VK_MENU)&0x8000)!=0)==alt, "synthetic shortcut has the requested modifier state");
            MSG message{}; message.hwnd = hwnd; message.message = alt ? WM_SYSKEYDOWN : WM_KEYDOWN;
            message.wParam = value; message.lParam = 1;
            const bool handled = CPaintManagerUI::TranslateMessage(&message);
            SetKeyboardState(saved);
            return handled;
        };
        // The same key sent to FastFile's own window (no hosted-view focus).
        auto frameKey = [&](WPARAM value, bool ctrl, bool shift = false, bool alt = false) {
            BYTE saved[256]{}, keys[256]{}; GetKeyboardState(saved);
            if (ctrl) keys[VK_CONTROL] = keys[VK_LCONTROL] = 0x80;
            if (shift) keys[VK_SHIFT] = keys[VK_LSHIFT] = 0x80;
            if (alt) keys[VK_MENU] = keys[VK_LMENU] = 0x80;
            SetKeyboardState(keys);
            MSG message{}; message.hwnd = window.m_hWnd; message.message = alt ? WM_SYSKEYDOWN : WM_KEYDOWN;
            message.wParam = value; message.lParam = 1;
            const bool handled = window.TranslateAccelerator(&message) == S_OK;
            SetKeyboardState(saved);
            return handled;
        };
        auto select = [&]() {
            const DWORD deadline = GetTickCount() + 5000;
            bool ready = false;
            do {
                check(key('A', true), "native Ctrl A routes to selection"); pump();
                std::vector<std::pair<std::wstring, bool>> selected;
                ready = window.m_shellBrowser->IsAtPath(window.m_currentPath)
                    && window.m_shellBrowser->GetSelection(selected) && !selected.empty();
                for (const auto& item : selected)
                    ready = GetFileAttributesW(item.first.c_str()) != INVALID_FILE_ATTRIBUTES && ready;
            } while (!ready && GetTickCount() < deadline);
            check(ready, "native selection reflects completed enumeration and rename notifications");
        };
        auto finish = [&]() {
            const DWORD until = GetTickCount() + 8000;
            do { pump(); } while (window.m_copyRunning && GetTickCount() < until);
            check(!window.m_copyRunning, "drag-drop file job finishes");
        };
        // FastFile's remaining own engine (drag and drop) runs IFileOperation with the native
        // Windows progress UI (production flags) and adds a Windows undo record.
        auto nativeOperation = [&](ShellFileOps::Kind kind, const char* name) {
            const auto& last = window.m_lastFileOperation;
            const std::wstring status = window.m_pStatus ? window.m_pStatus->GetText().GetData() : L"";
            check(last.engine == ShellFileOps::Engine::FileOperation && last.kind == kind &&
                window.m_lastFileOpRequestFlags == last.flags &&
                (last.flags & (FOF_SILENT | FOF_NOERRORUI | FOFX_NOMINIMIZEBOX)) == 0 &&
                (last.flags & (FOF_ALLOWUNDO | FOFX_ADDUNDORECORD)) == (FOF_ALLOWUNDO | FOFX_ADDUNDORECORD) &&
                status.find(L'%') == std::wstring::npos, name);
        };
        // Data object Windows put on the clipboard: exactly the expected file (CF_HDROP).
        auto clipboardHolds = [&](const std::wstring& expected) {
            bool ok = false;
            IDataObject* data = nullptr;
            if (SUCCEEDED(OleGetClipboard(&data)) && data) {
                FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
                STGMEDIUM medium{};
                if (SUCCEEDED(data->GetData(&format, &medium))) {
                    HDROP drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
                    wchar_t first[MAX_PATH]{};
                    ok = drop && DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) == 1
                        && DragQueryFileW(drop, 0, first, MAX_PATH) && CMainWnd::PathEquals(first, expected);
                    GlobalUnlock(medium.hGlobal);
                    ReleaseStgMedium(&medium);
                }
                data->Release();
            }
            return ok;
        };
        // Text of the Windows background menu's 撤销 entry (it names the last operation).
        auto undoText = [&]() {
            std::wstring text;
            IContextMenu* menu = nullptr; bool fromView = false;
            if (window.CreateNativeVerbMenu(CMainWnd::NativeScope::Background, true, &menu, &fromView)) {
                HMENU popup = nullptr; UINT last = 0;
                if (CMainWnd::QueryShellMenu(menu, CMF_NORMAL, &popup, &last)) {
                    const int pos = ShellMenuUtil::FindVerb(menu, popup, 1, last, L"undo");
                    wchar_t buffer[256]{};
                    if (pos >= 0 && !(GetMenuState(popup, pos, MF_BYPOSITION) & (MF_GRAYED | MF_DISABLED))) {
                        GetMenuStringW(popup, pos, buffer, _countof(buffer), MF_BYPOSITION);
                        text = buffer;
                    }
                    DestroyMenu(popup);
                }
                menu->Release();
            }
            return text;
        };
        // Diagnostic: every verb of the view's background menu with its state (undo / redo).
        auto dumpHistory = [&](const char* tag) {
            IContextMenu* menu = nullptr;
            if (FAILED(window.m_shellBrowser->CreateBackgroundContextMenu(&menu)) || !menu) { std::cerr << "  history " << tag << ": no menu\n"; return; }
            HMENU popup = CreatePopupMenu();
            const HRESULT hr = menu->QueryContextMenu(popup, 0, 1, 0x7FFF, CMF_NORMAL);
            std::cerr << "  history " << tag << ":";
            for (UINT offset = 0; SUCCEEDED(hr) && offset < UINT(HRESULT_CODE(hr)); ++offset) {
                wchar_t verb[128]{};
                if (FAILED(menu->GetCommandString(offset, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), _countof(verb)))) continue;
                const UINT state = GetMenuState(popup, offset + 1, MF_BYCOMMAND);
                wchar_t text[128]{}; GetMenuStringW(popup, offset + 1, text, _countof(text), MF_BYCOMMAND);
                char v[128]{}, t[256]{};
                WideCharToMultiByte(CP_UTF8, 0, verb, -1, v, sizeof(v), nullptr, nullptr);
                WideCharToMultiByte(CP_UTF8, 0, text, -1, t, sizeof(t), nullptr, nullptr);
                std::cerr << " [" << offset << ' ' << v << " state=0x" << std::hex << state << std::dec << " \"" << t << "\"]";
            }
            std::cerr << '\n';
            DestroyMenu(popup); menu->Release();
        };
        navigate(source); select();
        check(key('C', true, true), "Ctrl Shift C copies quoted paths");
        bool copiedPath = false;
        std::wstring copiedText;
        if (OpenClipboard(window.m_hWnd)) {
            HGLOBAL data = GetClipboardData(CF_UNICODETEXT);
            const wchar_t* text = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
            copiedPath = text && std::wstring(text) == L"\"" + original + L"\"";
            if (text) copiedText = text;
            if (text) GlobalUnlock(data); CloseClipboard();
        }
        if (!copiedPath) {
            std::vector<CMainWnd::ClipboardItem> selected;
            window.CollectSelectedItems(selected);
            std::cerr << "  copy-path diagnostic: selected=" << selected.size()
                << " expectedSelected=" << (selected.size()==1 && CMainWnd::PathEquals(selected[0].path, original))
                << " copiedChars=" << copiedText.size() << " clipboardOwner=" << GetOpenClipboardWindow() << "\n";
        }
        check(copiedPath, "copy as path publishes Unicode quoted selection");
        // Copy: the Windows view puts its own data object (CF_HDROP + preferred effect) on the clipboard.
        OleSetClipboard(nullptr);
        check(key(VK_INSERT, true), "Ctrl Insert is handled in the Windows view");
        check(waitFor([&] { return clipboardHolds(original); }, 3000), "Ctrl Insert puts the Windows file data object on the clipboard");
        OleSetClipboard(nullptr);
        check(key('C', true), "Ctrl C is handled in the Windows view");
        check(waitFor([&] { return clipboardHolds(original) && IsClipboardFormatAvailable(CF_HDROP); }, 3000),
            "Ctrl C puts the Windows file data object (CF_HDROP) on the clipboard");
        // Paste: Windows copies (its own progress / conflict UI, its own undo record).
        navigate(target);
        check(window.m_pBtnPaste->IsEnabled(), "system clipboard enables paste button");
        const auto copied = target + L"\\keyboard.txt";
        check(key('V', true), "Ctrl V is handled in the Windows view");
        auto sameBytes = [&](const std::wstring& path) {
            HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            char actual[sizeof(payload)]{}; DWORD read = 0;
            if (handle != INVALID_HANDLE_VALUE) { ReadFile(handle, actual, sizeof(actual), &read, nullptr); CloseHandle(handle); }
            return read == sizeof(payload) && memcmp(actual, payload, sizeof(payload)) == 0;
        };
        check(waitFor([&] { return exists(copied) && sameBytes(copied); }) && exists(original),
            "Windows paste copies exact file bytes and retains the source");
        if (!exists(copied)) {
            std::cerr << "  paste diagnostic: hdropAvailable=" << IsClipboardFormatAvailable(CF_HDROP)
                << " holds=" << clipboardHolds(original) << " nativeCount=" << window.m_lastNativeVerb.count
                << " hr=0x" << std::hex << window.m_lastNativeVerb.hr << std::dec << " formats:";
            if (OpenClipboard(window.m_hWnd)) {
                for (UINT format = EnumClipboardFormats(0); format; format = EnumClipboardFormats(format)) {
                    char name[128]{}; GetClipboardFormatNameA(format, name, sizeof(name));
                    std::cerr << ' ' << format << '(' << name << ')';
                }
                CloseClipboard();
            }
            char status[512]{};
            WideCharToMultiByte(CP_UTF8, 0, window.m_pStatus ? window.m_pStatus->GetText().GetData() : L"", -1, status, sizeof(status), nullptr, nullptr);
            char verb[64]{}; WideCharToMultiByte(CP_UTF8, 0, window.m_lastNativeVerb.verb.c_str(), -1, verb, sizeof(verb), nullptr, nullptr);
            std::cerr << " lastVerb=" << verb << " status=" << status << " atTarget=" << window.m_shellBrowser->IsAtPath(target) << '\n';
        }
        // F2 in the view is Windows' in-place rename; Ctrl+Z / Ctrl+Y are Windows' undo / redo.
        select();
        check(key(VK_F2, false), "F2 is handled in the Windows view");
        HWND renameEdit = nullptr;
        waitFor([&] { return (renameEdit = ShellBrowserHostTestAccess::EditControl(*window.m_shellBrowser)) != nullptr; }, 3000);
        check(renameEdit != nullptr, "F2 starts the view's own in-place rename");
        const auto inplace = target + L"\\inplace.txt";
        if (renameEdit) {
            SetWindowTextW(renameEdit, L"inplace.txt");
            SendMessageW(renameEdit, WM_KEYDOWN, VK_RETURN, 0);
            if (IsWindow(renameEdit)) SendMessageW(renameEdit, WM_CHAR, VK_RETURN, 0);
        }
        check(waitFor([&] { return exists(inplace) && !exists(copied); }), "the Windows view commits the in-place rename");
        // The Windows undo history: when Windows offers it in this process (its background
        // menu lists 撤销), Ctrl+Z / Ctrl+Y must reverse the view's rename. Some Windows builds
        // (observed on 26H2 26300) keep no undo history for operations in other processes'
        // hosted views; then Ctrl+Z must do nothing at all (FastFile has no private history).
        const bool systemUndo = window.NativeVerbAvailable(L"undo", CMainWnd::NativeScope::Background);
        if (!systemUndo) {
            std::cerr << "  info: Windows offers no undo history in this process; native Ctrl+Z / 撤销 are no-ops here\n";
            dumpHistory("no-undo background menu");
        }
        check(key('Z', true), "Ctrl Z is handled in the Windows view");
        if (systemUndo) {
            check(waitFor([&] { return exists(copied) && !exists(inplace); }), "Windows undo reverts the in-place rename");
            check(key('Y', true), "Ctrl Y is handled in the Windows view");
            check(waitFor([&] { return exists(inplace) && !exists(copied); }), "Windows redo reapplies the in-place rename");
            key('Z', true);
            waitFor([&] { return exists(copied) && !exists(inplace); });
        } else {
            pumpFor(500);
            check(exists(inplace) && !exists(copied), "without a Windows undo history Ctrl Z changes nothing");
            MoveFileW(inplace.c_str(), copied.c_str());
        }
        // Tree / search-list rename (FastFile's edit box) is a Windows rename with a Windows
        // undo record, so the toolbar 撤销 / 重做 (native verbs) reverse it as well.
        const auto engineName = target + L"\\engine.txt";
        check(window.RenameItem({copied, false}, L"engine.txt") && exists(engineName) && !exists(copied),
            "tree / search rename runs IFileOperation::RenameItem");
        const bool renameUndo = window.NativeVerbAvailable(L"undo", CMainWnd::NativeScope::Background);
        check(renameUndo == systemUndo, "the IFileOperation rename and the view's rename share one undo history");
        window.OnUndo();
        if (renameUndo) {
            check(waitFor([&] { return exists(copied) && !exists(engineName); }), "撤销 reverses FastFile's rename through the Windows history");
            check(window.m_lastNativeVerb.verb == L"undo" && window.m_lastNativeVerb.byOffset
                && window.m_lastNativeVerb.scope == CMainWnd::NativeScope::Background && SUCCEEDED(window.m_lastNativeVerb.hr),
                "撤销 invokes the native background undo verb");
            window.OnRedo();
            check(waitFor([&] { return exists(engineName) && !exists(copied); }), "重做 reapplies it through the Windows history");
        } else {
            check(waitFor([&] { return exists(copied) && !exists(engineName); }),
                "撤销 reverses FastFile's rename through the fallback undo stack");
            window.OnRedo();
            check(waitFor([&] { return exists(engineName) && !exists(copied); }),
                "重做 reapplies FastFile's rename through the fallback redo stack");
        }
        // Drag-and-drop copies (FastFile's only remaining engine use) join the same history.
        const auto directory = batchSource + L"\\Folder";
        CreateDirectoryW(directory.c_str(), nullptr);
        const auto nested = directory + L"\\data.txt";
        file = CreateFileW(nested.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        WriteFile(file, payload, sizeof(payload), &written, nullptr); CloseHandle(file);
        const auto standalone = batchSource + L"\\one.txt";
        CopyFileW(nested.c_str(), standalone.c_str(), TRUE);
        const std::wstring undoBeforeCopy = undoText();
        check(window.StartFileOperation(ShellFileOps::Kind::Copy, {directory, standalone}, batchDestination),
            "drag-drop copy starts"); finish();
        nativeOperation(ShellFileOps::Kind::Copy, "drag-drop copy runs IFileOperation with native UI and a Windows undo record");
        check(exists(batchDestination + L"\\Folder\\data.txt") && exists(batchDestination + L"\\one.txt") && exists(nested),
            "drag-drop copy copies every top-level item");
        const std::wstring undoAfterCopy = undoText();
        if (systemUndo && (undoAfterCopy.empty() || undoAfterCopy == undoBeforeCopy)) {
            char before[512]{}, after[512]{};
            WideCharToMultiByte(CP_UTF8, 0, undoBeforeCopy.c_str(), -1, before, sizeof(before), nullptr, nullptr);
            WideCharToMultiByte(CP_UTF8, 0, undoAfterCopy.c_str(), -1, after, sizeof(after), nullptr, nullptr);
            std::cerr << "  undo diagnostic: before=\"" << before << "\" after=\"" << after << "\"\n";
        }
        if (systemUndo)
            check(!undoAfterCopy.empty() && undoAfterCopy != undoBeforeCopy,
                "the Windows 撤销 entry now names the drag-drop copy (one shared history)");
        else
            check(undoAfterCopy.empty() && !window.NativeVerbAvailable(L"undo", CMainWnd::NativeScope::Background),
                "without a Windows undo history the drag-drop copy adds no private entry");
        // Buttons and keys outside the view: each invokes the native verb on the native menu.
        // The seam records the call instead of executing, so nothing is deleted here.
        MainWndRegressionAccess::ClearUndoStacks(window);
        navigate(target); select();
        std::vector<CMainWnd::NativeVerbCall> calls;
        struct HookReset {
            CMainWnd& window;
            ~HookReset() { window.m_nativeInvokeHook = nullptr; }
        } hookReset{window};
        window.m_nativeInvokeHook = [&](IContextMenu*) { calls.push_back(window.m_lastNativeVerb); return S_OK; };
        auto expect = [&](const std::function<void()>& action, const wchar_t* verb, CMainWnd::NativeScope scope,
                int shiftMask, bool fromView, int byOffset, const char* name) {
            calls.clear();
            action();
            const DWORD required = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
            const bool ok = calls.size() == 1 && _wcsicmp(calls[0].verb.c_str(), verb) == 0 && calls[0].scope == scope
                && (calls[0].mask & required) == required && (shiftMask < 0 || ((calls[0].mask & CMIC_MASK_SHIFT_DOWN) != 0) == (shiftMask != 0))
                && calls[0].fromView == fromView && (byOffset < 0 || calls[0].byOffset == (byOffset != 0));
            if (!ok && !calls.empty()) {
                char verbText[128]{};
                WideCharToMultiByte(CP_UTF8, 0, calls[0].verb.c_str(), -1, verbText, sizeof(verbText), nullptr, nullptr);
                std::cerr << "  native verb diagnostic: calls=" << calls.size() << " verb=" << verbText
                    << " scope=" << int(calls[0].scope) << " mask=0x" << std::hex << calls[0].mask << std::dec
                    << " fromView=" << calls[0].fromView << " byOffset=" << calls[0].byOffset << '\n';
            } else if (!ok) std::cerr << "  native verb diagnostic: no native invoke\n";
            check(ok, name);
        };
        using Scope = CMainWnd::NativeScope;
        expect([&] { window.OnCopyClicked(); }, L"copy", Scope::Selection, false, true, 1, "复制 button invokes the view's native copy");
        expect([&] { window.OnCutClicked(); }, L"cut", Scope::Selection, false, true, 1, "剪切 button invokes the view's native cut");
        expect([&] { window.OnPasteClicked(); }, L"paste", Scope::Background, false, true, 1, "粘贴 button invokes the view's native paste");
        expect([&] { window.OnDeleteClicked(false); }, L"delete", Scope::Selection, false, true, 1, "删除 button invokes native delete");
        expect([&] { window.OnDeleteClicked(true); }, L"delete", Scope::Selection, true, true, true,
            "permanent delete invokes native delete with Shift (Windows' own confirmation)");
        expect([&] { window.OnNewFolderClicked(); }, CMDSTR_NEWFOLDERW, Scope::Background, false, true, -1,
            "新建文件夹 invokes the native NewFolder verb of the folder background");
        expect([&] { window.ShowPropertiesForSelection(); }, L"properties", Scope::Selection, false, true, true,
            "属性 invokes the native properties verb");
        if (systemUndo) expect([&] { window.OnUndo(); }, L"undo", Scope::Background, false, true, 1, "撤销 invokes the native undo verb");
        else { calls.clear(); window.OnUndo(); check(calls.empty(), "撤销 with no Windows undo entry invokes nothing"); }
        calls.clear(); window.OnRedo();
        check(calls.empty(), "重做 with no Windows redo entry invokes nothing");
        expect([&] { frameKey(VK_DELETE, false); }, L"delete", Scope::Selection, false, true, 1, "Delete invokes native delete");
        expect([&] { frameKey(VK_DELETE, false, true); }, L"delete", Scope::Selection, true, true, true,
            "Shift Delete invokes native delete with Shift");
        expect([&] { frameKey('C', true); }, L"copy", Scope::Selection, false, true, 1, "Ctrl C outside the view invokes native copy");
        expect([&] { frameKey('X', true); }, L"cut", Scope::Selection, false, true, 1, "Ctrl X outside the view invokes native cut");
        expect([&] { frameKey('V', true); }, L"paste", Scope::Background, false, true, 1, "Ctrl V outside the view invokes native paste");
        if (systemUndo) expect([&] { frameKey('Z', true); }, L"undo", Scope::Background, false, true, 1, "Ctrl Z outside the view invokes native undo");
        else { calls.clear(); frameKey('Z', true); check(calls.empty(), "Ctrl Z outside the view with no Windows undo entry invokes nothing"); }
        expect([&] { frameKey(VK_RETURN, false, false, true); }, L"properties", Scope::Selection, false, true, true,
            "Alt Enter invokes native properties");
        expect([&] { frameKey('N', true, true); }, CMDSTR_NEWFOLDERW, Scope::Background, -1, true, -1,
            "Ctrl Shift N invokes the native NewFolder verb");
        // F2 outside the view still opens the view's own in-place edit (no FastFile prompt).
        calls.clear();
        check(frameKey(VK_F2, false), "F2 outside the view is handled");
        HWND frameEdit = nullptr;
        waitFor([&] { return (frameEdit = ShellBrowserHostTestAccess::EditControl(*window.m_shellBrowser)) != nullptr; }, 3000);
        check(frameEdit != nullptr && calls.empty(), "F2 begins the Windows in-place rename in the view");
        ShellBrowserHostTestAccess::CancelEdit(*window.m_shellBrowser); pumpFor(100);
        check(exists(engineName), "cancelled in-place rename keeps the name");
        // Tree focus: the verb runs on the tree folder's own Shell item menu.
        window.m_pDirTree->SetFocus(); SetFocus(window.m_hWnd);
        expect([&] { frameKey(VK_DELETE, false); }, L"delete", Scope::Selection, false, false, true,
            "tree Delete invokes native delete on the folder item menu");
        expect([&] { frameKey('C', true); }, L"copy", Scope::Selection, false, false, true,
            "tree Ctrl C invokes native copy on the folder item menu");
        window.m_nativeInvokeHook = nullptr;
        check(exists(engineName) && exists(target), "recorded verbs never touched the fixtures");
        window.FocusFileView();
        // Nothing to paste: Windows does nothing, and FastFile has no private clipboard to fall back on.
        OleSetClipboard(nullptr);
        auto entries = [](const std::wstring& folder) {
            int count = 0; WIN32_FIND_DATAW data{};
            HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &data);
            if (find != INVALID_HANDLE_VALUE) {
                do { if (wcscmp(data.cFileName, L".") && wcscmp(data.cFileName, L"..")) ++count; } while (FindNextFileW(find, &data));
                FindClose(find);
            }
            return count;
        };
        const int beforePaste = entries(target);
        check(key('V', true), "Ctrl V with an empty clipboard is handled"); pump();
        check(entries(target) == beforePaste && !window.m_copyRunning, "empty Windows clipboard pastes nothing");
        IFolderView2* view = ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
        IShellView* shellView = nullptr; HWND shellWindow = nullptr;
        if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&shellView)))) {
            shellView->GetWindow(&shellWindow); shellView->Release();
        }
        if (view) view->Release();
        HWND edit = CreateWindowExW(0, L"Edit", L"rename text", WS_CHILD, 0, 0, 100, 20, shellWindow, nullptr, nullptr, nullptr);
        MSG editing{}; editing.hwnd = edit; editing.message = WM_KEYDOWN; editing.wParam = VK_DELETE;
        check(window.TranslateAccelerator(&editing) == S_FALSE, "Shell rename edit owns Delete text key");
        BYTE saved[256]{}, keys[256]{}; GetKeyboardState(saved); keys[VK_CONTROL] = 0x80; SetKeyboardState(keys);
        for (WPARAM textKey : {'C', 'V', 'X', 'Z', 'Y', 'A'}) {
            editing.wParam = textKey;
            check(window.TranslateAccelerator(&editing) == S_FALSE, "native edit retains clipboard and history text keys");
        }
        SetKeyboardState(saved);
        DestroyWindow(edit);
        for (int mode = 1; mode <= 8; ++mode) {
            check(key('0' + mode, true, true), "Ctrl Shift digit switches view"); pump();
            IFolderView2* current = ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
            UINT actualMode = 0; DWORD flags = 0;
            if (current) { current->GetCurrentViewMode(&actualMode); current->GetCurrentFolderFlags(&flags); current->Release(); }
            const UINT expected[] = {FVM_ICON,FVM_ICON,FVM_ICON,FVM_SMALLICON,FVM_LIST,FVM_DETAILS,FVM_TILE,FVM_CONTENT};
            // Windows 11 normalizes smaller icon sizes to SMALLICON internally.
            check(actualMode == expected[mode - 1] || (mode <= 4 && (actualMode == FVM_ICON || actualMode == FVM_SMALLICON)),
                "view shortcut reaches corresponding Shell mode");
            if (mode <= 4) {
                IFolderView2* iconView = ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
                FOLDERVIEWMODE iconMode{}; int iconSize = 0;
                if (iconView) { iconView->GetViewModeAndIconSize(&iconMode, &iconSize); iconView->Release(); }
                const int sizes[] = {160,128,32,16};
                check(iconSize == window.DpiScale(sizes[mode - 1]), "icon view shortcut applies requested DPI-scaled size");
            }
            check(((flags & FWF_NOCOLUMNHEADER) == 0) == (mode == 6), "view shortcut retains details-only headers");
        }
        select(); check(key(VK_ESCAPE, false), "Escape handles native selection clear"); pump();
        check(!window.HasFileSelection(), "Escape clears real Shell selection");
        check(key('P', false, false, true), "Alt P toggles right pane");
        const bool preview = window.m_previewVisible;
        key('P', false, false, true); check(window.m_previewVisible != preview, "Alt P reverses pane visibility");
        navigate(source); navigate(target);
        check(key(VK_LEFT, false, false, true), "Alt Left handled"); pump();
        check(window.m_currentPath == source, "Alt Left restores history path");
        check(key(VK_RIGHT, false, false, true), "Alt Right handled"); pump();
        check(window.m_currentPath == target, "Alt Right advances history path");
        check(key(VK_UP, false, false, true), "Alt Up handled"); pump();
        check(window.m_currentPath == root, "Alt Up navigates parent path");
        check(key(VK_BACK, false), "Backspace handled"); pump();
        check(window.m_currentPath == target, "Backspace navigates backward");
        check(key('L', true), "Ctrl L focuses address edit");
        check(window.m_addressEditMode, "Ctrl L opens address editing mode");
        key(VK_ESCAPE, false); check(!window.m_addressEditMode, "Escape cancels address mode");
        check(key('E', true), "Ctrl E focuses search");
        check(window.m_PaintManager.GetFocus() == window.m_pSearchEdit, "Ctrl E owns search field focus");
        check(key(VK_F3, false), "F3 focuses search from native view");
        window.FocusFileView();
        WPARAM systemCommand = 0;
        auto captureSystemCommand = [](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) -> LRESULT {
            if (message == WM_SYSCOMMAND) { *reinterpret_cast<WPARAM*>(data) = wParam & 0xfff0; return 0; }
            return DefSubclassProc(hwnd, message, wParam, lParam);
        };
        SetWindowSubclass(window.m_hWnd, captureSystemCommand, 0x72, reinterpret_cast<DWORD_PTR>(&systemCommand));
        check(key(VK_F11, false) && systemCommand == SC_MAXIMIZE, "F11 forwards maximize command from hosted view");
        RemoveWindowSubclass(window.m_hWnd, captureSystemCommand, 0x72);
        check(key(VK_F6, false), "F6 cycles away from file view");
        check(window.m_PaintManager.GetFocus() == window.m_pDirTree, "F6 reaches navigation pane");
        window.FocusFileView();
        check(key(VK_F6, false, true), "Shift F6 reverses pane focus");
        check(window.m_PaintManager.GetFocus() == window.m_pSearchEdit, "Shift F6 reaches search field");
        window.FocusFileView();
        check(key('R', true) && key(VK_F5, false), "Ctrl R and F5 refresh current directory");
        const auto initialTab = window.m_activeTab;
        const auto tabsBefore = window.m_tabs.size();
        key('1', true); pump(); check(window.m_activeTab == 0, "Ctrl 1 activates first tab");
        key('9', true); pump(); check(window.m_activeTab == int(window.m_tabs.size()) - 1, "Ctrl 9 activates last tab");
        check(window.m_tabs.size() == tabsBefore, "number shortcuts do not create duplicate tabs");
        window.ActivateTab(initialTab); pump();
        UINT_PTR menuTimer = SetTimer(nullptr, 0, 100, [](HWND, UINT, UINT_PTR timer, DWORD) {
            EndMenu(); KillTimer(nullptr, timer);
        });
        check(key(VK_F4, false), "F4 opens address history dropdown"); KillTimer(nullptr, menuTimer);
        check(window.m_addressEditMode, "F4 leaves editable address after dismissing dropdown");
        key(VK_ESCAPE, false);
        navigate(batchDestination);
        check(key('E', true, true), "Ctrl Shift E expands current path in navigation tree");
        check(window.m_PaintManager.GetFocus() == window.m_pDirTree, "Ctrl Shift E focuses tree rather than search");
        auto treeKey = [&](WPARAM value) {
            window.m_pDirTree->SetFocus(); SetFocus(window.m_hWnd);
            MSG message{}; message.hwnd = window.m_hWnd; message.message = WM_KEYDOWN; message.wParam = value;
            return CPaintManagerUI::TranslateMessage(&message);
        };
        auto* folderNode = window.FindTreeNodeByPath(nullptr, batchDestination);
        check(treeKey(VK_ADD), "tree numpad plus expands current node");
        check(folderNode && folderNode->GetCountChild() > 0 && !folderNode->GetFolderButton()->IsSelected(),
            "tree expansion loads actual subfolders");
        check(treeKey(VK_SUBTRACT), "tree numpad minus collapses node");
        check(folderNode && folderNode->GetFolderButton()->IsSelected(), "tree collapse updates chevron state");
        treeKey(VK_RIGHT); treeKey(VK_RIGHT); pump();
        check(window.m_currentPath == batchDestination + L"\\Folder", "tree right enters first visible child");
        treeKey(VK_LEFT); treeKey(VK_LEFT); pump();
        check(window.m_currentPath == batchDestination, "tree left returns to parent node");
        window.m_pDirTree->SetFocus(); SetFocus(window.m_hWnd);
        std::vector<CMainWnd::ClipboardItem> treeSelection; window.CollectSelectedItems(treeSelection);
        check(treeSelection.size() == 1 && treeSelection[0].path == batchDestination && treeSelection[0].isDir,
            "tree keyboard operations target current folder rather than stale file selection");
        window.FocusFileView();
        check(dialogs.Dismissed() == 0, "no dialog appeared during the keyboard and file-operation checks");
        OleSetClipboard(savedClipboard);
        if (savedClipboard) { OleFlushClipboard(); savedClipboard->Release(); }
        return failures;
    }
    static int CheckHandlers(CMainWnd& window) {
        const auto testKey = L"Software\\FastFileHandlerRegression_" + std::to_wstring(GetCurrentProcessId());
        HKEY root = nullptr, user = nullptr, merged = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, testKey.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &root, nullptr)
            || RegCreateKeyExW(root, L"User", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &user, nullptr)
            || RegCreateKeyExW(root, L"Merged", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &merged, nullptr)) return 1;
        // Overrides are local to this test process. No real user associations are changed.
        const LONG userResult = RegOverridePredefKey(HKEY_CURRENT_USER, user);
        const LONG mergedResult = userResult == ERROR_SUCCESS ? RegOverridePredefKey(HKEY_CLASSES_ROOT, merged) : ERROR_ACCESS_DENIED;
        int failures = 0;
        if (userResult != ERROR_SUCCESS || mergedResult != ERROR_SUCCESS) ++failures;
        else {
            auto write = [&](const std::wstring& key, const std::wstring& value) {
                HKEY handle = nullptr;
                if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &handle, nullptr)) { ++failures; return; }
                if (RegSetValueExW(handle, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                    DWORD((value.size() + 1) * sizeof(wchar_t)))) ++failures;
                RegCloseKey(handle);
            };
            auto check = [&](bool value, const char* name) { if (!value) { std::cerr << "FAIL " << name << '\n'; ++failures; } };
            for(const auto* cls:{L"Folder",L"Directory",L"Drive"}) {
                const auto shell=std::wstring(L"Software\\Classes\\")+cls+L"\\shell";
                write(shell,L"FastFile.WindowsExplorer");
                write(shell+L"\\FastFile.WindowsExplorer",L"使用 Windows 文件资源管理器打开");
                write(shell+L"\\FastFile.WindowsExplorer\\command",L"\"C:\\Windows\\explorer.exe\" \"%1\\.\"");
                write(shell+L"\\FastFile.open\\command",L"\"C:\\Old\\FastFile.exe\" --open \"%1\"");
                write(shell+L"\\OtherTool\\command",L"untouched");
            }
            write(L"Software\\Classes\\Drive\\shell",L"FastFile.open");
            check(CMainWnd::RestoreNativeFolderHandlers(),"cleanup restores native Shell defaults without installing a replacement");
            for(const auto* cls:{L"Folder",L"Directory",L"Drive"}) {
                const auto shell=std::wstring(L"Software\\Classes\\")+cls+L"\\shell";
                wchar_t text[512]{};DWORD bytes=sizeof(text);
                check(RegGetValueW(HKEY_CURRENT_USER,shell.c_str(),nullptr,RRF_RT_REG_SZ,nullptr,text,&bytes)==ERROR_FILE_NOT_FOUND,
                    "owned default override is removed so Windows machine default is inherited");
                HKEY entry=nullptr;
                for(const auto* verb:{L"FastFile.open",L"FastFile.WindowsExplorer"}) {
                    check(RegOpenKeyExW(HKEY_CURRENT_USER,(shell+L"\\"+verb).c_str(),0,KEY_READ,&entry)==ERROR_FILE_NOT_FOUND,
                        "obsolete owned verbs do not remain in native context menus");
                    if(entry) {RegCloseKey(entry);entry=nullptr;}
                }
                check(RegOpenKeyExW(HKEY_CURRENT_USER,(shell+L"\\OtherTool\\command").c_str(),0,KEY_READ,&entry)==ERROR_SUCCESS,
                    "third-party context menu commands are preserved");
                if(entry)RegCloseKey(entry);
            }
            write(L"Software\\Classes\\Folder\\shell",L"OtherManager");
            write(L"Software\\Classes\\Folder\\shell\\OtherManager\\command",L"\"C:\\Other\\OtherManager.exe\" \"%1\"");
            check(CMainWnd::RestoreNativeFolderHandlers(),"repeated cleanup is harmless when no owned entries remain");
            wchar_t value[128]{};DWORD bytes=sizeof(value);
            check(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Classes\\Folder\\shell",nullptr,RRF_RT_REG_SZ,nullptr,value,&bytes)==ERROR_SUCCESS
                && wcscmp(value,L"OtherManager")==0,"cleanup never manages another application default");
            write(L"Software\\Classes\\Folder\\shell",L"FastFile.open");
            write(L"Software\\Classes\\Folder\\shell\\FastFile.open\\command",L"\"C:\\Other\\OtherManager.exe\" \"%1\"");
            check(CMainWnd::RestoreNativeFolderHandlers(),"same-name foreign registration is not treated as owned");
            bytes=sizeof(value);
            check(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Classes\\Folder\\shell",nullptr,RRF_RT_REG_SZ,nullptr,value,&bytes)==ERROR_SUCCESS
                && wcscmp(value,L"FastFile.open")==0,"cleanup preserves the default of a same-name foreign command");
        }

        if(userResult==ERROR_SUCCESS && mergedResult==ERROR_SUCCESS) {
            auto check=[&](bool ok,const char* name){if(!ok){std::cerr<<"FAIL "<<name<<'\n';++failures;}};
            auto write=[&](const std::wstring& key,const std::wstring& text,DWORD type=REG_SZ) {
                HKEY handle=nullptr;RegCreateKeyExW(HKEY_CURRENT_USER,key.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&handle,nullptr);
                RegSetValueExW(handle,nullptr,0,type,reinterpret_cast<const BYTE*>(text.c_str()),DWORD((text.size()+1)*sizeof(wchar_t)));RegCloseKey(handle);
            };
            auto read=[&](const std::wstring& key,DWORD* type=nullptr) {
                wchar_t buffer[32768]{};DWORD size=sizeof(buffer),kind=0;
                LONG result=RegGetValueW(HKEY_CURRENT_USER,key.c_str(),nullptr,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ|RRF_NOEXPAND,&kind,buffer,&size);
                if(type)*type=kind;return result==ERROR_SUCCESS?std::wstring(buffer):std::wstring();
            };
            const auto folder=std::wstring(L"Software\\Classes\\Folder\\shell");
            const auto directory=std::wstring(L"Software\\Classes\\Directory\\shell");
            const auto drive=std::wstring(L"Software\\Classes\\Drive\\shell");
            const auto computer=std::wstring(L"Software\\Classes\\CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\shell");
            write(folder,L"原始文件夹命令");write(directory,L"%原始目录命令%",REG_EXPAND_SZ);write(drive,L"原始磁盘命令");
            FastFileSettings enabled;enabled.contextMenu=true;enabled.defaultFolders=true;enabled.defaultComputer=true;
            write(directory+L"\\open\\command",L"original open",REG_EXPAND_SZ);
            write(directory+L"\\open\\ddeexec",L"original DDE");
            check(CMainWnd::ApplySystemIntegration(enabled),"explicit integration registers folders drives and Computer");
            auto publish=[&] {
                RegDeleteTreeW(merged,nullptr);
                check(RegCopyTreeW(HKEY_CURRENT_USER,L"Software\\Classes",merged)==ERROR_SUCCESS,"publish isolated effective classes for diagnostics");
            };
            auto effective=[&](const wchar_t* key,const wchar_t* name,const wchar_t* text) {
                HKEY handle=nullptr;RegCreateKeyExW(merged,key,0,nullptr,0,KEY_ALL_ACCESS,nullptr,&handle,nullptr);
                RegSetValueExW(handle,name,0,REG_SZ,reinterpret_cast<const BYTE*>(text),DWORD((wcslen(text)+1)*sizeof(wchar_t)));RegCloseKey(handle);
            };
            publish();
            const auto configured=CMainWnd::DetectSystemIntegration();
            check(configured.foldersReady && configured.computerReady && configured.menuReady,"diagnostics verify effective commands for the current executable");
            RegDeleteTreeW(merged,L"Drive\\shell\\FastFile.SettingsOpen");
            check(!CMainWnd::DetectSystemIntegration().menuReady,"context menu diagnostics verify all folder and drive classes");
            publish();
            const auto oldSettings=window.m_settings;
            auto integrationOnly=enabled;integrationOnly.startup=2;integrationOnly.startupPath=L"Z:\\missing-integration-startup";
            integrationOnly.navigationFont=16;
            check(window.CommitIntegrationSettings(integrationOnly) && window.m_settings.startup==oldSettings.startup
                && window.m_settings.navigationFont==oldSettings.navigationFont,
                "repair applies only integration and does not validate or commit unrelated staged preferences");
            window.m_settings=oldSettings;
            effective(L"Directory\\shell\\open\\command",L"DelegateExecute",L"{11dbb47c-a525-400b-9e80-a54615a090c0}");
            auto detected=CMainWnd::DetectSystemIntegration();
            check(!detected.foldersReady && detected.details.find(L"Windows 资源管理器")!=std::wstring::npos,
                "Explorer delegate takes precedence over a seemingly valid FastFile command");
            publish();effective(L"Directory\\shell",nullptr,L"OtherTool");
            effective(L"Directory\\shell\\OtherTool\\command",nullptr,L"\"C:\\Other\\Manager.exe\" \"%1\"");
            detected=CMainWnd::DetectSystemIntegration();
            check(!detected.foldersReady && detected.otherManager && read(directory)==L"FastFile.SettingsOpen",
                "read-only diagnostics detect another effective manager without rewriting configured flags or commands");
            publish();effective(L"Directory\\shell\\open\\command",nullptr,L"\"C:\\Old\\FastFile.exe\" --shell-folder \"%1\"");
            detected=CMainWnd::DetectSystemIntegration();
            check(!detected.foldersReady && detected.details.find(L"其他路径")!=std::wstring::npos,
                "another FastFile executable is not reported as the current running version");
            publish();effective(L"Directory\\shell",nullptr,L"BrokenVerb");
            check(!CMainWnd::DetectSystemIntegration().foldersReady,"a broken explicit default cannot be mistaken for the inherited Folder default");
            publish();
            for(const auto& shell:{directory,drive})for(const auto* action:{L"open",L"explore",L"opennewwindow"}) {
                const auto verb=shell+L"\\"+action;
                check(read(verb+L"\\command").find(L"--shell-folder")!=std::wstring::npos,
                    "explicit standard folder actions are routed to FastFile");
                wchar_t delegate[64]{};DWORD size=sizeof(delegate);
                check(RegGetValueW(HKEY_CURRENT_USER,(verb+L"\\command").c_str(),L"DelegateExecute",RRF_RT_REG_SZ,nullptr,delegate,&size)==ERROR_SUCCESS
                    && !delegate[0],"standard actions suppress inherited Explorer delegates");
            }
            check(read(folder)==L"原始文件夹命令","generic Folder namespace default is preserved for Windows virtual folders");
            for(const auto& shell:{directory,drive,computer})
                check(read(shell)==L"FastFile.SettingsOpen" && read(shell+L"\\FastFile.SettingsOpen\\command").find(L"--shell-folder")!=std::wstring::npos,
                    "default commands use the explicit integration verb and quoted path activation");
            check(CMainWnd::RestoreNativeFolderHandlers(),"legacy migration succeeds with new integration enabled");
            check(read(directory)==L"FastFile.SettingsOpen","startup cleanup cannot undo user-enabled settings integration");
            FastFileSettings state;CMainWnd::ReadSystemIntegration(state);
            check(state.contextMenu && state.defaultFolders && state.defaultComputer,"integration switches survive reloading state");
            enabled.explorerWindowTakeover=true;
            check(CMainWnd::ApplySystemIntegration(enabled),"window takeover can be enabled independently in the integration journal");
            CMainWnd::ReadSystemIntegration(state);check(state.explorerWindowTakeover,"window takeover survives a program restart");
            enabled.explorerWindowTakeover=false;
            check(CMainWnd::ApplySystemIntegration(enabled),"window takeover can be disabled without removing default associations");
            write(directory,L"");write(drive,L"");
            check(CMainWnd::ApplySystemIntegration(enabled) && read(directory)==L"FastFile.SettingsOpen"
                && read(drive)==L"FastFile.SettingsOpen",
                "saving unchanged enabled settings repairs missing effective default verbs");
            write(directory,L"");write(drive,L"AnotherAppAfterEnable");
            check(CMainWnd::RepairOwnedSystemIntegration() && read(directory)==L"FastFile.SettingsOpen"
                && read(drive)==L"AnotherAppAfterEnable",
                "startup repairs missing owned defaults without replacing a later explicit third-party default");
            check(CMainWnd::ApplySystemIntegration(enabled),"restore enabled fixture after third-party default check");
            enabled.defaultFolders=false;
            check(CMainWnd::ApplySystemIntegration(enabled),"folder defaults can be disabled while keeping context menu and Computer enabled");
            DWORD type=0;
            check(read(directory+L"\\open\\command",&type)==L"original open" && type==REG_EXPAND_SZ
                && read(directory+L"\\open\\ddeexec")==L"original DDE",
                "disabling restores the entire original explicit action including type and DDE");
            check(read(drive+L"\\explore\\command").empty(),"disabling removes an originally absent explicit action override");
            check(read(folder)==L"原始文件夹命令" && read(directory,&type)==L"%原始目录命令%" && type==REG_EXPAND_SZ
                && read(drive)==L"原始磁盘命令","disable restores exact original defaults including registry type");
            check(!read(folder+L"\\FastFile.SettingsOpen\\command").empty() && read(computer)==L"FastFile.SettingsOpen",
                "independent integration switches keep requested commands");
            FastFileSettings off;
            check(CMainWnd::ApplySystemIntegration(off),"restore Windows disables all opt-in integration");
            HKEY key=nullptr;
            check(read(computer).empty() && RegOpenKeyExW(HKEY_CURRENT_USER,(computer+L"\\FastFile.SettingsOpen").c_str(),0,KEY_READ,&key)==ERROR_FILE_NOT_FOUND,
                "Computer absent default is restored without an invented replacement");if(key)RegCloseKey(key);
            enabled.defaultFolders=true;
            check(CMainWnd::ApplySystemIntegration(enabled),"integration can be re-enabled after restoration");
            write(directory,L"AnotherAppAfterEnable");write(folder,L"AnotherAppAfterEnable");
            write(drive+L"\\explore\\command",L"later third-party action");
            check(CMainWnd::ApplySystemIntegration(off) && read(directory)==L"AnotherAppAfterEnable" && read(folder)==L"AnotherAppAfterEnable",
                "restoration preserves an application chosen after FastFile was enabled");
            check(read(drive+L"\\explore\\command")==L"later third-party action",
                "restoration preserves a later third-party explicit action command");
            check(CMainWnd::ApplySystemIntegration(enabled),"prepare interrupted registration recovery");
            HKEY flags=nullptr;RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\FastFile",0,KEY_SET_VALUE,&flags);
            for(const wchar_t* name:{L"IntegrationMenu",L"IntegrationFolders",L"IntegrationComputer"})RegDeleteValueW(flags,name);
            RegCloseKey(flags);
            check(CMainWnd::ApplySystemIntegration(off) && read(directory)==L"AnotherAppAfterEnable"
                && read(directory+L"\\FastFile.SettingsOpen\\command").empty(),
                "restore recovers owned registrations even if interrupted before state flags were written");
            write(directory+L"\\FastFile.SettingsOpen\\command",L"foreign program");
            const auto before=read(directory);
            check(!CMainWnd::ApplySystemIntegration(enabled) && read(directory)==before && read(folder)==L"AnotherAppAfterEnable",
                "foreign same-name registration blocks enable without partial default changes");
            check(read(directory+L"\\FastFile.SettingsOpen\\command")==L"foreign program","foreign context command is never overwritten");
        }
        RegOverridePredefKey(HKEY_CLASSES_ROOT, nullptr);
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegCloseKey(merged); RegCloseKey(user); RegCloseKey(root);
        RegDeleteTreeW(HKEY_CURRENT_USER, testKey.c_str());
        return failures;
    }

    inline static HWND settingsTestOwner=nullptr;
    inline static int explorerCloseCalls=0;
    inline static bool explorerCloseAllowed=true;
    static bool CloseTestExplorer(const CMainWnd::ExplorerSnapshot& source) {
        ++explorerCloseCalls;
        DWORD pid=0;GetWindowThreadProcessId(source.window,&pid);
        return explorerCloseAllowed && pid==GetCurrentProcessId() && DestroyWindow(source.window)!=FALSE;
    }
    static int CheckExplorerTransfer(CMainWnd& window,const std::wstring& fixture) {
        int failures=0;
        auto check=[&](bool ok,const char* name){if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}};
        auto pump=[] {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
        }};
        const auto original=window.m_settings;
        window.StopExplorerTakeover();window.m_settings.explorerWindowTakeover=false;
        CMainWnd::s_closeExplorerForTest=CloseTestExplorer;explorerCloseCalls=0;explorerCloseAllowed=true;
        auto makeSource=[&](const std::wstring& path) {
            HWND source=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"FastFile owned transfer fixture",WS_POPUP,-32000,-32000,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            return CMainWnd::ExplorerSnapshot{source,GetCurrentProcessId(),path,{}};
        };
        auto existing=makeSource(fixture);
        window.ProcessExplorerSnapshots({existing},false);
        check(window.m_explorerTransfers.empty() && IsWindow(existing.window),"disabled window takeover does not discover or close windows");
        window.m_settings.explorerWindowTakeover=true;
        window.ProcessExplorerSnapshots({existing},true);
        window.ProcessExplorerSnapshots({existing},false);
        check(window.m_explorerTransfers[existing.window].ignored && explorerCloseCalls==0,
            "Explorer windows present at enable time are never transferred or closed");
        window.ProcessExplorerSnapshots({},false);
        window.ProcessExplorerSnapshots({existing},false);
        check(window.m_explorerTransfers[existing.window].ignored && explorerCloseCalls==0,
            "an existing window stays protected when it is temporarily absent from a Shell enumeration");
        window.StopExplorerTakeover();
        auto source=makeSource(fixture);source.selection={fixture+L"\\sample.txt"};
        window.ProcessExplorerSnapshots({source},false);
        check(explorerCloseCalls==0,"new source waits for a stable observation");
        window.m_explorerTransfers[source.window].changedAt-=1100;
        window.ProcessExplorerSnapshots({source},false);
        check(explorerCloseCalls==0 && window.m_externalOpensPending>0,
            "source window stays open while FastFile is still resolving the folder");
        const auto deadline=GetTickCount64()+8000;
        while(GetTickCount64()<deadline && IsWindow(source.window)) {
            pump();window.ProcessExplorerSnapshots({source},false);Sleep(10);
        }
        std::vector<std::pair<std::wstring,bool>> selection;
        check(explorerCloseCalls==1 && !IsWindow(source.window)
            && window.m_shellBrowser->IsNavigationCompleteAt(fixture)
            && window.m_shellBrowser->GetSelection(selection) && selection.size()==1
            && CMainWnd::PathEquals(selection.front().first,source.selection.front()),
            "source closes exactly once after the real Shell view opens the folder and selects the requested file");
        window.StopExplorerTakeover();explorerCloseCalls=0;
        auto failed=makeSource(fixture);
        const auto oldProbe=CMainWnd::s_folderProbe;
        CMainWnd::s_folderProbe=[](const std::wstring&)->DWORD{return INVALID_FILE_ATTRIBUTES;};
        window.ProcessExplorerSnapshots({failed},false);
        window.m_explorerTransfers[failed.window].changedAt-=1100;
        window.ProcessExplorerSnapshots({failed},false);
        const auto failureDeadline=GetTickCount64()+3000;
        while(window.m_externalOpensPending && GetTickCount64()<failureDeadline){pump();Sleep(10);}
        window.ProcessExplorerSnapshots({failed},false);
        CMainWnd::s_folderProbe=oldProbe;
        check(explorerCloseCalls==0 && IsWindow(failed.window),
            "a failed folder resolution cannot close the source merely because FastFile has an old view of the same path");
        window.StopExplorerTakeover();explorerCloseCalls=0;
        auto busy=makeSource(fixture);EnableWindow(busy.window,FALSE);
        window.ProcessExplorerSnapshots({busy},false);
        window.m_explorerTransfers[busy.window].changedAt-=1100;
        window.ProcessExplorerSnapshots({busy},false);
        const auto busyDeadline=GetTickCount64()+3000;
        while(window.m_externalOpensPending && GetTickCount64()<busyDeadline){pump();Sleep(10);}
        window.ProcessExplorerSnapshots({busy},false);
        check(explorerCloseCalls==0 && IsWindow(busy.window),"a source disabled by an unfinished operation is retained");
        EnableWindow(busy.window,TRUE);
        window.StopExplorerTakeover();explorerCloseCalls=0;explorerCloseAllowed=false;
        auto retained=makeSource(fixture);
        window.ProcessExplorerSnapshots({retained},false);
        window.m_explorerTransfers[retained.window].changedAt-=1100;
        window.ProcessExplorerSnapshots({retained},false);
        const auto retainedDeadline=GetTickCount64()+8000;
        while(GetTickCount64()<retainedDeadline && !window.m_explorerTransfers[retained.window].ignored) {
            pump();window.ProcessExplorerSnapshots({retained},false);Sleep(10);
        }
        check(explorerCloseCalls==1 && IsWindow(retained.window),"changed or multi-tab source is retained when the final close verification fails");
        window.StopExplorerTakeover();explorerCloseCalls=0;
        auto missing=makeSource(fixture+L"\\does-not-exist");
        window.ProcessExplorerSnapshots({missing},false);
        auto& expired=window.m_explorerTransfers[missing.window];expired.started=true;expired.deadline=GetTickCount64()-1;
        window.ProcessExplorerSnapshots({missing},false);
        check(expired.ignored && IsWindow(missing.window) && explorerCloseCalls==0,
            "failed or expired transfers preserve the original window");
        window.StopExplorerTakeover();
        auto changed=makeSource(fixture);
        window.ProcessExplorerSnapshots({changed},false);
        window.m_explorerTransfers[changed.window].started=true;
        changed.path=fixture+L"\\Battle.net";
        window.ProcessExplorerSnapshots({changed},false);
        check(!window.m_explorerTransfers[changed.window].started && explorerCloseCalls==0,
            "source navigation resets the transfer rather than closing an outdated folder");
        for(HWND owned:{existing.window,source.window,failed.window,busy.window,retained.window,missing.window,changed.window})if(IsWindow(owned))DestroyWindow(owned);
        window.StopExplorerTakeover();CMainWnd::s_closeExplorerForTest=nullptr;window.m_settings=original;
        return failures;
    }
    static int CheckIntegration(CMainWnd& window,const std::wstring& root) {
        int failures=CheckHandlers(window);
        failures+=CheckExplorerTransfer(window,root+L"\\Program Files");
        failures+=CheckPreferences(window,root);
        return failures;
    }
    static int CheckExplorerAgentLive(CMainWnd& window,const std::wstring& fixture) {
        int failures=0;
        auto check=[&](bool ok,const char* name){if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}};
        auto pump=[] {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))
            if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}};
        wchar_t hive[256]{};GetEnvironmentVariableW(L"FASTFILE_TEST_HIVE",hive,_countof(hive));
        HKEY user=nullptr,classes=nullptr;
        RegCreateKeyExW(HKEY_CURRENT_USER,(std::wstring(hive)+L"\\User").c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&user,nullptr);
        RegCreateKeyExW(HKEY_CURRENT_USER,(std::wstring(hive)+L"\\Classes").c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&classes,nullptr);
        if(!user || !classes)return 1;
        // Keep a legitimate pending Shell registration while real windows arrive.
        IShellWindows* pendingShell=nullptr;long pendingCookie=0;PIDLIST_ABSOLUTE pendingLocation=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&pendingShell)))
            && SUCCEEDED(SHParseDisplayName(fixture.c_str(),nullptr,&pendingLocation,0,nullptr))) {
            VARIANT where{},root{};VariantInit(&where);VariantInit(&root);
            InitVariantFromBuffer(pendingLocation,ILGetSize(pendingLocation),&where);
            check(SUCCEEDED(pendingShell->RegisterPending(GetCurrentThreadId(),&where,&root,SWC_BROWSER,&pendingCookie)),
                "pending Shell entry is registered for scan regression");
            VariantClear(&where);CoTaskMemFree(pendingLocation);
        } else check(false,"pending Shell regression prerequisites available");
        window.StopExplorerTakeover();
        RegOverridePredefKey(HKEY_CURRENT_USER,user);
        FastFileSettings enabled;enabled.SetDefaultManager(true);enabled.confirmClose=false;enabled.startup=1;enabled.reuseTabs=false;
        check(CMainWnd::ApplySystemIntegration(enabled),"agent live test enables isolated registry flags");
        auto saved=window.m_settings;window.m_settings=enabled;enabled.Save(FastFileSettings::FilePath());
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,_countof(executable));
        auto command=L"\""+std::wstring(executable)+L"\" --agent-lifecycle-launcher";
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION agent{};
        const bool started=CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&agent)!=FALSE;
        check(started,"independent agent process starts");
        if(started) {
            CloseHandle(agent.hThread);Sleep(1500);
            wchar_t system[32768]{};GetWindowsDirectoryW(system,_countof(system));
            const auto explorer=std::wstring(system)+L"\\explorer.exe";
            const auto selected=fixture+L"\\Battle.net\\agent-selected.txt";
            HANDLE file=CreateFileW(selected.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
            HWND receiver=window.m_hWnd;
            HANDLE cold=nullptr;
            for(int iteration=0;iteration<2;++iteration) {
                const auto folder=iteration?fixture+L"\\Battle.net":fixture;
                if(iteration) {
                    window.m_closeConfirmed=true;PostMessageW(window.m_hWnd,WM_CLOSE,0,0);
                    const auto until=GetTickCount64()+4000;
                    while(IsWindow(window.m_hWnd) && GetTickCount64()<until){pump();Sleep(10);}
                    check(!IsWindow(receiver),"closing receiver destroys GUI while agent remains alive");receiver=nullptr;
                    check(WaitForSingleObject(agent.hProcess,0)==WAIT_TIMEOUT,"agent survives receiver close");
                } else ShowWindow(receiver,SW_HIDE);
                auto launch=L"\""+explorer+L"\" /n,/separate,"+(iteration?L"/select,":L"")+L"\""+(iteration?selected:folder)+L"\"";
                PROCESS_INFORMATION source{};
                check(CreateProcessW(explorer.c_str(),launch.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&source)!=FALSE,"real Explorer request starts");
                if(source.hThread)CloseHandle(source.hThread);if(source.hProcess)CloseHandle(source.hProcess);
                bool sourceSeen=false,transferred=false;
                const auto deadline=GetTickCount64()+35000;
                do {
                    pump();
                    const int count=ExplorerWindowsUnder(folder,receiver);
                    if(count>0)sourceSeen=true;
                    if(iteration && !receiver) {
                        EnumWindows([](HWND hwnd,LPARAM value)->BOOL {
                            wchar_t name[128]{};GetClassNameW(hwnd,name,_countof(name));DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
                            if(!wcscmp(name,L"FastFile_MainWnd") && pid!=GetCurrentProcessId()) {
                                *reinterpret_cast<HWND*>(value)=hwnd;return FALSE;
                            }return TRUE;
                        },reinterpret_cast<LPARAM>(&receiver));
                        if(receiver) {
                            DWORD pid=0;GetWindowThreadProcessId(receiver,&pid);cold=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
                        }
                    }
                    transferred=sourceSeen && count==0 && receiver && IsWindowVisible(receiver)
                        && !GetPropW(receiver,L"FastFile.ExternalOpenPending");
                    if(!iteration)transferred=transferred && window.m_shellBrowser && window.m_shellBrowser->IsNavigationCompleteAt(folder)
                        && window.m_shellBrowser->HasVisibleViewBounds();
                    if(transferred)break;Sleep(15);
                }while(GetTickCount64()<deadline);
                check(transferred,iteration?"agent cold-starts GUI and transfers real selected-file request":"agent transfers real Explorer request through IPC to existing GUI");
                if(!transferred)break;
            }
            if(cold) {
                check(GetPropW(receiver,L"FastFile.Test.ForcedNewWindow")!=nullptr,
                    "cold agent launch bypasses windows belonging to other installations");
                PostMessageW(receiver,WM_CLOSE,0,0);
                check(WaitForSingleObject(cold,6000)==WAIT_OBJECT_0,"cold GUI exits on close with integration still enabled");
                if(WaitForSingleObject(cold,0)==WAIT_TIMEOUT)TerminateProcess(cold,3);CloseHandle(cold);
                const int savedTabs=GetPrivateProfileIntW(L"Tabs",L"Count",-1,CMainWnd::GetSessionFilePath().c_str());
                check(savedTabs==1,"cold agent request is delivered once even when tab reuse is disabled");
            }
            FastFileSettings off;check(CMainWnd::ApplySystemIntegration(off),"agent live test disables isolated integration");
            check(WaitForSingleObject(agent.hProcess,6000)==WAIT_OBJECT_0,"independent agent stops after disable");
            if(WaitForSingleObject(agent.hProcess,0)==WAIT_TIMEOUT)TerminateProcess(agent.hProcess,3);CloseHandle(agent.hProcess);
        }
        auto logPath=FastFileSettings::FilePath();logPath=logPath.substr(0,logPath.find_last_of(L"\\/"))+L"\\explorer-agent.log";
        CopyFileW(logPath.c_str(),L"agent-live-trace.log",FALSE);
        ExplorerWindowsUnder(fixture,window.m_hWnd,true);
        if(pendingShell){if(pendingCookie)pendingShell->Revoke(pendingCookie);pendingShell->Release();}
        window.m_settings=saved;
        RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);RegCloseKey(classes);RegCloseKey(user);
        RegDeleteTreeW(HKEY_CURRENT_USER,hive);
        return failures;
    }
    static int CheckExplorerLive(CMainWnd& window,const std::wstring& fixture) {
        auto pump=[] {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
        }};
        const auto original=window.m_settings;
        window.StopExplorerTakeover();CMainWnd::s_explorerTestRoot=fixture;
        window.m_settings.explorerWindowTakeover=true;window.UpdateExplorerTakeover();
        const auto baselineDeadline=GetTickCount64()+10000;
        while(!window.m_explorerBaseline && GetTickCount64()<baselineDeadline){pump();window.PollExplorerTakeover();Sleep(20);}
        if(!window.m_explorerBaseline){std::cerr<<"FAIL native Explorer scanner could not establish its baseline\n";window.StopExplorerTakeover();window.m_settings=original;CMainWnd::s_explorerTestRoot.clear();return 1;}
        wchar_t system[32768]{};GetWindowsDirectoryW(system,_countof(system));
        const auto executable=std::wstring(system)+L"\\explorer.exe";
        int failures=0;
        const auto selectedFolder=fixture+L"\\Battle.net";
        const auto selectedFile=selectedFolder+L"\\native-selection.txt";
        HANDLE file=CreateFileW(selectedFile.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        for(int iteration=0;iteration<2;++iteration) {
            if(!iteration)ShowWindow(window.m_hWnd,SW_HIDE);
            const auto target=iteration?selectedFolder:fixture;
            std::wstring command=L"\""+executable+L"\" /n,/separate,"+(iteration?L"/select,":L"")+L"\""+(iteration?selectedFile:target)+L"\"";
            STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
            if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&child)) {
                std::cerr<<"FAIL direct Explorer launch "<<GetLastError()<<'\n';++failures;break;
            }
            CloseHandle(child.hThread);CloseHandle(child.hProcess);
            HWND observed=nullptr;bool closed=false;
            std::vector<std::wstring> lastSourceSelection;
            const auto deadline=GetTickCount64()+35000;
            while(GetTickCount64()<deadline && !closed) {
                pump();window.PollExplorerTakeover();
                for(const auto& entry:window.m_explorerTransfers)
                    if(CMainWnd::PathEquals(entry.second.source.path,target)) {
                        observed=entry.first;lastSourceSelection=entry.second.source.selection;
                    }
                std::vector<std::pair<std::wstring,bool>> actualSelection;
                const bool readySelection=!iteration || (window.m_shellBrowser && window.m_shellBrowser->GetSelection(actualSelection)
                    && actualSelection.size()==1 && CMainWnd::PathEquals(actualSelection.front().first,selectedFile));
                closed=observed && !IsWindow(observed) && IsWindowVisible(window.m_hWnd) && window.m_shellBrowser
                    && window.m_shellBrowser->IsNavigationCompleteAt(target) && readySelection;
                Sleep(20);
            }
            std::vector<std::pair<std::wstring,bool>> selected;
            const bool selectionOk=!iteration || (window.m_shellBrowser && window.m_shellBrowser->GetSelection(selected)
                && selected.size()==1 && CMainWnd::PathEquals(selected.front().first,selectedFile));
            std::cout<<"Native Explorer observed "<<observed<<", closed "<<closed<<", FastFile folder confirmed "
                <<(window.m_shellBrowser && window.m_shellBrowser->IsNavigationCompleteAt(target))<<", selection confirmed "<<selectionOk<<'\n';
            if(!closed || !IsWindowVisible(window.m_hWnd) || !window.m_shellBrowser || !window.m_shellBrowser->IsNavigationCompleteAt(target) || !selectionOk) {
                std::cerr<<"scan entries="<<window.m_explorerTransfers.size()<<" sequence="<<window.m_explorerScanSequence<<'\n';
                std::cerr<<"source still exists="<<IsWindow(observed)<<" actual selection count="<<selected.size()<<'\n';
                std::cerr<<"last source selection count="<<lastSourceSelection.size()<<'\n';
                for(const auto& item:lastSourceSelection)std::wcerr<<L"source selection="<<item<<L"\n";
                for(const auto& item:selected)std::wcerr<<L"actual selection="<<item.first<<L"\n";
                std::wcerr<<L"current="<<window.m_currentPath<<L" scan="<<window.ExplorerTakeoverStatus()<<L"\n";
                for(const auto& entry:window.m_explorerTransfers)std::wcerr<<L"transfer="<<entry.first<<L" path="<<entry.second.source.path
                    <<L" ignored="<<entry.second.ignored<<L" started="<<entry.second.started<<L" resolved="<<entry.second.resolved<<L"\n";
                std::cerr<<"FAIL native Explorer direct/selected-file request must transfer and close\n";++failures;break;
            }
            const auto settle=GetTickCount64()+2000;
            do {pump();window.PollExplorerTakeover();Sleep(20);}while(GetTickCount64()<settle);
        }
        window.StopExplorerTakeover();window.m_settings=original;CMainWnd::s_explorerTestRoot.clear();
        return failures;
    }
    inline static bool settingsTestSave=false,settingsDialogPages=false;
    inline static bool settingsIntegrationUi=false,settingsAboutUi=false;
    static void CALLBACK ExerciseSettingsDialog(HWND,UINT,UINT_PTR timer,DWORD) {
        HWND dialog=nullptr;
        EnumThreadWindows(GetCurrentThreadId(),[](HWND candidate,LPARAM value)->BOOL {
            wchar_t title[64]{};GetWindowTextW(candidate,title,_countof(title));
            if(wcscmp(title,L"设置")==0 && GetWindow(candidate,GW_OWNER)==settingsTestOwner) {
                *reinterpret_cast<HWND*>(value)=candidate;return FALSE;
            }return TRUE;
        },reinterpret_cast<LPARAM>(&dialog));
        // DialogBox can pump during creation; wait until initialization and showing
        // have finished before checking visibility or sending modal commands.
        if(!dialog || !IsWindowVisible(dialog) || !GetDlgItem(dialog,127))return;
        KillTimer(nullptr,timer);
        HWND tabs=GetDlgItem(dialog,100);
        settingsDialogPages=TabCtrl_GetItemCount(tabs)==5;
        for(int page=0;page<4;++page) {
            TabCtrl_SetCurSel(tabs,page);
            NMHDR changed{tabs,100,TCN_SELCHANGE};SendMessageW(dialog,WM_NOTIFY,100,reinterpret_cast<LPARAM>(&changed));
            const int visibleIds[]={110,116,121,127};
            settingsDialogPages=settingsDialogPages && IsWindowVisible(GetDlgItem(dialog,visibleIds[page]));
        }
        wchar_t report[32768]{};GetWindowTextW(GetDlgItem(dialog,194),report,_countof(report));
        settingsIntegrationUi=GetDlgItem(dialog,127) && !GetDlgItem(dialog,126)
            && !GetDlgItem(dialog,128) && !GetDlgItem(dialog,129)
            && !GetDlgItem(dialog,192) && !GetDlgItem(dialog,193) && !GetDlgItem(dialog,191)
            && GetDlgItem(dialog,195) && IsWindowVisible(GetDlgItem(dialog,195))
            && std::wstring(report).find(L"当前状态")!=std::wstring::npos;
        if(settingsTestSave) {
            SendMessageW(GetDlgItem(dialog,127),BM_SETCHECK,BST_CHECKED,0);
        }
        RECT details{},save{};
        GetWindowRect(GetDlgItem(dialog,194),&details);GetWindowRect(GetDlgItem(dialog,IDOK),&save);
        settingsIntegrationUi=settingsIntegrationUi && details.bottom<=save.top;
        TabCtrl_SetCurSel(tabs,4);
        NMHDR aboutChanged{tabs,100,TCN_SELCHANGE};SendMessageW(dialog,WM_NOTIFY,100,reinterpret_cast<LPARAM>(&aboutChanged));
        wchar_t aboutText[4096]{},programPath[32768]{};
        GetWindowTextW(GetDlgItem(dialog,196),aboutText,_countof(aboutText));
        GetWindowTextW(GetDlgItem(dialog,197),programPath,_countof(programPath));
        settingsAboutUi=IsWindowVisible(GetDlgItem(dialog,196)) && !IsWindowVisible(GetDlgItem(dialog,127))
            && std::wstring(aboutText).find(FASTFILE_VERSION_W)!=std::wstring::npos
            && std::wstring(aboutText).find(L"后台代理")!=std::wstring::npos
            && std::wstring(programPath)==FastFileAbout::ExecutablePath()
            && (GetWindowLongPtrW(GetDlgItem(dialog,196),GWL_STYLE)&ES_READONLY);
        for(int id=198;id<=205;++id) {
            RECT button{};GetWindowRect(GetDlgItem(dialog,id),&button);
            settingsAboutUi=settingsAboutUi && IsWindowVisible(GetDlgItem(dialog,id)) && button.bottom<=save.top;
        }
        SendMessageW(dialog,WM_COMMAND,198,0);
        // Exercise the real Unicode clipboard button, retaining every original
        // clipboard format through its IDataObject instead of copying user text.
        // A live OleGetClipboard proxy refers to the backing clipboard that the
        // copy button clears. Restoring then flushing that proxy recursively reads
        // itself and can overflow the stack; retain an independent format snapshot.
        IDataObject* originalClipboard=SnapshotClipboard();
        if(originalClipboard) {
            SendMessageW(dialog,WM_COMMAND,201,0);
            bool copied=false;
            if(OpenClipboard(dialog)) {
                const auto data=GetClipboardData(CF_UNICODETEXT);
                const auto value=data?static_cast<const wchar_t*>(GlobalLock(data)):nullptr;
                if(value) {
                    const std::wstring diagnostic(value);
                    copied=diagnostic.find(FASTFILE_VERSION_W)!=std::wstring::npos
                        && diagnostic.find(programPath)==std::wstring::npos;
                    GlobalUnlock(data);
                }
                CloseClipboard();
            }
            const bool restored=SUCCEEDED(OleSetClipboard(originalClipboard)) && SUCCEEDED(OleFlushClipboard());
            originalClipboard->Release();
            settingsAboutUi=settingsAboutUi && copied && restored;
        } else settingsAboutUi=false;
        TabCtrl_SetCurSel(tabs,1);
        NMHDR changed{tabs,100,TCN_SELCHANGE};SendMessageW(dialog,WM_NOTIFY,100,reinterpret_cast<LPARAM>(&changed));
        SendMessageW(GetDlgItem(dialog,116),CB_SETCURSEL,2,0); // 13 logical pixels
        SendMessageW(dialog,WM_COMMAND,settingsTestSave?IDOK:IDCANCEL,0);
    }
    static int CheckPreferences(CMainWnd& window,const std::wstring& fixture) {
        int failures=0;auto check=[&](bool ok,const char* name){if(!ok){std::cerr<<"FAIL "<<name<<'\n';++failures;}};
        check(WaitQuickFixture(window),"Shell snapshot is ready before preference tests");
        window.StopQuickAccessSync();
        FastFileAbout::Info diagnostic;
        check(FastFileAbout::Directory(L"C:\\FastFile.exe")==L"C:\\"
            && FastFileAbout::Directory(L"\\\\server\\share\\FastFile.exe")==L"\\\\server\\share",
            "about installation directory preserves drive and network roots");
        check(FastFileAbout::IsOwnedAgent(L"C:\\App\\FastFileAgent.exe",L"c:\\app\\fastfileagent.exe",1,1)
            && !FastFileAbout::IsOwnedAgent(L"D:\\App\\FastFileAgent.exe",L"C:\\App\\FastFileAgent.exe",1,1)
            && !FastFileAbout::IsOwnedAgent(L"C:\\App\\FastFileAgent.exe",L"C:\\App\\FastFileAgent.exe",0,1),
            "about agent status excludes other installations and other sessions");
        diagnostic.executable=L"C:\\private-profile\\secret.exe";
        diagnostic.build=L"Release / x64";diagnostic.windows=L"10.0.22621";
        diagnostic.agentInstalled=true;diagnostic.agentPid=123;diagnostic.agentVersion=L"0.0.1";
        check(diagnostic.Diagnostics().find(L"private-profile")==std::wstring::npos
            && diagnostic.AgentText().find(L"不一致")!=std::wstring::npos,
            "about diagnostics exclude paths and report agent version mismatch");
        diagnostic.agentPid=0;diagnostic.agentUnknown=true;
        check(diagnostic.AgentText().find(L"无法确认")!=std::wstring::npos,"inaccessible agent status is not reported as stopped");
        const auto buildDirectory=FastFileAbout::Directory(FastFileAbout::ExecutablePath());
        const auto app=buildDirectory+L"\\FastFile.exe";
        check(FastFileAbout::FileVersion(app)==FASTFILE_VERSION_W
            && FastFileAbout::FileVersion(buildDirectory+L"\\FastFileAgent.exe")==FASTFILE_VERSION_W,
            "GUI and agent VERSIONINFO share the generated build version");
        HMODULE metadata=LoadLibraryExW(app.c_str(),nullptr,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        const auto license=metadata?FastFileAbout::LicenseText(metadata):L"";
        check(license.find(L"Permission is hereby granted")!=std::wstring::npos
            && license.find(L"2013 duilib")!=std::wstring::npos,"DuiLib complete MIT notice is embedded in the shipped executable");
        if(metadata)FreeLibrary(metadata);
        const auto file=fixture+L"\\preferences-test.ini";
        FastFileSettings prefs;
        check(!prefs.contextMenu && !prefs.defaultFolders && !prefs.defaultComputer && prefs.startup==1
            && prefs.tabHeight==29 && prefs.favoritesHeight==29,"settings preserve current density and leave all integration off by default");
        prefs.startup=2;prefs.startupPath=fixture;prefs.externalNewWindow=true;prefs.reuseTabs=false;prefs.confirmClose=false;
        prefs.density=2;prefs.navigationFont=14;prefs.tabHeight=36;prefs.favoritesHeight=32;prefs.tabWidthPercent=200;
        prefs.navigationScrollbar=10;prefs.defaultView=4;prefs.rememberViews=false;prefs.sortColumn=1;prefs.sortAscending=false;prefs.grouping=0;
        prefs.contextMenu=true;prefs.defaultFolders=true; // integration deliberately excluded from INI
        check(prefs.Save(file),"preferences save atomically in UTF-16");
        const auto loaded=FastFileSettings::Load(file);
        check(loaded.startup==2 && loaded.startupPath==fixture && loaded.externalNewWindow && !loaded.reuseTabs && !loaded.confirmClose
            && loaded.density==2 && loaded.navigationFont==14 && loaded.tabHeight==36 && loaded.favoritesHeight==32
            && loaded.tabWidthPercent==200 && loaded.navigationScrollbar==10 && loaded.defaultView==4 && !loaded.rememberViews
            && loaded.sortColumn==1 && !loaded.sortAscending && loaded.grouping==0,"all preference fields round trip without losing Unicode paths");
        check(!loaded.contextMenu && !loaded.defaultFolders && !loaded.defaultComputer,"INI cannot enable system integration");
        check(!prefs.Save(fixture),"saving to a directory fails without replacing existing user data");
        WritePrivateProfileStringW(L"Preferences",L"navigationFont",L"999",file.c_str());
        WritePrivateProfileStringW(L"Preferences",L"tabHeight",L"-1",file.c_str());
        const auto malformed=FastFileSettings::Load(file);
        check(malformed.navigationFont==16 && malformed.tabHeight==24,"malformed preferences are bounded to usable dimensions");
        DeleteFileW(file.c_str());
        const auto original=window.m_settings;
        window.m_settings=loaded;window.ApplySettingsAppearance();
        LOGFONTW font{};GetObjectW(window.m_PaintManager.GetFont(4),sizeof(font),&font);
        check(font.lfHeight==-window.DpiScale(14) && window.m_pTabStrip->GetFixedHeight()==window.DpiScale(36)
            && window.m_pFavoritesBar->GetFixedHeight()==window.DpiScale(32),"live settings update fonts and chrome sizes");
        check(window.m_pDirTree->GetVerticalScrollBar()->GetFixedWidth()==window.DpiScale(14),"custom scrollbar retains larger hit area");
        check(window.LoadFolderViewForPath(fixture)==CMainWnd::ViewMode::Details,"disabled view memory uses configured default");
        window.AddTab(fixture,true);const int count=int(window.m_tabs.size());window.AddTab(fixture,true);
        check(int(window.m_tabs.size())==count+1,"duplicate folder tabs are allowed when reuse is disabled");
        window.m_settings.reuseTabs=true;window.AddTab(fixture,true);
        check(int(window.m_tabs.size())==count+1,"reuse switch returns to existing folder tab");
        check(window.ConfirmCloseWithMultipleTabs(),"disabled close confirmation returns without showing a dialog");
        window.m_settings.startup=0;window.SaveSession();const int sessionCount=int(window.m_tabs.size());
        check(window.LoadSession() && int(window.m_tabs.size())==sessionCount,"restore-tabs startup preserves saved tabs");
        window.m_settings.startup=2;window.m_settings.startupPath=fixture;
        check(window.LoadSession() && window.m_tabs.size()==1 && CMainWnd::PathEquals(window.m_currentPath,fixture),"custom startup folder overrides saved tabs");
        window.m_settings.startup=1;
        check(window.LoadSession() && window.m_tabs.size()==1 && CMainWnd::IsThisPcPath(window.m_currentPath),"This PC startup remains available");
        const auto startupProbe=CMainWnd::s_folderProbe;
        const auto startupCurrent=window.m_currentPath;
        slowProbeCalls=0;
        CMainWnd::s_folderProbe=[](const std::wstring&)->DWORD {++slowProbeCalls;return INVALID_FILE_ATTRIBUTES;};
        window.m_startupOpenPaths={fixture};window.m_settings.startup=2;
        window.m_settings.startupPath=fixture+L"\\unavailable-startup";
        check(window.LoadSession() && slowProbeCalls==0 && window.m_currentPath==startupCurrent,
            "external startup restores the tab model without probing or browsing the old startup location");
        window.m_startupOpenPaths.clear();CMainWnd::s_folderProbe=startupProbe;
        window.m_settings=original;window.ApplySettingsAppearance();
        auto* gear=window.m_PaintManager.FindControl(L"btn_settings");
        check(gear && gear->IsVisible() && gear->GetFixedHeight()==window.DpiScale(32),"settings entry has a normal command-button hit area");
        const auto drive=std::wstring(fixture.substr(0,3));
        for(const auto& sample:{std::pair<std::wstring,bool>{drive,false},{drive,true},{fixture,false}}) {
            window.m_settings.defaultFolders=sample.second;
            window.NavigateToNow(sample.first==drive?std::wstring(CMainWnd::kThisPcPath):window.ParentPath(sample.first),false);
            int selected=-1;const DWORD deadline=GetTickCount()+4000;
            while(selected<0 && GetTickCount()<deadline) {
                MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                    if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
                }
                IFolderView2* native=ShellBrowserHostTestAccess::View(*window.m_shellBrowser);int n=0;
                if(native) {
                    native->ItemCount(SVGIO_ALLVIEW,&n);
                    for(int i=0;i<n;++i) {
                        IShellItem* item=nullptr;PWSTR path=nullptr;
                        if(SUCCEEDED(native->GetItem(i,IID_PPV_ARGS(&item)))) {
                            if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
                                if(CMainWnd::PathEquals(path,sample.first))selected=i;CoTaskMemFree(path);
                            }item->Release();
                        }
                    }
                    if(selected>=0)native->SelectItem(selected,SVSI_SELECT|SVSI_DESELECTOTHERS);
                    native->Release();
                }Sleep(5);
            }
            check(selected>=0 && ShellBrowserHostTestAccess::ActivateSelection(*window.m_shellBrowser)==S_OK,
                "disk and folder activation is consumed internally with system takeover either enabled or disabled");
            const DWORD settle=GetTickCount()+500;
            do {MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
            }Sleep(5);}while(GetTickCount()<settle);
            check(CMainWnd::PathEquals(window.m_currentPath,sample.first),"native disk/folder activation reaches the existing FastFile tab");
        }
        window.m_settings=original;
        // Disk / folder item menus are Windows' own: identical to the raw provider menu, with
        // no FastFile 在新选项卡中打开 and no intercepted open / opennewwindow verbs.
        for(const auto& targetPath:{drive,fixture}) {
            RawShellMenu raw,rawAfter;
            const UINT flags=CMainWnd::ShellItemMenuFlags(false,false);
            IContextMenu* built=nullptr;HMENU popup=nullptr;UINT last=0;
            if(raw.Path(targetPath,window.m_hWnd))window.m_shellBrowser->SiteContextMenu(raw.menu);
            const bool ok=raw.Query(flags) && window.BuildShellItemMenu({targetPath},&built,&popup,&last);
            if(rawAfter.Path(targetPath,window.m_hWnd))window.m_shellBrowser->SiteContextMenu(rawAfter.menu);
            rawAfter.Query(flags);
            // Some Windows handlers hand out different command ids on successive queries in a
            // fresh process. Exact identity with a raw query before or after FastFile's is
            // required; when the provider itself is not repeatable, the items (text, type,
            // state, cascades) must still be identical to both raw queries.
            std::string why,whyAfter,whyItems;
            const bool exact=ok && ((last==raw.last && SameShellMenu(popup,raw.popup,&why))
                || (last==rawAfter.last && SameShellMenu(popup,rawAfter.popup,&whyAfter)));
            const bool providerVaries=!SameShellMenu(raw.popup,rawAfter.popup);
            const bool sameItems=ok && providerVaries && SameShellMenu(popup,raw.popup,&whyItems,false)
                && SameShellMenu(popup,rawAfter.popup,nullptr,false);
            if(!exact && sameItems)std::cerr<<"  info: Windows' own disk/folder menu ids vary between queries; items compared\n";
            check(exact || sameItems,"real Windows disk/folder menu is the unmodified provider menu (no FastFile item added or removed)");
            if(!exact && !sameItems)std::cerr<<"  disk/folder menu diagnostic: "<<why<<" | after: "<<whyAfter<<" | items: "<<whyItems<<'\n';
            if(popup)DestroyMenu(popup);if(built)built->Release();
        }
        // Double-click / Enter on a folder in the hosted view (ICommDlgBrowser::OnDefaultCommand)
        // still opens it inside FastFile; with several folders the extra ones open as tabs.
        auto folderOpen=[&](const std::wstring& path,bool newTab) {
            PostMessageW(window.m_hWnd,CMainWnd::kMsgShellFolderOpen,newTab?1:0,reinterpret_cast<LPARAM>(new std::wstring(path)));
            MSG request{};
            if(PeekMessageW(&request,window.m_hWnd,CMainWnd::kMsgShellFolderOpen,CMainWnd::kMsgShellFolderOpen,PM_REMOVE))
                window.HandleMessage(request.message,request.wParam,request.lParam);
        };
        folderOpen(drive,false);
        check(CMainWnd::PathEquals(window.m_currentPath,drive),"view folder activation navigates the FastFile tab");
        for(const auto& sample:{fixture,fixture+L"\\Program Files",drive}) {
            folderOpen(sample,true);
            const DWORD deadline=GetTickCount()+200;
            do {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
            }Sleep(5);}while(GetTickCount()<deadline);
            check(CMainWnd::PathEquals(window.m_currentPath,sample),
                "multi-folder activation opens each folder inside FastFile (tab reuse rules apply)");
        }
        // Appearance-only dialog checks must never rewrite the real user's Shell
        // commands to this test executable (enabled state is deliberately reapplied).
        FastFileSettings realIntegration;CMainWnd::ReadSystemIntegration(realIntegration);
        const auto settingsHive=L"Software\\FastFileSettingsDialogRegression_"+std::to_wstring(GetCurrentProcessId());
        HKEY dialogUser=nullptr;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,settingsHive.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&dialogUser,nullptr)!=ERROR_SUCCESS
            || RegOverridePredefKey(HKEY_CURRENT_USER,dialogUser)!=ERROR_SUCCESS) {
            check(false,"settings dialog has an isolated registry");
            if(dialogUser)RegCloseKey(dialogUser);
            return failures;
        }
        FastFileSettings testIntegration;testIntegration.contextMenu=true;testIntegration.defaultFolders=true;testIntegration.defaultComputer=true;
        check(CMainWnd::ApplySystemIntegration(testIntegration),"settings dialog starts with isolated integration enabled");
        check(window.ApplyDefaultManagerChoice(CMainWnd::DefaultManagerChoice::KeepCurrent),
            "keeping the current manager succeeds without applying staged preferences");
        FastFileSettings kept;CMainWnd::ReadSystemIntegration(kept);
        check(kept.contextMenu && kept.defaultFolders && kept.defaultComputer && !kept.explorerWindowTakeover,
            "keeping the current manager preserves independent integration flags");
        check(window.ApplyDefaultManagerChoice(CMainWnd::DefaultManagerChoice::UseFastFile),
            "the default manager choice applies unified integration");
        FastFileSettings chosen;CMainWnd::ReadSystemIntegration(chosen);
        check(chosen.contextMenu && chosen.defaultFolders && chosen.defaultComputer && chosen.explorerWindowTakeover
            && CMainWnd::DetectSystemIntegration().backgroundReady,
            "choosing FastFile enables all entry points and owned login startup");
        check(window.ApplyDefaultManagerChoice(CMainWnd::DefaultManagerChoice::RestorePrevious),
            "the restore choice restores the previous manager");
        FastFileSettings restored;CMainWnd::ReadSystemIntegration(restored);
        check(!restored.contextMenu && !restored.defaultFolders && !restored.defaultComputer && !restored.explorerWindowTakeover
            && !CMainWnd::DetectSystemIntegration().backgroundReady,
            "restoring the previous manager removes unified integration and login startup");
        check(CMainWnd::ApplySystemIntegration(testIntegration),"restore isolated integration before dialog checks");
        settingsTestOwner=window.m_hWnd;settingsTestSave=false;settingsDialogPages=false;
        auto settingsTimer=SetTimer(nullptr,0,100,ExerciseSettingsDialog);window.ShowSettings();KillTimer(nullptr,settingsTimer);
        if(!settingsDialogPages || window.m_settings.navigationFont!=original.navigationFont)
            std::cerr<<"settings cancel diagnostic pages="<<settingsDialogPages<<" font="<<window.m_settings.navigationFont<<" expected="<<original.navigationFont<<'\n';
        check(settingsIntegrationUi,"system integration exposes only one default FastFile switch with no repair controls");
        check(settingsAboutUi,"about settings page exposes version, readonly path, bounded actions and privacy-preserving clipboard");
        check(settingsDialogPages && window.m_settings.navigationFont==original.navigationFont,
            "all four real settings pages open and cancel discards staged changes");
        settingsTestSave=true;settingsDialogPages=false;
        settingsTimer=SetTimer(nullptr,0,100,ExerciseSettingsDialog);window.ShowSettings();KillTimer(nullptr,settingsTimer);
        check(settingsDialogPages && window.m_settings.navigationFont==13 && FastFileSettings::Load(FastFileSettings::FilePath()).navigationFont==13,
            "real settings save applies appearance and persists the new value");
        FastFileSettings unified;CMainWnd::ReadSystemIntegration(unified);
        check(unified.contextMenu && unified.defaultFolders && unified.defaultComputer && unified.explorerWindowTakeover,
            "single default switch saves folder Computer menu and direct Explorer handling together");
        wchar_t run[32768]{};DWORD runBytes=sizeof(run);
        check(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"FastFile.DefaultManager",
            RRF_RT_REG_SZ,nullptr,run,&runBytes)==ERROR_SUCCESS && std::wstring(run)==L"\""+CMainWnd::ExplorerAgentPath()+L"\"",
            "default integration registers independent agent at login");
        std::vector<std::wstring> decoded;
        auto payload=ExplorerAgentProtocol::Encode({fixture+L"\\Program Files",fixture+L"\\Program Files\\sample.txt"});
        COPYDATASTRUCT message{ExplorerAgentProtocol::Confirm,static_cast<DWORD>(payload.size()*sizeof(wchar_t)),payload.data()};
        check(ExplorerAgentProtocol::Decode(message,decoded) && decoded.size()==2,"agent protocol preserves folder and selection");
        check(ExplorerAgentProtocol::Encode(std::vector<std::wstring>(258,L"C")).empty(),"agent payload rejects excess selections");
        check(ExplorerAgentProtocol::Encode({std::wstring(ExplorerAgentProtocol::MaxChars,L'x')}).empty(),"agent payload enforces total size limit");
        message.dwData=0;check(!ExplorerAgentProtocol::Decode(message,decoded),"unknown agent operation is rejected");
        message.dwData=ExplorerAgentProtocol::Confirm;
        message.cbData--;check(!ExplorerAgentProtocol::Decode(message,decoded),"odd-byte UTF-16 payload is rejected");message.cbData++;
        message.cbData-=sizeof(wchar_t);
        check(!ExplorerAgentProtocol::Decode(message,decoded),"truncated agent protocol is rejected");
        message.cbData=static_cast<DWORD>(payload.size()*sizeof(wchar_t));
        const auto oldPath=window.m_currentPath;
        const bool wasVisible=IsWindowVisible(window.m_hWnd)!=FALSE;
        ShowWindow(window.m_hWnd,SW_HIDE);
        check(window.HandleExplorerAgentMessage(message)==0,"hidden or wrong-folder view cannot confirm handoff");
        message.dwData=ExplorerAgentProtocol::Open;
        check(window.HandleExplorerAgentMessage(message)==1,"agent open request is accepted asynchronously");
        message.dwData=ExplorerAgentProtocol::Confirm;
        window.m_externalOpensPending++;
        check(window.HandleExplorerAgentMessage(message)==0,"pending external navigation cannot confirm handoff");
        window.m_externalOpensPending--;
        const auto readyDeadline=GetTickCount64()+8000;
        bool confirmed=false;
        do {
            MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessageW(&msg);}
            confirmed=window.HandleExplorerAgentMessage(message)==1;if(confirmed)break;Sleep(10);
        }while(GetTickCount64()<readyDeadline);
        if(!confirmed)std::wcerr<<L"agent confirm current="<<window.m_currentPath<<L" target="<<payload.data()
            <<L" pending="<<window.m_externalOpensPending<<L" visible="<<IsWindowVisible(window.m_hWnd)
            <<L" hostVisible="<<(window.m_shellBrowser && window.m_shellBrowser->IsVisible())
            <<L" hostRect="<<(window.m_pListHost?window.m_pListHost->GetWidth():-1)<<L","<<(window.m_pListHost?window.m_pListHost->GetHeight():-1)
            <<L" sized="<<(window.m_shellBrowser && window.m_shellBrowser->HasVisibleViewBounds())
            <<L" file="<<GetFileAttributesW((fixture+L"\\Program Files\\sample.txt").c_str())
            <<L" complete="<<(window.m_shellBrowser && window.m_shellBrowser->IsNavigationCompleteAt(fixture+L"\\Program Files"))<<L"\n";
        if(!confirmed && window.m_shellBrowser)ShellBrowserHostTestAccess::DumpVisibleView(*window.m_shellBrowser);
        check(confirmed,"agent confirmation requires visible loaded folder and real file selection");
        if(!wasVisible)ShowWindow(window.m_hWnd,SW_HIDE);
        CharUpperBuffW(run,static_cast<DWORD>(wcslen(run)));
        HKEY startupKey=nullptr;
        if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&startupKey)==ERROR_SUCCESS) {
            RegSetValueExW(startupKey,L"FastFile.DefaultManager",0,REG_SZ,reinterpret_cast<const BYTE*>(run),static_cast<DWORD>((wcslen(run)+1)*sizeof(wchar_t)));
            RegCloseKey(startupKey);
        }
        check(CMainWnd::DetectSystemIntegration().backgroundReady,"agent login ownership ignores Windows path casing");
        // The test keeps its own window alive; the cold activation suite verifies exit.
        FastFileSettings disabled;
        check(CMainWnd::ApplySystemIntegration(disabled),"disabling default integration restores associations");
        runBytes=sizeof(run);
        check(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"FastFile.DefaultManager",
            RRF_RT_REG_SZ,nullptr,run,&runBytes)==ERROR_FILE_NOT_FOUND,"disabling removes owned login startup");
        window.m_settings.explorerWindowTakeover=false;window.UpdateExplorerTakeover();
        check(!window.m_explorerScan,"applying disabled preferences stops the test scanner");
        window.m_explorerAgentAllowed=false;
        RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);RegCloseKey(dialogUser);
        RegDeleteTreeW(HKEY_CURRENT_USER,settingsHive.c_str());
        FastFileSettings afterIntegration;CMainWnd::ReadSystemIntegration(afterIntegration);
        check(afterIntegration.contextMenu==realIntegration.contextMenu
            && afterIntegration.defaultFolders==realIntegration.defaultFolders
            && afterIntegration.defaultComputer==realIntegration.defaultComputer
            && afterIntegration.explorerWindowTakeover==realIntegration.explorerWindowTakeover,
            "settings dialog regression preserves real integration flags");
        window.m_settings=original;window.ApplySettingsAppearance();
        return failures;
    }
    static int CheckUiPolish(CMainWnd& window, const std::wstring& root) {
        int failures=0;
        auto check=[&](bool ok,const char* name) { if (!ok) {std::cerr<<"FAIL "<<name<<'\n';++failures;} };
        auto pump=[&]() {
            const DWORD until=GetTickCount()+700;
            do { MSG message; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message!=WM_QUIT) {TranslateMessage(&message);DispatchMessageW(&message);}
            } Sleep(5); } while(GetTickCount()<until);
            window.m_PaintManager.GetRoot()->SetPos({0,0,1180,740},false); window.SyncLayoutDependents();
        };
        const auto pictures=root+L"\\UI 图片",girls=pictures+L"\\GIRLS",folder=girls+L"\\刘诗诗";
        for(const auto& path : {pictures,girls,folder}) CreateDirectoryW(path.c_str(),nullptr);
        CLSID encoder{}; check(window.GetPngEncoderClsid(&encoder),"fixture PNG encoder available");
        for(const auto& sample : {std::pair<const wchar_t*,SIZE>{L"1 (583).PnG",{120,240}},
             {L"横图.png",{240,120}},{L"方图.png",{120,120}}}) {
            Gdiplus::Bitmap bitmap(sample.second.cx,sample.second.cy,PixelFormat32bppARGB);
            Gdiplus::Graphics graphics(&bitmap);graphics.Clear(Gdiplus::Color(255,40,100,180));
            check(bitmap.Save((folder+L"\\"+sample.first).c_str(),&encoder,nullptr)==Gdiplus::Ok,"write portrait/landscape/square fixtures");
        }
        window.NavigateToNow(folder,false);window.SetViewMode(CMainWnd::ViewMode::LargeIcons);
        SetWindowPos(window.m_hWnd,nullptr,0,0,1180,740,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        ShowWindow(window.m_hWnd,SW_SHOWNOACTIVATE);pump();
        for(auto mode : {CMainWnd::ViewMode::LargeIcons,CMainWnd::ViewMode::ExtraLargeIcons}) {
            window.SetViewMode(mode);pump();
            for(const auto& sample : {std::pair<const wchar_t*,SIZE>{L"1 (583).PnG",{120,240}},
                {L"横图.png",{240,120}},{L"方图.png",{120,120}}}) {
                check(ShellBrowserHostTestAccess::MediaAspectRatio(*window.m_shellBrowser,
                    folder+L"\\"+sample.first,sample.second.cx,sample.second.cy),
                    "large/extra-large Shell thumbnails preserve media aspect ratio and center in fixed slots");
            }
        }
        window.SetViewMode(CMainWnd::ViewMode::LargeIcons);pump();
        check(DuiLib::TabStripRegressionAccess::RoundedIcon(folder+L"\\方图.png"),
            "photo tab icon is clipped to a 2 logical pixel rounded shape at 150 percent DPI");
        auto text=[&](const wchar_t* name) {return std::wstring(window.m_PaintManager.FindControl(name)->GetText().GetData());};
        check(text(L"preview_size")==L"0 个文件夹 · 3 个文件","unselected image folder reports its actual children");
        auto* node=window.FindTreeNodeByPath(nullptr,folder);
        check(node && node->IsSelected() && node->IsVisible(),"image folder navigation expands GIRLS and selects nested Chinese leaf");
        if(node) {
            const RECT bounds=window.m_pDirTree->GetList()->GetPos(),item=node->GetPos();
            check(item.top>=bounds.top && item.bottom<=bounds.bottom,"nested image folder is fully revealed in tree viewport");
        }
        IFolderView2* view=ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
        int count=0,index=-1;view->ItemCount(SVGIO_ALLVIEW,&count);
        for(int i=0;i<count;++i) {
            IShellItem* item=nullptr;
            if(SUCCEEDED(view->GetItem(i,IID_PPV_ARGS(&item)))) {
                PWSTR path=nullptr;
                if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
                    if(std::wstring(path)==folder+L"\\1 (583).PnG") index=i;
                    CoTaskMemFree(path);
                } item->Release();
            }
        }
        check(index>=0,"Shell enumerates mixed-case portrait filename");
        if(index>=0) {
            check(ShellBrowserHostTestAccess::SkipsGroupThumbnail(*window.m_shellBrowser,index),
                "Shell group headers must not be treated as file thumbnails");
            check(ShellBrowserHostTestAccess::SkipsClippedThumbnail(*window.m_shellBrowser,index),
                "Shell custom draw must not fetch thumbnails outside a collapsed viewport");
            view->SelectItem(index,SVSI_SELECT|SVSI_DESELECTOTHERS);pump();
            check(ShellBrowserHostTestAccess::ContainsThumbnail(*window.m_shellBrowser,index),
                "large native cells contain real Shell image thumbnails rather than small file icons");
            check(ShellBrowserHostTestAccess::ShellMediaThumbnail(*window.m_shellBrowser,folder+L"\\1 (583).PnG"),
                "media cells render genuine Shell thumbnail content with association icons independently");
            check(window.m_previewPath==folder+L"\\1 (583).PnG" && text(L"preview_dimensions")==L"120 x 240",
                "selected native thumbnail and detail preview bind to identical file and retain resolution");
            check(window.m_previewBmp.find(L"preview_shell_")!=std::wstring::npos,
                "image detail preview prefers Windows Shell thumbnail instead of decoding the original itself");
            window.m_pDirTree->SetFocus(); SetFocus(window.m_hWnd);
            window.UpdatePreviewForSelection();
            check(window.m_previewPath==folder+L"\\1 (583).PnG",
                "tree scrollbar focus must not replace the selected Shell file preview with its folder");
            window.FocusFileView();
            check(window.m_pPreviewLocation->GetTextStyle() & DT_PATH_ELLIPSIS,"preview path preserves suffix through middle ellipsis");
            const RECT panel=window.m_pPreviewPane->GetPos(),image=window.m_pPreviewImage->GetPos(),title=window.m_pPreviewTitle->GetPos();
            check(image.left-panel.left==window.DpiScale(16) && panel.right-image.right==window.DpiScale(16)
                && image.top-panel.top==window.DpiScale(16),"preview has exactly 16 logical pixels at left right and top");
            check(title.top-image.bottom==window.DpiScale(12),"preview-title gap is 12 logical pixels");
            check(static_cast<CLabelUI*>(window.m_PaintManager.FindControl(L"preview_lbl_type"))->GetTextColor()==0xFF616161,
                "metadata labels use requested gray");
            Gdiplus::Bitmap rounded(window.m_previewBmp.c_str());
            int left=int(rounded.GetWidth()),top=int(rounded.GetHeight()),right=-1,bottom=-1;
            for(int y=0;y<int(rounded.GetHeight());++y) for(int x=0;x<int(rounded.GetWidth());++x) {
                Gdiplus::Color pixel;rounded.GetPixel(x,y,&pixel);
                if(pixel.GetAlpha()) {left=(std::min)(left,x);right=(std::max)(right,x);top=(std::min)(top,y);bottom=(std::max)(bottom,y);}
            }
            Gdiplus::Color corner,center;rounded.GetPixel(left,top,&corner);rounded.GetPixel((left+right)/2,(top+bottom)/2,&center);
            check(corner.GetAlpha()==0 && center.GetAlpha()==255,"portrait content corners are rounded within transparent contain margins");
            for(const wchar_t* name:{L"btn_cut",L"btn_copy",L"btn_rename",L"btn_share",L"btn_delete"})
                check(window.m_PaintManager.FindControl(name)->IsEnabled(),"selected portrait enables file commands");
            window.ClearFileSelection();pump();
            for(const wchar_t* name:{L"btn_cut",L"btn_copy",L"btn_rename",L"btn_share",L"btn_delete"})
                check(!window.m_PaintManager.FindControl(name)->IsEnabled(),"deselected portrait disables file commands");
        }
        view->Release();
        auto* treeBar=window.m_pDirTree->GetVerticalScrollBar();
        check(treeBar && treeBar->GetFixedWidth()>=window.DpiScale(12),"navigation scrollbar reserves the whole 12 logical pixel drag target");
        if(treeBar) {
            treeBar->SetVisible(true);
            RECT rc=window.m_pDirTree->GetPos();
            rc.left=rc.right-treeBar->GetFixedWidth(); rc.top+=10;rc.bottom=rc.top+100;
            treeBar->SetScrollRange(1000);treeBar->SetPos(rc,false);
            const POINT pt={rc.left+1,rc.top+2};
            check(window.m_PaintManager.FindControl(pt)==treeBar,"inner edge of navigation rail belongs to scrollbar instead of a tree row");
            window.HandleMessage(WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(pt.x,pt.y));
            check(!window.m_dragTracking && window.m_paneDragKind==0,"scrollbar press cannot arm file drag or pane resize");
            window.HandleMessage(WM_LBUTTONUP,0,MAKELPARAM(pt.x,pt.y));
            window.HandleMessage(WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(pt.x,pt.y));
            window.HandleMessage(WM_CAPTURECHANGED,0,0);
            auto* fluent=dynamic_cast<CFluentScrollBarUI*>(treeBar);
            check(fluent && !fluent->IsDragging() && !window.m_PaintManager.IsCaptured(),
                "lost native capture clears both scrollbar and manager logical capture");
            const int released=treeBar->GetScrollPos();
            window.HandleMessage(WM_MOUSEMOVE,0,MAKELPARAM(pt.x,pt.y+40));
            check(treeBar->GetScrollPos()==released,"returning to navigation scrollbar without a press only hovers");
        }
        ScrollBarDragFixture drag;
        drag.SetManager(&window.m_PaintManager,nullptr,false);
        drag.SetShowButton1(false);drag.SetShowButton2(false);
        drag.SetRailMetrics(window.DpiScale(8),window.DpiScale(12));
        drag.SetFixedWidth(window.DpiScale(12));drag.SetScrollRange(1000);
        drag.SetPos({0,0,window.DpiScale(12),300},false);drag.SetExpanded(true);
        TEventUI event{};event.Type=UIEVENT_BUTTONDOWN;event.ptMouse=drag.ThumbPoint();event.wKeyState=MK_LBUTTON;drag.DoEvent(event);
        check(drag.Captured(),"wide thumb captures a press at its inner edge");
        drag.SetExpanded(false);
        check(drag.IsExpanded(),"captured thumb stays wide even when pointer leaves the rail");
        event.Type=UIEVENT_MOUSEMOVE;event.ptMouse={-50,100};drag.DoEvent(event);
        check(drag.GetScrollPos()>0,"even a fast drag updates scroll before the 50ms repeat timer");
        event.Type=UIEVENT_TIMER;event.wParam=drag.TimerId();drag.DoEvent(event);
        check(drag.GetScrollPos()>0 && drag.Captured(),"drag continues to scroll while pointer is outside the rail");
        event.Type=UIEVENT_BUTTONUP;drag.DoEvent(event);
        check(!drag.Captured(),"releasing the thumb ends scrollbar capture");
        event.Type=UIEVENT_BUTTONDOWN;event.ptMouse=drag.ThumbPoint();event.wKeyState=MK_LBUTTON;drag.DoEvent(event);
        const int stopped=drag.GetScrollPos();
        event.Type=UIEVENT_MOUSEMOVE;event.ptMouse.y+=80;event.wKeyState=0;drag.DoEvent(event);
        check(!drag.Captured() && drag.GetScrollPos()==stopped,"missing mouse release cannot turn later hover movement into a drag");
        event.Type=UIEVENT_TIMER;event.wParam=drag.TimerId();drag.DoEvent(event);
        check(drag.GetScrollPos()==stopped,"stale repeat timer cannot scroll after button was released");
        event.Type=UIEVENT_BUTTONDOWN;event.ptMouse=drag.ThumbPoint();event.wKeyState=MK_LBUTTON;drag.DoEvent(event);
        event.Type=UIEVENT_KILLFOCUS;drag.DoEvent(event);
        check(!drag.Captured(),"switching focus to a native file view cancels the old scrollbar gesture");
        return failures;
    }
    // Cmd-bar alignment pass: command bar + address row follow the Win11 Explorer metrics
    // measured at 150% (logical px). Layout is checked at 150% DPI on a maximized-like
    // 1707-logical (2560 physical) row, and the painted pixels are sampled from an offscreen
    // render of the top area. FASTFILE_TOP_SHOT=<png> also saves that render.
    static int CheckCommandBarAlignment(CMainWnd& window) {
        int failures=0;
        auto check=[&](bool ok,const char* name) { if(!ok) {std::cerr<<"FAIL "<<name<<'\n';++failures;} };
        auto within=[](int a,int b,int tol) { return a>=b-tol && a<=b+tol; };
        auto ctl=[&](const wchar_t* name) { return window.m_PaintManager.FindControl(name); };
        // XML / tokens / code consistency: the design values in main.xml are the tokens.
        {
            wchar_t module[MAX_PATH]{}; GetModuleFileNameW(nullptr,module,MAX_PATH);
            PathRemoveFileSpecW(module);
            std::ifstream xml((std::wstring(module)+L"\\skin\\main.xml").c_str(),std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(xml)),std::istreambuf_iterator<char>());
            auto has=[&](const char* fragment) { return text.find(fragment)!=std::string::npos; };
            check(!text.empty(),"skin main.xml is readable next to the test executable");
            check(has("name=\"search_box\" width=\"240\" height=\"32\"") && UiTokens::SearchBoxW==240
                && UiTokens::SearchBoxMinW==240 && UiTokens::SearchBoxMaxW==435 && UiTokens::SearchBoxRowPct==30,
                "search box design width/height agree between main.xml and UiTokens");
            check(has("name=\"path_host\" height=\"32\" bkcolor=\"#FFFCFCFB\" bordercolor=\"#FFFCFCFB\" bordersize=\"1\" borderround=\"4,4\"")
                && UiTokens::FieldH==32 && UiTokens::FieldRound==4,"address box xml uses 32px height, radius 4 and borderless FCFCFB fill");
            check(has("name=\"toolbar\" height=\"46\"") && has("name=\"command_top_divider\" height=\"1\" bkcolor=\"#FFE0E0E0\"")
                && has("name=\"command_body_divider\" height=\"1\" bkcolor=\"#FFE0E0E0\"") && UiTokens::ToolbarH==48,
                "command bar xml = 1 + 46 + 1 = ToolbarH 48 with E0E0E0 hairlines");
            check(has("name=\"sep_new\" width=\"1\" height=\"32\"") && has("bkcolor=\"#FFF0F0F0\"") && UiTokens::SepH==32,
                "separators are 1x32 F0F0F0 in xml and tokens");
            check(has("name=\"btn_cut\" text=\"\" width=\"40\" height=\"32\"") && has("name=\"btn_back\" text=\"\" width=\"40\" height=\"32\"")
                && has("text=\"新建\" width=\"84\"") && text.find("⌄")==std::string::npos,
                "xml buttons use the 40/84 widths and no text chevrons");
        }
        for(int row:{2560,1770,1180,700})
            check(UiTokens::SearchBoxWidthFor(row,360,653)==(std::min)(653,(std::max)(360,row*30/100)),
                "search width is clamp(min, 30 percent of row, max)");

        const UINT initialDpi=window.m_dpi;
        window.m_dpi=144;
        window.ApplyDpiScaledFonts();window.ApplyDpiScaledChrome();window.ApplyUiChromeTokens();
        window.ApplyChromeShellIcons();window.UpdateCommandBarState();window.UpdateNavButtons();
        window.RebuildFavoritesBar();
        const int W=2560,H=1100;
        window.UpdateSearchBoxWidth(W);
        window.m_PaintManager.GetRoot()->SetPos({0,0,W,H},false);
        window.RebuildBreadcrumb();
        window.m_PaintManager.GetRoot()->SetPos({0,0,W,H},false);
        auto pos=[&](const wchar_t* name) { CControlUI* c=ctl(name); return c ? c->GetPos() : RECT{}; };
        const RECT top=pos(L"command_top_divider"),bar=pos(L"toolbar"),body=pos(L"command_body_divider");
        check(body.bottom-top.top==72 && top.bottom-top.top==2 && body.bottom-body.top==2 && bar.top==top.bottom && body.top==bar.bottom,
            "command bar is 48 logical at 150 percent including a hairline above and below");
        const RECT newBtn=pos(L"btn_new"),cut=pos(L"btn_cut"),del=pos(L"btn_delete"),sep=pos(L"sep_new"),sepOrg=pos(L"sep_organize");
        check(newBtn.bottom-newBtn.top==48 && cut.bottom-cut.top==48 && cut.right-cut.left==60 && newBtn.right-newBtn.left==126,
            "command buttons are 32 high, icon-only 40 wide and label buttons 84 wide");
        check(within(newBtn.left+window.DpiScale(12),27,1),"first command icon sits 18 logical from the window edge");
        const wchar_t* icons[]={L"btn_cut",L"btn_copy",L"btn_paste",L"btn_rename",L"btn_share",L"btn_delete"};
        bool pitch=true;
        for(int i=1;i<6;++i) pitch=pitch && pos(icons[i]).left-pos(icons[i-1]).left==72;
        check(pitch,"icon-only commands are 48 logical apart center to center");
        check(sep.right-sep.left==2 && sep.bottom-sep.top==48 && ctl(L"sep_new")->GetBkColor()==0xFFF0F0F0,
            "group separators are 1x32 logical and very light");
        const int chevronInk=newBtn.right-window.DpiScale(12),cutInk=cut.left+(60-24)/2;
        check(within(sep.left-chevronInk,27,2) && within(cutInk-sep.right,27,2) && within(sepOrg.left-(del.left+(60-24)/2+24),27,2),
            "separators sit 18 logical from the neighbouring ink");
        const RECT settings=pos(L"btn_settings");
        check(settings.right==W-window.DpiScale(6) && settings.right-settings.left==60,"settings gear stays at the right end of the bar");
        const RECT row=pos(L"address_bar"),back=pos(L"btn_back"),fwd=pos(L"btn_forward"),up=pos(L"btn_up"),refresh=pos(L"btn_refresh");
        check(row.bottom-row.top==72 && row.bottom==top.top,"address row is 48 logical directly above the command bar");
        check(within(back.left+(back.right-back.left-18)/2,32,1) && fwd.left-back.left==72 && up.left-fwd.left==72 && refresh.left-up.left==72,
            "nav glyphs start 21 logical from the edge on a 48 logical pitch");
        const RECT path=pos(L"path_host"),search=pos(L"search_box"),glyph=pos(L"search_glyph");
        check(path.bottom-path.top==48 && search.bottom-search.top==48 && path.top-row.top==12
            && ctl(L"path_host")->GetBorderRound().cx==6 && ctl(L"search_box")->GetBorderRound().cx==6,
            "address and search boxes are 32 logical high with radius 4");
        check(search.right-search.left==window.DpiScale(435) && search.left-path.right==12,
            "maximized search box is 435 logical wide and 8 logical after the address box");
        check(glyph.right-glyph.left==window.DpiScale(11) && search.right-glyph.right==window.DpiScale(13),
            "search glyph is 11 logical and 13 logical from the right edge");
        check(path.left-refresh.right==window.DpiScale(10),"address box starts 10 logical after Refresh like Explorer");

        // The colour check below needs a disabled copy command; the selection state depends on
        // what earlier checks did, so force it (UpdateCommandBarState restores it afterwards).
        ctl(L"btn_copy")->SetEnabled(false);window.ApplyCommandIcon(ctl(L"btn_copy"),2,false);
        // Paint the top area offscreen and sample real pixels.
        const int shotH=body.bottom+window.DpiScale(8);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=W;info.bmiHeader.biHeight=-shotH;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        HBITMAP dib=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,dib);
        RECT canvas{0,0,W,shotH};FillRect(dc,&canvas,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        window.m_PaintManager.GetRoot()->Paint(dc,canvas,nullptr);GdiFlush();
        auto px=[&](int x,int y)->DWORD { if(x<0||y<0||x>=W||y>=shotH) return 0; return static_cast<const DWORD*>(bits)[size_t(y)*W+x]&0xFFFFFF; };
        auto anyIn=[&](RECT r,DWORD rgb,int tol) {
            for(int y=r.top;y<r.bottom;++y) for(int x=r.left;x<r.right;++x) {
                const DWORD p=px(x,y);
                if(within(int(p>>16&255),int(rgb>>16&255),tol)&&within(int(p>>8&255),int(rgb>>8&255),tol)&&within(int(p&255),int(rgb&255),tol)) return true;
            } return false; };
        check(px(W/2,top.top)==0xE0E0E0 && px(W/2,top.bottom-1)==0xE0E0E0 && px(W/2,body.top)==0xE0E0E0,
            "command bar hairlines paint E0E0E0 above and below");
        check(px((sep.left+sep.right)/2,(sep.top+sep.bottom)/2)==0xF0F0F0 && px(sep.right+3,(sep.top+sep.bottom)/2)==0xFFFFFF,
            "separator paints F0F0F0 on the white bar");
        check(px(path.right-4,(path.top+path.bottom)/2)==0xFCFCFB && px(path.right-window.DpiScale(16),path.top)==0xFCFCFB,
            "address box fill is FCFCFB with no visible idle border");
        check(!ctl(L"btn_copy")->IsEnabled() && anyIn(pos(L"btn_copy"),0xC2C2C2,3) && anyIn(pos(L"btn_copy"),0xA3CEEF,3),
            "disabled copy shows grey C2C2C2 and blue A3CEEF at 36 percent");
        const RECT more=pos(L"btn_more");
        check(anyIn(more,0x1B1B1B,2),"more dots are 1B1B1B");
        auto imageFile=[&](const wchar_t* name) {
            std::wstring attr=static_cast<CButtonUI*>(ctl(name))->GetForeImage();
            const size_t a=attr.find(L"file='"),b=a==std::wstring::npos ? a : attr.find(L'\'',a+6);
            return a==std::wstring::npos||b==std::wstring::npos ? std::wstring() : attr.substr(a+6,b-a-6);
        };
        {
            // Label button bitmap: icon at 12, chevron = small E70D, 777777, 12 from the right.
            Gdiplus::Bitmap canvasBmp(imageFile(L"btn_new").c_str());
            int left=10000,right=-1,topY=10000,bottomY=-1;bool grey77=false;
            const int bw=int(canvasBmp.GetWidth()),zone=bw-window.DpiScale(12)-window.DpiScale(8);
            for(int y=0;y<int(canvasBmp.GetHeight());++y) for(int x=zone;x<bw;++x) {
                Gdiplus::Color c;canvasBmp.GetPixel(x,y,&c);
                if(c.GetAlpha()<24) continue;
                left=(std::min)(left,x);right=(std::max)(right,x);topY=(std::min)(topY,y);bottomY=(std::max)(bottomY,y);
                grey77=grey77||(c.GetR()==0x77&&c.GetG()==0x77&&c.GetB()==0x77);
            }
            check(bw==126 && grey77 && right>=0 && right-left+1<=window.DpiScale(6)+1 && bottomY-topY+1<=window.DpiScale(4)+1
                && within(bw-1-right,window.DpiScale(12),2),
                "label chevron is a small 5.3x3.3 E70D glyph in 777777 at the 12px right padding");
        }
        auto glyphColor=[&](const wchar_t* name,wchar_t glyph,COLORREF color) {
            const std::wstring expected=window.GetGlyphIconBmp(glyph,window.DpiScale(UiTokens::NavGlyphPx),color);
            return !expected.empty() && imageFile(name)==expected;
        };
        check(glyphColor(L"btn_back",UiTokens::GlyphNavBack,RGB(0xA2,0xA2,0xA0))
            && glyphColor(L"btn_refresh",UiTokens::GlyphNavRefresh,RGB(0x1A,0x1A,0x1A))
            && glyphColor(L"btn_up",UiTokens::GlyphNavUp,RGB(0x1A,0x1A,0x1A)),
            "nav glyphs are Segoe Fluent E72B/E72A/E74A/E72C at 12px in 1A1A1A, disabled A2A2A0");
        check(anyIn(back,0xC8C8C6,30) && anyIn(refresh,0x404040,40),"nav glyphs actually paint");
        wchar_t shot[MAX_PATH]{};
        if(GetEnvironmentVariableW(L"FASTFILE_TOP_SHOT",shot,MAX_PATH)) {
            std::vector<DWORD> argb(size_t(W)*shotH);
            for(size_t i=0;i<argb.size();++i) argb[i]=0xFF000000u|(static_cast<const DWORD*>(bits)[i]&0xFFFFFF);
            check(CMainWnd::SaveArgbPng(argb,W,shotH,shot),"top-area screenshot saved");
        }
        SelectObject(dc,old);DeleteObject(dib);DeleteDC(dc);ReleaseDC(nullptr,screen);
        window.UpdateCommandBarState();

        // Icon bitmaps: grey #555555 + accent #0078D4, hollow accent, disabled at 36%.
        for(bool dim:{false,true}) {
            Gdiplus::Bitmap icon(window.GetCommandIconBmp(2,24,dim).c_str());
            int grey=0,blue=0,maxAlpha=0;
            for(UINT y=0;y<icon.GetHeight();++y) for(UINT x=0;x<icon.GetWidth();++x) {
                Gdiplus::Color c;icon.GetPixel(x,y,&c);maxAlpha=(std::max)(maxAlpha,int(c.GetAlpha()));
                if(c.GetAlpha()<(dim?80:230)) continue;
                if(c.GetR()==0x55&&c.GetG()==0x55&&c.GetB()==0x55) ++grey;
                if(c.GetR()==0x00&&c.GetG()==0x78&&c.GetB()==0xD4) ++blue;
            }
            check(grey>0 && blue>0,"copy icon has a 555555 grey layer and a 0078D4 accent layer");
            check(maxAlpha==(dim?int(UiTokens::CmdDisabledAlpha):255),"disabled command icon is the whole icon at 36 percent");
            Gdiplus::Color inside;icon.GetPixel(UINT(24*10.5/16),UINT(24*6.5/16),&inside);
            check(inside.GetAlpha()==0,"copy accent sheet is hollow, not a tinted fill");
        }
        {
            // Stroke weight: a vertical stroke of the delete glyph at 24 px is ~1.5 px (1 logical).
            Gdiplus::Bitmap icon(window.GetCommandIconBmp(6,24,false).c_str());
            double coverage=0;const UINT y=UINT(24*0.6);
            for(UINT x=0;x<UINT(24*0.3);++x) {Gdiplus::Color c;icon.GetPixel(x,y,&c);coverage+=c.GetAlpha()/255.0;}
            check(coverage>0.9 && coverage<2.3,"command glyph stroke is about 1 logical px instead of 1.78");
        }
        window.m_dpi=initialDpi;
        window.ApplyDpiScaledFonts();window.ApplyDpiScaledChrome();window.ApplyUiChromeTokens();
        window.ApplyChromeShellIcons();window.UpdateCommandBarState();window.UpdateNavButtons();
        window.RebuildFavoritesBar();window.UpdateSearchBoxWidth(1180);
        window.m_PaintManager.GetRoot()->SetPos({0,0,1180,740},false);window.RebuildBreadcrumb();
        return failures;
    }
    // Sequenced explicitly: operands of + have no evaluation order, and the command-bar check
    // must run before CheckUiPolish selects files.
    static int RunUiPolish(CMainWnd& window, const std::wstring& root) {
        int failures=CheckUiMetrics(window);
        failures+=CheckCommandBarAlignment(window);
        failures+=CheckUiPolish(window, root);
        return failures;
    }
    static int CheckUiMetrics(CMainWnd& window) {
        int failures = 0;
        auto check = [&](bool result, const char* name) {
            if (!result) { std::cerr << "FAIL " << name << '\n'; ++failures; }
        };
        const UINT initialDpi = window.m_dpi;
        for (UINT dpi : {96u,144u,192u}) {
            window.m_dpi=dpi;window.ApplyDpiScaledChrome();window.ApplyUiChromeTokens();window.ApplyDpiScaledFonts();window.RebuildFavoritesBar();
            auto* toolbarRow=static_cast<CContainerUI*>(window.m_PaintManager.FindControl(L"toolbar"));
            check(toolbarRow->GetChildPadding()==0 && toolbarRow->GetInset().left==MulDiv(6,dpi,96)
                && toolbarRow->GetFixedHeight()+2*window.DpiScaleHairline(1)==MulDiv(48,dpi,96),
                "command bar is 48 logical including hairlines and spaces commands by padding");
            for(const auto* name:{L"btn_new",L"btn_sort",L"btn_view_menu"})
                check(window.m_PaintManager.FindControl(name)->GetFixedWidth()==MulDiv(84,dpi,96),"label commands are 84 logical: padding icon label chevron");
            for(const auto* name:{L"btn_cut",L"btn_copy",L"btn_paste",L"btn_rename",L"btn_share",L"btn_delete",L"btn_more"}) {
                auto* c=window.m_PaintManager.FindControl(name);
                check(c->GetFixedWidth()==MulDiv(40,dpi,96) && c->GetFixedHeight()==MulDiv(32,dpi,96),"icon-only commands are 40x32 logical");
            }
            for(const auto* name:{L"btn_copy",L"btn_paste",L"btn_rename",L"btn_share",L"btn_delete"})
                check(window.m_PaintManager.FindControl(name)->GetPadding().left==MulDiv(8,dpi,96),"icon-only commands keep an 8 logical gap (48 pitch)");
            for(const auto* name:{L"sep_new",L"sep_organize",L"sep_more"}) {
                auto* c=window.m_PaintManager.FindControl(name);
                check(c->GetFixedWidth()==MulDiv(1,dpi,96) && c->GetFixedHeight()==MulDiv(32,dpi,96) && c->GetBkColor()==0xFFF0F0F0
                    && c->GetPadding().left==MulDiv(6,dpi,96),"group separators are 1x32 F0F0F0 with 6 logical clearance");
            }
            auto* addressRow=static_cast<CContainerUI*>(window.m_PaintManager.FindControl(L"address_bar"));
            check(addressRow->GetFixedHeight()==MulDiv(48,dpi,96) && addressRow->GetInset().left==MulDiv(7,dpi,96),"address row is 48 logical");
            for(const auto* name:{L"btn_back",L"btn_forward",L"btn_up",L"btn_refresh"}) {
                auto* c=window.m_PaintManager.FindControl(name);
                check(c->GetFixedWidth()==MulDiv(40,dpi,96) && c->GetPadding().left==(wcscmp(name,L"btn_back")==0 ? 0 : MulDiv(8,dpi,96)),
                    "nav buttons are 40 wide on a 48 logical pitch");
            }
            for(const auto* name:{L"path_host",L"search_box"}) {
                auto* c=window.m_PaintManager.FindControl(name);
                check(c->GetFixedHeight()==MulDiv(32,dpi,96) && c->GetBorderRound().cx==MulDiv(4,dpi,96)
                    && c->GetBkColor()==0xFFFCFCFB && c->GetBorderColor()==0xFFFCFCFB,
                    "address and search boxes are 32 logical, radius 4, FCFCFB with no visible border");
            }
            check(window.m_PaintManager.FindControl(L"gap_address_search")->GetFixedWidth()==MulDiv(8,dpi,96),"address and search boxes are 8 logical apart");
            check(window.m_PaintManager.FindControl(L"search_glyph")->GetFixedWidth()==MulDiv(11,dpi,96)
                && static_cast<CContainerUI*>(window.m_PaintManager.FindControl(L"search_box"))->GetInset().right==MulDiv(13,dpi,96),
                "search glyph is 11 logical and 13 logical from the right edge");
            auto* settingsButton=window.m_PaintManager.FindControl(L"btn_settings");
            check(settingsButton && settingsButton->GetFixedWidth()==MulDiv(40,dpi,96)
                && settingsButton->GetFixedHeight()==MulDiv(32,dpi,96),"settings gear hit area follows 96 144 and 192 DPI");
            auto* topLine=window.m_PaintManager.FindControl(L"command_top_divider");
            check(topLine && topLine->GetFixedHeight()==window.DpiScaleHairline(1) && topLine->GetBkColor()==0xFFE0E0E0,"command bar has a painted E0E0E0 hairline above it");
            auto* divider=window.m_PaintManager.FindControl(L"command_body_divider");
            check(divider && divider->GetFixedHeight()==window.DpiScaleHairline(1) && divider->GetBkColor()==0xFFE0E0E0,"file area uses a real painted separator instead of bottom-only border");
            check(static_cast<CContainerUI*>(window.m_PaintManager.FindControl(L"body_host"))->GetInset().top==MulDiv(8,dpi,96),"file group headers have breathing room below command bar");
            if(divider) {
                const RECT previous=divider->GetPos();const int h=divider->GetFixedHeight();
                HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
                HBITMAP bitmap=CreateCompatibleBitmap(screen,64,h+2);auto old=SelectObject(dc,bitmap);
                RECT canvas{0,0,64,h+2};FillRect(dc,&canvas,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                divider->SetPos({0,0,64,h},false);divider->Paint(dc,canvas,nullptr);
                check(GetPixel(dc,32,0)==RGB(0xe0,0xe0,0xe0) && GetPixel(dc,32,h)==RGB(255,255,255),
                    "command separator actually paints its full row with no spill into file area");
                divider->SetPos(previous,false);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
            }
            for(const auto* name:{L"titlebar",L"favorites_bar",L"minbtn",L"maxbtn",L"restorebtn",L"closebtn"})
                check(window.m_PaintManager.FindControl(name)->GetFixedHeight()==MulDiv(29,dpi,96),
                    "both compact bands and caption buttons scale from 80 percent rounded height");
            check(window.m_PaintManager.GetCaptionRect().bottom==MulDiv(29,dpi,96),"caption hit area matches compact row");
            check(window.m_PaintManager.FindControl(L"fav_bar_hint")->GetFixedHeight()==MulDiv(25,dpi,96)
                && window.m_PaintManager.FindControl(L"btn_favorite_toggle")->GetFixedHeight()==MulDiv(25,dpi,96),
                "favorite star fits inside compact band padding");
            for(int i=0;i<window.m_pFavoritesStrip->GetCount();++i)
                check(window.m_pFavoritesStrip->GetItemAt(i)->GetFixedHeight()==MulDiv(25,dpi,96),"favorite chips share compact button height");
            LOGFONTW body{},tab{};GetObjectW(window.m_PaintManager.GetFont(0),sizeof(body),&body);
            GetObjectW(window.m_PaintManager.GetFont(7),sizeof(tab),&tab);
            check(body.lfHeight==-MulDiv(12,dpi,96) && tab.lfHeight==-MulDiv(12,dpi,96)
                && UiTokens::TabIconPx==16 && UiTokens::FavIconPx==16,"compact chrome preserves readable fonts and shell icon sizes");
        }
        window.m_dpi=initialDpi;window.ApplyDpiScaledChrome();window.ApplyUiChromeTokens();window.ApplyDpiScaledFonts();window.RebuildFavoritesBar();
        if (!WaitQuickFixture(window) || window.m_pLeftQuickRows->GetCount() == 0) {
            check(false, "isolated Shell quick rows are ready for row-metric checks");
            return failures;
        }
        const int quickHeight=window.m_pLeftQuickRows->GetItemAt(0)->GetFixedHeight();
        auto* rootNode=static_cast<CTreeNodeUI*>(window.m_pDirTree->GetItemAt(0));
        const int treeHeight=rootNode->GetFixedHeight();
        check(window.m_pLeftQuickRows->GetCount()>0,"first-run profile renders the isolated Shell snapshot");
        const auto initialMode = window.m_viewMode;
        for (UINT dpi : {96u, 144u, 192u}) {
            window.m_dpi = dpi;
            check(DuiLib::TabStripRegressionAccess::SeparatorPixels(*window.m_pTabStrip,dpi,false)
                && DuiLib::TabStripRegressionAccess::SeparatorPixels(*window.m_pTabStrip,dpi,true),
                "tab band begins flush and separators are thin, short and support both themes at each DPI");
            const auto titleInset=static_cast<CContainerUI*>(window.m_PaintManager.FindControl(L"titlebar"))->GetInset();
            check(titleInset.left==0 && titleInset.top==0,"titlebar has no extra leading gutter");
            window.ApplyDpiScaledFonts();
            window.StyleSidePaneScrollBars(window.m_pDirTree);
            check(window.m_pDirTree->GetVerticalScrollBar()->GetFixedWidth()==MulDiv(12,dpi,96),
                "navigation thumb reserves a stable wide drag target at 96, 144 and 192 DPI");
            for (auto mode : {CMainWnd::ViewMode::LargeIcons, CMainWnd::ViewMode::ExtraLargeIcons}) {
                window.m_viewMode=mode;
                int w,h,icon,pad,label;
                window.GetViewMetrics(w,h,icon,pad,label);
                const int slot=mode==CMainWnd::ViewMode::LargeIcons ? 128 : 160;
                check(icon==MulDiv(slot,dpi,96) && w==MulDiv(slot+16,dpi,96) && h==MulDiv(slot+36,dpi,96),
                    "large icons have square contain slots and fixed one-line row geometry at each DPI");
                CButtonUI tile;
                CMainWnd::DirEntry entry{};
                entry.name=L"非常长的原始文件名称_ABCDEFGHIJKLMNOPQRSTUVWXYZ_1 (583).jPg";
                entry.fullPath=L"C:\\"+entry.name;
                window.ApplyTileText(&tile,entry,label,false,false);
                check(std::wstring(tile.GetText().GetData())==entry.name,
                    "large icon names retain original extension case and are ellipsized during painting");
                tile.SetTag(0x100); window.ApplyIconSelectionVisual(&tile);
                check(tile.GetBkColor()==0xFFE5F1FB && tile.GetBorderColor()==0xFF99D1FF,
                    "selected large icon paints the whole cell in requested fill and border");
            }
            window.m_viewMode=initialMode;
            LOGFONTW previewFont{},navFont{};
            GetObjectW(window.m_PaintManager.GetFont(5),sizeof(previewFont),&previewFont);
            GetObjectW(window.m_PaintManager.GetFont(4),sizeof(navFont),&navFont);
            check(previewFont.lfHeight==-MulDiv(16,dpi,96) && navFont.lfHeight==-MulDiv(12,dpi,96),
                "preview title and shared quick/tree font scale from 16 and 12 logical pixels");
            check(wcscmp(navFont.lfFaceName,L"Segoe UI")==0 && navFont.lfWeight==FW_NORMAL,
                "navigation uses regular Segoe UI for Latin text with system CJK fallback");
            check(window.m_pLeftQuickRows->GetItemAt(0)->GetFixedHeight()==quickHeight && rootNode->GetFixedHeight()==treeHeight,
                "changing navigation fonts preserves quick access and tree hit heights");
            {
                HDC dc=GetDC(window.m_hWnd);
                auto old=SelectObject(dc,window.m_PaintManager.GetFont(0));
                const std::wstring tail=L"…\\刘诗诗\\1 (583).jpg";
                SIZE size{};GetTextExtentPoint32W(dc,tail.c_str(),int(tail.size()),&size);
                check(CPreviewPathLabelUI::FitTail(dc,window.m_PaintManager.GetFont(0),
                    L"C:\\Users\\JINLONG\\图片\\GIRLS\\刘诗诗\\1 (583).jpg",size.cx)==tail,
                    "preview path ellipsis prioritizes parent folder and original filename at every DPI");
                SelectObject(dc,old);ReleaseDC(window.m_hWnd,dc);
                for(bool dim:{false,true}) {
                    Gdiplus::Bitmap icon(window.GetCommandIconBmp(6,MulDiv(16,dpi,96),dim).c_str());
                    BYTE alpha=0;bool coverage[256]{};
                    for(UINT y=0;y<icon.GetHeight();++y)for(UINT x=0;x<icon.GetWidth();++x) {
                        Gdiplus::Color pixel;icon.GetPixel(x,y,&pixel);
                        alpha=(std::max)(alpha,pixel.GetAlpha());coverage[pixel.GetAlpha()]=true;
                        if(pixel.GetAlpha()>0) check(pixel.GetR()==0x55 && pixel.GetG()==0x55 && pixel.GetB()==0x55,
                            "grey command icon layer uses Explorer 555555");
                    }
                    const int full=dim?int(UiTokens::CmdDisabledAlpha):255;
                    int smoothLevels=0;for(int a=1;a<full;++a)if(coverage[a])++smoothLevels;
                    check(smoothLevels>=(dim?6:10),"command glyph edges retain graded alpha at actual DPI size");
                    check(icon.GetWidth()==UINT(MulDiv(16,dpi,96)) && alpha==full,
                        "command bitmap is rendered at physical size with overall 36 percent disabled alpha");
                }
            }
            LOGFONTW font{};
            GetObjectW(window.m_PaintManager.GetFont(7), sizeof(font), &font);
            check(font.lfHeight == -MulDiv(12, dpi, 96) && font.lfWeight <= FW_NORMAL,
                "tab font stays compact and regular at 100, 150 and 200 percent DPI");
            HDC dc = GetDC(window.m_hWnd);
            HGDIOBJ previous = SelectObject(dc, window.m_PaintManager.GetFont(7));
            for (const wchar_t* title : {L"Program Files", L"此电脑", L"图片"}) {
                SIZE compact{}, oldSize{};
                GetTextExtentPoint32W(dc, title, int(wcslen(title)), &compact);
                LOGFONTW oldFont = font;
                oldFont.lfHeight = -MulDiv(14, dpi, 96);
                HFONT larger = CreateFontIndirectW(&oldFont);
                SelectObject(dc, larger);
                GetTextExtentPoint32W(dc, title, int(wcslen(title)), &oldSize);
                SelectObject(dc, window.m_PaintManager.GetFont(7));
                DeleteObject(larger);
                check(compact.cx < oldSize.cx && compact.cy < oldSize.cy,
                    "Chinese and English tab titles leave more room than the old 14 pixel font");
            }
            SelectObject(dc, previous);
            ReleaseDC(window.m_hWnd, dc);
        }
        window.m_dpi = initialDpi;
        window.ApplyDpiScaledFonts();
        window.StyleSidePaneScrollBars(window.m_pDirTree);
        for(UINT dpi:{96u,144u,192u})
            check(DuiLib::TabStripRegressionAccess::NoAutomaticMotion(*window.m_pTabStrip,dpi),
                "tab add insert switch title reorder close and wheel render their final pixels immediately at every DPI");
        window.RebuildTabStrip();
        return failures;
    }
    // Clicking a folder in 大图标 / 超大图标 must repaint the selection frame at once: the
    // selection handler may not run the details-pane work (Shell properties, large Shell
    // icon -> PNG) synchronously; it is deferred and the folder icon loads off-thread.
    static int CheckSelectionLatency(CMainWnd& window, const std::wstring& folder) {
        int failures = 0;
        auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
        auto pump = [&](DWORD ms) {
            const DWORD until = GetTickCount() + ms;
            do {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message != WM_QUIT && !CPaintManagerUI::TranslateMessage(&message)) {
                        ::TranslateMessage(&message); DispatchMessageW(&message);
                    }
                }
                Sleep(5);
            } while (GetTickCount() < until);
        };
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        auto now = [&]() { LARGE_INTEGER c{}; QueryPerformanceCounter(&c); return double(c.QuadPart) * 1000.0 / double(frequency.QuadPart); };
        auto select = [&](const std::wstring& path) {
            IFolderView2* view = ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
            if (!view) return false;
            int count = 0, index = -1; view->ItemCount(SVGIO_ALLVIEW, &count);
            for (int i = 0; i < count && index < 0; ++i) {
                IShellItem* item = nullptr; PWSTR name = nullptr;
                if (SUCCEEDED(view->GetItem(i, IID_PPV_ARGS(&item)))) {
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                        if (_wcsicmp(name, path.c_str()) == 0) index = i;
                        CoTaskMemFree(name);
                    }
                    item->Release();
                }
            }
            const bool ok = index >= 0 && SUCCEEDED(view->SelectItem(index, SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_FOCUSED));
            view->Release(); return ok;
        };
        auto title = [&]() { return window.m_pPreviewTitle ? std::wstring(window.m_pPreviewTitle->GetText().GetData()) : std::wstring(); };
        // Click -> first paint of the selected frame, through the real message path: the list
        // handles the posted button messages, posts our selection message, then paints.
        // Click -> first paint of the selected frame, through the real message path: the list
        // handles the button messages (its drag-detect loop runs while the button is held),
        // posts our selection message, then paints. A thread timer releases the button so the
        // release also reaches the list's nested loop.
        static HWND clickList = nullptr; static LPARAM clickPoint = 0;
        auto clickToFrameMs = [&](const std::wstring& path, UINT holdMs) {
            HWND list = ShellBrowserHostTestAccess::ListWindow(*window.m_shellBrowser);
            IFolderView2* view = ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
            if (!list || !view) { if (view) view->Release(); return -1.0; }
            int count = 0, index = -1; view->ItemCount(SVGIO_ALLVIEW, &count);
            for (int i = 0; i < count && index < 0; ++i) {
                IShellItem* item = nullptr; PWSTR name = nullptr;
                if (SUCCEEDED(view->GetItem(i, IID_PPV_ARGS(&item)))) {
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                        if (_wcsicmp(name, path.c_str()) == 0) index = i;
                        CoTaskMemFree(name);
                    }
                    item->Release();
                }
            }
            view->SelectItem(-1, SVSI_DESELECTOTHERS); view->Release();
            if (index < 0) return -1.0;
            pump(150);
            RECT icon{}; ListView_GetItemRect(list, index, &icon, LVIR_ICON);
            clickList = list;
            clickPoint = MAKELPARAM((icon.left + icon.right) / 2, (icon.top + icon.bottom) / 2);
            SendMessageW(list, WM_MOUSEMOVE, 0, clickPoint); pump(30);
            window.m_previewPath.clear();
            ShellBrowserHostTestAccess::Probe(*window.m_shellBrowser, index);
            LARGE_INTEGER frequency{}, begin{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
            PostMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, clickPoint);
            const UINT_PTR release = SetTimer(nullptr, 0, (std::max)(static_cast<UINT>(USER_TIMER_MINIMUM), holdMs),
                [](HWND, UINT, UINT_PTR timer, DWORD) { KillTimer(nullptr, timer); PostMessageW(clickList, WM_LBUTTONUP, 0, clickPoint); });
            const double start = now();
            while (now() - start < 1500.0 && !ShellBrowserHostTestAccess::ProbeTick(*window.m_shellBrowser)) pump(1);
            KillTimer(nullptr, release);
            if (GetCapture() == list) PostMessageW(list, WM_LBUTTONUP, 0, clickPoint);
            pump(50);
            const LONGLONG tick = ShellBrowserHostTestAccess::ProbeTick(*window.m_shellBrowser);
            ShellBrowserHostTestAccess::Probe(*window.m_shellBrowser, -1);
            return tick ? double(tick - begin.QuadPart) * 1000.0 / double(frequency.QuadPart) : -2.0;
        };
        const bool previewWasVisible = window.m_previewVisible;
        const auto previousMode = window.m_viewMode;
        if (!previewWasVisible) window.SetPreviewVisible(true);
        window.NavigateToNow(folder, true); pump(500);
        const auto target = folder + L"\\Battle.net";
        for (auto mode : {CMainWnd::ViewMode::LargeIcons, CMainWnd::ViewMode::ExtraLargeIcons}) {
            window.SetViewMode(mode); pump(400);
            select(folder + L"\\115Chrome"); pump(400);
            check(select(target), "large icon fixture folder can be selected");
            // Run the handler directly, before the queue (and the view's WM_PAINT) is pumped.
            const double start = now();
            window.SyncShellViewSelection();
            const double handler = now() - start;
            const bool deferred = window.m_selectionPreviewPending && title() != L"Battle.net";
            std::cout << "selection handler " << (mode == CMainWnd::ViewMode::LargeIcons ? "large" : "extra-large")
                << ": " << handler << " ms\n";
            check(deferred, "large icon selection defers the details pane instead of loading it in the selection handler");
            check(handler < 50.0, "large icon selection handler returns quickly");
            const DWORD deadline = GetTickCount() + 3000;
            while ((title() != L"Battle.net" || window.m_previewBmp.empty()) && GetTickCount() < deadline) pump(20);
            check(title() == L"Battle.net" && !window.m_previewBmp.empty() && window.m_previewIconThreads.empty(),
                "deferred details pane and off-thread folder icon arrive for the selected folder");
            // Real click through the list: the selected frame is painted, and before the pane.
            const bool wasShown = IsWindowVisible(window.m_hWnd) != FALSE;
            if (!wasShown) { ShowWindow(window.m_hWnd, SW_SHOWNOACTIVATE); pump(300); }
            const double frame = clickToFrameMs(target, 0);
            std::cout << "click->frame fixture " << (mode == CMainWnd::ViewMode::LargeIcons ? "large" : "extra-large")
                << ": " << frame << " ms\n";
            check(frame >= 0.0, "a real click paints the large icon selection frame");
            pump(200);
            check(title() == L"Battle.net", "the clicked folder still reaches the details pane");
            if (!wasShown) { ShowWindow(window.m_hWnd, SW_HIDE); pump(150); }
        }
        {   // A stale icon result (selection moved on) is discarded, not shown.
            window.m_previewPath.clear();
            select(target); window.SyncShellViewSelection(); window.FlushSelectionPreview();
            const unsigned serial = window.m_previewSerial;
            select(folder + L"\\115Chrome"); window.SyncShellViewSelection(); window.FlushSelectionPreview();
            const DWORD deadline = GetTickCount() + 3000;
            while (!window.m_previewIconThreads.empty() && GetTickCount() < deadline) pump(20);
            pump(100);
            check(window.m_previewSerial == serial + 1 && title() == L"115Chrome" &&
                window.m_previewBmp.find(L"preview_icon_" + std::to_wstring(serial + 1)) != std::wstring::npos,
                "only the newest folder icon is applied");
        }
        // Timing on the user's real Pictures folder (read-only): the legacy synchronous handler
        // versus the deferred one, as click -> selected frame painted and as handler cost.
        // Printed for the report; not asserted (machine and data dependent).
        PWSTR pictures = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pictures))) {
            const std::wstring root = pictures; CoTaskMemFree(pictures);
            std::vector<std::wstring> folders;
            WIN32_FIND_DATAW data{}; HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &data);
            if (find != INVALID_HANDLE_VALUE) {
                do {
                    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && data.cFileName[0] != L'.' && folders.size() < 3)
                        folders.push_back(root + L"\\" + data.cFileName);
                } while (FindNextFileW(find, &data));
                FindClose(find);
            }
            window.NavigateToNow(root, true); pump(800);
            const bool wasShown = IsWindowVisible(window.m_hWnd) != FALSE;
            if (!wasShown) { ShowWindow(window.m_hWnd, SW_SHOWNOACTIVATE); pump(400); }
            for (auto mode : {CMainWnd::ViewMode::LargeIcons, CMainWnd::ViewMode::ExtraLargeIcons}) {
                window.SetViewMode(mode); pump(400);
                for (const auto& path : folders) {
                    for (bool legacy : {true, false}) {
                        window.m_deferSelectionPreview = !legacy; window.m_asyncPreviewIcons = !legacy;
                        const double ms = clickToFrameMs(path, 0);
                        pump(250);
                        std::wcout << L"pictures click->frame " << (mode == CMainWnd::ViewMode::LargeIcons ? L"large " : L"xlarge ")
                            << PathFindFileNameW(path.c_str()) << (legacy ? L" legacy=" : L" new=") << ms << L" ms\n";
                    }
                }
                window.m_deferSelectionPreview = true; window.m_asyncPreviewIcons = true;
            }
            if (!wasShown) { ShowWindow(window.m_hWnd, SW_HIDE); pump(200); }
            window.SetViewMode(CMainWnd::ViewMode::LargeIcons); pump(300);
            for (const auto& path : folders) {
                if (!select(path)) continue;
                window.m_previewPath.clear(); window.m_asyncPreviewIcons = false;
                double t0 = now(); window.UpdatePreviewForSelection(); const double oldCost = now() - t0;
                window.m_previewPath.clear(); window.m_asyncPreviewIcons = true;
                window.m_shellSelectionSnapshot.clear();
                t0 = now(); window.SyncShellViewSelection(); const double newHandler = now() - t0;
                t0 = now(); window.FlushSelectionPreview(); const double deferredCost = now() - t0;
                pump(250);
                std::wcout << L"pictures handler " << PathFindFileNameW(path.c_str()) << L": sync-old=" << oldCost
                    << L" ms, new-handler=" << newHandler << L" ms, deferred-ui=" << deferredCost << L" ms\n";
            }
            window.ClearFileSelection(); pump(200);
        }
        window.NavigateToNow(folder, true); pump(400);
        window.SetViewMode(previousMode); pump(300);
        if (!previewWasVisible) window.SetPreviewVisible(false);
        return failures;
    }
    // 1.0.21: every Shell menu is the unmodified Windows classic menu, its commands invoked
    // natively; the hosted view shows its own. Nothing is shown on screen (seams / CBT block).
    static int CheckShellMenus(CMainWnd& window, const std::wstring& folder) {
        int failures = CheckShellMenuDrawing(window);
        auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
        auto pump = [&](DWORD ms) {
            const DWORD until = GetTickCount() + ms;
            do {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message != WM_QUIT && !CPaintManagerUI::TranslateMessage(&message)) {
                        ::TranslateMessage(&message); DispatchMessageW(&message);
                    }
                }
                Sleep(5);
            } while (GetTickCount() < until);
        };
        window.NavigateToNow(folder, true);
        const DWORD deadline = GetTickCount() + 4000;
        do { pump(50); } while (!window.ShellBrowserShowsFolder(folder) && GetTickCount() < deadline);
        pump(300);
        // A parent repaint after a popup closes must never cover the native list.
        const bool paintWasShown = IsWindowVisible(window.m_hWnd) != FALSE;
        if (!paintWasShown) ShowWindow(window.m_hWnd, SW_SHOWNOACTIVATE);
        window.SyncLayoutDependents(); pump(200);
        HWND nativeList = nullptr;
        EnumChildWindows(window.m_hWnd, [](HWND child, LPARAM data)->BOOL {
            wchar_t name[64]{}; GetClassNameW(child,name,_countof(name));
            if (wcscmp(name,L"SysListView32")==0 && IsWindowVisible(child)) {
                *reinterpret_cast<HWND*>(data)=child; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&nativeList));
        RECT nativeRect{}; if (nativeList) GetWindowRect(nativeList,&nativeRect);
        POINT nativePoint{(nativeRect.left+nativeRect.right)/2,(nativeRect.top+nativeRect.bottom)/2};
        ScreenToClient(window.m_hWnd,&nativePoint);
        HDC parentDc=GetDC(window.m_hWnd);
        check(nativeList && nativeRect.right>nativeRect.left && nativeRect.bottom>nativeRect.top
            && (GetWindowLongPtrW(window.m_hWnd,GWL_STYLE)&WS_CLIPCHILDREN)
            && !PtVisible(parentDc,nativePoint.x,nativePoint.y),
            "parent painting excludes native files after context-menu repaint");
        ReleaseDC(window.m_hWnd,parentDc);
        NativeDialogGuard dialogs;
        IContextMenu* menu = nullptr; HMENU popup = nullptr; UINT shellMax = 0; bool fromView = false;
        const bool built = window.BuildShellBackgroundMenu(folder, &menu, &popup, &shellMax, &fromView);
        check(built && fromView, "folder background menu comes from the live Explorer view (SVGIO_BACKGROUND)");
        if (built) {
            check(ShellMenuUtil::FindVerb(menu, popup, 1, shellMax, L"paste") >= 0, "background menu contains native paste");
            check(ShellMenuUtil::FindVerb(menu, popup, 1, shellMax, L"properties") >= 0, "background menu contains native properties");
            check(ShellMenuUtil::FindVerb(menu, popup, 1, shellMax, L"groupby") >= 0, "background menu contains native group-by");
            // Unmodified: item-for-item the same as an untouched SVGIO_BACKGROUND menu queried
            // with Explorer's flags (no 查看/排序 swap, no refresh/paste/undo inserts, no tidying).
            RawShellMenu raw;
            const bool rawOk = SUCCEEDED(window.m_shellBrowser->CreateBackgroundContextMenu(&raw.menu))
                && raw.Query(CMainWnd::ShellBackgroundMenuFlags(false));
            std::string why;
            check(rawOk && shellMax == raw.last && SameShellMenu(popup, raw.popup, &why),
                "view background menu is the unmodified Windows menu");
            if (!why.empty()) std::cerr << "  background menu diagnostic: " << why << '\n';
            POINT popupPoint=nativePoint; ClientToScreen(window.m_hWnd,&popupPoint);
            const UINT_PTR cancelTimer=SetTimer(nullptr,0,100,[](HWND,UINT,UINT_PTR id,DWORD) {
                EndMenu(); KillTimer(nullptr,id);
            });
            if (cancelTimer) {
                window.TrackPopupShellMenu(menu,popup,popupPoint,1,shellMax);
                KillTimer(nullptr,cancelTimer);
                InvalidateRect(window.m_hWnd,nullptr,FALSE); UpdateWindow(window.m_hWnd);
                parentDc=GetDC(window.m_hWnd);
                check(!PtVisible(parentDc,nativePoint.x,nativePoint.y)
                    && !window.m_pCtxMenu2 && !window.m_pCtxMenu3,
                    "real Shell popup cleanup preserves native view paint exclusion");
                ReleaseDC(window.m_hWnd,parentDc);
            } else check(false,"popup cancellation timer starts");
            DestroyMenu(popup); menu->Release();
        }
        // The hosted view's right-click is DefView's own: FastFile never receives or builds it.
        // Menu windows are blocked by a CBT hook, so nothing is shown even here.
        int fastFileMenus = 0;
        window.m_trackMenuHook = [&](IContextMenu*, HMENU) { ++fastFileMenus; return 0u; };
        if (nativeList) {
            blockedMenuWindows = 0;
            HHOOK block = SetWindowsHookExW(WH_CBT, BlockMenuWindows, nullptr, GetCurrentThreadId());
            check(block != nullptr, "menu-window blocking hook installed");
            const UINT_PTR safety=SetTimer(nullptr,0,300,[](HWND,UINT,UINT_PTR id,DWORD) { EndMenu(); KillTimer(nullptr,id); });
            if (block) {
                POINT screen{(nativeRect.left+nativeRect.right)/2,(nativeRect.top+nativeRect.bottom)/2};
                SendMessageW(nativeList, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(nativeList), MAKELPARAM(screen.x, screen.y));
                pump(100);
                UnhookWindowsHookEx(block);
            }
            if (safety) KillTimer(nullptr, safety);
            check(blockedMenuWindows >= 1 && fastFileMenus == 0,
                "right-click in the Windows view opens DefView's own menu, not a FastFile-built one");
        }
        window.m_trackMenuHook = nullptr;
        if (!paintWasShown) ShowWindow(window.m_hWnd,SW_HIDE);
        // Every menu FastFile builds itself (tree, search list, Quick Access, favorites) is the
        // raw Windows menu with Explorer's flags. The track seam returns 0 (cancel) or a chosen
        // command instead of showing a popup; the invoke seam records instead of executing.
        struct Capture { int calls = 0; bool same = false; bool sited = false; std::string why; };
        auto captureMenu = [&](RawShellMenu& raw, Capture& out, const wchar_t* pick = nullptr) {
            window.m_trackMenuHook = [&raw, &out, pick](IContextMenu* shown, HMENU shownPopup) -> UINT {
                ++out.calls;
                out.same = raw.popup && SameShellMenu(shownPopup, raw.popup, &out.why);
                IUnknown* site = nullptr;
                out.sited = SUCCEEDED(IUnknown_GetSite(shown, IID_IUnknown, reinterpret_cast<void**>(&site))) && site;
                if (site) site->Release();
                return pick ? VerbCommand(shown, shownPopup, 0x7FFF, pick) : 0u;
            };
        };
        auto siteLikeFastFile = [&](RawShellMenu& raw) { if (raw.menu) window.m_shellBrowser->SiteContextMenu(raw.menu); };
        // Item menus are sited on the hosted ExplorerBrowser (SID_SShellBrowser), as Explorer
        // sites navigation-pane menus on its frame, so a native 打开 can browse in place.
        auto report = [&](const Capture& capture, const char* name, bool requireSite = true) {
            if (!capture.why.empty()) std::cerr << "  " << name << " diagnostic: " << capture.why << '\n';
            if (requireSite && capture.calls) {
                if (!capture.sited) std::cerr << "  " << name << " is not sited on the hosted browser\n";
                check(capture.sited, "FastFile-built item menus are sited on the hosted Windows browser");
            }
        };
        RECT owner{}; GetWindowRect(window.m_hWnd, &owner);
        const POINT at{owner.left + 80, owner.top + 80};
        const auto sample = folder + L"\\sample.txt";
        const auto subfolder = folder + L"\\Battle.net";
        BYTE savedKeys[256]{}; GetKeyboardState(savedKeys);
        auto setShift = [&](bool down) {
            BYTE keys[256]{}; if (down) keys[VK_SHIFT] = keys[VK_LSHIFT] = 0x80; SetKeyboardState(keys);
        };
        {
            RawShellMenu raw; Capture capture;
            const UINT flags = CMainWnd::ShellItemMenuFlags(false, false);
            check(flags == (CMF_NORMAL | CMF_ITEMMENU), "item menus use CMF_NORMAL | CMF_ITEMMENU");
            check(raw.Path(sample, window.m_hWnd), "raw file menu"); siteLikeFastFile(raw); raw.Query(flags);
            captureMenu(raw, capture);
            setShift(false);
            const bool shown = window.ShowShellContextMenu({sample}, at);
            check(shown && capture.calls == 1 && capture.same && window.m_lastShellMenuFlags == flags,
                "search-list file menu is the unmodified Windows item menu");
            check(ShellMenuUtil::FindVerb(raw.menu, raw.popup, 1, raw.last, L"rename") < 0,
                "menus outside the hosted view never claim CMF_CANRENAME");
            report(capture, "file menu");
        }
        {
            RawShellMenu raw; Capture capture;
            const UINT flags = CMainWnd::ShellItemMenuFlags(true, false);
            check((flags & CMF_EXTENDEDVERBS) && !(CMainWnd::ShellItemMenuFlags(false, false) & CMF_EXTENDEDVERBS)
                && (CMainWnd::ShellBackgroundMenuFlags(true) & CMF_EXTENDEDVERBS)
                && CMainWnd::ShellBackgroundMenuFlags(false) == CMF_NORMAL,
                "Shift adds CMF_EXTENDEDVERBS; background menus use CMF_NORMAL");
            setShift(true); // some handlers also read the Shift key state themselves
            raw.Path(sample, window.m_hWnd); siteLikeFastFile(raw); raw.Query(flags);
            captureMenu(raw, capture);
            window.ShowShellContextMenu({sample}, at);
            setShift(false);
            check(capture.calls == 1 && capture.same && (window.m_lastShellMenuFlags & CMF_EXTENDEDVERBS),
                "Shift right-click queries the extended Windows item menu");
            report(capture, "extended file menu");
        }
        {
            RawShellMenu raw; Capture capture;
            raw.Background(subfolder, window.m_hWnd); raw.Query(CMainWnd::ShellBackgroundMenuFlags(false));
            captureMenu(raw, capture);
            const bool shown = window.ShowShellBackgroundContextMenu(subfolder, at);
            check(shown && capture.calls == 1 && capture.same && window.m_lastShellMenuFlags == CMF_NORMAL,
                "background menu of a folder not on screen is the folder's own CreateViewObject menu");
            report(capture, "folder background menu", false);
        }
        // Navigation tree: This PC root gets the Computer item's menu, folders their item menu,
        // both with CMF_EXPLORE like Explorer's navigation pane; placeholders get none.
        CTreeNodeUI* thisPcNode = nullptr;
        for (int i = 0; window.m_pDirTree && i < window.m_pDirTree->GetCount() && !thisPcNode; ++i) {
            auto* node = static_cast<CTreeNodeUI*>(window.m_pDirTree->GetItemAt(i)->GetInterface(DUI_CTR_TREENODE));
            if (node && CMainWnd::IsThisPcPath(node->GetUserData().GetData())) thisPcNode = node;
        }
        check(thisPcNode != nullptr, "navigation tree has a This PC root");
        if (thisPcNode) {
            RawShellMenu raw; Capture capture;
            const UINT flags = CMainWnd::ShellItemMenuFlags(false, true);
            raw.Computer(window.m_hWnd); siteLikeFastFile(raw); raw.Query(flags);
            captureMenu(raw, capture);
            window.ShowTreeContextMenu(thisPcNode, at);
            check(capture.calls == 1 && capture.same && window.m_lastShellMenuFlags == flags && (flags & CMF_EXPLORE),
                "tree This PC root shows the Computer item menu with CMF_EXPLORE");
            check(ShellMenuUtil::FindVerb(raw.menu, raw.popup, 1, raw.last, L"properties") >= 0,
                "Computer item menu offers Windows' own properties");
            report(capture, "tree This PC menu");
        }
        window.SyncTreeToPath(folder); pump(200);
        if (CTreeNodeUI* folderNode = window.FindTreeNodeByPath(nullptr, folder)) {
            RawShellMenu raw; Capture capture;
            const UINT flags = CMainWnd::ShellItemMenuFlags(false, true);
            raw.Path(folder, window.m_hWnd); siteLikeFastFile(raw); raw.Query(flags);
            captureMenu(raw, capture);
            window.ShowTreeContextMenu(folderNode, at);
            check(capture.calls == 1 && capture.same && window.m_lastShellMenuFlags == flags,
                "tree folder shows its unmodified Windows item menu with CMF_EXPLORE");
            report(capture, "tree folder menu");
        } else check(false, "navigation tree reveals the fixture folder");
        {
            CTreeNodeUI pending; pending.SetUserData(CMainWnd::kPendingMarker);
            RawShellMenu raw; Capture capture; captureMenu(raw, capture);
            window.ShowTreeContextMenu(&pending, at);
            check(capture.calls == 0, "tree loading placeholder has no menu");
        }
        // Quick Access 此电脑 row and favorite chips use the same native item menus.
        {
            const auto savedRows = window.m_quickRows;
            CMainWnd::QuickRow thisPc; thisPc.isThisPc = true; thisPc.path = CMainWnd::kThisPcPath;
            window.m_quickRows = {thisPc};
            RawShellMenu raw; Capture capture;
            const UINT flags = CMainWnd::ShellItemMenuFlags(false, false);
            raw.Computer(window.m_hWnd); siteLikeFastFile(raw); raw.Query(flags);
            captureMenu(raw, capture);
            window.ShowQuickRowContextMenu(0, at);
            window.m_quickRows = savedRows;
            check(capture.calls == 1 && capture.same && window.m_lastShellMenuFlags == flags,
                "Quick Access 此电脑 row shows the Computer item menu");
            report(capture, "Quick Access This PC menu");
        }
        {
            CButtonUI chip; chip.SetUserData(folder.c_str());
            RawShellMenu raw; Capture capture;
            const UINT flags = CMainWnd::ShellItemMenuFlags(false, false);
            raw.Path(folder, window.m_hWnd); siteLikeFastFile(raw); raw.Query(flags);
            captureMenu(raw, capture);
            window.ShowFavoriteContextMenu(&chip, at);
            check(capture.calls == 1 && capture.same && window.m_lastShellMenuFlags == flags,
                "favorite chip shows the folder's unmodified Windows item menu");
            report(capture, "favorite chip menu");
            CButtonUI computerChip; computerChip.SetUserData(CMainWnd::kThisPcPath);
            RawShellMenu computerRaw; Capture computerCapture;
            computerRaw.Computer(window.m_hWnd); siteLikeFastFile(computerRaw); computerRaw.Query(flags);
            captureMenu(computerRaw, computerCapture);
            window.ShowFavoriteContextMenu(&computerChip, at);
            check(computerCapture.calls == 1 && computerCapture.same, "此电脑 favorite chip shows the Computer item menu");
        }
        // No takeover: a chosen command always goes back to the same native menu by its offset.
        std::vector<CMainWnd::NativeVerbCall> invoked;
        window.m_nativeInvokeHook = [&](IContextMenu*) { invoked.push_back(window.m_lastNativeVerb); return S_OK; };
        auto picked = [&](const wchar_t* verb, bool shift) {
            const DWORD required = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
            return invoked.size() == 1 && _wcsicmp(invoked[0].verb.c_str(), verb) == 0 && invoked[0].byOffset
                && (invoked[0].mask & required) == required && ((invoked[0].mask & CMIC_MASK_SHIFT_DOWN) != 0) == shift;
        };
        {
            RawShellMenu raw; Capture capture; captureMenu(raw, capture, L"delete");
            invoked.clear(); setShift(false);
            window.ShowShellContextMenu({sample}, at);
            check(picked(L"delete", false) && GetFileAttributesW(sample.c_str()) != INVALID_FILE_ATTRIBUTES,
                "chosen 删除 is invoked natively by offset (recorded, not executed)");
            invoked.clear(); setShift(true);
            window.ShowShellContextMenu({sample}, at);
            setShift(false);
            check(picked(L"delete", true), "Shift + chosen 删除 reaches Windows with CMIC_MASK_SHIFT_DOWN");
        }
        {
            const auto before = window.m_currentPath;
            RawShellMenu raw; Capture capture; captureMenu(raw, capture, L"open");
            invoked.clear();
            window.ShowShellContextMenu({subfolder}, at, true);
            MSG intercepted{};
            const bool routed = PeekMessageW(&intercepted, window.m_hWnd, CMainWnd::kMsgShellFolderOpen,
                CMainWnd::kMsgShellFolderOpen, PM_NOREMOVE) != FALSE;
            check(picked(L"open", false) && !routed && window.m_currentPath == before,
                "folder 打开 from a FastFile-built menu is Windows' open verb, not intercepted");
        }
        window.m_trackMenuHook = nullptr;
        window.m_nativeInvokeHook = nullptr;
        SetKeyboardState(savedKeys);
        check(dialogs.Dismissed() == 0, "no dialog appeared during the menu checks");
        return failures;
    }
    // Copy / move / recycle / delete run through IFileOperation. Production flags keep the
    // native progress, conflict and error UI; the non-interactive mode is for tests only.
    static int CheckFileOperationEngine(const std::wstring& root) {
        using namespace ShellFileOps;
        int failures = 0;
        auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
        for (Kind kind : {Kind::Copy, Kind::Move, Kind::Recycle, Kind::Delete}) {
            const DWORD flags = OperationFlags(kind, true);
            check((flags & (FOF_SILENT | FOF_NOERRORUI | FOFX_NOMINIMIZEBOX | FOFX_EARLYFAILURE)) == 0,
                "interactive file operations keep the Windows progress dialog and error UI");
            check((flags & FOFX_SHOWELEVATIONPROMPT) != 0, "interactive file operations allow elevation");
            check((OperationFlags(kind, false) & (FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION)) ==
                (FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION), "test mode suppresses every Windows dialog");
        }
        for (Kind kind : {Kind::Copy, Kind::Move}) {
            check((OperationFlags(kind, true) & (FOF_NOCONFIRMATION | FOF_RENAMEONCOLLISION)) == 0,
                "copy and move ask the native replace / skip question");
            check((OperationFlags(kind, true) & (FOF_ALLOWUNDO | FOFX_ADDUNDORECORD)) == (FOF_ALLOWUNDO | FOFX_ADDUNDORECORD),
                "drag-drop copy and move join the single Windows undo history");
        }
        check((OperationFlags(Kind::Recycle, true) & (FOFX_RECYCLEONDELETE | FOF_ALLOWUNDO | FOFX_ADDUNDORECORD)) ==
            (FOFX_RECYCLEONDELETE | FOF_ALLOWUNDO | FOFX_ADDUNDORECORD), "recycle keeps the Explorer undo record");
        check((OperationFlags(Kind::Delete, true) & (FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE)) == 0,
            "permanent delete never recycles");

        const auto base = root + L"\\FileOperationEngine";
        const auto source = base + L"\\源", target = base + L"\\目标", moved = base + L"\\移动", legacy = base + L"\\旧引擎";
        for (const auto& path : {base, source, target, moved, legacy}) CreateDirectoryW(path.c_str(), nullptr);
        const auto file = source + L"\\数据.txt";
        const auto folder = source + L"\\子目录";
        CreateDirectoryW(folder.c_str(), nullptr);
        auto write = [](const std::wstring& path) {
            HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD written = 0; WriteFile(handle, "FastFile", 8, &written, nullptr); CloseHandle(handle);
        };
        write(file); write(folder + L"\\内部.txt");
        auto exists = [](const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; };
        Request request; request.interactive = false;
        request.kind = Kind::Copy; request.sources = {file, folder}; request.destination = target;
        auto result = Perform(request);
        check(result.engine == Engine::FileOperation && SUCCEEDED(result.hr) && result.completed.size() == 2 &&
            exists(target + L"\\数据.txt") && exists(target + L"\\子目录\\内部.txt") && exists(file),
            "IFileOperation copy reports both top-level items and keeps sources");
        request.sources = {file}; request.destination = source;
        result = Perform(request);
        check(result.completed.size() == 1 && !PathsEqual(result.completed[0].second, file) &&
            exists(result.completed[0].second) && result.notUndoable == 0,
            "copy into the same folder records the renamed copy");
        const auto duplicate = result.completed.empty() ? std::wstring() : result.completed[0].second;
        request.kind = Kind::Move; request.sources = {target + L"\\数据.txt"}; request.destination = moved;
        result = Perform(request);
        check(result.engine == Engine::FileOperation && result.completed.size() == 1 &&
            PathsEqual(result.completed[0].second, moved + L"\\数据.txt") &&
            !exists(target + L"\\数据.txt") && exists(moved + L"\\数据.txt"), "IFileOperation move reports the moved item");
        std::atomic<bool> cancel{true};
        request.kind = Kind::Copy; request.sources = {file}; request.destination = legacy;
        result = Perform(request, &cancel);
        check(result.completed.empty() && !exists(legacy + L"\\数据.txt"), "cancelled operation records nothing");
        request.kind = Kind::Recycle; request.sources = {duplicate};
        result = Perform(request);
        check(!duplicate.empty() && result.completed.size() == 1 && !exists(duplicate), "IFileOperation recycle reports the recycled item");
        request.kind = Kind::Delete; request.sources = {target + L"\\子目录"};
        result = Perform(request);
        check(result.completed.size() == 1 && !exists(target + L"\\子目录"), "IFileOperation delete reports the removed folder");
        request.kind = Kind::Copy; request.sources = {file}; request.destination = legacy; request.useLegacyEngine = true;
        result = Perform(request);
        check(result.engine == Engine::LegacyFileOp && result.completed.size() == 1 && exists(legacy + L"\\数据.txt"),
            "SHFileOperation fallback copies and reports the created item");
        return failures;
    }
    // View switches in Shell browsing change the native view in place: one mode apply,
    // no filter reset / Refresh re-enumeration, no redundant spacing / image-list swaps,
    // a single batched repaint, and the remembered folder view is still saved.
    static int CheckViewSwitch(CMainWnd& window, const std::wstring& parent) {
        int failures = 0;
        auto check = [&](bool ok, const std::string& name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
        auto pump = [&](DWORD ms) {
            const DWORD until = GetTickCount() + ms;
            do {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message != WM_QUIT && !CPaintManagerUI::TranslateMessage(&message)) {
                        ::TranslateMessage(&message); DispatchMessageW(&message);
                    }
                }
                Sleep(5);
            } while (GetTickCount() < until);
        };
        const std::wstring folder = parent + L"\\ViewSwitch";
        CreateDirectoryW(folder.c_str(), nullptr);
        for (int i = 0; i < 6; ++i) CreateDirectoryW((folder + L"\\dir" + std::to_wstring(i)).c_str(), nullptr);
        for (int i = 0; i < 60; ++i) {
            HANDLE file = CreateFileW((folder + L"\\file" + std::to_wstring(i) + L".txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            if (file != INVALID_HANDLE_VALUE) { DWORD written = 0; WriteFile(file, "x", 1, &written, nullptr); CloseHandle(file); }
        }
        const bool remember = window.m_settings.rememberViews;
        window.m_settings.rememberViews = true;
        // Paint counts need a visible window (on the isolated test desktop).
        const bool wasShown = IsWindowVisible(window.m_hWnd) != FALSE;
        if (!wasShown) ShowWindow(window.m_hWnd, SW_SHOWNOACTIVATE);
        window.m_PaintManager.GetRoot()->SetPos({0, 0, 1180, 740}, false);
        window.SyncLayoutDependents();
        window.NavigateToNow(folder, false); pump(700);
        window.SetViewMode(CMainWnd::ViewMode::Details); pump(500);
        ShellBrowserHost& host = *window.m_shellBrowser;
        {
            // Verify that right after navigation in Details view, folder and file items resolve icons without F5:
            HWND list = ShellBrowserHostTestAccess::ListWindow(host);
            const int count = list ? ListView_GetItemCount(list) : 0;
            check(count > 0, "Details view has populated items after navigation");
            for (int itm = 0; itm < (std::min)(5, count); ++itm) {
                wchar_t c0[256]{}, c1[256]{}, c2[256]{};
                ListView_GetItemText(list, itm, 0, c0, _countof(c0));
                ListView_GetItemText(list, itm, 1, c1, _countof(c1));
                ListView_GetItemText(list, itm, 2, c2, _countof(c2));
                check(wcslen(c0) > 0, "Details view item name (column 0) is populated");
                check(wcslen(c1) > 0, "Details view item modified date (column 1) is populated");
                check(wcslen(c2) > 0, "Details view item type (column 2) is populated");
            }
            for (int itm = 0; itm < (std::min)(10, count); ++itm) {
                const int img = ShellBrowserHostTestAccess::ResolveItemIcon(host, itm);
                check(img >= 0, "Details view item resolves icon without manual refresh (F5)");
            }
        }
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        using Mode = CMainWnd::ViewMode;
        const Mode sequence[] = { Mode::LargeIcons, Mode::Details, Mode::ExtraLargeIcons, Mode::List,
            Mode::Details, Mode::Tiles, Mode::SmallIcons, Mode::MediumIcons, Mode::Details };
        const char* names[] = { "large", "details", "xlarge", "list", "details", "tiles", "small", "medium", "details" };
        // Warm the system icon / thumbnail caches for every size first: cold Shell icon
        // extraction invalidates items one by one as icons arrive, which is not switch cost.
        for (Mode mode : sequence) { window.SetViewMode(mode); pump(350); }
        int totalPaints = 0, maxPaints = 0;
        double totalMs = 0;
        for (size_t i = 0; i < _countof(sequence); ++i) {
            const auto before = ShellBrowserHostTestAccess::Counters(host);
            LARGE_INTEGER t0{}, t1{}; QueryPerformanceCounter(&t0);
            window.SetViewMode(sequence[i]);
            QueryPerformanceCounter(&t1);
            const double ms = double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(frequency.QuadPart);
            pump(400);
            const auto after = ShellBrowserHostTestAccess::Counters(host);
            const int paints = after.listPaints - before.listPaints;
            const int fullPaints = after.fullPaints - before.fullPaints;
            totalPaints += fullPaints; maxPaints = (std::max)(maxPaints, fullPaints); totalMs += ms;
            const std::string tag = std::string(" (to ") + names[i] + ")";
            std::cout << "view switch to " << names[i] << ": sync " << ms << " ms, list paints " << paints
                << " (full-list " << fullPaints << ")"
                << ", applies " << after.modeApplies - before.modeApplies << ", refreshes " << after.refreshes - before.refreshes
                << ", filters " << after.filterSets - before.filterSets << ", spacer swaps " << after.spacerSwaps - before.spacerSwaps
                << ", spacing sets " << after.iconSpacingSets - before.iconSpacingSets << '\n';
            check(after.refreshes == before.refreshes, "view switch must not Refresh / re-enumerate the Shell view" + tag);
            check(after.filterSets == before.filterSets, "view switch must not reset the Shell filter" + tag);
            check(after.modeApplies - before.modeApplies == 1, "view switch applies the native mode exactly once" + tag);
            check(after.modeSets - before.modeSets == 1, "view switch calls SetViewModeAndIconSize exactly once" + tag);
            check(after.redrawBatches - before.redrawBatches <= 1, "a real view change is one redraw batch" + tag);
            check(fullPaints >= 1, "list repaints fully after a view switch (redraw re-enabled)" + tag);
            check(fullPaints <= 2, "view switch repaints the whole list at most twice" + tag);
            check(window.LoadFolderViewForPath(folder) == sequence[i], "view switch still saves the per-folder view" + tag);
            IFolderView2* view = ShellBrowserHostTestAccess::View(host);
            FOLDERVIEWMODE native = FVM_AUTO; int size = 0; DWORD flags = 0;
            if (view) {
                view->GetViewModeAndIconSize(&native, &size);
                view->GetCurrentFolderFlags(&flags);
                view->Release();
            }
            if (sequence[i] == Mode::Tiles) {
                check(size == window.DpiScale(48), "Tiles view sets and reports uniform 48-px icon size" + tag);
            }
            const bool details = sequence[i] == Mode::Details;
            check(bool(flags & FWF_NOHEADERINALLVIEWS) == !details, "view switch keeps native header in Details only" + tag);
            const bool spacer = sequence[i] == Mode::Details || sequence[i] == Mode::List;
            check(ShellBrowserHostTestAccess::HasListSpacer(host) == spacer, "26-px list / details spacer only in List and Details" + tag);
            if (HWND list = ShellBrowserHostTestAccess::ListWindow(host); list && spacer) {
                int w = 0, h = 0; ImageList_GetIconSize(ListView_GetImageList(list, LVSIL_SMALL), &w, &h);
                check(h >= window.DpiScale(26), "List / Details keep 26 logical px rows after an in-place switch" + tag);
            }
        }
        std::cout << "view switch totals: " << _countof(sequence) << " switches, sync " << totalMs << " ms, full-list paints "
            << totalPaints << " (max " << maxPaints << ")\n";
        check(totalPaints <= int(_countof(sequence)) * 2, "view switches repaint the whole list at most twice on average");
        // List <-> Details share the spacer: no image-list swap either way.
        window.SetViewMode(Mode::List); pump(300);
        auto before = ShellBrowserHostTestAccess::Counters(host);
        window.SetViewMode(Mode::Details); pump(300);
        window.SetViewMode(Mode::List); pump(300);
        auto after = ShellBrowserHostTestAccess::Counters(host);
        check(after.spacerSwaps == before.spacerSwaps, "List <-> Details keeps the 26-px spacer (no image-list swap)");
        // Re-applying an unchanged mode touches nothing.
        for (Mode mode : { Mode::LargeIcons, Mode::Details }) {
            window.SetViewMode(mode); pump(300);
            before = ShellBrowserHostTestAccess::Counters(host);
            window.SetViewMode(mode); pump(300);
            after = ShellBrowserHostTestAccess::Counters(host);
            check(after.modeSets == before.modeSets && after.iconSpacingSets == before.iconSpacingSets
                && after.spacerSwaps == before.spacerSwaps && after.columnSets == before.columnSets
                && after.sortSets == before.sortSets && after.groupSets == before.groupSets
                && after.redrawBatches == before.redrawBatches && after.refreshes == before.refreshes,
                mode == Mode::Details ? "unchanged Details re-apply skips columns / spacer / sort / group"
                                      : "unchanged large-icon re-apply skips icon spacing / sort / group");
        }
        // Large <-> extra large changes the slot, so the spacing is applied exactly once.
        before = ShellBrowserHostTestAccess::Counters(host);
        window.SetViewMode(Mode::ExtraLargeIcons); pump(300);
        after = ShellBrowserHostTestAccess::Counters(host);
        check(after.iconSpacingSets - before.iconSpacingSets == 1, "changed icon slot applies icon spacing exactly once");
        // A selection survives the switch (no re-enumeration).
        if (IFolderView2* view = ShellBrowserHostTestAccess::View(host)) {
            view->SelectItem(3, SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_FOCUSED); view->Release();
        }
        pump(200);
        window.SetViewMode(Mode::Details); pump(300);
        if (IFolderView2* view = ShellBrowserHostTestAccess::View(host)) {
            int selected = -1; view->GetSelectedItem(-1, &selected); view->Release();
            check(selected == 3, "selection is kept across a view switch");
        }
        window.m_settings.rememberViews = remember;
        if (!wasShown) { ShowWindow(window.m_hWnd, SW_HIDE); pump(100); }
        return failures;
    }

    // Large / extra-large thumbnails never extract on the UI thread: a cache miss paints the
    // Shell icon and queues the item; the worker's result repaints only that cell; stale
    // results are dropped; the memory cache (LRU) survives mode switches, refreshes and
    // folder changes; association badges are resolved once per extension.
    static int CheckAsyncThumbs(CMainWnd& window, const std::wstring& parent) {
        int failures = 0;
        auto check = [&](bool ok, const std::string& name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
        auto pump = [&](DWORD ms) {
            const DWORD until = GetTickCount() + ms;
            do {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message != WM_QUIT && !CPaintManagerUI::TranslateMessage(&message)) {
                        ::TranslateMessage(&message); DispatchMessageW(&message);
                    }
                }
                Sleep(5);
            } while (GetTickCount() < until);
        };
        using Access = ShellBrowserHostTestAccess;
        using Mode = CMainWnd::ViewMode;
        check(Access::LruEviction(), "thumbnail memory cache evicts least recently used entries (no clear-all)");
        const std::wstring folder = parent + L"\\AsyncThumbs", other = parent + L"\\AsyncThumbsOther";
        CreateDirectoryW(folder.c_str(), nullptr); CreateDirectoryW(other.c_str(), nullptr);
        CreateDirectoryW((folder + L"\\子目录").c_str(), nullptr);
        CLSID encoder{}; check(window.GetPngEncoderClsid(&encoder), "fixture PNG encoder available");
        for (int i = 0; i < 18; ++i) {
            Gdiplus::Bitmap bitmap(200, 150, PixelFormat32bppARGB);
            Gdiplus::Graphics graphics(&bitmap);
            graphics.Clear(Gdiplus::Color(255, BYTE(30 + i * 11), BYTE(200 - i * 7), BYTE(90 + i * 5)));
            wchar_t name[32]; swprintf_s(name, L"\\图片%02d.png", i);
            bitmap.Save((folder + name).c_str(), &encoder, nullptr);
        }
        HANDLE text = CreateFileW((folder + L"\\说明.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (text != INVALID_HANDLE_VALUE) { DWORD written = 0; WriteFile(text, "x", 1, &written, nullptr); CloseHandle(text); }
        const bool wasShown = IsWindowVisible(window.m_hWnd) != FALSE;
        if (!wasShown) ShowWindow(window.m_hWnd, SW_SHOWNOACTIVATE);
        window.m_PaintManager.GetRoot()->SetPos({0, 0, 1180, 740}, false);
        window.SyncLayoutDependents();
        window.SetViewMode(Mode::Details);
        window.NavigateToNow(folder, false); pump(700);
        ShellBrowserHost& host = *window.m_shellBrowser;
        Access::ClearThumbs(host); // cold memory cache for this fixture
        // 1) First visit: placeholders on paint, async extraction, per-cell invalidation.
        auto c0 = Access::Counters(host);
        window.SetViewMode(Mode::LargeIcons);
        auto c1 = Access::Counters(host);
        check(c1.syncExtractions == c0.syncExtractions, "switching to large icons extracts no thumbnail on the UI thread");
        check(Access::SettleThumbs(host), "queued thumbnails are delivered by the worker");
        pump(200);
        auto c2 = Access::Counters(host);
        std::cout << "async thumbs first visit: placeholders " << c2.placeholders - c0.placeholders
                  << ", requests " << c2.thumbRequests - c0.thumbRequests << ", coalesced " << c2.coalesced - c0.coalesced
                  << ", extractions " << c2.thumbExtractions - c0.thumbExtractions << ", cell invalidations "
                  << c2.itemInvalidations - c0.itemInvalidations << ", list paints " << c2.listPaints - c0.listPaints
                  << " (full " << c2.fullPaints - c0.fullPaints << "), badge lookups " << c2.badgeResolves - c0.badgeResolves << '\n';
        check(c2.syncExtractions == c0.syncExtractions, "large-icon paint path never extracts thumbnails synchronously");
        check(c2.placeholders - c0.placeholders >= 10, "uncached cells paint a placeholder icon first");
        check(c2.thumbRequests - c0.thumbRequests >= 10, "uncached cells queue an async thumbnail request");
        check(c2.thumbExtractions - c0.thumbExtractions == c2.thumbRequests - c0.thumbRequests,
            "each queued item is extracted once (duplicate requests coalesced)");
        check(c2.itemInvalidations - c0.itemInvalidations >= 10, "each arriving thumbnail invalidates its own cell");
        check(c2.listPaints - c0.listPaints > c2.fullPaints - c0.fullPaints, "arriving thumbnails repaint partial regions, not the whole list");
        check(c2.badgeResolves - c0.badgeResolves <= 3, "association badges resolve once per extension (png / txt / folder)");
        // Exact cell: a delivered result for item i invalidates only that item's cell.
        HWND list = Access::ListWindow(host);
        int count = 0;
        if (IFolderView2* view = Access::View(host)) { view->ItemCount(SVGIO_ALLVIEW, &count); view->Release(); }
        const int probe = (std::min)(4, count - 1);
        std::wstring path;
        std::wstring key = Access::Key(host, probe, &path);
        check(!key.empty() && Access::HasThumb(host, key), "delivered thumbnail is cached under item + size + stamp");
        if (list && probe >= 0) {
            UpdateWindow(list); ValidateRect(list, nullptr);
            const auto before = Access::Counters(host);
            Access::Deliver(host, probe, path, key, Access::ThumbGeneration(host));
            RECT update{}, cell{}, client{}, expected{};
            const BOOL dirty = GetUpdateRect(list, &update, FALSE);
            Access::IconCell(host, probe, cell); GetClientRect(list, &client); IntersectRect(&expected, &cell, &client);
            check(dirty && EqualRect(&update, &expected) && Access::Counters(host).itemInvalidations == before.itemInvalidations + 1,
                "async completion invalidates exactly the item's cell");
            // Stale (old generation) results are dropped: not cached, nothing repainted.
            UpdateWindow(list); ValidateRect(list, nullptr);
            const std::wstring staleKey = key + L"|stale";
            Access::Deliver(host, probe, path, staleKey, Access::ThumbGeneration(host) - 1);
            check(!Access::HasThumb(host, staleKey) && !GetUpdateRect(list, &update, FALSE)
                && Access::Counters(host).staleDropped == before.staleDropped + 1,
                "stale thumbnail results (old folder / mode) are dropped");
        }
        // A cancelled (folder / mode change) request never lands in the cache.
        {
            Access::ClearThumbs(host);
            const auto before = Access::Counters(host);
            const std::wstring cancelledKey = Access::Key(host, probe);
            Access::Request(host, probe);
            Access::Cancel(host);
            pump(400);
            const auto after = Access::Counters(host);
            check(after.thumbRequests == before.thumbRequests + 1 && after.staleDropped >= before.staleDropped + 1
                && !Access::HasThumb(host, cancelledKey) && Access::PendingThumbs(host) == 0,
                "requests cancelled by a folder / mode change are dropped");
            InvalidateRect(list, nullptr, FALSE);
            Access::SettleThumbs(host); pump(200);
        }
        // Direct paint of an uncached cell: placeholder + request, no extraction.
        if (list && probe >= 0) {
            Access::ClearThumbs(host);
            const auto before = Access::Counters(host);
            RECT client{}; GetClientRect(list, &client);
            HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
            HBITMAP canvas = CreateCompatibleBitmap(screen, client.right, client.bottom);
            auto old = SelectObject(dc, canvas);
            FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            Access::DrawItem(host, dc, probe);
            RECT cell{}; Access::IconCell(host, probe, cell);
            int inked = 0;
            for (int y = cell.top; y < cell.bottom - (cell.bottom - cell.top) / 4; ++y)
                for (int x = cell.left; x < cell.right; ++x) {
                    const COLORREF color = GetPixel(dc, x, y);
                    inked += color != CLR_INVALID && color != RGB(255, 255, 255);
                }
            SelectObject(dc, old); DeleteObject(canvas); DeleteDC(dc); ReleaseDC(nullptr, screen);
            const auto after = Access::Counters(host);
            check(after.placeholders == before.placeholders + 1 && after.thumbRequests == before.thumbRequests + 1
                && after.syncExtractions == before.syncExtractions && Access::PendingThumbs(host) == 1,
                "custom draw of an uncached cell draws a placeholder and queues the item");
            check(inked > 400, "the placeholder cell shows the Shell icon (not an empty slot)");
            InvalidateRect(list, nullptr, FALSE);
            Access::SettleThumbs(host); pump(200);
        }
        // 2) Second visits: mode switch, other size, refresh, other folder and back -> 0 extractions.
        auto visit = [&](const char* name, const std::function<void()>& action) {
            const auto before = Access::Counters(host);
            action();
            Access::SettleThumbs(host); pump(250);
            const auto after = Access::Counters(host);
            std::cout << "async thumbs " << name << ": extractions " << after.thumbExtractions - before.thumbExtractions
                      << ", placeholders " << after.placeholders - before.placeholders << ", hits " << after.thumbHits - before.thumbHits
                      << ", badge lookups " << after.badgeResolves - before.badgeResolves << '\n';
            return std::make_pair(before, after);
        };
        window.SetViewMode(Mode::ExtraLargeIcons); Access::SettleThumbs(host); pump(300);   // fill the other size
        window.SetViewMode(Mode::LargeIcons); Access::SettleThumbs(host); pump(300);
        auto r = visit("large -> details -> large", [&] { window.SetViewMode(Mode::Details); pump(300); window.SetViewMode(Mode::LargeIcons); });
        check(r.second.thumbExtractions == r.first.thumbExtractions && r.second.placeholders == r.first.placeholders
            && r.second.thumbHits > r.first.thumbHits, "second visit of large icons draws from cache (0 extractions)");
        check(r.second.badgeResolves == r.first.badgeResolves, "badge cache survives view switches");
        r = visit("large -> extra large", [&] { window.SetViewMode(Mode::ExtraLargeIcons); });
        check(r.second.thumbExtractions == r.first.thumbExtractions && r.second.placeholders == r.first.placeholders,
            "switching between large and extra large keeps both sizes cached (0 extractions)");
        r = visit("refresh", [&] { host.Refresh(); pump(500); });
        check(r.second.thumbExtractions == r.first.thumbExtractions, "refresh keeps unchanged thumbnails cached (0 extractions)");
        const auto keysBefore = Access::CachedKeys(host);
        r = visit("other folder and back", [&] {
            window.NavigateToNow(other, false); pump(500); window.NavigateToNow(folder, false); pump(500);
        });
        // Navigation keeps the memory cache: everything cached before is still there and the
        // returning paint draws from it (at most a cell or two seen for the first time).
        bool kept = true;
        for (const auto& key : keysBefore) kept = kept && Access::HasThumb(host, key);
        check(kept && r.second.thumbHits > r.first.thumbHits && r.second.thumbExtractions - r.first.thumbExtractions <= 2,
            "returning to a folder draws its thumbnails from cache");
        // An edited file gets a fresh thumbnail (size / mtime are part of the key).
        {
            Gdiplus::Bitmap bitmap(120, 240, PixelFormat32bppARGB);
            Gdiplus::Graphics graphics(&bitmap); graphics.Clear(Gdiplus::Color(255, 10, 10, 10));
            bitmap.Save((folder + L"\\图片00.png").c_str(), &encoder, nullptr);
        }
        r = visit("edited file + refresh", [&] { host.Refresh(); pump(500); });
        check(r.second.thumbExtractions - r.first.thumbExtractions >= 1 && r.second.thumbExtractions - r.first.thumbExtractions <= 3,
            "an edited file is re-extracted, the others stay cached");
        check(Access::Counters(host).syncExtractions == c0.syncExtractions, "no UI-thread extraction anywhere in the async thumbnail run");
        window.SetViewMode(Mode::Details); pump(200);
        if (!wasShown) { ShowWindow(window.m_hWnd, SW_HIDE); pump(100); }
        return failures;
    }

    static const wchar_t* IconCacheVersion() { return CMainWnd::kIconCacheVersion; }
    // The PNG icon cache persists (no wipe at startup), lives in an isolated folder for
    // tests, and is invalidated by version directory / stamped names / trimming instead.
    static int CheckIconCache(CMainWnd& window, const std::wstring& root, const std::wstring& marker,
        size_t userFilesBefore, ULONGLONG userNewestBefore) {
        int failures = 0;
        auto check = [&](bool ok, const char* name) { if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; } };
        const std::wstring isolated = root + L"\\IconCache\\";
        check(_wcsnicmp(window.m_iconCacheDir.c_str(), isolated.c_str(), isolated.size()) == 0,
            "tests use the isolated icon cache dir (FASTFILE_ICON_CACHE_DIR)");
        check(PathFileExistsW((window.m_iconCacheDir + marker).c_str()) != FALSE,
            "icon cache entries survive main window creation (no wipe on startup)");
        size_t userFiles = 0; ULONGLONG userNewest = 0;
        UserIconCacheSnapshot(userFiles, userNewest);
        check(userFiles == userFilesBefore && userNewest == userNewestBefore,
            "the user's real %TEMP%\\FastFileIconCache is untouched by the test run");
        // Stamped names: same file -> same name; edited file -> new name; roots are session files.
        const std::wstring sample = root + L"\\stamp.txt";
        HANDLE file = CreateFileW(sample.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD written = 0; WriteFile(file, "a", 1, &written, nullptr);
        FILETIME old{}; SYSTEMTIME st{}; GetSystemTime(&st); st.wYear -= 1; SystemTimeToFileTime(&st, &old);
        SetFileTime(file, nullptr, nullptr, &old); CloseHandle(file);
        const std::wstring first = window.IconCacheLeaf(sample + L"@32", sample, L"f_32.png");
        check(first == window.IconCacheLeaf(sample + L"@32", sample, L"f_32.png"), "icon cache name is stable for an unchanged file");
        file = CreateFileW(sample.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        SetFilePointer(file, 0, nullptr, FILE_END); WriteFile(file, "b", 1, &written, nullptr); CloseHandle(file);
        check(first != window.IconCacheLeaf(sample + L"@32", sample, L"f_32.png"), "an edited file gets a new icon cache name");
        check(window.IconCacheLeaf(L"C:\\@32", L"C:\\", L"d_32.png").rfind(L"s_", 0) == 0, "drive roots are session-only cache entries");
        // Maintenance: legacy flat files, other versions, stale session files and aged entries go;
        // fresh entries stay; the size cap removes the oldest first.
        const std::wstring trim = root + L"\\TrimCache";
        CreateDirectoryW(trim.c_str(), nullptr);
        CreateDirectoryW((trim + L"\\v9").c_str(), nullptr);
        CreateDirectoryW((trim + L"\\v8").c_str(), nullptr);
        FILETIME nowFt{}; GetSystemTimeAsFileTime(&nowFt);
        const ULONGLONG now = (ULONGLONG(nowFt.dwHighDateTime) << 32) | nowFt.dwLowDateTime;
        const ULONGLONG day = 24ull * 3600ull * 10000000ull;
        auto make = [&](const std::wstring& path, ULONGLONG time, DWORD bytes) {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            std::string data(bytes, 'x'); DWORD w = 0; WriteFile(h, data.data(), bytes, &w, nullptr);
            FILETIME ft{ DWORD(time), DWORD(time >> 32) }; SetFileTime(h, nullptr, nullptr, &ft); CloseHandle(h);
        };
        make(trim + L"\\00000000_f_32_v8.png", now - day, 10);
        make(trim + L"\\v8\\old.png", now - day, 10);
        make(trim + L"\\v9\\fresh.png", now - day, 10);
        make(trim + L"\\v9\\aged.png", now - 40 * day, 10);
        make(trim + L"\\v9\\s_prev_preview_1.png", now - day / 24, 10);
        make(trim + L"\\v9\\s_live_preview_2.png", now + day / 24, 10);
        auto result = CMainWnd::MaintainIconCache(trim, L"v9", now, 30, 0);
        auto exists = [&](const wchar_t* leaf) { return PathFileExistsW((trim + leaf).c_str()) != FALSE; };
        check(!exists(L"\\00000000_f_32_v8.png") && result.removedLegacy == 1, "maintenance removes legacy flat cache files");
        check(!exists(L"\\v8") && result.removedVersionDirs == 1, "maintenance removes other cache versions");
        check(exists(L"\\v9\\fresh.png"), "maintenance keeps fresh entries");
        check(!exists(L"\\v9\\aged.png") && result.removedAged == 1, "maintenance removes entries past the age cap");
        check(!exists(L"\\v9\\s_prev_preview_1.png") && exists(L"\\v9\\s_live_preview_2.png"),
            "maintenance removes previous sessions' scratch files only");
        for (int i = 0; i < 4; ++i) make(trim + L"\\v9\\cap" + std::to_wstring(i) + L".png", now - (10 - i) * day, 1000);
        result = CMainWnd::MaintainIconCache(trim, L"v9", now, 30, 2500);
        check(!exists(L"\\v9\\cap0.png") && exists(L"\\v9\\cap3.png") && result.keptBytes <= 2500 / 4 * 3,
            "size cap trims oldest entries first");
        return failures;
    }
    static int Run(CMainWnd& window, const std::wstring& fixture) {
        int failures=CheckUiMetrics(window);
        auto check = [&](bool result, const char* name) {
            if (!result) { std::cerr << "FAIL " << name << '\n'; ++failures; }
        };
        auto pump = [&]() {
            const DWORD until = GetTickCount() + 700;
            while (GetTickCount() < until) {
                MSG message;
                while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message == WM_QUIT) continue;
                    TranslateMessage(&message); DispatchMessage(&message);
                }
                Sleep(5);
            }
            window.m_PaintManager.GetRoot()->SetPos({0, 0, 1180, 740}, false);
            window.SyncLayoutDependents();
        };
        auto text = [&](LPCTSTR name) {
            auto* control = window.m_PaintManager.FindControl(name);
            return control ? std::wstring(control->GetText().GetData()) : std::wstring();
        };
        window.NavigateToNow(fixture, false);
        pump();
        check(text(L"preview_size") == L"2 个文件夹 · 1 个文件", "current folder counts must come from actual children");
        check(text(L"preview_location") == fixture, "current folder location");
        window.m_PaintManager.SetFocus(window.m_pSearchEdit);
        const HWND searchEdit = window.m_pSearchEdit->GetNativeEditHWND();
        check(searchEdit != nullptr, "search accepts keyboard focus through its native edit");
        if (searchEdit) SetWindowTextW(searchEdit, L"Battle");
        pump();
        check(window.m_searchFilter == L"Battle", "typing in native search edit notifies the filter automatically");
        check(!window.m_shellBrowser->IsVisible() && window.m_listingDirs.size() == 1 &&
            window.m_listingDirs.front().name == L"Battle.net" && window.m_listingFiles.empty(),
            "search displays only matching entries instead of silently ignoring Shell filter");
        check(!ShellBrowserHostTestAccess::ContainerVisible(*window.m_shellBrowser),
            "native browser container must actually hide so it cannot cover search results");
        window.m_pSearchEdit->SetText(L"does-not-exist"); window.ApplySearchFilter(); pump();
        check(window.m_listingDirs.empty() && window.m_listingFiles.empty(), "search with no matches is empty");
        window.m_PaintManager.SetFocus(nullptr);
        window.ClearSearchFilter(); pump();
        check(window.m_shellBrowser->IsVisible(), "clearing search restores native Shell view");
        check(ShellBrowserHostTestAccess::ContainerVisible(*window.m_shellBrowser), "clearing search actually shows the browser window");
        for (bool hidden : {true, false}) {
            window.SetShowHidden(hidden); pump();
            IFolderView2* view = ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
            int itemCount = 0; view->ItemCount(SVGIO_ALLVIEW, &itemCount); view->Release();
            if (itemCount != (hidden ? 4 : 3)) std::printf("hidden=%d count=%d\n", hidden, itemCount);
            check(itemCount == (hidden ? 4 : 3), "hidden-items menu changes native enumeration in both directions");
        }
        auto* addressBar = static_cast<CContainerUI*>(window.m_PaintManager.FindControl(L"address_bar"));
        for (LPCTSTR name : {L"path_host", L"search_box"}) {
            auto* field = static_cast<CContainerUI*>(window.m_PaintManager.FindControl(name));
            const RECT bounds = field->GetPos(), row = addressBar->GetPos(), inset = field->GetInset();
            check(bounds.top > row.top && bounds.bottom < row.bottom &&
                bounds.bottom - bounds.top == window.DpiScale(32) &&
                field->GetBorderRound().cx == window.DpiScale(4) && inset.left >= window.DpiScale(10),
                "rounded address/search fields are 32 logical with radius 4 and inner text margins");
        }
        {
            auto* search = window.m_PaintManager.FindControl(L"search_box");
            const int rowW = int(addressBar->GetWidth());
            check(search->GetFixedWidth() == UiTokens::SearchBoxWidthFor(rowW, window.DpiScale(UiTokens::SearchBoxMinW),
                window.DpiScale(UiTokens::SearchBoxMaxW)), "search width follows clamp(240, 30 percent of row, 435)");
        }
        auto* node = window.FindTreeNodeByPath(nullptr, fixture);
        check(node && node->IsVisible() && node->IsSelected(), "navigate must expand and select nested tree target");
        window.m_PaintManager.GetRoot()->SetPos({0, 0, 1180, 740}, false);
        if (node) {
            RECT viewBounds = window.m_pDirTree->GetList()->GetPos(), nodeBounds = node->GetPos();
            check(nodeBounds.top >= viewBounds.top && nodeBounds.bottom <= viewBounds.bottom, "nested tree target must actually be inside viewport after reveal");
        }
        for (LPCTSTR button : {L"btn_cut", L"btn_copy", L"btn_rename", L"btn_delete", L"btn_share"})
            check(!window.m_PaintManager.FindControl(button)->IsEnabled(), "empty Shell selection must dim command buttons");

        for (auto mode : { CMainWnd::ViewMode::Details, CMainWnd::ViewMode::LargeIcons, CMainWnd::ViewMode::Tiles }) {
            window.SetViewMode(mode); pump();
            IFolderView2* view = ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
            DWORD flags = 0; view->GetCurrentFolderFlags(&flags); view->Release();
            check(bool(flags & FWF_NOHEADERINALLVIEWS) == (mode != CMainWnd::ViewMode::Details), "main window modes must synchronize native header");
        }
        IFolderView2* view = ShellBrowserHostTestAccess::View(*window.m_shellBrowser);
        int count = 0; view->ItemCount(SVGIO_ALLVIEW, &count);
        int battle = -1;
        for (int i = 0; i < count; ++i) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(view->GetItem(i, IID_PPV_ARGS(&item)))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    if (std::wstring(path) == fixture + L"\\Battle.net") battle = i;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        check(battle >= 0, "Shell enumerates Battle.net fixture");
        if (battle >= 0) {
            view->SelectItem(battle, SVSI_SELECT | SVSI_DESELECTOTHERS);
            pump();
            check(text(L"preview_title") == L"Battle.net", "Shell selection event must replace parent folder detail title");
            check(text(L"preview_location") == fixture + L"\\Battle.net", "selected folder retains its own location");
            check(window.m_PaintManager.FindControl(L"btn_rename")->IsEnabled(), "single native selection enables rename");
            IDataObject* previousClipboard = SnapshotClipboard();
            OleSetClipboard(nullptr);
            IShellView* shellView = nullptr;
            HWND shellWindow = nullptr;
            if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&shellView)))) {
                shellView->GetWindow(&shellWindow);
                shellView->Release();
            }
            BYTE previousKeys[256]{}, shortcutKeys[256]{};
            GetKeyboardState(previousKeys);
            shortcutKeys[VK_CONTROL] = 0x80;
            SetKeyboardState(shortcutKeys);
            MSG shortcut{}; shortcut.hwnd = shellWindow; shortcut.message = WM_KEYDOWN;
            shortcut.wParam = 'C'; shortcut.lParam = 1;
            check(CPaintManagerUI::TranslateMessage(&shortcut), "native child Ctrl C is handled before dispatch");
            SetKeyboardState(previousKeys);
            // The Windows view runs its copy verb asynchronously; wait for the clipboard.
            bool copiedSelection = false;
            const DWORD copyDeadline = GetTickCount() + 3000;
            while (!copiedSelection && GetTickCount() < copyDeadline) {
                pump();
                if (IsClipboardFormatAvailable(CF_HDROP) && OpenClipboard(window.m_hWnd)) {
                    HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
                    wchar_t copiedPath[32768]{};
                    if (drop && DragQueryFileW(drop, 0, copiedPath, _countof(copiedPath)))
                        copiedSelection = std::wstring(copiedPath) == fixture + L"\\Battle.net";
                    CloseClipboard();
                }
                if (!copiedSelection) Sleep(20);
            }
            check(copiedSelection, "native Ctrl C publishes the selected path to the Windows clipboard");
            if(!copiedSelection) {
                std::vector<CMainWnd::ClipboardItem> debugSelection;window.CollectSelectedItems(debugSelection);
                std::wcerr<<L"Clipboard selection count "<<debugSelection.size()<<L", tree focus "<<window.IsTreeKeyboardFocus()<<L"\n";
                for(const auto& item:debugSelection)std::wcerr<<L"Clipboard selection: "<<item.path<<L"\n";
            }
            OleSetClipboard(previousClipboard);
            if (previousClipboard) { OleFlushClipboard(); previousClipboard->Release(); }
            view->SelectItem(battle, SVSI_DESELECT); pump();
            check(text(L"preview_title") != L"Battle.net", "deselection restores current folder detail");
        }
        view->Release();

        // Make the fixture a quick target; only its quick row may own the highlight.
        CMainWnd::QuickRow quickFixture;
        quickFixture.path = fixture; quickFixture.label = L"测试目录"; quickFixture.pinned = true;
        window.m_quickRows.push_back(quickFixture);
        window.RebuildLeftQuickRows();
        window.SyncTreeToPath(fixture);
        window.UpdateQuickRowHighlight(); pump();
        check(node && !node->IsSelected(), "quick target suppresses duplicate tree selection");
        int selectedQuick = 0;
        for (int i = 0; i < window.m_pLeftQuickRows->GetCount(); ++i)
            selectedQuick += window.m_pLeftQuickRows->GetItemAt(i)->GetBkColor() == 0xffe8e8e8;
        check(selectedQuick == 1, "exactly one quick row owns the gray selection");
        const auto pictures = window.GetKnownFolderPath(CSIDL_MYPICTURES);
        check(window.TabTitleForPath(pictures) == window.GetShellDisplayName(pictures), "tab uses localized Shell display name");
        check(std::wstring(UiTokens::ColorNavSelected) == L"#FFE8E8E8", "navigation selected color remains neutral gray");
        // Explicitly establish another tab outside the current directory, then
        // navigate it to a child to exercise existing-target priority below.
        const auto otherFolder = window.ParentPath(fixture) + L"\\Other";
        CreateDirectoryW(otherFolder.c_str(), nullptr);
        window.AddTab(otherFolder, true);
        pump();
        window.NavigateToNow(fixture + L"\\Battle.net", false);
        pump();
        const auto settledTabs = DuiLib::TabStripRegressionAccess::LayoutSnapshot(*window.m_pTabStrip);
        for (int index : {0, 1, 0, 1}) {
            window.OnTabStripSelect(index);
            check(DuiLib::TabStripRegressionAccess::LayoutSnapshot(*window.m_pTabStrip) == settledTabs,
                "clicking existing tabs immediately preserves geometry and scroll position");
            pump();
        }
        int argc = 0;
        auto argv = CommandLineToArgvW(L"FastFile.exe --open \"C:\\\"", &argc);
        check(argv && argc == 3 && window.ResolveFolderOpenTarget(argv[2]) == L"C:\\",
            "legacy quoted drive-root command must resolve to root, not drive current directory");
        LocalFree(argv);
        check(window.NormalizePath(L"C:") == L"C:\\", "bare drive means drive root in folder navigation");
        const auto tabCount = window.m_tabs.size();
        const auto existingPath = window.m_currentPath;
        window.OnNewTabRequested();
        check(window.m_tabs.size() == tabCount && window.m_currentPath == existingPath,
            "plus and Ctrl T reuse an already open target folder");
        window.OpenExternalPaths({fixture + L"\\", fixture + L"\\Battle.net"}, false);
        const ULONGLONG resolvedDeadline=GetTickCount64()+5000;
        while(window.m_externalOpensPending && GetTickCount64()<resolvedDeadline)pump();
        check(window.m_externalOpensPending==0,"external repeated paths finish resolving before the next navigation");
        check(window.m_tabs.size() == tabCount && window.m_currentPath == fixture + L"\\Battle.net",
            "external repeated paths reuse existing tabs");
        window.NavigateToNow(fixture, true);
        check(window.m_tabs.size() == tabCount && window.m_tabs[window.m_activeTab].path == fixture,
            "navigation to an open folder activates its tab instead of duplicating its path");
        pump();
        window.OnShellBrowserNavigation(fixture + L"\\Battle.net");
        check(window.m_currentPath==fixture,"an obsolete queued Shell completion cannot switch the active tab");
        check(window.m_shellBrowser->Navigate(fixture + L"\\Battle.net"),"native Shell navigation fixture starts successfully");
        pump();
        check(window.m_tabs.size() == tabCount && window.m_currentPath == fixture + L"\\Battle.net",
            "native Shell navigation to an open folder reuses its tab");
        pump();
        const auto child = fixture + L"\\Battle.net\\Nested";
        const auto deep = child + L"\\Deep";
        CreateDirectoryW(child.c_str(), nullptr);
        CreateDirectoryW(deep.c_str(), nullptr);
        window.NavigateToNow(fixture, true); pump();
        window.AddTab(fixture + L"\\Battle.net", true); pump();
        check(window.m_activeTab == 1 && window.m_tabs.size() == tabCount &&
            window.m_tabs[0].path == fixture,
            "an already open child activates its tab and preserves the parent tab");
        const auto reusedIndex = window.m_activeTab;
        window.AddTab(child + L"\\", true); pump();
        check(window.m_tabs.size() == tabCount && window.m_activeTab == reusedIndex &&
            window.m_currentPath == child,
            "opening a new child directory reuses the current tab");
        window.AddTab(deep, true); pump();
        check(window.m_tabs.size() == tabCount && window.m_activeTab == reusedIndex &&
            window.m_currentPath == deep,
            "internal descendant navigation does not accumulate tabs");
        check(!window.m_tabs[window.m_activeTab].backStack.empty() &&
            window.m_tabs[window.m_activeTab].backStack.back() == child,
            "descendant reuse preserves the same tab navigation history");
        window.GoBack(); pump();
        check(window.m_currentPath == child && window.m_tabs.size() == tabCount,
            "back returns to the parent without creating a tab");
        window.OpenExternalPaths({deep}, false); pump();
        check(window.m_tabs.size()==tabCount+1 && window.m_activeTab!=reusedIndex && window.m_currentPath==deep,
            "external new folder opens in a separate tab according to preferences");
        window.OpenExternalPaths({deep}, false);pump();
        check(window.m_tabs.size()==tabCount+1,
            "external repeated folder reuses its existing tab when enabled");
        window.AddTab(fixture, true); pump();
        check(window.m_activeTab != reusedIndex && window.m_tabs.size() == tabCount+1 &&
            window.m_currentPath == fixture,
            "an existing target tab still takes priority over reuse");
        const auto siblingPrefix = fixture + L"-Other";
        CreateDirectoryW(siblingPrefix.c_str(), nullptr);
        window.AddTab(siblingPrefix, true); pump();
        check(window.m_tabs.size() == tabCount + 2 && window.m_currentPath == siblingPrefix,
            "a sibling sharing the parent name prefix is not a descendant");
        check(DuiLib::TabStripRegressionAccess::Check(*window.m_pTabStrip), "fixed-width tabs truncate long titles and preserve DPI, tooltip, close and overflow behavior");
        failures += CheckSelectionLatency(window, fixture);
        failures += CheckShellMenus(window, fixture);
        failures += CheckFileOperationEngine(window.ParentPath(fixture));
        failures += CheckHandlers(window);
        failures += CheckShortcuts(window, window.ParentPath(fixture));
        failures += CheckUiPolish(window, window.ParentPath(fixture));
        return failures;
    }
};

static LONG WINAPI ReportCrash(EXCEPTION_POINTERS* info) {
    // Print the faulting module offset so a crash in CI is diagnosable without a debugger.
    HMODULE module = nullptr; wchar_t name[MAX_PATH]{};
    void* address = info->ExceptionRecord->ExceptionAddress;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        static_cast<LPCWSTR>(address), &module);
    if (module) GetModuleFileNameW(module, name, MAX_PATH);
    fprintf(stderr, "CRASH code=0x%08lX module=%ls rva=0x%llX\n", info->ExceptionRecord->ExceptionCode, name,
        static_cast<unsigned long long>(reinterpret_cast<const char*>(address) - reinterpret_cast<const char*>(module)));
    CONTEXT context = *info->ContextRecord;
    for (int frame = 0; frame < 24 && context.Rip; ++frame) {
        HMODULE frameModule = nullptr; wchar_t frameName[MAX_PATH]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(context.Rip), &frameModule);
        if (frameModule) GetModuleFileNameW(frameModule, frameName, MAX_PATH);
        const wchar_t* shortName = wcsrchr(frameName, L'\\');
        fprintf(stderr, "  #%d %ls+0x%llX\n", frame, shortName ? shortName + 1 : frameName,
            static_cast<unsigned long long>(context.Rip - reinterpret_cast<DWORD64>(frameModule)));
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (!function) { context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp); context.Rsp += 8; continue; }
        void* handlerData = nullptr; DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
    }
    fprintf(stderr, "  thread=%lu\n", GetCurrentThreadId());
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

std::atomic<int> MainWndRegressionAccess::slowProbeCalls{0};
std::wstring MainWndRegressionAccess::slowRoot;

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    wchar_t diagnosticDpi[2]{};
    if(GetEnvironmentVariableW(L"FASTFILE_MENU_DPI_AWARE",diagnosticDpi,_countof(diagnosticDpi))) EnablePerMonitorDpiAwareness();
    // Only the Shell activation test registers windows in the user's Shell window list.
    CMainWnd::s_shellWindowRegistrationAllowed = false;
    SetUnhandledExceptionFilter(ReportCrash);
    wchar_t trace[2]{};
    if(GetEnvironmentVariableW(L"FASTFILE_TEST_EXCEPTION_TRACE",trace,_countof(trace)))
        AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* info)->LONG {
            if(info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) ReportCrash(info);
            return EXCEPTION_CONTINUE_SEARCH;
        });
    if(argc>1 && (strcmp(argv[1],"--shell-activation-launcher")==0 || strcmp(argv[1],"--agent-lifecycle-launcher")==0 || strcmp(argv[1],"--agent-ui-launcher")==0)) {
        wchar_t desktopName[256]{},hiveName[256]{};
        GetEnvironmentVariableW(L"FASTFILE_TEST_DESKTOP",desktopName,_countof(desktopName));
        GetEnvironmentVariableW(L"FASTFILE_TEST_HIVE",hiveName,_countof(hiveName));
        HDESK desktop=OpenDesktopW(desktopName,0,FALSE,GENERIC_ALL);
        HKEY user=nullptr,classes=nullptr;
        if(!desktop || (strcmp(argv[1],"--agent-lifecycle-launcher")!=0 && !SetThreadDesktop(desktop))
            || RegOpenKeyExW(HKEY_CURRENT_USER,(std::wstring(hiveName)+L"\\User").c_str(),0,KEY_ALL_ACCESS,&user)!=ERROR_SUCCESS
            || RegOpenKeyExW(HKEY_CURRENT_USER,(std::wstring(hiveName)+L"\\Classes").c_str(),0,KEY_ALL_ACCESS,&classes)!=ERROR_SUCCESS)return 4;
        if(RegOverridePredefKey(HKEY_CURRENT_USER,user)!=ERROR_SUCCESS || RegOverridePredefKey(HKEY_CLASSES_ROOT,classes)!=ERROR_SUCCESS)return 4;
        if(strcmp(argv[1],"--agent-lifecycle-launcher")==0) {
            RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr);
            return MainWndRegressionAccess::RunIsolatedAgent();
        }
        if(strcmp(argv[1],"--agent-ui-launcher")==0)RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr);
        return FastFileActivationEntry(GetModuleHandleW(nullptr),nullptr,nullptr,SW_HIDE);
    }
    const bool activationOnly=argc>1 && strcmp(argv[1],"--shell-activation-only")==0;
    HDESK isolatedDesktop=nullptr;
    {
        const auto name=L"FastFileActivation_"+std::to_wstring(GetCurrentProcessId());
        isolatedDesktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
        if(!isolatedDesktop || !SetThreadDesktop(isolatedDesktop))return 2;
        SetEnvironmentVariableW(L"FASTFILE_TEST_DESKTOP",name.c_str());
        SetEnvironmentVariableW(L"FASTFILE_TEST_HIVE",(L"Software\\"+name).c_str());
    }
    OleInitialize(nullptr);
    CPaintManagerUI::SetInstance(GetModuleHandle(nullptr));
    wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
    const std::wstring root = std::wstring(temp) + L"FastFileUIRegression_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(root.c_str(), nullptr);
    SetEnvironmentVariableW(L"APPDATA", root.c_str()); // isolated child-process profile
    if(activationOnly)SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());
    const auto fixture = root + L"\\Program Files";
    CreateDirectoryW(fixture.c_str(), nullptr);
    CreateDirectoryW((fixture + L"\\Battle.net").c_str(), nullptr);
    CreateDirectoryW((fixture + L"\\115Chrome").c_str(), nullptr);
    HANDLE file = CreateFileW((fixture + L"\\sample.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    CloseHandle(file);
    file = CreateFileW((fixture + L"\\hidden.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN, nullptr);
    CloseHandle(file);
    // Isolated icon cache: a test run must never read, write or trim the user's cache.
    // Seed an entry first to prove startup keeps (does not wipe) existing cache files.
    const std::wstring iconCacheRoot = root + L"\\IconCache";
    SetEnvironmentVariableW(L"FASTFILE_ICON_CACHE_DIR", iconCacheRoot.c_str());
    SHCreateDirectoryExW(nullptr, (iconCacheRoot + L"\\" + MainWndRegressionAccess::IconCacheVersion()).c_str(), nullptr);
    const std::wstring cacheMarker = L"0123456789ABCDEF_marker_32.png";
    file = CreateFileW((iconCacheRoot + L"\\" + MainWndRegressionAccess::IconCacheVersion() + L"\\" + cacheMarker).c_str(),
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    CloseHandle(file);
    size_t userCacheFiles = 0; ULONGLONG userCacheNewest = 0;
    UserIconCacheSnapshot(userCacheFiles, userCacheNewest);
    auto* window = new CMainWnd; // same process-lifetime ownership as application main
    MainWndRegressionAccess::ConfigureQuickFixture(*window, fixture);
    HWND hwnd = window->Create(nullptr, L"FastFile regression", UI_WNDSTYLE_FRAME, WS_EX_WINDOWEDGE);
    if (!hwnd) return 1;
    if (!MainWndRegressionAccess::WaitQuickFixture(*window)) { DestroyWindow(hwnd); return 1; }
    const int cacheFailures = MainWndRegressionAccess::CheckIconCache(*window, root, cacheMarker, userCacheFiles, userCacheNewest);
    ShowWindow(hwnd, SW_HIDE);
    int failures = argc>1 && strcmp(argv[1],"--quick-access-only")==0 ? MainWndRegressionAccess::CheckSystemQuickAccess(*window,fixture)
        : argc>1 && strcmp(argv[1],"--agent-live-only")==0 ? MainWndRegressionAccess::CheckExplorerAgentLive(*window,fixture)
        : activationOnly ? MainWndRegressionAccess::CheckShellActivation(*window,fixture)
        : argc>1 && strcmp(argv[1],"--explorer-live-only")==0
        ? MainWndRegressionAccess::CheckExplorerLive(*window,fixture)
        : argc>1 && strcmp(argv[1],"--delete-permission-check")==0
        ? MainWndRegressionAccess::CheckDeletePermissionDialog(*window, root)
        : argc>1 && strcmp(argv[1],"--delete-partial-check")==0
        ? MainWndRegressionAccess::CheckDeletePermissionDialog(*window, root, true)
        : argc>1 && strcmp(argv[1],"--tabs-only")==0
        ? MainWndRegressionAccess::CheckTabs(*window)
        : argc>1 && strcmp(argv[1],"--shell-menus-only")==0
        ? MainWndRegressionAccess::CheckShellMenus(*window,fixture)
        : argc>1 && strcmp(argv[1],"--view-switch-only")==0
        ? MainWndRegressionAccess::CheckViewSwitch(*window, root)
        : argc>1 && strcmp(argv[1],"--thumbs-only")==0
        ? MainWndRegressionAccess::CheckAsyncThumbs(*window, root)
        : argc>1 && strcmp(argv[1],"--ui-polish-only")==0
        ? MainWndRegressionAccess::RunUiPolish(*window, root)
        : argc>1 && strcmp(argv[1],"--shortcuts-only")==0
        ? MainWndRegressionAccess::CheckShortcuts(*window, root)
        : argc>1 && strcmp(argv[1],"--integration-only")==0
        ? MainWndRegressionAccess::CheckIntegration(*window,root)
        : MainWndRegressionAccess::Run(*window, fixture);
    if(!activationOnly && !(argc>1 && (strcmp(argv[1],"--quick-access-only")==0 || strcmp(argv[1],"--delete-permission-check")==0 || strcmp(argv[1],"--delete-partial-check")==0 || strcmp(argv[1],"--integration-only")==0 || strcmp(argv[1],"--explorer-live-only")==0 || strcmp(argv[1],"--agent-live-only")==0)))
        failures+=MainWndRegressionAccess::CheckPreferences(*window,root);
    failures += cacheFailures;
    if(!AppIconMatchesSource()) {++failures;std::cerr<<"FAIL all embedded application icon sizes must match res/FastFile.ico\n";}
    if(IsWindow(hwnd))DestroyWindow(hwnd);
    // This fixture/profile is exclusively created by this test, and never uses user data.
    SHFILEOPSTRUCTW cleanup{};
    std::wstring paths = root + L'\0'; paths += L'\0';
    cleanup.wFunc = FO_DELETE; cleanup.pFrom = paths.c_str();
    cleanup.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&cleanup);
    std::cout << "MainWndRegressionTests: " << failures << " failures\n";
    return failures ? 1 : 0;
}
