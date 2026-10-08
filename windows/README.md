# Windows rebuild

The previous Windows preview was rejected and removed from this build. This is
a fresh port of the **macOS v1.2.0 build 18 application**, pinned in
[`source-authority.json`](source-authority.json). Current macOS `Sources/` and
`Resources/` define the UI and behavior. The raw game scene alone is not the app.

The current target builds portable scene, motion and data components, a native
D3D11/DirectComposition renderer, retained source material and label adapters,
an event-driven window host, and Windows battery, clipboard and master-volume
service foundations. An explicit development preview now combines the source
shell, native captions/icons, clock and custom cursor. Module bodies are not
installed in that preview. It does **not** yet produce a complete runnable HUD
or a release candidate.

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
Hidden windows schedule no rendering work. All 34 Windows test suites pass on the laptop; the portable build passes
26 suites. Display selection has a read-only Windows service and a
portable policy matching the Mac fallback behavior, with no polling. Actual
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
frame. Chinese typing/selection were subsequently verified by the user.

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
