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
        const auto id=row["filter"].string();const auto items=m::mediaAssemblyFilters();std::size_t index{};while(index<items.size()&&items[index].id!=id)++index;check(index>0&&index<items.size(),"Source preset remains allow-listed");const auto cube=assets.cube(static_cast<m::MediaAssemblyFilter>(index));
        const auto out=cube->sample({float(row["input"].array()[0].number()),float(row["input"].array()[1].number()),float(row["input"].array()[2].number())});for(unsigned k=0;k<3;++k){const auto delta=std::abs(out[k]-float(row["output"].array()[k].number()));maxDelta=std::max(maxDelta,delta);check(delta<=.001f,"RGB8 interpolation matches actual original Core Image within quarter byte");}
        check(assets.stats().retainedCubes<=2&&assets.stats().retainedBytes<=2*n::MediaAssemblyCube::byteCount,"Only two original LUTs retained");
    }
    const auto manifest=J::parse(read(root/"catalog.json"),24*1024);std::size_t total{};for(const auto&record:manifest["files"].array()){const auto bytes=read(root/record["path"].string());check(bytes.size()==static_cast<std::size_t>(record["bytes"].integer())&&endfield::core::packet::sha256({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()})==record["sha256"].string(),"Every deployed original asset retains exact bytes");total+=bytes.size();}check(total==2742994,"All active PhotoMode artwork and filters fit in 2.62MiB");
    for(unsigned i=1;i<15;++i)check(assets.filterThumbnail(static_cast<m::MediaAssemblyFilter>(i)).path=="filter-icons/"+std::string(m::mediaAssemblyFilters()[i].id)+".png","Filter thumbnail matches exact source preset");
    for(unsigned i=0;i<24;++i)check(assets.sticker(static_cast<m::MediaAssemblyStickerKind>(i)).path=="stickers/"+std::string(m::mediaAssemblyStickers()[i].id)+".png","Only the 24 retained source stickers exist");
    check(!assets.cube(m::MediaAssemblyFilter::none),"None requests no cube");rejects([&]{assets.filterThumbnail(m::MediaAssemblyFilter::none);},"None has no fabricated thumbnail");rejects([&]{assets.cube(static_cast<m::MediaAssemblyFilter>(99));},"Invalid enum cannot become an arbitrary path");rejects([&]{assets.sticker(static_cast<m::MediaAssemblyStickerKind>(99));},"Invalid sticker rejected");
    auto retained=assets.cube(m::MediaAssemblyFilter::special1);const auto saved=retained->sample({.173f,.719f,.233f});assets.cube(m::MediaAssemblyFilter::special2);assets.cube(m::MediaAssemblyFilter::special3);assets.clear();check(assets.stats().retainedBytes==0&&retained->sample({.173f,.719f,.233f})==saved,"Eviction and hide release cache without invalidating in-flight borrower");
    const auto warm=assets.cube(m::MediaAssemblyFilter::special1);const auto before=assets.stats();allocations=0;counting=true;float sum{};for(unsigned i=0;i<10000;++i){const auto same=assets.cube(m::MediaAssemblyFilter::special1);sum+=same->sample({.3f,.6f,.9f})[0];}counting=false;check(sum>0&&allocations==0&&assets.stats().cubeReads==before.cubeReads,"Warm preview lookup/interpolation has no allocation or disk I/O");
    rejects([&]{warm->sample({NAN,0,0});},"Nonfinite cube coordinate rejects");check(warm->sample({-1,2,0})==warm->sample({0,1,0}),"Extended colors clamp to original cube domain");
    Temporary temporary;write(temporary.root/"catalog.json",read(root/"catalog.json"));std::filesystem::create_directory(temporary.root/"luts");auto bytes=read(root/"luts/sp_filter_1.rgb8");bytes[0]^=1;write(temporary.root/"luts/sp_filter_1.rgb8",bytes);n::NativeMediaAssemblyAssets damaged(temporary.root);rejects([&]{damaged.cube(m::MediaAssemblyFilter::special1);},"Corrupted original cube never publishes");check(damaged.stats().retainedBytes==0&&damaged.stats().cubeReads==0,"Rejected cube leaves cache untouched");write(temporary.root/"catalog.json","{}");rejects([&]{n::NativeMediaAssemblyAssets bad(temporary.root);},"Unknown catalog is not accepted");
    std::cout<<"Original Core Image maximum delta "<<maxDelta<<"; ";
}
}
int main(int argc,char**argv){try{check(argc==3,"Pass production resource root and original source fixture");run(std::filesystem::absolute(argv[1]),argv[2]);std::cout<<checks<<" Media Assembly asset checks passed\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"Media Assembly assets failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
