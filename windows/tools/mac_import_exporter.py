#!/usr/bin/env python3
"""One-time offline export of EndfieldHUD (macOS) data for the Windows import.

Run on the Mac with EndfieldHUD closed:

    python3 mac_import_exporter.py ~/Desktop/EndfieldHUD-export

The export folder is new and private (0700/0600). It contains copies of the
stores under ~/Library/Application Support/EndfieldCharge, the preference
domain io.github.endfieldcharge.EndfieldCharge (via `defaults export`) and a
manifest.json with every file's byte count and SHA-256. SQLite databases are
copied through SQLite's online-backup API, never by copying a live file. The
Keychain (com.ddduoduo.EndfieldHUD.hypergryph.account) is never read: account
credentials do not transfer and Windows requires signing in again. Nothing in
the source folder is modified.

Copy the folder to the Windows PC and choose it in EndfieldHUD's import.

Testing/oracles only: --source-root and --defaults-plist point at synthetic
data in temporary folders; with them the real app domain is never read.
"""
import argparse
import hashlib
import json
import os
import plistlib
import re
import shutil
import sqlite3
import stat
import subprocess
import sys
import tempfile
import time
import urllib.parse
from pathlib import Path

FORMAT = 'EndfieldHUD.macExport'
DOMAIN = 'io.github.endfieldcharge.EndfieldCharge'
UUID = r'[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}'
# Relative store files (and managed-image folders) exactly as the Mac app writes them.
JSON_STORES = ['Profile/profile.json', 'FileShelf/shelf.json', 'Reader/library.json', 'Calendar/calendar.json',
               'WorldMap/map.json', 'AppShortcuts/shortcuts.json', 'EventLog/events.json', 'Account/profile-cache.json']
SQLITE_STORES = ['Notes/notes.sqlite3', 'Archive/archive.sqlite']
IMAGE_FOLDERS = {'Notes/Images': re.compile('^' + UUID + r'\.png$'),
                 'Profile/Images': re.compile('^' + UUID + r'\.(png|image)$'),
                 'CenterLogo': re.compile('^' + UUID + r'\.png$')}


class ExportError(Exception):
    pass


def ordinary_file(path: Path) -> bool:
    try:
        info = os.lstat(path)
    except FileNotFoundError:
        return False
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ExportError(f'Refusing a link or special file: {path}')
    return True


def ordinary_dir(path: Path) -> bool:
    try:
        info = os.lstat(path)
    except FileNotFoundError:
        return False
    if not stat.S_ISDIR(info.st_mode):
        raise ExportError(f'Refusing a link or non-folder: {path}')
    return True


def digest(path: Path):
    h = hashlib.sha256()
    size = 0
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
            size += len(chunk)
    return size, h.hexdigest()


def private_copy(source: Path, target: Path):
    target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0)
    descriptor = os.open(target, flags, 0o600)
    with open(source, 'rb') as reader, os.fdopen(descriptor, 'wb') as writer:
        shutil.copyfileobj(reader, writer, 1 << 20)
        writer.flush()
        os.fsync(writer.fileno())


def backup_sqlite(source: Path, target: Path) -> int:
    for suffix in ('-journal', '-wal'):
        sibling = Path(str(source) + suffix)
        if sibling.exists() and sibling.stat().st_size > 0:
            raise ExportError(f'{source.name} has an active {suffix[1:]}; quit EndfieldHUD and export again')
    target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    reader = sqlite3.connect('file:' + urllib.parse.quote(str(source)) + '?mode=ro', uri=True)
    try:
        writer = sqlite3.connect(str(target))
        try:
            reader.backup(writer)
            version = writer.execute('PRAGMA user_version').fetchone()[0]
            if writer.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
                raise ExportError(f'{source.name} failed its integrity check')
        finally:
            writer.close()
    finally:
        reader.close()
    os.chmod(target, 0o600)
    return int(version)


def app_running() -> bool:
    return subprocess.run(['pgrep', '-x', 'EndfieldHUD'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def app_info(path):
    candidates = [Path(path)] if path else [Path('/Applications/EndfieldHUD.app/Contents/Info.plist'),
                                             Path.home() / 'Applications/EndfieldHUD.app/Contents/Info.plist']
    for candidate in candidates:
        if candidate.is_file():
            with open(candidate, 'rb') as handle:
                info = plistlib.load(handle)
            return str(info.get('CFBundleShortVersionString', '')), str(info.get('CFBundleVersion', ''))
    return '', ''


def export(output: Path, source_root: Path, defaults_plist, info_plist, check_running: bool):
    """Writes the export into a private partial folder next to `output` and
    renames it into place only when every file and the manifest are complete,
    so a failed or interrupted export never leaves a folder that looks usable."""
    if check_running and app_running():
        raise ExportError('EndfieldHUD is running; quit it (menu bar icon > Quit) and export again')
    if output.exists() or output.is_symlink():
        raise ExportError(f'{output} already exists; choose a new folder')
    if not ordinary_dir(source_root):
        raise ExportError(f'No EndfieldHUD data at {source_root}')
    output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    partial = Path(tempfile.mkdtemp(prefix='.' + output.name + '.partial-', dir=output.parent))  # mode 0700
    try:
        manifest = write_export(partial, source_root, defaults_plist, info_plist)
        if output.exists() or output.is_symlink():
            raise ExportError(f'{output} appeared during the export; choose a new folder')
        os.rename(partial, output)
    except BaseException:
        shutil.rmtree(partial, ignore_errors=True)
        raise
    return manifest


def write_export(output: Path, source_root: Path, defaults_plist, info_plist):
    files, databases = [], {}

    def record(relative: str):
        size, sha = digest(output / relative)
        files.append({'path': relative, 'bytes': size, 'sha256': sha})

    for store in SQLITE_STORES:
        source = source_root / store
        if ordinary_file(source):
            relative = 'EndfieldCharge/' + store
            databases[relative] = {'userVersion': backup_sqlite(source, output / relative)}
            record(relative)
    for store in JSON_STORES:
        source = source_root / store
        if ordinary_file(source):
            relative = 'EndfieldCharge/' + store
            private_copy(source, output / relative)
            record(relative)
    for folder, pattern in IMAGE_FOLDERS.items():
        directory = source_root / folder
        if not ordinary_dir(directory):
            continue
        for entry in sorted(directory.iterdir()):
            if pattern.match(entry.name) and ordinary_file(entry):
                relative = f'EndfieldCharge/{folder}/{entry.name}'
                private_copy(entry, output / relative)
                record(relative)
    preferences = None
    relative = f'Preferences/{DOMAIN}.plist'
    target = output / relative
    target.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    if defaults_plist:
        private_copy(Path(defaults_plist), target)
        preferences = relative
    else:
        result = subprocess.run(['defaults', 'export', DOMAIN, str(target)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if result.returncode == 0 and target.is_file():
            os.chmod(target, 0o600)
            preferences = relative
        else:
            # Usually the app never saved a preference. The import then keeps
            # the Windows settings; say so instead of failing silently.
            detail = result.stderr.decode('utf-8', 'replace').strip()
            print(f'Note: settings were not exported ({detail or "no saved preferences"}); Windows keeps its own settings.', file=sys.stderr)
            if target.exists():
                target.unlink()
    if preferences:
        with open(target, 'rb') as handle:
            plistlib.load(handle)  # must be a real property list
        record(preferences)
    version, build = app_info(info_plist)
    manifest = {'format': FORMAT, 'version': 1, 'exportedAt': time.time(),
                'source': {'bundleIdentifier': DOMAIN, 'appVersion': version, 'build': build},
                'files': files, 'sqlite': databases, 'preferences': preferences}
    descriptor = os.open(output / 'manifest.json', os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, 'w', encoding='utf-8') as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=1, sort_keys=True)
        handle.write('\n')
    return manifest


def self_test():
    """Synthetic stores in a temporary folder only; never the real app data."""
    with tempfile.TemporaryDirectory(prefix='ehud-exporter-selftest-') as temporary:
        root = Path(temporary) / 'EndfieldCharge'
        (root / 'Notes/Images').mkdir(parents=True)
        connection = sqlite3.connect(str(root / 'Notes/notes.sqlite3'))
        connection.execute('CREATE TABLE notes(id TEXT PRIMARY KEY NOT NULL)')
        connection.execute('PRAGMA user_version=2')
        connection.commit(); connection.close()
        (root / 'Notes/Images/00000000-0000-4000-8000-000000000001.png').write_bytes(b'\x89PNG\r\n\x1a\n')
        (root / 'Notes/Images/not-managed.txt').write_text('skip')
        (root / 'Reader').mkdir()
        (root / 'Reader/library.json').write_text('{"version":1,"books":[]}')
        plist = Path(temporary) / 'domain.plist'
        with open(plist, 'wb') as handle:
            plistlib.dump({'hudSettingsSchemaVersion': 1, 'language': 'english'}, handle, fmt=plistlib.FMT_BINARY)
        output = Path(temporary) / 'export'
        manifest = export(output, root, plist, None, check_running=False)
        paths = [f['path'] for f in manifest['files']]
        assert 'EndfieldCharge/Notes/notes.sqlite3' in paths and 'EndfieldCharge/Reader/library.json' in paths
        assert not any('not-managed' in p for p in paths), 'Only managed images are exported'
        assert manifest['sqlite']['EndfieldCharge/Notes/notes.sqlite3']['userVersion'] == 2
        for entry in manifest['files']:
            assert digest(output / entry['path']) == (entry['bytes'], entry['sha256'])
        assert stat.S_IMODE(os.stat(output).st_mode) == 0o700
        # A hot journal is refused and an existing output folder is never reused.
        Path(str(root / 'Notes/notes.sqlite3') + '-journal').write_bytes(b'hot')
        try:
            export(Path(temporary) / 'export2', root, plist, None, check_running=False)
            raise AssertionError('hot journal must be refused')
        except ExportError:
            pass
        leftovers = sorted(entry.name for entry in Path(temporary).iterdir() if 'partial' in entry.name or entry.name == 'export2')
        assert not leftovers, f'A failed export leaves nothing behind: {leftovers}'
        try:
            export(output, root, plist, None, check_running=False)
            raise AssertionError('existing output must be refused')
        except ExportError:
            pass
    print('PASS exporter self-test (synthetic temporary stores only)')


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('output', nargs='?', help='new folder for the export')
    parser.add_argument('--source-root', help=argparse.SUPPRESS)
    parser.add_argument('--defaults-plist', help=argparse.SUPPRESS)
    parser.add_argument('--app-info', help=argparse.SUPPRESS)
    parser.add_argument('--self-test', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.self_test:
        self_test()
        return 0
    if not args.output:
        parser.error('choose a new output folder')
    if sys.platform != 'darwin':
        parser.error('run this on the Mac that has the EndfieldHUD data')
    real = args.source_root is None
    source = Path(args.source_root) if args.source_root else Path.home() / 'Library/Application Support/EndfieldCharge'
    if real and args.defaults_plist:
        parser.error('--defaults-plist is only for synthetic test data')
    try:
        manifest = export(Path(args.output).expanduser().resolve(), source, args.defaults_plist, args.app_info, check_running=real)
    except ExportError as error:
        print(f'Export failed: {error}', file=sys.stderr)
        return 1
    print(f"Exported {len(manifest['files'])} files to {args.output}. Copy this folder to Windows and import it there.")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
