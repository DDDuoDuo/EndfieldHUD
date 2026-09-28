# Map interaction optimization — 2026-09-28

This is a dated comparison of the vector renderer and its raster replacement. The measured 72× scenarios remain reproducible; the default and reset zoom subsequently changed to 3×. See [Map](map.md) for current behavior.

## What was actually causing the lag

The earlier offscreen drawing benchmark missed the native compositor bottleneck. Even after local clipping, the live overview still submitted about 1,700 country, wall, mask and contour layers. Repeated zooming blocked the main thread for roughly 33 ms at the 95th percentile. The replacement's first bitmap prototype also exposed a second issue: combining every country boundary into one fallback-world path caused over 100 MiB of temporary Quartz allocations.

## Current implementation

The live canvas now retains four geographic image layers: one padded detail image and three antimeridian copies sharing one small world backdrop. Country relief, hatching, geographic edges and contours are painted directly with Core Graphics on one serial background worker. Pins and controls remain separate native layers, retaining sharp text, theme color and compositor animation.

Camera coordinates and image transforms update immediately. There is at most one in-flight paint and one latest camera request; intermediate wheel events never become a render queue. Panning reuses the padded image. Movement uses a smaller texture, with a cost-based cooldown limiting background painting; gesture end produces one full-detail image. Images are capped at 1536 × 1536 pixels (9 MiB each), the shared fallback is 2 MiB, and there is no bitmap history cache. Geographic indexes are confined to the worker and released on deactivation. Backdrop drawing uses small components and temporary indexes, avoiding large compound-stroke scratch buffers and duplicate retained geometry.

Accessibility projections update at most 12 times per second during gestures and flush exactly at their end. One pending wheel deadline replaces a new cancelled task for each sample. There is no idle render loop. Leaving Map cancels pending work and stops pin/camera animations while retaining the last image for the shell's closing animation. Obsolete data/theme results cannot publish after cancellation, and a paint completing during toolbar zoom cannot interrupt the terrain/pin animation.

Reset changes only zoom at the current location, subject to the polar bounds. It used 72× during these measurements and now uses 3×. First use starts near Shenzhen.

## Visible native comparison

Mac14,2 (M2, 8 GB), macOS 15.7.4, native arm64 Swift `-O`, 60 Hz display. Both versions used the same bundled terrain/country data, six pulsing pins, real HUD perspective/parallax transforms, and a visible nonactivating AppKit panel. Each case ran for six seconds at both 60 and 120 input updates per second. Compilation and other tests were kept outside timing intervals.

| Case | Input Hz | Process CPU before → after | Camera-update p95 before → after | Sampled physical footprint before → after |
| --- | ---: | ---: | ---: | ---: |
| Fast 72× pan | 60 | 8.6% → 7.6% | 3.91 → 0.32 ms | 88 → 109 MiB |
| World overview pan | 60 | 18.8% → 6.1% | 4.31 → 0.31 ms | 143 → 122 MiB |
| Repeated full-range zoom | 60 | 17.1% → 11.0% | 32.93 → 0.73 ms | 138 → 108 MiB |
| Fast 72× pan | 120 | 15.1% → 8.7% | 3.43 → 0.29 ms | 78 → 106 MiB |
| World overview pan | 120 | 11.3% → 7.8% | 1.29 → 0.30 ms | 162 → 96 MiB |
| Repeated full-range zoom | 120 | 15.7% → 11.7% | 32.18 → 4.92 ms | 159 → 99 MiB |

Equal-duration mean process CPU fell from 14.4% to 8.8% (39%). Kernel lifetime peak physical footprint fell from 166 to 133 MiB (20%). Peak sampled RSS fell from 259 to 225 MiB; allocator reuse makes individual RSS samples vary. A close pan uses more memory than the old small local vector view because it retains image backings, but the overview/zoom and overall peak are lower.

No input timer slots were missed in the final six cases; the old repeated-zoom cases missed 25 slots at 60 Hz and 278 at 120 Hz. Detail images continued updating during movement, and all six cases settled to a sharp 1532-pixel image after the gesture. The final camera therefore did not stay on a stale, low-resolution preview to obtain these results.

These measurements include the app-side compositor and drawing worker, but exclude WindowServer CPU/GPU work and the other HUD modules/background blur. Timer delivery and transaction costs are **not presented display FPS**. Setup, gesture-end persistence and final-detail settlement are outside interaction CPU intervals. Instruments is unavailable on this machine (Command Line Tools only).

## Reproducing the checks

Use `SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk ./scripts/stress-map.sh --visible --scenario all --hz 60,120 --seconds 6 --label current --output /tmp/EndfieldMapPerformance/live-current.json`. It uses isolated temporary map data and closes its test panel automatically; coordinate the visible run with other UI work.

The local measurement run wrote baseline and final reports to `/tmp/EndfieldMapPerformance/live-baseline.json` and `/tmp/EndfieldMapPerformance/live-raster-final.json`. These temporary reports and earlier offscreen captures are not bundled with the source; rerun the commands to produce fresh output. `scripts/benchmark-map.sh` now waits for exact asynchronous image settlement and reports that wait separately from forced Core Graphics drawing, so it cannot mistake a blank or stale map for a faster renderer.

Regression coverage includes reset/persistence, geographic wrapping and holes, north/south pixel orientation, theme changes, image-size limits, gesture backpressure, exact final detail, cancellation and immediate reopening, discarded-controller lifetime, terrain/pin coordinate alignment, interrupted toolbar animations and accessibility throttling.

Final verification: 24,325 core assertions and all 437 navigation assertions passed; native optimized build and strict signature verification passed. Native inspection confirmed upright Shenzhen coast/pin alignment, then full-HUD scrolling, dragging, 72× reset at the same geographic center, pin cleanup and section re-entry. Saved pins were preserved.
