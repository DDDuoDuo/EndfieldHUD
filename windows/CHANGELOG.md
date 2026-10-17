# EndfieldHUD for Windows — changelog

User-facing changes in the Windows release candidates, newest first. The
detailed verification record is [`windows/README.md`](README.md); the
requirements and the current status are in
[`WINDOWS-MIGRATION.md`](../WINDOWS-MIGRATION.md). Every release candidate
below was a private test build on the development laptop with its own test
data folder; none is a published release.

## Unreleased

## RC 137 — checkpoint 137

- **The HUD that Setup opens comes to the front**: on RC 136, the HUD that
  Setup opened after an install or an update (*Open EndfieldHUD*) appeared
  behind the other windows (tester report). Setup now starts EndfieldHUD
  from the *Open EndfieldHUD* click itself, while Setup is still in front,
  and passes on Windows' permission to come to the front. While open, the
  HUD now stays above other windows, as on the Mac, and leaves that layer
  when it closes. Windows allows that layer only with the same permission,
  which the hotkey, *Open EndfieldHUD*, the Start menu and the tray icon
  give. Without it, the HUD opens just behind the window in front and
  moves above the other windows once you click a visible part of it or
  open it again with the hotkey. After an update that had to close
  EndfieldHUD (also one installed from About), Setup restarts it in the
  notification area only, as before and as on the Mac. Confirmed by the
  tester on the test laptop: after RC 136's setup had installed it and
  RC 137's setup had updated it, the HUD opened by Setup, the Start menu,
  the tray icon or the hotkey came to the front and took typing.
- **Settings work with screen readers**: System, Display, Hotkeys and
  About, their choice pages and the *Keep this UI scale?* card are now
  available to Narrator and other screen readers through Windows UI
  Automation, as on the Mac: buttons, switches, sliders, choice lists,
  links and headings, named in the HUD's language, with their current
  values and states and the page's status line. Before, no Settings control
  was listed. Switching to a page, or the card appearing or going, updates
  the list without a further click or key; a change that arrives on its own
  (an update check finishing in the background) reaches the screen reader
  at the next click or key. Not yet tried with Narrator.
- **Storage Settings and the WebView2 download page open in front**:
  *Storage Settings* in Storage and Account Binding's link to the Microsoft
  Edge WebView2 Runtime download now close the HUD first and open once it
  has closed, as on the Mac, so they never open behind the HUD. A page asked
  for while the HUD is already closed now opens at once; before, it waited
  until the HUD had been opened and closed again. File choosers, color
  pickers and the account sign-in window still open above the HUD.

## RC 136 — checkpoint 136

- **Now Playing with NetEase: lyrics show**: with NetEase's **开启 SMTC**
  on, RC 135 showed no lyrics. Now Playing uses NetEase's own play queue
  file however long the queue is (up to 16 MiB). Since RC 132 a queue file
  over 1 MiB was skipped, so every song fell back to an online title
  search, which found nothing for some songs: those had no lyrics and no
  running time. The tester's queue (1352 songs, about 3.2 MB) now gives
  every song tried its exact NetEase song and length. Lyrics that NetEase
  stores with timestamps like `[01:23:45]` (1 minute 23.45 seconds) now
  appear on time; before, they were read as hours, so only the credit lines
  showed. A lyrics lookup that gets no answer (no connection, a timeout or a
  refusal from NetEase) is no longer remembered as "no lyrics": it is tried
  again after 5, 15, 45 and 135 seconds, then every 5 minutes while Now
  Playing is shown, and LRCLIB's lyrics stay on screen meanwhile. Confirmed
  by the tester on the test laptop: with **开启 SMTC** on, the lyrics run in
  time with the song and the running time shows.
- **Now Playing: sharp cover**: covers are drawn at exactly the size the HUD
  shows them (660 pixels, scaled down smoothly), not enlarged from at most
  512 pixels, which looked blurry. With NetEase's **开启 SMTC** on, the small
  cover from NetEase's media session appears at once, then the full-size
  cover from NetEase's image server fades in once it has loaded, without a
  gap. Other players keep showing their own cover. Confirmed by the tester
  on the test laptop with **开启 SMTC** on.
- **Third-party licences: Unicode data**: `THIRD-PARTY-NOTICES.txt` now also
  includes the Unicode License V3 for the Unicode character tables built
  into `EndfieldHUD.exe`: those of the minigame's script engine (Unicode
  17.0) and those of Account Binding's text checks.

## RC 135 — checkpoint 135

- **Clicking beside the text places the caret like the Mac**: in Archive's
  title and body, Notes text cards and checklists, Calendar details and the
  Profile introduction, a click to the right of a line puts the caret at the
  end of that line, a click below the last line at the end of the text, and
  a click to the left of a line at its start. Before, such a click did
  nothing. Shift-clicks and drags past the end of a line stop at that line's
  end. On a line that wraps, the caret goes before the space where the line
  wraps, so it stays on that line (the Mac puts it after that space).
- **Up and Down keep the caret's column**: moving up or down through a
  shorter line stops at its end and comes back to the same column on the
  next longer line, as on the Mac. Before, the caret jumped to the start of
  the following line and drifted left with each move. Up on the first line
  now goes to the start of the text, and Down on the last line to its end.
- **Text cursor over Notes, Archive and Calendar fields**: the pointer shows
  the text cursor (I-beam) over the Notes field you are editing, the open
  Archive document's title, date and body, Archive's category name field
  and the Calendar event fields, exactly where a click places the caret, as
  on the Mac. Over buttons, menus and the space beside the fields it stays
  the HUD's own cursor.
- **Touchpad pinch zooms the Map, E-Reader and Media Assembly**: a pinch
  zooms at the pointer within the Mac's limits. Ctrl + mouse wheel now zooms
  by the Mac's mouse-wheel steps; in the E-Reader and Media Assembly it used
  to zoom about three times as fast. A pinch or Ctrl + wheel over the
  Storage and Activity Monitor buttons below the circle no longer zooms the
  Map. In Notes and Projection, which have no pinch on the Mac, Ctrl + wheel
  still scrolls a note or sizes the brush. Pinching on a real touchpad has
  not been specifically reported yet.
- **Section list arrows**: the arrows at the top and bottom of the
  right-hand section list scroll the list when clicked, and each is lit only
  while the list can scroll that way, as on the Mac. Before, clicks on them
  did nothing and the up arrow always looked lit.
- **App shortcuts: original icons for up to 128 shortcuts**: up to 128
  shortcuts with the App icon show their application's own icon (about
  4.5 MiB at most), the visible rows first. Before, only 20 did and the
  others showed the grid icon. The Mac has no limit.
- **Now Playing with NetEase: less background work**: while Now Playing is
  shown and NetEase plays, the once-a-second reading of NetEase's player bar
  makes about 7.5 instead of 25 accessibility calls, and no longer reads
  everything twice. EndfieldHUD's CPU while shown and playing fell from
  about 0.72 to 0.50 % of one core (hidden test). With the HUD closed, or on
  another module, Now Playing does not read NetEase at all (about 0.001 %).
- **Third-party licences included**: `THIRD-PARTY-NOTICES.txt` now ships
  next to `EndfieldHUD.exe`, in the installed folder and in the portable
  zip, with the full licences of QuickJS-NG, the WebView2 SDK, zlib, the
  Noto Sans SC and KR fonts, Matter.js, Unity uGUI and C++/WinRT, and the
  attributions for the map data and the account header's numerals.
  Uninstalling removes it with the program.
- **Windows 11 required**: Setup refuses to install on Windows before
  Windows 11 (build 22000) and says so in all five languages. EndfieldHUD
  started on an older Windows says that Windows 11 is required instead of a
  general display error.
- **Release files**: the checksum list (SHA256SUMS) names only the setup
  and the portable zip, the two files a release offers; the developer
  symbols get a checksum file of their own. The release notes are drafted
  in `windows/RELEASE-NOTES.md`.
- **Test launch priority**: the test builds up to RC 134 were started on the
  test laptop at Below Normal priority (RC 134 was raised to Normal by
  hand), so the open HUD's priority raise never took effect, which may
  explain lag felt while gaming; RC 135 starts at Normal priority, as an
  installed copy always does.

## RC 134 — checkpoint 134

- **Less memory**: once the graphics card holds a module's pictures, the HUD
  gives back its own copy, and it stores pictures at 4 bytes per pixel, or 1
  for plain masks such as the Map's soft edge. After visiting every module
  the HUD uses about 418 instead of 464 MiB of memory and about 151 instead
  of 183 MiB of graphics memory; closed, about 307 instead of 335 MiB and
  47 instead of 61 MiB (hidden test).
- **Switching back to a module stays quick**: a module you switch away from
  keeps a ready copy of its finished pictures, so switching back shows them
  without drawing them again: about 8–10 ms for Clipboard, 7 ms for
  Event Log and 3–4 ms for Media Assembly (hidden test). These copies are
  freed with the rest of the graphics after the HUD has been closed for ten
  minutes.
- **Archive: switching back is instant too**: Archive keeps its document,
  its fields and its pictures while you visit another module inside the
  open HUD. Switching back takes about 2–3 ms instead of about 45 ms, and
  switching away under 1 ms instead of about 50 ms (hidden test). Your text
  is saved when you switch, and the field you were typing in keeps its
  caret when you come back (the Mac ends editing on a switch). Archive lets
  go of all of it when the HUD closes.
- **Archive costs nothing until you open it**: Archive is built the first
  time it shows. Launching and opening the HUD on another module no longer
  build about 3.4 MB of Archive pictures (hidden test).
- **Volume: app icons**: the app rows in Volume show each app's own icon, as
  on the Mac. Icons are read on the HUD's shared background queue, without a
  thread of their own. A program file without an icon shows none, as on the
  Mac.
- **Background threads after an opening**: about 23 Windows helper threads
  appear after an opening. They belong to Windows' software drawing, which
  draws the HUD's text and shapes on several cores at once, and end after
  about 2 minutes without work. They stay, because drawing on one core is
  about 17 % slower.

## RC 133 — checkpoint 133

- **Map: the Storage and Activity Monitor buttons stay on top during
  switches**: switching into or out of the Map no longer draws the Map or the
  other module over the two buttons below the circle, so they no longer
  flicker, as on the Mac.
- **Map: lighter pin pulse**: the pulsing pins now animate at the HUD's
  30 Hz ambient rate instead of the monitor's refresh rate (165 Hz on the
  laptop), with the same curve. With 12 pins the Map's CPU fell from about
  40.6 to 18.7 ms per second (hidden test). Zooming, panning and the toolbar
  still move at the monitor's refresh rate. In low-power visual mode the
  pins no longer pulse, as on the Mac.
- **Closed HUD: much less background activity**: after the HUD has been
  closed for ten minutes it releases its graphics device. Background
  wake-ups then fall from about 178 to about 0.1 per second, and memory
  drops by about 50 MiB (hidden test). Reopening within ten minutes is as
  fast as before (about 10–18 ms). The first opening after a longer close
  takes about 170 ms, or about 45 ms if you point at the tray icon first.
  Notes, Archive and Projection videos and Media Assembly now let go of the
  graphics device when they are not in use, so the release can happen.
- **Now Playing with NetEase's 开启 SMTC on**: the time bar and the
  synchronized lyrics now run with the song. NetEase's own media session
  gives the title, artist, cover and controls but no time, so Now Playing
  takes the time from NetEase's player bar and the song length from its
  queue file, and uses the session for everything else. Other players
  (Spotify, browsers) are unchanged.
- **Correction to RC 132's tip — seeking in NetEase**: turning on
  **开启 SMTC** does not make seeking possible. NetEase's media session
  reports no position and ignores position changes, so Now Playing cannot
  move NetEase's progress bar with or without that setting.
- **Activity Monitor: cheaper background sampling**: each system sample
  reads only the network adapters it counts, and lists the adapters again
  only when Windows reports a change. A sample now takes about 1.4 ms
  instead of about 2.5 ms, with about 12 instead of about 40 context
  switches; wake-ups from sampling while the HUD is closed fell from about
  500 to about 190 per minute.

## RC 132 — checkpoint 132

- **Notes drawing follows the cursor**: strokes keep every mouse sample
  Windows delivers and paint only the newly touched ink, so fast strokes stay
  smooth. Drawing CPU per frame fell from about 2.1–2.5 ms to about 0.35 ms.
- **Battery bar: "Don't display"**: Display › Battery alert › Display method
  has a third choice, *Don't display* (不显示 / 不顯示 / 表示しない /
  표시 안 함). The floating charge bar then never appears on its own: not on
  power changes, not in *Always* mode, not after the HUD or Projection
  closes, not after wake. It creates no window or graphics device and starts
  no sampling for it. Still available: the tray's *Preview charging effect*,
  *Edit position*, the HUD's charge badge (the Power entry) and the Power
  page.
- **Battery bar: GPU reading**: Reading has a *GPU* choice after the five Mac
  readings. It shows Activity Monitor's GPU total load, laid out like the CPU
  reading. The Event Log names a change to it. A Mac or an older Windows
  build reads either new choice as *Power changes* / *Battery*.
- **Activity Monitor GPU history has no gaps**: GPU is recorded at all times,
  on the same schedule as CPU and RAM (every 5 s, every second while
  Activity or the charge bar shows), also while Activity is hidden or the HUD
  is closed. Measured cost: about 7 ms of CPU per minute and under 1 MB of
  memory; no extra wake-ups.
- **Now Playing with NetEase Cloud Music 3**: the time bar and the
  synchronized lyrics now run with the song. Now Playing reads NetEase's own
  local queue file (read only) for the exact song and its length, so the time
  no longer stops when the online catalog search finds nothing, the lyrics no
  longer drift when it finds a different recording, and the lyrics are those
  of the exact song.
- **Tip — seeking in NetEase**: Now Playing cannot move NetEase's progress
  bar from outside, because NetEase exposes no position control for it. Turn
  on NetEase's own **开启 SMTC** setting (NetEase 3.1.21 or later). NetEase
  then publishes a Windows media session, and Now Playing uses it for
  NetEase's exact position and for seeking, together with the other
  controls.
- **Map**: the Storage and Activity Monitor buttons below the circle stay
  above the Map and answer clicks again, as on the Mac. The transitions into
  and out of the Map are not cut out yet.
- **Open idle**: with ambient animation on, frames redraw only what changed:
  about 5.2–5.5 % of one core at the Mac's 30 Hz ambient rate (5.5–6.1 %
  before, hidden test). Closed-idle wake-ups (about 70 per second from the
  graphics driver) are unchanged for now: releasing the graphics device while
  the HUD is closed is built but not yet in effect, and when to release it is
  still being decided.

## RC 131 — checkpoint 131

- **Projection brush follows the cursor**: input to display fell from about
  196 ms to about 21 ms on a 165 Hz display; drawing CPU fell from about 80 %
  to 21–30 % of one core.
- **Display-rate animations**: HUD animations follow the monitor's refresh
  rate (about 150–158 fps at 165 Hz) instead of a fixed 60 Hz.
- **Now Playing with NetEase Cloud Music**: play/pause/previous/next press
  NetEase's own controls; the cover appears sooner; a timeline is shown.
- **Activity Monitor**: GPU comes before CPU and shows the GPU's total load.

## RC 130a — checkpoint 130

- Projection strokes are no longer cancelled at their first drag.
- Clicking the Sanity bar opens its dropdown instead of closing the HUD.
- Memory and threads: the closed HUD keeps far less memory (hidden trace
  543 → 335 MB private, 86 → 51 threads).

## RC 129i (interim)

- Notes: drawing works right after switching pen and eraser; the font and
  size menus scroll with a bar.
- Clipboard and Activity Monitor sort transitions no longer glitch; the Volume
  page switch no longer shakes.
- Activity Monitor shows GPU usage; Storage lets you choose the disk.
- The centre content fills the centre ring; the centre logo flickers when
  clicked; switching clock styles scrolls with its animation.
- The summon hotkey keeps working over games, and Settings > Hotkeys can turn
  hotkey opening off.
- The open HUD runs at a raised priority to stay smooth while games run.

## RC 128 — checkpoint 128

- Opening Projection no longer ends the app; no module can receive a clock
  time earlier than one it already saw.
- Clicking outside the HUD closes it with the closing animation.
- Temporary File Shelf: the removal glitch is fixed and the eye button shows a
  properties card instead of a content preview.
- Now Playing recognizes NetEase Cloud Music without a system media session.
- Volume shows at most three apps and scrolls smoothly.
- Faster first opening after launch (precompiled shaders and background
  warm-up).

## RC 127a

- Fixed a crash after editing in Media Assembly or a Work Mode timer change
  (Event Log timestamps out of order).

## RC 127

- First production `EndfieldHUD.exe` with every module wired in, per-user
  installer and portable zip.
