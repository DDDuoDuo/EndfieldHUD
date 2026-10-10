#include "native/app_shortcut_service.hpp"
#include "native/app_shortcut_text.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <thread>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propsys.h>
#include <wrl/client.h>
#include <winrt/Windows.ApplicationModel.h>
#include <system_error>
#endif

namespace endfield::native {
namespace {
using J=modules::ShortcutJson;using Error=modules::ShortcutError;using Code=modules::ShortcutErrorCode;
void need(bool value,Code code=Code::unavailable){if(!value)throw Error(code);}
bool text(std::string_view value,std::size_t maximum=32768){return value.size()<=maximum&&J::validUtf8(value)&&value.find('\0')==value.npos;}
std::string suffix(std::string_view value){const auto dot=value.rfind('.');if(dot==value.npos)return {};std::string result(value.substr(dot));for(auto&c:result)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return result;}
bool aumid(std::string_view value){return !value.empty()&&text(value)&&value.find_first_of("\\/:\r\n\t") == value.npos;}
void selection(const AppShortcutSelection&s){
    if(s.kind==AppShortcutSelectionKind::file)need(text(s.value)&&ehud::data::validWindowsFilePath(s.value)&&(suffix(s.value)==".exe"||suffix(s.value)==".lnk"));
    else need(s.kind==AppShortcutSelectionKind::packagedApp&&aumid(s.value));
}
void resolved(const AppShortcutResolved&r){
    selection(r.selection);need(!r.name.empty()&&text(r.name)&&!r.applicationKey.empty()&&text(r.applicationKey));
    need(text(r.arguments)&&text(r.workingDirectory)&&text(r.selectedFileKey)&&text(r.resolvedApplicationKey));
    if(r.selection.kind==AppShortcutSelectionKind::file)need(!r.selectedFileKey.empty());else need(r.selectedFileKey.empty());
    if(r.launchKind==AppShortcutLaunchKind::executable){
        need(r.selection.kind==AppShortcutSelectionKind::file&&r.appUserModelID.empty()&&ehud::data::validWindowsFilePath(r.executablePath)&&suffix(r.executablePath)==".exe");
        need(r.workingDirectory.empty()||ehud::data::validWindowsFilePath(r.workingDirectory));
        if(suffix(r.selection.value)==".exe")need(r.selection.value==r.executablePath&&r.arguments.empty()&&!r.runAsUser);
    }else if(r.launchKind==AppShortcutLaunchKind::shellLink){
        need(r.selection.kind==AppShortcutSelectionKind::file&&suffix(r.selection.value)==".lnk"&&r.executablePath.empty()&&r.appUserModelID.empty());
    }else{
        need(r.launchKind==AppShortcutLaunchKind::packagedApp&&aumid(r.appUserModelID)&&r.executablePath.empty()&&r.workingDirectory.empty()&&!r.runAsUser);
        if(r.selection.kind==AppShortcutSelectionKind::packagedApp)need(r.selection.value==r.appUserModelID&&r.arguments.empty());
    }
}
J locator(const AppShortcutResolved&r){resolved(r);J::Object target{{"schemaVersion",1},{"applicationKey",r.applicationKey}};
    if(r.selection.kind==AppShortcutSelectionKind::packagedApp){target["kind"]="packagedApp";target["appUserModelID"]=r.selection.value;}
    else{target["kind"]=suffix(r.selection.value)==".lnk"?"shellLink":"executable";target["path"]=r.selection.value;target["selectedFileKey"]=r.selectedFileKey;}
    if(!r.resolvedApplicationKey.empty())target["resolvedApplicationKey"]=r.resolvedApplicationKey;
    J::Object launch{{"kind",r.launchKind==AppShortcutLaunchKind::executable?"executable":r.launchKind==AppShortcutLaunchKind::shellLink?"shellLink":"packagedApp"},{"arguments",r.arguments},{"runAsUser",r.runAsUser}};
    if(r.launchKind==AppShortcutLaunchKind::executable){launch["path"]=r.executablePath;launch["workingDirectory"]=r.workingDirectory;}
    else if(r.launchKind==AppShortcutLaunchKind::shellLink){launch["path"]=r.selection.value;launch["workingDirectory"]=r.workingDirectory;}
    else launch["appUserModelID"]=r.appUserModelID;
    target["launch"]=std::move(launch);return J::Object{{"referencePlatform","windows"},{"windowsTarget",std::move(target)}};
}
AppShortcutSelection selected(const J&value){
    need(value.isObject()&&value["referencePlatform"]==J("windows")&&value["windowsTarget"].isObject());
    const auto&t=value["windowsTarget"];need(!t.contains("schemaVersion")||t["schemaVersion"]==J(1));
    AppShortcutSelection s;
    if(t["kind"]==J("packagedApp")){s.kind=AppShortcutSelectionKind::packagedApp;need(t["appUserModelID"].isString());s.value=t["appUserModelID"].string();}
    else{need(t["kind"]==J("executable")||t["kind"]==J("shellLink"));need(t["path"].isString());s.value=t["path"].string();need(suffix(s.value)==(t["kind"]==J("shellLink")?".lnk":".exe"));}
    selection(s);return s;
}
modules::ShortcutCandidate candidate(const AppShortcutResolved&r){return {r.name,{},locator(r)};}
AppShortcutResolved verify(const AppShortcutOS&os,const J&old){auto current=os.inspect(selected(old));resolved(current);
    // Saved identity is the user-selected path/AUMID. Physical replacement by
    // an application update is allowed; launch uses fresh OS metadata and the
    // normal Windows activation path, never stale serialized command fields.
    return current;
}
void labels(const AppShortcutPickerLabels&l){need(!l.title.empty()&&text(l.title,1024)&&!l.accept.empty()&&text(l.accept,256)&&!l.applications.empty()&&text(l.applications,256));need(l.mode==AppShortcutPickerMode::files||l.mode==AppShortcutPickerMode::installedApps);}
}
NativeAppShortcutService::NativeAppShortcutService(AppShortcutOS os):os_(std::move(os)){need(bool(os_.inspect)&&bool(os_.launch));}
modules::ShortcutCandidate NativeAppShortcutService::inspect(const AppShortcutSelection&s)const{selection(s);return candidate(os_.inspect(s));}
modules::ShortcutCandidate NativeAppShortcutService::reinspect(const J&value)const{return candidate(verify(os_,value));}
AppShortcutLaunchReceipt NativeAppShortcutService::launch(const modules::ShortcutRecord&record,std::uintptr_t owner)const{auto target=verify(os_,record.locator);return os_.launch(target,owner);}
bool NativeAppShortcutService::sameTarget(const J&a,const J&b)noexcept{
    try{if(a["referencePlatform"]!=J("windows")||b["referencePlatform"]!=J("windows"))return false;const auto&x=a["windowsTarget"];const auto&y=b["windowsTarget"];
        const auto same=[](const J&a,const J&b){return a.isString()&&b.isString()&&!a.string().empty()&&a==b;};
        if(same(x["applicationKey"],y["applicationKey"]))return true;
        const auto&rx=x.contains("resolvedApplicationKey")?x["resolvedApplicationKey"]:x["applicationKey"];
        const auto&ry=y.contains("resolvedApplicationKey")?y["resolvedApplicationKey"]:y["applicationKey"];return same(rx,ry);
    }catch(...){return false;}
}
struct NativeAppShortcutPicker::Impl {
    const std::thread::id owner=std::this_thread::get_id();Factory factory;std::shared_ptr<AppShortcutPickerDialog>dialog;
    bool active{},stopped{};std::uint64_t generation{};
    explicit Impl(Factory f):factory(std::move(f)){need(bool(factory));}
    void check()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("App shortcut picker belongs to its creating STA thread");}
};
NativeAppShortcutPicker::NativeAppShortcutPicker(Factory f):impl_(std::make_shared<Impl>(std::move(f))){}
NativeAppShortcutPicker::~NativeAppShortcutPicker(){auto i=impl_;if(std::this_thread::get_id()!=i->owner)std::terminate();i->stopped=true;++i->generation;auto dialog=i->dialog;if(dialog)dialog->cancel();}
std::optional<AppShortcutSelection>NativeAppShortcutPicker::choose(std::uintptr_t owner,const AppShortcutPickerLabels&l){
    auto i=impl_;i->check();labels(l);need(!i->active&&!i->stopped);i->active=true;const auto generation=++i->generation;
    struct Finish{std::shared_ptr<Impl>i;~Finish(){i->active=false;i->dialog.reset();}}finish{i};
    auto dialog=i->factory();need(bool(dialog));if(i->stopped||generation!=i->generation)return {};i->dialog=dialog;
    auto result=dialog->show(owner,l);if(i->stopped||generation!=i->generation)return {};
    if(result)selection(*result);return result;
}
void NativeAppShortcutPicker::cancel(){auto i=impl_;i->check();++i->generation;auto dialog=i->dialog;if(dialog)dialog->cancel();}
bool NativeAppShortcutPicker::presenting()const{impl_->check();return impl_->active;}

#ifdef _WIN32
namespace {
using Microsoft::WRL::ComPtr;
void hr(HRESULT code){if(FAILED(code))throw std::system_error(static_cast<int>(code),std::system_category(),"Windows application operation failed");}
std::wstring wide(std::string_view value){need(text(value));if(value.empty())return {};const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);need(count>0&&count<32767);std::wstring result(static_cast<std::size_t>(count),L'\0');need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count)==count);return result;}
std::string utf8(std::wstring_view value){if(value.empty())return {};need(value.size()<32767);const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);need(count>0&&count<=32768);std::string result(static_cast<std::size_t>(count),'\0');need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr)==count);return result;}
std::wstring extended(std::wstring path){if(path.starts_with(L"\\\\?\\"))return path;return path.starts_with(L"\\\\")?L"\\\\?\\UNC\\"+path.substr(2):L"\\\\?\\"+path;}
std::wstring regular(std::wstring path){if(path.starts_with(L"\\\\?\\UNC\\"))return L"\\\\"+path.substr(8);if(path.starts_with(L"\\\\?\\"))return path.substr(4);return path;}
std::string pathKey(std::string_view path){auto value=wide(path);std::replace(value.begin(),value.end(),L'/',L'\\');const auto count=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr,0);need(count>0&&count<32767);std::wstring folded(static_cast<std::size_t>(count),L'\0');need(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,value.data(),static_cast<int>(value.size()),folded.data(),count,nullptr,nullptr,0)==count);return "path:"+utf8(folded);}
struct Handles {std::vector<HANDLE>values;~Handles(){for(auto value:values)CloseHandle(value);}};
struct File {std::string path,key;};
File file(std::string_view path,const std::shared_ptr<Handles>&hold){
    need(ehud::data::validWindowsFilePath(path));auto value=extended(wide(path));const HANDLE handle=CreateFileW(value.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    need(handle!=INVALID_HANDLE_VALUE);try{hold->values.push_back(handle);}catch(...){CloseHandle(handle);throw;}
    BY_HANDLE_FILE_INFORMATION info{};need(GetFileType(handle)==FILE_TYPE_DISK&&GetFileInformationByHandle(handle,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY));
    const auto count=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);need(count>0&&count<32767);std::wstring finalPath(count,L'\0');const auto written=GetFinalPathNameByHandleW(handle,finalPath.data(),count,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);need(written>0&&written<count);finalPath.resize(written);
    const auto id=(std::uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
    return {utf8(regular(finalPath)),"file:"+std::to_string(info.dwVolumeSerialNumber)+":"+std::to_string(id)};
}
std::string fileName(std::string_view path){return utf8(std::filesystem::path(wide(path)).stem().wstring());}
std::string description(std::string_view path){
    const auto name=extended(wide(path));DWORD unused{};const auto size=GetFileVersionInfoSizeW(name.c_str(),&unused);if(!size||size>1024*1024)return fileName(path);
    std::vector<std::uint8_t>bytes(size);if(!GetFileVersionInfoW(name.c_str(),0,size,bytes.data()))return fileName(path);
    struct Translation{WORD language,codepage;};Translation*translations{};UINT length{};
    if(VerQueryValueW(bytes.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&translations),&length)&&length>=sizeof(Translation)&&length<=16*sizeof(Translation)){
        for(std::size_t n=0;n<length/sizeof(Translation);++n){wchar_t query[80]{};swprintf_s(query,L"\\StringFileInfo\\%04x%04x\\FileDescription",translations[n].language,translations[n].codepage);wchar_t*value{};UINT chars{};
            if(VerQueryValueW(bytes.data(),query,reinterpret_cast<void**>(&value),&chars)&&chars>1&&chars<=32768&&value[chars-1]==0){auto result=nativeShortcutTextRules().trimmed(utf8(std::wstring_view(value,chars-1)));if(!result.empty())return result;}}
    }return fileName(path);
}
std::wstring expand(std::wstring_view value){if(value.empty())return {};const std::wstring input(value);const auto count=ExpandEnvironmentStringsW(input.c_str(),nullptr,0);need(count>0&&count<=32767);std::wstring result(count,L'\0');need(ExpandEnvironmentStringsW(input.c_str(),result.data(),count)==count);result.resize(count-1);return result;}
AppShortcutResolved packaged(const AppShortcutSelection&s){need(aumid(s.value));const auto info=winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(wide(s.value));need(bool(info));const auto id=utf8(info.AppUserModelId().c_str()),family=utf8(info.PackageFamilyName().c_str());need(id==s.value&&!family.empty());
    AppShortcutResolved r;r.selection=s;r.name=nativeShortcutTextRules().trimmed(utf8(info.DisplayInfo().DisplayName().c_str()));if(r.name.empty())r.name=id;r.applicationKey="package:"+id;r.launchKind=AppShortcutLaunchKind::packagedApp;r.appUserModelID=id;return r;
}
struct TaskString{PWSTR value{};~TaskString(){CoTaskMemFree(value);}};
std::string modelID(IShellItem2*item){TaskString value;hr(item->GetString(PKEY_AppUserModel_ID,&value.value));need(value.value!=nullptr);return utf8(value.value);}
AppShortcutResolved inspectWindows(const AppShortcutSelection&s){selection(s);if(s.kind==AppShortcutSelectionKind::packagedApp)return packaged(s);
    auto hold=std::make_shared<Handles>();auto selectedFile=file(s.value,hold);AppShortcutResolved r;r.selection={AppShortcutSelectionKind::file,selectedFile.path};r.selectedFileKey=selectedFile.key;r.applicationKey=pathKey(selectedFile.path);r.lease=hold;
    if(suffix(selectedFile.path)==".exe"){DWORD type{};need(GetBinaryTypeW(extended(wide(selectedFile.path)).c_str(),&type)&&(type==SCS_32BIT_BINARY||type==SCS_64BIT_BINARY));r.executablePath=selectedFile.path;r.name=description(r.executablePath);return r;}
    need(suffix(selectedFile.path)==".lnk");ComPtr<IShellLinkW>link;hr(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)));ComPtr<IPersistFile>persist;hr(link.As(&persist));hr(persist->Load(extended(wide(selectedFile.path)).c_str(),STGM_READ));
    // Inspection reads recorded fields only. Normal Shell execution of the
    // selected .lnk later handles installer-managed targets and arguments.
    // No Resolve/search, installer action or link rewrite occurs on inspect.
    std::vector<wchar_t>buffer(32768,0);hr(link->GetArguments(buffer.data(),static_cast<int>(buffer.size())));need(buffer.back()==0);r.arguments=utf8(buffer.data());
    std::fill(buffer.begin(),buffer.end(),0);hr(link->GetPath(buffer.data(),static_cast<int>(buffer.size()),nullptr,SLGP_RAWPATH));need(buffer.back()==0);const auto path=expand(buffer.data());
    ComPtr<IShellLinkDataList>data;hr(link.As(&data));DWORD flags{};hr(data->GetFlags(&flags));r.runAsUser=(flags&SLDF_RUNAS_USER)!=0;r.launchKind=AppShortcutLaunchKind::shellLink;
    std::fill(buffer.begin(),buffer.end(),0);hr(link->GetWorkingDirectory(buffer.data(),static_cast<int>(buffer.size())));need(buffer.back()==0);r.workingDirectory=utf8(expand(buffer.data()));
    if(!path.empty()){const auto recorded=utf8(path);if(ehud::data::validWindowsFilePath(recorded))r.resolvedApplicationKey=pathKey(recorded);}
    PIDLIST_ABSOLUTE id{};hr(link->GetIDList(&id));struct PIDL{PIDLIST_ABSOLUTE value;~PIDL(){CoTaskMemFree(value);}}pidl{id};
    need(!path.empty()||id!=nullptr||(flags&SLDF_HAS_DARWINID));
    if(path.empty()&&id){ComPtr<IShellItem2>item;if(SUCCEEDED(SHCreateItemFromIDList(id,IID_PPV_ARGS(&item)))){try{const auto app=modelID(item.Get());if(aumid(app))r.resolvedApplicationKey="package:"+app;}catch(const std::exception&){}}}
    r.name=fileName(selectedFile.path);return r;
}
AppShortcutLaunchReceipt launchWindows(const AppShortcutResolved&r,std::uintptr_t owner){resolved(r);
    if(r.launchKind==AppShortcutLaunchKind::packagedApp){ComPtr<IApplicationActivationManager>manager;hr(CoCreateInstance(CLSID_ApplicationActivationManager,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&manager)));DWORD pid{};hr(manager->ActivateApplication(wide(r.appUserModelID).c_str(),wide(r.arguments).c_str(),AO_NOERRORUI,&pid));return {pid};}
    const bool link=r.launchKind==AppShortcutLaunchKind::shellLink;const auto path=wide(link?r.selection.value:r.executablePath),arguments=wide(link?std::string_view{}:std::string_view(r.arguments)),directory=wide(link?std::string_view{}:std::string_view(r.workingDirectory));
    SHELLEXECUTEINFOW execute{};execute.cbSize=sizeof(execute);execute.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI|SEE_MASK_UNICODE;execute.hwnd=reinterpret_cast<HWND>(owner);execute.lpVerb=L"open";execute.lpFile=path.c_str();execute.lpParameters=arguments.empty()?nullptr:arguments.c_str();execute.lpDirectory=directory.empty()?nullptr:directory.c_str();execute.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&execute))throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Windows application launch failed");
    DWORD pid{};if(execute.hProcess){pid=GetProcessId(execute.hProcess);CloseHandle(execute.hProcess);}return {pid};
}
class WindowsPicker final:public AppShortcutPickerDialog {
    ComPtr<IFileOpenDialog>dialog_;bool canceled_{};
public:
    std::optional<AppShortcutSelection>show(std::uintptr_t owner,const AppShortcutPickerLabels&l)override{
        need(dialog_.Get()==nullptr);labels(l);hr(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog_)));DWORD options{};hr(dialog_->GetOptions(&options));
        options&=~(FOS_ALLOWMULTISELECT|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_ALLNONSTORAGEITEMS);options|=FOS_NODEREFERENCELINKS|FOS_DONTADDTORECENT|FOS_NOCHANGEDIR;
        const auto label=wide(l.applications);if(l.mode==AppShortcutPickerMode::files){options|=FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST;const COMDLG_FILTERSPEC filter{label.c_str(),L"*.exe;*.lnk"};hr(dialog_->SetFileTypes(1,&filter));}
        else{options|=FOS_ALLNONSTORAGEITEMS;ComPtr<IShellItem>folder;hr(SHCreateItemInKnownFolder(FOLDERID_AppsFolder,0,nullptr,IID_PPV_ARGS(&folder)));hr(dialog_->SetFolder(folder.Get()));}
        hr(dialog_->SetOptions(options));hr(dialog_->SetTitle(wide(l.title).c_str()));hr(dialog_->SetOkButtonLabel(wide(l.accept).c_str()));
        if(canceled_)return {};const auto result=dialog_->Show(reinterpret_cast<HWND>(owner));if(canceled_||result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};hr(result);
        ComPtr<IShellItem>item;hr(dialog_->GetResult(&item));TaskString path;if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path.value))&&path.value)return AppShortcutSelection{AppShortcutSelectionKind::file,utf8(path.value)};
        ComPtr<IShellItem2>properties;hr(item.As(&properties));return AppShortcutSelection{AppShortcutSelectionKind::packagedApp,modelID(properties.Get())};
    }
    void cancel()noexcept override{canceled_=true;if(dialog_)dialog_->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));}
};
}
AppShortcutOS windowsAppShortcutOS(){return {[](const AppShortcutSelection&s){
    try{return inspectWindows(s);}catch(const winrt::hresult_error&e){throw std::system_error(static_cast<int>(e.code()),std::system_category(),"Windows application metadata failed");}
},launchWindows};}
NativeAppShortcutPicker::Factory windowsAppShortcutPickerFactory(){return []{return std::make_shared<WindowsPicker>();};}
#endif
}
