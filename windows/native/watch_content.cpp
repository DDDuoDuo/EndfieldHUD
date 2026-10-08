#include "native/watch_content.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
using namespace core::source;
namespace {
void need(bool value,const char*reason){if(!value)throw std::invalid_argument(reason);}
std::string text(const Json&value){need(value.isString()&&value.string().size()<=4096,"Invalid source navigation string");return value.string();}
const Json::Array& array(const Json&value,std::size_t maximum){need(value.isArray()&&value.array().size()<=maximum,"Invalid source navigation array");return value.array();}
bool bottom(std::string_view target){return target=="storage"||target=="activityMonitor";} // HUDModule.group
Json identity(){return Json::Array{Json::Array{1,0,0,0},Json::Array{0,1,0,0},Json::Array{0,0,1,0},Json::Array{0,0,0,1}};}
void reidentify(Json&node,const std::string&id,unsigned depth,std::size_t&count){
    need(depth<=64&&++count<=4096&&node.isObject(),"Source local content exceeds tree limits");node["id"]=id;
    auto children=node["children"].array();for(std::size_t i=0;i<children.size();++i)reidentify(children[i],id+"/"+std::to_string(i),depth+1,count);node["children"]=std::move(children);
    if(!node["mask"].isNull())reidentify(node["mask"],id+"/mask",depth+1,count);
}
Json localTree(const Json&source,const std::string&id){
    Json result=source;std::size_t count{};reidentify(result,id,0,count);
    need(result["hidden"].isBool()&&!result["hidden"].boolean(),"Source local content root is hidden");
    const auto&bounds=array(result["bounds"],4);need(bounds.size()==4&&bounds[2].number()>0&&bounds[3].number()>0,"Source local content has no positive bounds");
    // Only this outer plane's projective placement is handled by NativeLabelPlan.
    // Descendant artwork transforms, masks, colors, font metrics and raster
    // descriptors remain exact original source data.
    result["transform"]=identity();result["position"]=Json::Array{0,0};result["anchorPoint"]=Json::Array{0,0};
    result["anchorPointZ"]=0;result["zPosition"]=0;result["frame"]=result["bounds"];
    return result;
}
WatchContentCatalog::Actions sampleActions(const WatchContentNavigation&navigation,const DesktopNavigationLayout&layout,double position){
    auto result=navigation.fixedActions;for(const auto&[id,index]:layout.sample(position).assignments){need(index<navigation.rightActions.size(),"Source row assignment exceeds navigation");result[id]=navigation.rightActions[index];}return result;
}
}
WatchContentCatalog::WatchContentCatalog(const SceneDefinition&scene,const MountedLayoutDocument&document,
    std::span<const NativeLabelBinding>targetBindings,std::span<const WatchContentSnapshot>snapshots){
    need(!snapshots.empty()&&snapshots.size()<=64&&snapshots.front().frame,"Missing source native-content snapshots");
    std::set<std::string,std::less<>> targets;
    for(const auto&entry:array((*snapshots.front().frame)["nativeNavigation"],1024)){
        WatchContentEntry item{entries_.size()+1,text(entry["target"]),text(entry["title"])};
        need(!item.target.empty()&&targets.insert(item.target).second,"Duplicate source navigation target");entries_.push_back(std::move(item));
    }
    need(!entries_.empty(),"Empty source navigation export");
    for(std::size_t i=0;i<std::min<std::size_t>({4,document.buttons.size(),entries_.size()});++i)navigation_.fixedActions.emplace(document.buttons[i].nodeID,entries_[i].action);
    for(std::size_t i=4;i<entries_.size();++i)if(!bottom(entries_[i].target))navigation_.rightActions.push_back(entries_[i].action);
    for(const auto&button:document.buttons)navigation_.managedButtons.insert(button.nodeID);
    for(const auto&[name,target]:std::array<std::pair<std::string_view,std::string_view>,2>{{{"TechtreeBtn","storage"},{"ReportBtn","activityMonitor"}}}){
        const auto node=std::find_if(scene.nodes().begin(),scene.nodes().end(),[&](const auto&n){return n.name==name;});
        if(node==scene.nodes().end())continue;navigation_.managedButtons.insert(node->id);
        if(const auto action=actionForTarget(target))navigation_.fixedActions.emplace(node->id,*action);
    }
    DesktopNavigationLayout layout(scene,document,static_cast<std::int64_t>(navigation_.rightActions.size()));
    initial_=sampleActions(navigation_,layout,snapshots.front().normalizedPosition);
    std::set<std::string,std::less<>> surfaceIDs;
    for(const auto&binding:targetBindings){
        if(binding.profileViewportClip)continue;
        need(navigation_.managedButtons.contains(binding.buttonID),"Native content binding is not a source desktop button");
        need(surfaceIDs.insert(binding.surfaceID).second,"Repeated target content surface");
        surfaces_.push_back({binding.surfaceID,binding.buttonID,binding.sourceNodeID,binding.kind,{}});
    }
    need(!surfaces_.empty()&&surfaces_.size()<=NativeLabelPlan::maximumBindings,"Missing or excessive source content surfaces");
    for(const auto&snapshot:snapshots){
        need(snapshot.frame&&std::isfinite(snapshot.normalizedPosition)&&snapshot.normalizedPosition>=0&&snapshot.normalizedPosition<=1,"Invalid source native-content snapshot");
        const auto&frame=*snapshot.frame;const auto&entries=array(frame["nativeNavigation"],1024);need(entries.size()==entries_.size(),"Source navigation snapshots differ");
        for(std::size_t i=0;i<entries.size();++i)need(text(entries[i]["target"])==entries_[i].target&&text(entries[i]["title"])==entries_[i].title,"Source navigation snapshot order/content differs");
        const auto actions=sampleActions(navigation_,layout,snapshot.normalizedPosition);
        std::map<std::string,const Json*,std::less<>> containers;
        for(const auto&container:array(frame["nativeLayers"]["children"],1024))need(containers.emplace(text(container["name"]),&container).second,"Duplicate native content container name");
        for(auto&surface:surfaces_){
            const auto action=actions.find(surface.buttonID);if(action==actions.end())continue;
            const auto name=std::string(surface.kind==NativeLabelKind::caption?"desktop.watch.label.":"desktop.watch.icon.")+surface.sourceNodeID;
            const auto container=containers.find(name);need(container!=containers.end(),"Source snapshot omits an assigned caption/icon container");
            const auto&children=array((*container->second)["children"],1);need(children.size()==1,"Source caption/icon plane needs one local content root");
            auto content=localTree(children.front(),surface.id);const auto found=surface.variants.find(action->second);
            if(found!=surface.variants.end())need(found->second==content,"Source local variant changes within the same action/slot; export a separate appearance catalog");
            else surface.variants.emplace(action->second,std::move(content));
        }
    }
    for(const auto&entry:entries_){bool caption=false,icon=false;for(const auto&surface:surfaces_)if(surface.variants.contains(entry.action)){if(surface.kind==NativeLabelKind::caption)caption=true;else icon=true;}
        if(!caption||!icon)throw std::invalid_argument("Original-source navigation content does not cover action "+entry.target);}
}
std::optional<std::uint64_t> WatchContentCatalog::actionForTarget(std::string_view target)const noexcept{
    for(const auto&entry:entries_)if(entry.target==target)return entry.action;return {};
}
const Json&WatchContentCatalog::localContent(std::string_view surfaceID,std::uint64_t action)const{
    for(const auto&surface:surfaces_)if(surface.id==surfaceID){const auto variant=surface.variants.find(action);if(variant!=surface.variants.end())return variant->second;break;}
    throw std::invalid_argument("Missing original-source local content variant for surface "+std::string(surfaceID)+" and action "+std::to_string(action));
}
std::size_t WatchContentCatalog::variantCount()const noexcept{std::size_t count{};for(const auto&surface:surfaces_)count+=surface.variants.size();return count;}
NativeWatchContent::NativeWatchContent(const WatchContentCatalog&catalog,LayerScene&layers,LayerRasterOptions options):catalog_(&catalog),layers_(&layers),options_(std::move(options)),sceneRevision_(layers.contentRevision()){
    rendered_.reserve(catalog.surfaces_.size());pending_.reserve(catalog.surfaces_.size());
    for(const auto&surface:catalog.surfaces_){need(layers.surfaceIndex(surface.id).has_value(),"Native navigation local surface is absent");const auto action=catalog.initial_.find(surface.buttonID);rendered_.push_back(action==catalog.initial_.end()?0:action->second);}
}
bool NativeWatchContent::update(const WatchContentCatalog::Actions&actions){
    need(sceneRevision_==layers_->contentRevision(),"Native content scene was structurally replaced; recreate the content bridge");
    pending_.clear();
    for(std::size_t i=0;i<catalog_->surfaces_.size();++i){const auto&surface=catalog_->surfaces_[i];const auto action=actions.find(surface.buttonID);
        if(action==actions.end())continue;const auto variant=surface.variants.find(action->second);
        if(variant==surface.variants.end())throw std::invalid_argument("Missing original-source caption/icon variant for button "+surface.buttonID+" and action "+std::to_string(action->second));
        if(rendered_[i]!=action->second)pending_.push_back({i,action->second,&variant->second});
    }
    ++stats_.updates;if(pending_.empty()){++stats_.unchangedUpdates;return false;}
    bool changed=false;for(const auto&pending:pending_){const auto&surface=catalog_->surfaces_[pending.surface];changed=layers_->updateLocalContent(surface.id,++nextRevision_,*pending.content,options_)||changed;rendered_[pending.surface]=pending.action;++stats_.surfaceUpdates;}
    return changed;
}
} // namespace endfield::native
