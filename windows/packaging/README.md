# EndfieldHUD Windows packaging

This folder holds the Windows release path: the per-user installer, the
portable zip, release staging, the CI workflow support, the section 9
performance-gate tooling, the EHUDZ01 verifier and the Windows updater. Only
the updater is linked into `EndfieldHUD.exe`, as the `windowsUpdates` area
(see *Updates*). The installer is its own small program.

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
| `…-SHA256SUMS.txt` | Checksums of exactly the setup and the portable zip, the files uploaded to a release (the list the Windows updater checks a download against) |
| `…-symbols.zip` | `EndfieldHUD.pdb`, when the build emits one (never in the consumer package, never uploaded) |
| `…-symbols-SHA256SUMS.txt` | The symbols zip's checksum, when there is one (never uploaded) |
| `…-size-report.json` / `.md` | Installed size per part, artifact sizes and SHA-256 values, exclusions, runtime dependencies |
| `…-release-notes.md` | `windows/RELEASE-NOTES.md` with its SHA-256 values filled in: the draft for the `windows-v<release>` GitHub release (see *Releases*) |

The release staging script is `stage_release.cmake`. It runs these steps:

1. Installs the `EndfieldHUD` component, which holds only `EndfieldHUD.exe`, `THIRD-PARTY-NOTICES.txt`, `fonts/` and `resources/`.
2. Rejects symbols, test programs, GIF/MP4/MOV files and archives.
3. Runs `EndfieldHUD.exe --verify-package`.
4. Zips the install tree.
5. Packs the setup with `tools/package_payload.cpp`, which refuses a tree without `THIRD-PARTY-NOTICES.txt`.
6. Re-verifies the setup with its own reader (`--verify-payload`).
7. Writes the size report and checksums.
8. Writes the release-notes draft.

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
| Program files | `%LOCALAPPDATA%\Programs\EndfieldHUD\` (`FOLDERID_UserProgramFiles`): `EndfieldHUD.exe`, `THIRD-PARTY-NOTICES.txt`, `fonts\`, `resources\` and `uninstall.exe`, with `EndfieldHUD.install.json` listing exactly what Setup wrote (the list uninstall removes) |
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

- Windows is older than Windows 11 (build 22000, `minimumWindowsBuild` in
  `package_format.hpp`): the HUD's blurred background needs
  `DWMWA_USE_HOSTBACKDROPBRUSH`, and `EndfieldHUD.exe` cannot start without
  it (it then reports that Windows 11 is required). The refusal names the
  PC's build. Isolated tests set the build with `--test-windows-build`.
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

## Updates (`update_check.hpp`, `updater.hpp`, `update_platform.hpp`, `app/areas/update_module.*`)

EndfieldHUD.exe checks for Windows releases itself. The `windowsUpdates` area
owns the updater and publishes `app::UpdateService`
(`app/areas/update_service.hpp`) for the About page and the tray. No timer or
polling loop is added.

**Releases are Windows only.** A Windows release is tagged
`windows-v<SemVer>`, for example `windows-v1.2.1` or `windows-v1.3.0-beta.1`.
The checker reads the same GitHub release list as the Mac app
(`/repos/DDDuoDuo/EndfieldHUD/releases?per_page=100`). It accepts only
`windows-v` tags. It never reads `updates/appcast.xml` or a Mac asset. The
unchanged Mac `HUDReleaseVersion` rejects every `windows-v` tag, so a Windows
release is never offered to a Mac (oracle scenario *windows tags only*).

**Parity with the Mac checker.** `update_check.cpp` ports
`HUDReleaseVersion`, `HUDGitHubRelease.validatedURL` and
`HUDGitHubReleaseClient`. Its decisions are checked against
`tests/fixtures/update-release-source.json`, which
`tools/package_update_reference.sh` generates from the unchanged
`Sources/HUDGitHubRelease.swift`. The port covers:

- SemVer precedence, and drafts, unpublished releases and foreign pages ignored.
- Stable builds see stable releases only; prerelease builds see the newest.
- ETag with `If-None-Match` and 304 reuse.
- `Retry-After` (capped at one day), `X-RateLimit-Reset`, the 1 MiB response
  cap, title cleanup and Foundation's JSON decoding rules.
- Equal versions are ordered by the installer build, so a Windows-only rebuild
  (1.2.0 build 19 over build 18) is offered.

**Installable releases.** A release is installable when it carries exactly
`EndfieldHUD-<X.Y.Z>-build<N>-windows-x64-setup.exe` and
`EndfieldHUD-<X.Y.Z>-build<N>-windows-x64-SHA256SUMS.txt`, uploaded to that
release, with `<X.Y.Z>` equal to the tag version. These are the
`stage_release.cmake` names. Any other release is only discovered: its page can
be opened.

**When it checks.**

- *Check for updates* in About or the tray sends one request.
- *Check for updates daily* (on by default) allows at most one check per day,
  when the HUD opens.
- The preference and the last check time are saved in
  `<data root>\Updates\state.json` (format `EndfieldHUD.Windows.Updates`,
  schema 1). Mac updater preferences are not imported.

**Consent.** Each step needs a click on the About update row.

1. *Update available* reads "Click to download and verify …". The click
   downloads the checksum list and then the setup.
2. *Ready to update* reads "Click to install …". The click installs.

**What stays resident.** Between checks the area holds only its state and
the transport's bookkeeping. The WinHTTP session exists only while a request
waits or is in flight, so a run that never checks starts none of WinHTTP's
threads (an asynchronous session starts them when it opens; before this, the
area opened one at launch). The first request opens the session on the
shared utility queue, never in an opening frame (the daily check starts with
an opening). The last completion, cancellation or deadline closes it.
Requests accepted while the session opens keep their deadline; one cancelled
or expired meanwhile never reaches the network.

**Download checks.**

- HTTPS only, through WinHTTP. Redirects are followed by the updater, never by
  WinHTTP, and only to `github.com` or `*.githubusercontent.com`, at most five.
- The exact published size.
- The setup's SHA-256 must equal its line in SHA256SUMS.
- The setup's own payload must verify (`package_io`) and name exactly that
  version, complete and x64.
- When this EndfieldHUD.exe is Authenticode-signed, the setup must be signed by
  the same publisher. No build is signed yet.

**Install hand-off.**

1. The HUD closes first, as on the Mac.
2. The download is held without write or delete sharing and hashed again.
3. The hand-off is saved in `state.json`.
4. Setup runs as `--quiet --open --result <Updates>\handoff-result.json`.
   Setup then closes EndfieldHUD through the session-end path (data saved),
   swaps the install folder, rolls back on any failure and restarts the app.
5. The next run reports *Updated to …* or Setup's refusal. If Setup exits while
   this run is still alive (for example it refused before closing the app), a
   thread-pool wait posts the area's notice and the result is shown at once.

**Only an installed copy updates itself** (developer-build protection). The
running EndfieldHUD.exe must be in the folder that the HKCU uninstall entry
names as `InstallLocation`, and that folder's `EndfieldHUD.install.json` must
name this exact version. Portable, moved and development copies discover
releases and open the release page. An isolated data root (`--data-root`)
never checks on its own and never installs. The `Updates` folder holds at most
one download. Stale downloads and result files are removed when the area
starts.

## Releases (`windows/RELEASE-NOTES.md`)

`windows/RELEASE-NOTES.md` is the reviewed text of the release notes and their
only source. `stage_release.cmake` writes `<stem>-release-notes.md`: that file
with each `<!-- sha256 at release -->` cell filled in from the staged setup and
portable zip. Nothing else is changed. The draft is marked *Do not publish*
at the top when the notes cannot describe the staged files: an incomplete
package (its names then carry `-incomplete`), notes that do not name this
release's tag and artifacts, a checksum cell without a staged artifact, or
signed artifacts while the notes describe unsigned builds.

The notes' comment holds the publishing checklist. For a later release the
maintainer rewrites the notes for that release (version, artifact names,
changes) and empties the SHA-256 cells again. Then:

- Stage a complete package (no `-incomplete` names).
- Run the section 9 performance gate on the laptop's desktop against that
  build and read its report (see *Performance gates*).
- Compare the differences list with the current gap tables.
- Use the `windows-v` tag only, with `gh release create … --latest=false`.
- Upload only the setup, the portable zip and SHA256SUMS: never the symbols
  zip, its checksum file, the size report or an `-incomplete` artifact.
- Never touch the Mac release or `updates/appcast.xml`.

Nothing in the build or CI publishes a release. `endfieldhud_release_contracts`
checks the staged files: SHA256SUMS lists exactly the setup and the portable
zip, the symbols zip (a stand-in when the build emits no PDB) has its own
checksum file, and the draft is `windows/RELEASE-NOTES.md` with exactly its
two SHA-256 cells filled in, names the tag, the artifacts and the exporter
link at the tag, says Windows 11, and is marked *Do not publish* exactly when
the package is incomplete.

## CI (`.github/workflows/windows.yml`)

The workflow runs two jobs:

- **`portable`** (ubuntu-24.04): the source-authority check, the Mac-feed guard, then building (with `make -k`, so one run lists every compile error) and running the portable suites.
- **`windows`** (windows-2025, MSVC x64): builds everything and runs every registered suite. It then stages the unsigned, incomplete developer package and uploads it, with its release-notes draft, as a 7-day workflow artifact.

The Windows job is pinned to the `windows-2025` image rather than
`windows-latest`. EndfieldHUD links the operating system's ICU, so
`event_log_names_contracts` (grapheme breaks) and `archive_date_contracts`
(time-zone rules) compare the image's `icu.dll` with their macOS oracles. The
windows-2022 image (build 20348) predates Unicode 15.1 Indic conjunct breaks
and still applies Samoa daylight saving, so both suites failed there. Build
26100 has the system binaries of the test laptop (build 26200: ICU 72.1 with
Unicode 15.1, time-zone data 2022g).

Before the suites run, `package_os_capabilities` checks the image against
`os-requirements.json`. The same check is registered as the CTest
`packaging_os_text_data_contracts`. A future image with older data fails with
a message naming the cause. The assertions themselves are unchanged.

**Run 38116894941 (2026-10-11, commit `e17f3af`).** The pin worked: the image
reported Windows 10.0.26100, ICU 72.1, Unicode 15.1.0 and time-zone data
2022g, and `archive_date_contracts` and `event_log_names_contracts` passed.
343 of 348 Windows suites passed. The causes of the rest, all reproduced on
the laptop:

- `packaging_installer_contracts`, `endfieldhud_release_contracts`,
  `app_notification_identity_contracts` and
  `calendar_notification_identity_contracts`: GitHub's runners set `%TEMP%`
  to the 8.3 alias `C:\Users\RUNNER~1\…`. The Shell stores a shortcut's
  target with long names, so a test identity built from the alias never
  matched the shortcut. With `%TEMP%` set to an 8.3 alias the laptop fails
  the same three checks (30, 21 and 51 checks in). The packaging tests now use
  the long form of `%TEMP%`; the two identity suites belong to other areas
  (host requests).
- `packaging_watch_runtime_staged`: the unchanged Mac packager reads its JSON
  with Python's default encoding, which is UTF-8 on macOS but cp1252 on the
  runner. The test now runs it in Python's UTF-8 mode (`-X utf8`).
- The portable job stopped at a missing `<cmath>` in
  `tests/system_overlay_state_tests.cpp` (GCC; host request).

**Run 38170093334 (2026-10-11, commit `03723d7`, the latest pushed).** The
Windows job passed: 358 of 358 registered suites (among them
`packaging_installer_contracts`, `endfieldhud_release_contracts`,
`packaging_watch_runtime_staged` and both notification identity suites), the
OS ICU check (ICU 72.1, Unicode 15.1.0, time-zone data 2022g) and the staged
developer package (artifact `EndfieldHUD-windows-x64-unsigned-developer-55`).
The 8.3 `%TEMP%` and cp1252 fixes above are confirmed there. The portable job (GCC 13) failed in its
build step; with `make -k` it listed every compile error at once, all in other
areas' files and all still present at the local head (host requests):

- `modules/media_assembly_export.hpp` uses `std::shared_ptr` without
  `<memory>`, which also breaks `media_assembly_export.cpp` and
  `media_assembly_controller.cpp`;
- `modules/reader_pdf_title.cpp` line 726,
  `Parser parser(std::span(tail).subspan(at + 9), true);`: GCC parses it as a
  declaration with a class template placeholder and rejects it;
- `modules/reader_pdf_crypto.cpp` uses `std::copy_n` without `<algorithm>`.

Targets that depend on `hud_media_assembly_controller` or `hud_reader` were
not compiled, so a later run may list more.

Both jobs have read-only permissions. The Mac-exported runtime inputs are not
in the repository, so CI does not register the input-dependent suites (owner
coverage, data-root and package contracts) and cannot run hidden WARP module
coverage. These remain laptop checks. CI never runs the performance gate
against `EndfieldHUD.exe`.

## Performance gates (`perf_gate.hpp`, `perf_report.hpp`, `perf_probe.hpp`, `perf_runner.hpp`, `tools/package_perf_gate.cpp`)

**One command on the laptop's desktop.** Open a terminal on the laptop's
desktop (not over SSH) and run the tool
from the build's `Release` folder, next to `EndfieldHUD.exe`:

```powershell
& "<build>\Release\package_perf_gate.exe" --desktop
```

For the packaging slot build on the test laptop that is
`& "$env:LOCALAPPDATA\EndfieldHUD-Windows-Rebuild-Test\slots\packaging\build\Release\package_perf_gate.exe" --desktop`.
Build it first with `cmake --build <build> --config Release --target EndfieldHUD endfieldhud_resources package_perf_gate`.

`--desktop` measures the `EndfieldHUD.exe` next to the tool, with the default
settings below, and writes
`Release\performance-reports\<local time>\performance-gate.json` and the
readable `performance-gate.md`. The run opens and closes the HUD about 100
times over roughly twelve minutes (ten more when the `EndfieldHUD.exe`
cannot shorten its graphics grace, see *The closed HUD and the graphics
grace* below), so the tool never starts on its own:

- No CTest, CI step or build target runs it against `EndfieldHUD.exe`. Its
  tests drive the packaging stand-in app only.
- Whenever the measured app would appear on a visible desktop, the tool first
  describes the run (program, temporary data, duration, report path, and how
  the release window is reached) and waits for Enter. Without a console to answer on it refuses unless `--yes` is
  given. `--desktop` itself refuses in a session without a visible desktop.
- An `EndfieldHUD.exe` without `--data-root`/`--toggle` support is never
  started (exit 2).
- Ctrl+C stops the run at the next wait. The app is stopped through the
  session-end protocol, the temporary data root is removed and the partial
  report is marked *cancelled* (exit 3).
- The measured app runs in a kill-on-close job object, so closing the console
  or killing the tool ends it too. Programs the app itself starts are not in
  the job.

Full form:

```
package_perf_gate --exe <EndfieldHUD.exe> --report <report.json> [--markdown <report.md>]
    [--scenarios closed,released,open,ambient-off,lifecycle] [--seconds 60] [--settle 10]
    [--cycles 100] [--open-settle 1.5] [--close-settle 1.5] [--interval 1] [--release-grace 10]
    [--limit <scenario>.<metric>=<value>]… [--no-startup-trace] [--work-folder <dir>] [--keep-data] [--yes]
package_perf_gate --render <report.json> [--markdown <report.md>]
```

The gate measures the process from outside: nothing is injected, and neither
administrator rights nor an ETW session is needed. It records the following:

- process CPU as a percentage of one core
- wake-ups: per-thread context switches from `NtQuerySystemInformation`, with the busiest threads named by description and start module
- handles, threads and GDI/USER objects
- private bytes (the commit charge, Task Manager's *Commit size*) and working
  set, with the process's peaks of both
- committed memory by region type (private, mapped, image) and the largest
  private allocations (`VirtualQueryEx`)
- every thread grouped by its start address (module+offset) and description,
  with what the module is: the Windows thread pool (recognized by the gate's
  own pool worker), graphics drivers, Media Foundation, COM, WinHTTP, WebView2,
  the program itself and other system components
- where the session exposes them, GPU engine utilization and GPU process memory (PDH)
- the cold and warm openings, from the app's own startup trace (below)

It runs these scenarios:

1. **Closed, within the graphics grace**, 60 s: the HUD starts closed
   (`--login`) and is sampled after `--settle`, while it still keeps its
   graphics device (below). `--scenarios closed`.
2. **Open idle** with ambient animation on. The opening before it is the
   **cold opening**.
3. **Lifecycle**: 100 open/close cycles. Growth is the median of the last 10
   cycles minus the median of the first 10 after warm-up. Its first opening is
   the **warm opening**.
4. **Openings**: the first session runs with `ENDFIELDHUD_STARTUP_TRACE=1`
   in its environment. EndfieldHUD then writes
   `<data root>\v1\Diagnostics\startup-trace.json` (numbers only) once both
   openings completed. For each opening the gate reports the time from the
   request to the first presented frame, to the completed entrance, the
   p50/p95/max interval between presented frames (without the first step),
   the frames later than 25 ms (1.5 × the 60 Hz budget), and the mean/max CPU
   of `present()`. It also lists the launch-to-first-frame time, the slowest
   startup stages and the module owners with the longest frame work during
   the cold opening. The HUD starts closed and the gate opens it only after
   the closed window within the grace, so the report then says that launch to the first
   frame is mostly that wait and gives the startup time before the window
   instead (the first desktop run showed 70975 ms there, of which 1098 ms
   were startup). This is the measurement for a slow or stuttering first
   opening after launch (section 9: "profile first and warmed opens
   separately", p50/p95 frame times, startup latency).
5. **Open idle** with ambient animation off.
6. **Closed, after the graphics release**, 60 s, in a separate launch: the
   HUD is opened once, closed, and left closed for the graphics grace plus
   `--settle` before the window. `--scenarios released`.
7. **Memory, threads and handles** (listed first in the report): private
   bytes, working set and their peaks, threads (also the graphics driver's
   threads alone), handles, GDI/USER objects, GPU memory, committed memory by
   type, the largest private allocations and the threads by start address at
   five points: closed after launch (the end of the window within the grace),
   open (the end of each open idle), closed after the cycles (`--settle`
   seconds after the last close, so deferred releases have run) and closed
   after the release (the end of that window). The report also states what
   the closed HUD kept after use (closed after the cycles minus closed after
   launch) and what the graphics release gave back (closed after the release
   minus closed after launch, two launches of the same program). This is the
   measurement for the reported footprint of the live build (426 MB private
   bytes and 95 threads soon after launch, 599 MB and 69 threads after use).

**The closed HUD and the graphics grace.** Since `3eecf89` EndfieldHUD keeps
its graphics device, swap chain and composition target for ten minutes after
closing, and after launch (`app/application.hpp`
`closedGraphicsGraceSeconds`), so every reopening within them stays fast
(about 10–18 ms in hidden traces); it releases them afterwards. While the
device lives, the graphics driver's thread wakes the process: 67–83 times a
second on the laptop's NVIDIA GPU, 0.05–0.10 times once released (hidden
traces). Release candidate 128's desktop run failed its single closed window
on 73.0 wake-ups/s, 67.2/s from that thread; that is now the intended
grace, so the gate measures the closed HUD on both sides of it:

- **Within the grace** the limit is 200 wake-ups/s. It allows for the live
  device (including the device-bound pool thread that brought hidden traces
  to about 178/s before `3eecf89`), while a closed HUD that presented at
  the display rate (165 Hz) would exceed it. The CPU limit is unchanged.
- **After the release** the strict limit of 20 wake-ups/s applies.
- In both, no frame may be submitted. The gate cannot count presents from
  outside, so GPU busy (≤ 0.1 %) stands for frame submission: a closed HUD
  submits no GPU work. It is measured where Windows exposes the GPU counters
  (the desktop session).

The window after the release would take more than ten minutes at the
production grace. A gate run therefore shortens the grace, for its separate
release launch only, through the environment variable
`ENDFIELDHUD_CLOSED_GRAPHICS_GRACE=<seconds>` (`--release-grace`, default
10 s, 1–3600). An `EndfieldHUD.exe` that reads this variable does so only
together with `--data-root`, the isolated temporary root of a gate run; any
other run keeps ten minutes, and production refuses another grace. The gate
recognizes such a build by the variable's name in its image. With a build
that does not read it (release candidate 133 and earlier), the gate waits the
production ten minutes once instead, and both the prompt and the report say
so. The other launches (within the grace, open, lifecycle, openings) never
set the variable, so they measure the production policy. The report states,
for each closed window, how long the HUD had been closed and which grace was
in effect.

Each run uses a fresh temporary data root (in the long form of `%TEMP%`) with
launch at login off and onboarding done. The app is stopped through the
session-end protocol. The report records the following:

- the OS build
- CPU, RAM, GPUs, DPI, refresh rate and display size
- the session, whether it has a visible desktop, and how many other
  EndfieldHUD processes were running
- the executable's version and SHA-256
- every setting and limit

The readable report (`--markdown`, or `--render` for a saved JSON report)
starts with the verdict and a summary table, lists the failures, then gives
each scenario's values with their limits (each closed window first says
which side of the graphics grace it measured), the busiest threads, the
opening tables, lifecycle growth, the settings (including both graces) and
the notes. A report of schema 2 (one closed window, before the grace) still
renders. Unavailable values (for
example GPU counters outside the console session) are written as *not
available*, never as zero. The console prints one line per scenario and the
paths of both reports.

The limits are provisional and configurable (`--limit`, `none` removes one):

| Scenario | Default limits |
| --- | --- |
| `closed` (within the grace) | CPU ≤ 0.5 % of one core, ≤ 200 wake-ups/s (a live graphics device), GPU ≤ 0.1 % (no frame submission), private bytes growth ≤ 4 MiB (the Mac measured 0.11–0.26 % closed) |
| `released` (after the release) | CPU ≤ 0.5 % of one core, ≤ 20 wake-ups/s, GPU ≤ 0.1 % (no frame submission), private bytes growth ≤ 4 MiB |
| `open` (ambient on) | CPU ≤ 10 %, ≤ 400 wake-ups/s, GPU ≤ 25 %, growth ≤ 8 MiB |
| `ambient-off` | CPU ≤ 5 %, ≤ 200 wake-ups/s, GPU ≤ 15 %, growth ≤ 8 MiB |
| `lifecycle` | growth of handles ≤ 16, threads ≤ 2, GDI/USER ≤ 8, private bytes ≤ 8 MiB, working set ≤ 24 MiB, dedicated GPU memory ≤ 16 MiB (warm-up and window 10 cycles) |
| `closed`, `after-cycles`, `released` footprint | private bytes ≤ 160 MiB, ≤ 40 threads; working set and handles reported (`private`, `working-set`, `threads`, `handles`) |
| `open`, `ambient-off` footprint | private bytes ≤ 256 MiB, ≤ 64 threads; working set and handles reported |
| `cold-open` | first frame ≤ 250 ms (the Mac 1.2 first synchronous open was 225 ms on an M2), p95 interval ≤ 25 ms, ≤ 3 frames later than 25 ms (`first-frame`, `p95-interval`, `late-frames`) |
| `warm-open` | first frame ≤ 100 ms, p95 interval ≤ 25 ms, ≤ 1 late frame |

The footprint limits compare with the Mac 1.2 physical footprint (96.5–98.2
MiB closed, 186–190 MiB during continuous tilt on an Apple M2,
`docs/performance-1.2.md`). Windows private bytes also count the graphics
driver's user-mode commit, so the limits allow some headroom; a closed HUD
after use must come back to the closed limit. Values are reported whether or
not a limit is set. Compositor latency, GPU duration and input latency need
ETW (WPR/PresentMon) and are not part of this gate.

A run that hits a fatal error is not measured as idle: EndfieldHUD writes
`<data root>\v1\Diagnostics\last-fatal.txt` and keeps a message box open,
so the gate checks for that record after startup, after the cold opening,
after a forwarded launch fails and at the end, and stops with the record's
exception, message and stage. This is what happens over SSH: a hidden run of
the slot's `EndfieldHUD.exe` on the laptop (2026-10-11) stopped after 3 s
with "Cannot locate the current display" (stage `launch > application
start`), left no process or temporary folder behind, and wrote both reports.
The measurement itself therefore needs the laptop's desktop.

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
| `packaging_perf_gate_contracts` (portable) | Idle arithmetic, gates, plateau versus leak detection; opening summaries from a startup trace (nearest-rank p50/p95, late frames, frame CPU), opening gates, malformed traces rejected, the launch timing of a HUD opened after the closed window; footprint gates, thread roles and grouping, the footprint scenario and its readable table, gates, graphics driver threads, threads by start address, the memory kept after use and what the graphics release gave back; the readable report and console summary of a full synthetic report with both closed windows (within the grace at 73 wake-ups/s, after the release with the shortened or the production grace), including unavailable GPU values, failures, a cancelled run and a schema 2 report |
| `packaging_ehudz01_contracts` (portable) | The decoder against the Mac oracle, and the staged-inventory verifier |
| `packaging_installer_contracts` | 385 checks through the real Setup/uninstall programs with synthetic payloads in `%TEMP%\ehud-<id>` and a private HKCU subtree. Covers fresh install, repair, upgrade, launch-at-login choices, refusals (including Windows builds below 22000), rollback after every step, interrupted-swap recovery, the Restart Manager close/restart of a stand-in app, uninstall relocation, kept data, explicit data removal and the signed layout. Outside the interactive console session (CI, the laptop's SSH session) it also drives the task-dialog windows with dialog messages: install with the box cleared, uninstall with confirmed data removal, *Keep my data*, and cancel |
| `packaging_third_party_notices_contracts` (portable) | `THIRD-PARTY-NOTICES.txt` against `third-party-notices.json`: every `third_party/` folder, bundled font, licence or notice file and licence-marked source comment is covered, every component's licence text is in the rendered file, the copies in `licenses/` equal their macOS originals (or the notice their vendored file embeds), every source file that embeds Unicode Character Database data (the Unicode licence notice or a generated Unicode tables note, such as QuickJS-NG's `libunicode-table.h`) is named by a Unicode-licensed component, no dependency is fetched by the build, and every licence file inside a folder staged into `resources/` (also external inputs such as the OrbiPom folder) is in the notices |
| `endfieldhud_release_contracts` | The real install tree staged, zipped, packed, installed (isolated), checked with the installed `EndfieldHUD.exe --verify-package` and uninstalled. Also: `THIRD-PARTY-NOTICES.txt` in the portable zip, the setup's file list and the install manifest; SHA256SUMS lists exactly the setup and the portable zip; the symbols zip has its own checksum file; the release-notes draft is `windows/RELEASE-NOTES.md` with its checksums filled in |
| `packaging_perf_probe_contracts`, `packaging_perf_gate_tool`, `packaging_perf_gate_render`, `packaging_perf_gate_refuses_unsupported` | The gate against the stand-in app: a quiet process passes, one handle/GDI leak per cycle and a 10 ms wake-up are caught and attributed; the graphics grace: the grace override is recognized in an image (UTF-16 or 8-bit) and without it the release launch waits the production ten minutes, a stand-in device that wakes about 100 times a second passes the higher bound within the grace and is gone after the release (shortened to 1 s through `ENDFIELDHUD_CLOSED_GRAPHICS_GRACE` from the gate's environment), its thread shows in the census within the grace only, and a device kept past the grace fails the window after the release; 48 MiB held while open shows in private bytes, working set and the largest allocations and is gone after the cycles, three described threads are grouped by start and the Windows thread pool is recognized, and an open footprint over its limit fails; `ENDFIELDHUD_STARTUP_TRACE=1` reaches the app through the environment block and its cold and warm openings are reported, a slow cold opening fails its gates; a cancelled run stops at once with a partial report; the measured process ends when the gate tool is killed (kill-on-close job); the tool writes and re-renders the readable report; an executable without `--data-root` support is never started |
| `packaging_identity_consistency` | Setup's notification identity equals the Calendar area's |
| `packaging_watch_runtime_staged` (CI, where `Resources/` and Python exist) | The Mac packager's real staged tree verifies natively |
| `packaging_update_check_contracts` (portable) | The release checker against the Mac oracle (59 tags, every ordered pair, 36 page URLs, Foundation's control characters for every scalar, 31 client scenarios). Also the Windows namespace and installer-asset rules, SHA256SUMS parsing, redirect rules, and the updater controller over synthetic routes and temporary folders. The controller cases cover check, consented download and install, the HUD closing first, the saved hand-off, its outcomes on the next run or at Setup exit, failures, discover-only copies, the daily check and its preference, Retry-After, deadlines, stop, and bounded files |
| `packaging_update_platform_contracts` | WinHTTP against a loopback fixture server: headers, no redirects followed, size bounds, file mode, cancellation, host-clock deadlines, and refusal of plain HTTP and credentials in production. The session's lifetime: none and no thread at construction, open only while a request waits or runs, opened on a background queue that serves every waiting request, requests cancelled or expired while it opens never reach the network, a refusing queue opens it on the owner thread, and one opened for a destroyed transport is closed. Also the installed-copy check in a private HKCU subtree, verification of synthetic setups (checksum, version, incomplete, tampered payload), the held file, and the launcher with a stand-in Setup. The updater runs end to end over those services |
| `packaging_os_text_data_contracts` | The OS ICU and time-zone data meet `os-requirements.json` (CI runs the same check before its suites) |
| `application_owner_coverage` (area `windowsUpdates`) | The area in the production owner with synthetic routes. Covers Mac, draft and prerelease releases ignored, the redirects, the real payload reader on a synthetic setup, the install hand-off after the HUD closes, a refused Setup, ETag, the daily check, Retry-After, a tampered checksum, a hostile redirect and a Mac-only feed |
