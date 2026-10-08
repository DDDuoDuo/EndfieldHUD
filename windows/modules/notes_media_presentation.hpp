#pragma once
#include "modules/notes_presentation.hpp"

namespace endfield::modules {
enum class NotesMediaKind {image,gif,video};
enum class NotesMediaState {hidden,loading,ready,playing,paused,failed};
struct NotesMediaStrings {
    std::string loading{"Loading media…"},play{"▶ Play"},pause{"Ⅱ Pause"};
    std::string playAction{"Play"},pauseAction{"Pause"},unavailable{"Image unavailable"};
};
struct NotesMediaStatus {
    NotesMediaState state{NotesMediaState::hidden};
    std::optional<std::string> localizedError;
};
struct NotesMediaCardContent {
    NotesMediaKind kind{NotesMediaKind::image};
    std::optional<double> duration;
    NotesMediaStatus status;
    NotesMediaStrings strings;
    bool legacyManagedImage{},legacyUnavailable{};
};
struct NotesMediaGeometry {
    core::Rect content,footer,playback,seek,rail;
    bool hasPlayback{},hasSeek{},legacy{};
};
struct NotesMediaProgressPose {
    core::Rect fill,handle; // local card coordinates, with the source anchor applied
    double fraction{};bool active{};
};
// NotesCanvas image/video local artwork and geometry. Common card/header/grip
// remain NotesCardPresentation's responsibility. Input is explicit decoded
// metadata; this class does not inspect files, hold pixels or own playback.
class NotesMediaLayout final {
public:
    NotesMediaLayout(double cardWidth,double cardHeight,NotesMediaKind,
        std::optional<double> duration={},bool legacyManagedImage=false);
    const NotesMediaGeometry& geometry()const noexcept{return geometry_;}
    NotesMediaKind kind()const noexcept{return kind_;}
    std::optional<double> duration()const noexcept{return duration_;}
    // CALayer.resizeAspect, preserving the actual decoded orientation/aspect.
    core::Rect fittedImage(unsigned pixelWidth,unsigned pixelHeight)const;
    double seekSeconds(double localCardX)const;
    NotesLayer footer(const NotesMediaStatus&,const NotesPalette&,const NotesMediaStrings& = {})const;
    std::optional<NotesAction> playbackAction(std::string_view noteID,const NotesMediaStatus&,const NotesMediaStrings& = {})const;
    // Source rail/fill/handle colors: replacing muted alpha with .3 (not
    // multiplying it); output has no timer and needs no image rasterization.
    std::array<NotesColor,3> progressColors(const NotesPalette&)const noexcept;
    static unsigned decodeDimension(int requested=512)noexcept;
    static double normalizedFrameDelay(std::optional<double>)noexcept;
private:
    NotesMediaGeometry geometry_;NotesMediaKind kind_;std::optional<double>duration_;
};
// Caller-clock equivalent of the source one-second linear CA progress pair.
// Update only on source progress/state/seek events, sample on the owner's one
// frame clock. A new event starts at its reported time, not the old animation's
// presentation value. Pointer/tilt samples allocate and decode nothing.
class NotesMediaProgress final {
public:
    explicit NotesMediaProgress(const NotesMediaLayout&);
    void update(double currentTime,std::optional<double> seekPreview,bool animated,
        bool playing,bool visible,bool reduceMotion,double now);
    NotesMediaProgressPose sample(double now)const;
    bool needsFrame(double now)const;
private:
    core::Rect seek_{};double duration_{},from_{},to_{},start_{};bool active_{};
};
} // namespace endfield::modules
