#pragma once
#include <UIlib.h>
#include <gdiplus.h>
#include <cmath>

// Original vector artwork, independent of font fallback and bitmap scaling.
class CFavoriteStarUI : public DuiLib::CButtonUI {
public:
    void SetPinned(bool pinned) { if (m_pinned != pinned) { m_pinned = pinned; Invalidate(); } }
    bool IsPinned() const { return m_pinned; }
    void PaintText(HDC) override {}
    void PaintStatusImage(HDC dc) override {
        const float scale = GetFixedWidth() / 32.0f;
        const float cx = (m_rcItem.left + m_rcItem.right) / 2.0f;
        const float cy = (m_rcItem.top + m_rcItem.bottom) / 2.0f;
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const bool hot = (m_uButtonState & (UISTATE_HOT | UISTATE_PUSHED)) != 0;
        if (hot && IsEnabled()) {
            const float r = 4 * scale, d = 2 * r;
            Gdiplus::RectF box(float(m_rcItem.left), float(m_rcItem.top),
                float(m_rcItem.right - m_rcItem.left), float(m_rcItem.bottom - m_rcItem.top));
            Gdiplus::GraphicsPath bg;
            bg.AddArc(box.X, box.Y, d, d, 180, 90);
            bg.AddArc(box.GetRight() - d, box.Y, d, d, 270, 90);
            bg.AddArc(box.GetRight() - d, box.GetBottom() - d, d, d, 0, 90);
            bg.AddArc(box.X, box.GetBottom() - d, d, d, 90, 90);
            bg.CloseFigure();
            Gdiplus::SolidBrush fill(Gdiplus::Color(255, 232, 232, 232));
            g.FillPath(&fill, &bg);
        }
        // The star including its thin stroke fits a 16-logical-pixel square.
        Gdiplus::PointF points[10];
        for (int i = 0; i < 10; ++i) {
            const float angle = float(-1.57079632679 + i * 3.14159265359 / 5);
            const float radius = (i % 2 ? 3.25f : 7.25f) * scale;
            points[i] = { cx + radius * std::cos(angle), cy + radius * std::sin(angle) };
        }
        Gdiplus::GraphicsPath star;
        star.AddPolygon(points, 10);
        Gdiplus::Color color = m_pinned ? Gdiplus::Color(255, 199, 163, 0) :
            hot ? Gdiplus::Color(255, 26, 26, 26) : Gdiplus::Color(255, 92, 92, 92);
        if (m_pinned) { Gdiplus::SolidBrush fill(color); g.FillPath(&fill, &star); }
        Gdiplus::Pen pen(color, scale);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPath(&pen, &star);
    }
private:
    bool m_pinned = false;
};
