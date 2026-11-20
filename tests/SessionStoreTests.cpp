#include "SessionStore.h"
#include "UndoHistory.h"
#include <windows.h>
#include <cassert>
#include <iostream>
#include <string>

static std::wstring MakeTempFile(const wchar_t* name) {
    wchar_t tempDir[MAX_PATH]{};
    DWORD n = ::GetTempPathW(_countof(tempDir), tempDir);
    assert(n > 0 && n < _countof(tempDir));
    return std::wstring(tempDir) + L"FastFileModTest_" + std::to_wstring(::GetCurrentProcessId()) + L"_" + name;
}

static void TestNormalizeViewKey() {
    using namespace FastFileSession;
    assert(NormalizeViewKey(L"", L"::ThisPC").empty());
    assert(NormalizeViewKey(L"::ThisPC", L"::ThisPC") == L"::ThisPC");
    assert(NormalizeViewKey(L"此电脑", L"::ThisPC") == L"::ThisPC");
    assert(NormalizeViewKey(L"this pc", L"::ThisPC") == L"::ThisPC");
    assert(NormalizeViewKey(L"  C:/Users/Test/  ", L"::ThisPC") == L"c:\\users\\test");
    assert(NormalizeViewKey(L"D:", L"::ThisPC") == L"d:\\");
    std::cout << "TestNormalizeViewKey passed.\n";
}

static void TestFolderViewsPersistence() {
    using namespace FastFileSession;
    const std::wstring path = MakeTempFile(L"folder_views.ini");
    ::DeleteFileW(path.c_str());

    // Missing file returns default
    assert(LoadFolderView(path, L"c:\\docs", 2, 5) == 2);

    // Save and reload multiple keys (including Unicode and root)
    assert(SaveFolderView(path, L"c:\\docs\\中文目录", 4, 5));
    assert(SaveFolderView(path, L"d:\\", 0, 5));
    assert(LoadFolderView(path, L"C:\\DOCS\\中文目录", 1, 5) == 4);
    assert(LoadFolderView(path, L"D:\\", 1, 5) == 0);
    assert(LoadFolderView(path, L"c:\\other", 3, 5) == 3);

    // Overwrite existing key and reject out-of-range modes
    assert(SaveFolderView(path, L"c:\\docs\\中文目录", 2, 5));
    assert(LoadFolderView(path, L"c:\\docs\\中文目录", 1, 5) == 2);
    assert(!SaveFolderView(path, L"c:\\docs\\中文目录", 99, 5));
    assert(LoadFolderView(path, L"c:\\docs\\中文目录", 1, 5) == 2);

    ::DeleteFileW(path.c_str());
    std::cout << "TestFolderViewsPersistence passed.\n";
}

static void TestSessionPersistence() {
    using namespace FastFileSession;
    const std::wstring path = MakeTempFile(L"session.ini");
    ::DeleteFileW(path.c_str());

    SessionData out;
    out.activeTab = 1;
    out.viewMode = 3;
    out.recursive = true;
    out.colWidthName = 420;
    out.colWidthMTime = 160;
    out.colWidthType = 120;
    out.colWidthSize = 130;
    out.sortColumn = 2;
    out.sortAscending = false;
    out.previewVisible = false;
    out.showHidden = true;
    out.favoritesBarVisible = false;
    out.tabs.push_back({ L"C:\\Work\\项目A", L"", true });
    out.tabs.push_back({ L"D:\\Downloads", L"*.pdf", false });

    assert(SaveSession(path, out));

    SessionData loaded;
    assert(LoadSession(path, loaded));
    assert(loaded.activeTab == 1);
    assert(loaded.viewMode == 3);
    assert(loaded.recursive == true);
    assert(loaded.colWidthName == 420);
    assert(loaded.colWidthMTime == 160);
    assert(loaded.colWidthType == 120);
    assert(loaded.colWidthSize == 130);
    assert(loaded.sortColumn == 2);
    assert(loaded.sortAscending == false);
    assert(loaded.previewVisible == false);
    assert(loaded.showHidden == true);
    assert(loaded.favoritesBarVisible == false);
    assert(loaded.tabs.size() == 2);
    assert(loaded.tabs[0].path == L"C:\\Work\\项目A" && loaded.tabs[0].isPinned);
    assert(loaded.tabs[1].path == L"D:\\Downloads" && loaded.tabs[1].searchFilter == L"*.pdf" && !loaded.tabs[1].isPinned);

    // Column width lower-bound clamping
    SessionData tinyCols = out;
    tinyCols.colWidthName = 10;
    tinyCols.colWidthSize = 5;
    assert(SaveSession(path, tinyCols));
    SessionData clamped;
    assert(LoadSession(path, clamped));
    assert(clamped.colWidthName == 60);
    assert(clamped.colWidthSize == 50);

    ::DeleteFileW(path.c_str());
    std::cout << "TestSessionPersistence passed.\n";
}

static void TestUndoHistory() {
    UndoHistory h(3);
    assert(!h.CanUndo() && !h.CanRedo());

    h.PushRename(L"C:\\a.txt", L"C:\\b.txt");
    h.PushCopy({ { L"C:\\src.txt", L"C:\\dst.txt" } });
    assert(h.UndoCount() == 2 && !h.CanRedo());

    UndoHistory::Entry e;
    assert(h.PopUndo(e));
    assert(e.kind == UndoHistory::Kind::CopyFiles && e.copiedDests.size() == 1 && e.copiedDests[0] == L"C:\\dst.txt");
    h.PushRedoEntry(std::move(e));
    assert(h.CanRedo() && h.UndoCount() == 1);

    // New operation clears redo stack
    h.PushRename(L"C:\\c.txt", L"C:\\d.txt");
    assert(!h.CanRedo() && h.UndoCount() == 2);

    // Max capacity trimming (maxEntries = 3)
    h.PushRename(L"C:\\e.txt", L"C:\\f.txt");
    h.PushRename(L"C:\\g.txt", L"C:\\h.txt");
    assert(h.UndoCount() == 3);
    assert(h.PopUndo(e) && e.newPath == L"C:\\h.txt");

    h.Clear();
    assert(!h.CanUndo() && !h.CanRedo());
    std::cout << "TestUndoHistory passed.\n";
}

int main() {
    TestNormalizeViewKey();
    TestFolderViewsPersistence();
    TestSessionPersistence();
    TestUndoHistory();
    std::cout << "All SessionStore & UndoHistory tests passed!\n";
    return 0;
}
