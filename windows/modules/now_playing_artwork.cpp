#include "modules/now_playing_artwork.hpp"
#include <algorithm>
namespace endfield::modules {
namespace {
using J=ehud::data::Json;using R=core::Rect;using P=core::Point;using C=NowPlayingColor;using Role=NowPlayingSurfaceRole;
C gray(double v,double a=1){return {v,v,v,a};}C alpha(C c,double a){c[3]=a;return c;}
J color(C c){return J::Object{{"sRGB",J::Array{c[0],c[1],c[2],c[3]}}};}
J node(R r,const char*kind="layer"){return J::Object{{"kind",kind},{"bounds",J::Array{0,0,r.width,r.height}},{"position",J::Array{r.x,r.y}},{"anchorPoint",J::Array{0,0}},{"opacity",1},{"children",J::Array{}}};}
J cmd(const char*op,std::initializer_list<P>points={}){J::Array p;for(auto q:points)p.push_back(J::Array{q.x,q.y});return J::Object{{"op",op},{"points",std::move(p)}};}
void rectangle(J::Array&out,R r){out.push_back(cmd("move",{{r.x,r.y}}));out.push_back(cmd("line",{{r.x+r.width,r.y}}));out.push_back(cmd("line",{{r.x+r.width,r.y+r.height}}));out.push_back(cmd("line",{{r.x,r.y+r.height}}));out.push_back(cmd("close"));}
J::Array rounded(double w,double h,double radius){const double r=std::min({radius,w/2,h/2}),k=r*.5522847498;return {cmd("move",{{w,h/2}}),cmd("line",{{w,h-r}}),cmd("cubic",{{w,h-r+k},{w-r+k,h},{w-r,h}}),cmd("line",{{r,h}}),cmd("cubic",{{r-k,h},{0,h-r+k},{0,h-r}}),cmd("line",{{0,r}}),cmd("cubic",{{0,r-k},{r-k,0},{r,0}}),cmd("line",{{w-r,0}}),cmd("cubic",{{w-r+k,0},{w,r-k},{w,r}}),cmd("close")};}
J::Array ellipse(double w,double h){const double x=w/2,y=h/2,kx=x*.5522847498,ky=y*.5522847498;return {cmd("move",{{w,y}}),cmd("cubic",{{w,y+ky},{x+kx,h},{x,h}}),cmd("cubic",{{x-kx,h},{0,y+ky},{0,y}}),cmd("cubic",{{0,y-ky},{x-kx,0},{x,0}}),cmd("cubic",{{x+kx,0},{w,y-ky},{w,y}}),cmd("close")};}
J shape(R r,J::Array path,std::optional<C>fill,std::optional<C>stroke={},double width=1){auto n=node(r,"shape");n["shape"]=J::Object{{"path",std::move(path)},{"fillColor",fill?color(*fill):J{}},{"strokeColor",stroke?color(*stroke):J{}},{"lineWidth",width},{"lineCap","butt"},{"lineJoin","miter"},{"fillRule","non-zero"}};return n;}
J plate(R r,C fill,double radius=0,std::optional<C>border={},double width=0){auto n=node(r);n["backgroundColor"]=color(fill);n["cornerRadius"]=radius;n["borderWidth"]=width;if(border)n["borderColor"]=color(*border);return n;}
J text(std::string s,R r,double size,C ink,const char*alignment="natural",bool semibold=false){auto n=node(r,"text");n["text"]=J::Object{{"string",std::move(s)},{"fontSize",size},{"font",J::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",semibold?".AppleSystemUIFontDemi":".AppleSystemUIFont"},{"pointSize",size},{"symbolicTraits",semibold?2:0}}},{"foregroundColor",color(ink)},{"alignment",alignment},{"wrapped",false},{"truncation","end"}};return n;}
void add(std::vector<NowPlayingSurface>&out,std::string id,J n,Role role=Role::artwork,float opacity=1,std::optional<NowPlayingAction>action={},unsigned row=0){const auto&p=n["position"].array();auto local=core::Matrix4::translation(p[0].number(),p[1].number());n["position"]=J::Array{0,0};n["id"]=id;out.push_back({std::move(id),std::move(n),local,opacity,role,action,row});}
J::Array glyph(NowPlayingAction action,bool playing,double size){J::Array p;const double c=size/2;
    if(action==NowPlayingAction::lyrics){for(unsigned row=0;row<3;++row)rectangle(p,{7,8+6.*row,row==1?16.:11.,2});}
    else if(action==NowPlayingAction::volume){
        // Exact original CGPath(strokingWithWidth:1.5, round, round) expansion.
        // Independent unchanged-Swift fixture checks every command/control point.
        p={cmd("move",{{6,11}}),cmd("line",{{11,11}}),cmd("line",{{17,6}}),cmd("line",{{17,24}}),cmd("line",{{11,19}}),cmd("line",{{6,19}}),cmd("close"),
          cmd("move",{{20.125,9.58734122634726}}),cmd("cubic",{{23.114328896539583,11.313231069460713},{24.138548616766194,15.13567110346042},{22.41265877365274,18.125}}),
          cmd("cubic",{{21.864106700371927,19.07512006151962},{21.07512006151962,19.864106700371924},{20.125,20.41265877365274}}),
          cmd("cubic",{{19.766280532392848,20.619765554839287},{19.643374165975125,21.078458358883918},{19.85048094716167,21.43717782649107}}),
          cmd("cubic",{{20.057587728348217,21.79589729409822},{20.516280532392848,21.918803660515945},{20.875,21.7116968793294}}),
          cmd("cubic",{{22.05314887628434,21.031492308461182},{23.031492308461186,20.053148876284336},{23.7116968793294,18.875}}),
          cmd("cubic",{{25.851800284815944,15.168232168246128},{24.581767831753872,10.428406526157145},{20.875,8.288303120670601}}),
          cmd("cubic",{{20.516280532392848,8.081196339484054},{20.057587728348217,8.20410270590178},{19.85048094716167,8.56282217350893}}),
          cmd("cubic",{{19.643374165975125,8.92154164111608},{19.766280532392848,9.380234445160712},{20.125,9.58734122634726}}),cmd("close")};
    }else if(action==NowPlayingAction::playPause&&playing){rectangle(p,{c-6,c-8,4,16});rectangle(p,{c+2,c-8,4,16});}
    else{const double d=action==NowPlayingAction::previous?-1:1;p={cmd("move",{{c-d*5,c-7}}),cmd("line",{{c+d*7,c}}),cmd("line",{{c-d*5,c+7}}),cmd("close")};if(action!=NowPlayingAction::playPause)rectangle(p,{c+d*8-1,c-7,2,14});}return p;
}
}
NowPlayingArtwork prepareNowPlayingArtwork(const NowPlayingPresentation&s){NowPlayingArtwork out;const auto&a=s.appearance();const auto&i=s.input();const auto primary=gray(a.dark?.94:.12),muted=gray(a.dark?.60:.40);const bool hasTrack=bool(i.track);
    add(out.panel,"nowPlaying.placeholder",shape(NowPlayingGeometry::placeholder,ellipse(60,60),{},alpha(muted,.32),2),Role::placeholder,i.coverAvailable?0:1);
    auto shade=node(NowPlayingGeometry::shade,"gradient");shade["gradient"]=J::Object{{"type","axial"},{"colors",J::Array{color(gray(0,0)),color(gray(0,.76)),color(gray(0,.90))}},{"locations",J::Array{0,.30,1}},{"startPoint",J::Array{.5,0}},{"endPoint",J::Array{.5,1}}};add(out.panel,"nowPlaying.metadataShade",std::move(shade),Role::shade,hasTrack?1:0);
    add(out.panel,"nowPlaying.title",text(hasTrack?i.track->title:"",NowPlayingGeometry::title,14,gray(1),"natural",true),Role::title);
    add(out.panel,"nowPlaying.artist",text(hasTrack?i.track->artist:"",NowPlayingGeometry::artist,10,gray(1,.8)),Role::artist);
    add(out.panel,"nowPlaying.empty",text("No music playing",NowPlayingGeometry::empty,13,primary,"center"),Role::empty,hasTrack?0:1);
    for(unsigned k=0;k<3;++k)add(out.lyrics,"nowPlaying.lyric."+std::to_string(k),text(s.lyricRows()[k],{55,331+16.*k,330,16},10,primary,"center"),Role::lyric,k==1?1:.36f,{},k);
    add(out.controls,"nowPlaying.progress.rail",plate({55,387,330,2},alpha(primary,.2)));
    add(out.controls,"nowPlaying.progress.fill",plate({55,387,1,2},a.accent),Role::progressFill);
    add(out.controls,"nowPlaying.progress.handle",plate({0,384,5,8},primary),Role::progressHandle,hasTrack&&i.track->duration?1:0);
    add(out.controls,"nowPlaying.elapsed",text(s.elapsedText(),NowPlayingGeometry::elapsed,9,muted));add(out.controls,"nowPlaying.duration",text(s.durationText(),NowPlayingGeometry::duration,9,muted,"right"));
    for(const auto&action:s.actions()){const auto r=action.rect;const auto id="nowPlaying.control."+std::string(nowPlayingActionID(action.action));const auto opacity=action.enabled?1.f:.35f;
        add(out.controls,id+".face",plate(r,action.selected?alpha(a.accent,.16):gray(a.dark?.96:.10,.07),action.action==NowPlayingAction::playPause?r.width/2:3),Role::artwork,opacity,action.action);
        add(out.controls,id+".glyph",shape(r,glyph(action.action,hasTrack&&i.track->isPlaying,r.width),primary),Role::artwork,opacity,action.action);
        auto path=action.action==NowPlayingAction::playPause?ellipse(r.width,r.height):rounded(r.width,r.height,3);
        add(out.controls,id+".tint",shape(r,path,alpha(a.accent,.30)),Role::tint,0,action.action);
        add(out.controls,id+".rim",shape(r,std::move(path),{},a.accent,.9),Role::rim,0,action.action);
    }
    const auto v=nowPlayingVolumeGeometry(i.volume.value_or(i.volumeAvailable?1:0),i.volumeAvailable);
    add(out.volume,"nowPlaying.volume.back",plate(v.back,gray(0,.3)));add(out.volume,"nowPlaying.volume.face",plate(v.face,gray(a.dark?.08:.92,.98),0,alpha(a.accent,.7),.7));
    add(out.volume,"nowPlaying.volume.rail",plate(v.rail,alpha(primary,.19)));
    add(out.volume,"nowPlaying.volume.fill",plate({382.5,0,2,1},alpha(a.accent,.85)),Role::volumeFill);
    add(out.volume,"nowPlaying.volume.handle",plate({379.5,0,8,7},a.accent,0,alpha(primary,.65),.6),Role::volumeHandle,static_cast<float>(v.handleOpacity));
    add(out.volume,"nowPlaying.volume.label",text(s.volumeText(),v.label,10,primary,"center"));return out;
}
}
