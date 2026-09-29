import AppKit
import QuartzCore

struct HUDSettingsAction {
    let id: String
    let label: String
    let rect: CGRect
    var enabled = true
}

struct HUDSettingsSliderValue {
    let id: String
    let label: String
    let rect: CGRect
    let value: Double
    let minimum: Double
    let maximum: Double
    let valueDescription: String
}

/// A retained settings instrument for one of the four left navigation entries.
/// Preferences and platform side effects belong to HUDSettingsController.
final class HUDSettingsCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    let module: HUDModule
    var onChange: (() -> Void)?
    var onChooseColor: ((NSColor) -> Void)?
    var onCaptureChanged: ((Bool) -> Void)?
    private let controller: HUDSettingsController
    private let shouldReduceMotion: () -> Bool
    private var observer: UUID?
    private var screenObserver: NSObjectProtocol?
    private let displayProvider: () -> [HUDDisplayDescriptor]
    private var displays: [HUDDisplayDescriptor] = []
    private var pageContent = CALayer()
    private var rowsLayer = CALayer()
    private var chrome = CALayer()
    private var outgoingPage: CALayer?
    private var pageCompletion: DispatchWorkItem?
    private var pageGeneration = 0
    private var rowLayers: [String: CALayer] = [:]
    private var pageMask = CAShapeLayer()
    private static let pageMovementKey = "settings.page.depth"
    private static let pageRevealKey = "settings.page.reveal"
    private var active = false
    private var dark = true
    private var renderScale: CGFloat = 2
    private var page: Page = .main
    private var mainScrollOffset: CGFloat = 0
    private(set) var scrollOffset: CGFloat = 0
    private var draggedSlider: String?
    private var stagedScale: Double?
    private var stagedPosition: (x: Double, y: Double)?
    private var selectedSlider: String?
    private var restoreConfirmation = false
    private var localStatus: String?
    private enum Page { case main, battery, icons, screens, languages }
    private enum Kind {
        case toggle(Bool), choice(String), slider(Double, Double, Double), palette, icons([HUDApplicationIcon]), info(String), link(String, URL?), heading
        case selectionOption(selected: Bool, detail: String, available: Bool)
    }
    private struct Row {
        let id: String
        let title: String
        let kind: Kind
        var height: CGFloat = 40
    }
    static let viewport = CGRect(x: 12, y: 40, width: 376, height: 250)
    var isDragging: Bool { draggedSlider != nil }
    var isTransitioning: Bool { outgoingPage != nil }
    var isCapturingShortcut: Bool { module == .hotkeys && controller.isCapturingShortcut }
    var accessibilityStatus: String {
        if module == .about { return localStatus ?? controller.updateState.detail ?? "" }
        return localStatus ?? controller.shortcutStatus ?? controller.status ?? ""
    }
    private var config: AppConfiguration { controller.configuration }
    private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.12, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.64 : 0.40, alpha: 1) }
    private var accent: NSColor { config.accentColor }
    private var maximumScroll: CGFloat { max(0, rows.reduce(0) { $0 + $1.height } - Self.viewport.height) }

    init(module: HUDModule, controller: HUDSettingsController,
         reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion },
         displayProvider: @escaping () -> [HUDDisplayDescriptor] = { HUDDisplayPolicy.connectedDisplays() }) {
        precondition([HUDModule.system, .display, .hotkeys, .about].contains(module))
        self.module = module; self.controller = controller; shouldReduceMotion = reduceMotion
        self.displayProvider = displayProvider
        displays = module == .system ? displayProvider() : []
        super.init()
        withoutActions {
            layer.name = "module.settings.\(module.rawValue)"
            layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
            layer.allowsGroupOpacity = false
            installPageLayers()
        }
        if module == .system {
            screenObserver = NotificationCenter.default.addObserver(forName: NSApplication.didChangeScreenParametersNotification,
                object: nil, queue: .main) { [weak self] _ in
                guard let self else { return }
                self.displays = self.displayProvider()
                if self.active { self.refresh() }
            }
        }
        observer = controller.addObserver { [weak self] in
            guard let self else { return }
            if self.active { self.refresh() }
        }
    }
    deinit {
        pageCompletion?.cancel()
        if let screenObserver { NotificationCenter.default.removeObserver(screenObserver) }
        if let observer { controller.removeObserver(observer) }
        if isCapturingShortcut { controller.endShortcutCapture(); onCaptureChanged?(false) }
    }

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; renderScale = style.contentsScale; refresh()
        return layer
    }
    func activate() {
        active = true; if module == .system { displays = displayProvider() }; controller.refreshExternalStatus(); refresh()
    }
    func deactivate() {
        // An interrupted gesture must not create a fresh preview after the
        // overlay coordinator has already rolled back its safety transaction.
        active = false; draggedSlider = nil; stagedScale = nil; stagedPosition = nil; cancelCapture(); settleTransition()
        restoreConfirmation = false; localStatus = nil
    }
    func updateRenderScale(_ value: CGFloat) {
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard next != renderScale else { return }
        renderScale = next; if active { refresh() }
    }
    func refresh() {
        if shouldReduceMotion(), isTransitioning { settleTransition() }
        scrollOffset = min(scrollOffset, maximumScroll)
        withoutActions { repaint() }; onChange?()
    }

    private var rows: [Row] {
        let c = config
        if module == .system {
            if page == .languages {
                return AppLanguage.allCases.map { language in
                    Row(id: "language:" + language.rawValue, title: languageTitle(language),
                        kind: .selectionOption(selected: c.language == language, detail: "", available: true))
                }
            }
            if page == .screens {
                var result = [
                    Row(id: "screen:pointer", title: L10n.text("Pointer display", "鼠标所在显示器"), kind: .selectionOption(selected: c.hudDisplayUUID == nil && c.openOnActiveDisplay, detail: "", available: true)),
                    Row(id: "screen:main", title: L10n.text("Main display", "主显示器"), kind: .selectionOption(selected: c.hudDisplayUUID == nil && !c.openOnActiveDisplay, detail: "", available: true))
                ]
                for (index, display) in displays.enumerated() {
                    let title = displays.filter { $0.name == display.name }.count > 1 ? "\(display.name) · \(index + 1)" : display.name
                    result.append(Row(id: "screen:" + display.uuid, title: title,
                        kind: .selectionOption(selected: c.hudDisplayUUID == display.uuid, detail: display.dimensions, available: true), height: 48))
                }
                if let uuid = c.hudDisplayUUID, !displays.contains(where: { $0.uuid == uuid }) {
                    result.append(Row(id: "screen:disconnected", title: c.hudDisplayName ?? L10n.text("Saved display", "已选显示器"),
                        kind: .selectionOption(selected: true, detail: L10n.text("Disconnected · using pointer display", "未连接 · 暂用鼠标所在显示器"), available: false), height: 48))
                }
                return result
            }
            return [
                Row(id: "language", title: L10n.text("Language", "语言"), kind: .choice(languageTitle(c.language))),
                Row(id: "login", title: L10n.text("Launch at login", "登录时启动"), kind: .toggle(c.launchAtLogin)),
                Row(id: "focus", title: L10n.text("Close when focus lost", "失去焦点时关闭"), kind: .toggle(c.closeOnFocusLost)),
                Row(id: "screen", title: L10n.text("Display", "显示器"), kind: .choice(HUDDisplayPolicy.selectionTitle(configuration: c, displays: displays))),
                Row(id: "ambient", title: L10n.text("Ambient animation", "持续环境动画"), kind: .toggle(c.ambientAnimation)),
                Row(id: "batteryEnabled", title: L10n.text("Battery alerts", "电池提醒"), kind: .toggle(c.batteryAlertsEnabled)),
                Row(id: "restore", title: L10n.text("Restore default settings", "恢复默认设置"), kind: .choice("↺"))
            ]
        }
        if module == .display {
            if page == .icons {
                let presets = HUDApplicationIcon.pickerCases
                return stride(from: 0, to: presets.count, by: 4).map { index in
                    Row(id: "icons:\(index)", title: "", kind: .icons(Array(presets[index..<min(index + 4, presets.count)])), height: 78)
                }
            }
            if page == .battery {
                return [
                    Row(id: "method", title: L10n.text("Display method", "显示方式"), kind: .choice(c.displayMode == .always ? L10n.text("Always", "始终显示") : L10n.text("Power changes", "充电状态变化"))),
                    Row(id: "duration", title: L10n.text("Display duration", "显示时长"), kind: .slider(c.displayDuration, 1, 60)),
                    Row(id: "placement", title: L10n.text("Display position", "显示位置"), kind: .choice(c.placement == .topCenter ? L10n.text("Top center", "顶部居中") : L10n.text("Custom", "自定"))),
                    Row(id: "editPosition", title: L10n.text("Edit position", "编辑位置"), kind: .choice("↗")),
                    Row(id: "batterySize", title: L10n.text("Size", "大小"), kind: .slider(c.scale, 0.65, 1.6))
                ]
            }
            return [
                Row(id: "uiScale", title: L10n.text("UI scale", "界面缩放"), kind: .slider(stagedScale ?? c.hudScale, 0.2, 2)),
                Row(id: "positionX", title: L10n.text("HUD position X", "界面位置 X"), kind: .slider(stagedPosition?.x ?? c.hudOffsetX, -0.5, 0.5)),
                Row(id: "positionY", title: L10n.text("HUD position Y", "界面位置 Y"), kind: .slider(stagedPosition?.y ?? c.hudOffsetY, -0.5, 0.5)),
                Row(id: "parallax", title: L10n.text("Parallax intensity", "视差强度"), kind: .slider(c.parallaxIntensity, 0, 2)),
                Row(id: "perspective", title: L10n.text("Perspective intensity", "透视强度"), kind: .slider(c.perspectiveIntensity, 0, 2)),
                Row(id: "darkness", title: L10n.text("Background brightness", "背景亮度"), kind: .slider(c.backgroundBrightness, 0, 1)),
                Row(id: "blur", title: L10n.text("Blur amount", "模糊程度"), kind: .slider(c.blurAmount, 0, 1)),
                Row(id: "motion", title: L10n.text("Reduce Motion", "减少动态效果"), kind: .toggle(c.reduceMotion)),
                Row(id: "theme", title: L10n.text("Theme", "主题"), kind: .choice(themeTitle(c.theme))),
                Row(id: "clockFormat", title: L10n.text("Time format", "时间格式"), kind: .choice(c.clockFormat == .twentyFourHour ? L10n.text("24-hour", "24 小时制") : L10n.text("12-hour AM/PM", "12 小时制 AM/PM"))),
                Row(id: "appIcon", title: L10n.text("App / menu bar icon", "应用 / 菜单栏图标"), kind: .choice("›")),
                Row(id: "palette", title: L10n.text("Theme color", "主题颜色"), kind: .palette, height: 54),
                Row(id: "battery", title: L10n.text("Battery alert", "电池提醒"), kind: .choice("›")),
                Row(id: "lowPower", title: L10n.text("Low Power visual mode", "低功耗视觉模式"), kind: .toggle(c.lowPowerVisualMode))
            ]
        }
        if module == .hotkeys {
            return [
                Row(id: "capture", title: L10n.text("Summon HUD", "呼出界面"), kind: .choice(isCapturingShortcut ? L10n.text("Press shortcut…", "请按快捷键…") : c.summonShortcut.displayName), height: 54),
                Row(id: "keyReset", title: L10n.text("Restore default hotkey", "恢复默认快捷键"), kind: .choice(SummonShortcut.default.displayName))
            ]
        }
        let about = controller.about
        var result = [
            Row(id: "app", title: about.name, kind: .info(about.version), height: 46),
            Row(id: "updates", title: L10n.text("Check for updates", "检查更新"), kind: .choice(controller.updateState.title)),
            Row(id: "latestRelease", title: L10n.text("Latest GitHub release", "GitHub 最新版本"), kind: .link(controller.updateState.latestVersion ?? "—", controller.updateState.releaseURL)),
            Row(id: "automaticUpdates", title: L10n.text("Install updates automatically", "自动安装更新"), kind: .toggle(controller.updateState.automaticallyInstalls)),
            Row(id: "author", title: L10n.text("Author", "作者"), kind: .info(about.author)),
            Row(id: "github", title: "GitHub", kind: .link(about.repositoryURL == nil ? L10n.text("Not configured", "尚未设置") : "↗", about.repositoryURL)),
            Row(id: "license", title: L10n.text("License", "许可证"), kind: .info(about.licenseName)),
            Row(id: "credits", title: "Credits:", kind: .heading, height: 30)
        ]
        for (index, credit) in about.credits.enumerated() {
            result.append(Row(id: "credit:\(index)", title: credit.name, kind: .link(credit.role, credit.url), height: 48))
        }
        return result
    }

    private var locatedRows: [(Row, CGRect)] {
        var y = Self.viewport.minY - scrollOffset
        return rows.map { row in
            defer { y += row.height }
            return (row, CGRect(x: 16, y: y, width: 366, height: row.height - 2))
        }
    }
    var accessibleActions: [HUDSettingsAction] {
        if restoreConfirmation {
            return [HUDSettingsAction(id: "restore:cancel", label: L10n.text("Cancel restore", "取消恢复"), rect: CGRect(x: 46, y: 197, width: 144, height: 32)),
                    HUDSettingsAction(id: "restore:confirm", label: L10n.text("Restore defaults", "恢复默认设置"), rect: CGRect(x: 206, y: 197, width: 144, height: 32))]
        }
        var result: [HUDSettingsAction] = []
        if page != .main { result.append(HUDSettingsAction(id: "back", label: L10n.text("Back", "返回"), rect: CGRect(x: 323, y: 2, width: 65, height: 25))) }
        for (row, rect) in locatedRows {
            let visible = rect.intersection(Self.viewport)
            guard !visible.isNull, visible.height >= min(25, rect.height) else { continue }
            switch row.kind {
            case .toggle(let value): result.append(HUDSettingsAction(id: row.id, label: row.title + ", " + (value ? L10n.text("On", "开") : L10n.text("Off", "关")), rect: visible))
            case .choice(let value): result.append(HUDSettingsAction(id: row.id, label: row.title + ", " + value, rect: visible))
            case .selectionOption(let selected, let detail, let available):
                result.append(HUDSettingsAction(id: row.id, label: [row.title, detail, selected ? L10n.text("Selected", "已选择") : ""].filter { !$0.isEmpty }.joined(separator: ", "), rect: visible, enabled: available))
            case .link(let value, let url):
                if url != nil { result.append(HUDSettingsAction(id: row.id, label: row.title + ", " + value, rect: visible)) }
            case .icons(let presets):
                for (index, icon) in presets.enumerated() {
                    let iconRect = iconRect(index: index, row: rect)
                    if Self.viewport.contains(iconRect) { result.append(HUDSettingsAction(id: "appIcon:\(icon.rawValue)", label: icon.title, rect: iconRect)) }
                }
            case .palette:
                for (index, hex) in HUDSettingsController.presetAccentHexes.enumerated() {
                    let buttonRect = paletteRect(index: index, row: rect)
                    if Self.viewport.contains(buttonRect) { result.append(HUDSettingsAction(id: "accent:\(hex)", label: L10n.text("Theme color", "主题颜色") + " #" + hex, rect: buttonRect)) }
                }
                let custom = paletteRect(index: 5, row: rect)
                if Self.viewport.contains(custom) { result.append(HUDSettingsAction(id: "customColor", label: L10n.text("Custom color wheel", "自定义色轮"), rect: custom)) }
            default: break
            }
        }
        if isCapturingShortcut { result.append(HUDSettingsAction(id: "capture:cancel", label: L10n.text("Cancel shortcut recording", "取消快捷键录制"), rect: CGRect(x: 297, y: 296, width: 90, height: 27))) }
        return result
    }
    var accessibleSliders: [HUDSettingsSliderValue] {
        guard !restoreConfirmation else { return [] }
        return locatedRows.compactMap { row, rect in
            guard case .slider(let value, let lower, let upper) = row.kind else { return nil }
            let sliderRect = CGRect(x: 192, y: rect.minY + 8, width: 184, height: 25)
            guard Self.viewport.contains(sliderRect) else { return nil }
            return HUDSettingsSliderValue(id: row.id, label: row.title, rect: sliderRect, value: value,
                minimum: lower, maximum: upper, valueDescription: formatted(value, id: row.id))
        }
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard layer.bounds.contains(point) else { return false }
        if let slider = accessibleSliders.first(where: { $0.rect.contains(point) }) {
            draggedSlider = slider.id; selectedSlider = slider.id; moveSlider(to: point); return true
        }
        if let action = accessibleActions.first(where: { $0.rect.contains(point) && $0.enabled }) { perform(actionID: action.id) }
        return true
    }
    func mouseDragged(to point: CGPoint) { if draggedSlider != nil { moveSlider(to: point) } }
    func mouseUp() {
        let staged = stagedScale, position = stagedPosition, id = draggedSlider
        draggedSlider = nil; stagedScale = nil; stagedPosition = nil
        if let staged { controller.previewScale(staged); animateRow("uiScale") }
        if let position { controller.previewPosition(x: position.x, y: position.y); animateRow(id ?? "positionX") }
    }
    private func moveSlider(to point: CGPoint) {
        guard point.x.isFinite, let id = draggedSlider, let item = accessibleSliders.first(where: { $0.id == id }) else { return }
        let fraction = Double(min(1, max(0, (point.x - item.rect.minX - 6) / (item.rect.width - 12))))
        _ = setSlider(id: id, value: item.minimum + fraction * (item.maximum - item.minimum))
    }
    @discardableResult func setSlider(id: String, value: Double) -> Bool {
        guard value.isFinite, let item = accessibleSliders.first(where: { $0.id == id }) else { return false }
        let value = min(item.maximum, max(item.minimum, value))
        selectedSlider = id
        if id == "uiScale" {
            let rounded = (value * 20).rounded() / 20
            if draggedSlider == id { stagedScale = rounded; refresh() }
            else { controller.previewScale(rounded); animateRow(id) }
            return true
        }
        if id == "positionX" || id == "positionY" {
            let rounded = (value * 100).rounded() / 100
            var position = stagedPosition ?? (x: config.hudOffsetX, y: config.hudOffsetY)
            if id == "positionX" { position.x = rounded } else { position.y = rounded }
            if draggedSlider == id { stagedPosition = position; refresh() }
            else { controller.previewPosition(x: position.x, y: position.y); animateRow(id) }
            return true
        }
        controller.update { c in
            switch id {
            case "parallax": c.parallaxIntensity = value
            case "perspective": c.perspectiveIntensity = value
            case "darkness": c.backgroundBrightness = value
            case "blur": c.blurAmount = value
            case "duration": c.displayDuration = value.rounded()
            case "batterySize": c.scale = value
            default: break
            }
        }
        if draggedSlider == nil { animateRow(id) }
        return true
    }
    func nudgeSlider(_ direction: Double) {
        guard let selectedSlider, let item = accessibleSliders.first(where: { $0.id == selectedSlider }) else { return }
        let step = item.id == "duration" ? 1 : item.id == "uiScale" ? 0.05 : item.id.hasPrefix("position") ? 0.01 : (item.maximum - item.minimum) * 0.02
        _ = setSlider(id: item.id, value: item.value + step * direction)
    }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard Self.viewport.contains(point), delta.isFinite, !restoreConfirmation, !isCapturingShortcut else { return false }
        let next = min(maximumScroll, max(0, scrollOffset + delta))
        if next != scrollOffset { mouseUp(); scrollOffset = next; refresh() }
        return true
    }

    func perform(actionID id: String) {
        guard accessibleActions.contains(where: { $0.id == id && $0.enabled }) else { return }
        localStatus = nil
        switch id {
        case "back": reveal(direction: -1) { page = .main; scrollOffset = mainScrollOffset }; return
        case "language": reveal(direction: 1) { mainScrollOffset = scrollOffset; page = .languages; scrollOffset = 0 }; return
        case "screen": displays = displayProvider(); reveal(direction: 1) { mainScrollOffset = scrollOffset; page = .screens; scrollOffset = 0 }; return
        case "appIcon": reveal(direction: 1) { mainScrollOffset = scrollOffset; page = .icons; scrollOffset = 0 }; return
        case "battery": reveal(direction: 1) { mainScrollOffset = scrollOffset; page = .battery; scrollOffset = 0 }; return
        case "restore": reveal(direction: 1) { restoreConfirmation = true }; return
        case "restore:cancel": reveal(direction: -1) { restoreConfirmation = false }; return
        case "restore:confirm": reveal(direction: -1) { restoreConfirmation = false; controller.restoreDefaults() }; return
        case "customColor": onChooseColor?(config.accentColor)
        case "capture":
            if isCapturingShortcut { cancelCapture() }
            else { controller.beginShortcutCapture(); onCaptureChanged?(true) }
        case "capture:cancel": cancelCapture()
        case "keyReset": _ = controller.setShortcut(.default)
        case "editPosition": controller.editBatteryPosition()
        case "github": if let url = controller.about.repositoryURL { controller.openLink(url) }
        case "updates": controller.checkForUpdates()
        case "automaticUpdates": controller.toggleAutomaticUpdates()
        case "latestRelease": if let url = controller.updateState.releaseURL { controller.openLink(url) }
        default:
            if id.hasPrefix("language:"), let language = AppLanguage(rawValue: String(id.dropFirst(9))) {
                reveal(direction: -1) {
                    page = .main; scrollOffset = mainScrollOffset
                    controller.update { $0.language = language }
                }
                return
            }
            else if id.hasPrefix("screen:") {
                let value = String(id.dropFirst(7))
                guard value == "pointer" || value == "main" || displays.contains(where: { $0.uuid == value }) else { return }
                reveal(direction: -1) {
                    page = .main; scrollOffset = mainScrollOffset
                    controller.update { c in
                        c.hudDisplayUUID = value == "pointer" || value == "main" ? nil : value
                        c.hudDisplayName = displays.first(where: { $0.uuid == value })?.name
                        c.openOnActiveDisplay = value != "main"
                    }
                }
                return
            }
            else if id.hasPrefix("credit:"), let index = Int(id.dropFirst(7)), controller.about.credits.indices.contains(index), let url = controller.about.credits[index].url { controller.openLink(url) }
            else if id.hasPrefix("appIcon:"), let icon = HUDApplicationIcon(rawValue: String(id.dropFirst(8))) { controller.update { $0.applicationIcon = icon } }
            else if id.hasPrefix("accent:") { let hex = String(id.dropFirst(7)); controller.update { $0.accentHex = hex } }
            else {
                controller.update { c in
                    switch id {
                    case "login": c.launchAtLogin.toggle()
                    case "focus": c.closeOnFocusLost.toggle()
                    case "ambient": c.ambientAnimation.toggle()
                    case "batteryEnabled": c.batteryAlertsEnabled.toggle()
                    case "motion": c.reduceMotion.toggle()
                    case "lowPower": c.lowPowerVisualMode.toggle()
                    case "theme": c.theme = next(c.theme, in: OverlayTheme.allCases)
                    case "clockFormat": c.clockFormat = next(c.clockFormat, in: HUDClockFormat.allCases)
                    case "method": c.displayMode = next(c.displayMode, in: DisplayMode.allCases)
                    case "placement": c.placement = next(c.placement, in: OverlayPlacement.allCases)
                    default: break
                    }
                }
            }
        }
        refresh(); animateRow(id)
    }
    func setCustomColor(_ color: NSColor) {
        guard let rgb = color.usingColorSpace(.sRGB) else { return }
        let hex = String(format: "%02X%02X%02X", Int((rgb.redComponent * 255).rounded()), Int((rgb.greenComponent * 255).rounded()), Int((rgb.blueComponent * 255).rounded()))
        controller.update { $0.accentHex = hex }; animateRow("palette")
    }
    @discardableResult func capture(_ event: NSEvent) -> Bool {
        guard isCapturingShortcut else { return false }
        if event.keyCode == 53 { cancelCapture(); return true }
        guard !event.isARepeat else { return true }
        let shortcut = SummonShortcut(event: event)
        if let error = shortcut.validationError { localStatus = error; refresh(); animateRow("capture"); return true }
        if let error = controller.setShortcut(shortcut) { localStatus = error; refresh(); animateRow("capture"); return true }
        cancelCapture(); animateRow("capture"); return true
    }
    func cancelCapture() {
        guard isCapturingShortcut else { return }
        controller.endShortcutCapture(); onCaptureChanged?(false)
        if active { refresh(); animateRow("capture") }
    }
    @discardableResult func escape() -> Bool {
        if isCapturingShortcut { cancelCapture(); return true }
        if restoreConfirmation { perform(actionID: "restore:cancel"); return true }
        if page != .main { perform(actionID: "back"); return true }
        return false
    }

    private func repaint() {
        rowsLayer.sublayers?.forEach { $0.removeFromSuperlayer() }; chrome.sublayers?.forEach { $0.removeFromSuperlayer() }; rowLayers.removeAll()
        text(page == .battery ? L10n.text("Battery alert", "电池提醒") : page == .icons ? L10n.text("App / menu bar icon", "应用 / 菜单栏图标") : page == .screens ? L10n.text("Display", "显示器") : page == .languages ? L10n.text("Language", "语言") : module.title, in: chrome,
             rect: CGRect(x: 12, y: 1, width: 306, height: 23), size: 16, weight: .semibold, color: primary)
        let line = CALayer(); line.frame = CGRect(x: 12, y: 29, width: 376, height: 1); line.backgroundColor = accent.withAlphaComponent(0.5).cgColor; chrome.addSublayer(line)
        if page != .main { smallButton(L10n.text("‹ Back", "‹ 返回"), rect: CGRect(x: 323, y: 2, width: 65, height: 25), in: chrome) }
        if restoreConfirmation { renderRestore(); return }
        for (row, rect) in locatedRows where rect.intersects(Self.viewport) { render(row, in: rect) }
        if maximumScroll > 0 {
            let height = max(22, Self.viewport.height * Self.viewport.height / (maximumScroll + Self.viewport.height))
            let track = CALayer(); track.frame = CGRect(x: 387, y: Self.viewport.minY + (Self.viewport.height - height) * scrollOffset / maximumScroll, width: 1.5, height: height)
            track.backgroundColor = muted.withAlphaComponent(0.55).cgColor; chrome.addSublayer(track)
        }
        var status = accessibilityStatus
        if status.isEmpty, module == .system { status = controller.loginStatus ?? "" }
        if status.isEmpty, isCapturingShortcut { status = L10n.text("Press up to 3 keys · Esc to cancel", "最多同时按 3 个键 · Esc 取消") }
        text(status, in: chrome, rect: CGRect(x: 14, y: 299, width: isCapturingShortcut ? 276 : 372, height: 31), size: 9.5, color: muted, wrapped: true)
        if isCapturingShortcut { smallButton(L10n.text("Cancel", "取消"), rect: CGRect(x: 297, y: 296, width: 90, height: 27), in: chrome) }
    }
    private func render(_ row: Row, in rect: CGRect) {
        let host = CALayer(); host.frame = layer.bounds; host.name = "settings.row.\(row.id)"; rowsLayer.addSublayer(host); rowLayers[row.id] = host
        let divider = CALayer(); divider.frame = CGRect(x: rect.minX, y: rect.maxY - 1, width: rect.width, height: 0.5); divider.backgroundColor = muted.withAlphaComponent(0.19).cgColor; host.addSublayer(divider)
        let labelY = rect.minY + (row.height > 45 ? 8 : 11)
        if case .selectionOption = row.kind {} else {
            text(row.title, in: host, rect: CGRect(x: 20, y: labelY, width: 175, height: 18), size: 11.5, weight: row.id == "credits" ? .semibold : .medium, color: row.id == "credits" ? accent : primary)
        }
        switch row.kind {
        case .selectionOption(let selected, let detail, let available):
            let plate = CAShapeLayer(); plate.path = cutCorner(rect.insetBy(dx: 1, dy: 3), corner: 3)
            plate.fillColor = (selected ? accent.withAlphaComponent(0.13) : muted.withAlphaComponent(0.07)).cgColor
            plate.strokeColor = (selected ? accent.withAlphaComponent(0.7) : muted.withAlphaComponent(0.2)).cgColor
            plate.lineWidth = 0.7; host.addSublayer(plate)
            if available { HUDControlHighlightLayer.add(to: host, rect: rect.insetBy(dx: 1, dy: 3), shape: .cutCorner, framed: true) }
            text(row.title, in: host, rect: CGRect(x: 26, y: rect.minY + (detail.isEmpty ? 11 : 7), width: 305, height: 18),
                 size: 11.5, weight: .medium, color: available ? primary : muted)
            if !detail.isEmpty {
                text(detail, in: host, rect: CGRect(x: 26, y: rect.minY + 27, width: 305, height: 13), size: 8.5, color: muted)
            }
            if selected { text("✓", in: host, rect: CGRect(x: 346, y: rect.midY - 9, width: 24, height: 18), size: 12, weight: .semibold, color: accent) }
        case .toggle(let enabled):
            let track = CAShapeLayer(); track.path = cutCorner(CGRect(x: 324, y: rect.minY + 10, width: 50, height: 20), corner: 3)
            track.fillColor = (enabled ? accent.withAlphaComponent(0.24) : muted.withAlphaComponent(0.12)).cgColor; track.strokeColor = (enabled ? accent : muted.withAlphaComponent(0.5)).cgColor; track.lineWidth = 0.7; host.addSublayer(track)
            HUDControlHighlightLayer.add(to: host, rect: CGRect(x: 321, y: rect.minY + 7, width: 56, height: 26), shape: .cutCorner, framed: true)
            let marker = CALayer(); marker.frame = CGRect(x: enabled ? 350 : 328, y: rect.minY + 14, width: 20, height: 12); marker.backgroundColor = (enabled ? accent : muted).cgColor; host.addSublayer(marker)
            text(enabled ? "ON" : "OFF", in: host, rect: CGRect(x: 270, y: rect.minY + 13, width: 42, height: 15), size: 9, color: enabled ? accent : muted, alignment: .right)
        case .choice(let value):
            let recording = row.id == "capture" && isCapturingShortcut
            smallButton(value, rect: CGRect(x: 196, y: rect.minY + 6, width: 179, height: row.height > 45 ? 34 : 27), in: host, highlighted: recording)
        case .slider(let value, let lower, let upper):
            text(formatted(value, id: row.id), in: host, rect: CGRect(x: 20, y: rect.minY + 26, width: 167, height: 13), size: 8.5, color: muted)
            let fraction = CGFloat(min(1, max(0, (value - lower) / (upper - lower))))
            let y = rect.minY + 20.5
            let track = CALayer(); track.frame = CGRect(x: 198, y: y, width: 172, height: 2); track.backgroundColor = muted.withAlphaComponent(0.3).cgColor; host.addSublayer(track)
            let fill = CALayer(); fill.frame = CGRect(x: 198, y: y, width: 172 * fraction, height: 2); fill.backgroundColor = accent.cgColor; host.addSublayer(fill)
            let handle = CAShapeLayer(); handle.path = cutCorner(CGRect(x: 194 + 172 * fraction, y: y - 6, width: 8, height: 14), corner: 2); handle.fillColor = accent.cgColor; host.addSublayer(handle)
        case .icons(let presets):
            for (index, icon) in presets.enumerated() {
                let r = iconRect(index: index, row: rect)
                let selected = config.applicationIcon == icon
                let plate = CAShapeLayer(); plate.path = cutCorner(r.insetBy(dx: 2, dy: 2), corner: 4)
                plate.fillColor = (selected ? accent.withAlphaComponent(0.16) : muted.withAlphaComponent(0.09)).cgColor
                host.addSublayer(plate)
                let frame = CAShapeLayer(); frame.path = cutCorner(r, corner: 5); frame.fillColor = nil
                frame.strokeColor = (selected ? accent : muted.withAlphaComponent(0.42)).cgColor; frame.lineWidth = 0.8; host.addSublayer(frame)
                HUDControlHighlightLayer.add(to: host, rect: r, shape: .cutCorner, framed: true)
                let glyph = CALayer(); glyph.frame = CGRect(x: r.midX - 26, y: r.midY - 26, width: 52, height: 52)
                glyph.contents = icon.image().cgImage(forProposedRect: nil, context: nil, hints: nil); glyph.contentsGravity = .resizeAspect
                host.addSublayer(glyph)
            }
        case .palette:
            for (index, hex) in HUDSettingsController.presetAccentHexes.enumerated() {
                var c = config; c.accentHex = hex
                let r = paletteRect(index: index, row: rect), swatch = CAShapeLayer()
                swatch.path = cutCorner(r.insetBy(dx: 2, dy: 2), corner: 3); swatch.fillColor = c.accentColor.cgColor; host.addSublayer(swatch)
                HUDControlHighlightLayer.add(to: host, rect: r, shape: .cutCorner, framed: true)
                if config.accentHex == hex {
                    let border = CAShapeLayer(); border.path = cutCorner(r, corner: 4); border.fillColor = nil; border.strokeColor = primary.cgColor; border.lineWidth = 1; host.addSublayer(border)
                }
            }
            smallButton("◉", rect: paletteRect(index: 5, row: rect), in: host)
        case .info(let value), .link(let value, _):
            if case .link(_, let url) = row.kind, url != nil {
                HUDControlHighlightLayer.add(to: host, rect: rect.insetBy(dx: 0, dy: 2), shape: .cutCorner, framed: true)
            }
            text(value, in: host, rect: CGRect(x: 201, y: rect.minY + 10, width: 175, height: row.height - 13), size: 10, color: muted, alignment: .right, wrapped: true)
        case .heading: break
        }
    }
    private func renderRestore() {
        text(L10n.text("Restore all preferences?", "恢复所有设置？"), in: rowsLayer, rect: CGRect(x: 35, y: 114, width: 330, height: 25), size: 17, weight: .medium, color: primary, alignment: .center)
        text(L10n.text("Notes, files and shortcuts are kept.", "保留便笺、文件与应用快捷方式。"), in: rowsLayer, rect: CGRect(x: 35, y: 151, width: 330, height: 34), size: 11, color: muted, alignment: .center)
        smallButton(L10n.text("Cancel", "取消"), rect: CGRect(x: 46, y: 197, width: 144, height: 32), in: rowsLayer)
        smallButton(L10n.text("Restore", "恢复"), rect: CGRect(x: 206, y: 197, width: 144, height: 32), in: rowsLayer, highlighted: true)
    }
    private func iconRect(index: Int, row: CGRect) -> CGRect { CGRect(x: 23 + CGFloat(index) * 90, y: row.minY + 5, width: 82, height: 66) }
    private func paletteRect(index: Int, row: CGRect) -> CGRect { CGRect(x: 198 + CGFloat(index) * 30, y: row.minY + 17, width: 25, height: 25) }
    private func installPageLayers() {
        pageContent.frame = layer.bounds; pageContent.allowsGroupOpacity = false
        pageContent.name = "settings.page"
        rowsLayer.frame = layer.bounds; rowsLayer.allowsGroupOpacity = false
        let rowMask = CAShapeLayer(); rowMask.path = CGPath(rect: Self.viewport, transform: nil); rowMask.fillColor = NSColor.black.cgColor
        rowsLayer.mask = rowMask
        chrome.frame = layer.bounds
        pageContent.addSublayer(rowsLayer); pageContent.addSublayer(chrome); layer.addSublayer(pageContent)
        pageMask = CAShapeLayer()
        pageMask.name = "settings.page.mask"; pageMask.frame = layer.bounds
        pageMask.fillColor = NSColor.black.cgColor
        pageMask.path = CGPath(rect: layer.bounds, transform: nil)
        pageContent.mask = pageMask
    }
    /// Keep both pages on the same plane and use one continuous hand-off edge.
    /// The previous strip shutter and separate rectangular retract exposed gaps
    /// and overlapping text because their reveal boundaries moved differently.
    private func reveal(direction: CGFloat, change: () -> Void) {
        settleTransition()
        let previous = pageContent, previousMask = pageMask
        pageContent = CALayer(); rowsLayer = CALayer(); chrome = CALayer()
        withoutActions { installPageLayers() }
        change(); refresh()
        guard active, !shouldReduceMotion() else {
            withoutActions { previous.removeFromSuperlayer() }
            return
        }
        outgoingPage = previous
        let sign: CGFloat = direction < 0 ? -1 : 1
        let bounds = layer.bounds
        let collapsedIncoming = CGRect(x: sign > 0 ? bounds.maxX : bounds.minX,
            y: bounds.minY, width: 0, height: bounds.height)
        let collapsedOutgoing = CGRect(x: sign > 0 ? bounds.minX : bounds.maxX,
            y: bounds.minY, width: 0, height: bounds.height)
        let timing = CAMediaTimingFunction(controlPoints: 0.2, 0.7, 0.3, 1)
        let retractMask = previousMask
        withoutActions {
            retractMask.path = CGPath(rect: collapsedOutgoing, transform: nil)
            previous.sublayerTransform = CATransform3DMakeTranslation(-18 * sign, 0, 0)
        }
        func revealAnimation(from: CGRect, to: CGRect) -> CABasicAnimation {
            let animation = CABasicAnimation(keyPath: "path")
            animation.fromValue = CGPath(rect: from, transform: nil)
            animation.toValue = CGPath(rect: to, transform: nil)
            animation.duration = HUDSubsectionTransition.duration
            animation.timingFunction = timing
            return animation
        }
        // Matching rectangular topology, duration and easing make the masks
        // exact complements at every frame, including while content moves.
        pageMask.add(revealAnimation(from: collapsedIncoming, to: bounds), forKey: Self.pageRevealKey)
        retractMask.add(revealAnimation(from: bounds, to: collapsedOutgoing), forKey: Self.pageRevealKey)
        func move(_ content: CALayer, from: CGFloat, to: CGFloat) {
            let animation = CABasicAnimation(keyPath: "sublayerTransform")
            animation.fromValue = NSValue(caTransform3D: CATransform3DMakeTranslation(from, 0, 0))
            animation.toValue = NSValue(caTransform3D: CATransform3DMakeTranslation(to, 0, 0))
            animation.duration = HUDSubsectionTransition.duration
            animation.timingFunction = timing
            content.add(animation, forKey: Self.pageMovementKey)
        }
        // Move artwork beneath stationary masks, never the masks themselves.
        move(pageContent, from: 18 * sign, to: 0)
        move(previous, from: 0, to: -18 * sign)
        let generation = pageGeneration
        let completion = DispatchWorkItem { [weak self] in
            guard let self, self.pageGeneration == generation else { return }
            self.settleTransition(); self.onChange?()
        }
        pageCompletion = completion
        DispatchQueue.main.asyncAfter(deadline: .now() + HUDSubsectionTransition.duration, execute: completion)
        onChange?()
    }
    func settleTransition() {
        pageGeneration += 1; pageCompletion?.cancel(); pageCompletion = nil
        withoutActions {
            outgoingPage?.removeAllAnimations(); outgoingPage?.mask?.removeAllAnimations()
            outgoingPage?.removeFromSuperlayer(); outgoingPage = nil
            pageContent.removeAnimation(forKey: Self.pageMovementKey)
            pageMask.removeAnimation(forKey: Self.pageRevealKey)
            pageContent.sublayerTransform = CATransform3DIdentity
            pageMask.path = CGPath(rect: layer.bounds, transform: nil)
        }
    }
    private func animateRow(_ id: String) {
        let key = id.hasPrefix("appIcon:") ? "icons:\((HUDApplicationIcon.pickerCases.firstIndex(of: config.applicationIcon) ?? 0) / 4 * 4)" : id.hasPrefix("accent:") || id == "customColor" ? "palette" : id == "capture:cancel" ? "capture" : id
        guard active, !shouldReduceMotion(), let row = rowLayers[key] else { return }
        let move = CABasicAnimation(keyPath: "transform.translation.x"); move.fromValue = 3; move.toValue = 0; move.duration = 0.16
        move.timingFunction = CAMediaTimingFunction(name: .easeOut); row.add(move, forKey: "settings.action.depth")
        let opacity = CABasicAnimation(keyPath: "opacity"); opacity.fromValue = 0.68; opacity.toValue = 1; opacity.duration = 0.16; row.add(opacity, forKey: "settings.action.engage")
    }
    private func smallButton(_ value: String, rect: CGRect, in host: CALayer, highlighted: Bool = false) {
        let plate = CAShapeLayer(); plate.path = cutCorner(rect, corner: 3); plate.fillColor = (highlighted ? accent.withAlphaComponent(0.22) : muted.withAlphaComponent(0.12)).cgColor
        plate.strokeColor = (highlighted ? accent : muted.withAlphaComponent(0.48)).cgColor; plate.lineWidth = 0.7; host.addSublayer(plate)
        HUDControlHighlightLayer.add(to: host, rect: rect, shape: .cutCorner, framed: true)
        text(value, in: host, rect: CGRect(x: rect.minX + 6, y: rect.midY - 7, width: rect.width - 12, height: 18), size: 10.5, weight: .medium, color: highlighted ? accent : primary, alignment: .center)
    }
    private func text(_ value: String, in host: CALayer, rect: CGRect, size: CGFloat, weight: NSFont.Weight = .regular, color: NSColor, alignment: CATextLayerAlignmentMode = .left, wrapped: Bool = false) {
        let label = CATextLayer(); label.frame = rect; label.string = value; label.font = NSFont.systemFont(ofSize: size, weight: weight); label.fontSize = size
        label.foregroundColor = color.cgColor; label.alignmentMode = alignment; label.isWrapped = wrapped; label.truncationMode = wrapped ? .none : .end
        label.contentsScale = HUDRenderScale.contentScale(for: label, baseScale: renderScale); host.addSublayer(label)
    }
    private func cutCorner(_ r: CGRect, corner: CGFloat) -> CGPath {
        let p = CGMutablePath(); p.move(to: CGPoint(x: r.minX + corner, y: r.minY)); p.addLine(to: CGPoint(x: r.maxX, y: r.minY)); p.addLine(to: CGPoint(x: r.maxX, y: r.maxY - corner)); p.addLine(to: CGPoint(x: r.maxX - corner, y: r.maxY)); p.addLine(to: CGPoint(x: r.minX, y: r.maxY)); p.addLine(to: CGPoint(x: r.minX, y: r.minY + corner)); p.closeSubpath(); return p
    }
    private func formatted(_ value: Double, id: String) -> String {
        if id == "duration" { return "\(Int(value.rounded())) " + L10n.text("seconds", "秒") }
        if id == "uiScale" || id == "batterySize" { return String(format: "%.2f×", value) }
        if id == "positionX" || id == "positionY" { return String(format: "%+.0f%%", value * 100) }
        return "\(Int((value * 100).rounded()))%"
    }
    private func languageTitle(_ value: AppLanguage) -> String {
        switch value {
        case .system: return L10n.text("System", "跟随系统")
        case .english: return "English"
        case .simplifiedChinese: return "简体中文"
        case .traditionalChinese: return "繁體中文"
        case .japanese: return "日本語"
        }
    }
    private func themeTitle(_ value: OverlayTheme) -> String {
        switch value { case .dark: return L10n.text("Dark", "深色"); case .light: return L10n.text("Light", "浅色"); case .system: return L10n.text("System", "跟随系统") }
    }
    private func next<T: Equatable>(_ value: T, in values: [T]) -> T { values[((values.firstIndex(of: value) ?? 0) + 1) % values.count] }
    private func withoutActions(_ body: () -> Void) { CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit() }
}
