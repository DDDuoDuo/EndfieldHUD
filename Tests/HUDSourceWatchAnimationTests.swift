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
            func curve(_ property: String, _ values: [Double], nodes: [HUDSourceID]? = nil, interval: Double = 1,
                       classID: Int? = nil) throws -> HUDSourceAnimationCurve {
                let keys = values.enumerated().map { index, value in HUDSourceAnimationKey(time: Double(index) * interval,
                    value: .scalar(value), inSlope: .scalar(0), outSlope: .scalar(0), weightedMode: 0,
                    inWeight: .scalar(1.0 / 3), outWeight: .scalar(1.0 / 3)) }
                return try HUDSourceAnimationCurve(group: "m_FloatCurves", path: "Child", attribute: property,
                    nodeIDs: nodes ?? [childID], body: HUDSourceAnimationCurve.Body(keys: keys, preInfinity: 2, postInfinity: 2), classID: classID)
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

            let groupOne = HUDSourceID(rawValue: "CAB-fixture:101")
            let groupTwo = HUDSourceID(rawValue: "CAB-fixture:102")
            func group(_ id: HUDSourceID, _ name: String, _ anchored: SIMD2<Double>) -> HUDSourceNode {
                HUDSourceNode(id: id, path: "Watch/" + name, name: name, parentID: rootID, childIDs: [],
                    transform: HUDSourceTransform(kind: .rectTransform, localPosition: HUDSourceVector3(0, 0, 5),
                        rect: HUDSourceRectTransform(anchorMin: HUDSourceVector2(0, 1), anchorMax: HUDSourceVector2(0, 1),
                            anchoredPosition: HUDSourceVector2(anchored.x, anchored.y), sizeDelta: HUDSourceVector2(286, 143),
                            pivot: HUDSourceVector2(0.5, 0.5))))
            }
            let groupsScene = try HUDSourceScene(rootID: rootID, nodes: [
                HUDSourceNode(id: rootID, path: "Watch", name: "Watch", parentID: nil, childIDs: [groupOne, groupTwo],
                    transform: HUDSourceTransform(kind: .rectTransform, localPosition: HUDSourceVector3(0, 0, 0), rect: rect)),
                group(groupOne, "Group1", SIMD2(214, 2)), group(groupTwo, "Group2", SIMD2(256, -150))])
            var groupCurves = [HUDSourceAnimationCurve]()
            // Original watch_in01 Group1/2 inherited scalar endpoints. The
            // installed Legacy TypeTree fallback writes these native fields;
            // RectTransform finalization derives visible XY from anchors,
            // without needing a parent layout component to restore rows.
            let originalX = -26.344890594482422, originalY = 0.564835786819458
            func originalGroupCurve(_ id: HUDSourceID, _ name: String, _ property: String,
                                    firstTime: Double, firstValue: Double, lastValue: Double) throws -> HUDSourceAnimationCurve {
                let keys = [(firstTime, firstValue), (0.75, lastValue)].map { time, value in
                    HUDSourceAnimationKey(time: time, value: .scalar(value), inSlope: .scalar(0), outSlope: .scalar(0),
                        weightedMode: 0, inWeight: .scalar(0.3333333432674408), outWeight: .scalar(0.3333333432674408))
                }
                return try HUDSourceAnimationCurve(group: "m_FloatCurves",
                    path: "Content/WatchNode/canvas_watch/RightBottomNode/ScrollView/Viewport/ScrollViewContent/RightBotton/" + name,
                    attribute: property, nodeIDs: [id],
                    body: HUDSourceAnimationCurve.Body(keys: keys, preInfinity: 2, postInfinity: 2), classID: 224)
            }
            for (id, name, firstTime) in [(groupOne, "Group1", 0.5333333611488342), (groupTwo, "Group2", 0.550000011920929)] {
                groupCurves += [try originalGroupCurve(id, name, "m_LocalPosition.x", firstTime: firstTime,
                                    firstValue: -26.344892501831055, lastValue: originalX),
                    try originalGroupCurve(id, name, "m_LocalPosition.y", firstTime: firstTime,
                                    firstValue: originalY, lastValue: originalY),
                    try curve("m_LocalPosition.z", [50, 0], nodes: [id], classID: 224)]
            }
            let groupsAnimation = try HUDSourceWatchAnimation(scene: groupsScene, library: HUDSourceAnimationLibrary(clips: [
                clip("_animationIn", 1, 0, groupCurves), clip("_animationLoop", 1, 2, []), clip("_animationOut", 1, 0, [])]))
            var groupsPose = try groupsAnimation.pose(entranceTime: 1, ambientTime: nil, exitTime: nil, canvasResolution: SIMD2(1920, 1080))
            let groupsResolved = try groupsScene.resolve(overrides: groupsPose.transforms)
            let firstPosition = groupsResolved[groupOne]!.worldMatrix.columns.3
            let secondPosition = groupsResolved[groupTwo]!.worldMatrix.columns.3
            check(groupsPose.value("m_LocalPosition.x", on: groupOne, fallback: 0) == originalX &&
                  groupsPose.value("m_LocalPosition.y", on: groupOne, fallback: 0) == originalY &&
                  groupsPose.value("m_LocalPosition.x", on: groupTwo, fallback: 0) == originalX &&
                  groupsPose.value("m_LocalPosition.y", on: groupTwo, fallback: 0) == originalY,
                  "Original class224 inherited XY endpoints remain accepted serialized property values")
            // Independent source-anchor calculation, with no layout pass:
            // parent top-left (-960,540) + original anchoredPosition XY.
            check(firstPosition.x == -746 && secondPosition.x == -704 &&
                  firstPosition.y == 542 && secondPosition.y == 390,
                  "Native RectTransform finalization preserves distinct anchored XY without a parent row layout")
            check(firstPosition.z == 0 && secondPosition.z == 0, "Registered RectTransform local Z still reaches its endpoint")
            check(groupsPose.unregisteredBindings.isEmpty, "Verified inherited bindings have no false rejection diagnostics")
            let rowLayout = HUDSourceWatchLayout(scene: groupsScene, components: [rootID: [
                HUDSourceWatchComponent(id: HUDSourceID(rawValue: "CAB-fixture:200"), type: "MonoBehaviour",
                    script: "VerticalLayoutGroup", data: ["m_Enabled": .number(1), "m_ChildAlignment": .number(0),
                        "m_Spacing": .number(9), "m_ChildControlWidth": .bool(false), "m_ChildControlHeight": .bool(false),
                        "m_ChildForceExpandWidth": .bool(false), "m_ChildForceExpandHeight": .bool(false)])]])
            groupsAnimation.apply(clip("Depth", 1, 0, [try curve("m_LocalPosition.z", [-5], nodes: [groupOne], classID: 224)]),
                time: 0, to: &groupsPose, base: groupsResolved)
            _ = try rowLayout.apply(to: &groupsPose)
            let laidOut = try groupsScene.resolve(overrides: groupsPose.transforms)
            let rowOne = laidOut[groupOne]!.localMatrix.columns.3, rowTwo = laidOut[groupTwo]!.localMatrix.columns.3
            // Independent top-left uGUI calculation: -1920/2 + 286/2;
            // 1080/2 - 143/2, then subtract one 143+9 row stride.
            check(rowOne.x == -817 && rowTwo.x == -817 && rowOne.y == 468.5 && rowTwo.y == 316.5,
                  "Layout after inherited animation restores separated source-size rows on both driven axes")
            check(rowOne.z == -5 && rowTwo.z == 0 && laidOut[groupOne]!.rect!.size == SIMD2(286, 143),
                  "Driven XY layout keeps animated depth and the original tile rectangle")
            // Adapter compatibility check only: a nonuniform synthetic scale
            // distinguishes all three components in our geometry evaluator.
            // Native inherited-scale finalization is not yet established;
            // the original main wrapper has no class224 scalar scale tracks.
            groupsAnimation.apply(clip("InheritedScale", 1, 0, [
                try curve("m_LocalScale.x", [1, 2], nodes: [groupOne], classID: 224),
                try curve("m_LocalScale.y", [1, 3], nodes: [groupOne], classID: 224),
                try curve("m_LocalScale.z", [1, 4], nodes: [groupOne], classID: 224)]),
                time: 1, to: &groupsPose, base: laidOut)
            let scaledNode = try groupsScene.resolve(overrides: groupsPose.transforms)[groupOne]!
            let scaledCorner = simd_mul(scaledNode.worldMatrix, SIMD4<Double>(143, 71.5, 2, 1))
            check(scaledCorner == SIMD4<Double>(-531, 683, 3, 1) && groupsPose.unregisteredBindings.isEmpty,
                  "Adapter scalar XYZ scale endpoints keep nonuniform geometry and their original classID")
            groupsAnimation.apply(clip("UnknownRectProperty", 1, 0, [
                try curve("m_UnknownFixtureProperty", [1], nodes: [groupTwo], classID: 224)]),
                time: 0, to: &groupsPose, base: laidOut)
            check(groupsPose.unregisteredBindings == ["224:m_UnknownFixtureProperty"],
                  "Unknown RectTransform properties remain bounded diagnostics")
            let transformTracks = clip("Animator.state.Highlighted", 1, 0, [
                try curve("m_LocalPosition.x", [12], nodes: [groupOne], classID: 4),
                try curve("m_LocalPosition.y", [-13], nodes: [groupOne], classID: 4)])
            groupsAnimation.apply(transformTracks, time: 0, to: &groupsPose, base: groupsResolved)
            let transformed = try groupsScene.resolve(overrides: groupsPose.transforms)
            check(transformed[groupOne]!.worldMatrix.columns.3.x == 12 && transformed[groupOne]!.worldMatrix.columns.3.y == -13,
                  "Packed Transform class 4 hover X/Y bindings remain effective on RectTransform targets")
            let vectorScale = try HUDSourceAnimationCurve(group: "m_ScaleCurves", path: "Group1", attribute: "m_ScaleCurves",
                nodeIDs: [groupOne], body: HUDSourceAnimationCurve.Body(keys: [
                    HUDSourceAnimationKey(time: 0, value: .vector3(HUDSourceVector3(2, 3, 4)),
                        inSlope: .vector3(HUDSourceVector3(0, 0, 0)), outSlope: .vector3(HUDSourceVector3(0, 0, 0)),
                        weightedMode: 0, inWeight: .vector3(HUDSourceVector3(0, 0, 0)),
                        outWeight: .vector3(HUDSourceVector3(0, 0, 0)))], preInfinity: 2, postInfinity: 2))
            groupsAnimation.apply(clip("VectorScale", 1, 0, [vectorScale]), time: 0, to: &groupsPose, base: groupsResolved)
            check(groupsPose.transforms[groupOne]?.localScale?.simd == SIMD3(2, 3, 4),
                  "Original vector scale tracks remain effective independently of the RectTransform scalar registry")
            var anchoredPose = HUDSourceWatchPose(transforms: [:])
            groupsAnimation.apply(clip("Anchored", 1, 0, [
                try curve("m_AnchoredPosition.x", [12], nodes: [groupOne], classID: 224),
                try curve("m_AnchoredPosition.y", [-13], nodes: [groupOne], classID: 224)]),
                time: 0, to: &anchoredPose, base: groupsResolved)
            check(anchoredPose.transforms[groupOne]?.anchoredPosition3D?.x == 12 &&
                  anchoredPose.transforms[groupOne]?.anchoredPosition3D?.y == -13,
                  "Registered RectTransform anchored X/Y continue to animate")
            let encodedClass = try JSONEncoder().encode(groupCurves[0])
            let decodedClass = try HUDSourceJSON.decoder().decode(HUDSourceAnimationCurve.self, from: encodedClass)
            check(decodedClass.classID == 224,
                  "Source class ID survives curve encoding and decoding")
            var originalRaw = try JSONSerialization.jsonObject(with: encodedClass) as! [String: Any]
            originalRaw.removeValue(forKey: "class_id")
            var originalBody = originalRaw["raw"] as! [String: Any]
            originalBody["classID"] = 224; originalRaw["raw"] = originalBody
            let originalData = try JSONSerialization.data(withJSONObject: originalRaw)
            let originalCurve = try HUDSourceJSON.decoder().decode(HUDSourceAnimationCurve.self, from: originalData)
            check(originalCurve.classID == 224,
                  "Direct legacy raw classID remains supported without an expanded class_id")

            let playback = HUDSourceWatchPlayback(animation: animation)
            check(abs(HUDSourceWatchPlayback.clipTime(elapsed: 0.375, length: 0.75) - 0.5625) < 1e-12,
                  "Finite wrapper OutQuad maps half wall time to three-quarter source clip time")
            check(HUDSourceWatchPlayback.clipTime(elapsed: -1, length: 0.75) == 0 && HUDSourceWatchPlayback.clipTime(elapsed: 10, length: 0.75) == 0.75,
                  "Finite clip easing clamps before and after its source endpoints")
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

            // Independent four-second tracks distinguish source Hermite from
            // the main menu's OutQuad, and linear blur exit from ease-in/out.
            func blurCurve(start: Double, end: Double, slope: Double) throws -> HUDSourceAnimationCurve {
                let keys = [0.0, 4.0].enumerated().map { index, time in
                    HUDSourceAnimationKey(time: time, value: .scalar(index == 0 ? start : end),
                        inSlope: .scalar(slope), outSlope: .scalar(slope), weightedMode: 0,
                        inWeight: .scalar(1.0 / 3), outWeight: .scalar(1.0 / 3))
                }
                return try HUDSourceAnimationCurve(group: "m_FloatCurves", path: "BlurBG", attribute: "m_Alpha",
                    nodeIDs: [], body: HUDSourceAnimationCurve.Body(keys: keys, preInfinity: 2, postInfinity: 2))
            }
            let blurIn = try HUDSourceWatchBlurAnimation.Track(curve: blurCurve(start: 0, end: 1, slope: 0))
            check(blurIn.alpha(at: 1) == 0.15625, "Blur entrance samples original smoothstep at absolute clip seconds")
            check(blurIn.alpha(at: 3) == 0.84375, "Blur alpha is independent of the main OutQuad wrapper")
            check(blurIn.controlPoints.y == 0 && blurIn.controlPoints.w == 1,
                  "Core Animation receives the original Hermite Bezier controls")
            let blurOut = try HUDSourceWatchBlurAnimation.Track(curve: blurCurve(start: 1, end: 0, slope: -0.25))
            check(blurOut.alpha(at: 1) == 0.75 && blurOut.alpha(at: 3) == 0.25,
                  "Original blur exit remains linear")
            check(abs(blurOut.controlPoints.y - 1 / 3) < 1e-6 && abs(blurOut.controlPoints.w - 2 / 3) < 1e-6,
                  "Exit tangents retain the original normalized derivative")
            check(blurOut.alpha(at: 8) == 0, "Finished blur alpha holds its endpoint")
        } catch { fatalError("Source playback fixture failed: \(error)") }
        return count
    }
}
