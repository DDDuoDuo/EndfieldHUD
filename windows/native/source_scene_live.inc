// Private retained source-template and live-frame adapter. No JSON, paths,
// source hashes, shader conversion or texture creation on a submission.
unsigned components(const UniformField& f) {
    if(f.hasValue)return static_cast<unsigned>(f.value.size());
    switch(f.dynamic){case Dynamic::world:case Dynamic::viewProjection:case Dynamic::viewNoTranslation:case Dynamic::projection:case Dynamic::inverseView:return 16;
    case Dynamic::uiProjection:case Dynamic::cameraPosition:case Dynamic::uiTime:case Dynamic::time:case Dynamic::screen:return 4;
    case Dynamic::renderPath:case Dynamic::flipX:case Dynamic::flipY:return 1;case Dynamic::none:return 0;}return 0;
}
void describePrototype(BatchPrototype& p,const PreparedReplay& s) {
    auto& d=p.description;d.material=d.originalState.material;d.logicalMetadataComplete=true;d.passes.clear();
    d.originalGeometry={p.geometry->stride,bytes(p.geometry->vertices),p.geometry->indices,p.geometry->colors};
    bool accentKnown=false;
    for(const auto& draw:p.draws){gpu::SourcePassTemplate pass;pass.pipelineID=draw.pipeline;pass.vertexStride=s.pipelines.at(draw.pipeline).stride;
        pass.attributes=s.pipelines.at(draw.pipeline).description.attributes;pass.stencilReference=draw.stencilReference;
        for(const auto& binding:draw.uniforms){const auto& cell=p.cells.at(binding.id);
            if(accentKnown)require(d.appliesDesktopAccent==cell.accentAllowed,"Source batch contains inconsistent accent metadata");
            d.appliesDesktopAccent=cell.accentAllowed;accentKnown=true;
            gpu::SourceBufferSchema schema{binding.stage,binding.slot,cell.bytes.size(),{}};
            for(const auto& field:cell.fields){d.logicalMetadataComplete&=!field.name.empty();schema.fields.push_back({field.name,field.offset,components(field),field.isColor,!field.hasValue});}
            pass.buffers.push_back(std::move(schema));}
        for(const auto& texture:draw.textures){d.logicalMetadataComplete&=!texture.propertyName.empty();pass.textures.push_back({texture.propertyName,texture.id,texture.stage,texture.slot,texture.samplerSlot});}
        d.passes.push_back(std::move(pass));}
    require(!p.draws.empty(),"Source prototype has no original passes");
}
std::vector<BatchPrototype> prototypes(const PreparedReplay& s) {
    std::map<const OriginalGeometry*,std::string> geometryIDs;for(const auto& [id,g]:s.originalGeometry)geometryIDs.emplace(g.get(),id);
    std::vector<BatchPrototype> out;out.reserve(s.batches.size());
    for(std::size_t index=0;index<s.batches.size();++index){BatchPrototype p;p.description.id=s.batches[index].sourceNodeID;
        p.description.originalState=s.batches[index];p.originalParameters=s.parameters;p.geometry=s.meshes.at(s.batchMeshes[index]).original;p.geometryID=geometryIDs.at(p.geometry.get());
        for(std::size_t i=0;i<s.draws.size();++i)if(s.drawOwners[i]==index){const auto& draw=s.draws[i];p.draws.push_back(draw);
            for(const auto& binding:draw.uniforms)p.cells.emplace(binding.id,s.uniforms.at(binding.id));}
        describePrototype(p,s);out.push_back(std::move(p));}
    return out;
}
void validateGeometry(const gpu::SourceGeometryView& input,const BatchPrototype& prototype,const std::array<float,4>& tint) {
    require(input.stride==80&&input.stride==prototype.geometry->stride&&!input.vertices.empty()&&input.vertices.size()%input.stride==0&&
        input.vertices.size()/input.stride<=1000000&&!input.indices.empty()&&input.indices.size()<=3000000&&input.indices.size()%3==0,"Invalid native source geometry dimensions");
    require(input.vertices.size()+input.indices.size_bytes()<=64*1024*1024,"Native source geometry exceeds retained slot bounds");
    const auto count=input.vertices.size()/input.stride;require(input.originalColors.empty()||input.originalColors.size()==count,"Original source color count differs");
    for(const auto index:input.indices)require(index<count,"Native source index exceeds vertices");
    std::array<bool,64> offsets{};
    for(const auto& pass:prototype.description.passes)for(const auto& attribute:pass.attributes)for(unsigned channel=0;channel<attribute.components;++channel)offsets[(attribute.byteOffset/4)+channel]=true;
    for(std::size_t vertex=0;vertex<count;++vertex){
        for(unsigned component=0;component<20;++component)if(offsets[component]){float value;std::memcpy(&value,input.vertices.data()+vertex*80+component*4,4);require(std::isfinite(value),"Nonfinite source geometry attribute");}
        for(unsigned channel=0;channel<4;++channel){float color;if(input.originalColors.empty())std::memcpy(&color,input.vertices.data()+vertex*80+32+channel*4,4);else color=input.originalColors[vertex][channel];
            require(std::isfinite(color)&&std::isfinite(color*tint[channel]),"Source original/tinted vertex color overflows");}}
}
void validateState(const gpu::SourceAssembledBatch& input,const BatchPrototype& prototype,const PreparedReplay& s,std::size_t indices) {
    require(validIdentity(input.stateID)&&input.stateID.size()<=128&&validIdentity(input.state.sourceNodeID)&&validIdentity(input.state.sourceMesh)&&
        input.state.material==prototype.description.material,"Live source identity/material contract differs");
    require(prototype.description.logicalMetadataComplete,"Source prototype lacks logical metadata; recompile from source with schema2");
    require(finiteMatrix(input.state.world),"Invalid live source world matrix");for(auto x:input.state.vertexColor)require(std::isfinite(x),"Invalid live source vertex tint");
    require(input.state.uniformOverrides.size()<=512,"Too many live source uniform overrides");
    for(std::size_t i=0;i<input.state.uniformOverrides.size();++i){const auto& value=input.state.uniformOverrides[i];require(validIdentity(value.name)&&value.value.size()<=16384,"Invalid live override identity/size");
        for(std::size_t j=0;j<i;++j)require(value.name!=input.state.uniformOverrides[j].name,"Duplicate live uniform override");for(auto x:value.value)require(std::isfinite(x),"Nonfinite live uniform override");
        bool found=false;for(const auto& [id,cell]:prototype.cells){(void)id;for(const auto& field:cell.fields)if(field.name==value.name){
            require(components(field)==value.value.size(),"Live override differs from exact source field width");found=true;}}
        // Mac assigns some source properties even when the selected program
        // does not declare/read them. Preserve that authoritative payload;
        // permit only the exact name/width already present in this prototype.
        if(!found)for(const auto& declared:prototype.description.originalState.uniformOverrides)
            if(declared.name==value.name&&declared.value.size()==value.value.size()){found=true;break;}
        require(found,"Live override has no original source field or declared source payload");}
    for(const auto& original:prototype.description.originalState.uniformOverrides){const auto found=std::find_if(input.state.uniformOverrides.begin(),input.state.uniformOverrides.end(),[&](const auto& v){return v.name==original.name;});
        require(found!=input.state.uniformOverrides.end()&&found->value.size()==original.value.size(),"Live frame omitted a source-authored override");}
    require(input.textureOverrides.size()<=64,"Too many live source textures");
    for(std::size_t i=0;i<input.textureOverrides.size();++i){const auto& value=input.textureOverrides[i];require(s.textures.contains(value.textureID),"Live texture resource not installed");
        for(std::size_t j=0;j<i;++j)require(value.name!=input.textureOverrides[j].name,"Duplicate live texture override");bool found=false;
        for(const auto& draw:prototype.draws)for(const auto& binding:draw.textures)found|=binding.propertyName==value.name;require(found,"Live texture has no original reflected property");}
    if(input.stencilReference)require(*input.stencilReference<=255,"Invalid live stencil reference");
    if(input.indexRange)require(input.indexRange->firstIndex<=indices&&input.indexRange->indexCount>0&&input.indexRange->indexCount%3==0&&input.indexRange->indexCount<=indices-input.indexRange->firstIndex,"Invalid live source index range");
    if(input.geometry)validateGeometry(*input.geometry,prototype,input.state.vertexColor);
    else for(const auto& color:prototype.geometry->colors)for(unsigned c=0;c<4;++c)require(std::isfinite(color[c]*input.state.vertexColor[c]),"Live source tint overflows original color");
}
UniformCell instantiate(const UniformCell& source,const gpu::SourceAssembledBatch& input,std::size_t owner,const gpu::SourceFrameParameters& params) {
    auto cell=source;cell.owner=owner;cell.accentAllowed=input.appliesDesktopAccent;cell.dependencies=0;cell.active=true;cell.dirty=true;
    for(auto& f:cell.fields){f.overrideIndex.reset();
        for(std::size_t i=0;i<input.state.uniformOverrides.size();++i)if(input.state.uniformOverrides[i].name==f.name){f.overrideIndex=i;f.hasValue=true;f.value=input.state.uniformOverrides[i].value;cell.dependencies|=overrideDependency;break;}
        if(f.hasValue){if(f.isColor&&f.value.size()==4&&cell.accentAllowed)cell.dependencies|=accentDependency;}else cell.dependencies|=dependency(f.dynamic);
        if(f.hasValue&&f.isColor&&f.value.size()==4&&cell.accentAllowed){const auto c=accentColor({f.value[0],f.value[1],f.value[2],f.value[3]},params.desktopAccentLinear);for(auto x:c)require(std::isfinite(x),"Live source color overflows theme accent");}}
    encode(cell,input.state,params);cell.bytes.swap(cell.scratch);return cell;
}
bool equalGeometry(const OriginalGeometry& old,const gpu::SourceGeometryView& input) {
    if(old.stride!=input.stride||old.vertices.size()!=input.vertices.size()||old.indices.size()!=input.indices.size()||
        !std::equal(old.vertices.begin(),old.vertices.end(),reinterpret_cast<const char*>(input.vertices.data()))||!std::equal(old.indices.begin(),old.indices.end(),input.indices.begin()))return false;
    if(!input.originalColors.empty())return std::equal(old.colors.begin(),old.colors.end(),input.originalColors.begin());
    for(std::size_t vertex=0;vertex<old.colors.size();++vertex)for(unsigned channel=0;channel<4;++channel){float color;
        std::memcpy(&color,input.vertices.data()+vertex*80+32+channel*4,4);if(color!=old.colors[vertex][channel])return false;}return true;
}
void copyGeometry(OriginalGeometry& out,const gpu::SourceGeometryView& input) {
    out.stride=input.stride;out.colorOffset=32;out.vertices.assign(reinterpret_cast<const char*>(input.vertices.data()),input.vertices.size());
    out.indices.assign(input.indices.begin(),input.indices.end());out.colors.resize(input.vertices.size()/80);
    for(std::size_t vertex=0;vertex<out.colors.size();++vertex)for(unsigned channel=0;channel<4;++channel){if(input.originalColors.empty())
        std::memcpy(&out.colors[vertex][channel],input.vertices.data()+vertex*80+32+channel*4,4);else out.colors[vertex][channel]=input.originalColors[vertex][channel];}
}
void packImage(std::vector<std::uint8_t>& data,const gpu::SourceImageGeometryView& input) {
    require(input.positions.size()==input.uv.size()&&!input.positions.empty()&&input.positions.size()<=1000000,"Invalid source image channels");
    require(input.positions.size()*80<=64*1024*1024,"Source image exceeds retained slot bounds");
    data.resize(input.positions.size()*80);std::fill(data.begin(),data.end(),std::uint8_t{});const float one=1;
    for(std::size_t i=0;i<input.positions.size();++i){std::memcpy(data.data()+i*80,input.positions[i].data(),16);std::memcpy(data.data()+i*80+16,input.uv[i].data(),8);
        for(unsigned channel=0;channel<4;++channel)std::memcpy(data.data()+i*80+32+channel*4,&one,4);}
}
