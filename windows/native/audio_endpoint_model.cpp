#include "native/audio_endpoint_model.hpp"
#include "native/system_services.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace endfield::native {
namespace {
wchar_t fold(wchar_t c)noexcept{return c>=L'A'&&c<=L'Z'?static_cast<wchar_t>(c-L'A'+L'a'):c;}
bool caseless(std::wstring_view a,std::wstring_view b)noexcept{
    if(a.size()!=b.size())return false;
    for(std::size_t n=0;n<a.size();++n)if(fold(a[n])!=fold(b[n]))return false;
    return true;
}
bool digit(wchar_t c)noexcept{return c>=L'0'&&c<=L'9';}
bool space(wchar_t c)noexcept{return c==L' '||c==L'\t'||c==L'\r'||c==L'\n'||c==0x00a0||c==0x3000;}
std::wstring_view trimmed(std::wstring_view v)noexcept{
    while(!v.empty()&&space(v.front()))v.remove_prefix(1);
    while(!v.empty()&&space(v.back()))v.remove_suffix(1);
    return v;
}
bool finiteUnit(float v)noexcept{return std::isfinite(v)&&v>=0&&v<=1;}
bool validText(std::wstring_view v,bool allowEmpty)noexcept{
    return (allowEmpty||!v.empty())&&v.size()<=32767&&v.find(L'\0')==v.npos&&audioUtf8(v).has_value();
}
int order(const AudioNameOrder&custom,std::wstring_view a,std::wstring_view b){return custom?custom(a,b):audioNameCompare(a,b);}
}

bool audioEndpointHeadphones(std::optional<std::uint32_t>formFactor)noexcept{
    return formFactor&&(*formFactor==static_cast<std::uint32_t>(AudioEndpointFormFactor::headphones)||
        *formFactor==static_cast<std::uint32_t>(AudioEndpointFormFactor::headset));
}
bool audioEndpointBluetooth(std::wstring_view enumerator)noexcept{
    for(const auto bus:{std::wstring_view(L"BTHENUM"),std::wstring_view(L"BTHHFENUM"),std::wstring_view(L"BTHLEDEVICE"),std::wstring_view(L"BTHLE")})
        if(caseless(enumerator,bus))return true;
    return false;
}

AudioEndpointControls audioEndpointControls(const AudioEndpointVolumeState&s,bool refused)noexcept{
    AudioEndpointControls c;
    if(s.status<0){c.error=s.status;return c;}
    if(!finiteUnit(s.scalar)){c.error=static_cast<std::int32_t>(0x8000FFFFu);return c;} // E_UNEXPECTED
    c.available=true;c.volume=s.scalar;
    const bool range=s.rangeStatus>=0&&std::isfinite(s.minimumDecibels)&&std::isfinite(s.maximumDecibels)&&s.maximumDecibels>s.minimumDecibels;
    const bool steps=s.stepStatus>=0&&s.steps>=2;
    c.canSetVolume=range&&steps&&!refused;
    if(s.muteStatus>=0){c.muted=s.muted;c.canSetMute=true;}
    if(s.channelStatus>=0&&s.channels==2&&finiteUnit(s.left)&&finiteUnit(s.right)){
        c.balance=audio_stereo_balance(s.left,s.right);c.canSetBalance=c.balance.has_value();
    }
    return c;
}
bool audioEndpointWriteRefused(std::int32_t status)noexcept{
    // E_NOTIMPL and HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) only. A transient
    // device/invalidated error is reported but never marks a fixed output.
    return status==static_cast<std::int32_t>(0x80004001u)||status==static_cast<std::int32_t>(0x80070032u);
}

int audioNameCompare(std::wstring_view a,std::wstring_view b)noexcept{
    std::size_t i{},j{};
    while(i<a.size()&&j<b.size()){
        if(digit(a[i])&&digit(b[j])){
            std::size_t ei=i,ej=j;while(ei<a.size()&&digit(a[ei]))++ei;while(ej<b.size()&&digit(b[ej]))++ej;
            std::size_t si=i,sj=j;while(si+1<ei&&a[si]==L'0')++si;while(sj+1<ej&&b[sj]==L'0')++sj;
            const auto li=ei-si,lj=ej-sj;if(li!=lj)return li<lj?-1:1;
            for(std::size_t k=0;k<li;++k)if(a[si+k]!=b[sj+k])return a[si+k]<b[sj+k]?-1:1;
            i=ei;j=ej;continue;
        }
        const auto x=fold(a[i]),y=fold(b[j]);if(x!=y)return x<y?-1:1;++i;++j;
    }
    if(i<a.size())return 1;if(j<b.size())return -1;return 0;
}
void sortAudioEndpoints(std::vector<AudioEndpoint>&devices,const AudioNameOrder&custom){
    std::stable_sort(devices.begin(),devices.end(),[&](const auto&x,const auto&y){return order(custom,x.name,y.name)<0;});
}
void sortAudioApplications(std::vector<AudioApplicationRoute>&apps,const AudioNameOrder&custom){
    std::stable_sort(apps.begin(),apps.end(),[&](const auto&x,const auto&y){const auto n=order(custom,x.name,y.name);return n!=0?n<0:x.id<y.id;});
}

std::optional<std::vector<AudioTopologyDevice>>audioTopology(std::span<const AudioEndpoint>outputs,std::span<const AudioEndpoint>inputs){
    std::vector<AudioTopologyDevice>out;out.reserve(outputs.size()+inputs.size());std::set<std::wstring_view>seen;
    for(const auto list:{outputs,inputs})for(const auto&d:list){
        const auto name=trimmed(d.name);
        if(trimmed(d.id).empty()||name.empty()||!seen.insert(d.id).second)return std::nullopt;
        out.push_back({d.id,std::wstring(name)});
    }
    std::sort(out.begin(),out.end(),[](const auto&x,const auto&y){return x.id<y.id;});
    return out;
}
std::vector<AudioTopologyEvent>AudioTopologyRecorder::receive(std::span<const AudioTopologyDevice>current){
    std::vector<AudioTopologyDevice>next(current.begin(),current.end());
    std::sort(next.begin(),next.end(),[](const auto&x,const auto&y){return x.id<y.id;});
    std::vector<AudioTopologyEvent>events;
    if(previous_){
        const auto find=[](const std::vector<AudioTopologyDevice>&list,std::wstring_view id){
            const auto it=std::lower_bound(list.begin(),list.end(),id,[](const auto&d,std::wstring_view v){return d.id<v;});
            return it!=list.end()&&it->id==id;
        };
        // Labels only; an unconvertible label is omitted rather than invented.
        for(const auto&d:*previous_)if(!find(next,d.id))if(auto label=audioUtf8(d.name))events.push_back({false,std::move(*label)});
        for(const auto&d:next)if(!find(*previous_,d.id))if(auto label=audioUtf8(d.name))events.push_back({true,std::move(*label)});
    }
    previous_=std::move(next);return events;
}

std::optional<std::string>audioUtf8(std::wstring_view value)noexcept{
    try{
        std::string out;out.reserve(value.size());
        for(std::size_t i=0;i<value.size();++i){
            auto c=static_cast<std::uint32_t>(value[i]);
            if constexpr(sizeof(wchar_t)==2){
                if(c>=0xd800&&c<=0xdbff){if(i+1>=value.size())return std::nullopt;const auto low=static_cast<std::uint32_t>(value[++i]);if(low<0xdc00||low>0xdfff)return std::nullopt;c=0x10000+((c-0xd800)<<10)+(low-0xdc00);}
            }
            if(!c||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return std::nullopt;
            if(c<0x80)out.push_back(static_cast<char>(c));
            else if(c<0x800){out.push_back(static_cast<char>(0xc0|(c>>6)));out.push_back(static_cast<char>(0x80|(c&63)));}
            else if(c<0x10000){out.push_back(static_cast<char>(0xe0|(c>>12)));out.push_back(static_cast<char>(0x80|((c>>6)&63)));out.push_back(static_cast<char>(0x80|(c&63)));}
            else{out.push_back(static_cast<char>(0xf0|(c>>18)));out.push_back(static_cast<char>(0x80|((c>>12)&63)));out.push_back(static_cast<char>(0x80|((c>>6)&63)));out.push_back(static_cast<char>(0x80|(c&63)));}
        }
        return out;
    }catch(...){return std::nullopt;}
}
std::optional<std::wstring>audioWide(std::string_view value)noexcept{
    try{
        std::wstring out;out.reserve(value.size());
        for(std::size_t i=0;i<value.size();){
            const auto b=static_cast<unsigned char>(value[i]);std::uint32_t c{};std::size_t n{};
            if(b<0x80){c=b;n=1;}else if((b&0xe0)==0xc0){c=b&0x1f;n=2;}else if((b&0xf0)==0xe0){c=b&0x0f;n=3;}else if((b&0xf8)==0xf0){c=b&0x07;n=4;}else return std::nullopt;
            if(i+n>value.size())return std::nullopt;
            for(std::size_t k=1;k<n;++k){const auto t=static_cast<unsigned char>(value[i+k]);if((t&0xc0)!=0x80)return std::nullopt;c=(c<<6)|(t&0x3f);}
            if((n==2&&c<0x80)||(n==3&&c<0x800)||(n==4&&c<0x10000)||!c||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return std::nullopt;
            if constexpr(sizeof(wchar_t)==2){if(c>=0x10000){c-=0x10000;out.push_back(static_cast<wchar_t>(0xd800+(c>>10)));out.push_back(static_cast<wchar_t>(0xdc00+(c&0x3ff)));}else out.push_back(static_cast<wchar_t>(c));}
            else out.push_back(static_cast<wchar_t>(c));
            i+=n;
        }
        return out;
    }catch(...){return std::nullopt;}
}

bool validAudioEndpointSnapshot(const AudioEndpointSnapshot&s)noexcept{
    try{
        if(s.outputs.size()>maximumAudioEndpoints||s.inputs.size()>maximumAudioEndpoints||s.topology.size()>2*maximumAudioEndpoints||s.applications.size()>AudioSessionRoutes::maximumApplications)return false;
        std::size_t bytes{};
        for(const auto*list:{&s.outputs,&s.inputs}){std::set<std::wstring_view>ids;for(const auto&d:*list){if(!validText(d.id,false)||!validText(d.name,false)||!ids.insert(d.id).second)return false;bytes+=d.id.size()+d.name.size();}}
        for(const auto&d:s.topology){if(!validText(d.id,false)||!validText(d.name,false))return false;bytes+=d.id.size()+d.name.size();}
        if(!validText(s.defaultOutputID,true)||!validText(s.defaultInputID,true))return false;
        if(s.volume&&!finiteUnit(*s.volume))return false;
        if(s.balance&&(!std::isfinite(*s.balance)||*s.balance< -1||*s.balance>1))return false;
        for(const auto&a:s.applications){if(a.id.empty()||a.id.size()>4096||a.name.size()>4096||(a.gain&&!finiteUnit(*a.gain)))return false;bytes+=a.id.size()+a.name.size();}
        return bytes<=16*1024*1024;
    }catch(...){return false;}
}
} // namespace endfield::native
