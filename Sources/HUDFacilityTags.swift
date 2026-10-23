import AppKit
import QuartzCore

/// The facility tag stack under the HUD title (spec §3.29, §3.26): a
/// right-aligned column, 272 wide with 6 px gaps, of chamfered tags with a
/// moving hazard strip, the warning glyph, the text with its bold orange value
/// and a × button. Recording mode shows in the clock's status row, not here.
/// Updates diff against what is shown: new tags slide in, removed ones collapse, the
/// rest move to their new places. Coordinates are content px; the host places
/// and scales `layer` (flat in shell space at stage 300, 214). The layer is
/// anchored at its top-left corner, so its animated height grows downwards.
final class HUDFacilityTagStack {
    static let width: CGFloat = 272
    static let tagHeight: CGFloat = 28
    static let gap: CGFloat = 6

    let layer = HUDLayer.plain("facility.tags")
    let env: HUDDesignEnvironment
    /// A tag was clicked: open Storage / Activity / Battery.
    var onOpen: ((FacilityAlertKind) -> Void)?
    /// × was pressed. Called at the start of the dismiss sequence; the stack
    /// finishes the strike-through and collapse after the alert leaves the list.
    var onDismiss: ((FacilityAlertKind) -> Void)?
    /// Loops (hazard strips) run only while active; the
    /// host turns this off while the HUD is hidden.
    var isActive = true { didSet { if isActive != oldValue { tags.forEach { $0.setLooping(isActive && $0.state != .collapsing) } } } }
    /// The settled height of the column (the layer's height animates to it).
    private(set) var preferredHeight: CGFloat = 0

    private let timers: HUDTimers
    /// Every tag on screen in column order, leaving ones included.
    private var tags: [FacilityTag] = []

    init(env: HUDDesignEnvironment, timers: HUDTimers = .main) {
        self.env = env; self.timers = timers
        HUDMotion.perform { layer.anchorPoint = .zero; layer.bounds = CGRect(x: 0, y: 0, width: Self.width, height: 0) }
    }

    /// Tags and their × buttons (later entries are on top), for the host's
    /// router and accessibility. It changes with `update` and when × is pressed.
    var controls: [HUDControl] {
        tags.filter { $0.state == .live }.flatMap { tag -> [HUDControl] in [tag.control, tag.close].compactMap { $0 } }
    }

    /// Shows `alerts` in raise order, one tag per kind.
    func update(alerts: [FacilityAlert], animated: Bool) {
        let animate = animated && !env.reducesMotion
        var wanted: [FacilityAlert] = []
        for alert in alerts where !wanted.contains(where: { $0.kind == alert.kind }) { wanted.append(alert) }
        let wantedKinds = wanted.map(\.kind)
        for tag in tags where tag.state == .live && !wantedKinds.contains(tag.kind) { collapse(tag, animated: animate) }
        tags.removeAll { $0.state == .removed }
        var live: [FacilityTag] = []
        for alert in wanted {
            if let tag = tags.first(where: { $0.kind == alert.kind && $0.state == .live }) {
                tag.render(alert)
                live.append(tag)
            } else {
                let tag = makeTag(alert)
                tag.isNew = true
                live.append(tag)
            }
        }
        // Leaving tags keep their index among the others until they are removed.
        var order = live
        for (index, tag) in tags.enumerated() where tag.state != .live { order.insert(tag, at: min(index, order.count)) }
        tags = order
        layout(animated: animate)
    }

    private func makeTag(_ alert: FacilityAlert) -> FacilityTag {
        let tag = FacilityTag(kind: alert.kind, env: env)
        tag.render(alert)
        tag.control.onActivate = { [weak self, weak tag] in
            guard let self, let tag, tag.state == .live else { return }
            self.onOpen?(tag.kind)
        }
        tag.close.onActivate = { [weak self, weak tag] in
            guard let self, let tag else { return }
            self.dismiss(tag)
        }
        HUDMotion.perform { layer.addSublayer(tag.layer) }
        env.applyScale(to: tag.layer)
        tag.setLooping(isActive)
        return tag
    }

    /// Positions the column: leaving tags in their strike-through hold keep
    /// their place; collapsing ones give it up.
    private func layout(animated: Bool) {
        var y: CGFloat = 0
        for tag in tags {
            guard tag.state != .collapsing, tag.state != .removed else { continue }
            let width = min(Self.width, tag.width)
            let origin = CGPoint(x: Self.width - width, y: y)
            if tag.isNew {
                tag.isNew = false
                tag.place(at: origin)
                if animated { tag.enter() }
            } else if tag.origin.y != origin.y {
                tag.move(to: origin, animated: animated)
            } else if tag.origin.x != origin.x {
                tag.place(at: origin)
            }
            y += Self.tagHeight + Self.gap
        }
        preferredHeight = max(0, y - Self.gap)
        // An unchanged column adds no track (the host re-runs this on every alert check).
        guard layer.bounds.height != preferredHeight else { return }
        HUDMotion.animate(layer, "bounds.size.height", to: preferredHeight, duration: animated ? 0.25 : 0, timing: HUDEasing.ease, env: env)
    }

    /// ×: strike-through (.28 s), collapse at 300 ms, removed at 560 ms.
    private func dismiss(_ tag: FacilityTag) {
        guard tag.state == .live else { return }
        let kind = tag.kind
        tag.state = .holding
        tag.disable()
        if env.reducesMotion {
            collapse(tag, animated: false)
            tags.removeAll { $0.state == .removed }
            layout(animated: false)
        } else {
            tag.strike()
            timers.after(0.3) { [weak self, weak tag] in
                guard let self, let tag, tag.state == .holding else { return }
                self.collapse(tag, animated: true)
                self.layout(animated: true)
            }
        }
        onDismiss?(kind)
    }

    /// `.gone` (.25 s), then removed.
    private func collapse(_ tag: FacilityTag, animated: Bool) {
        tag.state = .collapsing
        tag.disable()
        tag.setLooping(false)
        guard animated else { remove(tag); return }
        tag.collapse()
        timers.after(0.26) { [weak self, weak tag] in
            guard let self, let tag else { return }
            self.remove(tag)
            self.tags.removeAll { $0 === tag }
        }
    }

    private func remove(_ tag: FacilityTag) {
        tag.state = .removed
        tag.setLooping(false)
        HUDMotion.perform { tag.layer.removeFromSuperlayer() }
    }

    // MARK: Verification

    var kindsForTesting: [FacilityAlertKind] { tags.filter { $0.state == .live }.map(\.kind) }
    var shownKindsForTesting: [FacilityAlertKind] { tags.map(\.kind) }
    func tagForTesting(_ kind: FacilityAlertKind) -> (text: String, frame: CGRect, looping: Bool, struck: Bool)? {
        tags.first { $0.kind == kind && $0.state != .removed }.map { ($0.text.text, $0.layer.frame, $0.isLooping, $0.isStruck) }
    }
}

/// One tag of the stack.
private final class FacilityTag {
    enum State { case live, holding, collapsing, removed }
    static let textStyle = HUDTextStyle(size: 11, weight: .bold, color: HUDColor.rgb(0xF2F2F2), tracking: 0.06)

    let kind: FacilityAlertKind
    let env: HUDDesignEnvironment
    let layer = HUDLayer.plain("facility.tag")
    let control: HUDFacilityControl
    let close: HUDFacilityControl
    let text = HUDTextLayer(style: FacilityTag.textStyle)
    var state = State.live
    var isNew = false
    private(set) var width: CGFloat = 0
    private(set) var origin = CGPoint.zero
    private(set) var isStruck = false
    private let content = HUDLayer.plain("facility.tag.content")
    private let plate = HUDLayer.shape("facility.tag.plate")
    private let ring = HUDLayer.shape("facility.tag.ring")
    private let hazard: HUDHazardStripes
    private let hazardClip = HUDLayer.shape("facility.tag.hazardClip")
    private let glyph = HUDIconLayer(.warn, size: 13, color: HUDColor.orange)
    private let brighten = HUDLayer.shape("facility.tag.brighten")
    private let cross = HUDLayer.plain("facility.tag.cross")
    private let closeGlyph = HUDIconLayer(.x, size: 10, color: HUDColor.rgb(0xAAAAAA))
    private let closeHover = HUDIconLayer(.x, size: 10, color: HUDColor.white(1))

    init(kind: FacilityAlertKind, env: HUDDesignEnvironment) {
        self.kind = kind; self.env = env
        control = HUDFacilityControl(id: "facility.tag." + kind.rawValue, env: env, layer: layer)
        close = HUDFacilityControl(id: "facility.tag." + kind.rawValue + ".dismiss", env: env)
        hazard = HUDHazardStripes(size: CGSize(width: 12, height: HUDFacilityTagStack.tagHeight), period: 8 * CGFloat(2).squareRoot(),
                                  color: HUDColor.orange, cycle: 0.6)
        HUDMotion.perform {
            layer.anchorPoint = CGPoint(x: 0.5, y: 0)
            layer.masksToBounds = true
            plate.fillColor = HUDColor.rgb(0x121212, 0.92)
            ring.fillColor = HUDColor.rgb(0x4A3216)
            hazardClip.frame = hazard.layer.bounds; hazardClip.fillColor = HUDColor.black(1)
            hazardClip.path = HUDShape.chamfer(hazard.layer.bounds, cut: 10, corners: .bottomLeft)
            hazard.layer.mask = hazardClip
            brighten.fillColor = HUDColor.white(0.05); brighten.opacity = 0
            cross.backgroundColor = HUDColor.orange; cross.anchorPoint = CGPoint(x: 0, y: 0.5)
            cross.setValue(0, forKeyPath: "transform.scale.x")
            [plate, ring, hazard.layer, glyph, text].forEach(content.addSublayer)
            close.layer.bounds = CGRect(x: 0, y: 0, width: 20, height: 20)
            for icon in [closeGlyph, closeHover] { icon.position = CGPoint(x: 10, y: 10); close.layer.addSublayer(icon) }
            closeHover.opacity = 0
            content.addSublayer(close.layer)
            content.addSublayer(cross); content.addSublayer(brighten)
            layer.addSublayer(content)
        }
        control.onHover = { [weak self] in self?.hover($0) }
        close.onHover = { [weak self] in self?.hoverClose($0) }
    }

    /// `DISK 8% FREE`: label, bold orange value, optional suffix.
    func render(_ alert: FacilityAlert) {
        var runs = [HUDText.Run(alert.tagLabel + " ", Self.textStyle), HUDText.Run(alert.value, Self.textStyle.colored(HUDColor.orange))]
        if let suffix = alert.tagSuffix { runs.append(HUDText.Run(" " + suffix, Self.textStyle)) }
        control.label = alert.title + ", " + alert.tagText
        close.label = L10n.text("Dismiss", "忽略") + ", " + alert.title
        guard runs != text.runs || width == 0 else { return }
        HUDMotion.perform { text.runs = runs }
        layout()
    }

    /// Flex row, gap 8, padding-right 4: strip 12, glyph, text, then × (20).
    private func layout() {
        let h = HUDFacilityTagStack.tagHeight
        let glyphWidth: CGFloat = 13
        let textX = 12 + 8 + glyphWidth + 8
        let natural = ceil(text.preferredWidth)
        let trailing: CGFloat = 8 + 20 + 4
        width = textX + natural + trailing
        let w = min(HUDFacilityTagStack.width, width), textWidth = max(0, w - textX - trailing)
        let bounds = CGRect(x: 0, y: 0, width: w, height: h)
        HUDMotion.perform {
            layer.bounds = bounds; content.frame = bounds
            let silhouette = HUDShape.chamfer(bounds, cut: 10, corners: .bottomLeft)
            plate.frame = bounds; plate.path = silhouette
            brighten.frame = bounds; brighten.path = silhouette
            // inset 0 0 0 1px, clipped by the chamfer: no ring along the cut.
            let edges = CGMutablePath()
            edges.addRect(CGRect(x: 0, y: 0, width: w, height: 1)); edges.addRect(CGRect(x: w - 1, y: 0, width: 1, height: h))
            edges.addRect(CGRect(x: 10, y: h - 1, width: w - 10, height: 1)); edges.addRect(CGRect(x: 0, y: 0, width: 1, height: h - 10))
            ring.frame = bounds; ring.path = edges
            glyph.position = CGPoint(x: 20 + glyphWidth / 2, y: h / 2)
            text.lineFrame = CGRect(x: textX, y: (h - 11) / 2, width: textWidth, height: 11)
            close.layer.position = CGPoint(x: w - 4 - 10, y: h / 2)
            cross.bounds = CGRect(x: 0, y: 0, width: max(0, w - 14 - 28), height: 2)
            cross.position = CGPoint(x: 14, y: h / 2)
        }
        let silhouette = HUDShape.chamfer(bounds, cut: 10, corners: .bottomLeft)
        control.setTarget(bounds, path: silhouette, container: true)
        control.hitPath = silhouette
        close.setTarget(CGRect(x: 0, y: 0, width: 20, height: 20))
        env.applyScale(to: text)
    }

    func place(at point: CGPoint) {
        origin = point
        HUDMotion.perform { layer.position = CGPoint(x: point.x + layer.bounds.width / 2, y: point.y) }
    }

    /// Slides to a new place (transform .25 s, the stack's height transition).
    func move(to point: CGPoint, animated: Bool) {
        origin = point
        HUDMotion.animate(layer, "position", to: NSValue(point: CGPoint(x: point.x + layer.bounds.width / 2, y: point.y)),
                          duration: animated ? 0.25 : 0, timing: HUDEasing.ease, env: env)
    }

    /// ftIn .35 s E: from +16 px x and transparent.
    func enter() {
        let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 0; fade.toValue = 1
        let slide = CABasicAnimation(keyPath: "transform.translation.x"); slide.fromValue = 16; slide.toValue = 0
        slide.isAdditive = true
        let group = CAAnimationGroup(); group.animations = [fade, slide]
        group.duration = 0.35; group.timingFunction = HUDEasing.standard
        layer.add(group, forKey: "hud.enter")
    }

    var isLooping: Bool { hazard.isRunning }

    func setLooping(_ on: Bool) {
        if on, state != .removed { hazard.start(env: env) } else { hazard.stop() }
    }

    /// Hover: translateX(−3px) and brightness 1.1 (.22 s).
    private func hover(_ on: Bool) {
        let lift = on && state == .live
        HUDMotion.animate(layer, "transform.translation.x", to: lift ? -3 : 0, duration: HUDDuration.hover, env: env)
        HUDMotion.animate(brighten, "opacity", to: Float(lift ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
    }

    /// ×: #aaa → #fff and scale 1.15 (.2 s).
    private func hoverClose(_ on: Bool) {
        HUDMotion.animate(closeHover, "opacity", to: Float(on ? 1 : 0), duration: 0.2, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(close.layer, "transform.scale", to: on ? 1.15 : 1, duration: 0.2, env: env)
    }

    func disable() {
        control.isEnabled = false; close.isEnabled = false
        control.clearInteractionState(); close.clearInteractionState()
    }

    /// `.cross`: the 2 px orange strike scales in from the left (.28 s E).
    func strike() {
        isStruck = true
        HUDMotion.animate(cross, "transform.scale.x", to: 1, duration: 0.28, env: env)
    }

    /// `.gone`: opacity 0, height 0 and margin-top −6 (.25 s); the content
    /// stays centred in the shrinking box.
    func collapse() {
        let duration: CFTimeInterval = 0.25
        HUDMotion.animate(layer, "opacity", to: Float(0), duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(layer, "bounds.size.height", to: 0, duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(layer, "position.y", to: origin.y - HUDFacilityTagStack.gap, duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(content, "transform.translation.y", to: -HUDFacilityTagStack.tagHeight / 2, duration: duration,
                          timing: HUDEasing.ease, env: env)
    }
}
