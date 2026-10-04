import Foundation
import simd

/// Unknown source render fields remain available without rounding asset IDs.
/// The exporter stores every PPtr and unsafe integer as a decimal string.
enum HUDSourceJSONValue: Decodable {
    case object([String: HUDSourceJSONValue]), array([HUDSourceJSONValue])
    case string(String), number(Double), bool(Bool), null
    init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        if c.decodeNil() { self = .null }
        // Most source values are numbers or strings. Probe those before Bool
        // to avoid an extra decoding error for each one. String must stay
        // before Double: the shared decoder accepts nonfinite string sentinels.
        else if let v = try? c.decode(String.self) { self = .string(v) }
        else if let v = try? c.decode(Double.self) { self = .number(v) }
        else if let v = try? c.decode(Bool.self) { self = .bool(v) }
        else if let v = try? c.decode([HUDSourceJSONValue].self) { self = .array(v) }
        else { self = .object(try c.decode([String: HUDSourceJSONValue].self)) }
    }
    var object: [String: HUDSourceJSONValue] { if case .object(let v) = self { return v }; return [:] }
    var array: [HUDSourceJSONValue] { if case .array(let v) = self { return v }; return [] }
    var string: String? { if case .string(let v) = self { return v }; return nil }
    var number: Double? { if case .number(let v) = self { return v }; return nil }
    func float(_ fallback: Double = 0) -> Double { number ?? fallback }
    func flag(_ fallback: Bool = false) -> Bool {
        if case .bool(let v) = self { return v }
        if case .number(let v) = self { return v != 0 }
        return fallback
    }
    subscript(_ key: String) -> HUDSourceJSONValue { object[key] ?? .null }
    var targetID: HUDSourceID? { self["target_id"].string.map { HUDSourceID(rawValue: $0) } }
    var vector2: SIMD2<Double> { SIMD2(self["x"].float(), self["y"].float()) }
    var vector3: SIMD3<Double> { SIMD3(self["x"].float(), self["y"].float(), self["z"].float()) }
    var color: SIMD4<Float> {
        SIMD4(Float(self["r"].float(1)), Float(self["g"].float(1)), Float(self["b"].float(1)), Float(self["a"].float(1)))
    }
}

struct HUDSourceWatchComponent: Decodable {
    let id: HUDSourceID
    let type: String
    let script: String?
    let data: [String: HUDSourceJSONValue]
    var kind: String { script ?? type }
    subscript(_ key: String) -> HUDSourceJSONValue { data[key] ?? .null }
    var enabled: Bool { self["m_Enabled"].flag(true) }
}

struct HUDSourceWatchButton: Decodable {
    struct Label: Decodable {
        let nodeID: HUDSourceID
        let textID: String
        let literal: String?
        enum CodingKeys: String, CodingKey { case nodeID = "node_id", textID = "text_id", literal = "cn_literal" }
    }
    let nodeID: HUDSourceID
    let path: String
    let labels: [Label]
    enum CodingKeys: String, CodingKey { case nodeID = "node_id", path, labels }
    var label: Label? { labels.first { $0.textID != "ui_common_new_eng" && $0.literal != nil } }
}

final class HUDSourceWatchDocument {
    private static let desktopCache = HUDSourceDesktopDocumentCache()

    /// Only this profile is shared: its remaining members are immutable value
    /// trees, and the mutable game-widget owner is never constructed.
    static func desktop(resourceRoot: URL? = nil) throws -> HUDSourceWatchDocument {
        try desktopCache.document(resourceRoot: resourceRoot)
    }

    static func prewarmDesktop() { desktopCache.prewarm() }

    static func clearDesktopCacheForVerification() { desktopCache.clear() }

    struct NodeComponents: Decodable {
        let id: HUDSourceID
        let components: [HUDSourceWatchComponent]
    }
    fileprivate struct SceneGraphPayload: Decodable {
        private struct NodePayload: Decodable {
            let node: HUDSourceNode
            let components: [HUDSourceWatchComponent]
            private enum CodingKeys: String, CodingKey { case components }
            init(from decoder: Decoder) throws {
                node = try HUDSourceNode(from: decoder)
                components = try decoder.container(keyedBy: CodingKeys.self)
                    .decode([HUDSourceWatchComponent].self, forKey: .components)
            }
        }
        let scene: HUDSourceScene
        let nodes: [NodeComponents]
        private enum CodingKeys: String, CodingKey { case rootID = "root_node_id", nodes }
        init(from decoder: Decoder) throws {
            let container = try decoder.container(keyedBy: CodingKeys.self)
            let decoded = try container.decode([NodePayload].self, forKey: .nodes)
            // Keep the common graph initializer's identity, edge and cycle
            // validation; only the second parse of the same JSON is removed.
            scene = try HUDSourceScene(rootID: container.decode(HUDSourceID.self, forKey: .rootID), nodes: decoded.map(\.node))
            nodes = decoded.map { NodeComponents(id: $0.node.id, components: $0.components) }
        }
    }
    private struct ScenePayload: Decodable {
        let scene: HUDSourceScene
        let nodes: [NodeComponents]
        let buttons: [HUDSourceWatchButton]
        private enum CodingKeys: String, CodingKey { case buttons = "main_buttons" }
        init(from decoder: Decoder) throws {
            let graph = try SceneGraphPayload(from: decoder)
            scene = graph.scene; nodes = graph.nodes
            buttons = try decoder.container(keyedBy: CodingKeys.self).decode([HUDSourceWatchButton].self, forKey: .buttons)
        }
    }
    struct Animator: Decodable {
        struct State: Decodable {
            let name: String
            let clipID: HUDSourceID
            enum CodingKeys: String, CodingKey { case name, clipID = "bound_clip_id" }
        }
        let rootID: HUDSourceID
        let controllerName: String
        let states: [State]
        enum CodingKeys: String, CodingKey { case rootID = "root_node_id", controllerName = "controller_name", states }
    }
    private struct AnimationPayload: Decodable {
        let library: HUDSourceAnimationLibrary
        let animators: [Animator]
        private enum CodingKeys: String, CodingKey { case animators = "controller_instances" }
        init(from decoder: Decoder) throws {
            // Reuse the strict clip/curve decoders and the same decoded tree.
            library = try HUDSourceAnimationLibrary(from: decoder)
            animators = try decoder.container(keyedBy: CodingKeys.self).decode([Animator].self, forKey: .animators)
        }
    }
    let root: URL
    let scene: HUDSourceScene
    let library: HUDSourceAnimationLibrary
    let animation: HUDSourceWatchAnimation
    let blurAnimation: HUDSourceWatchBlurAnimation
    let runtimeRoot: HUDSourceJSONValue
    let controllerTransitions: HUDSourceJSONValue
    let components: [HUDSourceID: [HUDSourceWatchComponent]]
    let buttons: [HUDSourceWatchButton]
    let animators: [Animator]
    let sprites: HUDSourceJSONValue
    let fonts: HUDSourceJSONValue
    let labels: HUDSourceJSONValue
    let materials: HUDSourceJSONValue
    let spriteByComponent: [HUDSourceID: HUDSourceJSONValue]
    let widgets: HUDSourceWatchWidgets?
    let desktopProfileCard: HUDSourceDesktopProfileCard?
    private let desktopButtonIDs: [HUDSourceID]
    private let desktopHiddenDecorationIDs: [HUDSourceID]
    private let renderMetadataLock = NSLock()
    private var retainedRenderMetadata: RenderMetadata?

    /// Parsed source values are shared with each renderer of this immutable
    /// document. The renderer still validates its own uploaded textures.
    final class RenderMetadata {
        let materials: [HUDSourceID: HUDSourceJSONValue]
        let textures: [(id: String, file: String)]
        let textureSizes: [String: SIMD2<Float>]
        let sourceSprites: [String: HUDSourceImageGeometry.Sprite]
        let sprites: [HUDSourceID: HUDSourceImageGeometry.Sprite]
        init(materials: [HUDSourceID: HUDSourceJSONValue], textures: [(id: String, file: String)],
             textureSizes: [String: SIMD2<Float>], sourceSprites: [String: HUDSourceImageGeometry.Sprite],
             sprites: [HUDSourceID: HUDSourceImageGeometry.Sprite]) {
            self.materials = materials; self.textures = textures; self.textureSizes = textureSizes
            self.sourceSprites = sourceSprites; self.sprites = sprites
        }
    }

    func renderMetadata() throws -> RenderMetadata {
        renderMetadataLock.lock(); defer { renderMetadataLock.unlock() }
        if let retainedRenderMetadata { return retainedRenderMetadata }
        let materials = Dictionary(uniqueKeysWithValues: self.materials["materials"].array.compactMap { record in
            record["id"].string.map { (HUDSourceID(rawValue: $0), record) }
        })
        var textures: [String: HUDSourceJSONValue] = [:]
        var requiredTextures: [(id: String, file: String)] = []
        var sizes: [String: SIMD2<Float>] = ["__white": SIMD2(1, 1)]
        for texture in self.sprites["source_textures"].array {
            guard let id = texture["id"].string, let file = texture["png"]["file"].string else {
                throw HUDSourceError.invalid("Source Sprite texture metadata missing")
            }
            textures[id] = texture
            guard let space = texture["color_space"].number else {
                throw HUDSourceError.invalid("Unverified source Sprite texture color space: \(id)")
            }
            guard space == 0 || space == 1 else {
                throw HUDSourceError.invalid("Original Sprite mip chain missing from renderer: \(id), \(file)")
            }
            requiredTextures.append((id, file))
            sizes[id] = SIMD2(Float(texture["width"].float()), Float(texture["height"].float()))
        }
        var sourceSprites: [String: HUDSourceImageGeometry.Sprite] = [:]
        for sprite in self.sprites["sprites"].array {
            guard let id = sprite["id"].string, let textureID = sprite["texture"]["id"].string,
                  let texture = textures[textureID] else {
                throw HUDSourceError.invalid("Unresolved original named Sprite texture")
            }
            sourceSprites[id] = try HUDSourceImageGeometry.Sprite(source: sprite, texture: texture)
        }
        var sprites: [HUDSourceID: HUDSourceImageGeometry.Sprite] = [:]
        for (component, sprite) in spriteByComponent {
            guard let id = sprite["texture"]["id"].string, let texture = textures[id] else {
                throw HUDSourceError.invalid("Unresolved original Sprite texture: \(component)")
            }
            sprites[component] = try HUDSourceImageGeometry.Sprite(source: sprite, texture: texture)
        }
        let result = RenderMetadata(materials: materials, textures: requiredTextures, textureSizes: sizes,
                                    sourceSprites: sourceSprites, sprites: sprites)
        retainedRenderMetadata = result
        return result
    }

    init(resourceRoot: URL? = nil, includeWidgets: Bool = true, includeSourceText: Bool = true, includeDesktopProfile: Bool = false) throws {
        guard let root = resourceRoot ?? HUDResources.url(for: "WatchSource/Scene") else {
            throw HUDSourceError.invalid("Watch source scene resources are missing")
        }
        self.root = root
        let decoder = HUDSourceJSON.decoder()
        func data(_ name: String) throws -> Data { try HUDSourceResourceData.read(root.appendingPathComponent(name + ".json")) }
        let sceneData = try data("scene"), clipData = try data("clips")
        let details = try decoder.decode(ScenePayload.self, from: sceneData)
        runtimeRoot = try decoder.decode(HUDSourceJSONValue.self, from: data("runtime-root-camera"))
        controllerTransitions = try decoder.decode(HUDSourceJSONValue.self, from: data("controller-transitions"))
        let originalScene = details.scene
        let widgetURL = root.appendingPathComponent("Widgets/widget.json")
        let widgets: HUDSourceWatchWidgets?
        if includeWidgets && FileManager.default.fileExists(atPath: widgetURL.path) {
            widgets = try HUDSourceWatchWidgets(data: HUDSourceResourceData.read(widgetURL), originalSceneData: sceneData,
                bannerData: HUDSourceResourceData.read(root.appendingPathComponent("Widgets/banner-runtime.json")))
        } else { widgets = nil }
        self.widgets = widgets
        guard !includeDesktopProfile || widgets == nil else { throw HUDSourceError.invalid("Conflicting profile card runtimes") }
        let profileCard = includeDesktopProfile ? try HUDSourceDesktopProfileCard(data: data("desktop-profile-card")) : nil
        desktopProfileCard = profileCard
        scene = try profileCard?.mounted(in: originalScene) ?? widgets?.mounted(in: originalScene) ?? originalScene
        components = Dictionary(uniqueKeysWithValues: details.nodes.map { ($0.id, $0.components) })
            .merging(widgets?.components ?? profileCard?.components ?? [:]) { original, _ in original }
        buttons = details.buttons
        desktopButtonIDs = details.buttons.map(\.nodeID)
        let mainButtons = Set(desktopButtonIDs)
        let navigationScene = scene
        desktopHiddenDecorationIDs = navigationScene.nodes.compactMap { node in
            let name = node.name.trimmingCharacters(in: .whitespacesAndNewlines)
            if name.lowercased().hasSuffix("reddot") { return node.id }
            guard ["LockIcon", "SafeZoneIcon"].contains(name) else { return nil }
            var ancestor = node.parentID
            while let id = ancestor, !mainButtons.contains(id) { ancestor = navigationScene.node(id)?.parentID }
            return ancestor == nil ? nil : node.id
        }
        let clips = try decoder.decode(AnimationPayload.self, from: clipData)
        library = clips.library
        animation = try HUDSourceWatchAnimation(scene: scene, library: library)
        blurAnimation = try HUDSourceWatchBlurAnimation(data: data("watch-blur"))
        animators = clips.animators
        let originalSprites = try decoder.decode(HUDSourceJSONValue.self, from: data("sprites"))
        let spriteAdditions = widgets?.sprites ?? profileCard?.sprites
        sprites = spriteAdditions.map { HUDSourceWatchWidgets.merging(originalSprites, additions: $0,
            arrays: ["sprites", "source_textures"]) } ?? originalSprites
        // Desktop captions use native text on authored source planes. Keep
        // the complete TMP payload only for the original reference renderer.
        fonts = includeSourceText ? try decoder.decode(HUDSourceJSONValue.self, from: data("fonts")) : .null
        let originalLabels: HUDSourceJSONValue = includeSourceText
            ? try decoder.decode(HUDSourceJSONValue.self, from: data("labels")) : .null
        labels = widgets.map { HUDSourceWatchWidgets.merging(originalLabels, additions: $0.labels, arrays: ["nodes"]) } ?? originalLabels
        let originalMaterials = try decoder.decode(HUDSourceJSONValue.self, from: data("materials"))
        let materialAdditions = widgets?.materials ?? profileCard?.materials
        materials = materialAdditions.map { HUDSourceWatchWidgets.merging(originalMaterials, additions: $0,
            arrays: ["materials"]) } ?? originalMaterials
        var joined: [HUDSourceID: HUDSourceJSONValue] = [:]
        for sprite in sprites["sprites"].array {
            for binding in sprite["bindings"].array {
                if let id = binding["component_id"].string { joined[HUDSourceID(rawValue: id)] = sprite }
            }
        }
        for (copy, source) in widgets?.runtimeComponentAliases ?? [:] {
            if let sprite = joined[source] { joined[copy] = sprite }
        }
        spriteByComponent = joined
    }

    /// Game-only sections and the side-button glow suppressed by the desktop adapter.
    /// Layout verification shares this closure with the live view.
    var desktopHiddenNodeIDs: Set<HUDSourceID> {
        let hiddenNames: Set<String> = ["Map", "MoneyCellRoot", "ExploreRoot", "EndfieldLogo",
            "GlowLeftBtn", "GlowRightBtn",
            "Top_RightNode", "TopLeftBtnNode", "HomePageBtn", "CloseButtonNode", "FullScreenCloseBtn", "ControllerHintPlaceholder", "BannerNode"]
        var result = Set(scene.nodes.filter { hiddenNames.contains($0.name) }.map(\.id))
        if desktopProfileCard == nil, let parent = scene.nodes.first(where: { $0.name == "PlayInfoPosNode" }) { result.insert(parent.id) }
        if let bottom = scene.nodes.first(where: { $0.name == "HudBgShdow" }) {
            for child in scene.nodes where child.parentID == bottom.id && !["TechtreeNode", "ReportNode"].contains(child.name) {
                result.insert(child.id)
            }
        }
        return result
    }

    func component(_ kind: String, on id: HUDSourceID) -> HUDSourceWatchComponent? {
        components[id]?.first { $0.kind == kind && $0.enabled }
    }

    /// WatchCtrl._RelayoutRightList activates every non-hidden main button.
    /// The macOS adapter exposes all 22 mapped functions, with no game-account
    /// lock, safe-zone restriction or unread-notification state. This is an
    /// explicit desktop availability policy, not an inferred game save.
    func applyMacButtonAvailability(to pose: inout HUDSourceWatchPose) {
        for id in desktopButtonIDs {
            var value = pose.transforms[id] ?? HUDSourceTransformOverride()
            value.active = true; pose.transforms[id] = value
        }
        for id in desktopHiddenDecorationIDs {
            var value = pose.transforms[id] ?? HUDSourceTransformOverride()
            value.active = false; pose.transforms[id] = value
        }
    }

    func inheritedAlpha(pose: HUDSourceWatchPose) -> [HUDSourceID: Double] {
        var result: [HUDSourceID: Double] = [:]
        for id in scene.traversalIDs {
            guard let node = scene.node(id) else { continue }
            var alpha = node.parentID.flatMap { result[$0] } ?? 1
            for group in components[id] ?? [] where group.kind == "CanvasGroup" && group.enabled {
                alpha *= pose.value("m_Alpha", on: id, fallback: group["m_Alpha"].float(1))
            }
            result[id] = alpha
        }
        return result
    }
}

/// The authored BP13 identity card without the unrelated banner/account runtime.
/// Only immutable source records are shared by the desktop document cache.
struct HUDSourceDesktopProfileCard: Decodable {
    let parentID: HUDSourceID
    let scene: HUDSourceScene
    let components: [HUDSourceID: [HUDSourceWatchComponent]]
    let bindings: HUDSourceJSONValue
    let sprites: HUDSourceJSONValue
    let materials: HUDSourceJSONValue
    private enum CodingKeys: String, CodingKey { case parentID = "parent_id", scene, bindings, sprites, materials }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        parentID = try c.decode(HUDSourceID.self, forKey: .parentID)
        let records = try c.decode(HUDSourceWatchDocument.SceneGraphPayload.self, forKey: .scene)
        scene = records.scene
        components = Dictionary(uniqueKeysWithValues: records.nodes.map { ($0.id, $0.components) })
        bindings = try c.decode(HUDSourceJSONValue.self, forKey: .bindings)
        sprites = try c.decode(HUDSourceJSONValue.self, forKey: .sprites)
        materials = try c.decode(HUDSourceJSONValue.self, forKey: .materials)
        guard scene.node(scene.rootID)?.transform.rect?.sizeDelta == HUDSourceVector2(364, 128),
              ["button", "playerHead", "managerName", "managerNumber", "managerLevel", "levelSlider"].allSatisfy({
                  bindings[$0]["target_node_id"].string.flatMap { scene.node(HUDSourceID(rawValue: $0)) } != nil
              }) else { throw HUDSourceError.invalid("Invalid selected source profile card") }
    }
    init(data: Data) throws { self = try HUDSourceJSON.decoder().decode(Self.self, from: data) }
    func node(_ binding: String) -> HUDSourceID? { bindings[binding]["target_node_id"].string.map(HUDSourceID.init(rawValue:)) }
    var buttonIDs: Set<HUDSourceID> { Set(["button", "playerInfoBtn", "playerHeadBtn", "rightBtn"].compactMap(node)) }
    var backgroundNodeID: HUDSourceID? { scene.nodes.first { $0.name == "BgImage" }?.id }
    var defaultBackgroundSprite: HUDSourceJSONValue? {
        sprites["sprites"].array.first { $0["name"].string == "business_card_topic_normal_1" }
    }
    // The source Light graphics use additive blending over the entire card and
    // portrait. Desktop photographs remain unchanged when the card is hovered.
    var artworkGlowNodeIDs: Set<HUDSourceID> {
        Set(scene.nodes.filter { $0.name == "Light" && ($0.parentID == scene.rootID
            || $0.path.hasSuffix("/PlayerHeadBtn/Light")) }.map(\.id))
    }
    func mounted(in original: HUDSourceScene) throws -> HUDSourceScene {
        guard let parent = original.node(parentID), parent.childIDs.isEmpty,
              Set(original.nodes.map(\.id)).isDisjoint(with: scene.nodes.map(\.id)) else {
            throw HUDSourceError.invalid("Source profile parent or IDs conflict")
        }
        var nodes = original.nodes.map { node in
            node.id == parentID ? HUDSourceNode(id: node.id, path: node.path, name: node.name,
                parentID: node.parentID, childIDs: [scene.rootID], active: node.active, transform: node.transform) : node
        }
        nodes += scene.nodes.map { node in
            HUDSourceNode(id: node.id, path: parent.path + "/" + node.path, name: node.name,
                parentID: node.id == scene.rootID ? parentID : node.parentID,
                childIDs: node.childIDs, active: node.active, transform: node.transform)
        }
        return try HUDSourceScene(rootID: original.rootID, nodes: nodes)
    }
}

/// One validated desktop document and at most one decode in flight. Reference
/// fixtures continue to use the uncached initializer with their original data.
final class HUDSourceDesktopDocumentCache {
    private struct Identity: Equatable {
        struct File: Equatable {
            let path: String
            let size: UInt64
            let modified: Date
            let inode: UInt64
            let device: UInt64
        }
        let root: URL
        let files: [File]

        init(root: URL) throws {
            let canonicalRoot = root.standardizedFileURL.resolvingSymlinksInPath()
            self.root = canonicalRoot
            // Every immutable desktop input, including the selected profile prefab.
            files = try ["scene", "clips", "watch-blur", "sprites", "materials", "desktop-profile-card", "runtime-root-camera", "controller-transitions"].map { name in
                let file = canonicalRoot.appendingPathComponent(name + ".json").resolvingSymlinksInPath()
                let attributes = try FileManager.default.attributesOfItem(atPath: file.path)
                guard let size = attributes[.size] as? NSNumber,
                      let modified = attributes[.modificationDate] as? Date,
                      let inode = attributes[.systemFileNumber] as? NSNumber,
                      let device = attributes[.systemNumber] as? NSNumber else {
                    throw HUDSourceError.invalid("Cannot identify Watch source resource \(name)")
                }
                return File(path: file.path, size: size.uint64Value, modified: modified,
                    inode: inode.uint64Value, device: device.uint64Value)
            }
        }
    }
    private final class Flight {
        let identity: Identity
        let generation: UInt64
        var result: Result<HUDSourceWatchDocument, Error>?
        init(identity: Identity, generation: UInt64) {
            self.identity = identity; self.generation = generation
        }
    }
    private let condition = NSCondition()
    private let loader: (URL) throws -> HUDSourceWatchDocument
    private var cached: (identity: Identity, document: HUDSourceWatchDocument)?
    private var flight: Flight?
    private var generation: UInt64 = 0
    private var prewarmScheduled = false

    init(loader: @escaping (URL) throws -> HUDSourceWatchDocument = {
        try HUDSourceWatchDocument(resourceRoot: $0, includeWidgets: false, includeSourceText: false, includeDesktopProfile: true)
    }) { self.loader = loader }

    func document(resourceRoot: URL? = nil, generation expectedGeneration: UInt64? = nil) throws -> HUDSourceWatchDocument {
        guard let root = resourceRoot ?? HUDResources.url(for: "WatchSource/Scene") else {
            throw HUDSourceError.invalid("Watch source scene resources are missing")
        }
        while true {
            // File-system access and decoding never hold the metadata lock.
            let identity = try Identity(root: root)
            condition.lock()
            if let expectedGeneration, expectedGeneration != generation {
                condition.unlock()
                throw HUDSourceError.invalid("Watch source prewarm was cleared")
            }
            if let cached, cached.identity == identity {
                condition.unlock(); return cached.document
            }
            if let pending = flight {
                while pending.result == nil { condition.wait() }
                let result = pending.result!
                let matches = pending.identity == identity && pending.generation == generation
                condition.unlock()
                if matches { return try result.get() }
                continue
            }
            let pending = Flight(identity: identity, generation: generation)
            flight = pending
            // Do not retain a stale document while a replacement is decoded.
            cached = nil
            condition.unlock()
            let result = Result<HUDSourceWatchDocument, Error> {
                let document = try loader(identity.root)
                guard document.widgets == nil, case .null = document.fonts, case .null = document.labels else {
                    throw HUDSourceError.invalid("Only the immutable desktop Watch profile can be cached")
                }
                _ = try document.renderMetadata()
                guard try Identity(root: root) == identity else {
                    throw HUDSourceError.invalid("Watch source resources changed during loading")
                }
                return document
            }
            condition.lock()
            pending.result = result
            if generation == pending.generation, case .success(let document) = result {
                cached = (identity, document)
            }
            flight = nil
            condition.broadcast()
            condition.unlock()
            return try result.get()
        }
    }

    func prewarm() {
        condition.lock()
        guard !prewarmScheduled else { condition.unlock(); return }
        prewarmScheduled = true
        let scheduledGeneration = generation
        condition.unlock()
        DispatchQueue.global(qos: .utility).async { [self] in
            condition.lock()
            let current = scheduledGeneration == generation
            condition.unlock()
            if current { _ = try? document(generation: scheduledGeneration) }
            condition.lock(); prewarmScheduled = false; condition.unlock()
        }
    }

    func clear() {
        condition.lock()
        generation &+= 1
        cached = nil
        condition.unlock()
    }
}
