#include "native/profile_image.hpp"
#ifdef _WIN32
#include "core/data/data_store.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <list>
#include <map>
#include <set>
#include <thread>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace endfield::native {
namespace {
namespace m=modules;using Microsoft::WRL::ComPtr;
constexpr std::uintmax_t maximumBytes=128ull*1024*1024;
[[noreturn]]void fail(m::ProfileFailure code,std::string detail={}){throw m::ProfileError(code,std::move(detail));}
void wic(HRESULT hr,m::ProfileFailure code=m::ProfileFailure::image){if(FAILED(hr))fail(code);}
struct Apartment{HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Apartment(){if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)fail(m::ProfileFailure::image,"COM is unavailable");}~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}};
ComPtr<IWICImagingFactory>factory(){ComPtr<IWICImagingFactory>f;wic(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)));return f;}
struct Source {ComPtr<IWICImagingFactory>factory;ComPtr<IWICBitmapDecoder>decoder;ComPtr<IWICBitmapFrameDecode>frame;UINT width{},height{};int orientation{1};};
void readFrame(Source&s);
Source open(const std::filesystem::path&path){
    Source s;s.factory=factory();
    if(FAILED(s.factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&s.decoder)))fail(m::ProfileFailure::image);
    readFrame(s);return s;
}
// The bytes must outlive the returned source.
Source open(std::span<const std::uint8_t>bytes){
    Source s;s.factory=factory();ComPtr<IWICStream>stream;wic(s.factory->CreateStream(&stream));
    if(bytes.empty()||bytes.size()>maximumBytes)fail(bytes.empty()?m::ProfileFailure::image:m::ProfileFailure::imageTooLarge);
    wic(stream->InitializeFromMemory(const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bytes.data())),static_cast<DWORD>(bytes.size())));
    if(FAILED(s.factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&s.decoder)))fail(m::ProfileFailure::image);
    readFrame(s);return s;
}
void readFrame(Source&s){
    UINT count{};if(FAILED(s.decoder->GetFrameCount(&count))||count==0)fail(m::ProfileFailure::image);
    wic(s.decoder->GetFrame(0,&s.frame));wic(s.frame->GetSize(&s.width,&s.height));
    ComPtr<IWICMetadataQueryReader>reader;
    if(SUCCEEDED(s.frame->GetMetadataQueryReader(&reader)))for(const auto*name:{L"/app1/ifd/{ushort=274}",L"/ifd/{ushort=274}"}){
        PROPVARIANT v;PropVariantInit(&v);
        if(SUCCEEDED(reader->GetMetadataByName(name,&v))){const int o=v.vt==VT_UI2?v.uiVal:v.vt==VT_UI4?static_cast<int>(v.ulVal):0;PropVariantClear(&v);if(o>=1&&o<=8){s.orientation=o;break;}}
        else PropVariantClear(&v);
    }
}
// UserProfileStore.validPortraitDimensions on stored pixel sizes.
bool portraitDimensions(UINT w,UINT h){return w>=1&&h>=1&&w<=32768&&h<=32768&&std::uint64_t(w)*h<=128000000ull;}
m::ProfileImage pixels(Source&s,UINT width,UINT height){
    ComPtr<IWICBitmapSource>source=s.frame;
    if(width!=s.width||height!=s.height){ComPtr<IWICBitmapScaler>scaler;wic(s.factory->CreateBitmapScaler(&scaler));
        wic(scaler->Initialize(s.frame.Get(),width,height,WICBitmapInterpolationModeHighQualityCubic));source=scaler;}
    ComPtr<IWICFormatConverter>converter;wic(s.factory->CreateFormatConverter(&converter));
    wic(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    m::ProfileImage out{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4),false};
    wic(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(out.rgba.size()),out.rgba.data()));return out;
}
// CGImageSource thumbnail with transform: EXIF orientation applied.
m::ProfileImage oriented(m::ProfileImage image,int orientation){
    if(orientation<=1||orientation>8)return image;const auto w=image.width,h=image.height;const bool swap=orientation>=5;
    m::ProfileImage out{swap?h:w,swap?w:h,std::vector<std::uint8_t>(image.rgba.size()),image.premultiplied};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned dx{},dy{};
        switch(orientation){case 2:dx=w-1-x;dy=y;break;case 3:dx=w-1-x;dy=h-1-y;break;case 4:dx=x;dy=h-1-y;break;case 5:dx=y;dy=x;break;case 6:dx=h-1-y;dy=x;break;case 7:dx=h-1-y;dy=w-1-x;break;default:dx=y;dy=w-1-x;break;}
        std::copy_n(image.rgba.data()+(std::size_t(y)*w+x)*4,4,out.rgba.data()+(std::size_t(dy)*out.width+dx)*4);}
    return out;
}
std::pair<UINT,UINT>bounded(UINT w,UINT h,unsigned maximum){
    const double scale=std::min(1.,double(maximum)/std::max(w,h));
    return {std::max<UINT>(1,static_cast<UINT>(std::lround(w*scale))),std::max<UINT>(1,static_cast<UINT>(std::lround(h*scale)))};
}
std::vector<std::uint8_t>png(IWICImagingFactory&f,const m::ProfileImage&image){
    ComPtr<IStream>stream;wic(CreateStreamOnHGlobal(nullptr,TRUE,&stream));
    ComPtr<IWICBitmapEncoder>encoder;wic(f.CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));wic(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IPropertyBag2>properties;wic(encoder->CreateNewFrame(&frame,&properties));wic(frame->Initialize(properties.Get()));
    wic(frame->SetSize(image.width,image.height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;wic(frame->SetPixelFormat(&format));
    if(format!=GUID_WICPixelFormat32bppBGRA)fail(m::ProfileFailure::image);
    auto bgra=image.rgba;for(std::size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
    wic(frame->WritePixels(image.height,image.width*4,static_cast<UINT>(bgra.size()),bgra.data()));wic(frame->Commit());wic(encoder->Commit());
    STATSTG stat{};wic(stream->Stat(&stat,STATFLAG_NONAME));HGLOBAL memory{};wic(GetHGlobalFromStream(stream.Get(),&memory));
    const auto*bytes=static_cast<const std::uint8_t*>(GlobalLock(memory));if(!bytes)fail(m::ProfileFailure::image);
    std::vector<std::uint8_t>out(bytes,bytes+stat.cbSize.QuadPart);GlobalUnlock(memory);return out;
}
std::string managedName(m::ProfileImageKind kind){auto id=ehud::data::makeUUID();std::transform(id.begin(),id.end(),id.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});return id+(kind==m::ProfileImageKind::avatar?".image":".png");}
void atomicWrite(const std::filesystem::path&directory,const std::string&name,const std::vector<std::uint8_t>&bytes){
    std::error_code error;std::filesystem::create_directories(directory,error);if(error)fail(m::ProfileFailure::persistence,"The image folder could not be created");
    const auto target=directory/name,temporary=directory/("."+name+"."+ehud::data::makeUUID()+".tmp");
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));out.flush();
     if(!out){out.close();std::filesystem::remove(temporary,error);fail(m::ProfileFailure::persistence,"The image could not be written");}}
    if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)){std::filesystem::remove(temporary,error);fail(m::ProfileFailure::persistence,"The image could not be saved");}
}
}
std::string importProfileImage(const std::filesystem::path&source,m::ProfileImageKind kind,const std::filesystem::path&directory){
    std::error_code error;const auto status=std::filesystem::status(source,error);
    if(error||!std::filesystem::is_regular_file(status))fail(m::ProfileFailure::image);
    const auto size=std::filesystem::file_size(source,error);if(error)fail(m::ProfileFailure::image);if(size>maximumBytes)fail(m::ProfileFailure::imageTooLarge);
    const Apartment com;auto s=open(source);
    if(kind==m::ProfileImageKind::avatar&&!portraitDimensions(s.width,s.height))fail(m::ProfileFailure::imageDimensions);
    // A small decode verifies the image; only the background keeps it.
    const auto[w,h]=bounded(s.width,s.height,kind==m::ProfileImageKind::avatar?256:2048);
    auto thumbnail=pixels(s,w,h);
    std::vector<std::uint8_t>bytes;
    if(kind==m::ProfileImageKind::avatar){
        // Own the original encoded file, including its EXIF orientation.
        std::ifstream in(source,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(in),{});
        if(!in.eof()&&in.fail())fail(m::ProfileFailure::image);if(bytes.size()>maximumBytes)fail(m::ProfileFailure::imageTooLarge);
    }else bytes=png(*s.factory.Get(),oriented(std::move(thumbnail),s.orientation));
    const auto name=managedName(kind);atomicWrite(directory,name,bytes);return name;
}
std::string importProfileImageBytes(std::span<const std::uint8_t>encoded,m::ProfileImageKind kind,const std::filesystem::path&directory){
    if(encoded.size()>maximumBytes)fail(m::ProfileFailure::imageTooLarge);
    const Apartment com;auto s=open(encoded);
    if(kind==m::ProfileImageKind::avatar&&!portraitDimensions(s.width,s.height))fail(m::ProfileFailure::imageDimensions);
    const auto[w,h]=bounded(s.width,s.height,kind==m::ProfileImageKind::avatar?256:2048);
    auto thumbnail=pixels(s,w,h);
    const auto bytes=kind==m::ProfileImageKind::avatar?std::vector<std::uint8_t>(encoded.begin(),encoded.end()):png(*s.factory.Get(),oriented(std::move(thumbnail),s.orientation));
    const auto name=managedName(kind);atomicWrite(directory,name,bytes);return name;
}
ProfileDecodedImage decodeProfileImage(const std::filesystem::path&managed,m::ProfileImageKind kind,unsigned maximum){
    const Apartment com;auto s=open(managed);ProfileDecodedImage out;out.sourceWidth=s.width;out.sourceHeight=s.height;
    if(kind==m::ProfileImageKind::avatar){
        if(!portraitDimensions(s.width,s.height))fail(m::ProfileFailure::imageDimensions);
        const auto[w,h]=bounded(s.width,s.height,std::clamp(maximum,256u,16384u));out.image=pixels(s,w,h);out.orientation=s.orientation;
    }else{const auto[w,h]=bounded(s.width,s.height,2048);out.image=oriented(pixels(s,w,h),s.orientation);out.orientation=1;}
    return out;
}
struct ProfileImageService::Impl:std::enable_shared_from_this<Impl> {
    std::filesystem::path directory;app::UtilityExecutor&executor;ProfileImageCallbacks callbacks;app::UtilityExecutor::Route route;
    const std::thread::id owner=std::this_thread::get_id();bool alive{true},importing{};
    std::optional<std::pair<std::filesystem::path,m::ProfileImageKind>>queuedImport;std::list<std::pair<std::string,m::ProfileImageKind>>queuedDecodes;std::set<std::string>decoding;
    std::list<std::pair<std::string,std::shared_ptr<const ProfileDecodedImage>>>cache; // most recent first, at most two
    Impl(std::filesystem::path d,app::UtilityExecutor&e,ProfileImageCallbacks c):directory(std::move(d)),executor(e),callbacks(std::move(c)),route(e.makeRoute()){}
    void onOwner()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("Profile image service owner-thread operation");}
    static std::pair<m::ProfileFailure,std::string>classify(std::exception_ptr e){
        try{std::rethrow_exception(e);}catch(const m::ProfileError&p){return {p.code(),p.detail()};}catch(const std::exception&x){return {m::ProfileFailure::image,x.what()};}catch(...){}
        return {m::ProfileFailure::image,{}};
    }
    void pump(){
        if(!alive)return;const std::weak_ptr<Impl>weak=shared_from_this();
        if(queuedImport&&!importing){
            auto job=*queuedImport;auto result=std::make_shared<std::string>();const auto dir=directory;
            if(executor.submit(route,[job,result,dir]{*result=importProfileImage(job.first,job.second,dir);},[weak,job,result](std::exception_ptr error){
                auto i=weak.lock();if(!i||!i->alive)return;i->importing=false;
                if(error){const auto[code,detail]=classify(error);if(i->callbacks.importFailed)i->callbacks.importFailed(job.second,code,detail);}
                else if(i->callbacks.imported)i->callbacks.imported(job.second,*result);
                if(i->alive)i->pump();
            })){importing=true;queuedImport.reset();}
        }
        while(!queuedDecodes.empty()){
            auto job=queuedDecodes.front();if(decoding.contains(job.first)){queuedDecodes.pop_front();continue;}
            auto result=std::make_shared<std::shared_ptr<const ProfileDecodedImage>>();const auto path=directory/job.first;
            if(!executor.submit(route,[job,result,path]{*result=std::make_shared<const ProfileDecodedImage>(decodeProfileImage(path,job.second));},[weak,job,result](std::exception_ptr error){
                auto i=weak.lock();if(!i||!i->alive)return;i->decoding.erase(job.first);
                if(error){if(i->callbacks.decodeFailed)i->callbacks.decodeFailed(job.first,classify(error).first);}
                else{i->remember(job.first,*result);if(i->callbacks.decoded)i->callbacks.decoded(job.first,*result);}
                if(i->alive)i->pump();
            }))break;
            decoding.insert(job.first);queuedDecodes.pop_front();
        }
    }
    void remember(const std::string&name,std::shared_ptr<const ProfileDecodedImage>image){
        cache.remove_if([&](const auto&v){return v.first==name;});cache.emplace_front(name,std::move(image));while(cache.size()>2)cache.pop_back();
    }
};
ProfileImageService::ProfileImageService(std::filesystem::path directory,app::UtilityExecutor&executor,ProfileImageCallbacks callbacks){
    if(directory.empty()||!directory.is_absolute())throw std::invalid_argument("Profile images need an explicit absolute folder");
    impl_=std::make_shared<Impl>(std::move(directory),executor,std::move(callbacks));
}
ProfileImageService::~ProfileImageService(){impl_->alive=false;impl_->executor.invalidate(impl_->route,true);}
bool ProfileImageService::import(std::filesystem::path source,m::ProfileImageKind kind){auto i=impl_;i->onOwner();if(i->importing||i->queuedImport)return false;i->queuedImport=std::pair{std::move(source),kind};i->pump();return true;}
void ProfileImageService::request(const std::string&name,m::ProfileImageKind kind){
    auto i=impl_;i->onOwner();
    if(const auto hit=cached(name)){if(i->callbacks.decoded)i->callbacks.decoded(name,hit);return;}
    if(i->decoding.contains(name)||std::any_of(i->queuedDecodes.begin(),i->queuedDecodes.end(),[&](const auto&v){return v.first==name;}))return;
    i->queuedDecodes.emplace_back(name,kind);i->pump();
}
std::shared_ptr<const ProfileDecodedImage>ProfileImageService::cached(std::string_view name)const{
    for(const auto&[key,value]:impl_->cache)if(key==name)return value;return nullptr;
}
void ProfileImageService::forget(std::string_view name){impl_->onOwner();impl_->cache.remove_if([&](const auto&v){return v.first==name;});}
void ProfileImageService::discard(const std::string&name){
    auto i=impl_;i->onOwner();forget(name);const auto path=i->directory/name;
    i->executor.submit(i->route,[path]{std::error_code ignored;std::filesystem::remove(path,ignored);},[](std::exception_ptr){});
}
void ProfileImageService::queueCapacityAvailable(){impl_->onOwner();impl_->pump();}
bool ProfileImageService::busy()const noexcept{return impl_->importing||impl_->queuedImport||!impl_->decoding.empty()||!impl_->queuedDecodes.empty();}
}
#endif
