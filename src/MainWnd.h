#pragma once

#include "UIlib.h"
#include "UiTokens.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace DuiLib;

// Forward for OLE drop target / Shell context menu
struct IDropTarget;
struct IContextMenu;
struct IContextMenu2;
struct IContextMenu3;

// FastFile main window — single-pane file manager
// Features: search filter (+ recursive), directory tree, multi-tabs,
//           Explorer-like views, multi-select, bg copy, delete/rename/new folder,
//           shell context menu, back/forward history, drag-drop, session persist,
//           drop->bg copy progress, preview pane, breadcrumb/sort/col widths,
//           virtualized icon window for large folders,
//           Phase3 command-bar groups + Win11 chrome density,
//           Phase1 UI tokens (colors/fonts/spacing 4/8/12/16) + unified chrome,
//           Phase2 left-nav/details density (UiTokens / Win11 Explorer),
//           A visible scrollbars, B breadcrumb hits, C left nav splitter,
//           D shell IContextMenu (items/blank/tree)
class CMainWnd : public WindowImplBase
{
public:
    CMainWnd() = default;
    ~CMainWnd() override;

    CDuiString GetSkinFolder() override;
    CDuiString GetSkinFile() override;
    LPCTSTR GetWindowClassName() const override;

    void InitWindow() override;
    void Notify(TNotifyUI& msg) override;
    void OnClick(TNotifyUI& msg) override;
    LRESULT HandleCustomMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) override;
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    LRESULT ResponseDefaultKeyEvent(WPARAM wParam) override;

    // DPI (96 baseline design units → physical pixels)
    int DpiScale(int px) const;
    float DpiScaleF(float v) const;
    void RefreshDpiFromWindow();
    void ApplyDpiScaledFonts();
    void ApplyDpiScaledChrome();
    void OnDpiChanged(UINT newDpi, const RECT* suggested);

    // Called by OLE drop target
    DWORD HitTestDropPath(POINT ptScreen, std::wstring& outDir) const;
    bool PerformDropTransfer(const std::vector<std::wstring>& srcPaths, const std::wstring& destDir, DWORD effect);
    HWND GetSafeHwnd() const { return m_hWnd; }
    // Call after Create()/before CenterWindow so DPI is finalized.
    void EnsureDpiLayout();

private:
    struct DirEntry {
        std::wstring name;
        std::wstring fullPath;
        bool isDir = false;
        ULONGLONG size = 0;
        ULONGLONG capacity = 0;
        ULONGLONG mtime = 0; // FILETIME as ULONGLONG
        DWORD attrs = 0;
    };

    struct ClipboardItem {
        std::wstring path;
        bool isDir = false;
    };

    struct CopyProgressSnapshot {
        int filesDone = 0;
        int filesTotal = 0;
        ULONGLONG bytesDone = 0;
        ULONGLONG bytesTotal = 0;
        wchar_t current[MAX_PATH] = {};
        DWORD lastError = 0;
        enum class State { Idle, Running, Done, Failed, Cancelled } state = State::Idle;
    };

    struct TabInfo {
        std::wstring path;
        std::wstring searchFilter;
        std::vector<std::wstring> backStack;
        std::vector<std::wstring> forwardStack;
        CButtonUI* button = nullptr;
    };

    enum class ViewMode {
        ExtraLargeIcons,
        LargeIcons,
        MediumIcons,
        List,
        Details,
        Tiles
    };

    enum class SortColumn {
        Name = 0,
        Modified = 1,
        Type = 2,
        Size = 3
    };

    void NavigateTo(const std::wstring& path, bool addToHistory = true);
    void RefreshListing();
    void GoUp();
    void GoBack();
    void GoForward();
    void UpdateNavButtons();
    void PushHistoryBeforeNav(const std::wstring& fromPath);
    void OnItemActivate(CControlUI* pSender);
    void UpdateStatus(LPCTSTR text);

    void OnCopyClicked();
    void OnPasteClicked();
    void OnCancelCopyClicked();
    void OnDeleteClicked(bool permanent = false);
    void OnRenameClicked();
    void OnNewFolderClicked();
    void OnCutClicked();
    void OnShareClicked();
    void OnNewMenuClicked();
    void OnSortMenuClicked();
    void OnViewMenuClicked();
    void OnMoreMenuClicked();
    void FocusSearchBox();
    void ShowPropertiesForSelection();

    // Undo (Ctrl+Z) — only for operations FastFile performs itself
    struct UndoRecord {
        enum class Kind { Rename, CreateFolder, Move } kind = Kind::Rename;
        std::wstring from;   // path before the operation
        std::wstring to;     // path after the operation
        // Kind::Move only: every (source, destination) pair of that one move operation,
        // so a single Ctrl+Z restores the whole batch.
        std::vector<std::pair<std::wstring, std::wstring>> moved;
    };
    void PushUndo(UndoRecord::Kind kind, std::wstring from, std::wstring to);
    void PushMoveUndo(std::vector<std::pair<std::wstring, std::wstring>> pairs);
    void OnUndo();
    void ShowToolbarPopupMenu(CControlUI* anchor, HMENU hMenu);
    void OnFavoriteClicked(const CDuiString& name);
    void UpdateFavoritesHighlight();

    // C: Horizontal favorites bar (persist %APPDATA%\FastFile\favorites.txt)
    struct FavoriteItem {
        std::wstring path;
        std::wstring displayName;
    };
    static std::wstring GetFavoritesFilePath();
    void LoadFavorites();
    void SaveFavorites() const;
    void RebuildFavoritesBar();
    void RebuildLeftPinnedFavorites();
    bool PinFavorite(const std::wstring& path);
    bool UnpinFavorite(const std::wstring& path);
    bool IsFavoritePinned(const std::wstring& path) const;
    void OnPinnedFavoriteClick(CControlUI* btn);
    void ShowFavoriteContextMenu(CControlUI* btn, POINT ptScreen);
    bool IsOverFavoritesBar(POINT ptClient) const;
    static std::wstring GetQuickAccessFilePath();
    void LoadQuickAccess();
    void SaveQuickAccess() const;
    bool PinQuickAccess(const std::wstring& path);
    bool UnpinQuickAccess(const std::wstring& path);
    bool IsQuickAccessPinned(const std::wstring& path) const;
    bool InvokeShellRename(const std::wstring& path);

    // Search / filter
    void ApplySearchFilter();
    void ClearSearchFilter();
    void SetSearchPlaceholder(bool show);
    void SyncRecursiveCheckLabel();
    bool EntryMatchesFilter(const DirEntry& e) const;
    bool IsRecursiveSearch() const;
    void CollectRecursiveMatches(const std::wstring& root, const std::wstring& filter,
        std::vector<DirEntry>& dirs, std::vector<DirEntry>& files, bool& truncated);

    // Directory tree
    void InitDirectoryTree();
    void StyleTreeNode(CTreeNodeUI* node, const std::wstring& title, bool hasChildrenHint);
    void AttachPendingChild(CTreeNodeUI* parent);
    void ExpandTreeNode(CTreeNodeUI* node, bool navigate);
    void EnsureTreeChildren(CTreeNodeUI* node);
    void SyncTreeToPath(const std::wstring& path);
    CTreeNodeUI* FindTreeNodeByPath(CTreeNodeUI* parent, const std::wstring& path) const;
    CTreeNodeUI* AddTreeFolderNode(CTreeNodeUI* parent, const std::wstring& path, const std::wstring& title);
    bool OnTreeFolderNotify(void* param);
    void OnTreeNodeActivate(CTreeNodeUI* node);

    // Tabs
    void InitTabs();
    void RebuildTabStrip();
    void AddTab(const std::wstring& path, bool activate);
    void CloseTab(int index);
    void ActivateTab(int index);
    void UpdateActiveTabPath(const std::wstring& path);
    std::wstring TabTitleForPath(const std::wstring& path) const;

    // Session persist
    void SaveSession() const;
    bool LoadSession();
    static std::wstring GetSessionFilePath();

    // Per-folder view memory (%APPDATA%\FastFile\folder_views.ini)
    static std::wstring GetFolderViewsFilePath();
    static std::wstring NormalizeViewKey(const std::wstring& path);
    ViewMode LoadFolderViewForPath(const std::wstring& path) const;
    void SaveFolderViewForPath(const std::wstring& path, ViewMode mode) const;

    // View modes
    void SetViewMode(ViewMode mode);
    void UpdateViewModeButtons();
    bool IsTileViewMode() const;
    void ApplyTileLayoutMetrics();
    void GetViewMetrics(int& tileW, int& tileH, int& iconPx, int& childPad, int& maxLabel) const;
    void RebuildDetailsView(const std::vector<DirEntry>& dirs, const std::vector<DirEntry>& files, bool truncated);
    void RebuildIconsView(const std::vector<DirEntry>& dirs, const std::vector<DirEntry>& files, bool truncated);
    void RebuildCurrentViewFromCache();
    void StoreListingCache(std::vector<DirEntry> dirs, std::vector<DirEntry> files, bool truncated);
    void UpdateListingStatusTip();
    bool TryReuseIconsView(const std::vector<DirEntry>& dirs, const std::vector<DirEntry>& files);
    void ClearIconView();
    void ApplyTileIconImage(CControlUI* tile, const std::wstring& bmp,
        int tileW, int tileH, int iconPx, bool listMode, bool tilesMode);
    std::wstring PeekCachedIconBmp(const std::wstring& path, bool isDir, int cx);
    std::wstring GetShellIconBmp(const std::wstring& path, bool isDir, int cx);
    // Small shell icons only (no IShellItemImageFactory thumbnails) — details/list.
    std::wstring GetShellFileIconBmp(const std::wstring& path, bool isDir, int cx);
    static bool LetterboxHBitmapToPng(HBITMAP hbm, int cx, int cy, const std::wstring& pngPath);
    static bool SaveIconToPng(HICON hIcon, const std::wstring& pngPath, int cx, int cy);
    static bool SaveImageThumbnailPng(const std::wstring& srcPath, const std::wstring& pngPath, int cx, int cy);
    static bool IsImageExtension(const std::wstring& name);
    static bool IsTextExtension(const std::wstring& name);
    static bool IsVideoExtension(const std::wstring& name);
    static bool SaveHBitmapToPng(HBITMAP hbm, const std::wstring& pngPath);
    static bool EnsureGdiplus();
    static bool GetPngEncoderClsid(CLSID* pClsid);
    static void WipeDirectoryFiles(const std::wstring& dirNoSlash);
    // Content thumbs only; never SIIGBF_ICONONLY (folders/drives use HICON).
    static bool ExtractShellItemImage(const std::wstring& path, int cx, int cy, const std::wstring& pngPath);
    static bool ExtractShellIconSized(const std::wstring& path, bool isDir, int cx, const std::wstring& bmpPath);

    // Phase 1: Shell icons for chrome (toolbar/address/favorites) + tree
    void ApplyChromeShellIcons();
    void ApplyTreeNodeIcon(CTreeNodeUI* node, const std::wstring& path);
    void RefreshTreeShellIcons();
    void ApplyControlForeIcon(CControlUI* ctrl, const std::wstring& bmp,
        int iconPx, int destX, int destY, bool clearText);
    std::wstring GetStockIconBmp(int siid, int cx);
    std::wstring GetModuleIconBmp(const wchar_t* moduleFile, int index, int cx);
    static bool ExtractStockIconSized(int siid, int cx, const std::wstring& bmpPath);
    static bool ExtractModuleIconSized(const wchar_t* moduleFile, int index, int cx, const std::wstring& bmpPath);

    // Sort / columns / breadcrumb (D)
    void SortListingCache();
    void OnHeaderColumnClick(CControlUI* pHeaderItem);
    void UpdateHeaderSortIndicators();
    void ApplyColumnWidths();
    void CaptureColumnWidths();
    static std::wstring FormatModifiedTime(ULONGLONG ft);
    static std::wstring FormatDriveDisplayName(const std::wstring& rootPath);
    bool ShouldHideByAttributes(DWORD attrs) const;
    void SetShowHidden(bool show);
    void ToggleShowHidden();
    void ApplyWindowCornerAndPadding();
    void ApplyUiChromeTokens(); // Phase1: paddings + unified Win11 light colors
    bool TrackPopupShellMenu(IContextMenu* pMenu, HMENU hMenu, POINT ptScreen,
        UINT idCmdFirst, UINT idShellMax, bool appendHiddenToggle);
    void ForwardShellMenuMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* pResult, bool* handled);
    void RebuildBreadcrumb();
    void OnBreadcrumbSegmentClick(CControlUI* btn);
    // Explorer-style path: breadcrumb default; click/focus -> editable address
    void EnterAddressEditMode();
    void ExitAddressEditMode(bool commitNavigate);
    // Reclaim keyboard focus from the (hidden) native address edit after leaving edit mode.
    void ReturnFocusToFileView();
    void SyncAddressEditFromPath();

    // Preview pane (B) — Win11 Explorer-like details
    void SetPreviewVisible(bool visible);
    void ClearPreview();
    void ClearPreviewMeta();
    void ShowPreviewDetails(bool showMeta, bool showImage, bool showActions);
    void SetPreviewMeta(const std::wstring& typeName,
        const std::wstring& sizeText,
        const std::wstring& mtimeText,
        const std::wstring& ctimeText);
    void ClearPreviewExtraMeta();
    void SetPreviewExtraMeta(const std::wstring& location,
        const std::wstring& dimensions,
        const std::wstring& duration,
        const std::wstring& frameRate,
        const std::wstring& bitRate,
        const std::wstring& totalBitRate);
    void FillPreviewMetaFromPath(const std::wstring& path, bool isDir);
    void UpdatePreviewForCurrentFolder();
    void UpdatePreviewForSelection();
    void UpdatePreviewPath(const std::wstring& path, bool isDir);
    bool LoadPreviewImage(const std::wstring& path);
    bool LoadPreviewText(const std::wstring& path);
    bool LoadPreviewShellThumbnail(const std::wstring& path, int cx, int cy);
    // Folder/generic: HICON -> PNG true alpha (never SIIGBF black pocket)
    bool LoadPreviewShellIcon(const std::wstring& path, bool isDir, int iconPx);
    // Adaptive frame + centered Fit bkimage (folders: compact icon area)
    void ApplyPreviewImageBk(const std::wstring& pngPath, int imgPxW, int imgPxH, int frameDesignH);
    static std::wstring FormatFileTimeLocal(const FILETIME& ft);
    static std::wstring QueryShellTypeName(const std::wstring& path, bool isDir);
    static std::wstring QueryImageDimensions(const std::wstring& path);
    void FillVideoPreviewMeta(const std::wstring& path);

    // Virtualized icon window + progressive details fill (E)
    void FlattenListing(std::vector<DirEntry>& out) const;
    void RebuildIconsViewFull(const std::vector<DirEntry>& all);
    void RebuildIconsViewVirtual(const std::vector<DirEntry>& all);
    void EnsureIconTilePool(int poolCount, int tileW, int tileH);
    void BindIconTile(CButtonUI* tile, int index, const DirEntry& e, UINT gen,
        int tileW, int tileH, int iconPx, int maxLabel, bool listMode, bool tilesMode);
    void SyncVisibleIconWindow(bool force);
    int ComputeIconVirtPoolSize(int tileW, int tileH) const;
    void StartDetailsProgressiveFill(const std::vector<DirEntry>& dirs, const std::vector<DirEntry>& files);
    void OnDetailsFillTick();
    void StopDetailsFill();
    CListContainerElementUI* CreateDetailsRow(const DirEntry& e);

    // Async thumbnails
    struct ThumbJob {
        UINT generation = 0;
        int index = -1;
        std::wstring path;
        bool isDir = false;
        int iconPx = 48;
        int tileW = 100;
        int tileH = 108;
        bool listMode = false;
        bool tilesMode = false;
    };
    struct ThumbReadyPayload {
        UINT generation = 0;
        int index = -1;
        std::wstring bmpPath;
        int iconPx = 48;
        int tileW = 100;
        int tileH = 108;
        bool listMode = false;
        bool tilesMode = false;
    };
    void StartThumbWorker();
    void StopThumbWorker();
    void CancelThumbJobs();
    void EnqueueThumbJob(const ThumbJob& job);
    void OnThumbReadyMessage(LPARAM lParam);
    static void ThumbWorkerMain(CMainWnd* self);

    // Icon-mode selection
    void OnIconTileClick(CControlUI* tile);
    void ActivateIconTile(CControlUI* tile);
    void ClearIconSelection();
    void ClearFileSelection();
    void SelectAllItems();
    // Pixel width of `text` in the current UI font (font 0). Used to size chips/tabs to their
    // label instead of estimating from the character count. Returns 0 if it cannot measure.
    // (Non-const: DuiLib's CPaintManagerUI::GetFont is not const.)
    int MeasureTextWidth(const std::wstring& text);
    bool IsFileViewBlankHit(CControlUI* hit) const;
    bool HasFileSelection() const;
    // True while a text box that should own the keyboard has focus. The address box only
    // counts while it is actually in edit mode: DuiLib leaves it focused (and its native
    // window alive) after the host is hidden, which would otherwise swallow every shortcut.
    bool IsEditingText() const;
    void SetIconSelected(CControlUI* tile, bool selected);
    void ApplyIconSelectionVisual(CControlUI* tile);
    int FindIconIndex(CControlUI* tile) const;
    void SelectIconRange(int from, int to);

    void CollectSelectedItems(std::vector<ClipboardItem>& out) const;
    bool DeleteItems(const std::vector<ClipboardItem>& items, bool permanent = false);
    bool RenameItem(const ClipboardItem& item, const std::wstring& newName);
    bool CreateNewFolder();

    void ShowItemContextMenu(CControlUI* pItem, POINT ptScreen);
    bool ShowShellContextMenu(const std::vector<std::wstring>& paths, POINT ptScreen);
    bool ShowShellBackgroundContextMenu(const std::wstring& folderPath, POINT ptScreen);
    void ShowFallbackContextMenu(const std::vector<ClipboardItem>& items, POINT ptScreen);
    void ShowTreeContextMenu(CTreeNodeUI* node, POINT ptScreen);
    void ShowBlankAreaContextMenu(POINT ptScreen);

    // A: visible vertical scrollbars on file views
    void StyleVerticalScrollBar(CContainerUI* host);
    void ApplyFileViewScrollBars();

    // C: left Quick Access / This PC splitter persist
    static std::wstring GetLeftNavFilePath();
    void LoadLeftNavSplitter();
    void SaveLeftNavSplitter() const;
    void ApplyLeftNavSplitterHeight(int designHeight);
    void CaptureLeftNavSplitterIfChanged();

    // Drag-drop
    void InitDragDrop();
    void UninitDragDrop();
    bool BeginDragSelectedItems();
    CControlUI* HitTestFileItem(POINT ptClient) const;
    CTreeNodeUI* HitTestTreeNode(POINT ptClient) const;
    std::wstring ResolveDropDirectory(POINT ptScreen) const;
    bool TransferWithShell(const std::vector<std::wstring>& srcPaths, const std::wstring& destDir, bool move);
    bool TransferWithBackgroundCopy(const std::vector<std::wstring>& srcPaths, const std::wstring& destDir,
        bool move = false);

    void StartCopyJob(std::vector<ClipboardItem> items, std::wstring destDir, bool move = false);
    void StopCopyThread(bool wait);
    void ApplyCopyUiState();
    void OnCopyProgressMessage();
    void OnCopyFinishedMessage(WPARAM resultCode);

    static void CopyWorkerMain(CMainWnd* self,
        std::vector<ClipboardItem> items,
        std::wstring destDir,
        bool move);
    static bool CopyOneFile(CMainWnd* self, const std::wstring& src, const std::wstring& dst);
    static bool CopyDirectoryRecursive(CMainWnd* self, const std::wstring& src, const std::wstring& dst);
    // Move: same-volume rename fast path, otherwise copy-with-progress then delete the source.
    // itemFiles/itemBytes are pre-measured so the rename path can still advance the bar.
    static bool MoveOneItem(CMainWnd* self, const ClipboardItem& item, const std::wstring& dest,
        int itemFiles, ULONGLONG itemBytes);
    static bool DeleteTreePermanent(const std::wstring& path);
    static ULONGLONG CalcTotalBytes(const std::vector<ClipboardItem>& items, std::atomic<bool>& cancel);
    static ULONGLONG CalcPathBytes(const std::wstring& path, bool isDir, std::atomic<bool>& cancel);
    static int CountFiles(const std::vector<ClipboardItem>& items, std::atomic<bool>& cancel);
    static int CountFilesInPath(const std::wstring& path, bool isDir, std::atomic<bool>& cancel);
    static DWORD CALLBACK CopyProgressRoutine(
        LARGE_INTEGER TotalFileSize,
        LARGE_INTEGER TotalBytesTransferred,
        LARGE_INTEGER StreamSize,
        LARGE_INTEGER StreamBytesTransferred,
        DWORD dwStreamNumber,
        DWORD dwCallbackReason,
        HANDLE hSourceFile,
        HANDLE hDestinationFile,
        LPVOID lpData);
    static void PostProgress(CMainWnd* self);
    static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);
    static std::wstring UniqueDestPath(const std::wstring& destPath);
    static std::wstring GetLeafName(const std::wstring& path);

    static std::wstring GetDefaultStartPath();
    static std::wstring GetKnownFolderPath(int csidl);
    static std::wstring GetDownloadsPath();
    static bool PathEquals(const std::wstring& a, const std::wstring& b);
    static bool IsThisPcPath(const std::wstring& path);
    static std::wstring NormalizePath(const std::wstring& path);
    static std::wstring ParentPath(const std::wstring& path);
    static std::wstring FormatFileSize(ULONGLONG bytes);
    static void PumpUiMessages();
    static bool PromptText(HWND owner, const wchar_t* title, const wchar_t* prompt,
        const wchar_t* initial, std::wstring& out);

    void BringToForeground();
    void EnsureMainWindowVisible();

private:
    CEditUI* m_pAddressEdit = nullptr;
    CHorizontalLayoutUI* m_pAddressEditHost = nullptr;
    CHorizontalLayoutUI* m_pPathHost = nullptr;
    CEditUI* m_pSearchEdit = nullptr;
    bool m_addressEditMode = false;
    CListUI* m_pFileList = nullptr;
    CTreeViewUI* m_pDirTree = nullptr;
    CHorizontalLayoutUI* m_pTabStrip = nullptr;
    CHorizontalLayoutUI* m_pBreadcrumb = nullptr;
    CHorizontalLayoutUI* m_pFavoritesBar = nullptr;
    CHorizontalLayoutUI* m_pFavoritesStrip = nullptr;
    CVerticalLayoutUI* m_pLeftFavPins = nullptr;
    CVerticalLayoutUI* m_pLeftQuick = nullptr;
    CVerticalLayoutUI* m_pLeftThisPc = nullptr;
    CVerticalLayoutUI* m_pIconScroll = nullptr;
    CTileLayoutUI* m_pIconTiles = nullptr;
    int m_leftQuickDesignH = UiTokens::LeftQuickDefaultH; // @96 DPI, persisted
    std::vector<FavoriteItem> m_favorites;
    std::vector<FavoriteItem> m_quickAccess;
    std::vector<std::wstring> m_shellMenuPaths;
    CVerticalLayoutUI* m_pPreviewPane = nullptr;
    CLabelUI* m_pPreviewTitle = nullptr;
    CControlUI* m_pPreviewImage = nullptr;
    CLabelUI* m_pPreviewText = nullptr;
    CLabelUI* m_pPreviewType = nullptr;
    CLabelUI* m_pPreviewSize = nullptr;
    CLabelUI* m_pPreviewMTime = nullptr;
    CLabelUI* m_pPreviewCTime = nullptr;
    CLabelUI* m_pPreviewLocation = nullptr;
    CLabelUI* m_pPreviewDimensions = nullptr;
    CLabelUI* m_pPreviewDuration = nullptr;
    CLabelUI* m_pPreviewFrameRate = nullptr;
    CLabelUI* m_pPreviewBitRate = nullptr;
    CLabelUI* m_pPreviewTotalBitRate = nullptr;
    CButtonUI* m_pBtnTogglePreview = nullptr;
    CLabelUI* m_pStatus = nullptr;
    CButtonUI* m_pBtnCopy = nullptr;
    CButtonUI* m_pBtnPaste = nullptr;
    CButtonUI* m_pBtnCancelCopy = nullptr;
    CButtonUI* m_pBtnBack = nullptr;
    CButtonUI* m_pBtnForward = nullptr;
    COptionUI* m_pChkRecursive = nullptr;
    bool m_searchPlaceholder = false;

    std::wstring m_currentPath;
    std::wstring m_searchFilter;
    ViewMode m_viewMode = ViewMode::Tiles;
    UINT m_dpi = 96;
    bool m_dpiChromeApplied = false;
    int m_designClientW = 1180;
    int m_designClientH = 740;
    int m_iconAnchor = -1;
    DWORD m_lastIconClickTick = 0;
    CControlUI* m_lastIconClickTile = nullptr;
    int m_iconPx = 48;

    SortColumn m_sortColumn = SortColumn::Name;
    bool m_sortAscending = true;
    int m_colWidthName = 360;
    int m_colWidthMTime = 140;
    int m_colWidthType = 100;
    int m_colWidthSize = 110;
    bool m_showHidden = false;
    bool m_previewVisible = true;
    IContextMenu* m_pCtxMenu = nullptr;
    IContextMenu2* m_pCtxMenu2 = nullptr;
    IContextMenu3* m_pCtxMenu3 = nullptr;
    std::wstring m_previewPath;
    std::wstring m_previewBmp;
    unsigned m_previewSerial = 0;

    // Virtualization state (E)
    bool m_iconVirtMode = false;
    int m_virtFirstIndex = 0;
    int m_virtPoolCount = 0;
    int m_virtSpacerBefore = 0;
    int m_virtSpacerAfter = 0;
    CControlUI* m_pVirtSpacerBefore = nullptr;
    CControlUI* m_pVirtSpacerAfter = nullptr;
    std::vector<DirEntry> m_flatListing;
    int m_detailsFillNext = 0;
    std::vector<DirEntry> m_detailsFillQueue;
    bool m_detailsFilling = false;

    std::vector<TabInfo> m_tabs;
    int m_activeTab = -1;
    bool m_updatingTabs = false;
    bool m_syncingTree = false;
    bool m_navigatingHistory = false;

    // Drag state
    bool m_dragTracking = false;
    POINT m_dragStartPt = {};
    IDropTarget* m_pDropTarget = nullptr;
    bool m_inDoDragDrop = false;

    std::vector<ClipboardItem> m_clipboard;
    bool m_clipboardIsCut = false;
    std::wstring m_lastCopyDest;
    bool m_jobIsMove = false;
    // (source, destination) pairs of items moved by the running job; guarded by m_progressMutex.
    std::vector<std::pair<std::wstring, std::wstring>> m_moveUndoPairs;
    std::vector<UndoRecord> m_undoStack;

    std::map<std::wstring, std::wstring> m_iconCache;
    std::mutex m_iconCacheMutex;
    std::wstring m_iconCacheDir;

    // Listing cache
    std::vector<DirEntry> m_listingDirs;
    std::vector<DirEntry> m_listingFiles;
    bool m_listingTruncated = false;
    std::wstring m_listingPath;
    std::wstring m_listingFilter;
    bool m_listingRecursive = false;
    bool m_hasListingCache = false;

    // Thumbnail worker
    std::thread m_thumbThread;
    std::mutex m_thumbMutex;
    std::condition_variable m_thumbCv;
    std::deque<ThumbJob> m_thumbQueue;
    std::atomic<bool> m_thumbStop{false};
    std::atomic<UINT> m_thumbGeneration{1};

    std::thread m_copyThread;
    std::atomic<bool> m_copyCancel{false};
    std::atomic<bool> m_copyRunning{false};
    std::mutex m_progressMutex;
    CopyProgressSnapshot m_progress;
    ULONGLONG m_workerBytesBase = 0;
    ULONGLONG m_workerFileSize = 0;

    static constexpr const wchar_t* kThisPcPath = L"::ThisPC";
    static constexpr const wchar_t* kPendingMarker = L"::pending";
    static constexpr const wchar_t* kFavoritePinPath = L"::FavoritePin";
    static constexpr UINT_PTR kCmdFavUnpin = 9101;
    static constexpr UINT_PTR kCmdFavOpen = 9102;
    static constexpr int kMaxListItems = 8000;
    static constexpr int kMaxIconThumbs = 400;
    static constexpr int kMaxRecursiveItems = 4000;
    static constexpr int kPumpEvery = 200;
    static constexpr UINT kMsgReactivate = WM_USER + 100;
    static constexpr UINT kMsgCopyProgress = WM_USER + 101;
    static constexpr UINT kMsgCopyFinished = WM_USER + 102;
    static constexpr UINT kMsgThumbReady = WM_USER + 103;
    static constexpr UINT kMsgVirtSync = WM_USER + 104;
    static constexpr UINT kMsgDetailsFill = WM_USER + 105;
    static constexpr int kUiBatchSize = 40;
    static constexpr int kVirtThreshold = 220;
    static constexpr int kVirtOverscanRows = 3;
    static constexpr int kDetailsFirstBatch = 80;
    static constexpr int kDetailsFillBatch = 120;
    static constexpr int kPreviewMaxTextBytes = 96 * 1024;
    static constexpr UINT_PTR kTimerVirtSync = 0x4601;
    static constexpr UINT_PTR kTimerColWidth = 0x4602;

    static constexpr UINT_PTR kCmdCtxOpen = 9001;
    static constexpr UINT_PTR kCmdCtxCopy = 9002;
    static constexpr UINT_PTR kCmdCtxDelete = 9003;
    static constexpr UINT_PTR kCmdCtxRename = 9004;
    static constexpr UINT_PTR kCmdCtxRefresh = 9005;
    static constexpr UINT_PTR kCmdToggleHidden = 9201;
    // Commands FastFile adds to the Shell *folder background* menu (Explorer's own view menu),
    // kept well clear of the Shell's idCmdFirst..idCmdLast range.
    static constexpr UINT_PTR kCmdBgRefresh = 9300;
    static constexpr UINT_PTR kCmdBgPaste = 9301;
    static constexpr UINT_PTR kCmdBgViewBase = 9310;   // +0..5 -> ViewMode
    static constexpr UINT_PTR kCmdBgSortBase = 9320;   // +0..3 -> SortColumn, +4 asc, +5 desc
};
