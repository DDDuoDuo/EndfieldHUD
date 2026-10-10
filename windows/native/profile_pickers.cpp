#include "native/profile_pickers.hpp"
#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <set>
#include <stdexcept>
#include <commdlg.h>
#include <colordlg.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
std::wstring wide(std::string_view text){
    if(text.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(n<=0)throw std::invalid_argument("Invalid UTF-8 picker text");
    std::wstring out(static_cast<std::size_t>(n),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),n);return out;
}
void checked(HRESULT hr,const char*what){if(FAILED(hr))throw std::runtime_error(what);}
std::uint8_t channel(double v)noexcept{return static_cast<std::uint8_t>(std::clamp<long>(std::lround(std::isfinite(v)?v*255:0),0,255));}
std::optional<int>field(std::wstring_view s){
    if(s.empty()||s.size()>3)return std::nullopt;int v{};
    for(const auto c:s){if(c<L'0'||c>L'9')return std::nullopt;v=v*10+(c-L'0');}
    return v<=255?std::optional(v):std::nullopt;
}
// ChooseColorW hook: the dialog procedure's messages arrive here first.
constexpr UINT settleMessage=WM_APP+0x51;
constexpr wchar_t streamProperty[]=L"EndfieldHUD.ProfileColorStream";
struct HookState {ProfileColorStream stream;std::wstring title;ProfilePickerSession*session{};};
struct Detach {ProfilePickerSession*session;~Detach(){if(session)session->detach();}};
std::wstring text(HWND dialog,int id){wchar_t buffer[8]{};const UINT n=GetDlgItemTextW(dialog,id,buffer,8);return std::wstring(buffer,std::min<std::size_t>(n,7));}
UINT_PTR CALLBACK colorHook(HWND dialog,UINT message,WPARAM w,LPARAM l){
    if(message==WM_INITDIALOG){
        const auto*options=reinterpret_cast<const CHOOSECOLORW*>(l);auto*state=reinterpret_cast<HookState*>(options->lCustData);
        SetPropW(dialog,streamProperty,state);SetWindowTextW(dialog,state->title.c_str());
        if(state->session)state->session->attach([dialog]{PostMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDCANCEL,BN_CLICKED),0);});
        return TRUE;
    }
    auto*state=static_cast<HookState*>(GetPropW(dialog,streamProperty));if(!state)return FALSE;
    if(message==WM_COMMAND&&HIWORD(w)==EN_CHANGE&&(LOWORD(w)==COLOR_RED||LOWORD(w)==COLOR_GREEN||LOWORD(w)==COLOR_BLUE)){
        if(state->stream.fieldsChanged())PostMessageW(dialog,settleMessage,0,0);
        return FALSE;
    }
    if(message==settleMessage){state->stream.settle(profileColorFromFields(text(dialog,COLOR_RED),text(dialog,COLOR_GREEN),text(dialog,COLOR_BLUE)));return TRUE;}
    if(message==WM_DESTROY){if(state->session)state->session->detach();RemovePropW(dialog,streamProperty);}
    return FALSE;
}
}
ProfileImagePickerText profileImagePickerText(modules::ProfileImageKind kind,core::Language language){
    const bool avatar=kind==modules::ProfileImageKind::avatar;
    return {wide(avatar?core::localized("Choose profile picture","选择头像",language):core::localized("Choose profile background","选择名片背景",language)),
        wide(core::localized("Choose","选择",language)),wide(core::localized("Images","图片",language))};
}
std::wstring profileImageFilterSpec(){
    ComPtr<IWICImagingFactory>factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Create WIC factory");
    ComPtr<IEnumUnknown>components;checked(factory->CreateComponentEnumerator(WICDecoder,WICComponentEnumerateDefault,&components),"Enumerate WIC decoders");
    std::set<std::wstring>extensions;ComPtr<IUnknown>item;ULONG fetched{};
    while(components->Next(1,&item,&fetched)==S_OK&&fetched==1){
        ComPtr<IWICBitmapCodecInfo>info;
        if(SUCCEEDED(item.As(&info))){
            UINT length{};if(SUCCEEDED(info->GetFileExtensions(0,nullptr,&length))&&length>1&&length<4096){
                std::wstring list(length,L'\0');
                if(SUCCEEDED(info->GetFileExtensions(length,list.data(),&length))){
                    list.resize(wcsnlen(list.c_str(),list.size()));std::size_t start{};
                    while(start<=list.size()){
                        const auto end=std::min(list.find(L',',start),list.size());auto ext=list.substr(start,end-start);
                        ext.erase(0,ext.find_first_not_of(L" \t"));ext.erase(ext.find_last_not_of(L" \t")+1);
                        if(ext.size()>1&&ext[0]==L'.'&&ext.find_first_of(L";*?")==std::wstring::npos){
                            std::transform(ext.begin(),ext.end(),ext.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});extensions.insert(L"*"+ext);}
                        start=end+1;
                    }
                }
            }
        }
        item.Reset();
    }
    if(extensions.empty())throw std::runtime_error("No installed image decoders");
    std::wstring spec;for(const auto&e:extensions){if(!spec.empty())spec+=L';';spec+=e;}return spec;
}
void ProfilePickerSession::cancel(){if(close_){const auto close=close_;close();}}
std::optional<std::filesystem::path>chooseProfileImage(HWND owner,modules::ProfileImageKind kind,core::Language language,ProfilePickerSession*session){
    const auto strings=profileImagePickerText(kind,language);const auto spec=profileImageFilterSpec();
    ComPtr<IFileOpenDialog>dialog;checked(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)),"Create the image chooser");
    FILEOPENDIALOGOPTIONS flags{};checked(dialog->GetOptions(&flags),"Read chooser options");
    flags|=FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR;
    flags&=~static_cast<FILEOPENDIALOGOPTIONS>(FOS_ALLOWMULTISELECT|FOS_NODEREFERENCELINKS);
    checked(dialog->SetOptions(flags),"Set chooser options");
    const COMDLG_FILTERSPEC filter{strings.filterName.c_str(),spec.c_str()};
    checked(dialog->SetFileTypes(1,&filter),"Set image filter");checked(dialog->SetTitle(strings.title.c_str()),"Set chooser title");
    checked(dialog->SetOkButtonLabel(strings.okLabel.c_str()),"Set chooser prompt");
    if(session)session->attach([raw=dialog.Get()]{raw->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));});
    const Detach detach{session};
    const auto shown=dialog->Show(owner);
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED))return std::nullopt;checked(shown,"Show the image chooser");
    ComPtr<IShellItem>item;checked(dialog->GetResult(&item),"Read the chosen image");
    PWSTR path{};checked(item->GetDisplayName(SIGDN_FILESYSPATH,&path),"Resolve the chosen image path");
    std::filesystem::path result(path);CoTaskMemFree(path);return result;
}
std::wstring profileColorPickerTitle(core::Language language){return wide(core::localized("Card color","名片颜色",language));}
std::optional<std::array<double,3>>chooseProfileColor(HWND owner,std::array<double,3>initial,core::Language language,const std::function<void(std::array<double,3>)>&live,ProfilePickerSession*session){
    // The dialog's sixteen custom swatches persist for the process, like the shared NSColorPanel.
    static std::array<COLORREF,16>custom=[]{std::array<COLORREF,16>white;white.fill(RGB(255,255,255));return white;}();
    HookState state{ProfileColorStream(live,profileColorFromRef(profileColorRef(initial))),profileColorPickerTitle(language),session};
    const Detach detach{session};
    CHOOSECOLORW options{};options.lStructSize=sizeof options;options.hwndOwner=owner;options.rgbResult=profileColorRef(initial);
    options.lpCustColors=custom.data();options.Flags=CC_RGBINIT|CC_FULLOPEN|CC_ANYCOLOR|CC_ENABLEHOOK;
    options.lCustData=reinterpret_cast<LPARAM>(&state);options.lpfnHook=colorHook;
    if(!ChooseColorW(&options)){
        if(const auto error=CommDlgExtendedError())throw std::runtime_error("The colour picker failed ("+std::to_string(error)+")");
        return std::nullopt;
    }
    return profileColorFromRef(options.rgbResult);
}
COLORREF profileColorRef(std::array<double,3>c)noexcept{return RGB(channel(c[0]),channel(c[1]),channel(c[2]));}
std::array<double,3>profileColorFromRef(COLORREF c)noexcept{return {GetRValue(c)/255.,GetGValue(c)/255.,GetBValue(c)/255.};}
std::optional<std::array<double,3>>profileColorFromFields(std::wstring_view r,std::wstring_view g,std::wstring_view b){
    const auto red=field(r),green=field(g),blue=field(b);
    if(!red||!green||!blue)return std::nullopt;
    return std::array<double,3>{*red/255.,*green/255.,*blue/255.};
}
ProfileColorStream::ProfileColorStream(std::function<void(std::array<double,3>)>live,std::optional<std::array<double,3>>initial):live_(std::move(live)),last_(initial){}
bool ProfileColorStream::fieldsChanged()noexcept{if(pending_)return false;pending_=true;return true;}
bool ProfileColorStream::settle(std::optional<std::array<double,3>>fields){
    pending_=false;if(!fields||fields==last_)return false;
    last_=fields;if(live_)live_(*fields);return true;
}
}
#endif
