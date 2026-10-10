#pragma once
#include "core/data/data_store.hpp"
#include "core/localization.hpp"
#include "core/scene.hpp"
#include "core/source_watch_frame.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace endfield::modules {
// The bottom-left source personal card (desktop-profile-card.json). Node IDs
// and authored text metrics are resolved once from the selected card.
struct IdCardCaptionSource {std::string binding,nodeID;double sourceFontSize{16},width{};bool operator==(const IdCardCaptionSource&)const=default;};
struct IdCardSource {
    // managerName, managerNumber, managerLevel, managerLevelLabel, progressTxt
    std::array<IdCardCaptionSource,5>captions;
    std::string levelSlider,headFrame,playerHead,background;
    std::vector<std::string>accentNodes;  // levelSlider, headFrameImg, IconRight, ArrowImage
    std::vector<std::string>glowNodes;    // additive Light over the card and portrait
    std::string defaultBackgroundSprite;  // business_card_topic_normal_1
    struct Texture {std::string id,file;unsigned width{},height{};core::Rect spriteRect;bool operator==(const Texture&)const=default;} backgroundTexture;
};
IdCardSource idCardSourceFromCardJson(const ehud::data::Json&card);
// windows/resources/profile/id-card-source.json: the reduced unchanged
// desktop-profile-card.json written by windows/tools/id_card_reference.py.
// Its digest is pinned here, so the staged app resource is the verified card.
constexpr std::string_view idCardSourceSHA256="7dc76be935dbece7604217967ba53dfc70974b03b0ca8328331c47d63b4d6ea7";
IdCardSource loadIdCardSource(const std::filesystem::path&resourceDirectory);
constexpr std::array<std::string_view,5>idCardCaptionBindings{"managerName","managerNumber","managerLevel","managerLevelLabel","progressTxt"};
struct IdCardCaption {
    std::string text;double fontSize{};bool rightAligned{};std::array<double,4>color{1,1,1,1};
    bool operator==(const IdCardCaption&)const=default;
};
// Width of text in the medium-weight system font at a point size; the native
// owner supplies its DirectWrite measurement (LayerRasterizer::measureSourceText).
using IdCardMeasure=std::function<double(std::string_view text,double fontSize)>;
// Exact HUDSourceWatchView.setDesktopProfile caption/property/image binding.
// The card shows the synced display UID (override ?? game ?? local) and never
// the "#" tag. update() is a content event only: tilt, ambient motion and Work
// Mode ticks never call it, and an identical key performs no measurement.
class IdCardBinding final {
public:
    IdCardBinding(IdCardSource,IdCardMeasure);
    struct Input {
        const ehud::data::Profile*profile{};std::array<double,3>hudAccent{250./255,212./255,31./255};core::Language language{core::Language::english};
        std::optional<std::string>avatarImage,backgroundImage; // decoded image identities (managed filenames)
        int avatarOrientation{1};
    };
    bool update(const Input&);
    const IdCardSource&source()const noexcept{return source_;}
    std::span<const IdCardCaption>captions()const noexcept{return captions_;}
    int level()const noexcept{return level_;}
    std::array<double,3>accent()const noexcept{return accent_;}
    // Revisions: captions/properties, avatar texture, background and hover textures.
    std::uint64_t revision()const noexcept{return revision_;}
    std::uint64_t avatarRevision()const noexcept{return avatarRevision_;}
    std::uint64_t backgroundRevision()const noexcept{return backgroundRevision_;}
    std::uint64_t hoverRevision()const noexcept{return hoverRevision_;}
    // Source frameBuilder.desktopProperties/desktopSprites for this card:
    // glow alpha (hover highlight), level fill, accent tint, default sprite.
    void apply(core::source::SourceDesktopFrameSettings&,const std::optional<std::string>&highlightNode)const;
    // A caption leaf exported as desktop.profile.<binding> with this binding's
    // string, fitted medium font size, alignment and color. Geometry, font
    // family and clipping are retained; only content changes.
    ehud::data::Json captionLayer(const ehud::data::Json&exportedLeaf,std::size_t caption)const;
private:
    IdCardSource source_;IdCardMeasure measure_;
    struct Key {std::array<std::string,4>strings;std::array<double,11>values{};std::optional<std::string>avatar,background;bool operator==(const Key&)const=default;};
    std::optional<Key>key_;std::optional<std::array<double,8>>avatarKey_,backgroundKey_;std::optional<std::string>avatarImage_,backgroundImage_;
    std::array<IdCardCaption,5>captions_;int level_{60};std::array<double,3>accent_{};
    std::uint64_t revision_{},avatarRevision_{},backgroundRevision_{},hoverRevision_{};
};
}
