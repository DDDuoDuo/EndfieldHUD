import Foundation
import simd

enum HUDSourceDesktopNavigationLayoutTests {
    static func run() -> Int {
        var assertions = 0
        func check(_ value: Bool, _ message: String) {
            assertions += 1; precondition(value, message)
        }
        do {
            let document = try HUDSourceWatchDocument(includeWidgets: false)
            for count in [0, 1, 8, 18, 19, 100, 1000] {
                let layout = try HUDSourceDesktopNavigationLayout(document: document, entryCount: count)
                var reached = Set<Int>()
                var lastHeight = 0.0
                for index in 0...1000 {
                    let normalized = 1 - Double(index) / 1000
                    let sample = layout.sample(normalizedPosition: normalized)
                    reached.formUnion(sample.assignments.values)
                    check(sample.assignments.count <= 18 && Set(sample.assignments.values).count == sample.assignments.count,
                          "Virtual navigation binds each entry once in a bounded source pool")
                    check(sample.assignments.values.allSatisfy { $0 >= 0 && $0 < count },
                          "Virtual navigation never binds an absent shortcut")
                    check(sample.contentHeight >= layout.viewportHeight && (index == 0 || sample.contentHeight == lastHeight),
                          "Scrolling retains a stable, valid logical content extent")
                    lastHeight = sample.contentHeight
                }
                check(reached == Set(0..<count), "Every shortcut and the trailing Add App remains reachable")
                let bottom = layout.sample(normalizedPosition: 0)
                if count > 0 { check(bottom.assignments.values.contains(count - 1), "Bottom of navigation reaches its last entry") }
                var pose = try document.animation.pose(entranceTime: document.animation.entrance.lastKeyTime,
                    ambientTime: nil, exitTime: nil, canvasResolution: SIMD2(2400, 1350))
                layout.apply(to: &pose, normalizedPosition: 0)
                let nodes = try document.scene.resolve(overrides: pose.transforms)
                check(nodes[layout.contentID]?.rect?.size.y == bottom.contentHeight,
                      "The real source content rect uses the complete logical height")
                for row in layout.rows {
                    check(nodes[row.id]?.activeInHierarchy == (bottom.logicalRows[row.id] != nil),
                          "Only assigned physical rows render and receive input")
                }
            }
        } catch { fatalError("Desktop source navigation: \(error)") }
        return assertions
    }
}
