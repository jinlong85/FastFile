#pragma once

#include <Windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <ocidl.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <map>

class ShellBrowserHost final
{
public:
    ShellBrowserHost();
    ~ShellBrowserHost();

    ShellBrowserHost(const ShellBrowserHost&) = delete;
    ShellBrowserHost& operator=(const ShellBrowserHost&) = delete;

    bool Create(HWND parent, const RECT& bounds, UINT navigationMessage,
        UINT selectionMessage, UINT folderOpenMessage=0, UINT contextMenuMessage=0);
    void Destroy();
    void SetBounds(const RECT& bounds);
    bool Navigate(const std::wstring& path);
    void Refresh();
    bool BeginRename();
    bool SelectAll();
    bool ClearSelection();
    bool Focus();
    bool InvokeHistory(bool redo, bool invoke = true);
    HRESULT TranslateAccelerator(MSG* message);
    bool OwnsWindow(HWND window) const;
    bool IsAtPath(const std::wstring& path) const;
    std::wstring CurrentPath() const { return m_lastNavigation; }
    bool SetVisible(bool visible);
    bool IsVisible() const { return m_visible; }
    bool SetFilter(const std::wstring& text);
    bool SetShowHidden(bool show);
    bool SetViewMode(FOLDERVIEWMODE mode, int iconSize = -1);
    void EnsureSelectionVisible();
    bool SetSort(int column, bool ascending);
    bool SetGrouping(int mode);
    bool GetSelection(std::vector<std::pair<std::wstring, bool>>& paths) const;
    bool IsCreated() const { return m_browser != nullptr; }

private:
    HRESULT DefaultCommand(IShellView* view, BOOL (WINAPI *execute)(SHELLEXECUTEINFOW*) = ShellExecuteExW);
    HICON AssociatedAppIcon(const std::wstring& path);
    friend struct ShellBrowserHostTestAccess;
    class EventSink;
    void AttachViewFilter(IShellView* view);
    void DetachSelectionEvents();
    void StyleNativeView(IFolderView2* view);
    bool ApplyViewMode(IFolderView2* view);
    bool ForwardContextMenu(WPARAM source, LPARAM position);
    LRESULT DrawIconItem(NMLVCUSTOMDRAW* draw);
    void ClearItemImages();
    static HBITMAP NormalizeImageAlpha(HBITMAP bitmap);
    void RestoreListSpacing();
    LRESULT DrawListIcon(NMLVCUSTOMDRAW* draw);
    HBITMAP ItemImage(IShellItem* item, const std::wstring& path);
    void PaintScrollBar(HWND window);
    static LRESULT CALLBACK ViewSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK ListSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    HWND m_listWindow = nullptr;
    HWND m_viewWindow = nullptr;
    int m_iconSlot = 0;
    int m_hotItem = -1;
    UINT m_dpi = 96;
    LVTILEVIEWINFO m_originalTileInfo{};
    bool m_customTileHeight = false;
    HIMAGELIST m_listSpacer=nullptr;
    HIMAGELIST m_shellSmallImages=nullptr;
    std::map<std::wstring, HBITMAP> m_itemImages;
    std::map<std::wstring, HICON> m_associatedIcons;
    IConnectionPoint* m_selectionEvents = nullptr;
    DWORD m_selectionCookie = 0;
    IExplorerBrowser* m_browser = nullptr;
    EventSink* m_events = nullptr;
    DWORD m_eventCookie = 0;
    HWND m_parent = nullptr;
    UINT m_navigationMessage = 0;
    UINT m_selectionMessage = 0;
    UINT m_folderOpenMessage = 0;
    UINT m_contextMenuMessage = 0;
    FOLDERVIEWMODE m_requestedMode = FVM_DETAILS;
    int m_requestedIconSize = -1;
    std::wstring m_lastNavigation;
    std::wstring m_filterText;
    IFolderFilterSite* m_filterSite = nullptr;
    IFolderFilter* m_filter = nullptr;
    bool m_visible = true;
    bool m_showHidden = false;
    int m_groupingMode=-1;
    PROPERTYKEY m_windowsGrouping{};
    BOOL m_windowsGroupingAscending=TRUE;
    HWND m_groupingView=nullptr;
    std::wstring m_groupingPath;
    bool m_hasWindowsGrouping=false;
    bool ApplyGrouping(IFolderView2* view,bool restore);
};
