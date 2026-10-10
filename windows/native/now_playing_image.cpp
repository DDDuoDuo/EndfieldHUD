#include "native/now_playing_image.hpp"
#include "modules/now_playing_model.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <thread>
namespace endfield::native {
namespace {
bool valid(const NowPlayingImageResult&r){if(!r.image)return r.failure!=NowPlayingImageFailure::none;const auto&i=*r.image;return r.failure==NowPlayingImageFailure::none&&i.width&&i.height&&i.width<=512&&i.height<=512&&i.straightRGBA.size()==std::size_t(i.width)*i.height*4;}
}
struct NativeNowPlayingArtwork::Impl:std::enable_shared_from_this<Impl>{
 struct Entry{std::weak_ptr<const std::vector<std::uint8_t>>bytes;NowPlayingImageResult value;};
 app::UtilityExecutor&queue;std::function<void()>changed;Decoder decode;app::UtilityExecutor::Route route;const std::thread::id owner=std::this_thread::get_id();
 Bytes desired;std::array<Entry,2>cache;NowPlayingImageResult value{{},NowPlayingImageFailure::unavailable};Stats counts;std::uint64_t generation{},published{};bool alive{true},inFlight{},pending{};
 Impl(app::UtilityExecutor&q,std::function<void()>c,Decoder d):queue(q),changed(std::move(c)),decode(std::move(d)),route(q.makeRoute()){if(!changed||!decode)throw std::invalid_argument("Now Playing artwork needs decoder and owner invalidation");}
 void check()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Now Playing artwork belongs to its owner thread");}
 void notify(){auto callback=changed;if(alive&&callback)callback();}
 bool submit(){check();if(!alive||inFlight||!pending||!desired)return false;const auto bytes=desired;const auto token=generation;const auto weak=weak_from_this();auto result=std::make_shared<NowPlayingImageResult>();const auto decoder=decode;
  const bool accepted=queue.submit(route,[result,decoder,bytes]{*result=decoder(*bytes);if(!valid(*result))*result={{},NowPlayingImageFailure::decode};},[weak,result,bytes,token](std::exception_ptr error){if(auto self=weak.lock()){self->inFlight=false;++self->counts.decoded;if(error)*result={{},NowPlayingImageFailure::decode};
    // Two bounded immutable thumbnails/negative entries; encoded buffers are
    // weak keys, so retiring a provider does not retain up to16MiB of payloads.
    if(self->alive&&token==self->generation&&self->desired==bytes){self->cache[1]=std::move(self->cache[0]);self->cache[0]={bytes,*result};self->value=*result;++self->published;self->notify();}else ++self->counts.discarded;
    if(self->alive)self->submit();
  }});if(!accepted){++counts.backpressure;return false;}inFlight=true;pending=false;++counts.submitted;return true;
 }
};
NativeNowPlayingArtwork::NativeNowPlayingArtwork(app::UtilityExecutor&q,std::function<void()>c,Decoder d):impl_(std::make_shared<Impl>(q,std::move(c),std::move(d))){}
NativeNowPlayingArtwork::~NativeNowPlayingArtwork(){auto i=impl_;i->check();i->alive=false;i->changed={};i->desired.reset();i->pending=false;i->queue.invalidate(i->route);}
bool NativeNowPlayingArtwork::request(Bytes bytes){auto i=impl_;i->check();if(bytes&&bytes->empty())bytes.reset();if(i->desired==bytes)return false;++i->generation;i->desired=std::move(bytes);i->pending=false;i->value={{},NowPlayingImageFailure::unavailable};++i->published;
 if(!i->desired)return true;if(i->desired->size()>modules::nowPlayingMaximumArtworkBytes){i->value.failure=NowPlayingImageFailure::invalidPayload;return true;}
 for(std::size_t n=0;n<i->cache.size();++n)if(i->cache[n].bytes.lock()==i->desired){i->value=i->cache[n].value;if(n)std::swap(i->cache[0],i->cache[n]);++i->counts.cacheHits;return true;}
 i->pending=true;i->submit();return true;
}
void NativeNowPlayingArtwork::cancel(){auto i=impl_;i->check();++i->generation;i->desired.reset();i->pending=false;i->value={{},NowPlayingImageFailure::unavailable};++i->published;}
bool NativeNowPlayingArtwork::submitPending(){return impl_->submit();}
const NowPlayingImageResult&NativeNowPlayingArtwork::result()const{impl_->check();return impl_->value;}
std::uint64_t NativeNowPlayingArtwork::revision()const noexcept{return impl_->published;}
NativeNowPlayingArtwork::Stats NativeNowPlayingArtwork::stats()const{impl_->check();auto s=impl_->counts;s.inFlight=impl_->inFlight;s.pending=impl_->pending;return s;}
}
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <propidl.h>
#include <wrl/client.h>
namespace endfield::native {namespace {
using Microsoft::WRL::ComPtr;
struct Error{NowPlayingImageFailure kind;HRESULT code;};
void checked(HRESULT hr){if(FAILED(hr))throw Error{NowPlayingImageFailure::decode,hr};}
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Apartment(){if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)checked(hr);}~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
// ImageIO honors embedded orientation even where a WIC codec omits its
// metadata query block (notably PNG eXIf). Inspect only bounded original bytes.
unsigned tiffOrientation(std::span<const std::uint8_t>b){if(b.size()<8)return 0;const bool little=b[0]=='I'&&b[1]=='I',big=b[0]=='M'&&b[1]=='M';if(!little&&!big)return 0;
 auto u16=[&](std::size_t p){return little?unsigned(b[p])|(unsigned(b[p+1])<<8):(unsigned(b[p])<<8)|b[p+1];};auto u32=[&](std::size_t p){std::uint32_t v{};for(unsigned k=0;k<4;++k)v|=std::uint32_t(b[p+k])<<(little?k*8:(3-k)*8);return v;};if(u16(2)!=42)return 0;const auto at=u32(4);if(at>b.size()-2)return 0;const auto count=u16(at);if(count>(b.size()-at-2)/12)return 0;for(std::size_t k=0;k<count;++k){const auto p=std::size_t(at)+2+k*12;if(u16(p)==274&&u16(p+2)==3&&u32(p+4)==1){const auto n=u16(p+8);return n>=1&&n<=8?n:0;}}return 0;
}
unsigned encodedOrientation(std::span<const std::uint8_t>b){if(auto n=tiffOrientation(b))return n;
 constexpr std::array<std::uint8_t,8>png{137,80,78,71,13,10,26,10};if(b.size()>=8&&std::equal(png.begin(),png.end(),b.begin())){for(std::size_t p=8;b.size()-p>=12;){const auto size=(std::uint32_t(b[p])<<24)|(std::uint32_t(b[p+1])<<16)|(std::uint32_t(b[p+2])<<8)|b[p+3];if(size>b.size()-p-12)break;if(b[p+4]=='e'&&b[p+5]=='X'&&b[p+6]=='I'&&b[p+7]=='f')return tiffOrientation(b.subspan(p+8,size));p+=std::size_t(size)+12;}}
 if(b.size()>=4&&b[0]==255&&b[1]==216){std::size_t p=2;while(p<b.size()){if(b[p++]!=255)break;while(p<b.size()&&b[p]==255)++p;if(p>=b.size())break;const auto marker=b[p++];if(marker==218||marker==217)break;if(marker==1||(marker>=208&&marker<=215))continue;if(b.size()-p<2)break;const auto length=unsigned(b[p])*256+b[p+1];if(length<2||length>b.size()-p)break;if(marker==225&&length>=8&&b[p+2]=='E'&&b[p+3]=='x'&&b[p+4]=='i'&&b[p+5]=='f'&&!b[p+6]&&!b[p+7])if(auto n=tiffOrientation(b.subspan(p+8,length-8)))return n;p+=length;}}
 return 0;
}
unsigned orientation(IWICBitmapFrameDecode*frame,std::span<const std::uint8_t>bytes){if(auto n=encodedOrientation(bytes))return n;ComPtr<IWICMetadataQueryReader>reader;if(FAILED(frame->GetMetadataQueryReader(&reader)))return 1;
 for(const auto*query:{L"/app1/ifd/{ushort=274}",L"/ifd/{ushort=274}"}){PROPVARIANT value{};const auto hr=reader->GetMetadataByName(query,&value);const unsigned n=SUCCEEDED(hr)&&value.vt==VT_UI2?value.uiVal:1;PropVariantClear(&value);if(n>=2&&n<=8)return n;}return 1;
}
WICBitmapTransformOptions transform(unsigned n){switch(n){case 2:return WICBitmapTransformFlipHorizontal;case 3:return WICBitmapTransformRotate180;case 4:return WICBitmapTransformFlipVertical;case 5:return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal);case 6:return WICBitmapTransformRotate90;case 7:return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal);case 8:return WICBitmapTransformRotate270;default:return WICBitmapTransformRotate0;}}
ComPtr<IWICColorContext>colorContext(IWICImagingFactory*f,IWICBitmapFrameDecode*frame){UINT count{};const auto hr=frame->GetColorContexts(0,nullptr,&count);if(hr==WINCODEC_ERR_UNSUPPORTEDOPERATION||hr==E_NOTIMPL)return {};checked(hr);if(!count)return {};ComPtr<IWICColorContext>c;checked(f->CreateColorContext(&c));auto*raw=c.Get();checked(frame->GetColorContexts(1,&raw,&count));WICColorContextType type{};checked(c->GetType(&type));if(type==WICColorContextUninitialized)return {};if(type==WICColorContextProfile){UINT size{};checked(c->GetProfileBytes(0,nullptr,&size));if(size>modules::nowPlayingMaximumArtworkBytes)throw Error{NowPlayingImageFailure::unsupportedProfile,E_INVALIDARG};}if(type==WICColorContextExifColorSpace){UINT space{};checked(c->GetExifColorSpace(&space));if(space!=1&&space!=2)throw Error{NowPlayingImageFailure::unsupportedProfile,E_INVALIDARG};}return c;}
NowPlayingImageResult wic(std::span<const std::uint8_t>bytes){
 Apartment apartment;ComPtr<IWICImagingFactory>factory;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
 // Worker keeps original bytes alive until every lazy codec/transform and
 // stream below has been destroyed; only the copied RGBA thumbnail escapes.
 ComPtr<IWICStream>stream;checked(factory->CreateStream(&stream));checked(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size())));ComPtr<IWICBitmapDecoder>decoder;checked(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder));
 ComPtr<IWICBitmapFrameDecode>frame;checked(decoder->GetFrame(0,&frame));UINT width{},height{};checked(frame->GetSize(&width,&height));if(!modules::nowPlayingArtworkMetadata(width,height,bytes.size()))return {{},NowPlayingImageFailure::dimensions};
 const auto scale=std::min(1.,512./double(std::max(width,height)));UINT tw=std::max(1u,static_cast<unsigned>(std::floor(width*scale+.5))),th=std::max(1u,static_cast<unsigned>(std::floor(height*scale+.5)));
 ComPtr<IWICBitmapSource>source=frame;ComPtr<IWICBitmapScaler>scaler;if(tw!=width||th!=height){checked(factory->CreateBitmapScaler(&scaler));checked(scaler->Initialize(source.Get(),tw,th,WICBitmapInterpolationModeFant));source=scaler;}
 const auto rotation=orientation(frame.Get(),bytes);ComPtr<IWICBitmapFlipRotator>rotator;if(rotation!=1){checked(factory->CreateBitmapFlipRotator(&rotator));checked(rotator->Initialize(source.Get(),transform(rotation)));source=rotator;if(rotation>=5)std::swap(tw,th);}
 const auto profile=colorContext(factory.Get(),frame.Get());ComPtr<IWICColorContext>srgb;ComPtr<IWICColorTransform>color;if(profile){checked(factory->CreateColorContext(&srgb));checked(srgb->InitializeFromExifColorSpace(1));checked(factory->CreateColorTransformer(&color));checked(color->Initialize(source.Get(),profile.Get(),srgb.Get(),GUID_WICPixelFormat32bppRGBA));source=color;}
 ComPtr<IWICFormatConverter>converter;checked(factory->CreateFormatConverter(&converter));checked(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));auto image=std::make_shared<NotesImageFrame>();image->width=tw;image->height=th;image->straightRGBA.resize(std::size_t(tw)*th*4);checked(converter->CopyPixels(nullptr,tw*4,static_cast<UINT>(image->straightRGBA.size()),image->straightRGBA.data()));return {std::move(image)};
}
}}
#endif
namespace endfield::native {
NowPlayingImageResult decodeNowPlayingImage(std::span<const std::uint8_t>bytes){if(bytes.empty()||bytes.size()>modules::nowPlayingMaximumArtworkBytes)return {{},NowPlayingImageFailure::invalidPayload};
#ifdef _WIN32
 try{return wic(bytes);}catch(const Error&e){return {{},e.kind,static_cast<std::int32_t>(e.code)};}
#else
 return {{},NowPlayingImageFailure::unavailable};
#endif
}
}
