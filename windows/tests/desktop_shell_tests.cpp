#include "scene/desktop_shell.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace ehud::scene;
namespace {
int checks{};
void check(bool valid,const char *reason){++checks;if(!valid)throw std::runtime_error(reason);}
void near(double actual,double expected,double tolerance,const char *reason){check(std::abs(actual-expected)<=tolerance,reason);}
const Graphic &graphic(const Frame &frame,std::string_view id) {
    auto it=std::find_if(frame.graphics.begin(),frame.graphics.end(),[&](const auto &value){return value.componentId==id;});
    if(it==frame.graphics.end())throw std::runtime_error("Missing native desktop graphic: "+std::string(id));return *it;
}
void localizationAndIdentity() {
    check(DesktopShell::rightModules()==std::array<DesktopModule,18>{
        DesktopModule::notes,DesktopModule::fileShelf,DesktopModule::clipboard,DesktopModule::archive,
        DesktopModule::mediaAssembly,DesktopModule::minigame,DesktopModule::nowPlaying,DesktopModule::volume,
        DesktopModule::projection,DesktopModule::reader,DesktopModule::workMode,DesktopModule::calendar,
        DesktopModule::map,DesktopModule::eventLog,DesktopModule::profile,DesktopModule::account,
        DesktopModule::power,DesktopModule::addApp},"Source desktop right navigation remains row-major");
    check(DesktopShell::title(DesktopModule::notes,DesktopLanguage::english)=="Notes","English source Notes title");
    check(DesktopShell::title(DesktopModule::notes,DesktopLanguage::simplifiedChinese)=="便笺","Simplified Chinese source Notes title");
    check(DesktopShell::title(DesktopModule::notes,DesktopLanguage::traditionalChinese)=="便箋","Traditional Chinese source Notes title");
    check(DesktopShell::title(DesktopModule::notes,DesktopLanguage::japanese)=="メモ","Japanese source Notes title");
    check(DesktopShell::title(DesktopModule::notes,DesktopLanguage::korean)=="메모","Korean source Notes title");
    check(DesktopShell::title(DesktopModule::about,DesktopLanguage::japanese)=="このアプリについて","About preserves the full catalog key");
    check(DesktopShell::caption(DesktopModule::fileShelf,DesktopLanguage::english)=="Temporary\nFile Shelf","English shelf uses source two-line caption");
    check(DesktopShell::caption(DesktopModule::fileShelf,DesktopLanguage::japanese)=="一時ファイル\nシェルフ","Japanese shelf uses source two-line caption");
    check(DesktopShell::caption(DesktopModule::clipboard,DesktopLanguage::english)=="Clipboard\nCache","Clipboard keeps source English line break");
    check(DesktopShell::group(DesktopModule::profile)==DesktopGroup::power,"Profile is a power group shown in the right pool");
    check(DesktopShell::group(DesktopModule::storage)==DesktopGroup::bottom,"Storage remains a bottom source button");
    check(DesktopShell::approvedIcon(DesktopModule::notes).filename()=="Mission_Icon.png","Notes maps to the approved mission icon");
    check(DesktopShell::approvedIcon(DesktopModule::clipboard).filename()=="Database_Icon.png","Clipboard maps to the approved database icon");
    check(DesktopShell::approvedIcon(DesktopModule::fileShelf).filename()=="Depot_icon.png","Shelf maps to the approved depot icon");
    check(DesktopShell::approvedIcon(DesktopModule::power).empty(),"Power uses its precise source action glyph");
    check(DesktopShell::resolveLanguage({"fr-FR","zh-Hans-TW"})==DesktopLanguage::simplifiedChinese,"Explicit Hans script wins over Taiwan region");
    check(DesktopShell::resolveLanguage({"zh_Hant_CN"})==DesktopLanguage::traditionalChinese,"Explicit Hant script wins over mainland region");
    check(DesktopShell::resolveLanguage({"zh-HK"})==DesktopLanguage::traditionalChinese,"Hong Kong source locale resolution");
    check(DesktopShell::resolveLanguage({"de-DE","ko-KR","en-US"})==DesktopLanguage::korean,"First supported preferred locale wins");
    check(DesktopShell::resolveLanguage({"","fr-FR"})==DesktopLanguage::english,"Unsupported locales have a readable English fallback");
}
void nativePlanesAndFade() {
    DesktopNativePlane plane{"source.plane",{{-50,-32},{100,64}},{1000,640}};
    auto rect=plane.rect({{270,2},{300,27}});
    near(rect.origin.x,-23,1e-12,"Header design X maps to source authored plane");
    near(rect.origin.y,29.1,1e-12,"Header top-left design Y maps to source +up");
    near(rect.size.x,30,1e-12,"Header source width uses plane calibration");
    near(plane.fontSize(20),2,1e-12,"Native font size follows calibrated source plane");
    DesktopShellFixture fixture;fixture.phase=Phase::opening;
    fixture.phaseElapsed=.19;near(DesktopShell::canvasAlpha(fixture),0,1e-12,"Source native canvas waits until .20s");
    fixture.phaseElapsed=.44;near(DesktopShell::canvasAlpha(fixture),1,1e-12,"Source native canvas reaches endpoint at .44s");
    double prior=0;
    for(int i=0;i<=24;++i){fixture.phaseElapsed=.20+i*.01;double alpha=DesktopShell::canvasAlpha(fixture);check(alpha>=prior-1e-12&&alpha<=1,"Native canvas fade is bounded and monotonic");prior=alpha;}
    fixture.phase=Phase::closing;fixture.closingCanvasOpacity=.37;fixture.phaseElapsed=.34;
    near(DesktopShell::canvasAlpha(fixture),.37,1e-12,"Interrupted close retains captured native opacity before .35s");
    fixture.phaseElapsed=.41;near(DesktopShell::canvasAlpha(fixture),0,1e-12,"Source native close fade ends at .41s");
    fixture.reduceMotion=true;near(DesktopShell::canvasAlpha(fixture),0,0,"Reduced-motion close has no visible native plane");
    fixture.phase=Phase::opening;near(DesktopShell::canvasAlpha(fixture),1,0,"Reduced-motion opening shows native endpoint");
    fixture.phase=Phase::concealed;near(DesktopShell::canvasAlpha(fixture),0,0,"Closed native plane cannot remain visible");
}
void source(const std::filesystem::path &root) {
    auto document=Document::loadDesktop(root);DesktopShell shell(document);
    const auto &info=*document.desktopInfo();
    DesktopShellFixture fixture;
    auto sourceProperties=shell.sourcePresentation(fixture);
    near(sourceProperties.properties.at(info.profileBindings.at("levelSlider")).at("m_FillAmount"),1,1e-12,"Isolated default authority matches canonical fresh profile level60");
    for(const auto *key:{"levelSlider","headFrameImg"}) {
        const auto &properties=sourceProperties.properties.at(info.profileBindings.at(key));
        near(properties.at("m_Color.r"),250.0/255,1e-12,"Profile fill/frame uses canonical yellow accent");
        near(properties.at("m_Color.g"),212.0/255,1e-12,"Profile fill/frame does not retain original game green");
    }
    for(const auto &id:info.profileGlowIds)
        near(sourceProperties.properties.at(id).at("m_Color.a"),id==info.profileHighlightId?1:0,0,"Only canonical profile root highlight retains source ColorTint alpha");
    check(sourceProperties.normalMaterialNodes==std::vector<SourceId>{info.profileHighlightId},"Only prepared profile hover plate uses explicit normal alpha");
    check(sourceProperties.properties.at(info.profileBackgroundId).at("desktop.profileArtwork")==1&&sourceProperties.properties.at(info.profileHighlightId).at("desktop.profileArtwork")==2,"Card and hover replace exact original source nodes");
    for(const auto &id:{info.profileBackgroundId,info.profileHighlightId})
        for(std::size_t channel=0;channel<3;++channel)
            near(sourceProperties.properties.at(id).at(std::string("desktop.profileAccent.")+"rgb"[channel]),fixture.accent[channel],0,"Profile artwork carries encoded source accent without global bitmap tint");
    fixture.permissionLevel=999;auto clamped=shell.sourcePresentation(fixture);
    near(clamped.properties.at(info.profileBindings.at("levelSlider")).at("m_FillAmount"),1,0,"Source profile authority is bounded at60");
    fixture.permissionLevel=60;
    FrameInput input;input.viewport={1920,1080};input.playback={Phase::visible,document.entranceDuration(),{}, {},0};input.desktopPresentation=&sourceProperties;
    auto frame=document.frame(input);auto hits=frame.hits;auto nodeCount=frame.nodes.size();
    auto nodeGraphic=[](const Frame &value,const SourceId &id)->const Graphic* {
        auto it=std::find_if(value.graphics.begin(),value.graphics.end(),[&](const auto &g){return g.nodeId==id;});
        return it==value.graphics.end()?nullptr:&*it;
    };
    auto background=nodeGraphic(frame,info.profileBackgroundId);
    check(background&&background->sampledProperties.at("desktop.profileArtwork")==1&&!background->normalMaterial,"Prepared themed card retains original source UI material policy");
    ButtonMotion profileMotion(document);profileMotion.reset(0,true);profileMotion.setHovered(true,info.profileRootId,1);
    auto hoverInput=input;hoverInput.interaction=&profileMotion;hoverInput.time=2;
    auto hovered=document.frame(hoverInput);auto hover=nodeGraphic(hovered,info.profileHighlightId);
    check(hover&&hover->normalMaterial&&hover->sampledProperties.at("desktop.profileArtwork")==2&&hover->color[3]>0,"Source root hover finitefade reveals prepared normal-alpha plate");
    auto rawHoverInput=hoverInput;rawHoverInput.desktopPresentation=nullptr;
    auto rawHovered=document.frame(rawHoverInput);auto rawHover=nodeGraphic(rawHovered,info.profileHighlightId);
    check(rawHover&&hover->textureId==rawHover->textureId&&hover->quads.size()==rawHover->quads.size()&&hover->uvQuads.size()==rawHover->uvQuads.size(),"Native profile policy retains original highlight texture identity and authored mesh");
    near(hover->color[3],rawHover->color[3],0,"Profile replacement alpha comes from source ColorTint, without second fade");
    check(!profileMotion.requiresFrames(2),"Settled profile hover cannot require a perpetual animation loop");
    profileMotion.setHovered(false,info.profileRootId,2);hoverInput.time=3;
    auto departed=document.frame(hoverInput);auto hiddenHover=nodeGraphic(departed,info.profileHighlightId);
    check(!hiddenHover||hiddenHover->color[3]==0,"Source ColorTint fully hides native profile plate after finite exit");
    check(!profileMotion.requiresFrames(3),"Profile finite hover exit parks frame demand");
    auto blueFixture=fixture;blueFixture.accent={.12,.66,.95};auto blueProperties=shell.sourcePresentation(blueFixture);
    auto blueInput=input;blueInput.desktopPresentation=&blueProperties;auto blue=document.frame(blueInput);auto blueBackground=nodeGraphic(blue,info.profileBackgroundId);
    check(blueBackground&&blueBackground->textureId==background->textureId&&blueBackground->quads.size()==background->quads.size(),"Alternate synthetic accent never selects an unrelated card sprite or geometry");
    for(std::size_t channel=0;channel<3;++channel)
        near(blueBackground->sampledProperties.at(std::string("desktop.profileAccent.")+"rgb"[channel]),blueFixture.accent[channel],0,"Synthetic accent reaches cached renderer artwork contract");
    auto presentation=shell.decorate(frame,fixture);
    check(presentation.centerPlane.has_value()&&presentation.statusPlane.has_value()&&presentation.footerPlane.has_value(),"Native header/status/footer have explicit source planes");
    check(presentation.sourceLogoRetained,"Default source Endfield lettering remains in the source packet");
    check(frame.nodes.size()==nodeCount&&frame.hits.size()==hits.size(),"Shell presentation preserves source nodes and irregular hit regions");
    for(std::size_t i=0;i<hits.size();++i){check(frame.hits[i].buttonId==hits[i].buttonId,"Shell labels cannot change source button identity");check(frame.hits[i].world.values==hits[i].world.values,"Shell labels cannot flatten source hit transforms");}
    std::set<DesktopModule> modules;
    for(const auto &binding:presentation.bindings) {
        modules.insert(binding.module);check(!binding.implemented,"Presentation fixture must not impersonate a migrated module");
        check(binding.title==DesktopShell::title(binding.module,fixture.language),"Accessible navigation title uses canonical module identity");
        if(binding.sourcePath.find("/RightBottomNode/")!=std::string::npos)
            check(binding.module==DesktopShell::rightModules().at(frame.desktopRightAssignments.at(binding.buttonId)),"Recycled physical row uses this exact frame's logical assignment");
    }
    check(modules.size()==24,"All24 canonical module identities are represented without adding invented functions");
    check(graphic(frame,"desktop.shell.header.title").text=="ENDFIELDHUD","Native source header title");
    check(graphic(frame,"desktop.shell.header.subtitle").text=="SYSTEM INTERFACE","Native source header subtitle");
    check(graphic(frame,"desktop.shell.clock.time").text==fixture.clockTime,"Clock text is caller-provided fixture data");
    check(graphic(frame,"desktop.shell.profile.managerName").text=="Endministrator","Isolated name uses canonical fresh profile default");
    check(graphic(frame,"desktop.shell.profile.managerNumber").text=="UID: —","Unlinked fixture cannot display a fabricated account UID");
    check(graphic(frame,"desktop.shell.profile.progressTxt").text=="MAX","Canonical source fresh profile authority60 shows source MAX label");
    check(graphic(frame,"desktop.shell.profile.portraitPlate").kind=="DesktopVector"&&graphic(frame,"desktop.shell.profile.portraitSilhouette").kind=="DesktopVector","No-photo default uses exact native portrait silhouette");
    auto portraitId=info.profileBindings.at("playerHead");
    check(std::none_of(frame.graphics.begin(),frame.graphics.end(),[&](const auto &g){return g.nodeId==portraitId&&!g.componentId.starts_with("desktop.shell.profile.portrait");}),"Original game character portrait cannot leak into desktop default");
    check(graphic(frame,"desktop.shell.clock.time").sampledProperties.at("desktop.textAlignment")==2&&graphic(frame,"desktop.shell.clock.date").sampledProperties.at("desktop.textAlignment")==2,"Digital source clock time/date remain right aligned");
    check(graphic(frame,"desktop.shell.clock.time").sampledProperties.at("desktop.monospacedDigits")==1,"Digital source time requires tabular numerals");
    for(const auto *key:{"desktop.shell.header.title","desktop.shell.header.subtitle","desktop.shell.footer.hint","desktop.shell.clock.date"})
        check(graphic(frame,key).sampledProperties.at("desktop.fontFamily")==1,"Source native mono text retains its font-family contract");
    check(!graphic(frame,"desktop.shell.clock.outerStroke").quads.empty()&&!graphic(frame,"desktop.shell.clock.innerStroke").quads.empty(),"Clock keeps authored outer accent and inner neutral strokes");
    check(graphic(frame,"desktop.shell.clock.indicators").quads.size()==5&&graphic(frame,"desktop.shell.clock.selectedDigital").quads.size()==1,"Clock renders exactly five authored marks and one selected digital mark");
    check(graphic(frame,"desktop.shell.footer.hint").text=="ESC / CTRL + SHIFT + E / CLICK OUTSIDE TO CLOSE","Footer matches canonical close hint with supplied native shortcut");
    auto headerPoint=frame.camera.project(graphic(frame,"desktop.shell.header.title").quads.front()[1],graphic(frame,"desktop.shell.header.title").world);
    auto footerPoint=frame.camera.project(graphic(frame,"desktop.shell.footer.hint").quads.front()[1],graphic(frame,"desktop.shell.footer.hint").world);
    check(headerPoint.has_value()&&footerPoint.has_value(),"Native header and footer vertices project through the submitted camera");
    auto count=frame.graphics.size();shell.decorate(frame,fixture);check(frame.graphics.size()==count,"Repeated decoration does not append duplicates or lose Report artwork");
    for(const auto &graphic:frame.graphics) {
        check(graphic.kind!="UIText","Raw game label text cannot leak into the desktop shell");
        if(graphic.kind=="DesktopIcon") {
            check(std::filesystem::exists(root.parent_path().parent_path()/graphic.texturePath),"Every native icon resolves to an approved fresh resource");
            check(graphic.uvQuads.front()[1].y==0&&graphic.uvQuads.front()[0].y==1,"PNG top-left rows have explicit native UV orientation");
        }
        if(graphic.kind.starts_with("Desktop")){check(graphic.materialId.empty(),"Native overlays cannot accidentally run source game FX");check(graphic.color[3]>=0&&graphic.color[3]<=1,"Native overlay alpha remains bounded");}
    }
    // A gyro-only source update must move the header along its authored plane,
    // while the deliberately untransformed source footer stays screen-aligned.
    check(graphic(frame,"desktop.shell.footer.hint").fixedWorld,"Source footer explicitly retains its fixed screen world");
    document.reproject(frame,{0,std::sin(.05),0,std::cos(.05)});
    auto directFooter=frame.camera.project(graphic(frame,"desktop.shell.footer.hint").quads.front()[1],graphic(frame,"desktop.shell.footer.hint").world);
    check(directFooter.has_value(),"Footer remains projectable during direct pointer fast-path update");
    near(directFooter->x,footerPoint->x,1e-6,"Direct pointer fast path cannot move screen footer X");
    near(directFooter->y,footerPoint->y,1e-6,"Direct pointer fast path cannot move screen footer Y");
    shell.decorate(frame,fixture);
    auto movedHeader=frame.camera.project(graphic(frame,"desktop.shell.header.title").quads.front()[1],graphic(frame,"desktop.shell.header.title").world);
    auto movedFooter=frame.camera.project(graphic(frame,"desktop.shell.footer.hint").quads.front()[1],graphic(frame,"desktop.shell.footer.hint").world);
    check(movedHeader&&movedFooter,"Gyro native planes remain projectable");
    check(std::hypot(movedHeader->x-headerPoint->x,movedHeader->y-headerPoint->y)>.01,"Header follows the authored gyro plane");
    near(movedFooter->x,footerPoint->x,1e-6,"Source footer screen X remains fixed during gyro");near(movedFooter->y,footerPoint->y,1e-6,"Source footer screen Y remains fixed during gyro");
    for(auto language:{DesktopLanguage::english,DesktopLanguage::simplifiedChinese,DesktopLanguage::traditionalChinese,DesktopLanguage::japanese,DesktopLanguage::korean}) {
        fixture.language=language;auto localized=document.frame(input);auto localizedPresentation=shell.decorate(localized,fixture);
        check(localizedPresentation.bindings.size()==presentation.bindings.size(),"Changing locale preserves the source navigation identities");
        for(const auto &binding:localizedPresentation.bindings)check(!binding.title.empty(),"Every shipped navigation locale has a catalog label");
    }
}
} // namespace
int main(int argc,char **argv) {
    try {
        localizationAndIdentity();nativePlanesAndFade();
        if(argc>1)source(std::filesystem::path(argv[1]));
        std::cout<<"Passed "<<checks<<" desktop shell contract checks"<<(argc>1?" including fresh source resources":" (synthetic fixtures)")<<'\n';
        std::cout<<"Live Mac typography, native IME, desktop recording and visual parity remain unverified.\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<"Desktop shell contract failed: "<<error.what()<<'\n';return 1;}
}
