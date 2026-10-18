#include "core/source_watch_frame.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace endfield::core::source {
namespace {
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
double number(const Json&j,double fallback=0){const auto x=j.isNumber()?j.number():fallback;need(std::isfinite(x),"Nonfinite frame metadata");return x;}
bool flag(const Json&j,bool fallback=false){return j.isBool()?j.boolean():j.isNumber()?j.number()!=0:fallback;}
float f32(double x){need(std::isfinite(x)&&std::isfinite(static_cast<float>(x)),"Frame value exceeds Float range");return static_cast<float>(x);}
std::string text(const Json&j){need(j.isString(),"Missing frame string");return j.string();}
std::optional<std::string> target(const Json&j){return j["target_id"].isString()?std::optional(j["target_id"].string()):std::nullopt;}
template<class T,std::size_t N>std::array<T,N> array(const Json&j){need(j.isArray()&&j.array().size()==N,"Invalid frame vector width");std::array<T,N>a{};for(unsigned i=0;i<N;++i){const auto x=number(j.array()[i]);if constexpr(std::is_same_v<T,float>)a[i]=f32(x);else a[i]=x;}return a;}
std::array<double,4> xyzw(const Json&j){return {number(j["x"]),number(j["y"]),number(j["z"]),number(j["w"])};}
SourceFloat4 rgba(const Json&j){return {f32(number(j["r"],1)),f32(number(j["g"],1)),f32(number(j["b"],1)),f32(number(j["a"],1))};}
SourceSprite sprite(const Json&j){SourceSprite s;s.size=array<double,2>(j["size"]);s.padding=array<double,4>(j["padding"]);s.border=array<double,4>(j["border"]);s.outer=array<double,4>(j["outer"]);s.inner=array<double,4>(j["inner"]);s.pixelsPerUnit=number(j["pixelsPerUnit"]);s.textureID=text(j["textureID"]);return s;}
using V4=std::array<double,4>;
V4 transform(const Matrix4&m,const V4&v){V4 out{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out[r]+=m.values[c*4+r]*v[c];return out;}
std::optional<Matrix4> inverse(const Matrix4&m){
    if(!m.finite())return {};double a[4][8]{},determinant=1;for(unsigned r=0;r<4;++r){for(unsigned c=0;c<4;++c)a[r][c]=m.values[c*4+r];a[r][r+4]=1;}
    for(unsigned c=0;c<4;++c){unsigned p=c;for(unsigned r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[p][c]))p=r;if(a[p][c]==0)return {};
        if(p!=c){for(unsigned k=0;k<8;++k)std::swap(a[c][k],a[p][k]);determinant=-determinant;}const auto d=a[c][c];determinant*=d;for(auto&v:a[c])v/=d;
        for(unsigned r=0;r<4;++r)if(r!=c){const auto factor=a[r][c];for(unsigned k=0;k<8;++k)a[r][k]-=factor*a[c][k];}}
    if(!std::isfinite(determinant)||determinant==0)return {};Matrix4 out;for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)out.values[c*4+r]=a[r][c+4];return out.finite()?std::optional(out):std::nullopt;
}
Matrix4 quantized(Matrix4 m){for(auto&v:m.values)v=f32(v);return m;}
std::vector<float> flatten(const Matrix4&m){std::vector<float>v;v.reserve(16);for(const auto x:m.values)v.push_back(f32(x));return v;}
float gamma(float x,bool material=false){if(x<=.04045f)return x/12.92f;if(material&&x>=1)return std::pow(x,2.2f);return std::pow((x+.055f)/1.055f,2.4f);}
float color32(float x){const auto scaled=std::clamp(x,0.f,1.f)*255.f;const auto low=std::floor(scaled),part=scaled-low;return (part>.5f||(part==.5f&&static_cast<int>(low)%2)?low+1:low)/255.f;}
const TransformOverride* overridden(const Pose&p,const std::string&id){const auto i=p.transforms.find(id);return i==p.transforms.end()?nullptr:&i->second;}
}
std::optional<std::string_view> SourceWatchFrame::buttonAt(Point point,const Matrix4&vp,Rect viewport)const noexcept {
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(viewport.x)||!std::isfinite(viewport.y)||!std::isfinite(viewport.width)||!std::isfinite(viewport.height)||viewport.width<=0||viewport.height<=0||!viewport.contains(point))return {};
    auto intersects=[&](const SourceRect&rect,const Matrix4&world){const auto inv=inverse(vp*world);if(!inv)return false;
        const auto x=2*(point.x-viewport.x)/viewport.width-1,y=1-2*(point.y-viewport.y)/viewport.height;auto near=transform(*inv,{x,y,0,1}),far=transform(*inv,{x,y,1,1});
        auto finite=[](const V4&v){return std::all_of(v.begin(),v.end(),[](auto a){return std::isfinite(a);});};if(!finite(near)||!finite(far)||near[3]==0||far[3]==0)return false;
        Vec3 direction{};for(unsigned i=0;i<3;++i){near[i]/=near[3];far[i]/=far[3];direction[i]=far[i]-near[i];}if(!std::isfinite(direction[2])||std::abs(direction[2])<=1e-14)return false;
        const auto fraction=-near[2]/direction[2];if(!std::isfinite(fraction)||fraction< -1e-10||fraction>1+1e-10)return false;V4 local{0,0,0,1};for(unsigned i=0;i<3;++i)local[i]=near[i]+fraction*direction[i];
        if(!finite(local)||!rect.contains({local[0],local[1]}))return false;const auto clip=transform(vp,transform(world,local));return finite(clip)&&clip[3]>0&&clip[2]/clip[3]>=-1e-10&&clip[2]/clip[3]<=1+1e-10;
    };
    for(auto i=hits.rbegin();i!=hits.rend();++i)if(intersects(i->rect,i->world)&&std::all_of(i->masks.begin(),i->masks.end(),[&](const auto&m){return intersects(m.rect,m.world);}))return i->buttonID;
    return {};
}
SourceDesktopFrameSettings SourceDesktopFrameSettings::fromJson(const Json&j){
    SourceDesktopFrameSettings s;for(const auto&v:j["hiddenNodes"].array())s.hiddenNodes.insert(text(v));for(const auto&v:j["normalMaterialNodes"].array())s.normalMaterialNodes.insert(text(v));
    for(const auto&[id,values]:j["properties"].object())for(const auto&[name,v]:values.object())s.properties[id][name]=number(v);
    for(const auto&[id,v]:j["sprites"].object())s.sprites[id]=text(v);
    for(const auto&[id,v]:j["images"].object()){SourceDesktopImage image{text(v["texture"]),array<float,2>(v["size"]),{}};if(!v["displaySize"].isNull())image.displaySize=array<double,2>(v["displaySize"]);s.images[id]=std::move(image);}
    for(const auto&[id,v]:j["graphicStyles"].object()){SourceDesktopGraphicStyle style;style.opacity=f32(number(v["opacity"],1));if(!v["tint"].isNull())style.tint=array<float,3>(v["tint"]);s.graphicStyles[id]=style;}return s;
}
SourceWatchFrameResources SourceWatchFrameResources::fromJson(const Json&j){
    need(j.isObject(),"Missing source frame builder metadata");SourceWatchFrameResources r;
    for(const auto&[id,v]:j["sprites"].object())r.sprites.emplace(id,sprite(v));for(const auto&[id,v]:j["sourceSprites"].object())r.sourceSprites.emplace(id,sprite(v));
    for(const auto&[id,v]:j["textureSizes"].object())r.textureSizes[id]=array<float,2>(v);
    for(const auto&[id,v]:j["materials"].object()){if(v["name"].isString())r.namedMaterials.insert(id);const auto&saved=v["data"]["m_SavedProperties"];auto&values=r.materialVectors[id];
        for(const auto&pair:saved["m_Colors"].array())if(pair.array().size()==2&&pair.array()[0].isString()){const auto color=rgba(pair.array()[1]);values[pair.array()[0].string()]={color.begin(),color.end()};}
        for(const auto&pair:saved["m_TexEnvs"].array())if(pair.array().size()==2&&pair.array()[0].isString()){const auto&env=pair.array()[1];values[pair.array()[0].string()+"_ST"]={f32(number(env["m_Scale"]["x"],1)),f32(number(env["m_Scale"]["y"],1)),f32(number(env["m_Offset"]["x"])),f32(number(env["m_Offset"]["y"]))};}}
    for(const auto&[base,v]:j["materialVariants"].object())for(unsigned clip=0;clip<2;++clip)for(unsigned soft=0;soft<2;++soft){const auto key=std::to_string(clip)+std::to_string(soft);if(v[key].isString())r.materialVariants[base][clip*2+soft]=v[key].string();}
    for(const auto&[id,v]:j["materialPropertyTypes"].object())for(const auto&[name,p]:v.object())r.materialPropertyTypes[id][name]={static_cast<int>(number(p["type"])),static_cast<int>(number(p["flags"]))};
    for(const auto&[id,v]:j["sourceMeshNames"].object())r.sourceMeshNames[id]=text(v);for(const auto&v:j["profileNodeIDs"].array())r.profileNodeIDs.insert(text(v));
    if(!j["ambientRotationNodes"].isNull())for(const auto&v:j["ambientRotationNodes"].array())r.ambientRotationNodes.insert(text(v));
    for(const auto&[id,v]:j["defaultSelectableTints"].object())r.defaultSelectableTints[id]=array<float,4>(v);return r;
}
struct SourceWatchFrameBuilder::Impl {
    struct Animation {std::optional<std::string> material;Vec2 scale{1,1};double alpha{};};
    struct Graphic {
        std::string id;std::size_t node{};bool nonDrawing{},raw{},raycast{},unresolvedSprite{};
        std::string dynamicPath;std::optional<std::string> material,texture;SourceFloat4 color{1,1,1,1};std::array<double,4> padding{},uv{0,0,1,1};
        SourceImageParameters image;const SourceSprite*sprite{};std::optional<Animation> animation;
        ImageGeometryBuilder generator;SourceImageMesh rawMesh;std::optional<SourceRect> rawRect;
        SourceFrameGeometry geometry;std::optional<Matrix4> bakedMatrix;std::uint64_t bakedLocalRevision{},rawRevision{};bool bakedDesktopUV{};std::optional<SourceRect> bakedRect;
    };
    struct Mesh {std::string source;std::vector<std::string> materials;std::int64_t order{};};
    struct NodePlan {std::vector<std::size_t> graphics;std::optional<Mesh> mesh;bool gamma{},maskable{};};
    struct SoftPlan {std::size_t node{},outer{};const SourceSprite*sprite{};std::string texture;std::array<float,2> textureSize{};std::array<float,4> textureST{};bool sliced{},pixelPerfect{};double ppu{};std::string error;};
    struct SoftMask {Matrix4 worldToUnit;std::string texture;std::array<float,4> st{},inner{},innerUV{};bool sliced{};};
    const SceneDefinition*scene;const MountedLayoutDocument*document;const SourceWatchFrameResources*resources;
    SourceDesktopFrameSettings settings;SourceLayout layout;WatchLayout writer;IncrementalResolver resolver;SourceCanvasPlan canvas;
    std::vector<NodePlan> nodes;std::vector<Graphic> graphics;std::vector<SoftPlan> softPlans;std::size_t cut{};
    SourceWatchFrame frame;SourceWatchFrameStats counts;std::optional<Pose> previousInput,layoutInput;Pose layoutPose,beforeSlant;std::vector<ResolvedNode> beforeSlantResolved;std::vector<unsigned char> slantWritten;Matrix4 previousRoot,layoutRoot;
    double previousScroll{},layoutScroll{};std::optional<std::size_t> previousEntries,layoutEntries;SourceFrameTints previousTints;
    std::vector<std::optional<Matrix4>> canvasInverse;std::vector<std::optional<SoftMask>> softMasks;
    struct AmbientNode {std::size_t index{};std::optional<std::size_t> parent;bool root{};};
    struct AmbientBatch {std::size_t batch{},node{},canvas{};std::optional<std::size_t> graphic;};
    std::vector<AmbientNode> ambientTraversal;
    std::vector<std::size_t> ambientSlot,ambientRoots;
    std::vector<ResolvedNode> presentationResolved,ambientStaged;
    std::vector<std::optional<Quaternion>> currentAmbientRotations;
    std::vector<AmbientBatch> ambientBatches;
    bool ambientSupported{},slantIndependent{true};std::uint64_t revision{};
    static constexpr std::size_t noAmbient=std::numeric_limits<std::size_t>::max();
    struct Order {std::int64_t order{};std::size_t sequence{},index{};};std::vector<Order> batchOrder,hitOrder;
    Impl(const SceneDefinition&s,const MountedLayoutDocument&d,const SourceWatchFrameResources&r,SourceDesktopFrameSettings config)
      :scene(&s),document(&d),resources(&r),settings(std::move(config)),layout(s),writer(s,d),resolver(s),
       canvas(s,d,static_cast<std::int64_t>(d.component("Canvas",s.rootID())?number((*d.component("Canvas",s.rootID()))["m_SortingOrder"]):0)),nodes(s.nodes().size()),canvasInverse(s.nodes().size()),softMasks(s.nodes().size()) {
        ambientSlot.resize(s.nodes().size(),noAmbient);slantWritten.resize(s.nodes().size());
        for(const auto&id:r.ambientRotationNodes)need(layout.nodeIndex(id).has_value(),"Unknown source ambient rotation node");
        for(const auto index:layout.traversalIndices()){
            const auto&node=s.nodes()[index];const auto parent=node.parent?layout.nodeIndex(*node.parent):std::nullopt;
            const bool isRoot=r.ambientRotationNodes.contains(node.id);
            if(isRoot||(parent&&ambientSlot[*parent]!=noAmbient)){
                ambientSlot[index]=ambientTraversal.size();ambientTraversal.push_back({index,parent,isRoot});
                if(isRoot)ambientRoots.push_back(index);
            }
        }
        ambientStaged.resize(ambientTraversal.size());currentAmbientRotations.resize(ambientRoots.size());
        presentationResolved.resize(s.nodes().size());ambientBatches.reserve(ambientTraversal.size());
        for(const auto index:layout.traversalIndices())if(const auto*effect=d.component("UIScrollCellSlantEffect",s.nodes()[index].id)){
            if(ambientSlot[index]!=noAmbient)slantIndependent=false;
            for(const auto&cell:(*effect)["_cells"].array())if(const auto id=target(cell))if(const auto child=layout.nodeIndex(*id);child&&ambientSlot[*child]!=noAmbient)slantIndependent=false;
        }
        std::size_t cuts=0;
        for(const auto index:layout.traversalIndices()){
            const auto&node=s.nodes()[index];auto&plan=nodes[index];plan.gamma=d.component("Canvas",node.id)&&flag((*d.component("Canvas",node.id))["m_VertexColorAlwaysGammaSpace"]);plan.maskable=d.component("UISoftMaskable",node.id)!=nullptr;
            if(d.component("UIWatchPanelCut",node.id)){cut=index;++cuts;}
            const auto records=d.components.find(node.id);if(records!=d.components.end())for(const auto&c:records->second)if(c.enabled()){
                const auto kind=c.kind();if(kind!="NonDrawingGraphic"&&kind!="UIImage"&&kind!="Image"&&kind!="UIRawImage"&&kind!="RawImage")continue;
                Graphic g;g.id=c.id;g.node=index;g.nonDrawing=kind=="NonDrawingGraphic";g.raw=kind=="UIRawImage"||kind=="RawImage";g.raycast=flag(c["m_RaycastTarget"]);g.padding=xyzw(c["m_RaycastPadding"]);g.color=rgba(c["m_Color"]);g.material=target(c["m_Material"]);g.texture=target(c["m_Texture"]);
                g.dynamicPath=c["imgRefPath"].isString()?c["imgRefPath"].string():std::string{};g.unresolvedSprite=target(c["m_Sprite"]).has_value()||!g.dynamicPath.empty();g.image=SourceImageParameters::fromComponent(c);
                if(const auto i=r.sprites.find(c.id);i!=r.sprites.end())g.sprite=&i->second;
                g.uv={number(c["m_UVRect"]["x"]),number(c["m_UVRect"]["y"]),number(c["m_UVRect"]["width"],1),number(c["m_UVRect"]["height"],1)};
                if(const auto*a=d.component("UIGraphicAnimation",node.id))g.animation=Animation{target((*a)["_material"]),{number((*a)["_scale"]["x"],1),number((*a)["_scale"]["y"],1)},number((*a)["_alpha"])};
                plan.graphics.push_back(graphics.size());graphics.push_back(std::move(g));
            }
            if(const auto*filter=d.component("MeshFilter",node.id)){const auto source=target((*filter)["m_Mesh"]);const auto*render=d.component("MeshRenderer",node.id);const auto name=source?r.sourceMeshNames.find(*source):r.sourceMeshNames.end();
                if(render&&name!=r.sourceMeshNames.end()){Mesh m;m.source=name->second;m.order=static_cast<std::int64_t>(number((*render)["m_SortingOrder"]));
                    for(const auto&c:records->second)if(c.kind()=="UISortingOrder"&&number(c["_renderType"],-1)==0){if(c["_sortingOrderOffset"].isNumber())m.order=static_cast<std::int64_t>(number(c["_sortingOrderOffset"]));break;}
                    for(const auto&v:(*render)["m_Materials"].array())if(const auto id=target(v);id&&r.namedMaterials.contains(*id))m.materials.push_back(*id);plan.mesh=std::move(m);}}
            if(d.component("UISoftMask",node.id))softPlans.push_back(compileSoft(index));
        }
        need(cuts==1,"Unresolved original UIWatchPanelCut binding");frame.batches.reserve(graphics.size()+16);frame.hits.reserve(graphics.size());batchOrder.reserve(graphics.size()+16);hitOrder.reserve(graphics.size());
    }
    SoftPlan compileSoft(std::size_t index){
        SoftPlan p;p.node=index;const auto&node=scene->nodes()[index];const auto&records=document->components.at(node.id);const WatchComponent*image{};for(const auto&c:records)if(c.kind()=="UIImage"){image=&c;break;}
        if(!image){p.error="Missing original UIImage/Sprite soft mask binding";return p;}
        const auto raw=document->spriteByComponent.find(image->id);if(raw==document->spriteByComponent.end()||!raw->second["texture"]["id"].isString()){p.error="Missing original UIImage/Sprite soft mask binding";return p;}
        p.texture=raw->second["texture"]["id"].string();const auto size=resources->textureSizes.find(p.texture);if(size==resources->textureSizes.end()){p.error="Missing original UIImage/Sprite soft mask binding";return p;}p.textureSize=size->second;
        const auto&rect=raw->second["raw_sprite"]["m_Rect"];p.textureST={f32(number(rect["width"]))/p.textureSize[0],f32(number(rect["height"]))/p.textureSize[1],f32(number(rect["x"]))/p.textureSize[0],f32(number(rect["y"]))/p.textureSize[1]};
        std::optional<std::size_t>outer;double reference=100;bool foundPPU=false;for(std::optional<std::string>id=node.id;id;id=scene->node(*id)->parent){
            if(document->component("Canvas",*id))outer=layout.nodeIndex(*id);if(!foundPPU)if(const auto*scaler=document->component("CanvasScaler",*id)){reference=number((*scaler)["m_ReferencePixelsPerUnit"],100);foundPPU=true;}}
        if(!outer){p.error="Unresolved original outer Canvas for soft mask";return p;}p.outer=*outer;p.pixelPerfect=flag((*document->component("Canvas",scene->nodes()[*outer].id))["m_PixelPerfect"]);
        const auto sp=resources->sprites.find(image->id);if(sp!=resources->sprites.end())p.sprite=&sp->second;p.sliced=number((*image)["m_Type"])==1&&p.sprite&&p.sprite->border!=std::array<double,4>{};if(p.sprite)p.ppu=p.sprite->pixelsPerUnit/reference*number((*image)["m_PixelsPerUnitMultiplier"],1);return p;
    }
    SoftMask makeSoft(const SoftPlan&p,std::span<const ResolvedNode>resolved,const Matrix4&root){
        need(p.error.empty(),p.error.c_str());const auto&node=resolved[p.node];need(node.rect.has_value(),"Missing original UIImage/Sprite soft mask binding");const auto&rect=*node.rect;
        const auto outer=inverse(root*resolved[p.outer].worldMatrix);need(outer.has_value(),"Unresolved original outer Canvas for soft mask");const auto toOuter=*outer*(root*node.worldMatrix);std::array<V4,4>corners;const auto source=rect.corners();
        for(unsigned i=0;i<4;++i){const auto&v=source[std::array<unsigned,4>{0,3,2,1}[i]];corners[i]=transform(toOuter,{v[0],v[1],v[2],1});}
        V4 x{},y{};for(unsigned i=0;i<4;++i){x[i]=corners[3][i]-corners[0][i];y[i]=corners[1][i]-corners[0][i];}const Vec3 z{x[1]*y[2]-x[2]*y[1],x[2]*y[0]-x[0]*y[2],x[0]*y[1]-x[1]*y[0]};Matrix4 basis;
        for(unsigned i=0;i<4;++i){basis.values[i]=i<3?x[i]:0;basis.values[4+i]=i<3?y[i]:0;basis.values[8+i]=i<3?z[i]:0;basis.values[12+i]=corners[0][i];}
        const auto inv=inverse(basis);need(inv&&p.textureSize[0]>0&&p.textureSize[1]>0,"Degenerate original soft mask geometry");SoftMask out{*inv**outer,p.texture,p.textureST,{},{},p.sliced};
        if(p.sliced){need(!p.pixelPerfect,"Unverified pixel-adjusted source soft mask");need(p.ppu>0,"Invalid original sliced mask pixelsPerUnit");auto border=p.sprite->border;for(auto&v:border)v/=p.ppu;
            for(unsigned i=0;i<2;++i){const auto sum=border[i]+border[i+2];if(sum>rect.size[i]&&sum!=0){const auto ratio=rect.size[i]/sum;border[i]*=ratio;border[i+2]*=ratio;}}
            const auto min=transform(toOuter,{rect.origin[0]+border[0],rect.origin[1]+border[1],0,1}),max=transform(toOuter,{rect.origin[0]+rect.size[0]-border[2],rect.origin[1]+rect.size[1]-border[3],0,1});
            const auto width=std::sqrt(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]),height=std::sqrt(y[0]*y[0]+y[1]*y[1]+y[2]*y[2]);out.inner={f32((min[0]-corners[0][0])/width),f32((min[1]-corners[0][1])/height),f32((max[0]-corners[0][0])/width),f32((max[1]-corners[0][1])/height)};
            const Vec2 uvSize{p.sprite->outer[2]-p.sprite->outer[0],p.sprite->outer[3]-p.sprite->outer[1]};const auto tiny=double(std::numeric_limits<float>::denorm_min()*8);for(unsigned i=0;i<4;++i)out.innerUV[i]=f32(std::abs(uvSize[0])<tiny||std::abs(uvSize[1])<tiny?p.sprite->inner[i]:(p.sprite->inner[i]-p.sprite->outer[i%2])/uvSize[i%2]);
        }return out;
    }
    void materialProperties(const Pose&pose,const std::string&id,const std::optional<std::string>&materialID,SourceWatchBatch&batch){
        const auto properties=pose.properties.find(id);if(properties==pose.properties.end())return;std::set<std::string,std::less<>> animated;
        const auto base=materialID?resources->materialVectors.find(*materialID):resources->materialVectors.end();
        for(const auto&[attribute,value]:properties->second)if(attribute.starts_with("material.")){
            const auto name=attribute.substr(9);const auto dot=name.find('.');const auto suffix=dot==std::string::npos?std::string{}:name.substr(dot+1);int axis=-1;
            if(suffix.size()==1){const auto p=std::string_view("xyzw").find(suffix[0]),q=std::string_view("rgba").find(suffix[0]);if(p!=std::string_view::npos)axis=static_cast<int>(p);else if(q!=std::string_view::npos)axis=static_cast<int>(q);}
            if(dot!=std::string::npos&&axis>=0){const auto key=name.substr(0,dot);auto i=batch.uniformOverrides.find(key);if(i==batch.uniformOverrides.end()){
                    auto v=base!=resources->materialVectors.end()?base->second.find(key):SourceFrameUniforms::const_iterator{};batch.uniformOverrides[key]=base!=resources->materialVectors.end()&&v!=base->second.end()?v->second:std::vector<float>(4,0);i=batch.uniformOverrides.find(key);}
                need(i->second.size()>static_cast<unsigned>(axis),"Source material channel exceeds saved vector");i->second[static_cast<unsigned>(axis)]=f32(value);animated.insert(key);
            }else{batch.uniformOverrides[name]={f32(value)};animated.insert(name);}}
        const auto types=resources->materialPropertyTypes.find(batch.material);if(types==resources->materialPropertyTypes.end())return;
        for(const auto&name:animated){const auto kind=types->second.find(name);if(kind==types->second.end()||(kind->second.type!=0&&(kind->second.flags&32)==0))continue;
            auto&values=batch.uniformOverrides.at(name);const auto count=std::min<std::size_t>(kind->second.type==2||kind->second.type==3?1:3,values.size());for(std::size_t i=0;i<count;++i)values[i]=gamma(values[i],true);}
    }
    void hit(const Graphic&g,const SourceRect&rect,const Matrix4&world,std::span<const ResolvedNode>resolved,const Matrix4&root,std::int64_t order,std::size_t sequence){
        const auto&a=canvas.ancestry()[g.node];if(!g.raycast||!a.button||!a.acceptsInput)return;const auto&p=g.padding;
        SourceWatchHit h{scene->nodes()[g.node].id,scene->nodes()[*a.button].id,{{rect.origin[0]+p[0],rect.origin[1]+p[1]},{rect.size[0]-p[0]-p[2],rect.size[1]-p[1]-p[3]}},world,{}};
        for(const auto mask:a.rectMasks)if(resolved[mask].rect)h.masks.push_back({*resolved[mask].rect,root*resolved[mask].worldMatrix});hitOrder.push_back({order,sequence,frame.hits.size()});frame.hits.push_back(std::move(h));
    }
    const SourceImageMesh&local(Graphic&g,const SourceSprite*sp,const SourceRect&rect,Vec2 pivot,std::optional<double>fill,bool force){
        if(g.raw){if(!force&&g.rawRect==rect){++counts.localImageReuses;return g.rawMesh;}g.rawRect=rect;g.rawMesh.reset();const auto&o=rect.origin;const auto&s=rect.size;const auto&u=g.uv;
            g.rawMesh.positions={{f32(o[0]),f32(o[1]),0,1},{f32(o[0]),f32(o[1]+s[1]),0,1},{f32(o[0]+s[0]),f32(o[1]+s[1]),0,1},{f32(o[0]+s[0]),f32(o[1]),0,1}};
            g.rawMesh.uv={{f32(u[0]),f32(u[1])},{f32(u[0]),f32(u[1]+u[3])},{f32(u[0]+u[2]),f32(u[1]+u[3])},{f32(u[0]+u[2]),f32(u[1])}};g.rawMesh.indices={0,1,2,2,3,0};++counts.localImageBuilds;++g.rawRevision;return g.rawMesh;}
        if(force)g.generator.reset();const auto before=g.generator.stats();const auto&mesh=g.generator.build(g.image,sp,rect,pivot,100,fill);const auto after=g.generator.stats();counts.localImageBuilds+=after.builds-before.builds;counts.localImageReuses+=after.reuses-before.reuses;return mesh;
    }
    void baked(Graphic&g,const SourceImageMesh&mesh,const SourceRect&rect,const Matrix4&toCanvas,bool desktopUV,bool force){
        // Generator counts distinguish topology/trim changes from a world-only
        // presentation. RawImage's rect is its complete local mesh key.
        const auto localRevision=g.raw?g.rawRevision:g.generator.stats().builds;
        if(!force&&g.bakedMatrix==toCanvas&&g.bakedLocalRevision==localRevision&&g.bakedDesktopUV==desktopUV&&g.bakedRect==rect)return;
        auto&out=g.geometry.mesh;out.positions.resize(mesh.positions.size());
        for(std::size_t i=0;i<mesh.positions.size();++i){const auto&p=mesh.positions[i];const auto transformed=transform(toCanvas,{p[0],p[1],p[2],p[3]});for(unsigned c=0;c<4;++c)out.positions[i][c]=f32(transformed[c]);}
        out.uv=mesh.uv;if(desktopUV)for(std::size_t i=0;i<mesh.positions.size();++i)out.uv[i]={(mesh.positions[i][0]-f32(rect.origin[0]))/f32(rect.size[0]),(mesh.positions[i][1]-f32(rect.origin[1]))/f32(rect.size[1])};out.indices=mesh.indices;
        g.bakedMatrix=toCanvas;g.bakedLocalRevision=localRevision;g.bakedDesktopUV=desktopUV;g.bakedRect=rect;++g.geometry.revision;++counts.bakedGeometryBuilds;
    }
    void style(const std::string&id,SourceFloat4&color){if(const auto s=settings.graphicStyles.find(id);s!=settings.graphicStyles.end()){if(s->second.tint)std::copy(s->second.tint->begin(),s->second.tint->end(),color.begin());color[3]*=s->second.opacity;}}
    void append(SourceWatchBatch b,std::int64_t order,std::size_t sequence){batchOrder.push_back({order,sequence,frame.batches.size()});frame.batches.push_back(std::move(b));}
    static bool sameWithoutRotation(const TransformOverride&a,const TransformOverride&b){
        return a.localPosition==b.localPosition&&a.localScale==b.localScale&&a.anchoredPosition3D==b.anchoredPosition3D&&
            a.sizeDelta==b.sizeDelta&&a.anchorMin==b.anchorMin&&a.anchorMax==b.anchorMax&&a.pivot==b.pivot&&a.active==b.active&&a.positionComponents==b.positionComponents;
    }
    static bool rotationOnly(const TransformOverride&v){
        return !v.localPosition&&!v.localScale&&!v.anchoredPosition3D&&!v.sizeDelta&&!v.anchorMin&&!v.anchorMax&&!v.pivot&&!v.active&&v.positionComponents.empty();
    }
    bool sameStaticInput(const Pose&input)const{
        if(!previousInput||input.properties!=previousInput->properties||input.unboundPaths!=previousInput->unboundPaths||input.unregisteredBindings!=previousInput->unregisteredBindings)return false;
        auto a=input.transforms.begin(),b=previousInput->transforms.begin();
        while(a!=input.transforms.end()||b!=previousInput->transforms.end()){
            if(b==previousInput->transforms.end()||(a!=input.transforms.end()&&a->first<b->first)){
                if(!resources->ambientRotationNodes.contains(a->first)||!rotationOnly(a->second))return false;++a;
            }else if(a==input.transforms.end()||b->first<a->first){
                if(!resources->ambientRotationNodes.contains(b->first)||!rotationOnly(b->second))return false;++b;
            }else{if(resources->ambientRotationNodes.contains(a->first)?!sameWithoutRotation(a->second,b->second):a->second!=b->second)return false;++a;++b;}
        }return true;
    }
    bool sameAmbientRotations(const Overrides&rotations)const{
        for(std::size_t i=0;i<ambientRoots.size();++i){const auto it=rotations.find(scene->nodes()[ambientRoots[i]].id);
            const auto rotation=it==rotations.end()?std::nullopt:it->second.localRotation;if(rotation!=currentAmbientRotations[i])return false;
        }return true;
    }
    void prepareAmbient(const Pose&input){
        ambientSupported=!ambientRoots.empty()&&slantIndependent&&ambientSlot[cut]==noAmbient;ambientBatches.clear();
        for(const auto&hit:frame.hits)if(ambientSlot[*layout.nodeIndex(hit.graphicID)]!=noAmbient)ambientSupported=false;
        for(std::size_t i=0;i<frame.batches.size();++i){const auto&b=frame.batches[i];const auto node=*layout.nodeIndex(b.sourceNodeID);if(ambientSlot[node]==noAmbient)continue;
            const auto&ancestry=canvas.ancestry()[node];if(!ancestry.rectMasks.empty()||(nodes[node].maskable&&ancestry.softMask))ambientSupported=false;
            AmbientBatch moving{i,node,*canvas.sorting()[node].nearestCanvas,{}};
            if(b.geometry){for(const auto gi:nodes[node].graphics)if(&graphics[gi].geometry==b.geometry)moving.graphic=gi;need(moving.graphic.has_value(),"Missing ambient source graphic");}
            ambientBatches.push_back(moving);
        }
        // Seed optional entries only in the retained writer poses, once per
        // complete frame. Rotation-only updates never allocate map sentinels.
        for(std::size_t i=0;i<ambientRoots.size();++i){const auto&id=scene->nodes()[ambientRoots[i]].id;const auto value=input.transforms.find(id);
            currentAmbientRotations[i]=value==input.transforms.end()?std::nullopt:value->second.localRotation;
            layoutPose.transforms[id].localRotation=currentAmbientRotations[i];beforeSlant.transforms[id].localRotation=currentAmbientRotations[i];
        }
    }
    const SourceWatchFrame&ambientFrame(const Overrides&rotations,bool direct){
        // Resolve only the compiled source traversal. Descendants preserve their
        // authored local matrices; roots replace rotation around the already
        // laid-out translation and source/overridden scale, exactly as Swift.
        for(std::size_t i=0;i<ambientTraversal.size();++i){const auto&entry=ambientTraversal[i];auto next=presentationResolved[entry.index];
            if(entry.root){const auto&node=*next.node;const auto o=rotations.find(node.id);const auto q=o!=rotations.end()&&o->second.localRotation?*o->second.localRotation:node.rotation;
                const auto rotation=quaternionMatrix(q);need(rotation.has_value(),"Invalid source ambient quaternion");
                const auto&base=layoutPose.transforms.at(node.id);const auto scale=base.localScale.value_or(node.scale);
                next.localMatrix=Matrix4::translation(next.localMatrix.values[12],next.localMatrix.values[13],next.localMatrix.values[14])**rotation*Matrix4::scale(scale[0],scale[1],scale[2]);
            }
            const auto parent=entry.parent?(ambientSlot[*entry.parent]!=noAmbient?ambientStaged[ambientSlot[*entry.parent]].worldMatrix:presentationResolved[*entry.parent].worldMatrix):Matrix4{};
            next.worldMatrix=parent*next.localMatrix;need(next.worldMatrix.finite(),"Nonfinite source ambient world");ambientStaged[i]=next;
        }
        for(std::size_t i=0;i<ambientTraversal.size();++i)presentationResolved[ambientTraversal[i].index]=ambientStaged[i];
        for(const auto&entry:ambientBatches){auto&b=frame.batches[entry.batch];const auto&node=presentationResolved[entry.node];const auto&canvasNode=presentationResolved[entry.canvas];
            b.world=quantized(previousRoot*(entry.graphic?canvasNode.worldMatrix:node.worldMatrix));
            if(entry.graphic){auto&g=graphics[*entry.graphic];const auto inv=inverse(canvasNode.worldMatrix);need(inv.has_value(),"Singular retained ambient Canvas");
                const auto&mesh=g.raw?g.rawMesh:g.generator.mesh();baked(g,mesh,*node.rect,*inv*node.worldMatrix,settings.images.contains(b.sourceNodeID)&&!g.raw,false);}
        }
        for(std::size_t i=0;i<ambientRoots.size();++i){const auto&id=scene->nodes()[ambientRoots[i]].id;const auto value=rotations.find(id);currentAmbientRotations[i]=value==rotations.end()?std::nullopt:value->second.localRotation;
            layoutPose.transforms.at(id).localRotation=currentAmbientRotations[i];beforeSlant.transforms.at(id).localRotation=currentAmbientRotations[i];}
        beforeSlantResolved.clear();frame.resolved=presentationResolved;++counts.ambientFrames;if(direct)++counts.directAmbientFrames;
        counts.lastAmbientResolvedNodes=ambientTraversal.size();counts.ambientResolvedNodes+=ambientTraversal.size();return frame;
    }
    const SourceWatchFrame*settled(const Pose&ambient,std::uint64_t expected,const Matrix4&root,Vec2 size,double scroll,const DesktopNavigationLayout*navigation,const SourceFrameTints&tints){
        const auto entries=navigation?std::optional(navigation->entryCount()):std::nullopt;
        if(!previousInput||revision!=expected||!ambientSupported||previousRoot!=root||previousScroll!=scroll||previousEntries!=entries||previousTints!=tints||
            !ambient.properties.empty()||!ambient.unboundPaths.empty()||!ambient.unregisteredBindings.empty()||ambient.transforms.size()!=ambientRoots.size())return nullptr;
        const auto r=layoutPose.transforms.find(scene->rootID());if(r==layoutPose.transforms.end()||r->second.sizeDelta!=std::optional(size))return nullptr;
        for(const auto&[id,value]:ambient.transforms)if(!resources->ambientRotationNodes.contains(id)||!value.localRotation||!rotationOnly(value))return nullptr;
        try{return &ambientFrame(ambient.transforms,true);}catch(...){previousInput.reset();layoutInput.reset();ambientSupported=false;++revision;throw;}
    }
    const SourceWatchFrame&worldOnly(const Matrix4&root){
        // Same desktop presentation under a different gyro/root transform.
        // Reuse metrics, source topology, material maps and vertex buffers;
        // only the original slant writer may change Canvas-local positions.
        if(beforeSlantResolved.empty())beforeSlantResolved=layout.resolve({},beforeSlant.transforms);
        // Slant writes absolute local positions using beforeSlantResolved and
        // reads only anchors/pivot, which it never changes. Keep successful
        // cells and unrelated channels in-place rather than copying hundreds
        // of nested component maps. A skipped cell must recover its exact
        // baseline, including removal when no original override existed.
        std::fill(slantWritten.begin(),slantWritten.end(),static_cast<unsigned char>(0));
        writer.applySlant(layoutPose,root,ResolvedView{&layout,beforeSlantResolved},false,slantWritten);
        for(const auto&id:writer.slantRootIDs())if(const auto index=layout.nodeIndex(id);index&&!slantWritten[*index]){
            const auto baseline=beforeSlant.transforms.find(id),current=layoutPose.transforms.find(id);
            if(baseline==beforeSlant.transforms.end()){if(current!=layoutPose.transforms.end())layoutPose.transforms.erase(current);}
            else if(current==layoutPose.transforms.end())layoutPose.transforms.emplace(id,baseline->second);
            else if(current->second!=baseline->second)current->second=baseline->second;
        }
        const auto resolved=resolver.resolve({},layoutPose.transforms);const auto cutInverse=inverse(root*resolved[cut].worldMatrix);
        need(cutInverse.has_value(),"Unresolved original UIWatchPanelCut world matrix");
        for(const auto&p:softPlans)if(resolved[p.node].activeInHierarchy)softMasks[p.node]=makeSoft(p,resolved,root);
        std::fill(canvasInverse.begin(),canvasInverse.end(),std::nullopt);
        auto updateMatrix=[](std::vector<float>&out,const Matrix4&m){need(out.size()==16,"Retained source matrix width changed");for(unsigned i=0;i<16;++i)out[i]=f32(m.values[i]);};
        for(auto&b:frame.batches){const auto index=*layout.nodeIndex(b.sourceNodeID);const auto&node=resolved[index];const auto ci=*canvas.sorting()[index].nearestCanvas;
            if(!canvasInverse[ci])canvasInverse[ci]=inverse(resolved[ci].worldMatrix);need(canvasInverse[ci].has_value(),"Singular retained source Canvas");
            b.world=quantized(root*(b.geometry?resolved[ci].worldMatrix:node.worldMatrix));
            if(b.geometry){auto gi=std::find_if(nodes[index].graphics.begin(),nodes[index].graphics.end(),[&](auto i){return &graphics[i].geometry==b.geometry;});need(gi!=nodes[index].graphics.end(),"Retained source graphic is missing");auto&g=graphics[*gi];
                const auto&mesh=g.raw?g.rawMesh:g.generator.mesh();baked(g,mesh,*node.rect,*canvasInverse[ci]*node.worldMatrix,settings.images.contains(b.sourceNodeID)&&!g.raw,false);
                if(const auto clip=canvas.clip(index,resolved,*canvasInverse[ci])){auto&rectangle=b.uniformOverrides.at("clipRect");std::copy(clip->rectangle.begin(),clip->rectangle.end(),rectangle.begin());}
                if(nodes[index].maskable)if(const auto mask=canvas.ancestry()[index].softMask;mask&&softMasks[*mask]){const auto&soft=*softMasks[*mask];updateMatrix(b.uniformOverrides.at("_WorldToSoftMask"),soft.worldToUnit*b.world);std::copy(soft.inner.begin(),soft.inner.end(),b.uniformOverrides.at("_InnerSoftMask").begin());std::copy(soft.innerUV.begin(),soft.innerUV.end(),b.uniformOverrides.at("_InnerSoftMaskUV").begin());}
            }else {const auto uniform=b.uniformOverrides.find("_WatchWorldToLocalMatrix");need(uniform!=b.uniformOverrides.end(),"Retained source cut matrix is missing");updateMatrix(uniform->second,*cutInverse);}
        }
        for(auto&h:frame.hits){const auto index=*layout.nodeIndex(h.graphicID);h.world=root*resolved[index].worldMatrix;std::size_t m=0;for(const auto mask:canvas.ancestry()[index].rectMasks)if(resolved[mask].rect){need(m<h.masks.size(),"Retained hit masks changed");h.masks[m++].world=root*resolved[mask].worldMatrix;}need(m==h.masks.size(),"Retained hit mask count changed");}
        std::copy(resolved.begin(),resolved.end(),presentationResolved.begin());frame.resolved=presentationResolved;previousRoot=root;layoutRoot=root;++revision;++counts.worldOnlyFrames;return frame;
    }
    const SourceWatchFrame&build(const Pose&input,const Matrix4&root,double scroll,const DesktopNavigationLayout*navigation,const SourceFrameTints&tints,bool force){
        need(root.finite()&&std::isfinite(scroll),"Invalid source frame transform/scroll");++counts.builds;
        const auto entries=navigation?std::optional(navigation->entryCount()):std::nullopt;
        const bool staticMatch=!force&&sameStaticInput(input);
        const bool rotationsMatch=staticMatch&&sameAmbientRotations(input.transforms);
        const bool dependenciesMatch=previousScroll==scroll&&previousEntries==entries&&previousTints==tints;
        if(staticMatch&&rotationsMatch&&previousRoot==root&&dependenciesMatch){++counts.reusedFrames;return frame;}
        if(staticMatch&&rotationsMatch&&dependenciesMatch){
            // Keep the immutable snapshot in place on success. MSVC's node
            // containers allocate replacement sentinels even when moved; the
            // old move-out/move-back transaction therefore allocated per tick.
            // Failed updates still invalidate reuse before propagating error.
            try{return worldOnly(root);}
            catch(...){previousInput.reset();layoutInput.reset();throw;}
        }
        if(staticMatch&&previousRoot==root&&dependenciesMatch&&ambientSupported){
            try{return ambientFrame(input.transforms,false);}catch(...){previousInput.reset();layoutInput.reset();ambientSupported=false;++revision;throw;}
        }
        // Failed frames are not eligible for reuse. Local immutable mesh caches
        // can survive a failure; frame spans remain invalid until success.
        previousInput.reset();Pose pose=input;
        for(const auto&[id,properties]:settings.properties)for(const auto&[name,value]:properties)pose.properties[id][name]=value;
        for(const auto&id:settings.hiddenNodes)pose.transforms[id].active=false;
        for(const auto&[id,image]:settings.images)if(image.displaySize)pose.transforms[id].sizeDelta=*image.displaySize;
        const bool reuseLayout=!force&&layoutInput&&layoutInput->transforms==pose.transforms&&layoutRoot==root&&layoutScroll==scroll&&layoutEntries==entries;
        if(reuseLayout){auto properties=std::move(pose.properties);auto unbound=std::move(pose.unboundPaths),unregistered=std::move(pose.unregisteredBindings);pose=layoutPose;pose.properties=std::move(properties);pose.unboundPaths=std::move(unbound);pose.unregisteredBindings=std::move(unregistered);++counts.layoutReuses;}
        else{layoutInput=pose;frame.layoutReport=writer.apply(pose,scroll,root,navigation,{},[&](const Pose&value){beforeSlant=value;beforeSlantResolved.clear();},force);layoutPose=pose;layoutRoot=root;layoutScroll=scroll;layoutEntries=entries;++counts.layoutBuilds;}
        frame.resolved=resolver.resolve({},pose.transforms);canvas.updateAlpha(pose);frame.inheritedAlpha=canvas.inheritedAlpha();const auto resolved=frame.resolved;
        frame.batches.clear();frame.hits.clear();frame.diagnostics.clear();batchOrder.clear();hitOrder.clear();std::fill(canvasInverse.begin(),canvasInverse.end(),std::nullopt);std::fill(softMasks.begin(),softMasks.end(),std::nullopt);
        for(const auto&p:softPlans)if(resolved[p.node].activeInHierarchy){try{softMasks[p.node]=makeSoft(p,resolved,root);}catch(const std::exception&e){frame.diagnostics.push_back("Source soft mask "+scene->nodes()[p.node].path+": "+e.what());}}
        const auto cutInverse=inverse(root*resolved[cut].worldMatrix);need(cutInverse.has_value(),"Unresolved original UIWatchPanelCut world matrix");const auto watchToLocal=flatten(*cutInverse);
        std::size_t sequence=0;
        for(const auto index:layout.traversalIndices()){
            const auto&node=scene->nodes()[index];const auto&n=resolved[index];if(!n.activeInHierarchy)continue;const auto&sorting=canvas.sorting()[index];if(!sorting.nearestCanvas)continue;const auto ci=*sorting.nearestCanvas;
            if(!canvasInverse[ci])canvasInverse[ci]=inverse(resolved[ci].worldMatrix);if(!canvasInverse[ci])continue;
            const auto toCanvas=*canvasInverse[ci]*n.worldMatrix,world=root*n.worldMatrix,canvasWorld=root*resolved[ci].worldMatrix;const auto clip=canvas.clip(index,resolved,*canvasInverse[ci]);const auto&plan=nodes[index];
            for(const auto graphicIndex:plan.graphics){auto&g=graphics[graphicIndex];if(!n.rect)continue;const auto&rect=*n.rect;
                if(g.nonDrawing){const auto before=frame.hits.size();hit(g,rect,world,resolved,root,sorting.sortingOrder,sequence);if(frame.hits.size()!=before)++sequence;continue;}
                const auto replacement=settings.images.find(node.id);const auto explicitSprite=settings.sprites.find(node.id);const SourceSprite*sp=g.sprite;
                if(explicitSprite!=settings.sprites.end()){const auto i=resources->sourceSprites.find(explicitSprite->second);need(i!=resources->sourceSprites.end(),"Explicit widget Sprite missing");sp=&i->second;}
                if(!g.raw&&!sp&&g.unresolvedSprite){frame.diagnostics.push_back("Unresolved original UIImage Sprite: "+node.path+" / "+g.dynamicPath);continue;}
                const auto*o=overridden(pose,node.id);const auto pivot=o&&o->pivot?*o->pivot:node.rect?node.rect->pivot:Vec2{.5,.5};const auto fill=pose.value("m_FillAmount",node.id,g.image.fillAmount);
                const bool desktopUV=!g.raw&&replacement!=settings.images.end(),sized=desktopUV&&replacement->second.displaySize.has_value();
                const auto&mesh=local(g,sized?nullptr:sp,rect,g.raw?Vec2{}:pivot,g.raw?std::nullopt:std::optional(fill),force);
                auto color=g.color;constexpr const char*channels[]{"m_Color.r","m_Color.g","m_Color.b","m_Color.a"};for(unsigned c=0;c<4;++c)color[c]=color32(f32(pose.value(channels[c],node.id,color[c])));
                auto tint=tints.find(node.id);const auto fallback=resources->defaultSelectableTints.find(node.id);if(tint!=tints.end())for(unsigned c=0;c<4;++c)color[c]*=tint->second[c];else if(fallback!=resources->defaultSelectableTints.end())for(unsigned c=0;c<4;++c)color[c]*=fallback->second[c];
                if(!nodes[ci].gamma)for(unsigned c=0;c<3;++c)color[c]=gamma(color[c]);color[3]*=f32(frame.inheritedAlpha[index]);hit(g,rect,world,resolved,root,sorting.sortingOrder,sequence);++sequence;
                if(mesh.indices.empty()||color[3]<=0)continue;baked(g,mesh,rect,toCanvas,desktopUV,force);
                auto material=g.material;if(g.raw&&g.animation&&g.animation->material)material=g.animation->material;const auto base=settings.normalMaterialNodes.contains(node.id)?"__ui_default":material.value_or("__ui_default");
                const auto softID=plan.maskable?canvas.ancestry()[index].softMask:std::nullopt;const SoftMask*soft=softID&&softMasks[*softID]?&*softMasks[*softID]:nullptr;
                if(softID&&!soft){frame.diagnostics.push_back("Unresolved original soft mask: "+node.path);continue;}
                const auto variant=resources->materialVariants.find(base);const auto variantIndex=(clip?2u:0u)+(soft?1u:0u);if(variant==resources->materialVariants.end()||!variant->second[variantIndex]){frame.diagnostics.push_back("Unresolved original material clipping variant: "+node.path);continue;}
                style(node.id,color);SourceWatchBatch b;b.stateID="ui/"+g.id;b.sourceNodeID=node.id;b.sourceMesh=b.stateID;b.material=*variant->second[variantIndex];b.world=quantized(canvasWorld);b.color=color;b.geometry=&g.geometry;b.appliesDesktopAccent=!resources->profileNodeIDs.contains(node.id);
                const auto texture=g.raw?g.texture.value_or("__white"):desktopUV?replacement->second.texture:sp?sp->textureID:"__white";b.textureOverrides["_MainTex"]=texture;
                const auto size=resources->textureSizes.find(texture);const auto textureSize=desktopUV?std::optional(replacement->second.size):size!=resources->textureSizes.end()?std::optional(size->second):std::nullopt;
                if(textureSize)b.uniformOverrides["mainTexTexelSize"]={1/(*textureSize)[0],1/(*textureSize)[1],(*textureSize)[0],(*textureSize)[1]};
                if(clip){b.uniformOverrides["clipRect"]={clip->rectangle.begin(),clip->rectangle.end()};if(clip->hasSoftness){b.uniformOverrides["clipRectParam"]={clip->parameters.begin(),clip->parameters.end()};b.uniformOverrides["uiMaskHGSoftness"]={clip->hgSoftness.begin(),clip->hgSoftness.end()};}}
                materialProperties(pose,node.id,material,b);
                if(g.animation){const auto sx=pose.value("_scale.x",node.id,g.animation->scale[0]),sy=pose.value("_scale.y",node.id,g.animation->scale[1]);const auto tiny=double(std::numeric_limits<float>::denorm_min())*8;const auto ix=std::abs(sx)<tiny?0:1/sx,iy=std::abs(sy)<tiny?0:1/sy;
                    b.uniformOverrides["_VFXMainTex_ST"]={f32(ix),f32(iy),f32((1-ix)*.5),f32((1-iy)*.5)};b.uniformOverrides["_TintColorAlpha"]={f32(pose.value("_alpha",node.id,g.animation->alpha))};}
                if(soft){b.uniformOverrides["_WorldToSoftMask"]=flatten(soft->worldToUnit*b.world);b.uniformOverrides["_SoftMaskTex_ST"]={soft->st.begin(),soft->st.end()};b.uniformOverrides["_InnerSoftMask"]={soft->inner.begin(),soft->inner.end()};b.uniformOverrides["_InnerSoftMaskUV"]={soft->innerUV.begin(),soft->innerUV.end()};b.uniformOverrides["_SpriteIsSliced"]={soft->sliced?1.f:0.f};b.textureOverrides["_SoftMaskTex"]=soft->texture;}
                append(std::move(b),sorting.sortingOrder,sequence);
            }
            if(plan.mesh)for(std::size_t slot=0;slot<plan.mesh->materials.size();++slot){const auto&material=plan.mesh->materials[slot];SourceWatchBatch b;b.stateID="mesh/"+node.id+"/"+std::to_string(slot);b.sourceNodeID=node.id;b.sourceMesh=plan.mesh->source;b.material=material;b.world=quantized(world);style(node.id,b.color);b.appliesDesktopAccent=!resources->profileNodeIDs.contains(node.id);b.uniformOverrides["_WatchWorldToLocalMatrix"]=watchToLocal;materialProperties(pose,node.id,material,b);append(std::move(b),plan.mesh->order,sequence++);}
        }
        const auto less=[](const Order&a,const Order&b){return std::tie(a.order,a.sequence)<std::tie(b.order,b.sequence);};std::stable_sort(batchOrder.begin(),batchOrder.end(),less);std::stable_sort(hitOrder.begin(),hitOrder.end(),less);
        // Bounded latest frame only. The sorting scratch is discarded after
        // moving its storage into the retained presentation.
        std::vector<SourceWatchBatch> sorted;sorted.reserve(frame.batches.size());for(const auto&o:batchOrder)sorted.push_back(std::move(frame.batches[o.index]));frame.batches=std::move(sorted);
        std::vector<SourceWatchHit> hits;hits.reserve(frame.hits.size());for(const auto&o:hitOrder)hits.push_back(std::move(frame.hits[o.index]));frame.hits=std::move(hits);
        for(const auto&p:pose.unboundPaths)frame.diagnostics.push_back("Unbound source curve: "+p);for(const auto&p:pose.unregisteredBindings)frame.diagnostics.push_back("Ignored unregistered native animation binding: "+p);
        std::copy(frame.resolved.begin(),frame.resolved.end(),presentationResolved.begin());frame.resolved=presentationResolved;
        prepareAmbient(input);previousInput=input;previousRoot=root;previousScroll=scroll;previousEntries=entries;previousTints=tints;++revision;return frame;
    }
};
SourceWatchFrameBuilder::SourceWatchFrameBuilder(const SceneDefinition&s,const MountedLayoutDocument&d,const SourceWatchFrameResources&r,SourceDesktopFrameSettings settings):impl_(std::make_unique<Impl>(s,d,r,std::move(settings))){}
SourceWatchFrameBuilder::~SourceWatchFrameBuilder()=default;
void SourceWatchFrameBuilder::setDesktopSettings(SourceDesktopFrameSettings value){auto&p=*impl_;if(p.settings==value)return;p.settings=std::move(value);p.previousInput.reset();p.layoutInput.reset();p.ambientSupported=false;++p.revision;}
const SourceWatchFrame&SourceWatchFrameBuilder::build(const Pose&p,const Matrix4&root,double scroll,const DesktopNavigationLayout*nav,const SourceFrameTints&tints,bool force){return impl_->build(p,root,scroll,nav,tints,force);}
const SourceWatchFrame*SourceWatchFrameBuilder::buildSettledAmbient(const Pose&p,std::uint64_t revision,const Matrix4&root,Vec2 size,double scroll,const DesktopNavigationLayout*nav,const SourceFrameTints&tints){return impl_->settled(p,revision,root,size,scroll,nav,tints);}
std::uint64_t SourceWatchFrameBuilder::presentationRevision()const noexcept{return impl_->revision;}
SourceWatchFrameStats SourceWatchFrameBuilder::stats()const noexcept{return impl_->counts;}
const SourceFrameTints&SourceWatchFrameBuilder::emptyTints()noexcept{static const SourceFrameTints empty;return empty;}
} // namespace endfield::core::source
