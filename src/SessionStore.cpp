#include "SessionStore.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <map>

namespace FastFileSession {

namespace {

bool ReadUtf16LeFile(const std::wstring& filePath, std::wstring& outContent) {
    outContent.clear();
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, filePath.c_str(), L"rb") != 0 || !fp) return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 4) {
        fclose(fp);
        return false;
    }
    outContent.resize(static_cast<size_t>(sz) / sizeof(wchar_t));
    const size_t bytesRead = fread(&outContent[0], 1, static_cast<size_t>(sz), fp);
    fclose(fp);
    outContent.resize(bytesRead / sizeof(wchar_t));
    if (!outContent.empty() && outContent[0] == 0xFEFF) {
        outContent.erase(outContent.begin());
    }
    return !outContent.empty();
}

bool WriteUtf16LeFileAtomic(const std::wstring& filePath, const std::wstring& content) {
    const std::wstring tempPath = filePath + L".tmp-" + std::to_wstring(::GetCurrentProcessId());
    HANDLE hFile = ::CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    const unsigned char bom[2] = { 0xFF, 0xFE };
    DWORD written = 0;
    bool ok = ::WriteFile(hFile, bom, 2, &written, nullptr) && written == 2;
    if (ok && !content.empty()) {
        const DWORD bytes = static_cast<DWORD>(content.size() * sizeof(wchar_t));
        ok = ::WriteFile(hFile, content.data(), bytes, &written, nullptr) && written == bytes;
    }
    if (ok) {
        ok = ::FlushFileBuffers(hFile) != FALSE;
    }
    ::CloseHandle(hFile);

    if (ok) {
        ok = ::MoveFileExW(tempPath.c_str(), filePath.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) {
        ::DeleteFileW(tempPath.c_str());
    }
    return ok;
}

std::wstring NormalizeBasicPath(const std::wstring& path) {
    std::wstring s = path;
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
    for (auto& ch : s) {
        if (ch == L'/') ch = L'\\';
    }
    while (s.size() > 3 && s.back() == L'\\') {
        s.pop_back();
    }
    if (s.size() == 2 && s[1] == L':') {
        s.push_back(L'\\');
    }
    return s;
}

} // namespace

std::wstring ConfigDirectory() {
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(appdata) : std::wstring(L".");
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring SessionFilePath() {
    return ConfigDirectory() + L"\\session.ini";
}

std::wstring FolderViewsFilePath() {
    return ConfigDirectory() + L"\\folder_views.ini";
}

std::wstring NormalizeViewKey(const std::wstring& path, const wchar_t* thisPcSentinel) {
    if (path.empty()) return {};
    if ((thisPcSentinel && ::_wcsicmp(path.c_str(), thisPcSentinel) == 0)
        || path == L"此电脑"
        || ::_wcsicmp(path.c_str(), L"This PC") == 0) {
        return thisPcSentinel ? std::wstring(thisPcSentinel) : std::wstring(L"::ThisPC");
    }
    std::wstring key = NormalizeBasicPath(path);
    if (key.empty()) return {};
    for (auto& ch : key) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return key;
}

int LoadFolderView(const std::wstring& filePath, const std::wstring& normalizedKey,
                   int defaultMode, int maxValidMode) {
    if (normalizedKey.empty()) return defaultMode;
    std::wstring content;
    if (!ReadUtf16LeFile(filePath, content)) return defaultMode;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = content.size();
        std::wstring line = content.substr(pos, eol - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        pos = eol + 1;
        if (line.empty() || line[0] == L'[' || line[0] == L';') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = line.substr(0, eq);
        std::wstring v = line.substr(eq + 1);
        if (::_wcsicmp(k.c_str(), normalizedKey.c_str()) != 0) continue;
        int mode = _wtoi(v.c_str());
        if (mode >= 0 && mode <= maxValidMode) return mode;
        break;
    }
    return defaultMode;
}

bool SaveFolderView(const std::wstring& filePath, const std::wstring& normalizedKey,
                    int mode, int maxValidMode) {
    if (normalizedKey.empty() || mode < 0 || mode > maxValidMode) return false;
    std::map<std::wstring, int> entries;

    std::wstring content;
    if (ReadUtf16LeFile(filePath, content)) {
        size_t pos = 0;
        while (pos < content.size()) {
            size_t eol = content.find(L'\n', pos);
            if (eol == std::wstring::npos) eol = content.size();
            std::wstring line = content.substr(pos, eol - pos);
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            pos = eol + 1;
            if (line.empty() || line[0] == L'[' || line[0] == L';') continue;
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring k = line.substr(0, eq);
            int v = _wtoi(line.substr(eq + 1).c_str());
            if (!k.empty() && v >= 0 && v <= maxValidMode) {
                entries[k] = v;
            }
        }
    }

    entries[normalizedKey] = mode;

    std::wstring out = L"[FolderViews]\n";
    for (const auto& kv : entries) {
        wchar_t buf[32]{};
        swprintf_s(buf, L"=%d\n", kv.second);
        out += kv.first;
        out += buf;
    }
    return WriteUtf16LeFileAtomic(filePath, out);
}

bool LoadSession(const std::wstring& filePath, SessionData& outData) {
    std::wstring content;
    if (!ReadUtf16LeFile(filePath, content)) return false;

    int count = 0;
    std::map<int, std::wstring> paths;
    std::map<int, std::wstring> filters;
    std::map<int, bool> pinned;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = content.size();
        std::wstring line = content.substr(pos, eol - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        pos = eol + 1;
        if (line.empty() || line[0] == L'[' || line[0] == L';') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = line.substr(0, eq);
        std::wstring val = line.substr(eq + 1);
        if (key == L"ActiveTab") outData.activeTab = _wtoi(val.c_str());
        else if (key == L"ViewMode") outData.viewMode = _wtoi(val.c_str());
        else if (key == L"Recursive") outData.recursive = (_wtoi(val.c_str()) != 0);
        else if (key == L"ColName") outData.colWidthName = (std::max)(60, _wtoi(val.c_str()));
        else if (key == L"ColMTime") outData.colWidthMTime = (std::max)(50, _wtoi(val.c_str()));
        else if (key == L"ColType") outData.colWidthType = (std::max)(50, _wtoi(val.c_str()));
        else if (key == L"ColSize") outData.colWidthSize = (std::max)(50, _wtoi(val.c_str()));
        else if (key == L"SortCol") {
            int sc = _wtoi(val.c_str());
            if (sc >= 0 && sc <= 3) outData.sortColumn = sc;
        }
        else if (key == L"SortAsc") outData.sortAscending = (_wtoi(val.c_str()) != 0);
        else if (key == L"Preview") outData.previewVisible = (_wtoi(val.c_str()) != 0);
        else if (key == L"ShowHidden") outData.showHidden = (_wtoi(val.c_str()) != 0);
        else if (key == L"FavoritesBar") outData.favoritesBarVisible = (_wtoi(val.c_str()) != 0);
        else if (key == L"Count") count = _wtoi(val.c_str());
        else if (key.size() > 4 && key.compare(0, 4, L"Path") == 0)
            paths[_wtoi(key.c_str() + 4)] = val;
        else if (key.size() > 6 && key.compare(0, 6, L"Filter") == 0)
            filters[_wtoi(key.c_str() + 6)] = val;
        else if (key.size() > 6 && key.compare(0, 6, L"Pinned") == 0)
            pinned[_wtoi(key.c_str() + 6)] = (_wtoi(val.c_str()) != 0);
    }

    if (count <= 0 || paths.empty()) return false;

    outData.tabs.clear();
    outData.tabs.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto it = paths.find(i);
        if (it == paths.end() || it->second.empty()) continue;
        TabRecord rec;
        rec.path = it->second;
        auto fit = filters.find(i);
        if (fit != filters.end()) rec.searchFilter = fit->second;
        auto pit = pinned.find(i);
        if (pit != pinned.end()) rec.isPinned = pit->second;
        outData.tabs.push_back(std::move(rec));
    }
    return !outData.tabs.empty();
}

bool SaveSession(const std::wstring& filePath, const SessionData& data) {
    std::wstring out;
    out.reserve(512 + data.tabs.size() * 128);
    auto appendLine = [&](const std::wstring& line) {
        out += line;
        out.push_back(L'\n');
    };

    appendLine(L"[Session]");
    wchar_t buf[128]{};
    swprintf_s(buf, L"ActiveTab=%d", data.activeTab); appendLine(buf);
    swprintf_s(buf, L"ViewMode=%d", data.viewMode); appendLine(buf);
    swprintf_s(buf, L"Recursive=%d", data.recursive ? 1 : 0); appendLine(buf);
    swprintf_s(buf, L"ColName=%d", data.colWidthName); appendLine(buf);
    swprintf_s(buf, L"ColMTime=%d", data.colWidthMTime); appendLine(buf);
    swprintf_s(buf, L"ColType=%d", data.colWidthType); appendLine(buf);
    swprintf_s(buf, L"ColSize=%d", data.colWidthSize); appendLine(buf);
    swprintf_s(buf, L"SortCol=%d", data.sortColumn); appendLine(buf);
    swprintf_s(buf, L"SortAsc=%d", data.sortAscending ? 1 : 0); appendLine(buf);
    swprintf_s(buf, L"Preview=%d", data.previewVisible ? 1 : 0); appendLine(buf);
    swprintf_s(buf, L"ShowHidden=%d", data.showHidden ? 1 : 0); appendLine(buf);
    swprintf_s(buf, L"FavoritesBar=%d", data.favoritesBarVisible ? 1 : 0); appendLine(buf);

    appendLine(L"[Tabs]");
    swprintf_s(buf, L"Count=%d", static_cast<int>(data.tabs.size())); appendLine(buf);
    for (int i = 0; i < static_cast<int>(data.tabs.size()); ++i) {
        wchar_t key[64]{};
        swprintf_s(key, L"Path%d=", i);
        appendLine(std::wstring(key) + data.tabs[i].path);
        swprintf_s(key, L"Filter%d=", i);
        appendLine(std::wstring(key) + data.tabs[i].searchFilter);
        swprintf_s(key, L"Pinned%d=", i);
        appendLine(std::wstring(key) + (data.tabs[i].isPinned ? L"1" : L"0"));
    }
    return WriteUtf16LeFileAtomic(filePath, out);
}

} // namespace FastFileSession
