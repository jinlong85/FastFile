// FastFile - per-monitor DPI scaling (fonts, chrome metrics, resize handling)
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

namespace {

// Target outer size for the monitor the window is on: the 96-DPI design size scaled to the
// current DPI, clamped so it always fits the monitor work area. The design size is already
// large (1180x740 becomes 1770x1110 at 150%), so on a small work area - a 1366x768 laptop at
// 150% leaves roughly 911x512 - the unclamped window would hang off-screen and hide the
// status bar.
SIZE ClampSizeToWorkArea(HWND hWnd, SIZE size)
{
    HMONITOR mon = ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (mon && ::GetMonitorInfoW(mon, &mi)) {
        const int availW = mi.rcWork.right - mi.rcWork.left;
        const int availH = mi.rcWork.bottom - mi.rcWork.top;
        if (size.cx > availW) size.cx = availW;
        if (size.cy > availH) size.cy = availH;
    }
    return size;
}

// Keep an outer rect inside the monitor work area: a remembered position or the rect Windows
// suggests on WM_DPICHANGED can otherwise leave part of the window off-screen.
RECT ClampRectToWorkArea(HWND hWnd, RECT rc)
{
    HMONITOR mon = ::MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (mon && ::GetMonitorInfoW(mon, &mi)) {
        const RECT& wa = mi.rcWork;
        if (rc.right > wa.right) { rc.left -= rc.right - wa.right; rc.right = wa.right; }
        if (rc.bottom > wa.bottom) { rc.top -= rc.bottom - wa.bottom; rc.bottom = wa.bottom; }
        if (rc.left < wa.left) { rc.right += wa.left - rc.left; rc.left = wa.left; }
        if (rc.top < wa.top) { rc.bottom += wa.top - rc.top; rc.top = wa.top; }
        // Window bigger than the work area: pin the origin so the top/left stay reachable.
        if (rc.left < wa.left) rc.left = wa.left;
        if (rc.top < wa.top) rc.top = wa.top;
    }
    return rc;
}

} // namespace

namespace {

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

UINT QueryWindowDpi(HWND hwnd)
{
    if (hwnd) {
        using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
        static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
            ::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (fn) {
            UINT d = fn(hwnd);
            if (d >= 72) return d;
        }
        HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (mon) {
            using GetDpiForMonitorFn = HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*);
            static HMODULE shcore = ::LoadLibraryW(L"Shcore.dll");
            static GetDpiForMonitorFn getMon = shcore
                ? reinterpret_cast<GetDpiForMonitorFn>(::GetProcAddress(shcore, "GetDpiForMonitor"))
                : nullptr;
            if (getMon) {
                UINT dx = 0, dy = 0;
                if (SUCCEEDED(getMon(mon, 0, &dx, &dy)) && dx >= 72)
                    return dx;
            }
        }
    }
    HDC hdc = ::GetDC(hwnd ? hwnd : nullptr);
    int dpi = hdc ? ::GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc) ::ReleaseDC(hwnd ? hwnd : nullptr, hdc);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}

} // namespace

int CMainWnd::DpiScale(int px) const
{
    if (px == 0) return 0;
    return ::MulDiv(px, static_cast<int>(m_dpi), 96);
}

float CMainWnd::DpiScaleF(float v) const
{
    return v * (static_cast<float>(m_dpi) / 96.0f);
}

void CMainWnd::RefreshDpiFromWindow()
{
    m_dpi = QueryWindowDpi(m_hWnd);
    if (m_dpi < 72) m_dpi = 96;
}

void CMainWnd::ApplyDpiScaledFonts()
{
    // XML Font entries use 96-DPI pixel sizes; re-register at current DPI.
    // Prefer Segoe UI (Fluent); YaHei UI as CJK-friendly companion (id 4).
    const LPCTSTR face = _T("Segoe UI");
    const LPCTSTR faceCn = _T("Microsoft YaHei UI");
    m_PaintManager.AddFont(0, face, DpiScale(UiTokens::FontBody), false, false, false);
    m_PaintManager.SetDefaultFont(face, DpiScale(UiTokens::FontBody), false, false, false);
    m_PaintManager.AddFont(1, face, DpiScale(UiTokens::FontBody), true, false, false);
    m_PaintManager.AddFont(2, face, DpiScale(UiTokens::FontSmall), false, false, false);
    m_PaintManager.AddFont(3, face, DpiScale(UiTokens::FontCaption), false, false, false);
    m_PaintManager.AddFont(4, faceCn, DpiScale(UiTokens::FontBody), false, false, false);
    m_PaintManager.AddFont(5, face, DpiScale(UiTokens::FontPreviewTitle), true, false, false);
    // Command-bar / navigation glyphs: 16 design px keeps them just above the 12px labels,
    // matching Explorer's icon-to-label ratio (was 18, which crowded the text).
    m_PaintManager.AddFont(6, _T("Segoe MDL2 Assets"), DpiScale(UiTokens::ToolbarGlyphPx), false, false, false);
}

void CMainWnd::ApplyDpiScaledChrome()
{
    // Scale chrome bands + key panels from 96-DPI design sizes in main.xml.
    // Phase 3: command bar is 40px; separators/gaps DPI-scaled.
    ScaleNamedFixed(m_PaintManager, _T("title_bar"), 0, UiTokens::TitleBarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("tab_bar"), 0, UiTokens::TabBarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("toolbar"), 0, UiTokens::ToolbarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("favorites_bar"), 0, UiTokens::FavoritesBarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("address_bar"), 0, UiTokens::AddressBarH, m_dpi);
    // breadcrumb merged into address_bar (Explorer-style); no separate breadcrumb_bar
    ScaleNamedFixed(m_PaintManager, _T("status_bar"), 0, UiTokens::StatusBarH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("left_panel"), 220, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_pane"), UiTokens::PreviewPaneW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_image"), 0, UiTokens::PreviewImageH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_title"), 0, UiTokens::PreviewTitleH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_title"), 0, UiTokens::PreviewTitleGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_action"), 0, UiTokens::SpaceSm + 2, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_image"), 0, UiTokens::PreviewImageGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_text"), 0, UiTokens::PreviewTextGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_type"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_size"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_mtime"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_ctime"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_location"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_dimensions"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_duration"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_framerate"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_bitrate"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_row_totalbitrate"), 0, UiTokens::PreviewMetaRowH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_lbl_type"), UiTokens::PreviewMetaLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_lbl_size"), UiTokens::PreviewMetaLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_lbl_mtime"), UiTokens::PreviewMetaLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_lbl_ctime"), UiTokens::PreviewMetaLabelW, 0, m_dpi);
    for (LPCTSTR label : { _T("preview_lbl_location"), _T("preview_lbl_dimensions"),
        _T("preview_lbl_duration"), _T("preview_lbl_framerate"), _T("preview_lbl_bitrate"),
        _T("preview_lbl_totalbitrate") })
        ScaleNamedFixed(m_PaintManager, label, UiTokens::PreviewMetaLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("btn_preview_share"), UiTokens::PreviewActionW, UiTokens::PreviewActionH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("btn_cancel_copy"), UiTokens::StatusCancelW, UiTokens::StatusCancelH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("view_switcher"), 248, 28, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("search_box"), 210, UiTokens::SearchBoxH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("nav_hdr_quick"), 0, UiTokens::NavSectionHeaderH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("nav_hdr_thispc"), 0, UiTokens::NavSectionHeaderH, m_dpi);
    if (m_pLeftQuick) {
        m_pLeftQuick->SetSepHeight(DpiScale(UiTokens::LeftNavSepH));
        m_pLeftQuick->SetMinHeight(DpiScale(UiTokens::LeftQuickMinH));
        m_pLeftQuick->SetMaxHeight(DpiScale(720));
        if (m_leftQuickDesignH > 0)
            m_pLeftQuick->SetFixedHeight(DpiScale(m_leftQuickDesignH));
    }
    ScaleNamedFixed(m_PaintManager, _T("fav_bar_label"), UiTokens::FavLabelW, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("fav_bar_hint"), 180, 0, m_dpi);
    // Divider between the Quick Access block and the This PC tree (the drag band DuiLib
    // provides sits at the bottom of left_quick, immediately above this line).
    ScaleNamedFixed(m_PaintManager, _T("left_nav_divider_host"), 0, 13, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("left_nav_divider"), 0, 1, m_dpi);

    // Subtle vertical separators between command-bar groups
    const LPCTSTR seps[] = {
        _T("sep_new"), _T("sep_organize"), _T("sep_more"),
    };
    for (auto name : seps) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(1));
        c->SetFixedHeight(DpiScale(UiTokens::SepH));
    }
    const LPCTSTR gaps[] = {
        _T("gap_clip"), _T("gap_clip2"),
        _T("gap_org"), _T("gap_org2"), _T("gap_more"), _T("gap_more2"),
    };
    for (auto name : gaps) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(8));
    }

    // Compact command-bar buttons.
    const LPCTSTR toolbarBtns[] = {
        _T("btn_copy"), _T("btn_paste"), _T("btn_cut"), _T("btn_share"),
        _T("btn_delete"), _T("btn_rename"),
        _T("btn_new"), _T("btn_newfolder"), _T("btn_sort"), _T("btn_view_menu"), _T("btn_more"),
        _T("btn_toggle_preview"),
        _T("btn_view_xlarge"), _T("btn_view_large"), _T("btn_view_medium"),
        _T("btn_view_list"), _T("btn_view_details"), _T("btn_view_tiles"),
        _T("fav_thispc"), _T("fav_documents"), _T("fav_desktop"), _T("fav_downloads"),
    };
    for (auto name : toolbarBtns) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) continue;
        const int fh = c->GetFixedHeight();
        if (fh > 0)
            c->SetFixedHeight(DpiScale(UiTokens::CmdBtnH));
    }
    // Explicit widths for command-bar glyph buttons
    struct BtnW { LPCTSTR name; int w; };
    const BtnW widths[] = {
        { _T("btn_back"), UiTokens::ToolbarNavBtnW }, { _T("btn_forward"), UiTokens::ToolbarNavBtnW },
        { _T("btn_up"), UiTokens::ToolbarNavBtnW }, { _T("btn_refresh"), UiTokens::ToolbarNavBtnW },
        { _T("btn_new"), 76 },
        { _T("btn_cut"), UiTokens::ToolbarBtnW }, { _T("btn_copy"), UiTokens::ToolbarBtnW },
        { _T("btn_paste"), UiTokens::ToolbarBtnW }, { _T("btn_rename"), UiTokens::ToolbarBtnW },
        { _T("btn_share"), UiTokens::ToolbarBtnW }, { _T("btn_delete"), UiTokens::ToolbarBtnW },
        { _T("btn_sort"), 76 },
        { _T("btn_view_menu"), 76 },
        { _T("btn_more"), UiTokens::ToolbarBtnW },
        { _T("btn_toggle_preview"), UiTokens::ToolbarBtnW },
        { _T("btn_tab_add"), 28 },
        { _T("chk_recursive"), UiTokens::SearchChkW },
    };
    for (const auto& bw : widths) {
        CControlUI* c = m_PaintManager.FindControl(bw.name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(bw.w));
    }
    if (CControlUI* addTab = m_PaintManager.FindControl(_T("btn_tab_add")))
        addTab->SetFixedHeight(DpiScale(UiTokens::HitTabH));
    // Search row: align with address (~28-32), not CmdBtnH 42
    {
        const int sh = DpiScale(UiTokens::SearchBoxH);
        for (LPCTSTR nm : { _T("chk_recursive") }) {
            if (CControlUI* c = m_PaintManager.FindControl(nm))
            c->SetFixedHeight(DpiScale(UiTokens::SearchBoxH));
        }
        if (CControlUI* box = m_PaintManager.FindControl(_T("search_box")))
            box->SetFixedHeight(sh);
        for (LPCTSTR nm : { _T("btn_back"), _T("btn_forward"), _T("btn_up"), _T("btn_refresh") }) {
            if (CControlUI* c = m_PaintManager.FindControl(nm))
                c->SetFixedHeight(sh);
        }
        if (CControlUI* gap = m_PaintManager.FindControl(_T("gap_address_nav")))
            gap->SetFixedWidth(DpiScale(UiTokens::SpaceXs));
    }
    // Standard DuiLib caption controls: max/restore visibility is updated by WindowImplBase.
    const LPCTSTR captionBtns[] = { _T("minbtn"), _T("maxbtn"), _T("restorebtn"), _T("closebtn") };
    for (auto name : captionBtns) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(46));
        c->SetFixedHeight(DpiScale(32));
    }
    // XML attributes are design values. Keep all non-client hit areas in the same DPI space.
    RECT sizeBox = { DpiScale(4), DpiScale(4), DpiScale(4), DpiScale(4) };
    m_PaintManager.SetSizeBox(sizeBox);
    RECT caption = { 0, 0, 0, DpiScale(UiTokens::TitleBarH) };
    m_PaintManager.SetCaptionRect(caption);
    m_PaintManager.SetMinInfo(DpiScale(900), DpiScale(540));
    if (m_pDirTree)
        m_pDirTree->SetItemMinWidth(DpiScale(200));

    // Phase 2: details list header height from tokens
    if (m_pFileList) {
        CListHeaderUI* hdr = m_pFileList->GetHeader();
        if (hdr)
            hdr->SetFixedHeight(DpiScale(UiTokens::DetailsHeaderH));
    }

    // Phase 2: left-nav Quick Access rows use NavRowH
    const SIZE ctlRound = { DpiScale(UiTokens::RadiusControl), DpiScale(UiTokens::RadiusControl) };
    for (LPCTSTR favName : {
        _T("fav_thispc"), _T("fav_documents"), _T("fav_desktop"), _T("fav_downloads")
    }) {
        if (CControlUI* cFav = m_PaintManager.FindControl(favName)) {
            cFav->SetFixedHeight(DpiScale(UiTokens::NavRowH));
            cFav->SetBorderRound(ctlRound);
            cFav->SetAttribute(_T("font"), _T("4"));
        }
    }

    // Unify the corner language: command-bar buttons and the address/search inputs are now
    // 4px like Explorer (they used to be a mix of square and 6px).
    for (LPCTSTR nm : {
        _T("btn_new"), _T("btn_cut"), _T("btn_copy"), _T("btn_paste"), _T("btn_rename"),
        _T("btn_share"), _T("btn_delete"), _T("btn_sort"), _T("btn_view_menu"), _T("btn_more"),
        _T("btn_back"), _T("btn_forward"), _T("btn_up"), _T("btn_refresh"),
        _T("btn_tab_add"), _T("path_host"), _T("search_box")
    }) {
        if (CControlUI* c = m_PaintManager.FindControl(nm))
            c->SetBorderRound(ctlRound);
    }

    // Grow client area to design*scale on first apply so 150%/200% feels premium
    if (!m_dpiChromeApplied && m_hWnd && m_dpi != 96) {
        RECT rc = {};
        ::GetWindowRect(m_hWnd, &rc);
        const SIZE sz = ClampSizeToWorkArea(m_hWnd,
            SIZE{ DpiScale(m_designClientW), DpiScale(m_designClientH) });
        rc.right = rc.left + sz.cx;
        rc.bottom = rc.top + sz.cy;
        rc = ClampRectToWorkArea(m_hWnd, rc);
        ::SetWindowPos(m_hWnd, nullptr, rc.left, rc.top, sz.cx, sz.cy,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    m_dpiChromeApplied = true;
    m_PaintManager.NeedUpdate();
}

void CMainWnd::EnsureDpiLayout()
{
    RefreshDpiFromWindow();
    ApplyDpiScaledFonts();
    m_dpiChromeApplied = false;
    ApplyDpiScaledChrome();
    ApplyWindowCornerAndPadding();
    ApplyChromeShellIcons();
    RefreshTreeShellIcons();
    if (m_hWnd) {
        RECT rc = {};
        ::GetWindowRect(m_hWnd, &rc);
        const SIZE sz = ClampSizeToWorkArea(m_hWnd,
            SIZE{ DpiScale(m_designClientW), DpiScale(m_designClientH) });
        if (sz.cx > 0 && sz.cy > 0) {
            rc.right = rc.left + sz.cx;
            rc.bottom = rc.top + sz.cy;
            rc = ClampRectToWorkArea(m_hWnd, rc);
            ::SetWindowPos(m_hWnd, nullptr, rc.left, rc.top, sz.cx, sz.cy,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    RebuildBreadcrumb();
    RebuildTabStrip();
    if (m_hasListingCache)
        SetViewMode(m_viewMode);
    m_PaintManager.NeedUpdate();
}

void CMainWnd::OnDpiChanged(UINT newDpi, const RECT* suggested)
{
    if (newDpi < 72) newDpi = 96;
    const bool dpiChanged = (newDpi != m_dpi);
    m_dpi = newDpi;
    if (dpiChanged) {
        ApplyDpiScaledFonts();
        m_dpiChromeApplied = false;
        ApplyDpiScaledChrome();
        ApplyWindowCornerAndPadding();
        ApplyLeftNavSplitterHeight(m_leftQuickDesignH);
        ApplyFileViewScrollBars();
        StyleSidePaneScrollBars(m_pDirTree);
        StyleSidePaneScrollBars(m_pPreviewBody);
        ApplyChromeShellIcons();
        RefreshTreeShellIcons();
        RebuildBreadcrumb();
        RebuildTabStrip();
        SetViewMode(m_viewMode);
    }
    // Keep design*DPI outer size. Suggested rect is for cross-monitor position only.
    if (m_hWnd) {
        const SIZE sz = ClampSizeToWorkArea(m_hWnd,
            SIZE{ DpiScale(m_designClientW), DpiScale(m_designClientH) });
        RECT rc = {};
        ::GetWindowRect(m_hWnd, &rc);
        int x = rc.left, y = rc.top;
        if (suggested) { x = suggested->left; y = suggested->top; }
        rc.left = x;
        rc.top = y;
        rc.right = x + sz.cx;
        rc.bottom = y + sz.cy;
        rc = ClampRectToWorkArea(m_hWnd, rc);
        ::SetWindowPos(m_hWnd, nullptr, rc.left, rc.top, sz.cx, sz.cy,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    m_PaintManager.NeedUpdate();
}
