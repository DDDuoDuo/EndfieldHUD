import AppKit
import QuartzCore

/// The Projection screen of the redesign (spec §3.14): a preview of the pen on
/// the workspace background, the pen toolbar, pen width and background rows,
/// and the launcher of the projection workspace (`onOpen`, wired by the host).
/// Every value is the workspace's own preference (ProjectionSettings).
final class ProjectionCanvas: HUDDesignCanvas {
    var onOpen: (() -> Void)?
    let settings: ProjectionSettings
    /// In-place updates of the visible controls from the current settings.
    private var binders: [() -> Void] = []
    private static let previewSize = CGSize(width: HUDMetric.box.width, height: 150)

    init(context: HUDDesignContext, settings: ProjectionSettings = .shared) {
        self.settings = settings
        super.init(module: .projection, context: context)
    }

    override func didActivate() {
        settings.capture()
        refresh()
    }

    private var counter: String { settings.values.background ? "BG ON" : "BG OFF" }

    private func refresh() {
        binders.forEach { $0() }
        header.setCounter(counter)
    }

    private func change(_ update: (inout ProjectionSettings.Values) -> Void) {
        settings.update(update)
        refresh()
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        binders = []
        let width = HUDMetric.box.width
        var y = HUDMetric.headerHeight
        let preview = makePreview()
        HUDMotion.perform { preview.frame.origin = CGPoint(x: 0, y: y); page.layer.addSublayer(preview) }
        page.stagger(preview)
        y += Self.previewSize.height + 10

        let toolbar = makeToolbar(page)
        HUDMotion.perform { toolbar.frame.origin = CGPoint(x: 0, y: y); page.layer.addSublayer(toolbar) }
        page.stagger(toolbar)
        y += toolbar.bounds.height + 10

        let widthRow = HUDRowPlate(id: "row:penWidth", title: L10n.text("Pen width", "画笔粗细"), variant: .slider, width: width, env: env)
        let area = widthRow.controlArea
        let slider = HUDSlider(id: "penWidth", label: L10n.text("Pen width", "画笔粗细"),
                               value: min(ProjectionSettings.widthRange.upperBound, settings.values.width),
                               range: HUDSlider.Range(minimum: ProjectionSettings.widthRange.lowerBound,
                                                      maximum: ProjectionSettings.widthRange.upperBound, step: 1),
                               format: .pixels, width: area.width, env: env)
        HUDMotion.perform { slider.layer.frame.origin = area.origin; widthRow.layer.addSublayer(slider.layer) }
        hostBubble(slider)
        slider.onChange = { [weak self] value, final in
            guard let self else { return }
            if final { self.change { $0.width = value }; self.context.onInputStateChange?() }
            else { self.strokeWidthChanged?(value) }
        }
        binders.append { [weak self, weak slider] in
            guard let self, let slider, !slider.isDragging else { return }
            slider.setValue(self.settings.values.width)
        }
        page.add(widthRow); page.add(slider)

        let backgroundRow = HUDRowPlate(id: "row:dotted", title: L10n.text("Dotted background", "点阵背景"), width: width, env: env)
        let toggle = HUDToggle(id: "dotted", label: L10n.text("Dotted background", "点阵背景"), isOn: settings.values.background, env: env)
        toggle.onChange = { [weak self] on in self?.change { $0.background = on } }
        binders.append { [weak self, weak toggle] in guard let self else { return }; toggle?.setOn(self.settings.values.background, animated: true) }
        backgroundRow.setCluster([toggle.layer])
        page.add(backgroundRow); page.add(toggle)

        for row in [widthRow, backgroundRow] {
            HUDMotion.perform { row.layer.frame.origin = CGPoint(x: 0, y: y); page.layer.addSublayer(row.layer) }
            page.stagger(row.layer)
            y += row.layer.bounds.height + HUDMetric.rowGap
        }

        let open = HUDButton(id: "open", title: L10n.text("Open projection", "打开投影"), icon: .ext, kind: .primary, size: .large, env: env)
        open.place(at: CGPoint(x: ((width - open.preferredWidth) / 2).rounded(), y: y - HUDMetric.rowGap + 12))
        open.onActivate = { [weak self] in
            self?.onOpen?()
            self?.toast(icon: .ext, "Projection", "投影已打开")
        }
        HUDMotion.perform { page.layer.addSublayer(open.layer) }
        page.add(open); page.stagger(open.layer)

        header.configure(crumbs: ["PROJECTION"], code: "MOD-09", title: HUDModuleTitle("Projection", "投影"),
                         counter: counter, showsBack: false)
    }

    /// Live pen-width preview while the slider is dragged.
    private var strokeWidthChanged: ((Double) -> Void)?

    /// The workspace background (dots, or the checkerboard when it is off)
    /// with a sample stroke in the pen colour and width.
    private func makePreview() -> CALayer {
        let size = Self.previewSize
        let root = HUDLayer.plain("projection.preview")
        let checker = HUDLayer.shape("projection.checker"), dots = HUDLayer.shape("projection.dots")
        let stroke = HUDLayer.shape("projection.stroke")
        HUDMotion.perform {
            root.bounds = CGRect(origin: .zero, size: size)
            root.masksToBounds = true
            root.borderWidth = 1; root.borderColor = HUDColor.ringField
            checker.frame = root.bounds; checker.backgroundColor = HUDColor.rgb(0x1C1C1C)
            checker.fillColor = HUDColor.rgb(0x232323); checker.path = Self.checkerPath(size)
            dots.frame = root.bounds; dots.backgroundColor = HUDColor.rgb(0x141414)
            dots.fillColor = HUDColor.rgb(0x555555); dots.path = Self.dotPath(size)
            stroke.frame = root.bounds; stroke.fillColor = nil; stroke.lineCap = .round; stroke.lineJoin = .round
            stroke.path = Self.samplePath
            [checker, dots, stroke].forEach(root.addSublayer)
        }
        func render(width: Double) {
            let values = settings.values
            HUDMotion.perform {
                dots.isHidden = !values.background
                stroke.strokeColor = HUDColor.rgb(values.color)
                stroke.lineWidth = CGFloat(width / 4 + 1)
            }
        }
        render(width: settings.values.width)
        binders.append { [weak self] in guard let self else { return }; render(width: self.settings.values.width) }
        strokeWidthChanged = { render(width: $0) }
        return root
    }

    /// The pen toolbar (spec §3.14 item 2): swatches, divider, tools, Clear all.
    private func makeToolbar(_ page: HUDDesignPage) -> CALayer {
        let bar = HUDLayer.plain("projection.toolbar")
        let height: CGFloat = 42
        HUDMotion.perform {
            bar.bounds = CGRect(x: 0, y: 0, width: HUDMetric.box.width, height: height)
            bar.backgroundColor = HUDColor.panel
            bar.borderWidth = 1; bar.borderColor = HUDColor.ringField
        }
        var x: CGFloat = 6
        for hex in ProjectionSettings.palette {
            let name = String(format: "%06X", hex)
            let swatch = HUDSwatch(id: "color:" + name, label: L10n.text("Pen color", "画笔颜色") + " #" + name,
                                   color: HUDColor.rgb(hex), size: 22, env: env)
            swatch.onActivate = { [weak self] in self?.change { $0.color = hex; $0.erasing = false } }
            binders.append { [weak self, weak swatch] in guard let self else { return }; swatch?.isSelected = self.settings.values.color == hex }
            HUDMotion.perform { swatch.layer.frame.origin = CGPoint(x: x, y: (height - 22) / 2); bar.addSublayer(swatch.layer) }
            page.add(swatch)
            x += 22 + 6
        }
        // Any other colour: the shared custom-colour picker (wheel + brightness).
        let custom = HUDSwatch(id: "color:custom", label: L10n.text("Custom color", "自定义颜色"), color: nil, size: 22, env: env)
        custom.onActivate = { [weak self, weak custom] in guard let self, let custom else { return }; self.openColorPicker(anchor: custom) }
        binders.append { [weak self, weak custom] in
            guard let self else { return }; custom?.isSelected = !ProjectionSettings.palette.contains(self.settings.values.color)
        }
        HUDMotion.perform { custom.layer.frame.origin = CGPoint(x: x, y: (height - 22) / 2); bar.addSublayer(custom.layer) }
        page.add(custom)
        x += 22 + 6
        let divider = HUDLayer.plain("projection.divider")
        HUDMotion.perform {
            divider.frame = CGRect(x: x + 4, y: (height - 22) / 2, width: 1, height: 22)
            divider.backgroundColor = HUDColor.ringIcon
            bar.addSublayer(divider)
        }
        x += 4 + 1 + 4 + 6
        let tools: [(String, String, HUDIcon, (ProjectionSettings.Values) -> Bool, () -> Void)] = [
            ("pen", L10n.text("Pen", "画笔"), .pen, { !$0.erasing }, { [weak self] in self?.change { $0.erasing = false } }),
            ("eraser", L10n.text("Eraser", "橡皮擦"), .eraser, { $0.erasing }, { [weak self] in self?.change { $0.erasing = true } }),
            ("background", L10n.text("Background", "背景"), .grid, { $0.background }, { [weak self] in self?.change { $0.background.toggle() } }),
            ("media", L10n.text("Insert media", "插入媒体"), .image, { _ in false }, { [weak self] in self?.insertMedia() })
        ]
        for (id, label, icon, on, action) in tools {
            let button = HUDIconButton(id: id, label: label, icon: icon, env: env)
            button.onActivate = action
            binders.append { [weak self, weak button] in guard let self else { return }; button?.isOn = on(self.settings.values) }
            HUDMotion.perform { button.layer.frame.origin = CGPoint(x: x, y: (height - 30) / 2); bar.addSublayer(button.layer) }
            page.add(button)
            x += 30 + 6
        }
        let clear = HUDButton(id: "clear", title: L10n.text("Clear all", "全部清除"), icon: .trash, kind: .secondary, size: .small, env: env)
        clear.place(at: CGPoint(x: HUDMetric.box.width - 6 - clear.preferredWidth, y: (height - HUDButton.Size.small.height) / 2))
        clear.onActivate = { [weak self] in self?.confirmClear() }
        binders.append { [weak self, weak clear] in guard let self else { return }; clear?.isEnabled = self.settings.hasDrawing }
        HUDMotion.perform { bar.addSublayer(clear.layer) }
        page.add(clear)
        binders.forEach { $0() }
        env.applyScale(to: bar)
        return bar
    }

    private func openColorPicker(anchor: HUDControl) {
        if isPopoverOpen { closePopover(); return }
        let picker = NotesFormatBar.colorPicker(id: "penColor", title: L10n.text("PEN COLOR", "画笔颜色"), color: settings.values.color,
                                                env: env, bubbleHost: top) { [weak self] hex in
            self?.change { $0.color = hex; $0.erasing = false }
        }
        openPopover(picker.popover, anchor: anchor, controls: picker.controls)
    }

    private func insertMedia() {
        settings.requestMediaInsert()
        onOpen?()
    }

    private func confirmClear() {
        confirm(HUDConfirmRequest(id: "projectionClear", title: L10n.text("Clear the projection canvas?", "清空投影画布？"),
                                  message: L10n.text("Source files stay intact.", "源文件保持不变。"),
                                  confirmTitle: L10n.text("Clear all", "全部清除"), danger: true, icon: .trash)) { [weak self] in
            guard let self else { return }
            self.settings.clearDrawing()
            self.refresh()
            self.toast(icon: .trash, "Canvas cleared", "画布已清空")
        }
    }

    // MARK: Artwork

    /// `M40 110 C120 20, 200 140, 290 60 S 450 40, 540 100` in the preview's 590 × 150 space.
    static let samplePath: CGPath = {
        let path = CGMutablePath()
        path.move(to: CGPoint(x: 40, y: 110))
        path.addCurve(to: CGPoint(x: 290, y: 60), control1: CGPoint(x: 120, y: 20), control2: CGPoint(x: 200, y: 140))
        path.addCurve(to: CGPoint(x: 540, y: 100), control1: CGPoint(x: 380, y: -20), control2: CGPoint(x: 450, y: 40))
        return path
    }()

    /// radial-gradient(circle, #555 1px, transparent 1.5px) 0 0/14px 14px.
    static func dotPath(_ size: CGSize) -> CGPath {
        let path = CGMutablePath()
        var y: CGFloat = 7
        while y < size.height {
            var x: CGFloat = 7
            while x < size.width { path.addEllipse(in: CGRect(x: x - 1.25, y: y - 1.25, width: 2.5, height: 2.5)); x += 14 }
            y += 14
        }
        return path
    }

    /// repeating-conic-gradient(#1c1c1c 0 25%, #232323 0 50%) 0 0/20px 20px:
    /// the top-left and bottom-right quarter of every 20 px tile are lighter.
    static func checkerPath(_ size: CGSize) -> CGPath {
        let path = CGMutablePath()
        var y: CGFloat = 0
        while y < size.height {
            var x: CGFloat = 0
            while x < size.width {
                path.addRect(CGRect(x: x, y: y, width: 10, height: 10))
                path.addRect(CGRect(x: x + 10, y: y + 10, width: 10, height: 10))
                x += 20
            }
            y += 20
        }
        return path
    }
}
