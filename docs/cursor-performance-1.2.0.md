# v1.2.0 cursor movement follow-up

This follow-up removes repeated CPU work while keeping the existing pointer,
slant, tilt, transition curves, ambient motion, render resolution and shaders.
It does not claim performance parity with the old native renderer.

## What changed

- Settled moving-pointer frames retain their draw-structure identity. Their
  world matrices, numeric clipping/masking uniforms and vertex buffers still
  update every frame. The renderer continues to check compatibility within
  merged groups, and geometry registration marks affected groups dirty. A
  full pose/resource/layout change receives a new identity.
- Layout caches the final metrics of immutable image and LayoutElement inputs.
  Text and layout groups are measured in their original order on every layout
  pass, while only actual fitters/groups run the layout writers. Active-state
  filtering and zero-size fitting without a metric source remain intact.
- The pre-layout and pre-slant stages each retain one exact scene resolution.
  Scene resolution compares exact override fields, marks the changed nodes and
  their descendants, and evaluates those branches in the original traversal
  order. Unchanged branches reuse their resolved values. Failed requests do
  not commit any dependency snapshot. There is no frame history, new timer,
  reduced frame budget or coarser geometry.
- Native clock and account controls skip frame, label, value, visibility and
  enabled-state setters when the new value is equal. Localization and action
  lists are captured once per layout callback; their values still update when
  they change.

The unconditional layout/resolution path remains the verification oracle.
The renderer's generic encoder and full batch dependency checks remain the
pixel oracle.

## Correctness checks

The focused scene suite passed **245 assertions**. It compares each override
channel against the unconditional resolver, covers signed zero, invalid-input
recovery and root-parent changes, and checks that a changed leaf, changed parent,
removed override and inactive branch rebuild the correct descendants.

The focused layout suite passed **121 assertions**, including seven new checks
for changing text measurements and metric-free fitters across active-state
changes. Existing cases compare cached and unconditional layout with nested
scrolling, scale, rotation, pivot, resized parents and inactive branches.

The benchmark verification fixture additionally requires pointer packets to
retain the same draw-structure identity while their hit revision advances.
It compares actual Metal pixels for two seed poses and eight consecutive
moving/stationary poses against the generic encoder with full dependency
checks. The existing full-frame checks independently compare uploaded geometry,
all batch state/uniform values, resolved transforms, masks and hit/accessibility
geometry. The final optimized candidate passed `--verify-only` on this host;
its log is `build/cursor-performance/candidate2-verification.log`.

## Measurement method and evidence

The paired sources are frozen under `build/cursor-performance/`. The baseline is
commit `8a1123b`; the final candidate differs only in `HUDSourceScene.swift`,
`HUDSourceWatchLayout.swift`, `HUDSourceWatchFrameBuilder.swift`,
`SystemHUDView.swift` and the renderer's structure-token contract comment.
Both snapshots use the identical final benchmark fixture and staged resources.
Replacement logo providers/assets and the other UI fixes are excluded from
the CPU pair. The copied SystemHUDView also contains the fallback logo
contents-gravity correction; this workload uses the source shell and does not
exercise that fallback. The final candidate is frozen under
`candidate2-source` and its executable under `candidate2`; `candidate` denotes
the earlier layout/structure-only trial.

The machine is an Apple M2 with 8 GiB RAM, macOS 15.7.4. The benchmark uses the
macOS 15.5 SDK and `-O -whole-module-optimization`. Its `--ui-test --power-modes`
workload dispatches the same 60 Hz synthetic pointer events to both the source
view and native host while Map is selected. Normal, low-power, reduced-motion
and combined modes each include idle and moving-pointer intervals. This differs
from the older continuous-tilt benchmark, which selected Clipboard and changed
the pointer provider without directly dispatching native hover events; compare
only the matched runs here, not their absolute CPU values against that audit.
Normal motion is measured at its existing cadence; source submission rate is not displayed FPS.

CPU is percentage of one core and excludes WindowServer. Physical footprint
includes memory omitted by RSS. Each run uses temporary data and fixture
telemetry; the owner's running application and stored preferences are not
modified. Compilers and other requested verification work must be finished
before timed runs. The source hashes and snapshot delta are in
`build/cursor-performance/source-manifest.json`.

## Final matched result

All four final runs exited successfully with `--verify-power-modes`. Each mode's
pointer interval is five seconds. The sequence was baseline 3, final 1,
baseline 4, final 2, after compilation and the separate correctness run ended.
These are two samples per binary, not a statistical confidence interval.

| Pointer mode | Baseline CPU, % of one core | Final CPU, % of one core | Baseline frame prep, ms/s | Final frame prep, ms/s |
| --- | ---: | ---: | ---: | ---: |
| Normal | 33.45–34.09 | 34.17–36.06 | 216.41–218.30 | 216.02–227.46 |
| Low power | 26.92–27.66 | 22.76–23.13 | 161.82–163.24 | 130.18–132.61 |
| Reduced motion | 6.70–7.82 | 5.22–7.48 | 28.26–36.16 | 18.50–28.99 |
| Both | 7.74–7.95 | 6.66–7.57 | 35.19–35.39 | 26.78–31.44 |

Low-power pointer CPU was about **16% lower** on the mean of these paired
samples, with about **19% less frame-preparation time**, while retaining its
29.88–29.90 Hz source cadence. Normal-mode CPU did **not improve**: the final
mean was about **4% higher**, including a 36.06% second sample. Normal source
cadence remained 59.27–59.54 Hz. This small sample cannot separate all run
variation from an adverse normal-mode effect; a normal-mode benefit and a fix
for the user's reported 50% case are not established.

Reduced-motion results overlap between binaries. Combined mode was lower in
both pairs, but its second-pair CPU difference was only 0.17 percentage points.
Neither establishes a broad improvement across real pointer devices or all
modules. Reduced-motion/combined modes submitted 2.54–2.60 frames per second
for changed controls and did not keep a display timer alive.

Normal idle CPU was 9.62–9.98% in the baseline and 9.53–10.98% in the final
candidate, at about 30 Hz with the existing ambient motion. Low-power,
reduced-motion and combined idle submitted zero source frames with their timers
stopped; final process CPU was 0.99–1.16%. Closed-after CPU was 0.07–0.09% in
the baseline and 0.06–0.11% in the final candidate, with zero source frames and
no source timer. These power-mode behaviors already worked in the baseline
fixture; this follow-up does not introduce a cadence reduction.

Moving-interval physical footprint ranges overlap: baseline 180.13–192.41 MiB,
final 178.83–188.60 MiB. Closed-after footprint was 95.44–95.85 MiB in the
baseline and 95.24–96.92 MiB in the final candidate. These values do not
establish a dependable memory reduction.
No measured open interval had a run-loop probe gap above 50 ms; the final
maximum was 43.04 ms, versus 49.86 ms in the baseline. Probe gaps are not
presentation latency or display FPS.

The intended renderer reuse is exercised: normal pointer intervals reused the
structural merge plan 206–209 times in the final candidate versus 2–3 in the
baseline; low-power intervals reused it 101 times versus 1–2. Resource hashes
confirmed identical staged contents (655 files). The fixture hash, complete
source hashes and binary hashes are in `source-manifest.json`. Raw timing
reports are `baseline-power-3.json`, `baseline-power-4.json`,
`candidate2-power-1.json` and `candidate2-power-2.json`; derived ranges are in
`final-pair-summary.json`, all under `build/cursor-performance/`.

The remaining normal-mode cost is substantial. The sampled pipeline still
performs full frame/layout work when hover state changes, alongside settled
pointer preparation and native projection updates; the final normal intervals
rebuilt layout 61–62 times in five seconds. Those are the next profiling targets.
No additional architecture or timing changes were made after this round.

## Profile evidence

A separate diagnostic run sampled the first candidate during normal and
low-power pointer movement for four seconds each at 1 ms. These timings are
excluded from the paired result. Both profiles showed scene resolution across
layout/frame construction and
native clock-control layout among the repeated main-thread work. Native hover
traversal and ray-hit queries were smaller than those paths and were not the
leading target. Parent and child stack counts overlap and cannot be added to
estimate CPU percentages.

The first layout/structure-only trial did not show a dependable CPU improvement
across both runs. The profile then motivated the dirty-subtree resolver and
unchanged native-control setters. Raw samples are
`normal-pointer-events-sample.txt` and `low-power-pointer-events-sample.txt`
under the evidence directory.

## Limits

The synthetic stream covers this fixture, not every module, real high-frequency
mouse/trackpad device, live account update, large user library or other Mac.
This work does not resolve the intermittent Clipboard drawable/lifecycle stall
reported in `performance-1.2.md`; the power-mode workload selects Map.
Instruments/xctrace and the offline Metal compiler are unavailable on this host.
The checks do not establish GPU utilization, actual display FPS or long-term
leak behavior. Compiler-cache cleanup is a disk-space action, not a runtime
CPU optimization.
