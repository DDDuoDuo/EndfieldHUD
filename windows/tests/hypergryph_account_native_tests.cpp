// The header sanity gauge on the shared renderer (hidden
// owned window, WARP offscreen target). A scripted in-memory service, memory
// vault and immediate work queue stand in for the network/DPAPI/worker; no real
// account, token, file outside the test, UI or network is touched.
#ifdef _WIN32
#include "tools/account_sanity_gauge_preview.hpp"
#include "native/module_scene.hpp"
#include "core/module_presentation.hpp"
#include <objbase.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
namespace {std::atomic<bool> counting{};std::atomic<std::size_t> allocations{};}
void* operator new(std::size_t n) {if(counting) ++allocations;if(auto* p=std::malloc(n?n:1)) return p;throw std::bad_alloc();}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
#endif
namespace {
namespace gpu=endfield::native;namespace core=endfield::core;namespace tools=endfield::tools;namespace app=endfield::app;namespace h=endfield::modules::hypergryph;
using Json=ehud::data::Json;
unsigned checks{};void check(bool v,const char* s) {++checks;if(!v) throw std::runtime_error(s);}
struct Window {
    ATOM atom{};HWND hwnd{};
    Window() {WNDCLASSW c{};c.lpfnWndProc=DefWindowProcW;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"EndfieldAccountOwnedFixture";atom=RegisterClassW(&c);check(atom!=0,"Register hidden fixture");
        hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,c.lpszClassName,L"Owned Account fixture",WS_POPUP,0,0,1280,800,nullptr,nullptr,c.hInstance,nullptr);check(hwnd&&!IsWindowVisible(hwnd),"Owned fixture stays hidden");}
    ~Window() {if(hwnd) DestroyWindow(hwnd);if(atom) UnregisterClassW(reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(atom)),GetModuleHandleW(nullptr));}
};
struct NoRequest:h::AccountRequest {void cancel() override {}};
// Scripted service: bindings and profiles answer immediately on the owner queue.
struct Service:h::AccountService {
    std::deque<std::function<void()>>& queue;double now;
    Service(std::deque<std::function<void()>>& q,double n):queue(q),now(n) {}
    h::RequestHandle refreshCredentials(const std::string&,h::Region,std::function<void(h::Outcome<h::Credentials>)> done) override {queue.push_back([done]{done(h::Error{h::ErrorKind::transport});});return std::make_shared<NoRequest>();}
    h::RequestHandle bindings(const h::Credentials&,h::Region region,std::function<void(h::Outcome<std::vector<h::Role>>)> done) override {
        h::Role ef;ef.region=region;ef.game=h::Game::endfield;ef.bindingUID="9";ef.roleID="4000500060";ef.serverID="1";ef.name="Endmin#1234";ef.serverName="Asia";ef.isDefault=true;
        h::Role ak;ak.region=region;ak.game=h::Game::arknights;ak.bindingUID="88001";ak.roleID="88001";ak.serverID="1";ak.name="Doctor";
        queue.push_back([done,ef,ak]{done(std::vector<h::Role>{ef,ak});});return std::make_shared<NoRequest>();
    }
    h::RequestHandle profile(const h::Role& role,const h::Credentials&,std::function<void(h::Outcome<h::Snapshot>)> done) override {
        h::Snapshot s;s.role=role;s.observedAt=now;s.name=role.name;s.stamina=h::Stamina{42,360,now+318*432.0,now};
        queue.push_back([done,s]{done(s);});return std::make_shared<NoRequest>();
    }
};
struct Immediate:h::AccountWorkQueue {
    std::deque<std::function<void()>>& queue;explicit Immediate(std::deque<std::function<void()>>& q):queue(q) {}
    void run(std::function<void()> work,std::function<void(std::exception_ptr)> done) override {
        std::exception_ptr error;try {work();} catch(...) {error=std::current_exception();}
        queue.push_back([done,error]{done(error);});
    }
};
void drain(std::deque<std::function<void()>>& q) {while(!q.empty()) {auto f=std::move(q.front());q.pop_front();f();}}
std::size_t painted(const gpu::Readback& r) {std::size_t n{};for(std::size_t i=3;i<r.pixels.size();i+=4) if(r.pixels[i]) ++n;return n;}
// Pixel rows and columns holding ink: [minX, minY, maxX, maxY).
std::array<long,4> ink(const h::GaugeImage& image) {
    std::array<long,4> box{long(image.width),long(image.height),0,0};
    for(unsigned y=0;y<image.height;++y) for(unsigned x=0;x<image.width;++x) if(image.rgba[(std::size_t(y)*image.width+x)*4+3])
        box={std::min(box[0],long(x)),std::min(box[1],long(y)),std::max(box[2],long(x)+1),std::max(box[3],long(y)+1)};
    return box;
}
std::vector<std::uint8_t> decode64(const std::string& text) {
    std::vector<std::uint8_t> out;std::uint32_t buffer{};int bits{};
    for(const char c:text) {int v;if(c>='A'&&c<='Z') v=c-'A';else if(c>='a'&&c<='z') v=c-'a'+26;else if(c>='0'&&c<='9') v=c-'0'+52;else if(c=='+') v=62;else if(c=='/') v=63;else continue;
        buffer=(buffer<<6)|static_cast<std::uint32_t>(v);bits+=6;if(bits>=8) {bits-=8;out.push_back(static_cast<std::uint8_t>((buffer>>bits)&0xff));}}
    return out;
}
// The value and the icon as the Mac draws them (Mac 795b5cd, 51d9dac): the
// same raster sizes, right-aligned and shrunk the same way, pure white, and
// the vector icon's shape, through the shared rasterizer.
void macRasters(gpu::LayerRasterizer& raster,const Json& g) {
    for(const auto& row:g["numbers"].array()) {
        const double scale=row["renderScale"].number();const auto& value=row["value"].string();
        const auto image=gpu::rasterGaugeText(raster,value,h::gaugeValueText,{121,30},scale,1,core::TextRegion::simplifiedChinese);
        check(image.width==row["width"].integer()&&image.height==row["height"].integer(),"The value raster has the Mac's size");
        const auto mine=ink(image);const auto& mac=row["ink"].array();const double unit=scale;
        std::cout<<"value '"<<value<<"' @"<<scale<<": ink "<<mine[0]<<","<<mine[1]<<","<<mine[2]<<","<<mine[3]<<" Mac "<<mac[0].integer()<<","<<mac[1].integer()<<","<<mac[2].integer()<<","<<mac[3].integer()<<"\n";
        check(std::abs(mine[2]-mac[2].integer())<=1.5*unit+1,"Right-aligned like the Mac");
        check(std::abs((mine[1]+mine[3])-(mac[1].integer()+mac[3].integer()))<=2*unit+1&&std::abs((mine[3]-mine[1])-(mac[3].integer()-mac[1].integer()))<=2*unit+1,"Centred on its ink at the Mac's size");
        check(std::abs(mine[0]-mac[0].integer())<=2*unit+.04*double(image.width),"The same advance, shrunk to fit like the Mac");
        bool white=true;for(std::size_t i=0;i<image.rgba.size();i+=4) white=white&&image.rgba[i]==image.rgba[i+3]&&image.rgba[i+1]==image.rgba[i+3]&&image.rgba[i+2]==image.rgba[i+3];
        check(white,"The value is pure white");
    }
    check(gpu::rasterGaugeText(raster,"",h::gaugeValueText,{121,30},2,1,core::TextRegion::simplifiedChinese).width==0,"No value draws nothing");
    // The recovery cells (Mac renderTooltip): each label and countdown in the
    // cell of the menu Windows lays out (measured like the gauge owner does),
    // drawn the way the scene draws them: the Mac's raster size, left-aligned,
    // centred on its ink and pure white (the CJK labels used to be 0.92 white).
    for(const auto& row:g["tooltips"].array()) {
        const auto language=row["language"].string()=="simplifiedChinese"?core::Language::simplifiedChinese:core::Language::english;
        const auto region=core::regionFor(language,raster.fontLanguage());
        h::SanityGaugeModel model;
        model.widths.standard=[&](std::string_view text){
            const auto descriptor=Json::Object{{"string",std::string(text)},{"fontSize",h::GaugePopoverLayout::textSize},{"fontWeight",500},
                {"lineHeight",1},{"maximumLines",1},{"language",std::string(core::regionTag(region))}};
            return raster.measureSourceText("account.gauge.label",descriptor,1e6).width;
        };
        const double at=781000000;
        h::SanityPresentation s{h::Game::endfield,42,360,at,at+row["next"].number(),at+row["full"].number(),false,true};
        (void)model.update("42 / 360","Sanity 42 of 360",true,2,s,at,language);model.perform("toggle");
        check(model.popoverOpen(),"The recovery menu opens");
        const auto& tip=model.tooltip();const double scale=model.renderScale();
        const char* names[]{"nextLabel","fullLabel","nextValue","fullValue"};
        for(unsigned n=0;n<4;++n) {
            const auto& mac=row[names[n]];if(mac.isNull()) continue;
            const auto box=tip.layout.cell(n);
            const auto image=gpu::rasterGaugeText(raster,tip.text[n],h::gaugeRecoveryText,{box.width,box.height},scale,1,region);
            const auto mine=ink(image);const auto& macInk=mac["ink"].array();
            std::cout<<names[n]<<" '"<<tip.text[n]<<"' @"<<scale<<": "<<image.width<<"x"<<image.height<<" ink "<<mine[0]<<","<<mine[1]<<","<<mine[2]<<","<<mine[3]
                     <<" Mac "<<mac["width"].integer()<<"x"<<mac["height"].integer()<<" ink "<<macInk[0].integer()<<","<<macInk[1].integer()<<","<<macInk[2].integer()<<","<<macInk[3].integer()<<"\n";
            // The measured label column may differ by a point between the text engines (99 for 100 in English).
            check(image.height==mac["height"].integer()&&std::abs(long(image.width)-long(mac["width"].integer()))<=long(scale),"A recovery cell has the Mac's raster size");
            check(std::abs(mine[0]-macInk[0].integer())<=2*scale+1,"A recovery cell is left-aligned like the Mac");
            check(std::abs(mine[2]-macInk[2].integer())<=2*scale+.04*double(image.width),"A recovery cell has the Mac's advance");
            check(std::abs((mine[1]+mine[3])-(macInk[1].integer()+macInk[3].integer()))<=2*scale+1&&std::abs((mine[3]-mine[1])-(macInk[3].integer()-macInk[1].integer()))<=2*scale+1,
                  "A recovery cell is centred on its ink at the Mac's size");
            bool white=true;for(std::size_t i=0;i<image.rgba.size();i+=4) white=white&&image.rgba[i]==image.rgba[i+3]&&image.rgba[i+1]==image.rgba[i+3]&&image.rgba[i+2]==image.rgba[i+3];
            check(white&&mac["white"].boolean(),"Every recovery cell is pure white");
        }
    }
    for(const auto& row:g["scales"].array()) {
        if(!row.contains("iconRGBA")) continue;
        const double scale=row["renderScale"].number();const auto mac=decode64(row["iconRGBA"].string());
        const auto icon=gpu::rasterSanityIcon(raster,54,scale,1);
        check(icon.width==row["icon"].array()[0].integer()&&icon.height==row["icon"].array()[1].integer()&&icon.rgba.size()==mac.size(),"The icon raster has the Mac's size");
        double difference{},macAlpha{},alpha{};
        for(std::size_t i=0;i<mac.size();++i) {difference+=std::abs(int(mac[i])-int(icon.rgba[i]));if(i%4==3) {macAlpha+=mac[i];alpha+=icon.rgba[i];}}
        std::cout<<"icon @"<<scale<<": mean difference "<<difference/double(mac.size())<<", coverage "<<alpha/macAlpha<<"\n";
        check(difference/double(mac.size())<4&&std::abs(alpha/macAlpha-1)<.02,"The vector icon matches the Mac drawing");
    }
}
void run(const std::filesystem::path& shader,const std::filesystem::path& resources,const Json& oracle) {
    Window window;gpu::Renderer renderer;renderer.initialize(window.hwnd,1280,800,{gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});
    renderer.setCamera(gpu::layerViewportProjection(1280,800));gpu::LayerRasterizer raster;
    std::deque<std::function<void()>> queue;double clock=812692800;Service service(queue,clock);h::MemoryCredentialVault vault;Immediate work(queue);
    h::AccountControllerOptions o;o.now=[&]{return clock;};o.localDate=[](double){return h::LocalDateFields{10,5,12,0,0};};
    h::HypergryphAccountController controller(service,vault,work,nullptr,nullptr,nullptr,nullptr,o);
    double time{};gpu::LayerComposition composition;
    // Link through the controller's test seam; the scripted service answers on
    // the owner queue (the Account page itself is a design canvas, compared
    // with the Mac oracle by account_canvas_tests and driven through its
    // owner by hypergryph_account_owner_tests).
    controller.setVisible(true,true); // the HUD shows the Account page (the controller refreshes only while visible)
    controller.acceptLogin("SYNTHETIC-CRED",h::Region::mainland,"synthetic-token","synthetic-device");drain(queue);drain(queue);drain(queue);
    check(controller.presentation().isLinked&&controller.presentation().accountName=="Endmin#1234","The controller links through the scripted service");
    // Header gauge shares the same controller.
    tools::AccountSanityGaugeOptions gaugeOptions;gaugeOptions.raster.pixelsPerPoint=1;gaugeOptions.workMode=[]{return h::WorkModeGaugeInput{true,1500,61.5};};gaugeOptions.workModeRunning=[]{return true;};
    tools::AccountSanityGaugePreview gauge(controller,raster,resources,gaugeOptions);gauge.resize({1280,800,96,1,1280,800});gauge.setOverlayVisible(true,time);
    check(gauge.model().value()=="42 / 360"&&gauge.model().canOpen(),"Linked Endfield header shows projected sanity");
    gauge.update({},1,time);gauge.upload(renderer);
    std::vector<gpu::LayerCompositionEntry> all(gauge.entries().begin(),gauge.entries().end());
    composition.setEntries(renderer,all);composition.present(renderer);renderer.draw(false);
    const auto shot=renderer.readback();const auto alphaAt=[&](double x,double y){return shot.pixels[std::size_t(y)*shot.rowBytes+std::size_t(x)*4+3];};
    check(alphaAt(562+100,2+21)>0&&alphaAt(562+30,2+21)>0,"Wallet and sanity icon paint at the header position (562,2)");
    const core::Point wallet{562+90,2+20};
    check(gauge.covers(wallet)&&gauge.pointer({app::PointerKind::down,app::PointerButton::left,wallet.x,wallet.y},++time)&&gauge.model().popoverOpen()&&gauge.capturesPointer(),"Wallet click opens the recovery popover");
    check(gauge.model().tooltip().text[2]!="—"&&gauge.requiresFrames(time),"Popover shows countdowns and animates open");
    const auto wake=gauge.nextWakeTime(time);check(wake&&*wake<=time+1.0001,"Open popover wakes for its next visible countdown second");
    gauge.dismiss(time);
    {const auto idle=gauge.nextWakeTime(time);check(idle&&*idle>time+1.5,"Closed sanity wallet wakes only for the refresh guard, recovery points or refresh (no per-second ticks)");}
    gauge.pointer({app::PointerKind::down,app::PointerButton::left,wallet.x,wallet.y},time);check(gauge.model().popoverOpen(),"Popover reopens");
    // Popover refresh (Mac onRefresh -> refresh(manual: true)) after the 5 s guard.
    check(!gauge.model().tooltip().refreshEnabled,"Refresh is guarded for 5 s after the last attempt");
    clock+=6;gauge.contentChanged(time+.1);check(gauge.model().tooltip().refreshEnabled,"Refresh enables after the guard");
    const core::Point refreshPoint{562+(-56+195+11.5),2+(40+19+12.5)};
    check(gauge.pointer({app::PointerKind::down,app::PointerButton::left,refreshPoint.x,refreshPoint.y},time+.15)&&controller.hasActiveRequest(),"Popover refresh starts one manual refresh");
    check(gauge.model().popoverOpen()&&!gauge.model().tooltip().refreshEnabled,"Refresh disables while syncing; the popover stays open");
    drain(queue);drain(queue);drain(queue);gauge.contentChanged(time+.18);
    check(!controller.hasActiveRequest()&&gauge.model().popoverOpen(),"Manual refresh completes through the owner queue");
    gauge.update({},1,time+.2);gauge.upload(renderer);all.assign(gauge.entries().begin(),gauge.entries().end());
    composition.setEntries(renderer,all);composition.present(renderer);renderer.draw(false);
    check(gauge.key({app::KeyKind::down,VK_ESCAPE},false,time+.3)&&!gauge.model().popoverOpen(),"Esc dismisses only the recovery popover");
    h::AccountAction hide;hide.kind=h::AccountAction::Kind::selectHeaderMode;hide.headerMode=h::AccountPresentation::HeaderMode::hidden;controller.perform(hide);gauge.contentChanged(time+.4);
    check(gauge.model().hidden()&&!gauge.model().canOpen(),"Hidden header mode removes the gauge");
    check(!gauge.covers(wallet)&&!gauge.pointer({app::PointerKind::down,app::PointerButton::left,wallet.x,wallet.y},time+.4),"A hidden wallet takes no press");
    hide.headerMode=h::AccountPresentation::HeaderMode::workMode;controller.perform(hide);gauge.contentChanged(time+.5);
    check(gauge.model().value()=="24 / 25"&&!gauge.model().canOpen(),"Work Mode header shows Work Mode minutes");
    // The shown wallet is a header control whatever it shows: a press on
    // Work Mode minutes is consumed (it never reaches the host's close for a
    // press outside the displayed circle) and opens nothing.
    for(const core::Point p:{wallet,core::Point{562+2,2+2},core::Point{562+166,2+40}})
        check(gauge.covers(p)&&gauge.pointer({app::PointerKind::down,app::PointerButton::left,p.x,p.y},time+.5)&&!gauge.model().popoverOpen()&&!gauge.capturesPointer(),
              "A press anywhere on the Work Mode wallet is consumed and opens nothing");
    check(!gauge.covers({562+169,2+20})&&!gauge.pointer({app::PointerKind::down,app::PointerButton::left,562+169,2+20},time+.5),"A press beside the wallet is not the wallet's");
    {
        // The header change made a refresh due inside the 5 s guard: one wake when it opens settles the 600 s cadence.
        const auto guard=gauge.nextWakeTime(time+.5);check(guard&&std::abs(*guard-(time+.5+5))<1e-6,"A due refresh blocked by the guard wakes once when it opens");
        clock+=5;gauge.deadline(time+.5);
        const auto minute=gauge.nextWakeTime(time+.5);check(minute&&std::abs(*minute-(time+.5+58.5))<1e-9,"A running Work Mode countdown then wakes once, at the next shown minute");
    }
    time+=1;gauge.update({},1,time);gauge.upload(renderer);all.assign(gauge.entries().begin(),gauge.entries().end());
    composition.setEntries(renderer,all);composition.present(renderer);
    const auto beforeGauge=gauge.sceneStats();allocations=0;counting=true;
    try {for(unsigned n=0;n<120;++n) {auto center=core::Matrix4{};center.values[3]=n*.000001;gauge.update(center,1,time+=1./60);gauge.upload(renderer);composition.present(renderer);}} catch(...) {counting=false;throw;}
    counting=false;check(allocations==0,"120 changing-tilt gauge frames allocate nothing");
    check(gauge.sceneStats().groupUploads==beforeGauge.groupUploads&&gauge.sceneStats().groupRedraws==beforeGauge.groupRedraws,"Tilt never redraws the retained gauge group targets");
    const auto stats=gauge.sceneStats();check(stats.artworkRasters==1&&stats.iconRasters==1,"Wallet artwork and the icon rasterize once per scale");
    // The recovery popover was loaded when it first opened; shut, it is no
    // longer composed, so a closed HUD releases its card (the wallet stays).
    check(stats.popoverLoads>=1&&stats.popoverUploaded&&gauge.entries().size()==1,"A shut popover is not composed");
    check(gauge.conceal()&&!gauge.sceneStats().popoverLoaded&&!gauge.sceneStats().popoverUploaded&&gauge.sceneStats().popoverParks==1&&gauge.entries().size()==1,
          "Parking releases the shut popover's card and keeps the wallet");
    check(!gauge.conceal(),"Parking twice changes nothing");
    // HUD Scale, layout or display (HUDAccountGauge.setScale): the same design
    // scale redraws nothing; a new one redraws the artwork, the icon and the
    // value once at the new render scale, and the wallet's target follows.
    {
        const auto before=gauge.sceneStats();gauge.setDesignScale(1,time);
        check(gauge.sceneStats().artworkRasters==before.artworkRasters&&gauge.sceneStats().iconRasters==before.iconRasters&&gauge.sceneStats().numberRasters==before.numberRasters,
              "An unchanged design scale redraws nothing");
        gauge.setDesignScale(2,time);const auto after=gauge.sceneStats();
        check(gauge.model().renderScale()==3&&after.artworkRasters==before.artworkRasters+1&&after.iconRasters==before.iconRasters+1&&after.numberRasters==before.numberRasters+1,
              "A new design scale redraws the artwork, icon and value once");
        gauge.setSizeFactor(2,time);
        check(gauge.model().renderScale()==6&&gauge.sceneStats().iconRasters==before.iconRasters+2,"A larger Sanity size redraws at its size");
        gauge.update({},1,time+=1./60);gauge.upload(renderer);composition.present(renderer);
        check(gauge.sceneStats().groupUploads==after.groupUploads+1,"The wallet's target follows the render scale");
    }
    // Teardown.
    composition.setEntries(renderer,{});gauge.collected(renderer);gauge.release(renderer);composition.detach(renderer);
    check(!IsWindowVisible(window.hwnd),"Fixture never shows UI");
    renderer.reset();
    macRasters(raster,oracle["gauge"]);
}
}
int wmain(int argc,wchar_t** argv) {
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        check(SUCCEEDED(hr)&&argc==4,"Pass shader, windows/resources/account and hypergryph-account-source.json");
        std::ifstream file(std::filesystem::path(argv[3]),std::ios::binary);check(bool(file),"Read the account oracle");
        const std::string bytes((std::istreambuf_iterator<char>(file)),{});
        run(argv[1],std::filesystem::absolute(argv[2]).lexically_normal(),Json::parse(bytes,4*1024*1024));CoUninitialize();std::cout<<"PASS "<<checks<<" Account native checks\n";return 0;}
    catch(const std::exception& e) {counting=false;if(SUCCEEDED(hr)) CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#endif
