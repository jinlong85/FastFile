#pragma once

#include "UIlib.h"
#include "UiTokens.h"
#include "TabStripUI.h"
#include "FastFileSettings.h"
#include "ShellFileOperation.h"

#include <shtypes.h>

class ShellBrowserHost;
class ShellWindowRegistration;
struct ExplorerScanState;

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
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
class CMainWnd : public WindowImplBase, public ITranslateAccelerator
{
public:
    CMainWnd();
    ~CMainWnd() override;

    // Paths supplied by a Shell folder-open invocation.  The main window consumes these
    // after its controls and initial tab have been created.
    void SetStartupOpenPaths(std::vector<std::wstring> paths);
    static bool RestoreNativeFolderHandlers();
    static void ReadSystemIntegration(FastFileSettings& settings);
    static bool ApplySystemIntegration(const FastFileSettings& settings);
    static bool RepairOwnedSystemIntegration();
    struct IntegrationStatus {
        bool foldersReady=false, computerReady=false, menuReady=false;
        bool otherManager=false;
        std::wstring details;
    };
    static IntegrationStatus DetectSystemIntegration();
    struct ExplorerSnapshot {
        HWND window=nullptr;DWORD processId=0;
        std::wstring path;
        std::vector<std::wstring> selection;
        std::vector<std::vector<BYTE>> selectionIds;
    };
    // A second FastFile process hands its folders to a busy window through this
    // per-user queue plus a posted kMsgDrainOpenQueue, instead of dropping them.
    static std::wstring OpenQueueDirectory();
    static std::wstring QueueExternalOpen(const std::vector<std::wstring>& paths);
    static std::vector<std::wstring> DrainExternalOpenQueue();
    static constexpr UINT kMsgDrainOpenQueue = WM_USER + 108;   // must match main.cpp
    // Test hook replacing GetFileAttributesW when an external open probes its target
    // (simulates a spun-down disk). Production leaves it null.
    static DWORD (*s_folderProbe)(const std::wstring& path);
    // Tests that are not about Shell activation keep their windows out of the user's
    // real Shell window list.
    static bool s_shellWindowRegistrationAllowed;
    static bool ShouldRedirectDisabledShellOpen();
    static bool RedirectDisabledShellOpen(const std::vector<std::wstring>& paths);

    CDuiString GetSkinFolder() override;
    CDuiString GetSkinFile() override;
    LPCTSTR GetWindowClassName() const override;

    void InitWindow() override;
    void Notify(TNotifyUI& msg) override;
    void OnClick(TNotifyUI& msg) override;
    LRESULT HandleCustomMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) override;
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
    LRESULT ResponseDefaultKeyEvent(WPARAM wParam) override;
    LRESULT TranslateAccelerator(MSG* message) override;

    // DPI (96 baseline design units → physical pixels)
    int DpiScale(int px) const;
    int DpiScaleHairline(int px) const;   // rounds up: 1 design px -> 2 physical at 150%
    float DpiScaleF(float v) const;
    void RefreshDpiFromWindow();
    void ApplyDpiScaledFonts();
    void ApplyDpiScaledChrome();
    // Command bar + address row metrics (Explorer-measured tokens), shared by DPI and theme passes.
    void ApplyCommandBarLayout();
    // Search box width = clamp(240, 30% of the row, 435) logical; rowPx <= 0 uses the client width.
    void UpdateSearchBoxWidth(int rowPx = 0);
    void OnDpiChanged(UINT newDpi, const RECT* suggested);

    // Called by OLE drop target
    DWORD HitTestDropPath(POINT ptScreen, std::wstring& outDir) const;
    bool PerformDropTransfer(const std::vector<std::wstring>& srcPaths, const std::wstring& destDir, DWORD effect);
    HWND GetSafeHwnd() const { return m_hWnd; }
    // Call after Create()/before CenterWindow so DPI is finalized.
    void EnsureDpiLayout();
    void OnShellBrowserNavigation(std::wstring path);
    void SyncShellViewSelection();

private:
    friend struct MainWndRegressionAccess;
    static constexpr UINT kMsgCommitInlineRename = WM_APP + 0x451;
    static constexpr UINT kMsgCancelInlineRename = WM_APP + 0x452;
    static constexpr UINT kMsgShellNavigation = WM_APP + 0x453;
    static constexpr UINT kMsgShellSelection = WM_APP + 0x454;
    static constexpr UINT kMsgShellRename = WM_APP + 0x455;
    static constexpr UINT kMsgShellFolderOpen = WM_APP + 0x456;
    static constexpr UINT kMsgShellContextMenu = WM_APP + 0x457;
    bool HandleInternalFolderOpenVerb(const std::wstring& verb,const std::vector<std::wstring>& paths);
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
        Tiles,
        SmallIcons,
        Content
    };

    enum class SortColumn {
        Name = 0,
        Modified = 1,
        Type = 2,
        Size = 3
    };

    void NavigateTo(const std::wstring& path, bool addToHistory = true);
    // Real work for NavigateTo(); always runs from the deferred message, never inline
    // inside a control's own click notification.
    void NavigateToNow(const std::wstring& path, bool addToHistory);
    void RefreshListing();
    void GoUp();
    void GoBack();
    void GoForward();
    void UpdateNavButtons();
    void PushHistoryBeforeNav(const std::wstring& fromPath);
    void OnItemActivate(CControlUI* pSender);
    void UpdateStatus(LPCTSTR text);

    void OnCopyClicked();
    void OnCopyPaths();
    void OnPasteClicked();
    void OnCancelCopyClicked();
    void OnDeleteClicked(bool permanent = false);
    void OnRenameClicked();
    bool BeginInlineRename(const ClipboardItem& item);
    void CommitInlineRename();
    void CancelInlineRename();
    bool GetInlineRenameRect(const ClipboardItem& item, RECT& rect);
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
        enum class Kind { Rename, CreateFolder, Move, Copy, ShellRename, ShellDelete } kind = Kind::Rename;
        std::wstring from;   // path before the operation
        std::wstring to;     // path after the operation
        // Kind::Move only: every (source, destination) pair of that one move operation,
        // so a single Ctrl+Z restores the whole batch.
        std::vector<std::pair<std::wstring, std::wstring>> moved;
        std::vector<std::pair<std::wstring, std::wstring>> backups;
    };
    void PushUndo(UndoRecord::Kind kind, std::wstring from, std::wstring to);
    void PushHistoryRecord(UndoRecord record);
    void OnUndo();
    void OnRedo();
    bool ReplayHistory(UndoRecord& record, bool redo);
    void ClearRedoHistory();
    void TrackShellRename(WPARAM change, LPARAM process);
    void FinishShellHistory();
    void RefreshAfterHistory();
    void FocusFileView();
    void CycleKeyboardPane(bool reverse);
    void ShowAddressHistory();
    bool IsTreeKeyboardFocus() const;
    bool HandleTreeShortcut(WPARAM key);
    void ShowToolbarPopupMenu(CControlUI* anchor, HMENU hMenu);
    void UpdateFavoritesHighlight();
    // Collapsible favourites bar (toggle lives in the 查看 menu, state in session.ini).
    void SetFavoritesBarVisible(bool visible);

    // C: Horizontal favorites bar (persist %APPDATA%\FastFile\favorites.json)
    struct FavoriteItem {
        std::wstring path;
        std::wstring displayName;
    };
    static std::wstring GetFavoritesFilePath();
    void LoadFavorites();
    void SaveFavorites() const;
    void RebuildFavoritesBar();
    void RefitFavoritesChips();          // squeeze chips into the favourites row width
    void ScrollFavoritesBy(int dx);      // wheel over the favourites row
    bool PinFavorite(const std::wstring& path);
    bool UnpinFavorite(const std::wstring& path);
    bool IsFavoritePinned(const std::wstring& path) const;
    void OnPinnedFavoriteClick(CControlUI* btn);
    void ShowFavoriteContextMenu(CControlUI* btn, POINT ptScreen);
    bool IsOverFavoritesBar(POINT ptClient) const;
    // Quick access rows: the four built-ins plus the user's pinned folders live in one
    // ordered list so the order can be dragged and is persisted in quick_access.txt.
    struct QuickRow {
        bool isThisPc = false;
        bool builtIn = false;   // one of 此电脑 / 文档 / 桌面 / 下载
        std::wstring path;      // folder path (kThisPcPath for This PC)
        std::wstring label;     // display name (localized for the built-ins)
    };
    static std::wstring GetQuickAccessFilePath();
    void LoadQuickAccess();
    void SaveQuickAccess() const;
    void BuildDefaultQuickRows();
    void RebuildLeftQuickRows();
    void UpdateQuickRowHighlight();
    int HitTestQuickRow(POINT ptClient) const;
    void ActivateQuickRow(int index);
    void ShowQuickRowContextMenu(int index, POINT ptScreen);
    void MoveQuickRow(int from, int to);
    bool PinQuickAccess(const std::wstring& path);
    bool UnpinQuickAccess(const std::wstring& path);
    bool IsQuickAccessPinned(const std::wstring& path) const;
    void EnsureDefaultQuickRows();   // inserts any missing built-in row (keeps user order)
    void OpenQuickAccessTab(const std::wstring& path);

    // Search / filter
    void ApplySearchFilter();
    void ClearSearchFilter();
    void UpdateSearchOptionVisibility();   // "含子目录" follows the search box
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
    void RevealSyncedTreeNode();
    CTreeNodeUI* FindTreeNodeByPath(CTreeNodeUI* parent, const std::wstring& path) const;
    CTreeNodeUI* AddTreeFolderNode(CTreeNodeUI* parent, const std::wstring& path, const std::wstring& title);
    bool OnTreeFolderNotify(void* param);
    void OnTreeNodeActivate(CTreeNodeUI* node);

    // Tabs
    void InitTabs();
    void RebuildTabStrip();
    // Tab strip notifications (self-drawn CTabStripUI; see src/TabStripUI.h)
    void OnTabStripSelect(int index);
    void OnTabStripClose(int index);
    void OnTabStripReorder(int from, int to);
    void OnTabStripDragOut(int index, POINT screenPt);
    void OnTabStripContextMenu(int index, POINT screenPt);
    void OnTabStripAdd();
    void CloseOtherTabs(int keepIndex, bool rightSideOnly);
    void OpenPathInNewWindow(const std::wstring& path, POINT screenPt);
    // Caption buttons report HTMINBUTTON/HTMAXBUTTON/HTCLOSE, so their hover is painted here.
    void UpdateCaptionButtonHover(POINT ptClient, bool hovering);
    void ApplyDwmChrome();          // Mica Alt backdrop + frame extension (Win11 22H2+)
    // DuiLib's pre-translate pass swallows Tab (dialog navigation) before the window proc
    // sees WM_KEYDOWN, so Ctrl+Tab / Ctrl+Shift+Tab are handled in this filter instead.
    LRESULT MessageHandler(UINT uMsg, WPARAM wParam, LPARAM lParam, bool& bHandled) override;
    // Custom DuiLib controls (TabStrip)
    CControlUI* CreateControl(LPCTSTR pstrClass) override;
    // Reuse the existing tab whenever an open-folder target is already present.
    int FindTabForPath(const std::wstring& path) const;
    void AddTab(const std::wstring& path, bool activate, bool forceNew = false);
    void OnNewTabRequested();
    std::wstring NewTabTargetForSelection() const;
    void CloseTab(int index);
    void ActivateTab(int index);
    // Closing the window closes every tab at once, so ask before doing that.
    bool ConfirmCloseWithMultipleTabs();
    void UpdateActiveTabPath(const std::wstring& path);
    std::wstring TabTitleForPath(const std::wstring& path) const;
    void OpenExternalPaths(const std::vector<std::wstring>& paths, bool replaceInitialTab);
    static std::wstring ResolveFolderOpenTarget(const std::wstring& path);

    void ShowSettings();
    bool CommitSettings(FastFileSettings settings);
    bool CommitIntegrationSettings(const FastFileSettings& settings);
    void ApplySettingsAppearance();
    FastFileSettings m_settings;
    bool m_openingExternalPaths=false;

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
    void ApplyShellViewMode();
    void UpdateViewModeButtons();
    bool IsShellBrowsingCurrentPath() const;
    bool IsTileViewMode() const;
    void ApplyTileLayoutMetrics();
    void GetViewMetrics(int& tileW, int& tileH, int& iconPx, int& childPad, int& maxLabel) const;
    void RebuildDetailsView(const std::vector<DirEntry>& dirs, const std::vector<DirEntry>& files, bool truncated);
    void RebuildIconsView(const std::vector<DirEntry>& dirs, const std::vector<DirEntry>& files, bool truncated);
    void RebuildCurrentViewFromCache();
    void StoreListingCache(std::vector<DirEntry> dirs, std::vector<DirEntry> files, bool truncated);
    void UpdateListingStatusTip();
    // Shows/hides the "此文件夹为空" hint above the list (also covers "no match" when filtered).
    void UpdateEmptyStateHint();
    bool TryReuseIconsView(const std::vector<DirEntry>& dirs, const std::vector<DirEntry>& files);
    void ClearIconView();
    void ApplyTileIconImage(CControlUI* tile, const std::wstring& bmp,
        int tileW, int tileH, int iconPx, bool listMode, bool tilesMode);
    void ApplyTileText(CButtonUI* tile, const DirEntry& e, int maxLabel,
        bool listMode, bool tilesMode);
    std::wstring PeekCachedIconBmp(const std::wstring& path, bool isDir, int cx);
    std::wstring GetShellIconBmp(const std::wstring& path, bool isDir, int cx);
    // Small shell icons only (no IShellItemImageFactory thumbnails) — details/list.
    std::wstring GetShellFileIconBmp(const std::wstring& path, bool isDir, int cx);
    static bool LetterboxHBitmapToPng(HBITMAP hbm, int cx, int cy, const std::wstring& pngPath);
    static bool SaveIconToPng(HICON hIcon, const std::wstring& pngPath, int cx, int cy);
    // Icon rasterise + resample helpers (no DrawIconEx scaling: unfiltered -> jaggies)
    static bool RenderIconToArgbBuffer(HICON hIcon, int w, int h, std::vector<BYTE>& out);
    static bool ResizeArgbBuffer(const std::vector<BYTE>& src, int sw, int sh,
        int dw, int dh, std::vector<BYTE>& dst);
    static bool SaveImageThumbnailPng(const std::wstring& srcPath, const std::wstring& pngPath, int cx, int cy);
    static bool CropPngToContentAlpha(const std::wstring& pngPath);
    static bool IsImageExtension(const std::wstring& name);
    static bool IsTextExtension(const std::wstring& name);
    static bool IsVideoExtension(const std::wstring& name);
    static bool SaveHBitmapToPng(HBITMAP hbm, const std::wstring& pngPath);
    static bool EnsureGdiplus();
    static bool GetPngEncoderClsid(CLSID* pClsid);
    static void WipeDirectoryFiles(const std::wstring& dirNoSlash);
    // Persistent PNG icon cache (see MainWnd.Icons.cpp): versioned directory, stamped
    // names, background age / size trimming instead of a wipe on every launch.
    struct IconCacheTrimResult {
        int removedLegacy = 0, removedVersionDirs = 0, removedSession = 0, removedAged = 0, removedOverCap = 0;
        ULONGLONG keptBytes = 0;
    };
    static const wchar_t* const kIconCacheVersion;
    static constexpr int kIconCacheMaxAgeDays = 30;
    static constexpr ULONGLONG kIconCacheMaxBytes = 256ull * 1024 * 1024;
    static std::wstring ResolveIconCacheRoot();
    static IconCacheTrimResult MaintainIconCache(const std::wstring& root, const std::wstring& version,
        ULONGLONG sessionStart, int maxAgeDays, ULONGLONG maxBytes);
    void InitIconCache();
    std::wstring IconCacheLeaf(const std::wstring& key, const std::wstring& source, const wchar_t* tail) const;
    std::wstring SessionCacheFile(const wchar_t* leaf) const;
    // Command-bar icons: two-tone line art (grey outline + light blue accent) drawn with
    // GDI+ so the toolbar matches the Explorer command bar without shipping icon assets.
    // The icon ids live in MainWnd.Icons.cpp.
    std::wstring GetCommandIconBmp(int kind, int px, bool dim);
    // Whole-button command bitmap: icon at (iconX, iconY) plus, when chevEm > 0, the E70D chevron.
    std::wstring GetCommandCanvasBmp(int kind, int w, int h, int iconX, int iconY, int iconPx,
        float chevX, float chevCY, float chevEm, bool dim);
    static bool SaveArgbPng(const std::vector<DWORD>& pixels, int w, int h, const std::wstring& path);
    void ApplyNavButtonIcon(CControlUI* btn, wchar_t glyph);
    // Applies the command-bar bitmap for a button, picking the dimmed variant while the
    // button is disabled (Explorer greys out the commands that need a selection).
    void ApplyCommandIcon(CControlUI* btn, int kind, bool withLabel);
    // Enables/disables + repaints the selection-dependent command-bar buttons.
    void UpdateCommandBarState();
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
    // Segoe MDL2 glyph rendered to a cached PNG. Needed where a button shows an icon *and* a
    // text label: DuiLib draws a button's text with a single font, so a glyph used as text
    // would force the label into the symbol font.
    std::wstring GetGlyphIconBmp(wchar_t glyph, int px, COLORREF color);
    static bool RenderGlyphToPng(wchar_t glyph, int px, COLORREF color, const std::wstring& pngPath);
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
    void ApplyWindowIcon();
    void ApplyUiChromeTokens(); // Phase1: paddings + unified Win11 light colors
    bool TrackPopupShellMenu(IContextMenu* pMenu, HMENU hMenu, POINT ptScreen,
        UINT idCmdFirst, UINT idShellMax, bool appendHiddenToggle,
        const std::vector<std::pair<UINT, std::wstring>>* extraItems = nullptr,
        UINT* outExtraCmd = nullptr);
    // Hide shell-menu entries FastFile does not want to show (see the implementation)
    void PruneShellMenu(IContextMenu* pMenu, HMENU hMenu, UINT idCmdFirst, UINT idShellMax,
        bool backgroundMenu);
    void AddInternalFolderOpenMenu(IContextMenu* menu, HMENU popup, UINT first, UINT last,
        const std::vector<std::wstring>& paths);
    static void TidyMenuSeparators(HMENU hMenu);
    bool HandleRoutedShellVerb(const std::wstring& verb);
    HMENU CreateBackgroundViewSubmenu() const;
    HMENU CreateBackgroundSortSubmenu() const;
    bool ShellBrowserShowsFolder(const std::wstring& folderPath) const;
    bool BuildShellBackgroundMenu(const std::wstring& folderPath, IContextMenu** menu, HMENU* popup,
        UINT* shellMax, bool* fromView);
    void ReleaseRetiredShellMenus();
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
    bool LoadPreviewStockIcon(int siid, int iconPx);   // "This PC" / drive previews
    std::wstring GetShellDisplayName(const std::wstring& path) const;   // localized (图片/下载)
    // Adaptive frame + centered Fit bkimage (folders: compact icon area)
    void ApplyPreviewImageBk(const std::wstring& pngPath, int imgPxW, int imgPxH, int frameDesignH);
    // Live thumb box for the preview pane (follows the splitter width)
    void PreviewImageBox(int& boxW, int& boxH) const;
    bool RoundPreviewImage(const std::wstring& pngPath);
    // Re-fit breadcrumb / preview when the layout (not the window) changed
    void SyncLayoutDependents();
    void ReloadPreviewForWidth();
    // Rewrite a PNG on disk at the exact draw size so DuiLib never stretches it
    static bool ResamplePngToSize(const std::wstring& pngPath, int cx, int cy);
    static std::wstring FormatFileTimeLocal(const FILETIME& ft);
    static std::wstring QueryShellTypeName(const std::wstring& path, bool isDir);
    // Cached per-extension variant (tile / details 类型 column)
    std::wstring QueryShellTypeNameCached(const std::wstring& path, bool isDir);
    static std::wstring QueryImageDimensions(const std::wstring& path);
    void FillVideoPreviewMeta(const std::wstring& path);

    // Virtualized icon window + progressive details fill (E)
    void FlattenListing(std::vector<DirEntry>& out) const;
    bool EntryComesBefore(const DirEntry& a, const DirEntry& b) const;
    void BuildDisplayOrder(const std::vector<DirEntry>& dirs,
        const std::vector<DirEntry>& files, std::vector<DirEntry>& out) const;
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
    // --- Details-view virtualization ------------------------------------------------
    // The list holds only the rows for the visible window (plus overscan) and rebinds them
    // while scrolling, so a 100k-entry folder costs about as much as a 20-entry one.
    // Selection therefore lives in m_detailsSel (entry-index space) instead of on the list
    // items: item indices shift as the window moves, entry indices do not.
    void RebuildDetailsVirtual();
    void UpdateDetailsWindow(bool force);
    // Drops every cached pointer into the row pool. MUST run before anything clears the list
    // (RemoveAll) - the spacers are owned by the list and would otherwise dangle.
    void ResetDetailsVirtualState();
    void BindDetailsRow(CListContainerElementUI* row, int entryIdx);
    int DetailsEntryFromItem(CControlUI* item) const;
    void ApplyDetailsSelectionVisuals();
    void DetailsMoveCursor(int delta);
    void DetailsEnsureEntryVisible(int entryIdx);
    CListContainerElementUI* CreateDetailsRowShell();

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
    // Keyboard navigation for the tile/icon/list views (the `file_icons` TileLayout).
    // The cursor is the selected tile, so these mirror DetailsMoveCursor's Shift/Ctrl rules.
    bool IsIconViewFocused() const;
    int  IconCursorIndex() const;              // flat index of the cursor, or -1
    void IconMoveTo(int next);                 // move the cursor to a flat index
    void IconNavigate(int dCol, int dRow);     // one grid step (arrow keys)
    void IconPageMove(int dir);                // PageUp(-1) / PageDown(+1) by a screen
    void IconEnsureVisible(int flatIndex);     // scroll the tile host so the item shows
    void IconActivateCursor();                 // Enter/Space: open the cursor item

    void CollectSelectedItems(std::vector<ClipboardItem>& out) const;
    bool PublishFileClipboard(const std::vector<ClipboardItem>& items, bool cut);
    bool ReadFileClipboard(std::vector<ClipboardItem>& items, bool& cut) const;
    static DWORD DeleteOperationFlags(bool permanent);
    void DeletePaths(const std::vector<ClipboardItem>& items, bool permanent);
    bool DeleteItems(const std::vector<ClipboardItem>& items, bool permanent = false,
        std::vector<std::wstring>* completed = nullptr);
    bool RenameItem(const ClipboardItem& item, const std::wstring& newName);
    bool CreateNewFolder();

    void ShowItemContextMenu(CControlUI* pItem, POINT ptScreen);
    bool ShowShellContextMenu(const std::vector<std::wstring>& paths, POINT ptScreen,
        const std::vector<std::pair<UINT, std::wstring>>* extraItems = nullptr,
        UINT* outExtraCmd = nullptr);
    bool ShowShellBackgroundContextMenu(const std::wstring& folderPath, POINT ptScreen);
    // Builds the Shell item menu for paths (QueryContextMenu + prune + FastFile entries).
    // CMF_CANRENAME is passed for a single renamable item shown in the Shell view.
    bool BuildShellItemMenu(const std::vector<std::wstring>& paths, IContextMenu** menu, HMENU* popup,
        UINT* shellMax);
    bool CanRenameInShellView(const std::wstring& path) const;
    bool BeginShellRename(const std::wstring& path);
    void ShowFallbackContextMenu(const std::vector<ClipboardItem>& items, POINT ptScreen);
    void ShowTreeContextMenu(CTreeNodeUI* node, POINT ptScreen);
    void ShowBlankAreaContextMenu(POINT ptScreen);

    // A: visible vertical scrollbars on file views
    void StyleVerticalScrollBar(CContainerUI* host);
    void StyleHorizontalScrollBar(CContainerUI* host);
    void ApplyFileViewScrollBars();
    void StyleSidePaneScrollBars(CContainerUI* host);
    // Merged preview rail: the sidebar-width scrollbar strip that also owns the
    // left/right pane resize gesture (see IsPreviewScrollBarHit / WM_LBUTTONDOWN).
    void StylePreviewRail();
    void SyncPreviewRail();
    // Configures the Fluent bar (idle/hover thickness + which edge it hugs) when the
    // scrollbar is ours; no-op for a plain DuiLib bar.
    void ApplyFluentScrollBar(CScrollBarUI* sb, bool dockFar);
    // Expands the bar under (or just beside) the pointer and collapses the others.
    void UpdateFluentScrollBarHover(POINT clientPt);
    void CancelScrollBarGestures();
    bool PreviewRailThumbRect(RECT& out) const;
    // Shared row metrics for the Quick Access list: the four built-in rows (XML) and the
    // runtime-pinned favorites must land on exactly the same pixels.
    void ApplyQuickAccessRow(CControlUI* row, const std::wstring& iconBmp);
    int MeasureListColumnWidth(const std::vector<DirEntry>& all, int iconPx);
    int MeasureTextWidthPx(const std::wstring& text);

    // C: left Quick Access / This PC splitter persist
    static std::wstring GetLeftNavFilePath();
    void LoadLeftNavSplitter();
    void SaveLeftNavSplitter() const;
    void ApplyLeftNavSplitterHeight(int designHeight);
    void CaptureLeftNavSplitterIfChanged();
    // Generous grab band around the 快速访问 / 此电脑 boundary (DuiLib's own sep band is only
    // the last few pixels of the container, so the visible divider line was not draggable).
    bool HitTestLeftNavDivider(int clientX, int clientY) const;
    void UpdateLeftQuickAccessSpacing();
    // Sidebar / preview pane widths (design units, persisted in left_nav.ini)
    void ApplyPaneWidths(int leftDesignW, int previewDesignW);
    void CapturePaneWidthsIfChanged();
    // Divider hit testing / live drag for the sidebar (1) and preview pane (2)
    int PaneDividerBandPx() const;
    int HitTestPaneDivider(int clientX, int clientY) const;
    bool IsPaneScrollBarHit(int clientX, int clientY) const;
    bool IsPreviewScrollBarHit(int clientX, int clientY) const;
    void ApplyPaneDragWidth(int kind, int physicalWidth);

    // Drag-drop
    void InitDragDrop();
    void UninitDragDrop();
    bool BeginDragSelectedItems();
    CControlUI* HitTestFileItem(POINT ptClient) const;
    CTreeNodeUI* HitTestTreeNode(POINT ptClient) const;
    std::wstring ResolveDropDirectory(POINT ptScreen) const;
    bool TransferWithFileOperation(const std::vector<std::wstring>& srcPaths, const std::wstring& destDir,
        bool move = false);

    // Windows-native copy / move / recycle / delete (ShellFileOperation.h). Each call runs
    // IFileOperation on its own STA worker thread with the main window as owner, so the
    // Explorer progress dialog, conflict dialog and pause/cancel are shown by Windows.
    bool StartFileOperation(ShellFileOps::Kind kind, std::vector<std::wstring> sources,
        std::wstring destination = std::wstring());
    void OnFileOperationFinished(WPARAM id, LPARAM result);
    void ApplyFileOperationResult(const ShellFileOps::Result& result);
    static std::wstring DescribeFileOperation(const ShellFileOps::Result& result);
    void StopCopyThread(bool wait);
    void ApplyCopyUiState();
    static bool DeleteTreePermanent(const std::wstring& path);
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
    CControlUI* m_pListHost = nullptr;
    ShellBrowserHost* m_shellBrowser = nullptr;
    std::vector<std::wstring> m_shellSelectionSnapshot;
    HWND m_renameEdit = nullptr;
    std::wstring m_renameOriginalPath;
    bool m_renameIsDirectory = false;
    bool m_finishingInlineRename = false;
    bool m_addressEditMode = false;
    CListUI* m_pFileList = nullptr;
    CTreeViewUI* m_pDirTree = nullptr;
    CTabStripUI* m_pTabStrip = nullptr;       // self-drawn tab strip
    CHorizontalLayoutUI* m_pBreadcrumb = nullptr;
    CHorizontalLayoutUI* m_pFavoritesBar = nullptr;
    std::vector<int> m_favChipNatural;   // natural chip widths (design px, physical)
    int m_favBarFitW = 0;                // row width the chips were last fitted to
    int m_favScrollX = 0;                // horizontal chip scroll (overflow)
    int m_favScrollApplied = -1;
    CHorizontalLayoutUI* m_pFavoritesStrip = nullptr;
    CVerticalLayoutUI* m_pLeftQuickRows = nullptr;   // runtime rows (built-ins + pins)
    CVerticalLayoutUI* m_pLeftQuick = nullptr;
    CVerticalLayoutUI* m_pLeftThisPc = nullptr;
    CVerticalLayoutUI* m_pIconScroll = nullptr;
    CTileLayoutUI* m_pIconTiles = nullptr;
    int m_leftQuickDesignH = UiTokens::LeftQuickDefaultH; // @96 DPI, persisted
    int m_quickFitRows = -1;                              // rows the block was auto-fitted for
    bool m_leftNavDragging = false;
    int m_leftNavDragStartY = 0;
    int m_leftNavDragStartH = 0;
    int m_captionHot = -1;           // hovered caption button (min/max/restore/close)
    bool m_micaActive = false;       // DWM system backdrop accepted
    int m_leftPanelDesignW = 220;                         // sidebar width @96 DPI
    int m_previewPaneDesignW = UiTokens::PreviewPaneW;    // preview width @96 DPI
    int m_thisPcTilesLayoutW = 0;                         // physical central viewport width
    int m_paneDragKind = 0;          // 0 = none, 1 = sidebar, 2 = preview
    bool m_closeConfirmed = false;   // user already answered the multi-tab close prompt
    int m_paneDragStartX = 0;
    int m_paneDragStartLeft = 0;
    int m_paneDragStartPreview = 0;
    // 0 = idle, 1 = waiting for drag direction, 2 = vertical rail scrolling.
    int m_previewRailGesture = 0;
    int m_previewRailLastY = 0;
    std::vector<FavoriteItem> m_favorites;
    std::vector<QuickRow> m_quickRows;
    // Quick-access drag-to-reorder state (vertical drag moves the row under the cursor).
    int m_quickDragIndex = -1;
    bool m_quickDragActive = false;
    int m_quickDragStartY = 0;
    std::vector<std::wstring> m_shellMenuPaths;
    // preview_pane is a CHorizontalLayoutUI wrapper holding the merged scroll rail
    // (preview_rail, width grip + scrollbar along the divider) and preview_body, which
    // carries the content inset and DuiLib's own (hidden) scroll range.
    CContainerUI* m_pPreviewPane = nullptr;
    CVerticalLayoutUI* m_pPreviewBody = nullptr;
    CScrollBarUI* m_pPreviewRail = nullptr;  // scrollbar + width grip along the divider
    CContainerUI* m_pLeftPanel = nullptr;   // left_panel wrapper (right-edge drag grip)
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
    std::vector<std::wstring> m_startupOpenPaths;
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
    bool m_favoritesBarVisible = true;
    IContextMenu* m_pCtxMenu = nullptr;
    bool m_shellMenuBackground = false;       // the tracked Shell menu is a folder background menu
    std::wstring m_shellMenuFolder;           // ... for this folder
    std::vector<HMENU> m_retiredShellMenus;   // Shell submenus replaced by FastFile's 查看 / 排序方式
    IContextMenu2* m_pCtxMenu2 = nullptr;
    IContextMenu3* m_pCtxMenu3 = nullptr;
    std::wstring m_previewPath;
    bool m_previewPathIsDir = true;
    bool m_previewFromSelection = false;   // false => folder overview
    std::wstring m_previewBmp;
    unsigned m_previewSerial = 0;
    int m_previewPaneW = 0;      // last laid-out preview pane width (splitter drag)
    int m_breadcrumbFitW = 0;    // width the breadcrumb was fitted to

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
    // Virtual details view state (see RebuildDetailsVirtual)
    std::vector<DirEntry> m_detailsEntries;   // every entry, dirs first - the source of truth
    std::vector<char> m_detailsSel;           // per-entry selection flag (entry-index space)
    int m_detailsFirst = 0;                   // first entry currently bound to the row pool
    int m_detailsPoolRows = 0;
    int m_detailsCur = -1;                    // cursor for Shift ranges and keyboard moves
    int m_detailsAnchor = -1;                 // where the current selection started
    CListContainerElementUI* m_detailsSpacerTop = nullptr;
    CListContainerElementUI* m_detailsSpacerBottom = nullptr;

    std::vector<TabInfo> m_tabs;
    int m_activeTab = -1;
    bool m_updatingTabs = false;
    bool m_syncingTree = false;
    std::wstring m_treeRevealPath;
    bool m_suspendTreeSync = false;
    bool m_navigatingHistory = false;

    // Drag state
    bool m_dragTracking = false;
    POINT m_dragStartPt = {};
    IDropTarget* m_pDropTarget = nullptr;
    bool m_inDoDragDrop = false;

    std::vector<ClipboardItem> m_clipboard;
    bool m_clipboardIsCut = false;
    std::vector<UndoRecord> m_undoStack;
    std::vector<UndoRecord> m_redoStack;
    bool m_historyStarted = false;
    ULONG m_shellRenameNotify = 0;
    std::wstring m_pendingShellRename;
    std::vector<std::wstring> m_recentShellSelection;
    std::vector<std::pair<std::wstring, std::wstring>> m_appRenameNotifications;
    bool m_shellHistoryPending = false;
    bool m_shellHistoryRedo = false;
    DWORD m_shellHistoryStarted = 0;
    static constexpr UINT_PTR kTimerShellHistory = 0x7f13;

    std::map<std::wstring, std::wstring> m_iconCache;
    std::mutex m_iconCacheMutex;
    std::wstring m_iconCacheDir;      // <root>\<version>\ (trailing slash)
    std::wstring m_iconCacheRoot;
    std::wstring m_iconSessionTag;
    ULONGLONG m_iconCacheSessionStart = 0;

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

    struct FileOperationJob {
        unsigned id = 0;
        std::thread thread;
    };
    std::vector<std::unique_ptr<FileOperationJob>> m_fileOpJobs;
    unsigned m_nextFileOpId = 1;
    std::atomic<bool> m_copyCancel{false};    // aborts pending items of running operations
    std::atomic<bool> m_copyRunning{false};   // at least one Windows file operation is running
    bool m_closeAfterFileOps = false;         // WM_CLOSE deferred until the workers finish
    // Production always shows the Windows UI; only automated tests turn this off.
    bool m_fileOpsInteractive = true;
    DWORD m_lastFileOpRequestFlags = 0;
    ShellFileOps::Result m_lastFileOperation;

    static constexpr const wchar_t* kThisPcPath = L"::ThisPC";
    static constexpr const wchar_t* kPendingMarker = L"::pending";
    static constexpr const wchar_t* kFavoritePinPath = L"::FavoritePin";
    static constexpr UINT_PTR kCmdFavUnpin = 9101;
    static constexpr UINT_PTR kCmdFavOpen = 9102;
    static constexpr UINT_PTR kCmdFavOpenNewTab = 9103;
    static constexpr UINT_PTR kCmdFavOpenNewWindow = 9104;
    static constexpr UINT_PTR kCmdFavCopyPath = 9105;
    static constexpr int kMaxListItems = 8000;
    static constexpr int kMaxDetailsItems = 100000;   // details view virtualizes; icons do not
    static constexpr int kMaxIconThumbs = 400;
    static constexpr int kMaxRecursiveItems = 4000;
    static constexpr int kPumpEvery = 200;
    static constexpr UINT kMsgReactivate = WM_USER + 100;
    static constexpr UINT kMsgFileOpFinished = WM_USER + 102;   // wParam job id, lParam Result*
    static constexpr UINT kMsgPreviewIconReady = WM_APP + 0x458; // lParam PreviewIconJob* (WM_USER+103 is kMsgThumbReady)
    // Selection changes repaint the native view first; the details pane follows on this timer.
    static constexpr UINT_PTR kTimerSelectionPreview = 0x4605;
    static constexpr UINT kSelectionPreviewDelayMs = 30;
    struct PreviewIconJob {
        unsigned serial = 0;
        std::wstring path;
        std::wstring png;
        int px = 0;
        bool isDir = true;
        bool ok = false;
        std::thread::id thread;
    };
    std::vector<std::thread> m_previewIconThreads;
    bool m_selectionPreviewPending = false;
    bool m_deferSelectionPreview = true; // false = legacy synchronous pane update (timing comparison)
    bool m_asyncPreviewIcons = true;     // folder icons for the details pane load off the UI thread
    void FlushSelectionPreview();
    bool LoadPreviewShellIconAsync(const std::wstring& path, bool isDir, int iconPx);
    void OnPreviewIconReady(PreviewIconJob* job);
    void JoinPreviewIconThreads();
    int PreviewIconRequestPx(int iconPx);
    static constexpr UINT kMsgThumbReady = WM_USER + 103;
    static constexpr UINT kMsgVirtSync = WM_USER + 104;
    static constexpr UINT kMsgDetailsFill = WM_USER + 105;
    static constexpr UINT kMsgDeferredNav = WM_USER + 106;
    static constexpr UINT kMsgOpenExternalPaths = WM_USER + 107;
    static constexpr UINT kMsgExternalPathsResolved = WM_USER + 109; // lParam ExternalOpenJob*
    // External folder activations resolve their targets on a worker thread: the first
    // touch of a sleeping disk can take many seconds and must not freeze the window.
    struct ExternalOpenJob {
        std::vector<std::wstring> paths;
        std::vector<std::wstring> targets;
        bool replaceInitialTab = false;
    };
    int m_externalOpensPending = 0;
    void FinishExternalOpen(ExternalOpenJob* job);
    static DWORD ProbeFolderAttributes(const std::wstring& path);
    // Shell window registration (SHOpenFolderAndSelectItems finds FastFile).
    ShellWindowRegistration* m_shellWindow = nullptr;
    std::wstring m_shellWindowPath;
    // Set by an external open: the next location is announced as a fresh Shell window
    // registration (RegisterPending), which is what a waiting SHOpenFolderAndSelectItems
    // call listens for; a plain OnNavigate of an existing registration is not enough.
    bool m_shellWindowReannounce = false;
    bool RegisterShellWindowAt(const std::wstring& path);
    PIDLIST_ABSOLUTE m_pendingShellSelect = nullptr;
    UINT m_pendingShellSelectFlags = 0;
    ULONGLONG m_pendingShellSelectDeadline = 0;
    static constexpr UINT_PTR kTimerShellSelect = 0x4606;
    void UpdateShellWindowRegistration();
    void NotifyShellWindowLocation();
    HRESULT OnShellWindowSelect(PCIDLIST_ABSOLUTE item, UINT flags);
    HRESULT OnShellWindowNavigate(PCIDLIST_ABSOLUTE folder);
    bool TryApplyPendingShellSelect();
    void UpdateExplorerTakeover();
    void PollExplorerTakeover();
    void StopExplorerTakeover();
    std::wstring ExplorerTakeoverStatus() const;
    void ProcessExplorerSnapshots(const std::vector<ExplorerSnapshot>& snapshots,bool baseline);
    std::shared_ptr<ExplorerScanState> m_explorerScan;
    struct ExplorerTransfer {
        ExplorerSnapshot source;ULONGLONG changedAt=0,deadline=0;
        bool ignored=false,started=false,resolved=false;
    };
    std::map<HWND,ExplorerTransfer> m_explorerTransfers;
    std::map<HWND,DWORD> m_existingExplorerWindows;
    ULONGLONG m_explorerScanSequence=0;
    bool m_explorerBaseline=false;
    bool m_explorerPollBusy=false;
    static constexpr UINT_PTR kTimerExplorerTakeover=0x4607;
    // Test injection and root filter never set by the application.
    static bool (*s_closeExplorerForTest)(const ExplorerSnapshot&);
    static std::wstring s_explorerTestRoot;
    static_assert(kMsgPreviewIconReady != kMsgThumbReady && kMsgPreviewIconReady != kMsgFileOpFinished
        && kMsgPreviewIconReady != kMsgShellContextMenu, "private window messages must be unique");
    static constexpr int kDetailsVirtOverscan = 8;
    static constexpr UINT_PTR kTimerDetailsSync = 0x4603;
    static constexpr int kUiBatchSize = 40;
    static constexpr int kVirtThreshold = 220;
    static constexpr int kVirtOverscanRows = 3;
    static constexpr int kDetailsFirstBatch = 80;
    static constexpr int kDetailsFillBatch = 120;
    static constexpr int kPreviewMaxTextBytes = 96 * 1024;
    static constexpr UINT_PTR kTimerVirtSync = 0x4601;
    static constexpr UINT_PTR kTimerColWidth = 0x4602;
    static constexpr UINT_PTR kTimerLayoutSync = 0x4604;

    static constexpr UINT_PTR kCmdCtxOpen = 9001;
    static constexpr UINT_PTR kCmdCtxCopy = 9002;
    static constexpr UINT_PTR kCmdCtxDelete = 9003;
    static constexpr UINT_PTR kCmdCtxRename = 9004;
    static constexpr UINT_PTR kCmdCtxRefresh = 9005;
    static constexpr UINT_PTR kCmdShellRename = 0xFFF0; // outside the Shell command range (0x0001..0x7FFF)
    static constexpr UINT_PTR kCmdShellNewTab = 0xFFF1;
    // Commands FastFile mixes into Shell context menus live above the Shell's command range
    // (idCmdFirst 1 .. idCmdLast 0x7FFF; the view background menu really uses ids up to
    // 0x7FFE), so a Shell verb can never be mistaken for a FastFile command.
    static constexpr UINT_PTR kCmdToggleHidden = 0xFE50;
    static constexpr UINT_PTR kCmdBgRefresh = 0xFE00;
    static constexpr UINT_PTR kCmdBgPaste = 0xFE01;
    static constexpr UINT_PTR kCmdBgUndo = 0xFE02;
    static constexpr UINT_PTR kCmdBgRedo = 0xFE03;
    static constexpr UINT_PTR kCmdBgViewBase = 0xFE10;   // +0..7 -> ViewMode
    static constexpr UINT_PTR kCmdBgSortBase = 0xFE20;   // +0..3 -> SortColumn, +4 asc, +5 desc
    // FastFile entries appended below a shell context menu opened from a quick-access row.
    static constexpr UINT_PTR kCmdQuickOpen = 0xFE40;
    static constexpr UINT_PTR kCmdQuickUnpin = 0xFE41;
};
