<!--
Draft notes for the first Windows release (tag windows-v1.2.0), and the only
source of its release notes: the endfieldhud_release staging build writes
EndfieldHUD-1.2.0-build18-windows-x64-release-notes.md, which is this file
with the two SHA-256 values under "Verify the download" filled in from the
staged setup and portable zip. Nothing is published from either file. Before
publishing:
  1. Stage a complete Release build: no "-incomplete" artifact names and no
     "Do not publish" mark in the staged draft. Check the installed size in
     the staged size report.
  2. Run the section 9 gate on the test laptop's desktop against this build
     (package_perf_gate --desktop) and replace the "Performance" figures
     with its report.
  3. Install, upgrade and uninstall the setup live (with the SmartScreen
     prompt). Done on the test laptop with release candidates 135 and 136
     (2026-10-16): installed, updated over the installed copy with the data
     kept, and uninstalled from Settings → Apps. Remove every item under
     "Not yet checked on a real PC" that has passed.
     Blocker: when Setup opens EndfieldHUD after an install or an update,
     the HUD appears behind the other windows (openings by the hotkey come
     to the front; tester report on release candidate 136). Do not publish
     until a release candidate with the fix has been checked live.
  4. Compare "Known differences and limitations" with WINDOWS-MIGRATION.md
     section 6 at the release commit.
  5. Create the release under the Windows tag namespace only, from the
     staged draft:
       gh release create windows-v1.2.0 --target <commit> --latest=false
         --title "EndfieldHUD 1.2.0 for Windows" --notes-file <staged draft>
         <setup.exe> <portable.zip> <SHA256SUMS.txt>
     Upload exactly these three files: the Windows updater finds the setup
     and SHA256SUMS by these names. Never upload the symbols zip, its
     checksum file, the size report or an "-incomplete" artifact, and never
     touch the Mac release or updates/appcast.xml.
  6. The windows-v1.2.0 links work only once the tag exists.
Remove this comment before publishing.
-->
# EndfieldHUD 1.2.0 for Windows

The first Windows release of EndfieldHUD. It brings the same layered, animated HUD and the same tools as the macOS app, version 1.2.0 (build 18), to 64-bit Windows 11 as a native program, with the same look, animations, languages and data formats.

This is a Windows-only release (tag `windows-v1.2.0`). It does not change the macOS app, its downloads or its update feed, and Macs are never offered Windows releases.

This release is not code-signed; Windows SmartScreen may show **Windows protected your PC** the first time it runs. See [Install](#install).

## Requirements

- **Windows 11, 64-bit (x64).** Live testing has been done on one laptop: Windows 11 version 25H2 (build 26200), Intel Core Ultra 7 251HX, NVIDIA GeForce RTX 5060 Laptop GPU, 2560 × 1600 at 165 Hz and 150 % scaling. Other PCs, graphics cards and Windows 11 versions have not received equivalent testing.
- **Windows 10 is not supported.** The HUD's blurred background uses a window setting that Microsoft documents from Windows 11 onward, and EndfieldHUD has not been tested on Windows 10. Setup does not install before Windows 11 (build 22000), and a portable copy reports that Windows 11 is required. There is no ARM64 build.
- **Graphics:** a graphics card and driver with Direct3D 11 (feature level 11_0), including Direct3D 11 video support. The HUD has no software-drawing fallback.
- **Account Binding** needs the Microsoft Edge WebView2 Runtime. If it is missing, *Connect* explains this and links to Microsoft's download page; once it is installed, *Check Again* continues without a restart.
- **Videos** need Windows Media Foundation. On Windows N editions, install the Media Feature Pack for videos; images and GIFs work without it.
- About 61 MiB of disk space for the program, plus your data. The Visual C++ runtime is built in.

## Install

Both downloads contain the same program:

- **Setup (recommended):** `EndfieldHUD-1.2.0-build18-windows-x64-setup.exe`
- **Portable:** `EndfieldHUD-1.2.0-build18-windows-x64-portable.zip`

### Setup

1. Download the setup. You can check it first (see [Verify the download](#verify-the-download)).
2. Run it. EndfieldHUD installs for your Windows account only, into `%LOCALAPPDATA%\Programs\EndfieldHUD`. Administrator permission is not needed.
3. Keep *Launch at login* ticked to start EndfieldHUD when you sign in (on by default, as on the Mac), choose *Install*, then *Open EndfieldHUD*.

Setup adds EndfieldHUD to the Start menu and to **Settings → Apps → Installed apps**. If you turned EndfieldHUD off in **Settings → Apps → Startup**, Setup and EndfieldHUD leave it off.

Running a newer setup over the installed version updates it and keeps your data: Setup closes EndfieldHUD after saving your data and reopens it when it is done. Setup does not install an older version over a newer one.

To uninstall, open **Settings → Apps → Installed apps → EndfieldHUD → Uninstall**. Your notes, settings and other data are kept unless you tick *Also delete my EndfieldHUD data* and confirm. A small copy of the uninstaller stays in your temporary folder until the next Setup or uninstall run removes it, because Windows does not let a running program delete itself.

### Windows SmartScreen

The setup, the uninstaller and `EndfieldHUD.exe` are not code-signed yet: there is no code-signing certificate. The first time a downloaded copy runs, Windows may show **Windows protected your PC**. If the file came from this release page and its checksum matches, choose **More info → Run anyway**. You do not need to turn off SmartScreen or any other Windows security feature.

### Portable zip

Extract the zip anywhere and run `EndfieldHUD\EndfieldHUD.exe`. A portable copy:

- uses the same data folder as an installed copy, and only one EndfieldHUD runs at a time;
- does not update itself: *Check for updates* shows new releases and opens their page;
- has no Calendar reminder notifications, which need the Start menu entry that Setup creates;
- registers itself to start when you sign in while *Launch at login* is on in **System** (the default);
- has no uninstaller: turn off *Launch at login*, quit with the red power button, then delete the folder.

### Where your data is

Everything EndfieldHUD saves is in `%LOCALAPPDATA%\EndfieldHUD` (the current data in its `v1` folder). A linked account's sign-in is stored there, encrypted for your Windows account.

Your notes and settings stay on your PC. EndfieldHUD goes online only for account linking, update checks on GitHub, and Now Playing's covers and lyrics, which are looked up only while Now Playing is shown.

## Getting started

Press **Ctrl + backtick (`)** to open the HUD. You can change the hotkey in **Hotkeys**. EndfieldHUD lives in the notification area (system tray); click its icon for its menu. If Windows hides the tray icon, start EndfieldHUD again from the Start menu to open the HUD.

Use the side buttons to switch sections, and scroll the right side, or click the arrows at its ends, for more. **Esc** backs out of an edit before closing the HUD, and clicking outside the HUD closes it. Closing it keeps EndfieldHUD running; the red power button asks for confirmation and quits.

## What's included

Every tool of the macOS version:

| Tool | What you can do |
| --- | --- |
| Notes | Write, draw, make checklists, and add images or videos. Pin panels across sections. |
| Temporary File Shelf | Drop files in, see their properties, and drag them back out. Originals stay where they are. |
| Clipboard Cache | Scroll through recent copied text, links, images and files. History clears when you quit. |
| Archive | Keep documents in your own categories, with rich text and media. |
| E-Reader | Read TXT, EPUB and PDF books, bookmark pages and zoom in. |
| Media Assembly | Edit pictures and videos with cropping, adjustments, filters and stickers. |
| Projection | Draw on a screen-sized canvas, with an optional dotted background. |
| Now Playing | See album art and available lyrics, and control playback. |
| Work Mode & Volume | Run a countdown or stopwatch and count your work hours. See audio devices and adjust per-app volume. |
| Calendar | Keep events with reminders. |
| Map | Explore the offline terrain map and place and customize pins. |
| Closure's Minigame | Play Merge! OrbiPom! |
| Event Log | Check the event log like a real Endministrator. |
| Personal ID | Customize your card. |
| Account Binding | Link a HYPERGRYPH game account (Mainland China or Global) for profile data and sanity countdowns (optional). |
| Battery, Storage & Activity Monitor | Check charging status, disk space, CPU, GPU, RAM and running apps. |
| App shortcuts | Add programs, shortcuts and Microsoft Store apps with custom names and icons. |

In **System**, **Display**, **Hotkeys** and **About**, change the theme and theme color, tilt, clock styles, center logo, icons, display and animation settings. The HUD supports English, Simplified Chinese, Traditional Chinese, Japanese and Korean.

### Windows-only additions

- **Activity Monitor** shows GPU usage (the GPU's total load) before CPU, in the overview and per app.
- **Storage** can show another drive: choose it from the "C: ▾" menu.
- **Battery alert:** *Display method* has a third choice, *Don't display*, and *Reading* has a *GPU* choice.
- **Notes and Archive:** the font and size menus have a draggable scroll bar.
- **Volume** shows three app rows instead of two, and scrolls for more.
- **Hotkeys:** *Summon HUD with hotkey* turns the hotkey off; the tray icon still opens the HUD.
- **System:** *Import macOS data…* (see below).
- On displays larger than the MacBook the HUD was designed on, the center content is scaled to fill the center ring in the same proportion as on that MacBook. The Mac caps this scale, so its center content looks smaller on large displays.

## Moving from a Mac

The Mac app has no export button; a small script exports its data once.

1. On the Mac, quit EndfieldHUD, download [`mac_import_exporter.py`](https://github.com/DDDuoDuo/EndfieldHUD/blob/windows-v1.2.0/windows/tools/mac_import_exporter.py) and run it with Python 3:
   `python3 mac_import_exporter.py ~/Desktop/EndfieldHUD-export`
   It copies your EndfieldHUD data and settings into that new folder. It changes nothing on the Mac and never reads the Keychain.
2. Copy the `EndfieldHUD-export` folder to the PC.
3. In EndfieldHUD, open **System → Import macOS data…** and choose the folder. EndfieldHUD checks every file, then lists each kind of data for you to review.
4. Confirm. EndfieldHUD restarts once, opens on **System** and shows the result and the files to relink. Your Windows data is backed up first, as a `v1.backup-…` folder in `%LOCALAPPDATA%\EndfieldHUD`; the import never deletes it.

**Imported:** notes and their images, the Temporary File Shelf list, your profile with its avatar and background, Archive, the E-Reader library, Calendar events, Map pins, app shortcuts, the Event Log, account preferences, the custom center logo, settings and the minigame best score. Display and hotkey settings you already chose on this PC are kept, and the higher best score wins.

**Not imported, or needs a step:**

- Linked accounts must sign in again: credentials never leave the macOS Keychain.
- Files the Mac only referred to (Shelf items, books, app shortcuts, note and Archive media kept outside EndfieldHUD) must be relinked; the import lists them. Relink each from its tool, for example **Library → Relink…** in E-Reader, or a double-click on a Shelf item.
- A custom summon shortcut must be set again in **Hotkeys**; the default Ctrl + backtick carries over.
- Calendar reminders are scheduled again on Windows (with an installed copy).
- Clipboard history, Media Assembly sessions, minigame boards and a running Work Mode timer are not saved on the Mac either, so there is nothing to import. Your lifetime work hours do carry over.
- Mac update settings are not imported.
- Data made by a newer version, or that cannot be read, is skipped only if you choose *Skip and Import*. Importing again asks first (*Import Again*) and makes a new backup.

The import has been tested with sample exports, not yet with a real Mac's data.

## Updates

- In **About**, choose *Check for updates* (the tray's *Check for Updates…* does the same). With *Check for updates daily* on (the default), EndfieldHUD checks at most once a day, when the HUD opens.
- When a new version is listed, click the update row once to download and verify it, and again to install it. EndfieldHUD closes, saves your data, updates in place and reopens.
- Updates are never installed automatically, and there is no "update available" notification; the tray menu and About show when one is available.
- Because the builds are unsigned, a download is checked against SHA256SUMS from the same release. This confirms the file is intact, but it is not an independent signature.
- Only a copy installed with Setup updates itself. You can also run a newer setup yourself.

## Known differences and limitations

### Differences from the Mac

These come from Windows itself, or from choices that keep your data and system safe. The full list, with the reasons, is in [WINDOWS-MIGRATION.md](https://github.com/DDDuoDuo/EndfieldHUD/blob/windows-v1.2.0/WINDOWS-MIGRATION.md) (section 6, "Windows platform gaps").

- **Temporary File Shelf:** files cannot be dropped on the tray icon (Windows offers no drop target there). Drop them on the open HUD's Shelf or its button, use *Add files*, or press Ctrl+V. Items that are not real files (Outlook attachments, images dragged from a browser, files inside zip folders) are refused with a message. Items moved to the Recycle Bin, to another drive or to some network shares show as *Unavailable*. The eye button, a double-click or Space shows a properties card with *Open* and *Reveal* instead of a preview.
- **Now Playing:**
  - Players that publish to the Windows media controls (for example Spotify and browsers) work through them. NetEase Cloud Music, QQ Music, KuGou Music, Kuwo Music, foobar2000, AIMP, PotPlayer and VLC are also found by their window and read through their own on-screen controls; only NetEase has been checked on a real PC. Other players show *No music playing*.
  - NetEase Cloud Music 3 cannot be seeked from Now Playing, with or without its *开启 SMTC* setting. Its time comes from NetEase's player bar and the song's length from NetEase's own local play queue (read only); without a confident match there is no time. While NetEase is minimized its time may stop. A seek made in NetEase while it is paused shows once the song changes, playback resumes or you open Now Playing again.
  - To read NetEase's player bar, EndfieldHUD turns on NetEase's built-in accessibility support once; it stays on until NetEase exits.
  - A player's own volume cannot be set on Windows: the volume control changes the player's level in the Windows volume mixer. Browsers and players with several audio processes cannot be adjusted here; use Volume.
  - Lyrics come from the song file's embedded lyrics, then the NetEase catalog (NetEase only), then LRCLIB.
  - A player that starts hidden in the tray appears the next time you open the HUD or a player changes.
- **Volume:** switch the default output or input device in Windows Settings; Windows offers apps no supported way to change it. Per-app volume acts on each app's sound on the default device; sending an app to another device is not available. Exclusive-mode audio is not detected. Windows remembers each app's mixer level, so EndfieldHUD restores the levels it changed when it quits, the PC sleeps or the device changes.
- **Media Assembly:** videos export as MP4 (H.264/AAC), named `<name>-edited.mp4`, instead of QuickTime MOV. HEIC/HEIF, HEVC, ProRes, JPEG 2000, WebP and some older formats need optional codecs from the Microsoft Store or are not supported by Windows; without them they are reported as unsupported.
- **Notes and Archive:** Home and End move to the start or end of the line, and Ctrl+Home and Ctrl+End to the start or end of the text, as in other Windows text fields. Clicking to the right of a line that wraps puts the caret before the space where the line wraps; the Mac puts it after that space. In Archive, the field you were typing in keeps its caret when you switch to another module and back (the Mac ends editing); your text is saved when you switch. Picture data dragged without a file cannot be dropped on Notes: copy it and press Ctrl+V. Pasted Mac fonts Times and Times New Roman both become Times New Roman.
- **Calendar:** reminders are Windows notifications and need a copy installed with Setup. Windows has no permission prompt for them: they follow your notification settings, and Focus assist (Do not disturb) can hold the banner back; it still reaches the notification center.
- **Work Mode:** the timer does not switch Windows Focus or Do not disturb, which Windows reserves for approved apps. The timer, completion and lifetime hours work as on the Mac.
- **Activity Monitor:** per-app disk and network use are not shown (Windows needs administrator rights for them). App memory is the private working set, as in Task Manager's Memory column. Apps without a window are not listed, compressed memory is not shown, and CPU shows unavailable on PCs with more than 64 logical processors. The GPU figure is the total load, so it can read higher than Task Manager's GPU column; it shows "—" where the graphics driver offers no GPU counters.
- **Battery:** the charge alert cannot appear over games in exclusive full screen; the reading, badge, tray and Event Log still update. Battery health shows *Unavailable*, because Windows reports no health category. A battery that reports only a relative capacity shows it as `p / 100 %`.
- **Account Binding:** sign-in uses the Microsoft Edge WebView2 Runtime (see [Requirements](#requirements)).
- **App shortcuts:** Windows has no "reopen" signal for a running app. Opening a regular program that is already running brings its window forward; Store apps and other targets start normally.
- **Hotkeys:** the summon hotkey may not reach EndfieldHUD while an app running as administrator is in front, or in games whose anti-cheat captures the keyboard. Use the tray icon then.
- **Settings:** *App / menu bar icon* changes the tray icon; File Explorer, Start and notifications keep the program's own icon. The color pickers are the standard Windows dialog: colors apply live, *OK* keeps the new color and *Cancel* restores the previous one.
- **Clipboard Cache:** while another app holds the clipboard open, Clipboard shows *Clipboard unavailable* until the next copy.
- **Look:** the blurred background uses Windows' own blur, and text is drawn with Windows fonts and fonts bundled with EndfieldHUD, so both can look slightly different from the Mac.
- **Screen readers:** every tool's controls are available to Windows UI Automation. Text fields expose their whole text as one value rather than a text pattern, and the HUD's controls are used with the HUD's own keys rather than as Tab stops.
- **Minigame:** the rules and scoring match the Mac. Floating-point math differs slightly between Windows and macOS, so long-running physics can take a different (still repeatable) path.
- **Text and dates:** EndfieldHUD uses Windows' Unicode and time-zone data. On Windows 11 versions before 24H2, some Indic-script text segmentation and recent time-zone rules (for example Samoa's) can differ from the Mac, and a few historical dates can fall under a different daylight-saving rule.
- Smaller differences (Event Log audio-device events, clipboard link parsing, some color-profile conversions, the profile photo crop's resampling) are listed in WINDOWS-MIGRATION.md.

### Limitations in this release

- Builds are not code-signed yet (see [Windows SmartScreen](#windows-smartscreen)).
- **Archive:** the title field is not yet inset exactly like the Mac's.
- **E-Reader and Map:** touchpad scrolling moves in mouse-wheel steps.
- **Media Assembly:** the Highlights/Shadows slider has no effect yet (saved amounts are kept). 16-bit images are edited with 8-bit precision. Movies tagged with a color space other than BT.709 keep their decoded colors.
- **Projection:** lifting the pen after a very long stroke takes about 13–15 ms to redraw it.

### Not yet checked on a real PC

Automated tests run every tool over sample data, but these have not been tried live yet:

- a full tool-by-tool check of this build on the desktop;
- updating from one published Windows release to another (this is the first);
- signing in with a Global account (a Mainland China account worked);
- Now Playing with any player other than NetEase Cloud Music;
- Calendar reminder notifications, and clicking one;
- the Mac data import with a real Mac's data;
- Narrator and other screen readers, and Japanese and Korean input methods (Chinese input works);
- pinch-to-zoom on a real touchpad (Map, E-Reader and Media Assembly);
- screen recording with the HUD's cursor;
- sleep and resume, a graphics driver reset, plugging monitors with different scaling in or out, and a monitor on another graphics card;
- the summon hotkey in the games and apps where earlier test builds missed it;
- exclusive-mode audio, and per-app volume with real apps;
- video formats beyond those on the test laptop;
- Volume's app icons in the installed app.

## Performance

These figures come from automated runs on the test laptop with sample data, with the HUD drawing off-screen at 1280 × 800. The full on-screen performance check of this build has not been run yet, and other PCs will differ.

- **Closed HUD:** nothing is drawn while the HUD is closed. For ten minutes after you close it, EndfieldHUD keeps its graphics device, so reopening stays quick (about 10–18 ms to the first frame); during those minutes it wakes about 71–75 times a second, mostly for the graphics driver. After ten minutes it releases the device: wake-ups drop to about 0.1 a second, and private memory by about 50 MiB (to about 124 MiB after launch, 176 MiB after use). The first opening after that takes about 170 ms, or about 45 ms if you point at the tray icon first.
- **Background readings:** the CPU, RAM, GPU, network and disk readings for Activity Monitor and the battery bar are taken every 5 seconds, also while the HUD is closed (every second while Activity Monitor or the charge bar shows), at about 1.3–1.4 ms each.
- **Memory** (private memory, Task Manager's *Commit size*, plus graphics memory): about 418 MiB plus 151 MiB after visiting every tool; about 307 MiB plus 47 MiB once closed again; about 263 MiB plus 40 MiB after ten more open and close cycles. While the HUD stays open after visiting every tool, the graphics driver keeps about 120 MiB of freed memory until the HUD closes.
- **Open and idle:** with ambient animation on, about 5.2–5.5 % of one CPU core, drawing at the Mac's 30 Hz ambient rate. Opening, closing and switching follow your display's refresh rate (150–158 fps at 165 Hz), which is smoother than 60 Hz but uses more CPU while they run.
- **Threads:** after an opening, about 23 Windows helper threads for software drawing of text and shapes appear and end after about 2 minutes idle. After an account request or an update check, Windows' web client (WinHTTP) keeps about 9–10 threads until EndfieldHUD quits.
- While the HUD is shown it runs at a raised priority, so it stays smooth while a game runs; the normal priority returns when it closes.

The only on-screen measurement so far, of an earlier test build, failed its closed-idle wake-up and open-idle CPU limits and measured 352.5 MiB and 58 threads closed. The changes since then have been measured off-screen only.

## Report a problem

Open an issue at [github.com/DDDuoDuo/EndfieldHUD/issues](https://github.com/DDDuoDuo/EndfieldHUD/issues). Include your Windows version, your PC and graphics card, whether you use Setup or the portable zip, and what you were doing.

- **EndfieldHUD stopped with an error message:** attach `last-fatal.txt` from `%LOCALAPPDATA%\EndfieldHUD\v1\Diagnostics` (paste that path into File Explorer's address bar). It records the time, the error, its code and the step EndfieldHUD was in. It contains no note text, file paths in it are replaced with `<path>`, and only the last error is kept.
- **The Event Log** in the HUD lists recent actions with their times; it does not store clipboard contents, note text or credentials. A screenshot of the entries around the problem helps.
- **One tool stopped working or disappeared** while the rest of the HUD keeps running: EndfieldHUD switched it off after an error. This writes no file, so describe what you were doing. Quitting and reopening EndfieldHUD starts the tool again.
- **A notice says resources are missing or damaged:** reinstall EndfieldHUD. Your data is not changed.
- **EndfieldHUD closed without any message:** Windows may have recorded it in **Event Viewer → Windows Logs → Application**, as an *Application Error* for `EndfieldHUD.exe`.
- **Slow, or using a lot of memory:** quit EndfieldHUD, then start it with an optional trace that records numbers only. `ENDFIELDHUD_MEMORY_TRACE=1` writes `memory-trace.json` (memory and threads, with tool and Windows component names) and `ENDFIELDHUD_STARTUP_TRACE=1` writes `startup-trace.json` (opening times), both in the same Diagnostics folder:

  ```powershell
  $env:ENDFIELDHUD_MEMORY_TRACE = "1"
  & "$env:LOCALAPPDATA\Programs\EndfieldHUD\EndfieldHUD.exe"
  ```

## Verify the download

The SHA-256 checksums of both downloads are listed in `EndfieldHUD-1.2.0-build18-windows-x64-SHA256SUMS.txt` and below. In PowerShell, in the folder with the downloads:

```powershell
Get-FileHash .\EndfieldHUD-1.2.0-build18-windows-x64-setup.exe -Algorithm SHA256
Get-Content .\EndfieldHUD-1.2.0-build18-windows-x64-SHA256SUMS.txt
```

The hash must equal the value on that file's line (upper or lower case does not matter). Check the portable zip the same way.

| File | SHA-256 |
| --- | --- |
| `EndfieldHUD-1.2.0-build18-windows-x64-setup.exe` | <!-- sha256 at release --> |
| `EndfieldHUD-1.2.0-build18-windows-x64-portable.zip` | <!-- sha256 at release --> |

---

Made by [DDDuoDuo](https://github.com/DDDuoDuo). An unofficial fan project. Game artwork and branding belong to HYPERGRYPH and their respective owners. Original code is MIT licensed; third-party assets and libraries keep their own terms, and their licences are in `THIRD-PARTY-NOTICES.txt` beside `EndfieldHUD.exe`. [Full credits and sources](https://github.com/DDDuoDuo/EndfieldHUD/blob/windows-v1.2.0/CREDITS.md)
