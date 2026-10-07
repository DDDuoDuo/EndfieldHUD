#include "core/scene.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace endfield::core;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::vector<Point> square(double half){return {{-half,-half},{-half,half},{half,half},{half,-half}};}
}
int main() try {
    RetainedScene scene;
    const auto root=scene.append("mac.native.center"),note=scene.append("notes.synthetic",root,Matrix4::translation(.2,.1));
    const auto text=scene.append("notes.synthetic.text",note),other=scene.append("clock.synthetic");
    check(scene.resolve()==4,"First scene must resolve all nodes");
    const auto revision=scene.revision();
    check(scene.resolve()==0&&scene.revision()==revision,"Idle scene rebuilt");
    check(!scene.setLocal(note,Matrix4::translation(.2,.1)),"Unchanged transform caused work");
    check(scene.setLocal(note,Matrix4::translation(.3,.1)),"Edited transform was ignored");
    check(scene.resolve()==2,"An edit should resolve only that node and descendants");
    check(std::abs(scene.node(text).world.values[12]-.3)<1e-12,"Descendant did not inherit edited position");
    check(scene.node(other).world==Matrix4{},"Unrelated branch changed");
    scene.setTargets({{"edit",note,square(.3),{},true}});
    for(int x=-30;x<=30;x+=3)for(int y=-30;y<=30;y+=3) {
        auto camera=Matrix4::rotation(x*.01,y*.01,.05);camera.values[3]=.14;camera.values[7]=-.08;
        const auto projection=Projection::viewport(camera*scene.node(note).world,1920,1080);
        for(auto local:square(.15)){
            auto screen=projection.project(local);check(screen.has_value(),"Valid projection failed");
            auto inverse=projection.unproject(*screen);check(inverse.has_value(),"Inverse projection failed");
            check(std::hypot(inverse->x-local.x,inverse->y-local.y)<1e-10,"Draw/hit homographies diverged");
            check(scene.hit(*screen,camera,1920,1080)=="edit","Tilted text target missed");
        }
        check(scene.resolve()==0&&scene.revision()==revision+1,"Camera motion rebuilt retained geometry");
    }
    const auto projection=Projection::viewport(scene.node(note).world,1000,640);
    scene.setTargets({{"edit",note,square(.3),{{note,square(.1)}},true}});
    check(scene.hit(*projection.project({0,0}),{},1000,640)=="edit","Mask rejected center");
    check(!scene.hit(*projection.project({.2,0}),{},1000,640),"Masked content remained interactive");
    scene.setOpacity(root,0);scene.resolve();
    check(!scene.hit(*projection.project({0,0}),{},1000,640),"Concealed content remained interactive");
    scene.setOpacity(root,1);scene.setVisible(root,false);scene.resolve();
    check(!scene.node(text).resolvedVisible,"Ancestor visibility was not inherited");
    scene.setVisible(root,true);scene.resolve();
    scene.setTargets({{"back",note,square(.3),{},true},{"menu",note,square(.2),{},true}});
    check(scene.hit(*projection.project({0,0}),{},1000,640)=="menu","Submenu allowed click through");
    check(!Projection{{1,0,0,0,1,0,0,0,-1}}.project({0,0}),"Behind-camera plane accepted");
    bool rejected=false;try{scene.append("cycle",16384);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Invalid parent accepted");
    check(polygonContains(square(1),{1,1}),"Contour edge should be interactive");
    check(!polygonContains(square(1),{2,1}),"Exterior hit accepted");
    std::cout<<"retained scene: projection, clipping, focus order and idle invalidation passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
