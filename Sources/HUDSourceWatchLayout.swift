import Foundation
import simd

/// Layout writers operate on Unity rect coordinates before camera projection.
/// Group measurement is bottom-up; control is top-down, horizontal before
/// vertical. Animation is sampled first; driven X/Y are rewritten while the
/// independent hover/depth Z channel survives. Custom game components are
/// reported separately from the public uGUI layout algorithm.
struct HUDSourceWatchLayout {
    typealias IntrinsicSize = (HUDSourceID, HUDSourceRect?) -> SIMD2<Double>?
    typealias SlantMapping = (HUDSourceWatchComponent, HUDSourceID,
                             [HUDSourceID: HUDSourceResolvedNode]) -> Double?
    struct Metrics: Equatable {
        var minimum: Double = 0
        var preferred: Double = 0
        var flexible: Double = 0
    }
    struct ScrollInfo {
        let nodeID: HUDSourceID
        let contentID: HUDSourceID
        let viewportID: HUDSourceID
        let hiddenLength: Double
        let sensitivity: Double
        let normalizedPosition: Double
    }
    struct Report {
        var missingTextMetrics: Set<HUDSourceID> = []
        var unverifiedCustomComponents: Set<String> = []
        var scroll: ScrollInfo?
    }
    let scene: HUDSourceScene
    let components: [HUDSourceID: [HUDSourceWatchComponent]]
    let spriteByComponent: [HUDSourceID: HUDSourceJSONValue]
    let intrinsicSize: IntrinsicSize?

    init(document: HUDSourceWatchDocument, intrinsicSize: IntrinsicSize? = nil) {
        self.init(scene: document.scene, components: document.components,
                  spriteByComponent: document.spriteByComponent, intrinsicSize: intrinsicSize)
    }
    init(scene: HUDSourceScene, components: [HUDSourceID: [HUDSourceWatchComponent]],
         spriteByComponent: [HUDSourceID: HUDSourceJSONValue] = [:], intrinsicSize: IntrinsicSize? = nil) {
        self.scene = scene; self.components = components
        self.spriteByComponent = spriteByComponent; self.intrinsicSize = intrinsicSize
    }

    func apply(to pose: inout HUDSourceWatchPose, verticalNormalizedPosition: Double = 1,
               slantMapping: SlantMapping? = nil, worldRoot: simd_double4x4 = matrix_identity_double4x4) throws -> Report {
        guard verticalNormalizedPosition.isFinite else { throw HUDSourceError.invalid("Nonfinite Watch scroll position") }
        let initial = try scene.resolve(overrides: pose.transforms)
        var report = Report()
        for axis in 0...1 {
            var measured: [HUDSourceID: Metrics] = [:]
            for id in scene.traversalIDs.reversed() {
                guard initial[id]?.activeInHierarchy == true else { continue }
                measured[id] = metrics(id, axis: axis, pose: pose, active: initial, measured: measured, report: &report)
            }
            for id in scene.traversalIDs {
                guard initial[id]?.activeInHierarchy == true, scene.node(id)?.transform.rect != nil else { continue }
                if let fitter = component("ContentSizeFitter", on: id) {
                    let fit = Int(fitter[axis == 0 ? "m_HorizontalFit" : "m_VerticalFit"].float())
                    if fit == 1 || fit == 2, let input = measured[id] {
                        setSize(id, axis: axis, value: fit == 1 ? input.minimum : input.preferred, pose: &pose)
                    }
                }
                if let group = group(on: id) {
                    control(group, on: id, axis: axis, pose: &pose, active: initial, measured: measured)
                }
            }
        }
        try applyScroll(to: &pose, position: min(1, max(0, verticalNormalizedPosition)), report: &report)
        let resolved = try scene.resolve(overrides: pose.transforms)
        for id in scene.traversalIDs where resolved[id]?.activeInHierarchy == true {
            if let effect = component("UIScrollCellSlantEffect", on: id) {
                // Static source Tick: InverseTransformPoint(cell.position).y,
                // Clamp01((y-bottom)/(top-bottom)), curve * width + left,
                // TransformPoint(x,0,0), replace ONLY the cell's world X.
                // The call scheduling relative to engine layout is separate.
                report.unverifiedCustomComponents.insert("UIScrollCellSlantEffect.tickSchedulingRelativeToCanvasRebuild")
                if let mapping = slantMapping {
                    for cell in effect["_cells"].array.compactMap({ $0.targetID }) {
                        if let x = mapping(effect, cell, resolved), x.isFinite {
                            setAnchoredAxis(cell, axis: 0, value: x, resetAnchors: false, pose: &pose)
                        }
                    }
                } else { try applySlant(effect, on: id, resolved: resolved, worldRoot: worldRoot, pose: &pose) }
            }
            for c in components[id] ?? [] where c.enabled && ["UIStepScrollList", "GridLayoutGroup", "NotchAdapter"].contains(c.kind) {
                report.unverifiedCustomComponents.insert(c.kind)
            }
        }
        return report
    }

    /// Native wheel input supplies a signed delta. Positive delta moves toward
    /// the source's normalized top (=1), with the original ScrollSensitivity.
    /// Elastic/inertial/smooth-scroll scheduling is not inferred here.
    func scrolledPosition(_ current: Double, delta: Double, info: ScrollInfo?) -> Double {
        guard current.isFinite, delta.isFinite, let info = info, info.hiddenLength > 0 else { return current }
        return min(1, max(0, current + delta * info.sensitivity / info.hiddenLength))
    }

    static func slantCurve(_ component: HUDSourceWatchComponent) throws -> HUDSourceScalarCurve {
        let keys = component["_curve"]["m_Curve"].array.map { key in
            HUDSourceScalarKey(time: key["time"].float(), value: key["value"].float(),
                inSlope: key["inSlope"].float(), outSlope: key["outSlope"].float(),
                weightedMode: Int(key["weightedMode"].float()), inWeight: key["inWeight"].float(),
                outWeight: key["outWeight"].float())
        }
        return try HUDSourceScalarCurve(keys: keys)
    }

    private func component(_ kind: String, on id: HUDSourceID) -> HUDSourceWatchComponent? {
        components[id]?.first { $0.kind == kind && $0.enabled }
    }
    private func group(on id: HUDSourceID) -> HUDSourceWatchComponent? {
        components[id]?.first { $0.enabled && ($0.kind == "HorizontalLayoutGroup" || $0.kind == "VerticalLayoutGroup") }
    }
    private func children(_ id: HUDSourceID, active: [HUDSourceID: HUDSourceResolvedNode]) -> [HUDSourceID] {
        (scene.node(id)?.childIDs ?? []).filter { child in
            guard scene.node(child)?.transform.rect != nil, active[child]?.activeInHierarchy == true else { return false }
            // uGUI queries every ILayoutIgnorer, separately from its enabled
            // ILayoutElement measurement filter. At least one false includes it.
            let ignorers = (components[child] ?? []).filter { $0.kind == "LayoutElement" }
            return ignorers.isEmpty || ignorers.contains { !$0["m_IgnoreLayout"].flag() }
        }
    }
    private func rect(_ id: HUDSourceID, pose: HUDSourceWatchPose) -> HUDSourceRect? {
        guard let n = scene.node(id), let r = n.transform.rect else { return nil }
        let p = n.parentID.flatMap { rect($0, pose: pose) }, o = pose.transforms[id]
        return r.layout(parent: p, anchoredPosition3D: o?.anchoredPosition3D, sizeDelta: o?.sizeDelta,
            localZ: n.transform.localPosition.z, anchorMin: o?.anchorMin, anchorMax: o?.anchorMax, pivot: o?.pivot).rect
    }
    private func sizeDelta(_ id: HUDSourceID, pose: HUDSourceWatchPose) -> SIMD2<Double> {
        (pose.transforms[id]?.sizeDelta ?? scene.node(id)?.transform.rect?.sizeDelta ?? HUDSourceVector2(0, 0)).simd
    }
    private func scale(_ id: HUDSourceID, axis: Int, pose: HUDSourceWatchPose) -> Double {
        (pose.transforms[id]?.localScale ?? scene.node(id)?.transform.localScale ?? HUDSourceVector3(1, 1, 1)).simd[axis]
    }
    private func metrics(_ id: HUDSourceID, axis: Int, pose: HUDSourceWatchPose,
                         active: [HUDSourceID: HUDSourceResolvedNode], measured: [HUDSourceID: Metrics],
                         report: inout Report) -> Metrics {
        var candidates: [(Int, Metrics)] = []
        if let g = group(on: id) { candidates.append((0, totals(g, on: id, axis: axis, pose: pose, active: active, measured: measured))) }
        if component("UIText", on: id) != nil {
            if let size = intrinsicSize?(id, rect(id, pose: pose)), size.x.isFinite, size.y.isFinite {
                candidates.append((0, Metrics(minimum: 0, preferred: max(0, size[axis]), flexible: 0)))
            } else {
                report.missingTextMetrics.insert(id)
                // Source rectangle is retained as a visible fallback; no font
                // substitution or guessed glyph advance enters the evidence.
                candidates.append((0, Metrics(minimum: 0, preferred: max(0, sizeDelta(id, pose: pose)[axis]), flexible: 0)))
            }
        }
        for image in components[id] ?? [] where image.enabled && (image.kind == "UIImage" || image.kind == "Image") {
            guard let sprite = spriteByComponent[image.id] else { continue }
            let raw = sprite["raw_sprite"], border = raw["m_Border"]
            var referencePixels = 100.0, ancestor: HUDSourceID? = id
            while let current = ancestor {
                if let scaler = component("CanvasScaler", on: current) { referencePixels = scaler["m_ReferencePixelsPerUnit"].float(100); break }
                ancestor = scene.node(current)?.parentID
            }
            let ppu = raw["m_PixelsToUnits"].float(100) / referencePixels
            guard ppu.isFinite, ppu > 0 else { continue }
            let type = Int(image["m_Type"].float())
            let amount = type == 1 || type == 2 ? (axis == 0 ? border["x"].float() + border["z"].float() : border["y"].float() + border["w"].float()) : raw["m_Rect"][axis == 0 ? "width" : "height"].float()
            candidates.append((0, Metrics(minimum: 0, preferred: amount / ppu, flexible: 0)))
        }
        let suffix = axis == 0 ? "Width" : "Height"
        for element in components[id] ?? [] where element.kind == "LayoutElement" && element.enabled {
            candidates.append((Int(element["m_LayoutPriority"].float(1)), Metrics(
                minimum: element["m_Min" + suffix].float(-1), preferred: element["m_Preferred" + suffix].float(-1),
                flexible: element["m_Flexible" + suffix].float(-1))))
        }
        func property(_ value: (Metrics) -> Double) -> Double {
            var priority = Int.min, result = 0.0
            for (p, m) in candidates {
                let v = value(m)
                guard v >= 0, p >= priority else { continue }
                if p > priority { priority = p; result = v } else { result = max(result, v) }
            }
            return result
        }
        let minimum = property { $0.minimum }
        return Metrics(minimum: minimum, preferred: max(minimum, property { $0.preferred }), flexible: property { $0.flexible })
    }
    private func padding(_ g: HUDSourceWatchComponent, axis: Int) -> (start: Double, total: Double) {
        let p = g["m_Padding"], start = p[axis == 0 ? "m_Left" : "m_Top"].float()
        return (start, start + p[axis == 0 ? "m_Right" : "m_Bottom"].float())
    }
    private func childMetrics(_ child: HUDSourceID, g: HUDSourceWatchComponent, axis: Int,
                              pose: HUDSourceWatchPose, measured: [HUDSourceID: Metrics]) -> Metrics {
        let suffix = axis == 0 ? "Width" : "Height"
        var m: Metrics
        if g["m_ChildControl" + suffix].flag() { m = measured[child] ?? Metrics() }
        else { let size = sizeDelta(child, pose: pose)[axis]; m = Metrics(minimum: size, preferred: size, flexible: 0) }
        if g["m_ChildForceExpand" + suffix].flag() { m.flexible = max(m.flexible, 1) }
        return m
    }
    private func totals(_ g: HUDSourceWatchComponent, on id: HUDSourceID, axis: Int, pose: HUDSourceWatchPose,
                        active: [HUDSourceID: HUDSourceResolvedNode], measured: [HUDSourceID: Metrics]) -> Metrics {
        let p = padding(g, axis: axis), cross = (g.kind == "VerticalLayoutGroup") != (axis == 1)
        var sum = Metrics(minimum: p.total, preferred: p.total, flexible: 0)
        let list = children(id, active: active), suffix = axis == 0 ? "Width" : "Height", spacing = g["m_Spacing"].float()
        for child in list {
            var m = childMetrics(child, g: g, axis: axis, pose: pose, measured: measured)
            if g["m_ChildScale" + suffix].flag() { let s = scale(child, axis: axis, pose: pose); m.minimum *= s; m.preferred *= s; m.flexible *= s }
            if cross {
                sum.minimum = max(sum.minimum, m.minimum + p.total); sum.preferred = max(sum.preferred, m.preferred + p.total)
                sum.flexible = max(sum.flexible, m.flexible)
            } else { sum.minimum += m.minimum + spacing; sum.preferred += m.preferred + spacing; sum.flexible += m.flexible }
        }
        if !cross && !list.isEmpty { sum.minimum -= spacing; sum.preferred -= spacing }
        sum.preferred = max(sum.minimum, sum.preferred); return sum
    }
    private func control(_ g: HUDSourceWatchComponent, on id: HUDSourceID, axis: Int,
                         pose: inout HUDSourceWatchPose, active: [HUDSourceID: HUDSourceResolvedNode],
                         measured: [HUDSourceID: Metrics]) {
        guard let parentRect = rect(id, pose: pose) else { return }
        var list = children(id, active: active); if g["m_ReverseArrangement"].flag() { list.reverse() }
        let suffix = axis == 0 ? "Width" : "Height", controls = g["m_ChildControl" + suffix].flag(), scales = g["m_ChildScale" + suffix].flag()
        let p = padding(g, axis: axis), size = parentRect.size[axis], alignment = Int(g["m_ChildAlignment"].float())
        let align = Double(axis == 0 ? alignment % 3 : alignment / 3) * 0.5
        let cross = (g.kind == "VerticalLayoutGroup") != (axis == 1)
        let total = totals(g, on: id, axis: axis, pose: pose, active: active, measured: measured)
        var pos = p.start, flexibleMultiplier = 0.0
        if size > total.preferred {
            if total.flexible == 0 { pos += (size - total.preferred) * align }
            else if total.flexible > 0 { flexibleMultiplier = (size - total.preferred) / total.flexible }
        }
        let lerp = total.minimum == total.preferred ? 0 : min(1, max(0, (size - total.minimum) / (total.preferred - total.minimum)))
        for child in list {
            let m = childMetrics(child, g: g, axis: axis, pose: pose, measured: measured), s = scales ? scale(child, axis: axis, pose: pose) : 1
            let required: Double, start: Double
            if cross {
                let maximum = m.flexible > 0 ? size : m.preferred
                required = min(maximum, max(m.minimum, size - p.total))
                start = p.start + (size - p.total - required * s) * align
            } else { required = m.minimum + (m.preferred - m.minimum) * lerp + m.flexible * flexibleMultiplier; start = pos }
            let actual = controls ? required : sizeDelta(child, pose: pose)[axis]
            let offset = controls ? 0 : (required - actual) * align
            if controls { setSize(child, axis: axis, value: required, pose: &pose, resetAnchors: true) }
            let pivot = (pose.transforms[child]?.pivot ?? scene.node(child)?.transform.rect?.pivot ?? HUDSourceVector2(0.5, 0.5)).simd[axis]
            let anchored = axis == 0 ? start + offset + actual * pivot * s : -start - offset - actual * (1 - pivot) * s
            setAnchoredAxis(child, axis: axis, value: anchored, resetAnchors: true, pose: &pose)
            if !cross { pos += required * s + g["m_Spacing"].float() }
        }
    }
    private func setSize(_ id: HUDSourceID, axis: Int, value: Double, pose: inout HUDSourceWatchPose, resetAnchors: Bool = false) {
        guard let node = scene.node(id), let r = node.transform.rect else { return }
        var o = pose.transforms[id] ?? HUDSourceTransformOverride(), size = (o.sizeDelta ?? r.sizeDelta).simd
        if resetAnchors { o.anchorMin = HUDSourceVector2(0, 1); o.anchorMax = HUDSourceVector2(0, 1) }
        let span = (o.anchorMax ?? r.anchorMax).simd - (o.anchorMin ?? r.anchorMin).simd
        let parentSize = node.parentID.flatMap { rect($0, pose: pose)?.size } ?? .zero
        size[axis] = value - parentSize[axis] * span[axis]
        o.sizeDelta = HUDSourceVector2(size.x, size.y); pose.transforms[id] = o
    }
    private func setAnchoredAxis(_ id: HUDSourceID, axis: Int, value: Double, resetAnchors: Bool,
                                 pose: inout HUDSourceWatchPose) {
        guard let node = scene.node(id), let r = node.transform.rect else { return }
        var o = pose.transforms[id] ?? HUDSourceTransformOverride()
        if let local = o.localPosition {
            for i in 0...2 where i != axis && o.positionComponents[i] == nil { o.positionComponents[i] = local.simd[i] }
            o.localPosition = nil
        }
        o.positionComponents.removeValue(forKey: axis)
        var anchored = (o.anchoredPosition3D ?? HUDSourceVector3(r.anchoredPosition.x, r.anchoredPosition.y, node.transform.localPosition.z)).simd
        anchored[axis] = value
        o.anchoredPosition3D = HUDSourceVector3(anchored.x, anchored.y, anchored.z)
        if resetAnchors { o.anchorMin = HUDSourceVector2(0, 1); o.anchorMax = HUDSourceVector2(0, 1) }
        pose.transforms[id] = o
    }
    private func applyScroll(to pose: inout HUDSourceWatchPose, position: Double, report: inout Report) throws {
        for id in scene.traversalIDs {
            guard let scroll = component("UIScrollRect", on: id) ?? component("ScrollRect", on: id),
                  scroll["m_Vertical"].flag(), !scroll["disableScroll"].flag(),
                  let contentID = scroll["m_Content"].targetID, let viewportID = scroll["m_Viewport"].targetID else { continue }
            let resolved = try scene.resolve(overrides: pose.transforms)
            guard resolved[id]?.activeInHierarchy == true, let viewport = resolved[viewportID], let viewRect = viewport.rect,
                  let inverse = HUDSourceGeometry.inverse(viewport.worldMatrix), let content = resolved[contentID], let contentRect = content.rect else { continue }
            let transform = inverse * content.worldMatrix
            let ys = contentRect.corners.map { simd_mul(transform, SIMD4<Double>($0.x, $0.y, $0.z, 1)).y }
            guard let lower = ys.min(), let upper = ys.max() else { continue }
            var boundsMin = lower, extent = upper - lower
            if extent < viewRect.size.y {
                let excess = viewRect.size.y - extent
                let pivot = (pose.transforms[contentID]?.pivot ?? content.node.transform.rect?.pivot ?? HUDSourceVector2(0.5, 0.5)).y
                boundsMin -= excess * pivot; extent = viewRect.size.y
            }
            let hidden = max(0, extent - viewRect.size.y), delta = viewRect.origin.y - position * hidden - boundsMin
            if abs(delta) > 0.01 {
                let target = content.localMatrix.columns.3 + SIMD4<Double>(0, delta, 0, 0)
                setLocalPosition(contentID, target: target, parentRect: content.node.parentID.flatMap { resolved[$0]?.rect }, pose: &pose)
            }
            report.scroll = ScrollInfo(nodeID: id, contentID: contentID, viewportID: viewportID,
                hiddenLength: hidden, sensitivity: scroll["m_ScrollSensitivity"].float(1), normalizedPosition: position)
            report.unverifiedCustomComponents.insert("UIScrollRect.elasticInertiaAndSmoothScrollScheduling")
        }
    }
    private func applySlant(_ effect: HUDSourceWatchComponent, on id: HUDSourceID,
                            resolved: [HUDSourceID: HUDSourceResolvedNode], worldRoot: simd_double4x4,
                            pose: inout HUDSourceWatchPose) throws {
        guard let selfNode = resolved[id], let inverse = HUDSourceGeometry.inverse(simd_mul(worldRoot, selfNode.worldMatrix)) else { return }
        let bottom = effect["_bottomY"].float(), range = effect["_topY"].float() - bottom
        guard range != 0 else { throw HUDSourceError.invalid("Zero Watch slant Y range") }
        let curve = try Self.slantCurve(effect)
        for cellID in effect["_cells"].array.compactMap({ $0.targetID }) {
            guard let cell = resolved[cellID], let parentID = cell.node.parentID,
                  let parent = resolved[parentID], let parentInverse = HUDSourceGeometry.inverse(simd_mul(worldRoot, parent.worldMatrix)),
                  cell.node.transform.rect != nil else { continue }
            let world = simd_mul(worldRoot, cell.worldMatrix).columns.3, y = simd_mul(inverse, world).y
            let t = min(1, max(0, (y - bottom) / range))
            guard let value = curve.sample(at: t) else { continue }
            let x = effect["_leftX"].float() + value * effect["_maxWidth"].float()
            let desiredWorldX = simd_mul(simd_mul(worldRoot, selfNode.worldMatrix), SIMD4<Double>(x, 0, 0, 1)).x
            let target = simd_mul(parentInverse, SIMD4<Double>(desiredWorldX, world.y, world.z, 1))
            setLocalPosition(cellID, target: target, parentRect: parent.rect, pose: &pose)
        }
    }
    private func setLocalPosition(_ id: HUDSourceID, target: SIMD4<Double>, parentRect: HUDSourceRect?,
                                  pose: inout HUDSourceWatchPose) {
        guard let node = scene.node(id), let rect = node.transform.rect else { return }
        var override = pose.transforms[id] ?? HUDSourceTransformOverride()
        let reference = rect.layout(parent: parentRect, anchoredPosition3D: HUDSourceVector3(0, 0, target.z),
            sizeDelta: override.sizeDelta, localZ: target.z, anchorMin: override.anchorMin,
            anchorMax: override.anchorMax, pivot: override.pivot).position
        override.anchoredPosition3D = HUDSourceVector3(target.x - reference.x, target.y - reference.y, target.z)
        override.localPosition = nil
        override.positionComponents.removeValue(forKey: 0); override.positionComponents.removeValue(forKey: 1)
        override.positionComponents[2] = target.z
        pose.transforms[id] = override
    }
}
