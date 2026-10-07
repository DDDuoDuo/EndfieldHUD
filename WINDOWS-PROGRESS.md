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
| Frozen pre-opening backdrop, no recursive capture, HUD and cursor present in saved recording | Unverified. |
| Mixed-DPI monitor movement, hot-plug and recovery | Unverified. |
| Projected native editing, Chinese/Japanese/Korean IME and native font metrics | Unverified; isolated text/coordinate contracts alone are insufficient. |
| Closed CPU/GPU/wakeups, bounded warm reopen ownership and visible frame pacing | Unverified; rerun on the corrected desktop path. |

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
The normal HUD omits the diagnostic editor; `--editor-fixture` explicitly enables
the projected in-memory TSF fixture. The footer retains its source screen plane.

Desktop tilt applies the Mac adapter's default ×1.25 factor. Ambient animation
uses the source default, enabled; `--ambient-off` selects the diagnostic case.
The visible wall clock samples only on activation and its one-second callback,
then stops on close. Projected wheel scrolling uses the original finite spring
and bounded row pool. Windows precision touchpad phases and arrow controls still
need their dedicated input adaptation and live checks.

The resource gate verifies 741 canonical Git inputs and 753 traced outputs;
754 staged files occupy 29.63 MiB. No old local checkout or staged folder is
accepted. CI checks out its exact event commit, attaches the authorized branch,
and preserves canonical file bytes and baseline history.

Remaining visual work includes the source's themed profile background/hover
artwork, matched Mac font metrics, footer/wordmark overlap comparison at the
fixture size, full material/soft-mask/HDR composition and the frozen backdrop.
Module bindings are presentation only. Their bodies, providers, persistence,
accounts, accessibility and full settings are subsequent required milestones.

## Fresh verification

The restarted Release x64 build compiles with MSVC 14.44.35207 and Windows SDK
10.0.26100.0. All seven native CTest suites pass, including 2,702 source scene
checks, 776 desktop presentation checks and 126 isolated WARP material checks.
All 73 Python packaging/source-provenance tests pass. Source wheel tests cover
analytic spring samples, rebound, invalid inputs, reduced motion and endpoint
parking; actual resource tests confirm the final navigation slot is hittable.
These are isolated contracts, not acceptance of the five live architecture gates.

[Fresh sanitized diagnostic evidence](windows/evidence/restart-desktop-2026-10-07.json)
binds the final executable and native source files by SHA-256. In the corrected
desktop path, 100 warmed source-endpoint cycles retained 592 handles and reduced
private bytes from 198,987,776 to 186,372,096 after the GPU drained. The following
61.0106-second closed interval submitted zero HUD frames and consumed 0.046875
CPU seconds. Closed handles changed from 593 to 596; longer ownership testing
remains required. These hidden endpoint/ambient-off measurements establish no
visible frame pacing, default ambient performance, GPU activity, wakeup,
recording, live IME or Mac parity claim. Both compositor capability probes
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
