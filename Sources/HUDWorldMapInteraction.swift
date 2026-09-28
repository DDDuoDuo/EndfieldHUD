import AppKit

/// Projects native input and accessibility into the persistent map canvas.
/// Only an in-progress pointer gesture holds the HUD's parallax still.
final class HUDWorldMapInteraction: NSObject {
    private let canvas: WorldMapCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    private var active = false
    private var gestureLocked = false
    private var gestureEnd: DispatchWorkItem?
    private var gestureDeadline: TimeInterval = 0
    private var accessibilityUpdate: DispatchWorkItem?
    private var lastAccessibilityUpdate = -TimeInterval.infinity
    private let timeSource: () -> TimeInterval
    private var buttons: [String: HUDWorldMapActionButton] = [:]
    private let status = HUDWorldMapStatusView(frame: .zero)
    private weak var controlArtwork: CALayer?

    var isInputLocked: Bool { canvas.isDragging || gestureLocked }

    init(canvas: WorldMapCanvas, host: NSView,
         timeSource: @escaping () -> TimeInterval = { ProcessInfo.processInfo.systemUptime }) {
        self.canvas = canvas; self.host = host; self.timeSource = timeSource
        super.init()
        status.setAccessibilityElement(true)
        status.setAccessibilityRole(.staticText)
        status.isHidden = true
        status.projectedFrame = { [weak self, weak host] in
            guard let self, let host, let window = host.window else { return .zero }
            let rect = self.canvas.layer.bounds
            return window.convertToScreen(host.convert(self.project?(rect) ?? rect, to: nil))
        }
        host.addSubview(status)
        canvas.onChange = { [weak self] in self?.requestAccessibilityLayout() }
    }

    deinit { gestureEnd?.cancel(); accessibilityUpdate?.cancel() }

    func setActive(_ value: Bool) {
        guard active != value else { return }
        if value {
            active = true
            canvas.activate()
            layoutAccessibility()
        } else {
            deactivate()
        }
    }

    func deactivate() {
        endGesture()
        canvas.mouseUp()
        active = false
        canvas.deactivate()
        accessibilityUpdate?.cancel(); accessibilityUpdate = nil
        status.isHidden = true
        controlArtwork = nil
        buttons.values.forEach { $0.isHidden = true }
    }

    @discardableResult
    func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, canvas.containsMapPoint(point) else { return false }
        endGesture()
        onLock?()
        guard canvas.mouseDown(at: point) else { return false }
        host?.window?.makeFirstResponder(host)
        return true
    }

    func mouseDragged(to point: CGPoint) {
        // A drag that began inside the map continues beyond its bounds, so
        // the world never sticks when the pointer passes the circular edge.
        guard active, canvas.isDragging else { return }
        canvas.mouseDragged(to: point)
    }

    func mouseUp() {
        guard active else { return }
        canvas.mouseUp()
        layoutAccessibility()
    }

    @discardableResult
    func rightMouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, canvas.containsMapPoint(point) else { return false }
        endGesture()
        onLock?()
        guard canvas.rightMouseDown(at: point) else { return false }
        host?.window?.makeFirstResponder(host)
        return true
    }

    @discardableResult
    func scroll(at point: CGPoint, event: NSEvent) -> Bool {
        guard active else { return false }
        let ending = event.phase.contains(.ended) || event.phase.contains(.cancelled)
            || event.momentumPhase.contains(.ended) || event.momentumPhase.contains(.cancelled)
        guard canvas.containsMapPoint(point) else {
            if ending { endGesture() }
            return false
        }
        let delta = Double(event.scrollingDeltaY)
        if delta.isFinite, delta != 0 {
            holdGesture()
            let sensitivity = event.hasPreciseScrollingDeltas ? 0.012 : 0.12
            let exponent = min(log(2), max(log(0.5), delta * sensitivity))
            canvas.zoom(at: point, factor: exp(exponent))
        }
        if ending { endGesture() }
        return true
    }

    @discardableResult
    func magnify(at point: CGPoint, event: NSEvent) -> Bool {
        guard active else { return false }
        let ending = event.phase.contains(.ended) || event.phase.contains(.cancelled)
        guard canvas.containsMapPoint(point) else {
            if ending { endGesture() }
            return false
        }
        let amount = Double(event.magnification)
        if amount.isFinite, amount != 0 {
            holdGesture()
            canvas.zoom(at: point, factor: min(2, max(0.5, 1 + amount)))
        }
        if ending { endGesture() }
        return true
    }

    func keyDown(_ event: NSEvent) -> Bool {
        guard active,
              event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        endGesture()
        guard canvas.keyDown(keyCode: event.keyCode) else { return false }
        onLock?()
        return true
    }

    func layoutAccessibility() {
        accessibilityUpdate?.cancel(); accessibilityUpdate = nil
        guard active, let host else { return }
        lastAccessibilityUpdate = timeSource()
        let label = canvas.accessibilityStatus
        let help = label + L10n.text(
            ". Drag to pan. Scroll or pinch to zoom. Right-click to place a pin. Arrow keys move the map.",
            "。拖动平移，滚动或捏合缩放，右键放置标记，方向键移动地图。")
        if status.isHidden { status.isHidden = false }
        if status.accessibilityLabel() != label { status.setAccessibilityLabel(label) }
        if status.accessibilityHelp() != help { status.setAccessibilityHelp(help) }
        let content = canvas.layer.bounds
        let statusFrame = project?(content) ?? content
        if status.frame != statusFrame { status.frame = statusFrame }
        let actions = canvas.accessibleActions
        let expected = Set(actions.map(\.id))
        let currentArtwork = canvas.controlHighlightRoot.sublayers?.first
        var controlsChanged = controlArtwork !== currentArtwork
        controlArtwork = currentArtwork
        for id in Array(buttons.keys) where !expected.contains(id) {
            buttons.removeValue(forKey: id)?.removeFromSuperview()
            if !id.hasPrefix("map:pin:") { controlsChanged = true }
        }
        for action in actions {
            let button: HUDWorldMapActionButton
            if let existing = buttons[action.id] {
                button = existing
            } else {
                button = HUDWorldMapActionButton(frame: .zero)
                button.actionID = action.id
                button.title = ""
                button.isBordered = false
                button.focusRingType = .none
                button.target = self
                button.action = #selector(activateAction(_:))
                button.projectedFrame = { [weak self, weak host, weak button] in
                    guard let self, let host, let button, let window = host.window else { return .zero }
                    return window.convertToScreen(host.convert(self.project?(button.sourceRect) ?? button.sourceRect, to: nil))
                }
                host.addSubview(button)
                buttons[action.id] = button
                if !action.id.hasPrefix("map:pin:") { controlsChanged = true }
            }
            if button.isHidden { button.isHidden = false }
            button.sourceRect = action.rect
            if button.isEnabled != action.enabled {
                button.isEnabled = action.enabled
                if !action.id.hasPrefix("map:pin:") { controlsChanged = true }
            }
            if button.accessibilityLabel() != action.label { button.setAccessibilityLabel(action.label) }
            if button.accessibilityHelp() != help { button.setAccessibilityHelp(help) }
            let frame = project?(action.rect) ?? action.rect
            if button.frame != frame {
                button.frame = frame
                if !action.id.hasPrefix("map:pin:") { controlsChanged = true }
            }
        }
        // Camera movement changes pin accessibility frames, not the fixed HUD
        // controls. Avoid scheduling a shell-wide highlight pass for each one.
        if controlsChanged { HUDControlHighlightLayer.requestRefresh(on: host) }
    }

    private func requestAccessibilityLayout() {
        guard active else { return }
        // Camera math and visual input remain immediate. Native AX proxies
        // need only follow the gesture at a bounded rate; their exact final
        // positions are flushed before subsequent actions and on release.
        let remaining = 1.0 / 12 - (timeSource() - lastAccessibilityUpdate)
        guard isInputLocked, remaining > 0 else { layoutAccessibility(); return }
        guard accessibilityUpdate == nil else { return }
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.accessibilityUpdate = nil
            self.layoutAccessibility()
        }
        accessibilityUpdate = work
        DispatchQueue.main.asyncAfter(deadline: .now() + remaining, execute: work)
    }

    private func holdGesture() {
        if !gestureLocked { onLock?() }
        gestureLocked = true
        gestureDeadline = timeSource() + 0.18
        scheduleGestureEnd()
    }

    private func scheduleGestureEnd() {
        guard gestureEnd == nil, gestureLocked else { return }
        // Wheel mice do not send an end phase. One bounded task also releases
        // the lock if a trackpad gesture ends outside the overlay window. New
        // samples extend its deadline instead of allocating one task per event.
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.gestureEnd = nil
            guard self.active, self.gestureLocked else { return }
            if self.timeSource() < self.gestureDeadline { self.scheduleGestureEnd() }
            else { self.endGesture() }
        }
        gestureEnd = work
        DispatchQueue.main.asyncAfter(deadline: .now() + max(0, gestureDeadline - timeSource()), execute: work)
    }

    private func endGesture() {
        gestureEnd?.cancel()
        gestureEnd = nil
        let wasLocked = gestureLocked
        gestureLocked = false
        if wasLocked { canvas.endGesture() }
        layoutAccessibility()
    }

    @objc private func activateAction(_ sender: HUDWorldMapActionButton) {
        guard active, sender.isEnabled else { return }
        endGesture()
        onLock?()
        canvas.perform(actionID: sender.actionID)
    }
}

private final class HUDWorldMapActionButton: NSButton {
    var actionID = ""
    var sourceRect = CGRect.zero
    var projectedFrame: (() -> CGRect)?
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

private final class HUDWorldMapStatusView: NSView {
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}
