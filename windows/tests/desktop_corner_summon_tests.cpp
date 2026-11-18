#include "app/desktop_corner_summon.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdexcept>
using namespace endfield::app;
int main(){try{
    int checks=0;auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("Desktop corner gesture regression");};
    check(desktopCornerContains(10,1000,0,0,1920,1040));check(!desktopCornerContains(10,1060,0,0,1920,1040));
    check(!desktopCornerContains(200,1000,0,0,1920,1040));check(desktopCornerContains(-1900,1000,-1920,0,0,1040));
    using E=DesktopCornerGesture::Event;DesktopCornerGesture gesture;
    auto e=[&](E kind,int x,int y,unsigned time,bool desktop=true){return gesture.event(kind,x,y,time,desktop,500,4,4);};
    check(!e(E::down,10,1000,10));check(!e(E::up,10,1000,50));check(!e(E::down,11,1000,200));check(e(E::up,11,1000,240));
    check(!e(E::down,11,1000,300));check(!e(E::up,11,1000,340)); // triple click is not two summons
    e(E::cancel,0,0,350);e(E::down,10,1000,400);e(E::move,50,1000,410);check(!e(E::up,10,1000,420));
    e(E::down,10,1000,450);check(!e(E::up,10,1000,460));e(E::down,10,1000,1000);check(!e(E::up,10,1000,1010));
    e(E::down,10,1000,1100,false);check(!e(E::up,10,1000,1110,false));e(E::down,10,1000,1200);check(!e(E::up,10,1000,1210));
    e(E::down,30,1000,1300);check(!e(E::up,30,1000,1310)); // distant clicks do not pair
    e(E::cancel,0,0,0);e(E::down,10,1000,0xfffffff0u);e(E::up,10,1000,0xfffffff5u);e(E::down,10,1000,50);check(e(E::up,10,1000,60));
#ifdef _WIN32
    const auto window=CreateWindowExW(0,L"STATIC",L"Desktop corner test",0,0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(window!=nullptr);
    {DesktopCornerSummon hook(window,WM_APP+17);}
    check(DestroyWindow(window)!=0);
#endif
    std::cout<<"Desktop corner: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
