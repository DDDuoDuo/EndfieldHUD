#include "core/source_native_labels.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace endfield::core::source {
namespace {
void require(bool value,const char* reason){if(!value)throw std::invalid_argument(reason);}
bool finite(const SourceRect& r){return std::all_of(r.origin.begin(),r.origin.end(),[](double x){return std::isfinite(x);})&&std::all_of(r.size.begin(),r.size.end(),[](double x){return std::isfinite(x);});}
bool valid(const Rect& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.width)&&std::isfinite(v.height)&&v.width>0&&v.height>0;}
std::array<Point,4> viewportCorners(const Rect& v){return {{{v.x,v.y},{v.x+v.width,v.y},{v.x+v.width,v.y+v.height},{v.x,v.y+v.height}}};}
std::array<double,4> multiply(const Matrix4& m,std::array<double,4> p){std::array<double,4> out{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out[r]+=m.values[c*4+r]*p[c];return out;}
void clipInto(std::span<const Point> subject,std::span<const Point> clip,std::vector<Point>& a,std::vector<Point>& b){
    require(subject.size()<=64&&clip.size()>=3&&clip.size()<=64,"Invalid native clip polygon bound");
    a.assign(subject.begin(),subject.end());double area=0;
    for(std::size_t i=0;i<clip.size();++i)area+=clip[i].x*clip[(i+1)%clip.size()].y-clip[(i+1)%clip.size()].x*clip[i].y;
    const double orientation=area>=0?1:-1;
    for(std::size_t i=0;i<clip.size();++i){b.clear();if(a.empty())return;const auto p=clip[i],q=clip[(i+1)%clip.size()];
        auto distance=[&](Point x){return orientation*((q.x-p.x)*(x.y-p.y)-(q.y-p.y)*(x.x-p.x));};
        Point previous=a.back();double previousDistance=distance(previous);
        for(const auto current:a){const double d=distance(current);
            if((d>=0)!=(previousDistance>=0)){const double f=previousDistance/(previousDistance-d);b.push_back({previous.x+(current.x-previous.x)*f,previous.y+(current.y-previous.y)*f});}
            if(d>=0)b.push_back(current);previous=current;previousDistance=d;}
        require(b.size()<=64,"Native clip intersection exceeds its bound");a.swap(b);
    }
}
std::string text(const Json& j){require(j.isString()&&j.string().size()<=4096,"Invalid source native binding string");return j.string();}
const Json::Array& array(const Json& j,std::size_t limit){require(j.isArray()&&j.array().size()<=limit,"Invalid source native binding array");return j.array();}
bool bottomIconName(std::string_view name){const auto first=name.find_first_not_of(" \t\v\f");if(first==std::string_view::npos)return false;const auto last=name.find_last_not_of(" \t\v\f");return name.substr(first,last-first+1)=="Icon";}
}
std::optional<Point> projectNativePoint(Vec3 local,const Matrix4& world,const Matrix4& viewProjection,const Rect& viewport){
    if(!valid(viewport))return {};
    // Preserve the Mac's two matrix-vector products and its one Y conversion.
    const auto clip=multiply(viewProjection,multiply(world,{local[0],local[1],local[2],1}));
    if(!std::all_of(clip.begin(),clip.end(),[](double x){return std::isfinite(x);})||clip[3]<=0)return {};
    const double z=clip[2]/clip[3];if(z< -1e-10||z>1+1e-10)return {};
    return Point{viewport.x+(clip[0]/clip[3]+1)*viewport.width/2,viewport.y+(1-clip[1]/clip[3])*viewport.height/2};
}
Matrix4 nativeProjectiveTextTransform(const std::array<Point,4>& p,Vec2 size){
    require(size[0]>0&&size[1]>0&&std::isfinite(size[0])&&std::isfinite(size[1]),"Invalid native content size");
    const auto dx1=p[1].x-p[2].x,dx2=p[3].x-p[2].x,dy1=p[1].y-p[2].y,dy2=p[3].y-p[2].y;
    const auto sx=p[0].x-p[1].x+p[2].x-p[3].x,sy=p[0].y-p[1].y+p[2].y-p[3].y;
    const auto determinant=dx1*dy2-dx2*dy1;
    const double g=std::abs(determinant)>.000001?(sx*dy2-dx2*sy)/determinant:0;
    const double h=std::abs(determinant)>.000001?(dx1*sy-sx*dy1)/determinant:0;
    Matrix4 t;t.values[0]=(p[1].x-p[0].x+g*p[1].x)/size[0];t.values[1]=(p[1].y-p[0].y+g*p[1].y)/size[0];
    t.values[4]=(p[3].x-p[0].x+h*p[3].x)/size[1];t.values[5]=(p[3].y-p[0].y+h*p[3].y)/size[1];
    t.values[12]=p[0].x;t.values[13]=p[0].y;t.values[3]=g/size[0];t.values[7]=h/size[1];return t;
}
std::vector<Point> nativeClipPolygon(std::span<const Point> subject,std::span<const Point> clip){std::vector<Point>a,b;a.reserve(64);b.reserve(64);clipInto(subject,clip,a,b);return a;}

std::vector<NativeLabelBinding> nativeLabelBindingsFromExport(const SceneDefinition& scene,const Json& mountedButtons,const Json& nativeLayers,const Json& profileBindings){
    struct Button{std::string id,path;std::vector<std::string> labels;bool bottom{};};std::vector<Button> buttons;
    std::map<std::string,std::size_t,std::less<>> labelButtons;
    auto append=[&](Button button){require(scene.node(button.id)!=nullptr,"Native button node is absent from mounted scene");
        for(const auto& id:button.labels){require(scene.node(id)!=nullptr,"Native caption node is absent from mounted scene");require(labelButtons.emplace(id,buttons.size()).second,"Ambiguous native caption button");}buttons.push_back(std::move(button));};
    for(const auto& b:array(mountedButtons,1024)){Button item{text(b["node_id"]),text(b["path"]),{},false};for(const auto& label:array(b["labels"],64))item.labels.push_back(text(label["node_id"]));append(std::move(item));}
    for(const std::string name:{"TechtreeBtn","ReportBtn"}){
        const Node* button=nullptr;for(const auto& n:scene.nodes())if(n.name==name){button=&n;break;}if(!button)continue;
        const Node* label=nullptr;for(const auto& n:scene.nodes())if(n.path.starts_with(button->path+"/")&&n.name=="BtnName"){label=&n;break;}
        require(label!=nullptr,"Missing supplemental native caption");append({button->id,button->path,{label->id},true});
    }
    std::map<std::string,std::size_t,std::less<>> iconButtons;
    for(std::size_t i=0;i<buttons.size();++i){const auto& b=buttons[i];const Node* icon=nullptr;
        for(const auto& n:scene.nodes())if(n.path.starts_with(b.path+"/")&&(b.bottom?(n.path.find("/IconShadow/")!=std::string::npos&&bottomIconName(n.name)):(n.name=="Icon"||n.name=="Icon01"))){icon=&n;break;}
        if(icon)require(iconButtons.emplace(icon->id,i).second,"Ambiguous source icon button");
    }
    std::optional<std::string> profileRoot;
    if(profileBindings.isObject()&&!profileBindings.object().empty()){
        for(const auto& [name,value]:profileBindings.object()){
            (void)name;const Node* n=scene.node(text(value));require(n!=nullptr,"Profile caption node is absent from mounted scene");
            while(n&&n->name!="PlayerInfo")n=n->parent?scene.node(*n->parent):nullptr;
            require(n&&n->parent,"Missing mounted PlayerInfo profile ancestor");
            if(profileRoot)require(*profileRoot==*n->parent,"Profile captions disagree on their mounted root");else profileRoot=n->parent;
        }
    }
    std::vector<NativeLabelBinding> result;std::set<std::string> surfaces;
    for(const auto& container:array(nativeLayers["children"],1024)){
        const auto name=text(container["name"]);NativeLabelBinding binding;
        if(name.starts_with("desktop.watch.label.")){
            binding.sourceNodeID=name.substr(std::string_view("desktop.watch.label.").size());const auto it=labelButtons.find(binding.sourceNodeID);require(it!=labelButtons.end(),"Exported caption has no authored button binding");
            binding.buttonID=buttons[it->second].id;
        }else if(name.starts_with("desktop.watch.icon.")){
            binding.kind=NativeLabelKind::icon;binding.sourceNodeID=name.substr(std::string_view("desktop.watch.icon.").size());
            const auto it=iconButtons.find(binding.sourceNodeID);require(it!=iconButtons.end(),"Exported icon differs from the Mac's authored first-node selection");
            const auto& button=buttons[it->second];binding.buttonID=button.id;binding.rightButton=button.path.find("/RightBottomNode/")!=std::string::npos;
        }else if(name.starts_with("desktop.profile.")){
            require(profileRoot.has_value()&&profileBindings.contains(name),"Exported profile caption has no explicit binding");
            binding.sourceNodeID=text(profileBindings[name]);binding.buttonID=*profileRoot;binding.profileViewportClip=true;
        }else throw std::invalid_argument("Unknown exported native caption/icon container: "+name);
        const auto& children=array(container["children"],4);require(children.size()==1,"Native caption/icon container needs exactly one content root");
        binding.surfaceID=text(children.front()["id"]);require(surfaces.insert(binding.surfaceID).second,"Duplicate native surface ID");
        require(scene.node(binding.sourceNodeID)!=nullptr,"Native source node is missing");result.push_back(std::move(binding));
    }
    require(result.size()<=NativeLabelPlan::maximumBindings,"Native label binding count exceeds its bound");return result;
}

NativeLabelPlan::NativeLabelPlan(const SceneDefinition& scene,std::vector<NativeLabelBinding> bindings):scene_(&scene),bindings_(std::move(bindings)){
    require(bindings_.size()<=maximumBindings,"Native label plan exceeds its bound");std::map<std::string,std::size_t,std::less<>> indices,buttons;std::set<std::string> surfaces;std::set<std::size_t> relevant;
    for(std::size_t i=0;i<scene.nodes().size();++i)indices.emplace(scene.nodes()[i].id,i);
    for(const auto& b:bindings_){require(!b.surfaceID.empty()&&surfaces.insert(b.surfaceID).second,"Invalid or duplicate native surface identity");
        const auto node=indices.find(b.sourceNodeID),button=indices.find(b.buttonID);require(node!=indices.end()&&button!=indices.end(),"Native binding references a missing source node");
        auto [entry,inserted]=buttons.emplace(b.buttonID,buttonIDs_.size());if(inserted)buttonIDs_.push_back(b.buttonID);
        compiled_.push_back({node->second,button->second,entry->second});relevant.insert(node->second);relevant.insert(button->second);
        NativeLabelPlacement output;output.surfaceID=b.surfaceID;output.masks.reserve(8);output.clippedPolygon.reserve(64);placements_.push_back(std::move(output));
    }
    relevantNodes_.assign(relevant.begin(),relevant.end());previousNodes_.resize(relevantNodes_.size());previousButtons_.resize(buttonIDs_.size());clips_.resize(buttonIDs_.size());
    for(auto& b:previousButtons_)b.masks.reserve(maximumButtonMasks);for(auto& c:clips_){c.masks.reserve(8);c.polygon.reserve(64);}
    staged_=placements_;for(auto& p:staged_){p.masks.reserve(8);p.clippedPolygon.reserve(64);}scratchA_.reserve(64);scratchB_.reserve(64);
}
bool NativeLabelPlan::update(std::span<const ResolvedNode> nodes,std::span<const double> alpha,std::span<const NativeLabelButtonState> buttons,const CameraFrame& camera,const Rect& viewport){
    require(nodes.size()==scene_->nodes().size()&&(alpha.empty()||alpha.size()==nodes.size())&&buttons.size()==buttonIDs_.size(),"Native projection input order/length mismatch");
    require(camera.view.finite()&&camera.projection.finite()&&camera.worldRoot.finite()&&valid(viewport),"Invalid native projection camera/viewport");
    const auto viewProjection=camera.projection*camera.view;require(viewProjection.finite(),"Invalid native view projection");
    auto snapshot=[&](std::size_t i){return NodeSnapshot{nodes[i].worldMatrix,nodes[i].rect,nodes[i].activeInHierarchy,alpha.empty()?1:alpha[i]};};
    bool same=initialized_&&viewProjection==previousViewProjection_&&camera.worldRoot==previousWorldRoot_&&viewport==previousViewport_;
    for(std::size_t i=0;i<relevantNodes_.size();++i){const auto index=relevantNodes_[i];require(nodes[index].node==&scene_->nodes()[index],"Native projection nodes changed source ordering");const auto s=snapshot(index);
        require(s.world.finite()&&(!s.rect||finite(*s.rect))&&std::isfinite(s.alpha)&&s.alpha>=0&&s.alpha<=1,"Invalid native source node state");same=same&&(s==previousNodes_[i]);}
    for(std::size_t i=0;i<buttons.size();++i){const auto& b=buttons[i];const auto& old=previousButtons_[i];require(b.masks.size()<=maximumButtonMasks,"Native button mask count exceeds eight GPU planes including viewport");
        for(const auto& m:b.masks)require(m.world.finite()&&finite(m.rect),"Invalid native button mask state");
        same=same&&b.actionBound==old.actionBound&&b.hasHit==old.hasHit&&b.expandFileShelfCaption==old.expanded&&std::equal(b.masks.begin(),b.masks.end(),old.masks.begin(),old.masks.end());}
    ++stats_.updates;if(same){++stats_.unchangedUpdates;return false;}
    const auto viewportPoints=viewportCorners(viewport);
    std::uint64_t projected=0,clipped=0,changed=0,boundsChanged=0;
    for(std::size_t i=0;i<buttons.size();++i){auto& out=clips_[i];out.polygon.clear();out.masks.clear();if(!buttons[i].hasHit)continue;
        ++clipped;out.polygon.assign(viewportPoints.begin(),viewportPoints.end());out.masks.push_back({{},viewport});
        for(const auto& mask:buttons[i].masks){std::array<Point,4> points;bool validMask=true;const auto corners=mask.rect.corners();
            for(unsigned c=0;c<4;++c){const auto p=projectNativePoint(corners[c],mask.world,viewProjection,viewport);if(!p){validMask=false;break;}points[c]=*p;}
            if(!validMask){out.polygon.clear();out.masks.clear();break;}
            clipInto(out.polygon,points,scratchA_,scratchB_);out.polygon.assign(scratchA_.begin(),scratchA_.end());
            if(out.polygon.empty()){out.masks.clear();break;}
            try{out.masks.push_back({inverseSourceMatrix(nativeProjectiveTextTransform(points,{1,1})),{0,0,1,1}});}
            catch(const std::invalid_argument&){out.polygon.clear();out.masks.clear();break;}
        }
    }
    for(std::size_t i=0;i<bindings_.size();++i){const auto& b=bindings_[i];const auto& c=compiled_[i];const auto& n=nodes[c.node];const auto& button=buttons[c.button];
        auto& out=staged_[i];const auto& old=placements_[i];out.world=old.world;out.contentBounds=old.contentBounds;out.visible=false;out.opacity=0;out.masks.clear();out.clippedPolygon.clear();
        const bool active=b.kind==NativeLabelKind::caption?n.activeInHierarchy:nodes[c.buttonNode].activeInHierarchy;
        if(active&&n.rect&&button.actionBound){const auto& r=*n.rect;Vec2 size=r.size;
            if(b.kind==NativeLabelKind::caption&&button.expandFileShelfCaption){size[0]=std::max(size[0],124.);size[1]=std::max(size[1],56.);}
            if(b.kind==NativeLabelKind::icon&&b.rightButton)size={80,80};
            if(size[0]>0&&size[1]>0){const double x=r.origin[0]+(r.size[0]-size[0])/2,y=r.origin[1]+(r.size[1]-size[1])/2;
                const std::array<Vec3,4> local{{{x,y+size[1],0},{x+size[0],y+size[1],0},{x+size[0],y,0},{x,y,0}}};
                const auto world=camera.worldRoot*n.worldMatrix;std::array<Point,4> points;bool validPoints=true;++projected;
                for(unsigned k=0;k<4;++k){const auto p=projectNativePoint(local[k],world,viewProjection,viewport);if(!p){validPoints=false;break;}points[k]=*p;}
                const auto& clip=clips_[c.button];
                if(validPoints&&(b.profileViewportClip||!clip.polygon.empty())){
                    const Vec2 contentSize=b.kind==NativeLabelKind::icon?Vec2{32,32}:size;
                    out.world=nativeProjectiveTextTransform(points,contentSize);out.contentBounds={0,0,contentSize[0],contentSize[1]};out.visible=out.world.finite();
                    if(out.visible){out.opacity=static_cast<float>(alpha.empty()?1:alpha[c.node]);
                        if(b.profileViewportClip){out.masks.push_back({{},viewport});out.clippedPolygon.assign(viewportPoints.begin(),viewportPoints.end());}
                        else{out.masks.assign(clip.masks.begin(),clip.masks.end());out.clippedPolygon.assign(clip.polygon.begin(),clip.polygon.end());}}
                }
            }
        }
        if(out.world!=old.world||out.contentBounds!=old.contentBounds||out.visible!=old.visible||out.opacity!=old.opacity||out.masks!=old.masks||out.clippedPolygon!=old.clippedPolygon)++changed;
        if(out.contentBounds!=old.contentBounds)++boundsChanged;
    }
    placements_.swap(staged_);for(std::size_t i=0;i<relevantNodes_.size();++i)previousNodes_[i]=snapshot(relevantNodes_[i]);
    for(std::size_t i=0;i<buttons.size();++i){const auto& b=buttons[i];auto& old=previousButtons_[i];old.actionBound=b.actionBound;old.hasHit=b.hasHit;old.expanded=b.expandFileShelfCaption;old.masks.assign(b.masks.begin(),b.masks.end());}
    previousViewProjection_=viewProjection;previousWorldRoot_=camera.worldRoot;previousViewport_=viewport;initialized_=true;
    stats_.projectionComputations+=projected;stats_.clipComputations+=clipped;stats_.changedPlacements+=changed;stats_.contentBoundsChanges+=boundsChanged;return changed!=0;
}
} // namespace endfield::core::source
