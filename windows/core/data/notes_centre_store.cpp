#include "core/data/notes_centre_store.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <system_error>

namespace ehud::data {
namespace {
using Style=endfield::core::notes::TextStyle;using RGBA=endfield::core::notes::RGBA;
void need(bool condition){if(!condition)throw StoreError(StoreErrorCode::invalid,"The centre notes could not be read");}
// JSONDecoder: Int only from an integral number within Int64, Double from any
// number, Bool only from true/false, String only from a string.
std::int64_t whole(const Json&v){need(v.isNumber());const auto token=v.encode(256);std::int64_t value{};const auto r=std::from_chars(token.data(),token.data()+token.size(),value);if(r.ec==std::errc{}&&r.ptr==token.data()+token.size())return value;
    const double d=v.number();need(std::isfinite(d)&&std::trunc(d)==d&&d>=-9223372036854775808.0&&d<9223372036854775808.0);return static_cast<std::int64_t>(d);}
double real(const Json&v){need(v.isNumber());return v.number();}
bool flag(const Json&v){need(v.isBool());return v.boolean();}
std::string text(const Json&v){need(v.isString());return v.string();}
// decodeIfPresent: absent or null -> nil.
bool present(const Json&o,std::string_view key){return o.contains(key)&&!o[key].isNull();}
std::string uuid(const Json&v){auto value=text(v);need(validUUID(value));for(auto&c:value)if(c>='a'&&c<='f')c=static_cast<char>(c-'a'+'A');return value;}
// NotesTextStyle / NotesRGBA synthesized Codable (NotesRichText.swift:5-30):
// fontName and color optional, every other key required; no range checks.
Style runStyle(const Json&o){need(o.isObject());Style s;if(present(o,"fontName"))s.fontName=text(o["fontName"]);s.fontSize=real(o["fontSize"]);
    if(present(o,"color")){const auto&c=o["color"];need(c.isObject());s.color=RGBA{real(c["red"]),real(c["green"]),real(c["blue"]),real(c["alpha"])};}
    s.bold=flag(o["bold"]);s.italic=flag(o["italic"]);s.underline=flag(o["underline"]);s.strikethrough=flag(o["strikethrough"]);return s;}
Json runStyle(const Style&s){Json::Object o{{"fontSize",s.fontSize},{"bold",s.bold},{"italic",s.italic},{"underline",s.underline},{"strikethrough",s.strikethrough}};
    if(s.fontName)o["fontName"]=*s.fontName;if(s.color)o["color"]=Json::Object{{"red",s.color->red},{"green",s.color->green},{"blue",s.color->blue},{"alpha",s.color->alpha}};return o;}
// NotesNoteStyle.init(from:) (NotesFormattingControls.swift:46-54).
NotesNoteStyle noteStyle(const Json&o){need(o.isObject());NotesNoteStyle s;if(present(o,"family"))s.family=text(o["family"]);if(present(o,"size"))s.size=real(o["size"]);
    if(present(o,"bold"))s.bold=flag(o["bold"]);if(present(o,"italic"))s.italic=flag(o["italic"]);if(present(o,"underline"))s.underline=flag(o["underline"]);if(present(o,"color"))s.color=text(o["color"]);return s;}
std::string stamp(double unix){
    const auto seconds=static_cast<std::time_t>(std::isfinite(unix)?std::floor(unix):0);std::tm t{};
#ifdef _WIN32
    gmtime_s(&t,&seconds);
#else
    gmtime_r(&seconds,&t);
#endif
    // ISO8601DateFormatter() default (internet date-time, UTC "Z"), ':' -> '-'.
    char out[32];std::strftime(out,sizeof(out),"%Y-%m-%dT%H-%M-%SZ",&t);return out;
}
}
std::optional<std::uint32_t>NotesNoteStyle::rgb()const noexcept{
    if(color.size()!=6)return {};const bool sign=color[0]=='+'||color[0]=='-';std::uint32_t value{};
    for(std::size_t i=sign;i<6;++i){const char c=color[i];const int digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(digit<0)return {};value=value*16+static_cast<std::uint32_t>(digit);}
    if(color[0]=='-'&&value!=0)return {};return value;
}
bool NotesNoteStyle::valid()const noexcept{return std::isfinite(size)&&size>=6&&size<=144&&rgb()&&(!family||(!family->empty()&&family->size()<=256));}
Style NotesNoteStyle::textStyle()const{
    Style s;s.fontSize=std::isfinite(size)?std::clamp(size,6.,144.):14;s.bold=bold;s.italic=italic;s.underline=underline;
    if(family&&*family!=defaultFamily)s.fontName=*family;
    if(const auto value=rgb();value&&*value!=0xEEEEEE)s.color=RGBA{((*value>>16)&255)/255.,((*value>>8)&255)/255.,(*value&255)/255.,1};
    return s;
}
bool NotesCentreDocument::valid()const{std::set<std::string_view>ids;for(const auto&t:tasks)if(!ids.insert(t.id).second)return false;return version==1&&style.valid();}
std::size_t NotesCentreDocument::doneCount()const noexcept{return static_cast<std::size_t>(std::count_if(tasks.begin(),tasks.end(),[](const auto&t){return t.isChecked;}));}
bool NotesCentreDocument::showsDot(const NotesCentreTask&t)const{return !t.isChecked&&flagged.contains(t.id);}
bool NotesCentreDocument::hasDot()const{return std::any_of(tasks.begin(),tasks.end(),[&](const auto&t){return showsDot(t);});}
std::optional<endfield::core::notes::RichText>NotesCentreDocument::richRuns(std::u16string_view utf16)const{
    if(!richText||richText->version!=1)return {};endfield::core::notes::RichText rich;rich.runs.reserve(richText->runs.size());
    for(const auto&r:richText->runs){if(r.location<0||r.length<=0||r.location>0xffffffffLL||r.length>0xffffffffLL)return {};rich.runs.push_back({static_cast<std::uint32_t>(r.location),static_cast<std::uint32_t>(r.length),r.style,{}});}
    if(!rich.validFor(utf16))return {};return rich;
}
void NotesCentreDocument::setText(std::string value,const endfield::core::notes::RichText& captured){
    text=std::move(value);if(text.empty()){richText.reset();return;}
    NotesCentreRichText rich;rich.version=captured.version;rich.runs.reserve(captured.runs.size());for(const auto&r:captured.runs)rich.runs.push_back({r.location,r.length,r.style});richText=std::move(rich);
}
NotesCentreDocument decodeNotesCentre(std::string_view bytes){
    Json root;try{root=Json::parse(bytes,notesCentreMaximumBytes);}catch(const std::exception&){need(false);}need(root.isObject());
    // NotesCentreDocument.init(from:) (NotesCanvas.swift:26-36).
    NotesCentreDocument d;if(present(root,"version"))d.version=whole(root["version"]);
    if(present(root,"tasks")){need(root["tasks"].isArray());for(const auto&item:root["tasks"].array()){need(item.isObject());d.tasks.push_back({uuid(item["id"]),text(item["text"]),flag(item["isChecked"])});}}
    if(present(root,"text"))d.text=text(root["text"]);
    if(present(root,"richText")){const auto&r=root["richText"];need(r.isObject()&&r["runs"].isArray());NotesCentreRichText rich;rich.version=whole(r["version"]);
        for(const auto&run:r["runs"].array()){need(run.isObject());rich.runs.push_back({whole(run["location"]),whole(run["length"]),runStyle(run["style"])});}d.richText=std::move(rich);}
    if(present(root,"style"))d.style=noteStyle(root["style"]);
    if(present(root,"tab")){const auto tab=text(root["tab"]);need(tab=="text"||tab=="todo");d.tab=tab=="text"?NotesCentreDocument::Tab::text:NotesCentreDocument::Tab::todo;}
    if(present(root,"isFolded"))d.isFolded=flag(root["isFolded"]);
    if(present(root,"flagged")){need(root["flagged"].isArray());for(const auto&id:root["flagged"].array())d.flagged.insert(uuid(id));}
    return d;
}
std::string encodeNotesCentre(const NotesCentreDocument&d){
    Json::Array tasks,flagged;tasks.reserve(d.tasks.size());for(const auto&t:d.tasks)tasks.push_back(Json::Object{{"id",t.id},{"text",t.text},{"isChecked",t.isChecked}});
    for(const auto&id:d.flagged)flagged.push_back(id); // a Set: the Mac's order is not stable either
    Json::Object style{{"size",d.style.size},{"bold",d.style.bold},{"italic",d.style.italic},{"underline",d.style.underline},{"color",d.style.color}};if(d.style.family)style["family"]=*d.style.family;
    Json::Object root{{"version",d.version},{"tasks",std::move(tasks)},{"text",d.text},{"style",std::move(style)},{"tab",d.tab==NotesCentreDocument::Tab::text?"text":"todo"},{"isFolded",d.isFolded},{"flagged",std::move(flagged)}};
    if(d.richText){Json::Array runs;runs.reserve(d.richText->runs.size());for(const auto&r:d.richText->runs)runs.push_back(Json::Object{{"location",r.location},{"length",r.length},{"style",runStyle(r.style)}});root["richText"]=Json::Object{{"version",d.richText->version},{"runs",std::move(runs)}};}
    try{return Json(std::move(root)).encode(notesCentreMaximumBytes);}catch(const std::exception&){throw StoreError(StoreErrorCode::tooLarge,"The centre notes are too large to save");}
}
NotesCentreDocument validatedNotesCentre(std::string_view bytes){auto d=decodeNotesCentre(bytes);need(d.valid());return d;}
NotesCentreStore::NotesCentreStore(std::optional<std::filesystem::path> root,std::function<double()> unixNow){
    if(!root)return;detail::validateRoot(*root);path_=*root/"Notes"/"centre.json";
    std::optional<std::string> bytes;bool oversized{};
    try{bytes=detail::readFile(*path_,notesCentreMaximumBytes);}
    catch(const StoreError&e){if(e.code()!=StoreErrorCode::tooLarge){issue_=LoadIssue::unavailable;return;}oversized=true;}
    if(!bytes&&!oversized){writable_=true;return;}
    if(bytes)try{auto d=decodeNotesCentre(*bytes);if(d.valid()){document_=std::move(d);persisted_=std::move(bytes);writable_=true;return;}}catch(const StoreError&){}
    // NotesCentreStore.init (NotesCanvas.swift:58-63): moved aside, never overwritten.
    issue_=LoadIssue::setAside;const double now=unixNow?unixNow():std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto target=path_->parent_path()/("centre-unreadable-"+stamp(now)+".json");std::error_code error;
    if(std::filesystem::exists(target,error)||error)return;
    std::filesystem::rename(*path_,target,error);if(error)return;setAside_=target;writable_=true;
}
bool NotesCentreStore::update(const std::function<void(NotesCentreDocument&)>& change){
    auto next=document_;change(next);if(next==document_||!next.valid())return false;
    document_=std::move(next);if(!path_||!writable_)return true;
    const auto bytes=encodeNotesCentre(document_);detail::replaceFile(*path_,persisted_,bytes,notesCentreMaximumBytes);persisted_=bytes;return true;
}
}
