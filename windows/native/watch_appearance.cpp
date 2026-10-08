#include "native/watch_appearance.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace endfield::native {
using namespace core::source;
namespace {
void need(bool b,const char*s){if(!b)throw std::invalid_argument(s);}
void identify(Json&j,const std::string&id,unsigned depth=0){need(j.isObject()&&depth<64,"Invalid original local tree");j["id"]=id;if(!j["children"].isNull()){need(j["children"].isArray()&&j["children"].array().size()<=4096,"Invalid original children");auto children=j["children"].array();for(std::size_t n=0;n<children.size();++n)identify(children[n],id+"/"+std::to_string(n),depth+1);j["children"]=std::move(children);}if(!j["mask"].isNull())identify(j["mask"],id+"/mask",depth+1);}
Json rgba(std::array<double,4>c){return Json::Object{{"sRGB",Json::Array{c[0],c[1],c[2],c[3]}}};}
Json font(double size,bool bold){return Json::Object{{"familyName",".AppleSystemUIFont"},{"postScriptName",bold?".AppleSystemUIFontBold":".AppleSystemUIFontMedium"},{"pointSize",size},{"symbolicTraits",bold?2:0},{"ascender",size*.966796875},{"descender",size*-.2109375},{"leading",0}};}
}
WatchAppearanceTemplates::WatchAppearanceTemplates(const Json&j){need(j["schemaVersion"].integer()==1&&j["captionTemplate"]["kind"].string()=="text"&&j["icons"].isObject()&&!j["icons"].object().empty()&&j["icons"].object().size()<=256,"Invalid source appearance template asset");caption_=j["captionTemplate"];for(const auto&[key,v]:j["icons"].object()){need(!key.empty()&&key.size()<=256&&Json::validUtf8(key)&&v["tree"].isObject()&&v["reportOnly"].isBool(),"Invalid original icon template");icons_.emplace(key,Icon{v["tree"],v["reportOnly"].boolean()});}}
const Json&WatchAppearanceTemplates::icon(std::string_view key,bool reportSlot)const{const auto it=icons_.find(key);need(it!=icons_.end(),"Missing original source icon template");need(reportSlot||!it->second.reportOnly,"Original Report artwork cannot replace a different source icon slot");return it->second.tree;}
Json WatchAppearanceTemplates::localIcon(std::string_view key,bool report,std::string_view id)const{need(!id.empty()&&id.size()<=4096&&Json::validUtf8(id),"Invalid local icon identity");auto result=icon(key,report);identify(result,std::string(id));return result;}
Json compileDesktopCaption(const Json&original,const DesktopCaptionPlan&p,DesktopTextSize size,std::string_view id){
 need(original["kind"].string()=="text"&&!id.empty()&&id.size()<=4096&&Json::validUtf8(id)&&std::isfinite(size.width)&&std::isfinite(size.height)&&size.width>0&&size.height>0,"Invalid evaluated source caption geometry");auto result=original;identify(result,std::string(id));result["bounds"]=Json::Array{0,0,size.width,size.height};result["frame"]=result["bounds"];
 auto&text=result["text"];text["string"]=p.text;text["fontSize"]=p.fontSize;text["font"]=font(p.fontSize,p.bold);text["wrapped"]=p.wrapped;text["truncation"]=p.ellipsis?"end":"none";text["foregroundColor"]=rgba(p.color);return result;
}
#ifdef _WIN32
struct NativeWatchAppearance::Impl {
 const WatchAppearanceTemplates&templates;LayerScene&layers;LayerRasterizer&raster;LayerRasterOptions options;
 struct Key{std::uint64_t action{},entriesRevision{};bool selected{};core::Language language{};bool dark{};std::array<double,3>accent{};DesktopTextSize size;bool operator==(const Key&)const=default;};
 struct Surface{NativeLabelBinding binding;DesktopTextSize authored;bool report{};std::optional<Key>rendered;};
 struct Pending{std::size_t surface;Key key;Json content;};
 struct Entry{WatchAppearanceEntry value;std::uint64_t captionRevision{},iconRevision{};};
 std::vector<Surface>surfaces;std::vector<Entry>entries;std::vector<Pending>pending;std::uint64_t entriesRevision{},nextRevision{},structureRevision{};WatchAppearanceStats stats;
 Impl(const WatchAppearanceTemplates&t,const SceneDefinition&scene,std::span<const NativeLabelBinding>bindings,LayerScene&l,LayerRasterizer&r,LayerRasterOptions o):templates(t),layers(l),raster(r),options(std::move(o)),nextRevision(l.contentRevision()),structureRevision(l.contentRevision()){
  need(bindings.size()<=NativeLabelPlan::maximumBindings,"Too many source caption surfaces");std::set<std::string>ids;surfaces.reserve(bindings.size());pending.reserve(bindings.size());
  for(const auto&b:bindings){if(b.profileViewportClip)continue;const auto*node=scene.node(b.sourceNodeID),*button=scene.node(b.buttonID);need(node&&button&&l.surfaceIndex(b.surfaceID)&&ids.insert(b.surfaceID).second,"Missing/repeated source appearance surface");const auto size=node->rect?node->rect->sizeDelta:Vec2{};need(b.kind!=NativeLabelKind::caption||(size[0]>0&&size[1]>0),"Caption needs its original authored RectTransform");surfaces.push_back({b,{size[0],size[1]},button->path.ends_with("/ReportBtn"),{}});}
 }
 const Entry&entry(std::uint64_t action)const{const auto it=std::find_if(entries.begin(),entries.end(),[&](const auto&e){return e.value.action==action;});need(it!=entries.end(),"Missing runtime source navigation entry");return *it;}
};
NativeWatchAppearance::NativeWatchAppearance(const WatchAppearanceTemplates&t,const SceneDefinition&s,std::span<const NativeLabelBinding>b,LayerScene&l,LayerRasterizer&r,LayerRasterOptions o):impl_(std::make_unique<Impl>(t,s,b,l,r,std::move(o))){}
NativeWatchAppearance::~NativeWatchAppearance()=default;
void NativeWatchAppearance::setEntries(std::span<const WatchAppearanceEntry>entries){auto&i=*impl_;need(!entries.empty()&&entries.size()<=1024,"Invalid runtime source navigation entries");std::set<std::uint64_t>ids;for(const auto&e:entries){need(e.action&&ids.insert(e.action).second&&!e.target.empty()&&Json::validUtf8(e.target)&&Json::validUtf8(e.title)&&!e.iconKey.empty(),"Invalid runtime source navigation entry");}
 if(entries.size()==i.entries.size()&&std::equal(entries.begin(),entries.end(),i.entries.begin(),[](const auto&a,const auto&b){return a==b.value;}))return;
 std::vector<Impl::Entry>next;next.reserve(entries.size());const auto revision=i.entriesRevision+1;
 for(const auto&e:entries){const auto old=std::find_if(i.entries.begin(),i.entries.end(),[&](const auto&v){return v.value.action==e.action;});std::uint64_t caption=revision,icon=revision;
  if(old!=i.entries.end()){const auto&v=old->value;if(v.target==e.target&&v.title==e.title&&v.module==e.module)caption=old->captionRevision;if(v.iconKey==e.iconKey)icon=old->iconRevision;}
  next.push_back({e,caption,icon});}
 i.entries.swap(next);i.entriesRevision=revision;
}
bool NativeWatchAppearance::expandsFileShelfCaption(std::uint64_t action,core::Language language)const{const auto&e=impl_->entry(action).value;return e.module&&e.target=="fileShelf"&&core::isCJK(language)&&!core::isChinese(language);}
bool NativeWatchAppearance::update(const WatchContentCatalog::Actions&actions,const WatchAppearance&a,std::span<const NativeLabelPlacement>placements){
 auto&i=*impl_;need(i.structureRevision==i.layers.contentRevision(),"Runtime source label scene was structurally replaced");need(a.language!=core::Language::system,"Runtime caption language must already be resolved");for(auto c:a.accentSRGB)need(std::isfinite(c)&&c>=0&&c<=1,"Invalid runtime caption accent");i.pending.clear();
 for(std::size_t n=0;n<i.surfaces.size();++n){auto&s=i.surfaces[n];const auto action=actions.find(s.binding.buttonID);if(action==actions.end())continue;const auto&item=i.entry(action->second);const auto&e=item.value;const auto place=std::find_if(placements.begin(),placements.end(),[&](const auto&p){return p.surfaceID==s.binding.surfaceID;});need(place!=placements.end(),"Missing current evaluated source label placement");const DesktopTextSize size{place->contentBounds.width,place->contentBounds.height};if(size.width<=0||size.height<=0){need(!place->visible,"Visible source label has empty bounds");continue;}
  const bool caption=s.binding.kind==NativeLabelKind::caption;Impl::Key key{e.action,caption?item.captionRevision:item.iconRevision,caption&&e.module&&a.selectedAction==e.action,caption?a.language:core::Language::english,caption&&e.module&&(e.target=="storage"||e.target=="activityMonitor")&&a.dark,caption&&e.module&&a.selectedAction==e.action&&e.target!="storage"&&e.target!="activityMonitor"?a.accentSRGB:std::array<double,3>{},caption?size:DesktopTextSize{32,32}};if(s.rendered==key)continue;Json content;
  if(caption){DesktopCaptionRequest request{e.target,e.title,s.authored,a.language,e.module,s.binding.rightButton,key.selected,a.dark,a.accentSRGB};const auto measure=[&](std::string_view text,double pointSize,bool bold,double width,bool wrapped){Json descriptor=Json::Object{{"string",std::string(text)},{"fontSize",pointSize},{"font",font(pointSize,bold)},{"wrapped",wrapped}};const auto result=i.raster.measureSourceText(s.binding.surfaceID,descriptor,width,i.options);return DesktopTextSize{result.width,result.height};};const auto plan=sourceDesktopCaption(request,measure);content=compileDesktopCaption(i.templates.captionTemplate(),plan,size,s.binding.surfaceID);++i.stats.captionsCompiled;
  }else{content=i.templates.localIcon(e.iconKey,s.report,s.binding.surfaceID);++i.stats.iconsCompiled;}
  i.pending.push_back({n,key,std::move(content)});
 }
 ++i.stats.updates;if(i.pending.empty()){++i.stats.unchanged;return false;}bool changed{};
 for(auto&p:i.pending){auto&s=i.surfaces[p.surface];changed=i.layers.updateLocalContent(s.binding.surfaceID,++i.nextRevision,p.content,i.options)||changed;s.rendered=p.key;++i.stats.surfaceUpdates;}
 i.pending.clear();return changed;
}
WatchAppearanceStats NativeWatchAppearance::stats()const noexcept{return impl_->stats;}
#endif
}
