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
original icon, tray menu and Ctrl+` reopening. Other module bodies and the
production application owner remain unfinished.
It does **not** yet produce a complete runnable HUD or a release candidate.

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
