# EndfieldHUD for Windows — changelog

User-facing changes in the Windows release candidates, newest first. The
detailed verification record is [`windows/README.md`](README.md); the
requirements and the current status are in
[`WINDOWS-MIGRATION.md`](../WINDOWS-MIGRATION.md). Every release candidate
below was a private test build on the development laptop with its own test
data folder; none is a published release.

## Unreleased

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
