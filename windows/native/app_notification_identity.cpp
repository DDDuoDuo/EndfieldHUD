#include "native/app_notification_identity.hpp"
#include "core/data/data_store.hpp"
#include <algorithm>
#include <stdexcept>
#include <optional>

namespace endfield::native {
bool validAppNotificationID(std::string_view s)noexcept{
    return !s.empty()&&s.size()<=128&&s.front()!='.'&&s.back()!='.'&&
        std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_';});
}
bool validAppNotificationCLSID(std::string_view s)noexcept{return s.size()==38&&s.front()=='{'&&s.back()=='}'&&ehud::data::validUUID(s.substr(1,36));}
bool validAppNotificationIdentity(const AppNotificationIdentity&i)noexcept{
    const auto paths=ehud::data::validWindowsFilePath(i.executable)&&ehud::data::validWindowsFilePath(i.workingDirectory)&&ehud::data::validWindowsFilePath(i.shortcut);
    const auto suffix=i.shortcut.size()>=4?std::string_view(i.shortcut).substr(i.shortcut.size()-4):std::string_view{};
    return validAppNotificationID(i.appUserModelID)&&validAppNotificationCLSID(i.activatorCLSID)&&paths&&
        (suffix==".lnk"||suffix==".LNK")&&i.executable.find('"')==i.executable.npos;
}
std::string appNotificationServerCommand(const AppNotificationIdentity&i){if(!validAppNotificationIdentity(i))throw std::invalid_argument("Invalid explicit notification identity");return "\""+i.executable+"\" "+std::string(appNotificationActivationArgument);}
}

#ifdef _WIN32
#include <initguid.h>
#include <propkey.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#include <system_error>

namespace endfield::native {namespace {
using Microsoft::WRL::ComPtr;
struct Failure{HRESULT hr;};
void need(bool b){if(!b)throw Failure{E_INVALIDARG};}
void checked(HRESULT hr){if(FAILED(hr))throw Failure{hr};}
void registry(LSTATUS s){if(s!=ERROR_SUCCESS)throw Failure{HRESULT_FROM_WIN32(s)};}
template<class F>HRESULT protect(F&&f)noexcept{try{f();return S_OK;}catch(const Failure&e){return e.hr;}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}}
std::wstring wide(std::string_view s){need(s.size()<=32768&&ehud::data::Json::validUtf8(s)&&s.find('\0')==s.npos);if(s.empty())return {};const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);if(!n)throw Failure{HRESULT_FROM_WIN32(GetLastError())};std::wstring out(static_cast<std::size_t>(n),L'\0');need(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n)==n);return out;}
std::string utf8(PCWSTR s,std::size_t bound){need(s);std::size_t n{};while(n<=bound&&s[n])++n;need(n<=bound);if(!n)return {};const auto bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,static_cast<int>(n),nullptr,0,nullptr,nullptr);if(!bytes)throw Failure{E_INVALIDARG};std::string out(static_cast<std::size_t>(bytes),'\0');need(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,static_cast<int>(n),out.data(),bytes,nullptr,nullptr)==bytes);return out;}
GUID guid(std::string_view s){need(validAppNotificationCLSID(s));GUID g{};const auto text=wide(s);checked(CLSIDFromString(text.c_str(),&g));return g;}
void ownerSTA(){APTTYPE a{};APTTYPEQUALIFIER q{};checked(CoGetApartmentType(&a,&q));need(a==APTTYPE_STA||a==APTTYPE_MAINSTA);}
struct Key{HKEY value{};Key()=default;~Key(){if(value)RegCloseKey(value);}Key(const Key&)=delete;Key&operator=(const Key&)=delete;};
std::wstring serverKey(const AppNotificationIdentity&i){return L"CLSID\\"+wide(i.activatorCLSID)+L"\\LocalServer32";}
std::optional<std::wstring>registryCommand(HKEY root,const AppNotificationIdentity&i){Key k;const auto path=serverKey(i);const auto opened=RegOpenKeyExW(root,path.c_str(),0,KEY_QUERY_VALUE,&k.value);if(opened==ERROR_FILE_NOT_FOUND)return {};registry(opened);DWORD type{},bytes{};const auto query=RegQueryValueExW(k.value,nullptr,nullptr,&type,nullptr,&bytes);if(query==ERROR_FILE_NOT_FOUND)return std::wstring{};registry(query);need(type==REG_SZ&&bytes>=sizeof(wchar_t)&&bytes<=65536*sizeof(wchar_t)&&bytes%sizeof(wchar_t)==0);std::wstring value(bytes/sizeof(wchar_t),L'\0');registry(RegQueryValueExW(k.value,nullptr,nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&bytes));need(!value.empty()&&value.back()==L'\0'&&value.find(L'\0')==value.size()-1);value.pop_back();return value;}
struct File{HANDLE value{INVALID_HANDLE_VALUE};~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}File(const File&)=delete;File&operator=(const File&)=delete;File()=default;};
void ordinaryFile(File&file,PCWSTR path,DWORD access=FILE_READ_ATTRIBUTES,DWORD sharing=FILE_SHARE_READ){file.value=CreateFileW(path,access,sharing,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(file.value==INVALID_HANDLE_VALUE)throw Failure{HRESULT_FROM_WIN32(GetLastError())};FILE_ATTRIBUTE_TAG_INFO info{};if(!GetFileInformationByHandleEx(file.value,FileAttributeTagInfo,&info,sizeof(info)))throw Failure{HRESULT_FROM_WIN32(GetLastError())};need(!(info.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)));}
void actualPaths(const AppNotificationIdentity&i){File exe;const auto path=wide(i.executable);ordinaryFile(exe,path.c_str());const auto directory=wide(i.workingDirectory);const auto attributes=GetFileAttributesW(directory.c_str());need(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT));}
struct Variant{PROPVARIANT value{};~Variant(){PropVariantClear(&value);}};
void stringValue(PROPVARIANT&v,const std::wstring&s){v.vt=VT_LPWSTR;v.pwszVal=static_cast<PWSTR>(CoTaskMemAlloc((s.size()+1)*sizeof(wchar_t)));if(!v.pwszVal)throw std::bad_alloc{};std::copy(s.begin(),s.end(),v.pwszVal);v.pwszVal[s.size()]=0;}
bool sameShortcut(const AppNotificationIdentity&i){
    ComPtr<IShellLinkW>link;checked(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)));ComPtr<IPersistFile>persist;checked(link.As(&persist));const auto file=wide(i.shortcut);checked(persist->Load(file.c_str(),STGM_READ));
    std::vector<wchar_t>path(32768),args(32768),directory(32768);checked(link->GetPath(path.data(),static_cast<int>(path.size()),nullptr,SLGP_RAWPATH));checked(link->GetArguments(args.data(),static_cast<int>(args.size())));checked(link->GetWorkingDirectory(directory.data(),static_cast<int>(directory.size())));
    if(wide(i.executable)!=path.data()||wide(i.workingDirectory)!=directory.data()||args[0]!=L'\0')return false;
    ComPtr<IPropertyStore>store;checked(link.As(&store));Variant id,clsid;checked(store->GetValue(PKEY_AppUserModel_ID,&id.value));checked(store->GetValue(PKEY_AppUserModel_ToastActivatorCLSID,&clsid.value));return id.value.vt==VT_LPWSTR&&id.value.pwszVal&&wide(i.appUserModelID)==id.value.pwszVal&&clsid.value.vt==VT_CLSID&&clsid.value.puuid&&*clsid.value.puuid==guid(i.activatorCLSID);
}
bool exists(const std::wstring&path){const auto a=GetFileAttributesW(path.c_str());if(a!=INVALID_FILE_ATTRIBUTES)return true;const auto e=GetLastError();if(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND)return false;throw Failure{HRESULT_FROM_WIN32(e)};}
void emptyKeyRemove(HKEY root,const std::wstring&path){Key k;const auto opened=RegOpenKeyExW(root,path.c_str(),0,KEY_QUERY_VALUE|KEY_ENUMERATE_SUB_KEYS,&k.value);if(opened==ERROR_FILE_NOT_FOUND)return;registry(opened);DWORD subkeys{},values{};registry(RegQueryInfoKeyW(k.value,nullptr,nullptr,nullptr,&subkeys,nullptr,nullptr,&values,nullptr,nullptr,nullptr,nullptr));if(!subkeys&&!values)registry(RegDeleteKeyW(root,path.c_str()));}
void removeBinding(HKEY root,const AppNotificationIdentity&i){const auto command=registryCommand(root,i);if(!command||*command!=wide(appNotificationServerCommand(i)))return;Key k;const auto path=serverKey(i);registry(RegOpenKeyExW(root,path.c_str(),0,KEY_SET_VALUE,&k.value));registry(RegDeleteValueW(k.value,nullptr));emptyKeyRemove(root,path);emptyKeyRemove(root,L"CLSID\\"+wide(i.activatorCLSID));}
void saveShortcut(const AppNotificationIdentity&i,const std::wstring&path){ComPtr<IShellLinkW>link;checked(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)));const auto exe=wide(i.executable),dir=wide(i.workingDirectory);checked(link->SetPath(exe.c_str()));checked(link->SetWorkingDirectory(dir.c_str()));checked(link->SetArguments(L""));ComPtr<IPropertyStore>properties;checked(link.As(&properties));Variant id,clsid;stringValue(id.value,wide(i.appUserModelID));clsid.value.vt=VT_CLSID;clsid.value.puuid=static_cast<CLSID*>(CoTaskMemAlloc(sizeof(CLSID)));if(!clsid.value.puuid)throw std::bad_alloc{};*clsid.value.puuid=guid(i.activatorCLSID);checked(properties->SetValue(PKEY_AppUserModel_ID,id.value));checked(properties->SetValue(PKEY_AppUserModel_ToastActivatorCLSID,clsid.value));checked(properties->Commit());ComPtr<IPersistFile>persist;checked(link.As(&persist));checked(persist->Save(path.c_str(),TRUE));}
}
HRESULT inspectAppNotificationRegistration(HKEY root,const AppNotificationIdentity&i,AppNotificationRegistration*out)noexcept{return protect([&]{need(root&&out&&validAppNotificationIdentity(i));ownerSTA();*out=AppNotificationRegistration::conflict;const auto command=registryCommand(root,i);const auto shortcut=wide(i.shortcut);const bool file=exists(shortcut);if(!command&&!file){*out=AppNotificationRegistration::missing;return;}if(command&&*command==wide(appNotificationServerCommand(i))&&file){File held;ordinaryFile(held,shortcut.c_str());if(sameShortcut(i))*out=AppNotificationRegistration::matching;}});}
HRESULT installAppNotificationIdentity(HKEY root,const AppNotificationIdentity&i,AppNotificationRegistryLifetime lifetime)noexcept{return protect([&]{
    need(root&&validAppNotificationIdentity(i)&&(lifetime==AppNotificationRegistryLifetime::persistent||lifetime==AppNotificationRegistryLifetime::volatileFixture));ownerSTA();actualPaths(i);const auto command=registryCommand(root,i);const auto expected=wide(appNotificationServerCommand(i)),shortcut=wide(i.shortcut);if(command&&*command!=expected)throw Failure{HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)};
    if(exists(shortcut)){File held;ordinaryFile(held,shortcut.c_str());if(command&&sameShortcut(i))return;throw Failure{HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)};}
    GUID temporary{};checked(CoCreateGuid(&temporary));wchar_t token[40]{};need(StringFromGUID2(temporary,token,40)>0);const auto staging=shortcut+L"."+token+L".stage";
    File reserved;reserved.value=CreateFileW(staging.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(reserved.value==INVALID_HANDLE_VALUE)throw Failure{HRESULT_FROM_WIN32(GetLastError())};CloseHandle(reserved.value);reserved.value=INVALID_HANDLE_VALUE;
    bool newBinding{},newParent{};const auto parent=L"CLSID\\"+wide(i.activatorCLSID);const DWORD keyOptions=lifetime==AppNotificationRegistryLifetime::volatileFixture?REG_OPTION_VOLATILE:REG_OPTION_NON_VOLATILE;try{saveShortcut(i,staging);if(!command){Key clsid,k;DWORD parentDisposition{},disposition{};registry(RegCreateKeyExW(root,parent.c_str(),0,nullptr,keyOptions,KEY_CREATE_SUB_KEY|KEY_QUERY_VALUE,nullptr,&clsid.value,&parentDisposition));newParent=parentDisposition==REG_CREATED_NEW_KEY;registry(RegCreateKeyExW(clsid.value,L"LocalServer32",0,nullptr,keyOptions,KEY_QUERY_VALUE|KEY_SET_VALUE,nullptr,&k.value,&disposition));if(disposition!=REG_CREATED_NEW_KEY)throw Failure{HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)};newBinding=true;registry(RegSetValueExW(k.value,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(expected.c_str()),static_cast<DWORD>((expected.size()+1)*sizeof(wchar_t))));}
        if(!MoveFileExW(staging.c_str(),shortcut.c_str(),MOVEFILE_WRITE_THROUGH))throw Failure{HRESULT_FROM_WIN32(GetLastError())};
    }catch(...){DeleteFileW(staging.c_str());if(newBinding){try{removeBinding(root,i);emptyKeyRemove(root,serverKey(i));}catch(...){}}if(newParent){try{emptyKeyRemove(root,parent);}catch(...){}}throw;}
});}
HRESULT removeAppNotificationIdentity(HKEY root,const AppNotificationIdentity&i)noexcept{return protect([&]{need(root&&validAppNotificationIdentity(i));ownerSTA();const auto command=registryCommand(root,i);const auto shortcut=wide(i.shortcut);if(!command&&!exists(shortcut))return;need(command&&*command==wide(appNotificationServerCommand(i))&&exists(shortcut));// Shell IPersistFile may open its reader without delete sharing. Validate
    // while a read lease prevents writes, then acquire deletion only after the
    // Shell reader has released. File IDs close the intervening path race.
    File inspected;ordinaryFile(inspected,shortcut.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE);need(sameShortcut(i));FILE_ID_INFO before{};if(!GetFileInformationByHandleEx(inspected.value,FileIdInfo,&before,sizeof(before)))throw Failure{HRESULT_FROM_WIN32(GetLastError())};
    File held;ordinaryFile(held,shortcut.c_str(),FILE_READ_ATTRIBUTES|DELETE);FILE_ID_INFO after{};if(!GetFileInformationByHandleEx(held.value,FileIdInfo,&after,sizeof(after)))throw Failure{HRESULT_FROM_WIN32(GetLastError())};need(before.VolumeSerialNumber==after.VolumeSerialNumber&&std::equal(std::begin(before.FileId.Identifier),std::end(before.FileId.Identifier),std::begin(after.FileId.Identifier)));FILE_DISPOSITION_INFO disposition{TRUE};if(!SetFileInformationByHandle(held.value,FileDispositionInfo,&disposition,sizeof(disposition)))throw Failure{HRESULT_FROM_WIN32(GetLastError())};removeBinding(root,i);});}
HRESULT establishAppNotificationProcessIdentity(std::string_view id)noexcept{return protect([&]{need(validAppNotificationID(id));const auto text=wide(id);checked(SetCurrentProcessExplicitAppUserModelID(text.c_str()));});}

struct detail::AppNotificationActivationState:std::enable_shared_from_this<detail::AppNotificationActivationState>{
    const std::string appID;const GUID clsid;const AppNotificationActivationRoute route;const DWORD thread{GetCurrentThreadId()};std::mutex mutex;std::vector<AppNotificationActivation>pending;bool alive{true},posted{},starting{};DWORD cookie{};
    AppNotificationActivationState(std::string a,GUID c,AppNotificationActivationRoute r):appID(std::move(a)),clsid(c),route(r){pending.reserve(appNotificationMaximumQueuedActivations);}
    HRESULT activate(PCWSTR id,PCWSTR args,ULONG inputs)noexcept{return protect([&]{need(!inputs&&utf8(id,128)==appID);auto text=utf8(args,appNotificationMaximumArguments);need(text.size()<=appNotificationMaximumArguments);std::lock_guard lock(mutex);if(!alive)throw Failure{RO_E_CLOSED};if(pending.size()>=appNotificationMaximumQueuedActivations)throw Failure{HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)};pending.push_back({std::move(text)});if(!posted){if(!PostMessageW(route.owner,route.message,static_cast<WPARAM>(route.generation),0)){pending.pop_back();throw Failure{HRESULT_FROM_WIN32(GetLastError())};}posted=true;}});}
};
namespace {
class Activation final:public INotificationActivationCallback{
    std::atomic<ULONG>refs_{1};std::weak_ptr<detail::AppNotificationActivationState>state_;
public:explicit Activation(std::weak_ptr<detail::AppNotificationActivationState>s):state_(std::move(s)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=__uuidof(INotificationActivationCallback))return E_NOINTERFACE;*out=static_cast<INotificationActivationCallback*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Activate(LPCWSTR id,LPCWSTR args,const NOTIFICATION_USER_INPUT_DATA*,ULONG count)override{const auto s=state_.lock();return s?s->activate(id,args,count):RO_E_CLOSED;}
};
class ActivationFactory final:public IClassFactory{
    std::atomic<ULONG>refs_{1};std::weak_ptr<detail::AppNotificationActivationState>state_;
public:explicit ActivationFactory(std::weak_ptr<detail::AppNotificationActivationState>s):state_(std::move(s)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(iid!=IID_IUnknown&&iid!=IID_IClassFactory)return E_NOINTERFACE;*out=static_cast<IClassFactory*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*outer,REFIID iid,void**out)override{if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;return protect([&]{const auto s=state_.lock();if(!s)throw Failure{RO_E_CLOSED};{std::lock_guard lock(s->mutex);if(!s->alive)throw Failure{RO_E_CLOSED};}ComPtr<Activation>callback;callback.Attach(new Activation(s));checked(callback->QueryInterface(iid,out));});}
    HRESULT STDMETHODCALLTYPE LockServer(BOOL)override{return S_OK;}
};
}
NativeAppNotificationActivation::NativeAppNotificationActivation(std::string id,std::string clsid,AppNotificationActivationRoute r){const auto result=protect([&]{need(validAppNotificationID(id)&&validAppNotificationCLSID(clsid)&&r.owner&&r.message>=WM_APP&&r.message<=0xbfff&&r.generation&&static_cast<WPARAM>(r.generation)==r.generation);DWORD process{};need(IsWindow(r.owner)&&GetWindowThreadProcessId(r.owner,&process)==GetCurrentThreadId()&&process==GetCurrentProcessId());ownerSTA();impl_=std::make_shared<detail::AppNotificationActivationState>(std::move(id),guid(clsid),r);});if(FAILED(result))throw std::system_error(static_cast<int>(result),std::system_category(),"Notification activation owner");}
NativeAppNotificationActivation::~NativeAppNotificationActivation(){if(GetCurrentThreadId()!=impl_->thread)std::terminate();(void)stop();}
HRESULT NativeAppNotificationActivation::start()noexcept{return protect([&]{const auto s=impl_;if(GetCurrentThreadId()!=s->thread)throw Failure{RPC_E_WRONG_THREAD};ownerSTA();{std::lock_guard lock(s->mutex);if(!s->alive)throw Failure{RO_E_CLOSED};}if(s->cookie)return;if(s->starting)throw Failure{E_UNEXPECTED};s->starting=true;struct Starting{bool&flag;~Starting(){flag=false;}}starting{s->starting};ComPtr<ActivationFactory>factory;factory.Attach(new ActivationFactory(s));DWORD cookie{};checked(CoRegisterClassObject(s->clsid,factory.Get(),CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&cookie));bool closed{};{std::lock_guard lock(s->mutex);closed=!s->alive;if(!closed)s->cookie=cookie;}if(closed){CoRevokeClassObject(cookie);throw Failure{RO_E_CLOSED};}});}
HRESULT NativeAppNotificationActivation::stop()noexcept{return protect([&]{const auto s=impl_;if(GetCurrentThreadId()!=s->thread)throw Failure{RPC_E_WRONG_THREAD};{std::lock_guard lock(s->mutex);s->alive=false;s->pending.clear();s->posted=false;}if(s->cookie){const auto cookie=std::exchange(s->cookie,0u);checked(CoRevokeClassObject(cookie));}});}
std::vector<AppNotificationActivation>NativeAppNotificationActivation::take(){const auto s=impl_;if(GetCurrentThreadId()!=s->thread)throw std::logic_error("Notification activation owner thread required");std::vector<AppNotificationActivation>out;out.reserve(appNotificationMaximumQueuedActivations);std::lock_guard lock(s->mutex);if(!s->alive)return out;out.swap(s->pending);s->posted=false;return out;}
bool NativeAppNotificationActivation::registered()const noexcept{return impl_->cookie!=0;}
HRESULT NativeAppNotificationActivation::createCallback(INotificationActivationCallback**out)noexcept{if(!out)return E_POINTER;*out=nullptr;return protect([&]{const auto s=impl_;need(GetCurrentThreadId()==s->thread);{std::lock_guard lock(s->mutex);if(!s->alive)throw Failure{RO_E_CLOSED};}*out=new Activation(s);});}
}
#endif
