#!/usr/bin/env python3
"""Run the unchanged Mac SystemEventRecorder against synthetic transitions.

Usage: application_event_recorder_reference.py NEW_OUTPUT_DIRECTORY

Sources/SystemEventRecorder.swift is compiled byte-for-byte. Its collaborators
are inert stubs whose enum cases are copied verbatim from the pinned sources
(SystemEventKind, HUDClockStyle, HUDCenterLogo, HUDChargeMetric, WorkModeKind,
WorkModePhase). Only receive* calls are driven; refreshDisplays (NSScreen) is
never invoked. Writes application_event_recorder_source.json.
"""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

PIN = 'ca04f142185c7de40acd8523bdb563195d90a1d1'
root = pathlib.Path(__file__).resolve().parents[2]
if len(sys.argv) != 2:
    raise SystemExit(__doc__)
out = pathlib.Path(sys.argv[1]).resolve()
if out.exists():
    raise SystemExit('Refusing to reuse an existing output directory')
out.mkdir(parents=True)
sources = ['Sources/SystemEventRecorder.swift', 'Sources/SystemEventLog.swift', 'Sources/HUDClockStyle.swift', 'Sources/HUDCenterLogo.swift',
           'Sources/HUDChargeMetric.swift', 'Sources/WorkModeController.swift', 'Sources/BatteryMonitor.swift']
subprocess.run(['git', 'diff', '--quiet', PIN, '--'] + sources, cwd=root, check=True)
text = {name: (root / name).read_text() for name in sources}


def cases(source, marker):
    body = text[source]
    start = body.index(marker)
    lines = []
    for line in body[start:].splitlines()[1:]:
        stripped = line.strip()
        if stripped.startswith('case '):
            lines.append(stripped)
        elif stripped and not stripped.startswith('//'):
            break
    if not lines:
        raise SystemExit('No cases for ' + marker)
    return '\n    '.join(lines)


def inline_enum(source, marker):
    line = next(l for l in text[source].splitlines() if l.startswith(marker))
    return line


kinds = cases('Sources/SystemEventLog.swift', 'enum SystemEventKind: String, Codable, CaseIterable {')
clock = cases('Sources/HUDClockStyle.swift', 'enum HUDClockStyle: String, Codable, CaseIterable {')
logo = cases('Sources/HUDCenterLogo.swift', 'enum HUDCenterLogo: String, Codable, CaseIterable {')
metric = cases('Sources/HUDChargeMetric.swift', 'enum HUDChargeMetric: String, Codable, CaseIterable {')
work_kind = inline_enum('Sources/WorkModeController.swift', 'enum WorkModeKind: String')
work_phase = inline_enum('Sources/WorkModeController.swift', 'enum WorkModePhase: String')
for field in ['let percentage: Int?', 'let isPluggedIn: Bool', 'let isCharging: Bool', 'let isFullyCharged: Bool', 'let hasBattery: Bool']:
    if field not in text['Sources/BatteryMonitor.swift']:
        raise SystemExit('BatterySnapshot changed: ' + field)
if 'var isActive: Bool { phase == .running || phase == .paused }' not in text['Sources/WorkModeController.swift']:
    raise SystemExit('WorkModeSnapshot.isActive changed')

# Each step: [operation, payload]. Battery: [percentage|null, pluggedIn, charging, full, hasBattery].
# Work: [kind, phase, duration]. Config: [clockStyle, centerLogo, revision|null, alertMetric].
scripts = [
    [['battery', [80, True, True, False, True]], ['battery', [80, True, True, False, True]], ['battery', [81, False, False, False, True]],
     ['battery', [100, True, False, True, True]], ['battery', [None, False, False, False, False]], ['battery', [50, True, False, False, True]],
     ['battery', [50, False, False, False, True]], ['battery', [None, True, False, False, True]], ['battery', [None, True, True, False, True]]],
    [['work', ['countdown', 'idle', 1800]], ['work', ['countdown', 'running', 1800]], ['work', ['countdown', 'paused', 1800]],
     ['work', ['countdown', 'running', 1800]], ['work', ['countdown', 'completed', 1800]], ['work', ['countdown', 'idle', 1800]],
     ['work', ['stopwatch', 'running', 0]], ['work', ['stopwatch', 'idle', 0]], ['work', ['stopwatch', 'idle', 0]],
     ['work', ['countdown', 'running', 90000]], ['work', ['countdown', 'stopped', 90000]], ['work', ['countdown', 'idle', 90000]],
     ['work', ['countdown', 'running', 0.4]], ['work', ['countdown', 'idle', 0.4]]],
    [['config', ['digital', 'endfield', None, 'battery']], ['config', ['dial', 'endfield', None, 'battery']],
     ['config', ['dial', 'babel', None, 'cpu']], ['config', ['dial', 'custom', 'rev-1', 'cpu']], ['config', ['dial', 'custom', 'rev-2', 'cpu']],
     ['config', ['dial', 'custom', None, 'cpu']], ['config', ['rail', 'endfield', 'rev-3', 'ram']], ['config', ['rail', 'endfield', 'rev-4', 'ram']]],
    [['displays', {'1': 'Built-in Display'}], ['displays', {'1': 'Built-in Display', '7': '云终末地 Monitor'}],
     ['displays', {'7': '云终末地 Monitor'}], ['displays', {}], ['displays', {'2': 'B', '3': 'A'}],
     ['audio', {'a': 'Speakers'}], ['audio', {'a': 'Speakers', 'b': 'Headphones'}], ['audio', {'b': 'Headphones'}]],
    [['crop', [1, 1]], ['crop', [2, 1]], ['crop', [2, 3]], ['crop', [5, 6]], ['crop', [5, 6]], ['crop', [99, float('nan')]], ['crop', [20, 1]]],
]
swift = '''import AppKit
enum SystemEventKind: String, Codable, CaseIterable {
    ''' + kinds + '''
}
final class SystemEventLog {
    var records: [[String: Any]] = []
    func record(kind: SystemEventKind, metadata: [String: String] = [:]) { records.append(["kind": kind.rawValue, "metadata": metadata]) }
}
enum HUDClockStyle: String, Codable, CaseIterable {
    ''' + clock + '''
}
enum HUDCenterLogo: String, Codable, CaseIterable {
    ''' + logo + '''
}
enum HUDChargeMetric: String, Codable, CaseIterable {
    ''' + metric + '''
}
struct AppConfiguration { var clockStyle: HUDClockStyle; var centerLogo: HUDCenterLogo; var centerLogoRevision: String?; var alertMetric: HUDChargeMetric }
''' + work_kind + '\n' + work_phase + '''
struct WorkModeSnapshot { let kind: WorkModeKind; let phase: WorkModePhase; let duration: TimeInterval
    var isActive: Bool { phase == .running || phase == .paused } }
struct BatterySnapshot { let percentage: Int?; let isPluggedIn: Bool; let isCharging: Bool; let isFullyCharged: Bool; let hasBattery: Bool }

@main enum Reference {
    static func main() throws {
        let scripts = try JSONSerialization.jsonObject(with: Data(CommandLine.arguments[1].utf8)) as! [[[Any]]]
        var results: [[[String: Any]]] = []
        for script in scripts {
            let log = SystemEventLog()
            let recorder = SystemEventRecorder(log: log)
            var rows: [[String: Any]] = []
            for step in script {
                log.records = []
                let operation = step[0] as! String
                switch operation {
                case "battery":
                    let v = step[1] as! [Any]
                    recorder.receiveBattery(BatterySnapshot(percentage: v[0] as? Int, isPluggedIn: v[1] as! Bool, isCharging: v[2] as! Bool, isFullyCharged: v[3] as! Bool, hasBattery: v[4] as! Bool))
                case "work":
                    let v = step[1] as! [Any]
                    recorder.receiveWork(WorkModeSnapshot(kind: WorkModeKind(rawValue: v[0] as! String)!, phase: WorkModePhase(rawValue: v[1] as! String)!, duration: (v[2] as! NSNumber).doubleValue))
                case "config":
                    let v = step[1] as! [Any]
                    recorder.receiveConfiguration(AppConfiguration(clockStyle: HUDClockStyle(rawValue: v[0] as! String)!, centerLogo: HUDCenterLogo(rawValue: v[1] as! String)!, centerLogoRevision: v[2] as? String, alertMetric: HUDChargeMetric(rawValue: v[3] as! String)!))
                case "displays": recorder.receiveDisplays(step[1] as! [String: String])
                case "audio": recorder.receiveAudioDevices(step[1] as! [String: String])
                case "crop":
                    let v = step[1] as! [Any]
                    func number(_ value: Any) -> Double { (value as? NSNumber)?.doubleValue ?? Double.nan }
                    recorder.receiveProfileCrop(backgroundZoom: number(v[0]), thumbnailZoom: number(v[1]))
                default: fatalError("Unknown operation")
                }
                rows.append(["step": step, "records": log.records])
            }
            results.append(rows)
        }
        let data = try JSONSerialization.data(withJSONObject: ["scripts": results], options: [.sortedKeys])
        try data.write(to: URL(fileURLWithPath: CommandLine.arguments[2]), options: .withoutOverwriting)
    }
}
'''
compiler = out / '.compiler'
compiler.mkdir()
(compiler / 'main.swift').write_text(swift)
env = os.environ.copy()
env.pop('SDKROOT', None)
sdk = subprocess.check_output(['bash', str(root / 'scripts/build.sh'), '--print-sdk'], text=True, env=env).strip()
subprocess.run(['xcrun', 'swiftc', '-O', '-swift-version', '5', '-parse-as-library', '-sdk', sdk, '-module-cache-path', str(compiler / 'module-cache'),
                str(root / 'Sources/SystemEventRecorder.swift'), str(compiler / 'main.swift'), '-o', str(compiler / 'reference')], check=True, env=env)
target = out / 'application_event_recorder_source.json'
encoded = json.dumps(scripts, allow_nan=True).replace('NaN', '"nan"')
subprocess.run([str(compiler / 'reference'), encoded, str(target)], check=True)
value = json.loads(target.read_text())
value['provenance'] = {'commit': PIN, 'sources': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sources},
                       'compiledUnchanged': ['Sources/SystemEventRecorder.swift']}
target.write_text(json.dumps(value, ensure_ascii=False, separators=(',', ':'), sort_keys=True) + '\n')
print('Wrote', target, sum(len(s) for s in value['scripts']), 'steps')
