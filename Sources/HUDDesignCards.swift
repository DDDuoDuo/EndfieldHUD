import AppKit
import QuartzCore

/// Notes TODO task card (spec §2.13): light body and a chamfered side zone;
/// completing flips the side to yellow with the COMPLETED watermark and check
/// ring while the body darkens. The action pill toggles, × deletes.
final class HUDTaskCard: HUDControl {
    final class Action: HUDControl {
        fileprivate let plate = HUDLayer.plain("task.action")
        fileprivate let text = HUDTextLayer(style: HUDTextStyle(size: 11.5, weight: .medium, color: HUDColor.rgb(0x222222)))
        fileprivate weak var card: HUDTaskCard?
        override func hoverChanged() { card?.renderAction() }
    }

    static let height: CGFloat = 60, sideWidth: CGFloat = 168
    private(set) var isDone: Bool
    var onToggle: ((Bool) -> Void)?
    var onDelete: (() -> Void)?
    private let bodyLight = HUDLayer.plain(), bodyDone = HUDLayer.plain()
    private let side = HUDLayer.plain("task.side")
    private let sideLight = HUDLayer.plain(), sideDone = HUDLayer.plain()
    private let tagLight = HUDLayer.plain(), tagDone = HUDLayer.plain()
    private let title = HUDTextLayer(style: HUDTextStyle(size: 14.5, weight: .medium, color: HUDColor.nearBlack, lineHeight: 1.15))
    private let barTrack = HUDLayer.plain(), barFill = HUDLayer.plain()
    private let count = HUDTextLayer(style: HUDTextStyle(size: 10.5, weight: .bold, color: HUDColor.steel2, tabular: true))
    private let watermark = HUDTextLayer("COMPLETED", style: HUDTextStyle(size: 33, weight: .black, color: HUDColor.black(0.08), tracking: -0.02))
    private let ring = HUDLayer.plain("task.ring")
    let action: Action
    let delete: HUDIconButton
    private let attention = HUDBadge.diamond(size: 8)

    init(id: String, title text: String, done: Bool, attention needsAttention: Bool = false, width: CGFloat, env: HUDDesignEnvironment) {
        isDone = done
        action = Action(id: id + ":toggle", label: "", env: env)
        delete = HUDIconButton(id: id + ":delete", label: L10n.text("Delete", "删除"), icon: .x, style: .dark, side: 22, iconSize: 12, env: env)
        super.init(id: id, label: text, env: env)
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
            for (plate, colors, stops) in [(sideLight, [HUDColor.rgb(0xBEBFBA), HUDColor.rgb(0xAFB0AB)], [CGFloat(0), 1]),
                                           (sideDone, [HUDColor.yellowHigh, HUDColor.yellow, HUDColor.rgb(0xF1DD10)], [0, 0.5, 1])] {
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
            [title, barTrack, count].forEach(layer.addSublayer)
            action.plate.cornerRadius = 13
            action.layer.addSublayer(action.plate); action.layer.addSublayer(action.text)
            side.addSublayer(delete.layer); side.addSublayer(action.layer)
            attention.position = CGPoint(x: width + 1, y: 1); attention.isHidden = !needsAttention
            layer.addSublayer(attention)
        }
        delete.onActivate = { [weak self] in self?.onDelete?() }
        action.onActivate = { [weak self] in self?.toggle() }
        setTarget(bounds, container: true)
        render(animated: false)
    }

    var controls: [HUDControl] { [self, action, delete] }

    func toggle() { setDone(!isDone, animated: true); onToggle?(isDone) }

    func setDone(_ done: Bool, animated: Bool) {
        guard done != isDone else { return }
        isDone = done
        render(animated: animated)
    }

    fileprivate func renderAction() {
        let label = isDone ? "✓ " + L10n.text("Done", "已完成") : L10n.text("In progress", "进行中")
        HUDMotion.perform {
            action.label = label
            action.text.text = label
            action.text.color = isDone ? HUDColor.yellow : HUDColor.rgb(0x222222)
            let width = ceil(action.text.preferredWidth) + 26
            action.layer.frame = CGRect(x: Self.sideWidth - 12 - width, y: 17, width: width, height: 26)
            action.plate.frame = action.layer.bounds
            action.plate.backgroundColor = isDone ? HUDColor.counterBackground
                : (action.isHovered ? HUDColor.white(1) : HUDColor.rgb(0xDCDCDA))
            action.text.lineFrame = CGRect(x: 13, y: 7.25, width: width - 26 + 1, height: 11.5)
            delete.layer.frame = CGRect(x: action.layer.frame.minX - 8 - 22, y: 19, width: 22, height: 22)
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
            attention.isHidden = attention.isHidden || isDone
        }
        renderAction()
    }

    override func hoverChanged() {
        HUDMotion.animate(delete.layer, "opacity", to: Float(isHovered ? 1 : 0), duration: 0.12, timing: HUDEasing.ease, env: env)
    }
    override func pointerDown(at point: CGPoint) -> Bool { false }
    override var accessibilityRole: HUDAccessibilityItem.Role? { nil }
}

/// Clipboard list card (spec §2.14): type block with a yellow type label,
/// title and meta, age with a COPY reveal on hover, a pinned corner and a
/// yellow flash when copied.
final class HUDListCard: HUDControl {
    enum Kind: String { case text = "TEXT", link = "LINK", image = "IMAGE", file = "FILE" }
    static let height: CGFloat = 58
    private let plate = HUDLayer.plain("card.plate")
    private let brighten = HUDLayer.shape()
    private let flash = HUDLayer.shape()
    private let leftBar = HUDLayer.plain()
    private let sheen = HUDSheen()
    private let copy = HUDLayer.plain("card.copy")

    init(id: String, kind: Kind, title: String, meta: String, age: String, pinned: Bool, width: CGFloat, env: HUDDesignEnvironment) {
        super.init(id: id, label: title, env: env)
        let bounds = CGRect(x: 0, y: 0, width: width, height: Self.height)
        let shape = HUDShape.chamfer(bounds, cut: 10)
        HUDMotion.perform {
            layer.bounds = bounds
            plate.frame = bounds
            plate.showArtwork(HUDArtwork.plate([HUDColor.rgb(0xE2E3DE), HUDColor.rgb(0xCFD0CB)], size: bounds.size, cut: 10,
                                               scale: env.renderScale), scale: env.renderScale)
            brighten.frame = bounds; brighten.path = shape; brighten.fillColor = HUDColor.white(0.07); brighten.opacity = 0
            flash.frame = bounds; flash.path = shape; flash.fillColor = HUDColor.rgb(0xFFFA00, 0.55); flash.opacity = 0
            sheen.setShape(shape, bounds: bounds)
            leftBar.frame = CGRect(x: 0, y: 0, width: 3, height: Self.height); leftBar.backgroundColor = HUDColor.clear
            let block = HUDLayer.plain()
            block.frame = CGRect(x: 8, y: 7, width: 44, height: 44); block.backgroundColor = HUDColor.rgb(0x1B1B1B)
            let icon: HUDIcon = kind == .text ? .text : kind == .link ? .link : kind == .image ? .image : .file
            let glyph = HUDIconLayer(icon, size: 18, color: HUDColor.rgb(0xF2F2F2)); glyph.position = CGPoint(x: 22, y: 20)
            let strip = HUDLayer.plain(); strip.frame = CGRect(x: 0, y: 35, width: 44, height: 10); strip.backgroundColor = HUDColor.yellow
            let type = HUDTextLayer(kind.rawValue, style: HUDTextStyle(size: 6.5, weight: .black, color: HUDColor.nearBlack, lineHeight: 10 / 6.5, tracking: 0.1), alignment: .center)
            type.lineFrame = CGRect(x: 0, y: 0, width: 44, height: 10)
            strip.addSublayer(type); block.addSublayer(glyph); block.addSublayer(strip)
            let titleText = HUDTextLayer(title, style: HUDTextStyle(size: 13.5, weight: .medium, color: HUDColor.nearBlack, lineHeight: 1.25))
            let metaText = HUDTextLayer(meta, style: HUDTextStyle(size: 10.5, weight: .regular, color: HUDColor.steel2, lineHeight: 1.2))
            titleText.lineFrame = CGRect(x: 64, y: 13, width: width - 64 - 80, height: 16.9)
            metaText.lineFrame = CGRect(x: 64, y: 13 + 16.9 + 3, width: width - 64 - 80, height: 12.6)
            let ageText = HUDTextLayer(age, style: HUDTextStyle(size: 10, weight: .bold, color: HUDColor.rgb(0x55564F)), alignment: .right)
            ageText.lineFrame = CGRect(x: width - 10 - 70, y: 15, width: 70, height: 16)
            let copyText = HUDTextLayer("COPY", style: HUDTextStyle(size: 10, weight: .bold, color: HUDColor.rgb(0x55564F)), alignment: .right)
            copyText.lineFrame = CGRect(x: 0, y: 0, width: 52, height: 16)
            let copyIcon = HUDIconLayer(.copy, size: 14, color: HUDColor.rgb(0x55564F))
            copyIcon.frame = CGRect(x: 70 - 52 - 18, y: 1, width: 14, height: 14)
            copy.frame = CGRect(x: width - 10 - 70, y: 31, width: 70, height: 16); copy.opacity = 0
            copy.setValue(6, forKeyPath: "transform.translation.x")
            copyText.frame.origin.x = 18
            copy.addSublayer(copyIcon); copy.addSublayer(copyText)
            [plate, brighten, flash, sheen.layer, leftBar, block, titleText, metaText, ageText, copy].forEach(layer.addSublayer)
            if pinned {
                let corner = HUDLayer.shape()
                corner.frame = CGRect(x: width - 14, y: 0, width: 14, height: 14)
                let path = CGMutablePath(); path.addLines(between: [.zero, CGPoint(x: 14, y: 0), CGPoint(x: 14, y: 14)]); path.closeSubpath()
                corner.path = path; corner.fillColor = HUDColor.yellow; corner.zPosition = 1
                layer.addSublayer(corner)
            }
        }
        setTarget(bounds, path: shape)
        env.applyScale(to: layer)
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        HUDMotion.animate(layer, "transform.translation.x", to: on ? 3 : 0, duration: HUDDuration.hover, env: env)
        HUDMotion.animate(brighten, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(leftBar, "backgroundColor", to: on ? HUDColor.nearBlack : HUDColor.clear, duration: 0.1, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(copy, "opacity", to: Float(on ? 1 : 0), duration: 0.14, env: env)
        HUDMotion.animate(copy, "transform.translation.x", to: on ? 0 : 6, duration: 0.14, env: env)
        sheen.play(on, env: env)
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
    private let doc = HUDLayer.plain("tile.doc")
    private let strip = HUDLayer.plain("tile.label")
    private let labelText: HUDTextLayer
    private let brackets = HUDLayer.shape("tile.brackets")
    private(set) var isSelected = false
    private let wellHeight: CGFloat

    init(id: String, number: Int, title: String, ext: Extension, size: String?, date: String?, width: CGFloat,
         wellHeight: CGFloat = 100, env: HUDDesignEnvironment) {
        self.wellHeight = wellHeight
        labelText = HUDTextLayer(title, style: HUDTextStyle(size: 11.5, weight: .medium, color: HUDColor.nearBlack))
        super.init(id: id, label: title, env: env)
        let bounds = CGRect(x: 0, y: 0, width: width, height: wellHeight + 24)
        HUDMotion.perform {
            layer.bounds = bounds
            well.frame = CGRect(x: 0, y: 0, width: width, height: wellHeight)
            well.showArtwork(HUDArtwork.plate([HUDColor.rgb(0x8A8885), HUDColor.rgb(0xBDBDBB), HUDColor.rgb(0xD9D9D8)], locations: [0, 0.55, 1],
                                              size: well.bounds.size, cut: 0, scale: env.renderScale), scale: env.renderScale)
            well.masksToBounds = true
            wellBrighten.frame = well.bounds; wellBrighten.backgroundColor = HUDColor.white(0.12); wellBrighten.opacity = 0
            let codeBar = HUDLayer.plain(); codeBar.frame = CGRect(x: 0, y: 0, width: 34, height: 4); codeBar.backgroundColor = HUDColor.counterBackground
            let code = HUDTextLayer("#" + HUDFormat.pad2(number), style: HUDTextStyle(size: 6, weight: .bold, color: HUDColor.rgb(0x222222)))
            code.lineFrame = CGRect(x: 38, y: -1, width: 30, height: 6)
            doc.bounds = CGRect(x: 0, y: 0, width: 40, height: 50)
            doc.position = CGPoint(x: width / 2, y: wellHeight / 2)
            let page = HUDLayer.shape(); page.frame = doc.bounds
            let fold = CGMutablePath()
            fold.addLines(between: [.zero, CGPoint(x: 28, y: 0), CGPoint(x: 40, y: 11), CGPoint(x: 40, y: 50), CGPoint(x: 0, y: 50)]); fold.closeSubpath()
            page.path = fold; page.fillColor = HUDColor.rgb(0xF4F4F2)
            doc.shadowColor = HUDColor.black(1); doc.shadowOpacity = 0.25; doc.shadowRadius = 6; doc.shadowOffset = CGSize(width: 0, height: 6)
            doc.shadowPath = fold
            let lines = HUDLayer.shape(); lines.frame = CGRect(x: 6, y: 10, width: 24, height: 14)
            lines.path = { let p = CGMutablePath(); var y: CGFloat = 0; while y < 14 { p.addRect(CGRect(x: 0, y: y, width: 24, height: 1)); y += 4 }; return p }()
            lines.fillColor = HUDColor.rgb(0x9B9B9B)
            let extText = HUDTextLayer(ext.rawValue.uppercased(), style: HUDTextStyle(size: 8, weight: .black, color: ext.color, tracking: 0.06), alignment: .center)
            extText.lineFrame = CGRect(x: 0, y: 36, width: 40, height: 8)
            [page, lines, extText].forEach(doc.addSublayer)
            doc.setValue(-6 * CGFloat.pi / 180, forKeyPath: "transform.rotation.z")
            [wellBrighten, codeBar, code, doc].forEach(well.addSublayer)
            if let size {
                let tag = HUDTextLayer(size, style: HUDTextStyle(size: 8.5, weight: .bold, color: HUDColor.textOnDark))
                let w = ceil(tag.preferredWidth) + 8
                let plate = HUDLayer.plain(); plate.frame = CGRect(x: width - 4 - w, y: wellHeight - 4 - 12, width: w, height: 12)
                plate.backgroundColor = HUDColor.rgb(0x141414, 0.75)
                tag.lineFrame = CGRect(x: 4, y: 1.75, width: w - 8 + 1, height: 8.5)
                plate.addSublayer(tag); well.addSublayer(plate)
            }
            if let date {
                let tag = HUDTextLayer(date, style: HUDTextStyle(size: 8.5, weight: .bold, color: HUDColor.rgb(0x222222)))
                tag.lineFrame = CGRect(x: 6, y: wellHeight - 4 - 9, width: width / 2, height: 8.5)
                well.addSublayer(tag)
            }
            strip.frame = CGRect(x: 0, y: wellHeight, width: width, height: 24)
            labelText.lineFrame = CGRect(x: 6, y: 6.25, width: width - 12, height: 11.5)
            strip.addSublayer(labelText)
            brackets.frame = bounds; brackets.fillColor = HUDColor.yellow; brackets.opacity = 0
            brackets.path = Self.bracketPath(bounds, inset: -5)
            [well, strip, brackets].forEach(layer.addSublayer)
        }
        renderSelection(animated: false)
        setTarget(bounds)
        env.applyScale(to: layer)
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

    func setSelected(_ selected: Bool, animated: Bool = true) {
        guard selected != isSelected else { return }
        isSelected = selected
        renderSelection(animated: animated)
    }

    private func renderSelection(animated: Bool) {
        HUDMotion.animate(brackets, "opacity", to: Float(isSelected ? 1 : 0), duration: animated ? 0.1 : 0, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(brackets, "path", to: Self.bracketPath(layer.bounds, inset: isSelected ? -4 : -5), duration: animated ? 0.16 : 0, env: env)
        HUDMotion.perform {
            strip.backgroundColor = isSelected ? HUDColor.counterBackground : HUDColor.rgb(0xC4C4C6)
            labelText.color = isSelected ? HUDColor.yellow : HUDColor.nearBlack
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
