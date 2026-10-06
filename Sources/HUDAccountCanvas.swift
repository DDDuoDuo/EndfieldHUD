import AppKit
import QuartzCore

/// UI values only. Account credentials, persistence and refresh scheduling stay
/// in the account controller; repainting this surface never requests a sync.
struct HUDAccountPresentation: Equatable {
    enum Region: String, CaseIterable {
        case china, global
        var title: String { self == .china ? L10n.text("China", "国服") : L10n.text("Global", "国际服") }
    }
    enum HeaderMode: String, CaseIterable {
        case workMode, endfield, arknights, hidden
        var title: String {
            switch self {
            case .workMode: return L10n.text("Work Mode", "工作模式")
            case .endfield: return L10n.text("Arknights: Endfield", "明日方舟：终末地")
            case .arknights: return L10n.text("Arknights", "明日方舟")
            case .hidden: return L10n.text("Hidden", "隐藏")
            }
        }
    }
    enum Status: Equatable { case disconnected, connecting, connected, refreshing, failed }
    struct Role: Equatable {
        let id: String
        let title: String
        var subtitle = ""
    }
    var region: Region = .china
    var status: Status = .disconnected
    var statusMessage = ""
    var accountName = ""
    var roles: [Role] = []
    var selectedRoleID: String?
    var headerMode: HeaderMode = .workMode
    var syncProfile = false
    var syncAvatar = false
    var lastSync = ""
    var isLinked = false
    var busy: Bool { status == .connecting || status == .refreshing }
}

final class HUDAccountCanvas: NSObject, HUDModuleContentFactory {
    enum Action: Equatable {
        case connect(HUDAccountPresentation.Region), refresh, disconnect
        case selectRegion(HUDAccountPresentation.Region), selectRole(String)
        case selectHeaderMode(HUDAccountPresentation.HeaderMode)
        case setSyncProfile(Bool), setSyncAvatar(Bool)
    }
    struct Control {
        let id: String, label: String
        let rect: CGRect
        var enabled = true
    }
    private enum Menu: Equatable { case region, role, header, disconnect }
    private struct Choice { let id: String, title: String; let selected: Bool }
    let layer = CALayer()
    private let face = CALayer(), menuLayer = CALayer(), menuClip = CALayer(), menuRows = CALayer()
    private var mainControls: [Control] = [], menuControls: [Control] = []
    private var menu: Menu?, choices: [Choice] = [], selectedChoice = 0
    private var menuRect = CGRect.zero, listRect = CGRect.zero
    private var menuOffset: CGFloat = 0
    private var active = false, needsRender = true
    private var dark = true, scale: CGFloat = 2
    private var accent = HUDRuntimeAppearance.accent
    private var retiringMenu: CALayer?, retireWork: DispatchWorkItem?
    private(set) var presentation = HUDAccountPresentation()
    private(set) var renderCount = 0
    var onChange: (() -> Void)?
    var onAction: ((Action) -> Void)?
    var isPopoverOpen: Bool { menu != nil }
    var popoverBounds: CGRect? { menu == nil ? nil : menuRect }
    var accessibleActions: [Control] { menu == nil ? mainControls : menuControls }
    var menuScrollOffset: CGFloat { menuOffset }

    override init() {
        super.init()
        layer.name = "account"; layer.bounds = CGRect(x: 0, y: 0, width: 400, height: 334)
        face.frame = layer.bounds; layer.addSublayer(face)
        menuLayer.frame = layer.bounds; menuLayer.name = "account.secondaryMenu"; menuLayer.zPosition = 3_000_000
        layer.addSublayer(menuLayer)
    }
    deinit { retireWork?.cancel() }
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; scale = style.contentsScale; accent = style.accent
        needsRender = true; render(); return layer
    }
    func update(_ value: HUDAccountPresentation) {
        guard presentation != value else { return }
        let old = presentation; presentation = value; needsRender = true
        if old.region != value.region || old.roles != value.roles || value.busy || !value.isLinked && menu == .disconnect {
            dismissPopover(animated: false)
        }
        if active { render() }
    }
    func updateRenderScale(_ value: CGFloat) {
        guard value.isFinite, scale != min(3, max(1, value)) else { return }
        scale = min(3, max(1, value)); needsRender = true; if active { render() }
    }
    func setVisible(_ value: Bool) {
        active = value
        if value { if needsRender { render() } }
        else { dismissPopover(animated: false); clearRetiringMenu() }
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        if menu != nil {
            if let control = menuControls.last(where: { $0.enabled && $0.rect.contains(point) }) { perform(control.id) }
            else if !menuRect.contains(point) { dismissPopover() }
            return true
        }
        guard layer.bounds.contains(point) else { return false }
        if let control = mainControls.last(where: { $0.enabled && $0.rect.contains(point) }) { perform(control.id) }
        return true
    }
    func perform(_ id: String) {
        guard accessibleActions.contains(where: { $0.id == id && $0.enabled }) else { return }
        switch id {
        case "connect": onAction?(.connect(presentation.region))
        case "refresh": onAction?(.refresh)
        case "disconnect": show(.disconnect)
        case "region": show(.region)
        case "role": show(.role)
        case "header": show(.header)
        case "syncProfile": onAction?(.setSyncProfile(!presentation.syncProfile))
        case "syncAvatar": onAction?(.setSyncAvatar(!presentation.syncAvatar))
        case "menu:cancel": dismissPopover()
        case "menu:disconnect": dismissPopover(); onAction?(.disconnect)
        default:
            if id.hasPrefix("region:"), let value = HUDAccountPresentation.Region(rawValue: String(id.dropFirst(7))) {
                dismissPopover(); if value != presentation.region { onAction?(.selectRegion(value)) }
            } else if id.hasPrefix("header:"), let value = HUDAccountPresentation.HeaderMode(rawValue: String(id.dropFirst(7))) {
                dismissPopover(); if value != presentation.headerMode { onAction?(.selectHeaderMode(value)) }
            } else if id.hasPrefix("role:") {
                let value = String(id.dropFirst(5)); dismissPopover()
                if value != presentation.selectedRoleID { onAction?(.selectRole(value)) }
            }
        }
    }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard menu != nil else { return false }
        guard menuRect.contains(point), menu != .disconnect, delta.isFinite else { return true }
        let next = min(max(0, CGFloat(choices.count) * 31 - listRect.height), max(0, menuOffset + min(250, max(-250, delta))))
        guard next != menuOffset else { return true }
        let old = (menuRows.presentation() ?? menuRows).position
        menuOffset = next; layoutMenuRows()
        if !HUDRuntimeAppearance.reduceMotion {
            let move = CABasicAnimation(keyPath: "position"); move.fromValue = old; move.toValue = menuRows.position
            move.duration = 0.08; move.timingFunction = CAMediaTimingFunction(name: .easeOut); menuRows.add(move, forKey: "account.menu.scroll")
        }
        onChange?(); return true
    }
    func moveMenuSelection(_ direction: Int) {
        guard menu != nil, !choices.isEmpty else { return }
        selectedChoice = min(choices.count - 1, max(0, selectedChoice + direction))
        let top = CGFloat(selectedChoice) * 31
        let desired = min(top, max(menuOffset, top + 28 - listRect.height))
        _ = scroll(at: CGPoint(x: listRect.midX, y: listRect.midY), delta: desired - menuOffset)
    }
    func activateMenuSelection() {
        guard menu != nil else { return }
        if menu == .disconnect { perform("menu:cancel") }
        else if choices.indices.contains(selectedChoice) { perform(choices[selectedChoice].id) }
    }
    func dismissPopover(animated: Bool = true) {
        guard menu != nil else { return }
        clearRetiringMenu()
        if animated && active && !HUDRuntimeAppearance.reduceMotion {
            let old = CALayer(); old.frame = layer.bounds
            // Move existing tiny layers, avoiding a bitmap of the HUD.
            for piece in menuLayer.sublayers ?? [] { old.addSublayer(piece) }
            layer.addSublayer(old); old.zPosition = menuLayer.zPosition; old.opacity = 0; retiringMenu = old
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 1; fade.toValue = 0
            let move = CABasicAnimation(keyPath: "transform.translation.y"); move.fromValue = 0; move.toValue = -5
            let group = CAAnimationGroup(); group.animations = [fade, move]; group.duration = 0.14
            group.timingFunction = CAMediaTimingFunction(name: .easeOut); old.add(group, forKey: "account.menu.close")
            let work = DispatchWorkItem { [weak self, weak old] in
                guard let self, self.retiringMenu === old else { return }; self.clearRetiringMenu()
            }
            retireWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.15, execute: work)
        } else { menuLayer.sublayers?.forEach { $0.removeFromSuperlayer() } }
        menu = nil; choices = []; menuControls = []; menuOffset = 0; onChange?()
    }
    private func clearRetiringMenu() { retireWork?.cancel(); retireWork = nil; retiringMenu?.removeFromSuperlayer(); retiringMenu = nil }
    private func show(_ value: Menu) {
        dismissPopover(animated: false); clearRetiringMenu(); menu = value; menuOffset = 0
        switch value {
        case .region:
            choices = HUDAccountPresentation.Region.allCases.map { Choice(id: "region:" + $0.rawValue, title: $0.title, selected: $0 == presentation.region) }
            menuRect = CGRect(x: 174, y: 112, width: 214, height: 76)
        case .header:
            choices = HUDAccountPresentation.HeaderMode.allCases.map { Choice(id: "header:" + $0.rawValue, title: $0.title, selected: $0 == presentation.headerMode) }
            menuRect = CGRect(x: 148, y: 190, width: 240, height: 138)
        case .role:
            choices = presentation.roles.map { Choice(id: "role:" + $0.id, title: $0.subtitle.isEmpty ? $0.title : $0.title + " · " + $0.subtitle, selected: $0.id == presentation.selectedRoleID) }
            menuRect = CGRect(x: 112, y: 151, width: 276, height: min(174, CGFloat(choices.count) * 31 + 14))
        case .disconnect:
            choices = []; menuRect = CGRect(x: 144, y: 80, width: 244, height: 88)
        }
        selectedChoice = choices.firstIndex(where: \.selected) ?? 0
        paintMenu()
        if !HUDRuntimeAppearance.reduceMotion {
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 0; fade.toValue = 1
            let move = CABasicAnimation(keyPath: "transform.translation.y"); move.fromValue = -5; move.toValue = 0
            let group = CAAnimationGroup(); group.animations = [fade, move]; group.duration = 0.16
            group.timingFunction = CAMediaTimingFunction(name: .easeOut); menuLayer.add(group, forKey: "account.menu.open")
        }
        onChange?()
    }
    private var ink: NSColor { NSColor(white: dark ? 0.95 : 0.12, alpha: 1) }
    private func render() {
        needsRender = false; renderCount += 1
        CATransaction.begin(); CATransaction.setDisableActions(true)
        face.sublayers?.forEach { $0.removeFromSuperlayer() }; mainControls = []
        text("// " + L10n.text("Account", "账户绑定"), CGRect(x: 12, y: 8, width: 244, height: 24), size: 17, parent: face)
        addButton("connect", presentation.busy ? L10n.text("Connecting…", "正在连接…") : presentation.isLinked ? L10n.text("Reconnect", "重新连接") : L10n.text("Connect", "绑定账户"), CGRect(x: 276, y: 7, width: 112, height: 28), enabled: !presentation.busy, strong: true)
        let rule = CALayer(); rule.frame = CGRect(x: 12, y: 40, width: 376, height: 1); rule.backgroundColor = ink.withAlphaComponent(0.2).cgColor; face.addSublayer(rule)
        let status: String
        switch presentation.status {
        case .disconnected: status = L10n.text("Not connected", "未绑定")
        case .connecting: status = L10n.text("Connecting…", "正在连接…")
        case .connected: status = presentation.accountName.isEmpty ? L10n.text("Connected", "已绑定") : presentation.accountName
        case .refreshing: status = L10n.text("Syncing…", "正在同步…")
        case .failed: status = L10n.text("Connection unavailable", "连接不可用")
        }
        text(status, CGRect(x: 12, y: 53, width: 295, height: 18), size: 12, parent: face)
        addButton("refresh", "", CGRect(x: 321, y: 47, width: 28, height: 28), enabled: presentation.isLinked && !presentation.busy)
        drawRefresh(in: CGRect(x: 326, y: 52, width: 18, height: 18), enabled: presentation.isLinked && !presentation.busy)
        addButton("disconnect", "×", CGRect(x: 360, y: 47, width: 28, height: 28), enabled: presentation.isLinked && !presentation.busy, accessibility: L10n.text("Disconnect", "解除绑定"))
        row(L10n.text("Region", "地区"), y: 83)
        addButton("region", presentation.region.title + "  ▾", CGRect(x: 174, y: 81, width: 214, height: 28), enabled: !presentation.busy)
        row(L10n.text("Game account", "游戏账户"), y: 122)
        let role = presentation.roles.first { $0.id == presentation.selectedRoleID }
        addButton("role", (role?.title ?? L10n.text("Choose account", "选择账户")) + "  ▾", CGRect(x: 174, y: 120, width: 214, height: 28), enabled: !presentation.roles.isEmpty && !presentation.busy)
        row(L10n.text("Header display", "顶部显示"), y: 161)
        addButton("header", presentation.headerMode.title + "  ▾", CGRect(x: 174, y: 159, width: 214, height: 28))
        toggle("syncProfile", L10n.text("Sync personal profile", "同步个人名片"), y: 203, selected: presentation.syncProfile)
        toggle("syncAvatar", L10n.text("Sync game avatar", "同步游戏头像"), y: 244, selected: presentation.syncAvatar)
        if !presentation.lastSync.isEmpty { text(presentation.lastSync, CGRect(x: 12, y: 287, width: 376, height: 15), size: 9, color: ink.withAlphaComponent(0.55), parent: face) }
        if !presentation.statusMessage.isEmpty { text(presentation.statusMessage, CGRect(x: 12, y: 309, width: 376, height: 17), size: 10, color: presentation.status == .failed ? .systemOrange : ink.withAlphaComponent(0.6), parent: face) }
        if menu != nil { paintMenu() }
        CATransaction.commit(); onChange?()
    }
    private func row(_ label: String, y: CGFloat) { text(label, CGRect(x: 12, y: y + 5, width: 150, height: 17), size: 11, parent: face) }
    private func toggle(_ id: String, _ label: String, y: CGFloat, selected: Bool) {
        row(label, y: y)
        addButton(id, selected ? "✓" : "", CGRect(x: 360, y: y, width: 28, height: 28), enabled: !presentation.busy, strong: selected, accessibility: label)
    }
    private func addButton(_ id: String, _ label: String, _ rect: CGRect, enabled: Bool = true, strong: Bool = false, accessibility: String? = nil) {
        mainControls.append(Control(id: id, label: accessibility ?? (id == "refresh" ? L10n.text("Refresh", "刷新") : label), rect: rect, enabled: enabled))
        button(label, rect, parent: face, enabled: enabled, strong: strong)
    }
    private func button(_ label: String, _ rect: CGRect, parent: CALayer, enabled: Bool = true, strong: Bool = false, selected: Bool = false, centered: Bool = true) {
        let plate = CALayer(); plate.frame = rect
        plate.backgroundColor = (strong ? accent : selected ? accent.withAlphaComponent(0.22) : ink.withAlphaComponent(0.07)).cgColor
        plate.opacity = enabled ? 1 : 0.4; parent.addSublayer(plate)
        HUDControlHighlightLayer.add(to: parent, rect: rect, shape: .cutCorner, enabled: enabled, framed: true)
        text(label, CGRect(x: rect.minX + 7, y: rect.midY - 7, width: rect.width - 14, height: 16), size: 10,
             color: (strong ? NSColor(white: 0.12, alpha: 1) : ink).withAlphaComponent(enabled ? 1 : 0.4), centered: centered, parent: parent)
    }
    private func paintMenu() {
        guard let menu else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        menuLayer.sublayers?.forEach { $0.removeFromSuperlayer() }; menuControls = []
        let shadow = CALayer(); shadow.frame = menuRect.offsetBy(dx: -3, dy: 4); shadow.backgroundColor = NSColor.black.withAlphaComponent(0.30).cgColor; menuLayer.addSublayer(shadow)
        let plate = CALayer(); plate.frame = menuRect; plate.backgroundColor = NSColor(white: dark ? 0.08 : 0.92, alpha: 0.98).cgColor
        plate.borderWidth = 0.7; plate.borderColor = accent.withAlphaComponent(0.7).cgColor; menuLayer.addSublayer(plate)
        if menu == .disconnect {
            text(L10n.text("Disconnect this account?", "解除此账户绑定？"), CGRect(x: menuRect.minX + 12, y: menuRect.minY + 13, width: 220, height: 20), size: 12, parent: menuLayer)
            for (id, glyph, title, x) in [("menu:cancel", "×", L10n.text("Cancel", "取消"), menuRect.maxX - 80), ("menu:disconnect", "✓", L10n.text("Disconnect", "解除绑定"), menuRect.maxX - 40)] {
                let rect = CGRect(x: x, y: menuRect.maxY - 40, width: 28, height: 28)
                menuControls.append(Control(id: id, label: title, rect: rect)); button(glyph, rect, parent: menuLayer, strong: id == "menu:disconnect")
            }
        } else {
            listRect = menuRect.insetBy(dx: 8, dy: 7)
            menuClip.frame = listRect; menuClip.masksToBounds = true; menuLayer.addSublayer(menuClip)
            menuRows.sublayers?.forEach { $0.removeFromSuperlayer() }; menuRows.anchorPoint = .zero
            menuRows.bounds = CGRect(x: 0, y: 0, width: listRect.width, height: CGFloat(choices.count) * 31)
            menuClip.addSublayer(menuRows)
            for (index, choice) in choices.enumerated() { button(choice.title, CGRect(x: 2, y: CGFloat(index) * 31 + 2, width: listRect.width - 6, height: 26), parent: menuRows, selected: choice.selected, centered: false) }
            layoutMenuRows()
        }
        CATransaction.commit()
    }
    private func layoutMenuRows() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        menuRows.position = CGPoint(x: 0, y: -menuOffset)
        menuControls = choices.enumerated().compactMap { index, choice in
            let rect = CGRect(x: listRect.minX + 2, y: listRect.minY + CGFloat(index) * 31 + 2 - menuOffset, width: listRect.width - 6, height: 26).intersection(listRect)
            return rect.isEmpty || rect.isNull ? nil : Control(id: choice.id, label: choice.title, rect: rect)
        }
        CATransaction.commit()
    }
    private func drawRefresh(in rect: CGRect, enabled: Bool) {
        let shape = CAShapeLayer(); shape.frame = rect; shape.fillColor = ink.withAlphaComponent(enabled ? 1 : 0.35).cgColor
        let shaft = CGMutablePath(); shaft.addArc(center: CGPoint(x: 9, y: 9), radius: 6, startAngle: .pi / 3, endAngle: .pi * 65 / 36, clockwise: false)
        let path = CGMutablePath(); path.addPath(shaft.copy(strokingWithWidth: 1.6, lineCap: .round, lineJoin: .round, miterLimit: 1))
        path.move(to: CGPoint(x: 15.9, y: 8.2)); path.addLine(to: CGPoint(x: 11.4, y: 8.2)); path.addLine(to: CGPoint(x: 15.9, y: 3.7)); path.closeSubpath()
        shape.path = path; face.addSublayer(shape)
    }
    private func text(_ value: String, _ rect: CGRect, size: CGFloat, color: NSColor? = nil, centered: Bool = false, parent: CALayer) {
        let label = CATextLayer(); label.frame = rect; label.string = value; label.contentsScale = scale
        label.font = NSFont.systemFont(ofSize: size, weight: .semibold); label.fontSize = size
        label.foregroundColor = (color ?? ink).cgColor; label.truncationMode = .end; label.alignmentMode = centered ? .center : .left; parent.addSublayer(label)
    }
}
