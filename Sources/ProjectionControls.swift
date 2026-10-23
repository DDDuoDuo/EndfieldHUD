import AppKit
import QuartzCore

/// The projection workspace's floating toolbar in the redesign's language:
/// the menu panel (HUDPanelChrome) holding the centre page's swatches and the
/// custom-colour swatch, the pen / eraser tools, the brush-width dropdown, the
/// background tools, Clear all and Return. Its controls are routed by the
/// workspace (pointer, hover pass, VoiceOver stand-ins); no native window.
final class ProjectionToolbar {
    let layer = HUDLayer.plain("projection.toolbar")
    let env: HUDDesignEnvironment
    private(set) var controls: [HUDControl] = []
    /// Toolbar actions by id: color:RRGGBB, color:custom, pen, eraser, brush,
    /// background, appearance, media, clear, close.
    var onAction: ((String) -> Void)?
    private var swatches: [(UInt32, HUDSwatch)] = []
    private let custom: HUDSwatch
    private let pen: HUDIconButton, eraser: HUDIconButton, background: HUDIconButton
    private let brush: HUDDropdown
    static let height: CGFloat = 46

    /// Leave room for both the menu bar and camera housing without placing
    /// controls at the opposite screen edge on an ordinary external display.
    static func topInset(safeAreaTop: CGFloat, visibleTop: CGFloat) -> CGFloat {
        let safe = safeAreaTop.isFinite ? max(0, safeAreaTop) : 0
        let visible = visibleTop.isFinite ? max(0, visibleTop) : 0
        return max(64, max(safe, visible) + 24)
    }

    init(env: HUDDesignEnvironment) {
        self.env = env
        custom = HUDSwatch(id: "color:custom", label: L10n.text("Custom color", "自定义颜色"), color: nil, size: 22, env: env)
        pen = HUDIconButton(id: "pen", label: L10n.text("Pen", "画笔"), icon: .pen, env: env)
        eraser = HUDIconButton(id: "eraser", label: L10n.text("Eraser", "橡皮擦"), icon: .eraser, env: env)
        brush = HUDDropdown(id: "brush", label: L10n.text("Brush thickness", "画笔粗细"), value: "80 PX", size: .small, width: 84, env: env)
        background = HUDIconButton(id: "background", label: L10n.text("Background", "背景"), icon: .grid, env: env)
        let appearance = HUDIconButton(id: "appearance", label: L10n.text("Background appearance", "背景外观"), icon: .eye, env: env)
        let media = HUDIconButton(id: "media", label: L10n.text("Image/Video", "图片/视频"), icon: .image, env: env)
        let clear = HUDButton(id: "clear", title: L10n.text("Clear all", "清空"), icon: .trash, kind: .secondary, size: .small, env: env)
        let close = HUDButton(id: "close", title: L10n.text("Return", "返回"), icon: .back, kind: .primary, size: .small, env: env)
        let height = Self.height
        var x: CGFloat = 12
        func place(_ control: HUDControl) {
            HUDMotion.perform {
                control.layer.frame.origin = CGPoint(x: x, y: ((height - control.layer.bounds.height) / 2).rounded())
                layer.addSublayer(control.layer)
            }
            x += control.layer.bounds.width + 8
            controls.append(control)
        }
        func divider() {
            let line = HUDLayer.plain("projection.toolbar.divider")
            HUDMotion.perform { line.frame = CGRect(x: x + 2, y: (height - 22) / 2, width: 1, height: 22); line.backgroundColor = HUDColor.ringIcon }
            layer.addSublayer(line)
            x += 2 + 1 + 2 + 8
        }
        for hex in ProjectionSettings.palette {
            let name = String(format: "%06X", hex)
            let swatch = HUDSwatch(id: "color:" + name, label: L10n.text("Pen color", "画笔颜色") + " #" + name, color: HUDColor.rgb(hex), size: 22, env: env)
            swatches.append((hex, swatch)); place(swatch)
        }
        place(custom)
        divider()
        [pen, eraser].forEach(place); place(brush)
        divider()
        [background, appearance, media].forEach(place)
        divider()
        clear.place(at: .zero); place(clear)
        close.place(at: .zero); place(close)
        for control in controls { control.onActivate = { [weak self, unowned control] in self?.onAction?(control.id) } }
        HUDPanelChrome.apply(to: layer, size: CGSize(width: x - 8 + 12, height: height))
        env.applyScale(to: layer)
    }

    var contentSize: CGSize { layer.bounds.size }
    func control(_ id: String) -> HUDControl? { controls.first { $0.id == id } }

    /// In-place state from the session model (selection, tools, width).
    func update(_ model: ProjectionModel) {
        let color = NotesFormatBar.hex(model.color.cgColor)
        var matched = false
        for (hex, swatch) in swatches { swatch.isSelected = !model.erasing && hex == color; matched = matched || hex == color }
        custom.isSelected = !model.erasing && !matched
        pen.isOn = !model.erasing; eraser.isOn = model.erasing
        background.isOn = model.backgroundEnabled
        let width = "\(Int(model.brushWidth.rounded())) PX"
        if brush.valueText != width { brush.setValue(width) }
    }

    /// The menu or pop-over this control opened (its dropdown caret / bar).
    func setOpen(_ id: String?) { brush.isOpen = id == "brush" }
}
