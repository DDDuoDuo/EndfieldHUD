import Foundation
import simd

enum HUDSourceDomainAnimationTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; if !value { fatalError(message) } }
        func close(_ value: Double?, _ expected: Double, _ message: String) {
            check(value.map { $0.isFinite && abs($0 - expected) < 1e-8 } ?? false, message)
        }
        func fails(_ message: String, _ operation: () throws -> Void) {
            do { try operation(); check(false, message) } catch { check(true, message) }
        }
        do {
            let domain = try HUDSourceWatchDomain()
            let idle = try domain.frame(domainWorld: matrix_identity_double4x4).animationPose
            let roots = domain.scene.nodes.filter { $0.path.hasSuffix("_building") }
            check(roots.count == 6, "The reference retains all six original Region01 model roots")
            for root in roots {
                close(idle.properties[root.id]?["material._OuterColor.r"], 0.7509433031082153,
                      "Each nonselected model samples its own original deselected clip endpoint")
                close(idle.properties[root.id]?["material._InnerColor.g"], 1,
                      "Source deselected inner color is retained before material RGB conversion")
            }
            guard let first = roots.first(where: { $0.path == "map01_lv001_building" }) else { fatalError("Missing source model") }
            let selected = try domain.frame(domainWorld: matrix_identity_double4x4,
                animationState: .init(currentLevelID: "map01_lv001")).animationPose
            close(selected.properties[first.id]?["material._OuterColor.r"], 0.849056601524353,
                  "Explicit current level samples the source selected endpoint")
            close(selected.properties[first.id]?["material._InnerColor.b"], 0.6773585081100464,
                  "Selection uses the authored color, independent of controller MPB colors")
            for root in roots where root.id != first.id {
                close(selected.properties[root.id]?["material._OuterColor.r"], 0.7509433031082153,
                      "Selecting one source level leaves every other model deselected")
            }
            let length = 0.1666666716337204
            let half = try domain.frame(domainWorld: matrix_identity_double4x4,
                animationState: .init(currentLevelID: "map01_lv001", selectionElapsed: length / 2)).animationPose
            // OutQuad(.5)=.75, then the independently known zero-tangent
            // Hermite segment gives 3*.75^2 - 2*.75^3 = .84375.
            close(half.properties[first.id]?["material._OuterColor.r"],
                  0.7509433031082153 + (0.849056601524353 - 0.7509433031082153) * 0.84375,
                  "Finite selection combines original wrapper easing with original curve interpolation")
            let hovering = try domain.frame(domainWorld: matrix_identity_double4x4,
                animationState: .init(hoverClipTimes: ["map01_lv001": length])).animationPose
            close(hovering.properties[first.id]?["material._Lightness"], 1.399999976158142,
                  "Hover samples the source model Lightness channel")
            guard let glow = domain.scene.nodes.first(where: { $0.path == "map01_lv001_building/Glow01_hover" }),
                  let shadow = domain.scene.nodes.first(where: { $0.path == "map01_lv001_building/Glow01_hover/Glow01_shadow_hover" }) else {
                fatalError("Missing exact source hover paths")
            }
            close(hovering.properties[glow.id]?["material._Alpha"], 1, "Hover alpha targets the original glow Renderer")
            close(hovering.properties[shadow.id]?["material._Alpha"], 0.10000000149011612,
                  "The shadow glow retains its separate authored shader alpha")
            let hoverExit = try domain.frame(domainWorld: matrix_identity_double4x4,
                animationState: .init(hoverClipTimes: ["map01_lv001": 0])).animationPose
            close(hoverExit.properties[glow.id]?["material._Alpha"], 0, "Source hover exit sampling removes only its own material glow")
            check(domain.scene.nodes.filter { $0.path.contains("_ground") }.allSatisfy { selected.properties[$0.id] == nil },
                  "Model selection does not overwrite unrelated ground materials")
            check(hovering.unboundPaths.isEmpty && hovering.unregisteredBindings.isEmpty,
                  "Instance-bound Domain clips resolve within their own source scene")
            let region02 = try HUDSourceWatchDomain(domainName: "Region02")
            guard let linearRoot = region02.scene.nodes.first(where: { $0.path == "map02_lv002_building" }) else {
                fatalError("Missing original Region02 linear wrapper")
            }
            let linearHalf = try region02.frame(domainWorld: matrix_identity_double4x4,
                animationState: .init(currentLevelID: "map02_lv002", selectionElapsed: length / 2)).animationPose
            close(linearHalf.properties[linearRoot.id]?["material._OuterColor.r"],
                  (0.7509433031082153 + 0.849056601524353) / 2,
                  "Region02 linear wrapper is not resampled through Region01 OutQuad easing")
            let loop0 = try region02.frame(domainWorld: matrix_identity_double4x4).animationPose
            let loopQuarter = try region02.frame(domainWorld: matrix_identity_double4x4,
                animationState: .init(ambientTime: 14.966666221618652 / 4)).animationPose
            let rotated = loopQuarter.transforms.filter { $0.value.localRotation != nil }
            check(rotated.count == 2, "Only the autoPlay wheel wrapper's two original Transform targets rotate")
            check(rotated.contains { loop0.transforms[$0.key]?.localRotation != $0.value.localRotation },
                  "Source wheel loop advances through its original quaternion keys")
            fails("A level outside the original loaded scene is rejected") {
                _ = try domain.frame(domainWorld: matrix_identity_double4x4, animationState: .init(currentLevelID: "map02_lv007"))
            }
            fails("Nonfinite Domain animation time is rejected") {
                _ = try domain.frame(domainWorld: matrix_identity_double4x4, animationState: .init(ambientTime: .nan))
            }
            fails("Negative hover clip time is rejected") {
                _ = try domain.frame(domainWorld: matrix_identity_double4x4, animationState: .init(hoverClipTimes: ["map01_lv001": -1]))
            }
        } catch { fatalError("Original Domain animation checks failed: \(error)") }
        return count
    }
}
