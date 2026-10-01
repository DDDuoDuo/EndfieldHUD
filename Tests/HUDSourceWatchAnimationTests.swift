import Foundation
import simd

enum HUDSourceWatchAnimationTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            count += 1; if !condition() { fatalError(message) }
        }
        do {
            let rootID = HUDSourceID(rawValue: "CAB-fixture:1")
            let childID = HUDSourceID(rawValue: "CAB-fixture:2")
            let rect = HUDSourceRectTransform(anchorMin: HUDSourceVector2(0, 0), anchorMax: HUDSourceVector2(0, 0),
                anchoredPosition: HUDSourceVector2(0, 0), sizeDelta: HUDSourceVector2(0, 0), pivot: HUDSourceVector2(0.5, 0.5))
            let scene = try HUDSourceScene(rootID: rootID, nodes: [
                HUDSourceNode(id: rootID, path: "Watch", name: "Watch", parentID: nil, childIDs: [childID],
                    transform: HUDSourceTransform(kind: .rectTransform, localPosition: HUDSourceVector3(0, 0, 0),
                        localScale: HUDSourceVector3(0, 0, 0), rect: rect)),
                HUDSourceNode(id: childID, path: "Watch/Child", name: "Child", parentID: rootID, childIDs: [],
                    transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(3, 4, 5)))])
            func curve(_ property: String, _ values: [Double], nodes: [HUDSourceID]? = nil, interval: Double = 1) throws -> HUDSourceAnimationCurve {
                let keys = values.enumerated().map { index, value in HUDSourceAnimationKey(time: Double(index) * interval,
                    value: .scalar(value), inSlope: .scalar(0), outSlope: .scalar(0), weightedMode: 0,
                    inWeight: .scalar(1.0 / 3), outWeight: .scalar(1.0 / 3)) }
                return try HUDSourceAnimationCurve(group: "m_FloatCurves", path: "Child", attribute: property,
                    nodeIDs: nodes ?? [childID], body: HUDSourceAnimationCurve.Body(keys: keys, preInfinity: 2, postInfinity: 2))
            }
            func clip(_ binding: String, _ duration: Double, _ wrap: Int, _ curves: [HUDSourceAnimationCurve]) -> HUDSourceAnimationClip {
                HUDSourceAnimationClip(binding: binding, id: HUDSourceID(rawValue: "CAB-clip:\(binding)"),
                    name: binding, sampleRate: 30, wrapMode: wrap, lastKeyTime: duration, curves: curves)
            }
            let entrance = clip("_animationIn", 1, 0, [try curve("m_LocalPosition.z", [5, -10]),
                try curve("m_Alpha", [0, 1]), try curve("m_Color.a", [0.2, 0.7]), try curve("missing", [0], nodes: [])])
            let ambient = clip("_animationLoop", 2, 2, [try curve("material._Alpha", [0.4, 0.9])])
            let exit = clip("_animationOut", 0.25, 0, [try curve("m_Alpha", [1, 0], interval: 0.25)])
            let animation = try HUDSourceWatchAnimation(scene: scene, library: HUDSourceAnimationLibrary(clips: [entrance, ambient, exit]))
            let pose = try animation.pose(entranceTime: 1, ambientTime: 2.5, exitTime: nil, canvasResolution: SIMD2(2400, 1350))
            let resolved = try scene.resolve(overrides: pose.transforms)
            check(resolved[rootID]!.rect!.size == SIMD2(2400, 1350), "Runtime root initialization uses the explicit canvas resolution")
            check(resolved[childID]!.worldMatrix.columns.3.x == 3 && resolved[childID]!.worldMatrix.columns.3.y == 4,
                  "An animated Z channel retains source X/Y")
            check(resolved[childID]!.worldMatrix.columns.3.z == -10, "Entrance final source pose survives ambient sampling")
            check(pose.value("m_Alpha", on: childID, fallback: -1) == 1, "CanvasGroup alpha is sampled separately")
            check(pose.value("m_Color.a", on: childID, fallback: -1) == 0.7, "Graphic alpha is not folded into CanvasGroup alpha")
            check(abs(pose.value("material._Alpha", on: childID, fallback: -1) - 0.65) < 1e-12,
                  "Material alpha samples the loop independently")
            check(pose.unboundPaths == ["Child"], "Source residual bindings are reported rather than mapped to another node")

            let playback = HUDSourceWatchPlayback(animation: animation)
            let resolution = SIMD2<Double>(2400, 1350)
            var firstOpen = 0, close = 0, secondOpen = 0
            playback.open(at: 10, reduceMotion: false) { firstOpen += 1 }
            check(playback.phase == .opening, "Opening has its own source phase")
            _ = try playback.sample(at: 10.5, canvasResolution: resolution, reduceMotion: false)
            check(firstOpen == 0, "Source opening completion does not fire early")
            playback.close(at: 10.5, reduceMotion: false) { close += 1 }
            playback.open(at: 10.6, reduceMotion: false) { secondOpen += 1 }
            _ = try playback.sample(at: 11.61, canvasResolution: resolution, reduceMotion: false)
            check(firstOpen == 0 && close == 0 && secondOpen == 1, "Interrupted lifecycle completions cannot run later")
            _ = try playback.sample(at: 12, canvasResolution: resolution, reduceMotion: false)
            check(secondOpen == 1, "A completed entrance calls its handler exactly once")
            playback.close(at: 12, reduceMotion: false) { close += 1 }
            let beforeEndpoint = try playback.sample(at: 12.24, canvasResolution: resolution, reduceMotion: false)
            check(beforeEndpoint != nil,
                  "Closing remains visible through its exact source duration")
            let concealed = try playback.sample(at: 12.25, canvasResolution: resolution, reduceMotion: false)
            check(concealed == nil && close == 1 && playback.phase == .concealed, "Closing finishes at the source endpoint")
            playback.open(at: 20, reduceMotion: false) { secondOpen += 1 }
            let reduced = try playback.sample(at: 20.1, canvasResolution: resolution, reduceMotion: true)
            check(playback.phase == .visible && secondOpen == 2 && reduced?.value("m_Alpha", on: childID, fallback: -1) == 1,
                  "Enabling Reduce Motion seeks the source final pose and releases input")
            check(reduced?.properties[childID]?["material._Alpha"] == nil, "Reduce Motion stops ambient motion")
            playback.close(at: 21, reduceMotion: false) { close += 1 }
            let reducedClose = try playback.sample(at: 21.01, canvasResolution: resolution, reduceMotion: true)
            check(reducedClose == nil,
                  "Reduce Motion immediately completes an already closing menu")
            check(close == 2, "Reduce Motion completes a closing handler once")
            playback.open(at: 30, reduceMotion: false) { firstOpen += 1 }
            playback.conceal()
            _ = try playback.sample(at: 40, canvasResolution: resolution, reduceMotion: false)
            check(firstOpen == 0 && playback.phase == .concealed, "Concealment cancels pending entrance work")
        } catch { fatalError("Source playback fixture failed: \(error)") }
        return count
    }
}
