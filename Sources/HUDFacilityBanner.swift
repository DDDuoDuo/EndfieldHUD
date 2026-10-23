import AppKit
import QuartzCore

/// Main-queue timing of the facility banner, the tag stack and the boot log.
/// Tests pass a manual clock and collect the scheduled work instead.
struct HUDTimers {
    var now: () -> CFTimeInterval
    var schedule: (TimeInterval, DispatchWorkItem) -> Void

    static let main = HUDTimers(now: CACurrentMediaTime,
                                schedule: { DispatchQueue.main.asyncAfter(deadline: .now() + $0, execute: $1) })

    /// Schedules `action` and returns the work item, so the caller can cancel it.
    @discardableResult func after(_ delay: TimeInterval, _ action: @escaping () -> Void) -> DispatchWorkItem {
        let work = DispatchWorkItem(block: action)
        schedule(max(0, delay), work)
        return work
    }
}

/// A control whose hover and press drive closures, with an optional visible
/// silhouette (layer coordinates) outside which presses miss.
final class HUDFacilityControl: HUDControl {
    var onHover: ((Bool) -> Void)?
    var hitPath: CGPath?

    override func hoverChanged() { onHover?(isHovered && isEnabled) }
    override func contains(_ point: CGPoint) -> Bool {
        guard super.contains(point) else { return false }
        return hitPath?.contains(point) ?? true
    }
}

/// The moving hazard stripes (`hzMove`): `repeating-linear-gradient(-45deg,
/// color 0 p/2, #111 p/2 p)` drifting right. Bands run bottom-left to top-right
/// and are `period` px apart horizontally. The loop runs between `start` and
/// `stop` only, and never under Reduce Motion.
final class HUDHazardStripes {
    let layer = HUDLayer.plain("hazard")
    private let band = HUDLayer.shape("hazard.band")
    private let period: CGFloat
    private let cycle: CFTimeInterval

    /// `cycle` is the time one period takes to pass.
    init(size: CGSize, period: CGFloat, color: CGColor, cycle: CFTimeInterval) {
        self.period = period; self.cycle = cycle
        HUDMotion.perform {
            layer.frame = CGRect(origin: .zero, size: size)
            layer.masksToBounds = true
            layer.backgroundColor = HUDColor.nearBlack
            band.frame = layer.bounds
            band.fillColor = color
            band.path = Self.path(size: size, period: period)
            layer.addSublayer(band)
        }
    }

    var color: CGColor? {
        get { band.fillColor }
        set { HUDMotion.perform { band.fillColor = newValue } }
    }

    var isRunning: Bool { band.animation(forKey: "hud.hazard") != nil }

    func start(env: HUDDesignEnvironment) {
        guard !env.reducesMotion else { stop(); return }
        guard !isRunning else { return }
        let move = CABasicAnimation(keyPath: "transform.translation.x")
        move.fromValue = 0; move.toValue = period
        move.duration = cycle; move.repeatCount = .infinity; move.timingFunction = HUDEasing.linear
        band.add(move, forKey: "hud.hazard")
    }

    func stop() { band.removeAnimation(forKey: "hud.hazard") }

    /// Bands where (x + y) mod p lies in [p/2, p); one extra period on the left
    /// covers the drift.
    static func path(size: CGSize, period p: CGFloat) -> CGPath {
        let path = CGMutablePath(), h = size.height
        var k = -2
        while CGFloat(k) * p + p / 2 - h <= size.width + p {
            let x0 = CGFloat(k) * p + p / 2, x1 = CGFloat(k + 1) * p
            path.addLines(between: [CGPoint(x: x0, y: 0), CGPoint(x: x1, y: 0), CGPoint(x: x1 - h, y: h), CGPoint(x: x0 - h, y: h)])
            path.closeSubpath()
            k += 1
        }
        return path
    }
}

/// The facility alert banner (spec §3.29), flat in shell space at the top of
/// the screen: a rectangular plate with the moving hazard block, `FACILITY
/// ALERT` and (outside English) the English name, title, detail and the big orange value. It drops in and
/// unfolds (fbIn), counts down 5 s (paused while hovered), then rises out
/// (fbOut). Alerts raised while one shows queue up and follow one by one.
/// Coordinates are content px; the host places and scales `layer`.
final class HUDFacilityBanner {
    static let size = CGSize(width: 540, height: 70)
    static let lifetime: TimeInterval = 5

    let layer = HUDLayer.plain("facility.banner")
    let env: HUDDesignEnvironment
    /// The host routes pointer and hover here: hover pauses the countdown, a
    /// click opens the alert's module and hides the banner.
    var control: HUDControl { hit }
    var onClick: ((FacilityAlertKind) -> Void)?
    /// Called when the last banner has left (after its countdown, a click or `hide`).
    var onHidden: (() -> Void)?
    /// A banner is on screen, entering or leaving.
    private(set) var isShowing = false
    /// The alert on screen, then the queue.
    private(set) var current: FacilityAlert?
    private(set) var queue: [FacilityAlert] = []

    private let timers: HUDTimers
    private let hit: HUDFacilityControl
    private let plate = HUDLayer.plain("facility.banner.plate")
    private let clip = HUDLayer.shape("facility.banner.clip")
    private let fill = HUDLayer.gradient("facility.banner.fill")
    private let hazard: HUDHazardStripes
    private let label = HUDTextLayer(style: Style.label)
    private let name = HUDTextLayer(style: Style.label.colored(HUDColor.rgb(0x777777)))
    private let title = HUDTextLayer(style: Style.title)
    private let detail = HUDTextLayer(style: Style.detail)
    private let value = HUDTextLayer(style: Style.value, alignment: .right)
    private let unit = HUDTextLayer(style: Style.unit, alignment: .right)
    private let rule = HUDLayer.plain("facility.banner.countdown")
    private var leaving = false
    private var hovered = false
    private var remaining: TimeInterval = 0
    private var runningSince: CFTimeInterval?
    private var expiry: DispatchWorkItem?
    private var removal: DispatchWorkItem?

    private enum Style {
        static let label = HUDTextStyle(size: 10, weight: .bold, color: HUDColor.orange, tracking: 0.18)
        static let title = HUDTextStyle(size: 17, weight: .bold, color: HUDColor.rgb(0xF4F4F4), tracking: 0.08)
        static let detail = HUDTextStyle(size: 11, weight: .regular, color: HUDColor.rgb(0x9A9A9A))
        static let value = HUDTextStyle(size: 34, weight: .light, color: HUDColor.orange, tabular: true)
        static let unit = HUDTextStyle(size: 9, weight: .bold, color: HUDColor.rgb(0x9A9A9A), lineHeight: 1.4, tracking: 0.14)
    }

    init(env: HUDDesignEnvironment, timers: HUDTimers = .main) {
        self.env = env; self.timers = timers
        hit = HUDFacilityControl(id: "facility.banner", env: env, layer: layer)
        hazard = HUDHazardStripes(size: CGSize(width: 78, height: Self.size.height), period: 12 * CGFloat(2).squareRoot(),
                                  color: HUDColor.orange, cycle: 1.2)
        let bounds = CGRect(origin: .zero, size: Self.size)
        HUDMotion.perform {
            layer.bounds = bounds; layer.isHidden = true
            plate.frame = bounds
            clip.frame = bounds; clip.path = Self.platePath(unfolded: 1); clip.fillColor = HUDColor.black(1)
            plate.mask = clip
            fill.frame = bounds
            fill.colors = [HUDColor.rgb(0x1D1D1D), HUDColor.rgb(0x121212)]
            fill.startPoint = CGPoint(x: 0.5, y: 0); fill.endPoint = CGPoint(x: 0.5, y: 1)
            // inset 0 0 0 1px #4a3216 around the whole plate.
            fill.borderWidth = 1; fill.borderColor = HUDColor.rgb(0x4A3216)
            hazard.layer.frame.origin = CGPoint(x: 10, y: 0)
            let ring = HUDLayer.plain(); ring.frame = CGRect(x: 17, y: 13, width: 44, height: 44); ring.backgroundColor = HUDColor.orange
            let box = HUDLayer.plain(); box.frame = CGRect(x: 19, y: 15, width: 40, height: 40); box.backgroundColor = HUDColor.nearBlack
            let warn = HUDIconLayer(.warn, size: 22, color: HUDColor.orange); warn.position = CGPoint(x: 20, y: 20)
            box.addSublayer(warn)
            hazard.layer.addSublayer(ring); hazard.layer.addSublayer(box)
            rule.anchorPoint = CGPoint(x: 0, y: 0.5)
            rule.frame = CGRect(x: 28, y: Self.size.height - 2, width: Self.size.width - 56, height: 2)
            rule.backgroundColor = HUDColor.orange
            [fill, hazard.layer, label, name, title, detail, value, unit, rule].forEach(plate.addSublayer)
            layer.addSublayer(plate)
        }
        hit.isEnabled = false
        hit.setTarget(bounds, path: Self.platePath(unfolded: 1))
        hit.hitPath = Self.platePath(unfolded: 1)
        hit.onHover = { [weak self] in self?.setHovered($0) }
        hit.onActivate = { [weak self] in self?.click() }
    }

    /// Shows `alert`, or queues it behind the banner on screen. The same alert
    /// again refreshes its banner (on screen or queued) in place.
    func show(_ alert: FacilityAlert) {
        if let current, current.kind == alert.kind, !leaving {
            self.current = alert; render(alert); return
        }
        if let index = queue.firstIndex(where: { $0.kind == alert.kind }) { queue[index] = alert; return }
        if isShowing { queue.append(alert); return }
        present(alert)
    }

    /// Hides the banner on screen and drops the queue.
    func hide(animated: Bool) {
        queue.removeAll()
        guard isShowing else { return }
        leave(animated: animated)
    }

    /// Drops a queued banner of `kind`, and lets it leave if it is on screen
    /// (its alert recovered or was dismissed).
    func withdraw(_ kind: FacilityAlertKind) {
        queue.removeAll { $0.kind == kind }
        if current?.kind == kind, !leaving { leave(animated: true) }
    }

    // MARK: Presentation

    private func present(_ alert: FacilityAlert) {
        removal?.cancel(); removal = nil
        current = alert; isShowing = true; leaving = false
        render(alert)
        plate.removeAllAnimations(); clip.removeAllAnimations(); rule.removeAllAnimations()
        HUDMotion.perform {
            layer.isHidden = false
            plate.opacity = 1; plate.transform = CATransform3DIdentity
            clip.path = Self.platePath(unfolded: 1)
            rule.transform = CATransform3DIdentity
        }
        hit.isEnabled = true
        hazard.start(env: env)
        if !env.reducesMotion {
            // fbIn: down from −90 px by 60 % of the run while the clip unfolds from the left edge.
            let drop = CABasicAnimation(keyPath: "transform.translation.y")
            drop.fromValue = -90; drop.toValue = 0; drop.duration = 0.42 * 0.6; drop.timingFunction = HUDEasing.snap
            plate.add(drop, forKey: "hud.enter")
            let unfold = CABasicAnimation(keyPath: "path")
            unfold.fromValue = Self.platePath(unfolded: 0); unfold.toValue = Self.platePath(unfolded: 1)
            unfold.duration = 0.42; unfold.timingFunction = HUDEasing.snap
            clip.add(unfold, forKey: "hud.unfold")
        }
        remaining = Self.lifetime; runningSince = nil
        if !hovered { resumeCountdown() }
    }

    private func render(_ alert: FacilityAlert) {
        let english = L10n.resolvedLanguage == .english
        HUDMotion.perform {
            label.text = L10n.text("FACILITY ALERT", "设施警报")
            // Outside English the game's bilingual pattern: the English name beside
            // the label, the UI-language title below. English shows English only.
            name.text = english ? "" : alert.englishTitle
            name.language = nil
            title.text = english ? alert.englishTitle : alert.title
            detail.text = alert.detail
            value.text = alert.value
            unit.text = alert.unit
            let right = Self.size.width - 40
            let column = ceil(max(value.preferredWidth, unit.preferredWidth))
            value.lineFrame = CGRect(x: right - column, y: 11.7, width: column, height: 34)
            unit.lineFrame = CGRect(x: right - column, y: 45.7, width: column, height: 12.6)
            let x: CGFloat = 104, width = max(40, right - column - 16 - x)
            let top: CGFloat = alert.detail.isEmpty ? 18.5 : 10.5
            let labelWidth = min(width, label.preferredWidth)
            label.lineFrame = CGRect(x: x, y: top, width: labelWidth, height: 10)
            name.lineFrame = CGRect(x: x + labelWidth + 10, y: top, width: max(0, width - labelWidth - 10), height: 10)
            title.lineFrame = CGRect(x: x, y: top + 16, width: width, height: 17)
            detail.lineFrame = CGRect(x: x, y: top + 38, width: width, height: 11)
            detail.isHidden = alert.detail.isEmpty
        }
        hit.label = [L10n.text("FACILITY ALERT", "设施警报"), alert.title, "\(alert.value) \(alert.unit)"].joined(separator: ", ")
        env.applyScale(to: layer)
    }

    private func click() {
        guard let kind = current?.kind, isShowing, !leaving else { return }
        leave(animated: true)
        onClick?(kind)
    }

    /// fbOut: rises 90 px and fades (.26 s ease-in), removed at 260 ms; then
    /// the next queued alert, or idle.
    private func leave(animated: Bool) {
        guard isShowing, !leaving else {
            if leaving, !animated { finishLeaving() }
            return
        }
        leaving = true
        pauseCountdown(); remaining = 0
        hit.isEnabled = false
        hit.clearInteractionState()
        guard animated, !env.reducesMotion else { finishLeaving(); return }
        let rise = CABasicAnimation(keyPath: "transform.translation.y"); rise.toValue = -90
        let fade = CABasicAnimation(keyPath: "opacity"); fade.toValue = 0
        let group = CAAnimationGroup(); group.animations = [rise, fade]
        group.duration = 0.26; group.timingFunction = HUDEasing.easeIn
        group.fillMode = .forwards; group.isRemovedOnCompletion = false
        plate.add(group, forKey: "hud.leave")
        removal = timers.after(0.26) { [weak self] in self?.finishLeaving() }
    }

    private func finishLeaving() {
        removal?.cancel(); removal = nil
        hazard.stop()
        plate.removeAllAnimations(); clip.removeAllAnimations(); rule.removeAllAnimations()
        HUDMotion.perform { layer.isHidden = true }
        leaving = false; current = nil
        if !queue.isEmpty { present(queue.removeFirst()); return }
        isShowing = false
        onHidden?()
    }

    // MARK: Countdown (fbCd 5 s linear, paused while hovered)

    private func setHovered(_ on: Bool) {
        guard on != hovered else { return }
        hovered = on
        guard isShowing, !leaving else { return }
        if on { pauseCountdown() } else { resumeCountdown() }
    }

    private func resumeCountdown() {
        guard runningSince == nil, remaining > 0 else { return }
        runningSince = timers.now()
        expiry = timers.after(remaining) { [weak self] in
            guard let self else { return }
            self.expiry = nil; self.runningSince = nil; self.remaining = 0
            self.leave(animated: true)
        }
        // Reduce Motion keeps the rule whole: it is a readout, not a motion.
        guard !env.reducesMotion else { return }
        let scale = Double(remaining / Self.lifetime)
        HUDMotion.perform { rule.setValue(0, forKeyPath: "transform.scale.x") }
        let shrink = CABasicAnimation(keyPath: "transform.scale.x")
        shrink.fromValue = scale; shrink.toValue = 0; shrink.duration = remaining; shrink.timingFunction = HUDEasing.linear
        rule.add(shrink, forKey: "hud.countdown")
    }

    private func pauseCountdown() {
        expiry?.cancel(); expiry = nil
        guard let since = runningSince else { return }
        runningSince = nil
        remaining = max(0, remaining - (timers.now() - since))
        rule.removeAnimation(forKey: "hud.countdown")
        if !env.reducesMotion { HUDMotion.perform { rule.setValue(remaining / Self.lifetime, forKeyPath: "transform.scale.x") } }
    }

    // MARK: Geometry

    /// A plain rectangle (owner, 2026-10-22: no cut corners); `unfolded` 0 is
    /// the collapsed clip at the left edge that fbIn starts from. The flat PNG
    /// export renders this same layer, so it stays consistent.
    static func platePath(unfolded progress: CGFloat) -> CGPath {
        CGPath(rect: CGRect(x: 0, y: 0, width: size.width * max(0, min(1, progress)), height: size.height), transform: nil)
    }

    // MARK: Verification

    var textsForTesting: [String] { [label.text, name.text, title.text, detail.text, value.text, unit.text] }
    var countdownRemainingForTesting: TimeInterval {
        runningSince.map { max(0, remaining - (timers.now() - $0)) } ?? remaining
    }
    var isCountingDownForTesting: Bool { runningSince != nil }
    var isHazardRunningForTesting: Bool { hazard.isRunning }
}
