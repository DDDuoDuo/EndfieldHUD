#include "native/process_identity.hpp"

namespace endfield::native {
namespace {
char lower(char c)noexcept{return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;}
bool separator(char c)noexcept{return c=='\\'||c=='/';}
}
bool pathInsideFolder(std::string_view path,std::string_view folder)noexcept{
    while(!folder.empty()&&separator(folder.back()))folder.remove_suffix(1);
    if(folder.empty()||path.size()<=folder.size()+1||!separator(path[folder.size()]))return false;
    for(std::size_t n=0;n<folder.size();++n)if(lower(path[n])!=lower(folder[n]))return false;
    return true;
}
bool executableInsideFolderOf(std::string_view image,std::string_view parent)noexcept{
    const auto slash=parent.find_last_of("/\\");
    return slash!=parent.npos&&slash>3&&pathInsideFolder(image,parent.substr(0,slash));
}
bool executableNamed(std::string_view path,std::string_view name)noexcept{
    if(name.empty()||path.size()<name.size())return false;const auto at=path.size()-name.size();
    if(at&&!separator(path[at-1]))return false;
    for(std::size_t n=0;n<name.size();++n)if(lower(path[at+n])!=lower(name[n]))return false;
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
bool readWindowsApplicationWindows(std::set<std::uint32_t>&out,std::size_t limit){
    out.clear();struct State{std::set<std::uint32_t>&pids;std::size_t limit,count{};bool bounded{true},failed{};}state{out,limit};
    const auto callback=[](HWND hwnd,LPARAM value)->BOOL{auto&s=*reinterpret_cast<State*>(value);
        try{if(++s.count>s.limit){s.bounded=false;return FALSE;}if(GetWindow(hwnd,GW_OWNER)||GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)return TRUE;DWORD pid{};GetWindowThreadProcessId(hwnd,&pid);if(pid)s.pids.insert(pid);return TRUE;}
        catch(...){s.failed=true;return FALSE;}};
    return EnumWindows(callback,reinterpret_cast<LPARAM>(&state))&&state.bounded&&!state.failed;
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
