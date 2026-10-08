#include "native/notes_image_decoder.hpp"
#include "core/shell_packet.hpp"
#ifdef _WIN32
#include <array>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <system_error>
namespace native=endfield::native;namespace modules=endfield::modules;
namespace endfield::native::detail {std::unique_ptr<NativeNotesImageDecoder::Sequence>makeNotesVideoPosterSequence(const NotesImageRequest&,NotesImageAccess);}
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
struct Window {HWND value{CreateWindowExW(0,L"STATIC",L"Owned first video frame",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};Window(){check(value&&!IsWindowVisible(value),"Poster fixture owns only a hidden window");}~Window(){DestroyWindow(value);}};
std::string pathUTF8(const std::filesystem::path&p){const auto value=p.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
// Only this isolated test prints failures. All operation labels below come
// from fixed source strings, and the accepted file is SHA-pinned above.
template<class F>auto diagnostic(F&&operation){try{return operation();}catch(const std::system_error&e){std::cerr<<"Poster stage HRESULT="<<e.code().value()<<" operation="<<e.what()<<'\n';throw;}catch(const std::exception&e){std::cerr<<"Poster validation="<<e.what()<<'\n';throw;}}
class DiagnosticSequence final:public native::NativeNotesImageDecoder::Sequence {
    std::unique_ptr<native::NativeNotesImageDecoder::Sequence>sequence_;
public:DiagnosticSequence(const native::NotesImageRequest&r,native::NotesImageAccess access):sequence_(diagnostic([&]{return native::detail::makeNotesVideoPosterSequence(r,std::move(access));})){}
    const native::NotesImageInfo&info()const override{return sequence_->info();}
    native::NotesImageFrame decode(unsigned index)override{return diagnostic([&]{return sequence_->decode(index);});}
};
// Microsoft H.264 decoding requires at least 48×48 native pixels; the 64×64
// positive fixture deliberately requests a bounded 32×32 poster.
// Failure-only bounded discriminator. Each reader uses only the hash-verified
// 64px fixture, emits no audio and owns no playback clock or device. This does
// not substitute a fallback for the production result under test.
void diagnosePipeline(const std::filesystem::path&path){
    using Microsoft::WRL::ComPtr;
    const auto startup=MFStartup(MF_VERSION,MFSTARTUP_NOSOCKET);
    if(FAILED(startup)){std::cerr<<"Poster probe startup="<<startup<<'\n';return;}
    for(unsigned mode=0;mode<6;++mode){
        const char*names[]{"native","NV12","RGB32-basic","RGB32-bounded","RGB32-rate","RGB32-rate-seek"};
        HRESULT hr=S_OK;ComPtr<IMFAttributes>attributes;ComPtr<IMFSourceReader>reader;ComPtr<IMFMediaType>type,nativeType;
        if(mode>=2){hr=MFCreateAttributes(&attributes,1);if(SUCCEEDED(hr))hr=attributes->SetUINT32(mode==2?MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING:MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE);}
        if(SUCCEEDED(hr))hr=MFCreateSourceReaderFromURL(path.c_str(),attributes.Get(),&reader);
        if(SUCCEEDED(hr))hr=reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
        if(SUCCEEDED(hr))hr=reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE);
        if(SUCCEEDED(hr))hr=reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&nativeType);
        if(mode&&SUCCEEDED(hr)){
            hr=MFCreateMediaType(&type);
            if(SUCCEEDED(hr))hr=type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
            if(SUCCEEDED(hr))hr=type->SetGUID(MF_MT_SUBTYPE,mode==1?MFVideoFormat_NV12:MFVideoFormat_RGB32);
            if(mode>=3){
                if(SUCCEEDED(hr))hr=MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,32,32);
                if(SUCCEEDED(hr))hr=MFSetAttributeRatio(type.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
                if(SUCCEEDED(hr))hr=type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
                if(mode>=4){UINT32 numerator{},denominator{};if(SUCCEEDED(hr))hr=MFGetAttributeRatio(nativeType.Get(),MF_MT_FRAME_RATE,&numerator,&denominator);if(SUCCEEDED(hr))hr=MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,numerator,denominator);}
            }
            if(SUCCEEDED(hr))hr=reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type.Get());
        }
        if(mode==5&&SUCCEEDED(hr)){PROPVARIANT zero{};zero.vt=VT_I8;zero.hVal.QuadPart=0;hr=reader->SetCurrentPosition(GUID_NULL,zero);}
        std::cerr<<"Poster probe "<<names[mode]<<" setup="<<hr<<'\n';
        if(FAILED(hr))continue;
        for(unsigned read=0;read<4;++read){ComPtr<IMFSample>sample;DWORD flags{},stream{},bytes{};LONGLONG time{};
            hr=reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&stream,&flags,&time,&sample);
            if(sample)sample->GetTotalLength(&bytes);
            std::cerr<<"Poster probe "<<names[mode]<<" read="<<read<<" hr="<<hr<<" flags="<<flags<<" stream="<<stream<<" time="<<time<<" bytes="<<bytes<<'\n';
            if(FAILED(hr)||sample||(flags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_ENDOFSTREAM)))break;
        }
    }
    MFShutdown();
}
void run(const std::filesystem::path&path){
    std::ifstream file(path,std::ios::binary);std::array<std::uint8_t,1701>bytes{};file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    check(file&&file.peek()==std::char_traits<char>::eof()&&endfield::core::packet::sha256(bytes)=="0de10d53a6946351f127b78b738c68658708cd91e932164d110ca6ec2f8579cf","Decode only the pinned authored silent red/blue movie");
    Window window;constexpr UINT message=WM_APP+257;native::NativeNotesImageDecoder decoder([](const auto&r){return native::NotesImageAccess{std::filesystem::u8path(r.path),r.accessLease};},{window.value,message,19},[](const auto&r,auto access){return std::make_unique<DiagnosticSequence>(r,std::move(access));});
    const std::array request{native::NotesImageRequest{"owned.poster",pathUTF8(std::filesystem::absolute(path)),1,32,true,{},false,true}};
    auto invalid=request;invalid[0].allowVideoInspection=true;bool rejected{};try{decoder.setVisible(invalid);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Poster and metadata-only requests cannot be confused");
    rejected=false;try{decoder.setInspections(request);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Poster job does not replace the independent import batch");
    decoder.setVisible(request);std::vector<native::NotesImageCompletion>result;const auto deadline=GetTickCount64()+10000;
    while(result.empty()){result=decoder.drain(19);if(!result.empty())break;const auto time=GetTickCount64();check(time<deadline,"Shared media worker finishes first-frame extraction");
        MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(deadline-time),QS_POSTMESSAGE,MWMO_INPUTAVAILABLE);MSG event{};while(PeekMessageW(&event,window.value,message,message,PM_REMOVE))check(event.wParam==19&&event.lParam==0,"Poster completion carries only current route generation");}
    const auto&completion=result.front();if(FAILED(completion.result)||!completion.info||!completion.frame)diagnosePipeline(std::filesystem::absolute(path));check(SUCCEEDED(completion.result)&&completion.info&&completion.frame,"SourceReader returns one immutable bounded poster");
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
