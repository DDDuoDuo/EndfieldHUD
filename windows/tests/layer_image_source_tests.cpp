#include "native/layer_image_source.hpp"
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

namespace {std::atomic<bool>counting{};std::atomic<std::size_t>allocations{};unsigned checks{};}
void*operator new(std::size_t n){if(counting)++allocations;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void*operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void*p,std::size_t)noexcept{std::free(p);}void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
#endif
namespace n=endfield::native;
namespace {
void check(bool v,const char*m){++checks;if(!v)throw std::runtime_error(m);}
template<class F>void rejects(F f,const char*m){bool rejected{};try{f();}catch(const std::invalid_argument&){rejected=true;}check(rejected,m);}
constexpr std::array<std::uint8_t,16>rgba{255,0,0,255, 0,255,0,128, 90,40,10,0, 12,34,56,64};
void pixels(){
    n::LayerImageSource source;const auto image=source.publish("synthetic-icon",1,2,2,rgba);const std::array<std::uint8_t,16>expected{0,0,255,255,0,128,0,128,0,0,0,0,14,9,3,64};
    check(image->key()=="synthetic-icon"&&image->revision()==1&&image->width()==2&&image->height()==2&&image->rowBytes()==8,"Immutable identity/dimensions retained");check(std::equal(expected.begin(),expected.end(),image->premultipliedBGRA().begin()),"RGBA converts once to encoded-sRGB premultiplied BGRA with explicit rounding");
    check(source.publish("synthetic-icon",1,2,2,rgba)==image&&source.stats().publications==1,"Equal content revision reuses same immutable snapshot");auto transparent=rgba;transparent[8]=0;transparent[9]=255;check(source.publish("synthetic-icon",1,2,2,transparent)==image,"Same rendered transparent channels reuse snapshot without retaining a duplicate RGBA copy");
    const auto before=source.stats();auto changed=rgba;changed[0]=20;rejects([&]{source.publish("synthetic-icon",1,2,2,changed);},"Conflicting revision rejects");rejects([&]{source.publish("synthetic-icon",1,1,4,rgba);},"Same byte count with different dimensions rejects");check(source.acquire("synthetic-icon",1)==image&&source.stats().liveBytes==before.liveBytes,"Conflicting replacement preserves previous bytes and lookup");
    auto replacement=source.publish("synthetic-icon",2,2,2,changed);check(replacement!=image&&image->premultipliedBGRA()[2]==255&&replacement->premultipliedBGRA()[2]==20,"Borrowed old pixels survive exact-key replacement");check(!source.acquire("synthetic-icon",1)&&source.acquire("synthetic-icon",2)==replacement,"Lookup never substitutes another revision");rejects([&]{source.publish("synthetic-icon",1,2,2,rgba);},"Old revision cannot replace newer live key");
    check(source.retire("synthetic-icon")&&!source.retire("absent")&&!source.acquire("synthetic-icon",2),"Explicit retirement drops only cache ownership");check(image->premultipliedBGRA()[2]==255&&replacement->premultipliedBGRA()[2]==20&&source.stats().liveImages==2,"Retired scene borrowers remain valid and budgeted");
    check(source.publish("synthetic-icon",2,2,2,changed)==replacement,"Exact borrowed retired revision reactivates without copying");source.clear();check(source.stats().cachedImages==0&&source.stats().liveBytes==32,"Clear keeps borrowed generations alive and accounted");
}
void bounds(){
    n::LayerImageSource source;const auto valid=source.publish("valid",1,2,2,rgba);const auto before=source.stats();
    rejects([&]{source.publish({},1,2,2,rgba);},"Empty key rejects");rejects([&]{source.publish(std::string(513,'a'),1,2,2,rgba);},"Oversized key rejects");rejects([&]{source.publish(std::string("a\0b",3),1,2,2,rgba);},"Embedded zero key rejects");rejects([&]{source.publish(std::string("\xc0\xaf",2),1,2,2,rgba);},"Invalid UTF8 key rejects");rejects([&]{source.publish("zero",0,2,2,rgba);},"Zero revision rejects");
    rejects([&]{source.publish("zero",1,0,2,rgba);},"Empty dimensions reject");rejects([&]{source.publish("wide",1,257,2,rgba);},"Explicit maximum dimension enforced");rejects([&]{source.publish("short",1,3,2,rgba);},"Incomplete tight rows reject");rejects([&]{source.publish("long",1,1,1,rgba);},"Extra bytes reject");
    check(source.stats().publications==before.publications&&source.stats().liveBytes==before.liveBytes&&source.acquire("valid",1)==valid,"All malformed inputs leave prior cache unchanged");
}
void lruAndLifetime(){
    n::LayerImageSource source;std::array<std::string,25>keys;for(std::size_t k=0;k<keys.size();++k)keys[k]="icon-"+std::to_string(k);
    for(std::size_t k=0;k<24;++k)source.publish(keys[k],1,2,2,rgba);auto first=source.acquire(keys[0],1);source.publish(keys[24],1,2,2,rgba);
    check(source.stats().cachedImages==24&&source.stats().evictions==1&&source.acquire(keys[0],1)==first&&!source.acquire(keys[1],1),"Source capacity24 uses bounded LRU and updates touch order");
    auto oldest=source.acquire(keys[2],1);source.clear();check(source.stats().cachedImages==0&&source.stats().liveImages==2,"Clear releases cache-only images while preserving two borrowers");oldest.reset();check(source.stats().liveImages==1&&source.stats().liveBytes==16,"Last retired borrower releases its counted pixel payload");
    allocations=0;counting=true;try{source.publish(keys[0],1,2,2,rgba);for(unsigned k=0;k<1000;++k){auto image=source.acquire(keys[0],1);if(!image)throw std::runtime_error("Unexpected miss");source.publish(keys[0],1,2,2,rgba);}}catch(...){counting=false;throw;}counting=false;check(allocations==0,"Exact acquisitions and repeated same-content publications allocate nothing");
    std::shared_ptr<const n::LayerMemoryImage>survivor;{n::LayerImageSource temporary;survivor=temporary.publish("outlives-provider",1,2,2,rgba);}check(survivor->premultipliedBGRA()[2]==255,"Borrower survives provider destruction without native/resource dangling references");
    std::atomic<bool>wrongThreadRejected{};std::thread other([&]{try{source.acquire(keys[0],1);}catch(const std::invalid_argument&){wrongThreadRejected=true;}survivor.reset();});other.join();check(wrongThreadRejected,"Provider mutation/lookup remains owner-thread-only; immutable borrower release may cross threads");
}
void borrowedBudgets(){
    n::LayerImageSource source;std::vector<std::shared_ptr<const n::LayerMemoryImage>>held;held.reserve(48);
    for(unsigned k=0;k<48;++k)held.push_back(source.publish("held-"+std::to_string(k),1,2,2,rgba));check(source.stats().cachedImages==24&&source.stats().liveImages==48,"Evicted-but-borrowed images count toward total48 capacity");const auto before=source.stats();rejects([&]{source.publish("over-limit",1,2,2,rgba);},"Live-borrower capacity rejects unchanged");check(source.stats().liveImages==before.liveImages&&source.stats().evictions==before.evictions&&!source.acquire("over-limit",1),"No eviction or partial publication on exhausted live capacity");
    held[0].reset();auto resumed=source.publish("after-release",1,2,2,rgba);check(resumed&&source.stats().liveImages==48,"Released retired capacity is immediately reusable without polling");source.clear();held.clear();resumed.reset();check(source.stats().liveBytes==0&&source.stats().liveImages==0,"Explicit clear plus owner retirement drains all pixel storage");
    std::vector<std::uint8_t>large(256u*256u*4u,255);for(unsigned k=0;k<32;++k)held.push_back(source.publish("large-"+std::to_string(k),1,256,256,large));check(source.stats().liveBytes==n::LayerImageSource::maximumLiveBytes,"EightMiB byte bound includes cached and all borrowed retired images");const auto bytes=source.stats().liveBytes;rejects([&]{source.publish("too-many-bytes",1,2,2,rgba);},"Pixel-byte bound fails independently of image count");check(source.stats().liveBytes==bytes&&source.stats().liveImages==32,"Byte-bound failure preserves every owned generation");
}
}
int main(){try{pixels();bounds();lruAndLifetime();borrowedBudgets();std::cout<<"PASS "<<checks<<" memory image source checks\n";return 0;}catch(const std::exception&e){counting=false;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
