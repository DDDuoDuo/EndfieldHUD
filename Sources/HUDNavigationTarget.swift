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
    static let modules: [HUDModule] = [.system, .display, .hotkeys, .about, .storage, .activityMonitor]
        + rightModules.filter { $0 != .addApp }

    static func entries(shortcuts: [HUDAppShortcutPresentation]) -> [Entry] {
        var seen: Set<UUID> = []
        let unique = shortcuts.filter { seen.insert($0.id).inserted }
        let targets = modules.map { (HUDNavigationTarget.module($0), $0.title) }
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
