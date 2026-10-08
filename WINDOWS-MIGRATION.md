# EndfieldHUD — Windows migration handoff

This is the working handoff for DDDuoDuo's Windows laptop. It describes a native Windows port of the current macOS app. **The previous Windows preview was rejected. A fresh implementation is underway; no complete Windows app or stable release is verified yet.**

The fresh implementation uses macOS `main` at `ca04f142185c7de40acd8523bdb563195d90a1d1` (v1.2.0 build 18). [`windows/source-authority.json`](windows/source-authority.json) pins its source and resource trees. Do not restore the rejected Windows renderer or use the raw game scene as the application reference. The old preview is preserved in Git history and a local backup, outside the new build. See [`windows/README.md`](windows/README.md) for current build scope.

Keep the current HUD, functions, animations, saved data and optimizations. Replace the platform plumbing. Do not redesign the interface or silently remove a module because its Windows adapter is difficult.

Automated checkpoint: [CI run 37723655009](https://github.com/DDDuoDuo/EndfieldHUD/actions/runs/37723655009) passed 27 portable suites and 35 Windows suites at `7695dc0`. The current Notes integration checkpoint passes 34 portable suites and all 52 Windows suites in a Release build on the laptop, including retained Notes cards, shared projected editing, isolated save/reopen and transition clipping. Original Mac fixtures also verify 286 Notes presentation checks and 1,680 Core Animation transform samples. The user verified the updated backdrop, corrected card hover, faster scrolling and navigation dragging. Recording/capture remains unverified. The source-shell preview and isolated editor are development tools; module bodies are not yet installed in the shell. This is not a complete Windows app or release acceptance result.

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

Notifications and launch-at-login require packaging/identity decisions: see [app notifications](https://learn.microsoft.com/en-us/windows/apps/develop/notifications/app-notifications/) and [StartupTask](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.startuptask?view=winrt-26100). Respect settings disabled by the user. Start no duplicate watcher for the same OS event.

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

| Item | Current status / required decision |
| --- | --- |
| Compositor/backdrop | Native integration builds and passes automated tests. User verified live darkening/blur, card highlight and navigation scroll/drag. Recording/cursor and capture behavior still need laptop verification. |
| Projected input | Retained DirectWrite/TSF fixtures pass; the user verified Chinese typing and selection in the tilted editor. Japanese/Korean candidates, accessibility and full-module editing remain unverified. |
| Notes / Shelf | Plain Notes is connected to the isolated shell with native input, per-card transitions and retained artwork; live module acceptance is pending. Shelf state/persistence/file-access foundations exist; its UI and transfers remain in progress. |
| Direct tray drop | No verified equivalent for the Mac menu-bar drop target. Prototype before promising. |
| OS Focus/DND | Restricted OS capability; timer parity required, system integration may be deferred. |
| Media providers/lyrics/routing | Provider and codec dependent; test available Windows apps and APIs. |
| Global account | Source support exists; live Global testing outstanding on both this handoff and the future Windows client. |
| Architecture/installer/updater | Native builds and automated tests pass. A complete app, installer/updater, signing and full-app performance remain unverified. |

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
