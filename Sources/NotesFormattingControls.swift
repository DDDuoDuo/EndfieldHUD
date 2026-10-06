import AppKit
import QuartzCore

enum NotesFormatChange {
    case font(String), size(Double), color(NSColor), bold, italic, underline, strikethrough
}

/// Only this transparent event/AX surface is native. The actual menu belongs to
/// the same retained, tilted HUD plane as its note, including during deployment.
class NotesRetainedMenu: NSView {
    struct Item {
        let id: String
        let title: String
        let rect: CGRect
        var enabled = true
        var selected = false
        var color: NSColor? = nil
        var accessibilityTitle: String? = nil
    }
    let artwork = CALayer()
    let contentSize: CGSize
    var hostToLocal: ((CGPoint) -> CGPoint?)?
    var projectLocal: ((CGRect) -> CGRect)?
    var onCancel: (() -> Void)?
    let dark: Bool
    var items: [Item] = []
    private var accessibilityButtons: [NSButton] = []
    private var focusedIndex = 0
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    var ink: NSColor { NSColor(white: dark ? 0.96 : 0.10, alpha: 1) }
    init(size: CGSize, dark: Bool) {
        self.dark = dark; contentSize = size
        super.init(frame: CGRect(origin: .zero, size: size))
        artwork.name = "notes.secondaryMenu"; artwork.zPosition = 2_000_000
        artwork.bounds = bounds; artwork.anchorPoint = .zero
        setAccessibilityRole(.group)
    }
    required init?(coder: NSCoder) { nil }
    override func draw(_ dirtyRect: NSRect) {}
    override func removeFromSuperview() { artwork.removeFromSuperlayer(); super.removeFromSuperview() }
    /// Stop native input immediately while the retained face finishes its
    /// finite disappearance on the HUD plane.
    func detachInputKeepingArtwork() { super.removeFromSuperview() }
    func localPoint(_ hostPoint: CGPoint) -> CGPoint? {
        if let hostToLocal { return hostToLocal(hostPoint) }
        return CGPoint(x: (hostPoint.x - frame.minX) * bounds.width / max(1, frame.width),
                                        y: (hostPoint.y - frame.minY) * bounds.height / max(1, frame.height))
    }
    override func hitTest(_ point: NSPoint) -> NSView? {
        guard !isHidden, let point = localPoint(point), bounds.contains(point) else { return nil }
        return self
    }
    func eventPoint(_ event: NSEvent) -> CGPoint? {
        guard let host = superview else { return nil }
        return localPoint(host.convert(event.locationInWindow, from: nil))
    }
    override func mouseDown(with event: NSEvent) {
        guard let point = eventPoint(event) else { return }
        activate(at: point)
    }
    func activate(at point: CGPoint) {
        guard let item = items.last(where: { $0.enabled && $0.rect.contains(point) }) else { return }
        perform(item.id)
    }
    func perform(_ id: String) { if id == "close" { onCancel?() } }
    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 { onCancel?(); return }
        let enabled = items.filter(\.enabled)
        guard !enabled.isEmpty else { return }
        if event.keyCode == 125 || event.keyCode == 124 { focusedIndex = min(enabled.count - 1, focusedIndex + 1) }
        else if event.keyCode == 126 || event.keyCode == 123 { focusedIndex = max(0, focusedIndex - 1) }
        else if event.keyCode == 36 || event.keyCode == 49 { perform(enabled[min(focusedIndex, enabled.count - 1)].id) }
        else { super.keyDown(with: event) }
    }
    func paint() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        artwork.sublayers?.forEach { $0.removeFromSuperlayer() }
        let back = CALayer(); back.frame = bounds.offsetBy(dx: -3, dy: 4)
        back.backgroundColor = NSColor.black.withAlphaComponent(0.30).cgColor; artwork.addSublayer(back)
        let face = CALayer(); face.frame = bounds
        face.backgroundColor = NSColor(white: dark ? 0.08 : 0.92, alpha: 0.98).cgColor
        face.borderWidth = 0.7; face.borderColor = HUDRuntimeAppearance.accent.withAlphaComponent(0.7).cgColor
        artwork.addSublayer(face)
        for item in items {
            let plate = CALayer(); plate.frame = item.rect
            plate.backgroundColor = item.color?.cgColor ?? (item.selected ? HUDRuntimeAppearance.accent.withAlphaComponent(0.22) : ink.withAlphaComponent(0.07)).cgColor
            plate.opacity = item.enabled || item.color != nil ? 1 : 0.4; face.addSublayer(plate)
            if item.enabled { HUDControlHighlightLayer.add(to: face, rect: item.rect, shape: .cutCorner, framed: true) }
            if item.color == nil { text(item.title, rect: item.rect.insetBy(dx: 6, dy: 5), size: 10, parent: face) }
        }
        paintContent(on: face)
        CATransaction.commit()
        rebuildAccessibility()
        if let host = superview { HUDControlHighlightLayer.requestRefresh(on: host) }
    }
    func paintContent(on layer: CALayer) {}
    func text(_ value: String, rect: CGRect, size: CGFloat = 10, parent: CALayer, color: NSColor? = nil) {
        let label = CATextLayer(); label.frame = rect; label.string = value
        label.font = NSFont.systemFont(ofSize: size, weight: .semibold); label.fontSize = size
        label.foregroundColor = (color ?? ink).cgColor; label.contentsScale = 2
        label.truncationMode = .end; parent.addSublayer(label)
    }
    private func rebuildAccessibility() {
        accessibilityButtons.forEach { $0.removeFromSuperview() }; accessibilityButtons = []
        for (index, item) in items.enumerated() {
            let button = NotesMenuAXButton(frame: .zero); button.title = ""; button.isBordered = false
            button.tag = index; button.target = self; button.action = #selector(accessibilityActivate(_:))
            button.setAccessibilityLabel(item.accessibilityTitle ?? (item.id == "close" ? L10n.text("Close", "关闭") : item.title))
            button.isEnabled = item.enabled
            addSubview(button); accessibilityButtons.append(button)
        }
        layoutAccessibility()
    }
    func layoutAccessibility() {
        for (item, button) in zip(items, accessibilityButtons) {
            let projected = projectLocal?(item.rect) ?? item.rect.offsetBy(dx: frame.minX, dy: frame.minY)
            button.frame = convert(projected, from: superview)
        }
    }
    @objc private func accessibilityActivate(_ button: NSButton) {
        guard items.indices.contains(button.tag), items[button.tag].enabled else { return }
        perform(items[button.tag].id)
    }
}
private final class NotesMenuAXButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}

final class NotesMediaSourceChooser: NotesRetainedMenu {
    var onChoose: ((Bool) -> Void)?
    init(dark: Bool) {
        super.init(size: CGSize(width: 208, height: 75), dark: dark)
        items = [Item(id: "finder", title: L10n.text("Choose in Finder", "从 Finder 选择"), rect: CGRect(x: 8, y: 8, width: 166, height: 25)),
                 Item(id: "shelf", title: L10n.text("Choose from Shelf", "从暂存架选择"), rect: CGRect(x: 8, y: 40, width: 166, height: 25)),
                 Item(id: "close", title: "×", rect: CGRect(x: 178, y: 8, width: 23, height: 23))]
        paint()
    }
    required init?(coder: NSCoder) { nil }
    override func perform(_ id: String) { if id == "finder" || id == "shelf" { onChoose?(id == "shelf") } else { super.perform(id) } }
}

final class NotesFormattingControls: NotesRetainedMenu {
    var onChange: ((NotesFormatChange) -> Void)?
    var onClose: (() -> Void)? { get { onCancel } set { onCancel = newValue } }
    private let kind: String
    private var values: [String] = []
    private var firstRow = 0
    private var scrollRemainder: CGFloat = 0
    private static let fontFamilies = NSFontManager.shared.availableFontFamilies.sorted()
    private var traits: [Bool]
    private(set) var selectedValue = ""
    private var currentColor: NSColor
    private let wheel = NotesColorWheelView(frame: CGRect(x: 8, y: 8, width: 166, height: 166))
    private var wheelDrag = false
    init(kind: String, dark: Bool, style: NotesTextStyle) {
        self.kind = kind; traits = [style.bold, style.italic, style.underline, style.strikethrough]
        currentColor = style.color?.color ?? (dark ? .white : .black)
        super.init(size: CGSize(width: 242, height: kind == "special" ? 48 : 184), dark: dark)
        if kind == "font" {
            values = Self.fontFamilies
            selectedValue = (style.attributes(defaultColor: .black)[.font] as? NSFont)?.familyName ?? ""
        } else if kind == "size" {
            values = Set([8, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64, 72, 96, 144, Int(style.fontSize.rounded())]).sorted().map(String.init)
            selectedValue = String(Int(style.fontSize.rounded()))
        }
        firstRow = min(max(0, values.count - 7), values.firstIndex(of: selectedValue) ?? 0)
        wheel.onColor = { [weak self] color in
            guard let self else { return }
            self.currentColor = color; self.onChange?(.color(color)); self.refresh()
        }
        if kind == "color" {
            wheel.retainedArtworkOnly = true; addSubview(wheel)
            wheel.setColor(currentColor)
        }
        refresh()
    }
    required init?(coder: NSCoder) { nil }
    override func layoutAccessibility() {
        super.layoutAccessibility()
        wheel.projectedFrame = { [weak self] in
            guard let self, let host = self.superview, let window = self.window else { return .zero }
            let frame = self.projectLocal?(self.wheel.frame) ?? self.wheel.frame.offsetBy(dx: self.frame.minX, dy: self.frame.minY)
            return window.convertToScreen(host.convert(frame, to: nil))
        }
    }
    private func refresh() {
        items = [Item(id: "close", title: "×", rect: CGRect(x: 211, y: 8, width: 23, height: 23))]
        if kind == "font" || kind == "size" {
            for row in firstRow..<min(values.count, firstRow + 7) {
                items.append(Item(id: "value:\(row)", title: values[row], rect: CGRect(x: 8, y: 8 + (row - firstRow) * 24, width: 195, height: 22), selected: values[row] == selectedValue))
            }
        } else if kind == "special" {
            for (index, title) in ["B", "I", "U", "S"].enumerated() {
                items.append(Item(id: "trait:\(index)", title: title, rect: CGRect(x: 8 + index * 41, y: 8, width: 32, height: 32), selected: traits[index], accessibilityTitle: [L10n.text("Bold", "粗体"), L10n.text("Italic", "斜体"), L10n.text("Underline", "下划线"), L10n.text("Strikethrough", "删除线")][index]))
            }
        } else {
            items.append(Item(id: "swatch", title: L10n.text("Current color", "当前颜色"), rect: CGRect(x: 187, y: 80, width: 32, height: 32), enabled: false, color: currentColor))
        }
        paint()
    }
    override func paintContent(on layer: CALayer) {
        guard kind == "color" else { return }
        let image = CALayer(); image.frame = wheel.frame; image.contents = NotesColorWheelView.wheel; layer.addSublayer(image)
        let marker = CAShapeLayer(); marker.path = CGPath(ellipseIn: CGRect(x: 8 + wheel.selected.x * 166 - 4, y: 8 + wheel.selected.y * 166 - 4, width: 8, height: 8), transform: nil)
        marker.fillColor = nil; marker.strokeColor = NSColor.black.cgColor; marker.lineWidth = 1.5; layer.addSublayer(marker)
    }
    override func activate(at point: CGPoint) {
        if kind == "color", wheel.frame.contains(point) {
            wheelDrag = true; wheel.choose(CGPoint(x: point.x - wheel.frame.minX, y: point.y - wheel.frame.minY)); return
        }
        super.activate(at: point)
    }
    override func mouseDragged(with event: NSEvent) {
        guard wheelDrag, let point = eventPoint(event) else { return }
        wheel.choose(CGPoint(x: point.x - wheel.frame.minX, y: point.y - wheel.frame.minY))
    }
    override func mouseUp(with event: NSEvent) { wheelDrag = false }
    override func scrollWheel(with event: NSEvent) {
        guard !values.isEmpty, event.scrollingDeltaY != 0 else { return }
        scrollRemainder -= event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12)
        let rows = Int(scrollRemainder / 18)
        guard rows != 0 else { return }
        scrollRemainder -= CGFloat(rows) * 18
        let next = min(max(0, values.count - 7), max(0, firstRow + rows))
        if next != firstRow { firstRow = next; refresh() }
    }
    override func perform(_ id: String) {
        if id.hasPrefix("value:"), let row = Int(id.dropFirst(6)), values.indices.contains(row) {
            selectedValue = values[row]
            if kind == "font" { onChange?(.font(selectedValue)) }
            else if let value = Double(selectedValue) { onChange?(.size(value)) }
            refresh()
        } else if id.hasPrefix("trait:"), let index = Int(id.dropFirst(6)), traits.indices.contains(index) {
            traits[index].toggle(); onChange?([NotesFormatChange.bold, .italic, .underline, .strikethrough][index]); refresh()
        } else { super.perform(id) }
    }
}

/// One small raster shared by every open color chooser; no timer or display loop.
final class NotesColorWheelView: NSView {
    var onColor: ((NSColor) -> Void)?
    var retainedArtworkOnly = false
    var projectedFrame: (() -> CGRect)?
    private(set) var selected = CGPoint(x: 0.5, y: 0.5)
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    static let wheel: NSImage = {
        let size = 192, bytes = size * size * 4
        var pixels = [UInt8](repeating: 0, count: bytes)
        for y in 0..<size { for x in 0..<size {
            let dx = (Double(x) + 0.5) / Double(size) * 2 - 1
            let dy = (Double(y) + 0.5) / Double(size) * 2 - 1
            let saturation = hypot(dx, dy)
            guard saturation <= 1 else { continue }
            let hue = (atan2(dy, dx) / (2 * .pi) + 1).truncatingRemainder(dividingBy: 1)
            let color = NSColor(calibratedHue: hue, saturation: saturation, brightness: 1, alpha: 1).usingColorSpace(.sRGB)!
            let index = (y * size + x) * 4
            pixels[index] = UInt8(color.redComponent * 255); pixels[index + 1] = UInt8(color.greenComponent * 255)
            pixels[index + 2] = UInt8(color.blueComponent * 255); pixels[index + 3] = 255
        } }
        let data = Data(pixels)
        guard let provider = CGDataProvider(data: data as CFData), let image = CGImage(width: size, height: size,
            bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: size * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue), provider: provider,
            decode: nil, shouldInterpolate: true, intent: .defaultIntent) else { return NSImage() }
        return NSImage(cgImage: image, size: NSSize(width: size, height: size))
    }()
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        setAccessibilityRole(.slider); setAccessibilityLabel(L10n.text("Color wheel", "颜色轮盘"))
        setAccessibilityHelp(L10n.text("Use arrow keys to adjust hue and saturation.", "使用方向键调整色相与饱和度。"))
    }
    required init?(coder: NSCoder) { nil }
    override func draw(_ dirtyRect: NSRect) {
        guard !retainedArtworkOnly else { return }
        Self.wheel.draw(in: bounds)
        NSColor.black.setStroke()
        let circle = NSBezierPath(ovalIn: CGRect(x: selected.x * bounds.width - 4, y: selected.y * bounds.height - 4, width: 8, height: 8))
        circle.lineWidth = 1.5; circle.stroke()
    }
    override func hitTest(_ point: NSPoint) -> NSView? { retainedArtworkOnly ? nil : super.hitTest(point) }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
    override func accessibilityPerformIncrement() -> Bool {
        choose(CGPoint(x: selected.x * bounds.width + 4, y: selected.y * bounds.height)); return true
    }
    override func accessibilityPerformDecrement() -> Bool {
        choose(CGPoint(x: selected.x * bounds.width - 4, y: selected.y * bounds.height)); return true
    }
    func setColor(_ color: NSColor) {
        guard let color = color.usingColorSpace(.deviceRGB) else { return }
        let angle = color.hueComponent * 2 * CGFloat.pi
        selected = CGPoint(x: (cos(angle) * color.saturationComponent + 1) / 2,
                           y: (sin(angle) * color.saturationComponent + 1) / 2)
    }
    override func mouseDown(with event: NSEvent) { window?.makeFirstResponder(self); choose(convert(event.locationInWindow, from: nil)) }
    override func mouseDragged(with event: NSEvent) { choose(convert(event.locationInWindow, from: nil)) }
    override func keyDown(with event: NSEvent) {
        var point = CGPoint(x: selected.x * bounds.width, y: selected.y * bounds.height)
        switch event.keyCode { case 123: point.x -= 4; case 124: point.x += 4; case 125: point.y += 4; case 126: point.y -= 4; default: super.keyDown(with: event); return }
        choose(point)
    }
    func choose(_ point: CGPoint) {
        let dx = point.x / bounds.width * 2 - 1, dy = point.y / bounds.height * 2 - 1
        let radius = max(1, hypot(dx, dy))
        selected = CGPoint(x: (dx / radius + 1) / 2, y: (dy / radius + 1) / 2)
        let hue = (atan2(dy, dx) / (2 * .pi) + 1).truncatingRemainder(dividingBy: 1)
        let saturation = min(1, hypot(dx, dy))
        onColor?(NSColor(calibratedHue: hue, saturation: saturation, brightness: 1, alpha: 1))
        setAccessibilityValue(String(format: "%.0f°, %.0f%%", hue * 360, saturation * 100)); needsDisplay = true
    }
}

/// One formatting engine for Notes and Archive: preserve unrelated runs,
/// selection, native undo and the separate style for future typing.
enum NotesFormattingEditor {
    static func style(in editor: NSTextView, scale: CGFloat = 1, defaultColor: NSColor) -> NotesTextStyle {
        guard let storage = editor.textStorage else { return NotesTextStyle() }
        let selection = editor.selectedRange()
        let attributes = selection.length == 0 || storage.length == 0 ? editor.typingAttributes
            : storage.attributes(at: min(storage.length - 1, selection.location), effectiveRange: nil)
        var style = NotesRichText.capture(NSAttributedString(string: "x", attributes: attributes),
            scale: scale, defaultColor: defaultColor).runs.first?.style ?? NotesTextStyle()
        if selection.length > 0, NSMaxRange(selection) <= storage.length {
            var bold = true, italic = true, underline = true, strike = true
            storage.enumerateAttributes(in: selection) { attrs, _, _ in
                let font = attrs[.font] as? NSFont ?? .systemFont(ofSize: 12)
                let traits = NSFontManager.shared.traits(of: font)
                bold = bold && traits.contains(.boldFontMask); italic = italic && traits.contains(.italicFontMask)
                underline = underline && ((attrs[.underlineStyle] as? NSNumber)?.intValue ?? 0) != 0
                strike = strike && ((attrs[.strikethroughStyle] as? NSNumber)?.intValue ?? 0) != 0
            }
            style.bold = bold; style.italic = italic; style.underline = underline; style.strikethrough = strike
        }
        return style
    }
    static func apply(_ change: NotesFormatChange, to editor: NSTextView, scale: CGFloat = 1) {
        guard let storage = editor.textStorage else { return }
        let selected = editor.selectedRange()
        guard selected.location != NSNotFound, selected.location <= storage.length,
              selected.length <= storage.length - selected.location else { return }
        func enabled(in attributes: [NSAttributedString.Key: Any]) -> Bool {
            let font = attributes[.font] as? NSFont ?? .systemFont(ofSize: 12 * scale)
            switch change {
            case .bold: return NSFontManager.shared.traits(of: font).contains(.boldFontMask)
            case .italic: return NSFontManager.shared.traits(of: font).contains(.italicFontMask)
            case .underline: return ((attributes[.underlineStyle] as? NSNumber)?.intValue ?? 0) != 0
            case .strikethrough: return ((attributes[.strikethroughStyle] as? NSNumber)?.intValue ?? 0) != 0
            default: return false
            }
        }
        var allEnabled = selected.length > 0
        if selected.length == 0 { allEnabled = enabled(in: editor.typingAttributes) }
        else { storage.enumerateAttributes(in: selected) { attributes, _, _ in allEnabled = allEnabled && enabled(in: attributes) } }
        let applyTrait = !allEnabled
        func changed(_ attributes: [NSAttributedString.Key: Any]) -> [NSAttributedString.Key: Any] {
            var values = attributes
            let font = values[.font] as? NSFont ?? .systemFont(ofSize: 12 * scale)
            let manager = NSFontManager.shared
            switch change {
            case .font(let family):
                values[.font] = manager.font(withFamily: family, traits: manager.traits(of: font), weight: 5, size: font.pointSize) ?? font
            case .size(let size): values[.font] = manager.convert(font, toSize: CGFloat(min(144, max(6, size))) * scale)
            case .color(let color): values[.foregroundColor] = color
            case .bold:
                values[.font] = NotesTextStyle.changingTrait(.boldFontMask, enabled: applyTrait, on: font)
            case .italic:
                values[.font] = NotesTextStyle.changingTrait(.italicFontMask, enabled: applyTrait, on: font)
            case .underline:
                values[.underlineStyle] = applyTrait ? 1 : 0
            case .strikethrough:
                values[.strikethroughStyle] = applyTrait ? 1 : 0
            }
            let paragraph = NSMutableParagraphStyle(); paragraph.lineSpacing = scale
            values[.paragraphStyle] = paragraph
            return values
        }
        guard selected.length > 0 else { editor.typingAttributes = changed(editor.typingAttributes); return }
        guard editor.shouldChangeText(in: selected, replacementString: nil) else { return }
        let previous = storage.attributedSubstring(from: selected)
        editor.undoManager?.registerUndo(withTarget: editor) { target in
            restore(previous, in: target, range: selected)
        }
        var updates: [(NSRange, [NSAttributedString.Key: Any])] = []
        storage.enumerateAttributes(in: selected) { attributes, range, _ in updates.append((range, changed(attributes))) }
        storage.beginEditing()
        for (range, attributes) in updates { storage.setAttributes(attributes, range: range) }
        storage.endEditing(); editor.didChangeText(); editor.setSelectedRange(selected)

    }
    private static func restore(_ attributes: NSAttributedString, in editor: NSTextView, range: NSRange) {
        guard let storage = editor.textStorage, range.location <= storage.length,
              range.length <= storage.length - range.location else { return }
        let inverse = storage.attributedSubstring(from: range)
        editor.undoManager?.registerUndo(withTarget: editor) { target in restore(inverse, in: target, range: range) }
        storage.replaceCharacters(in: range, with: attributes)
        editor.setSelectedRange(range); editor.didChangeText()
    }

}
