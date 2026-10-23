#!/usr/bin/env python3
"""Run unchanged WorldMapStore against own temporary files; no app/user data."""
from pathlib import Path
import copy
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
output = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build/map-store-reference'
output.mkdir(parents=True, exist_ok=True)
base = {'version': 4, 'pins': [{'id': 'aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee',
    'x': .25, 'y': .75, 'createdAt': -12345.125, 'style': 'player'}],
    'viewport': {'centerX': (114.0579 + 180) / 360, 'centerY': (90 - 22.5431) / 180, 'zoom': 3}}
rows = []
def add(value): rows.append(json.dumps(value, ensure_ascii=False, separators=(',', ':')))
add(base)
for version, zoom, custom in [(1, 16, False), (2, 24, False), (2, 24, True),
    (3, 72, False), (3, 72, True), (3, 24, False), (4, 72, False)]:
    value = copy.deepcopy(base); value['version'] = version; value['viewport']['zoom'] = zoom
    if custom: value['viewport']['centerX'] = .2
    value['pins'][0].pop('style'); add(value)
for style in [None, 'yellow', 'green', 'unknown']:
    value = copy.deepcopy(base); value['pins'][0]['style'] = style; add(value)
for version in [0, 5, 4.0, 3.5]:
    value = copy.deepcopy(base); value['version'] = version; add(value)
for key, number in [('centerX', 1), ('centerY', -1), ('zoom', 128), ('zoom', 2.1), ('zoom', 2)]:
    value = copy.deepcopy(base); value['viewport'][key] = number; add(value)
value = copy.deepcopy(base); duplicate = copy.deepcopy(value['pins'][0]); duplicate['id'] = duplicate['id'].upper(); value['pins'].append(duplicate); add(value)
value = copy.deepcopy(base); value['pins'][0].pop('createdAt'); add(value)
# Mac 1.3.0 wideZoom (WorldMapStore.swift:87-105): applied, stale, out of range,
# wrong version, wrong type, and a viewport below v1.2.0's floor.
for version, zoom, wide in [(4, 2.1, 1.5), (4, 2.1, 1), (4, 2.1, 2.05), (4, 3, 1.5), (4, 2.1, .5), (4, 2.1, 2.1),
                            (3, 2.1, 1.5), (4, 2.1, None), (4, 2.1, '1.5'), (4, 1.5, None), (4, .5, None)]:
    value = copy.deepcopy(base); value['version'] = version; value['viewport']['zoom'] = zoom; value['wideZoom'] = wide; add(value)
rows.append('{broken')
(output / 'input.json').write_text(json.dumps(rows))
(output / 'main.swift').write_text(r'''import Foundation
struct Snapshot: Encodable {let version = 4;let pins:[MapPin];let viewport:WorldMapViewport}
let fm = FileManager.default
let own = fm.temporaryDirectory.appendingPathComponent("Endfield-map-codec-oracle-" + UUID().uuidString, isDirectory:true)
try fm.createDirectory(at:own,withIntermediateDirectories:true)
defer { try? fm.removeItem(at:own) }
let inputs = try JSONDecoder().decode([String].self,from:Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1])))
var cases:[[String:Any]]=[]
for (index,bytes) in inputs.enumerated() {
    let directory=own.appendingPathComponent(String(index),isDirectory:true)
    try fm.createDirectory(at:directory,withIntermediateDirectories:true)
    try Data(bytes.utf8).write(to:directory.appendingPathComponent("map.json"))
    var row:[String:Any] = ["input":bytes]
    do {
        let store=try WorldMapStore(directory:directory)
        let encoder=JSONEncoder();encoder.outputFormatting=[.sortedKeys]
        row["snapshot"]=String(decoding:try encoder.encode(Snapshot(pins:store.pins,viewport:store.viewport)),as:UTF8.self)
        row["accepted"]=true
    } catch {row["accepted"]=false}
    cases.append(row)
}
// Writer: the exact archive bytes the Mac saves for a camera (Archive.current),
// from the base archive and from one holding a stale wideZoom.
var writes:[[String:Any]]=[]
for (index,start) in [inputs[0],inputs.first { $0.contains("\"wideZoom\":1.5") && $0.contains("\"zoom\":3") }!].enumerated() {
    for zoom in [1.5,1.0,2.1,2.0999,4.0] {
        let directory=own.appendingPathComponent("write-\(index)-\(zoom)",isDirectory:true)
        try fm.createDirectory(at:directory,withIntermediateDirectories:true)
        try Data(start.utf8).write(to:directory.appendingPathComponent("map.json"))
        let store=try WorldMapStore(directory:directory)
        var camera=store.viewport;camera.zoom=zoom;try store.setViewport(camera)
        writes.append(["input":start,"zoom":zoom,"output":String(decoding:try Data(contentsOf:directory.appendingPathComponent("map.json")),as:UTF8.self)])
    }
}
let data=try JSONSerialization.data(withJSONObject:["cases":cases,"writes":writes],options:[.sortedKeys])
try data.write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
''')
source = root / 'Sources/WorldMapStore.swift'
cache = root / 'build/windows-module-reference/.compiler/module-cache'
subprocess.run(['xcrun', 'swiftc', '-O', '-sdk', '/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk',
    '-module-cache-path', str(cache), str(source), str(output / 'main.swift'), '-o', str(output / 'reference')], check=True)
subprocess.run([str(output / 'reference'), str(output / 'input.json'), str(output / 'output.json')], check=True)
document = json.loads((output / 'output.json').read_text())
document['source'] = {'path': 'Sources/WorldMapStore.swift', 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
(output / 'fixture.json').write_text(json.dumps(document, ensure_ascii=False, indent=2) + '\n')
print(f"PASS {len(rows)} original Foundation archive cases: {output / 'fixture.json'}")
