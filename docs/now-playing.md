# Now Playing

The module connects automatically to the system's current music publisher. It
keeps the existing spatial HUD and does not require Music/Spotify source buttons.
Playback commands, album art, app-volume capability and timed lyrics are separate:
a missing cover or unavailable native app-volume control does not disable playback.

## Presentation

A 330-point square cover sits directly on the transparent 440-point center plane, without
a heading, refresh button or panel border. A shallow gradient only beneath the
cover's title/artist keeps those labels legible. Previous, Play/Pause and Next
sit at bottom-left; Volume and Lyrics sit at bottom-right, leaving a 130-point
middle gap for the battery control. The three lyric rows retain faded previous
and next lines. Their space remains reserved when lyrics are hidden or unavailable,
so the seek row, transport buttons and volume popup stay in place.

Transport buttons retain their simple rounded faces and hover feedback. The
vertical volume menu follows Personal Card's face, offset backing plate,
projected input and highlight frames. Song metadata/artwork changes use bounded
crossfades; changed lyric rows slide within their own clipped viewport, including
backward seeks. Ordinary progress samples do not replay either transition.
Reduce Motion and concealment remove these finite animations. The empty state
is `No music playing` in every language; the navigation glyph is a circular Play
button.

## Sources and compatibility

The primary backend is a notification stream from the source-built, pinned
[ungive/mediaremote-adapter](https://github.com/ungive/mediaremote-adapter), licensed
BSD-3-Clause. See `ThirdParty/MediaRemoteAdapter/NOTICE.md` and `LICENSE` for the
exact revision and local compatibility changes. Its small Objective-C framework
runs inside macOS's `/usr/bin/perl` interpreter. This supports system Now Playing
metadata on newer macOS versions where direct calls return no information. It
uses **private MediaRemote APIs** and consequently has no Apple compatibility
guarantee. Missing/broken adapter resources degrade to the in-process bridge and
player-specific fallbacks, without a restart or polling loop.

Music, Spotify, NetEase Music, QQ Music and Kugou have known bundle identities.
Other running publishers can be identified from their actual system bundle ID.
Support requires that a player publish metadata and accept the relevant command;
it is not a claim that every version of every player has been tested. Music and
Spotify retain their public scripting-dictionary fallback. The other known music
apps have a narrowly scoped public Accessibility fallback for their existing
player footer and Controls menu, using an already granted Accessibility permission.
It does not inspect libraries, decrypt player caches, watch keystrokes or launch
players. App-specific Automation consent is requested only by the explicit
connection action when that fallback needs it.

Apple's public `MPNowPlayingInfoCenter` describes publishing your own app's media,
not a general system-wide metadata reader:
[Apple documentation](https://developer.apple.com/documentation/mediaplayer/mpnowplayinginfocenter).
The public fallback mechanisms are
[Scripting Bridge](https://developer.apple.com/documentation/scriptingbridge) and
[Accessibility](https://developer.apple.com/documentation/applicationservices/axuielement).
The architecture was compared with
[MusicIsland](https://github.com/James-Kua/MusicIsland) and
[CloudLyrics](https://github.com/hellomyonly55/CloudLyrics-for-macOS); it does not
adopt their library/cache scanning or repeated metadata polling.

## Playback and volume

Play/pause, previous/next and seeking target the current running publisher.
Commands use fixed arguments, no shell interpolation, a three-second deadline,
and cancellation. The stream process stops when the module leaves the screen;
its pipe is detached immediately and its own child receives bounded termination.
Unexpected EOF discards its cached state before fallback. Transport-generated
content-item UUID changes are not treated as track changes.

The controller keeps the last valid track through temporary read failures. One
serial worker coalesces background reads and retains the latest user command and
volume target. User commands take priority. Seeking updates one optimistic model
shared by the rail, lyrics and accessibility, preserving the target through older
reads until a fresh same-track position acknowledges the target. A single 2.5-second rollback deadline handles ignored commands without polling or resending them. Failure, another track, or hiding
ends the optimistic state. A queued seek cannot affect a different track.

Native player volume is available through Music/Spotify's real `sound volume`
property, or a player exposing an explicitly named, settable Accessibility volume
slider. It can work while paused because it is the player's own volume. The
MediaRemote adapter has no reliable per-application-volume API. NetEase 2.3.22's
inspected footer exposes Increase/Decrease Volume menu commands but no readable
native volume value or named slider; those commands cannot honestly provide an
absolute percentage slider. Where native volume is absent the UI uses the
existing audio-route capability. A paused app without an existing audio route
may therefore have no adjustable slider. No fabricated volume state is reported.

## Timed lyrics and covers

NetEase now shares one current-recording catalog search between its cover and
lyrics. The public `music.163.com/api/cloudsearch/pc` response is accepted only
when normalized title (or an explicit catalog alias), artist and duration match.
Album equality breaks ties; ambiguous equally strong recording IDs are rejected.
The matched song supplies its genuine album cover and its own timed LRC via
`/api/song/lyric`. This unauthenticated web API is also used by
[MusicIsland's source](https://github.com/James-Kua/MusicIsland/blob/main/Sources/MusicIsland/NetEase/NetEaseMusicClient.swift).
It is not an official stable integration contract. Eight in-memory entries share
positive and missing results; no cookies, login, player cache or library access
is involved. The cover request uses only exact `p1`–`p4.music.126.net` HTTPS hosts
and asks the CDN for a 600-pixel source image before the bounded 512-pixel decode. Original-language timed cues preserve
song timing without multiplying the three display rows.

Player-provided timed lyrics remain available. Other players, or a NetEase
recording with no catalog LRC, use [LRCLIB's documented API](https://lrclib.net/docs).
An exact lookup is followed by at most one bounded search using title and lead
artist; the result must still match recording title, artists and duration, but
an album translation no longer prevents a match. Different-artist recordings,
remixes, incorrect durations and ambiguous timed results are rejected. This
sends current-song metadata and network address to NetEase and/or LRCLIB for
these lookups; no audio, credentials, account, library or history is sent.
Requests are HTTPS, ephemeral, without cookies/credentials/disk cache or redirects,
and limited to 1 MiB and 8/10-second request/resource deadlines. Eight in-memory
results, including misses, prevent repeat requests. Hiding cancels outstanding
lookups. Plain untimed lyrics are not assigned invented timestamps.

The parser bounds LRC input to 512 KiB and 4096 cues, handles offsets, repeated
stamps, translations and blank instrumental cues, sorts once and uses binary
search for exactly three rows: previous/current/next. Artwork-only changes do not
request lyrics again. Progress interpolates locally between player notifications.
Late or corrected player-provided timed lyrics replace the same song's cached
result immediately, canceling any older lookup before it can overwrite them.

One visible-only, cancellable display deadline targets the earlier of the next
fractional lyric cue, whole playback second or track end. It replaces the former
one-second repeating timer and never queries the player for ordinary lyric or
progress changes. Dense cue clusters coalesce at a maximum of 30 local updates
per second; normal songs wake only at their actual cues and progress seconds.
Hiding lyrics removes their cue deadlines. Pausing, closing, changing tracks and
seeking cancel or recompute the same deadline. Finite clipped row movement uses
a shallow opacity change instead of dimming each new line; Reduce Motion retains
the exact timing without the transition.

Covers come from the publisher's compressed artwork, Music/Spotify's genuine
artwork fields, or a conservatively matched NetEase catalog recording. Missing artwork remains a neutral record, not an unrelated image.
Two in-memory entries cap reuse. Encoded images are limited to 8 MiB, dimensions
to 8192 per side/32 million pixels, and ImageIO thumbnails to 512 × 512. Optional
Spotify/NetEase downloads accept only their exact known HTTPS CDN hosts with the same no-cookie,
no-redirect and bounded-response policy. Covers are not persisted.

## Resource lifetime and verification

One streaming helper runs only while Now Playing is presented, including its
incoming reveal. It receives native
notifications, with 120 ms debounce and JSON diffs; it never loops over `get`.
JSON lines are capped at 12 MiB and decoded artwork at 8 MiB. Metadata strings,
AX tree discovery and observer counts are bounded. AX discoveries cache footer
handles so clock notifications read a few fields instead of traversing the app.
The shared visible display deadline interpolates stored time; it does not query players.
Reopening restores one private warm snapshot plus cached thumbnails/lyrics
synchronously, then verifies current metadata through a fresh stream. Presentation
starts before the incoming canvas is constructed or the HUD begins its reveal;
button input remains gated until the animation completes. The opening-to-stable
handoff preserves that same subscription and pending artwork/lyric requests.
Explicit close, cancellation and window detachment stop it, including the shared
audio listener. An inactive Volume bridge cannot stop the music consumer's
listener. No continuous hidden subscription or polling was added. Hidden
public state is still cleared, observers and the helper stop, and pending
network work is canceled. While the first stream event is pending the controller
does not block it behind a full Accessibility fallback scan. Native volume
capability checks no longer trigger an unrelated footer scan before publishing
metadata. Nothing adds a background login item or listening-history store. Event Log gets
only successful allowlisted control action/source categories, never song names.

Focused `NowPlayingAutomaticTests` and `NowPlayingLyricsTests` exercise automatic
selection, command priority, volume coalescing, optimistic seek, stream diffs/EOF,
stable identity, bounded input, parser timing, cancellation and lyric request reuse.
Existing artwork and UI tests use synthetic local media and injected backends.
`--ui-test --now-playing-live-probe` is an explicit read-only end-to-end bundle
diagnostic. It exercises the production controller, catalog, artwork decoder and
lyrics path, then closes/reopens the service. It reports cold/warm arrival times,
capability booleans and cue counts, without content or playback controls. Cached
warm arrival and the first fresh read are reported separately, so immediate
cached rendering is never presented as proof of current playback metadata.

A live macOS 15.7.4 / NetEase 2.3.22 check returned title, artist, album, duration,
position and playback state through the adapter where direct native MR returned
no metadata. An eight-second visible-stream observation emitted two notifications;
its process settled to 0.0% sampled CPU and approximately 12.3 MiB RSS after startup.
The current track supplied no system-published artwork. A later read-only catalog
probe found an exact title/artist/album and duration match, its genuine 16.7 KiB
300-pixel cover, and 365 timed cues. These measurements establish behavior on
this machine, not universal player or OS compatibility. A subsequent live check confirmed pause, a two-second seek, and restoration of the original playing state on the same track. Exact seek-back position was not confirmed within the one-second verification tolerance. An accepted command alone is not proof of audible playback or an exact position change.

## Earlier 2026-10-04 review build verification

The final arm64 review bundle's read-only production probe on NetEase 2.3.22 /
macOS 15.7.4 displayed current metadata in 130 ms, fetched the genuine 600-pixel
cover in 2,630 ms (decoded at the 512-pixel cap), and loaded 343 timed lyric cues
in 2,127 ms. Closing and reopening restored metadata, cover and lyrics together
in 14 ms on the same track. These are one-run current-track observations, not
network-independent latency promises or verification of QQ Music/Kugou/other OS
versions. No playback commands, accounts, libraries or real HUD stores changed.
The earlier 300-pixel/365-cue probe above was a different current track.

## Square player / contained clock review verification

`build/music-review/EndfieldHUD.app` is the optimized arm64 review bundle. Its
native HUD checks verify that music is prepared while opening/switching input
is disabled, cached cover is available before the return transition finishes,
and opening completion retains exactly one music presentation start. Both
repeated closing and audio-page handoff release the correct observer and clocks.
The rendered player was inspected with square art, no external panel fill/edge,
separate control groups and a clear battery area. Clock pixel tests verify
horizontal page clipping with stationary frame/indicators.

Results: 76,441 core assertions; 85 compatibility guard mutation checks;
74 Batch 3/4 native assertions, 149 clock/Batch 2 assertions and 655 navigation
assertions across all 17 sections (878 native). Focused checks also passed
210 music and 406 clock assertions. The native app signature and retained
resource bytes verify. The separate offline Metal probe cannot run without an
installed Metal compiler; actual native Metal rendering passed the HUD checks.

A read-only NetEase 2.3.22 / macOS 15.7.4 production run returned cold metadata
in 823 ms, 63 timed lyric cues in 2,738 ms, and artwork in 3,258 ms. Reopening
restored cached metadata, cover and lyrics in 17 ms; a new stream confirmed fresh
metadata in 62 ms. The latter is measured separately from cache hydration.
These are one-run observations, not network-independent latency guarantees.
Other players/OS versions and audio output were not retested. No playback,
user-library, real HUD data, or running-app changes were made.

## Larger cover and fixed controls (2026-10-04)

The music canvas is now 440×440, centered 20 design points lower toward the
battery bar. Its cover is a 330×330 square. Transport controls stay at the lower
left, with volume and lyrics at the lower right. A lyric visibility toggle changes
only the lyrics' finite opacity transition; the seek rail, elapsed/duration labels,
buttons and an already-open volume menu keep their exact positions.

No reader, cache, polling interval or audio ownership changed in this layout pass.
The native source-HUD check also verifies that every playback action remains clear
of the expanded battery bar. Synthetic editor/music tests use the existing isolated
stores and media fixtures; no real music player command is sent.
