#include "modules/notes_checklist.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace endfield::modules {
namespace {
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
bool boundary(std::string_view text,std::size_t at){return at<=text.size()&&(at==text.size()||(static_cast<unsigned char>(text[at])&0xc0)!=0x80);}
void validate(const NotesMeasuredText&text,double width){
    need(ehud::data::Json::validUtf8(text.text)&&text.fontSize==11&&text.width==width&&!text.richText&&!text.sourceRichPayload&&std::isfinite(text.height)&&text.height>0&&!text.lines.empty(),"TODO requires complete plain font11 source measurement");
    std::size_t end{};double y{};for(const auto&line:text.lines){need(line.begin==end&&line.begin<=line.visibleTextEnd&&line.visibleTextEnd<=line.end&&boundary(text.text,line.begin)&&boundary(text.text,line.visibleTextEnd)&&boundary(text.text,line.end)&&line.y==y&&std::isfinite(line.height)&&line.height>0,"Invalid TODO text line index");end=line.end;y+=line.height;}
    need(end==text.text.size()&&y==text.height,"Incomplete TODO measured text");
}
core::Rect intersection(core::Rect a,core::Rect b){const double x=std::max(a.x,b.x),y=std::max(a.y,b.y),right=std::min(a.x+a.width,b.x+b.width),bottom=std::min(a.y+a.height,b.y+b.height);return {x,y,std::max(0.,right-x),std::max(0.,bottom-y)};}
}
NotesChecklistLayout::NotesChecklistLayout(std::string id,double width,double height,std::span<const NotesChecklistMeasurement> input)
    :noteID_(std::move(id)),width_(width),height_(height),textWidth_(std::max(20.,width-89)),viewport_{5,27,width-10,std::max(1.,height-55)}{
    need(ehud::data::validUUID(noteID_)&&std::isfinite(width)&&width>=10&&width<=32768&&std::isfinite(height)&&height>0&&height<=32768&&input.size()<=maximumRows,"Invalid TODO source geometry or native row capacity");
    rows_.reserve(input.size());std::set<std::string,std::less<>>ids;
    for(const auto&item:input){need(ehud::data::validUUID(item.itemID)&&ids.insert(item.itemID).second&&item.text&&item.display,"Invalid TODO measured row identity");validate(*item.text,textWidth_);validate(*item.display,textWidth_);
        need(item.text->text.empty()||item.display==item.text||item.display->text==item.text->text,"Nonempty TODO display must preserve actual text");
        const auto h=std::max(25.,item.text->height+8);need(std::isfinite(contentHeight_+h),"TODO content extent overflow");rows_.push_back({item.itemID,item.text,item.display,contentHeight_,h,item.checked});contentHeight_+=h;}
}
double NotesChecklistLayout::maximumScrollOffset()const noexcept{return std::max(0.,contentHeight_-viewport_.height);}
double NotesChecklistLayout::clampOffset(double offset)const{need(std::isfinite(offset),"Nonfinite TODO scroll offset");return std::clamp(offset,0.,maximumScrollOffset());}
std::pair<std::size_t,std::size_t>NotesChecklistLayout::visibleRows(double offset)const{offset=clampOffset(offset);const auto first=std::lower_bound(rows_.begin(),rows_.end(),offset,[](const auto&r,double y){return r.origin+r.height<=y;});const auto last=std::lower_bound(first,rows_.end(),offset+viewport_.height,[](const auto&r,double y){return r.origin<y;});return {static_cast<std::size_t>(first-rows_.begin()),static_cast<std::size_t>(last-rows_.begin())};}
core::Rect NotesChecklistLayout::textRect(std::size_t row,double offset)const{need(row<rows_.size(),"TODO row exceeds current index");offset=clampOffset(offset);return {28,viewport_.y+rows_[row].origin-offset+3,textWidth_,rows_[row].height-6};}
NotesChecklistEdit NotesChecklistLayout::beginEditing(std::size_t row,double offset)const{need(row<rows_.size(),"TODO editor row is unavailable");offset=clampOffset(offset);const auto&r=rows_[row];const auto bottom=r.origin+r.height;
    if(r.origin<offset||bottom>offset+viewport_.height)offset=clampOffset(r.height>viewport_.height?r.origin:std::max(0.,bottom-viewport_.height));const auto full=textRect(row,offset);
    return {row,intersection(full,viewport_),offset,std::max(0.,viewport_.y-full.y),11,false};
}
double NotesChecklistLayout::finishEditingOffset(std::size_t row,double noteOffset,double initial,double final)const{need(row<rows_.size()&&std::isfinite(initial)&&initial>=0&&std::isfinite(final)&&final>=0,"Invalid TODO editor completion offset");return initial>0||final>0?clampOffset(rows_[row].origin+3+final):clampOffset(noteOffset);}
std::vector<NotesAction>NotesChecklistLayout::actions(double offset,const NotesChecklistStrings&strings)const{
    for(const auto*s:{&strings.check,&strings.uncheck,&strings.edit,&strings.up,&strings.down,&strings.remove,&strings.add})need(ehud::data::Json::validUtf8(*s),"Invalid localized TODO action label");
    offset=clampOffset(offset);const auto[first,last]=visibleRows(offset);std::vector<NotesAction>out;out.reserve((last-first)*5+1);const auto prefix="note:"+noteID_+":";
    auto append=[&](std::string verb,std::string label,core::Rect bounds){bounds=intersection(bounds,viewport_);if(bounds.width>1&&bounds.height>1)out.push_back({prefix+verb,std::move(verb),std::move(label),bounds,false});};
    for(auto index=first;index<last;++index){const auto&r=rows_[index];const auto y=viewport_.y+r.origin-offset;const auto suffix=":"+r.itemID;
        append("check"+suffix,(r.checked?strings.uncheck:strings.check)+" "+r.text->text,{5,y,21,22});append("editItem"+suffix,strings.edit+" "+r.text->text,textRect(index,offset));
        append("up"+suffix,strings.up,{width_-58,y,17,22});append("down"+suffix,strings.down,{width_-40,y,17,22});append("remove"+suffix,strings.remove,{width_-22,y,17,22});}
    out.push_back({prefix+"add","add",strings.add,{7,height_-26,width_-29,21},false});return out;
}
} // namespace endfield::modules
