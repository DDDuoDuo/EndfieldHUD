#pragma once
// Bottom-left ID card on the HUD's source shell (HUDSourceWatchView.
// setDesktopProfile): the live profile captions in the host's native label
// LayerScene, the desktop.profile.avatar/background/hover textures in the
// SourceScene and the card's desktop frame settings (level fill, accent tint,
// glow, sprite, image sizes).
//
// Producer: the Personal Profile module area (windows/app/areas/profile_module)
// owns an IdCardFeed and publishes it in the application's area service
// directory under IdCardFeed::serviceKey. Consumer: the application host owns
// one IdCardPresenter over its label scene, SourceScene and the source
// session's baseline desktop settings, connects it to the published feed and
// calls apply() once per frame before sampling the source session. apply()
// does nothing (and allocates nothing) until the feed changes, so tilt,
// ambient motion and Work Mode ticks never repaint the card.
#include "modules/id_card_binding.hpp"
#include "native/id_card_captions.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#ifdef _WIN32
namespace endfield::native {
class SourceScene;
enum class IdCardTextureSlot : std::uint8_t {avatar,background,hover};
inline constexpr std::array<std::string_view,3> idCardTextureIDs{"desktop.profile.avatar","desktop.profile.background","desktop.profile.hover"};
// One generated card texture: tight straight RGBA8 sRGB in the source
// texturePixels layout (bottom-origin rows), with the producer's revision.
struct IdCardTextureView {
    unsigned width{},height{};
    std::span<const std::uint8_t> rgba;
    std::uint64_t revision{};
};
class IdCardPresenter;
// The published card content. Views stay valid until the producer's next
// publish()/withdraw() (owner thread only). A null card keeps the packet
// defaults (profile not loaded or unreadable). One presenter at a time is
// connected (IdCardPresenter::connect); either side may be destroyed first.
class IdCardFeed final {
public:
    static constexpr std::string_view serviceKey{"profile.idCard"};
    IdCardFeed()=default;
    ~IdCardFeed();
    IdCardFeed(const IdCardFeed&)=delete;
    IdCardFeed&operator=(const IdCardFeed&)=delete;
    // Producer. Absent textures keep (avatar: restore) the packet originals.
    void publish(const modules::IdCardBinding*card,const std::array<std::optional<IdCardTextureView>,3>&textures);
    void withdraw();
    // Consumer.
    std::uint64_t revision()const noexcept{return revision_;}
    const modules::IdCardBinding*card()const noexcept{return card_;}
    const IdCardTextureView*texture(IdCardTextureSlot slot)const noexcept{const auto&t=textures_[static_cast<std::size_t>(slot)];return t?&*t:nullptr;}
    bool connected()const noexcept{return presenter_!=nullptr;}
private:
    friend class IdCardPresenter;
    const modules::IdCardBinding*card_{};std::array<std::optional<IdCardTextureView>,3>textures_{};
    std::uint64_t revision_{};IdCardPresenter*presenter_{};
    void changed();
};
// Where the presenter binds the card textures; SourceScene in the host.
struct IdCardTextureTarget {
    std::function<bool(std::string_view id,unsigned width,unsigned height,std::span<const std::uint8_t>rgba,std::uint64_t revision)>replace;
    std::function<bool(std::string_view id)>restore;
};
IdCardTextureTarget idCardTextureTarget(SourceScene&);
struct IdCardPresenterStats {std::uint64_t applies{},unchanged{},captionRepaints{},textureReplacements{},textureRestores{},settingsChanges{};};
class IdCardPresenter final {
public:
    // labels: the host's native label LayerScene holding the exported
    // desktop.profile.<binding> caption leaves (idCardCaptionLeaves of the top
    // frame's nativeLayers, copied before the setup JSON is released).
    // baseline: the source session's exported desktop frame settings; the
    // card's highlight node is the one bound to desktop.profile.hover there.
    IdCardPresenter(LayerScene&labels,IdCardCaptionLeaves,LayerRasterOptions,IdCardTextureTarget,core::source::SourceDesktopFrameSettings baseline);
    IdCardPresenter(LayerScene&labels,IdCardCaptionLeaves leaves,LayerRasterOptions options,SourceScene&materials,core::source::SourceDesktopFrameSettings baseline)
        :IdCardPresenter(labels,std::move(leaves),std::move(options),idCardTextureTarget(materials),std::move(baseline)){}
    ~IdCardPresenter();
    IdCardPresenter(const IdCardPresenter&)=delete;
    IdCardPresenter&operator=(const IdCardPresenter&)=delete;
    // Connects the published feed (replacing any presenter it had);
    // `changed` (e.g. request one frame) runs synchronously after each
    // publish, possibly inside a frame, and must only schedule work. A feed
    // destroyed while connected leaves the packet card on the next apply();
    // disconnect() (also a takeover by another presenter) leaves it idle.
    void connect(IdCardFeed&,std::function<void()>changed);
    void disconnect()noexcept;
    bool pending()const noexcept;
    // Content event: repaints changed captions (the label scene's content
    // revision then republishes it), replaces/restores the changed card
    // textures (uploaded by the SourceScene's next flush) and returns the
    // desktop settings for SourceWatchSession::setDesktopSettings when they
    // changed. Nothing happens while the feed is unchanged.
    std::optional<core::source::SourceDesktopFrameSettings>apply();
    const std::optional<std::string>&highlightNode()const noexcept{return highlight_;}
    const core::source::SourceDesktopFrameSettings&settings()const noexcept{return applied_;}
    const IdCardPresenterStats&stats()const noexcept{return stats_;}
private:
    NativeIdCardCaptions captions_;IdCardTextureTarget target_;
    core::source::SourceDesktopFrameSettings baseline_,applied_;
    std::optional<std::string>highlight_;
    friend class IdCardFeed;
    IdCardFeed*feed_{};std::function<void()>changed_;std::uint64_t seen_{};bool dirty_{},replaced_[3]{};std::uint64_t textureRevision_[3]{};
    std::uint64_t cardRevision_{};
    IdCardPresenterStats stats_;
};
}
#endif
