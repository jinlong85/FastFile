#pragma once
#include <Windows.h>
#include <shlobj.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>

namespace ShellPresentation {
// Resolve the effective Windows association, including per-user/packaged
// handlers, without reading or modifying protected UserChoice registry data.
inline std::wstring DefaultFileAssociation(const std::wstring& path) {
    const wchar_t* extension=PathFindExtensionW(path.c_str());
    if(!extension || !*extension)return {};
    IApplicationAssociationRegistration* registration=nullptr;
    PWSTR progid=nullptr;std::wstring result;
    if(SUCCEEDED(CoCreateInstance(CLSID_ApplicationAssociationRegistration,nullptr,
        CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&registration)))) {
        if(SUCCEEDED(registration->QueryCurrentDefault(extension,AT_FILEEXTENSION,AL_EFFECTIVE,&progid)) && progid)
            result=progid;
        CoTaskMemFree(progid);registration->Release();
    }
    return result;
}
inline bool OpenDefaultFile(HWND owner, const std::wstring& path,
    BOOL (WINAPI *execute)(SHELLEXECUTEINFOW*) = ShellExecuteExW) {
    const std::wstring association=DefaultFileAssociation(path);
    SHELLEXECUTEINFOW info{};
    info.cbSize=sizeof(info); info.hwnd=owner; info.lpFile=path.c_str();
    info.fMask=SEE_MASK_NOASYNC; info.nShow=SW_SHOWNORMAL;
    if(!association.empty()) {info.lpClass=association.c_str();info.fMask|=SEE_MASK_CLASSNAME;}
    return execute(&info)!=FALSE;
}
inline std::wstring LocalizedTypeName(const std::wstring& path) {
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    auto resourceName = [](const std::wstring& key) -> std::wstring {
        wchar_t resource[2048]{}, localized[2048]{};
        DWORD bytes = sizeof(resource);
        if (RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), L"FriendlyTypeName",
            RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, resource, &bytes) == ERROR_SUCCESS
            && resource[0] == L'@' && SUCCEEDED(SHLoadIndirectString(resource, localized, _countof(localized), nullptr)))
            return localized;
        return {};
    };
    if (ext && *ext) {
        // Honor the effective association when it has a localized Shell resource.
        wchar_t progid[512]{}; DWORD bytes = sizeof(progid);
        if (RegGetValueW(HKEY_CLASSES_ROOT, ext, nullptr, RRF_RT_REG_SZ, nullptr, progid, &bytes) == ERROR_SUCCESS) {
            auto name = resourceName(progid);
            if (!name.empty()) return name;
        }
        auto name = resourceName(std::wstring(L"SystemFileAssociations\\") + ext);
        if (!name.empty()) return name;
        // A third-party association can replace the friendly text with English.
        // OpenWithProgids retains the OS type and its MUI resource (no hardcoded
        // extensions, format names, translations, or Shell DLL resource ids).
        HKEY key = nullptr;
        const std::wstring openWith = std::wstring(ext) + L"\\OpenWithProgids";
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, openWith.c_str(), 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
            for (DWORD i=0;;++i) {
                DWORD length=_countof(progid);
                if (RegEnumValueW(key, i, progid, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                name = resourceName(progid);
                if (!name.empty()) break;
            }
            RegCloseKey(key);
            if (!name.empty()) return name;
        }
    }
    SHFILEINFOW info{};
    if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_TYPENAME)) return info.szTypeName;
    return {};
}
inline std::vector<std::wstring> AncestorPaths(const std::wstring& path) {
    std::vector<std::wstring> chain;
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) return chain;
    do {
        if (ILIsEmpty(pidl)) break; // Desktop's filesystem alias is not a drive ancestor
        PWSTR name = nullptr;
        if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, &name))) {
            chain.emplace_back(name);
            CoTaskMemFree(name);
        }
    } while (ILRemoveLastID(pidl));
    CoTaskMemFree(pidl);
    std::reverse(chain.begin(), chain.end());
    // Known folders can parse under the virtual UserFiles node, omitting C:\ and
    // Users from the PIDL. Complete that filesystem ancestry before expanding our
    // drive-rooted tree (the same applies to redirected known folders).
    if (!chain.empty()) {
        const auto first = chain.front();
        std::vector<std::wstring> parents;
        if (first.size() >= 3 && first[1] == L':') {
            parents.push_back(first.substr(0, 3));
            for (size_t slash = first.find(L'\\', 3); slash != std::wstring::npos;
                slash = first.find(L'\\', slash + 1)) parents.push_back(first.substr(0, slash));
            if (first.size() == 3) parents.clear();
        }
        chain.insert(chain.begin(), parents.begin(), parents.end());
    }
    return chain;
}
struct FolderCounts { unsigned folders = 0, files = 0; DWORD error = ERROR_SUCCESS; };
inline FolderCounts CountChildren(const std::wstring& path, bool showHidden) {
    FolderCounts result;
    std::wstring pattern = path;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) {
        result.error = GetLastError();
        if (result.error == ERROR_FILE_NOT_FOUND) result.error = ERROR_SUCCESS; // real empty folder
        return result;
    }
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
        if (!showHidden && (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ++result.folders;
        else ++result.files;
    } while (FindNextFileW(find, &data));
    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) result.error = error;
    FindClose(find);
    return result;
}
struct Properties {
    std::wstring name, type;
    ULONGLONG size = 0;
    bool hasSize = false;
    FILETIME modified{}, created{};
};
inline Properties ReadProperties(const std::wstring& path) {
    Properties result;
    IShellItem2* item = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        PWSTR value = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &value))) {
            result.name = value; CoTaskMemFree(value);
        }
        value = nullptr;
        if (SUCCEEDED(item->GetString(PKEY_ItemTypeText, &value))) {
            result.type = value; CoTaskMemFree(value);
        }
        result.hasSize = SUCCEEDED(item->GetUInt64(PKEY_Size, &result.size));
        item->GetFileTime(PKEY_DateModified, &result.modified);
        item->GetFileTime(PKEY_DateCreated, &result.created);
        item->Release();
    }
    const auto localized = LocalizedTypeName(path);
    if (!localized.empty()) result.type = localized;
    // Providers may omit properties. Only fill omitted fields from filesystem attributes.
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        if (!result.hasSize && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            result.size = (ULONGLONG(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            result.hasSize = true;
        }
        if (!result.modified.dwHighDateTime && !result.modified.dwLowDateTime) result.modified = data.ftLastWriteTime;
        if (!result.created.dwHighDateTime && !result.created.dwLowDateTime) result.created = data.ftCreationTime;
    }
    return result;
}
}
