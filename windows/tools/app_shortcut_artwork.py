#!/usr/bin/env python3
"""Refreshes windows/resources/app-shortcut (the + Add App tile artwork) from a reviewed Mac oracle run.

Usage: app_shortcut_artwork.py ORACLE_OUTPUT
ORACLE_OUTPUT is the NEW_OUTPUT directory of tools/app_shortcut_reference.sh: its presets/*.png are the Mac's own
AppShortcutArtwork.placedImage rasters (near-black glyphs for the accent tile, the white .93 App icon fallback) and
app-shortcut-source.json their pixel sizes, frames and titles. Writes artwork.json (version 2, read by
modules/app_shortcut_artwork.cpp; its SHA-256 is pinned in native/app_shortcut_assets.hpp) and copies the PNGs,
replacing the previous resources. Prints the new artwork.json SHA-256. Reads nothing else; no network.
"""
import hashlib, json, pathlib, shutil, sys

if len(sys.argv) != 2:
    sys.exit(__doc__)
source = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(__file__).resolve().parents[1] / 'resources' / 'app-shortcut'
data = json.loads((source / 'app-shortcut-source.json').read_text())
for entry in list(root.iterdir()):
    shutil.rmtree(entry) if entry.is_dir() else entry.unlink()
(root / 'presets').mkdir()
presets = []
for row in data['presets']:
    png = (source / row['file']).read_bytes()
    (root / row['file']).write_bytes(png)
    presets.append({'id': row['id'], 'english': row['titles']['en'], 'simplifiedChinese': row['titles']['zh-Hans'],
                    'file': row['file'], 'sha256': hashlib.sha256(png).hexdigest(), 'size': row['size'], 'frame': row['frame']})
provenance = data['provenance']
artwork = {'version': 2, 'presets': presets,
           'sourcePins': {k: v for k, v in sorted(provenance['originals'].items()) if not k.startswith('Sources/Localization')},
           'oracle': provenance['oracle'], 'gitHead': provenance['gitHead']}
text = json.dumps(artwork, sort_keys=True, ensure_ascii=False, separators=(',', ':')) + '\n'
(root / 'artwork.json').write_text(text)
print(hashlib.sha256(text.encode()).hexdigest())
