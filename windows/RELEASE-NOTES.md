<!--
Draft notes for Windows 1.3.0 (build 19), the Windows section of the shared
v1.3.0 release: the Windows files are extra assets of the regular release
tags, next to the Mac files, and this file is the only source of that
section. The endfieldhud_release staging build writes
EndfieldHUD-1.3.0-build19-windows-x64-release-notes.md, which is this file
with the two SHA-256 values under "Verify the download" filled in from the
staged setup and portable zip. Nothing is published from either file. This
file stays on the development branch: the main branch has no
windows/RELEASE-NOTES.md, and staging there writes no draft. This is a
draft: release candidate 139 has passed the automated tests only. Before
publishing:
  1. Stage a complete Release build: no "-incomplete" artifact names and no
     "Do not publish" mark in the staged draft. Check the installed size in
     the staged size report (release candidate 139: 85.91 MiB, 329 files;
     setup 42.40 MiB, portable zip 42.37 MiB).
  2. The owner decides whether the section 9 gate runs on the test laptop's
     desktop against this build (package_perf_gate --desktop). Until it
     does, "Performance" keeps the off-screen measurements of this version
     and must not claim an on-screen gate pass. (For Windows 1.2.0 the owner
     skipped the gate run and accepted the closed-HUD memory as it was.)
  3. Live acceptance of release candidate 139 on the test laptop, with the
     owner's go-ahead: install it over the published Windows 1.2.0 with the
     data kept, the tester's checklist (the Shelf drag-out, the redesigned
     centre, the shell, each module), the interactive Projection suites and
     the visual comparison with the Mac 1.3.0. Remove every item under "Not
     yet checked on a real PC" that has passed.
  4. Compare "Known differences and limitations" with WINDOWS-MIGRATION.md
     section 6 at the release commit.
  5. The Windows sources of this version reach main through their pull
     request first: the source links below point at the main branch
     (blob/main), whose exporter must be the one that exports the Mac
     1.3.0's Notes centre. Check that every link opens.
  6. Once the Mac 1.3.0 release (tag v1.3.0) exists, add the Windows files
     to it (no new tag):
       gh release upload v1.3.0 <setup.exe> <portable.zip> <SHA256SUMS.txt>
     Upload exactly these three files: the Windows updater finds the setup
     and SHA256SUMS by these names on the release's own tag, and Windows
     1.2.0 offers this update only once both are there. Never upload the
     symbols zip, its checksum file, the size report or an "-incomplete"
     artifact. Then append the staged draft (without this comment) to the
     v1.3.0 release notes as their Windows section. Keep the Mac notes, the
     release title, its Mac assets and its Latest state as the Mac release
     sets them, and never touch updates/appcast.xml.
  7. Then, on a PC with Windows 1.2.0 installed: About > Check for updates
     offers 1.3.0, downloads, verifies and installs it with the data kept
     (the first in-place update between two published Windows versions).
Remove this comment before publishing.
-->
# EndfieldHUD 1.3.0 for Windows

Windows 1.3.0 brings the macOS 1.3.0 redesign to Windows: every centre module rebuilt in the Endfield style, the Mac's fonts, a new clock, Recording mode, facility alerts, a daily boot log, and the module changes of macOS 1.3.0 (build 19). It is a native 64-bit Windows 11 program with the same look, animations, languages and data formats as the macOS app.

The Windows downloads are part of the 1.3.0 release (tag `v1.3.0`), next to the macOS download. They do not change the macOS app, its downloads or its update feed.

This release is not code-signed; Windows SmartScreen may show **Windows protected your PC** the first time it runs. See [Install](#install).

## What's new in 1.3.0

- **A redesigned centre.** Every module uses one shared set of headers, rows, toggles, menus, sliders, buttons, tabs, cards, confirmations and toasts. It follows your theme colour and has a Light variant. Confirmations blur and darken what is behind them, text fields have a blinking caret and a right-click menu, and scroll bars hide when idle.
- **The Mac's fonts.** HarmonyOS Sans for Latin letters and digits, and Noto Sans SC, TC, JP or KR for Chinese, Japanese and Korean, picked by language. English shows English only.
- **Shell.** Five redesigned clock styles with a status row for Work Mode and Recording mode; a sanity bar that never closes the HUD; reorder and hide the right-hand modules in **System → Modules**; quitting asks on the shared confirmation bar.
- **Recording mode** hides your UID, notes, clipboard and shelf details behind redaction bars, for screen recordings.
- **Facility alerts** (optional): low disk space and high memory use raise a banner and a tag that open Storage or Activity Monitor.
- **Daily boot log** (optional): a short summary of the day the first time the HUD opens each day.
- **Modules.** Notes gets TEXT and TODO tabs with colour, strikethrough and reordering; Archive gets classified documents with a password and editable categories; dragging a file out of the Shelf hides the HUD until the drop; the E-Reader reopens in your book with a much bigger page; Media Assembly gets the Wuling Impression filter and six stickers; the minigame gets a How to play panel; Calendar picks a month and year from its title; the Map fills the whole disc; Personal ID shows your best minigame score.

## Requirements

- **Windows 11, 64-bit (x64).** Live testing has been done on one laptop: Windows 11 version 25H2 (build 26200), Intel Core Ultra 7 251HX, NVIDIA GeForce RTX 5060 Laptop GPU, 2560 × 1600 at 165 Hz and 150 % scaling. Other PCs, graphics cards and Windows 11 versions have not received equivalent testing, and this version has not yet been tried on that laptop's desktop (see [Not yet checked on a real PC](#not-yet-checked-on-a-real-pc)).
- **Windows 10 is not supported.** The HUD's blurred background uses a window setting that Microsoft documents from Windows 11 onward, and EndfieldHUD has not been tested on Windows 10. Setup does not install before Windows 11 (build 22000), and a portable copy reports that Windows 11 is required. There is no ARM64 build.
- **Graphics:** a graphics card and driver with Direct3D 11 (feature level 11_0), including Direct3D 11 video support. The HUD has no software-drawing fallback.
- **Account Binding** needs the Microsoft Edge WebView2 Runtime. If it is missing, *Connect* explains this and links to Microsoft's download page; once it is installed, *Check Again* continues without a restart.
- **Videos** need Windows Media Foundation. On Windows N editions, install the Media Feature Pack for videos; images and GIFs work without it.
- About 86 MiB of disk space for the program, plus your data (the bundled fonts are about 48 MiB of it). The Visual C++ runtime is built in.

## Install

Both downloads contain the same program:

- **Setup (recommended):** `EndfieldHUD-1.3.0-build19-windows-x64-setup.exe`
- **Portable:** `EndfieldHUD-1.3.0-build19-windows-x64-portable.zip`

### Updating from Windows 1.2.0

In **About**, choose *Check for updates*, or run the new setup yourself. Your notes, settings and other data are kept, and nothing needs converting: Windows 1.2.0 can still read the data folder afterwards, except an Archive that holds a classified document, which Windows 1.2.0 does not open. Your *Summon HUD with hotkey* choice carries over to *Open with hotkey*. A new E-Reader library now starts with the Mac's default font, Georgia; libraries you already have keep their font.

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

Everything EndfieldHUD saves is in `%LOCALAPPDATA%\EndfieldHUD` (the current data in its `v1` folder). A linked account's sign-in and the password checks of classified Archive documents are stored there, encrypted for your Windows account.

Your notes and settings stay on your PC. EndfieldHUD goes online only for account linking, update checks on GitHub, and Now Playing's covers and lyrics, which are looked up only while Now Playing is shown.

## Getting started

Press **Ctrl + backtick (`)** to open the HUD. You can change the hotkey, or turn it off with *Open with hotkey*, in **Hotkeys**. EndfieldHUD lives in the notification area (system tray); click its icon for its menu. If Windows hides the tray icon, start EndfieldHUD again from the Start menu to open the HUD.

Use the side buttons to switch sections, and scroll the right side, or click the arrows at its ends, for more. **Esc** backs out of an edit before closing the HUD, and clicking outside the HUD closes it. Closing it keeps EndfieldHUD running; the red power button asks for confirmation and quits.

## What's included

Every tool of the macOS version:

| Tool | What you can do |
| --- | --- |
| Notes | Keep a TEXT note and a TODO list in the centre, with colour, strikethrough and bold, italic and underline; write, draw, make checklists and add images or videos on desktop notes, pinned across sections. |
| Temporary File Shelf | Drop files in, see their properties, and drag them back out (the HUD steps aside while you drag). Originals stay where they are. |
| Clipboard Cache | Scroll through recent copied text, links, images and files; pin entries or delete single ones. History clears when you quit. |
| Archive | Keep documents in your own categories, with rich text and media; lock classified documents with a password. |
| E-Reader | Read TXT, EPUB and PDF books, bookmark pages and zoom in; it reopens in the book you were reading. |
| Media Assembly | Edit pictures and videos with cropping, adjustments, filters and stickers. |
| Projection | Draw on a screen-sized canvas with a custom pen colour, with an optional dotted background. |
| Now Playing | See album art and available lyrics, and control playback. |
| Work Mode & Volume | Run a countdown or stopwatch and count your work hours. See audio devices and adjust per-app volume. |
| Calendar | Keep events with reminders. |
| Map | Explore the offline terrain map and place and customize pins. |
| Closure's Minigame | Play Merge! OrbiPom! |
| Event Log | Check the event log like a real Endministrator. |
| Personal ID | Customize your card. |
| Account Binding | Link a HYPERGRYPH game account (Mainland China or Global) for profile data and sanity countdowns (optional). |
| Battery, Storage & Activity Monitor | Check charging status, the disk of your choice, CPU, GPU, RAM and running apps. |
| App shortcuts | Add programs, shortcuts and Microsoft Store apps with custom names and icons. |

In **System**, **Display**, **Hotkeys** and **About**, change the theme and theme color, tilt, clock styles, center logo, icons, display and animation settings, the order of the modules, Recording mode, facility alerts and the daily boot log. The HUD supports English, Simplified Chinese, Traditional Chinese, Japanese and Korean.

**Windows-only:** *Import macOS data…* in **System** (see below). The GPU readings, the disk choice in Storage, the battery alert's *Don't display*, the three Volume app rows and the hotkey switch, Windows additions in 1.2.0, are now part of the macOS app too.

## Moving from a Mac

The Mac app has no export button; a small script exports its data once.

1. On the Mac, quit EndfieldHUD, download [`mac_import_exporter.py`](https://github.com/DDDuoDuo/EndfieldHUD/blob/main/windows/tools/mac_import_exporter.py) and run it with Python 3:
   `python3 mac_import_exporter.py ~/Desktop/EndfieldHUD-export`
   It copies your EndfieldHUD data and settings into that new folder. It changes nothing on the Mac and never reads the Keychain.
2. Copy the `EndfieldHUD-export` folder to the PC.
3. In EndfieldHUD, open **System → Import macOS data…** and choose the folder. EndfieldHUD checks every file, then lists each kind of data for you to review.
4. Confirm. EndfieldHUD restarts once, opens on **System** and shows the result and the files to relink. Your Windows data is backed up first, as a `v1.backup-…` folder in `%LOCALAPPDATA%\EndfieldHUD`; the import never deletes it.

**Imported:** notes with the centre TEXT note and TODO list and their images, the Temporary File Shelf list, your profile with its avatar and background, Archive (classified documents included), the E-Reader library, Calendar events, Map pins, app shortcuts, the Event Log, account preferences, the custom center logo, settings (module order, Recording mode, facility alerts, the boot log and the Projection pen included) and the minigame best score. Display and hotkey settings you already chose on this PC are kept, and the higher best score wins.

**Not imported, or needs a step:**

- Linked accounts must sign in again: credentials never leave the macOS Keychain.
- Classified Archive documents stay classified, but their passwords stay in the macOS Keychain: on Windows they show as missing their password until you set a new one or declassify the document.
- Files the Mac only referred to (Shelf items, books, app shortcuts, note and Archive media kept outside EndfieldHUD) must be relinked; the import lists them. Relink each from its tool, for example **Library → Relink…** in E-Reader, or a double-click on a Shelf item.
- A custom summon shortcut must be set again in **Hotkeys**; the default Ctrl + backtick carries over.
- Calendar reminders are scheduled again on Windows (with an installed copy).
- Clipboard history, Media Assembly sessions, minigame boards and a running Work Mode timer are not saved on the Mac either, so there is nothing to import. Your lifetime work hours do carry over.
- Mac update settings are not imported, and the daily boot log shows again on the first opening of the day.
- Data made by a newer version, or that cannot be read, is skipped only if you choose *Skip and Import*. Importing again asks first (*Import Again*) and makes a new backup.

The import has been tested with sample exports, not yet with a real Mac's data.

## Updates

- In **About**, choose *Check for updates* (the tray's *Check for Updates…* does the same). With *Check for updates daily* on (the default), EndfieldHUD checks at most once a day, when the HUD opens.
- When a new version is listed, click the update row once to download and verify it, and again to install it. EndfieldHUD closes, saves your data, updates in place and reopens.
- EndfieldHUD looks at the regular EndfieldHUD releases on GitHub and offers only those that include the Windows downloads; releases with only macOS downloads are skipped.
- Updates are never installed automatically: the macOS app's *Install updates automatically* is not offered on Windows, and there is no "update available" notification; the tray menu and About show when one is available.
- Because the builds are unsigned, a download is checked against SHA256SUMS from the same release. This confirms the file is intact, but it is not an independent signature.
- Only a copy installed with Setup updates itself. You can also run a newer setup yourself.

## Known differences and limitations

### Differences from the Mac

These come from Windows itself, or from choices that keep your data and system safe.

- **Facility alerts:** there is no overheating alert, because Windows gives apps no temperature state. Low disk space uses the figure Storage shows; high memory use warns at 90 % and turns critical at 95 % or when Windows reports low memory.
- **Temporary File Shelf:** files cannot be dropped on the tray icon (Windows offers no drop target there). Drop them on the open HUD's Shelf or its button, use *Add files*, or press Ctrl+V. Items that are not real files (Outlook attachments, images dragged from a browser, files inside zip folders) are refused with a message. Items moved to the Recycle Bin, to another drive or to some network shares show as *Unavailable*. Folders on cloud storage or network drives show no item count. When you drag an item out, releasing it within about 0.4 seconds onto a window under the fading HUD may be refused; the HUD then comes back. Releasing on another monitor, or a moment later, works.
- **Archive:** a classified document's password check is kept for your Windows account on this PC. Password recovery asks for Windows Hello or your Windows password; on a Windows account without a password, leave the password empty and confirm.
- **Now Playing:**
  - Players that publish to the Windows media controls (for example Spotify and browsers) work through them. NetEase Cloud Music, QQ Music, KuGou Music, Kuwo Music, foobar2000, AIMP, PotPlayer and VLC are also found by their window and read through their own on-screen controls; only NetEase, with its *开启 SMTC* setting on, has been checked on a real PC. Other players show *No music playing*.
  - Players found by their window cannot be seeked from Now Playing (these programs offer other apps no way to seek), and of them only NetEase shows album art. A seek or pause made in one of these players shows once the song changes, its sound starts or stops, you use the HUD's controls or you open Now Playing again; NetEase's time also follows its player bar while a song plays.
  - NetEase Cloud Music 3 cannot be seeked from Now Playing, with or without its *开启 SMTC* setting. Its time comes from NetEase's player bar, and the song's length from NetEase's own local play queue (read only) or, without a confident match there, from the NetEase catalog; without either there is no time. While NetEase is minimized its time may stop.
  - To read NetEase's player bar, EndfieldHUD turns on NetEase's built-in accessibility support once; it stays on until NetEase exits.
  - A player's own volume cannot be set on Windows: the volume control changes the player's level in the Windows volume mixer. Browsers and players with several audio processes cannot be adjusted here; use Volume.
  - Lyrics come from the song file's embedded lyrics, then the NetEase catalog (NetEase only), then LRCLIB.
  - A player that starts hidden in the tray appears the next time you open the HUD or a player changes.
- **Volume:** switch the default output or input device in Windows Settings; Windows offers apps no supported way to change it, so the device choosers stay disabled. Per-app volume acts on each app's sound on the default device; sending an app to another device is not available. Exclusive-mode audio is not detected. Windows remembers each app's mixer level, so EndfieldHUD restores the levels it changed when it quits, the PC sleeps or the device changes.
- **Media Assembly:** videos export as MP4 (H.264/AAC), named `<name>-edited.mp4`, instead of QuickTime MOV.
- **Image and video formats:** in Media Assembly, Notes, Archive and Projection, HEIC/HEIF, HEVC, ProRes, JPEG 2000, WebP and some older formats need optional codecs from the Microsoft Store or are not supported by Windows; without them they are reported as unsupported.
- **Notes and Archive:** Home and End move to the start or end of the line, and Ctrl+Home and Ctrl+End to the start or end of the text, as in other Windows text fields. On a line that wraps, a click to its right, End, and Up or Down onto it put the caret before the space where the line wraps, so typing there goes before that space; the Mac puts the caret after it. The same applies to the Calendar and Personal ID text fields. Picture data dragged without a file cannot be dropped on Notes: copy it and press Ctrl+V. Pasted Mac fonts Times and Times New Roman both become Times New Roman.
- **E-Reader:** Ctrl + mouse wheel zooms a page (the Mac's Option or Command + wheel); Alt + wheel does not.
- **Projection:** a brush size changed with the mouse wheel is saved at your next click or when the HUD closes.
- **Calendar:** reminders are Windows notifications and need a copy installed with Setup. Windows has no permission prompt for them: they follow your notification settings, and Focus assist (Do not disturb) can hold the banner back; it still reaches the notification center.
- **Work Mode:** the timer does not switch Windows Focus or Do not disturb, which Windows reserves for approved apps. The timer, completion and lifetime hours work as on the Mac.
- **Activity Monitor and Storage:** per-app disk and network use are not shown (Windows needs administrator rights for them). App memory is the private working set, as in Task Manager's Memory column. Apps without a window are not listed, compressed memory is not shown, and CPU shows unavailable on PCs with more than 64 logical processors. The GPU figure is the total load, so it can read higher than Task Manager's GPU column; it shows "—" where the graphics driver offers no GPU counters. A disk renamed while EndfieldHUD runs shows its new name at the next reading (within a minute) or after *Refresh*.
- **Battery:** the charge alert cannot appear over games in exclusive full screen; the reading, badge, tray and Event Log still update. Windows reports no time until full, charger power or battery health, so these show "—" or *Unavailable*. A battery that reports only a relative capacity shows it as `p / 100 %`.
- **Account Binding:** sign-in uses the Microsoft Edge WebView2 Runtime (see [Requirements](#requirements)).
- **App shortcuts:** Windows has no "reopen" signal for a running app. Opening a regular program that is already running brings its window forward; Store apps and other targets start normally.
- **Hotkeys:** the summon hotkey may not reach EndfieldHUD while an app running as administrator is in front, or in games whose anti-cheat captures the keyboard. Use the tray icon then.
- **Settings and Personal ID:** *App / menu bar icon* changes the tray icon; File Explorer, Start and notifications keep the program's own icon. The custom colour choices (*Theme color*, and the ID card's colour) open the standard Windows colour dialog: colours apply live, *OK* keeps the new colour and *Cancel* restores the starting one. Closing the HUD while the ID card's dialog is open also restores its starting colour; the Mac keeps the last live colour.
- **Clipboard Cache:** while another app holds the clipboard open, Clipboard shows *Clipboard unavailable* until the next copy.
- **Look:** the blurred background uses Windows' own blur. Text uses the same fonts as the Mac, laid out with Windows' text engine: lines in rich text are spaced slightly differently and the text caret can sit about 1.5 pixels off the Mac's position. The font licence does not allow adjusting the fonts.
- **Screen readers:** every tool's controls are available to Windows UI Automation. Text fields expose their whole text as one value rather than a text pattern, and the HUD's controls are used with the HUD's own keys rather than as Tab stops.
- **Minigame:** the rules and scoring match the Mac. Floating-point math differs slightly between Windows and macOS, so long-running physics can take a different (still repeatable) path.
- **Text and dates:** EndfieldHUD uses Windows' Unicode and time-zone data. On every Windows version, a few historical dates can fall under a different daylight-saving rule than on the Mac, because Windows' time-zone data applies each zone's earliest recorded rule to older dates. On Windows 11 versions before 24H2, some Indic-script text segmentation and Samoa's time-zone rules since 2021 can also differ from the Mac.
- Smaller differences remain in Event Log audio-device events, clipboard link parsing, some color-profile conversions and the profile photo's resampling.

### Limitations in this release

- Builds are not code-signed yet (see [Windows SmartScreen](#windows-smartscreen)).
- **E-Reader and Map:** touchpad scrolling moves in mouse-wheel steps.
- **Media Assembly:** the Highlights/Shadows slider has no effect yet (saved amounts are kept). 16-bit images are edited with 8-bit precision. Movies tagged with a color space other than BT.709 keep their decoded colors.
- **Projection:** lifting the pen after a very long stroke takes about 13–15 ms to redraw it.

### Not yet checked on a real PC

Automated tests run every tool over sample data, but these have not been tried live yet:

- this version on the desktop: the redesigned centre and its fonts in all five languages, the Shelf drag-out, facility alerts, Recording mode, the boot log, classified Archive documents and their Windows Hello recovery, and a tool-by-tool check;
- updating from Windows 1.2.0 to this version (the first update between two published Windows versions);
- signing in with a Global account (a Mainland China account worked);
- Now Playing with NetEase Cloud Music's *开启 SMTC* setting off, and with any other player;
- Calendar reminder notifications, and clicking one;
- *Launch at login* starting EndfieldHUD when you sign in;
- the Mac data import with a real Mac's data;
- Narrator and other screen readers, and Japanese and Korean input methods (Chinese input works);
- pinch-to-zoom on a real touchpad (Map, E-Reader and Media Assembly);
- screen recording with the HUD's cursor;
- sleep and resume, a graphics driver reset, plugging monitors with different scaling in or out, and a monitor on another graphics card;
- the summon hotkey in the games and apps where earlier test builds missed it, and the HUD's smoothness while a game runs;
- exclusive-mode audio, and per-app volume with real apps;
- Media Assembly video playback with color edits, and video formats beyond those on the test laptop;
- Volume's app icons in the installed app.

## Performance

These figures come from automated runs on the test laptop with sample data, with the HUD drawing off-screen; Windows 1.2.0 was measured beside this version in the same runs. The on-screen performance check has not been run for this version, and other PCs will differ.

- **Switching modules:** switching back to a module inside the open HUD takes about 2–12 ms, up to about twice as long as Windows 1.2.0 because the sharper text has more pieces to restore, and still well within one frame at 60 Hz. The first time a module is shown in a session it takes about 50–210 ms (about 35–120 ms in 1.2.0), mostly drawing its text. Switching away takes a few milliseconds longer than before, because the page you leave stays visible while it slides out, as on the Mac.
- **Idle:** the open HUD uses about the same processor time as Windows 1.2.0 (about 1.3–1.8 ms per frame with the ambient animation on, nothing with it off), and the closed HUD still draws nothing.
- **Map:** the terrain images are freed when the HUD closes, as on the Mac (about 12.6 MiB while closed, now none). The next opening shows its first frame in about 16 ms and decodes the terrain in the background, in about a third of a second, while the opening animation plays.
- **Text and memory:** text is drawn at the Mac's 1.35× sharpness. With every module visited, the open HUD uses about 630 MiB instead of about 420 MiB, mostly for that sharper text; most of it is given back when the HUD closes. The bundled fonts add no measurable private memory.
- **Opening and closing:** on the test laptop (2560 × 1600 at 150 %, 165 Hz), an opening on the Map misses about 23 of its refreshes against about 18 in Windows 1.2.0, mostly frames of 9–15 ms while the Map and the sharper text are drawn; the slowest frames are about as long as in 1.2.0. Turning Recording mode on or off takes about 30–65 ms with every module visited.
- **Clicks:** a change in place draws only what changed, and saves run in the background. On the test laptop (drawing off-screen in software, so slower than the desktop) Work Mode's start and pause take about 15–27 ms, selecting in the File Shelf about 36–100 ms, selecting a Calendar day about 25 ms, and opening a File Shelf item about 2 ms. A Personal ID edit still redraws the card (about 130–170 ms there), and the first switch to each module after an opening draws its page.
- **Confirmations:** the blur behind a confirmation is drawn only while the bar fades in or out, about 5 ms per frame with the software renderer.
- **Closed HUD:** as in Windows 1.2.0, nothing is drawn while the HUD is closed, and the graphics device is released after ten minutes closed.

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

The SHA-256 checksums of both downloads are listed in `EndfieldHUD-1.3.0-build19-windows-x64-SHA256SUMS.txt` and below. In PowerShell, in the folder with the downloads:

```powershell
Get-FileHash .\EndfieldHUD-1.3.0-build19-windows-x64-setup.exe -Algorithm SHA256
Get-Content .\EndfieldHUD-1.3.0-build19-windows-x64-SHA256SUMS.txt
```

The hash must equal the value on that file's line (upper or lower case does not matter). Check the portable zip the same way.

| File | SHA-256 |
| --- | --- |
| `EndfieldHUD-1.3.0-build19-windows-x64-setup.exe` | <!-- sha256 at release --> |
| `EndfieldHUD-1.3.0-build19-windows-x64-portable.zip` | <!-- sha256 at release --> |

---

Made by [DDDuoDuo](https://github.com/DDDuoDuo). An unofficial fan project. Game artwork and branding belong to HYPERGRYPH and their respective owners. Original code is MIT licensed; third-party assets and libraries keep their own terms, and their licences are in `THIRD-PARTY-NOTICES.txt` beside `EndfieldHUD.exe`. [Full credits and sources](https://github.com/DDDuoDuo/EndfieldHUD/blob/main/CREDITS.md)
