# Native Windows migration

DDDuoDuo's native Windows target is currently a **feasibility prototype**. The complete app has not been migrated and no Windows consumer release is ready. [The migration handoff](../WINDOWS-MIGRATION.md) retains every original requirement; [the progress log](../WINDOWS-PROGRESS.md) records evidence and open gates.

The prototype builds C++20/Win32 with D3D11, DXGI, DirectComposition, Direct2D, DirectWrite and TSF. It loads the original scene, clips, source camera, sprites and mesh metadata. The diagnostic renderer shows source geometry outlines and a projected synthetic text editor. Source materials, textures, blur, native module content, localization and full scene layout are not yet presented. This geometry probe is not a replacement interface.

## Build and preview

From the repository root on Windows 11 x64:

```powershell
.\windows\packaging\build.ps1 -Python C:\path\to\python.exe
```

Install the Visual Studio C++ build tools, Windows SDK and CMake tools first. This builds `windows/build/x64/Release/EndfieldHUDWindows.exe`, verifies isolated tests and byte-exact resource inventories, and prepares a developer ZIP under `windows/dist`. [Packaging details](packaging/README.md) describe dependency ownership, signature checks, size reports and the consumer release gate. No installer, updater or consumer version is selected.

Opening the executable creates a tray activation path. Use Ctrl + backtick or its tray menu; the shortcut reports keyboard-layout requirements and conflicts in the menu. Escape leaves editing before closing; the tray Quit action confirms and closes before exit. The diagnostic editor is in memory. No user profile, account, clipboard history, Mac app data or existing preferences are loaded. Explicit copy/paste inside the test editor uses the standard clipboard only when requested by the user.

## Reproducible isolated diagnostics

```powershell
.\windows\tests\run-feasibility.ps1
```

The script runs native contracts and hardware capability probes, submits source frames to an invisible test HWND, checks 100 warmed reopen cycles, and returns to the message loop for a 61-second closed-state measurement. It writes local, ignored evidence under `windows/evidence/local`; it does not capture the desktop or start a visible helper. GPU utilization, wakeups, visible frame pacing, cold/warm visible opening latency, recording, actual IME use and Mac comparisons require separate measurements. Hardware-probe success does not establish visual or module parity.

Live acceptance must cover Chinese/Japanese/Korean IME and projected candidates; mixed-DPI monitors and unplug/recovery; a pre-opening desktop backdrop without recursive HUD capture; normal/custom cursor recording; accessibility; device loss and sleep/resume; complete module/data/account/provider parity; and signed installation/update/rollback/uninstall. The first feasibility gate remains open. Module implementation proceeds only after those architecture problems are resolved; no required module is dropped.
