#include "FileTagManager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

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
    m_loaded = true;
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
    std::lock_guard<std::mutex> lock(m_mutex);
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
            }
        } else {
            auto& entry = m_tags[norm];
            if (entry.color != color) {
                entry.color = color;
                changed = true;
            }
        }
    }
    if (changed) {
        SaveLocked();
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
            SaveLocked();
        }
    } else {
        auto& entry = m_tags[norm];
        if (!entry.starred) {
            entry.starred = true;
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
        SaveLocked();
    }
}

void FileTagManager::ClearTag(const std::wstring& path) {
    std::wstring norm = Normalize(path);
    if (norm.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_tags.find(norm);
    if (it != m_tags.end()) {
        m_tags.erase(it);
        SaveLocked();
    }
}

bool FileTagManager::RenamePath(const std::wstring& oldPath, const std::wstring& newPath) {
    const std::wstring oldNorm = Normalize(oldPath);
    const std::wstring newNorm = Normalize(newPath);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!RenameInMap(m_tags, oldNorm, newNorm)) return false;
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
    SaveLocked();
    return toErase.size();
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
