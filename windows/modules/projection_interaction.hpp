#pragma once
#include "modules/projection_model.hpp"

namespace endfield::modules {
// Host routes retained toolbar/secondary controls first. This controller owns
// only the remaining canvas gesture; media decoding and HUD handoff are borrowed.
struct ProjectionInputResult {
    bool handled{}, changed{}, closeMenu{}, returnToHUD{}, drawingLimit{};
    bool brushChanged{}, strokeCompleted{}, erased{}, mediaRemoved{};
    std::optional<std::uint64_t> togglePlayback;
    struct Seek { std::uint64_t id{}; double seconds{}; };
    std::optional<Seek> seek;
};
class ProjectionInteraction final {
public:
    ProjectionInteraction(ProjectionModel&,core::Point size);
    void resize(core::Point size);
    ProjectionInputResult setActive(bool);
    void setMenuOpen(bool value)noexcept{menuOpen_=value;}
    bool active()const noexcept{return active_;}
    bool ownsPointer()const noexcept{return held_;}
    const std::optional<DrawingStroke>&liveStroke()const noexcept{return pending_;}
    ProjectionInputResult down(core::Point);
    ProjectionInputResult drag(core::Point);
    ProjectionInputResult up();
    ProjectionInputResult rightDown();
    ProjectionInputResult wheel(double delta,bool precise,bool ended);
    ProjectionInputResult escape();
    // Capture loss never commits an incomplete stroke or keeps media dragging.
    void cancelGesture()noexcept;
private:
    enum class Kind{move,resize,seek};
    struct Gesture{Kind kind;std::uint64_t id;core::Point start;core::Rect frame;};
    core::Point normalized(core::Point)const;
    ProjectionInputResult seek(std::uint64_t,core::Point)const;
    ProjectionModel&model_;core::Point size_;
    bool active_{},menuOpen_{},held_{},eraseChanged_{},pendingBrush_{};
    std::optional<DrawingStroke>pending_;std::optional<Gesture>gesture_;
    std::optional<core::Point>lastEraser_;
};
}
