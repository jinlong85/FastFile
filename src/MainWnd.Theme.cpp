// FastFile - Win11 light chrome: UI tokens, window corners, item formatting
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

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

    setPad(_T("tab_bar"), px, py, px, 0);
    setPad(_T("toolbar"), px, py, px, py);
    setPad(_T("favorites_bar"), px, UiTokens::FavBarPadY, px, UiTokens::FavBarPadY);
    setPad(_T("address_bar"), px, UiTokens::AddressBarPadY, px, UiTokens::AddressBarPadY);
    setPad(_T("left_panel"), UiTokens::SpaceSm, UiTokens::SpaceSm, UiTokens::SpaceSm, UiTokens::SpaceSm);
    setPad(_T("icon_scroll"), px, px, px, px);
    // Preview pane: the wrapper stays unpadded - the merged scroll rail sits flush along
    // the divider and preview_body carries the content inset (its left inset equals the
    // rail width, so the preview text keeps its original distance from the divider).
    setPad(_T("preview_pane"), 0, 0, 0, 0);
    setPad(_T("preview_body"), UiTokens::SidePaneScrollBarW, UiTokens::PreviewPad,
        UiTokens::PreviewPad, UiTokens::PreviewPad);

    // Chrome bands. Phase 6 (Explorer-style reference): the tab strip is a slightly darker
    // band carrying tab "cards", the address row keeps the chrome surface, and the command
    // bar sits on the white content surface with a divider above and below it.
    const LPCWSTR surf = UiTokens::ColorSurface;
    const LPCWSTR border = UiTokens::ColorChromeDivider;
    for (LPCTSTR band : {
        _T("tab_bar"), _T("favorites_bar"), _T("toolbar"),
        _T("address_bar"),
        _T("status_bar"), _T("left_panel")
    }) {
        setBk(band, surf);
        setBorder(band, border, _T("0,0,0,1"));
    }
    setBk(_T("tab_bar"), UiTokens::ColorTabStripBg);
    setBorder(_T("tab_bar"), _T("#FFCECECE"), _T("0,0,0,1"));
    setBk(_T("toolbar"), UiTokens::ColorContent);
    // Path / search fields: white rounded boxes on the chrome surface.
    for (LPCTSTR field : { _T("path_host"), _T("search_box") }) {
        if (CControlUI* c = m_PaintManager.FindControl(field)) {
            c->SetAttribute(_T("bkcolor"), UiTokens::ColorFieldBg);
            c->SetAttribute(_T("bordercolor"), UiTokens::ColorFieldBorder);
            c->SetAttribute(_T("bordersize"), _T("1"));
            CDuiString round;
            round.Format(_T("%d,%d"), DpiScale(UiTokens::FieldRound), DpiScale(UiTokens::FieldRound));
            c->SetAttribute(_T("borderround"), round.GetData());
        }
    }
    // status_bar top border only
    setBorder(_T("status_bar"), UiTokens::ColorBorderStrong, _T("0,1,0,0"));
    setBorder(_T("left_panel"), border, _T("0,0,1,0"));
    setBorder(_T("preview_pane"), _T("#00000000"), _T("0"));
    setBk(_T("preview_pane"), UiTokens::ColorContent);
    // breadcrumb_bar removed (merged into address_bar)

    // Body / list stay content white
    setBk(_T("body_host"), UiTokens::ColorContent);
    setBk(_T("list_host"), UiTokens::ColorContent);
    if (m_pFileList)
        m_pFileList->SetAttribute(_T("bkcolor"), UiTokens::ColorContent);

    // Toolbar group gaps use SpaceSm
    for (LPCTSTR gap : {
        _T("gap_clip"), _T("gap_clip2"), _T("gap_org"), _T("gap_org2"),
        _T("gap_more"), _T("gap_more2")
    }) {
        if (CControlUI* c = m_PaintManager.FindControl(gap))
            c->SetFixedWidth(DpiScale(UiTokens::GapGroup));
    }
    for (LPCTSTR sep : { _T("sep_new"), _T("sep_organize"), _T("sep_more") }) {
        if (CControlUI* c = m_PaintManager.FindControl(sep)) {
            c->SetFixedWidth(DpiScale(1));
            c->SetFixedHeight(DpiScale(UiTokens::SepH));
            c->SetAttribute(_T("bkcolor"), UiTokens::ColorSeparator);
        }
    }

    // Phase 2: details list hover/select + header chrome-aligned surface
    if (m_pFileList) {
        m_pFileList->SetAttribute(_T("itemhotbkcolor"), UiTokens::ColorListHover);
        m_pFileList->SetAttribute(_T("itemselectedbkcolor"), UiTokens::ColorListSelected);
        m_pFileList->SetAttribute(_T("itemtextcolor"), UiTokens::ColorTextPrimary);
        m_pFileList->SetAttribute(_T("itemselectedtextcolor"), UiTokens::ColorTextPrimary);
        m_pFileList->SetAttribute(_T("headerbkcolor"), UiTokens::ColorListHeaderBg);
        if (CListHeaderUI* hdr = m_pFileList->GetHeader()) {
            hdr->SetAttribute(_T("bkcolor"), UiTokens::ColorListHeaderBg);
            hdr->SetAttribute(_T("bordercolor"), UiTokens::ColorBorder);
            hdr->SetAttribute(_T("bordersize"), _T("0,0,0,1"));
            hdr->SetFixedHeight(DpiScale(UiTokens::DetailsHeaderH));
            const int n = hdr->GetCount();
            for (int i = 0; i < n; ++i) {
                if (CControlUI* hi = hdr->GetItemAt(i)) {
                    hi->SetAttribute(_T("textcolor"), UiTokens::ColorTextSecondary);
                    hi->SetAttribute(_T("sepcolor"), UiTokens::ColorBorder);
                }
            }
        }
    }

    // Phase 2: left nav surface + section headers + tree interaction colors
    setBk(_T("left_quick"), surf);
    setBk(_T("left_thispc"), surf);
    // Visible divider between the two nav sections; it doubles as the splitter's grab line.
    setBk(_T("left_nav_divider"), UiTokens::ColorSeparator);
    if (m_pDirTree) {
        m_pDirTree->SetAttribute(_T("bkcolor"), surf);
        m_pDirTree->SetAttribute(_T("itemhotbkcolor"), UiTokens::ColorListHover);
        m_pDirTree->SetAttribute(_T("itemselectedbkcolor"), UiTokens::ColorListSelected);
        m_pDirTree->SetAttribute(_T("itemtextcolor"), UiTokens::ColorTextPrimary);
        m_pDirTree->SetAttribute(_T("itemhottextcolor"), UiTokens::ColorTextPrimary);
        m_pDirTree->SetAttribute(_T("selitemtextcolor"), UiTokens::ColorTextPrimary);
        m_pDirTree->SetAttribute(_T("selitemhottextcolor"), UiTokens::ColorTextPrimary);
    }
    for (LPCTSTR hdrName : { _T("nav_hdr_quick"), _T("nav_hdr_thispc") }) {
        if (CControlUI* h = m_PaintManager.FindControl(hdrName)) {
            h->SetFixedHeight(DpiScale(UiTokens::NavSectionHeaderH));
            h->SetAttribute(_T("textcolor"), UiTokens::ColorNavSection);
            // Same size as the rows below, only lighter. Forcing font 3 (10px) made the
            // headers smaller AND fainter than their own items, i.e. an inverted hierarchy,
            // and it silently undid the matching change in main.xml.
            h->SetAttribute(_T("font"), _T("0"));
            CDuiString pad;
            pad.Format(_T("%d,%d,0,0"), DpiScale(UiTokens::NavHeaderPadL), DpiScale(UiTokens::SpaceXs));
            h->SetAttribute(_T("padding"), pad);
        }
    }
    // (the four built-in quick-access rows are created at runtime by RebuildLeftQuickRows,
    // which applies the same row metrics through ApplyQuickAccessRow)

    // Phase 3: preview pane density + Surface header chrome; status bar density
    // (the rail owns the pane's left edge; preview_body holds the content inset)
    setPad(_T("preview_pane"), 0, 0, 0, 0);
    setPad(_T("preview_body"), UiTokens::SidePaneScrollBarW, UiTokens::PreviewPad,
        UiTokens::PreviewPad, UiTokens::PreviewPad);
    setPad(_T("status_bar"), UiTokens::StatusPadX, UiTokens::StatusPadY,
        UiTokens::StatusPadX, UiTokens::StatusPadY);
    setBorder(_T("status_bar"), UiTokens::ColorBorderStrong, _T("0,1,0,0"));
    if (CControlUI* st = m_PaintManager.FindControl(_T("status_text"))) {
        st->SetAttribute(_T("textcolor"), UiTokens::ColorTextSecondary);
        st->SetAttribute(_T("font"), _T("2"));  // FontSmall
        st->SetAttribute(_T("padding"), _T("0,0,0,0"));
    }
    if (m_pPreviewTitle) {
        m_pPreviewTitle->SetFixedHeight(DpiScale(UiTokens::PreviewTitleH));
        m_pPreviewTitle->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
        m_pPreviewTitle->SetAttribute(_T("font"), _T("5"));  // FontPreviewTitle
        m_pPreviewTitle->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
    }
    if (CControlUI* fl = m_PaintManager.FindControl(_T("fav_bar_label"))) {
        fl->SetAttribute(_T("font"), _T("0"));
        fl->SetAttribute(_T("textcolor"), UiTokens::ColorTextSecondary);
        fl->SetAttribute(_T("valign"), _T("vcenter"));
        // CJK glyphs fall back to a different font whose visual centre sits lower than the
        // Latin glyphs of the chip labels, so the label looked misaligned. CLabelUI positions
        // text from "textpadding", not "padding". Two extra design px of height plus an equal
        // bottom text pad lift the text by half the pad and keep the box tall enough to avoid
        // clipping the taller CJK fallback glyphs.
        CDuiString tp;
        tp.Format(_T("0,0,0,%d"), DpiScale(UiTokens::FavLabelBaselineLift * 2));
        fl->SetAttribute(_T("textpadding"), tp);
        fl->SetFixedHeight(DpiScale(UiTokens::FavChipH + 2));
    }
    if (CControlUI* fh = m_PaintManager.FindControl(_T("fav_bar_hint"))) {
        fh->SetAttribute(_T("font"), _T("0"));
        fh->SetAttribute(_T("valign"), _T("vcenter"));
    }
    // Gap spacers DPI-scaled (also in ApplyDpiScaledChrome; re-apply after chrome token pass)
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
            lbl->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
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
            if (_tcscmp(valName, _T("preview_text")) == 0)
                v->SetAttribute(_T("textcolor"), UiTokens::ColorTextMuted);
            else
                v->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
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
        m_pPreviewImage->SetAttribute(_T("bkcolor"), UiTokens::ColorContent);
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
