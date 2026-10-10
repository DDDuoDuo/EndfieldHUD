// Bottom-left ID card captions written into the native label LayerScene,
// checked against the caption leaves the Mac shell exporter rendered for its
// default profile (windows/tools/id_card_captions_reference.py).
#include "native/id_card_captions.hpp"
#include "core/data/file_io.hpp"
#include <cmath>
#include <iostream>
#ifdef _WIN32
#include "native/layer_scene.hpp"
#include "native/layer_text_layout.hpp"
#include <windows.h>
#include <objbase.h>
namespace {
using namespace endfield;namespace m=modules;namespace gpu=native;using J=ehud::data::Json;
unsigned checks{};
void check(bool v,const std::string&why){++checks;if(!v)throw std::runtime_error(why);}
J load(const std::filesystem::path&file){const auto bytes=ehud::data::detail::readFile(file,8*1024*1024);check(bytes.has_value(),"Read "+file.string());return J::parse(*bytes,8*1024*1024);}
std::u16string painted(const gpu::LayerScene&labels,const std::string&id){const auto layout=labels.paintedTextLayout(id);return layout?std::u16string(layout->text()):std::u16string{};}
void run(const std::filesystem::path&fixture,const std::filesystem::path&resources){
    const auto exported=load(fixture);const auto&layers=exported["nativeLayers"];
    gpu::LayerRasterizer raster;gpu::LayerRasterOptions options;options.pixelsPerPoint=2;
    gpu::LayerScene labels(raster);labels.load(layers,options);
    check(labels.report().unsupported.empty(),"Exported profile captions rasterize without unsupported features");
    const auto leaves=gpu::idCardCaptionLeaves(layers);
    for(std::size_t i=0;i<5;++i){
        const auto&container=layers["children"].array()[i];
        check(container["name"].string()=="desktop.profile."+std::string(m::idCardCaptionBindings[i])&&leaves.surfaceIDs[i]==container["children"].array()[0]["id"].string(),"Leaf order follows the source caption bindings");
        check(exported["nativeProfileBindings"].contains(container["name"].string()),"Every caption has its exported source node binding");
    }
    bool rejected{};try{gpu::idCardCaptionLeaves(J::Object{{"children",J::Array{}}});}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"An export without the profile captions is rejected");
    // The staged reduced card is the pinned source, and its caption nodes are
    // the ones the exporter bound.
    const auto source=m::loadIdCardSource(resources);
    for(std::size_t i=0;i<5;++i)check(exported["nativeProfileBindings"]["desktop.profile."+std::string(m::idCardCaptionBindings[i])].string()==source.captions[i].nodeID,"Staged card and export bind the same caption node");
    const auto measure=gpu::nativeIdCardMeasure(raster,options);
    check(measure("Endministrator",22)>0&&measure("Endministrator",22)<source.captions[0].width&&measure("WW",22)>measure("W",22),"DirectWrite measures the medium system face");
    m::IdCardBinding binding(source,measure);
    // 1. Mac oracle: the exporter's default profile reproduces every caption.
    ehud::data::Profile profile;profile.uid="1000000000";profile.awakeningDate=0;
    m::IdCardBinding::Input input;input.profile=&profile;input.hudAccent={0xFA/255.,0xD4/255.,0x1F/255.};
    check(binding.update(input),"Initial card content");
    for(std::size_t i=0;i<5;++i){
        const auto expected=leaves.leaves[i]["text"];const auto actual=binding.captionLayer(leaves.leaves[i],i)["text"];
        const std::string name(m::idCardCaptionBindings[i]);
        check(actual["string"].string()==expected["string"].string(),"Exported caption string "+name);
        check(std::abs(actual["fontSize"].number()-expected["fontSize"].number())<1e-9,"Exported fitted size "+name);
        check(actual["alignment"].string()==expected["alignment"].string(),"Exported alignment "+name);
        for(std::size_t c=0;c<4;++c)check(std::abs(actual["foregroundColor"]["sRGB"].array()[c].number()-expected["foregroundColor"]["sRGB"].array()[c].number())<1e-9,"Exported caption colour "+name);
        for(const char*metric:{"ascender","descender","pointSize"})check(std::abs(actual["font"][metric].number()-expected["font"][metric].number())<1e-9,"Exported medium font metrics "+name);
    }
    // 2. Native writer: first content event repaints all five surfaces.
    gpu::NativeIdCardCaptions captions(labels,leaves,options);
    const auto resources0=labels.resourceRevision();
    check(captions.update(binding)&&captions.stats().surfaceUpdates==5&&labels.resourceRevision()!=resources0,"Every caption surface is written once");
    check(painted(labels,leaves.surfaceIDs[0])==u"Endministrator"&&painted(labels,leaves.surfaceIDs[1])==u"UID: 1000000000"&&painted(labels,leaves.surfaceIDs[2])==u"60"&&
          painted(labels,leaves.surfaceIDs[3])==u"Authority"&&painted(labels,leaves.surfaceIDs[4])==u"MAX","Painted captions match the source");
    // 3. Identical content and non-card fields do no raster work.
    const auto rasters=raster.stats().rasterizations;const auto writes=captions.stats().surfaceUpdates;
    check(!captions.update(binding)&&raster.stats().rasterizations==rasters,"Unchanged binding revision repaints nothing");
    profile.accumulatedWorkSeconds=7200;profile.introduction="Work ticks";profile.tag="4242";
    check(!binding.update(input)&&!captions.update(binding)&&raster.stats().rasterizations==rasters,"Work hours, biography and the # tag never touch the card");
    // 4. Official sync shows the game UID at once, never a # number.
    profile.gamePlayerID="1234567890123456789";check(binding.update(input)&&captions.update(binding),"Synced UID repaints");
    check(captions.stats().surfaceUpdates==writes+1&&painted(labels,leaves.surfaceIDs[1])==u"UID: 1234567890123456789","Only the UID caption repaints for a synced ID");
    for(std::size_t i=0;i<5;++i)check(painted(labels,leaves.surfaceIDs[i]).find(u'#')==std::u16string::npos,"No caption shows the # tag");
    profile.playerIDOverride="Manual-7";binding.update(input);captions.update(binding);check(painted(labels,leaves.surfaceIDs[1])==u"UID: Manual-7","Override has display priority");
    // 5. Levels: MAX only at 60; the empty caption keeps its retained surface.
    const auto beforeLevel=captions.stats().surfaceUpdates;profile.permissionLevel=42;binding.update(input);check(captions.update(binding),"Level change repaints");
    check(painted(labels,leaves.surfaceIDs[2])==u"42"&&painted(labels,leaves.surfaceIDs[4]).empty()&&captions.stats().surfaceUpdates==beforeLevel+2,"Level and MAX captions only");
    check(labels.surfaceIndex(leaves.surfaceIDs[4]).has_value(),"An empty MAX caption keeps its surface for later");
    // 6. Language event: Authority/MAX localize; the empty MAX stays untouched.
    const auto beforeLanguage=captions.stats().surfaceUpdates;input.language=core::Language::simplifiedChinese;binding.update(input);captions.update(binding);
    check(painted(labels,leaves.surfaceIDs[3])==u"权限等级"&&captions.stats().surfaceUpdates==beforeLanguage+1,"Language change repaints only Authority while below MAX");
    profile.permissionLevel=60;binding.update(input);captions.update(binding);check(painted(labels,leaves.surfaceIDs[4])==u"满级","MAX localizes at level 60");
    // 7. Long names shrink in 0.5 pt steps to fit, never below 10 pt.
    profile.name="WWWWWWWWWWWWWWWWWWWW";binding.update(input);captions.update(binding);
    const auto fitted=binding.captions()[0].fontSize;
    check(fitted<22&&fitted>=10&&std::fmod(22-fitted,.5)==0,"Long name steps down in 0.5 pt");
    check(fitted==10||measure(profile.name,fitted)<=source.captions[0].width,"Fitted name fits the authored width");
    check(fitted==22||measure(profile.name,fitted+.5)>source.captions[0].width,"The largest fitting size is chosen");
    const auto shrunk=binding.captionLayer(leaves.leaves[0],0)["text"]["font"];
    check(std::abs(shrunk["ascender"].number()-fitted*.966796875)<1e-9&&std::abs(shrunk["descender"].number()+fitted*.2109375)<1e-9,"Line metrics follow the fitted size");
    check(painted(labels,leaves.surfaceIDs[0])==u"WWWWWWWWWWWWWWWWWWWW","Painted fitted name");
    // 8. Card theme tints MAX only.
    const auto beforeTheme=captions.stats().surfaceUpdates;profile.themeColorHex="6EDFE8";binding.update(input);captions.update(binding);
    check(captions.stats().surfaceUpdates==beforeTheme+1&&binding.captions()[4].color[0]==0x6E/255.,"Theme colour repaints MAX in the card accent");
    check(labels.report().unsupported.empty(),"Runtime caption content stays fully supported");
}
}
int wmain(int argc,wchar_t**argv){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{check(SUCCEEDED(hr)&&argc==3,"Pass id_card-native-captions.json and windows/resources/profile");run(argv[1],argv[2]);CoUninitialize();
        std::cout<<"PASS "<<checks<<" ID card caption checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
