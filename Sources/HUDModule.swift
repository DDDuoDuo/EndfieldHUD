import Foundation

enum HUDModuleGroup: String { case left, right, bottom, power }

enum HUDModule: String, CaseIterable {
    case notes, fileShelf, clipboard, volume, workMode, eventLog, map, addApp
    case system, display, hotkeys, about, storage, activityMonitor, power, profile

    var title: String {
        switch self {
        case .notes: return L10n.text("Notes", "便笺")
        case .fileShelf: return L10n.text("Temporary File Shelf", "文件暂存架")
        case .clipboard: return L10n.text("Clipboard Cache", "剪贴板")
        case .volume: return L10n.text("Volume", "音量")
        case .workMode: return L10n.text("Work Mode", "工作模式")
        case .eventLog: return L10n.text("Event Log", "事件日志")
        case .map: return L10n.text("Map", "地图")
        case .addApp: return L10n.text("+ Add App", "+ 添加应用")
        case .system: return L10n.text("System", "系统")
        case .display: return L10n.text("Display", "显示")
        case .hotkeys: return L10n.text("Hotkeys", "快捷键")
        case .about: return L10n.text("About", "关于")
        case .storage: return L10n.text("Storage", "存储")
        case .activityMonitor: return L10n.text("Activity Monitor", "活动监视器")
        case .power: return L10n.text("Power", "电源")
        case .profile: return L10n.text("Personal Profile", "个人名片")
        }
    }

    var englishTitle: String {
        switch self {
        case .notes: return "Notes"
        case .fileShelf: return "Temporary File Shelf"
        case .clipboard: return "Clipboard Cache"
        case .volume: return "Volume"
        case .workMode: return "Work Mode"
        case .eventLog: return "Event Log"
        case .map: return "Map"
        case .addApp: return "+ Add App"
        case .system: return "System"
        case .display: return "Display"
        case .hotkeys: return "Hotkeys"
        case .about: return "About"
        case .storage: return "Storage"
        case .activityMonitor: return "Activity Monitor"
        case .power: return "Power"
        case .profile: return "Personal Profile"
        }
    }

    var localizedTitle: String { title }
    /// Canvas geometry is independent of the retained shell. Large instruments
    /// can occupy the complete dial without stretching note/file coordinates.
    var contentFrame: CGRect {
        self == .workMode || self == .map ? CGRect(x: 280, y: 100, width: 440, height: 440)
            : CGRect(x: 300, y: 152, width: 400, height: 334)
    }
    var navigationTitle: String {
        self == .power ? L10n.text("Power / Device Battery", "电源 / 设备电量") : title
    }

    var group: HUDModuleGroup {
        switch self {
        case .notes, .fileShelf, .clipboard, .volume, .workMode, .eventLog, .map, .addApp: return .right
        case .system, .display, .hotkeys, .about: return .left
        case .storage, .activityMonitor: return .bottom
        case .power, .profile: return .power
        }
    }
}

/// Only one swap runs at a time. Requests received during it replace the queued
/// destination; stale Core Animation completions cannot commit a newer request.
struct HUDModuleSelectionState {
    struct Transition: Equatable {
        let from: HUDModule
        let to: HUDModule
        let generation: Int
    }

    private(set) var selectedModule: HUDModule
    private(set) var transitioningTo: HUDModule?
    private(set) var pendingModule: HUDModule?
    private(set) var generation = 0
    var isTransitioning: Bool { transitioningTo != nil }
    var requestedModule: HUDModule { pendingModule ?? transitioningTo ?? selectedModule }

    init(selectedModule: HUDModule = .power) { self.selectedModule = selectedModule }

    mutating func request(_ module: HUDModule) -> Transition? {
        if let destination = transitioningTo {
            pendingModule = module == destination ? nil : module
            return nil
        }
        guard module != selectedModule else { return nil }
        generation += 1
        transitioningTo = module
        return Transition(from: selectedModule, to: module, generation: generation)
    }

    mutating func complete(generation token: Int) -> Transition? {
        guard token == generation, let destination = transitioningTo else { return nil }
        selectedModule = destination
        transitioningTo = nil
        let next = pendingModule
        pendingModule = nil
        return next.flatMap { request($0) }
    }

    /// Cancellation restores the last fully committed screen and drops requests.
    @discardableResult mutating func cancel() -> HUDModule {
        generation += 1
        transitioningTo = nil
        pendingModule = nil
        return selectedModule
    }

    mutating func settle(on module: HUDModule) {
        generation += 1
        selectedModule = module
        transitioningTo = nil
        pendingModule = nil
    }
}
