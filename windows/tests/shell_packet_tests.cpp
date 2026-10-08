#include "core/shell_packet.hpp"
#include "core/data/data_store.hpp"
#include <bit>
#include <fstream>
#include <iostream>
#include <functional>
#include <limits>

using namespace endfield::core::packet;
namespace {
int checks{};
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void rejects(ErrorCode code,const std::function<void()>& operation,const char* label){try{operation();}catch(const Error& error){check(error.code()==code,label);return;}throw std::runtime_error(label);}
std::span<const std::uint8_t> bytes(std::string_view value){return {reinterpret_cast<const std::uint8_t*>(value.data()),value.size()};}
void write(const std::filesystem::path& path,std::string_view value){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(value.data(),static_cast<std::streamsize>(value.size()));if(!out)throw std::runtime_error("Synthetic fixture write failed");}
Json matrix(){return Json::parse("[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]");}
struct Fixture {
    std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-packet-fixture-"+ehud::data::makeUUID());
    Json manifest,frame;
    std::string vertices,indices;
    Fixture(){
        std::filesystem::create_directories(root);
        const auto append=[&](std::string& output,std::uint32_t bits){for(unsigned i=0;i<4;++i)output.push_back(static_cast<char>(bits>>(i*8)));};
        for(unsigned i=0;i<3;++i)for(const float value:{float(i),0.f,0.f,1.f,0.f,1.f,1.f,0.5f,0.25f,1.f})append(vertices,std::bit_cast<std::uint32_t>(value));
        for(const auto index:{0u,1u,2u})append(indices,index);
        frame=Json::parse(R"({"viewport":[1280,800],"camera":{"canvasSize":[2400,1500]},"nodes":[{"id":"CAB:-9007199254740993","parent":null,"name":"WatchPanel_PC","path":"WatchPanel_PC","active":true,"inheritedAlpha":1,"rect":{"origin":[-20,-20],"size":[40,40]}}],"hits":[{"buttonID":"CAB:-9007199254740993","graphicID":"CAB:-9007199254740993","center":[100,100],"projectedCorners":[[0,0],[100,0],[100,100],[0,100]],"rect":{"origin":[-20,-20],"size":[40,40]},"masks":[]}],"batches":[{"sourceNodeID":"CAB:-9007199254740993","mesh":"mesh-1","sourceMesh":"SourceTriangle","material":"material-1","color":[1,1,1,0.88],"indexRange":[0,3],"colorWriteMask":null,"appliesDesktopAccent":true,"textureOverrides":{},"uniformOverrides":{"_WatchAngle":[1]},"unknownBatchField":"preserved"}],"gpuCamera":{"time":1.25,"hdrMode":"sourceRGBHDR"},"nativeLayers":{"text":[{"id":"native-label-1","caption":"RAM"}]}})");
        for(const auto* field:{"projection","view","worldRoot"})frame["camera"][field]=matrix();
        frame["gpuCamera"]["viewProjection"]=matrix();frame["gpuCamera"]["viewNoTranslationProjection"]=matrix();
    }
    ~Fixture(){std::error_code error;std::filesystem::remove_all(root,error);}
    Json blob(std::string file,std::string_view contents){write(root/file,contents);return Json::Object{{"file",file},{"sha256",sha256(bytes(contents))},{"bytes",Json(static_cast<std::int64_t>(contents.size()))}};}
    void finish(){
        // Json exposes immutable arrays; stage array edits by making owned copies.
        auto nodes=frame["nodes"].array();nodes[0]["localMatrix"]=matrix();nodes[0]["worldMatrix"]=matrix();frame["nodes"]=nodes;
        auto hits=frame["hits"].array();hits[0]["worldMatrix"]=matrix();frame["hits"]=hits;
        auto batches=frame["batches"].array();batches[0]["worldMatrix"]=matrix();frame["batches"]=batches;
        auto vertex=blob("mesh/vertices.bin",vertices);vertex["stride"]=40;vertex["fields"]=Json::Object{{"position",0},{"uv",16},{"color",24}};
        auto index=blob("mesh/indices.bin",indices);index["format"]="uint32-le";
        auto original=blob("mesh/original.bin",std::string(240,'\0'));original["stride"]=80;original["offsets"]=Json::Object{{"position",0},{"uv",16},{"color",32},{"normal",48},{"uv1",64}};
        auto mip=blob("texture/pixel.bin",std::string("\x40\x80\xC0\xFF",4));mip["level"]=0;mip["width"]=1;mip["height"]=1;mip["rowBytes"]=4;
        auto sampler=Json::parse(R"({"minFilter":1,"magFilter":1,"mipFilter":0,"addressU":0,"addressV":0,"addressW":0,"maxAnisotropy":1,"lodMinClamp":0,"lodMaxClamp":3.4028234663852886e38,"normalizedCoordinates":true,"compareFunction":0})");
        auto pass=Json::parse(R"({"id":"pass-1","shaderKey":"Game.UiMixed","shader":{"source":"unmodified"},"stages":[{"name":"fragment"}],"textureBindings":[{"name":"_MainTex","index":0}],"blend":{"enabled":true,"sourceRGB":4,"destinationRGB":5,"sourceAlpha":1,"destinationAlpha":5,"rgbOperation":0,"alphaOperation":0,"writeMask":15},"depthStencil":{"depthCompare":7,"stencilSource":"preserved"},"cull":0})");
        auto uniform=Json::parse(R"({"passID":"pass-1","shaderKey":"Game.UiMixed","stage":"fragment","index":0,"bufferName":"$Globals","byteCount":16,"needsWorld":false,"needsCamera":false,"needsTime":false,"fields":[{"name":"_MainColor","offset":0,"value":[1,1,1,1],"isColor":true,"dynamic":"none"}]})");
        uniform["payload"]=blob("uniform/color.bin",std::string(16,'\0'));batches=frame["batches"].array();batches[0]["uniforms"]=Json::Array{uniform};frame["batches"]=batches;
        auto frameBlob=blob("frame/top.json",frame.encode(Package::maximumJSONBytes));frameBlob["name"]="top";
        const auto animation=Json::parse(R"({"library":{"clips":[{"id":"CAB:9223372036854775807","sample_rate":30,"wrap_mode":2,"last_key_time":13.683333,"curves":[{"raw":{"slope":"Infinity"}}]}]},"scene":{},"runtimeRoot":{},"controllerTransitions":{},"playback":{"finiteEase":"OutQuad"},"limitations":["source state retained"]})");
        auto shader=blob("shader/source.metal","source shader bytes");shader["sourcePath"]="ui/source.metal";
        manifest=Json::Object{{"schemaVersion",1},{"desktopMode",true},{"coordinates",Json::Object{{"matrix","column-major"}}},{"fixture",Json::Object{{"viewport","1280x800"}}},
            {"meshes",Json::Array{Json::Object{{"id","mesh-1"},{"sourceMesh","SourceTriangle"},{"vertexCount",3},{"indexCount",3},{"vertices",vertex},{"indices",index},{"originalVertexBuffer",original}}}},
            {"textures",Json::Array{Json::Object{{"id","texture-1"},{"width",1},{"height",1},{"pixelFormat","rgba8Unorm_srgb"},{"sRGB",true},{"sampler",sampler},{"mips",Json::Array{mip}}}}},
            {"materials",Json::Array{Json::Object{{"id","material-1"},{"values",Json::Object{{"_MainColor",Json::Array{1,1,1,1}}}},{"propertyTypes",Json::Object{}},{"textures",Json::Object{{"_MainTex","texture-1"}}},{"passes",Json::Array{pass}}}}},
            {"frames",Json::Array{frameBlob}},{"animation",blob("animation.json",animation.encode())},{"shaderAssets",Json::Array{shader}},{"modules",Json::Array{Json::Object{{"sourceDescriptor","preserved"}}}}};save();
    }
    void save(){write(root/"shell-packet.json",manifest.encode(Package::maximumJSONBytes));}
    void changeFrame(){auto frames=manifest["frames"].array();frames[0]=blob("frame/top.json",frame.encode(Package::maximumJSONBytes));frames[0]["name"]="top";manifest["frames"]=frames;save();}
};
void hashes(){
    check(sha256({})=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty vector");
    check(sha256(bytes("abc"))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc vector");
    check(sha256(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","SHA256 two-block padding vector");
    check(sha256(bytes(std::string(1'000'000,'a')))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA256 million-a vector");
}
void normal(){
    Fixture fixture;fixture.finish();Package package(fixture.root);
    check(package.meshes().size()==1&&package.materials().size()==1&&package.textures().size()==1,"package descriptors indexed");
    const auto mesh=package.loadMesh("mesh-1");check(mesh.vertices.size()==3&&mesh.indices==std::vector<std::uint32_t>{0,1,2},"mesh float32-LE and indices loaded");
    check(mesh.vertices[2].position[0]==2&&mesh.vertices[0].position[3]==1&&mesh.vertices[0].color[1]==0.5f,"native position W and original color retained");
    check(package.loadOriginalVertexBuffer("mesh-1").storage.size()==240,"original native buffer retained verbatim");
    check(package.loadTextureMip("texture-1",0).storage==std::string("\x40\x80\xC0\xFF",4),"exact texture bytes and sRGB format retained");
    check(package.requiredPixelFormats()==std::vector<std::string>{"rgba8Unorm_srgb"}&&package.requiredShaderKeys()==std::vector<std::string>{"Game.UiMixed"},"renderer required modes explicit");
    const auto frame=package.loadFrame("top");check(frame.viewport.x==1280&&frame.canvasSize.y==1500&&frame.view.values[15]==1,"viewport and column-major camera parsed");
    check(frame.metadata["nodes"].array()[0]["id"].string()=="CAB:-9007199254740993","large source IDs are strings without rounding");
    check(frame.metadata["batches"].array()[0]["unknownBatchField"].string()=="preserved"&&frame.metadata["nativeLayers"]["text"].array()[0]["caption"].string()=="RAM","all native and unknown frame fields retained");
    check(frame.uniforms.size()==1&&frame.uniforms[0][0].metadata["fields"].array()[0]["isColor"].boolean()&&package.loadUniformPayload(frame.uniforms[0][0]).storage.size()==16,"uniform bytes and source field plans retained");
    check(package.loadAnimation()["library"]["clips"].array()[0]["curves"].array()[0]["raw"]["slope"].string()=="Infinity","source nonfinite slope sentinels retained as strings");
    check(package.loadShaderAsset("shader/source.metal").storage=="source shader bytes","shader assets lazy hash verified");
    rejects(ErrorCode::missing,[&]{package.loadMesh("absent");},"missing mesh error");rejects(ErrorCode::missing,[&]{package.loadTextureMip("texture-1",1);},"missing mip error");
    write(fixture.root/"texture/pixel.bin","bad!");rejects(ErrorCode::integrity,[&]{package.loadTextureMip("texture-1",0);},"modified texture hash rejected");
    std::filesystem::remove(fixture.root/"texture/pixel.bin");Package lazy(fixture.root);check(lazy.loadMesh("mesh-1").vertices.size()==3,"metadata/geometry loading does not eagerly read textures");
    rejects(ErrorCode::missing,[&]{lazy.loadTextureMip("texture-1",0);},"missing texture discovered only on explicit load");
}
void malformed(){
    Fixture fixture;fixture.finish();auto original=fixture.manifest;
    fixture.manifest["schemaVersion"]=2;fixture.save();rejects(ErrorCode::unsupportedVersion,[&]{Package p(fixture.root);},"future schema rejected");
    fixture.manifest=original;auto meshes=fixture.manifest["meshes"].array();meshes[0]["id"]=9007199254740991.0;fixture.manifest["meshes"]=meshes;fixture.save();rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"numeric source ID never coerced to string");
    for(const auto path:{"../outside.bin","/outside.bin","C:/outside.bin","mesh\\vertices.bin","mesh//vertices.bin","mesh/NUL.bin","mesh/a. ","mesh/./vertices.bin"}){
        fixture.manifest=original;auto values=fixture.manifest["meshes"].array();values[0]["vertices"]["file"]=path;fixture.manifest["meshes"]=values;fixture.save();rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"unsafe package path rejected");
    }
    fixture.manifest=original;meshes=fixture.manifest["meshes"].array();meshes[0]["vertices"]["stride"]=48;fixture.manifest["meshes"]=meshes;fixture.save();rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"unsupported canonical vertex layout rejected");
    fixture.manifest=original;auto textures=fixture.manifest["textures"].array();textures[0]["pixelFormat"]="genericRGBA";fixture.manifest["textures"]=textures;fixture.save();rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"unknown GPU format not silently converted");
    fixture.manifest=original;textures=fixture.manifest["textures"].array();textures[0]["sRGB"]=false;fixture.manifest["textures"]=textures;fixture.save();rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"color-space mismatch rejected");
    fixture.manifest=original;textures=fixture.manifest["textures"].array();auto mips=textures[0]["mips"].array();mips[0]["rowBytes"]=8;textures[0]["mips"]=mips;fixture.manifest["textures"]=textures;fixture.save();rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"invalid mip layout rejected");
    fixture.manifest=original;auto frames=fixture.manifest["frames"].array();frames[0]["bytes"]=Json(static_cast<std::int64_t>(Package::maximumJSONBytes+1));fixture.manifest["frames"]=frames;fixture.save();rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"JSON blob bounded before allocation");
    fixture.manifest=original;fixture.manifest["animation"]["bytes"]=std::int64_t(Package::maximumJSONBytes+1);fixture.save();
    {Package largeAnimationMetadata(fixture.root);check(!largeAnimationMetadata.frames().empty(),"Only mounted-animation metadata may exceed the ordinary JSON limit; bytes stay lazily loaded");}
    fixture.manifest["animation"]["bytes"]=std::int64_t(Package::maximumAnimationJSONBytes+1);fixture.save();
    rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"Mounted animation remains bounded before allocation");
    fixture.manifest=original;meshes=fixture.manifest["meshes"].array();meshes[0]["vertices"]["sha256"]="not a digest";fixture.manifest["meshes"]=meshes;fixture.save();rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"invalid digest syntax rejected");
    fixture.manifest=original;auto vertices=fixture.vertices;vertices[0]=0;vertices[1]=0;vertices[2]=static_cast<char>(0xc0);vertices[3]=static_cast<char>(0x7f);meshes=fixture.manifest["meshes"].array();auto descriptor=fixture.blob("mesh/vertices.bin",vertices);descriptor["stride"]=40;descriptor["fields"]=Json::Object{{"position",0},{"uv",16},{"color",24}};meshes[0]["vertices"]=descriptor;fixture.manifest["meshes"]=meshes;fixture.save();Package nan(fixture.root);rejects(ErrorCode::invalid,[&]{nan.loadMesh("mesh-1");},"nonfinite geometry rejected even with valid digest");
    fixture.manifest=original;write(fixture.root/"mesh/vertices.bin",fixture.vertices);auto badIndices=fixture.indices;badIndices[0]=3;meshes=fixture.manifest["meshes"].array();descriptor=fixture.blob("mesh/indices.bin",badIndices);descriptor["format"]="uint32-le";meshes[0]["indices"]=descriptor;fixture.manifest["meshes"]=meshes;fixture.save();Package index(fixture.root);rejects(ErrorCode::invalid,[&]{index.loadMesh("mesh-1");},"out-of-range geometry index rejected");
    fixture.manifest=original;write(fixture.root/"mesh/indices.bin",fixture.indices);auto nodes=fixture.frame["nodes"].array();nodes[0]["parent"]=nodes[0]["id"];fixture.frame["nodes"]=nodes;fixture.changeFrame();Package cyclic(fixture.root);rejects(ErrorCode::invalid,[&]{cyclic.loadFrame("top");},"node cycle rejected without recursive stack growth");
    nodes[0]["parent"]=nullptr;fixture.frame["nodes"]=nodes;auto batches=fixture.frame["batches"].array();batches[0]["mesh"]="missing";fixture.frame["batches"]=batches;fixture.changeFrame();Package missing(fixture.root);rejects(ErrorCode::invalid,[&]{missing.loadFrame("top");},"draw reference must resolve");
    write(fixture.root/"shell-packet.json","{\"schemaVersion\":1,\"schemaVersion\":1}");rejects(ErrorCode::invalid,[&]{Package p(fixture.root);},"duplicate JSON keys rejected");
    rejects(ErrorCode::invalid,[&]{Package p(std::filesystem::path("relative"));},"injected root must be absolute");
}
}
int main(){try{hashes();normal();malformed();std::cout<<checks<<" synthetic shell packet checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
