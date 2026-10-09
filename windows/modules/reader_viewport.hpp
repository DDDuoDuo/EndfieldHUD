#pragma once
#include "modules/reader_state.hpp"
#include <array>

namespace endfield::modules {
enum class ReaderGesturePhase {none,began,changed,ended,cancelled};
struct ReaderNavigationIntent {int step{};std::optional<double>progress;};
struct ReaderPagePlacement {core::Rect rect;bool hidden{};};
struct ReaderPageAnchor {std::string bookID;ReaderLocation location;bool next{},previous{},illustration{};double progress{};};
// Direct port of ReaderCanvas's cached-page interaction, not a renderer. The
// caller supplies its existing monotonic owner clock and dispatches each taken
// navigation intent to ReaderState, then supplies the updated page anchor.
// Detail/artwork deadlines share the application scheduler; this owns no timer.
class ReaderViewport final {
public:
    static constexpr core::Rect viewport{12,48,376,334};
    void setActive(bool,double time);void setPreferences(const ReaderPreferences&);
    // Publish ReaderState.current() changes, including nil while opening. A
    // coalesced different-book notification gives the same clear/new-book reset.
    void setPage(std::optional<ReaderPageAnchor>,double time,bool reduceMotion=false);
    bool pointerDown(core::Point);void pointerDragged(core::Point,double time);void pointerUp(double time);
    void cancelInteraction()noexcept;
    bool scroll(core::Point,double dx,double dy,double time,
        ReaderGesturePhase phase=ReaderGesturePhase::none,bool momentum=false,bool precision=true,bool zoomModifier=false);
    bool magnify(core::Point,double amount,double time);void zoomBy(double factor,core::Point,double time);
    void resetZoom(double time);bool turnPage(int direction);
    std::optional<ReaderNavigationIntent>takeNavigation()noexcept;
    std::optional<ReaderImageView>takeDetailRequest(double time);
    std::optional<double>nextWakeTime()const noexcept;
    bool artworkReleaseDue(double time);
    core::Rect progressRect()const noexcept;double displayedProgress()const noexcept;
    std::array<ReaderPagePlacement,3>placements(bool currentHasMatchingDetail=false)const noexcept;
    const ReaderImageView&imageView()const noexcept{return view_;}
    double scrollOffset()const noexcept{return offset_;}bool dragging()const noexcept{return dragProgress_.has_value()||panStart_.has_value();}
    bool canZoom()const noexcept{return page_&&page_->illustration;}
    std::uint64_t pageTurnSequence()const noexcept{return pageTurnSequence_;}
    int lastAnimatedTurnDirection()const noexcept{return animatedTurn_;}
    double pageTurnDuration()const noexcept{return .26;}
    double nonPrecisionScrollDuration()const noexcept{return animatedScroll_?.10:0;}
private:
    struct Pan {core::Point point,pan;};
    bool active_{},vertical_{true},animatedScroll_{};
    std::optional<ReaderPageAnchor>page_;ReaderImageView view_;
    double offset_{},horizontalRemainder_{},lastWheelTurn_{};bool turnConsumed_{};
    std::optional<double>pendingOffset_,dragProgress_,detailAt_,releaseAt_;
    std::optional<ReaderImageView>pendingView_;std::optional<Pan>panStart_;
    std::optional<ReaderNavigationIntent>navigation_;int turnDirection_{},animatedTurn_{};
    std::uint64_t pageTurnSequence_{};
    void setView(ReaderImageView,double time,double offset=0);
    void scheduleDetail(double time);void scrollZoomed(double dx,double dy,double time);
    double fraction(core::Point)const noexcept;
};
// Exact compact secondary-menu placement and six-row wheel accumulator. These
// dimensions/anchors are from ReaderInteraction, including delete confirmation.
enum class ReaderMenuKind {library,bookmarks,settings};
core::Rect readerMenuRect(ReaderMenuKind,std::optional<core::Rect>anchor={});
class ReaderListScroll final {
public:
    // The original list also displays Shelf choices. It retains only six
    // visible rows and scalar scroll state; the 128-bookmark storage limit does
    // not limit this generic list or require allocating an entry per row.
    void setCount(std::size_t);void setDeleteConfirmation(bool value)noexcept{confirming_=value;}
    bool scroll(double delta);std::size_t offset()const noexcept{return offset_;}
    std::optional<core::Rect>scrollbar(double menuWidth)const noexcept;
private:std::size_t count_{},offset_{};double remainder_{};bool confirming_{};
};
} // namespace endfield::modules
