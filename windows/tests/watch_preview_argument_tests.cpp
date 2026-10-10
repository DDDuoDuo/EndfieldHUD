#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace fs=std::filesystem;
unsigned checks{};
void check(bool value,const char*reason){++checks;if(!value)throw std::runtime_error(reason);}
struct Handle {
    HANDLE value{};
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;
    explicit Handle(HANDLE handle):value(handle){}
};
struct Temporary {
    fs::path root;
    Temporary():root(fs::temp_directory_path()/(L"EndfieldHUD watch arguments "+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()))){
        check(fs::create_directory(root),"The argument fixture requires a new owned temporary directory");
    }
    ~Temporary(){std::error_code ignored;fs::remove_all(root,ignored);}
};
std::string read(const fs::path&path){
    check(fs::file_size(path)<=65536,"Child diagnostics stay bounded and contain no user data");
    std::ifstream input(path,std::ios::binary);
    check(bool(input),"Read only owned argument-fixture output");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
std::wstring quote(std::wstring_view value){
    std::wstring out=L"\"";std::size_t slashes{};
    for(const auto c:value){
        if(c==L'\\'){++slashes;continue;}
        out.append(slashes*(c==L'\"'?2u:1u),L'\\');slashes=0;
        if(c==L'\"')out.push_back(L'\\');out.push_back(c);
    }
    out.append(slashes*2,L'\\');out.push_back(L'\"');return out;
}
void reject(const fs::path&executable,const Temporary&temp,unsigned index,
            std::vector<std::wstring>options,std::string_view expected){
    const auto output=temp.root/(L"child "+std::to_wstring(index)+L".log");
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle log(CreateFileW(output.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,
                          CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    check(log.value!=INVALID_HANDLE_VALUE,"Every child has its own new diagnostic file");
    std::vector<std::wstring>args{executable.wstring(),(temp.root/L"absent packet").wstring(),
                                 (temp.root/L"absent compiled scene").wstring(),
                                 (temp.root/L"absent shader").wstring()};
    args.insert(args.end(),options.begin(),options.end());
    std::wstring command;
    for(const auto&arg:args){if(!command.empty())command.push_back(L' ');command+=quote(arg);}
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    startup.hStdInput=nullptr;startup.hStdOutput=log.value;startup.hStdError=log.value;
    PROCESS_INFORMATION process{};
    check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,
                         CREATE_NO_WINDOW,nullptr,temp.root.c_str(),&startup,&process)!=FALSE,
          "Launch only the explicitly supplied test executable with invalid arguments");
    Handle child(process.hProcess),thread(process.hThread);
    const auto finished=WaitForSingleObject(child.value,10000);
    if(finished!=WAIT_OBJECT_0){TerminateProcess(child.value,97);WaitForSingleObject(child.value,1000);}
    check(finished==WAIT_OBJECT_0,"Invalid arguments finish before any app event loop");
    DWORD result{};check(GetExitCodeProcess(child.value,&result)!=FALSE&&result==1,
                         "The real preview rejects the isolated argument envelope");
    check(FlushFileBuffers(log.value)!=FALSE,"Flush only this fixture's child diagnostics");
    const auto diagnostics=read(output);
    if(diagnostics.find(expected)==std::string::npos)
        throw std::runtime_error("Unexpected argument rejection: "+diagnostics);
    check(diagnostics.find("Startup stage:")==std::string::npos,
          "Argument validation finishes before source preparation or window creation");
}
void run(const fs::path&executable){
    Temporary temp;const auto assets=(temp.root/L"absent Notes assets").wstring();
    const auto archiveAssets=(temp.root/L"absent Archive assets").wstring();
    const auto fresh=temp.root/L"new injected data";const auto report=temp.root/L"new report.json";
    const auto existing=temp.root/L"existing injected data";
    check(fs::create_directory(existing),"Existing-root rejection uses an owned directory only");
    const auto sentinel=existing/L"keep.txt";{std::ofstream out(sentinel);out<<"owned sentinel";}
    const auto existingReport=temp.root/L"existing report.json";{std::ofstream out(existingReport);out<<"owned report";}
    unsigned index{};
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--projection"},"Projection needs the visible shared HUD");
    reject(executable,temp,++index,{L"--visible",L"--projection",L"--projection"},"Duplicate Projection mode");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--reader"},"Reader needs the shared module owner");
    reject(executable,temp,++index,{L"--visible",L"--reader",L"--reader"},"Duplicate Reader mode");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--calendar"},"Calendar needs the shared module owner");
    reject(executable,temp,++index,{L"--visible",L"--calendar",L"--calendar"},"Duplicate Calendar mode");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--calendar",L"--notes-assets",assets,
           L"--notes-data",fresh.wstring()},"Notes preview requires assets, independent SHA and a new data root together");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--calendar",L"--notes-assets",assets,
           L"--notes-assets-sha",L"synthetic-pin",L"--notes-data",fresh.wstring()},"Hidden module integration requires explicit --module-coverage");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--calendar",L"--module-coverage",L"--notes-assets",assets,
           L"--notes-assets-sha",L"synthetic-pin",L"--notes-data",existing.wstring()},"Notes preview data root must be new");
    reject(executable,temp,++index,{L"--visible",L"--calendar",L"--module-coverage",L"--notes-assets",assets,
           L"--notes-assets-sha",L"synthetic-pin",L"--notes-data",fresh.wstring()},"Module coverage requires hidden mode and fresh isolated Notes data");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--native-activity"},
           "Native Activity requires explicit visible mode");
    reject(executable,temp,++index,{L"--visible",L"--native-activity"},
           "Native Activity requires explicit visible mode");
    reject(executable,temp,++index,{L"--visible",L"--native-activity",L"--native-activity"},
           "Duplicate native Activity mode");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--native-clipboard"},
           "Native Clipboard requires explicit visible mode");
    reject(executable,temp,++index,{L"--visible",L"--native-clipboard"},
           "Native Clipboard requires explicit visible mode");
    reject(executable,temp,++index,{L"--visible",L"--module-coverage",L"--native-clipboard",L"--notes-assets",assets,L"--clipboard-assets",assets},
           "Native Clipboard requires explicit visible mode");
    reject(executable,temp,++index,{L"--visible",L"--native-clipboard",L"--native-clipboard"},
           "Duplicate native Clipboard mode");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--activity-assets",assets},
           "Activity needs the shared module owner");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--activity-assets",assets,L"--activity-assets",assets},
           "Duplicate Activity assets");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--archive-assets",archiveAssets},
           "Archive needs explicit Notes assets and a new shared test data root");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--archive-assets",archiveAssets,
           L"--archive-assets",archiveAssets},"Duplicate Archive assets");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--archive-assets"},
           "Unknown or incomplete preview argument");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--notes-assets",assets,
           L"--notes-data",fresh.wstring(),L"--archive-assets",archiveAssets},
           "Notes preview requires assets, independent SHA and a new data root together");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--notes-assets",assets,
           L"--notes-assets-sha",L"synthetic-pin",L"--notes-data",fresh.wstring(),L"--archive-assets",archiveAssets},
           "Hidden module integration requires explicit --module-coverage");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--module-coverage",L"--notes-assets",assets,
           L"--notes-assets-sha",L"synthetic-pin",L"--notes-data",existing.wstring(),L"--archive-assets",archiveAssets},
           "Notes preview data root must be new");
    reject(executable,temp,++index,{L"--visible",L"--module-coverage",L"--notes-assets",assets,
           L"--notes-assets-sha",L"synthetic-pin",L"--notes-data",fresh.wstring(),L"--archive-assets",archiveAssets},
           "Module coverage requires hidden mode and fresh isolated Notes data");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--map-geography",assets},
           "Map needs geography and player assets together");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--map-player-assets",assets},
           "Map needs geography and player assets together");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--map-geography",assets,L"--map-player-assets",assets},
           "Map needs the shared module owner and a new temporary data root");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--map-geography",assets,L"--map-geography",assets},
           "Duplicate Map geography");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--orbipom-assets",assets},
           "Minigame needs the shared module owner and an isolated Settings store");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--orbipom-assets",assets,L"--orbipom-assets",assets},
           "Duplicate Minigame artwork");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--session-end-coverage"},
           "Session-end coverage extends hidden module coverage");
    reject(executable,temp,++index,{L"--benchmark",report.wstring(),L"--session-end-coverage",L"--session-end-coverage"},
           "Duplicate session-end coverage");
    reject(executable,temp,++index,{L"--benchmark",existingReport.wstring()},"Benchmark output already exists");
    check(!fs::exists(fresh)&&!fs::exists(report),"Rejected Archive/Calendar envelopes create neither a store nor a report");
    check(read(sentinel)=="owned sentinel"&&read(existingReport)=="owned report",
          "Existing data and report contents remain byte-for-byte intact");
    check(std::distance(fs::directory_iterator(existing),fs::directory_iterator{})==1,
          "The existing injected root gains no SQLite or other app files");
}
} // namespace
int wmain(int argc,wchar_t**argv){try{
    check(argc==2,"Pass the compiled watch_session_preview executable only");
    run(fs::absolute(argv[1]));std::cout<<"PASS "<<checks<<" preview argument/isolation checks\n";return 0;
}catch(const std::exception&error){std::cerr<<"FAIL after "<<checks<<": "<<error.what()<<'\n';return 1;}}
#else
int main(){return 0;}
#endif
