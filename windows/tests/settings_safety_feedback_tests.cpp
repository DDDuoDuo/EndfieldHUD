// The v1.2.0 card's .12 s button feedback (HUDScaleSafetyView, measured from
// the actual Mac source at ca04f14, tests/fixtures/settings-safety-feedback.json):
// the curve and colour interpolation the Windows-only Import macOS data card
// keeps (modules/settings_safety.hpp), and its centred plane transform.
#include "modules/settings_safety.hpp"
#include "core/data/json.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
using namespace endfield;using namespace modules;using ehud::data::Json;
namespace {unsigned checks{};double maximum{};void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}void close(double a,double b,double tolerance,const char*s){const auto delta=std::abs(a-b);maximum=std::max(maximum,delta);check(delta<=tolerance,s);}
SettingsSafetyColor color(const Json&v){return{v.array()[0].number(),v.array()[1].number(),v.array()[2].number(),v.array()[3].number()};}
void same(const SettingsSafetyColor&a,const Json&b){for(unsigned n=0;n<4;++n)close(a[n],b.array()[n].number(),n<3?1.3e-7:2e-12,"Actual CA color interpolation");}
void run(const Json&fixture){const SettingsSafetyColor identical{250./255,212./255,31./255,.9};for(double phase:{-.1,0.,.01,.3,.99,1.,2.})check(settingsSafetyInterpolateColor(identical,identical,phase)==identical,"Unanimated source color remains bit-identical without a phantom repaint");check(fixture["actualSource"].boolean()&&fixture["ownOffscreenNonactivatingPanel"].boolean()&&!fixture["screenCaptured"].boolean(),"Actual isolated source feedback oracle");
 for(const auto&record:fixture["records"].array()){const auto&samples=record["samples"].array();const auto first=samples.front(),last=samples.back();
  for(const auto&animation:record["animations"].array()){close(animation["duration"].number(),.12,0,"Actual implicit transaction duration");const auto&t=animation["timing"].array();close(t[0].array()[0].number(),.25,0,"Actual default timing x1");close(t[0].array()[1].number(),float(.1),0,"Actual default timing y1 Float boundary");close(t[1].array()[0].number(),.25,0,"Actual default timing x2");close(t[1].array()[1].number(),1,0,"Actual default timing y2");}
  SettingsSafetyColor fillFrom=color(first["fill"]),fillTo=fillFrom,strokeFrom=color(first["stroke"]),strokeTo=strokeFrom;double widthFrom=first["width"].number(),widthTo=widthFrom;
  for(const auto&animation:record["animations"].array()){const auto path=animation["path"].string();if(path=="fillColor"){fillFrom=color(animation["from"]);fillTo=color(animation["to"]);}else if(path=="strokeColor"){strokeFrom=color(animation["from"]);strokeTo=color(animation["to"]);}else if(path=="lineWidth"){widthFrom=animation["from"].number();widthTo=animation["to"].number();}}
  for(const auto&row:samples){const auto phase=row["phase"].number();same(settingsSafetyInterpolateColor(fillFrom,fillTo,phase),row["fill"]);same(settingsSafetyInterpolateColor(strokeFrom,strokeTo,phase),row["stroke"]);close(widthFrom+(widthTo-widthFrom)*settingsSafetyFeedbackProgress(phase),row["width"].number(),2e-12,"Actual CA scalar line-width interpolation");}
  (void)last;}
 auto transform=core::Matrix4::translation(600,400)*core::Matrix4::scale(2,2);const auto centered=SettingsSafety::centeredSourceTransform(transform,2);for(unsigned n=0;n<16;++n)close(centered.values[n],core::Matrix4{}.values[n],2e-6,"Source centered projection removes unsafe scale/position");}
}
int main(int argc,char**argv){try{check(argc==2,"Pass original Settings feedback fixture");std::ifstream in(argv[1]);check(in.good(),"Read fixture");run(Json::parse(std::string{std::istreambuf_iterator<char>(in),{}}));std::cout<<"Settings safety feedback: "<<checks<<" original CA checks; max component error "<<maximum<<'\n';}catch(const std::exception&e){std::cerr<<"Settings safety feedback after "<<checks<<": "<<e.what()<<'\n';return 1;}}
