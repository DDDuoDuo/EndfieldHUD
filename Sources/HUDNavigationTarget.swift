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
