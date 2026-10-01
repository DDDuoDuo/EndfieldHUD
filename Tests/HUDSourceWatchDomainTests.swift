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

            func sorting(_ type: Double, _ offset: Double, enabled: Bool = true) -> HUDSourceWatchComponent {
                HUDSourceWatchComponent(id: id(95), type: "MonoBehaviour", script: "UISortingOrder",
                    data: ["_renderType": .number(type), "_sortingOrderOffset": .number(offset), "m_Enabled": .bool(enabled)])
            }
            let renderer: HUDSourceJSONValue = .object(["m_SortingOrder": .number(0)])
            check(HUDSourceWatchDomain.rendererSortingOrder(renderer: renderer, ownComponents: [sorting(0, -5)]) == -5,
                  "Renderer UISortingOrder uses absolute negative offset even when serialized renderer order is0")
            check(HUDSourceWatchDomain.rendererSortingOrder(renderer: renderer, ownComponents: [sorting(1, 11)]) == 0,
                  "Canvas offset must not be assigned to an attached MeshRenderer")
            check(HUDSourceWatchDomain.rendererSortingOrder(renderer: renderer, ownComponents: [sorting(0, -5, enabled: false)]) == -5,
                  "SetOrder has no Behaviour enabled guard; its Awake registration writes the absolute offset")

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
            let packed = Float(bitPattern: 0x40001234)
            let normal = try HUDSourceWatchDomain.normalVectors([[Double(packed)]], vertexCount: 1)[0]
            check(normal.x.bitPattern == packed.bitPattern && normal.y == 0 && normal.z == 0,
                  "Single-component packed normal must retain source x bits for shader decoding")
            let fullNormal = try HUDSourceWatchDomain.normalVectors([[0.25, -0.5, 1]], vertexCount: 1)[0]
            check(fullNormal == SIMD3(0.25, -0.5, 1), "Three-component normal must retain every source component")
            let payload: UInt32 = 0x7fc01234
            let packedNaN = try HUDSourceWatchDomain.normalVectors([[.nan]], vertexCount: 1, words: [[payload]])[0]
            check(packedNaN.x.bitPattern == payload && packedNaN.y == 0 && packedNaN.z == 0,
                  "Packed NaN normal must retain raw payload rather than a canonical Float.nan")
            do {
                _ = try HUDSourceWatchDomain.normalVectors([[.nan]], vertexCount: 1)
                fatalError("Packed non-finite normal without raw words must be rejected")
            } catch { check(true, "Missing packed normal payload was rejected") }
            do {
                _ = try HUDSourceWatchDomain.normalVectors([[1, 2]], vertexCount: 1)
                fatalError("Unreviewed normal dimension must be rejected")
            } catch { check(true, "Unreviewed normal dimension was rejected") }
            let local = try domain.scene.resolve()
            for batch in frame.meshes {
                let normals = try batch.mesh.normalVectors()
                check(normals.isEmpty || normals.count == batch.mesh.positions.count,
                      "Every actual Region01 mesh normal channel must support source GPU upload")
                check(batch.mesh.normal_words.map { words in
                    zip(normals, words).allSatisfy { $0.0.x.bitPattern == $0.1[0] }
                } ?? false, "Actual packed normal words must reach vertex upload without losing payload bits")
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
            let region02 = try HUDSourceWatchDomain(domainName: "Region02")
            let terrain = try region02.frame(domainWorld: matrix_identity_double4x4).meshes.first {
                $0.path.hasSuffix("/S_regionMap_terrain")
            }
            check(terrain?.renderer["m_SortingOrder"].number == 0 && terrain?.sourceRuntimeSortingOrder == -5,
                  "Actual Region02 terrain must use native absolute -5 writer instead of serialized0")
            let spaceship = try HUDSourceWatchDomain(domainName: "Spaceship")
            let ship = try spaceship.frame(domainWorld: matrix_identity_double4x4).meshes.first {
                $0.path.hasSuffix("/SpaceShip")
            }
            check(ship?.renderer["m_SortingOrder"].number == 0 && ship?.sourceRuntimeSortingOrder == -2,
                  "Actual Spaceship mesh must use native absolute -2 writer instead of serialized0")
        } catch { fatalError("Source Domain tests failed: \(error)") }
        return count
    }
}
