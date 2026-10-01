import Foundation
import simd

/// The actual Region3D/Level3D scene graph, with original geometry and material
/// references. This does not invent the user's current level or unlock flags.
final class HUDSourceWatchDomain {
    struct Mesh: Decodable {
        let id: HUDSourceID
        let name: String
        let positions: [[Double]]
        let normals: [[Double]]
        let tangents: [[Double]]
        let colors: [[Double]]
        let uv0: [[Double]]
        let uv1: [[Double]]
        let uv2: [[Double]]
        let uv3: [[Double]]
        let indices: [UInt32]
        let index_format: Int
        let submeshes: [HUDSourceJSONValue]
    }
    struct MeshBatch {
        let nodeID: HUDSourceID
        let path: String
        let mesh: Mesh
        /// Slot order corresponds to the original MeshRenderer.m_Materials.
        let materialIDs: [HUDSourceID?]
        let worldMatrix: simd_double4x4
        let renderer: HUDSourceJSONValue
        let levelID: String?
        /// RegionMapSetting._RefreshMaterials sets this on every loaded
        /// renderer. Color selection is a separate explicit runtime action.
        let sourceUniformOverrides: [String: HUDSourceJSONValue]
        /// UISortingOrder.Renderer writes its absolute offset, rather than
        /// adding the owning panel's order. This handles the renderer's own
        /// component; overlapping ancestor writers require lifecycle ordering.
        let sourceRuntimeSortingOrder: Int
    }
    struct Frame {
        let meshes: [MeshBatch]
        /// Also exposes UIImage/UIText nodes so the common source UI renderer
        /// can use the same original layout and per-node material logic.
        let nodes: [HUDSourceID: HUDSourceResolvedNode]
        let selectionPolicy: String
        let limitations: [String]
    }
    private struct Instance {
        let nodeID: HUDSourceID
        let meshID: HUDSourceID
        let materials: [HUDSourceID?]
        let renderer: HUDSourceJSONValue
        let levelID: String?
    }
    struct Attachment {
        let scene: HUDSourceScene
        let parentID: HUDSourceID
    }
    let root: URL
    let domainName: String
    let scene: HUDSourceScene
    let components: [HUDSourceID: [HUDSourceWatchComponent]]
    let materials: [HUDSourceID: HUDSourceJSONValue]
    let sprites: HUDSourceJSONValue
    let labels: HUDSourceJSONValue
    let textures: HUDSourceJSONValue
    let spriteByComponent: [HUDSourceID: HUDSourceJSONValue]
    let levelRects: [String: SIMD4<Double>]
    let loadedLevelIDs: Set<String>
    let sourceNormalRotation: HUDSourceQuaternion
    let playerMarkerID: HUDSourceID?
    let modelNodeID: HUDSourceID?
    let uiNodeID: HUDSourceID?
    let normalOuterColor: SIMD4<Float>
    let selectedOuterColor: SIMD4<Float>
    let selectionPolicy: String
    private let meshes: [HUDSourceID: Mesh]
    private let instances: [Instance]
    private let levelRootIDs: [HUDSourceID: String]

    /// nil loads all declared source levels for a source-asset reference view.
    /// It is intentionally reported as such. A game-state equivalent view must
    /// pass the actual loaded levels, derived from current level/player state.
    init(resourceRoot: URL? = nil, domainName: String = "Region01",
         loadedLevelIDs requestedLevels: Set<String>? = nil) throws {
        guard let root = resourceRoot ?? HUDResources.url(for: "WatchSource/Scene/Domain") else {
            throw HUDSourceError.invalid("Source Domain resources are missing")
        }
        self.root = root; self.domainName = domainName
        let decoder = HUDSourceJSON.decoder()
        func data(_ file: String) throws -> Data { try Data(contentsOf: root.appendingPathComponent(file)) }
        func json(_ file: String) throws -> HUDSourceJSONValue {
            try decoder.decode(HUDSourceJSONValue.self, from: data(file))
        }
        let manifest = try json("manifest.json")
        guard let base = manifest["scenes"].array.first(where: {
            $0["name"].string?.lowercased() == domainName.lowercased() && !$0["is_level_resource"].flag()
        }), let baseFile = base["file"].string else {
            throw HUDSourceError.invalid("Missing source Domain \(domainName)")
        }
        let baseData = try data(baseFile)
        let baseScene = try decoder.decode(HUDSourceScene.self, from: baseData)
        let baseJSON = try decoder.decode(HUDSourceJSONValue.self, from: baseData)
        let bindings = try json("level-bindings.json")["level_bindings"].array.filter {
            $0["domain_scene"].string?.lowercased() == domainName.lowercased()
        }
        let declared = Set(bindings.compactMap { $0["level_id"].string })
        guard requestedLevels.map({ $0.isSubset(of: declared) }) ?? true else {
            throw HUDSourceError.invalid("Requested level is absent from source Domain cfg")
        }
        loadedLevelIDs = requestedLevels ?? declared
        selectionPolicy = requestedLevels == nil ? "all-declared-source-levels-reference" : "explicit-loaded-levels"
        var attachments: [Attachment] = [], metadataNodes = baseJSON["nodes"].array
        var levelByNode: [HUDSourceID: String] = [:], rects: [String: SIMD4<Double>] = [:]
        var rootLevels: [HUDSourceID: String] = [:]
        // Original cfg order and ui/model/ground Load order are retained.
        for binding in bindings {
            guard let level = binding["level_id"].string else { continue }
            let rect = binding["ui_rect"]
            rects[level] = SIMD4(rect["x"].float(), rect["y"].float(), rect["z"].float(), rect["w"].float())
            guard loadedLevelIDs.contains(level) else { continue }
            for kind in ["ui", "building", "ground"] {
                let resource = binding["bindings"][kind]
                guard let file = resource["scene_file"].string else { continue }
                guard let parent = resource["parent_reference"]["target_id"].string,
                      baseScene.node(HUDSourceID(rawValue: parent)) != nil else {
                    throw HUDSourceError.invalid("Unresolved source Level3D parent")
                }
                let bytes = try data(file), child = try decoder.decode(HUDSourceScene.self, from: bytes)
                attachments.append(Attachment(scene: child, parentID: HUDSourceID(rawValue: parent)))
                rootLevels[child.rootID] = level
                metadataNodes.append(contentsOf: try decoder.decode(HUDSourceJSONValue.self, from: bytes)["nodes"].array)
                for node in child.nodes { levelByNode[node.id] = level }
            }
        }
        levelRects = rects
        let joinedScene = try Self.join(base: baseScene, attachments: attachments)
        scene = joinedScene; levelRootIDs = rootLevels
        var componentMap: [HUDSourceID: [HUDSourceWatchComponent]] = [:]
        for node in metadataNodes {
            guard let id = node["id"].string else { continue }
            let list = node["components"].array.map { raw in
                HUDSourceWatchComponent(id: HUDSourceID(rawValue: raw["id"].string ?? ""),
                    type: raw["type"].string ?? "", script: raw["script"].string, data: raw["data"].object)
            }
            componentMap[HUDSourceID(rawValue: id)] = list
        }
        components = componentMap
        let settings = components[baseScene.rootID]?.first { $0.kind == "RegionMapSetting" }
        sourceNormalRotation = settings.map { Self.unityEuler($0["_moveFinalRotation"].vector3) } ?? .identity
        playerMarkerID = settings?["_uiPlayerMark"].targetID
        modelNodeID = settings?["_modelNode"].targetID
        uiNodeID = settings?["_uiNode"].targetID
        normalOuterColor = settings?["_normalModelOuterColor"].color ?? SIMD4(1, 1, 1, 1)
        selectedOuterColor = settings?["_selectedModelOuterColor"].color ?? SIMD4(1, 1, 1, 1)
        sprites = try json("sprites.json"); labels = try json("texts.json"); textures = try json("texture-mips.json")
        var spriteMap: [HUDSourceID: HUDSourceJSONValue] = [:]
        for sprite in sprites["sprites"].array {
            for binding in sprite["bindings"].array {
                if let id = binding["component_id"].string { spriteMap[HUDSourceID(rawValue: id)] = sprite }
            }
        }
        spriteByComponent = spriteMap
        var materialMap: [HUDSourceID: HUDSourceJSONValue] = [:]
        for material in try json("materials.json")["materials"].array {
            if let id = material["id"].string { materialMap[HUDSourceID(rawValue: id)] = material }
        }
        materials = materialMap
        var meshMap: [HUDSourceID: Mesh] = [:]
        // Load only meshes actually referenced by this assembled Domain.
        let sourceInstances = try json("instances.json")["instances"].array.filter {
            $0["node_id"].string.map { joinedScene.node(HUDSourceID(rawValue: $0)) != nil } ?? false
        }
        let wantedMeshes = Set(sourceInstances.compactMap { $0["mesh_id"].string })
        for descriptor in manifest["meshes"].array {
            guard let id = descriptor["id"].string, wantedMeshes.contains(id), let file = descriptor["file"].string else { continue }
            let mesh = try decoder.decode(Mesh.self, from: data(file))
            guard mesh.id.rawValue == id, mesh.positions.allSatisfy({ $0.count >= 3 && $0.allSatisfy(\.isFinite) }),
                  mesh.indices.allSatisfy({ Int($0) < mesh.positions.count }) else {
                throw HUDSourceError.invalid("Invalid source Domain mesh")
            }
            meshMap[mesh.id] = mesh
        }
        meshes = meshMap
        var instanceList: [Instance] = []
        for raw in sourceInstances {
            guard let node = raw["node_id"].string, let mesh = raw["mesh_id"].string,
                  meshMap[HUDSourceID(rawValue: mesh)] != nil else {
                throw HUDSourceError.invalid("Unresolved source Domain mesh instance")
            }
            let materialIDs = raw["material_ids"].array.map { $0.string.map { HUDSourceID(rawValue: $0) } }
            guard materialIDs.compactMap({ $0 }).allSatisfy({ materialMap[$0] != nil }) else {
                throw HUDSourceError.invalid("Unresolved source Domain material")
            }
            let nodeID = HUDSourceID(rawValue: node)
            instanceList.append(Instance(nodeID: nodeID, meshID: HUDSourceID(rawValue: mesh), materials: materialIDs,
                renderer: raw["renderer_data"], levelID: levelByNode[nodeID]))
        }
        // Stable scene traversal; dictionary or global asset order must not
        // reorder duplicated mesh/material instances within the original graph.
        let order = Dictionary(uniqueKeysWithValues: joinedScene.traversalIDs.enumerated().map { ($1, $0) })
        instances = instanceList.sorted { order[$0.nodeID, default: 0] < order[$1.nodeID, default: 0] }
    }

    /// Instantiate(origin, parent, false): reparent only the source root. All
    /// local transforms, child order and active flags remain source values.
    static func join(base: HUDSourceScene, attachments: [Attachment]) throws -> HUDSourceScene {
        var addedChildren: [HUDSourceID: [HUDSourceID]] = [:], roots: [HUDSourceID: HUDSourceID] = [:]
        var added: [HUDSourceNode] = []
        for attachment in attachments {
            guard base.node(attachment.parentID) != nil else { throw HUDSourceError.invalid("Missing Domain attachment parent") }
            addedChildren[attachment.parentID, default: []].append(attachment.scene.rootID)
            guard roots.updateValue(attachment.parentID, forKey: attachment.scene.rootID) == nil else {
                throw HUDSourceError.invalid("Repeated Domain attachment source root")
            }
            added.append(contentsOf: attachment.scene.nodes)
        }
        let nodes = (base.nodes + added).map { node in
            HUDSourceNode(id: node.id, path: node.path, name: node.name,
                parentID: roots[node.id] ?? node.parentID,
                childIDs: node.childIDs + (addedChildren[node.id] ?? []), active: node.active, transform: node.transform)
        }
        return try HUDSourceScene(rootID: base.rootID, nodes: nodes)
    }

    func frame(domainWorld: simd_double4x4, parentRect: HUDSourceRect? = nil,
               overrides: [HUDSourceID: HUDSourceTransformOverride] = [:],
               unlockedLevelIDs: Set<String>? = nil, showType: Int = 1) throws -> Frame {
        guard HUDSourceGeometry.isFinite(domainWorld) else { throw HUDSourceError.invalid("Invalid external Domain transform") }
        var overrides = overrides
        // _InitUI applies the caller's unlock flags to all ui/building/ground
        // instances. No missing account state is silently replaced by unlock-all.
        if let unlocked = unlockedLevelIDs {
            for (rootID, level) in levelRootIDs {
                var value = overrides[rootID] ?? HUDSourceTransformOverride()
                value.active = unlocked.contains(level); overrides[rootID] = value
            }
        }
        let resolved = try scene.resolve(rootParentRect: parentRect, overrides: overrides)
        var nodes: [HUDSourceID: HUDSourceResolvedNode] = [:]
        for (id, node) in resolved {
            nodes[id] = HUDSourceResolvedNode(node: node.node, localMatrix: node.localMatrix,
                worldMatrix: domainWorld * node.worldMatrix, rect: node.rect, activeInHierarchy: node.activeInHierarchy)
        }
        var batches: [MeshBatch] = []
        for instance in instances {
            guard let node = nodes[instance.nodeID], node.activeInHierarchy,
                  instance.renderer["m_Enabled"].flag(true), let mesh = meshes[instance.meshID] else { continue }
            batches.append(MeshBatch(nodeID: instance.nodeID, path: node.node.path, mesh: mesh,
                materialIDs: instance.materials, worldMatrix: node.worldMatrix, renderer: instance.renderer, levelID: instance.levelID,
                sourceUniformOverrides: ["_RegionMapEditor": .number(showType == 1 ? 0 : 1)],
                sourceRuntimeSortingOrder: Self.rendererSortingOrder(renderer: instance.renderer,
                    ownComponents: components[instance.nodeID] ?? [])))
        }
        return Frame(meshes: batches, nodes: nodes, selectionPolicy: selectionPolicy,
            limitations: ["Current level, player marker, unlock/selection and load completion require explicit caller state.",
                "The source -90 degree world rotation tween targets loadedRegionTransform; its invocation in Watch is not established and is not applied here.",
                "Renderer own UISortingOrder offsets are applied; conflicting ancestor writer lifecycle order remains unverified.",
                "Unity scheduling and pixel-identical engine rendering remain unverified."])
    }

    /// Native UISortingOrder.SetOrder(0x18359c160), Renderer branch
    /// 0x18359c254..0x18359c30a. Canvas and Particle use different rules.
    static func rendererSortingOrder(renderer: HUDSourceJSONValue,
                                     ownComponents: [HUDSourceWatchComponent]) -> Int {
        let source = Int(renderer["m_SortingOrder"].number ?? 0)
        guard let order = ownComponents.first(where: {
            $0.kind == "UISortingOrder" && $0["_renderType"].number == 0
        }), let offset = order["_sortingOrderOffset"].number else { return source }
        return Int(offset)
    }

    /// Original Watch InitData uses circle/rectangle intersection after its
    /// center and radius have been converted into uiRoot local coordinates.
    func sourceLoadedLevels(currentLevelID: String, centerInUIRoot: SIMD2<Double>, radiusInUIRoot: Double) -> Set<String> {
        var result = Set<String>()
        if levelRects[currentLevelID] != nil { result.insert(currentLevelID) }
        for (level, rect) in levelRects where Self.intersects(rect, center: centerInUIRoot, radius: radiusInUIRoot) {
            result.insert(level)
        }
        return result
    }
    static func intersects(_ rect: SIMD4<Double>, center: SIMD2<Double>, radius: Double) -> Bool {
        guard radius.isFinite, radius >= 0, center.x.isFinite, center.y.isFinite else { return false }
        let closest = SIMD2(min(max(center.x, rect.x), rect.z), min(max(center.y, rect.y), rect.w))
        let delta = center - closest
        return simd_dot(delta, delta) <= radius * radius
    }
    static func iconAnchors(mapPosition: SIMD3<Double>, uiRect: SIMD4<Double>) throws -> SIMD2<Double> {
        let size = SIMD2(uiRect.z - uiRect.x, uiRect.w - uiRect.y)
        guard size.x != 0, size.y != 0 else { throw HUDSourceError.invalid("Degenerate Domain uiRect") }
        let center = SIMD2(uiRect.x + uiRect.z, uiRect.y + uiRect.w) / 2
        return (SIMD2(mapPosition.x, mapPosition.z) - center) / size + SIMD2(repeating: 0.5)
    }
    static func moveToPlayer(playerWorld: SIMD3<Double>, centerWorld: simd_double4x4) throws -> SIMD3<Double> {
        guard let inverse = HUDSourceGeometry.inverse(centerWorld) else { throw HUDSourceError.invalid("Invalid source map center transform") }
        let point = inverse * SIMD4(playerWorld.x, playerWorld.y, playerWorld.z, 1)
        return SIMD3(-point.x / point.w, -point.y / point.w, 0)
    }
    private static func unityEuler(_ degrees: SIMD3<Double>) -> HUDSourceQuaternion {
        // Unity Euler applies Z then X then Y; the source only has X=-90 here.
        let radians = degrees * (.pi / 180)
        let q = simd_quatd(angle: radians.y, axis: SIMD3(0, 1, 0))
            * simd_quatd(angle: radians.x, axis: SIMD3(1, 0, 0))
            * simd_quatd(angle: radians.z, axis: SIMD3(0, 0, 1))
        return HUDSourceQuaternion(q.imag.x, q.imag.y, q.imag.z, q.real)
    }
}
