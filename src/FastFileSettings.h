#pragma once
#include <string>

// Persisted preferences use 96-DPI units. Integration state is read from its own registry
// journal so editing/importing an INI file cannot silently change Windows associations.
struct FastFileSettings {
    int startup = 1; // 0 restore tabs, 1 This PC, 2 chosen folder
    std::wstring startupPath;
    bool externalNewWindow = false;
    bool reuseTabs = true;
    bool confirmClose = true;
    int density = 0;
    int navigationFont = 12;
    int tabHeight = 29;
    int favoritesHeight = 29;
    int tabWidthPercent = 150;
    int navigationScrollbar = 8;
    int defaultView = 5;
    bool rememberViews = true;
    int sortColumn = 0;
    bool sortAscending = true;
    int grouping = -1; // -1 Windows folder preference, 0 none, 1 date, 2 type
    bool contextMenu = false;
    bool defaultFolders = false;
    bool defaultComputer = false;
    bool explorerWindowTakeover = false;
    void SetDefaultManager(bool enabled) {
        contextMenu=defaultFolders=defaultComputer=explorerWindowTakeover=enabled;
    }
    void Normalize();
    static std::wstring FilePath();
    static FastFileSettings Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;
    int NavigationRowHeight() const { return 36 + density * 4; }
};
