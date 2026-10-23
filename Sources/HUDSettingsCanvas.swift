import AppKit
import QuartzCore

/// The four left-navigation screens of the redesign (spec §3.1–§3.5):
/// System (with its Modules page), Display (with its Battery alert and App icon pages),
/// Hotkeys and About. Preferences and platform side effects belong to
/// HUDSettingsController; this canvas only presents and edits them.
final class HUDSettingsCanvas: HUDDesignCanvas {
    var onChooseColor: ((NSColor) -> Void)?
    var onChooseLogo: (() -> Void)?
    var onLogoSelection: (() -> Void)?
    var onCaptureChanged: ((Bool) -> Void)?
    private let controller: HUDSettingsController
    private let displayProvider: () -> [HUDDisplayDescriptor]
    private var displays: [HUDDisplayDescriptor] = []
    private var observer: UUID?
    private var screenObserver: NSObjectProtocol?
    private enum Section: Equatable { case main, battery, icons, credits, modules }
    private var section: Section = .main
    /// What the current page's layout depends on; anything else updates the
    /// existing controls in place (toggles keep their knob animation).
    private struct Structure: Equatable {
        let section: Section, language: AppLanguage, languageSetting: AppLanguage, displays: [HUDDisplayDescriptor]
        let capturing: Bool, icon: HUDApplicationIcon, accent: String
        let updatePhase: HUDUpdateState.Phase, latest: String?
    }
    private var renderedStructure: Structure?
    /// Updates of the visible controls from the current configuration.
    private var binders: [() -> Void] = []
    private var stagedLayout: (scale: Double?, x: Double?, y: Double?) = (nil, nil, nil)
    private var layoutPromptOpen = false
    private var revertedScaleText: String?
    private var lastStatus: String?, lastStatusLanguage: AppLanguage?
    private var checkProgress: Int?
    private var checkTimer: DispatchWorkItem?
    private var updateCells: HUDCells?
    private var updateText: HUDTextLayer?
    private weak var updateSpoken: HUDReadout?
    private var updateStamp: CALayer?
    private var keycaps: HUDKeycapGroup?
    /// System > Modules: one row per right-hand module, keyed by module.
    private struct ModuleRow { let plate: HUDRowPlate, grip: HUDDragGrip, glyph: CALayer, toggle: HUDToggle, up: HUDIconButton, down: HUDIconButton }
    private var moduleRows: [HUDModule: ModuleRow] = [:]
    private weak var modulesScroll: HUDScrollArea?
    /// The row being dragged: its grab offset and the slot it would drop into.
    private var moduleDrag: (module: HUDModule, grab: CGFloat, slot: Int)?
    var isCapturingShortcut: Bool { module == .hotkeys && controller.isCapturingShortcut }
    private var config: AppConfiguration { controller.configuration }
    var customLogoRevision: String? { config.centerLogoRevision }
    var selectedLogo: HUDCenterLogo { config.centerLogo }
    override var isInputLocked: Bool { super.isInputLocked || isCapturingShortcut }

    init(module: HUDModule, controller: HUDSettingsController, context: HUDDesignContext,
         displayProvider: @escaping () -> [HUDDisplayDescriptor] = { HUDDisplayPolicy.connectedDisplays() }) {
        precondition([HUDModule.system, .display, .hotkeys, .about].contains(module))
        self.controller = controller; self.displayProvider = displayProvider
        super.init(module: module, context: context)
        displays = module == .system ? displayProvider() : []
        if module == .system {
            screenObserver = NotificationCenter.default.addObserver(forName: NSApplication.didChangeScreenParametersNotification,
                object: nil, queue: .main) { [weak self] _ in
                guard let self else { return }
                self.displays = self.displayProvider()
                self.controllerChanged()
            }
        }
        observer = controller.addObserver { [weak self] in self?.controllerChanged() }
    }

    deinit {
        checkTimer?.cancel()
        if let screenObserver { NotificationCenter.default.removeObserver(screenObserver) }
        if let observer { controller.removeObserver(observer) }
        if isCapturingShortcut { controller.endShortcutCapture(); onCaptureChanged?(false) }
    }

    // MARK: Lifecycle

    override func didActivate() {
        lastStatus = currentStatus
        controllerChanged()
    }

    override func didDeactivate() {
        cancelCapture()
        // An interrupted drag never creates a preview after the HUD has gone;
        // an unanswered layout preview is reverted with its prompt.
        stagedLayout = (nil, nil, nil)
        if layoutPromptOpen { layoutPromptOpen = false; controller.revertLayout() }
        if section != .main, module != .display || section != .battery { section = .main }
        checkTimer?.cancel(); checkTimer = nil; checkProgress = nil
    }

    override func willLeave() { didDeactivate() }

    private var structure: Structure {
        Structure(section: section, language: L10n.resolvedLanguage, languageSetting: config.language, displays: displays, capturing: isCapturingShortcut,
                  icon: config.applicationIcon,
                  accent: config.accentHex, updatePhase: module == .about ? controller.updateState.phase : .idle,
                  latest: module == .about ? controller.updateState.latestVersion : nil)
    }

    private func controllerChanged() {
        guard renderedLanguage != nil else { return }
        if structure != renderedStructure { rerender() } else { binders.forEach { $0() } }
        guard isActive else { return }
        updateLayoutPrompt()
        // A language change re-localizes the same status: only a new one toasts.
        let status = currentStatus, language = L10n.resolvedLanguage
        if let status, status != lastStatus, language == lastStatusLanguage { toast(icon: .warn, "Notice", "提示", detail: status) }
        lastStatus = status; lastStatusLanguage = language
    }

    private var currentStatus: String? {
        switch module {
        case .system: return controller.loginStatus ?? controller.status
        case .hotkeys: return controller.shortcutStatus ?? controller.status
        case .about: return controller.updateState.phase == .failed ? controller.updateState.detail : nil
        default: return controller.status
        }
    }

    /// Opened from the Power screen's battery link.
    func showBatterySettings() {
        guard module == .display else { return }
        settleSwap(); section = .battery
        if renderedLanguage != nil { rerender() }
    }

    func setCustomColor(_ color: NSColor) {
        guard let rgb = color.usingColorSpace(.sRGB) else { return }
        let hex = String(format: "%02X%02X%02X", Int((rgb.redComponent * 255).rounded()),
                         Int((rgb.greenComponent * 255).rounded()), Int((rgb.blueComponent * 255).rounded()))
        controller.update { $0.accentHex = hex }
    }

    func setCustomLogo(revision: String) {
        controller.update { $0.centerLogo = .custom; $0.centerLogoRevision = revision }
    }

    func showImportError(_ error: String) { toast(icon: .warn, "Import failed", "导入失败", detail: error) }

    // MARK: Pages

    override func buildPage(_ page: HUDDesignPage) {
        binders = []; updateCells = nil; updateText = nil; updateStamp = nil; keycaps = nil
        moduleRows = [:]; modulesScroll = nil; moduleDrag = nil
        renderedStructure = structure
        switch (module, section) {
        case (.system, .modules): buildModules(page)
        case (.system, _): buildSystem(page)
        case (.display, .battery): buildBattery(page)
        case (.display, .icons): buildIcons(page)
        case (.display, _): buildDisplay(page)
        case (.hotkeys, _): buildHotkeys(page)
        case (.about, .credits): buildCredits(page)
        default: buildAbout(page)
        }
    }

    private func go(_ next: Section, forward: Bool) {
        section = next
        navigate(dx: forward ? 1 : -1)
    }

    private func buildSystem(_ page: HUDDesignPage) {
        let c = config
        var rows: [HUDRowPlate] = []
        let language = dropdown("language", L10n.text("Language", "语言"), languageTitle(c.language),
                                language: HUDTextLanguage.tag(naming: c.language)) { [weak self] anchor in
            guard let self else { return }
            let items = AppLanguage.allCases.map { HUDMenu.Item(id: $0.rawValue, title: self.languageTitle($0),
                language: HUDTextLanguage.tag(naming: $0), selected: $0 == self.config.language) }
            self.openMenu(id: "language", items: items, anchor: anchor) { item in
                guard let value = AppLanguage(rawValue: item.id) else { return }
                self.controller.update { $0.language = value }
            }
        }
        rows.append(row("language", L10n.text("Language", "语言"), [language], page))
        rows.append(toggleRow("login", L10n.text("Launch at login", "登录时启动"), \.launchAtLogin, page))
        rows.append(toggleRow("focus", L10n.text("Close when focus lost", "失去焦点时关闭"), \.closeOnFocusLost, page))
        let screen = dropdown("screen", L10n.text("Display", "显示器"),
                              HUDDisplayPolicy.selectionTitle(configuration: c, displays: displays)) { [weak self] anchor in
            self?.openDisplayMenu(anchor)
        }
        binders.append { [weak self, weak screen] in
            guard let self else { return }
            screen?.setValue(HUDDisplayPolicy.selectionTitle(configuration: self.config, displays: self.displays))
        }
        rows.append(row("screen", L10n.text("Display", "显示器"), [screen], page))
        // Battery alerts live on Display > Battery alert (its Display method).
        let shown = HUDChip(modulesCount, variant: .dark, env: env)
        let modulesTitle = L10n.text("Modules", "模块")
        let modules = navRow("modules", modulesTitle, page, cluster: [shown.layer]) { [weak self] in self?.go(.modules, forward: true) }
        modules.label = modulesTitle + ", " + modulesCount
        binders.append { [weak self, weak shown, weak modules] in
            guard let self else { return }
            shown?.setTitle(self.modulesCount); modules?.label = modulesTitle + ", " + self.modulesCount
        }
        rows.append(modules)
        let restore = HUDButton(id: "restore", title: L10n.text("Restore", "恢复"), icon: .reset, kind: .secondary, size: .small, env: env)
        restore.place(at: .zero)
        restore.onActivate = { [weak self] in self?.confirmRestore() }
        rows.append(row("restore", L10n.text("Restore default settings", "恢复默认设置"), [restore.layer], page, controls: [restore]))
        stack(rows, in: page.layer, page: page, top: HUDMetric.headerHeight)
        header.configure(crumbs: ["SYSTEM"], code: "SYS-01", title: HUDModuleTitle("System", "系统设置"),
                         counter: HUDFormat.pad2(rows.count), showsBack: false)
    }

    private func openDisplayMenu(_ anchor: HUDControl) {
        displays = displayProvider()
        let c = config
        var items = [
            HUDMenu.Item(id: "pointer", title: L10n.text("Pointer display", "鼠标所在显示器"), selected: c.hudDisplayUUID == nil && c.openOnActiveDisplay),
            HUDMenu.Item(id: "main", title: L10n.text("Main display", "主显示器"), selected: c.hudDisplayUUID == nil && !c.openOnActiveDisplay)
        ]
        for (index, display) in displays.enumerated() {
            let duplicate = displays.filter { $0.name == display.name }.count > 1
            items.append(HUDMenu.Item(id: display.uuid, title: duplicate ? "\(display.name) / \(index + 1)" : display.name,
                                      selected: c.hudDisplayUUID == display.uuid, separatorBefore: index == 0))
        }
        if let uuid = c.hudDisplayUUID, !displays.contains(where: { $0.uuid == uuid }) {
            items.append(HUDMenu.Item(id: uuid, title: HUDDisplayPolicy.selectionTitle(configuration: c, displays: displays),
                                      selected: true, separatorBefore: true, enabled: false))
        }
        openMenu(id: "screen", items: items, anchor: anchor) { [weak self] item in
            guard let self else { return }
            let value = item.id
            guard value == "pointer" || value == "main" || self.displays.contains(where: { $0.uuid == value }) else { return }
            self.controller.update { c in
                c.hudDisplayUUID = value == "pointer" || value == "main" ? nil : value
                c.hudDisplayName = self.displays.first(where: { $0.uuid == value })?.name
                c.openOnActiveDisplay = value != "main"
            }
        }
    }

    private func confirmRestore() {
        confirm(HUDConfirmRequest(id: "restore", title: L10n.text("Restore default settings?", "恢复默认设置？"),
                                  message: L10n.text("Notes, files and shortcuts are kept.", "保留便笺、文件与应用快捷方式。"),
                                  confirmTitle: L10n.text("Restore", "恢复"), icon: .reset)) { [weak self] in
            self?.controller.restoreDefaults()
            self?.toast(icon: .reset, "Settings restored", "已恢复默认设置")
        }
    }

    // MARK: Modules (System > Modules)

    private var modulesCount: String {
        let layout = config.moduleLayout
        return "\(layout.visible.count) / \(layout.order.count)"
    }
    private static let modulePitch = HUDMetric.rowHeight + HUDMetric.rowGap

    /// Order and visibility of the right-hand list: drag a row by its grip
    /// (the others reflow) or use its move buttons; the switch shows or hides it.
    private func buildModules(_ page: HUDDesignPage) {
        let layout = config.moduleLayout, width = rowWidth(page)
        let scroll = page.scrollArea(id: "modules", frame: CGRect(x: 0, y: HUDMetric.headerHeight, width: HUDMetric.box.width, height: HUDMetric.bodyHeight))
        modulesScroll = scroll
        var plates: [HUDRowPlate] = []
        for module in layout.order {
            let title = module.title
            let grip = HUDDragGrip(id: "grip:" + module.rawValue, size: CGSize(width: 20, height: 30), dotsAt: CGPoint(x: 7, y: 10), env: env)
            grip.onDrag = { [weak self] phase, point in self?.dragModule(module, phase, at: point) }
            let up = HUDIconButton(id: "up:" + module.rawValue, label: title + ", " + L10n.text("Move up", "上移"),
                                   icon: .left, style: .light, side: 26, iconSize: 13, env: env)
            let down = HUDIconButton(id: "down:" + module.rawValue, label: title + ", " + L10n.text("Move down", "下移"),
                                     icon: .right, style: .light, side: 26, iconSize: 13, env: env)
            // The back/forward chevrons, turned to point up and down.
            HUDMotion.perform {
                up.layer.setAffineTransform(CGAffineTransform(rotationAngle: .pi / 2))
                down.layer.setAffineTransform(CGAffineTransform(rotationAngle: .pi / 2))
            }
            up.onActivate = { [weak self] in self?.moveModule(module, by: -1) }
            down.onActivate = { [weak self] in self?.moveModule(module, by: 1) }
            let toggle = HUDToggle(id: "show:" + module.rawValue, label: title, isOn: !layout.isHidden(module), env: env)
            toggle.onChange = { [weak self] on in self?.controller.update { $0.moduleLayout.setHidden(module, !on) } }
            let plate = row("module:" + module.rawValue, title, [up.layer, down.layer, toggle.layer], page, controls: [grip, up, down, toggle])
            let glyph = HUDNavigationEntry.glyphLayer(for: module, side: 24, ink: NSColor(white: 0.12, alpha: 1), scale: env.renderScale)
            HUDMotion.perform {
                plate.titleInset = 56
                let middle = 10 + (plate.layer.bounds.height - 18) / 2 + 1
                grip.layer.position = CGPoint(x: 26, y: middle)
                glyph.position = CGPoint(x: 54, y: middle)
                plate.layer.addSublayer(glyph); plate.layer.addSublayer(grip.layer)
            }
            moduleRows[module] = ModuleRow(plate: plate, grip: grip, glyph: glyph, toggle: toggle, up: up, down: down)
            plates.append(plate)
        }
        let restore = HUDButton(id: "modulesReset", title: L10n.text("Restore", "恢复"), icon: .reset, kind: .secondary, size: .small, env: env)
        restore.place(at: .zero)
        restore.onActivate = { [weak self] in self?.confirmModulesReset() }
        plates.append(row("modulesReset", L10n.text("Restore default layout", "恢复默认布局"), [restore.layer], page, controls: [restore]))
        scroll.setContentHeight(stack(plates, in: scroll.content, page: page, top: 0, width: width))
        page.restoreOffset(of: scroll)
        renderModuleStates()
        binders.append { [weak self, weak restore] in
            guard let self else { return }
            self.renderModuleStates()
            restore?.isEnabled = !self.config.moduleLayout.isDefault
            // A drag in progress owns the positions until it drops.
            if self.moduleDrag == nil { self.layoutModuleRows(self.config.moduleLayout.order, animated: true) }
        }
        restore.isEnabled = !layout.isDefault
        header.configure(crumbs: ["SYSTEM", "MODULES"], code: "SYS-01", title: HUDModuleTitle("Modules", "模块"),
                         counter: HUDFormat.pad2(layout.order.count), showsBack: true)
        wireBack(page) { [weak self] in self?.go(.main, forward: false) }
    }

    /// Switches, move buttons and the hidden rows' dimmed glyph and title.
    private func renderModuleStates() {
        let layout = config.moduleLayout
        for (index, module) in layout.order.enumerated() {
            guard let row = moduleRows[module] else { continue }
            let hidden = layout.isHidden(module)
            row.toggle.setOn(!hidden, animated: true)
            // The last visible module cannot be hidden.
            row.toggle.isEnabled = layout.canSetHidden(module, true)
            row.up.isEnabled = index > 0
            row.down.isEnabled = index < layout.order.count - 1
            HUDMotion.animate(row.glyph, "opacity", to: Float(hidden ? 0.35 : 1), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        }
    }

    /// Places each row at its slot of `order`; the dragged row follows the pointer instead.
    private func layoutModuleRows(_ order: [HUDModule], animated: Bool) {
        for (index, module) in order.enumerated() where module != moduleDrag?.module {
            guard let plate = moduleRows[module]?.plate.layer else { continue }
            let y = CGFloat(index) * Self.modulePitch + plate.bounds.height / 2
            guard plate.position.y != y else { continue }
            if animated { HUDMotion.animate(plate, "position.y", to: y, duration: 0.24, timing: HUDEasing.snap, env: env) }
            else { HUDMotion.perform { plate.position.y = y } }
        }
    }

    private func moveModule(_ module: HUDModule, by offset: Int) {
        guard let index = config.moduleLayout.order.firstIndex(of: module) else { return }
        controller.update { $0.moduleLayout.move(module, to: index + offset) }
        guard let scroll = modulesScroll, let next = config.moduleLayout.order.firstIndex(of: module) else { return }
        // Keep the moved row in view.
        let rect = CGRect(x: 0, y: CGFloat(next) * Self.modulePitch, width: 1, height: HUDMetric.rowHeight)
        let target = rect.minY < scroll.offset ? rect.minY
            : (rect.maxY > scroll.offset + scroll.layer.bounds.height ? rect.maxY - scroll.layer.bounds.height : scroll.offset)
        if target != scroll.offset { scroll.setOffset(target, animated: true, env: env) }
    }

    private func dragModule(_ module: HUDModule, _ phase: HUDDragGrip.Phase, at point: CGPoint) {
        guard let scroll = modulesScroll, let row = moduleRows[module] else { return }
        let plate = row.plate.layer, order = config.moduleLayout.order
        let y = scroll.content.convert(point, from: row.grip.layer).y
        switch phase {
        case .began:
            guard let index = order.firstIndex(of: module) else { return }
            moduleDrag = (module, y - plate.frame.minY, index)
            HUDMotion.perform { plate.zPosition = 10 }
            HUDMotion.animate(plate, "opacity", to: Float(0.92), duration: HUDDuration.hover, env: env)
        case .moved:
            guard var drag = moduleDrag, drag.module == module else { return }
            // Near an edge the list follows the pointer (on pointer events only).
            let shown = y - scroll.offset, edge: CGFloat = 36
            if shown < edge { scroll.scroll(by: -min(12, edge - shown)) }
            else if shown > scroll.layer.bounds.height - edge { scroll.scroll(by: min(12, shown - scroll.layer.bounds.height + edge)) }
            let top = min(CGFloat(order.count - 1) * Self.modulePitch, max(0, y - drag.grab))
            HUDMotion.perform { plate.removeAnimation(forKey: "hud.position.y"); plate.position.y = top + plate.bounds.height / 2 }
            let slot = min(order.count - 1, max(0, Int((top / Self.modulePitch).rounded())))
            guard slot != drag.slot else { return }
            drag.slot = slot; moduleDrag = drag
            var preview = order.filter { $0 != module }
            preview.insert(module, at: slot)
            layoutModuleRows(preview, animated: true)
        case .ended:
            guard let drag = moduleDrag, drag.module == module else { return }
            moduleDrag = nil
            HUDMotion.animate(plate, "opacity", to: Float(1), duration: HUDDuration.hover, env: env)
            controller.update { $0.moduleLayout.move(module, to: drag.slot) }
            layoutModuleRows(config.moduleLayout.order, animated: true)
            HUDMotion.perform { plate.zPosition = 0 }
        }
    }

    private func confirmModulesReset() {
        confirm(HUDConfirmRequest(id: "modulesReset", title: L10n.text("Restore default layout?", "恢复默认布局？"),
                                  message: L10n.text("All modules are shown in their original order.", "所有模块按原顺序显示。"),
                                  confirmTitle: L10n.text("Restore", "恢复"), icon: .reset)) { [weak self] in
            self?.controller.update { $0.moduleLayout = HUDModuleLayout() }
            self?.toast(icon: .reset, "Layout restored", "已恢复默认布局")
        }
    }

    override func cursor(at point: CGPoint?) -> NSCursor? {
        if moduleDrag != nil { return .closedHand }
        guard isActive, section == .modules, let point else { return nil }
        return moduleRows.values.contains { row in
            row.grip.layer.isVisible(in: root) && row.grip.layer.isUnclipped(point, in: root)
                && row.grip.contains(row.grip.layer.convert(point, from: root))
        } ? .openHand : nil
    }

    private func buildDisplay(_ page: HUDDesignPage) {
        let c = config
        let scroll = page.scrollArea(id: "display", frame: CGRect(x: 0, y: HUDMetric.headerHeight, width: HUDMetric.box.width, height: 416))
        var rows: [HUDRowPlate] = []
        rows.append(sliderRow("uiScale", L10n.text("UI scale", "界面缩放"), value: stagedLayout.scale ?? c.hudScale,
                              range: HUDSlider.Range(minimum: 0.2, maximum: 2, step: 0.01), format: .scale, page: page,
                              read: { $0.hudScale }) { [weak self] value, final in
            guard let self else { return }
            if final { self.stagedLayout.scale = nil; self.controller.previewScale(value) } else { self.stagedLayout.scale = value }
        })
        rows.append(sliderRow("positionX", L10n.text("HUD position X", "界面位置 X"), value: (stagedLayout.x ?? c.hudOffsetX) * 100,
                              range: HUDSlider.Range(minimum: -50, maximum: 50, step: 1, zero: 0), format: .signedPercent, page: page,
                              read: { $0.hudOffsetX * 100 }) { [weak self] value, final in
            self?.stagePosition(x: value / 100, y: nil, final: final)
        })
        rows.append(sliderRow("positionY", L10n.text("HUD position Y", "界面位置 Y"), value: (stagedLayout.y ?? c.hudOffsetY) * 100,
                              range: HUDSlider.Range(minimum: -50, maximum: 50, step: 1, zero: 0), format: .signedPercent, page: page,
                              read: { $0.hudOffsetY * 100 }) { [weak self] value, final in
            self?.stagePosition(x: nil, y: value / 100, final: final)
        })
        rows.append(percentRow("parallax", L10n.text("Parallax intensity", "视差强度"), maximum: 200, \.parallaxIntensity, page))
        // Perspective drives the plane's tilt live while dragging (spec §1.3).
        rows.append(percentRow("perspective", L10n.text("Perspective intensity", "透视强度"), maximum: 200, \.perspectiveIntensity, page))
        rows.append(percentRow("darkness", L10n.text("Background brightness", "背景亮度"), maximum: 100, \.backgroundBrightness, page))
        rows.append(percentRow("blur", L10n.text("Blur amount", "模糊程度"), maximum: 100, \.blurAmount, page))
        let theme = dropdown("theme", L10n.text("Theme", "主题"), themeTitle(c.theme)) { [weak self] anchor in
            guard let self else { return }
            let items = OverlayTheme.allCases.map { HUDMenu.Item(id: $0.rawValue, title: self.themeTitle($0), selected: $0 == self.config.theme) }
            self.openMenu(id: "theme", items: items, anchor: anchor) { item in
                guard let value = OverlayTheme(rawValue: item.id) else { return }
                self.controller.update { $0.theme = value }
            }
        }
        binders.append { [weak self, weak theme] in guard let self else { return }; theme?.setValue(self.themeTitle(self.config.theme)) }
        rows.append(row("theme", L10n.text("Theme", "主题"), [theme], page))
        let formats = HUDClockFormat.allCases
        let format = HUDSegmentedControl(id: "clockFormat", label: L10n.text("Time format", "时间格式"),
                                         options: [L10n.text("24-hour", "24 小时制"), L10n.text("12-hour", "12 小时制")],
                                         selectedIndex: formats.firstIndex(of: c.clockFormat) ?? 0, width: 178, env: env)
        format.onChange = { [weak self] index in self?.controller.update { $0.clockFormat = formats[index] } }
        binders.append { [weak self, weak format] in
            guard let self else { return }; format?.select(formats.firstIndex(of: self.config.clockFormat) ?? 0, notify: false)
        }
        page.add(format) // The segments hold their group weakly.
        rows.append(row("clockFormat", L10n.text("Time format", "时间格式"), [format.layer], page, controls: format.segments))
        // The style's number and name in full; the menu lists all five.
        let style = dropdown("clockStyle", L10n.text("Clock style", "时钟样式"), clockStyleTitle(c.clockStyle)) { [weak self] anchor in
            guard let self else { return }
            let items = HUDClockStyle.allCases.map { HUDMenu.Item(id: $0.rawValue, title: self.clockStyleTitle($0), selected: $0 == self.config.clockStyle) }
            self.openMenu(id: "clockStyle", items: items, anchor: anchor) { item in
                guard let value = HUDClockStyle(rawValue: item.id) else { return }
                self.controller.update { $0.clockStyle = value }
            }
        }
        binders.append { [weak self, weak style] in guard let self else { return }; style?.setValue(self.clockStyleTitle(self.config.clockStyle)) }
        rows.append(row("clockStyle", L10n.text("Clock style", "时钟样式"), [style], page))
        rows.append(logoRow(page))
        rows.append(navRow("appIcon", L10n.text("App / menu bar icon", "应用 / 菜单栏图标"), page) { [weak self] in self?.go(.icons, forward: true) })
        rows.append(paletteRow(page))
        let reading = HUDChip(c.alertMetric.title.uppercased(), variant: .dark, env: env)
        let batteryTitle = L10n.text("Battery alert", "电池提醒")
        let batteryRow = navRow("battery", batteryTitle, page, cluster: [reading.layer]) { [weak self] in
            self?.go(.battery, forward: true)
        }
        batteryRow.label = batteryTitle + ", " + c.alertMetric.title // The chip's reading, spoken.
        binders.append { [weak self, weak reading, weak batteryRow] in
            guard let self else { return }
            reading?.setTitle(self.config.alertMetric.title.uppercased()); batteryRow?.label = batteryTitle + ", " + self.config.alertMetric.title
        }
        rows.append(batteryRow)
        rows.append(toggleRow("ambient", L10n.text("Ambient animation", "持续环境动画"), \.ambientAnimation, page))
        rows.append(toggleRow("motion", L10n.text("Reduce Motion", "减少动态效果"), \.reduceMotion, page))
        rows.append(toggleRow("lowPower", L10n.text("Low Power visual mode", "低功耗视觉模式"), \.lowPowerVisualMode, page))
        rows.append(toggleRow("recording", L10n.text("Recording mode", "录制模式"), \.recordingMode, page, changed: { [weak self] on in
            if on { self?.toast(icon: .rec, "Recording mode on", "私密内容已隐藏") }
            else { self?.toast(icon: .eye, "Recording mode off", "私密内容已恢复") }
        }))
        rows.append(toggleRow("bootLog", L10n.text("Daily boot log", "每日启动日志"), \.dailyBootLog, page))
        rows.append(toggleRow("facility", L10n.text("Facility alerts", "设施警报"), \.facilityAlerts, page,
                              confirmOff: { [weak self] commit in self?.confirmFacilityOff(commit) }))
        let height = stack(rows, in: scroll.content, page: page, top: 0, width: HUDMetric.box.width - 10)
        scroll.setContentHeight(height)
        page.restoreOffset(of: scroll)
        header.configure(crumbs: ["DISPLAY"], code: "SYS-02", title: HUDModuleTitle("Display", "显示"),
                         counter: HUDFormat.pad2(rows.count), showsBack: false)
    }

    /// Switching the facility alerts off asks first, in red: both answers
    /// shut the "defence system" down; only Esc or a press outside keeps it.
    private func confirmFacilityOff(_ commit: @escaping () -> Void) {
        let answer = L10n.text("Disable Shipwide Defense System", "关闭全舰防御系统")
        confirm(HUDConfirmRequest(id: "facilityOff", title: L10n.text("Are you sure you want to disable the Shipwide Defense System?", "是否关闭全舰防御系统？"),
                                  confirmTitle: answer, cancelTitle: answer, icon: .power, style: .alert, cancelPillConfirms: true),
                onConfirm: commit)
    }

    private func clockStyleTitle(_ style: HUDClockStyle) -> String {
        HUDFormat.pad2(style.index + 1) + "  " + style.title
    }

    private func stagePosition(x: Double?, y: Double?, final: Bool) {
        if let x { stagedLayout.x = x }
        if let y { stagedLayout.y = y }
        guard final else { return }
        let next = (x: stagedLayout.x ?? config.hudOffsetX, y: stagedLayout.y ?? config.hudOffsetY)
        stagedLayout.x = nil; stagedLayout.y = nil
        controller.previewPosition(x: next.x, y: next.y)
    }

    /// "Keep this UI scale?" (spec §2.18): the confirm bar with the 15 s countdown.
    private func updateLayoutPrompt() {
        let remaining = controller.layoutConfirmationRemaining
        if let remaining {
            if layoutPromptOpen { context.overlay.setCountdown(remaining); return }
            guard module == .display else { return }
            layoutPromptOpen = true
            let position = controller.isPositionPreviewPending && !controller.isScalePreviewPending
            let previous = HUDValueFormat.scale.string(controller.storedConfiguration.hudScale)
            let requested = HUDValueFormat.scale.string(config.hudScale)
            confirm(HUDConfirmRequest(id: "layout",
                                      title: position ? L10n.text("Keep this position?", "保留此位置？") : L10n.text("Keep this UI scale?", "保留此缩放？"),
                                      countdown: remaining, confirmTitle: L10n.text("Keep", "保留"),
                                      cancelTitle: L10n.text("Revert", "恢复"), icon: .check),
                    onConfirm: { [weak self] in
                        self?.layoutPromptOpen = false; self?.controller.confirmLayout()
                        if position { self?.toast(icon: .check, "Position kept", "已保留界面位置") }
                        else { self?.toast(icon: .check, "UI scale kept", "已保留界面缩放", detail: requested) }
                    },
                    onCancel: { [weak self] in self?.layoutPromptOpen = false; self?.controller.revertLayout() })
            revertedScaleText = position ? nil : previous
        } else if layoutPromptOpen {
            // The preview ended without an answer: it reverted itself.
            layoutPromptOpen = false
            context.overlay.dismissConfirm()
            if let previous = revertedScaleText { toast(icon: .reset, "UI scale reverted", "已恢复界面缩放", detail: previous) }
            else { toast(icon: .reset, "Position reverted", "已恢复界面位置") }
            lastStatus = currentStatus // This toast already reports the controller's restore status.
        }
    }

    private func paletteRow(_ page: HUDDesignPage) -> HUDRowPlate {
        var layers: [CALayer] = [], controls: [HUDControl] = []
        var swatches: [(String, HUDSwatch)] = []
        for hex in HUDSettingsController.presetAccentHexes {
            var c = config; c.accentHex = hex
            let swatch = HUDSwatch(id: "accent:" + hex, label: L10n.text("Theme color", "主题颜色") + " #" + hex,
                                   color: c.accentColor.cgColor, env: env)
            swatch.isSelected = config.accentHex == hex
            swatch.onActivate = { [weak self] in self?.controller.update { $0.accentHex = hex } }
            layers.append(swatch.layer); controls.append(swatch); swatches.append((hex, swatch))
        }
        let custom = HUDSwatch(id: "customColor", label: L10n.text("Custom color wheel", "自定义色轮"), color: nil, env: env)
        custom.isSelected = !HUDSettingsController.presetAccentHexes.contains(config.accentHex)
        custom.onActivate = { [weak self] in guard let self else { return }; self.onChooseColor?(self.config.accentColor) }
        layers.append(custom.layer); controls.append(custom)
        return row("palette", L10n.text("Theme color", "主题颜色"), layers, page, controls: controls)
    }

    private func buildBattery(_ page: HUDDesignPage) {
        let c = config
        var rows: [HUDRowPlate] = []
        let reading = dropdown("metric", L10n.text("Reading", "显示数据"), c.alertMetric.title) { [weak self] anchor in
            guard let self else { return }
            let items = HUDChargeMetric.allCases.map { HUDMenu.Item(id: $0.rawValue, title: $0.title, selected: $0 == self.config.alertMetric) }
            self.openMenu(id: "metric", items: items, anchor: anchor) { item in
                guard let value = HUDChargeMetric(rawValue: item.id) else { return }
                self.controller.update { $0.alertMetric = value }
            }
        }
        binders.append { [weak self, weak reading] in guard let self else { return }; reading?.setValue(self.config.alertMetric.title) }
        rows.append(row("metric", L10n.text("Reading", "显示数据"), [reading], page))
        let method = dropdown("method", L10n.text("Display method", "显示方式"), methodTitle(shownDisplayMode)) { [weak self] anchor in
            guard let self else { return }
            let modes: [DisplayMode] = [.whenChargingStarts, .always, .never]
            let items = modes.map { HUDMenu.Item(id: $0.rawValue, title: self.methodTitle($0), selected: $0 == self.shownDisplayMode) }
            self.openMenu(id: "method", items: items, anchor: anchor) { item in
                guard let value = DisplayMode(rawValue: item.id), value != self.shownDisplayMode else { return }
                // This menu is now the only alert switch: any choice also lifts
                // an "alerts off" saved by the former System toggle (or v1.2.0).
                self.controller.update { $0.displayMode = value; $0.batteryAlertsEnabled = true }
                if value == .never { self.toast(icon: .eye, "Bar hidden", "不再自动显示") }
            }
        }
        binders.append { [weak self, weak method] in guard let self else { return }; method?.setValue(self.methodTitle(self.shownDisplayMode)) }
        rows.append(row("method", L10n.text("Display method", "显示方式"), [method], page))
        rows.append(sliderRow("duration", L10n.text("Display duration", "显示时长"), value: c.displayDuration,
                              range: HUDSlider.Range(minimum: 1, maximum: 60, step: 1), format: .seconds, page: page,
                              read: { $0.displayDuration }) { [weak self] value, _ in
            self?.controller.update { $0.displayDuration = value }
        })
        let placement = dropdown("placement", L10n.text("Display position", "显示位置"), placementTitle(c.placement)) { [weak self] anchor in
            guard let self else { return }
            let items = OverlayPlacement.allCases.map { HUDMenu.Item(id: $0.rawValue, title: self.placementTitle($0), selected: $0 == self.config.placement) }
            self.openMenu(id: "placement", items: items, anchor: anchor) { item in
                guard let value = OverlayPlacement(rawValue: item.id) else { return }
                self.controller.update { $0.placement = value }
            }
        }
        binders.append { [weak self, weak placement] in guard let self else { return }; placement?.setValue(self.placementTitle(self.config.placement)) }
        rows.append(row("placement", L10n.text("Display position", "显示位置"), [placement], page))
        rows.append(navRow("editPosition", L10n.text("Edit position", "编辑位置"), page, chevron: "↗") { [weak self] in
            self?.controller.editBatteryPosition()
        })
        rows.append(sliderRow("batterySize", L10n.text("Size", "大小"), value: c.scale * 100,
                              range: HUDSlider.Range(minimum: 65, maximum: 160, step: 1), format: .percent, page: page,
                              read: { $0.scale * 100 }) { [weak self] value, _ in
            self?.controller.update { $0.scale = value / 100 }
        })
        stack(rows, in: page.layer, page: page, top: HUDMetric.headerHeight)
        header.configure(crumbs: ["DISPLAY", "BATTERY ALERT"], code: "SYS-02", title: HUDModuleTitle("Battery alert", "电池提醒"),
                         counter: HUDFormat.pad2(rows.count), showsBack: true)
        wireBack(page) { [weak self] in self?.go(.main, forward: false) }
    }

    /// Alerts switched off (the former System toggle, also in v1.2.0) read as Don't display.
    private var shownDisplayMode: DisplayMode { config.batteryAlertsEnabled ? config.displayMode : .never }

    // MARK: Center logo (an icon grid; the field shows the logo in use)

    private static let logoTile = CGSize(width: 120, height: 44), logoFieldHeight: CGFloat = 16
    /// Chooser and field rasters by logo, custom revision, size and scale.
    private static var logoRasters: [String: CGImage] = [:]

    private func logoRow(_ page: HUDDesignPage) -> HUDRowPlate {
        let title = L10n.text("Center logo", "中心标志")
        let field = dropdown("centerLogo", title, config.centerLogo.title) { [weak self] anchor in self?.openLogoMenu(anchor) }
        let show = { [weak self, weak field] in
            guard let self, let field else { return }
            field.setValue(self.config.centerLogo.title)
            field.setArtwork(self.logoArtwork(self.config.centerLogo, fitting: CGSize(width: 130, height: Self.logoFieldHeight)),
                             height: Self.logoFieldHeight)
        }
        show()
        binders.append(show)
        return row("centerLogo", title, [field], page)
    }

    /// The four preset logos as tiles (a full grid: an imported logo has no
    /// tile, the field shows it), then the custom-logo import.
    private func openLogoMenu(_ anchor: HUDDropdown) {
        let box = CGRect(origin: .zero, size: Self.logoTile).insetBy(dx: 8, dy: 8)
        var items: [HUDMenu.Item] = HUDCenterLogo.allCases.compactMap { logo in
            // The presets stay offered even if their artwork is unreadable.
            guard logo != .custom else { return nil }
            let image = logoArtwork(logo, fitting: box.size)
            return HUDMenu.Item(id: logo.rawValue, title: logo.title, selected: config.centerLogo == logo,
                                icon: HUDMenu.Icon(image: image, size: Self.logoTile, frame: box))
        }
        items.append(HUDMenu.Item(id: "import", title: L10n.text("Choose custom logo", "选择自定义图标")))
        openMenu(id: "logo", items: items, anchor: anchor) { [weak self] item in
            guard let self else { return }
            if item.id == "import" { self.onChooseLogo?(); return }
            guard let logo = HUDCenterLogo(rawValue: item.id) else { return }
            self.onLogoSelection?()
            self.controller.update { $0.centerLogo = logo }
        }
    }

    /// The logo as the centre shows it (trimmed, with its glow), drawn once at `size`.
    private func logoArtwork(_ logo: HUDCenterLogo, fitting size: CGSize) -> CGImage? {
        let revision = logo == .custom ? customLogoRevision : nil
        let key = "\(logo.rawValue):\(revision ?? ""):\(size.width)x\(size.height)@\(env.renderScale)"
        if let cached = Self.logoRasters[key] { return cached }
        guard let source = logo == .endfield ? Self.endfieldWordmark() : logo.image(revision: revision),
              let presented = HUDCenterLogoPresentation.image(source),
              let image = HUDMenu.Icon.raster(presented, fitting: size, scale: env.renderScale) else { return nil }
        // Only the current custom revision stays.
        if logo == .custom { Self.logoRasters = Self.logoRasters.filter { !$0.key.hasPrefix("custom:") || $0.key.hasPrefix("custom:\(revision ?? ""):") } }
        Self.logoRasters[key] = image
        return image
    }

    /// The default ENDFIELD INDUSTRIES lettering: the band of the supplied
    /// artwork the centre shows (the triangle artwork falls outside it).
    private static func endfieldWordmark() -> CGImage? {
        guard let url = HUDResources.url(for: "EndfieldIndustriesSource.png"),
              let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }
        let unit = CGFloat(image.width) / 430
        return image.cropping(to: CGRect(x: 0, y: 142 * unit, width: CGFloat(image.width), height: 83 * unit).integral)
    }

    private func buildIcons(_ page: HUDDesignPage) {
        let icons = HUDApplicationIcon.pickerCases
        let columns = 4, gap: CGFloat = 10, size = (HUDMetric.box.width - 10 - CGFloat(columns - 1) * gap) / CGFloat(columns)
        let scroll = page.scrollArea(id: "icons", frame: CGRect(x: 0, y: HUDMetric.headerHeight, width: HUDMetric.box.width, height: HUDMetric.bodyHeight))
        for (index, icon) in icons.enumerated() {
            let tile = HUDIconTile(id: "appIcon:" + icon.rawValue, icon: icon, size: CGSize(width: size, height: 96), env: env)
            tile.isSelected = config.applicationIcon == icon
            tile.onActivate = { [weak self] in self?.controller.update { $0.applicationIcon = icon } }
            HUDMotion.perform {
                tile.layer.frame.origin = CGPoint(x: CGFloat(index % columns) * (size + gap), y: CGFloat(index / columns) * (96 + gap))
                scroll.content.addSublayer(tile.layer)
            }
            page.add(tile); page.stagger(tile.layer)
        }
        let rowCount = (icons.count + columns - 1) / columns
        scroll.setContentHeight(CGFloat(rowCount) * (96 + gap) - gap)
        page.restoreOffset(of: scroll)
        header.configure(crumbs: ["DISPLAY", "APP ICON"], code: "SYS-02", title: HUDModuleTitle("App / menu bar icon", "应用 / 菜单栏图标"),
                         counter: HUDFormat.pad2(icons.count), showsBack: true)
        wireBack(page) { [weak self] in self?.go(.main, forward: false) }
    }

    private func buildHotkeys(_ page: HUDDesignPage) {
        let c = config
        var rows: [HUDRowPlate] = []
        let summon = HUDKeycapGroup(id: "capture", label: L10n.text("Summon HUD", "呼出界面") + ", " + c.summonShortcut.displayName,
                                    keys: Self.keycaps(c.summonShortcut), interactive: true, env: env)
        summon.onActivate = { [weak self] in
            guard let self else { return }
            if self.isCapturingShortcut { self.cancelCapture() } else { self.controller.beginShortcutCapture(); self.onCaptureChanged?(true) }
        }
        if isCapturingShortcut { summon.setRecording(true, prompt: L10n.text("Press a shortcut", "请按快捷键")) }
        keycaps = summon
        binders.append { [weak self, weak summon] in
            guard let self, let summon, !self.isCapturingShortcut else { return }
            summon.setKeys(Self.keycaps(self.config.summonShortcut))
        }
        rows.append(row("capture", L10n.text("Summon HUD", "呼出界面"), [summon.layer], page, controls: [summon]))
        rows.append(toggleRow("hotkeyEnabled", L10n.text("Open with hotkey", "使用快捷键打开"), \.summonHotkeyEnabled, page))
        let defaults = HUDKeycapGroup(id: "keyDefault", label: SummonShortcut.default.displayName,
                                      keys: Self.keycaps(.default), interactive: false, minimumWidth: 0, env: env)
        let restore = HUDButton(id: "keyReset", title: L10n.text("Restore", "恢复"), icon: .reset, kind: .secondary, size: .small, env: env)
        restore.place(at: .zero)
        restore.onActivate = { [weak self] in
            guard let self else { return }
            self.cancelCapture()
            if let error = self.controller.setShortcut(.default) { self.toast(icon: .warn, "Hotkey unavailable", "快捷键不可用", detail: error) }
            else { self.toast(icon: .check, "Hotkey restored", "已恢复默认快捷键") }
        }
        rows.append(row("keyReset", L10n.text("Restore default hotkey", "恢复默认快捷键"), [defaults.layer, restore.layer], page, controls: [restore]))
        stack(rows, in: page.layer, page: page, top: HUDMetric.headerHeight)
        header.configure(crumbs: ["HOTKEYS"], code: "SYS-03", title: HUDModuleTitle("Hotkeys", "快捷键"),
                         counter: HUDFormat.pad2(rows.count), showsBack: false)
    }

    /// Keycap labels in the Mac's order: CTRL, OPT, SHIFT, CMD, then the key.
    static func keycaps(_ shortcut: SummonShortcut) -> [String] {
        var keys: [String] = []
        if shortcut.modifiers.contains(.control) { keys.append("CTRL") }
        if shortcut.modifiers.contains(.option) { keys.append("OPT") }
        if shortcut.modifiers.contains(.shift) { keys.append("SHIFT") }
        if shortcut.modifiers.contains(.command) { keys.append("CMD") }
        keys.append(shortcut.displayName.components(separatedBy: " + ").last?.uppercased() ?? "")
        return keys
    }

    override func interceptKey(_ event: NSEvent) -> Bool { capture(event) }

    /// Live capture (spec §3.4): Esc cancels, modifier-only presses are
    /// ignored, a modifier plus a key saves.
    @discardableResult func capture(_ event: NSEvent) -> Bool {
        guard isCapturingShortcut else { return false }
        if event.keyCode == 53 { cancelCapture(); return true }
        guard !event.isARepeat else { return true }
        let shortcut = SummonShortcut(event: event)
        if let error = shortcut.validationError {
            if !shortcut.modifiers.isEmpty || event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty {
                toast(icon: .warn, "Hotkey unavailable", "快捷键不可用", detail: error)
            }
            return true
        }
        if let error = controller.setShortcut(shortcut) { toast(icon: .warn, "Hotkey unavailable", "快捷键不可用", detail: error); return true }
        cancelCapture()
        toast(icon: .check, "Hotkey saved", "快捷键已保存", detail: Self.keycaps(shortcut).joined(separator: " + "))
        return true
    }

    func cancelCapture() {
        guard isCapturingShortcut else { return }
        controller.endShortcutCapture(); onCaptureChanged?(false)
    }

    private func buildAbout(_ page: HUDDesignPage) {
        let about = controller.about, update = controller.updateState
        let hero = aboutHero(about)
        HUDMotion.perform { hero.frame.origin = CGPoint(x: 0, y: HUDMetric.headerHeight); page.layer.addSublayer(hero) }
        page.stagger(hero)
        let spokenHero = HUDReadout(id: "value:hero", label: about.name, value: about.version, frame: hero.bounds, env: env)
        HUDMotion.perform { hero.addSublayer(spokenHero.layer) }
        page.add(spokenHero)
        let strip = HUDLayer.plain("about.updates")
        let cells = HUDCells(count: 4, width: HUDMetric.box.width - 14 - 130, capacity: false, height: 10, env: env)
        let status = HUDTextLayer(style: HUDTextStyle(size: 10.5, weight: .bold, color: HUDColor.rgb(0xD0D0D0), tracking: 0.1, uppercase: true),
                                  alignment: .right)
        HUDMotion.perform {
            strip.frame = CGRect(x: 0, y: hero.frame.maxY + 12, width: HUDMetric.box.width, height: 20)
            cells.layer.frame.origin = CGPoint(x: 0, y: 5)
            status.lineFrame = CGRect(x: HUDMetric.box.width - 130, y: 4.75, width: 130, height: 10.5)
            strip.addSublayer(cells.layer); strip.addSublayer(status)
            page.layer.addSublayer(strip)
        }
        page.stagger(strip)
        let spoken = HUDReadout(id: "value:updates", label: L10n.text("Updates", "更新"), frame: strip.bounds, env: env)
        HUDMotion.perform { strip.addSublayer(spoken.layer) }
        page.add(spoken)
        updateCells = cells; updateText = status; updateSpoken = spoken
        renderUpdateStrip(animated: false)
        let scroll = page.scrollArea(id: "about", frame: CGRect(x: 0, y: strip.frame.maxY + 10, width: HUDMetric.box.width,
                                                                height: HUDMetric.box.height - strip.frame.maxY - 10))
        var rows: [HUDRowPlate] = []
        let check = HUDButton(id: "updates", title: L10n.text("Check", "检查"), icon: .refresh, kind: .primary, size: .small, env: env)
        check.place(at: .zero)
        check.isEnabled = update.phase != .checking && checkProgress == nil
        check.onActivate = { [weak self] in self?.checkForUpdates() }
        binders.append { [weak self, weak check] in
            guard let self else { return }
            check?.isEnabled = self.controller.updateState.phase != .checking && self.checkProgress == nil
            self.renderUpdateStrip(animated: true)
        }
        rows.append(row("updates", L10n.text("Check for updates", "检查更新"), [check.layer], page, controls: [check]))
        let latest = readout(update.latestVersion.map { $0.hasPrefix("v") ? $0 : "v" + $0 } ?? "—")
        var latestCluster: [CALayer] = [latest], latestControls: [HUDControl] = []
        if let url = update.releaseURL {
            let open = linkButton("latestRelease", L10n.text("Latest GitHub release", "GitHub 最新版本"), url)
            latestCluster.append(open.layer); latestControls.append(open)
        }
        let latestRow = row("latestRelease", L10n.text("Latest GitHub release", "GitHub 最新版本"), latestCluster, page, controls: latestControls)
        spokenValue("latestRelease", on: latestRow, update.latestVersion ?? "—", page)
        rows.append(latestRow)
        let automatic = HUDToggle(id: "automaticUpdates", label: L10n.text("Install updates automatically", "自动安装更新"),
                                  isOn: update.automaticallyInstalls, env: env)
        automatic.onChange = { [weak self] _ in self?.controller.toggleAutomaticUpdates() }
        binders.append { [weak self, weak automatic] in guard let self else { return }; automatic?.setOn(self.controller.updateState.automaticallyInstalls, animated: true) }
        rows.append(row("automaticUpdates", L10n.text("Install updates automatically", "自动安装更新"), [automatic.layer], page, controls: [automatic]))
        rows.append(valueRow("author", L10n.text("Author", "作者"), about.author, page))
        if let repository = about.repositoryURL {
            let open = linkButton("github", "GitHub", repository)
            rows.append(row("github", "GitHub", [open.layer], page, controls: [open]))
        }
        rows.append(valueRow("license", L10n.text("License", "许可证"), about.licenseName, page))
        rows.append(navRow("credits", L10n.text("Credits", "致谢"), page) { [weak self] in self?.go(.credits, forward: true) })
        scroll.setContentHeight(stack(rows, in: scroll.content, page: page, top: 0, width: HUDMetric.box.width - 10))
        page.restoreOffset(of: scroll)
        header.configure(crumbs: ["ABOUT"], code: "SYS-04", title: HUDModuleTitle("About", "关于"),
                         counter: "v" + (about.version.components(separatedBy: " ").first ?? about.version), showsBack: false)
    }

    private func buildCredits(_ page: HUDDesignPage) {
        var rows: [HUDRowPlate] = []
        for (index, credit) in controller.about.credits.enumerated() {
            var cluster: [CALayer] = [], controls: [HUDControl] = []
            if let url = credit.url {
                let open = linkButton("credit:\(index)", credit.name, url)
                cluster.append(open.layer); controls.append(open)
            }
            rows.append(row("credit:\(index)", credit.name, cluster, page, sublabel: credit.role, controls: controls))
        }
        stack(rows, in: page.layer, page: page, top: HUDMetric.headerHeight)
        header.configure(crumbs: ["ABOUT", "CREDITS"], code: "SYS-04", title: HUDModuleTitle("Credits", "致谢"),
                         counter: HUDFormat.pad2(rows.count), showsBack: true)
        wireBack(page) { [weak self] in self?.go(.main, forward: false) }
    }

    /// The hero plate (spec §3.5): tri-strip, ENDFIELD. INDUSTRIES bar, name, version.
    private func aboutHero(_ about: HUDAboutInformation) -> CALayer {
        let size = CGSize(width: HUDMetric.box.width, height: 84)
        let hero = HUDLayer.plain("about.hero")
        let parts = about.version.components(separatedBy: " ")
        let version = parts.first ?? about.version
        let build = parts.count > 1 ? parts[1].trimmingCharacters(in: CharacterSet(charactersIn: "()")) : nil
        HUDMotion.perform {
            hero.bounds = CGRect(origin: .zero, size: size)
            let plate = HUDLayer.plain()
            plate.frame = hero.bounds
            plate.showArtwork(HUDArtwork.plate(HUDColor.heroPlate, size: size, cut: 14, scale: env.renderScale), scale: env.renderScale)
            let strip = HUDLayer.plain(); strip.frame = CGRect(x: 0, y: 14, width: 4, height: 38)
            strip.showArtwork(HUDArtwork.triStrip(size: strip.bounds.size, scale: env.renderScale), scale: env.renderScale)
            let bar = HUDLayer.plain(); bar.frame = CGRect(x: 20, y: 14, width: 118, height: 9); bar.backgroundColor = HUDColor.rgb(0x141414)
            let barText = HUDTextLayer("ENDFIELD. INDUSTRIES", style: HUDTextStyle(size: 6, weight: .black, color: HUDColor.white(1), lineHeight: 1.5, tracking: 0.06))
            barText.lineFrame = CGRect(x: 4, y: 0, width: 114, height: 9); bar.addSublayer(barText)
            let name = HUDTextLayer(about.name, style: HUDTextStyle(size: 30, weight: .black, color: HUDColor.nearBlack, tracking: 0.02))
            name.lineFrame = CGRect(x: 20, y: 33, width: 320, height: 30)
            let number = HUDTextLayer(version, style: HUDTextStyle(size: 38, weight: .light, color: HUDColor.nearBlack, tabular: true), alignment: .right)
            number.lineFrame = CGRect(x: size.width - 20 - 220, y: 16, width: 220, height: 38)
            [plate, strip, bar, name, number].forEach(hero.addSublayer)
            if let build {
                let label = HUDTextLayer("BUILD " + build, style: HUDTextStyle(size: 9.5, weight: .bold, color: HUDColor.rgb(0x555555), tracking: 0.12), alignment: .right)
                label.lineFrame = CGRect(x: size.width - 20 - 220, y: 58, width: 220, height: 9.5)
                hero.addSublayer(label)
            }
        }
        return hero
    }

    /// The 4-cell strip advances every 650 ms while the real check runs, then
    /// shows the outcome; `UP TO DATE` pops in as a stamp.
    private func checkForUpdates() {
        guard checkProgress == nil else { return }
        controller.checkForUpdates()
        checkProgress = 1
        renderUpdateStrip(animated: true)
        scheduleCheckStep()
    }

    private func scheduleCheckStep() {
        let work = DispatchWorkItem { [weak self] in
            guard let self, let step = self.checkProgress else { return }
            if step < 3 { self.checkProgress = step + 1; self.renderUpdateStrip(animated: true); self.scheduleCheckStep(); return }
            // Hold at the last step until the real check reports.
            guard self.controller.updateState.phase != .checking else { self.scheduleCheckStep(); return }
            self.checkProgress = nil; self.checkTimer = nil
            self.binders.forEach { $0() }
        }
        checkTimer = work
        DispatchQueue.main.asyncAfter(deadline: .now() + (env.reducesMotion ? 0.05 : 0.65), execute: work)
    }

    private func renderUpdateStrip(animated: Bool) {
        guard let cells = updateCells, let text = updateText else { return }
        let state = controller.updateState
        let steps = [L10n.text("Connecting", "连接中"), L10n.text("Reading releases", "读取版本"), L10n.text("Verifying", "校验中")]
        let filled: Int, label: String
        if let step = checkProgress { filled = step; label = steps[step - 1] }
        else {
            switch state.phase {
            case .current: filled = 4; label = ""
            case .checking: filled = 3; label = steps[2]
            case .available, .downloading, .ready, .installing: filled = 4; label = state.title
            case .failed: filled = 0; label = state.title
            case .idle: filled = 0; label = L10n.text("Not checked yet", "尚未检查")
            }
        }
        cells.set(filled: filled, animated: animated)
        HUDMotion.perform { text.text = label; text.isHidden = label.isEmpty }
        let showStamp = checkProgress == nil && state.phase == .current
        updateSpoken?.value = showStamp ? L10n.text("Up to date", "已是最新") : label
        if showStamp, updateStamp == nil, let strip = cells.layer.superlayer {
            let stamp = HUDBadge.stamp(L10n.text("Up to date", "已是最新"), env: env)
            HUDMotion.perform {
                stamp.position = CGPoint(x: HUDMetric.box.width - stamp.bounds.width / 2 - 6, y: 10)
                strip.addSublayer(stamp)
            }
            updateStamp = stamp
            if animated { HUDMotion.pop(stamp, env: env) }
        } else if !showStamp, let stamp = updateStamp {
            HUDMotion.perform { stamp.removeFromSuperlayer() }; updateStamp = nil
        }
    }

    // MARK: Row building

    /// Stacks rows from `top` with the 6 px gap; returns the stack's height.
    @discardableResult
    private func stack(_ rows: [HUDRowPlate], in parent: CALayer, page: HUDDesignPage, top: CGFloat,
                       width: CGFloat = HUDMetric.box.width) -> CGFloat {
        var y = top
        HUDMotion.perform {
            for row in rows {
                row.layer.frame.origin = CGPoint(x: 0, y: y)
                parent.addSublayer(row.layer)
                y += row.layer.bounds.height + HUDMetric.rowGap
            }
        }
        rows.forEach { page.stagger($0.layer) }
        return max(0, y - top - HUDMetric.rowGap)
    }

    private func row(_ id: String, _ title: String, _ cluster: [CALayer], _ page: HUDDesignPage, sublabel: String? = nil,
                     controls: [HUDControl] = [], variant: HUDRowPlate.Variant = .standard) -> HUDRowPlate {
        let plate = HUDRowPlate(id: "row:" + id, title: title, sublabel: sublabel, variant: variant,
                                width: rowWidth(page), env: env)
        plate.setCluster(cluster)
        page.add(plate)
        page.add(controls)
        return plate
    }

    private func row(_ id: String, _ title: String, _ dropdowns: [HUDDropdown], _ page: HUDDesignPage) -> HUDRowPlate {
        row(id, title, dropdowns.map(\.layer), page, controls: dropdowns)
    }

    private func rowWidth(_ page: HUDDesignPage) -> CGFloat {
        (module == .display && section == .main) || section == .modules ? HUDMetric.box.width - 10 : HUDMetric.box.width
    }

    private func navRow(_ id: String, _ title: String, _ page: HUDDesignPage, chevron: String = "›", cluster: [CALayer] = [],
                        action: @escaping () -> Void) -> HUDRowPlate {
        let plate = HUDRowPlate(id: id, title: title, variant: .nav(chevron: chevron), width: rowWidth(page), env: env)
        plate.setCluster(cluster)
        plate.onActivate = action
        page.add(plate)
        return plate
    }

    /// `confirmOff` asks before switching off: the switch stays on until it
    /// calls its argument, which saves the change.
    private func toggleRow(_ id: String, _ title: String, _ key: WritableKeyPath<AppConfiguration, Bool>, _ page: HUDDesignPage,
                           confirmOff: ((@escaping () -> Void) -> Void)? = nil, changed: ((Bool) -> Void)? = nil) -> HUDRowPlate {
        let toggle = HUDToggle(id: id, label: title, isOn: config[keyPath: key], env: env)
        toggle.onChange = { [weak self, weak toggle] on in
            guard let self else { return }
            if !on, let confirmOff {
                toggle?.setOn(true, animated: true)
                confirmOff { [weak self] in self?.controller.update { $0[keyPath: key] = false } }
                return
            }
            self.controller.update { $0[keyPath: key] = on }
            changed?(self.config[keyPath: key])
        }
        binders.append { [weak self, weak toggle] in guard let self else { return }; toggle?.setOn(self.config[keyPath: key], animated: true) }
        return row(id, title, [toggle.layer], page, controls: [toggle])
    }

    private func sliderRow(_ id: String, _ title: String, value: Double, range: HUDSlider.Range, format: HUDValueFormat,
                           page: HUDDesignPage, read: @escaping (AppConfiguration) -> Double,
                           change: @escaping (Double, Bool) -> Void) -> HUDRowPlate {
        let plate = HUDRowPlate(id: "row:" + id, title: title, variant: .slider, width: rowWidth(page), env: env)
        let area = plate.controlArea
        let slider = HUDSlider(id: id, label: title, value: value, range: range, format: format, width: area.width, env: env)
        HUDMotion.perform { slider.layer.frame.origin = area.origin; plate.layer.addSublayer(slider.layer) }
        hostBubble(slider)
        slider.onChange = { [weak self] value, final in
            change(value, final)
            if final { self?.context.onInputStateChange?() }
        }
        binders.append { [weak self, weak slider] in
            guard let self, let slider, !slider.isDragging else { return }
            slider.setValue(read(self.config))
        }
        page.add(plate); page.add(slider)
        return plate
    }

    private func percentRow(_ id: String, _ title: String, maximum: Double, _ key: WritableKeyPath<AppConfiguration, Double>,
                            _ page: HUDDesignPage) -> HUDRowPlate {
        sliderRow(id, title, value: config[keyPath: key] * 100, range: HUDSlider.Range(minimum: 0, maximum: maximum, step: 1),
                  format: .percent, page: page, read: { $0[keyPath: key] * 100 }) { [weak self] value, _ in
            self?.controller.update { $0[keyPath: key] = value / 100 }
        }
    }

    private func dropdown(_ id: String, _ label: String, _ value: String, language: String? = nil,
                          open: @escaping (HUDDropdown) -> Void) -> HUDDropdown {
        let field = HUDDropdown(id: id, label: label, value: value, valueLanguage: language, env: env)
        field.onActivate = { [weak self, weak field] in
            guard let self, let field else { return }
            if self.isMenuOpen { self.closeMenu() } else { open(field) }
        }
        return field
    }

    /// `[ value ]` readout with grey brackets (spec §2.7), min-width 0.
    /// A row with a bracketed value, which VoiceOver reads as the row's text.
    private func valueRow(_ id: String, _ title: String, _ value: String, _ page: HUDDesignPage) -> HUDRowPlate {
        let plate = row(id, title, [readout(value)], page)
        spokenValue(id, on: plate, value, page)
        return plate
    }

    /// Exposes a row's drawn value to VoiceOver over the whole plate.
    @discardableResult
    private func spokenValue(_ id: String, on plate: HUDRowPlate, _ value: String, _ page: HUDDesignPage) -> HUDReadout {
        let spoken = HUDReadout(id: "value:" + id, label: plate.label, value: value, frame: plate.layer.bounds, env: env)
        HUDMotion.perform { plate.layer.addSublayer(spoken.layer) }
        page.add(spoken)
        return spoken
    }

    private func readout(_ value: String) -> HUDTextLayer {
        let text = HUDTextLayer(style: HUDType.readout, alignment: .right)
        setReadout(text, value)
        return text
    }

    private func setReadout(_ layer: HUDTextLayer?, _ value: String) {
        guard let layer else { return }
        HUDMotion.perform {
            layer.runs = [HUDText.Run("[ ", HUDType.readoutBracket), HUDText.Run(value, HUDType.readout), HUDText.Run(" ]", HUDType.readoutBracket)]
            // Only the line frame: it pads the bounds for the glyphs. Setting the
            // bounds first shrank an unchanged value's box and truncated it.
            layer.lineFrame = CGRect(x: 0, y: 0, width: ceil(layer.preferredWidth) + 2, height: 12)
        }
        env.applyScale(to: layer)
    }

    private func linkButton(_ id: String, _ label: String, _ url: URL) -> HUDIconButton {
        let button = HUDIconButton(id: id + ":open", label: label, icon: .ext, style: .light, env: env)
        button.onActivate = { [weak self] in
            self?.controller.openLink(url)
            if url.host == "github.com" { self?.toast(icon: .ext, "Opening GitHub", "正在打开 GitHub", detail: String(url.path.dropFirst())) }
            else { self?.toast(icon: .ext, "Opening link", "正在打开链接", detail: url.host ?? url.absoluteString) }
        }
        return button
    }

    // MARK: Titles

    private func languageTitle(_ value: AppLanguage) -> String {
        switch value {
        case .system: return L10n.text("System", "跟随系统")
        case .english: return "English"
        case .simplifiedChinese: return "简体中文"
        case .traditionalChinese: return "繁體中文"
        case .japanese: return "日本語"
        case .korean: return "한국어"
        }
    }

    private func themeTitle(_ value: OverlayTheme) -> String {
        switch value { case .dark: return L10n.text("Dark", "深色"); case .light: return L10n.text("Light", "浅色"); case .system: return L10n.text("System", "跟随系统") }
    }

    private func methodTitle(_ value: DisplayMode) -> String {
        switch value {
        case .whenChargingStarts: return L10n.text("Power changes", "充电状态变化")
        case .always: return L10n.text("Always", "始终显示")
        case .never: return L10n.text("Don't display", "不显示")
        }
    }

    private func placementTitle(_ value: OverlayPlacement) -> String {
        value == .topCenter ? L10n.text("Top center", "顶部居中") : L10n.text("Custom", "自定")
    }
}

/// An image tile with the selected L-brackets (App / menu bar icon picker).
final class HUDIconTile: HUDControl {
    private let well = HUDLayer.plain("tile.well")
    private let glyph = HUDLayer.plain("tile.glyph")
    private let brackets = HUDLayer.shape("tile.brackets")
    var isSelected = false { didSet { if isSelected != oldValue { render(animated: true) } } }

    init(id: String, icon: HUDApplicationIcon, size: CGSize, env: HUDDesignEnvironment) {
        super.init(id: id, label: icon.title, env: env)
        let bounds = CGRect(origin: .zero, size: size)
        HUDMotion.perform {
            layer.bounds = bounds
            well.frame = bounds.insetBy(dx: 4, dy: 4)
            well.showArtwork(HUDArtwork.plate([HUDColor.rgb(0x2E2E2E), HUDColor.rgb(0x1C1C1C)], size: well.bounds.size, cut: 9, scale: env.renderScale),
                             scale: env.renderScale)
            glyph.frame = CGRect(x: bounds.midX - 30, y: bounds.midY - 30, width: 60, height: 60)
            glyph.contents = icon.image().cgImage(forProposedRect: nil, context: nil, hints: nil)
            glyph.contentsGravity = .resizeAspect
            brackets.frame = bounds; brackets.fillColor = HUDColor.yellow
            brackets.path = HUDArchiveTile.bracketPath(bounds, inset: 0)
            [well, glyph, brackets].forEach(layer.addSublayer)
        }
        setTarget(bounds)
        render(animated: false)
    }

    private func render(animated: Bool) {
        HUDMotion.perform { brackets.isHidden = !isSelected }
        if isSelected, animated { HUDMotion.enter(brackets, from: .zero, duration: 0.2, env: env) }
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        HUDMotion.animate(well, "transform.translation.y", to: on ? -2 : 0, duration: HUDDuration.hover, env: env)
        HUDMotion.animate(glyph, "transform.translation.y", to: on ? -3 : 0, duration: 0.3, env: env)
    }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .tab(selected: isSelected) }
}

