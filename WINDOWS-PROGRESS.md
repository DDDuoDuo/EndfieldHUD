# Windows migration restart — 2026-10-07

Authorship: **DDDuoDuo**. Branch: `codex/windows-migration`.

The migration has restarted from a fresh clone of
[the requested GitHub branch](https://github.com/DDDuoDuo/EndfieldHUD/tree/codex/windows-migration),
at immutable commit `4036174a3facf935260f4d0a9c63bfff33b98c37`.
[WINDOWS-MIGRATION.md](WINDOWS-MIGRATION.md) remains the implementation guideline.
Its Mac source baseline is `fc429e63a2bead66f194ddb18ca947684de8fd76`.
The existing Mac sources, resources, packaging scripts and release are preserved.

## Why the previous result was rejected

The old preview rendered the raw exported Watch scene. The current Mac app
instead uses `HUDSourceWatchView(desktopMode: true)`: it mounts the canonical
`desktop-profile-card.json`, removes game-only elements and raw game text, and
adds the desktop navigation, icons, labels and shell artwork. Rendering the
correct asset bytes without this desktop adaptation did not reproduce the app.

The incorrect `windows-preview-2026.10.07-4036174a` release has been returned to
a recoverable draft. Its assets are preserved; it is no longer a public download.
The previous measurements under `windows/evidence` are historical diagnostics
of that implementation. They do not pass any restarted acceptance gate.

## Current scope and source authority

Milestone 1 is active. The desktop presentation is required to evaluate the
actual app's circle, buttons, projected input and animations; it does not imply
that the modules behind those buttons have been implemented.

- Use only the canonical GitHub checkout's approved assets and source exports.
  Staging must prove their Git blob identity and byte hashes, and distinguish
  original inputs from derived runtime files. Do not reuse sibling checkouts,
  their build outputs, screenshots, inventories or old staged resources.
- Follow the desktop loader and presentation in `HUDSourceWatchDocument.swift`,
  `HUDSourceWatchView.swift`, `HUDSourceDesktopNavigationLayout.swift`,
  `HUDNavigationTarget.swift` and `SystemHUDView.swift`. Native text and icon overlays retain the authored
  projected planes and source button hit regions.
- Keep fixtures synthetic and in memory. Do not read real app data, accounts,
  credentials or private recordings. Do not package documentation media.
- The user selected **unsigned distribution**. No signing certificate or paid
  signing service is required. This changes the signing requirement only;
  the visual, functional, data and performance gates still apply.

## Restarted milestone 1 gates

| Architecture check from the guideline | Acceptance status |
| --- | --- |
| Current desktop circle/buttons, exact open/close, live tilt and matching projected hit testing | Desktop adaptation implemented and isolated contracts checked; live matched Mac acceptance unverified. |
| Frozen pre-opening backdrop, no recursive capture, HUD and cursor present in saved recording | Frozen snapshot and explicit tint/vignette fallback implemented; live desktop freshness and recording unverified. |
| Mixed-DPI monitor movement, hot-plug and recovery | Unverified. |
| Projected native editing, Chinese/Japanese/Korean IME and native font metrics | Unverified; isolated text/coordinate contracts alone are insufficient. |
| Closed CPU/GPU/wakeups, bounded warm reopen ownership and visible frame pacing | Hidden endpoint and closed CPU checks repeated on the corrected path; live acceptance unverified. |

No gate is accepted merely because the app compiles, a COM capability probe
succeeds, or an offscreen WARP/unit test passes. Record fresh test results and
their limits here after the restarted implementation has been built and checked.

## Implemented on the restarted desktop path

The canonical 45-node BP13 profile card is mounted into the original Watch
document, with its original textures/materials and the Mac desktop default
background selection. The game-only subtrees and raw game text are suppressed.
Original scene controls, rectangular masks and source IDs drive drawing and
hits. The current desktop navigation uses the original icon mappings, native
action silhouettes and five catalog languages, with bounded source row recycling.

Native captions, header, profile fixture and digital clock use the source
projected planes. The clock preserves `HH:mm:ss` and `EEE MMM d` with uppercase
weekday, right alignment, tabular digits and source frame/indicator geometry.
The portrait is the source's native no-image silhouette; profile fields are
synthetic and carry no linked UID. Source accent bindings use the default FAD41F.
The card background and normal-alpha hover plate are derived from the canonical
normal_1 BGRA pixels using the source's byte-domain chroma and rounded-panel
operations. A bounded cache retains at most eight accent pairs; pointer/fade
updates reuse those textures. Matched Mac antialiasing at the boundary is open.
The normal HUD omits the diagnostic editor; `--editor-fixture` explicitly enables
the projected in-memory TSF fixture. The footer retains its source screen plane.

Desktop tilt applies the Mac adapter's default ×1.25 factor. Ambient animation
uses the source default, enabled; `--ambient-off` selects the diagnostic case.
The visible wall clock samples only on activation and its one-second callback,
then stops on close. Projected wheel scrolling uses the original finite spring
and bounded row pool. The source's outer scroll arrows retain their projected
hit planes and grey disabled styles. Release on the same arrow advances 32
logical points through the source spring; disabled arrows consume the press.
Windows precision touchpad phases and live input acceptance remain open.

Display selection uses the source's fixed/active/main policy and full display
bounds. An open HUD retains its current connected monitor through topology
notifications. Windows device paths provide stable identities; automatic mode
can still use virtual displays without them. PMv2 HWNDs supply current DPI.
The saved-display settings UI is a later module; mixed-DPI and physical hot-plug
acceptance remain unverified. The isolated editor has Tab/Shift+Tab focus entry
and exit; native moves invalidate TSF screen rectangles without flattening it.
Text edits now request both UIA text and Value property notifications, and caret/
selection changes request their own event only when the state changes. Native
Unicode double-click word selection and triple-click paragraph selection retain
shaped UTF-16 clusters and drag granularity. Actual CJK IME, Narrator delivery and
Mac linguistic/font comparisons are still unverified.

The normal opening now prepares a memory-only frozen SDR snapshot while the HUD
is hidden. Physical monitor bounds, visibility, cancellation and advanced-color
capabilities are checked before accepting it. Generation tickets reject late
results; the source clocks remain held until acceptance or the three-second tint
fallback. The first opening frame is committed while hidden, avoiding reuse of
the previous presented surface. Close and display/device changes release the
captured CPU pixels and three GPU textures. There is no capture/export loop.
Diagnostics provide generated pixels and never invoke desktop capture.

This is the guideline's Windows fallback for the Mac desktop's native live blur.
It retains source darkness/vignette controls and their exact alpha envelope;
the quarter-resolution Gaussian is explicitly a prototype, not a measured Mac
kernel. GDI has no ICC conversion or arbitrary z-order exclusion. Enabled advanced
color and unsupported capture providers use tint instead. Cancellation cannot
interrupt an executing GDI call: the deadline bounds fallback/publication, while
worker completion and composed-desktop freshness remain live acceptance items.
See [the backdrop contract](windows/render/FROZEN-BACKDROP.md).

The resource gate verifies 741 canonical Git inputs and 753 traced outputs;
754 staged files occupy 29.63 MiB. No old local checkout or staged folder is
accepted. CI checks out its exact event commit, attaches the authorized branch,
and preserves canonical file bytes and baseline history.

Source fragments now accumulate in a BGRA8 sRGB intermediate, preserving linear
source blending. A separate final pass converts that result to the encoded
premultiplied format required for Windows presentation. This is the source LDR
path; complete HDR/backdrop/material and matched Mac acceptance remain open.

Remaining visual work includes matched profile boundary and Mac font metrics,
footer/wordmark overlap comparison at the
fixture size, full material/soft-mask/HDR composition and live backdrop fidelity.
Module bindings are presentation only. Their bodies, providers, persistence,
accounts, accessibility and full settings are subsequent required milestones.

## Fresh verification

The restarted Release x64 build compiles with MSVC 14.44.35207 and Windows SDK
10.0.26100.0. All eleven native CTest suites pass, including 2,730 source scene
checks, 797 desktop presentation checks, 250 isolated WARP source material checks,
83 final presentation checks, 41 source monitor-policy checks, 338 native editing
contracts, 16,413 synthetic backdrop checks and 161 fake-clock readiness checks.
The Python packaging/source-provenance suite runs 78 tests: 77 pass locally and
one symbolic-link fixture is skipped because the local account lacks that
creation privilege. Windows short/long path aliases and real junction rejection
are checked. CI's first restarted run exposed a short-name temporary-path
mismatch in provenance. The corrected [CI run](https://github.com/DDDuoDuo/EndfieldHUD/actions/runs/37682014773)
passed native/resource/Python/developer-package and unsigned MSIX format checks
at commit `f4868da22b8afe972c989b3eeac9f8d0d78020ae`. The subsequent profile,
arrow, monitor and linear-composition implementation at
`998979dd0d43db550b627e6a54ca1f7e534bc4db` also passed its own
[fresh CI run](https://github.com/DDDuoDuo/EndfieldHUD/actions/runs/37685248165):
all nine native suites, all 78 Python tests (no runner skips), canonical resource
verification, developer ZIP and unsigned MSIX format validation. The subsequent
frozen backdrop and editor implementation at
`f7f3411046208fc24a5783fe6e0650e5b4e2a4f3` passed its
[fresh CI run](https://github.com/DDDuoDuo/EndfieldHUD/actions/runs/37689898752):
all eleven native suites, all 78 Python tests (no runner skips), canonical
resource verification, developer ZIP and optional unsigned MSIX format checks.
The unsigned developer ZIP is 18.36 MiB downloaded and 31.00 MiB extracted. It contains no
diagnostic images, recordings, symbols or user data. This is reviewable prototype
packaging; consumer release and installation acceptance remain unverified.
Source wheel tests cover
analytic spring samples, rebound, invalid inputs, reduced motion and endpoint
parking; actual resource tests confirm the final navigation slot is hittable.
These are isolated contracts, not acceptance of the five live architecture gates.

[Fresh sanitized diagnostic evidence](windows/evidence/restart-desktop-2026-10-07.json)
binds the final executable and native source files by SHA-256. In the corrected
desktop path with generated frozen SDR pixels, 100 warmed source-endpoint cycles
retained 592 handles. Private bytes changed from 220,020,736 to 226,893,824 and
working set from 135,860,224 to 134,926,336 after GPU drains. The 6,873,088-byte
private-memory increase is a measured open regression/ownership question; bounded
process RAM is unverified. Each close released all three backdrop textures and
all retained snapshot bytes. The following 61.0077-second closed interval
submitted zero HUD frames and recorded 0 CPU seconds in process-time counters.
Closed handles changed from 593 to 596; longer ownership testing
remains required. These hidden endpoint/ambient-off measurements establish no
visible frame pacing, default ambient performance, GPU activity, wakeup,
live capture, recording, live IME or Mac parity claim. Both compositor capability probes
returned success; their backdrop/recording/performance comparison is still open.

## Remaining plan

After the five architecture checks pass, follow the guideline's sequence:
shell and scene; versioned data and editing; local modules; system providers;
official account integrations; then full verification and packaging. Every
required module and original behavior remains in scope. Saved apps and Add App,
profile sync locks, data units/codecs, provider capabilities, accessibility,
safe updates and recording must retain the documented contracts.

A new Windows consumer release is not ready. Publish only after the corrected
implementation passes the applicable target-laptop checks; do not relabel the
withdrawn preview or its historical evidence as the completed migration.
