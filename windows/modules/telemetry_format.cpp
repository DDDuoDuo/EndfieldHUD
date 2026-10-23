#include "modules/telemetry_format.hpp"
#include "modules/hud_design_format.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace endfield::modules::telemetry {
namespace {
std::string fixed(double v,int digits){char chars[64];const auto r=std::to_chars(chars,chars+sizeof chars,v,std::chars_format::fixed,digits);if(r.ec!=std::errc{})throw std::invalid_argument("Telemetry value formatting failed");return {chars,r.ptr};}
}
// TelemetryFormat.bytes (:11-17).
std::string bytes(std::optional<double>value){
    if(!value||!std::isfinite(*value)||*value<0)return std::string(missing);
    static constexpr std::array<const char*,6>units{"B","KB","MB","GB","TB","PB"};
    double v=*value;std::size_t index{};while(v>=1000&&index+1<units.size()){v/=1000;++index;}
    return fixed(v,v>=100||index==0?0:1)+" "+units[index];
}
std::string rate(std::optional<double>value){auto text=bytes(value);return text==missing?text:text+"/s";}
// TelemetryFormat.percent (:25-28).
std::string percent(std::optional<double>value,std::optional<double>limit){
    if(!value||!std::isfinite(*value)||*value<0)return std::string(missing);
    return fixed(limit?std::min(*limit,*value):*value,1)+"%";
}
// TelemetryFormat.storage (:32-36).
StorageText storage(std::int64_t value){
    const auto text=design::format::storage(double(std::max<std::int64_t>(0,value))/1'000'000'000);
    const auto space=text.find(' ');
    if(space==std::string::npos)return {text,{}};
    return {text.substr(0,space),text.substr(space+1)};
}
// TelemetryFormat.memory (:39-44).
std::string memory(std::optional<std::uint64_t>value){
    if(!value)return std::string(missing);
    const double v=double(*value);
    return v>=1'000'000'000?fixed(v/1'000'000'000,1)+" GB":fixed(v/1'000'000,0)+" MB";
}
// TelemetryFormat.duration (:47-50).
std::string duration(std::optional<int>minutes){
    if(!minutes||*minutes<0)return std::string(missing);
    return std::to_string(*minutes/60)+":"+design::format::pad2(*minutes%60);
}
// TelemetryCaption.text (:58-62).
std::string caption(std::string_view english,std::string_view chinese,core::Language resolved){
    return resolved==core::Language::english?std::string{}:core::localized(english,chinese,resolved);
}
}
