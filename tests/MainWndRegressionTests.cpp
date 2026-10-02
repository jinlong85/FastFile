#include "MainWndInternal.h"
#include "FavoriteStarUI.h"
#include "ShellMenuUtil.h"
class FastFileActivationWindow : public CMainWnd {
public:
    HWND Create(HWND parent,LPCTSTR title,DWORD style,DWORD extendedStyle) {
        HWND window=CMainWnd::Create(parent,title,style,extendedStyle);
        if(window)SetPropW(window,L"FastFile.Test.Initialized",reinterpret_cast<HANDLE>(1));
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
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")

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
HWND deleteTestOwner = nullptr;
int deleteTestReply = IDNO;
int deleteDialogCount = 0;
bool createFixtureFolder = false;
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
LRESULT CALLBACK ConfirmFixtureDelete(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE) {
        HWND dialog = reinterpret_cast<HWND>(wParam);
        wchar_t title[128]{}; GetWindowTextW(dialog, title, _countof(title));
        if (GetWindow(dialog, GW_OWNER) == deleteTestOwner &&
            wcscmp(title, L"FastFile - 确认删除") == 0) {
            ++deleteDialogCount;
            PostMessageW(dialog, WM_COMMAND, deleteTestReply, 0);
        }
        if (createFixtureFolder && GetWindow(dialog, GW_OWNER) == deleteTestOwner &&
            wcscmp(title, L"新建文件夹") == 0) {
            SetDlgItemTextW(dialog, 1002, L"快捷键新目录");
            PostMessageW(dialog, WM_COMMAND, IDOK, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
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
        host.DrawIconItem(&draw);
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
    static bool ShellMediaThumbnail(ShellBrowserHost& host,const std::wstring& path) {
        return MediaAspectRatio(host,path,0,0);
    }
    static bool SkipsGroupThumbnail(ShellBrowserHost& host,int index) {
        host.ClearItemImages();
        NMLVCUSTOMDRAW draw{};
        draw.nmcd.dwItemSpec=index;draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;
        draw.dwItemType=LVCDI_GROUP;
        return host.DrawIconItem(&draw)==CDRF_DODEFAULT && host.m_itemImages.empty();
    }
    static bool SkipsClippedThumbnail(ShellBrowserHost& host,int index) {
        HWND list=host.m_listWindow;
        RECT bounds{};GetWindowRect(list,&bounds);
        host.ClearItemImages();
        SetWindowPos(list,nullptr,0,0,bounds.right-bounds.left,0,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        HDC dc=GetDC(list);
        NMLVCUSTOMDRAW draw{};draw.nmcd.hdc=dc;draw.nmcd.dwItemSpec=index;draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;
        host.DrawIconItem(&draw);
        const bool skipped=host.m_itemImages.empty();
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
        int pixels=0;
        for(int y=0;y<bounds.bottom;y+=2) for(int x=0;x<bounds.right;x+=2)
            { const COLORREF color=GetPixel(dc,x,y);
              pixels+=GetRValue(color)<80 && GetGValue(color)>60 && GetGValue(color)<150 && GetBValue(color)>140; }
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
        return pixels>100;
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
        const int height=MulDiv(29,dpi,96);strip.SetPos({0,0,1000,height},false);
        HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,1000,height);auto old=SelectObject(dc,bitmap);
        RECT canvas{0,0,1000,height};FillRect(dc,&canvas,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        strip.DoPaint(dc,canvas,nullptr);
        const int x=strip.m_tabs[0].body.right+MulDiv(UiTokens::TabCardGap,dpi,96)/2;
        const COLORREF expected=dark ? RGB(0x50,0x50,0x50):RGB(0xcf,0xcf,0xcf);
        const bool line=GetPixel(dc,x,height/2)==expected && GetPixel(dc,x,height/2- MulDiv(10,dpi,96))!=expected;
        const bool seam=strip.m_tabs[0].body.left==0 && GetPixel(dc,0,height/2)==GetPixel(dc,1,height/2);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
        strip.SetDarkMode(false);return line && seam && strip.GetFixedHeight()==height && strip.m_plus.top>=0 && strip.m_plus.bottom<=height;
    }
    static bool Check(DuiLib::CTabStripUI& strip) {
        strip.Clear();
        strip.SetMetrics(144);
        strip.SetTabWidthRange(UiTokens::TabMinW, UiTokens::TabSelMinW, UiTokens::TabMaxW);
        strip.Add(L"D:\\Program Files", L"Program Files", L"", 24, true);
        strip.Add(L"D:\\", L"D:", L"", 24, false);
        strip.SetPos({0, 0, 1000, MulDiv(29,144,96)}, false);
        const int preferred = strip.MeasureTabWidth(0);
        const int activeWidth = strip.m_tabs[0].width;
        strip.SetActiveTab(1);
        const bool equal = activeWidth == strip.m_tabs[0].width;
        const bool full = activeWidth >= preferred;
        const bool plus = strip.m_plus.left > strip.m_tabs.back().body.right &&
            strip.m_plus.left - strip.m_tabs.back().body.right <= MulDiv(8, 144, 96);
        const bool height=strip.m_tabs[0].body.bottom-strip.m_tabs[0].body.top==MulDiv(29,144,96) &&
            strip.m_tabs[1].body.bottom-strip.m_tabs[1].body.top==MulDiv(29,144,96);
        // Width is 1.5 times the measured icon/title/close-slot content at this DPI.
        HDC dc=GetDC(strip.m_pManager->GetPaintWindow());
        auto old=SelectObject(dc,strip.m_pManager->GetFont(7));SIZE title{};
        GetTextExtentPoint32W(dc,L"Program Files",13,&title);
        SelectObject(dc,old);ReleaseDC(strip.m_pManager->GetPaintWindow(),dc);
        const int former=MulDiv(10,144,96)+MulDiv(16,144,96)+MulDiv(6,144,96)+
            title.cx+MulDiv(28,144,96)+MulDiv(10,144,96);
        return equal && full && plus && height && preferred==MulDiv(former,3,2) &&
            UiTokens::TabMinW==120 && UiTokens::TabMaxW==360;
    }
};

}

struct MainWndRegressionAccess {
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
        FastFileSettings prefs=window.m_settings;prefs.contextMenu=true;prefs.confirmClose=false;prefs.externalNewWindow=false;
        check(CMainWnd::ApplySystemIntegration(prefs),"production integration registers the explicit FastFile verb");
        window.m_settings=prefs;prefs.Save(FastFileSettings::FilePath());
        DWORD disabled=0;HKEY state=nullptr;
        RegCreateKeyExW(user,L"Software\\FastFile",0,nullptr,0,KEY_ALL_ACCESS,nullptr,&state,nullptr);
        RegSetValueExW(state,L"FolderHandlerEnabled",0,REG_DWORD,reinterpret_cast<const BYTE*>(&disabled),sizeof(disabled));RegCloseKey(state);
        // Resolve the real production registration; only add the test harness
        // switch so the subprocess isolates its desktop/registry before entry.
        for(const auto* cls:{L"Directory",L"Drive"}) {
            const auto key=std::wstring(cls)+L"\\shell\\FastFile.SettingsOpen";
            wchar_t command[32768]{};DWORD bytes=sizeof(command);
            check(RegGetValueW(user,(L"Software\\Classes\\"+key+L"\\command").c_str(),nullptr,RRF_RT_REG_SZ,nullptr,command,&bytes)==ERROR_SUCCESS,
                "explicit verb has a registered launch command");
            std::wstring launch=command;const auto quote=launch.find(L'"',1);
            if(quote!=std::wstring::npos)launch.insert(quote+1,L" --shell-activation-launcher");
            HKEY verb=nullptr;RegCreateKeyExW(classes,(key+L"\\command").c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&verb,nullptr);
            RegSetValueExW(verb,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(launch.c_str()),DWORD((launch.size()+1)*sizeof(wchar_t)));RegCloseKey(verb);
        }
        if(RegOverridePredefKey(HKEY_CLASSES_ROOT,classes)!=ERROR_SUCCESS) {
            RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);RegCloseKey(classes);RegCloseKey(user);return failures+1;
        }
        auto pump=[](){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
        }};
        auto invoke=[&](const std::wstring& path,const wchar_t* cls) {
            SHELLEXECUTEINFOW call{};call.cbSize=sizeof(call);call.fMask=SEE_MASK_CLASSNAME|SEE_MASK_NOASYNC|SEE_MASK_NOCLOSEPROCESS;
            call.lpClass=cls;call.lpVerb=L"FastFile.SettingsOpen";call.lpFile=path.c_str();call.nShow=SW_HIDE;
            check(ShellExecuteExW(&call)!=FALSE && call.hProcess,"Windows executes the registered FastFile context-menu verb");
            return call.hProcess;
        };
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
        // Cold start: no receiver exists. Verify the production entry creates a
        // window for exactly the requested folder and saves that tab on close.
        RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr);
        DestroyWindow(window.m_hWnd);
        pump(); // Consume the closed host's WM_QUIT before ShellExecuteEx waits.
        RegOverridePredefKey(HKEY_CLASSES_ROOT,classes);
        HANDLE cold=invoke(fixture+L"\\115Chrome",L"Directory");
        if(cold) {
            HWND opened=nullptr;const DWORD deadline=GetTickCount()+6000;
            while(GetTickCount()<deadline && !opened){
                pump();HWND candidate=FindWindowW(L"FastFile_MainWnd",nullptr);DWORD pid=0;
                if(candidate)GetWindowThreadProcessId(candidate,&pid);
                if(candidate && pid==GetProcessId(cold) && IsWindowVisible(candidate)
                    && GetPropW(candidate,L"FastFile.Test.Initialized"))opened=candidate;
                Sleep(5);
            }
            check(opened!=nullptr,"explicit menu cold-starts a FastFile window");
            if(opened)PostMessageW(opened,WM_CLOSE,0,0);
            const DWORD closeDeadline=GetTickCount()+6000;
            while(GetTickCount()<closeDeadline && WaitForSingleObject(cold,0)==WAIT_TIMEOUT){pump();Sleep(5);}
            DWORD result=STILL_ACTIVE;GetExitCodeProcess(cold,&result);
            check(result==0,"cold-started FastFile closes normally");
            if(result==STILL_ACTIVE)TerminateProcess(cold,3);CloseHandle(cold);
            wchar_t saved[32768]{};GetPrivateProfileStringW(L"Tabs",L"Path0",L"",saved,_countof(saved),CMainWnd::GetSessionFilePath().c_str());
            check(CMainWnd::PathEquals(saved,fixture+L"\\115Chrome"),"cold start opens the requested folder rather than Explorer or the default location");
        }
        RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr);RegOverridePredefKey(HKEY_CURRENT_USER,nullptr);
        RegCloseKey(classes);RegCloseKey(user);RegDeleteTreeW(HKEY_CURRENT_USER,hiveName);
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
        const bool deleted = aclOk && window.DeleteItems(items, false, &completed);
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
        const DWORD recycleFlags = window.DeleteOperationFlags(false);
        const DWORD permanentFlags = window.DeleteOperationFlags(true);
        check((recycleFlags & (FOF_NOERRORUI | FOF_SILENT | FOFX_REQUIREELEVATION)) == 0,
            "Delete keeps Windows error, progress and permission confirmation UI enabled");
        check((recycleFlags & FOFX_SHOWELEVATIONPROMPT) != 0 &&
            (permanentFlags & FOFX_SHOWELEVATIONPROMPT) != 0,
            "both delete modes allow Windows elevation prompt");
        check((recycleFlags & (FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD | FOF_WANTNUKEWARNING)) ==
            (FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD | FOF_WANTNUKEWARNING),
            "normal Delete recycles, supports undo and warns if recycling is impossible");
        check((permanentFlags & (FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD)) == 0,
            "permanent Delete cannot accidentally recycle");
        auto pump = [&]() {
            const DWORD until = GetTickCount() + 700;
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
        const auto source = root + L"\\Keyboard Source";
        const auto target = root + L"\\Keyboard Target";
        const auto moved = root + L"\\Keyboard Move";
        for (const auto& path : {source, target, moved}) CreateDirectoryW(path.c_str(), nullptr);
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
            BYTE saved[256]{}, keys[256]{}; GetKeyboardState(saved);
            if (ctrl) keys[VK_CONTROL] = 0x80;
            if (shift) keys[VK_SHIFT] = 0x80;
            if (alt) keys[VK_MENU] = 0x80;
            SetKeyboardState(keys);
            MSG message{}; message.hwnd = hwnd; message.message = alt ? WM_SYSKEYDOWN : WM_KEYDOWN;
            message.wParam = value; message.lParam = 1;
            const bool handled = CPaintManagerUI::TranslateMessage(&message);
            SetKeyboardState(saved);
            return handled;
        };
        auto select = [&]() { check(key('A', true), "native Ctrl A routes to selection"); pump(); };
        auto finish = [&]() {
            const DWORD until = GetTickCount() + 8000;
            do { pump(); } while (window.m_copyRunning && GetTickCount() < until);
            check(!window.m_copyRunning, "keyboard file job finishes");
        };
        // Every keyboard file operation must run through IFileOperation with the native
        // Windows progress UI (production flags), never FastFile's old status-bar progress.
        auto nativeOperation = [&](ShellFileOps::Kind kind, const char* name) {
            const auto& last = window.m_lastFileOperation;
            const std::wstring status = window.m_pStatus ? window.m_pStatus->GetText().GetData() : L"";
            check(last.engine == ShellFileOps::Engine::FileOperation && last.kind == kind &&
                window.m_lastFileOpRequestFlags == last.flags &&
                (last.flags & (FOF_SILENT | FOF_NOERRORUI | FOFX_NOMINIMIZEBOX)) == 0 &&
                status.find(L'%') == std::wstring::npos, name);
        };
        navigate(source); select();
        {
            window.m_pendingShellRename = original;
            IFileOperation* operation = nullptr;
            IShellItem* item = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
            if (SUCCEEDED(hr)) hr = operation->SetOperationFlags(FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOF_SILENT | FOF_NOCONFIRMATION);
            if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName(original.c_str(), nullptr, IID_PPV_ARGS(&item));
            if (SUCCEEDED(hr)) hr = operation->RenameItem(item, L"renamed.txt", nullptr);
            if (SUCCEEDED(hr)) hr = operation->PerformOperations();
            if (item) item->Release(); if (operation) operation->Release();
            pump();
            check(SUCCEEDED(hr) && !window.m_undoStack.empty() &&
                window.m_undoStack.back().kind == CMainWnd::UndoRecord::Kind::ShellRename,
                "native rename notification joins application history");
            check(key('Z', true), "Ctrl Z invokes native rename undo");
            pump();
            check(GetFileAttributesW(original.c_str()) != INVALID_FILE_ATTRIBUTES, "native Shell undo restores original name");
            check(key('Y', true), "Ctrl Y invokes native rename redo"); pump();
            check(GetFileAttributesW((source + L"\\renamed.txt").c_str()) != INVALID_FILE_ATTRIBUTES, "native Shell redo reapplies rename");
            key('Z', true); pump();
            select();
        }
        check(key('C', true, true), "Ctrl Shift C copies quoted paths");
        bool copiedPath = false;
        if (OpenClipboard(window.m_hWnd)) {
            HGLOBAL data = GetClipboardData(CF_UNICODETEXT);
            const wchar_t* text = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
            copiedPath = text && std::wstring(text) == L"\"" + original + L"\"";
            if (text) GlobalUnlock(data); CloseClipboard();
        }
        check(copiedPath, "copy as path publishes Unicode quoted selection");
        check(key(VK_INSERT, true), "Ctrl Insert copies native selection");
        // Paste must read Windows data, even when the old internal cache is empty.
        window.m_clipboard.clear(); navigate(target);
        check(window.m_pBtnPaste->IsEnabled(), "system clipboard enables paste button");
        check(key(VK_INSERT, false, true), "Shift Insert pastes native file clipboard"); finish();
        nativeOperation(ShellFileOps::Kind::Copy, "paste copies through IFileOperation with native progress UI");
        const auto copied = target + L"\\keyboard.txt";
        file = CreateFileW(copied.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        char actual[sizeof(payload)]{}; DWORD read = 0;
        if (file != INVALID_HANDLE_VALUE) { ReadFile(file, actual, sizeof(actual), &read, nullptr); CloseHandle(file); }
        check(read == sizeof(payload) && memcmp(actual, payload, sizeof(payload)) == 0 &&
            GetFileAttributesW(original.c_str()) != INVALID_FILE_ATTRIBUTES, "Ctrl C V copies exact file bytes and retains source");
        // A Shell rename after an internal copy must be undone before that copy.
        {
            window.m_pendingShellRename = copied;
            IFileOperation* operation = nullptr; IShellItem* item = nullptr;
            HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation));
            if (SUCCEEDED(hr)) hr = operation->SetOperationFlags(FOF_ALLOWUNDO | FOFX_ADDUNDORECORD | FOF_SILENT | FOF_NOCONFIRMATION);
            if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName(copied.c_str(), nullptr, IID_PPV_ARGS(&item));
            if (SUCCEEDED(hr)) hr = operation->RenameItem(item, L"mixed.txt", nullptr);
            if (SUCCEEDED(hr)) hr = operation->PerformOperations();
            if (item) item->Release(); if (operation) operation->Release(); pump();
            key('Z', true); pump();
            check(SUCCEEDED(hr) && GetFileAttributesW(copied.c_str()) != INVALID_FILE_ATTRIBUTES,
                "mixed history undoes Shell rename before internal copy");
        }
        check(key('Z', true), "Ctrl Z handles copy undo"); pump();
        check(GetFileAttributesW(copied.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(original.c_str()) != INVALID_FILE_ATTRIBUTES, "copy undo only removes created copy");
        file = CreateFileW(copied.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr); CloseHandle(file);
        const auto redoBeforeConflict = window.m_redoStack.size();
        key('Y', true); pump();
        check(window.m_redoStack.size() == redoBeforeConflict && GetFileAttributesW(copied.c_str()) != INVALID_FILE_ATTRIBUTES,
            "copy redo refuses to overwrite unrelated same-name file and remains retryable");
        DeleteFileW(copied.c_str());
        check(key('Y', true), "Ctrl Y handles copy redo"); pump();
        check(GetFileAttributesW(copied.c_str()) != INVALID_FILE_ATTRIBUTES, "copy redo restores retained bytes");
        key('Y', true); pump();
        check(GetFileAttributesW((target + L"\\mixed.txt").c_str()) != INVALID_FILE_ATTRIBUTES,
            "mixed history redoes copy before Shell rename");
        key('Z', true); pump();
        pump(); select(); check(key('X', true), "native cut shortcut handled");
        std::vector<CMainWnd::ClipboardItem> clipboard; bool cut = false;
        bool clipboardReady=false;const DWORD clipboardDeadline=GetTickCount()+1000;
        do {
            clipboardReady=window.ReadFileClipboard(clipboard,cut) && cut && clipboard.size()==1
                && CMainWnd::PathEquals(clipboard[0].path,copied);
            if(clipboardReady)break;
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message!=WM_QUIT && !CPaintManagerUI::TranslateMessage(&message)) {
                    TranslateMessage(&message);DispatchMessageW(&message);
                }
            }Sleep(5); // Clipboard observers may briefly hold OpenClipboard.
        }while(GetTickCount()<clipboardDeadline);
        check(clipboardReady,
            "Ctrl X publishes the Windows move effect");
        navigate(moved); check(key('V', true), "cut paste shortcut handled"); finish();
        nativeOperation(ShellFileOps::Kind::Move, "cut paste moves through IFileOperation with native progress UI");
        check(GetFileAttributesW(copied.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW((moved + L"\\keyboard.txt").c_str()) != INVALID_FILE_ATTRIBUTES,
            "Ctrl X V moves the selected fixture");
        key('Z', true); pump();
        check(GetFileAttributesW(copied.c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW((moved + L"\\keyboard.txt").c_str()) == INVALID_FILE_ATTRIBUTES, "move Ctrl Z restores source");
        key('Y', true); pump();
        check(GetFileAttributesW(copied.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW((moved + L"\\keyboard.txt").c_str()) != INVALID_FILE_ATTRIBUTES, "move Ctrl Y restores destination");
        select();
        deleteTestOwner = window.m_hWnd;
        HHOOK hook = SetWindowsHookExW(WH_CBT, ConfirmFixtureDelete, nullptr, GetCurrentThreadId());
        check(hook != nullptr, "fixture-only delete confirmation hook installed");
        if (hook) {
            deleteTestReply = IDNO; deleteDialogCount = 0;
            check(key(VK_DELETE, false), "Delete runs recycle operation without custom confirmation"); finish();
            nativeOperation(ShellFileOps::Kind::Recycle, "Delete recycles through IFileOperation with native progress UI");
            check(deleteDialogCount == 0, "ordinary Delete never opens FastFile confirmation");
            check(GetFileAttributesW((moved + L"\\keyboard.txt").c_str()) == INVALID_FILE_ATTRIBUTES,
                "ordinary Delete immediately recycles fixture");
            {
                // A FastFile copy after a recycle must not push its own Explorer undo record:
                // undoing the copy and then the recycle has to restore the recycled item.
                window.PublishFileClipboard({{original, false}}, false);
                navigate(target); key('V', true); finish();
                const auto alignedCopy = target + L"\\keyboard.txt";
                check(GetFileAttributesW(alignedCopy.c_str()) != INVALID_FILE_ATTRIBUTES &&
                    window.m_undoStack.back().kind == CMainWnd::UndoRecord::Kind::Copy, "copy after recycle joins history");
                key('Z', true); pump();
                check(GetFileAttributesW(alignedCopy.c_str()) == INVALID_FILE_ATTRIBUTES &&
                    GetFileAttributesW(original.c_str()) != INVALID_FILE_ATTRIBUTES, "copy after recycle is undone first");
                navigate(moved);
            }
            key('Z', true); pump();
            check(GetFileAttributesW((moved + L"\\keyboard.txt").c_str()) != INVALID_FILE_ATTRIBUTES,
                "Delete Ctrl Z restores recycled item, also after a later FastFile copy was undone (stacks stay aligned)");
            key('Y', true); pump();
            check(GetFileAttributesW((moved + L"\\keyboard.txt").c_str()) == INVALID_FILE_ATTRIBUTES,
                "Delete Ctrl Y repeats recycle operation");
            navigate(source); select(); deleteTestReply = IDNO;
            check(key(VK_DELETE, false, true), "Shift Delete cancellation handled"); finish();
            check(GetFileAttributesW(original.c_str()) != INVALID_FILE_ATTRIBUTES,
                "cancelled permanent Delete retains fixture");
            deleteTestReply = IDYES;
            check(key(VK_DELETE, false, true), "Shift Delete handled"); finish();
            nativeOperation(ShellFileOps::Kind::Delete, "Shift Delete removes through IFileOperation with native progress UI");
            check(GetFileAttributesW(original.c_str()) == INVALID_FILE_ATTRIBUTES, "Shift Delete permanently removes fixture");
            createFixtureFolder = true;
            check(key('N', true, true), "Ctrl Shift N opens new folder prompt"); pump();
            createFixtureFolder = false;
            const auto newFolder = source + L"\\快捷键新目录";
            check(GetFileAttributesW(newFolder.c_str()) != INVALID_FILE_ATTRIBUTES, "new folder shortcut creates entered name");
            key('Z', true); pump();
            check(GetFileAttributesW(newFolder.c_str()) == INVALID_FILE_ATTRIBUTES, "new folder undo removes empty directory");
            key('Y', true); pump();
            check(GetFileAttributesW(newFolder.c_str()) != INVALID_FILE_ATTRIBUTES, "new folder redo recreates directory");
            const auto childFile = newFolder + L"\\keep.txt";
            file = CreateFileW(childFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr); CloseHandle(file);
            key('Z', true); pump();
            check(GetFileAttributesW(childFile.c_str()) != INVALID_FILE_ATTRIBUTES, "new folder undo cannot delete newly added contents");
            DeleteFileW(childFile.c_str()); key('Z', true); pump(); key('Y', true); pump();
            UnhookWindowsHookEx(hook);
        }
        deleteTestOwner = nullptr;
        OleSetClipboard(nullptr); window.m_clipboard = {{moved, true}};
        check(key('V', true) && !window.m_copyRunning, "empty Windows clipboard cannot paste stale internal data");
        window.m_clipboard.clear();
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
        // Batch copy with a directory verifies that undo records top-level items,
        // preserves originals, and can leave a directory removed by its own undo.
        const auto batchSource = root + L"\\Batch Source";
        const auto batchDestination = root + L"\\Batch Destination";
        CreateDirectoryW(batchSource.c_str(), nullptr); CreateDirectoryW(batchDestination.c_str(), nullptr);
        const auto directory = batchSource + L"\\Folder";
        CreateDirectoryW(directory.c_str(), nullptr);
        const auto nested = directory + L"\\data.txt";
        file = CreateFileW(nested.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        WriteFile(file, payload, sizeof(payload), &written, nullptr); CloseHandle(file);
        const auto standalone = batchSource + L"\\one.txt";
        CopyFileW(nested.c_str(), standalone.c_str(), TRUE);
        window.PublishFileClipboard({{directory,true},{standalone,false}}, false);
        navigate(batchDestination); key('V', true); finish();
        nativeOperation(ShellFileOps::Kind::Copy, "batch paste copies through IFileOperation");
        check(window.m_undoStack.back().moved.size() == 2, "batch copy records both completed top-level items");
        navigate(batchDestination + L"\\Folder"); key('Z', true); pump();
        check(window.m_currentPath == batchDestination &&
            GetFileAttributesW((batchDestination + L"\\Folder").c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW((batchDestination + L"\\one.txt").c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(nested.c_str()) != INVALID_FILE_ATTRIBUTES,
            "batch folder undo retains originals and returns removed view to parent");
        key('Y', true); pump();
        check(GetFileAttributesW((batchDestination + L"\\Folder\\data.txt").c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW((batchDestination + L"\\one.txt").c_str()) != INVALID_FILE_ATTRIBUTES,
            "batch folder redo restores complete retained contents");
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
            check(CMainWnd::ApplySystemIntegration(enabled),"explicit integration registers folders drives and Computer");
            check(read(folder)==L"原始文件夹命令","generic Folder namespace default is preserved for Windows virtual folders");
            for(const auto& shell:{directory,drive,computer})
                check(read(shell)==L"FastFile.SettingsOpen" && read(shell+L"\\FastFile.SettingsOpen\\command").find(L"--shell-folder")!=std::wstring::npos,
                    "default commands use the explicit integration verb and quoted path activation");
            check(CMainWnd::RestoreNativeFolderHandlers(),"legacy migration succeeds with new integration enabled");
            check(read(directory)==L"FastFile.SettingsOpen","startup cleanup cannot undo user-enabled settings integration");
            FastFileSettings state;CMainWnd::ReadSystemIntegration(state);
            check(state.contextMenu && state.defaultFolders && state.defaultComputer,"integration switches survive reloading state");
            enabled.defaultFolders=false;
            check(CMainWnd::ApplySystemIntegration(enabled),"folder defaults can be disabled while keeping context menu and Computer enabled");
            DWORD type=0;
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
            check(CMainWnd::ApplySystemIntegration(off) && read(directory)==L"AnotherAppAfterEnable" && read(folder)==L"AnotherAppAfterEnable",
                "restoration preserves an application chosen after FastFile was enabled");
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
    inline static bool settingsTestSave=false,settingsDialogPages=false;
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
        if(!dialog || !IsWindowVisible(dialog) || !GetDlgItem(dialog,126))return;
        KillTimer(nullptr,timer);
        HWND tabs=GetDlgItem(dialog,100);
        settingsDialogPages=TabCtrl_GetItemCount(tabs)==4;
        for(int page=0;page<4;++page) {
            TabCtrl_SetCurSel(tabs,page);
            NMHDR changed{tabs,100,TCN_SELCHANGE};SendMessageW(dialog,WM_NOTIFY,100,reinterpret_cast<LPARAM>(&changed));
            const int visibleIds[]={110,116,121,126};
            settingsDialogPages=settingsDialogPages && IsWindowVisible(GetDlgItem(dialog,visibleIds[page]));
        }
        TabCtrl_SetCurSel(tabs,1);
        NMHDR changed{tabs,100,TCN_SELCHANGE};SendMessageW(dialog,WM_NOTIFY,100,reinterpret_cast<LPARAM>(&changed));
        SendMessageW(GetDlgItem(dialog,116),CB_SETCURSEL,2,0); // 13 logical pixels
        SendMessageW(dialog,WM_COMMAND,settingsTestSave?IDOK:IDCANCEL,0);
    }
    static int CheckPreferences(CMainWnd& window,const std::wstring& fixture) {
        int failures=0;auto check=[&](bool ok,const char* name){if(!ok){std::cerr<<"FAIL "<<name<<'\n';++failures;}};
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
        for(const auto& targetPath:{drive,fixture}) {
            PIDLIST_ABSOLUTE pidl=nullptr;PCUITEMID_CHILD child=nullptr;
            IShellFolder* parent=nullptr;IContextMenu* nativeMenu=nullptr;
            HMENU menu=CreatePopupMenu();bool found=false;
            if(SUCCEEDED(SHParseDisplayName(targetPath.c_str(),nullptr,&pidl,0,nullptr))
                && SUCCEEDED(SHBindToParent(pidl,IID_PPV_ARGS(&parent),&child))
                && SUCCEEDED(parent->GetUIObjectOf(window.m_hWnd,1,&child,IID_IContextMenu,nullptr,reinterpret_cast<void**>(&nativeMenu)))) {
                const HRESULT result=nativeMenu->QueryContextMenu(menu,0,1,0x7fff,CMF_NORMAL);
                if(SUCCEEDED(result)) {
                    window.PruneShellMenu(nativeMenu,menu,1,1+HRESULT_CODE(result),false);
                    window.AddInternalFolderOpenMenu(nativeMenu,menu,1,1+HRESULT_CODE(result),{targetPath});
                    for(int i=0;i<GetMenuItemCount(menu);++i) {
                        const UINT id=GetMenuItemID(menu,i);wchar_t verb[128]{},text[256]{};
                        if(id==CMainWnd::kCmdShellNewTab || (id>=1 && id<1+HRESULT_CODE(result)
                            && SUCCEEDED(nativeMenu->GetCommandString(id-1,GCS_VERBW,nullptr,reinterpret_cast<LPSTR>(verb),_countof(verb)))
                            && _wcsicmp(verb,L"opennewwindow")==0)) {
                            GetMenuStringW(menu,i,text,_countof(text),MF_BYPOSITION);
                            found=wcscmp(text,L"在新选项卡中打开")==0;
                        }
                    }
                }
            }
            check(found,"real Windows disk/folder menu offers FastFile's new-tab action even when Shell omits its new-window verb");
            if(nativeMenu)nativeMenu->Release();if(parent)parent->Release();
            CoTaskMemFree(pidl);DestroyMenu(menu);
        }
        check(window.HandleInternalFolderOpenVerb(L"open",{drive}),"navigation menu open is handled internally for a drive");
        MSG folderMenu{};bool routed=false;
        if(PeekMessageW(&folderMenu,window.m_hWnd,CMainWnd::kMsgShellFolderOpen,CMainWnd::kMsgShellFolderOpen,PM_REMOVE)) {
            const auto* target=reinterpret_cast<std::wstring*>(folderMenu.lParam);
            routed=target && CMainWnd::PathEquals(*target,drive);
            window.HandleMessage(folderMenu.message,folderMenu.wParam,folderMenu.lParam);
        }
        check(routed && CMainWnd::PathEquals(window.m_currentPath,drive),"directory menu command reaches FastFile instead of invoking the registered open verb");
        for(const auto& sample:{std::pair<std::wstring,std::wstring>{L"opennewwindow",drive},
                {L"opennewtab",fixture},{L"OPENNEWWINDOW",fixture+L"\\Program Files"}}) {
            const auto count=window.m_tabs.size();const int active=window.m_activeTab;
            const auto originalPath=window.m_tabs[active].path;
            check(window.HandleInternalFolderOpenVerb(sample.first,{sample.second}),"new-window/tab Shell verbs are consumed by FastFile");
            bool newTab=false;MSG request{};
            if(PeekMessageW(&request,window.m_hWnd,CMainWnd::kMsgShellFolderOpen,CMainWnd::kMsgShellFolderOpen,PM_REMOVE)) {
                newTab=request.wParam!=0;
                window.HandleMessage(request.message,request.wParam,request.lParam);
            }
            check(newTab && window.m_tabs.size()==count+1 && window.m_tabs[active].path==originalPath
                && CMainWnd::PathEquals(window.m_currentPath,sample.second),"new-window command opens a new FastFile tab and retains source tab");
            const DWORD deadline=GetTickCount()+200;
            do {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
            }Sleep(5);}while(GetTickCount()<deadline);
            check(window.m_activeTab==static_cast<int>(count) && CMainWnd::PathEquals(window.m_currentPath,sample.second),
                "navigation completion must keep an explicitly duplicated tab active");
        }
        check(!window.HandleInternalFolderOpenVerb(L"opennewwindow",{fixture+L"\\Program Files\\sample.txt"})
            && !window.HandleInternalFolderOpenVerb(L"opennewwindow",{drive,fixture+L"\\missing"}),
            "new-tab routing leaves files and invalid mixed selections to Shell");
        check(!window.HandleInternalFolderOpenVerb(L"open",{fixture+L"\\Program Files\\sample.txt"})
            && !window.HandleInternalFolderOpenVerb(L"properties",{drive}),"menu routing preserves file associations and non-navigation Shell commands");
        settingsTestOwner=window.m_hWnd;settingsTestSave=false;settingsDialogPages=false;
        auto settingsTimer=SetTimer(nullptr,0,100,ExerciseSettingsDialog);window.ShowSettings();KillTimer(nullptr,settingsTimer);
        if(!settingsDialogPages || window.m_settings.navigationFont!=original.navigationFont)
            std::cerr<<"settings cancel diagnostic pages="<<settingsDialogPages<<" font="<<window.m_settings.navigationFont<<" expected="<<original.navigationFont<<'\n';
        check(settingsDialogPages && window.m_settings.navigationFont==original.navigationFont,
            "all four real settings pages open and cancel discards staged changes");
        settingsTestSave=true;settingsDialogPages=false;
        settingsTimer=SetTimer(nullptr,0,100,ExerciseSettingsDialog);window.ShowSettings();KillTimer(nullptr,settingsTimer);
        check(settingsDialogPages && window.m_settings.navigationFont==13 && FastFileSettings::Load(FastFileSettings::FilePath()).navigationFont==13,
            "real settings save applies appearance and persists the new value");
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
    static int CheckUiMetrics(CMainWnd& window) {
        int failures = 0;
        auto check = [&](bool result, const char* name) {
            if (!result) { std::cerr << "FAIL " << name << '\n'; ++failures; }
        };
        const UINT initialDpi = window.m_dpi;
        for (UINT dpi : {96u,144u,192u}) {
            window.m_dpi=dpi;window.ApplyDpiScaledChrome();window.ApplyUiChromeTokens();window.ApplyDpiScaledFonts();window.RebuildFavoritesBar();
            check(static_cast<CContainerUI*>(window.m_PaintManager.FindControl(L"toolbar"))->GetChildPadding()==MulDiv(4,dpi,96),"command buttons have DPI-scaled separation");
            for(const auto* name:{L"btn_new",L"btn_sort",L"btn_view_menu"})
                check(window.m_PaintManager.FindControl(name)->GetFixedWidth()==MulDiv(88,dpi,96),"label commands reserve icon text and chevron space");
            auto* settingsButton=window.m_PaintManager.FindControl(L"btn_settings");
            check(settingsButton && settingsButton->GetFixedWidth()==MulDiv(32,dpi,96)
                && settingsButton->GetFixedHeight()==MulDiv(32,dpi,96),"settings gear hit area follows 96 144 and 192 DPI");
            auto* divider=window.m_PaintManager.FindControl(L"command_body_divider");
            check(divider && divider->GetFixedHeight()==window.DpiScaleHairline(1) && divider->GetBkColor()==0xFFD0D0D0,"file area uses a real painted separator instead of bottom-only border");
            check(static_cast<CContainerUI*>(window.m_PaintManager.FindControl(L"body_host"))->GetInset().top==MulDiv(8,dpi,96),"file group headers have breathing room below command bar");
            if(divider) {
                const RECT previous=divider->GetPos();const int h=divider->GetFixedHeight();
                HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
                HBITMAP bitmap=CreateCompatibleBitmap(screen,64,h+2);auto old=SelectObject(dc,bitmap);
                RECT canvas{0,0,64,h+2};FillRect(dc,&canvas,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                divider->SetPos({0,0,64,h},false);divider->Paint(dc,canvas,nullptr);
                check(GetPixel(dc,32,0)==RGB(0xd0,0xd0,0xd0) && GetPixel(dc,32,h)==RGB(255,255,255),
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
        const int quickHeight=window.m_pLeftQuickRows->GetItemAt(0)->GetFixedHeight();
        auto* rootNode=static_cast<CTreeNodeUI*>(window.m_pDirTree->GetItemAt(0));
        const int treeHeight=rootNode->GetFixedHeight();
        check(window.m_pLeftQuickRows->GetCount()>=5,"first-run profile immediately renders default quick rows including Pictures");
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
                        if(pixel.GetAlpha()==(dim?102:255)) check(pixel.GetR()>=0x19 && pixel.GetR()<=0x1B && pixel.GetG()==pixel.GetR() && pixel.GetB()==pixel.GetR(),
                            "command icon uses requested monochrome ink");
                    }
                    int smoothLevels=0;for(int a=1;a<(dim?102:255);++a)if(coverage[a])++smoothLevels;
                    check(smoothLevels>=16,"command vector edges retain graded alpha at actual DPI size");
                    check(icon.GetWidth()==UINT(MulDiv(16,dpi,96)) && alpha==(dim?102:255),
                        "command bitmap is rendered at physical size with overall 40 percent disabled alpha");
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
    // Shell menus use separators with ids 0, -1 and private ids (0x7FFD / 0x7FFE). The old
    // tidy pass only recognised id 0, so pruning 授予访问权限 left two stacked lines.
    static int CheckShellMenus(CMainWnd& window, const std::wstring& folder) {
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
        auto stacked = [](HMENU menu) {
            const int count = GetMenuItemCount(menu);
            for (int i = 0; i < count; ++i) {
                if (!ShellMenuUtil::IsSeparatorAt(menu, i)) continue;
                if (i == 0 || i == count - 1 || ShellMenuUtil::IsSeparatorAt(menu, i - 1)) return true;
            }
            return false;
        };
        {
            HMENU menu = CreatePopupMenu();
            auto separator = [&](UINT id) {
                MENUITEMINFOW info{}; info.cbSize = sizeof(info);
                info.fMask = MIIM_FTYPE | MIIM_ID; info.fType = MFT_SEPARATOR; info.wID = id;
                InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &info);
            };
            separator(0x7FFC);
            AppendMenuW(menu, MF_STRING, 1, L"刷新");
            separator(0xFFFFFFFF);
            AppendMenuW(menu, MF_STRING, 2, L"在终端中打开(&T)");
            separator(0); separator(0x7FFD);           // the doubled line from the screenshot
            AppendMenuW(menu, MF_STRING, 3, L"新建(&W)");
            separator(0x7FFE);
            AppendMenuW(menu, MF_STRING, 4, L"属性(&R)");
            separator(0x7FFC);
            CMainWnd::TidyMenuSeparators(menu);
            check(!stacked(menu) && GetMenuItemCount(menu) == 7 && GetMenuItemID(menu, 0) == 1 &&
                GetMenuItemID(menu, 6) == 4, "Shell menu separators with any id are normalized");
            DestroyMenu(menu);
        }
        window.NavigateToNow(folder, true);
        const DWORD deadline = GetTickCount() + 4000;
        do { pump(50); } while (!window.ShellBrowserShowsFolder(folder) && GetTickCount() < deadline);
        pump(300);
        IContextMenu* menu = nullptr; HMENU popup = nullptr; UINT shellMax = 0; bool fromView = false;
        const bool built = window.BuildShellBackgroundMenu(folder, &menu, &popup, &shellMax, &fromView);
        check(built && fromView, "folder background menu comes from the live Explorer view (SVGIO_BACKGROUND)");
        if (built) {
            check(ShellMenuUtil::FindVerb(menu, popup, 1, shellMax, L"paste") >= 0, "background menu contains native paste");
            check(ShellMenuUtil::FindVerb(menu, popup, 1, shellMax, L"properties") >= 0, "background menu contains native properties");
            check(ShellMenuUtil::FindVerb(menu, popup, 1, shellMax, L"groupby") >= 0, "background menu contains native group-by");
            bool viewMenu = false, sortMenu = false;
            for (int i = 0; i < GetMenuItemCount(popup); ++i) {
                HMENU sub = GetSubMenu(popup, i);
                if (!sub || GetMenuItemCount(sub) == 0) continue;
                const UINT first = GetMenuItemID(sub, 0);
                viewMenu = viewMenu || first == static_cast<UINT>(CMainWnd::kCmdBgViewBase + int(CMainWnd::ViewMode::ExtraLargeIcons));
                sortMenu = sortMenu || first == static_cast<UINT>(CMainWnd::kCmdBgSortBase);
            }
            check(viewMenu && sortMenu, "background 查看 and 排序方式 keep driving the FastFile view");
            check(!stacked(popup), "background menu has no leading, trailing or stacked separators");
            DestroyMenu(popup); menu->Release();
            window.ReleaseRetiredShellMenus();
        }
        {
            // Item menus must offer Explorer's 重命名(M) (CMF_CANRENAME) for one renamable
            // item in the Shell view, and the verb must start the view's in-place edit.
            const auto sample = folder + L"\\sample.txt";
            IContextMenu* itemMenu = nullptr; HMENU itemPopup = nullptr; UINT itemMax = 0;
            const bool itemBuilt = window.BuildShellItemMenu({sample}, &itemMenu, &itemPopup, &itemMax);
            check(itemBuilt && ShellMenuUtil::FindVerb(itemMenu, itemPopup, 1, itemMax, L"rename") >= 0,
                "single file item menu contains native rename");
            check(itemBuilt && !stacked(itemPopup), "item menu has no leading, trailing or stacked separators");
            if (itemBuilt) { DestroyMenu(itemPopup); itemMenu->Release(); }
            const bool multiBuilt = window.BuildShellItemMenu({sample, folder + L"\\Battle.net"}, &itemMenu, &itemPopup, &itemMax);
            check(multiBuilt && ShellMenuUtil::FindVerb(itemMenu, itemPopup, 1, itemMax, L"rename") < 0,
                "multi-selection item menu does not offer rename");
            if (multiBuilt) { DestroyMenu(itemPopup); itemMenu->Release(); }
            window.m_shellMenuPaths = {sample};
            const bool routed = window.HandleRoutedShellVerb(L"Rename");
            window.m_shellMenuPaths.clear();
            HWND edit = nullptr;
            const DWORD editDeadline = GetTickCount() + 2000;
            while (routed && !(edit = ShellBrowserHostTestAccess::EditControl(*window.m_shellBrowser)) && GetTickCount() < editDeadline) pump(20);
            std::vector<std::pair<std::wstring, bool>> renameSelection; window.m_shellBrowser->GetSelection(renameSelection);
            check(routed && edit && window.m_pendingShellRename == sample && renameSelection.size() == 1 &&
                CMainWnd::PathEquals(renameSelection[0].first, sample), "context menu rename starts in-place edit on the item");
            ShellBrowserHostTestAccess::CancelEdit(*window.m_shellBrowser); pump(100);
            window.m_pendingShellRename.clear();
            check(GetFileAttributesW(sample.c_str()) != INVALID_FILE_ATTRIBUTES, "cancelled in-place rename keeps the name");
        }
        window.m_shellMenuBackground = true; window.m_shellMenuFolder = folder;
        check(window.HandleRoutedShellVerb(L"refresh"), "background refresh is routed to FastFile refresh");
        check(!window.HandleRoutedShellVerb(L"rename"), "background menu never routes rename");
        check(!window.HandleRoutedShellVerb(L"pastelink") && !window.HandleRoutedShellVerb(L"properties"),
            "other native background verbs stay with Windows");
        window.m_shellMenuBackground = false; window.m_shellMenuFolder.clear();
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
        for (Kind kind : {Kind::Copy, Kind::Move})
            check((OperationFlags(kind, true) & (FOF_NOCONFIRMATION | FOF_RENAMEONCOLLISION | FOFX_ADDUNDORECORD)) == 0,
                "copy and move ask the native replace / skip question and stay in FastFile history");
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
                bounds.bottom - bounds.top >= window.DpiScale(36) &&
                field->GetBorderRound().cx >= window.DpiScale(6) && inset.left >= window.DpiScale(10),
                "rounded address/search fields have room for full height and inner text margins");
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
            bool copiedSelection = false;
            if (OpenClipboard(window.m_hWnd)) {
                HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
                wchar_t copiedPath[32768]{};
                if (drop && DragQueryFileW(drop, 0, copiedPath, _countof(copiedPath)))
                    copiedSelection = std::wstring(copiedPath) == fixture + L"\\Battle.net";
                CloseClipboard();
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
        window.PinQuickAccess(fixture);
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
        check(DuiLib::TabStripRegressionAccess::Check(*window.m_pTabStrip), "150 percent tab title retains preferred width, idle/active equal, plus follows last tab");
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
    fwprintf(stderr, L"CRASH code=0x%08lX module=%ls rva=0x%llX\n", info->ExceptionRecord->ExceptionCode, name,
        static_cast<unsigned long long>(reinterpret_cast<const char*>(address) - reinterpret_cast<const char*>(module)));
    CONTEXT context = *info->ContextRecord;
    for (int frame = 0; frame < 24 && context.Rip; ++frame) {
        HMODULE frameModule = nullptr; wchar_t frameName[MAX_PATH]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(context.Rip), &frameModule);
        if (frameModule) GetModuleFileNameW(frameModule, frameName, MAX_PATH);
        const wchar_t* shortName = wcsrchr(frameName, L'\\');
        fwprintf(stderr, L"  #%d %ls+0x%llX\n", frame, shortName ? shortName + 1 : frameName,
            static_cast<unsigned long long>(context.Rip - reinterpret_cast<DWORD64>(frameModule)));
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (!function) { context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp); context.Rsp += 8; continue; }
        void* handlerData = nullptr; DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
    }
    fwprintf(stderr, L"  thread=%lu\n", GetCurrentThreadId());
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    SetUnhandledExceptionFilter(ReportCrash);
    if(argc>1 && strcmp(argv[1],"--shell-activation-launcher")==0) {
        wchar_t desktopName[256]{},hiveName[256]{};
        GetEnvironmentVariableW(L"FASTFILE_TEST_DESKTOP",desktopName,_countof(desktopName));
        GetEnvironmentVariableW(L"FASTFILE_TEST_HIVE",hiveName,_countof(hiveName));
        HDESK desktop=OpenDesktopW(desktopName,0,FALSE,GENERIC_ALL);
        HKEY user=nullptr,classes=nullptr;
        if(!desktop || !SetThreadDesktop(desktop)
            || RegOpenKeyExW(HKEY_CURRENT_USER,(std::wstring(hiveName)+L"\\User").c_str(),0,KEY_ALL_ACCESS,&user)!=ERROR_SUCCESS
            || RegOpenKeyExW(HKEY_CURRENT_USER,(std::wstring(hiveName)+L"\\Classes").c_str(),0,KEY_ALL_ACCESS,&classes)!=ERROR_SUCCESS)return 4;
        if(RegOverridePredefKey(HKEY_CURRENT_USER,user)!=ERROR_SUCCESS || RegOverridePredefKey(HKEY_CLASSES_ROOT,classes)!=ERROR_SUCCESS)return 4;
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
    auto* window = new CMainWnd; // same process-lifetime ownership as application main
    HWND hwnd = window->Create(nullptr, L"FastFile regression", UI_WNDSTYLE_FRAME, WS_EX_WINDOWEDGE);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_HIDE);
    int failures = activationOnly ? MainWndRegressionAccess::CheckShellActivation(*window,fixture)
        : argc>1 && strcmp(argv[1],"--delete-permission-check")==0
        ? MainWndRegressionAccess::CheckDeletePermissionDialog(*window, root)
        : argc>1 && strcmp(argv[1],"--delete-partial-check")==0
        ? MainWndRegressionAccess::CheckDeletePermissionDialog(*window, root, true)
        : argc>1 && strcmp(argv[1],"--ui-polish-only")==0
        ? MainWndRegressionAccess::CheckUiMetrics(*window) + MainWndRegressionAccess::CheckUiPolish(*window, root)
        : MainWndRegressionAccess::Run(*window, fixture);
    if(!activationOnly && !(argc>1 && (strcmp(argv[1],"--delete-permission-check")==0 || strcmp(argv[1],"--delete-partial-check")==0)))
        failures+=MainWndRegressionAccess::CheckPreferences(*window,root);
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
