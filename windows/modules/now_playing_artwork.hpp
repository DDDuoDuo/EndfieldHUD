#pragma once
#include "modules/now_playing_presentation.hpp"
#include "core/data/json.hpp"

namespace endfield::modules {
enum class NowPlayingSurfaceRole {artwork,placeholder,shade,title,artist,empty,lyric,
    progressFill,progressHandle,volumeFill,volumeHandle,tint,rim};
struct NowPlayingSurface {
    std::string id;ehud::data::Json content;core::Matrix4 local;float opacity{1};
    NowPlayingSurfaceRole role{};std::optional<NowPlayingAction>action;unsigned row{};
};
struct NowPlayingArtwork {
    // Paint order: borrowed cover, panel, clipped lyrics group, controls, volume
    // group. All positions are in the original 440-point canvas. The cover is
    // aspect-fill inside the square cover rect; no bitmap is fabricated here.
    std::vector<NowPlayingSurface>panel,lyrics,controls,volume;
};
// Content-event descriptions only. Numeric progress roles describe retained
// colored quads; never stretch an antialiased bitmap edge or rasterize a fill.
// Row movement clips to NowPlayingGeometry::lyrics. Volume fades as one group.
NowPlayingArtwork prepareNowPlayingArtwork(const NowPlayingPresentation&);
}
