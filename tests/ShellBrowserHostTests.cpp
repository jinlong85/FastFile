#include "ShellBrowserHost.h"
#include "ShellPresentation.h"
#include "ShellMenuUtil.h"

#include <Windows.h>
#include <objbase.h>

#include <iostream>
#include <fstream>
#include <string>
#include <propkey.h>

struct ShellBrowserHostTestAccess {
    static bool NavigationRecovery(HWND parent, const std::wstring& folder) {
        ShellBrowserHost host;
        if(!host.Create(parent,{0,0,640,480},WM_APP+1,WM_APP+2))return false;
        int failures=0;
        auto check=[&](bool ok,const char* message) {
            if(!ok){++failures;std::cerr<<"navigation recovery: "<<message<<'\n';}
        };
        auto pump=[&]() {
            host.PollNavigation();
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_APP+1){delete reinterpret_cast<std::wstring*>(message.lParam);continue;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }
        };
        auto settled=[&](const std::wstring& path,int expectedCount=1) {
            const ULONGLONG until=GetTickCount64()+3000;
            do {
                pump();
                IFolderView2* view=nullptr;int count=0;
                if(SUCCEEDED(host.m_browser->GetCurrentView(IID_PPV_ARGS(&view)))) {
                    view->ItemCount(SVGIO_ALLVIEW,&count);view->Release();
                }
                if(host.IsNavigationCompleteAt(path) && count==expectedCount)return true;
                Sleep(5);
            }while(GetTickCount64()<until);
            return false;
        };
        // EventSink's first base is IExplorerBrowserEvents. Exercise the same COM
        // callbacks as ExplorerBrowser without relying on a machine-specific delay.
        auto* events=reinterpret_cast<IExplorerBrowserEvents*>(host.m_events);
        PIDLIST_ABSOLUTE target=nullptr;SHParseDisplayName(folder.c_str(),nullptr,&target,0,nullptr);
        host.m_lastNavigation=folder; // request submitted, but no Shell view exists
        events->OnNavigationPending(target);
        check(!host.IsAtPath(folder),"a requested path must not count as a displayed folder");
        events->OnNavigationFailed(target);
        check(!host.IsAtPath(folder),"failed navigation must not count as successful");
        check(host.Navigate(folder),"same-path retry is accepted after failure");
        check(settled(folder),"same-path retry must actually enumerate the file");

        const std::wstring next=folder+L"\\retry";
        CreateDirectoryW(next.c_str(),nullptr);
        CopyFileW((folder+L"\\native-view.txt").c_str(),(next+L"\\native-view.txt").c_str(),FALSE);
        PIDLIST_ABSOLUTE nextTarget=nullptr;SHParseDisplayName(next.c_str(),nullptr,&nextTarget,0,nullptr);
        host.m_lastNavigation=next;
        events->OnNavigationPending(nextTarget);
        events->OnNavigationComplete(target); // late callback from the old request
        check(host.CurrentPath()==next,"old completion must not replace the newer target");
        events->OnNavigationFailed(target); // old failure must not cancel the newer request
        check(host.CurrentPath()==next,"old failure must not replace the newer target");
        events->OnNavigationFailed(nextTarget);
        host.Refresh();
        check(settled(next),"refresh recovers and enumerates the actual target after failure");
        bool accepted=true;
        for(int i=0;i<12;++i)accepted=host.Navigate(i%2?next:folder) && accepted;
        check(accepted && settled(next),"rapid requests are serialized and the latest target is populated");
        host.m_pendingNavigation=next;host.m_navigationDeadline=GetTickCount64()-1;
        host.PollNavigation();
        check(host.m_navigationFailed && !host.IsAtPath(next),"timed-out requests become retryable failures");
        host.Refresh();check(settled(next),"refresh recovers after a navigation timeout");
        host.SetBounds({0,0,640,480});
        IShellView* sizedView=nullptr;HWND nativeRoot=nullptr;
        if(SUCCEEDED(host.m_browser->GetCurrentView(IID_PPV_ARGS(&sizedView)))) {
            sizedView->GetWindow(&nativeRoot);sizedView->Release();
        }
        check(nativeRoot!=nullptr,"loaded native view is available for zero-size regression");
        if(nativeRoot) {
            SetWindowPos(nativeRoot,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            RECT collapsed{};GetClientRect(nativeRoot,&collapsed);
            check(collapsed.right==0 && collapsed.bottom==0 && !host.HasVisibleViewBounds(),"zero-sized native view cannot confirm handoff");
            host.SetBounds({0,0,640,480});pump();
            RECT restored{};GetClientRect(nativeRoot,&restored);
            check(restored.right>0 && restored.bottom>0,
                "unchanged outer bounds must relayout a newly zero-sized inner Shell view");
            check(!host.HasVisibleViewBounds(),"hidden parent cannot confirm even after bounds recover");
            check(host.IsNavigationCompleteAt(next),"layout recovery preserves completed navigation");
        }
        wchar_t logPath[32768]{};GetEnvironmentVariableW(L"FASTFILE_NAV_LOG",logPath,_countof(logPath));
        std::ifstream log(logPath,std::ios::binary);
        const std::string trace((std::istreambuf_iterator<char>(log)),std::istreambuf_iterator<char>());
        check(trace.find("event=failed")!=std::string::npos && trace.find("event=stale-complete")!=std::string::npos
            && trace.find("event=refresh-retry")!=std::string::npos && trace.find("event=timeout")!=std::string::npos
            && trace.find("listCount=1")!=std::string::npos,"diagnostics record failure, retry, timeout and actual populated view");
        log.close();
        host.Destroy();pump();
        CoTaskMemFree(target);CoTaskMemFree(nextTarget);
        DeleteFileW((next+L"\\native-view.txt").c_str());RemoveDirectoryW(next.c_str());
        return failures==0;
    }
    // 1.0.21: the view's right-click / menu key is DefView's own classic menu. The host must
    // not forward WM_CONTEXTMENU / NM_RCLICK anywhere; DefView itself tries to open a popup,
    // which a CBT hook blocks (counted) so nothing is ever shown.
    static inline int menuRequests=0;
    static inline int nativeMenus=0;
    static LRESULT CALLBACK MenuProbe(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
        if(message==WM_APP+4) {++menuRequests;return 1;}
        return DefSubclassProc(window,message,wp,lp);
    }
    static LRESULT CALLBACK BlockMenus(int code,WPARAM wp,LPARAM lp) {
        if(code==HCBT_CREATEWND) {
            wchar_t name[32]{};GetClassNameW(reinterpret_cast<HWND>(wp),name,_countof(name));
            if(wcscmp(name,L"#32768")==0) {++nativeMenus;return 1;}
        }
        return CallNextHookEx(nullptr,code,wp,lp);
    }
    static bool ContextMenuRouting(ShellBrowserHost& host) {
        menuRequests=0;nativeMenus=0;
        SetWindowSubclass(host.m_parent,MenuProbe,41,0);
        HHOOK block=SetWindowsHookExW(WH_CBT,BlockMenus,nullptr,GetCurrentThreadId());
        if(!block){RemoveWindowSubclass(host.m_parent,MenuProbe,41);return false;}
        // A popup that somehow escapes the hook must fail the test rather than block the suite.
        SetTimer(host.m_parent,42,500,[](HWND,UINT,UINT_PTR,DWORD){EndMenu();});
        RECT list{};GetWindowRect(host.m_listWindow,&list);
        SendMessageW(host.m_listWindow,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(host.m_listWindow),MAKELPARAM(list.left+20,list.top+20));
        SendMessageW(host.m_viewWindow,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(host.m_listWindow),MAKELPARAM(-1,-1));
        RECT row{};ListView_GetItemRect(host.m_listWindow,0,&row,LVIR_BOUNDS);
        const LPARAM point=MAKELPARAM(row.left+8,(row.top+row.bottom)/2);
        PostMessageW(host.m_listWindow,WM_RBUTTONDOWN,MK_RBUTTON,point);
        PostMessageW(host.m_listWindow,WM_RBUTTONUP,0,point);
        const DWORD deadline=GetTickCount()+700;
        while(GetTickCount()<deadline) {
            MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_APP+1) {delete reinterpret_cast<std::wstring*>(message.lParam);continue;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }Sleep(5);
        }
        KillTimer(host.m_parent,42);
        UnhookWindowsHookEx(block);
        RemoveWindowSubclass(host.m_parent,MenuProbe,41);
        if(menuRequests!=0 || nativeMenus<2)
            std::cerr<<"context menu diagnostic: forwarded="<<menuRequests<<" nativePopups="<<nativeMenus<<"\n";
        return menuRequests==0 && nativeMenus>=2;
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
    static BOOL WINAPI RecordDefaultOpen(SHELLEXECUTEINFOW* info) {
        launchInfoValid=launchInfoValid && info && info->cbSize==sizeof(*info)
            && !info->lpVerb && !info->hkeyClass && info->nShow==SW_SHOWNORMAL
            && !info->lpClass && info->fMask==(SEE_MASK_NOASYNC|SEE_MASK_INVOKEIDLIST);
        if(info && info->lpFile)launched.emplace_back(info->lpFile);
        SetLastError(ERROR_CANCELLED);
        return launchSucceeds;
    }
    static bool MissingAssociationFallback() {
        const std::wstring path=L"C:\\中文 folder\\image.ffunregistered"+std::to_wstring(GetCurrentProcessId());
        launched.clear();launchInfoValid=true;launchSucceeds=true;
        return ShellPresentation::OpenDefaultFile(nullptr,path,RecordDefaultOpen)
            && launchInfoValid && launched.size()==1 && launched.front()==path;
    }
    static bool DefaultOpen(ShellBrowserHost& host,const std::wstring& expected,bool succeeds=true) {
        launched.clear();launchInfoValid=true;launchSucceeds=succeeds;
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
    static bool ReapplyListBuffer(ShellBrowserHost& host) {
        // Shell can recreate/reset list styles without entering an icon view first.
        ListView_SetExtendedListViewStyleEx(host.m_listWindow,LVS_EX_DOUBLEBUFFER,0);
        IFolderView2* view=nullptr;
        if(FAILED(host.m_browser->GetCurrentView(IID_PPV_ARGS(&view))))return false;
        host.StyleNativeView(view); view->Release();
        if(!(ListView_GetExtendedListViewStyle(host.m_listWindow)&LVS_EX_DOUBLEBUFFER))return false;
        ListView_SetExtendedListViewStyleEx(host.m_listWindow,LVS_EX_DOUBLEBUFFER,0);
        host.Refresh();
        auto waitForItems=[&]() {
            const ULONGLONG deadline=GetTickCount64()+3000;
            ULONGLONG stableSince=0;
            do {
                MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                    if(message.message==WM_APP+1) {delete reinterpret_cast<std::wstring*>(message.lParam);continue;}
                    TranslateMessage(&message);DispatchMessageW(&message);
                }
                LVITEMW item{};item.mask=LVIF_IMAGE;item.iItem=0;
                if(ListView_GetItemCount(host.m_listWindow)>0 && ListView_GetItem(host.m_listWindow,&item) && item.iImage>=0) {
                    if(!stableSince)stableSince=GetTickCount64();
                    if(GetTickCount64()-stableSince>=100)return true;
                } else stableSince=0;
                Sleep(5);
            }while(GetTickCount64()<deadline);
            return false;
        };
        // Both native refresh and sort return before asynchronous rows are ready.
        if(!waitForItems())return false;
        if(!(ListView_GetExtendedListViewStyle(host.m_listWindow)&LVS_EX_DOUBLEBUFFER))return false;
        ListView_SetExtendedListViewStyleEx(host.m_listWindow,LVS_EX_DOUBLEBUFFER,0);
        host.SetSort(0,false);host.SetSort(0,true);
        if(!waitForItems())return false;
        // Exercise the subclass synchronization used by a real repaint even
        // though this test keeps its parent hidden.
        SendMessageW(host.m_listWindow,WM_PAINT,0,0);
        return (ListView_GetExtendedListViewStyle(host.m_listWindow)&LVS_EX_DOUBLEBUFFER)!=0;
    }
    static bool ListIconVisible(ShellBrowserHost& host) {
        if(!(ListView_GetExtendedListViewStyle(host.m_listWindow)&LVS_EX_DOUBLEBUFFER))return false;
        int w=0,h=0;
        if(!ImageList_GetIconSize(host.m_shellSmallImages,&w,&h) || h!=MulDiv(16,host.m_dpi,96)){std::cerr<<"icon size "<<w<<","<<h<<" items="<<ListView_GetItemCount(host.m_listWindow)<<"\n";return false;}
        HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        HBITMAP bitmap=CreateCompatibleBitmap(screen,640,480);auto old=SelectObject(dc,bitmap);
        RECT canvas{0,0,640,480};FillRect(dc,&canvas,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        NMLVCUSTOMDRAW draw{};draw.nmcd.hdc=dc;draw.nmcd.dwItemSpec=0;draw.nmcd.dwDrawStage=CDDS_ITEMPREPAINT;
        host.DrawListIcon(&draw);
        if(host.m_requestedMode==FVM_DETAILS || (GetWindowLongPtrW(host.m_listWindow,GWL_STYLE)&LVS_TYPEMASK)==LVS_REPORT) {
            draw.nmcd.dwDrawStage=CDDS_ITEMPOSTPAINT;host.DrawListIcon(&draw);
        }
        RECT icon{},row{};ListView_GetItemRect(host.m_listWindow,0,&icon,LVIR_ICON);
        ListView_GetItemRect(host.m_listWindow,0,&row,LVIR_BOUNDS);
        int colored=0;
        for(int y=row.top;y<row.bottom;++y)for(int x=icon.left;x<icon.left+w;++x)
            colored+=GetPixel(dc,x,y)!=RGB(255,255,255);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
        LVITEMW probe{};probe.mask=LVIF_IMAGE;probe.iItem=0;ListView_GetItem(host.m_listWindow,&probe);
        if(colored<=10)std::cerr<<"index="<<probe.iImage<<" sourceCount="<<ImageList_GetImageCount(host.m_shellSmallImages)<<" currentCount="<<ImageList_GetImageCount(ListView_GetImageList(host.m_listWindow,LVSIL_SMALL))<<" icon pixels="<<colored<<" row="<<row.left<<","<<row.top<<","<<row.right<<","<<row.bottom<<" items="<<ListView_GetItemCount(host.m_listWindow)<<"\n";
        return colored>10;
    }
    static bool TallImageRefresh(ShellBrowserHost& host) {
        const HIMAGELIST original=host.m_shellSmallImages;
        HIMAGELIST replacement=ImageList_Create(16,MulDiv(26,host.m_dpi,96),ILC_COLOR32,2,1);
        if(!replacement)return false;
        ImageList_SetImageCount(replacement,2);
        const UINT_PTR id=reinterpret_cast<UINT_PTR>(&host);
        auto replaceInternally=[&](HIMAGELIST images) {
            RemoveWindowSubclass(host.m_listWindow,ShellBrowserHost::ListSubclass,id);
            ListView_SetImageList(host.m_listWindow,images,LVSIL_SMALL);
            SetWindowSubclass(host.m_listWindow,ShellBrowserHost::ListSubclass,id,reinterpret_cast<DWORD_PTR>(&host));
            SendMessageW(host.m_listWindow,WM_PAINT,0,0);
        };
        replaceInternally(replacement);
        const bool ok=host.m_shellSmallImages==replacement
            && ListView_GetImageList(host.m_listWindow,LVSIL_SMALL)==host.m_listSpacer;
        replaceInternally(original); // restore before deleting our owned image list
        ImageList_Destroy(replacement);
        return ok;
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
    static bool FolderThumbCleanAlpha(ShellBrowserHost& host, const std::wstring& folderPath) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHParseDisplayName(folderPath.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl)
            return false;
        HBITMAP thumb = host.ExtractThumb(pidl, 72);
        CoTaskMemFree(pidl);
        if (!thumb) return false;
        BITMAP bm{}; GetObjectW(thumb, sizeof(bm), &bm);
        if (bm.bmBitsPixel != 32 || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
            DeleteObject(thumb);
            return false;
        }
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = bm.bmWidth; bi.bmiHeader.biHeight = -bm.bmHeight;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        std::vector<DWORD> pixels(size_t(bm.bmWidth) * bm.bmHeight);
        HDC dc = GetDC(nullptr);
        GetDIBits(dc, thumb, 0, bm.bmHeight, pixels.data(), &bi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        DeleteObject(thumb);
        const DWORD corner = pixels[0];
        return (corner >> 24) == 0;
    }
    static bool CheckDetailsColumnsAndIcons(ShellBrowserHost& host) {
        if (!host.SetViewMode(FVM_DETAILS)) return false;
        NMLVCUSTOMDRAW draw{};
        draw.nmcd.dwItemSpec = 0;
        draw.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
        draw.dwItemType = LVCDI_ITEM;
        if (host.DrawListIcon(&draw) != CDRF_NOTIFYPOSTPAINT) return false;
        const DWORD dl = GetTickCount() + 1500;
        while (GetTickCount() < dl && ListView_GetItemCount(host.m_listWindow) < 1) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            Sleep(5);
        }
        const int count = ListView_GetItemCount(host.m_listWindow);
        if (count < 1) return false;
        HIMAGELIST iml = nullptr;
        const int img = host.ResolveItemIcon(0, &iml);
        if (img < 0 || !iml || img >= ImageList_GetImageCount(iml)) return false;
        if (!host.SetViewMode(FVM_LIST)) return false;
        draw.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
        if (host.DrawListIcon(&draw) != CDRF_SKIPDEFAULT) return false;
        return true;
    }
    static bool CheckJpgListIconsAfterTileSwitch(ShellBrowserHost& host, const std::wstring& folder) {
        for (int i = 1; i <= 20; ++i) {
            wchar_t name[MAX_PATH]{};
            swprintf_s(name, L"%s\\pic_%03d.jpg", folder.c_str(), i);
            HANDLE h = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
        host.SetViewMode(FVM_TILE, 48);
        host.Navigate(folder);
        const DWORD dl = GetTickCount() + 3000;
        while (GetTickCount() < dl) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (ListView_GetItemCount(host.m_listWindow) >= 20) break;
            Sleep(10);
        }
        // Switch from Tile directly to List
        host.SetViewMode(FVM_LIST);
        for (int step = 0; step < 10; ++step) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            Sleep(10);
        }
        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 640, 480);
        auto old = SelectObject(dc, bitmap);
        bool allIconsDrawn = true;
        const int count = ListView_GetItemCount(host.m_listWindow);
        if (count < 20) allIconsDrawn = false;
        for (int i = 0; i < count; ++i) {
            RECT canvas{0, 0, 640, 480}; FillRect(dc, &canvas, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            NMLVCUSTOMDRAW draw{}; draw.nmcd.hdc = dc; draw.nmcd.dwItemSpec = i; draw.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
            draw.dwItemType = LVCDI_ITEM;
            LRESULT ret = host.DrawListIcon(&draw);
            if (ret != CDRF_SKIPDEFAULT) allIconsDrawn = false;
            int colored = 0;
            for (int y = 0; y < 480; ++y) {
                for (int x = 0; x < 640; ++x) {
                    if (GetPixel(dc, x, y) != RGB(255, 255, 255)) ++colored;
                }
            }
            HIMAGELIST iml = nullptr;
            const int img = host.ResolveItemIcon(i, &iml);
            if (colored < 10 || img < 0 || !iml) allIconsDrawn = false;
        }
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr, screen);
        for (int i = 1; i <= 20; ++i) {
            wchar_t name[MAX_PATH]{};
            swprintf_s(name, L"%s\\pic_%03d.jpg", folder.c_str(), i);
            DeleteFileW(name);
        }
        return allIconsDrawn;
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
    if (!folder.empty()) ::DeleteFileW((folder+L".navigation.log").c_str());
    if (!folder.empty()) ::RemoveDirectoryW(folder.c_str());
    std::cerr << "FAIL " << message << '\n';
    return 1;
}

} // namespace

// Shell menus mark separators with id 0, -1 or private ids (0x7FFD / 0x7FFE ...).
// Normalization must look at MFT_SEPARATOR, never at the id, and must recurse.
static bool MenuHasStackedSeparators(HMENU menu, bool recursive = true)
{
    const int count = ::GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        const bool separator = ShellMenuUtil::IsSeparatorAt(menu, i);
        if (separator && (i == 0 || i == count - 1 || ShellMenuUtil::IsSeparatorAt(menu, i - 1))) return true;
        HMENU sub = ::GetSubMenu(menu, i);
        if (recursive && sub && MenuHasStackedSeparators(sub)) return true;
    }
    return false;
}
static bool SeparatorNormalizationRegression()
{
    HMENU menu = ::CreatePopupMenu();
    HMENU sub = ::CreatePopupMenu();
    auto separator = [](HMENU target, UINT id) {
        MENUITEMINFOW info{}; info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_ID; info.fType = MFT_SEPARATOR; info.wID = id;
        ::InsertMenuItemW(target, ::GetMenuItemCount(target), TRUE, &info);
    };
    separator(menu, 0x7FFC);                       // leading
    ::AppendMenuW(menu, MF_STRING, 1, L"刷新");
    separator(menu, 0xFFFFFFFF); separator(menu, 0x7FFD); separator(menu, 0);  // stacked, mixed ids
    ::AppendMenuW(sub, MF_STRING, 3, L"文件夹");
    separator(sub, 0x7FFE); separator(sub, 0);
    ::AppendMenuW(sub, MF_STRING, 4, L"快捷方式");
    ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), L"新建");
    separator(menu, 0x7FFE);
    {   // an empty text item (pruned Shell placeholder) also draws as a line
        MENUITEMINFOW info{}; info.cbSize = sizeof(info);
        info.fMask = MIIM_ID | MIIM_STRING | MIIM_FTYPE; info.fType = MFT_STRING;
        info.wID = 5; info.dwTypeData = const_cast<wchar_t*>(L"");
        ::InsertMenuItemW(menu, ::GetMenuItemCount(menu), TRUE, &info);
    }
    ::AppendMenuW(menu, MF_STRING, 2, L"属性");
    separator(menu, 0x7FFC);                       // trailing
    const bool before = MenuHasStackedSeparators(menu);
    ShellMenuUtil::NormalizeSeparators(menu, true);
    const bool ok = before && !MenuHasStackedSeparators(menu) && ::GetMenuItemCount(menu) == 5
        && ::GetMenuItemCount(sub) == 3 && ::GetMenuItemID(menu, 0) == 1 && ::GetMenuItemID(menu, 4) == 2;
    ::DestroyMenu(menu);
    return ok;
}

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Every window, menu and Shell dialog of this suite lives on a private desktop: nothing can
    // appear on the user's screen, even if a native popup or error box were triggered.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    const std::wstring desktopName = L"FastFileShellView_" + std::to_wstring(::GetCurrentProcessId());
    HDESK isolatedDesktop = ::CreateDesktopW(desktopName.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!isolatedDesktop || !::SetThreadDesktop(isolatedDesktop))
        return Fail("isolated test desktop unavailable");
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
    if (!SeparatorNormalizationRegression()) return Fail("Shell menu separators with any id are normalized (no leading, trailing or stacked lines)");
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
    // Keep the diagnostic file outside the folder whose item count is asserted.
    SetEnvironmentVariableW(L"FASTFILE_NAV_LOG",(folder+L".navigation.log").c_str());
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
    if (props.name != L"native-view.txt" || props.type.empty() || !props.hasSize) {
        std::wcerr << L"Shell properties: name=" << props.name << L" type=" << props.type
            << L" hasSize=" << props.hasSize << L'\n';
        return Fail("selected object Shell properties missing", nullptr, nullptr, folder, file);
    }
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

    if(!ShellBrowserHostTestAccess::NavigationRecovery(parent,folder))
        return Fail("navigation state and same-path recovery",nullptr,parent,folder,file);

    ShellBrowserHost host;
    const RECT bounds = { 0, 0, 640, 480 };
    if (!host.Create(parent, bounds, WM_APP + 1, WM_APP + 2, WM_APP + 3)) {
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
    host.SetViewMode(FVM_TILE, 48);
    {
        IFolderView2* view = ShellBrowserHostTestAccess::View(host);
        FOLDERVIEWMODE vmode = FVM_AUTO; int vsize = 0;
        if (view) { view->GetViewModeAndIconSize(&vmode, &vsize); view->Release(); }
        if (vmode != FVM_TILE || vsize != 48)
            return Fail("Tile view mode must use standard 48 logical icon size", &host, parent, folder, file);
    }
    if (!ShellBrowserHostTestAccess::FolderThumbCleanAlpha(host, folder))
        return Fail("folder thumbnail extraction must preserve transparent alpha without black box defects", &host, parent, folder, file);
    if (!ShellBrowserHostTestAccess::CheckDetailsColumnsAndIcons(host))
        return Fail("Details view must notify postpaint and List view must resolve icons", &host, parent, folder, file);
    if (!ShellBrowserHostTestAccess::CheckJpgListIconsAfterTileSwitch(host, folder))
        return Fail("list icons must resolve and paint after switching from Tile mode", &host, parent, folder, file);
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
    if(!ShellBrowserHostTestAccess::ReapplyListBuffer(host))
        return Fail("list/details repaint stays buffered after native style reset",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::ListIconVisible(host))
        return Fail("roomier list rows retain real Shell icons at their original physical size",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::TallImageRefresh(host))
        return Fail("a tall replacement Shell image list updates the icon source by identity",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::InternalImageRefresh(host))
        return Fail("internal Shell image refresh must retain spacing and original icon source",&host,parent,folder,file);
    host.SetViewMode(FVM_DETAILS);
    RECT detailsRow{};ListView_GetItemRect(ShellBrowserHostTestAccess::List(host),0,&detailsRow,LVIR_BOUNDS);
    if(abs(detailsRow.bottom-detailsRow.top-MulDiv(26,dpi,96))>1)
        return Fail("details and list must share non-compact row spacing",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::ReapplyListBuffer(host))
        return Fail("list/details repaint stays buffered after native style reset",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::ListIconVisible(host))
        return Fail("non-compact details retain original Shell icons",&host,parent,folder,file);
    const auto spacer=ListView_GetImageList(ShellBrowserHostTestAccess::List(host),LVSIL_SMALL);
    host.SetViewMode(FVM_DETAILS);
    if(ListView_GetImageList(ShellBrowserHostTestAccess::List(host),LVSIL_SMALL)!=spacer)
        return Fail("reapplying the same mode must not restore a compact image list",&host,parent,folder,file);
    if(!ShellBrowserHostTestAccess::ContextMenuRouting(host))
        return Fail("mouse and keyboard view menus must be DefView's own (never forwarded to FastFile)",&host,parent,folder,file);
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
    {
        // Folder background menu comes from the live view (SVGIO_BACKGROUND), so it carries
        // Explorer's own 粘贴 / 撤销 / 分组依据 entries, unlike IShellFolder::CreateViewObject.
        host.Navigate(folder);
        const DWORD menuDeadline=GetTickCount()+3000;
        IContextMenu* background=nullptr;HMENU popup=nullptr;bool paste=false,properties=false,group=false;UINT max=0;
        while(GetTickCount()<menuDeadline && !paste) {
            MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_APP+1){delete reinterpret_cast<std::wstring*>(message.lParam);continue;}
                TranslateMessage(&message);DispatchMessageW(&message);
            }
            if(SUCCEEDED(host.CreateBackgroundContextMenu(&background)) && background) {
                popup=CreatePopupMenu();
                const HRESULT q=background->QueryContextMenu(popup,0,1,0x7FFF,CMF_NORMAL|CMF_EXPLORE);
                if(SUCCEEDED(q)) {
                    max=1+HRESULT_CODE(q);
                    paste=ShellMenuUtil::FindVerb(background,popup,1,max,L"paste")>=0;
                    properties=ShellMenuUtil::FindVerb(background,popup,1,max,L"properties")>=0;
                    group=ShellMenuUtil::FindVerb(background,popup,1,max,L"groupby")>=0;
                }
                if(!paste){DestroyMenu(popup);popup=nullptr;background->Release();background=nullptr;}
            }
            if(!paste)Sleep(20);
        }
        bool tidy=false;
        if(popup) {ShellMenuUtil::NormalizeSeparators(popup,false);tidy=!MenuHasStackedSeparators(popup,false);DestroyMenu(popup);}
        if(background)background->Release();
        if(!paste || !properties || !group || !tidy)
            return Fail("view background menu exposes native paste, groupby and properties verbs without stacked separators",&host,parent,folder,file);
    }
    host.Destroy();
    ::DestroyWindow(parent);
    ::DeleteFileW(file.c_str());
    ::DeleteFileW((folder+L".navigation.log").c_str());
    ::RemoveDirectoryW(folder.c_str());
    ::CoUninitialize();
    std::cout << "ShellBrowserHostTests: native browser creation/navigation/view operations passed\n";
    return 0;
}
