#!/usr/bin/env python3
"""Extract the bottom-left ID card caption leaves exported from the Mac shell.

export_shell_packet.swift encodes HUDSourceWatchView's native overlay layers for
the top desktop frame. setDesktopProfile creates one container per caption
("desktop.profile.<binding>") holding a single CATextLayer; the exporter rendered
them for the default profile (Endministrator, UID 1000000000, level 60, HUD
accent FAD41F, English). This tool copies those five containers, the native
root geometry and nativeProfileBindings verbatim from a Windows runtime input
built from that export, after checking the runtime manifest digest, and writes
windows/tests/fixtures/id_card-native-captions.json.

Usage: id_card_captions_reference.py [RUNTIME_INPUT_DIR] [OUTPUT]
Default input: build/windows-watch-runtime-compiled-animation (the runtime input
the module-coverage preview uses).
"""
from pathlib import Path
import hashlib, json, sys

root = Path(__file__).resolve().parents[2]
runtime = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/windows-watch-runtime-compiled-animation'
output = Path(sys.argv[2]) if len(sys.argv) > 2 else root / 'windows/tests/fixtures/id_card-native-captions.json'
manifest = json.loads((runtime / 'runtime-input.json').read_bytes())
assert manifest['format'] == 'endfield-watch-runtime-input'
part = manifest['parts']['nativeTop']
raw = (runtime / part['file']).read_bytes()
digest = hashlib.sha256(raw).hexdigest()
assert digest == part['sha256'], 'nativeTop part differs from its runtime manifest digest'
top = json.loads(raw)
layers = top['nativeLayers']
bindings = ['managerName', 'managerNumber', 'managerLevel', 'managerLevelLabel', 'progressTxt']
containers = {c['name']: c for c in layers['children'] if isinstance(c.get('name'), str) and c['name'].startswith('desktop.profile.')}
assert sorted(containers) == sorted('desktop.profile.' + b for b in bindings), sorted(containers)
for name, container in containers.items():
    assert len(container['children']) == 1 and container['children'][0]['kind'] == 'text', name
profile_bindings = top['nativeProfileBindings']
assert sorted(profile_bindings) == sorted(containers)
reduced = {
    'schema_version': 1,
    'nativeLayers': {key: value for key, value in layers.items() if key != 'children'} |
                    {'children': [containers['desktop.profile.' + b] for b in bindings]},
    'nativeProfileBindings': profile_bindings,
    'exportedProfile': {'name': 'Endministrator', 'uid': '1000000000', 'permissionLevel': 60,
                        'hudAccentHex': 'FAD41F', 'language': 'english'},
    'provenance': {'source': 'export_shell_packet.swift nativeLayers (frame desktop-shell-1280x800-top)',
                   'nativeTopPartSHA256': digest, 'packetNativeTopSHA256': manifest['sourcePins']['nativeTopSHA256'],
                   'sourceManifestSHA256': manifest['sourcePins']['sourceManifestSHA256'],
                   'reduction': 'the five desktop.profile.<binding> containers verbatim, in binding order, plus root geometry'},
}
output.write_text(json.dumps(reduced, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n')
print('Wrote', output, 'from nativeTop', digest)
