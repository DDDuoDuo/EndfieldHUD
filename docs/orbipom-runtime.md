# OrbiPom offline runtime

## Authoritative scope

Roadmap requirement 11 is preserved verbatim in `implementation-roadmap.txt`:

> 11. 新增模块：Closure's Minigame（可露希尔的小游戏，可露希尔是游戏中的角色，翻译时记得别翻译错）。游戏参考等会儿发。

The October 5 follow-up supplies `Endfield-OrbiPom-Merge-20261005` and requests the same skills, physics, scoring and gameplay without sound, using HUD button styling and lightweight storage/CPU/RAM. This implementation uses the supplied original code rather than substituting a generic circular-body merge game.

## Boundaries and architecture

`OrbiPomRuntime` owns one lazy JavaScriptCore context. The context has the unchanged original Matter.js 0.20.0 factory and 74 original pure game declarations: all eleven collision profiles, their primary/extras construction and primary-body mass/inertia override, the state reducers, merge queue, danger clock, fixed simulation clock, wind runtime, skill controller and game controller. `Resources/OrbiPom/provenance.json` records every extracted declaration's source line range and hash. The complete source archive, React, browser renderer, web bridge, account services, telemetry, fonts, sounds, atlas variants and research files are not bundled.

The small adapter replaces browser rendering with a snapshot for native retained layers. Source merge/clear easing, swap Bezier path, and preview appearance timing are retained. Native artwork consists of one original PNG each for eleven characters and four skills. Matter's MIT notice is included separately; original game artwork/code remains owned by HYPERGRYPH and is not relicensed under the app's MIT license.

There is no timer, display link, asynchronous service, audio player, WebView or networking capability inside the runtime. `advance(seconds:)` is called by the visible HUD. It retains the original fixed60Hz physics and original maximum-three-step catch-up; skills use active elapsed time. Thirty rendered frames still execute sixty physics steps. Hidden/paused time never accumulates. The HUD controls pause/resume. Pausing cancels active skill selection/casting using the source controller's own restore logic and retains the stack. The native host supplies a local high score; authenticated leaderboards, official rewards and account synchronization are not exposed.

Mouse movement sends only aim/target fields across the bridge, avoiding body-array conversion at raw pointer-event frequency. Body snapshots are transferred on the presentation cadence. Fifteen images share a bounded lazy cache with explicit release. No data migrations are required for older modules.

## Input and presentation contract

- World size is230×280 with positiveYdown. Source drop preview appears above the vessel.
- `movePointer(x:y:)` aims or highlights an active skill target. `pointerUp(x:y:)` consumes active skills first, otherwise drops within vessel bounds. `drop()` handles non-repeated Space.
- `activate(.clear)` selects one exact compound target and spendsone energy. `activate(.wind)`/`.shake` arms, then an inside click confirms and spends two/three energy. `activate(.swap)` requires six accumulated spent energy and selects two distinct bodies. Repeating the active skill or `cancelSkill()` cancels. Swap charges settle only at movement completion.
- `snapshot.bodies` includes negativeID outgoing visual ghosts. Native layers draw original PNGs at maximum image dimension `size * scale`, with supplied `opacity` and rotation in radians. Preview dimensions use the same convention.
- Snapshot includes original energy progress, danger countdown, selected/hovered targetIDs and active wind surface. The HUD owns its existing accessible button geometry, tilt and transitions.

## Isolated verification

`OrbiPomRuntimeTests` runs108 assertions in fresh JavaScriptCore contexts without opening an app window or touching user data. Tests cover the source Matter hash, all eleven complete compound profiles/primary mass/inertia, sleeping and solver constants, actual Matter collision/merge deduplication, every source-level score including level11 pair removal, spawn range/anti-three repetition, drop cooldown, all four skills and cancellation/restore behavior, partial energy decay, danger timing, paused zero-work behavior, nonfinite input and equivalent deterministic30/60Hz scenarios.

On the local Apple Silicon development machine, an optimized command-line microbenchmark processed600 native-bridge frames with60 initial compound bodies and37 merges in0.493seconds (0.821ms/frame, approximately4.93% of one CPU core at60Hz). Process maximumRSS was20.3MB and peakfootprint16.7MB. Six hundred paused calls took16.8µs with unchanged state. These are engine/bridge measurements, not a full-HUD GPU/frame-time or cross-device claim. The run uses generated isolated state and does not affect the user's active game/app.

Full original React presentation, character throw sequences, streamed wind/golden celebration frames, tutorials, soundtrack and official account/reward flows are outside this native renderer. Gameplay/physics use the supplied original controller; full game-scene pixel identity and cross-device hardware performance remain unverified.

## Native HUD ownership

`OrbiPomSession` belongs to the existing OverlayController and survives section
changes and HUD close/reopen. The VM is allocated only when Start is pressed.
The only persisted value is `orbipom.bestScore.v1` in the app's existing defaults
domain; it writes only when a higher score is committed at pause/finish. Tests
inject a disposable defaults suite or no persistence. Other module data and
preferences are neither migrated nor reset.

`OrbiPomCanvas` draws on the existing tilted center plane. Its single weakly
capturing native clock runs only while the HUD is presented, interactive,
foreground, playing and not paused or confirming restart. Focus loss, sleep,
section transition, cancellation and HUD close invalidate it immediately.
Restoring the UI resets elapsed-time tracking, so hidden time does not catch up.
Low Power visual mode uses a 30Hz presentation clock with the same fixed60Hz
original physics. Sleeping body layers retain their positions without redundant
transform updates. Control faces and text update on state changes; sprite layers
are pooled by original body IDs and removed when their bodies disappear.

Buttons use the shared external highlight frame and the existing projected
native accessibility approach. Click/release drops; pointer movement aims and
selects skill targets. Space drops, arrows aim/move the skill target, keys1–4
choose the four skills, P pauses/resumes, and Escape cancels skill/restart
selection before the normal HUD close action. Repeated Space does not auto-drop.
Confirmed restart is required to discard a live stack. Event Log records only
started/restarted/finished actions, with no account information or player input.

The native smoke test uses `--ui-test --minigame-smoke-test`. It verifies actual
HUD startup, retained projected pieces, all four controls, outgoing transition
pause, no hidden simulation, manual pause, restart cancel, cancellation safety
and close/reopen session identity. The separate canvas tests cover temporary
best scores, input coordinates/cooldown, retained layer reuse, projected native
controls and teardown. Full integration totals and the current review build
are recorded in DEVELOPMENT.md.

## Signed runtime

The main app uses only Apple’s `com.apple.security.cs.allow-jit` exception for
JavaScriptCore, alongside its existing Apple Events entitlement. Development
and release signing both carry it; nested frameworks do not. No unsigned-memory
or library-validation exception is added. Apple documents JavaScriptCore’s
fast path under [Allow execution of JIT-compiled code](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.cs.allow-jit).

An isolated ad hoc **hardened-runtime** A/B probe of the identical optimized
engine executable processed the same 600 frames / 60 starting bodies / 37 merges
in 0.633 seconds without the entitlement and 0.186 seconds with it
(0.310 ms/frame, about 1.86% of one core at 60 Hz). Both paused probes retained
identical state. This single-machine engine benchmark excludes native HUD
rendering; it is not a full-app performance guarantee.


## Pause, rules and danger display (2026-10-05)

Manual pause belongs to the retained game session. Rebuilding the canvas,
switching sections, changing display configuration, opening Rules or closing
and reopening the HUD cannot clear it; Resume or a new game is explicit.
Visibility and Rules use separate temporary clock gates. Paused/game-over
presentation fades a dark board layer behind its centered status. The original
five-second danger countdown is displayed separately in red to the board’s right.

A bottom-right question mark opens the existing projected, animated retained
menu with scrollable rules and keyboard/accessibility support. It explains
merging, the score table, SP and partial-charge decay, four skills, and overflow.
The supplied official locale chunks confirm 技力 (Simplified/Traditional Chinese),
SP (English/Japanese) and 스킬 게이지 (Korean): program-audit/readable
965.fc780b.js, 162.c2d254.js,
740.a3ea73.js, 246.adcfa0.js and 675.d23493.js. These labels are shared by the
header and localized rules; original JavaScript identifiers remain unchanged.
