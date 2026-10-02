#pragma once

#include "UIlib.h"

// Windows 11 style scrollbar for FastFile.
//
// The visible rail keeps the app's existing thin width. Once the pointer is over the
// scrolling view (or on the bar itself) the thumb turns into a rounded pill and widens,
// and a light rounded track appears behind it - Explorer's overlay bar.
//
// DuiLib sizes the thumb rect from the control's own width, so the extra width is painted
// *over* the neighbouring content instead of taking layout space:
//   * safe because CRenderEngine::DrawColor() returns early for fully transparent colours,
//     so the container beside the bar never repaints across the overhang;
//   * CContainerUI::DoPaint() paints the scrollbar after its contents, so nothing covers it;
//   * the overhang strip is added to the invalidate rect, otherwise the update-region clip
//     would cut it off (a child is skipped entirely when rcPaint misses its rect).
//
// Colours come from the usual DuiLib attributes: `bkcolor` is the idle track (0 = none, the
// file views) and `thumbcolor` the idle thumb. Missing / darker variants are derived, and a
// bar with no idle track gets Explorer's light track while hovered.
class CFluentScrollBarUI : public DuiLib::CScrollBarUI
{
public:
    // Physical pixels for the drawn cross-axis size, idle and hovered.
    void SetRailMetrics(int idlePx, int hoverPx);
    // true  : the rail hugs the right / bottom edge (file views, tree, horizontal bar)
    // false : it hugs the left / top edge (the preview rail on the preview pane's left edge)
    void SetDockFar(bool dockFar);

    void SetExpanded(bool on);
    bool IsExpanded() const { return m_expanded; }
    bool IsDragging() const { return (m_uThumbState & UISTATE_CAPTURED) != 0; }
    void CancelGesture();
    void DoEvent(DuiLib::TEventUI& event) override;
    // True when pt is on the bar, or within `margin` px of its docked-inner side.
    bool HitTestHover(const POINT& pt, int margin);

    bool DoPaint(HDC hDC, const RECT& rcPaint, CControlUI* pStopControl) override;

private:
    int DrawnThickness() const;      // (non-const helpers: CScrollBarUI::IsHorizontal is not const)
    RECT ThumbRectPx();
    RECT TrackRectPx();
    void InvalidateOverhang();
    static void PaintRounded(HDC hDC, const RECT& rc, DWORD argb, int radius);
    static DWORD ShiftColor(DWORD argb, int delta);
    static bool SameRect(const RECT& a, const RECT& b);

    bool m_expanded = false;
    bool m_dockFar = true;
    int m_idlePx = 4;
    int m_hoverPx = 4;
};
