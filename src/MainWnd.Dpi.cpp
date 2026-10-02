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

// Hairlines (1 design px borders / separators) round *up*: at 150% MulDiv would give 1, which
// lands the line on a half pixel and renders blurry - Windows 11 Explorer draws them 2px there.
int CMainWnd::DpiScaleHairline(int px) const
{
    if (px <= 0) return 0;
    const int dpi = static_cast<int>(m_dpi);
    const int scaled = ::MulDiv(px, dpi, 96);
    if (scaled * 96 < px * dpi)                       // not exact -> round up
        return scaled + 1;
    return scaled < 1 ? 1 : scaled;
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
    // Navigation uses Segoe UI Latin glyphs with Windows CJK font linking.
    const LPCTSTR face = _T("Segoe UI");
    const LPCTSTR faceCn = _T("Microsoft YaHei UI");
    m_PaintManager.AddFont(0, face, DpiScale(UiTokens::FontBody), false, false, false);
    m_PaintManager.SetDefaultFont(face, DpiScale(UiTokens::FontBody), false, false, false);
    m_PaintManager.AddFont(1, face, DpiScale(UiTokens::FontBody), true, false, false);
    m_PaintManager.AddFont(2, face, DpiScale(UiTokens::FontSmall), false, false, false);
    m_PaintManager.AddFont(3, face, DpiScale(UiTokens::FontCaption), false, false, false);
    m_PaintManager.AddFont(4, face, DpiScale(m_settings.navigationFont), false, false, false);
    m_PaintManager.AddFont(5, face, DpiScale(UiTokens::FontPreviewTitle), true, false, false);
    m_PaintManager.AddFont(7, faceCn, DpiScale(UiTokens::FontTab), false, false, false);
    // Command-bar / navigation glyphs: 16 design px keeps them just above the 12px labels,
    // matching Explorer's icon-to-label ratio (was 18, which crowded the text).
    m_PaintManager.AddFont(6, _T("Segoe MDL2 Assets"), DpiScale(UiTokens::ToolbarGlyphPx), false, false, false);
    // Caption buttons use the same Shell glyph font at Explorer's title-bar size (~10px), so
    // min / max / restore / close stop falling back to the UI font's "− □ ×" text glyphs.
    m_PaintManager.AddFont(8, _T("Segoe MDL2 Assets"), DpiScale(UiTokens::FontCaption), false, false, false);
}

void CMainWnd::ApplyDpiScaledChrome()
{
    // Scale chrome bands + key panels from 96-DPI design sizes in main.xml.
    // Phase 3: command bar is 40px; separators/gaps DPI-scaled.
    ScaleNamedFixed(m_PaintManager, _T("titlebar"), 0, m_settings.tabHeight, m_dpi);
    // Command bar height/dividers: ApplyCommandBarLayout (end of this pass).
    ScaleNamedFixed(m_PaintManager, _T("favorites_bar"), 0, m_settings.favoritesHeight, m_dpi);
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
    // search_box / path_host metrics: ApplyCommandBarLayout (search width follows the row).
    ScaleNamedFixed(m_PaintManager, _T("nav_hdr_quick"), 0, UiTokens::NavSectionHeaderH, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("nav_hdr_thispc"), 0, UiTokens::NavSectionHeaderH, m_dpi);
    if (m_pLeftQuick) {
        m_pLeftQuick->SetSepHeight(DpiScale(UiTokens::LeftNavSepH));
        m_pLeftQuick->SetMinHeight(DpiScale(UiTokens::LeftQuickMinH));
        m_pLeftQuick->SetMaxHeight(DpiScale(720));
        if (m_leftQuickDesignH > 0)
            m_pLeftQuick->SetFixedHeight(DpiScale(m_leftQuickDesignH));
        UpdateLeftQuickAccessSpacing();
    }
    ScaleNamedFixed(m_PaintManager, _T("btn_favorite_toggle"), UiTokens::FavStarHitSize, m_settings.favoritesHeight-4, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("fav_star_gap"), UiTokens::FavStarGap, 0, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("fav_bar_hint"), 180, m_settings.favoritesHeight-4, m_dpi);
    // Divider between the Quick Access block and the This PC tree (the drag band DuiLib
    // provides sits at the bottom of left_quick, immediately above this line).
    ScaleNamedFixed(m_PaintManager, _T("left_nav_divider_host"), 0, 13, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("left_nav_divider"), 0, 1, m_dpi);

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
        _T("btn_toggle_preview"), _T("btn_settings"),
        _T("btn_view_xlarge"), _T("btn_view_large"), _T("btn_view_medium"),
        _T("btn_view_list"), _T("btn_view_details"), _T("btn_view_tiles"),
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
        { _T("btn_new"), UiTokens::ToolbarTextBtnMinW },
        { _T("btn_cut"), UiTokens::ToolbarBtnW }, { _T("btn_copy"), UiTokens::ToolbarBtnW },
        { _T("btn_paste"), UiTokens::ToolbarBtnW }, { _T("btn_rename"), UiTokens::ToolbarBtnW },
        { _T("btn_share"), UiTokens::ToolbarBtnW }, { _T("btn_delete"), UiTokens::ToolbarBtnW },
        { _T("btn_sort"), UiTokens::ToolbarTextBtnMinW },
        { _T("btn_view_menu"), UiTokens::ToolbarTextBtnMinW },
        { _T("btn_more"), UiTokens::ToolbarBtnW },
        { _T("btn_settings"), UiTokens::ToolbarBtnW },
        { _T("btn_toggle_preview"), UiTokens::ToolbarBtnW },
        { _T("btn_tab_add"), 32 },
        { _T("chk_recursive"), UiTokens::SearchChkW },
    };
    for (const auto& bw : widths) {
        CControlUI* c = m_PaintManager.FindControl(bw.name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(bw.w));
    }
    if (CControlUI* addTab = m_PaintManager.FindControl(_T("btn_tab_add")))
        addTab->SetFixedHeight(DpiScale(m_settings.tabHeight));
    if (CControlUI* c = m_PaintManager.FindControl(_T("chk_recursive")))
        c->SetFixedHeight(DpiScale(UiTokens::SearchBoxH));
    // Standard DuiLib caption controls: max/restore visibility is updated by WindowImplBase.
    const LPCTSTR captionBtns[] = { _T("minbtn"), _T("maxbtn"), _T("restorebtn"), _T("closebtn") };
    for (auto name : captionBtns) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) continue;
        c->SetFixedWidth(DpiScale(46));
        c->SetFixedHeight(DpiScale(m_settings.tabHeight));   // fills the compact tab row
    }
    // XML attributes are design values. Keep all non-client hit areas in the same DPI space.
    RECT sizeBox = { DpiScale(4), DpiScale(4), DpiScale(4), DpiScale(4) };
    m_PaintManager.SetSizeBox(sizeBox);
    RECT caption = { 0, 0, 0, DpiScale(m_settings.tabHeight) };
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

    // Quick-access rows are rebuilt on DPI changes (RebuildLeftQuickRows) with NavRowH and
    // the runtime row metrics, so they need no named-control pass here.
    const SIZE ctlRound = { DpiScale(UiTokens::RadiusControl), DpiScale(UiTokens::RadiusControl) };

    // Unify the corner language: command-bar buttons and the address/search inputs are now
    // 4px like Explorer (they used to be a mix of square and 6px).
    for (LPCTSTR nm : {
        _T("btn_new"), _T("btn_cut"), _T("btn_copy"), _T("btn_paste"), _T("btn_rename"),
        _T("btn_share"), _T("btn_delete"), _T("btn_sort"), _T("btn_view_menu"), _T("btn_more"),
        _T("btn_back"), _T("btn_forward"), _T("btn_up"), _T("btn_refresh"),
        _T("btn_tab_add")
    }) {
        if (CControlUI* c = m_PaintManager.FindControl(nm))
            c->SetBorderRound(ctlRound);
    }
    ApplyCommandBarLayout();

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
        StylePreviewRail();
        RebuildLeftQuickRows();   // runtime rows are sized in physical pixels
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

// Command bar + address row, aligned to Win11 Explorer measured at 150% (logical px):
//  - command bar: 48 total = hairline (command_top_divider) + white body + hairline
//    (command_body_divider), 32px buttons, icon-only buttons 40 wide on a 48 pitch, label
//    buttons 84 wide, 1x32 #F0F0F0 separators with 18px from the neighbouring ink, first icon
//    18 from the window edge;
//  - address row: 48 high, nav hit boxes 40 wide on a 48 pitch (first glyph 21 from the edge),
//    32px radius-4 address/search boxes, 8px between them, search width follows the row.
// Spacing is carried by each control's left padding so XML, code and tokens stay in step.
void CMainWnd::ApplyCommandBarLayout()
{
    using namespace UiTokens;
    auto find = [&](LPCTSTR name) { return m_PaintManager.FindControl(name); };
    auto padLeft = [&](LPCTSTR name, int left) {
        if (CControlUI* c = find(name)) c->SetPadding(RECT{ DpiScale(left), 0, 0, 0 });
    };
    auto size = [&](LPCTSTR name, int w, int h) {
        if (CControlUI* c = find(name)) {
            if (w >= 0) c->SetFixedWidth(DpiScale(w));
            if (h >= 0) c->SetFixedHeight(DpiScale(h));
        }
    };
    const int hair = DpiScaleHairline(Hairline);
    if (auto* bar = dynamic_cast<CContainerUI*>(find(_T("toolbar")))) {
        bar->SetFixedHeight((std::max)(DpiScale(ToolbarH) - 2 * hair, DpiScale(CmdBtnH)));
        bar->SetInset(RECT{ DpiScale(ToolbarPadL), 0, DpiScale(ToolbarPadR), 0 });
        bar->SetChildPadding(DpiScale(m_settings.density * 2));
        bar->SetAttribute(_T("bordersize"), _T("0"));
    }
    for (LPCTSTR name : { _T("command_top_divider"), _T("command_body_divider") }) {
        if (CControlUI* c = find(name)) {
            c->SetFixedHeight(hair);
            c->SetBkColor(ArgbCmdLine);
        }
    }
    for (LPCTSTR name : { _T("sep_new"), _T("sep_organize"), _T("sep_more") }) {
        if (CControlUI* c = find(name)) {
            c->SetFixedWidth(DpiScale(1));
            c->SetFixedHeight(DpiScale(SepH));
            c->SetBkColor(ArgbCmdSeparator);
        }
        padLeft(name, ToolbarSepMargin);
    }
    for (LPCTSTR name : { _T("btn_new"), _T("btn_sort"), _T("btn_view_menu") }) {
        size(name, ToolbarTextBtnMinW, CmdBtnH);
        if (CControlUI* c = find(name)) {
            c->SetAttribute(_T("font"), _T("7"));
            c->SetAttribute(_T("align"), _T("left"));
            c->SetAttribute(_T("textcolor"), ColorCmdText);
            c->SetAttribute(_T("disabledtextcolor"), ColorCmdTextDisabled);
        }
    }
    for (LPCTSTR name : { _T("btn_cut"), _T("btn_copy"), _T("btn_paste"), _T("btn_rename"),
                          _T("btn_share"), _T("btn_delete"), _T("btn_more"), _T("btn_settings") })
        size(name, ToolbarBtnW, CmdBtnH);
    padLeft(_T("btn_new"), 0);
    padLeft(_T("btn_cut"), ToolbarSepMargin);
    for (LPCTSTR name : { _T("btn_copy"), _T("btn_paste"), _T("btn_rename"), _T("btn_share"), _T("btn_delete") })
        padLeft(name, ToolbarItemGap);
    padLeft(_T("btn_sort"), ToolbarSortPad);
    padLeft(_T("btn_view_menu"), ToolbarLabelGap);
    padLeft(_T("btn_more"), ToolbarMorePad);
    padLeft(_T("btn_settings"), 0);

    if (auto* row = dynamic_cast<CContainerUI*>(find(_T("address_bar")))) {
        row->SetFixedHeight(DpiScale(AddressBarH));
        row->SetInset(RECT{ DpiScale(AddressBarPadL), DpiScale(AddressBarPadY),
                            DpiScale(AddressBarPadR), DpiScale(AddressBarPadY) });
        row->SetAttribute(_T("bordersize"), _T("0"));
    }
    for (LPCTSTR name : { _T("btn_back"), _T("btn_forward"), _T("btn_up"), _T("btn_refresh") }) {
        size(name, ToolbarNavBtnW, FieldH);
        padLeft(name, NavBtnGap);
    }
    padLeft(_T("btn_back"), 0);
    size(_T("gap_address_nav"), AddressNavGap, -1);
    size(_T("gap_address_search"), AddressSearchGap, -1);
    const SIZE fieldRound = { DpiScale(FieldRound), DpiScale(FieldRound) };
    for (LPCTSTR name : { _T("path_host"), _T("search_box") }) {
        size(name, -1, FieldH);
        if (CControlUI* c = find(name)) c->SetBorderRound(fieldRound);
    }
    if (auto* box = dynamic_cast<CContainerUI*>(find(_T("search_box"))))
        box->SetInset(RECT{ DpiScale(FieldPadL), DpiScale(2), DpiScale(SearchGlyphPadR), DpiScale(2) });
    if (auto* host = dynamic_cast<CContainerUI*>(find(_T("path_host"))))
        host->SetInset(RECT{ DpiScale(FieldPadL), DpiScale(2), DpiScale(FieldPadL), DpiScale(2) });
    size(_T("search_glyph"), SearchGlyphPx, SearchGlyphPx);
    padLeft(_T("search_glyph"), SearchGlyphGap);
    UpdateSearchBoxWidth();
}

void CMainWnd::UpdateSearchBoxWidth(int rowPx)
{
    CControlUI* box = m_PaintManager.FindControl(_T("search_box"));
    if (!box) return;
    if (rowPx <= 0) {
        if (CControlUI* row = m_PaintManager.FindControl(_T("address_bar")))
            rowPx = static_cast<int>(row->GetWidth());
    }
    if (rowPx <= 0 && m_hWnd) {
        RECT rc = {};
        ::GetClientRect(m_hWnd, &rc);
        rowPx = rc.right - rc.left;
    }
    const int w = UiTokens::SearchBoxWidthFor(rowPx, DpiScale(UiTokens::SearchBoxMinW),
        DpiScale(UiTokens::SearchBoxMaxW));
    if (box->GetFixedWidth() != w)
        box->SetFixedWidth(w);
}
