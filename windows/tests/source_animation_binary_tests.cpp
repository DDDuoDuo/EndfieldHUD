#include "core/source_animation_binary.hpp"
#include "core/shell_packet.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace endfield::core::source;
namespace packet=endfield::core::packet;
namespace {
std::size_t checks{};const std::string pin(64,'a');
void check(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f,std::string_view reason){bool rejected=false;try{f();}catch(const std::invalid_argument&e){rejected=std::string_view(e.what()).find(reason)!=std::string_view::npos;}check(rejected,"Compiled animation rejects malformed input for its intended reason");}
bool same(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
void compare(const Library&a,const Library&b){
    check(a.clips().size()==b.clips().size(),"Exact clip count");
    for(std::size_t i=0;i<a.clips().size();++i){const auto&x=a.clips()[i];const auto&y=b.clips()[i];
        check(x.id==y.id&&x.binding==y.binding&&x.name==y.name&&x.wrapMode==y.wrapMode&&same(x.sampleRate,y.sampleRate)&&same(x.lastKeyTime,y.lastKeyTime),"Exact clip metadata and double bits");
        check(x.curves.size()==y.curves.size(),"Exact curve count");check(b.clip(x.id)==&y,"Decoded clip index retains exact identity");
        for(std::size_t c=0;c<x.curves.size();++c){const auto&u=x.curves[c];const auto&v=y.curves[c];
            check(u.group()==v.group()&&u.path()==v.path()&&u.attribute()==v.attribute()&&u.classID()==v.classID()&&u.nodeIDs()==v.nodeIDs(),"Exact curve metadata and binding order");
            check(u.channels().size()==v.channels().size(),"Exact channel width");
            for(std::size_t s=0;s<u.channels().size();++s){const auto ka=u.channels()[s].keys(),kb=v.channels()[s].keys();check(ka.size()==kb.size(),"Exact channel key count");
                for(std::size_t k=0;k<ka.size();++k){const auto&p=ka[k];const auto&q=kb[k];
                    check(same(p.time,q.time)&&same(p.value,q.value)&&same(p.inSlope,q.inSlope)&&same(p.outSlope,q.outSlope)&&same(p.inWeight,q.inWeight)&&same(p.outWeight,q.outWeight)&&p.weightedMode==q.weightedMode,"Every decoded source key field is bit-identical");
                    for(const auto t:{p.time,k+1<ka.size()?p.time+(ka[k+1].time-p.time)*.371:p.time+.25}){
                        const auto aa=u.channels()[s].sample(t),bb=v.channels()[s].sample(t);check(aa.has_value()==bb.has_value()&&(!aa||same(*aa,*bb)),"Original scalar sampling is bit-identical at keys and between keys");}
                }
            }
            for(double t:{-1.,0.,x.lastKeyTime*.137,x.lastKeyTime*.5,x.lastKeyTime*.923,x.lastKeyTime,x.lastKeyTime+1}){
                const auto aa=u.sample(t),bb=v.sample(t);check(aa.has_value()==bb.has_value(),"Exact sampled vector presence");
                if(aa){check(aa->kind==bb->kind,"Exact sampled value kind");for(unsigned k=0;k<aa->count();++k)check(same(aa->components[k],bb->components[k]),"Original vector/quaternion sampling is bit-identical");}
            }
        }
    }
}
Library fixture(){
    const auto inf=std::numeric_limits<double>::infinity();
    std::vector<Clip> clips;
    for(const auto group:{"m_FloatCurves","m_PositionCurves","m_ScaleCurves","m_RotationCurves"}){
        const unsigned n=std::string_view(group)=="m_FloatCurves"?1:std::string_view(group)=="m_RotationCurves"?4:3;
        std::vector<ScalarCurve> channels;for(unsigned i=0;i<n;++i)channels.emplace_back(std::vector<ScalarKey>{{-0.,i?-0.:1.,-inf,inf,3,.2,.7},{.25,2.,0.,1.,1,.3,.6},{1.,3.,2.,0.,2,.4,.5}});
        Clip clip;clip.id=group;clip.binding="source";clip.name="源 — \"curve\"";clip.sampleRate=60.;clip.lastKeyTime=1.;clip.wrapMode=2;
        clip.curves.push_back(Curve::fromChannels(group,"/actual/path","m_LocalPosition.x",{"node:001","node:002"},std::move(channels),224));clips.push_back(std::move(clip));
    }
    return Library(std::move(clips));
}
void put32(std::vector<std::uint8_t>&b,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<std::uint8_t>(value>>(i*8));}
void put64(std::vector<std::uint8_t>&b,std::size_t at,std::uint64_t value){for(unsigned i=0;i<8;++i)b.at(at+i)=static_cast<std::uint8_t>(value>>(i*8));}
void rehash(std::vector<std::uint8_t>&b){put64(b,16,b.size()-88);const auto h=packet::sha256(std::span<const std::uint8_t>(b).subspan(88));for(std::size_t i=0;i<32;++i){auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};b[56+i]=static_cast<std::uint8_t>((nibble(h[i*2])<<4)|nibble(h[i*2+1]));}}
template<class F>void mutation(const std::vector<std::uint8_t>&valid,F edit,std::string_view reason,bool checksum=true){auto b=valid;edit(b);if(checksum)rehash(b);rejects([&]{(void)decodeAnimationLibrary(b,pin);},reason);}
std::uint32_t read32(const std::vector<std::uint8_t>&b,std::size_t at){std::uint32_t n{};for(unsigned i=0;i<4;++i)n|=std::uint32_t(b.at(at+i))<<(i*8);return n;}
void guards(){
    const auto source=fixture();const auto data=encodeAnimationLibrary(source,pin);const auto decoded=decodeAnimationLibrary(data,pin);compare(source,decoded);
    check(encodeAnimationLibrary(decoded,pin)==data,"Compiled source encoding is deterministic after round trip");
    check(decodeAnimationLibrary(encodeAnimationLibrary(Library({}),pin),pin).clips().empty(),"Empty original library remains supported");
    rejects([&]{(void)encodeAnimationLibrary(source,"bad");},"SHA-256");rejects([&]{(void)decodeAnimationLibrary(data,std::string(64,'b'));},"source pin");
    for(std::size_t count=0;count<data.size();++count)rejects([&]{(void)decodeAnimationLibrary(std::span(data).first(count),pin);},count<88?"byte length":"payload length");
    mutation(data,[](auto&b){b[0]^=1;},"magic",false);mutation(data,[](auto&b){put32(b,8,2);},"schema",false);mutation(data,[](auto&b){put32(b,12,0x04030201);},"endian",false);
    mutation(data,[](auto&b){b.back()^=1;},"integrity",false);mutation(data,[](auto&b){b.push_back(0);},"Trailing");
    mutation(data,[](auto&b){put32(b,88,4097);},"count exceeds");mutation(data,[](auto&b){put32(b,92,UINT32_MAX);},"count exceeds");
    mutation(data,[](auto&b){b[96]=0xff;},"UTF-8");
    std::size_t at=92;for(unsigned i=0;i<3;++i)at+=4+read32(data,at);const auto timing=at;at+=16+4+4;for(unsigned i=0;i<3;++i)at+=4+read32(data,at);
    const auto nodes=read32(data,at);at+=4;for(unsigned i=0;i<nodes;++i)at+=4+read32(data,at);const auto optional=at;at+=4;if(read32(data,optional))at+=4;const auto infinity=at;at+=8;const auto channels=at;at+=4;const auto keys=at;at+=4;const auto firstKey=at;
    mutation(data,[&](auto&b){put64(b,timing,std::bit_cast<std::uint64_t>(0.));},"clip timing");
    mutation(data,[&](auto&b){put32(b,optional,2);},"optional class");mutation(data,[&](auto&b){put32(b,infinity,1);},"infinity mode");
    mutation(data,[&](auto&b){put32(b,channels,0);},"Empty source channels");mutation(data,[&](auto&b){put32(b,keys,1000001);},"count exceeds");
    mutation(data,[&](auto&b){put64(b,firstKey+8,std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity()));},"source curve");
    mutation(data,[&](auto&b){put64(b,firstKey+16,std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()));},"source curve");
    mutation(data,[&](auto&b){put64(b,firstKey+32,std::bit_cast<std::uint64_t>(1.1));},"source curve");mutation(data,[&](auto&b){put32(b,firstKey+48,4);},"source curve");
    mutation(data,[&](auto&b){put64(b,firstKey+52,std::bit_cast<std::uint64_t>(-0.));},"source curve");
    const auto single=encodeAnimationLibrary(Library({source.clips().front()}),pin);
    mutation(single,[&](auto&b){b.insert(b.end(),single.begin()+92,single.end());put32(b,88,2);},"Duplicate");
    std::vector<ScalarCurve> mismatched;for(unsigned i=0;i<3;++i)mismatched.emplace_back(std::vector<ScalarKey>{{i==1?0.:-0.,1.,0.,0.,0,.2,.7}});
    rejects([&]{(void)Curve::fromChannels("m_PositionCurves","p","",{},std::move(mismatched));},"key structure");
}
void actual(const std::filesystem::path&root){
    packet::Package packet(std::filesystem::absolute(root));const auto input=packet.loadAnimation();const auto library=Library::fromJson(input["library"]);
    const auto bytes=encodeAnimationLibrary(library,packet.manifestSHA256());const auto compiled=decodeAnimationLibrary(bytes,packet.manifestSHA256());compare(library,compiled);
    check(library.clips().size()==142,"Actual original-source clip inventory retained");std::cout<<"Actual compiled animation bytes: "<<bytes.size()<<'\n';
}
}
int main(int argc,char**argv){try{check(argc<=2,"Optional explicit original source packet only");guards();if(argc==2)actual(argv[1]);std::cout<<"PASS "<<checks<<" compiled animation checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
