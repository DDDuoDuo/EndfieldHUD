<!--
EndfieldHUD Windows release notes template: the first Windows release.

stage_release.cmake renders this file into <dist>/<artifact stem>-release-notes.md
with the staged artifact names, sizes and SHA-256 values filled in. It is a
draft for DDDuoDuo: nothing is published by the build or by CI.

The text below describes the FIRST Windows release, built from the macOS
v1.2.0 build 18 parity baseline (windows/source-authority.json). For any later
Windows release, replace "What's in this release" with that release's changes
and check "Differences from the macOS version" again.

Publishing checklist (maintainer, after choosing a Windows release):
  1. Stage from a clean Release build with the Mac-exported inputs configured,
     so no artifact name contains "-incomplete".
  2. On the test laptop's desktop, run the section 9 performance gate against
     that build (Release\package_perf_gate.exe --desktop) and read its
     performance-gate.md; resolve or knowingly accept every FAIL. Compare its
     "Memory, threads and handles" section (private bytes, working set,
     threads and handles closed after launch, open, and closed after the 100
     cycles) with the previous release's report: a release must not grow them.
  3. Check the live acceptance list (WINDOWS-MIGRATION.md sections 9 and 11)
     and compare "Differences from the macOS version" below with the platform
     gap tables in WINDOWS-MIGRATION.md section 6. Remove rows that were
     resolved; add any new one. Then remove this comment.
  4. Create the release under the Windows tag namespace only, never a Mac tag:
       gh release create windows-v@EHUD_NOTES_VERSION@ --target <commit> --latest=false \
         --title "EndfieldHUD @EHUD_NOTES_VERSION@ for Windows" --notes-file <this file> \
         "@EHUD_NOTES_SETUP@" "@EHUD_NOTES_PORTABLE@" "@EHUD_NOTES_SUMS@"
     Add --prerelease for a SemVer prerelease tag (windows-v1.3.0-beta.1).
     The macOS app's release checker rejects every windows-v tag, and
     updates/appcast.xml (the Mac Sparkle feed) is never edited for Windows.
  5. Upload exactly these three artifacts: the Windows updater installs a
     release only when its setup and SHA256SUMS assets carry these names.
  6. Do not upload a symbols zip, a size report or an "-incomplete" artifact.
-->
@EHUD_NOTES_DEVELOPER@# EndfieldHUD @EHUD_NOTES_VERSION@ for Windows (build @EHUD_NOTES_BUILD@)

The first EndfieldHUD release for Windows. It brings the macOS app's HUD,
version @EHUD_NOTES_VERSION@ (build @EHUD_NOTES_BUILD@), to 64-bit Windows as a
native program, with the same look, animations and data formats. This is a
Windows release (tag `windows-v@EHUD_NOTES_VERSION@`). It does not change the
macOS app, its downloads or its update feed.

## What's in this release

Press **Ctrl + backtick (`)** to open the HUD; change the hotkey in
**Hotkeys**. EndfieldHUD lives in the notification area (system tray). Closing
the HUD keeps it running; the red power button quits it.

Every tool of the macOS version is included:

| Tool | What you can do |
| --- | --- |
| Notes | Write, draw, make checklists, and add images or videos. Pin panels across sections. |
| Temporary File Shelf | Drop files in, preview them, and drag them back out. Originals stay where they are. |
| Clipboard Cache | Scroll through recent copied text, links, images and files. History clears when you quit. |
| Archive | Keep documents in your own categories, with rich text and media. |
| E-Reader | Read novels and PDFs, bookmark pages and zoom in. |
| Media Assembly | Edit pictures and videos with cropping, adjustments, filters and stickers. |
| Projection | Draw on a screen-sized canvas, with an optional dotted background. |
| Now Playing | See album art and available lyrics, and control playback. |
| Work Mode & Volume | Run a focus timer, see audio devices and adjust per-app volume. |
| Calendar | Run a timer or stopwatch, and keep events with reminders. |
| Map | Explore the offline terrain map and place and customize pins. |
| Closure's Minigame | Play Merge! OrbiPom! |
| Event Log | Check the event log like a real Endministrator. |
| Personal ID | Customize your card. |
| Account Binding | Link a HYPERGRYPH game account for profile data and sanity countdowns (optional). |
| Battery, Storage & Activity Monitor | Check charging status, disk space, CPU, RAM and running apps. |
| App shortcuts | Add your apps with custom names and icons. |

Settings, themes, tilt, clocks, icons and animation settings work as on the
Mac, in English, Simplified Chinese, Traditional Chinese, Japanese and Korean.
Your data stays on your PC.

## Install

1. Download `@EHUD_NOTES_SETUP@` (@EHUD_NOTES_SETUP_SIZE@).
2. Run it. EndfieldHUD installs for your Windows account only, into
   `%LOCALAPPDATA%\Programs\EndfieldHUD`, without administrator rights. Your
   data stays in `%LOCALAPPDATA%\EndfieldHUD`. Setup can start EndfieldHUD
   when you sign in to Windows (on by default, as on the Mac).

@EHUD_NOTES_SIGNING@

The portable zip (`@EHUD_NOTES_PORTABLE@`, @EHUD_NOTES_PORTABLE_SIZE@) contains
the same program without the installer. A portable copy does not update itself
and has no Calendar reminder notifications (they need the Start menu entry that
Setup creates).

To uninstall, open **Settings → Apps → Installed apps → EndfieldHUD →
Uninstall**. Your data is kept unless you tick *Also delete my EndfieldHUD
data* in the uninstaller and confirm.

## Moving from a Mac

The Mac app has no export button; a small script exports its data.

1. On the Mac, quit EndfieldHUD and run
   [`mac_import_exporter.py`](https://github.com/DDDuoDuo/EndfieldHUD/blob/windows-v@EHUD_NOTES_VERSION@/windows/tools/mac_import_exporter.py):
   `python3 mac_import_exporter.py ~/Desktop/EndfieldHUD-export`
2. Copy the `EndfieldHUD-export` folder to the PC.
3. In EndfieldHUD, open **Settings → System → Import macOS data…**, choose the
   folder, review what will be imported and confirm. EndfieldHUD restarts once
   and reports the result. Your Windows data is backed up first.

Notes, the shelf list, profile, Archive, Reader library, Calendar, Map pins,
App shortcuts, Event Log, account preferences, the custom center logo,
settings and the minigame best score are imported. Linked accounts must sign
in again: credentials never leave the macOS Keychain. Files the Mac referred
to (shelf items, books, app shortcuts, note media outside EndfieldHUD) must be
relinked; the import lists them. A custom summon shortcut must be set again in
**Hotkeys** (Ctrl + backtick carries over). Clipboard history, Media Assembly
sessions and minigame boards are session-only and are not imported.

## Update

- **From EndfieldHUD for Windows:** open **About** and choose *Check for
  updates* (it also checks once a day when the HUD opens, if *Check for
  updates daily* is on). When a new version is listed, click the row to
  download and verify it, then click again to install. EndfieldHUD closes,
  saves your data, updates in place and reopens.
- **Or** run the new setup over the installed version. Settings and data are
  kept.

## Differences from the macOS version

These come from Windows itself, or from choices that keep your data and
system safe. The full list, with the reasons, is in
[WINDOWS-MIGRATION.md](https://github.com/DDDuoDuo/EndfieldHUD/blob/windows-v@EHUD_NOTES_VERSION@/WINDOWS-MIGRATION.md)
(section 6, "Windows platform gaps").

- **Updates:** builds are not code-signed yet, so updates are never installed
  automatically and there is no "update available" notification. Each update
  needs your two clicks in About; the tray item and About show when one is
  available.
- **Temporary File Shelf:** files cannot be dropped on the tray icon (Windows
  offers no drop target there). Drop them on the open HUD, use *Add files* or
  press Ctrl+V. Items that are not real files (Outlook attachments, images
  dragged from a browser, files inside zip folders) are refused. Items moved
  to the Recycle Bin, to another drive or to some network shares show as
  *Unavailable*. An item's preview is a properties card inside the HUD, with
  *Open* and *Reveal*, instead of Quick Look.
- **Now Playing:** players appear when they publish to the Windows media
  controls; NetEase Cloud Music, QQ Music and Kugou are also recognized by
  their window titles. Other players show *No music playing*. A player's own
  volume is not available on Windows: the volume control uses the app's
  mixer level, and browsers or multi-process players are adjusted in Volume.
  Timed lyrics come from the file's embedded lyrics, the NetEase catalog
  (NetEase only) or LRCLIB.
- **Volume:** the default output and input devices are switched in Windows
  Settings (Windows has no supported way for apps to change them). Per-app
  volume acts on the app's sound on the default device; per-app output
  routing is not available. Windows remembers each app's mixer level, so
  EndfieldHUD restores the levels it changed when it quits or the device
  changes. The app list shows three rows (two on the Mac) and scrolls.
- **Media Assembly:** videos are exported as MP4 (H.264/AAC) instead of
  QuickTime MOV. HEIC/HEIF, HEVC, WebP and some other formats need optional
  codecs from the Microsoft Store; without them they are reported as
  unsupported. The Highlights/Shadows slider has no effect yet (saved amounts
  are kept), and 16-bit images are edited with 8-bit precision.
- **Notes and Archive:** video notes need Media Foundation, which Windows N
  editions lack until the Media Feature Pack is installed. Home and End move
  to the start or end of the line, and Ctrl+Home and Ctrl+End to the start or
  end of the text, as in other Windows text fields.
- **E-Reader and Map:** Ctrl + mouse wheel zooms. Touchpad pinch-to-zoom is
  not supported yet, and touchpad scrolling moves in mouse-wheel steps.
- **Calendar:** reminders are Windows notifications. Windows has no
  permission prompt for them: they follow your notification settings, and
  Focus assist can hold the banner back (it still reaches the notification
  center).
- **Work Mode:** the timer does not switch Windows Focus / Do not disturb,
  which Windows reserves for approved apps.
- **Activity Monitor:** per-app disk and network usage are not shown
  (Windows requires administrator rights for them). App memory is the private
  working set, as in Task Manager. Apps without a window are not listed, and
  compressed memory is not shown.
- **Battery:** the charge alert cannot appear over exclusive-fullscreen games
  (the reading, badge, tray and Event Log still update). Battery health shows
  *Unavailable*: Windows reports no health category.
- **Account Binding:** sign-in uses the Microsoft Edge WebView2 Runtime
  (included in Windows 11; EndfieldHUD links to Microsoft's download page if
  it is missing).
- **App shortcuts:** Microsoft Store apps use the default icon. Opening a
  running app restores its window for regular programs and starts Store apps
  normally.
- **Dialogs:** file pickers and the color picker are the standard Windows
  dialogs. The color picker applies colors live; *OK* keeps them and *Cancel*
  restores the previous color.
- **Text and dates:** EndfieldHUD uses the Unicode and time-zone data of
  Windows. Before the Windows 11 24H2 code base, some text segmentation
  (Indic scripts) and recent time-zone rules (for example Samoa's) can differ
  from the Mac. Pasted Mac fonts Times and Times New Roman both become Times
  New Roman.
- **Screen readers:** the section buttons and every module's controls are
  available to UI Automation. Text fields expose their whole value rather
  than a text pattern, and the HUD's controls are used with the HUD's own
  keys rather than as Tab stops.
- **App icon:** the *App / menu bar icon* setting changes the tray icon;
  File Explorer, Start and notifications keep the program's own icon.
- **Minigame:** the rules and scoring match the Mac. Because floating-point
  math differs slightly between Windows and macOS, long-running physics can
  take a different (still deterministic) path.

## Requirements

- 64-bit (x64) Windows 11, or Windows 10 version 2004 or later. Tested on
  Windows 11 25H2 (build 26200). There is no ARM64 build.
- Account sign-in uses the Microsoft Edge WebView2 Runtime, which Windows 11
  includes.

## Verify the download

The checksums are listed in `@EHUD_NOTES_SUMS@`. In PowerShell:

```powershell
Get-FileHash .\@EHUD_NOTES_SETUP@ -Algorithm SHA256
```

| File | Size | SHA-256 |
| --- | ---: | --- |
| `@EHUD_NOTES_SETUP@` | @EHUD_NOTES_SETUP_SIZE@ | `@EHUD_NOTES_SETUP_SHA256@` |
| `@EHUD_NOTES_PORTABLE@` | @EHUD_NOTES_PORTABLE_SIZE@ | `@EHUD_NOTES_PORTABLE_SHA256@` |

Ported from the macOS source at commit `@EHUD_NOTES_COMMIT@`.
