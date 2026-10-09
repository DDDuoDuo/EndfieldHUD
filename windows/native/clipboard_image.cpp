#include "native/clipboard_image.hpp"
#include "native/system_services.hpp"
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>

namespace endfield::native {
namespace {
using Bytes=std::span<const std::uint8_t>;
std::uint16_t le16(Bytes b,std::size_t p){return std::uint16_t(b[p])|std::uint16_t(b[p+1])<<8;}
std::uint32_t le32(Bytes b,std::size_t p){return std::uint32_t(b[p])|std::uint32_t(b[p+1])<<8|std::uint32_t(b[p+2])<<16|std::uint32_t(b[p+3])<<24;}
std::uint32_t be32(Bytes b,std::size_t p){return std::uint32_t(b[p])<<24|std::uint32_t(b[p+1])<<16|std::uint32_t(b[p+2])<<8|std::uint32_t(b[p+3]);}
ClipboardImageFormat dibFormat(ClipboardEncodedFormat f){return f==ClipboardEncodedFormat::dibV5?ClipboardImageFormat::dib_v5:ClipboardImageFormat::dib;}
#ifdef _WIN32
bool pngCRC(Bytes b){
    static const auto table=[] {std::array<std::uint32_t,256>v{};for(std::uint32_t n=0;n<256;++n){auto c=n;for(unsigned k=0;k<8;++k)c=(c&1)?0xedb88320u^(c>>1):c>>1;v[n]=c;}return v;}();
    for(std::size_t p=8;p<b.size();){if(b.size()-p<12)return false;const auto length=be32(b,p);if(length>b.size()-p-12)return false;std::uint32_t c=0xffffffffu;for(std::size_t n=p+4;n<p+8+length;++n)c=table[(c^b[n])&255]^(c>>8);if((c^0xffffffffu)!=be32(b,p+8+length))return false;p+=std::size_t(length)+12;}return true;
}
#endif
bool validResult(const ClipboardImageResult&r,const ClipboardEncodedImage&input){
    if(!r.thumbnail)return r.error!=ClipboardImageError::none;
    const auto&t=*r.thumbnail;return r.error==ClipboardImageError::none&&t.encoded.format==input.format&&t.encoded.bytes==input.bytes&&
        ClipboardHistory::valid_image_metadata(t.sourceWidth,t.sourceHeight,input.bytes->size())&&t.width&&t.height&&t.width<=96&&t.height<=96&&t.rgba.size()==std::size_t(t.width)*t.height*4;
}
}
bool validClipboardEncodedImage(const ClipboardEncodedImage&i)noexcept {
    if(!i.bytes||i.bytes->empty()||i.bytes->size()>ClipboardHistory::maximum_image_bytes)return false;const Bytes b=*i.bytes;
    switch(i.format){
    case ClipboardEncodedFormat::png:{constexpr std::array<std::uint8_t,8>magic{137,80,78,71,13,10,26,10};if(b.size()<45||!std::equal(magic.begin(),magic.end(),b.begin())||be32(b,8)!=13||be32(b,12)!=0x49484452u)return false;return ClipboardHistory::valid_image_metadata(be32(b,16),be32(b,20),b.size());}
    case ClipboardEncodedFormat::dib:case ClipboardEncodedFormat::dibV5:return ClipboardHistory::valid_image(dibFormat(i.format),b);
    case ClipboardEncodedFormat::tiff:{if(b.size()<8)return false;const bool little=b[0]=='I'&&b[1]=='I',big=b[0]=='M'&&b[1]=='M';if(!little&&!big)return false;const auto marker=little?le16(b,2):std::uint16_t(b[2]*256+b[3]);const auto first=little?le32(b,4):be32(b,4);return marker==42&&first>=8&&first<b.size()-1;}
    }return false;
}
struct NativeClipboardImageProbe::Impl : std::enable_shared_from_this<Impl> {
    struct Request {std::uint64_t sequence{},generation{};ClipboardEncodedImage image;};
    app::UtilityExecutor&executor;Completion callback;Decoder decoder;app::UtilityExecutor::Route route;std::thread::id owner=std::this_thread::get_id();
    std::optional<Request>pending;Stats counts;std::uint64_t generation{};bool alive{true};std::shared_ptr<std::atomic<std::uint64_t>>workerGeneration=std::make_shared<std::atomic<std::uint64_t>>(0);
    Impl(app::UtilityExecutor&e,Completion c,Decoder d):executor(e),callback(std::move(c)),decoder(std::move(d)),route(e.makeRoute()){}
    void onThread()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Clipboard image probe owner-thread operation");}
    void advance(){if(generation==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Clipboard image generation exhausted");++generation;workerGeneration->store(generation,std::memory_order_release);}
    bool submit(){onThread();if(!alive||counts.inFlight||!pending)return false;const auto request=*pending;auto result=std::make_shared<ClipboardImageResult>();const auto weak=weak_from_this();
        const bool accepted=executor.submit(route,[decode=decoder,request,result,token=workerGeneration]{if(token->load(std::memory_order_acquire)!=request.generation)return;*result=decode(request.image);if(!validResult(*result,request.image))throw std::invalid_argument("Invalid clipboard thumbnail decoder result");},
            [weak,result,gen=request.generation,sequence=request.sequence](std::exception_ptr error){if(auto self=weak.lock()){
                self->counts.inFlight=false;++self->counts.completed;if(error){*result={{},ClipboardImageError::decode,0};}
                if(gen==self->generation&&self->alive){if(result->error!=ClipboardImageError::none)++self->counts.failed;auto callback=self->callback;if(callback)callback(sequence,std::move(*result));}else ++self->counts.discarded;
                // A callback may cancel, replace or destroy the facade. Retain
                // this state for the call but do not resubmit after destruction.
                if(self->alive)self->submit();
            }});
        if(!accepted){++counts.backpressure;return false;}pending.reset();counts.inFlight=true;++counts.submitted;return true;
    }
};
NativeClipboardImageProbe::NativeClipboardImageProbe(app::UtilityExecutor&e,Completion c,Decoder d){if(!c||!d)throw std::invalid_argument("Clipboard thumbnail requires decoder and completion");impl_=std::make_shared<Impl>(e,std::move(c),std::move(d));}
NativeClipboardImageProbe::~NativeClipboardImageProbe(){impl_->alive=false;impl_->workerGeneration->store(0,std::memory_order_release);impl_->callback={};impl_->pending.reset();impl_->executor.invalidate(impl_->route);}
bool NativeClipboardImageProbe::request(std::uint64_t sequence,ClipboardEncodedImage image){auto&i=*impl_;i.onThread();if(!validClipboardEncodedImage(image))return false;i.advance();i.pending=Impl::Request{sequence,i.generation,std::move(image)};i.submit();return true;}
bool NativeClipboardImageProbe::submitPending(){return impl_->submit();}
void NativeClipboardImageProbe::cancel(){auto&i=*impl_;i.onThread();i.advance();i.pending.reset();}
NativeClipboardImageProbe::Stats NativeClipboardImageProbe::stats()const{impl_->onThread();auto s=impl_->counts;s.waiting=impl_->pending.has_value();return s;}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
struct DecodeError {ClipboardImageError kind;HRESULT code;};
void checked(HRESULT hr){if(FAILED(hr))throw DecodeError{ClipboardImageError::decode,hr};}
struct Apartment {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Apartment(){if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)checked(hr);}~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
// Read-only concatenation of a 14-byte BMP header and the SAME original DIB.
// Avoids copying a possible 64MiB clipboard payload into an HGLOBAL/BMP file.
class EncodedStream final : public IStream {
    std::atomic<ULONG>refs_{1};std::shared_ptr<const std::vector<std::uint8_t>>bytes_;std::array<std::uint8_t,14>prefix_{};std::size_t prefixSize_{};std::uint64_t offset_{};
public:
    EncodedStream(std::shared_ptr<const std::vector<std::uint8_t>>b,bool dib,std::uint32_t pixelOffset):bytes_(std::move(b)),prefixSize_(dib?14u:0u){if(dib){prefix_[0]='B';prefix_[1]='M';auto put=[&](unsigned p,std::uint32_t v){for(unsigned i=0;i<4;++i)prefix_[p+i]=static_cast<std::uint8_t>(v>>(8*i));};put(2,static_cast<std::uint32_t>(bytes_->size()+14));put(10,pixelOffset+14);}}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**p)override{if(!p)return E_POINTER;*p=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(ISequentialStream)||id==__uuidof(IStream)){*p=static_cast<IStream*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Read(void*buffer,ULONG count,ULONG*read)override{if(read)*read=0;if(!buffer&&count)return STG_E_INVALIDPOINTER;auto*out=static_cast<std::uint8_t*>(buffer);const auto total=std::uint64_t(prefixSize_)+bytes_->size();const auto n=static_cast<ULONG>(std::min<std::uint64_t>(count,offset_>=total?0:total-offset_));ULONG done{};if(offset_<prefixSize_){done=static_cast<ULONG>(std::min<std::uint64_t>(n,prefixSize_-offset_));if(done)std::memcpy(out,prefix_.data()+offset_,done);offset_+=done;}if(n>done){std::memcpy(out+done,bytes_->data()+offset_-prefixSize_,n-done);offset_+=n-done;}if(read)*read=n;return n==count?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Write(const void*,ULONG,ULONG*)override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move,DWORD origin,ULARGE_INTEGER*position)override{std::uint64_t base{};if(origin==STREAM_SEEK_CUR)base=offset_;else if(origin==STREAM_SEEK_END)base=prefixSize_+bytes_->size();else if(origin!=STREAM_SEEK_SET)return STG_E_INVALIDFUNCTION;const auto d=move.QuadPart;if(d<0){const auto magnitude=std::uint64_t(-(d+1))+1;if(magnitude>base)return STG_E_INVALIDFUNCTION;offset_=base-magnitude;}else{if(std::uint64_t(d)>std::numeric_limits<std::uint64_t>::max()-base)return STG_E_INVALIDFUNCTION;offset_=base+static_cast<std::uint64_t>(d);}if(position)position->QuadPart=offset_;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER)override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*,ULARGE_INTEGER,ULARGE_INTEGER*,ULARGE_INTEGER*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Commit(DWORD)override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE Revert()override{return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG*s,DWORD)override{if(!s)return E_POINTER;*s={};s->type=STGTY_STREAM;s->cbSize.QuadPart=prefixSize_+bytes_->size();s->grfMode=STGM_READ;return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IStream**p)override{if(!p)return E_POINTER;*p=nullptr;try{auto*copy=new EncodedStream(bytes_,false,0);copy->prefix_=prefix_;copy->prefixSize_=prefixSize_;copy->offset_=offset_;*p=copy;return S_OK;}catch(...){return E_OUTOFMEMORY;}}
};
std::uint32_t dibPixelOffset(Bytes b){const auto header=le32(b,0);const auto bits=le16(b,header==12?10:14);const auto colors=header==12?0u:le32(b,32),compression=header==12?0u:le32(b,16);return header+(header==40?(compression==3?12u:compression==6?16u:0u):0u)+(colors?colors:bits<=8?1u<<bits:0u)*(header==12?3u:4u);}
unsigned tiffOrientation(Bytes b){if(b.size()<8)return 1;const bool little=b[0]=='I'&&b[1]=='I',big=b[0]=='M'&&b[1]=='M';if(!little&&!big)return 1;
    auto u16=[&](std::size_t p){return little?le16(b,p):std::uint16_t(std::uint16_t(b[p])<<8|b[p+1]);};auto u32=[&](std::size_t p){return little?le32(b,p):be32(b,p);};if(u16(2)!=42)return 1;
    const auto at=u32(4);if(at>b.size()-2)return 1;const auto count=u16(at);if(count>(b.size()-at-2)/12)return 1;for(std::size_t n=0;n<count;++n){const auto p=std::size_t(at)+2+n*12;if(u16(p)==274&&u16(p+2)==3&&u32(p+4)==1){const auto value=u16(p+8);return value>=1&&value<=8?value:1;}}return 1;
}
unsigned orientation(const ClipboardEncodedImage&i){const Bytes b=*i.bytes;if(i.format==ClipboardEncodedFormat::tiff)return tiffOrientation(b);if(i.format==ClipboardEncodedFormat::png){for(std::size_t p=8;b.size()-p>=12;){const auto count=be32(b,p);if(count>b.size()-p-12)break;if(std::memcmp(b.data()+p+4,"eXIf",4)==0)return tiffOrientation(b.subspan(p+8,count));p+=std::size_t(count)+12;}}return 1;}
// Select the first source color context without making a second image buffer.
// WIC's color transform performs profile conversion lazily at thumbnail size.
ComPtr<IWICColorContext> sourceColor(IWICImagingFactory*factory,IWICBitmapFrameDecode*frame){UINT count{};const auto status=frame->GetColorContexts(0,nullptr,&count);if(status==WINCODEC_ERR_UNSUPPORTEDOPERATION||status==E_NOTIMPL)return {};checked(status);if(!count)return {};ComPtr<IWICColorContext>context;checked(factory->CreateColorContext(&context));IWICColorContext*raw=context.Get();checked(frame->GetColorContexts(1,&raw,&count));WICColorContextType type{};checked(context->GetType(&type));if(type==WICColorContextUninitialized)return {};if(type==WICColorContextExifColorSpace){UINT space{};checked(context->GetExifColorSpace(&space));if(space!=1&&space!=2)return {};}if(type==WICColorContextProfile){UINT size{};checked(context->GetProfileBytes(0,nullptr,&size));if(size>ClipboardHistory::maximum_image_bytes)throw DecodeError{ClipboardImageError::unsupportedProfile,E_INVALIDARG};}return context;}
WICBitmapTransformOptions orientOptions(unsigned n){switch(n){case 2:return WICBitmapTransformFlipHorizontal;case 3:return WICBitmapTransformRotate180;case 4:return WICBitmapTransformFlipVertical;case 5:return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal);case 6:return WICBitmapTransformRotate90;case 7:return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal);case 8:return WICBitmapTransformRotate270;default:return WICBitmapTransformRotate0;}}
ClipboardImageResult wic(const ClipboardEncodedImage&input){
    const Bytes bytes=*input.bytes;const bool dib=input.format==ClipboardEncodedFormat::dib||input.format==ClipboardEncodedFormat::dibV5;
    if(input.format==ClipboardEncodedFormat::png&&(!ClipboardHistory::valid_image(ClipboardImageFormat::png,bytes)||!pngCRC(bytes)))return {{},ClipboardImageError::invalidPayload,0};
    // A packed DIB may name an external ICC file; clipboard decoding must never
    // follow that filename. Embedded profiles remain supported by WIC.
    if(dib&&le32(bytes,0)>=108&&le32(bytes,56)==0x4c494e4bu)return {{},ClipboardImageError::unsupportedProfile,0};
    Apartment apartment;ComPtr<IWICImagingFactory>factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IStream>stream;stream.Attach(new EncodedStream(input.bytes,dib,dib?dibPixelOffset(bytes):0));ComPtr<IWICBitmapDecoder>decoder;
    const auto decoderClass=dib?CLSID_WICBmpDecoder:input.format==ClipboardEncodedFormat::png?CLSID_WICPngDecoder:CLSID_WICTiffDecoder;
    checked(CoCreateInstance(decoderClass,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&decoder)));checked(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand));UINT count{};checked(decoder->GetFrameCount(&count));if(!count)throw DecodeError{ClipboardImageError::decode,WINCODEC_ERR_FRAMEMISSING};
    ComPtr<IWICBitmapFrameDecode>frame;checked(decoder->GetFrame(0,&frame));UINT width{},height{};checked(frame->GetSize(&width,&height));if(!ClipboardHistory::valid_image_metadata(width,height,bytes.size()))throw DecodeError{ClipboardImageError::dimensions,E_INVALIDARG};
    const double scale=std::min(1.,96./double(std::max(width,height)));UINT tw=std::max(1u,static_cast<UINT>(std::floor(width*scale+.5))),th=std::max(1u,static_cast<UINT>(std::floor(height*scale+.5)));
    ComPtr<IWICBitmapSource>source=frame;ComPtr<IWICBitmapScaler>scaler;if(width!=tw||height!=th){checked(factory->CreateBitmapScaler(&scaler));checked(scaler->Initialize(source.Get(),tw,th,WICBitmapInterpolationModeFant));source=scaler;}
    const auto rotation=orientation(input);ComPtr<IWICBitmapFlipRotator>rotator;if(rotation!=1){checked(factory->CreateBitmapFlipRotator(&rotator));checked(rotator->Initialize(source.Get(),orientOptions(rotation)));source=rotator;if(rotation>=5)std::swap(tw,th);}
    // Keep the codec transform lazy until the bounded final CopyPixels. WIC
    // itself can allocate codec-private working memory; this is not a promise
    // of a hard process-RSS cap for arbitrary compressed input.
    const auto color=sourceColor(factory.Get(),frame.Get());ComPtr<IWICColorTransform>colorTransform;ComPtr<IWICColorContext>srgb;if(color){checked(factory->CreateColorContext(&srgb));checked(srgb->InitializeFromExifColorSpace(1));checked(factory->CreateColorTransformer(&colorTransform));checked(colorTransform->Initialize(source.Get(),color.Get(),srgb.Get(),GUID_WICPixelFormat32bppRGBA));source=colorTransform;}
    ComPtr<IWICFormatConverter>converter;checked(factory->CreateFormatConverter(&converter));checked(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    auto thumbnail=std::make_shared<ClipboardImageThumbnail>();thumbnail->encoded=input;thumbnail->sourceWidth=width;thumbnail->sourceHeight=height;thumbnail->width=tw;thumbnail->height=th;thumbnail->rgba.resize(std::size_t(tw)*th*4);
    checked(converter->CopyPixels(nullptr,tw*4,static_cast<UINT>(thumbnail->rgba.size()),thumbnail->rgba.data()));return {std::move(thumbnail),ClipboardImageError::none,0};
}
}
ClipboardImageResult decodeClipboardImage(const ClipboardEncodedImage&i){if(!validClipboardEncodedImage(i))return {{},ClipboardImageError::invalidPayload,0};try{return wic(i);}catch(const DecodeError&e){return {{},e.kind,static_cast<std::int32_t>(e.code)};}}
}
#else
namespace endfield::native {ClipboardImageResult decodeClipboardImage(const ClipboardEncodedImage&i){return {{},validClipboardEncodedImage(i)?ClipboardImageError::unavailable:ClipboardImageError::invalidPayload,0};}}
#endif
