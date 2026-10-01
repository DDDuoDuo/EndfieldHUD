import Foundation
import simd

enum HUDSourceWatchDomainTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            count += 1; if !condition() { fatalError(message) }
        }
        func near(_ a: Double, _ b: Double, _ tolerance: Double = 1e-9) -> Bool { abs(a - b) < tolerance }
        do {
            func id(_ n: Int) -> HUDSourceID { HUDSourceID(rawValue: "CAB-domain-fixture:\(n)") }
            let base = try HUDSourceScene(rootID: id(1), nodes: [
                HUDSourceNode(id: id(1), path: "Base", name: "Base", parentID: nil, childIDs: [id(2)],
                    transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(10, 20, 30))),
                HUDSourceNode(id: id(2), path: "Base/Parent", name: "Parent", parentID: id(1), childIDs: [],
                    transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(1, 2, 3),
                        localScale: HUDSourceVector3(2, 2, 2)))])
            let level = try HUDSourceScene(rootID: id(3), nodes: [
                HUDSourceNode(id: id(3), path: "Level", name: "Level", parentID: nil, childIDs: [id(4)],
                    transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(4, 5, 6))),
                HUDSourceNode(id: id(4), path: "Level/Mesh", name: "Mesh", parentID: id(3), childIDs: [],
                    transform: HUDSourceTransform(kind: .transform, localPosition: HUDSourceVector3(7, 8, 9)))])
            let joined = try HUDSourceWatchDomain.join(base: base, attachments: [.init(scene: level, parentID: id(2))])
            let pose = try joined.resolve()
            check(joined.traversalIDs == [id(1), id(2), id(3), id(4)], "Domain join must preserve source sibling order")
            check(joined.node(id(3))?.parentID == id(2), "Source Level3D root must use its exact parent PPtr")
            check(joined.node(id(3))?.transform.localPosition == HUDSourceVector3(4, 5, 6), "Instantiate false must retain source root local position")
            let world = pose[id(4)]!.worldMatrix.columns.3
            check(near(world.x, 33) && near(world.y, 48) && near(world.z, 63), "Parent matrix must multiply original Level3D geometry")
            do {
                _ = try HUDSourceWatchDomain.join(base: base, attachments: [.init(scene: level, parentID: id(99))])
                fatalError("Invalid Domain parent must be rejected")
            } catch { check(true, "Missing source parent was rejected") }
            do {
                _ = try HUDSourceWatchDomain.join(base: base, attachments: [.init(scene: level, parentID: id(2)), .init(scene: level, parentID: id(2))])
                fatalError("Duplicate source root must be rejected")
            } catch { check(true, "Duplicate source root was rejected") }

            let rect = SIMD4<Double>(-10, -20, 30, 40)
            let minAnchor = try HUDSourceWatchDomain.iconAnchors(mapPosition: SIMD3(-10, 999, -20), uiRect: rect)
            let maxAnchor = try HUDSourceWatchDomain.iconAnchors(mapPosition: SIMD3(30, -999, 40), uiRect: rect)
            check(minAnchor == .zero && maxAnchor == SIMD2(repeating: 1), "Map icon must use source X/Z and min/max bounds")
            check(HUDSourceWatchDomain.intersects(rect, center: SIMD2(33, 40), radius: 3), "Circle tangent must load source adjacent region")
            check(!HUDSourceWatchDomain.intersects(rect, center: SIMD2(33, 40), radius: 2.999), "Outside circle must not load source region")
            check(!HUDSourceWatchDomain.intersects(rect, center: SIMD2(0, 0), radius: -1), "Invalid radius must not load source region")
            let center = HUDSourceGeometry.translation(SIMD3(10, 20, 30)) * HUDSourceGeometry.scale(SIMD3(2, 4, 5))
            let position = try HUDSourceWatchDomain.moveToPlayer(playerWorld: SIMD3(16, 28, 40), centerWorld: center)
            check(position == SIMD3(-3, -2, 0), "MoveToPlayer must use center inverse and replace localXY only")

            // Real resource decoding catches case-sensitive manifest paths,
            // decimal-string source IDs, vertex channels and exact parent joins.
            let domain = try HUDSourceWatchDomain()
            check(domain.scene.nodes.count == 82 && domain.loadedLevelIDs.count == 6, "All Region01 source assets must join to 82 original nodes")
            check(domain.selectionPolicy == "all-declared-source-levels-reference", "Source reference view must not claim account state")
            let q = try domain.sourceNormalRotation.normalized()
            check(near(q.x, -sqrt(0.5)) && near(q.y, 0) && near(q.z, 0) && near(q.w, sqrt(0.5)), "Normal rotation must be original world X=-90 degrees")
            let external = HUDSourceGeometry.translation(SIMD3(1, 2, 3)) * HUDSourceGeometry.scale(SIMD3(repeating: 0.01))
            let frame = try domain.frame(domainWorld: external)
            check(!frame.meshes.isEmpty, "Real Domain must emit original mesh batches")
            let local = try domain.scene.resolve()
            for batch in frame.meshes {
                let expected = external * local[batch.nodeID]!.worldMatrix
                let error = simd_length(batch.worldMatrix.columns.3 - expected.columns.3)
                check(error < 1e-9, "Mesh instance must retain exact external×source world matrix")
                check(batch.mesh.indices.allSatisfy { Int($0) < batch.mesh.positions.count }, "Original Domain mesh indices must stay within source vertex channels")
                check(batch.sourceUniformOverrides["_RegionMapEditor"]?.number == 0,
                      "Watch showType1 sets original _RegionMapEditor to0 on every renderer")
                check(batch.sourceUniformOverrides["_OuterColor"] == nil,
                      "Selected/normal outer color is not assigned without source selected-level state")
            }
            let selected = try HUDSourceWatchDomain(loadedLevelIDs: ["map01_lv001"])
            check(selected.loadedLevelIDs == ["map01_lv001"] && selected.scene.nodes.count < 82, "Explicit loaded levels must filter resource attachment, not guess account state")
            let hidden = try selected.frame(domainWorld: matrix_identity_double4x4, unlockedLevelIDs: [])
            check(hidden.meshes.allSatisfy { $0.levelID == nil }, "Locked level UI/building/ground roots must be hidden together")
            do {
                _ = try HUDSourceWatchDomain(loadedLevelIDs: ["not-a-source-level"])
                fatalError("Unknown source level must be rejected")
            } catch { check(true, "Unknown source level was rejected") }
        } catch { fatalError("Source Domain tests failed: \(error)") }
        return count
    }
}
