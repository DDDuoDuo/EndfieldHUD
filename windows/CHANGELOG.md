# EndfieldHUD for Windows — changelog

User-facing changes in the Windows release candidates, newest first. The
detailed verification record is [`windows/README.md`](README.md); the
requirements and the current status are in
[`WINDOWS-MIGRATION.md`](../WINDOWS-MIGRATION.md). Every release candidate
below was a private test build on the development laptop with its own test
data folder; none is a published release.

## Unreleased (after RC 131)

- **Notes drawing** keeps every mouse sample and paints only newly touched
  ink, so strokes follow the cursor smoothly (`aa758e8`).
- In progress: Now Playing real-time progress, synchronized lyrics and seeking
  for NetEase Cloud Music; Activity Monitor recording GPU history at all times;
  Map drawn beneath the Storage and Activity Monitor buttons; a "Don't display"
  battery bar option and a GPU metric for the battery bar; lower closed-idle
  wake-ups and ambient-animation CPU.

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
