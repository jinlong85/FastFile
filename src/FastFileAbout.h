#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <winver.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include "FastFileBuildInfo.h"

namespace FastFileAbout {
inline std::wstring ExecutablePath() {
    std::vector<wchar_t> path(32768);
    const DWORD size=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
    return size && size<path.size()?std::wstring(path.data(),size):L"";
}
inline std::wstring Directory(std::wstring path) {
    const auto end=path.find_last_of(L"\\/");
    if(end==std::wstring::npos)return L"";
    return path.substr(0,end && path[end-1]==L':'?end+1:end);
}
inline std::wstring FileVersion(const std::wstring& path) {
    DWORD unused=0;const DWORD size=GetFileVersionInfoSizeW(path.c_str(),&unused);
    if(!size || size>1024*1024)return {};
    std::vector<BYTE> bytes(size);VS_FIXEDFILEINFO* info=nullptr;UINT length=0;
    if(!GetFileVersionInfoW(path.c_str(),0,size,bytes.data())
        || !VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&length)
        || length<sizeof(*info) || info->dwSignature!=0xfeef04bd)return {};
    auto version=std::to_wstring(HIWORD(info->dwFileVersionMS))+L"."+std::to_wstring(LOWORD(info->dwFileVersionMS))
        +L"."+std::to_wstring(HIWORD(info->dwFileVersionLS));
    if(LOWORD(info->dwFileVersionLS))version+=L"."+std::to_wstring(LOWORD(info->dwFileVersionLS));
    return version;
}
struct Info {
    std::wstring version=FASTFILE_VERSION_W,build,date,executable,windows,agentVersion;
    UINT dpi=96;DWORD agentPid=0;bool agentUnknown=false,agentInstalled=false;
    std::wstring AgentText() const {
        std::wstring text=agentPid?L"正在运行（PID "+std::to_wstring(agentPid)+L"）":agentUnknown?L"无法确认状态":L"未运行";
        if(!agentInstalled)return text+L"；未找到本安装目录的代理";
        text+=L"；版本 "+(agentVersion.empty()?L"无法读取":agentVersion);
        if(!agentVersion.empty() && agentVersion!=version)text+=L"（与界面版本不一致，请重新安装）";
        return text;
    }
    // Whitelist fields: never include executable/profile/log or browsing paths.
    std::wstring Diagnostics() const {
        return L"FastFile "+version+L"\r\n构建："+build+L"\r\n编译时间："+date
            +L"\r\nWindows："+windows+L"\r\n窗口 DPI："+std::to_wstring(dpi)
            +L"\r\n本安装后台代理："+AgentText()+L"\r\n";
    }
};
inline bool IsOwnedAgent(const std::wstring& path,const std::wstring& expected,DWORD session,DWORD currentSession) {
    return session==currentSession && !path.empty() && _wcsicmp(path.c_str(),expected.c_str())==0;
}
#define FF_ABOUT_WIDEN_IMPL(s) L##s
#define FF_ABOUT_WIDEN(s) FF_ABOUT_WIDEN_IMPL(s)
inline Info Collect(HWND owner) {
    Info info;info.executable=ExecutablePath();info.dpi=GetDpiForWindow(owner);if(!info.dpi)info.dpi=96;
#ifdef _DEBUG
    info.build=L"Debug";
#else
    info.build=L"Release";
#endif
#ifdef _WIN64
    info.build+=L" / x64";
#else
    info.build+=L" / x86";
#endif
    info.date=FF_ABOUT_WIDEN(__DATE__) L" " FF_ABOUT_WIDEN(__TIME__);
    using GetVersion=LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto getVersion=reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"));
    OSVERSIONINFOW os{};os.dwOSVersionInfoSize=sizeof(os);
    info.windows=getVersion && getVersion(&os)==0?std::to_wstring(os.dwMajorVersion)+L"."+std::to_wstring(os.dwMinorVersion)
        +L"."+std::to_wstring(os.dwBuildNumber):L"无法读取";
    auto agent=Directory(info.executable);
    if(!agent.empty() && agent.back()!=L'\\' && agent.back()!=L'/')agent+=L"\\";
    agent+=L"FastFileAgent.exe";
    const DWORD attributes=GetFileAttributesW(agent.c_str());
    info.agentInstalled=attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
    if(info.agentInstalled)info.agentVersion=FileVersion(agent);
    HANDLE processes=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(processes==INVALID_HANDLE_VALUE){info.agentUnknown=true;return info;}
    DWORD currentSession=0;ProcessIdToSessionId(GetCurrentProcessId(),&currentSession);
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    for(BOOL valid=Process32FirstW(processes,&entry);valid;valid=Process32NextW(processes,&entry)) {
        if(_wcsicmp(entry.szExeFile,L"FastFileAgent.exe"))continue;
        DWORD session=0;if(!ProcessIdToSessionId(entry.th32ProcessID,&session) || session!=currentSession)continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        wchar_t path[32768]{};DWORD size=_countof(path);
        if(process && QueryFullProcessImageNameW(process,0,path,&size)) {
            if(IsOwnedAgent(path,agent,session,currentSession))info.agentPid=entry.th32ProcessID;
        } else info.agentUnknown=true;
        if(process)CloseHandle(process);
    }
    CloseHandle(processes);return info;
}
#undef FF_ABOUT_WIDEN
#undef FF_ABOUT_WIDEN_IMPL
inline bool CopyDiagnostics(HWND owner,const Info& info) {
    const auto text=info.Diagnostics();const SIZE_T bytes=(text.size()+1)*sizeof(wchar_t);
    HGLOBAL storage=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!storage)return false;
    void* buffer=GlobalLock(storage);if(!buffer){GlobalFree(storage);return false;}
    memcpy(buffer,text.c_str(),bytes);GlobalUnlock(storage);
    if(!OpenClipboard(owner)){GlobalFree(storage);return false;}
    const bool ok=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,storage)!=nullptr;
    CloseClipboard();if(!ok)GlobalFree(storage);return ok;
}
inline std::wstring LicenseText(HMODULE module=nullptr) {
    if(!module)module=GetModuleHandleW(nullptr);const auto resource=FindResourceW(module,MAKEINTRESOURCEW(2),RT_RCDATA);
    const auto size=resource?SizeofResource(module,resource):0;
    const auto loaded=resource?LoadResource(module,resource):nullptr;
    const auto bytes=loaded?static_cast<const char*>(LockResource(loaded)):nullptr;
    std::wstring text=L"FastFile：当前项目未声明自身的开源许可证。\r\n\r\nDuiLib：MIT License，Copyright (c) 2013 duilib。\r\n\r\n";
    if(bytes && size) {const int length=MultiByteToWideChar(CP_UTF8,0,bytes,size,nullptr,0);
        std::wstring license(length,L'\0');MultiByteToWideChar(CP_UTF8,0,bytes,size,license.data(),length);text+=license;
    } else text+=L"无法读取内嵌的许可文本。";
    return text;
}
}
