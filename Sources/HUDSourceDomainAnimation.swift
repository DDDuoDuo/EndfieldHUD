import Foundation

/// Original level-model wrappers, bound within the assembled Domain scene.
/// Account state is explicit; an unset current level is a deselected reference.
struct HUDSourceDomainAnimation {
    struct State {
        var currentLevelID: String? = nil
        /// nil seeks the selected source endpoint; finite elapsed time uses
        /// the original wrapper's ease and default timeScale 1.
        var selectionElapsed: Double? = nil
        /// Exact source clip times. Native hover retarget scheduling is not
        /// inferred from the clip's duration or replaced with a new easing.
        var hoverClipTimes: [String: Double] = [:]
        var ambientTime: Double = 0
    }
    private struct Binding {
        let rootID: HUDSourceID
        let levelID: String
        let ease: Int
        let selected: HUDSourceAnimationClip
        let deselected: HUDSourceAnimationClip
        let hover: HUDSourceAnimationClip?
    }
    private struct Source: Decodable {
        let schema_version: Int
        let instances: [SourceInstance]
    }
    private struct SourceInstance: Decodable {
        let root_node_id: HUDSourceID
        let levels: [HUDSourceJSONValue]
        let is_level_model_root: Bool
        let wrapper_data: HUDSourceJSONValue
        let animation_data: HUDSourceJSONValue
        let bound_clips: [HUDSourceAnimationClip]
    }
    private let scene: HUDSourceScene
    private let loadedLevelIDs: Set<String>
    private let bindings: [Binding]
    private let autoLoops: [HUDSourceAnimationClip]

    init(data: Data, scene: HUDSourceScene, domainName: String, loadedLevelIDs: Set<String>) throws {
        let decoder = HUDSourceJSON.decoder()
        let source = try decoder.decode(Source.self, from: data)
        guard source.schema_version == 1 else {
            throw HUDSourceError.invalid("Unsupported original Domain animation schema")
        }
        self.scene = scene; self.loadedLevelIDs = loadedLevelIDs
        var bindings: [Binding] = [], loops: [HUDSourceAnimationClip] = []
        var roots: Set<HUDSourceID> = []
        for instance in source.instances {
            let root = instance.root_node_id
            guard scene.node(root) != nil else { continue }
            guard roots.insert(root).inserted else {
                throw HUDSourceError.invalid("Repeated original Domain animation root")
            }
            let wrapper = instance.wrapper_data
            guard wrapper["m_Enabled"].flag(true), instance.animation_data["m_Enabled"].flag(true) else { continue }
            let clips = instance.bound_clips
            guard clips.flatMap(\.curves).flatMap(\.nodeIDs).allSatisfy({ scene.node($0) != nil }) else {
                throw HUDSourceError.invalid("Domain clip binding is outside its original assembled scene")
            }
            func unique(_ binding: String) throws -> HUDSourceAnimationClip? {
                let matches = clips.filter { $0.binding == binding }
                guard matches.count <= 1 else { throw HUDSourceError.invalid("Repeated original Domain clip binding") }
                return matches.first
            }
            if instance.is_level_model_root {
                guard let level = instance.levels.first(where: {
                    $0["domain"].string?.lowercased() == domainName.lowercased()
                })?["level_id"].string, loadedLevelIDs.contains(level),
                      let easeValue = wrapper["_options"]["animEase"].number,
                      easeValue == 1 || easeValue == 6,
                      let selected = try unique("_animationIn"), let deselected = try unique("_animationOut") else {
                    throw HUDSourceError.invalid("Unresolved original level-model wrapper")
                }
                bindings.append(Binding(rootID: root, levelID: level, ease: Int(easeValue), selected: selected,
                    deselected: deselected, hover: try unique("SourceAnimation")))
            }
            if wrapper["autoPlay"].flag(), let loop = try unique("_animationLoop") {
                guard wrapper["_options"]["animEase"].number == 1, loop.wrapMode == 2 else {
                    throw HUDSourceError.invalid("Unsupported original Domain auto-loop clock")
                }
                loops.append(loop)
            }
        }
        self.bindings = bindings; autoLoops = loops
    }

    func pose(state: State, overrides: [HUDSourceID: HUDSourceTransformOverride] = [:]) throws -> HUDSourceWatchPose {
        guard state.ambientTime.isFinite, state.ambientTime >= 0,
              state.selectionElapsed.map({ $0.isFinite && $0 >= 0 }) ?? true,
              state.currentLevelID.map({ loadedLevelIDs.contains($0) }) ?? true,
              state.hoverClipTimes.allSatisfy({ loadedLevelIDs.contains($0.key) && $0.value.isFinite && $0.value >= 0 }) else {
            throw HUDSourceError.invalid("Invalid explicit Domain animation state")
        }
        var pose = HUDSourceWatchPose(transforms: overrides)
        let base = try scene.resolve(overrides: overrides)
        for binding in bindings {
            let selected = state.currentLevelID == binding.levelID
            let clip = selected ? binding.selected : binding.deselected
            let time = selected ? state.selectionElapsed.map {
                binding.ease == 6 ? HUDSourceWatchPlayback.clipTime(elapsed: $0, length: clip.lastKeyTime)
                    : min($0, clip.lastKeyTime)
            } ?? clip.lastKeyTime : clip.lastKeyTime
            HUDSourceWatchAnimation.apply(clip, time: time, to: &pose, base: base, scene: scene)
            if let hoverTime = state.hoverClipTimes[binding.levelID], let hover = binding.hover {
                HUDSourceWatchAnimation.apply(hover, time: hoverTime, to: &pose, base: base, scene: scene)
            }
        }
        for loop in autoLoops {
            HUDSourceWatchAnimation.apply(loop, time: state.ambientTime, to: &pose, base: base, scene: scene)
        }
        return pose
    }
}
