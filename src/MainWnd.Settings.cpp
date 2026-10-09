// FastFile - preferences, native Win32 settings dialog, live appearance.
#include "MainWndInternal.h"
#include "FastFileAbout.h"
#include "AutoUpdater.h"
#include <functional>
#include <algorithm>
#include <thread>

void FastFileSettings::Normalize() {
    startup=std::clamp(startup,0,2); density=std::clamp(density,0,2);
    navigationFont=std::clamp(navigationFont,11,16);
    tabHeight=std::clamp(tabHeight,24,48);favoritesHeight=std::clamp(favoritesHeight,24,48);
    tabWidthPercent=std::clamp(tabWidthPercent,100,200);
    navigationScrollbar=std::clamp(navigationScrollbar,6,14);
    defaultView=std::clamp(defaultView,0,7);sortColumn=std::clamp(sortColumn,0,3);
    grouping=std::clamp(grouping,-1,2);
}
std::wstring FastFileSettings::FilePath() {
    wchar_t buffer[32768]{};DWORD n=GetEnvironmentVariableW(L"APPDATA",buffer,_countof(buffer));
    std::wstring folder=n && n<_countof(buffer) ? buffer : L".";
    folder+=L"\\FastFile";CreateDirectoryW(folder.c_str(),nullptr);
    return folder+L"\\settings.ini";
}
FastFileSettings FastFileSettings::Load(const std::wstring& path) {
    FastFileSettings s;
    auto read=[&](const wchar_t* key,int fallback){return int(GetPrivateProfileIntW(L"Preferences",key,fallback,path.c_str()));};
#define FF_READ(field) s.field=read(L ## #field,s.field)
    FF_READ(startup);FF_READ(externalNewWindow);FF_READ(reuseTabs);FF_READ(confirmClose);
    FF_READ(density);FF_READ(navigationFont);FF_READ(tabHeight);FF_READ(favoritesHeight);
    FF_READ(tabWidthPercent);FF_READ(navigationScrollbar);FF_READ(defaultView);FF_READ(rememberViews);
    FF_READ(sortColumn);FF_READ(sortAscending);FF_READ(grouping);
#undef FF_READ
    wchar_t text[32768]{};GetPrivateProfileStringW(L"Preferences",L"startupPath",L"",text,_countof(text),path.c_str());
    s.startupPath=text;s.Normalize();return s;
}
bool FastFileSettings::Save(const std::wstring& path) const {
    FastFileSettings s=*this;s.Normalize();
    if(s.startupPath.find_first_of(L"\r\n")!=std::wstring::npos)return false;
    std::wstring text=L"\xFEFF[Preferences]\r\nVersion=1\r\n";
    auto add=[&](const wchar_t* key,int value){text+=std::wstring(key)+L"="+std::to_wstring(value)+L"\r\n";};
#define FF_WRITE(field) add(L ## #field,s.field)
    FF_WRITE(startup);FF_WRITE(externalNewWindow);FF_WRITE(reuseTabs);FF_WRITE(confirmClose);
    FF_WRITE(density);FF_WRITE(navigationFont);FF_WRITE(tabHeight);FF_WRITE(favoritesHeight);
    FF_WRITE(tabWidthPercent);FF_WRITE(navigationScrollbar);FF_WRITE(defaultView);FF_WRITE(rememberViews);
    FF_WRITE(sortColumn);FF_WRITE(sortAscending);FF_WRITE(grouping);
#undef FF_WRITE
    text+=L"startupPath="+s.startupPath+L"\r\n";
    const auto temporary=path+L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const DWORD bytes=DWORD(text.size()*sizeof(wchar_t));
    bool ok=WriteFile(file,text.data(),bytes,&written,nullptr) && written==bytes && FlushFileBuffers(file);
    CloseHandle(file);
    if(ok)ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temporary.c_str());return ok;
}

namespace {
enum { Startup=110,StartupPath,External,Reuse,ClosePrompt,Density,Font,TabHeight,FavHeight,TabWidth,Scrollbar,
       View,Remember,Sort,Ascending,Grouping,Context,DefaultFolders,DefaultComputer,ExplorerTakeover,
       Browse=190,RestoreWindows,DetectIntegration,RepairIntegration,IntegrationDetails,ConfigureManager,
       AboutSummary,AboutPath,AboutRefresh,AboutInstall,AboutLogs,AboutCopy,AboutProject,AboutChanges,AboutFeedback,AboutLicense,AboutResult,AboutCheckUpdate };
struct SettingsDialog {
    struct Control { HWND window;RECT bounds;int page; };
    FastFileSettings draft;std::function<bool(FastFileSettings)> save;
    std::function<void(HWND)> configure;
    std::function<void(const std::wstring&)> openFolder;
    FastFileAbout::Info about;
    HWND dialog=nullptr,tab=nullptr;HFONT font=nullptr;UINT dpi=96;int page=0;
    std::vector<Control> controls;
    void RefreshAbout() {
        about=FastFileAbout::Collect(dialog);
        const auto text=L"FastFile · 多标签 Windows 文件管理器\r\n\r\n"+about.Diagnostics();
        SetWindowTextW(Item(AboutSummary),text.c_str());
        SetWindowTextW(Item(AboutPath),about.executable.c_str());
    }
    void CheckUpdate() {
        HWND btn=Item(AboutCheckUpdate);
        HWND result=Item(AboutResult);
        EnableWindow(btn,FALSE);
        SetWindowTextW(result,L"正在检查新版本，请稍候...");
        HWND dlg=dialog;
        std::thread([dlg]() {
            auto check=FastFileUpdate::CheckForUpdate();
            PostMessageW(dlg,WM_APP+101,reinterpret_cast<WPARAM>(new FastFileUpdate::UpdateCheckResult(check)),0);
        }).detach();
    }
    void OpenProjectLink(const wchar_t* url) {
        SHELLEXECUTEINFOW command{};command.cbSize=sizeof(command);command.hwnd=dialog;
        command.lpFile=url;command.nShow=SW_SHOWNORMAL;
        if(!ShellExecuteExW(&command))SetWindowTextW(Item(AboutResult),L"无法打开链接，请检查默认浏览器。");
    }
    void Detect() {
        const auto status=CMainWnd::DetectSystemIntegration();
        const auto desired=Read();
        const bool ready=(!desired.defaultFolders || status.foldersReady)
            && (!desired.defaultComputer || status.computerReady) && (!desired.contextMenu || status.menuReady);
        const bool selected=desired.defaultFolders || desired.defaultComputer || desired.contextMenu;
        FastFileSettings applied;CMainWnd::ReadSystemIntegration(applied);
        const std::wstring text=!selected?L"当前状态：未设为默认。":
            ready && status.backgroundReady && applied.defaultFolders && applied.defaultComputer && applied.explorerWindowTakeover?
            L"当前状态：已设为默认。":L"当前状态：保存后应用默认打开方式。";
        SetWindowTextW(Item(IntegrationDetails),text.c_str());
    }
    HWND Item(int id) const {return GetDlgItem(dialog,id);}
    int Scale(int n) const {return MulDiv(n,dpi,96);}
    HWND Add(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style,int p) {
        HWND child=CreateWindowExW(wcscmp(cls,L"EDIT")==0 ? WS_EX_CLIENTEDGE : 0,cls,text,
            WS_CHILD|WS_VISIBLE|style,x,y,w,h,dialog,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        controls.push_back({child,{x,y,x+w,y+h},p});return child;
    }
    void Label(const wchar_t* text,int y,int p) {Add(L"STATIC",text,0,30,y,220,24,SS_LEFT,p);}
    void Combo(int id,const wchar_t* label,int y,int p,const std::vector<std::wstring>& options,int value) {
        Label(label,y+4,p);HWND c=Add(WC_COMBOBOXW,L"",id,256,y,340,220,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,p);
        for(const auto& option:options)SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(option.c_str()));
        SendMessageW(c,CB_SETCURSEL,value,0);
    }
    void Check(int id,const wchar_t* label,int y,int p,bool value) {
        HWND c=Add(L"BUTTON",label,id,30,y,566,28,WS_TABSTOP|BS_AUTOCHECKBOX,p);
        SendMessageW(c,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);
    }
    void Number(int id,const wchar_t* label,int y,int p,int value,int low,int high,int step=1) {
        std::vector<std::wstring> values;for(int n=low;n<=high;n+=step)values.push_back(std::to_wstring(n));
        Combo(id,label,y,p,values,(value-low)/step);
    }
    void Build() {
        tab=Add(WC_TABCONTROLW,L"",100,16,16,608,38,WS_TABSTOP,-1);
        for(const auto* title:{L"常规与标签",L"外观",L"浏览",L"系统集成",L"关于 FastFile"}) {
            TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(title);
            TabCtrl_InsertItem(tab,TabCtrl_GetItemCount(tab),&item);
        }
        Combo(Startup,L"启动时打开",74,0,{L"恢复上次标签",L"此电脑",L"指定文件夹"},draft.startup);
        Label(L"指定文件夹",120,0);
        Add(L"EDIT",draft.startupPath.c_str(),StartupPath,256,116,276,28,WS_TABSTOP|ES_AUTOHSCROLL,0);
        Add(L"BUTTON",L"浏览…",Browse,540,116,56,28,WS_TABSTOP|BS_PUSHBUTTON,0);
        Combo(External,L"其他应用打开路径时",160,0,{L"在已有窗口的新标签中打开",L"在新窗口中打开"},draft.externalNewWindow?1:0);
        Check(Reuse,L"目录已打开时切换到已有标签",208,0,draft.reuseTabs);
        Check(ClosePrompt,L"关闭多个标签时询问",246,0,draft.confirmClose);
        Combo(Density,L"布局密度",74,1,{L"紧凑（当前）",L"标准",L"宽松"},draft.density);
        Number(Font,L"导航字体大小",116,1,draft.navigationFont,11,16);
        Number(TabHeight,L"标签栏高度",158,1,draft.tabHeight,24,48);
        Number(FavHeight,L"收藏栏高度",200,1,draft.favoritesHeight,24,48);
        Number(TabWidth,L"标签宽度（%）",242,1,draft.tabWidthPercent,100,200,50);
        Number(Scrollbar,L"导航滚动条宽度",284,1,draft.navigationScrollbar,6,14);
        Add(L"STATIC",L"尺寸按窗口 DPI 自动缩放；图标保持原有清晰尺寸。",0,30,332,566,44,SS_LEFT,1);
        Combo(View,L"默认视图",74,2,{L"超大图标",L"大图标",L"中等图标",L"列表",L"详细信息",L"平铺",L"小图标",L"内容"},draft.defaultView);
        Check(Remember,L"记住每个文件夹的视图",120,2,draft.rememberViews);
        Combo(Sort,L"默认排序",164,2,{L"名称",L"修改日期",L"类型",L"大小"},draft.sortColumn);
        Combo(Ascending,L"排序方向",206,2,{L"升序",L"降序"},draft.sortAscending?0:1);
        Combo(Grouping,L"默认分组",248,2,{L"遵循 Windows 文件夹设置",L"不分组",L"修改日期",L"类型"},draft.grouping+1);
        Check(DefaultFolders,L"默认使用 FastFile",74,3,draft.defaultFolders);
        Add(L"STATIC",L"通过 FastFile 打开文件夹、磁盘和“此电脑”，并接收其他应用的打开文件夹请求。",0,30,118,566,52,SS_LEFT,3);
        Add(L"STATIC",L"开启后，独立代理随登录启动；关闭窗口会退出界面进程。直接调用资源管理器的应用可能短暂显示原窗口，确认目录加载成功后自动转交。",0,30,182,566,76,SS_LEFT,3);
        Add(L"STATIC",L"取消勾选并保存，恢复原打开方式并停止后台检测。",0,30,272,566,44,SS_LEFT,3);
        Add(L"BUTTON",L"检测默认文件管理器…",ConfigureManager,30,330,220,32,WS_TABSTOP|BS_PUSHBUTTON,3);
        Add(L"STATIC",L"",IntegrationDetails,30,388,566,72,SS_LEFT,3);
        Add(L"EDIT",L"",AboutSummary,30,74,566,224,ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP,4);
        Add(L"STATIC",L"当前程序位置",0,30,310,566,22,SS_LEFT,4);
        Add(L"EDIT",L"",AboutPath,30,334,566,28,ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,4);
        Add(L"BUTTON",L"检查更新",AboutCheckUpdate,30,382,132,32,WS_TABSTOP|BS_DEFPUSHBUTTON,4);
        Add(L"BUTTON",L"刷新信息",AboutRefresh,174,382,132,32,WS_TABSTOP|BS_PUSHBUTTON,4);
        Add(L"BUTTON",L"打开安装目录",AboutInstall,318,382,132,32,WS_TABSTOP|BS_PUSHBUTTON,4);
        Add(L"BUTTON",L"打开日志目录",AboutLogs,462,382,134,32,WS_TABSTOP|BS_PUSHBUTTON,4);
        Add(L"BUTTON",L"复制诊断信息",AboutCopy,30,426,132,32,WS_TABSTOP|BS_PUSHBUTTON,4);
        Add(L"BUTTON",L"项目主页",AboutProject,174,426,132,32,WS_TABSTOP|BS_PUSHBUTTON,4);
        Add(L"BUTTON",L"更新日志",AboutChanges,318,426,132,32,WS_TABSTOP|BS_PUSHBUTTON,4);
        Add(L"BUTTON",L"反馈问题",AboutFeedback,462,426,134,32,WS_TABSTOP|BS_PUSHBUTTON,4);
        Add(L"BUTTON",L"许可与组件",AboutLicense,30,470,110,28,WS_TABSTOP|BS_PUSHBUTTON,4);
        Add(L"STATIC",L"点击“检查更新”可获取最新版本并自动静默更新。",AboutResult,148,474,448,28,SS_LEFT,4);
        Add(L"BUTTON",L"恢复默认设置",IDRETRY,24,508,130,32,WS_TABSTOP|BS_PUSHBUTTON,-1);
        Add(L"BUTTON",L"保存",IDOK,428,508,88,32,WS_TABSTOP|BS_DEFPUSHBUTTON,-1);
        Add(L"BUTTON",L"取消",IDCANCEL,532,508,88,32,WS_TABSTOP|BS_PUSHBUTTON,-1);
        Layout();SelectPage(0);
        Detect();
    }
    void Layout() {
        HFONT next=CreateFontW(-Scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(const auto& c:controls) {
            const RECT r=c.bounds;MoveWindow(c.window,Scale(r.left),Scale(r.top),Scale(r.right-r.left),Scale(r.bottom-r.top),TRUE);
            SendMessageW(c.window,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        }
        if(font)DeleteObject(font);font=next;
    }
    void SelectPage(int p) {
        if(p==4)RefreshAbout();
        page=p;for(const auto& c:controls)ShowWindow(c.window,c.page==-1 || c.page==p ? SW_SHOW : SW_HIDE);
        TabCtrl_SetCurSel(tab,p);
        EnableWindow(Item(StartupPath),SendMessageW(Item(Startup),CB_GETCURSEL,0,0)==2);
        EnableWindow(Item(Browse),SendMessageW(Item(Startup),CB_GETCURSEL,0,0)==2);
    }
    int Choice(int id) const {return int(SendMessageW(Item(id),CB_GETCURSEL,0,0));}
    bool Checked(int id) const {return SendMessageW(Item(id),BM_GETCHECK,0,0)==BST_CHECKED;}
    FastFileSettings Read() const {
        FastFileSettings s=draft;s.startup=Choice(Startup);wchar_t buffer[32768]{};
        GetWindowTextW(Item(StartupPath),buffer,_countof(buffer));s.startupPath=buffer;
        s.externalNewWindow=Choice(External)==1;s.reuseTabs=Checked(Reuse);s.confirmClose=Checked(ClosePrompt);
        s.density=Choice(Density);s.navigationFont=11+Choice(Font);s.tabHeight=24+Choice(TabHeight);
        s.favoritesHeight=24+Choice(FavHeight);s.tabWidthPercent=100+50*Choice(TabWidth);s.navigationScrollbar=6+Choice(Scrollbar);
        s.defaultView=Choice(View);s.rememberViews=Checked(Remember);s.sortColumn=Choice(Sort);
        s.sortAscending=Choice(Ascending)==0;s.grouping=Choice(Grouping)-1;
        s.SetDefaultManager(Checked(DefaultFolders));
        s.Normalize();return s;
    }
    void BrowseFolder() {
        IFileDialog* picker=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&picker)))) {
            DWORD flags=0;picker->GetOptions(&flags);picker->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
            picker->SetTitle(L"选择启动文件夹");
            if(SUCCEEDED(picker->Show(dialog))) {
                IShellItem* item=nullptr;if(SUCCEEDED(picker->GetResult(&item))) {
                    PWSTR path=nullptr;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {SetWindowTextW(Item(StartupPath),path);CoTaskMemFree(path);}
                    item->Release();
                }
            }picker->Release();
        }
    }
    static INT_PTR CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l) {
        auto* self=reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(window,DWLP_USER));
        if(message==WM_INITDIALOG) {
            self=reinterpret_cast<SettingsDialog*>(l);SetWindowLongPtrW(window,DWLP_USER,l);self->dialog=window;
            self->dpi=GetDpiForWindow(window);if(!self->dpi)self->dpi=96;
            self->Build();RECT r{0,0,self->Scale(640),self->Scale(560)};
            AdjustWindowRectExForDpi(&r,GetWindowLongW(window,GWL_STYLE),FALSE,GetWindowLongW(window,GWL_EXSTYLE),self->dpi);
            RECT owner{};GetWindowRect(GetParent(window),&owner);
            SetWindowPos(window,nullptr,(owner.left+owner.right-(r.right-r.left))/2,(owner.top+owner.bottom-(r.bottom-r.top))/2,
                r.right-r.left,r.bottom-r.top,SWP_NOZORDER);return TRUE;
        }
        if(!self)return FALSE;
        if(message==WM_NOTIFY && reinterpret_cast<NMHDR*>(l)->hwndFrom==self->tab && reinterpret_cast<NMHDR*>(l)->code==TCN_SELCHANGE) {
            self->SelectPage(TabCtrl_GetCurSel(self->tab));return TRUE;
        }
        if(message==WM_DPICHANGED) {
            self->dpi=HIWORD(w);RECT r=*reinterpret_cast<RECT*>(l);
            SetWindowPos(window,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER);self->Layout();return TRUE;
        }
        if(message==WM_APP+101) {
            auto* check=reinterpret_cast<FastFileUpdate::UpdateCheckResult*>(w);
            HWND btn=self->Item(AboutCheckUpdate);
            HWND result=self->Item(AboutResult);
            EnableWindow(btn,TRUE);
            if(!check)return TRUE;
            if(check->status==FastFileUpdate::UpdateCheckStatus::UpToDate) {
                SetWindowTextW(result,check->message.c_str());
                MessageBoxW(window,(L"当前已是最新版本（v"+std::wstring(FASTFILE_VERSION_W)+L"）。").c_str(),L"检查更新",MB_OK|MB_ICONINFORMATION);
            } else if(check->status==FastFileUpdate::UpdateCheckStatus::Success && check->hasUpdate) {
                SetWindowTextW(result,(L"发现新版本："+check->release.tagName).c_str());
                const auto* installer=check->release.FindInstallerAsset();
                std::wstring promptMsg=L"发现 FastFile 新版本 "+check->release.tagName+L"！\r\n\r\n";
                if(!check->release.title.empty())promptMsg+=L"标题："+check->release.title+L"\r\n\r\n";
                if(!check->release.notes.empty())promptMsg+=L"更新说明：\r\n"+check->release.notes.substr(0,400)+L"\r\n\r\n";
                promptMsg+=L"是否立即自动下载并安装？更新完成后将自动重启应用。";
                if(MessageBoxW(window,promptMsg.c_str(),L"发现新版本",MB_YESNO|MB_ICONQUESTION)==IDYES) {
                    if(!installer) {
                        MessageBoxW(window,L"未在 Release 中找到安装包资产，将为您打开发布页面。",L"提示",MB_OK|MB_ICONINFORMATION);
                        self->OpenProjectLink(L"https://github.com/jinlong85/FastFile/releases");
                    } else {
                        EnableWindow(btn,FALSE);
                        SetWindowTextW(result,L"正在下载更新安装包...");
                        std::wstring dlUrl=installer->downloadUrl;
                        std::wstring dlVer=check->release.version.ToString();
                        std::wstring dst=FastFileUpdate::GetUpdateDownloadPath(dlVer);
                        HWND dlg=window;
                        std::thread([dlg,dlUrl,dst]() {
                            std::wstring err;
                            bool ok=FastFileUpdate::DownloadUpdateFile(dlUrl,dst,[dlg](uint64_t down,uint64_t total) {
                                if(total>0) {
                                    int pct=static_cast<int>((down*100)/total);
                                    wchar_t buf[128]{};
                                    swprintf_s(buf,L"正在下载更新：%d%% (%llu KB / %llu KB)...",pct,down/1024,total/1024);
                                    HWND resWnd=GetDlgItem(dlg,AboutResult);
                                    if(resWnd)SetWindowTextW(resWnd,buf);
                                }
                            },nullptr,&err);
                            PostMessageW(dlg,WM_APP+102,ok?1:0,ok?reinterpret_cast<LPARAM>(new std::wstring(dst)):reinterpret_cast<LPARAM>(new std::wstring(err)));
                        }).detach();
                    }
                }
            } else {
                SetWindowTextW(result,(L"检查更新失败："+check->message).c_str());
                MessageBoxW(window,(L"检查更新失败：\r\n\r\n"+check->message).c_str(),L"检查更新",MB_OK|MB_ICONWARNING);
            }
            delete check;return TRUE;
        }
        if(message==WM_APP+102) {
            bool ok=(w==1);
            auto* pStr=reinterpret_cast<std::wstring*>(l);
            HWND btn=self->Item(AboutCheckUpdate);
            HWND result=self->Item(AboutResult);
            EnableWindow(btn,TRUE);
            if(ok && pStr) {
                SetWindowTextW(result,L"下载完成，正在启动静默更新并重启应用...");
                std::wstring installerPath=*pStr;delete pStr;
                if(!FastFileUpdate::LaunchInstallerAndExit(installerPath,window)) {
                    SetWindowTextW(result,L"启动更新安装程序失败。");
                    MessageBoxW(window,L"启动更新安装程序失败，请尝试手动运行下载的安装包。",L"更新错误",MB_OK|MB_ICONERROR);
                }
            } else {
                std::wstring err=pStr?*pStr:L"未知网络错误";delete pStr;
                SetWindowTextW(result,(L"下载失败："+err).c_str());
                MessageBoxW(window,(L"下载更新失败：\r\n\r\n"+err).c_str(),L"更新错误",MB_OK|MB_ICONERROR);
            }
            return TRUE;
        }
        if(message==WM_COMMAND) {
            const int id=LOWORD(w);
            if(id==IDCANCEL) {EndDialog(window,IDCANCEL);return TRUE;}
            if(id==IDOK) {if(self->save(self->Read()))EndDialog(window,IDOK);return TRUE;}
            if(id==Startup && HIWORD(w)==CBN_SELCHANGE) {self->SelectPage(self->page);return TRUE;}
            if(id==Density && HIWORD(w)==CBN_SELCHANGE) {
                const int heights[]={29,36,40};const int height=heights[self->Choice(Density)];
                SendMessageW(self->Item(TabHeight),CB_SETCURSEL,height-24,0);SendMessageW(self->Item(FavHeight),CB_SETCURSEL,height-24,0);return TRUE;
            }
            if(id==Browse) {self->BrowseFolder();return TRUE;}
            if(id==AboutCheckUpdate) {self->CheckUpdate();return TRUE;}
            if(id==AboutRefresh) {self->RefreshAbout();return TRUE;}
            if(id==AboutCopy) {
                self->RefreshAbout();
                SetWindowTextW(self->Item(AboutResult),FastFileAbout::CopyDiagnostics(window,self->about)?L"已复制诊断信息，不包含路径和文件名。":L"复制失败，请稍后重试。");return TRUE;
            }
            if(id==AboutInstall || id==AboutLogs) {
                const auto path=FastFileAbout::Directory(id==AboutInstall?FastFileAbout::ExecutablePath():FastFileSettings::FilePath());
                if(!path.empty()){self->openFolder(path);SetWindowTextW(self->Item(AboutResult),L"已在主窗口打开目录。");}return TRUE;
            }
            if(id==AboutProject){self->OpenProjectLink(L"https://github.com/jinlong85/FastFile");return TRUE;}
            if(id==AboutChanges){self->OpenProjectLink(L"https://github.com/jinlong85/FastFile/blob/main/CHANGELOG.md");return TRUE;}
            if(id==AboutFeedback){self->OpenProjectLink(L"https://github.com/jinlong85/FastFile/issues/new/choose");return TRUE;}
            if(id==AboutLicense){const auto text=FastFileAbout::LicenseText();MessageBoxW(window,text.c_str(),L"许可与组件",MB_OK);return TRUE;}
            if(id==ConfigureManager) {
                self->configure(window);
                FastFileSettings applied;CMainWnd::ReadSystemIntegration(applied);
                SendMessageW(self->Item(DefaultFolders),BM_SETCHECK,applied.defaultFolders?BST_CHECKED:BST_UNCHECKED,0);
                self->Detect();return TRUE;
            }
            if(id==DefaultFolders && HIWORD(w)==BN_CLICKED){self->Detect();return TRUE;}
            if(id==IDRETRY) {
                self->draft=FastFileSettings{};for(const auto& c:self->controls)DestroyWindow(c.window);self->controls.clear();self->Build();return TRUE;
            }
        }
        if(message==WM_CLOSE) {EndDialog(window,IDCANCEL);return TRUE;}
        return FALSE;
    }
    ~SettingsDialog(){if(font)DeleteObject(font);}
};
}
void CMainWnd::ShowSettings() {
    SettingsDialog state;state.draft=m_settings;ReadSystemIntegration(state.draft);
    state.save=[this](FastFileSettings settings){return CommitSettings(settings);};
    state.configure=[this](HWND owner){ConfigureDefaultManager(owner);};
    state.openFolder=[this](const std::wstring& path){AddTab(path,true);};
    struct Template {DLGTEMPLATE dialog;WORD menu=0,cls=0;wchar_t title[4]=L"设置";} layout{};
    layout.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
    layout.dialog.dwExtendedStyle=WS_EX_CONTROLPARENT;layout.dialog.cx=440;layout.dialog.cy=320;
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,m_hWnd,SettingsDialog::Proc,reinterpret_cast<LPARAM>(&state));
}
bool CMainWnd::ApplyDefaultManagerChoice(DefaultManagerChoice choice) {
    if(choice==DefaultManagerChoice::KeepCurrent)return true;
    FastFileSettings next=m_settings;
    next.SetDefaultManager(choice==DefaultManagerChoice::UseFastFile);
    if(!ApplySystemIntegration(next))return false;
    m_settings.contextMenu=next.contextMenu;m_settings.defaultFolders=next.defaultFolders;
    m_settings.defaultComputer=next.defaultComputer;m_settings.explorerWindowTakeover=next.explorerWindowTakeover;
    UpdateShellWindowRegistration();UpdateExplorerTakeover();
    return true;
}
void CMainWnd::ConfigureDefaultManager(HWND owner) {
    const auto before=DetectSystemIntegration();
    const std::wstring content=before.summary+
        L"\r\n设置为默认后，统一文件夹、磁盘、此电脑及新窗口入口。关闭窗口会退出界面进程，独立代理继续接收请求并随登录启动。";
    const TASKDIALOG_BUTTON choices[]={
        {2001,L"设为默认并修复\n默认使用 FastFile，统一打开入口和后台检测。"},
        {2002,L"保留当前设置\n仅查看检测结果，不修改打开方式。"},
        {2003,L"恢复原打开方式\n恢复 FastFile 接管前的设置，关闭后台检测和登录启动。"}};
    TASKDIALOGCONFIG prompt{};prompt.cbSize=sizeof(prompt);prompt.hwndParent=owner;
    prompt.dwFlags=TDF_USE_COMMAND_LINKS|TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;
    prompt.pszWindowTitle=L"默认文件管理器";prompt.pszMainInstruction=L"是否默认使用 FastFile？";
    prompt.pszContent=content.c_str();prompt.pszExpandedInformation=before.details.c_str();
    prompt.pszExpandedControlText=L"查看各打开入口";prompt.cButtons=_countof(choices);prompt.pButtons=choices;
    prompt.nDefaultButton=2002;int selected=IDCANCEL;
    if(FAILED(TaskDialogIndirect(&prompt,&selected,nullptr,nullptr)) || selected==IDCANCEL)return;
    const auto choice=selected==2001?DefaultManagerChoice::UseFastFile:
        selected==2003?DefaultManagerChoice::RestorePrevious:DefaultManagerChoice::KeepCurrent;
    const bool applied=ApplyDefaultManagerChoice(choice);
    const auto after=DetectSystemIntegration();FastFileSettings actual;ReadSystemIntegration(actual);
    const bool ready=applied && after.foldersReady && after.computerReady && after.menuReady && after.backgroundReady
        && actual.defaultFolders && actual.defaultComputer && actual.explorerWindowTakeover;
    std::wstring title,report=after.summary;
    if(choice==DefaultManagerChoice::KeepCurrent) {
        title=L"已保留当前打开方式";report+=L"\r\n"+after.recommendations;
    } else if(!applied) {
        title=L"未能应用所选设置";
        report+=L"\r\n建议检查当前用户写入权限，以及 FastFile 专用入口或登录启动项是否被其他程序占用。原设置已尝试回滚，请重新检测。\r\n"+after.recommendations;
    } else if(choice==DefaultManagerChoice::RestorePrevious) {
        title=L"已恢复原打开方式";
        report+=L"\r\n已关闭 FastFile 默认接管、后台检测和登录启动。后来由其他程序修改的入口会保留。";
    } else if(ready) {
        title=L"FastFile 已设为默认文件管理器";
        report+=L"\r\n文件夹、磁盘、此电脑和标准打开入口已核对生效。建议关闭旧文件管理器窗口，再从原应用重新打开文件夹验证。";
    } else {
        title=L"设置已保存，部分入口仍需修复";
        report+=L"\r\n"+after.recommendations+L"\r\n建议关闭其他管理器的默认接管，再选择“设为默认并修复”重新检测。";
    }
    TASKDIALOGCONFIG result{};result.cbSize=sizeof(result);result.hwndParent=owner;
    result.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;result.dwCommonButtons=TDCBF_OK_BUTTON;
    result.pszWindowTitle=L"默认文件管理器检查结果";result.pszMainInstruction=title.c_str();result.pszContent=report.c_str();
    result.pszExpandedInformation=after.details.c_str();result.pszExpandedControlText=L"查看检测详情";
    TaskDialogIndirect(&result,nullptr,nullptr,nullptr);
}
bool CMainWnd::CommitSettings(FastFileSettings next) {
    next.Normalize();
    if(next.startup==2) {
        next.startupPath=NormalizePath(next.startupPath);
        DWORD attrs=GetFileAttributesW(next.startupPath.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_DIRECTORY)) {
            MessageBoxW(GetActiveWindow(),L"请选择一个存在的启动文件夹。",L"设置",MB_OK|MB_ICONWARNING);return false;
        }
    }
    FastFileSettings previous=m_settings;ReadSystemIntegration(previous);
    if(!ApplySystemIntegration(next)) {
        MessageBoxW(GetActiveWindow(),L"无法更新系统集成，已尝试恢复原设置。请检查当前用户的写入权限。",L"设置",MB_OK|MB_ICONERROR);return false;
    }
    if(!next.Save(FastFileSettings::FilePath())) {
        ApplySystemIntegration(previous);
        MessageBoxW(GetActiveWindow(),L"设置保存失败，请检查配置目录是否可写。",L"设置",MB_OK|MB_ICONERROR);return false;
    }
    m_settings=next;
    UpdateShellWindowRegistration();
    UpdateExplorerTakeover();
    if(next.sortColumn!=previous.sortColumn || next.sortAscending!=previous.sortAscending) {
        m_sortColumn=static_cast<SortColumn>(next.sortColumn);m_sortAscending=next.sortAscending;
    }
    ApplySettingsAppearance();
    if(next.defaultView!=previous.defaultView || next.rememberViews!=previous.rememberViews)SetViewMode(static_cast<ViewMode>(next.defaultView));
    ApplyShellViewMode();
    SaveSession();return true;
}
bool CMainWnd::CommitIntegrationSettings(const FastFileSettings& settings) {
    if(!ApplySystemIntegration(settings)) {
        MessageBoxW(GetActiveWindow(),L"无法修复系统集成，已尝试恢复原设置。请检查当前用户的写入权限。",L"系统集成",MB_OK|MB_ICONERROR);
        return false;
    }
    // Integration is journaled in HKCU, independently of unrelated INI drafts.
    m_settings.contextMenu=settings.contextMenu;m_settings.defaultFolders=settings.defaultFolders;
    m_settings.defaultComputer=settings.defaultComputer;m_settings.explorerWindowTakeover=settings.explorerWindowTakeover;
    UpdateShellWindowRegistration();UpdateExplorerTakeover();return true;
}
void CMainWnd::ApplySettingsAppearance() {
    ApplyDpiScaledFonts();ApplyDpiScaledChrome();ApplyUiChromeTokens();
    RebuildFavoritesBar();RebuildTabStrip();RebuildLeftQuickRows();
    std::function<void(CTreeNodeUI*)> update=[&](CTreeNodeUI* node){
        if(!node)return;node->SetFixedHeight(DpiScale(m_settings.NavigationRowHeight()));
        for(int i=0;i<node->GetCountChild();++i)update(node->GetChildNode(i));
    };
    if(m_pDirTree) {
        for(int i=0;i<m_pDirTree->GetCount();++i)if(auto* node=dynamic_cast<CTreeNodeUI*>(m_pDirTree->GetItemAt(i)))update(node);
        StyleSidePaneScrollBars(m_pDirTree);
    }
    ApplyChromeShellIcons();m_PaintManager.NeedUpdate();
}
