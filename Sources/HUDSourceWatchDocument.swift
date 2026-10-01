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
        else if let v = try? c.decode(Bool.self) { self = .bool(v) }
        else if let v = try? c.decode(String.self) { self = .string(v) }
        else if let v = try? c.decode(Double.self) { self = .number(v) }
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
    struct NodeComponents: Decodable {
        let id: HUDSourceID
        let components: [HUDSourceWatchComponent]
    }
    private struct Details: Decodable {
        let nodes: [NodeComponents]
        let buttons: [HUDSourceWatchButton]
        enum CodingKeys: String, CodingKey { case nodes, buttons = "main_buttons" }
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
    private struct ExtraAnimations: Decodable {
        let animators: [Animator]
        enum CodingKeys: String, CodingKey { case animators = "controller_instances" }
    }
    let root: URL
    let scene: HUDSourceScene
    let library: HUDSourceAnimationLibrary
    let animation: HUDSourceWatchAnimation
    let blurAnimation: HUDSourceWatchBlurAnimation
    let components: [HUDSourceID: [HUDSourceWatchComponent]]
    let buttons: [HUDSourceWatchButton]
    let animators: [Animator]
    let sprites: HUDSourceJSONValue
    let fonts: HUDSourceJSONValue
    let labels: HUDSourceJSONValue
    let materials: HUDSourceJSONValue
    let spriteByComponent: [HUDSourceID: HUDSourceJSONValue]
    let widgets: HUDSourceWatchWidgets?

    init(resourceRoot: URL? = nil, includeWidgets: Bool = true) throws {
        guard let root = resourceRoot ?? HUDResources.url(for: "WatchSource/Scene") else {
            throw HUDSourceError.invalid("Watch source scene resources are missing")
        }
        self.root = root
        let decoder = HUDSourceJSON.decoder()
        func data(_ name: String) throws -> Data { try Data(contentsOf: root.appendingPathComponent(name + ".json")) }
        let sceneData = try data("scene"), clipData = try data("clips")
        let originalScene = try decoder.decode(HUDSourceScene.self, from: sceneData)
        let widgetURL = root.appendingPathComponent("Widgets/widget.json")
        let widgets: HUDSourceWatchWidgets?
        if includeWidgets && FileManager.default.fileExists(atPath: widgetURL.path) {
            widgets = try HUDSourceWatchWidgets(data: Data(contentsOf: widgetURL), originalSceneData: sceneData,
                bannerData: Data(contentsOf: root.appendingPathComponent("Widgets/banner-runtime.json")))
        } else { widgets = nil }
        self.widgets = widgets
        scene = try widgets?.mounted(in: originalScene) ?? originalScene
        let details = try decoder.decode(Details.self, from: sceneData)
        components = Dictionary(uniqueKeysWithValues: details.nodes.map { ($0.id, $0.components) })
            .merging(widgets?.components ?? [:]) { original, _ in original }
        buttons = details.buttons
        library = try decoder.decode(HUDSourceAnimationLibrary.self, from: clipData)
        animation = try HUDSourceWatchAnimation(scene: scene, library: library)
        blurAnimation = try HUDSourceWatchBlurAnimation(data: data("watch-blur"))
        animators = try decoder.decode(ExtraAnimations.self, from: clipData).animators
        let originalSprites = try decoder.decode(HUDSourceJSONValue.self, from: data("sprites"))
        sprites = widgets.map { HUDSourceWatchWidgets.merging(originalSprites, additions: $0.sprites,
            arrays: ["sprites", "source_textures"]) } ?? originalSprites
        fonts = try decoder.decode(HUDSourceJSONValue.self, from: data("fonts"))
        let originalLabels = try decoder.decode(HUDSourceJSONValue.self, from: data("labels"))
        labels = widgets.map { HUDSourceWatchWidgets.merging(originalLabels, additions: $0.labels, arrays: ["nodes"]) } ?? originalLabels
        let originalMaterials = try decoder.decode(HUDSourceJSONValue.self, from: data("materials"))
        materials = widgets.map { HUDSourceWatchWidgets.merging(originalMaterials, additions: $0.materials,
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

    func component(_ kind: String, on id: HUDSourceID) -> HUDSourceWatchComponent? {
        components[id]?.first { $0.kind == kind && $0.enabled }
    }

    /// WatchCtrl._RelayoutRightList activates every non-hidden main button.
    /// The macOS adapter exposes all 22 mapped functions, with no game-account
    /// lock, safe-zone restriction or unread-notification state. This is an
    /// explicit desktop availability policy, not an inferred game save.
    func applyMacButtonAvailability(to pose: inout HUDSourceWatchPose) {
        let main = Set(buttons.map(\.nodeID))
        for id in main {
            var value = pose.transforms[id] ?? HUDSourceTransformOverride()
            value.active = true; pose.transforms[id] = value
        }
        for node in scene.nodes {
            let name = node.name.trimmingCharacters(in: .whitespacesAndNewlines)
            let notification = name.lowercased().hasSuffix("reddot")
            guard notification || ["LockIcon", "SafeZoneIcon"].contains(name) else { continue }
            var ancestor = node.parentID
            while let id = ancestor, !main.contains(id) { ancestor = scene.node(id)?.parentID }
            guard notification || ancestor != nil else { continue }
            var value = pose.transforms[node.id] ?? HUDSourceTransformOverride()
            value.active = false; pose.transforms[node.id] = value
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
