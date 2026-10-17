#include "modules/notes_controls.hpp"
#include "modules/notes_motion.hpp"
#include "native/notes_controls_scene.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Build-only comparison against notes_deletion_reference.swift output. Reads
// only the supplied bounded JSON; no store, window, native input or GPU calls.
using ehud::data::Json;
namespace mod=endfield::modules;
namespace native=endfield::native;
namespace {
std::size_t checks{};
void check(bool value,const std::string&message){++checks;if(!value)throw std::runtime_error(message);}
void number(double a,double b,const std::string&where,double tolerance=1e-7){check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=tolerance,where);}
void numericArray(const Json&a,const Json&b,const std::string&where,double tolerance=1e-7){
    check(a.isArray()&&b.isArray()&&a.array().size()==b.array().size(),where+" size");
    for(std::size_t n=0;n<a.array().size();++n){if(a.array()[n].isArray())numericArray(a.array()[n],b.array()[n],where,tolerance);else number(a.array()[n].number(),b.array()[n].number(),where,tolerance);}
}
void color(const Json&a,const Json&b,const std::string&where){check(a.isNull()==b.isNull(),where+" presence");if(!a.isNull())numericArray(a["sRGB"],b["sRGB"],where,1e-6);}
void tree(const Json&a,const Json&b,const std::string&where,double offsetX=0,double offsetY=0){
    check(a["kind"]==b["kind"],where+" kind");numericArray(a["bounds"],b["bounds"],where+" bounds");
    auto frame=a["frame"].array();frame[0]=frame[0].number()-offsetX;frame[1]=frame[1].number()-offsetY;
    numericArray(frame,b["frame"],where+" frame");number(a["opacity"].number(),b["opacity"].number(),where+" opacity");
    if(a["kind"].string()=="shape"){
        const auto&x=a["shape"];const auto&y=b["shape"];
        for(const auto key:{"lineWidth","miterLimit"})number(x[key].number(),y[key].number(),where+" "+key);
        for(const auto key:{"lineCap","lineJoin","fillRule"})check(x[key]==y[key],where+" "+key);
        color(x["fillColor"],y["fillColor"],where+" fill");color(x["strokeColor"],y["strokeColor"],where+" stroke");
        check(x["path"].array().size()==y["path"].array().size(),where+" path length");
        for(std::size_t n=0;n<x["path"].array().size();++n){const auto&p=x["path"].array()[n];const auto&q=y["path"].array()[n];check(p["op"]==q["op"],where+" command");numericArray(p["points"],q["points"],where+" path points");}
    }
    check(a["children"].array().size()==b["children"].array().size(),where+" child count");
    for(std::size_t n=0;n<a["children"].array().size();++n)tree(a["children"].array()[n],b["children"].array()[n],where+"/"+std::to_string(n));
}
void run(const char*filename){
    std::ifstream f(filename,std::ios::binary|std::ios::ate);check(bool(f),"Explicit source reference opens");const auto size=f.tellg();check(size>0&&size<=1024*1024,"Source reference fits 1 MiB bound");
    std::string text(static_cast<std::size_t>(size),'\0');f.seekg(0);f.read(text.data(),size);check(bool(f),"Source reference read completely");const auto source=Json::parse(text,1024*1024);
    check(source["schemaVersion"].integer()==1&&source["cases"].array().size()==8,"All eight original scenarios present");
    for(const auto&c:source["cases"].array()){
        const auto name=c["name"].string();mod::NotesControls controls;mod::NotesControlsInput input;input.kind=mod::NotesControlsKind::deletion;input.dark=c["dark"].boolean();
        for(std::size_t n=0;n<4;++n)input.accent[n]=c["accent"].array()[n].number();controls.update(input);
        const auto&actual=c["actions"].array();const auto&ws=c["workspaceBounds"].array();const auto&card=c["displayedCard"].array();
        const double x=std::min(ws[0].number()+ws[2].number()-56,std::max(ws[0].number(),card[0].number()+card[2].number()-59));
        const double y=std::min(ws[1].number()+ws[3].number()-25,std::max(ws[1].number(),card[1].number()+27));
        check(actual.size()==2&&controls.actions().size()==2,name+" two original actions");
        for(std::size_t n=0;n<2;++n){const auto&a=actual[n];const auto&b=controls.actions()[n];
            check(a["id"].string()==b.id&&a["label"].string()==b.label,name+" action ID/AX label");
            numericArray(a["rect"],Json::Array{x+b.rect.x,y+b.rect.y,b.rect.width,b.rect.height},name+" original hit rectangle");
            tree(c["root"]["children"].array()[n],controls.artwork()["children"].array()[n],name+"/"+b.id,x-ws[0].number(),y-ws[1].number());
        }
        check(!controls.actionAt({28,12}),name+" source gap stays inactive");
        const bool reduced=c["reduceMotion"].boolean();const auto&animation=c["animation"];
        check(animation.isNull()==reduced,name+" reduced motion removes animation");
        if(!reduced){
            check(animation["keyPath"].string()=="transform.translation.y",name+" exact transform channel");
            number(animation["duration"].number(),.16,name+" source duration");number(animation["from"].number(),-6,name+" source start");number(animation["to"].number(),0,name+" source end");
            numericArray(animation["timingControlPoints"],Json::Array{Json::Array{0,0},Json::Array{0,0},Json::Array{.58,1},Json::Array{1,1}},name+" native ease-out control points");
            for(double t:{0.,.04,.08,.12,.16}){const auto sample=mod::notesDeletionMenuMotion(t);check(sample.active==(t<.16),name+" finite active interval");check(sample.x==0&&sample.scale==1&&sample.opacity==1,name+" reveal changes only Y");}
        }else{const auto sample=mod::notesDeletionMenuMotion(0,true);check(!sample.active&&sample.y==0&&sample.opacity==1,name+" reduced motion settled pose");}
        // Source root groups overlapping plate/highlight/glyph opacity. The
        // root's workspace bounds/position are deliberately normalized by the
        // portable owner, but its compositing requirement must survive.
        check(c["root"]["allowsGroupOpacity"].boolean(),name+" source root requires grouped opacity");
        check(native::prepareNotesControlsScene(controls).requiresGroupOpacity,name+" native plan preserves grouped confirmation opacity");
    }
    std::cout<<Json(Json::Object{{"status","pass"},{"checks",static_cast<std::int64_t>(checks)},{"cases",8},
        {"scope","original model paths, two action rectangles, colors, child paint order, glyph strokes, group contract and CA reveal descriptor"},
        {"notVerified",Json::Array{"presentation pixels","cross-platform antialiasing","native input dispatch"}}}).encode()<<'\n';
}
}
int main(int argc,char**argv){try{if(argc!=2)throw std::invalid_argument("Usage: check_notes_deletion_reference confirmation.json");run(argv[1]);return 0;}catch(const std::exception&e){std::cerr<<"Notes deletion reference failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
