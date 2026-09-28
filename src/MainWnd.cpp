// FastFile - main window: lifecycle, message routing, selection, window activation
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

CMainWnd::~CMainWnd()
{
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
    RefreshDpiFromWindow();
    ApplyDpiScaledFonts();

    m_pAddressEdit = static_cast<CEditUI*>(m_PaintManager.FindControl(_T("edit_address")));
    m_pAddressEditHost = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("address_edit_host")));
    m_pPathHost = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("path_host")));
    m_pSearchEdit = static_cast<CEditUI*>(m_PaintManager.FindControl(_T("edit_search")));
    m_addressEditMode = false;
    m_pFileList = static_cast<CListUI*>(m_PaintManager.FindControl(_T("file_list")));
    m_pDirTree = static_cast<CTreeViewUI*>(m_PaintManager.FindControl(_T("dir_tree")));
    m_pTabStrip = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("tab_strip")));
    m_pIconScroll = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("icon_scroll")));
    m_pIconTiles = static_cast<CTileLayoutUI*>(m_PaintManager.FindControl(_T("file_icons")));
    m_pBreadcrumb = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("breadcrumb")));
    m_pFavoritesBar = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("favorites_bar")));
    m_pFavoritesStrip = static_cast<CHorizontalLayoutUI*>(m_PaintManager.FindControl(_T("favorites_strip")));
    m_pLeftFavPins = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("left_fav_pins")));
    m_pLeftQuick = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("left_quick")));
    m_pLeftThisPc = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("left_thispc")));
    m_pPreviewPane = static_cast<CVerticalLayoutUI*>(m_PaintManager.FindControl(_T("preview_pane")));
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
        StyleVerticalScrollBar(m_pDirTree);
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
    ApplyChromeShellIcons();
    ApplyCopyUiState();
    StartThumbWorker();
    InitDirectoryTree();
    InitTabs();
    LoadFavorites();
    LoadQuickAccess();
    RebuildFavoritesBar();
    RebuildLeftPinnedFavorites();
    InitDragDrop();
    ApplyFileViewScrollBars();
    ApplyColumnWidths();
    SetPreviewVisible(m_previewVisible);
    if (!LoadSession()) {
        const std::wstring start = GetDefaultStartPath();
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
    ApplyWindowCornerAndPadding();
    SyncRecursiveCheckLabel();
    SetSearchPlaceholder(true);
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
            return;
        }
    }
    else if (msg.sType == DUI_MSGTYPE_TEXTCHANGED) {
        if (msg.pSender == m_pSearchEdit) {
            if (m_searchPlaceholder)
                return;
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
        return;
    }
    else if (msg.sType == DUI_MSGTYPE_ITEMSELECT) {
        UpdatePreviewForSelection();
        UpdateListingStatusTip();
    }
    WindowImplBase::Notify(msg);
}

void CMainWnd::OnClick(TNotifyUI& msg)
{
    CDuiString name = msg.pSender->GetName();

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
    if (name == _T("btn_search")) {
        ApplySearchFilter();
        return;
    }
    if (name == _T("btn_search_clear")) {
        ClearSearchFilter();
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
        AddTab(m_currentPath.empty() ? GetDefaultStartPath() : m_currentPath, true);
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
    if (name == _T("btn_new") || name == _T("btn_new_glyph") || name == _T("btn_newfolder")) { OnNewMenuClicked(); return; }
    if (name == _T("btn_sort") || name == _T("btn_sort_glyph")) { OnSortMenuClicked(); return; }
    if (name == _T("btn_view_menu") || name == _T("btn_view_glyph")) { OnViewMenuClicked(); return; }
    if (name == _T("btn_more")) { OnMoreMenuClicked(); return; }
    if (name == _T("fav_thispc") || name == _T("fav_documents")
        || name == _T("fav_desktop") || name == _T("fav_downloads")) {
        OnFavoriteClicked(name);
        return;
    }
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
        CaptureColumnWidths();
        if (m_hWnd) {
            ::KillTimer(m_hWnd, kTimerVirtSync);
            ::KillTimer(m_hWnd, kTimerColWidth);
        }
        StopDetailsFill();
        SaveFavorites();
        SaveQuickAccess();
        SaveSession();
    }
    if (uMsg == WM_DESTROY) {
        // DuiLib's WindowImplBase::OnClose only clears bHandled and never posts WM_QUIT,
        // so without this the process lives on with no window after a close (it also keeps
        // FastFile.exe locked, which blocks rebuilds). Quit once the window is gone.
        ::PostQuitMessage(0);
    }
    if (uMsg == WM_TIMER) {
        if (wParam == kTimerVirtSync) { SyncVisibleIconWindow(false); return 0; }
        if (wParam == kTimerColWidth) { CaptureColumnWidths(); return 0; }
    }
    if (uMsg == WM_LBUTTONDOWN && !m_inDoDragDrop) {
        m_dragTracking = true;
        m_dragStartPt.x = (short)LOWORD(lParam);
        m_dragStartPt.y = (short)HIWORD(lParam);
    }
    if (uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONDOWN) {
        m_dragTracking = false;
    }
    if (uMsg == WM_LBUTTONUP) {
        CaptureLeftNavSplitterIfChanged();
        POINT ptClient = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        CControlUI* hit = m_PaintManager.FindControl(ptClient);
        if (IsFileViewBlankHit(hit)) {
            ClearFileSelection();
            UpdateListingStatusTip();
        }
    }
    if (uMsg == WM_RBUTTONUP) {
        POINT ptClient = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
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
                AddTab(m_currentPath, true);
                return 0;
            }
            if (ctrl && wParam == 'W') {
                if (m_activeTab >= 0)
                    CloseTab(m_activeTab);
                return 0;
            }
        }
    }
    return WindowImplBase::HandleMessage(uMsg, wParam, lParam);
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
    if (IsTileViewMode()) {
        ClearIconSelection();
        m_iconAnchor = -1;
    } else if (m_pFileList) {
        m_pFileList->UnSelectAllItems();
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
