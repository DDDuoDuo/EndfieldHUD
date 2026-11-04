#!/usr/bin/env python3
"""Execute the unchanged source clock and volume-label rules of
NowPlayingAccessibility (the Mac compatibility fallback that reads a player's
elapsed "1:23 /" and total "4:56" labels and its volume slider).

The Windows reader of a player's own controls (modules/now_playing_player_
controls.cpp) applies the same rules to the labels of the player's
accessibility tree. This oracle runs the original static methods over
synthetic labels (no player, window or user data) and writes
windows/tests/fixtures/now-playing-player-controls-source.json.
"""
from pathlib import Path
import hashlib, json, os, subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'build/now-playing-player-controls-reference/source'
out.mkdir(parents=True, exist_ok=True)
commit = 'ca04f142185c7de40acd8523bdb563195d90a1d1'
path = 'Sources/NowPlayingAccessibility.swift'
data = (root / path).read_bytes()
assert data == subprocess.check_output(['git', 'show', commit + ':' + path], cwd=root)
text = data.decode()
begin = text.index('    static func clock(_ value: String) -> Double? {')
end = text.index('\n    private static func walk(', begin)
methods = text[begin:end]

clocks = [
    '0:00', '1:23', '01:23', '4:56', '59:59', '60:00', '999:59', '1000:00', '1:60', '1:5', '1:005', '1:2:3', '1:02:03', '10:59:59',
    '1:60:00', '100:00:00', '1:2:3:4', '12', '', ':', '::', '1:', ':30', '1::30', ' 1:23 ', '\n1:23\n', '1:23 /', '1 :23', '1: 23',
    '-1:23', '+1:23', '1.5:23', '１:２３', '١:٢٣', '²:30', '1:23​', ' 1:23 ', '　1:23', '1:23 ', 'a:bc', '00:00:00',
    '0:59', '1:00:60', '007:07', '0007:07',
]
volumes = ['Volume', 'volume', 'VOLUME', 'Master Volume', 'Vol', '音量', '音量调节', 'Lautstärke', 'Sound', '', 'VoLuMe slider']
source = '''import Foundation
enum Source {
''' + methods + '''
}
let input = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1]))) as! [String: [String]]
let clocks: [[String: Any]] = input["clocks"]!.map { value in
 if let seconds = Source.clock(value) { return ["value": value, "seconds": seconds] }
 return ["value": value, "seconds": NSNull()]
}
let volumes: [[String: Any]] = input["volumes"]!.map { ["value": $0, "volume": Source.isVolumeLabel($0)] }
try JSONSerialization.data(withJSONObject: ["clocks": clocks, "volumes": volumes], options: [.sortedKeys]).write(to: URL(fileURLWithPath: CommandLine.arguments[2]))
'''
(out / 'oracle.swift').write_text(source)
(out / 'input.json').write_text(json.dumps({'clocks': clocks, 'volumes': volumes}, ensure_ascii=False))
env = os.environ.copy()
env.pop('SDKROOT', None)
sdk = subprocess.check_output(['bash', str(root / 'windows/tools/mac_sdk.sh')], text=True, env=env).strip()
subprocess.run(['/usr/bin/swiftc', '-sdk', sdk, '-module-cache-path', str(root / 'build/windows-module-reference/.compiler/module-cache'),
                str(out / 'oracle.swift'), '-o', str(out / 'oracle')], check=True, env=env)
for name in ['output', 'repeat']:
    subprocess.run([str(out / 'oracle'), str(out / 'input.json'), str(out / (name + '.json'))], check=True)
assert (out / 'output.json').read_bytes() == (out / 'repeat.json').read_bytes()
result = json.loads((out / 'output.json').read_text())
fixture = {'schemaVersion': 1, 'sourceCommit': commit, 'sourcePins': {path: hashlib.sha256(data).hexdigest()},
           'scope': 'Unchanged NowPlayingAccessibility.clock and isVolumeLabel over synthetic labels.',
           'clocks': result['clocks'], 'volumes': result['volumes']}
dest = root / 'windows/tests/fixtures/now-playing-player-controls-source.json'
dest.write_text(json.dumps(fixture, ensure_ascii=False, separators=(',', ':')) + '\n')
print(dest, len(dest.read_bytes()), hashlib.sha256(dest.read_bytes()).hexdigest())
