// FastFile - main window: lifecycle, message routing, selection, window activation
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

#include <memory>

CMainWnd::CMainWnd()
{
    // Every scrollbar DuiLib creates (containers' own bars and the <ScrollBar> elements the
    // skin loader builds) goes through this hook, so the Fluent rounded/hover-widening bar
    // is in place before the skin is loaded in OnCreate. See FluentScrollBarUI.h.
    DuiLib::SetScrollBarUICreator([]() -> CScrollBarUI* { return new CFluentScrollBarUI; });
}

CMainWnd::~CMainWnd()
{
    if (m_shellBrowser) {
        m_shellBrowser->Destroy();
        delete m_shellBrowser;
        m_shellBrowser = nullptr;
    }
    CaptureColumnWidths();
    SaveSession();
    if (m_hWnd) {
        ::KillTimer(m_hWnd, kTimerVirtSync);
        ::KillTimer(m_hWnd, kTimerColWidth);
    }
    StopDetailsFill();
    UninitDragDrop();
    CancelThumbJobs();
    StopThumbWorker();
    StopCopyThread(true);
}

CDuiString CMainWnd::GetSkinFolder()
{
    return CDuiString(_T("skin"));
}

CDuiString CMainWnd::GetSkinFile()
{
    return CDuiString(_T("main.xml"));
}

LPCTSTR CMainWnd::GetWindowClassName() const
{
    return _T("FastFile_MainWnd");
}

void CMainWnd::InitWindow()
{
    m_PaintManager.AddTranslateAccelerator(this);
    AddClipboardFormatListener(m_hWnd);
    PIDLIST_ABSOLUTE desktop = nullptr;
    if (SUCCEEDED(SHGetSpecialFolderLocation(m_hWnd, CSIDL_DESKTOP, &desktop))) {
        SHChangeNotifyEntry entry{desktop, TRUE};
        m_shellRenameNotify = SHChangeNotifyRegister(m_hWnd, SHCNRF_ShellLevel | SHCNRF_NewDelivery,
            SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER, kMsgShellRename, 1, &entry);
        CoTaskMemFree(desktop);
    }
    m_settings=FastFileSettings::Load(FastFileSettings::FilePath());
    m_viewMode=static_cast<ViewMode>(m_settings.defaultView);m_sortColumn=static_cast<SortColumn>(m_settings.sortColumn);m_sortAscending=m_settings.sortAscending;
    RefreshDpiFromWindow();
    ApplyDpiScaledFonts();

    m_pAddressEdit = static_cast<CEditUI*>(m_PaintManager.FindControl(_T("edit_address")));
    m_pAddressEditHost = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("address_edit_host")));
    m_pPathHost = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("path_host")));
    m_pSearchEdit = static_cast<CEditUI*>(m_PaintManager.FindControl(_T("edit_search")));
    m_pListHost = m_PaintManager.FindControl(_T("list_host"));
    m_addressEditMode = false;
    m_pFileList = static_cast<CListUI*>(m_PaintManager.FindControl(_T("file_list")));
    m_pDirTree = static_cast<CTreeViewUI*>(m_PaintManager.FindControl(_T("dir_tree")));
    m_pTabStrip = static_cast<CTabStripUI*>(m_PaintManager.FindControl(_T("tab_strip")));
    m_pIconScroll = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("icon_scroll")));
    m_pIconTiles = static_cast<CTileLayoutUI*>(m_PaintManager.FindControl(_T("file_icons")));
    m_pBreadcrumb = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("breadcrumb")));
    m_pFavoritesBar = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("favorites_bar")));
    m_pFavoritesStrip = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("favorites_strip")));
    m_pLeftQuickRows = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("left_quick_rows")));
    m_pLeftQuick = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("left_quick")));
    m_pLeftThisPc = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("left_thispc")));
    m_pPreviewPane = static_cast<CContainerUI*>(m_PaintManager.FindControl(_T("preview_pane")));
    m_pPreviewBody = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("preview_body")));
    m_pPreviewRail = static_cast<CScrollBarUI*>(m_PaintManager.FindControl(_T("preview_rail")));
    m_pLeftPanel = static_cast<CContainerUI*>(m_PaintManager.FindControl(_T("left_panel")));
    m_pPreviewTitle = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_title")));
    m_pPreviewImage = m_PaintManager.FindControl(_T("preview_image"));
    m_pPreviewText = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_text")));
    m_pPreviewType = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_type")));
    m_pPreviewSize = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_size")));
    m_pPreviewMTime = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_mtime")));
    m_pPreviewCTime = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_ctime")));
    m_pPreviewLocation = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_location")));
    m_pPreviewDimensions = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_dimensions")));
    m_pPreviewDuration = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_duration")));
    m_pPreviewFrameRate = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_framerate")));
    m_pPreviewBitRate = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_bitrate")));
    m_pPreviewTotalBitRate = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("preview_totalbitrate")));
    m_pBtnTogglePreview = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_toggle_preview")));
    m_pStatus = static_cast<CLabelUI*>(m_PaintManager.FindControl(_T("status_text")));
    m_pBtnCopy = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_copy")));
    m_pBtnPaste = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_paste")));
    m_pBtnCancelCopy = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_cancel_copy")));
    m_pBtnBack = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_back")));
    m_pBtnForward = static_cast<CButtonUI*>(m_PaintManager.FindControl(_T("btn_forward")));
    m_pChkRecursive = static_cast<COptionUI*>(m_PaintManager.FindControl(_T("chk_recursive")));

    if (m_pFileList)
        m_pFileList->EnableScrollBar(true, false);
    // A: scrollbar on TileLayout (owns cyNeeded); host must not steal wheel
    if (m_pIconScroll)
        m_pIconScroll->EnableScrollBar(false, false);
    if (m_pIconTiles)
        m_pIconTiles->EnableScrollBar(true, false);
    ApplyFileViewScrollBars();
    if (m_pDirTree) {
        m_pDirTree->SetVisibleCheckBtn(false);
        m_pDirTree->SetVisibleFolderBtn(true);
        m_pDirTree->SetItemMinWidth(DpiScale(200));
        StyleSidePaneScrollBars(m_pDirTree);
    }
    if (m_pPreviewBody) {
        m_pPreviewBody->EnableScrollBar(true, false);
        StylePreviewRail();
    }

    wchar_t tmp[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, tmp);
    m_iconCacheDir = tmp;
    m_iconCacheDir += L"FastFileIconCache";
    // v6: PNG + true alpha; wipe prior BMP/v5 cache on every relaunch.
    WipeDirectoryFiles(m_iconCacheDir);
    m_iconCacheDir.push_back(L'\\');
    ::CreateDirectoryW(m_iconCacheDir.c_str(), nullptr);

    ApplyDpiScaledChrome();
    LoadLeftNavSplitter();
    ApplyWindowIcon();
    ApplyChromeShellIcons();
    ApplyCopyUiState();
    StartThumbWorker();
    InitDirectoryTree();
    InitTabs();
    LoadFavorites();
    LoadQuickAccess();
    RebuildFavoritesBar();
    UpdateSearchOptionVisibility();
    InitDragDrop();
    ApplyFileViewScrollBars();
    ApplyColumnWidths();
    SetPreviewVisible(m_previewVisible);
    if (m_pListHost && m_hWnd) {
        const RECT bounds = m_pListHost->GetPos();
        m_shellBrowser = new (std::nothrow) ShellBrowserHost;
        if (!m_shellBrowser || !m_shellBrowser->Create(m_hWnd, bounds,
                kMsgShellNavigation, kMsgShellSelection, kMsgShellFolderOpen, kMsgShellContextMenu)) {
            delete m_shellBrowser;
            m_shellBrowser = nullptr;
            UpdateStatus(_T("Windows 文件视图初始化失败"));
        }
    }
    if (!LoadSession()) {
        const std::wstring start = m_settings.startup==2 && !ResolveFolderOpenTarget(m_settings.startupPath).empty() ? m_settings.startupPath : GetDefaultStartPath();
        AddTab(start, true);
    }
    ApplyColumnWidths();
    UpdateHeaderSortIndicators();
    RebuildBreadcrumb();
    ClearPreview();
    UpdateViewModeButtons();
    UpdateFavoritesHighlight();
    UpdateNavButtons();
    if (m_hWnd)
        ::SetTimer(m_hWnd, kTimerColWidth, 2000, nullptr);
    if (m_hWnd)
        ::SetTimer(m_hWnd, kTimerLayoutSync, 200, nullptr);
    ApplyWindowCornerAndPadding();
    SyncRecursiveCheckLabel();
    SetSearchPlaceholder(true);

    // A first launch from a folder association should land directly in that folder,
    // rather than briefly opening the normal "This PC" start tab.
    if (!m_startupOpenPaths.empty()) {
        std::vector<std::wstring> paths;
        paths.swap(m_startupOpenPaths);
        OpenExternalPaths(paths, true);
    }
}

void CMainWnd::SetStartupOpenPaths(std::vector<std::wstring> paths)
{
    m_startupOpenPaths = std::move(paths);
}

void CMainWnd::Notify(TNotifyUI& msg)
{
    if (msg.sType == DUI_MSGTYPE_RETURN) {
        if (msg.pSender == m_pAddressEdit && m_pAddressEdit) {
            ExitAddressEditMode(true); // Enter -> navigate, then breadcrumb
            return;
        }
        if (msg.pSender == m_pSearchEdit) {
            ApplySearchFilter();
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_SETFOCUS) {
        if (msg.pSender == m_pSearchEdit && m_searchPlaceholder) {
            SetSearchPlaceholder(false);
            UpdateSearchOptionVisibility();
            return;
        }
        if (msg.pSender == m_pSearchEdit) {
            UpdateSearchOptionVisibility();
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_KILLFOCUS) {
        if (msg.pSender == m_pAddressEdit && m_addressEditMode) {
            ExitAddressEditMode(false); // blur -> breadcrumb, no navigate
            return;
        }
        if (msg.pSender == m_pSearchEdit) {
            if (m_pSearchEdit && m_pSearchEdit->GetText().IsEmpty())
                SetSearchPlaceholder(true);
            UpdateSearchOptionVisibility();
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_TEXTCHANGED) {
        if (msg.pSender == m_pSearchEdit) {
            if (m_searchPlaceholder)
                return;
            UpdateSearchOptionVisibility();
            if (!IsRecursiveSearch())
                ApplySearchFilter();
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_SELECTCHANGED) {
        if (msg.pSender == m_pChkRecursive) {
            SyncRecursiveCheckLabel();
            if (!m_searchFilter.empty())
                ApplySearchFilter();
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_ITEMACTIVATE || msg.sType == DUI_MSGTYPE_ITEMDBCLICK) {
        // Tree node double-click / activate
        CControlUI* p = msg.pSender;
        while (p) {
            if (p->GetInterface(DUI_CTR_TREENODE)) {
                OnTreeNodeActivate(static_cast<CTreeNodeUI*>(p));
                return;
            }
            p = p->GetParent();
        }
        OnItemActivate(msg.pSender);
        return;
    }
    else if (msg.sType == DUI_MSGTYPE_ITEMCLICK) {
        CControlUI* p = msg.pSender;
        while (p) {
            if (p->GetInterface(DUI_CTR_TREENODE)) {
                if ((::GetKeyState(VK_RBUTTON) & 0x8000) != 0) {
                    POINT pt = {};
                    ::GetCursorPos(&pt);
                    ShowTreeContextMenu(static_cast<CTreeNodeUI*>(p), pt);
                    return;
                }
                // Single click on tree: load children + navigate
                OnTreeNodeActivate(static_cast<CTreeNodeUI*>(p));
                return;
            }
            p = p->GetParent();
        }
        // Right-click on file list/tiles is handled in WM_RBUTTONUP so it works
        // in every view mode (icons/tiles/list/details), including blank area.
        // ITEMCLICK is raised before Select() in DuiLib list items, so
        // CollectSelectedItems still sees the previous selection. Prefer the
        // clicked row; Ctrl+deselect already cleared IsSelected before notify.
        if (!IsTileViewMode()) {
            // Details view is virtualised: map the clicked row to its entry and drive the
            // entry-based selection model, then re-apply the visuals from that model. DuiLib's
            // own item selection (raised just after this notify) is only a visual and gets
            // corrected in the ITEMSELECT handler.
            CControlUI* clicked = FindListItemRoot(msg.pSender);
            const int entry = DetailsEntryFromItem(clicked);
            if (entry < 0) {
                UpdatePreviewForSelection();
                return;
            }
            const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (ctrl) {
                m_detailsSel[entry] = m_detailsSel[entry] ? 0 : 1;
                m_detailsCur = entry;
                m_detailsAnchor = entry;
            } else if (shift && m_detailsAnchor >= 0) {
                std::fill(m_detailsSel.begin(), m_detailsSel.end(), 0);
                int a = m_detailsAnchor, b = entry;
                if (a > b) { const int t = a; a = b; b = t; }
                for (int i = a; i <= b; ++i) m_detailsSel[i] = 1;
                m_detailsCur = entry;
            } else {
                std::fill(m_detailsSel.begin(), m_detailsSel.end(), 0);
                m_detailsSel[entry] = 1;
                m_detailsCur = entry;
                m_detailsAnchor = entry;
            }
            ApplyDetailsSelectionVisuals();
            UpdateListingStatusTip();
            UpdatePreviewForSelection();
            return;
        }
        {
            CControlUI* clicked = FindListItemRoot(msg.pSender);
            if (clicked && !clicked->GetUserData().IsEmpty()) {
                IListItemUI* li = static_cast<IListItemUI*>(
                    clicked->GetInterface(DUI_CTR_ILISTITEM));
                const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (li && ctrl && !li->IsSelected()) {
                    UpdatePreviewForSelection();
                } else {
                    UpdatePreviewPath(clicked->GetUserData().GetData(),
                        clicked->GetTag() != 0);
                }
            } else {
                UpdatePreviewForSelection();
            }
        }
        return;
    }
    else if (msg.sType == DUI_MSGTYPE_HEADERCLICK) {
        OnHeaderColumnClick(msg.pSender);
        return;
    }
    else if (msg.sType == DUI_MSGTYPE_SCROLL) {
        if (m_iconVirtMode)
            SyncVisibleIconWindow(false);
        if (!IsTileViewMode())
            UpdateDetailsWindow(false);
        return;
    }
    else if (msg.sType == DUI_MSGTYPE_ITEMSELECT) {
        // Keep the pooled rows showing the entry-based selection, whatever DuiLib did to the
        // clicked item on its own.
        if (!IsTileViewMode())
            ApplyDetailsSelectionVisuals();
        UpdatePreviewForSelection();
        UpdateListingStatusTip();
    }
    WindowImplBase::Notify(msg);
}

void CMainWnd::OnClick(TNotifyUI& msg)
{
    CDuiString name = msg.pSender->GetName();

    if (name == _T("btn_favorite_toggle")) {
        if (!IsThisPcPath(m_currentPath)) {
            if (IsFavoritePinned(m_currentPath)) UnpinFavorite(m_currentPath);
            else PinFavorite(m_currentPath);
        }
        return;
    }
    if (name == _T("btn_go")) {
        // Legacy go button removed from skin; keep handler harmless
        ExitAddressEditMode(true);
        return;
    }
    if (name == _T("bc_edit") || name == _T("path_host") || name == _T("address_bar")) {
        EnterAddressEditMode();
        return;
    }
    if (name == _T("btn_back")) {
        GoBack();
        return;
    }
    if (name == _T("btn_forward")) {
        GoForward();
        return;
    }
    if (name == _T("btn_up")) {
        GoUp();
        return;
    }
    if (name == _T("btn_refresh")) {
        RefreshListing();
        return;
    }
    if (name == _T("chk_recursive")) {
        if (!m_searchFilter.empty())
            ApplySearchFilter();
        return;
    }
    if (name == _T("btn_view_xlarge")) {
        SetViewMode(ViewMode::ExtraLargeIcons);
        return;
    }
    if (name == _T("btn_view_large")) {
        SetViewMode(ViewMode::LargeIcons);
        return;
    }
    if (name == _T("btn_view_medium")) {
        SetViewMode(ViewMode::MediumIcons);
        return;
    }
    if (name == _T("btn_view_list")) {
        SetViewMode(ViewMode::List);
        return;
    }
    if (name == _T("btn_view_details")) {
        SetViewMode(ViewMode::Details);
        return;
    }
    if (name == _T("btn_view_tiles") || name == _T("btn_view_icons")) {
        SetViewMode(ViewMode::Tiles);
        return;
    }
    if (name == _T("btn_tab_add")) {
        OnNewTabRequested();
        return;
    }
    if (name.Find(_T("tab_btn_")) == 0) {
        int idx = _ttoi(name.GetData() + 8);
        ActivateTab(idx);
        return;
    }
    if (name.Find(_T("tab_close_")) == 0) {
        int idx = _ttoi(name.GetData() + 10);
        CloseTab(idx);
        return;
    }
    if (name == _T("btn_copy")) { OnCopyClicked(); return; }
    if (name == _T("btn_cut")) { OnCutClicked(); return; }
    if (name == _T("btn_paste")) { OnPasteClicked(); return; }
    if (name == _T("btn_cancel_copy")) { OnCancelCopyClicked(); return; }
    if (name == _T("btn_delete")) { OnDeleteClicked(); return; }
    if (name == _T("btn_rename")) { OnRenameClicked(); return; }
    if (name == _T("btn_share") || name == _T("btn_preview_share")) { OnShareClicked(); return; }
        if (name == _T("btn_new") || name == _T("btn_newfolder")) { OnNewMenuClicked(); return; }
        if (name == _T("btn_settings")) { ShowSettings(); return; }
        if (name == _T("btn_sort")) { OnSortMenuClicked(); return; }
        if (name == _T("btn_view_menu")) { OnViewMenuClicked(); return; }
    if (name == _T("btn_more")) { OnMoreMenuClicked(); return; }
    // Quick-access rows: their clicks come from the window-level press/release handling
    // below (so a vertical drag can reorder them); ignore any stray notify.
    if (name.Find(_T("fav_row_")) == 0)
        return;
    if (name.Find(_T("fav_pin_")) == 0 || name.Find(_T("fav_dyn_")) == 0) {
        OnPinnedFavoriteClick(msg.pSender);
        return;
    }
    if (name == _T("btn_toggle_preview")) {
        SetPreviewVisible(!m_previewVisible);
        if (m_previewVisible) UpdatePreviewForSelection();
        return;
    }
    if (name == _T("bc_seg")) {
        OnBreadcrumbSegmentClick(msg.pSender);
        return;
    }
    // Icon tile: Ctrl/Shift multi-select; double-click opens
    if (msg.pSender && !msg.pSender->GetUserData().IsEmpty()) {
        if (name.Find(_T("icon_")) == 0
            || (IsTileViewMode() && msg.pSender->GetParent() == m_pIconTiles)) {
            OnIconTileClick(msg.pSender);
            UpdatePreviewForSelection();
            return;
        }
    }
    if (name == _T("btn_min")) {
        SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
        return;
    }
    if (name == _T("btn_max")) {
        if (::IsZoomed(m_hWnd))
            SendMessage(WM_SYSCOMMAND, SC_RESTORE, 0);
        else
            SendMessage(WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        return;
    }

    WindowImplBase::OnClick(msg);
}

LRESULT CMainWnd::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if(uMsg==kMsgShellContextMenu && m_shellBrowser) {
        POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};
        if(point.x==-1 && point.y==-1) {
            RECT bounds{};GetWindowRect(reinterpret_cast<HWND>(wParam),&bounds);
            point={bounds.left+24,bounds.top+24};
        }
        std::vector<std::pair<std::wstring,bool>> selected;
        if(!m_shellBrowser->GetSelection(selected))return 0;
        std::vector<std::wstring> paths;
        for(const auto& item:selected)paths.push_back(item.first);
        if(paths.empty())ShowBlankAreaContextMenu(point);
        else ShowShellContextMenu(paths,point);
        return 1;
    }
    if (uMsg == WM_CAPTURECHANGED || uMsg == WM_CANCELMODE || uMsg == WM_KILLFOCUS)
        CancelScrollBarGestures();
    if (uMsg == kMsgShellFolderOpen) {
        std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
        if(path) {if(wParam)AddTab(*path,true,wParam==2);else NavigateToNow(*path,true);}
        return 0;
    }
    if (uMsg == kMsgShellNavigation) {
        std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lParam));
        if (path)
            OnShellBrowserNavigation(std::move(*path));
        return 0;
    }
    if (uMsg == kMsgShellRename) {
        TrackShellRename(wParam, lParam);
        return 0;
    }
    if (uMsg == kMsgShellSelection) {
        SyncShellViewSelection();
        return 0;
    }
    if (uMsg == kMsgCommitInlineRename) { CommitInlineRename(); return 0; }
    if (uMsg == kMsgCancelInlineRename) { CancelInlineRename(); return 0; }
    if (uMsg == WM_COMMAND && m_renameEdit
        && reinterpret_cast<HWND>(lParam) == m_renameEdit
        && HIWORD(wParam) == EN_KILLFOCUS) {
        CommitInlineRename();
        return 0;
    }
    if (uMsg == WM_NCLBUTTONDOWN || uMsg == WM_NCLBUTTONUP) {
        // Custom frame: the caption buttons are reported by WM_NCHITTEST (HTMINBUTTON /
        // HTMAXBUTTON / HTCLOSE) instead of being DuiLib controls. DefWindowProc's caption
        // button tracking does not run its SC_MINIMIZE / SC_MAXIMIZE commands for a window
        // whose caption we removed - it swallows the mouse messages, which made the buttons
        // look dead (and the hover paint flicker). Run the commands ourselves; HTCAPTION and
        // the resize borders still go to DefWindowProc so moving / Aero Snap keep working.
        const UINT code = static_cast<UINT>(wParam);
        const bool isCaptionButton = code == HTMINBUTTON || code == HTMAXBUTTON
            || code == HTCLOSE;
        if (isCaptionButton) {
            if (uMsg == WM_NCLBUTTONDOWN) {
                if (code == HTMINBUTTON) {
                    SendMessage(WM_SYSCOMMAND, SC_MINIMIZE, 0);
                } else if (code == HTMAXBUTTON) {
                    SendMessage(WM_SYSCOMMAND, ::IsZoomed(m_hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
                } else {
                    SendMessage(WM_SYSCOMMAND, SC_CLOSE, 0);
                }
            }
            return 0;
        }
    }
    if (uMsg == WM_NCHITTEST) {
        // The title row is the caption: the system buttons report the standard non-client
        // codes (so minimize/maximize/close and Aero Snap behave natively), tabs answer
        // HTCLIENT, and the empty part of the row drags the window. The outer size box stays
        // with DuiLib so the window can still be resized from its edges.
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ::ScreenToClient(m_hWnd, &pt);
        RECT rcClient = {};
        ::GetClientRect(m_hWnd, &rcClient);
        const RECT szb = m_PaintManager.GetSizeBox();
        const bool inSizeBox = pt.x < rcClient.left + szb.left
            || pt.x >= rcClient.right - szb.right
            || pt.y < rcClient.top + szb.top
            || pt.y >= rcClient.bottom - szb.bottom;
        if (!inSizeBox) {
            const auto hitBtn = [&](LPCTSTR name) {
                CControlUI* c = m_PaintManager.FindControl(name);
                return c && c->IsVisible() && ::PtInRect(&c->GetPos(), pt);
            };
            if (hitBtn(_T("closebtn"))) return HTCLOSE;
            if (hitBtn(_T("maxbtn")) || hitBtn(_T("restorebtn"))) return HTMAXBUTTON;
            if (hitBtn(_T("minbtn"))) return HTMINBUTTON;
            if (m_pTabStrip && m_pTabStrip->IsVisible()
                && ::PtInRect(&m_pTabStrip->GetPos(), pt)) {
                return m_pTabStrip->HitTest(pt).part == CTabStripUI::Part::Empty
                    ? HTCAPTION : HTCLIENT;
            }
            CControlUI* bar = m_PaintManager.FindControl(_T("titlebar"));
            if (bar && bar->IsVisible() && ::PtInRect(&bar->GetPos(), pt))
                return HTCAPTION;
        }
    }
    if (uMsg == WM_NCMOUSEMOVE || uMsg == WM_NCMOUSELEAVE) {
        // The system owns the button clicks now (HTCAPTION family), so DuiLib never sees the
        // hover: paint the hover state here instead.
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        if (uMsg == WM_NCMOUSEMOVE) ::ScreenToClient(m_hWnd, &pt);
        UpdateCaptionButtonHover(pt, uMsg == WM_NCMOUSEMOVE);
    }
    // Messages posted by the self-drawn tab strip (see TabStripUI.h).
    if (uMsg == CTabStripUI::kMsgTabSelect) { OnTabStripSelect((int)wParam); return 0; }
    if (uMsg == CTabStripUI::kMsgTabClose) { OnTabStripClose((int)wParam); return 0; }
    if (uMsg == CTabStripUI::kMsgTabReorder) { OnTabStripReorder((int)wParam, (int)lParam); return 0; }
    if (uMsg == CTabStripUI::kMsgTabAdd) { OnTabStripAdd(); return 0; }
    if (uMsg == CTabStripUI::kMsgTabDragOut || uMsg == CTabStripUI::kMsgTabContextMenu) {
        POINT* screenPt = reinterpret_cast<POINT*>(lParam);
        const POINT pt = screenPt ? *screenPt : POINT{};
        delete screenPt;
        if (uMsg == CTabStripUI::kMsgTabDragOut) OnTabStripDragOut((int)wParam, pt);
        else OnTabStripContextMenu((int)wParam, pt);
        return 0;
    }
    // A second FastFile process forwards folders through WM_COPYDATA, then exits.  Use a
    // line-delimited payload: Windows paths cannot contain a line break, and the copy is
    // bounded by cbData so an untrusted sender cannot make us read beyond its buffer.
    constexpr ULONG_PTR kOpenPathsCopyData = 0x46464F50; // "FFOP"
    if (uMsg == WM_COPYDATA) {
        const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!cds || cds->dwData != kOpenPathsCopyData || !cds->lpData
            || cds->cbData < sizeof(wchar_t) || (cds->cbData % sizeof(wchar_t)) != 0)
            return 0;

        const auto* data = static_cast<const wchar_t*>(cds->lpData);
        size_t chars = cds->cbData / sizeof(wchar_t);
        size_t len = 0;
        while (len < chars && data[len] != L'\0')
            ++len;
        std::vector<std::wstring> paths;
        size_t begin = 0;
        while (begin < len) {
            size_t end = begin;
            while (end < len && data[end] != L'\n')
                ++end;
            if (end > begin)
                paths.emplace_back(data + begin, end - begin);
            begin = end + 1;
        }
        if (paths.empty())
            return 0;
        auto* pending = new (std::nothrow) std::vector<std::wstring>(std::move(paths));
        if (!pending || !::PostMessageW(m_hWnd, kMsgOpenExternalPaths, 0,
                reinterpret_cast<LPARAM>(pending))) {
            delete pending;
            return 0;
        }
        return 1;
    }

    // Forward owner-draw / cascading submenu messages to IContextMenu2/3
    if (m_pCtxMenu2 || m_pCtxMenu3) {
        if (uMsg == WM_INITMENUPOPUP || uMsg == WM_DRAWITEM || uMsg == WM_MEASUREITEM || uMsg == WM_MENUCHAR) {
            LRESULT lr = 0;
            bool handled = false;
            ForwardShellMenuMessage(uMsg, wParam, lParam, &lr, &handled);
            if (handled)
                return lr;
        }
    }
    if (uMsg == WM_DPICHANGED) {
        UINT newDpi = LOWORD(wParam);
        const RECT* prc = reinterpret_cast<const RECT*>(lParam);
        OnDpiChanged(newDpi, prc);
        return 0;
    }
    if (uMsg == WM_CLOSE) {
        // Closing the window closes every tab, so confirm first while more than one is
        // open (Explorer/Terminal style). Cancel keeps the window and all tabs alive.
        if (!m_closeConfirmed && m_tabs.size() > 1) {
            if (!ConfirmCloseWithMultipleTabs())
                return 0;
            m_closeConfirmed = true;
        }
        CaptureColumnWidths();
        if (m_hWnd) {
            ::KillTimer(m_hWnd, kTimerVirtSync);
            ::KillTimer(m_hWnd, kTimerColWidth);
            ::KillTimer(m_hWnd, kTimerDetailsSync);
            ::KillTimer(m_hWnd, kTimerLayoutSync);
        }
        StopDetailsFill();
        SaveFavorites();
        SaveQuickAccess();
        SaveSession();
    }
    if (uMsg == WM_DESTROY) {
        m_PaintManager.RemoveTranslateAccelerator(this);
        RemoveClipboardFormatListener(m_hWnd);
        if (m_shellRenameNotify) SHChangeNotifyDeregister(m_shellRenameNotify);
        // A failed copy undo can leave a partially retained batch. Restore its
        // retained items before dropping the in-memory history on close.
        for (const auto& record : m_undoStack) {
            for (const auto& pair : record.backups) {
                const auto folder = ParentPath(pair.second);
                const DWORD attributes = GetFileAttributesW(folder.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
                if (GetFileAttributesW(pair.first.c_str()) == INVALID_FILE_ATTRIBUTES)
                    MoveFileExW(pair.second.c_str(), pair.first.c_str(), 0);
                RemoveDirectoryW(folder.c_str());
            }
        }
        ClearRedoHistory();
        // DuiLib's WindowImplBase::OnClose only clears bHandled and never posts WM_QUIT,
        // so without this the process lives on with no window after a close (it also keeps
        // FastFile.exe locked, which blocks rebuilds). Quit once the window is gone.
        ::PostQuitMessage(0);
    }
    if (uMsg == WM_CLIPBOARDUPDATE) {
        UpdateCommandBarState();
        return 0;
    }
    if (uMsg == WM_TIMER) {
        if (wParam == kTimerShellHistory) { FinishShellHistory(); return 0; }
        if (wParam == kTimerVirtSync) { SyncVisibleIconWindow(false); return 0; }
        if (wParam == kTimerColWidth) { CaptureColumnWidths(); return 0; }
        if (wParam == kTimerDetailsSync) { UpdateDetailsWindow(false); return 0; }
        if (wParam == kTimerLayoutSync) { SyncLayoutDependents(); SyncShellViewSelection(); return 0; }
    }
    if (uMsg == WM_MBUTTONDOWN) {
        // Middle click on a tab closes it (Explorer behaviour).
        POINT mp = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        if (m_pTabStrip && m_pTabStrip->IsVisible()) {
            const CTabStripUI::HitInfo h = m_pTabStrip->HitTest(mp);
            if (h.part == CTabStripUI::Part::Body || h.part == CTabStripUI::Part::Close) {
                CloseTab(h.index);
                return 0;
            }
        }
        // Middle click on a favourite chip opens it in a new tab (Explorer behaviour).
        for (CControlUI* p = m_PaintManager.FindControl(mp); p; p = p->GetParent()) {
            const CDuiString nm = p->GetName();
            if (nm.Find(_T("fav_pin_")) == 0 && !p->GetUserData().IsEmpty()) {
                AddTab(p->GetUserData().GetData(), true);
                return 0;
            }
        }
    }
    if (uMsg == WM_MOUSEWHEEL && m_pFavoritesBar && m_pFavoritesBar->IsVisible()) {
        // WM_MOUSEWHEEL carries screen coordinates.
        POINT sp = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        POINT mp = sp;
        ::ScreenToClient(m_hWnd, &mp);
        const RECT fav = m_pFavoritesBar->GetPos();
        if (::PtInRect(&fav, mp)) {
            const int delta = (short)HIWORD(wParam);
            ScrollFavoritesBy(delta > 0 ? -DpiScale(60) : DpiScale(60));
            return 0;
        }
    }
    auto* pressedScrollBar = uMsg == WM_LBUTTONDOWN
        ? dynamic_cast<CScrollBarUI*>(m_PaintManager.FindControl(POINT{(short)LOWORD(lParam),(short)HIWORD(lParam)}))
        : nullptr;
    if (pressedScrollBar && pressedScrollBar != m_pPreviewRail)
        m_dragTracking = false; // Scrollbar capture must never arm an OLE file drag.
    if (uMsg == WM_LBUTTONDOWN && !m_inDoDragDrop
        && (!pressedScrollBar || pressedScrollBar == m_pPreviewRail)) {
        // Pane dividers own a generous grab band that straddles the divider line: the
        // cursor turns into a left/right arrow there and the press starts a drag.
        const int px = (short)LOWORD(lParam);
        const int py = (short)HIWORD(lParam);
        // Rows first: a press on a row is a click / reorder, never a divider drag.
        const POINT quickPt = { px, py };
        const int quickIdx = HitTestQuickRow(quickPt);
        if (quickIdx >= 0) {
            m_quickDragIndex = quickIdx;
            m_quickDragActive = false;
            m_quickDragStartY = py;
            ::SetCapture(m_hWnd);
            return 0;
        }
        // 快速访问 / 此电脑 divider: resizes the quick-access block (persisted).
        if (HitTestLeftNavDivider(px, py) && m_pLeftQuick) {
            m_leftNavDragging = true;
            m_leftNavDragStartY = py;
            m_leftNavDragStartH = m_pLeftQuick->GetFixedHeight();
            ::SetCapture(m_hWnd);
            return 0;
        }
        const int pane = HitTestPaneDivider(px, py);
        if (pane != 0) {
            m_paneDragKind = pane;
            m_paneDragStartX = px;
            m_paneDragStartLeft = m_pLeftPanel ? m_pLeftPanel->GetFixedWidth() : 0;
            m_paneDragStartPreview = m_pPreviewPane ? m_pPreviewPane->GetFixedWidth() : 0;
            ::SetCapture(m_hWnd);
            return 0;
        }
        // The preview rail intentionally owns both gestures. Delay the decision
        // until movement makes the intended axis clear: vertical scrolls content,
        // horizontal resizes the preview pane.
        if (IsPreviewScrollBarHit(px, py)) {
            m_previewRailGesture = 1;
            m_paneDragStartX = px;
            m_paneDragStartPreview = m_pPreviewPane ? m_pPreviewPane->GetFixedWidth() : 0;
            m_previewRailLastY = py;
            m_dragTracking = true;
            m_dragStartPt = { px, py };
            ::SetCapture(m_hWnd);
            return 0;
        }
        m_dragTracking = true;
        m_dragStartPt.x = (short)LOWORD(lParam);
        m_dragStartPt.y = (short)HIWORD(lParam);
    }
    if (uMsg == WM_MOUSEMOVE && m_paneDragKind != 0) {
        const int x = (short)LOWORD(lParam);
        const int dx = x - m_paneDragStartX;
        int w = 0;
        if (m_paneDragKind == 1) w = m_paneDragStartLeft + dx;         // sidebar: right edge
        else w = m_paneDragStartPreview - dx;                          // preview: left edge
        ApplyPaneDragWidth(m_paneDragKind, w);
        return 0;
    }
    if (uMsg == WM_MOUSEMOVE && m_quickDragIndex >= 0 && (wParam & MK_LBUTTON) != 0) {
        const int y = (short)HIWORD(lParam);
        if (!m_quickDragActive && ::abs(y - m_quickDragStartY) >= DpiScale(4))
            m_quickDragActive = true;
        if (m_quickDragActive && m_pLeftQuickRows) {
            // Live reorder: drop the dragged row into the slot the cursor sits over.
            int target = 0;
            const int n = m_pLeftQuickRows->GetCount();
            for (int i = 0; i < n; ++i) {
                CControlUI* c = m_pLeftQuickRows->GetItemAt(i);
                if (!c || !c->IsVisible()) continue;
                const RECT r = c->GetPos();
                if (y > (r.top + r.bottom) / 2) target = i + 1;
            }
            int moveTo = (target > m_quickDragIndex) ? target - 1 : target;
            if (moveTo < 0) moveTo = 0;
            if (moveTo >= static_cast<int>(m_quickRows.size()))
                moveTo = static_cast<int>(m_quickRows.size()) - 1;
            if (moveTo != m_quickDragIndex)
                MoveQuickRow(m_quickDragIndex, moveTo);
        }
        return 0;
    }
    if (uMsg == WM_MOUSEMOVE && m_leftNavDragging && m_pLeftQuick) {
        const int y = (short)HIWORD(lParam);
        // The block's *occupied* height is 2 * fixed - contentHeight (DuiLib adds the padding
        // on top of the fixed height), so half the cursor delta keeps the divider under the
        // pointer instead of running away at double speed.
        int h = m_leftNavDragStartH + (y - m_leftNavDragStartY) / 2;
        const int minH = m_pLeftQuick->GetMinHeight();
        const int maxH = m_pLeftQuick->GetMaxHeight();
        if (minH > 0 && h < minH) h = minH;
        if (maxH > 0 && h > maxH) h = maxH;
        m_pLeftQuick->SetFixedHeight(h);
        UpdateLeftQuickAccessSpacing();
        m_pLeftQuick->NeedParentUpdate();
        return 0;
    }
    if (uMsg == WM_LBUTTONUP && m_quickDragIndex >= 0) {
        const int idx = m_quickDragIndex;
        const bool reordered = m_quickDragActive;
        m_quickDragIndex = -1;
        m_quickDragActive = false;
        ::ReleaseCapture();
        if (reordered)
            SaveQuickAccess();      // keep the dragged order for the next launch
        else
            ActivateQuickRow(idx);  // plain click: open the folder / This PC
        return 0;
    }
    if (uMsg == WM_LBUTTONUP && m_leftNavDragging) {
        m_leftNavDragging = false;
        ::ReleaseCapture();
        CaptureLeftNavSplitterIfChanged();
        return 0;
    }
    if (uMsg == WM_MOUSEMOVE && m_previewRailGesture != 0
        && (wParam & MK_LBUTTON) != 0) {
        const int x = (short)LOWORD(lParam);
        const int y = (short)HIWORD(lParam);
        if (m_previewRailGesture == 1) {
            const int dx = x - m_paneDragStartX;
            const int dy = y - m_dragStartPt.y;
            const int threshold = DpiScale(3);
            if (::abs(dx) < threshold && ::abs(dy) < threshold)
                return 0;
            if (::abs(dx) > ::abs(dy)) {
                m_previewRailGesture = 0;
                m_paneDragKind = 2;
                ApplyPaneDragWidth(2, m_paneDragStartPreview - dx);
                return 0;
            }
            m_previewRailGesture = 2;
        }
        if (m_previewRailGesture == 2 && m_pPreviewBody) {
            SIZE scroll = m_pPreviewBody->GetScrollPos();
            scroll.cy += y - m_previewRailLastY;
            m_pPreviewBody->SetScrollPos(scroll);
            m_previewRailLastY = y;
            // Keep the rail's own thumb under the cursor while dragging (the periodic
            // layout sync would otherwise catch up 200 ms later).
            SyncPreviewRail();
            return 0;
        }
    }
    if (uMsg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
        if (m_paneDragKind != 0) {
            ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        }
        POINT pt = {};
        ::GetCursorPos(&pt);
        ::ScreenToClient(m_hWnd, &pt);
        if (m_previewRailGesture != 0 || IsPreviewScrollBarHit(pt.x, pt.y)) {
            ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        }
        if (m_leftNavDragging || HitTestLeftNavDivider(pt.x, pt.y)) {
            ::SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
            return TRUE;
        }
        if (HitTestPaneDivider(pt.x, pt.y) != 0) {
            ::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        }
    }
    if (uMsg == WM_LBUTTONUP && (m_paneDragKind != 0 || m_previewRailGesture != 0)) {
        // A click on the merged preview rail that never turned into a drag scrolls one
        // page towards the click, the way a normal scrollbar track behaves.
        if (m_previewRailGesture == 1 && m_paneDragKind == 0 && m_pPreviewBody) {
            RECT thumb = {};
            if (PreviewRailThumbRect(thumb)) {
                const int y = (short)HIWORD(lParam);
                if (y < thumb.top || y >= thumb.bottom) {
                    const RECT rail = m_pPreviewRail->GetPos();
                    const int thumbH = static_cast<int>(thumb.bottom - thumb.top);
                    const int railH = static_cast<int>(rail.bottom - rail.top);
                    const int page = (std::max)(railH - thumbH,
                        DpiScale(UiTokens::PreviewMetaRowH));
                    SIZE scroll = m_pPreviewBody->GetScrollPos();
                    scroll.cy += (y < thumb.top) ? -page : page;
                    m_pPreviewBody->SetScrollPos(scroll);
                    SyncPreviewRail();
                }
            }
        }
        m_paneDragKind = 0;
        m_previewRailGesture = 0;
        m_dragTracking = false;
        ::ReleaseCapture();
        CapturePaneWidthsIfChanged();
        return 0;
    }
    if (uMsg == WM_CAPTURECHANGED) {
        if (m_paneDragKind != 0)
            m_paneDragKind = 0;
        m_previewRailGesture = 0;
        m_quickDragIndex = -1;
        m_quickDragActive = false;
        m_leftNavDragging = false;
    }
    if (uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN) {
        m_dragTracking = false;
        m_previewRailGesture = 0;
    }
    if (uMsg == WM_LBUTTONUP) {
        CaptureLeftNavSplitterIfChanged();
        CapturePaneWidthsIfChanged();
        POINT ptClient = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        CControlUI* hit = m_PaintManager.FindControl(ptClient);
        if (IsFileViewBlankHit(hit)) {
            ClearFileSelection();
            UpdateListingStatusTip();
        }
    }
    if (uMsg == WM_RBUTTONUP) {
        POINT ptClient = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        // Quick-access row: hand over to the real Shell menu for that folder (This PC gets
        // the Computer folder's own menu), plus FastFile's own entries for pinned rows.
        const int quickIdx = HitTestQuickRow(ptClient);
        if (quickIdx >= 0) {
            POINT ptScreen = ptClient;
            ::ClientToScreen(m_hWnd, &ptScreen);
            ShowQuickRowContextMenu(quickIdx, ptScreen);
            return 0;
        }
        CControlUI* hit = m_PaintManager.FindControl(ptClient);
        CControlUI* p = hit;
        while (p) {
            CDuiString nm = p->GetName();
            if (nm.Find(_T("fav_pin_")) == 0 || nm.Find(_T("fav_dyn_")) == 0) {
                POINT ptScreen = ptClient;
                ::ClientToScreen(m_hWnd, &ptScreen);
                ShowFavoriteContextMenu(p, ptScreen);
                return 0;
            }
            p = p->GetParent();
        }
        if (IsFileViewBlankHit(hit)) {
            // Explorer clears selection before its folder-background menu.
            ClearFileSelection();
            UpdateListingStatusTip();
            POINT ptScreen = ptClient;
            ::ClientToScreen(m_hWnd, &ptScreen);
            ShowBlankAreaContextMenu(ptScreen);
            return 0;
        }
        bool overFileView = false;
        p = hit;
        while (p) {
            CDuiString nm = p->GetName();
            if (nm == _T("file_list") || nm == _T("file_icons") || nm == _T("icon_scroll") || nm == _T("list_host")) {
                overFileView = true;
                break;
            }
            if (p->GetInterface(DUI_CTR_TREENODE) || nm == _T("dir_tree") || nm == _T("left_panel")
                || nm == _T("left_quick") || nm == _T("left_thispc") || nm == _T("favorites_bar"))
                break;
            p = p->GetParent();
        }
        if (overFileView) {
            POINT ptScreen = ptClient;
            ::ClientToScreen(m_hWnd, &ptScreen);
            CControlUI* listItem = FindListItemRoot(hit);
            CControlUI* tile = nullptr;
            if (IsTileViewMode() && hit) {
                CControlUI* t = hit;
                while (t && t->GetParent() != m_pIconTiles)
                    t = t->GetParent();
                if (t && t->GetParent() == m_pIconTiles)
                    tile = t;
            }
            if (tile) {
                // Icons/tiles/list: select hit tile if needed, then IContextMenu
                if ((tile->GetTag() & 0x100) == 0) {
                    ClearIconSelection();
                    SetIconSelected(tile, true);
                    m_iconAnchor = FindIconIndex(tile);
                }
                ShowItemContextMenu(tile, ptScreen);
                return 0;
            }
            if (listItem) {
                // Details: DuiLib already selected on RBUTTONDOWN; show IContextMenu
                ShowItemContextMenu(listItem, ptScreen);
                return 0;
            }
            ShowBlankAreaContextMenu(ptScreen);
            return 0;
        }
    }
    if (uMsg == WM_MOUSEMOVE && m_dragTracking && !m_inDoDragDrop
        && (::GetKeyState(VK_LBUTTON) & 0x8000)) {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        const int dx = pt.x - m_dragStartPt.x;
        const int dy = pt.y - m_dragStartPt.y;
        const int thresh = ::GetSystemMetrics(SM_CXDRAG);
        if (dx * dx + dy * dy >= thresh * thresh) {
            m_dragTracking = false;
            BeginDragSelectedItems();
            return 0;
        }
    }
    if (uMsg == WM_MOUSEMOVE) {
        // Explorer widens the scrollbar as soon as the pointer nears the edge of the
        // scrolling view, so this runs on every move (cheap: a handful of rect tests).
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        UpdateFluentScrollBarHover(pt);
    }
    if (uMsg == WM_MOUSELEAVE) {
        // Collapse every bar; deliberately falls through so DuiLib still clears its own
        // control hot-states for this message.
        POINT away = { -100000, -100000 };
        UpdateFluentScrollBarHover(away);
    }
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) {
        const bool inEdit = IsEditingText();
        const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
        if (wParam == VK_ESCAPE && m_addressEditMode) {
            ExitAddressEditMode(false);
            return 0;
        }
        if (!inEdit) {
            // Details list: arrow / page keys move the *entry* cursor (the pooled rows shift
            // as the window moves, so DuiLib's own item-based navigation would land on spacers).
            if (!IsTileViewMode() && m_pFileList && m_pFileList->IsVisible()
                && m_PaintManager.GetFocus() == m_pFileList) {
                const int step = (std::max)(1, m_detailsPoolRows - 2 * kDetailsVirtOverscan);
                if (wParam == VK_DOWN) { DetailsMoveCursor(1); return 0; }
                if (wParam == VK_UP) { DetailsMoveCursor(-1); return 0; }
                if (wParam == VK_NEXT) { DetailsMoveCursor(step); return 0; }
                if (wParam == VK_PRIOR) { DetailsMoveCursor(-step); return 0; }
                if (wParam == VK_HOME) {
                    DetailsMoveCursor(-(static_cast<int>(m_detailsEntries.size()) + 1));
                    return 0;
                }
                if (wParam == VK_END) {
                    DetailsMoveCursor(static_cast<int>(m_detailsEntries.size()) + 1);
                    return 0;
                }
            }
            // Tile / icon / list grid: arrows walk the cursor by the live grid size, Home/End
            // jump to the ends, PageUp/PageDown move a screen, Enter/Space open the cursor.
            if (IsTileViewMode() && m_pIconTiles && IsIconViewFocused()) {
                if (wParam == VK_LEFT)  { IconNavigate(-1, 0); return 0; }
                if (wParam == VK_RIGHT) { IconNavigate(1, 0);  return 0; }
                if (wParam == VK_UP)    { IconNavigate(0, -1); return 0; }
                if (wParam == VK_DOWN)  { IconNavigate(0, 1);  return 0; }
                if (wParam == VK_PRIOR) { IconPageMove(-1);     return 0; }
                if (wParam == VK_NEXT)  { IconPageMove(1);      return 0; }
                if (wParam == VK_HOME)  { IconMoveTo(0);        return 0; }
                if (wParam == VK_END)   { IconMoveTo(m_pIconTiles->GetCount() - 1); return 0; }
                if (wParam == VK_RETURN || wParam == VK_SPACE) {
                    IconActivateCursor();
                    return 0;
                }
            }
            // ---- navigation ----
            if (wParam == VK_F5) {
                RefreshListing();
                return 0;
            }
            if (wParam == VK_BACK && !ctrl) {
                GoBack();
                return 0;
            }
            if (alt && wParam == VK_LEFT) {
                GoBack();
                return 0;
            }
            if (alt && wParam == VK_RIGHT) {
                GoForward();
                return 0;
            }
            if (alt && wParam == VK_UP) {
                GoUp();
                return 0;
            }
            if (alt && wParam == 'D') {
                EnterAddressEditMode();
                return 0;
            }
            // ---- clipboard / file operations ----
            if (ctrl && wParam == 'C') {
                OnCopyClicked();
                return 0;
            }
            if (ctrl && wParam == 'V') {
                OnPasteClicked();
                return 0;
            }
            if (ctrl && wParam == 'X') {
                OnCutClicked();
                return 0;
            }
            if (ctrl && wParam == 'Z') {
                OnUndo();
                return 0;
            }
            if (ctrl && shift && wParam == 'N') {
                OnNewFolderClicked();
                return 0;
            }
            if (ctrl && wParam == 'A') {
                SelectAllItems();
                return 0;
            }
            if (wParam == VK_DELETE && shift) {
                OnDeleteClicked(/*permanent*/ true);
                return 0;
            }
            if (wParam == VK_DELETE) {
                OnDeleteClicked();
                return 0;
            }
            if (wParam == VK_F2) {
                OnRenameClicked();
                return 0;
            }
            if (wParam == VK_F3 || (ctrl && wParam == 'F')) {
                FocusSearchBox();
                return 0;
            }
            if (alt && wParam == VK_RETURN) {
                ShowPropertiesForSelection();
                return 0;
            }
            // ---- tabs ----
            if (ctrl && wParam == 'T') {
                OnNewTabRequested();
                return 0;
            }
            if (ctrl && wParam == 'W') {
                if (m_activeTab >= 0)
                    CloseTab(m_activeTab);
                return 0;
            }
            if (ctrl && wParam == VK_F4) {
                if (m_activeTab >= 0)
                    CloseTab(m_activeTab);
                return 0;
            }
            if (ctrl && wParam == VK_TAB) {
                const int count = static_cast<int>(m_tabs.size());
                if (count > 1 && m_activeTab >= 0) {
                    const int step = shift ? -1 : 1;
                    ActivateTab((m_activeTab + step + count) % count);
                }
                return 0;
            }
        }
    }
    if (uMsg == WM_THEMECHANGED || (uMsg == WM_SETTINGCHANGE && wParam == 0)) {
        ApplyDwmChrome();   // backdrop / AppsUseLightTheme changed while running
    }
    return WindowImplBase::HandleMessage(uMsg, wParam, lParam);
}

LRESULT CMainWnd::TranslateAccelerator(MSG* message)
{
    if (!message || (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN)) return S_FALSE;
    if (message->hwnd != m_hWnd && !IsChild(m_hWnd, message->hwnd)) return S_FALSE;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const WPARAM key = message->wParam;
    wchar_t className[64]{};
    GetClassNameW(message->hwnd, className, _countof(className));
    const bool shellWindow = m_shellBrowser && m_shellBrowser->OwnsWindow(message->hwnd);
    const bool textEdit = _wcsicmp(className, L"Edit") == 0 ||
        _wcsnicmp(className, L"RichEdit", 8) == 0 || (!shellWindow && IsEditingText());
    // App navigation works from every pane, including native text edits. File
    // operations below never intercept the text edit's own C/X/V/Z/Y/Delete keys.
    if (ctrl && !alt && key == VK_TAB) {
        bool handled = false;
        MessageHandler(message->message, key, message->lParam, handled);
        return handled ? S_OK : S_FALSE;
    }
    if ((!alt && ctrl && (key == 'L')) || (!ctrl && alt && key == 'D') ||
        (!ctrl && !alt && key == VK_F4)) {
        if (key == VK_F4) ShowAddressHistory(); else EnterAddressEditMode();
        return S_OK;
    }
    if ((!alt && ctrl && (key == 'F' || (key == 'E' && !shift))) || (!ctrl && !alt && key == VK_F3)) {
        FocusSearchBox(); return S_OK;
    }
    if (!ctrl && !alt && key == VK_F6) { CycleKeyboardPane(shift); return S_OK; }
    if (!ctrl && !alt && key == VK_F11) {
        SendMessage(WM_SYSCOMMAND, IsZoomed(m_hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0); return S_OK;
    }
    if (ctrl && shift && !alt && key == 'E') {
        SyncTreeToPath(m_currentPath); if (m_pDirTree) m_pDirTree->SetFocus(); return S_OK;
    }
    if (!alt && ((ctrl && key == 'R') || key == VK_F5)) { RefreshListing(); return S_OK; }
    if (!ctrl && alt && (key == VK_LEFT || key == VK_RIGHT || key == VK_UP)) {
        if (key == VK_LEFT) GoBack(); else if (key == VK_RIGHT) GoForward(); else GoUp();
        return S_OK;
    }
    if (!ctrl && alt && key == 'P') { SetPreviewVisible(!m_previewVisible); return S_OK; }
    if (!alt && ctrl && !shift && key >= '1' && key <= '9') {
        const int index = key == '9' ? int(m_tabs.size()) - 1 : int(key - '1');
        if (index >= 0 && index < int(m_tabs.size())) ActivateTab(index);
        return S_OK;
    }
    if (!alt && ctrl && key == 'T') { OnNewTabRequested(); return S_OK; }
    if (!alt && ctrl && (key == 'W' || key == VK_F4)) {
        if (m_activeTab >= 0) CloseTab(m_activeTab);
        return S_OK;
    }
    if (key == VK_ESCAPE && !ctrl && !alt) {
        if (m_addressEditMode) { ExitAddressEditMode(false); FocusFileView(); return S_OK; }
        if (m_pSearchEdit && ::GetFocus() == m_pSearchEdit->GetNativeEditHWND()) {
            ClearSearchFilter(); FocusFileView(); return S_OK;
        }
        if (!textEdit) { ClearFileSelection(); return S_OK; }
        m_pendingShellRename.clear();
        return S_FALSE;
    }
    if (textEdit) return S_FALSE;
    if (!ctrl && !alt && IsTreeKeyboardFocus() && HandleTreeShortcut(key)) return S_OK;
    if (!alt && ctrl && shift && key >= '1' && key <= '8') {
        const ViewMode modes[] = {ViewMode::ExtraLargeIcons, ViewMode::LargeIcons,
            ViewMode::MediumIcons, ViewMode::SmallIcons, ViewMode::List,
            ViewMode::Details, ViewMode::Tiles, ViewMode::Content};
        SetViewMode(modes[key - '1']); return S_OK;
    }
    if (!alt && ctrl && key == 'N') {
        if (shift) OnNewFolderClicked(); else OpenPathInNewWindow(m_currentPath, {0, 0});
        return S_OK;
    }
    if (!alt && ctrl && key == 'C' && shift) { OnCopyPaths(); return S_OK; }
    if (!alt && ctrl && (key == 'C' || key == VK_INSERT)) { OnCopyClicked(); return S_OK; }
    if (!alt && ((ctrl && key == 'V') || (!ctrl && shift && key == VK_INSERT))) { OnPasteClicked(); return S_OK; }
    if (!alt && ctrl && key == 'X') { OnCutClicked(); return S_OK; }
    if (!alt && ctrl && key == 'Z') { if (shift) OnRedo(); else OnUndo(); return S_OK; }
    if (!alt && ctrl && key == 'Y') { OnRedo(); return S_OK; }
    if (!alt && ctrl && key == 'A') { SelectAllItems(); return S_OK; }
    if (!alt && (key == VK_DELETE || (ctrl && key == 'D'))) { OnDeleteClicked(shift); return S_OK; }
    if (!ctrl && !alt && key == VK_F2) { OnRenameClicked(); return S_OK; }
    if (!ctrl && !alt && key == VK_BACK) { GoBack(); return S_OK; }
    if (!ctrl && alt && key == VK_RETURN) { ShowPropertiesForSelection(); return S_OK; }
    if (!ctrl && !alt && (key == VK_APPS || (shift && key == VK_F10)) &&
        !(m_shellBrowser && m_shellBrowser->OwnsWindow(message->hwnd))) {
        RECT bounds = m_pFileList ? m_pFileList->GetPos() : RECT{0,0,100,100};
        POINT point{bounds.left + 20, bounds.top + 40}; ClientToScreen(m_hWnd, &point);
        std::vector<ClipboardItem> selected; CollectSelectedItems(selected);
        std::vector<std::wstring> paths; for (const auto& item : selected) paths.push_back(item.path);
        if (paths.empty()) ShowBlankAreaContextMenu(point); else ShowShellContextMenu(paths, point);
        return S_OK;
    }
    return m_shellBrowser ? m_shellBrowser->TranslateAccelerator(message) : S_FALSE;
}
// DuiLib calls its pre-message filters from CPaintManagerUI::TranslateMessage, *before*
// DispatchMessage, and its own handler turns any WM_KEYDOWN/VK_TAB into control tabbing - so a
// Ctrl+Tab never reaches the window proc. Claim the message here (bHandled) and cycle tabs.
LRESULT CMainWnd::MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, bool& bHandled)
{
    if (uMsg == WM_KEYDOWN && wParam == VK_TAB
        && (::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        const int count = static_cast<int>(m_tabs.size());
        if (count > 1 && m_activeTab >= 0) {
            const int step = (::GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
            ActivateTab((m_activeTab + step + count) % count);
        }
        bHandled = true;
        return 0;
    }
    return WindowImplBase::MessageHandler(uMsg, wParam, lParam, bHandled);
}

// Push the exe's own icon (resource id 1, see res\FastFile.rc) onto the window.
// DuiLib's CWindowWnd::RegisterWindowClass sets wc.hIcon = NULL, so without this the
// title bar, taskbar button and Alt-Tab entry would stay blank.
void CMainWnd::ApplyWindowIcon()
{
    if (!m_hWnd || !::IsWindow(m_hWnd))
        return;
    HINSTANCE inst = ::GetModuleHandleW(nullptr);
    const int cxBig = ::GetSystemMetrics(SM_CXICON);
    const int cyBig = ::GetSystemMetrics(SM_CYICON);
    const int cxSmall = ::GetSystemMetrics(SM_CXSMICON);
    const int cySmall = ::GetSystemMetrics(SM_CYSMICON);

    // LoadImage picks the best frame for each size; the icon file ships 16/32/48/128/256.
    HICON big = static_cast<HICON>(::LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        cxBig, cyBig, LR_DEFAULTCOLOR));
    HICON smallIcon = static_cast<HICON>(::LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
        cxSmall, cySmall, LR_DEFAULTCOLOR));
    if (big)
        ::SendMessageW(m_hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    if (smallIcon)
        ::SendMessageW(m_hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
}

LRESULT CMainWnd::ResponseDefaultKeyEvent(WPARAM wParam)
{
    if (wParam == VK_ESCAPE) {
        if (m_addressEditMode) {
            ExitAddressEditMode(false);
            return TRUE;
        }
        if (!IsEditingText() && HasFileSelection()) {
            ClearFileSelection();
            UpdateListingStatusTip();
        }
        return TRUE;
    }
    return WindowImplBase::ResponseDefaultKeyEvent(wParam);
}

bool CMainWnd::IsEditingText() const
{
    const HWND focused = ::GetFocus();
    wchar_t className[64]{};
    if (focused) GetClassNameW(focused, className, _countof(className));
    if (_wcsicmp(className, L"Edit") == 0 || _wcsnicmp(className, L"RichEdit", 8) == 0)
        return true;
    if (m_shellBrowser && m_shellBrowser->OwnsWindow(focused)) return false;
    CControlUI* pFocus = m_PaintManager.GetFocus();
    if (!pFocus || pFocus->GetInterface(DUI_CTR_EDIT) == nullptr)
        return false;
    // The address box keeps its DuiLib focus after edit mode ends (its native edit window
    // is hidden, not destroyed), so only treat it as editing while the mode is active.
    if (pFocus == static_cast<CControlUI*>(m_pAddressEdit) && !m_addressEditMode)
        return false;
    return true;
}

bool CMainWnd::HasFileSelection() const
{
    if (m_shellBrowser && m_shellBrowser->IsVisible()) {
        std::vector<std::pair<std::wstring, bool>> selected;
        return m_shellBrowser->GetSelection(selected) && !selected.empty();
    }
    if (IsTileViewMode() && m_pIconTiles) {
        const int n = m_pIconTiles->GetCount();
        for (int i = 0; i < n; ++i) {
            CControlUI* p = m_pIconTiles->GetItemAt(i);
            if (p && (p->GetTag() & 0x100) != 0)
                return true;
        }
        return false;
    }
    if (m_pFileList) {
        // Details view: selection is stored per entry, not on the pooled rows.
        for (size_t i = 0; i < m_detailsSel.size(); ++i) {
            if (m_detailsSel[i])
                return true;
        }
        const int n = m_pFileList->GetCount();
        for (int i = 0; i < n; ++i) {
            CControlUI* p = m_pFileList->GetItemAt(i);
            if (!p) continue;
            IListItemUI* li = static_cast<IListItemUI*>(p->GetInterface(DUI_CTR_ILISTITEM));
            if (li && li->IsSelected())
                return true;
        }
        if (m_pFileList->GetCurSel() >= 0)
            return true;
    }
    return false;
}

void CMainWnd::ClearFileSelection()
{
    if (m_shellBrowser && m_shellBrowser->IsVisible()) {
        m_shellBrowser->ClearSelection();
        UpdatePreviewForSelection(); UpdateCommandBarState();
        return;
    }
    if (IsTileViewMode()) {
        ClearIconSelection();
        m_iconAnchor = -1;
    } else {
        if (m_pFileList && m_pFileList->GetCount() > 0)
            m_pFileList->UnSelectAllItems();
        std::fill(m_detailsSel.begin(), m_detailsSel.end(), 0);
        m_detailsCur = -1;
        m_detailsAnchor = -1;
        ApplyDetailsSelectionVisuals();
    }
    ClearPreview();
}

bool CMainWnd::IsFileViewBlankHit(CControlUI* hit) const
{
    if (!hit)
        return false;

    bool overFileView = false;
    for (CControlUI* p = hit; p; p = p->GetParent()) {
        // Scrolling and column resizing are not blank-area gestures.
        if (p->GetInterface(DUI_CTR_SCROLLBAR)
            || p->GetInterface(DUI_CTR_LISTHEADER)
            || p->GetInterface(DUI_CTR_LISTHEADERITEM))
            return false;

        const CDuiString name = p->GetName();
        if (name == _T("file_list") || name == _T("file_icons")
            || name == _T("icon_scroll") || name == _T("list_host")) {
            overFileView = true;
            continue;
        }
        if (p->GetInterface(DUI_CTR_TREENODE) || name == _T("dir_tree")
            || name == _T("left_panel") || name == _T("left_quick")
            || name == _T("left_thispc") || name == _T("favorites_bar"))
            return false;
    }
    if (!overFileView)
        return false;

    CControlUI* listItem = FindListItemRoot(hit);
    if (listItem && !listItem->GetUserData().IsEmpty())
        return false;

    if (IsTileViewMode() && m_pIconTiles) {
        CControlUI* tile = hit;
        while (tile && tile->GetParent() != m_pIconTiles)
            tile = tile->GetParent();
        if (tile && !tile->GetUserData().IsEmpty())
            return false;
    }
    return true;
}

LRESULT CMainWnd::HandleCustomMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    if (uMsg == kMsgDetailsFill) {
        bHandled = TRUE;
        OnDetailsFillTick();
        return 0;
    }
    if (uMsg == kMsgVirtSync) {
        bHandled = TRUE;
        SyncVisibleIconWindow(false);
        return 0;
    }
    if (uMsg == kMsgReactivate) {
        bHandled = TRUE;
        BringToForeground();
        return 0;
    }
    if (uMsg == WM_ACTIVATE) {
        if (LOWORD(wParam) != WA_INACTIVE)
            EnsureMainWindowVisible();
        bHandled = FALSE;
        return 0;
    }
    if (uMsg == WM_ACTIVATEAPP) {
        if (wParam)
            EnsureMainWindowVisible();
        bHandled = FALSE;
        return 0;
    }
    if (uMsg == kMsgCopyProgress) {
        bHandled = TRUE;
        OnCopyProgressMessage();
        return 0;
    }
    if (uMsg == kMsgCopyFinished) {
        bHandled = TRUE;
        OnCopyFinishedMessage(wParam);
        return 0;
    }
    if (uMsg == kMsgThumbReady) {
        bHandled = TRUE;
        OnThumbReadyMessage(lParam);
        return 0;
    }
    if (uMsg == kMsgDeferredNav) {
        bHandled = TRUE;
        auto* pending = reinterpret_cast<std::pair<std::wstring, bool>*>(lParam);
        if (pending) {
            const std::wstring path = pending->first;
            const bool addToHistory = pending->second;
            delete pending;                 // free before navigating: it can post more work
            NavigateToNow(path, addToHistory);
        }
        return 0;
    }
    if (uMsg == kMsgOpenExternalPaths) {
        bHandled = TRUE;
        auto* paths = reinterpret_cast<std::vector<std::wstring>*>(lParam);
        if (paths) {
            // Startup already filters legacy cached --open activations. IPC
            // represents an explicit FastFile request (--shell-folder, a bare
            // path or another FastFile window), independent of default takeover.
            BringToForeground();
            OpenExternalPaths(*paths, false);
            delete paths;
        }
        return 0;
    }
    return WindowImplBase::HandleCustomMessage(uMsg, wParam, lParam, bHandled);
}

void CMainWnd::EnsureMainWindowVisible()
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
    if (!::IsWindowVisible(m_hWnd))
        ::ShowWindow(m_hWnd, SW_SHOW);
    if (::IsIconic(m_hWnd))
        ::ShowWindow(m_hWnd, SW_RESTORE);
    ::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
}

void CMainWnd::BringToForeground()
{
    if (!m_hWnd || !::IsWindow(m_hWnd)) return;
    EnsureMainWindowVisible();
    if (::IsIconic(m_hWnd))
        ::ShowWindow(m_hWnd, SW_RESTORE);
    else
        ::ShowWindow(m_hWnd, SW_SHOW);

    HWND hFg = ::GetForegroundWindow();
    DWORD ourTid = ::GetCurrentThreadId();
    DWORD fgTid = 0;
    BOOL attached = FALSE;
    if (hFg && hFg != m_hWnd) {
        fgTid = ::GetWindowThreadProcessId(hFg, nullptr);
        if (fgTid != 0 && fgTid != ourTid)
            attached = ::AttachThreadInput(ourTid, fgTid, TRUE);
    }
    ::BringWindowToTop(m_hWnd);
    ::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ::SetForegroundWindow(m_hWnd);
    ::SetActiveWindow(m_hWnd);
    ::SetFocus(m_hWnd);
    if (attached)
        ::AttachThreadInput(ourTid, fgTid, FALSE);
    if (::GetForegroundWindow() != m_hWnd) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_MENU;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_MENU;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        ::SendInput(2, inputs, sizeof(INPUT));
        ::SetForegroundWindow(m_hWnd);
        ::BringWindowToTop(m_hWnd);
    }
    ::InvalidateRect(m_hWnd, nullptr, FALSE);
    ::UpdateWindow(m_hWnd);
    UpdateStatus(_T("已恢复前台显示"));
}
