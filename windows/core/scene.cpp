#include "core/scene.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::core {
bool Rect::contains(Point p) const noexcept {return width>0&&height>0&&p.x>=x&&p.y>=y&&p.x<=x+width&&p.y<=y+height;}
Matrix4 Matrix4::translation(double x,double y,double z) {Matrix4 m;m.values[12]=x;m.values[13]=y;m.values[14]=z;return m;}
Matrix4 Matrix4::scale(double x,double y,double z) {Matrix4 m;m.values[0]=x;m.values[5]=y;m.values[10]=z;return m;}
Matrix4 operator*(const Matrix4& a,const Matrix4& b) {
    Matrix4 out;out.values.fill(0);
    for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)for(unsigned k=0;k<4;++k)
        out.values[col*4+row]+=a.values[k*4+row]*b.values[col*4+k];
    return out;
}
Matrix4 Matrix4::rotation(double x,double y,double z) {
    Matrix4 rx,ry,rz;
    rx.values[5]=rx.values[10]=std::cos(x);rx.values[6]=std::sin(x);rx.values[9]=-std::sin(x);
    ry.values[0]=ry.values[10]=std::cos(y);ry.values[2]=-std::sin(y);ry.values[8]=std::sin(y);
    rz.values[0]=rz.values[5]=std::cos(z);rz.values[1]=std::sin(z);rz.values[4]=-std::sin(z);
    return rz*ry*rx;
}
bool Matrix4::finite() const noexcept {return std::all_of(values.begin(),values.end(),[](double x){return std::isfinite(x);});}
Projection Projection::viewport(const Matrix4& m,double width,double height) {
    if(!m.finite()||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)
        throw std::invalid_argument("Invalid viewport projection");
    const auto& v=m.values;const auto w=width/2,h=height/2;
    return {{w*(v[0]+v[3]),w*(v[4]+v[7]),w*(v[12]+v[15]),
             h*(v[3]-v[1]),h*(v[7]-v[5]),h*(v[15]-v[13]),v[3],v[7],v[15]}};
}
std::optional<Point> Projection::project(Point p) const {
    const auto& m=values;const double w=m[6]*p.x+m[7]*p.y+m[8];
    if(!std::isfinite(w)||w<=1e-10)return {};
    Point out{(m[0]*p.x+m[1]*p.y+m[2])/w,(m[3]*p.x+m[4]*p.y+m[5])/w};
    return std::isfinite(out.x)&&std::isfinite(out.y)?std::optional(out):std::nullopt;
}
std::optional<Point> Projection::unproject(Point p) const {
    const auto& m=values;
    const double a=m[0]-p.x*m[6],b=m[1]-p.x*m[7],c=p.x*m[8]-m[2];
    const double d=m[3]-p.y*m[6],e=m[4]-p.y*m[7],f=p.y*m[8]-m[5];
    const double determinant=a*e-b*d;
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-12)return {};
    Point result{(c*e-b*f)/determinant,(a*f-c*d)/determinant};
    return project(result)?std::optional(result):std::nullopt;
}
bool polygonContains(std::span<const Point> contour,Point p) noexcept {
    if(contour.size()<3||!std::isfinite(p.x)||!std::isfinite(p.y))return false;
    bool inside=false;
    for(std::size_t i=0,j=contour.size()-1;i<contour.size();j=i++) {
        const auto a=contour[j],b=contour[i];
        const double cross=(p.x-a.x)*(b.y-a.y)-(p.y-a.y)*(b.x-a.x);
        if(std::abs(cross)<1e-9&&p.x>=std::min(a.x,b.x)&&p.x<=std::max(a.x,b.x)&&p.y>=std::min(a.y,b.y)&&p.y<=std::max(a.y,b.y))return true;
        if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;
    }
    return inside;
}
NodeID RetainedScene::append(std::string sourceID,NodeID parent,Matrix4 local) {
    if(sourceID.empty()||sourceID.size()>1024||nodes_.size()>=maximumNodes||!local.finite()||
       (parent!=noNode&&parent>=nodes_.size()))throw std::invalid_argument("Invalid retained node");
    if(std::any_of(nodes_.begin(),nodes_.end(),[&](const auto& n){return n.sourceID==sourceID;}))throw std::invalid_argument("Duplicate node ID");
    const auto id=static_cast<NodeID>(nodes_.size());
    SceneNode next;next.sourceID=std::move(sourceID);next.parent=parent;next.local=local;
    nodes_.push_back(std::move(next));dirty_.push_back(true);return id;
}
bool RetainedScene::setLocal(NodeID id,Matrix4 local) {
    if(!local.finite())throw std::invalid_argument("Nonfinite transform");
    auto& n=nodes_.at(id);if(n.local==local)return false;n.local=local;dirty_[id]=true;return true;
}
bool RetainedScene::setOpacity(NodeID id,double value) {
    if(!std::isfinite(value))throw std::invalid_argument("Nonfinite opacity");
    value=std::clamp(value,0.,1.);auto& n=nodes_.at(id);if(n.opacity==value)return false;n.opacity=value;dirty_[id]=true;return true;
}
bool RetainedScene::setVisible(NodeID id,bool value) {auto& n=nodes_.at(id);if(n.visible==value)return false;n.visible=value;dirty_[id]=true;return true;}
void RetainedScene::setTargets(std::vector<HitTarget> targets) {
    if(targets.size()>maximumTargets)throw std::length_error("Too many interactive targets");
    auto validContour=[](const auto& contour) {return contour.size()>=3&&contour.size()<=4096&&std::all_of(contour.begin(),contour.end(),[](Point p){return std::isfinite(p.x)&&std::isfinite(p.y);});};
    for(const auto& target:targets) {
        if(target.node>=nodes_.size()||target.action.empty()||!validContour(target.contour)||target.clips.size()>32)throw std::invalid_argument("Invalid interactive target");
        for(const auto& clip:target.clips)if(clip.node>=nodes_.size()||!validContour(clip.contour))throw std::invalid_argument("Invalid hit mask");
    }
    targets_=std::move(targets);
}
std::size_t RetainedScene::resolve() {
    std::size_t changed=0;
    for(std::size_t i=0;i<nodes_.size();++i) {
        auto& n=nodes_[i];const bool parentDirty=n.parent!=noNode&&dirty_[n.parent];
        if(!dirty_[i]&&!parentDirty)continue;
        dirty_[i]=true;++changed;
        n.world=n.parent==noNode?n.local:nodes_[n.parent].world*n.local;
        n.resolvedOpacity=n.opacity*(n.parent==noNode?1:nodes_[n.parent].resolvedOpacity);
        n.resolvedVisible=n.visible&&(n.parent==noNode||nodes_[n.parent].resolvedVisible);
    }
    if(changed){std::fill(dirty_.begin(),dirty_.end(),false);++revision_;}
    return changed;
}
const SceneNode& RetainedScene::node(NodeID id) const {return nodes_.at(id);}
std::optional<std::string> RetainedScene::hit(Point screen,const Matrix4& camera,double width,double height) const {
    for(auto t=targets_.rbegin();t!=targets_.rend();++t) {
        const auto& n=nodes_[t->node];if(!t->enabled||!n.resolvedVisible||n.resolvedOpacity<=.01)continue;
        auto p=Projection::viewport(camera*n.world,width,height).unproject(screen);
        if(!p||!polygonContains(t->contour,*p))continue;
        bool clipped=false;
        for(const auto& c:t->clips){auto q=Projection::viewport(camera*nodes_[c.node].world,width,height).unproject(screen);if(!q||!polygonContains(c.contour,*q)){clipped=true;break;}}
        if(!clipped)return t->action;
    }
    return {};
}
}
