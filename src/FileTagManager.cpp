#include "FileTagManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <objidl.h>
#include <shlobj.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

namespace {

bool EnsureTagGdiplus() {
    static std::once_flag once;
    static bool ready = false;
    std::call_once(once, [] {
        Gdiplus::GdiplusStartupInput input;
        ULONG_PTR token = 0;
        ready = (Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok);
    });
    return ready;
}

Gdiplus::Color ColorRefToGdip(COLORREF cr, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}

bool IsMountedLocalPath(const std::wstring& normPath) {
    // Only prune standard local drive paths (e.g. "c:\...") whose volume root is currently mounted.
    if (normPath.size() < 3 || normPath[1] != L':' || normPath[2] != L'\\') return false;
    const wchar_t drive = normPath[0];
    if (!((drive >= L'a' && drive <= L'z') || (drive >= L'A' && drive <= L'Z'))) return false;
    std::wstring root = normPath.substr(0, 3);
    return ::GetFileAttributesW(root.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool RenameInMap(std::unordered_map<std::wstring, FileTagInfo>& tags,
                 const std::wstring& oldNorm, const std::wstring& newNorm) {
    if (oldNorm.empty() || newNorm.empty() || oldNorm == newNorm) return false;
    std::vector<std::pair<std::wstring, FileTagInfo>> migrated;
    std::vector<std::wstring> toErase;
    const std::wstring prefix = (oldNorm.back() == L'\\') ? oldNorm : (oldNorm + L"\\");

    for (const auto& kv : tags) {
        if (kv.first == oldNorm) {
            toErase.push_back(kv.first);
            migrated.push_back({ newNorm, kv.second });
        } else if (kv.first.size() > prefix.size() && kv.first.compare(0, prefix.size(), prefix) == 0) {
            toErase.push_back(kv.first);
            const std::wstring suffix = kv.first.substr(prefix.size());
            const std::wstring target = (newNorm.back() == L'\\') ? (newNorm + suffix) : (newNorm + L"\\" + suffix);
            migrated.push_back({ target, kv.second });
        }
    }
    if (toErase.empty()) return false;
    for (const auto& key : toErase) {
        tags.erase(key);
    }
    for (auto& kv : migrated) {
        tags[kv.first] = kv.second;
    }
    return true;
}

std::string EscapeJsonString(const std::wstring& str) {
    std::string out;
    out.reserve(str.size() + 16);
    const char* hex = "0123456789abcdef";
    for (wchar_t c : str) {
        if (c == L'"' || c == L'\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32 || c > 126) {
            out += "\\u";
            for (int shift = 12; shift >= 0; shift -= 4) {
                out += hex[(c >> shift) & 15];
            }
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool ParseJsonString(const std::string& text, size_t& pos, std::wstring& out) {
    while (pos < text.size() && isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size() || text[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < text.size()) {
        char c = text[pos++];
        if (c == '"') return true;
        if (c == '\\' && pos < text.size()) {
            char next = text[pos++];
            switch (next) {
            case '"': out.push_back(L'"'); break;
            case '\\': out.push_back(L'\\'); break;
            case '/': out.push_back(L'/'); break;
            case 'b': out.push_back(L'\b'); break;
            case 'f': out.push_back(L'\f'); break;
            case 'n': out.push_back(L'\n'); break;
            case 'r': out.push_back(L'\r'); break;
            case 't': out.push_back(L'\t'); break;
            case 'u':
                if (pos + 4 <= text.size()) {
                    unsigned value = 0;
                    for (int di = 0; di < 4; ++di) {
                        char h = text[pos + di];
                        int n = (h >= '0' && h <= '9') ? (h - '0') :
                                (h >= 'a' && h <= 'f') ? (h - 'a' + 10) :
                                (h >= 'A' && h <= 'F') ? (h - 'A' + 10) : 0;
                        value = (value << 4) | n;
                    }
                    out.push_back(static_cast<wchar_t>(value));
                    pos += 4;
                }
                break;
            default:
                out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(next)));
                break;
            }
        } else {
            out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
    }
    return false;
}

void SkipJsonValue(const std::string& text, size_t& pos) {
    while (pos < text.size() && isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos >= text.size()) return;
    char c = text[pos];
    if (c == '"') {
        std::wstring dummy;
        ParseJsonString(text, pos, dummy);
    } else if (c == '{' || c == '[') {
        const char open = c;
        const char close = (c == '{') ? '}' : ']';
        int depth = 1;
        ++pos;
        while (pos < text.size() && depth > 0) {
            if (text[pos] == '"') {
                std::wstring dummy;
                ParseJsonString(text, pos, dummy);
            } else {
                if (text[pos] == open) ++depth;
                else if (text[pos] == close) --depth;
                ++pos;
            }
        }
    } else {
        while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']'
            && !isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }
}

} // namespace

FileTagManager& FileTagManager::Instance() {
    static FileTagManager s_instance;
    return s_instance;
}

FileTagManager::FileTagManager() {
    Load();
}

std::wstring FileTagManager::Normalize(const std::wstring& path) {
    if (path.empty()) return path;
    std::wstring res = path;
    // Trim spaces
    while (!res.empty() && (res.front() == L' ' || res.front() == L'\t')) res.erase(res.begin());
    while (!res.empty() && (res.back() == L' ' || res.back() == L'\t')) res.pop_back();
    if (res.empty()) return res;
    // Normalize slashes
    for (wchar_t& c : res) {
        if (c == L'/') c = L'\\';
    }
    // Remove redundant trailing slash if not root like "C:\"
    while (res.size() > 3 && res.back() == L'\\') {
        res.pop_back();
    }
    // Windows file paths are case-insensitive: normalize with invariant lowercase.
    const int needed = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        res.c_str(), static_cast<int>(res.size()), nullptr, 0, nullptr, nullptr, 0);
    if (needed > 0) {
        std::wstring lower(static_cast<size_t>(needed), L'\0');
        if (::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
            res.c_str(), static_cast<int>(res.size()), &lower[0], needed, nullptr, nullptr, 0) > 0) {
            return lower;
        }
    }
    for (wchar_t& c : res) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return res;
}

std::wstring FileTagManager::GetTagFilePath() {
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? appdata : L".";
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\file_tags.json";
}

std::wstring FileTagManager::GetTagNamesFilePath() {
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? appdata : L".";
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\tag_names.ini";
}

COLORREF FileTagManager::GetColorRef(FileTagColor color) {
    switch (color) {
    case FileTagColor::Red:    return RGB(255, 77, 79);   // #FF4D4F
    case FileTagColor::Orange: return RGB(255, 122, 69);  // #FF7A45
    case FileTagColor::Yellow: return RGB(255, 197, 61);  // #FFC53D
    case FileTagColor::Green:  return RGB(82, 196, 26);   // #52C41A
    case FileTagColor::Blue:   return RGB(24, 144, 255);  // #1890FF
    case FileTagColor::Purple: return RGB(114, 46, 209);  // #722ED1
    default:                   return CLR_INVALID;
    }
}

const wchar_t* FileTagManager::GetColorName(FileTagColor color) {
    switch (color) {
    case FileTagColor::Red:    return L"红色";
    case FileTagColor::Orange: return L"橙色";
    case FileTagColor::Yellow: return L"黄色";
    case FileTagColor::Green:  return L"绿色";
    case FileTagColor::Blue:   return L"蓝色";
    case FileTagColor::Purple: return L"紫色";
    default:                   return L"无";
    }
}

bool FileTagManager::IsTagViewPath(const std::wstring& path) {
    FileTagColor color = FileTagColor::None;
    bool starredOnly = false;
    return ParseTagViewPath(path, color, starredOnly);
}

bool FileTagManager::ParseTagViewPath(const std::wstring& path, FileTagColor& outColor, bool& outStarredOnly) {
    outColor = FileTagColor::None;
    outStarredOnly = false;
    if (path.empty()) return false;
    if (::_wcsicmp(path.c_str(), L"::Tags") == 0) return true;
    if (::_wcsicmp(path.c_str(), L"::Tags:Starred") == 0) {
        outStarredOnly = true;
        return true;
    }
    if (::_wcsicmp(path.c_str(), L"::Tags:Red") == 0)    { outColor = FileTagColor::Red; return true; }
    if (::_wcsicmp(path.c_str(), L"::Tags:Orange") == 0) { outColor = FileTagColor::Orange; return true; }
    if (::_wcsicmp(path.c_str(), L"::Tags:Yellow") == 0) { outColor = FileTagColor::Yellow; return true; }
    if (::_wcsicmp(path.c_str(), L"::Tags:Green") == 0)  { outColor = FileTagColor::Green; return true; }
    if (::_wcsicmp(path.c_str(), L"::Tags:Blue") == 0)   { outColor = FileTagColor::Blue; return true; }
    if (::_wcsicmp(path.c_str(), L"::Tags:Purple") == 0) { outColor = FileTagColor::Purple; return true; }
    return false;
}

std::wstring FileTagManager::TagViewDisplayName(const std::wstring& path) {
    FileTagColor color = FileTagColor::None;
    bool starredOnly = false;
    if (!ParseTagViewPath(path, color, starredOnly)) return path;
    if (starredOnly) return L"★ 星标项目";
    if (color != FileTagColor::None) return Instance().GetDisplayName(color) + L"标签";
    return L"所有已标记项目";
}

std::unordered_map<std::wstring, FileTagInfo> FileTagManager::ParseTagsJson(const std::string& text) {
    std::unordered_map<std::wstring, FileTagInfo> parsed;
    size_t pos = 0;
    auto skipWs = [&]() {
        while (pos < text.size() && isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    };

    // Locate the "tags" array in the top-level object.
    skipWs();
    if (pos >= text.size() || text[pos] != '{') return parsed;
    ++pos;
    while (pos < text.size()) {
        skipWs();
        if (pos >= text.size() || text[pos] == '}') break;
        std::wstring key;
        if (!ParseJsonString(text, pos, key)) break;
        skipWs();
        if (pos >= text.size() || text[pos] != ':') break;
        ++pos;
        skipWs();
        if (key == L"tags" && pos < text.size() && text[pos] == '[') {
            ++pos;
            while (pos < text.size()) {
                skipWs();
                if (pos >= text.size() || text[pos] == ']') {
                    if (pos < text.size()) ++pos;
                    break;
                }
                if (text[pos] == '{') {
                    ++pos;
                    std::wstring rawPath;
                    int colorVal = 0;
                    bool starredVal = false;
                    while (pos < text.size()) {
                        skipWs();
                        if (pos >= text.size() || text[pos] == '}') {
                            if (pos < text.size()) ++pos;
                            break;
                        }
                        std::wstring field;
                        if (!ParseJsonString(text, pos, field)) break;
                        skipWs();
                        if (pos >= text.size() || text[pos] != ':') break;
                        ++pos;
                        skipWs();
                        if (field == L"path") {
                            ParseJsonString(text, pos, rawPath);
                        } else if (field == L"color") {
                            size_t start = pos;
                            if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) ++pos;
                            while (pos < text.size() && isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
                            if (pos > start) {
                                colorVal = std::atoi(text.substr(start, pos - start).c_str());
                            } else {
                                SkipJsonValue(text, pos);
                            }
                        } else if (field == L"starred") {
                            if (text.compare(pos, 4, "true") == 0) {
                                starredVal = true;
                                pos += 4;
                            } else if (text.compare(pos, 5, "false") == 0) {
                                starredVal = false;
                                pos += 5;
                            } else {
                                SkipJsonValue(text, pos);
                            }
                        } else {
                            SkipJsonValue(text, pos);
                        }
                        skipWs();
                        if (pos < text.size() && text[pos] == ',') ++pos;
                    }

                    std::wstring norm = Normalize(rawPath);
                    if (!norm.empty()) {
                        FileTagInfo info;
                        info.color = static_cast<FileTagColor>(std::clamp(colorVal, 0, 6));
                        info.starred = starredVal;
                        if (info.color != FileTagColor::None || info.starred) {
                            parsed[norm] = info;
                        }
                    }
                } else {
                    SkipJsonValue(text, pos);
                }
                skipWs();
                if (pos < text.size() && text[pos] == ',') ++pos;
            }
        } else {
            SkipJsonValue(text, pos);
        }
        skipWs();
        if (pos < text.size() && text[pos] == ',') ++pos;
    }
    return parsed;
}

std::string FileTagManager::SerializeTagsJson(const std::unordered_map<std::wstring, FileTagInfo>& tags) {
    std::string text = "{\n  \"tags\": [\n";
    bool first = true;
    for (const auto& pair : tags) {
        if (pair.second.color == FileTagColor::None && !pair.second.starred) continue;
        if (!first) text += ",\n";
        first = false;
        text += "    {\n";
        text += "      \"path\": \"" + EscapeJsonString(pair.first) + "\",\n";
        text += "      \"color\": " + std::to_string(static_cast<int>(pair.second.color)) + ",\n";
        text += "      \"starred\": " + std::string(pair.second.starred ? "true" : "false") + "\n";
        text += "    }";
    }
    text += "\n  ]\n}\n";
    return text;
}

void FileTagManager::Load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tags.clear();
    m_ancestorSummary.clear();
    m_loaded = true;
    LoadCustomNamesLocked();
    const std::wstring filePath = GetTagFilePath();
    HANDLE file = ::CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    DWORD size = ::GetFileSize(file, nullptr);
    if (size == 0 || size == INVALID_FILE_SIZE) {
        ::CloseHandle(file);
        return;
    }

    std::string text(size, '\0');
    DWORD read = 0;
    if (!::ReadFile(file, &text[0], size, &read, nullptr) || read == 0) {
        ::CloseHandle(file);
        return;
    }
    ::CloseHandle(file);
    text.resize(read);

    m_tags = ParseTagsJson(text);
    RebuildAncestorIndexLocked();
}

bool FileTagManager::SaveLocked() const {
    const std::wstring filePath = GetTagFilePath();
    const std::string text = SerializeTagsJson(m_tags);

    const std::wstring tempPath = filePath + L".tmp-" + std::to_wstring(::GetCurrentProcessId());
    HANDLE file = ::CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    bool ok = ::WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)
        && written == text.size() && ::FlushFileBuffers(file);
    ::CloseHandle(file);

    if (ok) {
        ok = ::MoveFileExW(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok) {
        ::DeleteFileW(tempPath.c_str());
    }
    return ok;
}

bool FileTagManager::Save() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return SaveLocked();
}

FileTagInfo FileTagManager::GetTag(const std::wstring& path) const {
    const std::wstring norm = Normalize(path);
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tags.find(norm);
    if (it != m_tags.end()) return it->second;
    return {};
}

void FileTagManager::SetColor(const std::wstring& path, FileTagColor color) {
    SetColorBatch({ path }, color);
}

void FileTagManager::SetColorBatch(const std::vector<std::wstring>& paths, FileTagColor color) {
    if (paths.empty()) return;
    bool syncIcons = false;
    std::vector<std::wstring> syncPaths;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        syncIcons = m_syncFolderIcons;
        bool changed = false;
        for (const auto& path : paths) {
            std::wstring norm = Normalize(path);
            if (norm.empty()) continue;
            if (color == FileTagColor::None) {
                auto it = m_tags.find(norm);
                if (it != m_tags.end()) {
                    it->second.color = FileTagColor::None;
                    if (!it->second.starred) {
                        m_tags.erase(it);
                    }
                    changed = true;
                    if (syncIcons) syncPaths.push_back(path);
                }
            } else {
                auto& entry = m_tags[norm];
                if (entry.color != color) {
                    entry.color = color;
                    changed = true;
                    if (syncIcons) syncPaths.push_back(path);
                }
            }
        }
        if (changed) {
            RebuildAncestorIndexLocked();
            SaveLocked();
        }
    }
    if (syncIcons) {
        for (const auto& p : syncPaths) {
            SyncFolderIcon(p, color);
        }
    }
}

void FileTagManager::SetStarred(const std::wstring& path, bool starred) {
    std::wstring norm = Normalize(path);
    if (norm.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!starred) {
        auto it = m_tags.find(norm);
        if (it != m_tags.end()) {
            it->second.starred = false;
            if (it->second.color == FileTagColor::None) {
                m_tags.erase(it);
            }
            RebuildAncestorIndexLocked();
            SaveLocked();
        }
    } else {
        auto& entry = m_tags[norm];
        if (!entry.starred) {
            entry.starred = true;
            RebuildAncestorIndexLocked();
            SaveLocked();
        }
    }
}

void FileTagManager::ToggleStarred(const std::wstring& path) {
    ToggleStarredBatch({ path });
}

void FileTagManager::ToggleStarredBatch(const std::vector<std::wstring>& paths) {
    if (paths.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    bool changed = false;
    for (const auto& path : paths) {
        std::wstring norm = Normalize(path);
        if (norm.empty()) continue;
        auto it = m_tags.find(norm);
        const bool next = !((it != m_tags.end()) ? it->second.starred : false);
        if (!next) {
            if (it != m_tags.end()) {
                it->second.starred = false;
                if (it->second.color == FileTagColor::None) {
                    m_tags.erase(it);
                }
                changed = true;
            }
        } else {
            m_tags[norm].starred = true;
            changed = true;
        }
    }
    if (changed) {
        RebuildAncestorIndexLocked();
        SaveLocked();
    }
}

void FileTagManager::ClearTag(const std::wstring& path) {
    std::wstring norm = Normalize(path);
    if (norm.empty()) return;
    bool shouldClearIcon = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_tags.find(norm);
        if (it != m_tags.end()) {
            shouldClearIcon = m_syncFolderIcons && (it->second.color != FileTagColor::None);
            m_tags.erase(it);
            RebuildAncestorIndexLocked();
            SaveLocked();
        }
    }
    if (shouldClearIcon) {
        SyncFolderIcon(path, FileTagColor::None);
    }
}

bool FileTagManager::RenamePath(const std::wstring& oldPath, const std::wstring& newPath) {
    const std::wstring oldNorm = Normalize(oldPath);
    const std::wstring newNorm = Normalize(newPath);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!RenameInMap(m_tags, oldNorm, newNorm)) return false;
    RebuildAncestorIndexLocked();
    SaveLocked();
    return true;
}

size_t FileTagManager::RenamePathsBatch(const std::vector<std::pair<std::wstring, std::wstring>>& moves) {
    if (moves.empty()) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t movedCount = 0;
    for (const auto& pair : moves) {
        if (RenameInMap(m_tags, Normalize(pair.first), Normalize(pair.second))) {
            ++movedCount;
        }
    }
    if (movedCount > 0) {
        RebuildAncestorIndexLocked();
        SaveLocked();
    }
    return movedCount;
}

size_t FileTagManager::PruneMissingPaths() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::wstring> toErase;
    for (const auto& kv : m_tags) {
        if (!IsMountedLocalPath(kv.first)) continue;
        const DWORD attrs = ::GetFileAttributesW(kv.first.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
                toErase.push_back(kv.first);
            }
        }
    }
    if (toErase.empty()) return 0;
    for (const auto& key : toErase) {
        m_tags.erase(key);
    }
    RebuildAncestorIndexLocked();
    SaveLocked();
    return toErase.size();
}

std::vector<std::pair<std::wstring, FileTagInfo>> FileTagManager::QueryTags(
    FileTagColor colorFilter, bool starredOnly) const {
    std::vector<std::pair<std::wstring, FileTagInfo>> results;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        results.reserve(m_tags.size());
        for (const auto& kv : m_tags) {
            if (kv.second.color == FileTagColor::None && !kv.second.starred) continue;
            if (starredOnly && !kv.second.starred) continue;
            if (colorFilter != FileTagColor::None && kv.second.color != colorFilter) continue;
            results.push_back(kv);
        }
    }
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        if (a.second.starred != b.second.starred) return a.second.starred && !b.second.starred;
        return a.first < b.first;
    });
    return results;
}

void FileTagManager::DrawTagDot(HDC dc, int cx, int cy, int radius, COLORREF fill, COLORREF border) {
    if (!dc || radius <= 0) return;
    if (EnsureTagGdiplus()) {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(cx - radius);
        const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(cy - radius);
        const Gdiplus::REAL d = static_cast<Gdiplus::REAL>(radius * 2);
        Gdiplus::SolidBrush brush(ColorRefToGdip(fill));
        Gdiplus::Pen pen(ColorRefToGdip(border), 1.0f);
        g.FillEllipse(&brush, x, y, d, d);
        g.DrawEllipse(&pen, x, y, d, d);
        return;
    }
    HGDIOBJ oldBr = ::SelectObject(dc, ::GetStockObject(DC_BRUSH));
    HGDIOBJ oldPen = ::SelectObject(dc, ::GetStockObject(DC_PEN));
    const COLORREF oldBrColor = ::SetDCBrushColor(dc, fill);
    const COLORREF oldPenColor = ::SetDCPenColor(dc, border);
    ::Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    ::SetDCPenColor(dc, oldPenColor);
    ::SetDCBrushColor(dc, oldBrColor);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBr);
}

void FileTagManager::DrawStar(HDC dc, int cx, int cy, int outerR, COLORREF fill, COLORREF border) {
    if (!dc || outerR <= 0) return;
    const double pi = 3.14159265358979323846;
    const double innerR = outerR * 0.45;
    if (EnsureTagGdiplus()) {
        Gdiplus::PointF pts[10];
        for (int i = 0; i < 10; ++i) {
            const double r = (i % 2 == 0) ? outerR : innerR;
            const double angle = -pi / 2.0 + i * (pi / 5.0);
            pts[i].X = static_cast<Gdiplus::REAL>(cx + r * std::cos(angle));
            pts[i].Y = static_cast<Gdiplus::REAL>(cy + r * std::sin(angle));
        }
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        Gdiplus::SolidBrush brush(ColorRefToGdip(fill));
        Gdiplus::Pen pen(ColorRefToGdip(border), 1.0f);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        g.FillPolygon(&brush, pts, 10);
        g.DrawPolygon(&pen, pts, 10);
        return;
    }
    POINT pts[10];
    for (int i = 0; i < 10; ++i) {
        double r = (i % 2 == 0) ? outerR : innerR;
        double angle = -pi / 2.0 + i * (pi / 5.0);
        pts[i].x = cx + static_cast<int>(std::round(r * std::cos(angle)));
        pts[i].y = cy + static_cast<int>(std::round(r * std::sin(angle)));
    }
    HGDIOBJ oldBr = ::SelectObject(dc, ::GetStockObject(DC_BRUSH));
    HGDIOBJ oldPen = ::SelectObject(dc, ::GetStockObject(DC_PEN));
    const COLORREF oldBrColor = ::SetDCBrushColor(dc, fill);
    const COLORREF oldPenColor = ::SetDCPenColor(dc, border);
    ::Polygon(dc, pts, 10);
    ::SetDCPenColor(dc, oldPenColor);
    ::SetDCBrushColor(dc, oldBrColor);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBr);
}

void FileTagManager::RebuildAncestorIndexLocked() {
    m_ancestorSummary.clear();
    for (const auto& kv : m_tags) {
        if (kv.second.color == FileTagColor::None && !kv.second.starred) continue;
        std::wstring cur = kv.first;
        while (true) {
            const size_t slash = cur.find_last_of(L'\\');
            if (slash == std::wstring::npos) break;
            if (slash == 2 && cur.size() >= 3 && cur[1] == L':') {
                // Stop above drive root (don't index "c:\")
                break;
            }
            cur.resize(slash);
            if (cur.size() <= 2) break;
            auto& summary = m_ancestorSummary[cur];
            ++summary.count;
            if (kv.second.starred) ++summary.starredCount;
            if (summary.firstColor == FileTagColor::None && kv.second.color != FileTagColor::None) {
                summary.firstColor = kv.second.color;
            }
        }
    }
}

DescendantTagSummary FileTagManager::GetDescendantSummary(const std::wstring& folderPath) const {
    const std::wstring norm = Normalize(folderPath);
    if (norm.empty()) return {};
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_ancestorSummary.find(norm);
    if (it != m_ancestorSummary.end()) return it->second;
    return {};
}

void FileTagManager::LoadCustomNamesLocked() {
    for (auto& n : m_customNames) n.clear();
    const std::wstring iniPath = GetTagNamesFilePath();
    for (int i = 1; i <= 6; ++i) {
        wchar_t key[16]{};
        swprintf_s(key, L"Color%d", i);
        wchar_t buf[64]{};
        ::GetPrivateProfileStringW(L"TagNames", key, L"", buf, _countof(buf), iniPath.c_str());
        std::wstring name = buf;
        while (!name.empty() && (name.front() == L' ' || name.front() == L'\t')) name.erase(name.begin());
        while (!name.empty() && (name.back() == L' ' || name.back() == L'\t')) name.pop_back();
        if (name.size() > 24) name.resize(24);
        m_customNames[static_cast<size_t>(i)] = std::move(name);
    }
}

void FileTagManager::SaveCustomNamesLocked() const {
    const std::wstring iniPath = GetTagNamesFilePath();
    for (int i = 1; i <= 6; ++i) {
        wchar_t key[16]{};
        swprintf_s(key, L"Color%d", i);
        const auto& name = m_customNames[static_cast<size_t>(i)];
        ::WritePrivateProfileStringW(L"TagNames", key, name.empty() ? nullptr : name.c_str(), iniPath.c_str());
    }
}

std::wstring FileTagManager::GetDisplayName(FileTagColor color) const {
    const int idx = static_cast<int>(color);
    if (idx >= 1 && idx <= 6) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_customNames[static_cast<size_t>(idx)].empty()) {
            return m_customNames[static_cast<size_t>(idx)];
        }
    }
    return GetColorName(color);
}

void FileTagManager::SetCustomColorName(FileTagColor color, const std::wstring& name) {
    const int idx = static_cast<int>(color);
    if (idx < 1 || idx > 6) return;
    std::wstring cleaned = name;
    while (!cleaned.empty() && (cleaned.front() == L' ' || cleaned.front() == L'\t')) cleaned.erase(cleaned.begin());
    while (!cleaned.empty() && (cleaned.back() == L' ' || cleaned.back() == L'\t')) cleaned.pop_back();
    if (cleaned.size() > 24) cleaned.resize(24);
    if (cleaned == GetColorName(color)) cleaned.clear();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_customNames[static_cast<size_t>(idx)] = std::move(cleaned);
    SaveCustomNamesLocked();
}

void FileTagManager::SetSyncFolderIcons(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_syncFolderIcons = enabled;
}

bool FileTagManager::IsSyncFolderIconsEnabled() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_syncFolderIcons;
}

void FileTagManager::SyncAllFolderIcons(bool enabled) {
    std::vector<std::pair<std::wstring, FileTagColor>> folders;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_syncFolderIcons = enabled;
        for (const auto& kv : m_tags) {
            if (kv.second.color != FileTagColor::None) {
                folders.push_back({ kv.first, kv.second.color });
            }
        }
    }
    for (const auto& pair : folders) {
        SyncFolderIcon(pair.first, enabled ? pair.second : FileTagColor::None);
    }
}

namespace {

bool FindPngEncoderClsid(CLSID* outClsid) {
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return false;
    std::vector<BYTE> buf(size);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < num; ++i) {
        if (info[i].MimeType && wcscmp(info[i].MimeType, L"image/png") == 0) {
            *outClsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

void AddRoundRectPath(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y,
                      Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL r) {
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

Gdiplus::Color ShiftColor(COLORREF base, int delta) {
    const int r = std::clamp(static_cast<int>(GetRValue(base)) + delta, 0, 255);
    const int g = std::clamp(static_cast<int>(GetGValue(base)) + delta, 0, 255);
    const int b = std::clamp(static_cast<int>(GetBValue(base)) + delta, 0, 255);
    return Gdiplus::Color(255, static_cast<BYTE>(r), static_cast<BYTE>(g), static_cast<BYTE>(b));
}

} // namespace

std::wstring FileTagManager::EnsureColorFolderIconFile(FileTagColor color) {
    const int idx = static_cast<int>(color);
    if (idx < 1 || idx > 6) return {};
    static const wchar_t* kNames[7] = { L"", L"red", L"orange", L"yellow", L"green", L"blue", L"purple" };
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? appdata : L".";
    dir += L"\\FastFile";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\icons";
    ::CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring icoPath = dir + L"\\folder_" + kNames[idx] + L".ico";
    if (::GetFileAttributesW(icoPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return icoPath;
    }
    if (!EnsureTagGdiplus()) return {};
    CLSID pngClsid{};
    if (!FindPngEncoderClsid(&pngClsid)) return {};

    constexpr int kSize = 128;
    Gdiplus::Bitmap bmp(kSize, kSize, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));

        const COLORREF base = GetColorRef(color);
        // Back folder plate with top-left tab
        Gdiplus::GraphicsPath backPath;
        backPath.AddArc(8.0f, 18.0f, 16.0f, 16.0f, 180.0f, 90.0f);
        backPath.AddLine(16.0f, 18.0f, 48.0f, 18.0f);
        backPath.AddLine(48.0f, 18.0f, 60.0f, 30.0f);
        backPath.AddLine(60.0f, 30.0f, 112.0f, 30.0f);
        backPath.AddArc(104.0f, 30.0f, 16.0f, 16.0f, 270.0f, 90.0f);
        backPath.AddArc(104.0f, 94.0f, 16.0f, 16.0f, 0.0f, 90.0f);
        backPath.AddArc(8.0f, 94.0f, 16.0f, 16.0f, 90.0f, 90.0f);
        backPath.CloseFigure();
        Gdiplus::SolidBrush backBrush(ShiftColor(base, -36));
        g.FillPath(&backBrush, &backPath);

        // Front folder body with subtle gradient
        Gdiplus::GraphicsPath frontPath;
        AddRoundRectPath(frontPath, 8.0f, 38.0f, 112.0f, 72.0f, 9.0f);
        Gdiplus::LinearGradientBrush frontBrush(
            Gdiplus::PointF(8.0f, 38.0f), Gdiplus::PointF(8.0f, 110.0f),
            ShiftColor(base, 18), ShiftColor(base, -18));
        g.FillPath(&frontBrush, &frontPath);

        // Soft top highlight line
        Gdiplus::Pen hiPen(Gdiplus::Color(90, 255, 255, 255), 2.0f);
        g.DrawLine(&hiPen, 16.0f, 40.0f, 112.0f, 40.0f);
    }

    IStream* stream = nullptr;
    if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || !stream) return {};
    if (bmp.Save(stream, &pngClsid, nullptr) != Gdiplus::Ok) {
        stream->Release();
        return {};
    }
    HGLOBAL hMem = nullptr;
    if (FAILED(::GetHGlobalFromStream(stream, &hMem)) || !hMem) {
        stream->Release();
        return {};
    }
    const SIZE_T pngBytes = ::GlobalSize(hMem);
    void* pngPtr = ::GlobalLock(hMem);
    bool writtenOk = false;
    if (pngPtr && pngBytes > 0 && pngBytes <= 1024u * 1024u) {
        std::vector<BYTE> ico(22 + pngBytes, 0);
        // ICONDIR (6 bytes)
        ico[2] = 1; // type = ICO
        ico[4] = 1; // count = 1
        // ICONDIRENTRY (16 bytes)
        ico[6] = static_cast<BYTE>(kSize);
        ico[7] = static_cast<BYTE>(kSize);
        ico[10] = 1; // planes
        ico[12] = 32; // bpp
        const DWORD sz = static_cast<DWORD>(pngBytes);
        memcpy(&ico[14], &sz, sizeof(sz));
        const DWORD offset = 22;
        memcpy(&ico[18], &offset, sizeof(offset));
        memcpy(&ico[22], pngPtr, pngBytes);

        HANDLE f = ::CreateFileW(icoPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            writtenOk = ::WriteFile(f, ico.data(), static_cast<DWORD>(ico.size()), &written, nullptr)
                && written == ico.size();
            ::CloseHandle(f);
        }
    }
    if (pngPtr) ::GlobalUnlock(hMem);
    stream->Release();
    return writtenOk ? icoPath : std::wstring{};
}

bool FileTagManager::SyncFolderIcon(const std::wstring& folderPath, FileTagColor color) {
    if (folderPath.size() <= 3) return false; // Never touch drive roots like "C:\"
    const DWORD attrs = ::GetFileAttributesW(folderPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;

    if (color == FileTagColor::None) {
        // Only clear desktop.ini icon if it points to FastFile's own icon directory.
        const std::wstring desktopIni = folderPath + L"\\desktop.ini";
        if (::GetFileAttributesW(desktopIni.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
        wchar_t existing[MAX_PATH * 2]{};
        ::GetPrivateProfileStringW(L".ShellClassInfo", L"IconResource", L"", existing, _countof(existing), desktopIni.c_str());
        std::wstring lower = Normalize(existing);
        if (lower.find(L"\\fastfile\\icons\\folder_") == std::wstring::npos) return false;

        SHFOLDERCUSTOMSETTINGS fcs{};
        fcs.dwSize = sizeof(fcs);
        fcs.dwMask = FCSM_ICONFILE;
        fcs.pszIconFile = nullptr;
        fcs.cchIconFile = 0;
        fcs.iIconIndex = 0;
        const HRESULT hr = ::SHGetSetFolderCustomSettings(&fcs, folderPath.c_str(), FCS_FORCEWRITE);
        ::SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, folderPath.c_str(), nullptr);
        return SUCCEEDED(hr);
    }

    const std::wstring iconFile = EnsureColorFolderIconFile(color);
    if (iconFile.empty()) return false;
    SHFOLDERCUSTOMSETTINGS fcs{};
    fcs.dwSize = sizeof(fcs);
    fcs.dwMask = FCSM_ICONFILE;
    fcs.pszIconFile = const_cast<LPWSTR>(iconFile.c_str());
    fcs.cchIconFile = 0;
    fcs.iIconIndex = 0;
    const HRESULT hr = ::SHGetSetFolderCustomSettings(&fcs, folderPath.c_str(), FCS_FORCEWRITE);
    if (SUCCEEDED(hr)) {
        ::SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, folderPath.c_str(), nullptr);
    }
    return SUCCEEDED(hr);
}

void FileTagManager::DrawDescendantRing(HDC dc, int cx, int cy, int radius, COLORREF color) {
    if (!dc || radius <= 0) return;
    if (EnsureTagGdiplus()) {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(cx - radius);
        const Gdiplus::REAL y = static_cast<Gdiplus::REAL>(cy - radius);
        const Gdiplus::REAL d = static_cast<Gdiplus::REAL>(radius * 2);
        Gdiplus::Pen pen(ColorRefToGdip(color, 210), 1.6f);
        g.DrawEllipse(&pen, x, y, d, d);
        return;
    }
    HGDIOBJ oldBr = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
    HGDIOBJ oldPen = ::SelectObject(dc, ::GetStockObject(DC_PEN));
    const COLORREF oldPenColor = ::SetDCPenColor(dc, color);
    ::Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    ::SetDCPenColor(dc, oldPenColor);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBr);
}

int FileTagManager::DrawTagPill(HDC dc, int x, int cy, int maxRight, HFONT font, COLORREF color,
                                const std::wstring& label, bool darkMode, int dpi) {
    if (!dc || label.empty() || x >= maxRight - 16) return 0;
    const int saved = ::SaveDC(dc);
    if (font) ::SelectObject(dc, font);
    SIZE textSz{};
    ::GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &textSz);
    const int padX = MulDiv(6, dpi, 96);
    const int dotR = MulDiv(3, dpi, 96);
    const int dotGap = MulDiv(4, dpi, 96);
    const int pillH = MulDiv(17, dpi, 96);
    const int pillW = padX + dotR * 2 + dotGap + textSz.cx + padX;
    if (x + pillW > maxRight) {
        ::RestoreDC(dc, saved);
        return 0;
    }
    const int top = cy - pillH / 2;
    if (EnsureTagGdiplus()) {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        AddRoundRectPath(path, static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(top),
            static_cast<Gdiplus::REAL>(pillW), static_cast<Gdiplus::REAL>(pillH),
            static_cast<Gdiplus::REAL>(pillH) / 2.0f);
        Gdiplus::SolidBrush bgBrush(ColorRefToGdip(color, darkMode ? 52 : 34));
        Gdiplus::Pen borderPen(ColorRefToGdip(color, darkMode ? 120 : 95), 1.0f);
        g.FillPath(&bgBrush, &path);
        g.DrawPath(&borderPen, &path);
        Gdiplus::SolidBrush dotBrush(ColorRefToGdip(color, 255));
        g.FillEllipse(&dotBrush,
            static_cast<Gdiplus::REAL>(x + padX),
            static_cast<Gdiplus::REAL>(cy - dotR),
            static_cast<Gdiplus::REAL>(dotR * 2),
            static_cast<Gdiplus::REAL>(dotR * 2));
    } else {
        DrawTagDot(dc, x + padX + dotR, cy, dotR, color);
    }
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, darkMode ? RGB(0xEE, 0xEE, 0xEE) : RGB(0x2B, 0x2B, 0x2B));
    RECT textRc{ x + padX + dotR * 2 + dotGap, top, x + pillW - padX, top + pillH };
    ::DrawTextW(dc, label.c_str(), -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    ::RestoreDC(dc, saved);
    return pillW;
}

HBITMAP FileTagManager::CreateMenuColorBitmap(COLORREF color, int size) {
    if (size <= 0) size = 16;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screenDc = ::GetDC(nullptr);
    HBITMAP hbm = ::CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, screenDc);
    if (!hbm || !bits) {
        if (hbm) ::DeleteObject(hbm);
        return nullptr;
    }
    memset(bits, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    if (EnsureTagGdiplus()) {
        Gdiplus::Bitmap gdipBmp(size, size, size * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&gdipBmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        const Gdiplus::REAL pad = static_cast<Gdiplus::REAL>(size) * 0.18f;
        const Gdiplus::REAL d = static_cast<Gdiplus::REAL>(size) - pad * 2.0f;
        Gdiplus::SolidBrush br(ColorRefToGdip(color, 255));
        Gdiplus::Pen pen(Gdiplus::Color(90, 0, 0, 0), 1.0f);
        g.FillEllipse(&br, pad, pad, d, d);
        g.DrawEllipse(&pen, pad, pad, d, d);
    }
    return hbm;
}
