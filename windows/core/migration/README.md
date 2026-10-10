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
   before any store is validated, and nothing is left behind.
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
private folder and changes nothing. Afterwards, the owners reload from the root
and apply the summary: `profileSyncLocked`, `launchAtLogin` (the Windows
startup provider), `remindersNeedReconcile` (Calendar notifications), and
`customShortcutUntranslated`.

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

## 5. Acceptance tool

`mac_import_tool EXPORT_DIR DESTINATION` runs in dry-run mode by default. It
validates the whole export next to DESTINATION, prints the report as JSON
(including verified bytes and step counts), and removes its private folder
without writing to DESTINATION. Add `--commit` to
perform the import, `--replace` to repeat an import, and
`--accept store,...` to skip rejected stores. Use a scratch DESTINATION for
acceptance testing.
