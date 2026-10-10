#include "FileTagManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>
#include <cmath>

namespace {

std::string EscapeJsonString(const std::wstring& str) {
    std::string out;
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
    // Normalize slashes
    for (wchar_t& c : res) {
        if (c == L'/') c = L'\\';
    }
    // Remove redundant trailing slash if not root like "C:\"
    if (res.size() > 3 && res.back() == L'\\') {
        res.pop_back();
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

void FileTagManager::Load() {
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

    // Simple JSON parse for "path", "color", "starred"
    size_t pos = 0;
    while (pos < text.size()) {
        size_t pathPos = text.find("\"path\":", pos);
        if (pathPos == std::string::npos) break;
        size_t quote1 = text.find('"', pathPos + 7);
        if (quote1 == std::string::npos) break;
        size_t quote2 = text.find('"', quote1 + 1);
        while (quote2 != std::string::npos && text[quote2 - 1] == '\\') {
            quote2 = text.find('"', quote2 + 1);
        }
        if (quote2 == std::string::npos) break;

        std::string rawPath = text.substr(quote1 + 1, quote2 - quote1 - 1);
        // Unescape path
        std::wstring unescapedPath;
        for (size_t k = 0; k < rawPath.size(); ++k) {
            if (rawPath[k] == '\\' && k + 1 < rawPath.size()) {
                char next = rawPath[k + 1];
                if (next == '\\' || next == '"') {
                    unescapedPath.push_back(next);
                    ++k;
                } else if (next == 'u' && k + 5 < rawPath.size()) {
                    unsigned value = 0;
                    for (int di = 0; di < 4; ++di) {
                        char h = rawPath[k + 2 + di];
                        int n = (h >= '0' && h <= '9') ? (h - '0') :
                                (h >= 'a' && h <= 'f') ? (h - 'a' + 10) :
                                (h >= 'A' && h <= 'F') ? (h - 'A' + 10) : 0;
                        value = (value << 4) | n;
                    }
                    unescapedPath.push_back(static_cast<wchar_t>(value));
                    k += 5;
                } else {
                    unescapedPath.push_back(next);
                    ++k;
                }
            } else {
                unescapedPath.push_back(static_cast<wchar_t>(static_cast<unsigned char>(rawPath[k])));
            }
        }

        // Find color and starred
        size_t objEnd = text.find('}', quote2);
        if (objEnd == std::string::npos) objEnd = text.size();
        std::string objSnippet = text.substr(quote2, objEnd - quote2);

        int colorVal = 0;
        size_t colorPos = objSnippet.find("\"color\":");
        if (colorPos != std::string::npos) {
            colorVal = std::atoi(objSnippet.c_str() + colorPos + 8);
        }

        bool starredVal = false;
        size_t starredPos = objSnippet.find("\"starred\":");
        if (starredPos != std::string::npos) {
            size_t valPos = starredPos + 10;
            while (valPos < objSnippet.size() && isspace(static_cast<unsigned char>(objSnippet[valPos]))) ++valPos;
            if (objSnippet.compare(valPos, 4, "true") == 0) starredVal = true;
        }

        std::wstring norm = Normalize(unescapedPath);
        if (!norm.empty()) {
            FileTagInfo info;
            info.color = static_cast<FileTagColor>(std::clamp(colorVal, 0, 6));
            info.starred = starredVal;
            if (info.color != FileTagColor::None || info.starred) {
                m_tags[norm] = info;
            }
        }

        pos = objEnd + 1;
    }
}

bool FileTagManager::Save() const {
    const std::wstring filePath = GetTagFilePath();
    std::string text = "{\n  \"tags\": [\n";
    bool first = true;
    for (const auto& pair : m_tags) {
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

FileTagInfo FileTagManager::GetTag(const std::wstring& path) const {
    std::wstring norm = Normalize(path);
    auto it = m_tags.find(norm);
    if (it != m_tags.end()) return it->second;
    return {};
}

void FileTagManager::SetColor(const std::wstring& path, FileTagColor color) {
    std::wstring norm = Normalize(path);
    if (norm.empty()) return;
    if (color == FileTagColor::None) {
        auto it = m_tags.find(norm);
        if (it != m_tags.end()) {
            it->second.color = FileTagColor::None;
            if (!it->second.starred) {
                m_tags.erase(it);
            }
        }
    } else {
        m_tags[norm].color = color;
    }
    Save();
}

void FileTagManager::SetStarred(const std::wstring& path, bool starred) {
    std::wstring norm = Normalize(path);
    if (norm.empty()) return;
    if (!starred) {
        auto it = m_tags.find(norm);
        if (it != m_tags.end()) {
            it->second.starred = false;
            if (it->second.color == FileTagColor::None) {
                m_tags.erase(it);
            }
        }
    } else {
        m_tags[norm].starred = true;
    }
    Save();
}

void FileTagManager::ToggleStarred(const std::wstring& path) {
    std::wstring norm = Normalize(path);
    if (norm.empty()) return;
    auto it = m_tags.find(norm);
    bool current = (it != m_tags.end()) ? it->second.starred : false;
    SetStarred(norm, !current);
}

void FileTagManager::ClearTag(const std::wstring& path) {
    std::wstring norm = Normalize(path);
    if (norm.empty()) return;
    auto it = m_tags.find(norm);
    if (it != m_tags.end()) {
        m_tags.erase(it);
        Save();
    }
}

void FileTagManager::DrawTagDot(HDC dc, int cx, int cy, int radius, COLORREF fill, COLORREF border) {
    if (!dc || radius <= 0) return;
    HBRUSH br = ::CreateSolidBrush(fill);
    HPEN pen = ::CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBr = ::SelectObject(dc, br);
    HGDIOBJ oldPen = ::SelectObject(dc, pen);
    ::Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBr);
    ::DeleteObject(pen);
    ::DeleteObject(br);
}

void FileTagManager::DrawStar(HDC dc, int cx, int cy, int outerR, COLORREF fill, COLORREF border) {
    if (!dc || outerR <= 0) return;
    POINT pts[10];
    const double pi = 3.14159265358979323846;
    const double innerR = outerR * 0.45;
    for (int i = 0; i < 10; ++i) {
        double r = (i % 2 == 0) ? outerR : innerR;
        double angle = -pi / 2.0 + i * (pi / 5.0);
        pts[i].x = cx + static_cast<int>(std::round(r * std::cos(angle)));
        pts[i].y = cy + static_cast<int>(std::round(r * std::sin(angle)));
    }
    HBRUSH br = ::CreateSolidBrush(fill);
    HPEN pen = ::CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBr = ::SelectObject(dc, br);
    HGDIOBJ oldPen = ::SelectObject(dc, pen);
    ::Polygon(dc, pts, 10);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBr);
    ::DeleteObject(pen);
    ::DeleteObject(br);
}
