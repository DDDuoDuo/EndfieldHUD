import AppKit
import QuartzCore

/// The Adjust tab's tool sub-pages (spec §3.10). Trim exists for video sources.
enum MediaAssemblyTool: String, CaseIterable {
    case crop, adjust, curves, levels, trim

    var title: String {
        switch self {
        case .crop: return L10n.text("Crop", "裁剪")
        case .adjust: return L10n.text("Adjust", "调整")
        case .curves: return L10n.text("Curves", "曲线")
        case .levels: return L10n.text("Levels", "色阶")
        case .trim: return L10n.text("Trim", "剪辑")
        }
    }

    var icon: HUDIcon {
        switch self {
        case .crop: return .grid
        case .adjust: return .gear
        case .curves: return .lyric
        case .levels: return .apps
        case .trim: return .timer
        }
    }
}

/// One slider of a tool page: its range and default (spec §3.10 table) and
/// how it reads and writes the session's adjustments.
struct MediaAssemblyParameter {
    let id: String
    let title: String
    let range: HUDSlider.Range
    let defaultValue: Double
    let read: (MediaAssemblyAdjustments) -> Double
    let write: (inout MediaAssemblyAdjustments, Double) -> Void

    /// Two decimals, or an integer when the step is at least 1 (spec §1.8).
    var format: HUDValueFormat { range.step >= 1 ? .integer : .decimal }

    private init(_ id: String, _ title: String, _ minimum: Double, _ maximum: Double, step: Double = 0.01, default value: Double,
                 read: @escaping (MediaAssemblyAdjustments) -> Double, write: @escaping (inout MediaAssemblyAdjustments, Double) -> Void) {
        self.id = id; self.title = title; defaultValue = value; self.read = read; self.write = write
        range = HUDSlider.Range(minimum: minimum, maximum: maximum, step: step, zero: minimum < 0 ? 0 : nil)
    }

    /// The sliders of `tool`; trim needs the video's duration.
    static func parameters(for tool: MediaAssemblyTool, duration: Double = 0) -> [MediaAssemblyParameter] {
        switch tool {
        case .adjust:
            return [
                Self("brightness", L10n.text("Brightness", "亮度"), -1, 1, default: 0, read: { $0.brightness }, write: { $0.brightness = $1 }),
                Self("contrast", L10n.text("Contrast", "对比度"), 0, 4, default: 1, read: { $0.contrast }, write: { $0.contrast = $1 }),
                Self("saturation", L10n.text("Saturation", "饱和度"), 0, 2, default: 1, read: { $0.saturation }, write: { $0.saturation = $1 }),
                Self("temperature", L10n.text("Temperature", "色温"), 2000, 12000, step: 100, default: 6500,
                     read: { $0.temperature }, write: { $0.temperature = $1 }),
                Self("tint", L10n.text("Tint", "色调"), -200, 200, step: 1, default: 0, read: { $0.tint }, write: { $0.tint = $1 }),
                Self("highlights", L10n.text("Highlights", "高光"), 0, 1, default: 1, read: { $0.highlights }, write: { $0.highlights = $1 }),
                Self("shadows", L10n.text("Shadows", "阴影"), 0, 1, default: 0, read: { $0.shadows }, write: { $0.shadows = $1 }),
                Self("exposure", L10n.text("Exposure", "曝光度"), -4, 4, default: 0, read: { $0.exposure }, write: { $0.exposure = $1 })
            ]
        case .curves:
            return (0..<5).map { index in
                Self("curve\(index)", "\(index * 25)%", 0, 1, default: Double(index) / 4,
                     read: { $0.curve[index] }, write: { $0.curve[index] = $1 })
            }
        case .levels:
            // The engine needs the white point at least .01 above the black point.
            return [
                Self("black", L10n.text("Black point", "黑场"), 0, 1, default: 0, read: { $0.levelsBlack },
                     write: { $0.levelsBlack = min($1, 0.99, $0.levelsWhite - 0.01) }),
                Self("gamma", L10n.text("Midtones", "中间调"), 0.1, 4, default: 1, read: { $0.levelsGamma }, write: { $0.levelsGamma = $1 }),
                Self("white", L10n.text("White point", "白场"), 0, 1, default: 1, read: { $0.levelsWhite },
                     write: { $0.levelsWhite = max($1, 0.01, $0.levelsBlack + 0.01) })
            ]
        case .trim:
            let length = max(0.1, duration)
            return [
                Self("trimStart", L10n.text("Start", "开始"), 0, length, step: 0.05, default: 0, read: { $0.trimStart },
                     write: { $0.trimStart = min(max(0, $1), ($0.trimEnd ?? length) - 0.0501) }),
                Self("trimEnd", L10n.text("End", "结束"), 0, length, step: 0.05, default: length, read: { $0.trimEnd ?? length },
                     write: { $0.trimEnd = min(length, max($1, $0.trimStart + 0.0501)) })
            ]
        case .crop:
            return []
        }
    }
}

/// A light tool tile of the Adjust tab (spec §3.10 `.tools button`): 62 px with
/// the 18 px icon above the label, or a 36 px wide row with them side by side.
/// Hover lifts it 2 px and brightens it; `isOn` turns it yellow (Mirror).
final class MediaAssemblyToolTile: HUDControl {
    private let plate = HUDLayer.plain("tile.plate")
    private let brighten = HUDLayer.shape("tile.brighten")
    private let glyph: HUDIconLayer
    private let text: HUDTextLayer
    private let size: CGSize
    var isOn = false { didSet { if isOn != oldValue { paint() } } }

    init(id: String, title: String, icon: HUDIcon, size: CGSize, env: HUDDesignEnvironment) {
        self.size = size
        glyph = HUDIconLayer(icon, size: 18, color: HUDColor.nearBlack)
        text = HUDTextLayer(title, style: HUDTextStyle(size: 12, weight: .medium, color: HUDColor.nearBlack))
        super.init(id: id, label: title, env: env)
        let bounds = CGRect(origin: .zero, size: size)
        HUDMotion.perform {
            layer.bounds = bounds
            plate.frame = bounds; brighten.frame = bounds
            brighten.path = HUDShape.chamfer(bounds, cut: 8); brighten.fillColor = HUDColor.white(0.06); brighten.opacity = 0
            // Long translations shrink a little before they truncate.
            text.minimumScale = 0.75
            let width = min(text.preferredWidth, size.width - 8)
            if size.height > 40 {
                glyph.frame = CGRect(x: (size.width - 18) / 2, y: 12, width: 18, height: 18)
                text.alignment = .center
                text.lineFrame = CGRect(x: 4, y: 37, width: size.width - 8, height: 12)
            } else {
                let start = (size.width - 18 - 6 - width) / 2
                glyph.frame = CGRect(x: start, y: (size.height - 18) / 2, width: 18, height: 18)
                text.lineFrame = CGRect(x: start + 24, y: (size.height - 12) / 2, width: width + 1, height: 12)
            }
            [plate, brighten, glyph, text].forEach(layer.addSublayer)
        }
        paint()
        setTarget(bounds, path: HUDShape.chamfer(bounds, cut: 8))
        env.applyScale(to: text)
    }

    private func paint() {
        let colors = isOn ? [HUDColor.yellowHigh, HUDColor.yellow, HUDColor.yellowLow] : HUDColor.lightButton
        HUDMotion.perform {
            plate.showArtwork(HUDArtwork.plate(colors, locations: isOn ? [0, 0.5, 1] : nil, size: size, cut: 8, scale: env.renderScale),
                              scale: env.renderScale)
        }
    }

    override func hoverChanged() { applyHoverMotion(lift: 2, pressScale: 0.965, brighten: brighten) }
    override func pressChanged() { applyHoverMotion(lift: 2, pressScale: 0.965, brighten: brighten) }
    override var accessibilityRole: HUDAccessibilityItem.Role? { id == "tool:mirror" ? .toggle(isOn) : .button }
}

/// A filter or sticker cell (spec §3.10 `.cell`): dark well with an inset
/// ring, the asset thumbnail, and for filters a name strip that turns yellow
/// when selected. `None` shows ∅.
final class MediaAssemblyPresetCell: HUDControl {
    private let ring = HUDLayer.plain("cell.ring")
    private let strip = HUDLayer.plain("cell.strip")
    private let name: HUDTextLayer?
    var isSelected = false { didSet { if isSelected != oldValue { paint(animated: true) } } }

    /// - title: the name strip (filters); nil for stickers.
    init(id: String, label: String, title: String?, image: CGImage?, size: CGSize, env: HUDDesignEnvironment) {
        name = title.map { HUDTextLayer($0, style: HUDTextStyle(size: 10, weight: .medium, color: HUDColor.rgb(0xDDDDDD)), alignment: .center) }
        super.init(id: id, label: label, env: env)
        let bounds = CGRect(origin: .zero, size: size)
        let sticker = title == nil
        HUDMotion.perform {
            layer.bounds = bounds; layer.masksToBounds = true
            layer.backgroundColor = sticker ? HUDColor.charcoal : HUDColor.rgb(0x1E1E1E)
            let side: CGFloat = sticker ? 42 : 44
            if let image {
                let thumbnail = HUDLayer.plain("cell.image")
                thumbnail.frame = CGRect(x: (size.width - side) / 2, y: sticker ? 5 : 6, width: side, height: side)
                thumbnail.contents = image; thumbnail.contentsGravity = .resizeAspect
                thumbnail.minificationFilter = .trilinear
                layer.addSublayer(thumbnail)
            } else if !sticker {
                let none = HUDTextLayer("∅", style: HUDTextStyle(size: 22, weight: .regular, color: HUDColor.onDarkTag), alignment: .center)
                none.lineFrame = CGRect(x: 0, y: (size.height - 18 - 22) / 2, width: size.width, height: 22)
                layer.addSublayer(none)
            }
            // The inset ring paints under the content, as CSS box-shadow does.
            ring.frame = bounds; ring.borderWidth = 1; ring.borderColor = HUDColor.rgb(0x363636)
            layer.addSublayer(ring)
            if let name {
                strip.frame = CGRect(x: 0, y: size.height - 18, width: size.width, height: 18)
                name.lineFrame = CGRect(x: 4, y: 4, width: size.width - 8, height: 10)
                strip.addSublayer(name); layer.addSublayer(strip)
            }
        }
        paint(animated: false)
        setTarget(bounds)
        env.applyScale(to: layer)
    }

    private func paint(animated: Bool) {
        let hovered = isHovered && isEnabled
        let duration = animated ? HUDDuration.hover : 0
        HUDMotion.animate(ring, "borderWidth", to: isSelected ? 2 : 1, duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(ring, "borderColor", to: isSelected ? HUDColor.yellow : (hovered ? HUDColor.rgb(0x777777) : HUDColor.rgb(0x363636)),
                          duration: duration, timing: HUDEasing.ease, env: env)
        HUDMotion.perform {
            strip.backgroundColor = isSelected ? HUDColor.yellow : HUDColor.black(0.5)
            name?.color = isSelected ? HUDColor.nearBlack : HUDColor.rgb(0xDDDDDD)
        }
    }

    override func hoverChanged() {
        paint(animated: true)
        HUDMotion.animate(layer, "transform.translation.y", to: isHovered && isEnabled ? -2 : 0, duration: HUDDuration.hover, env: env)
    }
    override var accessibilityRole: HUDAccessibilityItem.Role? { name == nil ? .button : .tab(selected: isSelected) }
}

/// The zoom readout between − and + (spec §3.10 `.zv`): it shows the zoom and
/// resets zoom and pan.
final class MediaAssemblyZoomValue: HUDControl {
    private let text = HUDTextLayer(style: HUDTextStyle(size: 12, weight: .bold, color: HUDColor.textOnDark, tabular: true), alignment: .center)

    init(env: HUDDesignEnvironment) {
        super.init(id: "zoomReset", label: L10n.text("Reset zoom", "重置缩放"), env: env)
        let bounds = CGRect(x: 0, y: 0, width: 58, height: 30)
        HUDMotion.perform {
            layer.bounds = bounds
            layer.backgroundColor = HUDColor.field; layer.borderWidth = 1; layer.borderColor = HUDColor.ringField
            text.lineFrame = CGRect(x: 0, y: 9, width: 58, height: 12)
            layer.addSublayer(text)
        }
        setTarget(bounds)
        env.applyScale(to: text)
    }

    func setZoom(_ zoom: CGFloat) { HUDMotion.perform { text.text = String(format: "%.1f×", zoom) } }
    var valueTextForTesting: String { text.text }

    override func hoverChanged() {
        HUDMotion.animate(layer, "borderColor", to: isHovered && isEnabled ? HUDColor.tone(0x888888) : HUDColor.ringField,
                          duration: 0.2, timing: HUDEasing.ease, env: env)
    }
}

/// One row of a tool's slider list (spec §3.10 `.trow`): name and value on
/// the label line, the dark slider below. 146 × 44, padding 4 0 2.
final class MediaAssemblyParameterRow {
    static let height: CGFloat = 44
    let layer = HUDLayer.plain("media.parameter")
    let slider: HUDSlider
    let parameter: MediaAssemblyParameter
    private let value = HUDTextLayer(style: HUDTextStyle(size: 11, weight: .bold, color: HUDColor.textOnDark, tabular: true), alignment: .right)

    init(_ parameter: MediaAssemblyParameter, value current: Double, width: CGFloat, env: HUDDesignEnvironment) {
        self.parameter = parameter
        slider = HUDSlider(id: "parameter:" + parameter.id, label: parameter.title, value: current, range: parameter.range,
                           format: parameter.format, width: width, dark: true, showsReadout: false, env: env)
        let name = HUDTextLayer(parameter.title, style: HUDTextStyle(size: 12, weight: .regular, color: HUDColor.tone(0xDDDDDD)))
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: width, height: Self.height)
            // Both texts share the 14 px label line's baseline.
            name.lineFrame = CGRect(x: 0, y: 5, width: width - 44, height: 12)
            value.lineFrame = CGRect(x: width - 60, y: 5.75, width: 60, height: 11)
            // The slider's track sits 2 px higher than the shared one (top 9).
            slider.layer.frame.origin = CGPoint(x: 0, y: 18 - 2)
            [name, value, slider.layer].forEach(layer.addSublayer)
        }
        setValue(current)
        env.applyScale(to: layer)
    }

    /// Moves the slider and the label readout (in place, no re-render).
    func setValue(_ current: Double) {
        if !slider.isDragging { slider.setValue(current) }
        HUDMotion.perform { value.text = parameter.format.string(slider.isDragging ? slider.value : current) }
    }

    func showDragValue(_ current: Double) { HUDMotion.perform { value.text = parameter.format.string(current) } }
    var valueTextForTesting: String { value.text }
}
