#include "scene/desktop_shell.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <mutex>
#include <numbers>
#include <stdexcept>

namespace ehud::scene {
namespace {
constexpr std::array<DesktopModule, 18> rightOrder{
    DesktopModule::notes, DesktopModule::fileShelf, DesktopModule::clipboard, DesktopModule::archive,
    DesktopModule::mediaAssembly, DesktopModule::minigame, DesktopModule::nowPlaying, DesktopModule::volume,
    DesktopModule::projection, DesktopModule::reader, DesktopModule::workMode, DesktopModule::calendar,
    DesktopModule::map, DesktopModule::eventLog, DesktopModule::profile, DesktopModule::account,
    DesktopModule::power, DesktopModule::addApp};
struct Translation {
    DesktopModule module;
    const char *id;
    std::array<const char *, 5> values;
    const char *icon;
};
// Source pairs are HUDModule.title + LocalizationCatalog (including its Korean
// map). Asset names are HUDNavigationEntry.gameIcon -> EndfieldGameIcon.rawValue.
constexpr Translation translations[]{
    {DesktopModule::system,"system",{"System","系统","系統","システム","시스템"},""},
    {DesktopModule::display,"display",{"Display","显示","顯示","表示","화면"},""},
    {DesktopModule::hotkeys,"hotkeys",{"Hotkeys","快捷键","快速鍵","ショートカット","단축키"},""},
    {DesktopModule::about,"about",{"About","关于","關於","このアプリについて","정보"},""},
    {DesktopModule::storage,"storage",{"Storage","存储","儲存","ストレージ","저장 공간"},"Factory_icon"},
    {DesktopModule::activityMonitor,"activityMonitor",{"Activity Monitor","活动监视器","活動監視器","アクティビティモニタ","활성 상태 보기"},""},
    {DesktopModule::notes,"notes",{"Notes","便笺","便箋","メモ","메모"},"Mission_Icon"},
    {DesktopModule::fileShelf,"fileShelf",{"Temporary File Shelf","文件暂存架","檔案暫存架","一時ファイルシェルフ","임시 파일 보관함"},"Depot_icon"},
    {DesktopModule::clipboard,"clipboard",{"Clipboard Cache","剪贴板","剪貼簿","クリップボード","클립보드 캐시"},"Database_Icon"},
    {DesktopModule::archive,"archive",{"Archive","档案库","檔案庫","アーカイブ","아카이브"},"Archive_Icon"},
    {DesktopModule::mediaAssembly,"mediaAssembly",{"Media Assembly","影像加工","影像加工","メディア編集","미디어 편집"},"MediaAssembly_Icon"},
    {DesktopModule::minigame,"minigame",{"Closure's Minigame","可露希尔的小游戏","可露希爾的小遊戲","クロージャのミニゲーム","클로저의 미니게임"},"Minigame_Icon"},
    {DesktopModule::nowPlaying,"nowPlaying",{"Now Playing","当前播放","目前播放","再生中","지금 재생 중"},""},
    {DesktopModule::volume,"volume",{"Volume","音量","音量","音量","음량"},""},
    {DesktopModule::projection,"projection",{"Projection","投影","投影","プロジェクション","화면 필기"},"Projection_Icon"},
    {DesktopModule::reader,"reader",{"E-Reader","阅读器","閱讀器","リーダー","리더"},"Reader_Icon"},
    {DesktopModule::workMode,"workMode",{"Work Mode","工作模式","工作模式","作業モード","작업 모드"},"STR"},
    {DesktopModule::calendar,"calendar",{"Calendar","日历","日曆","カレンダー","달력"},"Calendar_Icon"},
    {DesktopModule::map,"map",{"Map","地图","地圖","マップ","지도"},"Region_icon"},
    {DesktopModule::eventLog,"eventLog",{"Event Log","事件日志","事件記錄","イベントログ","이벤트 로그"},"Questionnaire_Icon"},
    {DesktopModule::profile,"profile",{"Personal Profile","个人名片","個人名片","プロフィール","개인 프로필"},"Friends_Icon"},
    {DesktopModule::account,"account",{"Account Linking","账户绑定","帳戶綁定","アカウント連携","계정 연동"},"Operator_icon"},
    {DesktopModule::power,"power",{"Power","电源","電源","電源","전원"},""},
    {DesktopModule::addApp,"addApp",{"+ Add App","+ 添加应用","+ 新增程式","+ アプリを追加","+ 앱 추가"},""}
};
const Translation &translation(DesktopModule module) {
    auto it=std::find_if(std::begin(translations),std::end(translations),[&](const auto &t){return t.module==module;});
    if(it==std::end(translations)) throw std::invalid_argument("Unknown desktop module");
    return *it;
}
bool chinese(DesktopLanguage language) {
    return language==DesktopLanguage::simplifiedChinese || language==DesktopLanguage::traditionalChinese;
}
bool suffix(std::string_view path,std::string_view value) {return path.ends_with(value);}
double finiteUnit(double value,double fallback=0) {return std::isfinite(value)?std::clamp(value,0.0,1.0):fallback;}
std::array<Vec3,4> quad(Rect rect) {
    auto x=rect.origin.x,y=rect.origin.y,w=rect.size.x,h=rect.size.y;
    return {{{x,y,0},{x,y+h,0},{x+w,y+h,0},{x+w,y,0}}};
}
void geometry(Graphic &graphic,Rect rect) {
    graphic.rect=rect; graphic.quads={quad(rect)};
    graphic.uvQuads={{{{0,0},{0,1},{1,1},{1,0}}}};
}
Graphic overlay(const DesktopNativePlane &plane,std::string key,Rect rect,std::array<double,4> color) {
    Graphic result;
    result.nodeId=plane.nodeId; result.componentId="desktop.shell."+key;
    result.path=result.componentId; result.world=plane.world; result.sceneWorld=plane.sceneWorld;
    result.fixedWorld=plane.nodeId=="desktop.footer";
    result.masks=plane.masks; result.sortingOrder=plane.sortingOrder+10;
    result.color=color; result.color[3]*=finiteUnit(plane.alpha); result.vertexColorReady=true;
    geometry(result,rect); return result;
}
void text(Frame &frame,const DesktopNativePlane &plane,std::string key,std::string value,Rect design,
          double size,std::array<double,4> color,int alignment=1,bool wrap=false,int weight=500,bool fit=false) {
    auto graphic=overlay(plane,std::move(key),plane.rect(design),color);
    graphic.kind="DesktopText"; graphic.text=std::move(value); graphic.fontSize=plane.fontSize(size);
    graphic.sampledProperties={{"desktop.textAlignment",double(alignment)},{"desktop.textWrap",wrap?1.0:0.0},
        {"desktop.textTruncate",fit?1.0:0.0},{"desktop.textVerticalAlignment",0},{"desktop.fontWeight",double(weight)},
        {"desktop.textFit",fit?1.0:0.0},{"desktop.minimumFontSize",plane.fontSize(10)},
        {"desktop.fontSizeStep",plane.fontSize(0.5)}};
    frame.graphics.push_back(std::move(graphic));
}
DesktopNativePlane nativePlane(const NodeGeometry &node,Rect rect,Vec2 design) {
    return {node.id,rect,design,node.world,node.sceneWorld,node.inheritedAlpha,node.sortingOrder,node.masks};
}
// Source action silhouettes use even-odd fill. Curves are flattened only for
// native vertex submission (<0.003 icon-unit error); the source paths and sizes
// remain canonical. Each edge intersection is added to the scanline partition,
// so overlap/hole interiors are never filled by an unrelated bounding box.
using Polygon=std::vector<Vec2>;
struct Paths {
    std::vector<Polygon> contours;
    void polygon(std::initializer_list<Vec2> values){contours.emplace_back(values);}
    void box(double x,double y,double w,double h){polygon({{x,y},{x+w,y},{x+w,y+h},{x,y+h}});}
    void ellipse(double x,double y,double w,double h) {
        Polygon path; constexpr int steps=192;
        for(int i=0;i<steps;++i){double a=2*std::numbers::pi*i/steps;path.push_back({x+w/2+w/2*std::cos(a),y+h/2+h/2*std::sin(a)});}
        contours.push_back(std::move(path));
    }
    void rounded(double x,double y,double w,double h,double radius) {
        Polygon path; constexpr int steps=32;
        for(int corner=0;corner<4;++corner) {
            Vec2 center{corner==0||corner==3?x+w-radius:x+radius,corner<2?y+h-radius:y+radius};
            for(int i=0;i<=steps;++i) {
                double a=(corner*0.5+i/(2.0*steps))*std::numbers::pi;
                path.push_back({center.x+radius*std::cos(a),center.y+radius*std::sin(a)});
            }
        } contours.push_back(std::move(path));
    }
    void arcStroke(Vec2 center,double radius,double from,double to,double width) {
        Polygon path; auto steps=std::max(2,int(std::ceil(std::abs(to-from)*64)));
        for(int i=0;i<=steps;++i){double a=from+(to-from)*i/steps;path.push_back({center.x+(radius+width/2)*std::cos(a),center.y+(radius+width/2)*std::sin(a)});}
        for(int i=steps;i>=0;--i){double a=from+(to-from)*i/steps;path.push_back({center.x+(radius-width/2)*std::cos(a),center.y+(radius-width/2)*std::sin(a)});}
        contours.push_back(std::move(path));
    }
    void portraitBody(double side) {
        Polygon path;constexpr int steps=96;
        const Vec2 a{.18*side,.86*side},b{.24*side,.45*side},c{.77*side,.45*side},d{.83*side,.86*side};
        for(int i=0;i<=steps;++i){double t=double(i)/steps,s=1-t;
            path.push_back({s*s*s*a.x+3*s*s*t*b.x+3*s*t*t*c.x+t*t*t*d.x,
                s*s*s*a.y+3*s*s*t*b.y+3*s*t*t*c.y+t*t*t*d.y});}
        contours.push_back(std::move(path));
    }
};
std::vector<std::array<Vec3,4>> tessellate(const Paths &paths) {
    struct Edge {Vec2 a,b;double at(double y)const{return a.x+(b.x-a.x)*(y-a.y)/(b.y-a.y);}};
    std::vector<Edge> edges;std::vector<double> bands;
    for(const auto &path:paths.contours) for(std::size_t i=0;i<path.size();++i) {
        auto a=path[i],b=path[(i+1)%path.size()];bands.push_back(a.y);
        if(std::abs(a.y-b.y)>1e-12) edges.push_back({a,b});
    }
    for(std::size_t i=0;i<edges.size();++i) for(std::size_t j=i+1;j<edges.size();++j) {
        const auto &a=edges[i],&b=edges[j];double lo=std::max(std::min(a.a.y,a.b.y),std::min(b.a.y,b.b.y));
        double hi=std::min(std::max(a.a.y,a.b.y),std::max(b.a.y,b.b.y));
        if(hi-lo<1e-12)continue;
        double dl=a.at(lo)-b.at(lo),dh=a.at(hi)-b.at(hi);
        if(dl*dh<0)bands.push_back(lo+(hi-lo)*dl/(dl-dh));
    }
    std::sort(bands.begin(),bands.end());
    bands.erase(std::unique(bands.begin(),bands.end(),[](double a,double b){return std::abs(a-b)<1e-10;}),bands.end());
    std::vector<std::array<Vec3,4>> result;
    for(std::size_t i=1;i<bands.size();++i) {
        double y0=bands[i-1],y1=bands[i],middle=(y0+y1)/2;
        if(y1-y0<1e-10)continue;
        std::vector<const Edge *> crossing;
        for(const auto &edge:edges) if(middle>std::min(edge.a.y,edge.b.y)&&middle<std::max(edge.a.y,edge.b.y))crossing.push_back(&edge);
        std::sort(crossing.begin(),crossing.end(),[&](auto a,auto b){return a->at(middle)<b->at(middle);});
        for(std::size_t j=1;j<crossing.size();j+=2) {
            auto left=crossing[j-1],right=crossing[j];
            if(right->at(middle)-left->at(middle)<1e-10)continue;
            result.push_back({{{left->at(y1),y1,0},{left->at(y0),y0,0},{right->at(y0),y0,0},{right->at(y1),y1,0}}});
        }
    }return result;
}
void vector(Frame &frame,const DesktopNativePlane &plane,std::string key,const Paths &paths,std::array<double,4> color) {
    // Keys below are a fixed set of canonical skin shapes. Cache triangulation
    // independently of viewport/tilt; camera motion only remaps their vertices.
    static std::mutex cacheMutex;
    static std::map<std::string,std::vector<std::array<Vec3,4>>> cache;
    std::vector<std::array<Vec3,4>> geometry;
    {std::scoped_lock lock(cacheMutex);auto it=cache.find(key);if(it==cache.end())it=cache.emplace(key,tessellate(paths)).first;geometry=it->second;}
    auto graphic=overlay(plane,std::move(key),plane.sourceRect,color);
    graphic.kind="DesktopVector";graphic.quads=std::move(geometry);graphic.uvQuads.clear();
    for(auto &q:graphic.quads){for(auto &v:q){auto point=plane.rect({{v.x,v.y},{0,0}});v={point.origin.x,point.origin.y,0};}
        graphic.uvQuads.push_back({{{0,0},{0,1},{1,1},{1,0}}});}
    frame.graphics.push_back(std::move(graphic));
}
Paths roundedStroke(Rect rect,double radius,double width) {
    Paths result;auto x=rect.origin.x,y=rect.origin.y,w=rect.size.x,h=rect.size.y;
    result.rounded(x-width/2,y-width/2,w+width,h+width,radius+width/2);
    result.rounded(x+width/2,y+width/2,w-width,h-width,radius-width/2);return result;
}
const std::vector<std::array<Vec3,4>> &silhouette(DesktopModule module) {
    static const auto cache=[] {
        std::map<DesktopModule,std::vector<std::array<Vec3,4>>> result;
        for(auto module:{DesktopModule::system,DesktopModule::display,DesktopModule::hotkeys,DesktopModule::about,
            DesktopModule::nowPlaying,DesktopModule::volume,DesktopModule::power,DesktopModule::addApp}) {
            Paths p;
            switch(module) {
            case DesktopModule::system:
                p.box(7,7,18,18);p.box(12,12,8,8);
                for(auto v:{9.0,19.0}){p.box(v,1,4,5);p.box(v,26,4,5);p.box(1,v,5,4);p.box(26,v,5,4);}break;
            case DesktopModule::display:
                p.rounded(2,4,28,20,2);p.box(6,8,20,12);p.box(13,25,6,2);p.box(8,28,16,3);break;
            case DesktopModule::hotkeys:
                p.rounded(1,6,30,21,2);for(auto x:{5.0,11.0,17.0,23.0})p.box(x,10,4,4);p.box(6,19,20,3);break;
            case DesktopModule::about:p.ellipse(2,2,28,28);p.box(14,7,4,4);p.box(14,14,4,11);break;
            case DesktopModule::nowPlaying:p.ellipse(2,2,28,28);p.ellipse(5,5,22,22);p.polygon({{12,9},{24,16},{12,23}});break;
            case DesktopModule::volume:
                p.polygon({{2,12},{8,12},{17,5},{17,27},{8,20},{2,20}});
                for(auto r:{7.0,12.0})p.arcStroke({17,16},r,-std::numbers::pi/4,std::numbers::pi/4,3);break;
            case DesktopModule::power:
                p.arcStroke({16,17},11,-std::numbers::pi*0.28,std::numbers::pi*1.28,4.2);p.box(14,1,4,15);break;
            case DesktopModule::addApp:
                p.rounded(3,3,26,26,3);p.box(7,7,18,18);
                p.polygon({{14,10},{18,10},{18,14},{22,14},{22,18},{18,18},{18,22},{14,22},{14,18},{10,18},{10,14},{14,14}});break;
            default:break;
            }
            double minX=std::numeric_limits<double>::max(),minY=minX,maxX=-minX,maxY=-minX;
            for(auto &path:p.contours)for(auto v:path){minX=std::min(minX,v.x);maxX=std::max(maxX,v.x);minY=std::min(minY,v.y);maxY=std::max(maxY,v.y);}
            double scale=26/std::max(maxX-minX,maxY-minY);
            for(auto &path:p.contours)for(auto &v:path){v.x=16+(v.x-(minX+maxX)/2)*scale;v.y=16+(v.y-(minY+maxY)/2)*scale;}
            result.emplace(module,tessellate(p));
        }return result;
    }();
    return cache.at(module);
}
double sourceEase(double progress) {
    progress=finiteUnit(progress);double lo=0,hi=1;
    auto bezier=[](double t,double a,double b){return 3*(1-t)*(1-t)*t*a+3*(1-t)*t*t*b+t*t*t;};
    for(int i=0;i<48;++i){double mid=(lo+hi)/2;if(bezier(mid,0.20,0.22)<progress)lo=mid;else hi=mid;}
    return bezier((lo+hi)/2,0.72,1);
}
} // namespace

Rect DesktopNativePlane::rect(Rect design) const {
    double sx=sourceRect.size.x/designSize.x,sy=sourceRect.size.y/designSize.y;
    return {{sourceRect.origin.x+design.origin.x*sx,
        sourceRect.origin.y+(designSize.y-design.origin.y-design.size.y)*sy},{design.size.x*sx,design.size.y*sy}};
}
double DesktopNativePlane::fontSize(double points) const {return points*sourceRect.size.y/designSize.y;}
DesktopShell::DesktopShell(const Document &document):document_(document),buttons_(document.buttons()) {
    if(!document.desktopInfo())throw std::invalid_argument("DesktopShell requires Document::loadDesktop");
    info_=*document.desktopInfo();
    for(const auto *key:{"levelSlider","headFrameImg"})if(auto it=info_.profileBindings.find(key);it!=info_.profileBindings.end())profileAccentIds_.push_back(it->second);
    // Original decorations are node names, not Lua bindings. Resolve once from
    // a source frame; pointer updates never enumerate or rebuild this mapping.
    FrameInput input;input.playback={Phase::visible,document_.entranceDuration(),{}, {},0};input.rootRotation=document_.initialRootRotation();
    auto neutral=document_.frame(input);auto root=neutral.node(info_.profileRootId);
    if(root)for(const auto &node:neutral.nodes)if(node.path.starts_with(root->path+"/")&&(suffix(node.path,"/IconRight")||suffix(node.path,"/ArrowImage")))profileAccentIds_.push_back(node.id);
}
const std::array<DesktopModule,18> &DesktopShell::rightModules(){return rightOrder;}
std::string DesktopShell::identifier(DesktopModule module){return translation(module).id;}
DesktopGroup DesktopShell::group(DesktopModule module) {
    switch(module){case DesktopModule::system:case DesktopModule::display:case DesktopModule::hotkeys:case DesktopModule::about:return DesktopGroup::left;
    case DesktopModule::storage:case DesktopModule::activityMonitor:return DesktopGroup::bottom;
    case DesktopModule::profile:case DesktopModule::power:return DesktopGroup::power;default:return DesktopGroup::right;}
}
std::string DesktopShell::title(DesktopModule module,DesktopLanguage language) {
    auto index=static_cast<std::size_t>(language);if(index>=5)throw std::invalid_argument("Unknown desktop language");
    return translation(module).values[index];
}
std::string DesktopShell::caption(DesktopModule module,DesktopLanguage language) {
    if(language==DesktopLanguage::english) switch(module){
        case DesktopModule::fileShelf:return "Temporary\nFile Shelf";case DesktopModule::clipboard:return "Clipboard\nCache";
        case DesktopModule::activityMonitor:return "Activity\nMonitor";case DesktopModule::profile:return "Personal\nProfile";default:break;}
    if(language==DesktopLanguage::japanese&&module==DesktopModule::fileShelf)return "一時ファイル\nシェルフ";
    return title(module,language);
}
std::filesystem::path DesktopShell::approvedIcon(DesktopModule module) {
    const auto &value=translation(module);return *value.icon?std::filesystem::path("AppIconSources/EndfieldWiki")/(std::string(value.icon)+".png"):std::filesystem::path{};
}
DesktopLanguage DesktopShell::resolveLanguage(const std::vector<std::string> &preferred) {
    for(auto value:preferred) {
        std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return c=='_'?'-':char(std::tolower(c));});
        std::vector<std::string> parts;std::size_t offset=0;
        do{auto end=value.find('-',offset);parts.push_back(value.substr(offset,end-offset));if(end==std::string::npos)break;offset=end+1;}while(offset<=value.size());
        auto has=[&](std::string_view part){return std::find(parts.begin(),parts.end(),part)!=parts.end();};
        if(parts.front()=="en")return DesktopLanguage::english;if(parts.front()=="ja")return DesktopLanguage::japanese;if(parts.front()=="ko")return DesktopLanguage::korean;
        if(parts.front()=="zh") {if(has("hant"))return DesktopLanguage::traditionalChinese;if(has("hans"))return DesktopLanguage::simplifiedChinese;
            return has("tw")||has("hk")||has("mo")?DesktopLanguage::traditionalChinese:DesktopLanguage::simplifiedChinese;}
    }return DesktopLanguage::english;
}
double DesktopShell::canvasAlpha(const DesktopShellFixture &fixture) {
    switch(fixture.phase){case Phase::concealed:return 0;case Phase::visible:return 1;
    case Phase::opening:return fixture.reduceMotion?1:sourceEase((fixture.phaseElapsed-0.20)/0.24);
    case Phase::closing:return fixture.reduceMotion?0:finiteUnit(fixture.closingCanvasOpacity)*(1-sourceEase((fixture.phaseElapsed-0.35)/0.06));}return 0;
}
DesktopPresentation DesktopShell::sourcePresentation(const DesktopShellFixture &fixture) const {
    DesktopPresentation result;
    // setDesktopProfile retains exactly the root Selectable's highlight. Its
    // original ColorTint owns the finite fade of the normal-alpha native plate.
    for(const auto &id:info_.profileGlowIds)result.properties[id]["m_Color.a"]=id==info_.profileHighlightId?1:0;
    for(const auto &[id,kind]:std::array<std::pair<SourceId,double>,2>{{{info_.profileBackgroundId,1},{info_.profileHighlightId,2}}}) {
        if(id.empty())continue;
        auto &properties=result.properties[id];properties["desktop.profileArtwork"]=kind;
        for(std::size_t i=0;i<3;++i)properties[std::string("desktop.profileAccent.")+"rgb"[i]]=finiteUnit(fixture.accent[i]);
    }
    if(!info_.profileHighlightId.empty())result.normalMaterialNodes.push_back(info_.profileHighlightId);
    if(auto it=info_.profileBindings.find("levelSlider");it!=info_.profileBindings.end())result.properties[it->second]["m_FillAmount"]=std::clamp(fixture.permissionLevel,1,60)/60.0;
    for(const auto &id:profileAccentIds_)for(std::size_t i=0;i<3;++i)result.properties[id][std::string("m_Color.")+"rgb"[i]]=finiteUnit(fixture.accent[i]);
    return result;
}
void DesktopShell::calibrate(Vec2 viewport) {
    if(centerUnitsPerPoint_>0&&calibrationViewport_.x==viewport.x&&calibrationViewport_.y==viewport.y)return;
    FrameInput input;input.viewport=viewport;input.playback={Phase::visible,document_.entranceDuration(),std::nullopt,std::nullopt,0};input.rootRotation=document_.initialRootRotation();
    auto neutral=document_.frame(input);auto node=neutral.node(info_.centerNodeId);
    if(!node)throw std::runtime_error("Desktop source center plane missing");
    auto origin=neutral.camera.project({0,0,0},node->world),step=neutral.camera.project({1,0,0},node->world);
    if(!origin||!step)throw std::runtime_error("Desktop source center calibration did not project");
    double pixels=std::hypot(step->x-origin->x,step->y-origin->y);
    if(!std::isfinite(pixels)||pixels<=0.0001)throw std::runtime_error("Desktop source center calibration collapsed");
    centerUnitsPerPoint_=std::max(0.1,std::min({viewport.x/1100,viewport.y/740,1.15}))/pixels;
    calibrationViewport_=viewport;
}
DesktopShellPresentation DesktopShell::decorate(Frame &frame,const DesktopShellFixture &fixture) {
    DesktopShellPresentation result;result.canvasAlpha=canvasAlpha(fixture);calibrate(frame.camera.viewport);
    std::map<SourceId,Graphic> previousReport;
    for(const auto &graphic:frame.graphics)if(graphic.componentId.starts_with("desktop.shell.report."))previousReport[graphic.nodeId]=graphic;
    // Native source text is suppressed by loadDesktop. Remove prior decoration
    // before replacing it, so camera-only updates cannot accumulate overlays.
    std::erase_if(frame.graphics,[](const Graphic &graphic){return graphic.componentId.starts_with("desktop.shell.")||graphic.kind=="UIText";});
    constexpr std::array<DesktopModule,4> left{DesktopModule::system,DesktopModule::display,DesktopModule::hotkeys,DesktopModule::about};
    std::size_t leftIndex=0;
    for(const auto &button:buttons_) {
        std::optional<DesktopModule> module;
        if(button.path.find("/RightBottomNode/")!=std::string::npos) {
            auto it=frame.desktopRightAssignments.find(button.id);
            if(it!=frame.desktopRightAssignments.end()&&it->second<rightOrder.size())module=rightOrder[it->second];
        } else if(suffix(button.path,"/TechtreeBtn"))module=DesktopModule::storage;
        else if(suffix(button.path,"/ReportBtn"))module=DesktopModule::activityMonitor;
        else if(leftIndex<left.size())module=left[leftIndex++];
        if(!module)continue;
        auto buttonNode=frame.node(button.id);if(!buttonNode||!buttonNode->active)continue;
        result.bindings.push_back({button.id,button.captionNodeId,button.iconNodeId,button.path,*module,title(*module,fixture.language),caption(*module,fixture.language),false});
        bool bottom=group(*module)==DesktopGroup::bottom,right=button.path.find("/RightBottomNode/")!=std::string::npos;
        bool selected=fixture.selectedModule==module;
        if(auto node=frame.node(button.captionNodeId);node&&node->rect&&node->active) {
            Rect rect=*node->rect;
            if(*module==DesktopModule::fileShelf&&!chinese(fixture.language)&&fixture.language!=DesktopLanguage::english) {
                auto old=rect.size;rect.size={std::max(old.x,124.0),std::max(old.y,56.0)};
                rect.origin.x+=(old.x-rect.size.x)/2;rect.origin.y+=(old.y-rect.size.y)/2;
            }
            auto plane=nativePlane(*node,rect,rect.size);double size=bottom?20:right?22:26;
            if(*module==DesktopModule::fileShelf&&chinese(fixture.language))size=std::min(size,20.0);
            std::array<double,4> ink=bottom&&fixture.dark?std::array<double,4>{1,1,1,1}:std::array<double,4>{.12,.12,.12,1};
            if(selected&&!bottom)for(std::size_t i=0;i<3;++i)ink[i]=finiteUnit(fixture.accent[i])*0.5;
            text(frame,plane,"caption."+button.id,caption(*module,fixture.language),{{0,0},rect.size},size,ink,1,!(*module==DesktopModule::fileShelf&&chinese(fixture.language)),selected?700:500,true);
            frame.graphics.back().sampledProperties["desktop.textTruncate"]=0;
        }
        auto iconNode=frame.node(button.iconNodeId);
        if(!iconNode||!iconNode->rect)continue;
        // Report is a special case in the Mac shell: the exact authored glyph
        // is retained, white tinted, with its full original 84px padded canvas.
        if(*module==DesktopModule::activityMonitor) {
            std::optional<Graphic> report;
            for(const auto &graphic:frame.graphics)if(graphic.nodeId==button.iconNodeId)report=graphic;
            if(!report)if(auto it=previousReport.find(button.iconNodeId);it!=previousReport.end())report=it->second;
            const auto shadowPrefix=button.path+"/IconShadow";
            std::erase_if(frame.graphics,[&](const Graphic &graphic){return graphic.path==shadowPrefix||graphic.path.starts_with(shadowPrefix+"/");});
            if(report) {
                report->componentId="desktop.shell.report."+button.id;
                report->kind="DesktopSourceIcon";report->materialId.clear();
                report->color[0]=report->color[1]=report->color[2]=1;report->vertexColorReady=true;
                frame.graphics.push_back(std::move(*report));
            }
            continue;
        }
        std::erase_if(frame.graphics,[&](const Graphic &graphic){return graphic.nodeId==button.iconNodeId;});
        Rect rect=*iconNode->rect;
        if(right){auto old=rect.size;rect.size={80,80};rect.origin.x+=(old.x-80)/2;rect.origin.y+=(old.y-80)/2;}
        auto plane=nativePlane(*iconNode,rect,{32,32});
        auto ink=bottom?std::array<double,4>{1,1,1,1}:std::array<double,4>{.12,.12,.12,1};
        double artworkScale=*module==DesktopModule::workMode||*module==DesktopModule::storage?0.9:1;
        auto image=approvedIcon(*module);
        if(!image.empty()) {
            double margin=(32-26*artworkScale)/2;
            auto graphic=overlay(plane,"icon."+button.id,plane.rect({{margin,margin},{26*artworkScale,26*artworkScale}}),ink);
            graphic.kind="DesktopIcon";graphic.textureId="desktop.icon."+image.stem().string();graphic.texturePath=image;
            // WIC PNG rows have top-left origin, unlike the source Unity mip data.
            graphic.uvQuads={{{{0,1},{0,0},{1,0},{1,1}}}};
            graphic.sampledProperties={{"desktop.iconAlphaCrop",8},{"desktop.iconTint",1}};
            frame.graphics.push_back(std::move(graphic));
        } else {
            auto graphic=overlay(plane,"icon."+button.id,rect,ink);graphic.kind="DesktopVector";graphic.quads.clear();graphic.uvQuads.clear();
            for(auto q:silhouette(*module)) {for(auto &v:q){v.x=rect.origin.x+v.x/32*rect.size.x;v.y=rect.origin.y+(32-v.y)/32*rect.size.y;}
                // silhouette quads were top-left +down; reversing Y restores
                // the Graphic BL/TL/TR/BR ordering used by native submission.
                graphic.quads.push_back(q);graphic.uvQuads.push_back({{{0,0},{0,1},{1,1},{1,0}}});}
            frame.graphics.push_back(std::move(graphic));
        }
    }
    for(const auto &id:info_.profileButtonIds)if(auto node=frame.node(id);node&&node->active)
        result.bindings.push_back({id,{}, {},node->path,DesktopModule::profile,title(DesktopModule::profile,fixture.language),{},false});
    struct ProfileText {const char *key;double size;int alignment;};
    const ProfileText profileTexts[]={{"managerName",22,0},{"managerNumber",16,0},{"managerLevel",33.45,0},{"managerLevelLabel",14,2},{"progressTxt",14,2}};
    constexpr std::array<const char *,5> authority{"Authority","权限等级","權限等級","権限レベル","권한"},maximum{"MAX","满级","滿級","最大","최대"};
    auto languageIndex=static_cast<std::size_t>(fixture.language);(void)title(DesktopModule::profile,fixture.language);
    for(const auto &binding:profileTexts) {
        auto found=info_.profileBindings.find(binding.key);if(found==info_.profileBindings.end())continue;
        auto node=frame.node(found->second);if(!node||!node->rect||!node->active)continue;
        std::string value;
        if(std::string_view(binding.key)=="managerName")value=fixture.profileName;
        else if(std::string_view(binding.key)=="managerNumber")value="UID: "+(fixture.profileUID.empty()?std::string("—"):fixture.profileUID);
        else if(std::string_view(binding.key)=="managerLevel")value=std::to_string(std::clamp(fixture.permissionLevel,1,60));
        else if(std::string_view(binding.key)=="managerLevelLabel")value=authority[languageIndex];
        else if(fixture.permissionLevel>=60)value=maximum[languageIndex];
        auto ink=std::array<double,4>{1,1,1,1};if(std::string_view(binding.key)=="progressTxt")for(std::size_t i=0;i<3;++i)ink[i]=finiteUnit(fixture.accent[i]);
        auto plane=nativePlane(*node,*node->rect,node->rect->size);
        text(frame,plane,"profile."+std::string(binding.key),value,{{0,0},node->rect->size},binding.size,ink,binding.alignment,false,500,true);
    }
    if(auto it=info_.profileBindings.find("playerHead");it!=info_.profileBindings.end()) {
        auto node=frame.node(it->second);
        if(node&&node->rect&&node->active) {
            // Mac desktop always overrides the game portrait. This exact native
            // placeholder is HUDPortraitArtwork.makeLayer(image:nil), rendered
            // in the original PlayerHead plane without removing its hit region.
            std::erase_if(frame.graphics,[&](const Graphic &g){return g.nodeId==node->id;});
            auto plane=nativePlane(*node,*node->rect,{136,136});
            Paths plate;plate.box(0,0,136,136);vector(frame,plane,"profile.portraitPlate",plate,{.22,.22,.22,.85});
            Paths portrait;portrait.ellipse(136*.36,136*.20,136*.30,136*.31);portrait.portraitBody(136);
            vector(frame,plane,"profile.portraitSilhouette",portrait,{1,1,1,.63});
        }
    }
    if(auto node=frame.node(info_.centerNodeId)) {
        double u=centerUnitsPerPoint_;
        auto plane=nativePlane(*node,{{-500*u,-320*u},{1000*u,640*u}},{1000,640});
        // Mac native content inherits the source plane's homography, not that
        // source node's graphic alpha; canvas has its own authored finite fade.
        plane.alpha=result.canvasAlpha;result.centerPlane=plane;
        double primary=fixture.dark?.95:.13,muted=fixture.dark?.55:.40;
        text(frame,plane,"header.title","ENDFIELDHUD",{{270,2},{300,27}},20,{primary,primary,primary,1},0,false,600);
        frame.graphics.back().sampledProperties["desktop.fontFamily"]=1;
        text(frame,plane,"header.subtitle","SYSTEM INTERFACE",{{270,33},{300,18}},9,{muted,muted,muted,1},0,false,400);
        frame.graphics.back().sampledProperties["desktop.fontFamily"]=1;
    }
    if(auto node=frame.node(info_.statusNodeId);node&&node->rect) {
        // Banner artwork is intentionally hidden. Its transform still anchors
        // the native 528.28x122 status plane; the clock is inset86.28 and down8.
        auto plane=nativePlane(*node,*node->rect,{528.28,122});
        plane.alpha=result.canvasAlpha;result.statusPlane=plane;
        Paths platePath;platePath.rounded(93.28,15,326,108,12);vector(frame,plane,"clock.plate",platePath,{.055,.055,.055,.78});
        vector(frame,plane,"clock.innerStroke",roundedStroke({{93.28,15},{326,108}},12,1),{.70,.70,.70,.35});
        std::array<double,4> accent{finiteUnit(fixture.accent[0]),finiteUnit(fixture.accent[1]),finiteUnit(fixture.accent[2]),.70};
        vector(frame,plane,"clock.outerStroke",roundedStroke({{87.08,8.8},{338.4,120.4}},17,1.5),accent);
        Paths markers;for(int index=0;index<5;++index)markers.box(100.28+index*64,137.5,49,3);
        vector(frame,plane,"clock.indicators",markers,{.72,.72,.72,.55});
        Paths selection;selection.box(100.28,137.5,49,3);accent[3]=1;vector(frame,plane,"clock.selectedDigital",selection,accent);
        text(frame,plane,"clock.time",fixture.clockTime,{{108.28,28},{296,39}},32,{.96,.96,.96,1},2,false,600);
        frame.graphics.back().sampledProperties["desktop.monospacedDigits"]=1;
        text(frame,plane,"clock.date",fixture.clockDate,{{108.28,69},{296,20}},13,{.64,.64,.64,1},2,false,400);
        frame.graphics.back().sampledProperties["desktop.fontFamily"]=1;
    }
    double designScale=std::max(0.1,std::min({frame.camera.viewport.x/1100,frame.camera.viewport.y/740,1.15}))*fixture.hudScale;
    auto cameraInverse=inverse(frame.camera.viewProjection),rootInverse=inverse(frame.worldRoot);
    if(cameraInverse&&rootInverse&&std::isfinite(designScale)&&designScale>0) {
        Vec2 origin{(frame.camera.viewport.x-1000*designScale)/2+frame.camera.viewport.x*fixture.hudOffset.x,
            (frame.camera.viewport.y-640*designScale)/2-30*designScale+frame.camera.viewport.y*fixture.hudOffset.y};
        Mat4 screen=Mat4::identity();screen.values[0]=2*designScale/frame.camera.viewport.x;screen.values[5]=2*designScale/frame.camera.viewport.y;
        screen.values[12]=2*origin.x/frame.camera.viewport.x-1;screen.values[13]=1-2*origin.y/frame.camera.viewport.y;screen.values[14]=0.5;
        DesktopNativePlane plane{"desktop.footer",{{0,-640},{1000,640}},{1000,640},*cameraInverse*screen,{},result.canvasAlpha,10000,{}};
        plane.sceneWorld=*rootInverse*plane.world;result.footerPlane=plane;
        constexpr std::array<const char *,5> hints{"CLICK OUTSIDE TO CLOSE","点击外侧关闭","點擊外側關閉","外側をクリックして閉じる","바깥쪽을 클릭하여 닫기"};
        auto shortcut=fixture.summonShortcut;std::transform(shortcut.begin(),shortcut.end(),shortcut.begin(),[](unsigned char c){return char(std::toupper(c));});
        double muted=fixture.dark?.55:.40;
        text(frame,plane,"footer.hint","ESC / "+shortcut+" / "+hints[languageIndex],{{275,622},{450,18}},9,{muted,muted,muted,1},1,false,400);
        frame.graphics.back().sampledProperties["desktop.fontFamily"]=1;
    }
    result.sourceLogoRetained=std::any_of(frame.graphics.begin(),frame.graphics.end(),[](const Graphic &g){return suffix(g.path,"/EndfieldText");});
    return result;
}
} // namespace ehud::scene
