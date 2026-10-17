#include "native/notes_image_decoder.hpp"
#include "native/notes_video_colour.hpp"
#include "core/data/json.hpp"
#include "core/shell_packet.hpp"
#ifdef _WIN32
#include <array>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
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
// The Mac's CoreMedia 709 reading of BT.709 movie posters (AVAssetImageGenerator
// in NotesMediaPresentation, shared by Notes, Archive and Projection), against
// the Media Assembly area's pinned oracle: owned synthetic H.264 movies (BT.709
// tagged and untagged) whose patch k holds grey k, with the Mac preview of each.
std::vector<std::uint8_t> base64(std::string_view text){std::vector<std::uint8_t>out;unsigned bits{},value{};for(const char c:text){int v=-1;if(c>='A'&&c<='Z')v=c-'A';else if(c>='a'&&c<='z')v=c-'a'+26;else if(c>='0'&&c<='9')v=c-'0'+52;else if(c=='+')v=62;else if(c=='/')v=63;else if(c=='=')break;else continue;value=(value<<6)|unsigned(v);bits+=6;if(bits>=8){bits-=8;out.push_back(static_cast<std::uint8_t>((value>>bits)&0xff));}}return out;}
void coreMedia709(const std::filesystem::path&fixture){
    const auto&codes=native::notesCoreMedia709Codes();check(codes[0]==0&&codes[255]==255&&codes[128]==139,"CoreMedia 709 reads decoded mid grey 128 as 139 like the Mac preview");
    for(unsigned k=1;k<256;++k)check(codes[k]>=codes[k-1],"The reading keeps the grey ramp in order");
    check(native::notesCoreMedia709(0,0)&&native::notesCoreMedia709(MFVideoTransFunc_709,MFVideoPrimaries_BT709)&&!native::notesCoreMedia709(MFVideoTransFunc_sRGB,MFVideoPrimaries_BT709)&&!native::notesCoreMedia709(MFVideoTransFunc_709,MFVideoPrimaries_BT2020),"Only BT.709 or untagged movies take the reading");
    std::ifstream in(fixture,std::ios::binary);std::stringstream text;text<<in.rdbuf();check(bool(in),"Read the pinned CoreMedia 709 oracle");const auto oracle=ehud::data::Json::parse(text.str(),8*1024*1024);
    check(oracle["size"].number()==128&&oracle["patch"].number()==8&&oracle["movies"].array().size()==2,"Oracle movies are 128-pixel grey patch charts");
    const auto root=std::filesystem::temp_directory_path()/("endfield-notes-poster-709-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(root);
    struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{root};
    Window window;constexpr UINT message=WM_APP+258;unsigned generation=40;
    for(const auto&movie:oracle["movies"].array()){
        const auto name=movie["name"].string();const auto bytes=base64(movie["mp4"].string());check(bytes.size()>256,"Oracle movie bytes decode");
        const auto file=root/(name+".mp4");{std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));check(bool(out),"Write the owned oracle movie to its temporary directory");}
        ++generation;native::NativeNotesImageDecoder decoder([](const auto&r){return native::NotesImageAccess{std::filesystem::u8path(r.path),r.accessLease};},{window.value,message,generation},[](const auto&r,auto access){return std::make_unique<DiagnosticSequence>(r,std::move(access));});
        const std::array request{native::NotesImageRequest{"oracle."+name,pathUTF8(std::filesystem::absolute(file)),1,512,true,{},false,true}};
        decoder.setVisible(request);std::vector<native::NotesImageCompletion>result;const auto deadline=GetTickCount64()+10000;
        while(result.empty()){result=decoder.drain(generation);if(!result.empty())break;const auto time=GetTickCount64();check(time<deadline,"Oracle poster finishes");MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(deadline-time),QS_POSTMESSAGE,MWMO_INPUTAVAILABLE);MSG event{};while(PeekMessageW(&event,window.value,message,message,PM_REMOVE)){}}
        check(SUCCEEDED(result.front().result)&&result.front().frame,"The oracle movie has a poster");const auto&frame=*result.front().frame;
        check(frame.width==128&&frame.height==128&&frame.coreMedia709,"A BT.709 movie's poster (tagged or untagged) carries the CoreMedia 709 reading");
        int worst{};unsigned moved{};const auto&preview=movie["preview"].array();const auto&raw=movie["raw"].array();
        for(unsigned k=0;k<256;++k){const unsigned x0=(k%16)*8+2,y0=(k/16)*8+2;double sum{};for(unsigned y=y0;y<y0+4;++y)for(unsigned x=x0;x<x0+4;++x)sum+=frame.straightRGBA[(std::size_t(y)*128+x)*4];
            const auto mean=static_cast<int>(std::lround(sum/16));worst=std::max(worst,std::abs(mean-static_cast<int>(preview[k].number())));moved+=std::abs(static_cast<int>(preview[k].number())-static_cast<int>(raw[k].number()))>=8;}
        std::cout<<"Poster "<<name<<" worst difference from the Mac preview: "<<worst<<"\n";
        check(worst<=2&&moved>=8,"The poster matches the Mac's CoreMedia 709 preview within two levels where the decoded values differ by eight or more");
        decoder.hide();const auto worker=decoder.duplicateWorkerHandle();decoder.stop();check(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0,"The oracle worker stops");CloseHandle(worker);
    }
}
int wmain(int argc,wchar_t**argv){const auto apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(apartment))return 1;int result{};try{check(argc==2||argc==3,"Pass the authored silent MP4 fixture and optionally the CoreMedia 709 oracle");run(argv[1]);if(argc==3)coreMedia709(argv[2]);std::cout<<"PASS "<<checks<<" worker video poster checks\n";}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';result=1;}CoUninitialize();return result;}
#else
int main(){return 0;}
#endif
