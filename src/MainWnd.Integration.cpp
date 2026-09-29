// FastFile - optional integration with Windows folder open actions.
//
// This is intentionally a *folder opener*, not a replacement Windows shell.  It changes
// the current user's default verb for Folder, Directory and Drive to a FastFile-owned verb;
// desktop.exe, taskbar, Start, file pickers and all machine-wide settings remain untouched.

#include "MainWndInternal.h"

namespace {

constexpr wchar_t kFastFileVerb[] = L"FastFile.open";
constexpr wchar_t kClassesRoot[] = L"Software\\Classes\\";
constexpr wchar_t kBackupRoot[] = L"Software\\FastFile\\FolderHandlerBackup\\";

struct FolderClass {
    const wchar_t* name;
};

constexpr FolderClass kFolderClasses[] = {
    { L"Folder" },
    { L"Directory" },
    { L"Drive" },
};

std::wstring ClassShellKey(const wchar_t* className)
{
    return std::wstring(kClassesRoot) + className + L"\\shell";
}

std::wstring BackupKey(const wchar_t* className)
{
    return std::wstring(kBackupRoot) + className;
}

bool ReadRegString(HKEY root, const std::wstring& subKey, const wchar_t* valueName,
    std::wstring& value)
{
    value.clear();
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, subKey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    DWORD bytes = 0;
    const LONG query = ::RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes);
    if (query != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        ::RegCloseKey(key);
        return false;
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    const LONG read = ::RegQueryValueExW(key, valueName, nullptr, &type,
        reinterpret_cast<BYTE*>(buffer.data()), &bytes);
    ::RegCloseKey(key);
    if (read != ERROR_SUCCESS)
        return false;
    value.assign(buffer.data());
    return true;
}

bool WriteRegString(HKEY root, const std::wstring& subKey, const wchar_t* valueName,
    const std::wstring& value)
{
    HKEY key = nullptr;
    const LONG create = ::RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (create != ERROR_SUCCESS)
        return false;
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG write = ::RegSetValueExW(key, valueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    ::RegCloseKey(key);
    return write == ERROR_SUCCESS;
}

bool ReadRegDword(HKEY root, const std::wstring& subKey, const wchar_t* valueName,
    DWORD& value)
{
    value = 0;
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, subKey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    const LONG result = ::RegQueryValueExW(key, valueName, nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &bytes);
    ::RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_DWORD && bytes == sizeof(value);
}

bool WriteRegDword(HKEY root, const std::wstring& subKey, const wchar_t* valueName,
    DWORD value)
{
    HKEY key = nullptr;
    const LONG create = ::RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (create != ERROR_SUCCESS)
        return false;
    const LONG write = ::RegSetValueExW(key, valueName, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    ::RegCloseKey(key);
    return write == ERROR_SUCCESS;
}

bool BackupShellDefault(const wchar_t* className)
{
    const std::wstring backup = BackupKey(className);
    DWORD alreadySaved = 0;
    if (ReadRegDword(HKEY_CURRENT_USER, backup, L"Saved", alreadySaved) && alreadySaved == 1)
        return true;

    std::wstring previous;
    const bool hadPrevious = ReadRegString(HKEY_CURRENT_USER, ClassShellKey(className), nullptr, previous);
    if (!WriteRegDword(HKEY_CURRENT_USER, backup, L"Saved", 1)
        || !WriteRegDword(HKEY_CURRENT_USER, backup, L"ShellDefaultPresent", hadPrevious ? 1 : 0))
        return false;
    return !hadPrevious || WriteRegString(HKEY_CURRENT_USER, backup, L"ShellDefault", previous);
}

bool RestoreShellDefault(const wchar_t* className)
{
    const std::wstring backup = BackupKey(className);
    DWORD saved = 0;
    if (!ReadRegDword(HKEY_CURRENT_USER, backup, L"Saved", saved) || saved != 1)
        return true;

    const std::wstring shellKey = ClassShellKey(className);
    std::wstring current;
    if (ReadRegString(HKEY_CURRENT_USER, shellKey, nullptr, current)
        && ::_wcsicmp(current.c_str(), kFastFileVerb) == 0) {
        DWORD hadPrevious = 0;
        std::wstring previous;
        if (ReadRegDword(HKEY_CURRENT_USER, backup, L"ShellDefaultPresent", hadPrevious)
            && hadPrevious != 0 && ReadRegString(HKEY_CURRENT_USER, backup, L"ShellDefault", previous)) {
            if (!WriteRegString(HKEY_CURRENT_USER, shellKey, nullptr, previous))
                return false;
        } else {
            HKEY key = nullptr;
            if (::RegOpenKeyExW(HKEY_CURRENT_USER, shellKey.c_str(), 0, KEY_SET_VALUE, &key)
                == ERROR_SUCCESS) {
                const LONG result = ::RegDeleteValueW(key, nullptr);
                ::RegCloseKey(key);
                if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
                    return false;
            }
        }
    }
    return true;
}

std::wstring CurrentExecutablePath()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD copied = ::GetModuleFileNameW(nullptr, buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (copied == 0 || copied >= buffer.size() - 1)
        return {};
    return std::wstring(buffer.data(), copied);
}

void NotifyAssociationChanged()
{
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

} // namespace

std::wstring CMainWnd::ResolveFolderOpenTarget(const std::wstring& path)
{
    if (path.empty())
        return {};
    if (IsThisPcPath(path) || path == L"此电脑")
        return kThisPcPath;

    const std::wstring normalized = NormalizePath(path);
    if (normalized.empty())
        return {};
    const DWORD attrs = ::GetFileAttributesW(normalized.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return normalized;

    // A caller can hand us a file path (for example from a custom shortcut).  FastFile does
    // not yet have a "select this file" activation mode, so open its containing folder.
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        const std::wstring parent = ParentPath(normalized);
        const DWORD parentAttrs = parent.empty() ? INVALID_FILE_ATTRIBUTES
            : ::GetFileAttributesW(parent.c_str());
        if (parentAttrs != INVALID_FILE_ATTRIBUTES && (parentAttrs & FILE_ATTRIBUTE_DIRECTORY))
            return parent;
    }
    return {};
}

void CMainWnd::OpenExternalPaths(const std::vector<std::wstring>& paths, bool replaceInitialTab)
{
    std::vector<std::wstring> targets;
    for (const std::wstring& path : paths) {
        const std::wstring target = ResolveFolderOpenTarget(path);
        if (target.empty())
            continue;
        bool duplicate = false;
        for (const std::wstring& seen : targets) {
            if (PathEquals(seen, target)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            targets.push_back(target);
    }
    if (targets.empty()) {
        UpdateStatus(_T("没有可打开的文件夹"));
        return;
    }

    size_t first = 0;
    if (replaceInitialTab && m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        // InitWindow has finished constructing the default tab, so direct navigation is safe.
        NavigateToNow(targets.front(), false);
        first = 1;
    }
    for (; first < targets.size(); ++first)
        AddTab(targets[first], true);

    CDuiString status;
    status.Format(_T("已用 FastFile 打开 %d 个文件夹"), static_cast<int>(targets.size()));
    UpdateStatus(status.GetData());
}

bool CMainWnd::IsFolderOpenHandlerEnabled() const
{
    for (const FolderClass& cls : kFolderClasses) {
        std::wstring current;
        if (!ReadRegString(HKEY_CURRENT_USER, ClassShellKey(cls.name), nullptr, current)
            || ::_wcsicmp(current.c_str(), kFastFileVerb) != 0)
            return false;
    }
    return true;
}

bool CMainWnd::EnableFolderOpenHandler()
{
    const std::wstring exe = CurrentExecutablePath();
    if (exe.empty())
        return false;

    // Never overwrite an existing per-user verb of the same name.  It is very unlikely,
    // but declining is safer than destroying a separately installed tool's integration.
    for (const FolderClass& cls : kFolderClasses) {
        HKEY existing = nullptr;
        const std::wstring verbKey = ClassShellKey(cls.name) + L"\\" + kFastFileVerb;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, verbKey.c_str(), 0, KEY_QUERY_VALUE,
                &existing) == ERROR_SUCCESS) {
            ::RegCloseKey(existing);
            return false;
        }
    }

    for (const FolderClass& cls : kFolderClasses) {
        if (!BackupShellDefault(cls.name)) {
            DisableFolderOpenHandler();
            return false;
        }
    }

    const std::wstring command = L"\"" + exe + L"\" --open \"%1\"";
    for (const FolderClass& cls : kFolderClasses) {
        const std::wstring shellKey = ClassShellKey(cls.name);
        const std::wstring verbKey = shellKey + L"\\" + kFastFileVerb;
        if (!WriteRegString(HKEY_CURRENT_USER, shellKey, nullptr, kFastFileVerb)
            || !WriteRegString(HKEY_CURRENT_USER, verbKey, nullptr, L"使用 FastFile 打开")
            || !WriteRegString(HKEY_CURRENT_USER, verbKey, L"Icon", exe + L",0")
            || !WriteRegString(HKEY_CURRENT_USER, verbKey + L"\\command", nullptr, command)) {
            DisableFolderOpenHandler();
            return false;
        }
    }
    NotifyAssociationChanged();
    return true;
}

bool CMainWnd::DisableFolderOpenHandler()
{
    bool ok = true;
    bool allDefaultsRestored = true;
    for (const FolderClass& cls : kFolderClasses) {
        if (!RestoreShellDefault(cls.name)) {
            ok = false;
            allDefaultsRestored = false;
            continue;
        }
        std::wstring current;
        if (ReadRegString(HKEY_CURRENT_USER, ClassShellKey(cls.name), nullptr, current)
            && ::_wcsicmp(current.c_str(), kFastFileVerb) == 0) {
            // Do not remove the command while it is still the default: leaving a working
            // FastFile association is safer than producing a dead default verb.
            ok = false;
            allDefaultsRestored = false;
            continue;
        }
        const std::wstring verbKey = ClassShellKey(cls.name) + L"\\" + kFastFileVerb;
        const LONG erase = ::RegDeleteTreeW(HKEY_CURRENT_USER, verbKey.c_str());
        if (erase != ERROR_SUCCESS && erase != ERROR_FILE_NOT_FOUND)
            ok = false;
    }
    if (allDefaultsRestored) {
        const LONG eraseBackup = ::RegDeleteTreeW(HKEY_CURRENT_USER,
            L"Software\\FastFile\\FolderHandlerBackup");
        if (eraseBackup != ERROR_SUCCESS && eraseBackup != ERROR_FILE_NOT_FOUND)
            ok = false;
    }
    NotifyAssociationChanged();
    return ok;
}

void CMainWnd::OnFolderOpenHandlerMenuClicked()
{
    if (!m_hWnd)
        return;

    if (IsFolderOpenHandlerEnabled()) {
        const int answer = ::MessageBoxW(m_hWnd,
            L"停止后，文件夹、目录和磁盘会恢复为之前的默认打开方式。\n\n"
            L"Windows 的桌面、任务栏和开始菜单从未被替换。\n\n"
            L"要停止由 FastFile 打开系统文件夹吗？",
            L"FastFile - 系统文件夹打开", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
        if (answer != IDYES)
            return;
        if (DisableFolderOpenHandler())
            UpdateStatus(_T("已恢复系统文件夹原来的打开方式"));
        else
            ::MessageBoxW(m_hWnd, L"恢复关联时遇到问题；原来的设置没有被强行覆盖。",
                L"FastFile", MB_ICONWARNING | MB_OK);
        return;
    }

    const int answer = ::MessageBoxW(m_hWnd,
        L"FastFile 将接管当前用户的文件夹、目录和磁盘的默认“打开”动作。\n"
        L"双击文件夹或盘符时会在 FastFile 中打开，已运行的窗口会新建标签。\n\n"
        L"不会替换 Windows 桌面、任务栏、开始菜单或系统文件选择窗口；\n"
        L"可随时在“更多选项”中关闭并恢复原来的打开方式。\n\n"
        L"现在启用吗？",
        L"FastFile - 使用 FastFile 打开系统文件夹",
        MB_ICONINFORMATION | MB_YESNO | MB_DEFBUTTON2);
    if (answer != IDYES)
        return;

    if (EnableFolderOpenHandler()) {
        UpdateStatus(_T("已启用：系统文件夹将使用 FastFile 打开"));
    } else {
        ::MessageBoxW(m_hWnd,
            L"无法启用系统文件夹打开。为保护现有设置，FastFile 没有覆盖任何同名关联。",
            L"FastFile", MB_ICONWARNING | MB_OK);
    }
}
