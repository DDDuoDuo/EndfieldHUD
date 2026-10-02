# Stable desktop behavior in the new visual shell

The integration baseline is `a8770680044c4f7b664c6c8adecc0c02bed02a2c`
(EndfieldHUD 1.0.1, internal build 11). The new scene supplies presentation;
the existing desktop stores, controllers, text, and module identities remain
the source of truth. No data migration is needed for this visual integration.

The current visual and interaction contract, including dated design decisions,
is maintained in [Desktop HUD design](design.md).

The visual baseline is `90e3a09bdd3103caebe6acb060398d394555ea07` from
`codex/endfield-watch-motion`. Work is isolated on
`codex/endfield-hud-integration`; neither `main` nor the published stable release
is replaced by this branch.

## Identity and saved data

Keep `CFBundleIdentifier = io.github.endfieldcharge.EndfieldCharge`. Preferences,
login registration, and macOS permissions use this identity. The visible app
name is EndfieldHUD, but saved data remains under
`~/Library/Application Support/EndfieldCharge/`. Renaming that directory would
make an existing installation appear empty.

| Relative path | Version | Contents that must survive |
| --- | --- | --- |
| `Notes/notes.sqlite3`, `Notes/Images/` | SQLite `user_version=1` | Text, checklist item IDs/completion, images, position, size, z-order, date, pin state |
| `FileShelf/shelf.json` | 1 | Item IDs, file bookmarks, security scope, file identity and metadata; original files stay where the user put them |
| `AppShortcuts/shortcuts.json` | 1 | IDs, renamed labels, app identity/bookmark, original name, chosen icon preset |
| `Profile/profile.json`, `Profile/Images/` | 1 | UID, awakening/birthday, name/tag, introduction, levels/counts, full-resolution avatar, all crops, background, local card color, accumulated work seconds |
| `WorldMap/map.json` | 4 | Pin IDs/dates/coordinates and camera; existing versions 1–3 migrate once without discarding custom positions |
| `EventLog/events.json` | 1 | Bounded 500-event history, stable event kind identifiers, allowlisted metadata |

These stores reject corrupt/future schemas rather than replacing them with
empty documents. JSON stores check for concurrent file changes before saving.
Notes write only after explicit edits in transactions. Managed images are
removed only after the referencing edit commits; shelf deletion never deletes
the original file. Preserve these paths, codecs, validation and write semantics.

The profile UID and awakening date are created once. Recreating a profile during
view construction would lose identity and work history. Saved notes can be
outside the central circle; do not clamp them to a new center-panel rectangle.
Profile avatar zoom supports 20× and retains original imported image pixels.
A nil profile color follows the global theme; an explicit card color is separate.

Clipboard contents/pins, current Work Mode session, activity history, audio
routing sessions and storage caches are deliberately memory-only. This change
must not silently persist them or auto-enable audio capture on launch. Only
cumulative Work Mode duration is written into the profile.

## Preference compatibility

`ConfigurationStore` uses `UserDefaults.standard`, in the existing bundle domain.
The preserved keys in `Sources/Models.swift` are:

```text
hudSettingsSchemaVersion
displayMode displayDuration accentHex theme scale placement
customScreenID customPositionX customPositionY
language hudScale hudOffsetX hudOffsetY
parallaxIntensity perspectiveIntensity backgroundDarkness blurAmount
reduceMotion ambientAnimation closeOnFocusLost openOnActiveDisplay
hudDisplayUUID hudDisplayName launchAtLogin
batteryAlertsEnabled devicePopupEnabled lowPowerVisualMode
applicationIcon clockFormat summonShortcut
```

AppDelegate also owns `hasLaunched`. Updates use
`HUDUpdateAutomaticallyInstall`, `HUDUpdateLastNotifiedRelease`, and Sparkle's
own preferences. Do not replace the defaults domain or reset these during a
scene switch. `backgroundDarkness` means dimming opacity: the saved default
0.63 corresponds to 37% brightness. `scale` controls the battery popup;
`hudScale` controls the full HUD. Fixed display selection stores a UUID/name,
not just a transient display number.

Keep stored enum raw values, including retired `originium` and `orundum` app
icons. Preserve module raw values, event kind strings, shortcut preset values,
and the JSON-encoded summon shortcut representation. Default summon is physical
Control + backtick; its modifier bit encoding is independent of Carbon flags.
Languages remain System, English, Simplified Chinese, Traditional Chinese and
Japanese. All stable translations remain available.

The user's Focus shortcuts are still named **EndfieldCharge Focus Start** and
**EndfieldCharge Focus End**. Cosmetic renaming would break configured automation.
The requested Focus follow-up now prefers public Control Center Accessibility
controls after the user grants Accessibility access. It reads the existing Focus
state before enabling Do Not Disturb, preserves an already-active Focus, verifies
readback, and never falls back after an uncertain mutation. Unrecognized macOS
UI or missing permission may use those existing configured shortcuts. No private
Focus API, settings database, idle polling, or automatic permission grant is used.
The setup reminder is removed. Pause still does not end an active work session.
The native UI was inspected read-only on macOS 15.7.4; actual toggling is covered
by synthetic executor tests, and Tahoe is not independently verified.
Keep updater feed URL/public key, signed-feed policy,
bundle identity and monotonic internal build number intact.

This branch keeps the stable GitHub/Sparkle update feed. An integration build is
not a new stable release or a separate automatic-update channel. Testers must
download a successful build of this branch from GitHub Actions and install it
manually; the README release links still point to the stable application. A
normal branch installation uses the same saved-data identity as stable, so
automated checks use the isolated fixture modes below instead of a second live
installation.

## Feature and lifecycle boundary

All 16 `HUDModule` identities remain reachable: Notes, File Shelf, Clipboard,
Volume, Work Mode, Event Log, Map, Add App, System, Display, Hotkeys, About,
Storage, Activity Monitor, Power, and Personal Profile. Dynamic saved app
shortcuts remain launch actions with their own UUIDs, not aliases for Add App.

Existing canvases and native interactions continue to provide:

- Notes: free placement, editing, images, checklists, pinning, delete confirmation.
- Shelf: bookmarks, multi-selection/drag, navigation drop target, canceled-drag recovery.
- Clipboard: bounded private session history, copy/pin/clear, concealed-item exclusion.
- Volume: system devices/volume plus explicitly enabled per-app routing.
- Work Mode: countdown/stopwatch, pause/reset, Focus integration and tracked hours.
- Event Log: application actions and selected power/audio/display events, filter/clear.
- Storage: cached capacity/details and animated handoff to macOS Storage settings.
- Activity Monitor: overview/history and sortable per-app CPU/RAM/network/disk.
- App launcher: choose/drop/rename/icon selection, launch or reopen a closed window.
- Map: offline terrain, coalesced pan/zoom rendering, bounded pins and saved camera.
- Settings: language/display/hotkeys/appearance/battery options, scale/position rollback.
- Profile: identity, image import/crops/reset, editable statistics, introduction/color.
- Battery popup/menu, menu-bar actions, update checks and confirmed animated quit.

The `OverlayController` owns services and model lifetime; scene views must not
create duplicate monitors/stores. Canvas activate/deactivate remains paired.
Closing releases the shell/canvases and their animations while retaining service
state. Native hit regions, editing, accessibility and drag/drop must use the
same transforms as visible controls, including pinned notes during closing.

App/Finder/Storage/update/quit handoffs finish the closing animation before
activation. Interrupted transitions use generation checks. Pointer tracking
continues during opening and closing; ambient animation runs only while open.
System activity samples at 1 Hz visible and 5-second intervals closed; per-app
sampling stops while hidden. Clipboard polls at 1 Hz and suspends with the
session. Map rendering uses one coalescing worker, not a job per display frame.

## Integrated presentation and bounded rendering

`SystemHUDView` keeps one `HUDSourceWatchView` visible while the selected native
module changes in the existing `HUDModuleContent` host. The source scene provides
the outer geometry, authored opening/closing clips, depth, hover animation and
projected navigation. Existing canvases provide the actual desktop functions.
The integrated scene does not instantiate the game's Domain or banner carousel;
those remain available only to explicit source-reference tests. A selected,
immutable profile-card prefab and the source exit button supply their original
appearance, with the saved desktop profile and existing quit confirmation bound
to them. The native clock, battery badge, editors and free notes retain their
existing controllers and accessibility surfaces.

`HUDDesktopWatchNavigation` maps the four settings categories to the left-hand
plates and other desktop actions to the right-hand scroll. Storage and Activity
Monitor use the source scene's two lower center controls. The right-hand scroll
recycles a fixed nine-row, eighteen-button pool through
`HUDSourceDesktopNavigationLayout`; the number of saved app shortcuts does not
clone scene nodes. Every logical app UUID remains reachable, and Add App stays
last. Source mouse clicks and accessibility activation resolve the same camera,
projected rectangles and clipping masks; scroll accessibility actions reuse the
same bounded navigation model.

The source playback clock drives finite opening/closing and pointer/hover
changes. Idle ambient frames use the same scheduler. Native feature planes keep
the source center-plane homography instead of starting a second camera or set
of legacy ambient loops. Native dimensions are calibrated once per viewport;
their layout scale and offset cancel around the source projection so saved HUD
scale/position are not applied twice. Drawing, inverse hits, inline editors and
accessibility use the same projection, including free notes during closing.
The source gyro freezes only for an existing actual editor/drag lock; opening a
dropdown does not freeze it. Pointer angles are 1.25 times the authored response,
with the original resting pose and user intensity settings retained. The source
wrapper supplies the closing fold while native finite fades and the charge
animation remain. A temporarily stale window-occlusion flag must not stall a finite
transition on an otherwise visible window. Hiding or tearing down the shell
stops its source clock; service sampling follows the existing module lifecycle.

The rendering shortcuts are derived-state caches with explicit fallbacks:

- `HUDSourceWatchView.applyDesktopButtons` reuses a settled button pose only in
  the visible desktop phase, after the button animator no longer needs frames.
  Its key includes Reduce Motion, the animator's `stateGeneration`, and the
  wrapper transforms/properties/diagnostics with only known ambient rotations
  excluded. Hover, press, enable/reset changes advance the animator generation;
  wrapper changes and active blends take the sampled path. Ambient rotations
  are copied from the current wrapper into the reused result.
- `HUDSourceWatchFrameBuilder` reuses desktop layout only when non-ambient
  transforms, properties, diagnostic bindings, selection tints, scroll position,
  logical entry count and explicit generation match. Desktop label/visibility
  changes invalidate the generation. A changed world root can reuse the saved
  pose from before the authored slant pass, then rerun that pass against the new
  gyro axes. The resolved pre-slant graph is reused only when its slant cells do
  not depend on ambient descendants. It never reuses the previous tilt's slant.
- With the same root and unchanged layout, the seven ambient rotation roots and
  their descendants are resolved again. The restricted ambient path reuses
  static batches and recalculates affected Canvas-space vertex positions,
  inverse-transpose normals and batch world matrices on the CPU using the same
  operations as the full path. Dynamic text, masks, cuts or hit graphics make
  this shortcut ineligible. Reference Domain/widget scenes always use the full
  layout/presentation path.
- `forceRebuild` advances the layout generation and clears both geometry and
  geometry-content keys, so comparison does not accidentally share a stale
  vertex upload. `Tests/HUDIntegrationPerformance.swift` compares settled button
  channels, batches, transforms and hit geometry, plus SHA-256 fingerprints of
  the actual uploaded vertex/index buffer bytes, against forced full rebuilds.
  The inputs cover ambient poses, gyro changes and button-state changes.

Once the desktop wrapper, gyro, button animator and selectable-color animation
have settled, `HUDSourceWatchView` may retain a render packet tied to the current
playback generation, button generation and complete presentation revision.
`sampleAmbient` then samples only the original ambient rotation channels.
`buildSettledAmbient` additionally checks the world root, canvas size, scroll
position, logical navigation count, selection tints, builder generation and
renderer resource generation. Unexpected properties, missing/extra rotation
channels, visibility edits, changed resources or any failed guard return to the
full pose/layout path. Opening, closing, active gyro and hover blends continue
through that full path.

Each sparse frame keeps the immutable resolved base plus only the affected
ambient descendants. Production `Frame.node(id)` lookups consult the delta
before the base; the complete `resolved` snapshot is materialized for diagnostic
comparisons. The ambient path updates pre-indexed batches and uses the existing
geometry uploader for transformed vertices and normals. Cached stationary
pointer queries are tied to pointer position, presentation revision and input
eligibility; source changes or a different pointer trigger a new raycast. This
is derived presentation data, not another copy of desktop models or persistence.

Startup also avoids decoding unused desktop assets. Scene nodes/components and
animation clips/controllers each share one strict decode through
`HUDSourceWatchDocument`; the same graph/curve validation still runs. Desktop
captions use native text on the source planes, so desktop construction skips
original TMP fonts, labels and text-geometry loading. Source-reference rendering
keeps those assets. The frame builder decodes mesh `cab`/`path_id` through a small
typed identity record rather than decoding every mesh payload just to identify
it. Resource packaging and the manifest continue to verify retained source data.

The desktop document cache shares only the immutable document and selected
profile-card template; it does not instantiate game widgets.
One background prewarm can populate it; concurrent construction joins the same
load, resource identities are checked before and after decoding, and a failed
load does not become a cached empty document. Scene/layout caches belong to
their view/frame-builder instance and contain no copies of saved desktop state.

## Packaged resources and renderer lifetime

`package-watch-resources.py` derives an explicit desktop dependency inventory
instead of copying the complete authoring tree. Desktop staging omits unused
game Domain/news widgets, font atlases and evidence duplicates; the original
reference resources remain available to reference fixtures. Selected JSON and
binary resources use the bounded `EHUDZ01` raw-DEFLATE container decoded by
`HUDSourceResourceData`. Decoded source bytes, authored mip levels and BC7
payloads are retained. The compact material catalog selects original JSON
tokens without rewriting numeric/string values. Shader text, cursor files and
the small directly-read camera/mesh records stay in their native representation.
The existing Intel texture fallback and minimum OS versions are unchanged.

`HUDSourceMetalRenderer` implements these resource-lifetime controls:

- Desktop textures are loaded on first use; metadata-only inventory queries do
  not allocate textures. Aliases resolve to the same asset, and dynamic texture
  registration invalidates affected bindings. Source-reference loading remains
  eager. Authored sampler state and mip chains are retained.
- Shader functions and pipelines are prepared lazily for the desktop inventory.
  Identical immutable pipelines can share a program. One retained cache, keyed
  by Metal device and inventory fingerprint, is bounded to 32 libraries,
  32 function pairs and 64 pipelines; it does not retain view textures,
  geometry, drawables or a presentation clock.
- The drawable pool is bounded to two. Geometry/tint buffers are reused only
  at the required capacity after the previous render command has completed;
  otherwise a new buffer is allocated. Uniform plans and current encoded bytes
  are cached by their actual camera, time, world and override dependencies.
- A fresh per-encoder binding cache skips repeated pipeline, depth/stencil,
  buffer, uniform, texture and sampler bindings. No encoder state is carried
  across frames. Default texture bindings are immutable per material pass;
  overrides and newly registered textures take the applicable lookup path.
- Desktop uniform preparation resolves each field's override/material/dynamic
  precedence once per current batch and material pass. Camera, time and world
  dependencies still invalidate encoded values independently. Changed payloads
  are rebuilt from zero in the original field order, including partial and
  overlapping fields. Geometry uploads do not invalidate unrelated constants;
  material/accent changes do. The reference encoder remains available for exact
  byte comparisons and same-binary benchmark comparisons.
- Adjacent compatible desktop image batches share one draw when their material,
  world transform, textures, uniforms, clipping/stencil state and accent policy
  match. Original triangle order is retained; dynamic text, depth-writing and
  shader vertex-ID paths remain separate. Only the current groups are retained,
  and buffers are reused after the prior command completes. Reference scenes
  keep their original draws. Paired GPU tests explicitly disable this path in
  the baseline and enable it in the candidate.
- Desktop stencil-only attachments require the checked no-depth-write subset,
  supported comparisons, standard clipping and no fragment depth output or
  depth-failure stencil effect. A changed clear depth, unsupported material or
  runtime depth-failure override retains/restores the combined depth/stencil
  path before encoding. HDR/reference rendering keeps its required path.

Desktop accent mapping applies to the selected yellow color channels while
retaining alpha and leaving neutral/reference rendering outside that mapping.
The selected profile card opts out of global recoloring, so a saved card color
remains independent of the HUD theme in both vertex and material colors.
All derived output must preserve draw order, geometry, clipping, hit targets
and opacity. These mechanisms do not establish a CPU, GPU, memory or frame-rate
result; correctness checks and runtime measurements remain separate.

## Regression checks

```sh
python3 scripts/test-integration-compatibility.py --self-test
python3 scripts/test-integration-compatibility.py --behavioral
```

The static guard preserves 47 functional files byte-for-byte and records four
explicitly requested changes beside their original and current hashes: Focus
control, the map-edge constant, and two RAM unavailable labels. All 51 files
remain guarded. It also preserves the 575-entry baseline with three exact,
reviewed replacements (RAM labels and map help) and checks bundle/update identity.
New translations are allowed; unreviewed removal or rewriting still fails.
Intentional future functional changes need a separate compatibility review;
do not regenerate the manifest merely to make a failure disappear.

The behavioral command runs the existing core suite. `NotesStoreTests`,
`UserProfileStoreTests`, `FileShelfStoreTests`, `AppShortcutStoreTests`,
`WorldMapStoreTests`, `SystemEventLogTests` and `SettingsTests` seed disposable
stores/defaults suites, reopen data and check identity, geometry, images,
bookmarks, migrations, failed writes and corrupt/future-schema preservation.
No real user preferences, shelf files or history are used. Existing model and
controller tests cover sampling cadence, Focus ownership, audio fixtures and
shortcut conflicts. Navigation tests additionally cover the new shell's mapping
to every stable module and dynamic shortcut identity.

After a native build, run graphical checks one at a time with `--ui-test`:

```sh
APP=build/dev/EndfieldHUD.app/Contents/MacOS/EndfieldHUD
"$APP" --ui-test --lifecycle-smoke-test
# Also exercise an injected renderer failure and native-shell recovery:
"$APP" --ui-test --lifecycle-smoke-test --source-fallback-smoke-test
"$APP" --ui-test --navigation-smoke-test
"$APP" --ui-test --notes-shelf-smoke-test
"$APP" --ui-test --telemetry-smoke-test
"$APP" --ui-test --app-shortcut-smoke-test
"$APP" --ui-test --work-smoke-test
"$APP" --ui-test --event-log-smoke-test
# Optional repeated full-cycle, popup-restoration and position-edit check:
"$APP" --ui-test --system-smoke-test
```

These fixture paths bypass ordinary app startup and isolate preferences/stores.
Navigation excludes real pointer clicks and external focus dismissal so scripted
swaps are deterministic; Lifecycle explicitly exercises focus dismissal, and
System keeps the workspace observers for repeated opening/closing checks.
Add `--preview-directory /absolute/path` to the navigation fixture to export
all settled modules from their own rendered layers, without desktop capture.
Do not launch a normal development copy to measure this comparison: it shares
the real app's identity and preferences. Fixtures do not establish physical
hardware permissions, true cross-app activation or Gatekeeper behavior.

Resource and rendering changes have separate validation paths:

```sh
scripts/test-release-resources.sh
python3 scripts/test-watch-resources.py
# Use the optimized benchmark binary produced by benchmark-hud.sh:
build/hud-benchmark/EndfieldHUD-Benchmark.app/Contents/MacOS/EndfieldHUD --ui-test --verify-only --output /absolute/path/checks.json
```

The resource suite stages temporary desktop/reference inventories, decodes
selected files through the native loader, compares them with source bytes and
exercises malformed/tampered inputs and compact material selection. The
`--verify-only` fixture compares cached/full and sparse/direct rendering against
independent forced rebuilds: uploaded vertex/index fingerprints, node matrices,
rectangles, draw state/order and clipped hits. It also checks invalidation after
gyro/size/scroll/navigation/tint changes, caption/visibility edits, renderer
appearance changes and external geometry registration.

`verify-desktop-renderer-parity.py` accepts a saved pre-change renderer Swift
file, `--resources` pointing to one packaged WatchSource directory, and
`--optimization stencil`, `textures`, `bindings`, `uniforms` or `batches`. It snapshots the remaining
sources, compiles both variants, compares raw BGRA frames and batch diagnostics,
and writes input hashes plus allocation/binding counters to its report. Keep
the baseline and output directory separate for each optimization. The broader
`verify-source-renderer-parity.py` accepts a baseline Git revision and exercises
the reference rendering fixtures. Run GPU fixtures serially, with no benchmark
or other graphical fixture active. These bounded input sets cannot establish
all-device rendering equivalence or performance; an unavailable Metal probe
must be reported separately from passed non-GPU tests.

## Performance comparison

Use a native optimized development build (`DEV_OPTIMIZATION=-O`) for each revision,
with separate output directories and identical hardware/settings. The default
development build is unoptimized and cannot support release-performance claims.
Record OS/hardware, display scale, Reduce Motion, visible module and sample window.
Run graphical fixtures serially and avoid simultaneous builds during measurement.

`scripts/benchmark-map.sh` measures optimized map camera/render work against
temporary data, including exact-frame readiness. It explicitly does not measure
whole-HUD idle CPU, native compositor cost or memory. `scripts/stress-map.sh`
adds a self-driven visible map harness with CPU, RSS/physical footprint, input
latency and screenshot readiness; its statistics still belong to the map harness.
Use the same whole-HUD measurement protocol on stable and integrated builds for
closed/open-idle/transition comparisons. Keep source-build elapsed time separate
from runtime samples. A passed lifecycle fixture verifies teardown/invariants,
not a claim of 60 FPS or a GPU measurement.

`scripts/benchmark-hud.sh` builds a separate optimized whole-HUD fixture. Set
`HUD_BENCHMARK_SOURCE_PROJECT` to the stable checkout to compile that revision
without editing it, and `HUD_BENCHMARK_DIR` to an isolated output directory.
Run the resulting app bundle's `Contents/MacOS/EndfieldHUD` binary with
`--ui-test --output /absolute/path/report.json`.
The report covers closed, selected open-idle modules and injected pointer
movement, recording process CPU, RSS/physical footprint and rendering counters
when present. It does not include WindowServer CPU or directly measure GPU
activity/display FPS. Keep its window visible and unobstructed; run no other
GUI fixtures or compiles during the timed intervals. Cached/full frame parity
checks are correctness checks and should not overlap performance intervals.

The default benchmark follows the production background document prewarm;
`--cold-source` measures construction without it. Physical pointer events are
excluded so a user's mouse cannot compete with the injected movement. The
`--legacy-source-uniforms` flag selects the reference uniform encoder in the
same executable, only when `--ui-test` is also present. Desktop batch merging is enabled by
default; `--original-source-batches` selects the original draws and
`--merge-source-batches` explicitly selects merging. Both together are rejected.
Ordinary launches ignore these test switches. Pointer benchmarks inject only
screen coordinates and let the production source scheduler consume them;
synchronous geometry-verification hooks must not request extra timed frames.
Run both paths serially
after the correctness fixture, without recompiling between timed comparisons.


### Measured branch result — 2026-10-02

The local universal bundle is approximately **35 MiB**. Its selected source-scene
payload is **11.65 MiB**, compared with a 354 MiB authoring resource tree.
The original source resources remain in the repository for reproducible fixtures;
they are not all copied into the installed application.

On a MacBook Air M2 with 8 GB RAM, macOS 15.7.4 and a 1470 × 956-point display
at 2× scale, optimized sequential fixture runs produced these samples. Ambient
motion was enabled, Reduce Motion disabled, and real pointer events excluded.
CPU values are percentages of **one CPU core** for the application process.

| Scenario | Stable CPU | Integration CPU | Stable footprint | Integration footprint |
| --- | ---: | ---: | ---: | ---: |
| Closed after use | 0.04% | 0.06% | 52 MiB | 121 MiB |
| Map idle | 0.24% | 12.44% | 78 MiB | 190 MiB |
| Clipboard idle | 0.15% | 6.64% | 79 MiB | 190 MiB |
| Notes idle | 0.17% | 12.28% | 79 MiB | 191 MiB |
| Activity Monitor idle | 0.15% | 12.89% | 74 MiB | 191 MiB |
| Pointer motion | 4.47% | 42.24% | 74 MiB | 189 MiB |

The integration submitted about 30 source frames/second idle and
59.4 during pointer motion. Once closed, its source timer stopped and
submitted zero frames. The current build opened in
178 ms synchronously (stable 93 ms), presented its first source frame at
382 ms, and reopened in 131 ms (stable 54 ms).

A same-binary comparison with shader preparation disabled measured
191 ms synchronous opening and 404 ms first presentation. With it enabled,
twelve shader libraries/six function pairs, including required clipped variants,
were prepared in 20 ms on the utility queue within the existing eight-pair cap.
No pipelines, textures, renderer or window were created by preparation. The
first visible fixture compiled zero additional shader libraries; pipelines
remained lazy. This moves bounded compilation off first presentation, but does
not eliminate the remaining opening delay. The prior branch's single sample
was 169/391/138 ms for synchronous opening/first presentation/reopening; this
pass does not establish an across-the-board speedup over that sample.

Total CPU/document/shader preparation took 1.25 s off the main thread.
These opening figures start after launch preparation has completed; they do
not measure the whole application's cold launch. An immediate first opening
can still wait for CPU document preparation, and GPU programs remain lazy if
their preparation has not finished. A bounded immutable cache retains no view
or texture and runs no timer. These are single-run samples, not percentile
latency or guarantees on other Macs. The larger map preserves its raster budget;
projected button cutouts update only when their geometry or camera changes.

**Performance parity with stable has not been achieved.** Resource size and
closed rendering are bounded, and the new renderer has been substantially
reduced from its initial measurements, but open CPU and retained memory are
still higher. Stable delegates much of its animation to Core Animation; the
new scene resolves source geometry and submits Metal draws continuously.
Do not describe submission cadence as measured display FPS or claim near-zero
GPU utilization from these process counters. Instruments was unavailable here.
The raw samples and input fingerprints are in
[integration-benchmark.json](integration-benchmark.json).

The core suite passed 50,090 assertions during this pass. Final native checks
passed 623 navigation assertions across all 16 sections, 40 Notes/Shelf arrival
and queued-navigation assertions, 86 lifecycle assertions, and 85 shortcut
assertions. The latter include visible-versus-transparent map/button hit routing,
close-first launch, preset artwork, captions and live language changes. The compatibility guard retains 47 unchanged
files plus four exact reviewed changes, all 575 baseline translated entries
with three exact reviewed replacements, and bundle/update identity. All 13
isolated guard mutation checks passed. Fourteen CPU metadata checks verify all
132 material inputs, cache reuse/invalidation, corrupt-input recovery, reference
fallback and bounded required clip-shader selection without creating a GPU view.

Exact cached/full GPU geometry and hit comparison passed, including program
preparation's idempotence and bounds. Profile artwork helpers passed 18 CPU
checks and portrait helpers passed four; the packaged profile preview also
confirmed that both locations load the same frame through the compressed
resource reader. The prior 12 source-shader profile-color checks and 66 paired
renderer comparisons remain separate historical evidence. The offline Metal
compiler is unavailable on this host; runtime Metal checks work.


## Current presentation follow-up

The original right-side banner plane now hosts the desktop clock, date and Work
Mode status. It follows its own source projection and entrance, while the central
modules, notes and battery badge keep their established plane. The original
full-screen source close target is disabled in desktop mode; the native host
accepts outside-circle dismissal through inverse projected geometry.

Quit and layout-preview recovery share a safely sized centered card. Both now
use the central source projection directly, with scale and position removed for
safe screen placement. There is no separate pointer easing clock. The real
presentation-layer conversion drives pointer hits and accessibility bounds.
Re-previewing during dismissal cancels the old completion. The larger battery
capsule still clears the unchanged central buttons.

Desktop ambient rings receive a signed random rate once per opening; the six
triangles also spin independently. Right-list trackpad input follows the gesture
directly, then uses bounded edge travel and a finite settling motion on the
existing display clock. Momentum tails cannot restart an edge rebound. Limit
arrows grey out and reject further scrolling in that direction. Source
reference fixtures retain their authored movement. Right captions are smaller,
icon bounds are normalized, and hover faces retain their original neutral
colors. The side-button luminous edge is reduced independently of the source
shadows, which keep their original color and opacity. Custom shortcuts use the same artwork resolver
as their picker, and recycled slots explicitly activate their primary caption.
A language change refreshes source captions, profile labels and accessibility
actions in place while preserving custom names and scroll position. Side hover
sprites use 18% of their authored alpha. The whole profile plate and red quit background use the existing 100 ms
ColorTint fade. The plate has a normal-alpha 5% interior wash and 38% outer
edge; no new clock or additive photo overlay is introduced.

The card uses an existing dark industrial default, right-aligned authority/MAX
labels, a profile-colored outer border, and no additive avatar/card wash. The
profile editor reuses the exact selected source avatar-frame sprite. Uploaded card backgrounds are
cropped and darkened inside the rounded photo panel, preserving the original
outer decoration pixels and alpha silhouette. User artwork is explicitly uploaded
as straight-alpha sRGB: an offscreen source-shader probe reproduced the previous
gray-128-to-188 washout and verifies gray 128 now stays 128. No profile value or
saved image is rewritten. The proof in `scripts/VerifyProfileArtwork.swift` also
checks transparent edges and 4,251 visible source decoration pixels.

The 340-point clock plate is offset to (126.28, 8) in its original banner
plane, further left/down and shorter at the right edge. The
retired native battery arc remains hidden on every appearance/settings update.
Opening prepares the native layer hierarchy without first constructing a fully
deployed source frame that would immediately be discarded. Camera/controller
JSON and fragment-depth eligibility are prepared once; bundled metadata bypasses
repeat filesystem validation during the same app process. External preview and
probe directories retain file-identity validation and corruption rejection.

Launch preparation also derives the desktop drawable materials and prepares
their original Metal shader functions on a utility queue. Clip and soft-mask
variants are resolved using the same ancestor/Canvas boundaries as the frame
builder; required variants take priority within a hard limit of eight pairs. No renderer, textures, geometry, command queue, drawable, pipeline or
view is constructed. The existing one-device/inventory program cache retains
the result, and the foreground lazy path remains usable without waiting on a
compilation lock. Unsupported/failed preparation leaves ordinary lazy rendering
in place. This shifts finite work to application launch; it does not eliminate
launch work or guarantee a benefit when the HUD is opened before preparation
finishes.

Geometry cache keys preserve the Double precision used when baking vertices.
Rounding the input matrix to Float could incorrectly reuse a buffer even when
the final baked Float vertices differed. Uniform and vertex calculations remain
unchanged; the regression compares exact uploaded geometry rather than loosening
tolerances.

The map presents its retained 440-point canvas at 1.14× without increasing the
raster budget. A cached radial mask feathers its last 9 points. Source sprite
boundaries define the foreground cutouts for the lower two buttons and their
pointer precedence. Only changed camera/geometry projections rebuild these
small paths. Pin coordinate visibility is independent of persistent selection;
left-click hides coordinates, and right-click on a pin removes it. No map store
schema or initial camera setting changes.

Section swaps use a monotonic 300 ms reveal and matching exit. Notes workspace
visibility is driven by the actual swap, including queued requests and
cancellation, rather than the latest click. Input is deactivated again at the actual handoff
after navigation layout callbacks, before Notes installs its arrival; queued
clicks do not cancel it. Shelf and Add App reuse prepared
content on activation when unchanged. The wordmark's two brief opacity dips run
on the source clock and finish after 460 ms; Reduce Motion leaves it steady.
RAM replaces user-facing memory labels, including unavailable-data status,
without renaming stored identifiers or altering sampling. These explicit
interaction/copy updates are recorded narrowly in the compatibility guard.
