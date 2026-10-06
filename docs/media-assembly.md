# Media Assembly backend

The original requirement is authoritative:

> 5. 新增模块：Media Assembly（影像加工），使用终末地里装备加工的图标。在里面可以打开/拖入照片视频，也可以从temp file shelf选择。编辑照片和视频时可以裁剪、旋转、镜像、调整亮度、对比度、饱和度、色温、色调、高光、阴影、曝光度以及曲线和色阶。图片还支持使用终末地中的滤镜和贴纸。视频可以剪辑（调整视频长度）和添加终末地滤镜。导出更改后的媒体后可以选择覆盖原文件或者保存新的特定位置（默认原文件位置）。

`MediaAssemblyController` retains only the current source reference and session edit parameters. Open, drop and Shelf imports use `NotesMediaFactory` bookmarks and its existing media validation; no source is copied into application storage. Images are limited to the existing 128 MB / 64 megapixel rule; video editing also rejects frames above 64 megapixels. Editing is non-destructive until a user explicitly exports.

`MediaAssemblyAdjustments` describes a normalized top-left crop, clockwise quarter rotation, mirror, brightness, contrast, saturation, temperature, tint, highlights, shadows, exposure, a five-point tone curve, input black/white levels and gamma. Both photos and video use the same Core Image pipeline. Video trim uses a bounded source-time range. Photo stickers use normalized position/size and rotation. The current catalog contains the supplied package’s 14 current original LUT choices and 24 transparent original sticker PNGs; placeholder artwork and warm/cold/mono substitutes have been removed. Picker thumbnails use the original 14 filter icons. Stickers retain their source canvas aspect ratio; their normalized size describes the longest canvas edge relative to the shorter output edge. Interactive sticker placement uses separate retained layers; only a requested export composites stickers into the rendered media.

The source thumbnail and final preview are each bounded to a maximum 1,024-pixel edge. The engine caches one bounded source frame so repeated color-slider changes do not repeatedly decode the photo or movie. One serial utility worker processes previews; a hundred rapid slider changes keep only the currently running render and the newest pending request. Core Image contexts are created lazily and disable intermediate caching. A separate least-recently-used asset cache retains at most two expanded LUTs (1 MiB total) and 6 MiB of decoded sticker/icon images. Sticker-only placement changes do not schedule a Core Image preview job. No Media Assembly engine or player runs a polling loop while the user is idle. Edit events coalesce into one finite trailing action per adjustment burst.

Active video playback uses AVPlayer and a native `AVVideoCompositing` renderer with the same crop/rotation/color operations. Playback compositions have a 1,024-pixel maximum output edge. Only active playback installs a 4 Hz native progress observer. Closing the module immediately pauses/removes the player, releases its security-scoped access, drops controller preview/source-cache ownership and cancels pending preview presentation; session parameters remain for reopening. The canvas retains only its bounded last poster and sticker layers for the outgoing HUD animation, then those layers leave with the HUD. An explicitly started export can finish while the HUD is closed, without requiring the player or UI to remain alive. Its 4 Hz progress timer exists only for that finite export and is removed on completion/cancellation.

Export is performed on demand at the edited original resolution. ImageIO's installed encoders determine available PNG/JPEG/TIFF/HEIC formats. Video uses AVAssetExportSession's native highest-quality encoder, validates the chosen MOV/MP4 container against supported types, preserves native audio, and checks composition dimensions. Unsupported native codecs/formats fail explicitly; no bundled codec or network service is used. The custom video renderer uses 8-bit SDR output; HDR/wide-gamut preservation is not claimed. Video output dimensions are even for codec compatibility, at most 16,384 pixels per edge. Animated GIF editing is explicitly static first-frame Save As; GIF overwrite/export is rejected so an animation is never silently flattened in place. Multipage TIFF editing is not offered.

Every output is first written to a uniquely named sibling temporary file. Only a complete successful encode may atomically rename it into place. Source and destination file identities are rechecked immediately before commit. Save As cannot replace an existing destination without explicit overwrite; overwrite rejects encoder/container mismatches. Cancellation and commit share a lock, so cancellation before commit leaves the original untouched, while cancellation arriving after successful commit cannot falsely report an unwritten output. Failure, cancellation and success all remove temporary output files. Successful source overwrite reloads its new identity and resets already-applied edit parameters, preventing accidental double application or a stale-file error on the next edit. Originals carry no in-app edits until that final commit.

Export cannot start while a replacement source is importing. Save/confirmation panels bind the source document ID and pass it to the controller; a panel for an older document cannot export a replacement into the previous document's destination.

The focused generated-fixture suite passed 179 assertions. It exercises actual native pixel processing, large-image preview bounds, PNG export, a H.264 movie with AAC audio, native trimmed video output and filtered video frames, GIF static Save As, cancellation immediately before atomic replacement, a destination changed during export, unsupported encoding, source preservation, session reopen, overwrite identity refresh, replacement-import/stale-panel export rejection, event coalescing and hidden playback cleanup. The authentic-asset checks cover every LUT against independent trilinear RGBA8 samples, source alpha preservation, all icons/stickers, bounded caches, exact photo preview/export pixels, clockwise direct-layer/export sticker geometry, and 100 sticker moves with zero preview render jobs. No user media, preferences, running apps or production databases are involved. Large/high-frame-rate/rotated/HDR camera files and hardware-specific codec availability still require a broader real-media validation pass.

API references: [Apple Core Image filters](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Reference/CoreImageFilterReference/index.html), [custom video compositing](https://developer.apple.com/documentation/avfoundation/avvideocompositing), [asynchronous video composition requests](https://developer.apple.com/documentation/avfoundation/avasynchronousvideocompositionrequest).

## HUD integration

Media Assembly is an additive right-side utility before Add App, with the
original equipment-processing glyph. The 440×440 retained canvas shares the
existing module transition and perspective plane. Open/drop/Shelf use the same
media reference/access rules as Notes; Open's source chooser anchors below its
button. The larger image stage uses the supplied reference layout, with the
tool/filter/sticker grid at its upper left and compact zoom/export controls below.
Zoom (1×–20×), panning and direct sticker transforms move retained layers without
resampling or re-rendering the source image. Photos support direct sticker
movement, corner resize handles, a rotation handle and delete. With a sticker
selected, arrow keys move it, Option–Left/Right rotate it, Option–Up/Down resize it,
and Delete removes it. Invisible projected native sliders expose position,
size and rotation to keyboard/VoiceOver without showing duplicate flat controls.
Crop, adjustments, curves, levels and trim replace the contents of the same
upper-left tool drawer; they do not open a secondary popover. The drawer uses the
existing finite opacity transition, back control and smoothly translated,
clipped rows. Adjustment and curve overflow stays within the drawer, with a small
scroll indicator. Visible parameter tracks expose projected keyboard/VoiceOver
controls without drawing a flat native copy. Only source selection and export
confirmation retain separate anchored menus.

Crop presents the uncropped, already color-adjusted image and four direct corner
handles. Resize gestures change the saved normalized crop in original upright
source coordinates, correctly reversing preview rotation and mirroring. The
full-source preview is a transient display override: it never replaces saved
crop/export parameters. Crop-handle movement updates retained layers without
scheduling pixel renders; leaving Crop requests the final cropped preview. The
crop remains expandable on a later visit, and Reset restores the full source.
The corners remain reachable when the small tool panel overlaps the image.

Trim displays one dual-thumb range rail. Dragging either endpoint pauses playback
and immediately requests the corresponding source-time preview while the pointer
is still down. Endpoint changes and seeking are combined into one coalesced
preview request, keeping at most one pending job behind the current render. The
ordinary playback seek rail is hidden while Trim is open. Export still uses the
same validated source-time bounds. The central battery bar remains clear.

The active canvas draws stickers above the unstamped image preview; the native
export pipeline composites the same normalized position, aspect, size and
clockwise rotation into output pixels. Layer transforms never change the saved
crop or exported resolution. Hiding the module stops decoding immediately while
the bounded poster stays on the outgoing animation plane.

Save As starts in the source's folder and only offers encoders available on that
Mac. Export is disabled during import. Dialogs and overwrite confirmations bind
to the captured document ID, and the backend rejects stale requests before any
output is written. Hiding/cancelling a native dialog cannot later start export.
The session controller lives in OverlayController, so close/reopen keeps edits;
its allowlisted Event Log callback also survives a background export. Quit
requests cancellation before completing the document-write barrier.

## Supplied asset provenance

Runtime resources are under `Resources/MediaAssembly`. They are selected from the user-supplied `Endfield-PhotoMode-Assets-20261004` package, installed manifest version `2954fa80-23c1-1579-2b22-4ecfd6d70418`. Only 24 individual sticker PNGs, 14 original filter icons, and 14 active lookup tables are included. No demo images, contact sheets, legacy LUTs, full atlases, shader dumps, binaries, scripts, Lua, or scene/character lighting resources are bundled. The sanitized `provenance.json` contains relative source paths, dimensions and SHA-256 values; it excludes source-machine paths and account identifiers. The four promotional stickers Alipay, Five Towers, Happy Lemon and Katsuya are omitted from the selectable catalog and bundle. Remaining sticker identifiers are unchanged. Edit parameters are session-only; obsolete sticker identifiers are rejected rather than substituted with another image.

Every PNG is copied byte-for-byte after verifying its supplied SHA-256. Each current 32³ CUBE contains encoded RGB values exactly representable as `Float32(byte / 255)`; all 1,376,256 RGB components were independently checked before compacting them to raw RGB8 files. This preserves every original Float32 lookup node while reducing 15 MB of textual CUBE data to 1.38 MB. The 52 runtime assets and provenance total 2,758,221 bytes. Node order is red-fastest, then green, then blue; decoding restores opaque RGBA Float32 only for a selected native color cube.

After the four promotional stickers were removed, an isolated native catalog probe passed 86 assertions for removed-identifier rejection, missing removed resources, retained identifier round trips, original image dimensions, thumbnail limits and the decoded-cache bound. A separate manifest audit verified all 52 remaining resource sizes and SHA-256 values and exact correspondence between the 24 selectable sticker IDs and bundled PNGs. This check did not launch the app or touch user media.

The filters apply the supplied **external encoded lookup component** to ordinary photo/video colors through Core Image. The package explicitly distinguishes this component from the game’s entire rendering pipeline. Scene-dependent ACES/HDR grading, Bloom, character lighting, black-gold particles, dither, live texture-view state and camera/time/RNG inputs cannot be reconstructed from a finished arbitrary photo; no invented approximation of those effects is presented as original. The source game’s full-frame rendering therefore is not claimed to be pixel-identical. Black-gold/warm/pale names identify their authentic source LUTs, with the same bounded native processing as the remaining presets.

Native API contract: [Apple color cube data](https://developer.apple.com/documentation/coreimage/cicolorcubewithcolorspace/cubedata) and [Core Image filter reference](https://developer.apple.com/library/archive/documentation/GraphicsImaging/Reference/CoreImageFilterReference/index.html).

## Inline editing verification (2026-10-05)

The retained-canvas fixture now covers inline replacement for Crop/Adjust/Curves/
Levels/Trim, reachability and clipping of the long adjustment list, coupled level
limits, direct crop expansion, all eight rotation/mirror source-coordinate
round trips, and zero preview jobs during crop-handle movement. A disposable
2-second H.264 movie exercises both trim thumbs before mouse-up, immediate
source-time changes, range separation and the bounded pending-render queue.
Native Batch 7/8 checks use the inline canvas APIs rather than obsolete popover
sliders. These fixtures never open user media or persistent production stores.

The focused copied-source harness passed **57 assertions** for this follow-up. It used the real retained Media canvas, interaction adapter, menus, engine and controller; only unrelated shell configuration and Shelf access were isolated adapters. The full native HUD smoke check remains a separate integration run.


## Close the active source (2026-10-05)

The × below Open uses the shared button style and projected accessibility.
It closes only the current editor session: pending import/preview generations
are invalidated, video playback and observers are released, and temporary edit,
zoom and cache state is cleared. Source image/video files are never deleted or
modified. A short poster fade uses existing textures without retaining playback.
An in-progress export keeps Close disabled until the export finishes or is
cancelled. The focused fixture now passes 71 assertions, including real generated
H.264 playback, an in-flight import, reset state and unchanged source bytes.
