#include "native/hypergryph_account_avatar.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace endfield::native {
namespace {
namespace h=modules::hypergryph;
using Microsoft::WRL::ComPtr;
void checked(HRESULT hr,const char* why) {if(FAILED(hr)) throw std::invalid_argument(why);}
struct ComScope {
    bool owned{};
    ComScope() {const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);owned=hr==S_OK||hr==S_FALSE;if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE) throw std::runtime_error("COM is unavailable for avatar decoding");}
    ~ComScope() {if(owned) CoUninitialize();}
};
WICBitmapTransformOptions orientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;if(FAILED(frame->GetMetadataQueryReader(reader.GetAddressOf()))) return WICBitmapTransformRotate0;
    for(const wchar_t* query:{L"/app1/ifd/{ushort=274}",L"/ifd/{ushort=274}"}) {
        PROPVARIANT value;PropVariantInit(&value);
        if(SUCCEEDED(reader->GetMetadataByName(query,&value))) {
            unsigned short o=value.vt==VT_UI2?value.uiVal:0;PropVariantClear(&value);
            switch(o) {
                case 2: return WICBitmapTransformFlipHorizontal;
                case 3: return WICBitmapTransformRotate180;
                case 4: return WICBitmapTransformFlipVertical;
                case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal);
                case 6: return WICBitmapTransformRotate90;
                case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal);
                case 8: return WICBitmapTransformRotate270;
                default: return WICBitmapTransformRotate0;
            }
        }
        PropVariantClear(&value);
    }
    return WICBitmapTransformRotate0;
}
}
std::vector<std::uint8_t> downsampleAvatar(std::span<const std::uint8_t> encoded) {
    if(encoded.empty()||encoded.size()>avatarMaximumBytes) throw std::invalid_argument("Avatar image is empty or too large");
    ComScope com;
    ComPtr<IWICImagingFactory> wic;checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf())),"WIC is unavailable");
    ComPtr<IWICStream> stream;checked(wic->CreateStream(stream.GetAddressOf()),"Avatar stream");
    checked(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()),static_cast<DWORD>(encoded.size())),"Avatar bytes");
    ComPtr<IWICBitmapDecoder> decoder;checked(wic->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,decoder.GetAddressOf()),"Avatar is not a decodable image");
    ComPtr<IWICBitmapFrameDecode> frame;checked(decoder->GetFrame(0,frame.GetAddressOf()),"Avatar has no image frame");
    UINT width{},height{};checked(frame->GetSize(&width,&height),"Avatar size");
    if(!width||!height||width>avatarMaximumDimension||height>avatarMaximumDimension||width>avatarMaximumPixels/height) throw std::invalid_argument("Avatar dimensions are outside the bounded range");
    const unsigned longest=std::max(width,height);
    const double scale=longest>avatarThumbnailSize?double(avatarThumbnailSize)/longest:1;
    const UINT w=std::max<UINT>(1,static_cast<UINT>(std::lround(width*scale))),hgt=std::max<UINT>(1,static_cast<UINT>(std::lround(height*scale)));
    ComPtr<IWICBitmapSource> source=frame;
    if(w!=width||hgt!=height) {
        ComPtr<IWICBitmapScaler> scaler;checked(wic->CreateBitmapScaler(scaler.GetAddressOf()),"Avatar scaler");
        checked(scaler->Initialize(frame.Get(),w,hgt,WICBitmapInterpolationModeHighQualityCubic),"Avatar downsample");source=scaler;
    }
    const auto transform=orientation(frame.Get());
    if(transform!=WICBitmapTransformRotate0) {
        ComPtr<IWICBitmapFlipRotator> rotator;checked(wic->CreateBitmapFlipRotator(rotator.GetAddressOf()),"Avatar orientation");
        checked(rotator->Initialize(source.Get(),transform),"Avatar orientation transform");source=rotator;
    }
    ComPtr<IWICFormatConverter> converter;checked(wic->CreateFormatConverter(converter.GetAddressOf()),"Avatar conversion");
    checked(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Avatar pixel conversion");
    ComPtr<IStream> memory;checked(CreateStreamOnHGlobal(nullptr,TRUE,memory.GetAddressOf()),"Avatar output");
    ComPtr<IWICBitmapEncoder> encoder;checked(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.GetAddressOf()),"PNG encoder");
    checked(encoder->Initialize(memory.Get(),WICBitmapEncoderNoCache),"PNG encoder init");
    ComPtr<IWICBitmapFrameEncode> out;checked(encoder->CreateNewFrame(out.GetAddressOf(),nullptr),"PNG frame");
    checked(out->Initialize(nullptr),"PNG frame init");
    UINT ow{},oh{};checked(converter->GetSize(&ow,&oh),"Avatar output size");checked(out->SetSize(ow,oh),"PNG size");
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;checked(out->SetPixelFormat(&format),"PNG pixel format");
    checked(out->WriteSource(converter.Get(),nullptr),"PNG pixels");checked(out->Commit(),"PNG frame commit");checked(encoder->Commit(),"PNG commit");
    STATSTG stat{};checked(memory->Stat(&stat,STATFLAG_NONAME),"PNG length");
    if(stat.cbSize.QuadPart<=0||static_cast<unsigned long long>(stat.cbSize.QuadPart)>avatarMaximumBytes) throw std::invalid_argument("Avatar PNG exceeds its bound");
    std::vector<std::uint8_t> png(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};checked(memory->Seek(zero,STREAM_SEEK_SET,nullptr),"PNG rewind");
    ULONG read{};checked(memory->Read(png.data(),static_cast<ULONG>(png.size()),&read),"PNG read");
    if(read!=png.size()) throw std::invalid_argument("PNG read incomplete");
    return png;
}

struct WinHttpAvatarLoader::Impl:std::enable_shared_from_this<Impl> {
    WinHttpAccountTransport& transport;app::UtilityExecutor& executor;app::UtilityExecutor::Route route;std::uint64_t generation{};h::RequestHandle request;
    Impl(WinHttpAccountTransport& t,app::UtilityExecutor& e):transport(t),executor(e),route(e.makeRoute()) {}
    class Request final:public h::AccountRequest {
    public:
        Request(std::weak_ptr<Impl> owner,std::uint64_t generation):owner_(std::move(owner)),generation_(generation) {}
        void cancel() override {
            if(auto owner=owner_.lock();owner&&owner->generation==generation_) {++owner->generation;if(auto r=std::move(owner->request)) r->cancel();}
        }
    private:
        std::weak_ptr<Impl> owner_;std::uint64_t generation_;
    };
};
WinHttpAvatarLoader::WinHttpAvatarLoader(WinHttpAccountTransport& t,app::UtilityExecutor& e):impl_(std::make_shared<Impl>(t,e)) {}
WinHttpAvatarLoader::~WinHttpAvatarLoader() {++impl_->generation;if(auto r=std::move(impl_->request)) r->cancel();impl_->executor.invalidate(impl_->route,true);}
h::RequestHandle WinHttpAvatarLoader::load(const std::string& url,std::function<void(std::optional<std::vector<std::uint8_t>>)> completion) {
    auto& i=*impl_;const auto generation=++i.generation;if(auto r=std::move(i.request)) r->cancel();
    std::weak_ptr<Impl> weak=impl_;
    if(!h::avatarURLAllowed(url)) {i.transport.post([weak,generation,completion]{auto self=weak.lock();if(self&&self->generation==generation) completion(std::nullopt);});return std::make_shared<Impl::Request>(weak,generation);}
    i.request=i.transport.startImage(url,avatarMaximumBytes,[weak,generation,completion](h::HttpOutcome outcome) {
        auto self=weak.lock();if(!self||self->generation!=generation) return;self->request.reset();
        if(outcome.failure||outcome.status<200||outcome.status>299||outcome.body.empty()) {completion(std::nullopt);return;}
        auto bytes=std::make_shared<std::vector<std::uint8_t>>(outcome.body.begin(),outcome.body.end());
        auto png=std::make_shared<std::vector<std::uint8_t>>();
        const bool accepted=self->executor.submit(self->route,[bytes,png]{*png=downsampleAvatar(*bytes);},[weak,generation,png,completion](std::exception_ptr error) {
            auto owner=weak.lock();if(!owner||owner->generation!=generation) return;
            if(error||png->empty()) completion(std::nullopt);else completion(std::move(*png));
        });
        if(!accepted) completion(std::nullopt);
    });
    return std::make_shared<Impl::Request>(weak,generation);
}
}
#endif
