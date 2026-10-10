#include "native/hypergryph_account_sanity_gauge_scene.hpp"
#ifdef _WIN32
#include "core/motion.hpp"
#include "native/layer_group.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
namespace h=modules::hypergryph;namespace g=modules::hypergryph::gauge_geometry;
using Json=ehud::data::Json;using Matrix=core::Matrix4;using Rect=core::Rect;using Point=core::Point;
void need(bool value,const char* why) {if(!value) throw std::invalid_argument(why);}
constexpr core::CubicTiming easeOut{0,0,.58,1};
constexpr std::array<Vertex,4> quad{{{{0,0,0},{0,0},{1,1,1,1}},{{1,0,0},{1,0},{1,1,1,1}},{{1,1,0},{1,1},{1,1,1,1}},{{0,1,0},{0,1},{1,1,1,1}}}};
constexpr std::array<std::uint32_t,6> quadIndices{0,1,2,0,2,3};
const std::string quadID="account.gauge.quad";
const std::string walletGroupID="account.gauge.wallet";
const std::array<std::string,3> walletIDs{"account.gauge.artwork","account.gauge.number","account.gauge.highlight"};
const std::array<std::string,4> tipIDs{"account.gauge.tip.0","account.gauge.tip.1","account.gauge.tip.2","account.gauge.tip.3"};
std::vector<std::uint8_t> straight(const h::GaugeImage& image) {
    std::vector<std::uint8_t> out(image.rgba.size());
    for(std::size_t i=0;i<image.rgba.size();i+=4) {
        const unsigned a=image.rgba[i+3];out[i+3]=static_cast<std::uint8_t>(a);
        for(int c=0;c<3;++c) out[i+c]=a?static_cast<std::uint8_t>(std::min(255u,(image.rgba[i+c]*255u+a/2)/a)):0;
    }
    return out;
}
Json command(const char* op,std::initializer_list<Point> points) {Json::Array v;for(const auto& p:points) v.emplace_back(Json::Array{p.x,p.y});return Json::Object{{"op",op},{"points",std::move(v)}};}
// CGPath.copy(strokingWithWidth: 1.7) of the gauge refresh arc (center 11,11,
// radius 6.2) plus its arrow head, exported from the unchanged HUDAccountGauge.
Json arrowPath() {
    using P=Point;
    return Json::Array{command("move",{P{13.675,15.633235910246746}}),
        command("cubic",{P{11.116134464567663,17.110597615948667},P{7.84412579545517,16.23386553543234},P{6.366764089753253,13.675}}),
        command("cubic",{P{4.88940238405134,11.116134464567667},P{5.766134464567665,7.844125795455168},P{8.325,6.366764089753253}}),
        command("cubic",{P{10.727355873536649,4.97976327947759},P{13.791361472998249,5.659036967186308},P{15.382463436946106,7.931366065521901}}),
        command("cubic",{P{15.651724327806235,8.315910470097402},P{16.181738240914637,8.409366223129284},P{16.566282645490137,8.140105332269155}}),
        command("cubic",{P{16.950827050065637,7.870844441409025},P{17.04428280309752,7.340830528300622},P{16.775021912237392,6.956286123725123}}),
        command("cubic",{P{14.678336146661245,3.961908526852986},P{10.640721291295957,3.066790863610656},P{7.474999999999999,4.894520903319707}}),
        command("cubic",{P{4.103037004658127,6.841324646377796},P{2.947717160261621,11.153037004658131},P{4.894520903319707,14.525000000000002}}),
        command("cubic",{P{6.841324646377798,17.89696299534188},P{11.153037004658126,19.052282839738385},P{14.525000000000002,17.10547909668029}}),
        command("cubic",{P{14.931548729954772,16.870758078002204},P{15.07084261189486,16.35090623341829},P{14.836121593216774,15.944357503463518}}),
        command("cubic",{P{14.601400574538687,15.537808773508749},P{14.08154872995477,15.39851489156866},P{13.675,15.633235910246746}}),
        command("close",{}),command("move",{P{18.05,10.1}}),command("line",{P{13.25,10.1}}),command("line",{P{18.05,5.3}}),command("close",{})};
}
// CGPath(roundedRect: 23x25, cornerWidth: 3) as CoreGraphics emits it.
Json roundedHighlight() {
    using P=Point;constexpr double k=1.3431457506000002,m=21.656854249400002,n=23.656854249400002;
    return Json::Array{command("move",{P{23,12.5}}),command("line",{P{23,22}}),command("cubic",{P{23,n},P{m,25},P{20,25}}),command("line",{P{3,25}}),
        command("cubic",{P{k,25},P{0,n},P{0,22}}),command("line",{P{0,3}}),command("cubic",{P{0,k},P{k,0},P{3,0}}),command("line",{P{20,0}}),
        command("cubic",{P{m,0},P{23,k},P{23,3}}),command("close",{})};
}
Json leaf(std::string id,Json node,Rect frame) {
    node["id"]=std::move(id);node["position"]=Json::Array{0,0};node["anchorPoint"]=Json::Array{0,0};node["opacity"]=1;node["children"]=Json::Array{};
    (void)frame;return node;
}
}
struct NativeSanityGaugeScene::Impl {
    struct Track {double from{},target{},start{},duration{};bool active{};};
    std::shared_ptr<const h::GaugeAssets> assets;LayerRasterizer* raster;LayerRasterOptions options;
    // wallet: empty carrier for the encoded wallet group output; popover: the
    // local recovery card, grouped (with the numeral draws inserted last).
    LayerScene wallet,popover;std::unique_ptr<NativeLayerGroup> popoverGroup;
    std::array<DrawObject,3> walletDraws;std::array<DrawObject,1> walletOutput;std::array<DrawObject,4> tipDraws;std::array<LayerCompositionEntry,2> composed;
    bool walletRegistered{},walletLocalDirty{},popoverUploaded{},popoverLocalDirty{};std::uint64_t popoverStructure{},popoverResources{};
    Matrix gaugeWorld;double visibleOpacity{},popoverOpacity{},popoverLiftValue{};bool posed{};
    std::array<h::GaugeImage,3> walletImages;std::array<std::uint64_t,3> walletRevisions{};std::array<bool,3> walletDirty{true,true,true};
    std::array<h::GaugeImage,4> tipImages;std::array<std::uint64_t,4> tipRevisions{};std::array<bool,4> tipDirty{true,true,true,true};std::array<Rect,4> tipFrames{};
    std::vector<LayerPlacement> popoverPlacements;std::vector<Rect> popoverFrames;std::vector<std::string> popoverIDs;
    double scale{};std::string value;bool hidden{true},open{},refreshEnabled{},popoverDirty{true},popoverForce{true},meshDirty{true},uploaded{};
    std::array<std::string,4> tipText;std::array<bool,4> tipNumerals{};core::Language language{core::Language::english};
    Track hover,refreshHover,popoverAlpha,popoverLift;std::optional<double> lastTime;Renderer* owner{};
    NativeSanityGaugeStats stats;
    Impl(std::shared_ptr<const h::GaugeAssets> a,LayerRasterizer& r,LayerRasterOptions o):assets(std::move(a)),raster(&r),options(std::move(o)),wallet(r),popover(r) {
        need(assets!=nullptr,"Gauge needs its prepared original assets");
        wallet.load(Json::Object{{"bounds",Json::Array{0,0,168,42}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",Json::Array{}}},options);
        for(std::size_t i=0;i<3;++i) {walletDraws[i].sourceID=walletIDs[i];walletDraws[i].meshID=quadID;walletDraws[i].textureID=walletIDs[i];walletDraws[i].opacity=0;}
        for(std::size_t i=0;i<4;++i) {tipDraws[i].sourceID=tipIDs[i];tipDraws[i].meshID=quadID;tipDraws[i].textureID=tipIDs[i];tipDraws[i].opacity=0;}
        for(auto& image:walletImages) image={1,1,{0,0,0,0}};for(auto& image:tipImages) image={1,1,{0,0,0,0}};
        walletOutput[0].masks.reserve(8);
        loadPopover();
        // Mac sublayers composite in encoded sRGB; group both stacks so hover
        // tints and the popover card blend the same way.
        popoverGroup=std::make_unique<NativeLayerGroup>(popover,"account.gauge.popover",options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB);
    }
    // Group roots only: tilt/fade/slide never redraw the retained targets.
    void applyPose() {
        if(!posed) return;
        if(walletRegistered) {auto& d=walletOutput[0];d.world=gaugeWorld;d.opacity=static_cast<float>(visibleOpacity);}
        if(popoverUploaded) popoverGroup->setPose(gaugeWorld*Matrix::translation(0,popoverLiftValue),static_cast<float>(popoverOpacity));
    }
    NativeGroupInsertion insertion() const {
        return {popover.draws().size(),tipDraws,{g::popover.x,g::popover.y,g::popover.width,g::popover.height}};
    }
    // Gauge-local placements (hover tint, tooltip numerals, popover refresh
    // feedback). Unchanged values do not dirty the retained group targets.
    void placeLocal(double t) {
        const auto place=[](DrawObject& d,Rect r,double alpha,bool& dirty){
            const auto world=Matrix::translation(r.x,r.y)*Matrix::scale(r.width,r.height);const auto a=static_cast<float>(alpha);
            if(d.world!=world||d.opacity!=a) {d.world=world;d.opacity=a;dirty=true;}
        };
        place(walletDraws[0],g::artwork,1,walletLocalDirty);
        place(walletDraws[1],g::number,value.empty()?0:1,walletLocalDirty);
        place(walletDraws[2],g::bar,sample(hover,t),walletLocalDirty);
        const Rect frames[4]{g::nextLabel,g::fullLabel,g::nextValue,g::fullValue};
        for(std::size_t n=0;n<4;++n) place(tipDraws[n],{g::popover.x+frames[n].x,g::popover.y+frames[n].y,frames[n].width,frames[n].height},tipNumerals[n]?1:0,popoverLocalDirty);
        bool changed=false;
        for(std::size_t n=0;n<popoverPlacements.size();++n) {
            auto& p=popoverPlacements[n];const auto& f=popoverFrames[n];const auto world=Matrix::translation(f.x,f.y);
            const auto& id=popoverIDs[n];double a=1;
            if(id=="gauge/popover/arrow") a=refreshEnabled?1:.38;
            else if(id=="gauge/popover/tint") a=sample(refreshHover,t)*.62;
            else if(id=="gauge/popover/rim") a=sample(refreshHover,t);
            const auto alpha=static_cast<float>(a);
            if(p.world!=world||p.opacity!=alpha||!p.masks.empty()) {p.world=world;p.opacity=alpha;p.masks={};changed=true;}
        }
        if(changed||popoverForce) {popover.setPlacements(popoverPlacements);popoverLocalDirty=true;popoverForce=false;}
    }
    void time(double t) {need(std::isfinite(t)&&(!lastTime||t>=*lastTime),"Gauge scene requires a finite monotonic owner clock");lastTime=t;}
    static double sample(const Track& track,double t) {
        if(!track.active||track.duration<=0) return track.target;
        return track.from+(track.target-track.from)*easeOut.value(std::clamp((t-track.start)/track.duration,0.0,1.0));
    }
    static void animate(Track& track,double target,double duration,double t,bool reduced) {
        if(track.target==target&&!reduced) return;
        const double current=sample(track,t);track={current,target,t,reduced?0:duration,!reduced&&current!=target};
    }
    void loadPopover() {
        Json::Array leaves;popoverFrames.clear();popoverIDs.clear();
        const auto add=[&](std::string id,Json node,Rect frame){popoverIDs.push_back(id);popoverFrames.push_back(frame);leaves.push_back(leaf(std::move(id),std::move(node),frame));};
        const Rect box=g::popover;
        add("gauge/popover/background",Json::Object{{"kind","layer"},{"bounds",Json::Array{0,0,224,65}},{"cornerRadius",g::popoverCornerRadius},
            {"backgroundColor",Json::Object{{"sRGB",Json::Array{0.06499998271465302,0.06499998271465302,0.06499998271465302,0.98}}}}},box);
        for(std::size_t i=0;i<4;++i) if(!tipNumerals[i]&&!tipText[i].empty()) {
            const Rect frames[4]{g::nextLabel,g::fullLabel,g::nextValue,g::fullValue};const auto f=frames[i];
            // Native locale fallback (HUDAccountGauge.localizedText): system medium 14.4, white .92.
            add("gauge/popover/text/"+std::to_string(i),Json::Object{{"kind","text"},{"bounds",Json::Array{0,0,f.width,f.height-3}},{"text",Json::Object{{"string",tipText[i]},{"fontSize",14.4},
                {"font",Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",".AppleSystemUIFontMedium"},{"pointSize",14.4}}},
                {"foregroundColor",Json::Object{{"sRGB",Json::Array{.92,.92,.92,1}}}},{"alignment","left"},{"wrapped",false},{"truncation","end"},{"runs",Json::Array{}}}}},
                {box.x+f.x,box.y+f.y+3,f.width,f.height-3});
        }
        add("gauge/popover/arrow",Json::Object{{"kind","shape"},{"bounds",Json::Array{0,0,22,22}},{"shape",Json::Object{{"path",arrowPath()},
            {"fillColor",Json::Object{{"sRGB",Json::Array{0.9000000357627869,0.9000000357627869,0.9000000357627869,1}}}},{"strokeColor",Json{}},{"lineWidth",1},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}}}},
            {box.x+g::refresh.x+.5,box.y+g::refresh.y+1.5,22,22});
        add("gauge/popover/tint",Json::Object{{"kind","shape"},{"bounds",Json::Array{0,0,23,25}},{"shape",Json::Object{{"path",roundedHighlight()},
            {"fillColor",Json::Object{{"sRGB",Json::Array{.75,.75,.75,.3}}}},{"strokeColor",Json{}},{"lineWidth",1},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}}}},
            {box.x+g::refresh.x,box.y+g::refresh.y,23,25});
        add("gauge/popover/rim",Json::Object{{"kind","shape"},{"bounds",Json::Array{0,0,23,25}},{"shape",Json::Object{{"path",roundedHighlight()},
            {"fillColor",Json{}},{"strokeColor",Json::Object{{"sRGB",Json::Array{.75,.75,.75,1}}}},{"lineWidth",.9},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}}}},
            {box.x+g::refresh.x,box.y+g::refresh.y,23,25});
        popover.load(Json::Object{{"bounds",Json::Array{0,0,224,65}},{"position",Json::Array{0,0}},{"anchorPoint",Json::Array{0,0}},{"children",std::move(leaves)}},options);
        need(popover.report().unsupported.empty(),"Gauge popover contains unsupported raster state");
        popoverPlacements.assign(popoverIDs.size(),{});for(std::size_t i=0;i<popoverPlacements.size();++i) popoverPlacements[i].surface=i;
        popoverDirty=false;popoverForce=true;++stats.popoverLoads;
    }
    std::size_t index(std::string_view id) const {for(std::size_t i=0;i<popoverIDs.size();++i) if(popoverIDs[i]==id) return i;return SIZE_MAX;}
};
NativeSanityGaugeScene::NativeSanityGaugeScene(std::shared_ptr<const h::GaugeAssets> assets,LayerRasterizer& r,LayerRasterOptions options)
    :impl_(std::make_unique<Impl>(std::move(assets),r,std::move(options))) {}
NativeSanityGaugeScene::~NativeSanityGaugeScene()=default;
bool NativeSanityGaugeScene::sync(const h::SanityGaugeModel& model,core::Language language,double t,bool reduced) {
    auto& i=*impl_;i.time(t);bool changed=false;
    if(model.scale()!=i.scale&&model.scale()>0) {
        i.scale=model.scale();
        i.walletImages[0]=h::composeGaugeArtwork(*i.assets,i.scale);i.walletImages[2]=h::composeHighlight(*i.assets,i.scale);
        i.walletDirty[0]=i.walletDirty[2]=true;++i.stats.artworkRasters;i.value.clear();i.value.push_back('\x01');changed=true;
        std::fill(i.tipText.begin(),i.tipText.end(),std::string("\x01"));
    }
    if(model.value()!=i.value) {
        i.value=model.value();
        if(auto image=i.assets->numerals.render(i.value,g::number.width,g::number.height,i.scale)) i.walletImages[1]=std::move(*image);else i.walletImages[1]={1,1,{0,0,0,0}};
        i.walletDirty[1]=true;++i.stats.numberRasters;changed=true;
    }
    if(model.hidden()!=i.hidden) {i.hidden=model.hidden();changed=true;}
    if(model.popoverOpen()!=i.open) {
        i.open=model.popoverOpen();
        Impl::animate(i.popoverAlpha,i.open?1:0,g::popoverDuration,t,reduced);
        // Opening slides in from -5; closing drifts to -3 while fading.
        if(i.open) {i.popoverLift={-5,0,t,reduced?0:g::popoverDuration,!reduced};}
        else {i.popoverLift={0,-3,t,reduced?0:g::popoverDuration,!reduced};}
        changed=true;
    }
    if(model.popoverOpen()) {
        const auto& tip=model.tooltip();const Rect frames[4]{g::nextLabel,g::fullLabel,g::nextValue,g::fullValue};bool structure=false;
        for(std::size_t n=0;n<4;++n) if(tip.text[n]!=i.tipText[n]) {
            i.tipText[n]=tip.text[n];const bool numerals=i.assets->numerals.covers(tip.text[n]);
            if(numerals!=i.tipNumerals[n]) structure=true;i.tipNumerals[n]=numerals;
            if(numerals) i.tipImages[n]=*i.assets->numerals.render(tip.text[n],frames[n].width,frames[n].height,i.scale,14.4,false);
            else {i.tipImages[n]={1,1,{0,0,0,0}};structure=true;}
            i.tipDirty[n]=true;++i.stats.tooltipRasters;changed=true;
        }
        if(tip.refreshEnabled!=i.refreshEnabled) {i.refreshEnabled=tip.refreshEnabled;changed=true;}
        if(language!=i.language) {i.language=language;structure=true;}
        if(structure) i.popoverDirty=true;
    }
    if(i.popoverDirty) {i.loadPopover();changed=true;}
    i.placeLocal(t);
    return changed;
}
bool NativeSanityGaugeScene::setHover(std::optional<std::string_view> control,double t,bool reduced) {
    auto& i=*impl_;i.time(t);
    const auto before=std::pair{i.hover.target,i.refreshHover.target};
    Impl::animate(i.hover,control==std::optional<std::string_view>("toggle")?.62:0,g::highlightDuration,t,reduced);
    Impl::animate(i.refreshHover,control==std::optional<std::string_view>("refresh")&&i.refreshEnabled?1:0,g::highlightDuration,t,reduced);
    return before!=std::pair{i.hover.target,i.refreshHover.target};
}
bool NativeSanityGaugeScene::updatePose(const Matrix& world,float opacity,double t) {
    auto& i=*impl_;i.time(t);need(world.finite()&&std::isfinite(opacity)&&opacity>=0&&opacity<=1,"Invalid gauge placement");
    i.placeLocal(t);
    i.gaugeWorld=world;i.visibleOpacity=i.hidden?0:opacity;
    i.popoverOpacity=Impl::sample(i.popoverAlpha,t)*i.visibleOpacity;i.popoverLiftValue=Impl::sample(i.popoverLift,t);i.posed=true;
    i.applyPose();
    for(auto* track:{&i.hover,&i.refreshHover,&i.popoverAlpha,&i.popoverLift}) if(t>=track->start+track->duration) track->active=false;
    ++i.stats.poseUpdates;return true;
}
bool NativeSanityGaugeScene::requiresFrames(double t) const {
    const auto& i=*impl_;
    for(const auto* track:{&i.hover,&i.refreshHover,&i.popoverAlpha,&i.popoverLift}) if(track->active&&t<track->start+track->duration) return true;
    return false;
}
bool NativeSanityGaugeScene::visible() const noexcept {return !impl_->hidden;}
void NativeSanityGaugeScene::upload(Renderer& r) {
    auto& i=*impl_;need(!i.owner||i.owner==&r,"Gauge resources belong to their original renderer");
    need(i.posed,"Place the gauge before uploading it");i.owner=&r;
    if(i.meshDirty) {r.setMesh(quadID,1,{quad,quadIndices});i.meshDirty=false;}
    bool walletTextures=false,tipTextures=false;
    // Premultiplied encoded artwork, kept encoded for the encoded-sRGB groups.
    for(std::size_t n=0;n<3;++n) if(i.walletDirty[n]) {const auto pixels=straight(i.walletImages[n]);r.setTexture(walletIDs[n],++i.walletRevisions[n],{i.walletImages[n].width,i.walletImages[n].height,pixels,TextureColorSpace::encodedSRGB});i.walletDirty[n]=false;walletTextures=true;}
    for(std::size_t n=0;n<4;++n) if(i.tipDirty[n]) {const auto pixels=straight(i.tipImages[n]);r.setTexture(tipIDs[n],++i.tipRevisions[n],{i.tipImages[n].width,i.tipImages[n].height,pixels,TextureColorSpace::encodedSRGB});i.tipDirty[n]=false;tipTextures=true;}
    i.wallet.uploadResources(r);
    if(!i.walletRegistered) {
        r.configureNativeGroup(walletGroupID,{g::artwork,i.options.pixelsPerPoint,NativeGroupColorSpace::encodedSRGB},i.walletDraws);i.walletRegistered=true;
        if(i.walletOutput[0].sourceID.empty()) {auto next=r.nativeGroupOutput(walletGroupID);next.masks.reserve(8);i.walletOutput[0]=std::move(next);}
        i.walletLocalDirty=false;++i.stats.groupUploads;
    } else if(walletTextures||i.walletLocalDirty) {r.setNativeGroupDraws(walletGroupID,i.walletDraws);i.walletLocalDirty=false;++i.stats.groupRedraws;}
    const bool content=!i.popoverUploaded||i.popoverStructure!=i.popover.contentRevision()||i.popoverResources!=i.popover.resourceRevision();
    if(content) {
        const auto ink=i.popoverGroup->localCoverageBounds();const Rect& b=g::popover;
        const double x=std::min(ink.x,b.x),y=std::min(ink.y,b.y);
        i.popoverGroup->uploadResources(r,Rect{x,y,std::max(ink.x+ink.width,b.x+b.width)-x,std::max(ink.y+ink.height,b.y+b.height)-y},i.insertion());
        i.popoverUploaded=true;i.popoverStructure=i.popover.contentRevision();i.popoverResources=i.popover.resourceRevision();i.popoverLocalDirty=false;++i.stats.groupUploads;
    } else if(tipTextures||i.popoverLocalDirty) {i.popoverGroup->updateLocal(r,i.insertion());i.popoverLocalDirty=false;++i.stats.groupRedraws;}
    i.uploaded=true;i.applyPose();
}
std::span<const LayerCompositionEntry> NativeSanityGaugeScene::entries() {
    auto& i=*impl_;if(i.hidden||!i.walletRegistered||!i.popoverUploaded) return {};
    // Wallet stack first, the recovery popover (z 10) above it.
    i.composed[0]={&i.wallet,i.walletOutput};i.composed[1]=i.popoverGroup->entry();
    return i.composed;
}
void NativeSanityGaugeScene::collected(Renderer& r) {impl_->wallet.collectRetiredResources(r);}
bool NativeSanityGaugeScene::release(Renderer& r) {
    auto& i=*impl_;if(!i.uploaded) return true;
    if(i.walletRegistered) {if(r.stats().initialized&&!r.removeNativeGroup(walletGroupID)) return false;i.walletRegistered=false;}
    if(!i.popoverGroup->releaseResources(r)) return false;
    i.popoverUploaded=false;
    bool ok=i.wallet.releaseResources(r);
    for(const auto& id:walletIDs) ok=r.removeTexture(id)&&ok;
    for(const auto& id:tipIDs) ok=r.removeTexture(id)&&ok;
    ok=r.removeMesh(quadID)&&ok;
    if(ok) {i.uploaded=false;i.meshDirty=true;i.walletDirty={true,true,true};i.tipDirty={true,true,true,true};i.owner=nullptr;}
    return ok;
}
NativeSanityGaugeStats NativeSanityGaugeScene::stats() const noexcept {return impl_->stats;}
}
#endif
