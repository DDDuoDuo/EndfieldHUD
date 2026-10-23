# EndfieldHUD for Mac — changelog

User-facing changes to the macOS app, newest first. The Windows version keeps
its own changelog on its development branch.

## Unreleased

### Redesigned centre

- **Every centre module is rebuilt in the Endfield style**: one shared set of
  components (headers, rows, toggles, menus, sliders, buttons, tabs, cards,
  confirm bar, toasts) drawn with HarmonyOS Sans for Latin letters and digits
  and Noto Sans SC / TC / JP / KR for Chinese, Japanese and Korean. Motion is
  smooth and finite, hover eases in and out, and Reduce Motion (system or in
  the app) settles everything at once.
- **Theme colour and Light theme**: the redesign follows the theme colour
  (the default colour looks as before) and has its own Light variant; the
  System theme follows macOS.
- **English shows English only**: no Chinese subtitles, captions or toast
  lines in English. In other languages the bracketed hints show the English
  title with the translation in small grey text below.
- **Text fields**: the caret blinks, disappears when a field loses focus, and
  right-click, Cut / Copy / Paste / Select All and Undo work in every field.
- **Scroll bars** appear while scrolling or hovered and hide when idle.
- **Confirmations** blur and darken the content behind them; facility alerts
  get a red confirmation before they are switched off.
- **Large displays**: the whole centre grows with the display, as on Windows.

### Shell

- **Top-right clock**: five redesigned compact styles that switch with the
  vertical mouse wheel too; a status row always shows WORK MODE and RECORDING
  MODE, lit while active and grey otherwise.
- **Sanity bar**: in the Work Mode display it highlights on hover and opens
  Work Mode; clicks never close the HUD; the recovery menu fits every language.
- **Side buttons**: icons share one visible size on each side, with one gap
  between icon and caption on the left.
- **Quit** asks on the shared confirm bar.
- **Recording mode** hides personal details (UID, notes, clipboard, shelf)
  behind redaction bars.
- **Daily boot log** (optional): a short terminal summary of the day the first
  time the HUD opens each day.
- **Facility alerts** (optional): low disk space, high memory pressure and
  overheating raise a banner and a tag that open the related module.

### Modules

- **Notes**: TEXT and TODO tabs. TEXT has per-selection colour (presets or a
  colour wheel), strikethrough and ⌘B / ⌘I / ⌘U, and keeps editing while you
  format. TODO items can be edited, reordered by dragging and flagged with an
  orange dot (shown on the tab while any item has one). The draggable desktop
  notes are restyled with the same controls and colour picker.
- **File Shelf**: an eye button opens a properties card; clicking empty space
  deselects; removing an item says "Removed from shelf".
- **Clipboard**: delete single entries; pinned entries stay on top and cannot
  be deleted.
- **Archive**: classified documents with a password, editable categories,
  colour and strikethrough, and more room for text and media.
- **Media Assembly**: closing a file asks first; one new filter (Wuling
  Impression) and six new stickers.
- **Minigame**: a "How to play" panel, a centred board and a narrower side
  column.
- **Now Playing**: a bigger album cover; sharper covers; NetEase lyrics with
  [mm:ss:cc] timestamps play in time, and failed lyric lookups retry.
- **Volume**: three app rows with smooth scrolling and dragging; the orange
  dot shows only when an app's volume can be adjusted.
- **Projection**: a custom pen colour and a restyled desktop toolbar.
- **E-Reader**: reopens in the book you were reading, gives the page much more
  room, turns only the page in paged mode and scrolls continuously in vertical
  mode; page turns are no longer written to the Event Log.
- **Calendar**: click the month to pick a year and month.
- **Map**: fills the whole centre disc, like the Personal ID background.
- **Personal ID**: the OrbiPom! MERGE! best score under Work Mode hours; the
  "…" button and quotation marks are refined.
- **Activity Monitor**: app icons; GPU load in Overview and Apps, with history.
- **Storage**: choose the disk to show.
- **Battery alert**: "Don't display" and a GPU reading.
- **App Shortcuts and Center logo**: choose icons and logos from an icon grid;
  redesigned icons for apps without an official one.
- **Settings**: a Modules page to reorder and show or hide the right-hand
  modules; hotkey opening can be switched off; Battery alerts moved to the
  Battery alert page; clock style names show in full.

### Performance

- Idle CPU is unchanged, memory per open module is 10–17 MiB lower, pointer
  motion uses less CPU and reopening the HUD is no slower than in 1.2.0.
