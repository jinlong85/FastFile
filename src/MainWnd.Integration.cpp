// FastFile - external folder activation and migration of obsolete owned Shell verbs.

#include "MainWndInternal.h"
#include "ShellWindowRegistration.h"

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

bool IsAgentRunCommand(const std::wstring& command) {
    if(command.empty())return false;
    int count=0;auto args=CommandLineToArgvW(command.c_str(),&count);
    const bool owned=args && count==1 && !_wcsicmp(args[0],CMainWnd::ExplorerAgentPath().c_str());
    LocalFree(args);return owned;
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

DWORD (*CMainWnd::s_folderProbe)(const std::wstring& path) = nullptr;
bool CMainWnd::s_shellWindowRegistrationAllowed = true;

DWORD CMainWnd::ProbeFolderAttributes(const std::wstring& path)
{
    return s_folderProbe ? s_folderProbe(path) : ::GetFileAttributesW(path.c_str());
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
    // The first access to a spun-down disk blocks here until it is ready; callers on
    // the UI thread must run this on a worker (see OpenExternalPaths).
    const DWORD attrs = ProbeFolderAttributes(normalized);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return normalized;

    // A caller can hand us a file path (for example from a custom shortcut).  FastFile does
    // not yet have a "select this file" activation mode, so open its containing folder.
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        const std::wstring parent = ParentPath(normalized);
        const DWORD parentAttrs = parent.empty() ? INVALID_FILE_ATTRIBUTES
            : ProbeFolderAttributes(parent);
        if (parentAttrs != INVALID_FILE_ATTRIBUTES && (parentAttrs & FILE_ATTRIBUTE_DIRECTORY))
            return parent;
    }
    return {};
}

void CMainWnd::OpenExternalPaths(const std::vector<std::wstring>& paths, bool replaceInitialTab)
{
    if (paths.empty()) return;
    auto* job = new (std::nothrow) ExternalOpenJob;
    if (!job) return;
    job->paths = paths;
    job->replaceInitialTab = replaceInitialTab;
    if (!m_hWnd) { ++m_externalOpensPending; FinishExternalOpen(job); return; }
    // Resolving touches the target volume. A sleeping mechanical disk can take 10+
    // seconds to answer; do it off the UI thread so the window paints, accepts input and
    // keeps answering other activations meanwhile. The UI thread navigates once the
    // volume has answered, when its own Shell calls are fast again.
    ++m_externalOpensPending;
    ::SetPropW(m_hWnd, L"FastFile.ExternalOpenPending", reinterpret_cast<HANDLE>(1));
    UpdateStatus(_T("正在打开文件夹…"));
    const HWND window = m_hWnd;
    try {
        std::thread([window, job] {
            const HRESULT com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (const std::wstring& path : job->paths)
                job->targets.push_back(ResolveFolderOpenTarget(path));
            if (SUCCEEDED(com)) ::CoUninitialize();
            if (!::PostMessageW(window, kMsgExternalPathsResolved, 0, reinterpret_cast<LPARAM>(job)))
                delete job;
        }).detach();
    } catch (...) {
        FinishExternalOpen(job); // no worker available: resolve synchronously
    }
}

void CMainWnd::FinishExternalOpen(ExternalOpenJob* job)
{
    std::unique_ptr<ExternalOpenJob> owned(job);
    // The pending marker (read by the activation regression) clears only after the
    // navigation below has been issued.
    struct PendingDone {
        CMainWnd* self;
        ~PendingDone() {
            if (self->m_externalOpensPending > 0 && --self->m_externalOpensPending == 0 && self->m_hWnd)
                ::RemovePropW(self->m_hWnd, L"FastFile.ExternalOpenPending");
        }
    } pendingDone{this};
    if (!job) return;
    m_shellWindowReannounce = true;
    if (job->targets.size() != job->paths.size()) {
        job->targets.clear();
        for (const std::wstring& path : job->paths)
            job->targets.push_back(ResolveFolderOpenTarget(path));
    }
    const bool replaceInitialTab = job->replaceInitialTab;
    std::vector<std::wstring> targets;
    for (const std::wstring& target : job->targets) {
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
        if(m_currentPath.empty() && m_activeTab>=0)ActivateTab(m_activeTab);
        UpdateStatus(_T("没有可打开的文件夹"));
        return;
    }

    // A cached Shell view at the same path is not proof that this request was
    // resolved. Only successful physical targets can authorize window transfer.
    for(auto& entry:m_explorerTransfers)for(const auto& target:targets)
        if(entry.second.started && PathEquals(entry.second.source.path,target))entry.second.resolved=true;

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
    status.Format(_T("已接收 %d 个文件夹，正在加载文件列表…"), static_cast<int>(targets.size()));
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
constexpr const wchar_t* kFolderActions[]={L"open",L"explore",L"opennewwindow"};
bool DeleteTree(const std::wstring& path) {
    const LONG result=RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str());
    return result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND;
}
// Only filesystem classes are overridden. Folder also represents virtual objects.
bool ApplyFolderAction(const std::wstring& verb,const std::wstring& backup,
    const std::wstring& command,bool enabled) {
    DWORD hadKey=0;
    if(enabled) {
        if(!ReadRegDword(HKEY_CURRENT_USER,backup,L"HadKey",hadKey)) {
            TreeSnapshot original(verb);hadKey=original.existed?1:0;
            original.path=backup+L"\\Original";
            if(!original.Restore() || !WriteRegDword(HKEY_CURRENT_USER,backup,L"HadKey",hadKey))return false;
        }
        // Empty DelegateExecute masks the inherited machine-level Explorer delegate.
        // Drop stale user DDE/COM handlers so the quoted command is the sole action.
        if(!DeleteTree(verb))return false;
        return WriteRegString(HKEY_CURRENT_USER,verb,L"FastFile.Owner",kOwner)
            && WriteRegString(HKEY_CURRENT_USER,verb+L"\\command",nullptr,command)
            && WriteRegString(HKEY_CURRENT_USER,verb+L"\\command",L"DelegateExecute",L"");
    }
    if(!KeyExists(backup))return true;
    std::wstring current;
    if(OwnSettingsVerb(verb) && ReadRegString(HKEY_CURRENT_USER,verb+L"\\command",nullptr,current)
        && IsFastFileCommand(current)) {
        if(!ReadRegDword(HKEY_CURRENT_USER,backup,L"HadKey",hadKey))return false;
        TreeSnapshot original(backup+L"\\Original");
        original.path=verb;original.existed=hadKey!=0;
        if(!original.Restore())return false;
    }
    // A later third-party replacement is preserved, just as for the default verb.
    return DeleteTree(backup);
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
    flag=0;ReadRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",L"IntegrationExplorerWindows",flag);settings.explorerWindowTakeover=flag!=0;
}
CMainWnd::IntegrationStatus CMainWnd::DetectSystemIntegration() {
    IntegrationStatus status;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,_countof(executable));
    status.details=L"当前运行程序："+std::wstring(executable)+L"\r\n";
    auto inspect=[&](const wchar_t* label,const std::wstring& cls,const std::wstring& action,bool inherit) {
        std::wstring verb=action,command,delegate;
        if(verb.empty()) {
            ReadRegString(HKEY_CLASSES_ROOT,cls+L"\\shell",nullptr,verb);
            if(verb.empty() && inherit)ReadRegString(HKEY_CLASSES_ROOT,L"Folder\\shell",nullptr,verb);
            if(verb.empty())verb=L"open";
        }
        auto read=[&](const std::wstring& target) {
            const auto shell=target+L"\\shell";
            command.clear();delegate.clear();
            const auto key=shell+L"\\"+verb+L"\\command";
            const bool exists=ReadRegString(HKEY_CLASSES_ROOT,key,nullptr,command);
            ReadRegString(HKEY_CLASSES_ROOT,key,L"DelegateExecute",delegate);
            return exists || !delegate.empty();
        };
        if(!read(cls) && inherit)read(L"Folder");
        int count=0;auto args=command.empty()?nullptr:CommandLineToArgvW(command.c_str(),&count);
        const std::wstring path=args && count?args[0]:L"";
        bool shellArgument=false;
        for(int i=1;args && i+1<count;++i)if(wcscmp(args[i],L"--shell-folder")==0
            && (wcscmp(args[i+1],L"%1")==0 || wcscmp(args[i+1],kThisPcPath)==0))shellArgument=true;
        LocalFree(args);
        const bool ours=delegate.empty() && _wcsicmp(path.c_str(),executable)==0 && shellArgument;
        std::wstring owner;
        if(ours)owner=L"FastFile（当前程序）";
        else if(!delegate.empty()) {
            if(_wcsicmp(delegate.c_str(),L"{11dbb47c-a525-400b-9e80-a54615a090c0}")==0)owner=L"Windows 资源管理器";
            else {
                std::wstring server;
                ReadRegString(HKEY_CLASSES_ROOT,L"CLSID\\"+delegate+L"\\InprocServer32",nullptr,server);
                if(server.empty())ReadRegString(HKEY_CLASSES_ROOT,L"CLSID\\"+delegate+L"\\LocalServer32",nullptr,server);
                const auto module=PathFindFileNameW(server.c_str());
                if(_wcsicmp(module,L"ExplorerFrame.dll")==0 || _wcsicmp(module,L"explorer.exe")==0)owner=L"Windows 资源管理器";
                else if(!server.empty()) {owner=L"其他打开处理程序："+server;status.otherManager=true;}
                else owner=L"系统委托（名称未识别）："+delegate;
            }
        } else if(_wcsicmp(PathFindFileNameW(path.c_str()),L"explorer.exe")==0)owner=L"Windows 资源管理器";
        else if(!path.empty() && _wcsicmp(path.c_str(),executable)==0)owner=L"FastFile（启动参数异常，请修复）";
        else if(_wcsicmp(PathFindFileNameW(path.c_str()),L"FastFile.exe")==0)owner=L"FastFile（其他路径，请修复）";
        else if(!path.empty()) {
            const auto name=PathFindFileNameW(path.c_str());
            owner=(_wcsicmp(name,L"360FileBrowser64.exe")==0 || _wcsicmp(name,L"360FileBrowser.exe")==0)?
                L"360 文件管理器":L"其他文件管理程序："+std::wstring(name);
            status.otherManager=true;
        }
        else owner=L"未找到可用打开入口";
        status.details+=std::wstring(label)+L"："+owner+L"\r\n";
        if(action.empty())status.summary+=std::wstring(label)+L"："+owner+L"\r\n";
        if(!ours && action!=kSettingsVerb)status.recommendations+=L"• "+std::wstring(label)+L"仍由“"+owner+L"”处理。\r\n";
        return ours;
    };
    bool ready=true;
    for(const auto* cls:{L"Directory",L"Drive"}) {
        const std::wstring name=wcscmp(cls,L"Directory")==0?L"文件夹":L"磁盘";
        ready=inspect((name+L"默认打开").c_str(),cls,L"",true) && ready;
        for(const auto* action:kFolderActions) {
            const auto label=wcscmp(action,L"open")==0?L"标准打开":wcscmp(action,L"explore")==0?L"浏览":L"新窗口打开";
            ready=inspect((name+label).c_str(),cls,action,true) && ready;
        }
    }
    status.foldersReady=ready;
    status.computerReady=inspect(L"此电脑",L"CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}",L"",false);
    status.menuReady=true;
    for(const auto* cls:{L"Folder",L"Directory",L"Drive"})
        status.menuReady=inspect(wcscmp(cls,L"Folder")==0?L"通用文件夹右键入口":wcscmp(cls,L"Directory")==0?
            L"文件夹右键入口":L"磁盘右键入口",cls,kSettingsVerb,false) && status.menuReady;
    std::wstring run;
    ReadRegString(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"FastFile.DefaultManager",run);
    int runCount=0;auto runArgs=CommandLineToArgvW(run.c_str(),&runCount);
    status.backgroundReady=runArgs && runCount==1 && _wcsicmp(runArgs[0],ExplorerAgentPath().c_str())==0;
    LocalFree(runArgs);
    if(!status.foldersReady || !status.computerReady)status.recommendations+=
        L"建议选择“设为默认并修复”，统一文件夹、磁盘、此电脑及新窗口打开入口。若其他管理器重新接管，请在其设置中关闭默认接管，再重新检测。\r\n";
    if(!status.backgroundReady)status.recommendations+=L"后台登录启动尚未配置；设为默认时一并启用，用于接收直接启动资源管理器的请求。\r\n";
    status.details+=L"直接启动资源管理器的请求需要窗口检测转交；其他文件管理器的独立窗口不会被关闭。";
    return status;
}
bool CMainWnd::RepairOwnedSystemIntegration() {
    FastFileSettings settings;ReadSystemIntegration(settings);
    bool changed=false;
    for(const auto& cls:integrationClasses) {
        const bool enabled=cls.computer?settings.defaultComputer:
            settings.defaultFolders && wcscmp(cls.id,L"Folder")!=0;
        if(!enabled)continue;
        const auto shell=ClassShellKey(cls.cls),verb=shell+L"\\"+kSettingsVerb;
        std::wstring current;
        ReadRegString(HKEY_CURRENT_USER,shell,nullptr,current);
        // Repair an incomplete owned registration, never replace a later explicit
        // default chosen by another application and never invent a missing backup.
        if(!current.empty() || !OwnSettingsVerb(verb)
            || !KeyExists(std::wstring(kIntegrationRoot)+L"\\"+cls.id))continue;
        std::wstring command;
        if(!ReadRegString(HKEY_CURRENT_USER,verb+L"\\command",nullptr,command)
            || !IsFastFileCommand(command))continue;
        if(!WriteRegString(HKEY_CURRENT_USER,shell,nullptr,kSettingsVerb))return false;
        changed=true;
    }
    if(changed)NotifyAssociationChanged();
    return true;
}
bool CMainWnd::ApplySystemIntegration(const FastFileSettings& settings) {
    if(settings.explorerWindowTakeover) {
        const auto attributes=GetFileAttributesW(ExplorerAgentPath().c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    }
    FastFileSettings old;ReadSystemIntegration(old);
    const bool disabling=!settings.contextMenu && !settings.defaultFolders && !settings.defaultComputer && !settings.explorerWindowTakeover;
    bool recoveryNeeded=false;
    if(disabling)for(const auto& cls:integrationClasses)
        recoveryNeeded=recoveryNeeded || OwnSettingsVerb(ClassShellKey(cls.cls)+L"\\"+kSettingsVerb)
            || KeyExists(std::wstring(kIntegrationRoot)+L"\\"+cls.id);
    if(disabling)recoveryNeeded=recoveryNeeded || KeyExists(std::wstring(kIntegrationRoot)+L"\\Actions");
    std::wstring staleRun;
    if(disabling && ReadRegString(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"FastFile.DefaultManager",staleRun) && IsFastFileCommand(staleRun)
        && staleRun.find(L"--background")!=std::wstring::npos)recoveryNeeded=true;
    if(disabling && IsAgentRunCommand(staleRun))recoveryNeeded=true;
    // Enabled flags alone do not prove the effective default verbs or executable
    // commands are intact. Saving enabled settings must revalidate and reapply them.
    if(disabling && !recoveryNeeded && old.contextMenu==settings.contextMenu
        && old.defaultFolders==settings.defaultFolders && old.defaultComputer==settings.defaultComputer
        && old.explorerWindowTakeover==settings.explorerWindowTakeover)return true;
    wchar_t executable[32768]{};if(!GetModuleFileNameW(nullptr,executable,_countof(executable)))return false;
    std::vector<TreeSnapshot> trees;
    struct DefaultSnapshot {std::wstring shell;RegistryValue value;};std::vector<DefaultSnapshot> defaults;
    const wchar_t* flagNames[]={L"IntegrationMenu",L"IntegrationFolders",L"IntegrationComputer",L"IntegrationExplorerWindows"};
    RegistryValue flags[4];for(int i=0;i<4;++i)flags[i]=GetRaw(L"Software\\FastFile",flagNames[i]);
    const std::wstring runKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* runName=L"FastFile.DefaultManager";
    const auto runBefore=GetRaw(runKey,runName);
    std::wstring runCommand;ReadRegString(HKEY_CURRENT_USER,runKey,runName,runCommand);
    const bool ownedRun=IsAgentRunCommand(runCommand) || (!runCommand.empty() && IsFastFileCommand(runCommand)
        && runCommand.find(L"--background")!=std::wstring::npos);
    if(settings.explorerWindowTakeover && runBefore.exists && !ownedRun)return false;
    // Validate all private verb keys before mutating anything; a foreign same-name entry
    // is never overwritten, even when the user asks to enable the handler.
    for(const auto& cls:integrationClasses) {
        const auto verb=ClassShellKey(cls.cls)+L"\\"+kSettingsVerb;
        if(KeyExists(verb) && !OwnSettingsVerb(verb))return false;
        trees.emplace_back(verb);trees.emplace_back(std::wstring(kIntegrationRoot)+L"\\"+cls.id);
        const auto shell=ClassShellKey(cls.cls);defaults.push_back({shell,GetRaw(shell,nullptr)});
    }
    for(const auto* cls:{L"Directory",L"Drive"})for(const auto* action:kFolderActions) {
        const auto verb=ClassShellKey(cls)+L"\\"+action;
        trees.emplace_back(verb);
    }
    trees.emplace_back(std::wstring(kIntegrationRoot)+L"\\Actions");
    auto rollback=[&] {
        for(auto it=trees.rbegin();it!=trees.rend();++it)it->Restore();
        for(const auto& entry:defaults)PutRaw(entry.shell,nullptr,entry.value);
        for(int i=0;i<4;++i)PutRaw(L"Software\\FastFile",flagNames[i],flags[i]);
        PutRaw(runKey,runName,runBefore);
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
    const auto folderCommand=L"\""+std::wstring(executable)+L"\" --shell-folder \"%1\"";
    for(const auto* cls:{L"Directory",L"Drive"})for(const auto* action:kFolderActions) {
        const auto backup=std::wstring(kIntegrationRoot)+L"\\Actions\\"+cls+L"\\"+action;
        if(!ApplyFolderAction(ClassShellKey(cls)+L"\\"+action,backup,folderCommand,settings.defaultFolders))return rollback();
    }
    if(!settings.defaultFolders && !DeleteTree(std::wstring(kIntegrationRoot)+L"\\Actions"))return rollback();
    if(!WriteRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",flagNames[0],settings.contextMenu)
        || !WriteRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",flagNames[1],settings.defaultFolders)
        || !WriteRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",flagNames[2],settings.defaultComputer)
        || !WriteRegDword(HKEY_CURRENT_USER,L"Software\\FastFile",flagNames[3],settings.explorerWindowTakeover))return rollback();
    if(settings.explorerWindowTakeover) {
        if(!WriteRegString(HKEY_CURRENT_USER,runKey,runName,L"\""+ExplorerAgentPath()+L"\""))return rollback();
    } else if(ownedRun && !PutRaw(runKey,runName,RegistryValue{}))return rollback();
    NotifyAssociationChanged();return true;
}

// ---------------------------------------------------------------------------------------
// Busy-receiver handoff.  A second process first tries WM_COPYDATA; when the running
// window does not answer in time (for example while a sleeping disk spins up) it writes
// the request here and posts kMsgDrainOpenQueue, which is processed as soon as the
// window is free.  Nothing in this path ever starts Explorer or drops the request.
std::wstring CMainWnd::OpenQueueDirectory()
{
    wchar_t appdata[MAX_PATH]{};
    const DWORD n = ::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::wstring(appdata) + L"\\FastFile\\OpenQueue";
}

std::wstring CMainWnd::QueueExternalOpen(const std::vector<std::wstring>& paths)
{
    const std::wstring dir = OpenQueueDirectory();
    if (dir.empty() || paths.empty()) return {};
    ::SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    std::wstring payload;
    for (const auto& path : paths) {
        if (path.empty() || path.find_first_of(L"\r\n") != std::wstring::npos) continue;
        payload += path;
        payload += L"\n";
    }
    if (payload.empty()) return {};
    wchar_t name[96]{};
    swprintf_s(name, L"%016llX-%08lX", static_cast<unsigned long long>(::GetTickCount64()),
        ::GetCurrentProcessId());
    const std::wstring temp = dir + L"\\" + name + L".tmp";
    const std::wstring ready = dir + L"\\" + name + L".open";
    HANDLE file = ::CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    DWORD written = 0;
    const DWORD bytes = static_cast<DWORD>(payload.size() * sizeof(wchar_t));
    const BOOL ok = ::WriteFile(file, payload.data(), bytes, &written, nullptr) && written == bytes;
    ::CloseHandle(file);
    // Publish atomically: the receiver only reads complete *.open files.
    if (!ok || !::MoveFileExW(temp.c_str(), ready.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        ::DeleteFileW(temp.c_str());
        return {};
    }
    return ready;
}

std::vector<std::wstring> CMainWnd::DrainExternalOpenQueue()
{
    std::vector<std::wstring> paths;
    const std::wstring dir = OpenQueueDirectory();
    if (dir.empty()) return paths;
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW data{};
    HANDLE find = ::FindFirstFileW((dir + L"\\*.open").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return paths;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(data.cFileName);
    } while (::FindNextFileW(find, &data));
    ::FindClose(find);
    std::sort(files.begin(), files.end()); // names start with the request tick: oldest first
    FILETIME nowFile{};
    ::GetSystemTimeAsFileTime(&nowFile);
    const ULONGLONG now = (ULONGLONG(nowFile.dwHighDateTime) << 32) | nowFile.dwLowDateTime;
    for (const auto& name : files) {
        const std::wstring path = dir + L"\\" + name;
        HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue; // another window is reading it
        FILETIME written{};
        ::GetFileTime(file, nullptr, nullptr, &written);
        const ULONGLONG age = now - ((ULONGLONG(written.dwHighDateTime) << 32) | written.dwLowDateTime);
        LARGE_INTEGER size{};
        std::wstring text;
        if (::GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20)) {
            text.resize(static_cast<size_t>(size.QuadPart) / sizeof(wchar_t));
            DWORD read = 0;
            if (!::ReadFile(file, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &read, nullptr))
                text.clear();
            text.resize(read / sizeof(wchar_t));
        }
        ::CloseHandle(file);
        ::DeleteFileW(path.c_str());
        // A request left behind by a window that closed long ago is not replayed.
        if (age > 10ULL * 60 * 10000000ULL) continue;
        size_t begin = 0;
        while (begin < text.size()) {
            size_t end = text.find(L'\n', begin);
            if (end == std::wstring::npos) end = text.size();
            if (end > begin) paths.emplace_back(text.substr(begin, end - begin));
            begin = end + 1;
        }
    }
    return paths;
}

// ---------------------------------------------------------------------------------------
// Shell window registration.  Only while FastFile is the default folder handler: then
// "show in folder" requests from other applications launch FastFile, and the Shell must
// be able to find the FastFile window showing that folder.  Otherwise Explorer keeps
// handling those requests and FastFile stays out of the Shell window list.
void CMainWnd::UpdateShellWindowRegistration()
{
    FastFileSettings integration;
    ReadSystemIntegration(integration);
    const bool wanted = s_shellWindowRegistrationAllowed && integration.defaultFolders
        && m_hWnd && ::IsWindow(m_hWnd);
    if (!wanted) {
        if (m_shellWindow) m_shellWindow->Revoke();
        m_shellWindowPath.clear();
        return;
    }
    if (!m_shellWindow) m_shellWindow = new (std::nothrow) ShellWindowRegistration;
    if (!m_shellWindow) return;
    if (!m_shellWindow->IsRegistered()) {
        const std::wstring path = m_currentPath.empty() ? std::wstring(kThisPcPath) : m_currentPath;
        if (!RegisterShellWindowAt(path)) return;
    }
    NotifyShellWindowLocation();
}

static PIDLIST_ABSOLUTE ShellWindowLocationFor(const std::wstring& path, bool thisPc)
{
    PIDLIST_ABSOLUTE location = nullptr;
    if (path.empty() || thisPc)
        ::SHGetKnownFolderIDList(FOLDERID_ComputerFolder, 0, nullptr, &location);
    else
        ::SHParseDisplayName(path.c_str(), nullptr, &location, 0, nullptr);
    return location;
}

bool CMainWnd::RegisterShellWindowAt(const std::wstring& path)
{
    if (!m_shellWindow) return false;
    PIDLIST_ABSOLUTE location = ShellWindowLocationFor(path, IsThisPcPath(path));
    if (!location) return false;
    const HRESULT hr = m_shellWindow->Register(m_hWnd, location,
        [this](PCIDLIST_ABSOLUTE item, UINT flags) { return OnShellWindowSelect(item, flags); },
        [this](PCIDLIST_ABSOLUTE folder) { return OnShellWindowNavigate(folder); });
    ::CoTaskMemFree(location);
    if (FAILED(hr)) { m_shellWindowPath.clear(); return false; }
    m_shellWindowPath = path;
    return true;
}

void CMainWnd::NotifyShellWindowLocation()
{
    if (!m_shellWindow || !m_shellWindow->IsRegistered() || m_currentPath.empty()) return;
    if (PathEquals(m_shellWindowPath, m_currentPath)) { m_shellWindowReannounce = false; return; }
    if (m_shellWindowReannounce) {
        m_shellWindowReannounce = false;
        RegisterShellWindowAt(m_currentPath);
        return;
    }
    PIDLIST_ABSOLUTE location = ShellWindowLocationFor(m_currentPath, IsThisPcPath(m_currentPath));
    if (!location) return;
    if (SUCCEEDED(m_shellWindow->Navigate(location))) m_shellWindowPath = m_currentPath;
    ::CoTaskMemFree(location);
}

HRESULT CMainWnd::OnShellWindowNavigate(PCIDLIST_ABSOLUTE folder)
{
    if(!folder || !m_shellWindow || !m_shellWindow->IsRegistered())return E_INVALIDARG;
    wchar_t path[32768]{};
    if(!SHGetPathFromIDListEx(folder,path,_countof(path),GPFIDL_DEFAULT) || !path[0])return E_INVALIDARG;
    // Acknowledge the exact Shell location before slow disk access/enumeration. A
    // following SelectItem is queued until the real view reaches the destination.
    const HRESULT hr=m_shellWindow->Navigate(folder);
    if(FAILED(hr))return hr;
    m_shellWindowPath=path;
    OpenExternalPaths({path},false);
    BringToForeground();
    return S_OK;
}

HRESULT CMainWnd::OnShellWindowSelect(PCIDLIST_ABSOLUTE item, UINT flags)
{
    if (!item) return E_INVALIDARG;
    PIDLIST_ABSOLUTE copy = ILCloneFull(item);
    if (!copy) return E_OUTOFMEMORY;
    if (m_pendingShellSelect) ::CoTaskMemFree(m_pendingShellSelect);
    m_pendingShellSelect = copy;
    m_pendingShellSelectFlags = flags;
    // The folder may still be enumerating (a large folder on a slow disk); keep trying
    // until the item is listed instead of failing the request.
    m_pendingShellSelectDeadline = ::GetTickCount64() + 20000;
    BringToForeground();
    if (!TryApplyPendingShellSelect() && m_hWnd)
        ::SetTimer(m_hWnd, kTimerShellSelect, 150, nullptr);
    return S_OK;
}

bool CMainWnd::TryApplyPendingShellSelect()
{
    if (!m_pendingShellSelect) return true;
    bool done = false;
    if (m_shellBrowser && m_shellBrowser->IsCreated())
        done = m_shellBrowser->SelectAbsoluteItem(m_pendingShellSelect, m_pendingShellSelectFlags) == S_OK;
    if (done || ::GetTickCount64() > m_pendingShellSelectDeadline) {
        ::CoTaskMemFree(m_pendingShellSelect);
        m_pendingShellSelect = nullptr;
        if (m_hWnd) ::KillTimer(m_hWnd, kTimerShellSelect);
        if (done) SyncShellViewSelection();
        return true;
    }
    return false;
}
