# macOS data import

This folder implements the one-time offline import from WINDOWS-MIGRATION.md section 7.
The Mac app has no export feature. Exporting is a separate script, and the
Windows side reads only the folder that script produces.

## 1. Export on the Mac

```sh
python3 windows/tools/mac_import_exporter.py ~/Desktop/EndfieldHUD-export
```

The script refuses to run while `EndfieldHUD` is running, when the output
folder already exists, or when a SQLite database has an active `-journal` or
`-wal` file. Databases are copied with SQLite's backup API, and each copy must
pass `integrity_check`. JSON stores and managed images are copied without
changes. Only names in the Mac's managed-image format, `<UUID>.png` or
`<UUID>.image`, are exported. Preferences come from
`defaults export io.github.endfieldcharge.EndfieldCharge`. The script never
reads the Keychain. Everything is written into a private
`.<name>.partial-<random>` folder next to the output and renamed into place
only when complete; a failed export removes it, so a half-written export never
exists. The output folder uses mode 0700 and its files use 0600.

`manifest.json` has this shape:

```json
{"format":"EndfieldHUD.macExport","version":1,"exportedAt":<unix>,
 "source":{"bundleIdentifier":"io.github.endfieldcharge.EndfieldCharge","appVersion":"1.2.0","build":"18"},
 "files":[{"path":"EndfieldCharge/Notes/notes.sqlite3","bytes":12288,"sha256":"<hex>"}, ...],
 "sqlite":{"EndfieldCharge/Notes/notes.sqlite3":{"userVersion":2}},
 "preferences":"Preferences/io.github.endfieldcharge.EndfieldCharge.plist"}
```

## 2. Import on Windows (`MacImportSession`, `MacImportService`)

All work runs in bounded steps on the shared utility worker: at most
`stepBytes` (8 MiB) verified or copied per step and `stepImages` (16) managed
note images checked per step. A single SQLite `integrity_check` or one store's
codec load is one step. `progress()` reports the phase, verified/export bytes,
validated stores and carried bytes.

### Staging (the app keeps running)

1. **Open.** The importer validates the export folder, the manifest and the
   destination root. A second import is refused unless the user chooses
   replace (`replaceExistingImport`). The existing data root is walked once:
   any link or junction in it fails the import, and the free space next to it
   must hold the export copies plus the Windows data that will be carried,
   plus a 64 MiB margin.
2. **Verify the whole export.** Every listed file is checked against its
   manifest size and SHA-256, including files no store reads (those are only
   hashed in place). Files a store may read are copied into a private
   `<root>.import-<uuid>` folder on the same volume while they are hashed. A
   missing, extra-long, tampered, linked or locked file fails the whole import
   before any store is validated, and nothing is left behind. The folder is
   created exclusively and marked before anything else is written (see
   "Private work folders" below).
3. **Validate each store.** Each store's existing Windows codec validates the
   private copy, and migrations run only on that copy:

   | Store | Validation | Result |
   | --- | --- | --- |
   | Settings | Binary or XML plist, then `mapMacSettings` (`ConfigurationStore.init` oracle) | `settings.json`. Windows-only keys from an existing install are kept, and the higher OrbiPom score wins. The original plist is archived in `Migration/` |
   | Notes | `integrity_check`, newer than v2 rejected, then `NotesStore` (migrates v0/v1) | Managed PNGs are copied in bounded batches. A missing, oversized or undecodable image is reported and its note keeps the reference. Mac media is added to the relink list |
   | Archive | `integrity_check`, newer than v2 rejected, then `ArchiveSQLiteRepository`, decoding every body | Mac attachments are added to the relink list |
   | Profile | `ProfileStore`, only when `profile.json` exists | Avatar `.image` and background `.png` bytes are copied unchanged. WIC only checks them |
   | FileShelf | `FileShelfStore.decode` rules | Nothing is written. Each item goes to the relink list with its full record |
   | Reader, Calendar, WorldMap, AppShortcuts, EventLog | Their Windows repositories (map v1–3 migrate to v4) | Mac books and apps are added to the relink list |
   | Account | `HypergryphAccountController.Cache` Codable rules | Linked regions get `requiresReconnect=true`. Credential-like keys are removed. `profileSyncLocked` is reported. The file is written with inherited permissions; the account services restrict it to the current user in place, without changing its bytes, before their first write |
   | CenterLogo | PNG ≤ 4 MiB and ≤ 768 px, plus WIC | Only the selected revision is copied. A missing file is reported, and Settings stays "custom", as on the Mac |

   A store whose schema is newer, or whose data is malformed, is *rejected*:
   it writes nothing. While any store is rejected, commit is refused unless
   the user explicitly accepts skipping it (`acceptRejected`).

### Commit (after the owners closed their stores)

Before calling `commit()`/`commitNext()`, the owner must flush and close every
module store. The commit then works from the root *as it is now*, so nothing
saved while the summary was being reviewed is lost:

1. **Refresh.** Windows-only settings (display, hotkey, OrbiPom best score,
   first-run marker) are merged again from the current `settings.json`.
   Entries of an earlier `Migration/relink.json` are kept, with their
   decisions, for stores this import does not replace (for example Mac shelf
   items when the new export has no shelf, or the books of a carried Reader
   library). A root that appeared during the review is backed up like any
   other.
2. **Carry.** Existing data the import does not replace (stores absent from the
   export or rejected, Windows-only folders and the Windows shelf) is copied
   into the new root in bounded steps. A file that changes size now is a
   conflict.
3. **Report.** `Migration/import.json`, `Migration/relink.json` and the
   manifest are written next to the archived plist.
4. **Activate.** The current root is renamed to `<root>.backup-<UTC>-<id>`,
   and the new root is renamed to `<root>`. Both renames use `MoveFileExW`
   with write-through and never replace an existing target. If the second
   rename fails, the backup is renamed back. The importer never deletes a
   backup. A file left open makes a rename fail with `conflict`, and the root
   is left exactly as it was.

Cancellation is honoured until activation; any failure before it removes the
private folder and changes nothing. `commitMacImportSession` records the
outcome note as soon as the new root is in place, before the private copies
are removed.

### Private work folders (`mac_import_work.hpp`)

```
<parent>\<root>.import-<uuid>\
    import-work.json   marker: {"format":"EndfieldHUD.macImportWork","version":1,
                       "root":"v1","token":"<uuid>","createdAt":<unix>}
    source\  root\     verified export copies, staged root
    activation.json    {"format":"EndfieldHUD.macImportActivation","version":1,
                       "root":"v1","backup":"v1.backup-<UTC>-<id>"|null}
```

`MacImportWorkLock` creates the folder exclusively and then the marker, and
keeps the marker open for the session's whole life (Windows: `FILE_SHARE_READ`
only; POSIX: `flock`). The activation journal is written and flushed right
before the first rename. A session removes its own folder with the marker
last; if something is in use, the marker stays so a later sweep can finish.

A process that ends while it owns a folder (killed while staging, or during
the post-shutdown commit of the restart handoff) leaves the folder behind.
`MacImportWorkSweep` removes such folders in bounded steps (256 file system
operations per step on the shared executor), and only folders that are
provably the importer's own and abandoned:

- the name is exactly `<root>.import-<token>` next to the root, and the folder
  is an ordinary directory, never a link or junction;
- the marker is an ordinary file naming that root and token;
- no live owner holds the marker, so a running import in any process is never
  touched;
- every top-level entry is one the importer writes. Anything else, such as a
  file a user put there, keeps the whole folder.

An empty folder with such a name holds nothing and is removed non-recursively.
Backups, the outcome note, the data root, other roots' folders and anything
unmarked are never touched. Links inside a swept folder are removed as entries
and never followed. Folders made before checkpoint 124 carry no marker and are
therefore never swept.

| State | Meaning | Sweep |
| --- | --- | --- |
| abandoned | no journal, or the journal's backup never appeared, or the journal is unreadable | removed |
| activated | journal present and the staged root already moved into place | removed (only private copies remain) |
| interrupted | journal present, the root is at its backup, the staged root is still private | kept |
| live | a session holds the marker | kept |
| unrecognized | unmarked, foreign marker or unknown entries | kept |

`recoverMacImportWork(root)` runs before any owner opens the root (the host's
startup, under its single-instance guard). For every interrupted activation
whose root is absent, it moves the backup back with a no-replace rename and
records a failed outcome note ("…the previous data was restored…"), so the
next import card explains what happened. The folder is then an ordinary
abandoned one. Recovery never replaces an existing root: if a run already
recreated the root, the interrupted folder and the backup are left for the
user. Afterwards, the owners reload from the root
and apply the summary: `profileSyncLocked`, `launchAtLogin` (the Windows
startup provider), `remindersNeedReconcile` (Calendar notifications), and
`customShortcutUntranslated`.

## 2a. The "Import macOS data…" card (`macImport` area)

The Mac app has no import, so the Windows entry point reuses the HUD
confirmation style (the red power button card: same plate, header line,
caption, buttons, .16/.20 s reveal, .14 s dismissal, Cancel focused first)
with a list and a progress bar added (`app/areas/mac_import_card.hpp`). The
flow (`app/areas/mac_import_flow.hpp`, five HUD languages) is:

1. **Entry.** The area publishes `app::MacImportEntry`
   (`app/areas/mac_import_handoff.hpp`); the Settings System row
   "Import macOS data…" calls `present()`.
2. **Folder.** The host's shared file dialog. Choosing `manifest.json`
   means its folder.
3. **Staging.** `MacImportService` on the shared UtilityExecutor; the card
   shows verified megabytes, then each data set as it is checked. Cancel
   discards the private folder on the worker.
4. **Replace.** An earlier import (`MacImportErrorCode::alreadyImported`)
   asks for "Import Again" (`replaceExistingImport`, a new backup).
5. **Review.** One row per data set (ready, relink count, not in export,
   newer/unreadable and skipped). A rejected store is skipped only through the
   explicit "Skip and Import" (`MacImportService::acceptRejected`, no second
   staging).
6. **Commit.** Production hands the reviewed session to the host's
   `app::DataRootRestart` (`MacImportService::handOff`). The host runs the
   accepted-quit path, which flushes and closes every owner's store, then
   runs the commit on the main thread while nothing under the data root is
   open (`commitMacImportSession`: backup, same-volume switch, rollback on
   failure) and starts a fresh run over the same root, so every owner reopens
   the imported data and applies its post-import intents on load (Calendar
   reconcile, the profile lock from the account cache, launch at login). A
   save failure cancels the quit: the review stays and can be confirmed again
   or cancelled. Since checkpoint 123 the host publishes `DataRootRestart`
   in production only (`app/application.cpp`, restart loop in
   `app/main.cpp`), and the fresh run opens on System. Development previews
   and hidden coverage do not publish it, so there the review cannot be
   confirmed through the host; coverage plays the host's role itself.
   Since checkpoint 124 the host's own restart members are also checked in
   hidden coverage, behind a verification-only gate. A failed save destroys
   the pending work unrun, and its decline runs once. A restart pending at
   the confirmed quit is handed out exactly once.
7. **Report.** The commit records `<root>.mac-import-outcome.json` next to
   the root (statuses, counts, backup folder, the importer's error text; no
   record content). The next run's area shows it once (final report, then the
   relink list read from `Migration/relink.json`) and removes it on Done.
8. **Startup sweep.** Each run's area starts a `MacImportWorkSweep` of the
   root's stale private folders on the shared executor (bounded steps), before
   it reads the outcome note.
9. **UI Automation.** The card is one dialog (Pane, IsDialog, named by the
   title, help: the message) under the area's group of the HUD window's UIA
   root (`native/mac_import_automation.hpp`). Its parts are the title, the
   message, a read-only ProgressBar (RangeValue 0–100) while a bounded job
   runs, the detail text, a List whose ListItems are every row of the list
   ("label: value"; rows scrolled out of the card are offscreen), and the two
   Buttons. Invoke is the click's action and disabled buttons refuse it.
   HasKeyboardFocus follows the card's own focus (a disabled button never
   reports it), so the host's GetFocus finds the focused button. Whenever
   that focus moves to another part - the card appearing with Cancel
   focused, Tab, Left/Right, a press, SetFocus - the area raises
   `UIA_AutomationFocusChangedEventId` on the newly focused button for
   listening clients, once (the host never raises it for an owner). UIA
   SetFocus on a button moves the card's focus exactly like Tab; the dialog
   keeps its button's focus and text, list, rows and the progress bar are
   not keyboard focusable. The dialog is listed on the card's first frame
   (its parts need the card's pose) and its bounds are re-read once the
   reveal settles, so it appears without waiting for another content event.
   Parts keep their COM identity per id across steps and refresh on content
   events only.

10. **Residency.** Nothing of an import stays in memory once its card is
   hidden (WINDOWS-MIGRATION.md section 9):
   - `MacImportService` destroys the session on the worker with the step that
     ends the import (the last commit step, a discard, any failure); only the
     results travel back. `handOff()` keeps no copy of the review, and
     `release()` returns a finished service to idle without its summary,
     result, error text or progress (refused while staging, reviewed or
     committing).
   - `MacImportSession` returns what no later step reads as soon as it can:
     after staging the manifest's file list, the verification and note-image
     lists and the preferences bytes; after the report step the verbatim
     manifest; after the commit or a discard everything except its summary,
     progress and result.
   - The flow releases the service as soon as the final report or the
     failure card has taken its outcome, and on hiding replaces its rows,
     relink entries, notice, detail and export path with empty values
     (`MacImportFlow::residency()` reports all of them;
     `macImportSessionsAlive()` counts sessions in the process).
   - The area creates the card's rasters only while the card is presented
     and releases them at the first frame without it; its UI Automation
     parts are disconnected when the card hides. The import starts no thread:
     staging, discards, the sweep and the outcome note run on the shared
     UtilityExecutor, and the WIC check of managed images initializes COM
     per call on that worker.
   A card shown when the HUD closes keeps its artwork for the next opening,
   where it appears again, like the shown module's.

Hidden coverage imports the golden export into the area's own synthetic
destination, in-process (cycle 0) and through the production restart path
(cycles 1 and 2). The host publishes `DataRootRestart` in production only, so
in hidden coverage the area publishes a stand-in under the same key, and the
area's production code (`find`, `restartWith`) runs unchanged. The driver then
plays the host's answers: a refused second request, a failed save (the work is
destroyed unrun, then `declined` runs), the accepted quit (the owner's flow is
torn down first and the work runs once), and a session end while the restart
is pending (the work never runs). The next run sweeps planted stale folders
and shows the recorded report. The host's pre-open recovery is exercised on a
second synthetic root.

## 3. Relinking

Mac bookmarks, security scopes and inode/volume identities cannot authorize
Windows files. `MacRelinkLedger` (`Migration/relink.json`) lists every imported
external reference. Entries change only through explicit user decisions:
`markResolved` with a path chosen in a picker, or `dismiss`. Nothing searches by
path or name. The pure transformations `relinkedReaderBook`,
`relinkedNoteMedia`, `relinkedArchiveMedia` (native), `relinkShortcut`
(native, through the App Shortcuts editor transaction) and `ShelfRelinkSlot`
(the shelf owner's `FileShelfStore::Creation`) keep each record's id, order,
progress, bookmarks and unknown fields, and replace only the locator.

A resolved entry records an explicit Windows target
(`validMacRelinkTarget`): an absolute Windows file path for every store,
and for `appShortcuts.app` also a packaged application chosen in the
applications dialog, `shell:AppsFolder\<PackageFamilyName>!<ApplicationId>`
(ASCII package identity characters, at most 130 characters).

### Owner API (by store and record id)

Owners never need a ledger id. After their own record write committed
(record first, ledger second) they call the API below. The Shelf calls it on
the shared UtilityExecutor, App shortcuts on their worker, Archive on its
FIFO file queue behind the entry save, and Reader in the library's worker
job. Notes queues its relink and deletion calls, in order, on the shared
UtilityExecutor that the host hands it (since checkpoint 129; a full queue
runs them inline), right after its synchronous store write; its reconcile
runs once when the owner is constructed.

| Call | When | Rows it changes |
|---|---|---|
| `resolveMacRelink(appRoot, key, target)` | the user picked `target` in the owner's own picker and the relinked record was saved | the key's unresolved rows become resolved with `target` |
| `dismissMacRelink(appRoot, key)` | the user removed the record or attachment | the key's unresolved rows become dismissed |
| `reconcileMacRelinks(appRoot, store, check)` | on load | every unresolved row of `store` through the owner's own lookup (`MacRelinkLedger::reconcile`) |

`MacRelinkKey` is `{store, recordID, macReference, index}`:

| Owner | Key |
|---|---|
| Notes | `{relinkNotesMedia, note.id}` |
| Archive | `{relinkArchiveMedia, entry.id, &theAttachmentsMacObject}`; `{relinkArchiveMedia, entry.id}` for a deleted entry |
| Reader | `{relinkReaderBook, book.id}` |
| App shortcuts | `{relinkAppShortcut, shortcut.id}` |
| File Shelf | `{relinkShelfItem, record id}` |

Record ids match in any letter case. An Archive attachment is named by its
Mac locator (bookmark and last known path), which stays valid when earlier
attachments are removed; `index` (the import position) is the fallback, and
a key with neither names every row of the record. Two attachments of one
entry made from the same Mac file share one locator and resolve together.

Every call reads the ledger afresh and writes it at most once, atomically.
Without a ledger (no Mac import), or without a matching unresolved row,
nothing is created or written, so owners call unconditionally. Calls are
serialized within the process (the App shortcuts worker, the shelf and the
UI thread share one file); a ledger replaced by another writer between the
read and the write is read again and the change re-applied, up to four
times, after which `changedOnDisk` surfaces. Resolved and dismissed rows are
never changed again. Invalid stores, record ids and targets are refused
before anything is read; unreadable or newer ledgers throw and are kept byte
for byte.

`macRelinkMediaCheck(row, media)` is the shared lookup rule for media rows
of a record that still exists (the owner answers `missing` for a deleted
record): an object with the row's Mac locator keeps it listed; a note whose
one payload is a Windows reference resolves with its `windowsPath`; a note
without media is dismissed; an attachment whose Mac locator is gone was
relinked or removed while the ledger write was lost and is dismissed. A
Windows attachment is never attributed to a row by position, because
positions shift when attachments are removed.

**Status (checkpoint 128).** The ledger, the owner API and the transforms
are tested (`mac_import_relink_contracts`, `mac_import_contracts`,
`mac_import_native_contracts`), including concurrent writers in one process
and another writer replacing the file mid-call. Every owner of imported
records now calls the owner API:

- The File Shelf lists unresolved `fileShelf.item` entries as unavailable
  cards (double-click, Space or *Relink* opens the picker). A relink keeps
  the imported identifier and date through `ShelfRelinkSlot` and resolves
  the row; Remove and Clear dismiss; refresh reconciles. All ledger work runs
  on the utility worker (`shelf_services_contracts`).
- App shortcuts follow the committed `shortcuts.json` on their worker
  (`syncShortcutRelinks` through `reconcileMacRelinks`); a shortcut relinked
  to a packaged application resolves its row with its AppsFolder target
  (`app_shortcut_relink_contracts`).
- Notes (footer action) resolves a media note's row after a relink,
  dismisses a deleted note's row and reconciles when the persistent root
  loads (`modules/notes_relink_ledger`, `notes_relink_ledger_contracts`).
  Since checkpoint 129 the resolve and dismiss writes run on the shared
  executor (`notes_preview_contracts` queues a deletion's write on a real
  executor).
- Archive (the attachment's error text) saves the entry first, then resolves
  or dismisses on the same file queue only when the stored entry shows the
  change; deleting an entry dismisses all its rows, and the first load
  reconciles once (`archive_state_contracts`, `archive_preview_contracts`).
- Reader books are relinked from the Library (*Relink…*, for imported Mac
  books and unavailable Windows books). The library is saved first, then the
  book's row is resolved; removing an imported book dismisses its row, and
  loading reconciles. A ledger failure never fails the library
  (`reader_owner_contracts`, `reader_localization_contracts`).

All of this ran over synthetic ledgers in temporary folders; no real Mac
export has reached these owners.

## 3a. Calendar reminders

`beforeScheduledFor`/`dayScheduledFor` are receipts from the macOS
notification center. The reminder reconcile (on the Mac and on Windows) never
replays a reminder that has a receipt but is missing from the pending OS
schedule. Imported receipts would therefore silently drop every future
reminder on Windows. The import clears them, keeps every other field, and
reports `remindersNeedReconcile`. The Calendar owner's normal reconcile then
schedules each future reminder exactly once. `mac_import_reconcile_contracts`
checks this through the unchanged `calendarReminderPlan` and
`reconcileCalendarNotifications`, using an in-memory provider.

## 4. Oracles and tests

`windows/tools/mac_import_reference.py` runs Apple's PropertyListSerialization,
the unchanged `ConfigurationStore`/`OrbiPomSession`, and every Mac store in a
private `CFFIXED_USER_HOME`. It writes:

- `plist_mac_import_source.json`
- `mac_import_settings_source.json`
- `mac_import_golden_export.json`, which contains synthetic records written by
  the Mac stores and then exported by the exporter script.

`mac_import_contracts` also imports every case of the Swift-generated
`map-store-source.json` (WorldMapStore v1–v5 and malformed archives). Each
accepted map must reach the exact state the Mac writes back after migrating it.

`mac_import_flow_contracts` drives the flow, the card state/artwork and the
outcome note over synthetic exports (staging progress, review, consent,
in-process commit, restart handoff accepted/declined/discarded, the next
run's report and relink list, failures, five languages).

`mac_import_resident_contracts` counts every heap block of its process
(replaced global operator new/delete, usable sizes) and its threads around
seven scenarios over a synthetic export with 512 extra listed files: a
session staged and discarded on its own, then through the flow a completed
in-process import (review, report, relink list, Done), a cancelled review,
a failed staging, a production handoff that the host declines before
Cancel, the next run's recorded report, and the replace question. After a
warm-up run, each scenario must return the heap to the hidden flow's own
allocation (at most 512 bytes of allocator rounding), leave no session
alive and start no thread. Blocks allocated in a scenario and still alive
at its end are listed with their size and contents when it fails; that is
how six SQLite sidecar paths per staging, left alive by MSVC x64 Release
from a temporary inside a `&&` operand (`rejectJournals`), were found.

`mac_import_work_contracts` covers the marker and journal codecs, the lock (a
live folder is never swept, even in-process), classification of 13 planted
folders, a bounded sweep of a deep tree, links that are never followed, the
pre-open recovery, and the session's journal between the two renames.
`mac_import_automation_contracts` (Windows) covers the card's UIA tree over
synthetic nodes, including the focus events (the appearing card, moves,
disabled buttons, a re-shown card) and SetFocus routing.
`mac_import_relink_contracts` covers the relink ledger's owner API:
explicit targets per store (file paths, AppsFolder targets for App
shortcuts only), resolve/dismiss/reconcile by store and record id, the
media lookup rule (a re-encoded Mac object keeps its row; no Windows
attachment is attributed by position), refusals before any write,
unreadable and newer ledgers kept byte for byte, a ledger replaced
mid-call (re-read and re-applied, bounded) and concurrent owners on two
threads.

## 4a. Verification status (checkpoint 124)

Verified on the laptop by hidden tests only:

- `mac_import_work_contracts`: 846 checks; also passes on the Mac under
  clang.
- `mac_import_contracts`: 753 checks.
- `mac_import_flow_contracts`: 295 checks, including the card geometry that
  the artwork and UI Automation share.
- `mac_import_native_contracts`: 251 checks.
- `mac_import_automation_contracts`: 69 checks.
- `mac_import_reconcile_contracts`: 11 checks.
- The plist, settings, tool and exporter suites.
- The `macImport` area in `application_owner_coverage`: 1,021 checks over
  three cycles. Cycle 0 runs an in-process golden import and walks the card's
  UIA tree. Cycles 1 and 2 run the area's production restart path against a
  stand-in `DataRootRestart`: a refused second request, a declined save, the
  accepted quit, and a session end that never runs the work. They also cover
  the next run's sweep of planted folders and its report, and the pre-open
  recovery on a second synthetic root.

After checkpoint 127 (relink owner API and focus events), on the laptop:

- `mac_import_relink_contracts`: 95 checks (also passes on the Mac under
  clang).
- `mac_import_automation_contracts`: 82 checks.
- `app_shortcut_relink_contracts` (17) and `shelf_services_contracts` still
  pass against the ledger's new target rule; the App shortcuts sync now
  resolves a row relinked to a packaged application.
- The `macImport` area in `application_owner_coverage`: 1,608 checks. Cycle
  0 adds the card's focus events (appearing, Tab, SetFocus on each button,
  the dialog and a text part, a disabled button refusing it during staging,
  a reopened card listed and announced on its first frame) and the owner API
  over the imported root's ledger (a Notes resolve by note id, an Archive
  resolve by the attachment's Mac object, a packaged App shortcut target, a
  Shelf dismissal, a no-op and a resolving Reader reconcile, a root without
  an import left untouched).

After checkpoint 128 (residency), on the laptop (MSVC x64 Release):

- `mac_import_resident_contracts`: 675 checks. With 512 extra listed files
  the heap held while the card is open is 198,910 B for a staged session on
  its own, 278,292 B on the review, 201,034 B after a production handoff,
  11,892 B for the next run's recorded report and 1,575 B for a failure
  card; once each card hid, 0 B stayed in all seven scenarios, no session
  was alive and the process kept its 4 threads. The first laptop run left
  544 B per staging (the six SQLite sidecar paths above); naming that
  temporary removed them. Under clang on the Mac the same scenarios kept
  645,920 B (completed), 645,328 B (cancelled review), 325,696 B (replace
  question), 72,336 B (declined handoff), 3,776 B (failure) and 896 B
  (recorded report) after the card hid before this change, and 0 B after;
  the review itself went from 650,704 B to 337,280 B.
- `mac_import_contracts`: 758 checks, including the service's residency
  (a finished, failed or cancelled import keeps no session; `release()`
  returns it to idle without its results; a reviewed import refuses it).
- `mac_import_flow_contracts` (295), the work, relink, automation,
  reconcile, native, tool, plist and settings suites and
  `notes_relink_ledger_contracts` still pass.
- The `macImport` area in `application_owner_coverage`: 1,621 checks. Every
  hidden card now also reports an empty `MacImportFlowResidency` and no UIA
  part; the review holds exactly one session, the finished commit none, the
  restart work its one session until it ran, and every cycle ends with no
  session alive and no card.

Not verified yet:

- the host's real `DataRootRestart` and the `main.cpp` restart loop end to
  end (accepted quit, commit, fresh run);
- `recoverMacImportWork` as `main.cpp` now calls it before each run opens
  the root. The function itself is tested; the production call has not met
  an interrupted activation;
- the owner API as Notes, Archive and Reader call it on a real Mac
  export's ledger (their suites use synthetic ledgers only);
- focus events and SetFocus with a listening client (Narrator, Inspect);
- a sweep after a real process kill on the real root;
- the real shared folder dialog;
- an export of real user data, larger than the golden fixture;
- the real `%LOCALAPPDATA%\EndfieldHUD\v1` root;
- the card with a real UIA client (Narrator, Accessibility Insights);
- the card's text fit on a real display (longest CJK and Korean messages,
  DPI).

Known limits:

- Folders made before checkpoint 124 (`723fcfd`, named `e38d18d` before the
  push) carry no marker. They are never swept and must be removed by hand.
- The importer never deletes backups.
- The commit runs between two runs, so it shows no progress while it works.
  Staging progress and the final report are shown.

## 5. Acceptance tool

`mac_import_tool EXPORT_DIR DESTINATION` runs in dry-run mode by default. It
validates the whole export next to DESTINATION, prints the report as JSON
(including verified bytes and step counts), and removes its private folder
without writing to DESTINATION. Add `--commit` to
perform the import, `--replace` to repeat an import, and
`--accept store,...` to skip rejected stores. Use a scratch DESTINATION for
acceptance testing.

`mac_import_tool --work DESTINATION` lists the private
`<root>.import-<token>` folders next to DESTINATION and their states (see
"Private work folders") as JSON, without removing anything.
