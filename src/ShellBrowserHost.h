#pragma once

#include <Windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <ocidl.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <map>
#include <list>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class ShellBrowserHost final
{
public:
    ShellBrowserHost();
    ~ShellBrowserHost();

    ShellBrowserHost(const ShellBrowserHost&) = delete;
    ShellBrowserHost& operator=(const ShellBrowserHost&) = delete;

    // The hosted view keeps its own Windows context menus: WM_CONTEXTMENU / NM_RCLICK are
    // never intercepted, so DefView builds, shows and invokes the unmodified classic menu.
    bool Create(HWND parent, const RECT& bounds, UINT navigationMessage,
        UINT selectionMessage, UINT folderOpenMessage=0);
    void Destroy();
    void SetBounds(const RECT& bounds);
    bool Navigate(const std::wstring& path, bool retryPending = false);
    // Called by the host's layout timer: serialize navigation and retry Shell busy.
    void PollNavigation();
    void Refresh();
    bool BeginRename();
    bool SelectAll();
    bool ClearSelection();
    bool Focus();
    // The current view's own folder-background menu (IShellView::GetItemObject with
    // SVGIO_BACKGROUND): Explorer's 查看 / 排序方式 / 分组依据 / 粘贴 / 撤销 ... entries.
    HRESULT CreateBackgroundContextMenu(IContextMenu** menu) const;
    // The current view's own selection menu (IShellView::GetItemObject with SVGIO_SELECTION),
    // the object DefView itself uses for 复制 / 剪切 / 删除 / 属性 on the selected items.
    HRESULT CreateSelectionContextMenu(IContextMenu** menu) const;
    // Sites a Shell menu built outside the view (tree, quick-access rows, favourites) on the
    // hosted ExplorerBrowser, as Explorer sites its navigation-pane menus on the frame, so a
    // native "打开" browses the hosted view in place instead of launching a new window.
    bool SiteContextMenu(IUnknown* menu) const;
    HRESULT TranslateAccelerator(MSG* message);
    bool OwnsWindow(HWND window) const;
    bool IsAtPath(const std::wstring& path) const;
    bool IsNavigationCompleteAt(const std::wstring& path) const;
    bool HasVisibleViewBounds() const;
    // Selects an item of the current folder given its absolute id list (Shell
    // "show in folder" requests). S_FALSE while the view has not listed it yet.
    HRESULT SelectAbsoluteItem(PCIDLIST_ABSOLUTE item, UINT flags);
    std::wstring CurrentPath() const { return m_lastNavigation; }
    bool SetVisible(bool visible);
    bool IsVisible() const { return m_visible; }
    bool SetFilter(const std::wstring& text);
    bool SetShowHidden(bool show);
    bool SetViewMode(FOLDERVIEWMODE mode, int iconSize = -1);
    void EnsureSelectionVisible();
    // Paints pending invalidations of the native list now (selection frame / highlight)
    // instead of after FastFile's queued work.
    void FlushPaint();
    bool SetSort(int column, bool ascending);
    bool SetGrouping(int mode);
    bool GetSelection(std::vector<std::pair<std::wstring, bool>>& paths) const;
    // Selected items of the current view, virtual ones included (-1 without a view).
    int SelectedCount() const;
    bool IsCreated() const { return m_browser != nullptr; }
    // Large / extra-large thumbnail memory cache bounds (LRU; tests shrink them).
    void SetThumbnailCacheLimits(size_t maxBytes, size_t maxEntries);

private:
    HRESULT DefaultCommand(IShellView* view, BOOL (WINAPI *execute)(SHELLEXECUTEINFOW*) = ShellExecuteExW);
    HICON AssociatedAppIcon(const std::wstring& path);
    friend struct ShellBrowserHostTestAccess;
    class EventSink;
    std::wstring ActualViewPath() const;
    bool FinishNavigation(const std::wstring& path, bool failed);
    void TraceNavigation(const wchar_t* event, HRESULT result = S_OK) const;
    void ObserveViewState();
    void AttachViewFilter(IShellView* view);
    void DetachSelectionEvents();
    void StyleNativeView(IFolderView2* view);
    bool ApplyViewMode(IFolderView2* view);
    LRESULT DrawIconItem(NMLVCUSTOMDRAW* draw);
    void ClearItemImages();
    static HBITMAP NormalizeImageAlpha(HBITMAP bitmap);
    void RestoreListSpacing();
    bool InstallListSpacer(HWND list);
    HIMAGELIST GetSmallImageList();
    HIMAGELIST GetSystemSmallImageList();
    int ResolveItemIcon(int index, HIMAGELIST* outIml = nullptr);
    LRESULT DrawListIcon(NMLVCUSTOMDRAW* draw);
    // Thumbnail cache (UI thread): key = item identity + slot size + size / mtime stamp.
    struct ThumbEntry { std::wstring key; HBITMAP bitmap = nullptr; size_t bytes = 0; };
    bool LookupThumb(const std::wstring& key, HBITMAP& bitmap);
    void StoreThumb(const std::wstring& key, HBITMAP bitmap);
    void TrimThumbs();
    static std::wstring ThumbKey(IShellItem* item, const std::wstring& path, int size);
    // Extraction (IShellItemImageFactory) for one item; counts UI-thread calls.
    HBITMAP ExtractThumb(PCIDLIST_ABSOLUTE pidl, int size);
    // Async thumbnail worker (STA). Requests are coalesced by key, the most recently
    // painted (visible) items go first, and a generation bump drops stale ones.
    struct ThumbRequest {
        std::wstring key, path; PIDLIST_ABSOLUTE pidl = nullptr;
        int size = 0, item = -1; UINT generation = 0; ULONGLONG paintSeq = 0;
    };
    struct ThumbResult { std::wstring key, path; HBITMAP bitmap = nullptr; int item = -1; UINT generation = 0; };
    void RequestThumb(IShellItem* item, const std::wstring& key, const std::wstring& path, int index);
    void CancelThumbRequests();
    void StartThumbWorker();
    void StopThumbWorker();
    void OnThumbsReady();
    void InvalidateIconCell(int index);
    bool IconCell(int index, RECT& cell) const;
    void DrawPlaceholder(HDC dc, const std::wstring& path, const RECT& cell, bool folder);
    HICON PlaceholderIcon(int systemIndex);
    static void ThumbWorkerMain(ShellBrowserHost* self);
    static LRESULT CALLBACK ThumbWindowProc(HWND, UINT, WPARAM, LPARAM);
    // Per-extension association badge (perceived media type + app icon) and generic type icon,
    // cached for the session.
    struct BadgeInfo { bool media = false; HICON icon = nullptr; int size = 0; int systemIcon = -1; };
    const BadgeInfo& Badge(const std::wstring& path);
    void PaintScrollBar(HWND window);
    static LRESULT CALLBACK ViewSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK ListSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    // Cheap view-switch counters read by the regression tests (never reset).
    struct ViewCounters {
        int refreshes = 0;       // IShellView::Refresh (re-enumeration)
        int filterSets = 0;      // IFolderFilterSite::SetFilter
        int modeApplies = 0;     // ApplyViewMode
        int modeSets = 0;        // SetViewModeAndIconSize
        int iconSpacingSets = 0; // LVM_SETICONSPACING
        int spacerSwaps = 0;     // 26-px list / details spacer installed or removed
        int columnSets = 0;      // IColumnManager::SetColumns
        int sortSets = 0;        // SetSortColumns
        int groupSets = 0;       // SetGroupBy
        int redrawBatches = 0;   // WM_SETREDRAW off/on around a real change
        int listPaints = 0;      // WM_PAINT reaching the native list
        int fullPaints = 0;      // ... whose update region covers (nearly) the whole list
        int thumbHits = 0;       // large-icon cell drawn from the memory thumbnail cache
        int placeholders = 0;    // ... drawn with the Shell icon while its thumbnail loads
        int thumbRequests = 0;   // queued to the worker
        int coalesced = 0;       // repeated misses for an already queued item
        int thumbExtractions = 0;// thumbnails extracted by the worker (results received)
        int syncExtractions = 0; // thumbnail extractions on the UI thread (must stay 0)
        int staleDropped = 0;    // results / requests of an old folder or mode dropped
        int itemInvalidations = 0; // single cell invalidated when its thumbnail arrived
        int thumbEvictions = 0;  // LRU evictions
        int badgeResolves = 0;   // per-extension association badge lookups
    };
    ViewCounters m_counters;
    HWND m_redrawBatch = nullptr;    // list with redraw suspended during ApplyViewMode
    HWND m_spacingList = nullptr;    // icon spacing last applied: list, slot, result
    int m_spacingSlot = 0;
    DWORD m_appliedSpacing = 0;
    HWND m_listWindow = nullptr;
    HWND m_viewWindow = nullptr;
    int m_iconSlot = 0;
    int m_hotItem = -1;
    // Paint probe for tests: first QPC tick at which m_probeItem was custom-drawn selected.
    int m_probeItem = -1;
    LONGLONG m_probeTick = 0;
    UINT m_dpi = 96;
    LVTILEVIEWINFO m_originalTileInfo{};
    bool m_customTileHeight = false;
    HIMAGELIST m_listSpacer=nullptr;
    HIMAGELIST m_shellSmallImages=nullptr;
    HIMAGELIST m_systemSmallImages=nullptr;
    std::list<ThumbEntry> m_thumbLru;    // front = most recently used
    std::unordered_map<std::wstring, std::list<ThumbEntry>::iterator> m_thumbIndex;
    size_t m_thumbBytes = 0;
    size_t m_thumbMaxBytes = 96u * 1024u * 1024u;
    size_t m_thumbMaxEntries = 2000;
    std::unordered_set<std::wstring> m_thumbPending; // queued / in flight (UI thread)
    std::thread m_thumbThread;
    std::mutex m_thumbMutex;
    std::condition_variable m_thumbCv;
    std::deque<ThumbRequest> m_thumbQueue;     // guarded by m_thumbMutex
    std::vector<ThumbResult> m_thumbResults;   // guarded by m_thumbMutex
    bool m_thumbStop = false;                  // guarded by m_thumbMutex
    bool m_thumbPosted = false;                // guarded by m_thumbMutex
    int m_thumbDroppedQueued = 0;              // guarded by m_thumbMutex
    std::atomic<UINT> m_thumbGeneration{1};
    ULONGLONG m_paintSeq = 0;
    HWND m_thumbWindow = nullptr;
    DWORD m_uiThread = 0;
    std::map<std::wstring, HICON> m_associatedIcons;
    std::unordered_map<std::wstring, BadgeInfo> m_badges;
    std::map<int, HICON> m_placeholderIcons; // 256-px system icons by system image index
    int m_folderIcon = -1;
    IConnectionPoint* m_selectionEvents = nullptr;
    DWORD m_selectionCookie = 0;
    IExplorerBrowser* m_browser = nullptr;
    EventSink* m_events = nullptr;
    DWORD m_eventCookie = 0;
    HWND m_parent = nullptr;
    UINT m_navigationMessage = 0;
    UINT m_selectionMessage = 0;
    UINT m_folderOpenMessage = 0;
    FOLDERVIEWMODE m_requestedMode = FVM_DETAILS;
    int m_requestedIconSize = -1;
    std::wstring m_lastNavigation;
    std::wstring m_pendingNavigation;
    bool m_navigationFailed = false;
    bool m_navigationQueued = false;
    ULONGLONG m_navigationDeadline = 0;
    HWND m_observedList = nullptr;
    int m_observedItems = -2, m_observedVisible = -1, m_observedRedraw = -1;
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
