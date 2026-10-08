#include "native/notes_image_decoder.hpp"
#ifdef _WIN32
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace endfield::native::detail {
namespace {
using Microsoft::WRL::ComPtr;
void checked(HRESULT hr,const char*message){if(FAILED(hr))throw std::runtime_error(message);}
struct Platform {Platform(){checked(MFStartup(MF_VERSION,MFSTARTUP_NOSOCKET),"Initialize video metadata reader");}~Platform(){MFShutdown();}};
struct Variant {PROPVARIANT value{};~Variant(){PropVariantClear(&value);}};
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}

// A fresh Source Reader begins at the first video sample and has no playback
// clock/audio sink. Its output type, including resizing, is negotiated BEFORE
// ReadSample. Only this bounded RGB32 buffer is copied into application memory.
// MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING is documented to support
// this frame-size/color conversion; no paused Media Engine tick is assumed.
class VideoPoster final:public NativeNotesImageDecoder::Sequence {
    NotesImageAccess access_;Platform platform_;ComPtr<IMFSourceReader>reader_;
    NotesImageInfo info_;unsigned width_{},height_{},rotation_{};bool decoded_{};
    void outputType(){
        ComPtr<IMFMediaType>actual;checked(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&actual),"Read negotiated poster output");
        GUID subtype{};UINT32 width{},height{};checked(actual->GetGUID(MF_MT_SUBTYPE,&subtype),"Read poster pixel format");checked(MFGetAttributeSize(actual.Get(),MF_MT_FRAME_SIZE,&width,&height),"Read poster output dimensions");
        need(subtype==MFVideoFormat_RGB32&&width==width_&&height==height_,"Decoder did not honor bounded RGB32 poster output");
    }
public:
    VideoPoster(const NotesImageRequest&request,NotesImageAccess access):access_(std::move(access)){
        need(!access_.path.empty()&&request.videoPoster,"Missing explicit video-poster request");
        ComPtr<IMFAttributes>attributes;checked(MFCreateAttributes(&attributes,1),"Create poster reader attributes");
        checked(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE),"Enable bounded poster conversion");
        checked(MFCreateSourceReaderFromURL(access_.path.c_str(),attributes.Get(),&reader_),"Open referenced movie poster");
        checked(reader_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE),"Disable poster audio and other streams");
        checked(reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE),"Select only first video stream");
        ComPtr<IMFMediaType>native;checked(reader_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&native),"Movie has no poster video track");
        UINT32 width{},height{};checked(MFGetAttributeSize(native.Get(),MF_MT_FRAME_SIZE,&width,&height),"Read native poster dimensions");
        need(width&&height&&width<=65536&&height<=65536,"Invalid native poster dimensions");
        rotation_=MFGetAttributeUINT32(native.Get(),MF_MT_VIDEO_ROTATION,0);need(rotation_==0||rotation_==90||rotation_==180||rotation_==270,"Unsupported movie poster rotation");
        const auto bound=modules::NotesMediaLayout::decodeDimension(request.maximumDimension);const auto ratio=std::min(1.,double(bound)/std::max(width,height));
        width_=std::max(1u,static_cast<unsigned>(std::lround(width*ratio)));height_=std::max(1u,static_cast<unsigned>(std::lround(height*ratio)));
        ComPtr<IMFMediaType>output;checked(MFCreateMediaType(&output),"Create bounded poster media type");
        checked(output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video),"Select poster video output");checked(output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32),"Select poster RGB32 output");
        checked(MFSetAttributeSize(output.Get(),MF_MT_FRAME_SIZE,width_,height_),"Bound poster output dimensions");checked(MFSetAttributeRatio(output.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1),"Set square poster output pixels");
        checked(output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive),"Select progressive poster output");
        checked(reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,output.Get()),"Negotiate bounded movie poster");outputType();
        info_.pixelWidth=(rotation_==90||rotation_==270)?height_:width_;info_.pixelHeight=(rotation_==90||rotation_==270)?width_:height_;
    }
    const NotesImageInfo&info()const override{return info_;}
    NotesImageFrame decode(unsigned index)override{
        need(index==0&&!decoded_,"Poster sequence contains only its first frame");ComPtr<IMFSample>sample;const auto started=GetTickCount64();
        for(unsigned reads=0;reads<32&&!sample;++reads){DWORD flags{};LONGLONG time{};
            checked(reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&time,&sample),"Decode first movie poster sample");
            need(!(flags&MF_SOURCE_READERF_ERROR),"Poster source reader failed");
            if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)outputType();
            need(sample||!(flags&MF_SOURCE_READERF_ENDOFSTREAM),"Movie contains no first poster frame");
            need(sample||GetTickCount64()-started<5000,"Movie poster produced no sample within bounded work");
        }
        need(bool(sample),"Movie poster exceeded first-sample read bound");outputType();ComPtr<IMFMediaBuffer>buffer;checked(sample->ConvertToContiguousBuffer(&buffer),"Read first poster pixel buffer");
        struct Lock {
            ComPtr<IMFMediaBuffer>buffer;ComPtr<IMF2DBuffer2>two;BYTE*scan{},*start{};DWORD length{};LONG stride{};bool locked{};
            ~Lock(){if(locked){if(two)two->Unlock2D();else buffer->Unlock();}}
        }pixels;pixels.buffer=buffer;
        if(SUCCEEDED(buffer.As(&pixels.two))){checked(pixels.two->Lock2DSize(MF2DBuffer_LockFlags_Read,&pixels.scan,&pixels.stride,&pixels.start,&pixels.length),"Lock bounded poster rows");pixels.locked=true;}
        else{DWORD maximum{};checked(buffer->Lock(&pixels.start,&maximum,&pixels.length),"Lock bounded poster pixels");pixels.locked=true;
            ComPtr<IMFMediaType>actual;checked(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&actual),"Read poster stride");
            UINT32 raw{};if(SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE,&raw)))pixels.stride=static_cast<LONG>(raw);else checked(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1,width_,&pixels.stride),"Derive poster RGB32 stride");
            const auto pitch=std::int64_t(pixels.stride);need(pitch&&std::abs(pitch)>=std::int64_t(width_)*4&&std::abs(pitch)*height_<=pixels.length,"Invalid contiguous poster stride");
            pixels.scan=pixels.start+(pitch<0?std::size_t(-pitch)*(height_-1):0);
        }
        need(pixels.scan&&pixels.start&&pixels.stride&&std::abs(std::int64_t(pixels.stride))>=std::int64_t(width_)*4,"Invalid bounded poster row buffer");
        const auto base=reinterpret_cast<std::uintptr_t>(pixels.start);need(pixels.length<=std::numeric_limits<std::uintptr_t>::max()-base,"Invalid poster buffer range");const auto end=base+pixels.length;
        NotesImageFrame frame{info_.pixelWidth,info_.pixelHeight,0,{}};frame.straightRGBA.resize(std::size_t(frame.width)*frame.height*4);
        const auto scan=reinterpret_cast<std::uintptr_t>(pixels.scan);
        for(unsigned y=0;y<height_;++y){const auto delta=std::int64_t(y)*pixels.stride;std::uintptr_t row{};
            if(delta<0){need(std::uint64_t(-delta)<=scan,"Invalid reversed poster row");row=scan-static_cast<std::uintptr_t>(-delta);}else{need(std::uint64_t(delta)<=std::numeric_limits<std::uintptr_t>::max()-scan,"Invalid poster row offset");row=scan+static_cast<std::uintptr_t>(delta);}
            need(row>=base&&row<=end&&std::size_t(width_)*4<=end-row,"Poster row exceeds returned buffer");const auto*source=reinterpret_cast<const BYTE*>(row);
            for(unsigned x=0;x<width_;++x){unsigned dx=x,dy=y;if(rotation_==90){dx=height_-1-y;dy=x;}else if(rotation_==180){dx=width_-1-x;dy=height_-1-y;}else if(rotation_==270){dx=y;dy=width_-1-x;}
                const auto at=(std::size_t(dy)*frame.width+dx)*4;frame.straightRGBA[at]=source[x*4+2];frame.straightRGBA[at+1]=source[x*4+1];frame.straightRGBA[at+2]=source[x*4];frame.straightRGBA[at+3]=255;
            }
        }
        decoded_=true;return frame;
    }
};
}
// Invoked only by the existing serial Notes import worker. Like original
// NotesMediaFactory.importFile, this resolves referenced movie metadata; it
// never renders audio, reads a full decoded video sample, or creates a device.
NotesImageInfo inspectNotesVideoFile(NotesImageAccess access){
    if(access.path.empty())throw std::invalid_argument("Missing independently owned movie access");
    Platform platform;ComPtr<IMFSourceReader>reader;
    checked(MFCreateSourceReaderFromURL(access.path.c_str(),nullptr,&reader),"Open referenced movie metadata");
    checked(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE),"Disable metadata-only stream delivery");
    ComPtr<IMFMediaType>type;checked(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&type),"Movie has no video track");
    GUID major{};checked(type->GetGUID(MF_MT_MAJOR_TYPE,&major),"Read native movie track type");if(major!=MFMediaType_Video)throw std::invalid_argument("Movie has no video track");
    UINT32 width{},height{};checked(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&width,&height),"Read native movie dimensions");
    const auto rotation=MFGetAttributeUINT32(type.Get(),MF_MT_VIDEO_ROTATION,0);if(rotation==90||rotation==270)std::swap(width,height);else if(rotation!=0&&rotation!=180)throw std::invalid_argument("Unsupported native movie rotation metadata");
    Variant duration;checked(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,MF_PD_DURATION,&duration.value),"Read movie duration");
    if(duration.value.vt!=VT_UI8)throw std::invalid_argument("Movie duration is unavailable");
    const auto seconds=double(duration.value.uhVal.QuadPart)/10000000.;if(!width||!height||width>65536||height>65536||!std::isfinite(seconds)||seconds<=0||seconds>31536000)throw std::invalid_argument("Invalid source movie metadata");
    return {width,height,1,modules::NotesMediaKind::video,{},seconds};
}
std::unique_ptr<NativeNotesImageDecoder::Sequence>makeNotesVideoPosterSequence(const NotesImageRequest&r,NotesImageAccess access){return std::make_unique<VideoPoster>(r,std::move(access));}
}
#endif
