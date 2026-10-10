#include "native/profile_image.hpp"
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace {
namespace m=endfield::modules;namespace n=endfield::native;using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>m::ProfileFailure failure(F f){try{f();}catch(const m::ProfileError&e){return e.code();}throw std::runtime_error("Expected a profile image failure");}
struct Temporary{std::filesystem::path root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("EndfieldHUD-Profile-Images-"+ehud::data::makeUUID());Temporary(){std::filesystem::create_directories(root);}~Temporary(){std::error_code e;std::filesystem::remove_all(root,e);}};
std::vector<std::uint8_t>read(const std::filesystem::path&p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
// Synthetic encodes written only into the owned temporary folder.
void encode(const std::filesystem::path&path,const GUID&container,UINT width,UINT height,int orientation=0){
    ComPtr<IWICImagingFactory>f;check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f))),"WIC factory");
    ComPtr<IWICStream>stream;check(SUCCEEDED(f->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)),"Fixture stream");
    ComPtr<IWICBitmapEncoder>encoder;check(SUCCEEDED(f->CreateEncoder(container,nullptr,&encoder))&&SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)),"Fixture encoder");
    ComPtr<IWICBitmapFrameEncode>frame;ComPtr<IPropertyBag2>bag;check(SUCCEEDED(encoder->CreateNewFrame(&frame,&bag))&&SUCCEEDED(frame->Initialize(bag.Get()))&&SUCCEEDED(frame->SetSize(width,height)),"Fixture frame");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;check(SUCCEEDED(frame->SetPixelFormat(&format)),"Fixture format");
    if(orientation){ComPtr<IWICMetadataQueryWriter>writer;check(SUCCEEDED(frame->GetMetadataQueryWriter(&writer)),"EXIF writer");PROPVARIANT v;PropVariantInit(&v);v.vt=VT_UI2;v.uiVal=static_cast<USHORT>(orientation);check(SUCCEEDED(writer->SetMetadataByName(L"/app1/ifd/{ushort=274}",&v)),"EXIF orientation");}
    const UINT stride=(width*(format==GUID_WICPixelFormat24bppBGR?3:4)+3)&~3u;std::vector<BYTE>pixels(std::size_t(stride)*height);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){auto*p=&pixels[std::size_t(y)*stride+x*3];p[0]=BYTE(x);p[1]=BYTE(y);p[2]=BYTE(x+y);}
    check(SUCCEEDED(frame->WritePixels(height,stride,UINT(pixels.size()),pixels.data()))&&SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"Fixture commit");
}
void run(){
    Temporary t;const auto images=t.root/"Profile"/"Images";
    const auto avatarSource=t.root/"avatar.png";encode(avatarSource,GUID_ContainerFormatPng,300,200);
    const auto name=n::importProfileImage(avatarSource,m::ProfileImageKind::avatar,images);
    check(name.size()==42&&name.ends_with(".image")&&ehud::data::validUUID(name.substr(0,36))&&name.substr(0,36)==[&]{auto s=name.substr(0,36);for(auto&c:s)c=char(toupper(c));return s;}(),"Avatar keeps an uppercase <UUID>.image name");
    check(read(images/name)==read(avatarSource),"Avatar keeps its original encoded bytes");
    // The account's downsampled game avatar arrives as encoded bytes.
    const auto bytes=read(avatarSource);const auto fromMemory=n::importProfileImageBytes(bytes,m::ProfileImageKind::avatar,images);
    check(fromMemory.ends_with(".image")&&fromMemory!=name&&read(images/fromMemory)==bytes,"In-memory game avatar keeps its encoded bytes");
    const std::string junk="not an image";
    check(failure([&]{n::importProfileImageBytes(std::span(reinterpret_cast<const std::uint8_t*>(junk.data()),junk.size()),m::ProfileImageKind::avatar,images);})==m::ProfileFailure::image,"Unreadable game avatar bytes");
    check(failure([&]{n::importProfileImageBytes({},m::ProfileImageKind::avatar,images);})==m::ProfileFailure::image,"Empty game avatar bytes");
    const auto memoryBackground=n::importProfileImageBytes(bytes,m::ProfileImageKind::background,images);
    check(memoryBackground.ends_with(".png")&&n::decodeProfileImage(images/memoryBackground,m::ProfileImageKind::background).image.width==300,"In-memory background becomes a managed PNG");
    const auto rotated=t.root/"rotated.jpg";encode(rotated,GUID_ContainerFormatJpeg,3000,1000,6);
    const auto avatar=n::importProfileImage(rotated,m::ProfileImageKind::avatar,images);const auto decoded=n::decodeProfileImage(images/avatar,m::ProfileImageKind::avatar);
    check(decoded.orientation==6&&decoded.sourceWidth==3000&&decoded.sourceHeight==1000&&decoded.image.width==3000&&decoded.image.height==1000,"Avatar decode keeps native pixels and reports EXIF orientation");
    const auto reduced=n::decodeProfileImage(images/avatar,m::ProfileImageKind::avatar,1024);check(reduced.image.width==1024&&reduced.image.height==341,"Bounded avatar working copy");
    const auto background=n::importProfileImage(rotated,m::ProfileImageKind::background,images);
    check(background.ends_with(".png"),"Background becomes a managed PNG");
    const auto thumb=n::decodeProfileImage(images/background,m::ProfileImageKind::background);
    check(thumb.image.width==683&&thumb.image.height==2048&&thumb.orientation==1,"Background is oriented and at most 2048 px");
    // Source failures and messages.
    std::ofstream(t.root/"note.txt")<<"not an image";
    check(failure([&]{n::importProfileImage(t.root/"note.txt",m::ProfileImageKind::avatar,images);})==m::ProfileFailure::image,"Unreadable file");
    check(failure([&]{n::importProfileImage(t.root,m::ProfileImageKind::background,images);})==m::ProfileFailure::image,"A folder is not an image");
    const auto wide=t.root/"wide.png";encode(wide,GUID_ContainerFormatPng,33000,1);
    check(failure([&]{n::importProfileImage(wide,m::ProfileImageKind::avatar,images);})==m::ProfileFailure::imageDimensions,"Portrait side limit 32,768 px");
    check(n::importProfileImage(wide,m::ProfileImageKind::background,images).ends_with(".png"),"Backgrounds have no portrait dimension limit");
    const auto huge=t.root/"huge.png";{std::ofstream(huge).put('x');}std::filesystem::resize_file(huge,128ull*1024*1024+1);
    check(failure([&]{n::importProfileImage(huge,m::ProfileImageKind::avatar,images);})==m::ProfileFailure::imageTooLarge,"128 MB source limit");
    check(m::profileFailureMessage(m::ProfileFailure::imageTooLarge,endfield::core::Language::english)=="Choose an image smaller than 128 MB.","Source message");
    // Owner-thread service on the shared executor.
    endfield::app::UtilityExecutor executor([]{},4);std::vector<std::string>imported,decodedNames;std::vector<m::ProfileFailure>failures;
    n::ProfileImageService service(images,executor,{[&](m::ProfileImageKind,std::string v){imported.push_back(v);},[&](m::ProfileImageKind,m::ProfileFailure f,std::string){failures.push_back(f);},
        [&](const std::string&v,std::shared_ptr<const n::ProfileDecodedImage>){decodedNames.push_back(v);},[&](const std::string&,m::ProfileFailure f){failures.push_back(f);}});
    check(service.import(avatarSource,m::ProfileImageKind::avatar)&&!service.import(avatarSource,m::ProfileImageKind::avatar),"One import at a time");
    executor.waitIdle();executor.drain();check(imported.size()==1&&!service.busy(),"Import completes on the owner drain");
    service.request(imported[0],m::ProfileImageKind::avatar);service.request(imported[0],m::ProfileImageKind::avatar);executor.waitIdle();executor.drain();
    check(decodedNames.size()==1&&service.cached(imported[0]),"Coalesced decode is cached");
    service.request(imported[0],m::ProfileImageKind::avatar);check(decodedNames.size()==2,"Cached image is reported without worker work");
    service.import(t.root/"note.txt",m::ProfileImageKind::background);executor.waitIdle();executor.drain();check(failures.size()==1&&failures[0]==m::ProfileFailure::image,"Import failure reaches the owner");
    service.discard(imported[0]);executor.waitIdle();executor.drain();check(!std::filesystem::exists(images/imported[0])&&!service.cached(imported[0]),"Rolled-back import is removed");
    executor.shutdown();
}
}
int main(){
    const auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{run();if(SUCCEEDED(hr))CoUninitialize();std::cout<<"PASS "<<checks<<" Personal Profile image checks\n";return 0;}
    catch(const std::exception&e){if(SUCCEEDED(hr))CoUninitialize();std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
#else
int main(){return 0;}
#endif
