import AppKit
import QuartzCore

enum NotesFormatChange {
    case font(String), size(Double), color(NSColor), bold, italic, underline, strikethrough

    /// A toggle switches on unless every run already has it (other changes always apply).
    func switchesOn(over styles: [NotesTextStyle]) -> Bool {
        switch self {
        case .bold: return !styles.allSatisfy(\.bold)
        case .italic: return !styles.allSatisfy(\.italic)
        case .underline: return !styles.allSatisfy(\.underline)
        case .strikethrough: return !styles.allSatisfy(\.strikethrough)
        default: return true
        }
    }

    /// ⌘B / ⌘I / ⌘U in a rich text editor (the HUD has no Format menu).
    init?(shortcut event: NSEvent) {
        guard event.type == .keyDown, event.modifierFlags.intersection([.command, .shift, .option, .control]) == [.command] else { return nil }
        switch event.charactersIgnoringModifiers?.lowercased() {
        case "b": self = .bold
        case "i": self = .italic
        case "u": self = .underline
        default: return nil
        }
    }
}

/// The centre note's whole-text style of the first redesign build (family,
/// size, weight, italic, underline, colour). Notes now keeps per-run
/// formatting; this remains the base of text that has no runs.
struct NotesNoteStyle: Codable, Equatable {
    /// The HUD face; HarmonyOS Sans with the region's Noto cascade.
    static let defaultFamily = "HarmonyOS Sans"
    /// A family name; nil is `defaultFamily`.
    var family: String? = nil
    var size: Double = 14
    var bold = false
    var italic = false
    var underline = false
    /// RRGGBB.
    var color = "EEEEEE"

    init() {}
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        family = try c.decodeIfPresent(String.self, forKey: .family)
        size = try c.decodeIfPresent(Double.self, forKey: .size) ?? 14
        bold = try c.decodeIfPresent(Bool.self, forKey: .bold) ?? false
        italic = try c.decodeIfPresent(Bool.self, forKey: .italic) ?? false
        underline = try c.decodeIfPresent(Bool.self, forKey: .underline) ?? false
        color = try c.decodeIfPresent(String.self, forKey: .color) ?? "EEEEEE"
    }

    var rgb: UInt32? { color.count == 6 ? UInt32(color, radix: 16) : nil }
    var isValid: Bool {
        size.isFinite && (6...144).contains(size) && rgb != nil && (family.map { !$0.isEmpty && $0.utf8.count <= 256 } ?? true)
    }

    /// The same style as a rich-text run style (ArchiveRichText's model:
    /// no font name is the HUD face, no colour the #EEE ink).
    var textStyle: NotesTextStyle {
        var style = NotesTextStyle(fontSize: min(144, max(6, size)), bold: bold, italic: italic, underline: underline)
        if let family, family != Self.defaultFamily {
            style.fontName = NSFontManager.shared.font(withFamily: family, traits: [], weight: 5, size: 12)?.fontName
        }
        if let rgb, rgb != 0xEEEEEE { style.color = NotesRGBA(NotesFormatBar.color(rgb)) }
        return style
    }
}

/// What the format bar shows. The bar never reads text: its owner passes the
/// style at the caret or selection (`show`) and applies what it reports.
struct NotesFormatState: Equatable {
    var family = NotesNoteStyle.defaultFamily
    var size: Double = 14
    var bold = false
    var italic = false
    var underline = false
    var strikethrough = false
    /// 0xRRGGBB.
    var color: UInt32 = 0xEEEEEE
}

/// The shared format bar (spec §3.6 Notes, §3.9.3 Archive): font and size
/// dropdowns whose menus get the scroll bar past seven entries, B / I / U / S
/// toggles, five colour swatches and the custom colour wheel. It reports
/// `NotesFormatChange`s and never edits text itself; menus and the colour
/// pop-over open through the owning canvas, so a focused editor keeps its
/// selection when the owner leaves presses on `controls` to the bar.
final class NotesFormatBar {
    /// The demo's 17 families that this Mac can draw (Segoe UI, Consolas and
    /// Garamond ship with Windows only), the bundled faces first.
    static let families: [String] = {
        let installed = Set(NSFontManager.shared.availableFontFamilies)
        let bundled = Set(HUDFontRegistry.families.map(\.name))
        return [NotesNoteStyle.defaultFamily, "Noto Sans SC", "Noto Sans JP", "Noto Sans KR", "Segoe UI", "Arial", "Georgia",
                "Times New Roman", "Courier New", "Consolas", "Verdana", "Tahoma", "Trebuchet MS", "Palatino", "Garamond",
                "Helvetica", "Impact"].filter { $0 == NotesNoteStyle.defaultFamily || bundled.contains($0) || installed.contains($0) }
    }()
    static let sizes: [Double] = [9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28, 32, 36, 48, 64, 72]
    static let height: CGFloat = 26
    /// The five swatches: light ink, the theme colour, blue, red, green.
    static var colors: [UInt32] { [0xEEEEEE, hex(HUDColor.yellow), 0x22A6F2, 0xE5383B, 0x8CE03C] }

    let layer = HUDLayer.plain("notes.formatBar")
    private(set) var state: NotesFormatState
    private(set) var controls: [HUDControl] = []
    var onFormat: ((NotesFormatChange) -> Void)?
    private weak var canvas: HUDDesignCanvas?
    private let id: String
    private let families: [String]
    private let font: HUDDropdown
    private let size: HUDDropdown
    private var toggles: [(NotesFormatToggle, KeyPath<NotesFormatState, Bool>)] = []
    private var swatches: [(HUDSwatch, UInt32)] = []
    private let custom: HUDSwatch
    /// The owner's trailing controls (they do not edit the selection).
    private let trailingCount: Int

    /// - families: the font menu (Archive lists every installed family).
    /// - trailing: the owner's own controls after the swatches (Archive's Media).
    init(id: String, state: NotesFormatState, canvas: HUDDesignCanvas, env: HUDDesignEnvironment,
         families: [String] = NotesFormatBar.families, fontWidth: CGFloat = 150, sizeWidth: CGFloat = 72, trailing: [HUDControl] = []) {
        self.id = id; self.state = state; self.canvas = canvas; self.families = families; trailingCount = trailing.count
        font = HUDDropdown(id: id + ".font", label: L10n.text("Font", "字体"), value: state.family, size: .small, width: fontWidth, env: env)
        size = HUDDropdown(id: id + ".size", label: L10n.text("Font size", "字号"), value: Self.sizeTitle(state.size), size: .small,
                           width: sizeWidth, env: env)
        custom = HUDSwatch(id: id + ".color.custom", label: L10n.text("Custom color", "自定义颜色"), color: nil, size: 16, env: env)
        font.onActivate = { [weak self] in self?.openFontMenu() }
        size.onActivate = { [weak self] in self?.openSizeMenu() }
        custom.onActivate = { [weak self] in self?.openColorWheel() }
        var x: CGFloat = 0
        func place(_ control: HUDControl) {
            let bounds = control.layer.bounds
            HUDMotion.perform { control.layer.frame = CGRect(x: x, y: (Self.height - bounds.height) / 2, width: bounds.width, height: bounds.height) }
            layer.addSublayer(control.layer)
            x += bounds.width + 6
            controls.append(control)
        }
        place(font); place(size)
        let traits: [(NotesFormatToggle.Glyph, String, String, KeyPath<NotesFormatState, Bool>, NotesFormatChange)] = [
            (.bold, "bold", L10n.text("Bold", "粗体"), \.bold, .bold), (.italic, "italic", L10n.text("Italic", "斜体"), \.italic, .italic),
            (.underline, "underline", L10n.text("Underline", "下划线"), \.underline, .underline),
            (.strikethrough, "strikethrough", L10n.text("Strikethrough", "删除线"), \.strikethrough, .strikethrough)]
        for (glyph, name, label, key, change) in traits {
            let toggle = NotesFormatToggle(id: id + "." + name, label: label, glyph: glyph, isOn: state[keyPath: key], env: env)
            toggle.onActivate = { [weak self] in self?.onFormat?(change) }
            toggles.append((toggle, key))
            place(toggle)
        }
        for hex in Self.colors {
            let code = String(format: "%06X", hex)
            let swatch = HUDSwatch(id: id + ".color." + code, label: L10n.text("Text color", "文字颜色") + " #" + code,
                                   color: HUDColor.rgb(hex), size: 16, env: env)
            swatch.onActivate = { [weak self] in self?.onFormat?(.color(Self.color(hex))) }
            swatches.append((swatch, hex))
            place(swatch)
        }
        place(custom)
        trailing.forEach(place)
        HUDMotion.perform { layer.bounds = CGRect(x: 0, y: 0, width: x - 6, height: Self.height) }
        renderColor()
        env.applyScale(to: layer)
    }

    /// Updates every control in place (a caret move or selection change).
    func show(_ next: NotesFormatState) {
        guard next != state else { return }
        state = next
        font.setValue(next.family); size.setValue(Self.sizeTitle(next.size))
        for (toggle, key) in toggles { toggle.setOn(next[keyPath: key]) }
        renderColor()
    }

    /// A format control (not a trailing one) is under this point of `root`:
    /// a press there edits the selection, so the editor keeps its focus.
    func formatsSelection(at point: CGPoint, in root: CALayer) -> Bool {
        controls.dropLast(trailingCount).contains { $0.layer.isVisible(in: root) && $0.rect(in: root).contains(point) }
    }

    static func sizeTitle(_ size: Double) -> String { String(Int(size.rounded())) }
    static func color(_ hex: UInt32) -> NSColor { NSColor(cgColor: HUDColor.rgb(hex)) ?? .white }
    /// 0xRRGGBB of a colour (sRGB, rounded).
    static func hex(_ color: CGColor) -> UInt32 {
        guard let value = NSColor(cgColor: color)?.usingColorSpace(.sRGB) else { return 0xEEEEEE }
        func channel(_ value: CGFloat) -> UInt32 { UInt32((min(1, max(0, value)) * 255).rounded()) }
        return channel(value.redComponent) << 16 | channel(value.greenComponent) << 8 | channel(value.blueComponent)
    }

    private func renderColor() {
        var matched = false
        for (swatch, hex) in swatches { swatch.isSelected = hex == state.color; matched = matched || hex == state.color }
        custom.isSelected = !matched
    }

    private func openFontMenu() {
        let items = families.map { HUDMenu.Item(id: $0, title: $0, selected: $0 == state.family) }
        toggleMenu(font, items: items, minimumWidth: max(font.layer.bounds.width, 200)) { .font($0) }
    }

    private func openSizeMenu() {
        let items = Self.sizes.map { HUDMenu.Item(id: Self.sizeTitle($0), title: Self.sizeTitle($0), selected: $0 == state.size) }
        toggleMenu(size, items: items, minimumWidth: nil) { Double($0).map { .size($0) } }
    }

    private func toggleMenu(_ anchor: HUDDropdown, items: [HUDMenu.Item], minimumWidth: CGFloat?, change: @escaping (String) -> NotesFormatChange?) {
        guard let canvas else { return }
        if canvas.isMenuOpen { canvas.closeMenu(); return }
        canvas.openMenu(id: anchor.id, items: items, anchor: anchor, minimumWidth: minimumWidth) { [weak self] item in
            if let value = change(item.id) { self?.onFormat?(value) }
        }
    }

    /// The custom colour pop-over: the wheel picks hue and saturation, the
    /// slider brightness; the colour applies when a press or drag ends.
    private func openColorWheel() {
        guard let canvas else { return }
        if canvas.isPopoverOpen { canvas.closePopover(); return }
        let picker = Self.colorPicker(id: id, title: L10n.text("TEXT COLOR", "文字颜色"), color: state.color, env: custom.env,
                                      bubbleHost: canvas.top) { [weak self] hex in
            guard let self, hex != self.state.color else { return }
            self.onFormat?(.color(Self.color(hex)))
        }
        canvas.openPopover(picker.popover, anchor: custom, controls: picker.controls)
    }

    /// The shared custom-colour picker (this bar, Projection): a pop-over with
    /// the colour wheel (`<id>.wheel`) and a brightness slider
    /// (`<id>.brightness`). `onColor` gets 0xRRGGBB once per release.
    static func colorPicker(id: String, title: String, color: UInt32, env: HUDDesignEnvironment, bubbleHost: CALayer?,
                            onColor: @escaping (UInt32) -> Void) -> (popover: HUDPopover, controls: [HUDControl]) {
        let side: CGFloat = 156
        var hue: CGFloat = 0, saturation: CGFloat = 0, brightness: CGFloat = 1, alpha: CGFloat = 1
        Self.color(color).usingColorSpace(.deviceRGB)?.getHue(&hue, saturation: &saturation, brightness: &brightness, alpha: &alpha)
        let popover = HUDPopover(title: title, width: side + 24, contentHeight: side + 8 + HUDSlider.height, env: env)
        let wheel = NotesColorWheelControl(id: id + ".wheel", label: L10n.text("Color wheel", "颜色轮盘"), side: side,
                                           hue: hue, saturation: saturation, brightness: brightness, env: env)
        let light = HUDSlider(id: id + ".brightness", label: L10n.text("Brightness", "亮度"), value: Double(brightness * 100),
                              range: HUDSlider.Range(minimum: 10, maximum: 100, step: 1), format: .percent, width: side,
                              dark: true, showsTicks: false, env: env)
        light.bubbleHost = bubbleHost
        let apply = { [weak wheel] in if let wheel { onColor(Self.hex(wheel.color.cgColor)) } }
        wheel.onPick = { final in if final { apply() } }
        light.onChange = { [weak wheel] value, final in
            wheel?.setBrightness(CGFloat(value / 100))
            if final { apply() }
        }
        HUDMotion.perform {
            wheel.layer.frame.origin = .zero
            light.layer.frame.origin = CGPoint(x: 0, y: side + 8)
            popover.content.addSublayer(wheel.layer); popover.content.addSublayer(light.layer)
        }
        env.applyScale(to: popover.layer)
        return (popover, [wheel, light])
    }
}

/// A B / I / U / S toggle of the format bar (`.fmt`): 28 × 26, #262626 with
/// a #3A3A3A ring (#888 on hover) and a #DDD glyph; on, it is the theme
/// colour with a #111 glyph; a press pushes it in. The desktop notes use the
/// same plate at their own size for every card button with a text glyph
/// (`label`), the TODO check (`check`: the tick shows only when on) and the
/// colour button (`chip`, see `setChip`).
final class NotesFormatToggle: HUDControl {
    enum Glyph: Equatable { case bold, italic, underline, strikethrough, label(String), check, chip }
    private(set) var isOn: Bool
    private let glyph: Glyph
    private let text: HUDTextLayer
    private let rule = HUDLayer.plain("format.rule")
    private let tick: HUDIconLayer?
    private let chip: CALayer?

    init(id: String, label: String, glyph: Glyph, isOn: Bool, size: CGSize = CGSize(width: 28, height: 26), env: HUDDesignEnvironment) {
        self.isOn = isOn; self.glyph = glyph
        let letter: String
        switch glyph {
        case .bold: letter = "B"
        case .italic: letter = "I"
        case .underline: letter = "U"
        case .strikethrough: letter = "S"
        case .label(let value): letter = value
        case .check, .chip: letter = ""
        }
        let fontSize: CGFloat = size.height >= 26 ? 12 : 10
        text = HUDTextLayer(letter, style: HUDTextStyle(size: fontSize, weight: .bold, color: HUDColor.tone(0xDDDDDD)), alignment: .center)
        tick = glyph == .check ? HUDIconLayer(.check, size: min(size.width, size.height) * 0.62, color: HUDColor.nearBlack) : nil
        chip = glyph == .chip ? HUDLayer.plain("format.chip") : nil
        super.init(id: id, label: label, env: env)
        let bounds = CGRect(origin: .zero, size: size)
        HUDMotion.perform {
            layer.bounds = bounds
            layer.borderWidth = 1
            text.lineFrame = CGRect(x: 0, y: ((size.height - fontSize) / 2).rounded(), width: size.width, height: fontSize)
            // HarmonyOS Sans has no italic face; the browser's synthetic slant.
            if glyph == .italic { var slant = CATransform3DIdentity; slant.m21 = -0.2; text.transform = slant }
            let rules = glyph == .underline || glyph == .strikethrough
            let ruleWidth: CGFloat = glyph == .strikethrough ? 9 : 7
            rule.frame = CGRect(x: ((size.width - ruleWidth) / 2).rounded() + 0.5,
                                y: glyph == .strikethrough ? (size.height / 2).rounded() - 0.5 : (size.height * 0.73).rounded(),
                                width: ruleWidth, height: 1)
            rule.isHidden = !rules
            layer.addSublayer(text); layer.addSublayer(rule)
            if let tick { tick.position = CGPoint(x: size.width / 2, y: size.height / 2); layer.addSublayer(tick) }
            if let chip {
                chip.frame = bounds.insetBy(dx: 4, dy: 4); chip.borderWidth = 1; chip.borderColor = HUDColor.nearBlack
                layer.addSublayer(chip)
            }
        }
        render(animated: false)
        setTarget(bounds)
        env.applyScale(to: layer)
    }

    func setOn(_ on: Bool) { guard on != isOn else { return }; isOn = on; render(animated: true) }
    /// The colour a `chip` toggle shows.
    func setChip(_ color: CGColor) { HUDMotion.perform { chip?.backgroundColor = color } }

    private func render(animated: Bool) {
        let ring = isOn ? HUDColor.clear : (isHovered && isEnabled ? HUDColor.tone(0x888888) : HUDColor.ringField)
        HUDMotion.animate(layer, "backgroundColor", to: isOn ? HUDColor.yellow : HUDColor.tone(0x262626), duration: animated ? 0.12 : 0,
                          timing: HUDEasing.ease, env: env)
        HUDMotion.animate(layer, "borderColor", to: ring, duration: animated ? HUDDuration.hover : 0, timing: HUDEasing.ease, env: env)
        HUDMotion.perform {
            text.color = isOn ? HUDColor.nearBlack : HUDColor.tone(0xDDDDDD)
            rule.backgroundColor = text.color
            tick?.isHidden = !isOn
            layer.opacity = isEnabled ? 1 : 0.35
        }
    }

    override func hoverChanged() { render(animated: true) }
    override func pressChanged() { applyHoverMotion(pressScale: 0.94, brighten: nil) }
    override func enabledChanged() { render(animated: false) }
    override func activate() { guard isEnabled else { return }; onActivate?() }
    override var accessibilityRole: HUDAccessibilityItem.Role? {
        switch glyph { case .label, .chip: return .button; default: return .toggle(isOn) }
    }
}

/// The custom colour wheel: one shared hue/saturation raster with a ring
/// marker filled with the picked colour. Press or drag picks hue and saturation
/// (`onPick(false)` while dragging, `onPick(true)` on release); VoiceOver
/// presses step the hue by 30°.
final class NotesColorWheelControl: HUDControl {
    private let marker = HUDLayer.plain("wheel.marker")
    private let side: CGFloat
    private(set) var hue: CGFloat
    private(set) var saturation: CGFloat
    private(set) var brightness: CGFloat
    var onPick: ((Bool) -> Void)?
    var color: NSColor { NSColor(calibratedHue: hue, saturation: saturation, brightness: brightness, alpha: 1) }

    init(id: String, label: String, side: CGFloat, hue: CGFloat, saturation: CGFloat, brightness: CGFloat, env: HUDDesignEnvironment) {
        self.side = side; self.hue = hue; self.saturation = saturation; self.brightness = brightness
        super.init(id: id, label: label, env: env)
        let bounds = CGRect(x: 0, y: 0, width: side, height: side)
        HUDMotion.perform {
            layer.bounds = bounds
            let wheel = HUDLayer.plain("wheel.raster")
            wheel.frame = bounds; wheel.contents = Self.raster; wheel.contentsGravity = .resize
            marker.bounds = CGRect(x: 0, y: 0, width: 14, height: 14); marker.cornerRadius = 7
            marker.borderWidth = 2; marker.borderColor = HUDColor.nearBlack
            let ring = HUDLayer.plain(); ring.frame = marker.bounds.insetBy(dx: -1.5, dy: -1.5); ring.cornerRadius = 8.5
            ring.borderWidth = 1.5; ring.borderColor = HUDColor.white(0.9)
            marker.addSublayer(ring)
            layer.addSublayer(wheel); layer.addSublayer(marker)
        }
        setTarget(bounds, path: CGPath(ellipseIn: bounds, transform: nil))
        render()
    }

    /// One small raster shared by every open wheel; no timer or display loop.
    static let raster: NSImage = {
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
    /// The wheel point (0...1 in both axes) of a hue and saturation.
    static func unit(hue: CGFloat, saturation: CGFloat) -> CGPoint {
        let angle = hue * 2 * CGFloat.pi
        return CGPoint(x: (cos(angle) * saturation + 1) / 2, y: (sin(angle) * saturation + 1) / 2)
    }
    /// The hue and saturation under a wheel point (0...1), clamped to the rim.
    static func pick(_ unit: CGPoint) -> (hue: CGFloat, saturation: CGFloat, unit: CGPoint) {
        let dx = unit.x * 2 - 1, dy = unit.y * 2 - 1
        let radius = max(1, hypot(dx, dy))
        let hue = (atan2(dy, dx) / (2 * .pi) + 1).truncatingRemainder(dividingBy: 1)
        return (hue, min(1, hypot(dx, dy)), CGPoint(x: (dx / radius + 1) / 2, y: (dy / radius + 1) / 2))
    }

    func setBrightness(_ value: CGFloat) { brightness = min(1, max(0, value)); render() }

    private func render() {
        let unit = Self.unit(hue: hue, saturation: saturation)
        HUDMotion.perform {
            marker.position = CGPoint(x: unit.x * side, y: unit.y * side)
            marker.backgroundColor = color.cgColor
        }
    }

    private func pick(_ point: CGPoint, final: Bool) {
        let picked = Self.pick(CGPoint(x: point.x / side, y: point.y / side))
        hue = picked.hue; saturation = picked.saturation
        render()
        onPick?(final)
    }

    override func pointerDown(at point: CGPoint) -> Bool { guard isEnabled else { return false }; pick(point, final: false); return true }
    override func pointerDragged(to point: CGPoint) { pick(point, final: false) }
    override func pointerUp(at point: CGPoint) { pick(point, final: true) }
    override func activate() {
        hue = (hue + 1 / 12).truncatingRemainder(dividingBy: 1); saturation = max(saturation, 0.5)
        render(); onPick?(true)
    }
}

/// Receives a press on an editor's format controls in place of the HUD view.
/// AppKit makes a clicked view that accepts first responder (the HUD view)
/// first responder before the HUD sees the press, which ended editing; this
/// view never takes focus and hands the press on to its superview (the HUD
/// view) through the responder chain, unchanged. Hit tests return it
/// explicitly; it is never found by AppKit's own.
final class NotesPressForwarder: NSView {
    /// A forwarded press is being handled (the editor keeps its focus).
    private(set) var isForwarding = false
    override var acceptsFirstResponder: Bool { false }
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func mouseDown(with event: NSEvent) { forward { super.mouseDown(with: event) } }
    override func mouseDragged(with event: NSEvent) { forward { super.mouseDragged(with: event) } }
    override func mouseUp(with event: NSEvent) { forward { super.mouseUp(with: event) } }
    override func rightMouseDown(with event: NSEvent) { forward { super.rightMouseDown(with: event) } }
    private func forward(_ body: () -> Void) { isForwarding = true; body(); isForwarding = false }
    /// This view, attached to `host` (zero size), for the host's hit test.
    func target(in host: NSView) -> NSView {
        if superview !== host { setAccessibilityElement(false); host.addSubview(self) }
        return self
    }
}

/// A desktop note's formatting menu (font, size), colour pop-over or style bar
/// (B / I / U / S): the redesign's own HUDMenu, the shared colour picker
/// (`NotesFormatBar.colorPicker`, as in Archive and Projection) and format
/// toggles at the note buttons' size. The artwork (`chrome`) lies on the notes
/// plane and tilts with the cards; it is routed the way Projection's workspace
/// routes its chrome: a HUDInteractionRouter for presses, the host's highlight
/// pass for hover, HUDAccessibilityStandIns for VoiceOver. Only this
/// transparent input surface is native, and it never takes the focus from a
/// note being edited (`takesFocus`).
final class NotesDesktopPopup: NSView {
    enum Kind: String { case font, size, color, special }
    let kind: Kind
    let chrome: CALayer
    var onChange: ((NotesFormatChange) -> Void)?
    var onClose: (() -> Void)?
    /// Host point → chrome point (nil off the plane); chrome rect → host rect.
    var hostToLocal: ((CGPoint) -> CGPoint?)?
    var projectLocal: ((CGRect) -> CGRect)?
    /// False while a note is edited: presses format its selection and must
    /// leave the editor focused.
    var takesFocus = true
    /// The menu's selected entry (a font family or size).
    private(set) var selectedValue = ""
    private(set) var controls: [HUDControl] = []
    private let panel: CALayer
    private let env: HUDDesignEnvironment
    private var list: HUDMenu?
    private let router = HUDInteractionRouter()
    private var standIns: HUDAccessibilityStandIns?
    /// The desktop notes' button side and pitch (their bottom buttons, this style bar).
    static let buttonSide: CGFloat = 20
    static let buttonPitch: CGFloat = 24
    static let sizes = [8, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64, 72, 96, 144]
    var contentSize: CGSize { chrome.bounds.size }
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { takesFocus }

    /// - style: the selection's (or the drawing pen's) current style.
    init(kind: Kind, style: NotesTextStyle, defaultColor: NSColor, colorTitle: String, env: HUDDesignEnvironment) {
        self.kind = kind; self.env = env
        let chrome = HUDLayer.plain("notes.desktopPopup"), panel: CALayer
        var controls: [HUDControl] = [], selected = "", menu: HUDMenu?, popover: HUDPopover?
        var emit: ((NotesFormatChange) -> Void)?
        switch kind {
        case .font, .size:
            let values: [String]
            if kind == .font {
                values = ArchiveRichText.families
                selected = style.fontName == nil ? ArchiveRichText.defaultFamily
                    : (style.attributes(defaultColor: defaultColor)[.font] as? NSFont)?.familyName ?? ""
            } else {
                values = Set(Self.sizes + [Int(style.fontSize.rounded())]).sorted().map(String.init)
                selected = String(Int(style.fontSize.rounded()))
            }
            let list = HUDMenu(id: "notes.desk." + kind.rawValue, items: values.map { HUDMenu.Item(id: $0, title: $0, selected: $0 == selected) },
                               minimumWidth: kind == .font ? 200 : 96, env: env)
            if let index = values.firstIndex(of: selected) { list.setScroll(CGFloat(index - 3) * HUDMenu.rowHeight) }
            menu = list; panel = list.layer; controls = list.controls
        case .color:
            let color = NotesFormatBar.hex((style.color?.color ?? defaultColor).cgColor)
            let picker = NotesFormatBar.colorPicker(id: "notes.desk.color", title: colorTitle, color: color, env: env, bubbleHost: chrome) {
                emit?(.color(NotesFormatBar.color($0)))
            }
            popover = picker.popover; panel = picker.popover.layer
            controls = picker.controls + (picker.popover.close.map { [$0] } ?? [])
        case .special:
            panel = HUDLayer.plain("notes.styleBar")
            let side = Self.buttonSide, pitch = Self.buttonPitch, inset = (pitch - side) / 2 + 2
            let traits: [(NotesFormatToggle.Glyph, String, Bool, NotesFormatChange)] = [
                (.bold, L10n.text("Bold", "粗体"), style.bold, .bold), (.italic, L10n.text("Italic", "斜体"), style.italic, .italic),
                (.underline, L10n.text("Underline", "下划线"), style.underline, .underline),
                (.strikethrough, L10n.text("Strikethrough", "删除线"), style.strikethrough, .strikethrough)]
            for (index, trait) in traits.enumerated() {
                let toggle = NotesFormatToggle(id: "notes.desk.trait:\(index)", label: trait.1, glyph: trait.0, isOn: trait.2,
                                               size: CGSize(width: side, height: side), env: env)
                toggle.onActivate = { [unowned toggle] in toggle.setOn(!toggle.isOn); emit?(trait.3) }
                controls.append(toggle)
            }
            controls.append(HUDIconButton(id: "notes.desk.close", label: L10n.text("Close", "关闭"), icon: .x, side: side, iconSize: 9, env: env))
            HUDMotion.perform {
                for (index, control) in controls.enumerated() {
                    control.layer.frame = CGRect(x: inset + CGFloat(index) * pitch, y: inset, width: side, height: side)
                    panel.addSublayer(control.layer)
                }
            }
            HUDPanelChrome.apply(to: panel, size: CGSize(width: inset * 2 + CGFloat(controls.count - 1) * pitch + side, height: inset * 2 + side))
        }
        self.chrome = chrome; self.panel = panel; self.list = menu; self.controls = controls; selectedValue = selected
        super.init(frame: .zero)
        emit = { [weak self] in self?.onChange?($0) }
        menu?.onPick = { [weak self] item in self?.pick(item.id) }
        menu?.onClose = { [weak self] in self?.onClose?() }
        popover?.onClose = { [weak self] in self?.onClose?() }
        controls.first { $0.id == "notes.desk.close" }?.onActivate = { [weak self] in self?.onClose?() }
        finish()
    }
    required init?(coder: NSCoder) { nil }

    private func finish() {
        HUDMotion.perform {
            chrome.zPosition = 2_000_000; chrome.anchorPoint = .zero
            chrome.bounds = CGRect(origin: .zero, size: panel.bounds.size)
            panel.frame = chrome.bounds; chrome.addSublayer(panel)
        }
        env.applyScale(to: chrome)
        router.set(controls)
        let standIns = HUDAccessibilityStandIns(host: self)
        standIns.project = { [weak self] rect in
            guard let self, let host = self.superview else { return rect }
            return self.convert(self.projectLocal?(rect) ?? rect, from: host)
        }
        standIns.onPerform = { [weak self] id in self?.controls.first { $0.id == id }?.activate() }
        standIns.onAdjust = { [weak self] id, value in
            guard let slider = self?.controls.first(where: { $0.id == id }) as? HUDSlider else { return }
            slider.setValue(value); slider.onChange?(value, true)
        }
        self.standIns = standIns
        setAccessibilityRole(.group)
    }

    private func pick(_ value: String) {
        selectedValue = value
        if kind == .font { onChange?(.font(value)) } else if let size = Double(value) { onChange?(.size(size)) }
        onClose?()
    }

    /// The redesign's top-down wipe (menus and pop-overs alike).
    func playEntrance() { HUDMotion.wipe(panel, from: .top, duration: HUDDuration.menuWipe, fadeFrom: 0.4, env: env) }

    /// Activates a control by id (VoiceOver stand-ins, tests and previews).
    func performControl(_ id: String) { controls.first { $0.id == id }?.activate() }
    func controlRect(_ id: String) -> CGRect? { controls.first { $0.id == id }?.rect(in: chrome) }

    func layoutAccessibility() { standIns?.update(router.accessibilityItems(in: chrome)) }

    /// Ends every hover, press and drag (the pop-over is leaving).
    func cancelInteraction() {
        router.cancel()
        controls.forEach { ($0 as? HUDSlider)?.cancelDrag(); $0.clearInteractionState() }
        standIns?.update([])
    }

    override func draw(_ dirtyRect: NSRect) {}
    func localPoint(_ hostPoint: CGPoint) -> CGPoint? { hostToLocal?(hostPoint) }
    override func hitTest(_ point: NSPoint) -> NSView? {
        guard !isHidden, let local = localPoint(point), chrome.bounds.contains(local) else { return nil }
        return self
    }
    private func eventPoint(_ event: NSEvent) -> CGPoint? {
        superview.flatMap { localPoint($0.convert(event.locationInWindow, from: nil)) }
    }
    override func mouseDown(with event: NSEvent) {
        guard let point = eventPoint(event) else { return }
        router.pointerDown(at: point, in: chrome)
        HUDControlHighlightLayer.update(in: chrome, point: point, pressed: true)
    }
    override func mouseDragged(with event: NSEvent) {
        guard let point = eventPoint(event) else { return }
        router.pointerDragged(to: point, in: chrome)
    }
    override func mouseUp(with event: NSEvent) {
        guard let point = eventPoint(event) else { router.cancel(); return }
        HUDControlHighlightLayer.update(in: chrome, point: point)
        router.pointerUp(at: point, in: chrome)
    }
    override func scrollWheel(with event: NSEvent) {
        list?.scroll(by: -event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12))
    }
    override func keyDown(with event: NSEvent) {
        if let list, list.handleKey(keyCode: event.keyCode) { return }
        if event.keyCode == 53 { onClose?(); return }
        super.keyDown(with: event)
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
        var style = NotesRichText.capture(HUDStandardRichText.stored(NSAttributedString(string: "x", attributes: attributes)),
            scale: scale, defaultColor: defaultColor).runs.first?.style ?? NotesTextStyle()
        if selection.length > 0, NSMaxRange(selection) <= storage.length {
            var bold = true, italic = true, underline = true, strike = true
            storage.enumerateAttributes(in: selection) { attrs, _, _ in
                let font = attrs[.font] as? NSFont ?? HUDFontResolver.nsFont(size: 12)
                bold = bold && HUDStandardRichText.isBold(font); italic = italic && HUDStandardRichText.isItalic(font)
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
        // Only the four toggles matter to switchesOn(over:).
        func toggles(_ attributes: [NSAttributedString.Key: Any]) -> NotesTextStyle {
            let font = attributes[.font] as? NSFont ?? HUDFontResolver.nsFont(size: 12 * scale)
            var style = NotesTextStyle()
            style.bold = HUDStandardRichText.isBold(font); style.italic = HUDStandardRichText.isItalic(font)
            style.underline = ((attributes[.underlineStyle] as? NSNumber)?.intValue ?? 0) != 0
            style.strikethrough = ((attributes[.strikethroughStyle] as? NSNumber)?.intValue ?? 0) != 0
            return style
        }
        var current: [NotesTextStyle] = []
        if selected.length == 0 { current = [toggles(editor.typingAttributes)] }
        else { storage.enumerateAttributes(in: selected) { attributes, _, _ in current.append(toggles(attributes)) } }
        let applyTrait = change.switchesOn(over: current)
        func changed(_ attributes: [NSAttributedString.Key: Any]) -> [NSAttributedString.Key: Any] {
            var values = attributes
            let font = values[.font] as? NSFont ?? HUDFontResolver.nsFont(size: 12 * scale)
            let manager = NSFontManager.shared
            // The standard faces keep their language cascade; bold is the
            // embedded Bold and italic a slant (see HUDStandardRichText).
            let standard = HUDStandardRichText.isStandard(font)
            let bold = HUDStandardRichText.isBold(font), italic = HUDStandardRichText.isItalic(font)
            switch change {
            case .font(let family) where family == HUDFontResolver.latinFamily:
                values[.font] = HUDStandardRichText.font(size: font.pointSize, bold: bold, italic: italic)
            case .font(let family):
                let traits: NSFontTraitMask = standard ? [bold ? .boldFontMask : [], italic ? .italicFontMask : []] : manager.traits(of: font)
                values[.font] = manager.font(withFamily: family, traits: traits, weight: 5, size: font.pointSize) ?? font
            case .size(let size):
                let points = CGFloat(min(144, max(6, size))) * scale
                values[.font] = standard ? HUDStandardRichText.font(size: points, bold: bold, italic: italic) : manager.convert(font, toSize: points)
            case .color(let color): values[.foregroundColor] = color
            case .bold:
                values[.font] = standard ? HUDStandardRichText.font(size: font.pointSize, bold: applyTrait, italic: italic)
                    : NotesTextStyle.changingTrait(.boldFontMask, enabled: applyTrait, on: font)
            case .italic:
                values[.font] = standard ? HUDStandardRichText.font(size: font.pointSize, bold: bold, italic: applyTrait)
                    : NotesTextStyle.changingTrait(.italicFontMask, enabled: applyTrait, on: font)
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
        var updates: [(NSRange, [NSAttributedString.Key: Any])] = []
        storage.enumerateAttributes(in: selected) { attributes, range, _ in updates.append((range, changed(attributes))) }
        if editor.setAttributesUndoably(updates, in: selected) { editor.setSelectedRange(selected) }
    }
}

extension NSTextView {
    /// Sets the attributes of each run in `range` as one undoable edit (the
    /// format bars of Notes and Archive). Returns false when nothing changed.
    @discardableResult
    func setAttributesUndoably(_ runs: [(NSRange, [NSAttributedString.Key: Any])], in range: NSRange) -> Bool {
        guard let storage = textStorage, !runs.isEmpty, NSMaxRange(range) <= storage.length,
              shouldChangeText(in: range, replacementString: nil) else { return false }
        let previous = storage.attributedSubstring(from: range)
        undoManager?.registerUndo(withTarget: self) { $0.restoreFormatting(previous, in: range) }
        storage.beginEditing()
        for (run, attributes) in runs { storage.setAttributes(attributes, range: run) }
        storage.endEditing()
        didChangeText()
        return true
    }

    /// Undo / redo of setAttributesUndoably: swaps the text back and registers the inverse.
    private func restoreFormatting(_ text: NSAttributedString, in range: NSRange) {
        guard let storage = textStorage, NSMaxRange(range) <= storage.length else { return }
        let inverse = storage.attributedSubstring(from: range)
        undoManager?.registerUndo(withTarget: self) { $0.restoreFormatting(inverse, in: range) }
        storage.replaceCharacters(in: range, with: text)
        setSelectedRange(range); didChangeText()
    }
}
