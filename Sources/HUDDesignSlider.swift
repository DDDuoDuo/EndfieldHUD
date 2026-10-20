import AppKit
import QuartzCore

/// Slider (spec §2.7): dark 5 px track, yellow fill (from zero for centre-zero
/// ranges), 10 % tick ladder, dark thumb with a yellow core that grows on hover
/// or drag, and the `[ value ]` readout. While dragging, the white value bubble
/// floats in the canvas's top layer so nothing clips it.
final class HUDSlider: HUDControl {
    struct Range: Equatable {
        let minimum: Double, maximum: Double, step: Double
        /// Centre-zero sliders fill from this value.
        var zero: Double? = nil
        func clamp(_ value: Double) -> Double {
            let snapped = step > 0 ? ((value - minimum) / step).rounded() * step + minimum : value
            return min(maximum, max(minimum, snapped))
        }
        func fraction(_ value: Double) -> CGFloat {
            maximum > minimum ? CGFloat((min(maximum, max(minimum, value)) - minimum) / (maximum - minimum)) : 0
        }
    }

    static let readoutWidth: CGFloat = 58, height: CGFloat = 30
    let range: Range
    let format: HUDValueFormat
    let dark: Bool
    private(set) var value: Double
    private(set) var isDragging = false
    /// (value, final): final is true on release and for keyboard/accessibility steps.
    var onChange: ((Double, Bool) -> Void)?
    /// The canvas's top layer (z 40) that hosts the bubble.
    weak var bubbleHost: CALayer?
    private let track = HUDLayer.plain("slider.track")
    private let fill = HUDLayer.gradient("slider.fill")
    private let zeroMark = HUDLayer.plain("slider.zero")
    private let ticks = HUDLayer.shape("slider.ticks")
    private let thumb = HUDLayer.plain("slider.thumb")
    private let core = HUDLayer.plain("slider.core")
    private let readout = HUDTextLayer(style: HUDType.readout, alignment: .right)
    private var bubble: HUDSliderBubble?
    private var trackWidth: CGFloat = 0

    init(id: String, label: String, value: Double, range: Range, format: HUDValueFormat, width: CGFloat,
         dark: Bool = false, showsReadout: Bool = true, env: HUDDesignEnvironment) {
        self.range = range; self.format = format; self.dark = dark
        self.value = range.clamp(value)
        super.init(id: id, label: label, env: env)
        trackWidth = max(40, width - (showsReadout ? Self.readoutWidth + 8 : 0))
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: Self.height)
            track.frame = CGRect(x: 0, y: 11, width: trackWidth, height: 5)
            track.backgroundColor = dark ? HUDColor.rgb(0x3A3A3A) : HUDColor.sliderTrack
            if !dark { track.borderWidth = 1; track.borderColor = HUDColor.nearBlack }
            fill.startPoint = CGPoint(x: 0, y: 0.5); fill.endPoint = CGPoint(x: 1, y: 0.5)
            fill.colors = [HUDColor.yellowLow, HUDColor.yellow]
            zeroMark.backgroundColor = HUDColor.nearBlack; zeroMark.isHidden = range.zero == nil
            ticks.frame = CGRect(x: 0, y: 21, width: trackWidth, height: 4)
            ticks.path = HUDShape.ticks(count: 11, width: trackWidth, height: 4); ticks.fillColor = HUDColor.sliderTick
            thumb.bounds = CGRect(x: 0, y: 0, width: 9, height: 17)
            thumb.backgroundColor = dark ? HUDColor.rgb(0xEDEDED) : HUDColor.counterBackground
            core.frame = CGRect(x: 3.5, y: 3, width: 2, height: 11)
            core.backgroundColor = dark ? HUDColor.nearBlack : HUDColor.yellow
            thumb.addSublayer(core)
            readout.isHidden = !showsReadout
            readout.lineFrame = CGRect(x: width - Self.readoutWidth, y: 9, width: Self.readoutWidth, height: 12)
            [track, fill, zeroMark, ticks, thumb, readout].forEach(layer.addSublayer)
        }
        setTarget(CGRect(x: 0, y: 0, width: trackWidth, height: Self.height))
        render()
        env.applyScale(to: readout)
    }

    override func contains(_ point: CGPoint) -> Bool { CGRect(x: -4.5, y: 0, width: trackWidth + 9, height: Self.height).contains(point) }

    func setValue(_ newValue: Double) {
        let next = range.clamp(newValue)
        guard next != value else { return }
        value = next; render()
    }

    /// ± one step (arrow keys, accessibility increment).
    func nudge(_ direction: Double) {
        let next = range.clamp(value + direction * range.step)
        guard next != value else { return }
        value = next; render(); onChange?(value, true)
    }

    private var thumbX: CGFloat { range.fraction(value) * trackWidth }

    private func render() {
        HUDMotion.perform {
            let x = thumbX
            if let zero = range.zero {
                let z = range.fraction(zero) * trackWidth
                fill.frame = CGRect(x: min(x, z), y: 11, width: abs(x - z), height: 5)
                zeroMark.frame = CGRect(x: z - 0.5, y: 8, width: 1, height: 11)
            } else {
                fill.frame = CGRect(x: 0, y: 11, width: x, height: 5)
            }
            thumb.position = CGPoint(x: x, y: 13.5)
            readout.runs = [HUDText.Run("[ ", HUDType.readoutBracket.colored(dark ? HUDColor.bracket : HUDType.readoutBracket.color)),
                            HUDText.Run(format.string(value), dark ? HUDType.readout.colored(HUDColor.textOnDark) : HUDType.readout),
                            HUDText.Run(" ]", HUDType.readoutBracket)]
        }
        updateBubble()
    }

    private func value(at point: CGPoint) -> Double {
        range.clamp(range.minimum + Double(min(1, max(0, point.x / max(1, trackWidth)))) * (range.maximum - range.minimum))
    }

    override func pointerDown(at point: CGPoint) -> Bool {
        guard isEnabled else { return false }
        isDragging = true
        renderThumbGrowth()
        let next = value(at: point)
        if next != value { value = next; render(); onChange?(value, false) }
        showBubble()
        return true
    }
    override func pointerDragged(to point: CGPoint) {
        guard isDragging else { return }
        let next = value(at: point)
        guard next != value else { return }
        value = next; render(); onChange?(value, false)
    }
    override func pointerUp(at point: CGPoint) {
        guard isDragging else { return }
        isDragging = false
        renderThumbGrowth(); hideBubble()
        onChange?(value, true)
    }
    /// An interrupted drag (module change, confirm bar) ends without a commit.
    func cancelDrag() {
        guard isDragging else { return }
        isDragging = false; renderThumbGrowth(); hideBubble()
    }

    override func hoverChanged() { renderThumbGrowth() }
    override func enabledChanged() { HUDMotion.perform { layer.opacity = isEnabled ? 1 : 0.35 } }

    private func renderThumbGrowth() {
        let grown = (isHovered || isDragging) && isEnabled
        HUDMotion.animate(thumb, "transform.scale.y", to: grown ? 21.0 / 17.0 : 1, duration: HUDDuration.thumb,
                          timing: HUDEasing.ease, env: env)
    }

    private func showBubble() {
        guard let host = bubbleHost else { return }
        let bubble = self.bubble ?? HUDSliderBubble(env: env)
        self.bubble = bubble
        if bubble.layer.superlayer !== host { host.addSublayer(bubble.layer) }
        updateBubble()
        bubble.setVisible(true)
    }
    private func hideBubble() { bubble?.setVisible(false) }
    private func updateBubble() {
        guard let bubble, let host = bubbleHost, isDragging else { return }
        let anchor = host.convert(CGPoint(x: thumbX, y: 2), from: layer)
        bubble.show(format.string(value), anchor: anchor)
    }

    override var accessibilityRole: HUDAccessibilityItem.Role? {
        .slider(value: value, minimum: range.minimum, maximum: range.maximum, step: range.step, description: format.string(value))
    }
    var readoutTextForTesting: String { readout.text }
    var fillFrameForTesting: CGRect { fill.frame }
}

/// The white value bubble (spec §2.7 `.fbub`): bottom-centred on its anchor,
/// scales .9 → 1 and fades in; a 4 px arrow points at the thumb.
final class HUDSliderBubble {
    let layer = HUDLayer.plain("slider.bubble")
    private let shadow = HUDLayer.plain()
    private let body = HUDLayer.plain()
    private let arrow = HUDLayer.shape()
    private let text = HUDTextLayer(style: HUDType.sliderBubble, alignment: .center)
    private let env: HUDDesignEnvironment
    private(set) var isVisible = false

    init(env: HUDDesignEnvironment) {
        self.env = env
        HUDMotion.perform {
            layer.zPosition = HUDLayerOrder.bubble
            layer.anchorPoint = CGPoint(x: 0.5, y: 1)
            layer.opacity = 0
            layer.setValue(0.9, forKeyPath: "transform.scale")
            shadow.backgroundColor = HUDColor.nearBlack; shadow.cornerRadius = 10
            body.backgroundColor = HUDColor.rgb(0xFAFAFA); body.cornerRadius = 10
            body.shadowColor = HUDColor.black(1); body.shadowOpacity = 0.45; body.shadowRadius = 8
            body.shadowOffset = CGSize(width: 0, height: 6)
            arrow.path = HUDShape.triangleDown(width: 8, height: 4); arrow.fillColor = HUDColor.rgb(0xFAFAFA)
            [shadow, body, arrow, text].forEach(layer.addSublayer)
        }
    }

    func show(_ value: String, anchor: CGPoint) {
        HUDMotion.perform {
            text.text = value
            let width = ceil(text.preferredWidth) + 16, height: CGFloat = 21
            // translate(−50%, −100% − 6px): bottom-centre 6 px above the anchor.
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: height + 4)
            layer.position = CGPoint(x: anchor.x, y: anchor.y - 6 + 4)
            shadow.frame = CGRect(x: 0, y: 2, width: width, height: height)
            body.frame = CGRect(x: 0, y: 0, width: width, height: height)
            body.shadowPath = CGPath(roundedRect: body.bounds, cornerWidth: 10, cornerHeight: 10, transform: nil)
            arrow.frame = CGRect(x: width / 2 - 4, y: height, width: 8, height: 4)
            text.lineFrame = CGRect(x: 0, y: 5, width: width, height: 11)
        }
        env.applyScale(to: text)
    }

    func setVisible(_ visible: Bool) {
        guard visible != isVisible else { return }
        isVisible = visible
        HUDMotion.animate(layer, "opacity", to: Float(visible ? 1 : 0), duration: HUDDuration.bubbleFade, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(layer, "transform.scale", to: visible ? 1 : 0.9, duration: HUDDuration.bubbleScale, timing: HUDEasing.snap, env: env)
    }
}
