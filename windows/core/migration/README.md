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
   | Account | `HypergryphAccountController.Cache` Codable rules | Linked regions get `requiresReconnect=true`. Credential-like keys are removed. `profileSyncLocked` is reported |
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
   HasKeyboardFocus follows the card's own focus. Parts keep their COM
   identity per id across steps and refresh on content events only.

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

**Status (checkpoint 123).** The ledger and the transforms are tested
(`mac_import_contracts`, `mac_import_native_contracts`), but no module owner
calls `markResolved` or `reconcile` yet. Notes (footer action), Archive (the
attachment's error text) and App Shortcuts (choosing an app while editing)
relink their own records through their own pickers, without updating
`Migration/relink.json`, so the import card's relink list still shows those
items. Reader books and imported Shelf items have no relink action yet. The
contract below describes the intended owner behaviour.

An owner writes the relinked record first and then calls `markResolved`. When
it loads, it passes its unresolved entries through `reconcile(store, check)`,
where `check` looks the record up in the owner's own store. An entry whose
record already carries a Windows locator (the app stopped between the two
writes) is resolved with that record's path. An entry whose record the user
deleted is dismissed. Everything else stays unresolved. A repeated import
keeps earlier entries, and the decisions made on them, for stores it does not
replace.

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

`mac_import_work_contracts` covers the marker and journal codecs, the lock (a
live folder is never swept, even in-process), classification of 13 planted
folders, a bounded sweep of a deep tree, links that are never followed, the
pre-open recovery, and the session's journal between the two renames.
`mac_import_automation_contracts` (Windows) covers the card's UIA tree over
synthetic nodes.

## 4a. Verification status (checkpoint 123)

Verified on the laptop by hidden tests only: `mac_import_contracts` (753
checks), `mac_import_flow_contracts` (282), the plist, settings, reconcile,
native, tool and exporter suites, and the `macImport` area's three cycles in
`application_owner_coverage`. Those cycles cover an in-process golden import;
replace plus a synthetic restart, declined and then accepted, with the next
run's report; and an invalid folder, cancelled staging and keyboard input.

Not verified yet:

- the production `DataRootRestart` path end to end (accepted quit, commit,
  fresh run);
- the real shared folder dialog;
- an export of real user data, larger than the golden fixture;
- the real `%LOCALAPPDATA%\EndfieldHUD\v1` root;
- the card's text fit on a real display (longest CJK and Korean messages,
  DPI).

Since checkpoint 124 a private folder left by a process that ended during
staging or during the post-shutdown commit is swept by the next run, if it
is marked and abandoned. An activation interrupted between its two renames is
rolled back only when the host calls `recoverMacImportWork` before opening the
root. The importer never deletes backups. The commit runs between two runs,
so it shows no progress while it works; staging progress and the final report
are shown.

## 5. Acceptance tool

`mac_import_tool EXPORT_DIR DESTINATION` runs in dry-run mode by default. It
validates the whole export next to DESTINATION, prints the report as JSON
(including verified bytes and step counts), and removes its private folder
without writing to DESTINATION. Add `--commit` to
perform the import, `--replace` to repeat an import, and
`--accept store,...` to skip rejected stores. Use a scratch DESTINATION for
acceptance testing.
