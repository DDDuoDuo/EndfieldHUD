#!/usr/bin/env python3
"""Power/charge oracle: extract unchanged Mac source bodies into a data-only
fixture harness, then compact the Swift reference output for native tests.

  charge_indicator_reference.py ROOT OUTPUT        # extraction (no app/HUD)
  charge_indicator_reference.py --compact OUTPUT   # writes charge-indicator-source.json

The Swift harness never constructs AppDelegate, OverlayController, the HUD or a
battery monitor, and reads no power source, user data, clipboard or account."""
import hashlib, json, pathlib, sys

PINS = {
    'Sources/ChargeIndicatorView.swift': 'ec9e4e5237c2b3ba9c6a5b4ffb55daef5db682ec575687bbbd2f0efe6852857c',
    'Sources/HUDChargeBadge.swift': '255bf189221a71518e6897878028bd82933cff4de0ca32f4860e22d1b763105f',
    'Sources/HUDChargeMetric.swift': '258ade1b0ae53eb3ff588b6eec8e52a9d4981122a929c7c20aba31e61ef11083',
    'Sources/DisplayPolicy.swift': '13145948f98f06a9645fe07667c5b4c4a3ab1094defeafe025091c96d49e5968',
    'Sources/OverlayGeometry.swift': '92061517d4f3c8571966de77f59c4a2fb241b2cd6b367823fec9fea63c404d60',
    'Sources/DeviceBatteryProvider.swift': '0514115fcb7947d8f4bfa32ca905ba260bf2ea9b1876364ec64e691c5c8bbfe4',
    'Sources/SystemEventRecorder.swift': '4d5851f6767c463a5eda7ed1b44d29a6793e05313a7149a45230bc020f77b672',
    'Sources/AppDelegate.swift': '63714bd1ca2fce863c185cbe72dfb033fc8b510c69a2050d95264dd466eac080',
    'Sources/Models.swift': '5ce49023ae20e159aa574b7948b09140edf5717276d4d8949d7ce87ddf6d8967',
    'Sources/HUDDeploymentFlicker.swift': 'e70a01035068f06ed4b82baf814e86450d49d8db8073cfef5a03532ac010f0b2',
    'Sources/BatteryMonitor.swift': 'cda4c34ea3f010e3e45b434c8aa5b81d374ef105a2d6fbbd4f1d81f95f4274e4',
    'Sources/BatteryCapacity.swift': '491ae8673fa46391788a6c7a0abb01b25fe5b545576a59a5a127381b7e7a166e',
    'Sources/OverlayController.swift': 'f88a13bf51167c4e5fc199747be8befae15a3e3ba0d1aaf7ae2fae07db684de2',
    'Sources/SystemHUDView.swift': 'b2ab7090ad0a44c6bc4acbcaf6169b21983b138d868ed6135e4114791725d42f',
}


def block(text, marker):
    assert text.count(marker) == 1, marker
    start = text.index(marker); opening = text.index('{', start); level = 1; end = opening + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]


def between(text, start, end):
    assert text.count(start) == 1 and text.count(end) == 1, (start, end)
    return text[text.index(start):text.index(end, text.index(start))]


def line(text, needle):
    rows = [row for row in text.splitlines() if needle in row]
    assert len(rows) == 1, needle
    return rows[0].strip()


def compact(out):
    reference = json.loads((out / 'reference.json').read_text())
    provenance = json.loads((out / 'provenance.json').read_text())
    nodes, identities = [], {}

    def intern(layer):
        value = dict(layer)
        value['children'] = [intern(child) for child in value.get('children', [])]
        key = json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False)
        if key not in identities:
            identities[key] = len(nodes); nodes.append(value)
        return identities[key]

    trees = []
    for original in reference['trees']:
        item = {key: value for key, value in original.items() if key != 'layer'}
        item['root'] = intern(original['layer'])
        trees.append(item)
    unsupported = reference['unsupported']
    # Only the known source shadow on the backing shadow layer is outside the
    # plain layer rasterizer; it is reproduced by the native scene.
    assert all(issue['category'] in ('metadata-only-animation',) or issue['feature'] in ('layer delegate (not executed)',)
               for issue in unsupported), unsupported[:4]
    fixture = {key: value for key, value in reference.items() if key not in ('trees', 'unsupported')}
    fixture.update({'sourceSHA256': provenance['sources'], 'constants': provenance['constants'],
                    'nodes': nodes, 'trees': trees,
                    'scope': 'Detached unmodified ChargeIndicatorView/HUDChargeBadge/policy bodies, frozen Core Animation '
                             'clock. No live HUD, OverlayController, AppDelegate, battery provider or user data.'})
    (out / 'charge-indicator-source.json').write_text(json.dumps(fixture, separators=(',', ':'), ensure_ascii=False) + '\n')
    print(f"Exported {len(trees)} indicator trees, {len(nodes)} unique nodes, "
          f"{sum(len(t['samples']) for t in reference['timelines'])} timeline samples")


def extract(root, out):
    out.mkdir(parents=True, exist_ok=True)
    for name, expected in PINS.items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, \
            'Source changed; review extraction before updating pin: ' + name
    models = (root / 'Sources/Models.swift').read_text()
    recorder = (root / 'Sources/SystemEventRecorder.swift').read_text()
    delegate = (root / 'Sources/AppDelegate.swift').read_text()
    indicator = (root / 'Sources/ChargeIndicatorView.swift').read_text()
    badge = (root / 'Sources/HUDChargeBadge.swift').read_text()
    overlay = (root / 'Sources/OverlayController.swift').read_text()
    hud = (root / 'Sources/SystemHUDView.swift').read_text()

    normalization = between(models, '        let raw = accentHex.trimmingCharacters', '        result.hudScale = Self.clamp(')
    fixture = '''import AppKit
import QuartzCore
// Data-only harness around extracted Models.swift, SystemEventRecorder and
// AppDelegate battery bodies. Everything else is an explicit inert stand-in.
''' + block(models, 'enum OverlayTheme: String') + '\n' + block(models, 'enum OverlayPlacement: String') + '\n' \
        + block(models, 'struct OverlayPosition: Equatable') + '''
struct AppConfiguration: Equatable {
    var displayMode: DisplayMode
    var displayDuration: Double
    var accentHex: String
    var theme: OverlayTheme = .dark
    var scale: Double = 1
    var placement: OverlayPlacement = .topCenter
    var customPosition: OverlayPosition = OverlayPosition()
    var batteryAlertsEnabled = true
    var alertMetric: HUDChargeMetric = .battery
    static let defaults = AppConfiguration(displayMode: .whenChargingStarts, displayDuration: 3, accentHex: "FAD41F")
    var normalized: AppConfiguration {
''' + normalization + '''        return result
    }
''' + block(models, '    var accentColor: NSColor') + '''
}
enum RecorderKind: String { case powerConnected, powerDisconnected, batteryStateChanged }
final class RecorderLog { var rows: [[String: Any]] = []
    func record(kind: RecorderKind, metadata: [String: String]) { rows.append(["kind": kind.rawValue, "metadata": metadata]) } }
final class BatteryRecorderFixture {
    let log = RecorderLog()
    private var battery: BatterySnapshot?
''' + block(recorder, '    func receiveBattery(_ value: BatterySnapshot)') + '\n' \
        + block(recorder, '    private static func batteryState(_ value: BatterySnapshot)').replace('private ', '', 1) + '''
}
final class OverlayStub {
    var log: [String] = []
    var isVisible = false, isPersistent = false, isEditingPosition = false
    var isSystemOverlayActive = false, isProjectionActive = false
    let eventRecorder = BatteryRecorderFixture()
    static func describe(_ s: BatterySnapshot) -> String {
        "p=\\(s.percentage.map(String.init) ?? "nil"),b=\\(s.hasBattery ? 1 : 0),a=\\(s.isPluggedIn ? 1 : 0),c=\\(s.isCharging ? 1 : 0),f=\\(s.isFullyCharged ? 1 : 0)"
    }
    func update(snapshot: BatterySnapshot, configuration: AppConfiguration, preview: Bool = false) {
        log.append("update(\\(Self.describe(snapshot)),preview=\\(preview ? 1 : 0))") }
    func show(persistent: Bool, duration: Double, replay: Bool = false) {
        // OverlayController.show guards; visibility follows its synchronous state.
        guard !isEditingPosition, !isSystemOverlayActive, !isProjectionActive else { log.append("show-ignored"); return }
        log.append("show(persistent=\\(persistent ? 1 : 0),duration=\\(duration),replay=\\(replay ? 1 : 0))")
        isVisible = true; isPersistent = persistent }
    func hide() { log.append("hide"); isPersistent = false; isVisible = false }
}
final class MenuItemStub { var title = "" }
final class StatusButtonStub { var toolTip: String? }
final class StatusItemStub { var button: StatusButtonStub? = StatusButtonStub() }
final class ConfigurationSource { var configuration = AppConfiguration.defaults }
final class AppDelegateBatteryFixture {
    let overlay = OverlayStub()
    let store = ConfigurationSource(), hudSettings = ConfigurationSource()
    let batteryMenuItem = MenuItemStub()
    var statusItem: StatusItemStub? = StatusItemStub()
    var terminating = false, suspended = false
    var snapshot: BatterySnapshot?
    var presentedSnapshot: BatterySnapshot?
    var previewSnapshot: BatterySnapshot?
''' + '\n'.join(block(delegate, marker).replace('@objc ', '', 1).replace('private ', '', 1) for marker in [
        '    private func receive(_ next: BatterySnapshot)', '    private func presentLatestSnapshot()',
        '    private func apply(_ action: DisplayAction)', '    private func updateMenu(_ value: BatterySnapshot)',
        '    private var previewValue: BatterySnapshot', '    @objc private func preview(_ sender: Any?)']) + '\n' \
        + between(delegate, '    private static let demoSnapshot = BatterySnapshot(', '    private func editPosition()').replace('private ', '', 1) + '''
}
'''
    (out / 'ChargeReferenceFixture.swift').write_text(fixture)
    constants = {
        'indicatorCanvas': line(indicator, 'static let canvasSize = NSSize('),
        'indicatorEntrance': line(indicator, 'static let entranceDuration: TimeInterval'),
        'indicatorExit': line(indicator, 'static let exitDuration: TimeInterval'),
        'bannerTitleWidth': line(indicator, 'private static let bannerTitleWidth'),
        'entranceSequence': between(indicator, '        apply(.hidden, duration: 0)\n        apply(.circle, duration: 0.18)',
                                    '        schedule(after: Self.entranceDuration, token: token)'),
        'exitSequence': between(indicator, '        apply(.circle, duration: 0.30)\n        schedule(after: 0.30',
                                '            CATransaction.setCompletionBlock'),
        'hoverDuration': line(indicator, 'let duration = animated && !reduceMotion ? 0.16 : 0'),
        'morphDuration': '0.26 x%d (setStage, morphEmbeddedStage)' % indicator.count('apply(stage, duration: animated && !reduceMotion ? 0.26 : 0)'),
        'badgeCenter': line(badge, 'static let center = CGPoint('),
        'badgeScale': line(badge, 'static let rendererScale: CGFloat'),
        'badgeEntranceDelay': line(badge, 'static let entranceDelay: TimeInterval'),
        'badgeCompactHold': line(badge, 'static let compactHoldDuration: TimeInterval'),
        'badgeEntranceFlicker': line(badge, 'HUDDeploymentFlicker.apply(to: [badge.renderer.embeddedContentLayer], opening: true,') + ' '
                                + line(badge, 'duration: 0.18, delay: 0.14,'),
        'badgeExitFlicker': line(badge, 'duration: 0.11, delay: 0.07, reducedMotion: HUDRuntimeAppearance.reduceMotion)'),
        'badgeHoverInset': line(badge, 'let rect = isHovered ? Self.compactHitRect.insetBy(dx: -2.5, dy: -2.5) : hitRect'),
        'chargeSourceOffset': line(hud, 'private var chargeSourceOffset: CGFloat { usesSourceShell ? 38 : 0 }'),
        'positionEditorLayout': between(overlay, '        let height = isEditingPosition ? max(canvasHeight, 66 * scale + 34) : canvasHeight',
                                        '    private var isDark: Bool'),
        'positionButtonDraw': block(overlay, '    override func draw(_ dirtyRect: NSRect)'),
        'positionKeys': line(overlay, 'if event.keyCode == 53 { onCancel?() }') + ' ' + line(overlay, 'else if event.keyCode == 36 || event.keyCode == 76 { onConfirm?() }'),
        'dismissalClamp': line(overlay, 'requestedDuration = duration.isFinite ? min(60, max(1, duration)) : 5'),
        'panelLevel': line(overlay, 'panel.level = .statusBar'),
        'panelBehavior': line(overlay, 'panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .ignoresCycle]'),
    }
    pins = {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in PINS}
    (out / 'provenance.json').write_text(json.dumps({'sources': pins, 'sourceModified': False, 'liveHUDConstructed': False,
                                                     'constants': constants}, indent=2, ensure_ascii=False) + '\n')


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--compact':
        compact(pathlib.Path(sys.argv[2]))
    elif len(sys.argv) == 3:
        extract(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
    else:
        sys.exit('Usage: charge_indicator_reference.py ROOT OUTPUT | --compact OUTPUT')
