import AppKit
import QuartzCore

struct VolumeCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
    var enabled = true
    var visibleRect: CGRect? = nil
}

struct VolumeCanvasSlider {
    let id: String
    let label: String
    let rect: CGRect
    let value: Double?
    let minimum: Double
    let maximum: Double
    let enabled: Bool
    var help: String? = nil
    var visibleRect: CGRect? = nil
}

/// Audio controls are projected artwork in the existing center host. The
/// controller owns hardware state and validates every explicit user mutation.
final class VolumeCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    var onChange: (() -> Void)?
    var isDragging: Bool { draggedSlider != nil }
    var accessibilityStatus: String {
        perAppAudio.statusMessage ?? controller.statusMessage
            ?? L10n.text("Adjust each audio process independently. 100% is full volume and keeps an enabled route running.", "独立调整每个音频进程。100% 为原始音量，已启用的路由会继续运行。")
    }
    var accessibleActions: [VolumeCanvasAction] { chooser == nil ? mainActions() : chooserActions() }
    var accessibleSliders: [VolumeCanvasSlider] {
        guard chooser == nil else { return [] }
        var sliders = [VolumeCanvasSlider(id: "volume", label: L10n.text("Output volume", "输出音量"), rect: CGRect(x: 82, y: 116, width: 235, height: 25),
                                   value: snapshot.outputVolume, minimum: 0, maximum: 1, enabled: snapshot.canSetOutputVolume && snapshot.outputVolume != nil),
                VolumeCanvasSlider(id: "balance", label: L10n.text("Left/right balance", "左右声道平衡"), rect: CGRect(x: 102, y: 158, width: 244, height: 25),
                                   value: snapshot.balance, minimum: -1, maximum: 1, enabled: snapshot.canSetBalance && snapshot.balance != nil)]
        if details == .applications {
            for row in visibleApplicationRows {
                let app = row.app
                let session = appSession(app.id)
                let available = currentOutput.map { perAppAudio.availability(application: app, output: $0) == nil } ?? false
                let enabled = session.map { $0.state != .stopping } ?? available
                let identity = app.name == "PID \(app.pid)" ? app.name : app.name + " · PID \(app.pid)"
                let rect = CGRect(x: 151, y: row.y, width: 185, height: 28)
                let visible = rect.intersection(applicationViewport)
                guard !visible.isNull, visible.height > 0 else { continue }
                sliders.append(VolumeCanvasSlider(id: "app:\(app.id)", label: identity + L10n.text(" volume", " 音量"),
                    rect: rect,
                    value: session?.gain ?? (available ? 1 : nil), minimum: 0, maximum: 1, enabled: enabled, help: appSliderHelp(app),
                    visibleRect: visible))
            }
        }
        return sliders
    }
    let applicationViewport = CGRect(x: 12, y: 234, width: 376, height: 64)
    private(set) var applicationScrollOffset: CGFloat = 0
    private(set) var pageIndex = 0
    private(set) var selectedSliderID = "volume"
    var isChoosingDevice: Bool { chooser != nil }

    private enum DeviceChooser { case output, input }
    private enum Details { case headphones, applications }
    private let controller: AudioDeviceController
    private let perAppAudio: PerAppAudioController
    private var observer: UUID?
    private var perAppObserver: UUID?
    private func appSession(_ id: UInt32) -> PerAppAudioSession? {
        perAppAudio.sessions.first { $0.processID == id }
    }
    private func appSliderHelp(_ app: AudioApplicationInfo) -> String {
        if let session = appSession(app.id) {
            if let error = session.error {
                return error + (session.state == .failed ? L10n.text(" Return to 100% to retry cleanup.", " 回到 100% 可重试清理。") : "")
            }
            switch session.state {
            case .active: return L10n.text("Adjusts only this audio process; 100% keeps its route running at full volume", "仅调整此音频进程；100% 保持路由运行并恢复原始音量")
            case .preparing: return L10n.text("Requested volume; waiting for audio processing to start", "目标音量；正在等待音频处理启动")
            case .stopping: return L10n.text("Waiting for macOS to release this route", "正在等待 macOS 释放此路由")
            case .failed: return L10n.text("Return to 100% to retry restoring normal playback", "回到 100% 可重试恢复正常播放")
            }
        }
        guard let output = currentOutput else { return L10n.text("No output device", "无输出设备") }
        return perAppAudio.availability(application: app, output: output)
            ?? L10n.text("Adjust to enable a temporary audio route; macOS may request System Audio Recording access", "调整即可启用临时音频路由；macOS 可能请求系统音频录制权限")
    }
    private var visibleApplicationRows: [(app: AudioApplicationInfo, y: CGFloat)] {
        guard snapshot.applicationActivitySupported else { return [] }
        return listedApplications.enumerated().compactMap { index, app in
            let y = applicationViewport.minY + CGFloat(index) * 31 - applicationScrollOffset
            return CGRect(x: 12, y: y, width: 376, height: 30).intersects(applicationViewport) ? (app, y) : nil
        }
    }
    private var maximumApplicationScroll: CGFloat { max(0, CGFloat(listedApplications.count) * 31 - applicationViewport.height) }
    private var currentOutput: AudioDeviceInfo? { snapshot.outputs.first { $0.id == snapshot.defaultOutputID } }
    // Retain a way back to an enabled route even when its app is temporarily
    // silent and therefore disappears from HAL's active-process list.
    private var listedApplications: [AudioApplicationInfo] {
        var apps = snapshot.availableApplications.isEmpty ? snapshot.activeApplications : snapshot.availableApplications
        for session in perAppAudio.sessions where !apps.contains(where: { $0.id == session.processID }) {
            apps.append(AudioApplicationInfo(id: session.processID, pid: session.pid,
                name: session.name, isRunningOutput: false, applicationURL: session.applicationURL, icon: session.icon))
        }
        return apps.filter { app in
            if appSession(app.id) != nil { return true }
            return currentOutput.map { perAppAudio.availability(application: app, output: $0) == nil } ?? false
        }
    }
    private var snapshot: AudioDeviceSnapshot
    private var active = false
    private var chooser: DeviceChooser?
    private var details: Details = .applications
    private var draggedSlider: String?
    private var scrollAccumulation: CGFloat = 0
    private var dark = true
    private var scale: CGFloat = 2
    private let content = CALayer()
    private let appRowsLayer = CALayer()
    private var iconCache: [ObjectIdentifier: (source: NSImage, scale: CGFloat, image: CGImage)] = [:]
    private var drawingAppRows = false
    private var drawingLayer: CALayer { drawingAppRows ? appRowsLayer : content }
    private let revealMask = CAShapeLayer()
    private var yellow: NSColor { HUDRuntimeAppearance.accent }
    private let ink = NSColor(white: 0.14, alpha: 1)
    private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.11, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.67 : 0.37, alpha: 1) }
    private var chooserDevices: [AudioDeviceInfo] { chooser == .input ? snapshot.inputs : snapshot.outputs }
    private var connectedDevices: [AudioDeviceInfo] {
        var seen: Set<UInt32> = []
        return (snapshot.outputs + snapshot.inputs).filter { ($0.isHeadphones || $0.isBluetooth) && seen.insert($0.id).inserted }
    }
    private var pageCount: Int {
        if chooser == nil && details == .applications { return 1 }
        let count = chooser != nil ? chooserDevices.count : connectedDevices.count
        let capacity = chooser == nil ? 2 : 6
        return max(1, (count + capacity - 1) / capacity)
    }

    init(controller: AudioDeviceController, perAppAudio: PerAppAudioController = .fixture()) {
        self.controller = controller; self.perAppAudio = perAppAudio
        snapshot = controller.snapshot
        super.init()
        withoutActions {
            layer.name = "module.volume.canvas"
            layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
            layer.allowsGroupOpacity = false
            content.frame = layer.bounds
            content.allowsGroupOpacity = false
            revealMask.frame = layer.bounds
            revealMask.path = CGPath(rect: layer.bounds, transform: nil)
            revealMask.fillColor = NSColor.black.cgColor
            content.mask = revealMask
            layer.addSublayer(content)
        }
        observer = controller.observe { [weak self] in
            guard let self, self.active else { return }
            self.refresh()
        }
        perAppObserver = perAppAudio.observe { [weak self] in
            guard let self, self.active else { return }
            if let id = self.draggedSlider, id.hasPrefix("app:") {
                let processID = UInt32(id.dropFirst(4))
                let state = processID.flatMap({ self.appSession($0) })?.state
                if (state != .active && state != .preparing)
                    || self.accessibleSliders.first(where: { $0.id == id })?.enabled != true { self.draggedSlider = nil }
            }
            self.applicationScrollOffset = min(self.applicationScrollOffset, self.maximumApplicationScroll)
            self.pageIndex = min(self.pageIndex, self.pageCount - 1)
            self.withoutActions { self.repaint() }; self.onChange?()
        }
    }

    deinit {
        if let observer { controller.removeObserver(observer) }
        if let perAppObserver { perAppAudio.removeObserver(perAppObserver) }
        // Only release device observation here. Existing per-app audio routes
        // deliberately survive leaving or destroying the Volume surface.
        if active { controller.stop() }
        content.removeAllAnimations(); revealMask.removeAllAnimations()
    }

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; scale = style.contentsScale
        snapshot = controller.snapshot
        withoutActions { repaint() }
        return layer
    }

    func activate() {
        guard !active else { return }
        active = true
        controller.start()
        refresh()
    }

    func deactivate() {
        active = false
        draggedSlider = nil
        chooser = nil
        selectedSliderID = "volume"
        pageIndex = 0
        applicationScrollOffset = 0
        scrollAccumulation = 0
        controller.stop()
        content.removeAllAnimations(); revealMask.removeAllAnimations()
    }

    func updateRenderScale(_ value: CGFloat) {
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard scale != next else { return }
        scale = next
        if active { withoutActions { repaint() } }
    }

    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard point.x.isFinite, point.y.isFinite, layer.bounds.contains(point) else { return false }
        if let slider = accessibleSliders.first(where: { ($0.visibleRect ?? $0.rect).contains(point) }) {
            selectedSliderID = slider.id
            if slider.enabled { draggedSlider = slider.id; moveSlider(slider.id, to: point) }
            return true
        }
        if let action = accessibleActions.first(where: { ($0.visibleRect ?? $0.rect).contains(point) }) {
            if action.enabled { perform(actionID: action.id) }
            return true
        }
        return true
    }

    func mouseDragged(to point: CGPoint) {
        guard point.x.isFinite, point.y.isFinite, let id = draggedSlider else { return }
        moveSlider(id, to: point)
    }

    func mouseUp() { draggedSlider = nil }

    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        let rect = chooser == nil ? CGRect(x: 12, y: 234, width: 376, height: 64) : CGRect(x: 12, y: 42, width: 376, height: 250)
        guard rect.contains(point), delta.isFinite, abs(delta) > 0.01 else { return false }
        if chooser == nil && details == .applications {
            let next = min(maximumApplicationScroll, max(0, applicationScrollOffset + delta))
            if next != applicationScrollOffset {
                draggedSlider = nil; selectedSliderID = "volume"
                applicationScrollOffset = next
                withoutActions { repaint() }; onChange?()
            }
            return true
        }
        if (delta > 0) != (scrollAccumulation > 0) { scrollAccumulation = 0 }
        scrollAccumulation += delta
        if abs(scrollAccumulation) >= 40 { changePage(by: scrollAccumulation > 0 ? 1 : -1); scrollAccumulation = 0 }
        return true
    }

    @discardableResult func setSlider(id: String, value: Double) -> Bool {
        guard value.isFinite, let slider = accessibleSliders.first(where: { $0.id == id }), slider.enabled else { return false }
        selectedSliderID = id
        let bounded = min(slider.maximum, max(slider.minimum, value))
        if id.hasPrefix("app:"), let processID = UInt32(id.dropFirst(4)),
           let app = visibleApplicationRows.first(where: { $0.app.id == processID })?.app {
            if let session = appSession(processID) {
                // Unity is an ordinary gain on an enabled route. Repeatedly
                // crossing 100% must not destroy/recreate taps or aggregates.
                if session.state == .failed, bounded == 1 {
                    perAppAudio.stop(processID: processID)
                    return true
                }
                return perAppAudio.setGain(bounded, processID: processID)
            }
            if bounded == 1 { return true } // Untouched direct playback stays untouched.
            guard let output = currentOutput else { return false }
            return perAppAudio.start(application: app, output: output, initialGain: bounded)
        }
        if id == "volume" { return controller.setOutputVolume(bounded) }
        if id == "balance" { return controller.setBalance(bounded) }
        return false
    }

    func nudgeSelectedSlider(by direction: Double) {
        guard let slider = accessibleSliders.first(where: { $0.id == selectedSliderID }), let current = slider.value else { return }
        _ = setSlider(id: slider.id, value: current + direction * (slider.maximum - slider.minimum) * 0.02)
    }

    @discardableResult func dismissChooser() -> Bool {
        guard chooser != nil else { return false }
        chooser = nil; pageIndex = 0; scrollAccumulation = 0
        withoutActions { repaint() }; animateAction(direction: -1); onChange?(); return true
    }

    func perform(actionID value: String) {
        let wasChoosing = chooser != nil
        switch value {
        case "audio:output":
            guard snapshot.canSetDefaultOutput else { return }
            openChooser(.output)
        case "audio:input":
            guard snapshot.canSetDefaultInput else { return }
            openChooser(.input)
        case "audio:back": _ = dismissChooser()
        case "audio:mute":
            guard snapshot.canSetOutputMute, let muted = snapshot.outputMuted else { return }
            _ = controller.setOutputMuted(!muted)
        case "audio:headphones": draggedSlider = nil; selectedSliderID = "volume"; details = .headphones; pageIndex = 0; withoutActions { repaint() }; onChange?()
        case "audio:applications": draggedSlider = nil; selectedSliderID = "volume"; details = .applications; pageIndex = 0; withoutActions { repaint() }; onChange?()
        case "audio:previous": changePage(by: -1)
        case "audio:next": changePage(by: 1)
        default:
            let parts = value.split(separator: ":")
            guard parts.count == 3, parts[0] == "audio", let id = UInt32(parts[2]) else { return }
            var success = false
            if parts[1] == "output", chooser == .output,
               snapshot.canSetDefaultOutput, snapshot.outputs.contains(where: { $0.id == id && $0.canBeDefaultOutput }) {
                if id != snapshot.defaultOutputID {
                    perAppAudio.stopAll(reason: L10n.text("App routing stopped because the output device changed.", "输出设备已更改，应用混音已停止。"))
                }
                success = controller.setDefaultOutput(id)
            } else if parts[1] == "input", chooser == .input,
                      snapshot.canSetDefaultInput, snapshot.inputs.contains(where: { $0.id == id && $0.canBeDefaultInput }) {
                success = controller.setDefaultInput(id)
            }
            if success { _ = dismissChooser() }
        }
        if value != "audio:back" && wasChoosing == (chooser != nil) { animateAction(direction: 1) }
    }

    private func openChooser(_ next: DeviceChooser) {
        draggedSlider = nil; chooser = next; pageIndex = 0; scrollAccumulation = 0
        withoutActions { repaint() }; animateAction(direction: 1); onChange?()
    }

    private func refresh() {
        let next = controller.snapshot
        if let id = draggedSlider {
            let stillWritable = id.hasPrefix("app:") ? accessibleSliders.first(where: { $0.id == id })?.enabled == true : id == "volume"
                ? next.canSetOutputVolume && next.outputVolume != nil
                : next.canSetBalance && next.balance != nil
            // A gesture belongs to the device/control on which it began. A
            // disconnect or capability change must not retarget the next
            // pointer sample to newly selected speakers or a new control.
            if next.defaultOutputID != snapshot.defaultOutputID || !stillWritable {
                draggedSlider = nil
            }
        }
        snapshot = next
        applicationScrollOffset = min(applicationScrollOffset, maximumApplicationScroll)
        pageIndex = min(pageIndex, pageCount - 1)
        withoutActions { repaint() }; onChange?()
    }

    private func moveSlider(_ id: String, to point: CGPoint) {
        guard let slider = accessibleSliders.first(where: { $0.id == id }), slider.rect.width > 12 else { return }
        let unit = Double(min(1, max(0, (point.x - slider.rect.minX - 6) / (slider.rect.width - 12))))
        _ = setSlider(id: id, value: slider.minimum + unit * (slider.maximum - slider.minimum))
    }

    private func changePage(by direction: Int) {
        scrollAccumulation = 0
        let next = min(pageCount - 1, max(0, pageIndex + direction))
        guard next != pageIndex else { return }
        draggedSlider = nil; selectedSliderID = "volume"
        pageIndex = next
        withoutActions { repaint() }; animateAction(direction: CGFloat(direction)); onChange?()
    }

    /// A bounded lateral/depth engagement plus a hard-edged mask reveal keeps
    /// device pickers and detail actions in the mechanical navigation language.
    private func animateAction(direction: CGFloat) {
        guard active, !HUDRuntimeAppearance.reduceMotion else { return }
        let move = CABasicAnimation(keyPath: "sublayerTransform")
        var from = CATransform3DIdentity; from.m34 = -1 / 700
        from = CATransform3DTranslate(from, direction * 11, 0, -10)
        move.fromValue = NSValue(caTransform3D: from); move.toValue = NSValue(caTransform3D: CATransform3DIdentity)
        move.duration = 0.18; move.timingFunction = CAMediaTimingFunction(controlPoints: 0.15, 0.78, 0.3, 1)
        content.add(move, forKey: "action.volume.depth")
        let wipe = CABasicAnimation(keyPath: "path")
        wipe.fromValue = CGPath(rect: CGRect(x: direction > 0 ? 10 : 0, y: 0, width: 390, height: 334), transform: nil)
        wipe.toValue = CGPath(rect: layer.bounds, transform: nil)
        wipe.duration = 0.18; wipe.timingFunction = move.timingFunction
        revealMask.add(wipe, forKey: "action.volume.reveal")
    }

    private func mainActions() -> [VolumeCanvasAction] {
        var actions = [
            VolumeCanvasAction(id: "audio:output", label: L10n.text("Choose output device", "选择输出设备"), rect: CGRect(x: 82, y: 40, width: 306, height: 28), enabled: snapshot.canSetDefaultOutput && !snapshot.outputs.isEmpty),
            VolumeCanvasAction(id: "audio:input", label: L10n.text("Choose input device", "选择输入设备"), rect: CGRect(x: 82, y: 75, width: 306, height: 28), enabled: snapshot.canSetDefaultInput && !snapshot.inputs.isEmpty),
            VolumeCanvasAction(id: "audio:mute", label: snapshot.outputMuted == true ? L10n.text("Unmute", "取消静音") : L10n.text("Mute", "静音"), rect: CGRect(x: 328, y: 113, width: 60, height: 27), enabled: snapshot.canSetOutputMute && snapshot.outputMuted != nil),
            VolumeCanvasAction(id: "audio:headphones", label: L10n.text("Headphones / Bluetooth", "耳机 / 蓝牙"), rect: CGRect(x: 12, y: 205, width: 181, height: 26)),
            VolumeCanvasAction(id: "audio:applications", label: L10n.text("App volume", "应用音量"), rect: CGRect(x: 202, y: 205, width: 186, height: 26))]
        actions += pageActions()
        return actions
    }

    private func chooserActions() -> [VolumeCanvasAction] {
        var actions = [VolumeCanvasAction(id: "audio:back", label: L10n.text("Back", "返回"), rect: CGRect(x: 329, y: 0, width: 59, height: 24))]
        let start = min(chooserDevices.count, pageIndex * 6)
        for (offset, device) in chooserDevices[start..<min(chooserDevices.count, start + 6)].enumerated() {
            let output = chooser == .output
            let selected = output ? device.id == snapshot.defaultOutputID : device.id == snapshot.defaultInputID
            actions.append(VolumeCanvasAction(id: "audio:\(output ? "output" : "input"):\(device.id)",
                label: device.name + (selected ? L10n.text(", Selected", "，已选择") : ""),
                rect: CGRect(x: 12, y: 43 + CGFloat(offset) * 41, width: 376, height: 36),
                enabled: output ? device.canBeDefaultOutput && snapshot.canSetDefaultOutput : device.canBeDefaultInput && snapshot.canSetDefaultInput))
        }
        actions += pageActions()
        return actions
    }

    private func pageActions() -> [VolumeCanvasAction] {
        var actions: [VolumeCanvasAction] = []
        if pageIndex > 0 { actions.append(VolumeCanvasAction(id: "audio:previous", label: L10n.text("Previous page", "上一页"), rect: CGRect(x: 265, y: 301, width: 29, height: 25))) }
        if pageIndex + 1 < pageCount { actions.append(VolumeCanvasAction(id: "audio:next", label: L10n.text("Next page", "下一页"), rect: CGRect(x: 359, y: 301, width: 29, height: 25))) }
        return actions
    }

    private func repaint() {
        drawingAppRows = false
        content.sublayers?.forEach { $0.removeFromSuperlayer() }
        appRowsLayer.sublayers?.forEach { $0.removeFromSuperlayer() }
        let title = chooser == .output ? L10n.text("Output device", "输出设备") : (chooser == .input ? L10n.text("Input device", "输入设备") : HUDModule.volume.title)
        text(HUDSectionHeading.text(title), rect: CGRect(x: 12, y: 0, width: 310, height: 20), size: 15, color: primary, weight: .semibold)
        text(controller.statusMessage ?? (chooser == nil ? L10n.text("Device and app volume", "设备与应用音量") : L10n.text("Choose a connected device", "选择已连接的设备")),
             rect: CGRect(x: 12, y: 23, width: 376, height: 13), size: 9.5, color: muted)
        if chooser != nil { renderChooser() }
        else { renderMain() }
        if pageCount > 1 {
            text("\(pageIndex + 1) / \(pageCount)", rect: CGRect(x: 299, y: 307, width: 54, height: 15), size: 10, color: muted, alignment: .center)
        }
        for command in pageActions() { button(command, title: command.id == "audio:previous" ? "‹" : "›") }
    }

    private func renderMain() {
        text(L10n.text("Output", "输出"), rect: CGRect(x: 12, y: 47, width: 67, height: 17), size: 11.5, color: primary)
        text(L10n.text("Input", "输入"), rect: CGRect(x: 12, y: 82, width: 67, height: 17), size: 11.5, color: primary)
        for command in mainActions() where ["audio:output", "audio:input"].contains(command.id) {
            let device = command.id == "audio:output" ? snapshot.outputs.first { $0.id == snapshot.defaultOutputID } : snapshot.inputs.first { $0.id == snapshot.defaultInputID }
            button(command, title: (device?.name ?? L10n.text("No device", "无设备")) + "  ›", alignment: .left)
        }
        text(L10n.text("Volume", "音量"), rect: CGRect(x: 12, y: 116, width: 65, height: 16), size: 11.5, color: primary)
        text(snapshot.outputVolume.map { "\(Int(($0 * 100).rounded()))%" } ?? L10n.text("Unavailable", "不可用"),
             rect: CGRect(x: 12, y: 133, width: 67, height: 14), size: 9.5, color: muted)
        if let mute = mainActions().first(where: { $0.id == "audio:mute" }) {
            button(mute, title: mute.label, highlighted: snapshot.outputMuted == true)
        }
        text(L10n.text("Balance", "平衡"), rect: CGRect(x: 12, y: 161, width: 65, height: 16), size: 11.5, color: primary)
        text("L", rect: CGRect(x: 83, y: 163, width: 15, height: 15), size: 10, color: muted, alignment: .center)
        text("R", rect: CGRect(x: 351, y: 163, width: 16, height: 15), size: 10, color: muted, alignment: .center)
        for slider in accessibleSliders where !slider.id.hasPrefix("app:") { renderSlider(slider) }
        let balanceDescription: String
        if !snapshot.canSetBalance { balanceDescription = L10n.text("Balance is unavailable for this device", "此设备不支持声道平衡") }
        else if let balance = snapshot.balance {
            balanceDescription = abs(balance) < 0.01 ? L10n.text("Centered", "居中") : "\(balance < 0 ? "L" : "R") \(Int((abs(balance) * 100).rounded()))%"
        } else { balanceDescription = L10n.text("Balance unavailable", "声道平衡不可用") }
        text(balanceDescription, rect: CGRect(x: 82, y: 185, width: 306, height: 12), size: 9, color: muted, alignment: .center)
        if !snapshot.canSetOutputVolume {
            text(L10n.text("Use this device's controls", "请使用设备自身的音量控制"), rect: CGRect(x: 82, y: 144, width: 306, height: 12), size: 8.5, color: muted)
        }
        for command in mainActions() where ["audio:headphones", "audio:applications"].contains(command.id) {
            button(command, title: command.label, highlighted: (command.id == "audio:headphones") == (details == .headphones))
        }
        renderDetails()
    }

    private func renderSlider(_ slider: VolumeCanvasSlider) {
        HUDControlHighlightLayer.add(to: drawingLayer, rect: slider.visibleRect ?? slider.rect, enabled: slider.enabled)
        let line = CGRect(x: slider.rect.minX + 6, y: slider.rect.midY - 2, width: slider.rect.width - 12, height: 4)
        let base = CAShapeLayer(); base.path = CGPath(roundedRect: line, cornerWidth: 2, cornerHeight: 2, transform: nil)
        base.fillColor = NSColor(white: dark ? 0.60 : 0.30, alpha: slider.enabled ? 0.5 : 0.24).cgColor; drawingLayer.addSublayer(base)
        guard let value = slider.value, value.isFinite else { return }
        let fraction = CGFloat(min(1, max(0, (value - slider.minimum) / (slider.maximum - slider.minimum))))
        let color = slider.enabled ? yellow : muted.withAlphaComponent(0.65)
        if slider.id != "balance" {
            let fill = CAShapeLayer(); fill.path = CGPath(roundedRect: CGRect(x: line.minX, y: line.minY, width: line.width * fraction, height: line.height), cornerWidth: 2, cornerHeight: 2, transform: nil); fill.fillColor = color.cgColor; drawingLayer.addSublayer(fill)
        } else {
            let tick = CAShapeLayer(); tick.path = CGPath(rect: CGRect(x: line.midX - 0.5, y: slider.rect.midY - 5, width: 1, height: 10), transform: nil); tick.fillColor = muted.cgColor; drawingLayer.addSublayer(tick)
        }
        let handle = CAShapeLayer(); handle.path = cutCorner(CGRect(x: line.minX + line.width * fraction - 5, y: slider.rect.midY - 8, width: 10, height: 16), corner: 2); handle.fillColor = color.cgColor; handle.strokeColor = primary.withAlphaComponent(slider.enabled ? 0.8 : 0.3).cgColor; handle.lineWidth = 0.6; drawingLayer.addSublayer(handle)
    }

    private func renderDetails() {
        if details == .headphones {
            let devices = connectedDevices
            if devices.isEmpty {
                text(L10n.text("No headphones or Bluetooth audio devices", "未连接耳机或蓝牙音频设备"), rect: CGRect(x: 18, y: 250, width: 364, height: 35), size: 11, color: muted, alignment: .center, wrapped: true)
            } else {
                let start = min(devices.count, pageIndex * 2)
                for (offset, device) in devices[start..<min(devices.count, start + 2)].enumerated() {
                    let detail = (device.isBluetooth ? "Bluetooth" : L10n.text("Headphones", "耳机")) + (device.id == snapshot.defaultOutputID ? L10n.text(" · Output", " · 输出") : "")
                    detailRow(title: device.name, subtitle: detail, offset: offset)
                }
            }
        } else {
            if !snapshot.applicationActivitySupported {
                text(snapshot.applicationActivityMessage ?? L10n.text("Audio app detection requires macOS 14.2 or later", "音频应用检测需要 macOS 14.2 或更高版本"), rect: CGRect(x: 18, y: 245, width: 364, height: 48), size: 10.5, color: muted, alignment: .center, wrapped: true)
            } else if listedApplications.isEmpty {
                text(snapshot.applicationActivityMessage ?? L10n.text("No adjustable audio apps", "暂无可调节音量的应用"), rect: CGRect(x: 18, y: 251, width: 364, height: 35), size: 11, color: muted, alignment: .center, wrapped: true)
            } else {
                appRowsLayer.frame = layer.bounds
                let clip = CAShapeLayer(); clip.path = CGPath(rect: applicationViewport, transform: nil)
                clip.fillColor = NSColor.black.cgColor; appRowsLayer.mask = clip
                content.addSublayer(appRowsLayer)
                drawingAppRows = true
                for slider in accessibleSliders where slider.id.hasPrefix("app:") { renderSlider(slider) }
                for row in visibleApplicationRows {
                    let app = row.app, y = row.y + 1
                    let session = appSession(app.id)
                    let state: String
                    switch session?.state {
                    case .active?: state = "PID \(app.pid)"
                    case .preparing?: state = L10n.text("Starting…", "正在启动…")
                    case .stopping?: state = L10n.text("Stopping…", "正在停止…")
                    case .failed?: state = L10n.text("100% to restore", "回到 100% 恢复")
                    case nil:
                        let enabled = accessibleSliders.first { $0.id == "app:\(app.id)" }?.enabled == true
                        state = enabled ? "PID \(app.pid)" : L10n.text("Unsupported route", "暂不支持此路由")
                    }
                    let hasIcon = renderAppIcon(app, at: CGPoint(x: 18, y: y + 3))
                    let labelX: CGFloat = hasIcon ? 44 : 18
                    text(app.name, rect: CGRect(x: labelX, y: y, width: 147 - labelX, height: 15), size: 10.5, color: primary, weight: .medium)
                    text(state, rect: CGRect(x: labelX, y: y + 15, width: 147 - labelX, height: 12), size: 8, color: muted)
                    if let slider = accessibleSliders.first(where: { $0.id == "app:\(app.id)" }) {
                        text(slider.value.map { "\(Int(($0 * 100).rounded()))%" } ?? "—",
                             rect: CGRect(x: 339, y: y + 6, width: 46, height: 16), size: 10, color: primary, alignment: .right)
                    }
                }
                drawingAppRows = false
                if maximumApplicationScroll > 0 {
                    let track = CALayer(); track.frame = CGRect(x: 392, y: 234, width: 2, height: 64)
                    track.backgroundColor = muted.withAlphaComponent(0.2).cgColor; content.addSublayer(track)
                    let height = max(12, 64 * 64 / (maximumApplicationScroll + 64))
                    let thumb = CALayer()
                    thumb.frame = CGRect(x: 392, y: 234 + (64 - height) * applicationScrollOffset / maximumApplicationScroll, width: 2, height: height)
                    thumb.backgroundColor = muted.cgColor; content.addSublayer(thumb)
                }
            }
            text(perAppAudio.statusMessage ?? "",
                 rect: CGRect(x: 12, y: 300, width: pageCount > 1 ? 245 : 376, height: 31), size: 8.5, color: muted, wrapped: true)
        }
    }

    private func renderAppIcon(_ app: AudioApplicationInfo, at origin: CGPoint) -> Bool {
        guard let source = app.icon else { return false }
        let key = ObjectIdentifier(source)
        let image: CGImage
        if let cached = iconCache[key], cached.scale == scale {
            image = cached.image
        } else {
            var proposed = CGRect(x: 0, y: 0, width: 20 * scale, height: 20 * scale)
            guard let rendered = source.cgImage(forProposedRect: &proposed, context: nil, hints: nil) else { return false }
            if iconCache.count >= 64 { iconCache.removeAll(keepingCapacity: true) }
            iconCache[key] = (source, scale, rendered); image = rendered
        }
        let icon = CALayer(); icon.name = "volume.app.icon.\(app.id)"
        icon.frame = CGRect(origin: origin, size: CGSize(width: 20, height: 20))
        icon.contents = image; icon.contentsGravity = .resizeAspect; icon.contentsScale = scale
        drawingLayer.addSublayer(icon)
        return true
    }

    private func detailRow(title: String, subtitle: String, offset: Int) {
        let y = 237 + CGFloat(offset) * 30
        text(title, rect: CGRect(x: 18, y: y, width: 243, height: 17), size: 11, color: primary, weight: .medium)
        text(subtitle, rect: CGRect(x: 269, y: y + 1, width: 112, height: 16), size: 9, color: muted, alignment: .right)
        let line = CALayer(); line.frame = CGRect(x: 16, y: y + 24, width: 368, height: 0.5); line.backgroundColor = muted.withAlphaComponent(0.25).cgColor; drawingLayer.addSublayer(line)
    }

    private func renderChooser() {
        if chooserDevices.isEmpty { text(L10n.text("No connected devices", "无已连接设备"), rect: CGRect(x: 20, y: 148, width: 360, height: 35), size: 13, color: muted, alignment: .center) }
        for command in chooserActions() where !["audio:previous", "audio:next"].contains(command.id) {
            if command.id == "audio:back" { button(command, title: "‹ " + command.label); continue }
            let parts = command.id.split(separator: ":")
            let id = parts.last.flatMap { UInt32($0) }
            guard let device = chooserDevices.first(where: { $0.id == id }) else { continue }
            let selected = chooser == .output ? device.id == snapshot.defaultOutputID : device.id == snapshot.defaultInputID
            button(command, title: device.name + (selected ? "  ✓" : ""), highlighted: selected, alignment: .left)
        }
    }

    private func button(_ command: VolumeCanvasAction, title: String, highlighted: Bool = false, alignment: CATextLayerAlignmentMode = .center) {
        let plate = CAShapeLayer(); plate.path = cutCorner(command.rect, corner: 4)
        plate.fillColor = (highlighted && command.enabled ? yellow : NSColor(white: dark ? 0.80 : 0.91, alpha: command.enabled ? 1 : 0.55)).cgColor
        plate.strokeColor = NSColor(white: dark ? 0.95 : 0.33, alpha: command.enabled ? 0.65 : 0.25).cgColor; plate.lineWidth = 0.6; drawingLayer.addSublayer(plate)
        HUDControlHighlightLayer.add(to: drawingLayer, rect: command.rect, shape: .cutCorner,
                                     enabled: command.enabled, framed: true)
        text(title, rect: CGRect(x: command.rect.minX + 9, y: command.rect.midY - 7, width: command.rect.width - 18, height: 17), size: 10.5, color: ink.withAlphaComponent(command.enabled ? 1 : 0.65), weight: .medium, alignment: alignment)
    }

    private func cutCorner(_ r: CGRect, corner: CGFloat) -> CGPath {
        let p = CGMutablePath(); p.move(to: CGPoint(x: r.minX + corner, y: r.minY)); p.addLine(to: CGPoint(x: r.maxX, y: r.minY)); p.addLine(to: CGPoint(x: r.maxX, y: r.maxY - corner)); p.addLine(to: CGPoint(x: r.maxX - corner, y: r.maxY)); p.addLine(to: CGPoint(x: r.minX, y: r.maxY)); p.addLine(to: CGPoint(x: r.minX, y: r.minY + corner)); p.closeSubpath(); return p
    }
    private func text(_ value: String, rect: CGRect, size: CGFloat, color: NSColor, weight: NSFont.Weight = .regular, alignment: CATextLayerAlignmentMode = .left, wrapped: Bool = false) {
        let label = CATextLayer(); label.frame = rect; label.string = value; label.font = NSFont.systemFont(ofSize: size, weight: weight); label.fontSize = size
        label.foregroundColor = color.cgColor; label.alignmentMode = alignment; label.isWrapped = wrapped; label.truncationMode = wrapped ? .none : .end; label.contentsScale = HUDRenderScale.contentScale(for: label, baseScale: scale); drawingLayer.addSublayer(label)
    }
    private func withoutActions(_ body: () -> Void) { CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit() }
}
