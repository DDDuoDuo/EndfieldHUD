#include "core/source_animation_binary.hpp"
#include "core/data/file_io.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc,char**argv){try{
    if(argc!=4)throw std::invalid_argument("Usage: compile_source_animation library.json source-manifest-sha256 new-library.ehanim");
    const auto input=std::filesystem::absolute(argv[1]),output=std::filesystem::absolute(argv[3]);
    const auto bytes=ehud::data::detail::readFile(input,endfield::core::source::maximumAnimationBinaryBytes);
    if(!bytes)throw std::invalid_argument("Missing explicit animation library JSON");
    const auto library=endfield::core::source::Library::fromJson(ehud::data::Json::parse(*bytes,endfield::core::source::maximumAnimationBinaryBytes));
    const auto compiled=endfield::core::source::encodeAnimationLibrary(library,argv[2]);
    const std::string value(reinterpret_cast<const char*>(compiled.data()),compiled.size());
    ehud::data::detail::replaceFile(output,std::nullopt,value,endfield::core::source::maximumAnimationBinaryBytes);
    std::cout<<"Compiled "<<library.clips().size()<<" source clips into "<<compiled.size()<<" bytes\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
