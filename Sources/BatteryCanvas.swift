import AppKit
import QuartzCore

/// The Battery screen of the redesign (spec §3.22), reached from the ⚡ chip.
/// It replaces the legacy Power layer; the host forwards every reading here.
final class BatteryCanvas: HUDDesignCanvas {
    private(set) var snapshot: BatterySnapshot = .unavailable
    private(set) var configuration: AppConfiguration = .defaults

    init(context: HUDDesignContext) { super.init(module: .power, context: context) }

    /// Every battery reading and configuration change (main thread).
    func update(snapshot: BatterySnapshot, configuration: AppConfiguration) {
        guard snapshot != self.snapshot || configuration != self.configuration else { return }
        self.snapshot = snapshot; self.configuration = configuration
        if renderedLanguage != nil { rerender() }
    }

    override func buildPage(_ page: HUDDesignPage) {
        header.configure(crumbs: ["BATTERY"], code: "PWR-00", title: HUDModuleTitle("Battery", "电池状态"),
                         counter: "LIVE", showsBack: false)
    }
}
