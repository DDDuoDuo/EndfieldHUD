#include "native/notes_controls_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>

namespace endfield::native {
namespace {
using Json=ehud::data::Json;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
bool hex(std::string_view value,std::size_t size){return value.size()==size&&std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string hash(std::string_view bytes){return core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});}
std::string text(const Json&value,std::size_t maximum=512){need(value.isString(),"Missing prepared Notes asset string");auto result=value.string();need(!result.empty()&&result.size()<=maximum&&Json::validUtf8(result)&&std::none_of(result.begin(),result.end(),[](unsigned char c){return c<32||c==127;}),"Invalid prepared Notes asset string");return result;}
void keys(const Json&value,std::initializer_list<std::string_view>expected){need(value.isObject()&&value.object().size()==expected.size(),"Unexpected prepared Notes asset fields");for(auto key:expected)need(value.contains(key),"Missing prepared Notes asset field");}
std::int64_t integer(const Json&value,std::int64_t minimum,std::int64_t maximum){need(value.isNumber(),"Missing prepared Notes asset integer");const auto n=value.integer();need(n>=minimum&&n<=maximum,"Prepared Notes asset integer exceeds bounds");return n;}
double number(const Json&value){need(value.isNumber(),"Missing prepared Notes asset number");const auto n=value.number();need(std::isfinite(n),"Nonfinite prepared Notes asset number");return n;}
std::filesystem::path confined(const std::filesystem::path&root,std::string_view file){
    need(!file.empty()&&file.size()<=256&&file.find_first_of("\\:\0",0,3)==std::string_view::npos,"Invalid prepared Notes asset path");
    std::filesystem::path result=root;std::size_t start{};
    while(start<file.size()){
        const auto slash=file.find('/',start),end=slash==std::string_view::npos?file.size():slash;const auto part=file.substr(start,end-start);
        need(!part.empty()&&part!="."&&part!="..","Prepared Notes asset path escapes root");
        result/=std::u8string(reinterpret_cast<const char8_t*>(part.data()),part.size());
        need(!std::filesystem::is_symlink(std::filesystem::symlink_status(result)),"Prepared Notes asset symlink is not allowed");start=end+1;
    }
    need(file.back()!='/',"Invalid prepared Notes asset path suffix");return result;
}
std::string read(const std::filesystem::path&root,std::string_view file,std::size_t maximum){auto value=ehud::data::detail::readFile(confined(root,file),maximum);need(value.has_value(),"Missing prepared Notes asset file");return std::move(*value);}
std::uint32_t big32(std::string_view value,std::size_t offset){need(offset<=value.size()&&value.size()-offset>=4,"Truncated prepared PNG integer");const auto*b=reinterpret_cast<const unsigned char*>(value.data()+offset);return (std::uint32_t(b[0])<<24)|(std::uint32_t(b[1])<<16)|(std::uint32_t(b[2])<<8)|b[3];}
std::uint32_t crc32(std::string_view bytes){std::uint32_t c=~std::uint32_t{};for(unsigned char byte:bytes){c^=byte;for(unsigned bit=0;bit<8;++bit)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
void png(std::string_view data){
    need(data.size()>=45&&data.substr(0,8)==std::string_view("\x89PNG\r\n\x1a\n",8),"Prepared Notes raster is not PNG");
    std::size_t at=8;bool header{},pixels{},end{};
    while(at<data.size()){
        need(data.size()-at>=12,"Truncated prepared PNG chunk");const auto size=std::size_t(big32(data,at));
        need(size<=NativeNotesControlsAssets::maximumRasterBytes&&size<=data.size()-at-12,"Invalid prepared PNG chunk bounds");
        const auto kind=data.substr(at+4,4);need(crc32(data.substr(at+4,size+4))==big32(data,at+8+size),"Prepared PNG chunk checksum mismatch");
        if(!header){need(kind=="IHDR"&&size==13&&big32(data,at+8)==36&&big32(data,at+12)==36&&data.substr(at+16,5)==std::string_view("\x08\x06\0\0\0",5),"Prepared Notes PNG must be exact 36x36 RGBA8");header=true;}
        else need(kind!="IHDR","Duplicate prepared PNG header");
        if(kind=="IDAT"){need(size>0,"Empty prepared PNG pixel stream");pixels=true;}
        if(kind=="IEND"){need(size==0&&pixels&&at+12==data.size(),"Prepared PNG has missing pixels or trailing bytes");end=true;break;}
        need((static_cast<unsigned char>(kind[0])&32u)!=0||kind=="IHDR"||kind=="IDAT"||kind=="PLTE","Unsupported prepared PNG critical chunk");at+=size+12;
    }
    need(header&&pixels&&end,"Incomplete prepared Notes PNG");
}
bool same(const modules::NotesControlsImage&a,const modules::NotesControlsImage&b)noexcept{return a.layerID==b.layerID&&a.sourceResource==b.sourceResource&&a.rect==b.rect&&a.tint==b.tint&&a.requestedPixels==b.requestedPixels&&a.sourceInTint==b.sourceInTint&&a.resizeAspect==b.resizeAspect;}
constexpr std::array<std::string_view,2>layerIDs{"tool:text/icon","tool:todo/icon"};
constexpr std::array<std::string_view,2>resourceNames{"Operational_Manual_icon","Mission_Icon"};
constexpr std::array<std::string_view,2>sourceIDs{"notes/default/host/0/0/2/0/1","notes/default/host/0/0/2/1/1"};
}
NativeNotesControlsAssets::NativeNotesControlsAssets(std::filesystem::path root,NativeNotesControlsAssetPins expected):root_(std::move(root)){
    ehud::data::detail::validateRoot(root_);need(hex(expected.manifestSHA256,64)&&hex(expected.sourceCommit,40),"Prepared Notes assets require explicit caller manifest/source pins");
    const auto bytes=read(root_,"notes-controls-assets.json",maximumManifestBytes);manifestSHA256_=hash(bytes);
    need(manifestSHA256_==expected.manifestSHA256,"Prepared Notes manifest differs from its caller pin");const auto manifest=Json::parse(bytes,maximumManifestBytes);
    keys(manifest,{"format","schemaVersion","preparedFor","images","source","sourcePins","scope"});
    need(text(manifest["format"])=="endfield-notes-controls-assets"&&integer(manifest["schemaVersion"],1,1)==1,"Unsupported prepared Notes asset format");
    keys(manifest["preparedFor"],{"theme","contentsScale"});need(text(manifest["preparedFor"]["theme"])=="dark"&&number(manifest["preparedFor"]["contentsScale"])==2,"Unsupported prepared Notes asset theme or scale");
    keys(manifest["source"],{"release","build","commit"});need(text(manifest["source"]["release"],32)=="v1.2.0"&&integer(manifest["source"]["build"],18,18)==18,"Prepared Notes asset source is not the authoritative Mac release");
    sourceCommit_=text(manifest["source"]["commit"],40);need(sourceCommit_==expected.sourceCommit,"Prepared Notes assets have different source lineage");(void)text(manifest["scope"],512);
    const auto&pins=manifest["sourcePins"];keys(pins,{"moduleSHA256","manifestSHA256","provenanceSHA256","authoritySHA256","sourceAndResourceSHA256"});
    for(auto key:{"moduleSHA256","manifestSHA256","provenanceSHA256","authoritySHA256"})need(hex(text(pins[key],64),64),"Invalid prepared Notes source metadata hash");
    const auto&sources=pins["sourceAndResourceSHA256"];keys(sources,{"Sources/NotesCanvas.swift","Sources/EndfieldGameIcon.swift","Resources/AppIconSources/EndfieldWiki/Operational_Manual_icon.png","Resources/AppIconSources/EndfieldWiki/Mission_Icon.png"});
    for(const auto&[relative,sha]:sources.object()){(void)relative;need(hex(text(sha,64),64),"Invalid prepared Notes source resource hash");}
    need(manifest["images"].isArray()&&manifest["images"].array().size()==2,"Prepared Notes assets must contain exactly two original icons");
    std::set<std::string,std::less<>>files;
    for(std::size_t n=0;n<2;++n){const auto&row=manifest["images"].array()[n];keys(row,{"dependency","contents","sourceLayerID","raster"});
        need(text(row["sourceLayerID"])==sourceIDs[n],"Prepared Notes icon source node differs");const auto&d=row["dependency"];
        keys(d,{"layerID","sourceResource","rect","tint","requestedPixels","sourceInTint","resizeAspect"});auto&image=images_[n];auto&dependency=image.dependency;
        dependency.layerID=text(d["layerID"]);dependency.sourceResource=text(d["sourceResource"]);
        need(dependency.layerID==layerIDs[n]&&dependency.sourceResource=="AppIconSources/EndfieldWiki/"+std::string(resourceNames[n])+".png","Prepared Notes icon identity differs");
        need(d["rect"].isArray()&&d["rect"].array().size()==4&&d["tint"].isArray()&&d["tint"].array().size()==4,"Invalid prepared Notes icon geometry or tint");
        const auto&r=d["rect"].array();dependency.rect={number(r[0]),number(r[1]),number(r[2]),number(r[3])};
        for(std::size_t c=0;c<4;++c)dependency.tint[c]=number(d["tint"].array()[c]);dependency.requestedPixels=static_cast<unsigned>(integer(d["requestedPixels"],36,36));
        need(d["sourceInTint"].isBool()&&d["sourceInTint"].boolean()&&d["resizeAspect"].isBool()&&d["resizeAspect"].boolean(),"Prepared Notes icon operation differs");dependency.sourceInTint=dependency.resizeAspect=true;
        need(dependency.rect==core::Rect{9,5,18,18}&&dependency.tint==modules::NotesColor{.94,.94,.94,1},"Prepared Notes icon geometry/tint is not the exact source variant");
        keys(row["contents"],{"asset","sha256"});keys(row["raster"],{"file","sha256","bytes","width","height"});
        const auto file=text(row["contents"]["asset"]),digest=text(row["contents"]["sha256"],64);
        need(hex(digest,64)&&file=="raster/"+digest+".png"&&files.insert(file).second,"Invalid or duplicate prepared Notes raster identity");
        const auto&raster=row["raster"];need(text(raster["file"])==file&&text(raster["sha256"],64)==digest&&integer(raster["width"],36,36)==36&&integer(raster["height"],36,36)==36,"Prepared Notes raster metadata differs from its icon");
        const auto size=static_cast<std::size_t>(integer(raster["bytes"],45,maximumRasterBytes));const auto data=read(root_,file,size);
        need(data.size()==size&&hash(data)==digest,"Prepared Notes raster integrity mismatch");png(data);image.contents=row["contents"];
    }
}
std::span<const NativeNotesControlsImage>NativeNotesControlsAssets::imagesFor(const modules::NotesControls&source)const{
    need(source.contentRevision()!=0,"Initialize Notes controls before requesting prepared assets");const auto requested=source.images();if(requested.empty())return {};
    need(requested.size()==images_.size(),"Prepared Notes assets do not cover this source image request");
    for(std::size_t n=0;n<images_.size();++n)need(same(requested[n],images_[n].dependency),"Prepared Notes assets differ from the exact source dependency");return images_;
}
} // namespace endfield::native
