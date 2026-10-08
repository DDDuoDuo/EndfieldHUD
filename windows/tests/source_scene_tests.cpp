#include "native/source_scene.hpp"
#include <algorithm>
#include "core/shell_packet.hpp"
#include "core/data/data_store.hpp"
#include <atomic>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
std::atomic<std::size_t> allocations{};
void* operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using ehud::data::Json;
namespace packet=endfield::core::packet;
namespace gpu=endfield::native;
namespace fs=std::filesystem;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class Action>void rejects(Action action,const char* message){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,message);}
std::span<const std::uint8_t> bytes(const std::string& value){return {reinterpret_cast<const std::uint8_t*>(value.data()),value.size()};}
void write(const fs::path& path,const std::string& value){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out.write(value.data(),static_cast<std::streamsize>(value.size()));if(!out)throw std::runtime_error("Fixture write failed");}
std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void repairHash(std::string& file){const auto sum=packet::sha256(bytes(file).subspan(84));file.replace(20,64,sum);}
void little(std::string& file,std::size_t offset,std::uint32_t value){for(unsigned i=0;i<4;++i)file.at(offset+i)=static_cast<char>(value>>(8*i));}
struct CacheOffsets {
    std::size_t cursor=84,cameraWidth{},geometryStride{},firstIndex{},shaderCode{},attributeComponents{},pixelFormat{},mipWidth{},mipRow{},batchMatrix{},uniformOwner{},uniformDynamic{},uniformBytes{},drawIndexCount{},drawStage{},catalogCount{},catalogSource{},catalogGeometry{},catalogWidth{};
    const std::string& file;
    unsigned u32(){unsigned value{};for(unsigned i=0;i<4;++i)value|=unsigned(static_cast<unsigned char>(file.at(cursor++)))<<(8*i);return value;}
    void blob(){cursor+=u32();}
    explicit CacheOffsets(const std::string& input):file(input){
        for(unsigned i=0;i<4;++i)blob();cameraWidth=cursor;cursor+=8+4*128+3*8+4*4+4*4;const auto accent=static_cast<unsigned char>(file.at(cursor++));cursor+=accent?12:0;
        cursor+=4;auto count=u32();for(unsigned i=0;i<count;++i)blob();count=u32();
        for(unsigned i=0;i<count;++i){blob();if(!i)geometryStride=cursor;cursor+=8;blob();auto indices=u32();if(!i)firstIndex=cursor;cursor+=indices*4;cursor+=u32()*16;}
        count=u32();for(unsigned i=0;i<count;++i){blob();blob();}count=u32();
        for(unsigned i=0;i<count;++i){blob();const auto length=u32();if(!i)shaderCode=cursor;cursor+=length;}count=u32();
        for(unsigned i=0;i<count;++i){blob();cursor+=4;blob();blob();const auto attributes=u32();if(!i)attributeComponents=cursor+4;cursor+=attributes*12;cursor+=1+7*4+2+2*4+2*6*4;}count=u32();
        for(unsigned i=0;i<count;++i){blob();const auto size=u32();if(!i)pixelFormat=cursor;cursor+=size;cursor+=7*4;const auto mips=u32();
            for(unsigned j=0;j<mips;++j){if(!i&&!j){mipWidth=cursor;mipRow=cursor+8;}cursor+=12;blob();}}
        count=u32();for(unsigned i=0;i<count;++i){for(unsigned j=0;j<4;++j)blob();if(!i)batchMatrix=cursor;cursor+=128+16+1;const auto overrides=u32();
            for(unsigned j=0;j<overrides;++j){blob();cursor+=u32()*4;}}
        count=u32();for(unsigned i=0;i<count;++i){blob();if(!i)uniformOwner=cursor;cursor+=5;const auto size=u32();if(!i)uniformBytes=cursor;cursor+=size;const auto fields=u32();
            for(unsigned j=0;j<fields;++j){cursor+=4;blob();if(!i&&!j)uniformDynamic=cursor;cursor+=4+2;cursor+=u32()*4;const auto present=static_cast<unsigned char>(file.at(cursor++));cursor+=present?4:0;}}
        count=u32();for(unsigned i=0;i<count;++i){cursor+=4;blob();blob();if(!i)drawIndexCount=cursor+4;cursor+=12;const auto uniforms=u32();if(!i)drawStage=cursor;
            for(unsigned j=0;j<uniforms;++j){cursor+=8;blob();}const auto images=u32();for(unsigned j=0;j<images;++j){cursor+=12;blob();blob();}}
        const auto sources=u32();for(unsigned i=0;i<sources;++i)for(unsigned j=0;j<4;++j)blob();
        catalogCount=cursor;const auto extra=u32();
        if(extra){blob();catalogGeometry=cursor+4;blob();catalogSource=cursor;cursor+=4;catalogWidth=cursor;}
        else if(cursor!=file.size())throw std::runtime_error("Synthetic base cache layout scanner mismatch");
    }
};
void put(std::string& data,unsigned offset,float value){const auto bits=std::bit_cast<std::uint32_t>(value);for(unsigned i=0;i<4;++i)data[offset+i]=static_cast<char>(bits>>(8*i));}
Json matrix(){return Json::parse("[[1,0,0,0],[0,1,0,0],[0,0,1,0],[0,0,0,1]]");}
Json field(const char* name,unsigned offset,const char* dynamic,Json value=nullptr,bool color=false){return Json::Object{{"name",name},{"offset",int(offset)},{"dynamic",dynamic},{"value",value},{"isColor",color}};}
Json worldFields(){return Json::Array{field("ObjectToWorld",0,"world"),field("overlay",4,"none",Json::Array{42}),field("partial",8,"none",Json::Array{7})};}
Json cameraFields(){return Json::Array{field("VP",0,"viewProjection"),field("UITime",64,"uiTime"),field("Screen",80,"screen"),field("RenderPath",96,"renderPath"),field("FlipX",100,"flipX"),field("FlipY",104,"flipY"),field("Tint",112,"none",Json::Array{1,.8,.1,.6},true)};}
struct Fixture {
    fs::path root=fs::canonical(fs::temp_directory_path())/("EndfieldHUD-source-scene-fixture-"+ehud::data::makeUUID());
    fs::path shaders=root/"compiled";
    Fixture(){fs::create_directories(shaders);build();}
    ~Fixture(){std::error_code error;fs::remove_all(root,error);}
    Json blob(std::string path,const std::string& contents){write(root/path,contents);return Json::Object{{"file",path},{"bytes",std::int64_t(contents.size())},{"sha256",packet::sha256(bytes(contents))}};}
    void diversify(){
        auto manifest=Json::parse(read(root/"shell-packet.json")),frame=Json::parse(read(root/"frame.json"));
        auto texture=manifest["textures"].array()[0];texture["id"]="catalog-texture";
        manifest["textures"]=Json::Array{texture};auto meshes=manifest["meshes"].array();meshes[0]["id"]="catalog-mesh";manifest["meshes"]=meshes;
        auto materials=manifest["materials"].array();materials[0]["id"]="catalog-material";
        materials[0]["textures"]=Json::Object{{"Main","catalog-texture"}};manifest["materials"]=materials;
        frame["gpuCamera"]["timeSeconds"]=12;
        auto camera=read(root/"uniform/fragment.bin"),world=read(root/"uniform/vertex.bin");
        put(camera,64,.6f);put(camera,68,12);put(camera,72,24);put(world,48,17);
        auto batches=frame["batches"].array();for(auto& b:batches){b["mesh"]="catalog-mesh";b["material"]="catalog-material";auto columns=b["worldMatrix"].array();auto column=columns[3].array();column[0]=17;columns[3]=column;b["worldMatrix"]=columns;
            b["gpuVertexColor"]=Json::Array{.8,.7,.6,1};
            auto uniforms=b["uniforms"].array();uniforms[0]["payload"]=blob("uniform/vertex.bin",world);uniforms[1]["payload"]=blob("uniform/fragment.bin",camera);b["uniforms"]=uniforms;}frame["batches"]=batches;
        auto frameData=blob("frame.json",frame.encode());frameData["name"]="fixture";manifest["frames"]=Json::Array{frameData};
        write(root/"shell-packet.json",manifest.encode());
    }
    void build(){
        auto canonical=std::string(120,'\0'),native=std::string(240,'\0');
        for(unsigned vertex=0;vertex<3;++vertex){const std::array<float,10> v{float(vertex),0,0,1,0,1,1,1,1,1};
            for(unsigned x=0;x<10;++x)put(canonical,vertex*40+x*4,v[x]);
            for(unsigned x=0;x<4;++x)put(native,vertex*80+x*4,v[x]);for(unsigned x=0;x<2;++x)put(native,vertex*80+16+x*4,v[4+x]);for(unsigned x=0;x<4;++x)put(native,vertex*80+32+x*4,1);
        }
        auto vertices=blob("mesh/canonical.bin",canonical);vertices["stride"]=40;vertices["fields"]=Json::Object{{"position",0},{"uv",16},{"color",24}};
        std::string indices(12,'\0');indices[4]=1;indices[8]=2;auto index=blob("mesh/indices.bin",indices);index["format"]="uint32-le";
        auto original=blob("mesh/native.bin",native);original["stride"]=80;original["offsets"]=Json::Object{{"position",0},{"uv",16},{"color",32},{"normal",48},{"uv1",64}};
        auto mip=blob("texture/white.bin",std::string(4,static_cast<char>(255)));mip["level"]=0;mip["width"]=1;mip["height"]=1;mip["rowBytes"]=4;
        const auto descriptor=std::string("{\"shader\":\"synthetic owned fixture\"}");Json::Array assets{blob("shader/program.json",descriptor)};assets[0]["sourcePath"]="program.json";
        Json::Array programs;Json::Object stages;
        const auto stageData=[&](const std::string& name,const Json& fields,unsigned length,const std::string& buffer){
            const auto path="Shaders/test."+name+".metal",spvPath="Shaders/test."+name+".spv",metal="synthetic metal "+name,spv="synthetic spv "+name,code="DXBCsynthetic-"+name;
            auto metalBlob=blob("shader/"+path,metal);metalBlob["sourcePath"]=path;assets.push_back(metalBlob);auto spvBlob=blob("shader/"+spvPath,spv);spvBlob["sourcePath"]=spvPath;assets.push_back(spvBlob);
            Json::Array declarationFields;for(const auto& f:fields.array())declarationFields.emplace_back(Json::Object{{"name",f["name"]},{"offset",f["offset"]}});
            const auto declaration=Json::Object{{"name",buffer},{"size",int(length)},{"index",0},{"fields",declarationFields}};
            stages[name]=Json::Object{{"file",path},{"function","main0"},{"uniforms",Json::Array{declaration}}};
            const auto codeFile=name+".cso";write(shaders/codeFile,code);
            auto uniform=Json::Object{{"name",buffer},{"hlslName","buffer0"},{"active",true},{"sourceSize",int(length)},{"byteWidth",int(length)},{"slot",0},
                {"sourceFields",declarationFields},{"compiledMembers",Json::Array{Json::Object{{"name","member0"},{"offset",0},{"bytes",int(length)},{"used",true}}}}};
            programs.emplace_back(Json::Object{{"id","program.json:"+name},{"sourceDescriptor","program.json"},{"sourceDescriptorSHA256",packet::sha256(bytes(descriptor))},
                {"stage",name},{"profile",name=="vertex"?"vs_5_0":"ps_5_0"},{"entry","main"},{"metalFile",path},{"metalSHA256",packet::sha256(bytes(metal))},{"spirvSHA256",packet::sha256(bytes(spv))},
                {"bytecodeFile",codeFile},{"bytecodeBytes",std::int64_t(code.size())},{"bytecodeSHA256",packet::sha256(bytes(code))},{"uniforms",Json::Array{uniform}},{"textures",Json::Array{}},
                {"bindings",Json::Array{Json::Object{{"name","buffer0"},{"type",0},{"count",1},{"slot",0},{"dimension",0}}}}});
            if(name=="fragment"){
                programs.back()["textures"]=Json::Array{Json::Object{{"name","Main"},{"hlslTexture","texture0"},{"hlslSampler","sampler0"}}};
                auto bindings=programs.back()["bindings"].array();bindings.push_back(Json::Object{{"name","texture0"},{"type",2},{"count",1},{"slot",0},{"dimension",4}});
                bindings.push_back(Json::Object{{"name","sampler0"},{"type",3},{"count",1},{"slot",0},{"dimension",0}});
                programs.back()["bindings"]=std::move(bindings);
            }
        };
        stageData("vertex",worldFields(),64,"World");stageData("fragment",cameraFields(),128,"Camera");
        write(shaders/"compiled-shaders.json",Json(Json::Object{{"schemaVersion",1},{"programs",programs}}).encode());
        auto pass=Json::parse(R"({"id":"pass","shaderKey":"synthetic","shader":"fixture","shaderDescriptorFile":"program.json","vertexAttributes":[{"attribute":0,"format":31,"offset":0,"bufferIndex":30,"stride":80}],"textureBindings":[],"blend":{"enabled":true,"sourceRGB":1,"destinationRGB":5,"sourceAlpha":1,"destinationAlpha":5,"rgbOperation":0,"alphaOperation":0,"writeMask":15},"depthStencil":{"depthCompare":7,"depthWrite":false,"front":null,"back":null},"stencilReference":0,"cull":0})");pass["stages"]=stages;
        pass["textureBindings"]=Json::Array{Json::Object{{"name","Main"},{"stage","fragment"}}};
        std::string world(64,'\0');for(unsigned i=0;i<4;++i)put(world,i*20,1);put(world,4,42);put(world,8,3);put(world,12,4);
        std::string camera(128,'\0');for(unsigned i=0;i<4;++i)put(camera,i*20,1);put(camera,80,128);put(camera,84,80);put(camera,88,1.f/128);put(camera,92,1.f/80);put(camera,96,1);put(camera,112,1);put(camera,116,.8f);put(camera,120,.1f);put(camera,124,.6f);
        Json::Array nodes,batches;
        for(unsigned i=0;i<2;++i){const auto id="CAB:900719925474099"+std::to_string(3+i);
            nodes.emplace_back(Json::Object{{"id",id},{"parent",nullptr},{"name","fixture"},{"path","fixture"},{"active",true},{"inheritedAlpha",1},{"rect",nullptr},{"localMatrix",matrix()},{"worldMatrix",matrix()}});
            auto u=[&](const char* stage,unsigned length,const char* name,Json fields,const std::string& contents){return Json::Object{{"passID","pass"},{"shaderKey","synthetic"},{"stage",stage},{"index",0},{"bufferName",name},{"byteCount",int(length)},
                {"needsWorld",std::string(stage)=="vertex"},{"needsCamera",std::string(stage)=="fragment"},{"needsTime",std::string(stage)=="fragment"},{"fields",fields},{"payload",blob(std::string("uniform/")+stage+".bin",contents)}};};
            batches.emplace_back(Json::Object{{"sourceNodeID",id},{"sourceMesh","triangle"},{"mesh","mesh"},{"material","material"},{"worldMatrix",matrix()},{"color",Json::Array{1,1,1,1}},
                {"gpuVertexColor",Json::Array{1,1,1,1}},{"textureOverrides",Json::Object{}},{"uniformOverrides",Json::Object{{"partial",Json::Array{3,4}},{"unreflectedSourceProperty",Json::Array{1,2,3,4}}}},{"stencil",nullptr},{"colorWriteMask",nullptr},{"indexRange",nullptr},
                {"appliesDesktopAccent",true},{"uniforms",Json::Array{u("vertex",64,"World",worldFields(),world),u("fragment",128,"Camera",cameraFields(),camera)}}});
        }
        Json gpuCamera=Json::Object{{"viewProjection",matrix()},{"viewNoTranslationProjection",matrix()},{"projection",matrix()},{"inverseView",matrix()},
            {"worldSpacePosition",Json::Array{0,0,0}},{"uiProjectionParameters",Json::Array{-1,.3,200,.005}},{"timeSeconds",0},{"renderPathInjected",1},{"flipX",0},{"flipY",0},
            {"sceneColorMode","directLDR"},{"postprocess",Json::Array{}},{"sceneColorPixelFormat",81},{"drawablePixelFormat",81},{"depthStencilPixelFormat",260},
            {"clearColor",Json::Array{0,0,0,0}},{"clearDepth",1},{"clearStencil",0}};
        Json frame=Json::Object{{"viewport",Json::Array{128,80}},{"camera",Json::Object{{"canvasSize",Json::Array{128,80}},{"projection",matrix()},{"view",matrix()},{"worldRoot",matrix()}}},
            {"gpuCamera",gpuCamera},{"nodes",nodes},{"hits",Json::Array{}},{"batches",batches}};
        auto frameBlob=blob("frame.json",frame.encode());frameBlob["name"]="fixture";
        Json manifest=Json::Object{{"schemaVersion",1},{"desktopMode",true},{"coordinates",Json::Object{}},{"fixture",Json::Object{}},
            {"meshes",Json::Array{Json::Object{{"id","mesh"},{"sourceMesh","triangle"},{"vertexCount",3},{"indexCount",3},{"vertices",vertices},{"indices",index},{"originalVertexBuffer",original}}}},
            {"textures",Json::Array{Json::Object{{"id","__white"},{"width",1},{"height",1},{"pixelFormat","rgba8Unorm"},{"sRGB",false},
                {"sampler",Json::parse(R"({"minFilter":0,"magFilter":0,"mipFilter":0,"addressU":0,"addressV":0,"addressW":0,"maxAnisotropy":1,"lodMinClamp":0,"lodMaxClamp":3.4028234663852886e38,"normalizedCoordinates":true,"compareFunction":0})")},{"mips",Json::Array{mip}}}}},
            {"materials",Json::Array{Json::Object{{"id","material"},{"values",Json::Object{}},{"propertyTypes",Json::Object{}},{"textures",Json::Object{}},{"passes",Json::Array{pass}}}}},
            {"frames",Json::Array{frameBlob}},{"animation",blob("animation.json","{}")},{"shaderAssets",assets},{"modules",Json::Array{}}};
        write(root/"shell-packet.json",manifest.encode());
    }
};
std::vector<std::pair<std::string,std::string>> snapshot(const gpu::SourceScene& scene){std::vector<std::pair<std::string,std::string>> out;for(const auto& u:scene.uniformPayloads())out.emplace_back(u.id,std::string(reinterpret_cast<const char*>(u.bytes.data()),u.bytes.size()));return out;}
std::vector<std::pair<std::string,std::string>> activeSnapshot(const gpu::SourceScene& scene){std::vector<std::pair<std::string,std::string>> out;for(const auto& u:scene.uniformPayloads())if(u.active)out.emplace_back(u.id,std::string(reinterpret_cast<const char*>(u.bytes.data()),u.bytes.size()));return out;}
std::uint64_t geometryRevision(const gpu::SourceScene& scene,const char* id){for(const auto& g:scene.geometryPayloads())if(g.id==id)return g.revision;throw std::runtime_error("Fixture geometry missing");}
float scalar(const std::string& data,unsigned offset){float value{};std::memcpy(&value,data.data()+offset,4);return value;}
void run(){
    Fixture fixture;gpu::SourceScene scene(fixture.root,fixture.shaders/"compiled-shaders.json","fixture");
    check(scene.stats().batches==2&&scene.stats().draws==2&&scene.stats().uniforms==3,"Identical world buffers are isolated per batch; compatible camera buffer shared");
    const auto cache=fixture.root/"compiled-source.ehscene";scene.writeCompiled(cache);const auto file=read(cache);
    gpu::SourceScene cached(gpu::CompiledSourceScene{cache,packet::sha256(bytes(file))});
    check(snapshot(scene)==snapshot(cached)&&cached.stats().batches==scene.stats().batches&&cached.shaderKeys()==scene.shaderKeys()&&cached.stats().textures==1,"Compiled scene preserves all exact shader constants, identities, mips and original passes");
    check(cached.provenance().packetSHA256==scene.provenance().packetSHA256&&cached.provenance().frameSHA256==scene.provenance().frameSHA256&&cached.provenance().shaderManifestSHA256==scene.provenance().shaderManifestSHA256,"Source root/frame/shader provenance survives compilation");
    const auto copy=fixture.root/"second.ehscene";cached.writeCompiled(copy);check(file==read(copy),"Compiled output is deterministic after lossless load");
    rejects([&]{scene.writeCompiled(cache);},"Build compiler never overwrites existing artifacts");
    rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{cache,std::string(64,'0')});},"Shipping hash pin rejects a different complete artifact");
    const auto corrupt=fixture.root/"corrupt.ehscene";
    auto badFile=file;badFile.back()^=1;write(corrupt,badFile);rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},"Payload corruption is rejected before decoding");
    badFile=file;badFile[8]=4;write(corrupt,badFile);rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},"Unsupported compiled schema is rejected");
    badFile=file;badFile[12]^=1;write(corrupt,badFile);rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},"Inconsistent compiled payload length is rejected");
    badFile=file;badFile.push_back(0);write(corrupt,badFile);rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},"Trailing compiled data is rejected");
    badFile=file;badFile[84]=static_cast<char>(255);badFile[85]=static_cast<char>(255);badFile[86]=static_cast<char>(255);badFile[87]=static_cast<char>(255);repairHash(badFile);write(corrupt,badFile);
    rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},"Hash-valid oversized identity length is rejected without allocating it");
    const CacheOffsets offsets(file);
    const auto rejects32=[&](std::size_t offset,unsigned value,const char* message){auto altered=file;little(altered,offset,value);repairHash(altered);write(corrupt,altered);rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},message);};
    rejects32(offsets.cameraWidth,0,"Hash-valid zero viewport rejected");
    rejects32(offsets.geometryStride,257,"Hash-valid oversized geometry stride rejected");
    rejects32(offsets.firstIndex,3,"Hash-valid index outside vertex bounds rejected");
    rejects32(offsets.attributeComponents,5,"Hash-valid unsupported vertex format rejected");
    rejects32(offsets.mipWidth,0,"Hash-valid zero texture width rejected");
    rejects32(offsets.mipRow,5,"Hash-valid incorrect mip pitch rejected");
    rejects32(offsets.uniformOwner,2,"Hash-valid absent uniform owner rejected");
    rejects32(offsets.uniformDynamic,14,"Hash-valid unknown dynamic uniform plan rejected");
    rejects32(offsets.drawIndexCount,4,"Hash-valid incomplete triangle draw rejected");
    rejects32(offsets.drawStage,2,"Hash-valid unsupported GPU binding stage rejected");
    badFile=file;badFile.at(offsets.shaderCode)='X';repairHash(badFile);write(corrupt,badFile);rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},"Hash-valid invalid DXBC signature rejected");
    badFile=file;badFile.at(offsets.pixelFormat)='X';repairHash(badFile);write(corrupt,badFile);rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},"Hash-valid unsupported original texture format rejected");
    badFile=file;little(badFile,offsets.batchMatrix+4,0x7ff00000);repairHash(badFile);write(corrupt,badFile);rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},"Hash-valid nonfinite source world matrix rejected");
    badFile=file;badFile.at(offsets.uniformBytes)^=1;repairHash(badFile);write(corrupt,badFile);rejects([&]{gpu::SourceScene other(gpu::CompiledSourceScene{corrupt,{}});},"Hash-valid uniform bytes that disagree with ordered field plans rejected");
    auto states=std::vector<gpu::SourceBatchState>(scene.batches().begin(),scene.batches().end());auto p=scene.parameters();
    const auto initial=snapshot(scene);const auto before=allocations.load();const auto beforeStats=scene.stats();
    bool idle=false;for(unsigned i=0;i<100;++i)idle|=scene.update(states,p);const auto after=allocations.load();
    check(!idle&&after==before&&scene.stats().uniformEncodes==beforeStats.uniformEncodes,"100 idle updates allocate and encode nothing");
    states[0].world.values[12]=9;auto allocationBefore=allocations.load();const auto changed=scene.update(states,p);auto allocationAfter=allocations.load();
    check(changed&&allocationAfter==allocationBefore,"World update uses retained allocations");
    auto cachedStates=std::vector<gpu::SourceBatchState>(cached.batches().begin(),cached.batches().end());cachedStates[0].world.values[12]=9;
    const auto cacheBefore=allocations.load();check(cached.update(cachedStates,p),"Compiled scene keeps dynamic world uniforms");const auto cacheAfter=allocations.load();
    check(cacheAfter==cacheBefore&&snapshot(cached)==snapshot(scene),"Compiled world update matches source without new allocations");
    rejects([&]{cached.writeCompiled(fixture.root/"runtime-snapshot.ehscene");},"Build compiler rejects mutated runtime snapshots");
    auto world=snapshot(scene);unsigned changedBuffers{};for(std::size_t i=0;i<world.size();++i)if(world[i].second!=initial[i].second){++changedBuffers;check(scalar(world[i].second,48)==9&&scalar(world[i].second,4)==42&&scalar(world[i].second,8)==3&&scalar(world[i].second,12)==4,"World field updates preserve later static/partial overlapping writes");}
    check(changedBuffers==1,"World update leaves other batch and shared camera buffers unchanged");
    states[1].uniformOverrides[0].value={6,7};check(scene.update(states,p),"Typed override update changes its original cell");auto overrides=snapshot(scene);changedBuffers=0;
    for(std::size_t i=0;i<overrides.size();++i)if(overrides[i].second!=world[i].second){++changedBuffers;check(scalar(overrides[i].second,8)==6&&scalar(overrides[i].second,12)==7,"Typed partial override follows original field order");}check(changedBuffers==1,"Override update is confined to its own batch");
    p.timeSeconds=120;p.camera.viewProjection.values[12]=2;p.width=256;p.flipX=1;p.desktopAccentLinear=std::array<float,3>{.2f,.3f,.4f};
    check(scene.update(states,p),"Submitted camera/time/screen/flags/theme update changes shared cell");auto dynamic=snapshot(scene);changedBuffers=0;
    for(std::size_t i=0;i<dynamic.size();++i)if(dynamic[i].second!=overrides[i].second){++changedBuffers;const auto& d=dynamic[i].second;
        check(d.size()==128&&scalar(d,48)==2&&scalar(d,64)==6&&scalar(d,68)==120&&scalar(d,72)==240&&scalar(d,76)==0,"Exact original UITime and matrix field encoding");
        check(scalar(d,80)==256&&scalar(d,88)==1.f/256&&scalar(d,100)==1,"Viewport reciprocal and flip flag remain exact");
        check(scalar(d,112)==.2f&&scalar(d,116)==.3f&&scalar(d,120)==.4f&&scalar(d,124)==.6f,"Only declared yellow color maps to theme while alpha stays exact");}
    check(changedBuffers==1,"Shared camera update encodes one compatible cell");
    const auto stable=snapshot(scene);auto invalid=states;invalid[0].sourceNodeID="changed";rejects([&]{scene.update(invalid,p);},"Immutable identities cannot change implicitly");check(snapshot(scene)==stable,"Rejected identity update leaves all payloads unchanged");
    invalid=states;invalid[0].world.values[0]=std::numeric_limits<double>::infinity();rejects([&]{scene.update(invalid,p);},"Nonfinite world rejected before mutation");
    auto bad=p;bad.timeSeconds=std::numeric_limits<float>::max();rejects([&]{scene.update(states,bad);},"Time vector overflow rejected before mutation");
    invalid=states;invalid[0].uniformOverrides[0].value.push_back(1);rejects([&]{scene.update(invalid,p);},"Override widths cannot change implicitly");
    states[1].visible=false;check(scene.update(states,p)&&scene.stats().draws==1,"Visibility changes ordered original draw subset");
    states[1].visible=true;check(scene.update(states,p)&&scene.stats().draws==2,"Visibility can restore the original order");
    check(snapshot(scene)==stable,"Visibility update leaves all shader payloads unchanged");
    {
        gpu::SourceScene live(fixture.root,fixture.shaders/"compiled-shaders.json","fixture");const auto templates=live.templates();
        check(templates.size()==2&&templates[0].logicalMetadataComplete&&templates[0].passes[0].textures[0].name=="Main","Source templates expose exact material/field/texture property metadata");
        std::vector<gpu::SourceAssembledBatch> frame(2);
        for(unsigned i=0;i<2;++i){frame[i].stateID=i?"right":"left";frame[i].prototypeID=templates[i].id;frame[i].state=templates[i].originalState;
            frame[i].state.sourceNodeID="new source node "+std::to_string(i);frame[i].state.sourceMesh="new ui source "+std::to_string(i);}
        const std::array<std::array<float,4>,3> positions{{{{0,0,0,1}},{{1,0,0,1}},{{0,1,0,1}}}};
        const std::array<std::array<float,2>,3> uv{{{{0,0}},{{1,0}},{{0,1}}}};const std::array<std::uint32_t,3> triangles{0,1,2};
        frame[0].imageGeometry=gpu::SourceImageGeometryView{positions,uv,triangles};
        check(live.assembleFrame(frame,live.parameters())&&live.stats().draws==2&&live.stateIDs().size()==4,"Complete frame installs new stable identities from exact source templates");
        frame[0].imageGeometry.reset();const auto idleStart=allocations.load();const auto idleChanged=live.assembleFrame(frame,live.parameters());const auto idleEnd=allocations.load();
        check(!idleChanged&&idleStart==idleEnd,"Unchanged assembled frames allocate no CPU storage or rebuild GPU bindings");
        auto liveState=std::vector<gpu::SourceBatchState>(live.batches().begin(),live.batches().end());liveState[2].world.values[12]=5;
        const auto moveStart=allocations.load();const auto moved=live.update(liveState,live.parameters());const auto moveEnd=allocations.load();
        check(moved&&moveStart==moveEnd,"Assembled world motion uses the original retained field writers without allocation");
        auto beforeInvalid=snapshot(live);auto broken=frame;broken[1].state.material="unknown source material";
        rejects([&]{live.assembleFrame(broken,live.parameters());},"Complete frame rejects unknown material before committing any state");
        check(snapshot(live)==beforeInvalid,"Invalid full-frame submission preserves every original shader buffer");
        broken=frame;broken[1].stateID=broken[0].stateID;rejects([&]{live.assembleFrame(broken,live.parameters());},"Duplicate host state ID rejected");
        broken=frame;broken[1].textureOverrides={{"Main","not installed"}};rejects([&]{live.assembleFrame(broken,live.parameters());},"Uninstalled source texture rejected without fallback");
        broken=frame;broken[1].state.uniformOverrides.push_back({"invented",{1}});rejects([&]{live.assembleFrame(broken,live.parameters());},"Undeclared source uniform rejected");
        auto changedPositions=positions;changedPositions[1][0]=2;
        check(live.updateGeometry("left",gpu::SourceImageGeometryView{changedPositions,uv,triangles}),"Source image geometry changes without altering shader/texture resources");
        const auto geometryStart=allocations.load();const auto geometryIdle=live.updateGeometry("left",gpu::SourceImageGeometryView{changedPositions,uv,triangles});const auto geometryEnd=allocations.load();
        check(!geometryIdle&&geometryStart==geometryEnd,"Identical source geometry revisions reuse retained CPU capacity");
        changedPositions[1][0]=3;const auto geometryChangeStart=allocations.load();const auto geometryChanged=live.updateGeometry("left",gpu::SourceImageGeometryView{changedPositions,uv,triangles});const auto geometryChangeEnd=allocations.load();
        check(geometryChanged&&geometryChangeStart==geometryChangeEnd,"Same-sized source geometry updates retain CPU capacity");
        changedPositions[1][0]=std::numeric_limits<float>::infinity();rejects([&]{live.updateGeometry("left",gpu::SourceImageGeometryView{changedPositions,uv,triangles});},"Invalid source image attributes rejected before replacing retained geometry");
        std::swap(frame[0],frame[1]);check(live.assembleFrame(frame,live.parameters()),"Source draw order can change independently of stable state slots");
        check(live.assembleFrame({},live.parameters())&&live.stats().draws==0,"Empty assembled frame suppresses passes without deleting retained slots");
        check(live.assembleFrame(frame,live.parameters())&&live.stats().draws==2&&live.stateIDs().size()==4,"Returning source frame reuses existing identities and restores draw order");
        const auto revision=geometryRevision(live,"live:mesh:left");
        for(auto& b:frame)if(b.stateID=="left")b.prototypeID=templates[1].id;
        check(live.assembleFrame(frame,live.parameters())&&geometryRevision(live,"live:mesh:left")>revision,"Changing a retained mesh prototype monotonically advances GPU revision");
        auto resized=positions;resized[1][0]=4;const auto replacementRevision=geometryRevision(live,"live:mesh:left");
        check(live.updateGeometry("left",gpu::SourceImageGeometryView{resized,uv,triangles})&&geometryRevision(live,"live:mesh:left")>replacementRevision,"Viewport geometry cannot reuse the old GPU revision after prototype replacement");
        auto declared=frame;declared[0].state.uniformOverrides[1].value={5,6,7,8};
        check(live.assembleFrame(declared,live.parameters()),"Known unreflected source property accepts its exact authored width");
        declared[0].state.uniformOverrides[1].value.push_back(9);
        rejects([&]{live.assembleFrame(declared,live.parameters());},"Known unreflected source property cannot change authored width");
        rejects([&]{live.writeCompiled(fixture.root/"live-cache.ehscene");},"Build compiler refuses a live assembled snapshot");
    }
    {
        gpu::SourceScene retained(fixture.root,fixture.shaders/"compiled-shaders.json","fixture"),fresh(fixture.root,fixture.shaders/"compiled-shaders.json","fixture");
        std::vector<gpu::SourceAssembledBatch> entries;for(const auto& t:retained.templates()){gpu::SourceAssembledBatch b;b.stateID="reactivated:"+t.id;b.prototypeID=t.id;b.state=t.originalState;entries.push_back(std::move(b));}
        auto params=retained.parameters();retained.assembleFrame(entries,params);retained.assembleFrame({},params);
        params.width=256;params.height=160;params.timeSeconds=14;params.camera.viewProjection.values[12]=8;
        params.desktopAccentLinear=std::array<float,3>{.2f,.3f,.4f};
        const auto before=retained.stats().uniformEncodes;retained.assembleFrame({},params);
        check(retained.stats().uniformEncodes==before,"Inactive source cells avoid needless camera/time encodes");
        retained.assembleFrame(entries,params);fresh.assembleFrame(entries,params);
        check(activeSnapshot(retained)==activeSnapshot(fresh),"Reactivated camera/screen/time/theme constants refresh after a viewport change while hidden");
        const auto refreshed=retained.stats().uniformEncodes;const auto allocationsBefore=allocations.load();
        retained.assembleFrame(entries,params);const auto allocationsAfter=allocations.load();
        check(retained.stats().uniformEncodes==refreshed&&allocationsBefore==allocationsAfter,"Restored idle frame avoids repeated refreshes and allocations");
    }
    {
        Fixture additionalFixture;additionalFixture.diversify();
        gpu::SourceScene catalog(fixture.root,fixture.shaders/"compiled-shaders.json","fixture"),extra(additionalFixture.root,additionalFixture.shaders/"compiled-shaders.json","fixture");
        catalog.includeTemplates(extra,"extra-source");check(catalog.templates().size()==4&&catalog.stats().textures==2,"Explicit source catalog extends prototypes and installs distinct exact resources");
        check(catalog.catalogProvenance().size()==2&&catalog.catalogProvenance()[1]==extra.provenance(),"Every included frame retains its exact root/frame/compiler provenance");
        rejects([&]{catalog.includeTemplates(extra,"extra-source");},"Duplicate source catalog namespaces rejected");
        const auto catalogCache=fixture.root/"catalog.ehscene";catalog.writeCompiled(catalogCache);auto catalogBytes=read(catalogCache);
        gpu::SourceScene savedCatalog(gpu::CompiledSourceScene{catalogCache,packet::sha256(bytes(catalogBytes))});
        check(savedCatalog.templates().size()==4&&savedCatalog.catalogProvenance().size()==2&&savedCatalog.stats().textures==2,"Compiled catalog restores all typed prototypes, provenance and resources");
        const auto catalogCopy=fixture.root/"catalog-copy.ehscene";savedCatalog.writeCompiled(catalogCopy);
        check(catalogBytes==read(catalogCopy),"Catalog compilation is deterministic after a lossless load");
        gpu::SourceAssembledBatch entry;entry.stateID="catalog-entry";entry.prototypeID=catalog.templates()[2].id;entry.state=catalog.templates()[2].originalState;
        entry.state.sourceNodeID="new original node";entry.state.sourceMesh="new original ui";
        check(catalog.assembleFrame(std::span(&entry,1),catalog.parameters())&&catalog.stats().draws==1,"Namespaced source prototype can instantiate exact known passes");
        check(savedCatalog.assembleFrame(std::span(&entry,1),savedCatalog.parameters())&&snapshot(savedCatalog)==snapshot(catalog),"Loaded catalog new-node field writers match the source union exactly");
        auto next=std::vector<gpu::SourceBatchState>(catalog.batches().begin(),catalog.batches().end());next.back().world.values[12]=23;
        auto savedNext=std::vector<gpu::SourceBatchState>(savedCatalog.batches().begin(),savedCatalog.batches().end());savedNext.back().world.values[12]=23;
        auto params=catalog.parameters();params.timeSeconds=16;params.camera.viewProjection.values[12]=4;
        check(catalog.update(next,params)&&savedCatalog.update(savedNext,params)&&snapshot(savedCatalog)==snapshot(catalog),"Loaded catalog world/camera/time changes preserve source exact byte plans");
        const std::array<std::array<float,4>,3> positions{{{{0,0,0,1}},{{5,0,0,1}},{{0,3,0,1}}}};
        const std::array<std::array<float,2>,3> uv{{{{0,0}},{{1,0}},{{0,1}}}};const std::array<std::uint32_t,3> triangles{0,1,2};
        check(catalog.updateGeometry("catalog-entry",gpu::SourceImageGeometryView{positions,uv,triangles})&&savedCatalog.updateGeometry("catalog-entry",gpu::SourceImageGeometryView{positions,uv,triangles}),"Compiled catalog can accept dynamic exact image geometry");
        const auto originalGeometry=catalog.geometryPayloads(),cachedGeometry=savedCatalog.geometryPayloads();
        const auto original=std::find_if(originalGeometry.begin(),originalGeometry.end(),[](const auto& g){return g.id=="live:mesh:catalog-entry";});
        const auto restored=std::find_if(cachedGeometry.begin(),cachedGeometry.end(),[](const auto& g){return g.id=="live:mesh:catalog-entry";});
        check(original!=originalGeometry.end()&&restored!=cachedGeometry.end()&&original->vertices.size()==restored->vertices.size()&&std::equal(original->vertices.begin(),original->vertices.end(),restored->vertices.begin()),"Dynamic geometry after catalog load retains every source-native vertex byte");
        rejects([&]{catalog.includeTemplates(extra,"late");},"Catalog installation is forbidden during live frame submission");
        const auto brokenCatalog=fixture.root/"broken-catalog.ehscene";auto truncated=catalogBytes;truncated.pop_back();
        write(brokenCatalog,truncated);rejects([&]{gpu::SourceScene bad(gpu::CompiledSourceScene{brokenCatalog,{}});},"Truncated catalog rejects before allocating prototypes");
        auto corruptCatalog=catalogBytes;const auto geometryPosition=corruptCatalog.rfind("catalog-mesh");check(geometryPosition!=std::string::npos,"Fixture includes catalog geometry reference");
        corruptCatalog[geometryPosition]='X';repairHash(corruptCatalog);write(brokenCatalog,corruptCatalog);
        rejects([&]{gpu::SourceScene bad(gpu::CompiledSourceScene{brokenCatalog,{}});},"Hash-valid unknown catalog geometry reference rejects before assembly");
        const CacheOffsets catalogOffsets(catalogBytes);
        const auto rejectCatalog32=[&](std::size_t offset,unsigned value,const char* message){auto altered=catalogBytes;little(altered,offset,value);repairHash(altered);write(brokenCatalog,altered);rejects([&]{gpu::SourceScene bad(gpu::CompiledSourceScene{brokenCatalog,{}});},message);};
        rejectCatalog32(catalogOffsets.catalogCount,4096,"Hash-valid oversized catalog prototype count rejects before reserving memory");
        rejectCatalog32(catalogOffsets.catalogSource,2,"Hash-valid missing catalog provenance reference rejects");
        rejectCatalog32(catalogOffsets.catalogWidth,0,"Hash-valid invalid catalog original camera context rejects");
        // The independent catalog no longer needs any original fixture files.
        fs::remove_all(additionalFixture.root);gpu::SourceScene portableCatalog(gpu::CompiledSourceScene{catalogCache,{}});
        check(portableCatalog.assembleFrame(std::span(&entry,1),portableCatalog.parameters()),"Compiled catalog reads no source paths, raw packet or shader JSON");
    }
    // A future load cannot accept bytecode corruption, even though updates
    // deliberately never reopen files after initial validation.
    write(fixture.shaders/"vertex.cso","DXBCchanged");rejects([&]{gpu::SourceScene other(fixture.root,fixture.shaders/"compiled-shaders.json","fixture");},"Changed compiled bytecode rejected");
    const auto encodes=scene.stats().uniformEncodes;check(!scene.update(states,p)&&scene.stats().uniformEncodes==encodes,"Existing scene uses retained state after source artifacts change");
    write(fixture.root/"shell-packet.json","removed source fixture");fs::remove_all(fixture.shaders);fs::remove(fixture.root/"frame.json");
    gpu::SourceScene independent(gpu::CompiledSourceScene{cache,{}});check(snapshot(independent)==initial,"Compiled load reads no original package, shader paths or JSON");
}
}
int main(){try{run();std::cout<<checks<<" isolated source scene checks passed; no GPU/runtime OS APIs called\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
