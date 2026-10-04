import AppKit
import QuartzCore

enum OverlayStage: String, CaseIterable {
    case hidden, circle, supercharge, compact
}

/// A finite Core Animation sequence. Every visible component has its own layer,
/// so the bolt, text and capsule can move independently without stretching text.
final class ChargeIndicatorView: NSView {
    static let canvasSize = NSSize(width: 300, height: 84)
    static let entranceDuration: TimeInterval = 1.50
    static let exitDuration: TimeInterval = 0.64
    private static let bannerTitleWidth: CGFloat = 121

    private let canvas = CALayer()
    private let shadowLayer = CALayer()
    private let body = CALayer()
    private let emblem = CAShapeLayer()
    private let bolt = CAShapeLayer()
    private let bannerEnglish = CATextLayer()
    private let bannerTitle = CATextLayer()
    private let capacityLabel = CATextLayer()
    private let percentageLabel = CATextLayer()
    private let ringContainer = CALayer()
    private let ringTrack = CAShapeLayer()
    private let ringProgress = CAShapeLayer()
    private let laptop = CAShapeLayer()
    private var ripples: [CAShapeLayer] = []
    private var scheduled: [DispatchWorkItem] = []
    private var generation = 0
    private var snapshot = BatterySnapshot.unavailable
    private var configuration = AppConfiguration.defaults
    private var preview = false
    private var embeddedDarkAppearance: Bool?
    private var embeddedContentsScale: CGFloat?
    private var embeddedHovered = false
    private(set) var stage: OverlayStage = .hidden

    override var isFlipped: Bool { true }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.addSublayer(canvas)
        canvas.anchorPoint = .zero
        canvas.position = .zero
        canvas.bounds = CGRect(origin: .zero, size: Self.canvasSize)
        // AppKit already flips the view's backing layer. Flipping this child
        // again would mirror its glyphs and icon geometry on screen.
        canvas.isGeometryFlipped = false
        canvas.addSublayer(shadowLayer)
        canvas.addSublayer(body)
        body.masksToBounds = true
        shadowLayer.shadowColor = NSColor.black.cgColor
        shadowLayer.shadowOpacity = 0.20
        shadowLayer.shadowRadius = 4
        shadowLayer.shadowOffset = CGSize(width: 0, height: 1)

        for _ in 0..<3 {
            let ripple = CAShapeLayer()
            ripple.bounds = CGRect(x: 0, y: 0, width: 320, height: 320)
            ripple.path = CGPath(ellipseIn: CGRect(x: 3, y: 3, width: 314, height: 314), transform: nil)
            ripple.fillColor = nil
            ripple.lineWidth = 5
            ripple.opacity = 0
            body.addSublayer(ripple)
            ripples.append(ripple)
        }

        canvas.addSublayer(emblem)
        canvas.addSublayer(bolt)
        bolt.bounds = CGRect(x: 0, y: 0, width: 24, height: 24)
        bolt.path = Self.boltPath()

        for textLayer in [bannerEnglish, bannerTitle, capacityLabel, percentageLabel] {
            textLayer.anchorPoint = .zero
            textLayer.alignmentMode = .left
            textLayer.truncationMode = .none
            textLayer.isWrapped = false
            canvas.addSublayer(textLayer)
        }
        percentageLabel.alignmentMode = .right
        ringContainer.bounds = CGRect(x: 0, y: 0, width: 28, height: 28)
        canvas.addSublayer(ringContainer)
        for ring in [ringTrack, ringProgress] {
            ring.frame = ringContainer.bounds
            ring.fillColor = nil
            ring.lineWidth = 2
            ring.lineCap = .round
            ringContainer.addSublayer(ring)
        }
        ringTrack.path = CGPath(ellipseIn: CGRect(x: 3, y: 3, width: 22, height: 22), transform: nil)
        let progress = CGMutablePath()
        progress.addArc(center: CGPoint(x: 14, y: 14), radius: 11,
                        startAngle: -.pi / 2, endAngle: .pi * 1.5, clockwise: false)
        ringProgress.path = progress
        laptop.frame = ringContainer.bounds
        laptop.path = Self.laptopPath()
        laptop.fillColor = nil
        laptop.lineWidth = 1.15
        laptop.lineJoin = .round
        ringContainer.addSublayer(laptop)

        setAccessibilityElement(true)
        setAccessibilityRole(.staticText)
        updateContentsScale()
        updateContent()
        apply(.hidden, duration: 0)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    deinit { scheduled.forEach { $0.cancel() } }

    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        // Bounds may stay at canvasSize while the containing view frame scales.
        // A caller that changes bounds instead is supported too.
        canvas.setAffineTransform(CGAffineTransform(scaleX: bounds.width / Self.canvasSize.width,
                                                   y: bounds.height / Self.canvasSize.height))
        CATransaction.commit()
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        updateContentsScale()
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        updateContentsScale()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        updateContent()
        apply(stage, duration: 0)
    }

    /// The vector canvas is independent of AppKit's backing layer. A retained,
    /// detached renderer may embed this child in another Core Animation scene
    /// while continuing to use the exact notification artwork and timeline.
    var embeddedContentLayer: CALayer { canvas }

    /// The visible capsule in canvas coordinates. Embedded controls use the
    /// presentation geometry while it morphs, instead of claiming blank space.
    var embeddedBodyRect: CGRect {
        let visible = body.presentation() ?? body
        let bounds = visible.bounds
        let scaleX = abs(visible.transform.m11)
        let scaleY = abs(visible.transform.m22)
        return CGRect(x: visible.position.x - bounds.width * scaleX / 2,
                      y: visible.position.y - bounds.height * scaleY / 2,
                      width: bounds.width * scaleX, height: bounds.height * scaleY)
    }

    /// Hover feedback belongs to the embedded control only; the charging
    /// notification retains its usual appearance and animation.
    func setEmbeddedHovered(_ hovered: Bool, animated: Bool) {
        guard embeddedHovered != hovered else { return }
        embeddedHovered = hovered
        let duration = animated && !reduceMotion ? 0.16 : 0
        change(body, key: "backgroundColor", to: bodyBackground.cgColor, duration: duration)
        change(body, key: "borderColor", to: bodyBorder.cgColor, duration: duration)
        change(body, key: "borderWidth", to: hovered ? 1.5 : 0.5, duration: duration)
    }

    /// A detached view has no window from which to infer display scale/theme.
    /// The HUD supplies its resolved appearance and effective pixel density.
    func configureEmbedding(dark: Bool, contentsScale: CGFloat) {
        let changedAppearance = embeddedDarkAppearance != dark
        embeddedDarkAppearance = dark
        embeddedContentsScale = contentsScale.isFinite ? min(8, max(1, contentsScale)) : 2
        updateContentsScale()
        if changedAppearance { updateContent() }
    }

    func set(snapshot: BatterySnapshot, configuration: AppConfiguration, preview: Bool = false) {
        self.snapshot = snapshot
        self.configuration = configuration
        self.preview = preview
        updateContentsScale()
        updateContent()
        updateColors(for: stage)
    }

    func setStage(_ stage: OverlayStage, animated: Bool = false) {
        cancelAnimations()
        apply(stage, duration: animated && !reduceMotion ? 0.26 : 0)
    }

    /// Pointer-driven reversals must begin at the rendered intermediate width,
    /// rather than snapping to the previous animation's destination first.
    func morphEmbeddedStage(_ stage: OverlayStage, animated: Bool) {
        generation += 1
        scheduled.forEach { $0.cancel() }
        scheduled.removeAll()
        apply(stage, duration: animated && !reduceMotion ? 0.26 : 0)
    }

    func animateEntrance(completion: @escaping () -> Void = {}) {
        cancelAnimations()
        let token = generation
        guard !reduceMotion else {
            apply(.compact, duration: 0)
            completion()
            return
        }
        apply(.hidden, duration: 0)
        apply(.circle, duration: 0.18)
        schedule(after: 0.20, token: token) { view in
            view.apply(.supercharge, duration: 0.30)
            view.startRipples()
        }
        schedule(after: 1.22, token: token) { $0.apply(.compact, duration: 0.28) }
        schedule(after: Self.entranceDuration, token: token) { _ in completion() }
    }

    func animateExit(completion: @escaping () -> Void = {}) {
        cancelAnimations()
        let token = generation
        guard !reduceMotion else {
            apply(.hidden, duration: 0)
            completion()
            return
        }
        apply(.circle, duration: 0.30)
        schedule(after: 0.30, token: token) { view in
            // Keep the window alive until the rendered shrink finishes, including
            // its final fade. A wall-clock dismissal can cut off the last frame.
            CATransaction.begin()
            CATransaction.setCompletionBlock { [weak view] in
                guard let view = view, view.generation == token else { return }
                completion()
            }
            view.apply(.hidden, duration: 0.34)
            CATransaction.commit()
        }
    }

    /// Invalidates finite delayed stages as well as their visual animations.
    func cancelAnimations() {
        generation += 1
        scheduled.forEach { $0.cancel() }
        scheduled.removeAll()
        Self.removeAnimations(from: canvas)
    }

    /// A stable model-layer frame for documentation and local diagnostics.
    func renderStage(_ stage: OverlayStage) {
        setStage(stage)
        if stage == .supercharge {
            withoutActions {
                for (index, ripple) in self.ripples.enumerated() {
                    let scale = CGFloat(index + 1) * 0.19
                    ripple.transform = CATransform3DMakeScale(scale, scale, 1)
                    ripple.opacity = Float(0.17 - Double(index) * 0.035)
                }
            }
        }
        layoutSubtreeIfNeeded()
        CATransaction.flush()
    }

    /// Export drawn model layers without a screenshot or a visible window.
    func writePNG(to url: URL) throws {
        layoutSubtreeIfNeeded()
        CATransaction.flush()
        let scale: CGFloat = 2
        let size = bounds.size
        guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil,
                                            pixelsWide: max(1, Int(size.width * scale)),
                                            pixelsHigh: max(1, Int(size.height * scale)),
                                            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                                            isPlanar: false, colorSpaceName: .deviceRGB,
                                            bytesPerRow: 0, bitsPerPixel: 0),
              let context = NSGraphicsContext(bitmapImageRep: bitmap) else {
            throw NSError(domain: "EndfieldCharge.Rendering", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "Could not create the preview bitmap."])
        }
        let cg = context.cgContext
        cg.translateBy(x: 0, y: size.height * scale)
        cg.scaleBy(x: scale, y: -scale)
        canvas.render(in: cg)
        guard let png = bitmap.representation(using: .png, properties: [:]) else {
            throw NSError(domain: "EndfieldCharge.Rendering", code: 2,
                          userInfo: [NSLocalizedDescriptionKey: "Could not encode the preview image."])
        }
        try png.write(to: url, options: .atomic)
    }

    private var reduceMotion: Bool { HUDRuntimeAppearance.reduceMotion }

    private var isDark: Bool {
        if let embeddedDarkAppearance { return embeddedDarkAppearance }
        switch configuration.theme {
        case .dark: return true
        case .light: return false
        case .system: return effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
        }
    }

    private var foreground: NSColor { isDark ? NSColor(white: 0.95, alpha: 1) : NSColor(white: 0.10, alpha: 1) }
    private var background: NSColor { isDark ? NSColor(white: 0.105, alpha: 0.99) : NSColor(white: 0.975, alpha: 0.99) }
    private var bodyBackground: NSColor { background }
    private var bodyBorder: NSColor {
        embeddedHovered ? configuration.accentColor.withAlphaComponent(0.98)
            : foreground.withAlphaComponent(isDark ? 0.045 : 0.08)
    }
    private var levelColor: NSColor {
        switch snapshot.levelTone {
        case .green?: return isDark ? NSColor(srgbRed: 0.25, green: 0.87, blue: 0.43, alpha: 1)
                                   : NSColor(srgbRed: 0.08, green: 0.62, blue: 0.27, alpha: 1)
        case .yellow?: return isDark ? NSColor(srgbRed: 0.96, green: 0.80, blue: 0.25, alpha: 1)
                                    : NSColor(srgbRed: 0.73, green: 0.49, blue: 0.02, alpha: 1)
        case .red?: return isDark ? NSColor(srgbRed: 0.98, green: 0.35, blue: 0.34, alpha: 1)
                                 : NSColor(srgbRed: 0.80, green: 0.16, blue: 0.16, alpha: 1)
        case nil: return foreground.withAlphaComponent(0.45)
        }
    }

    private func updateContent() {
        withoutActions {
            let englishMode = self.snapshot.isCharging ? "CHARGE MODE" : "BATTERY MODE"
            let localizedMode = self.snapshot.isCharging ? L10n.text("CHARGE MODE", "超充模式")
                : L10n.text("BATTERY MODE", "电池模式")
            let title = NSAttributedString(string: "// " + englishMode, attributes: [
                .font: NSFont.systemFont(ofSize: 7.5, weight: .medium),
                .foregroundColor: self.foreground.withAlphaComponent(0.55), .kern: 0.2
            ])
            self.bannerEnglish.string = title
            // Keep the original bilingual styling and capsule geometry while
            // fitting longer English and Japanese titles without clipping.
            var titleFont = NSFont.systemFont(ofSize: 20, weight: .semibold)
            while titleFont.pointSize > 10,
                  (localizedMode as NSString).size(withAttributes: [.font: titleFont]).width > Self.bannerTitleWidth - 2 {
                titleFont = NSFont.systemFont(ofSize: titleFont.pointSize - 0.5, weight: .semibold)
            }
            self.bannerTitle.string = NSAttributedString(string: localizedMode, attributes: [
                .font: titleFont, .foregroundColor: self.foreground
            ])
            let current = self.snapshot.capacity.map { Self.number($0.current) } ?? "—"
            let maximum = self.snapshot.capacity.map { Self.number($0.maximum) } ?? "—"
            let unit = self.snapshot.capacity?.unit.rawValue ?? ""
            let line = NSMutableAttributedString(string: current, attributes: [
                .font: NSFont.monospacedDigitSystemFont(ofSize: 12.5, weight: .semibold),
                .foregroundColor: self.foreground
            ])
            line.append(NSAttributedString(string: "/" + maximum, attributes: [
                .font: NSFont.monospacedDigitSystemFont(ofSize: 9.5, weight: .medium),
                .foregroundColor: self.foreground.withAlphaComponent(0.47)
            ]))
            if !unit.isEmpty {
                line.append(NSAttributedString(string: " " + unit, attributes: [
                    .font: NSFont.systemFont(ofSize: 7.5, weight: .medium),
                    .foregroundColor: self.foreground.withAlphaComponent(0.47)
                ]))
            }
            self.capacityLabel.string = line
            let percentage = self.snapshot.percentage.map { "\($0)%" } ?? "—"
            self.percentageLabel.string = NSAttributedString(string: percentage, attributes: [
                .font: NSFont.monospacedDigitSystemFont(ofSize: 13, weight: .semibold),
                .foregroundColor: self.foreground
            ])
            self.ringProgress.strokeEnd = CGFloat(min(100, max(0, self.snapshot.percentage ?? 0))) / 100
        }
        let value = snapshot.percentage.map { "\($0)%" } ?? L10n.text("Battery unavailable", "电量不可用")
        let state = snapshot.isCharging ? L10n.text("Charging", "正在充电")
            : snapshot.isPluggedIn ? L10n.text("Power connected", "已连接电源")
            : L10n.text("On battery", "使用电池")
        setAccessibilityLabel("EndfieldHUD, \(value), \(state)" + (preview ? L10n.text(", Preview", "，预览") : ""))
        updateColors(for: stage)
    }

    private func updateColors(for stage: OverlayStage) {
        withoutActions {
            self.body.backgroundColor = self.bodyBackground.cgColor
            self.shadowLayer.backgroundColor = self.background.cgColor
            self.body.borderColor = self.bodyBorder.cgColor
            self.body.borderWidth = self.embeddedHovered ? 1.5 : 0.5
            self.emblem.fillColor = (stage == .compact ? self.foreground.withAlphaComponent(0.19) : self.foreground).cgColor
            self.bolt.fillColor = (stage == .compact ? self.foreground : self.background).cgColor
            self.ringTrack.strokeColor = self.levelColor.withAlphaComponent(0.19).cgColor
            self.ringProgress.strokeColor = self.levelColor.cgColor
            self.laptop.strokeColor = self.foreground.withAlphaComponent(0.78).cgColor
            for ripple in self.ripples { ripple.strokeColor = self.foreground.cgColor }
        }
    }

    private func apply(_ next: OverlayStage, duration: TimeInterval) {
        stage = next
        let center = CGPoint(x: 150, y: 42)
        let bodyRect: CGRect
        let radius: CGFloat
        let emblemCenter: CGPoint
        let emblemSize: CGSize
        let emblemRadius: CGFloat
        let boltScale: CGFloat
        switch next {
        case .hidden, .circle:
            bodyRect = CGRect(x: 131, y: 23, width: 38, height: 38)
            radius = 19
            emblemCenter = center
            emblemSize = CGSize(width: 23, height: 23)
            emblemRadius = 11.5
            boltScale = 0.69
        case .supercharge:
            bodyRect = CGRect(x: 30, y: 18, width: 240, height: 48)
            radius = 14
            emblemCenter = CGPoint(x: 107, y: 42)
            emblemSize = CGSize(width: 23, height: 23)
            emblemRadius = 11.5
            boltScale = 0.70
        case .compact:
            bodyRect = CGRect(x: 18, y: 23, width: 264, height: 38)
            radius = 19
            emblemCenter = CGPoint(x: 37, y: 42)
            emblemSize = CGSize(width: 17, height: 20)
            emblemRadius = 3
            boltScale = 0.60
        }

        let hidden = next == .hidden
        let scale: CGFloat = hidden ? 0.001 : 1
        let isBanner = next == .supercharge
        let isCompact = next == .compact
        for surface in [shadowLayer, body] {
            change(surface, key: "bounds", to: NSValue(rect: CGRect(origin: .zero, size: bodyRect.size)), duration: duration)
            change(surface, key: "position", to: NSValue(point: center), duration: duration)
            change(surface, key: "cornerRadius", to: radius, duration: duration)
            change(surface, key: "transform", to: NSValue(caTransform3D: CATransform3DMakeScale(scale, scale, 1)), duration: duration)
            change(surface, key: "opacity", to: hidden ? 0 : 1, duration: duration)
        }
        change(emblem, key: "bounds", to: NSValue(rect: CGRect(origin: .zero, size: emblemSize)), duration: duration)
        change(emblem, key: "position", to: NSValue(point: emblemCenter), duration: duration)
        change(emblem, key: "path", to: CGPath(roundedRect: CGRect(origin: .zero, size: emblemSize),
                                              cornerWidth: emblemRadius, cornerHeight: emblemRadius, transform: nil), duration: duration)
        change(emblem, key: "transform", to: NSValue(caTransform3D: CATransform3DMakeScale(scale, scale, 1)), duration: duration)
        change(emblem, key: "opacity", to: hidden ? 0 : 1, duration: duration)
        change(bolt, key: "position", to: NSValue(point: emblemCenter), duration: duration)
        change(bolt, key: "transform", to: NSValue(caTransform3D: CATransform3DMakeScale(boltScale * scale, boltScale * scale, 1)), duration: duration)
        change(bolt, key: "opacity", to: hidden ? 0 : 1, duration: duration)

        // The text translates with the morph; it is never squashed with the body.
        place(bannerEnglish, at: CGRect(x: isBanner ? 126 : 150, y: 27, width: 116, height: 12),
              visible: isBanner, duration: duration)
        place(bannerTitle, at: CGRect(x: isBanner ? 124 : 149, y: 36, width: Self.bannerTitleWidth, height: 28),
              visible: isBanner, duration: duration)
        place(capacityLabel, at: CGRect(x: isCompact ? 55 : 83, y: 33, width: 143, height: 19),
              visible: isCompact, duration: duration)
        place(percentageLabel, at: CGRect(x: isCompact ? 205 : 192, y: 32.5, width: 40, height: 20),
              visible: isCompact, duration: duration)
        change(ringContainer, key: "position", to: NSValue(point: CGPoint(x: isCompact ? 263 : 239, y: 42)), duration: duration)
        change(ringContainer, key: "opacity", to: isCompact ? 1 : 0, duration: duration)
        for ripple in ripples {
            change(ripple, key: "position", to: NSValue(point: CGPoint(x: emblemCenter.x - bodyRect.minX,
                                                                      y: emblemCenter.y - bodyRect.minY)), duration: duration)
            if !isBanner { change(ripple, key: "opacity", to: 0, duration: min(duration, 0.12)) }
        }
        updateColors(for: next)
    }

    private func place(_ layer: CALayer, at rect: CGRect, visible: Bool, duration: TimeInterval) {
        change(layer, key: "bounds", to: NSValue(rect: CGRect(origin: .zero, size: rect.size)), duration: 0)
        change(layer, key: "position", to: NSValue(point: rect.origin), duration: duration)
        change(layer, key: "opacity", to: visible ? 1 : 0, duration: min(duration, 0.20))
    }

    private func startRipples() {
        let start = CACurrentMediaTime()
        for (index, ripple) in ripples.enumerated() {
            withoutActions {
                ripple.opacity = 0
                ripple.transform = CATransform3DMakeScale(1.10, 1.10, 1)
            }
            let expansion = CABasicAnimation(keyPath: "transform.scale")
            expansion.fromValue = 0.06
            expansion.toValue = 1.10
            let fade = CAKeyframeAnimation(keyPath: "opacity")
            fade.values = [0, 0.20, 0.14, 0]
            fade.keyTimes = [0, 0.12, 0.52, 1]
            let group = CAAnimationGroup()
            group.animations = [expansion, fade]
            group.duration = 0.76
            group.beginTime = ripple.convertTime(start, from: nil) + Double(index) * 0.15
            group.timingFunction = CAMediaTimingFunction(name: .easeOut)
            group.fillMode = .backwards
            ripple.add(group, forKey: "ripple")
        }
    }

    private func schedule(after delay: TimeInterval, token: Int, action: @escaping (ChargeIndicatorView) -> Void) {
        let item = DispatchWorkItem { [weak self] in
            guard let self = self, self.generation == token else { return }
            action(self)
        }
        scheduled.append(item)
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: item)
    }

    private func change(_ layer: CALayer, key: String, to value: Any, duration: TimeInterval) {
        let previous = layer.presentation()?.value(forKeyPath: key) ?? layer.value(forKeyPath: key)
        withoutActions { layer.setValue(value, forKeyPath: key) }
        guard duration > 0, let previous = previous else {
            layer.removeAnimation(forKey: key)
            return
        }
        let animation: CAPropertyAnimation
        if stage == .hidden && key == "opacity" {
            // Let the circle visibly shrink before fading its final few pixels.
            let fade = CAKeyframeAnimation(keyPath: key)
            fade.values = [previous, previous, value]
            fade.keyTimes = [0, 0.68, 1]
            fade.timingFunctions = [CAMediaTimingFunction(name: .linear),
                                    CAMediaTimingFunction(name: .easeInEaseOut)]
            animation = fade
        } else {
            let movement = CABasicAnimation(keyPath: key)
            movement.fromValue = previous
            movement.toValue = value
            movement.timingFunction = stage == .hidden
                ? CAMediaTimingFunction(name: .easeInEaseOut)
                : CAMediaTimingFunction(controlPoints: 0.18, 0.78, 0.22, 1)
            animation = movement
        }
        animation.duration = duration
        layer.add(animation, forKey: key)
    }

    private func withoutActions(_ changes: () -> Void) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        changes()
        CATransaction.commit()
    }

    private func updateContentsScale() {
        let backing = window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2
        let scale = embeddedContentsScale ?? (backing * CGFloat(configuration.normalized.scale))
        func applyScale(_ layer: CALayer) {
            layer.contentsScale = scale
            layer.sublayers?.forEach(applyScale)
        }
        applyScale(canvas)
    }

    private static func removeAnimations(from layer: CALayer) {
        layer.removeAllAnimations()
        layer.sublayers?.forEach(removeAnimations)
    }

    private static func number(_ value: Int) -> String {
        let formatter = NumberFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.numberStyle = .decimal
        formatter.usesGroupingSeparator = true
        formatter.maximumFractionDigits = 0
        return formatter.string(from: NSNumber(value: value)) ?? String(value)
    }

    private static func boltPath() -> CGPath {
        let path = CGMutablePath()
        path.move(to: CGPoint(x: 14, y: 2.8))
        path.addLine(to: CGPoint(x: 5.5, y: 13.3))
        path.addLine(to: CGPoint(x: 11.0, y: 13.3))
        path.addLine(to: CGPoint(x: 9.5, y: 21.2))
        path.addLine(to: CGPoint(x: 18.5, y: 10.2))
        path.addLine(to: CGPoint(x: 13.0, y: 10.2))
        path.closeSubpath()
        return path
    }

    private static func laptopPath() -> CGPath {
        let path = CGMutablePath()
        path.addRoundedRect(in: CGRect(x: 8.5, y: 8, width: 11, height: 9), cornerWidth: 0.7, cornerHeight: 0.7)
        path.move(to: CGPoint(x: 7, y: 19))
        path.addLine(to: CGPoint(x: 21, y: 19))
        path.move(to: CGPoint(x: 8.5, y: 17))
        path.addLine(to: CGPoint(x: 7, y: 19))
        path.move(to: CGPoint(x: 19.5, y: 17))
        path.addLine(to: CGPoint(x: 21, y: 19))
        return path
    }
}
