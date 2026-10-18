#!/usr/bin/env python3
"""Validate actual-source fixture geometry, provenance and reusable layer schema.

Usage: test_module_reference.py OUTPUT [--source-root REPO] [--compare SECOND_OUTPUT]
No real app, device, clipboard, account or user preference access.
"""
import argparse
import hashlib
import json
import math
import pathlib
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=pathlib.Path)
parser.add_argument('--source-root', type=pathlib.Path)
parser.add_argument('--compare', type=pathlib.Path)
args = parser.parse_args()
checks = 0

def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)

def read(name):
    return json.loads((args.output/name).read_text())

def finite(value, context='root'):
    if isinstance(value, (float, int)):
        check(math.isfinite(value), 'nonfinite value '+context)
    elif isinstance(value, list):
        for i, child in enumerate(value):
            finite(child, context+'/'+str(i))
    elif isinstance(value, dict):
        for key, child in value.items():
            finite(child, context+'/'+key)

def walk(layer):
    yield layer
    if layer['mask'] is not None:
        yield from walk(layer['mask'])
    for child in layer['children']:
        yield from walk(child)

manifest = read('modules.json')
check(manifest['schemaVersion'] == 1, 'schema')
finite(manifest)
expected = {('notes','default'), ('profile','default')} | {(m,s) for m in ('system','display','hotkeys','about') for s in ('top','bottom')}
check({(e['module'],e['state']) for e in manifest['entries']} == expected, 'actual source state coverage')
check(len(manifest['entries']) == 10, 'no duplicate entries')
check(manifest['isolation'] == {
    'windowCreated':False, 'systemHUDViewCreated':False, 'temporaryStoresOnly':True,
    'isolatedPreferencesSuite':True, 'liveProvidersCreated':False, 'clipboardAccessed':False,
    'nativeCursorSetCount':0, 'profileActivated':False, 'settingsTimersScheduled':0}, 'isolation contract')
check(manifest['sourceDerivedAttachment']['fullSystemHUDViewPlacementVerified'] is False, 'honest placement boundary')
check(manifest['sourceDerivedAttachment'] == read('attachment.json'), 'attachment evidence')
check(manifest['sourceDerivedAttachment']['reportScale'] == .86, 'actual current report scale')
check(manifest['sourceDerivedAttachment']['reportCenter'] == [500,285], 'actual current report center')

all_layers = []
files = ['modules.json', 'attachment.json', 'serializer-fixture.json']
for entry in manifest['entries']:
    files.append(entry['file'])
    data = read(entry['file'])
    finite(data, entry['file'])
    check(data['module'] == entry['module'] and data['state'] == entry['state'], 'entry identity')
    check(data['contentFrameInDesignSpace'] == [300,152,400,334], 'actual module content frame')
    check(data['hostViewportInDesignSpace'] == [280,100,440,440], 'actual host viewport')
    layers = [node for root in data['roots'] for node in walk(root['layer'])]
    all_layers.extend(layers)
    check(len(layers) > 12, 'actual populated layer tree '+entry['file'])
    ids = [node['id'] for node in layers]
    check(len(ids) == len(set(ids)), 'stable unique node IDs')
    check(sum(node['kind'] == 'text' for node in layers) >= 3, 'actual native text')
    check(entry['actionCount'] == len(data['actions']), 'action count')
    check(entry['sliderCount'] == len(data['sliders']), 'slider count')
    check(len({a['id'] for a in data['actions']}) == len(data['actions']), 'stable unique action IDs')
    for a in data['actions'] + data['sliders']:
        check(bool(a['id']) and bool(a['label']), 'source action identity/label')
        check(len(a['rect']) == 4 and a['rect'][2] > 0 and a['rect'][3] > 0, 'action positive geometry')
        check(a['space'] in ('module-local','workspace-local'), 'action coordinate space')
    for node in layers:
        check(len(node['transform']) == 4 and all(len(c) == 4 for c in node['transform']), 'transform shape')
        check(node['animationKeys'] == [], 'static model fixture contains animation')
        check(0 <= node['opacity'] <= 1, 'opacity')
        if node['kind'] == 'shape' and node['shape']['path']:
            for command in node['shape']['path']:
                check(command['op'] in ('move','line','quadratic','cubic','close'), 'path command')
    if entry['module'] == 'notes':
        texts = '\n'.join(node.get('text',{}).get('string','') for node in layers)
        check('Neutral note' in texts and 'Compare geometry' in texts, 'neutral stored notes rendered by original canvas')
        check(any(a['space'] == 'workspace-local' for a in data['actions']), 'workspace actions')
        check(any(a['space'] == 'module-local' for a in data['actions']), 'toolbar actions')
    if entry['module'] == 'profile':
        texts = '\n'.join(node.get('text',{}).get('string','') for node in layers)
        check('Endministrator' in texts and '1000000000' in texts, 'neutral profile rendered by original canvas')
        check(any(a['id'] == 'profile:playerID' for a in data['actions']), 'actual profile ID action')

fixture = read('serializer-fixture.json')
finite(fixture)
check(fixture['position'] != [0,0] and fixture['anchorPoint'] == [.25,.75], 'anchor/position preserved')
check(fixture['transform'][3] == [9,12,0,1], 'column-major translation preserved')
check(fixture['opacity'] == .75 and fixture['masksToBounds'], 'opacity/clipping preserved')
check(fixture['mask']['kind'] == 'shape', 'recursive mask preserved')
check([c['name'] for c in fixture['children']] == ['shape','text','gradient','raster'], 'source child order preserved')
shape, text, gradient, raster = fixture['children']
check([p['op'] for p in shape['shape']['path']] == ['move','line','quadratic','cubic','close'], 'all CGPath segment types preserved')
check(shape['shape']['path'][2]['points'] == [[5,6],[7,8]], 'quadratic control/endpoint order')
check(shape['shape']['path'][3]['points'] == [[9,10],[11,12],[13,14]], 'cubic control/endpoint order')
check(shape['shape']['fillRule'] == 'even-odd', 'shape fill rule')
check(shape['shape']['lineDashPattern'] == [2,3], 'shape dash')
check(text['text']['string'] == 'Neutral', 'attributed text')
check(text['text']['runs'][0]['utf16Range'] == [0,7], 'UTF16 attribute range')
check(text['text']['runs'][0]['attributes']['NSBaselineOffset'] == 2, 'text baseline retained')
check(len(gradient['gradient']['colors']) == 2, 'gradient colors')
check(all('sRGB' in c and 'sourceColorSpace' in c for c in gradient['gradient']['colors']), 'color conversion/source encoding')
check(raster['contents'] is not None, 'intrinsic raster fixture')

for asset in manifest['rasterAssets']:
    path = pathlib.PurePosixPath(asset['path'])
    check(not path.is_absolute() and '..' not in path.parts and path.parts[0] == 'raster', 'safe asset path')
    data = (args.output/asset['path']).read_bytes()
    check(hashlib.sha256(data).hexdigest() == asset['sha256'], 'asset bytes hash')
    check(data[:8] == b'\x89PNG\r\n\x1a\n', 'PNG encoding')
    check(list(struct.unpack('>II',data[16:24])) == [asset['width'],asset['height']], 'PNG dimensions')
    files.append(asset['path'])
assets = {a['path'] for a in manifest['rasterAssets']}
for node in all_layers + list(walk(fixture)):
    if node['contents'] is not None:
        check(node['contents']['asset'] in assets, 'raster referenced by exported layer exists')

for projection in manifest['desktopCenterProjections']:
    check(projection['desktopMode'] and not projection['windowCreated'] and not projection['displayTimerActive'], 'real isolated desktop projection')
    matrix = projection['transform']
    for sample in projection['samplePoints']:
        x,y = sample['design']
        w = x*matrix[0][3] + y*matrix[1][3] + matrix[3][3]
        actual = [(x*matrix[0][i]+y*matrix[1][i]+matrix[3][i])/w for i in (0,1)]
        check(all(abs(a-b) < 1e-7 for a,b in zip(actual,sample['screen'])), 'source projection/consumer matrix parity')
check({tuple(p['viewport']) for p in manifest['desktopCenterProjections']} == {(1280,800),(1920,1080)}, 'projection size coverage')
for unsupported in manifest['unsupported']:
    check(set(unsupported) == {'node','feature','category'}, 'explicit unsupported schema')
    check(unsupported['category'] != 'metadata-only-animation', 'unexpected active fixture animation')

if args.source_root:
    provenance = read('provenance.json')
    check(provenance['baselineRequested'] == 'ca04f14', 'source lineage')
    check(provenance['sourceAndResourcesMatchBaseline'], 'original Mac source/resource baseline')
    for rel, digest in provenance['exporterSHA256'].items():
        check(hashlib.sha256((args.source_root/rel).read_bytes()).hexdigest() == digest, 'exporter changed during export: '+rel)
    for rel, digest in provenance['sourceAndResourceSHA256'].items():
        check(hashlib.sha256((args.source_root/rel).read_bytes()).hexdigest() == digest, 'source/resource changed during export: '+rel)
    check(provenance['sourceAndResourceSHA256']['Sources/SystemHUDView.swift'] == manifest['sourceDerivedAttachment']['sourceSHA256'], 'attachment source hash')
if args.compare:
    for name in files:
        check((args.output/name).read_bytes() == (args.compare/name).read_bytes(), 'independent run differs: '+name)
print(json.dumps({'passed':checks, 'moduleStates':len(manifest['entries']), 'layers':len(all_layers),
                  'rasterAssets':len(assets), 'unsupportedEntries':len(manifest['unsupported']),
                  'independentRunCompared':args.compare is not None}, sort_keys=True))
