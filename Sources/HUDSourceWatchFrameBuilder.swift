import Foundation
import CoreGraphics
import simd

/// The original scene, animation, layout and raycast share one resolved pose.
/// UI vertices are baked into their nearest Canvas space, as CanvasRenderer
/// batches them; this also keeps RectMask2D clipping in the shader's space.
final class HUDSourceWatchFrameBuilder {
    struct Hit {
        let graphicID: HUDSourceID
        let buttonID: HUDSourceID
        let rect: HUDSourceRect
        let world: simd_double4x4
        let masks: [(rect: HUDSourceRect, world: simd_double4x4)]
    }
    struct Frame {
        let resolved: [HUDSourceID: HUDSourceResolvedNode]
        let batches: [HUDSourceMetalRenderer.Batch]
        let hits: [Hit]
        let layoutReport: HUDSourceWatchLayout.Report
        let diagnostics: [String]

        func button(at point: CGPoint, camera: HUDSourceCamera, viewport: CGRect) -> HUDSourceID? {
            for hit in hits.reversed() {
                guard camera.hit(point, world: hit.world, rect: hit.rect, viewport: viewport) != nil,
                      hit.masks.allSatisfy({ camera.hit(point, world: $0.world, rect: $0.rect, viewport: viewport) != nil }) else { continue }
                return hit.buttonID
            }
            return nil
        }
    }
    let document: HUDSourceWatchDocument
    let text: HUDSourceTextGeometry
    private let renderer: HUDSourceMetalRenderer
    private let materials: [HUDSourceID: HUDSourceJSONValue]
    private var sprites: [HUDSourceID: HUDSourceImageGeometry.Sprite] = [:]
    private var textureSizes: [String: SIMD2<Float>] = ["__white": SIMD2(1, 1)]
    private var sourceMeshNames: [HUDSourceID: String] = [:]
    private var geometryKeys: [HUDSourceID: [Double]] = [:]
    private var textMeshes: [HUDSourceID: (key: [Double], mesh: HUDSourceTextGeometry.Mesh)] = [:]
    private let buttonIDs: Set<HUDSourceID>

    init(document: HUDSourceWatchDocument, renderer: HUDSourceMetalRenderer) throws {
        self.document = document; self.renderer = renderer
        text = try HUDSourceTextGeometry(document: document)
        materials = Dictionary(uniqueKeysWithValues: document.materials["materials"].array.compactMap { record in
            record["id"].string.map { (HUDSourceID(rawValue: $0), record) }
        })
        buttonIDs = Set(document.components.compactMap { id, records in
            records.contains(where: { $0.kind == "UIButton" && $0.enabled }) ? id : nil
        })
        var textures: [String: HUDSourceJSONValue] = [:]
        for texture in document.sprites["source_textures"].array {
            guard let id = texture["id"].string, let file = texture["png"]["file"].string else {
                throw HUDSourceError.invalid("Source Sprite texture metadata missing")
            }
            textures[id] = texture
            // Unity ColorSpace.Gamma=0 marks sRGB texture sampling; the
            // exporter supplies this original field, not a guessed preset.
            guard let space = texture["color_space"].number else {
                throw HUDSourceError.invalid("Unverified source Sprite texture color space: \(id)")
            }
            guard space == 0 || space == 1, renderer.containsTexture(named: id) else {
                throw HUDSourceError.invalid("Original Sprite mip chain missing from renderer: \(id), \(file)")
            }
            textureSizes[id] = SIMD2(Float(texture["width"].float()), Float(texture["height"].float()))
        }
        for (component, sprite) in document.spriteByComponent {
            guard let id = sprite["texture"]["id"].string, let texture = textures[id] else {
                throw HUDSourceError.invalid("Unresolved original Sprite texture: \(component)")
            }
            sprites[component] = try HUDSourceImageGeometry.Sprite(source: sprite, texture: texture)
        }
        let root = document.root.deletingLastPathComponent()
        for name in ["Equipring", "watchline", "Plane", "Cylinder"] {
            let value = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
                from: Data(contentsOf: root.appendingPathComponent("Meshes/\(name).json")))
            guard let cab = value["cab"].string, let path = value["path_id"].string else {
                throw HUDSourceError.invalid("Source Mesh identity missing")
            }
            sourceMeshNames[HUDSourceID(rawValue: cab + ":" + path)] = name
        }
    }

    func build(pose input: HUDSourceWatchPose, worldRoot: simd_double4x4,
               verticalNormalizedPosition: Double = 1) throws -> Frame {
        var pose = input
        let layout = HUDSourceWatchLayout(document: document) { [weak self] id, rect in
            guard let self, let rect,
                  let mesh = try? self.localText(on: id, rect: rect, sdfScale: 1) else { return nil }
            return SIMD2(mesh.advance, mesh.ascender - mesh.descender)
        }
        let report = try layout.apply(to: &pose, verticalNormalizedPosition: verticalNormalizedPosition, worldRoot: worldRoot)
        let resolved = try document.scene.resolve(overrides: pose.transforms)
        let alpha = document.inheritedAlpha(pose: pose)
        var batches: [(order: Int, sequence: Int, batch: HUDSourceMetalRenderer.Batch)] = []
        var hits: [(order: Int, sequence: Int, hit: Hit)] = []
        var diagnostics: [String] = []
        var canvases: [HUDSourceID: HUDSourceID] = [:], orders: [HUDSourceID: Int] = [:]
        var masks: [HUDSourceID: [HUDSourceID]] = [:]
        var sequence = 0
        for id in document.scene.traversalIDs {
            guard let n = resolved[id], n.activeInHierarchy else { continue }
            let parent = n.node.parentID
            let canvas = document.component("Canvas", on: id)
            canvases[id] = canvas != nil ? id : parent.flatMap { canvases[$0] }
            let inheritedOrder = parent.flatMap { orders[$0] } ?? 0
            orders[id] = canvas?["m_OverrideSorting"].flag() == true ? Int(canvas!["m_SortingOrder"].float()) : inheritedOrder
            masks[id] = canvas?["m_OverrideSorting"].flag() == true ? [] : (parent.flatMap { masks[$0] } ?? [])
            if document.component("RectMask2D", on: id) != nil { masks[id, default: []].append(id) }
            guard let canvasID = canvases[id], let canvasNode = resolved[canvasID] else { continue }
            let canvasWorld = simd_mul(worldRoot, canvasNode.worldMatrix)
            guard let inverseCanvas = HUDSourceGeometry.inverse(canvasNode.worldMatrix) else { continue }
            let toCanvas = simd_mul(inverseCanvas, n.worldMatrix)
            let world = simd_mul(worldRoot, n.worldMatrix)
            let maskIDs = masks[id] ?? []
            let clip = clipRect(maskIDs, resolved: resolved, inverseCanvas: inverseCanvas)
            for component in document.components[id] ?? [] where component.enabled {
                guard ["UIImage", "Image", "UIRawImage", "RawImage", "UIText"].contains(component.kind), let rect = n.rect else { continue }
                let isText = component.kind == "UIText"
                let localPositions: [SIMD4<Float>], uv: [SIMD2<Float>], indices: [UInt32]
                var normals: [SIMD3<Float>] = [], uv1: [SIMD2<Float>] = []
                var color = component["m_Color"].color
                var textureID = "__white"
                var materialID = component["m_Material"].targetID
                var key = [rect.origin.x, rect.origin.y, rect.size.x, rect.size.y]
                if isText {
                    guard let literal = text.literal(on: id), !literal.isEmpty else { continue }
                    let sdfScale = simd_length(SIMD3(world.columns.1.x, world.columns.1.y, world.columns.1.z))
                    do {
                        let mesh = try localText(on: id, rect: rect, sdfScale: sdfScale)
                        localPositions = mesh.positions; uv = mesh.uv; indices = mesh.indices
                        normals = mesh.normals; uv1 = mesh.uv2; color = mesh.color
                        materialID = mesh.materialID; textureID = mesh.atlasID.rawValue
                        key.append(sdfScale)
                    } catch { diagnostics.append("\(n.node.path): \(error)"); continue }
                } else if component.kind == "UIRawImage" || component.kind == "RawImage" {
                    let r = component["m_UVRect"]
                    let u = r["x"].float(), v = r["y"].float(), w = r["width"].float(1), h = r["height"].float(1)
                    var mesh = HUDSourceImageMesh()
                    mesh.quad([rect.origin, SIMD2(rect.origin.x, rect.origin.y + rect.size.y), rect.origin + rect.size,
                               SIMD2(rect.origin.x + rect.size.x, rect.origin.y)],
                              [SIMD2(u, v), SIMD2(u, v + h), SIMD2(u + w, v + h), SIMD2(u + w, v)])
                    localPositions = mesh.positions; uv = mesh.uv; indices = mesh.indices
                    textureID = component["m_Texture"].targetID?.rawValue ?? "__white"
                    if let animation = document.component("UIGraphicAnimation", on: id) {
                        materialID = animation["_material"].targetID ?? materialID
                    }
                } else {
                    let fill = pose.value("m_FillAmount", on: id, fallback: component["m_FillAmount"].float(1))
                    let pivot = pose.transforms[id]?.pivot?.simd ?? n.node.transform.rect?.pivot.simd ?? SIMD2(0.5, 0.5)
                    let mesh = try HUDSourceImageGeometry.build(image: component, sprite: sprites[component.id], rect: rect, pivot: pivot, fillAmount: fill)
                    localPositions = mesh.positions; uv = mesh.uv; indices = mesh.indices
                    textureID = sprites[component.id]?.textureID ?? "__white"; key.append(fill)
                }
                let colorPrefix = isText ? "m_fontColor" : "m_Color"
                for (axis, suffix) in ["r", "g", "b", "a"].enumerated() {
                    let sampled = Float(pose.value(colorPrefix + "." + suffix, on: id, fallback: Double(color[axis])))
                    // Graphic/TMP vertex streams carry Color32, before
                    // CanvasRenderer multiplies inherited CanvasGroup alpha.
                    color[axis] = (min(1, max(0, sampled)) * 255).rounded(.toNearestOrEven) / 255
                }
                color.w *= Float(alpha[id] ?? 1)
                let order = orders[id] ?? 0
                if component["m_RaycastTarget"].flag(), let buttonID = buttonAncestor(id), canReceiveInput(id) {
                    let padding = component["m_RaycastPadding"]
                    let hitRect = HUDSourceRect(origin: rect.origin + SIMD2(padding["x"].float(), padding["y"].float()),
                        size: rect.size - SIMD2(padding["x"].float() + padding["z"].float(), padding["y"].float() + padding["w"].float()))
                    let hitMasks = maskIDs.compactMap { mask -> (rect: HUDSourceRect, world: simd_double4x4)? in
                        guard let m = resolved[mask], let r = m.rect else { return nil }
                        return (r, simd_mul(worldRoot, m.worldMatrix))
                    }
                    hits.append((order, sequence, Hit(graphicID: id, buttonID: buttonID, rect: hitRect, world: world, masks: hitMasks)))
                }
                sequence += 1
                guard !indices.isEmpty, color.w > 0 else { continue }
                let positions = localPositions.map { p -> SIMD4<Float> in
                    let result = simd_mul(toCanvas, SIMD4<Double>(Double(p.x), Double(p.y), Double(p.z), Double(p.w)))
                    return SIMD4(Float(result.x), Float(result.y), Float(result.z), Float(result.w))
                }
                key.append(contentsOf: Self.flatten(toCanvas).map(Double.init))
                let meshName = "ui/" + component.id.rawValue
                if geometryKeys[component.id] != key {
                    let normalMatrix = simd_double3x3(columns: (SIMD3(toCanvas.columns.0.x, toCanvas.columns.0.y, toCanvas.columns.0.z),
                        SIMD3(toCanvas.columns.1.x, toCanvas.columns.1.y, toCanvas.columns.1.z), SIMD3(toCanvas.columns.2.x, toCanvas.columns.2.y, toCanvas.columns.2.z))).inverse.transpose
                    let bakedNormals = normals.map { normal -> SIMD3<Float> in
                        let value = simd_normalize(simd_mul(normalMatrix, SIMD3<Double>(Double(normal.x), Double(normal.y), Double(normal.z))))
                        return SIMD3(Float(value.x), Float(value.y), Float(value.z))
                    }
                    try renderer.registerGeometry(named: meshName, positions: positions, uv: uv, indices: indices, normals: bakedNormals, uv1: uv1)
                    geometryKeys[component.id] = key
                }
                let material = materialID.flatMap { materials[$0]?["name"].string } ?? (clip != nil ? "__ui_default_clip" : "__ui_default")
                var batch = HUDSourceMetalRenderer.Batch(mesh: meshName, material: isText ? materialID!.rawValue : material,
                    world: simd_float4x4(canvasWorld), color: color, textureOverrides: ["_MainTex": textureID])
                if let size = textureSizes[textureID] { batch.uniformOverrides["mainTexTexelSize"] = [1 / size.x, 1 / size.y, size.x, size.y] }
                if let clip {
                    batch.uniformOverrides["clipRect"] = clip
                    if let mask = maskIDs.last, let m = document.component("RectMask2D", on: mask) {
                        let softness = m["m_HGSoftness"]
                        batch.uniformOverrides["uiMaskHGSoftness"] = ["x", "y", "z", "w"].map { Float(softness[$0].float()) }
                    }
                    if materialID != nil { diagnostics.append("Source custom-material rect-clip variant pending: \(n.node.path)") }
                }
                applyMaterialProperties(pose, on: id, materialID: materialID, to: &batch)
                if let animation = document.component("UIGraphicAnimation", on: id) {
                    let sx = pose.value("_scale.x", on: id, fallback: animation["_scale"]["x"].float(1))
                    let sy = pose.value("_scale.y", on: id, fallback: animation["_scale"]["y"].float(1))
                    // UIGraphicAnimation.LateTick writes its cloned material,
                    // never the transform. Approximately(scale,0) uses Unity's
                    // subnormal-zero threshold for this particular comparison.
                    let zero = Double(Float.leastNonzeroMagnitude) * 8
                    let ix = abs(sx) < zero ? 0 : 1 / sx, iy = abs(sy) < zero ? 0 : 1 / sy
                    batch.uniformOverrides["_VFXMainTex_ST"] = [Float(ix), Float(iy), Float((1 - ix) * 0.5), Float((1 - iy) * 0.5)]
                    batch.uniformOverrides["_TintColorAlpha"] = [Float(pose.value("_alpha", on: id, fallback: animation["_alpha"].float()))]
                }
                batches.append((order, sequence, batch))
            }
            if let mesh = document.component("MeshFilter", on: id), let sourceID = mesh["m_Mesh"].targetID,
               let meshName = sourceMeshNames[sourceID], let render = document.component("MeshRenderer", on: id) {
                for material in render["m_Materials"].array {
                    guard let materialID = material.targetID, let name = materials[materialID]?["name"].string else { continue }
                    var batch = HUDSourceMetalRenderer.Batch(mesh: meshName, material: name, world: simd_float4x4(world), color: SIMD4(repeating: 1))
                    applyMaterialProperties(pose, on: id, materialID: materialID, to: &batch)
                    batches.append((Int(render["m_SortingOrder"].float()), sequence, batch)); sequence += 1
                }
            }
        }
        batches.sort { $0.order == $1.order ? $0.sequence < $1.sequence : $0.order < $1.order }
        hits.sort { $0.order == $1.order ? $0.sequence < $1.sequence : $0.order < $1.order }
        return Frame(resolved: resolved, batches: batches.map(\.batch), hits: hits.map(\.hit), layoutReport: report,
            diagnostics: diagnostics + pose.unboundPaths.sorted().map { "Unbound source curve: " + $0 })
    }

    private func localText(on id: HUDSourceID, rect: HUDSourceRect, sdfScale: Double) throws -> HUDSourceTextGeometry.Mesh {
        let key = [rect.origin.x, rect.origin.y, rect.size.x, rect.size.y, sdfScale]
        if let cached = textMeshes[id], cached.key == key { return cached.mesh }
        let mesh = try text.build(on: id, rect: rect, sdfScale: sdfScale)
        textMeshes[id] = (key, mesh); return mesh
    }
    private func buttonAncestor(_ id: HUDSourceID) -> HUDSourceID? {
        var current: HUDSourceID? = id
        while let node = current {
            if buttonIDs.contains(node) { return node }
            current = document.scene.node(node)?.parentID
        }
        return nil
    }
    private func canReceiveInput(_ id: HUDSourceID) -> Bool {
        var current: HUDSourceID? = id
        while let node = current {
            var stop = false
            for group in document.components[node] ?? [] where group.kind == "CanvasGroup" && group.enabled {
                if !group["m_BlocksRaycasts"].flag(true) || !group["m_Interactable"].flag(true) { return false }
                stop = stop || group["m_IgnoreParentGroups"].flag()
            }
            if stop { break }
            current = document.scene.node(node)?.parentID
        }
        return true
    }
    private func clipRect(_ ids: [HUDSourceID], resolved: [HUDSourceID: HUDSourceResolvedNode], inverseCanvas: simd_double4x4) -> [Float]? {
        var low = SIMD2<Double>(repeating: -.infinity), high = SIMD2<Double>(repeating: .infinity), found = false
        for id in ids {
            guard let node = resolved[id], let rect = node.rect else { continue }
            let matrix = simd_mul(inverseCanvas, node.worldMatrix)
            let points = rect.corners.map { simd_mul(matrix, SIMD4($0.x, $0.y, $0.z, 1)) }
            let minimum = SIMD2(points.map(\.x).min()!, points.map(\.y).min()!)
            let maximum = SIMD2(points.map(\.x).max()!, points.map(\.y).max()!)
            let padding = document.component("RectMask2D", on: id)?["m_Padding"] ?? .null
            low = simd_max(low, minimum + SIMD2(padding["x"].float(), padding["y"].float()))
            high = simd_min(high, maximum - SIMD2(padding["z"].float(), padding["w"].float())); found = true
        }
        return found ? [Float(low.x), Float(low.y), Float(high.x), Float(high.y)] : nil
    }
    private func applyMaterialProperties(_ pose: HUDSourceWatchPose, on id: HUDSourceID, materialID: HUDSourceID?, to batch: inout HUDSourceMetalRenderer.Batch) {
        var values: [String: [Float]] = [:]
        if let materialID, let raw = materials[materialID]?["data"]["m_SavedProperties"] {
            for pair in raw["m_Colors"].array {
                guard pair.array.count == 2, let name = pair.array[0].string else { continue }
                let color = pair.array[1].color; values[name] = [color.x, color.y, color.z, color.w]
            }
            for pair in raw["m_TexEnvs"].array {
                guard pair.array.count == 2, let name = pair.array[0].string else { continue }
                let value = pair.array[1]; values[name + "_ST"] = [Float(value["m_Scale"]["x"].float(1)), Float(value["m_Scale"]["y"].float(1)), Float(value["m_Offset"]["x"].float()), Float(value["m_Offset"]["y"].float())]
            }
        }
        for (attribute, value) in pose.properties[id] ?? [:] where attribute.hasPrefix("material.") {
            let property = String(attribute.dropFirst("material.".count))
            let parts = property.split(separator: ".")
            if parts.count == 2, let axis = ["x": 0, "y": 1, "z": 2, "w": 3, "r": 0, "g": 1, "b": 2, "a": 3][String(parts[1])] {
                let name = String(parts[0]); var vector = batch.uniformOverrides[name] ?? values[name] ?? [0, 0, 0, 0]
                vector[axis] = Float(value); batch.uniformOverrides[name] = vector
            } else { batch.uniformOverrides[property] = [Float(value)] }
        }
    }
    static func flatten(_ matrix: simd_double4x4) -> [Float] {
        (0..<4).flatMap { column in (0..<4).map { Float(matrix[column][$0]) } }
    }
}
