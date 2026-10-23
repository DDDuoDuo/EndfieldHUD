import AppKit
import QuartzCore

/// Notes TODO task card (spec §2.13): light body and a chamfered side zone;
/// completing flips the side to yellow with the COMPLETED watermark and check
/// ring while the body darkens. The action pill toggles, × (shown on hover)
/// deletes. Recording mode replaces the title with a redaction bar (§3.26).
final class HUDTaskCard: HUDControl {
    final class Action: HUDControl {
        fileprivate let plate = HUDLayer.plain("task.action")
        fileprivate let text = HUDTextLayer(style: HUDTextStyle(size: 11.5, weight: .medium, color: HUDColor.rgb(0x222222)))
        fileprivate let glyph = HUDIconLayer(.check, size: 12, color: HUDColor.yellow)
        fileprivate weak var card: HUDTaskCard?
        override func hoverChanged() { card?.renderAction() }
    }

    /// The bare × glyph (#555, #333 when done); it fades in with the card's hover.
    final class Delete: HUDControl {
        fileprivate let glyph = HUDIconLayer(.x, size: 12, color: HUDColor.rgb(0x555555))
    }

    static let height: CGFloat = 60, sideWidth: CGFloat = 168
    static let titleStyle = HUDTextStyle(size: 14.5, weight: .medium, color: HUDColor.nearBlack, lineHeight: 1.15)
    private(set) var isDone: Bool
    private(set) var needsAttention: Bool
    var onToggle: ((Bool) -> Void)?
    var onDelete: (() -> Void)?
    private let titleText: String
    private let bodyLight = HUDLayer.plain(), bodyDone = HUDLayer.plain()
    private let side = HUDLayer.plain("task.side")
    private let sideLight = HUDLayer.plain(), sideDone = HUDLayer.plain()
    private let tagLight = HUDLayer.plain(), tagDone = HUDLayer.plain()
    private let title = HUDTextLayer(style: HUDTaskCard.titleStyle)
    private let barTrack = HUDLayer.plain(), barFill = HUDLayer.plain()
    private let count = HUDTextLayer(style: HUDTextStyle(size: 10.5, weight: .bold, color: HUDColor.steel2, tabular: true))
    private let watermark = HUDTextLayer("COMPLETED", style: HUDTextStyle(size: 33, weight: .black, color: HUDColor.black(0.08), tracking: -0.02))
    private let ring = HUDLayer.plain("task.ring")
    let action: Action
    let delete: Delete
    private let attention = HUDBadge.diamond(size: 8)

    /// - attention: the orange diamond at the top-right while the task is open.
    /// - redacted: Recording mode; the title becomes a bar (and VoiceOver hears no title).
    init(id: String, title text: String, done: Bool, attention showsAttention: Bool = false, redacted: Bool = false, width: CGFloat,
         env: HUDDesignEnvironment) {
        isDone = done; needsAttention = showsAttention
        titleText = redacted ? L10n.text("Redacted", "已隐藏") : text
        action = Action(id: id + ":toggle", label: "", env: env)
        delete = Delete(id: id + ":delete", label: L10n.text("Delete", "删除") + " " + titleText, env: env)
        super.init(id: id, label: titleText, env: env)
        let bounds = CGRect(x: 0, y: 0, width: width, height: Self.height)
        let bodyWidth = width - Self.sideWidth
        action.card = self
        HUDMotion.perform {
            layer.bounds = bounds
            for (plate, colors) in [(bodyLight, [HUDColor.rgb(0xE0E1DC), HUDColor.rgb(0xD0D1CC)]), (bodyDone, [HUDColor.rgb(0x5E5E5E), HUDColor.rgb(0x525252)])] {
                plate.frame = CGRect(x: 0, y: 0, width: bodyWidth, height: Self.height)
                plate.showArtwork(HUDArtwork.plate(colors, size: plate.bounds.size, cut: 0, scale: env.renderScale), scale: env.renderScale)
                layer.addSublayer(plate)
            }
            side.frame = CGRect(x: bodyWidth, y: 0, width: Self.sideWidth, height: Self.height); side.masksToBounds = true
            let sides: [(CALayer, [CGColor], [CGFloat])] = [(sideLight, [HUDColor.rgb(0xBEBFBA), HUDColor.rgb(0xAFB0AB)], [0, 1]),
                                                            (sideDone, [HUDColor.yellowHigh, HUDColor.yellow, HUDColor.yellowDeep], [0, 0.5, 1])]
            for (plate, colors, stops) in sides {
                plate.frame = side.bounds
                plate.showArtwork(HUDArtwork.plate(colors, locations: stops, size: side.bounds.size, cut: 10, scale: env.renderScale), scale: env.renderScale)
                side.addSublayer(plate)
            }
            watermark.lineFrame = CGRect(x: -6, y: Self.height + 9 - 33, width: 220, height: 33)
            ring.frame = CGRect(x: 18, y: 7, width: 46, height: 46); ring.cornerRadius = 23
            ring.borderWidth = 5; ring.borderColor = HUDColor.black(0.08)
            side.addSublayer(watermark); side.addSublayer(ring)
            layer.addSublayer(side)
            for (tag, inverted) in [(tagLight, false), (tagDone, true)] {
                tag.frame = CGRect(origin: CGPoint(x: 14, y: 4), size: HUDArtwork.microTagSize)
                tag.showArtwork(HUDArtwork.microTag(inverted: inverted, scale: env.renderScale), scale: env.renderScale)
                layer.addSublayer(tag)
            }
            title.text = text
            title.lineFrame = CGRect(x: 18, y: 14, width: bodyWidth - 34, height: 14.5 * 1.15)
            barTrack.frame = CGRect(x: 18, y: 14 + 16.7 + 8 + 4, width: 150, height: 3)
            barFill.frame = barTrack.bounds; barFill.anchorPoint = CGPoint(x: 0, y: 0.5); barFill.position = CGPoint(x: 0, y: 1.5)
            barTrack.addSublayer(barFill)
            count.lineFrame = CGRect(x: 178, y: 14 + 16.7 + 8 - 0.5, width: 40, height: 10.5)
            if redacted {
                layer.addSublayer(HUDRedaction.bar(replacing: title.lineFrame, style: Self.titleStyle, width: HUDRedaction.width(for: text), env: env))
            } else { layer.addSublayer(title) }
            [barTrack, count].forEach(layer.addSublayer)
            action.plate.cornerRadius = 13
            [action.plate, action.glyph, action.text].forEach(action.layer.addSublayer)
            delete.layer.bounds = CGRect(x: 0, y: 0, width: 22, height: 22); delete.layer.opacity = 0
            delete.glyph.position = CGPoint(x: 11, y: 11); delete.layer.addSublayer(delete.glyph)
            side.addSublayer(delete.layer); side.addSublayer(action.layer)
            attention.position = CGPoint(x: width + 1, y: 1); attention.zPosition = 2
            layer.addSublayer(attention)
        }
        delete.setTarget(delete.layer.bounds)
        delete.onActivate = { [weak self] in self?.onDelete?() }
        action.onActivate = { [weak self] in self?.toggle() }
        setTarget(bounds, container: true)
        render(animated: false)
        env.applyScale(to: layer)
    }

    var controls: [HUDControl] { [self, action, delete] }

    func toggle() { setDone(!isDone, animated: true); onToggle?(isDone) }

    func setDone(_ done: Bool, animated: Bool) {
        guard done != isDone else { return }
        isDone = done
        render(animated: animated)
    }

    /// Moves the attention diamond in place (it never shows on a done task).
    func setAttention(_ on: Bool) {
        needsAttention = on
        HUDMotion.perform { attention.isHidden = !needsAttention || isDone }
    }

    fileprivate func renderAction() {
        let state = isDone ? L10n.text("Done", "已完成") : L10n.text("In progress", "进行中")
        HUDMotion.perform {
            action.label = titleText + ", " + state
            action.text.text = state
            action.text.color = isDone ? HUDColor.yellow : HUDColor.rgb(0x222222)
            action.glyph.isHidden = !isDone
            let icon: CGFloat = isDone ? 12 + 6 : 0
            let width = 13 + icon + ceil(action.text.preferredWidth) + 13
            action.layer.frame = CGRect(x: Self.sideWidth - 12 - width, y: 17, width: width, height: 26)
            action.plate.frame = action.layer.bounds
            action.plate.backgroundColor = isDone ? HUDColor.counterBackground
                : (action.isHovered ? HUDColor.white(1) : HUDColor.rgb(0xDCDCDA))
            action.glyph.frame = CGRect(x: 13, y: 7, width: 12, height: 12)
            action.text.lineFrame = CGRect(x: 13 + icon, y: 7.25, width: width - 26 - icon + 1, height: 11.5)
            delete.layer.frame = CGRect(x: action.layer.frame.minX - 8 - 22, y: 19, width: 22, height: 22)
            delete.glyph.color = isDone ? HUDColor.rgb(0x333333) : HUDColor.rgb(0x555555)
        }
        action.setTarget(action.layer.bounds, path: CGPath(roundedRect: action.layer.bounds, cornerWidth: 13, cornerHeight: 13, transform: nil))
        env.applyScale(to: action.layer)
    }

    private func render(animated: Bool) {
        let duration = animated ? HUDDuration.taskFlip : 0
        HUDMotion.animate(bodyDone, "opacity", to: Float(isDone ? 1 : 0), duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(sideDone, "opacity", to: Float(isDone ? 1 : 0), duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(tagDone, "opacity", to: Float(isDone ? 1 : 0), duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(watermark, "transform.translation.x", to: isDone ? 0 : 30, duration: animated ? 0.45 : 0, env: env)
        HUDMotion.animate(watermark, "opacity", to: Float(isDone ? 1 : 0), duration: animated ? 0.3 : 0, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(ring, "transform.scale", to: isDone ? 1 : 0.6, duration: animated ? 0.35 : 0, env: env)
        HUDMotion.animate(ring, "opacity", to: Float(isDone ? 1 : 0), duration: animated ? 0.35 : 0, env: env)
        HUDMotion.animate(barFill, "transform.scale.x", to: isDone ? 1 : 0.0001, duration: animated ? 0.35 : 0, env: env)
        HUDMotion.perform {
            title.color = isDone ? HUDColor.rgb(0xF2F2F2) : HUDColor.nearBlack
            barTrack.backgroundColor = isDone ? HUDColor.rgb(0x3F3F3F) : HUDColor.rgb(0xA9AAA5)
            barFill.backgroundColor = isDone ? HUDColor.yellow : HUDColor.counterBackground
            count.text = isDone ? "1/1" : "0/1"
            count.color = isDone ? HUDColor.rgb(0xCFCFCF) : HUDColor.steel2
            attention.isHidden = !needsAttention || isDone
        }
        renderAction()
    }

    override func hoverChanged() {
        HUDMotion.animate(delete.layer, "opacity", to: Float(isHovered ? 1 : 0), duration: 0.12, timing: HUDEasing.ease, env: env)
    }
    override func pointerDown(at point: CGPoint) -> Bool { false }
    override var accessibilityRole: HUDAccessibilityItem.Role? { nil }
    /// The × is transparent until the card is hovered; VoiceOver reaches it anyway.
    override func accessibilityItems(in root: CALayer) -> [HUDAccessibilityItem] {
        guard delete.layer.opacity < 0.5 else { return [] }
        return [HUDAccessibilityItem(id: delete.id, label: delete.label, role: .button, rect: delete.rect(in: root))]
    }
}

/// Clipboard list card (spec §2.14): type block with a yellow type label,
/// title and meta, age with a COPY reveal on hover, a pinned corner and a
/// yellow flash when copied. An image record can show its thumbnail in the
/// type block; Recording mode replaces the title and meta with bars (§3.26).
final class HUDListCard: HUDControl {
    enum Kind: String { case text = "TEXT", link = "LINK", image = "IMAGE", file = "FILE" }
    static let height: CGFloat = 58
    static let titleStyle = HUDTextStyle(size: 13.5, weight: .medium, color: HUDColor.nearBlack, lineHeight: 1.25)
    static let metaStyle = HUDTextStyle(size: 10.5, weight: .regular, color: HUDColor.steel2, lineHeight: 1.2)
    private static let metaColumn = HUDTextStyle(size: 10, weight: .bold, color: HUDColor.rgb(0x55564F))
    private let plate = HUDLayer.plain("card.plate")
    private let brighten = HUDLayer.shape()
    private let flash = HUDLayer.shape()
    private let leftBar = HUDLayer.plain()
    private let sheen = HUDSheen()
    private let copy = HUDLayer.plain("card.copy")
    private let ageText = HUDTextLayer(style: HUDListCard.metaColumn, alignment: .right)
    /// Row actions (Clipboard's pin and delete), nested left of the age
    /// column; they fade in with the hover or focus state.
    let actions: [HUDControl]
    private let actionStrip = HUDLayer.plain("card.actions")
    /// Keyboard focus shows the hover state.
    var isFocused = false { didSet { if isFocused != oldValue { renderHighlight() } } }

    init(id: String, kind: Kind, title: String, meta: String, age: String, pinned: Bool, width: CGFloat,
         thumbnail: CGImage? = nil, copyTitle: String = "COPY", redacted: Bool = false, actions: [HUDControl] = [], env: HUDDesignEnvironment) {
        self.actions = actions
        super.init(id: id, label: title, env: env)
        let bounds = CGRect(x: 0, y: 0, width: width, height: Self.height)
        let shape = HUDShape.chamfer(bounds, cut: 10)
        let stripWidth = actions.reduce(CGFloat(0)) { $0 + $1.layer.bounds.width } + CGFloat(max(0, actions.count - 1)) * 6
        let textWidth = width - 64 - 80 - (actions.isEmpty ? 0 : stripWidth + 8)
        HUDMotion.perform {
            layer.bounds = bounds
            plate.frame = bounds
            plate.showArtwork(HUDArtwork.plate([HUDColor.rgb(0xE2E3DE), HUDColor.rgb(0xCFD0CB)], size: bounds.size, cut: 10,
                                               scale: env.renderScale), scale: env.renderScale)
            brighten.frame = bounds; brighten.path = shape; brighten.fillColor = HUDColor.white(0.07); brighten.opacity = 0
            flash.frame = bounds; flash.path = shape; flash.fillColor = HUDColor.yellow.copy(alpha: 0.55); flash.opacity = 0
            sheen.setShape(shape, bounds: bounds)
            leftBar.frame = CGRect(x: 0, y: 0, width: 3, height: Self.height); leftBar.backgroundColor = HUDColor.clear
            let block = HUDLayer.plain()
            block.frame = CGRect(x: 8, y: 7, width: 44, height: 44); block.backgroundColor = HUDColor.rgb(0x1B1B1B)
            if let thumbnail {
                let image = HUDLayer.plain("card.thumbnail")
                image.frame = CGRect(x: 0, y: 0, width: 44, height: 35); image.masksToBounds = true
                image.contents = thumbnail; image.contentsGravity = .resizeAspectFill
                block.addSublayer(image)
            } else {
                let icon: HUDIcon = kind == .text ? .text : kind == .link ? .link : kind == .image ? .image : .file
                let glyph = HUDIconLayer(icon, size: 18, color: HUDColor.rgb(0xF2F2F2)); glyph.position = CGPoint(x: 22, y: 20)
                block.addSublayer(glyph)
            }
            let strip = HUDLayer.plain(); strip.frame = CGRect(x: 0, y: 35, width: 44, height: 10); strip.backgroundColor = HUDColor.yellow
            let type = HUDTextLayer(kind.rawValue, style: HUDTextStyle(size: 6.5, weight: .black, color: HUDColor.nearBlack, lineHeight: 10 / 6.5, tracking: 0.1), alignment: .center)
            type.lineFrame = CGRect(x: 0, y: 0, width: 44, height: 10)
            strip.addSublayer(type); block.addSublayer(strip)
            [plate, brighten, flash, sheen.layer, leftBar, block].forEach(layer.addSublayer)
            let titleLine = CGRect(x: 64, y: 13, width: textWidth, height: 16.9)
            let metaLine = CGRect(x: 64, y: 13 + 16.9 + 3, width: textWidth, height: 12.6)
            if redacted {
                layer.addSublayer(HUDRedaction.bar(replacing: titleLine, style: Self.titleStyle, width: HUDRedaction.width(for: title), env: env))
                layer.addSublayer(HUDRedaction.bar(replacing: metaLine, style: Self.metaStyle, width: 96, env: env))
            } else {
                let titleText = HUDTextLayer(title, style: Self.titleStyle), metaText = HUDTextLayer(meta, style: Self.metaStyle)
                titleText.lineFrame = titleLine; metaText.lineFrame = metaLine
                layer.addSublayer(titleText); layer.addSublayer(metaText)
            }
            // `.mt`: two 16 px lines (age, then the COPY reveal), right-aligned and centred.
            ageText.text = age
            ageText.lineFrame = CGRect(x: width - 10 - 70, y: 13, width: 70, height: 16)
            let copyText = HUDTextLayer(copyTitle, style: Self.metaColumn)
            let textWidth = ceil(copyText.preferredWidth), copyWidth = 14 + 4 + textWidth
            copy.frame = CGRect(x: width - 10 - copyWidth, y: 29, width: copyWidth, height: 16); copy.opacity = 0
            copy.setValue(6, forKeyPath: "transform.translation.x")
            let copyIcon = HUDIconLayer(.copy, size: 14, color: HUDColor.rgb(0x55564F))
            copyIcon.frame = CGRect(x: 0, y: 2, width: 14, height: 14)
            copyText.lineFrame = CGRect(x: 18, y: 0, width: textWidth + 1, height: 16)
            copy.addSublayer(copyIcon); copy.addSublayer(copyText)
            layer.addSublayer(ageText); layer.addSublayer(copy)
            if !actions.isEmpty {
                actionStrip.frame = CGRect(x: width - 80 - stripWidth, y: 0, width: stripWidth, height: Self.height); actionStrip.opacity = 0
                var x: CGFloat = 0
                for action in actions {
                    let size = action.layer.bounds.size
                    action.layer.frame = CGRect(origin: CGPoint(x: x, y: ((Self.height - size.height) / 2).rounded()), size: size)
                    actionStrip.addSublayer(action.layer); x += size.width + 6
                }
                layer.addSublayer(actionStrip)
            }
            if pinned {
                let corner = HUDLayer.shape()
                corner.frame = CGRect(x: width - 14, y: 0, width: 14, height: 14)
                let path = CGMutablePath(); path.addLines(between: [.zero, CGPoint(x: 14, y: 0), CGPoint(x: 14, y: 14)]); path.closeSubpath()
                corner.path = path; corner.fillColor = HUDColor.yellow; corner.zPosition = 1
                layer.addSublayer(corner)
            }
        }
        // The card stays hovered while one of its actions is.
        setTarget(bounds, path: shape, container: !actions.isEmpty)
        env.applyScale(to: layer)
    }

    var controls: [HUDControl] { [self] + actions }

    /// A press on the action strip (between the actions, or on a disabled one) never copies.
    override func contains(_ point: CGPoint) -> Bool { super.contains(point) && (actions.isEmpty || !actionStrip.frame.contains(point)) }

    /// The actions are transparent until the card is hovered; VoiceOver reaches them anyway.
    override func accessibilityItems(in root: CALayer) -> [HUDAccessibilityItem] {
        super.accessibilityItems(in: root) + (actionStrip.opacity < 0.5 ? actions.flatMap { $0.accessibilityItems(in: root) } : [])
    }

    /// The age label changes in place (no rebuild on a clock tick).
    func setAge(_ text: String) { if ageText.text != text { HUDMotion.perform { ageText.text = text } } }

    override func hoverChanged() {
        renderHighlight()
        sheen.play(isHovered && isEnabled, env: env)
    }

    private func renderHighlight() {
        let on = (isHovered || isFocused) && isEnabled
        HUDMotion.animate(layer, "transform.translation.x", to: on ? 3 : 0, duration: HUDDuration.hover, env: env)
        HUDMotion.animate(brighten, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(leftBar, "backgroundColor", to: on ? HUDColor.nearBlack : HUDColor.clear, duration: 0.1, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(copy, "opacity", to: Float(on ? 1 : 0), duration: 0.14, env: env)
        HUDMotion.animate(copy, "transform.translation.x", to: on ? 0 : 6, duration: 0.14, env: env)
        if !actions.isEmpty { HUDMotion.animate(actionStrip, "opacity", to: Float(on ? 1 : 0), duration: 0.14, env: env) }
    }

    /// okflash: the inset yellow fill fades out over .6 s.
    func flashCopied() {
        HUDMotion.perform { flash.opacity = 0 }
        guard !env.reducesMotion else { return }
        let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 1; fade.toValue = 0
        fade.duration = 0.6; fade.timingFunction = HUDEasing.easeOut
        flash.add(fade, forKey: "hud.flash")
    }
}

/// Archive / File Shelf tile (spec §2.15): gradient well with a code bar and
/// number, folded doc glyph with its extension, size/date tags, label strip;
/// selection tightens yellow L-brackets and inverts the label.
final class HUDArchiveTile: HUDControl {
    enum Extension: String {
        case pdf, img, zip, vid, txt
        var color: CGColor {
            switch self {
            case .pdf: return HUDColor.red
            case .img: return HUDColor.cyan
            case .zip: return HUDColor.rgb(0x8A6D00)
            case .vid: return HUDColor.rgb(0x7A3CC9)
            case .txt: return HUDColor.rgb(0x333333)
            }
        }
    }

    private let well = HUDLayer.plain("tile.well")
    private let wellBrighten = HUDLayer.plain()
    private let doc: CALayer
    private let strip = HUDLayer.plain("tile.label")
    private let labelText: HUDTextLayer
    private let brackets = HUDLayer.shape("tile.brackets")
    private(set) var isSelected = false
    private let wellHeight: CGFloat
    static let labelStyle = HUDTextStyle(size: 11.5, weight: .medium, color: HUDColor.nearBlack)
    /// `classified` (spec §3.9.4): dark well, red rotated stamp, red `[ CLASSIFIED ]` strip.
    enum Style { case standard, classified }
    let style: Style
    private var poster: CALayer?

    /// - extLabel: the text on the doc glyph (a file's own extension); the kind's name by default.
    /// - redacted: Recording mode, the label becomes an 84 px bar (spec §3.26).
    init(id: String, number: Int, title: String, ext: Extension, extLabel: String? = nil, size: String?, date: String?, width: CGFloat,
         wellHeight: CGFloat = 100, redacted: Bool = false, style: Style = .standard, env: HUDDesignEnvironment) {
        self.wellHeight = wellHeight; self.style = style
        doc = Self.docGlyph(ext: ext, label: extLabel, env: env)
        labelText = HUDTextLayer(title, style: style == .classified
            ? HUDTextStyle(size: 10, weight: .bold, color: HUDColor.red, tracking: 0.12) : Self.labelStyle, alignment: .center)
        super.init(id: id, label: title, env: env)
        let bounds = CGRect(x: 0, y: 0, width: width, height: wellHeight + 24)
        HUDMotion.perform {
            layer.bounds = bounds
            well.frame = CGRect(x: 0, y: 0, width: width, height: wellHeight)
            well.showArtwork(style == .classified
                ? HUDArtwork.plate([HUDColor.rgb(0x2A2A2A), HUDColor.rgb(0x151515)], size: well.bounds.size, cut: 0, scale: env.renderScale)
                : HUDArtwork.plate([HUDColor.rgb(0x8A8885), HUDColor.rgb(0xBDBDBB), HUDColor.rgb(0xD9D9D8)], locations: [0, 0.55, 1],
                                   size: well.bounds.size, cut: 0, scale: env.renderScale), scale: env.renderScale)
            well.masksToBounds = true
            wellBrighten.frame = well.bounds; wellBrighten.backgroundColor = HUDColor.white(0.12); wellBrighten.opacity = 0
            let codeBar = HUDLayer.plain(); codeBar.frame = CGRect(x: 0, y: 0, width: 34, height: 4); codeBar.backgroundColor = HUDColor.counterBackground
            let code = HUDTextLayer("#" + HUDFormat.pad2(number), style: HUDTextStyle(size: 6, weight: .bold, color: HUDColor.rgb(0x222222)))
            code.lineFrame = CGRect(x: 38, y: -1, width: 30, height: 6)
            doc.position = CGPoint(x: width / 2, y: wellHeight / 2)
            [wellBrighten, codeBar, code].forEach(well.addSublayer)
            if style == .classified { well.addSublayer(Self.classifiedStamp(center: CGPoint(x: width / 2, y: wellHeight / 2), env: env)) }
            else { well.addSublayer(doc) }
            if let size {
                let tag = HUDTextLayer(size, style: HUDTextStyle(size: 8.5, weight: .bold, color: HUDColor.onDarkTag))
                let w = ceil(tag.preferredWidth) + 8
                let plate = HUDLayer.plain(); plate.frame = CGRect(x: width - 4 - w, y: wellHeight - 4 - 12, width: w, height: 12)
                plate.backgroundColor = HUDColor.rgb(0x141414, 0.75)
                tag.lineFrame = CGRect(x: 4, y: 1.75, width: w - 8 + 1, height: 8.5)
                plate.addSublayer(tag); well.addSublayer(plate)
            }
            if let date {
                let tag = HUDTextLayer(date, style: HUDTextStyle(size: 8.5, weight: .bold, color: HUDColor.rgb(style == .classified ? 0x777777 : 0x222222)))
                tag.lineFrame = CGRect(x: 6, y: wellHeight - 4 - 9, width: width / 2, height: 8.5)
                well.addSublayer(tag)
            }
            strip.frame = CGRect(x: 0, y: wellHeight, width: width, height: 24)
            labelText.lineFrame = CGRect(x: 6, y: 6.25, width: width - 12, height: 11.5)
            if redacted {
                strip.addSublayer(HUDRedaction.bar(replacing: labelText.lineFrame, style: Self.labelStyle, width: 84, alignment: .center, env: env))
            } else { strip.addSublayer(labelText) }
            brackets.frame = bounds; brackets.fillColor = HUDColor.yellow; brackets.opacity = 0
            brackets.path = Self.bracketPath(bounds, inset: -5)
            [well, strip, brackets].forEach(layer.addSublayer)
        }
        renderSelection(animated: false)
        setTarget(bounds)
        env.applyScale(to: layer)
    }

    /// The folded document glyph (spec §2.15 `.doc`): 40 × 50, ruled lines and
    /// the extension in the kind's colour, rotated −6°. `label` replaces the
    /// kind's name (a file's own extension).
    static func docGlyph(ext: Extension, label: String? = nil, env: HUDDesignEnvironment) -> CALayer {
        let doc = HUDLayer.plain("tile.doc")
        HUDMotion.perform {
            doc.bounds = CGRect(x: 0, y: 0, width: 40, height: 50)
            let page = HUDLayer.shape(); page.frame = doc.bounds
            let fold = CGMutablePath()
            fold.addLines(between: [.zero, CGPoint(x: 28, y: 0), CGPoint(x: 40, y: 11), CGPoint(x: 40, y: 50), CGPoint(x: 0, y: 50)]); fold.closeSubpath()
            page.path = fold; page.fillColor = HUDColor.rgb(0xF4F4F2)
            doc.shadowColor = HUDColor.black(1); doc.shadowOpacity = 0.25; doc.shadowRadius = 6; doc.shadowOffset = CGSize(width: 0, height: 6)
            doc.shadowPath = fold
            let lines = HUDLayer.shape(); lines.frame = CGRect(x: 6, y: 10, width: 24, height: 14)
            lines.path = { let p = CGMutablePath(); var y: CGFloat = 0; while y < 14 { p.addRect(CGRect(x: 0, y: y, width: 24, height: 1)); y += 4 }; return p }()
            lines.fillColor = HUDColor.rgb(0x9B9B9B)
            let extText = HUDTextLayer((label ?? ext.rawValue).uppercased(), style: HUDTextStyle(size: 8, weight: .black, color: ext.color, tracking: 0.06),
                                       alignment: .center)
            extText.lineFrame = CGRect(x: 0, y: 36, width: 40, height: 8)
            [page, lines, extText].forEach(doc.addSublayer)
            doc.setValue(-6 * CGFloat.pi / 180, forKeyPath: "transform.rotation.z")
        }
        env.applyScale(to: doc)
        return doc
    }

    /// Two 9 px wide vertical brackets, 2 px yellow, open toward the centre.
    static func bracketPath(_ bounds: CGRect, inset: CGFloat) -> CGPath {
        let rect = bounds.insetBy(dx: inset, dy: inset), path = CGMutablePath()
        for left in [true, false] {
            let x = left ? rect.minX : rect.maxX - 9
            path.addRect(CGRect(x: x, y: rect.minY, width: 9, height: 2))
            path.addRect(CGRect(x: x, y: rect.maxY - 2, width: 9, height: 2))
            path.addRect(CGRect(x: left ? rect.minX : rect.maxX - 2, y: rect.minY, width: 2, height: rect.height))
        }
        return path
    }

    /// The red stamp of a classified tile: padding 3 × 7, 2 px ring, 900 10 px, rotated −10°.
    private static func classifiedStamp(center: CGPoint, env: HUDDesignEnvironment) -> CALayer {
        let stamp = HUDLayer.plain("tile.stamp")
        let text = HUDTextLayer(L10n.text("CLASSIFIED", "机密"), style: HUDTextStyle(size: 10, weight: .black, color: HUDColor.red, tracking: 0.18),
                                alignment: .center)
        let width = ceil(text.preferredWidth) + 14 + 4
        HUDMotion.perform {
            stamp.bounds = CGRect(x: 0, y: 0, width: width, height: 10 + 6 + 4)
            stamp.position = center
            stamp.borderWidth = 2; stamp.borderColor = HUDColor.red
            text.lineFrame = CGRect(x: 0, y: 5, width: width, height: 10)
            stamp.addSublayer(text)
            stamp.setValue(-10 * CGFloat.pi / 180, forKeyPath: "transform.rotation.z")
        }
        env.applyScale(to: stamp)
        return stamp
    }

    /// A media thumbnail filling the well in place of the doc glyph (nil restores the glyph).
    func setPoster(_ image: CGImage?) {
        guard style == .standard, image != nil || poster != nil else { return }
        HUDMotion.perform {
            if poster == nil, image != nil {
                let layer = HUDLayer.plain("tile.poster")
                layer.frame = well.bounds; layer.contentsGravity = .resizeAspectFill; layer.masksToBounds = true
                well.insertSublayer(layer, at: 0); poster = layer
            }
            poster?.contents = image; poster?.isHidden = image == nil
            doc.isHidden = image != nil
        }
    }

    func setSelected(_ selected: Bool, animated: Bool = true) {
        guard selected != isSelected else { return }
        isSelected = selected
        renderSelection(animated: animated)
    }

    private func renderSelection(animated: Bool) {
        HUDMotion.animate(brackets, "opacity", to: Float(isSelected ? 1 : 0), duration: animated ? 0.1 : 0, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(brackets, "path", to: Self.bracketPath(layer.bounds, inset: isSelected ? -4 : -5), duration: animated ? 0.16 : 0, env: env)
        HUDMotion.perform {
            strip.backgroundColor = isSelected ? HUDColor.counterBackground : HUDColor.rgb(style == .classified ? 0x262626 : 0xC4C4C6)
            labelText.color = isSelected ? HUDColor.yellow : (style == .classified ? HUDColor.red : HUDColor.nearBlack)
        }
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        HUDMotion.animate(wellBrighten, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(well, "transform.translation.y", to: on ? -2 : 0, duration: 0.25, env: env)
        HUDMotion.animate(doc, "transform", to: NSValue(caTransform3D: CATransform3DRotate(
            CATransform3DMakeTranslation(0, on ? -3 : 0, 0), (on ? -2 : -6) * .pi / 180, 0, 0, 1)), duration: 0.3, env: env)
    }

    /// gone: fade and scale to .85, then the caller removes the tile.
    func playRemoval(completion: @escaping () -> Void) {
        guard !env.reducesMotion else { completion(); return }
        CATransaction.begin()
        CATransaction.setCompletionBlock(completion)
        HUDMotion.animate(layer, "opacity", to: Float(0), duration: 0.28, env: env)
        HUDMotion.animate(layer, "transform.scale", to: 0.85, duration: 0.28, env: env)
        CATransaction.commit()
    }
}
