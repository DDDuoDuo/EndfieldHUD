import AppKit
import QuartzCore

/// Volume (spec §3.13): the Devices tab (output and input menus, the output
/// volume with its mute button, centre-zero balance) and the App volume tab
/// (one row per audio process: three visible, two while a per-app status line
/// shows; the list glides on the wheel and drags). The controllers own the
/// hardware and the per-app routes and validate every change; the canvas
/// presents them and updates its controls in place.
final class VolumeCanvas: HUDDesignCanvas {
    private let controller: AudioDeviceController
    private let perAppAudio: PerAppAudioController
    private var observers: (devices: UUID?, routes: UUID?) = (nil, nil)
    /// 0 Devices, 1 App volume; kept across visits (spec §2.9).
    private(set) var tab = 0
    private struct Structure: Equatable {
        let language: AppLanguage, tab: Int, apps: [UInt32], supported: Bool, status: Bool
    }
    private var renderedStructure: Structure?
    /// Updates of the visible controls from the current snapshot and routes.
    private var binders: [() -> Void] = []
    /// The slider the arrow keys adjust: the last one touched, else Volume.
    private weak var selectedSlider: HUDSlider?
    private weak var masterSlider: HUDSlider?
    private var appList: HUDScrollArea?
    private var panOrigin: (y: CGFloat, offset: CGFloat)?
    /// A slider gesture belongs to the output device on which it began.
    private struct Gesture { let output: UInt32? }
    private var gesture: Gesture?
    private var lastStatus: String?
    private var iconCache: [ObjectIdentifier: (scale: CGFloat, image: CGImage)] = [:]

    private static let listTop = HUDMetric.headerHeight + 34 + 10
    /// A compact slider row with its PID line grows to 54 px (demo layout).
    private static let appRowHeight: CGFloat = 54
    /// Rows the app list shows: three, or two while the status line takes the third.
    var visibleApplicationRows: Int { perAppAudio.statusMessage == nil ? 3 : 2 }
    var applicationScrollOffset: CGFloat { appList?.offset ?? 0 }
    private var appViewport: CGRect {
        let rows = CGFloat(visibleApplicationRows)
        return CGRect(x: 0, y: Self.listTop, width: HUDMetric.box.width,
                      height: rows * Self.appRowHeight + (rows - 1) * HUDMetric.rowGap)
    }

    init(context: HUDDesignContext, controller: AudioDeviceController, perAppAudio: PerAppAudioController = .fixture()) {
        self.controller = controller; self.perAppAudio = perAppAudio
        super.init(module: .volume, context: context)
        observers = (controller.observe { [weak self] in self?.changed() }, perAppAudio.observe { [weak self] in self?.changed() })
    }

    deinit {
        if let id = observers.devices { controller.removeObserver(id) }
        if let id = observers.routes { perAppAudio.removeObserver(id) }
        // Device observation ends here; enabled per-app routes deliberately
        // survive leaving or destroying the Volume surface.
        if isActive { controller.stop() }
    }

    // MARK: Lifecycle

    override func didActivate() {
        controller.start()
        lastStatus = controller.statusMessage
        changed()
    }

    override func didDeactivate() {
        controller.stop()
        gesture = nil; panOrigin = nil
    }

    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        if renderedLanguage != nil { changed() }
        return super.makeContent(for: module, style: style)
    }

    override func recordingModeChanged() {}

    private var snapshot: AudioDeviceSnapshot { controller.snapshot }
    private var currentOutput: AudioDeviceInfo? { snapshot.outputs.first { $0.id == snapshot.defaultOutputID } }

    private func session(_ id: UInt32) -> PerAppAudioSession? { perAppAudio.sessions.first { $0.processID == id } }

    /// Audio processes with an available route, plus every enabled route even
    /// while its app is silent and absent from the active-process list.
    var listedApplications: [AudioApplicationInfo] {
        guard snapshot.applicationActivitySupported else { return [] }
        var apps = snapshot.availableApplications.isEmpty ? snapshot.activeApplications : snapshot.availableApplications
        for session in perAppAudio.sessions where !apps.contains(where: { $0.id == session.processID }) {
            apps.append(AudioApplicationInfo(id: session.processID, pid: session.pid, name: session.name,
                isRunningOutput: false, applicationURL: session.applicationURL, icon: session.icon))
        }
        return apps.filter { app in
            session(app.id) != nil || currentOutput.map { perAppAudio.availability(application: app, output: $0) == nil } ?? false
        }
    }

    private var structure: Structure {
        Structure(language: L10n.resolvedLanguage, tab: tab, apps: tab == 1 ? listedApplications.map(\.id) : [],
                  supported: snapshot.applicationActivitySupported, status: tab == 1 && perAppAudio.statusMessage != nil)
    }

    private func changed() {
        guard renderedLanguage != nil else { return }
        // A gesture never retargets to another device or a control that became read-only.
        if let gesture, gesture.output != snapshot.defaultOutputID { cancelSliderGestures() }
        // A route starting under a drag changes the list's status line; the
        // rebuild waits for the release so the gesture is never cut off.
        if structure != renderedStructure, !isSliding { rerender() } else { binders.forEach { $0() } }
        guard isActive else { return }
        let status = controller.statusMessage
        if let status, status != lastStatus { toast(icon: .warn, "Notice", "提示", detail: status) }
        lastStatus = status
    }

    private var isSliding: Bool { page.controls.contains { ($0 as? HUDSlider)?.isDragging == true } }

    private func cancelSliderGestures() {
        for case let slider as HUDSlider in page.controls where slider.isDragging { slider.cancelDrag() }
        gesture = nil
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        binders = []; appList = nil; masterSlider = nil; selectedSlider = nil
        renderedStructure = structure
        let tabs = HUDTabs(id: "volumeTab", titles: [(L10n.text("Devices", "设备"), .devices), (L10n.text("App volume", "应用音量"), .apps)],
                           selectedIndex: tab, width: HUDMetric.box.width, env: env)
        // The orange dot marks App volume only while some app's volume can be adjusted.
        tabs.tabs[1].hasNews = hasAdjustableApplication
        tabs.onSelect = { [weak self] index, direction in
            self?.tab = index
            self?.navigate(dx: direction)
        }
        HUDMotion.perform { tabs.layer.frame.origin = CGPoint(x: 0, y: HUDMetric.headerHeight); page.layer.addSublayer(tabs.layer) }
        page.stagger(tabs.layer)
        page.tabs = tabs
        page.add(tabs.controls)
        if tab == 0 { buildDevices(page) } else { buildApplications(page) }
        header.configure(crumbs: ["VOLUME"], code: "MOD-08", title: HUDModuleTitle("Volume", "音量"), counter: counterText, showsBack: false)
        binders.append { [weak self, weak tabs] in
            guard let self else { return }
            let adjustable = self.hasAdjustableApplication
            if let tab = tabs?.tabs[1], tab.hasNews != adjustable { tab.hasNews = adjustable }
            guard self.masterSlider?.isDragging != true else { return }
            self.header.setCounter(self.counterText)
        }
    }

    /// At least one listed app whose route accepts a gain change.
    var hasAdjustableApplication: Bool { listedApplications.contains(where: isRouteAdjustable) }

    /// Master volume as two digits, or MUTED (spec §3.13 header).
    private var counterText: String {
        if snapshot.outputMuted == true { return L10n.text("MUTED", "已静音") }
        return HUDFormat.pad2(Int(((snapshot.outputVolume ?? 0) * 100).rounded()))
    }

    private func buildDevices(_ page: HUDDesignPage) {
        let output = dropdown("output", L10n.text("Output", "输出"), input: false)
        let input = dropdown("input", L10n.text("Input", "输入"), input: true)
        let mute = HUDIconButton(id: "mute", label: muteLabel, icon: snapshot.outputMuted == true ? .mute : .vol, env: env)
        mute.onActivate = { [weak self] in
            guard let self, self.snapshot.canSetOutputMute, let muted = self.snapshot.outputMuted else { return }
            _ = self.controller.setOutputMuted(!muted)
        }
        binders.append { [weak self, weak mute] in
            guard let self, let mute else { return }
            let muted = self.snapshot.outputMuted == true
            mute.isOn = muted; mute.setIcon(muted ? .mute : .vol); mute.label = self.muteLabel
            mute.isEnabled = self.snapshot.canSetOutputMute && self.snapshot.outputMuted != nil
        }
        let volume = sliderRow("volume", L10n.text("Volume", "音量"), accessibility: L10n.text("Output volume", "输出音量"),
                               range: HUDSlider.Range(minimum: 0, maximum: 100, step: 1), format: .percent, trailing: mute,
                               read: { $0.outputVolume.map { $0 * 100 } }, writable: { $0.canSetOutputVolume && $0.outputVolume != nil }) {
            [weak self] value, _ in
            guard let self else { return }
            self.header.setCounter(HUDFormat.pad2(Int(value.rounded())))
            _ = self.controller.setOutputVolume(value / 100)
        }
        masterSlider = volume.slider
        let balance = sliderRow("balance", L10n.text("Balance", "平衡"), accessibility: L10n.text("Left/right balance", "左右声道平衡"),
                                range: HUDSlider.Range(minimum: -100, maximum: 100, step: 1, zero: 0), format: .balance,
                                read: { $0.balance.map { $0 * 100 } }, writable: { $0.canSetBalance && $0.balance != nil }) {
            [weak self] value, _ in _ = self?.controller.setBalance(value / 100)
        }
        var rows: [HUDRowPlate] = []
        for (id, title, field) in [("output", L10n.text("Output", "输出"), output), ("input", L10n.text("Input", "输入"), input)] {
            let plate = HUDRowPlate(id: "row:" + id, title: title, variant: .compact, env: env)
            plate.setCluster([field.layer])
            rows.append(plate)
        }
        rows += [volume.row, balance.row]
        var y = Self.listTop
        HUDMotion.perform {
            for row in rows {
                row.layer.frame.origin = CGPoint(x: 0, y: y)
                page.layer.addSublayer(row.layer)
                y += row.layer.bounds.height + HUDMetric.rowGap
            }
        }
        rows.forEach { page.stagger($0.layer) }
        page.add([output, input, volume.slider, mute, balance.slider])
        binders.forEach { $0() }
    }

    private var muteLabel: String { snapshot.outputMuted == true ? L10n.text("Unmute", "取消静音") : L10n.text("Mute", "静音") }

    private func dropdown(_ id: String, _ label: String, input: Bool) -> HUDDropdown {
        let field = HUDDropdown(id: id, label: label, value: deviceName(input: input), env: env)
        field.onActivate = { [weak self, weak field] in
            guard let self, let field else { return }
            if self.isMenuOpen { self.closeMenu() } else { self.openDeviceMenu(input: input, anchor: field) }
        }
        binders.append { [weak self, weak field] in
            guard let self, let field else { return }
            field.setValue(self.deviceName(input: input))
            field.isEnabled = input ? self.snapshot.canSetDefaultInput && !self.snapshot.inputs.isEmpty
                                    : self.snapshot.canSetDefaultOutput && !self.snapshot.outputs.isEmpty
        }
        return field
    }

    private func deviceName(input: Bool) -> String {
        let device = input ? snapshot.inputs.first { $0.id == snapshot.defaultInputID } : currentOutput
        return device?.name ?? L10n.text("No device", "无设备")
    }

    /// Every connected device, headphones and Bluetooth included.
    private func openDeviceMenu(input: Bool, anchor: HUDDropdown) {
        let devices = input ? snapshot.inputs : snapshot.outputs
        let selected = input ? snapshot.defaultInputID : snapshot.defaultOutputID
        let items = devices.map { HUDMenu.Item(id: String($0.id), title: $0.name, selected: $0.id == selected,
                                               enabled: input ? $0.canBeDefaultInput : $0.canBeDefaultOutput) }
        openMenu(id: input ? "input" : "output", items: items, anchor: anchor) { [weak self] item in
            guard let self, let id = UInt32(item.id) else { return }
            if input { _ = self.controller.setDefaultInput(id); return }
            if id != self.snapshot.defaultOutputID {
                self.perAppAudio.stopAll(reason: L10n.text("App routing stopped because the output device changed.", "输出设备已更改，应用混音已停止。"))
            }
            _ = self.controller.setDefaultOutput(id)
        }
    }

    /// A compact slider row; `trailing` (the mute button) sits after the readout.
    private func sliderRow(_ id: String, _ title: String, accessibility: String, range: HUDSlider.Range, format: HUDValueFormat,
                           trailing: HUDIconButton? = nil, width: CGFloat = HUDMetric.box.width, height: CGFloat? = nil,
                           read: @escaping (AudioDeviceSnapshot) -> Double?, writable: @escaping (AudioDeviceSnapshot) -> Bool,
                           change: @escaping (Double, Bool) -> Void) -> (row: HUDRowPlate, slider: HUDSlider) {
        let plate = HUDRowPlate(id: "row:" + id, title: title, variant: .slider, width: width, height: height, env: env)
        var area = plate.controlArea
        if let trailing { area.size.width -= trailing.layer.bounds.width + 8 }
        let slider = HUDSlider(id: id, label: accessibility, value: read(snapshot) ?? range.minimum, range: range, format: format,
                               width: area.width, env: env)
        HUDMotion.perform {
            slider.layer.frame.origin = area.origin
            plate.layer.addSublayer(slider.layer)
            if let trailing {
                trailing.layer.position = CGPoint(x: area.maxX + 8 + trailing.layer.bounds.width / 2, y: area.midY)
                plate.layer.addSublayer(trailing.layer)
            }
        }
        hostBubble(slider)
        slider.onChange = { [weak self, weak slider] value, final in
            guard let self else { return }
            self.selectedSlider = slider
            if self.gesture == nil { self.gesture = Gesture(output: self.snapshot.defaultOutputID) }
            guard self.gesture?.output == self.snapshot.defaultOutputID else { return }
            change(value, final)
            if final { self.gesture = nil; self.changed(); self.context.onInputStateChange?() }
        }
        binders.append { [weak self, weak slider] in
            guard let self, let slider else { return }
            let enabled = writable(self.snapshot)
            if slider.isDragging, !enabled { slider.cancelDrag(); self.gesture = nil }
            slider.isEnabled = enabled
            if !slider.isDragging { slider.setValue(read(self.snapshot) ?? range.minimum) }
        }
        return (plate, slider)
    }

    private func buildApplications(_ page: HUDDesignPage) {
        let apps = listedApplications
        let viewport = appViewport
        guard snapshot.applicationActivitySupported, !apps.isEmpty else {
            let title = L10n.text("No adjustable audio apps", "暂无可调节音量的应用")
            let detail = snapshot.applicationActivitySupported ? snapshot.applicationActivityMessage
                : snapshot.applicationActivityMessage ?? L10n.text("Audio app detection requires macOS 14.2 or later", "音频应用检测需要 macOS 14.2 或更高版本")
            let empty = HUDEmptyState.make(title: title.uppercased(), subtitle: detail,
                                           size: CGSize(width: HUDMetric.box.width, height: appViewport.height), env: env)
            HUDMotion.perform { empty.frame.origin = CGPoint(x: 0, y: Self.listTop); page.layer.addSublayer(empty) }
            page.stagger(empty)
            buildStatus(page, top: viewport.maxY + 10)
            return
        }
        let area = page.scrollArea(id: "apps", frame: viewport)
        let count = CGFloat(apps.count)
        area.setContentHeight(count * Self.appRowHeight + (count - 1) * HUDMetric.rowGap)
        // Presses between the sliders drag the list (sliders sit above it).
        let pan = VolumeListPan(id: "apps.pan", env: env)
        HUDMotion.perform { pan.layer.frame = area.layer.bounds; area.layer.insertSublayer(pan.layer, at: 0) }
        pan.onBegin = { [weak self, weak area] y in
            guard let area else { return }
            self?.panOrigin = (y, area.offset)
        }
        pan.onDrag = { [weak self, weak area] y in
            guard let self, let area, let origin = self.panOrigin else { return }
            area.setOffset(origin.offset - (y - origin.y), animated: false, env: self.env)
        }
        page.add(pan)
        var y: CGFloat = 0
        for app in apps {
            let row = applicationRow(app, width: area.contentWidth, page: page)
            HUDMotion.perform { row.layer.frame.origin = CGPoint(x: 0, y: y); area.content.addSublayer(row.layer) }
            page.stagger(row.layer)
            y += Self.appRowHeight + HUDMetric.rowGap
        }
        appList = area
        page.restoreOffset(of: area)
        buildStatus(page, top: viewport.maxY + 10)
        binders.forEach { $0() }
    }

    /// The per-app status line, in the third row's place (Windows A2).
    private func buildStatus(_ page: HUDDesignPage, top: CGFloat) {
        guard let message = perAppAudio.statusMessage else { return }
        let text = HUDTextLayer(message, style: HUDTextStyle(size: 11, weight: .regular, color: HUDColor.steel, lineHeight: 1.4))
        text.maximumLines = 2
        HUDMotion.perform { text.lineFrame = CGRect(x: 18, y: top, width: HUDMetric.box.width - 36, height: 11 * 1.4 * 2) }
        binders.append { [weak self, weak text] in
            guard let self, let text, let message = self.perAppAudio.statusMessage else { return }
            HUDMotion.perform { text.text = message }
        }
        HUDMotion.perform { page.layer.addSublayer(text) }
        env.applyScale(to: text)
        page.stagger(text)
    }

    /// Icon square, app name, `PID n` (or the route's state) and its slider.
    private func applicationRow(_ app: AudioApplicationInfo, width: CGFloat, page: HUDDesignPage) -> HUDRowPlate {
        let id = "app:\(app.id)"
        let row = sliderRow(id, "", accessibility: (app.name == "PID \(app.pid)" ? app.name : app.name + " / PID \(app.pid)")
                                + L10n.text(" volume", " 音量"),
                            range: HUDSlider.Range(minimum: 0, maximum: 100, step: 1), format: .percent, width: width,
                            height: Self.appRowHeight,
                            read: { [weak self] _ in self.map { ($0.session(app.id)?.gain ?? 1) * 100 } },
                            writable: { [weak self] _ in self?.isRouteAdjustable(app) ?? false }) { [weak self] value, _ in
            self?.setGain(value / 100, for: app)
        }
        let plate = row.row
        plate.setSublabel(routeState(app))
        let icon = HUDLayer.plain("volume.app.icon")
        let name = HUDTextLayer(app.name, style: HUDType.rowLabel)
        HUDMotion.perform {
            // The 18 px square and the name share the label line (centre y 20.5).
            icon.frame = CGRect(x: 18, y: 11.5, width: 18, height: 18)
            if let image = iconImage(app) { icon.contents = image; icon.contentsGravity = .resizeAspect }
            else { icon.backgroundColor = HUDColor.rgb(0x4A4A4A) }
            name.lineFrame = CGRect(x: 18 + 18 + 8, y: 20.5 - 15.5 * 0.6, width: HUDMetric.sliderLabelColumn - 26, height: 15.5 * 1.2)
            plate.layer.addSublayer(icon); plate.layer.addSublayer(name)
        }
        env.applyScale(to: name)
        binders.append { [weak self, weak plate] in
            guard let self, let plate else { return }
            plate.setSublabel(self.routeState(app))
        }
        page.add(row.slider)
        return plate
    }

    private func isRouteAdjustable(_ app: AudioApplicationInfo) -> Bool {
        if let session = session(app.id) { return session.state != .stopping }
        return currentOutput.map { perAppAudio.availability(application: app, output: $0) == nil } ?? false
    }

    private func routeState(_ app: AudioApplicationInfo) -> String {
        switch session(app.id)?.state {
        case .active?: return "PID \(app.pid)"
        case .preparing?: return L10n.text("Starting…", "正在启动…")
        case .stopping?: return L10n.text("Stopping…", "正在停止…")
        case .failed?: return L10n.text("Route failed", "路由失败")
        case nil: return isRouteAdjustable(app) ? "PID \(app.pid)" : L10n.text("Unsupported route", "暂不支持此路由")
        }
    }

    /// 100% is an ordinary gain on an enabled route (crossing it never
    /// rebuilds taps); untouched direct playback stays untouched at 100%;
    /// back at 100% a failed route is retired.
    @discardableResult func setGain(_ value: Double, for app: AudioApplicationInfo) -> Bool {
        guard value.isFinite, isRouteAdjustable(app) else { return false }
        let bounded = min(1, max(0, value))
        if let session = session(app.id) {
            if session.state == .failed, bounded == 1 { perAppAudio.stop(processID: app.id); return true }
            return perAppAudio.setGain(bounded, processID: app.id)
        }
        if bounded == 1 { return true }
        guard let output = currentOutput else { return false }
        return perAppAudio.start(application: app, output: output, initialGain: bounded)
    }

    private func iconImage(_ app: AudioApplicationInfo) -> CGImage? {
        guard let source = app.icon else { return nil }
        let key = ObjectIdentifier(source), scale = env.renderScale
        if let cached = iconCache[key], cached.scale == scale { return cached.image }
        var proposed = CGRect(x: 0, y: 0, width: 18 * scale, height: 18 * scale)
        guard let image = source.cgImage(forProposedRect: &proposed, context: nil, hints: nil) else { return nil }
        if iconCache.count >= 64 { iconCache.removeAll(keepingCapacity: true) }
        iconCache[key] = (scale, image)
        return image
    }

    // MARK: Input

    /// The wheel glides the app list (Windows A2) instead of jumping.
    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        guard let list = appList, list.layer.frame.contains(point), list.maximumOffset > 0 else { return false }
        list.setOffset(list.offset + delta.y, animated: true, env: env)
        return true
    }

    /// ← / → adjust the last slider touched (Volume until another is).
    override func handleKey(_ event: NSEvent) -> Bool {
        guard event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty,
              event.keyCode == 123 || event.keyCode == 124 else { return false }
        guard let slider = selectedSlider ?? masterSlider ?? page.controls.lazy.compactMap({ $0 as? HUDSlider }).first,
              slider.isEnabled else { return false }
        selectedSlider = slider
        slider.nudge(event.keyCode == 123 ? -2 : 2)
        return true
    }

    /// App rows scrolled out of the list are not offered to VoiceOver.
    override var accessibilityItems: [HUDAccessibilityItem] {
        let viewport = appList?.layer.frame
        return super.accessibilityItems.filter { item in
            guard item.id.hasPrefix("app:"), let viewport else { return true }
            return viewport.intersects(item.rect)
        }
    }
}

/// The app list's drag surface: presses between the sliders pan the list.
private final class VolumeListPan: HUDControl {
    var onBegin: ((CGFloat) -> Void)?
    var onDrag: ((CGFloat) -> Void)?
    override func pointerDown(at point: CGPoint) -> Bool { onBegin?(point.y); return true }
    override func pointerDragged(to point: CGPoint) { onDrag?(point.y) }
    override func pointerUp(at point: CGPoint) {}
    override var accessibilityRole: HUDAccessibilityItem.Role? { nil }
}
