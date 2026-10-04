# Left-side navigation and opening performance — 2026-10-03

This pass compares against `1d7a3d1`, the preceding interaction performance fix.
The scope is opening the HUD and switching System, Display, Hotkeys, and About.
The authored artwork, animation curves, durations, resolution, and frame-rate
policies remain unchanged.

## Removed work

- Settings retain their painted rows until a displayed input changes. Reentering
  a page restores its accessibility controls without rebuilding its text layers.
- Navigation consumes the settings coordinator's cached operating-system status.
  Login-item status reads run on one serial utility owner and return value
  snapshots to the main thread. Launch and activation request coalesced refreshes;
  explicit registration edits preserve synchronous errors and invalidate stale
  callbacks. The current language localizes the cached snapshot without another
  operating-system call. Unchanged status does not broadcast a redundant repaint.
  About metadata is cached per resolved language.
- The healthy source shell no longer builds an invisible native navigation/card
  duplicate or its hidden hit controls. Logical fallback state remains available;
  rendering failure builds that artwork and its controls from current state.
- Source navigation is initialized with the user's actual shortcuts, avoiding an
  immediately replaced default binding and a repeated initial caption layout.
- The existing utility preparation job now prepares exact immutable Metal render
  pipelines as well as shader functions. Preparation is capped at eight shaders
  and sixteen pipelines; this bundle prepares six. It retains no scene textures,
  geometry, drawables, or views. Opening retains its lazy fallback if preparation
  is unavailable or unfinished.
- Cold desktop loading decodes the clip library alongside the scene/profile
  graph using one additional serial worker. Each owns the original JSONDecoder.
  The join preserves error ordering; bit-pattern tests cover every authored key,
  tangent, weight, binding and sampled channel against serial decoding.

## Measurement scope

`Tests/HUDIntegrationPerformance.swift --left-tab-workload` installs the real
settings controller with an isolated preferences suite. It validates that each
selected section contains its actual Settings canvas and accessible controls.
Clicks use the rendered source buttons, with one first-visit and two repeat rounds,
then close and reopen. The fixture reads macOS login status without changing it.

The fixture records synchronous input time, first completed/presented GPU frame,
and a 10 ms run-loop probe covering both handlers and deferred work. Probe gaps
are responsiveness measurements, not display-frame timing. The synthetic 40 ms
mouse press is excluded from handler latency.

The first fixture draft omitted the optional settings controller and would have
measured placeholder pages. It was corrected before collecting comparison data.
No placeholder-page results are used here.

Normal runs finish background preparation before summoning. Separate cold-source
runs skip it, exposing initial document decoding and lazy graphics preparation.
These cases must not be conflated. This comparison does not establish parity with
the older native-only interface.

## Results

Apple M2 MacBook Air, 8 GB, macOS 15.7.4, Swift 6.1, macOS 15.5 SDK,
optimized native builds, 1470 × 956 points at 2×. Each workload ran alone,
without compilation, another HUD test, or a profiler. Values below are medians
of two normal runs per build. Repeated-tab values combine both repeat rounds.

| Measurement | Before | After |
| --- | ---: | ---: |
| First-visit tab round, largest run-loop gap | 401 ms | 34 ms |
| Repeated tab round, largest run-loop gap | 441 ms | 32 ms |
| Reopening, largest run-loop gap | 394 ms | 112 ms |
| Reopening, synchronous construction | 107 ms | 95 ms |
| Prepared first opening, first presented source frame | 367 ms | 385 ms |
| Fully cold source opening, first presented source frame | 1,388 ms | 1,137 ms |

The cold-source comparison has one baseline and two final samples. It skips
application preparation but does not flush macOS or driver caches. Its tab-round
results are excluded: the fixture could leave the asynchronously read login status
text blank. The measured first-map opening does not display that Settings row.
The committed fixture now republishes status after settling, before timing input.

All six final normal tab rounds had no probe gap over 50 ms; every baseline
round had four. Prepared first presentation did not improve reliably, so this
pass makes no universal first-frame claim. Cold first presentation improved
about 18%, and reopening's long deferred pause was removed; synchronous view
construction still takes about 95 ms on this host.

CPU during repeated tab rounds rose from 10.4% to 16.3% of one core while source
submissions rose from 21.9 to 33.8 per second: the main thread previously stopped
rendering during each blocking status query. This is a responsiveness improvement,
not evidence of lower busy CPU. Frame cadence policies are unchanged. Repeated-tab
physical footprint was 187 versus 189 MiB. Closed samples had zero source frames,
no active source timer and no layer animations; their roughly 1.4% versus 1.2% CPU
includes short-lived heap cleanup, not a long-duration idle measurement.

[Recorded samples and source fingerprints](tab-opening-performance-benchmark.json)
include the exact measured fixture fingerprint and the final production sources.
Local full reports and traces are under `build/tab-opening-performance`.

## Verification

The GPU fixture compares 66 authored frames and batch states against the baseline,
both normally and after program prewarming. Both sets match exactly, including
combined depth fallback and geometry, packet, and uniform byte checks. The shared
pipeline descriptor preserves vertex layouts, attachment formats and blending.

Regression coverage includes unchanged-page layer identity, changed status and
language, accessibility deactivation/reactivation, and deferred fallback state.
The stable data contract remains hash guarded; the controller notification/cache
and serial login-status owner have explicit reviewed exceptions rather than
replacing their baselines. Final checks passed 74,480 core assertions, 118 lifecycle,
623 navigation and 108 app-shortcut assertions, plus 21 compatibility-guard mutation
checks. Cached and rebuilt source frames also preserve hit/accessibility geometry.

The complete test script reaches the separate offline Metal source probe and stops
because this host has no `metal` compiler. Runtime Metal verification succeeded:
66 ordinary and 66 prewarmed reference frames match exactly. Instruments is not
installed; run-loop probes and GPU completion/presentation timestamps were used.

Research: Apple's [responsiveness guidance](https://developer.apple.com/documentation/xcode/improving-app-responsiveness)
recommends keeping preparation out of the input handler. The changes here reduce
actual main-thread work without shortening the visible animation.
