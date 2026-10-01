import Foundation
import CoreGraphics
import simd

/// Pure source geometry/curve checks; no AppKit window, display timer, or game
/// installation. Expected positions/values below are independently calculated.
enum HUDSourceSceneTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func close(_ actual: Double, _ expected: Double, _ message: String, tolerance: Double = 1e-9,
                   file: StaticString = #file, line: UInt = #line) {
            check(actual.isFinite && abs(actual - expected) <= tolerance, message, file: file, line: line)
        }
        func fails(_ message: String, _ operation: () throws -> Void) {
            do { try operation(); check(false, message) } catch { check(true, message) }
        }
        do {
            let rootID = HUDSourceID(rawValue: "CAB-fixture:9223372036854775807")
            let childID = HUDSourceID(rawValue: "CAB-fixture:-9223372036854775808")
            // This is the extraction's real nested schema, including IDs whose
            // low bits cannot survive conversion through an IEEE-754 Double.
            let fixture = Data("""
            {"root_node_id":"CAB-fixture:9223372036854775807","nodes":[
              {"id":"CAB-fixture:9223372036854775807","path":"root","name":"root",
               "parent_id":null,"child_ids":["CAB-fixture:-9223372036854775808"],
               "game_object":{"data":{"m_IsActive":true}},"transform":{"type":"RectTransform","raw":{
                 "m_LocalPosition":{"x":999,"y":999,"z":10},"m_LocalRotation":{"x":0,"y":0,"z":0,"w":1},
                 "m_LocalScale":{"x":1,"y":1,"z":1},"m_AnchorMin":{"x":0,"y":0},"m_AnchorMax":{"x":0,"y":0},
                 "m_AnchoredPosition":{"x":0,"y":0},"m_SizeDelta":{"x":200,"y":100},"m_Pivot":{"x":0.25,"y":0.75}}}},
              {"id":"CAB-fixture:-9223372036854775808","path":"root/child","name":"child",
               "parent_id":"CAB-fixture:9223372036854775807","child_ids":[],
               "game_object":{"data":{"m_IsActive":true}},"transform":{"type":"RectTransform","raw":{
                 "m_LocalPosition":{"x":999,"y":999,"z":4},"m_LocalRotation":{"x":0,"y":0,"z":0,"w":1},
                 "m_LocalScale":{"x":1,"y":1,"z":1},"m_AnchorMin":{"x":0.2,"y":0.1},"m_AnchorMax":{"x":0.8,"y":0.7},
                 "m_AnchoredPosition":{"x":7,"y":-9},"m_SizeDelta":{"x":10,"y":-5},"m_Pivot":{"x":0.25,"y":0.75}}}}
            ]}
            """.utf8)
            let scene = try HUDSourceJSON.decoder().decode(HUDSourceScene.self, from: fixture)
            check(scene.rootID == rootID && scene.node(childID)?.parentID == rootID,
                  "Exact signed 64-bit CAB IDs survive extraction decoding")
            let roundTrip = try HUDSourceJSON.decoder().decode(HUDSourceScene.self, from: HUDSourceJSON.encoder().encode(scene))
            check(roundTrip.nodes.map { $0.id } == [rootID, childID], "Source IDs/order survive Codable round trip")
            let resolved = try scene.resolve()
            guard let child = resolved[childID], let childRect = child.rect else { fatalError("Missing resolved fixture child") }
            close(childRect.size.x, 130, "Stretched width = parent 200 × anchor span .6 + 10")
            close(childRect.size.y, 55, "Negative sizeDelta is added to stretched height")
            close(childRect.origin.x, -32.5, "Child local bounds respect its own pivot")
            close(childRect.origin.y, -41.25, "Child pivot is independent of parent pivot")
            close(child.localMatrix.columns.3.x, 27, "Anchor reference uses parent origin and child pivot")
            close(child.localMatrix.columns.3.y, -29, "RectTransform uses anchored XY instead of serialized local XY")
            close(child.worldMatrix.columns.3.z, 14, "RectTransform keeps source local Z through parent composition")
            var hidden = HUDSourceTransformOverride(); hidden.active = false
            let hiddenScene = try scene.resolve(overrides: [rootID: hidden])
            check(hiddenScene[childID]?.activeInHierarchy == false, "Inactive ancestors disable descendant geometry")
            var anchored = HUDSourceTransformOverride(); anchored.anchoredPosition3D = HUDSourceVector3(10, 20, 30)
            let moved = try scene.resolve(overrides: [childID: anchored])
            close(moved[childID]!.localMatrix.columns.3.x, 30, "Animated anchored XY keeps the source anchor reference")
            close(moved[childID]!.worldMatrix.columns.3.z, 40, "Animated anchoredPosition3D overrides Z")
            let noRectParent = HUDSourceRectTransform(anchorMin: HUDSourceVector2(1, 1), anchorMax: HUDSourceVector2(1, 1),
                anchoredPosition: HUDSourceVector2(3, 4), sizeDelta: HUDSourceVector2(40, 20), pivot: HUDSourceVector2(0.5, 0.5))
                .layout(parent: nil, localZ: 5)
            close(noRectParent.position.x, 3, "A Transform parent has no inherited ancestor anchor width")
            close(noRectParent.position.y, 4, "A Transform parent keeps explicit anchored coordinates")

            let transformRoot = HUDSourceNode(id: rootID, path: "root", name: "root", parentID: nil, childIDs: [childID],
                transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(10, 20, 5),
                    localRotation: HUDSourceQuaternion(0, 0, sqrt(2), sqrt(2)), localScale: HUDSourceVector3(2, 3, 1)))
            let transformChild = HUDSourceNode(id: childID, path: "root/child", name: "child", parentID: rootID, childIDs: [],
                transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(4, 2, -1)))
            // Parent T(10,20,5) Rz(90°) S(2,3,1) maps child origin
            // (4,2,-1) to (10-6,20+8,5-1) = (4,28,4).
            let unordered = try HUDSourceScene(rootID: rootID, nodes: [transformChild, transformRoot])
            check(unordered.traversalIDs == [rootID, childID], "Render traversal preserves child order independently of array order")
            let composed = try unordered.resolve()[childID]!
            close(composed.worldMatrix.columns.3.x, 4, "Parent TRS applies scale before rotation before translation")
            close(composed.worldMatrix.columns.3.y, 28, "Parent evaluation does not depend on serialized array order")
            close(composed.worldMatrix.columns.3.z, 4, "Parent composition retains depth")
            let transformed = composed.worldMatrix * SIMD4<Double>(1, 0, 0, 1)
            close(transformed.x, 4, "A normalized quaternion rotates the scaled local X axis")
            close(transformed.y, 30, "Child vertices use the same world matrix as the child origin")
            let zeroRoot = HUDSourceNode(id: rootID, path: "root", name: "root", parentID: nil, childIDs: [],
                transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(0, 0, 0),
                    localScale: HUDSourceVector3(0, 0, 0)))
            let zeroScene = try HUDSourceScene(rootID: rootID, nodes: [zeroRoot])
            check(HUDSourceGeometry.inverse(try zeroScene.resolve()[rootID]!.worldMatrix) == nil,
                  "Serialized hidden root scale is preserved until explicit runtime initialization")
            var initialized = HUDSourceTransformOverride(); initialized.localScale = HUDSourceVector3(1, 1, 1)
            check(HUDSourceGeometry.inverse(try zeroScene.resolve(overrides: [rootID: initialized])[rootID]!.worldMatrix) != nil,
                  "Runtime initialization is a caller-supplied override")
            fails("Duplicate source IDs are rejected") { _ = try HUDSourceScene(rootID: rootID, nodes: [zeroRoot, zeroRoot]) }
            fails("Dangling parent/child references are rejected") { _ = try HUDSourceScene(rootID: rootID, nodes: [transformRoot]) }
            let orphan = HUDSourceNode(id: childID, path: "orphan", name: "orphan", parentID: nil, childIDs: [],
                transform: transformChild.transform)
            fails("Disconnected nodes cannot silently lose their parent transform") {
                _ = try HUDSourceScene(rootID: rootID, nodes: [zeroRoot, orphan])
            }
            fails("Zero quaternion is rejected rather than inventing a pose") { _ = try HUDSourceQuaternion(0, 0, 0, 0).matrix() }

            let viewport = CGRect(x: 10, y: 20, width: 400, height: 200)
            let rect = HUDSourceRect(size: HUDSourceVector2(40, 40), pivot: HUDSourceVector2(0.5, 0.5))
            let ortho = try HUDSourceCamera.orthographic(view: matrix_identity_double4x4,
                left: -100, right: 100, bottom: -50, top: 50, near: 1, far: 101)
            let orthoWorld = HUDSourceGeometry.translation(SIMD3(20, -10, 11))
            let orthographicPoint = ortho.project(SIMD3(5, 15, 0), world: orthoWorld, viewport: viewport)!
            close(Double(orthographicPoint.point.x), 260, "Orthographic projection respects viewport X origin")
            close(Double(orthographicPoint.point.y), 110, "Only viewport projection flips Unity +Y into AppKit downY")
            close(orthographicPoint.depth, 0.1, "Orthographic NDC depth is zero-to-one")
            let orthoHit = ortho.hit(orthographicPoint.point, world: orthoWorld, rect: rect, viewport: viewport)!
            close(orthoHit.x, 5, "Orthographic hit returns local X from the rendering matrix")
            close(orthoHit.y, 15, "Orthographic hit returns local +Y up")

            let perspective = try HUDSourceCamera.perspective(view: matrix_identity_double4x4,
                verticalFieldOfViewRadians: .pi / 2, aspect: 2, near: 1, far: 11)
            let perspectiveWorld = HUDSourceGeometry.translation(SIMD3(0, 0, 5))
            // FOV90, aspect2: NDC=(x/(2z),y/z)=(.2,.2), w=z=5.
            let point = perspective.project(SIMD3(2, 1, 0), world: perspectiveWorld, viewport: viewport)!
            close(Double(point.point.x), 250, "Perspective uses injected FOV/aspect and +Z forward")
            close(Double(point.point.y), 100, "Perspective screen Y is flipped once")
            close(point.depth, 0.88, "Perspective near/far mapping gives (1.1z-1.1)/z")
            let perspectiveHit = perspective.hit(point.point, world: perspectiveWorld, rect: rect, viewport: viewport)!
            close(perspectiveHit.x, 2, "Perspective inverse ray agrees with render projection")
            close(perspectiveHit.y, 1, "Perspective inverse ray retains exact local coordinates")
            let tilt = try HUDSourceQuaternion(sin(.pi / 12), 0, 0, cos(.pi / 12)).matrix()
            let tiltedWorld = perspectiveWorld * tilt * HUDSourceGeometry.scale(SIMD3(-2, 3, 1))
            let tiltedPoint = perspective.project(SIMD3(0.5, -0.25, 0), world: tiltedWorld, viewport: viewport)!
            let tiltedHit = perspective.hit(tiltedPoint.point, world: tiltedWorld, rect: rect, viewport: viewport)!
            close(tiltedHit.x, 0.5, "Tilted, mirrored hierarchy projects and hits using one matrix")
            close(tiltedHit.y, -0.25, "Perspective intersection supports nonuniform scale")
            check(perspective.project(.zero, world: HUDSourceGeometry.translation(SIMD3(0, 0, -5)), viewport: viewport) == nil,
                  "Geometry behind a perspective camera is not projected")
            check(perspective.hit(CGPoint(x: 210, y: 120), world: HUDSourceGeometry.translation(SIMD3(0, 0, 0.5)),
                rect: rect, viewport: viewport) == nil, "Hit cannot select geometry in front of the near plane")
            check(perspective.hit(point.point, world: HUDSourceGeometry.scale(.zero), rect: rect, viewport: viewport) == nil,
                  "Singular source transforms fail hit testing safely")
            check(perspective.hit(CGPoint(x: 500, y: 100), world: perspectiveWorld, rect: rect, viewport: viewport) == nil,
                  "Hit points outside the injected viewport are rejected")
            check(perspective.project(.zero, world: perspectiveWorld, viewport: .zero) == nil,
                  "Empty viewport does not yield a fabricated projection")
            fails("Invalid camera ranges are rejected") {
                _ = try HUDSourceCamera.perspective(view: matrix_identity_double4x4,
                    verticalFieldOfViewRadians: .pi, aspect: 2, near: 1, far: 11)
            }

            let linear = try HUDSourceScalarCurve(keys: [HUDSourceScalarKey(time: 2, value: 4, outSlope: 3),
                HUDSourceScalarKey(time: 4, value: 10, inSlope: 3)])
            close(linear.sample(at: 2.5)!, 5.5, "Hermite tangent slopes use actual seconds, including nonunit duration")
            close(linear.sample(at: -10)!, 4, "Source pre-infinity mode 2 holds the first key")
            close(linear.sample(at: 10)!, 10, "Source post-infinity mode 2 holds the last key")
            check(linear.sample(at: .nan) == nil, "Nonfinite sampling time is rejected")
            let smooth = try HUDSourceScalarCurve(keys: [HUDSourceScalarKey(time: 0, value: 0), HUDSourceScalarKey(time: 2, value: 1)])
            close(smooth.sample(at: 0.5)!, 0.15625, "Zero slopes produce analytic smoothstep at u=.25")
            let stepped = try HUDSourceScalarCurve(keys: [HUDSourceScalarKey(time: 0, value: 0, outSlope: .infinity),
                HUDSourceScalarKey(time: 1, value: 1, inSlope: .infinity), HUDSourceScalarKey(time: 2, value: 2)])
            close(stepped.sample(at: 0.999999)!, 0, "Infinite tangents hold the left key without NaN arithmetic")
            close(stepped.sample(at: 1)!, 1, "A stepped curve changes at the exact key boundary")
            let weighted = try HUDSourceScalarCurve(keys: [HUDSourceScalarKey(time: 2, value: 3, outSlope: 2,
                weightedMode: 2, outWeight: 0.2), HUDSourceScalarKey(time: 6, value: 7, inSlope: -1,
                weightedMode: 1, inWeight: 0.4)])
            // Bezier u=.5: time handles [2,2.8,4.4,6] give t=3.7;
            // value handles [3,4.6,8.6,7] give value=6.2.
            close(weighted.sample(at: 3.7)!, 6.2, "Weighted tangents invert Bezier time rather than using normalized seconds")
            let crossed = try HUDSourceScalarCurve(keys: [HUDSourceScalarKey(time: 0, value: 0, weightedMode: 2, outWeight: 1),
                HUDSourceScalarKey(time: 1, value: 1, weightedMode: 1, inWeight: 1)])
            close(crossed.sample(at: 0.5)!, 0.5, "Crossing time handles with zero midpoint derivative stay bounded")
            fails("Duplicate curve times are rejected") {
                _ = try HUDSourceScalarCurve(keys: [HUDSourceScalarKey(time: 1, value: 1), HUDSourceScalarKey(time: 1, value: 2)])
            }
            fails("NaN tangent is rejected") { _ = try HUDSourceScalarCurve(keys: [HUDSourceScalarKey(time: 0, value: 1, outSlope: .nan)]) }

            let animationFixture = Data("""
            {"clips":[{"binding":"_animationLoop","id":"CAB-fixture:9223372036854775807","name":"source-loop",
              "sample_rate":30,"wrap_mode":2,"last_key_time":2,"curves":[
              {"group":"m_FloatCurves","path":"child","attribute":"m_IsActive","node_matches":["CAB-fixture:-9223372036854775808"],
                "raw":{"curve":{"m_PreInfinity":2,"m_PostInfinity":2,"m_Curve":[
                  {"time":0,"value":0,"inSlope":0,"outSlope":"Infinity","weightedMode":0,"inWeight":0.3333333333333333,"outWeight":0.3333333333333333},
                  {"time":1,"value":1,"inSlope":"Infinity","outSlope":0,"weightedMode":0,"inWeight":0.3333333333333333,"outWeight":0.3333333333333333}]}}},
              {"group":"m_RotationCurves","path":"child","attribute":"m_RotationCurves","node_matches":["CAB-fixture:-9223372036854775808"],
                "raw":{"curve":{"m_PreInfinity":2,"m_PostInfinity":2,"m_Curve":[
                  {"time":0,"value":{"x":0,"y":0,"z":0,"w":1},"inSlope":{"x":0,"y":0,"z":0,"w":0},"outSlope":{"x":0,"y":0,"z":0,"w":0},
                   "weightedMode":0,"inWeight":{"x":0.3333333333333333,"y":0.3333333333333333,"z":0.3333333333333333,"w":0.3333333333333333},"outWeight":{"x":0.3333333333333333,"y":0.3333333333333333,"z":0.3333333333333333,"w":0.3333333333333333}},
                  {"time":1,"value":{"x":0,"y":0,"z":1,"w":0},"inSlope":{"x":0,"y":0,"z":0,"w":0},"outSlope":{"x":0,"y":0,"z":0,"w":0},
                   "weightedMode":0,"inWeight":{"x":0.3333333333333333,"y":0.3333333333333333,"z":0.3333333333333333,"w":0.3333333333333333},"outWeight":{"x":0.3333333333333333,"y":0.3333333333333333,"z":0.3333333333333333,"w":0.3333333333333333}}]}}}
              ]}]}
            """.utf8)
            let library = try HUDSourceJSON.decoder().decode(HUDSourceAnimationLibrary.self, from: animationFixture)
            let clip = library.clips[0]
            check(clip.curves[0].sample(at: 0.5) == .scalar(0), "String Infinity in the runtime manifest retains stepped behavior")
            check(clip.curves[0].nodeIDs == [childID], "Animation targets retain exact source node IDs")
            guard case .quaternion(let quaternion)? = clip.curves[1].sample(at: 0.5) else { fatalError("Missing quaternion sample") }
            close(quaternion.z, sqrt(0.5), "Quaternion component Hermite sample is normalized after interpolation")
            close(quaternion.w, sqrt(0.5), "Quaternion normalization retains the source component ratio")
            close(clip.localTime(2.25)!, 0.25, "Loop wraps at the whole source clip duration")
            close(clip.localTime(-0.25)!, 1.75, "Negative phase wraps consistently for a loop")
            let animationRoundTrip = try HUDSourceJSON.decoder().decode(HUDSourceAnimationLibrary.self,
                from: HUDSourceJSON.encoder().encode(library))
            check(animationRoundTrip.clips[0].curves[0].sample(at: 1) == .scalar(1),
                  "Infinity animation keys survive a standards-compliant JSON round trip")
        } catch { fatalError("Source scene fixture failed: \(error)") }
        return count
    }
}
