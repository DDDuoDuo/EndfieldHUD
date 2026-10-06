import AppKit
import QuartzCore

/// The HUD's persistent charge control uses the notification renderer itself.
/// Only its independent vector canvas is adopted; AppKit's view-owned backing
/// layer remains with the detached view and never enters the HUD layer tree.
final class HUDChargeBadge {
    typealias Scheduler = (TimeInterval, @escaping () -> Void) -> (() -> Void)

    static let center = CGPoint(x: 500, y: 446)
    static let rendererScale: CGFloat = 0.82
    static let entranceDelay: TimeInterval = 0.58
    static let compactHoldDuration: TimeInterval = 3
    static let entranceDuration = entranceDelay + ChargeIndicatorView.entranceDuration
    static let exitDuration = ChargeIndicatorView.exitDuration
    static let frame = CGRect(x: center.x - ChargeIndicatorView.canvasSize.width * rendererScale / 2,
                              y: center.y - ChargeIndicatorView.canvasSize.height * rendererScale / 2,
                              width: ChargeIndicatorView.canvasSize.width * rendererScale,
                              height: ChargeIndicatorView.canvasSize.height * rendererScale)
    /// The maximum interactive capsule, excluding transparent renderer margins.
    static let compactHitRect = CGRect(x: center.x - 264 * rendererScale / 2,
                                       y: center.y - 38 * rendererScale / 2,
                                       width: 264 * rendererScale, height: 38 * rendererScale)
    static let circleHitRect = CGRect(x: center.x - 19 * rendererScale,
                                      y: center.y - 19 * rendererScale,
                                      width: 38 * rendererScale, height: 38 * rendererScale)

    let layer = CALayer()
    var onHitRegionChanged: (() -> Void)?
    private let renderer = ChargeIndicatorView(frame: CGRect(origin: .zero, size: ChargeIndicatorView.canvasSize))
    private let scheduler: Scheduler
    private var cancellations: [() -> Void] = []
    private var generation = 0
    private enum Phase { case hidden, waiting, entering, presented, exiting }
    private var phase: Phase = .hidden
    private var compactDeadlinePassed = false
    private(set) var isHovered = false
    var stage: OverlayStage { renderer.stage }
    var hitRect: CGRect {
        guard phase != .hidden && phase != .waiting && phase != .exiting else { return .zero }
        let rect = renderer.embeddedBodyRect
        return CGRect(x: Self.frame.minX + rect.minX * Self.rendererScale,
                      y: Self.frame.minY + rect.minY * Self.rendererScale,
                      width: rect.width * Self.rendererScale,
                      height: rect.height * Self.rendererScale)
    }
    var accessibilityLabel: String { renderer.accessibilityLabel() ?? L10n.text("Power / Device Battery", "电量 / 设备电池") }

    init(scheduler: @escaping Scheduler = HUDChargeBadge.dispatchSchedule) {
        self.scheduler = scheduler
        layer.name = "hud.chargeBadge"
        layer.frame = Self.frame
        layer.masksToBounds = false
        let artwork = renderer.embeddedContentLayer
        artwork.name = "hud.chargeBadge.notificationCanvas"
        artwork.position = .zero
        artwork.setAffineTransform(CGAffineTransform(scaleX: Self.rendererScale, y: Self.rendererScale))
        layer.addSublayer(artwork)
        renderer.setStage(.hidden)
    }

    deinit {
        cancellations.forEach { $0() }
        renderer.cancelAnimations()
    }

    func update(snapshot: BatterySnapshot, configuration: AppConfiguration, dark: Bool, contentsScale: CGFloat) {
        // Notification size/position preferences must not scale the persistent
        // HUD control. Raster density is supplied for this exact embedded size.
        renderer.configureEmbedding(dark: dark, contentsScale: contentsScale * Self.rendererScale)
        renderer.set(snapshot: snapshot, configuration: configuration)
    }

    /// The HUD owner shares its existing telemetry sample with the embedded
    /// renderer. This control never starts a monitor or owns a sampling timer.
    func setMetric(_ metric: HUDChargeMetric, telemetry: SystemActivitySnapshot?) {
        renderer.setMetric(metric, telemetry: telemetry)
    }

    func setStable(visible: Bool = true) {
        cancelAnimations()
        phase = visible ? .presented : .hidden
        compactDeadlinePassed = false
        renderer.setStage(visible ? .compact : .hidden)
        if visible { scheduleCompactDeadline() }
        onHitRegionChanged?()
    }

    func animateEntrance(completion: @escaping () -> Void = {}) {
        cancelAnimations()
        compactDeadlinePassed = false
        let token = generation
        if HUDRuntimeAppearance.reduceMotion {
            phase = .presented
            renderer.setStage(.compact)
            scheduleCompactDeadline()
            onHitRegionChanged?()
            completion()
            return
        }
        phase = .waiting
        renderer.setStage(.hidden)
        onHitRegionChanged?()
        schedule(after: Self.entranceDelay, token: token) { badge in
            badge.phase = .entering
            badge.renderer.animateEntrance { [weak badge] in
                guard let badge, badge.generation == token, badge.phase == .entering else { return }
                badge.phase = .presented
                badge.scheduleCompactDeadline()
                badge.onHitRegionChanged?()
                completion()
            }
            HUDDeploymentFlicker.apply(to: [badge.renderer.embeddedContentLayer], opening: true,
                                       duration: 0.18, delay: 0.14,
                                       reducedMotion: HUDRuntimeAppearance.reduceMotion)
            badge.onHitRegionChanged?()
        }
    }

    func animateExit(completion: @escaping () -> Void = {}) {
        let hadVisibleContent = phase != .hidden && phase != .waiting
        cancelAnimations()
        phase = .exiting
        let token = generation
        onHitRegionChanged?()
        // Closing before the delayed reveal must not summon a new circle.
        guard hadVisibleContent else {
            phase = .hidden
            renderer.setStage(.hidden)
            completion()
            return
        }
        renderer.animateExit { [weak self] in
            guard let self, self.generation == token else { return }
            self.phase = .hidden
            self.onHitRegionChanged?()
            completion()
        }
        HUDDeploymentFlicker.apply(to: [renderer.embeddedContentLayer], opening: false,
                                   duration: 0.11, delay: 0.07, reducedMotion: HUDRuntimeAppearance.reduceMotion)
    }

    func setHovered(_ hovered: Bool, animated: Bool = true) {
        guard phase != .hidden && phase != .waiting && phase != .exiting else { return }
        guard isHovered != hovered else { return }
        isHovered = hovered
        if phase == .presented && compactDeadlinePassed {
            renderer.morphEmbeddedStage(hovered ? .compact : .circle, animated: animated)
        }
        renderer.setEmbeddedHovered(hovered, animated: animated)
        onHitRegionChanged?()
    }

    func cancelAnimations() {
        generation += 1
        cancellations.forEach { $0() }
        cancellations.removeAll()
        renderer.cancelAnimations()
        isHovered = false
        renderer.setEmbeddedHovered(false, animated: false)
    }

    func contains(_ point: CGPoint) -> Bool {
        guard phase != .hidden && phase != .waiting && phase != .exiting else { return false }
        // Once acquired, the complete capsule remains a hover target during its
        // expansion. A small boundary allowance prevents edge jitter from
        // repeatedly reversing the morph as the HUD moves under the pointer.
        let rect = isHovered ? Self.compactHitRect.insetBy(dx: -2.5, dy: -2.5) : hitRect
        guard rect.width > 0, rect.height > 0 else { return false }
        return CGPath(roundedRect: rect, cornerWidth: rect.height / 2,
                      cornerHeight: rect.height / 2, transform: nil).contains(point)
    }

    var animationCount: Int {
        func count(_ item: CALayer) -> Int {
            (item.animationKeys()?.count ?? 0) + (item.sublayers ?? []).reduce(0) { $0 + count($1) }
        }
        return count(renderer.embeddedContentLayer)
    }

    private func scheduleCompactDeadline() {
        schedule(after: Self.compactHoldDuration, token: generation) { badge in
            guard badge.phase == .presented else { return }
            badge.compactDeadlinePassed = true
            if !badge.isHovered { badge.renderer.morphEmbeddedStage(.circle, animated: true) }
            badge.onHitRegionChanged?()
        }
    }

    private func schedule(after delay: TimeInterval, token: Int, action: @escaping (HUDChargeBadge) -> Void) {
        cancellations.append(scheduler(delay) { [weak self] in
            guard let self, self.generation == token else { return }
            action(self)
        })
    }

    private static func dispatchSchedule(after delay: TimeInterval, action: @escaping () -> Void) -> (() -> Void) {
        let item = DispatchWorkItem(block: action)
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: item)
        return { item.cancel() }
    }
}
