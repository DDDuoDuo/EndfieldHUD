import AppKit
import QuartzCore

/// The Projection screen of the redesign (spec §3.14): pen presets and the
/// launcher of the projection workspace (`onOpen`, wired by the host).
final class ProjectionCanvas: HUDDesignCanvas {
    var onOpen: (() -> Void)?

    init(context: HUDDesignContext) { super.init(module: .projection, context: context) }

    override func buildPage(_ page: HUDDesignPage) {
        header.configure(crumbs: ["PROJECTION"], code: "MOD-09", title: HUDModuleTitle("Projection", "投影"),
                         counter: "BG ON", showsBack: false)
    }
}
