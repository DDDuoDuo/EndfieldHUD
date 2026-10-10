#include "native/map_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace endfield::native {
namespace {
using J=ehud::data::Json;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
bool hex(std::string_view value,std::size_t length){return value.size()==length&&std::all_of(value.begin(),value.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string hash(std::string_view data){return core::packet::sha256({reinterpret_cast<const std::uint8_t*>(data.data()),data.size()});}
std::string read(const std::filesystem::path&root,const char*file,std::size_t maximum){auto data=ehud::data::detail::readFile(root/file,maximum);need(bool(data),"Missing prepared Map player asset");return std::move(*data);}
void keys(const J&value,std::initializer_list<std::string_view>expected){need(value.isObject()&&value.object().size()==expected.size(),"Unexpected Map asset fields");for(auto key:expected)need(value.contains(key),"Missing Map asset field");}
bool text(const J&value,std::string_view expected){return value.isString()&&value.string()==expected;}
bool integer(const J&value,std::int64_t expected){return value.isNumber()&&value.number()==double(expected);}
constexpr std::array names{"halo","beam","glyph"};
constexpr std::array files{"halo.rgba","beam.rgba","glyph.rgba"};
constexpr std::array<unsigned,3> widths{191,256,90},heights{208,256,92};
}
MapPlayerImages loadMapPlayerImages(const std::filesystem::path&root,const MapPlayerAssetPins&pins){
    ehud::data::detail::validateRoot(root);
    need(hex(pins.manifestSHA256,64)&&hex(pins.sourceCommit,40),"Map player assets require explicit caller manifest and source pins");
    const auto bytes=read(root,"map-player-assets.json",32*1024);
    need(hash(bytes)==pins.manifestSHA256,"Prepared Map player manifest differs from caller pin");
    const auto manifest=J::parse(bytes,32*1024);
    keys(manifest,{"format","schemaVersion","sourceCommit","pixelFormat","sourcePins","images","maskProof","feather","colors"});
    need(text(manifest["format"],"endfield-map-player-assets")&&integer(manifest["schemaVersion"],1)&&text(manifest["pixelFormat"],"straight-RGBA8-sRGB-top-left"),"Unsupported Map player asset format");
    need(text(manifest["sourceCommit"],pins.sourceCommit),"Map player asset source lineage differs");
    const auto&sourcePins=manifest["sourcePins"];
    need(sourcePins.isObject()&&sourcePins.object().size()==6,"Map player asset source pins are incomplete");
    const auto dependencies=modules::mapPlayerAssets();
    for(const auto*file:{"Sources/WorldMapPinArtwork.swift","Sources/WorldMapCanvas.swift","Sources/WorldMapGeometry.swift"})
        need(sourcePins[file].isString()&&hex(sourcePins[file].string(),64),"Map player artwork source hash is missing");
    for(const auto&dependency:dependencies){const auto key="Resources/"+std::string(dependency.path);need(sourcePins[key].isString()&&hex(sourcePins[key].string(),64),"Map player original resource hash is missing");}
    const auto&images=manifest["images"];need(images.isArray()&&images.array().size()==3,"Map player assets must contain three source images");
    std::array<std::shared_ptr<const MapPaintImage>,3>result;
    for(std::size_t n=0;n<result.size();++n){const auto&row=images.array()[n];
        keys(row,{"name","width","height","bytes","premultipliedRoundTripMaximum","file","resource","sha256"});
        const auto count=std::size_t(widths[n])*heights[n]*4;
        need(text(row["name"],names[n])&&text(row["file"],files[n])&&text(row["resource"],dependencies[n].path),"Map player image identity differs from original dependency");
        need(integer(row["width"],widths[n])&&integer(row["height"],heights[n])&&integer(row["bytes"],std::int64_t(count))&&integer(row["premultipliedRoundTripMaximum"],0),"Map player image size or source conversion differs");
        need(row["sha256"].isString()&&hex(row["sha256"].string(),64),"Invalid Map player image hash");
        const auto pixels=read(root,files[n],count);need(pixels.size()==count&&hash(pixels)==row["sha256"].string(),"Map player image integrity mismatch");
        auto image=std::make_shared<MapPaintImage>();image->width=widths[n];image->height=heights[n];
        image->straightRGBA.assign(reinterpret_cast<const std::uint8_t*>(pixels.data()),reinterpret_cast<const std::uint8_t*>(pixels.data())+pixels.size());
        result[n]=std::move(image);
    }
    const auto&f=manifest["feather"];keys(f,{"file","encoding","width","height","scale","bytes","sha256","decodedSHA256"});
    constexpr std::size_t pixelCount=880*880;
    need(text(f["file"],"feather.alpha-rle")&&text(f["encoding"],"u16le-count-u8-alpha")&&integer(f["width"],880)&&integer(f["height"],880)&&integer(f["scale"],2),"Map feather differs from original 2x source mask");
    need(f["sha256"].isString()&&hex(f["sha256"].string(),64)&&f["decodedSHA256"].isString()&&hex(f["decodedSHA256"].string(),64),"Map feather integrity pins are missing");
    const auto packed=read(root,"feather.alpha-rle",pixelCount*3);
    need(integer(f["bytes"],std::int64_t(packed.size()))&&!packed.empty()&&packed.size()%3==0&&hash(packed)==f["sha256"].string(),"Map feather RLE integrity mismatch");
    const auto run=[&](std::size_t at){return std::size_t(static_cast<unsigned char>(packed[at]))|(std::size_t(static_cast<unsigned char>(packed[at+1]))<<8);};
    std::size_t total{};for(std::size_t at=0;at<packed.size();at+=3){const auto count=run(at);need(count&&count<=pixelCount-total,"Map feather RLE exceeds original image bounds");total+=count;}
    need(total==pixelCount,"Map feather RLE is incomplete");
    std::vector<std::uint8_t>alpha(pixelCount);total=0;
    for(std::size_t at=0;at<packed.size();at+=3){const auto count=run(at);std::fill_n(alpha.data()+total,count,static_cast<std::uint8_t>(packed[at+2]));total+=count;}
    need(core::packet::sha256(alpha)==f["decodedSHA256"].string(),"Map feather decoded pixels differ from source");
    auto feather=std::make_shared<MapPaintImage>();feather->width=feather->height=880;feather->straightRGBA.resize(pixelCount*4,255);
    for(std::size_t p=0;p<pixelCount;++p)feather->straightRGBA[p*4+3]=alpha[p];
    return {std::move(result[0]),std::move(result[1]),std::move(result[2]),std::move(feather)};
}
}
