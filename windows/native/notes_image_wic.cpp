#include "native/notes_image_decoder.hpp"
#ifdef _WIN32
#include <wincodec.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <optional>
namespace endfield::native::detail {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr,const char*what){if(FAILED(hr))throw std::runtime_error(what);}
void need(bool v,const char*what){if(!v)throw std::invalid_argument(what);}
std::optional<unsigned>metadata(IWICMetadataQueryReader*reader,const wchar_t*name){if(!reader)return {};PROPVARIANT v{};const auto hr=reader->GetMetadataByName(name,&v);std::optional<unsigned>result;
    if(SUCCEEDED(hr)){if(v.vt==VT_UI1)result=v.bVal;else if(v.vt==VT_UI2)result=v.uiVal;else if(v.vt==VT_UI4)result=v.ulVal;else if(v.vt==VT_BOOL)result=v.boolVal!=VARIANT_FALSE?1u:0u;}PropVariantClear(&v);return result;}
struct GifFrame {unsigned x{},y{},width{},height{},disposal{};bool transparent{};unsigned transparentIndex{};};
// Application buffers are output-sized. WIC may decode internally at source
// size; metadata/file guards remain necessary even with IWICBitmapScaler.
std::vector<std::uint8_t>pixels(IWICImagingFactory*factory,IWICBitmapFrameDecode*frame,unsigned w,unsigned h){
    ComPtr<IWICBitmapSource>source;check(frame->QueryInterface(IID_PPV_ARGS(&source)),"Cannot query WIC frame");
    UINT contexts{};if(SUCCEEDED(frame->GetColorContexts(0,nullptr,&contexts))&&contexts){need(contexts<=16,"Excess image color contexts");ComPtr<IWICColorContext>input,output;check(factory->CreateColorContext(&input),"Cannot create source color context");check(factory->CreateColorContext(&output),"Cannot create destination color context");
        IWICColorContext*raw=input.Get();UINT actual{};if(SUCCEEDED(frame->GetColorContexts(1,&raw,&actual))&&actual){check(output->InitializeFromExifColorSpace(1),"Cannot initialize sRGB color context");ComPtr<IWICColorTransform>transform;check(factory->CreateColorTransformer(&transform),"Cannot create image color transform");check(transform->Initialize(source.Get(),input.Get(),output.Get(),GUID_WICPixelFormat32bppRGBA),"Unsupported image color profile");source=transform;}}
    UINT originalW{},originalH{};check(source->GetSize(&originalW,&originalH),"Cannot read image extent");
    if(originalW!=w||originalH!=h){ComPtr<IWICBitmapScaler>scaled;check(factory->CreateBitmapScaler(&scaled),"Cannot create bounded image scaler");check(scaled->Initialize(source.Get(),w,h,WICBitmapInterpolationModeFant),"Cannot scale image");source=scaled;}
    ComPtr<IWICFormatConverter>converted;check(factory->CreateFormatConverter(&converted),"Cannot create image pixel converter");check(converted->Initialize(source.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Cannot convert image pixels");
    std::vector<std::uint8_t>result(std::size_t(w)*h*4);check(converted->CopyPixels(nullptr,w*4,static_cast<UINT>(result.size()),result.data()),"Cannot decode bounded image pixels");return result;
}
void orient(NotesImageFrame&frame,unsigned orientation){if(orientation<=1||orientation>8)return;const auto w=frame.width,h=frame.height;const bool transpose=orientation>=5;const unsigned outW=transpose?h:w,outH=transpose?w:h;std::vector<std::uint8_t>out(frame.straightRGBA.size());
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){unsigned dx{},dy{};switch(orientation){case 2:dx=w-1-x;dy=y;break;case 3:dx=w-1-x;dy=h-1-y;break;case 4:dx=x;dy=h-1-y;break;case 5:dx=y;dy=x;break;case 6:dx=h-1-y;dy=x;break;case 7:dx=h-1-y;dy=w-1-x;break;default:dx=y;dy=w-1-x;break;}std::copy_n(frame.straightRGBA.data()+(std::size_t(y)*w+x)*4,4,out.data()+(std::size_t(dy)*outW+dx)*4);}
    frame.width=outW;frame.height=outH;frame.straightRGBA=std::move(out);
}
class WicSequence final:public NativeNotesImageDecoder::Sequence {
    NotesImageAccess access_;ComPtr<IWICImagingFactory>factory_;ComPtr<IWICBitmapDecoder>decoder_;ComPtr<IStream>stream_;
    NotesImageInfo info_;unsigned width_{},height_{},orientation_{1},next_{};std::vector<GifFrame>frames_;
    std::vector<std::uint8_t>canvas_,previous_;std::array<std::uint8_t,4>background_{};
    void clear(unsigned x,unsigned y,unsigned w,unsigned h){for(unsigned row=y;row<y+h;++row)for(unsigned col=x;col<x+w;++col)std::copy(background_.begin(),background_.end(),canvas_.begin()+static_cast<std::ptrdiff_t>((std::size_t(row)*width_+col)*4));}
    std::array<unsigned,4>rect(const GifFrame&f)const{const double sx=double(width_)/info_.pixelWidth,sy=double(height_)/info_.pixelHeight;
        const unsigned x=static_cast<unsigned>(std::round(f.x*sx)),y=static_cast<unsigned>(std::round(f.y*sy));
        const unsigned endX=std::min(width_,static_cast<unsigned>(std::round((f.x+f.width)*sx))),endY=std::min(height_,static_cast<unsigned>(std::round((f.y+f.height)*sy)));
        return {x,y,endX-x,endY-y};}
public:
    WicSequence(const NotesImageRequest&r,NotesImageAccess access):access_(std::move(access)){
        check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory_)),"Cannot initialize WIC image decoder");
        check(SHCreateStreamOnFileEx(access_.path.c_str(),STGM_READ|STGM_SHARE_DENY_WRITE,FILE_ATTRIBUTE_NORMAL,FALSE,nullptr,&stream_),"Original image file is unavailable");
        STATSTG stat{};check(stream_->Stat(&stat,STATFLAG_NONAME),"Cannot read image file metadata");need(stat.type==STGTY_STREAM&&stat.cbSize.QuadPart>0&&stat.cbSize.QuadPart<=NativeNotesImageDecoder::maximumInputBytes,"Image exceeds128MiB source limit");
        check(factory_->CreateDecoderFromStream(stream_.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder_),"No installed WIC codec supports this image");GUID format{};check(decoder_->GetContainerFormat(&format),"Cannot read image format");
        const bool gif=IsEqualGUID(format,GUID_ContainerFormatGif);info_.kind=gif?modules::NotesMediaKind::gif:modules::NotesMediaKind::image;
        UINT count{};check(decoder_->GetFrameCount(&count),"Cannot read image frames");need(count&&(!gif||count<=NativeNotesImageDecoder::maximumGIFFrames),"Image GIF frame count outside source limit");info_.frameCount=gif?count:1;
        ComPtr<IWICBitmapFrameDecode>first;check(decoder_->GetFrame(0,&first),"Cannot decode initial image frame");check(first->GetSize(&info_.pixelWidth,&info_.pixelHeight),"Cannot read image dimensions");ComPtr<IWICMetadataQueryReader>meta;first->GetMetadataQueryReader(&meta);
        if(gif){ComPtr<IWICMetadataQueryReader>global;check(decoder_->GetMetadataQueryReader(&global),"Cannot read GIF logical screen");auto w=metadata(global.Get(),L"/logscrdesc/Width"),h=metadata(global.Get(),L"/logscrdesc/Height");need(w&&h,"GIF logical screen is missing");info_.pixelWidth=*w;info_.pixelHeight=*h;
            if(metadata(global.Get(),L"/logscrdesc/GlobalColorTableFlag").value_or(0)){auto bg=metadata(global.Get(),L"/logscrdesc/BackgroundColorIndex");ComPtr<IWICPalette>palette;if(bg&&SUCCEEDED(factory_->CreatePalette(&palette))&&SUCCEEDED(decoder_->CopyPalette(palette.Get()))){std::array<WICColor,256>colors{};UINT actual{};if(SUCCEEDED(palette->GetColors(256,colors.data(),&actual))&&*bg<actual){const auto c=colors[*bg];background_={static_cast<std::uint8_t>(c>>16),static_cast<std::uint8_t>(c>>8),static_cast<std::uint8_t>(c),static_cast<std::uint8_t>(c>>24)};
                        if(metadata(meta.Get(),L"/grctlext/TransparencyFlag").value_or(0)&&metadata(meta.Get(),L"/grctlext/TransparentColorIndex")==bg)background_={};}}}
        }else{orientation_=metadata(meta.Get(),L"/app1/ifd/{ushort=274}").value_or(metadata(meta.Get(),L"/ifd/{ushort=274}").value_or(1));}
        need(info_.pixelWidth&&info_.pixelHeight&&info_.pixelWidth<=65536&&info_.pixelHeight<=65536&&std::uint64_t(info_.pixelWidth)*info_.pixelHeight<=NativeNotesImageDecoder::maximumInputPixels,"Image exceeds64MP source limit");
        const double scale=std::min(1.,double(modules::NotesMediaLayout::decodeDimension(r.maximumDimension))/std::max(info_.pixelWidth,info_.pixelHeight));width_=std::max(1u,static_cast<unsigned>(std::round(info_.pixelWidth*scale)));height_=std::max(1u,static_cast<unsigned>(std::round(info_.pixelHeight*scale)));
        if(!gif){if(orientation_>=5&&orientation_<=8)std::swap(info_.pixelWidth,info_.pixelHeight);return;}
        frames_.reserve(count);info_.frameDelays.reserve(count);
        for(unsigned n=0;n<count;++n){ComPtr<IWICBitmapFrameDecode>f;check(decoder_->GetFrame(n,&f),"Cannot read GIF frame");ComPtr<IWICMetadataQueryReader>m;check(f->GetMetadataQueryReader(&m),"Cannot read GIF frame metadata");GifFrame g;check(f->GetSize(&g.width,&g.height),"Cannot read GIF frame extent");g.x=metadata(m.Get(),L"/imgdesc/Left").value_or(0);g.y=metadata(m.Get(),L"/imgdesc/Top").value_or(0);g.disposal=metadata(m.Get(),L"/grctlext/Disposal").value_or(0);need(g.width&&g.height&&g.x<=info_.pixelWidth&&g.y<=info_.pixelHeight&&g.width<=info_.pixelWidth-g.x&&g.height<=info_.pixelHeight-g.y&&g.disposal<=3,"Malformed GIF frame/disposal");
            const auto delay=metadata(m.Get(),L"/grctlext/Delay");const double seconds=modules::NotesMediaLayout::normalizedFrameDelay(delay?std::optional<double>{*delay*.01}:std::nullopt);info_.frameDelays.push_back(seconds);info_.duration+=seconds;frames_.push_back(g);}
        canvas_.resize(std::size_t(width_)*height_*4);clear(0,0,width_,height_);
    }
    const NotesImageInfo&info()const override{return info_;}
    NotesImageFrame decode(unsigned index)override{
        need(index<info_.frameCount,"Image frame out of range");if(info_.kind==modules::NotesMediaKind::image){ComPtr<IWICBitmapFrameDecode>frame;check(decoder_->GetFrame(0,&frame),"Cannot read image");NotesImageFrame result{width_,height_,0,pixels(factory_.Get(),frame.Get(),width_,height_)};orient(result,orientation_);return result;}
        // A backward uncached seek restarts composition. Normal forward GIF
        // playback is O(one frame); cache hits never decode or reshape artwork.
        if(index<next_){next_=0;previous_.clear();clear(0,0,width_,height_);}
        for(;next_<=index;++next_){if(next_){const auto&last=frames_[next_-1];const auto r=rect(last);if(last.disposal==2)clear(r[0],r[1],r[2],r[3]);else if(last.disposal==3){need(previous_.size()==canvas_.size(),"Missing GIF restore-previous canvas");canvas_=previous_;}}
            const auto&f=frames_[next_];if(f.disposal==3)previous_=canvas_;else previous_.clear();const auto r=rect(f);if(!r[2]||!r[3])continue;ComPtr<IWICBitmapFrameDecode>frame;check(decoder_->GetFrame(next_,&frame),"Cannot decode GIF frame");const auto incoming=pixels(factory_.Get(),frame.Get(),r[2],r[3]);
            for(unsigned y=0;y<r[3];++y)for(unsigned x=0;x<r[2];++x){const auto*s=incoming.data()+(std::size_t(y)*r[2]+x)*4;auto*d=canvas_.data()+(std::size_t(y+r[1])*width_+x+r[0])*4;const unsigned a=s[3],inverse=255-a,alphaNumerator=a*255+d[3]*inverse;
                if(!alphaNumerator){std::fill_n(d,4,std::uint8_t(0));continue;}for(unsigned c=0;c<3;++c)d[c]=static_cast<std::uint8_t>((unsigned(s[c])*a*255+unsigned(d[c])*d[3]*inverse+alphaNumerator/2)/alphaNumerator);d[3]=static_cast<std::uint8_t>((alphaNumerator+127)/255);}
        }
        return {width_,height_,index,canvas_};
    }
};
}
std::unique_ptr<NativeNotesImageDecoder::Sequence>makeNotesWICSequence(const NotesImageRequest&r,NotesImageAccess access){return std::make_unique<WicSequence>(r,std::move(access));}
} // namespace endfield::native::detail
#endif
