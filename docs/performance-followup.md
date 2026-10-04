# CPU bottleneck follow-up — 2026-10-03

This continues the [first performance pass](performance-parity.md). The baseline
is `33c1d9f`, not the old native interface. Normal mode keeps its animation
curves, sampling cadence, render resolution, shaders and effects. Low Power now
also caps finite source motion at 30 Hz. Saved-data formats are unchanged.

## Diagnosis

The mode comparison sends the same synthetic 60 Hz pointer stream directly to
both the source shell and native host, with physical pointer events filtered.
Before this change, Low Power stopped idle rendering but still prepared moving
frames at 60 Hz. Reduce Motion already lowered CPU substantially; its remaining
work included control hover updates. These settings should not be described as
having no effect: the problem was particularly visible during moving Low Power
workloads and normal tilt.

During tilt, the full preparation path revisited a 789-node scene even though
the slant writer modifies nine navigation roots and their descendants. It also
updated transforms on the hidden native fallback. Renderer preparation treated
changing numeric camera/mask values as a reason to rebuild draw setup. An early
packet prototype still rebuilt nearly every moving frame and did not improve
normal CPU; it was corrected before the final comparison.

## Changes

- A settled-pointer path updates the slant and ambient subtrees with the exact
  original matrix operations. It updates geometry, clipped hit regions,
  soft-mask matrices and world-dependent uniforms. Transitions, content changes,
  scrolling, resources and unsupported cases keep the complete builder.
- The renderer retains validated draw state and binding dependencies. Numeric
  override values refresh private uniform cells; key/shape changes rebuild the
  packet. Adjacent batching still checks that every merged member has compatible
  current state. This is prepared direct encoding, not an indirect command buffer.
- The source camera remains the owner of native-plane projection. Pointer events
  retain fallback state without rewriting the same external projection. Hidden
  native navigation does not receive redundant hover-layer traversal.
- Layout reuses immutable component/metric metadata. Rectangle calculations are
  retained only within one layout pass and cleared at every writer. Restoring a
  full frame after sparse motion invalidates the affected geometry, retaining
  unrelated image templates. The forced oracle bypasses these optimizations.
- Labels and icons share each button's current clip projection and retain
  unchanged clip paths. Invariant layer properties are not rewritten each frame;
  the original projective transforms still update during motion.
- Low Power consumes the newest pointer/hover state on a 30 Hz clock. Settled
  Low Power and Reduce Motion stop that clock. Normal mode retains 60 Hz finite
  motion and 30 Hz idle ambient rendering.
- Activity Monitor restores the source Report glyph and its authored shadow.
  Charging banners use 超充模式 in both Chinese variants.

Only one settled pointer presentation is retained; there is no growing frame
history. Existing in-flight geometry ownership and immutable index reuse remain
in force. Geometry and hit queries have independent revision checks.

## Research

Apple recommends reusing persistent Metal objects and frequently repeated draw
setup. Its indirect-command documentation explicitly uses heads-up displays as
an example. This renderer has per-draw texture and stencil state, so this pass
keeps direct encoding and caches compatible preparation rather than claiming an
ICB conversion. See [persistent objects](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/PersistentObjects.html)
and [encoding indirect command buffers](https://developer.apple.com/documentation/metal/encoding-indirect-command-buffers-on-the-cpu).

The Low Power frame budget follows Apple's recommendation to provide a frame
rate limit in performance settings. Timers stop when no visual work remains;
there is no second MTKView display clock. See [graphics performance and settings](https://developer.apple.com/documentation/Metal/improving-your-games-graphics-performance-and-settings)
and [minimizing timer use](https://developer.apple.com/library/archive/documentation/Performance/Conceptual/EnergyGuide-iOS/MinimizeTimerUse.html).

## Measurement method

Optimized arm64 builds use the same packaged resources on an M2 MacBook Air,
8 GB RAM, macOS 15.7.4, SDK 15.5, and a 1470 × 956-point display at 2× scale.
All fixtures use isolated `--ui-test` stores. CPU is the app process as a
percentage of one core, not total machine utilization. Physical footprint is
reported separately from RSS. Source submissions are not measured display FPS.
No compilers, samplers or other graphical fixtures run during timing.
The `sourceBuildMillisecondsPerSecond` field covers the whole source-view render
preparation, including native label projection and accessibility updates; it is
not an isolated measurement of the frame builder.

The four-mode workload uses two seconds to settle each configuration, then five
seconds idle and five seconds of pointer events. Assertions check both upper
power budgets and positive frame production, so a stalled view cannot count as
an optimization. The normal integration workload separately measures sections,
opening, closing and reopening. Correctness fixtures are run separately.

Instruments and the offline Metal compiler are unavailable on this host. Runtime
GPU comparisons verify rendered output. Opening results must distinguish source
decoding, shader-cache warmth, the synchronous opening call and first GPU
presentation; a warm driver cache is not evidence of a code improvement.

## Results and verification

[Raw measurements, source/binary hashes and GPU comparisons](performance-followup-benchmark.json)
identify the frozen build. These are two runs of each four-mode workload; ranges
describe observed variation, not confidence intervals.

| App CPU, percent of one core | Before | After |
| --- | ---: | ---: |
| Normal idle | 7.8–12.0% | 10.4–10.8% |
| Normal pointer and hover | 38.8–40.0% | 33.5–35.6% |
| Low Power pointer and hover | 35.5–38.4% | 22.5–25.6% |
| Reduce Motion pointer and hover | 7.1–9.8% | 7.2–7.7% |
| Both settings, pointer and hover | 8.1–9.5% | 6.9–7.3% |
| Closed | 0.08–0.12% | 0.08–0.09% |

The two-run means show about **13% lower normal pointer/hover CPU** and **35%
lower Low Power pointer/hover CPU**. Normal motion submits 59.3–59.6 frames/s;
Low Power submits 30.0 instead of about 59.4. Reduced-motion pointer events
produce about 2.6 changed-control frames/s. Settled power-saving modes submit no
source frames and have no source timer, with roughly 0.3–0.6% app CPU. Closing
also stops source submissions. Visual effects in normal mode were not reduced.

The separate normal integration workload is less encouraging: continuous tilt
without dispatched hover events measured 36.3% before and 36.6% after. Its source
preparation fell from 286 to 250 ms/s, but overall process CPU did not improve.
Idle sections and memory varied; **no consistent idle CPU or RAM improvement is
established**. Normal-workload closed footprint was 95.7/99.3 MiB before/after.
The mode workload ranged from 74–99 MiB closed and roughly 165–193 MiB open across
candidate scenarios. These numbers must not be selectively presented as a RAM
reduction.

Source preparation was 1.04 seconds in both normal runs. Synchronous opening was
235/199 ms, first GPU presentation 527/391 ms, and warm synchronous reopening
136/125 ms. These are single observations with warm shader caches, not a reliable
cold-start improvement claim. The earlier startup audit found most of the
remaining preparation time in source/metadata decoding, not shader compilation.

**Old-native-shell performance parity remains unachieved.** Its historical
measurements were about 5.2% tilt CPU and 0.1–0.3% idle CPU. The new source shell
still spends CPU on full interactive layout, projected text/accessibility and
continuous multipass rendering. A live stack sample separated drawable waits
from active CPU work; waiting for a drawable was not counted as a CPU hotspot.

Verification on the final sources:

- 66 exact GPU frames and batch states matched the complete baseline source
  closure, including depth fallback, masks, color policies and texture identity.
- Compiled packets matched generic encoding across repeated draws, changing
  camera/mask payloads and override key/shape invalidation. Numeric payload
  changes did not rebuild packets.
- Cached versus forced full frames matched for 43 ambient poses and consecutive
  positive/negative/neutral tilts, stationary continuation, ambient-off reseeding,
  clipped hits and restoration after sparse geometry writes.
- 50,520 core assertions passed, including uncached layout comparisons and
  charging localization. The separate offline Metal compiler probe was
  unavailable; runtime compilation and GPU comparisons passed.
- 623 navigation assertions covered all 16 sections; 110 lifecycle, 92 app
  shortcut and 26 backdrop assertions passed. Forced source failure restored
  the native fallback correctly.
- The stable data/localization guard and 19 mutation checks passed. No stored
  identifiers, preference domains or data formats changed.
- A live scan after closing found 0 leaks / 0 leaked bytes across 241,646
  allocated nodes. That instrumented run is excluded from the timing tables.

Removed 3.89 GiB of generated logical file content: disposable compiler caches,
superseded benchmark binaries and raw GPU frame dumps. The final native and
benchmark apps, released bundle, source snapshots, logs, measurements and pixel
hashes remain. Logical bytes removed are not a measurement of physical disk
space recovered on a compressed or cloned filesystem.

## Reproduction

Build with `scripts/benchmark-hud.sh --build-only`, using separate
`HUD_BENCHMARK_DIR` directories and the same `HUD_BENCHMARK_RESOURCE_APP` for
baseline and candidate. Run the resulting executable with `--ui-test` and
`--output <report.json>`. Add `--power-modes --verify-power-modes` for mode
comparisons, or `--verify-only` for independent frame correctness checks.
Run correctness checks and profiling separately from timings.

Use `scripts/verify-desktop-renderer-parity.py` with the baseline's full frozen
Sources directory and `--optimization geometry` for the GPU comparison. Use
`scripts/test.sh`, the native lifecycle/navigation/shortcut fixtures and
`scripts/test-integration-compatibility.py --self-test` for the remaining checks.
