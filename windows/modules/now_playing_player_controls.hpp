#pragma once
// A Win32 player's own on-screen controls read through its accessibility tree
// (Windows counterpart of the Mac NowPlayingAccessibility read: the elapsed
// "1:23 /" and total "4:56" labels, the play/pause, previous and next
// controls and the volume slider of NetEase Cloud Music, QQ Music and Kugou).
// Pure and portable: the platform tree is reached through
// NowPlayingAccessibleTree (MSAA in native/now_playing_player_controls.*,
// synthetic trees in tests).
#include "modules/now_playing_model.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::modules {
// Mac NowPlayingAccessibility.clock: whitespace and newlines trimmed, split at
// ":" (empty fields skipped), two or three fields of one to three ASCII
// digits, seconds (and minutes of h:mm:ss) below 60. "4:56" -> 296.
std::optional<double>nowPlayingClock(std::string_view);
// Mac NowPlayingAccessibility.isVolumeLabel: "volume" (any case) or 音量.
bool nowPlayingVolumeLabel(std::string_view);

enum class NowPlayingControlRole:std::uint8_t {other,heading,text,slider,button,image,document};
// One accessible element as the walk saw it.
struct NowPlayingControlNode {
    NowPlayingControlRole role{};
    // Mac label(): the name (title), else the description, else the value,
    // at most 1024 Characters.
    std::string label;
    // Children reported by the element (before the walk's own bounds).
    std::size_t children{};
    bool operator==(const NowPlayingControlNode&)const=default;
};
// The platform tree. Element ids are the tree's own (0 is the root); every
// call may fail (an element that went away): empty children, nullopt node.
class NowPlayingAccessibleTree {
public:
    virtual ~NowPlayingAccessibleTree()=default;
    virtual std::optional<NowPlayingControlNode>node(std::uint64_t id)=0;
    virtual std::vector<std::uint64_t>children(std::uint64_t id)=0;
};
struct NowPlayingWalked {std::uint64_t id{};NowPlayingControlNode node;};
// Mac NowPlayingAccessibility.walk: depth first from the root; each node's
// children (at most its last 160) are pushed in order and taken from the end,
// so the bottom player bar is visited before a large library or sidebar;
// depth below 12, at most `limit` nodes; `stop` ends it early (deadline,
// cancellation).
std::vector<NowPlayingWalked>nowPlayingWalk(NowPlayingAccessibleTree&,std::size_t limit=700,const std::function<bool()>&stop={});

// What a walk found (Mac read(): the first match of each kind in walk order).
struct NowPlayingPlayerControls {
    // Element ids of the found controls (to re-read or press them later).
    std::optional<std::uint64_t>heading,position,duration,volume,playPause,previous,next;
    std::optional<std::string>headingText;  // a heading containing " - "
    std::optional<double>positionSeconds,durationSeconds;
    // The play/pause control's label: Pause/暂停/暫停 means playing, Play/播放 paused.
    std::optional<bool>playing;
    // An empty web document: the player exposes its page only once an
    // assistive client asks for it (Chromium/CEF players).
    bool emptyDocument{};
    bool found()const noexcept{return positionSeconds.has_value()||durationSeconds.has_value()||playPause.has_value()||previous.has_value()||next.has_value();}
};
// Classifies walked nodes:
//   heading   role heading whose label contains " - " (Mac AXHeading)
//   position  text whose trimmed label ends with "/" ("1:23 /"; Mac), or the
//             first field of a "1:23 / 4:56" text (Windows web players)
//   duration  the first other clock text (Mac), or the second field above
//   volume    slider labelled volume/音量 (Mac)
//   playPause, previous, next: buttons named like the Mac menu items
//             (Play/Pause/播放/暂停/暫停, Previous/上一首/上一个, Next/下一首/下一个)
// A position later than the duration is dropped (not this player's clock).
NowPlayingPlayerControls nowPlayingPlayerControls(std::span<const NowPlayingWalked>);
// Mac perform() names of the control for a command (seek has none).
bool nowPlayingControlNamed(NowPlayingCommandKind,std::string_view label);
// The playing state a play/pause control's label states (nullopt: neither).
std::optional<bool>nowPlayingControlPlaying(std::string_view label);
}
