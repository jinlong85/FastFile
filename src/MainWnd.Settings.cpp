// FastFile - preferences, native Win32 settings dialog, live appearance.
#include "MainWndInternal.h"
#include <functional>
#include <algorithm>

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
       View,Remember,Sort,Ascending,Grouping,Context,DefaultFolders,DefaultComputer,Browse=190,RestoreWindows };
struct SettingsDialog {
    struct Control { HWND window;RECT bounds;int page; };
    FastFileSettings draft;std::function<bool(FastFileSettings)> save;
    HWND dialog=nullptr,tab=nullptr;HFONT font=nullptr;UINT dpi=96;int page=0;
    std::vector<Control> controls;
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
        for(const auto* title:{L"常规与标签",L"外观",L"浏览",L"系统集成"}) {
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
        Check(Context,L"添加右键“使用 FastFile 打开”",74,3,draft.contextMenu);
        Check(DefaultFolders,L"使用 FastFile 默认打开文件夹和磁盘",116,3,draft.defaultFolders);
        Check(DefaultComputer,L"使用 FastFile 打开桌面“此电脑”",158,3,draft.defaultComputer);
        Add(L"STATIC",L"默认关闭，仅作用于当前用户。启用前保存原设置，关闭后恢复。\r\n直接调用 Explorer 的应用可能不受影响；图片和视频的默认程序由 Windows 管理。",0,30,210,566,100,SS_LEFT,3);
        Add(L"BUTTON",L"恢复 Windows 打开方式",RestoreWindows,30,322,220,32,WS_TABSTOP|BS_PUSHBUTTON,3);
        Add(L"BUTTON",L"恢复默认设置",IDRETRY,24,416,130,32,WS_TABSTOP|BS_PUSHBUTTON,-1);
        Add(L"BUTTON",L"保存",IDOK,428,416,88,32,WS_TABSTOP|BS_DEFPUSHBUTTON,-1);
        Add(L"BUTTON",L"取消",IDCANCEL,532,416,88,32,WS_TABSTOP|BS_PUSHBUTTON,-1);
        Layout();SelectPage(0);
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
        s.contextMenu=Checked(Context);s.defaultFolders=Checked(DefaultFolders);s.defaultComputer=Checked(DefaultComputer);
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
            self->Build();RECT r{0,0,self->Scale(640),self->Scale(468)};
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
            if(id==RestoreWindows) {
                for(int item:{Context,DefaultFolders,DefaultComputer})SendMessageW(self->Item(item),BM_SETCHECK,BST_UNCHECKED,0);
                return TRUE;
            }
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
    struct Template {DLGTEMPLATE dialog;WORD menu=0,cls=0;wchar_t title[4]=L"设置";} layout{};
    layout.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
    layout.dialog.dwExtendedStyle=WS_EX_CONTROLPARENT;layout.dialog.cx=440;layout.dialog.cy=320;
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&layout.dialog,m_hWnd,SettingsDialog::Proc,reinterpret_cast<LPARAM>(&state));
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
    if(next.sortColumn!=previous.sortColumn || next.sortAscending!=previous.sortAscending) {
        m_sortColumn=static_cast<SortColumn>(next.sortColumn);m_sortAscending=next.sortAscending;
    }
    ApplySettingsAppearance();
    if(next.defaultView!=previous.defaultView || next.rememberViews!=previous.rememberViews)SetViewMode(static_cast<ViewMode>(next.defaultView));
    ApplyShellViewMode();
    SaveSession();return true;
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
