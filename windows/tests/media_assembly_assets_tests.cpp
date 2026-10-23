#include "native/media_assembly_assets.hpp"
#include "core/data/file_io.hpp"
#include "core/shell_packet.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <utility>
#include <vector>
namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace n=endfield::native;namespace m=endfield::modules;using J=ehud::data::Json;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&&f,const char*why){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
std::string read(const std::filesystem::path&path,std::size_t limit=1024*1024){const auto data=ehud::data::detail::readFile(path,limit);check(bool(data),"Read only explicitly supplied fixture");return *data;}
struct Temporary {std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("endfield-media-assets-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Temporary(){std::filesystem::create_directory(root);}~Temporary(){std::error_code ec;std::filesystem::remove_all(root,ec);}};
void write(const std::filesystem::path&path,std::string_view bytes){std::ofstream file(path,std::ios::binary);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(file),"Write owned synthetic fixture only");}
void run(const std::filesystem::path&root,const std::filesystem::path&fixture){
    n::NativeMediaAssemblyAssets assets(root);check(assets.stats().retainedBytes==0&&assets.stats().cubeReads==0,"Catalog construction never eagerly loads LUTs or artwork");
    const auto source=J::parse(read(fixture),1024*1024);check(source["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1"&&!source["usesAppOrWindow"].boolean(),"Original detached Core Image source oracle");
    float maxDelta{};for(const auto&row:source["rows"].array()){
        const auto filter=m::mediaAssemblyFilter(row["filter"].string());check(filter&&*filter!=m::MediaAssemblyFilter::none,"Source preset remains allow-listed");const auto cube=assets.cube(*filter);
        const auto out=cube->sample({float(row["input"].array()[0].number()),float(row["input"].array()[1].number()),float(row["input"].array()[2].number())});for(unsigned k=0;k<3;++k){const auto delta=std::abs(out[k]-float(row["output"].array()[k].number()));maxDelta=std::max(maxDelta,delta);check(delta<=.001f,"RGB8 interpolation matches actual original Core Image within quarter byte");}
        check(assets.stats().retainedCubes<=2&&assets.stats().retainedBytes<=2*n::MediaAssemblyCube::byteCount,"Only two original LUTs retained");
    }
    // The shared Mac provenance.json is the file list: pinned, every listed
    // file is exact, and it lists luts, then stickers, then icons, each in the
    // source allCases order (an independent check of the model's grid order).
    const auto provenanceBytes=read(root/"provenance.json",n::NativeMediaAssemblyAssets::maximumProvenanceBytes);check(endfield::core::packet::sha256({reinterpret_cast<const std::uint8_t*>(provenanceBytes.data()),provenanceBytes.size()})==n::NativeMediaAssemblyAssets::provenanceSHA256,"Pinned 1.3.0 Mac provenance");
    const auto provenance=J::parse(provenanceBytes,n::NativeMediaAssemblyAssets::maximumProvenanceBytes);std::size_t total{};std::vector<std::string>luts,icons,stickers;
    for(const auto&record:provenance["files"].array()){const auto path=record["file"].string();const auto bytes=read(root/path);check(bytes.size()==static_cast<std::size_t>(record["bytes"].integer())&&endfield::core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()})==record["sha256"].string(),"Every deployed original asset retains exact bytes");total+=bytes.size();
        const auto slash=path.find('/'),dot=path.rfind('.');const auto group=path.substr(0,slash),id=path.substr(slash+1,dot-slash-1);(group=="luts"?luts:group=="filter-icons"?icons:stickers).push_back(id);}
    check(provenance["files"].array().size()==60&&total==3452423,"All 60 active PhotoMode artwork and filters fit in 3.3MiB");
    const auto filters=m::mediaAssemblyFilters();const auto kinds=m::mediaAssemblyStickers();check(luts.size()+1==filters.size()&&icons.size()+1==filters.size()&&stickers.size()==kinds.size(),"Exactly one LUT and icon per filter and one PNG per sticker");
    for(std::size_t i=1;i<filters.size();++i){check(luts[i-1]==filters[i].id&&icons[i-1]==filters[i].id,"Provenance filter order is the source allCases order");const auto filter=m::mediaAssemblyFilter(filters[i].id);check(filter&&assets.filterThumbnail(*filter).path=="filter-icons/"+std::string(filters[i].id)+".png","Filter thumbnail resolves by its source raw id");}
    for(std::size_t i=0;i<kinds.size();++i){check(stickers[i]==kinds[i].id,"Provenance sticker order is the source allCases order");const auto kind=m::mediaAssemblySticker(kinds[i].id);const auto&asset=assets.sticker(*kind);check(asset.path=="stickers/"+std::string(kinds[i].id)+".png","Sticker resolves by its source raw id");
        for(const auto&record:provenance["files"].array())if(record["file"].string()==asset.path){const auto size=m::mediaAssemblyStickerPixelSize(*kind);check(record["width"].integer()==static_cast<std::int64_t>(size.x)&&record["height"].integer()==static_cast<std::int64_t>(size.y)&&record["sha256"].string()==asset.sha256,"Sticker canvas is the source pixelSize");}}
    // 1.3.0 inserted these mid-list; every neighbour keeps its own file.
    check(assets.filterThumbnail(m::MediaAssemblyFilter::special7).path=="filter-icons/sp_filter_7.png"&&assets.filterThumbnail(m::MediaAssemblyFilter::special6).path=="filter-icons/sp_filter_6.png"&&assets.filterThumbnail(m::MediaAssemblyFilter::filter1).path=="filter-icons/filter_1.png","Wuling Impression sits between Blood Memory and Bright");
    for(const auto&[kind,id]:std::array<std::pair<m::MediaAssemblyStickerKind,std::string_view>,8>{{{m::MediaAssemblyStickerKind::sticker29,"sticker_bp_7"},{m::MediaAssemblyStickerKind::sticker7,"sticker_1"},{m::MediaAssemblyStickerKind::sticker30,"sticker_activity_5"},{m::MediaAssemblyStickerKind::sticker31,"sticker_channel_v1d6_1"},
        {m::MediaAssemblyStickerKind::sticker27,"sticker_birthday_2026"},{m::MediaAssemblyStickerKind::sticker32,"sticker_giftpack_3"},{m::MediaAssemblyStickerKind::sticker33,"sticker_giftpack_4"},{m::MediaAssemblyStickerKind::sticker34,"sticker_giftpack_5"}}})
        check(assets.sticker(kind).path=="stickers/"+std::string(id)+".png"&&m::mediaAssemblyStickerPixelSize(kind).x==(kind==m::MediaAssemblyStickerKind::sticker7||kind==m::MediaAssemblyStickerKind::sticker27?320:640),"Inserted 1.6 stickers and their neighbours resolve to their own 640/320 canvases");
    check(!m::mediaAssemblySticker("sticker_giftpack_2")&&!m::mediaAssemblySticker("sticker_23")&&!m::mediaAssemblyFilter("sp_filter_8"),"Removed promotional stickers and unknown presets stay absent");
    const auto cube7=assets.cube(m::MediaAssemblyFilter::special7);check(cube7&&assets.stats().retainedCubes<=2,"New Wuling Impression LUT loads through its pinned hash");
    check(!assets.cube(m::MediaAssemblyFilter::none),"None requests no cube");rejects([&]{assets.filterThumbnail(m::MediaAssemblyFilter::none);},"None has no fabricated thumbnail");rejects([&]{assets.cube(static_cast<m::MediaAssemblyFilter>(99));},"Invalid enum cannot become an arbitrary path");rejects([&]{assets.sticker(static_cast<m::MediaAssemblyStickerKind>(99));},"Invalid sticker rejected");
    auto retained=assets.cube(m::MediaAssemblyFilter::special1);const auto saved=retained->sample({.173f,.719f,.233f});assets.cube(m::MediaAssemblyFilter::special2);assets.cube(m::MediaAssemblyFilter::special3);assets.clear();check(assets.stats().retainedBytes==0&&retained->sample({.173f,.719f,.233f})==saved,"Eviction and hide release cache without invalidating in-flight borrower");
    const auto warm=assets.cube(m::MediaAssemblyFilter::special1);const auto before=assets.stats();allocations=0;counting=true;float sum{};for(unsigned i=0;i<10000;++i){const auto same=assets.cube(m::MediaAssemblyFilter::special1);sum+=same->sample({.3f,.6f,.9f})[0];}counting=false;check(sum>0&&allocations==0&&assets.stats().cubeReads==before.cubeReads,"Warm preview lookup/interpolation has no allocation or disk I/O");
    rejects([&]{warm->sample({NAN,0,0});},"Nonfinite cube coordinate rejects");check(warm->sample({-1,2,0})==warm->sample({0,1,0}),"Extended colors clamp to original cube domain");
    Temporary temporary;write(temporary.root/"provenance.json",provenanceBytes);std::filesystem::create_directory(temporary.root/"luts");auto bytes=read(root/"luts/sp_filter_1.rgb8");bytes[0]^=1;write(temporary.root/"luts/sp_filter_1.rgb8",bytes);n::NativeMediaAssemblyAssets damaged(temporary.root);rejects([&]{damaged.cube(m::MediaAssemblyFilter::special1);},"Corrupted original cube never publishes");check(damaged.stats().retainedBytes==0&&damaged.stats().cubeReads==0,"Rejected cube leaves cache untouched");write(temporary.root/"provenance.json",provenanceBytes+" ");rejects([&]{n::NativeMediaAssemblyAssets bad(temporary.root);},"Changed provenance is not accepted");write(temporary.root/"provenance.json","{}");rejects([&]{n::NativeMediaAssemblyAssets bad(temporary.root);},"Unknown provenance is not accepted");
    std::cout<<"Original Core Image maximum delta "<<maxDelta<<"; ";
}
}
int main(int argc,char**argv){try{check(argc==3,"Pass production resource root and original source fixture");run(std::filesystem::absolute(argv[1]),argv[2]);std::cout<<checks<<" Media Assembly asset checks passed\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"Media Assembly assets failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
