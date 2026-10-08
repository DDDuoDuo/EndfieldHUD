#include "modules/notes_drawing.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
using J=ehud::data::Json;using P=core::Point;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
bool finite(P p)noexcept{return std::isfinite(p.x)&&std::isfinite(p.y);}
bool sizeValid(P p)noexcept{return finite(p)&&p.x>0&&p.y>0;}
bool pointValid(P p)noexcept{return finite(p)&&p.x>=0&&p.x<=1&&p.y>=0&&p.y<=1;}
double number(const J&j,const char*key){need(j[key].isNumber(),"Missing drawing number");const auto n=j[key].number();need(std::isfinite(n),"Nonfinite drawing number");return n;}
bool touches(const DrawingStroke&s,P local,double radius,P size){
    const auto threshold=radius+s.width/2;
    auto pixel=[&](P p){return P{p.x*size.x,p.y*size.y};};
    auto previous=pixel(s.points.front());
    if(s.points.size()==1)return std::hypot(previous.x-local.x,previous.y-local.y)<=threshold;
    for(std::size_t n=1;n<s.points.size();++n){const auto next=pixel(s.points[n]);const auto dx=next.x-previous.x,dy=next.y-previous.y,length=dx*dx+dy*dy;
        const auto t=length==0?0:std::clamp(((local.x-previous.x)*dx+(local.y-previous.y)*dy)/length,0.,1.);
        if(std::hypot(local.x-previous.x-t*dx,local.y-previous.y-t*dy)<=threshold)return true;previous=next;
    }return false;
}
}
bool DrawingStroke::valid()const noexcept{return !points.empty()&&points.size()<=NotesDrawing::maximumPointsPerStroke&&std::isfinite(width)&&width>=1&&width<=80&&std::all_of(points.begin(),points.end(),pointValid)&&std::all_of(color.begin(),color.end(),[](double c){return std::isfinite(c)&&c>=0&&c<=1;});}
NotesDrawing::NotesDrawing(std::string_view payload):original_(J::parse(payload,32*1024*1024)){
    need(original_.isObject()&&original_["version"].isNumber()&&original_["version"].number()==1&&original_["strokes"].isArray(),"Unsupported drawing payload");
    const auto&stored=original_["strokes"].array();need(stored.size()<=maximumStrokes,"Drawing stroke limit exceeded");strokes_.reserve(stored.size());originalStrokes_.reserve(stored.size());
    for(const auto&item:stored){need(item.isObject()&&item["points"].isArray()&&item["color"].isObject(),"Invalid drawing stroke");const auto&points=item["points"].array();need(!points.empty()&&points.size()<=maximumPointsPerStroke&&points.size()<=maximumPoints-pointCount_,"Drawing point limit exceeded");
        DrawingStroke stroke;stroke.width=number(item,"width");const auto&c=item["color"];stroke.color={number(c,"red"),number(c,"green"),number(c,"blue"),number(c,"alpha")};stroke.points.reserve(points.size());for(const auto&p:points){need(p.isObject(),"Invalid drawing point");stroke.points.push_back({number(p,"x"),number(p,"y")});}
        need(stroke.valid(),"Invalid drawing stroke bounds");pointCount_+=stroke.points.size();strokes_.push_back(std::move(stroke));originalStrokes_.push_back(item);
    }
    // Keep only document-level extension fields here; original point metadata
    // belongs to the corresponding retained stroke instead of a second copy.
    original_.erase("strokes");
}
bool NotesDrawing::append(DrawingStroke stroke){if(!stroke.valid()||strokes_.size()>=maximumStrokes||stroke.points.size()>maximumPoints-pointCount_)return false;
    // Reserve both before mutation, so allocation failure cannot split the two
    // parallel arrays. Geometric growth avoids copying all strokes per drag.
    if(strokes_.size()==strokes_.capacity()){const auto capacity=std::min(maximumStrokes,std::max(std::size_t(8),strokes_.size()*2));strokes_.reserve(capacity);originalStrokes_.reserve(capacity);}
    else if(originalStrokes_.size()==originalStrokes_.capacity())originalStrokes_.reserve(strokes_.capacity());
    pointCount_+=stroke.points.size();strokes_.push_back(std::move(stroke));originalStrokes_.emplace_back();return true;
}
bool NotesDrawing::erase(P local,double radius,P size){need(finite(local)&&std::isfinite(radius)&&radius>=0&&sizeValid(size),"Invalid drawing eraser geometry");bool changed{};std::size_t output{};
    for(std::size_t n=0;n<strokes_.size();++n){if(touches(strokes_[n],local,radius,size)){pointCount_-=strokes_[n].points.size();changed=true;continue;}if(output!=n){strokes_[output]=std::move(strokes_[n]);originalStrokes_[output]=std::move(originalStrokes_[n]);}++output;}
    strokes_.resize(output);originalStrokes_.resize(output);return changed;
}
std::string NotesDrawing::encode()const{auto document=original_;document["version"]=1;J::Array output;output.reserve(strokes_.size());
    for(std::size_t n=0;n<strokes_.size();++n){const auto&s=strokes_[n];auto item=originalStrokes_[n];if(!item.isObject())item=J::Object{};item["width"]=s.width;auto c=item["color"];if(!c.isObject())c=J::Object{};const char*keys[]={"red","green","blue","alpha"};for(unsigned k=0;k<4;++k)c[keys[k]]=s.color[k];item["color"]=std::move(c);
        J::Array points;points.reserve(s.points.size());for(std::size_t k=0;k<s.points.size();++k){auto p=item["points"].isArray()&&k<item["points"].array().size()?item["points"].array()[k]:J(J::Object{});p["x"]=s.points[k].x;p["y"]=s.points[k].y;points.push_back(std::move(p));}item["points"]=std::move(points);output.push_back(std::move(item));
    }document["strokes"]=std::move(output);return document.encode(32*1024*1024);
}
std::vector<P>NotesDrawing::path(const DrawingStroke&stroke,P size){need(stroke.valid()&&sizeValid(size),"Invalid drawing path geometry");std::vector<P>result;result.reserve(std::max(std::size_t(2),stroke.points.size()));for(const auto&p:stroke.points)result.push_back({p.x*size.x,p.y*size.y});if(result.size()==1)result.push_back({result[0].x+.01,result[0].y});return result;}
core::Rect NotesDrawing::viewport(P size){need(finite(size)&&size.x>10&&size.y>0,"Invalid drawing card size");return {5,29,size.x-10,std::max(1.,size.y-56)};}
DrawingSample sampleDrawingStroke(DrawingStroke&stroke,P local,P size){need(finite(local)&&sizeValid(size),"Invalid drawing sample geometry");const P next{std::clamp(local.x,0.,size.x)/size.x,std::clamp(local.y,0.,size.y)/size.y};if(!stroke.points.empty()){const auto&last=stroke.points.back();if(std::hypot((last.x-next.x)*size.x,(last.y-next.y)*size.y)<.8)return DrawingSample::ignored;}if(stroke.points.size()>=NotesDrawing::maximumPointsPerStroke)return DrawingSample::limit;stroke.points.push_back(next);return DrawingSample::appended;}
double scrollDrawingWidth(double width,double delta){need(std::isfinite(width)&&width>=1&&width<=80&&std::isfinite(delta),"Invalid drawing width scroll");return std::clamp(width+delta*.25,1.,80.);}
}
