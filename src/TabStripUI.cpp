// FastFile - Explorer-style tab strip implementation (see TabStripUI.h for the contract).

#include "MainWndInternal.h"
#include "TabStripUI.h"

#include <gdiplus.h>

namespace DuiLib {

namespace {

// Explorer-ish palette. Light mode keeps the chrome white-ish; dark mode follows the
// standard Win11 greys so the strip still reads on a dark Mica title bar.
struct TabPalette {
    Gdiplus::ARGB selected;      // fill of the active tab (== the row below)
    Gdiplus::ARGB selectedEdge;  // 1px outline, three sides only
    Gdiplus::ARGB hover;         // 8-12% tint for the hovered idle tab
    Gdiplus::ARGB text;          // idle label
    Gdiplus::ARGB textActive;
    Gdiplus::ARGB closeGlyph;
    Gdiplus::ARGB closeGlyphHot;
    Gdiplus::ARGB closeHotFill;
};

const TabPalette kLight = {
    0xFFFFFFFF, 0xFFE0E0E0, 0x1E000000,
    0xFF5C5C5C, 0xFF1A1A1A,
    0xFF9A9A9A, 0xFFC42B1C, 0x22E81123,
};
const TabPalette kDark = {
    0xFF2B2B2B, 0xFF3A3A3A, 0x24FFFFFF,
    0xFFB3B3B3, 0xFFFFFFFF,
    0xFF9A9A9A, 0xFFFF99A4, 0x33E81123,
};

int Scaled(int designPx, int dpi) { return ::MulDiv(designPx, dpi, 96); }

// Builds the tab shape: rounded top corners, straight (and open) bottom edge.
void BuildTopRoundedPath(Gdiplus::GraphicsPath& path, const RECT& rc, int radius)
{
    const float l = (float)rc.left + 0.5f, t = (float)rc.top + 0.5f;
    const float r = (float)rc.right - 0.5f, b = (float)rc.bottom - 0.5f;
    const float d = (float)(radius * 2);
    path.Reset();
    path.AddArc(l, t, d, d, 180.0f, 90.0f);
    path.AddArc(r - d, t, d, d, 270.0f, 90.0f);
    path.AddLine(r, b, l, b);
    path.CloseFigure();
}

// Outlines the card on three sides only: rounded top corners + both vertical sides. The bottom
// edge is deliberately left open so the active tab flows into the white row underneath it
// (Win11 Explorer behaviour) instead of looking like a closed box with a bottom border.
void StrokeTopAndSides(Gdiplus::Graphics& g, const RECT& rc, int radius, Gdiplus::ARGB color)
{
    Gdiplus::Pen pen(Gdiplus::Color(color), 1.0f);
    const float l = (float)rc.left + 0.5f, t = (float)rc.top + 0.5f;
    const float r = (float)rc.right - 0.5f, b = (float)rc.bottom - 0.5f;
    const float d = (float)(radius * 2);
    if (r - d <= l + d) return;                       // too narrow for two corners
    g.DrawArc(&pen, l, t, d, d, 180.0f, 90.0f);       // top-left corner
    g.DrawLine(&pen, l + d / 2.0f, t, r - d / 2.0f, t);
    g.DrawArc(&pen, r - d, t, d, d, 270.0f, 90.0f);   // top-right corner
    g.DrawLine(&pen, l, t + d / 2.0f, l, b);          // left side
    g.DrawLine(&pen, r, t + d / 2.0f, r, b);          // right side
}

} // namespace

CTabStripUI::CTabStripUI()
{
    m_bMouseChildEnabled = false;
    SetMinHeight(Scaled(26, 96));
    m_dpi = 96;
}

CTabStripUI::~CTabStripUI()
{
    Clear();
    if (m_font) { delete m_font; m_font = nullptr; }
    if (m_fontFamily) { delete m_fontFamily; m_fontFamily = nullptr; }
}

LPCTSTR CTabStripUI::GetClass() const { return _T("TabStrip"); }

LPVOID CTabStripUI::GetInterface(LPCTSTR pstrName)
{
    if (_tcscmp(pstrName, _T("TabStrip")) == 0) return static_cast<CTabStripUI*>(this);
    return CContainerUI::GetInterface(pstrName);
}

void CTabStripUI::SetMetrics(int dpi)
{
    m_dpi = dpi > 0 ? dpi : 96;
    // The strip owns the whole title row: the active tab's card has to reach the row's bottom
    // edge so it merges with the surface below (a shorter strip floated the card above it).
    SetMinHeight(Scaled(UiTokens::TabBarH, m_dpi));
    SetFixedHeight(Scaled(UiTokens::TabBarH, m_dpi));
    if (m_font) { delete m_font; m_font = nullptr; }
    if (m_fontFamily) { delete m_fontFamily; m_fontFamily = nullptr; }
    RecalcRects();
    Invalidate();
}

void CTabStripUI::SetDarkMode(bool dark)
{
    if (m_dark == dark) return;
    m_dark = dark;
    Invalidate();
}

void CTabStripUI::StartAnimTimer()
{
    if (!m_pManager || m_animating) return;
    m_animating = true;
    m_pManager->SetTimer(this, 1, 16);
}

void CTabStripUI::AnimateAppear(int index)
{
    if (index < 0 || index >= (int)m_tabs.size()) return;
    m_tabs[index].born = ::GetTickCount();
    StartAnimTimer();
}

int CTabStripUI::Add(const std::wstring& path, const std::wstring& title,
    const std::wstring& iconBmp, int iconPx, bool activate)
{
    Tab tab;
    tab.path = path;
    tab.title = title;
    tab.iconPx = iconPx;
    tab.born = ::GetTickCount();
    m_tabs.push_back(std::move(tab));
    const int index = (int)m_tabs.size() - 1;
    if (!iconBmp.empty()) SetTabIcon(index, iconBmp, iconPx);
    if (activate) m_active = index;
    RecalcRects();
    if (activate) EnsureTabVisible(index);
    StartAnimTimer();
    Invalidate();
    return index;
}

void CTabStripUI::Insert(int index, const std::wstring& path, const std::wstring& title,
    const std::wstring& iconBmp, int iconPx)
{
    Tab tab;
    tab.path = path;
    tab.title = title;
    tab.iconPx = iconPx;
    tab.born = ::GetTickCount();
    if (index < 0) index = 0;
    if (index > (int)m_tabs.size()) index = (int)m_tabs.size();
    m_tabs.insert(m_tabs.begin() + index, std::move(tab));
    SetTabIcon(index, iconBmp, iconPx);
    if (m_active >= index) ++m_active;
    RecalcRects();
    StartAnimTimer();
    Invalidate();
}

bool CTabStripUI::RemoveAt(int index)
{
    if (index < 0 || index >= (int)m_tabs.size()) return false;
    if (m_tabs[index].icon) { delete m_tabs[index].icon; m_tabs[index].icon = nullptr; }
    m_tabs.erase(m_tabs.begin() + index);
    if (m_active == index) m_active = (std::min)(index, (int)m_tabs.size() - 1);
    else if (m_active > index) --m_active;
    m_hot = m_hotClose = m_pressed = -1;
    RecalcRects();
    Invalidate();
    return true;
}

void CTabStripUI::Clear()
{
    for (auto& t : m_tabs)
        if (t.icon) { delete t.icon; t.icon = nullptr; }
    m_tabs.clear();
    m_active = m_hot = m_hotClose = m_hotPlus = m_pressed = m_dragFrom = -1;
    m_dragging = false;
    if (m_pManager && m_animating) { m_pManager->KillTimer(this, 1); m_animating = false; }
}

bool CTabStripUI::Select(int index)
{
    if (index < 0 || index >= (int)m_tabs.size() || index == m_active) return false;
    m_active = index;
    RecalcRects();
    EnsureTabVisible(index);
    Invalidate();
    return true;
}

void CTabStripUI::SetActiveTab(int index)
{
    if (index < -1 || index >= (int)m_tabs.size()) return;
    m_active = index;
    RecalcRects();
    if (index >= 0) EnsureTabVisible(index);
    Invalidate();
}

int CTabStripUI::FindByPath(const std::wstring& path) const
{
    for (int i = 0; i < (int)m_tabs.size(); ++i)
        if (_wcsicmp(m_tabs[i].path.c_str(), path.c_str()) == 0) return i;
    return -1;
}

void CTabStripUI::SetTabTitle(int index, const std::wstring& title)
{
    if (index < 0 || index >= (int)m_tabs.size()) return;
    m_tabs[index].title = title;
    Invalidate();
}

void CTabStripUI::SetTabIcon(int index, const std::wstring& iconBmp, int iconPx)
{
    if (index < 0 || index >= (int)m_tabs.size()) return;
    Tab& t = m_tabs[index];
    if (t.icon) { delete t.icon; t.icon = nullptr; }
    t.iconBmp = iconBmp;
    t.iconPx = iconPx;
    if (!iconBmp.empty())
        t.icon = Gdiplus::Bitmap::FromFile(iconBmp.c_str());
    Invalidate();
}

bool CTabStripUI::Reorder(int from, int to)
{
    if (from < 0 || to < 0 || from == to) return false;
    if (from >= (int)m_tabs.size() || to >= (int)m_tabs.size()) return false;
    Tab moved = m_tabs[from];
    m_tabs.erase(m_tabs.begin() + from);
    m_tabs.insert(m_tabs.begin() + to, std::move(moved));
    if (m_active == from) m_active = to;
    else if (from < m_active && to >= m_active) --m_active;
    else if (from > m_active && to <= m_active) ++m_active;
    m_hot = to;
    RecalcRects();
    Invalidate();
    return true;
}

SIZE CTabStripUI::EstimateSize(SIZE szAvailable)
{
    // Ask for the width the tabs actually need; the host clips us to the row and the strip
    // scrolls horizontally when there is not enough room.
    const int n = (int)m_tabs.size();
    if (n == 0) return SIZE{ Scaled(60, m_dpi), szAvailable.cy };
    int total = Scaled(32, m_dpi);                       // room for "+"
    for (int i = 0; i < n; ++i)
        total += TabWidth(i) + Scaled(UiTokens::TabCardGap, m_dpi);
    return SIZE{ total, szAvailable.cy };
}

void CTabStripUI::RecalcRects(bool notifyOnly)
{
    const int n = (int)m_tabs.size();
    const int top = m_rcItem.top + Scaled(2, m_dpi);
    const int bottom = m_rcItem.bottom;
    const int gap = Scaled(UiTokens::TabCardGap, m_dpi);
    const int plusW = Scaled(32, m_dpi);
    const int avail = (std::max)(0, static_cast<int>(m_rcItem.right - m_rcItem.left) - plusW - gap);
    (void)notifyOnly;

    // Explorer geometry: each tab is sized by its own measured title (clamped to the min/max
    // range), never by "divide the row by the tab count" - that is what squeezed eight tabs
    // down to a single glyph each. When the row runs out of room the strip scrolls instead of
    // shrinking further.
    m_contentW = 0;
    for (int i = 0; i < n; ++i)
        m_contentW += TabWidth(i) + gap;
    ClampScroll();

    int x = m_rcItem.left - m_scrollX;
    for (int i = 0; i < n; ++i) {
        const int w = TabWidth(i);
        m_tabs[i].body = { x, top, x + w, bottom };
        const int closeSize = Scaled(16, m_dpi);
        const int cx = m_tabs[i].body.right - Scaled(6, m_dpi) - closeSize;
        const int cy = (top + bottom - closeSize) / 2;
        m_tabs[i].close = { cx, cy, cx + closeSize, cy + closeSize };
        x += w + gap;
    }
    // "+" follows the last tab, but never slides under the caption buttons.
    int plusX = x;
    if (plusX + plusW > m_rcItem.right)
        plusX = m_rcItem.right - plusW;
    if (plusX < m_rcItem.left)
        plusX = m_rcItem.left;
    m_plus = { plusX, (top + bottom - Scaled(32, m_dpi)) / 2,
               plusX + plusW, (top + bottom + Scaled(32, m_dpi)) / 2 };
}

// Natural width of one tab: icon + gap + measured title + (close button when shown).
int CTabStripUI::MeasureTabWidth(int index) const
{
    if (index < 0 || index >= (int)m_tabs.size()) return Scaled(m_minTabW, m_dpi);
    const int iconPad = Scaled(10, m_dpi);
    const int iconPx = Scaled(16, m_dpi);
    const int iconGap = Scaled(6, m_dpi);
    const int closeSlot = Scaled(22, m_dpi);

    int textW = 0;
    if (m_pManager && !m_tabs[index].title.empty()) {
        HFONT hf = m_pManager->GetFont(7);
        if (!hf) hf = m_pManager->GetFont(0);
        HDC dc = ::GetDC(m_pManager->GetPaintWindow());
        if (dc) {
            HGDIOBJ old = hf ? ::SelectObject(dc, hf) : nullptr;
            SIZE sz = { 0, 0 };
            ::GetTextExtentPoint32W(dc, m_tabs[index].title.c_str(),
                static_cast<int>(m_tabs[index].title.size()), &sz);
            textW = sz.cx;
            if (old) ::SelectObject(dc, old);
            ::ReleaseDC(m_pManager->GetPaintWindow(), dc);
        }
    }
    return iconPad + iconPx + iconGap + textW + closeSlot + Scaled(6, m_dpi);
}

int CTabStripUI::TabWidth(int index) const
{
    int w = MeasureTabWidth(index);
    const int minW = Scaled(index == m_active ? m_selMinTabW : m_minTabW, m_dpi);
    const int maxW = Scaled(m_maxTabW, m_dpi);
    if (w < minW) w = minW;
    if (w > maxW) w = maxW;
    return w;
}

void CTabStripUI::ClampScroll()
{
    const int plusW = Scaled(32, m_dpi);
    const int gap = Scaled(UiTokens::TabCardGap, m_dpi);
    const int viewW = (std::max)(0, static_cast<int>(m_rcItem.right - m_rcItem.left) - plusW - gap);
    const int maxScroll = (std::max)(0, m_contentW - viewW);
    if (m_scrollX > maxScroll) m_scrollX = maxScroll;
    if (m_scrollX < 0) m_scrollX = 0;
}

void CTabStripUI::EnsureTabVisible(int index)
{
    if (index < 0 || index >= (int)m_tabs.size()) return;
    const int gap = Scaled(UiTokens::TabCardGap, m_dpi);
    const int plusW = Scaled(32, m_dpi);
    const int viewW = (std::max)(0, static_cast<int>(m_rcItem.right - m_rcItem.left) - plusW - gap);
    int x = 0;
    for (int i = 0; i < index; ++i)
        x += TabWidth(i) + gap;
    const int w = TabWidth(index);
    int scroll = m_scrollX;
    if (x < scroll) scroll = x;
    else if (x + w > scroll + viewW) scroll = x + w - viewW;
    if (scroll != m_scrollX) {
        m_scrollX = scroll;
        ClampScroll();
        RecalcRects();
        Invalidate();
    }
}

void CTabStripUI::SetPos(RECT rc, bool bNeedInvalidate)
{
    CContainerUI::SetPos(rc, bNeedInvalidate);
    RecalcRects();
}

CTabStripUI::HitInfo CTabStripUI::HitTest(POINT pt) const
{
    HitInfo info;
    if (!::PtInRect(&m_rcItem, pt)) return info;      // outside -> None
    if (::PtInRect(&m_plus, pt)) { info.part = Part::Plus; return info; }
    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        if (::PtInRect(&m_tabs[i].close, pt)) { info.index = i; info.part = Part::Close; return info; }
        if (::PtInRect(&m_tabs[i].body, pt)) { info.index = i; info.part = Part::Body; return info; }
    }
    info.part = Part::Empty;
    return info;
}

bool CTabStripUI::IsCaptionDragPoint(POINT pt) const
{
    const HitInfo h = HitTest(pt);
    return h.part == Part::Empty;
}

void CTabStripUI::Notify(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!m_pManager) return;
    HWND hwnd = m_pManager->GetPaintWindow();
    if (!hwnd) return;
    ::PostMessageW(hwnd, msg, wParam, lParam);
}

void CTabStripUI::DoEvent(TEventUI& event)
{
    if (event.Type == UIEVENT_MOUSEMOVE) {
        const HitInfo h = HitTest(event.ptMouse);
        const int hot = (h.part == Part::Body || h.part == Part::Close) ? h.index : -1;
        const int hotClose = (h.part == Part::Close) ? h.index : -1;
        const int hotPlus = (h.part == Part::Plus) ? 1 : -1;
        if (hot != m_hot || hotClose != m_hotClose || hotPlus != m_hotPlus) {
            m_hot = hot;
            m_hotClose = hotClose;
            m_hotPlus = hotPlus;
            Invalidate();
        }
        // Drag: reorder live, or leave the window to clone the tab.
        if (m_pressed >= 0 && (event.wKeyState & MK_LBUTTON) != 0) {
            const int dx = event.ptMouse.x - m_dragStart.x;
            const int dy = event.ptMouse.y - m_dragStart.y;
            if (!m_dragging && (abs(dx) > Scaled(4, m_dpi) || abs(dy) > Scaled(4, m_dpi))) {
                m_dragging = true;
                m_dragFrom = m_pressed;
            }
            if (m_dragging) {
                RECT client = {};
                ::GetClientRect(m_pManager->GetPaintWindow(), &client);
                POINT screen = event.ptMouse;
                ::ClientToScreen(m_pManager->GetPaintWindow(), &screen);
                if (!::PtInRect(&client, event.ptMouse)) {
                    // Moved outside the window: hand over to the host (it clones a window).
                    POINT* buf = new POINT(screen);
                    Notify(kMsgTabDragOut, (WPARAM)m_dragFrom, (LPARAM)buf);
                    m_dragging = false;
                    m_pressed = -1;
                    m_dragFrom = -1;
                    return;
                }
                const int target = IndexFromX(event.ptMouse.x);
                if (target >= 0 && target != m_dragFrom) {
                    const int from = m_dragFrom;
                    Reorder(from, target);
                    m_dragFrom = target;
                    m_pressed = target;
                    Notify(kMsgTabReorder, (WPARAM)from, (LPARAM)target);
                }
            }
            m_dragLast = event.ptMouse;
        }
        CContainerUI::DoEvent(event);
        return;
    }

    if (event.Type == UIEVENT_BUTTONDOWN) {
        const HitInfo h = HitTest(event.ptMouse);
        if (h.part == Part::Close || h.part == Part::Body || h.part == Part::Plus) {
            m_pressed = (h.part == Part::Plus) ? -2 : h.index;
            m_dragFrom = -1;
            m_dragging = false;
            m_dragStart = event.ptMouse;
            m_dragLast = event.ptMouse;
            ::SetCapture(m_pManager->GetPaintWindow());
            Invalidate();
            return;
        }
    }

    if (event.Type == UIEVENT_BUTTONUP) {
        if (m_pManager) ::ReleaseCapture();
        const HitInfo h = HitTest(event.ptMouse);
        const int pressed = m_pressed;
        const bool dragging = m_dragging;
        m_pressed = -1;
        m_dragging = false;
        m_dragFrom = -1;
        if (dragging) { Invalidate(); return; }
        if (pressed == -2) {                       // "+"
            if (h.part == Part::Plus) Notify(kMsgTabAdd, 0, 0);
            Invalidate();
            return;
        }
        if (pressed >= 0 && h.index == pressed) {
            if (h.part == Part::Close) Notify(kMsgTabClose, (WPARAM)pressed, 0);
            else if (h.part == Part::Body) Notify(kMsgTabSelect, (WPARAM)pressed, 0);
        }
        Invalidate();
        return;
    }

    if (event.Type == UIEVENT_CONTEXTMENU) {
        const HitInfo h = HitTest(event.ptMouse);
        if (h.part == Part::Body || h.part == Part::Close) {
            POINT screen = event.ptMouse;
            ::ClientToScreen(m_pManager->GetPaintWindow(), &screen);
            POINT* buf = new POINT(screen);
            Notify(kMsgTabContextMenu, (WPARAM)h.index, (LPARAM)buf);
        }
        return;
    }

    if (event.Type == UIEVENT_SCROLLWHEEL) {
        // Overflowing tabs scroll horizontally (Explorer scrolls the strip with the wheel).
        const int step = Scaled(m_minTabW / 2, m_dpi);
        const int dir = (LOWORD(event.wParam) == SB_LINEDOWN) ? 1 : -1;
        const int before = m_scrollX;
        m_scrollX += dir * step;
        ClampScroll();
        if (m_scrollX != before) {
            RecalcRects();
            Invalidate();
        }
        return;
    }

    if (event.Type == UIEVENT_MOUSELEAVE) {
        if (m_hot >= 0 || m_hotClose >= 0 || m_hotPlus >= 0) {
            m_hot = m_hotClose = m_hotPlus = -1;
            Invalidate();
        }
    }

    if (event.Type == UIEVENT_TIMER) {
        const DWORD now = ::GetTickCount();
        bool busy = false;
        for (const auto& t : m_tabs)
            if (t.born && now - t.born < 160) { busy = true; break; }
        Invalidate();
        if (!busy) {
            if (m_pManager) m_pManager->KillTimer(this, 1);
            m_animating = false;
        }
        return;
    }

    CContainerUI::DoEvent(event);
}

int CTabStripUI::IndexFromX(int x) const
{
    for (int i = 0; i < (int)m_tabs.size(); ++i)
        if (x < m_tabs[i].body.right) return i;
    return (int)m_tabs.size() - 1;
}

void CTabStripUI::DrawTabGlass(Gdiplus::Graphics& g, const RECT& rc, bool selected, bool hovered)
{
    const TabPalette& pal = m_dark ? kDark : kLight;
    const int radius = Scaled(UiTokens::TabCardRound, m_dpi);
    // The active card is one pixel taller than the strip so its bottom edge lands *inside* the
    // next row: no grey hairline between the tab and the surface it merges into.
    RECT card = rc;
    if (selected) card.bottom += 1;
    Gdiplus::GraphicsPath shape;
    BuildTopRoundedPath(shape, card, radius);
    if (selected) {
        Gdiplus::SolidBrush brush(Gdiplus::Color(pal.selected));
        g.FillPath(&brush, &shape);
        StrokeTopAndSides(g, card, radius, pal.selectedEdge);
    } else if (hovered) {
        Gdiplus::GraphicsPath hoverPath;
        BuildTopRoundedPath(hoverPath, card, radius);
        Gdiplus::SolidBrush brush(Gdiplus::Color(pal.hover));
        g.FillPath(&brush, &hoverPath);
    }
}

void CTabStripUI::DrawTabIcon(Gdiplus::Graphics& g, int index, const RECT& rc)
{
    Tab& t = m_tabs[index];
    const int px = Scaled(16, m_dpi);
    const int x = rc.left + Scaled(10, m_dpi);
    const int y = (rc.top + rc.bottom - px) / 2;
    if (t.icon && t.icon->GetLastStatus() == Gdiplus::Ok) {
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(t.icon, Gdiplus::Rect(x, y, px, px));
    }
}

void CTabStripUI::DrawTabText(Gdiplus::Graphics& g, int index, const RECT& rc, COLORREF color)
{
    if (!m_pManager) return;
    HFONT hf = m_pManager->GetFont(7);
    if (!hf) hf = m_pManager->GetFont(0);
    LOGFONTW lf = {};
    if (hf) ::GetObjectW(hf, sizeof(lf), &lf);

    Gdiplus::FontFamily family(lf.lfFaceName[0] ? lf.lfFaceName : L"Segoe UI");
    Gdiplus::Font font(&family, (Gdiplus::REAL)Scaled(13, m_dpi), Gdiplus::FontStyleRegular,
        Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));

    Gdiplus::StringFormat fmt;
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);

    const int left = rc.left + Scaled(10, m_dpi) + Scaled(16, m_dpi) + Scaled(6, m_dpi);
    const int right = rc.right - Scaled(22, m_dpi);
    if (right <= left) return;
    Gdiplus::RectF text((Gdiplus::REAL)left, (Gdiplus::REAL)rc.top,
        (Gdiplus::REAL)(right - left), (Gdiplus::REAL)(rc.bottom - rc.top));
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    g.DrawString(m_tabs[index].title.c_str(), -1, &font, text, &fmt, &brush);
}

void CTabStripUI::DrawCloseGlyph(Gdiplus::Graphics& g, const RECT& rc, bool hot)
{
    const TabPalette& pal = m_dark ? kDark : kLight;
    if (hot) {
        Gdiplus::GraphicsPath rr;
        BuildTopRoundedPath(rr, rc, Scaled(4, m_dpi));
        Gdiplus::SolidBrush brush(Gdiplus::Color(pal.closeHotFill));
        g.FillPath(&brush, &rr);
    }
    const Gdiplus::ARGB color = hot ? pal.closeGlyphHot : pal.closeGlyph;
    Gdiplus::Pen pen(Gdiplus::Color(color), (Gdiplus::REAL)Scaled(1, m_dpi) + 0.6f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const int pad = Scaled(4, m_dpi);
    g.DrawLine(&pen, (Gdiplus::REAL)(rc.left + pad), (Gdiplus::REAL)(rc.top + pad),
        (Gdiplus::REAL)(rc.right - pad), (Gdiplus::REAL)(rc.bottom - pad));
    g.DrawLine(&pen, (Gdiplus::REAL)(rc.right - pad), (Gdiplus::REAL)(rc.top + pad),
        (Gdiplus::REAL)(rc.left + pad), (Gdiplus::REAL)(rc.bottom - pad));
}

void CTabStripUI::DrawPlusGlyph(Gdiplus::Graphics& g, const RECT& rc)
{
    const TabPalette& pal = m_dark ? kDark : kLight;
    if (m_hotPlus > 0) {
        Gdiplus::SolidBrush brush(Gdiplus::Color(pal.hover));
        Gdiplus::GraphicsPath rr;
        BuildTopRoundedPath(rr, rc, Scaled(6, m_dpi));
        g.FillPath(&brush, &rr);
    }
    Gdiplus::Pen pen(Gdiplus::Color(pal.text), (Gdiplus::REAL)Scaled(1, m_dpi) + 0.6f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;
    const int r = Scaled(5, m_dpi);
    g.DrawLine(&pen, (Gdiplus::REAL)cx, (Gdiplus::REAL)(cy - r), (Gdiplus::REAL)cx, (Gdiplus::REAL)(cy + r));
    g.DrawLine(&pen, (Gdiplus::REAL)(cx - r), (Gdiplus::REAL)cy, (Gdiplus::REAL)(cx + r), (Gdiplus::REAL)cy);
}

bool CTabStripUI::DoPaint(HDC hDC, const RECT& rcPaint, CControlUI* pStopControl)
{
    RECT rcClip = {};
    if (!::IntersectRect(&rcClip, &rcPaint, &m_rcItem)) return true;
    const TabPalette& pal = m_dark ? kDark : kLight;

    Gdiplus::Graphics g(hDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    // DuiLib only clips us to the parent band, so a scrolled tab could paint over the caption
    // buttons; clip to the strip's own rect as well.
    {
        RECT rcSelf = m_rcItem;
        if (rcSelf.right > rcClip.right) rcSelf.right = rcClip.right;
        if (rcSelf.bottom > rcClip.bottom) rcSelf.bottom = rcClip.bottom;
        g.SetClip(Gdiplus::Rect(rcSelf.left, rcSelf.top,
            rcSelf.right - rcSelf.left, rcSelf.bottom - rcSelf.top),
            Gdiplus::CombineModeIntersect);
    }

    // The title band is left unpainted so the DWM backdrop (Mica Alt) shows through. A very
    // light wash over the tab area keeps the white "active card" readable on top of it - the
    // same trick Explorer uses for its slightly darker tab strip.
    {
        Gdiplus::SolidBrush wash(Gdiplus::Color(m_dark ? 0x18FFFFFF : 0x12000000));
        // Only the strip's own content (tabs + "+") is tinted, like Explorer's tab band; the
        // caption area to its right stays pure Mica.
        int washRight = m_rcItem.left + Scaled(1, m_dpi);
        if (!m_tabs.empty()) washRight = (std::max)(washRight, (int)m_tabs.back().body.right);
        washRight = (std::max)(washRight, (int)m_plus.right + Scaled(4, m_dpi));
        if (washRight > m_rcItem.right) washRight = m_rcItem.right;
        const int wl = (std::max)((int)rcClip.left, (int)m_rcItem.left);
        const int wr = (std::min)((int)rcClip.right, washRight);
        if (wr > wl) {
            g.FillRectangle(&wash, Gdiplus::Rect(wl, rcClip.top, wr - wl,
                rcClip.bottom - rcClip.top));
        }
    }

    const DWORD now = ::GetTickCount();
    for (int i = 0; i < (int)m_tabs.size(); ++i) {
        RECT rc = m_tabs[i].body;
        if (rc.right < rcClip.left || rc.left > rcClip.right) continue;   // scrolled out
        // short slide-in for a fresh tab
        if (m_tabs[i].born) {
            const DWORD age = now - m_tabs[i].born;
            if (age < 160) {
                const int shift = Scaled(10, m_dpi) * (int)(160 - age) / 160;
                rc.left += shift;
                rc.right += shift;
            } else {
                m_tabs[i].born = 0;
            }
        }
        const bool selected = (i == m_active);
        const bool hovered = (i == m_hot && !selected);
        DrawTabGlass(g, rc, selected, hovered);
        DrawTabIcon(g, i, rc);
        DrawTabText(g, i, rc, selected ? GetSysColor(COLOR_WINDOWTEXT)
            : (m_dark ? RGB(0xB3, 0xB3, 0xB3) : RGB(0x5C, 0x5C, 0x5C)));
        if (selected || hovered || m_hotClose == i) {
            RECT cr = m_tabs[i].close;
            if (i != m_active && m_hotClose != i) { /* show a quiet X for hovered idle tabs */ }
            DrawCloseGlyph(g, cr, m_hotClose == i);
        }
    }
    DrawPlusGlyph(g, m_plus);
    (void)pal;
    return CContainerUI::DoPaint(hDC, rcPaint, pStopControl);
}

} // namespace DuiLib
