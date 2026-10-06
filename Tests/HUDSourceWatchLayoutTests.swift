import Foundation
import simd

enum HUDSourceWatchLayoutTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            count += 1; if !condition() { fatalError(message) }
        }
        func near(_ a: Double, _ b: Double) -> Bool { abs(a - b) < 1e-8 }
        func id(_ value: Int) -> HUDSourceID { HUDSourceID(rawValue: "CAB-layout:\(value)") }
        func component(_ kind: String, _ number: Int, _ fields: [String: HUDSourceJSONValue]) -> HUDSourceWatchComponent {
            HUDSourceWatchComponent(id: id(number), type: "MonoBehaviour", script: kind,
                data: fields.merging(["m_Enabled": .number(1)]) { a, _ in a })
        }
        func node(_ number: Int, parent: Int?, children: [Int] = [], size: SIMD2<Double>,
                  anchored: SIMD2<Double> = .zero, pivot: SIMD2<Double> = SIMD2(0.5, 0.5),
                  anchors: SIMD2<Double> = SIMD2(0.5, 0.5), active: Bool = true,
                  rotation: HUDSourceQuaternion = .identity, scale: HUDSourceVector3 = HUDSourceVector3(1, 1, 1)) -> HUDSourceNode {
            HUDSourceNode(id: id(number), path: "Fixture/\(number)", name: "\(number)", parentID: parent.map(id),
                childIDs: children.map(id), active: active, transform: HUDSourceTransform(kind: .rectTransform,
                    localPosition: HUDSourceVector3(0, 0, 0), localRotation: rotation, localScale: scale,
                    rect: HUDSourceRectTransform(anchorMin: HUDSourceVector2(anchors.x, anchors.y), anchorMax: HUDSourceVector2(anchors.x, anchors.y),
                        anchoredPosition: HUDSourceVector2(anchored.x, anchored.y), sizeDelta: HUDSourceVector2(size.x, size.y), pivot: HUDSourceVector2(pivot.x, pivot.y))))
        }
        func group(_ control: Bool, force: Bool, scale: Bool = false, vertical: Bool = false) -> HUDSourceWatchComponent {
            component(vertical ? "VerticalLayoutGroup" : "HorizontalLayoutGroup", 100, [
                "m_ChildAlignment": .number(4), "m_Spacing": .number(0),
                "m_ChildControlWidth": .bool(control), "m_ChildControlHeight": .bool(control),
                "m_ChildForceExpandWidth": .bool(force), "m_ChildForceExpandHeight": .bool(force),
                "m_ChildScaleWidth": .bool(scale), "m_ChildScaleHeight": .bool(scale)])
        }
        do {
            let scene = try HUDSourceScene(rootID: id(1), nodes: [
                node(1, parent: nil, children: [2, 3, 4, 5], size: SIMD2(300, 100)),
                node(2, parent: 1, size: SIMD2(100, 80)), node(3, parent: 1, size: SIMD2(100, 80)),
                node(4, parent: 1, size: SIMD2(999, 999), active: false), node(5, parent: 1, size: SIMD2(999, 999))])
            let ignore = component("LayoutElement", 105, ["m_IgnoreLayout": .bool(true)])
            let layout = HUDSourceWatchLayout(scene: scene, components: [id(1): [group(false, force: true)], id(5): [ignore]])
            var override = HUDSourceTransformOverride(); override.localPosition = HUDSourceVector3(99, 88, 17); override.positionComponents[2] = -5
            var pose = HUDSourceWatchPose(transforms: [id(2): override])
            _ = try layout.apply(to: &pose)
            let result = try scene.resolve(overrides: pose.transforms)
            check(near(result[id(2)]!.localMatrix.columns.3.x, -75) && near(result[id(3)]!.localMatrix.columns.3.x, 75),
                  "Force-expand slots center two source-width tiles while ignoring inactive and ignored children")
            check(near(result[id(2)]!.localMatrix.columns.3.y, 0), "Cross-axis centering accounts for uncontrolled tile height")
            check(near(result[id(2)]!.localMatrix.columns.3.z, -5), "Layout overrides driven X/Y while preserving the independent hover Z")
            check(pose.transforms[id(2)]!.anchorMin == HUDSourceVector2(0, 1), "Group writes the source top-left anchors")
            check(result[id(2)]!.rect!.size == SIMD2(100, 80), "Uncontrolled dimensions retain the source sizeDelta")

            func element(_ number: Int, minimum: Double, preferred: Double, flexible: Double) -> HUDSourceWatchComponent {
                component("LayoutElement", number, ["m_LayoutPriority": .number(1), "m_MinWidth": .number(minimum),
                    "m_PreferredWidth": .number(preferred), "m_FlexibleWidth": .number(flexible),
                    "m_MinHeight": .number(40), "m_PreferredHeight": .number(40), "m_FlexibleHeight": .number(0)])
            }
            let controlled = HUDSourceWatchLayout(scene: scene, components: [id(1): [group(true, force: false)],
                id(2): [element(102, minimum: 30, preferred: 70, flexible: 1)],
                id(3): [element(103, minimum: 10, preferred: 30, flexible: 3)], id(5): [ignore]])
            var root = HUDSourceTransformOverride(); root.sizeDelta = HUDSourceVector2(70, 100)
            var small = HUDSourceWatchPose(transforms: [id(1): root]); _ = try controlled.apply(to: &small)
            let constrained = try scene.resolve(overrides: small.transforms)
            check(near(constrained[id(2)]!.rect!.size.x, 50) && near(constrained[id(3)]!.rect!.size.x, 20),
                  "Available width between min and preferred interpolates the actual child requirements")
            root.sizeDelta = HUDSourceVector2(200, 100)
            var wide = HUDSourceWatchPose(transforms: [id(1): root]); _ = try controlled.apply(to: &wide)
            let expanded = try scene.resolve(overrides: wide.transforms)
            check(near(expanded[id(2)]!.rect!.size.x, 95) && near(expanded[id(3)]!.rect!.size.x, 105),
                  "Surplus uses per-child flexible weights, not equal expansion")

            var scaled = HUDSourceWatchPose(transforms: [id(2): HUDSourceTransformOverride(localScale: HUDSourceVector3(2, 1, 1))])
            let scaleLayout = HUDSourceWatchLayout(scene: scene, components: [id(1): [group(false, force: false, scale: true)], id(5): [ignore]])
            _ = try scaleLayout.apply(to: &scaled)
            let scales = try scene.resolve(overrides: scaled.transforms)
            check(near(scales[id(2)]!.localMatrix.columns.3.x, -50) && near(scales[id(3)]!.localMatrix.columns.3.x, 100),
                  "Source childScaleWidth includes scale in cell extent and pivot position")

            let fitter = component("ContentSizeFitter", 110, ["m_HorizontalFit": .number(2), "m_VerticalFit": .number(0)])
            let fittedLayout = HUDSourceWatchLayout(scene: scene, components: [id(1): [group(false, force: false), fitter], id(5): [ignore]])
            var fitted = HUDSourceWatchPose(transforms: [:]); _ = try fittedLayout.apply(to: &fitted)
            let fittedResult = try scene.resolve(overrides: fitted.transforms)
            check(fittedResult[id(1)]!.rect!.size.x == 200,
                  "ContentSizeFitter uses measured preferred width before child control")

            // Immutable candidates and within-call rect memoization must
            // preserve the uncached horizontal-before-vertical writer order.
            // Reuse the same layout across different inputs to ensure sampled
            // rectangles never leak into a subsequent animation frame.
            for candidate in [layout, controlled, scaleLayout, fittedLayout] {
                for width in [70.0, 200.0, 377.5] {
                    var rootInput = HUDSourceTransformOverride()
                    rootInput.sizeDelta = HUDSourceVector2(width, 135)
                    var child = HUDSourceTransformOverride(localScale: HUDSourceVector3(1.7, 0.8, 1))
                    child.pivot = HUDSourceVector2(0.2, 0.9)
                    child.localPosition = HUDSourceVector3(40, -20, 7)
                    var input = HUDSourceWatchPose(transforms: [id(1): rootInput, id(2): child])
                    if width == 200 { input.transforms[id(3)] = HUDSourceTransformOverride(active: false) }
                    var cached = input, forced = input
                    let cachedReport = try candidate.apply(to: &cached)
                    let forcedReport = try candidate.apply(to: &forced, forceSlantRebuild: true)
                    check(cached.transforms == forced.transforms && cached.properties == forced.properties,
                        "Cached layout metadata and rect values must preserve every authored transform channel")
                    check(cachedReport.missingTextMetrics == forcedReport.missingTextMetrics
                        && cachedReport.unverifiedCustomComponents == forcedReport.unverifiedCustomComponents,
                        "Cached layout metadata must preserve diagnostics")
                }
            }

            let image = component("UIImage", 111, ["m_Type": .number(1)])
            let scaler = component("CanvasScaler", 112, ["m_ReferencePixelsPerUnit": .number(200)])
            let sprite: HUDSourceJSONValue = .object(["raw_sprite": .object([
                "m_PixelsToUnits": .number(100), "m_Rect": .object(["width": .number(128), "height": .number(64)]),
                "m_Border": .object(["x": .number(3), "y": .number(5), "z": .number(7), "w": .number(11)])])])
            let imageLayout = HUDSourceWatchLayout(scene: scene, components: [id(1): [group(true, force: false), fitter, scaler],
                id(2): [image, element(113, minimum: 8, preferred: 12, flexible: 0)], id(5): [ignore]],
                spriteByComponent: [image.id: sprite])
            var cachedImage = HUDSourceWatchPose(transforms: [:]), forcedImage = cachedImage
            _ = try imageLayout.apply(to: &cachedImage)
            _ = try imageLayout.apply(to: &forcedImage, forceSlantRebuild: true)
            check(cachedImage.transforms == forcedImage.transforms,
                "Image borders, inherited Canvas pixels-per-unit and LayoutElement priority retain exact metric order")

            // A node with a fitter but no ILayoutElement still measures zero;
            // sparse fixed metrics must not preserve its previous dimensions.
            let blankFit = HUDSourceWatchLayout(scene: scene, components: [id(2): [fitter]])
            for enabled in [true, false, true] {
                var input = HUDSourceWatchPose(transforms: [id(2): HUDSourceTransformOverride(active: enabled)])
                var oracle = input
                _ = try blankFit.apply(to: &input)
                _ = try blankFit.apply(to: &oracle, forceSlantRebuild: true)
                check(input.transforms == oracle.transforms,
                    "A metric-free fitter preserves zero sizing and activation changes")
            }
            let dynamicText = component("UIText", 114, [:])
            var preferred = SIMD2<Double>(72, 18)
            let textLayout = HUDSourceWatchLayout(scene: scene,
                components: [id(1): [group(true, force: false), fitter], id(2): [dynamicText], id(5): [ignore]],
                intrinsicSize: { _, _ in preferred })
            for width in [72.0, 119, 0, 72] {
                preferred.x = width
                var actual = HUDSourceWatchPose(transforms: [:]), oracle = actual
                let report = try textLayout.apply(to: &actual)
                let reference = try textLayout.apply(to: &oracle, forceSlantRebuild: true)
                check(actual.transforms == oracle.transforms && report.missingTextMetrics == reference.missingTextMetrics,
                    "Retained layout stages always remeasure changing text before fitter and group writers")
            }

            let scrollScene = try HUDSourceScene(rootID: id(1), nodes: [node(1, parent: nil, children: [2], size: SIMD2(100, 100)),
                node(2, parent: 1, size: SIMD2(100, 300), pivot: SIMD2(0.5, 1), anchors: SIMD2(0.5, 1))])
            func pointer(_ number: Int) -> HUDSourceJSONValue { .object(["target_id": .string(id(number).rawValue)]) }
            let scroll = component("UIScrollRect", 120, ["m_Vertical": .bool(true), "m_Content": pointer(2),
                "m_Viewport": pointer(1), "m_ScrollSensitivity": .number(45)])
            let scrollLayout = HUDSourceWatchLayout(scene: scrollScene, components: [id(1): [scroll]])
            var top = HUDSourceWatchPose(transforms: [:]); let topReport = try scrollLayout.apply(to: &top)
            check(topReport.scroll?.hiddenLength == 200 && !top.transforms.keys.contains(id(2)), "Normalized 1 preserves source top alignment")
            var bottom = HUDSourceWatchPose(transforms: [:]); _ = try scrollLayout.apply(to: &bottom, verticalNormalizedPosition: 0)
            check(near(bottom.transforms[id(2)]!.anchoredPosition3D!.y, 200), "Normalized 0 aligns the content bottom to the view bottom")
            check(near(scrollLayout.scrolledPosition(0.5, delta: 1, info: topReport.scroll), 0.725),
                  "Wheel scroll uses source sensitivity and measured overflow")

            // The scoped scroll resolver must include every ancestor, observe
            // prior scroll writes, and ignore only unrelated scene branches.
            let decorations = Array(100..<132)
            let nestedScene = try HUDSourceScene(rootID: id(1), nodes: [
                node(1, parent: nil, children: [2] + decorations, size: SIMD2(800, 600)),
                node(2, parent: 1, children: [3], size: SIMD2(220, 150), anchored: SIMD2(65, -40)),
                node(3, parent: 2, children: [4], size: SIMD2(220, 600), pivot: SIMD2(0.5, 1), anchors: SIMD2(0.5, 1)),
                node(4, parent: 3, children: [5], size: SIMD2(160, 110), anchored: SIMD2(12, -170)),
                node(5, parent: 4, children: [6], size: SIMD2(160, 390), pivot: SIMD2(0.5, 1), anchors: SIMD2(0.5, 1)),
                node(6, parent: 5, size: SIMD2(45, 35))
            ] + decorations.map { node($0, parent: 1, size: SIMD2(Double($0), 50)) })
            let parentScroll = component("UIScrollRect", 121, ["m_Vertical": .bool(true), "m_Content": pointer(3),
                "m_Viewport": pointer(2), "m_ScrollSensitivity": .number(35)])
            let childScroll = component("ScrollRect", 122, ["m_Vertical": .bool(true), "m_Content": pointer(5),
                "m_Viewport": pointer(4), "m_ScrollSensitivity": .number(25)])
            let nestedLayout = HUDSourceWatchLayout(scene: nestedScene, components: [id(2): [parentScroll], id(4): [childScroll]])
            check(nestedLayout.scrollResolutionNodeCount(for: id(2)) == 3
                  && nestedLayout.scrollResolutionNodeCount(for: id(4)) == 5,
                  "Each scroll resolves only its ancestor closure rather than unrelated decorations or content descendants")
            for variant in 0..<6 {
                let turn = simd_quatd(angle: Double(variant) * 0.07, axis: SIMD3(1, 0, 0))
                let rotation = HUDSourceQuaternion(turn.imag.x, turn.imag.y, turn.imag.z, turn.real)
                var input = HUDSourceWatchPose(transforms: [
                    id(1): HUDSourceTransformOverride(localRotation: rotation, localScale: HUDSourceVector3(1.2, 0.8, 1)),
                    id(2): HUDSourceTransformOverride(sizeDelta: HUDSourceVector2(220, 130 + Double(variant) * 9)),
                    id(3): HUDSourceTransformOverride(positionComponents: [2: Double(variant) * -4]),
                    id(4): HUDSourceTransformOverride(localScale: HUDSourceVector3(1, variant == 5 ? 0 : 0.9, 1)),
                    id(5): HUDSourceTransformOverride(sizeDelta: HUDSourceVector2(160, 290 + Double(variant) * 25))
                ])
                if variant == 3 { input.transforms[id(3)]?.active = false }
                if variant == 4 { input.transforms[id(2)]?.active = false }
                for position in [-0.3, 0, 0.37, 0.82, 1, 1.3] {
                    var scoped = input, forced = input
                    let actual = try nestedLayout.apply(to: &scoped, verticalNormalizedPosition: position)
                    let oracle = try nestedLayout.apply(to: &forced, verticalNormalizedPosition: position, forceSlantRebuild: true)
                    check(scoped.transforms == forced.transforms && scoped.properties == forced.properties,
                          "Scoped scroll resolution preserves nested writer order, depth, rotation, scale and active overrides")
                    check(actual.scroll?.nodeID == oracle.scroll?.nodeID
                          && actual.scroll?.hiddenLength.bitPattern == oracle.scroll?.hiddenLength.bitPattern
                          && actual.scroll?.normalizedPosition.bitPattern == oracle.scroll?.normalizedPosition.bitPattern
                          && actual.unverifiedCustomComponents == oracle.unverifiedCustomComponents,
                          "Scoped scroll bounds and diagnostics exactly match the independent full-scene oracle")
                }
            }

            let rotation = simd_quatd(angle: 0.35, axis: SIMD3(1, 0, 0)) * simd_quatd(angle: 0.17, axis: SIMD3(0, 0, 1))
            let q = HUDSourceQuaternion(rotation.imag.x, rotation.imag.y, rotation.imag.z, rotation.real)
            let slantScene = try HUDSourceScene(rootID: id(1), nodes: [node(1, parent: nil, children: [2], size: SIMD2(500, 500),
                rotation: q, scale: HUDSourceVector3(1.3, 0.8, 1)), node(2, parent: 1, size: SIMD2(100, 80), anchored: SIMD2(100, 50))])
            func key(_ time: Double, _ value: Double) -> HUDSourceJSONValue {
                .object(["time": .number(time), "value": .number(value), "inSlope": .number(1), "outSlope": .number(1),
                    "weightedMode": .number(0), "inWeight": .number(0), "outWeight": .number(0)])
            }
            let effect = component("UIScrollCellSlantEffect", 130, ["_topY": .number(100), "_bottomY": .number(-100),
                "_leftX": .number(-50), "_maxWidth": .number(100), "_cells": .array([pointer(2)]),
                "_curve": .object(["m_Curve": .array([key(0, 0), key(1, 1)])])])
            let slantLayout = HUDSourceWatchLayout(scene: slantScene, components: [id(1): [effect]])
            var before = HUDSourceTransformOverride(); before.positionComponents = [0: 150, 1: 75, 2: -5]
            var slanted = HUDSourceWatchPose(transforms: [id(2): before])
            let original = try slantScene.resolve(overrides: slanted.transforms), originalPosition = original[id(2)]!.worldMatrix.columns.3
            let desiredX = (original[id(1)]!.worldMatrix * SIMD4<Double>(37.5, 0, 0, 1)).x
            _ = try slantLayout.apply(to: &slanted)
            let actual = try slantScene.resolve(overrides: slanted.transforms)[id(2)]!.worldMatrix.columns.3
            check(near(actual.x, desiredX), "Source slant samples component-local height and transforms the desired X back to world")
            check(near(actual.y, originalPosition.y) && near(actual.z, originalPosition.z),
                  "Static native Tick changes only world X under tilted and scaled parents")
            let external = simd_mul(HUDSourceGeometry.translation(SIMD3(0, 100, 30)),
                simd_double4x4(simd_quatd(angle: 0.12, axis: SIMD3(0, 1, 0))))
            var withGyro = HUDSourceWatchPose(transforms: [id(2): before])
            let originalGlobal = simd_mul(external, original[id(2)]!.worldMatrix).columns.3
            let desiredGlobalX = simd_mul(simd_mul(external, original[id(1)]!.worldMatrix), SIMD4<Double>(37.5, 0, 0, 1)).x
            _ = try slantLayout.apply(to: &withGyro, worldRoot: external)
            let global = simd_mul(external, try slantScene.resolve(overrides: withGyro.transforms)[id(2)]!.worldMatrix).columns.3
            check(near(global.x, desiredGlobalX) && near(global.y, originalGlobal.y) && near(global.z, originalGlobal.z),
                  "WorldUIRoot gyro is included when Tick replaces global X, preserving global Y/Z")
            let slantSample = try HUDSourceWatchLayout.slantCurve(effect).sample(at: 0.75)
            check(slantSample == 0.75, "Slant curve reuses exact Hermite sampling")
        } catch { fatalError("Source Watch layout fixture failed: \(error)") }
        return count
    }
}
