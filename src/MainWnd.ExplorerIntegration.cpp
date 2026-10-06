// Detect external Explorer windows independently of default Shell verbs.
#include "MainWndInternal.h"
#include <exdisp.h>
#include <shlguid.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include "ExplorerAgentProtocol.h"

using Microsoft::WRL::ComPtr;

struct ExplorerScanState {
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<CMainWnd::ExplorerSnapshot> snapshots;
    std::vector<CMainWnd::ExplorerSnapshot> closeRequests;
    std::vector<std::pair<CMainWnd::ExplorerSnapshot,bool>> closeResults;
    std::map<HWND,DWORD> ignoredWindows;
    ULONGLONG sequence=0;
    HRESULT error=S_OK;
};

bool (*CMainWnd::s_closeExplorerForTest)(const ExplorerSnapshot&)=nullptr;
std::wstring CMainWnd::s_explorerTestRoot;
std::wstring CMainWnd::s_agentInterfaceForTest;
std::wstring CMainWnd::s_agentArgumentsForTest;
HDESK CMainWnd::s_agentDesktopForTest=nullptr;

namespace {
void AgentTrace(const wchar_t* event,const std::wstring& folder);
bool IsExplorerWindow(HWND window,DWORD* processId=nullptr) {
    wchar_t name[64]{};GetClassNameW(window,name,_countof(name));
    if(wcscmp(name,L"CabinetWClass") && wcscmp(name,L"ExploreWClass"))return false;
    DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return false;
    wchar_t actual[32768]{},system[32768]{};DWORD size=_countof(actual);
    const bool queried=QueryFullProcessImageNameW(process,0,actual,&size)!=FALSE;
    CloseHandle(process);GetWindowsDirectoryW(system,_countof(system));
    if(!queried || _wcsicmp(actual,(std::wstring(system)+L"\\explorer.exe").c_str()))return false;
    if(processId)*processId=pid;
    return true;
}

bool ReadExplorerFolder(IDispatch* dispatch,CMainWnd::ExplorerSnapshot& result,
    const std::map<HWND,DWORD>& ignored,HWND only) {
    ComPtr<IWebBrowserApp> app;
    if(FAILED(dispatch->QueryInterface(IID_PPV_ARGS(app.GetAddressOf()))))return false;
    SHANDLE_PTR value=0;if(FAILED(app->get_HWND(&value)))return false;
    result.window=GetAncestor(reinterpret_cast<HWND>(value),GA_ROOT);
    if(only && result.window!=only)return false;
    if(!IsExplorerWindow(result.window,&result.processId))return false;
    const auto known=ignored.find(result.window);
    if(known!=ignored.end() && known->second==result.processId)return true;
    ComPtr<IServiceProvider> service;ComPtr<IShellBrowser> browser;
    ComPtr<IShellView> view;ComPtr<IFolderView2> folderView;ComPtr<IPersistFolder2> folder;
    // Keep native windows with unavailable/virtual folders in the baseline too.
    if(FAILED(dispatch->QueryInterface(IID_PPV_ARGS(service.GetAddressOf())))
        || FAILED(service->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(browser.GetAddressOf())))
        || FAILED(browser->QueryActiveShellView(view.GetAddressOf()))
        || FAILED(view.As(&folderView))
        || FAILED(folderView->GetFolder(IID_PPV_ARGS(folder.GetAddressOf()))))return true;
    PIDLIST_ABSOLUTE location=nullptr;
    if(SUCCEEDED(folder->GetCurFolder(&location)) && location) {
        wchar_t path[32768]{};
        if(SHGetPathFromIDListEx(location,path,_countof(path),GPFIDL_DEFAULT))result.path=path;
        else {
            PIDLIST_ABSOLUTE computer=nullptr;
            if(SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_ComputerFolder,0,nullptr,&computer)) && computer) {
                if(ILIsEqual(location,computer))result.path=L"::ThisPC";
                CoTaskMemFree(computer);
            }
        }
        CoTaskMemFree(location);
    }
    int selected=0;
    if(FAILED(folderView->ItemCount(SVGIO_SELECTION,&selected)) || selected>256){result.path.clear();return true;}
    if(selected>0) {
        ComPtr<IShellItemArray> items;
        if(FAILED(folderView->GetSelection(FALSE,items.GetAddressOf())) || !items){result.path.clear();return true;}
        DWORD count=0;if(FAILED(items->GetCount(&count)) || count>256){result.path.clear();return true;}
        for(DWORD i=0;i<count;++i) {
            ComPtr<IShellItem> item;PWSTR path=nullptr;
            if(FAILED(items->GetItemAt(i,item.GetAddressOf()))
                || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) {
                result.path.clear();result.selection.clear();return true;
            }
            result.selection.emplace_back(path);CoTaskMemFree(path);
            PIDLIST_ABSOLUTE id=nullptr;
            if(FAILED(SHGetIDListFromObject(item.Get(),&id)) || !id) {
                result.path.clear();result.selection.clear();result.selectionIds.clear();return true;
            }
            const auto bytes=reinterpret_cast<const BYTE*>(id);
            result.selectionIds.emplace_back(bytes,bytes+ILGetSize(id));CoTaskMemFree(id);
        }
    }
    return true;
}

bool ScanExplorer(std::vector<CMainWnd::ExplorerSnapshot>& snapshots,const std::wstring& testRoot,
    const std::map<HWND,DWORD>& ignored={},HWND only=nullptr) {
    ComPtr<IShellWindows> windows;
    const HRESULT created=CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_ALL,IID_PPV_ARGS(windows.GetAddressOf()));
    if(FAILED(created)){if(!testRoot.empty())AgentTrace(L"test-scan-com-failed",std::to_wstring(static_cast<unsigned long>(created)));return false;}
    long count=0;const HRESULT counted=windows->get_Count(&count);
    if(FAILED(counted)){if(!testRoot.empty())AgentTrace(L"test-scan-count-failed",std::to_wstring(static_cast<unsigned long>(counted)));return false;}
    for(long i=0;i<count;++i) {
        VARIANT index{};index.vt=VT_I4;index.lVal=i;ComPtr<IDispatch> dispatch;
        const HRESULT fetched=windows->Item(index,dispatch.GetAddressOf());
        // A missing/pending collection entry is S_FALSE, not a scan failure.
        // Existing windows are protected by the independent HWND baseline, and
        // closure still requires the expected source to be readable and unique.
        if(fetched==S_FALSE && !dispatch) {
            if(!testRoot.empty())AgentTrace(L"test-scan-empty-entry-skipped",std::to_wstring(i));
            continue;
        }
        if(fetched!=S_OK || !dispatch){if(!testRoot.empty())AgentTrace(L"test-scan-item-failed",std::to_wstring(i)+L" hr="+std::to_wstring(static_cast<unsigned long>(fetched)));return false;}
        CMainWnd::ExplorerSnapshot snapshot;
        if(dispatch && ReadExplorerFolder(dispatch.Get(),snapshot,ignored,only)) {
            if(!testRoot.empty() && (snapshot.path.size()<testRoot.size()
                || _wcsnicmp(snapshot.path.c_str(),testRoot.c_str(),testRoot.size())))continue;
            snapshots.push_back(std::move(snapshot));
        }
    }
    long finalCount=0;
    return SUCCEEDED(windows->get_Count(&finalCount)) && finalCount==count;
}

bool CloseTransferredExplorer(const CMainWnd::ExplorerSnapshot& expected,const std::function<bool()>& enabled) {
    DWORD pid=0;
    if(!IsExplorerWindow(expected.window,&pid) || pid!=expected.processId)return false;
    auto idle=[&] {
        const HWND popup=GetLastActivePopup(expected.window);
        return IsWindowEnabled(expected.window) && (popup==expected.window || !IsWindowVisible(popup));
    };
    if(!idle())return false;
    // Recheck the source immediately before closing. Do not close a navigated or
    // multi-view window. UI Automation also protects Explorer's hidden tabs.
    std::vector<CMainWnd::ExplorerSnapshot> current;
    if(!ScanExplorer(current,L"",{},expected.window))return false;
    int matching=0;
    for(const auto& item:current)if(item.window==expected.window) {
        if(_wcsicmp(item.path.c_str(),expected.path.c_str()) || item.selection!=expected.selection)return false;
        ++matching;
    }
    if(matching!=1)return false;
    ComPtr<IUIAutomation> automation;ComPtr<IUIAutomationElement> element;
    ComPtr<IUIAutomationCondition> condition;ComPtr<IUIAutomationElementArray> tabs;
    if(FAILED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(automation.GetAddressOf())))
        || FAILED(automation->ElementFromHandle(expected.window,element.GetAddressOf())))return false;
    VARIANT type{};type.vt=VT_I4;type.lVal=UIA_TabItemControlTypeId;
    if(FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,condition.GetAddressOf()))
        || FAILED(element->FindAll(TreeScope_Descendants,condition.Get(),tabs.GetAddressOf())))return false;
    int tabCount=0;if(FAILED(tabs->get_Length(&tabCount)) || tabCount>1)return false;
    // UI Automation may pump messages. Recheck the active folder/selection and
    // busy state after it, immediately before requesting closure.
    current.clear();
    if(!ScanExplorer(current,L"",{},expected.window) || current.size()!=1
        || _wcsicmp(current.front().path.c_str(),expected.path.c_str())
        || current.front().selection!=expected.selection || !idle())return false;
    // Never terminate explorer.exe: it also hosts the desktop and taskbar.
    return enabled() && PostMessageW(expected.window,WM_CLOSE,0,0)!=FALSE;
}
}

namespace {
void AgentTrace(const wchar_t* event,const std::wstring& folder=L"") {
    auto directory=FastFileSettings::FilePath();const auto slash=directory.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return;
    directory.resize(slash);SHCreateDirectoryExW(nullptr,directory.c_str(),nullptr);
    const auto path=directory+L"\\explorer-agent.log";
    HANDLE file=CreateFileW(path.c_str(),FILE_APPEND_DATA|GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    LARGE_INTEGER size{};
    if(GetFileSizeEx(file,&size) && size.QuadPart>2*1024*1024) {
        CloseHandle(file);file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
    }
    SYSTEMTIME now{};GetLocalTime(&now);wchar_t stamp[128]{};
    swprintf_s(stamp,L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu ",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId());
    const auto line=std::wstring(stamp)+event+L" folder="+folder+L"\r\n";
    const int count=WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),nullptr,0,nullptr,nullptr);
    if(count>0) {
        std::string bytes(count,0);WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),bytes.data(),count,nullptr,nullptr);
        DWORD written=0;WriteFile(file,bytes.data(),count,&written,nullptr);
    }
    CloseHandle(file);
}
std::wstring AgentMutexName() {
    auto profile=FastFileSettings::FilePath();
    for(auto& ch:profile)if(ch==L'\\' || ch==L'/')ch=L'_';
    return L"Local\\FastFile.ExplorerAgent."+profile;
}
bool AgentEnabled() {
    FastFileSettings settings;CMainWnd::ReadSystemIntegration(settings);
    return settings.defaultFolders && settings.explorerWindowTakeover;
}
bool LaunchAgentProcess(const std::wstring& executable,const std::wstring& arguments) {
    if(executable.empty() || GetFileAttributesW(executable.c_str())==INVALID_FILE_ATTRIBUTES)return false;
    auto command=L"\""+executable+L"\""+arguments;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process))return false;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return true;
}
HWND AgentInterfaceWindow(HDESK desktop) {
    struct Search { std::wstring executable;HWND window=nullptr; } search{CMainWnd::ExplorerAgentInterfacePath()};
    auto enumerate=[](HWND window,LPARAM value)->BOOL {
        auto& search=*reinterpret_cast<Search*>(value);wchar_t name[128]{};
        GetClassNameW(window,name,_countof(name));if(wcscmp(name,L"FastFile_MainWnd"))return TRUE;
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(!process)return TRUE;
        wchar_t path[32768]{};DWORD count=_countof(path);
        const bool match=QueryFullProcessImageNameW(process,0,path,&count) && !_wcsicmp(path,search.executable.c_str());
        CloseHandle(process);if(!match)return TRUE;
        search.window=window;return FALSE;
    };
    if(desktop)EnumDesktopWindows(desktop,enumerate,reinterpret_cast<LPARAM>(&search));
    else EnumWindows(enumerate,reinterpret_cast<LPARAM>(&search));
    return search.window;
}
bool AgentSend(HWND window,ULONG_PTR operation,const std::vector<wchar_t>& payload) {
    if(!window || payload.empty())return false;
    COPYDATASTRUCT data{operation,static_cast<DWORD>(payload.size()*sizeof(wchar_t)),const_cast<wchar_t*>(payload.data())};
    DWORD_PTR result=0;
    return SendMessageTimeoutW(window,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data),
        SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&result) && result==1;
}
}

std::wstring CMainWnd::ExplorerAgentPath() { return ExplorerAgentProtocol::Sibling(L"FastFileAgent.exe"); }
std::wstring CMainWnd::ExplorerAgentInterfacePath() {
    return s_agentInterfaceForTest.empty()?ExplorerAgentProtocol::Sibling(L"FastFile.exe"):s_agentInterfaceForTest;
}
bool CMainWnd::StartExplorerAgent() {
    HANDLE existing=OpenMutexW(SYNCHRONIZE,FALSE,AgentMutexName().c_str());
    if(existing){CloseHandle(existing);return true;}
    return LaunchAgentProcess(ExplorerAgentPath(),L"");
}
int CMainWnd::RunExplorerAgent() {
    HANDLE singleton=CreateMutexW(nullptr,FALSE,AgentMutexName().c_str());
    const auto mutexError=GetLastError();
    if(!singleton)return 2;
    if(mutexError==ERROR_ALREADY_EXISTS){CloseHandle(singleton);return 0;}
    if(FAILED(OleInitialize(nullptr))){CloseHandle(singleton);return 3;}
    // Baseline includes unavailable folders, and persists across partial COM scans.
    std::map<HWND,DWORD> protectedWindows;
    EnumWindows([](HWND window,LPARAM value)->BOOL {
        DWORD pid=0;if(IsExplorerWindow(window,&pid))(*reinterpret_cast<std::map<HWND,DWORD>*>(value))[window]=pid;
        return TRUE;
    },reinterpret_cast<LPARAM>(&protectedWindows));
    AgentTrace(L"baseline-ready");
    struct Pending { ExplorerSnapshot source;ULONGLONG changed=0,deadline=0;bool opened=false,launched=false;HWND receiver=nullptr; };
    std::map<HWND,Pending> pending;
    bool scanFailed=false;
    while(AgentEnabled()) {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        std::vector<ExplorerSnapshot> snapshots;
        if(ScanExplorer(snapshots,s_explorerTestRoot,protectedWindows)) {
            if(scanFailed){AgentTrace(L"scan-recovered");scanFailed=false;}
            const auto now=GetTickCount64();std::map<HWND,bool> seen;
            for(const auto& source:snapshots) {
                seen[source.window]=true;
                const auto known=protectedWindows.find(source.window);
                if(known!=protectedWindows.end() && known->second==source.processId)continue;
                auto& item=pending[source.window];
                if(item.source.processId!=source.processId || item.source.path!=source.path || item.source.selection!=source.selection) {
                    const bool launched=item.launched && item.source.processId==source.processId;
                    item=Pending{};item.source=source;item.changed=now;item.launched=launched;
                    AgentTrace(L"discovered",source.path);
                }
            }
            for(auto iterator=pending.begin();iterator!=pending.end();) {
                if(!seen.count(iterator->first))iterator=pending.erase(iterator);else ++iterator;
            }
            // One request at a time prevents another navigation racing confirmation.
            for(auto& pair:pending) {
                auto& item=pair.second;if(item.source.path.empty() || now-item.changed<1000)continue;
                if(!item.deadline)item.deadline=now+20000;
                if(now>item.deadline){AgentTrace(L"retained-timeout",item.source.path);protectedWindows[pair.first]=item.source.processId;pending.erase(pair.first);break;}
                std::vector<std::wstring> paths{item.source.path};paths.insert(paths.end(),item.source.selection.begin(),item.source.selection.end());
                const auto payload=ExplorerAgentProtocol::Encode(paths);
                if(payload.empty()){AgentTrace(L"retained-payload-limit",item.source.path);protectedWindows[pair.first]=item.source.processId;pending.erase(pair.first);break;}
                const HWND receiver=AgentInterfaceWindow(s_agentDesktopForTest);
                if(!receiver) {
                    if(!item.launched && item.source.path.find(L'"')==std::wstring::npos) {
                        // A different installation's window must not intercept this launch.
                        item.launched=LaunchAgentProcess(ExplorerAgentInterfacePath(),s_agentArgumentsForTest+L" --new-window --shell-folder \""+item.source.path+L"\"");
                        AgentTrace(item.launched?L"interface-started":L"interface-start-failed",item.source.path);
                    }
                    break;
                }
                if(item.receiver!=receiver) {
                    item.receiver=receiver;
                    // A cold launch already delivered the folder via its command line.
                    // Sending Open again creates a duplicate tab when reuse is disabled.
                    item.opened=item.launched;
                }
                if(!item.opened) {
                    item.opened=AgentSend(receiver,ExplorerAgentProtocol::Open,payload);
                    if(item.opened)AgentTrace(L"request-accepted",item.source.path);
                }
                else if(AgentSend(receiver,ExplorerAgentProtocol::Confirm,payload)) {
                    AgentTrace(CloseTransferredExplorer(item.source,AgentEnabled)?L"confirmed-close-posted":L"confirmed-source-retained",item.source.path);
                    protectedWindows[pair.first]=item.source.processId;pending.erase(pair.first);
                }
                break;
            }
        }
        else if(!scanFailed){AgentTrace(L"scan-unavailable-source-retained");scanFailed=true;}
        MsgWaitForMultipleObjects(0,nullptr,FALSE,500,QS_ALLINPUT);
    }
    AgentTrace(L"disabled-exit");OleUninitialize();CloseHandle(singleton);return 0;
}

LRESULT CMainWnd::HandleExplorerAgentMessage(const COPYDATASTRUCT& data) {
    std::vector<std::wstring> paths;
    if(!ExplorerAgentProtocol::Decode(data,paths) || !AgentEnabled())return 0;
    if(data.dwData==ExplorerAgentProtocol::Open) {
        if(m_externalOpensPending)return 0;
        OpenExternalPaths({paths.front()},false);EnsureMainWindowVisible();return 1;
    }
    if(m_externalOpensPending || !m_shellBrowser || !PathEquals(m_currentPath,paths.front())
        || !m_shellBrowser->IsNavigationCompleteAt(paths.front()) || !IsWindowVisible(m_hWnd) || IsIconic(m_hWnd))return 0;
    if(!m_shellBrowser->HasVisibleViewBounds())return 0;
    if(paths.size()==1 && !m_shellBrowser->ClearSelection())return 0;
    for(size_t i=1;i<paths.size();++i) {
        // A source may select only direct children of the requested folder.
        const auto slash=paths[i].find_last_of(L"\\/");
        const auto parent=paths[i].substr(0,slash==2?3:slash);
        if(!PathEquals(parent,paths.front()))return 0;
        PIDLIST_ABSOLUTE id=nullptr;
        if(FAILED(SHParseDisplayName(paths[i].c_str(),nullptr,&id,0,nullptr)) || !id)return 0;
        const auto result=m_shellBrowser->SelectAbsoluteItem(id,SVSI_SELECT|SVSI_ENSUREVISIBLE|(i==1?SVSI_DESELECTOTHERS:0));
        CoTaskMemFree(id);if(result!=S_OK)return 0;
    }
    if(paths.size()>1) {
        std::vector<std::pair<std::wstring,bool>> actual;
        if(!m_shellBrowser->GetSelection(actual) || actual.size()!=paths.size()-1)return 0;
        for(size_t i=1;i<paths.size();++i) {
            bool found=false;for(const auto& selected:actual)if(PathEquals(selected.first,paths[i]))found=true;
            if(!found)return 0;
        }
    }
    m_shellBrowser->FlushPaint();return 1;
}

void CMainWnd::StopExplorerTakeover() {
    if(m_hWnd)KillTimer(m_hWnd,kTimerExplorerTakeover);
    if(m_explorerScan){m_explorerScan->stop=true;m_explorerScan->wake.notify_all();}
    m_explorerScan.reset();m_explorerTransfers.clear();m_existingExplorerWindows.clear();m_explorerBaseline=false;m_explorerScanSequence=0;
    if(m_integrationMonitorMutex){CloseHandle(m_integrationMonitorMutex);m_integrationMonitorMutex=nullptr;}
}
std::wstring CMainWnd::ExplorerTakeoverStatus() const {
    if(!m_settings.explorerWindowTakeover)return L"窗口转交：当前未启用。";
    if(m_explorerAgentAllowed)return L"窗口转交：由独立后台代理检测；关闭文件管理器窗口会退出界面进程。";
    if(!m_explorerScan)return L"窗口转交：检测未启动，请修复并应用。";
    std::lock_guard<std::mutex> lock(m_explorerScan->mutex);
    if(FAILED(m_explorerScan->error))return L"窗口转交：无法读取资源管理器状态，原窗口将保留。";
    if(!m_explorerScan->sequence)return L"窗口转交：正在建立已有窗口基线…";
    return L"窗口转交：检测运行中（仅处理之后新开的文件夹窗口）。";
}

void CMainWnd::UpdateExplorerTakeover() {
    if(!m_settings.explorerWindowTakeover || !m_hWnd){StopExplorerTakeover();return;}
    if(m_explorerScan)return;
    if(m_explorerAgentAllowed) {
        if(!StartExplorerAgent())UpdateStatus(L"后台代理启动失败，请检查安装文件");
        return;
    }
    // Capture existing top-level windows independently of the non-atomic COM
    // enumeration. Their protection survives a missed/partial later scan.
    EnumWindows([](HWND window,LPARAM value)->BOOL {
        DWORD pid=0;
        if(IsExplorerWindow(window,&pid))(*reinterpret_cast<std::map<HWND,DWORD>*>(value))[window]=pid;
        return TRUE;
    },reinterpret_cast<LPARAM>(&m_existingExplorerWindows));
    auto state=std::make_shared<ExplorerScanState>();m_explorerScan=state;
    state->ignoredWindows=m_existingExplorerWindows;
    const auto testRoot=s_explorerTestRoot;
    try {
        // Discovery runs off the UI thread. Its shared state outlives a slow COM
        // call; disabling/closing never waits for another process to respond.
        std::thread([state,testRoot] {
            const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            if(FAILED(initialized)){std::lock_guard<std::mutex> lock(state->mutex);state->error=initialized;return;}
            while(!state->stop) {
                std::vector<ExplorerSnapshot> snapshots;
                std::map<HWND,DWORD> ignored;
                {std::lock_guard<std::mutex> lock(state->mutex);ignored=state->ignoredWindows;}
                const bool scanned=ScanExplorer(snapshots,testRoot,ignored);
                if(scanned && !state->stop) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->snapshots=std::move(snapshots);++state->sequence;state->error=S_OK;
                } else if(!state->stop) {std::lock_guard<std::mutex> lock(state->mutex);state->error=E_FAIL;}
                std::vector<ExplorerSnapshot> requests;
                {std::lock_guard<std::mutex> lock(state->mutex);requests.swap(state->closeRequests);}
                for(const auto& request:requests) {
                    const bool closed=!state->stop && CloseTransferredExplorer(request,[state]{return !state->stop.load();});
                    if(!state->stop) {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        state->closeResults.emplace_back(request,closed);
                    }
                }
                std::unique_lock<std::mutex> lock(state->mutex);
                state->wake.wait_for(lock,std::chrono::milliseconds(750),[&]{return state->stop.load();});
            }
            CoUninitialize();
        }).detach();
        if(!SetTimer(m_hWnd,kTimerExplorerTakeover,250,nullptr)) {
            StopExplorerTakeover();UpdateStatus(L"无法启动资源管理器窗口检测");
        }
    } catch(...) {StopExplorerTakeover();UpdateStatus(L"无法启动资源管理器窗口检测");}
}

void CMainWnd::PollExplorerTakeover() {
    if(!m_explorerScan || m_explorerPollBusy)return;
    if(m_explorerAgentAllowed) {
        FastFileSettings current;ReadSystemIntegration(current);
        if(!current.explorerWindowTakeover || !current.defaultFolders) {
            m_settings.explorerWindowTakeover=false;StopExplorerTakeover();
            if(!IsWindowVisible(m_hWnd))::PostMessageW(m_hWnd,WM_CLOSE,0,0);
            return;
        }
    }
    struct Guard {bool& busy;Guard(bool& value):busy(value){busy=true;}~Guard(){busy=false;}} guard(m_explorerPollBusy);
    std::vector<ExplorerSnapshot> snapshots;ULONGLONG sequence=0;
    std::vector<std::pair<ExplorerSnapshot,bool>> completed;
    HRESULT scanError=S_OK;
    {
        std::lock_guard<std::mutex> lock(m_explorerScan->mutex);
        sequence=m_explorerScan->sequence;snapshots=m_explorerScan->snapshots;
        completed.swap(m_explorerScan->closeResults);
        scanError=m_explorerScan->error;
    }
    for(const auto& result:completed) {
        if(result.second){BringToForeground();UpdateStatus(L"已将资源管理器文件夹转交给 FastFile");}
        else UpdateStatus(L"文件夹已转交；原窗口状态变化或包含其他标签，已保留");
    }
    if(!sequence || FAILED(scanError))return;
    // Reusing the latest observation permits a pending FastFile navigation to
    // finish between background scans; source identity is rechecked on close.
    ProcessExplorerSnapshots(snapshots,!m_explorerBaseline);
    m_explorerBaseline=true;m_explorerScanSequence=sequence;
    if(m_explorerScan) {
        std::lock_guard<std::mutex> lock(m_explorerScan->mutex);
        m_explorerScan->ignoredWindows=m_existingExplorerWindows;
        for(const auto& entry:m_explorerTransfers)if(entry.second.ignored)
            m_explorerScan->ignoredWindows[entry.first]=entry.second.source.processId;
    }
}

void CMainWnd::ProcessExplorerSnapshots(const std::vector<ExplorerSnapshot>& snapshots,bool baseline) {
    if(!m_settings.explorerWindowTakeover)return;
    const auto now=GetTickCount64();
    for(auto it=m_existingExplorerWindows.begin();it!=m_existingExplorerWindows.end();) {
        DWORD pid=0;GetWindowThreadProcessId(it->first,&pid);
        if(!IsWindow(it->first) || pid!=it->second)it=m_existingExplorerWindows.erase(it);else ++it;
    }
    for(auto it=m_explorerTransfers.begin();it!=m_explorerTransfers.end();) {
        bool present=false;for(const auto& snapshot:snapshots)
            if(snapshot.window==it->first && snapshot.processId==it->second.source.processId){present=true;break;}
        if(!present)it=m_explorerTransfers.erase(it);else ++it;
    }
    for(const auto& snapshot:snapshots) {
        if(baseline)m_existingExplorerWindows[snapshot.window]=snapshot.processId;
        auto [it,inserted]=m_explorerTransfers.try_emplace(snapshot.window);
        auto& transfer=it->second;
        const auto existing=m_existingExplorerWindows.find(snapshot.window);
        if(inserted) {
            transfer.source=snapshot;transfer.changedAt=now;
            transfer.ignored=baseline || (existing!=m_existingExplorerWindows.end() && existing->second==snapshot.processId);
        }
        else if(!PathEquals(transfer.source.path,snapshot.path) || transfer.source.selection!=snapshot.selection) {
            transfer.source=snapshot;transfer.changedAt=now;transfer.started=false;transfer.resolved=false;
        }
    }
    std::vector<HWND> candidates;
    for(const auto& entry:m_explorerTransfers)candidates.push_back(entry.first);
    for(HWND candidate:candidates) {
        auto entry=m_explorerTransfers.find(candidate);
        if(entry==m_explorerTransfers.end() || !m_settings.explorerWindowTakeover)continue;
        auto& transfer=entry->second;
        if(transfer.ignored || transfer.source.path.empty())continue;
        if(!transfer.started) {
            bool occupied=false;for(const auto& other:m_explorerTransfers)if(other.second.started && !other.second.ignored)occupied=true;
            if(occupied || m_externalOpensPending || now-transfer.changedAt<1000)continue;
            transfer.started=true;transfer.resolved=false;transfer.deadline=now+20000;
            OpenExternalPaths({transfer.source.path},false);EnsureMainWindowVisible();
            continue;
        }
        if(now>transfer.deadline) {
            transfer.ignored=true;UpdateStatus(L"文件夹转交未完成，已保留资源管理器窗口");continue;
        }
        const auto expected=transfer.source;
        if(!transfer.resolved || m_externalOpensPending || !PathEquals(m_currentPath,expected.path)
            || !m_shellBrowser || !m_shellBrowser->IsNavigationCompleteAt(expected.path))continue;
        bool selected=true;
        for(size_t i=0;i<expected.selection.size();++i) {
            PIDLIST_ABSOLUTE item=nullptr;
            if(i<expected.selectionIds.size() && !expected.selectionIds[i].empty())
                item=ILCloneFull(reinterpret_cast<PCIDLIST_ABSOLUTE>(expected.selectionIds[i].data()));
            else if(s_closeExplorerForTest)SHParseDisplayName(expected.selection[i].c_str(),nullptr,&item,0,nullptr);
            if(!item){selected=false;break;}
            const UINT flags=SVSI_SELECT|SVSI_ENSUREVISIBLE|(i?0:SVSI_DESELECTOTHERS);
            const HRESULT result=m_shellBrowser->SelectAbsoluteItem(item,flags);CoTaskMemFree(item);
            if(result!=S_OK){selected=false;break;}
        }
        if(!selected)continue;
        if(!expected.selection.empty()) {
            std::vector<std::pair<std::wstring,bool>> actual;
            if(!m_shellBrowser->GetSelection(actual) || actual.size()!=expected.selection.size())continue;
            for(const auto& path:expected.selection) {
                bool found=false;for(const auto& item:actual)if(PathEquals(item.first,path))found=true;
                if(!found)selected=false;
            }
            if(!selected)continue;
        }
        entry=m_explorerTransfers.find(candidate);
        if(entry==m_explorerTransfers.end() || !m_settings.explorerWindowTakeover)return;
        entry->second.ignored=true;
        const HWND popup=GetLastActivePopup(expected.window);
        if(!IsWindowEnabled(expected.window) || (popup!=expected.window && IsWindowVisible(popup))) {
            UpdateStatus(L"文件夹已转交；原窗口有未结束的操作，已保留");continue;
        }
        if(!s_closeExplorerForTest && m_explorerScan) {
            std::lock_guard<std::mutex> lock(m_explorerScan->mutex);
            m_explorerScan->closeRequests.push_back(expected);
            m_explorerScan->wake.notify_all();
            SyncShellViewSelection();UpdateStatus(L"文件夹已转交，正在确认原窗口状态");continue;
        }
        const bool closed=s_closeExplorerForTest && s_closeExplorerForTest(expected);
        if(!IsWindow(m_hWnd) || !m_settings.explorerWindowTakeover)return;
        if(closed){SyncShellViewSelection();BringToForeground();UpdateStatus(L"已将资源管理器文件夹转交给 FastFile");}
        else UpdateStatus(L"文件夹已转交；原窗口状态变化或包含其他标签，已保留");
    }
}
