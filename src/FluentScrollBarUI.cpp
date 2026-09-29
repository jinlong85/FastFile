#include "MainWndInternal.h"
#include "FluentScrollBarUI.h"

namespace {

// Explorer's overlay bar: the thumb darkens a touch on hover and again while dragging.
constexpr int kHotShift = -14;      // per channel, on hover
constexpr int kPushedShift = -26;   // per channel, while dragging
constexpr DWORD kHotTrackFallback = 0xFFF0F0F0;  // shown when the bar had no idle track

} // namespace

DWORD CFluentScrollBarUI::ShiftColor(DWORD argb, int delta)
{
    const int a = static_cast<int>((argb >> 24) & 0xFF);
    int r = static_cast<int>((argb >> 16) & 0xFF) + delta;
    int g = static_cast<int>((argb >> 8) & 0xFF) + delta;
    int b = static_cast<int>(argb & 0xFF) + delta;
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (static_cast<DWORD>(a) << 24) | (static_cast<DWORD>(r) << 16)
        | (static_cast<DWORD>(g) << 8) | static_cast<DWORD>(b);
}

bool CFluentScrollBarUI::SameRect(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

void CFluentScrollBarUI::SetRailMetrics(int idlePx, int hoverPx)
{
    if (idlePx < 1) idlePx = 1;
    if (hoverPx < idlePx) hoverPx = idlePx;
    if (m_idlePx == idlePx && m_hoverPx == hoverPx) return;
    const bool wasExpanded = m_expanded;
    const RECT before = ThumbRectPx();
    m_idlePx = idlePx;
    m_hoverPx = hoverPx;
    if (!SameRect(before, ThumbRectPx()) || wasExpanded) {
        InvalidateOverhang();
        Invalidate();
    }
}

void CFluentScrollBarUI::SetDockFar(bool dockFar)
{
    if (m_dockFar == dockFar) return;
    m_dockFar = dockFar;
    InvalidateOverhang();
    Invalidate();
}

int CFluentScrollBarUI::DrawnThickness() const
{
    return m_expanded ? m_hoverPx : m_idlePx;
}

RECT CFluentScrollBarUI::ThumbRectPx()
{
    RECT out = m_rcThumb;
    const int t = DrawnThickness();
    if (IsHorizontal()) {
        if (m_dockFar) { out.bottom = m_rcItem.bottom; out.top = m_rcItem.bottom - t; }
        else { out.top = m_rcItem.top; out.bottom = m_rcItem.top + t; }
    } else {
        if (m_dockFar) { out.right = m_rcItem.right; out.left = m_rcItem.right - t; }
        else { out.left = m_rcItem.left; out.right = m_rcItem.left + t; }
    }
    return out;
}

RECT CFluentScrollBarUI::TrackRectPx()
{
    RECT out = m_rcItem;
    const int t = DrawnThickness();
    if (IsHorizontal()) {
        if (m_dockFar) out.top = out.bottom - t;
        else out.bottom = out.top + t;
    } else {
        if (m_dockFar) out.left = out.right - t;
        else out.right = out.left + t;
    }
    return out;
}

void CFluentScrollBarUI::InvalidateOverhang()
{
    if (!m_pManager) return;
    const int grow = m_hoverPx - m_idlePx;
    if (grow <= 0) return;
    RECT rc = m_rcItem;
    if (IsHorizontal()) {
        if (m_dockFar) rc.top -= grow; else rc.bottom += grow;
    } else {
        if (m_dockFar) rc.left -= grow; else rc.right += grow;
    }
    m_pManager->Invalidate(rc);
}

void CFluentScrollBarUI::SetExpanded(bool on)
{
    if (m_expanded == on) return;
    const RECT before = ThumbRectPx();
    m_expanded = on;
    // The thumb moved outside its old bounds: repaint both the bar and the overhang strip.
    InvalidateOverhang();
    if (!SameRect(before, ThumbRectPx())) {
        RECT both = before;
        const RECT after = ThumbRectPx();
        ::UnionRect(&both, &before, &after);
        if (m_pManager) m_pManager->Invalidate(both);
    }
    Invalidate();
}

bool CFluentScrollBarUI::HitTestHover(const POINT& pt, int margin)
{
    const RECT rc = m_rcItem;
    if (rc.right <= rc.left || rc.bottom <= rc.top) return false;
    if (margin < 0) margin = 0;
    const bool horizontal = IsHorizontal();
    // Along the bar the pointer must be inside its span; across the bar it may also sit
    // just inside the edge the rail hugs (the pointer never has to find the thin rail).
    const int along = horizontal ? pt.x : pt.y;
    const int alongFrom = horizontal ? rc.left : rc.top;
    const int alongTo = horizontal ? rc.right : rc.bottom;
    if (along < alongFrom || along >= alongTo) return false;

    const int across = horizontal ? pt.y : pt.x;
    const int acrossFrom = horizontal ? rc.top : rc.left;
    const int acrossTo = horizontal ? rc.bottom : rc.right;
    if (across >= acrossFrom && across < acrossTo) return true;   // right on the rail
    if (m_dockFar) return across >= acrossFrom - margin && across < acrossFrom;
    return across >= acrossTo && across < acrossTo + margin;
}

void CFluentScrollBarUI::PaintRounded(HDC hDC, const RECT& rc, DWORD argb, int radius)
{
    if (argb == 0 || argb <= 0x00FFFFFF) return;
    if (rc.right <= rc.left || rc.bottom <= rc.top) return;
    const int d = (std::max)(2, radius * 2);
    const COLORREF cr = RGB(GetBValue(argb), GetGValue(argb), GetRValue(argb));
    HBRUSH brush = ::CreateSolidBrush(cr);
    HPEN pen = ::CreatePen(PS_SOLID, 1, cr);
    HGDIOBJ oldBrush = ::SelectObject(hDC, brush);
    HGDIOBJ oldPen = ::SelectObject(hDC, pen);
    ::RoundRect(hDC, rc.left, rc.top, rc.right, rc.bottom, d, d);
    ::SelectObject(hDC, oldPen);
    ::SelectObject(hDC, oldBrush);
    ::DeleteObject(pen);
    ::DeleteObject(brush);
}

bool CFluentScrollBarUI::DoPaint(HDC hDC, const RECT& rcPaint, CControlUI* pStopControl)
{
    (void)rcPaint;
    (void)pStopControl;
    if (!hDC) return true;
    if (m_rcItem.right <= m_rcItem.left || m_rcItem.bottom <= m_rcItem.top) return true;

    const bool pushed = (m_uThumbState & UISTATE_PUSHED) != 0;
    // A transparent bkcolor (the file views) is 0x00FFFFFF, not 0: test the alpha, not the
    // whole value, or the bar would look like it has a track that paints nothing.
    const DWORD idleTrack = GetBkColor();
    const bool hasIdleTrack = ((idleTrack >> 24) & 0xFF) != 0;
    DWORD trackColor = 0;
    if (m_expanded)
        trackColor = hasIdleTrack ? ShiftColor(idleTrack, kHotShift) : kHotTrackFallback;
    else if (hasIdleTrack)
        trackColor = idleTrack;
    const bool rangeOk = GetScrollRange() > 0
        && m_rcThumb.right > m_rcThumb.left && m_rcThumb.bottom > m_rcThumb.top;
    // A zero thumb colour means "no thumb" (the preview rail uses that when nothing scrolls).
    DWORD thumbColor = GetThumbColor();
    if (((thumbColor >> 24) & 0xFF) == 0) thumbColor = 0;
    if (thumbColor != 0 && (m_expanded || pushed))
        thumbColor = ShiftColor(thumbColor, pushed ? kPushedShift : kHotShift);

    const int thickness = DrawnThickness();
    const int radius = (thickness + 1) / 2;

    if (trackColor != 0) {
        const RECT rcTrack = TrackRectPx();
        PaintRounded(hDC, rcTrack, trackColor, radius);
    }
    if (rangeOk && thumbColor != 0) {
        RECT rcThumb = ThumbRectPx();
        // Keep DuiLib's minimum grab length (it uses the control width) so a short thumb
        // still reads as a pill rather than a dot.
        const int minLen = (std::max)(m_hoverPx, m_idlePx);
        if (IsHorizontal()) {
            if (rcThumb.right - rcThumb.left < minLen) rcThumb.right = rcThumb.left + minLen;
        } else {
            if (rcThumb.bottom - rcThumb.top < minLen) rcThumb.bottom = rcThumb.top + minLen;
        }
        PaintRounded(hDC, rcThumb, thumbColor, radius);
    }
    return true;
}
