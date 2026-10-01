import Foundation

/// The original BP13 prefab is loaded by WatchCtrl, rather than serialized in
/// WatchPanel_PC. This sidecar retains its source IDs/TRS and joins that subtree
/// under the original PlayInfoPosNode. It has no renderer or account dependency.
final class HUDSourceWatchWidgets {
    struct Profile {
        var displayName: String? = nil
        var identifier: String? = nil
        var level: Int? = nil
        var relativeExperience: Int? = nil
        var nextLevelExperience: Int? = nil
        var isMaximumLevel = false
        var avatarArtwork: String? = nil
        var frameArtwork: String? = nil

        /// An explicit artwork fixture, never a recovered/live player account.
        /// The recording's personal name and UID are intentionally omitted.
        static var recordReference: Profile {
            Profile(displayName: "管理员", level: 60, isMaximumLevel: true,
                avatarArtwork: "icon_chr_0004_pelica", frameArtwork: "icon_user_avatar_frame_bp_1")
        }
    }
    struct State {
        var profile = Profile()
        /// Static reference artwork does not claim account eligibility. The
        /// original 4-second/.2-second/ease3 contract remains in widget.json;
        /// a multi-cell eligible-banner controller is not inferred from it.
        var bannerArtwork: String? = "yvonne_banner"
        static var desktopReference: State { State() }
        static var recordReference: State { State(profile: .recordReference) }
    }
    struct Overrides {
        var text: [HUDSourceID: String] = [:]
        var sprites: [HUDSourceID: String] = [:]
    }
    private struct Payload: Decodable {
        let parentID: HUDSourceID
        let scene: HUDSourceScene
        enum CodingKeys: String, CodingKey { case parentID = "parent_id", scene }
    }
    private struct Components: Decodable {
        let nodes: [HUDSourceWatchDocument.NodeComponents]
    }
    let parentID: HUDSourceID
    let sourceScene: HUDSourceScene
    let components: [HUDSourceID: [HUDSourceWatchComponent]]
    let sprites: HUDSourceJSONValue
    let materials: HUDSourceJSONValue
    let labels: HUDSourceJSONValue
    let bannerImageComponentID: HUDSourceID
    let bannerImageNodeID: HUDSourceID
    let profileButtonIDs: Set<HUDSourceID>
    private let bindings: HUDSourceJSONValue
    private let artwork: [String: String]
    private let maxLevelLiteral: String

    init(data: Data) throws {
        let decoder = HUDSourceJSON.decoder()
        let payload = try decoder.decode(Payload.self, from: data)
        let raw = try decoder.decode(HUDSourceJSONValue.self, from: data)
        // The scene's nodes contain the exact component records as well as
        // the generic hierarchy fields; decode them without lossy ID numbers.
        struct NodePayload: Decodable { let scene: Components }
        let details = try decoder.decode(NodePayload.self, from: data)
        parentID = payload.parentID; sourceScene = payload.scene
        components = Dictionary(uniqueKeysWithValues: details.scene.nodes.map { ($0.id, $0.components) })
        sprites = raw["sprites"]; materials = raw["materials"]; labels = raw["labels"]
        bindings = raw["bindings"]
        artwork = raw["artwork_by_name"].object.compactMapValues(\.string)
        guard let banner = raw["banner_image_component_id"].string,
              let bannerNode = raw["banner_image_node_id"].string,
              let maxLiteral = raw["max_level_literal"].string,
              sourceScene.node(sourceScene.rootID)?.transform.rect?.sizeDelta == HUDSourceVector2(364, 128),
              sourceScene.node(sourceScene.rootID)?.transform.localScale == HUDSourceVector3(1, 1, 1) else {
            throw HUDSourceError.invalid("Invalid original Watch widget contract")
        }
        bannerImageComponentID = HUDSourceID(rawValue: banner); maxLevelLiteral = maxLiteral
        bannerImageNodeID = HUDSourceID(rawValue: bannerNode)
        profileButtonIDs = Set(["button", "playerInfoBtn", "playerHeadBtn", "rightBtn",
            "adventureRewardEntry", "adventureRewardEntryBtn"].compactMap { key in
                raw["bindings"][key]["target_node_id"].string.map { HUDSourceID(rawValue: $0) }
            })
    }

    func mounted(in scene: HUDSourceScene) throws -> HUDSourceScene {
        guard let parent = scene.node(parentID), parent.childIDs.isEmpty,
              Set(scene.nodes.map(\.id)).isDisjoint(with: sourceScene.nodes.map(\.id)) else {
            throw HUDSourceError.invalid("Original Watch widget parent or source IDs conflict")
        }
        var nodes = scene.nodes.map { node in
            node.id == parentID ? HUDSourceNode(id: node.id, path: node.path, name: node.name,
                parentID: node.parentID, childIDs: [sourceScene.rootID], active: node.active, transform: node.transform) : node
        }
        for node in sourceScene.nodes {
            nodes.append(HUDSourceNode(id: node.id, path: parent.path + "/" + node.path, name: node.name,
                parentID: node.id == sourceScene.rootID ? parentID : node.parentID,
                childIDs: node.childIDs, active: node.active, transform: node.transform))
        }
        return try HUDSourceScene(rootID: scene.rootID, nodes: nodes)
    }

    static func merging(_ base: HUDSourceJSONValue, additions: HUDSourceJSONValue, arrays: [String]) -> HUDSourceJSONValue {
        var object = base.object
        for key in arrays {
            var original = base[key].array
            if key == "sprites" {
                for (index, row) in original.enumerated() {
                    guard let id = row["id"].string,
                          let extra = additions[key].array.first(where: { $0["id"].string == id }) else { continue }
                    var joined = row.object
                    let bound = Set(row["bindings"].array.compactMap { $0["component_id"].string })
                    joined["bindings"] = .array(row["bindings"].array + extra["bindings"].array.filter {
                        !bound.contains($0["component_id"].string ?? "")
                    })
                    original[index] = .object(joined)
                }
            }
            let identities = Set(original.compactMap { $0["id"].string ?? $0["node_id"].string })
            let extra = additions[key].array.filter { row in
                guard let id = row["id"].string ?? row["node_id"].string else { return true }
                return !identities.contains(id)
            }
            object[key] = .array(original + extra)
        }
        return .object(object)
    }

    /// Source Lua initialization has explicit inactivity gates. Desktop state
    /// supplies text/artwork only; it does not unlock seasons, rewards, PSN,
    /// unread notifications or birthdays without their game-account contracts.
    func apply(to pose: inout HUDSourceWatchPose, state: State, at time: Double) throws -> Overrides {
        guard time.isFinite else { throw HUDSourceError.invalid("Nonfinite widget time") }
        var result = Overrides()
        func node(_ key: String) -> HUDSourceID? {
            bindings[key]["target_node_id"].string.map { HUDSourceID(rawValue: $0) }
        }
        func active(_ key: String, _ enabled: Bool) {
            guard let id = node(key) else { return }
            var transform = pose.transforms[id] ?? HUDSourceTransformOverride()
            transform.active = enabled; pose.transforms[id] = transform
        }
        func literal(_ key: String, _ value: String?) {
            guard let id = node(key) else { return }
            result.text[id] = value ?? ""; active(key, !(value ?? "").isEmpty)
        }
        for key in ["levelInfo", "levelTxt", "descText", "signatureTxt", "nameText", "groupName", "notesName",
            "psRoot", "pcNode", "levelTag", "seasonTag", "birthdayTipsNode", "redDot", "adventureRewardEntry"] {
            active(key, false)
        }
        let profile = state.profile
        literal("managerName", profile.displayName)
        literal("managerNumber", profile.identifier.map { "UID:" + $0 })
        literal("managerLevel", profile.level.map { String(format: "%02d", max(0, $0)) })
        active("managerLevelLabel", profile.level != nil)
        if let slider = node("levelSlider") {
            let fraction: Double
            if profile.isMaximumLevel { fraction = 1 }
            else if let experience = profile.relativeExperience, let next = profile.nextLevelExperience, next > 0 {
                fraction = min(1, max(0, Double(experience) / Double(next)))
            } else { fraction = 0 }
            pose.properties[slider, default: [:]]["m_FillAmount"] = fraction
            active("levelSlider", profile.level != nil)
        }
        if profile.isMaximumLevel { literal("progressTxt", maxLevelLiteral) }
        else if let experience = profile.relativeExperience, let next = profile.nextLevelExperience, next > 0 {
            literal("progressTxt", "\(experience)/\(next)")
        } else { literal("progressTxt", nil) }
        func sprite(_ key: String, _ name: String?) throws {
            active(key, name != nil)
            guard let name else { return }
            guard let id = artwork[name], let component = bindings[key]["target_component_id"].string else {
                throw HUDSourceError.invalid("Unsupported explicit widget artwork: " + name)
            }
            result.sprites[HUDSourceID(rawValue: component)] = id
        }
        try sprite("playerHead", profile.avatarArtwork)
        try sprite("headFrameImg", profile.frameArtwork)
        if let name = state.bannerArtwork {
            guard let id = artwork[name] else { throw HUDSourceError.invalid("Unsupported explicit banner artwork: " + name) }
            result.sprites[bannerImageComponentID] = id
        }
        var bannerTransform = pose.transforms[bannerImageNodeID] ?? HUDSourceTransformOverride()
        bannerTransform.active = state.bannerArtwork != nil
        pose.transforms[bannerImageNodeID] = bannerTransform
        return result
    }
}
