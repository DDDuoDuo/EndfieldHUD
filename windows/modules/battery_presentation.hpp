#pragma once
#include "core/data/json.hpp"
#include "core/localization.hpp"
#include "core/scene.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace endfield::modules {
// Optional provider values stay optional. A percentage pair is an explicit
// source fallback, never a fabricated mAh capacity or battery-health estimate.
struct BatteryReading {
    std::optional<unsigned> percentage;
    bool present{},pluggedIn{},charging{},fullyCharged{};
    struct Capacity {unsigned current{},maximum{};std::string unit;bool operator==(const Capacity&)const=default;};
    std::optional<Capacity>capacity;
    std::optional<std::string>health;
    bool operator==(const BatteryReading&)const=default;
};
struct BatteryAppearance {
    bool dark{true};double scale{2};core::Language language{core::Language::english};
    std::array<double,4>accent{250./255,212./255,31./255,1};
    bool operator==(const BatteryAppearance&)const=default;
};
struct BatteryLabels {std::string heading,percentage,state,source,capacity,health,settings;bool operator==(const BatteryLabels&)const=default;};
struct BatterySurface {std::string id;core::Matrix4 local;bool tint{},rim{};};
struct BatteryArtwork {ehud::data::Json layers;std::vector<BatterySurface>surfaces;};
class BatteryPresentation final {
public:
    explicit BatteryPresentation(BatteryReading={},BatteryAppearance={});
    bool receive(BatteryReading);bool setAppearance(BatteryAppearance);
    const BatteryReading&reading()const noexcept{return reading_;}
    const BatteryAppearance&appearance()const noexcept{return appearance_;}
    const BatteryLabels&labels()const noexcept{return labels_;}
    std::array<double,4>levelColor()const noexcept;
    bool chargeMode()const noexcept{return reading_.present&&reading_.pluggedIn;}
    std::uint64_t revision()const noexcept{return revision_;}
    static constexpr core::Rect bounds(){return {0,0,400,334};}
    static constexpr core::Rect settingsRect(){return {111,314,178,20};}
    static bool hitsSettings(core::Point)noexcept;
private:
    BatteryReading reading_;BatteryAppearance appearance_;BatteryLabels labels_;std::uint64_t revision_{1};
    void refresh();
};
// Only content events generate descriptors. The owner applies the already
// shared module transition and hover feedback to these retained local surfaces.
BatteryArtwork prepareBatteryArtwork(const BatteryPresentation&);
}
