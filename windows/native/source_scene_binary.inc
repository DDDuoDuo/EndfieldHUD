// Private typed SourceScene codec. Included in source_scene.cpp's anonymous
// namespace so the on-disk format cannot bypass the retained scene contracts.
// Layout v1/v2/v3/v4: EHSRC001 + u32 version + u64 payload bytes +64 ASCII SHA-256;
// payload consists of bounded little-endian fields, with no paths/JSON. The
// enclosing shipping manifest may pin the complete artifact for authenticity.
constexpr std::size_t compiledHeaderBytes=84;
constexpr std::size_t compiledMemoryBudget=256*1024*1024;
bool validSHA(std::string_view value) {
    return value.size()==64&&value.find_first_not_of("0123456789abcdef")==std::string_view::npos;
}
bool validIdentity(std::string_view value) {
    return !value.empty()&&value.size()<=4096&&Json::validUtf8(value)&&
        std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32||c==127;});
}
class BinaryWriter {
public:
    std::string data;
    void raw(std::string_view value) {
        require(value.size()<=gpu::SourceScene::maximumCompiledBytes-data.size(),"Compiled source payload exceeds bounds");data.append(value);
    }
    void u32(std::uint32_t value){for(unsigned i=0;i<4;++i){const char c=static_cast<char>(value>>(i*8));raw(std::string_view(&c,1));}}
    void u64(std::uint64_t value){for(unsigned i=0;i<8;++i){const char c=static_cast<char>(value>>(i*8));raw(std::string_view(&c,1));}}
    void flag(bool value){const char c=value?1:0;raw(std::string_view(&c,1));}
    void f32(float value){require(std::isfinite(value),"Nonfinite compiled source scalar");u32(std::bit_cast<std::uint32_t>(value));}
    void f64(double value){require(std::isfinite(value),"Nonfinite compiled source scalar");u64(std::bit_cast<std::uint64_t>(value));}
    void blob(std::string_view value){require(value.size()<=std::numeric_limits<std::uint32_t>::max(),"Unbounded compiled source blob");u32(static_cast<std::uint32_t>(value.size()));raw(value);}
    void identity(std::string_view value){require(validIdentity(value),"Invalid compiled source identity");blob(value);}
    void count(std::size_t value,std::size_t maximum){require(value<=maximum,"Too many compiled source records");u32(static_cast<std::uint32_t>(value));}
    void matrix(const endfield::core::Matrix4& value){require(finiteMatrix(value),"Invalid compiled matrix");for(auto x:value.values)f64(x);}
};
class BinaryReader {
public:
    explicit BinaryReader(std::string_view value):data_(value){}
    std::size_t remaining()const{return data_.size()-offset_;}
    std::string_view raw(std::size_t size){require(size<=remaining(),"Truncated compiled source data");auto result=data_.substr(offset_,size);offset_+=size;return result;}
    std::uint32_t u32(){const auto value=raw(4);std::uint32_t out{};for(unsigned i=0;i<4;++i)out|=std::uint32_t(static_cast<unsigned char>(value[i]))<<(8*i);return out;}
    std::uint64_t u64(){const auto value=raw(8);std::uint64_t out{};for(unsigned i=0;i<8;++i)out|=std::uint64_t(static_cast<unsigned char>(value[i]))<<(8*i);return out;}
    bool flag(){const auto value=static_cast<unsigned char>(raw(1)[0]);require(value<=1,"Invalid compiled boolean");return value!=0;}
    float f32(){const auto value=std::bit_cast<float>(u32());require(std::isfinite(value),"Nonfinite compiled float");return value;}
    double f64(){const auto value=std::bit_cast<double>(u64());require(std::isfinite(value),"Nonfinite compiled double");return value;}
    unsigned bounded(unsigned maximum){const auto value=u32();require(value<=maximum,"Compiled integer/count exceeds bounds");return value;}
    unsigned count(unsigned maximum,std::size_t minimumRecordBytes=1){const auto value=bounded(maximum);require(value<=remaining()/minimumRecordBytes,"Truncated compiled record array");return value;}
    std::string blob(std::size_t maximum){const auto length=u32();require(length<=maximum,"Compiled blob exceeds bounds");return std::string(raw(length));}
    std::string identity(){auto value=blob(4096);require(validIdentity(value),"Invalid compiled source identity");consumeBudget(value.size());return value;}
    endfield::core::Matrix4 matrix(){endfield::core::Matrix4 out;for(auto& x:out.values)x=f64();require(finiteMatrix(out),"Compiled matrix overflows float32");return out;}
    void consumeBudget(std::size_t bytes){require(bytes<=compiledMemoryBudget-budget_,"Compiled decoded memory exceeds bounds");budget_+=bytes;}
private:
    std::string_view data_;std::size_t offset_{},budget_{};
};
void saveParameters(BinaryWriter& w,const gpu::SourceFrameParameters& p) {
    validate(p);w.u32(p.width);w.u32(p.height);w.matrix(p.camera.projection);w.matrix(p.camera.viewProjection);
    w.matrix(p.camera.viewNoTranslationProjection);w.matrix(p.camera.inverseView);
    for(auto x:p.camera.worldPosition)w.f64(x);for(auto x:p.camera.uiProjectionParameters)w.f32(x);
    w.f32(p.timeSeconds);w.f32(p.renderPathInjected);w.f32(p.flipX);w.f32(p.flipY);
    w.flag(p.desktopAccentLinear.has_value());if(p.desktopAccentLinear)for(auto x:*p.desktopAccentLinear)w.f32(x);
}
gpu::SourceFrameParameters loadParameters(BinaryReader& r) {
    gpu::SourceFrameParameters p;p.width=r.bounded(4096);p.height=r.bounded(4096);
    p.camera.projection=r.matrix();p.camera.viewProjection=r.matrix();p.camera.viewNoTranslationProjection=r.matrix();p.camera.inverseView=r.matrix();
    for(auto& x:p.camera.worldPosition)x=r.f64();for(auto& x:p.camera.uiProjectionParameters)x=r.f32();
    p.timeSeconds=r.f32();p.renderPathInjected=r.f32();p.flipX=r.f32();p.flipY=r.f32();
    if(r.flag()){std::array<float,3> color;for(auto& x:color)x=r.f32();p.desktopAccentLinear=color;}validate(p);return p;
}
void saveFace(BinaryWriter& w,const gpu::SourceStencilFace& f){w.u32(f.compare);w.u32(f.fail);w.u32(f.depthFail);w.u32(f.pass);w.u32(f.readMask);w.u32(f.writeMask);}
gpu::SourceStencilFace loadFace(BinaryReader& r){return {r.bounded(7),r.bounded(7),r.bounded(7),r.bounded(7),r.bounded(255),r.bounded(255)};}
void savePipeline(BinaryWriter& w,const PreparedPipeline& p,const std::map<std::string,std::string,std::less<>>& programs) {
    w.u32(p.stride);w.identity(programs.at(*p.vertexCode));w.identity(programs.at(*p.fragmentCode));const auto& d=p.description;
    w.count(d.attributes.size(),16);for(const auto& a:d.attributes){w.u32(a.location);w.u32(a.components);w.u32(a.byteOffset);}
    w.flag(d.blendEnabled);w.u32(d.sourceRGB);w.u32(d.destinationRGB);w.u32(d.sourceAlpha);w.u32(d.destinationAlpha);
    w.u32(d.rgbOperation);w.u32(d.alphaOperation);w.u32(d.writeMask);w.flag(d.depthWrite);w.flag(d.stencilEnabled);
    w.u32(d.depthCompare);w.u32(d.cull);saveFace(w,d.front);saveFace(w,d.back);
}
PreparedPipeline loadPipeline(BinaryReader& r,const std::map<std::string,std::shared_ptr<const std::string>,std::less<>>& programs) {
    PreparedPipeline p;p.stride=r.bounded(256);require(p.stride>=8,"Invalid compiled vertex stride");
    const auto vertex=r.identity(),fragment=r.identity();require(programs.contains(vertex)&&programs.contains(fragment),"Compiled program reference missing");
    p.vertexCode=programs.at(vertex);p.fragmentCode=programs.at(fragment);auto& d=p.description;
    d.vertexBytecode=bytes(*p.vertexCode);d.fragmentBytecode=bytes(*p.fragmentCode);
    const auto count=r.count(16,12);require(count>0,"Compiled source has no vertex attributes");std::set<unsigned> locations;
    for(unsigned i=0;i<count;++i){gpu::SourceAttribute a{r.bounded(15),r.bounded(4),r.bounded(240)};
        require(a.components>=1&&a.byteOffset%4==0&&a.byteOffset+a.components*4<=p.stride&&locations.insert(a.location).second,"Invalid compiled source attribute");d.attributes.push_back(a);}
    d.blendEnabled=r.flag();d.sourceRGB=r.bounded(10);d.destinationRGB=r.bounded(10);d.sourceAlpha=r.bounded(10);d.destinationAlpha=r.bounded(10);
    d.rgbOperation=r.bounded(4);d.alphaOperation=r.bounded(4);d.writeMask=r.bounded(15);d.depthWrite=r.flag();d.stencilEnabled=r.flag();
    d.depthCompare=r.bounded(7);d.cull=r.bounded(2);d.front=loadFace(r);d.back=loadFace(r);return p;
}
void saveProvenance(BinaryWriter& w,const gpu::SourceSceneProvenance& p) {
    require(validSHA(p.packetSHA256)&&validSHA(p.frameSHA256)&&validSHA(p.shaderManifestSHA256),"Missing source provenance hashes");
    w.identity(p.packetSHA256);w.identity(p.frameSHA256);w.identity(p.shaderManifestSHA256);w.identity(p.frameName);
}
gpu::SourceSceneProvenance loadProvenance(BinaryReader& r) {
    gpu::SourceSceneProvenance p{r.identity(),r.identity(),r.identity(),r.identity()};
    require(validSHA(p.packetSHA256)&&validSHA(p.frameSHA256)&&validSHA(p.shaderManifestSHA256),"Invalid compiled source provenance");return p;
}
void saveState(BinaryWriter& w,const gpu::SourceBatchState& b) {
    w.identity(b.sourceNodeID);w.identity(b.sourceMesh);w.identity(b.material);w.matrix(b.world);for(auto x:b.vertexColor)w.f32(x);w.flag(b.visible);
    w.count(b.uniformOverrides.size(),512);for(const auto& o:b.uniformOverrides){w.identity(o.name);w.count(o.value.size(),16384);for(auto x:o.value)w.f32(x);}
}
gpu::SourceBatchState loadState(BinaryReader& r) {
    gpu::SourceBatchState b;b.sourceNodeID=r.identity();b.sourceMesh=r.identity();b.material=r.identity();b.world=r.matrix();for(auto& c:b.vertexColor)c=r.f32();b.visible=r.flag();
    const auto count=r.count(512,9);r.consumeBudget(count*sizeof(gpu::SourceUniformOverride));b.uniformOverrides.reserve(count);std::set<std::string> names;
    for(unsigned i=0;i<count;++i){gpu::SourceUniformOverride o;o.name=r.identity();require(names.insert(o.name).second,"Duplicate compiled override name");const auto n=r.count(16384,4);
        r.consumeBudget(std::size_t(n)*4);o.value.resize(n);for(auto& x:o.value)x=r.f32();b.uniformOverrides.push_back(std::move(o));}return b;
}
void saveCell(BinaryWriter& w,const UniformCell& c) {
    w.flag(c.accentAllowed);w.blob(c.bytes);w.count(c.fields.size(),4096);
    for(const auto& f:c.fields){w.u32(f.offset);w.identity(f.name);w.u32(static_cast<unsigned>(f.dynamic));w.flag(f.hasValue);w.flag(f.isColor);w.count(f.value.size(),16384);for(auto x:f.value)w.f32(x);
        w.flag(f.overrideIndex.has_value());if(f.overrideIndex)w.count(*f.overrideIndex,511);}
}
UniformCell loadCell(BinaryReader& r,const gpu::SourceBatchState& state,const gpu::SourceFrameParameters& params,unsigned version,std::size_t& totalFields) {
    UniformCell c;c.accentAllowed=r.flag();c.bytes=r.blob(65536);require(!c.bytes.empty()&&c.bytes.size()%16==0,"Compiled constant buffer is not16-byte aligned");
    r.consumeBudget(c.bytes.size()*2);c.scratch.resize(c.bytes.size());const auto count=r.count(4096,15);
    require(count<=262144-totalFields,"Too many total compiled uniform fields");totalFields+=count;
    r.consumeBudget(count*sizeof(UniformField)*2);c.fields.reserve(count);
    for(unsigned i=0;i<count;++i){UniformField f;f.offset=r.bounded(65536);if(version>=2)f.name=r.identity();require(f.offset%4==0,"Misaligned compiled source field offset");
        f.dynamic=static_cast<Dynamic>(r.bounded(13));f.hasValue=r.flag();f.isColor=r.flag();const auto values=r.count(16384,4);r.consumeBudget(std::size_t(values)*4);f.value.resize(values);for(auto& x:f.value)x=r.f32();
        require(f.hasValue||values==0,"Dynamic compiled field has static values");
        if(r.flag()){f.overrideIndex=r.bounded(511);const auto& o=state.uniformOverrides;
            require(f.hasValue&&*f.overrideIndex<o.size()&&(version<2||f.name==o[*f.overrideIndex].name)&&f.value==o[*f.overrideIndex].value,"Invalid compiled typed override plan");c.dependencies|=overrideDependency;}
        if(f.hasValue){if(f.isColor&&f.value.size()==4&&c.accentAllowed)c.dependencies|=accentDependency;}else c.dependencies|=dependency(f.dynamic);
        c.fields.push_back(std::move(f));}
    encode(c,state,params);require(c.bytes==c.scratch,"Compiled ordered uniform plan differs from source bytes");return c;
}
void saveDraw(BinaryWriter& w,const gpu::SourceDraw& d) {
    w.identity(d.mesh);w.identity(d.pipeline);w.u32(d.firstIndex);w.u32(d.indexCount);w.u32(d.stencilReference);w.count(d.uniforms.size(),28);
    for(const auto& b:d.uniforms){w.u32(static_cast<unsigned>(b.stage));w.u32(b.slot);w.identity(b.id);}
    w.count(d.textures.size(),32);for(const auto& t:d.textures){w.u32(static_cast<unsigned>(t.stage));w.u32(t.slot);w.u32(t.samplerSlot);w.identity(t.id);w.identity(t.propertyName);}
}
struct LoadedSourceScene {
    PreparedReplay scene;gpu::SourceSceneProvenance provenance;
    std::vector<BatchPrototype> extra;std::vector<gpu::SourceSceneProvenance> catalogProvenance;
};
std::string saveCompiled(const PreparedReplay& scene,const gpu::SourceSceneProvenance& provenance,
                         std::span<const BatchPrototype> prototypes,std::span<const gpu::SourceSceneProvenance> catalogProvenance) {
    BinaryWriter w;saveProvenance(w,provenance);
    saveParameters(w,scene.parameters);w.count(scene.inactiveUniforms,1024*1024);
    w.count(scene.shaderKeys.size(),2048);for(const auto& key:scene.shaderKeys)w.identity(key);
    w.count(scene.originalGeometry.size(),8192);std::map<const OriginalGeometry*,std::string> geometryIDs;
    for(const auto& [id,g]:scene.originalGeometry){geometryIDs.emplace(g.get(),id);w.identity(id);w.u32(g->stride);w.u32(g->colorOffset);w.blob(g->vertices);
        w.count(g->indices.size(),3000000);for(auto i:g->indices)w.u32(i);w.count(g->colors.size(),1000000);for(const auto& color:g->colors)for(auto x:color)w.f32(x);}
    w.count(scene.meshes.size(),4096);for(const auto& [id,m]:scene.meshes){w.identity(id);w.identity(geometryIDs.at(m.original.get()));}
    // Deduplicate exact bytecode without depending on pointer identity. Source
    // descriptor/stage hashes remain chained by shaderManifestSHA256.
    std::map<std::string,std::string,std::less<>> programs;
    for(const auto& [id,p]:scene.pipelines){(void)id;programs.emplace(*p.vertexCode,hash(*p.vertexCode));programs.emplace(*p.fragmentCode,hash(*p.fragmentCode));}
    w.count(programs.size(),4096);for(const auto& [code,id]:programs){w.identity(id);w.blob(code);}
    w.count(scene.pipelines.size(),2048);for(const auto& [id,p]:scene.pipelines){w.identity(id);savePipeline(w,p,programs);}
    w.count(scene.textures.size(),2048);for(const auto& [id,t]:scene.textures){w.identity(id);const auto& d=t.description;w.identity(d.pixelFormat);
        w.u32(d.minFilter);w.u32(d.magFilter);w.u32(d.mipFilter);w.u32(d.addressU);w.u32(d.addressV);w.u32(d.addressW);w.u32(d.maxAnisotropy);
        w.count(d.mips.size(),15);require(d.mips.size()==t.levels.size(),"Texture storage/mip plan mismatch");
        for(unsigned i=0;i<d.mips.size();++i){const auto& m=d.mips[i];w.u32(m.width);w.u32(m.height);w.u32(m.rowBytes);w.blob(t.levels[i].storage);}}
    w.count(scene.batches.size(),4096);require(scene.batchMeshes.size()==scene.batches.size(),"Missing source batch geometry plan");
    for(std::size_t i=0;i<scene.batches.size();++i){const auto& b=scene.batches[i];w.identity(b.sourceNodeID);w.identity(b.sourceMesh);w.identity(b.material);w.identity(scene.batchMeshes[i]);
        w.matrix(b.world);for(auto x:b.vertexColor)w.f32(x);w.flag(b.visible);w.count(b.uniformOverrides.size(),512);
        for(const auto& o:b.uniformOverrides){w.identity(o.name);w.count(o.value.size(),16384);for(auto x:o.value)w.f32(x);}}
    w.count(scene.uniforms.size(),8192);for(const auto& [id,c]:scene.uniforms){w.identity(id);w.count(c.owner,4095);saveCell(w,c);}
    w.count(scene.draws.size(),16384);require(scene.drawOwners.size()==scene.draws.size(),"Missing source draw ownership");
    for(std::size_t i=0;i<scene.draws.size();++i){w.count(scene.drawOwners[i],4095);saveDraw(w,scene.draws[i]);}
    // Version3 stores extra prototypes independently of the initial rendered
    // frame. Resources are shared once; no hidden fake batches or baked poses.
    require(prototypes.size()>=scene.batches.size()&&prototypes.size()<=4096,"Invalid compiled catalog size");
    w.count(catalogProvenance.size(),4096);require(!catalogProvenance.empty()&&catalogProvenance.front()==provenance,"Missing primary catalog provenance");
    for(const auto& p:catalogProvenance)saveProvenance(w,p);
    w.count(prototypes.size()-scene.batches.size(),4096);
    for(std::size_t i=scene.batches.size();i<prototypes.size();++i){const auto& p=prototypes[i];w.identity(p.description.id);w.identity(p.geometryID);
        const auto found=std::find(catalogProvenance.begin(),catalogProvenance.end(),p.provenance);
        require(found!=catalogProvenance.end(),"Prototype source provenance missing");w.count(static_cast<std::size_t>(found-catalogProvenance.begin()),4095);
        saveParameters(w,p.originalParameters);saveState(w,p.description.originalState);
        w.count(p.cells.size(),256);for(const auto& [id,c]:p.cells){w.identity(id);saveCell(w,c);}
        w.count(p.draws.size(),32);for(const auto& d:p.draws)saveDraw(w,d);
    }
    // Version4 declares source-metadata texture dependencies which need not be
    // visible in any sampled template (for example an initially transparent
    // profile hover). Keep the unused-resource check strict for all other data.
    w.count(scene.textureDependencies.size(),2048);
    for(const auto& [id,origin]:scene.textureDependencies){
        require(scene.textures.contains(id),"Declared desktop texture has no retained original mip data");
        const auto found=std::find(catalogProvenance.begin(),catalogProvenance.end(),origin);
        require(found!=catalogProvenance.end(),"Declared desktop texture source provenance missing");
        w.identity(id);w.count(static_cast<std::size_t>(found-catalogProvenance.begin()),4095);
    }
    BinaryWriter file;file.raw("EHSRC001");file.u32(gpu::SourceScene::compiledSchemaVersion);file.u64(w.data.size());file.raw(hash(w.data));file.raw(w.data);return std::move(file.data);
}
LoadedSourceScene loadCompiled(std::string_view file) {
    require(std::endian::native==std::endian::little,"Original source GPU buffers require a little-endian host");
    require(file.size()>=compiledHeaderBytes&&file.size()<=gpu::SourceScene::maximumCompiledBytes,"Compiled source file exceeds bounds");
    BinaryReader header(file);require(header.raw(8)=="EHSRC001","Unsupported compiled source magic");const auto version=header.u32();require(version>=1&&version<=gpu::SourceScene::compiledSchemaVersion,"Unsupported compiled source schema");
    const auto length=header.u64();require(length==file.size()-compiledHeaderBytes,"Compiled payload length mismatch");
    const auto declared=header.raw(64);require(validSHA(declared),"Invalid compiled payload hash");const auto payload=header.raw(static_cast<std::size_t>(length));
    require(packet::sha256({reinterpret_cast<const std::uint8_t*>(payload.data()),payload.size()})==declared,"Compiled source integrity failed");
    BinaryReader r(payload);auto provenance=loadProvenance(r);
    PreparedReplay s;s.parameters=loadParameters(r);s.width=s.parameters.width;s.height=s.parameters.height;s.inactiveUniforms=r.bounded(1024*1024);
    const auto keys=r.count(2048,5);require(keys>0,"Missing compiled shader keys");for(unsigned i=0;i<keys;++i)require(s.shaderKeys.insert(r.identity()).second,"Duplicate compiled shader key");
    const auto geometryCount=r.count(version>=3?8192:4096,32);require(geometryCount>0,"Missing compiled source geometry");
    for(unsigned i=0;i<geometryCount;++i){const auto id=r.identity();auto g=std::make_shared<OriginalGeometry>();g->stride=r.bounded(256);g->colorOffset=r.bounded(240);
        require(g->stride>=8&&g->stride%4==0&&g->colorOffset%4==0&&g->colorOffset+16<=g->stride,"Invalid compiled source color layout");
        g->vertices=r.blob(gpu::SourceScene::maximumCompiledBytes);r.consumeBudget(g->vertices.size());
        require(!g->vertices.empty()&&g->vertices.size()%g->stride==0&&g->vertices.size()/g->stride<=1000000,"Invalid compiled source vertex length");
        const auto vertexCount=g->vertices.size()/g->stride;const auto indexCount=r.count(3000000,4);require(indexCount>0&&indexCount%3==0,"Invalid compiled source triangle indices");
        r.consumeBudget(std::size_t(indexCount)*4);g->indices.reserve(indexCount);for(unsigned j=0;j<indexCount;++j){const auto index=r.u32();require(index<vertexCount,"Compiled index exceeds source vertex count");g->indices.push_back(index);}
        const auto colors=r.count(1000000,16);require(colors==vertexCount,"Compiled original colors/vertices differ");r.consumeBudget(std::size_t(colors)*16);g->colors.resize(colors);
        for(auto& color:g->colors)for(auto& x:color)x=r.f32();require(s.originalGeometry.emplace(id,std::move(g)).second,"Duplicate compiled geometry identity");}
    const auto meshes=r.count(4096,10);require(meshes>0,"Missing compiled source meshes");std::set<std::string> usedOriginals;
    for(unsigned i=0;i<meshes;++i){const auto id=r.identity(),original=r.identity();require(s.originalGeometry.contains(original),"Compiled original geometry reference missing");usedOriginals.insert(original);
        const auto g=s.originalGeometry.at(original);r.consumeBudget(g->vertices.size());PreparedMesh m{g->stride,g,g->vertices};require(s.meshes.emplace(id,std::move(m)).second,"Duplicate compiled mesh identity");}
    if(version<3)require(usedOriginals.size()==s.originalGeometry.size(),"Compiled asset includes unused source geometry");
    std::map<std::string,std::shared_ptr<const std::string>,std::less<>> programs;const auto programCount=r.count(4096,73);require(programCount>0,"Missing compiled source programs");
    for(unsigned i=0;i<programCount;++i){const auto id=r.identity();auto code=r.blob(4*1024*1024);r.consumeBudget(code.size());
        require(validSHA(id)&&code.size()>=4&&code.substr(0,4)=="DXBC","Invalid compiled source DXBC program");
        // The one complete-file SHA covers the raw bytecode. Program SHA IDs
        // retain the build deduplication identity without rehashing every stage.
        require(programs.emplace(id,std::make_shared<const std::string>(std::move(code))).second,"Duplicate compiled program identity");}
    const auto pipelines=r.count(2048,64);require(pipelines>0,"Missing compiled source pipelines");std::set<const std::string*> usedPrograms;
    for(unsigned i=0;i<pipelines;++i){const auto id=r.identity();auto p=loadPipeline(r,programs);usedPrograms.insert(p.vertexCode.get());usedPrograms.insert(p.fragmentCode.get());
        require(s.pipelines.emplace(id,std::move(p)).second,"Duplicate compiled pipeline identity");}
    require(usedPrograms.size()==programs.size(),"Compiled asset includes unused source bytecode");
    const auto textures=r.count(2048,44);const std::set<std::string> formats{"rgba8Unorm","rgba8Unorm_srgb","bgra8Unorm","bgra8Unorm_srgb","r8Unorm","bc7_rgbaUnorm","bc7_rgbaUnorm_srgb"};
    for(unsigned i=0;i<textures;++i){const auto id=r.identity();PreparedTexture t;auto& d=t.description;d.pixelFormat=r.identity();require(formats.contains(d.pixelFormat),"Unsupported compiled source pixel format");
        d.minFilter=r.bounded(1);d.magFilter=r.bounded(1);d.mipFilter=r.bounded(2);d.addressU=r.bounded(4);d.addressV=r.bounded(4);d.addressW=r.bounded(4);d.maxAnisotropy=r.bounded(16);
        require(d.maxAnisotropy>=1,"Invalid compiled anisotropy");const auto mips=r.count(15,17);require(mips>0,"Missing compiled texture mips");t.levels.reserve(mips);d.mips.reserve(mips);
        unsigned width{},height{};const bool compressed=d.pixelFormat.starts_with("bc7_");
        for(unsigned level=0;level<mips;++level){const auto w=r.bounded(16384),h=r.bounded(16384),row=r.bounded(65536);require(w>0&&h>0,"Invalid compiled texture dimensions");
            if(level==0){width=w;height=h;}require(w==std::max(1u,width>>level)&&h==std::max(1u,height>>level),"Compiled mip dimensions mismatch");
            const auto rows=compressed?(h+3)/4:h,pitch=compressed?(w+3)/4*16:w*(d.pixelFormat=="r8Unorm"?1:4);
            auto raw=r.blob(gpu::SourceScene::maximumCompiledBytes);r.consumeBudget(raw.size());require(row==pitch&&raw.size()==std::size_t(rows)*pitch,"Compiled mip pitch/size mismatch");
            t.levels.push_back({std::move(raw)});d.mips.push_back({w,h,row,t.levels.back().bytes()});}
        require(s.textures.emplace(id,std::move(t)).second,"Duplicate compiled texture identity");}
    const auto batches=r.count(4096,158);require(batches>0,"Missing compiled source batches");s.batches.reserve(batches);s.batchMeshes.reserve(batches);std::set<std::string> usedMeshes;
    for(unsigned i=0;i<batches;++i){gpu::SourceBatchState b;b.sourceNodeID=r.identity();b.sourceMesh=r.identity();b.material=r.identity();const auto mesh=r.identity();
        require(s.meshes.contains(mesh)&&usedMeshes.insert(mesh).second,"Missing/aliased compiled per-batch geometry");b.world=r.matrix();for(auto& c:b.vertexColor)c=r.f32();b.visible=r.flag();
        const auto overrides=r.count(512,9);std::set<std::string> names;
        for(unsigned j=0;j<overrides;++j){gpu::SourceUniformOverride o;o.name=r.identity();require(names.insert(o.name).second,"Duplicate compiled override name");const auto values=r.count(16384,4);
            r.consumeBudget(std::size_t(values)*4);o.value.resize(values);for(auto& x:o.value)x=r.f32();b.uniformOverrides.push_back(std::move(o));}
        tint(s.meshes.at(mesh),b.vertexColor);s.batches.push_back(std::move(b));s.batchMeshes.push_back(mesh);}
    require(usedMeshes.size()==s.meshes.size(),"Compiled asset includes unused per-batch geometry");s.sourceBatches=batches;s.worldChanged.resize(batches);s.overridesChanged.resize(batches);
    const auto uniforms=r.count(8192,14);std::size_t totalFields{};
    for(unsigned i=0;i<uniforms;++i){const auto id=r.identity();const auto owner=r.bounded(batches-1);auto c=loadCell(r,s.batches[owner],s.parameters,version,totalFields);c.owner=owner;
        require(s.uniforms.emplace(id,std::move(c)).second,"Duplicate compiled uniform identity");}
    const auto draws=r.count(16384,27);require(draws>0,"Missing compiled original passes");s.draws.reserve(draws);s.drawOwners.reserve(draws);
    std::set<std::string> usedPipelines,usedUniforms,usedTextures;
    std::map<const OriginalGeometry*,std::set<unsigned>> checkedAttributeOffsets;
    for(unsigned i=0;i<draws;++i){const auto owner=r.bounded(batches-1);gpu::SourceDraw d;d.mesh=r.identity();d.pipeline=r.identity();
        require(d.mesh==s.batchMeshes[owner]&&s.pipelines.contains(d.pipeline),"Compiled draw resource/owner missing");usedPipelines.insert(d.pipeline);
        const auto& mesh=s.meshes.at(d.mesh);const auto& p=s.pipelines.at(d.pipeline);require(p.stride==mesh.stride,"Compiled pass and source vertex strides differ");
        d.firstIndex=r.u32();d.indexCount=r.u32();d.stencilReference=r.bounded(255);
        require(d.firstIndex<=mesh.original->indices.size()&&d.indexCount>0&&d.indexCount%3==0&&d.indexCount<=mesh.original->indices.size()-d.firstIndex,"Invalid compiled draw triangle range");
        // Validate only declared attributes, preserving all undeclared native
        // bytes/padding losslessly. This never normalizes source geometry.
        auto& checked=checkedAttributeOffsets[mesh.original.get()];
        for(const auto& a:p.description.attributes)for(unsigned k=0;k<a.components;++k){const auto offset=a.byteOffset+k*4;if(!checked.insert(offset).second)continue;
            for(std::size_t v=0;v<mesh.original->colors.size();++v){float value;std::memcpy(&value,mesh.original->vertices.data()+v*mesh.stride+offset,4);require(std::isfinite(value),"Nonfinite compiled original vertex attribute");}}
        const auto constants=r.count(28,13);std::set<std::pair<unsigned,unsigned>> slots;
        for(unsigned j=0;j<constants;++j){const auto stage=r.bounded(1),slot=r.bounded(13);const auto id=r.identity();require(s.uniforms.contains(id)&&slots.emplace(stage,slot).second,"Missing/duplicate compiled constant binding");
            const auto& c=s.uniforms.at(id);require(!(c.dependencies&(worldDependency|overrideDependency))||c.owner==owner,"Compiled world/override buffer aliases another batch");
            usedUniforms.insert(id);d.uniforms.push_back({static_cast<gpu::SourceStage>(stage),slot,id});}
        const auto images=r.count(32,17);std::set<std::pair<unsigned,unsigned>> textureSlots,samplerSlots;
        for(unsigned j=0;j<images;++j){const auto stage=r.bounded(1),slot=r.bounded(127),sampler=r.bounded(15);const auto id=r.identity();
            require(s.textures.contains(id)&&textureSlots.emplace(stage,slot).second&&samplerSlots.emplace(stage,sampler).second,"Missing/duplicate compiled texture/sampler binding");
            const auto name=version>=2?r.identity():std::string{};usedTextures.insert(id);d.textures.push_back({static_cast<gpu::SourceStage>(stage),slot,sampler,id,name});}
        s.drawOwners.push_back(owner);s.draws.push_back(std::move(d));}
    require(usedUniforms.size()==s.uniforms.size(),"Compiled asset contains unused initial uniform resources");
    LoadedSourceScene loaded;loaded.provenance=provenance;
    if(version>=3){
        const auto sources=r.count(4096,208);require(sources>0,"Compiled catalog has no provenance chain");loaded.catalogProvenance.reserve(sources);
        for(unsigned i=0;i<sources;++i){auto p=loadProvenance(r);require(std::find(loaded.catalogProvenance.begin(),loaded.catalogProvenance.end(),p)==loaded.catalogProvenance.end(),"Duplicate compiled catalog provenance");loaded.catalogProvenance.push_back(std::move(p));}
        require(loaded.catalogProvenance.front()==provenance,"Compiled catalog primary provenance differs");std::set<unsigned> usedSources{0};
        const auto count=r.count(4096-batches,166);r.consumeBudget(count*sizeof(BatchPrototype)*2);loaded.extra.reserve(count);
        std::set<std::string> templateIDs;for(const auto& b:s.batches)require(templateIDs.insert(b.sourceNodeID).second,"Duplicate initial source template identity");
        std::size_t totalCells=uniforms,totalPasses=draws;
        for(unsigned i=0;i<count;++i){BatchPrototype p;p.description.id=r.identity();require(templateIDs.insert(p.description.id).second,"Duplicate compiled catalog prototype identity");
            p.geometryID=r.identity();require(s.originalGeometry.contains(p.geometryID),"Compiled catalog geometry missing");usedOriginals.insert(p.geometryID);p.geometry=s.originalGeometry.at(p.geometryID);
            const auto source=r.bounded(sources-1);usedSources.insert(source);p.provenance=loaded.catalogProvenance[source];
            p.originalParameters=loadParameters(r);p.description.originalState=loadState(r);
            const auto cellCount=r.count(256,15);require(cellCount<=32768-totalCells,"Too many compiled catalog uniform cells");totalCells+=cellCount;
            for(unsigned j=0;j<cellCount;++j){const auto id=r.identity();auto c=loadCell(r,p.description.originalState,p.originalParameters,3,totalFields);require(p.cells.emplace(id,std::move(c)).second,"Duplicate catalog uniform identity");}
            const auto passes=r.count(32,27);require(passes>0&&passes<=65536-totalPasses,"Invalid compiled catalog pass count");totalPasses+=passes;
            r.consumeBudget(passes*sizeof(gpu::SourceDraw)*2);p.draws.reserve(passes);std::set<std::string> usedCells;std::string sourceMesh;
            for(unsigned j=0;j<passes;++j){gpu::SourceDraw d;d.mesh=r.identity();if(j==0)sourceMesh=d.mesh;require(d.mesh==sourceMesh,"Compiled catalog pass geometry conflicts");
                d.pipeline=r.identity();require(s.pipelines.contains(d.pipeline),"Compiled catalog pipeline missing");usedPipelines.insert(d.pipeline);const auto& pipeline=s.pipelines.at(d.pipeline);
                require(pipeline.stride==p.geometry->stride,"Catalog pass and source vertex strides differ");d.firstIndex=r.u32();d.indexCount=r.u32();d.stencilReference=r.bounded(255);
                require(d.firstIndex<=p.geometry->indices.size()&&d.indexCount>0&&d.indexCount%3==0&&d.indexCount<=p.geometry->indices.size()-d.firstIndex,"Invalid catalog triangle range");
                auto& checked=checkedAttributeOffsets[p.geometry.get()];
                for(const auto& a:pipeline.description.attributes)for(unsigned k=0;k<a.components;++k){const auto offset=a.byteOffset+k*4;if(!checked.insert(offset).second)continue;
                    for(std::size_t v=0;v<p.geometry->colors.size();++v){float value;std::memcpy(&value,p.geometry->vertices.data()+v*p.geometry->stride+offset,4);require(std::isfinite(value),"Nonfinite catalog vertex attribute");}}
                const auto constants=r.count(28,13);r.consumeBudget(constants*sizeof(gpu::SourceUniformBinding)*2);std::set<std::pair<unsigned,unsigned>> constantSlots;
                for(unsigned k=0;k<constants;++k){const auto stage=r.bounded(1),slot=r.bounded(13);const auto id=r.identity();
                    require(p.cells.contains(id)&&constantSlots.emplace(stage,slot).second,"Missing/duplicate catalog constant binding");usedCells.insert(id);d.uniforms.push_back({static_cast<gpu::SourceStage>(stage),slot,id});}
                const auto images=r.count(32,17);r.consumeBudget(images*sizeof(gpu::SourceTextureBinding)*2);std::set<std::pair<unsigned,unsigned>> imageSlots,samplerSlots;
                for(unsigned k=0;k<images;++k){const auto stage=r.bounded(1),slot=r.bounded(127),sampler=r.bounded(15);const auto id=r.identity(),name=r.identity();
                    require(s.textures.contains(id)&&imageSlots.emplace(stage,slot).second&&samplerSlots.emplace(stage,sampler).second,"Missing/duplicate catalog texture/sampler binding");
                    usedTextures.insert(id);d.textures.push_back({static_cast<gpu::SourceStage>(stage),slot,sampler,id,name});}
                p.draws.push_back(std::move(d));}
            require(usedCells.size()==p.cells.size(),"Compiled catalog contains unused uniform cells");loaded.extra.push_back(std::move(p));}
        require(usedSources.size()==loaded.catalogProvenance.size(),"Compiled catalog contains unused source provenance");
    }else loaded.catalogProvenance.push_back(provenance);
    if(version>=4){
        const auto dependencies=r.count(2048,9);
        for(unsigned i=0;i<dependencies;++i){const auto id=r.identity();const auto origin=r.bounded(static_cast<unsigned>(loaded.catalogProvenance.size()-1));
            require(s.textures.contains(id)&&s.textureDependencies.emplace(id,loaded.catalogProvenance[origin]).second,"Missing/duplicate compiled desktop texture dependency");usedTextures.insert(id);
        }
    }
    require(usedOriginals.size()==s.originalGeometry.size(),"Compiled asset includes unused source geometry");
    require(usedPipelines.size()==s.pipelines.size()&&usedTextures.size()==s.textures.size(),"Compiled asset contains unused shader resources");
    require(r.remaining()==0,"Compiled source data has trailing bytes");visibleDraws(s);loaded.scene=std::move(s);return loaded;
}
