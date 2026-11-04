#include "native/map_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>

namespace n=endfield::native;namespace c=endfield::core;using J=ehud::data::Json;
namespace {
unsigned checks{};
constexpr auto sourceCommit="0290f8eb3f1bf2f0329fd776e0556803c813ec37";
constexpr auto sourceManifest="72a2f41cbf39d04c3852a09e9505227630be6a4f1caa7e74fd1714ccb3b8b11b";
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&&fn,const char*why){bool rejected{};try{fn();}catch(const std::exception&){rejected=true;}check(rejected,why);}
std::string hash(std::string_view data){return c::packet::sha256({reinterpret_cast<const std::uint8_t*>(data.data()),data.size()});}
std::string read(const std::filesystem::path&path){auto bytes=ehud::data::detail::readFile(path,4*1024*1024);check(bool(bytes),"Explicit fixture exists");return std::move(*bytes);}
void put(const std::filesystem::path&path,std::string_view data){std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write(data.data(),std::streamsize(data.size()));check(bool(f),"Synthetic fixture writes completely");}
struct Fixture {
    std::filesystem::path root=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path())/("endfield-map-assets-"+ehud::data::makeUUID());
    J manifest;std::array<std::string,3>payload;const std::array<const char*,3>files{"halo.rgba","beam.rgba","glyph.rgba"};
    explicit Fixture(const std::filesystem::path&original):manifest(J::parse(read(original/"map-player-assets.json"),32768)){
        std::filesystem::create_directory(root);for(std::size_t k=0;k<3;++k)payload[k]=read(original/files[k]);save();
    }
    ~Fixture(){std::error_code e;std::filesystem::remove_all(root,e);}
    n::MapPlayerAssetPins pins()const{return {hash(manifest.encode()),sourceCommit};}
    void save(){put(root/"map-player-assets.json",manifest.encode());for(std::size_t k=0;k<3;++k)put(root/files[k],payload[k]);}
    void reject(){save();rejects([&]{n::loadMapPlayerImages(root,pins());},"Malformed source image contract rejects atomically");}
};
void original(const std::filesystem::path&root){
    const auto manifest=J::parse(read(root/"map-player-assets.json"),32768);
    const auto images=n::loadMapPlayerImages(root,{sourceManifest,sourceCommit});
    const std::array borrowed{images.halo,images.beam,images.glyph};
    const std::array<unsigned,3>w{191,256,90},h{208,256,92};
    for(std::size_t k=0;k<3;++k){const auto&image=*borrowed[k];check(image.width==w[k]&&image.height==h[k]&&image.straightRGBA.size()==std::size_t(w[k])*h[k]*4,"Original artwork dimensions are not resampled");check(c::packet::sha256(image.straightRGBA)==manifest["images"].array()[k]["sha256"].string(),"Native source image bytes exactly match original exported CGImage");}
    bool transparentBlack=true,hasFeather=false;for(std::size_t p=0;p<images.beam->straightRGBA.size();p+=4){const auto a=images.beam->straightRGBA[p+3];if(!a)for(unsigned channel=0;channel<3;++channel)transparentBlack&=images.beam->straightRGBA[p+channel]==0;else if(a<255)hasFeather=true;}
    check(transparentBlack&&hasFeather,"Actual original beam tint becomes straight color with meaningful fractional alpha");
}
void malformed(const std::filesystem::path&originalRoot){
    Fixture f(originalRoot);const auto pristine=f.manifest;const auto initial=f.payload;
    const auto reset=[&]{f.manifest=pristine;f.payload=initial;};
    const auto imageField=[&](std::size_t index,const char*key,J value){auto rows=f.manifest["images"].array();rows[index][key]=std::move(value);f.manifest["images"]=std::move(rows);};
    const auto validPins=f.pins();const auto keep=n::loadMapPlayerImages(f.root,validPins);check(keep.halo&&keep.beam&&keep.glyph,"Complete prepared bundle loads without native graphics services");
    auto wrong=validPins;wrong.manifestSHA256[0]=wrong.manifestSHA256[0]=='0'?'1':'0';rejects([&]{n::loadMapPlayerImages(f.root,wrong);},"Caller hash is independent of manifest contents");
    wrong=validPins;wrong.sourceCommit=std::string(40,'b');rejects([&]{n::loadMapPlayerImages(f.root,wrong);},"Source lineage is caller pinned");
    rejects([&]{n::loadMapPlayerImages("relative",validPins);},"Relative package lookup is forbidden");
    std::filesystem::remove(f.root/"beam.rgba");rejects([&]{n::loadMapPlayerImages(f.root,validPins);},"Missing source image rejects complete publication");check(keep.beam->width==256,"Already published immutable artwork survives package retirement");f.save();
    for(const auto key:{"width","height","bytes"}){reset();imageField(0,key,0);f.reject();}
    reset();imageField(0,"width",191.5);f.reject();
    reset();imageField(0,"file","../halo.rgba");f.reject();
    reset();imageField(1,"resource",f.manifest["images"].array()[0]["resource"]);f.reject();
    reset();imageField(1,"premultipliedRoundTripMaximum",1);f.reject();
    reset();f.payload[0][17]^=1;f.reject();reset();f.payload[0].pop_back();f.reject();reset();f.payload[0].push_back('x');f.reject();
    reset();f.manifest["feather"]=J::Object{};f.reject(); // v1.2.0's feather is not part of the bundle any more
    reset();f.manifest["sourcePins"].object().erase("Sources/WorldMapPinArtwork.swift");f.reject();
    reset();{auto rows=f.manifest["images"].array();rows.pop_back();f.manifest["images"]=std::move(rows);}f.reject();
    reset();f.manifest["pixelFormat"]="premultiplied-RGBA";f.reject();
    reset();f.save();const auto restored=n::loadMapPlayerImages(f.root,f.pins());check(restored.beam->straightRGBA==keep.beam->straightRGBA,"Rejected input never mutates previously published source images");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass explicit prepared map-player package root");const auto root=std::filesystem::weakly_canonical(argv[1]);original(root);malformed(root);std::cout<<"Map assets: "<<checks<<" checks passed (source CGImages; no window or user data)\n";return 0;}catch(const std::exception&e){std::cerr<<"Map assets failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
