import AppKit

/// A saved application is an action with its own identity, never an alias for
/// the Add App module. Module selection remains independent of launch clicks.
enum HUDNavigationTarget: Hashable {
    case module(HUDModule)
    case appShortcut(UUID)

    var module: HUDModule? {
        if case .module(let module) = self { return module }
        return nil
    }
    var group: HUDModuleGroup { module?.group ?? .right }
    var identifier: String {
        switch self {
        case .module(let module): return module.rawValue
        case .appShortcut(let id): return "app.\(id.uuidString)"
        }
    }
}

struct HUDAppShortcutPresentation {
    let id: UUID
    let name: String
    let iconPreset: AppShortcutIcon
    let icon: NSImage?
}

/// The authored Watch scene supplies the plates and motion. Desktop actions
/// and labels retain their identities independently of the game's button names.
enum HUDDesktopWatchNavigation {
    struct Entry {
        let sourceName: String
        let target: HUDNavigationTarget
        let title: String
        let shortcut: HUDAppShortcutPresentation?
    }
    static let sourceNames = [
        "CharInfoBtnShadow", "ActivityBtnShadow", "GachaBtnShadow", "PurchaseBtnNode",
        "AdventureBookBtnShadow", "BattlePassShadow", "DomainBtnShadow", "FriendBtn1Shadow",
        "EquipBtnShadow", "CharfomationBtnNode", "WikiBtnShadow", "ValuablesBtnShadow",
        "StarShopBtnShadow", "NarrateBtnShadow", "BackPackBtnShadow", "AchievementBtn2Shadow",
        "GemEnhanceBtnShadow", "MissionBtnShadow", "MapBtnShadow", "SNSBtnShadow",
        "QuestionnaireBtnShadow", "GameToolShadow"
    ]
    /// Row-major desktop order; saved apps retain their own order between
    /// Power and the always-last Add App action.
    static let rightModules: [HUDModule] = [
        .notes, .fileShelf,
        .clipboard, .archive,
        .mediaAssembly, .minigame,
        .nowPlaying, .volume,
        .projection, .reader,
        .workMode, .calendar,
        .map, .eventLog,
        .profile, .account,
        .power, .addApp
    ]
    static let fixedModules: [HUDModule] = [.system, .display, .hotkeys, .about, .storage, .activityMonitor]

    /// The right-hand modules follow the user's layout (System > Modules);
    /// hidden ones get no entry, so pointer, accessibility and recycled rows
    /// all skip them. The layout defaults to the one currently applied.
    static func entries(shortcuts: [HUDAppShortcutPresentation],
                        layout: HUDModuleLayout = HUDRuntimeAppearance.configuration.moduleLayout) -> [Entry] {
        var seen: Set<UUID> = []
        let unique = shortcuts.filter { seen.insert($0.id).inserted }
        let targets = (fixedModules + layout.visible).map { (HUDNavigationTarget.module($0), $0.title) }
            + unique.map { (HUDNavigationTarget.appShortcut($0.id), $0.name) }
            + [(HUDNavigationTarget.module(.addApp), HUDModule.addApp.title)]
        let byID = Dictionary(uniqueKeysWithValues: unique.map { ($0.id, $0) })
        return targets.enumerated().map { index, item in
            let shortcut: HUDAppShortcutPresentation?
            if case .appShortcut(let id) = item.0 { shortcut = byID[id] } else { shortcut = nil }
            return Entry(sourceName: sourceName(at: index), target: item.0, title: item.1, shortcut: shortcut)
        }
    }
    static func sourceName(at index: Int) -> String {
        index < sourceNames.count ? sourceNames[index] : "DesktopButton_\(index)"
    }
}

/// The user's order and visibility of the modules in the right-hand list
/// (System > Modules). Saved apps and Add App keep following the visible
/// modules; modules outside that list are not configurable. Every value is
/// normalized: each configurable module appears once, unknown or duplicate
/// entries are dropped, and at least one module stays visible.
struct HUDModuleLayout: Equatable {
    /// Keys added after v1.2.0, which shares the defaults domain and ignores them.
    static let orderKey = "hudModuleOrder", hiddenKey = "hudHiddenModules"
    /// The configurable modules in their default desktop order.
    static let configurable = HUDDesktopWatchNavigation.rightModules.filter { $0 != .addApp }
    private(set) var order: [HUDModule]
    private(set) var hidden: Set<HUDModule>

    init(order: [HUDModule] = configurable, hidden: Set<HUDModule> = []) {
        var seen = Set<HUDModule>()
        let kept = order.filter { Self.configurable.contains($0) && seen.insert($0).inserted }
        // Modules a later version adds (or a damaged list misses) join at the end.
        self.order = kept + Self.configurable.filter { !seen.contains($0) }
        self.hidden = hidden.intersection(Self.configurable)
        if self.hidden.count == Self.configurable.count { self.hidden.remove(self.order[0]) }
    }

    init(defaults: UserDefaults) {
        let read = { (key: String) in (defaults.stringArray(forKey: key) ?? []).compactMap(HUDModule.init(rawValue:)) }
        self.init(order: defaults.object(forKey: Self.orderKey) == nil ? Self.configurable : read(Self.orderKey),
                  hidden: Set(read(Self.hiddenKey)))
    }

    /// The default layout leaves no keys behind.
    func save(to defaults: UserDefaults) {
        if order == Self.configurable { defaults.removeObject(forKey: Self.orderKey) }
        else { defaults.set(order.map(\.rawValue), forKey: Self.orderKey) }
        if hidden.isEmpty { defaults.removeObject(forKey: Self.hiddenKey) }
        else { defaults.set(order.filter(hidden.contains).map(\.rawValue), forKey: Self.hiddenKey) }
    }

    var visible: [HUDModule] { order.filter { !hidden.contains($0) } }
    /// The module to reopen the HUD on: a hidden one is not restored, the
    /// first visible module of the list stands in for it.
    func restorable(_ module: HUDModule) -> HUDModule { hidden.contains(module) ? visible[0] : module }
    var isDefault: Bool { order == Self.configurable && hidden.isEmpty }
    func isHidden(_ module: HUDModule) -> Bool { hidden.contains(module) }
    /// False when the change would hide the last visible module.
    func canSetHidden(_ module: HUDModule, _ value: Bool) -> Bool {
        !value || !Self.configurable.contains(module) || hidden.contains(module) || visible.count > 1
    }

    @discardableResult mutating func setHidden(_ module: HUDModule, _ value: Bool) -> Bool {
        guard Self.configurable.contains(module), canSetHidden(module, value) else { return false }
        if value { hidden.insert(module) } else { hidden.remove(module) }
        return true
    }

    /// Moves `module` to `index` in the full order (clamped).
    @discardableResult mutating func move(_ module: HUDModule, to index: Int) -> Bool {
        guard let from = order.firstIndex(of: module) else { return false }
        let to = min(order.count - 1, max(0, index))
        guard to != from else { return false }
        order.remove(at: from); order.insert(module, at: to)
        return true
    }
}
