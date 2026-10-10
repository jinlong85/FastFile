#pragma once
// FastFile - Explorer-style tab strip.
//
// One self-drawn control owns the tab geometry, hover/press state and the hit testing that
// the caption (WM_NCHITTEST) and the mouse gestures use. CMainWnd keeps the tab *model*
// (src/MainWnd.Tabs.cpp) and mirrors it here through Add/RemoveAt/SetActive; the strip only
// reports user intent back with the kMsgTab* notifications below.

#include <string>
#include <vector>

namespace DuiLib {

class CTabStripUI : public CContainerUI
{
public:
    enum class Part { None, Body, Close, Plus, Empty };
    struct HitInfo {
        int  index = -1;
        Part part = Part::None;
    };

    CTabStripUI();
    ~CTabStripUI() override;

    // ---- model mirror (called by CMainWnd) --------------------------------
    int  GetCount() const { return static_cast<int>(m_tabs.size()); }
    int  GetActive() const { return m_active; }
    int  Add(const std::wstring& path, const std::wstring& title,
             const std::wstring& iconBmp, int iconPx, bool activate, bool isPinned = false);
    void Insert(int index, const std::wstring& path, const std::wstring& title,
                const std::wstring& iconBmp, int iconPx, bool isPinned = false);
    bool RemoveAt(int index);
    bool SetTabPinned(int index, bool pinned);
    bool IsTabPinned(int index) const;
    int  GetPinnedCount() const;
    void Clear();
    bool Select(int index);
    bool Reorder(int from, int to);
    int  FindByPath(const std::wstring& path) const;
    void SetTabTitle(int index, const std::wstring& title);
    void SetTabIcon(int index, const std::wstring& iconBmp, int iconPx);
    void SetActiveTab(int index);

    // ---- appearance -------------------------------------------------------
    void SetDarkMode(bool dark);
    void SetBarHeight(int height) { m_barHeight=height; }
    void SetWidthPercent(int percent) { m_widthPercent=percent; }
    void SetMetrics(int dpi);            // icon px / radii / max tab width
    void SetMaxTabWidth(int px) { m_maxTabW = px; }
    void SetTabWidthRange(int minW, int selMinW, int maxW)
    {
        m_minTabW = minW; m_selMinTabW = selMinW; m_maxTabW = maxW;
    }
    // Bring a tab into the visible window (a selected tab that was scrolled out must come back).
    void EnsureTabVisible(int index);

    // ---- hit testing (mouse gestures + WM_NCHITTEST) ----------------------
    HitInfo HitTest(POINT ptClient) const;
    bool    IsCaptionDragPoint(POINT ptClient) const;   // empty area -> HTCAPTION

    // ---- DuiLib overrides -------------------------------------------------
    LPCTSTR GetClass() const override;
    LPVOID  GetInterface(LPCTSTR pstrName) override;
    SIZE    EstimateSize(SIZE szAvailable) override;
    void    SetPos(RECT rc, bool bNeedInvalidate = true) override;
    void    DoEvent(TEventUI& event) override;
    bool    DoPaint(HDC hDC, const RECT& rcPaint, CControlUI* pStopControl) override;

    // notifications sent to the host window
    static constexpr UINT kMsgTabSelect = WM_USER + 300;   // wParam = index
    static constexpr UINT kMsgTabClose  = WM_USER + 301;   // wParam = index
    static constexpr UINT kMsgTabReorder = WM_USER + 302;  // wParam = from, lParam = to
    static constexpr UINT kMsgTabDragOut = WM_USER + 303;  // wParam = index, lParam = POINT*
    static constexpr UINT kMsgTabContextMenu = WM_USER + 304; // wParam = index, lParam = POINT*
    static constexpr UINT kMsgTabAdd = WM_USER + 305;
    static constexpr UINT kMsgTabEmptyContextMenu = WM_USER + 306; // lParam = POINT*

private:
    friend struct TabStripRegressionAccess;
    struct Tab {
        std::wstring path;
        std::wstring title;
        std::wstring iconBmp;
        Gdiplus::Bitmap* icon = nullptr;   // cached, owned
        int  iconPx = 16;
        RECT body = {};
        RECT close = {};
        int  width = 0;        // allocated by preferred/minimum layout, independent of active state
        bool isPinned = false;
    };

    void  RecalcRects(bool notifyOnly = false);
    void  Notify(UINT msg, WPARAM wParam, LPARAM lParam);
    int   IndexFromX(int x) const;
    void  DrawTabGlass(Gdiplus::Graphics& g, const RECT& rc, bool selected, bool hovered);
    void  DrawTabIcon(Gdiplus::Graphics& g, int index, const RECT& rc);
    void  DrawTabText(Gdiplus::Graphics& g, int index, const RECT& rc, COLORREF color);
    void  DrawCloseGlyph(Gdiplus::Graphics& g, const RECT& rc, bool hot);
    void  DrawPlusGlyph(Gdiplus::Graphics& g, const RECT& rc);
    int   MeasureTabWidth(int index) const;   // natural width from the measured title
    int   TabWidth(int index) const;          // clamped to [min|selMin, max]
    void  ClampScroll();

    std::vector<Tab> m_tabs;
    int   m_active = -1;
    int   m_hot = -1;
    int   m_hotClose = -1;
    int   m_hotPlus = -1;
    int   m_pressed = -1;
    int   m_dragFrom = -1;
    POINT m_dragStart = {};
    POINT m_dragLast = {};
    bool  m_dragging = false;
    bool  m_dark = false;
    int   m_dpi = 96;
    int   m_maxTabW = 180;
    int m_barHeight=29;
    int m_widthPercent=150;
    int   m_minTabW = 180;
    int   m_selMinTabW = 180;
    int   m_scrollX = 0;         // horizontal offset when the tabs do not fit
    int   m_contentW = 0;        // total width of the tab cells
    RECT  m_plus = {};
    Gdiplus::Font* m_font = nullptr;
    Gdiplus::FontFamily* m_fontFamily = nullptr;
};

} // namespace DuiLib
