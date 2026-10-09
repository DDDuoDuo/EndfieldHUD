#pragma once
#include "modules/storage_state.hpp"
#include "core/data/json.hpp"
#include "core/localization.hpp"
#include "core/scene.hpp"
#include <array>
#include <span>
namespace endfield::modules {
struct StorageAppearance {bool dark{true};double scale{2};core::Language language{core::Language::english};std::array<double,4>accent{250./255,212./255,31./255,1};// Exact NSColor blend converted to sRGB. Required for non-default accents;
    // Generic RGB blending is not equivalent to blending encoded sRGB values.
    std::optional<std::array<double,4>> availableColor;
    bool operator==(const StorageAppearance&)const=default;};
struct StorageLabels {std::string heading,caption,meter,settings,refresh,status;std::array<std::string,3>titles,values;bool operator==(const StorageLabels&)const=default;};
struct StorageAction {std::string id,label;core::Rect rect;};
// Local root positions are flattened only across translation-only containers.
// Rotation applies about the refresh arrow local center (11,11); opacity/feedback
// are numeric placements, never baked into local raster content.
struct StorageSurface {std::string id;core::Matrix4 local;std::string action;bool rim{},rotates{};float opacity{1};};
struct StorageArtwork {ehud::data::Json layers;std::vector<StorageSurface>surfaces;};
class StoragePresentation final {
public:
    explicit StoragePresentation(StorageSnapshot={},StorageAppearance={});
    bool receive(StorageSnapshot);bool setAppearance(StorageAppearance);
    const StorageSnapshot&snapshot()const noexcept{return snapshot_;}const StorageAppearance&appearance()const noexcept{return appearance_;}
    const StorageLabels&labels()const noexcept{return labels_;}
    std::span<const StorageAction>actions()const noexcept{return {actions_.data(),snapshot_.isLoading?1u:2u};}
    std::optional<std::string_view>actionAt(core::Point)const noexcept;
    std::uint64_t revision()const noexcept{return revision_;}
    static constexpr core::Rect bounds(){return {0,0,400,334};}
    static constexpr core::Rect settingsRect(){return {12,252,166,29};}
    static constexpr core::Rect refreshRect(){return {352,252,36,29};}
private:StorageSnapshot snapshot_;StorageAppearance appearance_;StorageLabels labels_;std::array<StorageAction,2>actions_;std::uint64_t revision_{1};void update();
};
std::string storageBytes(std::optional<double>);
std::array<core::Rect,32>storageMeter(double fraction)noexcept; // zero-width cells are omitted from source path
// The caller supplies the EXACT original filled refresh-arrow CGPath command
// array from storage_presentation_reference; do not replace it with a font icon
// or approximate stroked arc. Path metadata is validated by LayerRasterizer.
struct StorageSourcePaths {ehud::data::Json refreshArrow,settingsFeedback,refreshFeedback;};
StorageArtwork prepareStorageArtwork(const StoragePresentation&,const StorageSourcePaths&);
struct StorageRefreshSample {double angle{};bool active{};std::uint64_t generation{};};
// Source finite0.72s linear turns. Sampling/completion is on the one host clock;
// fast reads finish the current turn, manual requests coalesce one extra turn.
class StorageRefreshMotion final {
public:
    static constexpr double duration=.72;
    void update(bool active,bool loading,bool reduceMotion,double now);
    void manualTurn(double now);StorageRefreshSample sample(double now);
    bool complete(std::uint64_t generation,bool finished,double now);
    bool running()const noexcept{return running_;}
private:bool active_{},loading_{},reduce_{},running_{},queued_{};double began_{};std::uint64_t generation_{};
    void begin(double);void cancel();
};
}
