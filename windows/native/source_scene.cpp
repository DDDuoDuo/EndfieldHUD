#include "native/source_scene.hpp"
#include "native/renderer.hpp"
#include "core/shell_packet.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <limits>
#include <vector>
using ehud::data::Json;
namespace packet = endfield::core::packet;
namespace gpu = endfield::native;
namespace fs = std::filesystem;
namespace {
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
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
    require(value.is_absolute() && value.lexically_normal() == value && value.native().find(fs::path::value_type{})==fs::path::string_type::npos,
            "Replay paths must be explicit absolute paths without NUL characters");
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
struct OriginalGeometry {
    unsigned stride{},colorOffset{};
    std::string vertices;
    std::vector<std::uint32_t> indices;
    std::vector<std::array<float,4>> colors;
};
struct PreparedMesh {
    unsigned stride{};
    std::shared_ptr<const OriginalGeometry> original;
    std::string vertices;
    std::uint64_t revision{1};
    bool dirty{true};
};
struct PreparedTexture { gpu::SourceTexture description; std::vector<packet::BinaryData> levels; };
struct PreparedPipeline {
    gpu::SourcePipeline description;
    unsigned stride{};
    std::shared_ptr<const std::string> vertexCode,fragmentCode;
};
enum Dependency : unsigned { worldDependency=1,cameraDependency=2,timeDependency=4,screenDependency=8,flagsDependency=16,accentDependency=32,overrideDependency=64 };
enum class Dynamic { none,world,viewProjection,viewNoTranslation,projection,inverseView,uiProjection,cameraPosition,uiTime,time,screen,renderPath,flipX,flipY };
struct UniformField {
    std::string name;
    unsigned offset{};
    Dynamic dynamic{};
    bool hasValue{},isColor{};
    std::vector<float> value;
    std::optional<std::size_t> overrideIndex;
};
struct UniformCell {
    std::size_t owner{};
    unsigned dependencies{};
    bool accentAllowed{},dirty{true},active{true},reactivated{};
    std::string bytes,scratch;
    std::vector<UniformField> fields;
};
struct PreparedReplay {
    unsigned width{}, height{};
    std::map<std::string,PreparedMesh,std::less<>> meshes;
    std::map<std::string,PreparedTexture,std::less<>> textures;
    std::map<std::string,gpu::SourceSceneProvenance,std::less<>> textureDependencies;
    std::map<std::string,PreparedPipeline,std::less<>> pipelines;
    std::map<std::string,UniformCell,std::less<>> uniforms;
    std::map<std::string,std::shared_ptr<const OriginalGeometry>,std::less<>> originalGeometry;
    gpu::SourceFrameParameters parameters;
    std::vector<gpu::SourceBatchState> batches;
    std::vector<std::string> batchMeshes;
    std::vector<std::size_t> drawOwners;
    std::vector<bool> worldChanged;
    std::vector<bool> overridesChanged;
    std::vector<gpu::SourceDraw> activeDraws;
    bool visibilityDirty{true};
    gpu::SourceSceneStats counters;
    std::vector<gpu::SourceDraw> draws;
    std::size_t sourceBatches{}, inactiveUniforms{};
    std::set<std::string> shaderKeys;
};
struct BatchPrototype {
    gpu::SourceBatchTemplate description;
    std::string geometryID;
    gpu::SourceFrameParameters originalParameters;
    gpu::SourceSceneProvenance provenance;
    std::shared_ptr<const OriginalGeometry> geometry;
    std::vector<gpu::SourceDraw> draws;
    std::map<std::string,UniformCell,std::less<>> cells;
};
struct RuntimeSlot {
    std::size_t prototype{},index{};
    bool configured{};
    std::shared_ptr<OriginalGeometry> mutableGeometry;
    std::vector<gpu::SourceDraw> draws;
};

endfield::core::Matrix4 matrix(const Json& value) {
    const auto& columns=value.array();require(columns.size()==4,"Submitted source matrix is not4x4");
    endfield::core::Matrix4 out;
    for(unsigned c=0;c<4;++c){require(columns[c].array().size()==4,"Submitted matrix column is not4D");for(unsigned r=0;r<4;++r)out.values[c*4+r]=columns[c].array()[r].number();}
    require(out.finite(),"Nonfinite submitted source matrix");return out;
}
gpu::SourceFrameParameters parameters(const packet::FrameData& frame) {
    const auto& c=frame.metadata["gpuCamera"];gpu::SourceFrameParameters p;
    p.width=static_cast<unsigned>(frame.viewport.x);p.height=static_cast<unsigned>(frame.viewport.y);
    p.camera.viewProjection=frame.gpuViewProjection;p.camera.viewNoTranslationProjection=frame.gpuViewNoTranslationProjection;
    p.camera.projection=matrix(c["projection"]);p.camera.inverseView=matrix(c["inverseView"]);
    require(c["worldSpacePosition"].array().size()==3&&c["uiProjectionParameters"].array().size()==4,"Source camera parameters are missing");
    for(unsigned i=0;i<3;++i)p.camera.worldPosition[i]=c["worldSpacePosition"].array()[i].number();
    for(unsigned i=0;i<4;++i)p.camera.uiProjectionParameters[i]=static_cast<float>(c["uiProjectionParameters"].array()[i].number());
    p.timeSeconds=static_cast<float>(c["timeSeconds"].number());p.renderPathInjected=static_cast<float>(c["renderPathInjected"].number());
    p.flipX=static_cast<float>(c["flipX"].number());p.flipY=static_cast<float>(c["flipY"].number());
    if(!c["desktopAccentLinear"].isNull()){
        require(c["desktopAccentLinear"].array().size()==3,"Invalid source accent");std::array<float,3> accent;
        for(unsigned i=0;i<3;++i)accent[i]=static_cast<float>(c["desktopAccentLinear"].array()[i].number());p.desktopAccentLinear=accent;
    }return p;
}
Dynamic dynamic(const Json& value) {
    static const std::map<std::string,Dynamic,std::less<>> values{{"none",Dynamic::none},{"world",Dynamic::world},
        {"viewProjection",Dynamic::viewProjection},{"viewNoTranslation",Dynamic::viewNoTranslation},{"projection",Dynamic::projection},
        {"inverseView",Dynamic::inverseView},{"uiProjection",Dynamic::uiProjection},{"cameraPosition",Dynamic::cameraPosition},
        {"uiTime",Dynamic::uiTime},{"time",Dynamic::time},{"screen",Dynamic::screen},{"renderPath",Dynamic::renderPath},{"flipX",Dynamic::flipX},{"flipY",Dynamic::flipY}};
    const auto found=values.find(value.string());require(found!=values.end(),"Unsupported source uniform field plan");return found->second;
}
unsigned dependency(Dynamic d) {
    switch(d){case Dynamic::world:return worldDependency;
    case Dynamic::viewProjection:case Dynamic::viewNoTranslation:case Dynamic::projection:case Dynamic::inverseView:case Dynamic::uiProjection:case Dynamic::cameraPosition:return cameraDependency;
    case Dynamic::time:case Dynamic::uiTime:return timeDependency;case Dynamic::screen:return screenDependency;
    case Dynamic::renderPath:case Dynamic::flipX:case Dynamic::flipY:return flagsDependency;case Dynamic::none:return 0;}
    return 0;
}
std::array<float,4> accentColor(std::array<float,4> color,const std::optional<std::array<float,3>>& accent) {
    if(accent&&color[0]>0&&color[1]>=color[0]*.25f&&color[1]<=color[0]*1.1f&&color[2]>=0&&color[2]<std::min(color[0],color[1])*.5f){
        const auto intensity=std::max(color[0],color[1]);for(unsigned i=0;i<3;++i)color[i]=(*accent)[i]*intensity;
    }return color;
}
void put(std::string& data,unsigned offset,std::span<const float> value) {
    if(offset>data.size()||value.size_bytes()>data.size()-offset)return; // same all-or-nothing Mac field write
    std::memcpy(data.data()+offset,value.data(),value.size_bytes());
}
void put(std::string& data,unsigned offset,const endfield::core::Matrix4& matrix) {
    std::array<float,16> value;for(unsigned i=0;i<16;++i)value[i]=static_cast<float>(matrix.values[i]);put(data,offset,value);
}
void encode(UniformCell& cell,const gpu::SourceBatchState& batch,const gpu::SourceFrameParameters& p) {
    std::fill(cell.scratch.begin(),cell.scratch.end(),'\0');
    for(const auto& field:cell.fields) {
        if(field.hasValue){
            const auto& value=field.overrideIndex?batch.uniformOverrides[*field.overrideIndex].value:field.value;
            if(field.isColor&&value.size()==4&&cell.accentAllowed){
                const auto color=accentColor({value[0],value[1],value[2],value[3]},p.desktopAccentLinear);put(cell.scratch,field.offset,color);
            }else put(cell.scratch,field.offset,value);
            continue;
        }
        const auto t=p.timeSeconds;
        switch(field.dynamic){
        case Dynamic::world:put(cell.scratch,field.offset,batch.world);break;
        case Dynamic::viewProjection:put(cell.scratch,field.offset,p.camera.viewProjection);break;
        case Dynamic::viewNoTranslation:put(cell.scratch,field.offset,p.camera.viewNoTranslationProjection);break;
        case Dynamic::projection:put(cell.scratch,field.offset,p.camera.projection);break;
        case Dynamic::inverseView:put(cell.scratch,field.offset,p.camera.inverseView);break;
        case Dynamic::uiProjection:put(cell.scratch,field.offset,p.camera.uiProjectionParameters);break;
        case Dynamic::cameraPosition:{const std::array<float,4> value{float(p.camera.worldPosition[0]),float(p.camera.worldPosition[1]),float(p.camera.worldPosition[2]),0};put(cell.scratch,field.offset,value);break;}
        case Dynamic::uiTime:{const std::array<float,4> value{t*.05f,t,t*2,0};put(cell.scratch,field.offset,value);break;}
        case Dynamic::time:{const std::array<float,4> value{t/20,t,t*2,t*3};put(cell.scratch,field.offset,value);break;}
        case Dynamic::screen:{const std::array<float,4> value{float(p.width),float(p.height),1/float(p.width),1/float(p.height)};put(cell.scratch,field.offset,value);break;}
        case Dynamic::renderPath:put(cell.scratch,field.offset,std::span(&p.renderPathInjected,1));break;
        case Dynamic::flipX:put(cell.scratch,field.offset,std::span(&p.flipX,1));break;
        case Dynamic::flipY:put(cell.scratch,field.offset,std::span(&p.flipY,1));break;
        case Dynamic::none:break;
        }
    }
}
std::pair<std::string,UniformCell> uniformCell(const packet::UniformDescriptor& descriptor,std::string payload,const Json& pass,
        const Json& batch,const std::string& stageName,std::size_t batchIndex) {
    UniformCell cell;cell.owner=batchIndex;cell.accentAllowed=batch["appliesDesktopAccent"].boolean();cell.bytes=std::move(payload);
    cell.scratch.resize(cell.bytes.size());Json::Array semantic;
    for(const auto& record:descriptor.metadata["fields"].array()) {
        UniformField f;f.name=text(record["name"]);f.offset=number(record["offset"],65536);f.isColor=record["isColor"].boolean();f.dynamic=dynamic(record["dynamic"]);
        const auto& override=batch["uniformOverrides"][record["name"].string()];const auto& value=override.isNull()?record["value"]:override;
        f.hasValue=!value.isNull();
        if(f.hasValue){require(value.array().size()<=16384,"Unbounded source field value");for(const auto& x:value.array()){const auto v=static_cast<float>(x.number());require(std::isfinite(v),"Nonfinite static source field value");f.value.push_back(v);}
            if(f.isColor&&f.value.size()==4&&cell.accentAllowed)cell.dependencies|=accentDependency;
        }else cell.dependencies|=dependency(f.dynamic);
        if(!override.isNull()){
            std::size_t index{};for(const auto& [name,data]:batch["uniformOverrides"].object()){
                (void)data;if(name==record["name"].string())f.overrideIndex=index;++index;
            }
            require(f.overrideIndex.has_value(),"Source override plan has no typed value");cell.dependencies|=overrideDependency;
        }
        Json entry=record;entry["value"]=value;semantic.push_back(std::move(entry));cell.fields.push_back(std::move(f));
    }
    const auto owner=(cell.dependencies&(worldDependency|overrideDependency))?":batch:"+std::to_string(batchIndex):std::string{};
    const auto id="uniform:"+hash(text(pass["stages"][stageName]["file"])+descriptor.bufferName+Json(semantic).encode()+
        (cell.accentAllowed?"accent":"neutral")+cell.bytes)+owner;
    return {id,std::move(cell)};
}

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
void tint(PreparedMesh& mesh,const std::array<float,4>& color) {
    const auto& original=*mesh.original;
    std::copy(original.vertices.begin(),original.vertices.end(),mesh.vertices.begin());
    for(std::size_t vertex=0;vertex<original.colors.size();++vertex)for(unsigned component=0;component<4;++component) {
        const auto channel=original.colors[vertex][component]*color[component];
        require(std::isfinite(channel),"Submitted source vertex color overflow");
        std::memcpy(mesh.vertices.data()+vertex*original.stride+original.colorOffset+component*4,&channel,4);
    }
}
std::string addMesh(PreparedReplay& output,const packet::Package& source,const Json& batch,std::size_t batchIndex) {
    require(std::endian::native==std::endian::little,"Native source vertex replay needs a little-endian host");
    const auto id=text(batch["mesh"]);
    if(!output.originalGeometry.contains(id)) {
        const auto& m=source.meshes().at(id);auto original=std::make_shared<OriginalGeometry>();
        original->stride=number(m.metadata["originalVertexBuffer"]["stride"],256);
        original->colorOffset=number(m.metadata["originalVertexBuffer"]["offsets"]["color"],240);
        require(original->stride>=original->colorOffset+16,"Original vertex color does not fit source stride");
        original->vertices=source.loadOriginalVertexBuffer(id).storage;auto canonical=source.loadMesh(id);
        original->indices=std::move(canonical.indices);original->colors.reserve(canonical.vertices.size());
        for(const auto& v:canonical.vertices)original->colors.push_back(v.color);
        output.originalGeometry.emplace(id,std::move(original));
    }
    const auto key="mesh:batch:"+std::to_string(batchIndex);const auto original=output.originalGeometry.at(id);
    PreparedMesh mesh{original->stride,original,original->vertices};tint(mesh,output.batches.at(batchIndex).vertexColor);
    output.meshes.emplace(key,std::move(mesh));return key;
}
PreparedPipeline pipeline(const Json& pass, const Json& batch, CompiledShaders& shaders) {
    PreparedPipeline result; auto& p = result.description;
    result.vertexCode=std::make_shared<const std::string>(shaders.get(pass,"vertex").code);
    result.fragmentCode=std::make_shared<const std::string>(shaders.get(pass,"fragment").code);
    p.vertexBytecode=bytes(*result.vertexCode);p.fragmentBytecode=bytes(*result.fragmentCode);
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
        auto compiled=uniformCell(*descriptor,std::move(payload.storage),pass,batch,stageName,batchIndex);
        const auto uniformID=compiled.first;auto& cell=compiled.second;
        encode(cell,output.batches.at(batchIndex),output.parameters);
        require(cell.bytes==cell.scratch,"Typed initial uniform writer differs from exact Mac payload: "+context);
        output.uniforms.emplace(uniformID,std::move(cell));
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
        draw.textures.push_back({sourceStage,number((*image)["slot"],127),number((*sampler)["slot"],15),textureID,property});
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
    result.parameters=parameters(frame);
    const auto& batches=frame.metadata["batches"].array();result.sourceBatches=batches.size();result.batches.reserve(batches.size());
    for(const auto& batch:batches){
        gpu::SourceBatchState state;state.sourceNodeID=text(batch["sourceNodeID"]);state.sourceMesh=text(batch["sourceMesh"]);state.material=text(batch["material"]);
        state.world=matrix(batch["worldMatrix"]);require(batch["gpuVertexColor"].array().size()==4,"Missing source submitted tint");
        for(unsigned i=0;i<4;++i)state.vertexColor[i]=static_cast<float>(batch["gpuVertexColor"].array()[i].number());
        for(const auto& [name,data]:batch["uniformOverrides"].object()){
            gpu::SourceUniformOverride override;override.name=name;for(const auto& value:data.array())override.value.push_back(static_cast<float>(value.number()));state.uniformOverrides.push_back(std::move(override));
        }
        result.batches.push_back(std::move(state));
    }
    result.worldChanged.resize(batches.size());result.overridesChanged.resize(batches.size());
    for(std::size_t index=0;index<batches.size();++index) {
        const auto& batch=batches[index];const auto meshID=addMesh(result,source,batch,index);result.batchMeshes.push_back(meshID);
        const auto& mesh=result.meshes.at(meshID);const auto& material=source.materials().at(text(batch["material"]));
        for(const auto& pass:material["passes"].array()) {
            const auto key=text(pass["id"])+":"+hash(batch["stencil"].encode()+batch["colorWriteMask"].encode());
            if(!result.pipelines.contains(key))result.pipelines.emplace(key,pipeline(pass,batch,shaders));
            require(result.pipelines.at(key).stride==mesh.stride,"Source pass and raw mesh strides differ");
            gpu::SourceDraw draw;draw.mesh=meshID;draw.pipeline=key;
            draw.indexCount=static_cast<unsigned>(mesh.original->indices.size());
            if(!batch["indexRange"].isNull()) {
                const auto& range=batch["indexRange"].array();require(range.size()==2,"Invalid source triangle range");
                draw.firstIndex=number(range[0],static_cast<unsigned>(mesh.original->indices.size()));
                const auto upper=number(range[1],static_cast<unsigned>(mesh.original->indices.size()));
                require(upper>=draw.firstIndex,"Invalid source triangle range");draw.indexCount=upper-draw.firstIndex;
            }
            require(draw.indexCount>0&&draw.indexCount%3==0,"Empty or incomplete source triangle range");
            draw.stencilReference=number(batch["stencil"].isNull()?pass["stencilReference"]:batch["stencil"]["reference"],255);
            bindStage(result,draw,source,frame,index,material,pass,batch,shaders,"vertex");
            bindStage(result,draw,source,frame,index,material,pass,batch,shaders,"fragment");
            result.shaderKeys.insert(text(pass["shaderKey"]));result.drawOwners.push_back(index);result.draws.push_back(std::move(draw));
        }
    }
    require(!result.draws.empty(),"Frame has no original GPU passes");result.activeDraws=result.draws;return result;
}
bool finiteMatrix(const endfield::core::Matrix4& m) {
    return m.finite()&&std::all_of(m.values.begin(),m.values.end(),[](double x){return std::isfinite(static_cast<float>(x));});
}
void validate(const gpu::SourceFrameParameters& p) {
    require(p.width>0&&p.height>0&&p.width<=4096&&p.height<=4096&&std::uint64_t(p.width)*p.height<=gpu::Renderer::maximumRenderPixels,"Invalid source viewport dimensions");
    require(finiteMatrix(p.camera.projection)&&finiteMatrix(p.camera.viewProjection)&&finiteMatrix(p.camera.viewNoTranslationProjection)&&finiteMatrix(p.camera.inverseView),"Invalid submitted source camera matrix");
    for(const auto x:p.camera.worldPosition)require(std::isfinite(static_cast<float>(x)),"Invalid camera position");
    for(const auto x:p.camera.uiProjectionParameters)require(std::isfinite(x),"Invalid UI projection parameter");
    require(std::isfinite(p.timeSeconds)&&std::isfinite(p.timeSeconds*3)&&std::isfinite(p.renderPathInjected)&&std::isfinite(p.flipX)&&std::isfinite(p.flipY),"Invalid source time/flags");
    if(p.desktopAccentLinear)for(const auto x:*p.desktopAccentLinear)require(std::isfinite(x),"Nonfinite source theme accent");
}
bool sameCamera(const endfield::core::source::GPUCamera& a,const endfield::core::source::GPUCamera& b) {
    return a.projection==b.projection&&a.viewProjection==b.viewProjection&&a.viewNoTranslationProjection==b.viewNoTranslationProjection&&
        a.inverseView==b.inverseView&&a.worldPosition==b.worldPosition&&a.uiProjectionParameters==b.uiProjectionParameters;
}
void visibleDraws(PreparedReplay& s) {
    s.activeDraws.clear();
    for(std::size_t i=0;i<s.draws.size();++i)if(s.batches[s.drawOwners[i]].visible)s.activeDraws.push_back(s.draws[i]);
}
#include "source_scene_binary.inc"
#include "source_scene_live.inc"
} // namespace
namespace endfield::native {
struct SourceScene::Impl {
    PreparedReplay scene;
    std::vector<std::string> keys;
    SourceSceneProvenance provenance;
    std::vector<SourceSceneProvenance> catalogProvenance;
    std::vector<BatchPrototype> prototypes;
    std::vector<SourceBatchTemplate> descriptors;
    std::vector<std::string> ids;
    std::map<std::string,RuntimeSlot,std::less<>> slots;
    std::map<std::string,std::size_t,std::less<>> prototypeIDs;
    std::vector<SourceBatchState> nextStates;
    std::vector<std::size_t> order,seen;
    std::vector<std::uint8_t> imageVertices;
    std::size_t submissionGeneration{};
    bool assembled{};
    SourceGraphics* uploadedTarget{}; // identity only, never retained/dereferenced on destruction
    void initializeLive(std::vector<BatchPrototype> extra={}){
        prototypes=::prototypes(scene);const auto initialCount=prototypes.size();
        for(auto& p:prototypes)p.provenance=provenance;
        for(auto& p:extra){describePrototype(p,scene);prototypes.push_back(std::move(p));}
        descriptors.reserve(prototypes.size());ids.reserve(4096);scene.batches.reserve(4096);scene.batchMeshes.reserve(4096);
        nextStates.reserve(4096);order.reserve(4096);seen.resize(4096);scene.worldChanged.resize(4096);scene.overridesChanged.resize(4096);
        for(std::size_t i=0;i<prototypes.size();++i){auto& p=prototypes[i];auto id=p.description.id;require(prototypeIDs.emplace(id,i).second,"Source template ID is duplicated");
            descriptors.push_back(p.description);if(i<initialCount){ids.push_back(id);slots.emplace(id,RuntimeSlot{i,i,false,{},p.draws});}}
        nextStates=scene.batches;
    }
};
SourceScene::SourceScene(fs::path packetRoot,fs::path compiled,std::string frameName):impl_(std::make_unique<Impl>()) {
    packet::Package source(absoluteRoot(packetRoot));CompiledShaders shaders(absoluteOrdinary(compiled),source);
    const auto frame=source.loadFrame(frameName);impl_->scene=prepare(source,frame,shaders);
    validate(impl_->scene.parameters);impl_->keys.assign(impl_->scene.shaderKeys.begin(),impl_->scene.shaderKeys.end());
    impl_->provenance={hash(read(packetRoot/"shell-packet.json",packet::Package::maximumJSONBytes)),
        source.frames().at(frameName).data.sha256,hash(read(compiled,32*1024*1024)),std::move(frameName)};
    impl_->catalogProvenance.push_back(impl_->provenance);impl_->initializeLive();
    // Large source frame/node/native-layer JSON and unused packet assets are
    // released here. Typed field plans and used raw upload bytes remain.
}
SourceScene::SourceScene(CompiledSourceScene compiled):impl_(std::make_unique<Impl>()) {
    auto file=read(absoluteOrdinary(compiled.file),maximumCompiledBytes);
    if(!compiled.expectedSHA256.empty())require(validSHA(compiled.expectedSHA256)&&hash(file)==compiled.expectedSHA256,"Compiled source shipping hash differs");
    auto loaded=loadCompiled(file);impl_->scene=std::move(loaded.scene);impl_->provenance=std::move(loaded.provenance);
    impl_->catalogProvenance=std::move(loaded.catalogProvenance);
    impl_->keys.assign(impl_->scene.shaderKeys.begin(),impl_->scene.shaderKeys.end());
    impl_->initializeLive(std::move(loaded.extra));
}
SourceScene::~SourceScene()=default;
const SourceFrameParameters& SourceScene::parameters()const noexcept{return impl_->scene.parameters;}
std::span<const SourceBatchState> SourceScene::batches()const noexcept{return impl_->scene.batches;}
const std::vector<std::string>& SourceScene::shaderKeys()const noexcept{return impl_->keys;}
const SourceSceneProvenance& SourceScene::provenance()const noexcept{return impl_->provenance;}
std::span<const SourceSceneProvenance> SourceScene::catalogProvenance()const noexcept{return impl_->catalogProvenance;}
std::span<const SourceBatchTemplate> SourceScene::templates()const noexcept{return impl_->descriptors;}
std::span<const std::string> SourceScene::stateIDs()const noexcept{return impl_->ids;}
void SourceScene::includeTemplates(const SourceScene& other,std::string prefix) {
    auto& impl=*impl_;auto& s=impl.scene;const auto& input=other.impl_->scene;
    require(&other!=this&&!impl.uploadedTarget&&!impl.assembled&&s.counters.updates==0,"Install source catalogs only before first upload/submission");
    require(validIdentity(prefix)&&prefix.size()<=64,"Source catalog namespace must be explicit and bounded");
    require(impl.prototypes.size()+other.impl_->prototypes.size()<=4096,"Source prototype catalog exceeds bounds");
    for(const auto& p:other.impl_->prototypes){const auto id=prefix+":"+p.description.id;require(validIdentity(id),"Namespaced source prototype ID exceeds bounds");require(!impl.prototypeIDs.contains(id),"Source catalog namespace/prototype already installed");}
    for(const auto& [id,p]:input.pipelines)if(const auto found=s.pipelines.find(id);found!=s.pipelines.end()){
        const auto& d=found->second.description;const auto& v=p.description;
        require(*found->second.vertexCode==*p.vertexCode&&*found->second.fragmentCode==*p.fragmentCode&&found->second.stride==p.stride&&
            d.attributes.size()==v.attributes.size()&&d.blendEnabled==v.blendEnabled&&d.sourceRGB==v.sourceRGB&&d.destinationRGB==v.destinationRGB&&
            d.sourceAlpha==v.sourceAlpha&&d.destinationAlpha==v.destinationAlpha&&d.rgbOperation==v.rgbOperation&&d.alphaOperation==v.alphaOperation&&d.writeMask==v.writeMask&&
            d.depthWrite==v.depthWrite&&d.stencilEnabled==v.stencilEnabled&&d.depthCompare==v.depthCompare&&d.cull==v.cull,"Same source pipeline ID has differing immutable programs/state");
        for(std::size_t i=0;i<d.attributes.size();++i)require(d.attributes[i].location==v.attributes[i].location&&d.attributes[i].components==v.attributes[i].components&&d.attributes[i].byteOffset==v.attributes[i].byteOffset,"Source pipeline attributes conflict");
        const auto equalFace=[](const SourceStencilFace& a,const SourceStencilFace& b){return a.compare==b.compare&&a.fail==b.fail&&a.depthFail==b.depthFail&&a.pass==b.pass&&a.readMask==b.readMask&&a.writeMask==b.writeMask;};
        require(equalFace(d.front,v.front)&&equalFace(d.back,v.back),"Source pipeline stencil faces conflict");}
    std::size_t textureBytes{};for(const auto& [id,t]:s.textures){(void)id;for(const auto& b:t.levels)textureBytes+=b.storage.size();}
    for(const auto& [id,t]:input.textures)if(const auto found=s.textures.find(id);found!=s.textures.end()){
        const auto& a=found->second.description;const auto& b=t.description;require(a.pixelFormat==b.pixelFormat&&a.minFilter==b.minFilter&&a.magFilter==b.magFilter&&a.mipFilter==b.mipFilter&&
            a.addressU==b.addressU&&a.addressV==b.addressV&&a.addressW==b.addressW&&a.maxAnisotropy==b.maxAnisotropy&&a.mips.size()==b.mips.size(),"Source texture ID has differing immutable sampler/format");
        for(std::size_t i=0;i<a.mips.size();++i)require(a.mips[i].width==b.mips[i].width&&a.mips[i].height==b.mips[i].height&&a.mips[i].rowBytes==b.mips[i].rowBytes&&found->second.levels[i].storage==t.levels[i].storage,"Source texture ID has differing original mip bytes");
    }else for(const auto& b:t.levels){require(b.storage.size()<=128*1024*1024-textureBytes,"Source catalog texture bytes exceed bounds");textureBytes+=b.storage.size();}
    require(s.pipelines.size()+input.pipelines.size()<=2048&&s.textures.size()+input.textures.size()<=2048,"Source catalog resources exceed bounds");
    for(const auto& [id,g]:input.originalGeometry)if(const auto found=s.originalGeometry.find(id);found!=s.originalGeometry.end())
        require(found->second->stride==g->stride&&found->second->colorOffset==g->colorOffset&&found->second->vertices==g->vertices&&found->second->indices==g->indices&&found->second->colors==g->colors,"Source catalog geometry identity has differing immutable bytes");
    std::size_t geometryBytes{};for(const auto& [id,g]:s.originalGeometry){(void)id;geometryBytes+=g->vertices.size()+g->indices.size()*4+g->colors.size()*16;}
    for(const auto& [id,g]:input.originalGeometry)if(!s.originalGeometry.contains(id)){const auto cost=g->vertices.size()+g->indices.size()*4+g->colors.size()*16;
        require(cost<=128*1024*1024-geometryBytes,"Source catalog original geometry bytes exceed bounds");geometryBytes+=cost;}
    require(s.originalGeometry.size()+input.originalGeometry.size()<=8192,"Source catalog geometry records exceed bounds");
    for(const auto& [id,p]:input.pipelines)s.pipelines.emplace(id,p);
    for(const auto& [id,g]:input.originalGeometry)s.originalGeometry.emplace(id,g);
    s.shaderKeys.insert(input.shaderKeys.begin(),input.shaderKeys.end());impl.keys.assign(s.shaderKeys.begin(),s.shaderKeys.end());
    for(const auto& [id,t]:input.textures)if(!s.textures.contains(id)){auto copy=t;for(std::size_t i=0;i<copy.levels.size();++i)copy.description.mips[i].bytes=copy.levels[i].bytes();s.textures.emplace(id,std::move(copy));}
    for(const auto& source:other.impl_->prototypes){auto copy=source;copy.geometry=s.originalGeometry.at(copy.geometryID);
        copy.description.originalGeometry={copy.geometry->stride,bytes(copy.geometry->vertices),copy.geometry->indices,copy.geometry->colors};
        copy.description.id=prefix+":"+source.description.id;const auto index=impl.prototypes.size();
        impl.prototypeIDs.emplace(copy.description.id,index);impl.descriptors.push_back(copy.description);impl.prototypes.push_back(std::move(copy));}
    for(const auto& p:other.impl_->catalogProvenance)if(std::find(impl.catalogProvenance.begin(),impl.catalogProvenance.end(),p)==impl.catalogProvenance.end())impl.catalogProvenance.push_back(p);
    for(const auto& [id,origin]:input.textureDependencies)s.textureDependencies.emplace(id,origin);
}
std::vector<std::string> SourceScene::includeDesktopResources(const fs::path& packetRoot) {
    auto& impl=*impl_;auto& scene=impl.scene;
    require(!impl.uploadedTarget&&!impl.assembled&&scene.counters.updates==0,"Install desktop resource closure only before first upload/submission");
    const auto root=absoluteRoot(packetRoot);packet::Package source(root);
    require(hash(read(root/"shell-packet.json",packet::Package::maximumJSONBytes))==impl.provenance.packetSHA256,
            "Desktop resources must match the compiled scene's source packet provenance");
    const auto animation=source.loadAnimation();const auto& builder=animation["frameBuilder"];const auto& mounted=animation["mountedDocument"];
    require(builder.isObject()&&mounted.isObject(),"Desktop resource closure needs original frameBuilder and mountedDocument metadata");
    require(builder["desktopTextureDependencies"].isArray(),"Desktop resource closure needs a complete source-derived texture dependency export");
    std::set<std::string,std::less<>> dependencies;
    const auto reference=[&](const Json& value,bool mandatory=false){
        if(value.isNull()&&!mandatory)return;
        const auto id=text(value);const bool present=source.textures().contains(id);
        require(!mandatory||present,"Desktop replacement texture is absent from exported source resources: "+id);
        if(present){dependencies.insert(id);require(dependencies.size()<=2048,"Desktop texture closure exceeds resource bound");}
    };
    for(const auto& id:builder["desktopTextureDependencies"].array())reference(id,true);
    // These are source metadata catalogs, not claims that every game asset was
    // exported for this desktop configuration. Only packet-registered resources
    // have validated mip/sampler descriptors available for the shipping cache.
    for(const auto* key:{"sprites","sourceSprites"})for(const auto& [id,sprite]:builder[key].object()){
        (void)id;reference(sprite["textureID"]);
    }
    const auto& settings=builder["desktopSettings"];
    for(const auto& [node,image]:settings["images"].object()){(void)node;reference(image["texture"],true);}
    for(const auto& [node,id]:settings["sprites"].object()){
        (void)node;const auto& sprite=builder["sourceSprites"][text(id)];
        require(sprite.isObject(),"Desktop replacement Sprite is absent from source metadata");
        // An explicit desktop image replaces the Sprite's GPU texture while
        // retaining its authored geometry/border metadata (profile background).
        reference(sprite["textureID"],!settings["images"].contains(node));
    }
    // Fixed material textures are already resolved from the reflected shader
    // bindings by prepare()/includeTemplates(). FrameBuilder only animates their
    // numeric properties; inactive m_TexEnvs defaults are not live dependencies.
    for(const auto& [id,sprite]:mounted["spriteByComponent"].object()){(void)id;reference(sprite["texture"]["id"]);}
    for(const auto& [node,components]:mounted["components"].object()){
        (void)node;for(const auto& component:components.array()){
            const auto kind=component["script"].isString()?component["script"].string():component["type"].string();
            if(kind=="UIRawImage"||kind=="RawImage")reference(component["data"]["m_Texture"]["target_id"]);
        }
    }
    // Sprite-less source UI and null original shader properties use this exact
    // exported source resource; never manufacture a replacement texture.
    reference(Json("__white"),true);
    PreparedReplay staged;std::size_t bytesTotal{};
    for(const auto& [id,texture]:scene.textures){(void)id;for(const auto& mip:texture.levels){require(mip.storage.size()<=maximumCompiledBytes-bytesTotal,"Source texture closure exceeds retained bound");bytesTotal+=mip.storage.size();}}
    for(const auto& id:dependencies)if(!scene.textures.contains(id)){
        addTexture(staged,source,id);
        for(const auto& mip:staged.textures.at(id).levels){require(mip.storage.size()<=maximumCompiledBytes-bytesTotal,"Source texture closure exceeds retained bound");bytesTotal+=mip.storage.size();}
    }
    require(scene.textures.size()+staged.textures.size()<=2048,"Source texture closure exceeds resource bound");
    std::vector<std::string> result(dependencies.begin(),dependencies.end());auto declared=scene.textureDependencies;
    for(const auto& id:dependencies)declared.emplace(id,impl.provenance);
    scene.textures.merge(staged.textures);scene.textureDependencies.swap(declared);return result;
}
SourceProfileHoverMaskReport SourceScene::maskDesktopProfileHoverOutline(const fs::path& packetRoot) {
    auto& impl=*impl_;auto& scene=impl.scene;
    require(!impl.uploadedTarget&&!impl.assembled&&scene.counters.updates==0,"Mask profile artwork only before first upload/submission");
    const auto root=absoluteRoot(packetRoot);packet::Package source(root);
    require(source.manifestSHA256()==impl.provenance.packetSHA256,"Profile artwork must match the scene's source packet provenance");
    constexpr const char* hoverID="desktop.profile.hover";
    constexpr const char* backgroundID="desktop.profile.background";
    require(scene.textures.contains(hoverID)&&scene.textures.contains(backgroundID),"Install original profile textures before masking the hover outline");
    PreparedReplay staged;addTexture(staged,source,hoverID);addTexture(staged,source,backgroundID);
    auto& hover=staged.textures.at(hoverID);const auto& background=staged.textures.at(backgroundID);
    require(hover.description.pixelFormat=="rgba8Unorm_srgb"&&background.description.pixelFormat=="rgba8Unorm_srgb"&&
            hover.levels.size()==1&&background.levels.size()==1,"Profile outline correction requires original single-level straight RGBA8-sRGB artwork");
    const auto& h=hover.description.mips.front();const auto& b=background.description.mips.front();
    require(h.width==b.width&&h.height==b.height&&h.rowBytes==b.rowBytes&&h.rowBytes==h.width*4,
            "Profile hover and background must have identical texel mapping");
    SourceProfileHoverMaskReport report{hash(hover.levels.front().storage),hash(background.levels.front().storage),{},h.width,h.height};
    auto& output=hover.levels.front().storage;const auto& mask=background.levels.front().storage;
    // HUDSourceProfileArtwork.texturePixels explicitly unpremultiplies RGB;
    // the source UI shader premultiplies once after sampling. Keep that palette
    // intact and change only alpha. No gamma operation belongs on coverage.
    for(std::size_t alpha=3;alpha<output.size();alpha+=4){
        const auto before=static_cast<unsigned char>(output[alpha]);
        const auto coverage=static_cast<unsigned char>(mask[alpha]);
        const auto after=static_cast<unsigned char>((unsigned(before)*coverage+127)/255);
        report.changedAlphaPixels+=after!=before;report.clearedFringePixels+=before!=0&&after==0;
        output[alpha]=static_cast<char>(after);
    }
    report.maskedSHA256=hash(output);hover.description.mips.front().bytes=hover.levels.front().bytes();
    // All validation/allocation above is staged; committing one owned texture
    // cannot affect any source geometry or partially alter the installed image.
    scene.textures.at(hoverID)=std::move(hover);return report;
}
bool SourceScene::assembleFrame(std::span<const SourceAssembledBatch> entries,const SourceFrameParameters& params) {
    auto& impl=*impl_;auto& s=impl.scene;validate(params);require(entries.size()<=4096,"Too many live source batches");
    std::vector<SourceAssembledBatch> converted;std::vector<std::vector<std::uint8_t>> convertedVertices;
    if(std::any_of(entries.begin(),entries.end(),[](const auto& b){return b.imageGeometry.has_value();})){
        converted.assign(entries.begin(),entries.end());convertedVertices.resize(entries.size());
        for(std::size_t i=0;i<entries.size();++i)if(entries[i].imageGeometry){require(!entries[i].geometry,"Submit one source geometry representation");packImage(convertedVertices[i],*entries[i].imageGeometry);
            converted[i].geometry=SourceGeometryView{80,convertedVertices[i],entries[i].imageGeometry->indices,{}};converted[i].imageGeometry.reset();}
        entries=converted;
    }
    if(++impl.submissionGeneration==0){std::fill(impl.seen.begin(),impl.seen.end(),0);++impl.submissionGeneration;}
    struct Install {std::string id,mesh;RuntimeSlot slot;SourceBatchState state;std::map<std::string,UniformCell,std::less<>> cells;std::optional<PreparedMesh> newMesh;bool append{};};
    std::vector<Install> installs;
    // Scratch indices and validation do not alter renderable state. Invalid
    // identities/resources/ranges/colors/field widths reject the whole frame.
    std::size_t added{},submittedDrawCount{};impl.order.clear();
    for(std::size_t at=0;at<entries.size();++at){const auto& input=entries[at];const auto prototype=impl.prototypeIDs.find(input.prototypeID);
        require(prototype!=impl.prototypeIDs.end(),"Requested source prototype is not installed");const auto& p=impl.prototypes[prototype->second];
        require(p.draws.size()<=16384-submittedDrawCount,"Live source pass count exceeds bounds");submittedDrawCount+=p.draws.size();
        auto existing=impl.slots.find(input.stateID);std::size_t index;
        if(existing==impl.slots.end()){for(std::size_t j=0;j<at;++j)require(entries[j].stateID!=input.stateID,"Duplicate live state identity");index=s.batches.size()+added++;
            require(index<4096,"Retained source state slots exceed bounds");}
        else{index=existing->second.index;require(impl.seen[index]!=impl.submissionGeneration,"Duplicate live state identity");impl.seen[index]=impl.submissionGeneration;}
        const auto current=(existing==impl.slots.end()||existing->second.prototype!=prototype->second)?p.geometry:s.meshes.at(s.batchMeshes[index]).original;
        validateState(input,p,s,input.geometry?input.geometry->indices.size():current->indices.size());
        // A complete shader writer also validates computed color products before
        // any live state is committed, including newly supplied overrides.
        for(const auto& [id,c]:p.cells){(void)id;if(input.appliesDesktopAccent&&params.desktopAccentLinear)for(const auto& f:c.fields)if(f.isColor){
            const std::vector<float>* value=&f.value;for(const auto& o:input.state.uniformOverrides)if(o.name==f.name)value=&o.value;
            if(value->size()==4){const auto color=accentColor({(*value)[0],(*value)[1],(*value)[2],(*value)[3]},params.desktopAccentLinear);for(auto x:color)require(std::isfinite(x),"Live source accent overflows");}}}
        bool same=existing!=impl.slots.end()&&existing->second.configured&&existing->second.prototype==prototype->second;
        if(same){const auto& old=s.batches[index];same=old.sourceNodeID==input.state.sourceNodeID&&old.sourceMesh==input.state.sourceMesh&&old.material==input.state.material&&old.uniformOverrides.size()==input.state.uniformOverrides.size();
            for(std::size_t i=0;same&&i<old.uniformOverrides.size();++i)same=old.uniformOverrides[i].name==input.state.uniformOverrides[i].name&&old.uniformOverrides[i].value.size()==input.state.uniformOverrides[i].value.size();
            for(const auto& d:existing->second.draws)for(const auto& b:d.uniforms)same&=s.uniforms.at(b.id).accentAllowed==input.appliesDesktopAccent;}
        if(!same){Install install;install.id=input.stateID;install.append=existing==impl.slots.end();install.state=input.state;install.mesh=install.append?"live:mesh:"+input.stateID:s.batchMeshes[index];
            install.slot={prototype->second,index,true,{},p.draws};
            for(auto& draw:install.slot.draws){draw.mesh=install.mesh;for(auto& binding:draw.uniforms){const auto& original=p.cells.at(binding.id);auto cell=instantiate(original,input,index,params);
                    // Pure camera/static plans with unchanged source accent may
                    // keep their shared source identity. Batch-dependent plans
                    // get stable retained identities without per-frame hashing.
                    const auto id=!(cell.dependencies&(worldDependency|overrideDependency))&&cell.accentAllowed==original.accentAllowed?binding.id:
                        "live:uniform:"+input.stateID+":"+binding.id;
                    install.cells.emplace(id,std::move(cell));binding.id=id;}}
            if(install.append||existing->second.prototype!=prototype->second){PreparedMesh mesh{p.geometry->stride,p.geometry,p.geometry->vertices};
                // A retained GPU ID must never reuse an earlier revision when
                // the prototype changes. Geometry updates then advance again.
                if(!install.append){const auto revision=s.meshes.at(install.mesh).revision;require(revision<std::numeric_limits<std::uint64_t>::max(),"Source geometry revision exhausted");mesh.revision=revision+1;}
                tint(mesh,input.state.vertexColor);install.newMesh=std::move(mesh);}
            installs.push_back(std::move(install));}
        impl.order.push_back(index);
    }
    if(!installs.empty()){
        std::set<std::string_view> newUniforms;for(const auto& install:installs)for(const auto& [id,c]:install.cells){(void)c;if(!s.uniforms.contains(id))newUniforms.insert(id);}
        require(s.uniforms.size()+newUniforms.size()<=8192,"Retained source uniform slots exceed bounds");
    }
    // All source contracts are now checked. Structural allocations occur only
    // when installing/reconfiguring identities; steady submissions retain them.
    bool changed=!installs.empty();
    for(auto& install:installs){const auto index=install.slot.index;
        if(install.append){s.batches.push_back(install.state);s.batchMeshes.push_back(install.mesh);impl.ids.push_back(install.id);impl.nextStates.push_back(install.state);}
        else{s.batches[index]=install.state;impl.nextStates[index]=install.state;}
        if(install.newMesh)s.meshes.insert_or_assign(install.mesh,std::move(*install.newMesh));
        else{auto& mesh=s.meshes.at(install.mesh);tint(mesh,install.state.vertexColor);++mesh.revision;mesh.dirty=true;}
        for(auto it=install.cells.begin();it!=install.cells.end();){auto node=install.cells.extract(it++);const auto current=s.uniforms.find(node.key());
            if(current==s.uniforms.end())s.uniforms.insert(std::move(node));else current->second=std::move(node.mapped());}
        impl.slots.insert_or_assign(install.id,std::move(install.slot));
    }
    for(auto& state:impl.nextStates)state.visible=false;
    for(const auto& input:entries){const auto index=impl.slots.at(input.stateID).index;auto& next=impl.nextStates[index];next.world=input.state.world;next.vertexColor=input.state.vertexColor;next.visible=input.state.visible;
        for(std::size_t j=0;j<next.uniformOverrides.size();++j)std::copy(input.state.uniformOverrides[j].value.begin(),input.state.uniformOverrides[j].value.end(),next.uniformOverrides[j].value.begin());}
    for(auto& [id,c]:s.uniforms){(void)id;c.reactivated=!c.active;c.active=false;}
    for(const auto& input:entries){const auto& slot=impl.slots.at(input.stateID);for(const auto& d:slot.draws)for(const auto& binding:d.uniforms){auto& c=s.uniforms.at(binding.id);c.active=true;c.owner=slot.index;}}
    changed|=update(impl.nextStates,params);
    // Geometry validation ran with the submitted tint, and update() finished
    // its own complete finite-value validation before these writes.
    for(const auto& input:entries)if(input.geometry)changed|=updateGeometry(input.stateID,*input.geometry,input.indexRange);
    bool drawsChanged=s.drawOwners.size()==0;
    std::size_t drawCount{};for(const auto& input:entries)drawCount+=impl.slots.at(input.stateID).draws.size();require(drawCount<=16384,"Live source pass count exceeds bounds");
    drawsChanged|=drawCount!=s.draws.size();std::size_t cursor{};
    std::size_t maxTextureID{};for(const auto& [id,t]:s.textures){(void)t;maxTextureID=std::max(maxTextureID,id.size());}
    for(const auto& input:entries){auto& slot=impl.slots.at(input.stateID);const auto& p=impl.prototypes[slot.prototype];const auto indexCount=s.meshes.at(s.batchMeshes[slot.index]).original->indices.size();
        for(std::size_t pass=0;pass<slot.draws.size();++pass){auto& d=slot.draws[pass];const auto first=input.indexRange?input.indexRange->firstIndex:0;
            const auto count=input.indexRange?input.indexRange->indexCount:static_cast<unsigned>(indexCount),ref=input.stencilReference?*input.stencilReference:p.draws[pass].stencilReference;
            drawsChanged|=d.firstIndex!=first||d.indexCount!=count||d.stencilReference!=ref;d.firstIndex=first;d.indexCount=count;d.stencilReference=ref;
            for(auto& t:d.textures){const auto& original=p.draws[pass].textures;const auto found=std::find_if(original.begin(),original.end(),[&](const auto& v){return v.stage==t.stage&&v.slot==t.slot;});
                const std::string* texture=&found->id;for(const auto& value:input.textureOverrides)if(value.name==t.propertyName)texture=&value.textureID;
                if(t.id!=*texture){t.id.reserve(maxTextureID);t.id=*texture;drawsChanged=true;}}
            if(!drawsChanged&&cursor<s.draws.size())drawsChanged|=s.drawOwners[cursor]!=slot.index||s.draws[cursor].pipeline!=d.pipeline||s.draws[cursor].mesh!=d.mesh;
            ++cursor;}}
    if(drawsChanged||!installs.empty()){s.draws.clear();s.drawOwners.clear();s.draws.reserve(drawCount);s.drawOwners.reserve(drawCount);
        for(const auto& input:entries){const auto& slot=impl.slots.at(input.stateID);for(const auto& draw:slot.draws){s.draws.push_back(draw);s.drawOwners.push_back(slot.index);}}
        visibleDraws(s);s.visibilityDirty=true;changed=true;}
    impl.assembled=true;return changed;
}
bool SourceScene::updateGeometry(std::string_view id,const SourceGeometryView& input,std::optional<SourceIndexRange> range) {
    auto& impl=*impl_;auto& s=impl.scene;const auto found=impl.slots.find(id);require(found!=impl.slots.end(),"Geometry state ID not installed");auto& slot=found->second;
    const auto& prototype=impl.prototypes[slot.prototype];validateGeometry(input,prototype,s.batches[slot.index].vertexColor);
    if(range)require(range->firstIndex<=input.indices.size()&&range->indexCount>0&&range->indexCount%3==0&&range->indexCount<=input.indices.size()-range->firstIndex,"Geometry source range exceeds triangles");
    auto& mesh=s.meshes.at(s.batchMeshes[slot.index]);const auto count=range?range->indexCount:static_cast<unsigned>(input.indices.size()),first=range?range->firstIndex:0;
    bool drawChanged=false;for(const auto& d:slot.draws)drawChanged|=d.firstIndex!=first||d.indexCount!=count;
    if(equalGeometry(*mesh.original,input)&&!drawChanged)return false;
    if(!equalGeometry(*mesh.original,input)){
        if(!slot.mutableGeometry)slot.mutableGeometry=std::make_shared<OriginalGeometry>();
        // Reserve all buffers before changing values. Repeated slice/fill/quad
        // sizes retain capacity; original source templates are never overwritten.
        auto& geometry=*slot.mutableGeometry;geometry.vertices.reserve(input.vertices.size());geometry.indices.reserve(input.indices.size());geometry.colors.reserve(input.vertices.size()/80);mesh.vertices.reserve(input.vertices.size());
        copyGeometry(geometry,input);mesh.original=slot.mutableGeometry;mesh.vertices.resize(input.vertices.size());tint(mesh,s.batches[slot.index].vertexColor);++mesh.revision;mesh.dirty=true;++s.counters.changedMeshes;
    }
    for(auto& d:slot.draws){d.firstIndex=first;d.indexCount=count;}
    for(std::size_t i=0;i<s.draws.size();++i)if(s.drawOwners[i]==slot.index){s.draws[i].firstIndex=first;s.draws[i].indexCount=count;}
    if(drawChanged){visibleDraws(s);s.visibilityDirty=true;}impl.assembled=true;return true;
}
bool SourceScene::updateGeometry(std::string_view id,const SourceImageGeometryView& input,std::optional<SourceIndexRange> range) {
    auto& data=impl_->imageVertices;packImage(data,input);
    return updateGeometry(id,SourceGeometryView{80,data,input.indices,{}},range);
}
void SourceScene::writeCompiled(const fs::path& output)const {
    require(impl_->scene.counters.updates==0&&!impl_->assembled,"Compile only unmodified source scenes/catalogs; runtime snapshots are rejected");
    absoluteOrdinary(output);require(!ehud::data::detail::readFile(output,maximumCompiledBytes),"Compiled output already exists");
    auto file=saveCompiled(impl_->scene,impl_->provenance,impl_->prototypes,impl_->catalogProvenance);(void)loadCompiled(file);
    ehud::data::detail::replaceFile(output,std::nullopt,file,maximumCompiledBytes);
}
bool SourceScene::update(std::span<const SourceBatchState> states,const SourceFrameParameters& next) {
    auto& s=impl_->scene;validate(next);require(states.size()==s.batches.size(),"Source batch structure changed; explicit scene reload is required");
    // Validate the complete update before touching any current state.
    for(std::size_t i=0;i<states.size();++i){const auto& b=states[i];const auto& old=s.batches[i];
        require(b.sourceNodeID==old.sourceNodeID&&b.sourceMesh==old.sourceMesh&&b.material==old.material,"Immutable source batch identity changed");
        require(b.uniformOverrides.size()==old.uniformOverrides.size(),"Source override structure changed; explicit reload is required");
        for(std::size_t index=0;index<b.uniformOverrides.size();++index){const auto& value=b.uniformOverrides[index];
            require(value.name==old.uniformOverrides[index].name&&value.value.size()==old.uniformOverrides[index].value.size(),"Source override names/widths changed");
            for(const auto x:value.value)require(std::isfinite(x),"Nonfinite source uniform override");
        }
        if(b.world!=old.world)require(finiteMatrix(b.world),"Invalid submitted batch world matrix");
        for(const auto color:b.vertexColor)require(std::isfinite(color),"Nonfinite source vertex tint");
        if(b.vertexColor!=old.vertexColor)for(const auto& color:s.meshes.at(s.batchMeshes[i]).original->colors)
            for(unsigned channel=0;channel<4;++channel)require(std::isfinite(color[channel]*b.vertexColor[channel]),"Submitted vertex tint overflows original color");
    }
    for(const auto& [id,cell]:s.uniforms){(void)id;if(!cell.active)continue;
        const bool overrideChanged=(cell.dependencies&overrideDependency)&&states[cell.owner].uniformOverrides!=s.batches[cell.owner].uniformOverrides;
        if(next.desktopAccentLinear==s.parameters.desktopAccentLinear&&!overrideChanged&&!cell.reactivated)continue;
        if(cell.accentAllowed&&next.desktopAccentLinear)for(const auto& f:cell.fields)if(f.hasValue&&f.isColor&&f.value.size()==4){
            const auto& value=f.overrideIndex?states[cell.owner].uniformOverrides[*f.overrideIndex].value:f.value;
            const auto c=accentColor({value[0],value[1],value[2],value[3]},next.desktopAccentLinear);
            for(const auto x:c)require(std::isfinite(x),"Submitted theme accent overflows original color");
        }
    }
    unsigned changed{};const auto& old=s.parameters;
    if(!sameCamera(old.camera,next.camera))changed|=cameraDependency;
    if(old.timeSeconds!=next.timeSeconds)changed|=timeDependency;
    if(old.width!=next.width||old.height!=next.height)changed|=screenDependency;
    if(old.renderPathInjected!=next.renderPathInjected||old.flipX!=next.flipX||old.flipY!=next.flipY)changed|=flagsDependency;
    if(old.desktopAccentLinear!=next.desktopAccentLinear)changed|=accentDependency;
    bool any=false,visibilityChangedNow=false;
    for(std::size_t i=0;i<states.size();++i){const auto& b=states[i];auto& previous=s.batches[i];
        s.worldChanged[i]=previous.world!=b.world;
        s.overridesChanged[i]=previous.uniformOverrides!=b.uniformOverrides;
        if(s.worldChanged[i]){previous.world=b.world;any=true;}
        if(s.overridesChanged[i]){for(std::size_t index=0;index<previous.uniformOverrides.size();++index)
            std::copy(b.uniformOverrides[index].value.begin(),b.uniformOverrides[index].value.end(),previous.uniformOverrides[index].value.begin());any=true;}
        if(previous.vertexColor!=b.vertexColor){auto& mesh=s.meshes.at(s.batchMeshes[i]);tint(mesh,b.vertexColor);++mesh.revision;mesh.dirty=true;
            previous.vertexColor=b.vertexColor;++s.counters.changedMeshes;any=true;}
        if(previous.visible!=b.visible){previous.visible=b.visible;s.visibilityDirty=true;visibilityChangedNow=true;++s.counters.visibilityChanges;any=true;}
    }
    s.parameters=next;++s.counters.updates;
    for(auto& [id,cell]:s.uniforms){(void)id;if(!cell.active)continue;
        if(!cell.reactivated&&!(cell.dependencies&changed)&&!((cell.dependencies&worldDependency)&&s.worldChanged[cell.owner])&&
            !((cell.dependencies&overrideDependency)&&s.overridesChanged[cell.owner]))continue;
        encode(cell,s.batches[cell.owner],next);cell.reactivated=false;++s.counters.uniformEncodes;
        if(cell.bytes!=cell.scratch){cell.bytes.swap(cell.scratch);cell.dirty=true;++s.counters.changedUniforms;any=true;}
    }
    if(visibilityChangedNow)visibleDraws(s);
    return any;
}
void SourceScene::upload(SourceGraphics& target) {
#ifdef _WIN32
    auto& s=impl_->scene;target.clear();
    for(auto& [id,m]:s.meshes){target.setMesh(id,m.revision,m.stride,bytes(m.vertices),m.original->indices);m.dirty=false;}
    for(const auto& [id,t]:s.textures)target.setTexture(id,1,t.description);
    for(const auto& [id,p]:s.pipelines)target.setPipeline(id,p.description);
    for(auto& [id,u]:s.uniforms){target.setUniform(id,bytes(u.bytes));u.dirty=false;}
    target.setDraws(s.activeDraws);s.visibilityDirty=false;impl_->uploadedTarget=&target;
#else
    (void)target;throw std::runtime_error("SourceGraphics upload requires native Windows graphics");
#endif
}
void SourceScene::flush(SourceGraphics& target) {
#ifdef _WIN32
    require(impl_->uploadedTarget==&target,"SourceScene needs explicit upload to this graphics target");auto& s=impl_->scene;
    if(s.visibilityDirty)target.setDraws({}); // no draw executes while this owned-thread transaction updates changed ranges
    for(auto& [id,m]:s.meshes)if(m.dirty){target.setMesh(id,m.revision,m.stride,bytes(m.vertices),m.original->indices);m.dirty=false;}
    for(auto& [id,u]:s.uniforms)if(u.dirty){target.setUniform(id,bytes(u.bytes));u.dirty=false;}
    if(s.visibilityDirty){target.setDraws(s.activeDraws);s.visibilityDirty=false;}
#else
    (void)target;throw std::runtime_error("SourceGraphics flush requires native Windows graphics");
#endif
}
SourceSceneStats SourceScene::stats()const noexcept {
    const auto& s=impl_->scene;auto out=s.counters;out.batches=s.batches.size();out.draws=s.activeDraws.size();out.meshes=s.meshes.size();
    out.textures=s.textures.size();out.pipelines=s.pipelines.size();out.uniforms=s.uniforms.size();out.inactiveUniforms=s.inactiveUniforms;
    for(const auto& [id,m]:s.meshes){(void)id;out.retainedCPUBytes+=m.vertices.size();}
    for(const auto& [id,g]:s.originalGeometry){(void)id;out.retainedCPUBytes+=g->vertices.size()+g->indices.size()*4+g->colors.size()*16;}
    for(const auto& [id,t]:s.textures){(void)id;for(const auto& mip:t.levels)out.retainedCPUBytes+=mip.storage.size();}
    for(const auto& [id,p]:s.pipelines){(void)id;out.retainedCPUBytes+=p.vertexCode->size()+p.fragmentCode->size();}
    for(const auto& [id,u]:s.uniforms){(void)id;out.retainedCPUBytes+=u.bytes.size()+u.scratch.size();for(const auto& f:u.fields)out.retainedCPUBytes+=f.value.size()*4;}
    for(const auto& b:s.batches)for(const auto& v:b.uniformOverrides)out.retainedCPUBytes+=v.value.size()*4;
    for(const auto& p:impl_->prototypes){for(const auto& v:p.description.originalState.uniformOverrides)out.retainedCPUBytes+=v.value.size()*4;
        for(const auto& [id,c]:p.cells){(void)id;out.retainedCPUBytes+=c.bytes.size()+c.scratch.size();for(const auto& f:c.fields)out.retainedCPUBytes+=f.value.size()*4;}}
    for(const auto& d:impl_->descriptors)for(const auto& v:d.originalState.uniformOverrides)out.retainedCPUBytes+=v.value.size()*4;
    for(const auto& [id,slot]:impl_->slots)if(slot.mutableGeometry){(void)id;const auto& g=*slot.mutableGeometry;out.retainedCPUBytes+=g.vertices.size()+g.indices.size()*4+g.colors.size()*16;}
    return out;
}
std::vector<SourceGeometryPayload> SourceScene::geometryPayloads()const {
    std::vector<SourceGeometryPayload> out;out.reserve(impl_->scene.meshes.size());
    for(const auto& [id,m]:impl_->scene.meshes)out.push_back({id,m.stride,m.revision,bytes(m.vertices),m.original->indices});return out;
}
std::vector<SourceUniformPayload> SourceScene::uniformPayloads()const {
    std::vector<SourceUniformPayload> out;out.reserve(impl_->scene.uniforms.size());
    for(const auto& [id,u]:impl_->scene.uniforms)out.push_back({id,bytes(u.bytes),u.active});return out;
}
} // namespace endfield::native
