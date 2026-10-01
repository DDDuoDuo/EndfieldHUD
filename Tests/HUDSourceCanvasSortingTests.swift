import Foundation

enum HUDSourceCanvasSortingTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) {
            count += 1; if !condition { fatalError(message) }
        }
        func id(_ value: Int) -> HUDSourceID { HUDSourceID(rawValue: "CAB-sorting:\(value)") }
        func node(_ value: Int, parent: Int?, children: [Int]) -> HUDSourceNode {
            HUDSourceNode(id: id(value), path: "fixture/\(value)", name: "\(value)",
                parentID: parent.map(id), childIDs: children.map(id),
                transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(0, 0, 0)))
        }
        func canvas(_ value: Int, order: Int, overrideSorting: Bool, enabled: Bool = true) -> HUDSourceWatchComponent {
            HUDSourceWatchComponent(id: id(value + 100), type: "Canvas", script: nil, data: [
                "m_Enabled": .bool(enabled), "m_SortingOrder": .number(Double(order)),
                "m_OverrideSorting": .bool(overrideSorting)])
        }
        func writer(_ value: Int, type: Int, offset: Int, enabled: Bool = true) -> HUDSourceWatchComponent {
            HUDSourceWatchComponent(id: id(value + 200), type: "MonoBehaviour", script: "UISortingOrder", data: [
                "m_Enabled": .bool(enabled), "_renderType": .number(Double(type)),
                "_sortingOrderOffset": .number(Double(offset))])
        }
        do {
            let scene = try HUDSourceScene(rootID: id(1), nodes: [
                node(1, parent: nil, children: [2, 6]), node(2, parent: 1, children: [3]),
                node(3, parent: 2, children: [4]), node(4, parent: 3, children: [5]),
                node(5, parent: 4, children: []), node(6, parent: 1, children: [])])
            let components = [id(1): [canvas(1, order: 6080, overrideSorting: true)],
                // The real canvas_watch writer is type 0, offset 5. Its Canvas
                // must inherit the root and must not start a new mask boundary.
                id(2): [canvas(2, order: 0, overrideSorting: false), writer(2, type: 0, offset: 5)],
                id(3): [canvas(3, order: 6092, overrideSorting: true), writer(3, type: 1, offset: 12)],
                // Deliberately nested to distinguish base+offset from erroneous
                // parentOrder+offset. A registered disabled MonoBehaviour is
                // still visited by UICtrl's sortingOrderComps loop.
                id(4): [canvas(4, order: 0, overrideSorting: false), writer(4, type: 1, offset: -3, enabled: false)],
                id(6): [canvas(6, order: 99, overrideSorting: true, enabled: false), writer(6, type: 1, offset: 50)]]
            let sorting = HUDSourceCanvasSorting(scene: scene, components: components)
            let states = sorting.resolve(panelBase: 7020)
            check(states[id(1)]?.sortingOrder == 7020,
                  "UIManager's injected live base replaces the authored panel order")
            check(states[id(2)]?.sortingOrder == 7020 && states[id(2)]?.localCanvasSortingOrder == 0
                  && states[id(2)]?.startsSortingBoundary == false,
                  "Renderer-type sorting on a Canvas does not change its Canvas order or mask boundary")
            check(states[id(3)]?.sortingOrder == 7032 && states[id(3)]?.startsSortingBoundary == true,
                  "Canvas-type sorting enables overrideSorting and adds the panel base")
            check(states[id(4)]?.sortingOrder == 7017 && states[id(4)]?.overrideSorting == true,
                  "Nested registered Canvas writers use the same panel base, not the parent's offset")
            check(states[id(5)]?.nearestCanvasID == id(4) && states[id(5)]?.sortingOrder == 7017
                  && states[id(5)]?.startsSortingBoundary == false,
                  "A graphic inherits the nearest Canvas order without inventing another mask boundary")
            check(states[id(6)]?.nearestCanvasID == id(1) && states[id(6)]?.sortingOrder == 7020
                  && states[id(6)]?.localCanvasSortingOrder == nil,
                  "A disabled Canvas does not become a graphic sorting or clipping boundary")
            let registered = sorting.resolve(panelBase: 7000, registeredSortingComponentIDs: [id(203)])
            check(registered[id(3)]?.sortingOrder == 7012 && registered[id(4)]?.sortingOrder == 7012
                  && registered[id(4)]?.startsSortingBoundary == false,
                  "An explicit registration set preserves unregistered Canvas fields and parent inheritance")
            let moved = sorting.resolve(panelBase: 7060)
            check(moved[id(3)]?.sortingOrder == 7072 && moved[id(4)]?.sortingOrder == 7057,
                  "UIManager rebasing updates all registered offsets against the new common base")
        } catch { fatalError("Canvas sorting fixture failed: \(error)") }
        return count
    }
}
