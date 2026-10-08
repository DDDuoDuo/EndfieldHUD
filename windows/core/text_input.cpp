#include "core/text_input.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::core::text {
namespace {
void need(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
bool high(char16_t c){return c>=0xd800&&c<=0xdbff;}
bool low(char16_t c){return c>=0xdc00&&c<=0xdfff;}
bool boundary(std::u16string_view s,std::uint32_t at){return at<=s.size()&&(at==0||at==s.size()||!high(s[at-1])||!low(s[at]));}
bool validRange(std::u16string_view s,Range r){return r.start<=r.end&&r.end<=s.size();}
bool finite(Rect r){return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&std::isfinite(r.x+r.width)&&std::isfinite(r.y+r.height);}
std::optional<Rect> projectRect(const Projection& p,Rect r){
    const std::array<Point,4> corners{{{r.x,r.y},{r.x+r.width,r.y},{r.x+r.width,r.y+r.height},{r.x,r.y+r.height}}};
    double left=std::numeric_limits<double>::infinity(),top=left,right=-left,bottom=-left;
    for(const auto& c:corners){const auto v=p.project(c);if(!v)return {};left=std::min(left,v->x);right=std::max(right,v->x);top=std::min(top,v->y);bottom=std::max(bottom,v->y);}
    const Rect result{left,top,right-left,bottom-top};return finite(result)?std::optional(result):std::nullopt;
}
}
bool Buffer::validUTF16(std::u16string_view text)noexcept{
    for(std::size_t i=0;i<text.size();++i){if(high(text[i])){if(++i==text.size()||!low(text[i]))return false;}else if(low(text[i]))return false;}return true;
}
Buffer::Buffer(std::u16string value,std::uint32_t maximum):text_(std::move(value)),maximum_(maximum){
    need(maximum>0&&maximum<=safetyMaximumUnits&&text_.size()<=maximum&&validUTF16(text_),"Invalid bounded UTF-16 text buffer");
}
void Buffer::setSelection(Selection value){
    need((value.activeEnd==ActiveEnd::none||value.activeEnd==ActiveEnd::start||value.activeEnd==ActiveEnd::end)&&validRange(text_,value.range)&&(!value.interim||value.activeEnd==ActiveEnd::none),"Invalid UTF-16 selection");selection_=value;
}
void Buffer::beginInputTransaction(){need(!inputSelection_,"Input transaction already active");inputSelection_=selection_;}
void Buffer::endInputTransaction()noexcept{inputSelection_.reset();preparedSnapshot_.reset();}
Change Buffer::replace(Range range,std::u16string_view value){
    need(!readOnly_,"Text document is read only");need(validRange(text_,range)&&boundary(text_,range.start)&&boundary(text_,range.end),"Invalid UTF-16 replacement range");
    need(value.size()<=maximum_-(text_.size()-(range.end-range.start)),"Text replacement exceeds document limit");
    need(validUTF16(value),"Invalid UTF-16 replacement text");
    if(inputSelection_&&!composition_&&!preparedSnapshot_)preparedSnapshot_=Snapshot{text_,*inputSelection_};
    const auto newEnd=range.start+static_cast<std::uint32_t>(value.size());const Change change{range.start,range.end,newEnd};
    // std::basic_string::replace handles source aliases. Complete validation
    // occurs before mutation; allocation failure preserves the old model.
    text_.replace(range.start,range.end-range.start,value);selection_={{newEnd,newEnd},ActiveEnd::end,false};++revision_;
    if(composition_){const auto map=[&](std::uint32_t p,bool right){if(p<range.start)return p;if(p>range.end)return p-(range.end-range.start)+static_cast<std::uint32_t>(value.size());return right?newEnd:range.start;};
        composition_=Range{map(composition_->start,false),map(composition_->end,true)};}
    return change;
}
void Buffer::beginComposition(Range range){
    need(!readOnly_&&!composition_&&validRange(text_,range),"Invalid composition start");
    if(preparedSnapshot_){snapshot_=std::move(preparedSnapshot_);preparedSnapshot_.reset();}
    else{Snapshot copy{text_,inputSelection_.value_or(selection_)};snapshot_=std::move(copy);}
    composition_=range;
}
void Buffer::updateComposition(Range range){need(composition_.has_value()&&validRange(text_,range),"Invalid composition update");composition_=range;}
std::optional<Change> Buffer::endComposition(bool cancel){
    if(!composition_)return {};std::optional<Change> change;
    if(cancel){need(snapshot_.has_value(),"Composition snapshot missing");change=Change{0,static_cast<std::uint32_t>(text_.size()),static_cast<std::uint32_t>(snapshot_->text.size())};
        text_=std::move(snapshot_->text);selection_=snapshot_->selection;++revision_;}
    snapshot_.reset();composition_.reset();preparedSnapshot_.reset();return change;
}
bool Placement::operator==(const Placement& p)const{return projection.values==p.projection.values&&viewport==p.viewport&&scroll==p.scroll&&visible==p.visible;}
bool validPlacement(const Placement& p)noexcept{
    return finite(p.viewport)&&p.viewport.width>0&&p.viewport.height>0&&std::isfinite(p.scroll.x)&&std::isfinite(p.scroll.y)&&
        std::all_of(p.projection.values.begin(),p.projection.values.end(),[](double v){return std::isfinite(v);});
}
std::optional<Rect> projectedViewport(const Placement& p){if(!validPlacement(p))return {};if(!p.visible)return Rect{};return projectRect(p.projection,p.viewport);}
std::optional<ProjectedBounds> projectedRange(const Document& doc,const Layout& layout,Range range,const Placement& p){
    if(!validPlacement(p)||!validRange(doc.text(),range)||layout.textRevision()!=doc.revision())return {};
    if(!p.visible)return ProjectedBounds{{},true};const auto bounds=layout.bounds(range);if(!bounds||!finite(bounds->bounds)||bounds->bounds.width<0||bounds->bounds.height<0)return {};
    auto r=bounds->bounds;r.x+=p.viewport.x-p.scroll.x;r.y+=p.viewport.y-p.scroll.y;if(!finite(r))return {};
    const auto left=std::max(r.x,p.viewport.x),top=std::max(r.y,p.viewport.y),right=std::min(r.x+r.width,p.viewport.x+p.viewport.width),bottom=std::min(r.y+r.height,p.viewport.y+p.viewport.height);
    const bool clipped=bounds->clipped||left!=r.x||top!=r.y||right!=r.x+r.width||bottom!=r.y+r.height;
    if(right<=left||bottom<=top)return ProjectedBounds{{},true};const auto projected=projectRect(p.projection,{left,top,right-left,bottom-top});
    return projected?std::optional(ProjectedBounds{*projected,clipped}):std::nullopt;
}
std::optional<std::uint32_t> projectedHit(const Document& doc,const Layout& layout,Point client,const Placement& p,bool nearest,bool roundNearest){
    if(!validPlacement(p)||!p.visible||layout.textRevision()!=doc.revision())return {};auto local=p.projection.unproject(client);if(!local)return {};
    if(!p.viewport.contains(*local)){if(!nearest)return {};local->x=std::clamp(local->x,p.viewport.x,p.viewport.x+p.viewport.width);local->y=std::clamp(local->y,p.viewport.y,p.viewport.y+p.viewport.height);}
    const auto hit=layout.hit({local->x-p.viewport.x+p.scroll.x,local->y-p.viewport.y+p.scroll.y},nearest,roundNearest);
    return hit&&*hit<=doc.text().size()?hit:std::nullopt;
}
} // namespace endfield::core::text
