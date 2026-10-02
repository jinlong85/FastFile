#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace FastFileCore {

inline bool IsValidLeafName(const std::wstring& name)
{
    return !name.empty()
        && name != L"."
        && name != L".."
        && name.find_first_of(L"\\/") == std::wstring::npos;
}

inline std::wstring ParentPath(const std::wstring& path)
{
    if (path.empty())
        return {};
    if (path.size() == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        return {};

    std::wstring parent = path;
    while (!parent.empty() && (parent.back() == L'\\' || parent.back() == L'/'))
        parent.pop_back();
    const size_t separator = parent.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return {};
    if (separator == 2 && parent[1] == L':')
        return parent.substr(0, 3);
    return parent.substr(0, separator);
}

inline std::wstring GetLeafName(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return path;
    return path.substr(separator + 1);
}

inline size_t RenameSelectionEnd(const std::wstring& name, bool isDirectory)
{
    if (isDirectory)
        return name.size();
    const size_t dot = name.find_last_of(L'.');
    return dot != std::wstring::npos && dot > 0 ? dot : name.size();
}

inline std::wstring FormatFileSize(std::uint64_t bytes)
{
    std::wostringstream out;
    if (bytes < 1024ULL) {
        out << bytes << L" B";
    } else if (bytes < 1024ULL * 1024ULL) {
        out << std::fixed << std::setprecision(1) << (bytes / 1024.0) << L" KB";
    } else if (bytes < 1024ULL * 1024ULL * 1024ULL) {
        out << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << L" MB";
    } else {
        out << std::fixed << std::setprecision(2)
            << (bytes / (1024.0 * 1024.0 * 1024.0)) << L" GB";
    }
    return out.str();
}

} // namespace FastFileCore
