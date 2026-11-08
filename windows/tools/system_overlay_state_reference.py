#!/usr/bin/env python3
"""Run the unchanged Mac overlay lifecycle against synthetic event scripts.

Usage: system_overlay_state_reference.py NEW_OUTPUT_DIRECTORY

Sources/SystemOverlayState.swift is compiled byte-for-byte. The AppDelegate
summon/first-run/settings/work-mode entry points and the OverlayController
toggle/close/quit/finish functions are extracted verbatim from the pinned
sources and compiled against inert stubs (no AppKit window, defaults domain,
shortcut, battery or timer). Only the two AppKit-heavy bodies (view creation
and teardown) are modelled, and the model is built from source lines that this
script asserts are present unchanged. The opening module
(HUDOverlayLayout.initialModule), "Last opened" across relaunches
(lastSystemModule read and saved through HUDOverlayLayout.lastModule /
saveLastModule, never in a diagnostic run), HUDModuleLayout's hidden-module
swap, and the explicit requests of a failed app launch and of the return
from Projection are extracted verbatim too. The result is
windows/tests/fixtures/system_overlay_state_source.json.
"""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

PIN = '2c047e0024aa337b11ab7d1e2577f7a918bace78'
root = pathlib.Path(__file__).resolve().parents[2]
if len(sys.argv) != 2:
    raise SystemExit(__doc__)
out = pathlib.Path(sys.argv[1]).resolve()
if out.exists():
    raise SystemExit('Refusing to reuse an existing output directory')
out.mkdir(parents=True)
sources = ['Sources/SystemOverlayState.swift', 'Sources/OverlayController.swift', 'Sources/AppDelegate.swift',
           'Sources/HUDModule.swift', 'Sources/SystemHUDView.swift', 'Sources/HUDOverlayLayout.swift',
           'Sources/HUDNavigationTarget.swift']
subprocess.run(['git', 'diff', '--quiet', PIN, '--'] + sources, cwd=root, check=True)
text = {name: (root / name).read_text() for name in sources}


def declaration(source, marker):
    """Return one complete declaration whose body contains no brace literals."""
    body = text[source]
    start = body.index(marker)
    if body.find(marker, start + 1) != -1:
        raise SystemExit('Ambiguous declaration: ' + marker)
    begin = body.index('{', start)
    depth, at = 1, begin + 1
    while depth:
        depth += (body[at] == '{') - (body[at] == '}')
        at += 1
    return body[start:at]


def public(value):
    return re.sub(r'(@objc |@discardableResult )?private (func|var)', lambda m: (m.group(1) or '').replace('@objc ', '') + m.group(2), value)


def require(source, snippet):
    if snippet not in text[source]:
        raise SystemExit('Pinned source line changed: ' + snippet)
    return snippet


# HUDModule cases, verbatim.
module_body = declaration('Sources/HUDModule.swift', 'enum HUDModule: String, CaseIterable {')
cases = []
for line in module_body.splitlines()[1:]:
    if line.strip().startswith('var '):
        break
    if line.strip().startswith('case '):
        cases.append(line.strip())
modules = [m.strip() for line in cases for m in line[5:].split(',')]
entrance = re.search(r'static let entranceDuration: TimeInterval = ([0-9.]+)', text['Sources/SystemHUDView.swift']).group(0)

# Lines that the model of performSystemAction/tearDownSystemPresentation uses.
opening_line = require('Sources/OverlayController.swift', 'let opening = HUDOverlayLayout.initialModule(request: initialModuleRequest, configuration: configuration, last: lastSystemModule)')
require('Sources/OverlayController.swift', 'initialModule: opening)')
perform = declaration('Sources/OverlayController.swift', 'private func performSystemAction(')
if '            initialModuleRequest = nil\n' not in perform:
    raise SystemExit('Initial request consumption changed')
teardown = declaration('Sources/OverlayController.swift', 'private func tearDownSystemPresentation(')
teardown_line = 'lastSystemModule = systemView?.selectedModule ?? lastSystemModule'
for line in ['let shouldQuit = quitAfterSystemClose', 'quitAfterSystemClose = false', teardown_line,
             'systemView = nil', 'onQuitAfterSystemClose?()']:
    if line not in teardown:
        raise SystemExit('Teardown changed: ' + line)
force = declaration('Sources/OverlayController.swift', 'func forceCloseSystemOverlay()')
for line in ['guard isSystemOverlayActive else { return }', 'systemState.forceClose()',
             'tearDownSystemPresentation(restoreFocus: false, notify: false)']:
    if line not in force:
        raise SystemExit('Forced close changed: ' + line)
# lastSystemModule: read at init and saved on every change, both through the
# user's defaults except in a diagnostic run (isDiagnosticRun is a launch
# argument test, modelled as a variable the script sets).
last_property = declaration('Sources/OverlayController.swift', 'private var lastSystemModule: HUDModule =')
last_defaults = require('Sources/OverlayController.swift', 'private static var lastModuleDefaults: UserDefaults? { isDiagnosticRun ? nil : .standard }')
require('Sources/OverlayController.swift', 'static let isDiagnosticRun = CommandLine.arguments.contains {')
layout_parts = [declaration('Sources/HUDOverlayLayout.swift', marker) for marker in [
    'static func initialModule(request:', 'static func lastModule(in defaults:', 'static func saveLastModule(']]
layout_parts += [require('Sources/HUDOverlayLayout.swift', line) for line in [
    'static let lastModuleKey = "hudLastOpenedModule"', 'static let firstModule: HUDModule = .map']]
# HUDModuleLayout's order, hidden set and restore rule (Display > Modules).
navigation = text['Sources/HUDNavigationTarget.swift']
right_marker = '    static let rightModules: [HUDModule] = ['
right_start = navigation.index(right_marker)
if navigation.find(right_marker, right_start + 1) != -1:
    raise SystemExit('Ambiguous declaration: ' + right_marker)
right_modules = navigation[right_start:navigation.index(']', right_start + len(right_marker)) + 1]
module_layout_parts = [require('Sources/HUDNavigationTarget.swift', line) for line in [
    'static let configurable = HUDDesktopWatchNavigation.rightModules.filter { $0 != .addApp }',
    'private(set) var order: [HUDModule]', 'private(set) var hidden: Set<HUDModule>',
    'var visible: [HUDModule] { order.filter { !hidden.contains($0) } }',
    'func restorable(_ module: HUDModule) -> HUDModule { hidden.contains(module) ? visible[0] : module }']]
module_layout_parts += [declaration('Sources/HUDNavigationTarget.swift', marker) for marker in [
    'init(order: [HUDModule] = configurable,', 'func canSetHidden(_ module: HUDModule, _ value: Bool) -> Bool {',
    '@discardableResult mutating func setHidden(']]
# The explicit requests: a failed app launch reopening on Add App
# (requestAppShortcutLaunch's completion) and the return from Projection.
launch_failure = require('Sources/OverlayController.swift', '''                guard self.systemState.phase == .closed else { return }
                // An explicit request: a chosen Open on module cannot replace it.
                self.initialModuleRequest = .addApp
                self.hide(animated: false)
                self.performSystemAction(self.systemState.toggle())
''')
projection_return = require('Sources/OverlayController.swift', '''            self.projectionHandoff = false
            // A return to where the HUD was: a chosen Open on module cannot replace it.
            self.initialModuleRequest = self.configuration.moduleLayout.restorable(self.lastSystemModule)
            self.performSystemAction(self.systemState.toggle(), snapshot: self.latestBatterySnapshot)
''')

overlay_parts = [public(declaration('Sources/OverlayController.swift', marker)) for marker in [
    '@discardableResult func toggleSystemOverlay(', 'private func requestQuit()', 'func closeSystemOverlay()',
    'func selectSystemModule(', 'private func finishSystemOpening(', 'private func finishSystemClosing(']]
app_parts = [public(declaration('Sources/AppDelegate.swift', marker)) for marker in [
    '@objc private func openWorkMode(', '@objc private func showPreferences(', 'private func openSettingsModule(',
    '@discardableResult private func toggleSystemOverlay()', '@objc private func openSystemOverlay(',
    '@objc private func about(', 'func applicationShouldHandleReopen(']]
quit_accepted = declaration('Sources/AppDelegate.swift', 'overlay.onQuitAccepted = {')
launch = text['Sources/AppDelegate.swift']
first = launch.index('        let firstRun = !UserDefaults.standard.bool(forKey: "hasLaunched")')
last_marker = '        if args.contains("--power") { _ = toggleSystemOverlay() }\n'
last = launch.index(last_marker, first) + len(last_marker)
first_run = launch[first:last]

# Event scripts. Every event is valid on the Mac model; the Windows replay must
# reproduce every recorded snapshot exactly.
M = modules
scripts = [
    ['launch:', 'opened', 'select:notes', 'hotkey', 'closed', 'hotkey', 'opened'],
    ['launch:--login', 'hotkey', 'hotkey', 'opened', 'hotkey', 'hotkey', 'closed', 'hotkey'],
    ['launch:--settings', 'opened', 'select:display', 'close', 'closed', 'hotkey', 'opened'],
    ['launch:--no-onboarding', 'traySettings', 'opened', 'trayAbout', 'trayWorkMode', 'close', 'traySettings', 'closed', 'traySettings', 'opened'],
    ['launch:--login', 'trayWorkMode', 'advance:0.5', 'opened', 'advance:0.6', 'close', 'closed', 'hotkey', 'opened'],
    ['launch:--login', 'trayWorkMode', 'close', 'advance:0.4', 'opened', 'advance:2', 'closed', 'trayWorkMode', 'opened', 'advance:1.05'],
    ['launch:--login', 'trayWorkMode', 'opened', 'close', 'closed', 'hotkey', 'advance:1.06', 'opened', 'advance:1'],
    ['launch:--login', 'reopen', 'reopen', 'opened', 'reopen', 'select:map', 'close', 'reopen', 'closed', 'reopen'],
    ['launch:--login', 'trayOpen', 'trayOpen', 'opened', 'trayOpen', 'hotkey', 'trayOpen', 'closed', 'trayOpen'],
    ['launch:--login', 'hotkey', 'close', 'opened', 'closed', 'hotkey', 'openedStale', 'opened', 'closedStale', 'close', 'closed'],
    ['launch:--login', 'hotkey', 'opened', 'quit', 'quit', 'hotkey', 'closed', 'hotkey'],
    ['launch:--login', 'hotkey', 'quit', 'opened', 'quit', 'closed'],
    ['launch:--login', 'hotkey', 'opened', 'select:archive', 'force', 'hotkey', 'opened', 'force', 'force', 'hotkey'],
    ['launch:--login', 'suspend', 'hotkey', 'trayOpen', 'traySettings', 'reopen', 'resume', 'hotkey', 'opened'],
    ['launch:--login', 'editing:on', 'hotkey', 'traySettings', 'editing:off', 'traySettings', 'opened'],
    ['launch:--power', 'opened'],
    ['launch:--settings,--power', 'opened'],
    ['launch:--settings,--login'],
    ['launch:--preview'],
    ['launch:--preview,--power', 'opened'],
    ['launch:--no-onboarding,--settings'],
    ['launched', 'launch:'],
    ['launched', 'launch:--settings', 'opened'],
    ['launched', 'launch:--power', 'opened', 'select:calendar', 'close', 'closed', 'trayAbout', 'opened', 'close', 'closed', 'hotkey'],
    ['launch:--login', 'hotkey', 'opened', 'select:notes', 'select:clipboard', 'close', 'select:reader', 'closed', 'select:volume', 'hotkey'],
    ['launch:--login', 'trayAbout', 'trayAbout', 'opened', 'trayAbout', 'select:storage', 'close', 'closed', 'hotkey'],
    ['launch:--login', 'hotkey', 'opened', 'close', 'trayWorkMode', 'closed', 'trayWorkMode', 'advance:1.04', 'opened', 'advance:0.02'],
    ['launch:--login', 'trayWorkMode', 'trayWorkMode', 'advance:1.1', 'opened', 'trayWorkMode'],
]
for name in M:
    scripts.append(['launch:--login', 'hotkey', 'opened', 'select:' + name, 'close', 'closed', 'hotkey', 'opened'])
# Open on, Last opened across relaunches, hidden modules and the explicit
# requests. "restart" relaunches with the same defaults and configuration;
# "diagnostic" relaunches as a diagnostic run; "saved:<name>" stores a raw
# hudLastOpenedModule value as an earlier run (or a damaged file) left it.
scripts += [
    ['launch:--login', 'hotkey', 'opened', 'select:calendar', 'close', 'closed', 'restart', 'launch:--login', 'hotkey', 'opened', 'close', 'closed', 'hotkey'],
    ['launch:', 'opened', 'close', 'closed', 'hotkey', 'opened', 'close', 'closed', 'restart', 'launched', 'launch:', 'hotkey', 'opened'],
    ['launch:--login', 'choose:hotkeys', 'hotkey', 'opened', 'select:reader', 'close', 'closed', 'hotkey', 'opened', 'choose:last', 'close', 'closed', 'hotkey'],
    ['launch:--login', 'choose:archive', 'restart', 'launch:--login', 'hotkey', 'opened', 'close', 'closed', 'choose:last', 'restart', 'launch:--login', 'hotkey'],
    ['launch:--login', 'choose:calendar', 'hide:calendar', 'hotkey', 'opened', 'close', 'closed', 'hide:notes', 'hotkey', 'opened', 'close', 'closed', 'choose:last', 'hotkey'],
    ['launch:--login', 'hotkey', 'opened', 'select:map', 'close', 'closed', 'hide:map', 'hotkey', 'opened', 'select:volume', 'close', 'closed', 'show:map', 'hotkey'],
    ['launch:--login', 'choose:storage', 'traySettings', 'opened', 'close', 'closed', 'trayAbout', 'opened', 'close', 'closed', 'hotkey'],
    ['launch:--login', 'choose:hotkeys', 'hotkey', 'opened', 'close', 'closed', 'launchFailed', 'opened', 'close', 'closed', 'hotkey', 'opened', 'close', 'launchFailed', 'closed'],
    ['launch:--login', 'choose:calendar', 'hotkey', 'opened', 'select:projection', 'close', 'closed', 'projectionReturn', 'opened', 'close', 'closed', 'hotkey'],
    ['launch:--login', 'hotkey', 'opened', 'select:projection', 'close', 'closed', 'hide:projection', 'choose:display', 'projectionReturn', 'opened', 'close', 'closed', 'projectionReturn'],
    ['saved:teleporter', 'restart', 'launch:--login', 'hotkey', 'opened', 'close', 'closed', 'saved:addApp', 'restart', 'launch:--login', 'hotkey'],
    ['launch:--login', 'hotkey', 'opened', 'select:eventLog', 'force', 'restart', 'launch:--login', 'hotkey', 'opened', 'quit', 'closed'],
    ['saved:workMode', 'diagnostic', 'launch:--login', 'hotkey', 'opened', 'select:clipboard', 'close', 'closed', 'hotkey', 'opened', 'close', 'closed', 'diagnostic', 'launch:--login', 'hotkey'],
    ['launch:--login', 'hotkey', 'opened', 'select:archive', 'close', 'closed', 'diagnostic', 'launch:--login', 'hotkey', 'opened', 'close', 'closed', 'restart', 'launch:--login', 'hotkey'],
]

swift = '''import Foundation
// Inert stubs. No AppKit, defaults domain, window, timer or shortcut exists.
enum HUDModule: String, CaseIterable {
''' + '\n'.join('    ' + c for c in cases) + '''
}
enum SystemHUDView { ''' + entrance.replace('static let', 'static let') + ''' }
struct BatterySnapshot { static let unavailable = BatterySnapshot() }
enum HUDDesktopWatchNavigation {
''' + right_modules + '''
}
struct HUDModuleLayout: Equatable {
''' + '\n'.join('    ' + part for part in module_layout_parts) + '''
}
struct OverlayLayoutStub { var openModule: HUDModule? }
struct AppConfiguration { var closeOnFocusLost = true; var moduleLayout = HUDModuleLayout(); var overlayLayout = OverlayLayoutStub() }
struct SettingsStub { var configuration = AppConfiguration() }
final class NSApplication {}
struct FakeTime { var seconds: Double; static func now() -> FakeTime { FakeTime(seconds: Clock.now) } }
func + (time: FakeTime, delay: Double) -> FakeTime { FakeTime(seconds: time.seconds + delay) }
enum Clock { static var now = 0.0; static var pending: [(Double, Int, () -> Void)] = []; static var serial = 0 }
struct FakeQueue {
    func asyncAfter(deadline: FakeTime, execute body: @escaping () -> Void) { Clock.serial += 1; Clock.pending.append((deadline.seconds, Clock.serial, body)) }
    func async(execute body: @escaping () -> Void) { asyncAfter(deadline: FakeTime.now(), execute: body) }
}
enum DispatchQueue { static let main = FakeQueue() }
final class UserDefaults {
    static var standard = UserDefaults()
    var values: [String: Any] = [:]; var writes: [String: Int] = [:]
    func bool(forKey key: String) -> Bool { values[key] as? Bool ?? false }
    func string(forKey key: String) -> String? { values[key] as? String }
    func set(_ value: Any, forKey key: String) { values[key] = value; writes[key, default: 0] += 1 }
}
enum HUDOverlayLayout {
''' + '\n'.join('    ' + part for part in layout_parts) + '''
}
final class ViewStub {
    var selectedModule: HUDModule; var interactionEnabled = false; var isDraggingShelfItem = false; var isPreparingSourceBackdrop = false
    init(initialModule: HUDModule) { selectedModule = initialModule }
    func selectModule(_ module: HUDModule, animated: Bool) { selectedModule = module }
    func showStable(preservingChargeAnimation: Bool, preservingPointerMotion: Bool, preservingNowPlayingPresentation: Bool) {}
}
struct ShelfDragStub { var isActive = false }
final class WorkStub { func cancel() {} }
final class RevealStub { func close() {} }
final class ShortcutStub { var stops = 0; func stop() { stops += 1 } }
var effects: [String] = []

final class OverlayController {
    static var isDiagnosticRun = false
    ''' + public(last_property) + '''
    ''' + public(last_defaults) + '''
    var systemState = SystemOverlayState()
    var configuration = AppConfiguration()
    var latestBatterySnapshot: BatterySnapshot?
    var projectionHandoff = false
    var initialModuleRequest: HUDModule?
    var systemView: ViewStub?
    var quitRequested = false, quitAfterSystemClose = false, isEditingPosition = false, appLaunchInFlight = false
    var isProjectionActive: Bool { false }
    var shelfDragPresentation = ShelfDragStub()
    var closeAfterShelfDrag = false, documentTerminationInProgress = false, openStorageAfterClose = false
    var transitionDeadline: WorkStub?
    var pendingAppLaunch: (name: String, url: URL)?
    var pendingShelfReveal: RevealStub?
    var afterSystemClose: (() -> Void)?
    var onQuitAccepted: (() -> Void)?
    var onQuitAfterSystemClose: (() -> Void)?
    var canPresentShelfDrop: Bool { false }
    var pendingShelfDropPresentation: Int?
    var systemPhase: SystemOverlayPhase { systemState.phase }
    var isSystemOverlayActive: Bool { systemState.isActive }
    var systemPresentationGeneration: Int { systemState.generation }
    func hide(animated: Bool = true) {}
    func update(snapshot: BatterySnapshot, configuration: AppConfiguration) {}
    func returnFromProjection() {}
    func logSystemPhase() {}
    func presentPendingShelfDrop() {}
''' + '\n'.join(overlay_parts) + '''
    // Model: only the asserted source lines of these AppKit-heavy bodies.
    func performSystemAction(_ action: SystemOverlayState.Action, snapshot: BatterySnapshot? = nil) {
        switch action {
        case .none: return
        case .open(let token):
            ''' + opening_line + '''
            systemView = ViewStub(initialModule: opening)
            initialModuleRequest = nil
            effects.append("open:\\(token):\\(opening.rawValue)")
        case .close(let token):
            systemView?.interactionEnabled = false
            effects.append("close:\\(token)")
        }
    }
    func tearDownSystemPresentation(restoreFocus: Bool, notify: Bool) {
        let shouldQuit = quitAfterSystemClose
        quitAfterSystemClose = false
        ''' + teardown_line + '''
        systemView = nil
        if shouldQuit { onQuitAfterSystemClose?(); return }
    }
    func forceCloseSystemOverlay() {
        guard isSystemOverlayActive else { return }
        systemState.forceClose()
        logSystemPhase()
        systemView?.interactionEnabled = false
        tearDownSystemPresentation(restoreFocus: false, notify: false)
    }
    // The two completion bodies, verbatim.
    func appLaunchFailed() {
''' + launch_failure + '''    }
    func projectionReturned() {
''' + projection_return + '''    }
}

final class AppModel {
    let overlay = OverlayController()
    let shortcut = ShortcutStub()
    var suspensionReasons = Set<Int>()
    var suspended: Bool { !suspensionReasons.isEmpty }
    var terminating = false
    var previewSnapshot: BatterySnapshot?
    var snapshot: BatterySnapshot?
    let hudSettings = SettingsStub()
    let diagnosticDomain: String? = nil
    func preview(_ sender: Any?) { effects.append("preview") }
    init() {
        ''' + quit_accepted + '''
        overlay.onQuitAfterSystemClose = { effects.append("terminate") }
    }
''' + '\n'.join(app_parts) + '''
    func startup(_ args: [String]) {
''' + first_run + '''    }
}

func run(_ script: [String]) -> [[String: Any]] {
    Clock.now = 0; Clock.pending = []; Clock.serial = 0; UserDefaults.standard = UserDefaults(); effects = []
    OverlayController.isDiagnosticRun = false
    var app = AppModel()
    var rows: [[String: Any]] = []
    for event in script {
        effects = []
        let parts = event.split(separator: ":", maxSplits: 1, omittingEmptySubsequences: false).map(String.init)
        switch parts[0] {
        case "launched": UserDefaults.standard.set(true, forKey: "hasLaunched")
        case "launch": app.startup(["EndfieldHUD"] + parts[1].split(separator: ",").map(String.init))
        case "hotkey": _ = app.toggleSystemOverlay()
        case "trayOpen": app.openSystemOverlay(nil)
        case "traySettings": app.showPreferences(nil)
        case "trayAbout": app.about(nil)
        case "trayWorkMode": app.openWorkMode(nil)
        case "reopen": _ = app.applicationShouldHandleReopen(NSApplication(), hasVisibleWindows: false)
        case "select": app.overlay.selectSystemModule(HUDModule(rawValue: parts[1])!)
        case "opened": app.overlay.finishSystemOpening(app.overlay.systemState.generation)
        case "openedStale": app.overlay.finishSystemOpening(app.overlay.systemState.generation - 1)
        case "closed": app.overlay.finishSystemClosing(app.overlay.systemState.generation)
        case "closedStale": app.overlay.finishSystemClosing(app.overlay.systemState.generation - 1)
        case "close": app.overlay.closeSystemOverlay()
        case "force": app.overlay.forceCloseSystemOverlay()
        case "quit": app.overlay.requestQuit()
        case "suspend": app.suspensionReasons.insert(1)
        case "resume": app.suspensionReasons.remove(1)
        case "editing": app.overlay.isEditingPosition = parts[1] == "on"
        case "choose": app.overlay.configuration.overlayLayout.openModule = parts[1] == "last" ? nil : HUDModule(rawValue: parts[1])!
        case "hide": app.overlay.configuration.moduleLayout.setHidden(HUDModule(rawValue: parts[1])!, true)
        case "show": app.overlay.configuration.moduleLayout.setHidden(HUDModule(rawValue: parts[1])!, false)
        case "saved": UserDefaults.standard.values[HUDOverlayLayout.lastModuleKey] = parts[1]
        case "restart", "diagnostic":
            // A relaunch: the defaults and the configuration outlive the process.
            let configuration = app.overlay.configuration
            OverlayController.isDiagnosticRun = parts[0] == "diagnostic"
            app = AppModel(); app.overlay.configuration = configuration
        case "launchFailed": app.overlay.appLaunchFailed()
        case "projectionReturn": app.overlay.projectionReturned()
        case "advance":
            Clock.now += Double(parts[1])!
            while let next = Clock.pending.enumerated().filter({ $0.element.0 <= Clock.now }).min(by: { ($0.element.0, $0.element.1) < ($1.element.0, $1.element.1) }) {
                Clock.pending.remove(at: next.offset); next.element.2()
            }
        default: fatalError("Unknown event")
        }
        let o = app.overlay
        rows.append(["event": event, "effects": effects, "phase": o.systemPhase.rawValue, "generation": o.systemState.generation,
                     "closeAfterOpening": o.systemState.closeAfterOpening, "view": o.systemView?.selectedModule.rawValue as Any? ?? NSNull(),
                     "last": o.lastSystemModule.rawValue, "request": o.initialModuleRequest?.rawValue as Any? ?? NSNull(),
                     "quitRequested": o.quitRequested, "terminating": app.terminating, "shortcutStops": app.shortcut.stops,
                     "hasLaunched": UserDefaults.standard.bool(forKey: "hasLaunched"),
                     "saved": UserDefaults.standard.values[HUDOverlayLayout.lastModuleKey] ?? NSNull(),
                     "savedWrites": UserDefaults.standard.writes[HUDOverlayLayout.lastModuleKey, default: 0],
                     "timers": Clock.pending.map { $0.0 - Clock.now }])
    }
    return rows
}

// Raw state machine: 400 seeded sequences of 24 operations, including stale tokens.
var seed: UInt64 = 0x5eed0f0e
func random(_ bound: Int) -> Int { seed = seed &* 6364136223846793005 &+ 1442695040888963407; return Int((seed >> 33) % UInt64(bound)) }
var raw: [[Int]] = []
for _ in 0..<400 {
    var state = SystemOverlayState()
    raw.append([-1])
    for _ in 0..<24 {
        let op = random(8)
        var kind = 0, token = 0, closed = -1
        func record(_ action: SystemOverlayState.Action) { switch action { case .none: kind = 0; case .open(let t): kind = 1; token = t; case .close(let t): kind = 2; token = t } }
        switch op {
        case 0: record(state.toggle())
        case 1: record(state.requestClose())
        case 2: record(state.requestClose(interruptOpening: true))
        case 3: record(state.didOpen(state.generation))
        case 4: record(state.didOpen(state.generation - 1 - random(2)))
        case 5: closed = state.didClose(state.generation) ? 1 : 0
        case 6: closed = state.didClose(random(2) == 0 ? state.generation - 1 : state.generation + 1) ? 1 : 0
        default: state.forceClose()
        }
        let phase = ["closed", "opening", "open", "closing"].firstIndex(of: state.phase.rawValue)!
        raw.append([op, kind, token, closed, phase, state.generation, state.closeAfterOpening ? 1 : 0, state.isActive ? 1 : 0])
    }
}
let scripts: [[String]] = ''' + json.dumps(scripts) + '''
let result: [String: Any] = ["modules": HUDModule.allCases.map(\\.rawValue), "entranceDuration": SystemHUDView.entranceDuration,
    "raw": raw, "scripts": scripts.map { ["events": $0, "rows": run($0)] }]
let data = try! JSONSerialization.data(withJSONObject: result, options: [.sortedKeys])
try! data.write(to: URL(fileURLWithPath: CommandLine.arguments[1]), options: .withoutOverwriting)
'''
compiler = out / '.compiler'
compiler.mkdir()
(compiler / 'main.swift').write_text(swift)
env = os.environ.copy()
env.pop('SDKROOT', None)
sdk = subprocess.check_output(['bash', str(root / 'windows/tools/mac_sdk.sh')], text=True, env=env).strip()
cache = compiler / 'module-cache'
subprocess.run(['xcrun', 'swiftc', '-O', '-swift-version', '5', '-sdk', sdk, '-module-cache-path', str(cache),
                str(root / 'Sources/SystemOverlayState.swift'), str(compiler / 'main.swift'), '-o', str(compiler / 'reference')],
               check=True, env=env)
target = out / 'system_overlay_state_source.json'
subprocess.run([str(compiler / 'reference'), str(target)], check=True)
value = json.loads(target.read_text())
value['provenance'] = {'commit': PIN, 'sources': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in sources},
                       'compiledUnchanged': ['Sources/SystemOverlayState.swift'],
                       'extractedVerbatim': ['AppDelegate.openWorkMode', 'AppDelegate.showPreferences', 'AppDelegate.openSettingsModule',
                                             'AppDelegate.toggleSystemOverlay', 'AppDelegate.openSystemOverlay', 'AppDelegate.about',
                                             'AppDelegate.applicationShouldHandleReopen', 'AppDelegate first-run block',
                                             'AppDelegate onQuitAccepted', 'OverlayController.toggleSystemOverlay',
                                             'OverlayController.requestQuit', 'OverlayController.closeSystemOverlay',
                                             'OverlayController.selectSystemModule', 'OverlayController.finishSystemOpening',
                                             'OverlayController.finishSystemClosing', 'OverlayController.lastSystemModule',
                                             'OverlayController.lastModuleDefaults', 'OverlayController.requestAppShortcutLaunch failure',
                                             'OverlayController.returnFromProjection completion', 'HUDOverlayLayout.initialModule',
                                             'HUDOverlayLayout.lastModule', 'HUDOverlayLayout.saveLastModule',
                                             'HUDDesktopWatchNavigation.rightModules', 'HUDModuleLayout order, hidden and restorable'],
                       'modelledFromAssertedLines': ['OverlayController.performSystemAction initial module', 'OverlayController.tearDownSystemPresentation',
                                                     'OverlayController.forceCloseSystemOverlay']}
target.write_text(json.dumps(value, ensure_ascii=False, separators=(',', ':'), sort_keys=True) + '\n')
print('Wrote', target, len(value['scripts']), 'scripts', len(value['raw']), 'raw rows')
