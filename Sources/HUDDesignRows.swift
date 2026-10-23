import AppKit
import QuartzCore

/// The light row plate (spec §2.3): chamfered gradient plate, micro tag, left
/// notch, label (and a value-only sublabel) and a right-hand control cluster.
/// Hover slides it 3 px, brightens it, grows the notch to yellow and sends one
/// sheen across. Nav rows are clickable as a whole and end in a chevron.
final class HUDRowPlate: HUDControl {
    enum Variant: Equatable {
        case standard, compact, slider, nav(chevron: String)
        var height: CGFloat { self == .standard || isNav ? HUDMetric.rowHeight : HUDMetric.compactRowHeight }
        var isNav: Bool { if case .nav = self { return true }; return false }
    }

    let variant: Variant
    private let plate = HUDLayer.plain("row.plate")
    private let brighten = HUDLayer.shape("row.brighten")
    private let sheen = HUDSheen()
    private let notch = HUDLayer.plain("row.notch")
    private let tag = HUDLayer.plain("row.tag")
    private let title = HUDTextLayer(style: HUDType.rowLabel)
    private let sublabel = HUDTextLayer(style: HUDType.rowSublabel)
    private let chevron = HUDTextLayer(style: HUDTextStyle(size: 16, weight: .bold, color: HUDColor.rgb(0x333333)), alignment: .right)
    /// Right-hand controls; the row lays them out right to left.
    let cluster = HUDLayer.plain("row.cluster")
    private var clusterItems: [CALayer] = []

    /// - height: overrides the variant's height (App Shortcuts rows are 56).
    init(id: String, title text: String, sublabel detail: String? = nil, variant: Variant = .standard,
         width: CGFloat = HUDMetric.box.width, height customHeight: CGFloat? = nil, env: HUDDesignEnvironment) {
        self.variant = variant
        super.init(id: id, label: text, env: env)
        let height = customHeight ?? variant.height
        let bounds = CGRect(x: 0, y: 0, width: width, height: height)
        HUDMotion.perform {
            layer.bounds = bounds
            [plate, brighten, sheen.layer, notch, tag, title, sublabel, cluster, chevron].forEach(layer.addSublayer)
            plate.frame = bounds
            plate.showArtwork(HUDArtwork.plate(HUDColor.rowPlate, locations: [0, 0.55, 1], size: bounds.size,
                                               cut: HUDMetric.rowChamfer, highlight: true, scale: env.renderScale), scale: env.renderScale)
            let shape = HUDShape.chamfer(bounds, cut: HUDMetric.rowChamfer)
            brighten.frame = bounds; brighten.path = shape; brighten.fillColor = HUDColor.white(0.07); brighten.opacity = 0
            sheen.setShape(shape, bounds: bounds)
            notch.backgroundColor = HUDColor.notch
            notch.frame = CGRect(x: 0, y: 9, width: 3, height: height - 18)
            tag.frame = CGRect(origin: CGPoint(x: 18, y: 4), size: HUDArtwork.microTagSize)
            tag.showArtwork(HUDArtwork.microTag(scale: env.renderScale), scale: env.renderScale)
            cluster.frame = bounds
            if case .nav(let mark) = variant { chevron.text = mark } else { chevron.isHidden = true }
        }
        self.title.text = text
        sublabel.text = detail ?? ""
        sublabel.isHidden = detail == nil
        setTarget(bounds, path: HUDShape.chamfer(bounds, cut: HUDMetric.rowChamfer), container: !variant.isNav)
        layoutContents()
    }

    func setTitle(_ text: String) { label = text; title.text = text; layoutContents() }
    /// Room left of the label for a leading handle or glyph (System > Modules).
    var titleInset: CGFloat = 0 { didSet { if titleInset != oldValue { layoutContents() } } }
    func setSublabel(_ text: String?) { sublabel.text = text ?? ""; sublabel.isHidden = text == nil; layoutContents() }
    /// The label's line box in row coordinates (for redaction or tests).
    var titleFrame: CGRect { title.lineFrame }

    /// Controls right-aligned with an 8 px gap, centred on the content box.
    func setCluster(_ layers: [CALayer]) {
        HUDMotion.perform {
            clusterItems.forEach { if $0.superlayer === cluster { $0.removeFromSuperlayer() } }
            clusterItems = layers
            layers.forEach(cluster.addSublayer)
        }
        layoutContents()
    }

    /// The area right of the label column (slider rows fill it).
    var controlArea: CGRect {
        let bounds = layer.bounds, centerY = 10 + (bounds.height - 18) / 2
        let left = variant == .slider ? 18 + HUDMetric.sliderLabelColumn + 14 : 18
        let right = bounds.width - 12 - (variant.isNav ? 22 : 0)
        return CGRect(x: left, y: centerY - 15, width: right - left, height: 30)
    }

    private func layoutContents() {
        let bounds = layer.bounds
        let contentTop: CGFloat = 10, contentHeight = bounds.height - 18
        HUDMotion.perform {
            let labelHeight = 15.5 * 1.2, subHeight = 11 * 1.2
            let column = 3 + labelHeight + (sublabel.isHidden ? 0 : 1 + subHeight)
            let top = contentTop + (contentHeight - column) / 2 + 3
            let right = variant == .slider ? 18 + HUDMetric.sliderLabelColumn : clusterLeft(in: bounds) - 14
            let left = 18 + titleInset
            title.lineFrame = CGRect(x: left, y: top, width: max(10, right - left), height: labelHeight)
            sublabel.lineFrame = CGRect(x: left, y: top + labelHeight + 1, width: max(10, right - left), height: subHeight)
            if variant.isNav {
                chevron.lineFrame = CGRect(x: bounds.width - 12 - 20, y: contentTop + contentHeight / 2 - 8, width: 20, height: 16)
            }
            var x = bounds.width - 12 - (variant.isNav ? 22 : 0)
            let centerY = contentTop + contentHeight / 2
            for item in clusterItems.reversed() {
                let size = item.bounds.size
                x -= size.width
                item.position = CGPoint(x: x + size.width * item.anchorPoint.x, y: centerY - size.height / 2 + size.height * item.anchorPoint.y)
                x -= 8
            }
        }
        env.applyScale(to: title); env.applyScale(to: sublabel); env.applyScale(to: chevron)
    }

    private func clusterLeft(in bounds: CGRect) -> CGFloat {
        let widths = clusterItems.map(\.bounds.width)
        let total = widths.reduce(0, +) + CGFloat(max(0, widths.count - 1)) * 8
        return bounds.width - 12 - (variant.isNav ? 22 : 0) - total
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        HUDMotion.animate(layer, "transform.translation.x", to: on ? 3 : 0, duration: HUDDuration.hover, env: env)
        HUDMotion.animate(brighten, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(notch, "backgroundColor", to: on ? HUDColor.yellowLow : HUDColor.notch,
                          duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        let grown = (layer.bounds.height - 10) / max(1, layer.bounds.height - 18)
        HUDMotion.animate(notch, "transform.scale.y", to: on ? grown : 1, duration: HUDDuration.hover, env: env)
        sheen.play(on, env: env)
    }

    /// A static row VoiceOver reads as text with this value (Event Log rows).
    var readout: String?

    override func pointerDown(at point: CGPoint) -> Bool { variant.isNav && isEnabled }
    override var accessibilityRole: HUDAccessibilityItem.Role? {
        if variant.isNav { return .button }
        return readout.map { .text($0) }
    }
}

/// Segmented OFF | ON switch (spec §2.4). The knob slides with SNAP; the label
/// under it turns dark.
final class HUDToggle: HUDControl {
    static let size = CGSize(width: 88, height: 26)
    private(set) var isOn: Bool
    var onChange: ((Bool) -> Void)?
    private let knob = HUDLayer.plain("toggle.knob")
    private let marks = HUDLayer.shape("toggle.marks")
    private var labels: [(grey: HUDTextLayer, dark: HUDTextLayer)] = []

    init(id: String, label: String, isOn: Bool, env: HUDDesignEnvironment) {
        self.isOn = isOn
        super.init(id: id, label: label, env: env)
        let bounds = CGRect(origin: .zero, size: Self.size)
        HUDMotion.perform {
            layer.bounds = bounds
            layer.backgroundColor = HUDColor.toggleTrack
            layer.borderWidth = 1; layer.borderColor = HUDColor.ringToggle
            for (index, word) in ["OFF", "ON"].enumerated() {
                let frame = CGRect(x: CGFloat(index) * 44, y: 8, width: 44, height: 10)
                let grey = HUDTextLayer(word, style: HUDType.toggleLabel, alignment: .center)
                let dark = HUDTextLayer(word, style: HUDType.toggleLabel.colored(HUDColor.nearBlack), alignment: .center)
                grey.lineFrame = frame; dark.lineFrame = frame
                labels.append((grey, dark))
            }
            knob.frame = CGRect(x: 2, y: 2, width: 42, height: 22)
            marks.frame = knob.bounds
            let path = CGMutablePath()
            path.addPath(HUDShape.cornerMark(CGRect(x: -2, y: -2, width: 5, height: 5), length: 5, width: 1, corner: .topLeft))
            path.addPath(HUDShape.cornerMark(CGRect(x: 39, y: -2, width: 5, height: 5), length: 5, width: 1, corner: .topRight))
            marks.path = path; marks.fillColor = HUDColor.nearBlack
            knob.addSublayer(marks)
            labels.forEach { layer.addSublayer($0.grey) }
            layer.addSublayer(knob)
            labels.forEach { layer.addSublayer($0.dark) }
        }
        setTarget(bounds)
        render(animated: false)
    }

    func setOn(_ value: Bool, animated: Bool) {
        guard value != isOn else { return }
        isOn = value
        render(animated: animated)
    }

    private func render(animated: Bool) {
        let knobDuration = animated ? HUDDuration.knob : 0, labelDuration = animated ? HUDDuration.toggleLabel : 0
        HUDMotion.animate(knob, "transform.translation.x", to: isOn ? 42 : 0, duration: knobDuration, timing: HUDEasing.snap, env: env)
        HUDMotion.animate(knob, "backgroundColor", to: isOn ? HUDColor.yellow : HUDColor.knobOff, duration: knobDuration,
                          timing: HUDEasing.ease, env: env)
        for (index, pair) in labels.enumerated() {
            let active = (index == 1) == isOn
            HUDMotion.animate(pair.dark, "opacity", to: Float(active ? 1 : 0), duration: labelDuration, timing: HUDEasing.ease, env: env)
            HUDMotion.animate(pair.grey, "opacity", to: Float(active ? 0 : 1), duration: labelDuration, timing: HUDEasing.ease, env: env)
        }
    }

    override func activate() {
        guard isEnabled else { return }
        setOn(!isOn, animated: true)
        onChange?(isOn)
        onActivate?()
    }
    override func enabledChanged() { HUDMotion.perform { layer.opacity = isEnabled ? 1 : 0.35 } }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .toggle(isOn) }
}

/// Equal-column segmented control (spec §2.5); `yellow` is Work Mode's variant.
final class HUDSegmentedControl: HUDControl {
    final class Segment: HUDControl {
        fileprivate let text: HUDTextLayer
        fileprivate let bar = HUDLayer.plain("segment.bar")
        fileprivate weak var owner: HUDSegmentedControl?
        fileprivate let index: Int
        init(id: String, title: String, index: Int, env: HUDDesignEnvironment) {
            self.index = index
            text = HUDTextLayer(title, style: HUDType.segmented, alignment: .center)
            super.init(id: id, label: title, env: env)
            HUDMotion.perform {
                layer.borderWidth = 1
                layer.addSublayer(text); layer.addSublayer(bar)
                bar.backgroundColor = HUDColor.nearBlack
            }
        }
        override func hoverChanged() { owner?.renderSegments(animated: true) }
        override func activate() {
            guard isEnabled, let owner else { return }
            if owner.selectedIndex == index { owner.onReselect?(index) } else { owner.select(index, notify: true) }
        }
        override var accessibilityRole: HUDAccessibilityItem.Role? { .tab(selected: owner?.selectedIndex == index) }
    }

    let segments: [Segment]
    private(set) var selectedIndex: Int
    let yellow: Bool
    var onChange: ((Int) -> Void)?
    /// A press on the already selected segment (Work Mode's Custom reopens its editor).
    var onReselect: ((Int) -> Void)?

    init(id: String, label: String, options: [String], selectedIndex: Int, width: CGFloat, yellow: Bool = false,
         env: HUDDesignEnvironment) {
        self.selectedIndex = selectedIndex; self.yellow = yellow
        segments = options.enumerated().map { Segment(id: id + ":\($0.offset)", title: $0.element, index: $0.offset, env: env) }
        super.init(id: id, label: label, env: env)
        let gap: CGFloat = 4, count = CGFloat(max(1, options.count))
        let segmentWidth = (width - gap * (count - 1)) / count
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: 30)
            for segment in segments {
                segment.owner = self
                let frame = CGRect(x: CGFloat(segment.index) * (segmentWidth + gap), y: 0, width: segmentWidth, height: 30)
                segment.layer.frame = frame
                segment.text.lineFrame = CGRect(x: 6, y: 8.75, width: segmentWidth - 12, height: 12.5)
                segment.bar.frame = CGRect(x: 0, y: 0, width: 3, height: 30)
                layer.addSublayer(segment.layer)
                segment.setTarget(CGRect(origin: .zero, size: frame.size))
            }
        }
        renderSegments(animated: false)
    }

    func select(_ index: Int, notify: Bool) {
        guard segments.indices.contains(index), index != selectedIndex else { return }
        selectedIndex = index
        renderSegments(animated: false)
        if notify { onChange?(index) }
    }

    fileprivate func renderSegments(animated: Bool) {
        for segment in segments {
            let selected = segment.index == selectedIndex, hovered = segment.isHovered && !selected
            let fill = selected ? (yellow ? HUDColor.yellow : HUDColor.plate) : HUDColor.control
            let ring = selected ? HUDColor.clear : (hovered ? HUDColor.muted : HUDColor.ringField)
            let duration = animated ? HUDDuration.hover : 0
            HUDMotion.animate(segment.layer, "backgroundColor", to: fill, duration: duration, timing: HUDEasing.ease, env: env)
            HUDMotion.animate(segment.layer, "borderColor", to: ring, duration: duration, timing: HUDEasing.ease, env: env)
            HUDMotion.perform {
                segment.text.color = selected ? HUDColor.nearBlack : (hovered ? HUDColor.foreground(1) : HUDColor.segmentedText)
                segment.bar.isHidden = !selected || yellow
            }
        }
    }

    /// The group itself is not a target; its segments are.
    override var accessibilityRole: HUDAccessibilityItem.Role? { nil }
    override func contains(_ point: CGPoint) -> Bool { false }
}

/// One sheen pass across a hovered plate (spec §1.9): a 105° white band from
/// −110 % to 110 % over .7 s E, clipped to the plate; it snaps back on exit.
/// The clip (a mask) exists only while the band moves.
final class HUDSheen {
    let layer = HUDLayer.plain("sheen")
    private let band = HUDLayer.gradient("sheen.band")
    private var shape: CGPath?

    init(alpha: CGFloat = 0.28) {
        HUDMotion.perform {
            layer.isHidden = true
            band.startPoint = CGPoint(x: 0, y: 0.3); band.endPoint = CGPoint(x: 1, y: 0.7)
            band.colors = [HUDColor.white(0), HUDColor.white(alpha), HUDColor.white(0)]
            band.locations = [0.35, 0.5, 0.65]
            layer.addSublayer(band)
        }
    }

    func setShape(_ path: CGPath, bounds: CGRect) {
        shape = path
        HUDMotion.perform { layer.frame = bounds; band.frame = bounds }
    }

    func play(_ on: Bool, env: HUDDesignEnvironment) {
        band.removeAnimation(forKey: "hud.sheen")
        HUDMotion.perform { layer.isHidden = true; layer.mask = nil }
        guard on, !env.reducesMotion, let shape else { return }
        let mask = HUDLayer.shape()
        HUDMotion.perform {
            mask.frame = layer.bounds; mask.path = shape; mask.fillColor = HUDColor.black(1)
            layer.mask = mask; layer.isHidden = false
        }
        let sweep = CABasicAnimation(keyPath: "transform.translation.x")
        sweep.fromValue = -1.1 * layer.bounds.width; sweep.toValue = 1.1 * layer.bounds.width
        sweep.duration = HUDDuration.sheen; sweep.timingFunction = HUDEasing.standard
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self] in
            guard let self, self.band.animation(forKey: "hud.sheen") == nil else { return }
            HUDMotion.perform { self.layer.isHidden = true; self.layer.mask = nil }
        }
        band.add(sweep, forKey: "hud.sheen")
        CATransaction.commit()
    }
}
