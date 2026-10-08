#include "native/notes_controls_assets.hpp"
#include "core/data/data_store.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};unsigned checks{};}
void*operator new(std::size_t n){if(counting)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void*operator new[](std::size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace native=endfield::native;namespace modules=endfield::modules;namespace core=endfield::core;using Json=ehud::data::Json;
namespace {
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char*message){bool rejected{};try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
std::string hash(std::string_view data){return core::packet::sha256({reinterpret_cast<const std::uint8_t*>(data.data()),data.size()});}
void put(const std::filesystem::path&path,std::string_view bytes){std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(file),"Own fixture file writes completely");}
void big(std::string&value,std::uint32_t n){for(unsigned shift:{24u,16u,8u,0u})value.push_back(static_cast<char>(n>>shift));}
std::uint32_t crc(std::string_view data){std::uint32_t value=0xffffffffu;for(unsigned char byte:data){value^=byte;for(unsigned n=0;n<8;++n)value=(value&1u)?(value>>1)^0xedb88320u:value>>1;}return value^0xffffffffu;}
void chunk(std::string&png,std::string_view name,std::string_view payload){big(png,static_cast<std::uint32_t>(payload.size()));const auto start=png.size();png+=name;png+=payload;big(png,crc(std::string_view(png).substr(start)));}
std::string image(unsigned variant){
    std::string header;big(header,36);big(header,36);header+=std::string("\x08\x06\0\0\0",5);
    std::string pixels;pixels.reserve(5220);for(unsigned y=0;y<36;++y){pixels.push_back(0);for(unsigned x=0;x<36;++x){pixels+=std::string(3,static_cast<char>(239));pixels.push_back(static_cast<char>((x+y+variant)%2?255:128));}}
    std::string deflate("\x78\x01\x01",3);const auto size=static_cast<std::uint16_t>(pixels.size());deflate.push_back(static_cast<char>(size));deflate.push_back(static_cast<char>(size>>8));deflate.push_back(static_cast<char>(~size));deflate.push_back(static_cast<char>((~size)>>8));deflate+=pixels;
    std::uint32_t a=1,b{};for(unsigned char byte:pixels){a=(a+byte)%65521;b=(b+a)%65521;}big(deflate,(b<<16)|a);
    std::string result("\x89PNG\r\n\x1a\n",8);chunk(result,"IHDR",header);chunk(result,"IDAT",deflate);chunk(result,"IEND",{});return result;
}
struct Fixture {
    std::filesystem::path root=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path())/("endfield-notes-assets-"+ehud::data::makeUUID());
    Json manifest;std::array<std::string,2>files,pixels;
    native::NativeNotesControlsAssetPins pins;
    Fixture(){std::filesystem::create_directory(root);modules::NotesControls source;source.update({});Json::Array images;
        for(std::size_t n=0;n<2;++n){const auto&d=source.images()[n];pixels[n]=image(static_cast<unsigned>(n));const auto digest=hash(pixels[n]);files[n]="raster/"+digest+".png";
            Json dependency=Json::Object{{"layerID",d.layerID},{"sourceResource",d.sourceResource},{"rect",Json::Array{d.rect.x,d.rect.y,d.rect.width,d.rect.height}},
                {"tint",Json::Array{d.tint[0],d.tint[1],d.tint[2],d.tint[3]}},{"requestedPixels",int(d.requestedPixels)},{"sourceInTint",d.sourceInTint},{"resizeAspect",d.resizeAspect}};
            images.emplace_back(Json::Object{{"dependency",std::move(dependency)},{"contents",Json::Object{{"asset",files[n]},{"sha256",digest}}},
                {"sourceLayerID",n==0?"notes/default/host/0/0/2/0/1":"notes/default/host/0/0/2/1/1"},
                {"raster",Json::Object{{"file",files[n]},{"sha256",digest},{"bytes",std::int64_t(pixels[n].size())},{"width",36},{"height",36}}}});
        }
        Json::Object sources;for(const auto*file:{"Sources/NotesCanvas.swift","Sources/EndfieldGameIcon.swift","Resources/AppIconSources/EndfieldWiki/Operational_Manual_icon.png","Resources/AppIconSources/EndfieldWiki/Mission_Icon.png"})sources[file]=std::string(64,'a');
        manifest=Json::Object{{"format","endfield-notes-controls-assets"},{"schemaVersion",1},{"preparedFor",Json::Object{{"theme","dark"},{"contentsScale",2}}},
            {"images",std::move(images)},{"source",Json::Object{{"release","v1.2.0"},{"build",18},{"commit",std::string(40,'b')}}},
            {"sourcePins",Json::Object{{"moduleSHA256",std::string(64,'a')},{"manifestSHA256",std::string(64,'a')},{"provenanceSHA256",std::string(64,'a')},{"authoritySHA256",std::string(64,'a')},{"sourceAndResourceSHA256",std::move(sources)}}},
            {"scope","Owned synthetic exact preparation fixture"}};save();
    }
    ~Fixture(){std::error_code error;std::filesystem::remove_all(root,error);}
    void save(){const auto bytes=manifest.encode();pins={hash(bytes),std::string(40,'b')};put(root/"notes-controls-assets.json",bytes);for(std::size_t n=0;n<2;++n)put(root/files[n],pixels[n]);}
    void invalid(){save();rejects([&]{native::NativeNotesControlsAssets assets(root,pins);},"Malformed prepared source contract rejects before publication");}
};
void basic(){
    Fixture fixture;native::NativeNotesControlsAssets assets(fixture.root,fixture.pins);check(assets.assetRoot()==fixture.root&&assets.manifestSHA256()==fixture.pins.manifestSHA256&&assets.sourceCommit()==fixture.pins.sourceCommit,"Retained prepared source identity matches explicit owner pins");
    modules::NotesControls source;rejects([&]{assets.imagesFor(source);},"Uninitialized owner cannot borrow prepared artwork");source.update({});const auto images=assets.imagesFor(source);check(images.size()==2,"Both exact original toolbar icons resolve together");
    for(std::size_t n=0;n<2;++n)check(images[n].dependency.layerID==source.images()[n].layerID&&images[n].contents["asset"].string()==fixture.files[n],"Prepared row reaches the corresponding exact source dependency");
    const auto prepared=native::prepareNotesControlsScene(source,images);for(const auto&binding:images){bool found{};for(const auto&leaf:prepared.layers["children"].array())if(leaf["id"].string()==binding.dependency.layerID){found=true;check(leaf["contents"]==binding.contents,"Prepared metadata is consumed unchanged by the actual scene compiler");}check(found,"Prepared original icon is present in the retained scene plan");}
    allocations=0;counting=true;try{for(unsigned n=0;n<120;++n){const auto retained=assets.imagesFor(source);if(retained.data()!=images.data())throw std::runtime_error("Retained binding identity changed");}}catch(...){counting=false;throw;}counting=false;check(allocations==0,"Repeated retained binding access performs zero C++ allocations");
    // A retained accessor must not reread/hash the package after startup.
    std::filesystem::remove(fixture.root/fixture.files[0]);check(assets.imagesFor(source).data()==images.data(),"Retained image dependency access does not revisit files");
    modules::NotesControlsInput input;input.dark=false;source.update(input);rejects([&]{assets.imagesFor(source);},"Different source tint never silently uses the dark prepared image");input.dark=true;input.contentsScale=3;source.update(input);rejects([&]{assets.imagesFor(source);},"Different requested pixel size rejects exact variant mismatch");
    input={};input.kind=modules::NotesControlsKind::mediaSource;source.update(input);check(assets.imagesFor(source).empty(),"Image-free source menus need no invented bindings");input.kind=modules::NotesControlsKind::color;source.update(input);rejects([&]{assets.imagesFor(source);},"Missing original wheel is explicit rather than blank or fallback");
}
void invalid(){
    Fixture f;rejects([&]{native::NativeNotesControlsAssets assets(f.root,{});},"Missing caller pins reject");auto wrong=f.pins;wrong.manifestSHA256=std::string(64,'f');rejects([&]{native::NativeNotesControlsAssets assets(f.root,wrong);},"Different expected manifest SHA rejects");wrong=f.pins;wrong.sourceCommit=std::string(40,'f');rejects([&]{native::NativeNotesControlsAssets assets(f.root,wrong);},"Different expected source lineage rejects");
    const auto original=f.manifest;
    for(const auto*key:{"moduleSHA256","manifestSHA256","provenanceSHA256","authoritySHA256"}){f.manifest["sourcePins"][key]="not-a-pin";f.invalid();f.manifest=original;}
    f.manifest["sourcePins"]["sourceAndResourceSHA256"].erase("Sources/NotesCanvas.swift");f.invalid();f.manifest=original;
    f.manifest["preparedFor"]["theme"]="light";f.invalid();f.manifest=original;f.manifest["preparedFor"]["contentsScale"]=3;f.invalid();f.manifest=original;
    f.manifest["schemaVersion"]=2;f.invalid();f.manifest=original;f.manifest["unexpected"]="unused";f.invalid();f.manifest=original;
    auto rows=original["images"].array();rows[0]["dependency"]["requestedPixels"]=37;f.manifest["images"]=rows;f.invalid();f.manifest=original;
    rows=original["images"].array();rows[0]["dependency"]["tint"]=Json::Array{1,1,1,1};f.manifest["images"]=rows;f.invalid();f.manifest=original;
    rows=original["images"].array();rows[0]["dependency"]["rect"]=Json::Array{9,5,19,18};f.manifest["images"]=rows;f.invalid();f.manifest=original;
    rows=original["images"].array();std::swap(rows[0],rows[1]);f.manifest["images"]=rows;f.invalid();f.manifest=original;
    rows=original["images"].array();rows[0]["contents"]["asset"]="../outside.png";f.manifest["images"]=rows;f.invalid();f.manifest=original;
    rows=original["images"].array();rows[0]["raster"]["bytes"]=std::int64_t(native::NativeNotesControlsAssets::maximumRasterBytes+1);f.manifest["images"]=rows;f.invalid();f.manifest=original;
    f.save();put(f.root/f.files[0],"changed raster bytes");rejects([&]{native::NativeNotesControlsAssets assets(f.root,f.pins);},"Changed raster bytes reject at startup");f.save();
    std::filesystem::remove(f.root/f.files[1]);rejects([&]{native::NativeNotesControlsAssets assets(f.root,f.pins);},"Missing exact raster rejects at startup");f.save();
    std::error_code symlinkError;const auto outside=f.root/"outside.png";put(outside,f.pixels[0]);std::filesystem::remove(f.root/f.files[0]);std::filesystem::create_symlink(outside,f.root/f.files[0],symlinkError);
    if(!symlinkError)rejects([&]{native::NativeNotesControlsAssets assets(f.root,f.pins);},"Raster symlink cannot escape confined assets");else check(!std::filesystem::exists(f.root/f.files[0]),"Platform denied creation of the owned symlink fixture explicitly");std::filesystem::remove(f.root/f.files[0]);f.save();
    const auto duplicate=std::string("{\"format\":1,\"format\":2}");put(f.root/"notes-controls-assets.json",duplicate);wrong={hash(duplicate),f.pins.sourceCommit};rejects([&]{native::NativeNotesControlsAssets assets(f.root,wrong);},"Duplicate JSON manifest keys reject");
    const std::string oversized(native::NativeNotesControlsAssets::maximumManifestBytes+1,'x');put(f.root/"notes-controls-assets.json",oversized);wrong={hash(oversized),f.pins.sourceCommit};rejects([&]{native::NativeNotesControlsAssets assets(f.root,wrong);},"Oversized prepared metadata rejects before parse");
}
void corruptPNG(){
    Fixture f;const auto original=f.manifest;for(unsigned mode=0;mode<3;++mode){f.manifest=original;auto bytes=f.pixels[0];if(mode==0)bytes[16]=1;else if(mode==1)bytes.back()^=1;else bytes+="trailing";
        const auto digest=hash(bytes),file="raster/"+digest+".png";auto rows=f.manifest["images"].array();rows[0]["contents"]=Json::Object{{"asset",file},{"sha256",digest}};rows[0]["raster"]=Json::Object{{"file",file},{"sha256",digest},{"bytes",std::int64_t(bytes.size())},{"width",36},{"height",36}};f.manifest["images"]=rows;f.save();put(f.root/file,bytes);rejects([&]{native::NativeNotesControlsAssets assets(f.root,f.pins);},"Pinned but malformed PNG structure/checksum rejects before WIC load");}
}
void actual(const char*root,const char*sha,const char*commit){native::NativeNotesControlsAssets assets(std::filesystem::absolute(root).lexically_normal(),{sha,commit});modules::NotesControls source;source.update({});const auto images=assets.imagesFor(source);check(images.size()==2,"Explicit actual-source bundle binds both current Notes controls");const auto prepared=native::prepareNotesControlsScene(source,images);check(!prepared.requiresGroupOpacity&&!prepared.surfaces.empty(),"Actual source-prepared rasters enter the current retained control plan");}
}
int main(int argc,char**argv){try{check(argc==1||argc==4,"Optional arguments: prepared bundle root, expected manifest SHA, expected source commit");basic();invalid();corruptPNG();if(argc==4)actual(argv[1],argv[2],argv[3]);std::cout<<"PASS "<<checks<<" prepared Notes controls asset checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
