# Interaction performance regression — 2026-10-03

This pass investigates high CPU and delays while opening the HUD, switching
sections, and interacting with Activity Monitor's Apps table. It follows the
[CPU bottleneck pass](performance-followup.md); the comparison baseline is
`d6b82ad`, not the old native interface. The reported CPU peak prompted a more
representative interaction workload, rather than another idle-only comparison.

The visual contract is unchanged: the same artwork, shaders, render resolution,
tilt, hover and press curves, opening and closing motion, and section transitions.
Normal mode keeps 60 Hz finite motion and 30 Hz settled ambient rendering. The
existing Low Power and Reduce Motion policies remain in place. No stored-data
formats, user settings or feature behavior are intentionally changed.

## Findings

The earlier steady-state activity scenario covered the overview, not a live
Apps table. It did not exercise process rows, sorting, first icon lookup, or
repeated section changes. The new fixture uses real read-only system telemetry
with isolated preferences and feature stores.

Separate `sample` captures identified several main-thread costs:

- Section changes re-applied all source navigation captions and remeasured
  unchanged fonts through CoreText. An unchanged appearance update repeated the
  same work, invalidated prepared frames, and requested another immediate draw.
- Hover and press callbacks could synchronously prepare and draw an extra frame
  while the source animation clock was already running. Acquiring a drawable
  can wait for the compositor, so this also prolonged input handlers.
- Full layout resolved the entire scene for scroll bounds, although only a
  scroll owner, content, viewport and their ancestors determine those bounds.
- Accessibility projection and clipping ran for every source button on every
  rendered frame, even without an accessibility geometry query. Native hover
  traversal also converted coordinates for decorative descendants.
- The Apps table repeated static row styling, text assignment and scaling;
  first-time app icon lookup and catalog metadata resolution shared the main
  thread with interaction handling.
- Startup reparsed profile scene data, sprite/material metadata, and mesh
  identity information already available elsewhere. Hidden fallback artwork and
  some module activation paths also did duplicate presentation work.

These are distinct costs. A blocked drawable acquisition is not CPU execution,
and an expensive section handler does not necessarily raise idle CPU. Neither a
single stack sample nor the user's observed peak establishes the contribution
of every individual change below.

## Changes

### Keep interaction work bounded

Module selection updates only the previous and current source button captions
and selected accessibility values. A full refresh still occurs for actual
language, theme, configuration or system motion changes. Reapplying unchanged
preferences keeps the active clock and prepared presentation intact.

When a desktop animation clock is already running, hover and press callbacks
record their state and original timestamps for the next scheduled frame. They
do not synchronously submit an additional frame or restart that clock. Initial
opening and stopped-clock changes retain immediate presentation. The authored
animation durations and sampling timestamps are unchanged.

Scroll layout retains immutable ancestor lists, then resolves their current
transforms in the original parent-first order. It resolves again for each scroll
writer so nested scrolling can observe an earlier writer's changes. Forced
verification retains the independent full-scene resolver. The desktop scroll
bounds need eight nodes instead of the full 789-node scene; 73 focused assertions
compare this scoped path with the full resolver.

Native highlight traversal performs coordinate conversion at actual controls
and clipping boundaries, rather than at every decorative layer. Clipboard and
Add App activation reuse prepared content; genuinely dirty content still
refreshes before use.

### Prepare only the data being consumed

Source accessibility geometry is prepared from the latest rendered frame when
an accessibility client requests children, hit testing, a button frame,
visibility, enabled state or activation. This works for all accessibility
clients, without a VoiceOver-only gate. A revision key avoids duplicate work
within the same presentation, while hidden/closed windows and disabled phases
remain unavailable. Weak callbacks and one latest-frame reference avoid adding
a timer, frame queue or ownership cycle.

Activity Monitor keeps its bounded row pool. Scrolling moves existing rows and
the scrollbar; unchanged strings, fonts, colors and render scales stay intact.
The hidden overview does not rebuild graph paths while Apps is visible. Process
sampling remains at 1 Hz while Apps is active, with catalog resolution no more
often than every five seconds.

The app list is captured on the main thread; bundle/name metadata is resolved
on the existing utility worker. Initial icons load on a serial utility queue,
with at most eight pending requests and a cache bounded to the current app
catalog. Completion verifies the row still represents the requested bundle.
UI mutations remain on the main thread. Apple documents atomic access to
[NSRunningApplication properties](https://developer.apple.com/documentation/appkit/nsrunningapplication)
and permits [icon(forFile:)](https://developer.apple.com/documentation/appkit/nsworkspace/icon%28forfile%3A%29)
on background threads; these guarantees do not make arbitrary AppKit operations
safe to move off the main thread.

### Reduce render and startup overhead

Each Metal submission has its own autorelease pool. The renderer releases its
previous diagnostic drawable reference before acquiring the next drawable from
the existing bounded pool. Readback between draws remains available. Acquisition
time is recorded separately to distinguish compositor waits from CPU work.
Apple explains that unavailable drawables block their caller and recommends
short drawable lifetimes and a per-frame autorelease pool in
[Metal Best Practices: Drawables](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/Drawables.html).

Uniform encoding writes once into a zeroed contiguous buffer instead of entering
`Data.replaceSubrange` for each field. Field order, partial and overlapping
overrides, matrix layout and default bytes remain identical. The original writer
is retained in verification builds as an independent byte oracle.

Immutable material, sprite and texture metadata is parsed once per source
document under a lock and shared with builders. Profile scene/components use a
single decode. Mesh identity names come from the renderer's existing validated
loads. Each renderer still validates its actual texture availability. Hidden
native fallback saved-profile binding and repaint are deferred until needed,
and duplicate launcher refresh is removed. The fallback card's constructor
still prepares its default artwork. Source profile updates continue to use
current saved data.

This reduces repeated preparation, but does not remove every cold-start wait:
an immediate opening can still wait for source prewarming in progress. It also
does not convert the renderer to an asynchronous render thread. The changes
follow Apple's guidance to separate data preparation from UI mutation and avoid
unnecessary synchronous work in interaction callbacks; see
[Improving app responsiveness](https://developer.apple.com/documentation/xcode/improving-app-responsiveness).

## Measurement method

`Tests/HUDIntegrationPerformance.swift` is compiled with the optimized build
settings in `scripts/benchmark-hud.sh`. Comparable runs use frozen source
snapshots, the same packaged resources, display and fixture revision. Compilers,
sampling and other graphical fixtures must not run during a timing comparison.
Profiling and correctness runs are separate from timing runs.

The interaction workload uses `--ui-test`, `--interaction-workload` and
`--live-telemetry-benchmark`. Preferences, notes and other feature stores remain
isolated. System and app metrics are real, read-only readings; the fixture does
not launch applications or post global input. It filters physical pointer events
within its own process, then directly sends the same synthetic mouse events to
both the source shell and native host. The default stream is 60 Hz;
`--interaction-pointer-hz` permits a separately identified stress run.

After opening and entering Apps, the fixture measures six seconds of live idle,
eight seconds of pointer movement, six seconds of sorting every 650 ms, and
eight seconds of section changes every 700 ms. It then closes for five seconds
and reopens. Section actions cover Clipboard, Notes, File Shelf, Add App,
Storage, Event Log, System, Display, Profile, Map and Activity Monitor.

| Measurement | Interpretation |
| --- | --- |
| Process CPU | User plus system CPU time divided by wall time, as a percentage of one logical core. Excludes WindowServer. |
| Physical footprint and RSS | Separate end-of-scenario process measurements. Allocator, caches and system load can change them; an increase alone does not establish a leak. |
| Synchronous action time | Time until the selection or sorting handler returns, not the complete transition or end-to-end input latency. |
| 10 ms run-loop probe | Distribution of main-thread scheduling gaps, including p95, p99, maximum and gaps over 50 ms. It is not an FPS or presentation measurement. Disabled while closed. |
| Source submissions | Frames submitted by the source view, not frames actually displayed. |
| Source preparation | Entire source-view preparation path, including native projection work; not just scene layout. |
| Opening | Record synchronous opening separately from source/program preparation and the first completed/presented GPU frame. Driver/cache warmth must be reported. |

Live application counts and activity vary between runs, so repeated ranges are
more useful than one apparent best result. The fixture does not reproduce every
application, display, drag/drop operation or user-input pattern. Its readings
must not be equated directly with the reported screenshot's instantaneous CPU.

## Measured results

Three isolated runs per revision on an M2 MacBook Air with 8 GB RAM,
macOS 15.7.4, SDK 15.5, and a 1470 × 956-point display at 2× scale.
Both use optimized builds and a requested 240 Hz pointer stream. Values are
ranges across runs; CPU is a percentage of one logical core.

| Workload | Before | After |
| --- | ---: | ---: |
| Apps with moving pointer | 19.0–20.5% CPU | 15.8–16.8% CPU |
| Repeated section switches | 13.5–14.1% CPU | 10.2–10.5% CPU |
| Live Apps idle | 2.8–3.3% CPU | 3.1–3.7% CPU |
| Apps sorting | 3.8–4.2% CPU | 3.5–3.7% CPU |
| Reopened Map idle | 8.1–12.3% CPU | 8.6–9.2% CPU |
| Closed after interaction | 0.12–0.23% CPU | 0.19–0.27% CPU |
| Median synchronous section handler | 36.3 ms | 9.4 ms |
| Enter Apps handler | 58.6–62.1 ms | 4.6–11.8 ms |

Pointer source preparation fell from 115–128 to 91–100 ms per second, with
approximately 60 source submissions per second in both builds. Section handler
ranges were 16–41 ms before and 1–30 ms after. There are still scheduling outliers:
maximum section run-loop gaps were 60–65 ms before and 55–57 ms after. Faster
handlers do not establish hitch-free display presentation.

**Opening and RAM are not resolved.** First synchronous opening measured
133–200 ms before and 179–239 ms after; first GPU presentation was 323–456 ms
before and 382–520 ms after. Source/program preparation was approximately
1.0–1.1 seconds before and 1.0–2.0 seconds after. One candidate preparation
included 969 ms of driver program compilation versus 10–26 ms in the baseline;
that run remains in the data. Warm synchronous opening was 123–151 ms before
and 122–205 ms after. The first-open observations are worse, not an
opening-speed win. Cache warmth, other apps and machine conditions remain
uncontrolled beyond excluding concurrent compilation/profiling. Further opening
work needs phase measurements rather than attributing all delay to JSON parsing.

Apps-idle footprint was 186–191 MiB before and 186–202 MiB after; closed footprint
was approximately 98–100 MiB in both builds. Neither idle CPU nor RAM shows a
consistent improvement. The screenshot's 53% instantaneous usage was not
reproduced by this isolated workload, and must not be presented as falling to
16%. The valid comparison is the matched workload above.

A separate current-build power-mode check retains the moving Map workload.
It measured about 35% CPU in normal mode, 21% with Low Power and 7% with Reduce
Motion, with normal/low-power source submissions near 60/30 Hz and the settled
power-saving clocks stopped. These are correctness/policy checks, not a matched
before/after Map comparison. Apps-workload gains must not be generalized to all
sections or described as old-native performance parity.

[Raw runs, source/binary hashes and exclusions](interaction-performance-benchmark.json)
identify the exact inputs. An overlapping repeat and a sampled repeat were
excluded completely. Earlier intermediate builds and the 60 Hz diagnostic run
are not mixed into the final ranges.

## Verification and limits

The final optimized native build passed:

- 51,243 core assertions, including 73 scoped-scroll comparisons.
- 66 byte-identical GPU frames and matching batch states against the baseline,
  plus original uniform-byte, prepared-packet and index-topology oracles.
- 118 lifecycle, 623 navigation, 108 app-shortcut and 20 telemetry HUD assertions.
- Cached/full source frames, current accessibility projection and all four
  power-mode policies in separate integration runs.
- The 51-file compatibility contract and 19 mutation checks.
- One isolated cache/accessibility run under `leaks`: zero leaked allocations
  and zero leaked bytes. Its time and instrumented footprint are excluded.

Correctness checks compare optimized uniform bytes and GPU images with the
original paths, and compare scoped layout against the full resolver. Additional
checks cover recycled Apps rows and icons, unchanged-content updates, source
button accessibility after scrolling and pointer movement, visibility and
enabled state during opening/closing, and hidden-window activation rejection.
These checks protect the exact visual and interaction contract; they do not
replace measured performance results.

Instruments is unavailable on this host. Investigation uses macOS `sample`,
process resource measurements and explicit render/interaction counters. No
display-FPS or comprehensive GPU-utilization claim follows from those tools.

**Performance parity with the old native interface has not been achieved.**
The baseline here is the preceding integrated renderer. Final comparative
results must identify the frozen source and binary used; measurements from
earlier passes do not automatically apply to this revision.

## Local artifacts and reproduction

The optimized test app is `build/interaction-performance/dev/EndfieldHUD.app`.
Quit the running copy before launching it; bundle identity and saved-data paths
are unchanged. This pass does not publish or replace a release.

The retained baseline and candidate benchmark apps under
`build/interaction-performance/baseline` and `build/interaction-performance/scoped`
can rerun the interaction workload with the switches above. Run them serially,
with no compiler, profiler or other graphical fixture running. A future fresh
baseline compile must use its matching fixture: the current verification-only
accessibility hooks do not exist at `d6b82ad`.

Removed approximately 2.85 GiB of generated compiler caches, superseded test
binaries and raw frame dumps after verification. Source snapshots, baseline and
final benchmark apps, the native app, measurements, logs and GPU pixel hashes
remain available. User files and saved HUD data were not changed.
