#include "native/media_assembly_codec.hpp"
#include "core/data/json.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <numbers>

namespace {
namespace n=endfield::native;namespace m=endfield::modules;namespace fs=std::filesystem;using Microsoft::WRL::ComPtr;using J=ehud::data::Json;
unsigned checks{};
std::string current;
void check(bool v,const std::string&why){++checks;current=why;if(!v)throw std::runtime_error(why);}
void hr(HRESULT h,const char*why){if(FAILED(h))throw std::runtime_error(std::string(why)+" HRESULT "+std::to_string(unsigned(h)));}
template<class F>m::MediaAssemblyError failure(F&&f){try{f();}catch(const m::MediaAssemblyFailure&e){return e.error();}throw std::runtime_error("Expected a typed Media Assembly failure after: "+current);}
struct COM{HRESULT result{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};~COM(){if(SUCCEEDED(result))CoUninitialize();}};
struct MF{MF(){hr(MFStartup(MF_VERSION,MFSTARTUP_NOSOCKET),"MFStartup");}~MF(){MFShutdown();}};
struct Temp {fs::path path;Temp(){path=fs::temp_directory_path()/(L"endfield-owned-media-assembly-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));check(!fs::exists(path),"Fresh owned temporary root");fs::create_directories(path);}~Temp(){std::error_code e;fs::remove_all(path,e);}};
std::string utf8(const fs::path&p){return n::mediaAssemblyUTF8(p);}
std::vector<std::uint8_t>gradient(unsigned w,unsigned h,bool alpha){std::vector<std::uint8_t>b(std::size_t(w)*h*4);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto*p=b.data()+(std::size_t(y)*w+x)*4;p[0]=std::uint8_t(x*255/std::max(1u,w-1));p[1]=std::uint8_t(y*255/std::max(1u,h-1));p[2]=std::uint8_t((x*7+y*13)%256);p[3]=alpha?std::uint8_t(255-(x*3)%128):255;}return b;}
// Writes an owned synthetic still through WIC (optionally a second frame and EXIF orientation).
void writeImage(const fs::path&path,const GUID&container,unsigned w,unsigned h,const std::vector<std::uint8_t>&rgba,unsigned frames=1,std::optional<unsigned short>orientation={}){
    ComPtr<IWICImagingFactory>wic;hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"WIC");
    ComPtr<IWICStream>stream;hr(wic->CreateStream(&stream),"stream");hr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"create file");
    ComPtr<IWICBitmapEncoder>enc;hr(wic->CreateEncoder(container,nullptr,&enc),"encoder");hr(enc->Initialize(stream.Get(),WICBitmapEncoderNoCache),"init");
    for(unsigned f=0;f<frames;++f){ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IPropertyBag2>props;hr(enc->CreateNewFrame(&frame,&props),"frame");hr(frame->Initialize(props.Get()),"frame init");hr(frame->SetSize(w,h),"size");
        WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA;hr(frame->SetPixelFormat(&fmt),"format");
        if(orientation){ComPtr<IWICMetadataQueryWriter>meta;hr(frame->GetMetadataQueryWriter(&meta),"metadata writer");PROPVARIANT v{};PropVariantInit(&v);v.vt=VT_UI2;v.uiVal=*orientation;hr(meta->SetMetadataByName(L"/app1/ifd/{ushort=274}",&v),"orientation");}
        std::vector<std::uint8_t>bgra(rgba.size());for(std::size_t i=0;i<rgba.size();i+=4){bgra[i]=rgba[i+2];bgra[i+1]=rgba[i+1];bgra[i+2]=std::uint8_t(rgba[i]^(f*40));bgra[i+3]=rgba[i+3];}
        if(IsEqualGUID(fmt,GUID_WICPixelFormat24bppBGR)){std::vector<std::uint8_t>rgb;const UINT stride=(w*3+3)&~3u;rgb.resize(std::size_t(stride)*h);for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)for(int c=0;c<3;++c)rgb[std::size_t(y)*stride+x*3+c]=bgra[(std::size_t(y)*w+x)*4+c];hr(frame->WritePixels(h,stride,UINT(rgb.size()),rgb.data()),"pixels");}
        else if(IsEqualGUID(fmt,GUID_WICPixelFormat8bppIndexed)){
            ComPtr<IWICBitmap>bitmap;hr(wic->CreateBitmapFromMemory(w,h,GUID_WICPixelFormat32bppBGRA,w*4,UINT(bgra.size()),bgra.data(),&bitmap),"bitmap");
            ComPtr<IWICPalette>palette;hr(wic->CreatePalette(&palette),"palette");hr(palette->InitializeFromBitmap(bitmap.Get(),256,FALSE),"palette init");hr(frame->SetPalette(palette.Get()),"set palette");
            ComPtr<IWICFormatConverter>conv;hr(wic->CreateFormatConverter(&conv),"conv");hr(conv->Initialize(bitmap.Get(),GUID_WICPixelFormat8bppIndexed,WICBitmapDitherTypeNone,palette.Get(),0,WICBitmapPaletteTypeCustom),"conv init");hr(frame->WriteSource(conv.Get(),nullptr),"write source");}
        else hr(frame->WritePixels(h,w*4,UINT(bgra.size()),bgra.data()),"pixels");
        hr(frame->Commit(),"frame commit");}
    hr(enc->Commit(),"commit");hr(stream->Commit(STGC_DEFAULT),"stream commit");
}
std::vector<std::uint8_t>readImage(const fs::path&path,unsigned&w,unsigned&h){
    ComPtr<IWICImagingFactory>wic;hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic)),"WIC");
    ComPtr<IWICBitmapDecoder>dec;hr(wic->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec),"decode");ComPtr<IWICBitmapFrameDecode>f;hr(dec->GetFrame(0,&f),"frame");hr(f->GetSize(&w,&h),"size");
    ComPtr<IWICFormatConverter>c;hr(wic->CreateFormatConverter(&c),"conv");hr(c->Initialize(f.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"conv");
    std::vector<std::uint8_t>out(std::size_t(w)*h*4);hr(c->CopyPixels(nullptr,w*4,UINT(out.size()),out.data()),"copy");return out;
}
// Owned synthetic H.264 movie: frame i is a solid colour (i*12, 255-i*12, 64).
void writeMovie(const fs::path&path,unsigned w,unsigned h,unsigned frames,unsigned fps){
    ComPtr<IMFSinkWriter>writer;hr(MFCreateSinkWriterFromURL(path.c_str(),nullptr,nullptr,&writer),"sink");
    ComPtr<IMFMediaType>out;MFCreateMediaType(&out);out->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);out->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);out->SetUINT32(MF_MT_AVG_BITRATE,2000000);out->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
    MFSetAttributeSize(out.Get(),MF_MT_FRAME_SIZE,w,h);MFSetAttributeRatio(out.Get(),MF_MT_FRAME_RATE,fps,1);MFSetAttributeRatio(out.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);DWORD stream{};hr(writer->AddStream(out.Get(),&stream),"add stream");
    ComPtr<IMFMediaType>in;MFCreateMediaType(&in);in->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);in->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);in->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
    MFSetAttributeSize(in.Get(),MF_MT_FRAME_SIZE,w,h);MFSetAttributeRatio(in.Get(),MF_MT_FRAME_RATE,fps,1);MFSetAttributeRatio(in.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);in->SetUINT32(MF_MT_DEFAULT_STRIDE,w*4);hr(writer->SetInputMediaType(stream,in.Get(),nullptr),"input");
    hr(writer->BeginWriting(),"begin");const LONGLONG duration=10000000/fps;
    for(unsigned i=0;i<frames;++i){ComPtr<IMFMediaBuffer>b;hr(MFCreateMemoryBuffer(w*h*4,&b),"buffer");BYTE*p{};b->Lock(&p,nullptr,nullptr);for(unsigned k=0;k<w*h;++k){const bool top=k/w<h/4;p[k*4]=top?255:64;p[k*4+1]=BYTE(255-i*12);p[k*4+2]=BYTE(i*12);p[k*4+3]=255;}b->Unlock();b->SetCurrentLength(w*h*4);
        ComPtr<IMFSample>s;MFCreateSample(&s);s->AddBuffer(b.Get());s->SetSampleTime(LONGLONG(i)*duration);s->SetSampleDuration(duration);hr(writer->WriteSample(stream,s.Get()),"write");}
    hr(writer->Finalize(),"finalize");
}
std::size_t temporaries(const fs::path&dir){std::size_t n{};for(const auto&e:fs::directory_iterator(dir))if(e.path().filename().wstring().starts_with(L".endfield-export-"))++n;return n;}

void stills(const fs::path&root,const fs::path&assets){
    n::NativeMediaAssemblyEngine engine({assets});
    const auto png=root/L"Owned Photo ü.png";const auto pixels=gradient(64,48,true);writeImage(png,GUID_ContainerFormatPng,64,48,pixels);
    auto d=engine.open(utf8(png));d.id="png";check(!d.video&&d.kind==m::MediaAssemblyKind::image&&d.container=="png"&&d.pixels==endfield::core::Point{64,48}&&d.name=="Owned Photo ü.png","PNG opens with source metadata and a Unicode path");
    check(d.suggestedFilename()=="Owned Photo ü-edited.png","Suggested export name");
    const auto decoded=n::NativeMediaAssemblyEngine::decodeStill(d,1024);check(decoded.width==64&&decoded.height==48&&decoded.straightRGBA==pixels,"Lossless still decodes exactly");
    auto preview=engine.preview(d,{},0);check(preview.width==64&&preview.height==48,"Bounded preview of a small image keeps its size");
    int worst{};for(std::size_t i=0;i<pixels.size();++i)if(pixels[i|3]>0)worst=std::max(worst,std::abs(int(preview.straightRGBA[i])-int(pixels[i])));check(worst<=1,"Identity preview is the source within unassociation rounding");
    const auto before=engine.stats();engine.preview(d,{},0);check(engine.stats().previewCacheHits==before.previewCacheHits+1&&engine.stats().previewDecodes==before.previewDecodes,"Unchanged source frame is cached");
    m::MediaAssemblyAdjustments a;a.crop={.25,.25,.5,.5};a.rotationQuarterTurns=1;a.exposure=.5;a.filter=m::MediaAssemblyFilter::filter2;
    preview=engine.preview(d,a,0);check(preview.width==24&&preview.height==32,"Preview applies crop and rotation");
    {// Deferred (GPU) preview: the same request without CPU edits.
        auto filtered=a;filtered.filter=m::MediaAssemblyFilter::filter4;filtered.stickers.push_back({"s",m::MediaAssemblyStickerKind::sticker2,.5,.5,.3,0});
        const auto before=engine.stats();const auto deferred=engine.previewSource(d,filtered,0);
        check(deferred.deferred()&&deferred.straightRGBA.empty()&&deferred.width==24&&deferred.height==32&&deferred.source&&deferred.source->width==64&&deferred.source->height==48,"Deferred preview carries the cached bounded source and the edited size");
        check(deferred.edits&&deferred.edits->filter==m::MediaAssemblyFilter::filter4&&deferred.edits->stickers.empty()&&deferred.lookup.size()==32*32*32*3&&deferred.lookupOwner,"Deferred preview carries the edits (no stickers) and the borrowed filter cube");
        check(engine.stats().deferredPreviews==before.deferredPreviews+1&&engine.stats().previewDecodes==before.previewDecodes,"Deferred preview reuses the cached decode");}
    engine.clearCaches();check(engine.stats().cachedSourceBytes==0,"clearCaches releases the bounded source");
    // EXIF orientation 6 swaps the dimensions and rotates pixels.
    const auto jpeg=root/L"rotated.jpg";writeImage(jpeg,GUID_ContainerFormatJpeg,40,20,gradient(40,20,false),1,6);
    auto j=engine.open(utf8(jpeg));check(j.container=="jpeg"&&j.pixels==endfield::core::Point{20,40},"EXIF orientation 5-8 swaps the source dimensions");
    const auto oriented=n::NativeMediaAssemblyEngine::decodeStill(j,std::nullopt);check(oriented.width==20&&oriented.height==40,"Oriented decode");
    // Large picture previews at <=1024.
    const auto big=root/L"big.png";writeImage(big,GUID_ContainerFormatPng,3000,2000,gradient(3000,2000,false));auto b=engine.open(utf8(big));b.id="big";
    preview=engine.preview(b,{},0);check(preview.width==1024&&preview.height==683,"Preview decodes at most 1024 px");
    // Animated GIF: first frame; multi-page TIFF is rejected like the source.
    const auto gif=root/L"anim.gif";writeImage(gif,GUID_ContainerFormatGif,32,16,gradient(32,16,false),3);auto g=engine.open(utf8(gif));
    check(g.kind==m::MediaAssemblyKind::gif&&g.frameCount==3&&g.animated()&&g.container=="gif","Animated GIF metadata");
    check(n::NativeMediaAssemblyEngine::decodeStill(g,1024).width==32,"GIF first frame on its logical screen");
    const auto tiff=root/L"pages.tiff";writeImage(tiff,GUID_ContainerFormatTiff,16,16,gradient(16,16,false),2);check(failure([&]{engine.open(utf8(tiff));})==m::MediaAssemblyError::unsupported,"Multi-frame non-GIF images are unsupported");
    const auto text=root/L"notes.txt";{std::ofstream f(text);f<<"Owned synthetic non-media bytes.";}check(failure([&]{engine.open(utf8(text));})==m::MediaAssemblyError::unsupported,"Non-media is unsupported");
    check(failure([&]{engine.open(utf8(root/L"missing.png"));})==m::MediaAssemblyError::sourceUnavailable&&failure([&]{engine.open(utf8(root));})==m::MediaAssemblyError::unavailable,"A missing source asks to be chosen again; a directory is unavailable");
    // File identity and atomic operations.
    const auto ops=n::mediaAssemblyFileOperations();const auto id=n::mediaAssemblyFileIdentity(png);check(id&&id->bytes==fs::file_size(png)&&!n::mediaAssemblyFileIdentity(root/L"missing"),"Identity of a regular file");
    const auto copy=root/L"copy.png";fs::copy_file(png,copy);check(!ops.move(utf8(copy),utf8(png),false)&&fs::exists(copy),"Exclusive move refuses an existing target");
    check(n::mediaAssemblySamePath(utf8(png),utf8(root/L"sub"/L".."/L"Owned Photo ü.png"))&&!n::mediaAssemblySamePath(utf8(png),utf8(copy)),"Path identity");
    // Export: PNG with stickers, exists/overwrite, container rule, cancel, race.
    a=m::MediaAssemblyAdjustments{};a.saturation=1.4;a.stickers.push_back({"s",m::MediaAssemblyStickerKind::sticker7,.5,.5,.5,20});
    const auto out=root/L"out.png";m::MediaAssemblyExportTicket t;auto docPtr=std::make_shared<m::MediaAssemblyDocumentInfo>(d);
    check(engine.exportMedia({docPtr,a,utf8(out),false,"t1",false},t)==utf8(out)&&fs::exists(out)&&temporaries(root)==0,"PNG export commits atomically");
    unsigned w{},h{};const auto written=readImage(out,w,h);check(w==64&&h==48,"Export keeps full resolution");
    {std::vector<std::uint8_t>sticker;unsigned sw{},sh{};sticker=readImage(assets/L"stickers"/L"sticker_1.png",sw,sh);const auto art=m::mediaAssemblyPremultiply(sticker);const std::array<m::MediaAssemblyStickerArtwork,1>arts{{{sw,sh,art}}};
        const m::MediaAssemblyFrameProcessor p(64,48,a,{},true,arts);const auto expected=p.render({64,48,256,pixels,false},m::MediaAssemblyOutputAlpha::straight);
        int err{};for(std::size_t i=0;i<expected.size();++i)if(expected[i|3]>8)err=std::max(err,std::abs(int(expected[i])-int(written[i])));check(err<=2,"Exported PNG equals the portable original apply() including stickers");}
    check(failure([&]{m::MediaAssemblyExportTicket u;engine.exportMedia({docPtr,a,utf8(out),false,"t2",false},u);})==m::MediaAssemblyError::exists&&temporaries(root)==0,"Existing destination without overwrite");
    for(const auto*ext:{L"jpg",L"tiff"}){const auto p=root/(std::wstring(L"out.")+ext);m::MediaAssemblyExportTicket u;engine.exportMedia({docPtr,{},utf8(p),false,std::string("t-")+char('a'+ext[0]%26),false},u);unsigned ww{},hh{};readImage(p,ww,hh);check(ww==64&&hh==48,"JPEG/TIFF export decodes");}
    {m::MediaAssemblyExportTicket u;const auto identityBefore=n::mediaAssemblyFileIdentity(png);auto fresh=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(png)));
        check(engine.exportMedia({fresh,{},utf8(png),true,"t4",false},u)==utf8(png)&&n::mediaAssemblyFileIdentity(png)!=identityBefore&&temporaries(root)==0,"Overwrite original replaces atomically with a new identity");
        check(failure([&]{m::MediaAssemblyExportTicket x;engine.exportMedia({fresh,{},utf8(png),true,"t5",false},x);})==m::MediaAssemblyError::changedOnDisk,"The previous document identity is stale after overwrite");
        // A JPEG source exported under another name/type is an ordinary new export;
        // overwriting the original itself must keep its container type.
        auto jdoc=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(jpeg)));const auto asPNG=fs::path(jpeg).replace_extension(L".png");
        {m::MediaAssemblyExportTicket x;check(engine.exportMedia({jdoc,{},utf8(asPNG),false,"t7",false},x)==utf8(asPNG)&&fs::exists(asPNG),"A different name/type is an ordinary new export");}
        const auto disguised=root/L"rotated-as.png";fs::copy_file(jpeg,disguised);auto disguisedDoc=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(disguised)));
        check(disguisedDoc->container=="jpeg"&&failure([&]{m::MediaAssemblyExportTicket x;engine.exportMedia({disguisedDoc,{},utf8(disguised),true,"t7b",false},x);})==m::MediaAssemblyError::unsupportedExport&&temporaries(root)==0,"Overwriting a JPEG original as PNG keeps the source container rule");
        (void)jdoc;}
    {auto fresh=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(png)));m::MediaAssemblyExportTicket u;u.cancel();check(failure([&]{engine.exportMedia({fresh,{},utf8(root/L"c.png"),false,"t8",false},u);})==m::MediaAssemblyError::cancelled&&!fs::exists(root/L"c.png")&&temporaries(root)==0,"Cancelled export writes nothing");}
    {n::NativeMediaAssemblyEngine racing({assets,[&]{std::ofstream f(png,std::ios::app|std::ios::binary);f<<'x';}});auto fresh=std::make_shared<m::MediaAssemblyDocumentInfo>(racing.open(utf8(png)));m::MediaAssemblyExportTicket u;
        check(failure([&]{racing.exportMedia({fresh,{},utf8(root/L"race.png"),false,"t9",false},u);})==m::MediaAssemblyError::changedOnDisk&&!fs::exists(root/L"race.png")&&temporaries(root)==0,"Source changed before commit is reported and leaves nothing");}
    {auto fresh=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(png)));std::ofstream(png,std::ios::app|std::ios::binary)<<'y';check(failure([&]{engine.preview(*fresh,{},0);})==m::MediaAssemblyError::changedOnDisk,"Preview re-checks the source identity");}
    {const auto gone=root/L"gone.png";fs::copy_file(png,gone);auto fresh=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(gone)));fs::remove(gone);
        check(failure([&]{engine.preview(*fresh,{},0);})==m::MediaAssemblyError::sourceUnavailable,"A removed original asks to be chosen again before a preview");
        m::MediaAssemblyExportTicket u;check(failure([&]{engine.exportMedia({fresh,{},utf8(root/L"gone-export.png"),false,"t10",false},u);})==m::MediaAssemblyError::sourceUnavailable&&!fs::exists(root/L"gone-export.png")&&temporaries(root)==0,"...and before an export");
        fs::create_directory(gone);check(failure([&]{engine.preview(*fresh,{},0);})==m::MediaAssemblyError::sourceUnavailable,"A directory in its place is not the original");fs::remove(gone);}
    {// Source bookmark: a renamed or moved original is found again by its file ID.
        const auto original=root/L"moving.png";fs::copy_file(png,original);auto doc=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(original)));doc->id="moving";
        check(n::mediaAssemblyLocate(utf8(original),doc->identity)==utf8(original),"An unmoved original stays at its stored path");
        const auto folder=root/L"moved here";fs::create_directory(folder);const auto moved=folder/L"renamed ü.png";fs::rename(original,moved);
        check(n::mediaAssemblySamePath(n::mediaAssemblyLocate(utf8(original),doc->identity),utf8(moved)),"A renamed and moved original is found again by its file ID");
        const auto shown=engine.preview(*doc,{},0);check(shown.width==64&&shown.height==48,"Preview reads a moved original where it now is");
        const auto deferred=engine.previewSource(*doc,{},0);check(deferred.deferred()&&deferred.source->width==64,"Deferred preview reads it there too");
        {m::MediaAssemblyExportTicket u;const auto out=root/L"from-moved.png";
            check(engine.exportMedia({doc,{},utf8(out),false,"mv1",false},u)==utf8(out)&&fs::exists(out)&&fs::exists(moved)&&!fs::exists(original)&&temporaries(root)==0,"Export reads a moved original where it now is");}
        fs::copy_file(jpeg,original);engine.clearCaches();
        check(n::mediaAssemblySamePath(n::mediaAssemblyLocate(utf8(original),doc->identity),utf8(moved))&&engine.preview(*doc,{},0).width==64,"A different file at the stored path does not replace the original");
        {m::MediaAssemblyExportTicket u;check(engine.exportMedia({doc,{},utf8(original),true,"mv2",false},u)==utf8(original)&&n::mediaAssemblyFileIdentity(moved)==doc->identity&&temporaries(root)==0,
            "Overwrite original writes the document's own path and leaves the moved original untouched");}
        std::ofstream(moved,std::ios::app|std::ios::binary)<<'z';check(failure([&]{engine.preview(*doc,{},0);})==m::MediaAssemblyError::changedOnDisk,"A moved original that changed is reported");
        fs::remove(moved);fs::remove(original);check(failure([&]{engine.preview(*doc,{},0);})==m::MediaAssemblyError::sourceUnavailable&&n::mediaAssemblyLocate(utf8(original),doc->identity)==utf8(original),
            "A moved original that is gone asks to be chosen again");
        fs::remove(folder);}
    {// Stills: the export job's GPU device edits float row bands and the CPU
     // composites the stickers (source CIContext); same result as the exact CPU export.
        const auto photo=root/L"opaque photo.png";writeImage(photo,GUID_ContainerFormatPng,301,203,gradient(301,203,false));
        auto doc=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(photo)));doc->id="opaque";
        m::MediaAssemblyAdjustments e;e.exposure=.4;e.saturation=1.3;e.temperature=5200;e.tint=10;e.highlights=.6;e.shadows=.3;e.curve={0,.2,.55,.8,1};e.filter=m::MediaAssemblyFilter::filter3;
        e.crop={.1,.05,.8,.9};e.rotationQuarterTurns=3;e.mirrored=true;
        e.stickers.push_back({"a",m::MediaAssemblyStickerKind::sticker7,.4,.5,.5,20});e.stickers.push_back({"b",m::MediaAssemblyStickerKind::sticker2,.8,.2,.3,-35});
        const auto warp=[]()->std::shared_ptr<void>{Microsoft::WRL::ComPtr<ID3D11Device>device;const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_10_0;
            if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,nullptr,nullptr)))return {};
            return std::shared_ptr<void>(device.Detach(),[](void*p){static_cast<ID3D11Device*>(p)->Release();});};
        n::NativeMediaAssemblyEngine viaGpu({assets,{},{},warp,0}),viaCpu({assets,{},{},[]{return std::shared_ptr<void>{};},0});
        {n::NativeMediaAssemblyEngine threshold({assets,{},{},warp});m::MediaAssemblyExportTicket t;
            check(threshold.exportMedia({doc,e,utf8(root/L"below.png"),false,"s0",false},t)==utf8(root/L"below.png")&&threshold.stats().cpuStillExports==1&&threshold.stats().gpuStillExports==0,
                "A still below the GPU size threshold finishes on the CPU without a device");fs::remove(root/L"below.png");}
        const auto gpuOut=root/L"opaque-gpu.png",cpuOut=root/L"opaque-cpu.png";m::MediaAssemblyExportTicket t1,t2;
        check(viaGpu.exportMedia({doc,e,utf8(gpuOut),false,"s1",false},t1)==utf8(gpuOut)&&viaCpu.exportMedia({doc,e,utf8(cpuOut),false,"s2",false},t2)==utf8(cpuOut)&&temporaries(root)==0,"GPU and CPU still exports commit");
        check(viaGpu.stats().gpuStillExports==1&&viaGpu.stats().cpuStillExports==0&&viaCpu.stats().cpuStillExports==1&&viaCpu.stats().gpuStillExports==0,"An opaque still exports on the job's GPU device; without one on the CPU");
        // GPU and CPU exports of the same edit, compared byte for byte.
        unsigned sequence{};const auto both=[&](const std::shared_ptr<m::MediaAssemblyDocumentInfo>&document,const m::MediaAssemblyAdjustments&edit,const char*name){
            const auto a=root/(std::wstring(L"gpu-")+std::to_wstring(++sequence)+L".png"),b=root/(std::wstring(L"cpu-")+std::to_wstring(sequence)+L".png");m::MediaAssemblyExportTicket u1,u2;
            const auto gpuBefore=viaGpu.stats().gpuStillExports;
            check(viaGpu.exportMedia({document,edit,utf8(a),false,"g"+std::to_string(sequence),false},u1)==utf8(a)&&viaCpu.exportMedia({document,edit,utf8(b),false,"c"+std::to_string(sequence),false},u2)==utf8(b),std::string("GPU and CPU exports commit: ")+name);
            check(viaGpu.stats().gpuStillExports==gpuBefore+1&&viaGpu.stats().cpuStillExports==0,std::string("The still exports on the job's GPU device: ")+name);
            unsigned aw{},ah{},bw{},bh{};const auto x=readImage(a,aw,ah),y=readImage(b,bw,bh);int worst{};std::size_t over{};
            for(std::size_t i=0;i<x.size()&&i<y.size();++i){const int delta=std::abs(int(x[i])-int(y[i]));worst=std::max(worst,delta);over+=delta>1;}
            std::cout<<"gpu/cpu still export "<<name<<' '<<aw<<"x"<<ah<<" worst="<<worst<<" over1="<<over<<" of "<<x.size()<<'\n';
            check(aw==bw&&ah==bh&&x.size()==y.size()&&!x.empty()&&worst<=1,std::string("The GPU still export matches the exact CPU export within one level, stickers included: ")+name);
            fs::remove(a);fs::remove(b);};
        {unsigned gw{},gh{},cw{},ch{};const auto onGpu=readImage(gpuOut,gw,gh),onCpu=readImage(cpuOut,cw,ch);int worst{};for(std::size_t i=0;i<onGpu.size()&&i<onCpu.size();++i)worst=std::max(worst,std::abs(int(onGpu[i])-int(onCpu[i])));
            check(gw==cw&&gh==ch&&onGpu.size()==onCpu.size()&&worst<=1,"The GPU still export matches the exact CPU export within one level, stickers included");}
        // Translucent stills take the GPU too: float rows unassociate exactly.
        const auto clear=root/L"translucent.png";writeImage(clear,GUID_ContainerFormatPng,40,30,gradient(40,30,true));auto see=std::make_shared<m::MediaAssemblyDocumentInfo>(viaGpu.open(utf8(clear)));
        both(see,e,"translucent");
        // Strong colour edits without a LUT push colour out of [0, 1] under
        // the translucent sticker edges; the float rows composite like the CPU.
        m::MediaAssemblyAdjustments bright;bright.exposure=1.5;bright.contrast=1.6;bright.saturation=1.4;bright.stickers=e.stickers;
        both(doc,bright,"extended-range stickers");both(see,bright,"extended-range stickers translucent");
        // Time (printed): a 12 MP heavy edit on the hardware export device vs the CPU.
        const auto big=root/L"twelve.jpg";writeImage(big,GUID_ContainerFormatJpeg,4000,3000,gradient(4000,3000,false));
        auto large=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(big)));large->id="twelve";const bool hardware=bool(n::createMediaAssemblyExportDevice());
        n::NativeMediaAssemblyEngine onDevice({assets});const auto timed=[&](n::NativeMediaAssemblyEngine&with,const fs::path&out,const char*id){
            m::MediaAssemblyExportTicket t;const auto started=GetTickCount64();check(with.exportMedia({large,e,utf8(out),false,id,false},t)==utf8(out),"12 MP export commits");return GetTickCount64()-started;};
        const auto deviceMs=timed(onDevice,root/L"twelve-device.jpg","s4"),cpuMs=timed(viaCpu,root/L"twelve-cpu.jpg","s5");
        std::cout<<"12 MP heavy still export: export device "<<(hardware?"hardware":"none")<<" "<<deviceMs<<" ms (gpu stills "<<onDevice.stats().gpuStillExports<<"), CPU "<<cpuMs<<" ms\n";
        check(onDevice.stats().gpuStillExports==(hardware?1u:0u),"A hardware export device takes the opaque 12 MP still");
        for(const auto*name:{L"twelve.jpg",L"twelve-device.jpg",L"twelve-cpu.jpg"})fs::remove(root/name);}
    std::cout<<"HEIC encoding available: "<<(engine.heicEncoder()?"yes":"no")<<'\n';
    {
        // Source CGImageDestinationCopyTypeIdentifiers filtering: HEIC is only
        // offered when a real encode works; otherwise .heic is unsupported.
        auto fresh=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(png)));const auto heic=root/L"out.heic";m::MediaAssemblyExportTicket u;std::optional<m::MediaAssemblyError>error;
        try{engine.exportMedia({fresh,{},utf8(heic),false,"h1",engine.heicEncoder()},u);}catch(const m::MediaAssemblyFailure&f){error=f.error();}
        std::cout<<"HEIC export: "<<(error?"refused ("+std::to_string(int(*error))+")":std::string("written"))<<'\n';
        if(engine.heicEncoder()){unsigned hw{},hh{};check(!error&&fs::exists(heic)&&(readImage(heic,hw,hh),hw==64&&hh==48),"Offered HEIC export round-trips");}
        else{const auto offered=engine.heicEncoder()?std::vector<std::string>{}:m::mediaAssemblyExportExtensions(*fresh,engine.heicEncoder());
            check(error==m::MediaAssemblyError::unsupportedExport&&!fs::exists(heic)&&temporaries(root)==0&&std::find(offered.begin(),offered.end(),"heic")==offered.end(),"Without a working HEVC encoder HEIC is neither offered nor written");}
    }
}
void movies(const fs::path&root,const fs::path&assets){
    MF mf;const auto movie=root/L"owned-movie.mp4";writeMovie(movie,128,96,20,10);
    {n::NativeMediaAssemblyEngine probe({assets});const auto c=probe.capabilities();
        std::cout<<"codec capabilities: heifDecode="<<c.heifDecode<<" hevcDecode="<<c.hevcDecode<<" heicEncode="<<c.heicEncode<<" webpDecode="<<c.webpDecode<<" h264Encode="<<c.h264Encode<<" aacEncode="<<c.aacEncode<<" movExport="<<c.movExport<<'\n';
        check(c.h264Encode&&!c.movExport&&c.heicEncode==probe.heicEncoder(),"MP4 export capability is present, MOV export is an explicit gap and HEIC follows the real encode probe");}
    n::NativeMediaAssemblyEngine engine({assets});auto d=engine.open(utf8(movie));d.id="movie";
    check(d.video&&d.kind==m::MediaAssemblyKind::video&&d.pixels==endfield::core::Point{128,96}&&std::abs(d.duration-2)<.11,"Movie metadata");check(d.suggestedFilename()=="owned-movie-edited.mp4","MP4 suggestion");
    const auto frame=n::NativeMediaAssemblyEngine::decodeVideoFrame(d,1.0,1024);const auto*p=frame.straightRGBA.data()+(48*128+64)*4;
    check(frame.width==128&&frame.height==96&&std::abs(int(p[0])-120)<=16&&std::abs(int(p[1])-135)<=16,"Frame at time within the +/-0.08 s tolerance");
    const auto*topRow=frame.straightRGBA.data()+(4*128+64)*4;const auto*lowRow=frame.straightRGBA.data()+(90*128+64)*4;
    std::cout<<"movie rows: top blue "<<int(topRow[2])<<" bottom blue "<<int(lowRow[2])<<'\n';
    check(topRow[2]>200&&lowRow[2]<100,"Top-down rows survive writer and reader");
    m::MediaAssemblyAdjustments a;a.brightness=.2;const auto preview=engine.preview(d,a,.5);check(preview.width==128&&preview.height==96,"Movie preview with edits");
    {const auto deferred=engine.previewSource(d,a,.5);check(deferred.deferred()&&deferred.width==128&&deferred.height==96&&deferred.source&&deferred.source->width==128,"Deferred movie preview shares the cached frame");}
    a.trimStart=.5;a.trimEnd=1.5;a.crop={0,0,.5,1};const auto out=root/L"trim.mp4";m::MediaAssemblyExportTicket t;double last{};
    n::NativeMediaAssemblyEngine hooked({assets,{},[&](double s){last=s;check(t.progress()>=0&&t.progress()<=1,"Progress stays bounded");}});
    auto doc=std::make_shared<m::MediaAssemblyDocumentInfo>(d);check(hooked.exportMedia({doc,a,utf8(out),false,"v1",false},t)==utf8(out)&&temporaries(root)==0,"MP4 export commits");
    auto e=engine.open(utf8(out));check(e.video&&e.pixels==endfield::core::Point{64,96}&&std::abs(e.duration-1)<.2&&last>.7,"Trimmed, cropped, even MP4");
    e.id="exported";const auto first=n::NativeMediaAssemblyEngine::decodeVideoFrame(e,0,1024);const auto*q=first.straightRGBA.data()+(48*64+32)*4;check(q[0]>40,"Exported frames carry the brightness edit");
    check(failure([&]{m::MediaAssemblyExportTicket u;engine.exportMedia({doc,{},utf8(root/L"x.mov"),false,"v2",false},u);})==m::MediaAssemblyError::unsupportedExport,"MOV is an explicit Windows gap");
    m::MediaAssemblyExportTicket c;std::atomic<int>frames{};n::NativeMediaAssemblyEngine cancelling({assets,{},[&](double){if(++frames==3)c.cancel();}});
    check(failure([&]{cancelling.exportMedia({doc,{},utf8(root/L"cancel.mp4"),false,"v3",false},c);})==m::MediaAssemblyError::cancelled&&!fs::exists(root/L"cancel.mp4")&&temporaries(root)==0,"Cancelled video export leaves no file");
    // The export job's GPU composition (an injected isolated WARP device)
    // and the CPU processor fallback produce the same edited movie.
    {
        m::MediaAssemblyAdjustments g;g.trimStart=.25;g.trimEnd=1.25;g.exposure=.3;g.highlights=.5;g.shadows=.4;g.temperature=5000;g.curve={0,.3,.55,.8,1};
        g.filter=m::MediaAssemblyFilter::filter3;g.crop={.1,.1,.8,.75};g.rotationQuarterTurns=1;g.mirrored=true;
        const auto warp=[]()->std::shared_ptr<void>{Microsoft::WRL::ComPtr<ID3D11Device>device;const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_10_0;
            if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,nullptr,nullptr)))return {};
            return std::shared_ptr<void>(device.Detach(),[](void*p){static_cast<ID3D11Device*>(p)->Release();});};
        n::NativeMediaAssemblyEngine viaGpu({assets,{},{},warp}),viaCpu({assets,{},{},[]{return std::shared_ptr<void>{};}});
        const auto gpuOut=root/L"gpu.mp4",cpuOut=root/L"cpu.mp4";m::MediaAssemblyExportTicket t1,t2;
        check(viaGpu.exportMedia({doc,g,utf8(gpuOut),false,"v4",false},t1)==utf8(gpuOut)&&viaCpu.exportMedia({doc,g,utf8(cpuOut),false,"v5",false},t2)==utf8(cpuOut)&&temporaries(root)==0,"GPU and CPU movie exports commit");
        const auto gs=viaGpu.stats(),cs=viaCpu.stats();std::cout<<"video export frames gpu="<<gs.gpuExportFrames<<"/"<<gs.cpuExportFrames<<" cpu="<<cs.gpuExportFrames<<"/"<<cs.cpuExportFrames<<'\n';
        check(gs.gpuExportFrames>=8&&gs.cpuExportFrames==0&&cs.cpuExportFrames==gs.gpuExportFrames&&cs.gpuExportFrames==0,"The GPU job edits every frame; the fallback edits the same frames on the CPU");
        auto ge=engine.open(utf8(gpuOut)),ce=engine.open(utf8(cpuOut));ge.id="gpu";ce.id="cpu";
        check(ge.pixels==ce.pixels&&ge.pixels==endfield::core::Point{72,102}&&std::abs(ge.duration-ce.duration)<1e-3,"Same even, rotated, cropped composition size and duration");
        for(double at:{0.,.5,.9}){
            const auto a1=n::NativeMediaAssemblyEngine::decodeVideoFrame(ge,at,1024),a2=n::NativeMediaAssemblyEngine::decodeVideoFrame(ce,at,1024);
            check(a1.width==a2.width&&a1.height==a2.height,"Decoded frame sizes match");double total{};int worst{};
            for(std::size_t i=0;i<a1.straightRGBA.size();++i){const int dlt=std::abs(int(a1.straightRGBA[i])-int(a2.straightRGBA[i]));total+=dlt;worst=std::max(worst,dlt);}
            const double mean=total/double(a1.straightRGBA.size());std::cout<<"gpu/cpu movie frame "<<at<<" mean="<<mean<<" worst="<<worst<<'\n';
            // Inputs differ by at most one level before the lossy H.264 encode.
            check(mean<1.5&&worst<=24,"GPU-edited movie frames equal the CPU-edited frames within encoder noise");
        }
    }
}
}
std::vector<std::uint8_t>base64(std::string_view text){
    std::vector<std::uint8_t>out;unsigned buffer{},bits{};
    for(char c:text){int v=c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:c=='/'?63:-1;if(v<0)continue;buffer=(buffer<<6)|unsigned(v);bits+=6;if(bits>=8){bits-=8;out.push_back(std::uint8_t(buffer>>bits));}}
    return out;
}
// Colour-managed stills (tests/fixtures/media-assembly-icc-source.json,
// tools/media_assembly_icc_reference.py): owned PNGs with synthetic ICC
// profiles decoded by WIC and edited by the engine, compared with the pinned
// still preview and export on the Mac. The embedded profile is read from the
// PNG (IWICColorContext profile bytes) and converted per pixel like Core
// Image (MediaAssemblySourceColour), on the CPU and on the export job's GPU,
// so colour outside sRGB survives the edits as on the Mac. The former WIC
// colour transform (Windows Color System, perceptual gamut mapping) missed
// these stills by up to 24 levels on in-gamut pixels.
void colourManaged(const fs::path&root,const fs::path&assets,const fs::path&fixturePath){
    std::ifstream in(fixturePath,std::ios::binary);check(bool(in),"ICC fixture");const std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    const auto fixture=J::parse(text,2*1024*1024);
    check(fixture["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1"&&!fixture["usesAppOrWindow"].boolean(),"ICC fixture uses the authoritative isolated oracle");
    const unsigned W=unsigned(fixture["width"].integer());
    std::vector<std::pair<std::string,m::MediaAssemblyAdjustments>>edits;
    for(const auto&e:fixture["edits"].array()){
        m::MediaAssemblyAdjustments a;
        for(const auto&[key,value]:e["adjustments"].object()){
            if(key=="saturation")a.saturation=value.number();else if(key=="exposure")a.exposure=value.number();
            else if(key=="filter"){const auto f=m::mediaAssemblyFilters();const auto at=std::find_if(f.begin(),f.end(),[&](const auto&i){return i.id==value.string();});check(at!=f.end(),"Known ICC fixture filter");a.filter=m::MediaAssemblyFilter(at-f.begin());}
            else check(false,"Unknown ICC fixture adjustment "+key);
        }
        edits.emplace_back(e["name"].string(),a);
    }
    const auto warp=[]()->std::shared_ptr<void>{Microsoft::WRL::ComPtr<ID3D11Device>device;const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_10_0;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&level,1,D3D11_SDK_VERSION,&device,nullptr,nullptr)))return {};
        return std::shared_ptr<void>(device.Detach(),[](void*p){static_cast<ID3D11Device*>(p)->Release();});};
    // CPU engine (no export device) and a GPU engine that takes every still.
    n::NativeMediaAssemblyEngine engine({assets,{},{},[]{return std::shared_ptr<void>{};},0}),onGpu({assets,{},{},warp,0});unsigned serial{};std::size_t converted{},exports{};
    for(const auto&image:fixture["images"].array()){
        const auto name=image["name"].string(),kind=image["kind"].string();const unsigned H=unsigned(image["height"].integer());
        const auto file=root/(L"icc-"+std::wstring(name.begin(),name.end())+L".png");
        {const auto bytes=base64(image["png"].string());std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));check(bool(out),"Owned ICC still written");}
        auto doc=std::make_shared<m::MediaAssemblyDocumentInfo>(engine.open(utf8(file)));doc->id="icc-"+name;
        check(doc->pixels==endfield::core::Point{double(W),double(H)},"ICC still opens at its size: "+name);
        // The profile reaches the decoded source: converted unless it is sRGB.
        const bool convert=kind!="none"&&name!="srgb-matrix";converted+=convert;
        const auto decoded=n::NativeMediaAssemblyEngine::decodeStill(*doc,1024);check(bool(decoded.colour)==convert,"The decoded still carries its embedded profile: "+name);
        {const auto deferred=engine.previewSource(*doc,{},0);check(deferred.source&&bool(deferred.source->colour)==convert,"Deferred previews carry the embedded profile to the GPU: "+name);}
        // Pixels a wide-gamut profile places outside sRGB.
        const auto raw=base64(image["identityFloat"].string());check(raw.size()==std::size_t(W)*H*16,"Identity floats: "+name);
        std::vector<bool>outside(std::size_t(W)*H);std::size_t outsideCount{};
        for(std::size_t p=0;p<outside.size();++p)for(int c=0;c<3;++c){float v;std::memcpy(&v,raw.data()+(p*4+c)*4,4);if(v<-.5f/255||v>1+.5f/255)outside[p]=true;}
        for(const bool o:outside)outsideCount+=o;
        const auto&cases=image["cases"].array();
        for(std::size_t k=0;k<cases.size();++k){
            const auto&c=cases[k];const auto&[editName,a]=edits[k];check(c["name"].string()==editName,"ICC case order");
            const auto expectExport=base64(c["export"].string());check(expectExport.size()==std::size_t(W)*H*4,"ICC case bytes: "+name+" "+editName);
            int worst[2][2]{};const auto measure=[&](const std::vector<std::uint8_t>&got,int path){check(got.size()==expectExport.size(),"ICC output size");
                for(std::size_t p=0;p<outside.size();++p)for(int ch=0;ch<4;++ch){const std::size_t i=p*4+ch;worst[path][outside[p]]=std::max(worst[path][outside[p]],std::abs(int(got[i])-int(expectExport[i])));}};
            for(int path=0;path<2;++path){auto&with=path?onGpu:engine;const auto gpuBefore=with.stats().gpuStillExports;
                const auto out=root/(L"icc-out-"+std::to_wstring(++serial)+L".png");m::MediaAssemblyExportTicket t;
                check(with.exportMedia({doc,a,utf8(out),false,"icc"+std::to_string(serial),false},t)==utf8(out),"ICC still exports: "+name+" "+editName);
                check(with.stats().gpuStillExports==gpuBefore+(path?1u:0u),"The ICC still exports on the intended path");
                unsigned w{},h{};measure(readImage(out,w,h),path);fs::remove(out);check(w==W&&h==H,"ICC export size");++exports;}
            std::cout<<"icc "<<name<<' '<<editName<<": export worst in sRGB "<<worst[0][0]<<" outside "<<worst[0][1]<<"; GPU export worst in sRGB "<<worst[1][0]<<" outside "<<worst[1][1]<<" ("<<outsideCount<<" of "<<outside.size()<<" pixels outside sRGB)";
            check(std::max({worst[0][0],worst[0][1],worst[1][0],worst[1][1]})<=1,"Edited ICC stills export within one level of the Mac: "+name+" "+editName);
            if(c.contains("preview")){
                const auto expectPreview=base64(c["preview"].string());const auto preview=engine.preview(*doc,a,0);check(preview.width==W&&preview.height==H&&preview.straightRGBA.size()==expectPreview.size(),"ICC preview size");
                int previewWorst{};for(std::size_t i=0;i<expectPreview.size();++i)previewWorst=std::max(previewWorst,std::abs(int(preview.straightRGBA[i])-int(expectPreview[i])));
                std::cout<<"; preview worst "<<previewWorst;check(previewWorst<=1,"ICC stills preview within one level of the Mac: "+name);}
            std::cout<<'\n';
        }
        fs::remove(file);
    }
    check(converted==20&&exports==2*(9*4+13),"Every ICC fixture still was exported on both paths");
}

int wmain(int argc,wchar_t**argv){
    COM com;try{check(SUCCEEDED(com.result)&&argc==3,"Pass windows/resources/media-assembly and the ICC fixture");Temp temp;colourManaged(temp.path,fs::absolute(argv[1]),fs::absolute(argv[2]));stills(temp.path,fs::absolute(argv[1]));movies(temp.path,fs::absolute(argv[1]));
        std::cout<<"PASS "<<checks<<" native Media Assembly codec checks\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){std::cout<<"Native Media Assembly codec needs Windows\n";return 0;}
#endif
