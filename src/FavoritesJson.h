#pragma once
#include <string>
#include <vector>
#include <cwctype>

// favorites.json is an ordered JSON array of folder paths. Escape UTF-16 code units
// so the on-disk representation is ASCII (and therefore valid UTF-8).
namespace FavoritesJson {
inline std::string Encode(const std::vector<std::wstring>& paths) {
    std::string out = "[\n";
    const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < paths.size(); ++i) {
        out += "  \"";
        for (wchar_t c : paths[i]) {
            if (c == L'"' || c == L'\\') { out += '\\'; out += char(c); }
            else if (c < 32 || c > 126) {
                out += "\\u";
                for (int shift = 12; shift >= 0; shift -= 4) out += hex[(c >> shift) & 15];
            } else out += char(c);
        }
        out += i + 1 == paths.size() ? "\"\n" : "\",\n";
    }
    return out + "]\n";
}
inline bool Decode(const std::wstring& text, std::vector<std::wstring>& paths) {
    size_t p = 0;
    std::vector<std::wstring> result;
    auto space = [&]() { while (p < text.size() && iswspace(text[p])) ++p; };
    auto take = [&](wchar_t c) { space(); if (p == text.size() || text[p] != c) return false; ++p; return true; };
    if (!take(L'[')) return false;
    space();
    if (p < text.size() && text[p] != L']') {
        for (;;) {
            if (!take(L'"')) return false;
            std::wstring path;
            bool closed = false;
            while (p < text.size()) {
                wchar_t c = text[p++];
                if (c == L'"') { closed = true; break; }
                if (c < 32) return false;
                if (c == L'\\') {
                    if (p == text.size()) return false;
                    c = text[p++];
                    if (c == L'u') {
                        unsigned value = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (p == text.size()) return false;
                            wchar_t h = text[p++];
                            int n = h >= L'0' && h <= L'9' ? h - L'0' :
                                h >= L'a' && h <= L'f' ? h - L'a' + 10 :
                                h >= L'A' && h <= L'F' ? h - L'A' + 10 : -1;
                            if (n < 0) return false;
                            value = value * 16 + n;
                        }
                        c = wchar_t(value);
                    } else if (c == L'n') c = L'\n';
                    else if (c == L'r') c = L'\r';
                    else if (c == L't') c = L'\t';
                    else if (c == L'b') c = L'\b';
                    else if (c == L'f') c = L'\f';
                    else if (c != L'"' && c != L'\\' && c != L'/') return false;
                }
                if (c == 0) return false;
                path += c;
            }
            if (!closed) return false;
            result.push_back(std::move(path));
            space();
            if (p < text.size() && text[p] == L',') { ++p; continue; }
            break;
        }
    }
    if (!take(L']')) return false;
    space();
    if (p != text.size()) return false;
    paths = std::move(result);
    return true;
}
}
