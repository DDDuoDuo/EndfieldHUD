#ifdef _WIN32
#include "tools/projection_workspace.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <cmath>
#include <iostream>
namespace {
namespace n=endfield::native;namespace t=endfield::tools;namespace a=endfield::app;namespace c=endfield::core;
unsigned checks{};void check(bool b,const char*s){++checks;if(!b)throw std::runtime_error(s);}
void hr(HRESULT value){winrt::check_hresult(value);}
struct Queue {
 winrt::Windows::System::DispatcherQueueController controller{nullptr};
 Queue(){const DispatcherQueueOptions options{sizeof(DispatcherQueueOptions),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};hr(CreateDispatcherQueueController(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))));}
 ~Queue(){try{auto operation=controller.ShutdownQueueAsync();const auto end=GetTickCount64()+10000;while(operation.Status()==winrt::Windows::Foundation::AsyncStatus::Started){if(GetTickCount64()>end)std::terminate();MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}operation.GetResults();}catch(...){std::terminate();}}
};
void run(const wchar_t*shader){
 Queue queue;a::OverlayHost host;host.create({L"Hidden Projection workspace lifecycle",0,0,1280,800,{}});n::Renderer renderer;
 renderer.initialize(host.hwnd(),1280,800,{n::Driver::warpForTests,shader,n::RenderTarget::composition});
 const BOOL off=FALSE;hr(DwmSetWindowAttribute(static_cast<HWND>(host.hwnd()),DWMWA_USE_HOSTBACKDROPBRUSH,&off,sizeof(off)));
 n::DesktopBackdrop backdrop;backdrop.initialize(host.hwnd(),{1280,800,true,false,.75,.63,0},{false});n::LayerRasterizer raster;
 const n::NotesImageRoute route{static_cast<HWND>(host.hwnd()),WM_APP+250,1};
 n::NativeNotesImageDecoder decoder([](const auto&)->n::NotesImageAccess{throw std::runtime_error("No document read authorized in empty workspace fixture");},route);
 n::NativeNotesImagePlayback images(decoder);n::NativeMediaRequestBroker broker(decoder,images,route);
 {
  t::ProjectionWorkspace workspace(host,renderer,raster,backdrop,broker,{.98,.83,.12,1},.5,.5);
  t::ProjectionPreviewOptions options;options.raster.pixelsPerPoint=2;
  t::ProjectionMediaBindingOptions media;media.resolve=[](const auto&)->n::NotesImageAccess{throw std::runtime_error("Unexpected media read");};media.errorText=[](HRESULT){return std::string("Synthetic error");};
  for(unsigned cycle=0;cycle<3;++cycle){const double start=double(cycle)*2;
   workspace.show(options,media,start,false);
   check(workspace.presented()&&!host.stats().visible&&backdrop.stats().foregroundAttached&&!renderer.sourcePassEnabled(),"Hidden workspace borrows one host/swapchain and suppresses retained HUD pass");
   check(backdrop.stats().panelOpacity==0&&workspace.demand(start).finiteAnimation,"Source entrance starts at final-window zero alpha");
   workspace.render(start+.09,false);check(std::abs(backdrop.stats().panelOpacity-c::CubicTiming{.25,.1,.25,1}.value(.5))<.00001,"Source panel animator curve applies once to whole composition");
   workspace.render(start+.2,false);check(backdrop.stats().panelOpacity==1&&!workspace.demand(start+.2).finiteAnimation,"Empty settled workspace has no animation demand");
   const auto before=raster.stats();const auto gpu=renderer.stats();
   for(unsigned k=0;k<30;++k)workspace.render(start+.21+double(k)*.01,false);
   check(raster.stats().rasterizations==before.rasterizations&&raster.stats().textLayoutsCreated==before.textLayoutsCreated&&renderer.stats().textureUploads==gpu.textureUploads&&renderer.stats().meshUploads==gpu.meshUploads,"Unchanged workspace frames allocate no local raster/texture/mesh work");
   if(!cycle){workspace.pointer({a::PointerKind::down,a::PointerButton::left,100,300},start+.6);workspace.pointer({a::PointerKind::move,a::PointerButton::none,200,330},start+.61);workspace.pointer({a::PointerKind::up,a::PointerButton::left,200,330},start+.62);}
   workspace.render(start+.7,false);check(workspace.model().drawing().strokes().size()==1,"Session ink survives reopening without persisted files");
   workspace.resize({0,0,96,1,0,0});workspace.render(start+.71,false);check(!workspace.demand(start+.71).onScreen,"Zero-size host suspends Projection paint safely");workspace.resize({1280,800,96,1,1280,800});
   workspace.dismiss(start+.8,false);check(workspace.closing()&&!workspace.preview()->active(),"Dismissal immediately cancels interaction/provider demand");workspace.render(start+.88,false);
   check(std::abs(backdrop.stats().panelOpacity-(1-c::CubicTiming{.25,.1,.25,1}.value(.5)))<.00001,"Source dismissal preserves unmultiplied child alpha");
   check(!workspace.dismissalComplete(start+.95)&&workspace.dismissalComplete(start+.97),"Finite dismissal retains source .16-second completion");workspace.close(start+1);
   check(!workspace.presented()&&!backdrop.stats().foregroundAttached&&renderer.sourcePassEnabled()&&renderer.stats().textures==0&&renderer.stats().meshes==0,"Close restores original compositor and releases visible resources");
  }
  workspace.show(options,media,7,false);workspace.resize({0,0,96,1,0,0});workspace.dismiss(7.1,false);
  check(!workspace.demand(7.1).onScreen&&workspace.dismissalDeadline()&&std::abs(*workspace.dismissalDeadline()-7.26)<.00001,"A minimized dismissal supplies the shared owner deadline without frames");
  check(workspace.dismissalComplete(7.27),"The owner can complete dismissal while no display frame is available");workspace.close(7.27);
  check(!workspace.dismissalDeadline(),"Closed Projection leaves no owner deadline");
  options.reduceMotion=true;workspace.show(options,media,8,false);check(backdrop.stats().panelOpacity==1&&!workspace.demand(8).finiteAnimation,"Reduced motion commits source entrance endpoint");workspace.dismiss(8.1,true);check(workspace.dismissalComplete(8.1),"Reduced motion dismisses immediately");workspace.close(8.1);
  check(host.stats().timerArms==0&&!host.stats().visible,"Entire workspace fixture uses no independent timer or visible window");
 }
 decoder.stop();backdrop.reset();renderer.reset();host.destroy();
}
}
int wmain(int argc,wchar_t**argv){const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{hr(com);check(argc==2,"Pass original shader path");run(argv[1]);CoUninitialize();std::cout<<"PASS "<<checks<<" hidden Projection workspace checks\n";return 0;}catch(const winrt::hresult_error&e){if(SUCCEEDED(com))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<winrt::to_string(e.message())<<" HRESULT "<<std::hex<<std::uint32_t(e.code().value)<<'\n';return 1;}catch(const std::exception&e){if(SUCCEEDED(com))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#endif
