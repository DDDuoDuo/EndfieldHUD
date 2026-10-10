#include "native/media_assembly_codec.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
std::filesystem::path mediaAssemblyPath(std::string_view s){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()),s.size()));}
std::string mediaAssemblyUTF8(const std::filesystem::path&p){const auto u=p.u8string();return std::string(reinterpret_cast<const char*>(u.data()),u.size());}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <wrl/client.h>
#include <limits>
#include <cstdio>
#include <source_location>

namespace endfield::native {
namespace {
namespace m=modules;using Microsoft::WRL::ComPtr;
[[noreturn]]void fail(m::MediaAssemblyError e,const std::source_location at=std::source_location::current()){throw m::MediaAssemblyFailure(e,"line "+std::to_string(at.line()));}
void check(HRESULT hr,m::MediaAssemblyError e,const std::source_location at=std::source_location::current()){if(FAILED(hr)){char code[16];std::snprintf(code,sizeof code,"0x%08lx",static_cast<unsigned long>(hr));throw m::MediaAssemblyFailure(e,"line "+std::to_string(at.line())+" hr "+code);}}
void need(bool v,m::MediaAssemblyError e,const std::source_location at=std::source_location::current()){if(!v)fail(e,at);}
struct Apartment {HRESULT hr{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
struct Platform {HRESULT hr{MFStartup(MF_VERSION,MFSTARTUP_NOSOCKET)};Platform(){check(hr,m::MediaAssemblyError::unsupported);}~Platform(){if(SUCCEEDED(hr))MFShutdown();}};
struct Variant {PROPVARIANT value{};Variant(){PropVariantInit(&value);}~Variant(){PropVariantClear(&value);}};
ComPtr<IWICImagingFactory>factory(){ComPtr<IWICImagingFactory>f;check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)),m::MediaAssemblyError::unsupported);return f;}
std::optional<unsigned>metadata(IWICMetadataQueryReader*reader,const wchar_t*name){
    if(!reader)return {};Variant v;if(FAILED(reader->GetMetadataByName(name,&v.value)))return {};
    switch(v.value.vt){case VT_UI1:return v.value.bVal;case VT_UI2:return v.value.uiVal;case VT_UI4:return v.value.ulVal;case VT_I2:return unsigned(std::max<SHORT>(0,v.value.iVal));case VT_I4:return unsigned(std::max<LONG>(0,v.value.lVal));default:return {};}
}
std::string containerName(const GUID&g){
    if(IsEqualGUID(g,GUID_ContainerFormatPng))return "png";if(IsEqualGUID(g,GUID_ContainerFormatJpeg))return "jpeg";
    if(IsEqualGUID(g,GUID_ContainerFormatTiff))return "tiff";if(IsEqualGUID(g,GUID_ContainerFormatGif))return "gif";
    if(IsEqualGUID(g,GUID_ContainerFormatBmp))return "bmp";if(IsEqualGUID(g,GUID_ContainerFormatHeif))return "heif";
    if(IsEqualGUID(g,GUID_ContainerFormatWebp))return "webp";if(IsEqualGUID(g,GUID_ContainerFormatIco))return "ico";
    if(IsEqualGUID(g,GUID_ContainerFormatWmp))return "jxr";if(IsEqualGUID(g,GUID_ContainerFormatDds))return "dds";
    return "image";
}
struct Still {
    ComPtr<IWICImagingFactory>wic;ComPtr<IStream>stream;ComPtr<IWICBitmapDecoder>decoder;GUID container{};unsigned frames{1},width{},height{},orientation{1};bool gif{};
};
// Opens one still container, enforcing the source open rules.
Still openStill(const std::filesystem::path&path){
    Still s;s.wic=factory();
    check(SHCreateStreamOnFileEx(path.c_str(),STGM_READ|STGM_SHARE_DENY_WRITE,FILE_ATTRIBUTE_NORMAL,FALSE,nullptr,&s.stream),m::MediaAssemblyError::unavailable);
    check(s.wic->CreateDecoderFromStream(s.stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&s.decoder),m::MediaAssemblyError::unsupported);
    check(s.decoder->GetContainerFormat(&s.container),m::MediaAssemblyError::unsupported);s.gif=IsEqualGUID(s.container,GUID_ContainerFormatGif);
    UINT count{};check(s.decoder->GetFrameCount(&count),m::MediaAssemblyError::unsupported);need(count>=1&&(count==1||s.gif),m::MediaAssemblyError::unsupported);s.frames=count;
    ComPtr<IWICBitmapFrameDecode>first;check(s.decoder->GetFrame(0,&first),m::MediaAssemblyError::unsupported);check(first->GetSize(&s.width,&s.height),m::MediaAssemblyError::unsupported);
    ComPtr<IWICMetadataQueryReader>meta;first->GetMetadataQueryReader(&meta);
    if(s.gif){ComPtr<IWICMetadataQueryReader>global;if(SUCCEEDED(s.decoder->GetMetadataQueryReader(&global))){const auto w=metadata(global.Get(),L"/logscrdesc/Width"),h=metadata(global.Get(),L"/logscrdesc/Height");if(w&&h&&*w&&*h){s.width=*w;s.height=*h;}}}
    else s.orientation=metadata(meta.Get(),L"System.Photo.Orientation").value_or(metadata(meta.Get(),L"/app1/ifd/{ushort=274}").value_or(metadata(meta.Get(),L"/ifd/{ushort=274}").value_or(1)));
    if(s.orientation<1||s.orientation>8)s.orientation=1;
    need(s.width&&s.height&&std::uint64_t(s.width)*s.height<=NativeMediaAssemblyEngine::maximumPixels,m::MediaAssemblyError::unsupported);
    return s;
}
// Converts one WIC source to sRGB straight RGBA8 at width x height (Fant).
std::vector<std::uint8_t>pixels(IWICImagingFactory*wic,ComPtr<IWICBitmapSource>source,IWICBitmapFrameDecode*frame,unsigned w,unsigned h){
    if(frame){UINT contexts{};if(SUCCEEDED(frame->GetColorContexts(0,nullptr,&contexts))&&contexts&&contexts<=16){
        ComPtr<IWICColorContext>input,output;if(SUCCEEDED(wic->CreateColorContext(&input))&&SUCCEEDED(wic->CreateColorContext(&output))){IWICColorContext*raw=input.Get();UINT actual{};
            if(SUCCEEDED(frame->GetColorContexts(1,&raw,&actual))&&actual&&SUCCEEDED(output->InitializeFromExifColorSpace(1))){ComPtr<IWICColorTransform>t;if(SUCCEEDED(wic->CreateColorTransformer(&t))&&SUCCEEDED(t->Initialize(source.Get(),input.Get(),output.Get(),GUID_WICPixelFormat32bppRGBA)))source=t;}}}}
    UINT sw{},sh{};check(source->GetSize(&sw,&sh),m::MediaAssemblyError::unsupported);
    if(sw!=w||sh!=h){ComPtr<IWICBitmapScaler>scaler;check(wic->CreateBitmapScaler(&scaler),m::MediaAssemblyError::unsupported);check(scaler->Initialize(source.Get(),w,h,WICBitmapInterpolationModeFant),m::MediaAssemblyError::unsupported);source=scaler;}
    ComPtr<IWICFormatConverter>converted;check(wic->CreateFormatConverter(&converted),m::MediaAssemblyError::unsupported);
    check(converted->Initialize(source.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),m::MediaAssemblyError::unsupported);
    std::vector<std::uint8_t>out(std::size_t(w)*h*4);check(converted->CopyPixels(nullptr,w*4,UINT(out.size()),out.data()),m::MediaAssemblyError::unsupported);return out;
}
void orient(MediaAssemblyBitmap&b,unsigned o){
    if(o<=1||o>8)return;const auto w=b.width,h=b.height;const bool t=o>=5;const unsigned ow=t?h:w,oh=t?w:h;std::vector<std::uint8_t>out(b.straightRGBA.size());
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned dx{},dy{};
        switch(o){case 2:dx=w-1-x;dy=y;break;case 3:dx=w-1-x;dy=h-1-y;break;case 4:dx=x;dy=h-1-y;break;case 5:dx=y;dy=x;break;case 6:dx=h-1-y;dy=x;break;case 7:dx=h-1-y;dy=w-1-x;break;default:dx=y;dy=w-1-x;break;}
        std::copy_n(b.straightRGBA.data()+(std::size_t(y)*w+x)*4,4,out.data()+(std::size_t(dy)*ow+dx)*4);}
    b.width=ow;b.height=oh;b.straightRGBA=std::move(out);
}
std::pair<unsigned,unsigned>bounded(unsigned w,unsigned h,std::optional<unsigned>maximum){
    if(!maximum||std::max(w,h)<=*maximum)return {w,h};const double s=double(*maximum)/std::max(w,h);
    return {std::clamp<unsigned>(unsigned(std::lround(w*s)),1u,*maximum),std::clamp<unsigned>(unsigned(std::lround(h*s)),1u,*maximum)};
}
// Writes decoded pixels rotated by the Media Foundation display rotation
// into out (same byte count; no allocation).
void rotateInto(const std::uint8_t*in,unsigned w,unsigned h,unsigned degrees,std::uint8_t*out){
    const bool swap=degrees==90||degrees==270;const unsigned ow=swap?h:w;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned dx=x,dy=y;if(degrees==90){dx=h-1-y;dy=x;}else if(degrees==180){dx=w-1-x;dy=h-1-y;}else if(degrees==270){dx=y;dy=w-1-x;}
        std::copy_n(in+(std::size_t(y)*w+x)*4,4,out+(std::size_t(dy)*ow+dx)*4);}
}
// Rotates decoded pixels by the Media Foundation display rotation.
void rotate(MediaAssemblyBitmap&b,unsigned degrees){
    if(degrees==0)return;const auto w=b.width,h=b.height;const bool swap=degrees==90||degrees==270;std::vector<std::uint8_t>out(b.straightRGBA.size());
    rotateInto(b.straightRGBA.data(),w,h,degrees,out.data());b.width=swap?h:w;b.height=swap?w:h;b.straightRGBA=std::move(out);
}
struct VideoInfo {unsigned width{},height{},rotation{};double duration{},fps{30};bool audio{};};
VideoInfo inspectVideo(IMFSourceReader*reader){
    VideoInfo v;ComPtr<IMFMediaType>type;check(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&type),m::MediaAssemblyError::unsupported);
    UINT32 w{},h{};check(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h),m::MediaAssemblyError::unsupported);v.width=w;v.height=h;
    v.rotation=MFGetAttributeUINT32(type.Get(),MF_MT_VIDEO_ROTATION,0);need(v.rotation==0||v.rotation==90||v.rotation==180||v.rotation==270,m::MediaAssemblyError::unsupported);
    UINT32 num{},den{};if(SUCCEEDED(MFGetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,&num,&den))&&num&&den)v.fps=double(num)/den;
    Variant d;check(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,MF_PD_DURATION,&d.value),m::MediaAssemblyError::unsupported);need(d.value.vt==VT_UI8,m::MediaAssemblyError::unsupported);
    v.duration=double(d.value.uhVal.QuadPart)/1e7;need(w&&h&&w<=65536&&h<=65536&&std::isfinite(v.duration)&&v.duration>0&&std::uint64_t(w)*h<=NativeMediaAssemblyEngine::maximumPixels,m::MediaAssemblyError::unsupported);
    ComPtr<IMFMediaType>audio;v.audio=SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,&audio));
    return v;
}
// Locks one RGB32 sample and copies BGRA rows into straight RGBA (alpha 255).
void copyFrame(IMFSample*sample,unsigned w,unsigned h,LONG defaultStride,std::uint8_t*out){
    ComPtr<IMFMediaBuffer>buffer;check(sample->ConvertToContiguousBuffer(&buffer),m::MediaAssemblyError::unsupported);
    ComPtr<IMF2DBuffer2>two;BYTE*scan{};BYTE*start{};LONG stride{};DWORD length{};
    struct Unlock {IMF2DBuffer2*two;IMFMediaBuffer*one;~Unlock(){if(two)two->Unlock2D();else if(one)one->Unlock();}}unlock{nullptr,nullptr};
    if(SUCCEEDED(buffer.As(&two))&&SUCCEEDED(two->Lock2DSize(MF2DBuffer_LockFlags_Read,&scan,&stride,&start,&length))){unlock.two=two.Get();}
    else{DWORD maximum{};check(buffer->Lock(&start,&maximum,&length),m::MediaAssemblyError::unsupported);unlock.one=buffer.Get();stride=defaultStride?defaultStride:LONG(w*4);scan=stride<0?start+std::size_t(-stride)*(h-1):start;}
    const std::int64_t pitch=stride;need(pitch&&std::abs(pitch)>=std::int64_t(w)*4&&std::uint64_t(std::abs(pitch))*h<=std::uint64_t(length)+std::uint64_t(std::abs(pitch)),m::MediaAssemblyError::unsupported);
    for(unsigned y=0;y<h;++y){const BYTE*row=scan+std::ptrdiff_t(y)*pitch;need(row>=start&&row+std::size_t(w)*4<=start+length,m::MediaAssemblyError::unsupported);auto*d=out+std::size_t(y)*w*4;
        for(unsigned x=0;x<w;++x){d[x*4]=row[x*4+2];d[x*4+1]=row[x*4+1];d[x*4+2]=row[x*4];d[x*4+3]=255;}}
}
ComPtr<IMFSourceReader>videoReader(const std::filesystem::path&path,bool audio){
    ComPtr<IMFAttributes>attributes;check(MFCreateAttributes(&attributes,2),m::MediaAssemblyError::unsupported);
    check(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE),m::MediaAssemblyError::unsupported);
    ComPtr<IMFSourceReader>reader;check(MFCreateSourceReaderFromURL(path.c_str(),attributes.Get(),&reader),m::MediaAssemblyError::unsupported);
    check(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE),m::MediaAssemblyError::unsupported);
    check(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE),m::MediaAssemblyError::unsupported);
    if(audio)reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE);
    return reader;
}
LONG negotiateRGB32(IMFSourceReader*reader,unsigned w,unsigned h){
    ComPtr<IMFMediaType>output;check(MFCreateMediaType(&output),m::MediaAssemblyError::unsupported);
    check(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),m::MediaAssemblyError::unsupported);check(output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32),m::MediaAssemblyError::unsupported);
    check(MFSetAttributeSize(output.Get(),MF_MT_FRAME_SIZE,w,h),m::MediaAssemblyError::unsupported);check(MFSetAttributeRatio(output.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1),m::MediaAssemblyError::unsupported);
    check(output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive),m::MediaAssemblyError::unsupported);
    check(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,output.Get()),m::MediaAssemblyError::unsupported);
    ComPtr<IMFMediaType>actual;check(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&actual),m::MediaAssemblyError::unsupported);
    GUID subtype{};UINT32 aw{},ah{};check(actual->GetGUID(MF_MT_SUBTYPE,&subtype),m::MediaAssemblyError::unsupported);check(MFGetAttributeSize(actual.Get(),MF_MT_FRAME_SIZE,&aw,&ah),m::MediaAssemblyError::unsupported);
    need(subtype==MFVideoFormat_RGB32&&aw==w&&ah==h,m::MediaAssemblyError::unsupported);
    UINT32 raw{};if(SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE,&raw)))return LONG(raw);LONG stride{};MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1,w,&stride);return stride;
}
void seek(IMFSourceReader*reader,double seconds){Variant p;p.value.vt=VT_I8;p.value.hVal.QuadPart=LONGLONG(std::llround(std::max(0.,seconds)*1e7));check(reader->SetCurrentPosition(GUID_NULL,p.value),m::MediaAssemblyError::unsupported);}
GUID encoderContainer(m::MediaAssemblyImageFormat f){
    switch(f){case m::MediaAssemblyImageFormat::png:return GUID_ContainerFormatPng;case m::MediaAssemblyImageFormat::jpeg:return GUID_ContainerFormatJpeg;
    case m::MediaAssemblyImageFormat::tiff:return GUID_ContainerFormatTiff;default:return GUID_ContainerFormatHeif;}
}
}

std::optional<m::MediaAssemblyFileIdentity>mediaAssemblyFileIdentity(const std::filesystem::path&path){
    HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){const auto e=GetLastError();if(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND)return {};fail(m::MediaAssemblyError::unavailable);}
    struct Close {HANDLE h;~Close(){CloseHandle(h);}}close{file};
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(file,&info))fail(m::MediaAssemblyError::unavailable);
    if(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))fail(m::MediaAssemblyError::unavailable);
    m::MediaAssemblyFileIdentity id;FILE_ID_INFO full{};
    if(GetFileInformationByHandleEx(file,FileIdInfo,&full,sizeof full)){id.volume=full.VolumeSerialNumber;std::copy_n(full.FileId.Identifier,16,id.file.begin());}
    else{id.volume=info.dwVolumeSerialNumber;const std::uint64_t index=(std::uint64_t(info.nFileIndexHigh)<<32)|info.nFileIndexLow;for(int i=0;i<8;++i)id.file[std::size_t(i)]=std::uint8_t(index>>(8*i));}
    id.bytes=(std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;id.modified=std::int64_t((std::uint64_t(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime);
    return id;
}
m::MediaAssemblyFileOperations mediaAssemblyFileOperations(){
    return {[](const std::string&p){return mediaAssemblyFileIdentity(mediaAssemblyPath(p));},
        [](const std::string&from,const std::string&to,bool replace){return MoveFileExW(mediaAssemblyPath(from).c_str(),mediaAssemblyPath(to).c_str(),MOVEFILE_WRITE_THROUGH|(replace?MOVEFILE_REPLACE_EXISTING:0))!=FALSE;},
        [](const std::string&p){DeleteFileW(mediaAssemblyPath(p).c_str());}};
}
bool mediaAssemblySamePath(const std::string&a,const std::string&b){
    std::error_code ea,eb;const auto pa=std::filesystem::weakly_canonical(mediaAssemblyPath(a),ea),pb=std::filesystem::weakly_canonical(mediaAssemblyPath(b),eb);
    const auto&x=ea?mediaAssemblyPath(a):pa;const auto&y=eb?mediaAssemblyPath(b):pb;
    return CompareStringOrdinal(x.c_str(),-1,y.c_str(),-1,TRUE)==CSTR_EQUAL;
}

struct NativeMediaAssemblyEngine::Impl {
    Options options;mutable std::mutex lock;NativeMediaAssemblyAssets assets;
    struct Cached {std::string id;double time{};std::shared_ptr<const MediaAssemblyBitmap>image;};std::optional<Cached>cached;std::uint64_t generation{};
    MediaAssemblyEngineStats statistics;std::optional<bool>heic;
    explicit Impl(Options o):options(std::move(o)),assets(options.assetRoot){}
};
NativeMediaAssemblyEngine::NativeMediaAssemblyEngine(Options o):impl_(std::make_unique<Impl>(std::move(o))){}
NativeMediaAssemblyEngine::~NativeMediaAssemblyEngine()=default;
void NativeMediaAssemblyEngine::clearCaches()noexcept{std::lock_guard g(impl_->lock);impl_->cached.reset();++impl_->generation;impl_->assets.clear();++impl_->statistics.clears;}
MediaAssemblyEngineStats NativeMediaAssemblyEngine::stats()const{std::lock_guard g(impl_->lock);auto s=impl_->statistics;s.cachedSourceBytes=impl_->cached&&impl_->cached->image?impl_->cached->image->straightRGBA.size():0;return s;}
bool NativeMediaAssemblyEngine::heicEncoder()const{
    {std::lock_guard g(impl_->lock);if(impl_->heic)return *impl_->heic;}
    // Source CGImageDestinationCopyTypeIdentifiers filtering: HEIC is offered
    // only when an installed WIC HEIF encoder can actually be created.
    // The HEIF container can be installed without an HEVC encoder, so one
    // tiny in-memory encode proves the capability (cached for the process).
    bool available{};try{Apartment com;auto wic=factory();ComPtr<IWICBitmapEncoder>e;ComPtr<IStream>memory;
        if(SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatHeif,nullptr,&e))&&SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&memory))&&SUCCEEDED(e->Initialize(memory.Get(),WICBitmapEncoderNoCache))){
            ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IPropertyBag2>props;std::array<std::uint8_t,16*16*4>pixels{};pixels.fill(128);WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
            available=SUCCEEDED(e->CreateNewFrame(&frame,&props))&&SUCCEEDED(frame->Initialize(props.Get()))&&SUCCEEDED(frame->SetSize(16,16))&&SUCCEEDED(frame->SetPixelFormat(&format))
                &&(IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA)||IsEqualGUID(format,GUID_WICPixelFormat32bppBGR))&&SUCCEEDED(frame->WritePixels(16,64,UINT(pixels.size()),pixels.data()))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(e->Commit());}}
    catch(...){available=false;}
    std::lock_guard g(impl_->lock);impl_->heic=available;return available;
}
MediaAssemblyCodecCapabilities NativeMediaAssemblyEngine::capabilities()const{
    MediaAssemblyCodecCapabilities c;c.heicEncode=heicEncoder();
    try{Apartment com;auto wic=factory();ComPtr<IWICBitmapDecoder>d;
        c.heifDecode=SUCCEEDED(wic->CreateDecoder(GUID_ContainerFormatHeif,nullptr,&d));d.Reset();
        c.webpDecode=SUCCEEDED(wic->CreateDecoder(GUID_ContainerFormatWebp,nullptr,&d));}catch(...){}
    const auto transform=[](GUID category,GUID major,GUID subtype,bool input){
        MFT_REGISTER_TYPE_INFO info{major,subtype};IMFActivate**found{};UINT32 count{};
        const HRESULT hr=MFTEnumEx(category,MFT_ENUM_FLAG_SYNCMFT|MFT_ENUM_FLAG_ASYNCMFT|MFT_ENUM_FLAG_HARDWARE|MFT_ENUM_FLAG_SORTANDFILTER,input?&info:nullptr,input?nullptr:&info,&found,&count);
        for(UINT32 i=0;i<count;++i)found[i]->Release();CoTaskMemFree(found);return SUCCEEDED(hr)&&count>0;};
    try{Platform mf;
        c.hevcDecode=transform(MFT_CATEGORY_VIDEO_DECODER,MFMediaType_Video,MFVideoFormat_HEVC,true);
        c.h264Encode=transform(MFT_CATEGORY_VIDEO_ENCODER,MFMediaType_Video,MFVideoFormat_H264,false);
        c.aacEncode=transform(MFT_CATEGORY_AUDIO_ENCODER,MFMediaType_Audio,MFAudioFormat_AAC,false);}catch(...){}
    return c;
}
modules::MediaAssemblyDocumentInfo NativeMediaAssemblyEngine::open(const std::string&utf8){
    Apartment com;const auto path=mediaAssemblyPath(utf8);
    m::MediaAssemblyDocumentInfo d;d.path=utf8;d.name=mediaAssemblyUTF8(path.filename());
    const auto identity=mediaAssemblyFileIdentity(path);if(!identity)fail(m::MediaAssemblyError::unavailable);d.identity=*identity;
    {std::lock_guard g(impl_->lock);++impl_->statistics.opens;}
    try{
        const auto s=openStill(path);d.container=containerName(s.container);d.kind=s.gif?m::MediaAssemblyKind::gif:m::MediaAssemblyKind::image;d.frameCount=s.frames;
        unsigned w=s.width,h=s.height;if(s.orientation>=5)std::swap(w,h);d.pixels={double(w),double(h)};return d;
    }catch(const m::MediaAssemblyFailure&f){if(f.error()==m::MediaAssemblyError::unavailable)throw;}
    // Not a still container: Media Foundation movie metadata.
    Platform mf;ComPtr<IMFSourceReader>reader;check(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader),m::MediaAssemblyError::unsupported);
    const auto v=inspectVideo(reader.Get());d.video=true;d.kind=m::MediaAssemblyKind::video;d.container="video";d.duration=v.duration;
    const bool swap=v.rotation==90||v.rotation==270;d.pixels={double(swap?v.height:v.width),double(swap?v.width:v.height)};return d;
}
MediaAssemblyBitmap NativeMediaAssemblyEngine::decodeStill(const m::MediaAssemblyDocumentInfo&d,std::optional<unsigned>maximum){
    const auto s=openStill(mediaAssemblyPath(d.path));MediaAssemblyBitmap out;
    ComPtr<IWICBitmapFrameDecode>frame;check(s.decoder->GetFrame(0,&frame),m::MediaAssemblyError::unsupported);
    if(!s.gif){
        // Scale before orienting; the bound applies to the longest side.
        const auto[w,h]=bounded(s.width,s.height,maximum);ComPtr<IWICBitmapSource>source;check(frame.As(&source),m::MediaAssemblyError::unsupported);
        out.width=w;out.height=h;out.straightRGBA=pixels(s.wic.Get(),source,frame.Get(),w,h);orient(out,s.orientation);return out;
    }
    // GIF first frame on its logical screen (transparent outside the frame).
    UINT fw{},fh{};check(frame->GetSize(&fw,&fh),m::MediaAssemblyError::unsupported);ComPtr<IWICMetadataQueryReader>meta;frame->GetMetadataQueryReader(&meta);
    const unsigned left=metadata(meta.Get(),L"/imgdesc/Left").value_or(0),top=metadata(meta.Get(),L"/imgdesc/Top").value_or(0);
    ComPtr<IWICBitmapSource>source;check(frame.As(&source),m::MediaAssemblyError::unsupported);const auto rgba=pixels(s.wic.Get(),source,frame.Get(),fw,fh);
    std::vector<std::uint8_t>canvas(std::size_t(s.width)*s.height*4,0);
    for(unsigned y=0;y<fh&&top+y<s.height;++y)for(unsigned x=0;x<fw&&left+x<s.width;++x)std::copy_n(rgba.data()+(std::size_t(y)*fw+x)*4,4,canvas.data()+(std::size_t(top+y)*s.width+left+x)*4);
    const auto[w,h]=bounded(s.width,s.height,maximum);
    if(w==s.width&&h==s.height){out={w,h,std::move(canvas)};return out;}
    ComPtr<IWICBitmap>bitmap;check(s.wic->CreateBitmapFromMemory(s.width,s.height,GUID_WICPixelFormat32bppRGBA,s.width*4,UINT(canvas.size()),canvas.data(),&bitmap),m::MediaAssemblyError::unsupported);
    ComPtr<IWICBitmapSource>bs;check(bitmap.As(&bs),m::MediaAssemblyError::unsupported);out={w,h,pixels(s.wic.Get(),bs,nullptr,w,h)};return out;
}
MediaAssemblyBitmap NativeMediaAssemblyEngine::decodeVideoFrame(const m::MediaAssemblyDocumentInfo&d,double time,unsigned maximum){
    Platform mf;auto reader=videoReader(mediaAssemblyPath(d.path),false);const auto v=inspectVideo(reader.Get());
    // AVAssetImageGenerator: maximumSize box with the preferred transform,
    // +/-0.08 s tolerance around min(duration - 0.001, time).
    const auto[w,h]=bounded(v.width,v.height,maximum);const auto stride=negotiateRGB32(reader.Get(),w,h);
    const double target=std::clamp(time,0.,std::max(0.,v.duration-.001));seek(reader.Get(),target);
    MediaAssemblyBitmap out{w,h,std::vector<std::uint8_t>(std::size_t(w)*h*4)};bool have{};const auto started=GetTickCount64();
    for(unsigned reads=0;reads<2000;++reads){
        DWORD flags{};LONGLONG stamp{};ComPtr<IMFSample>sample;check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&stamp,&sample),m::MediaAssemblyError::unsupported);
        need(!(flags&MF_SOURCE_READERF_ERROR),m::MediaAssemblyError::unsupported);
        if(sample){copyFrame(sample.Get(),w,h,stride,out.straightRGBA.data());have=true;if(double(stamp)/1e7>=target-.08)break;}
        if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;need(GetTickCount64()-started<15000,m::MediaAssemblyError::unsupported);
    }
    need(have,m::MediaAssemblyError::unsupported);rotate(out,v.rotation);return out;
}
// Source Engine.preview up to (not including) Engine.apply: identity
// re-check, cached bounded decode and the filter cube.
std::pair<std::shared_ptr<const MediaAssemblyBitmap>,std::shared_ptr<const MediaAssemblyCube>>NativeMediaAssemblyEngine::previewInputs(const m::MediaAssemblyDocumentInfo&d,const m::MediaAssemblyAdjustments&a,double time){
    if(!a.valid())fail(m::MediaAssemblyError::invalidAdjustment);Apartment com;
    const auto identity=mediaAssemblyFileIdentity(mediaAssemblyPath(d.path));if(!identity||*identity!=d.identity)fail(m::MediaAssemblyError::changedOnDisk);
    const double sourceTime=d.video?time:0;std::shared_ptr<const MediaAssemblyBitmap>image;std::uint64_t generation{};
    {std::lock_guard g(impl_->lock);generation=impl_->generation;if(impl_->cached&&impl_->cached->id==d.id&&impl_->cached->time==sourceTime){image=impl_->cached->image;++impl_->statistics.previewCacheHits;}}
    if(!image){
        auto decoded=std::make_shared<MediaAssemblyBitmap>(d.video?decodeVideoFrame(d,time,maximumPreviewDimension):decodeStill(d,maximumPreviewDimension));image=decoded;
        std::lock_guard g(impl_->lock);++impl_->statistics.previewDecodes;if(generation==impl_->generation)impl_->cached=Impl::Cached{d.id,sourceTime,image};
    }
    std::shared_ptr<const MediaAssemblyCube>cube;if(a.filter!=m::MediaAssemblyFilter::none){std::lock_guard g(impl_->lock);cube=impl_->assets.cube(a.filter);}
    return {std::move(image),std::move(cube)};
}
modules::MediaAssemblyPreviewImage NativeMediaAssemblyEngine::preview(const m::MediaAssemblyDocumentInfo&d,const m::MediaAssemblyAdjustments&a,double time){
    const auto[image,cube]=previewInputs(d,a,time);
    const m::MediaAssemblyFrameProcessor processor(image->width,image->height,a,cube?cube->rgb8():std::span<const std::uint8_t>{},false);
    m::MediaAssemblyPreviewImage out{processor.width(),processor.height(),{}};
    out.straightRGBA=processor.render({image->width,image->height,std::size_t(image->width)*4,image->straightRGBA,false},m::MediaAssemblyOutputAlpha::straight);
    return out;
}
modules::MediaAssemblyPreviewImage NativeMediaAssemblyEngine::previewSource(const m::MediaAssemblyDocumentInfo&d,const m::MediaAssemblyAdjustments&a,double time){
    auto[image,cube]=previewInputs(d,a,time);auto edits=a;edits.stickers.clear();
    const auto plan=m::mediaAssemblyPixelPlan(image->width,image->height,edits);
    m::MediaAssemblyPreviewImage out{plan.outputWidth,plan.outputHeight,{}};out.source=std::move(image);out.edits=std::move(edits);
    if(cube){out.lookup=cube->rgb8();out.lookupOwner=std::move(cube);}
    {std::lock_guard g(impl_->lock);++impl_->statistics.deferredPreviews;}
    return out;
}

namespace {
// Streams processor rows into one WIC frame in the encoder's pixel format.
void encodeImage(IWICImagingFactory*wic,const std::filesystem::path&temp,m::MediaAssemblyImageFormat format,const m::MediaAssemblyFrameProcessor&processor,const m::MediaAssemblyPixels&source,m::MediaAssemblyExportTicket&ticket){
    ComPtr<IWICStream>stream;check(wic->CreateStream(&stream),m::MediaAssemblyError::exportFailed);check(stream->InitializeFromFilename(temp.c_str(),GENERIC_WRITE),m::MediaAssemblyError::exportFailed);
    ComPtr<IWICBitmapEncoder>encoder;check(wic->CreateEncoder(encoderContainer(format),nullptr,&encoder),m::MediaAssemblyError::unsupportedExport);check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),m::MediaAssemblyError::exportFailed);
    ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IPropertyBag2>props;check(encoder->CreateNewFrame(&frame,&props),m::MediaAssemblyError::exportFailed);
    if(format==m::MediaAssemblyImageFormat::jpeg||format==m::MediaAssemblyImageFormat::heic){
        // kCGImageDestinationLossyCompressionQuality 0.95.
        PROPBAG2 option{};option.pstrName=const_cast<LPOLESTR>(L"ImageQuality");VARIANT value{};VariantInit(&value);value.vt=VT_R4;value.fltVal=.95f;props->Write(1,&option,&value);
    }
    check(frame->Initialize(props.Get()),m::MediaAssemblyError::exportFailed);const unsigned w=processor.width(),h=processor.height();check(frame->SetSize(w,h),m::MediaAssemblyError::exportFailed);
    // The original encodes its associated RGBA8 createCGImage result: PNG/HEIC
    // store it unassociated, TIFF keeps associated alpha, JPEG drops alpha.
    WICPixelFormatGUID requested=format==m::MediaAssemblyImageFormat::jpeg?GUID_WICPixelFormat24bppBGR:format==m::MediaAssemblyImageFormat::tiff?GUID_WICPixelFormat32bppPBGRA:GUID_WICPixelFormat32bppBGRA;
    WICPixelFormatGUID actual=requested;check(frame->SetPixelFormat(&actual),m::MediaAssemblyError::unsupportedExport);
    const bool bgr24=IsEqualGUID(actual,GUID_WICPixelFormat24bppBGR),pbgra=IsEqualGUID(actual,GUID_WICPixelFormat32bppPBGRA),bgra=IsEqualGUID(actual,GUID_WICPixelFormat32bppBGRA)||IsEqualGUID(actual,GUID_WICPixelFormat32bppBGR);
    need(bgr24||pbgra||bgra,m::MediaAssemblyError::unsupportedExport);
    const unsigned band=std::max(1u,std::min(h,(16u*1024u*1024u)/std::max(1u,w*4)));std::vector<std::uint8_t>rows(std::size_t(w)*band*4),packed(std::size_t(w)*band*4);
    for(unsigned y=0;y<h;y+=band){
        if(ticket.cancelled())fail(m::MediaAssemblyError::cancelled);const unsigned n=std::min(band,h-y);
        processor.render(source,y,n,rows,std::size_t(w)*4,(bgra&&!pbgra)?m::MediaAssemblyOutputAlpha::straight:m::MediaAssemblyOutputAlpha::premultiplied);
        const unsigned stride=bgr24?((w*3+3)&~3u):w*4;if(packed.size()<std::size_t(stride)*n)packed.resize(std::size_t(stride)*n);
        for(unsigned r=0;r<n;++r){const auto*s=rows.data()+std::size_t(r)*w*4;auto*d=packed.data()+std::size_t(r)*stride;
            for(unsigned x=0;x<w;++x){if(bgr24){d[x*3]=s[x*4+2];d[x*3+1]=s[x*4+1];d[x*3+2]=s[x*4];}else{d[x*4]=s[x*4+2];d[x*4+1]=s[x*4+1];d[x*4+2]=s[x*4];d[x*4+3]=s[x*4+3];}}}
        check(frame->WritePixels(n,stride,UINT(std::size_t(stride)*n),packed.data()),m::MediaAssemblyError::exportFailed);
    }
    check(frame->Commit(),m::MediaAssemblyError::exportFailed);check(encoder->Commit(),m::MediaAssemblyError::exportFailed);check(stream->Commit(STGC_DEFAULT),m::MediaAssemblyError::exportFailed);
}
MediaAssemblyBitmap decodeSticker(IWICImagingFactory*wic,const std::filesystem::path&path){
    ComPtr<IWICBitmapDecoder>decoder;check(wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),m::MediaAssemblyError::unavailable);
    ComPtr<IWICBitmapFrameDecode>frame;check(decoder->GetFrame(0,&frame),m::MediaAssemblyError::unavailable);UINT w{},h{};check(frame->GetSize(&w,&h),m::MediaAssemblyError::unavailable);
    ComPtr<IWICFormatConverter>c;check(wic->CreateFormatConverter(&c),m::MediaAssemblyError::unavailable);check(c->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),m::MediaAssemblyError::unavailable);
    MediaAssemblyBitmap out{w,h,std::vector<std::uint8_t>(std::size_t(w)*h*4)};check(c->CopyPixels(nullptr,w*4,UINT(out.straightRGBA.size()),out.straightRGBA.data()),m::MediaAssemblyError::unavailable);return out;
}
}

std::string NativeMediaAssemblyEngine::exportMedia(const m::MediaAssemblyExportRequest&r,m::MediaAssemblyExportTicket&ticket){
    Apartment com;const auto ops=mediaAssemblyFileOperations();
    {std::lock_guard g(impl_->lock);++impl_->statistics.exports;}
    auto plan=m::mediaAssemblyPrepareExport(r,ops,ticket,mediaAssemblySamePath);
    struct Temporary {const m::MediaAssemblyFileOperations&ops;std::string path;bool armed{true};~Temporary(){if(armed)try{ops.remove(path);}catch(...){}}}temporary{ops,plan.temporary};
    const auto&d=*r.document;const auto tempPath=mediaAssemblyPath(plan.temporary);
    std::shared_ptr<const MediaAssemblyCube>cube;if(r.adjustments.filter!=m::MediaAssemblyFilter::none){std::lock_guard g(impl_->lock);cube=impl_->assets.cube(r.adjustments.filter);}
    const auto lookup=cube?cube->rgb8():std::span<const std::uint8_t>{};
    if(!d.video){
        auto wic=factory();const auto source=decodeStill(d,std::nullopt);ticket.setProgress(.25);
        std::vector<std::vector<std::uint8_t>>art;std::vector<m::MediaAssemblyStickerArtwork>stickers;art.reserve(r.adjustments.stickers.size());
        for(const auto&s:r.adjustments.stickers){const auto&asset=impl_->assets.sticker(s.kind);const auto image=decodeSticker(wic.Get(),impl_->assets.root()/mediaAssemblyPath(asset.path));art.push_back(m::mediaAssemblyPremultiply(image.straightRGBA));stickers.push_back({image.width,image.height,art.back()});}
        const m::MediaAssemblyFrameProcessor processor(source.width,source.height,r.adjustments,lookup,true,stickers);
        if(ticket.cancelled())fail(m::MediaAssemblyError::cancelled);
        need(processor.width()<=65536&&processor.height()<=65536,m::MediaAssemblyError::unsupportedExport);
        ticket.setProgress(.7);
        encodeImage(wic.Get(),tempPath,plan.format.image,processor,{source.width,source.height,std::size_t(source.width)*4,source.straightRGBA,false},ticket);
    }else{
        Platform mf;const auto range=r.adjustments.timeRange(d.duration);if(!range)fail(m::MediaAssemblyError::invalidAdjustment);
        if(!r.adjustments.stickers.empty())fail(m::MediaAssemblyError::unsupported);
        const double start=double(range->startTicks)/600,end=start+double(range->durationTicks)/600;
        auto reader=videoReader(mediaAssemblyPath(d.path),true);const auto v=inspectVideo(reader.Get());const auto stride=negotiateRGB32(reader.Get(),v.width,v.height);
        bool audio=v.audio;ComPtr<IMFMediaType>pcm;
        if(audio){
            ComPtr<IMFMediaType>native;reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,&native);const UINT32 rate=MFGetAttributeUINT32(native.Get(),MF_MT_AUDIO_SAMPLES_PER_SECOND,48000),channels=MFGetAttributeUINT32(native.Get(),MF_MT_AUDIO_NUM_CHANNELS,2);
            const UINT32 outRate=rate==44100?44100:48000,outChannels=channels>=2?2:1;
            ComPtr<IMFMediaType>want;MFCreateMediaType(&want);want->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);want->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);
            want->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,outRate);want->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,outChannels);want->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,outChannels*2);want->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,outRate*outChannels*2);
            audio=SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,want.Get()))&&SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&pcm));
            if(!audio)reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,FALSE);
        }
        const bool swap=v.rotation==90||v.rotation==270;const unsigned ow=swap?v.height:v.width,oh=swap?v.width:v.height;
        // MediaAssemblyEngine.composition: even output, <=16384, 1...120 fps.
        const auto even=m::mediaAssemblyPixelPlan(ow,oh,r.adjustments,std::nullopt,true);need(even.outputWidth<=16384&&even.outputHeight<=16384,m::MediaAssemblyError::unsupportedExport);
        const m::MediaAssemblyFrameProcessor processor(ow,oh,r.adjustments,lookup,false,{},true);
        const double fps=std::clamp(v.fps,1.,120.);
        // Keep the source's exact nominal rate ratio when it is already within
        // the composition clamp (1...120 fps); otherwise use the clamped rate.
        UINT32 sourceNum{},sourceDen{};{ComPtr<IMFMediaType>native;if(SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&native)))MFGetAttributeRatio(native.Get(),MF_MT_FRAME_RATE,&sourceNum,&sourceDen);}
        const bool sourceRate=sourceNum&&sourceDen&&std::abs(double(sourceNum)/sourceDen-fps)<1e-9;
        // Microsoft's software H.264/AAC encoders: hardware MFTs reject small
        // or unusual frame sizes with E_FAIL, so they are not opted into.
        ComPtr<IMFAttributes>attributes;MFCreateAttributes(&attributes,1);attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE,MFTranscodeContainerType_MPEG4);
        ComPtr<IMFSinkWriter>writer;check(MFCreateSinkWriterFromURL(tempPath.c_str(),nullptr,attributes.Get(),&writer),m::MediaAssemblyError::exportFailed);
        const UINT32 fpsNum=sourceRate?sourceNum:UINT32(std::lround(fps*1000)),fpsDen=sourceRate?sourceDen:1000;
        ComPtr<IMFMediaType>h264;MFCreateMediaType(&h264);h264->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);h264->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);
        h264->SetUINT32(MF_MT_AVG_BITRATE,UINT32(std::clamp(double(even.outputWidth)*even.outputHeight*fps*.2,500000.,std::numeric_limits<UINT32>::max()/2.)));h264->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        MFSetAttributeSize(h264.Get(),MF_MT_FRAME_SIZE,even.outputWidth,even.outputHeight);MFSetAttributeRatio(h264.Get(),MF_MT_FRAME_RATE,fpsNum,fpsDen);MFSetAttributeRatio(h264.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
        DWORD videoStream{};check(writer->AddStream(h264.Get(),&videoStream),m::MediaAssemblyError::unsupportedExport);
        ComPtr<IMFMediaType>rgb;MFCreateMediaType(&rgb);rgb->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);rgb->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);rgb->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
        MFSetAttributeSize(rgb.Get(),MF_MT_FRAME_SIZE,even.outputWidth,even.outputHeight);MFSetAttributeRatio(rgb.Get(),MF_MT_FRAME_RATE,fpsNum,fpsDen);MFSetAttributeRatio(rgb.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
        check(writer->SetInputMediaType(videoStream,rgb.Get(),nullptr),m::MediaAssemblyError::unsupportedExport);
        DWORD audioStream{};
        if(audio){
            const UINT32 rate=MFGetAttributeUINT32(pcm.Get(),MF_MT_AUDIO_SAMPLES_PER_SECOND,48000),channels=MFGetAttributeUINT32(pcm.Get(),MF_MT_AUDIO_NUM_CHANNELS,2);
            ComPtr<IMFMediaType>aac;MFCreateMediaType(&aac);aac->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);aac->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_AAC);aac->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);
            aac->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,rate);aac->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,channels);aac->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,24000);
            audio=SUCCEEDED(writer->AddStream(aac.Get(),&audioStream))&&SUCCEEDED(writer->SetInputMediaType(audioStream,pcm.Get(),nullptr));
            if(!audio)reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,FALSE);
        }
        std::atomic<bool>cancelled{};ticket.observeCancellation([&cancelled]{cancelled=true;});std::atomic<double>written{0};const double span=std::max(1e-6,end-start);
        ticket.observeProgress([&written,span]{return written.load()/span;});
        struct Forget {m::MediaAssemblyExportTicket&t;~Forget(){t.finish();}}forget{ticket};
        check(writer->BeginWriting(),m::MediaAssemblyError::exportFailed);seek(reader.Get(),start);
        MediaAssemblyBitmap frame{v.width,v.height,std::vector<std::uint8_t>(std::size_t(v.width)*v.height*4)};
        // The source composition renders on the GPU; so does this job when a
        // device exists (raw frames upload once, the display rotation is part
        // of the geometry). Any GPU failure finishes the job on the CPU.
        std::unique_ptr<NativeMediaAssemblyGpuProcessor>gpu;bool gpuConfigured{};
        if(impl_->options.exportDevice&&std::max(v.width,v.height)<=16384)if(auto device=impl_->options.exportDevice())try{gpu=std::make_unique<NativeMediaAssemblyGpuProcessor>(std::move(device));}catch(const std::exception&){gpu.reset();}
        std::vector<std::uint8_t>oriented,processed,scaled;
        const auto cpuBuffers=[&]{if(processed.empty()){if(v.rotation)oriented.resize(frame.straightRGBA.size());processed.resize(std::size_t(processor.width())*processor.height()*4);scaled.resize(std::size_t(even.outputWidth)*even.outputHeight*4);}};
        if(!gpu)cpuBuffers();
        std::uint64_t gpuFrames{},cpuFrames{};
        struct Count {Impl&i;std::uint64_t&g,&c;~Count(){std::lock_guard l(i.lock);i.statistics.gpuExportFrames+=g;i.statistics.cpuExportFrames+=c;}}count{*impl_,gpuFrames,cpuFrames};
        bool videoDone{},audioDone{!audio};const LONGLONG start100=LONGLONG(std::llround(start*1e7)),end100=LONGLONG(std::llround(end*1e7));
        while(!videoDone||!audioDone){
            if(cancelled)fail(m::MediaAssemblyError::cancelled);
            DWORD index{},flags{};LONGLONG stamp{};ComPtr<IMFSample>sample;check(reader->ReadSample(MF_SOURCE_READER_ANY_STREAM,0,&index,&flags,&stamp,&sample),m::MediaAssemblyError::exportFailed);
            need(!(flags&MF_SOURCE_READERF_ERROR),m::MediaAssemblyError::exportFailed);
            ComPtr<IMFMediaType>which;bool isVideo{};if(SUCCEEDED(reader->GetCurrentMediaType(index,&which))){GUID major{};which->GetGUID(MF_MT_MAJOR_TYPE,&major);isVideo=major==MFMediaType_Video;}
            const bool eos=(flags&MF_SOURCE_READERF_ENDOFSTREAM)!=0;
            if(sample&&stamp<end100){
                LONGLONG duration{};sample->GetSampleDuration(&duration);
                if(isVideo&&stamp+std::max<LONGLONG>(duration,1)>start100){
                    copyFrame(sample.Get(),v.width,v.height,stride,frame.straightRGBA.data());
                    ComPtr<IMFMediaBuffer>buffer;const DWORD bytes=DWORD(std::size_t(even.outputWidth)*even.outputHeight*4);check(MFCreateMemoryBuffer(bytes,&buffer),m::MediaAssemblyError::exportFailed);
                    {BYTE*dst{};check(buffer->Lock(&dst,nullptr,nullptr),m::MediaAssemblyError::exportFailed);
                    struct Unlock {IMFMediaBuffer*b;~Unlock(){b->Unlock();}}unlock{buffer.Get()};
                    bool done{};
                    if(gpu)try{
                        gpu->setSource(v.width,v.height,frame.straightRGBA,std::size_t(v.width)*4,false,v.rotation);
                        if(!gpuConfigured){gpu->configure(r.adjustments,lookup,true);gpuConfigured=true;}
                        // Opaque frames: associated BGRA equals RGB32 (X = 255).
                        gpu->renderToMemory(even.outputWidth,even.outputHeight,std::span<std::uint8_t>(dst,bytes),std::size_t(even.outputWidth)*4);done=true;++gpuFrames;
                    }catch(const std::exception&){gpu.reset();cpuBuffers();}
                    if(!done){
                        const std::uint8_t*src=frame.straightRGBA.data();if(v.rotation){rotateInto(src,v.width,v.height,v.rotation,oriented.data());src=oriented.data();}
                        processor.render({ow,oh,std::size_t(ow)*4,std::span<const std::uint8_t>(src,frame.straightRGBA.size()),false},0,processor.height(),processed,std::size_t(processor.width())*4,m::MediaAssemblyOutputAlpha::premultiplied);
                        const std::uint8_t*rgba=processed.data();
                        if(processor.width()!=even.outputWidth||processor.height()!=even.outputHeight){m::mediaAssemblyResample({processor.width(),processor.height(),std::size_t(processor.width())*4,processed,true},even.outputWidth,even.outputHeight,scaled,std::size_t(even.outputWidth)*4);rgba=scaled.data();}
                        for(std::size_t i=0;i<bytes;i+=4){dst[i]=rgba[i+2];dst[i+1]=rgba[i+1];dst[i+2]=rgba[i];dst[i+3]=255;}++cpuFrames;
                    }}
                    buffer->SetCurrentLength(bytes);
                    ComPtr<IMFSample>out;MFCreateSample(&out);out->AddBuffer(buffer.Get());const LONGLONG at=std::max<LONGLONG>(0,stamp-start100);out->SetSampleTime(at);out->SetSampleDuration(duration>0?duration:LONGLONG(1e7/fps));
                    check(writer->WriteSample(videoStream,out.Get()),m::MediaAssemblyError::exportFailed);written=std::max(written.load(),double(at)/1e7);
                    if(impl_->options.videoFrameHook)impl_->options.videoFrameHook(double(at)/1e7);
                }else if(!isVideo&&audio&&stamp+std::max<LONGLONG>(duration,1)>start100){
                    const LONGLONG at=stamp-start100;if(at>=0){sample->SetSampleTime(at);check(writer->WriteSample(audioStream,sample.Get()),m::MediaAssemblyError::exportFailed);}
                }
            }
            if(eos||(sample&&stamp>=end100)){
                // Stop reading a finished stream so the other can drain.
                if(isVideo&&!videoDone){videoDone=true;reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,FALSE);}
                else if(!isVideo&&!audioDone){audioDone=true;reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,FALSE);}
            }
        }
        check(writer->Finalize(),m::MediaAssemblyError::exportFailed);
    }
    temporary.armed=false;
    return m::mediaAssemblyCommitExport(plan,r,ops,ticket,impl_->options.beforeCommit);
}
}
#endif
