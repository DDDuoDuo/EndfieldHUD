import AppKit
import QuartzCore

/// Chips (spec §2.10): yellow, dark, light, outline, cyan or a custom tone.
final class HUDChip {
    enum Variant: Equatable {
        case yellow, dark, light, outline, cyan
        case custom(fill: CGColor, text: CGColor)
        var colors: (fill: CGColor, text: CGColor, ring: CGColor?) {
            switch self {
            case .yellow: return (HUDColor.yellow, HUDColor.nearBlack, nil)
            case .dark: return (HUDColor.counterBackground, HUDColor.onDarkTag, nil)
            case .light: return (HUDColor.plate, HUDColor.nearBlack, nil)
            case .outline: return (HUDColor.clear, HUDColor.tone(0xCFCFCF), HUDColor.tone(0x5A5A5A))
            case .cyan: return (HUDColor.cyan, HUDColor.rgb(0x06263A), nil)
            case .custom(let fill, let text): return (fill, text, nil)
            }
        }
    }
    let layer = HUDLayer.plain("chip")
    private let text = HUDTextLayer(style: HUDType.chip)
    private var glyph: HUDIconLayer?

    init(_ title: String, variant: Variant, icon: HUDIcon? = nil, env: HUDDesignEnvironment) {
        let colors = variant.colors
        HUDMotion.perform {
            layer.backgroundColor = colors.fill
            if let ring = colors.ring { layer.borderWidth = 1; layer.borderColor = ring }
            text.textStyle = HUDType.chip.colored(colors.text)
            layer.addSublayer(text)
            if let icon { let g = HUDIconLayer(icon, size: 10, color: colors.text); glyph = g; layer.addSublayer(g) }
        }
        setTitle(title)
        env.applyScale(to: layer)
    }

    func setTitle(_ title: String) {
        HUDMotion.perform {
            text.text = title
            var x: CGFloat = 7
            if let glyph { glyph.frame = CGRect(x: x, y: 4, width: 10, height: 10); x += 15 }
            let width = text.preferredWidth
            text.lineFrame = CGRect(x: x, y: 4.25, width: width + 1, height: 9.5)
            layer.bounds = CGRect(x: 0, y: 0, width: ceil(x + width + 7), height: 18)
        }
    }
}

/// The skewed yellow NEW badge with a black ring (spec §2.10).
enum HUDBadge {
    static func new(env: HUDDesignEnvironment) -> CALayer {
        let badge = HUDLayer.plain("badge.new")
        let text = HUDTextLayer("NEW", style: HUDType.newBadge, alignment: .center)
        let width = ceil(text.preferredWidth) + 10
        HUDMotion.perform {
            badge.bounds = CGRect(x: 0, y: 0, width: width, height: 12)
            badge.backgroundColor = HUDColor.yellow
            badge.borderWidth = 1.5; badge.borderColor = HUDColor.nearBlack
            text.lineFrame = CGRect(x: 0, y: 2, width: width, height: 8)
            badge.addSublayer(text)
            var skew = CATransform3DIdentity; skew.m21 = tan(-8 * .pi / 180)
            badge.transform = skew; badge.zPosition = 2
        }
        env.applyScale(to: badge)
        return badge
    }

    /// The bordered yellow stamp, rotated −4°; `show` pops it in (spec §2.10).
    static func stamp(_ title: String, env: HUDDesignEnvironment) -> CALayer {
        let stamp = HUDLayer.plain("badge.stamp")
        let text = HUDTextLayer(title, style: HUDType.stamp, alignment: .center)
        let width = ceil(text.preferredWidth) + 16 + 4
        HUDMotion.perform {
            stamp.bounds = CGRect(x: 0, y: 0, width: width, height: 20)
            stamp.borderWidth = 2; stamp.borderColor = HUDColor.accentInk
            text.lineFrame = CGRect(x: 0, y: 5, width: width, height: 10)
            stamp.addSublayer(text)
            stamp.setValue(-4 * CGFloat.pi / 180, forKeyPath: "transform.rotation.z")
        }
        env.applyScale(to: stamp)
        return stamp
    }

    /// The rotated orange notification square.
    static func diamond(size: CGFloat = 7) -> CALayer {
        let dot = HUDLayer.plain("badge.diamond")
        HUDMotion.perform {
            dot.bounds = CGRect(x: 0, y: 0, width: size, height: size)
            dot.backgroundColor = HUDColor.orange
            dot.setValue(CGFloat.pi / 4, forKeyPath: "transform.rotation.z")
        }
        return dot
    }
}

/// Keycaps and the keycap group with its recording state (spec §2.10, §3.4).
final class HUDKeycapGroup: HUDControl {
    private let keyLayer = HUDLayer.plain("keys.caps")
    private let scan = HUDLayer.plain("keys.scan")
    private let prompt = HUDTextLayer(style: HUDTextStyle(size: 10.5, weight: .bold, color: HUDColor.accentInk, tracking: 0.1, uppercase: true))
    private let cursor = HUDTextLayer("_", style: HUDTextStyle(size: 10.5, weight: .bold, color: HUDColor.accentInk))
    private(set) var keys: [String] = []
    private(set) var isRecording = false
    let interactive: Bool
    let minimumWidth: CGFloat

    init(id: String, label: String, keys: [String], interactive: Bool, minimumWidth: CGFloat = 178, env: HUDDesignEnvironment) {
        self.interactive = interactive; self.minimumWidth = minimumWidth
        super.init(id: id, label: label, env: env)
        HUDMotion.perform {
            layer.backgroundColor = HUDColor.tabBar
            layer.borderWidth = 1; layer.borderColor = HUDColor.ringField
            scan.isHidden = true; prompt.isHidden = true; cursor.isHidden = true
            scan.masksToBounds = true
            [scan, keyLayer, prompt, cursor].forEach(layer.addSublayer)
        }
        setKeys(keys)
    }

    func setKeys(_ keys: [String]) {
        self.keys = keys
        HUDMotion.perform {
            keyLayer.sublayers?.forEach { $0.removeFromSuperlayer() }
            var x: CGFloat = 10
            for (index, key) in keys.enumerated() {
                if index > 0 {
                    let plus = HUDTextLayer("+", style: HUDTextStyle(size: 11, weight: .bold, color: HUDColor.tone(0x888888)))
                    plus.lineFrame = CGRect(x: x, y: 11.5, width: 8, height: 11); keyLayer.addSublayer(plus); x += 8 + 6
                }
                let cap = Self.keycap(key, scale: env.renderScale)
                cap.frame.origin = CGPoint(x: x, y: 4); keyLayer.addSublayer(cap)
                x += cap.bounds.width + 6
            }
            let width = max(interactive ? minimumWidth : 0, x + 4)
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: 34)
            keyLayer.frame = layer.bounds; scan.frame = layer.bounds
            prompt.lineFrame = CGRect(x: 10, y: 11.75, width: width - 30, height: 10.5)
        }
        setTarget(layer.bounds)
        env.applyScale(to: layer)
    }

    /// A single light keycap (min 34 × 26).
    static func keycap(_ key: String, scale: CGFloat) -> CALayer {
        let cap = HUDLayer.plain("keycap")
        let text = HUDTextLayer(key, style: HUDType.keycap, alignment: .center)
        let width = max(34, ceil(text.preferredWidth) + 16)
        let plate = HUDLayer.plain()
        plate.frame = CGRect(x: 0, y: 0, width: width, height: 26)
        plate.showArtwork(HUDArtwork.plate([HUDColor.rgb(0xF2F3EE), HUDColor.rgb(0xCFD0CB)], size: plate.bounds.size, cut: 0, scale: scale), scale: scale)
        plate.borderWidth = 1; plate.borderColor = HUDColor.rgb(0xB4B5B0)
        let shadow = HUDLayer.plain()
        shadow.frame = CGRect(x: 0, y: 2, width: width, height: 26); shadow.backgroundColor = HUDColor.bracket
        text.lineFrame = CGRect(x: 0, y: 7.5, width: width, height: 11)
        cap.bounds = CGRect(x: 0, y: 0, width: width, height: 28)
        [shadow, plate, text].forEach(cap.addSublayer)
        return cap
    }

    /// Recording: yellow ring, scanning hatch, PRESS A SHORTCUT and a blinking
    /// cursor. The scan and blink loop only while recording (a live state).
    func setRecording(_ recording: Bool, prompt title: String) {
        guard recording != isRecording || prompt.text != title else { return }
        isRecording = recording
        HUDMotion.perform {
            layer.borderColor = recording ? HUDColor.accentInk : HUDColor.ringField
            keyLayer.isHidden = recording
            scan.isHidden = !recording; prompt.isHidden = !recording; cursor.isHidden = !recording
            prompt.text = title
            cursor.lineFrame = CGRect(x: 10 + prompt.preferredWidth + 2, y: 11.75, width: 8, height: 10.5)
            scan.sublayers?.forEach { $0.removeFromSuperlayer() }
        }
        scan.removeAllAnimations(); cursor.removeAllAnimations()
        guard recording else { return }
        let stripes = HUDLayer.plain()
        HUDMotion.perform {
            stripes.frame = CGRect(x: 0, y: 0, width: layer.bounds.width * 2, height: 34)
            stripes.backgroundColor = HUDColor.tabBar
            let path = CGMutablePath(), period: CGFloat = 12 * CGFloat(2).squareRoot()
            var x: CGFloat = -34
            while x < stripes.bounds.width + 34 {
                path.addLines(between: [CGPoint(x: x, y: 34), CGPoint(x: x + period / 2, y: 34),
                                        CGPoint(x: x + period / 2 + 34, y: 0), CGPoint(x: x + 34, y: 0)])
                path.closeSubpath(); x += period
            }
            let band = HUDLayer.shape(); band.frame = stripes.bounds; band.path = path; band.fillColor = HUDColor.tone(0x262414)
            stripes.addSublayer(band); scan.addSublayer(stripes)
        }
        guard !env.reducesMotion else { return }
        let move = CABasicAnimation(keyPath: "transform.translation.x")
        move.fromValue = 0; move.toValue = -34; move.duration = 1; move.repeatCount = .infinity
        move.timingFunction = HUDEasing.linear
        stripes.add(move, forKey: "hud.scan")
        let blink = CAKeyframeAnimation(keyPath: "opacity")
        blink.values = [1, 0.2, 1]; blink.keyTimes = [0, 0.5, 1]; blink.duration = 1.2; blink.repeatCount = .infinity
        blink.timingFunction = HUDEasing.easeInOut
        cursor.add(blink, forKey: "hud.blink")
    }

    override func pointerDown(at point: CGPoint) -> Bool { interactive && isEnabled }
    override var accessibilityRole: HUDAccessibilityItem.Role? { interactive ? .button : nil }
}

/// Text-field frame states (spec §2.11): focus = yellow ring + 3 px bar;
/// invalid = red. The editor itself is a projected AppKit field.
final class HUDFieldFrame {
    enum State { case normal, focused, invalid }
    let layer = HUDLayer.plain("field")
    private let bar = HUDLayer.plain("field.bar")
    let light: Bool
    /// A bare text area (the Notes note sits in its own panel): no fill, ring or bar.
    let plain: Bool
    /// The focus ring and bar (yellow; Personal ID uses its card accent).
    var focusColor = HUDColor.accentInk

    init(size: CGSize, light: Bool = false, plain: Bool = false) {
        self.light = light; self.plain = plain
        HUDMotion.perform {
            layer.bounds = CGRect(origin: .zero, size: size)
            layer.backgroundColor = plain ? nil : (light ? HUDColor.rgb(0xF2F3EE) : HUDColor.field)
            layer.borderWidth = plain ? 0 : 1
            bar.frame = CGRect(x: 0, y: 0, width: 3, height: size.height)
            layer.addSublayer(bar)
        }
        setState(.normal)
    }

    func setState(_ state: State) {
        HUDMotion.perform {
            if plain { bar.isHidden = true; return }
            switch state {
            case .normal: layer.borderColor = light ? HUDColor.rgb(0xB4B5B0) : HUDColor.ringField; bar.isHidden = true
            case .focused: layer.borderColor = focusColor; bar.backgroundColor = focusColor; bar.isHidden = false
            case .invalid: layer.borderColor = HUDColor.red; bar.backgroundColor = HUDColor.red; bar.isHidden = false
            }
        }
    }
}

/// Empty state (spec §2.11): light hatch, ring, `[ TITLE ]` with yellow
/// brackets, optional CJK line below.
enum HUDEmptyState {
    /// The bilingual hint: the English title always sits in the brackets;
    /// other UIs add their own language as the small grey line below.
    /// English UIs show the English title alone.
    static func bilingual(_ english: L10n.Text, _ chinese: L10n.Text, size: CGSize = CGSize(width: HUDMetric.box.width, height: 250),
                          reserving: CGFloat = 0, env: HUDDesignEnvironment) -> CALayer {
        make(title: english.value, subtitle: bilingualHasSubtitle ? L10n.text(english, chinese) : nil,
             size: size, reserving: reserving, env: env)
    }

    /// Whether `bilingual` shows a subtitle line in the current language
    /// (for `textBottom(size:subtitle:reserving:)`).
    static var bilingualHasSubtitle: Bool { L10n.resolvedLanguage != .english }

    /// - reserving: height kept free under the text for buttons (with their
    ///   margin); the whole block stays centred. `textBottom` reports where it starts.
    static func make(title: String, subtitle: String? = nil, subtitleLanguage: String? = nil,
                     size: CGSize = CGSize(width: HUDMetric.box.width, height: 250), reserving: CGFloat = 0,
                     env: HUDDesignEnvironment) -> CALayer {
        let root = HUDLayer.configure(HUDHatchLayer(.light, scale: env.renderScale), "empty")
        HUDMotion.perform {
            root.bounds = CGRect(origin: .zero, size: size)
            root.setNeedsDisplay()
            root.borderWidth = 1; root.borderColor = HUDColor.ringField
            let bracket = HUDType.emptyTitle.colored(HUDColor.accentInk)
            let text = HUDTextLayer(style: HUDType.emptyTitle, alignment: .center)
            text.runs = [HUDText.Run("[ ", bracket), HUDText.Run(title, HUDType.emptyTitle), HUDText.Run(" ]", bracket)]
            let blockHeight: CGFloat = textHeight(subtitle: subtitle != nil)
            let top = (size.height - blockHeight - reserving) / 2
            text.lineFrame = CGRect(x: 0, y: top, width: size.width, height: 20)
            root.addSublayer(text)
            if let subtitle {
                let line = HUDTextLayer(subtitle, style: HUDType.labelCJK, alignment: .center)
                line.language = subtitleLanguage
                line.lineFrame = CGRect(x: 0, y: top + 30, width: size.width, height: 11.5)
                root.addSublayer(line)
            }
        }
        env.applyScale(to: root)
        return root
    }

    /// Height of the title (and subtitle) block.
    static func textHeight(subtitle: Bool) -> CGFloat { subtitle ? 20 + 10 + 11.5 : 20 }
    /// Where the reserved space begins in a panel made with `reserving`.
    static func textBottom(size: CGSize, subtitle: Bool, reserving: CGFloat) -> CGFloat {
        (size.height - textHeight(subtitle: subtitle) - reserving) / 2 + textHeight(subtitle: subtitle)
    }

    /// The 11 × 11 crosshair (spec §2.11).
    static func crosshair() -> CALayer {
        let mark = HUDLayer.shape("crosshair")
        mark.bounds = CGRect(x: 0, y: 0, width: 11, height: 11)
        mark.path = HUDShape.crosshair(); mark.fillColor = HUDColor.foreground(0.45)
        return mark
    }
}

/// The idle-hiding scroll bar (owner item 4) shared by scroll areas, long
/// menus and editors: hidden while idle, it fades in while the content
/// scrolls or is dragged and while the pointer is over the bar's strip, and
/// fades out about a second after that ends. One delayed fade per scroll
/// burst, nothing scheduled while idle; Reduce Motion shows and hides it at once.
final class HUDScrollBarVisibility {
    static let linger: CFTimeInterval = 1
    let bar: CALayer
    private(set) var isShown = false
    private var hovered = false, dragging = false
    private var lastActivity: CFTimeInterval = 0
    private var hideWork: DispatchWorkItem?
    private static let motion = HUDDesignEnvironment()

    init(bar: CALayer) {
        self.bar = bar
        HUDMotion.perform { bar.opacity = 0 }
    }

    /// The content scrolled (wheel, drag, keyboard): show, hide after the burst.
    func noteScroll() {
        lastActivity = CACurrentMediaTime()
        setShown(true)
        scheduleHide()
    }

    func setHovered(_ value: Bool) {
        guard value != hovered else { return }
        hovered = value
        if value { setShown(true) } else { lastActivity = CACurrentMediaTime(); scheduleHide() }
    }

    func setDragging(_ value: Bool) {
        guard value != dragging else { return }
        dragging = value
        if value { setShown(true) } else { noteScroll() }
    }

    /// Hidden at once (the content left the screen).
    func reset() {
        hideWork?.cancel(); hideWork = nil
        hovered = false; dragging = false
        guard isShown else { return }
        isShown = false
        bar.removeAnimation(forKey: "hud.opacity")
        HUDMotion.perform { bar.opacity = 0 }
    }

    private func scheduleHide() {
        guard hideWork == nil else { return }
        let delay = max(0.05, lastActivity + Self.linger - CACurrentMediaTime())
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.hideWork = nil
            guard !self.hovered, !self.dragging else { return }
            // Scrolled again meanwhile: wait for the rest of the burst.
            if self.lastActivity + Self.linger - CACurrentMediaTime() > 0.05 { self.scheduleHide() } else { self.setShown(false) }
        }
        hideWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }

    private func setShown(_ value: Bool) {
        guard value != isShown else { return }
        isShown = value
        HUDMotion.animate(bar, "opacity", to: Float(value ? 1 : 0), duration: value ? 0.15 : 0.3,
                          timing: value ? HUDEasing.easeOut : HUDEasing.easeIn, env: Self.motion)
    }
}

/// A vertical scroll area (spec §2.11): clipped content, 4 px yellow thumb on
/// a #222 track that shows only while scrolling or hovered (HUDScrollBarVisibility).
/// Offsets survive in-place re-renders.
final class HUDScrollArea {
    let layer = HUDLayer.plain("scroll")
    let content = HUDLayer.plain("scroll.content")
    private let track = HUDLayer.plain("scroll.track")
    private let thumb = HUDLayer.plain("scroll.thumb")
    private(set) var offset: CGFloat = 0
    private(set) var contentHeight: CGFloat = 0
    let scrollBar: HUDScrollBarVisibility
    /// The bar's strip as a hover-only container target: it never takes a
    /// press nor the hover of the controls under it.
    private var strip: HUDControlHighlightLayer?
    static let stripWidth: CGFloat = 12

    init(frame: CGRect) {
        scrollBar = HUDScrollBarVisibility(bar: track)
        HUDMotion.perform {
            layer.frame = frame; layer.masksToBounds = true
            track.backgroundColor = HUDColor.tone(0x222222); thumb.backgroundColor = HUDColor.accentInk
            layer.addSublayer(content); layer.addSublayer(track); track.addSublayer(thumb)
        }
        strip = HUDControlHighlightLayer.addTarget(to: layer, rect: stripRect, container: true) { [weak self] hovered, _ in
            self?.scrollBar.setHovered(hovered)
        }
    }

    private var stripRect: CGRect {
        CGRect(x: layer.bounds.width - Self.stripWidth, y: 0, width: Self.stripWidth, height: layer.bounds.height)
    }

    /// Shows the bar for a scroll the area did not make itself (a module's drag).
    func noteScroll() { if maximumOffset > 0 { scrollBar.noteScroll() } }

    var maximumOffset: CGFloat { max(0, contentHeight - layer.bounds.height) }
    /// Width left for content (the bar takes 4 px plus 6 px padding when shown).
    var contentWidth: CGFloat { layer.bounds.width - (maximumOffset > 0 ? 10 : 0) }

    func setContentHeight(_ height: CGFloat) {
        contentHeight = height
        setOffset(offset)
    }

    func setOffset(_ value: CGFloat) {
        offset = min(maximumOffset, max(0, value))
        HUDMotion.perform {
            let bounds = layer.bounds
            content.frame = CGRect(x: 0, y: -offset, width: bounds.width, height: max(contentHeight, bounds.height))
            track.isHidden = maximumOffset <= 0
            track.frame = CGRect(x: bounds.width - 4, y: 0, width: 4, height: bounds.height)
            strip?.isHidden = maximumOffset <= 0
            if strip?.frame != stripRect { strip?.setTarget(rect: stripRect) }
            let thumbHeight = max(18, bounds.height * bounds.height / max(1, contentHeight))
            let fraction = maximumOffset > 0 ? offset / maximumOffset : 0
            thumb.frame = CGRect(x: 0, y: fraction * (bounds.height - thumbHeight), width: 4, height: thumbHeight)
        }
    }

    @discardableResult func scroll(by delta: CGFloat) -> Bool {
        let before = offset
        setOffset(offset + delta)
        if offset != before { scrollBar.noteScroll() }
        return offset != before
    }

    /// Smooth scrolling: content and thumb glide (.2 s E) from where they are
    /// on screen to `value`; unanimated (a drag) they follow at once.
    func setOffset(_ value: CGFloat, animated: Bool, env: HUDDesignEnvironment) {
        let starts = [content, thumb].map { ($0.presentation() ?? $0).position.y }
        let before = offset
        setOffset(value)
        if offset != before { scrollBar.noteScroll() }
        for (item, start) in zip([content, thumb], starts) {
            item.removeAnimation(forKey: "hud.scroll")
            guard animated, !env.reducesMotion, abs(start - item.position.y) > 0.01 else { continue }
            let glide = CABasicAnimation(keyPath: "position.y")
            glide.fromValue = start; glide.toValue = item.position.y
            glide.duration = 0.2; glide.timingFunction = HUDEasing.standard
            item.add(glide, forKey: "hud.scroll")
        }
    }

    /// Keeps a rect (content coordinates) fully visible.
    func reveal(_ rect: CGRect) {
        if rect.minY < offset { setOffset(rect.minY) }
        else if rect.maxY > offset + layer.bounds.height { setOffset(rect.maxY - layer.bounds.height) }
    }
}

/// Big numeral with a small top-aligned unit (spec §2.12).
final class HUDBigNumeral {
    let layer = HUDLayer.plain("numeral")
    private let value: HUDTextLayer
    private let unit: HUDTextLayer
    let size: CGFloat

    init(size: CGFloat = 64, env: HUDDesignEnvironment) {
        self.size = size
        value = HUDTextLayer(style: HUDType.numeral.sized(size))
        unit = HUDTextLayer(style: HUDType.numeralUnit.sized(size * 0.34))
        layer.addSublayer(value); layer.addSublayer(unit)
        env.applyScale(to: layer)
    }

    /// `numberTone` colours the digits too (Battery's level tone); nil keeps #F4F4F4.
    func set(_ number: String, unit text: String, tone: CGColor = HUDColor.yellow, numberTone: CGColor? = nil) {
        HUDMotion.perform {
            value.textStyle = HUDType.numeral.sized(size).colored(numberTone.map(HUDColor.legible) ?? HUDType.numeral.color)
            value.text = number
            unit.textStyle = HUDType.numeralUnit.sized(size * 0.34).colored(HUDColor.legible(tone))
            unit.text = text
            let width = value.preferredWidth
            value.lineFrame = CGRect(x: 0, y: 0, width: width + 1, height: size * 0.9)
            unit.lineFrame = CGRect(x: width + 4, y: size * 0.08, width: unit.preferredWidth + 1, height: size * 0.34)
            layer.bounds = CGRect(x: 0, y: 0, width: width + 4 + unit.preferredWidth, height: size * 0.9)
        }
    }
}

/// Dark stat plate with an L-corner, label and value (spec §2.12).
final class HUDStatPlate {
    let layer = HUDLayer.plain("stat")
    private let label = HUDTextLayer(style: HUDType.label)
    private let value = HUDTextLayer(style: HUDType.statValue)
    private let valueStyle: HUDTextStyle

    /// - valueStyle: the value's type (Minigame's SCORE is 300 40 px); the plate grows with its size.
    init(width: CGFloat, valueStyle: HUDTextStyle = HUDType.statValue, env: HUDDesignEnvironment) {
        self.valueStyle = valueStyle
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: 9 + 10 + 6 + valueStyle.size + 10)
            layer.showArtwork(HUDArtwork.plate([HUDColor.tone(0x232323), HUDColor.tone(0x1B1B1B)], size: layer.bounds.size, cut: 0,
                                               scale: env.renderScale), scale: env.renderScale)
            layer.borderWidth = 1; layer.borderColor = HUDColor.ringTab
            let corner = HUDLayer.shape()
            corner.frame = CGRect(x: 0, y: 0, width: 8, height: 8)
            corner.path = HUDShape.cornerMark(corner.bounds, length: 8, width: 2, corner: .topLeft)
            corner.fillColor = HUDColor.tone(0x6A6A6A)
            label.lineFrame = CGRect(x: 12, y: 9, width: width - 24, height: 10)
            value.textStyle = valueStyle
            value.lineFrame = CGRect(x: 12, y: 25, width: width - 24, height: valueStyle.size)
            [corner, label, value].forEach(layer.addSublayer)
        }
        env.applyScale(to: layer)
    }

    /// A value wider than the plate shrinks down to this fraction before it
    /// truncates (Battery's condition in every language).
    var valueMinimumScale: CGFloat {
        get { value.minimumScale }
        set { HUDMotion.perform { value.minimumScale = newValue } }
    }

    /// `tone` overrides the value colour (Battery's orange temperature).
    func set(label text: String, value number: String, unit: String? = nil, highlighted: Bool = false, tone: CGColor? = nil) {
        HUDMotion.perform {
            label.text = text
            let color = tone.map(HUDColor.legible) ?? (highlighted ? HUDColor.accentInk : valueStyle.color)
            var runs = [HUDText.Run(number, valueStyle.colored(color))]
            if let unit { runs.append(HUDText.Run(" " + unit, HUDType.statUnit)) }
            value.runs = runs
        }
    }

    /// The 3-column grid (gap 8) the spec uses for stat plates.
    static func gridFrames(count: Int, width: CGFloat, columns: Int = 3, rowHeight: CGFloat = 53) -> [CGRect] {
        let cell = (width - CGFloat(columns - 1) * 8) / CGFloat(columns)
        return (0..<count).map { CGRect(x: CGFloat($0 % columns) * (cell + 8), y: CGFloat($0 / columns) * (rowHeight + 8),
                                       width: cell, height: rowHeight) }
    }
}

/// Segment bar and capacity cells (spec §2.12). Charging cells pulse with an
/// 80 ms phase per cell, a live readout and the only loop here.
final class HUDCells {
    let layer = HUDLayer.plain("cells")
    private var cells: [CALayer] = []
    private let env: HUDDesignEnvironment
    let capacity: Bool

    /// - capacity: false for the 8 px segment bar, true for 22 px capacity cells.
    init(count: Int, width: CGFloat, capacity: Bool, height: CGFloat? = nil, env: HUDDesignEnvironment) {
        self.env = env; self.capacity = capacity
        let cellHeight = height ?? (capacity ? 22 : 8)
        let cellWidth = (width - CGFloat(count - 1) * 3) / CGFloat(count)
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: cellHeight)
            cells = (0..<count).map { index in
                let cell = HUDLayer.plain("cell")
                cell.frame = CGRect(x: CGFloat(index) * (cellWidth + 3), y: 0, width: cellWidth, height: cellHeight)
                cell.borderWidth = 1
                layer.addSublayer(cell)
                return cell
            }
        }
    }

    /// Cells running the charging pulse (the live loop; diagnostics count it apart).
    var pulseAnimationCount: Int { cells.filter { $0.animation(forKey: "hud.pulse") != nil }.count }

    /// `pulse` is the mid colour of the charging pulse (spec `cellPulseB`:
    /// the tone mixed 35 % into #2A2A2A); nil keeps the yellow-tone default.
    /// `pulseCount` limits the pulse to the next N empty cells (Battery: 3).
    func set(filled: Int, tone: (high: CGColor, low: CGColor)? = nil, pulsing: Bool = false, animated: Bool = true,
             pulse pulseColor: CGColor? = nil, pulseCount: Int? = nil) {
        let empty = capacity ? HUDColor.tone(0x2A2A2A) : HUDColor.iconButton
        let ring = capacity ? HUDColor.ringField : HUDColor.tone(0x4C4C4C)
        let high = tone?.high ?? (capacity ? HUDColor.yellowHigh : HUDColor.yellow), low = tone?.low ?? HUDColor.yellowLow
        for (index, cell) in cells.enumerated() {
            let on = index < filled
            HUDMotion.perform {
                cell.borderColor = on ? HUDColor.clear : ring
                if on, capacity {
                    cell.backgroundColor = nil
                    cell.showArtwork(HUDArtwork.plate([high, low], size: cell.bounds.size, cut: 0, scale: env.renderScale), scale: env.renderScale)
                } else { cell.contents = nil }
            }
            if !(on && capacity) {
                HUDMotion.animate(cell, "backgroundColor", to: on ? high : empty, duration: animated ? (capacity ? 0.3 : 0.2) : 0,
                                  timing: HUDEasing.ease, env: env)
            }
            cell.removeAnimation(forKey: "hud.pulse")
            guard pulsing, !on, !env.reducesMotion, index < filled + (pulseCount ?? cells.count) else { continue }
            let pulse = CAKeyframeAnimation(keyPath: "backgroundColor")
            // Light: the tone mixed 35 % into the pale empty cell instead.
            let mid = HUDColor.isLight ? HUDTheme.mixed(empty, high, 0.35)
                : pulseColor ?? tone.map { _ in HUDColor.rgb(0x3A3826) } ?? HUDColor.rgb(0x4D4A1C)
            pulse.values = [empty, mid, empty]; pulse.keyTimes = [0, 0.5, 1]
            pulse.duration = 2.4; pulse.repeatCount = .infinity; pulse.timingFunction = HUDEasing.easeInOut
            pulse.beginTime = cell.convertTime(CACurrentMediaTime(), from: nil) + Double(index - filled) * 0.08
            pulse.fillMode = .backwards
            cell.add(pulse, forKey: "hud.pulse")
        }
    }
}
