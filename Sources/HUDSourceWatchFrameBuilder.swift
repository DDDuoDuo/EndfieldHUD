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
    let domain: HUDSourceWatchDomain
    private let renderer: HUDSourceMetalRenderer
    private let materials: [HUDSourceID: HUDSourceJSONValue]
    private var sprites: [HUDSourceID: HUDSourceImageGeometry.Sprite] = [:]
    private var textureSizes: [String: SIMD2<Float>] = ["__white": SIMD2(1, 1)]
    private var sourceMeshNames: [HUDSourceID: String] = [:]
    private var registeredDomainMeshes: Set<HUDSourceID> = []
    private var geometryKeys: [HUDSourceID: [Double]] = [:]
    private var textMeshes: [HUDSourceID: (key: [Double], mesh: HUDSourceTextGeometry.Mesh)] = [:]
    private let buttonIDs: Set<HUDSourceID>
    private struct SoftMask {
        let worldToUnit: simd_double4x4
        let textureID: String
        let textureST: [Float]
        let inner: [Float]
        let innerUV: [Float]
        let sliced: Bool
        func apply(to batch: inout HUDSourceMetalRenderer.Batch) {
            batch.uniformOverrides["_WorldToSoftMask"] = HUDSourceWatchFrameBuilder.flatten(
                simd_mul(worldToUnit, HUDSourceGeometry.doubleMatrix(batch.world)))
            batch.uniformOverrides["_SoftMaskTex_ST"] = textureST
            batch.uniformOverrides["_InnerSoftMask"] = inner
            batch.uniformOverrides["_InnerSoftMaskUV"] = innerUV
            batch.uniformOverrides["_SpriteIsSliced"] = [sliced ? 1 : 0]
            batch.textureOverrides["_SoftMaskTex"] = textureID
        }
    }

    init(document: HUDSourceWatchDocument, renderer: HUDSourceMetalRenderer,
         domain: HUDSourceWatchDomain? = nil) throws {
        self.document = document; self.renderer = renderer
        let sourceDomain = try domain ?? HUDSourceWatchDomain(resourceRoot: document.root.appendingPathComponent("Domain"))
        self.domain = sourceDomain
        text = try HUDSourceTextGeometry(document: document, additionalComponents: sourceDomain.components,
            additionalLabels: sourceDomain.labels, additionalMaterials: sourceDomain.materials)
        materials = Dictionary(uniqueKeysWithValues: document.materials["materials"].array.compactMap { record in
            record["id"].string.map { (HUDSourceID(rawValue: $0), record) }
        }).merging(sourceDomain.materials) { watch, _ in watch }
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
        var domainTextures: [String: HUDSourceJSONValue] = [:]
        for record in sourceDomain.textures["textures"].array {
            guard let id = record["texture_id"].string, renderer.containsTexture(named: id) else {
                throw HUDSourceError.invalid("Original Domain texture mip chain missing")
            }
            var raw = record.object; raw["id"] = .string(id)
            domainTextures[id] = .object(raw)
            textureSizes[id] = SIMD2(Float(record["width"].float()), Float(record["height"].float()))
        }
        for (component, sprite) in sourceDomain.spriteByComponent {
            guard let id = sprite["texture_id"].string, let texture = domainTextures[id] else {
                throw HUDSourceError.invalid("Unresolved original Domain Sprite texture")
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
        let layout = HUDSourceWatchLayout(document: document) { [weak self] id, _ in
            guard let self else { return nil }
            return try? self.text.preferredSize(on: id)
        }
        let report = try layout.apply(to: &pose, verticalNormalizedPosition: verticalNormalizedPosition, worldRoot: worldRoot)
        let resolved = try document.scene.resolve(overrides: pose.transforms)
        let alpha = document.inheritedAlpha(pose: pose)
        var batches: [(order: Int, sequence: Int, batch: HUDSourceMetalRenderer.Batch)] = []
        var hits: [(order: Int, sequence: Int, hit: Hit)] = []
        var diagnostics: [String] = []
        var softMasks: [HUDSourceID: SoftMask] = [:]
        for id in document.scene.traversalIDs where resolved[id]?.activeInHierarchy == true {
            guard document.component("UISoftMask", on: id) != nil else { continue }
            do { softMasks[id] = try softMask(on: id, resolved: resolved, worldRoot: worldRoot) }
            catch { diagnostics.append("Source soft mask \(document.scene.node(id)?.path ?? id.rawValue): \(error)") }
        }
        let cutNodes = document.scene.traversalIDs.filter { document.component("UIWatchPanelCut", on: $0) != nil }
        guard cutNodes.count == 1, let cut = resolved[cutNodes[0]],
              let watchInverse = HUDSourceGeometry.inverse(simd_mul(worldRoot, cut.worldMatrix)) else {
            throw HUDSourceError.invalid("Unresolved original UIWatchPanelCut world matrix")
        }
        let watchWorldToLocal = Self.flatten(watchInverse)
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
                if component.kind == "NonDrawingGraphic", let rect = n.rect,
                   component["m_RaycastTarget"].flag(), let buttonID = buttonAncestor(id), canReceiveInput(id) {
                    let padding = component["m_RaycastPadding"]
                    let hitRect = HUDSourceRect(origin: rect.origin + SIMD2(padding["x"].float(), padding["y"].float()),
                        size: rect.size - SIMD2(padding["x"].float() + padding["z"].float(), padding["y"].float() + padding["w"].float()))
                    let hitMasks = maskIDs.compactMap { mask -> (rect: HUDSourceRect, world: simd_double4x4)? in
                        guard let m = resolved[mask], let r = m.rect else { return nil }
                        return (r, simd_mul(worldRoot, m.worldMatrix))
                    }
                    hits.append((orders[id] ?? 0, sequence, Hit(graphicID: id, buttonID: buttonID,
                        rect: hitRect, world: world, masks: hitMasks))); sequence += 1
                    continue
                }
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
                color = Self.canvasVertexColor(color, alwaysGamma: document.component("Canvas", on: canvasID)?["m_VertexColorAlwaysGammaSpace"].flag() ?? false)
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
                let baseMaterial = materialID?.rawValue ?? "__ui_default"
                let maskable = document.component("UISoftMaskable", on: id) != nil
                let softMaskID = maskable ? nearestSoftMask(id) : nil
                let sourceSoftMask = softMaskID.flatMap { softMasks[$0] }
                if softMaskID != nil && sourceSoftMask == nil {
                    diagnostics.append("Unresolved original soft mask: " + n.node.path); continue
                }
                let material: String
                if let key = renderer.materialKey(named: baseMaterial, clipRect: clip != nil, alphaClip: false,
                    softMask: sourceSoftMask != nil) { material = key }
                else { diagnostics.append("Unresolved original material clipping variant: \(n.node.path)"); continue }
                var batch = HUDSourceMetalRenderer.Batch(mesh: meshName, material: material,
                    world: HUDSourceGeometry.floatMatrix(canvasWorld), color: color, textureOverrides: ["_MainTex": textureID])
                if let size = textureSizes[textureID] { batch.uniformOverrides["mainTexTexelSize"] = [1 / size.x, 1 / size.y, size.x, size.y] }
                if let clip {
                    batch.uniformOverrides["clipRect"] = clip
                    if let mask = maskIDs.last, let m = document.component("RectMask2D", on: mask) {
                        let softness = m["m_HGSoftness"]
                        batch.uniformOverrides["uiMaskHGSoftness"] = ["x", "y", "z", "w"].map { Float(softness[$0].float()) }
                    }
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
                // Native material refresh copies the base properties first,
                // then writes the six active soft-mask parameters.
                sourceSoftMask?.apply(to: &batch)
                batches.append((order, sequence, batch))
            }
            if let mesh = document.component("MeshFilter", on: id), let sourceID = mesh["m_Mesh"].targetID,
               let meshName = sourceMeshNames[sourceID], let render = document.component("MeshRenderer", on: id) {
                for material in render["m_Materials"].array {
                    guard let materialID = material.targetID, let name = materials[materialID]?["name"].string else { continue }
                    var batch = HUDSourceMetalRenderer.Batch(mesh: meshName, material: materialID.rawValue, world: HUDSourceGeometry.floatMatrix(world), color: SIMD4(repeating: 1))
                    batch.uniformOverrides["_WatchWorldToLocalMatrix"] = watchWorldToLocal
                    applyMaterialProperties(pose, on: id, materialID: materialID, to: &batch)
                    batches.append((Int(render["m_SortingOrder"].float()), sequence, batch)); sequence += 1
                }
            }
        }
        // Watch's serialized Region01/02/Spaceship placement is retained.
        // The generic RegionMap rotation tween is not called by Watch; its
        // -90 degree target must not be stacked onto this authored placement.
        guard let placement = document.scene.nodes.first(where: {
            $0.path.hasSuffix("/Map/RegionRoot/RegionMask/MoveRoot/" + domain.domainName)
        }), let region = resolved[placement.id] else {
            throw HUDSourceError.invalid("Unresolved original Domain placement")
        }
        if region.activeInHierarchy {
            let source = try domain.frame(domainWorld: simd_mul(worldRoot, region.worldMatrix), parentRect: region.rect)
            diagnostics.append("Domain selection: " + source.selectionPolicy)
            diagnostics.append(contentsOf: source.limitations)
            for instance in source.meshes {
                let mesh = instance.mesh, name = "domain/" + mesh.id.rawValue
                if registeredDomainMeshes.insert(mesh.id).inserted {
                    let positions = mesh.positions.map { SIMD4<Float>(Float($0[0]), Float($0[1]), Float($0[2]), 1) }
                    let uv = mesh.uv0.isEmpty ? Array(repeating: SIMD2<Float>(repeating: 0), count: positions.count)
                        : mesh.uv0.map { SIMD2<Float>(Float($0[0]), Float($0[1])) }
                    let normals = mesh.normals.map { SIMD3<Float>(Float($0[0]), Float($0[1]), Float($0[2])) }
                    let uv1 = mesh.uv1.map { SIMD2<Float>(Float($0[0]), Float($0[1])) }
                    let colors = mesh.colors.map { SIMD4<Float>(Float($0[0]), Float($0[1]), Float($0[2]), Float($0[3])) }
                    try renderer.registerGeometry(named: name, positions: positions, uv: uv, colors: colors,
                        indices: mesh.indices, normals: normals, uv1: uv1)
                }
                for (slot, materialID) in instance.materialIDs.enumerated() {
                    guard let materialID, !mesh.submeshes.isEmpty else { continue }
                    // Unity repeats the final submesh for surplus material
                    // slots. Exported indices already include baseVertex.
                    let submesh = mesh.submeshes[min(slot, mesh.submeshes.count - 1)]
                    guard mesh.index_format == 0 || mesh.index_format == 1 else {
                        throw HUDSourceError.invalid("Unknown source mesh index format")
                    }
                    let first = Int(submesh["firstByte"].float()) / (mesh.index_format == 1 ? 4 : 2)
                    let count = Int(submesh["indexCount"].float())
                    guard Int(submesh["topology"].float()) == 0, first >= 0, count > 0,
                          first + count <= mesh.indices.count else {
                        throw HUDSourceError.invalid("Unsupported original Domain submesh")
                    }
                    var batch = HUDSourceMetalRenderer.Batch(mesh: name, material: materialID.rawValue,
                        world: HUDSourceGeometry.floatMatrix(instance.worldMatrix), color: SIMD4(repeating: 1), indexRange: first..<(first + count))
                    for (uniform, value) in instance.sourceUniformOverrides {
                        if let number = value.number { batch.uniformOverrides[uniform] = [Float(number)] }
                    }
                    batch.uniformOverrides["_WatchWorldToLocalMatrix"] = watchWorldToLocal
                    batches.append((instance.sourceRuntimeSortingOrder, sequence, batch)); sequence += 1
                }
            }
            let regionMaskID = nearestSoftMask(placement.id)
            for (order, batch) in try domainUIBatches(source, watchWorldToLocal: watchWorldToLocal,
                softMask: regionMaskID.flatMap { softMasks[$0] }, expectsSoftMask: regionMaskID != nil,
                diagnostics: &diagnostics) {
                batches.append((order, sequence, batch)); sequence += 1
            }
        }
        batches.sort { $0.order == $1.order ? $0.sequence < $1.sequence : $0.order < $1.order }
        hits.sort { $0.order == $1.order ? $0.sequence < $1.sequence : $0.order < $1.order }
        return Frame(resolved: resolved, batches: batches.map(\.batch), hits: hits.map(\.hit), layoutReport: report,
            diagnostics: diagnostics + pose.unboundPaths.sorted().map { "Unbound source curve: " + $0 }
                + pose.unregisteredBindings.sorted().map { "Ignored unregistered native animation binding: " + $0 })
    }

    private func domainUIBatches(_ frame: HUDSourceWatchDomain.Frame, watchWorldToLocal: [Float],
                                 softMask: SoftMask?, expectsSoftMask: Bool,
                                 diagnostics: inout [String]) throws -> [(Int, HUDSourceMetalRenderer.Batch)] {
        var result: [(Int, HUDSourceMetalRenderer.Batch)] = []
        var canvases: [HUDSourceID: HUDSourceID] = [:], orders: [HUDSourceID: Int] = [:], inheritedAlpha: [HUDSourceID: Float] = [:]
        for id in domain.scene.traversalIDs {
            guard let node = frame.nodes[id], node.activeInHierarchy else { continue }
            let components = domain.components[id] ?? [], parent = node.node.parentID
            let canvas = components.first { $0.kind == "Canvas" && $0.enabled }
            canvases[id] = canvas != nil ? id : parent.flatMap { canvases[$0] }
            orders[id] = canvas?["m_OverrideSorting"].flag() == true ? Int(canvas!["m_SortingOrder"].float()) : parent.flatMap { orders[$0] } ?? 0
            var alpha = parent.flatMap { inheritedAlpha[$0] } ?? 1
            for group in components where group.kind == "CanvasGroup" && group.enabled { alpha *= Float(group["m_Alpha"].float(1)) }
            inheritedAlpha[id] = alpha
            guard let canvasID = canvases[id], let canvasNode = frame.nodes[canvasID], let rect = node.rect,
                  let inverse = HUDSourceGeometry.inverse(canvasNode.worldMatrix) else { continue }
            let toCanvas = simd_mul(inverse, node.worldMatrix)
            for component in components where component.enabled && ["UIImage", "Image", "UIText"].contains(component.kind) {
                let localPositions: [SIMD4<Float>], uv: [SIMD2<Float>], indices: [UInt32]
                var normals: [SIMD3<Float>] = [], uv1: [SIMD2<Float>] = []
                var materialID = component["m_Material"].targetID
                var color = component["m_Color"].color
                let texture: String
                var key = [rect.origin.x, rect.origin.y, rect.size.x, rect.size.y] + Self.flatten(toCanvas).map(Double.init)
                if component.kind == "UIText" {
                    guard let literal = text.literal(on: id), !literal.isEmpty else { continue }
                    let sdfScale = simd_length(SIMD3(node.worldMatrix.columns.1.x, node.worldMatrix.columns.1.y, node.worldMatrix.columns.1.z))
                    do {
                        let mesh = try localText(on: id, rect: rect, sdfScale: sdfScale)
                        localPositions = mesh.positions; uv = mesh.uv; indices = mesh.indices
                        normals = mesh.normals; uv1 = mesh.uv2; materialID = mesh.materialID
                        color = mesh.color; texture = mesh.atlasID.rawValue; key.append(sdfScale)
                    } catch { diagnostics.append(node.node.path + ": " + String(describing: error)); continue }
                } else {
                    let mesh = try HUDSourceImageGeometry.build(image: component, sprite: sprites[component.id], rect: rect,
                        pivot: node.node.transform.rect?.pivot.simd ?? SIMD2(repeating: 0.5))
                    localPositions = mesh.positions; uv = mesh.uv; indices = mesh.indices
                    texture = sprites[component.id]?.textureID ?? "__white"
                }
                guard !indices.isEmpty else { continue }
                let name = "domain-ui/" + component.id.rawValue
                if geometryKeys[component.id] != key {
                    let positions = localPositions.map { p -> SIMD4<Float> in
                        let v: SIMD4<Double> = simd_mul(toCanvas, SIMD4<Double>(Double(p.x), Double(p.y), Double(p.z), Double(p.w)))
                        return SIMD4(Float(v.x), Float(v.y), Float(v.z), Float(v.w))
                    }
                    let normalMatrix = simd_double3x3(columns: (SIMD3(toCanvas.columns.0.x, toCanvas.columns.0.y, toCanvas.columns.0.z),
                        SIMD3(toCanvas.columns.1.x, toCanvas.columns.1.y, toCanvas.columns.1.z), SIMD3(toCanvas.columns.2.x, toCanvas.columns.2.y, toCanvas.columns.2.z))).inverse.transpose
                    let bakedNormals = normals.map { normal -> SIMD3<Float> in
                        let v: SIMD3<Double> = simd_normalize(simd_mul(normalMatrix, SIMD3<Double>(Double(normal.x), Double(normal.y), Double(normal.z))))
                        return SIMD3(Float(v.x), Float(v.y), Float(v.z))
                    }
                    try renderer.registerGeometry(named: name, positions: positions, uv: uv, indices: indices, normals: bakedNormals, uv1: uv1)
                    geometryKeys[component.id] = key
                }
                let base = materialID?.rawValue ?? "__ui_default"
                let maskable = components.contains { $0.kind == "UISoftMaskable" && $0.enabled }
                if maskable && expectsSoftMask && softMask == nil {
                    diagnostics.append("Unresolved original Domain soft mask: " + node.node.path); continue
                }
                guard let material = renderer.materialKey(named: base, clipRect: false, alphaClip: false,
                    softMask: maskable && softMask != nil) else {
                    diagnostics.append("Unresolved original Domain UI material: " + node.node.path); continue
                }
                for axis in 0..<4 { color[axis] = (min(1, max(0, color[axis])) * 255).rounded(.toNearestOrEven) / 255 }
                color = Self.canvasVertexColor(color, alwaysGamma: domain.components[canvasID]?.first(where: { $0.kind == "Canvas" })?["m_VertexColorAlwaysGammaSpace"].flag() ?? false)
                color.w *= alpha
                var batch = HUDSourceMetalRenderer.Batch(mesh: name, material: material,
                    world: HUDSourceGeometry.floatMatrix(canvasNode.worldMatrix), color: color, textureOverrides: ["_MainTex": texture])
                if maskable { softMask?.apply(to: &batch) }
                batch.uniformOverrides["_WatchWorldToLocalMatrix"] = watchWorldToLocal
                if let size = textureSizes[texture] { batch.uniformOverrides["mainTexTexelSize"] = [1 / size.x, 1 / size.y, size.x, size.y] }
                result.append((orders[id] ?? 0, batch))
            }
        }
        return result
    }
    private func nearestSoftMask(_ id: HUDSourceID) -> HUDSourceID? {
        var current: HUDSourceID? = id
        while let node = current {
            if let mask = document.components[node]?.first(where: { $0.kind == "UISoftMask" }) {
                // Native GetComponentInParent finds the closest component;
                // a disabled closest mask returns the base material.
                return mask.enabled ? node : nil
            }
            current = document.scene.node(node)?.parentID
        }
        return nil
    }
    private func softMask(on id: HUDSourceID, resolved: [HUDSourceID: HUDSourceResolvedNode],
                          worldRoot: simd_double4x4) throws -> SoftMask {
        guard let node = resolved[id], let rect = node.rect,
              let image = document.components[id]?.first(where: { $0.kind == "UIImage" }),
              let sprite = document.spriteByComponent[image.id],
              let textureID = sprite["texture"]["id"].string,
              let textureSize = textureSizes[textureID] else {
            throw HUDSourceError.invalid("Missing original UIImage/Sprite soft mask binding")
        }
        // Native _UpdateParam measures GetWorldCorners in the outermost Canvas,
        // builds [BR-BL, TL-BL, cross, BL], and uploads its inverse. Convert that
        // matrix into world space once, then into each batch's Canvas space.
        var outerCanvasID: HUDSourceID?, ancestor: HUDSourceID? = id
        while let current = ancestor {
            if document.component("Canvas", on: current) != nil { outerCanvasID = current }
            ancestor = document.scene.node(current)?.parentID
        }
        guard let outerCanvasID, let outerCanvas = resolved[outerCanvasID],
              let inverseOuter = HUDSourceGeometry.inverse(simd_mul(worldRoot, outerCanvas.worldMatrix)) else {
            throw HUDSourceError.invalid("Unresolved original outer Canvas for soft mask")
        }
        let toOuter = simd_mul(inverseOuter, simd_mul(worldRoot, node.worldMatrix))
        // HUDSourceRect.corners is BL,BR,TR,TL. Unity GetWorldCorners is BL,TL,TR,BR.
        let corners: [SIMD4<Double>] = [0, 3, 2, 1].map {
            let p = rect.corners[$0]; return simd_mul(toOuter, SIMD4<Double>(p.x, p.y, p.z, 1))
        }
        let x = corners[3] - corners[0], y = corners[1] - corners[0]
        let z = simd_cross(SIMD3(x.x, x.y, x.z), SIMD3(y.x, y.y, y.z))
        let basis = simd_double4x4(columns: (SIMD4(x.x, x.y, x.z, 0), SIMD4(y.x, y.y, y.z, 0),
            SIMD4(z.x, z.y, z.z, 0), corners[0]))
        guard let inverseBasis = HUDSourceGeometry.inverse(basis), textureSize.x > 0, textureSize.y > 0 else {
            throw HUDSourceError.invalid("Degenerate original soft mask geometry")
        }
        // The source uses Sprite.rect and image.mainTexture. The UIImage's own
        // color alpha is not a mask property: the map mask intentionally has 0.
        let sourceRect = sprite["raw_sprite"]["m_Rect"]
        let st = [Float(sourceRect["width"].float()) / textureSize.x,
            Float(sourceRect["height"].float()) / textureSize.y,
            Float(sourceRect["x"].float()) / textureSize.x, Float(sourceRect["y"].float()) / textureSize.y]
        var inner = [Float](repeating: 0, count: 4), innerUV = inner
        var sliced = false
        if Int(image["m_Type"].float()) == 1, let sourceSprite = sprites[image.id], sourceSprite.border != .zero {
            // All selected source Canvases have pixelPerfect=false; the native
            // GetPixelAdjustedRect therefore retains RectTransform.rect.
            guard document.component("Canvas", on: outerCanvasID)?["m_PixelPerfect"].flag() != true else {
                throw HUDSourceError.invalid("Unverified pixel-adjusted source soft mask")
            }
            var referencePPU = 100.0, current: HUDSourceID? = id
            while let ancestorID = current {
                if let scaler = document.component("CanvasScaler", on: ancestorID) {
                    referencePPU = scaler["m_ReferencePixelsPerUnit"].float(100); break
                }
                current = document.scene.node(ancestorID)?.parentID
            }
            let ppu = sourceSprite.pixelsPerUnit / referencePPU * image["m_PixelsPerUnitMultiplier"].float(1)
            guard ppu > 0 else { throw HUDSourceError.invalid("Invalid original sliced mask pixelsPerUnit") }
            var border = sourceSprite.border / ppu
            for axis in 0..<2 {
                let sum = border[axis] + border[axis + 2]
                if sum > rect.size[axis] && sum != 0 {
                    let ratio = rect.size[axis] / sum; border[axis] *= ratio; border[axis + 2] *= ratio
                }
            }
            let minimum = simd_mul(toOuter, SIMD4<Double>(rect.origin.x + border.x, rect.origin.y + border.y, 0, 1))
            let maximum = simd_mul(toOuter, SIMD4<Double>(rect.origin.x + rect.size.x - border.z,
                rect.origin.y + rect.size.y - border.w, 0, 1))
            let width = simd_length(SIMD3(x.x, x.y, x.z)), height = simd_length(SIMD3(y.x, y.y, y.z))
            inner = [Float((minimum.x - corners[0].x) / width), Float((minimum.y - corners[0].y) / height),
                Float((maximum.x - corners[0].x) / width), Float((maximum.y - corners[0].y) / height)]
            let uvSize = SIMD2(sourceSprite.outer.z - sourceSprite.outer.x, sourceSprite.outer.w - sourceSprite.outer.y)
            let uv = sourceSprite.inner
            let tiny = Double(Float.leastNonzeroMagnitude * 8)
            // Native normalization returns the original UV pair when either
            // dimension is approximately zero; it does not divide one axis.
            if abs(uvSize.x) < tiny || abs(uvSize.y) < tiny {
                innerUV = [Float(uv.x), Float(uv.y), Float(uv.z), Float(uv.w)]
            } else {
                innerUV = [Float((uv.x - sourceSprite.outer.x) / uvSize.x), Float((uv.y - sourceSprite.outer.y) / uvSize.y),
                    Float((uv.z - sourceSprite.outer.x) / uvSize.x), Float((uv.w - sourceSprite.outer.y) / uvSize.y)]
            }
            sliced = true
        }
        return SoftMask(worldToUnit: simd_mul(inverseBasis, inverseOuter), textureID: textureID,
            textureST: st, inner: inner, innerUV: innerUV, sliced: sliced)
    }
    /// The original PlayerSettings.m_ActiveColorSpace is Linear. Canvas keeps
    /// alpha unchanged and converts Color32 RGB unless its explicit gamma flag
    /// is set. Material colors are independent of this vertex-stream policy.
    static func canvasVertexColor(_ color: SIMD4<Float>, alwaysGamma: Bool) -> SIMD4<Float> {
        guard !alwaysGamma else { return color }
        var value = color
        for axis in 0..<3 {
            let c = color[axis]
            value[axis] = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4)
        }
        return value
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
        var animated: Set<String> = []
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
                vector[axis] = Float(value); batch.uniformOverrides[name] = vector; animated.insert(name)
            } else { batch.uniformOverrides[property] = [Float(value)]; animated.insert(property) }
        }
        // Compose all source channels before color conversion, so a second
        // channel cannot accidentally read an already-linearized partial value.
        for name in animated {
            guard let value = batch.uniformOverrides[name] else { continue }
            batch.uniformOverrides[name] = renderer.gpuMaterialValue(value, property: name, materialKey: batch.material)
        }
    }
    static func flatten(_ matrix: simd_double4x4) -> [Float] {
        (0..<4).flatMap { column in (0..<4).map { Float(matrix[column][$0]) } }
    }
}
