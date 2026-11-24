// FastFile - Win11 light chrome: UI tokens, window corners, item formatting
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"
#include "FavoriteStarUI.h"

namespace {

std::wstring FormatFileTimeULONGLONG(ULONGLONG ft)
{
    if (ft == 0) return L"";
    FILETIME ftw{};
    ftw.dwLowDateTime = static_cast<DWORD>(ft & 0xFFFFFFFFu);
    ftw.dwHighDateTime = static_cast<DWORD>((ft >> 32) & 0xFFFFFFFFu);
    FILETIME local{};
    if (!::FileTimeToLocalFileTime(&ftw, &local))
        return L"";
    SYSTEMTIME st{};
    if (!::FileTimeToSystemTime(&local, &st))
        return L"";
    wchar_t buf[64] = {};
    swprintf_s(buf, L"%04u/%02u/%02u %02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

} // namespace

std::wstring CMainWnd::FormatModifiedTime(ULONGLONG ft)
{
    return FormatFileTimeULONGLONG(ft);
}

std::wstring CMainWnd::FormatDriveDisplayName(const std::wstring& rootPath)
{
    std::wstring root = rootPath;
    if (root.empty())
        return L"本地磁盘";
    if (root.back() != L'\\' && root.back() != L'/')
        root.push_back(L'\\');
    wchar_t label[MAX_PATH + 1] = {};
    wchar_t fsName[MAX_PATH + 1] = {};
    DWORD serial = 0, maxComp = 0, flags = 0;
    const BOOL ok = ::GetVolumeInformationW(root.c_str(), label, MAX_PATH,
        &serial, &maxComp, &flags, fsName, MAX_PATH);
    const wchar_t letter = static_cast<wchar_t>(::towupper(root[0]));
    const std::wstring name = (ok && label[0] != L'\0') ? label : L"本地磁盘";
    wchar_t buf[MAX_PATH + 64] = {};
    swprintf_s(buf, L"%s (%c:)", name.c_str(), letter);
    return buf;
}

bool CMainWnd::ShouldHideByAttributes(DWORD attrs) const
{
    if (m_showHidden)
        return false;
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false;
    // Explorer hides HIDDEN items; a SYSTEM-only item stays visible. The "protected
    // operating system files" option is about files that are hidden *and* system.
    // Filtering on SYSTEM alone hid the redirected profile folders
    // (D:\Users\<name>\Documents, Desktop, Downloads ... are ReadOnly|System),
    // which then looked like an empty folder.
    return (attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
}

void CMainWnd::SetShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    const std::wstring path = m_currentPath;
    InitDirectoryTree();
    if (!path.empty())
        SyncTreeToPath(path);
    RefreshListing();
    SaveSession();
    UpdateStatus(m_showHidden ? _T("已显示隐藏的项目") : _T("已隐藏隐藏的项目"));
}

void CMainWnd::ToggleShowHidden()
{
    SetShowHidden(!m_showHidden);
}

void CMainWnd::ApplyWindowCornerAndPadding()
{
    if (!m_hWnd || !::IsWindow(m_hWnd))
        return;
    // Win11 DWM rounded corners (ignored on older Windows)
    DWORD pref = DWMWCP_ROUND;
    ::DwmSetWindowAttribute(m_hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    // Soften outer gutter padding with DPI (token OuterGutter=14, was 8)
    if (CControlUI* gutter = m_PaintManager.FindControl(_T("outer_gutter"))) {
        CDuiString pad;
        const int g = DpiScale(UiTokens::OuterGutter);
        pad.Format(_T("%d,%d,%d,%d"), g, g, g, g);
        gutter->SetAttribute(_T("padding"), pad);
        gutter->SetAttribute(_T("bkcolor"), UiTokens::ColorGutter);
    }
    if (CControlUI* chrome = m_PaintManager.FindControl(_T("chrome_root"))) {
        SIZE roundSz = { DpiScale(UiTokens::ChromeRound), DpiScale(UiTokens::ChromeRound) };
        chrome->SetBorderRound(roundSz);
        chrome->SetAttribute(_T("bkcolor"), UiTokens::ColorSurface);
        chrome->SetAttribute(_T("bordercolor"), UiTokens::ColorBorderStrong);
    }
    ApplyUiChromeTokens();
    ApplyDwmChrome();
}

bool CMainWnd::IsSystemDarkTheme()
{
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS st = ::RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    return (st == ERROR_SUCCESS) && (value == 0);
}

bool CMainWnd::IsDarkThemeActive() const
{
    if (m_settings.themeMode == 2) return true;
    if (m_settings.themeMode == 1) return false;
    return IsSystemDarkTheme();
}

// DWM shell: rounded corners, Mica Alt backdrop (DWMSBT_TABBEDWINDOW) and the frame extended
// over the client area so the tab strip / title row shows the material. Everything below the
// title row paints its own opaque surface, so Mica is only visible where we want it.
// On Windows 10 / older Win11 the backdrop call fails: we then fall back to a flat title band.
void CMainWnd::ApplyDwmChrome()
{
    if (!m_hWnd || !::IsWindow(m_hWnd))
        return;
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_AUTO
#define DWMSBT_AUTO 0
#endif
#ifndef DWMSBT_NONE
#define DWMSBT_NONE 1
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif
#ifndef DWMSBT_TABBEDWINDOW
#define DWMSBT_TABBEDWINDOW 4
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

    const bool dark = IsDarkThemeActive();
    const BOOL darkMode = dark ? TRUE : FALSE;
    ::DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    const COLORREF borderNone = DWMWA_COLOR_NONE;
    ::DwmSetWindowAttribute(m_hWnd, DWMWA_BORDER_COLOR, &borderNone, sizeof(borderNone));

    int backdrop = DWMSBT_TABBEDWINDOW;
    if (m_settings.backdropType == 1) backdrop = DWMSBT_MAINWINDOW;
    else if (m_settings.backdropType == 2) backdrop = DWMSBT_TRANSIENTWINDOW;
    else if (m_settings.backdropType == 3) backdrop = DWMSBT_NONE;

    const HRESULT hr = ::DwmSetWindowAttribute(m_hWnd, DWMWA_SYSTEMBACKDROP_TYPE,
        &backdrop, sizeof(backdrop));
    m_micaActive = (backdrop != DWMSBT_NONE) && SUCCEEDED(hr);
    MARGINS margins = { 0, 0, 0, 0 };
    ::DwmExtendFrameIntoClientArea(m_hWnd, &margins);
    if (CControlUI* bar = m_PaintManager.FindControl(_T("titlebar"))) {
        bar->SetAttribute(_T("bkcolor"),
            dark ? L"#FF202020" : (m_micaActive ? UiTokens::ColorTransparent : L"#FFEDEDED"));
        bar->Invalidate();
    }
    for (LPCTSTR rootName : { _T("outer_gutter"), _T("chrome_root") }) {
        if (CControlUI* root = m_PaintManager.FindControl(rootName)) {
            root->SetAttribute(_T("bkcolor"), dark ? L"#FF202020" : UiTokens::ColorSurface);
            root->Invalidate();
        }
    }
    if (auto* star = dynamic_cast<CFavoriteStarUI*>(m_PaintManager.FindControl(_T("btn_favorite_toggle")))) {
        star->SetDarkMode(dark);
    }
    if (CControlUI* addBtn = m_PaintManager.FindControl(_T("btn_tab_add"))) {
        addBtn->SetAttribute(_T("hotbkcolor"), dark ? L"#20FFFFFF" : L"#14000000");
        addBtn->SetAttribute(_T("pushedbkcolor"), dark ? L"#30FFFFFF" : L"#22000000");
        addBtn->Invalidate();
    }
    const LPCWSTR captionText = dark ? L"#FFF3F3F3" : UiTokens::ColorTextPrimary;
    for (LPCTSTR name : { _T("minbtn"), _T("maxbtn"), _T("restorebtn"), _T("closebtn") }) {
        if (CControlUI* btn = m_PaintManager.FindControl(name)) {
            btn->SetAttribute(_T("textcolor"), captionText);
            btn->Invalidate();
        }
    }
    if (m_pTabStrip) {
        m_pTabStrip->SetDarkMode(dark);
        m_pTabStrip->SetMetrics(static_cast<int>(m_dpi));
        m_pTabStrip->Invalidate();
    }
    if (m_shellBrowser) {
        m_shellBrowser->SetDarkMode(dark);
    }
}

void CMainWnd::UpdateCaptionButtonHover(POINT ptClient, bool hovering)
{
    const bool dark = IsDarkThemeActive();
    const LPCWSTR textNorm = dark ? L"#FFF3F3F3" : UiTokens::ColorTextPrimary;
    const LPCWSTR btnHotBg = dark ? L"#FF3A3A3A" : L"#FFE0E0E0";
    struct BtnState {
        LPCTSTR name;
        LPCWSTR hotBg;
        LPCWSTR normalBg;
        LPCWSTR hotText;
        LPCWSTR normalText;
    };
    const BtnState kBtns[] = {
        { _T("minbtn"),     btnHotBg,     UiTokens::ColorTransparent, textNorm,     textNorm },
        { _T("maxbtn"),     btnHotBg,     UiTokens::ColorTransparent, textNorm,     textNorm },
        { _T("restorebtn"), btnHotBg,     UiTokens::ColorTransparent, textNorm,     textNorm },
        { _T("closebtn"),   L"#FFE81123", UiTokens::ColorTransparent, L"#FFFFFFFF", textNorm },
    };
    int hot = -1;
    if (hovering) {
        for (int i = 0; i < ARRAYSIZE(kBtns); ++i) {
            CControlUI* c = m_PaintManager.FindControl(kBtns[i].name);
            if (!c || !c->IsVisible()) continue;
            if (::PtInRect(&c->GetPos(), ptClient)) { hot = i; break; }
        }
    }
    if (hot == m_captionHot) return;
    m_captionHot = hot;
    for (int i = 0; i < ARRAYSIZE(kBtns); ++i) {
        CControlUI* c = m_PaintManager.FindControl(kBtns[i].name);
        if (!c) continue;
        const bool on = (i == hot);
        c->SetAttribute(_T("bkcolor"), on ? kBtns[i].hotBg : kBtns[i].normalBg);
        c->SetAttribute(_T("textcolor"), on ? kBtns[i].hotText : kBtns[i].normalText);
        c->Invalidate();
    }
}

void CMainWnd::ApplyUiChromeTokens()
{
    // Phase 1: DPI-scaled inner paddings + unify tab/toolbar/breadcrumb to Win11 light surface.
    auto setPad = [&](LPCTSTR name, int l, int t, int r, int b) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        CDuiString pad;
        pad.Format(_T("%d,%d,%d,%d"),
            DpiScale(l), DpiScale(t), DpiScale(r), DpiScale(b));
        c->SetAttribute(_T("padding"), pad);
    };
    // Chrome bands must use `inset`, not `padding`: a container's padding is added to the space
    // it occupies in its parent, so padding here left visible gaps between the stacked rows.
    auto setInset = [&](LPCTSTR name, int l, int t, int r, int b) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        CDuiString pad;
        pad.Format(_T("%d,%d,%d,%d"),
            DpiScale(l), DpiScale(t), DpiScale(r), DpiScale(b));
        c->SetAttribute(_T("inset"), pad);
    };
    auto setBk = [&](LPCTSTR name, LPCWSTR color) {
        if (CControlUI* c = m_PaintManager.FindControl(name))
            c->SetAttribute(_T("bkcolor"), color);
    };
    auto setBorder = [&](LPCTSTR name, LPCWSTR color, LPCTSTR size) {
        CControlUI* c = m_PaintManager.FindControl(name);
        if (!c) return;
        c->SetAttribute(_T("bordercolor"), color);
        c->SetAttribute(_T("bordersize"), size);
    };

    const int px = UiTokens::InnerPadX;
    const int py = UiTokens::InnerPadY;

    // Title row = caption: no vertical inset, so the 32px band is fully usable by the tab cards
    // and the caption buttons (an inset here squashed both).
    setInset(_T("titlebar"), 0, 0, 0, 0);
    // Command bar insets / spacing come from ApplyCommandBarLayout (Explorer metrics).
    setInset(_T("body_host"), 0, UiTokens::BodyTopGap, 0, 0);
    // This DuiLib build skips bottom-only borders; the command bar's top and bottom lines are
    // dedicated painted hairlines (command_top_divider / command_body_divider).
    // Favourite chips hang below the tab strip: the top inset keeps them off the strip and
    // lets the address row's own inset make the gaps above/below look even.
    setInset(_T("favorites_bar"), 8, UiTokens::FavBarPadTop, 8, UiTokens::FavBarPadBottom);
    setInset(_T("status_bar"), 12, 4, 12, 4);
    setPad(_T("left_panel"), UiTokens::SpaceSm, UiTokens::SpaceSm, UiTokens::SpaceSm, UiTokens::SpaceSm);
    setPad(_T("icon_scroll"), px, px, px, px);
    // Preview pane: the wrapper stays unpadded - the merged scroll rail sits flush along
    // the divider and preview_body carries the content inset (its left inset equals the
    // rail width, so the preview text keeps its original distance from the divider).
    setPad(_T("preview_pane"), 0, 0, 0, 0);
    setPad(_T("preview_body"), UiTokens::PreviewPad - UiTokens::SidePaneScrollBarW, UiTokens::PreviewPad,
        UiTokens::PreviewPad, UiTokens::PreviewPad);

    const bool dark = IsDarkThemeActive();
    const LPCWSTR surf = dark ? L"#FF202020" : UiTokens::ColorSurface;
    const LPCWSTR content = dark ? L"#FF191919" : UiTokens::ColorContent;
    const LPCWSTR border = dark ? L"#FF333333" : UiTokens::ColorChromeDivider;
    const LPCWSTR borderLight = dark ? L"#FF383838" : UiTokens::ColorBorder;
    const LPCWSTR borderStrong = dark ? L"#FF3F3F3F" : UiTokens::ColorBorderStrong;
    const LPCWSTR fieldBg = dark ? L"#FF2B2B2B" : UiTokens::ColorFieldBg;
    const LPCWSTR fieldBorder = dark ? L"#FF3E3E3E" : UiTokens::ColorFieldBorder;
    const LPCWSTR textPrimary = dark ? L"#FFF3F3F3" : UiTokens::ColorTextPrimary;
    const LPCWSTR textSecondary = dark ? L"#FFB3B3B3" : UiTokens::ColorTextSecondary;
    const LPCWSTR textMuted = dark ? L"#FF9A9A9A" : UiTokens::ColorTextMuted;
    const LPCWSTR listHover = dark ? L"#FF2D2D2D" : UiTokens::ColorListHover;
    const LPCWSTR listSelected = dark ? L"#FF364E6B" : UiTokens::ColorListSelected;
    const LPCWSTR listHeaderBg = dark ? L"#FF202020" : UiTokens::ColorListHeaderBg;
    const LPCWSTR navHover = dark ? L"#FF2D2D2D" : UiTokens::ColorNavHover;
    const LPCWSTR navSelected = dark ? L"#FF363636" : UiTokens::ColorNavSelected;
    const LPCWSTR navSection = dark ? L"#FFA0A0A0" : UiTokens::ColorNavSection;
    const LPCWSTR sepColor = dark ? L"#FF383838" : UiTokens::ColorSeparator;
    const LPCWSTR metaLabel = dark ? L"#FFA0A0A0" : L"#FF616161";

    // 1 design px dividers must land on 2 physical px at 150% (a 1.5px line renders blurry).
    CDuiString hair;
    hair.Format(_T("0,0,0,%d"), DpiScaleHairline(UiTokens::Hairline));
    for (LPCTSTR band : {
        _T("titlebar"),
        _T("status_bar"), _T("left_panel")
    }) {
        setBk(band, surf);
        setBorder(band, border, hair.GetData());
    }
    setBk(_T("favorites_bar"), content);
    setBorder(_T("favorites_bar"), border, hair.GetData());
    setBk(_T("toolbar"), content);
    setBk(_T("address_bar"), surf);
    setBorder(_T("address_bar"), UiTokens::ColorTransparent, _T("0"));
    setBorder(_T("toolbar"), UiTokens::ColorTransparent, _T("0"));
    for (LPCTSTR field : { _T("path_host"), _T("search_box") }) {
        setPad(field, 0, 0, 0, 0);
        if (CControlUI* c = m_PaintManager.FindControl(field)) {
            c->SetAttribute(_T("bkcolor"), fieldBg);
            const bool editing = m_addressEditMode && wcscmp(field, _T("path_host")) == 0;
            c->SetAttribute(_T("bordercolor"), editing ? UiTokens::ColorFieldFocus : fieldBorder);
            c->SetAttribute(_T("bordersize"), _T("1"));
            CDuiString round;
            round.Format(_T("%d,%d"), DpiScale(UiTokens::FieldRound), DpiScale(UiTokens::FieldRound));
            c->SetAttribute(_T("borderround"), round.GetData());
        }
    }
    if (m_pAddressEdit) {
        m_pAddressEdit->SetAttribute(_T("bkcolor"), fieldBg);
        m_pAddressEdit->SetAttribute(_T("nativebkcolor"), fieldBg);
        m_pAddressEdit->SetAttribute(_T("textcolor"), textPrimary);
    }
    if (m_pSearchEdit) {
        m_pSearchEdit->SetAttribute(_T("bkcolor"), fieldBg);
        m_pSearchEdit->SetAttribute(_T("nativebkcolor"), fieldBg);
        m_pSearchEdit->SetAttribute(_T("textcolor"), m_searchPlaceholder ? textMuted : textPrimary);
    }
    setPad(_T("address_edit_host"), 0, 0, 0, 0);
    // status_bar top border only
    {
        CDuiString top;
        top.Format(_T("0,%d,0,0"), DpiScaleHairline(UiTokens::Hairline));
        setBorder(_T("status_bar"), borderStrong, top.GetData());
        CDuiString right;
        right.Format(_T("0,0,%d,0"), DpiScaleHairline(UiTokens::Hairline));
        setBorder(_T("left_panel"), borderLight, right.GetData());
    }
    setBorder(_T("preview_pane"), _T("#00000000"), _T("0"));
    setBk(_T("preview_pane"), content);

    // Body / list surface
    setBk(_T("body_host"), content);
    setBk(_T("list_host"), content);
    if (m_pFileList)
        m_pFileList->SetAttribute(_T("bkcolor"), content);

    // Toolbar group gaps use SpaceSm
    for (LPCTSTR gap : {
        _T("gap_clip"), _T("gap_clip2"), _T("gap_org"), _T("gap_org2"),
        _T("gap_more"), _T("gap_more2")
    }) {
        if (CControlUI* c = m_PaintManager.FindControl(gap))
            c->SetFixedWidth(DpiScale(UiTokens::GapGroup));
    }
    ApplyCommandBarLayout();

    // Phase 2: details list hover/select + header chrome-aligned surface
    if (m_pFileList) {
        m_pFileList->SetAttribute(_T("itemhotbkcolor"), listHover);
        m_pFileList->SetAttribute(_T("itemselectedbkcolor"), listSelected);
        m_pFileList->SetAttribute(_T("itemtextcolor"), textPrimary);
        m_pFileList->SetAttribute(_T("itemselectedtextcolor"), textPrimary);
        m_pFileList->SetAttribute(_T("headerbkcolor"), listHeaderBg);
        if (CListHeaderUI* hdr = m_pFileList->GetHeader()) {
            hdr->SetAttribute(_T("bkcolor"), listHeaderBg);
            hdr->SetAttribute(_T("bordercolor"), borderLight);
            hdr->SetAttribute(_T("bordersize"), _T("0,0,0,1"));
            hdr->SetFixedHeight(DpiScale(UiTokens::DetailsHeaderH));
            const int n = hdr->GetCount();
            for (int i = 0; i < n; ++i) {
                if (CControlUI* hi = hdr->GetItemAt(i)) {
                    hi->SetAttribute(_T("textcolor"), textSecondary);
                    hi->SetAttribute(_T("sepcolor"), borderLight);
                }
            }
        }
    }

    // Phase 2: left nav surface + section headers + tree interaction colors
    setBk(_T("left_quick"), surf);
    setBk(_T("left_thispc"), surf);
    setBk(_T("left_tags"), surf);
    setBk(_T("left_nav_divider"), sepColor);
    setBk(_T("left_tags_divider"), sepColor);
    if (m_pDirTree) {
        m_pDirTree->SetAttribute(_T("bkcolor"), surf);
        m_pDirTree->SetAttribute(_T("itemhotbkcolor"), navHover);
        m_pDirTree->SetAttribute(_T("itemselectedbkcolor"), navSelected);
        m_pDirTree->SetAttribute(_T("itemtextcolor"), textPrimary);
        m_pDirTree->SetAttribute(_T("itemhottextcolor"), textPrimary);
        m_pDirTree->SetAttribute(_T("selitemtextcolor"), textPrimary);
        m_pDirTree->SetAttribute(_T("selitemhottextcolor"), textPrimary);
    }
    for (LPCTSTR hdrName : { _T("nav_hdr_quick"), _T("nav_hdr_thispc") }) {
        if (CControlUI* h = m_PaintManager.FindControl(hdrName)) {
            h->SetFixedHeight(DpiScale(UiTokens::NavSectionHeaderH));
            h->SetAttribute(_T("textcolor"), navSection);
            h->SetAttribute(_T("font"), _T("0"));
            CDuiString pad;
            pad.Format(_T("%d,%d,0,0"), DpiScale(UiTokens::NavHeaderPadL), DpiScale(UiTokens::SpaceXs));
            h->SetAttribute(_T("padding"), pad);
        }
    }

    // Phase 3: preview pane density + Surface header chrome; status bar density
    setPad(_T("preview_pane"), 0, 0, 0, 0);
    setPad(_T("preview_body"), UiTokens::PreviewPad - UiTokens::SidePaneScrollBarW, UiTokens::PreviewPad,
        UiTokens::PreviewPad, UiTokens::PreviewPad);
    setPad(_T("status_bar"), UiTokens::StatusPadX, UiTokens::StatusPadY,
        UiTokens::StatusPadX, UiTokens::StatusPadY);
    setBorder(_T("status_bar"), borderStrong, _T("0,1,0,0"));
    if (CControlUI* st = m_PaintManager.FindControl(_T("status_text"))) {
        st->SetAttribute(_T("textcolor"), textSecondary);
        st->SetAttribute(_T("font"), _T("2"));  // FontSmall
        st->SetAttribute(_T("padding"), _T("0,0,0,0"));
    }
    if (m_pPreviewTitle) {
        m_pPreviewTitle->SetFixedHeight(DpiScale(UiTokens::PreviewTitleH));
        m_pPreviewTitle->SetAttribute(_T("textcolor"), textPrimary);
        m_pPreviewTitle->SetAttribute(_T("font"), _T("5"));  // FontPreviewTitle
        m_pPreviewTitle->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
    }
    if (CControlUI* dt = m_PaintManager.FindControl(_T("preview_details_title"))) {
        dt->SetAttribute(_T("textcolor"), dark ? L"#FFE0E0E0" : L"#FF303030");
    }
    if (CControlUI* sep = m_PaintManager.FindControl(_T("preview_sep_details"))) {
        sep->SetAttribute(_T("bkcolor"), dark ? L"#FF333333" : L"#FFE8E8E8");
    }
    if (CControlUI* shareBtn = m_PaintManager.FindControl(_T("btn_preview_share"))) {
        shareBtn->SetAttribute(_T("textcolor"), dark ? L"#FFF3F3F3" : L"#FF303030");
        shareBtn->SetAttribute(_T("bkcolor"), dark ? L"#FF2B2B2B" : L"#FFFFFFFF");
        shareBtn->SetAttribute(_T("hotbkcolor"), dark ? L"#FF383838" : L"#FFF3F3F3");
        shareBtn->SetAttribute(_T("pushedbkcolor"), dark ? L"#FF454545" : L"#FFE5E5E5");
        shareBtn->SetAttribute(_T("bordercolor"), dark ? L"#FF404040" : L"#FFD6D6D6");
    }
    if (m_pChkRecursive) {
        m_pChkRecursive->SetAttribute(_T("textcolor"), textSecondary);
        m_pChkRecursive->SetAttribute(_T("hotbkcolor"), dark ? L"#FF2D2D2D" : L"#FFE5E5E5");
    }
    if (CControlUI* fh = m_PaintManager.FindControl(_T("fav_bar_hint"))) {
        fh->SetAttribute(_T("font"), _T("0"));
        fh->SetAttribute(_T("textcolor"), textMuted);
        fh->SetAttribute(_T("valign"), _T("vcenter"));
    }
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_title"), 0, UiTokens::PreviewTitleGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_action"), 0, UiTokens::SpaceSm + 2, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_image"), 0, UiTokens::PreviewImageGap, m_dpi);
    ScaleNamedFixed(m_PaintManager, _T("preview_gap_text"), 0, UiTokens::PreviewTextGap, m_dpi);
    for (LPCTSTR lblName : {
        _T("preview_lbl_type"), _T("preview_lbl_size"), _T("preview_lbl_location"),
        _T("preview_lbl_mtime"), _T("preview_lbl_ctime"), _T("preview_lbl_dimensions"),
        _T("preview_lbl_duration"), _T("preview_lbl_framerate"), _T("preview_lbl_bitrate"),
        _T("preview_lbl_totalbitrate")
    }) {
        if (CControlUI* lbl = m_PaintManager.FindControl(lblName)) {
            lbl->SetFixedWidth(DpiScale(UiTokens::PreviewMetaLabelW));
            lbl->SetAttribute(_T("textcolor"), metaLabel);
            lbl->SetAttribute(_T("font"), _T("0"));  // FontPreviewMeta
        }
    }
    for (LPCTSTR valName : {
        _T("preview_type"), _T("preview_size"), _T("preview_location"),
        _T("preview_mtime"), _T("preview_ctime"), _T("preview_dimensions"),
        _T("preview_duration"), _T("preview_framerate"), _T("preview_bitrate"),
        _T("preview_totalbitrate"), _T("preview_text")
    }) {
        if (CControlUI* v = m_PaintManager.FindControl(valName)) {
            v->SetAttribute(_T("font"), _T("0"));  // FontPreviewMeta
            v->SetAttribute(_T("align"), _T("left"));
            if (_tcscmp(valName, _T("preview_location")) == 0)
                static_cast<CLabelUI*>(v)->SetTextStyle(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
            if (_tcscmp(valName, _T("preview_text")) == 0)
                v->SetAttribute(_T("textcolor"), textMuted);
            else
                v->SetAttribute(_T("textcolor"), textPrimary);
        }
    }
    for (LPCTSTR rowName : {
        _T("preview_row_type"), _T("preview_row_size"), _T("preview_row_location"),
        _T("preview_row_mtime"), _T("preview_row_ctime"), _T("preview_row_dimensions"),
        _T("preview_row_duration"), _T("preview_row_framerate"), _T("preview_row_bitrate"),
        _T("preview_row_totalbitrate")
    }) {
        if (CControlUI* row = m_PaintManager.FindControl(rowName))
            row->SetFixedHeight(DpiScale(UiTokens::PreviewMetaRowH));
    }
    if (m_pPreviewImage) {
        m_pPreviewImage->SetFixedHeight(DpiScale(UiTokens::PreviewImageH));
        m_pPreviewImage->SetAttribute(_T("bkcolor"), content);
        m_pPreviewImage->SetAttribute(_T("bordercolor"), _T("#00000000"));
        m_pPreviewImage->SetAttribute(_T("bordersize"), _T("0"));
        CDuiString br;
        br.Format(_T("%d,%d"), DpiScale(UiTokens::PreviewImageRound), DpiScale(UiTokens::PreviewImageRound));
        m_pPreviewImage->SetAttribute(_T("borderround"), br.GetData());
    }
    if (CControlUI* pane = m_PaintManager.FindControl(_T("preview_pane"))) {
        CDuiString br;
        br.Format(_T("%d,%d"), DpiScale(UiTokens::PreviewRound), DpiScale(UiTokens::PreviewRound));
        pane->SetAttribute(_T("borderround"), br.GetData());
        pane->SetAttribute(_T("bordercolor"), _T("#00000000"));
        pane->SetAttribute(_T("bordersize"), _T("0"));
    }
}
