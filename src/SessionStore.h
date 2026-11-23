#pragma once
#include <string>
#include <vector>

namespace FastFileSession {

struct TabRecord {
    std::wstring path;
    std::wstring searchFilter;
    bool isPinned = false;
};

struct SessionData {
    int activeTab = 0;
    int viewMode = 1; // ViewMode::Tiles
    bool recursive = false;
    int colWidthName = 360;
    int colWidthMTime = 140;
    int colWidthType = 100;
    int colWidthSize = 110;
    int sortColumn = 0;
    bool sortAscending = true;
    bool previewVisible = true;
    bool showHidden = false;
    bool favoritesBarVisible = true;
    std::vector<TabRecord> tabs;
};

// Return (and ensure) "%APPDATA%\FastFile".
std::wstring ConfigDirectory();
std::wstring SessionFilePath();
std::wstring FolderViewsFilePath();

// Normalize a folder path into a case-insensitive lookup key for folder_views.ini.
std::wstring NormalizeViewKey(const std::wstring& path, const wchar_t* thisPcSentinel);

// Load/save per-folder view mode from a UTF-16 LE INI file.
int LoadFolderView(const std::wstring& filePath, const std::wstring& normalizedKey,
                   int defaultMode, int maxValidMode);
bool SaveFolderView(const std::wstring& filePath, const std::wstring& normalizedKey,
                    int mode, int maxValidMode);

// Load/save tab session state from a UTF-16 LE INI file (atomic write on save).
bool LoadSession(const std::wstring& filePath, SessionData& outData);
bool SaveSession(const std::wstring& filePath, const SessionData& data);

} // namespace FastFileSession
