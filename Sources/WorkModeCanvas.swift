import AppKit
import QuartzCore

/// The giant ring of Work Mode (spec §3.16): a 620 px square centred on the
/// disc, outside the content box (box px; the ring's own centre is (310, 310)).
enum WorkModeDialGeometry {
    static let frame = CGRect(x: -15, y: -5, width: 620, height: 620)
    static let center = CGPoint(x: 310, y: 310)
    static let radius: CGFloat = 310
    static let arcRadius: CGFloat = 252
    static let tickCount = 120

    /// Countdown: remaining / total, full at the start; stopwatch: the
    /// position of the second hand. Both follow the displayed seconds.
    static func fraction(_ value: WorkModeSnapshot) -> CGFloat {
        switch value.kind {
        case .countdown:
            guard value.duration > 0 else { return 0 }
            return CGFloat(min(1, max(0, Double(value.displayedSeconds) / value.duration)))
        case .stopwatch: return CGFloat(value.displayedSeconds % 60) / 60
        }
    }

    /// Ticks k with k / 120 < fraction are lit.
    static func litTicks(_ fraction: CGFloat) -> Int {
        min(tickCount, max(0, Int((fraction * CGFloat(tickCount) - 0.000_001).rounded(.up))))
    }

    /// Filled quads for ticks `range` (a stroked SVG line with butt caps):
    /// r 292 inward to 268 (every 10th, 4 wide) or 278 (2.4 wide).
    static func ticks(_ range: Range<Int>) -> CGPath {
        let path = CGMutablePath()
        for k in range {
            let angle = CGFloat(k) / CGFloat(tickCount) * 2 * .pi - .pi / 2, major = k % 10 == 0
            let inner: CGFloat = major ? 268 : 278, half: CGFloat = (major ? 4 : 2.4) / 2
            let u = CGPoint(x: cos(angle), y: sin(angle)), n = CGPoint(x: -u.y, y: u.x)
            func point(_ r: CGFloat, _ side: CGFloat) -> CGPoint {
                CGPoint(x: center.x + u.x * r + n.x * side, y: center.y + u.y * r + n.y * side)
            }
            path.addLines(between: [point(292, -half), point(292, half), point(inner, half), point(inner, -half)])
            path.closeSubpath()
        }
        return path
    }
}

/// The ring's layers, built once per page. `set(_:animated:)` moves the
/// progress arc (.6 s E), the lit ticks and the travelling marker in place.
private final class WorkModeRing {
    let layer = HUDLayer.plain("workMode.ring")
    private let arc = HUDLayer.shape("workMode.arc")
    private let dim = HUDLayer.shape("workMode.ticks")
    private let lit = HUDLayer.shape("workMode.ticks.lit")
    /// Rotates about the ring centre; its triangle sits at 12 o'clock.
    private let orbit = HUDLayer.plain("workMode.marker")
    private let env: HUDDesignEnvironment
    private var litCount = -1

    init(fraction: CGFloat, env: HUDDesignEnvironment) {
        self.env = env
        let g = WorkModeDialGeometry.self, c = g.center
        func circle(_ radius: CGFloat, _ color: CGColor, _ width: CGFloat, dash: [NSNumber]? = nil) -> CAShapeLayer {
            let shape = HUDLayer.shape("workMode.circle")
            shape.frame = CGRect(origin: .zero, size: g.frame.size)
            shape.path = CGPath(ellipseIn: CGRect(x: c.x - radius, y: c.y - radius, width: radius * 2, height: radius * 2), transform: nil)
            shape.fillColor = nil; shape.strokeColor = color; shape.lineWidth = width; shape.lineDashPattern = dash
            return shape
        }
        func triangle(_ points: [CGPoint]) -> CAShapeLayer {
            let shape = HUDLayer.shape("workMode.triangle")
            shape.frame = CGRect(origin: .zero, size: g.frame.size)
            let path = CGMutablePath(); path.addLines(between: points); path.closeSubpath()
            shape.path = path; shape.fillColor = HUDColor.accentInk
            return shape
        }
        HUDMotion.perform {
            layer.frame = g.frame
            layer.addSublayer(circle(304, HUDColor.foreground(0.18), 1))
            layer.addSublayer(circle(g.arcRadius, HUDColor.tone(0x2A2A2A), 10))
            arc.frame = layer.bounds
            let progress = CGMutablePath()
            // Starts at 12 o'clock and runs clockwise in the HUD's flipped space.
            progress.addArc(center: c, radius: g.arcRadius, startAngle: -.pi / 2, endAngle: 3 * .pi / 2, clockwise: false)
            arc.path = progress; arc.fillColor = nil; arc.strokeColor = HUDColor.accentInk; arc.lineWidth = 10; arc.lineCap = .butt
            layer.addSublayer(arc)
            layer.addSublayer(circle(232, HUDColor.foreground(0.12), 1, dash: [2, 7]))
            dim.frame = layer.bounds; dim.fillColor = HUDColor.tone(0x3D3D3D)
            lit.frame = layer.bounds; lit.fillColor = HUDColor.accentInk
            layer.addSublayer(dim); layer.addSublayer(lit)
            let style = HUDTextStyle(size: 11, weight: .regular, color: HUDColor.steel)
            let metrics = HUDText.metrics(style)
            for (quarter, label) in ["00", "15", "30", "45"].enumerated() {
                let angle = CGFloat(quarter) * .pi / 2 - .pi / 2
                let text = HUDTextLayer(label, style: style, alignment: .center)
                // SVG text: y is the baseline, 4 px below the r 322 point.
                let baseline = c.y + sin(angle) * 322 + 4
                text.lineFrame = CGRect(x: c.x + cos(angle) * 322 - 15, y: baseline - metrics.baseline, width: 30, height: 11)
                layer.addSublayer(text)
            }
            orbit.bounds = layer.bounds; orbit.position = c
            let top = c.y - g.arcRadius
            orbit.addSublayer(triangle([CGPoint(x: c.x, y: top - 17), CGPoint(x: c.x + 9, y: top - 2), CGPoint(x: c.x - 9, y: top - 2)]))
            layer.addSublayer(orbit)
            layer.addSublayer(triangle([CGPoint(x: c.x, y: 4), CGPoint(x: c.x + 7, y: 14), CGPoint(x: c.x - 7, y: 14)]))
        }
        set(fraction, animated: false)
    }

    private(set) var fraction: CGFloat = -1

    func set(_ value: CGFloat, animated: Bool) {
        let shown = arc.presentation()?.strokeEnd ?? arc.strokeEnd
        fraction = value
        let count = WorkModeDialGeometry.litTicks(value)
        if count != litCount {
            litCount = count
            HUDMotion.perform {
                dim.path = WorkModeDialGeometry.ticks(count..<WorkModeDialGeometry.tickCount)
                lit.path = WorkModeDialGeometry.ticks(0..<count)
            }
        }
        // A stopwatch minute wraps forward: jump instead of rewinding the arc.
        let animate = animated && !env.reducesMotion && value >= shown - 0.5 && abs(value - shown) > 0.0001
        orbit.removeAnimation(forKey: "hud.workMode.marker")
        HUDMotion.perform { orbit.transform = CATransform3DMakeRotation(value * 2 * .pi, 0, 0, 1) }
        guard animate else {
            arc.removeAnimation(forKey: "hud.strokeEnd")
            HUDMotion.perform { arc.strokeEnd = value }
            return
        }
        HUDMotion.animate(arc, "strokeEnd", to: value, duration: Self.transition, env: env)
        let turn = CABasicAnimation(keyPath: "transform.rotation.z")
        turn.fromValue = shown * 2 * .pi; turn.toValue = value * 2 * .pi
        turn.duration = Self.transition; turn.timingFunction = HUDEasing.standard
        orbit.add(turn, forKey: "hud.workMode.marker")
    }

    static let transition: CFTimeInterval = 0.6
}

/// Work Mode (spec §3.16): no module header; the giant ring around a column
/// of title, Countdown / Stopwatch tabs, state label, 112 px timer, presets or
/// laps and the actions. The controller owns the session (Focus, tracked work
/// time that feeds Personal ID, event records); this canvas presents it. The
/// one-second tick only changes the digits, the ring and a label in place;
/// phase, mode, preset, lap and editor changes re-render the page in place.
final class WorkModeCanvas: HUDDesignCanvas {
    let controller: WorkModeController
    var onRequestFocusAccess: (() -> Void)?
    private struct Lap: Equatable { let number: Int, seconds: Int }
    private struct Structure: Equatable {
        let kind: WorkModeKind, phase: WorkModePhase, duration: TimeInterval
        let laps: [Lap], editingCustom: Bool, focus: String?, focusAccess: Bool
    }
    private var observer: UUID?
    private var renderedStructure: Structure?
    private var lastPhase: WorkModePhase
    private var laps: [Lap] = []
    private var lapCount = 0
    private(set) var isEditingCustom = false
    private var applying = false
    private(set) var focusMessage: String?
    private var focusNeedsAccess = false
    private var ring: WorkModeRing?
    private weak var timerText: HUDTextLayer?
    private weak var stateDot: CALayer?
    private weak var titleGroup: CALayer?
    private weak var primary: HUDButton?
    /// The tabs hold their bar weakly; a locked bar is not the page's `tabs`.
    private var kindTabs: HUDTabs?
    private lazy var customField = makeCustomField()

    override var hitBounds: CGRect { WorkModeDialGeometry.frame.union(super.hitBounds) }
    /// Kept for the verification build; the ring's arc transition.
    static var layoutTransitionDuration: TimeInterval { WorkModeRing.transition }

    static var timerStyle: HUDTextStyle {
        HUDTextStyle(size: 112, weight: .light, color: HUDColor.tone(0xF6F6F6), lineHeight: 0.9, tracking: -0.03, tabular: true)
    }
    static let presets: [TimeInterval] = [5 * 60, 30 * 60, 60 * 60]

    init(context: HUDDesignContext, controller: WorkModeController) {
        self.controller = controller
        lastPhase = controller.snapshot.phase
        super.init(module: .workMode, context: context)
        showsHeader = false
        observer = controller.observe { [weak self] in self?.controllerChanged() }
    }

    deinit {
        if let observer { controller.removeObserver(observer) }
        // The session belongs to the controller; the visible ticker ends with this view.
        if isActive { controller.setVisible(false) }
    }

    // MARK: Host interface

    /// Real Focus failures from the app owner: a toast when one appears and a
    /// warning button beside the state label (it opens Accessibility settings
    /// when that permission is missing). Normal operation shows nothing.
    func setFocusStatusMessage(_ value: String?, needsAccessibilityPermission: Bool = false) {
        let trimmed = value?.trimmingCharacters(in: .whitespacesAndNewlines)
        let message = trimmed?.isEmpty == false ? trimmed : nil
        let appeared = message != nil && message != focusMessage
        focusMessage = message
        focusNeedsAccess = needsAccessibilityPermission && message != nil
        if appeared, isActive, let message { toast(icon: .warn, "Focus mode", "专注模式", detail: message) }
        refresh(animated: false)
    }

    /// Reduce Motion changed: settle the arc and the blink.
    func refreshMotionPreference() {
        guard renderedLanguage != nil else { return }
        rerender(); applyBlink()
    }

    // MARK: Lifecycle

    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        let content = super.makeContent(for: module, style: style)
        refresh(animated: false)
        return content
    }

    override func didActivate() {
        controller.setVisible(true)
        refresh(animated: false)
        applyBlink()
    }

    override func didDeactivate() {
        controller.setVisible(false)
        stateDot?.removeAnimation(forKey: "workMode.blink")
        if isEditingCustom, !customField.isEditing { isEditingCustom = false; customField.setInvalid(false) }
    }

    override func willLeave() { stateDot?.removeAnimation(forKey: "workMode.blink") }

    /// The flick of the inner title; the column staggers in (the ring has no entrance).
    override func playEntrance(from offset: CGPoint) {
        super.playEntrance(from: offset)
        if let titleGroup { HUDMotion.wipe(titleGroup, from: .left, duration: HUDDuration.titleWipe, fadeFrom: 0, env: env) }
    }

    override func recordingModeChanged() {}

    // MARK: State

    private var structure: Structure {
        let value = controller.snapshot
        return Structure(kind: value.kind, phase: value.phase, duration: value.duration, laps: laps,
                         editingCustom: isEditingCustom, focus: focusMessage, focusAccess: focusNeedsAccess)
    }

    private func controllerChanged() {
        let value = controller.snapshot
        let finished = lastPhase == .running && value.phase == .completed
        lastPhase = value.phase
        // The countdown keeps its deadline while the module is hidden; the toast
        // appears whenever the HUD is on screen.
        if finished, context.host?.window?.isVisible == true { toast(icon: .timer, "Session complete", "专注结束") }
        if !applying { refresh(animated: true) }
    }

    /// Structure changes re-render in place; anything else updates the digits,
    /// the ring and the primary button's label.
    private func refresh(animated: Bool) {
        guard renderedLanguage != nil, !isTransitioning else { return }
        if structure != renderedStructure { rerender(); applyBlink(); return }
        let value = controller.snapshot
        HUDMotion.perform { timerText?.text = Self.timeText(value) }
        ring?.set(WorkModeDialGeometry.fraction(value), animated: animated && isActive)
        if let primary { primary.label = primary.title + ", " + accessibilityStatus(value) }
    }

    /// Runs a controller action without the observer re-rendering mid-way.
    private func apply(_ change: () -> Void) {
        applying = true; change(); applying = false
        lastPhase = controller.snapshot.phase
    }

    /// The FOCUS ON square blinks only while the countdown or stopwatch runs.
    private func applyBlink() {
        guard let dot = stateDot else { return }
        dot.removeAnimation(forKey: "workMode.blink")
        guard isActive, controller.snapshot.phase == .running, !env.reducesMotion else { return }
        let blink = CAKeyframeAnimation(keyPath: "opacity")
        blink.values = [1, 0.2, 1]; blink.keyTimes = [0, 0.5, 1]
        blink.duration = 1.6; blink.repeatCount = .infinity; blink.timingFunction = HUDEasing.easeInOut
        dot.add(blink, forKey: "workMode.blink")
    }

    static func timeText(_ value: WorkModeSnapshot) -> String { HUDFormat.clock(seconds: value.displayedSeconds) }

    private func accessibilityStatus(_ value: WorkModeSnapshot) -> String {
        (value.kind == .countdown ? L10n.text("Countdown", "倒计时") : L10n.text("Stopwatch", "秒表")) + " " + Self.timeText(value)
    }

    /// `5 min` for whole minutes, `1:30` otherwise.
    static func durationTitle(_ seconds: TimeInterval) -> String {
        let value = Int(seconds.rounded())
        if value % 60 == 0 { let minutes = value / 60; return L10n.text("\(minutes) min", "\(minutes) 分") }
        return HUDFormat.clock(seconds: value)
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        let value = controller.snapshot
        renderedStructure = structure
        let ring = WorkModeRing(fraction: WorkModeDialGeometry.fraction(value), env: env)
        self.ring = ring
        HUDMotion.perform { page.layer.addSublayer(ring.layer) }
        buildTitle(page)
        buildTabs(page, value)
        buildState(page, value)
        let timer = HUDTextLayer(Self.timeText(value), style: Self.timerStyle, alignment: .center)
        timer.minimumScale = 0.5
        HUDMotion.perform { timer.lineFrame = CGRect(x: 115, y: 218, width: 360, height: 112 * 0.9); page.layer.addSublayer(timer) }
        timerText = timer
        page.stagger(timer)
        let slotBottom: CGFloat
        if value.kind == .countdown { buildPresets(page, value); slotBottom = 331 + 30 }
        else { slotBottom = 331 + buildLaps(page) }
        buildActions(page, value, top: slotBottom + 12)
        page.back = isEditingCustom ? { [weak self] in self?.closeCustom() } : nil
    }

    private func buildTitle(_ page: HUDDesignPage) {
        let lines = HUDModuleTitle("Work Mode", "工作模式").lines()
        let titleStyle = HUDType.moduleTitle.sized(20)
        let title = HUDTextLayer(lines.title, style: titleStyle), subtitle = HUDTextLayer(lines.subtitle, style: HUDType.titleSubtitle)
        title.language = lines.titleLanguage; subtitle.language = lines.subtitleLanguage
        let titleWidth = title.preferredWidth, subtitleWidth = subtitle.preferredWidth
        let titleMetrics = HUDText.metrics(titleStyle, language: lines.titleLanguage ?? HUDTextLanguage.tag())
        let subMetrics = HUDText.metrics(HUDType.titleSubtitle, language: lines.subtitleLanguage ?? HUDTextLanguage.tag())
        let width = titleWidth + (lines.subtitle.isEmpty ? 0 : 12 + subtitleWidth)
        let group = HUDLayer.plain("workMode.title")
        HUDMotion.perform {
            group.frame = CGRect(x: (295 - width / 2).rounded(), y: 118, width: width + 2, height: 20)
            title.lineFrame = CGRect(x: 0, y: 10 - titleMetrics.height / 2, width: titleWidth + 1, height: titleMetrics.height)
            let baseline = title.lineFrame.minY + titleMetrics.baseline
            subtitle.lineFrame = CGRect(x: titleWidth + 12, y: baseline - subMetrics.baseline, width: subtitleWidth + 1, height: subMetrics.height)
            group.addSublayer(title); group.addSublayer(subtitle)
            page.layer.addSublayer(group)
        }
        titleGroup = group
    }

    private func buildTabs(_ page: HUDDesignPage, _ value: WorkModeSnapshot) {
        let tabs = HUDTabs(id: "workMode.kind", titles: [(L10n.text("Countdown", "倒计时"), .timer), (L10n.text("Stopwatch", "秒表"), .watch)],
                           selectedIndex: value.kind == .countdown ? 0 : 1, width: 270, keycaps: false, env: env)
        HUDMotion.perform { tabs.layer.frame.origin = CGPoint(x: 160, y: 150); page.layer.addSublayer(tabs.layer) }
        // A running or paused session keeps its mode: switching would end it.
        let locked = value.isActive
        for (index, tab) in tabs.tabs.enumerated() where index != tabs.selectedIndex {
            tab.isEnabled = !locked
            HUDMotion.perform { tab.layer.opacity = locked ? 0.35 : 1 }
        }
        tabs.onSelect = { [weak self] index, direction in self?.chooseKind(index == 0 ? .countdown : .stopwatch, direction: direction) }
        page.add(tabs.controls)
        kindTabs = tabs
        if !locked { page.tabs = tabs }
        page.stagger(tabs.layer)
    }

    private func buildState(_ page: HUDDesignPage, _ value: WorkModeSnapshot) {
        let group = HUDLayer.plain("workMode.state")
        let text = HUDTextLayer(value.isActive ? L10n.text("Focus on", "专注中") : L10n.text("Standby", "待机"), style: HUDType.label)
        let dot = HUDLayer.plain("workMode.state.dot")
        var alert: HUDIconButton?
        if let focusMessage {
            let button = HUDIconButton(id: "workMode.focus", label: focusNeedsAccess ? L10n.text("Open Accessibility Settings", "打开辅助功能设置") : focusMessage,
                                       icon: .warn, side: 22, iconSize: 12, env: env)
            button.onActivate = { [weak self] in self?.focusAlertPressed() }
            alert = button
        }
        let textWidth = ceil(text.preferredWidth)
        let width = 7 + 8 + textWidth + (alert == nil ? 0 : 10 + 22)
        HUDMotion.perform {
            group.frame = CGRect(x: (295 - width / 2).rounded(), y: 196, width: width, height: 10)
            dot.frame = CGRect(x: 0, y: 1.5, width: 7, height: 7)
            dot.backgroundColor = value.isActive ? HUDColor.green : HUDColor.tone(0x666666)
            text.lineFrame = CGRect(x: 15, y: 0, width: textWidth + 1, height: 10)
            [dot, text].forEach(group.addSublayer)
            if let alert { alert.layer.frame = CGRect(x: width - 22, y: -6, width: 22, height: 22); group.addSublayer(alert.layer) }
            page.layer.addSublayer(group)
        }
        if let alert { page.add(alert) }
        stateDot = dot
        page.stagger(group)
    }

    private func buildPresets(_ page: HUDDesignPage, _ value: WorkModeSnapshot) {
        if isEditingCustom {
            let field = customField
            register(field)
            HUDMotion.perform { field.layer.frame.origin = CGPoint(x: 145, y: 331); page.layer.addSublayer(field.layer) }
            page.stagger(field.layer)
            return
        }
        let options = Self.presets.map { Self.durationTitle($0) } + [L10n.text("Custom", "自定")]
        let selected = Self.presets.firstIndex(of: value.duration) ?? Self.presets.count
        let presets = HUDSegmentedControl(id: "workMode.preset", label: L10n.text("Duration", "时长"), options: options,
                                          selectedIndex: selected, width: 300, yellow: true, env: env)
        presets.onChange = { [weak self] index in self?.pickPreset(index) }
        presets.onReselect = { [weak self] index in if index == Self.presets.count { self?.openCustom() } }
        HUDMotion.perform { presets.layer.frame.origin = CGPoint(x: 145, y: 331); page.layer.addSublayer(presets.layer) }
        // The page keeps the group alive: its segments only hold it weakly.
        page.add(presets); page.add(presets.segments); page.stagger(presets.layer)
    }

    /// Up to three laps, newest first; the slot is at least 42 px tall.
    private func buildLaps(_ page: HUDDesignPage) -> CGFloat {
        let group = HUDLayer.plain("workMode.laps")
        let style = HUDTextStyle(size: 12.5, weight: .regular, color: HUDColor.tone(0xBBBBBB), lineHeight: 1.6, tabular: true)
        let height = max(42, CGFloat(laps.count) * 20)
        HUDMotion.perform {
            group.frame = CGRect(x: 115, y: 331, width: 360, height: height)
            for (index, lap) in laps.enumerated() {
                let line = HUDTextLayer("#" + HUDFormat.pad2(lap.number) + "   " + HUDFormat.clock(seconds: lap.seconds), style: style, alignment: .center)
                line.lineFrame = CGRect(x: 0, y: CGFloat(index) * 20, width: 360, height: 20)
                group.addSublayer(line)
            }
            page.layer.addSublayer(group)
        }
        page.stagger(group)
        return height
    }

    private func buildActions(_ page: HUDDesignPage, _ value: WorkModeSnapshot, top: CGFloat) {
        let running = value.phase == .running
        // Resume names a partly elapsed countdown; a paused stopwatch's Start continues it.
        let resumes = value.kind == .countdown && value.phase == .paused
        let title = running ? L10n.text("Pause", "暂停") : (resumes ? L10n.text("Resume", "继续") : L10n.text("Start", "开始"))
        let primary = HUDButton(id: "workMode.primary", title: title, icon: running ? .pause : .play, kind: .primary, size: .large, env: env)
        primary.place(at: .zero, width: 150)
        primary.label = title + ", " + accessibilityStatus(value)
        primary.onActivate = { [weak self] in self?.primaryAction() }
        // Countdown: Reset. Stopwatch: Lap while it runs (unavailable before
        // it starts), Reset once paused, so a stopwatch can always be cleared.
        let lap = value.kind == .stopwatch && (running || value.phase == .idle)
        let secondary = HUDButton(id: lap ? "workMode.lap" : "workMode.reset", title: lap ? L10n.text("Lap", "计次") : L10n.text("Reset", "重置"),
                                  icon: lap ? .plus : .reset, kind: .secondary, size: .large, env: env)
        secondary.place(at: .zero)
        secondary.isEnabled = !lap || running
        secondary.onActivate = { [weak self] in if lap { self?.recordLap() } else { self?.reset() } }
        let group = HUDLayer.plain("workMode.actions")
        let width = primary.layer.bounds.width + 8 + secondary.layer.bounds.width
        HUDMotion.perform {
            group.frame = CGRect(x: (295 - width / 2).rounded(), y: top, width: width, height: 40)
            primary.layer.frame.origin = .zero
            secondary.layer.frame.origin = CGPoint(x: primary.layer.bounds.width + 8, y: 0)
            group.addSublayer(primary.layer); group.addSublayer(secondary.layer)
            page.layer.addSublayer(group)
        }
        page.add([primary, secondary]); page.stagger(group)
        self.primary = primary
    }

    // MARK: Actions

    private func chooseKind(_ kind: WorkModeKind, direction: CGFloat) {
        let value = controller.snapshot
        guard !value.isActive, value.kind != kind else { return }
        apply {
            if kind == .countdown { _ = controller.chooseCountdown(seconds: value.duration) } else { controller.chooseStopwatch() }
        }
        laps = []; lapCount = 0; isEditingCustom = false
        navigate(dx: direction)
    }

    /// 5 / 30 / 60 set the total and stop; Custom opens the minutes field.
    private func pickPreset(_ index: Int) {
        guard Self.presets.indices.contains(index) else { openCustom(); return }
        apply { _ = controller.chooseCountdown(seconds: Self.presets[index]) }
        refresh(animated: true)
    }

    func openCustom() {
        guard controller.snapshot.kind == .countdown else { return }
        let duration = Int(controller.snapshot.duration.rounded())
        customField.setText(duration % 60 == 0 ? String(duration / 60) : WorkModeDuration.editText(controller.snapshot.duration))
        customField.setInvalid(false)
        isEditingCustom = true
        rerender()
        customField.beginEditing()
    }

    /// Bare minutes or min:sec (0:01–1440:00). An invalid entry shakes and
    /// stays open; the same duration closes without restarting a session.
    func commitCustom(_ text: String) {
        guard isEditingCustom else { return }
        guard let seconds = WorkModeDuration.parse(text) else { customField.setInvalid(true, shake: true); return }
        isEditingCustom = false
        let current = controller.snapshot
        if current.kind == .countdown && current.duration == seconds { refresh(animated: false); return }
        apply { _ = controller.chooseCountdown(seconds: seconds) }
        refresh(animated: true)
        toast(icon: .timer, "Custom \(Self.durationTitle(seconds))", "自定义 \(Self.durationTitle(seconds))")
    }

    private func closeCustom() {
        guard isEditingCustom else { return }
        isEditingCustom = false
        customField.setInvalid(false)
        refresh(animated: false)
    }

    private func makeCustomField() -> HUDDesignTextField {
        var options = HUDDesignTextField.Options()
        options.maximumLength = 7; options.selectsAllOnBegin = true
        let field = HUDDesignTextField(id: "workMode.custom", label: L10n.text("Custom countdown: minutes and seconds", "自定义倒计时：分和秒"),
                                       text: "", placeholder: L10n.text("Minutes", "分钟"), size: CGSize(width: 300, height: 30),
                                       options: options, env: env)
        field.onCommit = { [weak self] in self?.commitCustom($0) }
        field.onCancel = { [weak self] in self?.closeCustom() }
        return field
    }

    /// Start (a finished countdown restarts from its total), Pause, Resume.
    func primaryAction() {
        let value = controller.snapshot
        apply {
            switch value.phase {
            case .running: controller.pause()
            case .paused: controller.resume()
            case .idle, .stopped, .completed:
                if value.kind == .stopwatch { laps = []; lapCount = 0 }
                controller.start()
            }
        }
        refresh(animated: true)
    }

    func recordLap() {
        let value = controller.snapshot
        guard value.kind == .stopwatch, value.phase == .running else { return }
        lapCount += 1
        laps = Array(([Lap(number: lapCount, seconds: value.displayedSeconds)] + laps).prefix(3))
        refresh(animated: false)
    }

    func reset() {
        apply { controller.reset() }
        laps = []; lapCount = 0
        refresh(animated: true)
    }

    private func focusAlertPressed() {
        if focusNeedsAccess { onRequestFocusAccess?() }
        else if let focusMessage { toast(icon: .warn, "Focus mode", "专注模式", detail: focusMessage) }
    }

    /// Space or Return: Start, Pause or Resume.
    override func handleExtraKey(_ event: NSEvent) -> Bool {
        guard [49, 36, 76].contains(event.keyCode), event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty else { return false }
        primaryAction()
        return true
    }

    // MARK: Testing

    var lapTextsForTesting: [String] { laps.map { "#" + HUDFormat.pad2($0.number) + "   " + HUDFormat.clock(seconds: $0.seconds) } }
    var timerTextForTesting: String? { timerText?.text }
    var ringFractionForTesting: CGFloat? { ring?.fraction }
    var customFieldForTesting: HUDDesignTextField { customField }
}
