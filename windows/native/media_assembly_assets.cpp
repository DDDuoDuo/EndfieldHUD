#include "native/media_assembly_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace endfield::native {namespace {
using Filter=modules::MediaAssemblyFilter;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
std::string hash(std::string_view bytes){return core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});}
}
MediaAssemblyCube::MediaAssemblyCube(std::span<const std::uint8_t>bytes){need(bytes.size()==byteCount,"Invalid original Media Assembly cube size");bytes_.assign(bytes.begin(),bytes.end());}
std::array<float,3>MediaAssemblyCube::sample(std::array<float,3>input)const{
    std::array<unsigned,3>low{},high{};std::array<float,3>fraction{},result{};
    for(unsigned k=0;k<3;++k){need(std::isfinite(input[k]),"Nonfinite Media Assembly cube input");const float at=std::clamp(input[k],0.f,1.f)*31;low[k]=unsigned(at);high[k]=std::min(31u,low[k]+1);fraction[k]=at-float(low[k]);}
    for(unsigned z=0;z<2;++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){
        const auto index=3*((z?high[2]:low[2])*32*32+(y?high[1]:low[1])*32+(x?high[0]:low[0]));
        const float weight=(x?fraction[0]:1-fraction[0])*(y?fraction[1]:1-fraction[1])*(z?fraction[2]:1-fraction[2]);
        for(unsigned k=0;k<3;++k)result[k]+=weight*(float(bytes_[index+k])/255.f);
    }return result;
}
NativeMediaAssemblyAssets::NativeMediaAssemblyAssets(std::filesystem::path root):root_(std::move(root)){
    ehud::data::detail::validateRoot(root_);const auto bytes=ehud::data::detail::readFile(root_/"provenance.json",maximumProvenanceBytes);
    need(bytes&&hash(*bytes)==provenanceSHA256,"Missing or changed original Media Assembly provenance");
    const auto provenance=ehud::data::Json::parse(*bytes,maximumProvenanceBytes);const auto&rows=provenance["files"].array();
    need(provenance["formatVersion"].integer()==1&&rows.size()==filters_.size()*2+stickers_.size(),"Media Assembly provenance lists files outside the source catalog");
    // Every listed file is claimed once by the case whose raw id names it.
    std::vector<bool>claimed(rows.size());
    const auto row=[&](const std::string&path)->const ehud::data::Json&{
        for(std::size_t n=0;n<rows.size();++n)if(rows[n]["file"].string()==path){need(!claimed[n],"Duplicate Media Assembly provenance entry");claimed[n]=true;return rows[n];}
        throw std::invalid_argument("Media Assembly provenance lacks a source catalog asset");};
    const auto asset=[&](const ehud::data::Json&r){MediaAssemblyAsset a{r["file"].string(),r["sha256"].string(),static_cast<std::size_t>(r["bytes"].integer())};
        need(a.sha256.size()==64&&a.sha256.find_first_not_of("0123456789abcdef")==std::string::npos&&a.bytes>0&&a.bytes<=maximumAssetBytes,"Unexpected Media Assembly asset extent or hash");return a;};
    const auto edge=[](const ehud::data::Json&r,double w,double h){need(r["width"].integer()==static_cast<std::int64_t>(w)&&r["height"].integer()==static_cast<std::int64_t>(h),"Media Assembly artwork canvas differs from its source size");};
    std::size_t f{};for(const auto&item:modules::mediaAssemblyFilters()){
        const auto kind=modules::mediaAssemblyFilter(item.id);need(bool(kind),"Unknown original Media Assembly filter");if(*kind==Filter::none)continue;
        const auto&lut=row("luts/"+std::string(item.id)+".rgb8");const auto&icon=row("filter-icons/"+std::string(item.id)+".png");
        filters_[f]={*kind,asset(lut),asset(icon)};need(filters_[f].lut.bytes==MediaAssemblyCube::byteCount&&lut["dimension"].integer()==static_cast<std::int64_t>(MediaAssemblyCube::dimension),"Original cube must have 32 cubed RGB nodes");
        edge(icon,168,168);++f;
    }
    std::size_t s{};for(const auto&item:modules::mediaAssemblyStickers()){
        const auto kind=modules::mediaAssemblySticker(item.id);need(bool(kind),"Unknown original Media Assembly sticker");const auto&r=row("stickers/"+std::string(item.id)+".png");
        stickers_[s]={*kind,asset(r)};const auto size=modules::mediaAssemblyStickerPixelSize(*kind);edge(r,size.x,size.y);++s;
    }
    need(f==filters_.size()&&s==stickers_.size(),"Incomplete Media Assembly source catalog");
}
const NativeMediaAssemblyAssets::FilterAssets&NativeMediaAssemblyAssets::filter(Filter kind)const{
    const auto at=std::find_if(filters_.begin(),filters_.end(),[=](const auto&a){return a.kind==kind;});need(at!=filters_.end(),"Unknown original Media Assembly filter");return *at;}
const MediaAssemblyAsset&NativeMediaAssemblyAssets::filterThumbnail(Filter kind)const{need(kind!=Filter::none,"None has no filter thumbnail");return filter(kind).icon;}
const MediaAssemblyAsset&NativeMediaAssemblyAssets::sticker(modules::MediaAssemblyStickerKind kind)const{
    const auto at=std::find_if(stickers_.begin(),stickers_.end(),[=](const auto&a){return a.kind==kind;});need(at!=stickers_.end(),"Unknown original Media Assembly sticker");return at->image;}
std::shared_ptr<const MediaAssemblyCube>NativeMediaAssemblyAssets::cube(Filter kind){
    if(kind==Filter::none)return {};const auto&asset=filter(kind).lut;
    for(std::size_t n=0;n<cubes_.size();++n)if(cubes_[n].cube&&cubes_[n].kind==kind){++hits_;if(n==1)std::swap(cubes_[0],cubes_[1]);return cubes_[0].cube;}
const auto bytes=ehud::data::detail::readFile(root_/asset.path,MediaAssemblyCube::byteCount);
    need(bytes&&bytes->size()==asset.bytes&&hash(*bytes)==asset.sha256,"Missing or changed original Media Assembly cube");
    auto result=std::make_shared<MediaAssemblyCube>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()));
    cubes_[1]=std::move(cubes_[0]);cubes_[0]={kind,result};++reads_;return result;
}
void NativeMediaAssemblyAssets::clear()noexcept{cubes_={};}
MediaAssemblyAssetStats NativeMediaAssemblyAssets::stats()const noexcept{MediaAssemblyAssetStats result;result.cubeReads=reads_;result.cubeHits=hits_;for(const auto&item:cubes_)if(item.cube){++result.retainedCubes;result.retainedBytes+=MediaAssemblyCube::byteCount;}return result;}
}
