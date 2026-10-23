import AppKit
import QuartzCore

/// Hover and press motion shared by buttons (spec §1.9): ease in and back out
/// over .22 s; the press is a short depth push.
extension HUDControl {
    func applyHoverMotion(lift: CGFloat = 0, hoverScale: CGFloat = 1, pressScale: CGFloat, brighten overlay: CALayer?) {
        let pressed = isPressed && isEnabled, hovered = isHovered && isEnabled
        HUDMotion.animate(layer, "transform.translation.y", to: hovered && !pressed ? -lift : 0,
                          duration: HUDDuration.hover, env: env)
        HUDMotion.animate(layer, "transform.scale", to: pressed ? pressScale : (hovered ? hoverScale : 1),
                          duration: pressed ? HUDDuration.press : HUDDuration.hover, env: env)
        if let overlay {
            HUDMotion.animate(overlay, "opacity", to: hovered ? Float(1) : Float(0), duration: HUDDuration.hover,
                              timing: HUDEasing.ease, env: env)
        }
    }
}

/// Primary (yellow), secondary (dark, bar → ▶) and light buttons (spec §2.8).
final class HUDButton: HUDControl {
    enum Kind { case primary, secondary, light }
    enum Size {
        case small, regular, large
        var height: CGFloat { self == .small ? 26 : self == .large ? 40 : 32 }
        var padding: CGFloat { self == .small ? 11 : self == .large ? 22 : 16 }
        var fontSize: CGFloat { self == .small ? 10.5 : self == .large ? 13 : 12 }
    }

    let kind: Kind
    let size: Size
    var title: String { didSet { if title != oldValue { label = title; layoutContents() } } }
    private let icon: HUDIcon?
    private let plate = HUDLayer.plain("button.plate")
    private let brighten = HUDLayer.shape("button.brighten")
    private let text: HUDTextLayer
    private var iconLayer: HUDIconLayer?
    private let bar = HUDLayer.shape("button.bar")
    private let corner = HUDLayer.plain("button.pixel")

    init(id: String, title: String, icon: HUDIcon? = nil, kind: Kind, size: Size = .regular, env: HUDDesignEnvironment) {
        self.kind = kind; self.size = size; self.title = title; self.icon = icon
        let color = kind == .secondary ? HUDColor.textOnDark : (kind == .light ? HUDColor.counterBackground : HUDColor.nearBlack)
        text = HUDTextLayer(title, style: HUDType.button.sized(size.fontSize).colored(color))
        super.init(id: id, label: title, env: env)
        layer.addSublayer(plate); layer.addSublayer(brighten)
        brighten.fillColor = HUDColor.white(0.07); brighten.opacity = 0
        if let icon { let glyph = HUDIconLayer(icon, size: 15, color: color); iconLayer = glyph; layer.addSublayer(glyph) }
        layer.addSublayer(text)
        if kind == .secondary { bar.fillColor = HUDColor.accentInk; layer.addSublayer(bar) }
        if kind == .primary { corner.backgroundColor = HUDColor.nearBlack; layer.addSublayer(corner) }
        layoutContents()
    }

    var preferredWidth: CGFloat {
        let leading = kind == .secondary ? (size == .small ? 20 : 22) : size.padding
        // An icon-only button has no gap after its icon (flex gap).
        return ceil(leading + (icon == nil ? 0 : 15 + (title.isEmpty ? 0 : 8)) + HUDText.width(title, text.textStyle) + size.padding)
    }

    /// Sizes the button to its content at `origin`; a larger `width` stretches
    /// it (flex: 1) with the icon and title centred.
    func place(at origin: CGPoint, width: CGFloat? = nil) {
        HUDMotion.perform { layer.frame = CGRect(x: origin.x, y: origin.y, width: max(preferredWidth, width ?? 0), height: size.height) }
        layoutContents()
    }

    private func layoutContents() {
        let bounds = CGRect(origin: .zero, size: CGSize(width: max(layer.bounds.width, preferredWidth), height: size.height))
        HUDMotion.perform {
            layer.bounds.size = bounds.size
            plate.frame = bounds; brighten.frame = bounds
            switch kind {
            case .primary:
                plate.showArtwork(HUDArtwork.plate(HUDColor.primaryButton, locations: [0, 0.38, 1], size: bounds.size,
                                                   cut: HUDMetric.buttonChamfer, scale: env.renderScale), scale: env.renderScale)
                brighten.path = HUDShape.chamfer(bounds, cut: HUDMetric.buttonChamfer)
                corner.frame = CGRect(x: bounds.maxX - 6 - 4, y: bounds.maxY - 5 - 4, width: 4, height: 4)
            case .light:
                plate.showArtwork(HUDArtwork.plate(HUDColor.lightButton, size: bounds.size, cut: HUDMetric.buttonChamfer,
                                                   scale: env.renderScale), scale: env.renderScale)
                brighten.path = HUDShape.chamfer(bounds, cut: HUDMetric.buttonChamfer)
            case .secondary:
                plate.contents = nil
                plate.backgroundColor = isHovered && isEnabled ? HUDColor.secondaryButtonHover : HUDColor.secondaryButton
                brighten.path = CGPath(rect: bounds, transform: nil)
                bar.frame = CGRect(x: 8, y: (bounds.height - 12) / 2, width: 7, height: 12)
                bar.path = Self.barPath(arrow: isHovered && isEnabled)
            }
            let leading = kind == .secondary ? (size == .small ? 20 : 22) : size.padding
            var x = leading + max(0, (bounds.width - preferredWidth) / 2).rounded()
            if let iconLayer { iconLayer.frame = CGRect(x: x, y: (bounds.height - 15) / 2, width: 15, height: 15); x += 15 + 8 }
            text.lineFrame = CGRect(x: x, y: (bounds.height - size.fontSize) / 2, width: bounds.width - x - size.padding + 2, height: size.fontSize)
            layer.opacity = isEnabled ? 1 : 0.35
        }
        setTarget(bounds, path: kind == .secondary ? nil : HUDShape.chamfer(bounds, cut: HUDMetric.buttonChamfer))
        env.applyScale(to: text)
    }

    /// The 4 × 12 bar and the ▶ it morphs into share one 4-point topology.
    static func barPath(arrow: Bool) -> CGPath {
        let path = CGMutablePath()
        let points = arrow ? [CGPoint(x: 2, y: 2.4), CGPoint(x: 9, y: 6), CGPoint(x: 9, y: 6), CGPoint(x: 2, y: 9.6)]
                           : [CGPoint(x: 0, y: 0), CGPoint(x: 4, y: 0), CGPoint(x: 4, y: 12), CGPoint(x: 0, y: 12)]
        path.addLines(between: points); path.closeSubpath()
        return path
    }

    override func hoverChanged() {
        applyHoverMotion(lift: 1, pressScale: 0.965, brighten: brighten)
        if kind == .secondary {
            let on = isHovered && isEnabled
            HUDMotion.animate(plate, "backgroundColor", to: on ? HUDColor.secondaryButtonHover : HUDColor.secondaryButton,
                              duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
            HUDMotion.animate(bar, "path", to: Self.barPath(arrow: on), duration: 0.2, timing: HUDEasing.ease, env: env)
        }
    }
    override func pressChanged() { applyHoverMotion(lift: 1, pressScale: 0.965, brighten: brighten) }
    override func enabledChanged() { HUDMotion.perform { layer.opacity = isEnabled ? 1 : 0.35 }; hoverChanged() }

}

/// Square (or round) icon buttons, the round back button (spec §2.8.5–6).
final class HUDIconButton: HUDControl {
    enum Style { case dark, on, light, back }
    let style: Style
    private let plate = HUDLayer.plain("icon.plate")
    private let glyph: HUDIconLayer
    private let side: CGFloat

    init(id: String, label: String, icon: HUDIcon, style: Style = .dark, round: Bool = false, big: Bool = false,
         side customSide: CGFloat? = nil, iconSize customIconSize: CGFloat? = nil, env: HUDDesignEnvironment) {
        self.style = style
        side = customSide ?? (big ? 44 : 30)
        let iconSize: CGFloat = customIconSize ?? (style == .back ? 14 : (big ? 20 : 15))
        glyph = HUDIconLayer(icon, size: iconSize, color: HUDColor.textOnDark)
        super.init(id: id, label: label, env: env)
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: side, height: side)
            plate.frame = layer.bounds
            plate.cornerRadius = style == .back || round ? side / 2 : 0
            plate.borderWidth = style == .back ? (side < 30 ? 1.5 : 2) : (style == .on ? 0 : 1)
            glyph.position = CGPoint(x: side / 2, y: side / 2)
            layer.addSublayer(plate); layer.addSublayer(glyph)
        }
        applyStyle()
        setTarget(layer.bounds, path: plate.cornerRadius > 0 ? CGPath(ellipseIn: layer.bounds, transform: nil) : nil)
    }

    var isOn = false { didSet { if isOn != oldValue { applyStyle() } } }

    /// Swaps the glyph in place (Volume's mute button shows `mute` while muted).
    func setIcon(_ icon: HUDIcon) { HUDMotion.perform { glyph.set(icon, size: glyph.bounds.width) } }

    private func applyStyle() {
        let hovered = isHovered && isEnabled
        let (fill, ring, icon): (CGColor, CGColor, CGColor)
        switch (style, isOn) {
        case (.back, _): (fill, ring, icon) = (hovered ? HUDColor.foreground(1) : HUDColor.clear, HUDColor.tone(0xDDDDDD),
                                               hovered ? HUDColor.tone(0x111111) : HUDColor.foreground(1))
        case (_, true): (fill, ring, icon) = (HUDColor.yellow, HUDColor.clear, HUDColor.nearBlack)
        case (.light, _): (fill, ring, icon) = (HUDColor.lightButton[0], HUDColor.rgb(0xB9BAB5), HUDColor.counterBackground)
        default: (fill, ring, icon) = (HUDColor.iconButton, hovered ? HUDColor.tone(0x888888) : HUDColor.ringIcon, HUDColor.textOnDark)
        }
        let duration = style == .back ? 0.12 : HUDDuration.hover
        HUDMotion.animate(plate, "backgroundColor", to: fill, duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(plate, "borderColor", to: style == .on || isOn ? HUDColor.clear : ring, duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.perform { glyph.color = icon; layer.opacity = isEnabled ? 1 : 0.35 }
    }

    override func hoverChanged() {
        applyStyle()
        if style != .back { applyHoverMotion(hoverScale: 1.06, pressScale: 0.94, brighten: nil) }
    }
    override func pressChanged() { if style != .back { applyHoverMotion(hoverScale: 1.06, pressScale: 0.94, brighten: nil) } }
    override func enabledChanged() { applyStyle() }

}

/// Confirm-bar pills (spec §2.8.7): line, label, glyph circle.
final class HUDPill: HUDControl {
    enum Kind { case cancel, confirm, danger }
    let kind: Kind
    private let plate = HUDLayer.plain("pill.plate")
    private let outer = HUDLayer.plain("pill.ring")
    private let line = HUDLayer.plain("pill.line")
    private let text: HUDTextLayer
    private let glyphPlate = HUDLayer.plain("pill.glyph")
    private let glyph: HUDIconLayer

    init(id: String, title: String, icon: HUDIcon, kind: Kind, env: HUDDesignEnvironment) {
        self.kind = kind
        // On the confirm band: light theme inverts the neutrals with the band.
        let foreground = kind == .cancel ? HUDColor.tone(0xECECEC) : HUDColor.tone(0x161616)
        text = HUDTextLayer(title, style: HUDType.pill.colored(foreground))
        glyph = HUDIconLayer(icon, size: 14, color: kind == .cancel ? HUDColor.tone(0x2A2A2A) : (kind == .danger ? HUDColor.white(1) : HUDColor.foreground(1)))
        super.init(id: id, label: title, env: env)
        let width = max(150, 16 + 22 + 8 + HUDText.width(title, HUDType.pill) + 10 + 28 + 5)
        let bounds = CGRect(x: 0, y: 0, width: width, height: 38)
        HUDMotion.perform {
            layer.bounds = bounds
            if kind != .cancel {
                outer.frame = bounds.insetBy(dx: -3, dy: -3); outer.cornerRadius = 22
                outer.borderWidth = 1
                outer.borderColor = kind == .danger ? HUDColor.rgb(0xE5383B, 0.9) : HUDColor.tone(0xE2E0E1, 0.9)
                let gap = HUDLayer.plain(); gap.frame = CGRect(x: 1, y: 1, width: outer.bounds.width - 2, height: outer.bounds.height - 2)
                gap.cornerRadius = 21; gap.borderWidth = 2; gap.borderColor = HUDColor.tone(0x1D1D1D)
                outer.addSublayer(gap); layer.addSublayer(outer)
            }
            plate.frame = bounds; plate.cornerRadius = 19
            plate.backgroundColor = kind == .cancel ? HUDColor.tone(0x4A4A4A, 0.9) : HUDColor.tone(0xE2E0E1)
            layer.addSublayer(plate)
            let glyphRect = CGRect(x: bounds.width - 5 - 28, y: 5, width: 28, height: 28)
            glyphPlate.frame = glyphRect; glyphPlate.cornerRadius = 14
            glyphPlate.backgroundColor = kind == .cancel ? HUDColor.tone(0xEDEDED) : (kind == .danger ? HUDColor.red : HUDColor.tone(0x1B1B1B))
            glyph.position = CGPoint(x: 14, y: 14); glyphPlate.addSublayer(glyph)
            let labelWidth = HUDText.width(title, HUDType.pill)
            let labelX = glyphRect.minX - 10 - labelWidth
            text.lineFrame = CGRect(x: labelX, y: 12, width: labelWidth + 2, height: 14)
            line.frame = CGRect(x: 16, y: 18.5, width: max(22, labelX - 8 - 16), height: 1)
            line.backgroundColor = foreground; line.opacity = 0.4
            [line, text, glyphPlate].forEach(layer.addSublayer)
        }
        setTarget(bounds, path: CGPath(roundedRect: bounds, cornerWidth: 19, cornerHeight: 19, transform: nil))
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        let fill: CGColor = kind == .cancel ? (on ? HUDColor.tone(0x606060, 0.95) : HUDColor.tone(0x4A4A4A, 0.9))
                                            : (on ? HUDColor.foreground(1) : HUDColor.tone(0xE2E0E1))
        HUDMotion.animate(plate, "backgroundColor", to: fill, duration: 0.12, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(line, "opacity", to: Float(on ? 0.75 : 0.4), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(glyphPlate, "transform", to: NSValue(caTransform3D: on
            ? CATransform3DRotate(CATransform3DMakeScale(1.1, 1.1, 1), -8 * .pi / 180, 0, 0, 1) : CATransform3DIdentity),
                          duration: 0.25, env: env)
    }
    override func pressChanged() {
        HUDMotion.animate(layer, "transform.scale", to: isPressed ? 0.97 : 1, duration: HUDDuration.press, timing: HUDEasing.ease, env: env)
    }

}

/// A colour swatch (spec §2.17 `.sws`; Notes, Projection, Settings): a square
/// with a 1 px #111 ring; selected adds a 2 px #111 ring and a 3 px yellow one
/// outside it. Without a colour it is the conic custom-colour circle.
final class HUDSwatch: HUDControl {
    let color: CGColor?
    private let fill: CALayer
    private let ring = HUDLayer.plain("swatch.ring")
    var isSelected = false { didSet { if isSelected != oldValue { render() } } }

    init(id: String, label: String, color: CGColor?, size: CGFloat = 24, env: HUDDesignEnvironment) {
        self.color = color
        if color == nil {
            let wheel = HUDLayer.gradient("swatch.wheel")
            wheel.type = .conic
            wheel.startPoint = CGPoint(x: 0.5, y: 0.5); wheel.endPoint = CGPoint(x: 0.5, y: 0)
            wheel.colors = [0xFF5A5A, 0xFFD84A, 0x7BE37B, 0x4AC6FF, 0xA77BFF, 0xFF5A5A].map { HUDColor.rgb($0) }
            fill = wheel
        } else { fill = HUDLayer.plain("swatch.fill") }
        super.init(id: id, label: label, env: env)
        let bounds = CGRect(x: 0, y: 0, width: size, height: size)
        HUDMotion.perform {
            layer.bounds = bounds
            fill.frame = bounds; fill.backgroundColor = color
            fill.borderWidth = 1; fill.borderColor = HUDColor.nearBlack
            if color == nil { fill.cornerRadius = size / 2; fill.masksToBounds = true }
            ring.frame = bounds.insetBy(dx: -5, dy: -5); ring.borderWidth = 3; ring.borderColor = HUDColor.yellow
            ring.isHidden = true
            let inner = HUDLayer.plain(); inner.frame = ring.bounds.insetBy(dx: 3, dy: 3)
            inner.borderWidth = 2; inner.borderColor = HUDColor.nearBlack
            ring.addSublayer(inner)
            layer.addSublayer(fill); layer.addSublayer(ring)
        }
        setTarget(bounds)
    }

    private func render() { HUDMotion.perform { ring.isHidden = !isSelected } }
    override func hoverChanged() {
        HUDMotion.animate(layer, "transform.scale", to: isHovered && isEnabled ? 1.08 : 1, duration: HUDDuration.hover, env: env)
    }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .tab(selected: isSelected) }
}

/// A drag handle in the grip motif (two columns of three 2 px dots; TODO cards,
/// the Modules list): a drag past 3 px reports its start, each move and the
/// release in the grip's coordinates; a press without a drag activates it.
/// Without a label it stays out of VoiceOver, whose users get the owner's move
/// actions instead.
final class HUDDragGrip: HUDControl {
    enum Phase { case began, moved, ended }
    var onDrag: ((Phase, CGPoint) -> Void)?
    private let dots = HUDLayer.shape("grip.dots")
    private var start: CGPoint?
    private(set) var isDragging = false

    init(id: String, label: String = "", size: CGSize, dotsAt origin: CGPoint, env: HUDDesignEnvironment) {
        super.init(id: id, label: label, env: env)
        let bounds = CGRect(origin: .zero, size: size)
        HUDMotion.perform {
            layer.bounds = bounds
            dots.frame = CGRect(origin: origin, size: CGSize(width: 6, height: 10)); dots.path = Self.dots(columns: 2, rows: 3)
            dots.fillColor = HUDColor.bracket
            layer.addSublayer(dots)
        }
        setTarget(bounds)
    }

    /// The grip motif: 2 px squares 4 px apart (the desktop notes' titles use it too).
    static func dots(columns: Int, rows: Int) -> CGPath {
        let path = CGMutablePath()
        for column in 0..<columns { for row in 0..<rows { path.addRect(CGRect(x: CGFloat(column) * 4, y: CGFloat(row) * 4, width: 2, height: 2)) } }
        return path
    }

    override func hoverChanged() {
        HUDMotion.animate(dots, "fillColor", to: isHovered || isDragging ? HUDColor.nearBlack : HUDColor.bracket,
                          duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
    }

    override func pointerDown(at point: CGPoint) -> Bool {
        guard isEnabled else { return false }
        start = point; isDragging = false
        return true
    }

    override func pointerDragged(to point: CGPoint) {
        guard let start else { return }
        if !isDragging {
            guard hypot(point.x - start.x, point.y - start.y) > 3 else { return }
            isDragging = true
            onDrag?(.began, start)
        }
        onDrag?(.moved, point)
    }

    override func pointerUp(at point: CGPoint) {
        defer { start = nil; isDragging = false; hoverChanged() }
        if isDragging { onDrag?(.ended, point) } else if contains(point) { activate() }
    }

    override func clearInteractionState() {
        if isDragging, let start { onDrag?(.ended, start) }
        start = nil; isDragging = false
        super.clearInteractionState()
    }

    override var accessibilityRole: HUDAccessibilityItem.Role? { label.isEmpty ? nil : .button }
}
