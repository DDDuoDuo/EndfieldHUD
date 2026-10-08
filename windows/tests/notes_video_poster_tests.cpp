#include "native/notes_image_decoder.hpp"
#include "core/shell_packet.hpp"
#ifdef _WIN32
#include <array>
#include <fstream>
#include <iostream>
#include <objbase.h>
namespace native=endfield::native;namespace modules=endfield::modules;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
struct Window {HWND value{CreateWindowExW(0,L"STATIC",L"Owned first video frame",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};Window(){check(value&&!IsWindowVisible(value),"Poster fixture owns only a hidden window");}~Window(){DestroyWindow(value);}};
std::string pathUTF8(const std::filesystem::path&p){const auto value=p.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
void run(const std::filesystem::path&path){
    std::ifstream file(path,std::ios::binary);std::array<std::uint8_t,1736>bytes{};file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    check(file&&file.peek()==std::char_traits<char>::eof()&&endfield::core::packet::sha256(bytes)=="a388d90f0a024dea0735f15322269e740ccb725977b58681e32591cfeb4807a1","Decode only the pinned authored silent red/blue movie");
    Window window;constexpr UINT message=WM_APP+257;native::NativeNotesImageDecoder decoder([](const auto&r){return native::NotesImageAccess{std::filesystem::u8path(r.path),r.accessLease};},{window.value,message,19});
    const std::array request{native::NotesImageRequest{"owned.poster",pathUTF8(std::filesystem::absolute(path)),1,32,true,{},false,true}};
    auto invalid=request;invalid[0].allowVideoInspection=true;bool rejected{};try{decoder.setVisible(invalid);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Poster and metadata-only requests cannot be confused");
    rejected=false;try{decoder.setInspections(request);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Poster job does not replace the independent import batch");
    decoder.setVisible(request);std::vector<native::NotesImageCompletion>result;const auto deadline=GetTickCount64()+10000;
    while(result.empty()){result=decoder.drain(19);if(!result.empty())break;const auto time=GetTickCount64();check(time<deadline,"Shared media worker finishes first-frame extraction");
        MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(deadline-time),QS_POSTMESSAGE,MWMO_INPUTAVAILABLE);MSG event{};while(PeekMessageW(&event,window.value,message,message,PM_REMOVE))check(event.wParam==19&&event.lParam==0,"Poster completion carries only current route generation");}
    const auto&completion=result.front();check(SUCCEEDED(completion.result)&&completion.info&&completion.frame,"SourceReader returns one immutable bounded poster");
    check(completion.info->kind==modules::NotesMediaKind::image&&completion.info->frameCount==1&&completion.info->duration==0,"Poster is one still image, not a playback state or clock");
    const auto&frame=*completion.frame;check(frame.width==32&&frame.height==32&&frame.index==0&&frame.straightRGBA.size()==32*32*4,"Poster negotiated exact bounded output dimensions");
    const auto at=(16*32+16)*4;std::cout<<"Poster RGBA="<<unsigned(frame.straightRGBA[at])<<','<<unsigned(frame.straightRGBA[at+1])<<','<<unsigned(frame.straightRGBA[at+2])<<','<<unsigned(frame.straightRGBA[at+3])<<'\n';
    check(frame.straightRGBA[at]>220&&frame.straightRGBA[at+1]<40&&frame.straightRGBA[at+2]<40&&frame.straightRGBA[at+3]==255,"Paused-independent SourceReader extracts the red time-zero frame");
    check(decoder.stats().decodes==1&&decoder.stats().liveFrameBytes==4096,"Only one bounded frame is decoded and budgeted");decoder.hide();const auto worker=decoder.duplicateWorkerHandle();decoder.stop();check(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0,"Shared worker releases reader/file lease before explicit shutdown completion");CloseHandle(worker);
    check(completion.frame->straightRGBA[at]>220&&!IsWindowVisible(window.value),"Borrowed poster survives worker cleanup without visible UI, playback or audio");
}
}
int wmain(int argc,wchar_t**argv){const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(apartment))return 1;int result{};try{check(argc==2,"Pass only the authored silent MP4 fixture");run(argv[1]);std::cout<<"PASS "<<checks<<" worker video poster checks\n";}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#else
int main(){return 0;}
#endif
