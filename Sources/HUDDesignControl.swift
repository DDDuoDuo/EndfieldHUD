import AppKit
import QuartzCore

/// What a control exposes to VoiceOver through its invisible AppKit stand-in.
/// Rects are in the canvas root's coordinates (the content box).
struct HUDAccessibilityItem: Equatable {
    enum Role: Equatable {
        case button
        case toggle(Bool)
        case popUp(String)
        case menuItem(selected: Bool)
        case tab(selected: Bool)
        case slider(value: Double, minimum: Double, maximum: Double, step: Double, description: String)
    }
    let id: String
    let label: String
    let role: Role
    let rect: CGRect
    var enabled = true
}

/// One interactive element of the redesign. It owns its layer, receives hover
/// and press state from the shared highlight pass (HUDControlHighlightLayer
/// targets, driven by the host's pointer events), pointer tracking from its
/// canvas's HUDInteractionRouter, and publishes accessibility items.
class HUDControl {
    let id: String
    let layer: CALayer
    let env: HUDDesignEnvironment
    var label: String
    var onActivate: (() -> Void)?
    private(set) var target: HUDControlHighlightLayer?
    private(set) var isHovered = false
    private(set) var isPressed = false
    var isEnabled = true {
        didSet {
            guard isEnabled != oldValue else { return }
            target?.setEnabled(isEnabled)
            enabledChanged()
        }
    }

    init(id: String, label: String = "", env: HUDDesignEnvironment, layer: CALayer = HUDLayer.plain()) {
        self.id = id; self.label = label; self.env = env; self.layer = layer
        if layer.name == nil { layer.name = "hud.control." + id }
    }

    /// Installs (or moves) the invisible hover/press target, in layer coordinates.
    func setTarget(_ rect: CGRect, path: CGPath? = nil, container: Bool = false) {
        if let target { target.setTarget(rect: rect, path: path); return }
        target = HUDControlHighlightLayer.addTarget(to: layer, rect: rect, path: path, container: container) { [weak self] hovered, pressed in
            self?.receive(hovered: hovered, pressed: pressed)
        }
        target?.setEnabled(isEnabled)
    }

    private func receive(hovered: Bool, pressed: Bool) {
        let hoverChanged = hovered != isHovered, pressChanged = pressed != isPressed
        isHovered = hovered; isPressed = pressed
        if hoverChanged { self.hoverChanged() }
        if pressChanged { self.pressChanged() }
    }

    /// Resets hover/press when the control leaves the screen.
    func clearInteractionState() { receive(hovered: false, pressed: false) }

    func hoverChanged() {}
    func pressChanged() {}
    func enabledChanged() {}

    /// Point in this control's layer coordinates.
    func contains(_ point: CGPoint) -> Bool { (target?.frame ?? layer.bounds).contains(point) }
    /// Returns true to receive the drag and release.
    func pointerDown(at point: CGPoint) -> Bool { isEnabled }
    func pointerDragged(to point: CGPoint) {}
    func pointerUp(at point: CGPoint) { if contains(point) { activate() } }
    func activate() { guard isEnabled else { return }; onActivate?() }

    /// The role VoiceOver sees; nil keeps a control out of the accessibility tree.
    var accessibilityRole: HUDAccessibilityItem.Role? { .button }

    func accessibilityItems(in root: CALayer) -> [HUDAccessibilityItem] {
        guard let role = accessibilityRole, !label.isEmpty else { return [] }
        return [HUDAccessibilityItem(id: id, label: label, role: role, rect: rect(in: root), enabled: isEnabled)]
    }

    /// This control's rect in `root` coordinates (accessibility and menus).
    func rect(in root: CALayer) -> CGRect { root.convert(target?.frame ?? layer.bounds, from: layer) }
}

/// Routes a canvas's pointer events to its controls: the topmost enabled
/// control under the pointer tracks the gesture until release.
final class HUDInteractionRouter {
    private(set) var controls: [HUDControl] = []
    private var tracking: HUDControl?
    var isTracking: Bool { tracking != nil }

    func set(_ controls: [HUDControl]) { self.controls = controls; if let tracking, !controls.contains(where: { $0 === tracking }) { self.tracking = nil } }

    func control(at point: CGPoint, in root: CALayer) -> HUDControl? {
        controls.reversed().first { control in
            control.isEnabled && control.layer.isVisible(in: root) && control.layer.isUnclipped(point, in: root)
                && control.contains(control.layer.convert(point, from: root))
        }
    }

    @discardableResult func pointerDown(at point: CGPoint, in root: CALayer) -> Bool {
        guard let control = control(at: point, in: root) else { tracking = nil; return false }
        tracking = control.pointerDown(at: control.layer.convert(point, from: root)) ? control : nil
        return true
    }
    func pointerDragged(to point: CGPoint, in root: CALayer) {
        guard let tracking else { return }
        tracking.pointerDragged(to: tracking.layer.convert(point, from: root))
    }
    func pointerUp(at point: CGPoint, in root: CALayer) {
        guard let control = tracking else { return }
        tracking = nil
        control.pointerUp(at: control.layer.convert(point, from: root))
    }
    func cancel() { tracking = nil }

    func accessibilityItems(in root: CALayer) -> [HUDAccessibilityItem] {
        controls.filter { $0.layer.isVisible(in: root) }.flatMap { $0.accessibilityItems(in: root) }
    }
}

extension CALayer {
    /// Attached under `root` and not hidden on the way up.
    func isVisible(in root: CALayer) -> Bool {
        var layer: CALayer? = self
        while let current = layer {
            if current.isHidden || current.opacity <= 0.01 { return false }
            if current === root { return true }
            layer = current.superlayer
        }
        return false
    }
}

/// Invisible AppKit stand-ins that give each drawn control a real accessibility
/// element on the tilted plane. Frames are projected by the host; the pointer
/// never reaches them (hitTest is nil), clicks stay with the projected canvas.
final class HUDAccessibilityStandIns: NSObject {
    var project: ((CGRect) -> CGRect)?
    var onPerform: ((String) -> Void)?
    var onAdjust: ((String, Double) -> Void)?
    private weak var host: NSView?
    private var buttons: [String: StandInButton] = [:]
    private var sliders: [String: StandInSlider] = [:]

    init(host: NSView) { self.host = host }

    func update(_ items: [HUDAccessibilityItem], help: String = "") {
        guard let host else { return }
        var wantedButtons = Set<String>(), wantedSliders = Set<String>()
        for item in items {
            let projected = project?(item.rect) ?? item.rect
            if case .slider(let value, let minimum, let maximum, let step, let description) = item.role {
                wantedSliders.insert(item.id)
                let slider = sliders[item.id] ?? makeSlider(item.id, in: host)
                slider.isHidden = false; slider.isEnabled = item.enabled
                slider.minValue = minimum; slider.maxValue = maximum; slider.doubleValue = value; slider.step = step
                slider.frame = projected; slider.projectedFrame = screenFrame(item.rect)
                slider.setAccessibilityLabel(item.label); slider.setAccessibilityHelp(description)
                slider.setAccessibilityValueDescription(description)
            } else {
                wantedButtons.insert(item.id)
                let button = buttons[item.id] ?? makeButton(item.id, in: host)
                button.isHidden = false; button.isEnabled = item.enabled
                button.frame = projected; button.projectedFrame = screenFrame(item.rect)
                button.setAccessibilityLabel(item.label); button.setAccessibilityHelp(help.isEmpty ? nil : help)
                switch item.role {
                case .toggle(let on):
                    button.setAccessibilityRole(.checkBox); button.setAccessibilitySubrole(NSAccessibility.Subrole(rawValue: "AXSwitch"))
                    button.setAccessibilityValue(NSNumber(value: on ? 1 : 0))
                case .popUp(let value):
                    button.setAccessibilityRole(.popUpButton); button.setAccessibilitySubrole(nil); button.setAccessibilityValue(value)
                case .menuItem(let selected):
                    button.setAccessibilityRole(.menuItem); button.setAccessibilitySubrole(nil)
                    button.setAccessibilitySelected(selected); button.setAccessibilityValue(nil)
                case .tab(let selected):
                    button.setAccessibilityRole(.radioButton); button.setAccessibilitySubrole(NSAccessibility.Subrole(rawValue: "AXTabButton"))
                    button.setAccessibilityValue(NSNumber(value: selected ? 1 : 0))
                case .button, .slider:
                    button.setAccessibilityRole(.button); button.setAccessibilitySubrole(nil); button.setAccessibilityValue(nil)
                }
            }
        }
        for id in Array(buttons.keys) where !wantedButtons.contains(id) { buttons.removeValue(forKey: id)?.removeFromSuperview() }
        for id in Array(sliders.keys) where !wantedSliders.contains(id) { sliders.removeValue(forKey: id)?.removeFromSuperview() }
    }

    func hideAll() {
        buttons.values.forEach { $0.isHidden = true }
        sliders.values.forEach { $0.isHidden = true }
    }

    var identifiers: Set<String> { Set(buttons.keys).union(sliders.keys) }

    private func makeButton(_ id: String, in host: NSView) -> StandInButton {
        let button = StandInButton(frame: .zero)
        button.controlID = id; button.isBordered = false; button.title = ""
        button.target = self; button.action = #selector(performStandIn(_:))
        host.addSubview(button); buttons[id] = button
        return button
    }
    private func makeSlider(_ id: String, in host: NSView) -> StandInSlider {
        let slider = StandInSlider(frame: .zero)
        slider.controlID = id; slider.isContinuous = true
        slider.target = self; slider.action = #selector(adjustStandIn(_:))
        host.addSubview(slider); sliders[id] = slider
        return slider
    }
    private func screenFrame(_ rect: CGRect) -> () -> CGRect {
        { [weak self] in
            guard let self, let host = self.host, let window = host.window else { return .zero }
            return window.convertToScreen(host.convert(self.project?(rect) ?? rect, to: nil))
        }
    }
    @objc private func performStandIn(_ sender: StandInButton) { if sender.isEnabled { onPerform?(sender.controlID) } }
    @objc private func adjustStandIn(_ sender: StandInSlider) { if sender.isEnabled { onAdjust?(sender.controlID, sender.doubleValue) } }

    private final class StandInButton: NSButton {
        var controlID = ""
        var projectedFrame: (() -> CGRect)?
        override func draw(_ dirtyRect: NSRect) {}
        override func hitTest(_ point: NSPoint) -> NSView? { nil }
        override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
    }

    private final class StandInSlider: NSSlider {
        var controlID = ""
        var step: Double = 1
        var projectedFrame: (() -> CGRect)?
        override func draw(_ dirtyRect: NSRect) {}
        override func hitTest(_ point: NSPoint) -> NSView? { nil }
        override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
        override func setAccessibilityValue(_ value: Any?) {
            guard isEnabled, let number = value as? NSNumber, number.doubleValue.isFinite else { return }
            doubleValue = min(maxValue, max(minValue, number.doubleValue)); _ = sendAction(action, to: target)
        }
        override func accessibilityPerformIncrement() -> Bool { adjust(1) }
        override func accessibilityPerformDecrement() -> Bool { adjust(-1) }
        private func adjust(_ direction: Double) -> Bool {
            guard isEnabled else { return false }
            setAccessibilityValue(NSNumber(value: doubleValue + direction * step)); return true
        }
    }
}
