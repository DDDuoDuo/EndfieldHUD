# Desktop HUD design

This is the current visual and interaction contract for the integration branch.
Record accepted design changes here as they happen; implementation details and
verification evidence belong in [integration preservation](integration-preservation.md).
The [game UI reference](reference/game-ui-reference.md) records the extracted
source behavior and its limits. Desktop adaptations below are deliberate.

## Current contract

The source watch scene supplies the shell, depth, opening and closing motion,
and navigation artwork. Existing native modules supply the desktop functions,
editable content, accessibility and drag/drop. Switching sections keeps one
shell and the existing feature controllers.

| Area | Accepted design |
| --- | --- |
| Side navigation | Keep the authored neutral gray/white hover colors. Theme changes must not recolor the hover faces. Reduce luminous edge intensity while retaining neutral source shadows and the authored depth response. |
| Bottom-left profile card and exit | Exit retains subtle source feedback. Profile hover covers the whole rounded card plate, with a faint interior wash and a stronger outer edge. Its outer frame follows the saved profile color; the photograph stays neutral. The card opens the existing profile; exit uses the existing quit confirmation. |
| Top-right clock | Keep the clock, date and Work Mode status on the source banner plane, with the 340-point plate offset left and down from the banner anchor. Preserve its source entrance and projection. |
| Central modules and confirmations | Native feature planes follow the same source tilt and projection. Quit and layout-preview recovery use the central source projection with safe placement; their visible bounds, pointer hits and accessibility bounds agree. |
| Pointer and motion | One source playback clock owns shell motion and tilt. Preserve user motion settings and existing editor/drag locks. Opening a dropdown alone does not stop tilt. Desktop rings vary their signed rates per opening, and triangles spin independently; source-reference fixtures retain the authored motion. |
| HUD cursor | Use the original Endfield cursor throughout opening, the native center and source shell, and closing. Native text editors keep the text-selection cursor; leaving or hiding the HUD restores ownership to AppKit. |
| Battery notification | Localize the shared popup/HUD title: CHARGE MODE / 超充模式 when external power is connected, POWER MODE / 电源模式 otherwise. Plug/unplug state drives the title; actual charging remains a separate diagnostic value. Preserve the capsule geometry. |
| Right-hand list | Trackpad scrolling follows the gesture, with direct finger tracking, up to 144 source pixels of elastic edge travel (96 for wheel input), capped at 24% of the scroll range, and a stronger finite rebound. Disabled limit arrows remain dim and reject further scrolling. Recycled rows preserve each shortcut's identity, icon and custom name. |
| Profile artwork | Retain the dark industrial default, readable right-aligned authority/MAX labels, and source decoration. Imported backgrounds stay cropped and darkened within the rounded photo panel. A saved card color remains independent of global theme recoloring. Both the compact card and profile editor use the same selected source avatar frame. |
| Map | Enlarge the fixed-resolution map plane until the two lower controls overlap it. Preserve the original button silhouettes in front and feather the circular edge. Left-click dismisses coordinates; right-clicking a pin deletes it. |
| Section changes | Use one monotonic 300 ms transition without registration jitter. Notes workspace follows the actual section swap; pinned notes remain visible. Prepared File Shelf and Add App contents are reused on activation. |
| Bottom wordmark | Clicking ENDFIELD INDUSTRIES triggers a brief, local flicker on the existing render clock. Reduce Motion leaves it steady. |
| Terminology | Use RAM for user-facing memory labels in all five languages; preserve stored identifiers and metric sampling. |
| Feature scope | Preserve desktop actions, module identities and saved data. Only explicitly requested interaction/copy refinements and the wordmark Easter egg extend the existing contract; no migration is required. |

Saved scale and position apply once. Native editors, notes, confirmations and
their input surfaces must follow the visible projection, including during
closing. External handoffs continue after the closing animation. Closing tears
down presentation work while existing persistent services keep their established
lifetimes. The detailed data and lifecycle boundaries remain in
[integration preservation](integration-preservation.md#identity-and-saved-data).

## Decision history

Entries record accepted direction, not a claim that every change has passed
visual or performance verification. Later entries supersede earlier direction
where they conflict.

- **2026-10-02 — Integrated shell and native behavior:** Retain the source shell
  and unified tilt while preserving native feature controllers, confirmations,
  texts and persistence. Keep the adaptation within existing feature scope.
- **2026-10-02 — Side-hover correction:** Replace the earlier theme-colored
  hover direction with the authored neutral colors. Keep the luminous edges
  dimmer and the outside shadows neutral.
- **2026-10-02 — Small control feedback and balance:** Add restrained hover
  highlights to the bottom-left profile card and exit control, and move the
  shorter top-right clock plate slightly left.
- **2026-10-02 — Design record:** Keep this contract and dated decisions current
  alongside visual and interaction changes, with implementation and measurement
  details linked rather than duplicated.

- **2026-10-02 — Card, map and transition refinement:** Whole-card hover and
  profile-colored border; one source avatar frame in both profile locations;
  clock further inset/down; stronger bounded right-list rebound; larger,
  feathered map under original button silhouettes; dismissible coordinates and
  right-click pin deletion. Remove section jitter and synchronize free notes
  with section swaps. Add the requested wordmark flicker and RAM terminology.

- **2026-10-02 — Final interaction pass:** Localize both battery-banner states;
  extend finite top/bottom scroll travel; register the original cursor across
  source and native surfaces, including entrance/exit. Remove render-frame cursor
  polling and avoid unchanged hover/logo style copies. Clear obsolete local
  compiler caches while retaining build dependencies and verification evidence.

- **2026-10-03 — Performance preservation:** Keep the exact released motion,
  effects, frame cadence and render resolution. Reuse unchanged scene/image and
  draw preparation, defer hidden fallback artwork, shorten JSON loading, and
  release replaced portraits and unused allocator pages after close. Retain live
  caches for reopening and preserve existing saved-data contracts. The
  [performance investigation](performance-parity.md) records exact GPU comparisons
  and measured gains; old-native-shell CPU/RAM parity remains an open limitation.

- **2026-10-03 — CPU bottleneck follow-up:** Activity Monitor uses the source
  Report button's glyph and shadow. Chinese charging banners read 超充模式.
  Normal motion retains its existing cadence, curves and effects. Low Power
  limits finite source motion to 30 Hz and stops settled ambient work; Reduce
  Motion redraws changed controls without a continuous pointer animation clock.
  Reuse settled pointer geometry and prepared draw state with exact invalidation
  for content, layout, resource and shader changes. The
  [follow-up measurements](performance-followup.md) record the tested result.

- **2026-10-03 — Interaction performance regression:** Preserve the exact
  artwork, projection, motion curves, normal frame cadence and transitions.
  Remove repeated caption/layout work, prepare accessibility geometry when
  queried, and keep app metadata/icon lookup out of input handlers. Retain
  bounded resources and the existing saved-data contract. The
  [interaction investigation](interaction-performance.md) documents the live
  Apps workload, underlying changes and measurement limits; old-native
  performance parity remains unachieved.


- **2026-10-03 — Left tabs and opening:** Keep existing presentation and motion.
  Reuse unchanged Settings rows, move passive login-status IPC off the main
  thread, and materialize hidden native fallback artwork only on recovery.
  Prepare bounded immutable pipelines before opening and overlap independent
  clip/scene decoding with the same numeric decoder. The
  [opening investigation](tab-opening-performance.md) records measurements and
  preserves the distinction between prepared and completely cold opening.

- **2026-10-03 — Center icon glow:** Activity Monitor retains the authored Report
  glyph, shadow, size and tilt plane, with a soft white glow to match Storage.
  The glow is baked once into a cached 84 × 84 bitmap; it adds no blur pass or
  animation timer to the frame loop.

- **2026-10-04 — Menu-bar file drops:** The status icon accepts file and folder
  references into the existing Temporary File Shelf. Successful drops reveal the
  shelf through the normal HUD entrance or section transition, after native drag
  delivery finishes. An in-progress entrance/exit finishes before the shelf
  request is presented. The menu retains native click behavior, and the drop
  target adds no polling or idle animation.

## Verification evidence

The integration document records the current
[checks and measured results](integration-preservation.md#measured-branch-result--2026-10-02);
[integration-benchmark.json](integration-benchmark.json) contains the recorded
samples and input fingerprints. These results apply to their measured build,
not automatically to later visual changes. Source frame submissions are not
measured display FPS. This design contract makes no unverified performance
claim.

## v1.2.0 account and module contract — 2026-10-05

The Chinese requirements remain verbatim in [the roadmap](implementation-roadmap.txt).
New modules share the retained shell, source tilt, bounded secondary menus,
control feedback and existing clocks. Archive, Reader, Media Assembly, Projection,
Calendar, Now Playing and OrbiPom preserve their documented lifecycle limits.
Menus consume an outside click before a control behind them can activate.

The right navigation is row-major: Notes / Temporary File Shelf; Clipboard /
Archive; Media Assembly / Minigame; Now Playing / Volume; Projection / Reader;
Work Mode / Calendar; Map / Event Log; Personal ID / Account binding; Battery /
Add shortcut. Saved app shortcuts follow Battery, with Add App always last.
Projection uses the original 塔晶集换 icon, Event Log uses 问卷, and Personal ID
uses 好友 from the original game resources.

The sanity wallet mirrors the ENDFIELDHUD heading and retains the original
HarmonyOS Sans SC Medium numerals. Hover gives neutral grey feedback. Clicking
opens the dark two-row recovery menu shown in the supplied 13.09.24.02 reference:
下次回复 and 全部回复, with an explicit refresh action. It shares the wallet's tilt
and has a finite reveal/dismiss animation; Escape and outside click dismiss it.
Native accessibility actions expose the same values and refresh availability.

Recovery advances locally from timestamped snapshots: 432 seconds per Endfield
point and 360 seconds per Arknights point. The existing visible HUD clock checks
for an API refresh every ten minutes while account/sanity content is relevant;
fresh data is reused when reopening. Manual refresh is bounded, closing cancels
reads, and no account timer runs in the background. In-game spending and bonuses
become visible at the next API refresh. Missing recovery timestamps stay unknown.
See [account linking](account-linking.md) for regional support and verification.

Unopened media/minigame/reader/calendar panels defer artwork, and Media Assembly
creates its video layer only for an actual player. These construction changes do
not alter the authored motion or animation cadence. Performance claims require
measurements from the current build, separately from older baselines.
