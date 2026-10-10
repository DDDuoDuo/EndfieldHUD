#include "native/app_shortcut_service.hpp"
#include "native/app_shortcut_text.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#endif

namespace n=endfield::native;namespace m=endfield::modules;using J=m::ShortcutJson;
namespace {
unsigned checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&&fn,const char*why){bool failed{};try{fn();}catch(const std::exception&){failed=true;}check(failed,why);}
constexpr auto uuid="12345678-1234-4234-8234-123456789ABC";
struct Lease {unsigned&live;explicit Lease(unsigned&v):live(v){++live;}~Lease(){--live;}};
struct OS {
    unsigned inspections{},launches{},live{};bool failInspect{},failLaunch{};n::AppShortcutResolved value,lastLaunch;std::uintptr_t lastOwner{};
    OS(){value.selection={n::AppShortcutSelectionKind::file,"C:\\Apps\\中文应用.exe"};value.name="中文应用 이름";value.applicationKey="path:c:\\apps\\中文应用.exe";value.selectedFileKey="file:10:20";value.executablePath=value.selection.value;}
    n::AppShortcutOS operations(){return {[this](const auto&s){++inspections;if(failInspect)throw m::ShortcutError(m::ShortcutErrorCode::unavailable);auto result=value;check(s.kind==value.selection.kind&&s.value==value.selection.value,"Inspection receives exactly the selected path or AUMID");result.lease=std::make_shared<Lease>(live);return result;},[this](const auto&r,std::uintptr_t owner){++launches;check(live==1,"Short-lived inspected state survives synchronous handoff");lastLaunch=r;lastLaunch.lease.reset();lastOwner=owner;if(failLaunch)throw m::ShortcutError(m::ShortcutErrorCode::unavailable);return n::AppShortcutLaunchReceipt{4242};}};}
};
m::ShortcutRecord record(const m::ShortcutCandidate&c){m::ShortcutRecord r;r.id=uuid;r.name="Saved 中文";r.originalName=c.name;r.locator=c.locator;return r;}
void service(){OS os;n::NativeAppShortcutService service(os.operations());const auto chosen=service.inspect(os.value.selection);
    check(chosen.name==os.value.name&&!chosen.bundleIdentifier&&os.inspections==1&&os.launches==0&&os.live==0,"Selecting Unicode application metadata never launches or retains access handles");
    check(chosen.locator["windowsTarget"]["kind"]==J("executable")&&!chosen.locator.contains("bookmark"),"Windows locator never fabricates a Mac bookmark");
    auto saved=record(chosen);const auto before=saved;const auto receipt=service.launch(saved,99);check(receipt.processID==4242&&os.lastOwner==99&&os.lastLaunch.executablePath==os.value.executablePath&&os.live==0&&saved==before,"Explicit launch uses a fresh exact path and does not edit saved data");
    os.value.selectedFileKey="file:10:200";os.value.name="Updated 中文应用";
    const auto updated=service.reinspect(saved.locator);check(updated.name==os.value.name&&updated.locator["windowsTarget"]["selectedFileKey"]==J("file:10:200"),"Legitimate binary replacement at the saved path remains available");
    check(n::NativeAppShortcutService::sameTarget(chosen.locator,updated.locator),"Same application path remains the same target after update");
    service.launch(saved);check(os.lastLaunch.selectedFileKey=="file:10:200"&&os.live==0,"Old saved locator launches the current application without reselection");
    auto stale=record(chosen);stale.locator["windowsTarget"]["launch"]["path"]="C:\\Other\\substitute.exe";
    service.launch(stale);check(os.lastLaunch.executablePath==os.value.executablePath,"Serialized launch metadata cannot substitute the user-selected executable path");
    os.failLaunch=true;rejects([&]{service.launch(saved);},"Launch failure is returned to the host");check(os.live==0,"Failed launch releases its short-lived OS state");os.failLaunch=false;
    os.failInspect=true;const auto calls=os.launches;rejects([&]{service.launch(saved);},"Unavailable selected file never launches a substitute");check(os.launches==calls&&os.live==0,"Inspection failure prevents every launch side effect");os.failInspect=false;
    const auto inspections=os.inspections;for(const auto*path:{"relative.exe","https://example.invalid/app.exe","C:\\Apps\\script.bat","C:\\Apps\\site.url","C:\\Apps\\document.txt"})rejects([&]{service.inspect({n::AppShortcutSelectionKind::file,path});},"Malformed or non-application direct selection rejects before OS lookup");
    check(os.inspections==inspections,"Invalid direct selections never invoke OS operations");
    auto mac=saved;mac.locator=J::Object{{"bookmark","AQID"},{"securityScoped",true},{"lastKnownPath","/Applications/Original.app"}};mac.originalFields=J::Object{{"opaqueFuture",J::Array{1,"kept"}}};const auto originalMac=mac;
    rejects([&]{service.launch(mac);},"Opaque Mac bookmark is never treated as a Windows path");check(mac==originalMac&&os.inspections==inspections,"Imported Mac access metadata remains byte-structurally untouched");
    check(!n::NativeAppShortcutService::sameTarget(mac.locator,chosen.locator),"Mac references never deduplicate against guessed Windows applications");
    // Exercise the already-verified model's reinspection and duplicate seam.
    const auto rules=n::nativeShortcutTextRules();m::ShortcutFile model;const auto id=m::saveShortcutDraft(model,chosen,updated,"  名称 이름  ","original",{},uuid,1,rules,n::NativeAppShortcutService::sameTarget);
    check(id==uuid&&model.items.front().name=="名称 이름"&&model.items.front().originalName==os.value.name,"Verified model accepts an updated binary and Unicode name through fresh inspection");
    rejects([&]{m::saveShortcutDraft(model,updated,updated,"Duplicate","original",{},"12345678-1234-4234-8234-123456789ABD",2,rules,n::NativeAppShortcutService::sameTarget);},"Existing same-path shortcut still deduplicates after update");
}
void linksAndPackages(){OS os;os.value.selection.value="C:\\Start Menu\\工具.lnk";os.value.applicationKey="path:c:\\start menu\\工具.lnk";os.value.resolvedApplicationKey="path:c:\\apps\\中文应用.exe";os.value.launchKind=n::AppShortcutLaunchKind::shellLink;os.value.executablePath.clear();os.value.arguments="--profile \"中文\"";os.value.workingDirectory="C:\\Apps";os.value.runAsUser=true;
    n::NativeAppShortcutService service(os.operations());const auto selected=service.inspect(os.value.selection);check(selected.locator["windowsTarget"]["kind"]==J("shellLink"),"Selected shell link remains its own Windows reference");
    OS executable;n::NativeAppShortcutService direct(executable.operations());check(n::NativeAppShortcutService::sameTarget(selected.locator,direct.inspect(executable.value.selection).locator),"Known recorded target supports executable/link duplicate detection");
    auto saved=record(selected);os.value.arguments="--profile \"更新\"";os.value.workingDirectory="C:\\Apps\\Version2";os.value.resolvedApplicationKey="path:c:\\apps\\version2\\app.exe";os.value.selectedFileKey="file:10:300";
    const auto current=service.reinspect(saved.locator);check(n::NativeAppShortcutService::sameTarget(selected.locator,current.locator),"Installer-updated link at the selected path does not require reselection");
    service.launch(saved);check(os.lastLaunch.launchKind==n::AppShortcutLaunchKind::shellLink&&os.lastLaunch.selection.value==saved.locator["windowsTarget"]["path"].string()&&os.lastLaunch.arguments==os.value.arguments,"Launch hands the selected link and fresh metadata to the Windows boundary");
    os.value={};os.value.selection={n::AppShortcutSelectionKind::packagedApp,"Example.Package_abc123!Application"};os.value.name="Packaged 应用";os.value.applicationKey="package:"+os.value.selection.value;os.value.launchKind=n::AppShortcutLaunchKind::packagedApp;os.value.appUserModelID=os.value.selection.value;
    const auto packaged=service.inspect(os.value.selection);check(packaged.locator["windowsTarget"]["kind"]==J("packagedApp")&&!packaged.locator["windowsTarget"].contains("path"),"Packaged app stores its exact AUMID without an invented executable path");
    service.launch(record(packaged));check(os.lastLaunch.appUserModelID==os.value.selection.value&&os.lastLaunch.launchKind==n::AppShortcutLaunchKind::packagedApp,"Packaged launch retains exact application identity");
    rejects([&]{service.inspect({n::AppShortcutSelectionKind::packagedApp,"shell:AppsFolder\\other"});},"Arbitrary shell strings are not accepted as AUMIDs");
}
struct Dialog: n::AppShortcutPickerDialog {
    unsigned shows{},cancels{};std::function<void()>during;std::optional<n::AppShortcutSelection>result{n::AppShortcutSelection{n::AppShortcutSelectionKind::file,"C:\\Apps\\Chosen.exe"}};
    std::optional<n::AppShortcutSelection>show(std::uintptr_t owner,const n::AppShortcutPickerLabels&l)override{++shows;check(owner==44&&l.title=="选择应用","Native picker receives the owner and Unicode labels");if(during)during();return result;}
    void cancel()noexcept override{++cancels;}
};
void picker(){auto dialog=std::make_shared<Dialog>();unsigned factories{};n::NativeAppShortcutPicker picker([&]{++factories;return dialog;});const n::AppShortcutPickerLabels labels{"选择应用","选择","应用程序",n::AppShortcutPickerMode::files};
    check(!picker.presenting()&&factories==0,"Picker construction creates no dialog or scan");const auto selected=picker.choose(44,labels);check(selected==dialog->result&&factories==1&&!picker.presenting(),"One explicit request returns one selected path");
    dialog->during=[&]{check(picker.presenting(),"Picker state spans the modal boundary");rejects([&]{picker.choose(44,labels);},"Nested picker request cannot create a second dialog");picker.cancel();};
    check(!picker.choose(44,labels)&&dialog->cancels==1&&!picker.presenting(),"Cancellation discards a late successful dialog result");
    dialog->during={};dialog->result.reset();check(!picker.choose(44,labels),"User cancellation returns no synthetic application");
    bool threadRejected{};std::thread other([&]{try{picker.presenting();}catch(const std::logic_error&){threadRejected=true;}});other.join();check(threadRejected,"Picker never moves its native dialog to another thread");
    const auto previous=factories;auto bad=labels;bad.title.clear();rejects([&]{picker.choose(44,bad);},"Invalid labels reject before dialog creation");check(factories==previous,"Malformed picker request performs no native work");
    dialog->result=n::AppShortcutSelection{n::AppShortcutSelectionKind::file,"C:\\Apps\\not-an-app.txt"};rejects([&]{picker.choose(44,labels);},"Invalid selected direct file never becomes a draft");check(!picker.presenting(),"Failed picker result resets modal state");
    auto owned=std::make_unique<n::NativeAppShortcutPicker>([&]{return dialog;});dialog->result=n::AppShortcutSelection{n::AppShortcutSelectionKind::file,"C:\\Apps\\Chosen.exe"};dialog->during=[&]{owned.reset();};auto*raw=owned.get();check(!raw->choose(44,labels)&&!owned&&dialog->cancels==2,"Owner destruction during modal reentry cancels and suppresses stale result");
    std::unique_ptr<n::NativeAppShortcutPicker>factoryOwner;factoryOwner=std::make_unique<n::NativeAppShortcutPicker>([&]{factoryOwner.reset();return dialog;});const auto shows=dialog->shows;raw=factoryOwner.get();check(!raw->choose(44,labels)&&dialog->shows==shows,"Owner destruction during dialog factory cannot subsequently show UI");
}
#ifdef _WIN32
void hr(HRESULT value){if(FAILED(value))throw std::runtime_error("Owned shortcut fixture COM call failed");}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("endfield-shortcut-service-"+ehud::data::makeUUID());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
std::string utf8Path(const std::filesystem::path&p){const auto value=p.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
void stub(const std::filesystem::path&path){std::array<unsigned char,1024>bytes{};const auto u16=[&](std::size_t p,unsigned v){bytes[p]=static_cast<unsigned char>(v);bytes[p+1]=static_cast<unsigned char>(v>>8);};const auto u32=[&](std::size_t p,unsigned v){u16(p,v);u16(p+2,v>>16);};
    u16(0,0x5a4d);u32(0x3c,0x80);u32(0x80,0x4550);u16(0x84,0x14c);u16(0x86,1);u16(0x94,224);u16(0x96,0x102);const std::size_t o=0x98;u16(o,0x10b);u32(o+4,512);u32(o+16,0x1000);u32(o+20,0x1000);u32(o+28,0x400000);u32(o+32,4096);u32(o+36,512);u16(o+40,6);u16(o+48,6);u32(o+56,8192);u32(o+60,512);u16(o+68,3);u32(o+72,0x100000);u32(o+76,4096);u32(o+80,0x100000);u32(o+84,4096);u32(o+92,16);const std::size_t s=o+224;const char name[]=".text";std::copy(name,name+5,bytes.data()+s);u32(s+8,1);u32(s+12,0x1000);u32(s+16,512);u32(s+20,512);u32(s+36,0x60000020);bytes[512]=0xc3;
    std::ofstream output(path,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));check(bool(output),"Write only owned synthetic PE metadata fixture; never execute it");
}
void shellLink(const std::filesystem::path&path,const std::filesystem::path&target,const wchar_t*args){Microsoft::WRL::ComPtr<IShellLinkW>link;hr(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)));hr(link->SetPath(target.c_str()));hr(link->SetArguments(args));hr(link->SetWorkingDirectory(target.parent_path().c_str()));Microsoft::WRL::ComPtr<IPersistFile>persist;hr(link.As(&persist));hr(persist->Save(path.c_str(),TRUE));}
void nativeFixtures(){hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));struct End{~End(){CoUninitialize();}}end;Temp temp;const auto exe=temp.path/L"中文程序.exe",link=temp.path/L"应用链接.lnk",replacement=temp.path/L"updated.exe";stub(exe);shellLink(link,exe,L"--profile 中文");
    auto operations=n::windowsAppShortcutOS();unsigned launches{};n::AppShortcutResolved last;operations.launch=[&](const auto&r,std::uintptr_t){++launches;last=r;last.lease.reset();return n::AppShortcutLaunchReceipt{123};};n::NativeAppShortcutService service(std::move(operations));
    const auto app=service.inspect({n::AppShortcutSelectionKind::file,utf8Path(exe)});check(app.name=="中文程序","Real Win32 inspector preserves Unicode filename when no version resource exists");const auto shortcut=service.inspect({n::AppShortcutSelectionKind::file,utf8Path(link)});check(n::NativeAppShortcutService::sameTarget(app.locator,shortcut.locator),"Real ShellLink metadata targets the same owned application");
    check(shortcut.locator["windowsTarget"]["launch"]["arguments"]==J("--profile 中文"),"Real ShellLink reader preserves authored Unicode arguments");
    stub(replacement);check(MoveFileExW(replacement.c_str(),exe.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE,"Owned update replaces executable at the same saved path");const auto updated=service.reinspect(app.locator);check(n::NativeAppShortcutService::sameTarget(app.locator,updated.locator),"Real binary file replacement does not invalidate saved application path");
    service.launch(record(app));check(launches==1&&last.selection.value==updated.locator["windowsTarget"]["path"].string(),"Real reinspection hands the updated selected path to injected launch only");
    shellLink(link,exe,L"--profile 已更新");service.launch(record(shortcut));check(launches==2&&last.launchKind==n::AppShortcutLaunchKind::shellLink&&last.arguments=="--profile 已更新","Updated real link remains launchable through normal selected-link handoff");
    {std::ofstream broken(temp.path/L"bad.exe");broken<<"not an executable";}rejects([&]{service.inspect({n::AppShortcutSelectionKind::file,utf8Path(temp.path/L"bad.exe")});},"Native binary type rejects malformed direct executable");
    std::filesystem::remove(exe);rejects([&]{service.launch(record(app));},"Missing direct executable returns unavailable without starting a process");check(launches==2,"Native fixture never invokes real launch or any unselected application");
}
#endif
}
int main(){try{service();linksAndPackages();picker();
#ifdef _WIN32
nativeFixtures();
#endif
std::cout<<"App Shortcut service: "<<checks<<" checks passed (synthetic OS/dialog boundaries; no real application launch)\n";return 0;}catch(const std::exception&e){std::cerr<<"App Shortcut service failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
