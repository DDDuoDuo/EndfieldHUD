import AppKit
import QuartzCore

struct WorkModeCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
}

enum WorkModeDialGeometry {
    static let canvas = CGRect(x: 0, y: 0, width: 440, height: 440)
    static let center = CGPoint(x: 220, y: 220)
    static let radius: CGFloat = 215

    /// This explicit path starts at twelve o'clock and advances toward three
    /// o'clock in the HUD's flipped coordinates. Reducing strokeEnd therefore
    /// moves the remaining arc's endpoint counter-clockwise: top, left, bottom,
    /// right, top. An ellipse's unspecified start point cannot guarantee this.
    static func countdownPath() -> CGPath {
        let path = CGMutablePath()
        path.addArc(center: center, radius: radius, startAngle: -.pi / 2,
                    endAngle: 3 * .pi / 2, clockwise: false)
        return path
    }

    static func remainingFraction(_ value: WorkModeSnapshot) -> Double {
        guard value.duration > 0 else { return 0 }
        return min(1, max(0, value.remaining / value.duration))
    }
}

/// Retained dial and labels; one-second notifications change the timer string
/// only. Core Animation advances the ring independently of AppKit drawing.
final class WorkModeCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    let controller: WorkModeController
    var onChange: (() -> Void)?
    var onEditDuration: ((CGRect) -> Void)?
    var onRequestFocusAccess: (() -> Void)?
    private let heading = CATextLayer()
    private let digits = CATextLayer()
    private let clockClip = CALayer()
    private let stateLabel = CATextLayer()
    private let ringBase = CAShapeLayer()
    private let ring = CAShapeLayer()
    private let controls = CALayer()
    private let configurationControls = CALayer()
    private let focusStatus = CATextLayer()
    private let focusAccessPlate = CAShapeLayer()
    private var focusAccessAvailable = false
    private var actionLayers: [String: CALayer] = [:]
    private let shouldReduceMotion: () -> Bool
    private var layoutExpanded: Bool?
    private var layoutGeneration = 0
    private var configurationInteractive = true
    static let layoutTransitionDuration: TimeInterval = 0.44
    private var observer: UUID?
    private var active = false
    private var dark = true
    private var scale: CGFloat = 2
    private var renderedRevision = -1
    private var animationRevision = -1
    private var reducedMotion = false
    private var error: String?
    private var lastTimeText = ""
    private var yellow: NSColor { HUDRuntimeAppearance.accent }
    private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.11, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.64 : 0.42, alpha: 1) }
    private var ink: NSColor { NSColor(white: 0.13, alpha: 1) }

    var durationEditorRect: CGRect { CGRect(x: 64, y: 184, width: 312, height: 68) }
    var accessibilityStatus: String {
        if let error { return error }
        let value = controller.snapshot
        let status = (value.kind == .countdown ? L10n.text("Countdown", "倒计时") : L10n.text("Stopwatch", "秒表"))
            + ", " + value.timeText + ", " + phaseTitle(value.phase)
        if !focusStatus.isHidden, let message = focusStatus.string as? String { return status + ". " + message }
        return status
    }

    var accessibleActions: [WorkModeCanvasAction] {
        let value = controller.snapshot
        var actions = value.isActive || !configurationInteractive ? [] : configurationActions(value)
        let verbs: [(String, String)]
        switch value.phase {
        case .running: verbs = [("pause", L10n.text("Pause", "暂停")), ("reset", L10n.text("Reset", "重置"))]
        case .paused: verbs = [("resume", L10n.text("Resume", "继续")), ("reset", L10n.text("Reset", "重置"))]
        case .idle, .stopped, .completed: verbs = [("start", L10n.text("Start", "开始")), ("reset", L10n.text("Reset", "重置"))]
        }
        // Leave the bottom of the full-size dial available for the shared
        // charge badge and two persistent center navigation buttons.
        let width = (240 - CGFloat(verbs.count - 1) * 8) / CGFloat(verbs.count)
        for (index, verb) in verbs.enumerated() {
            actions.append(WorkModeCanvasAction(id: "work:" + verb.0, label: verb.1,
                rect: CGRect(x: 100 + CGFloat(index) * (width + 8), y: 296, width: width, height: 30)))
        }
        if focusAccessAvailable && !focusStatus.isHidden {
            actions.append(WorkModeCanvasAction(id: "work:focusAccess", label: L10n.text("Open Accessibility Settings", "打开辅助功能设置"), rect: focusStatus.frame))
        }
        return actions
    }

    private func configurationActions(_ value: WorkModeSnapshot) -> [WorkModeCanvasAction] {
        var actions = [
            WorkModeCanvasAction(id: "work:countdown", label: L10n.text("Countdown", "倒计时"), rect: CGRect(x: 94, y: 86, width: 122, height: 27)),
            WorkModeCanvasAction(id: "work:stopwatch", label: L10n.text("Stopwatch", "秒表"), rect: CGRect(x: 224, y: 86, width: 122, height: 27))
        ]
        if value.kind == .countdown {
            for (index, minutes) in [5, 30, 60].enumerated() {
                actions.append(WorkModeCanvasAction(id: "work:preset:\(minutes)", label: L10n.text("\(minutes) min", "\(minutes) 分"),
                    rect: CGRect(x: 94 + CGFloat(index) * 65, y: 127, width: 57, height: 25)))
            }
            actions.append(WorkModeCanvasAction(id: "work:custom", label: L10n.text("Custom", "自定"), rect: CGRect(x: 289, y: 127, width: 57, height: 25)))
        }
        return actions
    }

    init(controller: WorkModeController, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.controller = controller
        shouldReduceMotion = reduceMotion
        super.init()
        layer.name = "module.workMode.canvas"
        layer.frame = WorkModeDialGeometry.canvas
        layer.allowsGroupOpacity = false
        heading.name = "workMode.heading"
        heading.frame = CGRect(x: 130, y: 53, width: 180, height: 20)
        configure(heading, size: 13, weight: .semibold)
        heading.alignmentMode = .center
        layer.addSublayer(heading)
        for shape in [ringBase, ring] {
            shape.frame = layer.bounds
            shape.path = WorkModeDialGeometry.countdownPath()
            shape.fillColor = nil; shape.lineWidth = 4; shape.lineCap = .butt
            layer.addSublayer(shape)
        }
        ringBase.lineWidth = 1.5
        ring.name = "workMode.ring"
        ringBase.name = "workMode.ringBase"
        digits.name = "workMode.clock"
        clockClip.name = "workMode.clockViewport"
        clockClip.frame = CGRect(x: 48, y: 180, width: 344, height: 77)
        clockClip.masksToBounds = true
        layer.addSublayer(clockClip)
        digits.frame = clockClip.bounds
        configure(digits, size: 60, weight: .medium)
        digits.font = NSFont.monospacedDigitSystemFont(ofSize: 60, weight: .medium)
        digits.alignmentMode = .center
        clockClip.addSublayer(digits)
        stateLabel.name = "workMode.phase"
        stateLabel.frame = CGRect(x: 89, y: 261, width: 262, height: 30)
        configure(stateLabel, size: 11, weight: .medium)
        stateLabel.alignmentMode = .center
        stateLabel.isWrapped = true
        layer.addSublayer(stateLabel)
        controls.frame = layer.bounds; controls.allowsGroupOpacity = false
        controls.name = "workMode.controls"
        layer.addSublayer(controls)
        configurationControls.frame = layer.bounds
        configurationControls.name = "workMode.configuration"
        layer.addSublayer(configurationControls)
        focusStatus.name = "workMode.focusStatus"
        focusStatus.frame = CGRect(x: 60, y: 25, width: 320, height: 24)
        configure(focusStatus, size: 9.5, weight: .regular)
        focusStatus.alignmentMode = .center
        focusStatus.isWrapped = true
        focusStatus.isHidden = true
        focusAccessPlate.frame = focusStatus.frame
        focusAccessPlate.path = cutCorner(focusAccessPlate.bounds)
        focusAccessPlate.lineWidth = 0.8
        focusAccessPlate.isHidden = true
        layer.addSublayer(focusAccessPlate)
        HUDControlHighlightLayer.add(to: focusAccessPlate, rect: focusAccessPlate.bounds, shape: .cutCorner, framed: true)
        layer.addSublayer(focusStatus)
        observer = controller.observe { [weak self] in
            guard let self, self.active else { return }
            self.refresh()
        }
        refresh(force: true, animateLayout: false)
    }

    deinit {
        if let observer { controller.removeObserver(observer) }
        // The session belongs to the controller; the visible ticker and
        // compositor tracks belong to this canvas and must end with it.
        if active { controller.setVisible(false) }
        removeFeedback(); settleLayout(); ring.removeAllAnimations()
    }

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; scale = style.contentsScale
        refresh(force: true, animateLayout: false)
        return layer
    }

    func activate() {
        guard !active else { return }
        active = true
        controller.setVisible(true)
        refresh(force: true, animateLayout: false)
    }
    func deactivate() {
        active = false
        removeFeedback()
        settleLayout()
        ring.removeAllAnimations()
        animationRevision = -1
        controller.setVisible(false)
        withoutActions { self.updateRing(self.controller.snapshot, force: false) }
    }

    func updateRenderScale(_ value: CGFloat) {
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard next != scale else { return }
        scale = next
        if active { refresh(force: true) }
    }

    /// Real Focus errors are supplied by the app owner. Normal operation has no
    /// Focus badge, button, or implied system state, and never retimes the clock.
    func setFocusStatusMessage(_ value: String?, needsAccessibilityPermission: Bool = false) {
        let message = value?.trimmingCharacters(in: .whitespacesAndNewlines)
        withoutActions {
            focusAccessAvailable = needsAccessibilityPermission && message?.isEmpty == false
            focusStatus.string = message
            focusStatus.isHidden = message?.isEmpty != false
            updateFocusStatusAppearance()
        }
        onChange?()
    }

    private func updateFocusStatusAppearance() {
        focusStatus.foregroundColor = (focusAccessAvailable ? yellow : muted).cgColor
        focusAccessPlate.isHidden = !focusAccessAvailable
        focusAccessPlate.fillColor = muted.withAlphaComponent(0.08).cgColor
        focusAccessPlate.strokeColor = yellow.withAlphaComponent(0.5).cgColor
    }

    func refreshMotionPreference() {
        animationRevision = -1
        if shouldReduceMotion() { removeFeedback(); settleLayout() }
        refresh()
    }

    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard point.x.isFinite, point.y.isFinite, layer.bounds.contains(point) else { return false }
        if let action = accessibleActions.first(where: { $0.rect.contains(point) }) { perform(actionID: action.id) }
        return true
    }

    func perform(actionID: String) {
        guard accessibleActions.contains(where: { $0.id == actionID }) else { return }
        if actionID == "work:focusAccess" {
            if active && !shouldReduceMotion() {
                let highlight = CABasicAnimation(keyPath: "opacity")
                highlight.fromValue = 0.55; highlight.toValue = 1; highlight.duration = 0.16
                focusAccessPlate.add(highlight, forKey: "workFeedback.focusAccess")
            }
            onRequestFocusAccess?()
            return
        }
        let previous = controller.snapshot
        let revision = controller.revision
        error = nil
        switch actionID {
        case "work:countdown":
            if controller.snapshot.kind != .countdown { _ = controller.chooseCountdown(seconds: controller.snapshot.duration) }
        case "work:stopwatch":
            if controller.snapshot.kind != .stopwatch { controller.chooseStopwatch() }
        case "work:custom": onEditDuration?(durationEditorRect)
        case "work:start": controller.start()
        case "work:pause": controller.pause()
        case "work:resume": controller.resume()
        case "work:reset": controller.reset()
        default:
            if actionID.hasPrefix("work:preset:"), let minutes = Double(actionID.dropFirst("work:preset:".count)),
               [5.0, 30, 60].contains(minutes) { _ = controller.chooseCountdown(seconds: minutes * 60) }
        }
        if controller.revision == revision { refresh() }
        animateAction(actionID, from: previous, changed: controller.revision != revision)
    }

    @discardableResult func setCustomDuration(_ text: String) -> Bool {
        if controller.snapshot.isActive {
            // A native field opened before an external Start cannot reconfigure
            // an active session. Re-entering its existing value is harmless.
            return controller.snapshot.kind == .countdown && WorkModeDuration.parse(text) == controller.snapshot.duration
        }
        guard let seconds = WorkModeDuration.parse(text) else {
            error = L10n.text("Use 0:01–1440:00 (min:sec)", "输入 0:01–1440:00（分:秒）")
            refresh(); return false
        }
        error = nil
        // Inspecting or re-entering the current duration must not reset an
        // active session; a changed duration intentionally prepares a new one.
        let current = controller.snapshot
        if current.kind == .countdown && current.duration == seconds {
            refresh()
            return true
        }
        let changed = controller.chooseCountdown(seconds: seconds)
        if changed { animateAction("work:custom", from: current, changed: true) }
        return changed
    }

    /// Cancelling the native editor clears its validation message without
    /// changing the timer's duration, elapsed time, phase, or deadline.
    func cancelCustomEditing() {
        guard error != nil else { return }
        error = nil
        refresh()
    }

    private func refresh(force: Bool = false, animateLayout: Bool = true) {
        let value = controller.snapshot
        if !active || controller.isSuspended || shouldReduceMotion() { removeFeedback() }
        withoutActions {
            if self.lastTimeText != value.timeText || force {
                self.lastTimeText = value.timeText; self.digits.string = value.timeText
            }
            self.stateLabel.string = self.error ?? self.phaseTitle(value.phase)
            if force || self.renderedRevision != self.controller.revision {
                self.renderedRevision = self.controller.revision
                self.heading.string = HUDSectionHeading.text(HUDModule.workMode.title)
                self.heading.foregroundColor = self.primary.cgColor
                self.digits.foregroundColor = self.primary.cgColor
                self.stateLabel.foregroundColor = self.muted.cgColor
                self.ringBase.strokeColor = self.muted.withAlphaComponent(0.25).cgColor
                self.ring.strokeColor = self.yellow.cgColor
                self.updateFocusStatusAppearance()
                for text in [self.heading, self.digits, self.stateLabel, self.focusStatus] {
                    text.contentsScale = HUDRenderScale.contentScale(for: text, baseScale: self.scale)
                }
                self.renderControls(value)
            }
            self.updateRing(value, force: force)
        }
        updateLayout(expanded: value.isActive, animated: animateLayout && active && !controller.isSuspended && !shouldReduceMotion())
        onChange?()
    }

    private func updateRing(_ value: WorkModeSnapshot, force: Bool) {
        let reduce = shouldReduceMotion()
        let animate = active && !controller.isSuspended && value.phase == .running && !reduce
        ring.lineWidth = value.kind == .countdown ? 4 : 2.5
        if !animate {
            ring.removeAllAnimations()
            ring.strokeEnd = value.kind == .countdown ? WorkModeDialGeometry.remainingFraction(value) : 0.045
            let angle = value.kind == .countdown ? 0 : value.progress * 2 * .pi
            ring.transform = CATransform3DMakeRotation(angle, 0, 0, 1)
            animationRevision = -1; reducedMotion = reduce
            return
        }
        guard force || animationRevision != controller.revision || reducedMotion != reduce else { return }
        ring.removeAllAnimations()
        animationRevision = controller.revision; reducedMotion = reduce
        if value.kind == .countdown {
            ring.transform = CATransform3DIdentity
            ring.strokeEnd = 0
            let animation = CABasicAnimation(keyPath: "strokeEnd")
            animation.fromValue = WorkModeDialGeometry.remainingFraction(value); animation.toValue = 0
            animation.duration = max(0.001, value.remaining)
            animation.timingFunction = CAMediaTimingFunction(name: .linear)
            ring.add(animation, forKey: "workMode.countdown")
        } else {
            ring.strokeEnd = 0.045
            let angle = value.progress * 2 * .pi
            ring.transform = CATransform3DMakeRotation(angle, 0, 0, 1)
            let animation = CABasicAnimation(keyPath: "transform.rotation.z")
            animation.fromValue = angle; animation.toValue = angle + 2 * .pi
            animation.duration = 60; animation.repeatCount = .infinity
            animation.timingFunction = CAMediaTimingFunction(name: .linear)
            ring.add(animation, forKey: "workMode.stopwatch")
        }
    }

    /// Configuration opacity and clock geometry have independent finite tracks.
    /// Their model values always describe the destination, so removal, hiding,
    /// or reduced-motion changes settle without a delayed layout callback.
    private func updateLayout(expanded: Bool, animated: Bool) {
        let changed = layoutExpanded != expanded
        let hasAnimations = [configurationControls, clockClip, stateLabel].contains {
            ($0.animationKeys() ?? []).contains { $0.hasPrefix("workLayout.") }
        }
        guard changed || (!animated && hasAnimations) else { return }
        let hadLayout = layoutExpanded != nil
        layoutExpanded = expanded
        layoutGeneration += 1
        let token = layoutGeneration
        let fromOpacity = configurationControls.presentation()?.opacity ?? configurationControls.opacity
        let fromPosition = clockClip.presentation()?.position ?? clockClip.position
        let fromTransform = clockClip.presentation()?.transform ?? clockClip.transform
        let fromStatePosition = stateLabel.presentation()?.position ?? stateLabel.position
        for item in [configurationControls, clockClip, stateLabel] {
            for key in item.animationKeys() ?? [] where key.hasPrefix("workLayout.") { item.removeAnimation(forKey: key) }
        }
        let shouldAnimate = animated && hadLayout && changed
        configurationInteractive = !expanded && !shouldAnimate
        withoutActions {
            configurationControls.opacity = expanded ? 0 : 1
            clockClip.position = CGPoint(x: 220, y: expanded ? 166 : 218.5)
            clockClip.transform = expanded ? CATransform3DMakeScale(1.28, 1.28, 1) : CATransform3DIdentity
            digits.contentsScale = HUDRenderScale.contentScale(for: digits, baseScale: scale)
            stateLabel.position = CGPoint(x: 220, y: expanded ? 244 : 276)
        }
        guard shouldAnimate else { return }
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self] in
            DispatchQueue.main.async { [weak self] in
                guard let self, self.active, self.layoutGeneration == token, !self.controller.snapshot.isActive else { return }
                self.configurationInteractive = true
                self.onChange?()
            }
        }
        addLayoutAnimation(configurationControls, keyPath: "opacity", from: fromOpacity, to: configurationControls.opacity)
        addLayoutAnimation(clockClip, keyPath: "position", from: NSValue(point: fromPosition), to: NSValue(point: clockClip.position))
        addLayoutAnimation(clockClip, keyPath: "transform", from: NSValue(caTransform3D: fromTransform), to: NSValue(caTransform3D: clockClip.transform))
        addLayoutAnimation(stateLabel, keyPath: "position", from: NSValue(point: fromStatePosition), to: NSValue(point: stateLabel.position))
        CATransaction.commit()
    }

    private func addLayoutAnimation(_ item: CALayer, keyPath: String, from: Any, to: Any) {
        let animation = CABasicAnimation(keyPath: keyPath)
        animation.fromValue = from; animation.toValue = to
        animation.duration = Self.layoutTransitionDuration
        animation.timingFunction = CAMediaTimingFunction(controlPoints: 0.22, 0.68, 0.24, 1)
        item.add(animation, forKey: "workLayout." + keyPath)
    }

    private func settleLayout() {
        updateLayout(expanded: controller.snapshot.isActive, animated: false)
    }

    private func renderControls(_ value: WorkModeSnapshot) {
        controls.sublayers?.forEach { $0.removeFromSuperlayer() }
        configurationControls.sublayers?.forEach { $0.removeFromSuperlayer() }
        actionLayers.removeAll(keepingCapacity: true)
        let topActions = configurationActions(value)
        let topIDs = Set(topActions.map(\.id))
        for action in topActions + accessibleActions.filter({ !topIDs.contains($0.id) && $0.id != "work:focusAccess" }) {
            let selected = (action.id == "work:countdown" && value.kind == .countdown)
                || (action.id == "work:stopwatch" && value.kind == .stopwatch)
                || (action.id.hasPrefix("work:preset:") && Double(action.id.dropFirst("work:preset:".count)).map { $0 * 60 == value.duration } == true)
                || (action.id == "work:custom" && ![300.0, 1800, 3600].contains(value.duration))
            let primaryAction = action.id == "work:start" || action.id == "work:resume"
            let group = CALayer(); group.frame = action.rect; group.allowsGroupOpacity = false
            group.name = "workMode.button." + action.id
            (topIDs.contains(action.id) ? configurationControls : controls).addSublayer(group)
            actionLayers[action.id] = group
            let plate = CAShapeLayer(); plate.frame = group.bounds
            plate.path = cutCorner(group.bounds)
            plate.fillColor = (primaryAction ? yellow : NSColor(white: dark ? 0.78 : 0.91, alpha: 1)).cgColor
            plate.strokeColor = (selected ? yellow : NSColor(white: dark ? 0.94 : 0.4, alpha: 0.5)).cgColor
            plate.lineWidth = selected ? 1.5 : 0.6
            group.addSublayer(plate)
            HUDControlHighlightLayer.add(to: group, rect: group.bounds, shape: .cutCorner, framed: true)
            let text = CATextLayer(); text.frame = group.bounds.insetBy(dx: 4, dy: 5)
            text.name = "workMode.control." + action.id
            configure(text, size: action.id.hasPrefix("work:preset:") ? 10.5 : 11, weight: .semibold)
            text.string = action.label; text.alignmentMode = .center; text.foregroundColor = ink.cgColor
            text.contentsScale = HUDRenderScale.contentScale(for: text, baseScale: scale)
            group.addSublayer(text)
        }
    }

    private func animateAction(_ actionID: String, from previous: WorkModeSnapshot, changed: Bool) {
        guard active, !controller.isSuspended, !shouldReduceMotion() else { return }
        let current = controller.snapshot
        var visibleAction = actionID
        if actionLayers[visibleAction] == nil {
            visibleAction = current.phase == .running ? "work:pause" : "work:start"
        }
        if let button = actionLayers[visibleAction] {
            let expand = CABasicAnimation(keyPath: "transform")
            expand.fromValue = NSValue(caTransform3D: CATransform3DMakeScale(0.985, 0.985, 1))
            expand.toValue = NSValue(caTransform3D: CATransform3DIdentity)
            expand.duration = 0.18
            expand.timingFunction = CAMediaTimingFunction(name: .easeOut)
            button.add(expand, forKey: "workFeedback.expand")
        }
        guard changed else { return }
        // The clock indexes mechanically through a clipped viewport. No
        // opacity animation is used, so the large glyphs remain crisp.
        let direction: CGFloat = current.kind != previous.kind
            ? (current.kind == .countdown ? -1 : 1)
            : (current.duration < previous.duration ? -1 : 1)
        let travel = CABasicAnimation(keyPath: "transform")
        travel.fromValue = NSValue(caTransform3D: CATransform3DMakeTranslation(10 * direction, 11, -12))
        travel.toValue = NSValue(caTransform3D: CATransform3DIdentity)
        travel.duration = 0.22
        travel.timingFunction = CAMediaTimingFunction(name: .easeOut)
        digits.add(travel, forKey: "workFeedback.clockIndex")
    }

    private func removeFeedback() {
        func remove(_ item: CALayer) {
            for key in item.animationKeys() ?? [] where key.hasPrefix("workFeedback.") { item.removeAnimation(forKey: key) }
            item.sublayers?.forEach(remove)
        }
        remove(layer)
    }

    private func phaseTitle(_ phase: WorkModePhase) -> String {
        switch phase {
        case .idle: return L10n.text("Ready", "就绪")
        case .running: return "RUNNING"
        case .paused: return "PAUSED"
        case .stopped: return L10n.text("Stopped", "已结束")
        case .completed: return L10n.text("Completed", "已完成")
        }
    }
    private func configure(_ text: CATextLayer, size: CGFloat, weight: NSFont.Weight) {
        // CATextLayer can render its backing contents after our disabled-actions
        // transaction commits; keep those deferred text updates instantaneous.
        text.actions = ["contents": NSNull()]
        text.font = NSFont.systemFont(ofSize: size, weight: weight); text.fontSize = size
        text.truncationMode = .end
        text.contentsScale = HUDRenderScale.contentScale(for: text, baseScale: scale)
    }
    private func cutCorner(_ rect: CGRect) -> CGPath {
        let p = CGMutablePath(); p.move(to: CGPoint(x: rect.minX + 4, y: rect.minY))
        p.addLine(to: CGPoint(x: rect.maxX, y: rect.minY)); p.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - 4))
        p.addLine(to: CGPoint(x: rect.maxX - 4, y: rect.maxY)); p.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        p.addLine(to: CGPoint(x: rect.minX, y: rect.minY + 4)); p.closeSubpath(); return p
    }
    private func withoutActions(_ body: () -> Void) { CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit() }
}
