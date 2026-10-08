#include "core/shell_packet.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#if defined(_WIN32) && !defined(END_FIELD_FORCE_PORTABLE_SHA256)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__) && !defined(END_FIELD_FORCE_PORTABLE_SHA256)
#include <CommonCrypto/CommonDigest.h>
#endif

namespace endfield::core::packet {
namespace {
[[noreturn]] void invalid(const char* message) { throw Error(ErrorCode::invalid,message); }
std::string string(const Json& value, std::size_t maximum=512) {
    if(!value.isString()) invalid("Packet string is missing or has the wrong type");
    auto result=value.string();
    if(result.empty()||result.size()>maximum) invalid("Packet string is empty or too large");
    for(const auto unit:result)if(static_cast<unsigned char>(unit)<32||unit==127)invalid("Packet string contains control characters");
    return result;
}
std::size_t integer(const Json& value,std::size_t maximum,bool zero=false) {
    if(!value.isNumber()) invalid("Packet integer has the wrong type");
    std::int64_t number{};try{number=value.integer();}catch(...){invalid("Packet integer is not exact");}
    if(number<0||(!zero&&number==0)||static_cast<std::uint64_t>(number)>maximum)invalid("Packet integer is outside its limit");
    return static_cast<std::size_t>(number);
}
double number(const Json& value,double maximum=1e12) {
    if(!value.isNumber())invalid("Packet numeric field has the wrong type");
    double result{};try{result=value.number();}catch(...){invalid("Packet number is not representable");}
    if(!std::isfinite(result)||std::abs(result)>maximum)invalid("Packet number is not finite or bounded");
    return result;
}
bool boolean(const Json& value) {if(!value.isBool())invalid("Packet boolean has the wrong type");return value.boolean();}
const Json::Array& array(const Json& value,std::size_t maximum,bool empty=true) {
    if(!value.isArray()||value.array().size()>maximum||(!empty&&value.array().empty()))invalid("Packet array is missing, empty or too large");
    return value.array();
}
void object(const Json& value) {if(!value.isObject())invalid("Packet object has the wrong type");}
void finiteTree(const Json& value) {
    if(value.isNumber()){(void)number(value,std::numeric_limits<double>::max());}
    else if(value.isArray())for(const auto& child:value.array())finiteTree(child);
    else if(value.isObject())for(const auto& [key,child]:value.object()){if(key.size()>4096)invalid("Packet key is too large");finiteTree(child);}
}
Matrix4 matrix(const Json& value) {
    Matrix4 result;const auto& columns=array(value,4,false);if(columns.size()!=4)invalid("Packet matrix must contain four columns");
    for(std::size_t c=0;c<4;++c){const auto& rows=array(columns[c],4,false);if(rows.size()!=4)invalid("Packet matrix column must contain four values");for(std::size_t r=0;r<4;++r)result.values[c*4+r]=number(rows[r]);}
    return result;
}
Point point(const Json& value,bool positive=false) {
    const auto& pair=array(value,2,false);if(pair.size()!=2)invalid("Packet point needs two values");
    Point result{number(pair[0]),number(pair[1])};if(positive&&(result.x<=0||result.y<=0||result.x>32768||result.y>32768))invalid("Packet viewport is invalid");return result;
}
Rect rect(const Json& value,bool hitGeometry=true) {object(value);const auto origin=point(value["origin"]),size=point(value["size"]);if(hitGeometry&&(size.x<0||size.y<0))invalid("Packet hit rectangle has a negative size");return {origin.x,origin.y,size.x,size.y};}
std::uint32_t little32(std::span<const std::uint8_t> bytes,std::size_t at) {
    return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);
}
float float32(std::span<const std::uint8_t> bytes,std::size_t at) {
    const auto result=std::bit_cast<float>(little32(bytes,at));if(!std::isfinite(result))invalid("Geometry has a nonfinite float");return result;
}
std::string lower(std::string value) {for(auto& unit:value)if(unit>='A'&&unit<='Z')unit+=('a'-'A');return value;}
std::string readText(const std::filesystem::path& path,std::size_t maximum) {
    try {auto bytes=ehud::data::detail::readFile(path,maximum);if(!bytes)throw Error(ErrorCode::missing,"Package file is missing");return std::move(*bytes);}
    catch(const ehud::data::StoreError& error){throw Error(error.code()==ehud::data::StoreErrorCode::tooLarge?ErrorCode::tooLarge:ErrorCode::unavailable,"Package file could not be read safely");}
}
Json parse(std::string_view bytes,std::size_t maximum=Package::maximumJSONBytes) {try{return Json::parse(bytes,maximum);}catch(...){invalid("Package JSON is invalid or too large");}}
bool digest(std::string_view value) {if(value.size()!=64)return false;for(const auto c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
unsigned pixelBytes(std::string_view format) {
    if(format=="rgba8Unorm"||format=="rgba8Unorm_srgb"||format=="bgra8Unorm"||format=="bgra8Unorm_srgb")return 4;
    if(format=="r8Unorm")return 1;
    if(format=="bc7_rgbaUnorm"||format=="bc7_rgbaUnorm_srgb")return 16;
    invalid("Texture format is outside packet schema v1");
}
}

std::string sha256(std::span<const std::uint8_t> bytes) {
    if(bytes.size()>UINT32_MAX)throw Error(ErrorCode::tooLarge,"Hash input is outside packet limits");
#if (defined(_WIN32) || defined(__APPLE__)) && !defined(END_FIELD_FORCE_PORTABLE_SHA256)
    std::array<std::uint8_t,32> digest{};const std::uint8_t empty{};
    const auto* input=bytes.empty()?&empty:bytes.data();
#ifdef _WIN32
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(input),static_cast<ULONG>(bytes.size()),digest.data(),static_cast<ULONG>(digest.size()))<0)
        throw Error(ErrorCode::unavailable,"Native SHA-256 failed");
#else
    if(!CC_SHA256(input,static_cast<CC_LONG>(bytes.size()),digest.data()))throw Error(ErrorCode::unavailable,"Native SHA-256 failed");
#endif
    constexpr char hex[]="0123456789abcdef";std::string result(64,'0');
    for(std::size_t i=0;i<digest.size();++i){result[i*2]=hex[digest[i]>>4];result[i*2+1]=hex[digest[i]&15];}
    return result;
#else
    // FIPS 180-4 sections 4.2.2, 5.1.1 and 6.2. No padded input-size allocation.
    constexpr std::array<std::uint32_t,64> constants{
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<std::uint32_t,8> state{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const auto block=[&](std::span<const std::uint8_t> input) {
        std::array<std::uint32_t,64> w{};
        for(unsigned i=0;i<16;++i)w[i]=(std::uint32_t(input[i*4])<<24)|(std::uint32_t(input[i*4+1])<<16)|(std::uint32_t(input[i*4+2])<<8)|input[i*4+3];
        for(unsigned i=16;i<64;++i){const auto s0=std::rotr(w[i-15],7)^std::rotr(w[i-15],18)^(w[i-15]>>3),s1=std::rotr(w[i-2],17)^std::rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
        auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for(unsigned i=0;i<64;++i){const auto t1=h+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+constants[i]+w[i];const auto t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    };
    if(bytes.size()>std::numeric_limits<std::uint64_t>::max()/8)invalid("Hash input is too large");
    std::size_t offset{};while(bytes.size()-offset>=64){block(bytes.subspan(offset,64));offset+=64;}
    std::array<std::uint8_t,128> tail{};const auto remainder=bytes.size()-offset;
    if(remainder)std::memcpy(tail.data(),bytes.data()+offset,remainder);tail[remainder]=0x80;
    const std::size_t tailSize=remainder<56?64:128;const auto bits=std::uint64_t(bytes.size())*8;
    for(unsigned i=0;i<8;++i)tail[tailSize-1-i]=static_cast<std::uint8_t>(bits>>(i*8));
    block(std::span(tail).first(64));if(tailSize==128)block(std::span(tail).subspan(64,64));
    constexpr char hex[]="0123456789abcdef";std::string result(64,'0');
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j){const auto byte=static_cast<std::uint8_t>(state[i]>>((3-j)*8));result[i*8+j*2]=hex[byte>>4];result[i*8+j*2+1]=hex[byte&15];}
    return result;
#endif
}

std::filesystem::path Package::path(std::string_view relative) const {
    if(relative.empty()||relative.size()>4096||relative.front()=='/'||relative.back()=='/')invalid("Package path is invalid");
    for(const auto c:relative)if(static_cast<unsigned char>(c)<32||c==127||c=='\\'||c==':'||c=='?'||c=='*'||c=='"'||c=='<'||c=='>'||c=='|')invalid("Package path is not portable");
    std::size_t at{};while(at<relative.size()){
        const auto next=relative.find('/',at);const auto part=relative.substr(at,next==std::string_view::npos?relative.size()-at:next-at);
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' ')invalid("Package path escapes its root or is ambiguous");
        const auto base=lower(std::string(part.substr(0,part.find('.'))));
        if(base=="con"||base=="prn"||base=="aux"||base=="nul"||(base.size()==4&&(base.starts_with("com")||base.starts_with("lpt"))&&base[3]>='1'&&base[3]<='9'))invalid("Package path uses a reserved device name");
        if(next==std::string_view::npos)break;at=next+1;
    }
    const auto result=root_/std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(relative.data()),relative.size()));
    try{ehud::data::detail::validateDataFile(result);}catch(const ehud::data::StoreError&){invalid("Package path is not an ordinary confined file");}
    return result;
}
Blob Package::registerBlob(const Json& value,std::size_t maximum) {
    object(value);Blob result{string(value["file"],4096),string(value["sha256"],64),integer(value["bytes"],maximum,true)};
    if(!digest(result.sha256))invalid("Package digest is not lowercase SHA-256");(void)path(result.file);
    if(auto existing=blobs_.find(result.file);existing!=blobs_.end()){if(existing->second!=result)invalid("Package file has conflicting descriptors");return result;}
    if(blobs_.size()>=16384||referencedBytes_>maximumPackageBytes-result.bytes)throw Error(ErrorCode::tooLarge,"Package reference budget exceeded");
    if(!portablePaths_.insert(lower(result.file)).second)invalid("Package paths differ only in case");
    referencedBytes_+=result.bytes;blobs_.emplace(result.file,result);return result;
}
BinaryData Package::read(const Blob& blob) const {
    if(blob.bytes>maximumBlobBytes||!digest(blob.sha256))invalid("Blob descriptor is outside its limits");
    BinaryData result{readText(path(blob.file),blob.bytes)};
    if(result.storage.size()!=blob.bytes||sha256(result.bytes())!=blob.sha256)throw Error(ErrorCode::integrity,"Package blob length or digest mismatch");
    return result;
}

Package::Package(std::filesystem::path root):root_(std::move(root)) {
    try{ehud::data::detail::validateRoot(root_);}catch(const ehud::data::StoreError&){invalid("Package root must be explicit, absolute and ordinary");}
    {
        const auto bytes=readText(path("shell-packet.json"),maximumJSONBytes);
        manifest_=parse(bytes);manifestSHA256_=sha256({reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});
    }
    object(manifest_);
    if(!manifest_["schemaVersion"].isNumber()||integer(manifest_["schemaVersion"],INT32_MAX)!=1)throw Error(ErrorCode::unsupportedVersion,"Unsupported shell packet schema");
    if(!boolean(manifest_["desktopMode"]))invalid("Package is not a desktop source fixture");
    object(manifest_["coordinates"]);object(manifest_["fixture"]);finiteTree(manifest_);
    for(const auto& entry:array(manifest_["meshes"],4096,false)) {
        object(entry);MeshDescriptor mesh;mesh.id=string(entry["id"]);mesh.sourceMesh=string(entry["sourceMesh"]);
        mesh.vertexCount=integer(entry["vertexCount"],1'000'000);mesh.indexCount=integer(entry["indexCount"],3'000'000);
        if(mesh.indexCount%3)invalid("Mesh indices must form triangle primitives");
        mesh.vertices=registerBlob(entry["vertices"]);mesh.indices=registerBlob(entry["indices"]);mesh.originalVertexBuffer=registerBlob(entry["originalVertexBuffer"]);
        if(mesh.vertices.bytes!=mesh.vertexCount*40||integer(entry["vertices"]["stride"],40)!=40||
           integer(entry["vertices"]["fields"]["position"],40,true)!=0||integer(entry["vertices"]["fields"]["uv"],40,true)!=16||integer(entry["vertices"]["fields"]["color"],40,true)!=24)
            invalid("Canonical vertex layout differs from schema v1");
        if(mesh.indices.bytes!=mesh.indexCount*4||string(entry["indices"]["format"])!="uint32-le")invalid("Index buffer layout differs from schema v1");
        const auto stride=integer(entry["originalVertexBuffer"]["stride"],1024);if(stride%4||mesh.originalVertexBuffer.bytes!=mesh.vertexCount*stride)invalid("Original vertex buffer stride or length is invalid");
        object(entry["originalVertexBuffer"]["offsets"]);for(const auto& [field,offset]:entry["originalVertexBuffer"]["offsets"].object()) {(void)field;if(integer(offset,stride,true)>=stride)invalid("Original vertex attribute is outside its stride");}
        mesh.metadata=entry;if(!meshes_.emplace(mesh.id,std::move(mesh)).second)invalid("Duplicate mesh ID");
    }
    for(const auto& entry:array(manifest_["textures"],2048)) {
        object(entry);TextureDescriptor texture;texture.id=string(entry["id"]);texture.pixelFormat=string(entry["pixelFormat"]);const auto unitBytes=pixelBytes(texture.pixelFormat);
        texture.width=static_cast<unsigned>(integer(entry["width"],16384));texture.height=static_cast<unsigned>(integer(entry["height"],16384));texture.sRGB=boolean(entry["sRGB"]);
        if(texture.sRGB!=texture.pixelFormat.ends_with("_srgb"))invalid("Texture color-space metadata conflicts with its format");
        object(entry["sampler"]);texture.metadata=entry;
        for(const auto* field:{"minFilter","magFilter","mipFilter","addressU","addressV","addressW","compareFunction"})(void)integer(entry["sampler"][field],UINT32_MAX,true);
        (void)integer(entry["sampler"]["maxAnisotropy"],16);(void)number(entry["sampler"]["lodMinClamp"],std::numeric_limits<float>::max());(void)number(entry["sampler"]["lodMaxClamp"],std::numeric_limits<float>::max());(void)boolean(entry["sampler"]["normalizedCoordinates"]);
        unsigned expectedWidth=texture.width,expectedHeight=texture.height,level{};
        for(const auto& mip:array(entry["mips"],15,false)) {
            object(mip);MipDescriptor descriptor;descriptor.level=static_cast<unsigned>(integer(mip["level"],14,true));
            descriptor.width=static_cast<unsigned>(integer(mip["width"],16384));descriptor.height=static_cast<unsigned>(integer(mip["height"],16384));descriptor.rowBytes=static_cast<unsigned>(integer(mip["rowBytes"],262144));descriptor.data=registerBlob(mip);
            if(descriptor.level!=level++||descriptor.width!=expectedWidth||descriptor.height!=expectedHeight)invalid("Texture mip sequence has invalid dimensions");
            const bool blocks=texture.pixelFormat.starts_with("bc7_");const auto columns=blocks?(expectedWidth+3)/4:expectedWidth,rows=blocks?(expectedHeight+3)/4:expectedHeight;
            if(descriptor.rowBytes!=columns*unitBytes||descriptor.data.bytes!=std::size_t(descriptor.rowBytes)*rows)invalid("Texture mip storage size is invalid");
            texture.mips.push_back(descriptor);expectedWidth=std::max(1u,expectedWidth/2);expectedHeight=std::max(1u,expectedHeight/2);
            if(descriptor.width==1&&descriptor.height==1&&texture.mips.size()!=entry["mips"].array().size())invalid("Texture repeats the last mip level");
        }
        if(!textures_.emplace(texture.id,std::move(texture)).second)invalid("Duplicate texture ID");
    }
    for(const auto& entry:array(manifest_["materials"],4096,false)) {
        object(entry);const auto id=string(entry["id"]);object(entry["values"]);object(entry["propertyTypes"]);object(entry["textures"]);
        for(const auto& [property,texture]:entry["textures"].object()) {(void)property;if(!texture.isNull()&&!textures_.contains(string(texture)))invalid("Material references an absent texture");}
        std::set<std::string> passes;
        for(const auto& pass:array(entry["passes"],64,false)) {
            object(pass);if(!passes.insert(string(pass["id"])).second)invalid("Duplicate material pass ID");
            (void)string(pass["shaderKey"]);object(pass["blend"]);(void)boolean(pass["blend"]["enabled"]);
            for(const auto* field:{"sourceRGB","destinationRGB","sourceAlpha","destinationAlpha","rgbOperation","alphaOperation","writeMask"})(void)integer(pass["blend"][field],UINT32_MAX,true);
            // Exact source shader/stages/depthStencil/cull and textureBindings
            // remain available verbatim. The renderer must map or reject them.
            if(!pass.contains("shader")||!pass.contains("stages")||!pass.contains("textureBindings")||!pass.contains("depthStencil")||!pass.contains("cull"))invalid("Material pass is incomplete");
        }
        if(!materials_.emplace(id,entry).second)invalid("Duplicate material ID");
    }
    for(const auto& entry:array(manifest_["frames"],256,false)) {
        object(entry);FrameDescriptor frame{string(entry["name"]),registerBlob(entry,maximumJSONBytes)};
        if(!frames_.emplace(frame.name,std::move(frame)).second)invalid("Duplicate frame name");
    }
    animation_=registerBlob(manifest_["animation"],maximumAnimationJSONBytes);
    for(const auto& entry:array(manifest_["shaderAssets"],4096)) {auto blob=registerBlob(entry,maximumJSONBytes);(void)string(entry["sourcePath"],4096);if(!shaderAssets_.emplace(blob.file,blob).second)invalid("Duplicate shader asset");}
    if(manifest_.contains("nativeRasterAssets"))for(const auto& entry:array(manifest_["nativeRasterAssets"],2048)) {
        auto blob=registerBlob(entry);(void)integer(entry["width"],16384);(void)integer(entry["height"],16384);
        if(entry.contains("path")&&string(entry["path"],4096)!=blob.file)invalid("Native raster path aliases disagree");
        if(!nativeRasters_.emplace(blob.file,blob).second)invalid("Duplicate native raster asset");
    }
    if(manifest_.contains("modules"))(void)array(manifest_["modules"],256);
}
MeshData Package::loadMesh(std::string_view id) const {
    const auto found=meshes_.find(id);if(found==meshes_.end())throw Error(ErrorCode::missing,"Mesh ID is not in this package");
    const auto& descriptor=found->second;const auto vertices=read(descriptor.vertices),indices=read(descriptor.indices);
    MeshData result;result.vertices.reserve(descriptor.vertexCount);result.indices.reserve(descriptor.indexCount);
    for(std::size_t i=0;i<descriptor.vertexCount;++i) {
        SourceVertex vertex;for(unsigned c=0;c<4;++c){vertex.position[c]=float32(vertices.bytes(),i*40+c*4);vertex.color[c]=float32(vertices.bytes(),i*40+24+c*4);}for(unsigned c=0;c<2;++c)vertex.uv[c]=float32(vertices.bytes(),i*40+16+c*4);result.vertices.push_back(vertex);
    }
    for(std::size_t i=0;i<descriptor.indexCount;++i){const auto index=little32(indices.bytes(),i*4);if(index>=descriptor.vertexCount)invalid("Geometry index is outside its vertex buffer");result.indices.push_back(index);}
    return result;
}
BinaryData Package::loadOriginalVertexBuffer(std::string_view id) const {
    const auto found=meshes_.find(id);if(found==meshes_.end())throw Error(ErrorCode::missing,"Mesh ID is not in this package");return read(found->second.originalVertexBuffer);
}
BinaryData Package::loadTextureMip(std::string_view id,unsigned level) const {
    const auto found=textures_.find(id);if(found==textures_.end()||level>=found->second.mips.size())throw Error(ErrorCode::missing,"Texture mip is not in this package");return read(found->second.mips[level].data);
}
FrameData Package::loadFrame(std::string_view name) const {
    const auto found=frames_.find(name);if(found==frames_.end())throw Error(ErrorCode::missing,"Frame name is not in this package");
    const auto data=read(found->second.data);FrameData frame;frame.metadata=parse(data.storage);object(frame.metadata);finiteTree(frame.metadata);
    frame.viewport=point(frame.metadata["viewport"],true);const auto& camera=frame.metadata["camera"];object(camera);frame.canvasSize=point(camera["canvasSize"],true);
    frame.projection=matrix(camera["projection"]);frame.view=matrix(camera["view"]);frame.worldRoot=matrix(camera["worldRoot"]);
    object(frame.metadata["gpuCamera"]);
    frame.gpuViewProjection=matrix(frame.metadata["gpuCamera"]["viewProjection"]);
    frame.gpuViewNoTranslationProjection=matrix(frame.metadata["gpuCamera"]["viewNoTranslationProjection"]);
    std::map<std::string,std::string,std::less<>> parents;
    for(const auto& node:array(frame.metadata["nodes"],RetainedScene::maximumNodes,false)) {
        object(node);const auto id=string(node["id"]);if(!parents.emplace(id,node["parent"].isNull()?std::string{}:string(node["parent"])).second)invalid("Duplicate frame node ID");
        (void)matrix(node["localMatrix"]);(void)matrix(node["worldMatrix"]);
        // Source Transform nodes need no RectTransform; keep that distinction.
        if(!node["rect"].isNull())(void)rect(node["rect"],false);(void)boolean(node["active"]);
        const auto alpha=number(node["inheritedAlpha"]);if(alpha<0||alpha>1)invalid("Frame inherited alpha is invalid");
    }
    for(const auto& [id,parent]:parents){(void)id;if(!parent.empty()&&!parents.contains(parent))invalid("Frame node parent is absent");}
    std::map<std::string,unsigned,std::less<>> state;
    for(const auto& [id,parent]:parents){(void)parent;std::string cursor=id;std::vector<std::string> chain;while(!cursor.empty()&&state[cursor]!=2){if(state[cursor]==1)invalid("Frame node parents form a cycle");state[cursor]=1;chain.push_back(cursor);cursor=parents.at(cursor);}for(const auto& node:chain)state[node]=2;}
    for(const auto& hit:array(frame.metadata["hits"],RetainedScene::maximumTargets)) {
        object(hit);if(!parents.contains(string(hit["buttonID"]))||!parents.contains(string(hit["graphicID"])))invalid("Hit target references an absent source node");
        (void)point(hit["center"]);(void)rect(hit["rect"]);(void)matrix(hit["worldMatrix"]);
        const auto& corners=array(hit["projectedCorners"],4,false);if(corners.size()!=4)invalid("Hit contour is not a projected quad");for(const auto& corner:corners)(void)point(corner);
        for(const auto& mask:array(hit["masks"],16)){object(mask);(void)rect(mask["rect"]);(void)matrix(mask["worldMatrix"]);}
    }
    std::map<std::string,Blob,std::less<>> uniformFiles;std::set<std::string> uniformPaths;std::size_t uniformBytes{};
    for(const auto& batch:array(frame.metadata["batches"],16384)) {
        object(batch);const auto mesh=string(batch["mesh"]);if(!meshes_.contains(mesh)||!materials_.contains(string(batch["material"]))||!parents.contains(string(batch["sourceNodeID"])))invalid("Draw batch references absent geometry, material or node");
        (void)matrix(batch["worldMatrix"]);if(array(batch["color"],4,false).size()!=4)invalid("Draw batch color is invalid");
        object(batch["textureOverrides"]);for(const auto& [property,texture]:batch["textureOverrides"].object()){(void)property;if(!texture.isNull()&&!textures_.contains(string(texture)))invalid("Draw batch override references an absent texture");}
        object(batch["uniformOverrides"]);
        std::vector<UniformDescriptor> uniforms;std::set<std::pair<std::string,unsigned>> bindings;
        for(const auto& uniform:array(batch["uniforms"],256)) {
            object(uniform);UniformDescriptor descriptor;
            descriptor.passID=string(uniform["passID"]);descriptor.shaderKey=string(uniform["shaderKey"]);descriptor.stage=string(uniform["stage"]);descriptor.bufferName=string(uniform["bufferName"]);
            if(descriptor.stage!="vertex"&&descriptor.stage!="fragment")invalid("Uniform stage is outside schema v1");
            descriptor.index=static_cast<unsigned>(integer(uniform["index"],31,true));descriptor.byteCount=integer(uniform["byteCount"],65536);
            descriptor.needsWorld=boolean(uniform["needsWorld"]);descriptor.needsCamera=boolean(uniform["needsCamera"]);descriptor.needsTime=boolean(uniform["needsTime"]);
            const auto& payload=uniform["payload"];object(payload);
            descriptor.data={string(payload["file"],4096),string(payload["sha256"],64),integer(payload["bytes"],65536)};
            if(descriptor.data.bytes!=descriptor.byteCount||!digest(descriptor.data.sha256))invalid("Uniform payload length/digest is invalid");(void)path(descriptor.data.file);
            if(const auto existing=uniformFiles.find(descriptor.data.file);existing!=uniformFiles.end()) {
                if(existing->second!=descriptor.data)invalid("Uniform file has conflicting descriptors");
            } else {
                if(!uniformPaths.insert(lower(descriptor.data.file)).second)invalid("Uniform paths differ only in case");
                if(const auto manifestBlob=blobs_.find(descriptor.data.file);manifestBlob!=blobs_.end()&&manifestBlob->second!=descriptor.data)invalid("Uniform descriptor conflicts with manifest blob");
                if(uniformBytes>64*1024*1024-descriptor.byteCount)throw Error(ErrorCode::tooLarge,"Frame uniform payload budget exceeded");
                uniformBytes+=descriptor.byteCount;uniformFiles.emplace(descriptor.data.file,descriptor.data);
            }
            const auto key=std::make_pair(descriptor.passID+"/"+descriptor.stage,descriptor.index);
            if(!bindings.insert(key).second)invalid("Uniform buffer binding is duplicated");
            bool knownPass=false;
            for(const auto& pass:materials_.at(batch["material"].string())["passes"].array())
                if(pass["id"].string()==descriptor.passID&&pass["shaderKey"].string()==descriptor.shaderKey)knownPass=true;
            if(!knownPass)invalid("Uniform payload references an absent material pass");
            for(const auto& field:array(uniform["fields"],4096)) {
                object(field);(void)string(field["name"]);const auto offset=integer(field["offset"],descriptor.byteCount,true);(void)boolean(field["isColor"]);
                if(offset>=descriptor.byteCount||offset%4)invalid("Uniform field offset is invalid");
                if(!field["value"].isNull()) {const auto& values=array(field["value"],4096);if(values.size()>(descriptor.byteCount-offset)/4)invalid("Uniform field value exceeds its buffer");}
                if(field.contains("dynamic")&&!field["dynamic"].isNull())(void)string(field["dynamic"]);
            }
            descriptor.metadata=uniform;uniforms.push_back(std::move(descriptor));
        }
        frame.uniforms.push_back(std::move(uniforms));
        // Source indexRange/colorWriteMask/appliesDesktopAccent/gpuCamera and
        // native layer descriptors remain exact. No rendering mode is inferred.
        if(!batch["indexRange"].isNull()) {
            const auto& range=array(batch["indexRange"],2,false);if(range.size()!=2)invalid("Draw index range is invalid");
            const auto begin=integer(range[0],meshes_.at(mesh).indexCount,true),end=integer(range[1],meshes_.at(mesh).indexCount,true);
            if(end<begin||(end-begin)%3)invalid("Draw index range is outside its geometry");
        }
    }
    return frame;
}
BinaryData Package::loadUniformPayload(const UniformDescriptor& uniform) const {
    if(uniform.byteCount==0||uniform.byteCount>65536||uniform.data.bytes!=uniform.byteCount)invalid("Uniform payload descriptor is invalid");
    return read(uniform.data);
}
Json Package::loadAnimation() const {
    const auto blob=read(animation_);auto document=parse(blob.storage,maximumAnimationJSONBytes);object(document);finiteTree(document);object(document["library"]);
    for(const auto& clip:array(document["library"]["clips"],4096)) {object(clip);(void)string(clip["id"]);}
    return document;
}
BinaryData Package::loadShaderAsset(std::string_view file) const {const auto found=shaderAssets_.find(file);if(found==shaderAssets_.end())throw Error(ErrorCode::missing,"Shader asset is not in this package");return read(found->second);}
BinaryData Package::loadNativeRaster(std::string_view file) const {const auto found=nativeRasters_.find(file);if(found==nativeRasters_.end())throw Error(ErrorCode::missing,"Native raster is not in this package");return read(found->second);}
std::vector<std::string> Package::requiredPixelFormats() const {std::set<std::string> unique;for(const auto& [id,texture]:textures_){(void)id;unique.insert(texture.pixelFormat);}return {unique.begin(),unique.end()};}
std::vector<std::string> Package::requiredShaderKeys() const {std::set<std::string> unique;for(const auto& [id,material]:materials_){(void)id;for(const auto& pass:material["passes"].array())unique.insert(pass["shaderKey"].string());}return {unique.begin(),unique.end()};}
} // namespace endfield::core::packet
