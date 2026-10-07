# Native Windows migration

DDDuoDuo's native Windows migration has **restarted from the requested GitHub branch**, with canonical resources pinned to `4036174a3facf935260f4d0a9c63bfff33b98c37`. Milestone 1 is in progress. The complete app has not been migrated and no Windows consumer release is ready. [The migration handoff](../WINDOWS-MIGRATION.md) retains every original requirement; [the restarted progress log](../WINDOWS-PROGRESS.md) records fresh evidence and open gates. The incorrect raw-game preview release has been withdrawn to a recoverable draft.

The prototype builds C++20/Win32 with D3D11, DXGI, DirectComposition, Direct2D, DirectWrite and TSF. Its normal path loads the current **desktop adaptation**: canonical profile card, suppressed game-only UI/text, recycled desktop navigation, original desktop icons and five navigation languages. Native captions, header, clock and profile fixture use the source planes; the footer stays in its native screen plane through tilt. The source camera, sprites, clips, meshes, materials, fills, sorting, masks, button feedback and original cursor retain their source contracts. Complete FX/HDR/backdrop parity, matched Mac typography, profile artwork and hover replacements, module bodies, persistence and providers remain incomplete. [Desktop presentation contracts](scene/DESKTOP-SHELL.md) distinguish implemented artwork from module behavior.

## Build and preview

From the repository root on Windows 11 x64:

```powershell
.\windows\packaging\build.ps1 -Python C:\path\to\python.exe
```

For a new checkout, preserve canonical bytes before Git writes files:

```powershell
git clone --config core.autocrlf=false --branch codex/windows-migration https://github.com/DDDuoDuo/EndfieldHUD.git EndfieldHUD
```

The [source provenance gate](packaging/SOURCE-PROVENANCE.md) rejects newline-converted assets as well as altered or stale resources.

The normal desktop retains the source default ambient animation. `--ambient-off` selects the explicit idle diagnostic case. Wheel input uses the source finite scroll spring and projected viewport; precision touchpad phases, scroll-arrow controls and live input acceptance remain unfinished.

Install the Visual Studio C++ build tools, Windows SDK and CMake tools first. CMake downloads a hash-pinned zlib 1.3.2 source archive and links only its decoder statically. The script builds `windows/build/x64/Release/EndfieldHUDWindows.exe`, verifies isolated tests and byte-exact resource inventories, and prepares a developer ZIP under `windows/dist`. The chosen Windows distribution is an **unsigned portable ZIP**; no signing certificate or paid signing service is required. [Packaging details](packaging/README.md) describe dependency ownership, the portable manual update/rollback helper, honest signature/size reports and consumer acceptance gates. A consumer version is assigned only after all applicable evidence passes; signed MSIX is an optional future format.

Opening the executable creates a tray activation path. Use Ctrl + backtick or its tray menu; the shortcut reports keyboard-layout requirements and conflicts in the menu. Escape leaves editing before closing; the tray Quit action confirms and closes before exit. The normal desktop omits diagnostic artwork. `--editor-fixture` explicitly enables an in-memory projected editing fixture; `--language en`, `zh-Hans`, `zh-Hant`, `ja` or `ko` selects a diagnostic navigation language. No user profile, account, clipboard history, Mac app data or existing preferences are loaded. Explicit copy/paste inside the test editor uses the standard clipboard only when requested by the user. Module labels describe the required app surface; their module bodies are not implemented at this milestone.

## Reproducible isolated diagnostics

```powershell
.\windows\tests\run-feasibility.ps1
```

The script runs native contracts and hardware capability probes, submits source frames to an invisible test HWND, checks 100 warmed reopen cycles, and returns to the message loop for a 61-second closed-state measurement. It writes local, ignored evidence under `windows/evidence/local`; it does not capture the desktop or start a visible helper. GPU utilization, wakeups, visible frame pacing, cold/warm visible opening latency, recording, actual IME use and Mac comparisons require separate measurements. Hardware-probe success does not establish visual or module parity.

The projected editor includes native UIA Edit/Text/Value providers, scrolling and TSF marked-text display attributes. Live acceptance still must cover Chinese/Japanese/Korean IME and projected candidates; mixed-DPI monitors and unplug/recovery; a pre-opening desktop backdrop without recursive HUD capture; normal/custom cursor recording; Narrator and whole-HUD accessibility; device loss and sleep/resume; complete module/data/account/provider parity; and portable installation/update/rollback/uninstall with consent and verified hashes. The first feasibility gate remains open. Module implementation proceeds only after those architecture problems are resolved; no required module is dropped.
