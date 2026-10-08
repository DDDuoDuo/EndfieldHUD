#include "native/clipboard_assets.hpp"
#include "core/data/file_io.hpp"
#include <fstream>
#include <iostream>
namespace n=endfield::native;namespace d=ehud::data;
namespace{unsigned checks{};void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}template<class F>void rejects(F fn){bool failed{};try{fn();}catch(const std::exception&){failed=true;}check(failed,"Malformed original resource must reject");}}
int main(int argc,char**argv){try{check(argc==2,"Pass packaged Clipboard artwork root");const auto root=std::filesystem::absolute(argv[1]);n::NativeClipboardAssets assets(root);const auto images=assets.images();check(images.text&&images.files&&images.text->pixels==50&&images.files->pixels==50,"Prepared 25pt original icons at source scale 2");check(images.text->sourceResource=="Operational_Manual_icon"&&images.files->sourceResource=="Depot_icon","Original named game artwork retained");check(assets.revealSamples()->sourceViewport()==endfield::core::Rect{0,0,376,246},"Clipboard retains its own normalized CA viewport");rejects([&]{assets.images(3);});rejects([&]{n::NativeClipboardAssets missing(root/"missing");});
    const auto temp=std::filesystem::canonical(std::filesystem::temp_directory_path())/("ehud-clipboard-assets-"+d::makeUUID());check(std::filesystem::create_directory(temp),"Create isolated resource corruption fixture");struct Clean{std::filesystem::path p;~Clean(){std::error_code ec;std::filesystem::remove_all(p,ec);}}clean{temp};
    for(const auto name:{"Operational_Manual_icon.png","Depot_icon.png","reveal-mask.bin"})std::filesystem::copy_file(root/name,temp/name);{n::NativeClipboardAssets copied(temp);check(copied.images().text->contents==images.text->contents,"Exact copies preserve original descriptors");}
    {std::ofstream corrupt(temp/"Depot_icon.png",std::ios::binary|std::ios::app);corrupt.put('x');}rejects([&]{n::NativeClipboardAssets changed(temp);});
    std::cout<<"PASS "<<checks<<" Clipboard resource checks\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
