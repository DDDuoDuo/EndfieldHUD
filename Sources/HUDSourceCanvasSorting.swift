import Foundation

/// Shipped UICtrl.SetSortingOrder / UISortingOrder.SetOrder Canvas rules.
/// The panel base is allocated by UIManager's live stack and must be injected;
/// an authored Canvas order (6080 in WatchPanel_PC) is not that live stack.
struct HUDSourceCanvasSorting {
    struct State: Equatable {
        let nearestCanvasID: HUDSourceID?
        /// Effective order inherited by graphics and non-override subcanvases.
        let sortingOrder: Int
        /// Present only on this node's enabled Canvas, including its unused
        /// local order when overrideSorting is false.
        let localCanvasSortingOrder: Int?
        let overrideSorting: Bool
        var startsSortingBoundary: Bool { localCanvasSortingOrder != nil && overrideSorting }
    }

    private let scene: HUDSourceScene
    private let components: [HUDSourceID: [HUDSourceWatchComponent]]

    init(scene: HUDSourceScene, components: [HUDSourceID: [HUDSourceWatchComponent]]) {
        self.scene = scene; self.components = components
    }

    /// A registered component receives the panel's base directly, including
    /// when Awake obtains it from its LuaPanel Canvas. Parent offsets are not
    /// accumulated. nil means all declared source sorting components have
    /// registered; a caller tracking activation can supply its actual set.
    /// Registration survives component disable until OnDestroy, so this list
    /// deliberately does not infer registration from m_Enabled.
    func resolve(panelBase: Int,
                 registeredSortingComponentIDs: Set<HUDSourceID>? = nil) -> [HUDSourceID: State] {
        var result: [HUDSourceID: State] = [:]
        for id in scene.traversalIDs {
            guard let node = scene.node(id) else { continue }
            let inherited = node.parentID.flatMap { result[$0] }
            guard let canvas = components[id]?.first(where: { $0.kind == "Canvas" && $0.enabled }) else {
                result[id] = State(nearestCanvasID: inherited?.nearestCanvasID,
                    sortingOrder: inherited?.sortingOrder ?? 0,
                    localCanvasSortingOrder: nil, overrideSorting: false)
                continue
            }
            var localOrder = Int(canvas["m_SortingOrder"].float())
            var overrideSorting = canvas["m_OverrideSorting"].flag()
            if id == scene.rootID { localOrder = panelBase }
            for component in components[id] ?? [] where component.kind == "UISortingOrder" {
                guard component["_renderType"].float(-1) == 1,
                      registeredSortingComponentIDs?.contains(component.id) != false else { continue }
                // Type 0 writes Renderer orders, even on a Canvas GameObject.
                // Type 2 writes particles. Neither changes Canvas boundaries.
                localOrder = panelBase + Int(component["_sortingOrderOffset"].float())
                overrideSorting = true
            }
            let effectiveOrder = overrideSorting || inherited?.nearestCanvasID == nil
                ? localOrder : (inherited?.sortingOrder ?? localOrder)
            result[id] = State(nearestCanvasID: id, sortingOrder: effectiveOrder,
                localCanvasSortingOrder: localOrder, overrideSorting: overrideSorting)
        }
        return result
    }
}
