# Integration and polish — 2026-09-28

This is a historical checkpoint before the later map raster renderer, 3× default, and lifecycle changes. Its counts and measurements describe that build, not the current release. See [Map](map.md), [map performance](map-performance.md), and [TESTING.md](../TESTING.md) for current behavior and validation commands.

This checkpoint added no app features or release archive. Its native development bundle was built at `build/dev/EndfieldHUD.app`.

## Changes

- Map: removed the visible gesture instruction and place labels; default/reset is Shenzhen at **72×**. Schema 3 migrates the old starting camera once, preserves custom cameras and all pins, and allows a later user-selected 24× view to persist.
- Notes: the native text editor now follows committed parallax and scale changes without losing draft text, selection, or wrapping. Notes and app-rename editor edges follow the theme. Image selection stays above the HUD.
- File Shelf: Reveal in Finder waits until the close animation and panel teardown finish. The security-scoped reference remains valid throughout this handoff. Cancelling an outgoing drag still restores the same shell.
- Work Mode, Volume and Hotkeys: destroying their canvases also releases visible-only timers, device observation, animations and shortcut capture. Work sessions and per-app audio routes remain owned by their persistent controllers.
- Activity: removed duplicate tab renders, unchanged scroll-edge redraws and repeated sorting notifications. Network CSV decoding compacts the input buffer once per chunk instead of once per line.
- Audio and Event Log: unchanged gain no longer queues redundant worker updates; an already saved log revision no longer rewrites its archive. Audio routes stay alive at 100%.
- Accessibility: retained action proxies reuse projection closures and skip unchanged frame, text, help and font writes.
- Icons: the 30 faction atlas selections ship as prepared 512px assets; the original 59-megapixel atlas stays in the source tree but is omitted from the runtime bundle. The final icon-variant cache is bounded to 32 MiB. Artwork is unchanged.

## Validation

**24,154 core assertions passed.** All nine graphical checks passed:

- `smoke-test`: charging-stop display and timeout, preview retention, position commit/discard, overlay timeout, reconnection race, persistent display, monitor restart, suspended charging and multi-reason wake
- `navigation-smoke-test`: 437 navigation assertions; all 15 sections; unchanged windows, panel, shell and center host; 10 uninterrupted ambient tracks; one settled center and at most two during swaps; latest request wins; stationary-pointer opening and refreshed reopening poses; close-during-swap cleanup; safe immediate reopen; zero hidden animations
- `system-smoke-test`: 8 full Power cycles; rapid repeats; focus-close queued while opening; cancellation generations; data updates; one shared panel; ten bounded ambient tracks; thirteen pointer planes; zero hidden animations; position-edit exclusion; persistent charging HUD restored through AppDelegate
- `charge-badge-smoke-test`: 16 charge HUD assertions; delayed entrance, fixed three-second hold, circular hit area, hover expansion/retraction, section continuity, clean closing and cancellation
- `work-smoke-test`: 20 Work Mode HUD assertions; countdown completion while hidden; continuing stopwatch; pause/resume/reset; suspension catch-up; stable shell; one visible timer track; audio observers and animations removed on hide
- `event-log-smoke-test`: 13 Event Log HUD assertions; real navigation and timer actions; retained shell; private history; close/reopen lifecycle
- `app-shortcut-smoke-test`: 21 app-shortcut HUD assertions; shared shell, close-first launch, duplicate-click gate, cancellation, errors and retry
- `telemetry-smoke-test`: 20 telemetry HUD assertions; default Power, explicit Activity selection, retained shell, retained history, Settings handoff, foreground/background readers, clean close/reopen
- `notes-shelf-smoke-test`: 25 Notes/Shelf HUD assertions

The live navigation check covers all 15 sections, one persistent panel/shell/center host, bounded transition content, stationary-pointer opening, interrupted transitions and clean re-entry. Closing checks confirm zero remaining owned HUD animations. Regression tests cover native editor lifetime/projection, shelf reference access through handoff, settings capture cleanup, Work Mode visibility ownership, audio gain routing, clipboard change-count gating, bounded persistence and map migration.

The system smoke test's old 12-plane expectation was corrected to the current 13 planes, including the profile background. Shelf cancellation waits now derive from entrance/exit durations with a scheduling margin. Production animation timing was preserved.

Real hardware Focus changes and per-app audio attenuation were not repeated in this pass; their existing synthetic, controller and lifetime checks passed. The registered physical summon shortcut was not revalidated using synthetic keyboard input.

## Performance

Machine: Mac14,2, 8 logical CPUs, macOS 15.7.4. Native arm64 development build, **-Onone**, SDK 15.5. These are short measurements of this app process, using cumulative CPU time and 1 Hz RSS readings. CPU percentages are relative to one core. They exclude WindowServer and helper processes. RSS is not the complete graphics/compressed-memory footprint.

The normal app used the saved profile, background, pinned note and ambient settings. Automatic closure on focus loss was temporarily disabled for open samples, visibility was checked before/after, and the setting was restored. Initial open samples without verified visibility were discarded. No before/after speedup is claimed from them.

| State | Duration | Average CPU | RSS range |
|---|---:|---:|---:|
| System idle | 20 s | 0.25% | 31.0–31.9 MiB |
| Map idle, 72× | 20 s | 0.25% | 37.8–39.4 MiB |
| Activity overview | 20 s | 2.00% | 42.7–47.6 MiB |
| Activity apps | 20 s | 2.00% | 45.9–49.8 MiB |
| HUD closed | 30 s | 0.07% | 30.1–45.5 MiB |

Activity continues its low-cadence history sampling when hidden; clipboard monitoring checks the pasteboard change counter. The apps network helper was no longer running after closure. Visible clocks, Work Mode display ticks, device UI observations, map pin tracks and ambient/pointer animations have explicit teardown paths. No system statistics are sampled at display refresh rate.

Instruments/xctrace is unavailable: this machine has Command Line Tools and no Xcode/Instruments installation. `sample` and `leaks` were used instead. **GPU utilization and actual 60/120 Hz frame pacing were not measured.** The lifecycle assertions establish animation cleanup and retained shell behavior, not a measured frame-rate guarantee.

## Memory checks

- Isolated repeated-lifecycle run with `MallocStackLogging=1` and `leaks --atExit`: **0 leaked bytes**, after eight full cycles plus interruption/reopen cases. Stack-logging overhead makes its footprint unsuitable for performance comparison.
- Normal interactive run after accessibility inspection: **102 allocations / 2,416 bytes** reported, including small Foundation arrays and AXObserverCookie objects. Allocation stacks were unavailable for that process. Framework ownership is plausible but unproven; this is not claimed as zero leakage in every interaction path.
- Normal process physical footprint after closing was 80.5 MiB; its session high-water mark was 662.1 MiB. Most transient rendering memory was released. No claim is made that peak graphics memory has been eliminated.

## Space cleanup and development

Removed **2,301,718,528 allocated bytes** of old compiler caches, obsolete test binaries/snapshots, reference-review captures and superseded app bundles. A single native compiler cache was regenerated and kept for continued development. The project is now about **238 MiB**, including that 208 MiB cache; net project-space savings are approximately **2.1 GB**. User Application Support data, input artwork, source/tests, current app and historical release archives were preserved. This pass's temporary test/compiler directory was also removed after validation.

`DEV_OPTIMIZATION=-O ./scripts/dev.sh` is now available for future native optimized profiling without packaging; regular development still defaults to `-Onone`. Faction crops can be regenerated using `scripts/prepare-faction-icons.swift`. No commit, upload, release ZIP or DMG was created.
