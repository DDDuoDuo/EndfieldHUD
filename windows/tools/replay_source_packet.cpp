// Frozen original-Mac GPU replay. Only an owned hidden target is rendered;
// nativeLayers are deliberately reported separately, never silently claimed.
#include "core/shell_packet.hpp"
#include "core/data/file_io.hpp"
#include "native/renderer.hpp"
#include "native/source_graphics.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3dcommon.h>
#endif

using ehud::data::Json;
namespace packet = endfield::core::packet;
namespace gpu = endfield::native;
namespace fs = std::filesystem;
namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
unsigned number(const Json& value, unsigned maximum) {
    require(value.isNumber(), "Missing integer source state");
    const auto result = value.integer();
    require(result >= 0 && static_cast<std::uint64_t>(result) <= maximum, "Source integer state is out of bounds");
    return static_cast<unsigned>(result);
}
std::string text(const Json& value) {
    const auto result = value.string();
    require(!result.empty() && result.size() <= 4096, "Missing or oversized source identity");
    return result;
}
std::span<const std::uint8_t> bytes(const std::string& value) {
    return {reinterpret_cast<const std::uint8_t*>(value.data()), value.size()};
}
std::string hash(const std::string& value) { return packet::sha256(bytes(value)); }
std::string read(const fs::path& path, std::size_t maximum) {
    auto data = ehud::data::detail::readFile(path, maximum);
    require(data.has_value(), "Missing replay artifact"); return std::move(*data);
}
fs::path absoluteOrdinary(const fs::path& value) {
    require(value.is_absolute() && value.lexically_normal() == value, "Replay paths must be explicit absolute paths");
    ehud::data::detail::validateDataFile(value); return value;
}
fs::path absoluteRoot(const fs::path& value) {
    ehud::data::detail::validateRoot(value); return value;
}
fs::path immediate(const fs::path& root, const std::string& name) {
    require(!name.empty() && name.size() <= 255 && name.find_first_of("/\\:") == std::string::npos && name.find('\0') == std::string::npos &&
            name != "." && name != ".." && name.back() != '.' && name.back() != ' ', "Bytecode path is not a package child");
    const auto result = root / fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(name.data()), name.size()));
    ehud::data::detail::validateDataFile(result); return result;
}
gpu::SourceStage stage(const std::string& name) {
    require(name == "vertex" || name == "fragment", "Unsupported source shader stage");
    return name == "vertex" ? gpu::SourceStage::vertex : gpu::SourceStage::fragment;
}
// Values are explicitly taken from the original Mac compare mapping; batch
// overrides are Unity serialization, unlike the material's raw Metal enums.
unsigned unityCompare(unsigned value) {
    require(value <= 8, "Unsupported serialized stencil comparison");
    return value == 0 || value == 8 ? 7 : value - 1;
}
unsigned unityWriteMask(unsigned value) {
    require(value <= 15, "Invalid serialized color mask");
    return ((value & 1) << 3) | ((value & 2) << 1) | ((value & 4) >> 1) | ((value & 8) >> 3);
}
gpu::SourceStencilFace face(const Json& value) {
    return {number(value["compare"],7), number(value["failure"],7), number(value["depthFailure"],7),
            number(value["pass"],7), number(value["readMask"],255), number(value["writeMask"],255)};
}
struct Program { Json metadata; std::string code; };
class CompiledShaders {
public:
    CompiledShaders(const fs::path& manifest, const packet::Package& source) : root_(manifest.parent_path()), source_(source) {
        ehud::data::detail::validateRoot(root_);
        manifest_ = Json::parse(read(absoluteOrdinary(manifest),32*1024*1024),32*1024*1024);
        require(manifest_["schemaVersion"].integer() == 1, "Unsupported compiled shader schema");
        const auto& programs = manifest_["programs"].array();
        require(!programs.empty() && programs.size() <= 512, "Invalid compiled shader count");
        for (const auto& item : programs) {
            const auto identity = text(item["sourceDescriptor"]) + ":" + text(item["stage"]);
            require(text(item["id"]) == identity && metadata_.emplace(identity,item).second, "Duplicate or mismatched compiled shader identity");
        }
    }
    const Program& get(const Json& pass, const std::string& name) {
        const auto descriptor = text(pass["shaderDescriptorFile"]), id = descriptor + ":" + name;
        if (const auto found = programs_.find(id); found != programs_.end()) return found->second;
        const auto found = metadata_.find(id);
        require(found != metadata_.end(), "Compiled source shader is missing: " + id);
        const auto& m = found->second;
        require(text(m["stage"]) == name && text(m["profile"]) == (name == "vertex" ? "vs_5_0" : "ps_5_0") &&
                text(m["entry"]) == "main", "Compiled shader stage contract differs from source");
        const auto descriptorBytes = source_.loadShaderAsset("shader/" + descriptor);
        require(packet::sha256(descriptorBytes.bytes()) == text(m["sourceDescriptorSHA256"]), "Compiled source descriptor hash differs");
        const auto metal = text(m["metalFile"]);
        require(metal == text(pass["stages"][name]["file"]), "Compiled shader points to a different original stage");
        const auto metalBytes = source_.loadShaderAsset("shader/" + metal);
        require(packet::sha256(metalBytes.bytes()) == text(m["metalSHA256"]), "Original Metal source hash differs");
        require(metal.ends_with(".metal"), "Original stage has no Metal/SPIR-V source pair");
        const auto spirv = source_.loadShaderAsset("shader/" + metal.substr(0,metal.size()-6) + ".spv");
        require(packet::sha256(spirv.bytes()) == text(m["spirvSHA256"]), "Original SPIR-V hash differs");
        const auto declared = number(m["bytecodeBytes"],4*1024*1024);
        auto code = read(immediate(root_,text(m["bytecodeFile"])),4*1024*1024);
        require(code.size() == declared && hash(code) == text(m["bytecodeSHA256"]), "Compiled bytecode integrity failed");
        require(code.size() >= 4 && code.substr(0,4) == "DXBC", "Compiled stage is not SM5 DXBC");
        return programs_.emplace(id,Program{m,std::move(code)}).first->second;
    }
private:
    fs::path root_;
    const packet::Package& source_;
    Json manifest_;
    std::map<std::string,Json,std::less<>> metadata_;
    std::map<std::string,Program,std::less<>> programs_;
};
struct PreparedMesh { unsigned stride{}; std::string vertices; std::vector<std::uint32_t> indices; };
struct PreparedTexture { gpu::SourceTexture description; std::vector<packet::BinaryData> levels; };
struct PreparedPipeline { gpu::SourcePipeline description; unsigned stride{}; };
struct PreparedReplay {
    unsigned width{}, height{};
    std::map<std::string,PreparedMesh,std::less<>> meshes;
    std::map<std::string,PreparedTexture,std::less<>> textures;
    std::map<std::string,PreparedPipeline,std::less<>> pipelines;
    std::map<std::string,std::string,std::less<>> uniforms;
    std::vector<gpu::SourceDraw> draws;
    std::size_t sourceBatches{}, inactiveUniforms{};
    std::set<std::string> shaderKeys;
};
const Json* binding(const Json& program, const std::string& name, unsigned expectedType) {
    const Json* result{};
    for (const auto& item : program["bindings"].array()) if (item["name"].string() == name) {
        require(!result && number(item["type"],32) == expectedType && number(item["count"],128) == 1,
                "Reflected binding is duplicated or has an unsupported resource type"); result = &item;
    }
    return result;
}
void addTexture(PreparedReplay& output, const packet::Package& source, const std::string& id) try {
    if (output.textures.contains(id)) return;
    const auto found = source.textures().find(id); require(found != source.textures().end(), "Source texture is absent");
    const auto& original = found->second; const auto& s = original.metadata["sampler"];
    require(s["normalizedCoordinates"].boolean() && s["lodMinClamp"].number() == 0 &&
            s["lodMaxClamp"].number() == static_cast<double>(std::numeric_limits<float>::max()) &&
            number(s["compareFunction"],7) == 0, "Unsupported nondefault source sampler clamp/comparison");
    PreparedTexture texture; auto& d = texture.description; d.pixelFormat = original.pixelFormat;
    d.minFilter=number(s["minFilter"],1);d.magFilter=number(s["magFilter"],1);d.mipFilter=number(s["mipFilter"],2);
    d.addressU=number(s["addressU"],4);d.addressV=number(s["addressV"],4);d.addressW=number(s["addressW"],4);
    d.maxAnisotropy=number(s["maxAnisotropy"],16);
    texture.levels.reserve(original.mips.size());d.mips.reserve(original.mips.size());
    for (const auto& mip : original.mips) {
        texture.levels.push_back(source.loadTextureMip(id,mip.level));
        d.mips.push_back({mip.width,mip.height,mip.rowBytes,texture.levels.back().bytes()});
    }
    output.textures.emplace(id,std::move(texture));
} catch(const std::exception& error) {
    throw std::runtime_error("Texture "+id+": "+error.what());
}
std::string addMesh(PreparedReplay& output, const packet::Package& source, const Json& batch) {
    require(std::endian::native == std::endian::little, "Native source vertex replay needs a little-endian host");
    const auto id = text(batch["mesh"]); const auto& m = source.meshes().at(id);
    const auto stride = number(m.metadata["originalVertexBuffer"]["stride"],256);
    const auto colorOffset = number(m.metadata["originalVertexBuffer"]["offsets"]["color"],240);
    require(stride >= colorOffset + 16, "Original vertex color does not fit the source stride");
    const auto& tint = batch["gpuVertexColor"].array(); require(tint.size() == 4, "Missing exact submitted vertex tint");
    auto original = source.loadOriginalVertexBuffer(id); auto canonical = source.loadMesh(id);
    for (std::size_t vertex = 0; vertex < m.vertexCount; ++vertex) for (unsigned component = 0; component < 4; ++component) {
        const auto offset = vertex*stride+colorOffset+component*4;
        // Preview's native buffer can already contain the uploaded tint. The
        // canonical record is the authoritative original unmodified color;
        // overwrite that one field to avoid multiplying submitted alpha twice.
        float channel=canonical.vertices.at(vertex).color[component];
        const auto value = tint[component].number(); require(std::isfinite(value), "Nonfinite submitted tint");
        channel *= static_cast<float>(value); require(std::isfinite(channel), "Submitted source vertex color overflow");
        std::memcpy(original.storage.data()+offset,&channel,4);
    }
    const auto key = "mesh:" + hash(original.storage) + ":" + m.indices.sha256;
    if (!output.meshes.contains(key)) output.meshes.emplace(key,PreparedMesh{stride,std::move(original.storage),std::move(canonical.indices)});
    return key;
}
PreparedPipeline pipeline(const Json& pass, const Json& batch, CompiledShaders& shaders) {
    PreparedPipeline result; auto& p = result.description;
    p.vertexBytecode=bytes(shaders.get(pass,"vertex").code);p.fragmentBytecode=bytes(shaders.get(pass,"fragment").code);
    unsigned sourceBuffer = 32;
    for (const auto& attribute : pass["vertexAttributes"].array()) {
        const auto format = number(attribute["format"],255), buffer = number(attribute["bufferIndex"],31);
        // Metal SDK MTLVertexFormatFloat[1..4] are 28,29,30,31.
        require(format >= 28 && format <= 31, "Unsupported original Metal vertex format");
        if (sourceBuffer == 32) { sourceBuffer=buffer;result.stride=number(attribute["stride"],256); }
        require(buffer == sourceBuffer && number(attribute["stride"],256) == result.stride,
                "Multiple source vertex buffers are not implemented");
        p.attributes.push_back({number(attribute["attribute"],15),format-27,number(attribute["offset"],240)});
    }
    require(!p.attributes.empty(), "Source pass has no declared vertex attributes");
    const auto& b=pass["blend"];p.blendEnabled=b["enabled"].boolean();
    p.sourceRGB=number(b["sourceRGB"],10);p.destinationRGB=number(b["destinationRGB"],10);
    p.sourceAlpha=number(b["sourceAlpha"],10);p.destinationAlpha=number(b["destinationAlpha"],10);
    p.rgbOperation=number(b["rgbOperation"],4);p.alphaOperation=number(b["alphaOperation"],4);p.writeMask=number(b["writeMask"],15);
    if (!batch["colorWriteMask"].isNull()) p.writeMask=unityWriteMask(number(batch["colorWriteMask"],15));
    const auto& depth=pass["depthStencil"];p.depthCompare=number(depth["depthCompare"],7);p.depthWrite=depth["depthWrite"].boolean();
    require(depth["front"].isNull() == depth["back"].isNull(), "One-sided source stencil enable is unsupported");
    p.stencilEnabled=!depth["front"].isNull();
    if(p.stencilEnabled){p.front=face(depth["front"]);p.back=face(depth["back"]);}
    if (!batch["stencil"].isNull()) {
        const auto& s=batch["stencil"];p.stencilEnabled=true;
        p.front={unityCompare(number(s["compare"],8)),number(s["fail"],7),number(s["depthFail"],7),
                 number(s["pass"],7),number(s["readMask"],255),number(s["writeMask"],255)};p.back=p.front;
    }
    p.cull=number(pass["cull"],2);return result;
}
void bindStage(PreparedReplay& output, gpu::SourceDraw& draw, const packet::Package& source,
               const packet::FrameData& frame, std::size_t batchIndex, const Json& material,
               const Json& pass, const Json& batch, CompiledShaders& shaders, const std::string& stageName) try {
    const auto& program=shaders.get(pass,stageName).metadata;const auto sourceStage=stage(stageName);
    std::set<std::string> recognized;
    for(const auto& mapped:program["uniforms"].array()) {
        const auto hlslName=text(mapped["hlslName"]);recognized.insert(hlslName);
        if(!mapped["active"].boolean()){++output.inactiveUniforms;continue;}
        const auto* resource=binding(program,hlslName,0); // D3D_SIT_CBUFFER
        require(resource && number(resource->operator[]("slot"),13)==number(mapped["slot"],13), "Active uniform has no exact reflected slot");
        const packet::UniformDescriptor* descriptor{};
        for(const auto& item:frame.uniforms.at(batchIndex))
            if(item.passID==pass["id"].string()&&item.stage==stageName&&item.bufferName==mapped["name"].string()) {
                require(!descriptor,"Source uniform logical name is duplicated");descriptor=&item;
            }
        const auto rawSize=number(mapped["sourceSize"],65536);
        const auto byteWidth=number(mapped["byteWidth"],65536);
        const auto context=text(pass["id"])+"/"+stageName+"/"+text(mapped["name"])+
            " sourceSize="+std::to_string(rawSize)+" payloadBytes="+(descriptor?std::to_string(descriptor->byteCount):"missing")+
            " compiledBytes="+std::to_string(byteWidth);
        require(descriptor && rawSize>0 && descriptor->byteCount>=rawSize && byteWidth==(rawSize+15)/16*16,
                "Compiled/source uniform allocation mismatch: "+context);
        const Json* declaration{};
        for(const auto& item:pass["stages"][stageName]["uniforms"].array())
            if(item["name"].string()==mapped["name"].string()){require(!declaration,"Duplicate source uniform declaration");declaration=&item;}
        require(declaration && number((*declaration)["size"],65536)==rawSize &&
                number((*declaration)["index"],31)==descriptor->index,"Source uniform ABI declaration mismatch: "+context);
        // Mac's reflected MSL allocation and D3D's cbuffer allocation can be
        // larger than the serialized size. Verify logical field offsets against
        // the source descriptor before retaining the exact payload prefix;
        // only API-required trailing padding may be zero-filled or discarded.
        for(const auto& field:mapped["sourceFields"].array()) {
            bool declared=false,planned=false;
            for(const auto& item:(*declaration)["fields"].array())
                if(item["name"].string()==field["name"].string()&&item["offset"].integer()==field["offset"].integer())declared=true;
            for(const auto& item:descriptor->metadata["fields"].array())
                if(item["name"].string()==field["name"].string()&&item["offset"].integer()==field["offset"].integer())planned=true;
            require(declared&&planned,"Source uniform logical field offset mismatch: "+context);
        }
        for(const auto& member:mapped["compiledMembers"].array()) {
            const auto offset=number(member["offset"],65536),length=number(member["bytes"],65536);
            require(offset<=byteWidth&&length<=byteWidth-offset,"Compiled member exceeds source cbuffer: "+context);
            if(member["used"].boolean())require(offset<=descriptor->byteCount&&length<=descriptor->byteCount-offset,
                "Active compiled member exceeds exact Mac payload: "+context);
        }
        auto payload=source.loadUniformPayload(*descriptor);payload.storage.resize(byteWidth,'\0');
        const auto uniformID="uniform:"+hash(payload.storage);
        output.uniforms.emplace(uniformID,std::move(payload.storage));
        draw.uniforms.push_back({sourceStage,number(mapped["slot"],13),uniformID});
    }
    for(const auto& mapped:program["textures"].array()) {
        const auto textureName=text(mapped["hlslTexture"]),samplerName=text(mapped["hlslSampler"]);
        recognized.insert(textureName);recognized.insert(samplerName);
        const auto* image=binding(program,textureName,2);const auto* sampler=binding(program,samplerName,3); // D3D_SIT_TEXTURE/SAMPLER
        if(!image&&!sampler)continue;
        require(image&&sampler&&number((*image)["dimension"],16)==4,"Unsupported texture-only/sampler-only or non-2D stage resource");
        const auto property=text(mapped["name"]);
        bool declared=false;
        for(const auto& item:pass["textureBindings"].array())
            if(item["name"].string()==property&&item["stage"].string()==stageName)declared=true;
        require(declared,"Compiled texture logical name is not declared by the original pass");
        const auto& override=batch["textureOverrides"][property];
        const auto& original=override.isNull()?material["textures"][property]:override;
        // Actual Mac texture plan uses the exported __white resource when a
        // shader binding has neither a batch override nor a material texture.
        const auto textureID=original.isNull()?std::string("__white"):text(original);addTexture(output,source,textureID);
        draw.textures.push_back({sourceStage,number((*image)["slot"],127),number((*sampler)["slot"],15),textureID});
    }
    for(const auto& item:program["bindings"].array())require(recognized.contains(text(item["name"])),"Reflected resource has no original logical binding");
} catch(const std::exception& error) {
    throw std::runtime_error("Binding pass "+text(pass["id"])+" stage "+stageName+": "+error.what());
}
PreparedReplay prepare(const packet::Package& source, const packet::FrameData& frame, CompiledShaders& shaders) {
    PreparedReplay result;
    require(frame.viewport.x>0&&frame.viewport.y>0&&std::floor(frame.viewport.x)==frame.viewport.x&&std::floor(frame.viewport.y)==frame.viewport.y,
            "Source viewport must contain integral pixels");
    require(frame.viewport.x<=4096&&frame.viewport.y<=4096,"Replay viewport is too large");
    result.width=static_cast<unsigned>(frame.viewport.x);result.height=static_cast<unsigned>(frame.viewport.y);
    const auto& camera=frame.metadata["gpuCamera"];
    require(camera["sceneColorMode"].string()=="directLDR"&&camera["postprocess"].array().empty(),"Source HDR/postprocess is not implemented; replay stopped");
    require(number(camera["sceneColorPixelFormat"],1000)==81&&number(camera["drawablePixelFormat"],1000)==81&&
            number(camera["depthStencilPixelFormat"],1000)==260,"Unsupported exact source attachment format");
    require(camera["clearColor"].array().size()==4&&camera["clearDepth"].number()==1&&number(camera["clearStencil"],255)==0,
            "Unsupported exact source clear state");
    for(const auto& component:camera["clearColor"].array())require(component.number()==0,"Nontransparent source clear is unsupported");
    const auto& batches=frame.metadata["batches"].array();result.sourceBatches=batches.size();
    for(std::size_t index=0;index<batches.size();++index) {
        const auto& batch=batches[index];const auto meshID=addMesh(result,source,batch);
        const auto& mesh=result.meshes.at(meshID);const auto& material=source.materials().at(text(batch["material"]));
        for(const auto& pass:material["passes"].array()) {
            const auto key=text(pass["id"])+":"+hash(batch["stencil"].encode()+batch["colorWriteMask"].encode());
            if(!result.pipelines.contains(key))result.pipelines.emplace(key,pipeline(pass,batch,shaders));
            require(result.pipelines.at(key).stride==mesh.stride,"Source pass and raw mesh strides differ");
            gpu::SourceDraw draw;draw.mesh=meshID;draw.pipeline=key;
            draw.indexCount=static_cast<unsigned>(mesh.indices.size());
            if(!batch["indexRange"].isNull()) {
                const auto& range=batch["indexRange"].array();require(range.size()==2,"Invalid source triangle range");
                draw.firstIndex=number(range[0],static_cast<unsigned>(mesh.indices.size()));
                const auto upper=number(range[1],static_cast<unsigned>(mesh.indices.size()));
                require(upper>=draw.firstIndex,"Invalid source triangle range");draw.indexCount=upper-draw.firstIndex;
            }
            require(draw.indexCount>0&&draw.indexCount%3==0,"Empty or incomplete source triangle range");
            draw.stencilReference=number(batch["stencil"].isNull()?pass["stencilReference"]:batch["stencil"]["reference"],255);
            bindStage(result,draw,source,frame,index,material,pass,batch,shaders,"vertex");
            bindStage(result,draw,source,frame,index,material,pass,batch,shaders,"fragment");
            result.shaderKeys.insert(text(pass["shaderKey"]));result.draws.push_back(std::move(draw));
        }
    }
    require(!result.draws.empty(),"Frame has no original GPU passes");return result;
}
void upload(gpu::SourceGraphics& target, const PreparedReplay& source) {
    for(const auto& [id,m]:source.meshes)target.setMesh(id,1,m.stride,bytes(m.vertices),m.indices);
    for(const auto& [id,t]:source.textures)target.setTexture(id,1,t.description);
    for(const auto& [id,p]:source.pipelines)target.setPipeline(id,p.description);
    for(const auto& [id,u]:source.uniforms)target.setUniform(id,bytes(u));target.setDraws(source.draws);
}
void writeNew(const fs::path& path, const std::string& data, std::size_t maximum) {
    require(!ehud::data::detail::readFile(path,maximum).has_value(),"Replay output already exists; choose a new isolated output directory");
    ehud::data::detail::replaceFile(path,std::nullopt,data,maximum);
}
#ifdef _WIN32
static_assert(D3D_SIT_CBUFFER==0&&D3D_SIT_TEXTURE==2&&D3D_SIT_SAMPLER==3&&D3D_SRV_DIMENSION_TEXTURE2D==4);
class OwnedWindow {
public:
    OwnedWindow(unsigned width,unsigned height) {
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);
        type.lpszClassName=L"EndfieldOriginalPacketHiddenReplay";atom_=RegisterClassW(&type);
        require(atom_!=0,"Cannot register owned replay window");
        window_=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP|WS_EX_TOOLWINDOW,type.lpszClassName,L"Owned source replay",
            WS_POPUP,0,0,static_cast<int>(width),static_cast<int>(height),nullptr,nullptr,type.hInstance,nullptr);
        if(!window_){UnregisterClassW(MAKEINTATOM(atom_),type.hInstance);atom_=0;throw std::runtime_error("Cannot create owned hidden replay target");}
    }
    ~OwnedWindow(){if(window_)DestroyWindow(window_);if(atom_)UnregisterClassW(MAKEINTATOM(atom_),GetModuleHandleW(nullptr));}
    HWND get()const{return window_;}
private:HWND window_{};ATOM atom_{};
};
std::string utf8(const std::wstring& value) {
    require(value.size()<=32768,"Argument is too long");
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    require(count>0,"Argument is not valid Unicode");std::string result(static_cast<std::size_t>(count),'\0');
    require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr)==count,"Cannot decode argument");return result;
}
#endif
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    try {
        require(argc==6||(argc==7&&std::wstring(argv[6])==L"--hardware"),
            "Usage: replay_source_packet packet-root compiled-shaders.json frame-name hud.hlsl new-output-directory [--hardware]");
        const auto sourceRoot=absoluteRoot(fs::path(argv[1]));packet::Package source(sourceRoot);
        CompiledShaders shaders(absoluteOrdinary(fs::path(argv[2])),source);
        const auto frameName=utf8(argv[3]);const auto frame=source.loadFrame(frameName);const auto prepared=prepare(source,frame,shaders);
        const auto shader=absoluteOrdinary(fs::path(argv[4]));const auto output=absoluteRoot(fs::path(argv[5]));
        OwnedWindow window(prepared.width,prepared.height);gpu::Renderer renderer;
        renderer.initialize(window.get(),prepared.width,prepared.height,{argc==7?gpu::Driver::hardware:gpu::Driver::warpForTests,shader,gpu::RenderTarget::offscreenForTests});
        require(!IsWindowVisible(window.get()),"Replay window unexpectedly became visible");
        renderer.setDrawList({});upload(renderer.sourceGraphics(),prepared);renderer.draw(false);
        const auto image=renderer.readback();const auto counters=renderer.sourceGraphics().stats();
        const std::string pixels(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size());
        Json result=Json::Object{{"schemaVersion",1},{"frame",frameName},{"width",int(image.width)},{"height",int(image.height)},
            {"rowBytes",int(image.rowBytes)},{"pixelFormat","BGRA8_sRGB"},{"alpha","premultiplied"},
            {"driver",argc==7?"hardware":"WARP"},{"nativeLayersRendered",false},{"desktopCaptured",false},{"windowVisible",false},
            {"sourceColorMode","directLDR"},{"sourceBatches",std::int64_t(prepared.sourceBatches)},
            {"originalPassDraws",std::int64_t(counters.draws)},{"meshes",std::int64_t(counters.meshes)},
            {"textures",std::int64_t(counters.textures)},{"pipelines",std::int64_t(counters.pipelines)},
            {"uniforms",std::int64_t(counters.uniforms)},{"payloadBytes",std::int64_t(counters.payloadBytes)},
            {"inactiveUniformsSkipped",std::int64_t(prepared.inactiveUniforms)},{"frames",std::int64_t(counters.frames)},
            {"rawSHA256",hash(pixels)},{"rawFile","source.raw-bgra.bin"},
            {"limitations",Json::Array{"Frozen original GPU frame only; native UI layers are not rendered","Source HDR/postprocess is explicitly rejected"}}};
        Json::Array keys;for(const auto& key:prepared.shaderKeys)keys.emplace_back(key);result["shaderKeys"]=keys;
        writeNew(output/"source.raw-bgra.bin",pixels,128*1024*1024);writeNew(output/"replay-stats.json",result.encode(),4*1024*1024);
        renderer.reset();std::cout<<"Replayed "<<counters.draws<<" original passes into an owned hidden "<<image.width<<"x"<<image.height<<" target\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
#endif
