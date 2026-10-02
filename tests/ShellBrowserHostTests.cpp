#include "ShellBrowserHost.h"
#include "ShellPresentation.h"

#include <Windows.h>
#include <objbase.h>

#include <iostream>
#include <string>
#include <propkey.h>

struct ShellBrowserHostTestAccess {
    static inline int menuRequests=0;
    static LRESULT CALLBACK MenuProbe(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
        if(message==WM_APP+4) {++menuRequests;return 1;}
        return DefSubclassProc(window,message,wp,lp);
    }
    static bool ContextMenuRouting(ShellBrowserHost& host) {
        menuRequests=0;
        SetWindowSubclass(host.m_parent,MenuProbe,41,0);
        SendMessageW(host.m_listWindow,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(host.m_listWindow),MAKELPARAM(20,20));
        SendMessageW(host.m_viewWindow,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(host.m_listWindow),MAKELPARAM(-1,-1));
        RECT row{};ListView_GetItemRect(host.m_listWindow,0,&row,LVIR_BOUNDS);
        const LPARAM point=MAKELPARAM(row.left+8,(row.top+row.bottom)/2);
        // A stuck native popup must fail the test rather than block the suite.
        SetTimer(host.m_parent,42,500,[](HWND,UINT,UINT_PTR,DWORD){EndMenu();});
        PostMessageW(host.m_listWindow,WM_RBUTTONDOWN,MK_RBUTTON,point);
        PostMessageW(host.m_listWindow,WM_RBUTTONUP,0,point);
        const DWORD deadline=GetTickCount()+700;
        while(menuRequests<3 && GetTickCount()<deadline) {
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_APP+1) {delete reinterpret_cast<std::wstring*>(message.lParam);continue;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }Sleep(5);
        }
        KillTimer(host.m_parent,42);
        RemoveWindowSubclass(host.m_parent,MenuProbe,41);
        return menuRequests==3;
    }
    static bool NavigationSpacing(ShellBrowserHost& host,const std::wstring& path,FOLDERVIEWMODE mode) {
        if(!host.SetViewMode(mode) || !host.Navigate(path))return false;
        const DWORD deadline=GetTickCount()+3000;
        bool completed=false;
        while(GetTickCount()<deadline) {
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                // No parent reapplication of mode: the newly created view must
                // already be styled when navigation completion is delivered.
                if(message.message==WM_APP+1) {delete reinterpret_cast<std::wstring*>(message.lParam);completed=true;continue;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            RECT row{};
            if(completed && host.m_listWindow && ListView_GetItemRect(host.m_listWindow,0,&row,LVIR_BOUNDS)) {
                UINT current=0;IFolderView2* view=View(host);
                if(view){view->GetCurrentViewMode(&current);view->Release();}
                return current==mode && host.m_listSpacer && host.m_groupingPath==host.m_lastNavigation
                    && abs(row.bottom-row.top-MulDiv(26,host.m_dpi,96))<=1;
            }
            Sleep(5);
        }
        return false;
    }
    static bool AssociationBadgeRegression() {
        const std::wstring key=L"Software\\FastFileBadgeTest_"+std::to_wstring(GetCurrentProcessId());
        HKEY isolated=nullptr;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,key.c_str(),0,nullptr,0,KEY_ALL_ACCESS,nullptr,&isolated,nullptr))return false;
        bool ok=RegOverridePredefKey(HKEY_CLASSES_ROOT,isolated)==ERROR_SUCCESS;
        auto write=[&](const wchar_t* subkey,const wchar_t* name,const wchar_t* value) {
            HKEY entry=nullptr;
            if(RegCreateKeyExW(isolated,subkey,0,nullptr,0,KEY_ALL_ACCESS,nullptr,&entry,nullptr))return false;
            const auto result=RegSetValueExW(entry,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value),DWORD((wcslen(value)+1)*sizeof(wchar_t)));
            RegCloseKey(entry);return result==ERROR_SUCCESS;
        };
        if(ok) {
            ok=write(L".ffaspect",nullptr,L"FastFile.Badge") &&
                write(L"FastFile.Badge",L"TypeOverlay",L"%SystemRoot%\\System32\\shell32.dll,0");
            ShellBrowserHost host;
            HICON icon=host.AssociatedAppIcon(L"C:\\image.ffaspect");
            ok=icon && host.AssociatedAppIcon(L"C:\\other.ffaspect")==icon && ok;
            host.ClearItemImages();
            ok=host.m_associatedIcons.empty() && ok;
            ok=write(L"FastFile.Badge",L"TypeOverlay",L"") && ok;
            ok=host.AssociatedAppIcon(L"C:\\image.ffaspect")==nullptr && ok;
        }
        RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr);
        RegCloseKey(isolated);RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str());
        return ok;
    }
    static inline std::vector<std::wstring> launched;
    static inline bool launchInfoValid=true, launchSucceeds=true;
    static inline std::wstring expectedAssociation;
    static BOOL WINAPI RecordDefaultOpen(SHELLEXECUTEINFOW* info) {
        launchInfoValid=launchInfoValid && info && info->cbSize==sizeof(*info)
            && !info->lpVerb && !info->hkeyClass && info->nShow==SW_SHOWNORMAL
            && (expectedAssociation.empty() ? !info->lpClass && info->fMask==SEE_MASK_NOASYNC
                : info->lpClass && expectedAssociation==info->lpClass
                    && info->fMask==(SEE_MASK_NOASYNC|SEE_MASK_CLASSNAME));
        if(info && info->lpFile)launched.emplace_back(info->lpFile);
        SetLastError(ERROR_CANCELLED);
        return launchSucceeds;
    }
    static bool MissingAssociationFallback() {
        const std::wstring path=L"C:\\中文 folder\\image.ffunregistered"+std::to_wstring(GetCurrentProcessId());
        launched.clear();launchInfoValid=true;launchSucceeds=true;expectedAssociation.clear();
        return ShellPresentation::DefaultFileAssociation(path).empty()
            && ShellPresentation::OpenDefaultFile(nullptr,path,RecordDefaultOpen)
            && launchInfoValid && launched.size()==1 && launched.front()==path;
    }
    static bool DefaultOpen(ShellBrowserHost& host,const std::wstring& expected,bool succeeds=true) {
        launched.clear();launchInfoValid=true;launchSucceeds=succeeds;
        expectedAssociation=ShellPresentation::DefaultFileAssociation(expected);
        IShellView* view=nullptr;
        if(FAILED(host.m_browser->GetCurrentView(IID_PPV_ARGS(&view))))return false;
        const HRESULT result=host.DefaultCommand(view,RecordDefaultOpen);
        view->Release();
        if(expected.empty())return result==S_FALSE && launched.empty();
        return result==S_OK && launchInfoValid && launched.size()==1 && launched.front()==expected;
    }
    static bool InternalFolderOpen(ShellBrowserHost& host,const std::wstring& expected) {
        launched.clear();IShellView* view=nullptr;
        if(FAILED(host.m_browser->GetCurrentView(IID_PPV_ARGS(&view))))return false;
        const HRESULT result=host.DefaultCommand(view,RecordDefaultOpen);view->Release();
        MSG request{};bool matched=false;
        if(PeekMessageW(&request,host.m_parent,WM_APP+3,WM_APP+3,PM_REMOVE)) {
            auto* target=reinterpret_cast<std::wstring*>(request.lParam);
            matched=target && *target==expected && request.wParam==0;delete target;
        }
        return result==S_OK && launched.empty() && matched;
    }
    static bool ListIconVisible(ShellBrowserHost& host) {
        int w=0,h=0;
        if(!ImageList_GetIconSize(host.m_shellSmallImages,&w,&h) || h!=MulDiv(16,host.m_dpi,96))return false;
        HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,640,480);auto old=SelectObject(dc,bitmap);
        RECT canvas{0,0,640,480};FillRect(dc,&canvas,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        NMLVCUSTOMDRAW draw{};draw.nmcd.hdc=dc;draw.nmcd.dwItemSpec=0;draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;
        host.DrawListIcon(&draw);
        if((GetWindowLongPtrW(host.m_listWindow,GWL_STYLE)&LVS_TYPEMASK)==LVS_REPORT) {
            draw.nmcd.dwDrawStage=CDDS_ITEMPOSTPAINT;host.DrawListIcon(&draw);
        }
        RECT icon{},row{};ListView_GetItemRect(host.m_listWindow,0,&icon,LVIR_ICON);
        ListView_GetItemRect(host.m_listWindow,0,&row,LVIR_BOUNDS);
        int colored=0;
        for(int y=row.top;y<row.bottom;++y)for(int x=icon.left;x<icon.left+w;++x)
            colored+=GetPixel(dc,x,y)!=RGB(255,255,255);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
        return colored>10;
    }
    static bool InternalImageRefresh(ShellBrowserHost& host) {
        HIMAGELIST original=host.m_shellSmallImages;
        HIMAGELIST proxy=ImageList_Create(16,16,ILC_COLOR32,1,1);ImageList_SetImageCount(proxy,1);
        const UINT_PTR id=reinterpret_cast<UINT_PTR>(&host);
        RemoveWindowSubclass(host.m_listWindow,ShellBrowserHost::ListSubclass,id);
        ListView_SetImageList(host.m_listWindow,proxy,LVSIL_SMALL);
        SetWindowSubclass(host.m_listWindow,ShellBrowserHost::ListSubclass,id,reinterpret_cast<DWORD_PTR>(&host));
        InvalidateRect(host.m_listWindow,nullptr,FALSE);SendMessageW(host.m_listWindow,WM_PAINT,0,0);
        RECT row{};ListView_GetItemRect(host.m_listWindow,0,&row,LVIR_BOUNDS);
        const bool ok=host.m_shellSmallImages==original && abs(row.bottom-row.top-MulDiv(26,host.m_dpi,96))<=1
            && ListIconVisible(host);
        ImageList_Destroy(proxy);return ok;
    }
    static HBITMAP Normalize(HBITMAP image) {return ShellBrowserHost::NormalizeImageAlpha(image);}
    static bool DriveTileHeight(ShellBrowserHost& host) {
        LVTILEVIEWINFO info{};info.cbSize=sizeof(info);info.dwMask=LVTVIM_TILESIZE;
        ListView_GetTileViewInfo(host.m_listWindow,&info);
        return host.m_customTileHeight && (info.dwFlags&LVTVIF_FIXEDHEIGHT)
            && info.sizeTile.cy==MulDiv(64,host.m_dpi,96)
            && abs(info.sizeTile.cx-host.m_originalTileInfo.sizeTile.cx)<=1;
    }
    static bool AlphaRegression() {
        auto run=[](DWORD value,DWORD expected) {
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=1;info.bmiHeader.biHeight=-1;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
            void* bits=nullptr;HBITMAP image=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            if(!image)return false;
            *static_cast<DWORD*>(bits)=value;image=Normalize(image);
            BITMAP bitmap{};GetObjectW(image,sizeof(bitmap),&bitmap);
            const bool ok=bitmap.bmBits && *static_cast<DWORD*>(bitmap.bmBits)==expected;
            DeleteObject(image);return ok;
        };
        return run(0x40C86432,0x4032190D) && run(0x4032190D,0x4032190D)
            && run(0x00FFFFFF,0) && run(0xFFC86432,0xFFC86432);
    }
    static HWND List(ShellBrowserHost& host) { return host.m_listWindow; }
    static bool Borderless(ShellBrowserHost& host) {
        EXPLORER_BROWSER_OPTIONS options=EBO_NONE;
        if (FAILED(host.m_browser->GetOptions(&options)) || !(options & EBO_NOBORDER)) return false;
        for (HWND frame=host.m_listWindow; frame && frame!=host.m_parent; frame=GetParent(frame)) {
            if ((GetWindowLongPtrW(frame,GWL_STYLE) & WS_BORDER)
                || (GetWindowLongPtrW(frame,GWL_EXSTYLE) & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE))) return false;
        }
        return host.m_listWindow != nullptr;
    }
    static int Slot(ShellBrowserHost& host) { return host.m_iconSlot; }
    static bool SelectedCellColor(ShellBrowserHost& host, bool selected = true) {
        HWND list=host.m_listWindow;
        POINT point{}; ListView_GetItemPosition(list,0,&point);
        const int previousHot=host.m_hotItem;
        if(!selected) host.m_hotItem=0;
        HDC screen=GetDC(nullptr), dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,640,480);
        auto old=SelectObject(dc,bitmap);
        NMLVCUSTOMDRAW draw{};
        draw.nmcd.hdr.hwndFrom=list; draw.nmcd.hdr.code=NM_CUSTOMDRAW;
        draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT; draw.nmcd.dwItemSpec=0; draw.nmcd.hdc=dc;
        const auto result=SendMessageW(host.m_viewWindow,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&draw));
        const bool correct=result==CDRF_SKIPDEFAULT && GetPixel(dc,point.x+3,point.y+3)==
            (selected ? RGB(0xE5,0xF1,0xFB) : RGB(0xF5,0xF5,0xF5));
        host.m_hotItem=previousHot;
        SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr,screen);
        return correct;
    }
    static IFolderView2* View(ShellBrowserHost& host) {
        IFolderView2* view = nullptr;
        host.m_browser->GetCurrentView(IID_PPV_ARGS(&view));
        return view;
    }
};

namespace {
bool TestLocalizedAssociations() {
    const std::wstring id=L"FastFile.TypeTest."+std::to_wstring(GetCurrentProcessId());
    const std::wstring ext=L".fftype"+std::to_wstring(GetCurrentProcessId());
    const std::wstring prefix=L"Software\\Classes\\";
    const std::wstring resource=L"@%SystemRoot%\\System32\\shell32.dll,-30596";
    auto value=[&](const std::wstring& key,const wchar_t* name,const std::wstring& text) {
        HKEY handle=nullptr;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,(prefix+key).c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&handle,nullptr)!=ERROR_SUCCESS) return false;
        const auto error=RegSetValueExW(handle,name,0,REG_SZ,reinterpret_cast<const BYTE*>(text.c_str()),DWORD((text.size()+1)*sizeof(wchar_t)));
        RegCloseKey(handle);return error==ERROR_SUCCESS;
    };
    wchar_t expected[512]{};
    bool ok=SUCCEEDED(SHLoadIndirectString(resource.c_str(),expected,_countof(expected),nullptr));
    ok=value(ext,nullptr,id) && value(id,nullptr,L"English Image file") &&
        value(id+L".Localized",L"FriendlyTypeName",resource) &&
        value(ext+L"\\OpenWithProgids",(id+L".Localized").c_str(),L"") && ok;
    if(ok) ok=ShellPresentation::LocalizedTypeName(L"C:\\image"+ext)==expected;
    for(const auto& key:{ext+L"\\OpenWithProgids",ext,id+L".Localized",id})
        RegDeleteKeyW(HKEY_CURRENT_USER,(prefix+key).c_str());
    return ok;
}

int Fail(const char* message, ShellBrowserHost* host = nullptr, HWND parent = nullptr,
    const std::wstring& folder = {}, const std::wstring& file = {})
{
    if (host) host->Destroy();
    if (parent) ::DestroyWindow(parent);
    if (!file.empty()) ::DeleteFileW(file.c_str());
    if (!folder.empty()) ::RemoveDirectoryW(folder.c_str());
    std::cerr << "FAIL " << message << '\n';
    return 1;
}

} // namespace

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com))
        return Fail("CoInitializeEx failed");
    if(!ShellBrowserHostTestAccess::MissingAssociationFallback()) return Fail("unassociated Unicode files must retain normal Shell association UI fallback");
    if(!ShellBrowserHostTestAccess::AssociationBadgeRegression()) return Fail("association badges honor Shell TypeOverlay, cache ownership and explicit suppression");
    if(!ShellBrowserHostTestAccess::AlphaRegression()) return Fail("transparent edges must blend without color overflow or double premultiplication");
    {
        IShellItemImageFactory* factory=nullptr;
        SHCreateItemFromParsingName(L"C:\\",nullptr,IID_PPV_ARGS(&factory));
        for(int size:{192,240}) {
            HBITMAP image=nullptr;
            if(factory && SUCCEEDED(factory->GetImage({size,size},SIIGBF_ICONONLY,&image))) {
                image=ShellBrowserHostTestAccess::Normalize(image);
                BITMAP bitmap{};GetObjectW(image,sizeof(bitmap),&bitmap);
                BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth=bitmap.bmWidth;info.bmiHeader.biHeight=-bitmap.bmHeight;
                info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
                std::vector<DWORD> pixels(bitmap.bmWidth*bitmap.bmHeight);
                HDC dc=GetDC(nullptr);GetDIBits(dc,image,0,bitmap.bmHeight,pixels.data(),&info,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);
                int invalid=0,partial=0,invalidPartial=0;
                for(DWORD p:pixels) { const DWORD a=p>>24;partial+=a>0 && a<255;
                    const bool bad=((p&255)>a || ((p>>8)&255)>a || ((p>>16)&255)>a);invalid+=bad;invalidPartial+=bad && a>0; }
                DeleteObject(image);
                if(invalid) return Fail("real Shell drive icon retains invalid alpha edge pixels");
            }
        }
        if(factory)factory->Release();
    }
    if (!TestLocalizedAssociations()) return Fail("Shell MUI friendly type must take precedence over an English association label");

    wchar_t temp[MAX_PATH] = {};
    if (!::GetTempPathW(MAX_PATH, temp)) {
        ::CoUninitialize();
        return Fail("GetTempPathW failed");
    }
    std::wstring folder = temp;
    folder += L"FastFileShellView_";
    folder += std::to_wstring(::GetCurrentProcessId());
    if (!::CreateDirectoryW(folder.c_str(), nullptr)) {
        ::CoUninitialize();
        return Fail("CreateDirectoryW failed");
    }
    const std::wstring file = folder + L"\\native-view.txt";
    HANDLE fixture = ::CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fixture == INVALID_HANDLE_VALUE) {
        const int result = Fail("could not create fixture", nullptr, nullptr, folder);
        ::CoUninitialize();
        return result;
    }
    ::CloseHandle(fixture);
    const auto counts = ShellPresentation::CountChildren(folder, false);
    if (counts.error || counts.folders != 0 || counts.files != 1)
        return Fail("folder summary must enumerate native folder, not empty legacy cache", nullptr, nullptr, folder, file);
    const auto chain = ShellPresentation::AncestorPaths(folder);
    if (chain.empty() || chain.back() != folder || chain.front().size() != 3) {
        std::wcerr << L"Target: " << folder << L'\n';
        for (const auto& part : chain) std::wcerr << L"Ancestor: " << part << L'\n';
        return Fail("PIDL ancestor chain must reach drive and target", nullptr, nullptr, folder, file);
    }
    const std::wstring nested = folder + L"\\nested";
    CreateDirectoryW(nested.c_str(), nullptr);
    const auto withFolder = ShellPresentation::CountChildren(folder, false);
    const auto emptyFolder = ShellPresentation::CountChildren(nested, false);
    RemoveDirectoryW(nested.c_str());
    if (withFolder.folders != 1 || withFolder.files != 1 || emptyFolder.folders || emptyFolder.files || emptyFolder.error)
        return Fail("folder counts must distinguish non-empty and truly empty folders", nullptr, nullptr, folder, file);
    const auto props = ShellPresentation::ReadProperties(file);
    if (props.name != L"native-view.txt" || props.type.empty() || !props.hasSize)
        return Fail("selected object Shell properties missing", nullptr, nullptr, folder, file);
    if (!ShellPresentation::CountChildren(folder + L"\\missing", false).error)
        return Fail("missing folder must not appear empty", nullptr, nullptr, folder, file);

    HWND parent = ::CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
        0, 0, 640, 480, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (!parent) {
        const int result = Fail("could not create host window", nullptr, nullptr, folder, file);
        ::CoUninitialize();
        return result;
    }
    ::ShowWindow(parent, SW_HIDE);

    ShellBrowserHost host;
    const RECT bounds = { 0, 0, 640, 480 };
    if (!host.Create(parent, bounds, WM_APP + 1, WM_APP + 2, WM_APP + 3, WM_APP + 4)) {
        const int result = Fail("IExplorerBrowser initialization failed", &host, parent, folder, file);
        ::CoUninitialize();
        return result;
    }
    if (!host.Navigate(folder) || !host.IsAtPath(folder)) {
        const int result = Fail("native Shell navigation failed", &host, parent, folder, file);
        ::CoUninitialize();
        return result;
    }
    if (!host.SetViewMode(FVM_DETAILS) || !host.SetSort(0, true)) {
        const int result = Fail("native Shell view mode or sort failed", &host, parent, folder, file);
        ::CoUninitialize();
        return result;
    }
    for (auto mode : { FVM_ICON, FVM_TILE, FVM_LIST, FVM_THUMBNAIL, FVM_DETAILS }) {
        if (!host.SetViewMode(mode, mode == FVM_ICON ? 72 : -1))
            return Fail("view switch failed", &host, parent, folder, file);
        IFolderView2* view = ShellBrowserHostTestAccess::View(host);
        DWORD flags = 0;
        if (!view || FAILED(view->GetCurrentFolderFlags(&flags)))
            return Fail("could not inspect view flags", &host, parent, folder, file);
        view->Release();
        const DWORD headerMask = FWF_NOCOLUMNHEADER | FWF_NOHEADERINALLVIEWS;
        if ((flags & headerMask) != (mode == FVM_DETAILS ? 0 : headerMask))
            return Fail("column header leaked into a non-details view", &host, parent, folder, file);
    }
    host.Refresh();
    const UINT dpi = GetDpiForWindow(parent);
    for (int logical : {128, 160}) {
        const int slot = MulDiv(logical, dpi, 96);
        if (!host.SetViewMode(FVM_ICON, slot) || !ShellBrowserHostTestAccess::List(host)
            || ShellBrowserHostTestAccess::Slot(host) != slot)
            return Fail("large icons must use customizable native Shell cells at window DPI", &host, parent, folder, file);
        const DWORD spacing = ListView_GetItemSpacing(ShellBrowserHostTestAccess::List(host), FALSE);
        if (LOWORD(spacing) != slot + MulDiv(16, dpi, 96) || HIWORD(spacing) != slot + MulDiv(36, dpi, 96))
            return Fail("large icon slot and single-line row have fixed geometry", &host, parent, folder, file);
    }
    if (!ShellBrowserHostTestAccess::Borderless(host))
        return Fail("Shell host must leave flat dividers to DuiLib instead of drawing an inset frame", &host, parent, folder, file);
    host.SetViewMode(FVM_DETAILS);
    std::vector<std::pair<std::wstring, bool>> selected;
    IFolderView2* view = ShellBrowserHostTestAccess::View(host);
    PROPERTYKEY initialGrouping{};BOOL initialAscending=FALSE;
    view->GetGroupBy(&initialGrouping,&initialAscending);
    for(int grouping : {0,1,2}) {
        PROPERTYKEY actual{};BOOL ascending=FALSE;
        const PROPERTYKEY expected=grouping==1?PKEY_DateModified:grouping==2?PKEY_ItemTypeText:PKEY_Null;
        if(!host.SetGrouping(grouping) || FAILED(view->GetGroupBy(&actual,&ascending)) || !IsEqualPropertyKey(actual,expected))
            return Fail("settings grouping must reach the native Shell view",&host,parent,folder,file);
    }
    if(!host.SetGrouping(0) || !host.SetGrouping(-1))
        return Fail("Windows grouping preference must remain available",&host,parent,folder,file);
    PROPERTYKEY restoredGrouping{};BOOL restoredAscending=FALSE;
    if(FAILED(view->GetGroupBy(&restoredGrouping,&restoredAscending)) || !IsEqualPropertyKey(initialGrouping,restoredGrouping)
        || initialAscending!=restoredAscending)
        return Fail("returning to Windows grouping must restore the original folder grouping",&host,parent,folder,file);
    // Shell enumeration is asynchronous. Pump until the fixture is available.
    const DWORD deadline = GetTickCount() + 5000;
    int itemCount = 0;
    while (GetTickCount() < deadline) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_APP + 1) { delete reinterpret_cast<std::wstring*>(message.lParam); continue; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (SUCCEEDED(view->ItemCount(SVGIO_ALLVIEW, &itemCount)) && itemCount == 1) break;
        Sleep(10);
    }
    if (itemCount != 1 || FAILED(view->SelectItem(0, SVSI_SELECT | SVSI_DESELECTOTHERS)))
        return Fail("native selection fixture unavailable", &host, parent, folder, file);
    if (!host.GetSelection(selected) || selected.size() != 1 || selected[0].first != file)
        return Fail("native selection must bind to selected item", &host, parent, folder, file);
    if(!ShellBrowserHostTestAccess::DefaultOpen(host,file) || !ShellBrowserHostTestAccess::DefaultOpen(host,file,false))
        return Fail("native default command must open the selected file once using Windows default, even on cancellation",&host,parent,folder,file);
    host.EnsureSelectionVisible();
    if (!host.GetSelection(selected) || selected.size()!=1 || selected[0].first!=file)
        return Fail("revealing selected native item must preserve its selection", &host, parent, folder, file);
    if (!host.SetViewMode(FVM_ICON,MulDiv(128,dpi,96)) || !ShellBrowserHostTestAccess::SelectedCellColor(host))
        return Fail("native selected icon must paint the requested whole-cell blue fill", &host, parent, folder, file);
    view->SelectItem(0, SVSI_DESELECT);
    if(!ShellBrowserHostTestAccess::SelectedCellColor(host,false))
        return Fail("unselected hovered native icon must paint neutral hover fill", &host, parent, folder, file);
    view->Release();
    if (!host.GetSelection(selected) || !selected.empty())
        return Fail("cleared native selection must restore current folder summary", &host, parent, folder, file);
    if(!ShellBrowserHostTestAccess::DefaultOpen(host,L""))
        return Fail("no selection must not launch the current folder",&host,parent,folder,file);
    host.SetViewMode(FVM_LIST);
    const DWORD listDeadline=GetTickCount()+700;
    while(GetTickCount()<listDeadline) {
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_APP+1) {delete reinterpret_cast<std::wstring*>(message.lParam);continue;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        Sleep(5);
    }
    RECT listRow{};ListView_GetItemRect(ShellBrowserHostTestAccess::List(host),0,&listRow,LVIR_BOUNDS);
    if(abs(listRow.bottom-listRow.top-MulDiv(26,dpi,96))>1)
        return Fail("list rows must reserve 26 logical pixels at window DPI",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::ListIconVisible(host))
        return Fail("roomier list rows retain real Shell icons at their original physical size",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::InternalImageRefresh(host))
        return Fail("internal Shell image refresh must retain spacing and original icon source",&host,parent,folder,file);
    host.SetViewMode(FVM_DETAILS);
    RECT detailsRow{};ListView_GetItemRect(ShellBrowserHostTestAccess::List(host),0,&detailsRow,LVIR_BOUNDS);
    if(abs(detailsRow.bottom-detailsRow.top-MulDiv(26,dpi,96))>1)
        return Fail("details and list must share non-compact row spacing",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::ListIconVisible(host))
        return Fail("non-compact details retain original Shell icons",&host,parent,folder,file);
    const auto spacer=ListView_GetImageList(ShellBrowserHostTestAccess::List(host),LVSIL_SMALL);
    host.SetViewMode(FVM_DETAILS);
    if(ListView_GetImageList(ShellBrowserHostTestAccess::List(host),LVSIL_SMALL)!=spacer)
        return Fail("reapplying the same mode must not restore a compact image list",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::ContextMenuRouting(host))
        return Fail("mouse and keyboard native view menus must reach the host command router",&host,parent,folder,file);
    CreateDirectoryW(nested.c_str(),nullptr);
    CopyFileW(file.c_str(),(nested+L"\\item.txt").c_str(),FALSE);
    bool navigationSpacing=true;
    for(auto mode:{FVM_LIST,FVM_DETAILS}) {
        navigationSpacing=ShellBrowserHostTestAccess::NavigationSpacing(host,nested,mode) && navigationSpacing;
        navigationSpacing=ShellBrowserHostTestAccess::NavigationSpacing(host,folder,mode) && navigationSpacing;
    }
    DeleteFileW((nested+L"\\item.txt").c_str());RemoveDirectoryW(nested.c_str());
    if(!navigationSpacing)return Fail("fresh folder views must have non-compact spacing before the parent navigation handler",&host,parent,folder,file);
    if(!host.Navigate(L"::ThisPC")) return Fail("drive spacing fixture navigation failed",&host,parent,folder,file);
    const DWORD driveDeadline=GetTickCount()+3000;
    while(GetTickCount()<driveDeadline) {
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_APP+1) {delete reinterpret_cast<std::wstring*>(message.lParam);continue;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(host.IsAtPath(L"::ThisPC")) break;
        Sleep(10);
    }
    for(int repeat=0;repeat<2;++repeat) {
        host.SetViewMode(FVM_TILE);
        if(!ShellBrowserHostTestAccess::DriveTileHeight(host))
            return Fail("drive tiles must reserve 64 logical pixels without cumulative growth",&host,parent,folder,file);
    }
    const DWORD driveSelectionDeadline=GetTickCount()+5000;
    while(GetTickCount()<driveSelectionDeadline) {
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_APP+1) {delete reinterpret_cast<std::wstring*>(message.lParam);continue;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        IFolderView2* populated=ShellBrowserHostTestAccess::View(host);
        int count=0;if(populated) {populated->ItemCount(SVGIO_ALLVIEW,&count);populated->Release();}
        if(count>0)break;
        Sleep(10);
    }
    IFolderView2* drives=ShellBrowserHostTestAccess::View(host);
    if(!drives || FAILED(drives->SelectItem(0,SVSI_SELECT|SVSI_DESELECTOTHERS)))
        return Fail("drive default command selection unavailable",&host,parent,folder,file);
    IShellItem* drive=nullptr;PWSTR drivePath=nullptr;
    if(FAILED(drives->GetItem(0,IID_PPV_ARGS(&drive))) || FAILED(drive->GetDisplayName(SIGDN_FILESYSPATH,&drivePath)))
        return Fail("filesystem drive path unavailable",&host,parent,folder,file);
    const std::wstring selectedDrive=drivePath;CoTaskMemFree(drivePath);drive->Release();drives->Release();
    if(!ShellBrowserHostTestAccess::InternalFolderOpen(host,selectedDrive))
        return Fail("drive default command must request internal navigation without launching a Shell verb",&host,parent,folder,file);
    // A ZIP may expose SFGAO_FOLDER although its filesystem object is a file.
    // Preserve native compressed-folder browsing instead of treating it as a disk directory.
    const std::wstring archive=folder+L"\\native-archive.zip";
    const BYTE emptyZip[22]={0x50,0x4b,0x05,0x06};DWORD archiveWritten=0;
    HANDLE archiveFile=CreateFileW(archive.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    WriteFile(archiveFile,emptyZip,sizeof(emptyZip),&archiveWritten,nullptr);CloseHandle(archiveFile);
    host.Navigate(folder);bool archiveSelected=false,archiveIsFolder=false;
    const DWORD archiveDeadline=GetTickCount()+4000;
    while(!archiveSelected && GetTickCount()<archiveDeadline) {
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_APP+1){delete reinterpret_cast<std::wstring*>(message.lParam);continue;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        IFolderView2* archiveView=ShellBrowserHostTestAccess::View(host);int n=0;
        if(archiveView) {
            archiveView->ItemCount(SVGIO_ALLVIEW,&n);
            for(int i=0;i<n;++i) {
                IShellItem* item=nullptr;PWSTR path=nullptr;
                if(SUCCEEDED(archiveView->GetItem(i,IID_PPV_ARGS(&item)))) {
                    if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
                        if(archive==path) {
                            SFGAOF attrs=0;item->GetAttributes(SFGAO_FOLDER,&attrs);archiveIsFolder=(attrs&SFGAO_FOLDER)!=0;
                            archiveSelected=SUCCEEDED(archiveView->SelectItem(i,SVSI_SELECT|SVSI_DESELECTOTHERS));
                        }CoTaskMemFree(path);
                    }item->Release();
                }
            }archiveView->Release();
        }Sleep(5);
    }
    const bool archiveHandled=archiveSelected && ShellBrowserHostTestAccess::DefaultOpen(host,archiveIsFolder?L"":archive);
    DeleteFileW(archive.c_str());
    if(!archiveHandled)return Fail("archive folders retain native browsing and ordinary archive files retain default application opening",&host,parent,folder,file);
    host.Destroy();
    ::DestroyWindow(parent);
    ::DeleteFileW(file.c_str());
    ::RemoveDirectoryW(folder.c_str());
    ::CoUninitialize();
    std::cout << "ShellBrowserHostTests: native browser creation/navigation/view operations passed\n";
    return 0;
}
