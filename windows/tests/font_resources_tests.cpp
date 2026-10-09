#include "native/layer_raster.hpp"
#ifdef _WIN32
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cmath>
#include <exception>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
using namespace endfield::native;
namespace {int checks{};void require(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}void hr(HRESULT value){if(FAILED(value))throw std::runtime_error("Retained font fixture HRESULT="+std::to_string(static_cast<unsigned long>(value)));}}
int main(int argc,char**argv){try{
    hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));struct End{~End(){CoUninitialize();}}end;
    LayerFontResources initial,korean;{LayerRasterizer raster(argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path{});
        initial=raster.retainedFontResources();require(bool(initial)&&initial.revision()==1&&initial.fallbackFamily()=="Noto Sans SC","Initial source default is retained");
        require(raster.setDefaultFontLanguage(LayerFontLanguage::korean),"Language event changes font generation");korean=raster.retainedFontResources();
        require(korean.fallbackFamily()=="Noto Sans KR"&&korean.revision()==2&&initial.revision()==1&&initial.fallbackFamily()=="Noto Sans SC","Snapshots remain immutable across language event");
        require(initial.factory()==korean.factory()&&initial.bundledCollection()==korean.bundledCollection(),"Retained snapshots reuse original factory and private collection");
        require(!raster.setDefaultFontLanguage(LayerFontLanguage::korean),"Equal configuration creates no new generation");
    }
    std::exception_ptr error;std::thread worker([&]{try{hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED));End workerEnd;
        for(const auto&resources:{initial,korean}){UINT32 index{};BOOL exists{};const auto family=resources.fallbackFamily()=="Noto Sans KR"?L"Noto Sans KR":L"Noto Sans SC";
            hr(resources.bundledCollection()->FindFamilyName(family,&index,&exists));require(exists!=FALSE,"Private family survives rasterizer destruction on MTA worker");
            Microsoft::WRL::ComPtr<IDWriteTextFormat>format;hr(resources.factory()->CreateTextFormat(family,resources.bundledCollection(),DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,14,L"zh-cn",&format));
            Microsoft::WRL::ComPtr<IDWriteTextLayout>layout;const wchar_t text[]=L"Reader 123 中文 한국어 \U0001F642";
            hr(resources.factory()->CreateTextLayout(text,static_cast<UINT32>(std::size(text)-1),format.Get(),240,100,&layout));DWRITE_TEXT_METRICS metrics{};hr(layout->GetMetrics(&metrics));
            require(std::isfinite(metrics.height)&&metrics.height>0&&metrics.width>0,"Retained worker resources create actual Unicode layout");
        }
    }catch(...){error=std::current_exception();}});worker.join();if(error)std::rethrow_exception(error);
    LayerFontResources empty;require(!empty&&empty.factory()==nullptr&&empty.revision()==0,"Empty resources are explicit");
    std::cout<<"Retained font resources passed "<<checks<<" checks\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
#else
int main(){return 0;}
#endif
