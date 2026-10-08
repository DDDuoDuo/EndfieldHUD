#include "core/subsection_mask.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif
namespace endfield::core {
namespace {
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
struct Reader {
    std::span<const std::uint8_t> bytes;std::size_t at{};
    std::span<const std::uint8_t> take(std::size_t n){need(n<=bytes.size()-at,"Truncated source subsection mask");const auto out=bytes.subspan(at,n);at+=n;return out;}
    std::uint32_t u32(){auto p=take(4);return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
    double f64(){const auto lo=u32(),hi=u32();const auto v=std::bit_cast<double>(std::uint64_t(lo)|(std::uint64_t(hi)<<32));need(std::isfinite(v),"Nonfinite source mask value");return v;}
    float f32(){const auto v=std::bit_cast<float>(u32());need(std::isfinite(v)&&std::abs(v)<=8192,"Source mask coordinate exceeds local bounds");return v;}
};
std::size_t scalarCount(std::span<const std::uint8_t> ops){std::size_t count{};bool opened{};unsigned paths{};
    for(auto op:ops){need(op<=4,"Invalid source mask command");if(op==0){need(!opened&&++paths<=5,"Invalid source mask subpath");opened=true;}else need(opened,"Source mask command outside subpath");
        count+=op==0||op==1?2:op==2?4:op==3?6:0;if(op==4)opened=false;}
    need(!opened&&paths>0&&count<=192,"Unclosed or oversized source mask");return count;
}
}
struct SubsectionMaskSampler::Impl {
    struct Topology{std::vector<std::uint8_t>ops;std::size_t coordinates{};};
    struct Fit{double lo{},hi{};unsigned topology{},samples{},count{};std::vector<float>values;};
    struct Track{std::vector<Topology>topologies;std::vector<Fit>fits;std::vector<std::array<double,2>>gaps;};
    std::array<Track,2>tracks;Rect viewport;
};
SubsectionMaskSampler::SubsectionMaskSampler(std::span<const std::uint8_t> bytes,std::string_view pin):SubsectionMaskSampler(bytes,pin,viewport){
    need(bytes.size()==98856&&pin==assetSHA256,"Default Shelf mask requires the original pinned source asset");
}
SubsectionMaskSampler::SubsectionMaskSampler(std::span<const std::uint8_t> bytes,std::string_view pin,Rect expectedViewport):impl_(std::make_unique<Impl>()){
    need(bytes.size()<=256*1024&&pin.size()==64&&pin.find_first_not_of("0123456789abcdef")==std::string_view::npos&&packet::sha256(bytes)==pin,"Pinned source subsection mask integrity mismatch");
    need(std::isfinite(expectedViewport.x)&&std::isfinite(expectedViewport.y)&&std::isfinite(expectedViewport.width)&&std::isfinite(expectedViewport.height)&&expectedViewport.width>0&&expectedViewport.height>0&&std::abs(expectedViewport.x)+expectedViewport.width<=8192&&std::abs(expectedViewport.y)+expectedViewport.height<=8192,"Invalid expected source mask viewport");
    impl_->viewport=expectedViewport;Reader r{bytes};const auto magic=r.take(8);need(std::memcmp(magic.data(),"EHSUBC01",8)==0,"Unsupported source mask format");
    for(double expected:{expectedViewport.x,expectedViewport.y,expectedViewport.width,expectedViewport.height,duration})need(r.f64()==expected,"Source mask viewport/timing mismatch");need(r.u32()==2,"Source mask requires both directions");
    for(unsigned direction=0;direction<2;++direction){need(std::bit_cast<std::int32_t>(r.u32())==(direction?1:-1),"Source mask direction order mismatch");auto&t=impl_->tracks[direction];const auto topologies=r.u32();need(topologies>0&&topologies<=16,"Source mask topology budget exceeded");
        for(unsigned n=0;n<topologies;++n){const auto count=r.u32();need(count>0&&count<=40,"Source mask command budget exceeded");const auto ops=r.take(count);Impl::Topology topology;topology.ops.assign(ops.begin(),ops.end());topology.coordinates=scalarCount(topology.ops);t.topologies.push_back(std::move(topology));}
        const auto count=r.u32();need(count>0&&count<=64,"Source mask interval budget exceeded");
        for(unsigned n=0;n<count;++n){Impl::Fit f;f.lo=r.f64();f.hi=r.f64();f.topology=r.u32();f.samples=r.u32();f.count=r.u32();need(f.lo>=0&&f.hi<=1&&f.lo<f.hi&&(t.fits.empty()||f.lo>=t.fits.back().hi),"Invalid source mask interval order");need(f.topology<t.topologies.size()&&(f.samples==1||f.samples==4)&&f.count==t.topologies[f.topology].coordinates,"Invalid source mask sample shape");f.values.reserve(f.samples*f.count);for(unsigned k=0;k<f.samples*f.count;++k)f.values.push_back(r.f32());t.fits.push_back(std::move(f));}
        const auto gaps=r.u32();need(gaps<=16,"Source mask discontinuity budget exceeded");for(unsigned n=0;n<gaps;++n){const auto lo=r.f64(),hi=r.f64();need(lo>=0&&hi<=1&&lo<hi&&hi-lo<=1e-12,"Unbounded source mask topology gap");t.gaps.push_back({lo,hi});}
        need(t.fits.front().lo==0&&t.fits.back().hi==1,"Source mask phases incomplete");std::size_t used{};for(std::size_t n=1;n<t.fits.size();++n)if(t.fits[n-1].hi!=t.fits[n].lo){const auto lo=t.fits[n-1].hi,hi=t.fits[n].lo;need(std::count(t.gaps.begin(),t.gaps.end(),std::array{lo,hi})==1,"Unreported source mask phase gap");++used;}need(used==t.gaps.size(),"Unused source mask topology gap");
    }need(r.at==bytes.size(),"Trailing source mask bytes");
}
SubsectionMaskSampler::~SubsectionMaskSampler()=default;
Rect SubsectionMaskSampler::sourceViewport()const noexcept{return impl_->viewport;}
SubsectionCurvePath SubsectionMaskSampler::sample(double direction,double phase)const{
    need(std::isfinite(direction)&&std::isfinite(phase),"Nonfinite source mask phase/direction");phase=std::clamp(phase,0.,1.);const auto&t=impl_->tracks[direction<0?0:1];
    auto found=std::lower_bound(t.fits.begin(),t.fits.end(),phase,[](const auto&f,double value){return f.hi<value;});need(found!=t.fits.end(),"Source mask sample missing");bool gap{};
    if(phase<found->lo){gap=true;const auto previous=std::prev(found);if(phase-previous->hi<=found->lo-phase){found=previous;phase=found->hi;}else phase=found->lo;}
    const auto&f=*found;const auto&topology=t.topologies[f.topology];SubsectionCurvePath out;out.opcodeCount=topology.ops.size();out.coordinateCount=f.count;out.topologyGap=gap;std::copy(topology.ops.begin(),topology.ops.end(),out.opcodes.begin());
    const auto x=(phase-f.lo)/(f.hi-f.lo);std::array<double,4>weights{1,0,0,0};if(f.samples==4){constexpr std::array<double,4>knots{0,1./3,2./3,1};for(unsigned j=0;j<4;++j){weights[j]=1;for(unsigned k=0;k<4;++k)if(j!=k)weights[j]*=(x-knots[k])/(knots[j]-knots[k]);}}
    for(unsigned c=0;c<f.count;++c){double value{};for(unsigned j=0;j<f.samples;++j)value+=weights[j]*f.values[j*f.count+c];out.coordinates[c]=double(static_cast<float>(value));}return out;
}
} // namespace endfield::core
