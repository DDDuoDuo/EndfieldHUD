#include "native/reader_document.hpp"
#include "native/layer_raster.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace {
using namespace endfield::modules;using namespace endfield::native;namespace fs=std::filesystem;
unsigned checks{};
void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
void hr(HRESULT value){if(FAILED(value))throw std::runtime_error("Owned Reader predecessor fixture COM initialization failed");}
struct Apartment {explicit Apartment(DWORD mode){hr(CoInitializeEx(nullptr,mode));}~Apartment(){CoUninitialize();}};
struct Fixture {
    fs::path directory=fs::temp_directory_path()/("endfield-reader-predecessor-"+ehud::data::makeUUID());
    fs::path path=directory/L"mixed-width.txt";
    Fixture(){fs::create_directory(directory);std::u16string text;
        // One long paragraph: reverse shaping can pack different preceding
        // characters than the actual preceding page's forward line wrapping.
        for(unsigned n=0;n<2400;++n){text+=u"中文混合宽度段落：窄字il与宽字MW比较，";text+=readerUTF8(std::to_string(n));text+=u"。WindowsDirectWrite_unequal_widths 12345 (标点，括号！) 全角ＡＢ和半角ABC；连续阅读不应在换页时替换离开的文字。";}
        const auto bytes=readerUTF8(text);std::ofstream file(path,std::ios::binary);file.write(bytes.data(),std::streamsize(bytes.size()));check(bool(file),"Write only owned mixed-width synthetic TXT fixture");
    }
    ~Fixture(){std::error_code error;fs::remove_all(directory,error);}
    ReaderBook book()const{const auto value=path.u8string();return windowsReaderReference(ehud::data::makeUUID(),std::string(reinterpret_cast<const char*>(value.data()),value.size()),"Owned predecessor fixture");}
};
const ReaderPage*find(const std::vector<ReaderPage>&pages,const ReaderLocation&at){const auto row=std::find_if(pages.begin(),pages.end(),[&](const auto&p){return p.location==at;});return row==pages.end()?nullptr:&*row;}
void walk(const LayerFontResources&fonts){Fixture fixture;NativeReaderDocument provider(fonts);provider.open(fixture.book(),{});
    const auto prefs=ReaderPreferences::defaults("Noto Sans SC");const endfield::core::Point size{376,334};
    auto pages=provider.render({}, {},prefs,size,true,{});check(pages.size()==2&&pages.front().next,"Initial source text returns current and forward neighbor");
    unsigned wrongAnchors{},replacedOutgoing{},extraLayouts{},wrongNextPrevious{};const auto initial=provider.stats();
    if(initial.textLayouts!=2)++extraLayouts;
    const auto*firstNext=find(pages,*pages.front().next);check(firstNext!=nullptr,"First forward neighbor is prepared");
    if(firstNext->previous!=std::optional<ReaderLocation>(pages.front().location))++wrongNextPrevious;
    std::uint64_t maximumStepLayouts{};
    for(unsigned step=0;step<24;++step){const auto outgoing=pages.front();check(outgoing.next.has_value(),"Long synthetic paragraph retains a forward page");
        const auto*residentNext=find(pages,*outgoing.next);check(residentNext&&residentNext->pixels,"Next page was already resident");const auto residentPixels=residentNext->pixels;
        const auto before=provider.stats();auto advanced=provider.render(outgoing.next,{},prefs,size,true,{});const auto after=provider.stats();
        check(!advanced.empty()&&advanced.front().location==*outgoing.next,"Forward render starts at the exact saved UTF16 anchor");
        check(advanced.front().pixels==residentPixels,"Forward render retains the preloaded current bitmap identity");
        if(advanced.front().previous!=std::optional<ReaderLocation>(outgoing.location))++wrongAnchors;
        const auto*previous=advanced.front().previous?find(advanced,*advanced.front().previous):nullptr;
        if(!previous||previous->location!=outgoing.location||previous->pixels!=outgoing.pixels)++replacedOutgoing;
        const auto*next=advanced.front().next?find(advanced,*advanced.front().next):nullptr;check(next!=nullptr,"Forward neighbor remains available during the walk");
        if(next->previous!=std::optional<ReaderLocation>(advanced.front().location))++wrongNextPrevious;
        const auto layouts=after.textLayouts-before.textLayouts;maximumStepLayouts=std::max(maximumStepLayouts,layouts);if(layouts!=1||after.pagesRendered-before.pagesRendered!=1)++extraLayouts;
        check(after.cachedPages<=3,"Exact predecessor reuse stays within the existing three-page cache");
        pages=std::move(advanced);
    }
    std::cout<<"Reader predecessor diagnostics: wrongAnchors="<<wrongAnchors<<" replacedOutgoing="<<replacedOutgoing<<" wrongNextPrevious="<<wrongNextPrevious<<" extraLayoutSteps="<<extraLayouts<<" initialLayouts="<<initial.textLayouts<<" maxStepLayouts="<<maximumStepLayouts<<std::endl;
    check(wrongAnchors==0,"Forward page previous must equal the exact outgoing anchor, not an independently reshaped suffix");
    check(replacedOutgoing==0,"Forward seam must retain the exact outgoing resident bitmap");
    check(wrongNextPrevious==0,"Fresh next neighbor must reuse the current page as its exact predecessor");
    check(extraLayouts==0,"One forward page requires one paragraph layout and no predecessor binary-search layouts");
    // Returning within the resident cache needs no raster or shape work.
    const auto previous=pages.front().previous;check(previous.has_value(),"Resident reverse neighbor remains known");
    const auto*residentPrevious=find(pages,*previous);check(residentPrevious!=nullptr,"Reverse neighbor is already resident");
    const auto expected=residentPrevious->pixels;const auto reverse=provider.render(previous,{},prefs,size,true,{});
    check(reverse.front().pixels==expected,"Reverse request preserves resident page pixels");
    provider.release();check(provider.stats().cachedPages==0,"Release clears all three page references");
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass the bundled font directory");Apartment mainApartment(COINIT_APARTMENTTHREADED);
    LayerFontResources fonts;{LayerRasterizer raster(fs::u8path(argv[1]));fonts=raster.retainedFontResources();}
    std::exception_ptr error;std::thread worker([&]{try{Apartment workerApartment(COINIT_MULTITHREADED);walk(fonts);}catch(...){error=std::current_exception();}});worker.join();if(error)std::rethrow_exception(error);
    std::cout<<"Reader predecessor: "<<checks<<" checks passed (owned native provider fixture; no window or user document)\n";return 0;
}catch(const std::exception&e){std::cerr<<"Reader predecessor failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
#else
int main(){std::cout<<"Reader predecessor requires native Windows DirectWrite; not executed\n";return 77;}
#endif
