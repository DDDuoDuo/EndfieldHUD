# Windows rebuild

The previous Windows preview was rejected and removed from this build. This is
a fresh port of the **macOS v1.2.0 build 18 application**, pinned in
[`source-authority.json`](source-authority.json). Current macOS `Sources/` and
`Resources/` define the UI and behavior. The raw game scene alone is not the app.

The current target builds portable scene, motion and data components, a native
D3D11/DirectComposition renderer, retained source material and label adapters,
an event-driven window host, and Windows battery, clipboard and master-volume
service foundations. An explicit development preview now combines the source
shell, native captions/icons, clock and custom cursor. An optional isolated
Notes preview adds scrollable text and rich-text editing, font/size/color/trait menus,
checklists, moving, resizing, pinning and confirmed deletion. Temporary File Shelf,
synthetic Clipboard history, Volume, Work Mode and Event Log use the same module transitions,
renderer and frame clock. Their hidden integration tests pass. Settings now joins
that shared preview, with event-driven shell captions, theme, motion, clock and
hotkey preferences. Connected module captions now update on the same language-change event.
Still-image, GIF, drawing and real Windows video import/playback pass isolated
integration checks. Archive storage/category models and its standalone editor
are verified; its shared application integration now has hidden hardware coverage. Other module bodies still need integration.
An optional resident preview adds the
original icon, tray menu and Ctrl+` reopening. Since checkpoint 122,
`EndfieldHUD.exe` is the production application owner. Every module of the
section 6 table joins it as a built-in owner or a registered module area
(checkpoint 123 below). Live desktop acceptance and a signed release are still
outstanding, so it is not a release candidate.

The fresh graphics path now also runs the original Mac material programs. A
build-only exporter captures the current desktop shell's actual geometry,
textures, uniforms, animation library and native model layers in an isolated
fixture. Original Swift sources and assets stay unchanged. Shader translation
uses a pinned SPIRV-Cross revision, followed by Windows shader compilation and
reflection checks; no shader math is replaced with a generic approximation.

```sh
python windows/tools/check_source_authority.py
cmake -S windows -B build/windows-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows-core --config Release
ctest --test-dir build/windows-core -C Release --output-on-failure
```

Tests use synthetic fixtures and temporary directories. They do not read the
installed Mac app's data, clipboard, account session or windows. Windows uses its
system SQLite library; the portable tests use system SQLite on macOS/Linux.

Graphics tests render synthetic fixtures into owned offscreen textures. They
check linear-light blending, transparent edges, clipping, retained resources and
device recreation. An explicit `--composition --hardware` run of
`native_renderer_tests` also checks a hidden owned window in a logged-in Windows
desktop session. This path passed on the test laptop; it never captures another
window. Passing it does not establish game-material, full-HUD or performance
parity. Service tests exercise pure models and do not read or modify the real
clipboard or audio devices.

The first 1280×800 original-material replay contains 133 source batches and 139
passes. Against the Mac renderer's raw target, the Windows WARP result has a
mean channel error of 0.00721 out of 255; 99.98994% of pixels differ by at most
two levels in every channel. Hardware replay also runs on the test laptop.
This comparison excludes native labels, module content and the desktop backdrop;
it is not a full-interface or performance acceptance result. The retained mesh,
texture and constant payload for this frame is 6,032,252 bytes, excluding driver
allocations, frame targets, CPU models and other modules.

The portable source animation evaluator has been compared with 27,641 samples
from the original Swift curves and ten complete opening/ambient/closing poses.
Camera tests compare the actual Mac projection at two viewport sizes. The source
layout writers, finite button controllers, image geometry and Canvas clipping
are now ported. On the Windows laptop, 36 generated frames pass 1,814,041 checks
against the original Mac builder, including opening, closing, scrolling, hover
and compound tilt. The expanded portable comparison also covers 44 frames and
retained ambient motion, with 2,414,202 checks against the original builder on
the Windows laptop. A
separate image-geometry comparison verifies original Float
positions, UVs and indices across 1,864 cases.

Native caption/icon content is rasterized locally and retained while its source
projection and masks move. The live label bridge passes 538 Windows checks,
including initial placement, content reload, clipping and repeated pointer
presentation without new C++ allocations or GPU resources. Local font
substitution remains explicit; this is not
a claim of identical Mac and Windows font rasterization. A sequence test exposed
stale camera constants when previously hidden materials returned after resizing.
The corrected retained path passes all 36 sequential Windows WARP frame
comparisons at 1280×800 and 1920×1080: at least 99.9861% of pixels differ by at
most two levels in every channel, and the worst mean channel difference is
0.00801 out of 255. This still excludes native labels, modules and backdrop.

Selectable hover/press colors now match 22,030 original Swift events, including
shared targets, interrupted fades and the grouped personal-card highlight. This
check is bit-exact on both the portable build and MSVC. Ambient updates
visit the 29 affected descendants instead of resolving all 834 source nodes.

Header, footer and all five clock layouts now have a source-derived portable
plan. The isolated original-source comparison covers 20 clock states, 216
layouts and six projected poses (13,001 checks). The native chrome adapter also
passes 25 Windows checks using those original trees. Clock or footer content changes
can rerasterize and upload only their own retained surface; Windows tests verify
that sibling labels, geometry, projection and masks stay unchanged. Animated
clock-page displacement and native font appearance still need visual checks.

The window host coalesces redraw requests and owns one cancellable wake timer.
Hidden windows schedule no rendering work. [CI run 37723655009](https://github.com/DDDuoDuo/EndfieldHUD/actions/runs/37723655009)
passed all 27 portable suites and 35 Windows suites at commit `7695dc0`.
The last published checkpoint passed 34 portable suites and all 52 Windows
suites in a Release build on the laptop. New integration results are recorded
below; these counts do not establish full-module coverage. The user has now
confirmed the updated desktop darkening/blur, contained personal-card highlight,
faster scrolling and pointer dragging. Display selection has a read-only Windows service and a portable policy
matching the Mac fallback behavior, with no polling. Actual
display switching, visible input, cursor, IME,
multi-monitor behavior and full-app performance still require acceptance tests.
The host now routes an editor's consumed keys before Windows generates text
messages, and supports coalesced private editor notifications. Hidden-window
tests cover duplicate-text prevention, unrelated-window isolation and teardown
inside the key filter; these are not live IME acceptance tests. An initial live
test also exposed a visibility ordering bug: `WM_SHOWWINDOW` arrives before
native visibility changes, so the stable frame gate could remain asleep. The
host now refreshes that gate after showing completes. An explicit
`overlay_host_tests --interactive-startup` check uses a separate, never-switched
desktop to verify hidden-startup behavior and delivery of the first frame. It
passed on the laptop, and the corrected visible editor reported a presented
frame. Chinese typing/selection were subsequently verified by the user. The first
assembled shell was also visible; user feedback identified missing backdrop,
profile highlight overflow and insufficient navigation scrolling.

The projected editor now shares the actual retained DirectWrite layout that
painted its text with caret, selection, pointer hits and TSF. Hidden native
fixtures verify multilingual text, three tilted poses, composition contracts,
cache replacement and 120 pointer updates without new text layouts or raster
work. The explicit text preview keeps its sample document in memory.
The user verified Chinese typing and selection stayed aligned in the visible
tilted editor. Japanese/Korean candidate-window behavior still needs live testing.

The assembled shell's hardware test uses the laptop's RTX 5060 adapter, confirmed
from the created rendering device. In one 1280×800 offscreen run, pointer and
scroll preparation/submission averaged about 1.06 ms and 0.95 ms respectively.
Concealed samples submitted no frames or GPU uploads and armed no window timer.
These are CPU-side test samples, not measured GPU duration, displayed FPS, or
full-app CPU percentages. The development loader still took about 2.85 seconds;
its large verification JSON has now been replaced by a compact, hashed input
option. That input totals 8.71 MB, including the native artwork. A portable
allocation probe reduced loader peak requested heap from 104.69 MB to 33.72 MB;
A 1920×1080 hardware preflight measured about 1.34 seconds for total
preparation with this compact path. Loading the animation library accounted for
about 667 ms and initial native rasterization about 226 ms. This is one synthetic
run, not a cold-start distribution or full-app launch measurement.

The current compiled input schema stores the same animation values in a bounded,
source-pinned binary without changing evaluation. It preserves every binary64
key, tangent, weight and ordering, and retains the JSON development path. The
animation section shrinks from 4,378,637 to 1,991,308 bytes; the full compact input
is 6,329,209 bytes. In a same-process Mac test, animation loading fell from about
132 to 3.2 ms and full input loading from 218 to 87 ms. These exclude GPU/window
setup and are not Windows launch measurements. Exact library checks, all 44
source frames and 26 session checkpoints pass with both input schemas.

The live preview now connects the Windows backdrop using the original source
fade, 75% blur and 63% darkness. It requires explicit `--watch-blur` pointing to
the hash-verified original resource. Windows wheel input uses the cached system
line/page setting; captured pointer dragging follows the projected navigation
plane and uses the existing bounce. Drag release cannot click a recycled button,
stationary dragging does not add an ambient-off animation wake, and focus/hide
teardown releases the host's own pointer capture. The user accepted the live
appearance and drag feel. Recording/capture behavior remains unverified.


Runtime sprite records now keep only fields consumed by the original layout and
clip logic. An isolated C++ allocation probe measured 23,305,962 fewer retained
bytes (22.23 MiB), with all source comparisons unchanged. Settled pointer and
ambient frame tests also allocate no new C++ memory after warm-up on MSVC.
Driver allocations, native surfaces and allocator overhead are additional.
Windows numeric reads avoid constructing a locale stream for each value;
original decimal tokens, integer IDs and range behavior remain preserved.

The build-only content exporter also captures five languages, light/dark themes,
selected captions and recycled shortcut slots directly from unchanged Mac code.
The default matrix deduplicates to 348 local trees (about 891 KB of descriptors).
Arbitrary new custom labels, images and accents still need a source-equivalent
runtime fitting path; the finite reference catalog does not establish that.

A compiled original-material catalog avoids runtime JSON and shader compilation.
Its schema preserves exact material templates, reflected field plans, textures
and geometry across multiple original frames; older caches remain readable.
The single-frame cache prepared in 12.5–18.9 ms on the laptop, compared with
roughly 1.6 seconds for the development JSON path, and produced identical Windows
readback bytes. These timings measure artwork preparation, not total startup.
The complete desktop catalog retains 268 templates and 66 textures in
8,364,314 bytes. The first live hover attempt exposed artwork absent from the
sampled-frame export. The exporter now derives the full texture dependency set
from nonpermanently-hidden source image components and desktop replacements,
including authored inactive/zero-alpha nodes. The compiler keeps these declared
resources with their source provenance and rejects incomplete inventories.
A template-only record from the original hover controller also retains the
clipped hint material that opening/closing frame samples do not activate; it
adds 10,487 bytes without duplicating a complete frame.
The native 1920×1080 hardware preflight passed 20 irregular opening times and
19 source hit-tested hover/press points, plus pointer, navigation-scroll, closing
and concealed stages. Hidden native backdrop tests passed 12,173 checks for
creation and restoration of caller-established state; backdrop visual parity
and capture behavior remain unverified. Driver allocations and frame targets are additional memory,
not included in the catalog size.

One requested Windows-only artwork correction is explicit at build time:
`compile_source_scene --profile-hover-outline-mask` multiplies the straight-alpha
hover texture by its original card background alpha, eliminating 3,528 faint
padding texels outside the visible card. RGB, source geometry, UVs and fade stay
unchanged. An isolated Mac reference also reproduces the original fringe, so
this correction is intentionally excluded from Mac pixel-parity claims. It adds
no runtime masking work and leaves the authoritative Mac source/assets untouched.
The correction passes isolated tests and the Windows CI build; the user also
confirmed its visible result on the laptop.

The compiled animation input was subsequently measured on the same Windows
laptop at 1920×1080: total preparation fell from about 1,338 to 720 ms, and
animation loading from 667 to 14 ms. Pointer and navigation-scroll preparation
and submission each averaged about 0.63 ms. A repeat after integrating the
shared native composition prepared in 746 ms, with 0.69 ms pointer samples;
concealed samples again submitted no frames/uploads and armed no timer. These are synthetic hardware-test
samples, not displayed FPS, GPU duration or full-app cold-start measurements.

Module integration now has caller-clock transition orchestration and an ordered
native scene composition path. The latter retains unrelated module/chrome
resources and performs no allocation or raster work on warmed placement-only
frames. Notes has a source-derived state controller for plain-text creation and
editing, selection, pinning, confirmed deletion and drag/resize, with temporary
SQLite tests. It preserves unsaved drafts on persistence failure; dragging
retains geometry rather than copying the note's text/media payload each frame.
Notes now also has plain-text presentation descriptors and a retained native
card adapter. The original Mac fixture passes 286 presentation checks; native
fixtures cover source feedback timing, rounded clipping, resize-grip order,
sibling-resource retention and 120 allocation-free pointer/closing placements.
Tiny clamped cards keep their clipping; oversized raster groups reject before
replacing the previous valid scene. Large-card tiling remains required.

The native transition mask preserves the six original pentagons on the GPU,
intersected with existing plane clips. Caller-clock changes allocate no frame
storage or re-rasterize content. Transform interpolation matches 1,680 samples
from Core Animation (maximum matrix error 2.67e-15); this sampler remains an
explicit native integration API, not a change to the existing portable timing
helper. Edge antialiasing still needs visual comparison.

The optional plain-text preview now connects these foundations to the visible
shell. Rich formatting, to-do, drawing and media behavior still require their
actual module adapters.
Native font measurement, rounded projected editor clipping and registration strokes
now compose through the same renderer. An isolated Notes workspace fixture edits
multilingual text, retains its tilted caret through 120 closing/pointer poses
without new layout/raster work, commits through NotesState, and reopens a fresh
temporary SQLite store. It uses no real account or Notes data. The connected
workspace remains a bounded development preview; no complete Notes module is
claimed.

The source toolbar and ten menu states pass 8,667 checks against the original
Mac layer trees. Finite section/card/menu motion uses caller time and starts no
independent timer. Registration geometry matches 20,808 Core Graphics coverage
samples; Core Animation edge antialiasing remains unverified. Native editor tests
query the exact DirectWrite object used to paint glyphs, rather than guessing
caret heights from line spacing. One CJK fallback line has a measured 0.1113-point
baseline difference between settled and editing layouts; an original-Mac
comparison is still required before changing that platform behavior.

A later 1920×1080 hardware preflight prepared the synthetic shell in 594 ms, with
0.68 ms average pointer preparation/submission. All concealed samples again
recorded zero CPU time, submitted frames and armed timers. These remain single
synthetic runs, not full-app launch, CPU-percentage or GPU-time measurements.

Retained native menu groups now apply fade and tilt to one cached GPU surface,
including the original overflow shadow. Group opacity is verified against
completed-menu readback at 25%, 50% and 75%; 120 tilt/fade frames allocate no new
CPU storage or artwork. The renderer enforces bounded group memory and retains
unchanged source-shell output. A repeat laptop preflight after this change took
643 ms to prepare; concealed samples again submitted no frames, armed no timer
and recorded zero process CPU time. These are synthetic checks, not full-app
performance acceptance.

The plain Notes workspace coordinator now owns card/editor lifetime, selection,
pinning, confirmed removal and drag/resize through one existing NotesState. It
retains retired artwork until the shared composition stops borrowing it. Its
pointer and card-movement tests allocate no new CPU storage after warm-up.
Per-card creation/deletion and section transitions now follow caller-clock
source timing in the connected preview. Pinned cards retain their placement
across section changes. The other Notes kinds remain separate work.

Temporary File Shelf now has a metadata-only interaction model. Selection,
range/toggle selection, stable drag order, two-column scrolling, partial removal
errors, clear confirmation and source Quick Look requests pass 15,691 checks,
including 1,200 operations/geometry traces from the original Swift implementation.
Windows metadata persistence and native file identity/access are now implemented
in separate bounded adapters. The presentation descriptor also passes 175,453
checks against 20 detached original Mac canvases on both macOS and Windows.
Only eight visible cards are retained even for a 10,000-item shelf; ordinary
scrolling preserves their local artwork. Native rendering, file icons, picker and
preview are still separate integration work. Model and descriptor tests do not
establish those features.

The current portable build passes 44 suites and the clean Windows Release build
passes all 62 registered suites. The laptop passes the focused
Notes workspace/editor and Shelf tests; the integrated Notes owner now passes
561 checks, including editing/settled wheel routing, queued selection notifications,
owner-message feedback prevention and synchronous capture notifications with an
older outer callback. The first live Notes preview
crashed on dragging: capture reentry advanced a child clock before an earlier
refresh resumed. The regression reproduces the exact previous-build failure.
A single monotonic Notes owner clock now keeps nested events at the same instant,
clamps resumed older timestamps and preserves strict child-scene validation.
Backdrop work also finishes before borrowing the current shell frame. No new
timer, service or per-frame allocation is added. The user subsequently verified
dragging, Chinese typing, pinning, section changes and deletion. Editing delayed
the whole HUD, and note scrolling was missing; those failures are tracked below.

The integrated plain Notes fixture exercises the same owner as the visible
preview: Unicode input, drag/resize, pin/unpin, create, deletion confirmation,
section transitions and resource teardown. Its warmed pointer/hover frames
retain text layouts and GPU resources without C++ allocations. The original
confirmation geometry passes 5,892 source checks, and the two prepared toolbar
icons retain their pinned source pixels in a 4,724-byte development bundle.
These automated checks do not replace live acceptance of editing latency and scrolling.

The Notes repair routes wheel input through the current tilted card plane,
retains fractional offsets per session, and preserves them when entering/leaving
editing. Scrolling repaints only the viewport using the same DirectWrite layout
as hits, selection and IME geometry; it neither rewrites SQLite nor creates a
new layout. Typing reveals the caret. At scroll limits, repeated wheel events
create no artwork and do not leak to the navigation behind Notes. The editor's
visible scroll thumb remains a separate source-parity task.

Caret moves now update numeric placement, while empty selection/composition
artwork is reused. A resource-only composition update retains sibling draw
records and uploads only changed surfaces. Texture conversion fast paths use a
512-byte table and preserve every tested RGB/alpha byte result, including
partial alpha; no worker, timer or polling service was added.

A six-sample synthetic Windows WARP comparison on the same laptop measured
large-viewport (602×242 points, 2× raster) short-text typing at about 49.26 →
6.16 ms mean. Caret-only movement fell from about 37 ms to below 0.2 ms, with no
layout, raster or texture upload. In the small 222×87 viewport, unchanged entry
fell from 30.13 to 13.46 ms. Large-viewport entry remains about 39.17 ms; unchanged
exit preparation/publication totals about 17.34 ms. These are synchronous
CPU-side fixture stages, not displayed FPS or a real IME performance guarantee.
The separate preview uses temporary sample data for live acceptance.

The user rejected that repair's live editing performance: typing and pointer
interaction could still stall the entire HUD. Profiling the actual visible HUD
with Windows text services exposed a separate feedback loop: each editor pose
update posted a layout notice to its owner, which requested another frame. The
owner no longer receives its own placement back as new work. Required TSF
layout notifications still keep IME candidate geometry aligned; genuine text,
selection and composition changes still notify the owner.

A bounded posted-message regression failed on the previous code: one external
pose update generated 16 frames/16 owner notices and still had queued work after
32 dispatches. It passes with the fix. All 62 Windows suites and 561 Notes owner
checks pass, including eight changed poses with synchronous TSF geometry queries
and no owner echo. The Mac source/resource authority check remains unchanged.

Two 16-second visible synthetic runs on the same Windows laptop used the real
text service and the same controlled tilt. During the five-second focused-tilt
phase, summed CPU-stage duration fell from 4,954 to 933 ms (about 81% less).
The first timing recorder can double-count scope entries when returned by value,
so its call counts are not used as frame-cadence evidence. The recorder now
transfers scope ownership explicitly; a corrected count run is pending.
Layout-only owner posts fell from 822 to zero; all 300 changed placements in the repaired run still notified TSF. These
are inclusive CPU timings, not GPU completion times, displayed FPS or measured
human typing latency. No keystrokes/composition were injected into these runs.
The user then confirmed the corrected manual preview was responsive while
testing typing, selection, scrolling and pointer movement. Diagnostics are
explicit opt-in, use fresh sample Notes data, record no document text or screen,
and add no timer, worker or polling service. The normal path does not sample
performance clocks.

Very long text is not fixed by those short-text gains. At 32,768 UTF-16 units,
typing still took about 115–163 ms: each edit builds a full layout, and committing
changed text rebuilds its complete line index. The current 65,536-unit plain
fixture limit is explicit and never truncates stored text. Incremental long-text
layout, native rich editing and full source document capacity remain required;
card entry/exit still rebuilds its changed local artwork synchronously.

Shelf records use native 128-bit file identities plus their volume, never a
path-only identity guess or a reinterpreted Mac bookmark. One bounded store
persists metadata atomically and retains access handles only while needed.
The Windows adapter now passes 324 synthetic checks, including replacement
rejection, Unicode paths, hard links, symbolic links, copy-lifetime teardown
and file/directory rename blocking while leased. It requests read access to
participate in Windows sharing rules but reads no contents or directory entries.
No watcher, background scan or second service was added. The copy-only OLE transfer adapter now passes a separate hidden-window Windows
suite, including actual drop-target registration, Unicode CF_HDROP, coalesced
notifications, cancellation and leases that survive asynchronous extraction.
Incoming data is released before the metadata commit, and UI notices remain
outside COM callbacks. The native file picker, bounded icon worker and shelf actions are connected.
Live Explorer drops/picker behavior, virtual files and direct tray drops still
require their own integration or acceptance checks.

All MSVC targets explicitly use UTF-8 source and execution encodings, including
on Chinese-language Windows. Incremental deployment must copy changed files
with fresh timestamps. Public C++ layout changes additionally require a clean
build or a new build directory: reusing dependent objects produced inconsistent
fixtures even after fresh deployment timestamps. A clean native build passed
all nine targeted transfer/rendering/Notes suites and the 470-check Notes owner
fixture after the mask layout changed.


The settled shelf bridge retains only visible cards and shares the module's
projection, clips and owner clock. Static card artwork is grouped while mutable
highlights stay separate. A distant scroll can retain two generations until the
owner publishes replacement draws; this uses 176 card raster entries, plus
chrome. Shared-cache capacity is checked before staging, without evicting live
Notes/shell resources. Native tests cover scroll recycling, source paint order,
hover/selection, failed replacement rollback and teardown. The shelf is connected to the shared preview with selection, scrolling,
reference persistence, removal/clear confirmation, native icons and picker,
copy-only drag preparation and finite reveal/drop animation. Shell preview is
being added separately; visible drag/drop and visual parity remain unverified.

The retained mask shader now supports the source subsection’s four six-vertex
strips alongside the existing six five-vertex module strips. Both use the same
fixed constant buffer, with no per-frame bitmap mask. Native readback verifies
source path coverage, both windings and ancestor clipping while preserving GPU
resource counts. The source subsection timing and composed shelf reveal remain
a separate adapter; this result alone is not animation parity.


Native icon pixels now have one bounded in-memory owner: 24 cached snapshots,
48 total live snapshots including borrowers, an 8 MiB cap and a 256-pixel edge
limit. The rasterizer borrows exact immutable revisions without file decoding
or a second retained pixel copy. The shelf accepts these only for native file
icons; original Depot artwork still requires pinned assets. Native tests verify
alpha, orientation, revision replacement, rollback and zero provider requests
from 120 unchanged pointer frames. This is an input/cache foundation, not live
file-icon integration.

The Notes rich-text model passes 5,659 portable/native checks for UTF-16 ranges,
source style capture, typing/selection changes, bounded undo and IME grouping.
An unchanged-source Mac reference records 22 real formatting/layout cases,
including mixed sizes, fonts, paragraph normalization and undo. Its detached
text view creates one explicitly recorded invisible AppKit input helper; it
never shows a window or activates the app. The native typed rich editor now preserves runs and shares its exact DWrite
layout with painting, selection and IME geometry. Color effects remain valid
after scrolling to a new raster target. Native formatting-menu tests exercise
selection-only size/traits/color changes, scrolling, tilt and finite dismissal.
Notes workspace integration and imported paragraph-format undo remain open;
source data must never be silently flattened to plain text.

The subsection timing and transform adapter matches 666 paused original Mac
samples in 265,584 comparisons. It also preserves authored mask keyframes.
Intermediate mask shape is a separate problem: Core Animation normalizes the
paths into curved contours, sometimes with five subpaths. A build-only sampled
candidate takes 98,856 bytes for both directions and stays within 0.0142 local
points at 394 independent holdouts. The development shelf now uses this bounded sample asset during its finite
0.26-second reveal, with no settled mask repaint. It uses nearest-endpoint
selection across topology gaps no larger than 1e-12 in normalized time. Native
readback verifies alpha-mask composition/retirement; exact live Core Animation
raster/antialias parity remains unverified, so this is not a full-parity claim.



Latest Windows validation (2026-10-08, snapshot 68 plus the Event Log native
include correction): **83 suites passed**, plus **265,584** original subsection
transform checks, **686** connected Notes checks and **151** checklist-owner checks.
The user confirmed font selection and repeated font-menu scrolling stayed open in
live preview 20 after two earlier dismissals. Its event log contains completed
selections and scrolls with no callback error. The earlier dismissal has no proven
cause yet; additional real-host burst-input coverage is in progress. This manual
success is not evidence that an unidentified intermittent fault was repaired.
The new shelf owner fixture imports only owned temporary files, persists their
references, scrolls/selects/removes/clears them and confirms every original file
survives. Sixty settled tilt frames create no text layouts, rasters, textures or
meshes. The menu fixture similarly retains resources through 120 tilt frames.
These checks run in hidden offscreen windows; they do not open Explorer, native
preview handlers, dialogs, another app's data or the real clipboard.

Volume and Event Log owners are verified with injected data/actions only. Volume
mirrors the original artwork (44,329 detached Mac comparisons), balances confirmed
stereo channels, and lists input/output endpoints through the existing audio
service. Actual default-device switching remains unavailable through the public
API used here; the live fixture's fake switch is not a system capability. Per-app
audio session control is still being connected to one sleeping worker.

Event Log preserves source categories, bounded rows, confirmation and the original
finite page handoff. Live OS event recording, persistence and long Unicode name
compaction remain unconnected. Clipboard uses its own source-derived reveal mask,
retains press timing when a row is rebuilt, and allocates no frame-time resources
for settled tilt. Its fixture does not inspect the user's real clipboard.

The bounded WIC still/GIF decoder passes generated-media, cancellation, retained
frame and teardown tests on Windows. It has one worker and no idle polling. This
validates the decoder, not connected Notes media UI or cross-platform decoder
pixel identity. GPU video integration remains in progress.


Reference tools (macOS, temporary fixture data only):

```sh
bash windows/tools/export_shell_packet.sh build/windows-shell-packet
bash windows/tools/module_reference.sh build/windows-module-reference
```

`translate_source_shaders.py`, `compile_source_shaders` and
`replay_source_packet`, `compile_source_scene` and `replay_watch_frames` form the
build/verification path. Their development
exports, raw readbacks, compiler caches and documentation GIFs are not runtime
assets and must not be bundled with the application.

Completion requires the actual desktop shell, every module and Windows service,
same data contracts, projected editing, current animation behavior, and live
visual/input/performance checks on the test laptop. Passing core tests establishes
none of those broader acceptance results. See [the migration requirements](../WINDOWS-MIGRATION.md).

The next isolated Windows checkpoint passes all 88 suites, plus the original
subsection comparison and combined Notes, checklist and real-host font-menu
checks. The user also confirmed font selection and scrolling work in preview20;
the earlier intermittent disappearance has not been conclusively reproduced.
Limited-user Windows tests skip symlink creation when the OS refuses that
privilege; the path/JSON checks still run, and the symlink case passes on macOS.

Media card artwork now accepts borrowed resident textures, preserves aspect-fit
and clipping, and updates tilt/progress without raster or resource uploads.
Image/GIF playback uses explicit deadlines on the host's existing waitable timer.
The media texture bridge performs same-device conversion; its WARP test verifies
texture lifetime/color, not hardware video decoding. Event Log persistence and
Unicode metadata filtering are verified separately, including all 4,261 original
Swift name cases against Windows ICU15.1. Per-application WASAPI controls compile
and pass injected worker tests; actual audio control, connected media insertion
and video playback remain unverified/in progress. These checks are not a stable
release or complete module migration claim.


Snapshot71 (2026-10-08) passes93 Windows suites plus the original subsection,
combined Notes, checklist, font-host and media-menu checks. Notes image/GIF
insertion now uses the existing decoder worker and root deadline; source/Shelf
menus use retained projected artwork and finite transitions. Shelf insertion
moves an independent access lease through inspection, playback and retirement.
The root publishes supplemental media revisions even when draw-vector addresses
stay unchanged. Closing preserves artwork through the fade while cancelling
playback work; completed closing clears media and deadlines.

Event Log now uses its persistent owner in the isolated preview, with the same
root timer for debounce and one lazy bounded utility worker for atomic writes.
Rejected saves retain the latest immutable snapshot until queue space is freed.
Shutdown joins accepted writes before destroying the window, and stale callbacks
cannot access retired module owners. The native hidden-save test originally held
its own diagnostic file reader open during replacement; closing that test reader
restored the intended Windows sharing contract without changing production code.

This checkpoint validates generated files and hidden render targets only. Native
media-picker/visible image insertion, actual audio sessions, video playback and
all remaining module bodies are still integration/desktop acceptance work. The
clock, Work Mode and Archive additions under development are outside snapshot71.

## October 8 integration checkpoint

The fresh Windows Release build passed all 103 automated suites. This includes
Work Mode's original countdown/stopwatch behavior, the clock, tilted single-line
text fields, Archive category/storage compatibility, all 66 original icon assets,
and tray/hotkey lifecycle tests. Separate owned-window tests passed Notes rich
formatting, checklists, image/GIF import and the original subsection transitions.
The user confirmed the diagnostic font-menu preview now works; the earlier
intermittent disappearance has no confirmed root cause yet.

At that earlier checkpoint, the actual hardware video test failed to expose its first paused frame even
though Windows reports loaded media. It uses a tiny synthetic silent H.264 file,
not user media. This is an explicit unresolved gate. No stable Windows release
is available yet, and these checks do not establish full-app performance parity.


The next checkpoint (snapshot 78 plus its shared-cache test correction) passes
all 116 Windows suites, the source subsection comparison, and the connected
Notes formatting, checklist, image/GIF and drawing-owner checks. Battery artwork
matches 51,814 detached original-source comparisons; Settings and Archive artwork
have independent native tests but are not connected to the live shell yet.

The user confirmed Work Mode is smooth. Clipboard scrolling then exposed a
whole-application graphics-cache count limit that its standalone test missed.
The count ceiling now accommodates retained shell/module artwork and replacement
generations; the 256 MiB byte ceiling is unchanged and no slots are preallocated.
A hidden hardware run cycles seven connected module owners three times and sends
72 Clipboard wheel events. Actual scroll offsets move and return to the top.
Each completed cycle retains 315 raster entries / 52,777,320 bytes, with no growth;
these are raster-cache bytes, not whole-process RAM or GPU memory. The app exits
with code 0. The user subsequently confirmed Clipboard scrolling, the summon hotkey, and the tray icon are smooth and working.

At snapshot 78, paused video was still unresolved; the later checkpoint below supersedes that gate. The new drawing owner shares the existing
renderer, input and persistence; it creates no timer or worker. Common Notes
preview code now compiles once and is linked by the fixtures, reducing duplicate
build intermediates without changing runtime appearance.


Snapshot 81 with corrections 81b/81c verifies the real silent Media Foundation
path on the laptop: 14 worker-poster checks, 38 real decoder checks, and 160
complete Notes video import/seek/play checks. The synthetic H.264 fixture is now
64 × 64 (the earlier 32 × 32 frame was below the decoder's documented minimum).
Paused seeks request the existing host clock only until the requested frame is
available; they neither play audio nor introduce a polling timer.

The full Windows run passed 120 of 122 registered suites; its two failures were
corrected and independently rerun successfully, alongside 715 Notes-owner checks,
70 Archive-owner checks, and 29 native Settings-safety checks. Archive tilted
frames no longer copy unchanged draw resources. Equal Settings colors retain
their exact endpoint instead of producing a redundant dirty frame. The original
source feedback oracle passes 3,860 comparisons.

WM_ACTIVATEAPP now distinguishes switching applications from an owned text field
or file dialog. The shell closes on application deactivation by default and a
hotkey brings an unfocused overlay forward on the first press. Native activation
routing is tested. The user has now confirmed animated Alt+Tab closing,
one-press hotkey reopening, and the preference that leaves the overlay open. The concealed-tray
Quit path also stops scheduling frames before shutdown. These fixes and standalone
Archive/Settings owners are verified source work, not a complete Windows release.
Remaining modules/services, and full performance and accessibility acceptance
remain in progress.

Snapshot 82 plus corrections 82b/82c connects the four Settings pages to the
same renderer, source module transitions, host clock, and utility file queue.
Settings load/save uses the isolated preview directory; failed writes retain the
latest settings and prevent silent quit. System/theme/language/motion changes
update the shell on change events, without remeasuring captions during tilt.
The native cache test covers eleven module bodies over three full cycles,
including Settings and repeated Clipboard scrolling. It passes without growing
the retained raster cache between completed cycles; this is not whole-app RAM,
GPU, frame-rate or accessibility acceptance.

The Windows run passed 124 of 126 registered suites. Both remaining failures
were invalid JSON fixture construction (null instead of object), corrected and
rerun successfully without weakening assertions. The other explicit Notes,
media, source-transition and full hardware module checks also passed. The live
preview exposes the close-on-focus-loss setting; both settings states and
one-press hotkey reopening are user-confirmed. Real startup registration, display selection,
custom icons, global module preferences and the full release owner still need
integration; the isolated preview does not change the laptop's startup or data.


The next typography checkpoint bundles Noto Sans SC for default HUD text and
numbers, and Noto Sans KR for Korean. Both fonts are private application resources;
no Windows font installation is required. Language changes refresh retained
captions and editing layouts through one event, preserving document data and
explicit user font choices. The five selectable clock styles keep their existing
faces. Fonts are not reloaded or reshaped during pointer tilt.

Snapshot 84b passed 138 of 141 Windows suites plus all explicit Notes, media,
Storage source comparisons and combined Archive hardware coverage. The subsequent
84c checks corrected two test-harness issues (an allocating fixture lookup and an
old full-opacity expectation after a half-opacity pose) without changing the
runtime. The private-font, Notes font-menu, Archive editor and retained-group
checks pass, including actual SC/KR default selection. The remaining Archive synthetic movie readiness check passed in 84d after checking
the actual paused worker poster instead of requiring a playback-only GPU target.
All 141 registered suites have therefore passed across 84b and its targeted
corrections; the combined hardware module check also passed after the changes.
The user confirmed the SC/KR appearance, immediate language changes, typing and
hovering in the isolated live preview. Dedicated Korean IME composition/candidate
placement still needs a specific acceptance test.

Snapshot 85 connects Storage to the same renderer and utility queue. Its combined
hardware test replaces all OS readers with fixtures before construction, checks
manual refresh and the visible 60-second deadline, and verifies that hidden
Storage and warm animation frames do not query capacity. Source color blending
matches the original AppKit conversion rather than blending encoded RGB values.

The full native run passed 141 of 142 suites. The remaining Clipboard image test
had assumed malformed streams must be rejected; both the original ImageIO path
and Windows WIC recover the tested PNG/TIFF streams. Targeted 85b/85c checks now
verify their exact recovered pixels and unchanged copy-back bytes, while keeping
CRC, truncation, orientation, alpha and size checks. All 142 suites have passed
across that run and the targeted corrections. Notes, Archive, private-font menus
and combined hardware coverage also passed after the small imported-system-font
menu correction. This validates the thumbnail helper, not live clipboard capture;
connecting Windows notifications and the metadata bridge is the next checkpoint.

Snapshot 86 connects event-driven Windows clipboard capture to the existing
shared utility queue and bounded thumbnail cache. Its explicit native-capture
option is rejected in hidden/coverage runs; all tests still use injected history
and leave the real clipboard untouched. Inactive sections do not rebuild on
clipboard notifications. Reader library, bookmark and navigation models now
match the original versioned data, including preserved additive metadata and
Mac bookmark bytes. Activity sampling and presentation foundations also pass
isolated checks; Reader format rendering and Activity UI remain separate work.

A clean laptop rebuild passed all 146 registered suites and all 18 explicit
checks. The initial incremental run retained three consumers of an older shared
Clipboard structure and crashed those test binaries; rebuilding every consumer
resolved all three without a runtime workaround. Live native clipboard capture,
Reader interaction and Activity integration are not yet accepted.

Snapshot 87 adds Activity Monitor to the shared isolated preview: overview/apps
tabs, RAM/CPU sorting, original yellow/blue graphs, matching finite transitions,
and six retained scrolling app rows. Seven targeted laptop suites plus combined
hardware coverage pass. Repeated settled frames do not rebuild graphs, text or
sort masks; repeated module cycles retain no additional raster resources. These
checks use synthetic readings, not a complete real-process metrics provider.

A retained font snapshot now lets background document rendering borrow the same
SC/KR font collections without creating another font loader. Its Windows MTA
lifetime checks and the existing Notes/Archive checks pass. Startup timestamps
now begin the opening animation after every preview owner is ready. Projection's
session model and media geometry match 247 original AppKit cases; its visible
workspace and handoff are not yet integrated.

Snapshot 90 adds read-only PDF/EPUB/TXT providers on the existing utility worker,
bounded page caches, original Chinese text decoding, and shared SC/KR document
fonts. Reader's native validation passes; its visible controls are the next
integration step. Projection now has its source interaction/handoff model and a
single-file Reader mode reuses the existing native picker. Activity has optional
real process sampling; hidden tests continue to use synthetic catalogs only.

A clean Windows build passed all 156 registered suites, the explicit backdrop
check, and combined hardware module coverage. The combined run needed a corrected
asset-checksum argument in its test script, with no runtime change. Live Reader,
Projection, real Activity sampling and full-app performance remain unverified.


Snapshot 96 connects Reader controls and the Projection workspace to the shared
renderer and host. Projection borrows the existing media broker, backdrop, file
picker and Shelf drop registration; return/quit retires its visible resources
before releasing media. Calendar civil-date/storage/artwork foundations and
Map camera/pin/presentation models now have source comparisons and bounded-state
tests. Their visible owners are still being integrated.

The Reader preview exposed slow zoom, vertical page rebound and sidebar clicks
being intercepted. Its correction retains separate base/detail page textures,
reuses one bounded EPUB image decode, and keeps document rendering on the shared
worker. Wheel changes continue from the sampled position; crossing a vertical
page boundary clears the old interpolation. Only a Reader-owned press captures
the matching release. Sidebar wheel travel is doubled while retaining the same
spring and direct dragging.

All 175 Windows suites and combined hardware module coverage passed. Reader
checks include actual button/navigation routing, retained-texture zoom/pan,
bounded real PDF/EPUB rendering and page-boundary behavior. Small synthetic
document timings do not establish full-document or whole-app performance.
The user accepted the corrected zoom, module navigation and faster sidebar in
live preview 26. Its remaining vertical page jumps are addressed in snapshot 98
below; visible acceptance of that correction is pending. Projection media/drop/return still needs live
acceptance. Neither is a stable release claim.

Snapshot 97 passes all 180 Windows suites and combined hardware module coverage.
It adds the original Map geography/raster contracts, portable OrbiPom session and
input rules, and versioned app-shortcut storage with Windows target references.
The unchanged Mac Map renderer and game engine provide independent comparison
fixtures. These foundations do not yet connect Map, Minigame or shortcut editing
to the visible Windows preview. Shortcut tests use synthetic paths and never
inspect or launch installed applications.

Snapshot 98 preserves vertical scroll position when the current-page anchor
changes. Page transitions reuse already-visible textures; when a neighboring page
is still rendering, scrolling holds at its ready boundary and continues when the
same bounded worker queue supplies it. Rapid wheel input waits for the physical
page seam before retiring the outgoing page. Zoom, existing easing, the three-page
cache and the shared frame clock remain unchanged.

All 180 native suites passed across 98 and its targeted fixture correction;
the nine Reader suites include 4,133 preview checks plus rendered pixel checks
for forward/backward, zoomed and delayed-page crossings. A reverse-scroll test
fixture needed to publish its input-time frame before checking the settled edge;
no runtime assertion was weakened. Combined hardware module coverage now opens
owned TXT and PDF fixtures and verifies cache retention after both have warmed.

That expanded check exposed a separate PDF shutdown fault in Windows' internal
PDF/DXGI teardown. The application retires every provider, worker and renderer,
then releases cached WinRT factories and invokes COM's documented unused-library
cleanup with its default unload delay before its outer apartment closes. It does
not force immediate DLL unloading or add periodic cleanup. The combined hardware
check now exits normally. Live preview 27 uses fresh temporary data; the user's
wheel/trackpad test still reports a jump across pages, so this continuity change
is not accepted yet. Live 27 exited normally. Further real-provider regressions now reproduce the
remaining scrolling problem; visible acceptance is still pending.


Native Reader diagnostics 102–104 reproduce two independent issues. Long mixed
Chinese paragraphs recomputed the previous page as a different suffix when
moving forward; the immutable outgoing bitmap could be replaced and a single
step performed up to 30 layouts. Reusing the resident exact predecessor reduces
the synthetic 24-page walk to one new layout per step, retaining the existing
three-page bound. All 156 native predecessor checks pass with that correction.

A separate actual-provider test holds the shared worker briefly while ordinary
wheel input continues. The bounded 668-point backlog then resumes too quickly:
83.7 points in one 16 ms submitted frame, versus at most 33 in the input phase.
That continuity test intentionally still fails at checkpoint 104; its correction
and live wheel/trackpad acceptance are pending. These diagnostics do not read
user document contents and do not establish whole-app performance.

Checkpoint 106 passes all 182 Windows suites and combined owned TXT/PDF hardware
coverage. It preserves the resident predecessor page and scales catch-up duration
only for delayed wheel input; ordinary scrolling retains its original 0.10-second
curve. Reversing the wheel samples the current displayed position and discards
obsolete queued motion. Typography changes also cancel stale recovery. Native
regressions cover dense Chinese text, a deliberately occupied shared worker,
forward/backward page crossings and immediate direction reversal. These changes
add no worker, timer or page cache. Live preview 28 exited normally. The user confirmed the jumps were gone but
rejected the prolonged automatic movement after wheel input. Its numeric-only
trace confirms motion continuing up to 1.4 seconds after the last input. That
catch-up policy remains unaccepted and is being replaced with direct continuous
document scrolling; no stable Reader claim is made.

Checkpoint 109 replaces that catch-up policy with ordinary continuous document
scrolling. Missing-page completion cannot start or prolong movement; new wheel
input uses the existing 0.10-second settling curve. Cached page rebasing preserves
the displayed positions in both directions. All 12 Reader/argument Windows
suites pass, including dense Chinese trajectories and same-time pixel checks,
and combined owned TXT/PDF hardware coverage exits normally. The user confirmed
live preview 29 works well with wheel/trackpad input; it exited normally. No new
worker, timer or larger page cache was added. Other module integration remains
unfinished.

Checkpoint 115g fixes the minigame crash reported during live preview 30. Skill
targeting and sprite swaps can change a texture inside an otherwise unchanged
draw buffer. The host now compares those resource identities with the retained
publication before presenting. Isolated Windows tests reproduce the old failure
and verify the correction for skill rings, current/next sprites and same-count
body replacements. Warm-frame checks retain zero allocation/upload behavior;
shared module coverage and hardware composition pass. The user confirmed skill
interaction is smooth in live preview 31, which exited normally. This remains an
integration preview; the full migration is unfinished.

Checkpoints 116–121 (2026-10-08/09) add the Calendar retained editor, scene and
notification registration, the Map store/player assets/retained scene, the
OrbiPom minigame runtime on vendored QuickJS-ng, and App Shortcut, Now Playing
and Media Assembly foundations. Calendar, Map and the minigame are wired into the
isolated preview; App Shortcut, Now Playing and Media Assembly are not yet.

Checkpoint 119 failed three suites. Checkpoint 120 fixes their causes: the
Calendar field localized glyphs from draw records that the projected editor had
not yet refreshed, and an unselected Now Playing owner published feedback before
its scene was synchronized. Its remaining module-coverage failure came from a
stale object: MSBuild's FileTracker ignores files under `%LOCALAPPDATA%`, so a
header-only layout change never rebuilt its includers. `cmake/msbuild_include_fingerprints.cmake`
now gives each source a content fingerprint of its reachable project headers,
so such edits recompile exactly their includers. Checkpoint 121 on the laptop
passes all 222 registered suites, combined module coverage and hardware
composition. Two test tolerances added during this work (one Calendar alpha
level, and the minigame long-run trajectory recorded without assertion) are
still under review; they are not parity claims.

## Checkpoint 122 (2026-10-10)

A fresh laptop integration build passes all **300** registered Windows suites,
combined module coverage and hardware composition. New in this checkpoint:

- **Production application owner.** `EndfieldHUD.exe` (GUI subsystem, Mac-rendered
  icon, VERSIONINFO from `source-authority.json`) now owns the shell through a
  `ModuleOwner`/`ModuleRegistry` interface instead of the preview's hand-chained
  owners. It adds the ported `SystemOverlayState` (63,876 checks against the Mac
  state machine), single instance and relaunch forwarding, a versioned
  current-user data root under `%LOCALAPPDATA%\EndfieldHUD`, session-end saves,
  the quit confirmation card (1,252 source checks), per-module resource packages
  whose damage disables only the dependent modules, and launch at login. The
  development preview is now a thin harness over the same owner.
- **Media Assembly** processor matching the original Core Image pipeline (crop,
  mirror, rotation, exposure, colour, temperature/tint, tone curves, levels,
  gamma, the 14 LUTs and sticker scaling), WIC/Media Foundation import and export
  with atomic commit and cancel, and its retained owner.
- **Account Linking** signing, API, DPAPI credential store, role cache, sanity
  polling/recovery projection and presentation, verified only with synthetic
  fixtures; no login has been attempted.
- **Personal Profile**, the bottom-left ID card binding and Work Mode lifetime
  hours; **Now Playing** lyrics (embedded, LRCLIB, NetEase public catalog), warm
  start and app-volume submenu; **Volume** WASAPI endpoints and per-app sessions;
  **Power** provider, charge indicator/badge and device battery popup; and the
  offline **Mac data import** (every store in the migration guide, including a
  binary/XML plist codec, backup and rollback).
- The Calendar scene comparison is strict again (at most two levels in every
  channel); the earlier alpha allowance traced to a WARP triangle seam in the
  test's own reference composite. The minigame's long-run trajectory difference
  is inherent: Apple's libm and the Windows UCRT differ by one ULP on 48 of 4,918
  recorded `sin`/`cos`/`atan2` inputs. Fed Apple's results, the Windows runtime
  reproduces all 11 JavaScriptCore snapshots at 1e-8.

Not yet connected to the production owner: Media Assembly, Now Playing, Profile,
Account Linking, the charge indicator and Mac import UI. The production Notes and
File Shelf owners still need persistent-root entry points before `EndfieldHUD.exe`
can open existing user data. Explicit platform gaps recorded so far include MOV
export (MP4 only), Highlights/Shadows (no change applied yet), HEIC export
(encoder unavailable on the test laptop), default audio device switching and a
player's internal volume (Windows adjusts its mixer session instead).

## Checkpoint 123 (2026-10-10): host integration

A fresh laptop build in the private host slot passes 341 of 344
registered suites. That includes `application_owner_coverage` and
`application_session_end_coverage` over the production owner and the 12
registered areas. The three failures come from the slot's SSH session, and the
unchanged HEAD fails them the same way:

- `projection_composition_contracts` and `projection_workspace_contracts` need a
  composition swap chain, which a non-interactive session refuses with
  `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE`. Both pass in a logged-in desktop session.
- `packet_export_contracts` reads the Mac `Sources/` tree, which the slot does not
  sync.

### Host routes

- **UI Automation root.** The HUD window answers `WM_GETOBJECT`
  (`UiaRootObjectId`) with one fragment root (`app/hud_automation.*`, hosted
  through `UiaHostProviderFromHwnd`). Every enabled owner with fragments
  appears under it as a group named after its module, in registration order.
  Its fragments use that group as parent and fragment root, and hit tests ask
  the owners in reverse order. `ModuleOwner` gained `automationRefresh`,
  `automationCount`, `automationChild`, `automationFromPoint` and
  `automationDisconnect`:
  - Refresh runs only on content events: an owner reporting changed artwork,
    module selection, opening, closing or resize.
  - Concealing disconnects every fragment. Teardown disconnects all providers.
  - The root exists only once a UIA client has asked for it.

  The built-in Volume owner implements the hooks with
  `NativeVolumeAutomation`. (Checkpoint 124: every other module now
  implements them too; see that entry.) The hidden coverage walks the root over Volume:
  groups, parent and child navigation, the hit test and the disconnect.
- **HUD file drops** (`SystemHUDView.performDragOperation`). The window keeps
  its one OLE target, owned by File Shelf. While another module is selected,
  the host installs that target's external callbacks. The COM callbacks only
  hit-test (`SourceWatchSession::buttonAt`, a read-only source hit test) and
  queue; a private message delivers the paths afterwards. The order follows
  the source: the File Shelf navigation entry (import, then select Shelf), an
  area presenting the selected module (`ModuleOwner::drop`: App shortcuts,
  Media Assembly), Reader (first PDF/EPUB/TXT), an open Archive document (up
  to 16 media files) and the Notes canvas. Pinned Notes cards keep a drop over
  them. The effect is copy only.
- **Restart handoff and device loss.** `DataRootRestart` (production only)
  runs the red power button's accepted quit. That quit commits editors, runs
  the flush chain and every save queue, closes every store and destroys the
  HWND. `main.cpp` then runs the area's work exactly once and starts a fresh
  owner on System under the same single-instance guard. A cancelled quit
  (failed save) or a session end destroys the work unrun and calls the area's
  `declined` on the UI thread. A DXGI device removal, reset, hang or driver
  error during a frame or resize ends the run with the session-end drain. A
  fresh owner then rebuilds the device and reopens the last module, at most 3
  times a minute; development previews keep the failure fatal.
- **Displays.** The HUD now follows the display chosen in Settings
  (`windowsDisplayID`); the opening code previously read the Mac UUID key.
  `WM_DISPLAYCHANGE` while the HUD is open runs OverlayController.reposition:
  the HUD keeps its display unless a fixed display is chosen, and closes
  without animation when no display remains. `WM_DPICHANGED` fits the covering
  window to its monitor's pixel bounds.
- **Tray.** The menu follows AppDelegate.makeMenu: status, *Open overlay*,
  *Work Mode*, *Settings…*, *Preview charging effect*, *About*, *Check for
  Updates…*, *Quit*. The status line and tooltip come from the Power area;
  `--preview` previews the charge at launch. *Check for Updates…* opens About,
  because no Windows updater exists (Mac `installationSupported == false`).
  The tray icon is the icon chosen in Settings (`HUDApplicationIcon.menuBarImage`):
  the tray template is inked black or white for the taskbar theme (Perlica
  stays in colour) at the system small-icon size. It is rebuilt only on the
  preference or a theme change.
- **Settings.**
  - About shows `1.2.0 (18)` from `source-authority.json`. Its GitHub and
    credit links open after the HUD closes. The update row opens the GitHub
    releases page, and automatic installation is reported unavailable.
  - *Edit position* goes to the Power area's editor, and the Power page's
    *Battery alert ›* opens Display > Battery alert.
  - The System page gains the Windows-only *Import macOS data…* row (macImport
    area, five languages).
  - The system "Animation effects" setting (`SPI_GETCLIENTAREAANIMATION`) counts
    as Reduce Motion, re-read on `WM_SETTINGCHANGE`, as the Mac honours
    `accessibilityDisplayShouldReduceMotion`.
- **Area services.**
  - File Shelf: `fileShelf`, `fileShelf.mediaChoices` and `fileShelf.openReference`.
  - `app.longWorkExecutor`: a second executor for Media Assembly exports, so
    saves never wait behind an export.
  - The Power page sink, the bottom-left ID card presenter (profile feed),
    saved app shortcuts in the source navigation between Power and Add App
    (`AppShortcutNavigation`), and `DataRootRestart`.
  - `ModuleOwner::planeEntries()` is a core-plane slot below pinned Notes. The
    Power badge moves there once the area returns it from `planeEntries()`
    instead of `floatingEntries()`.
- **Records and texts.** These now reach the Event Log:
  - Notes, Archive, Projection and Reader actions.
  - Shelf added/removed/cleared.
  - Clipboard copy-backs.

  Event Log rows show local `MM-dd HH:mm:ss` times.

  The following texts now come from the source catalogs in five languages:
  - Clipboard capture statuses: text too large, too many files and image
    dimensions; capture pauses on sleep or lock and catches up once on resume.
  - Media errors in Notes, Archive and Projection.
  - Reader captions and errors.
  - The Shelf reveal failure.

  Activity keeps 5 s of history from launch, stops on sleep or lock, loads
  visible-row icons on the shared executor and feeds the Power area's metric.
  Imported Mac font names resolve to Windows families in the rasterizer.
  Played video frames can be edited in place (`NotesVideoRequest::frameFilter`).
  The shared dialog gains a single-file mode with a filter, for the profile
  image chooser. Work Mode no longer writes `profile.json`; the profile
  area is the only writer.
- **Packaging hooks.** `EndfieldHUD.exe --data-root <absolute folder>
  [--toggle]` serves only the section 9 gate. Such a run gets its own
  single-instance names (SHA-256 of the lower-cased path), writes no startup
  entry and no restart registration, and leaves out the Calendar notification
  area. Release objects build with `/Z7`, and `EndfieldHUD.exe` links with
  `/DEBUG:FULL /OPT:REF /OPT:ICF`, so the separate symbols zip contains its PDB.
- Hidden coverage now runs 10 (both coverage runs) host integration checks
  (`hostIntegrationChecks`, at least 7 required by `run_owner_coverage.cmake`).
  New contracts check the `--data-root`/`--toggle` arguments and the isolated
  instance names (`application_root_contracts`).

### Module areas in `EndfieldHUD.exe`

These entries are the area stages' own laptop results, recorded here because
the README is the host's verification log.

- **Media Assembly** (`mediaAssembly`). The area registers its fragment and
  ships the packaged `resources/media-assembly`. There are 14
  `media_assembly_*` suites. `sourceUnavailable` and the player identity
  re-check give Mac NotesMediaFactory/makePlaybackItem parity. The
  CIHighlightShadowAdjust mapping was read from the installed CoreImage
  framework and verified against Core Image output: 720 oracle samples, worst
  float error 2.4e-7, byte-exact. The laptop codec report: heifDecode=1,
  hevcDecode=0, heicEncode=0, webpDecode=1, h264Encode=1, aacEncode=1,
  movExport=0. Gaps: MOV export, HEVC/HEIC, CPU still-export speed, and moved
  originals are not followed by file ID. Played-frame colour edits switch on
  through `frameFilter` (this checkpoint). The UIA root and the Shelf and drop
  routes are now provided by the host.
- **Now Playing** (`nowPlaying`, `app/areas/now_playing_module.*`, registered
  by `cmake/modules/now_playing.cmake`). In production:
  - Media sessions come from GSMTC.
  - Lyric and cover downloads from LRCLIB/NetEase run only while the module is
    visible: at most two requests per track, 8-entry caches, a 10 s bound,
    1 MiB lyric and 8 MiB cover limits, and no cookies, cache or redirects.
  - The playing app's per-app mixer route goes through the shared
    AudioRuntime.
  - The Event Log records `playbackAction {action, source}`.

  Development previews and hidden coverage use the source fixture player and
  one synthetic per-app route. There is no timer or thread: worker wakes post
  the area's WM_APP message, and deadlines fold into the shared waitable
  timer. Hidden coverage runs 6 steps and 95 checks, with identical raster
  usage in cycles 1 and 2. New suites: `now_playing_runtime_volume_contracts`
  and `now_playing_automation_contracts`. **Semantic difference:** SMTC cannot
  set a player's internal volume (Mac AppleScript `sound volume`), so Windows
  adjusts the playing app's per-app mixer route. Zero or several matching
  processes keep the source "unavailable" submenu. Live acceptance is run by
  the user (`now_playing_probe.exe --live` and the HUD).
- **Personal Profile** (`profile`). Hidden coverage runs 6 steps and 73
  checks, with stable raster across cycles. New suite:
  `id_card_presenter_contracts`. Extended suites: `source_scene_contracts`
  (runtime `desktop.profile.*` textures), `work_mode_contracts` (checkpoint
  listeners) and `profile_service_contracts` (observed controller lifetime).
  The host now connects the bottom-left card. It also offers the shared
  dialog's single-file mode with a filter, which the area still has to
  request. Platform gaps: `ChooseColorW` is modal with live streaming, and
  Cancel restores the earlier colour.
- **Account Linking** (`account`, commits 51fc678 and e94eba1). 11 suites:
  `hypergryph_account_{api,login,sanity_gauge,canvas,controller,profile_relay,native,platform,webview,owner,area}_contracts`.
  Hidden coverage includes the area: 6 steps and 211 checks, with identical
  raster usage in cycles 1 and 2. WebView2 SDK 1.0.4191.47 is vendored at
  `windows/third_party/webview2`, with pins checked at configure. The
  presenter is compiled into `EndfieldHUD.exe` and verified only with the
  stub and the fake site host; real-runtime login is live acceptance. After a
  Mac import commit, the account owner is reconstructed by the fresh run.
- **Power** (`powerCharge`, `hud_power_area`, `power_area_contracts`). It
  provides the charge badge and the floating alert. The host now wires the
  tray, `--preview`, *Edit position* (summons are refused while the editor is
  open), the Power page reading, Projection and the Activity telemetry. It
  also offers the plane slot, which the area has yet to use. Gap: the source shell's navigation entries are not yet UIA
  elements, so the badge's accessibility label has no host element.
- **App shortcuts** (`appShortcuts`). Data lives in the data root's
  `AppShortcuts/shortcuts.json`. A launch closes the HUD first and records
  `appShortcutOpened` in the Event Log. Imported Mac records can be relinked,
  and the original `.exe`/`.lnk` icons come from `SHDefExtractIconW`. Covered
  by `application_owner_coverage` and `app_shortcut_icons_contracts`. Gaps: no
  generic reopen event (an existing `.exe` window is activated instead), no
  packaged-app logos or AppsFolder choice in the shared dialog, and no drag
  and drop or paste onto Add App.
- **macOS data import UI** (`macImport`). The Windows-only *Import macOS
  data…* card (confirmation style, five languages) opens from the System row
  through the `MacImportEntry` service and appears above the HUD. It covers:
  - the folder dialog and bounded staging with progress;
  - review per data set, with explicit consent to skip a rejected store;
  - *Import Again* for an earlier import;
  - the restart handoff: accepted-quit teardown, then the commit with backup
    and rollback, the outcome note `<root>.mac-import-outcome.json` and a
    fresh run;
  - the next run's final report and relink list.

  Laptop results at commit 9798e54:
  - `mac_import_flow_contracts`: 282 checks; `mac_import_contracts`: 753.
  - The plist, settings, reconcile, native and tool contracts pass.
  - `application_owner_coverage` passes with the area's three cycles: an
    in-process golden import; replace plus a synthetic restart (declined,
    then accepted) and the next-run report; an invalid folder, cancelled
    staging and keyboard input.

  See `windows/core/migration/README.md` section 2a.
- **System providers.** Event Log rows format `EventLogCanvas.timestamp` in
  the current Windows time zone, checked against 486 Mac DateFormatter oracle
  cases in nine zones (`tools/event_log_timestamp_reference.sh`). The
  coverage-only `systemProviders` area exercises them in
  `application_owner_coverage`. Platform gaps:
  - per-app network needs elevation (shows a dash);
  - per-app disk: `GetProcessIoCounters` mixes all I/O (dash);
  - compressed memory and systems with more than 64 CPUs are unavailable;
  - windowless desktop apps without an AUMID are not listed;
  - the private working set replaces `phys_footprint` (the whole working set
    before build 20348);
  - Windows time-zone history before each zone's first dynamic entry;
  - the Windows-only clipboard busy notice.
- **Local modules.** The 43 Reader/Calendar/Map/Minigame/app_notification
  suites pass. New suites: `calendar_notification_identity_contracts`,
  `calendar_notification_setup_inert`, `calendar_reminder_area_contracts`,
  `reader_localization_contracts`, `reader_pdf_title_contracts`,
  `reader_accessibility_contracts` and `map_accessibility_contracts`. PDF
  metadata titles match all 65 PDFKit oracle cases, including owner-password
  RC4/AES files and repaired cross-references, which closes the earlier PDF
  title gap. Reader error localization, Event Log actions and drops are now
  connected by the host. Notification gaps: toasts fire at absolute instants;
  there is no permission prompt; Focus assist can hold them back; and a click
  routes through the COM activator without opening the HUD, as on the Mac.
  The installer owns the toast identity: `packaging/installer.cpp` registers
  the AUMID shortcut and the activator, so no separate setup helper ships.
  Reader, Map, Calendar and Minigame UIA descriptors exist but are not yet
  listed under the HUD root.
- **Notes and Archive.** Cut, copy and paste in fields with the private rich
  format. Ctrl+V pastes media files and image data as managed `Notes/Images`
  PNGs. ICU word navigation and multi-click selection work. The editor shows a
  scroll indicator, and the chunked editor promotes pastes. Cards of any size
  draw, so the "Large-card tiling remains required" note is obsolete. Culling
  covers more than 128 retained cards. The Notes store bound rose from 64 MiB
  to 1 GiB. Archive relinks Mac attachments and accepts drops. The codec
  capability matrix covers JP2, HEIC, WebP and Windows N editions. The final
  run passed 62/62 owned suites plus `application_owner_coverage`.
- **Temporary File Shelf (stage 2).** Shelf references follow moved and
  renamed originals the way a stale Mac bookmark is renewed:
  - On activation, an original whose path fails or holds another object is
    reopened by its 128-bit file ID on its volume (`OpenFileById`,
    `GetFinalPathNameByHandleW`). It is leased again by path, so rename and
    delete stay blocked, its identity is verified again, and the new path and
    name are saved.
  - Deleted originals, originals moved into `$Recycle.Bin` and other volumes
    stay unavailable; nothing is matched by name or path.

  Files show the Shell's registered type name. Folders, links and types
  without a working stream preview handler open Quick Look's generic preview
  in the same owned popup: system type icon, name, type · size and modified
  time, in the HUD language. Outgoing drags carry a Shell drag image with the
  Mac frame sizes and offsets, and incoming drags keep theirs over the HUD.
  The copy-only data object keeps bounded Shell-private formats and refuses
  performed-move and paste-result formats. Virtual-file drops are refused with
  a visible explanation, because the Shelf stores references only. **Direct
  tray-icon drop is unsupported:**
  - `Shell_NotifyIconW` has no drop contract;
  - Explorer's process draws the icon;
  - `RegisterDragDrop` accepts only the caller's windows.

  The fallback is to open the HUD and drop on the Shelf, use *Add files*, or
  press Ctrl+V. Laptop results: `file_shelf_files` 357 checks,
  `file_shelf_transfer` 4,338, `shelf_file_preview` 530, `shelf_services` 112.
  Live Explorer drags and drag images, real preview handlers and paste remain
  for desktop acceptance.
- **Packaging (2026-10-10).** `EndfieldHUD-Setup.exe` and `uninstall.exe`
  install per user, unpackaged, into `%LOCALAPPDATA%\Programs\EndfieldHUD`. The
  install adds the Start-menu AppUserModelID and toast activator, an HKCU
  uninstall entry, and launch at login per the app setting. Upgrades close the
  app through the Restart Manager, swap folders and roll back on any failure.
  Uninstall keeps user data unless the user explicitly confirms removal.
  `endfieldhud_release` stages a 27.5 MiB setup and a 27.3 MiB portable zip
  (60.7 MiB installed, 317 files) with a size report. These pass on the laptop:
  - `packaging_installer_contracts` (385 checks, temporary roots and a
    private HKCU subtree);
  - `endfieldhud_release_contracts`;
  - the performance-gate runner and tool tests;
  - the EHUDZ01 oracle tests.

  Builds are unsigned (no certificate), and the `windows.yml` workflow has not
  yet run on GitHub. See `windows/packaging/README.md`.

### Still open after this checkpoint

- (Done in checkpoint 124: every area and built-in module lists its UIA
  controls under the root.)
- Display settings (done in checkpoint 124, except the cursor):
  `chooseProfileColor(HWND, initial, title, ...)` exists and Settings uses it;
  the center logo renders and imports; the clock's style indicators are
  clickable. The cursor does not yet switch to an I-beam over editors.
- Live acceptance:
  - recording behaviour;
  - GPU driver reset (Win+Ctrl+Shift+B) and mixed-DPI hot-plug;
  - tray icon inks on light and dark taskbars;
  - file drops from Explorer onto every route;
  - Narrator and Inspect over the UIA root;
  - the import restart on the real data root;
  - the section 9 gate with `--data-root`.

## Checkpoint 123 (batch 3): consolidated status

This entry sums up batch 3 as a whole: the prep stage, the area stages and the
host integration recorded above. Code is at `3007768`; later commits only
change documentation. No part of this batch ran on the laptop's interactive
desktop. All evidence below comes from automated hidden runs, and none of
them touched the user's data, accounts or desktop.

### Test runs

| Run | Result |
| --- | --- |
| Host slot, full MSVC x64 Release build | 341 of 344 suites pass. The three failures are environmental and fail the same way on the unchanged parent: `projection_composition_contracts` and `projection_workspace_contracts` (no composition swap chain in the SSH session), and `packet_export_contracts` (no Mac `Sources/` in the slot). |
| `application_owner_coverage` | 54 module results, 19 production lifecycle checks, 10 host integration checks, 12 registered areas, ID card connected. |
| `application_session_end_coverage` | 15 lifecycle checks, 10 host integration checks. |
| Area slots (before the host stage) | Each area's own suites plus both coverage runs passed, for example Media Assembly 19 tests, Now Playing 25, Power 17, App shortcuts 16, Shelf 15, packaging 10, Reader/Calendar/Map/Minigame 43, Notes 62. |
| Prep slot | 300 of 303, with the same three environmental failures. The portable data suites also pass on the Mac under clang, libc++ and AddressSanitizer. |

Not run in this batch: the GitHub Actions workflow `windows.yml`, the section 9
gate (`package_perf_gate`) against `EndfieldHUD.exe`, and any visible run.

### What `EndfieldHUD.exe` contains

- **15 built-in owners:** Notes (including Projection), File Shelf, Clipboard,
  Archive, Volume, Event Log, Work Mode, the Power page, Settings (System,
  Display, Hotkeys, About), Storage, Activity Monitor, Reader, Calendar, Map
  and the minigame, plus the quit confirmation card.
- **8 production module areas** (generated `hud_application_areas`):

  | Area | Factory | Registered by |
  | --- | --- | --- |
  | `mediaAssembly` | `endfield::areas::mediaAssemblyModule` | `cmake/modules/media_assembly.cmake` |
  | `nowPlaying` | `endfield::areas::nowPlayingModule` | `cmake/modules/now_playing.cmake` |
  | `profile` | `endfield::areas::profileModule` | `cmake/modules/profile.cmake` |
  | `account` | `endfield::areas::accountModule` | `cmake/modules/account.cmake` |
  | `powerCharge` | `endfield::power::powerModule` | `cmake/modules/power.cmake` |
  | `appShortcuts` | `endfield::areas::appShortcutModule` | `cmake/modules/app_shortcut.cmake` |
  | `macImport` | `endfield::areas::macImportModule` | `cmake/modules/import.cmake` |
  | `calendarReminders` | `endfield::areas::calendarRemindersModule` | `cmake/modules/local.cmake` |

  `calendarReminders` constructs only when the installer's notification
  identity matches the running executable, and never in `--data-root` runs.
- **4 coverage-only areas**, linked only into `watch_session_preview`:
  `areaProbe`, `systemProviders`, `notesLongDocument` and `fileShelfCoverage`.
- Notes and File Shelf now open persistent roots, so the executable can open an
  existing data root. This closes the checkpoint 122 limitation. It was checked
  on temporary roots only (`application_data_root_contracts`, 136 checks).
- New vendored inputs: the WebView2 SDK header and x64 static loader. QuickJS-NG,
  zlib and the Noto Sans SC/KR fonts were vendored earlier. All are listed in
  `CREDITS.md`.

### Verified, by suite

Except for packaging, every area below is also exercised by both owner
coverage runs.

| Area | Suites | Main evidence |
| --- | --- | --- |
| Area contract (prep) | `application_registry_contracts` (58), `application_data_root_contracts` (136), `application_persistence_contracts`, `data_store_contracts` (144), `json_contracts` (328), `watch_preview_argument_contracts` | Fragment registration and generation, broadcasts with failure isolation, persistent Notes/Shelf roots, Mac ProfileStore load normalization, the libc++ `Json` fix |
| Media Assembly | The 14 `media_assembly_*` suites, `endfieldhud_package_contracts`, `endfieldhud_install_contracts` | Highlights/Shadows matches 720 Core Image samples (worst float error 2.4e-7); `sourceUnavailable` and the player re-check; packaged assets |
| Now Playing | All `now_playing_*` suites, including the new `now_playing_runtime_volume_contracts` and `now_playing_automation_contracts` | Coverage: 6 steps, 95 checks; per-app volume over the real `AudioRuntime` with injected backends |
| Personal Profile | 12 `profile_*`/`id_card_*` suites, including `id_card_presenter_contracts`; `source_scene_contracts`, `work_mode_contracts` | Coverage: 73 checks; runtime `desktop.profile.*` textures (CPU side); lifetime hours counted once |
| Account Linking | 11 `hypergryph_account_*` suites | Coverage: 6 steps, 211 checks, against a fake official site and the Mac oracle envelopes; WebView2 pins checked at configure |
| Power | `power_area_contracts` (118), `power_owner_contracts` (30), `charge_badge_preview_contracts` and the other charge/power suites | Badge, alert cadence on the shared timer, position editing, sleep/wake, always mode |
| App shortcuts | 11 `app_shortcut_*` suites, including `app_shortcut_icons_contracts` | Close-first launch, relink of imported records, navigation plan, original icons |
| Mac import UI | `mac_import_flow_contracts` (282), `mac_import_contracts` (753) and the plist, settings, reconcile, native, tool and exporter suites | Every card step; three coverage cycles including a synthetic restart handoff |
| System providers | `event_log_time_contracts` (1,536 checks, 486 Mac oracle cases), `clipboard_provider_contracts`, `activity_{catalog,provider,preview,state}_contracts`, `storage_files_contracts` | Local Event Log times, clipboard statuses and copy events, Activity names/icons/telemetry |
| Reader, Calendar, Map | `reader_pdf_title_contracts` (65 PDFKit cases), `reader_localization_contracts`, `reader_accessibility_contracts`, `map_accessibility_contracts`, `calendar_notification_identity_contracts`, `calendar_reminder_area_contracts` | PDF titles, five-language Reader errors, reminder identity lookup in a private registry subtree |
| Notes, Archive, Projection | `notes_long_editor_contracts` (166,924), `notes_clipboard_contracts`, `notes_text_navigation_contracts`, `notes_store_capacity_contracts`, `notes_font_names_contracts`, `notes_media_relink_contracts`, `archive_preview_contracts`, `notes_workspace_owner_contracts` | Chunked long documents, clipboard codecs, word navigation, cards of any size, stores over 64 MiB |
| File Shelf | `file_shelf_files_contracts` (357), `file_shelf_transfer_contracts` (4,338), `shelf_file_preview_contracts` (530), `shelf_services_contracts` (112) | Identity relocation, generic preview, drag images, virtual-file refusal, Event Log records |
| Packaging | `packaging_installer_contracts` (385), `packaging_format_contracts` (1,105), `endfieldhud_release_contracts`, `packaging_perf_*`, `packaging_ehudz01_contracts` (35), `packaging_identity_consistency` | Install, upgrade, rollback and uninstall in temporary folders and a private HKCU subtree; release staging; gate evaluation against a stand-in app |
| Host | `application_root_contracts` and the 10 host checks in each coverage run | `--data-root`/`--toggle`, the UIA root over Volume, the Shelf navigation drop hit test, `DataRootRestart` in production only, the Import row, the long-work executor drain |

### Corrections to the host entry above

- App shortcuts does not implement `ModuleOwner::drop`. Only Media Assembly
  does: its `drop` has the virtual's signature. A file dropped while Add App
  is shown is therefore refused, and the "no drag and drop onto Add App" gap
  still applies.
- Media Assembly now compiles its played-frame colour pass, because
  `NotesVideoRequest::frameFilter` exists. The full suite passes, but the
  pass has not run on real playback, since coverage never plays.
- `calendar_notification_setup` is still built and tested inert
  (`calendar_notification_setup_inert`). It is not shipped, because
  `packaging/installer.cpp` writes and removes the identity itself.

### Not verified yet

These paths are compiled but have never run, either by design or because they
need the desktop.

- **Host paths:** real `IFileOpenDialog`/`IFileSaveDialog` and `ChooseColorW`
  dialogs from every area; production `updateSettings`, `openModule` and
  `closeOverlay`; broadcasts from real `WM_POWERBROADCAST`, display, session,
  activation and audio messages; the accepted `DataRootRestart` path; GPU
  device-loss recovery; display reposition and the DPI fit; tray inking;
  About links; Reduce Motion from "Animation effects"; HUD drops from
  Explorer; the UIA root with Narrator or Inspect; clipboard suspension on
  sleep or lock; Mac font mapping in the rasterizer.
- **OS providers:** production Volume on the shared `AudioRuntime` with real
  WASAPI; Now Playing over GSMTC, the network and real mixer sessions; real
  battery changes, the alert window and badge routing; launching shortcuts
  and activating existing windows; Activity icons and packaged names; the
  Storage volume name; Shelf drags, drag images, preview handlers and paste;
  the Notes system clipboard; ICU on Windows builds older than 1903; Calendar
  toasts with an installed identity; WebView2 login (China and Global) and the
  community API; Media Assembly playback through `IMFMediaEngine`; profile
  textures uploaded to D3D11; a real Mac export imported into the real root;
  the interactive Setup and uninstall windows and SmartScreen; the minigame's
  1/30 s low-power tick.
- **Performance:** no section 9 numbers exist for `EndfieldHUD.exe`. The gate
  can now start it with `--data-root`. Frame pacing still needs an ETW
  recorder.
- The user's real `%LOCALAPPDATA%\EndfieldHUD\v1` root has never been opened
  by a test.

### Still open in code

- UIA (checkpoint 124: resolved for every module): the Power position editor
  and the navigation entries other than Power have no provider.
- (Checkpoint 124: the Power badge composes through `planeEntries()`, below
  pinned Notes.)
- No module updates `Migration/relink.json`. Notes, Archive and App shortcuts
  relink their own records, but none calls `MacRelinkLedger::markResolved` or
  `reconcile`, so the import card's relink list does not reflect those
  relinks. Reader books and imported Shelf items have no relink action.
- App shortcuts: no drop or paste onto Add App, no packaged-app choice or
  logos, and no original icons in the right-side navigation.
- Settings Display still lacks the custom accent colour picker, the
  center-logo import and rendering, and clickable clock style indicators.
  The cursor does not change shape over editors or grips.
- Packaging: unsigned builds, the Evergreen WebView2 runtime is a
  prerequisite, and only the x64 loader is vendored. The staged package
  ships the font licences, the OrbiPom folder (with Matter's licence) and
  (checkpoint 124) the WebView2 SDK's `LICENSE.txt` and `NOTICE.txt`. It
  does not ship QuickJS-NG's `LICENSE`, although it is compiled into
  `EndfieldHUD.exe`.
- (Checkpoint 124: marked stale `<root>.import-*` folders are swept at
  startup, and an interrupted activation is rolled back before the root opens.)

Recorded platform gaps (capability, reason, fallback) are collected in
`WINDOWS-MIGRATION.md`, section 6, "Windows platform gaps".

## Checkpoint 124 (2026-10-10): batch 4 host integration

The host stage applied the batch 4 area requests and its own follow-ups. A
full MSVC x64 Release build in the private host slot passes 356 of 359
registered suites. The three failures are the environmental ones recorded at
checkpoint 123 (`projection_composition_contracts` and
`projection_workspace_contracts` need a desktop session for a composition
swap chain; `packet_export_contracts` needs the Mac `Sources/` tree).

| Run | Result |
| --- | --- |
| `application_owner_coverage` | 54 module results, 19 production lifecycle checks, 81 host integration checks (at least 81 now required), 14 registered areas (15 area module results), ID card connected |
| `application_session_end_coverage` | 15 lifecycle checks, 78 host integration checks (at least 78 required) |
| `display_center_logo_contracts` (new) | The center logo against the Mac oracle: also passes on the Mac under clang with the portable no-ICU configure |
| Portable no-ICU configure | `cmake -S windows -B <dir> -DCMAKE_DISABLE_FIND_PACKAGE_ICU=TRUE -DBUILD_TESTING=ON` configures and generates; `settings_*`, `media_request_catalog`, `shelf_file_picker` and `display_center_logo` contracts pass there |

### Host routes

- **UI Automation for every module.** The built-in owners now implement the
  `ModuleOwner` hooks: Notes and Archive (`notes::NotesAutomationBinding`,
  with the app clock and a content event after an accepted peer action),
  File Shelf (its `ShelfPreview` peers), Work Mode (`NativeProfileAutomation`
  over `workModeAutomationControls`) and Reader, Map, Calendar and the
  minigame (`NativeScreenReaderPeers` over `tools::*Automation`). Volume
  re-creates its fragments when it is given another group. `ModuleOwner::
  automationModule()` names the group of an owner that presents no module
  (Notes; the Power area still has to return Power). An accepted UIA press or
  value write presents and re-lists the tree (Volume, Work Mode, the local
  peers, Notes, Archive). The tree is re-listed when an opening completes
  (the Power entry's IsEnabled). While a modal owner that lists controls
  captures the pointer (the import card, an open menu), the root's hit test
  reaches only its controls. The Projection workspace is published as
  `projection.workspace` for the projection area, withdrawn before it is
  destroyed, and the tree is marked changed on its show, close and input.
- **Drag feedback over the HUD** (`ModuleOwner::dropHover`, Mac
  draggingUpdated). `ShelfDropTarget::Callbacks` gained an optional `hover`
  and `hoverWanted`: when a route wants feedback, `DragEnter` decodes the
  paths once per drag; `DragEnter`/`DragOver` AND the owner's answer into the
  offered effect; leave and drop end it. The Shelf forwards only to its
  external route, so its own drops still read no payload while hovering. The
  host routes the drag to the area owner presenting the selected module; a
  move off it, a leave, the drop or the route closing ends the highlight.
- **Shared dialog applications mode** (`ApplicationFileRequest::
  applications`, `ShelfPickerMode::applications`): one application, an
  existing `.exe`/`.lnk` through the caller's filter or a packaged app chosen
  in AppsFolder (the default folder) returned as
  `shell:AppsFolder\<AppUserModelID>`; non-storage items, links not
  dereferenced, nothing added to Recent.
- **Media broker:** `core::ScopedMediaRequest::frameFilter` carries
  `NotesVideoRequest::frameFilter` to the frame server, so frames played
  through the broker get Media Assembly's colour edits
  (`media_assembly_preview_contracts` now asserts it).
- **Mac import:** `main.cpp` calls `recoverMacImportWork` before each run
  opens the root (`EndfieldHUD` links `hud_mac_import`). `wWinMain` calls
  `DisableProcessWindowsGhosting()` for the Shelf's watched preview thread.
- **Area services:** the Event Log owner (`eventLog.owner`, read-only) and
  Calendar's reminder-click refresh (`calendar.reminderActivation`). About's
  update row, its toggle ("Check for updates daily",
  `SettingsUpdateStatus::automaticTitle`) and the tray's *Check for Updates…*
  route to the windowsUpdates area (`app::UpdateService`); the fallback
  without it still opens the GitHub releases page.
- **Settings > Display.** *Theme color* posts a private notice; the modal
  `ChooseColorW` (`chooseProfileColor` with the "Theme color" title) streams
  live changes, OK keeps the colour, Cancel restores the starting one and a
  module switch or close dismisses it keeping the last live colour (source
  orderOut). *Center logo* follows `HUDSourceWatchView.setDesktopCenterLogo`:
  the three bundled wordmarks (`resources/center-logos`, pinned) or the
  managed `CenterLogo/<revision>.png` are decoded on the shared executor,
  cropped at alpha above 8, drawn with the white 0.7 shadow and bound as
  `desktop.center-logo` to EndfieldText (glow hidden), through the ID card
  presenter's baseline; the packet logo returns for Endfield and the pixels
  retire after the next frame. *Custom* imports one image through the shared
  dialog (WIC, EXIF orientation, at most 768 px, 32 MB / 64 MP / 16,384 px
  bounds, the five-language errors) and applies it only while the same
  selection and revision are shown. Core Graphics' shadow blur is matched by
  a Gaussian of variance (b/2)^2 + 1/2: at most 3 premultiplied levels from
  the Mac oracle on the generated logo and 8 on the presets
  (`tools/display_center_logo_reference.sh`).
- **Desktop clock** (`SystemHUDView.clockPoint`):
  `NativeChromePresentation::statusPoint` hit-tests the projected status
  panel (340 x 145). Hovering it raises the hover plate when no menu
  captures the pointer and no Notes card covers it; a style indicator
  selects that style, a horizontal swipe of 28 points changes it once per
  gesture, and the rest of the clock opens Work Mode. Its accessibility
  buttons (`layoutClockControls`: "Work Mode" and "Clock style 1-5", the
  selected one with "Selected" as its help) are UIA peers of a host owner
  that draws nothing and routes no input.
- **GPU adapter change:** on a display change the production owner asks
  `Renderer::adapterCurrent()` (`IDXGIFactory1::IsCurrent`, then the default
  adapter's LUID) and takes the device-loss fresh run when the default
  adapter moved away from the device.
- **CMake:** the Now Playing base targets are guarded by `hud_archive` /
  `hud_now_playing`, so a portable configure without ICU generates.

Hidden coverage now runs 81 host checks (78 in the session-end run, which
ends without the quit card), most of them over the real HUD root: the groups of
Work Mode, File Shelf, Archive, Media Assembly, Now Playing (6 controls),
Account and Profile (named after the module, children navigate back, the
root's hit test finds a control, concealing disconnects); the Reader peers
(in cycle 0), Map, Calendar and minigame peers pressed through UIA Invoke;
the Power entry's help and hit test; the published services and the update
rows; the colour and logo dialogs requested without opening; the center
logo (a preset, an imported custom logo, Endfield again with the pixels
retired); the clock (hit test, hover, indicator, swipe, Work Mode and its
UIA buttons); drag
feedback reaching App shortcuts and ending on leave; and the restart members
(a failed save destroys the pending work unrun and its decline runs once
from the notice; a restart pending at the confirmed quit is handed out
exactly once). In cycle 0 the host's Work Mode controller runs a stopwatch
through its accessible controls, so the profile area sees real lifetime
checkpoints.

### Found while integrating

- **Work Mode read the wall clock in hidden coverage.** `constructWorkMode`
  passed `now()` to the owner, which moved its continuous time to the
  machine's uptime, so stopwatch presses at synthetic times tracked 0 s. It
  now passes the area clock (the same value in visible runs).
- **A quit within seconds of rendering a PDF faults at exit.**
  `Windows.Data.Pdf` keeps its own Direct3D device until an internal idle
  timer releases it. Re-entering the Reader with a PDF late in the run made
  the process end first: `dxgi.dll` raised exception `0x87A` during
  `Windows.Data.Pdf`'s static destruction (exit code 2170). Waiting 30 s
  before or after the teardown avoids it; the unchanged parent passes only
  because its last PDF render happens long enough before exit. The data is
  saved before this point. The Reader peer probe therefore runs in cycle 0
  (plain text), and the fix is requested from the Reader: render through
  `IPdfRendererNative` with an owned device that the owner releases. Recorded
  in `WINDOWS-MIGRATION.md`, section 6.

### Module areas (batch 4 results, recorded by the host)

- **Media Assembly.** UI Automation hooks implemented (fragments under the
  owner's group; a UIA Invoke/SetValue refreshes them and raises
  StructureChanged on the group); hidden coverage drives the hooks through a
  stand-in group. Moved originals are followed by file ID
  (`mediaAssemblyLocate`: `OpenFileById` on the stored path's volume,
  verified volume serial, `GetFinalPathNameByHandleW`) for previews, exports
  (`MediaAssemblyExportPlan::source`, re-checked at commit) and new players;
  *Overwrite original* still targets the document's own path (Mac
  `sourceURL`). Played frames get a GPU colour filter in linear working space,
  within 1 level of the CPU reference on a frame-server surface, now carried by
  the broker. Opaque stills of at least 1 MP export on the job's GPU device in
  row bands with the sticker step on the CPU (laptop: 12 MP heavy edit 359 ms
  vs 1563 ms CPU; WARP within 1 level of the CPU export); stills with
  transparency stay on the exact CPU processor. Laptop: 19/19 (14
  `media_assembly` suites, both owner coverage runs, package, install and
  executable contracts).
- **Now Playing UI Automation.** The nowPlaying area implements the
  `ModuleOwner` automation hooks (`app/areas/now_playing_module.cpp`) over
  `native::NativeNowPlayingAutomation`. The fragments are created on the
  host's first refresh and re-created when the host hands over a new group.
  They are re-listed immediately after a UIA Invoke or SetValue (opening or
  closing the volume menu adds or removes *Playing app volume*;
  StructureChanged is raised on the group while clients listen) and
  disconnected on conceal. The fragment COM boundary returns `E_FAIL` or
  `E_OUTOFMEMORY` instead of letting owner exceptions unwind
  (`now_playing_automation_contracts` adds throwing-callback cases). Hidden
  coverage drives the hooks through a stand-in group: navigation, parent and
  fragment root, group hit test, Invoke Next, seek identity, RangeValue seek,
  volume menu by UIA with a route write and its confirmed value, Escape
  removing the slider, disconnect and reconnect, re-rooting, nothing listed
  while unpresented. It now runs 6 steps / 115 checks, with raster usage
  identical in cycles 1-2. The Now Playing fragment skips itself in a
  portable configure without ICU. Slot nowplaying: 25/25.
- **Account Linking.** The area implements the `ModuleOwner` UI Automation
  hooks (peers under the area's group of the HUD root; a UIA press posts the
  area's registered message so the host refreshes). Without the WebView2
  Runtime, Connect shows a localized explanation page linking to
  `https://developer.microsoft.com/microsoft-edge/webview2/#download-section`;
  *Check Again* continues without restart; the face names the missing
  runtime; no bootstrapper is bundled. The WebView2 SDK `LICENSE.txt` and
  `NOTICE.txt` now ship at `resources/licenses/webview2`. After the Mac import
  restart the fresh owner reads the imported cache and restricts it to the
  current user in place. Suites: `hypergryph_account_{api,login,sanity_gauge,
  canvas,controller,profile_relay,native,platform,webview,owner,area}_contracts`
  (webview 375 checks, area 49, platform 88, controller 1745, login 1111);
  account hidden coverage 6 steps, 245 checks. Commits `5dd147e`, `16a186d`.
- **Power.** The badge composes in the `planeEntries()` slot, below pinned
  Notes. The Power navigation entry is a UIA Button provided by the
  powerCharge area (`power_automation_contracts`). Hovering the Power entry
  follows the Mac `refreshChargeHover` rule: it keeps an existing hover but
  never acquires one. The supercharge banner has an exact 14-point capsule
  set (ChargeIndicatorScene drawCount 85). Final slot result: 18 of 18.
  Commits `c2a500d` and `bb4ef09`.
- **Personal Profile (batch 4).** Profile area: 6 steps, 148 checks; raster
  stable across cycles 1 and 2 (786 entries). New suites:
  `profile_automation_contracts`, `work_mode_automation_contracts`. Extended:
  `source_scene_contracts` (registered runtime desktop textures),
  `id_card_presenter_contracts` (`setBaseline`), `profile_pickers_contracts`
  (titled picker), `work_mode_preview_contracts` (`perform`/`clientRect`).
  The profile area implements the UIA hooks (buttons, sliders, the editing
  field's Edit peer) and requests the single-file image filter.
- **App shortcuts.** Saved apps with the App icon preset show their original
  application icon in the right-side navigation; `WatchAppearanceEntry`
  shares the area's `LayerImageSource`; a released snapshot paints the source
  grid; at most 20 retained icons. *+ Add App* takes one dropped `.exe`/`.lnk`
  (`ModuleOwner::drop`) and Ctrl+V of a single application file; the name
  field supports Edit-menu Cut/Copy/Paste. UI Automation peers: buttons with
  Invoke and the name-field edit with Value. The chooser asks for one file
  through the "Applications" (`*.exe;*.lnk`) filter and, with this
  checkpoint, the dialog's applications mode (AppsFolder). A navigation
  launch error selects *+ Add App*; every opening starts a fresh *+ Add App*
  session that keeps only the last failed launch error. New suite
  `app_shortcut_automation_contracts`. Gaps: no packaged-app logos; a 20-icon
  bound.
- **Mac import** (commit `e38d18d`): private-folder sweep, recovery, card UI
  Automation and the restart path in coverage. `MacImportSession` marks and
  locks `<root>.import-<uuid>` (`import-work.json`) and journals the backup in
  `activation.json` before the first rename. The macImport area sweeps only
  marked, abandoned folders at startup in bounded steps on the
  UtilityExecutor; it never touches backups, the outcome note, unmarked or
  live folders, or interrupted activations. `recoverMacImportWork` rolls an
  interrupted activation back before the root opens (`main.cpp`, this
  checkpoint). The outcome note is written right after activation. The
  import card is a UIA dialog (title, message, ProgressBar, List/ListItems,
  Invoke buttons). Hidden coverage drives the area's production
  `DataRootRestart` path against a stand-in host service: refused second
  request, declined save, accepted quit with teardown before the work,
  session end never running the work, the next run's sweep and report,
  recovery on a second synthetic root. Laptop: `mac_import_work_contracts`
  846, `mac_import_automation_contracts` 69, `mac_import_flow_contracts` 295,
  `mac_import_contracts` 753, `mac_import_native_contracts` 251,
  `mac_import_reconcile_contracts` 11; the plist, settings and tool contracts
  pass; `application_owner_coverage` passes with 1021 macImport checks. New
  tool option: `mac_import_tool --work DEST` (read-only listing).
- **System providers.** Clipboard links match ClipboardStore.
  `UniformResourceLocatorW` (ANSI fallback) is read before `CF_UNICODETEXT`
  as the link, with the string as its original text. Copy-back writes the URL
  type plus the original string. Plain strings are links up to 2048 UTF-8
  bytes. `ClipboardHistory::parsed_url` ports macOS 14+ `URL(string:)` with
  ICU UTS #46 hosts: 246 Mac oracle strings, `tools/clipboard_url_reference.sh`,
  `clipboard_url_contracts`. Over-limit headers report
  `imageDimensionsTooLarge`. Capture suspended on sleep or lock catches up the
  latest change once and re-reads an interrupted image. A `WM_POWERBROADCAST`
  storm posts one notice (`system_services_contracts` over a synthetic
  clipboard and a message-only window). systemProviders coverage runs
  SystemServices over a synthetic clipboard, and with this checkpoint checks
  the host-wired Event Log records through `eventLog.owner`.
- **Local modules.** UI Automation peers for Reader, Map, Calendar and
  Minigame: `native/screen_reader_peers` with Button/Edit/Text/Group, and
  `tools/{reader,map,calendar,orbipom}_automation`; preview projection and
  press APIs, plus Calendar editor Value and focus; the Map's 12 Hz gesture
  cadence through the shared deadline; the Calendar refresh after a reminder
  click (area sink, now published by the host); the Reader default kept as
  the automatic Noto Sans SC/KR sentinel by the user's decision. Tests: new
  `screen_reader_peers_contracts`, which includes the unchanged HUD root;
  extended reader/map/calendar/orbipom preview contracts and
  `calendar_reminder_area_contracts`. Full slot: 44/44 local tests plus
  `application_owner_coverage`. Commits `2d8aa16`, `1d6c219`, `5bc5d14`.
- **Notes, Archive, Projection.** UIA peers for Notes, Archive and Projection
  (Invoke, RangeValue, Value, focus). Font-menu dismissal root cause: a menu
  opened between frames was hit-tested before being positioned; the
  `poseMenus` fix, with the `queuedRowClick` reproduction. Undo is kept across
  promotion to the chunked editor. RTF and CF_HTML paste import is checked
  against a 45-case Mac oracle (`windows/tools/notes_paste_reference.sh`), plus
  RTF export on copy. Large-card plate and backing split (enter 305 -> 222 ms,
  exit 157 -> 60 ms, re-enter 300 -> 128 ms, exit after edit 184 -> 87 ms on
  WARP at 1600x1000 and 2 pixels per point). Slot notes: 64 of 64 owned suites
  plus `application_owner_coverage`.
- **Windows updater** (2026-10-10): the windowsUpdates area checks GitHub for
  `windows-v*` releases only (never the Mac feed or Mac assets; the unchanged
  Mac parser rejects `windows-v` tags). It checks on *Check for Updates*, or at
  most once a day when the HUD opens with "Check for updates daily" on. On
  two explicit About clicks it downloads and verifies the setup (HTTPS,
  GitHub-only redirects, published size, SHA256SUMS line, the setup's own
  payload and version), then closes the HUD and hands off to a quiet
  in-place Setup upgrade; the outcome is reported on the next run or at Setup
  exit. Only the registered installed copy updates; isolated data roots never
  check on their own. Suites: `packaging_update_check_contracts` (Mac
  HUDGitHubRelease oracle plus controller, portable),
  `packaging_update_platform_contracts` (WinHTTP against a loopback server,
  installed-copy check, verification, launcher, end to end),
  `packaging_os_text_data_contracts`, and windowsUpdates hidden coverage in
  `application_owner_coverage`. CI now runs on `windows-2025` and checks the
  OS ICU (laptop: build 26200, ICU 72.1, Unicode 15.1, tzdata 2022g) before its
  suites; `windows-2022` lacked Unicode 15.1 and the Samoa rules.
  `stage_release` writes a `windows-v` release-notes draft. Builds remain
  unsigned, so every update needs explicit consent; see
  `windows/packaging/README.md`.

### Temporary File Shelf (stage 3)

Preview containment: Shell preview handlers and Quick Look's generic preview
run on a dedicated STA preview thread with its own message pump
(`native/shelf_preview_thread.hpp`). The HUD thread only queues requests and
drains the controller's events through the Shelf's private notice. The popup
is an unowned tool window of the preview thread, centred on the HUD; an owner
window on the HUD thread would attach the two threads' input queues. Each
blocking preview operation (resolution, activation, every `IPreviewHandler`,
`IInitializeWithStream` and `IObjectWithSite` call, proxy releases, teardown)
arms a one-shot thread-pool watchdog of 10 s, and each stream read restarts
it. On expiry the call is cancelled (`CoEnableCallCancellation`,
`CoCancelCall`) and the thread is retired. The handler is abandoned: it is
never called again, its proxies are leaked, the leases and the stream's file
handle close, and a popup holding a foreign child window is detached. The
thread's apartment teardown cancels its own calls to the hung server, so the
thread ends. The Shelf then shows "The preview stopped responding and was
closed." in five languages, and the next preview starts a fresh thread.
Closing or replacing a preview cancels a running call at once. A hang outside
COM keeps its retired thread; while two remain, previews are refused with the
same message. Cloud placeholders (`IO_REPARSE_TAG_CLOUD..CLOUD_F`, or recall
attributes when the filter disguises them) are references resolved,
relocated, iconified and revealed from no-recall metadata only. Only the
explicit preview stream reopens the same identity with recall, and the
destination of a drag reads the path itself; other reparse types and
HSM-offline objects stay refused. UI Automation: one Button per Mac
HUDShelfActionButton (toolbar, card plates with a read-only Selected value,
preview/reveal/remove) plus Relink on imported cards, with the Windows
keyboard help; Invoke runs `ShelfPreview::perform`. With this checkpoint the
built-in adapter forwards the hooks. Mac import relink: unresolved
`fileShelf.item` entries of `Migration/relink.json` are listed after the
Windows references as unavailable cards. Double-click, Space or Relink opens
the picker for that item; the chosen file or folder keeps the imported
identifier and date (`ShelfRelinkSlot`) and the entry is resolved. Remove
and Clear dismiss entries, an interrupted relink is reconciled on load, and
nothing is matched by path or name. Verified on the laptop:
`shelf_preview_thread` 98 checks (including a hung out-of-process COM server
that is a hidden child of the test), `shelf_file_preview` 591,
`file_shelf_files` 383, `shelf_services` 365, all other Shelf suites, and
`application_owner_coverage` and `application_session_end_coverage` with the
extended fileShelfCoverage area. Not verified: real handlers and real hangs
on a desktop, a real OneDrive placeholder, Narrator/Inspect, and the relink
dialog itself.

### Not verified yet (added in batch 4)

- The real dialogs: *Theme color* (`ChooseColorW`), the center-logo image
  chooser, and the applications mode in AppsFolder with a packaged app.
- Drag feedback with Explorer drags over *+ Add App* (only the routing ran).
- Narrator and Inspect over every group; the Notes editor peer's focus in
  the real tree.
- An adapter switch with real GPUs; the updater against a real `windows-v`
  release; the clock hover plate and swipe with a real touchpad.

### Still open in code

- The cursor does not change to an I-beam over open projected editors (the
  only non-source cursor the Mac shows); each editor owner must expose its
  open field's client area to a host cursor hit test.
- UI Automation: the navigation entries other than Power have no provider.
  The Power area must return `core::Module::power` from `automationModule()`
  so its group is named Power. UIA buttons have no value pattern, so the
  clock's "Selected" is its help text.
- Reader: render PDFs on an owned device so a quit right after reading
  cannot fault at exit (above). Reader books have no relink action.
- Notes, Archive and App shortcuts do not update `Migration/relink.json`
  after their own relinks.
- App shortcuts: no packaged-app logos. Settings: the app-icon choice.
- Packaging: QuickJS-NG's `LICENSE` is not shipped; builds are unsigned.

## Checkpoint 124 (batch 4): consolidated status

This entry sums up batch 4 as a whole: the area stages and the host
integration recorded above. Code is at `dd113c9`; later commits change
documentation only. As in batch 3, nothing ran on the laptop's interactive
desktop, and no UI Automation client, network service, real account or real
dialog was involved. All evidence comes from automated hidden runs over
synthetic data in temporary folders and private registry subtrees.

### Test runs

| Run | Result |
| --- | --- |
| Host slot, full MSVC x64 Release build at `dd113c9` | 356 of 359 suites pass. The three failures are the environmental ones from checkpoint 123: `projection_composition_contracts` and `projection_workspace_contracts` (no composition swap chain over SSH), and `packet_export_contracts` (no Mac `Sources/` in the slot). |
| `application_owner_coverage` | 54 module results, 19 lifecycle checks, 81 host integration checks, 14 areas constructed, ID card connected. The minimum of 81 host checks is now enforced (`HOST_CHECKS`, `app/run_owner_coverage.cmake`). |
| `application_session_end_coverage` | 15 lifecycle checks, 78 host checks (minimum enforced). |
| Area slots, before the host stage | Media Assembly 19/19, Now Playing 25/25, Power 18/18, Personal Profile 23/23, App shortcuts 18/18, System providers 28/28, Reader/Map/Calendar/Minigame 44/44, Notes 64/64, packaging 13/13. Account, Mac import and Shelf passed their slots, including `application_owner_coverage`; their check counts are in the entries above. |
| Mac, clang | The portable no-ICU configure generates. The `settings`, `settings_presentation`, `media_request_catalog`, `shelf_file_picker` and `display_center_logo` contracts pass there. `clipboard_url_contracts` passes against ICU 78 under AddressSanitizer. `packaging_update_check_contracts` passes under ASan/UBSan, and `mac_import_work_contracts` and the portable part of `file_shelf_files_contracts` (52 checks) pass too. |

Not run in this batch: `windows.yml` on GitHub (so the `windows-2025` pin and
its OS ICU check are untested there), the section 9 gate on the laptop
desktop (`package_perf_gate --desktop`), and any visible run.

### What `EndfieldHUD.exe` contains

- **Owners:** the 15 built-in owners and the quit card from checkpoint 123.
  A new desktop-clock owner only lists the clock's UIA buttons; it draws
  nothing and routes no input.
- **10 production module areas:** the 8 from checkpoint 123, plus:

  | Area | Factory | Registered by | Role |
  | --- | --- | --- | --- |
  | `projection` | `endfield::notes::projectionAutomationArea` | `cmake/modules/notes.cmake` | Lists the Projection workspace's UIA peers through the `projection.workspace` service |
  | `windowsUpdates` | `endfield::areas::windowsUpdatesModule` | `cmake/modules/packaging.cmake` | Publishes `app::UpdateService`, which About and the tray now use |

- **4 coverage-only areas**, unchanged: `areaProbe`, `systemProviders`,
  `notesLongDocument` and `fileShelfCoverage`.
- **UI Automation:** the HUD window's one root lists a group for each owner
  that has fragments. These are Volume, Work Mode, Notes, Archive, Projection,
  File Shelf, Reader, Map, Calendar, the minigame, Now Playing, Media
  Assembly, Account Linking, Personal Profile, App shortcuts, the Mac import
  card, the Power navigation entry and the desktop clock. Not listed: the
  navigation entries other than Power.
- **Host routes added:**
  - drag feedback (`ModuleOwner::dropHover`);
  - the shared dialog's applications mode;
  - the broker's `frameFilter`;
  - `recoverMacImportWork` before each `openRoot`, and
    `DisableProcessWindowsGhosting`;
  - the `eventLog.owner` and `calendar.reminderActivation` services;
  - update routing;
  - Settings: the Theme color picker, the center logo and the clickable
    clock;
  - GPU adapter-change detection.
- **Package additions:** `resources/licenses/webview2/` (the WebView2 SDK's
  `LICENSE.txt` and `NOTICE.txt`) and `common/center-logos` (byte copies of
  the Mac `Resources/AppIconSources/CenterLogos`). No vendored library or
  font was added in this batch. QuickJS-NG's `LICENSE` is still not staged
  (`CREDITS.md`).

### Verified, by suite

Except for packaging, every area is also exercised by both owner coverage
runs.

| Area | Suites | Main evidence |
| --- | --- | --- |
| Host | Both coverage runs, `display_center_logo_contracts`, `shelf_file_picker_contracts`, `media_assembly_preview_contracts` | 81/78 host checks over the real HUD root. Center logo within 3 premultiplied levels of the Core Graphics oracle on the generated logo, and 8 on the presets. Applications-mode selection validation. Frames played through the broker carry the colour filter. |
| UIA providers | `screen_reader_peers_contracts` (48), `power_automation_contracts` (32), `now_playing_automation_contracts`, `profile_automation_contracts`, `work_mode_automation_contracts`, `app_shortcut_automation_contracts`, `mac_import_automation_contracts` (69), `notes_automation_contracts`, `hypergryph_account_area_contracts` | Roles, patterns, navigation, hit tests, identity across refreshes, disconnect. Owner exceptions never cross COM. The local-module and account suites compile the unchanged `app/hud_automation.cpp` root. |
| Media Assembly | The 14 `media_assembly_*` suites | Played-frame filter within 1 level of the CPU reference. GPU row bands equal a full render (0 levels), and the WARP export is within 1 level of the CPU. `compositeStickers` (23,611 checks). Moved originals followed by file ID. Laptop: 12 MP heavy edit 359 ms on the GPU vs 1,563 ms on the CPU. |
| Now Playing | All `now_playing_*` suites | 6 steps / 115 coverage checks. COM boundary guard. |
| Account Linking | `hypergryph_account_{webview,login,controller,platform,owner,area}_contracts` | Missing-runtime page and `runtimeUnavailable`. Text fit in five languages. Mac import restart contract. Cache restricted to the current user. 245 coverage checks. |
| Power | `power_area_contracts` (153), `power_automation_contracts`, `charge_badge_preview_contracts` (40), `charge_indicator_scene_contracts` | Plane slot below pinned Notes. Mac hover rule. Exact 14-point banner set (the old scaled set fails the new assertion). |
| Personal Profile | `profile_automation`, `work_mode_automation`, `work_mode_preview`, `profile_pickers`, `source_scene` and `id_card_presenter` contracts | 148 coverage checks. Registered runtime textures (CPU side). Lifetime counted once, with nonzero stopwatch time in cycle 0. |
| App shortcuts | `app_shortcut_automation_contracts`, `app_shortcut_preview_contracts`, `watch_appearance_contracts` | Original navigation icons, with no allocations on unchanged frames. Drop, paste and hover. Fresh session on each opening. |
| Mac import | `mac_import_work_contracts` (846), `mac_import_automation_contracts` (69), `mac_import_flow_contracts` (295), `mac_import_contracts` (753), `mac_import_native_contracts` (251), `mac_import_reconcile_contracts` (11), `mac_import_tool_contracts` | Sweep classification, bounded sweep, pre-open recovery, activation journal. 1,021 macImport coverage checks, including the production restart path against a stand-in host service. |
| System providers | `clipboard_url_contracts` (1,001; 246 Mac oracle strings), `clipboard_matches_contracts` (23,510; batch 5), `system_services_contracts`, `clipboard_provider_contracts` | Link parsing parity. Capture statuses. Suspension catch-up. A 10,000-message power storm posts one notice. Host-wired Event Log records checked through `eventLog.owner`. |
| Reader, Map, Calendar, Minigame | `screen_reader_peers_contracts`, `reader_preview_contracts` (6,184), `map_preview_contracts` (74), `calendar_preview_contracts` (142), `orbipom_preview_contracts`, `calendar_reminder_area_contracts` (100) | Peers and presses, the Map's 12 Hz accessibility cadence, Calendar editor Value and focus, the reminder-click refresh sink. |
| Notes, Archive, Projection | `notes_automation_contracts`, `notes_paste_import_contracts` (45 Mac oracle cases), `notes_clipboard_contracts`, `notes_format_host_contracts`, `notes_scene_contracts`, `notes_workspace_owner_contracts`, `archive_preview_contracts`, `projection_preview_contracts` | RTF/CF_HTML import and RTF export. Undo across promotion. The `queuedRowClick` reproduction fails without the fix. Large-card timings on WARP. |
| File Shelf | `shelf_preview_thread_contracts` (98), `shelf_file_preview_contracts` (591), `file_shelf_files_contracts` (383), `shelf_services_contracts` (365) | A hung out-of-process COM server is cancelled and its thread ends. Cloud placeholder classes. UIA peers. Relink ledger. |
| Packaging | `packaging_update_check_contracts`, `packaging_update_platform_contracts`, `packaging_os_text_data_contracts`, `endfieldhud_release_contracts` | Mac HUDGitHubRelease oracle (59 tags, 36 URLs, 31 client scenarios). WinHTTP against a loopback server. Installed-copy check in a private HKCU subtree. Laptop OS text data: ICU 72.1, Unicode 15.1, tzdata 2022g. |

### Clarifications to the host entry above

- "UI Automation for every module" means that every module lists its
  controls. Focus is not routed: the root's `GetFocus` returns no element,
  and the import card exposes its focused button only through
  HasKeyboardFocus, without AutomationFocusChanged. Owners raise
  StructureChanged, property and focus events only while
  `UiaClientsAreListening()` is true. It was false in every run, so no event
  has been observed.
- Until the powerCharge area returns `core::Module::power` from
  `automationModule()`, the group that holds the Power entry is named
  "EndfieldHUD".
- Media Assembly's played-frame filter is verified on a renderer
  frame-server surface and through the broker route. It has not run on real
  playback. Without GPU processor shaders or the renderer's media device,
  played frames keep only crop, mirror and quarter turns (platform gap).
- The production-only paths have never run: `recoverMacImportWork` in
  `main.cpp`, `DisableProcessWindowsGhosting`, the adapter-change check, the
  real `DataRootRestart` and the real `UpdateService` from About and the tray.
- SystemServices' `WM_POWERBROADCAST` storm coalescing, which the Power area
  listed as remaining, is now covered by `system_services_contracts`.

### Live acceptance still needed

The user runs each of these on the laptop desktop, with explicit consent
where an account, the network or real data is involved.

- **Every area:** a visible `EndfieldHUD.exe` with the real data root,
  persistence and relaunch, five languages, real DPI and multiple monitors.
- **UI Automation:** Narrator, Inspect or Accessibility Insights over every
  group, including the events, the Power entry's live help, the import card
  dialog and the Calendar and Profile edit peers.
- **Real dialogs:**
  - *Theme color*;
  - the center-logo and profile image choosers;
  - the applications mode with a packaged app;
  - the Media Assembly file dialogs;
  - the Shelf relink picker.
- **Drops and clipboard:**
  - Explorer drops and drag feedback over *+ Add App*;
  - real Ctrl+V of an application file;
  - pasting from Word, Edge and Chrome into Notes, and Word reading Notes'
    RTF;
  - browser URL formats on the clipboard;
  - lock and sleep catch-up.
- **Media and audio:**
  - Media Assembly playback with audio and colour edits;
  - moved originals on network shares and non-NTFS volumes;
  - ICC-managed sources and AAC export compared with the Mac;
  - the GSMTC player matrix, lyrics over the network and per-app volume.
- **Account:** China and Global login, and the missing-runtime page on a
  machine without the WebView2 Runtime.
- **Power:** the alert window, the badge under pinned Notes, and the
  supercharge banner at real DPI.
- **Shelf:** real preview handlers, a real hung handler, and OneDrive Files
  On-Demand placeholders.
- **Mac import:** a real export, the real restart, a process kill followed by
  the sweep, and an interrupted activation.
- **Calendar:** a real toast click refreshing Calendar.
- **Updater:** the first `windows-v` release and an in-place update. Signed
  builds and job objects that forbid breakaway are untested.
- **Shell:** a GPU adapter switch; the clock's hover plate and swipe on a
  touchpad.
- **Performance:** the section 9 gate on the laptop desktop
  (`package_perf_gate --desktop`), the large-card timings on a real GPU, and
  Reader, Map and minigame measurements.

### Still open in code

- **UI Automation:**
  - a root focus route (`GetFocus`, AutomationFocusChanged);
  - providers for the navigation entries other than Power;
  - the Power group's name;
  - a text pattern for the projected fields (Value only today);
  - names for the alert's position-editor buttons.
- **Reader:**
  - render PDFs on an owned device (exit fault `0x87A` after a late PDF
    render);
  - no relink action for books.
- **Map:** a geography read or decode failure switches off the whole Map
  owner. The Mac keeps the Map with its pins and shows the load error.
- **Relink ledger:** only the Shelf updates `Migration/relink.json`. Notes,
  Archive and App shortcuts relink their records without it.
- **App shortcuts and Settings:** no packaged-app logos; no app-icon choice
  in Settings.
- **Notes:**
  - the large-card field border is still one slot-sized raster (entry about
    128–222 ms on WARP);
  - the first selection change reloads the frame scene;
  - the Archive single-line inset comes from the shared editor;
  - pasted images are not transcoded on the shared media worker (needs a
    broker hook);
  - Office HTML class style sheets are not applied.
- **Input:** precise touchpad scroll and pinch (a WM_POINTER or
  DirectManipulation path).
- **Mac import:**
  - private folders made before `e38d18d` are unmarked and never swept;
  - no progress is visible during the post-shutdown commit.
- **Shelf:** an in-process property handler that hangs holds the utility
  worker (the HUD thread never waits; cloud and remote items use cached
  properties only).
- **Packaging:**
  - QuickJS-NG's `LICENSE` is not staged;
  - builds are unsigned;
  - there is no "update available" toast.

### Decisions for the user

- Home/End in chunked Notes: move to the visual line or the logical line?
- Mac-imported Reader libraries keep their stored `fontName`. The Mac default
  Georgia cannot be told apart from an explicit choice, so these libraries
  show Georgia instead of the Noto Sans SC/KR default.
- Approve an interaction for relinking imported books inside the Reader.
- Once a certificate exists, may signed builds install updates automatically
  (Mac-style 30 s idle gate)?

Contributor note: the Windows SDK's `rpcndr.h` defines `small` as a macro, so
code must not use it as an identifier.

## Batch 5 (2026-10-11): area and host entries

The stage that owns each change recorded these entries; the host's second
integration pass applied the host-side requests and wrote them down here.
The consolidated batch 5 checkpoint follows from the docs stage. As before,
every run was hidden on the laptop (WARP, hidden windows, synthetic data in
temporary folders and private registry subtrees). No interactive desktop,
real account, real audio, real dialog or UI Automation client was involved.

### Host: the user-reported fixes (`0fcabbc`)

- **One host time.** The Application hands every owner, deadline, utility
  completion, message, pointer and key event and coverage step
  `ModuleRegistry::hostTime`: the sample clamped to the latest time any owner
  was handed, taken again at each hand-off. A nested `WM_CAPTURECHANGED` or an
  owner's clock read inside a callback is never followed by the host's
  earlier sample, which ended the live RCs with "Projection controls require a
  finite monotonic owner clock" and "Event persistence requires finite
  monotonic owner time". Hidden coverage reproduces the live order with two
  probe owners (a later event nested in a deadline, a deadline nested in a
  message). Projection, Notes, Archive, Profile and Work Mode also take an
  earlier time as the latest one seen instead of failing (`dab027e`,
  `3b059ec`).
- **Isolation.** Projection workspace calls run under registry isolation (a
  failure closes Projection and the HUD keeps running), as do area services,
  built-in owner calls outside the routing chains and utility completions,
  which are attributed to the owner that submitted them.
- **Closing on an outside press.** A left press that no source control or
  owner takes closes the HUD with the normal closing animation when it falls
  outside the displayed circle (radius 310 around the design centre, through
  the live projection; `SystemHUDView.mouseDown`). Inside the circle the press
  keeps the HUD open, modal cards and menus unwind first, and a press during
  the entrance closes the HUD once the opening completed. Hidden coverage
  drives this through the real pointer route.
- **Cold start.** `hud.hlsl` is compiled at build time (fxc) and embedded. A
  launch prewarm, posted once the preference record applied, poses the first
  opening offscreen while the HUD is closed (captions, chrome, bound
  appearance and compiled materials rasterized and uploaded, one draw that is
  never presented), applies the ID card, queues Map's geography decode and
  lets owners do their first-update work (`ModuleOwner::prewarm`, used by
  Archive). Opt-in numeric trace: `ENDFIELDHUD_STARTUP_TRACE=1` and
  `watch_session_preview --startup-trace`. Hidden trace on the laptop (WARP):
  renderer stage 312.8 ms → about 130 ms; the first opening's first frame
  68.6 → 29.0 ms and its second frame 17.1 → 5.1 ms. Most of the remaining
  29 ms is Map's first update and upload (about 4.4 + 12.0 ms), Account's
  first upload (about 2.5 ms) and Power's (about 1.2 ms); those owners have no
  prewarm entry point yet.
- Slot: 365 of 368 suites.

### Host integration pass 2

Applied area requests:

- **Map geography** (Reader/Map area): the host worker calls
  `loadMapGeography`. A missing, differing or undecodable dataset is flagged
  instead of thrown inside the Map's isolation, so `EndfieldHUD.exe` keeps the
  Map with its pins and the Mac message.
- **Shelf:** the Shelf receives the shared utility executor right after it
  is created (properties reads and import-ledger writes), so its private
  fallback worker is not used in `EndfieldHUD.exe`. The properties card's
  *Open* waits for the close like *Reveal* (`openShelfReference` revalidates
  the identity; a failure shows the localized message; a quit or forced close
  drops it). The unused Shell preview handler sources
  (`native/shelf_file_preview.*` and `shelf_file_preview_contracts`), the
  process-wide `DisableProcessWindowsGhosting()` call and the startup read of
  the unused Core Animation reveal samples are removed (the packaged
  `reveal-mask.bin` stays for `subsection_mask_contracts`).
- **Work Mode:** the open custom duration field is listed as the focused Edit
  peer with Value; SetValue replaces the draft and keeps the field open.
  Hidden coverage checks the role, keyboard focus, value and SetValue under
  the real root.
- **ID card:** a feed change while the HUD is concealed is applied as its own
  content event (a private notice; the presenter's callback still only
  schedules work), so a profile published after launch does not land in the
  first opening's first frame. The launch prewarm already applies the card
  once at launch.
- **Volume:** the hidden fixture lists five synthetic adjustable apps with
  per-app writers. Coverage checks the three-row (95-point) list, a wheel
  detent of 36 points and that scrolling lays out at most the row entering the
  window.
- **Now Playing:** the shared audio worker publishes
  `AudioApplicationRoute::rendering` (any of the process's sessions is
  active), so a play or pause republishes the applications through
  `audioChanged(audioApplications)` (`audio_session_worker_contracts`).
- **Reader:** a left release or capture loss routed to an inactive Reader no
  longer re-syncs its canvas. Before, a release anywhere over the HUD after a
  settings change, with the Reader hidden, made the next publish throw "Place
  Reader before upload": fatal in hidden coverage, and in production the
  Reader owner was switched off. Hidden coverage now releases its press inside
  the circle after an appearance change and checks that the Reader stays
  enabled. The Library's *Relink…* titles the shared picker "Relink…".
- **Packaging:** both notification identity suites use the long form of
  `%TEMP%` (the CI runner's 8.3 `%TEMP%` made them fail);
  `tests/system_overlay_state_tests.cpp` includes `<cmath>` for GCC; and
  `native/app_notification_identity.cpp` compares the shortcut's target and
  working folder with the identity after expanding both with
  `GetLongPathNameW`, ordinally without case, so an `EndfieldHUD.exe` started
  through an 8.3 path still finds its notification identity.

Found while integrating:

- **Shared raster cache bound.** With the five fixture apps, hidden coverage
  of all owners and areas reached 1,022 retained raster entries, and the next
  *+ Add App* raster failed with "Retained layer cache is full". The count
  bound (`LayerRasterizer::maximumEntries`) is metadata only, and a user's own
  Shelf, Notes and audio apps could reach it in production, where the failing
  owner would be switched off. It is now 4,096 entries; the 256 MB pixel
  budget and the text budgets are unchanged and remain the memory limits.
- **Cold start, measured.** Hidden startup trace (WARP, `watch_session_preview
  --startup-trace`, four runs): the first opening's first frame takes
  27.4–30.6 ms of CPU and the warm opening's 3.2–5.2 ms; the launch prewarm
  takes about 50 ms while the HUD is closed. In the cold frame, Map's first
  update and upload take about 5.6–6.7 + 10 ms, Account's upload about 2 ms
  and Power's 1–1.7 ms; the source shell's own stages differ from the warm
  opening by about 2.5 ms. A host-side Map prewarm through
  `MapPreview::update` with the overlay hidden changed nothing (the module
  presentation at prewarm does not show Map, and activation redoes the work),
  so it was not kept: Map, Account and Power need their own prewarm entry
  points (area work).
- **Video colour in Notes, Archive and Projection** (investigation, no code
  change). On the Mac, a Notes video poster (`NotesMediaFactory.videoPoster`:
  `AVAssetImageGenerator`, preferred transform, 1600 px box) is a
  `kCGColorSpaceCoreMedia709` image. Drawn into an sRGB context it equals
  Media Assembly's preview reading (Core Image, CoreMedia 709) within 1 level
  on all 256 codes of the grey chart, tagged or untagged (128 → 139, 64 → 73,
  16 → 14). Archive thumbnails use the same generator. `AVPlayerLayer`
  playback was not measured. Windows shows the decoded values, so Notes and
  Archive posters, and probably played frames, are up to 11 levels darker.
  Reusing `mediaAssemblyVideoColour` needs a broker design for posters
  (`setVideoPoster`) and played frames (a frame filter per BT.709 movie);
  left to the media and notes areas.

Not applied, with the reason:

- Per-area startup marks: already recorded (`area:<name>` after each area's
  construction).
- Projection host times and the area clock: already in `0fcabbc`; every
  hand-off, the area clock and Projection's key, cancelInteraction and
  dismissal calls take the clamped host time.
- Notes, Archive, Reader, App shortcuts and Shelf ledger calls: done by their
  owners (`c6ee3a1`, `9126a23`, `b75b1c2`, `d61626e`). Notes reconciles at
  construction, which reads one small file that is absent without an import.
- The shared projected editor's rich single-line inset (Archive title and
  date): not a host file; recorded as an area follow-up.

Laptop slot `host` (MSVC x64 Release, full build, all 369 suites): 366 pass.
The three failures are the environmental ones of checkpoint 124
(`projection_composition_contracts` and `projection_workspace_contracts`: no
composition swap chain over SSH; `packet_export_contracts`: no Mac `Sources/`
in the slot). After the last host change, the 28 `application_*`, `map_*`,
`watch_preview` and `endfieldhud_*` suites passed again, including both owner
coverage runs; their host-check minimums are now 117 and 113 (6 new checks
each). `media_assembly_codec_contracts` failed once in an earlier full run of
this pass ("A moved original that is gone asks to be chosen again") and then
passed in the next full run and three times alone; it is reported to the
Media Assembly owner as possibly order- or timing-dependent.

### Media Assembly

1. The framework-derived Highlights/Shadows port was withdrawn (`4b2766e`).
   The black-box fit (121 amount nodes × 17³ lattice, a grey ramp, 240
   held-out pairs, 33³ and 65³ lattices at 4 nodes, structure probes) misses
   the parity bound by 5 levels at best, so the slider applies no change (a
   documented gap). The fixture regenerates byte for byte.
2. Played-frame colour edits are verified through the shared broker (route 1,
   frames edited in playback).
3. Stills of any alpha export on the GPU in float bands with a CPU sticker
   step: a 12 MP heavy edit takes 0.41 s, against 0.80 s on the CPU.
   Translucent and extended-range edges are exact.
4. Embedded ICC profiles (matrix/TRC, including v4 `para`, and grey) and PNG
   gAMA/cHRM chunks are read like Core Image and ImageIO, derived black-box:
   20 profiles and 14 PNG chunk probes are within 1 level on the CPU, GPU and
   preview paths. The WIC transform used before missed by up to 24 levels in
   gamut and by 121 after a LUT.
5. Movie frames are read as CoreMedia 709 (gamma 1.961, v/16 toe) in
   previews, exports and played frames; Windows showed them up to 11 levels
   darker (`c2adc67`).
6. Video exports were upside down (RGB32 input without a default stride).
   Fixed.

Laptop slot: 19 of 19.

### Now Playing: window-title players

- NetEase Cloud Music's Win32 client publishes no SMTC session (GSMTC listed
  0 sessions while it played on the test laptop), so Now Playing composes
  GSMTC with a window-title fallback (`native/now_playing_players.*`,
  `modules/now_playing_players.*`; the Mac `NowPlayingAccessibility`
  counterpart) for NetEase Cloud Music (`cloudmusic.exe`, class
  `OrpheusBrowserHost`), QQ Music and Kugou.
- While Now Playing is presented, WinEvent hooks on the UI thread (title
  change, show, hide, destroy; no timer or polling) trigger a read.
  EnumWindows with a bounded per-activation process cache and
  `GetWindowTextW` (no message to the player) give one session per player
  process (identity: image path; Event Log source by file name).
- Title and artist are split like the source heading (oracle:
  `tools/now_playing_players_reference.py` →
  `tests/fixtures/now-playing-players-source.json`). Playing or paused is
  inferred from title changes, HUD toggles and, when the audio worker
  publishes it, the process's audio activity (`AudioApplicationRoute::
  rendering`, added by the host pass). Play/pause, previous and next are
  posted as `WM_APPCOMMAND`; NetEase covers and lyrics come from the public
  catalog. A playing media session keeps precedence, and a player GSMTC lists
  is not listed twice.
- Hidden coverage drives the path through a synthetic desktop (Now Playing
  area: 140 checks). `now_playing_probe` pumps messages, records window-title
  players with their gaps (timeline, progress, seek; skips verified by a
  following title change) and prints raw player windows with
  `--live --windows`. `now_playing_players_contracts` (157 checks),
  `now_playing_acceptance_contracts` (323) and `now_playing_probe_matrix` use
  synthetic windows only.
- Slot `nowplaying`: 28 of 28 (all `now_playing_*` suites through
  `now_playing_all`, `application_owner_coverage`,
  `application_session_end_coverage`, `application_registry_contracts`,
  `application_executable_contracts`, `endfieldhud_package_contracts`).

### Account Linking

`b0fc68d` gives the login windows the Mac panel geometry. The consent and
missing-runtime page has 860 × 650-point content, a minimum outer size of
680 × 540, close and maximize but no minimize, and is centred on the HUD
monitor's work area. Sign-in popups have 820 × 640 content, a minimum of
680 × 440, and are centred on the login window; the minimum follows the
per-monitor DPI. File drags over the Account page are refused, as on the Mac.
`d6bafad` makes `hypergryph_account_owner_contracts` print its startup times
(laptop: about 5–6.5 ms warm construction, 0.01 ms to open, about 10 ms for
the first raster of the Account page when it is selected). Suites: webview
489 checks, area 77, owner 71. Slot: 17 of 17, including
`application_owner_coverage`.

### Power

1. The floating charge alert has its own UI Automation root
   (`native/charge_alert_automation.*`): the indicator as static text with
   its label, plus the Discard and Confirm buttons with the Mac labels and
   tooltip help while editing. Invoke runs from the alert window, and the
   editor buttons show the system tooltips.
2. The HUD charge badge is rasterized once for the core plane's resting
   position (design scale × HUD scale × 0.82 × DPI scale) and drawn again only
   on size, DPI or HUD scale changes, never by opening or closing frames.
   Each removed re-draw cost 4 to 13 ms of UI-thread time on the test laptop;
   the startup raster takes about 25 ms (`charge_alert_probe
   --raster-timing`). The Power area applies the badge content once at
   startup.
3. The `powerCharge` UIA group carries the Power caption.

Commits `454440a` and `3d74603`. Final slot: 18 of 18.

### Personal Profile

- Profile area coverage: 6 steps, 158 checks. The raster is stable in cycles
  1 and 2 (770 entries, 98,327,224 bytes), and lifetime is checked with
  nonzero stopwatch time.
- The Profile and Work Mode scenes take an earlier owner time as the latest
  time seen (regressions in the `profile_scene`, `work_mode_scene`,
  `work_mode_preview` and `profile_owner` contracts).
- A large avatar's working copy is made on the worker, and portrait crops use
  an exact premultiplied-linear table. A 4000 × 3000 photo now costs 17 ms of
  UI-thread time instead of about 460 ms; a page crop takes 14 ms instead of
  90 ms.
- New opt-in diagnostic: `profile_cold_start_probe <hud.hlsl>
  <resources/profile> <id_card-native-captions.json>` (prints timings only).
- Every owner with projected editors now answers `ModuleOwner::cursorAt`
  (Notes, Archive, Calendar, Work Mode, Personal Profile, App shortcuts), so
  the I-beam shows over open fields.

### App shortcuts

1. *+ Add App* keeps its saved cards in six retained slots. A wheel step only
   moves them: 1.7 ms per small step and 2.1 ms per 36-point notch, down from
   about 25 ms of full-canvas rebuild per step. Only a card entering the
   window draws its own name, original name and icon again (`72e59e9`).
2. Relinks of imported Mac shortcuts update `Migration/relink.json` through
   `reconcileMacRelinks`. A packaged-app relink resolves with its
   `shell:AppsFolder` target (`b75b1c2`).
3. Synthetic key input in tests and coverage states its modifiers and never
   reads the keyboard state (`c57089d`).
4. The *+ Add App* canvas owner costs 0.04 ms to construct; its first frame
   takes about 36–40 ms of CPU (the canvas raster, done lazily on first view
   as on the Mac).

### Mac import: relink-ledger owner API and import card focus events (`74cc045`)

- `resolveMacRelink`, `dismissMacRelink`, `reconcileMacRelinks` and
  `macRelinkMediaCheck` work by store and record id. Nothing is written
  without an import or a matching row; writers are serialized in-process, a
  concurrent replacement is re-applied a bounded number of times, decisions
  are final, and Archive attachments are named by their Mac locator, never
  by position.
- Packaged `shell:AppsFolder` targets are accepted for `appShortcuts.app`
  rows.
- The import card raises AutomationFocusChanged on its focus moves
  (appearing, Tab, Left/Right, press, SetFocus). SetFocus routes to the
  card's focus, and the dialog is listed on the card's first frame.
- Laptop: `mac_import_relink_contracts` 95, `mac_import_automation_contracts`
  82, `app_shortcut_relink_contracts` 17 (packaged branch); the
  `shelf_services`, `mac_import`, flow, work, native, reconcile, plist,
  settings and tool suites pass; `application_owner_coverage` passes with
  1,608 macImport checks.
- The owners now use it: App shortcuts and the Shelf (`b75b1c2`, `d61626e`),
  Notes media and Archive attachments after their record writes and on load
  (`c6ee3a1`, `modules/notes_relink_ledger`), and Reader books (`9126a23`).

### System providers (`918502b`)

Clipboard history identity matches `ClipboardPayload.matches`. Files compare
by `standardizedFileURL`, done lexically: separators, `.`, `..` and the
directory hint; Foundation keeps `/./` for a root followed only by dots;
drive letters are folded; verbatim and device paths stay literal. Text and
link originals compare by canonical equivalence through ICU NFC. Mac oracle
`tools/clipboard_matches_reference.sh`: 87 paths, 9 lists, 43 string pairs.
`clipboard_matches_contracts` runs 23,510 checks; `system_services_contracts`
and the `systemProviders` coverage capture a respelled `CF_HDROP` selection
and an NFD string through the real service. `event_log_owner_contracts` now
expects the clamped owner clock of `dadf151`.

### Reader and Map (`9126a23`)

- Reader PDF pages render through `IPdfRendererNative` on a Direct3D device
  the document owns and releases with it. `reader_pdf_exit_contracts`
  reproduced the `0x87A`/2170 exit fault with the old `RenderToStreamAsync`
  path; its hidden children now exit 0.
- Map: `loadMapGeography` loads `Terrain.bin` and `Countries.bin`
  separately. A failed dataset keeps the Map with its pins and shows the Mac
  message; `EndfieldHUD.exe` uses it since the host pass. Tests:
  `map_geography_load_contracts` and `map_preview_contracts`.
- Reader Library *Relink…* for imported Mac books and unavailable Windows
  books, over the Open picker route. It keeps the id, title, progress and
  bookmarks, and resolves, dismisses and reconciles `Migration/relink.json`
  rows through the import ledger API. Imported libraries keep their stored
  `fontName` (Georgia). Tests: `reader_localization`, `reader_preview`,
  `reader_owner` and `reader_native` contracts.
- Final slot: 46 of 46 local suites plus `application_owner_coverage`.

### Notes, Projection and Archive

1. Owner times in Projection, Notes and Archive: an earlier time is taken as
   the latest time seen, never fatal. Six paths handed raw times: the
   workspace demand passed the raw time to the preview's frame demand; the
   host called the media binding and the import receivers directly; key and
   cancelInteraction took raw `now()`; the UIA clock; the binding passed the
   raw time on; the preview passed the raw time to its scenes. Regressions in
   the live order are in the projection preview, binding and scene suites and
   in hidden coverage (an earlier-clock UIA invoke on Projection, `0deb3df`).
2. Home/End move along the caret's visual line and Ctrl+Home/Ctrl+End to the
   document, the Windows equivalent of Cmd+Left/Right and Cmd+Up/Down
   (`0aa6019`).
3. The Mac import relink ledger is kept up to date after Notes and Archive
   relinks, deletions and loads (`modules/notes_relink_ledger`, `c6ee3a1`).
4. Large Notes cards draw their border and field border as GPU ring meshes,
   and a selection keeps the frame. On WARP at 1600 × 1000 points and 2 px
   per point, editing entry went from 242 to 76 ms and re-entry from 145 to
   71 ms (`e441cb2`).
5. Slot `notes`: full build, 65 of 65 owned suites including
   `application_owner_coverage`.

### Temporary File Shelf (user reports, stage 5)

- **Removal glitch.** The collection reveal (removal, Clear, additions) drew
  the measured Core Animation path of `HUDSubsectionTransition.reveal`.
  Between the collapsed first and the full last keyframe, Core Animation
  drops zero-length segments; from phase .68 its path is a mismatched
  five-subpath shape, and curved wedges covered cards that were already
  revealed. The Shelf now draws the authored shutter keyframes
  (`shelfRevealShutter`) at the Core Animation key times with per-segment
  ease-out, which matches Core Animation between the well-formed keyframes
  (`9b91c0a`). `shelf_removal_frames_contracts` renders an × removal frame by
  frame through the production owner, published like `publishNative`: no
  region is covered again and the frame is exactly settled at .26 s. The
  Clipboard reveal and the Activity sort mask use samples with the same
  topology.
- **Properties card** (`d61626e`, `200e513`). The eye button, double-click
  and Space open a `NotesRetainedMenu`-style card on the Shelf plane (it
  follows the tilt; .16 s fade; modal for pointer, keys and UIA; follows the
  selection). Rows: Name, Type, Size or Contains (direct children up to
  10,000; never cloud or remote folders), Location (middle-shortened by
  measured width, keeping whole trailing folders), Created and Modified (the
  short date when the long one does not fit), and Dimensions and Duration
  through `IPropertyStore` (cached properties only for cloud placeholders and
  remote volumes). Open (after the HUD closes, `openShelfReference`) and
  Reveal, in five languages. Properties are read on the utility executor. The
  preview thread and its containment were removed.
- **Import ledger.** Shelf relink, remove and clear call `resolveMacRelink`,
  `dismissMacRelink` and `reconcileMacRelinks` on the utility worker after
  the Shelf record write; nothing reads the ledger on the HUD thread.
- Verified on the laptop: `shelf_removal_frames`, `shelf_properties` 28,
  `shelf_services` 568, `shelf_scene` 115,987, `shelf_presentation` 1,754,
  `shelf_file_picker` 130, all other Shelf suites,
  `application_owner_coverage` and `application_session_end_coverage`. Not
  verified: a real GPU and desktop, real media property handlers, OneDrive
  placeholders, and Open in `EndfieldHUD.exe` (wired by the host pass).

### Volume

The app list now shows at most three rows. This is an explicit user change:
the source shows two rows in a 64-point viewport, and three rows give 95
points by the same rows × 31 + 2 rule. A longer list scrolls by wheel, by
dragging the rows outside their sliders, or by dragging the scrollbar thumb
or track, all within the source clamp. While a per-app status message is
shown (`perAppStatus` or routing stopped), the list keeps the source two-row
viewport so the status line at y = 300 stays clear. App rows are now retained
LayerScenes keyed by process identity: the visible rows plus one on each
side, at most 7. A scroll or drag only moves them, with no module reload and
no caption layout; only a row crossing the viewport edge repaints its clipped
highlight. Settled frames allocate, rasterize and upload nothing, and icon
requests follow the same row window. The Mac oracle comparison
(`volume_reference_tests`) uses `VolumeLayout::source()`. Suites:
`volume_scene_contracts` (list layout, native scrolling) and
`volume_preview_contracts` (owner scrolling); the host pass adds the list to
`EndfieldHUD.exe` coverage (`edbe0b7`).

### Packaging: section 9 gate and first release notes (`2d2f1e2`)

- `package_perf_gate --desktop`, run from a terminal on the laptop's desktop
  next to `EndfieldHUD.exe`, measures that build against a temporary
  `--data-root`: closed idle, open idle with ambient on and off, 100
  open/close cycles, and the cold and warm openings from the app's own
  `ENDFIELDHUD_STARTUP_TRACE=1` trace (first frame, p50/p95 interval, late
  frames, frame CPU, slowest startup stages and owners). It writes
  `Release\performance-reports\<time>\performance-gate.json` and a readable
  `performance-gate.md`. It asks before showing the HUD, refuses without a
  visible desktop, stops on Ctrl+C with a partial report, keeps the app in a
  kill-on-close job, and stops on the app's fatal record. Over SSH the slot's
  `EndfieldHUD.exe` stops with "Cannot locate the current display", so no
  Windows numbers exist yet.
- `windows/packaging/release-notes-template.md` is now the first Windows
  release (v1.2.0 build 18 baseline), including moving from a Mac and the
  differences from macOS.
- CI run 38116894941 (`e17f3af`, windows-2025: ICU 72.1, Unicode 15.1,
  tzdata 2022g): 343 of 348 Windows suites passed. The installer, release and
  both notification identity suites failed on the runner's 8.3 `%TEMP%`
  (reproduced on the laptop; packaging fixed in `2d2f1e2`, the identity
  suites by the host pass). `packaging_watch_runtime_staged` failed on cp1252
  (fixed with `-X utf8`). The portable job stopped at a missing `<cmath>` in
  `tests/system_overlay_state_tests.cpp` (fixed by the host pass).
- Suites: `packaging_perf_gate_contracts` (56 checks),
  `packaging_perf_probe_contracts`, `packaging_perf_gate_tool`,
  `packaging_perf_gate_render`, `endfieldhud_release_contracts`.

## Checkpoint 128 (batch 5): consolidated status

This entry sums up batch 5: every commit after checkpoint 124's record
(`1062ffd`), including the area and host entries above and the commits those
entries leave out. Code is at `58962f4`; `28b3e40` and this entry change
documentation only. Nothing from batch 5 is pushed.

Batch 5 started from problems the user reported after running an
`EndfieldHUD.exe` build on the laptop desktop. No fix has been confirmed on
the desktop yet. All evidence below comes from hidden runs on the laptop
(hidden windows, WARP or the hardware GPU offscreen, synthetic data,
temporary folders, private registry subtrees) and from local clang builds on
the Mac. No UI Automation client, network service, real account, real dialog
or real audio session was involved.

### The user's reports

| Report | Change | Hidden evidence | Still to confirm on the desktop |
| --- | --- | --- | --- |
| Projection crashed on opening (Media Assembly no longer did) | Owner times arrived out of order: an event nested in a host callback (the `WM_CAPTURECHANGED` after a Projection release, an owner reading the clock inside a callback) gave Projection a later time, then the host's earlier sample failed a strict check ("Projection controls require a finite monotonic owner clock"). The host now hands every owner one non-decreasing time (`ModuleRegistry::hostTime`, `0fcabbc`). Projection, Notes, Archive, Profile and Work Mode take an earlier time as the latest seen (`dab027e`, `3b059ec`), and Projection calls run isolated. The earlier Media Assembly crash was the same class in the Event Log store (`dadf151`). | `application_registry_contracts` replays both live orders; the Projection preview, media binding, controls scene and media scene suites; hidden coverage audits every area for an earlier time (none in a whole run) and invokes Projection through UIA with an earlier clock. | Open and use Projection (keys, UIA, media, dismissal). `projection_workspace_contracts`, which holds the same regression, needs a desktop session. |
| The HUD did not close on presses at the sides | `SystemHUDView.mouseDown` ported: a left press that no source control or owner takes closes the HUD with the normal animation when it falls outside the circle of radius 310 around the design centre, through the live projection (`0fcabbc`). Inside the circle nothing happens; during the entrance the close waits for the opening. | 9 host checks in both coverage runs (sides, corners, the closing animation, the queued close, reopening, an inside press, the quit card keeping the press). | Sides, corners, the taskbar area, another monitor (closed through *Close when focus lost*). |
| A glitch when removing Shelf items | The reveal drew Core Animation's measured path, which sweeps wedges over cards between its degenerate keyframes. Removal, Clear and additions now draw the authored shutter keyframes at Core Animation's key times (`9b91c0a`). | `shelf_removal_frames_contracts` renders an × removal frame by frame through the production owner: no region is covered again, and the frame settles exactly at .26 s. | The real GPU. Clipboard's reveal and Activity's sort transition use samples of the same topology and are unchanged. |
| Now Playing did not recognize the song | NetEase Cloud Music's Win32 client publishes no media session (GSMTC listed none while it played on the laptop). Now Playing also reads the main window titles of NetEase Cloud Music, QQ Music and Kugou, like the Mac's accessibility fallback (`597df08`, `70ebabb`, `e8f4b1f`; process audio activity from host pass 2). | `now_playing_players_contracts` (157), `now_playing_acceptance_contracts` (323), `now_playing_probe_matrix`, 140 Now Playing coverage checks over a synthetic desktop. | Recognition, cover, lyrics and `WM_APPCOMMAND` transport in the user's player; `now_playing_probe --live --windows` for QQ Music and Kugou. Another player that publishes no session is still not found. |
| Show a Shelf item's properties instead of a preview | A HUD secondary-menu card (name, type, size or item count, location, dates, dimensions, duration; Open and Reveal) read on the utility executor (`d61626e`, `200e513`; the shared executor and Open after the HUD closes from host pass 2). The Shell preview popup, its thread and its handler code were removed. | `shelf_properties_contracts`, `shelf_presentation_contracts` (1,754), `shelf_services_contracts` (568), `shelf_file_picker_contracts` (130), the `fileShelfCoverage` area. | Open in `EndfieldHUD.exe` (a production-only path), real media types, OneDrive placeholders, SMB shares. |
| Volume scrolling stutters with more than two apps; show three | App rows are retained scenes that a scroll only moves; at most three rows (95 points), scrolled by wheel, row drag or scrollbar drag (`edbe0b7`). This deliberately differs from the Mac's two rows. | `volume_scene_contracts`, `volume_preview_contracts`; coverage with five synthetic apps (95-point list, 36-point detent, at most 3 text layouts while scrolling). | Wheel, touchpad and drag with real audio sessions. |
| The cold start is laggy | `hud.hlsl` compiled at build time and embedded; a launch prewarm poses and draws the first opening offscreen while the HUD is closed (`0fcabbc`); the charge badge rasterizes once for its resting size (`3d74603`); a large avatar's crop working copy is made on the worker (`f72c590`); an ID card change while closed is applied before the next opening (host pass 2). | Hidden traces: renderer start-up 312.8 → about 130 ms; launch to first frame 1,076 → about 920 ms; the first opening's first frame 68.6 → 29.0 ms of CPU (27–31 ms in four later WARP runs) and its second 17.1 → 5.1 ms; a warm opening's first frame 3–5 ms. | A production trace (`ENDFIELDHUD_STARTUP_TRACE=1`) or `package_perf_gate --desktop`. Unchanged: about 160–190 ms before the first startup stage, about 150 ms of area construction, and Map (about 16 ms), Account (about 2 ms) and Power (1–1.7 ms) work in the cold first frame. |

### Test runs

| Run | Result |
| --- | --- |
| Host pass 2, full MSVC x64 Release build | 366 of 369 suites. The three failures are the environmental ones of checkpoints 123 and 124: `projection_composition_contracts` and `projection_workspace_contracts` (no composition swap chain over SSH) and `packet_export_contracts` (no Mac `Sources/` in the slot). After the last code change (`58962f4`), 28 suites passed again: the `application_*`, `map_*` and `endfieldhud_*` suites and `watch_preview_argument_contracts`. |
| `application_owner_coverage` | 54 module results and 19 lifecycle checks (both exact), at least 117 host checks (enforced minimum; 81 at checkpoint 124), 14 areas constructed. |
| `application_session_end_coverage` | 15 lifecycle checks, at least 113 host checks (78 at checkpoint 124). |
| Earlier host slots | Host pass 1 (`fff7dea`): 356 of 359. The user-reported host fixes (`0fcabbc`): 365 of 368. The same three environmental failures. |
| Area slots (committed tree plus the area's files) | Media Assembly 19/19, Now Playing 28/28, Account 17/17, Power 18/18, Personal Profile 23/23, App shortcuts 19/19, Mac import 13/13, System providers 29/29, Reader/Map/Calendar/Minigame 46/46, Notes/Archive/Projection 65/65, Shelf 17/17, Volume 17/17, packaging 14/14. |
| Mac, clang | The portable `now_playing_*` suites (ICU from Homebrew), `clipboard_matches_contracts` against ICU 78 under ASan/UBSan, `mac_import_relink_contracts`, `packaging_perf_gate_contracts` (56, ASan/UBSan), the portable Volume suite (142 checks), `shelf_scene_contracts`, `shelf_presentation_contracts`, `reader_localization_contracts` and `source_watch_session_contracts`; the no-ICU portable configure generates. |
| GitHub Actions | One run, on `e17f3af` before batch 5's fixes (run 38116894941, `windows-2025`: ICU 72.1, Unicode 15.1, tzdata 2022g): 343 of 348 Windows suites. Its failures (the runner's 8.3 `%TEMP%`, a cp1252 read, a missing `<cmath>`) are fixed in local commits that no run has confirmed. |
| Intermittent | `media_assembly_codec_contracts` failed once in a full host pass 2 run ("A moved original that is gone asks to be chosen again"), then passed in the next full run and three times alone. Unexplained. |

### What `EndfieldHUD.exe` contains now

Changes since checkpoint 124:

- **Same owners and areas:** 15 built-in owners, the quit card, the desktop
  clock's automation owner, 10 production module areas and 4 coverage-only
  areas.
- **Time and failures:**
  - one non-decreasing host time at every owner hand-off;
  - isolation for Projection workspace calls, area services, built-in calls
    outside the routing chains and utility completions (run under the owner
    that submitted them);
  - the Event Log store clamps out-of-order owner times, and a failed save
    shows its status and retries at the next wake (`dadf151`, `d8bf2c9`);
  - a fatal failure writes the exception type, a bounded path-redacted
    message, the HRESULT and the active host stage to
    `Diagnostics/last-fatal.txt` in the data root (`d8bf2c9`); the section 9
    gate reads it.
- **Pointer:** the outside-press close; `ModuleOwner::cursorAt` with one
  `WM_SETCURSOR` path (`fff7dea`). The I-beam shows over the open Work Mode
  duration field, the open profile field and the open *+ Add App* name field.
  Notes, Archive and Calendar fields keep the source cursor (see the
  corrections below).
- **UI Automation:**
  - the root's and groups' `GetFocus` return the fragment that has keyboard
    focus;
  - `HudButtonAutomation` lists the source navigation (bound entries valued
    Selected or Not selected, the red power button, the profile card, Scroll
    modules up and down) and the clock's buttons (the selected style's Value
    is "Selected");
  - the Power group is captioned Power;
  - Work Mode's open duration field is a focused Edit peer with SetValue;
  - the import card raises AutomationFocusChanged and accepts SetFocus;
  - the charge alert window has its own UIA root.
- **Startup:** embedded `hud.hlsl` bytecode (fxc at build time; without fxc,
  or when the shipped `hud.hlsl` differs, the shader compiles at run time);
  the launch prewarm and `ModuleOwner::prewarm`, which only Archive
  implements; the opt-in startup trace with per-area construction stages.
- **Area work wired by the host:**
  - Map geography through `loadMapGeography`;
  - the Shelf on the shared UtilityExecutor, and the properties card's Open
    after the HUD closes;
  - an ID card change while concealed applied as its own content event;
  - `AudioApplicationRoute::rendering` from the audio worker;
  - a release reaching the inactive Reader is ignored (it used to switch the
    Reader off with "Place Reader before upload");
  - the Library's *Relink…* titles the picker.
- **Bounds:** `LayerRasterizer::maximumEntries` went from 1,024 to 4,096
  entries. The 256 MB pixel budget is unchanged.
- **Removed:** `DisableProcessWindowsGhosting`, the Shell preview handler code
  and its suite (`native/shelf_file_preview.*`), the Shelf preview thread and
  the startup read of the unused reveal samples.
- **Package:** no new resources, vendored code or fonts. QuickJS-NG's
  `LICENSE` is still not staged (`CREDITS.md`).

### Verified, by suite

| Area | Suites | Main evidence |
| --- | --- | --- |
| Host | `application_registry_contracts`, both coverage runs, `overlay_host_contracts`, `source_watch_session_contracts`, `audio_session_worker_contracts`, `layer_raster_contracts`, the two notification identity suites | Both live crash orders. No earlier time handed to any area in a whole coverage run. Outside-press close. The cursor route and the hidden-window cursor contract. `GetFocus` on an open Notes editor. Navigation buttons, hit test and a UIA scroll press. Work Mode's Edit peer and SetValue. The Volume list. The Reader stays enabled after a release. Embedded shaders reported by the renderer. |
| Projection, Notes, Archive | Projection preview, media binding, controls scene and media scene suites; `notes_scene_contracts`, `notes_workspace_owner_contracts`, `notes_text_navigation_contracts`, `notes_relink_ledger_contracts`, `archive_state_contracts`, `archive_preview_contracts` | Earlier owner times taken as the latest. Visual-line Home/End in both editor kinds. Ledger resolve, dismiss and reconcile. Large-card GPU rings: editing entry 242 → 76 ms on WARP. |
| Media Assembly | the 14 `media_assembly_*` suites | 20 ICC profiles and 14 PNG chunk probes within 1 level of Core Image on the CPU, GPU and preview paths. Movie frames within 2 levels of the Mac preview (11 before). Exported video row order. GPU stills exact against the CPU (12 MP: 0.41 s against 0.80 s). The Highlights/Shadows fit reproduces its fixture byte for byte. |
| Now Playing | `now_playing_players_contracts` (157), `now_playing_acceptance_contracts` (323), `now_playing_probe_matrix`, all `now_playing_*` through `now_playing_all` | The Mac-oracle title split on 35 headings. Composite precedence. The synthetic desktop in coverage (140 checks). |
| Account | `hypergryph_account_webview_contracts` (489), `hypergryph_account_owner_contracts` | Mac panel geometry and per-monitor DPI. Drags refused. Startup costs printed, not asserted. |
| Power | `charge_indicator_panel_contracts`, `charge_badge_preview_contracts`, `power_area_contracts` | The alert's UIA root and tooltips. No re-raster while opening or closing at 2× (counters). The Power caption. |
| Personal Profile, Work Mode | `profile_image_contracts`, `id_card_artwork_contracts` (Mac oracle, unchanged), `profile_scene_contracts`, `work_mode_scene_contracts`, `work_mode_preview_contracts`, `profile_owner_contracts`, `work_mode_automation_contracts` | The working copy made on the worker (a 4000 × 3000 avatar: about 460 → 17 ms of UI-thread time on WARP). Out-of-order times. The I-beam rect. 158 profile coverage checks. |
| App shortcuts | `app_shortcut_preview_contracts`, `app_shortcut_scene_contracts`, `app_shortcut_presentation_contracts`, `app_shortcut_relink_contracts` (17) | A wheel step 1.7–2.5 ms, small steps with no raster (about 25 ms before). The handoff mask in the host's frame order. A packaged relink resolves. |
| Mac import | `mac_import_relink_contracts` (95), `mac_import_automation_contracts` (82), the other `mac_import_*` suites | The owner API under concurrency, a mid-call replacement and refusals. Focus events. 1,608 macImport coverage checks. |
| System providers | `clipboard_matches_contracts` (23,510), `system_services_contracts` (11,375), `event_log_owner_contracts` (46) | 87 Mac-oracle paths, 9 lists, 43 string pairs. The clamped Event Log owner clock. |
| Reader, Map | `reader_pdf_exit_contracts` (19), `map_geography_load_contracts`, `map_preview_contracts` (82), `reader_preview_contracts` (6,223), `reader_owner_contracts` (57), `reader_localization_contracts`, `reader_native_contracts` | Exit fault `0x87A` reproduced with the old renderer, exit 0 on the owned device. Each geography dataset fails alone. A relink keeps the book's id, title and bookmarks and updates the ledger. |
| File Shelf | `shelf_removal_frames_contracts`, `shelf_scene_contracts` (115,987), `shelf_presentation_contracts` (1,754), `shelf_properties_contracts`, `shelf_services_contracts` (568), `shelf_file_picker_contracts` (130) | Removal frames. Properties of synthetic files (PNG dimensions, WAV duration). The modal card. The ledger on the worker. |
| Volume | `volume_scene_contracts`, `volume_preview_contracts`, `volume_preview_icon_contracts` | No module reload or caption layout on scroll. Settled frames allocate, rasterize and upload nothing. |
| Packaging | `packaging_perf_gate_contracts` (56), `packaging_perf_probe_contracts`, `packaging_perf_gate_tool`, `packaging_perf_gate_render`, `endfieldhud_release_contracts`, `packaging_installer_contracts` (385 with an 8.3 `%TEMP%`) | Opening analysis and the refusal over SSH. The first-release notes rendered. |

### Corrections to the entries above

- The batch 5 "Personal Profile" entry says every owner with projected
  editors answers `ModuleOwner::cursorAt`, naming Notes, Archive and Calendar.
  Only Work Mode, Personal Profile and App shortcuts do. The host adapters for
  Notes, Archive and Calendar (`app/application_modules.cpp`) forward a
  preview's `textCursorAt` only once the preview declares it, and none of the
  three does yet, so their fields show the source cursor. WINDOWS-MIGRATION.md
  is corrected too.
- The batch 5 entries leave out these commits:
  - host pass 1 (`fff7dea`): the cursor route, `GetFocus`, the navigation and
    clock buttons (the clock's selected style now carries the Mac Value
    "Selected" instead of help text), and a bounded update toggle label;
  - `dadf151`: the Event Log store clamps out-of-order owner times. Before,
    a Media Assembly edit or a Work Mode transition ended the HUD;
  - `d8bf2c9`: the fatal record and the Event Log save retry;
  - `9d85cd6` and `666505a`: the profile field's I-beam, Work Mode's Edit
    peer builder, and Ctrl+X/C/V in the profile and duration fields;
  - `d29fd94`: App shortcut relink rows, packaged-app logos and the name
    field's I-beam;
  - `c5650db` and `b724f25`: the account menu's focus, toggle state and status
    in UIA, keyboard navigation and DPI changes in the login window, the Mac
    auth button art, the wallet's recovery countdowns as UIA value, and
    accent-lit account controls;
  - the validation fixes `a53cc5d`, `ef4c92b`, `40578fa`, `e07cb2a`,
    `e17f3af` and `c57089d` (and Work Mode in `666505a`): synthetic keys in
    the Map, Account, Profile, Work Mode, Activity, Clipboard, Event Log,
    Shelf and *+ Add App* tests state their modifiers. Before, the tests read
    the live keyboard, so a key held on the laptop made checks fail.
- Checkpoint 124's "Still open in code" items now done: the root focus route
  and focus events (import card), the navigation entries in UIA, the Power
  group's name, names for the alert's editor buttons, Reader PDFs on an owned
  device, a Reader relink action, keeping the Map when its geography fails,
  the relink ledger in Notes, Archive and App shortcuts (and Reader),
  packaged-app logos, the large Notes card's field border raster and its
  frame reload on the first selection change. The
  app-icon choice is now a recorded platform gap: it changes the tray icon,
  and the executable's own icon cannot change at run time.
- Checkpoint 124's decisions: Home/End (visual line), Georgia for imported
  Reader libraries and relinking from the Library are settled; automatic
  updates for signed builds are still open.
- Checkpoint 124 said no visible run of `EndfieldHUD.exe` had been recorded.
  The user has since run a build on the laptop desktop; this batch answers
  that run's reports.

### Live acceptance still needed

Checkpoint 124's list still applies. Batch 5 adds:

- **The reports above:**
  - open and use Projection;
  - presses at the sides, corners, taskbar and on another monitor;
  - Shelf removal, Clear and additions on the real GPU;
  - the properties card (Open, Reveal, Esc, real photos and videos, a
    OneDrive placeholder, an SMB share);
  - Now Playing with the user's player, and `now_playing_probe --live
    --windows` for QQ Music and Kugou;
  - Volume with four or more real audio apps (wheel, touchpad, drag);
  - the cold start, through `ENDFIELDHUD_STARTUP_TRACE=1` or
    `package_perf_gate --desktop`.
- **Pointer and accessibility:**
  - the visible I-beam over the three fields that answer it;
  - Narrator or Inspect over the navigation group, `GetFocus`, the clock's
    Value, the charge alert's root, Work Mode's duration Edit and the import
    card's focus events.
- **Media Assembly:** Display P3 photos, movie brightness (paused, playing,
  exported), export orientation, PNG gamma, a large transparent export.
- **Reader and Map:** quitting right after a PDF render; relinking with a
  synthetic Mac export; damaged geography in a development package copy.
- **Other areas:**
  - Account: login window geometry on mixed-DPI monitors; the no-drop cursor
    over the Account page;
  - Power: the alert's UIA root, the editor tooltips, opening and closing
    smoothness at several display and HUD scales;
  - Profile: a large phone photo as the avatar, slider drags;
  - *+ Add App*: scrolling a long list (coverage saves at most 3 apps);
  - Clipboard: respelled selections from Explorer and VS Code, NFC and NFD
    text;
  - Notes: the GPU card border at strong tilt and real DPI; Home/End with an
    IME.
- **Packaging:** GitHub Actions after a push; the first `windows-v` release
  and an in-place update.

### Still open in code

- **Cursor:** `textCursorAt` in the Notes, Archive and Calendar previews.
- **UI Automation:**
  - a text pattern for projected fields;
  - a focus announcement when the Shelf properties card opens;
  - no UIA event has been observed by a listening client.
- **Cold start:**
  - prewarm entry points for Map (about 16 ms), Account (about 2 ms) and
    Power (1–1.7 ms) of the 27–31 ms cold first frame;
  - Map verifies and expands its player images on the UI thread at launch
    (not measured);
  - about 160–190 ms before the first startup stage (COM and DLL loading)
    and about 150 ms of area construction were measured, not changed.
- **Video colour:** Notes and Archive posters (measured on the Mac: grey 128
  shows as 139), and probably played frames in Notes, Archive and
  Projection, need the CoreMedia 709 reading through the media broker.
- **Reveal masks:** Clipboard's reveal and Activity's sort transition use
  samples of the Shelf's old topology (a decision for the user, below).
- **Notes:**
  - leaving the editor on a card larger than 1024 × 1024 points re-rasters
    about 46 text lines (59 ms on WARP);
  - the relink ledger is written on the UI thread;
  - the Archive single-line inset comes from the shared editor;
  - pasted images are not transcoded on the shared media worker;
  - Office HTML class style sheets are not applied.
- **Personal Profile:** the page portrait crop runs on the UI thread (about
  14 ms per change, slider drags included); decoded avatars up to 4096 px
  stay in the two-entry cache.
- **Source shell:** the ENDFIELD INDUSTRIES logo's click flicker and the
  clickable scroll indicators are not ported; presses there are absorbed
  without an action.
- **Input:** precise touchpad scroll and pinch.
- **Mac import:** unmarked private folders from before `e38d18d`; no progress
  during the post-shutdown commit.
- **Packaging:** QuickJS-NG's `LICENSE` is not staged; builds are unsigned;
  no "update available" toast.

### Decisions for the user

- Clipboard's reveal and Activity's sort transition: draw the authored
  keyframes there too, as the Shelf now does? The Mac shows Core Animation's
  interpolation, which most likely has the same wedges.
- Once a certificate exists, may signed builds install updates automatically
  (Mac-style 30 s idle gate)?
- If NetEase Cloud Music ignores `WM_APPCOMMAND` in the live run, a follow-up
  chooses another delivery path from that evidence.
- Settled in batch 5: Volume shows three app rows (the user's change); the
  Shelf preview became the properties card; Home/End follow the visual line;
  imported Reader libraries keep Georgia; books are relinked from the Library;
  Highlights/Shadows is derived black-box only and stays a gap.
