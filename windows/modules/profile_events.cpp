#include "modules/profile_events.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
std::optional<std::string_view>ProfileCropRecorder::receive(double backgroundZoom,double thumbnailZoom)noexcept{
    const auto normalized=[](double v){return std::isfinite(v)?std::min(20.,std::max(1.,v)):1.;};
    const std::pair next{normalized(backgroundZoom),normalized(thumbnailZoom)};
    const auto previous=crop_;crop_=next;
    if(!previous)return std::nullopt;
    const bool background=previous->first!=next.first,thumbnail=previous->second!=next.second;
    if(!background&&!thumbnail)return std::nullopt;
    return background&&thumbnail?std::string_view("both"):background?std::string_view("background"):std::string_view("thumbnail");
}
}
