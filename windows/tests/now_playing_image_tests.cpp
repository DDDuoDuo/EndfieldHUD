#include "native/now_playing_image.hpp"
#include "modules/now_playing_model.hpp"
#include <iostream>
#include "core/data/json.hpp"
#include <fstream>
#include <cmath>
#include <future>
#include <stdexcept>
namespace n=endfield::native;using Q=endfield::app::UtilityExecutor;
namespace {unsigned checks{};void check(bool b,const char*w){++checks;if(!b)throw std::runtime_error(w);}auto bytes(unsigned n){return std::make_shared<const std::vector<std::uint8_t>>(2,static_cast<std::uint8_t>(n));}auto frame(unsigned n){auto p=std::make_shared<n::NotesImageFrame>();p->width=p->height=1;p->straightRGBA={static_cast<std::uint8_t>(n),0,0,255};return p;}void flush(Q&q){q.waitIdle();q.drain();}
void boundedQueue(){Q q([]{});unsigned notices{},decodes{};std::promise<void>entered,release;auto gate=release.get_future().share();auto a=bytes(1),b=bytes(2),c=bytes(3);n::NativeNowPlayingArtwork art(q,[&]{++notices;},[&](auto data){++decodes;if(data[0]==1){entered.set_value();gate.wait();}return n::NowPlayingImageResult{frame(data[0])};});
 check(art.request(a),"First artwork event schedules a decode");entered.get_future().wait();for(unsigned k=0;k<100;++k)check(!art.request(a),"Repeated identical immutable payload is a no-op");art.request(b);art.request(c);check(art.stats().inFlight&&art.stats().pending&&q.stats().accepted==1,"Busy queue retains only latest requested artwork");release.set_value();flush(q);flush(q);check(decodes==2&&notices==1&&art.result().image->straightRGBA[0]==3,"Obsolete image cannot flash between current content; replaced pending never decoded");check(art.stats().discarded==1,"Stale completion is accounted");art.request(b);flush(q);art.request(c);check(decodes==3&&art.result().image->straightRGBA[0]==3&&art.stats().cacheHits==1,"Two-entry source cache reuses completed current immutable thumbnail");art.cancel();check(!art.result().image&&!art.stats().pending,"Hide clears output and latest request");art.request(c);check(art.result().image&&decodes==3,"Existing immutable cached bytes can be reused after conceal");
}
void failuresAndLifetime(){Q q([]{});unsigned notices{},decodes{};auto bad=bytes(9),good=bytes(10);auto art=std::make_unique<n::NativeNowPlayingArtwork>(q,[&]{++notices;},[&](auto data){++decodes;if(data[0]==9)return n::NowPlayingImageResult{{},n::NowPlayingImageFailure::decode,-4};return n::NowPlayingImageResult{frame(data[0])};});art->request(bad);flush(q);check(art->result().failure==n::NowPlayingImageFailure::decode&&art->result().nativeError==-4,"Actual decoder failure remains missing artwork");art->request(good);flush(q);art->request(bad);check(decodes==2&&!art->result().image,"Negative result is cached without repeated decode");auto huge=std::make_shared<const std::vector<std::uint8_t>>(endfield::modules::nowPlayingMaximumArtworkBytes+1);art->request(huge);check(art->result().failure==n::NowPlayingImageFailure::invalidPayload&&decodes==2,"Source byte bound rejects before worker submission");art.reset();
 std::promise<void>entered,release;auto gate=release.get_future().share();art=std::make_unique<n::NativeNowPlayingArtwork>(q,[&]{++notices;},[&](auto){entered.set_value();gate.wait();return n::NowPlayingImageResult{frame(3)};});art->request(good);entered.get_future().wait();const auto before=notices;art.reset();release.set_value();flush(q);check(notices==before,"Destroyed owner receives no late image callback");
 art=std::make_unique<n::NativeNowPlayingArtwork>(q,[&]{art.reset();},[](auto){return n::NowPlayingImageResult{frame(5)};});art->request(good);flush(q);check(!art,"Completion may destroy the owner safely");
}
void invalidResult(){Q q([]{});auto b=bytes(7);n::NativeNowPlayingArtwork art(q,[]{},[](auto){auto i=frame(1);i->width=513;return n::NowPlayingImageResult{i};});art.request(b);flush(q);check(!art.result().image&&art.result().failure==n::NowPlayingImageFailure::decode,"Injected decoder cannot publish an invalid/oversize frame");check(n::decodeNowPlayingImage({}).failure==n::NowPlayingImageFailure::invalidPayload,"Empty source bytes fail before platform codec");}
// Windows decodes covers to the slot's drawn size (up to 1024 pixels) rather
// than the source's 512-pixel thumbnail; anything larger is still rejected.
void coverBound(){Q q([]{});
 const auto sized=[](unsigned w){auto i=std::make_shared<n::NotesImageFrame>();i->width=w;i->height=1;i->straightRGBA.assign(std::size_t(w)*4,255);return i;};
 auto a=bytes(11),b=bytes(12);
 n::NativeNowPlayingArtwork wide(q,[]{},[&](auto){return n::NowPlayingImageResult{sized(endfield::modules::nowPlayingMaximumCoverPixels)};});wide.request(a);flush(q);
 check(wide.result().image&&wide.result().image->width==1024,"A cover up to 1024 pixels per side (the drawn-size bound) is accepted");
 n::NativeNowPlayingArtwork over(q,[]{},[&](auto){return n::NowPlayingImageResult{sized(endfield::modules::nowPlayingMaximumCoverPixels+1)};});over.request(b);flush(q);
 check(!over.result().image&&over.result().failure==n::NowPlayingImageFailure::decode,"Beyond 1024 pixels a decoder cannot publish a frame");
 check(n::decodeNowPlayingCover({},660).failure==n::NowPlayingImageFailure::invalidPayload,"The sized decode rejects empty bytes before the codec");
}
#ifdef _WIN32
// A 24-bit bottom-up BMP (WIC decodes it like any cover).
std::vector<std::uint8_t>bitmap(unsigned w,unsigned h,bool(*white)(unsigned,unsigned)){
 const unsigned row=(w*3+3)/4*4,data=row*h,size=54+data;std::vector<std::uint8_t>b(size);
 const auto put=[&](std::size_t at,std::uint32_t v,unsigned n){for(unsigned k=0;k<n;++k)b[at+k]=static_cast<std::uint8_t>(v>>(8*k));};
 b[0]='B';b[1]='M';put(2,size,4);put(10,54,4);put(14,40,4);put(18,w,4);put(22,h,4);put(26,1,2);put(28,24,2);put(34,data,4);
 for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto v=white(x,h-1-y)?255:0;for(unsigned c=0;c<3;++c)b[54+std::size_t(y)*row+x*3+c]=static_cast<std::uint8_t>(v);}
 return b;
}
// The cover pixel size chosen vs the drawn size, and the filter: a cover
// larger than the 660-pixel slot is scaled down to exactly 660 with an area
// filter (each output pixel averages its source pixels: no aliasing), a
// smaller one keeps its size (never enlarged on the CPU), and the source path
// keeps its 512-pixel thumbnail.
void drawnSize(){
 const auto stripes=bitmap(2640,8,[](unsigned x,unsigned){return x%4==0;}); // one white column in four
 const auto cover=n::decodeNowPlayingCover(stripes,660);
 check(cover.image&&cover.image->width==660&&cover.image->height==2,"A 2640-pixel cover is decoded to exactly the 660-pixel drawn size");
 bool averaged=bool(cover.image);
 if(cover.image)for(unsigned y=0;y<cover.image->height;++y)for(unsigned x=1;x+1<cover.image->width;++x){const auto v=cover.image->straightRGBA[(std::size_t(y)*cover.image->width+x)*4];averaged=averaged&&v>=40&&v<=150;}
 check(averaged,"Downscaling averages every source column (area filter): a quarter-white grey, never a pure stripe");
 const auto thumbnail=n::decodeNowPlayingCover(bitmap(100,100,[](unsigned x,unsigned y){return (x+y)%2==0;}),660);
 check(thumbnail.image&&thumbnail.image->width==100&&thumbnail.image->height==100,"A smaller cover keeps its own size");
 const auto source=n::decodeNowPlayingImage(stripes);
 check(source.image&&source.image->width==512,"The source decode still makes a 512-pixel thumbnail");
 const auto bounded=n::decodeNowPlayingCover(stripes,5000);
 check(bounded.image&&bounded.image->width==endfield::modules::nowPlayingMaximumCoverPixels,"The drawn size is bounded at 1024 pixels");
}
#endif
void capacity(){Q q([]{},1);auto route=q.makeRoute();std::promise<void>entered,release;auto gate=release.get_future().share();q.submit(route,[&]{entered.set_value();gate.wait();},[](auto){});entered.get_future().wait();unsigned decoded{};n::NativeNowPlayingArtwork art(q,[]{},[&](auto){++decoded;return n::NowPlayingImageResult{frame(1)};});auto b=bytes(2);std::weak_ptr<const std::vector<std::uint8_t>>weak=b;art.request(b);check(art.stats().pending&&!art.stats().inFlight&&art.stats().backpressure==1,"Full shared queue retains latest request without another thread/timer");release.set_value();flush(q);check(art.submitPending(),"Owner completion event retries bounded shared queue capacity");flush(q);check(decoded==1&&art.result().image,"Retried artwork publishes once");art.cancel();b.reset();check(weak.expired(),"Two-thumbnail cache does not retain retired compressed payload storage");}
void source(const char*path){std::ifstream in(path,std::ios::binary);check(bool(in),"Read immutable original artwork fixture");const auto j=ehud::data::Json::parse(std::string((std::istreambuf_iterator<char>(in)),{}),16384);check(j["sourceCommit"].string()=="ca04f142185c7de40acd8523bdb563195d90a1d1","Original ImageIO decode fixture is pinned");
#ifdef _WIN32
 for(const auto&row:j["images"].array()){std::vector<std::uint8_t>b;const auto&s=row["encodedHex"].string();for(std::size_t k=0;k<s.size();k+=2)b.push_back(static_cast<std::uint8_t>(std::stoul(s.substr(k,2),nullptr,16)));const auto result=n::decodeNowPlayingImage(b);if(row["width"].integer()==0){check(!result.image&&result.failure!=n::NowPlayingImageFailure::none,"Malformed original fixture stays missing");continue;}check(result.image&&result.image->width==row["width"].integer()&&result.image->height==row["height"].integer(),"Original first-frame dimensions and EXIF orientation match WIC");check(result.image->straightRGBA.size()==row["rgba"].array().size(),"Bounded native thumbnail matches original dimensions");for(std::size_t k=0;k<result.image->straightRGBA.size();++k)check(std::abs(int(result.image->straightRGBA[k])-row["rgba"].array()[k].integer())<=1,"Unscaled synthetic sRGB and alpha sample matches actual original ImageIO decode");}
#endif
}
}
int main(int argc,char**argv){try{check(argc==2,"Pass original owner/artwork source fixture");source(argv[1]);boundedQueue();failuresAndLifetime();invalidResult();coverBound();
#ifdef _WIN32
drawnSize();
#endif
capacity();std::cout<<"Now Playing artwork: "<<checks<<" checks passed (synthetic bytes/decoder)\n";return 0;}catch(const std::exception&e){std::cerr<<"Now Playing artwork failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
