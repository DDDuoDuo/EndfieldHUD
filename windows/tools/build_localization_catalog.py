#!/usr/bin/env python3
"""Compile the pinned Mac catalog to immutable C++ literals; no UI/app data.

The Mac catalog is two files merged by Sources/LocalizationCatalog.swift: `entries = coreEntries +
redesignEntries.map(\\.entry)` (lookup takes the first row of a source pair) and `koreanTranslations =
coreKoreanTranslations.merging(redesignKoreanTranslations) { core, _ in core }`. Both files are compiled together
and dumped through `LocalizationCatalog.entries` / `Entry.korean`, so the rows and their Korean are exactly the
Mac's. Windows-kept pairs (tools/localization_windows_kept.json) are v1.2.0 pairs the Mac pruned while Windows
modules that are not redesigned yet still call them; their five languages come from the Mac catalog at the kept
file's base commit, never from Windows. Lookup stays by the full (English, Simplified Chinese) pair.

  build_localization_catalog.py                       verify the source authority pin, then generate
  build_localization_catalog.py --source-commit REV   before the pin moves: require the two worktree catalog files
                                                      to equal REV's and record REV instead
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from check_source_authority import verify

ROOT = Path(__file__).resolve().parents[2]
CATALOGS = ['Sources/LocalizationCatalog.swift', 'Sources/LocalizationCatalogRedesign.swift']
KEPT = ROOT / 'windows/tools/localization_windows_kept.json'
DUMP = '''
let result = LocalizationCatalog.entries.map { [$0.english, $0.simplifiedChinese, $0.traditionalChinese, $0.japanese, $0.korean] }
let data = try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys])
FileHandle.standardOutput.write(data)
'''


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def mac_rows(sources):
    """LocalizationCatalog.entries of the given Swift sources, in the Mac's order."""
    # /usr/bin/python3 (the Command Line Tools shim) exports its own SDKROOT; drop it so mac_sdk.sh picks the
    # SDK the Mac build uses with this compiler, like the other oracle tools.
    env = {k: v for k, v in os.environ.items() if k != 'SDKROOT'}
    sdk = subprocess.check_output(['bash', str(ROOT / 'windows/tools/mac_sdk.sh')], text=True, env=env).strip()
    with tempfile.TemporaryDirectory(prefix='ehud-language-') as work:
        swift = Path(work) / 'main.swift'
        # One script: the files' own `import Foundation` lines repeat harmlessly.
        swift.write_text('\n'.join(sources) + DUMP)
        return json.loads(subprocess.check_output(['swift', '-sdk', sdk, str(swift)], env=env))


def generate(source_commit=None):
    if source_commit:
        commit = git('rev-parse', '--verify', source_commit + '^{commit}').decode().strip()
        for path in CATALOGS:
            if git('show', f'{commit}:{path}') != (ROOT / path).read_bytes():
                raise ValueError(f'{path} differs from {commit}')
    else:
        commit = verify(ROOT)['commit']
    sources = [(ROOT / path).read_text() for path in CATALOGS]
    rows, seen = [], set()
    for row in mac_rows(sources):
        if (row[0], row[1]) in seen: continue  # the Mac's entry(english:simplifiedChinese:) finds the first row
        seen.add((row[0], row[1])); rows.append(row)
    mac_count = len(rows)
    kept = json.loads(KEPT.read_text())
    base = {(r[0], r[1]): r for r in mac_rows([git('show', kept['base'] + ':' + CATALOGS[0]).decode()])}
    names = [path.read_text(errors='replace') for path in (ROOT / 'windows').rglob('*')
             if path.suffix in ('.cpp', '.hpp', '.inc') and path.name != 'localization_catalog.inc' and 'tests' not in path.parts]
    unused = []
    for group, pairs in kept['pairs'].items():
        for english, simplified, *_ in pairs:
            if (english, simplified) in seen: raise ValueError(f'Windows-kept pair is back in the Mac catalog, drop it: {english}')
            if (english, simplified) not in base: raise ValueError(f'Windows-kept pair is not in {kept["base"]}: {english}')
            seen.add((english, simplified)); rows.append(base[(english, simplified)])
            literal = '"' + english.replace('\\', '\\\\').replace('"', '\\"') + '"'
            if not any(literal in text for text in names): unused.append(f'{group}: {english} / {simplified}')
    rows.sort(key=lambda x: (x[0].encode(), x[1].encode()))
    def literal(s):
        # Fixed octal avoids C++ hex-escape consumption by adjacent digits.
        return '"' + ''.join('\\%03o' % ord(c) if ord(c) < 32 else '\\"' if c == '"' else '\\\\' if c == '\\' else c for c in s) + '"'
    text = ('// Generated from the authoritative macOS LocalizationCatalog.swift + LocalizationCatalogRedesign.swift (merged as\n'
            '// the Mac merges them) and the Windows-kept v1.2.0 pairs of tools/localization_windows_kept.json; do not edit\n'
            '// translations here. Rows: %d Mac + %d Windows-kept.\n' % (mac_count, len(rows) - mac_count))
    text += '// Commit: ' + commit + ' (Windows-kept rows: ' + kept['base'] + ')\n'
    text += '// SHA256: ' + ' '.join(hashlib.sha256(s.encode()).hexdigest() for s in sources) + '\n'
    text += '\n'.join('    {' + ','.join(map(literal, row)) + '},' for row in rows) + '\n'
    target = ROOT / 'windows/core/localization_catalog.inc'
    target.write_text(text)
    print(f'Generated {len(rows)} five-language entries: {mac_count} Mac, {len(rows) - mac_count} Windows-kept ({len(text.encode())} source bytes)')
    for line in unused:
        print('Windows-kept pair without a Windows caller (drop it from the kept list):', line)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-commit')
    generate(parser.parse_args().source_commit)
