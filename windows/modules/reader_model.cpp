#include "modules/reader_model.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace endfield::modules {
namespace {
using J=ReaderJson;
J extra(J j,std::initializer_list<const char*>keys){for(const auto*key:keys)j.erase(key);return j;}
void number(J&j,const char*key,double value,const J&original){const auto&token=original[key];j[key]=token.isNumber()&&token.number()==value?token:J(value);}
J numeric(const J&j,std::initializer_list<const char*>keys){J out=J::Object{};for(const auto*key:keys)out[key]=j[key];return out;}
void need(bool value){if(!value)throw ReaderError(ReaderErrorCode::invalidBook);}
std::string uuid(std::string value){need(ehud::data::validUUID(value));for(auto&c:value)if(c>='a'&&c<='f')c=char(c-'a'+'A');return value;}
bool bookmark(std::string_view value){
    if(value.empty()||value.size()%4||value.size()>349528)return false;
    const auto padding=value.ends_with("==")?2u:value.ends_with("=")?1u:0u;
    const auto size=value.size()/4*3-padding;if(!size||size>256*1024)return false;
    for(std::size_t n=0;n<value.size()-padding;++n){const auto c=value[n];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'))return false;}
    return true;
}
bool progress(double value){return std::isfinite(value)&&value>=0&&value<=1;}
void validBook(const ReaderBook&b){
    need(ehud::data::validUUID(b.id)&&!b.title.empty()&&b.title.size()<=4096&&J::validUtf8(b.title)&&b.location.valid()&&progress(b.progress)&&b.bookmarks.size()<=readerMaximumBookmarks&&b.originalFields.isObject());
    if(b.platform==ReaderReferencePlatform::macOS)need(bookmark(b.bookmarkBase64)&&b.path.starts_with('/')&&b.path.size()<=32768&&b.path.find('\0')==std::string::npos&&J::validUtf8(b.path)&&b.windowsPath.empty());
    else{need(b.platform==ReaderReferencePlatform::windows&&ehud::data::validWindowsFilePath(b.windowsPath)&&b.path.empty()&&b.bookmarkBase64.empty()&&!b.scoped);}
    std::set<std::string,std::less<>>ids;for(const auto&v:b.bookmarks)need(ehud::data::validUUID(v.id)&&ids.insert(uuid(v.id)).second&&v.location.valid()&&progress(v.progress)&&v.originalFields.isObject());
}
ReaderPreferences preferences(const J&j){need(j.isObject());ReaderPreferences p;p.originalFields=extra(j,{"fontName","fontSize","lineSpacing","margin","rightToLeft","continuous"});p.originalNumbers=numeric(j,{"fontSize","lineSpacing","margin"});p.fontName=j["fontName"].string();p.fontSize=j["fontSize"].number();p.lineSpacing=j["lineSpacing"].number();p.margin=j["margin"].number();p.rightToLeft=j["rightToLeft"].boolean();p.continuous=j["continuous"].boolean();need(p.valid());return p;}
J encodePreferences(const ReaderPreferences&p){auto j=p.originalFields;j["fontName"]=p.fontName;number(j,"fontSize",p.fontSize,p.originalNumbers);number(j,"lineSpacing",p.lineSpacing,p.originalNumbers);number(j,"margin",p.margin,p.originalNumbers);j["rightToLeft"]=p.rightToLeft;j["continuous"]=p.continuous;return j;}
ReaderBook book(const J&j){need(j.isObject());ReaderBook b;b.originalFields=extra(j,{"id","title","bookmark","path","scoped","referencePlatform","windowsPath","location","progress","bookmarks"});b.id=uuid(j["id"].string());b.title=j["title"].string();
    if(j["referencePlatform"].isNull()){need(!j.contains("windowsPath"));b.bookmarkBase64=j["bookmark"].string();b.path=j["path"].string();b.scoped=j["scoped"].boolean();}
    else{need(j["referencePlatform"]==J("windows")&&!j.contains("bookmark")&&!j.contains("path")&&!j.contains("scoped"));b.platform=ReaderReferencePlatform::windows;b.windowsPath=j["windowsPath"].string();}
    b.location=decodeReaderLocation(j["location"]);b.progress=j["progress"].number();b.originalNumbers=numeric(j,{"progress"});need(j["bookmarks"].array().size()<=readerMaximumBookmarks);
    for(const auto&m:j["bookmarks"].array()){need(m.isObject());b.bookmarks.push_back({uuid(m["id"].string()),decodeReaderLocation(m["location"]),m["progress"].number(),extra(m,{"id","location","progress"}),numeric(m,{"progress"})});}validBook(b);return b;
}
J encodeBook(const ReaderBook&b){auto j=b.originalFields;j["id"]=uuid(b.id);j["title"]=b.title;
    if(b.platform==ReaderReferencePlatform::macOS){j["bookmark"]=b.bookmarkBase64;j["path"]=b.path;j["scoped"]=b.scoped;j.erase("referencePlatform");j.erase("windowsPath");}
    else{j["referencePlatform"]="windows";j["windowsPath"]=b.windowsPath;j.erase("bookmark");j.erase("path");j.erase("scoped");}
    j["location"]=encodeReaderLocation(b.location);number(j,"progress",b.progress,b.originalNumbers);J::Array marks;marks.reserve(b.bookmarks.size());for(const auto&m:b.bookmarks){auto value=m.originalFields;value["id"]=uuid(m.id);value["location"]=encodeReaderLocation(m.location);number(value,"progress",m.progress,m.originalNumbers);marks.push_back(std::move(value));}j["bookmarks"]=std::move(marks);return j;
}
}
ReaderError::ReaderError(ReaderErrorCode code):std::runtime_error(code==ReaderErrorCode::unsupported?"Choose a PDF, EPUB or TXT book.":code==ReaderErrorCode::invalidArchive?"This EPUB archive is damaged or unsafe.":code==ReaderErrorCode::tooLarge?"This book exceeds the reader's safety limits.":code==ReaderErrorCode::unavailable?"The original book is unavailable. Choose it again.":code==ReaderErrorCode::encrypted?"Encrypted books are not supported.":code==ReaderErrorCode::changedOnDisk?"The reader library changed on disk. Restart the app before saving.":"This book could not be read."),code_(code){}
bool ReaderLocation::valid()const noexcept{return section>=0&&section<100000&&block>=0&&block<100000&&character>=0&&character<=64000000&&originalFields.isObject();}
bool ReaderPreferences::operator==(const ReaderPreferences&v)const{return std::tie(fontName,fontSize,lineSpacing,margin,rightToLeft,continuous,originalFields)==std::tie(v.fontName,v.fontSize,v.lineSpacing,v.margin,v.rightToLeft,v.continuous,v.originalFields);}
bool ReaderBookmark::operator==(const ReaderBookmark&v)const{return std::tie(id,location,progress,originalFields)==std::tie(v.id,v.location,v.progress,v.originalFields);}
bool ReaderBook::operator==(const ReaderBook&v)const{return std::tie(id,title,platform,bookmarkBase64,path,scoped,windowsPath,location,progress,bookmarks,originalFields)==std::tie(v.id,v.title,v.platform,v.bookmarkBase64,v.path,v.scoped,v.windowsPath,v.location,v.progress,v.bookmarks,v.originalFields);}
bool ReaderPreferences::valid()const noexcept{return !fontName.empty()&&fontName.size()<=256&&J::validUtf8(fontName)&&std::isfinite(fontSize)&&fontSize>=10&&fontSize<=32&&std::isfinite(lineSpacing)&&lineSpacing>=0&&lineSpacing<=18&&std::isfinite(margin)&&margin>=6&&margin<=48&&originalFields.isObject();}
ReaderPreferences ReaderPreferences::defaults(std::string name){ReaderPreferences p;p.fontName=std::move(name);need(p.valid());return p;}
ReaderLocation decodeReaderLocation(const J&j){need(j.isObject());ReaderLocation r{j["section"].integer(),j["block"].integer(),j["character"].integer(),extra(j,{"section","block","character"})};need(r.valid());return r;}
J encodeReaderLocation(const ReaderLocation&r){need(r.valid());auto j=r.originalFields;j["section"]=r.section;j["block"]=r.block;j["character"]=r.character;return j;}
void validateReaderLibrary(const ReaderLibrary&l){need(l.books.size()<=readerMaximumBooks&&l.preferences.valid()&&l.originalFields.isObject());std::set<std::string,std::less<>>ids;for(const auto&b:l.books){validBook(b);need(ids.insert(uuid(b.id)).second);}if(l.selected)need(ids.contains(uuid(*l.selected)));}
ReaderLibrary decodeReaderLibrary(const J&j){need(j.isObject());const auto version=j["version"].integer();if(version>1)throw ehud::data::StoreError(ehud::data::StoreErrorCode::newerVersion,"Reader library needs a newer app version");need(version==1&&j["books"].array().size()<=readerMaximumBooks);ReaderLibrary l;l.originalFields=extra(j,{"version","books","selected","preferences"});l.preferences=preferences(j["preferences"]);if(!j["selected"].isNull())l.selected=uuid(j["selected"].string());for(const auto&b:j["books"].array())l.books.push_back(book(b));validateReaderLibrary(l);return l;}
J encodeReaderLibrary(const ReaderLibrary&l){validateReaderLibrary(l);auto j=l.originalFields;j["version"]=1;J::Array books;books.reserve(l.books.size());for(const auto&b:l.books)books.push_back(encodeBook(b));j["books"]=std::move(books);j["preferences"]=encodePreferences(l.preferences);if(l.selected)j["selected"]=uuid(*l.selected);else j.erase("selected");(void)j.encode(readerMaximumLibraryBytes);return j;}
ReaderBook windowsReaderReference(std::string id,std::string path,std::string title){ReaderBook b;b.id=uuid(std::move(id));b.title=std::move(title);b.platform=ReaderReferencePlatform::windows;b.windowsPath=std::move(path);validBook(b);return b;}
std::string_view readerReferencePath(const ReaderBook&b)noexcept{return b.platform==ReaderReferencePlatform::windows?std::string_view(b.windowsPath):std::string_view(b.path);}
bool readerSupportedExtension(std::string_view ext)noexcept{if(ext.starts_with('.'))ext.remove_prefix(1);if(ext.size()>4)return false;char value[4]{};for(std::size_t n=0;n<ext.size();++n){const auto c=ext[n];value[n]=c>='A'&&c<='Z'?char(c+32):c;}const std::string_view normalized(value,ext.size());return normalized=="pdf"||normalized=="epub"||normalized=="txt";}
} // namespace endfield::modules
