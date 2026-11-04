#!/usr/bin/env python3
"""Run the unchanged Mac Shelf drag presentation state against seeded sequences.

Usage: shelf_drag_presentation_reference.py NEW_OUTPUT_DIRECTORY

Sources/ShelfDragPresentationState.swift (OverlayController's
shelfDragPresentation, unchanged since 0.4.0) is compiled byte-for-byte with a
driver that replays seeded random transitions, stale tokens included. The
result is windows/tests/fixtures/shelf_drag_presentation_source.json, replayed
by tests/system_overlay_state_tests.cpp against core::ShelfDragPresentationState.
"""
import hashlib
import json
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[2]
if len(sys.argv) != 2:
    raise SystemExit(__doc__)
out = pathlib.Path(sys.argv[1]).resolve()
if out.exists():
    raise SystemExit('Refusing to reuse an existing output directory')
out.mkdir(parents=True)
source = 'Sources/ShelfDragPresentationState.swift'
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--', source], cwd=root, check=True)

# Rows: [op, actionKind, actionToken, didRestore, phase, generation, isActive].
# Ops: 0 begin, 1 ended(false), 2 ended(true), 3 didRetract(generation),
# 4 didRetract(stale), 5 didRestore(generation), 6 didRestore(stale), 7 reset.
# actionKind: 0 none, 1 retract, 2 hide, 3 restore, 4 close; didRestore -1 when
# the op is not didRestore.
swift = r'''
import Foundation
var seed: UInt64 = 0x5eed_5e1f
func random(_ upper: Int) -> Int {
    seed = seed &* 6364136223846793005 &+ 1442695040888963407
    return Int((seed >> 33) % UInt64(upper))
}
var raw: [[Int]] = []
for _ in 0..<300 {
    var state = ShelfDragPresentationState()
    raw.append([-1])
    for _ in 0..<16 {
        // Weighted towards the live order (begin, ended, didRetract, didRestore).
        let pick = random(20)
        let op = pick < 4 ? 0 : pick < 7 ? 1 : pick < 10 ? 2 : pick < 14 ? 3 : pick < 15 ? 4 : pick < 18 ? 5 : pick < 19 ? 6 : 7
        var kind = 0, token = 0, restored = -1
        func record(_ action: ShelfDragPresentationState.Action) {
            switch action {
            case .none: kind = 0
            case .retract(let t): kind = 1; token = t
            case .hide: kind = 2
            case .restore(let t): kind = 3; token = t
            case .close: kind = 4
            }
        }
        switch op {
        case 0: record(state.begin())
        case 1: record(state.ended(delivered: false))
        case 2: record(state.ended(delivered: true))
        case 3: record(state.didRetract(state.generation))
        case 4: record(state.didRetract(random(2) == 0 ? state.generation - 1 : state.generation + 1))
        case 5: restored = state.didRestore(state.generation) ? 1 : 0
        case 6: restored = state.didRestore(random(2) == 0 ? state.generation - 1 : state.generation + 1) ? 1 : 0
        default: state.reset()
        }
        let phase: Int
        switch state.phase { case .idle: phase = 0; case .retracting: phase = 1; case .hidden: phase = 2; case .restoring: phase = 3 }
        raw.append([op, kind, token, restored, phase, state.generation, state.isActive ? 1 : 0])
    }
}
let data = try! JSONSerialization.data(withJSONObject: ["raw": raw], options: [.sortedKeys])
try! data.write(to: URL(fileURLWithPath: CommandLine.arguments[1]), options: .withoutOverwriting)
'''
compiler = out / '.compiler'
compiler.mkdir()
(compiler / 'main.swift').write_text(swift)
env = os.environ.copy()
env.pop('SDKROOT', None)
sdk = subprocess.check_output(['bash', str(root / 'windows/tools/mac_sdk.sh')], text=True, env=env).strip()
subprocess.run(['xcrun', 'swiftc', '-O', '-swift-version', '5', '-sdk', sdk, '-module-cache-path', str(compiler / 'module-cache'),
                str(root / source), str(compiler / 'main.swift'), '-o', str(compiler / 'reference')], check=True, env=env)
target = out / 'shelf_drag_presentation_source.json'
subprocess.run([str(compiler / 'reference'), str(target)], check=True)
value = json.loads(target.read_text())
value['provenance'] = {'commit': commit, 'sources': {source: hashlib.sha256((root / source).read_bytes()).hexdigest()},
                       'compiledUnchanged': [source]}
target.write_text(json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n')
subprocess.run(['rm', '-rf', str(compiler)], check=True)
print(target)
