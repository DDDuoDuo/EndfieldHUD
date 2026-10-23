import AppKit
import QuartzCore

/// UI values only. Account credentials, persistence and refresh scheduling stay
/// in the account controller; repainting this surface never requests a sync.
struct HUDAccountPresentation: Equatable {
    enum Region: String, CaseIterable {
        case china, global
        var title: String { self == .china ? L10n.text("Mainland China", "国服") : L10n.text("Global", "国际服") }
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
    /// The personal-card fields an Endfield sync may write (spec §3.21 field chips).
    enum SyncField: String, CaseIterable {
        case name, tag, uid, awakening, levels, counts
        var title: String {
            switch self {
            case .name: return L10n.text("Game name", "游戏名称")
            case .tag: return L10n.text("# number", "# 编号")
            case .uid: return "UID"
            case .awakening: return L10n.text("Awakening date", "苏醒日期")
            case .levels: return L10n.text("Levels", "等级")
            case .counts: return L10n.text("Collection counts", "收集数量")
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
    var syncFields = Set(SyncField.allCases)
    var lastSync = ""
    var isLinked = false
    /// The stored credential expired: the linked view offers Reconnect.
    var needsReconnect = false
    /// The selected role's latest card; its sanity is projected each second.
    var snapshot: HypergryphProfileSnapshot?
    var busy: Bool { status == .connecting || status == .refreshing }
}

/// Account (spec §3.21): the Hypergryph / Gryphline link per region. Linked:
/// sanity and recovery plates, game role, header display, sync row and the
/// field chips; offline: the NOT LINKED panel with Connect. Every action goes
/// to the account controller (login window, Keychain, refresh) via `onAction`.
final class HUDAccountCanvas: HUDDesignCanvas {
    enum Action: Equatable {
        case connect(HUDAccountPresentation.Region), refresh, disconnect
        case selectRegion(HUDAccountPresentation.Region), selectRole(String)
        case selectHeaderMode(HUDAccountPresentation.HeaderMode)
        case setSyncProfile(Bool), setSyncAvatar(Bool)
        case setSyncField(HUDAccountPresentation.SyncField, Bool)
    }
    private(set) var presentation = HUDAccountPresentation()
    var onAction: ((Action) -> Void)?
    /// What the page's layout depends on; anything else updates in place.
    private struct Structure: Equatable {
        let linked: Bool, region: HUDAccountPresentation.Region, reconnect: Bool, synced: Bool, language: AppLanguage
    }
    private var renderedStructure: Structure?
    private var binders: [() -> Void] = []
    private var sanity: SanityReadout?
    private var clock: Timer?
    private var awaitingRefresh = false, awaitingUnlink = false
    /// Sanity is projected locally between network samples (tests inject time).
    var now: () -> Date = Date.init

    init(context: HUDDesignContext) { super.init(module: .account, context: context) }
    deinit { clock?.invalidate() }

    // MARK: Presentation

    func update(_ value: HUDAccountPresentation) {
        guard value != presentation else { return }
        let old = presentation
        presentation = value
        guard renderedLanguage != nil else { return }
        if old.region != value.region || old.roles != value.roles || (value.busy && !old.busy) { closeMenu() }
        if old.region != value.region {
            // Each region keeps its own link: Global is forward (spec §3.21).
            awaitingUnlink = false; awaitingRefresh = false
            navigate(dx: value.region == .global ? 1 : -1)
        } else if old.isLinked != value.isLinked {
            navigate(dx: value.isLinked ? 1 : -1)
            if value.isLinked { toast(icon: .link2, "Account linked", "账户已绑定", detail: value.region.title) }
            else if awaitingUnlink { toast(icon: .x, "Disconnected", "已断开连接") }
            awaitingUnlink = false; awaitingRefresh = false
        } else if structure != renderedStructure { rerender() } else { binders.forEach { $0() } }
        if awaitingRefresh, old.status == .refreshing, value.status != .refreshing {
            awaitingRefresh = false
            if value.status == .connected {
                toast(icon: .refresh, "Sanity refreshed", "理智已刷新", detail: sanityPresentation.map { "\($0.current) / \($0.maximum)" })
            }
        }
        if isActive, value.statusMessage != old.statusMessage, !value.statusMessage.isEmpty {
            toast(icon: .warn, "Notice", "提示", detail: value.statusMessage)
        }
        updateClock()
    }

    private var structure: Structure {
        Structure(linked: presentation.isLinked, region: presentation.region, reconnect: presentation.needsReconnect,
                  synced: !presentation.lastSync.isEmpty, language: L10n.resolvedLanguage)
    }

    private var sanityPresentation: HypergryphSanityPresentation? { presentation.snapshot?.sanityPresentation(at: now()) }

    // MARK: Lifecycle

    override func didActivate() { updateClock(); sanity?.render(sanityPresentation, at: now()) }
    override func didDeactivate() { clock?.invalidate(); clock = nil; awaitingRefresh = false }
    override func willLeave() { didDeactivate() }

    /// The recovery countdowns are a live readout: one tick a second while
    /// the linked page is on screen, none otherwise.
    private func updateClock() {
        let wanted = isActive && presentation.isLinked && presentation.snapshot?.stamina != nil
        if !wanted { clock?.invalidate(); clock = nil; return }
        guard clock == nil else { return }
        let timer = Timer(timeInterval: 1, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.sanity?.render(self.sanityPresentation, at: self.now())
        }
        timer.tolerance = 0.1
        RunLoop.main.add(timer, forMode: .common)
        clock = timer
    }

    // MARK: Pages

    override func buildPage(_ page: HUDDesignPage) {
        binders = []; sanity = nil
        renderedStructure = structure
        let p = presentation
        var actions: [HUDControl] = []
        if p.isLinked {
            if p.needsReconnect {
                let reconnect = HUDButton(id: "reconnect", title: L10n.text("Reconnect", "重新连接"), icon: .link2, kind: .primary, size: .small, env: env)
                reconnect.place(at: .zero)
                reconnect.onActivate = { [weak self] in guard let self else { return }; self.onAction?(.connect(self.presentation.region)) }
                bind { [weak reconnect] p in reconnect?.isEnabled = !p.busy }
                actions.append(reconnect)
            }
            let disconnect = HUDButton(id: "disconnect", title: L10n.text("Disconnect", "解除绑定"), icon: .x, kind: .secondary, size: .small, env: env)
            disconnect.place(at: .zero)
            disconnect.onActivate = { [weak self] in self?.confirmDisconnect() }
            bind { [weak disconnect] p in disconnect?.isEnabled = !p.busy }
            actions.append(disconnect)
        }
        header.configure(crumbs: ["ACCOUNT"], code: "MOD-16", title: HUDModuleTitle("Account", "账户绑定"),
                         counter: p.isLinked ? "LINKED" : "OFFLINE", showsBack: false, actions: actions)
        let regions = HUDAccountPresentation.Region.allCases
        let segmented = HUDSegmentedControl(id: "region", label: L10n.text("Region", "地区"), options: regions.map(\.title),
                                            selectedIndex: regions.firstIndex(of: p.region) ?? 0, width: HUDMetric.box.width, env: env)
        segmented.onChange = { [weak self] index in
            guard let self, regions.indices.contains(index), regions[index] != self.presentation.region else { return }
            self.onAction?(.selectRegion(regions[index]))
        }
        bind { [weak segmented] p in
            segmented?.select(regions.firstIndex(of: p.region) ?? 0, notify: false)
            segmented?.segments.forEach { $0.isEnabled = !p.busy }
        }
        add(segmented.layer, at: CGPoint(x: 0, y: HUDMetric.headerHeight), to: page)
        // The page retains the group: its segments only hold it weakly.
        page.add(segmented); page.add(segmented.segments)
        let top = HUDMetric.headerHeight + 30 + 10
        if p.isLinked { buildLinked(page, top: top) } else { buildOffline(page, top: top) }
        binders.forEach { $0() }
    }

    private func buildOffline(_ page: HUDDesignPage, top: CGFloat) {
        let size = CGSize(width: HUDMetric.box.width, height: 250), reserved: CGFloat = 16 + 40
        let panel = HUDEmptyState.make(title: L10n.text("NOT LINKED", "未绑定"), subtitle: L10n.text("Hypergryph account", "鹰角网络通行证"),
                                       size: size, reserving: reserved, env: env)
        let connect = HUDButton(id: "connect", title: L10n.text("Connect", "绑定账户"), icon: .link2, kind: .primary, size: .large, env: env)
        let y = HUDEmptyState.textBottom(size: size, subtitle: true, reserving: reserved) + 16
        connect.place(at: CGPoint(x: ((size.width - connect.preferredWidth) / 2).rounded(), y: y))
        connect.onActivate = { [weak self] in guard let self else { return }; self.onAction?(.connect(self.presentation.region)) }
        bind { [weak connect] p in connect?.isEnabled = !p.busy }
        HUDMotion.perform { panel.addSublayer(connect.layer) }
        add(panel, at: CGPoint(x: 0, y: top), to: page)
        page.add(connect)
    }

    private func buildLinked(_ page: HUDDesignPage, top: CGFloat) {
        // Hero: sanity (flex 1) and recovery (flex .8), gap 10.
        let width = HUDMetric.box.width, sanityWidth = ((width - 10) / 1.8).rounded()
        let readout = SanityReadout(sanityWidth: sanityWidth, recoveryWidth: width - 10 - sanityWidth, env: env)
        readout.plate.onActivate = { [weak self] in self?.showRecovery() }
        sanity = readout
        bind { [weak self, weak readout] _ in if let self { readout?.render(self.sanityPresentation, at: self.now()) } }
        add(readout.layer, at: CGPoint(x: 0, y: top), to: page)
        page.add(readout.plate)
        var y = top + readout.layer.bounds.height + 8
        // Rows: game role, header display, sync with refresh.
        let role = dropdown("role", L10n.text("Game role", "游戏角色"), width: 240) { [weak self] anchor in self?.openRoleMenu(anchor) }
        bind { [weak role] p in
            role?.setValue(p.roles.first { $0.id == p.selectedRoleID }?.title ?? L10n.text("Choose account", "选择账户"))
            role?.isEnabled = !p.roles.isEmpty && !p.busy
        }
        let headerMode = dropdown("header", L10n.text("Header display", "顶部显示")) { [weak self] anchor in self?.openHeaderMenu(anchor) }
        bind { [weak headerMode] p in headerMode?.setValue(p.headerMode.title) }
        let refresh = HUDIconButton(id: "refresh", label: L10n.text("Refresh", "刷新"), icon: .refresh, env: env)
        refresh.onActivate = { [weak self] in
            guard let self, !self.presentation.busy else { return }
            self.awaitingRefresh = true
            self.onAction?(.refresh)
        }
        bind { [weak refresh] p in refresh?.isEnabled = !p.busy && !p.needsReconnect }
        let sync = HUDToggle(id: "syncProfile", label: L10n.text("Sync personal profile", "同步个人名片"), isOn: presentation.syncProfile, env: env)
        sync.onChange = { [weak self] on in self?.onAction?(.setSyncProfile(on)) }
        bind { [weak sync] p in sync?.setOn(p.syncProfile, animated: true) }
        let rows = [
            row("role", L10n.text("Game role", "游戏角色"), [role.layer]),
            row("header", L10n.text("Header display", "顶部显示"), [headerMode.layer]),
            row("sync", L10n.text("Sync personal profile", "同步个人名片"), [refresh.layer, sync.layer],
                sublabel: presentation.lastSync.isEmpty ? nil : presentation.lastSync)
        ]
        if let syncRow = rows.last { bind { [weak syncRow] p in if !p.lastSync.isEmpty { syncRow?.setSublabel(p.lastSync) } } }
        for row in rows {
            add(row.layer, at: CGPoint(x: 0, y: y), to: page)
            page.add(row)
            y += row.layer.bounds.height + HUDMetric.rowGap
        }
        page.add([role, headerMode, refresh, sync])
        // Field chips: wrap, gap 6.
        let chips = HUDLayer.plain("account.chips")
        var chipControls: [AccountChip] = []
        for field in HUDAccountPresentation.SyncField.allCases {
            let chip = AccountChip(id: "field:" + field.rawValue, title: field.title, isOn: presentation.syncFields.contains(field), env: env)
            chip.onToggle = { [weak self] on in self?.onAction?(.setSyncField(field, on)) }
            bind { [weak chip] p in chip?.setOn(p.syncFields.contains(field)) }
            chipControls.append(chip)
        }
        let avatar = AccountChip(id: "field:avatar", title: L10n.text("Avatar", "头像"), isOn: presentation.syncAvatar, env: env)
        avatar.onToggle = { [weak self] on in self?.onAction?(.setSyncAvatar(on)) }
        bind { [weak avatar] p in avatar?.setOn(p.syncAvatar) }
        chipControls.append(avatar)
        // A chip's check changes its width: the row wraps again in place.
        let layout = { [weak chips] in
            guard let chips else { return }
            var x: CGFloat = 0, line: CGFloat = 0
            HUDMotion.perform {
                for chip in chipControls {
                    let w = chip.layer.bounds.width
                    if x > 0, x + w > width { x = 0; line += 24 + 6 }
                    chip.layer.frame.origin = CGPoint(x: x, y: line)
                    if chip.layer.superlayer !== chips { chips.addSublayer(chip.layer) }
                    x += w + 6
                }
                chips.bounds.size = CGSize(width: width, height: line + 24)
            }
        }
        chipControls.forEach { $0.onResize = layout }
        layout()
        add(chips, at: CGPoint(x: 0, y: y - HUDMetric.rowGap + 8), to: page)
        page.add(chipControls)
    }

    private func confirmDisconnect() {
        confirm(HUDConfirmRequest(id: "disconnect", title: L10n.text("Disconnect this account?", "解除此账户绑定？"),
                                  message: presentation.region.title, confirmTitle: L10n.text("Disconnect", "解除绑定"),
                                  danger: true, icon: .x)) { [weak self] in
            guard let self, self.presentation.isLinked else { return }
            self.awaitingUnlink = true
            self.onAction?(.disconnect)
        }
    }

    /// The sanity plate's press: next point and full recovery (Appendix C).
    private func showRecovery() {
        let value = sanityPresentation
        let next = HUDAccountGauge.countdown(until: value?.nextRecoveryAt, at: now(), hours: true)
        let full = HUDAccountGauge.countdown(until: value?.fullRecoveryAt, at: now(), hours: true)
        context.toast(HUDToastRequest(icon: .bolt, title: L10n.text("Next \(next)", "下次回复 \(next)"),
                                      caption: L10n.text("All recovered \(full)", "全部回复 \(full)")))
    }

    private func openRoleMenu(_ anchor: HUDControl) {
        let p = presentation
        let items = p.roles.map { HUDMenu.Item(id: $0.id, title: $0.subtitle.isEmpty ? $0.title : $0.title + " / " + $0.subtitle,
                                               selected: $0.id == p.selectedRoleID) }
        guard !items.isEmpty else { return }
        openMenu(id: "role", items: items, anchor: anchor) { [weak self] item in
            guard let self, item.id != self.presentation.selectedRoleID else { return }
            self.onAction?(.selectRole(item.id))
        }
    }

    private func openHeaderMenu(_ anchor: HUDControl) {
        let items = HUDAccountPresentation.HeaderMode.allCases.map {
            HUDMenu.Item(id: $0.rawValue, title: $0.title, selected: $0 == presentation.headerMode)
        }
        openMenu(id: "header", items: items, anchor: anchor) { [weak self] item in
            guard let self, let mode = HUDAccountPresentation.HeaderMode(rawValue: item.id), mode != self.presentation.headerMode else { return }
            self.onAction?(.selectHeaderMode(mode))
        }
    }

    // MARK: Building helpers

    private func bind(_ update: @escaping (HUDAccountPresentation) -> Void) {
        binders.append { [weak self] in if let self { update(self.presentation) } }
    }

    private func add(_ layer: CALayer, at origin: CGPoint, to page: HUDDesignPage) {
        HUDMotion.perform { layer.frame.origin = origin; page.layer.addSublayer(layer) }
        page.stagger(layer)
    }

    private func row(_ id: String, _ title: String, _ cluster: [CALayer], sublabel: String? = nil) -> HUDRowPlate {
        // A compact row has no room for a sublabel under the micro tag: the
        // sync row with its last-sync time keeps the standard height.
        let plate = HUDRowPlate(id: "row:" + id, title: title, sublabel: sublabel, variant: sublabel == nil ? .compact : .standard, env: env)
        plate.setCluster(cluster)
        return plate
    }

    private func dropdown(_ id: String, _ label: String, width: CGFloat? = nil, open: @escaping (HUDDropdown) -> Void) -> HUDDropdown {
        let field = HUDDropdown(id: id, label: label, value: "", width: width, env: env)
        field.onActivate = { [weak self, weak field] in
            guard let self, let field else { return }
            if self.isMenuOpen { self.closeMenu() } else { open(field) }
        }
        return field
    }
}

/// The hero row's two dark stat plates (spec §3.21): SANITY with the 56 px
/// numeral, `/ max` and 24 capacity cells; NEXT POINT and ALL RECOVERED. The
/// values change in place every second.
private final class SanityReadout {
    let layer = HUDLayer.plain("account.hero")
    let plate: HUDControl
    private let number = HUDTextLayer(style: HUDType.numeral.sized(56))
    private let maximum = HUDTextLayer(style: HUDType.label)
    private let cells: HUDCells
    private let next = HUDTextLayer(style: HUDType.statValue)
    private let full = HUDTextLayer(style: HUDType.statValue)
    private var rendered: (String, String, Int, String, String)?
    private let env: HUDDesignEnvironment
    static let height: CGFloat = 9 + 10 + 4 + 56 * 0.9 + 8 + 8 + 10

    init(sanityWidth: CGFloat, recoveryWidth: CGFloat, env: HUDDesignEnvironment) {
        self.env = env
        let height = Self.height
        let sanity = Self.statPlate(CGSize(width: sanityWidth, height: height), env: env)
        let recovery = Self.statPlate(CGSize(width: recoveryWidth, height: height), env: env)
        plate = HUDControl(id: "sanity", label: L10n.text("Sanity", "理智"), env: env, layer: sanity)
        cells = HUDCells(count: 24, width: sanityWidth - 24, capacity: true, height: 8, env: env)
        HUDMotion.perform {
            layer.bounds = CGRect(x: 0, y: 0, width: sanityWidth + 10 + recoveryWidth, height: height)
            sanity.frame.origin = .zero
            recovery.frame.origin = CGPoint(x: sanityWidth + 10, y: 0)
            let label = HUDTextLayer("SANITY", style: HUDType.label)
            label.lineFrame = CGRect(x: 12, y: 9, width: sanityWidth - 24, height: 10)
            cells.layer.frame.origin = CGPoint(x: 12, y: height - 10 - 8)
            [label, number, maximum, cells.layer].forEach(sanity.addSublayer)
            for (index, (title, value)) in [("NEXT POINT", next), ("ALL RECOVERED", full)].enumerated() {
                let top: CGFloat = 9 + CGFloat(index) * (10 + 6 + 18 + 12)
                let caption = HUDTextLayer(title, style: HUDType.label)
                caption.lineFrame = CGRect(x: 12, y: top, width: recoveryWidth - 24, height: 10)
                value.lineFrame = CGRect(x: 12, y: top + 16, width: recoveryWidth - 24, height: 18)
                recovery.addSublayer(caption); recovery.addSublayer(value)
            }
            layer.addSublayer(sanity); layer.addSublayer(recovery)
        }
        plate.setTarget(sanity.bounds)
        env.applyScale(to: layer)
    }

    /// The `.sp` plate: dark gradient, #343434 ring, grey L-corner.
    private static func statPlate(_ size: CGSize, env: HUDDesignEnvironment) -> CALayer {
        let plate = HUDLayer.plain("account.stat")
        HUDMotion.perform {
            plate.bounds = CGRect(origin: .zero, size: size)
            plate.showArtwork(HUDArtwork.plate([HUDColor.tone(0x232323), HUDColor.tone(0x1B1B1B)], size: size, cut: 0, scale: env.renderScale),
                              scale: env.renderScale)
            plate.borderWidth = 1; plate.borderColor = HUDColor.ringTab
            let corner = HUDLayer.shape()
            corner.frame = CGRect(x: 0, y: 0, width: 8, height: 8)
            corner.path = HUDShape.cornerMark(corner.bounds, length: 8, width: 2, corner: .topLeft)
            corner.fillColor = HUDColor.tone(0x6A6A6A)
            plate.addSublayer(corner)
        }
        return plate
    }

    func render(_ value: HypergryphSanityPresentation?, at date: Date) {
        let current = value.map { String($0.current) } ?? "—"
        let limit = "/ " + (value.map { String($0.maximum) } ?? "—")
        let filled = value.map { $0.maximum > 0 ? Int((Double($0.current) / Double($0.maximum) * 24).rounded()) : 0 } ?? 0
        let nextText = HUDAccountGauge.countdown(until: value?.nextRecoveryAt, at: date, hours: true)
        let fullText = HUDAccountGauge.countdown(until: value?.fullRecoveryAt, at: date, hours: true)
        let state = (current, limit, min(24, filled), nextText, fullText)
        if let rendered, rendered == state { return }
        let first = rendered == nil
        rendered = state
        HUDMotion.perform {
            number.text = current
            let width = ceil(number.preferredWidth)
            number.lineFrame = CGRect(x: 12, y: 23, width: width + 1, height: 56 * 0.9)
            maximum.text = limit
            maximum.lineFrame = CGRect(x: 12 + width + 10, y: 23 + 56 * 0.9 - 8 - 10, width: ceil(maximum.preferredWidth) + 1, height: 10)
            next.text = nextText
            full.text = fullText
        }
        cells.set(filled: state.2, animated: !first)
        plate.label = L10n.text("Sanity", "理智") + " " + current + " " + limit
        env.applyScale(to: number); env.applyScale(to: maximum)
    }
}

/// A field chip that toggles (spec §3.21): 24 px tall, yellow with a check
/// when on, outlined when off.
private final class AccountChip: HUDControl {
    private(set) var isOn: Bool
    var onToggle: ((Bool) -> Void)?
    var onResize: (() -> Void)?
    private let check = HUDIconLayer(.check, size: 10, color: HUDColor.nearBlack)
    private let text: HUDTextLayer
    private let title: String

    init(id: String, title: String, isOn: Bool, env: HUDDesignEnvironment) {
        self.isOn = isOn; self.title = title
        text = HUDTextLayer(title, style: HUDType.chip)
        super.init(id: id, label: title, env: env)
        HUDMotion.perform { layer.addSublayer(check); layer.addSublayer(text) }
        render()
    }

    func setOn(_ value: Bool) { guard value != isOn else { return }; isOn = value; render(); onResize?() }

    private func render() {
        HUDMotion.perform {
            let width = ceil(text.preferredWidth)
            var x: CGFloat = 7
            check.isHidden = !isOn
            if isOn { check.frame = CGRect(x: x, y: 7, width: 10, height: 10); x += 15 }
            text.textStyle = HUDType.chip.colored(isOn ? HUDColor.nearBlack : HUDColor.tone(0xCFCFCF))
            text.lineFrame = CGRect(x: x, y: 7.25, width: width + 1, height: 9.5)
            layer.bounds = CGRect(x: 0, y: 0, width: x + width + 7, height: 24)
            layer.backgroundColor = isOn ? HUDColor.yellow : HUDColor.clear
            layer.borderWidth = isOn ? 0 : 1; layer.borderColor = HUDColor.tone(0x5A5A5A)
        }
        setTarget(layer.bounds)
        env.applyScale(to: text)
    }

    override func activate() {
        guard isEnabled else { return }
        isOn.toggle(); render(); onResize?()
        onToggle?(isOn)
    }
    override var accessibilityRole: HUDAccessibilityItem.Role? { .toggle(isOn) }
}
