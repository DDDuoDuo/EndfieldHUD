#include "native/media_assembly_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {namespace {
using Filter=modules::MediaAssemblyFilter;
void need(bool value,const char*why){if(!value)throw std::invalid_argument(why);}
std::string hash(std::string_view bytes){return core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});}
std::size_t filterIndex(Filter kind){const auto index=static_cast<std::size_t>(kind);need(index<modules::mediaAssemblyFilters().size(),"Unknown original Media Assembly filter");return index;}
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
    ehud::data::detail::validateRoot(root_);const auto bytes=ehud::data::detail::readFile(root_/"catalog.json",24*1024);
    need(bytes&&hash(*bytes)==catalogSHA256,"Missing or changed original Media Assembly catalog");
    const auto catalog=ehud::data::Json::parse(*bytes,24*1024);const auto&rows=catalog["files"].array();
    need(catalog["version"].integer()==1&&rows.size()==assets_.size(),"Invalid Media Assembly asset catalog");
    for(std::size_t index=0;index<rows.size();++index){
        const auto&row=rows[index];auto&asset=assets_[index];asset={row["path"].string(),row["sha256"].string(),static_cast<std::size_t>(row["bytes"].integer())};
        const auto expected=index<14?"luts/"+std::string(modules::mediaAssemblyFilters()[index+1].id)+".rgb8":index<28?"filter-icons/"+std::string(modules::mediaAssemblyFilters()[index-13].id)+".png":"stickers/"+std::string(modules::mediaAssemblyStickers()[index-28].id)+".png";
        need(asset.path==expected&&asset.bytes>0&&asset.bytes<=1024*1024,"Unexpected Media Assembly asset path or extent");
        if(index<14)need(asset.bytes==MediaAssemblyCube::byteCount,"Original cube must have 32 cubed RGB nodes");
    }
}
const MediaAssemblyAsset&NativeMediaAssemblyAssets::filterThumbnail(Filter kind)const{const auto index=filterIndex(kind);need(index>0,"None has no filter thumbnail");return assets_[13+index];}
const MediaAssemblyAsset&NativeMediaAssemblyAssets::sticker(modules::MediaAssemblyStickerKind kind)const{const auto index=static_cast<std::size_t>(kind);need(index<24,"Unknown original Media Assembly sticker");return assets_[28+index];}
std::shared_ptr<const MediaAssemblyCube>NativeMediaAssemblyAssets::cube(Filter kind){
    const auto index=filterIndex(kind);if(!index)return {};
    for(std::size_t n=0;n<cubes_.size();++n)if(cubes_[n].cube&&cubes_[n].kind==kind){++hits_;if(n==1)std::swap(cubes_[0],cubes_[1]);return cubes_[0].cube;}
    const auto&asset=assets_[index-1];const auto bytes=ehud::data::detail::readFile(root_/asset.path,MediaAssemblyCube::byteCount);
    need(bytes&&bytes->size()==asset.bytes&&hash(*bytes)==asset.sha256,"Missing or changed original Media Assembly cube");
    auto result=std::make_shared<MediaAssemblyCube>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()),bytes->size()));
    cubes_[1]=std::move(cubes_[0]);cubes_[0]={kind,result};++reads_;return result;
}
void NativeMediaAssemblyAssets::clear()noexcept{cubes_={};}
MediaAssemblyAssetStats NativeMediaAssemblyAssets::stats()const noexcept{MediaAssemblyAssetStats result;result.cubeReads=reads_;result.cubeHits=hits_;for(const auto&item:cubes_)if(item.cube){++result.retainedCubes;result.retainedBytes+=MediaAssemblyCube::byteCount;}return result;}
}
