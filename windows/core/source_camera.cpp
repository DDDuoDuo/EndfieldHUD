#include "core/source_camera.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace endfield::core::source {
namespace {
void need(bool value,const char* message){if(!value)throw std::invalid_argument(message);}
Quaternion normalize(Quaternion q){auto value=normalizedQuaternion(q);need(value.has_value(),"Invalid source quaternion");return *value;}
Matrix4 rotationMatrix(Quaternion q){auto value=quaternionMatrix(q);need(value.has_value(),"Invalid source rotation");return *value;}
Quaternion slerp(Quaternion a,Quaternion b,double t){auto value=slerpQuaternion(a,b,t);need(value.has_value(),"Invalid gyro interpolation");return *value;}
double numeric(const Json& j){need(j.isNumber(),"Missing source camera number");const auto n=j.number();need(std::isfinite(n),"Nonfinite source camera number");return n;}
bool flag(const Json&j,bool fallback=false){return j.isNull()?fallback:j.isBool()?j.boolean():numeric(j)!=0;}
Vec2 vec2(const Json&j){return {numeric(j["x"]),numeric(j["y"])};}
Vec3 vec3(const Json&j){return {numeric(j["x"]),numeric(j["y"]),numeric(j["z"])};}
Quaternion quat(const Json&j){return normalize({numeric(j["x"]),numeric(j["y"]),numeric(j["z"]),numeric(j["w"])});}
bool finite(Vec3 v){return std::all_of(v.begin(),v.end(),[](double x){return std::isfinite(x);});}
void sizeCheck(Vec2 s){need(std::isfinite(s[0])&&std::isfinite(s[1])&&s[0]>0&&s[1]>0,"Invalid source screen dimensions");}
std::string id(const Json&j){need(j["cab"].isString()&&j["path_id"].isString(),"Missing runtime camera identity");return j["cab"].string()+":"+j["path_id"].string();}
const Json& unique(const Json::Array& rows,const std::function<bool(const Json&)>& predicate){const Json* value{};for(const auto&r:rows)if(predicate(r)){need(!value,"Ambiguous runtime camera binding");value=&r;}need(value,"Missing runtime camera binding");return *value;}
std::string text(const Json&j){return j.isString()?j.string():std::string{};}
const Json& gyroObject(const Json& root){return unique(root.array(),[](const Json&row){return text(row["script"]["m_ClassName"])=="UIGyroscopeEffect";});}
ScalarCurve scalar(const Json& value){
    auto slope=[](const Json&v){if(v.isNumber())return numeric(v);if(v.string()=="Infinity")return std::numeric_limits<double>::infinity();if(v.string()=="-Infinity")return -std::numeric_limits<double>::infinity();throw std::invalid_argument("Missing gyro slope");};
    std::vector<ScalarKey> keys;
    for(const auto&k:value["m_Curve"].array()){
        const auto mode=numeric(k["weightedMode"]);need(mode>=0&&mode<=3&&std::floor(mode)==mode,"Invalid gyro weight mode");
        keys.push_back({numeric(k["time"]),numeric(k["value"]),slope(k["inSlope"]),slope(k["outSlope"]),static_cast<unsigned>(mode),k["inWeight"].isNull()?1.0/3:numeric(k["inWeight"]),k["outWeight"].isNull()?1.0/3:numeric(k["outWeight"])});
    }return ScalarCurve(std::move(keys));
}
Matrix4 toFloat(Matrix4 m){for(auto&v:m.values)v=static_cast<float>(v);return m;}
Matrix4 multiplyFloat(const Matrix4&a,const Matrix4&b){Matrix4 m;m.values.fill(0);for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r){float v=0;for(unsigned k=0;k<4;++k)v+=float(a.values[k*4+r])*float(b.values[c*4+k]);m.values[c*4+r]=v;}return m;}
}
Matrix4 inverseSourceMatrix(const Matrix4& matrix){
    need(matrix.finite(),"Nonfinite camera matrix");double values[4][8]{};
    for(unsigned r=0;r<4;++r){for(unsigned c=0;c<4;++c)values[r][c]=matrix.values[c*4+r];values[r][r+4]=1;}
    for(unsigned c=0;c<4;++c){unsigned pivot=c;for(unsigned r=c+1;r<4;++r)if(std::abs(values[r][c])>std::abs(values[pivot][c]))pivot=r;need(std::abs(values[pivot][c])>1e-15,"Singular source camera matrix");
        for(unsigned k=0;k<8;++k)std::swap(values[c][k],values[pivot][k]);const auto d=values[c][c];for(auto&v:values[c])v/=d;
        for(unsigned r=0;r<4;++r)if(r!=c){const auto factor=values[r][c];for(unsigned k=0;k<8;++k)values[r][k]-=factor*values[c][k];}}
    Matrix4 result;for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)result.values[c*4+r]=values[r][c+4];need(result.finite(),"Invalid inverse source camera");return result;
}
GyroDefinition GyroDefinition::fromJson(const Json&j){
    need(numeric(j["ease"])==6,"Unsupported source gyro ease");
    GyroDefinition result{flag(j["enableDetect"]),scalar(j["x"]["valueCurve"]),scalar(j["y"]["valueCurve"]),numeric(j["x"]["maxAngle"]),numeric(j["y"]["maxAngle"]),numeric(j["time"])};
    need(result.duration>0,"Invalid source gyro duration");return result;
}
Vec3 GyroDefinition::targetEuler(Vec2 mouse,Vec2 screen,bool detect) const {
    sizeCheck(screen);need(std::isfinite(mouse[0])&&std::isfinite(mouse[1]),"Invalid gyro pointer");if(!enabled||!detect)return {};
    const float w=float(screen[0]),h=float(screen[1]);const float x=(std::clamp(float(mouse[0]),0.f,w)-w*.5f)/(w*.5f),y=(std::clamp(float(mouse[1]),0.f,h)-h*.5f)/(h*.5f);
    const auto pitch=pitchCurve.sample(y),yaw=yawCurve.sample(x);need(pitch.has_value()&&yaw.has_value(),"Cannot sample source gyro");
    return {double(float(*pitch)*float(maxPitch)),double(float(*yaw)*float(maxYaw)),0};
}
Vec3 GyroDefinition::desktopTarget(Vec2 mouse,Vec2 screen,double parallax,double perspective,bool detect) const {
    need(std::isfinite(parallax)&&std::isfinite(perspective),"Invalid desktop tilt intensity");auto value=targetEuler({mouse[0],screen[1]-mouse[1]},screen,detect);
    const auto factor=1.25*std::clamp(parallax,0.,2.)*std::clamp(perspective,0.,2.);for(auto&v:value)v*=factor;return value;
}
SourceCamera::SourceCamera(const Json& runtime,double referenceScale):gyro_(GyroDefinition::fromJson(gyroObject(runtime)["data"])),referenceScale_(referenceScale){
    need(std::isfinite(referenceScale)&&referenceScale>0,"Invalid reference resolution scale");const auto&rows=runtime.array();need(rows.size()<=4096,"Runtime camera hierarchy exceeds bounds");
    const auto&g=gyroObject(runtime);const auto cab=g["cab"].string(),go=g["data"]["m_GameObject"]["m_PathID"].string();
    const auto&root=unique(rows,[&](const Json&r){return r["cab"].string()==cab&&r["type"].string()=="RectTransform"&&r["data"]["m_GameObject"]["m_PathID"].string()==go;});
    const auto&helper=unique(rows,[&](const Json&r){return text(r["script"]["m_ClassName"])=="UICanvasScaleHelper"&&r["cab"].string()==cab&&r["data"]["m_GameObject"]["m_PathID"].string()==go;});
    need(flag(helper["data"]["m_Enabled"],true)&&flag(g["data"]["m_Enabled"],true),"Disabled source world configuration");
    const auto&camera=unique(rows,[](const Json&r){return r["type"].string()=="Camera"&&flag(r["data"]["m_Enabled"],true);});
    const auto&ct=unique(rows,[&](const Json&r){return r["type"].string()=="Transform"&&r["cab"]==camera["cab"]&&r["data"]["m_GameObject"]["m_PathID"]==camera["data"]["m_GameObject"]["m_PathID"];});
    const auto&data=camera["data"];const auto&viewport=data["m_NormalizedViewPortRect"];
    need(!flag(data["orthographic"])&&numeric(viewport["x"])==0&&numeric(viewport["y"])==0&&numeric(viewport["width"])==1&&numeric(viewport["height"])==1&&vec2(data["m_LensShift"])==Vec2{},"Unsupported source camera projection");
    std::map<std::string,const Json*,std::less<>> transforms;for(const auto&r:rows)if(r["type"].string()=="Transform"||r["type"].string()=="RectTransform")need(transforms.emplace(id(r),&r).second,"Duplicate runtime transform");
    auto parent=[](const Json&r)->std::optional<std::string>{const auto&p=r["data"]["m_Father"];need(p["m_PathID"].isString()&&numeric(p["m_FileID"])==0,"Cross-asset camera parent");return p["m_PathID"].string()=="0"?std::nullopt:std::optional(r["cab"].string()+":"+p["m_PathID"].string());};
    std::set<std::string> visited;
    std::function<Matrix4(const Json&)> world=[&](const Json&r){const auto key=id(r);need(visited.insert(key).second,"Cyclic runtime hierarchy");const auto&d=r["data"];const auto pos=vec3(d["m_LocalPosition"]),scale=vec3(d["m_LocalScale"]);
        auto local=Matrix4::translation(pos[0],pos[1],pos[2])*rotationMatrix(quat(d["m_LocalRotation"]))*Matrix4::scale(scale[0],scale[1],scale[2]);
        if(const auto p=parent(r)){const auto it=transforms.find(*p);need(it!=transforms.end(),"Missing runtime ancestor");local=world(*it->second)*local;}visited.erase(key);return local;};
    if(const auto p=parent(root)){const auto it=transforms.find(*p);need(it!=transforms.end(),"Missing world parent");worldParent_=world(*it->second);}
    cameraWorld_=world(ct);view_=inverseSourceMatrix(cameraWorld_);worldRootID_=id(root);rootPosition_=vec3(root["data"]["m_LocalPosition"]);rootRotation_=quat(root["data"]["m_LocalRotation"]);
    fieldOfView_=numeric(data["field of view"]);near_=numeric(data["near clip plane"]);far_=numeric(data["far clip plane"]);need(fieldOfView_>0&&fieldOfView_<180&&near_>0&&far_>near_,"Invalid camera clipping/FOV");
}
double SourceCamera::runtimeFieldOfView(Vec2 screen) const {
    sizeCheck(screen);const auto aspect=screen[0]/screen[1],reference=double(std::bit_cast<float>(0x3fe38e39u));if(aspect>=reference)return fieldOfView_;
    const auto pi=std::numbers::pi;const float horizontal=float(2*std::atan(std::tan(fieldOfView_*pi/360)*reference)*180/pi);
    return double(float(2*std::atan(std::tan(double(horizontal)*pi/360)/double(float(aspect)))*180/pi));
}
CameraLayout SourceCamera::layout(Vec2 screen) const {
    const auto fov=runtimeFieldOfView(screen);const auto& m=worldParent_.values;const float z=std::abs(float(m[2]*rootPosition_[0]+m[6]*rootPosition_[1]+m[10]*rootPosition_[2]+m[14]));
    const float radians=((float(fov)*.5f)/180)*std::numbers::pi_v<float>,half=float(std::tan(double(radians)))*z,height=half+half,aspect=float(screen[0])/float(screen[1]);
    const float standardW=1920*float(referenceScale_),standardH=1080*float(referenceScale_),reference=std::bit_cast<float>(0x3fe38e39u);
    const float width=aspect>reference?standardH*aspect:standardW,canvasH=aspect>reference?standardH:standardW/aspect,scale=aspect>reference?height/standardH:(aspect*height)/standardW;
    need(std::isfinite(width)&&std::isfinite(canvasH)&&std::isfinite(scale)&&scale>0,"Invalid source world canvas scale");return {{width,canvasH},scale,aspect,height,fov};
}
CameraFrame SourceCamera::frame(Vec2 screen,std::optional<Quaternion> rotation) const {
    const auto l=layout(screen);const auto y=1/std::tan(l.verticalFieldOfViewDegrees*std::numbers::pi/360),z=far_/(far_-near_);
    Matrix4 projection;projection.values={y/(screen[0]/screen[1]),0,0,0, 0,y,0,0, 0,0,z,1, 0,0,-near_*z,0};
    const auto world=worldParent_*Matrix4::translation(rootPosition_[0],rootPosition_[1],rootPosition_[2])*rotationMatrix(rotation.value_or(rootRotation_))*Matrix4::scale(l.scale,l.scale,l.scale);
    return {view_,projection,world,l};
}
CameraFrame SourceCamera::desktopFrame(Vec2 screen,const Quaternion&rotation,Vec2 offset,double scale) const {
    need(std::isfinite(offset[0])&&std::isfinite(offset[1])&&std::isfinite(scale)&&scale>0,"Invalid desktop HUD placement");auto f=frame(screen,rotation);
    f.worldRoot=f.worldRoot*Matrix4::translation(f.layout.canvasSize[0]*offset[0],-f.layout.canvasSize[1]*offset[1])*Matrix4::scale(scale,scale,scale);return f;
}
GPUCamera SourceCamera::gpu(const CameraFrame& frame) const {
    GPUCamera out;out.projection=toFloat(frame.projection);for(unsigned c=0;c<4;++c)out.projection.values[c*4+1]*=-1;
    const auto view=toFloat(frame.view);out.viewProjection=multiplyFloat(out.projection,view);auto untranslated=view;for(unsigned r=0;r<4;++r)untranslated.values[12+r]=r==3?1:0;
    out.viewNoTranslationProjection=multiplyFloat(out.projection,untranslated);out.inverseView=toFloat(cameraWorld_*Matrix4::scale(1,1,-1));out.worldPosition={cameraWorld_.values[12],cameraWorld_.values[13],cameraWorld_.values[14]};
    const auto inv=inverseSourceMatrix(out.projection);const auto probeY=inv.values[5]+inv.values[13],probeW=inv.values[7]+inv.values[15];need(std::isfinite(probeY/probeW)&&probeW!=0,"Invalid UI projection probe");out.uiProjectionParameters={probeY/probeW<0?-1.f:1.f,float(near_),float(far_),1/float(far_)};return out;
}
Quaternion SourceCamera::eulerQuaternion(Vec3 degrees){need(finite(degrees),"Invalid gyro Euler angles");const auto half=std::numbers::pi/360;const Quaternion x{std::sin(degrees[0]*half),0,0,std::cos(degrees[0]*half)},y{0,std::sin(degrees[1]*half),0,std::cos(degrees[1]*half)},z{0,0,std::sin(degrees[2]*half),std::cos(degrees[2]*half)};return normalize(multiplyQuaternion(multiplyQuaternion(y,x),z));}
GyroMotion::GyroMotion(Quaternion initial):start_(normalize(initial)),end_(start_){}
Quaternion GyroMotion::rotation(double time) const {need(std::isfinite(time),"Invalid gyro animation time");if(!animating_||duration_<=0)return end_;const auto t=std::clamp((time-startedAt_)/duration_,0.,1.);return slerp(start_,end_,t*(2-t));}
bool GyroMotion::retarget(Vec3 degrees,double time,double duration,bool reduce){need(std::isfinite(time)&&std::isfinite(duration)&&duration>0&&finite(degrees),"Invalid gyro retarget");double distance{};for(unsigned i=0;i<3;++i)distance+=(degrees[i]-lastEuler_[i])*(degrees[i]-lastEuler_[i]);need(std::isfinite(distance),"Invalid gyro target");const bool changed=distance>=double(std::bit_cast<float>(0x2edbe6feu));if(!changed&&!reduce)return false;start_=rotation(time);end_=SourceCamera::eulerQuaternion(degrees);lastEuler_=degrees;startedAt_=time;duration_=duration;animating_=!reduce;if(reduce)start_=end_;return changed;}
void GyroMotion::finishIfNeeded(double time){need(std::isfinite(time),"Invalid gyro finish time");if(animating_&&time>=startedAt_+duration_){animating_=false;start_=end_;}}
void GyroMotion::stop(double time){start_=end_=rotation(time);animating_=false;}
} // namespace endfield::core::source
