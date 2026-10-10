#include "native/volume_accessibility.hpp"
#include "native/volume_strings.hpp"
#include "core/data/json.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

// Source: bash windows/tools/audio_reference.sh exports HUDVolumeInteraction's
// projected NSButton/NSSlider accessibility for synthetic VolumeCanvas states.
namespace {
namespace n=endfield::native;namespace c=endfield::core;using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);}
Json fixture(const std::filesystem::path&path){std::ifstream f(path,std::ios::binary);check(bool(f),"Explicit Mac audio oracle fixture exists");std::stringstream s;s<<f.rdbuf();return Json::parse(s.str());}

// The Windows equivalent of audio_reference.swift base(): same devices, levels,
// two available direct applications, and no default-device switching.
n::VolumeSnapshot base(){
    n::VolumeSnapshot s;s.outputs={{"10","Speakers"},{"11","Headphones",true},{"12","Buds",false,true}};s.inputs={{"20","Microphone"}};
    s.outputID="10";s.inputID="20";s.volume=.55;s.canSetVolume=true;s.muted=false;s.canSetMute=true;s.balance=-.25;s.canSetBalance=true;
    s.canSetDefaultOutput=s.canSetDefaultInput=false;s.applicationActivitySupported=true;
    s.applications.push_back({"40","Player",1000,true});s.applications.push_back({"41","PID 1001",1001,true});return s;
}
n::VolumeSnapshot state(std::string_view name){
    auto s=base();
    if(name=="app-active"){s.applications[0].state=n::VolumeAppState::active;s.applications[0].gain=.5;}
    else if(name=="fixed"){s.canSetVolume=false;s.balance.reset();s.canSetBalance=false;}
    else if(name=="centered")s.balance=.004;
    else if(name=="right-unavailable"){s.balance=.6;s.volume.reset();s.canSetVolume=false;}
    return s;
}
void oracle(const Json&root){
    std::size_t states{};
    for(const auto&expected:root["accessibility"].array()){
        const auto name=expected["state"].string();
        n::VolumeController controller(state(name));controller.setActive(true);
        if(name=="headphones")check(controller.perform("audio:headphones",0),"Headphones page opens");
        const auto actual=n::volumeAccessibility(controller);
        check(actual.status==expected["status"].string(),"Accessibility status equals the source default status");
        const auto&buttons=expected["buttons"].array();check(actual.buttons.size()==buttons.size(),"Same projected buttons as the source");
        for(std::size_t i=0;i<buttons.size();++i)check(actual.buttons[i].label==buttons[i]["label"].string()&&actual.buttons[i].help==buttons[i]["help"].string()&&actual.buttons[i].enabled==buttons[i]["enabled"].boolean(),"Button label, help and enabled state equal the source");
        const auto&sliders=expected["sliders"].array();check(actual.sliders.size()==sliders.size(),"Same projected sliders as the source");
        for(std::size_t i=0;i<sliders.size();++i){
            const auto&e=sliders[i];const auto&a=actual.sliders[i];
            check(a.label==e["label"].string()&&a.enabled==e["enabled"].boolean()&&a.minimum==e["minimum"].number()&&a.maximum==e["maximum"].number(),"Slider label, range and enabled state equal the source");
            check(e["value"].isNull()?!a.value:(a.value&&std::abs(*a.value-e["value"].number())<1e-9),"Slider value equals the source");
            if(!e["valueText"].isNull())check(a.valueText==e["valueText"].string(),"Unavailable value text equals the source");
            const auto help=e["help"].string();
            const bool directApp=a.id.starts_with("app:")&&help.find("macOS may request System Audio Recording access")!=std::string::npos;
            if(directApp){
                // Platform difference: the source hint announces a macOS System
                // Audio Recording permission prompt. Windows session volume needs
                // no permission, so only the value part is announced.
                check(help.starts_with(a.valueText+" · ")&&a.help==a.valueText,"Direct app slider keeps the source value text without the macOS permission hint");
            }else check(a.help==help,"Slider help (value text and route state) equals the source");
        }
        ++states;
    }
    check(states==6,"Every exported source state was compared");
}
void steps(){
    n::VolumeAccessibleSlider s{"volume","Output volume","","",{},.5,0,1,true};
    check(n::volumeAccessibleStep(s,1)==.52&&n::volumeAccessibleStep(s,-1)==.48,"Increment/decrement adjust by 2% of the range");
    s.value=.995;check(n::volumeAccessibleStep(s,1)==1.,"Steps clamp to the range");
    n::VolumeAccessibleSlider b{"balance","","","",{},0,-1,1,true};check(std::abs(*n::volumeAccessibleStep(b,-1)+.04)<1e-12,"Balance step uses its signed range");
    s.enabled=false;check(!n::volumeAccessibleStep(s,1),"Disabled slider does not step");
    s.enabled=true;s.value.reset();check(!n::volumeAccessibleStep(s,1),"Unavailable value does not step");
    s.value=.5;check(!n::volumeAccessibleStep(s,2),"Only single increments are defined");
}
void localization(){
    const n::VolumeStrings english,chinese=n::VolumeStrings::simplifiedChinese();
    check(n::volumeStrings(c::Language::english)==english&&n::volumeStrings(c::Language::simplifiedChinese)==chinese,"English and Simplified Chinese equal the source pairs");
    const std::array fields{&n::VolumeStrings::title,&n::VolumeStrings::outputDevice,&n::VolumeStrings::inputDevice,&n::VolumeStrings::subtitle,
        &n::VolumeStrings::chooseConnected,&n::VolumeStrings::output,&n::VolumeStrings::input,&n::VolumeStrings::noDevice,&n::VolumeStrings::volume,
        &n::VolumeStrings::balance,&n::VolumeStrings::outputVolume,&n::VolumeStrings::balanceLabel,&n::VolumeStrings::appVolumeSuffix,&n::VolumeStrings::unavailable,
        &n::VolumeStrings::mute,&n::VolumeStrings::unmute,&n::VolumeStrings::headphonesBluetooth,&n::VolumeStrings::appVolume,&n::VolumeStrings::back,
        &n::VolumeStrings::chooseOutput,&n::VolumeStrings::chooseInput,&n::VolumeStrings::selected,&n::VolumeStrings::previousPage,&n::VolumeStrings::nextPage,
        &n::VolumeStrings::centered,&n::VolumeStrings::unsupportedBalance,&n::VolumeStrings::missingBalance,&n::VolumeStrings::deviceControls,
        &n::VolumeStrings::noHeadphones,&n::VolumeStrings::headphones,&n::VolumeStrings::outputSuffix,&n::VolumeStrings::noApps,&n::VolumeStrings::unsupportedApps,
        &n::VolumeStrings::noConnectedDevices,&n::VolumeStrings::starting,&n::VolumeStrings::stopping,&n::VolumeStrings::restore,&n::VolumeStrings::unsupportedRoute,
        &n::VolumeStrings::appRoutingStopped};
    for(const auto field:fields)check(c::translationEntry(english.*field,chinese.*field)!=nullptr,"Every Volume caption is a source catalog pair (no invented text)");
    for(const auto language:{c::Language::english,c::Language::simplifiedChinese,c::Language::traditionalChinese,c::Language::japanese,c::Language::korean}){
        const auto strings=n::volumeStrings(language);
        for(const auto field:fields){
            const auto*entry=c::translationEntry(english.*field,chinese.*field);
            const std::string_view expected=language==c::Language::english?entry->english:language==c::Language::simplifiedChinese?entry->simplifiedChinese:language==c::Language::traditionalChinese?entry->traditionalChinese:language==c::Language::japanese?entry->japanese:entry->korean;
            check(strings.*field==expected,"Each language resolves every caption from its own catalog column");
        }
        n::VolumeController controller(base(),{},strings);check(controller.strings()==strings,"Controller accepts every language");
        const auto text=n::volumeErrorText(language);
        const auto native=text({n::VolumeProviderFailureKind::native,-2147024809});
        check(native.find("Windows")!=std::string::npos&&native.find("macOS")==std::string::npos&&native.ends_with(" (-2147024809)."),"Native failure names Windows with the source code suffix");
        for(const auto kind:{n::VolumeProviderFailureKind::unavailable,n::VolumeProviderFailureKind::unsupported,n::VolumeProviderFailureKind::deviceChanged,n::VolumeProviderFailureKind::invalidValue})
            check(!text({kind,0}).empty(),"Every failure category has text");
        const auto a=n::volumeAccessibilityStrings(language);
        check(!a.status.empty()&&!a.notAdjustable.empty()&&!a.appActive.empty()&&!a.appFailed.empty()&&!a.retryCleanup.empty(),"Accessibility text exists in every language");
    }
    check(n::volumeErrorText(c::Language::simplifiedChinese)({n::VolumeProviderFailureKind::deviceChanged,0})=="音频设备已更改，请重试。","Source Chinese device-changed message");
    check(n::volumeAccessibilityStrings(c::Language::english)==n::VolumeAccessibilityStrings{},"English accessibility defaults are the source text");
    // Language switch on a live controller: one rebuild, then none for an equal language.
    n::VolumeController controller(base());controller.setActive(true);const auto before=controller.contentRevision();
    check(controller.setStrings(n::volumeStrings(c::Language::japanese))&&controller.contentRevision()==before+1,"Language change rebuilds captions once");
    check(!controller.setStrings(n::volumeStrings(c::Language::japanese))&&controller.contentRevision()==before+1,"Equal language does not reshape");
    check(controller.actions()[2].label==n::volumeStrings(c::Language::japanese).mute,"Open interface updates immediately");
}
}
int main(int argc,char**argv){
    try{check(argc==2,"Usage: volume_accessibility_tests <audio-endpoint-source.json>");oracle(fixture(argv[1]));steps();localization();
        std::cout<<"PASS "<<checks<<" Volume accessibility/localization checks against the Mac oracle\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
