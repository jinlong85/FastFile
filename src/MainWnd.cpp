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
        // DuiLib's WindowImplBase::OnClose only clears bHandled and never posts WM_QUIT,
        // so without this the process lives on with no window after a close (it also keeps
        // FastFile.exe locked, which blocks rebuilds). Quit once the window is gone.
        ::PostQuitMessage(0);
    }
    if (uMsg == WM_TIMER) {
        if (wParam == kTimerVirtSync) { SyncVisibleIconWindow(false); return 0; }
        if (wParam == kTimerColWidth) { CaptureColumnWidths(); return 0; }
        if (wParam == kTimerDetailsSync) { UpdateDetailsWindow(false); return 0; }
        if (wParam == kTimerLayoutSync) { SyncLayoutDependents(); return 0; }
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
                AddTab(p->GetUserData().GetData(), true, true);
                return 0;
            }
        }
    }
    if (uMsg == WM_LBUTTONDOWN && !m_inDoDragDrop) {
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
