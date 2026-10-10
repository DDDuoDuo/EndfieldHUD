// Bottom-left ID card presenter: the Personal Profile area's published feed
// drives the captions in a native label LayerScene built from the exported
// caption leaves (windows/tools/id_card_captions_reference.py), the
// desktop.profile.* texture bindings and the card's desktop frame settings
// (HUDSourceWatchView.setDesktopProfile). Synthetic profile and pixels only.
#include "native/id_card_presenter.hpp"
#include "core/data/file_io.hpp"
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
template<class F>bool rejects(F f){try{f();}catch(const std::exception&){return true;}return false;}
J load(const std::filesystem::path&file){const auto bytes=ehud::data::detail::readFile(file,8*1024*1024);check(bytes.has_value(),"Read "+file.string());return J::parse(*bytes,8*1024*1024);}
struct Pixels {unsigned width{},height{};std::vector<std::uint8_t>rgba;std::uint64_t revision{};
    gpu::IdCardTextureView view()const{return {width,height,rgba,revision};}};
Pixels pixels(unsigned w,unsigned h,std::uint8_t fill,std::uint64_t revision){return {w,h,std::vector<std::uint8_t>(std::size_t(w)*h*4,fill),revision};}
void run(const std::filesystem::path&fixture,const std::filesystem::path&resources){
    const auto exported=load(fixture);const auto&layers=exported["nativeLayers"];
    gpu::LayerRasterizer raster;gpu::LayerRasterOptions options;options.pixelsPerPoint=2;
    gpu::LayerScene labels(raster);labels.load(layers,options);
    const auto leaves=gpu::idCardCaptionLeaves(layers);
    const auto painted=[&](std::size_t i){const auto layout=labels.paintedTextLayout(leaves.surfaceIDs[i]);return layout?std::u16string(layout->text()):std::u16string{};};
    const auto source=m::loadIdCardSource(resources);
    m::IdCardBinding binding(source,gpu::nativeIdCardMeasure(raster,options));
    check(!source.glowNodes.empty(),"The source card has glow nodes");
    // The exported baseline: the source binds desktop.profile.hover to the
    // highlight plate and the packet silhouette / default card elsewhere.
    core::source::SourceDesktopFrameSettings baseline;
    const auto highlight=source.glowNodes.front();
    baseline.images[source.playerHead]={"desktop.profile.avatar",{272,272},core::source::Vec2{136,136}};
    baseline.images[source.background]={"desktop.profile.background",{530,204},{}};
    baseline.images[highlight]={"desktop.profile.hover",{530,204},{}};
    baseline.properties[source.levelSlider]["m_FillAmount"]=1;
    std::vector<std::string>calls;
    gpu::IdCardTextureTarget target{
        [&](std::string_view id,unsigned w,unsigned h,std::span<const std::uint8_t>rgba,std::uint64_t revision){
            check(rgba.size()==std::size_t(w)*h*4,"Tight RGBA reaches the target");calls.push_back("replace "+std::string(id)+" "+std::to_string(w)+"x"+std::to_string(h)+" r"+std::to_string(revision));return true;},
        [&](std::string_view id){calls.push_back("restore "+std::string(id));return true;}};
    check(rejects([&]{gpu::IdCardPresenter bad(labels,leaves,options,gpu::IdCardTextureTarget{},baseline);}),"A texture target is required");
    auto twoPlates=baseline;twoPlates.images["second-plate"]={"desktop.profile.hover",{530,204},{}};
    check(rejects([&]{gpu::IdCardPresenter bad(labels,leaves,options,target,twoPlates);}),"One hover plate");
    gpu::IdCardPresenter presenter(labels,leaves,options,target,baseline);
    check(presenter.highlightNode()==highlight&&!presenter.pending()&&!presenter.apply(),"Unconnected presenter finds the plate and does nothing");
    unsigned frames{};gpu::IdCardFeed feed;presenter.connect(feed,[&]{++frames;});
    check(feed.connected(),"The feed knows its presenter");
    // 1. Before the profile loads: packet defaults stay, nothing is written.
    check(presenter.pending(),"A new connection applies once");
    check(!presenter.apply()&&calls.empty()&&painted(1)==u"UID: 1000000000"&&presenter.settings()==baseline,"No card: packet captions, textures and settings");
    // 2. Loaded profile with a photo avatar, themed background and hover.
    ehud::data::Profile profile;profile.uid="1000000000";profile.name="Synthetic";profile.gamePlayerID="1234567890123";profile.permissionLevel=42;profile.themeColorHex="0080FF";
    m::IdCardBinding::Input input;input.profile=&profile;input.avatarImage="avatar.image";check(binding.update(input),"Card content");
    auto avatar=pixels(368,368,200,binding.avatarRevision()),background=pixels(530,204,90,binding.backgroundRevision()),hover=pixels(530,204,40,binding.hoverRevision());
    check(rejects([&]{auto bad=avatar.view();bad.height=367;feed.publish(&binding,{bad,std::nullopt,std::nullopt});}),"Feed textures must be tight RGBA8");
    const auto rejectedRevision=feed.revision();
    feed.publish(&binding,{avatar.view(),background.view(),hover.view()});
    check(frames==1&&feed.revision()==rejectedRevision+1&&presenter.pending(),"A publish requests one frame");
    const auto settings=presenter.apply();
    check(painted(0)==u"Synthetic"&&painted(1)==u"UID: 1234567890123"&&painted(2)==u"42"&&painted(4).empty(),"Captions show the synced profile");
    for(std::size_t i=0;i<5;++i)check(painted(i).find(u'#')==std::u16string::npos,"No caption shows the # tag");
    check((calls==std::vector<std::string>{"replace desktop.profile.avatar 368x368 r"+std::to_string(avatar.revision),"replace desktop.profile.background 530x204 r"+std::to_string(background.revision),
        "replace desktop.profile.hover 530x204 r"+std::to_string(hover.revision)}),"Every generated texture is bound once");
    check(settings&&settings->images.at(source.playerHead).size==std::array<float,2>{368,368}&&settings->images.at(source.playerHead).displaySize==core::source::Vec2{136,136},"The avatar binding follows the crop size and keeps its display size");
    check(settings->properties.at(source.levelSlider).at("m_FillAmount")==42./60,"Level fill is level/60");
    check(settings->properties.at(highlight).at("m_Color.a")==1,"The hover plate's glow is lit");
    for(std::size_t n=1;n<source.glowNodes.size();++n)check(settings->properties.at(source.glowNodes[n]).at("m_Color.a")==0,"Other glow nodes stay dark");
    for(const auto&id:source.accentNodes){const auto&v=settings->properties.at(id);check(v.at("m_Color.r")==0&&v.at("m_Color.g")==128./255&&v.at("m_Color.b")==1,"Accent nodes take the card colour");}
    check(settings->sprites.at(source.background)==source.defaultBackgroundSprite,"The default card sprite stays selected");
    // 3. Identical feed content and unrelated republishes do no work.
    const auto rasters=raster.stats().rasterizations;
    check(!presenter.apply()&&presenter.stats().unchanged>=1,"Unchanged feed is a no-op");
    feed.publish(&binding,{avatar.view(),background.view(),hover.view()});
    check(!presenter.apply()&&calls.size()==3&&raster.stats().rasterizations==rasters,"A republish of the same revisions repaints and uploads nothing");
    // 4. A new accent regenerates the background and hover only.
    profile.themeColorHex="FF0000";check(binding.update(input),"Accent change");
    background.revision=binding.backgroundRevision();hover.revision=binding.hoverRevision();
    check(avatar.revision==binding.avatarRevision(),"The avatar crop does not depend on the accent");
    feed.publish(&binding,{avatar.view(),background.view(),hover.view()});const auto recoloured=presenter.apply();
    check(calls.size()==5&&calls[3].starts_with("replace desktop.profile.background")&&calls[4].starts_with("replace desktop.profile.hover"),"Only the themed textures are replaced");
    check(recoloured&&recoloured->properties.at(source.headFrame).at("m_Color.r")==1&&painted(4).empty(),"The new accent reaches the tinted nodes");
    // 5. The photo is removed: the packet silhouette and its size return.
    input.avatarImage.reset();binding.update(input);
    feed.publish(&binding,{std::nullopt,background.view(),hover.view()});const auto silhouette=presenter.apply();
    check(calls.size()==6&&calls[5]=="restore desktop.profile.avatar","The silhouette is restored, not regenerated");
    check(silhouette&&silhouette->images.at(source.playerHead)==baseline.images.at(source.playerHead),"The silhouette binding keeps the packet size");
    // 6. MAX at level 60 in the card colour; language events.
    profile.permissionLevel=60;input.language=core::Language::simplifiedChinese;binding.update(input);
    feed.publish(&binding,{std::nullopt,background.view(),hover.view()});(void)presenter.apply();
    check(painted(3)==u"权限等级"&&painted(4)==u"满级","Authority/MAX localize and MAX appears at 60");
    // 7. Withdrawal (area shutdown or unreadable record) returns the baseline.
    feed.withdraw();const auto withdrawn=presenter.apply();
    check(withdrawn&&*withdrawn==baseline&&calls.size()==8&&calls[6]=="restore desktop.profile.background"&&calls[7]=="restore desktop.profile.hover","Without a card the packet textures and settings return");
    presenter.disconnect();feed.publish(&binding,{});check(!feed.connected()&&!presenter.pending()&&!presenter.apply(),"A disconnected presenter ignores the feed");
    // 8. The producing area is destroyed while connected: the packet card returns.
    {
        unsigned changes{};
        {gpu::IdCardFeed scoped;presenter.connect(scoped,[&]{++changes;});scoped.publish(&binding,{avatar.view(),background.view(),hover.view()});
         check(presenter.apply().has_value()&&calls.size()==11,"A reconnected feed binds its textures again");}
        check(changes==2&&presenter.pending(),"Feed destruction schedules the presenter");
        const auto restoredSettings=presenter.apply();
        check(restoredSettings&&*restoredSettings==baseline&&calls.size()==14&&calls[11]=="restore desktop.profile.avatar","The packet textures and settings return without the feed");
        check(!presenter.pending()&&!presenter.apply(),"Nothing remains to apply");
    }
    // A second presenter takes the feed over; the first is disconnected.
    gpu::IdCardPresenter other(labels,leaves,options,target,baseline);gpu::IdCardFeed shared;presenter.connect(shared,{});other.connect(shared,{});
    shared.publish(&binding,{});check(!presenter.pending()&&other.pending(),"One presenter per feed");other.disconnect();
}
}
int wmain(int argc,wchar_t**argv){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{check(SUCCEEDED(hr)&&argc==3,"Pass the exported caption fixture and windows/resources/profile");run(argv[1],argv[2]);CoUninitialize();std::cout<<"PASS "<<checks<<" ID card presenter checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
