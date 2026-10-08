#include "core/source_watch_layout.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <variant>

namespace endfield::core::source {
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
double number(const Json&v,double fallback=0){return v.isNumber()?v.number():fallback;}
bool flag(const Json&v,bool fallback=false){return v.isBool()?v.boolean():v.isNumber()?v.number()!=0:fallback;}
std::string text(const Json&v){return v.isString()?v.string():std::string{};}
std::optional<std::string> target(const Json&v){return v["target_id"].isString()?std::optional(v["target_id"].string()):std::nullopt;}
const TransformOverride* overrideFor(const Pose&p,std::string_view id){const auto i=p.transforms.find(id);return i==p.transforms.end()?nullptr:&i->second;}
}
bool WatchComponent::enabled() const {return flag(data["m_Enabled"],true);}
MountedLayoutDocument MountedLayoutDocument::fromJson(const Json&value){
    MountedLayoutDocument result;need(value["components"].object().size()<=16384,"Too many mounted component nodes");std::size_t records{};
    for(const auto&[id,list]:value["components"].object())for(const auto&record:list.array()){
        need(++records<=1000000,"Too many mounted components");WatchComponent c;c.id=text(record["id"]);c.type=text(record["type"]);
        if(record["script"].isString())c.script=text(record["script"]);c.data=record["data"];need(!c.id.empty()&&c.data.isObject(),"Invalid mounted component");result.components[id].push_back(std::move(c));
    }
    for(const auto&[id,spriteValue]:value["spriteByComponent"].object())result.spriteByComponent.emplace(id,spriteValue);
    for(const auto&b:value["buttons"].array()){
        WatchButton button{text(b["node_id"]),text(b["path"]),{}};need(!button.nodeID.empty(),"Missing mounted button identity");
        for(const auto&label:b["labels"].array())if(text(label["text_id"])!="ui_common_new_eng"&&label["cn_literal"].isString()){button.captionID=text(label["node_id"]);break;}
        result.buttons.push_back(std::move(button));
    }return result;
}
const WatchComponent* MountedLayoutDocument::component(std::string_view kind,std::string_view node) const noexcept {
    const auto i=components.find(node);if(i!=components.end())for(const auto&c:i->second)if(c.kind()==kind&&c.enabled())return &c;return nullptr;
}
const ResolvedNode* ResolvedView::node(std::string_view id) const noexcept {if(!layout)return nullptr;const auto i=layout->nodeIndex(id);return i&&*i<nodes.size()&&nodes[*i].node?&nodes[*i]:nullptr;}
DesktopNavigationLayout::DesktopNavigationLayout(const SceneDefinition&scene,const MountedLayoutDocument&document,std::int64_t count):entryCount_(static_cast<std::size_t>(std::max<std::int64_t>(0,count))){
    std::vector<std::string> ids;std::map<std::string,std::vector<std::string>,std::less<>> byRow;
    for(const auto&button:document.buttons)if(button.path.find("/RightBottomNode/")!=std::string::npos){
        if(button.captionID)need(captions_.emplace(button.nodeID,*button.captionID).second,"Repeated source button caption");
        const auto*node=scene.node(button.nodeID);if(!node||!node->parent)continue;
        auto [i,added]=byRow.try_emplace(*node->parent);if(added)ids.push_back(*node->parent);i->second.push_back(button.nodeID);
    }
    for(const auto&id:ids)if(const auto*n=scene.node(id);n&&n->rect)rows_.push_back({id,byRow.at(id),n->rect->anchoredPosition,n->rect->sizeDelta});
    need(rows_.size()>1&&!rows_[0].buttons.empty(),"Missing desktop navigation row pool");columns_=rows_[0].buttons.size();for(const auto&r:rows_)need(r.buttons.size()==columns_,"Uneven desktop navigation row columns");
    std::set<std::string,std::less<>> ancestors;std::optional<std::string> id=rows_[0].id;while(id){ancestors.insert(*id);id=scene.node(*id)->parent;}
    const WatchComponent*scroll{};const Node*scrollNode{};SourceLayout layout(scene);
    for(const auto index:layout.traversalIndices()){const auto&node=scene.nodes()[index];const auto*c=document.component("UIScrollRect",node.id);const auto content=c?target((*c)["m_Content"]):std::nullopt;
        if(content&&ancestors.contains(*content)){scroll=c;scrollNode=&node;break;}}
    need(scroll&&scrollNode&&scrollNode->rect,"Missing source navigation scroll owner");const auto content=target((*scroll)["m_Content"]);const auto*contentNode=content?scene.node(*content):nullptr;
    const auto*rowNode=scene.node(rows_[0].id);const auto*parent=rowNode&&rowNode->parent?scene.node(*rowNode->parent):nullptr;
    need(contentNode&&contentNode->rect&&parent,"Missing source navigation content/row parent");
    rowStep_=(rows_.front().anchored[1]-rows_.back().anchored[1])/static_cast<double>(rows_.size()-1);rowScale_=parent->scale[1];viewportHeight_=scrollNode->rect->sizeDelta[1];
    need(rowStep_>0&&rowScale_>0&&viewportHeight_>0,"Invalid desktop source navigation extent");contentID_=*content;originalContentSize_=contentNode->rect->sizeDelta;
}
DesktopNavigationLayout::Sample DesktopNavigationLayout::sample(double position) const {
    const auto count=entryCount_/columns_+(entryCount_%columns_?1:0);
    const auto lastY=rows_[0].anchored[1]-static_cast<double>(count?count-1:0)*rowStep_;
    Sample sample;sample.contentHeight=std::max(viewportHeight_,originalContentSize_[1]+(rows_.back().anchored[1]-lastY)*rowScale_);
    const auto normalized=std::isfinite(position)?std::clamp(position,0.,1.):1.;const auto offset=(1-normalized)*std::max(0.,sample.contentHeight-viewportHeight_);
    const auto raw=offset/(rowStep_*rowScale_);need(std::isfinite(raw)&&raw<static_cast<double>(std::numeric_limits<std::int64_t>::max()),"Desktop navigation index overflow");
    const auto first=std::min(count>rows_.size()?count-rows_.size():0,static_cast<std::size_t>(std::max<std::int64_t>(0,static_cast<std::int64_t>(raw)-1)));
    for(auto logical=first;logical<std::min(count,first+rows_.size());++logical){
        const auto&row=rows_[logical%rows_.size()];sample.logicalRows[row.id]=logical;
        for(std::size_t column=0;column<row.buttons.size();++column){const auto index=logical*columns_+column;if(index<entryCount_)sample.assignments[row.buttons[column]]=index;}
    }return sample;
}
void DesktopNavigationLayout::apply(Pose&pose,double position) const {
    const auto s=sample(position);pose.transforms[contentID_].sizeDelta=Vec2{originalContentSize_[0],s.contentHeight};
    for(const auto&row:rows_){
        auto&t=pose.transforms[row.id];const auto i=s.logicalRows.find(row.id);t.active=i!=s.logicalRows.end();
        if(i!=s.logicalRows.end()){
            const auto logical=i->second;const auto z=t.positionComponents.find(2);
            const auto depth=z!=t.positionComponents.end()?z->second:t.localPosition?(*t.localPosition)[2]:t.anchoredPosition3D?(*t.anchoredPosition3D)[2]:0;
            t.anchoredPosition3D=Vec3{rows_[0].anchored[0],rows_[0].anchored[1]-static_cast<double>(logical)*rowStep_,depth};t.localPosition.reset();t.positionComponents.erase(0);t.positionComponents.erase(1);
            const auto occupied=std::min(columns_,entryCount_-logical*columns_);const auto width=row.size[0]*static_cast<double>(occupied)/static_cast<double>(columns_);
            t.sizeDelta=Vec2{width,row.size[1]};t.pivot=Vec2{row.size[0]/(2*width),.5};
        }
        for(const auto&button:row.buttons){auto&item=pose.transforms[button];item.active=s.assignments.contains(button);if(const auto caption=captions_.find(button);caption!=captions_.end())pose.transforms[caption->second].active=item.active;}
    }
}
namespace {
using Vector4=std::array<double,4>;
Vector4 transform(const Matrix4&m,const Vector4&v){Vector4 out{};for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)out[row]+=m.values[col*4+row]*v[col];return out;}
Vector4 translation(const Matrix4&m){return {m.values[12],m.values[13],m.values[14],m.values[15]};}
std::optional<Matrix4> inverse(const Matrix4&m){
    if(!m.finite())return {};double a[4][8]{};double determinant=1;
    for(unsigned r=0;r<4;++r){for(unsigned c=0;c<4;++c)a[r][c]=m.values[c*4+r];a[r][r+4]=1;}
    for(unsigned c=0;c<4;++c){unsigned pivot=c;for(unsigned r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
        if(a[pivot][c]==0)return {};if(pivot!=c){for(unsigned k=0;k<8;++k)std::swap(a[c][k],a[pivot][k]);determinant=-determinant;}
        const auto d=a[c][c];determinant*=d;for(auto&v:a[c])v/=d;
        for(unsigned r=0;r<4;++r)if(r!=c){const auto factor=a[r][c];for(unsigned k=0;k<8;++k)a[r][k]-=factor*a[c][k];}
    }
    if(!std::isfinite(determinant)||determinant==0)return {};Matrix4 out;for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)out.values[c*4+r]=a[r][c+4];return out.finite()?std::optional(out):std::nullopt;
}
std::int64_t integer(double value){need(std::isfinite(value)&&value>=static_cast<double>(std::numeric_limits<std::int64_t>::min())&&value<static_cast<double>(std::numeric_limits<std::int64_t>::max()),"Source layout integer overflow");return static_cast<std::int64_t>(value);}
constexpr std::string_view controlSize[]{"m_ChildControlWidth","m_ChildControlHeight"},scaleSize[]{"m_ChildScaleWidth","m_ChildScaleHeight"},forceSize[]{"m_ChildForceExpandWidth","m_ChildForceExpandHeight"};
constexpr std::string_view minSize[]{"m_MinWidth","m_MinHeight"},preferredSize[]{"m_PreferredWidth","m_PreferredHeight"},flexibleSize[]{"m_FlexibleWidth","m_FlexibleHeight"};
struct RectLayout {SourceRect rect;Vec3 position;};
RectLayout rectLayout(const Node&node,const TransformOverride*o,const std::optional<SourceRect>&parent){
    const auto&r=*node.rect;const auto min=o&&o->anchorMin?*o->anchorMin:r.anchorMin,max=o&&o->anchorMax?*o->anchorMax:r.anchorMax,pivot=o&&o->pivot?*o->pivot:r.pivot;
    const auto delta=o&&o->sizeDelta?*o->sizeDelta:r.sizeDelta,ps=parent?parent->size:Vec2{},po=parent?parent->origin:Vec2{};
    const auto anchored=o&&o->anchoredPosition3D?*o->anchoredPosition3D:Vec3{r.anchoredPosition[0],r.anchoredPosition[1],node.position[2]};
    Vec2 size{};Vec3 position{0,0,anchored[2]};for(unsigned i=0;i<2;++i){const auto span=max[i]-min[i];size[i]=ps[i]*span+delta[i];position[i]=po[i]+ps[i]*(min[i]+span*pivot[i])+anchored[i];}
    return {SourceRect::fromSizePivot(size,pivot),position};
}
}
struct WatchLayout::Impl {
    using Candidate=std::pair<std::int64_t,Metrics>;
    struct Slant {
        double bottom{},range{},left{},width{};std::vector<std::string> cells;ScalarCurve curve;
        explicit Slant(const WatchComponent&c):bottom(number(c["_bottomY"])),range(number(c["_topY"])-bottom),left(number(c["_leftX"])),width(number(c["_maxWidth"])),curve(makeCurve(c)){
            need(range!=0,"Zero Watch slant Y range");for(const auto&cell:c["_cells"].array())if(const auto id=target(cell))cells.push_back(*id);
        }
        static ScalarCurve makeCurve(const WatchComponent&c){std::vector<ScalarKey> keys;for(const auto&k:c["_curve"]["m_Curve"].array()){
            const auto mode=integer(number(k["weightedMode"]));need(mode>=0&&mode<=3,"Invalid source slant weighted mode");
            keys.push_back({number(k["time"]),number(k["value"]),number(k["inSlope"]),number(k["outSlope"]),static_cast<unsigned>(mode),number(k["inWeight"]),number(k["outWeight"])});
        }return ScalarCurve(std::move(keys));}
    };
    const SceneDefinition*scene;const MountedLayoutDocument*document;IntrinsicSize intrinsic;
    SourceLayout layout;IncrementalResolver initialResolver,slantResolver;
    std::vector<std::map<std::string_view,const WatchComponent*,std::less<>>> enabled;
    std::vector<const WatchComponent*> groups;
    std::vector<std::vector<std::size_t>> children;
    std::vector<std::array<std::vector<Candidate>,2>> fixed;
    std::array<std::vector<Metrics>,2> fixedMeasured,measured;
    std::vector<std::size_t> dynamicIDs,writerIDs,customIDs,slantIDs,scrollIDs;
    std::vector<std::string> slantRoots;
    std::map<std::string,std::variant<Slant,std::exception_ptr>,std::less<>> slants;
    std::vector<std::vector<std::size_t>> scrollChains;
    std::vector<ResolvedNode> scrollResolved;
    std::vector<std::optional<SourceRect>> rects;
    std::vector<std::uint64_t> rectVersions;
    std::vector<std::size_t> rectPending;
    std::uint64_t rectVersion{};
    bool force{};
    Impl(const SceneDefinition&s,const MountedLayoutDocument&d,IntrinsicSize intrinsicCallback):scene(&s),document(&d),intrinsic(std::move(intrinsicCallback)),layout(s),initialResolver(s),slantResolver(s),
        enabled(s.nodes().size()),groups(s.nodes().size()),children(s.nodes().size()),fixed(s.nodes().size()),scrollChains(s.nodes().size()),scrollResolved(s.nodes().size()),rects(s.nodes().size()),rectVersions(s.nodes().size()){
        const auto nodes=s.nodes();rectPending.reserve(nodes.size());for(unsigned axis=0;axis<2;++axis){fixedMeasured[axis].resize(nodes.size());measured[axis].resize(nodes.size());}
        for(const auto&[id,records]:d.components){(void)id;for(const auto&c:records)if(c.enabled()&&c.kind()=="UIScrollCellSlantEffect"){
            try{slants.insert_or_assign(c.id,Slant(c));}catch(...){slants.insert_or_assign(c.id,std::current_exception());}
        }}
        for(std::size_t index=0;index<nodes.size();++index){
            const auto&node=nodes[index];const auto records=d.components.find(node.id);
            if(records!=d.components.end())for(const auto&c:records->second)if(c.enabled()){
                enabled[index].try_emplace(c.kind(),&c);if(!groups[index]&&(c.kind()=="HorizontalLayoutGroup"||c.kind()=="VerticalLayoutGroup"))groups[index]=&c;
            }
            for(const auto&child:node.children){const auto ci=*layout.nodeIndex(child);if(!nodes[ci].rect)continue;bool any=false,include=false;
                const auto values=d.components.find(child);if(values!=d.components.end())for(const auto&c:values->second)if(c.kind()=="LayoutElement"){any=true;include|=!flag(c["m_IgnoreLayout"]);}
                if(!any||include)children[index].push_back(ci);
            }
            for(unsigned axis=0;axis<2;++axis){fixed[index][axis]=fixedCandidates(index,axis);fixedMeasured[axis][index]=select(fixed[index][axis]);}
        }
        std::set<std::string,std::less<>> roots;
        for(const auto index:layout.traversalIndices()){
            if(groups[index]||component("ContentSizeFitter",index))writerIDs.push_back(index);
            if(component("UIScrollCellSlantEffect",index)){slantIDs.push_back(index);for(const auto&cell:(*component("UIScrollCellSlantEffect",index))["_cells"].array())if(const auto id=target(cell))roots.insert(*id);}
            const auto records=d.components.find(nodes[index].id);if(records!=d.components.end()&&std::any_of(records->second.begin(),records->second.end(),[](const auto&c){return c.enabled()&&(c.kind()=="UIStepScrollList"||c.kind()=="GridLayoutGroup"||c.kind()=="NotchAdapter");}))customIDs.push_back(index);
            const auto*scroll=component("UIScrollRect",index);if(!scroll)scroll=component("ScrollRect",index);if(!scroll)continue;scrollIDs.push_back(index);
            std::set<std::string,std::less<>> required;
            for(auto start:{std::optional(nodes[index].id),target((*scroll)["m_Content"]),target((*scroll)["m_Viewport"])})while(start&&required.insert(*start).second){const auto*n=s.node(*start);start=n?n->parent:std::nullopt;}
            for(const auto i:layout.traversalIndices())if(required.contains(nodes[i].id))scrollChains[index].push_back(i);
        }
        for(auto i=layout.traversalIndices().rbegin();i!=layout.traversalIndices().rend();++i)if(groups[*i]||component("UIText",*i))dynamicIDs.push_back(*i);
        slantRoots.assign(roots.begin(),roots.end());
    }
    const WatchComponent*component(std::string_view kind,std::size_t index) const {const auto i=enabled[index].find(kind);return i==enabled[index].end()?nullptr:i->second;}
    void invalidateRects(){if(++rectVersion==0){std::fill(rectVersions.begin(),rectVersions.end(),std::uint64_t{0});++rectVersion;}}
    std::optional<SourceRect> rect(std::size_t index,const Pose&pose){
        if(!scene->nodes()[index].rect)return {};if(rectVersions[index]==rectVersion&&!force)return rects[index];
        rectPending.clear();auto current=index;std::optional<SourceRect> parent;
        while(scene->nodes()[current].rect){
            if(!force&&rectVersions[current]==rectVersion){parent=rects[current];break;}
            rectPending.push_back(current);const auto&p=scene->nodes()[current].parent;if(!p)break;current=*layout.nodeIndex(*p);
        }
        while(!rectPending.empty()){
            const auto i=rectPending.back();rectPending.pop_back();parent=rectLayout(scene->nodes()[i],overrideFor(pose,scene->nodes()[i].id),parent).rect;
            if(!force){rects[i]=parent;rectVersions[i]=rectVersion;}
        }return parent;
    }
    Vec2 sizeDelta(std::size_t index,const Pose&pose) const {const auto&n=scene->nodes()[index];const auto*o=overrideFor(pose,n.id);return o&&o->sizeDelta?*o->sizeDelta:n.rect?n.rect->sizeDelta:Vec2{};}
    double scale(std::size_t index,unsigned axis,const Pose&pose) const {const auto&n=scene->nodes()[index];const auto*o=overrideFor(pose,n.id);return (o&&o->localScale?*o->localScale:n.scale)[axis];}
    static Metrics select(std::span<const Candidate> candidates){
        std::array<std::int64_t,3> priorities;priorities.fill(std::numeric_limits<std::int64_t>::min());std::array<double,3> result{};
        for(const auto&[priority,m]:candidates){const std::array values{m.minimum,m.preferred,m.flexible};for(unsigned i=0;i<3;++i)if(values[i]>=0&&priority>=priorities[i]){
            if(priority>priorities[i]){priorities[i]=priority;result[i]=values[i];}else result[i]=std::max(result[i],values[i]);
        }}return {result[0],std::max(result[0],result[1]),result[2]};
    }
    std::vector<Candidate> fixedCandidates(std::size_t index,unsigned axis) const {
        std::vector<Candidate> result;const auto&node=scene->nodes()[index];const auto records=document->components.find(node.id);if(records==document->components.end())return result;
        for(const auto&image:records->second)if(image.enabled()&&(image.kind()=="UIImage"||image.kind()=="Image")){
            const auto sprite=document->spriteByComponent.find(image.id);if(sprite==document->spriteByComponent.end())continue;
            const auto&raw=sprite->second["raw_sprite"];const auto&border=raw["m_Border"];double reference=100;std::optional<std::string>ancestor=node.id;
            while(ancestor){if(const auto*c=document->component("CanvasScaler",*ancestor)){reference=number((*c)["m_ReferencePixelsPerUnit"],100);break;}const auto*n=scene->node(*ancestor);ancestor=n?n->parent:std::nullopt;}
            const auto ppu=number(raw["m_PixelsToUnits"],100)/reference;if(!std::isfinite(ppu)||ppu<=0)continue;
            const auto type=integer(number(image["m_Type"]));const auto amount=type==1||type==2?(axis==0?number(border["x"])+number(border["z"]):number(border["y"])+number(border["w"])):number(raw["m_Rect"][axis==0?"width":"height"]);
            result.push_back({0,{0,amount/ppu,0}});
        }
        for(const auto&c:records->second)if(c.kind()=="LayoutElement"&&c.enabled())result.push_back({integer(number(c["m_LayoutPriority"],1)),{number(c[minSize[axis]],-1),number(c[preferredSize[axis]],-1),number(c[flexibleSize[axis]],-1)}});
        return result;
    }
    std::pair<double,double> padding(const WatchComponent&g,unsigned axis) const {const auto&p=g["m_Padding"];const auto start=number(p[axis==0?"m_Left":"m_Top"]);return {start,start+number(p[axis==0?"m_Right":"m_Bottom"])};}
    Metrics childMetrics(std::size_t child,const WatchComponent&g,unsigned axis,const Pose&pose) const {
        Metrics m;if(flag(g[controlSize[axis]]))m=measured[axis][child];else{const auto size=sizeDelta(child,pose)[axis];m={size,size,0};}
        if(flag(g[forceSize[axis]]))m.flexible=std::max(m.flexible,1.);return m;
    }
    Metrics totals(const WatchComponent&g,std::size_t index,unsigned axis,const Pose&pose,std::span<const ResolvedNode>active) const {
        const auto p=padding(g,axis);const bool cross=(g.kind()=="VerticalLayoutGroup")!=(axis==1);const auto spacing=number(g["m_Spacing"]);
        Metrics sum{p.second,p.second,0};bool any=false;
        for(const auto child:children[index])if(active[child].activeInHierarchy){any=true;auto m=childMetrics(child,g,axis,pose);if(flag(g[scaleSize[axis]])){const auto s=scale(child,axis,pose);m.minimum*=s;m.preferred*=s;m.flexible*=s;}
            if(cross){sum.minimum=std::max(sum.minimum,m.minimum+p.second);sum.preferred=std::max(sum.preferred,m.preferred+p.second);sum.flexible=std::max(sum.flexible,m.flexible);}
            else{sum.minimum+=m.minimum+spacing;sum.preferred+=m.preferred+spacing;sum.flexible+=m.flexible;}}
        if(!cross&&any){sum.minimum-=spacing;sum.preferred-=spacing;}sum.preferred=std::max(sum.minimum,sum.preferred);return sum;
    }
    Metrics metrics(std::size_t index,unsigned axis,const Pose&pose,std::span<const ResolvedNode>active,Report&report){
        // Fixed candidates are immutable. Dynamic group/text entries need only
        // two stack slots; property selection preserves source priority rules.
        std::array<Candidate,2> dynamic{};std::size_t count{};if(groups[index])dynamic[count++]={0,totals(*groups[index],index,axis,pose,active)};
        if(component("UIText",index)){
            const auto&node=scene->nodes()[index];const auto size=intrinsic?intrinsic(node.id,rect(index,pose)):std::nullopt;
            if(size&&std::isfinite((*size)[0])&&std::isfinite((*size)[1]))dynamic[count++]={0,{0,std::max(0.,(*size)[axis]),0}};
            else{report.missingTextMetrics.insert(node.id);dynamic[count++]={0,{0,std::max(0.,sizeDelta(index,pose)[axis]),0}};}
        }
        std::array<std::int64_t,3> priorities;priorities.fill(std::numeric_limits<std::int64_t>::min());std::array<double,3> values{};
        auto add=[&](const Candidate&c){const std::array v{c.second.minimum,c.second.preferred,c.second.flexible};for(unsigned i=0;i<3;++i)if(v[i]>=0&&c.first>=priorities[i]){if(c.first>priorities[i]){priorities[i]=c.first;values[i]=v[i];}else values[i]=std::max(values[i],v[i]);}};
        for(std::size_t i=0;i<count;++i)add(dynamic[i]);for(const auto&c:fixed[index][axis])add(c);return {values[0],std::max(values[0],values[1]),values[2]};
    }
    void setSize(std::size_t index,unsigned axis,double value,Pose&pose,bool reset=false){
        const auto&node=scene->nodes()[index];if(!node.rect)return;const auto&r=*node.rect;auto&o=pose.transforms[node.id];auto size=o.sizeDelta.value_or(r.sizeDelta);
        if(reset){o.anchorMin=Vec2{0,1};o.anchorMax=Vec2{0,1};}const auto min=o.anchorMin.value_or(r.anchorMin),max=o.anchorMax.value_or(r.anchorMax);
        const auto parent=node.parent?rect(*layout.nodeIndex(*node.parent),pose):std::nullopt;size[axis]=value-(parent?parent->size[axis]:0)*(max[axis]-min[axis]);o.sizeDelta=size;invalidateRects();
    }
    void setAnchored(std::size_t index,unsigned axis,double value,bool reset,Pose&pose){
        const auto&node=scene->nodes()[index];if(!node.rect)return;const auto&r=*node.rect;auto&o=pose.transforms[node.id];
        if(o.localPosition){for(unsigned i=0;i<3;++i)if(i!=axis&&!o.positionComponents.contains(i))o.positionComponents[i]=(*o.localPosition)[i];o.localPosition.reset();}
        o.positionComponents.erase(axis);auto anchored=o.anchoredPosition3D.value_or(Vec3{r.anchoredPosition[0],r.anchoredPosition[1],node.position[2]});anchored[axis]=value;o.anchoredPosition3D=anchored;
        if(reset){o.anchorMin=Vec2{0,1};o.anchorMax=Vec2{0,1};}invalidateRects();
    }
    void control(const WatchComponent&g,std::size_t index,unsigned axis,Pose&pose,std::span<const ResolvedNode>active){
        const auto parent=rect(index,pose);if(!parent)return;const bool controls=flag(g[controlSize[axis]]),scales=flag(g[scaleSize[axis]]),cross=(g.kind()=="VerticalLayoutGroup")!=(axis==1);
        const auto p=padding(g,axis);const auto size=parent->size[axis];const auto alignment=integer(number(g["m_ChildAlignment"]));const double align=static_cast<double>(axis==0?alignment%3:alignment/3)*.5;
        const auto total=totals(g,index,axis,pose,active);double pos=p.first,multiplier=0;
        if(size>total.preferred){if(total.flexible==0)pos+=(size-total.preferred)*align;else if(total.flexible>0)multiplier=(size-total.preferred)/total.flexible;}
        const auto lerp=total.minimum==total.preferred?0:std::clamp((size-total.minimum)/(total.preferred-total.minimum),0.,1.);
        auto write=[&](std::size_t child){if(!active[child].activeInHierarchy)return;const auto m=childMetrics(child,g,axis,pose);const auto s=scales?scale(child,axis,pose):1.;double required,start;
            if(cross){required=std::min(m.flexible>0?size:m.preferred,std::max(m.minimum,size-p.second));start=p.first+(size-p.second-required*s)*align;}
            else{required=m.minimum+(m.preferred-m.minimum)*lerp+m.flexible*multiplier;start=pos;}
            const auto actual=controls?required:sizeDelta(child,pose)[axis],offset=controls?0:(required-actual)*align;if(controls)setSize(child,axis,required,pose,true);
            const auto&node=scene->nodes()[child];const auto*o=overrideFor(pose,node.id);const auto pivot=(o&&o->pivot?*o->pivot:node.rect?node.rect->pivot:Vec2{.5,.5})[axis];
            const auto anchored=axis==0?start+offset+actual*pivot*s:-start-offset-actual*(1-pivot)*s;setAnchored(child,axis,anchored,true,pose);
            if(!cross)pos+=required*s+number(g["m_Spacing"]);
        };
        if(flag(g["m_ReverseArrangement"]))for(auto i=children[index].rbegin();i!=children[index].rend();++i)write(*i);else for(const auto i:children[index])write(i);
    }
    void setLocal(std::size_t index,const Vector4&value,const std::optional<SourceRect>&parent,Pose&pose){
        const auto&node=scene->nodes()[index];if(!node.rect)return;auto&o=pose.transforms[node.id];const auto&r=*node.rect;
        // RectTransform's zero-anchored reference needs only anchors/pivot and
        // the immediate parent rect. Do not copy the override's component map.
        const auto min=o.anchorMin.value_or(r.anchorMin),max=o.anchorMax.value_or(r.anchorMax),pivot=o.pivot.value_or(r.pivot);
        const auto ps=parent?parent->size:Vec2{},po=parent?parent->origin:Vec2{};Vec3 anchored{0,0,value[2]};
        for(unsigned axis=0;axis<2;++axis)anchored[axis]=value[axis]-(po[axis]+ps[axis]*(min[axis]+(max[axis]-min[axis])*pivot[axis]));
        o.anchoredPosition3D=anchored;o.localPosition.reset();o.positionComponents.erase(0);o.positionComponents.erase(1);o.positionComponents[2]=value[2];invalidateRects();
    }
    ResolvedView resolveChain(std::size_t index,const Pose&pose){
        for(const auto i:scrollChains[index]){const auto&node=scene->nodes()[i];const auto*parent=node.parent?&scrollResolved[*layout.nodeIndex(*node.parent)]:nullptr;
            scrollResolved[i]=SourceLayout::resolveNode(node,overrideFor(pose,node.id),parent?parent->rect:std::nullopt,parent?parent->worldMatrix:Matrix4{},parent?parent->activeInHierarchy:true);}
        return {&layout,scrollResolved};
    }
    void scroll(Pose&pose,double position,const DesktopNavigationLayout*desktop,Report&report){
        for(const auto index:scrollIDs){const auto*componentScroll=component("UIScrollRect",index);if(!componentScroll)componentScroll=component("ScrollRect",index);const auto&c=*componentScroll;
            const auto contentID=target(c["m_Content"]),viewportID=target(c["m_Viewport"]);if(!flag(c["m_Vertical"])||flag(c["disableScroll"])||!contentID||!viewportID)continue;
            std::vector<ResolvedNode> full;ResolvedView resolved;if(force){full=layout.resolve({},pose.transforms);resolved={&layout,full};}else resolved=resolveChain(index,pose);
            const auto*owner=resolved.node(scene->nodes()[index].id),*viewport=resolved.node(*viewportID),*content=resolved.node(*contentID);
            if(!owner||!owner->activeInHierarchy||!viewport||!viewport->rect||!content||!content->rect)continue;const auto inv=inverse(viewport->worldMatrix);if(!inv)continue;
            const auto mapping=*inv*content->worldMatrix;double lower=std::numeric_limits<double>::infinity(),upper=-lower;
            for(const auto&corner:content->rect->corners()){const auto y=transform(mapping,{corner[0],corner[1],corner[2],1})[1];lower=std::min(lower,y);upper=std::max(upper,y);}
            auto boundsMin=lower,extent=upper-lower;const auto&viewRect=*viewport->rect;
            if(extent<viewRect.size[1]){const auto excess=viewRect.size[1]-extent;const auto*o=overrideFor(pose,*contentID);const auto pivot=(o&&o->pivot?*o->pivot:content->node->rect?content->node->rect->pivot:Vec2{.5,.5})[1];boundsMin-=excess*pivot;extent=viewRect.size[1];}
            const auto hidden=std::max(0.,extent-viewRect.size[1]);const auto normalized=desktop&&*contentID==desktop->contentID()?DesktopScrollMotion::presentationPosition(position,hidden):std::clamp(position,0.,1.);
            const auto delta=viewRect.origin[1]-normalized*hidden-boundsMin;
            if(std::abs(delta)>.01){auto destination=translation(content->localMatrix);destination[1]+=delta;const auto*parent=content->node->parent?resolved.node(*content->node->parent):nullptr;setLocal(*layout.nodeIndex(*contentID),destination,parent?parent->rect:std::nullopt,pose);}
            report.scroll=ScrollInfo{scene->nodes()[index].id,*contentID,*viewportID,hidden,number(c["m_ScrollSensitivity"],1),normalized};report.unverifiedCustomComponents.insert("UIScrollRect.elasticInertiaAndSmoothScrollScheduling");
        }
    }
    void slant(const WatchComponent&effect,std::size_t index,ResolvedView resolved,const Matrix4&worldRoot,Pose&pose,bool forceRebuild){
        const auto*self=resolved.node(scene->nodes()[index].id);if(!self)return;const auto inv=inverse(worldRoot*self->worldMatrix);if(!inv)return;
        std::optional<Slant> rebuilt;const Slant*configuration{};
        if(!forceRebuild){const auto i=slants.find(effect.id);if(i!=slants.end()){if(const auto*error=std::get_if<std::exception_ptr>(&i->second))std::rethrow_exception(*error);configuration=&std::get<Slant>(i->second);}}
        if(!configuration){rebuilt.emplace(effect);configuration=&*rebuilt;}
        for(const auto&id:configuration->cells){const auto*cell=resolved.node(id);if(!cell||!cell->node->parent||!cell->node->rect)continue;const auto*parent=resolved.node(*cell->node->parent);if(!parent)continue;
            const auto parentInverse=inverse(worldRoot*parent->worldMatrix);if(!parentInverse)continue;const auto world=translation(worldRoot*cell->worldMatrix);const auto y=transform(*inv,world)[1];
            const auto time=std::clamp((y-configuration->bottom)/configuration->range,0.,1.);const auto value=configuration->curve.sample(time);if(!value)continue;
            const auto x=configuration->left+*value*configuration->width;const auto desiredX=transform(worldRoot*self->worldMatrix,{x,0,0,1})[0];const auto destination=transform(*parentInverse,{desiredX,world[1],world[2],1});
            setLocal(*layout.nodeIndex(id),destination,parent->rect,pose);
        }
    }
};
WatchLayout::WatchLayout(const SceneDefinition&s,const MountedLayoutDocument&d,IntrinsicSize i):impl_(std::make_unique<Impl>(s,d,std::move(i))){}
WatchLayout::~WatchLayout()=default;WatchLayout::WatchLayout(WatchLayout&&) noexcept=default;WatchLayout&WatchLayout::operator=(WatchLayout&&) noexcept=default;
WatchLayout::Report WatchLayout::apply(Pose&pose,double normalized,const Matrix4&worldRoot,const DesktopNavigationLayout*navigation,const SlantMapping&mapping,const std::function<void(const Pose&)>&before,bool forceRebuild){
    need(std::isfinite(normalized),"Nonfinite Watch scroll position");auto&p=*impl_;p.force=forceRebuild;p.invalidateRects();if(navigation)navigation->apply(pose,normalized);
    std::vector<ResolvedNode> initialFull;std::span<const ResolvedNode> initial;if(forceRebuild){initialFull=p.layout.resolve({},pose.transforms);initial=initialFull;}else initial=p.initialResolver.resolve({},pose.transforms);
    Report report;for(unsigned axis=0;axis<2;++axis){p.measured[axis]=p.fixedMeasured[axis];
        for(const auto index:p.dynamicIDs)if(initial[index].activeInHierarchy)p.measured[axis][index]=p.metrics(index,axis,pose,initial,report);
        for(const auto index:p.writerIDs)if(initial[index].activeInHierarchy&&p.scene->nodes()[index].rect){
            if(const auto*fitter=p.component("ContentSizeFitter",index)){const auto fit=integer(number((*fitter)[axis==0?"m_HorizontalFit":"m_VerticalFit"]));if(fit==1||fit==2){const auto&m=p.measured[axis][index];p.setSize(index,axis,fit==1?m.minimum:m.preferred,pose);}}
            if(p.groups[index])p.control(*p.groups[index],index,axis,pose,initial);
        }
    }
    p.scroll(pose,normalized,navigation,report);if(before)before(pose);
    std::vector<ResolvedNode> slantFull;ResolvedView resolved;if(forceRebuild){slantFull=p.layout.resolve({},pose.transforms);resolved={&p.layout,slantFull};}else resolved={&p.layout,p.slantResolver.resolve({},pose.transforms)};
    for(const auto index:p.slantIDs)if(resolved.nodes[index].activeInHierarchy){const auto&effect=*p.component("UIScrollCellSlantEffect",index);report.unverifiedCustomComponents.insert("UIScrollCellSlantEffect.tickSchedulingRelativeToCanvasRebuild");
        if(mapping){for(const auto&cell:effect["_cells"].array())if(const auto id=target(cell)){const auto x=mapping(effect,*id,resolved);if(x&&std::isfinite(*x))if(const auto i=p.layout.nodeIndex(*id))p.setAnchored(*i,0,*x,false,pose);}}
        else p.slant(effect,index,resolved,worldRoot,pose,forceRebuild);
    }
    for(const auto index:p.customIDs)if(resolved.nodes[index].activeInHierarchy){const auto i=p.document->components.find(p.scene->nodes()[index].id);if(i!=p.document->components.end())for(const auto&c:i->second)if(c.enabled()&&(c.kind()=="UIStepScrollList"||c.kind()=="GridLayoutGroup"||c.kind()=="NotchAdapter"))report.unverifiedCustomComponents.insert(std::string(c.kind()));}
    return report;
}
void WatchLayout::applySlant(Pose&pose,const Matrix4&worldRoot,std::optional<ResolvedView>before,bool forceRebuild){
    auto&p=*impl_;p.force=forceRebuild;p.invalidateRects();std::vector<ResolvedNode> full;ResolvedView resolved;
    if(before)resolved=*before;else{full=p.layout.resolve({},pose.transforms);resolved={&p.layout,full};}
    for(const auto index:p.slantIDs){const auto*node=resolved.node(p.scene->nodes()[index].id);if(node&&node->activeInHierarchy)p.slant(*p.component("UIScrollCellSlantEffect",index),index,resolved,worldRoot,pose,forceRebuild);}
}
double WatchLayout::scrolledPosition(double current,double delta,const std::optional<ScrollInfo>&info) noexcept {return std::isfinite(current)&&std::isfinite(delta)&&info&&info->hiddenLength>0?std::clamp(current+delta*info->sensitivity/info->hiddenLength,0.,1.):current;}
std::optional<std::size_t> WatchLayout::scrollResolutionNodeCount(std::string_view id) const noexcept {const auto i=impl_->layout.nodeIndex(id);return i&&!impl_->scrollChains[*i].empty()?std::optional(impl_->scrollChains[*i].size()):std::nullopt;}
std::span<const std::string> WatchLayout::slantRootIDs() const noexcept{return impl_->slantRoots;}
std::uint64_t WatchLayout::initialRebuiltNodeCount() const noexcept{return impl_->initialResolver.rebuiltNodeCount();}
std::uint64_t WatchLayout::slantRebuiltNodeCount() const noexcept{return impl_->slantResolver.rebuiltNodeCount();}
} // namespace endfield::core::source
