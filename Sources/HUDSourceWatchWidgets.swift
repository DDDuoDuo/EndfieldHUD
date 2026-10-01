import Foundation

/// The loaded BP13 card retains source IDs/TRS under PlayInfoPosNode. Banner
/// cells/page indicators are separately copied from original hidden templates
/// with derived runtime IDs and explicit source aliases. No account is inferred.
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
        /// A single reference is the deterministic default. A caller may
        /// supply an explicit desktop sequence; this is never table eligibility.
        var bannerArtwork: String? = "yvonne_banner"
        var bannerArtworks: [String]? = nil
        var bannerPaused = false
        static var desktopReference: State { State() }
        static var recordReference: State { State(profile: .recordReference) }
    }
    struct BannerSample {
        let artworks: [String]
        let selectedIndex: Int
        let normalizedPosition: Double
    }
    /// The Lua hold clock discards overshoot at each >=4-second tick. DOTween
    /// runs independently of pause and starts from the current scroll position.
    struct BannerPlayback {
        let artworks: [String]
        let holdDuration: Double
        let tweenDuration: Double
        private(set) var selectedIndex = 0
        private(set) var holdTime = 0.0
        private(set) var normalizedPosition = 0.0
        private var centerIndex = 0
        private var tweenStart = 0.0
        private var tweenTarget = 0.0
        private var tweenElapsed: Double? = nil

        init(artworks: [String], holdDuration: Double = 4,
             tweenDuration: Double = 0.20000000298023224) throws {
            guard artworks.count <= 2, !artworks.contains(where: \.isEmpty),
                  holdDuration.isFinite, holdDuration > 0,
                  tweenDuration.isFinite, tweenDuration > 0 else {
                throw HUDSourceError.invalid("Unsupported explicit source banner sequence")
            }
            self.artworks = artworks; self.holdDuration = holdDuration; self.tweenDuration = tweenDuration
        }
        var sample: BannerSample {
            BannerSample(artworks: artworks, selectedIndex: selectedIndex, normalizedPosition: normalizedPosition)
        }
        var isTweening: Bool { tweenElapsed != nil }
        mutating func select(index: Int) throws {
            guard artworks.indices.contains(index) else { throw HUDSourceError.invalid("Invalid source banner page") }
            holdTime = 0; selectedIndex = index
            tweenStart = normalizedPosition
            tweenTarget = artworks.count > 1 ? Double(index) / Double(artworks.count - 1) : 0
            tweenElapsed = 0
        }
        mutating func dragged() { holdTime = 0 }
        /// UIStep.OnBeginDrag kills the current DOTween with complete=false.
        mutating func beganDrag() { tweenElapsed = nil }
        mutating func scrolled(to position: Double) throws {
            guard position.isFinite, (0...1).contains(position) else {
                throw HUDSourceError.invalid("Invalid source banner normalized position")
            }
            normalizedPosition = position
            updateCenter()
        }
        private mutating func updateCenter() {
            guard !artworks.isEmpty else { return }
            let center = Float(normalizedPosition) * Float(artworks.count - 1) + 0.5
            let next = min(artworks.count - 1, max(0, Int(center.rounded(.towardZero))))
            if next != centerIndex {
                centerIndex = next; selectedIndex = next; holdTime = 0
            }
        }
        mutating func advance(delta: Double, paused: Bool = false) throws {
            guard delta.isFinite, delta >= 0 else { throw HUDSourceError.invalid("Invalid source banner delta") }
            if let elapsed = tweenElapsed {
                let next = min(Float(tweenDuration), Float(elapsed) + Float(delta))
                // Exact installed Ease.OutSine: Float32 division/multiply,
                // literal 0x3fc90fdb, Double Math.Sin, then Float32 result.
                let phase = next / Float(tweenDuration) * Float(1.5707963705062866)
                let progress = Float(sin(Double(phase)))
                normalizedPosition = Double(Float(tweenStart) + (Float(tweenTarget) - Float(tweenStart)) * progress)
                tweenElapsed = next < Float(tweenDuration) ? Double(next) : nil
                // UIStep.UpdateShowingCells has no manual/automatic distinction;
                // an automatic tween also emits the center-changed Lua callback.
                updateCenter()
            }
            guard !paused, !artworks.isEmpty else { return }
            holdTime += delta
            if holdTime >= holdDuration { try select(index: (selectedIndex + 1) % artworks.count) }
        }
    }
    struct BannerInstance {
        let rootID: HUDSourceID
        let imageNodeID: HUDSourceID
        let imageComponentID: HUDSourceID
        let buttonNodeID: HUDSourceID
        let lightNodeID: HUDSourceID
        let pageRootID: HUDSourceID
        let pageOnID: HUDSourceID
        let pageOffID: HUDSourceID
    }
    private struct BannerContract: Decodable {
        let cellTemplateID: HUDSourceID, containerID: HUDSourceID, listID: HUDSourceID
        let imageNodeID: HUDSourceID, imageComponentID: HUDSourceID, lightNodeID: HUDSourceID
        let pageTemplateID: HUDSourceID, pageContainerID: HUDSourceID, pageOnID: HUDSourceID, pageOffID: HUDSourceID
        let viewSize: HUDSourceVector2, cellSize: HUDSourceVector2
        let spacing: Double, holdDuration: Double, tweenDuration: Double, tweenEase: Int, capacity: Int
        enum CodingKeys: String, CodingKey {
            case cellTemplateID = "cell_template_id", containerID = "cell_container_id"
            case listID = "source_list_id"
            case imageNodeID = "image_node_id", imageComponentID = "image_component_id", lightNodeID = "light_node_id"
            case pageTemplateID = "page_template_id", pageContainerID = "page_container_id"
            case pageOnID = "page_on_id", pageOffID = "page_off_id", viewSize = "view_size", cellSize = "cell_size"
            case spacing = "cell_spacing_x", holdDuration = "auto_scroll_time", tweenDuration = "tween_duration"
            case tweenEase = "tween_ease", capacity = "runtime_capacity"
        }
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
    let bannerInstances: [BannerInstance]
    let bannerListNodeID: HUDSourceID
    var bannerButtonIDs: Set<HUDSourceID> { Set(bannerInstances.map(\.buttonNodeID)).union([bannerListNodeID]) }
    /// All copied component references resolve through these source aliases.
    let runtimeComponentAliases: [HUDSourceID: HUDSourceID]
    let runtimeNodeAliases: [HUDSourceID: HUDSourceID]
    private let runtimeNodes: [HUDSourceNode]
    private let bannerContract: BannerContract
    private let bindings: HUDSourceJSONValue
    private let artwork: [String: String]
    private let maxLevelLiteral: String

    init(data: Data, originalSceneData: Data, bannerData: Data) throws {
        let decoder = HUDSourceJSON.decoder()
        let payload = try decoder.decode(Payload.self, from: data)
        let raw = try decoder.decode(HUDSourceJSONValue.self, from: data)
        // The scene's nodes contain the exact component records as well as
        // the generic hierarchy fields; decode them without lossy ID numbers.
        struct NodePayload: Decodable { let scene: Components }
        let details = try decoder.decode(NodePayload.self, from: data)
        parentID = payload.parentID; sourceScene = payload.scene
        var joinedComponents = Dictionary(uniqueKeysWithValues: details.scene.nodes.map { ($0.id, $0.components) })
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
        let contract = try decoder.decode(BannerContract.self, from: bannerData)
        let original = try decoder.decode(HUDSourceScene.self, from: originalSceneData)
        let originalRaw = try decoder.decode(HUDSourceJSONValue.self, from: originalSceneData)
        let gameObjects: [HUDSourceID: HUDSourceID] = Dictionary(uniqueKeysWithValues: originalRaw["nodes"].array.compactMap {
            guard let node = $0["id"].string, let object = $0["game_object"]["id"].string else { return nil }
            return (HUDSourceID(rawValue: node), HUDSourceID(rawValue: object))
        })
        let originalDetails = try decoder.decode(Components.self, from: originalSceneData)
        let sourceComponents = Dictionary(uniqueKeysWithValues: originalDetails.nodes.map { ($0.id, $0.components) })
        guard contract.capacity == 2, contract.tweenEase == 3, contract.holdDuration == 4,
              contract.viewSize == HUDSourceVector2(365, 128.5), contract.cellSize == HUDSourceVector2(360, 122),
              contract.spacing == 6.5, original.node(contract.cellTemplateID)?.active == false,
              original.node(contract.containerID)?.childIDs.isEmpty == true else {
            throw HUDSourceError.invalid("Original banner clone/scroll contract changed")
        }
        bannerContract = contract; bannerListNodeID = contract.listID
        var clonedNodes: [HUDSourceNode] = [], instances: [BannerInstance] = []
        var componentAliases: [HUDSourceID: HUDSourceID] = [:], nodeAliases: [HUDSourceID: HUDSourceID] = [:]
        func clone(_ template: HUDSourceID, into parent: HUDSourceID, index: Int, kind: String) throws {
            guard let root = original.node(template), let parentNode = original.node(parent) else {
                throw HUDSourceError.invalid("Original banner template parent missing")
            }
            var sources: [HUDSourceNode] = []
            func walk(_ id: HUDSourceID) throws {
                guard let node = original.node(id) else { throw HUDSourceError.invalid("Banner subtree source missing") }
                sources.append(node); for child in node.childIDs { try walk(child) }
            }
            try walk(template)
            var mapping: [HUDSourceID: HUDSourceID] = [:]
            func derived(_ id: HUDSourceID) -> HUDSourceID {
                HUDSourceID(rawValue: "runtime-watch-banner/\(kind)/\(index)/" + id.rawValue)
            }
            for node in sources {
                mapping[node.id] = derived(node.id); nodeAliases[derived(node.id)] = node.id
                if let object = gameObjects[node.id] { mapping[object] = derived(object) }
                for c in sourceComponents[node.id] ?? [] {
                    mapping[c.id] = derived(c.id); componentAliases[derived(c.id)] = c.id
                }
            }
            func remap(_ value: HUDSourceJSONValue) -> HUDSourceJSONValue {
                if case .array(let values) = value { return .array(values.map(remap)) }
                if case .object(let fields) = value {
                    var result = fields.mapValues(remap)
                    if let id = value.targetID, let target = mapping[id] { result["target_id"] = .string(target.rawValue) }
                    return .object(result)
                }
                return value
            }
            for node in sources {
                let id = derived(node.id)
                var transform = node.transform
                if kind == "cell", node.id == template {
                    transform = HUDSourceTransform(kind: .rectTransform, localPosition: node.transform.localPosition,
                        localRotation: node.transform.localRotation, localScale: node.transform.localScale,
                        rect: HUDSourceRectTransform(anchorMin: HUDSourceVector2(0, 1), anchorMax: HUDSourceVector2(0, 1),
                            anchoredPosition: HUDSourceVector2(0, 0), sizeDelta: contract.cellSize, pivot: HUDSourceVector2(0, 1)))
                }
                clonedNodes.append(HUDSourceNode(id: id,
                    path: parentNode.path + "/" + root.name + "[runtime\(index)]" + String(node.path.dropFirst(root.path.count)),
                    name: node.name, parentID: node.id == template ? parent : node.parentID.flatMap { mapping[$0] },
                    childIDs: node.childIDs.compactMap { mapping[$0] }, active: node.id == template ? false : node.active,
                    transform: transform))
                joinedComponents[id] = (sourceComponents[node.id] ?? []).map { c in
                    HUDSourceWatchComponent(id: derived(c.id), type: c.type, script: c.script, data: c.data.mapValues(remap))
                }
            }
        }
        for index in 0..<contract.capacity {
            try clone(contract.cellTemplateID, into: contract.containerID, index: index, kind: "cell")
            try clone(contract.pageTemplateID, into: contract.pageContainerID, index: index, kind: "page")
            func id(_ source: HUDSourceID, _ kind: String) -> HUDSourceID {
                HUDSourceID(rawValue: "runtime-watch-banner/\(kind)/\(index)/" + source.rawValue)
            }
            instances.append(BannerInstance(rootID: id(contract.cellTemplateID, "cell"),
                imageNodeID: id(contract.imageNodeID, "cell"), imageComponentID: id(contract.imageComponentID, "cell"),
                buttonNodeID: id(contract.cellTemplateID, "cell"), lightNodeID: id(contract.lightNodeID, "cell"),
                pageRootID: id(contract.pageTemplateID, "page"), pageOnID: id(contract.pageOnID, "page"),
                pageOffID: id(contract.pageOffID, "page")))
        }
        runtimeNodes = clonedNodes; bannerInstances = instances
        runtimeComponentAliases = componentAliases; runtimeNodeAliases = nodeAliases
        components = joinedComponents
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
        let additions = Dictionary(grouping: runtimeNodes.filter { node in
            node.parentID == bannerContract.containerID || node.parentID == bannerContract.pageContainerID
        }, by: { $0.parentID! })
        nodes = nodes.map { node in
            guard let children = additions[node.id] else { return node }
            return HUDSourceNode(id: node.id, path: node.path, name: node.name, parentID: node.parentID,
                childIDs: node.childIDs + children.map(\.id), active: node.active, transform: node.transform)
        }
        nodes += runtimeNodes
        return try HUDSourceScene(rootID: scene.rootID, nodes: nodes)
    }

    func makeBannerPlayback(artworks: [String]) throws -> BannerPlayback {
        guard artworks.allSatisfy({ artwork[$0] != nil }) else {
            throw HUDSourceError.invalid("Unpublished source banner artwork in explicit sequence")
        }
        return try BannerPlayback(artworks: artworks, holdDuration: bannerContract.holdDuration,
            tweenDuration: bannerContract.tweenDuration)
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
    func apply(to pose: inout HUDSourceWatchPose, state: State, at time: Double,
               banner sample: BannerSample? = nil) throws -> Overrides {
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
        let banner = sample ?? BannerSample(artworks: state.bannerArtworks ?? (state.bannerArtwork.map { [$0] } ?? []), selectedIndex: 0,
            normalizedPosition: 0)
        guard banner.artworks.count <= bannerInstances.count, banner.normalizedPosition.isFinite,
              (0...1).contains(banner.normalizedPosition),
              banner.artworks.isEmpty || banner.artworks.indices.contains(banner.selectedIndex) else {
            throw HUDSourceError.invalid("Invalid explicit source banner sample")
        }
        func setActive(_ id: HUDSourceID, _ enabled: Bool) {
            var value = pose.transforms[id] ?? HUDSourceTransformOverride()
            value.active = enabled; pose.transforms[id] = value
        }
        // UIListCache hides its template; UIScrollList only enables copies.
        setActive(bannerContract.cellTemplateID, false); setActive(bannerContract.pageTemplateID, false)
        let hiddenLength = Double(max(0, banner.artworks.count - 1)) * (bannerContract.cellSize.x + bannerContract.spacing)
        var container = pose.transforms[bannerContract.containerID] ?? HUDSourceTransformOverride()
        container.anchorMin = HUDSourceVector2(0, 1); container.anchorMax = HUDSourceVector2(0, 1)
        container.pivot = HUDSourceVector2(0, 1)
        container.sizeDelta = HUDSourceVector2(banner.artworks.isEmpty ? 0 : bannerContract.viewSize.x + hiddenLength,
            bannerContract.viewSize.y)
        container.anchoredPosition3D = HUDSourceVector3(-banner.normalizedPosition * hiddenLength, 0, 0)
        pose.transforms[bannerContract.containerID] = container
        for (index, instance) in bannerInstances.enumerated() {
            let enabled = banner.artworks.indices.contains(index)
            setActive(instance.rootID, enabled); setActive(instance.pageRootID, enabled)
            setActive(instance.pageOnID, enabled && index == banner.selectedIndex)
            setActive(instance.pageOffID, enabled && index != banner.selectedIndex)
            var cell = pose.transforms[instance.rootID] ?? HUDSourceTransformOverride()
            cell.anchoredPosition3D = HUDSourceVector3((bannerContract.viewSize.x - bannerContract.cellSize.x) / 2
                + Double(index) * (bannerContract.cellSize.x + bannerContract.spacing),
                -(bannerContract.viewSize.y - bannerContract.cellSize.y) / 2, 0)
            pose.transforms[instance.rootID] = cell
            // Original Selectable ColorTint starts in Normal (target alpha0).
            if pose.properties[instance.lightNodeID]?["m_Color.a"] == nil {
                pose.properties[instance.lightNodeID, default: [:]]["m_Color.a"] = 0
            }
            guard enabled else { continue }
            let name = banner.artworks[index]
            guard let spriteID = artwork[name] else { throw HUDSourceError.invalid("Unsupported explicit banner artwork: " + name) }
            result.sprites[instance.imageComponentID] = spriteID
        }
        return result
    }
}
