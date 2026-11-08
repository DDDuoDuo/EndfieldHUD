#include "native/process_identity.hpp"

namespace endfield::native {
namespace {
char lower(char c)noexcept{return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;}
}
bool executableInsideFolderOf(std::string_view image,std::string_view parent)noexcept{
    const auto slash=parent.find_last_of("/\\");
    if(slash==parent.npos||slash<=3||image.size()<=slash+1)return false;
    for(std::size_t n=0;n<=slash;++n)if(lower(image[n])!=lower(parent[n]))return false;
    return true;
}
}
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <array>

namespace endfield::native {
bool readWindowsProcesses(std::vector<WindowsProcessEntry>&out,std::size_t limit){
    out.clear();const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return false;
    struct Close{HANDLE value;~Close(){CloseHandle(value);}}close{snapshot};
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(!Process32FirstW(snapshot,&entry))return GetLastError()==ERROR_NO_MORE_FILES;
    do{if(out.size()>=limit)return false;out.push_back({entry.th32ProcessID,entry.th32ParentProcessID,entry.cntThreads});}while(Process32NextW(snapshot,&entry));
    return GetLastError()==ERROR_NO_MORE_FILES;
}
std::optional<std::uint64_t>readWindowsProcessCreation(void*process)noexcept{
    FILETIME created{},exited{},kernel{},user{};if(!process||!GetProcessTimes(process,&created,&exited,&kernel,&user))return {};
    const auto ticks=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;if(!ticks)return {};return ticks;
}
std::optional<std::wstring>readWindowsProcessPath(void*process){
    if(!process)return {};std::array<wchar_t,32768>path{};DWORD length=static_cast<DWORD>(path.size());
    if(!QueryFullProcessImageNameW(process,0,path.data(),&length)||!length||length>=path.size())return {};
    return std::wstring(path.data(),length);
}
}
#endif
