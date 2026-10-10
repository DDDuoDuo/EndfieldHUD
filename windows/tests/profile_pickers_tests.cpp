// Personal Profile host pickers without showing any dialog: source strings,
// the WIC image filter, colour conversion and the live colour stream.
#include "native/profile_pickers.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#ifdef _WIN32
#include <objbase.h>
namespace {
namespace m=endfield::modules;namespace n=endfield::native;using endfield::core::Language;
unsigned checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
void run(){
    // NSOpenPanel title/prompt of HUDPersonalProfileInteraction.chooseImage.
    const auto avatar=n::profileImagePickerText(m::ProfileImageKind::avatar,Language::english);
    check(avatar.title==L"Choose profile picture"&&avatar.okLabel==L"Choose","Avatar chooser strings");
    check(n::profileImagePickerText(m::ProfileImageKind::background,Language::english).title==L"Choose profile background","Background chooser title");
    const auto chinese=n::profileImagePickerText(m::ProfileImageKind::avatar,Language::simplifiedChinese);
    check(chinese.title==L"选择头像"&&chinese.okLabel==L"选择","Chinese chooser strings");
    check(n::profileImagePickerText(m::ProfileImageKind::background,Language::simplifiedChinese).title==L"选择名片背景","Chinese background title");
    check(n::profileColorPickerTitle(Language::english)==L"Card color"&&n::profileColorPickerTitle(Language::simplifiedChinese)==L"名片颜色","NSColorPanel title");
    // UTType.image: the installed WIC decoders.
    const auto spec=n::profileImageFilterSpec();
    for(const auto*ext:{L"*.png",L"*.jpg",L"*.jpeg",L"*.gif",L"*.bmp",L"*.tif",L"*.tiff",L"*.ico"})
        check((L";"+spec+L";").find(std::wstring(L";")+ext+L";")!=std::wstring::npos,"Built-in WIC decoders are offered");
    std::size_t start{};std::wstring previous;
    while(start<=spec.size()){const auto end=std::min(spec.find(L';',start),spec.size());const auto item=spec.substr(start,end-start);
        check(item.size()>2&&item.starts_with(L"*.")&&item>previous,"Sorted unique *.ext patterns");previous=item;start=end+1;}
    // Colour conversion like String(format:"%02X", Int((c*255).rounded())).
    check(n::profileColorRef({1,0,.5})==RGB(255,0,128)&&n::profileColorRef({-1,2,std::nan("")})==RGB(0,255,0),"Rounded, clamped 8-bit channels");
    const auto back=n::profileColorFromRef(RGB(0x6E,0xDF,0xE8));check(back[0]==0x6E/255.&&back[1]==0xDF/255.&&back[2]==0xE8/255.,"COLORREF to sRGB");
    check(n::profileColorFromFields(L"255",L"0",L"128")==std::array<double,3>{1,0,128/255.},"Dialog fields to sRGB");
    check(!n::profileColorFromFields(L"",L"0",L"0")&&!n::profileColorFromFields(L"256",L"0",L"0")&&!n::profileColorFromFields(L"1a",L"0",L"0")&&!n::profileColorFromFields(L"0",L"-1",L"0"),"Incomplete or invalid fields are ignored");
    // Session: hiding the HUD closes an open picker; an idle session ignores it.
    n::ProfilePickerSession session;unsigned closes{};session.cancel();check(!session.open(),"Idle session");
    session.attach([&]{++closes;});check(session.open(),"A running picker registers its close");session.cancel();check(closes==1,"cancel() closes the running picker");
    session.detach();session.cancel();check(closes==1&&!session.open(),"A finished picker is never closed again");
    // The live stream: one settle per burst, no repeats, the initial colour is not re-sent.
    std::vector<std::array<double,3>>seen;n::ProfileColorStream stream([&](std::array<double,3>c){seen.push_back(c);},std::array<double,3>{1,0,0});
    check(stream.fieldsChanged()&&!stream.fieldsChanged()&&!stream.fieldsChanged(),"Three field edits post one settle message");
    check(!stream.settle(std::array<double,3>{1,0,0})&&seen.empty(),"The starting colour is not streamed again");
    check(stream.fieldsChanged()&&stream.settle(n::profileColorFromFields(L"0",L"0",L"255"))&&seen.size()==1&&seen[0]==std::array<double,3>{0,0,1},"A settled change streams once");
    check(stream.fieldsChanged()&&!stream.settle(std::nullopt)&&seen.size()==1,"An incomplete field streams nothing");
    check(stream.fieldsChanged()&&!stream.settle(std::array<double,3>{0,0,1})&&seen.size()==1,"An equal colour is not repeated");
}
}
int main(){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{run();if(SUCCEEDED(hr))CoUninitialize();std::cout<<"PASS "<<checks<<" Personal Profile picker checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
