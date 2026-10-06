# v1.2.0 performance audit

This pass removes unnecessary work from HUD construction. It does **not** establish performance parity with the old native interface. First synchronous opening improved in the measured pair; display-ready time and warm reopening did not. The renderer remains the main CPU cost during tilt.

## Changes

- Media Assembly keeps a lightweight layer skeleton until first presentation. It creates an `AVPlayerLayer` only when a real video player is needed; opening the HUD, an empty editor or a still image does not create one.
- OrbiPom defers preview, next-piece, skill and control artwork until its panel is requested. Its simulation, pause state and single active-game clock are unchanged.
- Reader and Calendar retain incoming render scale without drawing their unopened panels. Repeated identical scale updates do not rebuild controls.
- The account gauge defers numeral artwork until a visible value needs it, including the unlinked Work Mode fallback. Its visible countdown uses the existing HUD cadence and updates changed labels only.

There are no new background services or polling timers. These changes do not change animation curves, frame cadence, render resolution, shaders, visual effects or saved-data formats. Finite artwork setup now occurs on the first visit to these panels; this audit did not separately time every first module visit.

## Runtime audit

Media Assembly already has one lazy Core Image context, one latest pending preview behind its worker, and releases its player observers and preview caches on hide. Reader releases decoded pages and its worker on hide. Archive bounds thumbnail work to six wanted items, one decode and a twelve-image cache, then clears images when hidden. OrbiPom only advances its single simulation clock while active and playing. Its fifteen original sprite/skill images form a bounded cache, not a growing frame history.

The audit found avoidable startup artwork, rather than a second continuously running service in these modules. Compiler-cache cleanup saves local disk space; it is not a runtime CPU or RAM improvement. Authoring resources remain outside the packaged runtime resource closure.

## Method

The comparison used `scripts/benchmark-hud.sh`, compiled with `-O -whole-module-optimization`, on an Apple M2 Mac with 8 GiB RAM, macOS 15.7.4 and the macOS 15.5 SDK. The display was 1470 × 956 points at 2× backing scale. Normal ambient motion and a synthetic 60 Hz pointer stream remained enabled.

Both executables used the same isolated `--ui-test` workload: closed, Map, Clipboard, Notes, Activity Monitor, continuous tilt, closed again, and reopening. The fixture uses temporary data, three synthetic processes and no linked account. It did not replace, quit or read the owner's normal application data. No compiler, sampler or other requested profiling workload ran during the clean pair.

The baseline is a frozen working-project snapshot from before this pass, not the old native release. Candidate resources include the current navigation icons and 33-glyph account font. The benchmark snapshot predates the final account-popover input guards, legacy-system file-picker fallback, Notes pencil sizing, personal-card quote adjustment and subsequent asset cleanup. These later edits do not change the renderer or timers, but the measurements are not a test of those final UI changes. This unlinked fixture does not exercise the account-popover or file-picker paths.

CPU is percentage of one core. Physical footprint includes memory that RSS alone omits. Source submission rate is **not** measured display FPS.

## Measured pair

| Measurement | Before | After |
| --- | ---: | ---: |
| First native canvas construction | 83.51 ms | 9.44 ms |
| First complete native view construction | 225.22 ms | 155.25 ms |
| First synchronous open | 300.90 ms | 225.21 ms |
| First completed source frame | 653.96 ms | 539.16 ms |
| First presented source frame | 760.64 ms | 772.50 ms |
| Warm synchronous reopen | 188.22 ms | 258.35 ms |
| Continuous tilt CPU | 27.43% | 23.82% |
| Continuous tilt source submissions | 59.51 Hz | 59.34 Hz |
| Continuous tilt physical footprint | 186.2 MiB | 189.8 MiB |
| Closed-after CPU | 0.11% | 0.26% |
| Closed-after physical footprint | 98.2 MiB | 96.5 MiB |
| Closed source submissions | 0 | 0 |

These are single paired observations, not a statistically established steady-state improvement. The startup component reduction is consistent with removing the sampled eager initialization. The overall presentation and warm-open figures do not justify claiming faster display-ready opening or reopening. The small memory difference does not establish a RAM improvement.

The candidate Clipboard interval ended with its source clock stopped and averaged only 20.33 Hz, followed by a 189 ms Notes run-loop gap. The reason for that interruption was not established. Those lower idle CPU readings must **not** be counted as an optimization. The baseline's uninterrupted idle intervals used 6.85–7.83% CPU at approximately 30 Hz. A further uninterrupted repeated comparison is needed to establish an idle CPU trend.

Source-only review found the display scheduling and overlay controller unchanged between the measured snapshots. Scheduling can stop for hidden, minimized or occluded windows, concealed playback, or reduced/ambient-off conditions. The log records no HUD close or source-renderer failure during the affected interval, but does not capture the exact window/phase state at the stop. Therefore an intermittent lifecycle issue remains unresolved; it is not dismissed as outside interference. Candidate drawable-acquisition maximums rose to 106.4 ms in Clipboard and 183.3 ms in Notes, close to their 110.8 ms and 189.2 ms main-loop gaps; the baseline maximum through those sections was 13.7 ms. These stalls also need follow-up.

Both clean processes exited successfully. Closed phases recorded zero source frames, no active source timer and no retained animation keys. That verifies the application's submission behavior; it is not a hardware GPU-utilization measurement.

## Remaining bottleneck and limits

A separate sampled run showed that full scene/layout resolution during hover changes, pointer subtree updates, adjacent Metal batching and uniform preparation still consume CPU. Baseline source frame preparation used about 179 ms of each second during tilt, compared with 17–20 ms while idle. Much of the sampled wall time was waiting for `MTKView.currentDrawable`; waiting time must not be mistaken for active CPU work.

The historical old native renderer measured approximately 0.14–0.26% idle CPU, 5.17% tilt CPU and 44 MiB closed footprint in the earlier [parity audit](performance-parity.md). Those historical figures are not a fresh direct comparison, but this pass clearly does not close that gap. Further renderer work needs the existing geometry, hit-region and pixel-equivalence checks. No speculative renderer rewrite or reduced animation quality was included here.

Instruments/`xctrace` and the Metal command-line compiler are unavailable on this host. GPU utilization, stable displayed 60 fps, every first module visit, long-duration leaks, live account updates, real process telemetry and large user libraries remain outside this measurement. Driver shader caches, system scheduling and the user's other applications can affect startup results.

## Verification and local evidence

At the performance snapshot, the isolated core suite passed **80,816 assertions**, including six new deferred-construction assertions and the existing media, reader, calendar and minigame lifecycle/input tests. The separate Metal CLI probe could not run because its compiler is not installed. The optimized benchmark compiled and both clean runs completed without a source-renderer diagnostic failure.

Local evidence is retained under `build/v1.2-performance/`: `baseline-repeat.json`, `candidate-1.json`, their startup logs, frozen-source SHA256 manifests, the candidate resource manifest, startup/idle/pointer samples and `sample-pointer-symbolicated.txt`. The earlier sampled run is diagnostic evidence only; its timings are excluded from the comparison above.

Fixture SHA256: `118aef219b2f0f30d3db1998335ec011178bdf50c289a8d6a3db4502772e4383`.
Baseline binary SHA256: `b7dae168afb53ead2ef38d66bd719c48cd9c9aa8535b40377bebbf13467d130a`.
Candidate binary SHA256: `6d53ccd94c68af6c5fa04f53ecbf68d42bac3d8a17f8b3dfc66e291a854e3e3e`.
