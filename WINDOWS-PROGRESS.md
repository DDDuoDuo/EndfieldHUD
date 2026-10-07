# Windows migration progress — 2026-10-07

Authorship: **DDDuoDuo**. Branch: `codex/windows-migration`. Source baseline: `fc429e63a2bead66f194ddb18ca947684de8fd76`; handoff branch started at `4079363c4a34d09f60f3f593b8b4f1cb82d9b518`.

**Milestone 1 is in progress. The full app is not migrated and a Windows consumer release is not ready.** This log supplements [WINDOWS-MIGRATION.md](WINDOWS-MIGRATION.md). DDDuoDuo explicitly chose unsigned portable distribution and authorized release publication; those choices supersede the handoff's signing/publication restrictions. The app, visual, data and performance requirements remain authoritative. No required module is removed or narrowed to fit this prototype. Mac sources, scripts, resources, version, releases and update feed remain unchanged.

## Latest continuation

- Original source sprites draw as filled perspective-correct D3D11 geometry with authored mip chains, source IDs and ancestor rectangular masks. Selected original UI and mesh main/mask/dissolve shader programs now execute with authored material/gamma flags, raw mesh colors, blend states and animated uniforms. Full FX/soft-mask/stencil/HDR parity, desktop rows/profile mounting and exact typography remain incomplete. Explicit seeded deployment-flicker groups are implemented for the future desktop mount; current Watch sprites keep their source wrapper clips without inferred extra groups.
- The original 58×58 cursor and (0,0) hotspot use one native HCURSOR, with event-driven focus/presentation ownership, projected-editor I-beam precedence and nested native-menu yielding. Actual pixels/mask and 100 reopen/recreation cycles pass isolated tests; recording, accessibility sizing and mixed-DPI live acceptance remain unverified.
- Projected editing now includes TSF display attributes/marked ranges, viewport scroll/caret reveal and native UIA Edit/Text/Value providers. DirectWrite caching includes the editor's artwork revision. Synthetic COM and coordinate contracts pass; live CJK, Narrator and whole-HUD accessibility remain unverified.
- Six native CTest suites pass, including **1,992** combined scene/source checks, **124** offscreen WARP material checks and original-cursor pixel/lifecycle contracts; **54** Python packaging tests pass. The native packed-resource decoder rejects corrupt, oversized, truncated and trailing raw-DEFLATE payloads. Its static zlib 1.3.2 source/archive/license are pinned.
- Unsigned portable ZIP is the chosen release format and requires no paid signing service. Manual portable install/update/rollback/uninstall helpers enforce explicit consent, exact inventory/hash validation, closed-app replacement, separate data snapshots and retained prior payloads. Temporary-folder tamper and failed-promotion recovery tests pass. Every full app/data/parity/performance gate is still required. Signed MSIX/App Installer remains optional; its SDK schema/payload validation passes, while trusted live deployment remains unverified.
- The current staged resource inventory has **752 files, 28.61 MiB**, including eleven decoded scene/controller inputs and the zlib notice. No user data, credentials, recordings or documentation media are bundled.

The latest sanitized evidence is [windows/evidence/feasibility-base-image-2026-10-07.json](windows/evidence/feasibility-base-image-2026-10-07.json). The source renderer originally grew private bytes by 65.48 MiB across 100 rapid hidden submits. Reusing one vertex upload per frame and draining queued GPU work before snapshots resolved that observed growth: private bytes **196,788,224 → 190,558,208**, working set **128,593,920 → 127,922,176**, handles **581 → 581** over 100 source-endpoint cycles after five warmups. The same warmed process then closed for **61.001 seconds**, submitting **0** additional HUD frames with **0.046875 CPU seconds**. These are hidden endpoint tests, not visible animation or module-switch benchmarks. GPU utilization, wakeups, live input latency and p50/p95 remain unverified.

A renderer review caught incompatible D3D/D2D alpha encoding. The output now uses one encoded-space premultiplied BGRA contract; an actual synthetic half-alpha-white GPU readback passes. Rendering/resize failures park and report the preview instead of being silently ignored. Longer cache/device-loss and matched Mac visual/performance acceptance remain required.

The final preview review found that timer parking could omit the settled entrance,
gyro or button endpoint. Submission history now requires that last frame before
parking, with a full rebuild for the final button tint/depth. Hover follows the
presented geometry even under a stationary pointer. Failure/suspend/close cancel
mouse capture and pointer state. Synthetic scheduling checks verify settlement,
reduced-motion reopen and closed/idle parking; live interaction remains unverified.

The first continuation CI build compiled successfully but caught pins that had
been calculated from Windows-converted CRLF dependency files. The pins now use
the authoritative Git blob bytes, and `.gitattributes` preserves those immutable
JavaScript/license bytes in fresh Windows clones. The source assets themselves
have no Git diff; local staging and all 46 packaging checks pass after the fix.

Fresh CI then found a hidden-folder lookup in the synthetic rollback test.
Snapshot helpers now include hidden ancestors, directories and files in their
existing path/hash checks and restore them explicitly. A hidden-data regression
failed before the fix and passes afterward; all 47 local packaging checks pass.

## Initial feasibility baseline (d03915a)

- Native Windows-only C++20/CMake x64 executable, static MSVC runtime, per-monitor-v2 manifest, single lifecycle owner, tray activation, Explorer restart handler, keyboard-layout-aware Ctrl/backtick registration with conflict labels, suspend parking, and idle/closed frame scheduling. Live shell interaction and OS-change acceptance still need testing.
- Source scene loader: 789 original nodes, exact decimal signed IDs, source Hermite/stepped/weighted curves, finite OutQuad clocks, camera/FOV/canvas arithmetic, quaternion gyro, original sprite and triangle geometry, source-mask inverse hit testing and shared frame transforms. Pointer-only reprojection retains geometry allocations. Source flicker primitives are ported but their shell integration is pending.
- TSF `ITextStoreACP` context, DirectWrite cluster layout, UTF-16 model, bounded undo/redo, composition grouping, caret/selection and projective hit/candidate extents. The synthetic editor is drawn into a cached GPU surface on the same source-plane mapping as its hits. No child Edit HWND is created. Live CJK IME, accessibility and rich formatting remain unverified/incomplete.
- Both DirectComposition and Windows.UI.Composition create desktop targets successfully on this laptop; DispatcherQueue, TSF and seven selected Latin/CJK/emoji fonts are available. DirectComposition is **provisional**, pending visual/performance/backdrop/recording comparisons.
- Isolated tests: three native CTest suites pass; 509 scene checks include actual source resources; 37 Python packaging tests pass. Tests use synthetic content and temporary files and do not read app data or accounts.
- Windows staging reuses the unchanged desktop Watch packer and verifies retained bytes. The Watch source tree is 354.15 MiB; its selected runtime payload is 11.74 MiB. Complete Windows resources contain 750 files, 28.33 MiB, including ten byte-exact decoded metadata inputs. No GIFs, recordings, user data, extraction intermediates or SDF atlases enter the package. Matter.js 0.20.0 and its MIT notice are pinned by exact hashes; the game adapter is not implemented.
- Preview-only packaging produces per-file hashes, actual size/dependency reports and Authenticode status. Consumer packaging rejects incomplete or changed evidence and unsigned binaries, and remains unavailable until a signed installer/updater is implemented and tested. The new Windows-only CI workflow successfully built/tested/staged/uploaded its developer artifact on the Windows runner: [first CI run](https://github.com/DDDuoDuo/EndfieldHUD/actions/runs/37642437184). CI success does not satisfy the laptop's live acceptance gates.

## Target laptop and measured limits

Windows 11 x64, OS build **26200**, Intel Core Ultra 7 251HX, NVIDIA RTX 5060 Laptop GPU plus Intel Graphics, RAM **16,573,128,704 bytes**. One attached display: 2560×1600, **144 DPI**, NVIDIA reports 165 Hz. Mixed-DPI and unplug/recovery tests cannot be represented by this single-display measurement.

Toolchain: Visual Studio Build Tools 17.14.37710.0, MSVC tools 14.44.35207/compiler 19.44, Windows SDK 10.0.26100.0, CMake 3.31.6-msvc6, Python 3.12.14. The latest build adds a hash-pinned static zlib 1.3.2 inflater; no third-party DLL or background browser is added.

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
| Transparent source circle/buttons, exact opening/closing, live tilt and hits | **Incomplete** | Source sprites/mips, layout, ordering, radial fills and button controllers are now implemented; full FX/soft-mask/stencil/HDR, desktop mounting, shell flicker and visible Mac comparison remain. The renderer remains a feasibility preview. |
| Frozen backdrop and recordable HUD/custom cursor | **Partially implemented, recording unverified** | Original native cursor ownership/pixels pass isolated tests. No desktop capture performed. Prototype pre-opening capture/guarded refresh, test recursion and saved Windows recordings; verify live cursor capture. |
| Mixed-DPI selection, moving/unplugging screen | **Unverified** | Physical-coordinate/per-monitor shell exists; only one display is connected. Explicit selected-monitor settings and real hot-plug acceptance remain. |
| Tilted editor with CJK IME, caret, keyboard and font comparisons | **Partially verified** | TSF/model/DirectWrite/shared projection, scroll and native UIA contracts pass. Marked-text decoration is implemented. Live CJK composition/candidates, Narrator, whole-HUD accessibility and Mac metrics remain unverified. |
| Closed parking, bounded reopen memory, smooth pacing | **Partially verified** | Closed frame submissions and CPU measured; 100 hidden cycles pass ownership/handle checks, and the observed private-byte rise is resolved in this fixture. Broader cache/GPU budgets and visible frame pacing remain unverified. |

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
| Portable installation/update/rollback/uninstall, ARM64/older Windows | Unsigned portable distribution chosen; synthetic manual deployment, consent, tamper and rollback checks pass. Full-app live deployment remains unverified. Signed MSIX is optional. Only x64 is tested. |

## Release preparation

The Windows developer ZIP has its own name, inventory and hashes and does not change any Mac release/feed. It is unsigned (`NotSigned`) and explicitly labeled incomplete. DDDuoDuo requested the latest build, release publication and unsigned distribution without a paid signing service. The signing choice is resolved. Publish this foundation only as a Windows feasibility prerelease, with the remaining modules and live acceptance gaps stated explicitly; full consumer gates remain incomplete. Publication does not constitute completed migration.

To reproduce: [windows/README.md](windows/README.md), [packaging documentation](windows/packaging/README.md), and `windows/tests/run-feasibility.ps1`. The [portable release evidence template](windows/packaging/portable-release-evidence.template.json) deliberately leaves every consumer gate unverified. Advance through all seven handoff milestones and resolve measured regressions before preparing a full release candidate.
