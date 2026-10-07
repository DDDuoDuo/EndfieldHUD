# Windows migration progress — 2026-10-07

Authorship: **DDDuoDuo**. Branch: `codex/windows-migration`. Source baseline: `fc429e63a2bead66f194ddb18ca947684de8fd76`; handoff branch started at `4079363c4a34d09f60f3f593b8b4f1cb82d9b518`.

**Milestone 1 is in progress. The full app is not migrated and a Windows consumer release is not ready.** This log supplements [WINDOWS-MIGRATION.md](WINDOWS-MIGRATION.md); its original requirements remain authoritative and unchanged. No required module is removed or narrowed to fit this prototype. Mac sources, scripts, resources, version, releases and update feed remain unchanged.

## Implemented and verified

- Native Windows-only C++20/CMake x64 executable, static MSVC runtime, per-monitor-v2 manifest, single lifecycle owner, tray activation, Explorer restart handler, keyboard-layout-aware Ctrl/backtick registration with conflict labels, suspend parking, and idle/closed frame scheduling. Live shell interaction and OS-change acceptance still need testing.
- Source scene loader: 789 original nodes, exact decimal signed IDs, source Hermite/stepped/weighted curves, finite OutQuad clocks, camera/FOV/canvas arithmetic, quaternion gyro, original sprite and triangle geometry, source-mask inverse hit testing and shared frame transforms. Pointer-only reprojection retains geometry allocations. Source flicker primitives are ported but their shell integration is pending.
- TSF `ITextStoreACP` context, DirectWrite cluster layout, UTF-16 model, bounded undo/redo, composition grouping, caret/selection and projective hit/candidate extents. The synthetic editor is drawn into a cached GPU surface on the same source-plane mapping as its hits. No child Edit HWND is created. Live CJK IME, accessibility and rich formatting remain unverified/incomplete.
- Both DirectComposition and Windows.UI.Composition create desktop targets successfully on this laptop; DispatcherQueue, TSF and seven selected Latin/CJK/emoji fonts are available. DirectComposition is **provisional**, pending visual/performance/backdrop/recording comparisons.
- Isolated tests: three native CTest suites pass; 509 scene checks include actual source resources; 37 Python packaging tests pass. Tests use synthetic content and temporary files and do not read app data or accounts.
- Windows staging reuses the unchanged desktop Watch packer and verifies retained bytes. The Watch source tree is 354.15 MiB; its selected runtime payload is 11.74 MiB. Complete Windows resources contain 750 files, 28.33 MiB, including ten byte-exact decoded metadata inputs. No GIFs, recordings, user data, extraction intermediates or SDF atlases enter the package. Matter.js 0.20.0 and its MIT notice are pinned by exact hashes; the game adapter is not implemented.
- Preview-only packaging produces per-file hashes, actual size/dependency reports and Authenticode status. Consumer packaging rejects incomplete or changed evidence and unsigned binaries, and remains unavailable until a signed installer/updater is implemented and tested. A new Windows-only CI workflow builds/tests/stages a developer preview; its first remote run must be checked separately.

## Target laptop and measured limits

Windows 11 x64, OS build **26200**, Intel Core Ultra 7 251HX, NVIDIA RTX 5060 Laptop GPU plus Intel Graphics, RAM **16,573,128,704 bytes**. One attached display: 2560×1600, **144 DPI**, NVIDIA reports 165 Hz. Mixed-DPI and unplug/recovery tests cannot be represented by this single-display measurement.

Toolchain: Visual Studio Build Tools 17.14.37710.0, MSVC tools 14.44.35207/compiler 19.44, Windows SDK 10.0.26100.0, CMake 3.31.6-msvc6, Python 3.12.14. No third-party native runtime or background browser is added.

Sanitized measurements and executable hash are in [windows/evidence/feasibility-2026-10-07.json](windows/evidence/feasibility-2026-10-07.json). Raw local outputs stay ignored under `windows/evidence/local`.

| Scenario | Measured result | Interpretation |
| --- | --- | --- |
| Hidden GPU source/editor fixture; 5 warmups + 100 cycles | 105 submitted frames, 1.481 s total; handles 563 → 563 | Native composition/submission and projection checks pass. This is not visible animation pacing or an actual module-switch test. |
| Private bytes across those 100 cycles | 152,805,376 → 161,763,328 | An 8.54 MiB rise remains to be profiled. Bounded settled-cache RAM and GPU memory are unverified. |
| Working set across those cycles | 98,865,152 → 108,068,864 | Hidden-test working set only; not a heavy-module or consumer-app benchmark. |
| Closed after warmed graphics | 61.024 s, **0 additional HUD frames**, 0.03125 process CPU seconds | The real message loop parks rendering while retaining the warmed renderer. No clipboard, account, map, game, media or backdrop service runs in this prototype. |
| Closed retained memory | Working set 87,023,616; private bytes 140,689,408 | One observation; longer settled-cache and device-loss measurements still required. |
| GPU activity, wakeups, visible p50/p95, input/opening latency | Unverified | Requires trace/profiler and visible-window measurements. No performance-parity claim. |

The hidden probe exposed a temporary-HWND null-pointer bug; it was fixed and the full diagnostic completed successfully afterward. Selection dragging now updates the shared tilt pointer before editor dispatch; suspend resets the stopped timer state; DPI/resize events schedule presentation; TSF initialization failures are reported. Windows asset staging also needed UTF-8 mode, extended paths and bounded retries for transient scanner locks; the original Mac packer was not edited.

## First feasibility gate

| Required gate | Status | Remaining work |
| --- | --- | --- |
| Transparent source circle/buttons, exact opening/closing, live tilt and hits | **Incomplete** | Geometry/camera/clips/hit contracts are tested; original HLSL materials/textures/HDR, layout writers, button controllers, shell flicker and visible Mac comparison remain. The current outline renderer is a diagnostic, not visual parity. |
| Frozen backdrop and recordable HUD/custom cursor | **Unverified** | No desktop capture performed. Prototype pre-opening capture/guarded refresh, test recursion and saved Windows recordings, implement/test cursor ownership. Current cursor is native. |
| Mixed-DPI selection, moving/unplugging screen | **Unverified** | Physical-coordinate/per-monitor shell exists; only one display is connected. Explicit selected-monitor settings and real hot-plug acceptance remain. |
| Tilted editor with CJK IME, caret, keyboard and font comparisons | **Partially verified** | TSF/model/DirectWrite/shared projection checks pass. Live IME composition/candidates, marked-text decoration, scrolling, UI Automation and Mac metrics are pending. |
| Closed parking, bounded reopen memory, smooth pacing | **Partially verified** | Closed frame submissions and CPU measured; 100 hidden cycles pass ownership/handle checks. Private-byte rise, GPU budgets and visible frame pacing remain unresolved. |

The five gates have not passed as a set. Module porting and consumer shipment cannot proceed on a claimed completed architecture; continue resolving milestone 1 before advancing the handoff's sequence.

## Required modules and data work remain

| Required scope | Current Windows status |
| --- | --- |
| System, Display, Hotkeys, About; theme/localization/navigation/profile shell | Partial feasibility shell only. Full settings, five-language refresh, module order, logos/card and confirmations pending. |
| Offline import/settings codec; Notes, Shelf, Clipboard, Archive; profile locks | Not implemented; starts after feasibility passes. Preserve date epochs, UTF-16 rich runs, large string IDs, unknown fields, atomic backup/rollback and unresolved Mac references. |
| Reader, Media Assembly, Projection, Map, Calendar, Event Log, app shortcuts, OrbiPom | Not implemented. Approved assets staged; assets alone do not implement modules. Preserve every behavior described in the handoff. |
| Battery/device readings, Storage/Activity Monitor, audio/Now Playing, Work Mode | Not implemented. OS/provider capability tests, parked observers and Focus/DND restrictions remain. |
| China/Global official login, signing, sanity, synced profile | Not implemented or live tested. No credentials transferred/read; require Windows reauthentication and byte-identical synthetic signing tests first. |
| Direct tray drops, notifications/startup, accessibility, recording, sleep/device loss | Unverified; HUD shelf drop target and fallback/capability reports still required. |
| Signed installation/update/rollback/uninstall, ARM64/older Windows | Not implemented/tested. Only the x64 feasibility build is established. |

## Release preparation

The Windows developer ZIP has its own name, inventory and hashes and does not change any Mac release/feed. It is unsigned (`NotSigned`) and explicitly labeled incomplete. DDDuoDuo has not chosen a Windows consumer version or signing identity. No release tag, consumer version bump, release-note change, installer, upload to Releases, or publication has been made.

To reproduce: [windows/README.md](windows/README.md), [packaging documentation](windows/packaging/README.md), and `windows/tests/run-feasibility.ps1`. The [release evidence template](windows/packaging/release-evidence.template.json) deliberately leaves every consumer gate unverified. Advance through all seven handoff milestones and resolve measured regressions before preparing a release candidate.
