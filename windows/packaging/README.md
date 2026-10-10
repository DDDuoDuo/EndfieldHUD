# EndfieldHUD Windows packaging

This folder holds the Windows release path: the per-user installer, the
portable zip, release staging, the CI workflow support, the section 9
performance-gate tooling and the EHUDZ01 verifier. None of it is linked into
`EndfieldHUD.exe`. The installer is its own small program.

Decision: EndfieldHUD is installed **per user and unpackaged**. There is no
MSIX package, no administrator prompt and no self-signed-certificate
workaround. The builds are **unsigned** until DDDuoDuo has a code-signing
certificate (see *Signing*).

## Artifacts

`cmake --build <build> --config Release --target endfieldhud_release` stages
the following files into `<build>/packaging/dist/`. It never creates a tag or
release.

| File | Contents |
| --- | --- |
| `EndfieldHUD-<release>-build<N>-windows-x64-setup.exe` | Installer: the setup stub plus a verified payload (install tree + `uninstall.exe`) |
| `EndfieldHUD-<release>-build<N>-windows-x64-portable.zip` | The install tree under `EndfieldHUD/` (no uninstaller, no registration) |
| `…-symbols.zip` | `EndfieldHUD.pdb`, when the build emits one (never in the consumer package) |
| `…-size-report.json` / `.md` | Installed size per part, artifact sizes and SHA-256 values, exclusions, runtime dependencies |
| `…-SHA256SUMS.txt` | Checksums of the artifacts |

The release staging script is `stage_release.cmake`. It runs these steps:

1. Installs the `EndfieldHUD` component, which holds only `EndfieldHUD.exe`, `fonts/` and `resources/`.
2. Rejects symbols, test programs, GIF/MP4/MOV files and archives.
3. Runs `EndfieldHUD.exe --verify-package`.
4. Zips the install tree.
5. Packs the setup with `tools/package_payload.cpp`.
6. Re-verifies the setup with its own reader (`--verify-payload`).
7. Writes the size report and checksums.

If the Mac-exported runtime inputs are missing, the package does not verify and
staging is refused. With `-DEHUD_RELEASE_ALLOW_INCOMPLETE=ON` (CI) every
artifact name gains `-incomplete`, and that setup refuses to install outside
isolated tests.

Measured on the test laptop (Release, `source-24ac9f5` inputs, 2026-10-10):

| Part | Size |
| --- | ---: |
| `EndfieldHUD.exe` | 11.85 MiB |
| `fonts/` (5 files, bundled with their licences) | 26.89 MiB |
| `resources/` (shell 14.03, media-assembly 2.64, common 1.65, map 1.36, profile 0.68, orbipom 0.52, account 0.11 MiB) | 21.03 MiB |
| `uninstall.exe` | 0.94 MiB |
| **Installed** (317 files) | **60.70 MiB** |
| Setup download (LZMS) | 27.49 MiB |
| Portable zip (deflate) | 27.27 MiB |

The fonts are the largest dependency. The Visual C++ runtime is linked
statically. The WebView2 runtime, used only for account sign-in, is a system
component and is not bundled.

## Payload format (`package_format.hpp`)

The setup file is laid out as follows:

```
[stub PE image][payload][zero padding][64-byte trailer][Authenticode table, when signed]
```

The payload is `EHUDPAY1`, a u64 manifest length, a manifest JSON and then the
data. Each file records its path, installed size, SHA-256 of its installed
bytes, its encoding and its range. The encoding is identity, or LZMS through
the Windows Compression API (an OS component, not a third-party compressor),
used when it shrinks the file.

The trailer contains `EHUDSFX1`, the payload offset and length, the SHA-256 of
the payload, and `EHUDSFXE`. It ends 8-byte aligned, so a later `signtool sign`
adds no padding. The reader finds the trailer at the end of the signed content,
taken from the PE security directory; it never scans for it.

Before any change, the reader rejects all of the following:

- a damaged payload hash
- a payload that overlaps the image or trailer, or non-zero padding
- unsafe paths: absolute, `..`, reserved device names, Windows-illegal characters, or more than 180 bytes
- paths that collide case-insensitively, and files that are also folders
- non-contiguous ranges
- missing `EndfieldHUD.exe` or `uninstall.exe`
- compressed files that do not decode to their exact size and hash, or that declare more than 256 MiB

## Installer (`installer.hpp`, `setup_main.cpp`)

`EndfieldHUD-Setup.exe` and `uninstall.exe` are one source built in two roles.
The window is a Windows task dialog with a progress bar, a *Launch at login*
checkbox and explicit consent before deleting data. Its text is in five
languages (`setup_text.cpp`) and uses the app's catalog terms. The language
follows the app's saved language, or the Windows display language.

| Item | Location |
| --- | --- |
| Program files | `%LOCALAPPDATA%\Programs\EndfieldHUD\` (`FOLDERID_UserProgramFiles`) with `EndfieldHUD.install.json` listing exactly what Setup wrote |
| Start menu | `Programs\EndfieldHUD.lnk` with AppUserModelID `io.github.endfieldcharge.EndfieldCharge` and toast activator `{FF18639C-ADE8-4BA7-AC09-7F1216F9E200}` |
| Toast activator | `HKCU\Software\Classes\CLSID\{…}\LocalServer32 = "<exe>" --notification-activation` |
| Uninstall entry | `HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\EndfieldHUD` (DisplayName, DisplayVersion `1.2.0.18`, Publisher, InstallLocation, DisplayIcon, UninstallString/QuietUninstallString, EstimatedSize, NoModify/NoRepair) |
| Launch at login | `HKCU\…\Run\EndfieldHUD = "<exe>" --login` |
| User data | `%LOCALAPPDATA%\EndfieldHUD\` is never touched, except for the two cases below |

The Start menu entry and toast activator are the identity that
`native/calendar_notification_identity.hpp` looks up. The
`packaging_identity_consistency` test checks that both definitions agree. The
Run value is the same command that `app/application_login_item.cpp` writes.

Setup changes user data in only two cases. It saves an explicit
launch-at-login choice to the app's settings, using the app's own
`SettingsStore`. And the uninstaller deletes the data folder, but only after
the user explicitly consents.

**Launch at login, per the app setting.** The checkbox starts from the app's
saved `launchAtLogin`, which defaults to on as on the Mac.

- An explicit choice updates both the Run value and that setting. Otherwise the app would re-register at its next launch.
- A quiet install without `--launch-at-login` keeps the saved preference.
- If the user turned the entry off in Windows Settings → Apps → Startup, Setup reports it and does not override it.

**Upgrade and repair** happen in place, in this order:

1. Setup extracts the payload into `.EndfieldHUD.staging-<id>` beside the
   install folder. Every file is decoded, hashed and flushed.
2. Setup asks the Restart Manager which processes use the installed files. It
   closes only an `EndfieldHUD.exe` started from that folder, through
   `WM_QUERYENDSESSION`/`WM_ENDSESSION`, so the app saves first.
3. The two folders are swapped by renaming them.
4. Setup writes the registrations, keeping snapshots of each.
5. Files that Setup did not install are moved into the new folder. They are
   never deleted.
6. Setup restarts the app. It uses the Restart Manager for apps registered
   with `RegisterApplicationRestart("--login")`, and otherwise starts the app
   with `--login`.

If the app refuses to close, nothing changes. Any failure restores the previous
folder and every registry value, removes a Start menu entry that Setup created,
rolls back the setting and restarts the app it closed. If the swap itself is
interrupted (for example by a power cut), the next Setup or uninstall run
restores `.EndfieldHUD.backup-<id>`.

Setup refuses, and changes nothing, in these cases:

- A newer version is installed. `--allow-downgrade` overrides this.
- The folder exists and Setup did not create it.
- The install path is too long for `MAX_PATH`.
- The setup is an incomplete developer build (except in isolated tests).
- The payload is damaged (exit 6).
- Another Setup run holds the lock (exit 5).

**Uninstall.** A running image cannot delete itself, so `uninstall.exe` copies
itself to `%TEMP%\EndfieldHUD-Uninstall-<id>\` and waits for that copy, which
returns the exit code. The copy works through these steps:

1. Closes EndfieldHUD.
2. Removes scheduled Calendar toasts while the identity still resolves them.
3. Removes exactly the matching Start menu entry and activator.
4. Removes the Run value, if it launches this installation.
5. Removes the files listed in the manifest. The waiting parent's image is
   moved out of the folder, because a running image can be renamed but not
   deleted.
6. Removes the empty folders and the uninstall entry.

Files Setup did not create are kept and reported. User data is kept unless
`--remove-user-data` is given, or the user ticks *Also delete my EndfieldHUD
data* and confirms a second warning. Even then the folder is deleted only if
its `root.json` marks it as EndfieldHUD data, links are never followed, and the
data is kept while another EndfieldHUD holds the data root. The relocated copy
stays in `%TEMP%` until the next Setup or uninstall run removes it. Unelevated
code has no supported way to delete its own running image.

**Administrator rights.** Setup never asks for them (`asInvoker`). It
installs for the Windows account that runs it. If you start it with "Run as
administrator" and enter another account's credentials, it installs for that
other account. A Setup that closed EndfieldHUD restarts it in Setup's own
context.

**Command line.**

- `--quiet` (also `/quiet`, `/S`) shows no window.
- `--launch-at-login on|off` sets the login choice.
- `--open` starts EndfieldHUD after installing.
- `--verify-payload` only checks the setup file.
- `--result <file>` writes a JSON outcome.
- `--remove-user-data` is the uninstaller's consent to delete user data.

Exit codes: 0 success, 1 failed and rolled back, 2 usage, 3 refused,
4 cancelled, 5 busy, 6 damaged payload, 7 incomplete build.

**Isolation (tests only).** `--test-registry Software\EndfieldHUD-Test-<id>`
moves every registry value below that private HKCU subtree. It requires
temporary `--install-root`, `--programs-folder` and `--data-base` folders. Only
then are `--work-folder` and `--fail-after <step>` accepted. Isolated runs use
a fixture instead of the notification platform, and the uninstall command
written to their private entry stays isolated.

## Signing

There is no certificate yet, so Setup, `uninstall.exe` and `EndfieldHUD.exe`
are unsigned. SmartScreen may show "Windows protected your PC" the first time a
downloaded copy runs. Nothing here tells users to disable a Windows security
feature.

When DDDuoDuo has a certificate (OV/EV or Azure Trusted Signing), configure the
build with `-DEHUD_SIGN_CERTIFICATE_SHA1=<thumbprint>
-DEHUD_SIGN_TIMESTAMP_URL=<RFC 3161 server>`. Staging then runs these steps:

1. Signs `EndfieldHUD.exe` and `uninstall.exe` before packing, using
   `signtool sign /fd sha256 /tr … /td sha256`.
2. Signs the finished setup.
3. Checks each signature with `signtool verify /pa`.
4. Marks the size report `signed`.

The payload reader accepts the appended certificate table. This is tested with
a synthetic table.

## Updates

There is no Windows updater yet; see `remaining` in the area report. A
side-by-side update would reuse the installer transaction: extract beside the
folder, close the app through the session-end protocol, swap, then roll back or
restart.

Windows releases must use their own tag namespace (`windows-v<release>`, which
fails the Mac `HUDReleaseVersion` parser) and their own feed. The CI workflow
fails if `updates/appcast.xml` differs from `main`.

## CI (`.github/workflows/windows.yml`)

The workflow runs two jobs:

- **`portable`** (ubuntu-24.04): the source-authority check, the Mac-feed guard, then building and running the portable suites.
- **`windows`** (windows-latest, MSVC x64): builds everything and runs every registered suite. It then stages the unsigned, incomplete developer package and uploads it as a 7-day workflow artifact.

Both jobs have read-only permissions. The Mac-exported runtime inputs are not
in the repository, so CI does not register the input-dependent suites (owner
coverage, data-root and package contracts) and cannot run hidden WARP module
coverage. These remain laptop checks.

## Performance gates (`perf_gate.hpp`, `perf_probe.hpp`, `perf_runner.hpp`, `tools/package_perf_gate.cpp`)

```
package_perf_gate --exe <EndfieldHUD.exe> --report <report.json>
    [--scenarios closed,open,ambient-off,lifecycle] [--seconds 60] [--settle 10]
    [--cycles 100] [--open-settle 1.5] [--close-settle 1.5] [--limit open.cpu=8]…
```

The gate measures the process from outside: nothing is injected, and neither
administrator rights nor an ETW session is needed. It records the following:

- process CPU as a percentage of one core
- wake-ups: per-thread context switches from `NtQuerySystemInformation`, with the busiest threads named by description and start module
- handles, threads and GDI/USER objects
- private bytes and working set
- where the session exposes them, GPU engine utilization and GPU process memory (PDH)

It runs these scenarios:

1. **Closed idle**, 60 s.
2. **Open idle** with ambient animation on.
3. **Lifecycle**: 100 open/close cycles. Growth is the median of the last 10
   cycles minus the median of the first 10 after warm-up.
4. **Open idle** with ambient animation off.

Each run uses a fresh temporary data root with launch at login off and
onboarding done. The app is stopped through the session-end protocol. The
report records the following:

- the OS build
- CPU, RAM, GPUs, DPI, refresh rate and display size
- whether the session is the interactive console
- the executable's version and SHA-256
- every setting and limit

The limits are provisional and configurable. The closed defaults are ≤ 0.5 %
of one core, ≤ 20 wake-ups/s and ≤ 0.5 % GPU; the Mac measured 0.11–0.26 %
closed. Values are reported whether or not a limit is set.

**Requirement:** the gate drives `EndfieldHUD.exe` only if the executable
supports `--data-root <folder>` and the forwarded `--toggle`; it checks for
both before starting anything. Until the host adds them (see the host
requests), the tool refuses to start the app (exit 2), so it can never run
against the real data root or the user's running copy.

Frame pacing, GPU duration and input latency need ETW (WPR/PresentMon) and are
not part of this gate.

## EHUDZ01 (`ehudz01.hpp`, `tools/package_watch_resources.cpp`)

Decision: `EndfieldHUD.exe` ships only the compiled runtime catalog
(`resources/shell`). No EHUDZ01 payload is packaged or decoded at runtime.

The bounded decoder implements the Mac packager's own checks: the magic, the
LE64 length of 1…128 MiB, raw DEFLATE, exact length, end of stream and no
trailing bytes. `tools/package_ehudz01_reference.py` runs the unchanged
`scripts/package-watch-resources.py` to produce the oracle fixture, which
covers every accepted and rejected case including the 128 MiB maximum.

`package_watch_resources verify <staged folder>` checks a Mac-staged
WatchSource natively, as `package-watch-resources.py verify` does. The real
repository tree (579 records and one derived file, 428 EHUDZ01 payloads,
12.29 MB stored, 99.73 MB decoded) verifies byte-exact, and a single flipped
byte is rejected. CI repeats this with the `packaging_watch_runtime_staged`
test.

## Tests

| Test | What it proves |
| --- | --- |
| `packaging_format_contracts` (portable) | Manifest and install-manifest codecs and every rejection rule, versions, trailer and PE/Authenticode location, Setup arguments and isolation rules, all five languages and their placeholders |
| `packaging_perf_gate_contracts` (portable) | Idle arithmetic, gates, plateau versus leak detection |
| `packaging_ehudz01_contracts` (portable) | The decoder against the Mac oracle, and the staged-inventory verifier |
| `packaging_installer_contracts` | 385 checks through the real Setup/uninstall programs with synthetic payloads in `%TEMP%\ehud-<id>` and a private HKCU subtree. Covers fresh install, repair, upgrade, launch-at-login choices, refusals, rollback after every step, interrupted-swap recovery, the Restart Manager close/restart of a stand-in app, uninstall relocation, kept data, explicit data removal and the signed layout. Outside the interactive console session (CI, the laptop's SSH session) it also drives the task-dialog windows with dialog messages: install with the box cleared, uninstall with confirmed data removal, *Keep my data*, and cancel |
| `endfieldhud_release_contracts` | The real install tree staged, zipped, packed, installed (isolated), checked with the installed `EndfieldHUD.exe --verify-package` and uninstalled |
| `packaging_perf_probe_contracts`, `packaging_perf_gate_tool`, `packaging_perf_gate_refuses_unsupported` | The gate against the stand-in app: a quiet process passes, one handle/GDI leak per cycle and a 10 ms wake-up are caught and attributed, and an executable without `--data-root` support is never started |
| `packaging_identity_consistency` | Setup's notification identity equals the Calendar area's |
| `packaging_watch_runtime_staged` (CI, where `Resources/` and Python exist) | The Mac packager's real staged tree verifies natively |
