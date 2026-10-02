// FastFile - external folder activation and migration of obsolete owned Shell verbs.

#include "MainWndInternal.h"

namespace {

constexpr wchar_t kFastFileVerb[] = L"FastFile.open";
constexpr wchar_t kExplorerVerb[] = L"FastFile.WindowsExplorer";
constexpr wchar_t kClassesRoot[] = L"Software\\Classes\\";

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

bool IsFastFileCommand(const std::wstring& command)
{
    int argc = 0;
    auto argv = CommandLineToArgvW(command.c_str(), &argc);
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, _countof(exe));
    const bool matches = argv && argc && (_wcsicmp(PathFindFileNameW(argv[0]), L"FastFile.exe") == 0
        || _wcsicmp(argv[0], exe) == 0);
    LocalFree(argv);
    return matches;
}

bool RestoreShellDefault(const wchar_t* className)
{
    const auto shell=ClassShellKey(className);
    std::wstring current;
    if(!ReadRegString(HKEY_CURRENT_USER,shell,nullptr,current))return true;
    if(_wcsicmp(current.c_str(),kFastFileVerb)!=0 && _wcsicmp(current.c_str(),kExplorerVerb)!=0)return true;
    std::wstring command;
    if(ReadRegString(HKEY_CURRENT_USER,shell+L"\\"+current+L"\\command",nullptr,command)) {
        int argc=0;auto argv=CommandLineToArgvW(command.c_str(),&argc);
        const bool owned=_wcsicmp(current.c_str(),kFastFileVerb)==0 ? IsFastFileCommand(command)
            : argv && argc && _wcsicmp(PathFindFileNameW(argv[0]),L"explorer.exe")==0;
        LocalFree(argv);if(!owned)return true;
    }
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,shell.c_str(),0,KEY_SET_VALUE,&key)!=ERROR_SUCCESS)return false;
    const LONG result=RegDeleteValueW(key,nullptr);RegCloseKey(key);
    return result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND;
}

void NotifyAssociationChanged()
{
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
    wchar_t roots[512]{};
    const DWORD length = GetLogicalDriveStringsW(_countof(roots), roots);
    if (length && length < _countof(roots)) {
        for (const wchar_t* root = roots; *root; root += wcslen(root) + 1)
            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSH, root, nullptr);
    }
    PIDLIST_ABSOLUTE computer = nullptr;
    if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &computer))) {
        SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_IDLIST | SHCNF_FLUSH, computer, nullptr);
        CoTaskMemFree(computer);
    }
}

} // namespace

bool CMainWnd::ShouldRedirectDisabledShellOpen()
{
    DWORD enabled = 1;
    return ReadRegDword(HKEY_CURRENT_USER, L"Software\\FastFile", L"FolderHandlerEnabled", enabled)
        && enabled == 0;
}

bool CMainWnd::RedirectDisabledShellOpen(const std::vector<std::wstring>& paths)
{
    if (paths.empty() || !ShouldRedirectDisabledShellOpen()) return false;
    wchar_t windows[MAX_PATH]{};
    if (!GetWindowsDirectoryW(windows, _countof(windows))) return false;
    const auto explorer = std::wstring(windows) + L"\\explorer.exe";
    bool opened = false;
    for (const auto& path : paths) {
        const auto target = NormalizePath(path);
        if (target.empty()) continue;
        std::wstring command = L"\"" + explorer + L"\" \"" + target + L"\\.\"";
        STARTUPINFOW startup = {sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (CreateProcessW(explorer.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                nullptr, windows, &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            opened = true;
        }
    }
    return opened;
}

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

    m_openingExternalPaths=true;
    size_t first = 0;
    if (replaceInitialTab && m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        // InitWindow has finished constructing the default tab, so direct navigation is safe.
        NavigateToNow(targets.front(), false);
        first = 1;
    }
    for (; first < targets.size(); ++first)
        AddTab(targets[first], true);

    m_openingExternalPaths=false;
    CDuiString status;
    status.Format(_T("已用 FastFile 打开 %d 个文件夹"), static_cast<int>(targets.size()));
    UpdateStatus(status.GetData());
}

bool CMainWnd::RestoreNativeFolderHandlers()
{
    bool changed=false;
    for(const auto& cls:kFolderClasses) {
        HKEY key=nullptr;std::wstring current;
        if(ReadRegString(HKEY_CURRENT_USER,ClassShellKey(cls.name),nullptr,current)
            && (_wcsicmp(current.c_str(),kFastFileVerb)==0 || _wcsicmp(current.c_str(),kExplorerVerb)==0))changed=true;
        for(const auto* verb:{kFastFileVerb,kExplorerVerb}) {
            if(RegOpenKeyExW(HKEY_CURRENT_USER,(ClassShellKey(cls.name)+L"\\"+verb).c_str(),0,KEY_READ,&key)==ERROR_SUCCESS) {
                changed=true;RegCloseKey(key);
            }
        }
    }
    if(!changed)return true;
    bool ok = true;
    bool allDefaultsRestored = true;
    for (const FolderClass& cls : kFolderClasses) {
        if (!RestoreShellDefault(cls.name)) {
            ok = false;
            allDefaultsRestored = false;
            continue;
        }
        for(const auto* verb:{kFastFileVerb,kExplorerVerb}) {
            const auto verbKey=ClassShellKey(cls.name)+L"\\"+verb;
            std::wstring command;
            if(ReadRegString(HKEY_CURRENT_USER,verbKey+L"\\command",nullptr,command)) {
                int argc=0;auto argv=CommandLineToArgvW(command.c_str(),&argc);
                const bool owned=_wcsicmp(verb,kFastFileVerb)==0 ? IsFastFileCommand(command)
                    : argv && argc && _wcsicmp(PathFindFileNameW(argv[0]),L"explorer.exe")==0;
                LocalFree(argv);
                if(!owned)continue;
            }
            const LONG erase=RegDeleteTreeW(HKEY_CURRENT_USER,verbKey.c_str());
            if(erase!=ERROR_SUCCESS && erase!=ERROR_FILE_NOT_FOUND)ok=false;
        }
    }
    if (allDefaultsRestored) {
        const LONG eraseBackup = ::RegDeleteTreeW(HKEY_CURRENT_USER,
            L"Software\\FastFile\\FolderHandlerBackup");
        if (eraseBackup != ERROR_SUCCESS && eraseBackup != ERROR_FILE_NOT_FOUND)
            ok = false;
    }
    if (allDefaultsRestored && !WriteRegDword(HKEY_CURRENT_USER, L"Software\\FastFile", L"FolderHandlerEnabled", 0))
        ok = false;
    NotifyAssociationChanged();
    return ok;
}


namespace {
constexpr wchar_t kSettingsVerb[]=L"FastFile.SettingsOpen";
constexpr wchar_t kIntegrationRoot[]=L"Software\\FastFile\\IntegrationBackupV1";
constexpr wchar_t kOwner[]=L"FastFile.Settings.Integration.v1";
struct RegistryValue {
    bool exists=false;DWORD type=REG_NONE;std::vector<BYTE> bytes;
};
RegistryValue GetRaw(const std::wstring& path,const wchar_t* name) {
    RegistryValue value;HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return value;
    DWORD size=0;
    if(RegQueryValueExW(key,name,nullptr,&value.type,nullptr,&size)==ERROR_SUCCESS) {
        value.bytes.resize(size);DWORD count=size;
        if(RegQueryValueExW(key,name,nullptr,&value.type,value.bytes.data(),&count)==ERROR_SUCCESS)value.exists=true;
    }RegCloseKey(key);return value;
}
bool PutRaw(const std::wstring& path,const wchar_t* name,const RegistryValue& value) {
    HKEY key=nullptr;
    if(!value.exists) {
        LONG open=RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_SET_VALUE,&key);
        if(open==ERROR_FILE_NOT_FOUND)return true;if(open!=ERROR_SUCCESS)return false;
        LONG result=RegDeleteValueW(key,name);RegCloseKey(key);return result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND;
    }
    if(RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    LONG result=RegSetValueExW(key,name,0,value.type,value.bytes.data(),DWORD(value.bytes.size()));RegCloseKey(key);
    return result==ERROR_SUCCESS;
}
struct TreeValue {std::wstring relative,name;RegistryValue value;};
struct TreeSnapshot {
    std::wstring path;bool existed=false;std::vector<std::wstring> nodes;std::vector<TreeValue> values;
    explicit TreeSnapshot(const std::wstring& root):path(root) {
        HKEY key=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_READ,&key)==ERROR_SUCCESS) {
            existed=true;Capture(key,L"");RegCloseKey(key);
        }
    }
    void Capture(HKEY key,const std::wstring& relative) {
        nodes.push_back(relative);
        DWORD nameMax=0,dataMax=0;RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&nameMax,&dataMax,nullptr,nullptr);
        std::vector<wchar_t> name(nameMax+2);std::vector<BYTE> bytes(dataMax+1);
        for(DWORD index=0;;++index) {
            DWORD count=DWORD(name.size()),size=DWORD(bytes.size()),type=0;
            LONG result=RegEnumValueW(key,index,name.data(),&count,nullptr,&type,bytes.data(),&size);
            if(result==ERROR_NO_MORE_ITEMS)break;if(result!=ERROR_SUCCESS)continue;
            RegistryValue value;value.exists=true;value.type=type;value.bytes.assign(bytes.begin(),bytes.begin()+size);
            values.push_back({relative,std::wstring(name.data(),count),std::move(value)});
        }
        for(DWORD index=0;;++index) {
            wchar_t child[256]{};DWORD count=_countof(child);
            LONG result=RegEnumKeyExW(key,index,child,&count,nullptr,nullptr,nullptr,nullptr);
            if(result==ERROR_NO_MORE_ITEMS)break;if(result!=ERROR_SUCCESS)continue;
            HKEY sub=nullptr;if(RegOpenKeyExW(key,child,0,KEY_READ,&sub)==ERROR_SUCCESS) {
                Capture(sub,relative.empty()?child:relative+L"\\"+child);RegCloseKey(sub);
            }
        }
    }
    bool Restore() const {
        LONG erase=RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());
        bool ok=erase==ERROR_SUCCESS || erase==ERROR_FILE_NOT_FOUND;
        if(!existed)return ok;
        for(const auto& relative:nodes) {
            HKEY key=nullptr;const auto node=relative.empty()?path:path+L"\\"+relative;
            if(RegCreateKeyExW(HKEY_CURRENT_USER,node.c_str(),0,nullptr,0,KEY_WRITE,nullptr,&key,nullptr)!=ERROR_SUCCESS)ok=false;
            if(key)RegCloseKey(key);
        }
        for(const auto& value:values) {
            const auto node=value.relative.empty()?path:path+L"\\"+value.relative;
            if(!PutRaw(node,value.name.c_str(),value.value))ok=false;
        }return ok;
    }
};
struct IntegrationClass {const wchar_t* id;const wchar_t* cls;bool computer;};
constexpr IntegrationClass integrationClasses[]={
    {L"Folder",L"Folder",false},{L"Directory",L"Directory",false},{L"Drive",L"Drive",false},
    {L"Computer",L"CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}",true}
};
bool OwnSettingsVerb(const std::wstring& verb) {
    std::wstring owner;return ReadRegString(HKEY_CURRENT_USER,verb,L"FastFile.Owner",owner) && owner==kOwner;
}
bool KeyExists(const std::wstring& path) {
    HKEY key=nullptr;LONG result=RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_READ,&key);
    if(key)RegCloseKey(key);return result==ERROR_SUCCESS;
}
bool SaveIntegrationDefault(const std::wstring& shell,const std::wstring& backup) {
    if(KeyExists(backup))return true;
    const RegistryValue value=GetRaw(shell,nullptr);
    if(!WriteRegDword(HKEY_CURRENT_USER,backup,L"HadDefault",value.exists?1:0)
        || !WriteRegDword(HKEY_CURRENT_USER,backup,L"DefaultType",value.type))return false;
    RegistryValue data=value;data.exists=true;data.type=REG_BINARY;return PutRaw(backup,L"DefaultData",data);
}
bool RestoreIntegrationDefault(const std::wstring& shell,const std::wstring& backup) {
    std::wstring current;ReadRegString(HKEY_CURRENT_USER,shell,nullptr,current);
    if(current==kSettingsVerb) {
        DWORD exists=0,type=REG_SZ;
        if(!ReadRegDword(HKEY_CURRENT_USER,backup,L"HadDefault",exists))return false;
        ReadRegDword(HKEY_CURRENT_USER,backup,L"DefaultType",type);
        RegistryValue original=GetRaw(backup,L"DefaultData");original.exists=exists!=0;original.type=type;
        if(!PutRaw(shell,nullptr,original))return false;
    }
    LONG result=RegDeleteTreeW(HKEY_CURRENT_USER,backup.c_str());return result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND;
}
}
void CMainWnd::ReadSystemIntegration(FastFileSettings& settings) {
    DWORD flag=0;
    ReadRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",L"IntegrationMenu",flag);settings.contextMenu=flag!=0;
    flag=0;ReadRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",L"IntegrationFolders",flag);settings.defaultFolders=flag!=0;
    flag=0;ReadRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",L"IntegrationComputer",flag);settings.defaultComputer=flag!=0;
}
bool CMainWnd::ApplySystemIntegration(const FastFileSettings& settings) {
    FastFileSettings old;ReadSystemIntegration(old);
    const bool disabling=!settings.contextMenu && !settings.defaultFolders && !settings.defaultComputer;
    bool recoveryNeeded=false;
    if(disabling)for(const auto& cls:integrationClasses)
        recoveryNeeded=recoveryNeeded || OwnSettingsVerb(ClassShellKey(cls.cls)+L"\\"+kSettingsVerb)
            || KeyExists(std::wstring(kIntegrationRoot)+L"\\"+cls.id);
    if(!recoveryNeeded && old.contextMenu==settings.contextMenu && old.defaultFolders==settings.defaultFolders && old.defaultComputer==settings.defaultComputer)return true;
    wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,_countof(executable)))return false;
    std::vector<TreeSnapshot> trees;
    struct DefaultSnapshot {std::wstring shell;RegistryValue value;};std::vector<DefaultSnapshot> defaults;
    const wchar_t* flagNames[]={L"IntegrationMenu",L"IntegrationFolders",L"IntegrationComputer"};
    RegistryValue flags[3];for(int i=0;i<3;++i)flags[i]=GetRaw(L"Software\\FastFile",flagNames[i]);
    // Validate all private verb keys before mutating anything; a foreign same-name entry
    // is never overwritten, even when the user asks to enable the handler.
    for(const auto& cls:integrationClasses) {
        const auto verb=ClassShellKey(cls.cls)+L"\\"+kSettingsVerb;
        if(KeyExists(verb) && !OwnSettingsVerb(verb))return false;
        trees.emplace_back(verb);trees.emplace_back(std::wstring(kIntegrationRoot)+L"\\"+cls.id);
        const auto shell=ClassShellKey(cls.cls);defaults.push_back({shell,GetRaw(shell,nullptr)});
    }
    auto rollback=[&] {
        for(auto it=trees.rbegin();it!=trees.rend();++it)it->Restore();
        for(const auto& entry:defaults)PutRaw(entry.shell,nullptr,entry.value);
        for(int i=0;i<3;++i)PutRaw(L"Software\\FastFile",flagNames[i],flags[i]);
        NotifyAssociationChanged();return false;
    };
    for(const auto& cls:integrationClasses) {
        // Folder also covers Control Panel, Recycle Bin and other virtual namespaces.
        // Keep its default intact; Directory/Drive handle filesystem targets, and
        // Computer has its own explicitly supported namespace command.
        const bool enabled=cls.computer?settings.defaultComputer:(settings.defaultFolders && wcscmp(cls.id,L"Folder")!=0);
        const bool menu=cls.computer?settings.defaultComputer:settings.contextMenu;
        const auto shell=ClassShellKey(cls.cls),verb=shell+L"\\"+kSettingsVerb;
        const auto backup=std::wstring(kIntegrationRoot)+L"\\"+cls.id;
        if(enabled) {
            if(!SaveIntegrationDefault(shell,backup))return rollback();
        } else if(KeyExists(backup) && !RestoreIntegrationDefault(shell,backup))return rollback();
        if(enabled || menu) {
            const auto target=cls.computer?std::wstring(kThisPcPath):std::wstring(L"%1");
            const auto command=L"\""+std::wstring(executable)+L"\" --shell-folder \""+target+L"\"";
            if(!WriteRegString(HKEY_CURRENT_USER,verb,L"FastFile.Owner",kOwner)
                || !WriteRegString(HKEY_CURRENT_USER,verb,nullptr,L"使用 FastFile 打开")
                || !WriteRegString(HKEY_CURRENT_USER,verb,L"Icon",L"\""+std::wstring(executable)+L"\",0")
                || !WriteRegString(HKEY_CURRENT_USER,verb+L"\\command",nullptr,command))return rollback();
            if(enabled && !WriteRegString(HKEY_CURRENT_USER,shell,nullptr,kSettingsVerb))return rollback();
        } else if(KeyExists(verb)) {
            if(RegDeleteTreeW(HKEY_CURRENT_USER,verb.c_str())!=ERROR_SUCCESS)return rollback();
        }
    }
    if(!WriteRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",flagNames[0],settings.contextMenu)
        || !WriteRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",flagNames[1],settings.defaultFolders)
        || !WriteRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",flagNames[2],settings.defaultComputer))return rollback();
    NotifyAssociationChanged();return true;
}
