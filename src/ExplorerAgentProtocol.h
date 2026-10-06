#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstring>

// Bounded, copied UTF-16 messages; never pass pointers across processes.
namespace ExplorerAgentProtocol {
constexpr ULONG_PTR Open = 0x46464101;
constexpr ULONG_PTR Confirm = 0x46464102;
constexpr size_t MaxChars = 32768;
inline std::vector<wchar_t> Encode(const std::vector<std::wstring>& paths) {
    if(paths.empty() || paths.size()>257)return {};
    std::vector<wchar_t> text;
    for(const auto& path:paths) {
        if(path.empty() || path.find(L'\0')!=std::wstring::npos || text.size()+path.size()+2>MaxChars)return {};
        text.insert(text.end(),path.begin(),path.end());text.push_back(0);
    }
    text.push_back(0);return text;
}
inline bool Decode(const COPYDATASTRUCT& data,std::vector<std::wstring>& paths) {
    paths.clear();
    if((data.dwData!=Open && data.dwData!=Confirm) || !data.lpData
        || data.cbData<3*sizeof(wchar_t) || data.cbData%sizeof(wchar_t)
        || data.cbData>MaxChars*sizeof(wchar_t))return false;
    // COPYDATA storage need not be aligned; copy before reading wchar_t values.
    std::vector<wchar_t> text(data.cbData/sizeof(wchar_t));memcpy(text.data(),data.lpData,data.cbData);
    size_t pos=0;
    while(pos<text.size()) {
        if(text[pos]==0)return pos==text.size()-1 && !paths.empty();
        const auto start=pos;while(pos<text.size() && text[pos]!=0)++pos;
        if(pos==text.size() || paths.size()==257){paths.clear();return false;}
        paths.emplace_back(text.data()+start,pos-start);++pos;
    }
    paths.clear();return false;
}
inline std::wstring Sibling(const wchar_t* filename) {
    wchar_t path[32768]{};
    const auto length=GetModuleFileNameW(nullptr,path,_countof(path));
    if(!length || length>=_countof(path))return {};
    std::wstring result(path);const auto slash=result.find_last_of(L"\\/");
    return slash==std::wstring::npos?std::wstring():result.substr(0,slash+1)+filename;
}
}
