#!/usr/bin/env python3
"""Reduce actual PersonalProfileCanvas model-layer exports to the drawn facts.

Input: presentation.json written by profile_reference.swift (ModuleReferenceLayerEncoder
rows of PersonalProfileCanvas.layer/backgroundLayer per state) and its raster/ PNGs.
Output: windows/tests/fixtures/profile-presentation-source.json. Geometry, colors (sRGB),
paths, text descriptors, gradients and masks are retained verbatim; intrinsic raster
contents keep only their digest and pixel size (Windows produces its own pixels).
Usage: profile_presentation_reference.py ROOT presentation.json OUTPUT.json
"""
import hashlib, json, struct, sys
from pathlib import Path

root, source, output = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
raster = source.parent
IDENTITY = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]

def color(value):
    return None if value is None else value.get('sRGB')

def png_size(path):
    data = path.read_bytes()[:24]
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    return list(struct.unpack('>II', data[16:24]))

def reduce(layer):
    out = {k: layer[k] for k in ('id', 'name', 'kind', 'bounds', 'position', 'anchorPoint', 'opacity', 'hidden', 'masksToBounds',
                                  'borderWidth', 'cornerRadius', 'contentsGravity', 'contentsRect', 'contentsScale', 'allowsGroupOpacity')}
    out['class'] = layer['class']
    if layer['transform'] != IDENTITY: out['transform'] = layer['transform']
    if layer['zPosition']: out['zPosition'] = layer['zPosition']
    out['backgroundColor'] = color(layer['backgroundColor'])
    out['borderColor'] = color(layer['borderColor'])
    contents = layer.get('contents')
    if contents:
        out['contents'] = {'sha256': contents['sha256'], 'size': png_size(raster / contents['asset'])}
    else:
        out['contents'] = None
    if layer['kind'] == 'shape':
        s = layer['shape']
        out['shape'] = {'path': s['path'], 'fillColor': color(s['fillColor']), 'strokeColor': color(s['strokeColor']), 'lineWidth': s['lineWidth'],
                        'fillRule': s['fillRule'], 'lineCap': s['lineCap'], 'lineJoin': s['lineJoin'], 'miterLimit': s['miterLimit']}
    elif layer['kind'] == 'text':
        t = layer['text']
        font = t.get('font', {})
        out['text'] = {'string': t['string'], 'fontSize': t['fontSize'], 'foregroundColor': color(t['foregroundColor']), 'alignment': t['alignment'],
                       'truncation': t['truncation'], 'wrapped': t['wrapped'],
                       'font': {k: font.get(k) for k in ('postScriptName', 'familyName', 'pointSize', 'symbolicTraits')}}
    elif layer['kind'] == 'gradient':
        g = layer['gradient']
        out['gradient'] = {'colors': [color(c) for c in g['colors']], 'locations': g['locations'], 'startPoint': g['startPoint'],
                           'endPoint': g['endPoint'], 'type': g['type']}
    out['mask'] = reduce(layer['mask']) if layer.get('mask') else None
    out['children'] = [reduce(c) for c in layer['children']]
    return out

value = json.loads(source.read_text())
states = []
for state in value['states']:
    row = {k: state[k] for k in ('name', 'language', 'dark', 'profile', 'hidden', 'popover', 'locked', 'status', 'hours')}
    row['layer'] = reduce(state['layer']); row['background'] = reduce(state['background'])
    states.append(row)
harness = root / 'windows/tools/profile_reference.swift'
sources = ['Sources/PersonalProfileCanvas.swift', 'Sources/HUDPortraitArtwork.swift', 'Sources/HUDControlHighlightLayer.swift', 'Sources/UserProfileStore.swift']
result = {'schemaVersion': 1, 'states': states,
          'provenance': {'harness': str(harness.relative_to(root)), 'harnessSHA256': hashlib.sha256(harness.read_bytes()).hexdigest(),
                         'sources': {s: hashlib.sha256((root / s).read_bytes()).hexdigest() for s in sources}, 'modifications': []},
          'coordinates': 'module-local 400x334 canvas; CALayer local geometry (bounds, position, anchorPoint) in insertion order'}
output.write_text(json.dumps(result, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n')
print('Wrote', output, len(states), 'presentation states', output.stat().st_size, 'bytes')
