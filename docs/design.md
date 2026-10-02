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
| Right-hand list | Trackpad scrolling follows the gesture, with direct finger tracking, up to 96 source pixels of elastic edge travel (64 for wheel input), and a stronger finite rebound. Disabled limit arrows remain dim and reject further scrolling. Recycled rows preserve each shortcut's identity, icon and custom name. |
| Profile artwork | Retain the dark industrial default, readable right-aligned authority/MAX labels, and source decoration. Imported backgrounds stay cropped and darkened within the rounded photo panel. A saved card color remains independent of global theme recoloring. Both the compact card and profile editor use the same selected source avatar frame. |
| Map | Enlarge the fixed-resolution map plane until the two lower controls overlap it. Preserve the original button silhouettes in front and feather the circular edge. Left-click dismisses coordinates; right-clicking a pin deletes it. |
| Section changes | Use one monotonic 300 ms transition without registration jitter. Notes workspace follows the actual section swap; pinned notes remain visible. Prepared File Shelf and Add App contents are reused on activation. |
| Bottom wordmark | Clicking ENDFIELD INDUSTRIES triggers a brief, local flicker on the existing render clock. Reduce Motion leaves it steady. |
| Terminology | Use RAM for user-facing memory labels in all four languages; preserve stored identifiers and metric sampling. |
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

## Verification evidence

The integration document records the current
[checks and measured results](integration-preservation.md#measured-branch-result--2026-10-02);
[integration-benchmark.json](integration-benchmark.json) contains the recorded
samples and input fingerprints. These results apply to their measured build,
not automatically to later visual changes. Source frame submissions are not
measured display FPS. This design contract makes no unverified performance
claim.
