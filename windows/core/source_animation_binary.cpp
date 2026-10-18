#include "core/source_animation_binary.hpp"
#include "core/shell_packet.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace endfield::core::source {
namespace {
static_assert(sizeof(double)==8&&std::numeric_limits<double>::is_iec559);
constexpr std::array<std::uint8_t,8> magic{'E','H','A','N','I','M','0','1'};
constexpr std::size_t headerBytes=88,maximumClips=4096,maximumCurves=65536,maximumKeys=1000000,maximumNodes=16384;
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
std::array<std::uint8_t,32> digest(std::string_view value){
    need(value.size()==64,"Invalid animation source SHA-256");std::array<std::uint8_t,32> result{};
    auto nibble=[](char c)->unsigned{if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw std::invalid_argument("Invalid animation source SHA-256");};
    for(std::size_t i=0;i<result.size();++i)result[i]=static_cast<std::uint8_t>((nibble(value[i*2])<<4)|nibble(value[i*2+1]));return result;
}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void reserve(std::size_t count){need(count<=maximumAnimationBinaryBytes-bytes.size(),"Compiled animation exceeds byte bound");}
    void raw(std::span<const std::uint8_t> value){reserve(value.size());bytes.insert(bytes.end(),value.begin(),value.end());}
    void u32(std::uint32_t value){reserve(4);for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(i*8)));}
    void u64(std::uint64_t value){reserve(8);for(unsigned i=0;i<8;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(i*8)));}
    void number(double value){u64(std::bit_cast<std::uint64_t>(value));}
    void count(std::size_t value,std::size_t maximum){need(value<=maximum,"Compiled animation count exceeds bound");u32(static_cast<std::uint32_t>(value));}
    void string(std::string_view value){need(Json::validUtf8(value),"Invalid animation UTF-8");count(value.size(),maximumAnimationBinaryBytes);raw({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});}
};
struct Reader {
    std::span<const std::uint8_t> bytes;std::size_t offset{};
    std::span<const std::uint8_t> take(std::size_t count){need(count<=bytes.size()-offset,"Truncated compiled animation");const auto value=bytes.subspan(offset,count);offset+=count;return value;}
    std::uint32_t u32(){const auto b=take(4);std::uint32_t value{};for(unsigned i=0;i<4;++i)value|=std::uint32_t(b[i])<<(i*8);return value;}
    std::uint64_t u64(){const auto b=take(8);std::uint64_t value{};for(unsigned i=0;i<8;++i)value|=std::uint64_t(b[i])<<(i*8);return value;}
    double number(){return std::bit_cast<double>(u64());}
    std::size_t count(std::size_t maximum,std::size_t minimumBytes=1){const auto value=u32();need(value<=maximum&&value<=(bytes.size()-offset)/minimumBytes,"Compiled animation count exceeds bound");return value;}
    std::string string(){const auto value=take(count(maximumAnimationBinaryBytes));std::string result(reinterpret_cast<const char*>(value.data()),value.size());need(Json::validUtf8(result),"Invalid animation UTF-8");return result;}
};
}
std::vector<std::uint8_t> encodeAnimationLibrary(const Library& library,std::string_view sourcePin){
    const auto pin=digest(sourcePin);Writer payload;payload.count(library.clips().size(),maximumClips);
    for(const auto& clip:library.clips()){
        payload.string(clip.binding);payload.string(clip.id);payload.string(clip.name);payload.number(clip.sampleRate);payload.number(clip.lastKeyTime);payload.u32(std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(clip.wrapMode)));
        payload.count(clip.curves.size(),maximumCurves);
        for(const auto& curve:clip.curves){
            payload.string(curve.group());payload.string(curve.path());payload.string(curve.attribute());payload.count(curve.nodeIDs().size(),maximumNodes);
            for(const auto& node:curve.nodeIDs())payload.string(node);
            payload.u32(curve.classID()?1u:0u);if(curve.classID())payload.u32(std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(*curve.classID())));
            payload.u32(2);payload.u32(2);payload.count(curve.channels().size(),4);payload.count(curve.channels().front().keys().size(),maximumKeys);
            for(const auto& channel:curve.channels())for(const auto& key:channel.keys()){
                payload.number(key.time);payload.number(key.value);payload.number(key.inSlope);payload.number(key.outSlope);payload.number(key.inWeight);payload.number(key.outWeight);payload.u32(key.weightedMode);
            }
        }
    }
    Writer result;result.raw(magic);result.u32(1);result.u32(0x01020304);result.u64(payload.bytes.size());result.raw(pin);result.raw(digest(packet::sha256(payload.bytes)));result.raw(payload.bytes);
    // Build-time inputs can also be assembled through public typed constructors.
    // Run the bounded decoder once so invalid typed data never becomes a cache.
    (void)decodeAnimationLibrary(result.bytes,sourcePin);return std::move(result.bytes);
}
Library decodeAnimationLibrary(std::span<const std::uint8_t> bytes,std::string_view expectedPin){
    const auto pin=digest(expectedPin);need(bytes.size()>=headerBytes&&bytes.size()<=maximumAnimationBinaryBytes,"Invalid compiled animation byte length");Reader input{bytes};
    const auto actualMagic=input.take(magic.size());need(std::equal(actualMagic.begin(),actualMagic.end(),magic.begin()),"Invalid compiled animation magic");
    need(input.u32()==1,"Unsupported compiled animation schema");need(input.u32()==0x01020304,"Unsupported compiled animation endian marker");need(input.u64()==bytes.size()-headerBytes,"Compiled animation payload length mismatch");
    const auto actualPin=input.take(pin.size());need(std::equal(actualPin.begin(),actualPin.end(),pin.begin()),"Compiled animation source pin mismatch");
    const auto actualHash=input.take(32),payload=bytes.subspan(headerBytes);const auto payloadHash=digest(packet::sha256(payload));need(std::equal(actualHash.begin(),actualHash.end(),payloadHash.begin()),"Compiled animation integrity mismatch");
    const auto clipCount=input.count(maximumClips,36);std::vector<Clip> clips;clips.reserve(clipCount);std::size_t totalCurves{},totalKeys{};
    for(std::size_t i=0;i<clipCount;++i){
        Clip clip;clip.binding=input.string();clip.id=input.string();clip.name=input.string();need(!clip.id.empty()&&clip.id.size()<=4096,"Invalid source identity");
        clip.sampleRate=input.number();clip.lastKeyTime=input.number();clip.wrapMode=std::bit_cast<std::int32_t>(input.u32());
        need(std::isfinite(clip.sampleRate)&&clip.sampleRate>0&&std::isfinite(clip.lastKeyTime)&&clip.lastKeyTime>=0,"Invalid source clip timing");
        const auto curveCount=input.count(maximumCurves,36);need(curveCount<=maximumCurves-totalCurves,"Source curve count exceeds limit");totalCurves+=curveCount;clip.curves.reserve(curveCount);
        for(std::size_t j=0;j<curveCount;++j){
            auto group=input.string(),path=input.string(),attribute=input.string();const auto nodeCount=input.count(maximumNodes,4);std::vector<std::string> nodes;nodes.reserve(nodeCount);for(std::size_t k=0;k<nodeCount;++k)nodes.push_back(input.string());
            const auto hasClass=input.u32();need(hasClass<=1,"Invalid compiled animation optional class");std::optional<int> classID;if(hasClass)classID=std::bit_cast<std::int32_t>(input.u32());
            const auto pre=input.u32(),post=input.u32();need(pre==2&&post==2,"Empty curve or unsupported infinity mode");const auto channelCount=input.count(4,52);need(channelCount>0,"Empty source channels");
            const auto keyCount=input.count(maximumKeys,52*channelCount);need(keyCount>0&&keyCount<=maximumKeys-totalKeys,"Source key count exceeds limit");totalKeys+=keyCount;
            std::vector<ScalarCurve> channels;channels.reserve(channelCount);
            for(std::size_t channel=0;channel<channelCount;++channel){
                std::vector<ScalarKey> keys;keys.reserve(keyCount);
                for(std::size_t k=0;k<keyCount;++k){ScalarKey key;key.time=input.number();key.value=input.number();key.inSlope=input.number();key.outSlope=input.number();key.inWeight=input.number();key.outWeight=input.number();key.weightedMode=input.u32();keys.push_back(key);}
                channels.emplace_back(std::move(keys));
            }
            clip.curves.push_back(Curve::fromChannels(std::move(group),std::move(path),std::move(attribute),std::move(nodes),std::move(channels),classID));
        }
        clips.push_back(std::move(clip));
    }
    need(input.offset==bytes.size(),"Trailing compiled animation data");return Library(std::move(clips));
}
} // namespace endfield::core::source
