#pragma once
#include <array>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <windows.h>

enum class FileTagColor : uint8_t {
    None = 0,
    Red = 1,     // #FF4D4F 重要/紧急
    Orange = 2,  // #FF7A45 进行中
    Yellow = 3,  // #FFC53D 待办/关注
    Green = 4,   // #52C41A 已完成/已审
    Blue = 5,    // #1890FF 工作/项目
    Purple = 6   // #722ED1 创意/归档
};

struct FileTagInfo {
    FileTagColor color = FileTagColor::None;
    bool starred = false;
};

struct DescendantTagSummary {
    int count = 0;
    int starredCount = 0;
    FileTagColor firstColor = FileTagColor::None;
};

class FileTagManager {
public:
    static FileTagManager& Instance();

    void Load();
    bool Save() const;

    FileTagInfo GetTag(const std::wstring& path) const;
    DescendantTagSummary GetDescendantSummary(const std::wstring& folderPath) const;
    void SetColor(const std::wstring& path, FileTagColor color);
    void SetColorBatch(const std::vector<std::wstring>& paths, FileTagColor color);
    void SetStarred(const std::wstring& path, bool starred);
    void ToggleStarred(const std::wstring& path);
    void ToggleStarredBatch(const std::vector<std::wstring>& paths);
    void ClearTag(const std::wstring& path);
    // Migrate tags when a file or folder is renamed or moved (including nested children).
    bool RenamePath(const std::wstring& oldPath, const std::wstring& newPath);
    size_t RenamePathsBatch(const std::vector<std::pair<std::wstring, std::wstring>>& moves);
    // Remove entries whose files/folders no longer exist on mounted local drives.
    size_t PruneMissingPaths();
    // Thread-safe snapshot query for cross-folder tag/star aggregation views.
    std::vector<std::pair<std::wstring, FileTagInfo>> QueryTags(
        FileTagColor colorFilter = FileTagColor::None, bool starredOnly = false) const;

    // Custom display names for tag colors (persisted in tag_names.ini; empty resets to default).
    std::wstring GetDisplayName(FileTagColor color) const;
    void SetCustomColorName(FileTagColor color, const std::wstring& name);

    // Optional Windows Explorer folder icon sync via desktop.ini / SHGetSetFolderCustomSettings.
    void SetSyncFolderIcons(bool enabled);
    bool IsSyncFolderIconsEnabled() const;
    void SyncAllFolderIcons(bool enabled);
    static std::wstring EnsureColorFolderIconFile(FileTagColor color);
    static bool SyncFolderIcon(const std::wstring& folderPath, FileTagColor color);

    static COLORREF GetColorRef(FileTagColor color);
    static const wchar_t* GetColorName(FileTagColor color);
    static bool IsTagViewPath(const std::wstring& path);
    static bool ParseTagViewPath(const std::wstring& path, FileTagColor& outColor, bool& outStarredOnly);
    static std::wstring TagViewDisplayName(const std::wstring& path);

    const std::unordered_map<std::wstring, FileTagInfo>& GetAll() const { return m_tags; }

    static std::wstring Normalize(const std::wstring& path);
    static std::unordered_map<std::wstring, FileTagInfo> ParseTagsJson(const std::string& text);
    static std::string SerializeTagsJson(const std::unordered_map<std::wstring, FileTagInfo>& tags);

    static void DrawTagDot(HDC dc, int cx, int cy, int radius, COLORREF fill, COLORREF border = RGB(255, 255, 255));
    static void DrawDescendantRing(HDC dc, int cx, int cy, int radius, COLORREF color);
    static void DrawStar(HDC dc, int cx, int cy, int outerR, COLORREF fill = RGB(255, 184, 0), COLORREF border = RGB(212, 155, 0));
    static int DrawTagPill(HDC dc, int x, int cy, int maxRight, HFONT font, COLORREF color,
        const std::wstring& label, bool darkMode, int dpi);
    static HBITMAP CreateMenuColorBitmap(COLORREF color, int size = 16);

private:
    FileTagManager();
    static std::wstring GetTagFilePath();
    static std::wstring GetTagNamesFilePath();
    bool SaveLocked() const;
    void LoadCustomNamesLocked();
    void SaveCustomNamesLocked() const;
    void RebuildAncestorIndexLocked();

    mutable std::mutex m_mutex;
    std::unordered_map<std::wstring, FileTagInfo> m_tags;
    std::unordered_map<std::wstring, DescendantTagSummary> m_ancestorSummary;
    std::array<std::wstring, 7> m_customNames;
    bool m_syncFolderIcons = true;
    bool m_loaded = false;
};
