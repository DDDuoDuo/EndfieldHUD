#!/usr/bin/env python3
"""Compacts an account oracle run into the committed fixture and Unicode table.

python3 -I windows/tools/hypergryph_account_reference.py REPO_ROOT ORACLE_OUTPUT PIN
"""
import hashlib, json, pathlib, sys

root, out, pin = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), sys.argv[3]
reference = json.loads((out / 'reference.json').read_text(encoding='utf-8'))
sources = ['Sources/HypergryphAccountAPI.swift', 'Sources/HypergryphAccountController.swift', 'Sources/HypergryphAccountKeychain.swift',
           'Sources/HypergryphAccountLogin.swift', 'Sources/HypergryphAccountModels.swift', 'Sources/HypergryphAvatarLoader.swift',
           'Sources/HUDAccountCanvas.swift', 'Sources/HUDAccountGauge.swift', 'Sources/HUDAccountInteraction.swift',
           'Sources/HUDControlHighlightLayer.swift', 'Sources/UserProfileStore.swift', 'Sources/WorkModeController.swift',
           'Resources/WatchSource/Scene/account-numerals.json',
           'Resources/WatchSource/Scene/sprites/source-textures/item_ap--2524b69d--8210737671276829403.png',
           'Resources/WatchSource/Scene/sprites/source-textures/bg_walletbar_1--cfe92272--8572312840272182184.png',
           'Resources/WatchSource/Scene/sprites/source-textures/bg_walletbar_2--cfe92272--7683125525266349835.png']
tools = ['windows/tools/hypergryph_account_reference.sh', 'windows/tools/hypergryph_account_reference.py',
         'windows/tools/hypergryph_account_reference.swift', 'windows/tools/hypergryph_account_reference_ui.swift',
         'windows/tools/module_reference_layers.swift']
sha = lambda p: hashlib.sha256((root / p).read_bytes()).hexdigest()

def compact_color(c):
    if c is None:
        return None
    return {'sRGB': c.get('sRGB')}

def compact(node):
    keep = {k: node[k] for k in ('id', 'name', 'kind', 'bounds', 'position', 'anchorPoint', 'opacity', 'hidden', 'zPosition',
                                 'masksToBounds', 'borderWidth', 'cornerRadius', 'transform') if k in node}
    keep['backgroundColor'] = compact_color(node.get('backgroundColor'))
    keep['borderColor'] = compact_color(node.get('borderColor'))
    if node.get('kind') == 'text':
        t = node['text']
        keep['text'] = {'string': t.get('string'), 'fontSize': t.get('fontSize'), 'alignment': t.get('alignment'),
                        'truncation': t.get('truncation'), 'wrapped': t.get('wrapped'),
                        'foregroundColor': compact_color(t.get('foregroundColor')),
                        'font': {k: t.get('font', {}).get(k) for k in ('postScriptName', 'familyName', 'pointSize')}}
    if node.get('kind') == 'shape':
        s = node['shape']
        keep['shape'] = {'path': s.get('path'), 'fillColor': compact_color(s.get('fillColor')), 'strokeColor': compact_color(s.get('strokeColor')),
                         'lineWidth': s.get('lineWidth'), 'lineCap': s.get('lineCap'), 'lineJoin': s.get('lineJoin'), 'fillRule': s.get('fillRule')}
    keep['mask'] = compact(node['mask']) if node.get('mask') else None
    keep['children'] = [compact(c) for c in node.get('children', [])]
    if node.get('contents') is not None:
        keep['contents'] = node['contents']
    return keep

for key in ('popoverEnabled', 'popoverDisabled', 'layer'):
    reference['gauge'][key] = compact(reference['gauge'][key])
for state in reference['canvas']['states']:
    if 'layers' in state:
        state['layers'] = compact(state['layers'])
unicode_sets = reference.pop('unicode')
reference['unicode'] = unicode_sets
reference['provenance'] = {'sourceAuthority': pin, 'sourceSHA256': {p: sha(p) for p in sources},
                           'exporterSHA256': {p: sha(p) for p in tools}, 'modifications': [],
                           'isolation': 'detached layers; in-process URLProtocol; synthetic credentials; temporary profile; fixed clock; TZ=UTC'}

# Gauge artwork prepared by the unchanged HUDAccountGauge.Artwork (CoreGraphics
# crop + sourceIn tint), stored as premultiplied RGBA so Windows needs no PNG
# decoder or colour-management guesswork. The numerals JSON is copied verbatim.
import base64
assets = root / 'windows/resources/account'
assets.mkdir(parents=True, exist_ok=True)
numerals = root / 'Resources/WatchSource/Scene/account-numerals.json'
(assets / 'account-numerals.json').write_bytes(numerals.read_bytes())
images = []
for name, key in [('wallet-back', 'back'), ('wallet-deco', 'deco'), ('item-ap', 'icon'), ('wallet-silhouette', 'silhouette')]:
    a = reference['gauge']['artwork'][key]
    data = base64.b64decode(a['rgba'])
    assert len(data) == a['width'] * a['height'] * 4
    (assets / f'{name}.rgba').write_bytes(data)
    images.append({'name': name, 'file': f'{name}.rgba', 'width': a['width'], 'height': a['height'], 'bytes': len(data),
                   'sha256': hashlib.sha256(data).hexdigest(), 'frame': a['frame'], 'contentsCenter': a['contentsCenter'], 'gravity': a['gravity']})
manifest = {'format': 'endfield-account-gauge-assets', 'schemaVersion': 1, 'sourceCommit': pin,
            'pixelFormat': 'premultiplied-RGBA8-sRGB-top-left',
            'numerals': {'file': 'account-numerals.json', 'sha256': hashlib.sha256(numerals.read_bytes()).hexdigest(),
                         'source': 'Resources/WatchSource/Scene/account-numerals.json'},
            'images': images,
            'sourcePins': {p: sha(p) for p in sources if p.startswith('Resources/') or p == 'Sources/HUDAccountGauge.swift'},
            'notice': 'Original-game wallet sprites, sanity icon and HarmonyOS Sans SC Medium SDF numeral subset, unchanged ownership; prepared by the unchanged Mac gauge artwork code.'}
(assets / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n', encoding='utf-8')
for a in reference['gauge']['artwork'].values():
    a.pop('rgba', None)

order = ['alphanumerics', 'whitespaces', 'whitespacesAndNewlines', 'controlCharacters', 'format', 'icuDigit', 'icuSpace', 'prependBeforeHash', 'joinsAfterBase'] + [f'digitEquivalent{d}' for d in range(10)]
lines = ['// Generated by windows/tools/hypergryph_account_reference.sh from the macOS',
         '// Foundation/ICU Unicode tables used by the unchanged account sources.',
         f'// Source authority: {pin}. Do not edit by hand.']
for name in order:
    ranges = unicode_sets[name]
    lines.append(f'inline constexpr UnicodeRange {name}Ranges[]{{')
    for i in range(0, len(ranges), 6):
        lines.append('    ' + ','.join(f'{{0x{a:X},0x{b:X}}}' for a, b in ranges[i:i + 6]) + ',')
    lines.append('};')
(root / 'windows/modules/hypergryph_account_unicode.inc').write_text('\n'.join(lines) + '\n', encoding='utf-8')
fixture = root / 'windows/tests/fixtures/hypergryph-account-source.json'
fixture.write_text(json.dumps(reference, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n', encoding='utf-8')
print('fixture', fixture.stat().st_size, 'bytes;', {n: len(unicode_sets[n]) for n in order})
