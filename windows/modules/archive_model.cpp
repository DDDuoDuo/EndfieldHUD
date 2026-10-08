#include "modules/archive_model.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::modules {
namespace {
using J=ArchiveJson;using Style=core::notes::TextStyle;
void need(bool v,const char*m){if(!v)throw ehud::data::StoreError(ehud::data::StoreErrorCode::invalid,m);}
void rules(const ArchiveTextRules&r){need(bool(r.characters)&&bool(r.trimmed)&&bool(r.categoryNameKey),"Archive requires explicit Unicode text rules");}
void text(std::string_view s,std::size_t bytes){need(s.size()<=bytes&&J::validUtf8(s),"Invalid bounded Archive UTF-8");}
std::string required(const J&j,const char*k){need(j[k].isString(),"Missing Archive string field");return j[k].string();}
double number(const J&j,const char*k){need(j[k].isNumber(),"Missing Archive date/number");const double n=j[k].number();need(std::isfinite(n),"Nonfinite Archive date/number");return n;}
std::optional<std::string>category(const J&j){if(j["categoryID"].isNull())return {};return required(j,"categoryID");}
Style style(const J&j,double fallback){if(j.isNull())return Style{.fontSize=fallback};
    auto rich=core::notes::decodeRichText(J::Object{{"version",1},{"runs",J::Array{J::Object{{"location",0},{"length",1},{"style",j}}}}},u"x");return rich.runs.front().style;
}
J style(const Style&s){core::notes::RichText value;value.runs.push_back({0,1,s,{}});return core::notes::encodeRichText(value,u"x")["runs"].array().front()["style"];}
ArchiveTemplate type(const J&j){const auto t=required(j,"template");need(t=="journal"||t=="research","Unknown Archive template");return t=="journal"?ArchiveTemplate::journal:ArchiveTemplate::research;}
std::size_t base64Size(std::string_view s){need(!s.empty()&&s.size()%4==0&&s.size()<=1'398'104,"Invalid preserved Mac Archive bookmark");const auto padding=s.ends_with("==")?2u:s.ends_with("=")?1u:0u;
    for(std::size_t i=0;i<s.size()-padding;++i){const char c=s[i];need((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/',"Invalid preserved Mac Archive bookmark encoding");}
    for(std::size_t i=s.size()-padding;i<s.size();++i)need(s[i]=='=',"Invalid Archive bookmark padding");return s.size()/4*3-padding;
}
}
std::u16string archiveUTF16(std::string_view value){need(J::validUtf8(value),"Invalid Archive UTF-8");std::u16string out;out.reserve(value.size());
    for(std::size_t at=0;at<value.size();){const auto first=static_cast<unsigned char>(value[at++]);std::uint32_t code=first;if(first>=128){const unsigned count=first<0xe0?2u:first<0xf0?3u:4u;code=first&((1u<<(7-count))-1);for(unsigned n=1;n<count;++n)code=(code<<6)|(static_cast<unsigned char>(value[at++])&63);}
        if(code<0x10000)out.push_back(static_cast<char16_t>(code));else{code-=0x10000;out.push_back(static_cast<char16_t>(0xd800+(code>>10)));out.push_back(static_cast<char16_t>(0xdc00+(code&1023)));}}
    return out;
}
std::string archiveUTF8(std::u16string_view value){std::string out;out.reserve(value.size());for(std::size_t at=0;at<value.size();){std::uint32_t c=value[at++];
    if(c>=0xd800&&c<=0xdbff){need(at<value.size()&&value[at]>=0xdc00&&value[at]<=0xdfff,"Invalid Archive UTF-16 pair");c=0x10000+(c-0xd800)*1024+(value[at++]-0xdc00);}else need(c<0xdc00||c>0xdfff,"Invalid Archive UTF-16 surrogate");
    if(c<128)out.push_back(static_cast<char>(c));else if(c<0x800){out.push_back(char(0xc0|(c>>6)));out.push_back(char(0x80|(c&63)));}else if(c<0x10000){out.push_back(char(0xe0|(c>>12)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}else{out.push_back(char(0xf0|(c>>18)));out.push_back(char(0x80|((c>>12)&63)));out.push_back(char(0x80|((c>>6)&63)));out.push_back(char(0x80|(c&63)));}}
    return out;
}
void validateArchiveMedia(const J&j){need(j.isObject()&&j["version"].isNumber(),"Invalid Archive attachment reference");const auto version=j["version"].integer();if(version>1)throw ehud::data::StoreError(ehud::data::StoreErrorCode::newerVersion,"Archive media needs a newer app version");need(version==1,"Invalid Archive media version");
    const auto kind=required(j,"kind");need(kind=="image"||kind=="gif"||kind=="video","Unknown Archive media kind");
    if(j.contains("referencePlatform")||j.contains("windowsPath")){need(j["referencePlatform"].isString()&&j["referencePlatform"].string()=="windows"&&j["windowsPath"].isString()&&ehud::data::validWindowsFilePath(j["windowsPath"].string()),"Invalid native Archive media path");need(!j.contains("bookmark")&&!j.contains("isSecurityScoped")&&!j.contains("lastKnownPath"),"Ambiguous Archive media locator");}
    else {need(base64Size(required(j,"bookmark"))<=1024*1024&&j["isSecurityScoped"].isBool(),"Invalid preserved Mac Archive access context");const auto path=required(j,"lastKnownPath");text(path,32768);need(!path.empty()&&path.front()=='/'&&path.find('\0')==std::string::npos,"Invalid preserved Mac Archive media path");}
    const auto name=required(j,"displayName");text(name,4096);need(!name.empty()&&name.find('\0')==std::string::npos,"Invalid Archive media display name");
    for(const char*k:{"pixelWidth","pixelHeight"})need(j[k].isNumber()&&j[k].integer()>0&&j[k].integer()<=65536,"Invalid Archive media dimensions");need(j["frameCount"].isNumber(),"Missing Archive frame count");const auto frames=j["frameCount"].integer();need(frames>0&&frames<=2000&&(kind!="image"||frames==1),"Invalid Archive frame count");
    need(kind!="video"||!j["duration"].isNull(),"Video Archive reference has no duration");if(!j["duration"].isNull()){const auto duration=number(j,"duration");need(duration>0&&duration<=31536000,"Invalid Archive media duration");}
}
void validateArchiveCategory(const ArchiveCategory&c,const ArchiveTextRules&r){rules(r);text(c.name,1024);need(ehud::data::validUUID(c.id)&&std::isfinite(c.created)&&!r.trimmed(c.name).empty()&&r.characters(c.name)<=80&&c.name.find_first_of(std::string("\0\n\r",3))==std::string::npos,"Invalid Archive category");}
void validateArchiveEntry(const ArchiveEntry&e,const ArchiveTextRules&r){rules(r);text(e.title,archiveMaximumPayloadBytes);text(e.body,archiveMaximumBodyBytes);need(ehud::data::validUUID(e.id)&&e.type>=ArchiveTemplate::journal&&e.type<=ArchiveTemplate::research&&r.characters(e.title)<=200&&e.title.find('\0')==std::string::npos&&std::isfinite(e.date)&&std::isfinite(e.modified)&&e.media.size()<=16&&(!e.categoryID||ehud::data::validUUID(*e.categoryID))&&e.titleStyle.valid()&&e.bodyStyle.valid(),"Invalid Archive document");
    for(const auto&m:e.media)validateArchiveMedia(m);if(e.titleRichText)need(e.titleRichText->validFor(archiveUTF16(e.title)),"Invalid Archive title formatting");if(e.bodyRichText)need(e.bodyRichText->validFor(archiveUTF16(e.body)),"Invalid Archive body formatting");
}
void validateArchiveSummary(const ArchiveSummary&s,const ArchiveTextRules&r){rules(r);text(s.title,archiveMaximumPayloadBytes);need(ehud::data::validUUID(s.id)&&s.type>=ArchiveTemplate::journal&&s.type<=ArchiveTemplate::research&&r.characters(s.title)<=200&&s.title.find('\0')==std::string::npos&&std::isfinite(s.date)&&s.mediaCount<=16&&(!s.categoryID||ehud::data::validUUID(*s.categoryID))&&(!s.thumbnail||s.mediaCount>0),"Invalid Archive summary");if(s.thumbnail)validateArchiveMedia(*s.thumbnail);}
ArchiveCategory decodeArchiveCategory(const J&j,const ArchiveTextRules&r){need(j.isObject(),"Invalid Archive category object");ArchiveCategory c{required(j,"id"),required(j,"name"),number(j,"created"),j.object()};for(const char*k:{"id","name","created"})c.extra.erase(k);validateArchiveCategory(c,r);return c;}
ArchiveEntry decodeArchiveEntry(const J&j,const ArchiveTextRules&r){need(j.isObject()&&j["media"].isArray(),"Invalid Archive document object");ArchiveEntry e;e.id=required(j,"id");e.type=type(j);e.title=required(j,"title");e.body=required(j,"body");e.date=number(j,"date");e.modified=number(j,"modified");e.media=j["media"].array();e.categoryID=category(j);e.titleStyle=style(j["titleStyle"],17);e.bodyStyle=style(j["bodyStyle"],12);
    if(!j["titleRichText"].isNull())e.titleRichText=core::notes::decodeRichText(j["titleRichText"],archiveUTF16(e.title));if(!j["bodyRichText"].isNull())e.bodyRichText=core::notes::decodeRichText(j["bodyRichText"],archiveUTF16(e.body));e.extra=j.object();for(const char*k:{"id","template","title","date","body","media","modified","categoryID","titleRichText","bodyRichText","titleStyle","bodyStyle"})e.extra.erase(k);validateArchiveEntry(e,r);return e;
}
J encodeArchiveCategory(const ArchiveCategory&c,const ArchiveTextRules&r){validateArchiveCategory(c,r);J out=c.extra;out["id"]=c.id;out["name"]=c.name;out["created"]=c.created;return out;}
J encodeArchiveEntry(const ArchiveEntry&e,const ArchiveTextRules&r){validateArchiveEntry(e,r);J out=e.extra;out["id"]=e.id;out["template"]=e.type==ArchiveTemplate::journal?"journal":"research";out["title"]=e.title;out["date"]=e.date;out["body"]=e.body;out["media"]=e.media;out["modified"]=e.modified;
    out.erase("categoryID");if(e.categoryID)out["categoryID"]=*e.categoryID;out.erase("titleRichText");if(e.titleRichText)out["titleRichText"]=core::notes::encodeRichText(*e.titleRichText,archiveUTF16(e.title));out.erase("bodyRichText");if(e.bodyRichText)out["bodyRichText"]=core::notes::encodeRichText(*e.bodyRichText,archiveUTF16(e.body));out["titleStyle"]=style(e.titleStyle);out["bodyStyle"]=style(e.bodyStyle);return out;
}
ArchiveSummary archiveSummary(const ArchiveEntry&e){return {e.id,e.type,e.title,e.date,e.media.size(),e.categoryID,e.media.empty()?std::nullopt:std::optional(e.media.front())};}
std::size_t archiveThumbnailCost(const std::optional<J>&j){if(!j)return 0;validateArchiveMedia(*j);std::size_t bookmark{},path{};if((*j)["bookmark"].isString()){bookmark=base64Size((*j)["bookmark"].string());path=(*j)["lastKnownPath"].string().size();}else path=(*j)["windowsPath"].string().size();return bookmark*2+(path+(*j)["displayName"].string().size())*6+512;}
ArchiveCategory archiveLegacyCategory(ArchiveTemplate t){need(t==ArchiveTemplate::journal||t==ArchiveTemplate::research,"Unknown legacy Archive template");return {t==ArchiveTemplate::journal?"8A67BC44-25D4-4DC1-91EF-000000000001":"8A67BC44-25D4-4DC1-91EF-000000000002",t==ArchiveTemplate::journal?"Journal":"Q&A",-archiveFoundationToUnix,{}};}
} // namespace endfield::modules
