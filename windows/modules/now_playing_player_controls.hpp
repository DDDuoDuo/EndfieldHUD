#pragma once
// A Win32 player's own on-screen controls read through its accessibility tree
// (Windows counterpart of the Mac NowPlayingAccessibility read: the elapsed
// "1:23 /" and total "4:56" labels, the play/pause, previous and next
// controls and the volume slider of NetEase Cloud Music, QQ Music and Kugou).
// Pure and portable: the platform tree is reached through
// NowPlayingAccessibleTree (MSAA/IAccessible2 in native/now_playing_player_
// controls.*, synthetic trees in tests).
//
// NetEase Cloud Music 3.1 (CEF 91, checked live on the test laptop) has no
// elapsed/total labels: its player bar is a <footer> with the transport
// buttons (names "pre"/"play"|"pause"/"next", the icons described
// "上一首（Ctrl + Left）", "播放（Ctrl + P）", "下一首（Ctrl + Right）"), a
// progress bar labelled "播放进度调节" whose part with class "track" is the
// played width, and the cover <img> whose source embeds the catalog picture
// URL (orpheus://cache/?http://p3.music.126.net/...). The rules below read
// those as well, confined to the bar that holds the transport controls, so
// the page's playlists (their "播放" buttons and "03:33" lengths) never count.
// windows/tests/fixtures/now-playing-netease-tree.json is that tree's shape.
#include "modules/now_playing_model.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
    // The element's description when the label is its name (a web icon's
    // tooltip such as "下一首（Ctrl + Right）"), else empty.
    std::string description;
    bool operator==(const NowPlayingControlNode&)const=default;
};
struct NowPlayingControlBounds {
    double left{},top{},width{},height{};
    bool operator==(const NowPlayingControlBounds&)const=default;
};
// Facts a few elements are asked for (the parts of a progress bar, images):
// screen bounds and, on a web page (IAccessible2 object attributes), the
// element's class tokens and image source.
struct NowPlayingControlDetails {
    std::optional<NowPlayingControlBounds>bounds;
    std::vector<std::string>classes;
    std::string source;
    bool operator==(const NowPlayingControlDetails&)const=default;
};
// The platform tree. Element ids are the tree's own (0 is the root); every
// call may fail (an element that went away): empty children, nullopt node,
// empty details.
class NowPlayingAccessibleTree {
public:
    virtual ~NowPlayingAccessibleTree()=default;
    virtual std::optional<NowPlayingControlNode>node(std::uint64_t id)=0;
    virtual std::vector<std::uint64_t>children(std::uint64_t id)=0;
    virtual NowPlayingControlDetails details(std::uint64_t id){(void)id;return {};}
};
struct NowPlayingWalked {
    std::uint64_t id{};NowPlayingControlNode node;
    std::optional<std::uint64_t>parent; // the walked parent (none for the root)
    unsigned depth{};
};
// Mac NowPlayingAccessibility.walk: depth first from the root; each node's
// children (at most its last 160) are pushed in order and taken from the end,
// so the bottom player bar is visited before a large library or sidebar;
// depth below 12, at most `limit` nodes; `stop` ends it early (deadline,
// cancellation, everything found); `visit` sees each node as it is walked.
std::vector<NowPlayingWalked>nowPlayingWalk(NowPlayingAccessibleTree&,std::size_t limit=700,const std::function<bool()>&stop={},
    const std::function<void(const NowPlayingWalked&)>&visit={});

// What a walk found (Mac read(): the first match of each kind in walk order).
struct NowPlayingPlayerControls {
    // Element ids of the found controls (to re-read or press them later).
    // playPause/previous/next are the elements to press (a button when the
    // match was its icon); playLabel is the element whose label states the
    // play state when it is not playPause itself.
    std::optional<std::uint64_t>heading,position,duration,volume,playPause,previous,next,playLabel;
    // The player bar's progress bar, its played part and its cover image.
    std::optional<std::uint64_t>progress,played,artwork;
    std::optional<std::string>headingText;  // a heading containing " - "
    std::optional<double>positionSeconds,durationSeconds;
    // The played part of the progress bar (0...1) at the time of the walk.
    std::optional<double>fraction;
    // The cover image's allow-listed catalog URL (nowPlayingPlayerArtworkURL).
    std::optional<std::string>artworkURL;
    // The play/pause control's label: Pause/暂停/暫停 means playing, Play/播放 paused.
    std::optional<bool>playing;
    // Texts of the player bar (at most 32), to confirm which track it shows.
    std::vector<std::uint64_t>texts;std::vector<std::string>textLabels;
    // An empty web document: the player exposes its page only once an
    // assistive client asks for it (Chromium/CEF players).
    bool emptyDocument{};
    bool found()const noexcept{return positionSeconds.has_value()||durationSeconds.has_value()||fraction.has_value()||playPause.has_value()||previous.has_value()||next.has_value();}
};
// Classifies walked nodes as they arrive:
//   heading   role heading whose label contains " - " (Mac AXHeading)
//   position  text whose trimmed label ends with "/" ("1:23 /"; Mac), or the
//             first field of a "1:23 / 4:56" text (Windows web players)
//   duration  the first other clock text (Mac), or the second field above
//   volume    slider labelled volume/音量 (Mac)
//   playPause, previous, next: elements named (or, for a web icon,
//             described) like the Mac menu items (nowPlayingControlKind); an
//             icon inside a button stands for that button
//   progress  a slider or group labelled as a progress bar
//             (nowPlayingProgressLabel); its child with class "track" is the
//             played part (NetEase 3.1), the fraction its width over the bar's
//   artwork   an image whose source is an allow-listed cover
//   texts     the texts of the player bar
// Transport controls count only as a player bar: previous, play/pause and
// next all found, each at most three levels below their common ancestor. The
// bar is that ancestor's grandparent: clocks, progress, artwork and texts
// count only inside it (a page's playlists never do). Without a bar, clocks
// follow the Mac rule (first match anywhere) except in a web page (a walk
// rooted at a document), whose lists show track lengths. A position later than the
// duration is dropped (not this player's clock). Details are asked from the
// tree only for images and the progress bar's parts.
class NowPlayingControlScan {
public:
    explicit NowPlayingControlScan(NowPlayingAccessibleTree* =nullptr);
    void add(const NowPlayingWalked&);
    // Transport controls with a clock or a played fraction, and the bar's
    // cover and texts already seen: a walk may stop.
    bool complete()const;
    NowPlayingPlayerControls result()const;
private:
    struct Entry {std::optional<std::uint64_t>parent;unsigned depth{};NowPlayingControlRole role{};};
    NowPlayingAccessibleTree* tree_{};
    std::map<std::uint64_t,Entry>entries_;
    std::vector<NowPlayingWalked>walked_;
    std::optional<std::uint64_t>playPause_,previous_,next_,playLabel_,progress_,played_;
    std::optional<NowPlayingControlBounds>progressBounds_,playedBounds_;
    std::optional<bool>playing_;
    std::vector<std::pair<std::uint64_t,std::string>>images_; // allow-listed covers, walk order (at most 8)
    std::vector<std::uint64_t>clocks_; // clock texts, walk order (at most 64)
    unsigned imageDetails_{};
    bool emptyDocument_{},webPage_{};
    std::optional<std::uint64_t>buttonOf(std::uint64_t id)const;
    std::optional<std::uint64_t>container()const;
    bool inside(std::uint64_t id,std::uint64_t ancestor)const;
};
NowPlayingPlayerControls nowPlayingPlayerControls(std::span<const NowPlayingWalked>);

// A control label without a trailing keyboard shortcut in parentheses
// ("下一首（Ctrl + Right）" -> "下一首"), trimmed.
std::string nowPlayingControlText(std::string_view);
// The command a control is named for: its label or description (without a
// shortcut) equals a Mac menu item name (Play/Pause/播放/暂停/暫停,
// Previous/上一首/上一个, Next/下一首/下一个), ASCII letters in any case.
std::optional<NowPlayingCommandKind>nowPlayingControlKind(const NowPlayingControlNode&);
bool nowPlayingControlNamed(NowPlayingCommandKind,std::string_view label);
// The playing state a play/pause control's label states (nullopt: neither).
std::optional<bool>nowPlayingControlPlaying(std::string_view label);
// A progress bar's label: it names playback progress (进度/進度/"progress").
bool nowPlayingProgressLabel(std::string_view);
// IAccessible2 object attributes ("name:value;" with \-escapes) as pairs.
std::vector<std::pair<std::string,std::string>>nowPlayingAccessibleAttributes(std::string_view);
// The catalog cover an image source shows: the source itself or a URL it
// embeds (NetEase's orpheus://cache/?http://p3.music.126.net/...jpg?...),
// accepted by nowPlayingCatalogCoverURL (p1-p4.music.126.net, https,
// param=600y600). nullopt for any other source.
std::optional<std::string>nowPlayingPlayerArtworkURL(std::string_view source);
// Whether a player bar shows the track of its window title: one of the bar's
// texts (trimmed) is the title of the window's "Title - Artist" caption
// (nowPlayingHeadingTrack, the last separator; NetEase's grammar).
bool nowPlayingTitleShown(std::string_view windowTitle,std::span<const std::string>texts);
}
