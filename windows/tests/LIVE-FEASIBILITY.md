# Live feasibility on the Windows laptop

These are the five architecture gates in [migration handoff §3](../../WINDOWS-MIGRATION.md). **All remain unverified.** Unit tests, COM capability creation, hidden-window graphics endpoints and synthetic GPU readbacks do not pass them. Record actual failures as failures; missing hardware, a reference or a measurement leaves the affected check unverified.

## Prepare and launch

Use the current x64 Release build with its sibling `Resources` folder, from the repository root:

```powershell
$exe = (Resolve-Path .\windows\build\x64\Release\EndfieldHUDWindows.exe).Path
& $exe --editor-fixture --language en
```

Activate through the tray **Open / hide** action or Ctrl + backtick. The tray reports layout-specific modifiers or a shortcut conflict. The executable starts hidden; activating this normal path, including `--editor-fixture`, takes one **memory-only desktop snapshot while the HUD is hidden**. Captured pixels are discarded on close. Use a desktop containing only synthetic test content. The diagnostic script below uses generated pixels and never captures the desktop.

Tab/Shift+Tab enter or leave the single projected editor; clicking it also focuses it. Escape ends composition/editing before another Escape hides the HUD. Hiding does not exit: use tray **Quit EndfieldHUD feasibility prototype**, confirm, and wait for closing. Quit before each new language run; another launch while the instance exists only toggles that instance.

Replace `en` with each supported argument:

| Language | Argument |
| --- | --- |
| English | `en` |
| Simplified Chinese | `zh-Hans` |
| Traditional Chinese | `zh-Hant` |
| Japanese | `ja` |
| Korean | `ko` |

Type synthetic text only; leave files, account linking, imports and the clipboard untouched. The fixture loads no saved profile, account, Mac data or existing app preferences, and edits are not saved. Module labels are presentation; their bodies are unfinished.

Required references: a **second physical monitor with different scaling**, and matched Mac output from the same canonical `Sources/` snapshot (`4036174a3facf935260f4d0a9c63bfff33b98c37`) using an isolated synthetic fixture. Match viewport, normalized pointer, animation time/phase, language, dark theme, FAD41F accent, Endministrator/level 60/empty UID and text. Record clock values when comparing typography. Documentation GIFs alone are not matched evidence; do not use the running Mac app's real data.

## 1. Source overlay, animation, tilt and hits

Open and close slowly, then interrupt opening/closing with the shortcut. Move the pointer through corners, the circle, irregular button edges, profile and editor; hold it stationary during transitions. Check top-to-bottom entrance, bottom-to-top exit, one current transform for hover/press/hits, continuous tilt, and editor movement with its plane. Center clicks should keep the HUD open; outside clicks hide it.

Scroll the right navigation to both limits with wheel and arrows. Compare row order, recycled icons/captions, overscroll/rebound and grey limit arrows. Compare matched Mac frames for the circle, source icons, card crop/hover, header, digital clock, shadows, masks and blend/fade timing. The footer should stay in its screen plane while the circle tilts. **Footer/wordmark overlap at the fixture size, Mac font metrics and profile edge raster remain open comparisons.** Retained source IDs alone are insufficient; clock alternatives, live status, full FX/HDR and settings are unfinished.

Pass requires the matched sequence and projected hits to agree. Note each mismatch with phase, pointer and viewport.

## 2. Frozen backdrop and saved HUD/cursor recording

Place a synthetic checkerboard/text window behind the HUD. Open, move that window while the HUD is visible, close, change the pattern, and reopen: the open backdrop stays frozen and the new opening refreshes it. Rapidly toggle at least 20 times, including closing during preparation, and reopen from the tray. Look for old HUD/menu shapes, recursive blur, black flashes or stale previous-display pixels.

In Windows Snipping Tool choose **Record → New**, select a region containing the HUD and pointer, then **Start**. Record opening, tilt, hover, editor I-beam/selection, closing, rapid reopen, focus loss and tray/native-menu return. **Stop, save and play the saved video**, checking HUD visibility, the original custom cursor, native I-beam precedence, restoration after hiding, and no missing/duplicate/alternating arrows. A correct live view or synthetic BMP does not establish recording success. [Microsoft's recording instructions](https://support.microsoft.com/en-us/windows/apps/use-snipping-tool-to-capture-screenshots) describe the recorder controls, not HUD/cursor acceptance.

Record recorder name/version and cursor settings. Unsupported capture or tint/vignette fallback is a limitation, not blur parity. The SDR Gaussian prototype still needs matched Mac blur/profile/HDR acceptance.

## 3. Mixed DPI, movement and hot-plug

Use two monitors at different Windows scaling values, such as 150% and 100%; record physical bounds, scaling/DPI and refresh rates. Include a left/above display with a negative origin when available. Hide, move the pointer to each monitor, then activate: the current prototype opens on the active display and covers full monitor bounds.

On each screen test irregular hits, scrolling, cursor, editor selection and IME candidates. While open, change scale/arrangement; unplug the selected external screen, reopen on the laptop, reconnect and repeat. Check connected-screen retention, valid-screen recovery, resized source viewport, no stretched old backdrop, and caret/candidates at the drawn plane. This is close/reopen relocation, not dragging an ordinary window. Fixed-display preference/settings UI is unfinished.

One monitor or equal DPI cannot pass this gate. Record observed recovery and each unavailable selection case.

## 4. Projected editing, IME, keyboard and fonts

Repeat all five language launches. In the tilted editor type English, `中文 繁體中文 日本語 한국어`, combining `é`, emoji `😀 👩‍💻`, punctuation and multiple lines. Use installed Chinese, Japanese and Korean IMEs to compose, choose candidates, commit and cancel while moving the pointer and after display changes. Missing IMEs leave those cases unverified.

Check caret/candidate alignment, drag selection outside the plane, double-click words, triple-click paragraphs across wrapped lines, arrows/Home/End, Backspace/Delete, undo/redo, scrolling and Tab/Shift+Tab. Escape must unwind composition/focus before closing. With Narrator, verify editor name/value, edit announcements and selection/caret changes. Provider notification counters do not prove delivery; whole-HUD traversal/accessibility remains unfinished.

Compare Mac/Windows glyphs, wrapping, fitting and baseline positions in **all five languages**, including long Japanese captions, Chinese file-shelf labels, profile fields and footer. Record installed Segoe UI/Emoji, YaHei UI, JhengHei UI, Yu Gothic UI, Meiryo UI and Malgun Gothic; identify actual fallback where measurable, otherwise mark it unverified. Source SDF atlases are not packaged and font-family availability is not metric parity.

## 5. Closed work, bounded reopening and visible pacing

Test the ordinary desktop separately, after quitting the editor fixture:

```powershell
& $exe --language en
# After quitting that run, compare the explicit finite-animation diagnostic:
& $exe --ambient-off --language en
```

The default keeps source ambient animation while visible; it is not expected to submit zero idle frames. Compare cold opening, warm opening, continuous tilt/scroll and interrupted transitions against matched Mac timings at the same refresh rate. Record latency and frame-time distribution/dropped frames with a presentation trace; Task Manager alone cannot establish smooth pacing.

Measure at least 60 seconds closed before first activation and after repeated visible reopen batches (for example 1,10,100). Record app CPU time, private bytes, working set and handles after settling. Use a GPU/wakeup trace to check that frame submission, backdrop capture, game/media work and polling stop while closed; necessary OS/tray notifications remain allowed. Persistent growth across drained batches, stalls or continuing closed loops fail the check. Record sleep/resume and recovery too.

For complementary **synthetic** ownership evidence, the current script supports:

```powershell
.\windows\tests\run-feasibility.ps1 -ReopenCycles 1000
```

It writes ignored local evidence under `windows/evidence/local`, uses generated backdrop pixels and hidden animation endpoints, and measures a closed interval. It does not measure default visible ambient cost, recording, live capture, pacing or matched Mac performance. A plateau or zero process CPU counter alone does not pass this gate.

Optional [per-process GPU sampling](measure-closed-gpu.ps1): after a **new** lifecycle JSON is written, while that probe remains alive in its 61-second closed interval, use a second PowerShell window with its actual PID assigned to `$probeProcessId`:

```powershell
.\windows\tests\measure-closed-gpu.ps1 -ProcessId $probeProcessId -Output .\windows\evidence\local\closed-gpu.json
```

This observes only the selected `--graphics-probe` process for 30 seconds. Missing/invalid counters remain unverified; samples do not establish wakeups, compositor attribution or full GPU acceptance.

## Record results

Keep results local under `windows/evidence/local`; do not package recordings. Record date, Git commit, executable SHA256, OS/build, CPU/GPU/driver/RAM, monitor bounds/DPI/Hz, launch flags, Mac reference commit/fixture, recorder/trace versions, observations and evidence filenames. Use **PASS** only when every required check and comparison for that gate was observed; **FAIL** for a reproduced defect; **UNVERIFIED** for missing evidence. Separate functional observations from unmatched parity.

| Gate | Status | Observations/failures | Evidence and remaining limits |
| --- | --- | --- | --- |
| 1 Overlay/animation/hits | UNVERIFIED | | |
| 2 Backdrop/saved recording | UNVERIFIED | | |
| 3 Mixed DPI/hot-plug | UNVERIFIED | | |
| 4 Editing/IME/fonts | UNVERIFIED | | |
| 5 Closed ownership/pacing | UNVERIFIED | | |
