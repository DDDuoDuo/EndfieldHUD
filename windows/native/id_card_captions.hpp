#pragma once
#include "modules/id_card_binding.hpp"
#include "native/layer_raster.hpp"
#include <array>
#include <cstdint>
#include <string>
#ifdef _WIN32
namespace endfield::native {
class LayerScene;
// The five exported desktop.profile.<binding> caption leaves (one CATextLayer
// per container, see export_shell_packet.swift). Copied once at setup from the
// top frame's nativeLayers, before the host releases its reference JSON.
// Order follows modules::idCardCaptionBindings.
struct IdCardCaptionLeaves {
    std::array<ehud::data::Json,5>leaves;
    std::array<std::string,5>surfaceIDs;
};
IdCardCaptionLeaves idCardCaptionLeaves(const ehud::data::Json&nativeLayers);
// NSString.size(withAttributes: NSFont.systemFont(ofSize:weight:.medium)).width
// through the rasterizer's exact source caption resolver (DirectWrite). Content
// events only; creates no cache entry or retained layout.
modules::IdCardMeasure nativeIdCardMeasure(LayerRasterizer&,LayerRasterOptions={});
struct IdCardCaptionStats {std::uint64_t updates{},unchanged{},surfaceUpdates{};};
// Sole runtime writer of the bottom-left card captions in the host's native
// label LayerScene (NativeWatchAppearance deliberately skips these surfaces).
// update() follows HUDSourceWatchView.setDesktopProfile: it repaints only the
// captions whose fitted text, size, alignment or colour changed, keyed on the
// binding revision, so tilt, ambient motion and Work Mode ticks do no work.
// Placement, masks and the profile viewport clip stay with NativeLabelPlan.
// After a true result the caller uploads the label scene as for any content
// event. Borrowed scene and rasterizer outlive this writer; owner thread only.
class NativeIdCardCaptions final {
public:
    NativeIdCardCaptions(LayerScene&labels,IdCardCaptionLeaves,LayerRasterOptions);
    bool update(const modules::IdCardBinding&);
    const IdCardCaptionLeaves&leaves()const noexcept{return leaves_;}
    IdCardCaptionStats stats()const noexcept{return stats_;}
private:
    LayerScene*labels_;IdCardCaptionLeaves leaves_;LayerRasterOptions options_;
    std::array<modules::IdCardCaption,5>painted_{};std::array<bool,5>paintedValid_{};
    std::uint64_t seenRevision_{},nextRevision_{};bool seen_{};IdCardCaptionStats stats_;
};
}
#endif
