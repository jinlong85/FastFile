// FastFile - view modes, details list, icon/tile views, virtualization, sorting
// Implements CMainWnd members moved out of the original monolithic MainWnd.cpp.
// Behaviour is unchanged; declarations live in MainWnd.h.

#include "MainWndInternal.h"

namespace {

CLabelUI* MakeCell(LPCTSTR text, int fixedWidth, int padL)
{
    auto* p = new CLabelUI;
    p->SetText(text ? text : _T(""));
    p->SetAttribute(_T("textcolor"), UiTokens::ColorTextPrimary);
    p->SetAttribute(_T("align"), _T("left"));
    p->SetAttribute(_T("valign"), _T("vcenter"));
    p->SetAttribute(_T("endellipsis"), _T("true"));
    if (padL > 0) {
        CDuiString pad;
        pad.Format(_T("%d,0,0,0"), padL);
        p->SetAttribute(_T("padding"), pad);
    }
    if (fixedWidth > 0)
        p->SetFixedWidth(fixedWidth);
    return p;
}

// Tile button with hand-painted captions.
// DuiLib labels hold a single colour, but Explorer's tiles show "名称" plus a grey
// "类型  大小" line, and the drive tiles show a usage bar / caption — so the text is
// painted here instead of relying on the control's label.
class TileButtonUI final : public CButtonUI {
public:
    enum class Layout {
        Label,       // plain control label (list view, and icon views for files)
        TilesName,   // tiles: name only (folder)
        TilesFile,   // tiles: name + grey "type  size"
        TilesDrive,  // tiles (This PC): name / usage bar / grey "X 可用，共 Y"
        IconDrive    // xlarge/large/medium on This PC: centred name + grey caption
    };

    void SetLayoutMode(Layout l) { m_layout = l; }
    void SetUiDpi(UINT dpi) { m_dpi = dpi ? dpi : 96; }
    void SetDriveSpace(ULONGLONG freeBytes, ULONGLONG totalBytes) {
        m_freeBytes = freeBytes;
        m_totalBytes = totalBytes;
    }
    void SetTexts(const std::wstring& name, const std::wstring& meta) {
        m_name = name;
        m_meta = meta;
    }
    // Physical rect of the icon slot (filled by ApplyTileIconImage) — IconDrive puts the
    // caption right under the icon.
    void SetIconSlot(int x, int y, int px) { m_iconX = x; m_iconY = y; m_iconPx = px; }

    void PaintStatusImage(HDC hDC) override {
        CButtonUI::PaintStatusImage(hDC);   // background + foreimage (the icon)
        if (!hDC || m_layout == Layout::Label) return;
        const RECT rc = GetPos();
        if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) return;
        if (m_name.empty() && m_meta.empty()) return;

        HFONT font = m_pManager ? m_pManager->GetFont(m_iFont) : nullptr;
        HFONT smallFont = nullptr;
        LOGFONTW lf = {};
        if (font && ::GetObjectW(font, sizeof(lf), &lf) == sizeof(lf)) {
            lf.lfHeight = ::MulDiv(lf.lfHeight * 16, 18, 1);   // ~11pt vs ~12pt
            if (lf.lfHeight < -18) lf.lfHeight = -18;
            smallFont = ::CreateFontIndirectW(&lf);
        }

        const int oldBk = ::SetBkMode(hDC, TRANSPARENT);
        const COLORREF oldColor = ::GetTextColor(hDC);
        HGDIOBJ oldFont = font ? ::SelectObject(hDC, font) : nullptr;
        // DuiLib stores #AARRGGBB; GDI wants 0x00BBGGRR.
        const COLORREF nameColor = RGB(GetBValue(m_dwTextColor), GetGValue(m_dwTextColor), GetRValue(m_dwTextColor));
        const COLORREF metaColor = RGB(0x70, 0x70, 0x70);   // lighter than the name
        ::SetTextColor(hDC, nameColor);

        switch (m_layout) {
        case Layout::TilesDrive: PaintTilesDrive(hDC, rc, font, smallFont, metaColor); break;
        case Layout::IconDrive:  PaintIconDrive(hDC, rc, font, smallFont, metaColor); break;
        case Layout::TilesFile:  PaintTilesEntries(hDC, rc, font, smallFont, metaColor, true); break;
        case Layout::TilesName:  PaintTilesEntries(hDC, rc, font, smallFont, metaColor, false); break;
        default: break;
        }

        if (oldFont) ::SelectObject(hDC, oldFont);
        if (smallFont) ::DeleteObject(smallFont);
        ::SetTextColor(hDC, oldColor);
        ::SetBkMode(hDC, oldBk);
    }

private:
    int S(int design) const { return ::MulDiv(design, static_cast<int>(m_dpi), 96); }

    int MeasureRun(HDC dc, const std::wstring& text) const
    {
        if (text.empty()) return 0;
        SIZE sz = { 0, 0 };
        ::GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
        return sz.cx;
    }

    std::wstring Ellipsize(HDC dc, const std::wstring& text, int maxW) const
    {
        if (MeasureRun(dc, text) <= maxW) return text;
        const std::wstring dots = L"…";
        std::wstring out = text;
        while (!out.empty() && MeasureRun(dc, out + dots) > maxW)
            out.pop_back();
        return out + dots;
    }

    // Greedy wrap into at most maxLines lines; the overflow is folded into the last line
    // with an ellipsis, so a long name never leaves a lone "…" line behind.
    std::vector<std::wstring> WrapLines(HDC dc, const std::wstring& text, int maxW, size_t maxLines) const
    {
        std::vector<std::wstring> out;
        if (text.empty() || maxW <= 0 || maxLines == 0) return out;
        std::wstring cur;
        for (size_t i = 0; i < text.size(); ++i) {
            std::wstring next = cur;
            next.push_back(text[i]);
            if (MeasureRun(dc, next) <= maxW) {
                cur.swap(next);
                continue;
            }
            if (cur.empty()) {                 // single glyph wider than the box
                out.push_back(next);
                if (out.size() >= maxLines) {
                    out.back() = Ellipsize(dc, out.back() + text.substr(i + 1), maxW);
                    return out;
                }
                cur.clear();
                continue;
            }
            out.push_back(cur);
            cur.clear();
            if (out.size() >= maxLines) {
                out.back() = Ellipsize(dc, out.back() + text.substr(i), maxW);
                return out;
            }
            --i;                                // retry this glyph on the new line
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    void PaintTilesEntries(HDC dc, const RECT& rc, HFONT font, HFONT smallFont,
        COLORREF metaColor, bool withMeta)
    {
        const int textL = rc.left + S(56);
        const int textR = rc.right - S(8);
        if (textR <= textL) return;
        const int lineH = S(17);
        const int metaH = S(16);
        const std::vector<std::wstring> lines = WrapLines(dc, m_name, textR - textL, 2);
        const bool meta = withMeta && !m_meta.empty();
        int blockH = static_cast<int>(lines.size()) * lineH + (meta ? metaH : 0);
        int y = rc.top + ((rc.bottom - rc.top) - blockH) / 2;
        if (y < rc.top + S(2)) y = rc.top + S(2);
        for (const auto& ln : lines) {
            RECT r = { textL, y, textR, y + lineH };
            ::DrawTextW(dc, ln.c_str(), -1, &r,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            y += lineH;
        }
        if (meta) {
            if (smallFont) ::SelectObject(dc, smallFont);
            ::SetTextColor(dc, metaColor);
            RECT r = { textL, y, textR, y + metaH };
            ::DrawTextW(dc, m_meta.c_str(), -1, &r,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (font) ::SelectObject(dc, font);
        }
    }

    // This PC in the tile view: name / usage bar / "X 可用，共 Y".
    void PaintTilesDrive(HDC dc, const RECT& rc, HFONT font, HFONT smallFont, COLORREF metaColor)
    {
        const int textL = rc.left + S(56);
        const int textR = rc.right - S(10);
        if (textR <= textL) return;
        int fsTextHeight = S(15);
        {
            HGDIOBJ of = font ? ::SelectObject(dc, font) : nullptr;
            TEXTMETRICW tm = {};
            if (::GetTextMetricsW(dc, &tm)) {
                fsTextHeight = tm.tmHeight - tm.tmExternalLeading;
                if (fsTextHeight < tm.tmHeight * 3 / 4) fsTextHeight = tm.tmHeight;
            }
            if (of) ::SelectObject(dc, of);
        }
        const int nameH = S(20);
        int barH = fsTextHeight;
        if (barH < S(10)) barH = S(10);
        if (barH > (rc.bottom - rc.top) / 3) barH = (rc.bottom - rc.top) / 3;
        const int gap = S(5);
        const int capH = S(17);
        const bool hasBar = (m_totalBytes != 0);
        const bool hasCap = hasBar && !m_meta.empty();
        int blockH = nameH + (hasBar ? gap + barH : 0) + (hasCap ? gap + capH : 0);
        int top = rc.top + ((rc.bottom - rc.top) - blockH) / 2;
        if (top < rc.top + S(2)) top = rc.top + S(2);

        RECT rcName = { textL, top, textR, top + nameH };
        ::DrawTextW(dc, m_name.c_str(), -1, &rcName,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        int y = top + nameH + gap;
        if (hasBar) {
            RECT rcBar = { textL, y, textR, y + barH };
            DrawUsageBar(dc, rcBar, m_freeBytes, m_totalBytes);
            y = rcBar.bottom + gap;
        }
        if (hasCap) {
            if (smallFont) ::SelectObject(dc, smallFont);
            ::SetTextColor(dc, metaColor);
            RECT rcCap = { textL, y, textR, y + capH };
            ::DrawTextW(dc, m_meta.c_str(), -1, &rcCap,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (font) ::SelectObject(dc, font);
        }
    }

    // This PC in the icon views: no usage bar (too cramped next to the big drive icon),
    // just the drive name and a grey "X 可用，共 Y" under it.
    void PaintIconDrive(HDC dc, const RECT& rc, HFONT font, HFONT smallFont, COLORREF metaColor)
    {
        const int l = rc.left + S(4);
        const int r = rc.right - S(4);
        if (r <= l) return;
        const int lineH = S(18);
        const int metaH = S(17);
        const bool meta = !m_meta.empty();
        int y = rc.bottom - S(6) - (meta ? (lineH + metaH) : lineH);
        if (m_iconPx > 0 && y < m_iconY + m_iconPx + S(2))
            y = m_iconY + m_iconPx + S(2);
        if (y < rc.top + S(2)) y = rc.top + S(2);
        RECT rn = { l, y, r, y + lineH };
        ::DrawTextW(dc, m_name.c_str(), -1, &rn,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (meta) {
            if (smallFont) ::SelectObject(dc, smallFont);
            ::SetTextColor(dc, metaColor);
            RECT rm = { l, y + lineH, r, y + lineH + metaH };
            ::DrawTextW(dc, m_meta.c_str(), -1, &rm,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (font) ::SelectObject(dc, font);
        }
    }

    // Plain rectangular usage bar (user preference: 矩形, no rounded corners).
    static void DrawUsageBar(HDC hDC, const RECT& rc, ULONGLONG freeBytes, ULONGLONG totalBytes)
    {
        if (rc.right <= rc.left || rc.bottom <= rc.top) return;
        HBRUSH track = ::CreateSolidBrush(RGB(0xE6, 0xE6, 0xE6));
        ::FillRect(hDC, &rc, track);
        ::DeleteObject(track);

        if (totalBytes == 0) return;
        const ULONGLONG used = totalBytes > freeBytes ? totalBytes - freeBytes : 0;
        const long w = rc.right - rc.left;
        long fillW = static_cast<long>((used * static_cast<ULONGLONG>(w)) / totalBytes);
        if (used > 0 && fillW < 2) fillW = 2;
        if (fillW <= 0) return;
        RECT rf = rc;
        rf.right = rf.left + fillW;
        HBRUSH fill = ::CreateSolidBrush(RGB(0x00, 0x78, 0xD4));
        ::FillRect(hDC, &rf, fill);
        ::DeleteObject(fill);
    }

    ULONGLONG m_freeBytes = 0;
    ULONGLONG m_totalBytes = 0;
    std::wstring m_name;
    std::wstring m_meta;
    UINT m_dpi = 96;
    Layout m_layout = Layout::Label;
    int m_iconX = 0;
    int m_iconY = 0;
    int m_iconPx = 0;
};

} // namespace

// ---- A: visible vertical scrollbars --------------------------------------

void CMainWnd::StyleVerticalScrollBar(CContainerUI* host)
{
    if (!host) return;
    CScrollBarUI* sb = host->GetVerticalScrollBar();
    if (!sb) {
        host->EnableScrollBar(true, host->GetHorizontalScrollBar() != nullptr);
        sb = host->GetVerticalScrollBar();
    }
    if (!sb) return;

    const int w = (std::max)(DpiScale(UiTokens::ScrollBarW), 10);
    sb->SetFixedWidth(w);
    sb->SetShowButton1(false);
    sb->SetShowButton2(false);
    // Track-free (thumb only) like Windows 11: the file view's bar sits directly beside the
    // preview rail, and two light tracks of the same colour merged into a band twice as wide
    // as the thumb, which still read as a width mismatch at the divider.
    sb->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
    sb->SetThumbColor(0xFFC4C4C4); // ColorScrollThumb #FFC4C4C4
    sb->SetAttribute(_T("button1color"), UiTokens::ColorTransparent);
    sb->SetAttribute(_T("button2color"), UiTokens::ColorTransparent);
}

void CMainWnd::ApplyFileViewScrollBars()
{
    if (m_pFileList)
        StyleVerticalScrollBar(m_pFileList);
    if (m_pIconTiles)
        StyleVerticalScrollBar(m_pIconTiles);
    if (m_pIconScroll)
        m_pIconScroll->EnableScrollBar(false, false);
}

// Navigation and preview panes need a more forgiving scrollbar than the dense file view.
// The larger thickness also gives the draggable thumb a sensible minimum grab size.
void CMainWnd::StyleSidePaneScrollBars(CContainerUI* host)
{
    if (!host) return;
    const int extent = (std::max)(DpiScale(UiTokens::SidePaneScrollBarW), 10);
    const auto style = [extent](CScrollBarUI* sb, bool vertical) {
        if (!sb) return;
        if (vertical) sb->SetFixedWidth(extent);
        else sb->SetFixedHeight(extent);
        sb->SetShowButton1(false);
        sb->SetShowButton2(false);
        sb->SetAttribute(_T("bkcolor"), UiTokens::ColorScrollTrack);
        sb->SetThumbColor(0xFFB5B5B5);
        sb->SetAttribute(_T("button1color"), UiTokens::ColorScrollTrack);
        sb->SetAttribute(_T("button2color"), UiTokens::ColorScrollTrack);
    };
    style(host->GetVerticalScrollBar(), true);
    style(host->GetHorizontalScrollBar(), false);
}

// The preview rail is the scrollbar of the preview pane AND the grip that adjusts the
// pane width (see IsPreviewScrollBarHit / HitTestPaneDivider). It is a real
// CScrollBarUI placed along the divider, so it gets the same width, track and thumb as
// the navigation scrollbar inside left_panel instead of DuiLib's thin default.
//
// preview_body keeps its own DuiLib scrollbar: that one still owns the layout math
// (scroll range, wheel, keyboard, child shifting) but is collapsed to zero width and
// fully transparent, so the rail is the only bar the user ever sees. SyncPreviewRail()
// mirrors range/position onto the rail and drives the body when the rail is dragged.
void CMainWnd::StylePreviewRail()
{
    if (!m_pPreviewRail) return;
    if (m_pPreviewBody) m_pPreviewRail->SetOwner(m_pPreviewBody);
    const int extent = (std::max)(DpiScale(UiTokens::SidePaneScrollBarW), 10);
    m_pPreviewRail->SetHorizontal(false);
    m_pPreviewRail->SetFixedWidth(extent);
    m_pPreviewRail->SetShowButton1(false);
    m_pPreviewRail->SetShowButton2(false);
    m_pPreviewRail->SetAttribute(_T("bkcolor"), UiTokens::ColorScrollTrack);
    m_pPreviewRail->SetAttribute(_T("button1color"), UiTokens::ColorScrollTrack);
    m_pPreviewRail->SetAttribute(_T("button2color"), UiTokens::ColorScrollTrack);
    m_pPreviewRail->SetThumbColor(0xFFB5B5B5);
    m_pPreviewRail->SetVisible(true);

    if (m_pPreviewBody) {
        if (CScrollBarUI* bodyBar = m_pPreviewBody->GetVerticalScrollBar()) {
            bodyBar->SetFixedWidth(0);
            bodyBar->SetShowButton1(false);
            bodyBar->SetShowButton2(false);
            bodyBar->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
            bodyBar->SetAttribute(_T("button1color"), UiTokens::ColorTransparent);
            bodyBar->SetAttribute(_T("button2color"), UiTokens::ColorTransparent);
            bodyBar->SetThumbColor(0);
        }
    }
    SyncPreviewRail();
}

void CMainWnd::SyncPreviewRail()
{
    if (!m_pPreviewRail || !m_pPreviewBody) return;
    CScrollBarUI* bodyBar = m_pPreviewBody->GetVerticalScrollBar();
    const bool scrollable = bodyBar && bodyBar->IsVisible() && bodyBar->GetScrollRange() > 0;
    if (!scrollable) {
        // Nothing to scroll: keep the rail (it is also the width grip and the pane
        // boundary) but drop the thumb instead of letting DuiLib paint a full-height
        // block for range 0.
        if (m_pPreviewRail->GetScrollRange() != 0) m_pPreviewRail->SetScrollRange(0);
        if (m_pPreviewRail->GetScrollPos() != 0) m_pPreviewRail->SetScrollPos(0, false);
        if (m_pPreviewRail->GetThumbColor() != 0) m_pPreviewRail->SetThumbColor(0);
        return;
    }
    const int range = bodyBar->GetScrollRange();
    const int pos = bodyBar->GetScrollPos();
    if (m_pPreviewRail->GetScrollRange() != range) m_pPreviewRail->SetScrollRange(range);
    if (m_pPreviewRail->GetScrollPos() != pos) m_pPreviewRail->SetScrollPos(pos, false);
    if (m_pPreviewRail->GetThumbColor() != 0xFFB5B5B5) m_pPreviewRail->SetThumbColor(0xFFB5B5B5);
}

// Thumb rectangle of the rail, using DuiLib's own sizing formula (CScrollBarUI::SetPos,
// vertical, buttons hidden) so clicking the track pages the way the drawn thumb implies.
bool CMainWnd::PreviewRailThumbRect(RECT& out) const
{
    out = RECT{};
    if (!m_pPreviewRail || !m_pPreviewRail->IsVisible()) return false;
    const int range = m_pPreviewRail->GetScrollRange();
    if (range <= 0) return false;
    const RECT rc = m_pPreviewRail->GetPos();
    const int cy = rc.bottom - rc.top;
    const int width = rc.right - rc.left;
    if (cy <= 0 || width <= 0) return false;
    int cyThumb = cy * cy / (range + cy);
    if (cyThumb < width) cyThumb = width;
    out.left = rc.left;
    out.right = rc.right;
    out.top = m_pPreviewRail->GetScrollPos() * (cy - cyThumb) / range + rc.top;
    out.bottom = out.top + cyThumb;
    return true;
}

// Horizontal twin of StyleVerticalScrollBar (list view needs it when the columns
// grow past the right edge). The bar itself is created by EnableScrollBar().
void CMainWnd::StyleHorizontalScrollBar(CContainerUI* host)
{
    if (!host) return;
    CScrollBarUI* sb = host->GetHorizontalScrollBar();
    if (!sb) return;
    sb->SetFixedHeight((std::max)(DpiScale(UiTokens::ScrollBarW), 10));
    sb->SetShowButton1(false);
    sb->SetShowButton2(false);
    sb->SetAttribute(_T("bkcolor"), UiTokens::ColorTransparent);
    sb->SetThumbColor(0xFFC4C4C4);
    sb->SetAttribute(_T("button1color"), UiTokens::ColorTransparent);
    sb->SetAttribute(_T("button2color"), UiTokens::ColorTransparent);
}

// ---- View modes ----------------------------------------------------------

bool CMainWnd::IsTileViewMode() const
{
    return m_viewMode != ViewMode::Details;
}

void CMainWnd::GetViewMetrics(int& tileW, int& tileH, int& iconPx, int& childPad, int& maxLabel) const
{
    // Design metrics @ 96 DPI; scale for Per-Monitor awareness.
    switch (m_viewMode) {
    case ViewMode::ExtraLargeIcons:
        tileW = 200; tileH = 220; iconPx = 128; childPad = UiTokens::TileChildPadXLarge; maxLabel = 22; break;
    case ViewMode::LargeIcons:
        tileW = 128; tileH = 148; iconPx = 96; childPad = UiTokens::TileChildPadLarge; maxLabel = 18; break;
    case ViewMode::MediumIcons:
        tileW = 100; tileH = 108; iconPx = 48; childPad = UiTokens::TileChildPadMedium; maxLabel = 16; break;
    case ViewMode::List:
        // Width is measured from the longest name (see MeasureListColumnWidth); maxLabel
        // only guards against absurd names now that the column can grow.
        tileW = 180; tileH = UiTokens::DetailsRowH; iconPx = UiTokens::DetailsIconPx; childPad = UiTokens::TileChildPadList; maxLabel = 260; break;
    case ViewMode::Tiles:
        // Tall enough for a wrapped name + the size line, so nothing is clipped
        // (Explorer's tiles reserve two text lines under/next to the icon).
        tileW = 230; tileH = 72; iconPx = 44; childPad = UiTokens::TileChildPadMedium; maxLabel = 30; break;
    case ViewMode::Details:
    default:
        tileW = 100; tileH = 108; iconPx = 48; childPad = UiTokens::TileChildPadMedium; maxLabel = 16; break;
    }
    if (IsThisPcPath(m_currentPath) && m_viewMode == ViewMode::Tiles) {
        tileW = 280; tileH = 72; iconPx = 40; maxLabel = 48;
    }
    tileW = DpiScale(tileW);
    tileH = DpiScale(tileH);
    iconPx = DpiScale(iconPx);
    childPad = DpiScale(childPad);

    // Drive cards are the one tile type whose content benefits from consuming the
    // full central canvas.  A fixed 280px card left a large unused strip whenever
    // the preview pane was wide.  Pick 1–4 columns from the live tile viewport and
    // distribute the remaining width evenly, while retaining an Explorer-like gap.
    if (IsThisPcPath(m_currentPath) && m_viewMode == ViewMode::Tiles && m_pIconTiles) {
        const int viewportW = static_cast<int>(m_pIconTiles->GetWidth());
        const int gap = DpiScale(18);
        const int minCardW = DpiScale(250);
        if (viewportW >= minCardW) {
            int columns = viewportW / (minCardW + gap);
            if (columns < 1) columns = 1;
            if (columns > 4) columns = 4;
            tileW = (viewportW - (columns - 1) * gap) / columns;
            if (tileW < minCardW) tileW = minCardW;
            childPad = gap;
        }
    }
    // maxLabel stays character count (not pixels)
}

int CMainWnd::MeasureListColumnWidth(const std::vector<DirEntry>& all, int /*iconPx*/)
{
    // Explorer's list view sizes each column to its content, so the full name shows.
    // Measuring the longest name also keeps every column of the grid the same width.
    const int textL = DpiScale(24);      // icon slot (ApplyTileIconImage listMode: 4..4+iconPx)
    const int rightPad = DpiScale(12);
    int textW = 0;
    if (m_hWnd && !all.empty()) {
        HDC dc = ::GetDC(m_hWnd);
        if (dc) {
            HFONT font = m_PaintManager.GetFont(0);
            HGDIOBJ oldFont = font ? ::SelectObject(dc, font) : nullptr;
            const size_t kMaxScan = 4000;   // bound the cost on huge folders
            const size_t n = (std::min)(all.size(), kMaxScan);
            for (size_t i = 0; i < n; ++i) {
                SIZE sz = { 0, 0 };
                ::GetTextExtentPoint32W(dc, all[i].name.c_str(),
                    static_cast<int>(all[i].name.size()), &sz);
                if (sz.cx > textW) textW = sz.cx;
            }
            if (oldFont) ::SelectObject(dc, oldFont);
            ::ReleaseDC(m_hWnd, dc);
        }
    }
    if (textW <= 0) textW = DpiScale(140);
    int w = textL + textW + rightPad;
    if (w < DpiScale(180)) w = DpiScale(180);
    if (w > DpiScale(900)) w = DpiScale(900);
    return w;
}

void CMainWnd::ApplyTileLayoutMetrics()
{
    if (!m_pIconTiles) return;
    int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
    GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
    m_iconPx = iconPx;
    // List view flows top->bottom inside a column and then wraps right (Explorer order);
    // every other mode is the usual row-major tile grid.
    const bool listMode = (m_viewMode == ViewMode::List);
    m_pIconTiles->SetColumnFirst(listMode);
    SIZE sz = { tileW, tileH };
    m_pIconTiles->SetItemSize(sz);
    {
        CDuiString pad;
        pad.Format(_T("%d"), childPad);
        m_pIconTiles->SetAttribute(_T("childpadding"), pad.GetData());
        m_pIconTiles->SetAttribute(_T("childvpadding"), pad.GetData());
    }
    if (listMode) {
        // Tight rows: the column is filled top->bottom, so vertical gaps only waste space.
        m_pIconTiles->SetAttribute(_T("childvpadding"), _T("0"));
        m_pIconTiles->SetChildVPadding(0);
    }
    if (m_pIconScroll) {
        {
            const int p = (m_viewMode == ViewMode::List)
                ? DpiScale(UiTokens::TilePadCompact)
                : DpiScale(UiTokens::TilePadNormal);
            CDuiString pad;
            pad.Format(_T("%d,%d,%d,%d"), p, p, p, p);
            m_pIconScroll->SetAttribute(_T("padding"), pad);
        }
    }
    if (m_pIconTiles)
        m_pIconTiles->EnableScrollBar(!listMode, listMode);
    StyleHorizontalScrollBar(m_pIconTiles);
    ApplyFileViewScrollBars();
}

void CMainWnd::SetViewMode(ViewMode mode)
{
    if (m_viewMode == mode) {
        UpdateViewModeButtons();
        return;
    }
    m_viewMode = mode;
    m_iconAnchor = -1;
    m_lastIconClickTile = nullptr;
    m_lastIconClickTick = 0;
    if (!m_currentPath.empty())
        SaveFolderViewForPath(m_currentPath, mode);
    UpdateViewModeButtons();

    // 步骤2：切视图复用已枚举的 listing，避免重新扫盘
    if (m_hasListingCache
        && PathEquals(m_listingPath, m_currentPath)
        && m_listingFilter == m_searchFilter
        && m_listingRecursive == IsRecursiveSearch()) {
        RebuildCurrentViewFromCache();
        return;
    }
    RefreshListing();
}

void CMainWnd::UpdateViewModeButtons()
{
    struct Pair { LPCTSTR name; ViewMode mode; };
    const Pair buttons[] = {
        { _T("btn_view_xlarge"),  ViewMode::ExtraLargeIcons },
        { _T("btn_view_large"),   ViewMode::LargeIcons },
        { _T("btn_view_medium"),  ViewMode::MediumIcons },
        { _T("btn_view_list"),    ViewMode::List },
        { _T("btn_view_details"), ViewMode::Details },
        { _T("btn_view_tiles"),   ViewMode::Tiles },
    };
    auto styleActive = [](CButtonUI* b) {
        if (!b) return;
        b->SetAttribute(_T("bkcolor"), _T("#FFE8E8E8"));
        b->SetAttribute(_T("textcolor"), _T("#FF1A1A1A"));
        b->SetAttribute(_T("bordercolor"), _T("#FFC8C8C8"));
        b->Invalidate();
    };
    auto styleIdle = [](CButtonUI* b) {
        if (!b) return;
        b->SetAttribute(_T("bkcolor"), _T("#00FFFFFF"));
        b->SetAttribute(_T("textcolor"), _T("#FF3B3B3B"));
        b->SetAttribute(_T("bordercolor"), _T("#00FFFFFF"));
        b->Invalidate();
    };
    for (const auto& b : buttons) {
        auto* btn = static_cast<CButtonUI*>(m_PaintManager.FindControl(b.name));
        if (m_viewMode == b.mode) styleActive(btn);
        else styleIdle(btn);
    }

    const bool tiles = IsTileViewMode();
    if (m_pFileList) m_pFileList->SetVisible(!tiles);
    if (m_pIconScroll) m_pIconScroll->SetVisible(tiles);
}

void CMainWnd::ClearIconView()
{
    CancelThumbJobs();
    if (m_pIconTiles)
        m_pIconTiles->RemoveAll();
    m_iconAnchor = -1;
    m_lastIconClickTile = nullptr;
    m_lastIconClickTick = 0;
    m_virtPoolCount = 0;
    m_virtFirstIndex = 0;
    m_pVirtSpacerBefore = nullptr;
    m_pVirtSpacerAfter = nullptr;
}

void CMainWnd::ClearIconSelection()
{
    if (!m_pIconTiles) return;
    const int n = m_pIconTiles->GetCount();
    for (int i = 0; i < n; ++i) {
        CControlUI* p = m_pIconTiles->GetItemAt(i);
        if (p) SetIconSelected(p, false);
    }
}

void CMainWnd::SetIconSelected(CControlUI* tile, bool selected)
{
    if (!tile) return;
    UINT_PTR tag = tile->GetTag();
    if (selected) tag |= 0x100;
    else tag &= ~static_cast<UINT_PTR>(0x100);
    tile->SetTag(tag);
    ApplyIconSelectionVisual(tile);

    UpdateListingStatusTip();
}

void CMainWnd::SelectAllItems()
{
    int count = 0;

    if (IsTileViewMode() && m_pIconTiles) {
        count = m_pIconTiles->GetCount();
        for (int i = 0; i < count; ++i)
            SetIconSelected(m_pIconTiles->GetItemAt(i), true);
        if (count > 0)
            m_iconAnchor = 0;
    } else {
        // Details: select every *entry*, not the pooled rows that happen to be visible.
        count = static_cast<int>(m_detailsEntries.size());
        if (count > 0) {
            std::fill(m_detailsSel.begin(), m_detailsSel.end(), 1);
            m_detailsCur = 0;
            m_detailsAnchor = 0;
            ApplyDetailsSelectionVisuals();
        }
        UpdateListingStatusTip();
    }

    CDuiString tip;
    tip.Format(_T("已全选 %d 项"), count);
    UpdateStatus(tip.GetData());
    UpdatePreviewForSelection();
}

void CMainWnd::ApplyIconSelectionVisual(CControlUI* tile)
{
    if (!tile) return;
    const bool selected = (tile->GetTag() & 0x100) != 0;
    if (selected) {
        tile->SetAttribute(_T("bkcolor"), UiTokens::ColorListSelected);
        tile->SetAttribute(_T("bordercolor"), UiTokens::ColorBorder);
        tile->SetAttribute(_T("bordersize"), _T("1"));
        tile->SetAttribute(_T("hotbkcolor"), UiTokens::ColorListHover);
        tile->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorListSelected);
    } else {
        tile->SetAttribute(_T("bkcolor"), UiTokens::ColorContent);
        tile->SetAttribute(_T("bordercolor"), UiTokens::ColorTransparent);
        tile->SetAttribute(_T("bordersize"), _T("0"));
        tile->SetAttribute(_T("hotbkcolor"), UiTokens::ColorListHover);
        tile->SetAttribute(_T("pushedbkcolor"), UiTokens::ColorListSelected);
    }
    tile->Invalidate();
}

int CMainWnd::FindIconIndex(CControlUI* tile) const
{
    if (!m_pIconTiles || !tile) return -1;
    const int n = m_pIconTiles->GetCount();
    for (int i = 0; i < n; ++i) {
        if (m_pIconTiles->GetItemAt(i) == tile)
            return m_iconVirtMode ? (m_virtFirstIndex + i) : i;
    }
    return -1;
}

void CMainWnd::SelectIconRange(int from, int to)
{
    if (!m_pIconTiles) return;
    if (from > to) std::swap(from, to);
    const int nTiles = m_pIconTiles->GetCount();
    if (m_iconVirtMode) {
        const int total = (int)m_flatListing.size();
        from = (std::max)(0, from);
        to = (std::min)(total - 1, to);
        for (int i = 0; i < nTiles; ++i) {
            const int flat = m_virtFirstIndex + i;
            SetIconSelected(m_pIconTiles->GetItemAt(i), flat >= from && flat <= to);
        }
        return;
    }
    from = (std::max)(0, from);
    to = (std::min)(nTiles - 1, to);
    for (int i = 0; i < nTiles; ++i)
        SetIconSelected(m_pIconTiles->GetItemAt(i), i >= from && i <= to);
}

void CMainWnd::ActivateIconTile(CControlUI* tile)
{
    if (!tile) return;
    CDuiString ud = tile->GetUserData();
    if (ud.IsEmpty()) return;
    const bool isDir = (tile->GetTag() & 1) != 0;
    if (isDir)
        NavigateTo(ud.GetData(), true);
    else {
        ::ShellExecuteW(m_hWnd, L"open", ud.GetData(), nullptr, nullptr, SW_SHOWNORMAL);
        CDuiString tip;
        tip.Format(_T("已打开: %s"), ud.GetData());
        UpdateStatus(tip.GetData());
    }
}

void CMainWnd::OnIconTileClick(CControlUI* tile)
{
    if (!tile || !m_pIconTiles) return;

    const DWORD now = ::GetTickCount();
    const DWORD dbl = ::GetDoubleClickTime();
    if (tile == m_lastIconClickTile && (now - m_lastIconClickTick) <= dbl) {
        m_lastIconClickTick = 0;
        m_lastIconClickTile = nullptr;
        if ((tile->GetTag() & 0x100) == 0) {
            ClearIconSelection();
            SetIconSelected(tile, true);
        }
        ActivateIconTile(tile);
        return;
    }
    m_lastIconClickTick = now;
    m_lastIconClickTile = tile;

    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const int idx = FindIconIndex(tile);

    if (shift && m_iconAnchor >= 0 && idx >= 0) {
        SelectIconRange(m_iconAnchor, idx);
    } else if (ctrl) {
        const bool on = (tile->GetTag() & 0x100) == 0;
        SetIconSelected(tile, on);
        if (idx >= 0) m_iconAnchor = idx;
    } else {
        ClearIconSelection();
        SetIconSelected(tile, true);
        if (idx >= 0) m_iconAnchor = idx;
    }

    m_PaintManager.SetFocus(tile);

    std::vector<ClipboardItem> sel;
    CollectSelectedItems(sel);
    CDuiString tip;
    if (sel.size() <= 1)
        tip.Format(_T("已选 1 项（Ctrl/Shift 多选，双击打开）"));
    else
        tip.Format(_T("已选 %d 项"), static_cast<int>(sel.size()));
    UpdateStatus(tip.GetData());
}

// Read width/height straight out of a PNG header (cheap: no decode).
static bool PngSizeFromFile(const std::wstring& path, int& w, int& h)
{
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) return false;
    unsigned char hdr[24] = {};
    const size_t n = fread(hdr, 1, sizeof(hdr), fp);
    fclose(fp);
    static const unsigned char kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (n < sizeof(hdr) || ::memcmp(hdr, kSig, sizeof(kSig)) != 0) return false;
    auto be32 = [](const unsigned char* p) {
        return static_cast<int>((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
    };
    w = be32(hdr + 16);
    h = be32(hdr + 20);
    return w > 0 && h > 0;
}

void CMainWnd::ApplyTileIconImage(CControlUI* tile, const std::wstring& bmp,
    int tileW, int tileH, int iconPx, bool listMode, bool tilesMode)
{
    if (!tile || bmp.empty()) return;

    // Picture/video thumbs are cached tightly cropped, so fit them into the square icon
    // slot at their own aspect ratio — a portrait shot is narrow, a landscape one is wide
    // (Explorer/360 look) instead of every thumb being forced into one square size.
    int imgW = 0, imgH = 0;
    int dw = iconPx, dh = iconPx;
    if (PngSizeFromFile(bmp, imgW, imgH)) {
        const double s = (std::min)(static_cast<double>(iconPx) / imgW,
                                    static_cast<double>(iconPx) / imgH);
        dw = (std::max)(1, static_cast<int>(imgW * s + 0.5));
        dh = (std::max)(1, static_cast<int>(imgH * s + 0.5));
    }

    int slotX = 0, slotY = 0;
    if (listMode) {
        slotX = DpiScale(4);
        slotY = (tileH - iconPx) / 2;
    } else if (tilesMode) {
        slotX = DpiScale(8);
        slotY = (tileH - iconPx) / 2;
    } else {
        slotX = (tileW - iconPx) / 2;
        slotY = DpiScale(UiTokens::SpaceSm);
    }
    const int x0 = slotX + (iconPx - dw) / 2;
    const int y0 = slotY + (iconPx - dh) / 2;

    if (auto* ft = dynamic_cast<TileButtonUI*>(tile))
        ft->SetIconSlot(slotX, slotY, iconPx);   // IconDrive puts the caption under the icon

    CDuiString imgAttr;
    imgAttr.Format(_T("file='%s' dest='%d,%d,%d,%d'"),
        bmp.c_str(), x0, y0, x0 + dw, y0 + dh);
    tile->SetAttribute(_T("foreimage"), imgAttr.GetData());
    tile->SetAttribute(_T("hotforeimage"), imgAttr.GetData());
}

void CMainWnd::RebuildDetailsView(const std::vector<DirEntry>& dirs,
    const std::vector<DirEntry>& files, bool /*truncated*/)
{
    if (!m_pFileList) return;
    UpdateViewModeButtons();
    if (IsTileViewMode()) {
        // Icon/tile views still create one control per item, so they keep the bounded
        // progressive fill (see StartDetailsProgressiveFill).
        if (m_hWnd)
            ::KillTimer(m_hWnd, kTimerDetailsSync);
        StopDetailsFill();
        m_pFileList->SetVisible(false);
        StartDetailsProgressiveFill(dirs, files);
        return;
    }
    RebuildDetailsVirtual();
}

bool CMainWnd::TryReuseIconsView(const std::vector<DirEntry>& dirs,
    const std::vector<DirEntry>& files)
{
    if (!m_pIconTiles) return false;
    std::vector<DirEntry> all;
    BuildDisplayOrder(dirs, files, all);

    const int n = m_pIconTiles->GetCount();
    if (n != static_cast<int>(all.size()) || n <= 0)
        return false;
    // List view re-measures its column width from the names, so reuse is not safe there.
    if (m_viewMode == ViewMode::List)
        return false;

    // 路径不一致则不能复用控件
    for (int i = 0; i < n; ++i) {
        CControlUI* p = m_pIconTiles->GetItemAt(i);
        if (!p) return false;
        CDuiString ud = p->GetUserData();
        if (ud.IsEmpty() || ::_wcsicmp(ud.GetData(), all[i].fullPath.c_str()) != 0)
            return false;
    }

    CancelThumbJobs();
    ApplyTileLayoutMetrics();

    int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
    GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
    m_iconPx = iconPx;
    const bool listMode = (m_viewMode == ViewMode::List);
    const bool tilesMode = (m_viewMode == ViewMode::Tiles);
    const UINT gen = m_thumbGeneration.load();

    for (int i = 0; i < n; ++i) {
        auto* tile = static_cast<CButtonUI*>(m_pIconTiles->GetItemAt(i));
        if (!tile) continue;
        const DirEntry& e = all[i];

        tile->SetFixedWidth(tileW);
        tile->SetFixedHeight(tileH);

        if (listMode) {
            tile->SetAttribute(_T("align"), _T("left"));
            tile->SetAttribute(_T("valign"), _T("vcenter"));
            {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(24), DpiScale(0), DpiScale(4), DpiScale(0));
            tile->SetAttribute(_T("textpadding"), tp);
        }
        } else if (tilesMode) {
            tile->SetAttribute(_T("align"), _T("left"));
            tile->SetAttribute(_T("valign"), _T("vcenter"));
            {
                CDuiString tp;
                tp.Format(_T("%d,%d,%d,%d"), DpiScale(56), DpiScale(4), DpiScale(8), DpiScale(4));
                tile->SetAttribute(_T("textpadding"), tp);
            }
        } else {
            tile->SetAttribute(_T("align"), _T("center"));
            tile->SetAttribute(_T("valign"), _T("bottom"));
            {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(4), DpiScale(4), DpiScale(4), DpiScale(6));
            tile->SetAttribute(_T("textpadding"), tp);
        }
        }
        tile->SetAttribute(_T("endellipsis"), _T("true"));
        ApplyTileText(tile, e, maxLabel, listMode, tilesMode);

        if (i < kMaxIconThumbs) {
            std::wstring cached = PeekCachedIconBmp(e.fullPath, e.isDir, iconPx);
            if (!cached.empty()) {
                ApplyTileIconImage(tile, cached, tileW, tileH, iconPx, listMode, tilesMode);
            } else {
                // 尺寸变了：先清旧图，后台填新缩略图
                tile->SetAttribute(_T("foreimage"), _T(""));
                tile->SetAttribute(_T("hotforeimage"), _T(""));
                ThumbJob job;
                job.generation = gen;
                job.index = i;
                job.path = e.fullPath;
                job.isDir = e.isDir;
                job.iconPx = iconPx;
                job.tileW = tileW;
                job.tileH = tileH;
                job.listMode = listMode;
                job.tilesMode = tilesMode;
                EnqueueThumbJob(job);
            }
        }

        if (((i + 1) % kUiBatchSize) == 0)
            PumpUiMessages();
    }

    m_pIconTiles->NeedUpdate();
    if (m_pFileList) m_pFileList->SetVisible(false);
    if (m_pIconScroll) m_pIconScroll->SetVisible(true);
    UpdateViewModeButtons();
    return true;
}

void CMainWnd::RebuildIconsView(const std::vector<DirEntry>& dirs,
    const std::vector<DirEntry>& files, bool /*truncated*/)
{
    if (!m_pIconTiles) {
        RebuildDetailsView(dirs, files, false);
        return;
    }
    std::vector<DirEntry> all;
    BuildDisplayOrder(dirs, files, all);
    if ((int)all.size() >= kVirtThreshold) {
        if (m_hWnd) ::KillTimer(m_hWnd, kTimerVirtSync);
        RebuildIconsViewVirtual(all);
        UpdateViewModeButtons();
        return;
    }
    if (m_hWnd) ::KillTimer(m_hWnd, kTimerVirtSync);
    m_iconVirtMode = false;
    if (TryReuseIconsView(dirs, files))
        return;
    RebuildIconsViewFull(all);
    UpdateViewModeButtons();
}

void CMainWnd::FlattenListing(std::vector<DirEntry>& out) const
{
    BuildDisplayOrder(m_listingDirs, m_listingFiles, out);
}

bool CMainWnd::EntryComesBefore(const DirEntry& a, const DirEntry& b) const
{
    if (IsThisPcPath(m_currentPath)) {
        const wchar_t da = a.fullPath.empty() ? L'Z' : static_cast<wchar_t>(::towupper(a.fullPath[0]));
        const wchar_t db = b.fullPath.empty() ? L'Z' : static_cast<wchar_t>(::towupper(b.fullPath[0]));
        if (da == L'C') return db != L'C';
        if (db == L'C') return false;
        return da < db;
    }
    // Icon / list / tile views keep Explorer's folder grouping (folders first, then files,
    // each group ordered by the active column). Only the details view sorts strictly by
    // the clicked column, so "按修改日期降序" can bring the newest files to the top.
    if (m_viewMode != ViewMode::Details && a.isDir != b.isDir)
        return a.isDir && !b.isDir;
    int r = 0;
    switch (m_sortColumn) {
    case SortColumn::Size:
        if (a.size < b.size) r = -1;
        else if (a.size > b.size) r = 1;
        else r = ::_wcsicmp(a.name.c_str(), b.name.c_str());
        break;
    case SortColumn::Modified:
        if (a.mtime < b.mtime) r = -1;
        else if (a.mtime > b.mtime) r = 1;
        else r = ::_wcsicmp(a.name.c_str(), b.name.c_str());
        break;
    case SortColumn::Type: {
        const wchar_t* ea = PathFindExtensionW(a.name.c_str());
        const wchar_t* eb = PathFindExtensionW(b.name.c_str());
        r = ::_wcsicmp(ea ? ea : L"", eb ? eb : L"");
        if (r == 0) r = ::_wcsicmp(a.name.c_str(), b.name.c_str());
        break;
    }
    case SortColumn::Name:
    default:
        r = ::_wcsicmp(a.name.c_str(), b.name.c_str());
        break;
    }
    return m_sortAscending ? (r < 0) : (r > 0);
}

void CMainWnd::BuildDisplayOrder(const std::vector<DirEntry>& dirs,
    const std::vector<DirEntry>& files, std::vector<DirEntry>& out) const
{
    out.clear();
    out.reserve(dirs.size() + files.size());
    out.insert(out.end(), dirs.begin(), dirs.end());
    out.insert(out.end(), files.begin(), files.end());
    // Stable so equal keys (same name/size/time) keep folders before files.
    std::stable_sort(out.begin(), out.end(),
        [this](const DirEntry& a, const DirEntry& b) { return EntryComesBefore(a, b); });
}

void CMainWnd::SortListingCache()
{
    // The visible order comes from BuildDisplayOrder() (folders and files merged); this
    // keeps the cached vectors themselves sorted for anything that reads them directly.
    auto cmp = [this](const DirEntry& a, const DirEntry& b) { return EntryComesBefore(a, b); };
    std::sort(m_listingDirs.begin(), m_listingDirs.end(), cmp);
    std::sort(m_listingFiles.begin(), m_listingFiles.end(), cmp);
}

void CMainWnd::UpdateHeaderSortIndicators()
{
    if (!m_pFileList) return;
    CListHeaderUI* hdr = m_pFileList->GetHeader();
    if (!hdr) return;
    const wchar_t* arrowsAsc = L" ▲";
    const wchar_t* arrowsDesc = L" ▼";
    const wchar_t* bases[4] = { L"名称", L"修改日期", L"类型", L"大小" };
    for (int i = 0; i < hdr->GetCount() && i < 4; ++i) {
        CControlUI* c = hdr->GetItemAt(i);
        if (!c) continue;
        std::wstring t = bases[i];
        if (static_cast<int>(m_sortColumn) == i)
            t += m_sortAscending ? arrowsAsc : arrowsDesc;
        c->SetText(t.c_str());
    }
}

void CMainWnd::OnHeaderColumnClick(CControlUI* pHeaderItem)
{
    if (!pHeaderItem || !m_pFileList) return;
    CListHeaderUI* hdr = m_pFileList->GetHeader();
    if (!hdr) return;
    int idx = -1;
    for (int i = 0; i < hdr->GetCount(); ++i) {
        if (hdr->GetItemAt(i) == pHeaderItem) { idx = i; break; }
    }
    if (idx < 0 || idx > 3) return;
    auto col = static_cast<SortColumn>(idx);
    if (m_sortColumn == col) m_sortAscending = !m_sortAscending;
    else { m_sortColumn = col; m_sortAscending = true; }
    if (!m_hasListingCache) return;
    SortListingCache();
    UpdateHeaderSortIndicators();
    RebuildCurrentViewFromCache();
}

void CMainWnd::ApplyColumnWidths()
{
    if (!m_pFileList) return;
    CListHeaderUI* hdr = m_pFileList->GetHeader();
    if (!hdr || hdr->GetCount() < 4) return;
    // Order: 名称, 修改日期, 类型, 大小
    if (auto* c = hdr->GetItemAt(0)) c->SetFixedWidth(m_colWidthName);
    if (auto* c = hdr->GetItemAt(1)) c->SetFixedWidth(m_colWidthMTime);
    if (auto* c = hdr->GetItemAt(2)) c->SetFixedWidth(m_colWidthType);
    if (auto* c = hdr->GetItemAt(3)) c->SetFixedWidth(m_colWidthSize);
}

void CMainWnd::CaptureColumnWidths()
{
    if (!m_pFileList) return;
    CListHeaderUI* hdr = m_pFileList->GetHeader();
    if (!hdr || hdr->GetCount() < 4) return;
    if (auto* c = hdr->GetItemAt(0)) {
        int w = c->GetFixedWidth();
        if (w > 40) m_colWidthName = w;
    }
    if (auto* c = hdr->GetItemAt(1)) {
        int w = c->GetFixedWidth();
        if (w > 40) m_colWidthMTime = w;
    }
    if (auto* c = hdr->GetItemAt(2)) {
        int w = c->GetFixedWidth();
        if (w > 40) m_colWidthType = w;
    }
    if (auto* c = hdr->GetItemAt(3)) {
        int w = c->GetFixedWidth();
        if (w > 40) m_colWidthSize = w;
    }
}

CListContainerElementUI* CMainWnd::CreateDetailsRowShell()
{
    // ListHBoxElement: children map 1:1 to ListHeader columns (Name/MTime/Type/Size).
    // Created empty - BindDetailsRow() fills it - so the virtualised list can recycle rows
    // while scrolling instead of creating one control tree per file.
    auto* pItem = new CListHBoxElementUI;
    pItem->SetFixedHeight(DpiScale(UiTokens::DetailsRowH));
    pItem->SetBorderRound({ DpiScale(UiTokens::RadiusControl), DpiScale(UiTokens::RadiusControl) });

    const int cellPad = DpiScale(UiTokens::DetailsCellPadL);
    const int iconPx = DpiScale(UiTokens::DetailsIconPx); // SHIL_SMALL ~16
    const int iconPadL = DpiScale(UiTokens::DetailsIconPadL);
    const int iconGap = DpiScale(UiTokens::DetailsIconTextGap);
    const int rowH = DpiScale(UiTokens::DetailsRowH);

    // Name column: Shell smallFont icon + gap + name
    // IMPORTANT: use bkimage — CControlUI ignores foreimage (Button/Option only).
    auto* nameCol = new CHorizontalLayoutUI;
    nameCol->SetMouseEnabled(true);
    auto* iconCtrl = new CControlUI;
    iconCtrl->SetFixedWidth(iconPadL + iconPx + iconGap);
    iconCtrl->SetFixedHeight(rowH);
    iconCtrl->SetMouseEnabled(false);
    nameCol->Add(iconCtrl);
    nameCol->Add(MakeCell(_T(""), 0, 0));
    pItem->Add(nameCol);
    pItem->Add(MakeCell(_T(""), 0, cellPad));
    pItem->Add(MakeCell(_T(""), 0, cellPad));
    pItem->Add(MakeCell(_T(""), 0, cellPad));
    return pItem;
}

void CMainWnd::BindDetailsRow(CListContainerElementUI* row, int entryIdx)
{
    if (!row) return;
    if (entryIdx < 0 || entryIdx >= static_cast<int>(m_detailsEntries.size())) {
        // Park an unused pooled row instead of showing stale content.
        row->SetVisible(false);
        row->SetUserData(_T(""));
        row->SetTag(0);
        return;
    }
    const DirEntry& e = m_detailsEntries[entryIdx];
    const int iconPx = DpiScale(UiTokens::DetailsIconPx);
    const int iconPadL = DpiScale(UiTokens::DetailsIconPadL);
    const int rowH = DpiScale(UiTokens::DetailsRowH);

    row->SetVisible(true);
    row->SetUserData(e.fullPath.c_str());
    row->SetTag(e.isDir ? 1 : 0);

    if (auto* nameCol = static_cast<CHorizontalLayoutUI*>(row->GetItemAt(0))) {
        CControlUI* iconCtrl = nameCol->GetItemAt(0);
        CControlUI* nameLabel = nameCol->GetItemAt(1);
        std::wstring iconBmp = PeekCachedIconBmp(e.fullPath, e.isDir, iconPx);
        if (iconBmp.empty())
            iconBmp = GetShellFileIconBmp(e.fullPath, e.isDir, iconPx);
        if (iconCtrl) {
            CDuiString imgAttr = _T("");
            if (!iconBmp.empty()) {
                const int oy = (std::max)(0, (rowH - iconPx) / 2);
                imgAttr.Format(_T("file='%s' dest='%d,%d,%d,%d'"),
                    iconBmp.c_str(), iconPadL, oy, iconPadL + iconPx, oy + iconPx);
            }
            iconCtrl->SetAttribute(_T("bkimage"), imgAttr);
            iconCtrl->Invalidate();
        }
        if (nameLabel) {
            nameLabel->SetText(e.name.c_str());
            nameLabel->Invalidate();
        }
    }
    if (CControlUI* c = row->GetItemAt(1)) {
        c->SetText(FormatModifiedTime(e.mtime).c_str());
        c->Invalidate();
    }
    if (CControlUI* c = row->GetItemAt(2)) {
        // Real Shell type name ("光盘映像文件", "JPG 文件", ...) instead of a generic "文件".
        c->SetText(QueryShellTypeNameCached(e.fullPath, e.isDir).c_str());
        c->Invalidate();
    }
    if (CControlUI* c = row->GetItemAt(3)) {
        std::wstring sizeText = e.isDir ? L"" : FormatFileSize(e.size);
        if (IsThisPcPath(m_currentPath) && e.capacity > 0)
            sizeText = FormatFileSize(e.size) + L" 可用 / " + FormatFileSize(e.capacity);
        c->SetText(sizeText.c_str());
        c->Invalidate();
    }
}

void CMainWnd::StopDetailsFill()
{
    m_detailsFilling = false;
    m_detailsFillQueue.clear();
    m_detailsFillNext = 0;
}

void CMainWnd::ResetDetailsVirtualState()
{
    if (m_hWnd)
        ::KillTimer(m_hWnd, kTimerDetailsSync);
    m_detailsSpacerTop = nullptr;
    m_detailsSpacerBottom = nullptr;
    m_detailsPoolRows = 0;
    m_detailsFirst = 0;
    m_detailsCur = -1;
    m_detailsAnchor = -1;
    m_detailsEntries.clear();
    m_detailsSel.clear();
}

// ---- Virtual details view -------------------------------------------------------------
// The list keeps [top spacer][row pool][bottom spacer]. Spacers carry the height of the rows
// that are not materialised, so the scrollbar stays honest while only the visible window
// exists. Selection is stored per entry (m_detailsSel), never on the recycled rows.

void CMainWnd::RebuildDetailsVirtual()
{
    if (!m_pFileList) return;
    StopDetailsFill();
    ResetDetailsVirtualState();

    m_detailsEntries.clear();
    BuildDisplayOrder(m_listingDirs, m_listingFiles, m_detailsEntries);
    m_detailsSel.assign(m_detailsEntries.size(), 0);
    m_detailsFirst = 0;
    m_detailsCur = -1;
    m_detailsSpacerTop = nullptr;
    m_detailsSpacerBottom = nullptr;

    m_pFileList->RemoveAll();
    m_pFileList->SetVisible(true);
    if (m_pIconScroll)
        m_pIconScroll->SetVisible(false);
    ApplyColumnWidths();
    UpdateHeaderSortIndicators();

    const int total = static_cast<int>(m_detailsEntries.size());
    if (total <= 0) {
        m_detailsPoolRows = 0;
        m_pFileList->NeedUpdate();
        UpdateListingStatusTip();
        UpdateEmptyStateHint();
        return;
    }

    const int rowH = (std::max)(1, DpiScale(UiTokens::DetailsRowH));
    int viewH = 0;
    {
        RECT rc = m_pFileList->GetPos();
        viewH = (std::max)(0, static_cast<int>(rc.bottom - rc.top) - DpiScale(UiTokens::DetailsHeaderH));
    }
    int poolRows = viewH > 0 ? (viewH + rowH - 1) / rowH : 24;
    poolRows += 2 * kDetailsVirtOverscan;
    if (poolRows < 24) poolRows = 24;
    if (poolRows > total) poolRows = total;
    m_detailsPoolRows = poolRows;

    auto* top = new CListContainerElementUI;
    top->SetFixedHeight(0);
    top->SetMaxHeight(kMaxDetailsItems * DpiScale(UiTokens::DetailsRowH));   // see below
    top->SetMouseEnabled(false);
    top->SetUserData(_T(""));
    m_pFileList->Add(top);
    m_detailsSpacerTop = top;

    for (int i = 0; i < poolRows; ++i) {
        auto* row = CreateDetailsRowShell();
        BindDetailsRow(row, i);
        m_pFileList->Add(row);
    }

    auto* bottom = new CListContainerElementUI;
    bottom->SetFixedHeight(0);
    // DuiLib clamps a control to its max size (default 9999), which would silently cap the
    // virtual content height and therefore the scroll range.
    bottom->SetMaxHeight(kMaxDetailsItems * DpiScale(UiTokens::DetailsRowH));
    bottom->SetMouseEnabled(false);
    bottom->SetUserData(_T(""));
    m_pFileList->Add(bottom);
    m_detailsSpacerBottom = bottom;

    // Vertical for the rows, horizontal because a wide 名称 column can push the remaining
    // columns out of view (Explorer shows one in the same situation; otherwise they are
    // unreachable).
    m_pFileList->EnableScrollBar(true, true);
    m_pFileList->SetScrollPos({ 0, 0 });
    ApplyFileViewScrollBars();
    m_pFileList->NeedUpdate();

    UpdateDetailsWindow(true);
    if (m_hWnd)
        ::SetTimer(m_hWnd, kTimerDetailsSync, 60, nullptr);   // safety net for scroll sources
    UpdateListingStatusTip();
    UpdateEmptyStateHint();
}

void CMainWnd::UpdateDetailsWindow(bool force)
{
    if (!m_pFileList || m_detailsPoolRows <= 0 || m_detailsEntries.empty())
        return;
    // The cached spacer pointers are only valid while the list still owns them. Compare them
    // (never dereference) first: if another path rebuilt the list, the pool is gone and using
    // those pointers would call into freed controls.
    const int itemCount = m_pFileList->GetCount();
    if (itemCount != m_detailsPoolRows + 2
        || m_pFileList->GetItemAt(0) != m_detailsSpacerTop
        || m_pFileList->GetItemAt(itemCount - 1) != m_detailsSpacerBottom) {
        ResetDetailsVirtualState();
        return;
    }
    const int rowH = (std::max)(1, DpiScale(UiTokens::DetailsRowH));
    const int total = static_cast<int>(m_detailsEntries.size());

    int first = 0;
    if (m_detailsPoolRows < total) {
        const int scrollY = m_pFileList->GetScrollPos().cy;
        first = scrollY / rowH - kDetailsVirtOverscan;
        if (first < 0) first = 0;
        const int maxFirst = total - m_detailsPoolRows;
        if (first > maxFirst) first = maxFirst;
    }

    const bool moved = (first != m_detailsFirst);
    if (moved)
        m_detailsFirst = first;

    if (moved || force) {
        for (int i = 0; i < m_detailsPoolRows; ++i)
            BindDetailsRow(static_cast<CListContainerElementUI*>(m_pFileList->GetItemAt(1 + i)),
                m_detailsFirst + i);
    }

    if (m_detailsSpacerTop) {
        const int h = m_detailsFirst * rowH;
        if (m_detailsSpacerTop->GetFixedHeight() != h)
            m_detailsSpacerTop->SetFixedHeight(h);
        if (m_detailsSpacerTop->GetMaxHeight() < h)
            m_detailsSpacerTop->SetMaxHeight(h);
    }
    if (m_detailsSpacerBottom) {
        const int tail = total - (m_detailsFirst + m_detailsPoolRows);
        const int h = tail > 0 ? tail * rowH : 0;
        if (m_detailsSpacerBottom->GetFixedHeight() != h)
            m_detailsSpacerBottom->SetFixedHeight(h);
        if (m_detailsSpacerBottom->GetMaxHeight() < h)
            m_detailsSpacerBottom->SetMaxHeight(h);
    }

    if (moved || force) {
        ApplyDetailsSelectionVisuals();
        m_pFileList->NeedUpdate();
    }
}

int CMainWnd::DetailsEntryFromItem(CControlUI* item) const
{
    if (!item || !m_pFileList)
        return -1;
    if (item == m_detailsSpacerTop || item == m_detailsSpacerBottom)
        return -1;
    const int n = m_pFileList->GetCount();
    for (int i = 0; i < n; ++i) {
        if (m_pFileList->GetItemAt(i) != item)
            continue;
        const int row = i - 1;   // item 0 is the top spacer
        if (row < 0 || row >= m_detailsPoolRows)
            return -1;
        const int entry = m_detailsFirst + row;
        return (entry >= 0 && entry < static_cast<int>(m_detailsEntries.size())) ? entry : -1;
    }
    return -1;
}

void CMainWnd::ApplyDetailsSelectionVisuals()
{
    if (!m_pFileList)
        return;
    const int n = m_pFileList->GetCount();
    for (int i = 0; i < n; ++i) {
        CControlUI* item = m_pFileList->GetItemAt(i);
        if (!item) continue;
        auto* li = static_cast<IListItemUI*>(item->GetInterface(DUI_CTR_ILISTITEM));
        if (!li) continue;
        const int entry = DetailsEntryFromItem(item);
        const bool sel = (entry >= 0 && m_detailsSel[entry] != 0);
        if (li->IsSelected() != sel)
            li->Select(sel, false);
    }
}

void CMainWnd::DetailsEnsureEntryVisible(int entryIdx)
{
    if (!m_pFileList || entryIdx < 0 || m_detailsPoolRows <= 0)
        return;
    const int rowH = (std::max)(1, DpiScale(UiTokens::DetailsRowH));
    const int visible = (std::max)(1, m_detailsPoolRows - 2 * kDetailsVirtOverscan);
    if (entryIdx < m_detailsFirst || entryIdx >= m_detailsFirst + visible) {
        SIZE pos = m_pFileList->GetScrollPos();
        pos.cy = entryIdx * rowH;
        m_pFileList->SetScrollPos(pos);
        UpdateDetailsWindow(true);
    }
}

void CMainWnd::DetailsMoveCursor(int delta)
{
    const int total = static_cast<int>(m_detailsEntries.size());
    if (total <= 0 || delta == 0)
        return;

    int next;
    if (m_detailsCur < 0)
        next = (delta > 0) ? 0 : total - 1;
    else
        next = m_detailsCur + delta;
    if (next < 0) next = 0;
    if (next >= total) next = total - 1;

    const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (shift && m_detailsAnchor >= 0) {
        std::fill(m_detailsSel.begin(), m_detailsSel.end(), 0);
        int a = m_detailsAnchor, b = next;
        if (a > b) { const int t = a; a = b; b = t; }
        for (int i = a; i <= b; ++i) m_detailsSel[i] = 1;
    } else if (ctrl) {
        m_detailsSel[next] = 1;
        m_detailsAnchor = next;
    } else {
        std::fill(m_detailsSel.begin(), m_detailsSel.end(), 0);
        m_detailsSel[next] = 1;
        m_detailsAnchor = next;
    }
    m_detailsCur = next;

    DetailsEnsureEntryVisible(next);
    ApplyDetailsSelectionVisuals();
    UpdateListingStatusTip();
    UpdatePreviewForSelection();
}

void CMainWnd::StartDetailsProgressiveFill(const std::vector<DirEntry>& dirs,
    const std::vector<DirEntry>& files)
{
    // Icon / tile views only - the details view is virtualised (RebuildDetailsVirtual). These
    // views still create one control per item, so the queue is bounded by kMaxListItems and
    // filled in batches to keep the window responsive.
    StopDetailsFill();
    // The list is about to lose its items: drop the cached spacers/entries first, otherwise
    // the details timer or a scroll notify would call into freed controls.
    ResetDetailsVirtualState();
    if (!m_pFileList || !m_pIconTiles) return;
    m_pFileList->RemoveAll();
    m_pFileList->SetVisible(false);
    ApplyColumnWidths();
    UpdateHeaderSortIndicators();

    m_detailsFillQueue.clear();
    {
        std::vector<DirEntry> ordered;
        BuildDisplayOrder(dirs, files, ordered);
        if ((int)ordered.size() > kMaxListItems)
            ordered.resize(kMaxListItems);
        m_detailsFillQueue.swap(ordered);
    }
    m_detailsFillNext = 0;
    m_detailsFilling = true;

    if (m_pIconScroll)
        m_pIconScroll->SetVisible(true);
    m_pIconTiles->EnableScrollBar(true, false);
    ApplyFileViewScrollBars();

    if (m_detailsFillNext < (int)m_detailsFillQueue.size() && m_hWnd)
        ::PostMessageW(m_hWnd, kMsgDetailsFill, 0, 0);
    else {
        m_detailsFilling = false;
        UpdateListingStatusTip();
        UpdateEmptyStateHint();
    }
}

void CMainWnd::OnDetailsFillTick()
{
    if (!m_detailsFilling) return;
    const int n = (int)m_detailsFillQueue.size();
    if (n <= 0) { m_detailsFilling = false; return; }

    // Icon progressive path
    if (IsTileViewMode() && m_pIconTiles) {
        int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
        GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
        const bool listMode = (m_viewMode == ViewMode::List);
        const bool tilesMode = (m_viewMode == ViewMode::Tiles);
        const UINT gen = m_thumbGeneration.load();
        int added = 0;
        while (m_detailsFillNext < n && added < kDetailsFillBatch) {
            auto* tile = new TileButtonUI;
            BindIconTile(tile, m_detailsFillNext, m_detailsFillQueue[m_detailsFillNext],
                gen, tileW, tileH, iconPx, maxLabel, listMode, tilesMode);
            m_pIconTiles->Add(tile);
            ++m_detailsFillNext;
            ++added;
        }
        m_pIconTiles->EnableScrollBar(true, false);
        m_pIconTiles->NeedUpdate();
        if (m_detailsFillNext < n) {
            PumpUiMessages();
            ::PostMessageW(m_hWnd, kMsgDetailsFill, 1, 0);
        } else {
            m_detailsFilling = false;
            m_detailsFillQueue.clear();
        }
        return;
    }

    // The details view is virtualised now, so there is nothing left to fill here.
    m_detailsFilling = false;
    m_detailsFillQueue.clear();
    UpdateListingStatusTip();
    UpdateEmptyStateHint();
}

// Tile caption for one entry. Shared by BindIconTile (fresh tiles) and
// TryReuseIconsView (re-bound tiles) — keeping them in one place is what stopped the
// "switch from large icons to tiles → text painted twice / overlapping" bug.
void CMainWnd::ApplyTileText(CButtonUI* tile, const DirEntry& e, int maxLabel,
    bool listMode, bool tilesMode)
{
    if (!tile) return;
    const bool driveTile = IsThisPcPath(m_currentPath);
    std::wstring label = e.name;
    bool painted = false;

    if (auto* ft = dynamic_cast<TileButtonUI*>(tile)) {
        ft->SetUiDpi(m_dpi);
        ft->SetDriveSpace(e.size, e.capacity);
        if (listMode) {
            ft->SetLayoutMode(TileButtonUI::Layout::Label);
            ft->SetTexts(L"", L"");
        } else if (driveTile) {
            // Tiles keep the usage bar; the icon views only show name + free/total.
            ft->SetLayoutMode(tilesMode ? TileButtonUI::Layout::TilesDrive
                                        : TileButtonUI::Layout::IconDrive);
            ft->SetTexts(e.name, e.capacity > 0
                ? (FormatFileSize(e.size) + L" 可用，共 " + FormatFileSize(e.capacity))
                : std::wstring());
            painted = true;
        } else if (tilesMode) {
            if (e.isDir) {
                ft->SetLayoutMode(TileButtonUI::Layout::TilesName);
                ft->SetTexts(e.name, std::wstring());
            } else {
                // Explorer tiles: name, then the grey "type  size" line. The type comes
                // from the Shell ("光盘映像文件", "JPG 文件", ...), not a generic "文件".
                ft->SetLayoutMode(TileButtonUI::Layout::TilesFile);
                std::wstring meta = QueryShellTypeNameCached(e.fullPath, false);
                const std::wstring sizeText = FormatFileSize(e.size);
                if (!sizeText.empty())
                    meta += (meta.empty() ? L"" : L"  ") + sizeText;
                ft->SetTexts(e.name, meta);
            }
            painted = true;
        } else {
            ft->SetLayoutMode(TileButtonUI::Layout::Label);
            ft->SetTexts(L"", L"");
        }
    }

    if (painted) {
        label.clear();          // the caption is painted by the control
    } else if (label.size() > static_cast<size_t>(maxLabel)) {
        label = label.substr(0, maxLabel - 1) + L"…";
    }
    tile->SetText(label.c_str());
}

void CMainWnd::BindIconTile(CButtonUI* tile, int index, const DirEntry& e, UINT gen,
    int tileW, int tileH, int iconPx, int maxLabel, bool listMode, bool tilesMode)
{
    if (!tile) return;
    CDuiString name;
    name.Format(_T("icon_%d"), index);
    tile->SetName(name);
    tile->SetFixedWidth(tileW);
    tile->SetFixedHeight(tileH);
    tile->SetBorderRound({ DpiScale(UiTokens::RadiusControl), DpiScale(UiTokens::RadiusControl) });
    tile->SetUserData(e.fullPath.c_str());
    // keep selection bit if same path? reset selection for virt remap
    UINT_PTR tag = e.isDir ? 1 : 0;
    tile->SetTag(tag);
    ApplyIconSelectionVisual(tile);

    if (listMode) {
        tile->SetAttribute(_T("align"), _T("left"));
        tile->SetAttribute(_T("valign"), _T("vcenter"));
        {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(24), DpiScale(0), DpiScale(4), DpiScale(0));
            tile->SetAttribute(_T("textpadding"), tp);
        }
    } else if (tilesMode) {
        tile->SetAttribute(_T("align"), _T("left"));
        tile->SetAttribute(_T("valign"), _T("vcenter"));
        {
            CDuiString tp;
            tp.Format(_T("%d,%d,%d,%d"), DpiScale(56), DpiScale(4), DpiScale(8), DpiScale(4));
            tile->SetAttribute(_T("textpadding"), tp);
        }
    } else {
        tile->SetAttribute(_T("align"), _T("center"));
        tile->SetAttribute(_T("valign"), _T("bottom"));
        tile->SetAttribute(_T("multiline"), _T("false"));
        {
            CDuiString tp; tp.Format(_T("%d,%d,%d,%d"), DpiScale(4), DpiScale(4), DpiScale(4), DpiScale(6));
            tile->SetAttribute(_T("textpadding"), tp);
        }
    }
    tile->SetAttribute(_T("endellipsis"), _T("true"));
    ApplyTileText(tile, e, maxLabel, listMode, tilesMode);

    tile->SetAttribute(_T("foreimage"), _T(""));
    tile->SetAttribute(_T("hotforeimage"), _T(""));
    if (index < kMaxIconThumbs || m_iconVirtMode) {
        std::wstring cached = PeekCachedIconBmp(e.fullPath, e.isDir, iconPx);
        if (!cached.empty()) {
            ApplyTileIconImage(tile, cached, tileW, tileH, iconPx, listMode, tilesMode);
        } else {
            ThumbJob job;
            job.generation = gen;
            job.index = index;
            job.path = e.fullPath;
            job.isDir = e.isDir;
            job.iconPx = iconPx;
            job.tileW = tileW;
            job.tileH = tileH;
            job.listMode = listMode;
            job.tilesMode = tilesMode;
            EnqueueThumbJob(job);
        }
    }
}

void CMainWnd::RebuildIconsViewFull(const std::vector<DirEntry>& all)
{
    if (!m_pIconTiles) return;
    m_iconVirtMode = false;
    ClearIconView();
    ApplyTileLayoutMetrics();

    int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
    GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
    m_iconPx = iconPx;
    const bool listMode = (m_viewMode == ViewMode::List);
    const bool tilesMode = (m_viewMode == ViewMode::Tiles);
    if (listMode) {
        tileW = MeasureListColumnWidth(all, iconPx);
        SIZE lsz = { tileW, tileH };
        m_pIconTiles->SetItemSize(lsz);
    }
    const UINT gen = m_thumbGeneration.load();

    int added = 0;
    for (const auto& e : all) {
        auto* tile = new TileButtonUI;
        BindIconTile(tile, added, e, gen, tileW, tileH, iconPx, maxLabel, listMode, tilesMode);
        m_pIconTiles->Add(tile);
        ++added;
        if ((added % kUiBatchSize) == 0)
            PumpUiMessages();
    }
    m_pIconTiles->NeedUpdate();
    if (m_pFileList) m_pFileList->SetVisible(false);
    if (m_pIconScroll) m_pIconScroll->SetVisible(true);
    if (m_pIconTiles) {
        m_pIconTiles->EnableScrollBar(!listMode, listMode);
        StyleHorizontalScrollBar(m_pIconTiles);
        SIZE sp = { 0, 0 };
        m_pIconTiles->SetScrollPos(sp);
    }
    if (m_pIconScroll) m_pIconScroll->NeedUpdate();
}

void CMainWnd::EnsureIconTilePool(int /*poolCount*/, int /*tileW*/, int /*tileH*/)
{
    // retained for header ABI; progressive fill does not use a fixed pool
}

int CMainWnd::ComputeIconVirtPoolSize(int tileW, int tileH) const
{
    RECT rc = { 0, 0, 800, 600 };
    if (m_pIconScroll) rc = m_pIconScroll->GetPos();
    int vw = (std::max)(200, static_cast<int>(rc.right - rc.left));
    int vh = (std::max)(200, static_cast<int>(rc.bottom - rc.top));
    int cols = (std::max)(1, vw / (std::max)(1, tileW + 6));
    int rows = (std::max)(1, vh / (std::max)(1, tileH + 6));
    return cols * (rows + kVirtOverscanRows * 2);
}

void CMainWnd::SyncVisibleIconWindow(bool /*force*/)
{
    // Visible-window remapping is limited by DuiLib TileLayout scroll model.
    // Large folders use progressive creation instead (see RebuildIconsViewVirtual).
}

void CMainWnd::RebuildIconsViewVirtual(const std::vector<DirEntry>& all)
{
    // Strong batching for large folders: first screen immediately, rest async.
    if (!m_pIconTiles) return;
    m_iconVirtMode = false;
    m_flatListing = all;
    CancelThumbJobs();
    ClearIconView();
    ApplyTileLayoutMetrics();

    int tileW = 100, tileH = 108, iconPx = 48, childPad = 6, maxLabel = 16;
    GetViewMetrics(tileW, tileH, iconPx, childPad, maxLabel);
    m_iconPx = iconPx;
    const bool listMode = (m_viewMode == ViewMode::List);
    const bool tilesMode = (m_viewMode == ViewMode::Tiles);
    if (listMode) {
        tileW = MeasureListColumnWidth(all, iconPx);
        SIZE lsz = { tileW, tileH };
        m_pIconTiles->SetItemSize(lsz);
    }
    const UINT gen = m_thumbGeneration.load();

    // Reuse details fill queue machinery for icon progressive create
    StopDetailsFill();
    m_detailsFillQueue = all;
    m_detailsFillNext = 0;
    m_detailsFilling = true;

    const int first = (std::min)(ComputeIconVirtPoolSize(tileW, tileH), (int)all.size());
    for (int i = 0; i < first; ++i) {
        auto* tile = new TileButtonUI;
        BindIconTile(tile, i, all[i], gen, tileW, tileH, iconPx, maxLabel, listMode, tilesMode);
        m_pIconTiles->Add(tile);
    }
    m_detailsFillNext = first;
    m_pIconTiles->NeedUpdate();
    if (m_pFileList) m_pFileList->SetVisible(false);
    if (m_pIconScroll) m_pIconScroll->SetVisible(true);
    if (m_pIconTiles) {
        m_pIconTiles->EnableScrollBar(!listMode, listMode);
        StyleHorizontalScrollBar(m_pIconTiles);
        SIZE sp = { 0, 0 };
        m_pIconTiles->SetScrollPos(sp);
    }

    if (m_detailsFillNext < (int)m_detailsFillQueue.size() && m_hWnd)
        ::PostMessageW(m_hWnd, kMsgDetailsFill, 1, 0); // wParam=1 => icon mode
    else
        m_detailsFilling = false;
}
// Pixel width of a string in the UI font (0 = unknown).
int CMainWnd::MeasureTextWidthPx(const std::wstring& text)
{
    if (text.empty() || !m_hWnd) return 0;
    HDC dc = ::GetDC(m_hWnd);
    if (!dc) return 0;
    HFONT font = m_PaintManager.GetFont(0);
    HGDIOBJ oldFont = font ? ::SelectObject(dc, font) : nullptr;
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
    if (oldFont) ::SelectObject(dc, oldFont);
    ::ReleaseDC(m_hWnd, dc);
    return sz.cx;
}
