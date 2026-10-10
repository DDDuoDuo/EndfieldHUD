# EndfieldHUD — Windows migration handoff

This is the working handoff for DDDuoDuo's Windows laptop. It describes a native Windows port of the current macOS app. **The previous Windows preview was rejected and replaced. The fresh native build (`EndfieldHUD.exe` with a per-user installer) now contains every module, but it has not been accepted live on the laptop, measured against section 9, signed or released. No stable Windows release exists.**

The fresh implementation uses macOS `main` at `ca04f142185c7de40acd8523bdb563195d90a1d1` (v1.2.0 build 18). [`windows/source-authority.json`](windows/source-authority.json) pins its source and resource trees. Do not restore the rejected Windows renderer or use the raw game scene as the application reference. The old preview is preserved in Git history and a local backup, outside the new build. See [`windows/README.md`](windows/README.md) for current build scope.

Keep the current HUD, functions, animations, saved data and optimizations. Replace the platform plumbing. Do not redesign the interface or silently remove a module because its Windows adapter is difficult.

Current checkpoint (123, 2026-10-10): `EndfieldHUD.exe` runs every module in section 6 through one application owner. There are 15 built-in owners and 8 module areas: Media Assembly, Now Playing, Personal Profile, Account Linking, the Power charge badge and alert, App shortcuts, the Mac import card and Calendar reminders. The host adds a UI Automation root, HUD file drops, the restart handoff for the Mac import, GPU device-loss recovery and display recovery. On the laptop 341 of 344 suites pass. The three failures are environmental (a non-interactive SSH session, and no Mac `Sources/` in the test slot) and fail the same way on the unchanged parent. Hidden owner coverage exercises every module and area over synthetic data in temporary roots. In earlier isolated previews the user accepted the backdrop, navigation, tray, hotkey, language switching, plain-text Notes with Chinese input, the Clipboard UI, Reader scrolling, the Work Mode timer and the minigame. No visible run of `EndfieldHUD.exe` has been recorded, and no section 9 measurement of it exists. See [the build record](windows/README.md) (checkpoint 123), the unresolved items in section 11 and the platform gaps in section 6.

## 1. Start here on the Windows laptop

```powershell
git clone --branch codex/windows-migration https://github.com/DDDuoDuo/EndfieldHUD.git
cd EndfieldHUD
git fetch origin --tags
```

Read this file, [README.md](README.md), the relevant [module descriptions](docs/), and their current `Sources/` implementations before editing. Work incrementally on `codex/windows-migration`; leave `main` and the released Mac app unchanged. Add the Windows target under `windows/`. Keep Mac build scripts and Swift sources as the reference implementation.

Baseline as of **2026-10-07**:

| Item | Reference |
| --- | --- |
| Source baseline | `ca04f142185c7de40acd8523bdb563195d90a1d1` — macOS main, including the revised READMEs and Korean translation |
| Released Mac version | v1.2.0, build 18 |
| Original game interface branch | `archive/codex/endfield-watch-motion` — preserved tag, not an active branch |
| Current app source | `Sources/`, `Resources/`, `scripts/` on this branch |
| Visual references | [opening/closing](docs/media/readme/opening.gif), [module walkthrough](docs/media/readme/modules.gif), [minigame](docs/media/readme/minigame.gif) |
| Existing languages | English, Simplified Chinese, Traditional Chinese, Japanese, Korean |

The GIFs show behavior but are large documentation assets. **Do not bundle `docs/media/` into the Windows app.** Old development records, reference captures, tests and workflows were intentionally kept local and are absent from a fresh clone. This handoff must work without them. Do not recreate or upload private recordings, account diagnostics or real user data.

Authorship remains DDDuoDuo. Use the repository's configured identity; do not add a Codex coauthor. Preserve [LICENSE](LICENSE) and [CREDITS.md](CREDITS.md), including game-asset and dependency attribution. The MIT license covers the original app code/assets; game resources and third-party materials retain their separate ownership and terms.

## 2. Requirements that must survive the port

The following original requirements remain authoritative. Preserve their wording rather than narrowing them in an English implementation plan:

> Maintain the existing Endfield visual style, animations, data compatibility and performance optimizations. Keep CPU, RAM and storage use lightweight. Do not add heavy background polling or duplicate services. Use isolated tests without affecting my real data or running application.

> When using icons, first find corresponding original-game resources from branch codex/endfield-watch-motion.

> Animations should remain the exact same. The goal is to have same performance as old version.

> The sanity recovers 1 per 6 minutes in Arknights, and 7min 12s in Endfield.

> When binding game account, the # number should sync as well. The UID should sync as well and, UID, ID, # number, awakening date cannot be changed once toggled sync personal profile. At bottom left id card, Don't display # number. The UID at bottom left id card should also sync. This update shouldn't affect any performance.

> Mark everything as DDDuoDuo's actions.

For Windows, platform names and modifier labels may need equivalents, but module content and behavior stay the same. A missing OS capability must be reported explicitly, with its reason and fallback, rather than presented as completed parity.

### Visual and interaction contract

- Preserve the source Watch geometry, irregular button frames, icons, shadows, theme rules, typography, translucent backdrop, depth planes and layout. Do not replace them with a generic Windows dashboard.
- Keep `ENDFIELDHUD`, `SYSTEM INTERFACE`, the clock/date, Work Mode status, close hints, bottom logo and personal card. Translate only through the existing localization keys. RAM remains `RAM` in every language; Chinese charge mode is `超充模式`.
- Tilt follows the pointer during opening, closing, editing, confirmations and submenus. One frame's transform must govern both drawing and hit testing. Opening flicker reveals top to bottom; closing disappears bottom to top. Notes must follow the closing plane.
- Keep smooth module transitions, pointer hover/press timing, right-side overscroll and rebound, grey limit arrows, horizontal clock scrolling inside its rounded box, and the bottom-logo flicker.
- All secondary menus use the personal-card menu style and follow the HUD tilt. Menus sit beneath their trigger, appear above other content, animate smoothly and consume input before controls behind them. Text editors must not duplicate text at the screen's top-left or flatten the scene.
- Center clicks do not automatically dismiss the HUD. Escape dismisses editing/menu state before closing. The red power button asks for confirmation, plays the closing animation, then quits.
- Preserve individual profile-theme colors, original avatar quality/crop, matching avatar frames, rounded card, background clipping/fade, and logos' natural aspect ratio at a common height.
- Language changes update the entire open interface immediately. Check long Japanese labels, Chinese file-shelf labels, Korean and mixed-script user text.

Source authorities: [HUDModule.swift](Sources/HUDModule.swift), [HUDNavigationTarget.swift](Sources/HUDNavigationTarget.swift), [HUDNavigation.swift](Sources/HUDNavigation.swift), [HUDSourceWatchView.swift](Sources/HUDSourceWatchView.swift), [HUDSourceWatchAnimation.swift](Sources/HUDSourceWatchAnimation.swift), [HUDSourceWatchButtonAnimation.swift](Sources/HUDSourceWatchButtonAnimation.swift), [HUDSubsectionTransition.swift](Sources/HUDSubsectionTransition.swift).

### Navigation order

The left side stays System, Display, Hotkeys, About. Storage and Activity Monitor stay below the circle. The profile and power controls retain their dedicated bottom-left positions.

The right side is row-major:

| Row | Left | Right |
| --- | --- | --- |
| 1 | Notes | Temporary File Shelf |
| 2 | Clipboard | Archive |
| 3 | Media Assembly | Minigame |
| 4 | Now Playing | Volume |
| 5 | Projection | Reader |
| 6 | Work Mode | Calendar |
| 7 | Map | Event Log |
| 8 | Personal Profile | Account Linking |
| 9 onward | Power, then saved app shortcuts in their existing order | Add App is always last |

Follow the current source mapping for original-game icons: Projection uses 塔晶集换, Event Log uses 问卷, and Personal Profile uses 好友. Do not restore obsolete icon options from an old branch.

## 3. Native Windows architecture — prove it first

The Mac app is Swift/AppKit/Core Animation/Metal with macOS system services. It is not a project that can be recompiled for Windows with a build flag. The practical reuse is the scene/assets, animation semantics, game scripts, storage contracts and module logic.

**Recommended prototype:** C++20, Win32, C++/WinRT, D3D11/DXGI, Direct2D/DirectWrite, SQLite, and a Windows compositor. Evaluate Windows.UI.Composition interop against DirectComposition before choosing the final compositor. Microsoft describes [Win32 composition interop](https://learn.microsoft.com/en-us/windows/uwp/composition/using-the-visual-layer-with-win32), the [graphics API roles](https://learn.microsoft.com/en-us/windows/win32/learnwin32/overview-of-the-windows-graphics-architecture), and [DirectComposition's architecture](https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components).

This is an engineering recommendation, not a measured claim that C++ alone fixes performance. Start with Windows 11 x64 on the actual laptop; record the OS build, CPU, GPU, RAM, monitor DPI and refresh rate. ARM64 and older Windows support require their own builds and capability tests. Do not advertise support before those tests.

Keep the HUD renderer native. Use WebView2 only for short-lived official account authentication. Avoid shipping a second always-running web renderer or a browser solely for the minigame. Pin dependency versions, sizes and licenses; keep the first prototype small.

Suggested boundaries:

```text
windows/
  CMakeLists.txt
  app/             entry point, HWND/tray, activation, focus, shortcuts
  scene/           source-document loader, animation, camera, hit testing
  render/          D3D/HLSL, composition, text, texture/cache ownership
  modules/         the existing HUD functions and their presentation
  platform/        battery, audio, media, notifications, accounts, storage
  persistence/     codecs, schema versions, atomic writes, offline import
  resources/       staging manifest; reuse approved Resources inputs
  tests/           isolated synthetic fixtures and parity checks
  packaging/       Windows-specific build, signing, install/update pipeline
```

Define platform interfaces outside drawing code. Use a single app state, module selection state and lifecycle owner. Do not create independent copies of profile/account/settings state for each panel.

### First feasibility gate

Before porting every module, demonstrate on Windows:

1. A transparent overlay with the current source circle/buttons, one exact opening/closing sequence, live tilt and correct irregular-shape hits.
2. The current desktop shell's live blurred backdrop without capturing the HUD into itself, and correct visible recording of the HUD/custom cursor.
3. Mixed-DPI monitor selection and pointer coordinates, including moving/unplugging a screen.
4. A tilted text editor with Chinese/Japanese/Korean IME, caret/selection and keyboard access; font inventory/fallback and CJK/emoji layout comparisons against the Mac baseline.
5. No renderer/game/media loop while closed, bounded RAM after repeated reopen, and smooth frame pacing while animating.

If these fail, resolve the architecture issue first. Do not build all modules on top of a known flat-input or high-CPU implementation.

## 4. Rendering, assets and lifecycle

Read [HUDSourceWatchDocument.swift](Sources/HUDSourceWatchDocument.swift), [HUDSourceWatchFrameBuilder.swift](Sources/HUDSourceWatchFrameBuilder.swift), [HUDSourceWatchCamera.swift](Sources/HUDSourceWatchCamera.swift), [HUDSourceMetalRenderer.swift](Sources/HUDSourceMetalRenderer.swift), and [HUDSourceUIComposite.swift](Sources/HUDSourceUIComposite.swift).

- Port the actual shader/material behavior to HLSL: vertex transforms, meshes/UVs, clipping/stencil, blend factors, masks, mip levels, color-space handling and glow/shadow policy. Shader files are not interchangeable merely because both render APIs support a similar effect.
- Reuse the source scene's design space and animation data. Preserve signed asset IDs and precise floating-point values. Do not hard-code replacement curves based only on GIFs.
- Use one visible-frame clock for animation, pointer tilt, physics and lyric presentation. Coalesce high-rate pointer events to the next frame; moving the mouse must not rebuild every module, reread files or decode images.
- Cache static geometry, decoded assets, text layouts and module surfaces by revision. Refresh only changed content. Do not rasterize the whole HUD to a new CPU bitmap each frame.
- Match typography and layout against the current source. Labels use Mac system-font measurements, packaged resources exclude source SDF font atlases, and rich text may contain Mac PostScript font names. Copying assets does not supply equivalent Windows fonts. Select licensed fallbacks, compare metrics/glyphs/wrapping in all five languages, and record remaining differences rather than promising identical pixels without tests.
- Preserve lazy loading and the Mac fallback's separation from the main renderer. Avoid loading a complete parallel fallback UI during normal startup.
- Bound GPU buffers, decoded textures, artwork, PDF pages and map tiles. Store ownership explicitly; cancel tasks and discard stale generation completions on switch/close.

Use the existing staged runtime inventory rather than bundling every extraction artifact. [package-watch-resources.py](scripts/package-watch-resources.py) selects the desktop's transitive resources and applies **lossless** compression. Its `EHUDZ01\0` payload has an eight-byte magic, little-endian 64-bit uncompressed length, and raw DEFLATE data. Honor its 128 MiB per-resource guard and validate complete decompression. Do not resample source textures or flatten animated material data to save space. Reuse `Resources/Watch`, `WatchSource`, `WorldMap`, `MediaAssembly`, `OrbiPom` and approved icon sources according to the actual dependency inventory.

### Backdrop and cursor

The authoritative build-18 desktop shell uses `HUDBackgroundBlurView` / `NSVisualEffectView` with `.hudWindow`, `.behindWindow` and `.active`. Its opacity follows `blurAmount`, and low-power mode hides that view. `HUDSourceWatchView.canCaptureDesktopBackdrop` explicitly returns false in desktop mode; the separate original-game capture branch is not this app's normal backdrop. Preserve the shell's live blur, brightness, theme, vignette and transition behavior through the Windows compositor where possible. Windows 11's [system backdrop API](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type) offers a desktop-acrylic material, but that API alone does not prove equivalent opacity controls or visuals. This remains a native feasibility check; do not replace the current behavior with constant screen capture or silently introduce a frozen snapshot. Full-display capture can include the HUD and cause recursive blur.

Do not permanently exclude the HUD from capture: users need to record demonstrations. Preserve the event-driven cursor ownership in [HUDRenderedCursor.swift](Sources/HUDRenderedCursor.swift). Choose and test a single native/custom-drawn cursor path; restore the normal cursor on close, focus loss and native dialogs. No polling timer that repeatedly forces or hides the cursor. Verify Windows built-in recording and the saved video: no alternating arrows, duplicate cursor or missing HUD. [Native cursor capture controls](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscapturesession.iscursorcaptureenabled?view=winrt-26100) apply to the capture session, not every external recorder.

### Scheduling contract

| State | Allowed work |
| --- | --- |
| Closed | Tray/hotkey messages and necessary OS notifications; persist changed data; work/calendar deadlines when required. Stop frame submission, backdrop capture, tilt, map painting, game physics and video presentation. |
| Open and idle | Retain the selected visual mode and ambient behavior; redraw only at the cadence actually needed. Metadata updates invalidate changed regions rather than all modules. |
| Opening/closing/scrolling | One synchronized frame clock; prioritize input and presentation. No synchronous disk/network/process scanning on the UI thread. |
| Inactive module | Park its expensive work and observers unless a narrowly defined shared service needs them. Keep a bounded snapshot for fast return. |
| Sleep/resume/device loss | Stop work safely; rebase deadlines and recreate invalid GPU/device resources. Do not advance physics through a long sleep interval. |

Waitable DXGI swapchains are one option for controlled frame latency; see [Microsoft's guidance](https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains). Do not substitute continuous spinning for a scheduler. User-visible reduced-motion/low-power settings must work, but they must not be the only way to avoid excessive CPU.

## 5. Platform shell, input and accessibility

- Tray icon/menu: adapt [AppDelegate.swift](Sources/AppDelegate.swift). Use [Shell_NotifyIcon](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw), handle Explorer restarting, and keep a working activation path even if the icon is hidden by Windows. Menu shortcut labels reflect the user's actual shortcut.
- Shortcut: adapt [GlobalShortcutController.swift](Sources/GlobalShortcutController.swift) and [SummonShortcut.swift](Sources/SummonShortcut.swift) using [RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey). Keep Ctrl + backtick as the initial intent, account for keyboard layout, and report conflicts. Do not copy Mac physical keycode numbers into Windows virtual keys.
- Displays: preserve explicit screen selection and active-display mode with [per-monitor DPI awareness](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows). Recover to a valid screen if the selected one disappears. Respect work areas and fullscreen apps.
- Editing: [HUDProjectedTextEditor.swift](Sources/HUDProjectedTextEditor.swift) is a core parity requirement. A native edit HWND does not automatically share a 3D-rendered plane. Implement a projected editor backed by a real text/IME model; keep visual caret, selection, hit testing and IME candidate positioning in sync. DirectWrite alone is not an editor. Evaluate [TSF](https://learn.microsoft.com/en-us/windows/win32/tsf/text-services-framework) and [UI Automation providers](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-providersoverview).
- Focus: dialogs and menus block underlying actions; Escape unwinds the top state first. Preserve clipboard/undo, selection, tab order, focus indicators and accessible names for image-only controls.
- File drops: use OLE drop targets for the HUD/shelf, with ordinary files and virtual-file formats, Unicode paths, folders/packages where applicable, cancellation and duplicates. Do not read the entire file merely to add a reference.
- **Direct tray-icon drop is unproven.** The Windows notification-area icon is not an AppKit status-button view, and Shell_NotifyIcon does not itself offer that view's drop contract. Prototype direct drops and test Explorer/taskbar/Downloads sources. The ordinary HUD shelf target must work regardless. If direct tray drop cannot be supported cleanly, report it as a platform gap; do not silently claim parity or introduce an unrequested floating widget.

## 6. Module parity map

All modules below are required. The Windows mechanisms are candidates; verify their real behavior and packaging requirements on the laptop.

| Module | Mac source / description | Windows work and acceptance |
| --- | --- | --- |
| System, Display, Hotkeys, About | [HUDSettingsController](Sources/HUDSettingsController.swift), [settings](docs/settings.md) | Same settings, theme/language updates, icon/logo choices, clock styles, monitor selection and confirmations. Replace Mac-only settings with explicit platform equivalents; keep credits/update access. |
| Notes | [NotesStore](Sources/NotesStore.swift), [NotesRichText](Sources/NotesRichText.swift), [notes](docs/notes-canvas.md) | Text, to-do, image/video, drawing, pinned windows, media progress, font/color/style tools, menus, drag/resize and persistence. Formatting appears while editing; dark text stays readable; rich runs use UTF-16. |
| Temporary File Shelf | [FileShelfStore](Sources/FileShelfStore.swift), [shelf](docs/file-shelf.md) | Smooth scrolling, add/drop/open/reveal/export references, clear without deleting originals, stale-path detection. Replace bookmarks/file identities and verify tray-drop feasibility separately. |
| Clipboard | [ClipboardStore](Sources/ClipboardStore.swift), [clipboard](docs/clipboard-cache.md) | Session-only scrollable text/image/file entries, deduplication and exclusions. Prefer [AddClipboardFormatListener](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-addclipboardformatlistener) over fast polling. Respect format availability and sensitive content. |
| Archive | [ArchiveStore](Sources/ArchiveStore.swift), [archive](docs/archive.md) | Editable rich documents/media thumbnails, categories, smooth scrolling/transitions. 全部 and 未分类 remain protected; preserve user categories and attachment-menu placement. |
| Media Assembly | [MediaAssemblyController](Sources/MediaAssemblyController.swift), [media](docs/media-assembly.md) | Image/video open/close, zoom, handles/crop, curves/levels/adjustments, range trim with live seek, stickers/filters, export. Reuse supplied presets; removed commercial stickers stay removed. Consider [WIC](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-lh) and [Media Foundation](https://learn.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk); verify codecs and export equivalence. |
| Minigame | [OrbiPomRuntime](Sources/OrbiPomRuntime.swift), [runtime](docs/orbipom-runtime.md) | Exact existing skills, physics, scoring, danger countdown, pause/game-over dimming, rules and 技力 terminology; no sound. Reuse offline Matter 0.20.0/game scripts. Choose a small embedded JS engine only after seeded parity tests; no browser or separate timer. Paused games must stay paused after reopening. |
| Now Playing | [NowPlayingController](Sources/NowPlayingController.swift), [music](docs/now-playing.md) | Automatic session discovery, square artwork, transport/seek, volume, lyrics and transitions; no old title/refresh block. Use [SMTC session manager](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager?view=winrt-26100), subject to provider support and deployment capability. Generic SMTC does not promise lyrics or every player. Test NetEase, QQ Music, Kugou, Spotify and available local players. Preserve `No music playing` in every localization. |
| Volume / per-app audio | [AudioDeviceController](Sources/AudioDeviceController.swift), [PerAppAudioController](Sources/PerAppAudioController.swift), [audio](docs/per-app-audio.md) | Core Audio endpoints and event-driven sessions; [IAudioSessionManager2](https://learn.microsoft.com/en-us/windows/win32/api/audiopolicy/nn-audiopolicy-iaudiosessionmanager2), [ISimpleAudioVolume](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nn-audioclient-isimpleaudiovolume). Verify shared/exclusive sessions and paused-player volume. Arbitrary cross-app device routing is a separate unresolved capability. |
| Projection | [projection](docs/projection.md), existing projection source files | Screen-sized drawing/media, compact centered rounded toolbar, Clear All confirmation, Return, transparent/darker dotted backdrop toggle, same media insertion rules as Notes. Test click-through behavior and focus independently. |
| Reader | [ReaderController](Sources/ReaderController.swift), [reader](docs/reader.md) | TXT/EPUB/PDF, library/bookmarks, horizontal page flips or vertical scrolling, zoom preserved across pages, anchored menus and default font 10 / line spacing 2 / margins 16. Evaluate [Windows.Data.Pdf](https://learn.microsoft.com/en-us/uwp/api/windows.data.pdf.pdfpage?view=winrt-26100); retain bounded rendering and EPUB parsing semantics. |
| Work Mode | [WorkModeController](Sources/WorkModeController.swift), [work mode](docs/audio-and-work-mode.md) | Countdown/stopwatch, completion and lifetime hours independent of OS Focus. Windows [TryStartFocusSession](https://learn.microsoft.com/en-us/uwp/api/windows.ui.shell.focussessionmanager.trystartfocussession?view=winrt-28000) is a Limited Access Feature requiring authorization. Do not promise generic Focus/DND switching; capability-gate or defer OS integration while preserving the timer. |
| Calendar | [HUDCalendarController](Sources/HUDCalendarController.swift), [calendar](docs/calendar.md) | Same event/reminder rules, category/color choices, left-aligned items, themed Today/add/refresh/confirm controls. Reconcile notification registrations; avoid duplicate reminders after import. |
| Map | [WorldMapRasterController](Sources/WorldMapRasterController.swift), [map](docs/map.md) | Keep the optimized center-country highlight version, contour/country depth, theme pins, pan/zoom and feathered circle. Default/reset zoom 3×, current min 2.1× / max 128×; no place labels/helper paragraph. Left click hides coordinates, right-click existing ping removes it. Cache raster work; no all-country hover recalculation on every mouse event. |
| Event Log | [SystemEventLog](Sources/SystemEventLog.swift), [events](docs/event-log.md) | Same bounded events, filtering and smooth tabs; platform event adapters must preserve known kinds and avoid logging credentials or raw private content. |
| Personal Profile | [UserProfileStore](Sources/UserProfileStore.swift), [profile](docs/personal-profile.md) | Names/tags/UID/date, levels/counts, work hours, biography, avatar/background sliders/reset/hide, local theme and sync locks. Match both card frames, quote/pencil icons, crop quality and menu tilt. |
| Account Linking | [HypergryphAccountController](Sources/HypergryphAccountController.swift), [account](docs/account-linking.md) | China and Global official login, per-region role selection/cache, profile/sanity sync and shaped hover dropdown. Follow the working current client; see section 8. |
| Power / device battery | [BatteryMonitor](Sources/BatteryMonitor.swift), [overlay](docs/system-overlay.md) | Windows power notifications and initial state, localized charge/battery transitions, fixed compact timing, hover expansion, alerts and exposed accessory readings. Distinguish plugged-in/charging/full states; no delayed or stale 超充模式. Unknown accessory readings stay unavailable. |
| App shortcuts / Add App | [AppShortcutStore](Sources/AppShortcutStore.swift), [shortcuts](docs/app-shortcuts.md) | Windows executable/shortcut selection and launch; names/icons must match the selected preset. Support Unicode and packaged apps through tested adapters, not `.app/Info.plist` assumptions. Preserve list order and always-last Add App. |
| Storage / Activity Monitor | [StorageController](Sources/StorageController.swift), [AppActivityMonitor](Sources/AppActivityMonitor.swift), [monitoring](docs/storage-and-activity.md) | Windows disk/process/system counters sampled only as needed. Retain chart yellow/blue independently of theme, RAM labels, equal sector glow and current icons. Avoid enumerating all processes or disk trees on every pointer/frame event. |

### Windows status (October 10, 2026)

Every module in the table runs inside `EndfieldHUD.exe` through the single application owner, either as a built-in owner or as a registered module area (`windows/app/areas`). The hidden owner coverage (`application_owner_coverage`) runs each module over synthetic data. "Live acceptance" means the user still has to try the module on the laptop desktop. The README verification log records the test results.

| Module | Windows status |
| --- | --- |
| Settings (System, Display, Hotkeys, About) | Wired. The display chosen in Settings (`windowsDisplayID`) now places the HUD at every opening and on display changes; the earlier code read the Mac UUID key. About shows the source version (`1.2.0 (18)`) and opens its GitHub and credit links once the HUD has closed. There is no Windows updater, so the update row and the tray's *Check for Updates…* open About and the GitHub releases page; automatic installation is reported unavailable. The Windows-only System row *Import macOS data…* belongs to the macImport area. Remaining: the custom accent colour picker, center-logo import and the app-icon choice. |
| Now Playing | Wired (module area `nowPlaying`, in `EndfieldHUD.exe` and hidden coverage). Media sessions come from GSMTC. Lyrics are taken from the embedded tags first, then the NetEase catalog (NetEase only), then LRCLIB. Volume uses the playing app's per-app mixer route through the shared AudioRuntime. **Platform gap:** SMTC cannot set a player's internal volume, and browser and multi-process players stay unavailable. Playback actions are recorded in the Event Log as `playbackAction`. The UI Automation fragments are listed under the HUD window's UIA root once the area implements the root hooks. Remaining: the live acceptance matrix per player, run by the user. |
| Media Assembly | Wired (module area `mediaAssembly`). The host provides Shelf choices, HUD file drops (`ModuleOwner::drop`), a separate executor for long exports and the played-frame filter hook. Gaps: MOV export, HEVC/HEIC, CPU still-export speed, and moved originals are not followed by file ID. |
| Personal Profile / ID card | Wired (module area `profile`). The host connects the bottom-left ID card presenter to the area's feed. The area is the only `profile.json` writer, so Work Mode no longer writes lifetime hours itself. The shared dialog now has a single-file mode with a filter (`ApplicationFileRequest::singleFile`), but the area still has to request it with the installed WIC image filter; until then it takes the first regular file chosen. |
| Account Linking | Wired (module area `account`): face, header wallet and popover, official login (WebView2), DPAPI vault and profile sync through AccountProfileLink. The UIA peers exist but are not listed under the HUD root until the area implements the `ModuleOwner` automation hooks. Remaining: live acceptance (China and Global login). |
| Power / charge badge | Wired (module area `powerCharge`). The tray shows the area's status, tooltip and *Preview charging effect*, and `--preview` works at launch. Settings *Edit position* opens the area's editor, and summons are refused while it is open. The Power page shows the area's merged reading. The host offers a core-plane slot below pinned Notes (`planeEntries()`), but the badge reaches it only once the area returns it there instead of from `floatingEntries()`. The alert stays hidden while Projection is active, and the shared Activity sampler drives the CPU/RAM/Network/Disk metrics. |
| App shortcuts / Add App | Wired (module area `appShortcuts`). Saved apps are bound into the source navigation between Power and Add App, and their actions start the area's close-first launch. Gaps: no generic reopen event (an existing `.exe` window is activated instead), no packaged-app logos or AppsFolder choice in the shared dialog, and no drag and drop or paste onto Add App. |
| Reader | Built in. PDF titles now match PDFKit, which removes the earlier "filename only" gap. Error messages and the Windows-only picker texts are localized in five languages. Reader actions are recorded in the Event Log, and HUD file drops import the first PDF/EPUB/TXT. |
| Notes / Archive / Projection | Built in. Notes, Archive and Projection actions are recorded in the Event Log, and media errors use the source descriptions in five languages. Files dropped on the HUD import into Notes, or into an open Archive document (up to 16). Imported Mac font names resolve to their Windows families at the rasterizer. Gap: image data without a file (no `CF_HDROP`) cannot be dropped on Notes through the HUD's shared file-drop target. |
| Calendar reminders | Wired (module area `calendarReminders`). The notification identity belongs to the installer (see section 10). Runs over an isolated `--data-root` never touch it. |
| UI Automation | The HUD window answers `WM_GETOBJECT` with one UIA root. Each owner that has fragments appears under it as a group named after its module. Volume is wired. The Now Playing, Media Assembly and Account areas have their fragments, but each area still has to implement the `ModuleOwner` automation hooks. Reader, Map, Calendar and Minigame descriptors are not yet exposed. |

Notifications and launch-at-login require packaging/identity decisions: see [app notifications](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/app-notifications/) and [StartupTask](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.startuptask?view=winrt-26100). Respect settings disabled by the user. Start no duplicate watcher for the same OS event.

### Windows platform gaps

Each gap below was recorded by the stage that owns the code, together with the fallback that ships. This list contains every gap still current at checkpoint 123. The first table covers capabilities that Windows does not offer, or offers only through a path the project rules forbid. The second covers limits that come from pending work, the test environment or a deliberate decision; those are not OS limits.

**OS and platform limits**

| Area | Capability | Reason | Fallback |
| --- | --- | --- | --- |
| Shelf | Direct file drop on the tray icon (Mac StatusItemFileDropView) | `Shell_NotifyIconW` has no drop contract. Explorer's process draws the icon, and `RegisterDragDrop` accepts only the caller's windows. Injection, hidden or floating windows and a taskbar button are excluded (section 5). | Open the HUD and drop on the Shelf or its navigation entry, use *Add files*, or press Ctrl+V. All three record the same `shelfAdded` events. |
| Shelf | Virtual-file drops and pastes (Outlook attachments, browser images, items inside zip folders) | These items have no filesystem object, and the Mac Shelf stores references only. | Refused with `DROPEFFECT_NONE`, plus a visible, localized explanation once per drag. |
| Shelf | Following a reference moved to the Recycle Bin, to another volume, or kept on a share without open-by-ID | Windows renames items in `$Recycle.Bin`, file IDs are per volume, and SMB/FAT may not support `OpenFileById`. | The card shows *Unavailable*; nothing is matched by name or path. |
| Import, Notes, Archive | Opening imported Mac bookmarks, security-scoped URLs and inode/volume identities | macOS bookmarks and file identities cannot authorize Windows files. | The relink list (`Migration/relink.json`) and explicit relink actions. Paths are never guessed. |
| Dialogs | Asynchronous NSOpenPanel/NSSavePanel sheets over the HUD | `IFileOpenDialog`/`IFileSaveDialog::Show` are modal on the UI thread. | Requests are posted and run outside page input, after pointer capture is released. A module switch or close cancels the request. |
| Profile | Modeless, continuous NSColorPanel without Cancel | Win32 has no modeless system colour panel; `ChooseColorW` is modal with OK/Cancel. | A hook streams each settled colour live. OK commits; Cancel or hiding the HUD restores the starting colour (the Mac keeps the last live colour). |
| Profile | Core Image Lanczos portrait crop | Not available on Windows. | Linear-light Lanczos emulation with measured bounds, cropped from a 1024 px working copy. |
| App shortcuts | Reopen event to a running app (`createsNewApplicationInstance=false`) | Windows has no generic reopen event for an arbitrary process. | A plain `.exe` gets its existing visible window restored and activated. Other targets use the normal Shell launch, and packaged apps use `IApplicationActivationManager`. |
| App shortcuts | Logos of packaged (Store) apps for the App icon preset | The logo comes only from asynchronous WinRT streams, and Shell image factories can load third-party handlers that could hang the shared executor. | The source fallback glyph. `.exe`/`.lnk` targets get their real icon (`SHDefExtractIconW`). |
| Media Assembly | QuickTime MOV export | Media Foundation has no QuickTime sink (`movExport=0`). | H.264/AAC MP4 named `<stem>-edited.mp4`. A `.mov` destination is refused with the localized message. |
| Media Assembly, Notes, Archive, Projection | HEIC/HEIF, HEVC, ProRes, JPEG-2000, WebP and some legacy codecs | These need optional Store codecs or have no inbox codec (laptop: `heifDecode=1`, `hevcDecode=0`, `heicEncode=0`). | The localized *unsupported* error; nothing is substituted. HEIC appears in Save As only when a real test encode succeeds. |
| Notes | Video notes on Windows N editions | Media Foundation is missing without the Media Feature Pack. | Movie cards show the source error text; image and GIF notes still work. |
| Media Assembly | ImageIO thumbnail resampler for previews | Not available on Windows. | WIC Fant scaling to at most 1024 px, for previews only. |
| Notes, Archive | Rich clipboard content from other apps (RTF/HTML) | Windows has no system attributed-string pasteboard, and parsing foreign RTF/HTML is out of scope. | Plain `CF_UNICODETEXT` in the current typing style. Formatting round-trips between EndfieldHUD fields through a private format. |
| Notes, Archive | ICU word segmentation on Windows 10 before 1903 | `icu.dll` ships only since 1903. | A Unicode character-class fallback keeps word navigation and multi-click selection working. |
| Now Playing | A player's internal volume (Mac AppleScript `sound volume`) | SMTC/GSMTC has no volume command or property. | The playing app's per-app mixer route through the shared `AudioRuntime`. |
| Now Playing | Volume for browsers (explicit AUMIDs) and multi-process players | No public API maps an explicit AUMID to one process, and the source forbids guessing. | The source *unavailable* submenu ("No single adjustable audio process. Use Volume controls."). |
| Now Playing | Players that do not publish to SMTC | GSMTC is the only event-driven, permission-free media-session source. | *No music playing*. Each such player is recorded in the live acceptance matrix. |
| Now Playing | Timed lyrics supplied by the player | GSMTC media properties carry no lyrics. | Embedded lyrics, then the NetEase catalog (NetEase only), then LRCLIB, requested only while visible. |
| Volume | Switching the default output/input device | Windows has no documented API; only the undocumented `IPolicyConfig` can change it, and the project allows no undocumented interfaces. | Chooser rows stay disabled, as on the Mac when the default is not settable. The user switches in Windows Settings. The research interop compiles only under a define that no build sets. |
| Volume | Per-app output device routing | Only the undocumented `IAudioPolicyConfigFactory` exists. | Per-app routes act on the app's sessions on the default output, like the Mac's taps on the default device. |
| Volume | Detecting exclusive-mode streams | No public session API exposes the share mode, and `ISimpleAudioVolume` writes have no audible effect on exclusive streams. | The route shows the confirmed readback; listed for live acceptance. |
| Volume | In-memory per-app taps that vanish with the process | Windows persists each app's mixer level and shows it in the Volume Mixer. | A recovery journal, restore on exit, sleep, quit and device change, and startup recovery only when the level is unchanged. |
| Event Log | Mac HAL device-list granularity for audio-device events | Windows lists output and input endpoints separately, and no documented property marks virtual devices. | Active render and capture endpoints keyed by endpoint ID, with labels only. |
| Activity | Per-app network bytes | No unprivileged per-process counter; ETW and TCP EStats need elevation. | A dash and the note "Per-app disk and network accounting unavailable". |
| Activity | Per-app disk read/write | `GetProcessIoCounters` mixes file, network and device I/O; the ETW disk provider needs elevation. | A dash; system-wide disk rates still come from PDH. |
| Activity | App memory equal to `phys_footprint` | No public counter equals it; `PrivateWorkingSetSize` needs build 20348 or later. | The private working set (Task Manager's Memory column), or the whole working set on older builds. |
| Activity | Compressed memory, and CPU on more than one processor group | No public compressor footprint matching the Mac formula; `GetSystemTimes` covers at most 64 logical CPUs. | Compressed memory is not shown; CPU reports unavailable on multi-group machines. |
| Activity | Windowless background apps | Windows has no running-application registry for desktop processes without a top-level window or AUMID. | They are not listed as app cards. |
| Event Log | Historical time-zone rules | The Windows time-zone database starts each zone's dynamic DST at its first entry. | The zone's earliest Windows rule applies; 3 of 102 pre-2025 oracle instants differ. |
| Clipboard | Another process holding the clipboard open (no Mac equivalent) | Windows-only state. | One coalesced retry, then the localized *Clipboard unavailable* status until the next capture. |
| Power | Alert over exclusive-fullscreen Direct3D apps | A top-most window cannot cover an exclusive-fullscreen swap chain. | No alert there; the reading, badge, tray and Event Log still update. |
| Power | Battery health category (`kIOPSBatteryHealth`) | Windows has no battery-condition category, and the source forbids a computed percentage. | Health stays *Unavailable*. |
| Power | Accessory (Bluetooth/HID) battery readings | Not provided on the Mac either. | Only the host row *This PC*; unknown values stay unavailable. |
| Power | Capacity on relative-scale batteries | Some batteries report `BATTERY_CAPACITY_RELATIVE` or unknown capacity. | The explicitly labelled `p / 100 %` pair, never mislabelled mWh. |
| Calendar | Floating civil-time reminder triggers | Scheduled toasts fire at absolute instants. | Reconcile at launch, on time and settings changes and on resume. |
| Calendar | Notification identity for an unpackaged app | Desktop toasts need an AppUserModelID declared by a Start-menu shortcut. | The installer writes and removes it. Without it the `calendarReminders` area stays absent and reminders report *unavailable*. |
| Calendar | Notification permission prompt | Windows has no app-initiated prompt. | Enabled maps to authorized; disabled for the app, the user or by policy maps to denied; no identity maps to unavailable. |
| Calendar | Notification click activation | A toast click arrives through the shortcut's COM activator. | The running HUD registers the activator and consumes clicks without opening the HUD, as on the Mac. If the app is not running, Windows starts it with `--notification-activation`. |
| Calendar | Banner delivery under Focus assist | Windows suppresses the banner, as Mac Focus does. | The toast still reaches the notification center. |
| Work Mode | OS Focus / Do Not Disturb switching | `FocusSessionManager.TryStartFocusSession` is a Limited Access Feature. | Not wired. The timer, completion and lifetime hours do not depend on it. |
| Account | Official sign-in through WebView2 | It needs the installed Evergreen WebView2 runtime, which is not redistributed, and only the x64 loader is vendored. | Connect checks the runtime and otherwise shows the Mac text "Sign-in could not finish. Please try again."; ARM64 and x86 builds have no official login. |
| Account, Import | Mac Keychain credentials | Credentials must never leave the macOS Keychain (section 8). | Imported regions need reconnection; DPAPI blobs are written only after a fresh Windows sign-in. |
| Account | Two Mac strings that name the Keychain | The Keychain does not exist on Windows. | A Windows wording in all five languages from an owned table. |
| Import | Transferring macOS notification registrations | The macOS notification center does not exist on Windows. | Receipts are cleared at import; Calendar's reconcile schedules each future reminder once. |
| Import | Translating a custom macOS summon shortcut | Mac key codes and Command/Option modifiers have no faithful Windows equivalent. | Only the default Ctrl + backtick maps. The report says "Set again in Hotkeys", and a hotkey already chosen on this PC is kept. |
| Minigame | Bit-identical long-run physics against JavaScriptCore | Apple libm is not correctly rounded and is unavailable elsewhere; the MSVC UCRT differs by one ULP on 48 of 4,918 inputs, and the contact solver amplifies it. | Every primitive is within 1 ULP of Apple. Discrete gameplay matches exactly, and the Windows trajectory is deterministic. |
| Packaging | Self-deletion of the uninstaller's worker copy | Unelevated code cannot delete its own running image. | The worker runs from `%TEMP%`, and the next Setup or uninstall run deletes stale worker folders. |

**Pending work, test environment and decisions (not OS limits)**

| Area | Limit | Reason | Current behaviour |
| --- | --- | --- | --- |
| UI Automation | Screen-reader access to Now Playing, Media Assembly and Account controls, the Reader/Map/Calendar/minigame descriptors, the Power position editor and the navigation entries | The HUD root exists (checkpoint 123). The areas still have to implement the `ModuleOwner` hooks, the descriptors need projection and invoke APIs, and the editor and navigation entries have no provider. | Only Volume is listed under the root. |
| Notes | Dropping raw image data (no `CF_HDROP`) on Notes through the HUD | The HUD's single OLE target delivers filesystem paths only. | Ctrl+V pastes image data as a managed PNG; image files drop normally. |
| App shortcuts | Choosing packaged apps; paste or drag and drop onto Add App | The shared dialog has no AppsFolder mode; the area does not implement `drop` or paste. | Choose the app's `.exe` or its Start-menu `.lnk`. |
| Profile | Image-only, single-file chooser | The host offers `ApplicationFileRequest::singleFile` with a filter, but the area does not request it yet. | The first regular file chosen is imported; a non-image shows the Mac image failure. |
| Profile | Restoring Work Mode lifetime into the controller at launch | Areas receive a read-only controller, and the profile loads asynchronously. | The stored lifetime is an offset over checkpoint listeners; each second counts once. |
| Profile | Grapheme limits inside the portable `ProfileStore` | The portable core has no Unicode segmentation (ICU is optional there). | The store applies every other Mac normalization; the profile owner applies grapheme limits with ICU. |
| Reader, Map | Precise touchpad scroll and pinch | The host's wheel events carry detents only. | Ctrl+wheel zooms; a WM_POINTER/DirectManipulation path is pending. |
| Media Assembly | Full-resolution still export speed | The exact CPU processor runs in row bands so stickers stay exact. | Correct, with progress and cancel; a heavy 24 MP edit takes seconds (about 1 s on the Mac). |
| Media Assembly | GPU editing without same-device media support or after a GPU failure | GPU previews need the renderer's media device. | The exact CPU processor on the utility worker; video export falls back to the CPU for that job. |
| Power | Sharing the HUD's D3D device with the alert window | `native::Renderer` binds one device per HWND; the optional second target was not built. | The alert keeps a second small device, created on the first alert. |
| Now Playing | URLSession 8 s idle timeout | It would need a per-request timer, which the scheduling contract forbids. | Only the 10 s total bound applies, folded into the host deadline. |
| Now Playing | JSONSerialization leniency (duplicate keys, nesting deeper than 64) | The shared `Json` parser rejects such input. | Malformed responses count as no lyrics or no match. |
| Renderer | Precompiled shaders | The renderer compiles `hud.hlsl` at run time (`D3DCompileFromFile`). | The package ships `hud.hlsl` pinned by SHA-256. |
| Packaging | EHUDZ01 payloads at run time | Not needed: only the compiled catalog ships. | A bounded decoder exists for staging verification only. |
| Packaging | Launch at login through an MSIX `StartupTask` | The installer is per-user and unpackaged. | `HKCU\...\Run` with `--login`; the user's StartupApproved choice is never overridden. |
| Packaging | Code signing | No OV/EV or Trusted Signing certificate exists. | Unsigned artifacts, reported as such; an opt-in `signtool` hook is ready. |
| Packaging | Automatic updates | No signed Windows updater or feed exists yet. | About and the tray's *Check for Updates…* open the GitHub releases page; automatic installation is reported unavailable. |
| Packaging | Complete packages and owner coverage in GitHub Actions | The Mac-exported runtime inputs are not in the repository. | CI builds an `-incomplete` developer package that refuses to install outside tests. |
| Packaging | GPU counters, frame pacing and input latency in the performance gate | PDH GPU counters are hidden in the non-console session; ETW needs administrator rights and an interactive desktop. | GPU gates are reported absent rather than failed. |
| Shell | A per-module failure view | The Mac has none (`presentSourceFailure` covers the whole shell). | A failing owner is switched off and the rest of the HUD keeps running. |
| Tests | Real OS message routes in hidden coverage | Hidden runs have no message loop, and the real route reads the wall clock. | Coverage routes through the same owner message chain on the synthetic clock. |
| Tests | Composition swap chains over SSH | Non-interactive sessions refuse them (`DXGI_ERROR_NOT_CURRENTLY_AVAILABLE`). | Hidden WARP targets; the two projection composition suites need a desktop session. |
| Tests | Live login and community API | Automated tests must never use the network or a real account. | Stubs, a fake official site and the Mac oracle envelopes; real behaviour is live acceptance. |

Resolved in batch 3, no longer gaps: the WebView2 SDK is vendored; `notes_video_playback` exposes the played-frame filter that Media Assembly compiles in; the HUD has a UIA root; the shared dialog has a typed single-file mode; Highlights/Shadows matches Core Image; PDF titles match PDFKit; the installer decision (per-user, unpackaged) settles the data root and notification identity.

## 7. Data compatibility and safe import

The Mac data root is **`~/Library/Application Support/EndfieldCharge/`**, not `EndfieldHUD`. The historical preference domain/bundle identifier is **`io.github.endfieldcharge.EndfieldCharge`**. Do not rename or modify the Mac stores during this work.

Use a versioned Windows root under `%LOCALAPPDATA%\EndfieldHUD`. Implement a one-time **copy/import** of a user-supplied offline export; never overwrite the source export or try to access the Mac's live data from tests. Do not turn this into cloud sync.

| Relative Mac path / store | Current format | What transfers |
| --- | --- | --- |
| `Notes/notes.sqlite3`, `Notes/Images/` | SQLite `user_version=2` | Text, checklist/rich-text/media/drawing JSON, note IDs/layout and managed images. Drawing rows retain SQL kind `text` and are identified by their drawing payload. |
| `FileShelf/shelf.json` | JSON version 1, `version/items` | Metadata/reference identity; not the original external files or usable Windows bookmarks. |
| `Profile/profile.json`, `Profile/Images/` | JSON version 1, `version/profile` | Local/synced IDs, name/tag, dates, crop/theme/work duration and managed images. Avatar `.image` files contain original encoded bytes; do not infer their codec from the extension. |
| `Archive/archive.sqlite` | SQLite `user_version=2`, `entries/categories/state` | JSON BLOB documents/thumbnail descriptors, category UUIDs, legacy template values and selected state. |
| `Reader/library.json` | JSON version 1, `version/books/selected/preferences` | Locations/bookmarks/preferences and book references; sources need relinking. Legacy `continuous` now means vertical mode; `rightToLeft` is retained but ignored. |
| `Calendar/calendar.json` | JSON version 1, `version/events` | Events and `{year,month,day}` calendar days; recreate/reconcile reminder registrations. |
| `WorldMap/map.json` | JSON version 4, `version/pins/viewport`; readers for 1–3 | IDs/styles and normalized equirectangular coordinates/viewport, not screen pixels. Preserve max 128 pins and bounded input validation. |
| `AppShortcuts/shortcuts.json` | JSON version 1, `version/items` | Names/presets/order; `.app`, bundle IDs, paths and bookmarks require relinking. |
| `EventLog/events.json` | JSON version 1, `version/events`, max 500 | Known event kinds, IDs, dates and allowlisted metadata. |
| `Account/profile-cache.json` | JSON version 1 | Region/roles/snapshots/sync preferences; **no credentials**. Mark session disconnected until reauthentication. |
| `CenterLogo/<revision UUID>.png` | Managed image, revision in configuration | Copy the selected custom logo and revision together. |
| UserDefaults | Settings schema marker 1 | Explicit settings export/import; best score `orbipom.bestScore.v1`. Platform-specific values require remapping. |

**Windows import entry point.** The Windows-only System row *Import macOS data…* opens the macImport card (`windows/app/areas/mac_import_*`). The card covers the folder choice, staging, review per data set and consent. The commit runs between two runs through the host's restart handoff (`DataRootRestart`, `app/areas/mac_import_handoff.hpp`). First the accepted-quit teardown closes every store. Then the commit runs with a backup and rollback and writes `<root>.mac-import-outcome.json`. Finally a fresh run opens the HUD on System and reports the result and the relink list (`Migration/relink.json`). The account controller reads `Account/profile-cache.json` only at construction, and that fresh run reconstructs it. Calendar reminders are rescheduled by reconcile after the receipts are cleared. See [windows/core/migration/README.md](windows/core/migration/README.md) section 2a and the exporter [windows/tools/mac_import_exporter.py](windows/tools/mac_import_exporter.py).

**Import status (checkpoint 123).** The importer, the card and a synthetic restart handoff pass `mac_import_contracts`, `mac_import_flow_contracts` and hidden owner coverage over the Mac-generated golden export. The production restart path, an export of real user data and the real `%LOCALAPPDATA%\EndfieldHUD\v1` root have not been exercised. Notes, Archive and App shortcuts can relink their own imported records, but no module yet updates `Migration/relink.json` (`MacRelinkLedger::markResolved`/`reconcile`), so the import card's relink list does not show those relinks. Reader books and imported Shelf items have no relink action yet. Mac file identities, notification registrations, custom summon shortcuts and Keychain credentials cannot transfer; see "Windows platform gaps" in section 6.

Clipboard history, Media Assembly editing sessions and minigame boards are session-only. There is nothing to import for them. Work Mode's active timer is also memory-only; only `accumulatedWorkSeconds` in the profile transfers. Do not invent persisted sessions or reset the user's first awakening date to Windows installation time.

### Codec traps

1. **Two date epochs:** default Swift Codable JSON `Date` is seconds since 2001-01-01 UTC. Add `978307200` to convert it to Unix seconds. Notes SQL `created_at` also uses 2001. Archive SQL `date`, `modified` and category `created` use Unix seconds, but dates inside its JSON BLOBs use the Foundation default. Identify each field before converting.
2. Rich text is **version-1 JSON, not RTF**. [NotesRichText.swift](Sources/NotesRichText.swift) uses UTF-16 `location/length` runs and font/color/style descriptors. Plain text is canonical. Preserve surrogate pairs, combining sequences, emoji, styles, undo and font fallback.
3. Keep exact key casing, enum strings, optional-field defaults, UUIDs and identifier strings. Never parse account IDs through floating point; values above `2^53` may lose digits. `Data` fields are base64, including opaque Mac bookmarks.
4. Current reference validators require `/`-prefixed absolute paths, and shortcuts expect `.app`. Windows paths need a platform-aware reference model and schema migration; writing `C:\...` into the old validators is not compatibility.
5. Mac bookmarks/device/inode/volume UUIDs cannot authorize Windows files. Preserve unresolved references and ask users to relink; never silently replace an unavailable image/book with another file at a similar path. Managed image copies are different from referenced external media.
6. Export SQLite while the source app is closed or through SQLite's backup API. Do not copy only the database while live WAL writes exist. Back up destination data, validate the whole import, write atomically, and support rollback. Reject newer/malformed schemas without silently erasing them; preserve unknown fields through a versioned import model.
7. Character limits are not all UTF-16 lengths. Swift profile name/tag/biography limits use extended grapheme clusters (20/10/150); rich-text offsets use UTF-16, and byte guards use UTF-8. Preserve each unit separately. A Windows `wcslen` check alone changes emoji and combined-character behavior.

Settings keys currently include:

```text
hudSettingsSchemaVersion, displayMode, displayDuration, accentHex, theme,
scale, placement, customScreenID, customPositionX, customPositionY,
language, hudScale, hudOffsetX, hudOffsetY, parallaxIntensity,
perspectiveIntensity, backgroundDarkness, blurAmount, reduceMotion,
ambientAnimation, closeOnFocusLost, openOnActiveDisplay, hudDisplayUUID,
hudDisplayName, launchAtLogin, batteryAlertsEnabled, devicePopupEnabled,
lowPowerVisualMode, applicationIcon, clockFormat, clockStyle, centerLogo,
centerLogoRevision, alertMetric, summonShortcut
```

Also inspect `hasLaunched`, `orbipom.bestScore.v1`, `HUDUpdateAutomaticallyInstall` and `HUDUpdateLastNotifiedRelease`. Read values/defaults from [Models.swift](Sources/Models.swift), not an old screenshot. Translate monitor identity, startup state and hotkey intent rather than copying Mac IDs/keycodes. Do not import Mac updater feed state into Windows. Require an explicit plist/settings codec; a plist is not a JSON file renamed.

## 8. Account linking and sanity

The current China Endfield and Arknights profile routes were verified with the user's account. Global support exists in source but **live Global verification is still outstanding**. The old `10001` experiments are historical; use the working client on this baseline, not an earlier tester or a guessed endpoint.

Read [HypergryphAccountAPI.swift](Sources/HypergryphAccountAPI.swift), [HypergryphAccountLogin.swift](Sources/HypergryphAccountLogin.swift), [HypergryphAccountModels.swift](Sources/HypergryphAccountModels.swift), [HypergryphAccountController.swift](Sources/HypergryphAccountController.swift), and [HypergryphAccountKeychain.swift](Sources/HypergryphAccountKeychain.swift).

- Keep Mainland `skland.com` and Global `skport.com` origins/credentials/roles separate. Open the official login destination directly; preserve strict origin/navigation checks and validated web messages. Follow [WebView2 security guidance](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security); dispose the browser after login/cancel.
- Preserve the exact current request canonicalization, signing headers, token renewal/device context, role identifiers, cancellation generations, response limits and error handling. Port against synthetic byte-for-byte signing fixtures before live tests. Do not scrape a different app, bypass authorization or export passport credentials.
- Community credentials remain outside profile/cache/log files. Mac Keychain uses service `com.ddduoduo.EndfieldHUD.hypergryph.account`, accounts `mainland/global`, device-only/unlocked/non-synchronizing storage. **Require Windows re-login.**
- A restricted current-user [DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)-protected store is a candidate. Do not use machine-wide encryption. Credential Manager's [2,560-byte blob limit](https://learn.microsoft.com/en-us/windows/win32/api/wincred/ns-wincred-credentialw) must be considered: accepted source credential strings can exceed it. Never truncate a token to fit.
- Keep selected-role sanity, shaped hover/click dropdown and manual refresh. Current active-HUD API cadence is **600 seconds**, with failure backoff/cancellation. Preserve it unless an evidenced service constraint requires adjustment; no 30-minute or per-second API poll.
- Project recovery locally between successful snapshots: Arknights 360 seconds/point, Endfield 432 seconds/point. Respect server observation/full-recovery timestamps and unknown deadlines; never reduce a reported value or clamp purchased sanity above cap. Reuse the existing HUD clock, not a new timer. Closed HUDs do not run active sanity polling.
- Personal-profile sync applies to the selected **Endfield** snapshot; Arknights currently contributes sanity. Sync name, `#`, game UID, awakening date and available levels/counts, with UID/name/tag/date editing locked. Avatar sync is a separate toggle. Bottom-left card displays the synced UID and omits `#`.
- Preserve the original local UID. Enabling profile sync sets `gamePlayerID` and clears `playerIDOverride`; display priority remains `playerIDOverride ?? gamePlayerID ?? uid`. Disabling sync unlocks editing and retains the last imported values; it does not restore an earlier name/tag/date or manual UID automatically. Keep those semantics during import and disconnection.

No real account tokens or IDs belong in fixtures, logs, documentation or Git. Windows login/China/Global recovery are separate acceptance tests, even though the China protocol already worked on Mac.

## 9. Performance and verification gates

Measure on the actual Windows laptop before claiming parity. Save hardware/build/settings with each result. The Mac historical numbers are not a Windows benchmark. Use Windows Performance Recorder/Analyzer and GPUView or the available Visual Studio profiler; Microsoft documents [WPR/WPA](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/) and [GPUView](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/using-gpuview).

| Scenario | Required evidence |
| --- | --- |
| Closed for 60 seconds | No submitted HUD frames or repeating map/game/backdrop tasks; near-zero HUD GPU activity, minimal CPU/wakeups. Explain any required timer/notification work. |
| Open idle, ambient on/off | Low CPU, bounded RAM/GPU caches; same visible animation. Separate app CPU from compositor/GPU utilization. |
| Pointer movement and navigation | Continuous tilt, no whole-tree rebuild or process scan per event, responsive hotkey and tab switching. Capture input-to-first-frame latency and p50/p95 frame times. |
| Opening/closing | Stable 60 fps where hardware allows, about 16.67 ms frame budget; exact ordering/timing and pointer tilt. Profile first and warmed opens separately. |
| Heavy modules | Notes with media, large archive, zoomed PDF, Map drag/zoom, Activity Monitor, video editing and active minigame. Bounded work; no hidden-module loops. |
| Repeat lifecycle | At least 100 open/close and module-switch cycles; no increasing live objects, threads, handles, GPU buffers or working set after caches settle. |
| OS changes | Sleep/resume, unplug charger, reconnect devices, mixed-DPI monitor hot-plug, Explorer restart, recording, update cancellation and exit while tasks run. |

Report private bytes/working set, allocation peaks, GPU memory, CPU/wakeups, frame pacing and startup latency using consistent settings. Do not declare success because one idle average improved while mouse movement still consumes half a core. Use bounded queues, immutable snapshots, async I/O, cancellation and dirty invalidation before reducing visible effects.

Tests use a temporary data root, fake clock, memory credential vault, fixture providers and synthetic documents. Cover date epochs, SQLite migrations, UTF-16 styles, long IDs, missing files, schema rejection, sync locks, reminder deduplication, clipboard exclusions, shortcut conflicts and paused minigame. Physics tests compare seeded outcomes to the original scripts; rendering tests compare the same source frame/time/pointer.

Live checks still needed on Windows: all supported music providers/lyrics/artwork/volume, Global account login, tray file drops, device battery details, IME/accessibility, GPU loss, codecs/PDFs, install/update/signing and recording cursor behavior. Record each as passed, failed or unverified; do not collapse unverified cases into a generic “tested.”

## 10. Build, updates and release boundaries

Mac `scripts/build.sh` uses `xcrun`, Mach-O, Cocoa/Metal and Apple signing. Sparkle, `.app`, `.pkg`, `.dmg` and the Mac appcast are not Windows build/update tools.

Create a Windows-only CMake/build/package path. Evaluate signed MSIX/[App Installer](https://learn.microsoft.com/en-us/windows/msix/app-installer/app-installer-file-overview) or a small signed installer/updater after testing the needed desktop capabilities. Verify asset signatures/hashes and supported architecture; save data, stop work and perform safe replacement/rollback. Respect update consent and developer-build protection.

Windows and Mac releases must select their own assets and feeds. Never replace the existing Mac v1.2.0 artifacts or feed with a Windows package. Do not change version/release notes until DDDuoDuo chooses a Windows release. No instruction to disable Windows security or bypass signing belongs in the installer.

Package only runtime dependencies/assets. Exclude recordings, GIFs, raw reference exports, intermediate shaders, build caches, symbols from the consumer package, and all user data. Keep separate developer symbols for debugging. Document the installed/download size and dependency overhead before deciding the packaging strategy.

**Windows packaging decisions (current).**

- The installer is per user and unpackaged (`windows/packaging`). It owns the Calendar notification identity: AppUserModelID `io.github.endfieldcharge.EndfieldCharge`, toast activator `{FF18639C-ADE8-4BA7-AC09-7F1216F9E200}` with its `LocalServer32`, and `Programs\EndfieldHUD.lnk`, all written through `native::installAppNotificationIdentity` and removed on uninstall. Platform gaps: toasts fire at absolute instants; there is no permission prompt; Focus assist can hold them back; and a click on a toast routes through the COM activator without opening the HUD, as on the Mac.
- The Evergreen WebView2 runtime is a prerequisite and is not redistributed. Each Connect detects it through the loader and, without it, reports an explicit failure. The vendored SDK in `windows/third_party/webview2` is used only at build time: the static loader is linked into `EndfieldHUD.exe` and no `WebView2Loader.dll` is installed. `LICENSE.txt` and `NOTICE.txt` are kept beside it.
- Release objects build with `/Z7` and `EndfieldHUD.exe` links with `/DEBUG:FULL /OPT:REF /OPT:ICF`. The separate developer symbols zip therefore contains the PDB, code generation is unchanged, and the consumer package never contains it.
- `EndfieldHUD.exe --data-root <absolute folder> [--toggle]` exists only for the section 9 performance gate. Such a run uses its own single-instance names, writes no startup entry or restart registration and never touches the notification identity.

## 11. Incremental implementation plan

1. **Feasibility:** inventory laptop/toolchain; establish a minimal native target and prove the five architecture checks in section 3. Commit no app-data changes.
2. **Shell and scene:** source loader/materials, exact open/close/tilt, navigation/scroll, clock/profile shell, tray/hotkey, monitor selection and cursor. Establish closed-idle measurements before adding modules.
3. **Data and editing:** versioned codecs/import plus projected input/IME; synthetic migration fixtures; Notes, Shelf, Clipboard, Archive and profile sync locks. Preserve Mac data untouched.
4. **Local modules:** Reader, Media Assembly, Projection, Map, Calendar, Event Log, app shortcuts and the offline minigame. Validate module transitions, cache bounds and parked inactive work.
5. **System providers:** battery/device popup, Storage/Activity Monitor, endpoint/per-app audio, Now Playing and Work Mode. Label platform capability gaps; no fake parity.
6. **Accounts:** transient official China login first, byte-identical signing tests, sanity/profile sync; then live Global verification. Reauthenticate instead of transferring Mac secrets.
7. **Polish and packaging:** full matrix, profile regressions, recordable cursor, accessibility and safe updates/install/uninstall. Ship only after the target laptop passes the applicable gates.

At each step report exactly what is implemented and verified, what remains unverified, and what is deferred and why. Keep a concise Windows progress log beside this handoff as implementation starts; list measured regressions and decisions. Do not reduce the original requirements to fit a milestone.

### Current unresolved items

Status at checkpoint 123 (2026-10-10). "Hidden tests" means automated runs over synthetic data in temporary roots; "live" means the user trying it on the laptop desktop.

| Item | Current status / required decision |
| --- | --- |
| `EndfieldHUD.exe` live acceptance | Every module is in the executable and passes hidden owner coverage, but no visible run has been recorded. Still needed: first launch, persistence and relaunch on the real data root, the quit card, session end, damaged package resources, sleep/lock/hotkey, launch at login, the icon and version, and each area's live list in the [build record](windows/README.md). |
| Compositor/backdrop | User verified live darkening/blur, card highlight and navigation scroll/drag in the isolated preview. Recording, cursor and capture behaviour still need laptop verification. |
| Projected input | The user verified Chinese typing and selection in the tilted editor. Cut/copy/paste, word navigation and multi-click selection pass hidden tests only. Japanese/Korean candidates, the real clipboard and accessibility remain unverified. |
| Notes / Archive / Projection | In the executable with chunked long documents, clipboard, word navigation, cards of any size, stores up to 1 GiB, HUD drops and relink of imported media. Live acceptance is pending, including per-codec video. Open: formatting of foreign RTF/HTML pastes, and undo across a short-to-chunked promotion. |
| File Shelf | Connected (picker, drops, copy-only drag-out with Shell drag images, generic and handler preview, identity-based stale-path renewal, Event Log records). Live desktop acceptance is pending. Open: containment of a hung third-party preview handler and cloud placeholder files. |
| Direct tray drop | Recorded platform gap (section 6, "Windows platform gaps"). Fallback: open the HUD and drop on the Shelf, *Add files*, or Ctrl+V. |
| UI Automation | The HUD window has one UIA root, and Volume is listed under it. Now Playing, Media Assembly and Account have fragments but must implement the `ModuleOwner` hooks. Reader, Map, Calendar and minigame descriptors need projection and invoke APIs. Notes, Archive, Projection, Shelf, the Power editor and the navigation entries have no provider. Narrator and Inspect have not been tried. |
| System providers | Wired by the host: local Event Log times (486 Mac oracle cases), clipboard copy events and statuses, Activity history, icons and sleep/lock handling. Real icons, packaged names and the 5 s / 1 Hz cadence need live checks. Gaps are in section 6. |
| Shell recovery | Display hot-plug, DPI changes and GPU device loss are handled in code (save, then a fresh owner on the last module, at most 3 times a minute). None has run: device loss has no test hook, and an adapter switch is detected only once the device is removed. Live checks: driver reset (Win+Ctrl+Shift+B), mixed-DPI hot-plug, recording. |
| Area follow-ups | The Power badge still composes above pinned Notes (`floatingEntries()` instead of `planeEntries()`). The profile area does not yet request the single-file image filter. App shortcuts has no drop or paste, no packaged-app choice or logos, and no original icons in the navigation. |
| Settings and cursor | Missing: the custom accent colour picker, center-logo import and rendering, and clickable clock style indicators. The cursor does not yet change to an I-beam over editors or to resize cursors over Notes grips. |
| Mac data import | Hidden tests pass for the importer, the card and a synthetic restart handoff. Unverified: the production restart, an export of real data and the real root. Modules do not update `Migration/relink.json` after relinking, and Reader books and Shelf items have no relink action. A killed post-shutdown commit leaves its `<root>.import-*` folder unswept. |
| OS Focus/DND | Restricted OS capability (Limited Access Feature). Not wired; the Work Mode timer, completion and lifetime hours do not depend on it. |
| Media providers/lyrics/routing | Now Playing (GSMTC, lyrics, per-app volume) and Volume (WASAPI through the shared `AudioRuntime`) ran only against fixtures and injected backends. Live matrix still needed: NetEase, QQ Music, Kugou, Spotify and local players, exclusive-mode streams, real per-app volume, Media Assembly playback and each machine's codecs. |
| Account Linking | China and Global login, roles, sanity and profile sync have never run against the real service; tests must not. Needs the installed Evergreen WebView2 runtime, and only the x64 loader is vendored. Live Global testing is outstanding on both platforms. |
| Calendar reminders | Need the installer's notification identity. Real toast scheduling, delivery and click activation are unverified. |
| Performance (section 9) | No measurements of `EndfieldHUD.exe` exist yet. `package_perf_gate` and `--data-root` are ready; GPU counters need the interactive console session, and frame pacing and latency need an ETW recorder (WPR/PresentMon). |
| Installer/updater/signing | Install, upgrade, rollback and uninstall pass isolated tests; the interactive windows and SmartScreen are unverified. Builds are unsigned (no certificate). No updater or Windows feed exists. The CI workflow has not run on GitHub. The package does not yet ship the QuickJS-NG and WebView2 SDK licence notices compiled into `EndfieldHUD.exe`. |
| Developer builds | The portable no-ICU configure still fails at the Now Playing targets. |

Suggested first prompt for Codex on the Windows laptop:

> Read WINDOWS-MIGRATION.md and the current source. Start milestone 1 only, then proceed incrementally after its feasibility checks pass. Preserve the existing EndfieldHUD functions, visuals, animations, data formats and optimizations. Keep the Mac app unchanged. Use isolated synthetic data; do not access my real app data or accounts without a specific test request. Measure CPU, RAM, GPU activity and cold/warm opening latency. Report implemented/verified, unverified, and deferred items separately. Use DDDuoDuo's Git identity and do not publish a release yet.

## 12. Recoverable old branches

The four old branch heads were saved as annotated tags, verified on GitHub, then removed from the active branch list. Their history and original-game resources remain available.

| Archived tag | Preserved commit |
| --- | --- |
| `archive/codex/endfield-hud-integration` | `d49f64fb5b9a24f2d45d9c205596cec6f0c18876` |
| `archive/codex/endfield-watch-motion` | `b4825738f55fa5b5cf2e28af61e6647162756853` |
| `archive/codex/performance-parity` | `baa40c99041927c5b29793b0f2280bec5fe476ad` |
| `archive/codex/roadmap-ui-polish` | `5859a1248e2c8dfe3214a82838ec1c450e027f7f` |

Browse the original interface through [its archive tag](https://github.com/DDDuoDuo/EndfieldHUD/tree/archive/codex/endfield-watch-motion). To inspect an old branch locally without changing this branch:

```powershell
git worktree add ..\EndfieldHUD-watch-reference --detach archive/codex/endfield-watch-motion
```

To restore an active branch later, create a branch from its archive tag and push it only when requested. No history was rewritten and no release tags were removed.
