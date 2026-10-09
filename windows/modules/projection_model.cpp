#include "modules/projection_model.hpp"
#include "core/data/data_store.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
namespace {
void need(bool b,const char*m){if(!b)throw std::invalid_argument(m);}bool finite(core::Rect r){return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height);}
bool colorValid(NotesColor c){return std::all_of(c.begin(),c.end(),[](double n){return std::isfinite(n)&&n>=0&&n<=1;});}
// CGRect getters standardize negative sizes; source callers normally supply
// positive workspace sizes, but setFrame/constrain accepts arbitrary geometry.
core::Rect standardized(core::Rect r){if(r.width<0){r.x+=r.width;r.width=-r.width;}if(r.height<0){r.y+=r.height;r.height=-r.height;}return r;}
}
std::shared_ptr<const ProjectionMediaReference>ProjectionMediaReference::fromWindowsReference(std::string raw,std::shared_ptr<void>lease){
    const auto json=ehud::data::Json::parse(raw,2*1024*1024);need(json["version"].integer()==1&&json["referencePlatform"].string()=="windows"&&!json.contains("bookmark")&&!json.contains("lastKnownPath")&&!json.contains("isSecurityScoped"),"Projection needs one validated native media locator");
    auto result=std::shared_ptr<ProjectionMediaReference>(new ProjectionMediaReference);result->path_=json["windowsPath"].string();result->name_=json["displayName"].string();const auto kind=json["kind"].string();const auto width=json["pixelWidth"].integer(),height=json["pixelHeight"].integer(),frames=json["frameCount"].integer();
    need(width>=1&&width<=65536&&height>=1&&height<=65536&&frames>=1&&frames<=2000,"Projection media dimensions/frame count exceed source bounds");result->duration_=json["duration"].isNull()?std::nullopt:std::optional(json["duration"].number());
    // Use the existing common reference validator, preserve original additive
    // metadata and lease in the session rather than normalizing/re-saving it.
    (void)ehud::data::makeWindowsMediaReference(result->path_,result->name_,static_cast<int>(width),static_cast<int>(height),kind,result->duration_,static_cast<int>(frames));
    result->kind_=kind=="image"?NotesMediaKind::image:kind=="gif"?NotesMediaKind::gif:NotesMediaKind::video;result->width_=static_cast<unsigned>(width);result->height_=static_cast<unsigned>(height);result->frames_=static_cast<unsigned>(frames);result->encoded_=std::move(raw);result->lease_=std::move(lease);return result;
}
ProjectionWorkspaceGeometry projectionWorkspaceGeometry(core::Point size,double safe,double visible){need(std::isfinite(size.x)&&std::isfinite(size.y)&&size.x>0&&size.y>0,"Projection requires finite positive workspace dimensions");safe=std::isfinite(safe)?std::max(0.,safe):0;visible=std::isfinite(visible)?std::max(0.,visible):0;const auto inset=std::max(64.,std::max(safe,visible)+24);
    const core::Rect toolbar{std::max(6.,(size.x-336)/2),std::min(inset,std::max(6.,size.y-42-8)),336,42};return {{0,0,size.x,size.y},toolbar,{std::max(10.,size.x/2-280),toolbar.y+toolbar.height+10,std::min(560.,size.x-20),30}};
}
core::Rect projectionMenuFrame(const ProjectionWorkspaceGeometry&g,core::Point size){need(std::isfinite(size.x)&&std::isfinite(size.y)&&size.x>0&&size.y>0,"Invalid Projection menu size");return {std::min(g.bounds.x+g.bounds.width-size.x-8,std::max(8.,g.toolbar.x+g.toolbar.width/2-size.x/2)),g.toolbar.y+g.toolbar.height+12,size.x,size.y};}
ProjectionMediaGeometry projectionMediaGeometry(core::Point s,NotesMediaKind kind){need(std::isfinite(s.x)&&std::isfinite(s.y)&&s.x>0&&s.y>0,"Invalid Projection media bounds");const bool moving=kind!=NotesMediaKind::image;const core::Rect seek{37,s.y-20,std::max(1.,s.x-108),12};return {{0,0,s.x,24},{s.x-23,1,22,22},{7,6,s.x-38,14},{5,25,s.x-10,std::max(1.,s.y-(moving?58:30))},moving?core::Rect{7,s.y-25,22,22}:core::Rect{},seek,{s.x-62,s.y-21,44,16},{seek.x,seek.y+seek.height/2,seek.width,2},{s.x-14,s.y-14,14,14},moving,kind==NotesMediaKind::video};}
ProjectionModel::ProjectionModel(NotesColor c,double dark,double blur):color_(c),darkness_(dark),blur_(blur){need(colorValid(c)&&std::isfinite(dark)&&dark>=0&&dark<=1&&std::isfinite(blur)&&blur>=0&&blur<=1,"Invalid initial Projection appearance");media_.reserve(maximumMedia);}
bool ProjectionModel::setColor(NotesColor c){need(colorValid(c),"Invalid Projection brush color");if(c==color_)return false;color_=c;++preferencesRevision_;return true;}
bool ProjectionModel::setBrushWidth(double n){if(!std::isfinite(n))return false;n=std::clamp(n,1.,80.);if(n==width_)return false;width_=n;++preferencesRevision_;return true;}
bool ProjectionModel::setDarkness(double n){if(!std::isfinite(n))return false;n=std::clamp(n,0.,1.);if(n==darkness_)return false;darkness_=n;++preferencesRevision_;return true;}
bool ProjectionModel::setBlur(double n){if(!std::isfinite(n))return false;n=std::clamp(n,0.,1.);if(n==blur_)return false;blur_=n;++preferencesRevision_;return true;}
bool ProjectionModel::setErasing(bool n){if(n==erasing_)return false;erasing_=n;++preferencesRevision_;return true;}
bool ProjectionModel::setBackgroundEnabled(bool n){if(n==background_)return false;background_=n;++preferencesRevision_;return true;}
bool ProjectionModel::append(DrawingStroke stroke){if(!drawing_.append(std::move(stroke)))return false;++drawingRevision_;return true;}
bool ProjectionModel::erase(core::Point point,core::Point size){if(!drawing_.erase(point,width_/2,size))return false;++drawingRevision_;return true;}
const ProjectionMediaItem*ProjectionModel::find(std::uint64_t id)const noexcept{const auto it=std::find_if(media_.begin(),media_.end(),[&](const auto&m){return m.id==id;});return it==media_.end()?nullptr:&*it;}
core::Rect ProjectionModel::constrain(core::Rect frame,core::Rect bounds){bounds=standardized(bounds);frame=standardized(frame);if(!finite(bounds)||bounds.width<=0||bounds.height<=0)return {};const auto w=std::min(bounds.width,std::max(110.,std::isfinite(frame.width)?frame.width:240)),h=std::min(bounds.height,std::max(100.,std::isfinite(frame.height)?frame.height:180));const auto x=std::isfinite(frame.x)?frame.x:bounds.x,y=std::isfinite(frame.y)?frame.y:bounds.y;return {std::min(bounds.x+bounds.width-w,std::max(bounds.x,x)),std::min(bounds.y+bounds.height-h,std::max(bounds.y,y)),w,h};}
std::optional<std::uint64_t>ProjectionModel::addMedia(std::shared_ptr<const ProjectionMediaReference>ref,core::Point point,core::Rect bounds){bounds=standardized(bounds);if(!ref||media_.size()>=maximumMedia||!finite(bounds)||bounds.width<=0||bounds.height<=0)return {};need(nextID_!=std::numeric_limits<std::uint64_t>::max(),"Projection session identity exhausted");const auto width=std::min(360.,std::max(110.,bounds.width*.4)),height=std::min(std::max(110.,bounds.height*.55),std::max(100.,width*double(ref->height())/double(ref->width())+52));const auto id=nextID_++;media_.push_back({id,std::move(ref),constrain({point.x-width/2,point.y-height/2,width,height},bounds)});++mediaRevision_;return id;}
bool ProjectionModel::setFrame(core::Rect frame,std::uint64_t id,core::Rect bounds){auto it=std::find_if(media_.begin(),media_.end(),[&](const auto&m){return m.id==id;});if(it==media_.end())return false;frame=constrain(frame,bounds);if(frame==it->frame)return false;it->frame=frame;++mediaRevision_;return true;}
bool ProjectionModel::bringForward(std::uint64_t id){const auto it=std::find_if(media_.begin(),media_.end(),[&](const auto&m){return m.id==id;});if(it==media_.end()||it+1==media_.end())return false;std::rotate(it,it+1,media_.end());++mediaRevision_;return true;}
bool ProjectionModel::removeMedia(std::uint64_t id){const auto before=media_.size();std::erase_if(media_,[&](const auto&m){return m.id==id;});if(media_.size()==before)return false;++mediaRevision_;return true;}
bool ProjectionModel::clearContent(){const bool changed=!drawing_.strokes().empty()||!media_.empty();if(!drawing_.strokes().empty()){drawing_=NotesDrawing{};++drawingRevision_;}if(!media_.empty()){media_.clear();++mediaRevision_;}return changed;}
}
