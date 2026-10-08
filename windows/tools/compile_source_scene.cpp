// Build-only original-source compiler. No window, device, desktop, user data,
// timer or service is created. Runtime can load the one validated .ehscene file.
#include "native/source_scene.hpp"
#include "core/shell_packet.hpp"
#include "core/data/file_io.hpp"
#include <chrono>
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace fs=std::filesystem;
namespace gpu=endfield::native;
namespace {
std::string utf8(const fs::path& path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
using IncludedFrames=std::vector<std::pair<std::string,std::string>>;
int compile(const fs::path& root,const fs::path& shaders,std::string frame,const fs::path& output,const IncludedFrames& included){
    try{
        const auto started=std::chrono::steady_clock::now();gpu::SourceScene source(root,shaders,std::move(frame));
        for(const auto& [name,prefix]:included){gpu::SourceScene additional(root,shaders,name);source.includeTemplates(additional,prefix);}
        source.writeCompiled(output);
        const auto data=ehud::data::detail::readFile(output,gpu::SourceScene::maximumCompiledBytes);if(!data)throw std::runtime_error("Compiled output missing");
        const auto sha=endfield::core::packet::sha256({reinterpret_cast<const std::uint8_t*>(data->data()),data->size()});
        const auto& p=source.provenance();const auto stats=source.stats();
        ehud::data::Json::Array provenance;for(const auto& origin:source.catalogProvenance())provenance.emplace_back(ehud::data::Json::Object{{"packetSHA256",origin.packetSHA256},{"frameSHA256",origin.frameSHA256},{"shaderManifestSHA256",origin.shaderManifestSHA256},{"frameName",origin.frameName}});
        ehud::data::Json report=ehud::data::Json::Object{{"schemaVersion",1},{"compiledSchemaVersion",int(gpu::SourceScene::compiledSchemaVersion)},{"catalogProvenance",std::move(provenance)},{"templates",std::int64_t(source.templates().size())},{"file",utf8(output)},{"bytes",std::int64_t(data->size())},{"sha256",sha},
            {"sourcePacketSHA256",p.packetSHA256},{"sourceFrameSHA256",p.frameSHA256},{"compiledShaderManifestSHA256",p.shaderManifestSHA256},{"frame",p.frameName},
            {"batches",std::int64_t(stats.batches)},{"draws",std::int64_t(stats.draws)},{"textures",std::int64_t(stats.textures)},{"pipelines",std::int64_t(stats.pipelines)},
            {"milliseconds",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()},
            {"nativeLayersIncluded",false},{"motionBaked",false},{"userDataRead",false}};
        std::cout<<report.encode()<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
    if(argc<5||(argc-5)%3){std::cerr<<"Usage: compile_source_scene absolute-packet-root absolute-compiled-shaders.json frame-name new-absolute-output.ehscene [--include-frame frame-name namespace]...\n";return 2;}
    try{IncludedFrames included;for(int i=5;i<argc;i+=3){if(std::wstring_view(argv[i])!=L"--include-frame")throw std::runtime_error("Expected --include-frame");included.emplace_back(utf8(fs::path(argv[i+1])),utf8(fs::path(argv[i+2])));}
        return compile(fs::path(argv[1]),fs::path(argv[2]),utf8(fs::path(argv[3])),fs::path(argv[4]),included);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#else
int main(int argc,char** argv){
    if(argc<5||(argc-5)%3){std::cerr<<"Usage: compile_source_scene absolute-packet-root absolute-compiled-shaders.json frame-name new-absolute-output.ehscene [--include-frame frame-name namespace]...\n";return 2;}
    try{IncludedFrames included;for(int i=5;i<argc;i+=3){if(std::string_view(argv[i])!="--include-frame")throw std::runtime_error("Expected --include-frame");included.emplace_back(argv[i+1],argv[i+2]);}
        return compile(fs::path(argv[1]),fs::path(argv[2]),argv[3],fs::path(argv[4]),included);
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#endif
