#pragma once
// The facility tag stack under the HUD title: the port of
// Sources/HUDFacilityTags.swift (HUDFacilityTagStack, FacilityTag). SHELL-72.
// A right-aligned column, 272 wide with 6 px gaps, of tags chamfered at the
// bottom-left with a moving hazard strip, the warning glyph, the text with
// its bold orange value and a x button. Recording mode is not shown here
// (the clock's status row shows it). Updates diff against what is shown: new
// tags slide in, removed ones strike through and collapse, the rest move.
// Coordinates are content px; the host places the root layer (anchored at
// its top-right) on the CORE plane under the header, so the stack tilts with
// the centre (native/facility_owner.hpp; the Mac host does the same,
// SHELL-20, although the Swift file's comment says flat).
//
// Each tag owns its own LayerArena and ControlSet (tags come and go for the
// whole session; a removed tag frees its layers). Layer tree per tag:
//   slot (placement: the column position and the .25 s move)
//    └ tag (masksToBounds; hover translateX -3, ftIn, the collapse: opacity,
//      the height as a top wipe 28 -> 0, margin-top -6 as translateY)
//       └ content (collapse: translateY -14, so it stays centred)
//          plate, ring, hazard (+ band, clipped by the hazard's chamfered
//          alpha: the Mac's hazardClip mask), glyph, text, x, cross, brighten
// Substitutions (recorded): the stack layer's .25 s height animation has no
// visible effect on the Mac (the layer draws and clips nothing) and is set
// without motion; the collapse's bounds-height animation is the equivalent
// top-anchored clip (Channel::wipe), the position.y move a translateY.
#include "modules/facility_banner.hpp"

namespace endfield::modules::facility {
class FacilityTagStack final {
public:
    static constexpr double width = 272, tagHeight = 28, gap = 6;
    explicit FacilityTagStack(d::Environment&);
    ~FacilityTagStack();
    FacilityTagStack(const FacilityTagStack&) = delete;
    FacilityTagStack& operator=(const FacilityTagStack&) = delete;
    // Anchored at its top-left corner: the column grows downwards.
    d::Layer& layer() const noexcept { return *layer_; }
    // A tag was clicked: open Storage / Activity Monitor / Power.
    std::function<void(FacilityAlertKind)> onOpen;
    // x was pressed. Called at the start of the dismiss sequence; the stack
    // finishes the strike-through and collapse after the alert leaves the list.
    std::function<void(FacilityAlertKind)> onDismiss;
    // Loops (hazard strips) run only while active; the host turns this off
    // while the HUD is hidden.
    bool isActive() const noexcept { return active_; }
    void setActive(bool);
    // The settled height of the column.
    double preferredHeight() const noexcept { return preferredHeight_; }
    // Tags and their x buttons (later entries on top), for the host's router
    // and accessibility. It changes with update and when x is pressed.
    std::vector<d::Control*> controls() const;
    // Shows `alerts` in raise order, one tag per kind.
    void update(const std::vector<FacilityAlert>&, bool animated);
    // No tag on screen (leaving ones included): nothing to draw.
    bool empty() const noexcept { return tags_.empty(); }
    // A content key over the stack's and every tag's arena (the owner re-emits on change).
    std::uint64_t contentRevision() const noexcept;
    // Verification.
    std::vector<FacilityAlertKind> kinds() const;       // live tags
    std::vector<FacilityAlertKind> shownKinds() const;  // every tag on screen, leaving ones included
    struct TagState { std::string text; d::Rect frame; bool looping{}, struck{}; };
    std::optional<TagState> tag(FacilityAlertKind) const;
    const d::Layer* tagLayer(FacilityAlertKind) const;  // the first tag of `kind` not removed
private:
    class Tag;
    d::Environment* env_;
    d::LayerArena arena_;
    d::Layer* layer_;
    std::vector<std::unique_ptr<Tag>> tags_;
    std::uint64_t serial_{};
    d::Deadlines::Token token_{};
    double preferredHeight_{};
    bool active_{true};
    std::unique_ptr<Tag> makeTag(const FacilityAlert&);
    Tag* find(std::uint64_t serial) const noexcept;
    void layout(bool animated);
    void dismiss(Tag&);
    void collapse(Tag&, bool animated);
    void remove(Tag&);
    void dropRemoved();
};
} // namespace endfield::modules::facility
