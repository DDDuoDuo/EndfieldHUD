import AppKit
import QuartzCore
import CoreText

/// The same retained plates, detached frames and hover language as Notes and
/// the personal-card menus. No native preferences/popover window is created.
final class ProjectionToolbar: NotesRetainedMenu {
    var onAction: ((String) -> Void)?
    init() { super.init(size: CGSize(width: 336, height: 42), dark: true) }
    required init?(coder: NSCoder) { nil }
    /// Leave room for both the menu bar and camera housing without placing
    /// controls at the opposite screen edge on an ordinary external display.
    static func topInset(safeAreaTop: CGFloat, visibleTop: CGFloat) -> CGFloat {
        let safe = safeAreaTop.isFinite ? max(0, safeAreaTop) : 0
        let visible = visibleTop.isFinite ? max(0, visibleTop) : 0
        return max(64, max(safe, visible) + 24)
    }
    func update(_ model: ProjectionModel) {
        items = [
            Item(id: "color", title: "", rect: CGRect(x: 6, y: 6, width: 30, height: 30), color: model.color,
                 accessibilityTitle: L10n.text("Color", "颜色")),
            Item(id: "brush", title: String(Int(model.brushWidth)), rect: CGRect(x: 40, y: 6, width: 34, height: 30),
                 accessibilityTitle: L10n.text("Brush thickness", "画笔粗细")),
            Item(id: "eraser", title: "◇", rect: CGRect(x: 78, y: 6, width: 30, height: 30), selected: model.erasing,
                 accessibilityTitle: L10n.text("Eraser", "橡皮擦")),
            Item(id: "clear", title: L10n.text("Clear all", "清空"), rect: CGRect(x: 112, y: 6, width: 54, height: 30)),
            Item(id: "background", title: "▦", rect: CGRect(x: 170, y: 6, width: 30, height: 30), selected: model.backgroundEnabled,
                 accessibilityTitle: L10n.text("Background", "背景")),
            Item(id: "appearance", title: "◐", rect: CGRect(x: 204, y: 6, width: 30, height: 30),
                 accessibilityTitle: L10n.text("Background appearance", "背景外观")),
            Item(id: "media", title: "+", rect: CGRect(x: 238, y: 6, width: 30, height: 30),
                 accessibilityTitle: L10n.text("Image/Video", "图片/视频")),
            Item(id: "close", title: L10n.text("Return", "返回"), rect: CGRect(x: 272, y: 6, width: 58, height: 30),
                 accessibilityTitle: L10n.text("Return", "返回"))
        ]; paint()
    }
    override func paint() {
        super.paint()
        CATransaction.begin(); CATransaction.setDisableActions(true)
        // Keep the common menu colors and detached backing, with a softer,
        // compact face only for this floating toolbar.
        for surface in artwork.sublayers ?? [] { surface.cornerRadius = 10 }
        CATransaction.commit()
    }
    override func paintContent(on layer: CALayer) {
        for child in layer.sublayers ?? [] {
            if child is HUDControlHighlightLayer { child.removeFromSuperlayer() }
            else if child is CATextLayer { child.removeFromSuperlayer() }
            else { child.cornerRadius = 5 }
        }
        for item in items {
            if item.color == nil {
                let label = CALayer(); label.name = "projection.toolbar.label." + item.id; label.frame = item.rect
                label.contents = Self.centeredGlyphs(item.title, size: item.rect.size, color: ink)
                label.contentsScale = 2; layer.addSublayer(label)
            }
            if item.enabled { HUDControlHighlightLayer.add(to: layer, rect: item.rect, shape: .rounded, framed: false) }
        }
    }
    /// Center actual ink bounds, including fallback CJK and symbol glyphs. A
    /// CATextLayer's taller line box otherwise leaves visible space below ink.
    private static func centeredGlyphs(_ text: String, size: CGSize, color: NSColor) -> CGImage? {
        let scale: CGFloat = 2
        guard let context = CGContext(data: nil, width: Int(size.width * scale), height: Int(size.height * scale), bitsPerComponent: 8,
            bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        let line = CTLineCreateWithAttributedString(NSAttributedString(string: text, attributes: [
            .font: NSFont.systemFont(ofSize: 10, weight: .semibold), .foregroundColor: color]))
        let ink = CTLineGetBoundsWithOptions(line, .useGlyphPathBounds)
        context.scaleBy(x: scale, y: scale)
        context.textPosition = CGPoint(x: (size.width - ink.width) / 2 - ink.minX, y: (size.height - ink.height) / 2 - ink.minY)
        CTLineDraw(line, context); return context.makeImage()
    }
    override func perform(_ id: String) { onAction?(id) }
}

/// A retained confirmation in the same menu plane; it never opens an alert.
final class ProjectionClearConfirmation: NotesRetainedMenu {
    var onConfirm: (() -> Void)?
    init() {
        super.init(size: CGSize(width: 260, height: 92), dark: true)
        items = [Item(id: "close", title: L10n.text("Cancel", "取消"), rect: CGRect(x: 10, y: 48, width: 116, height: 32),
                      accessibilityTitle: L10n.text("Cancel", "取消")),
                 Item(id: "confirm", title: L10n.text("Clear all", "清空"), rect: CGRect(x: 134, y: 48, width: 116, height: 32), selected: true)]
        paint()
    }
    required init?(coder: NSCoder) { nil }
    override func paintContent(on layer: CALayer) {
        text(L10n.text("Clear drawings and media?", "清空绘画和媒体？"), rect: CGRect(x: 12, y: 14, width: 236, height: 22), size: 12, parent: layer)
    }
    override func perform(_ id: String) { if id == "confirm" { onConfirm?() } else { super.perform(id) } }
}

final class ProjectionAdjustmentMenu: NotesRetainedMenu {
    struct Value { let id: String; let title: String; var value: Double; let range: ClosedRange<Double> }
    private(set) var values: [Value]
    var onValue: ((String, Double) -> Void)?
    private var draggingIndex: Int?
    init(values: [Value]) {
        self.values = values
        super.init(size: CGSize(width: 260, height: 40 + CGFloat(values.count) * 62), dark: true)
        refresh()
    }
    required init?(coder: NSCoder) { nil }
    func rail(_ index: Int) -> CGRect { CGRect(x: 42, y: 63 + CGFloat(index) * 62, width: 170, height: 20) }
    private func refresh() {
        items = [Item(id: "close", title: "×", rect: CGRect(x: 227, y: 7, width: 23, height: 23))]
        for (index, value) in values.enumerated() {
            let y = 56 + CGFloat(index) * 62
            items.append(Item(id: "minus:\(index)", title: "−", rect: CGRect(x: 8, y: y, width: 26, height: 30), accessibilityTitle: value.title + " −"))
            items.append(Item(id: "plus:\(index)", title: "+", rect: CGRect(x: 224, y: y, width: 26, height: 30), accessibilityTitle: value.title + " +"))
        }; paint()
    }
    override func paintContent(on layer: CALayer) {
        for (index, value) in values.enumerated() {
            text(value.title + "  " + String(Int(value.value.rounded())), rect: CGRect(x: 10, y: 35 + CGFloat(index) * 62, width: 236, height: 18), parent: layer)
            let rect = rail(index), fraction = (value.value - value.range.lowerBound) / (value.range.upperBound - value.range.lowerBound)
            let track = CALayer(); track.frame = CGRect(x: rect.minX, y: rect.midY, width: rect.width, height: 2)
            track.backgroundColor = NSColor.white.withAlphaComponent(0.20).cgColor; layer.addSublayer(track)
            let fill = CALayer(); fill.frame = CGRect(x: rect.minX, y: rect.midY, width: rect.width * fraction, height: 2)
            fill.backgroundColor = HUDRuntimeAppearance.accent.cgColor; layer.addSublayer(fill)
            let thumb = CALayer(); thumb.frame = CGRect(x: rect.minX + rect.width * fraction - 2.5, y: rect.midY - 3, width: 5, height: 8)
            thumb.backgroundColor = NSColor.white.cgColor; layer.addSublayer(thumb)
        }
    }
    override func mouseDown(with event: NSEvent) {
        guard let point = eventPoint(event) else { return }
        if let index = values.indices.first(where: { rail($0).insetBy(dx: -5, dy: -6).contains(point) }) {
            draggingIndex = index; update(index, at: point)
        } else { super.mouseDown(with: event) }
    }
    override func mouseDragged(with event: NSEvent) { if let index = draggingIndex, let point = eventPoint(event) { update(index, at: point) } }
    override func mouseUp(with event: NSEvent) { draggingIndex = nil }
    private func update(_ index: Int, at point: CGPoint) {
        let fraction = min(1, max(0, (point.x - rail(index).minX) / rail(index).width))
        setValue(values[index].range.lowerBound + fraction * (values[index].range.upperBound - values[index].range.lowerBound), index)
    }
    private func setValue(_ next: Double, _ index: Int) {
        let value = min(values[index].range.upperBound, max(values[index].range.lowerBound, next))
        guard values[index].value != value else { return }
        values[index].value = value; onValue?(values[index].id, value); refresh()
    }
    override func perform(_ id: String) {
        let pieces = id.split(separator: ":")
        if pieces.count == 2, let index = Int(pieces[1]), values.indices.contains(index) {
            setValue(values[index].value + (pieces[0] == "plus" ? 1 : -1), index)
        } else { super.perform(id) }
    }
}
