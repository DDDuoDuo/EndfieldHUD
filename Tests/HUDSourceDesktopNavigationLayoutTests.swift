import Foundation
import CoreGraphics
import simd

enum HUDSourceDesktopNavigationLayoutTests {
    static func run() -> Int {
        var assertions = 0
        func check(_ value: Bool, _ message: String) {
            assertions += 1; precondition(value, message)
        }
        do {
            var scroll = HUDSourceDesktopScrollMotion()
            scroll.reset(to: 1, at: 0)
            scroll.scroll(by: -0.3, hiddenLength: 1200, at: 0, reduceMotion: false)
            check(scroll.position == 1 && scroll.isAnimating, "Wheel input starts a finite response without jumping to its target")
            let first = scroll.advance(at: 1.0 / 60)
            check(first < 1 && first > 0.7, "Scroll approaches its target smoothly")
            _ = scroll.advance(at: 2)
            check(scroll.position == 0.7 && !scroll.isAnimating, "Scroll settles exactly and releases its animation clock")
            scroll.reset(to: 1, at: 3)
            scroll.scroll(by: 5, hiddenLength: 20_000, at: 3, reduceMotion: false)
            check(scroll.position > 1 && (scroll.position - 1) * 20_000 <= 36.0001, "Edge bounce is bounded in content pixels even with many shortcuts")
            _ = scroll.advance(at: 5)
            check(scroll.position == 1 && !scroll.isAnimating, "Elastic edge returns to the exact valid endpoint")
            scroll.scroll(by: -0.4, hiddenLength: 1200, at: 6, reduceMotion: true)
            check(scroll.position == 0.6 && !scroll.isAnimating, "Reduce Motion scrolls immediately without a spring")
            let unchanged = scroll.position
            scroll.scroll(by: .nan, hiddenLength: 1200, at: 7, reduceMotion: false)
            check(scroll.position == unchanged, "Invalid wheel input cannot poison presentation state")
            var a = HUDSourceDesktopScrollMotion(), b = HUDSourceDesktopScrollMotion()
            a.reset(to: 1, at: 0); b.reset(to: 1, at: 0)
            a.scroll(by: -0.5, hiddenLength: 1000, at: 0, reduceMotion: false)
            b.scroll(by: -0.5, hiddenLength: 1000, at: 0, reduceMotion: false)
            for frame in 1...10 { _ = a.advance(at: Double(frame) / 60) }
            _ = b.advance(at: 1.0 / 6)
            check(abs(a.position - b.position) < 1e-12, "Spring sampling is independent of dropped display ticks")
            let path = CGMutablePath(); path.addRect(CGRect(x: 25, y: 10, width: 8, height: 20))
            let fitted = HUDSourceDesktopIconLayout.path(path).boundingBoxOfPath
            check(abs(max(fitted.width, fitted.height) - 26) < 1e-10 && abs(fitted.midX - 16) < 1e-10 && abs(fitted.midY - 16) < 1e-10,
                  "Vector icons share centered visible bounds without stretching")
            let document = try HUDSourceWatchDocument(includeWidgets: false)
            let motion = HUDSourceDesktopAmbientMotion(animation: document.animation, seed: 1234)
            let same = HUDSourceDesktopAmbientMotion(animation: document.animation, seed: 1234)
            let other = HUDSourceDesktopAmbientMotion(animation: document.animation, seed: 5678)
            check(motion.channels.map(\.rate) == same.channels.map(\.rate), "An opening retains deterministic sampled speeds")
            check(motion.channels.map(\.rate) != other.channels.map(\.rate), "Another opening selects different decorative motion")
            check(motion.channels.filter { $0.curve == nil }.count == 6, "Each of the six triangles has independent local motion")
            check(motion.channels.contains { $0.rate < 0 } && motion.channels.contains { $0.rate > 0 }, "Decoration supports both rotation directions")
            let playback = HUDSourceWatchPlayback(animation: document.animation)
            playback.desktopAmbientMotion = motion; playback.showStable(at: 0)
            for time in [0.0, 0.01, 0.3, 2.0, 100_000.0] {
                let full = try playback.sample(at: time, canvasResolution: SIMD2(2400, 1350), reduceMotion: false)!
                let sparse = playback.sampleAmbient(at: time)!
                check(sparse.transforms.count == motion.channels.count && sparse.transforms.allSatisfy {
                    full.transforms[$0.key]?.localRotation == $0.value.localRotation
                },
                      "Sparse and complete clocks use the same per-opening decorative channels")
                check(try sparse.transforms.values.allSatisfy { value in
                    guard let rotation = value.localRotation else { return false }
                    return HUDSourceGeometry.isFinite(try rotation.matrix())
                }, "Decorative rotation remains finite for long-running openings")
            }
            playback.ambientMotionEnabled = false
            check(playback.sampleAmbient(at: 3)?.transforms.isEmpty == true, "Ambient-off stops all randomized decoration")
            let reduced = try playback.sample(at: 3, canvasResolution: SIMD2(2400, 1350), reduceMotion: true)!
            let still = try document.animation.pose(entranceTime: document.animation.entrance.lastKeyTime,
                ambientTime: nil, exitTime: nil, canvasResolution: SIMD2(2400, 1350))
            check(reduced.transforms == still.transforms, "Reduce Motion preserves the stationary authored pose")
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
