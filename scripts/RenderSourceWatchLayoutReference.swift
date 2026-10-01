import Foundation
import CoreGraphics
import simd

/// Native source mathematics without an NSWindow, MTLDevice or rasterization.
/// The source layout/text/image implementations are the production ones.
@main
enum RenderSourceWatchLayoutReference {
    struct Sample {
        let name: String
        let opening: Double?
        let ambient: Double?
        let closing: Double?
        let hover: Double?
        init(_ name: String, opening: Double? = nil, ambient: Double? = 0,
             closing: Double? = nil, hover: Double? = nil) {
            self.name = name; self.opening = opening; self.ambient = ambient
            self.closing = closing; self.hover = hover
        }
    }
    static func main() throws {
        guard CommandLine.arguments.count == 2 else { throw HUDSourceError.invalid("Expected layout reference output directory") }
        let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        let document = try HUDSourceWatchDocument()
        let domain = try HUDSourceWatchDomain(resourceRoot: document.root.appendingPathComponent("Domain"))
        let text = try HUDSourceTextGeometry(document: document, additionalComponents: domain.components,
            additionalLabels: domain.labels, additionalMaterials: domain.materials)
        let runtime = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: document.root.appendingPathComponent("runtime-root-camera.json")))
        let cameraModel = try HUDSourceWatchCamera(runtimeRoot: runtime)
        let size = SIMD2<Double>(1728, 1080)
        let viewport = CGRect(x: 0, y: 0, width: 1728, height: 1080)
        let euler = try cameraModel.gyro.targetEuler(mouseUnity: size / 2, screenSize: size)
        let rotation = try HUDSourceWatchCamera.quaternion(eulerDegrees: euler)
        let camera = try cameraModel.frame(screenSize: size, localRotation: rotation)
        let buttons = try HUDSourceWatchButtonAnimation(document: document)
        guard let battlepass = document.buttons.first(where: { $0.label?.literal == "通行证" }),
              document.animators.contains(where: { $0.rootID == battlepass.nodeID }) else {
            throw HUDSourceError.invalid("Missing original Battle Pass Animator identity")
        }
        var textures: [String: HUDSourceJSONValue] = [:]
        for t in document.sprites["source_textures"].array {
            if let id = t["id"].string { textures[id] = t }
        }
        var sprites: [HUDSourceID: HUDSourceImageGeometry.Sprite] = [:]
        for (id, record) in document.spriteByComponent {
            guard let textureID = record["texture"]["id"].string, let texture = textures[textureID] else {
                throw HUDSourceError.invalid("Unresolved original Sprite metadata: \(id)")
            }
            sprites[id] = try HUDSourceImageGeometry.Sprite(source: record, texture: texture)
        }
        let samples = [Sample("opening-000", opening: 0, ambient: nil),
            Sample("opening-100", opening: 0.1, ambient: nil), Sample("opening-250", opening: 0.25, ambient: nil),
            Sample("opening-500", opening: 0.5, ambient: nil), Sample("opening-end", opening: document.animation.entrance.lastKeyTime, ambient: nil),
            Sample("stable"), Sample("ambient-midpoint", ambient: document.animation.ambient.lastKeyTime / 2),
            Sample("hover-000", hover: 0), Sample("hover-033", hover: 1 / 30), Sample("hover-067", hover: 2 / 30),
            Sample("hover-100", hover: 0.1), Sample("hover-hold", hover: 1),
            Sample("closing-100", closing: 0.1), Sample("closing-250", closing: 0.25),
            Sample("closing-end", closing: document.animation.exit.lastKeyTime)]
        var reports: [[String: Any]] = []
        let main = Set(document.buttons.map(\.nodeID))
        func mainAncestor(_ id: HUDSourceID) -> HUDSourceID? {
            var node: HUDSourceID? = id
            while let current = node {
                if main.contains(current) { return current }
                node = document.scene.node(current)?.parentID
            }
            return nil
        }
        for sample in samples {
            buttons.reset(at: 0)
            if sample.hover != nil { buttons.setHovered(true, on: battlepass.nodeID, at: 0) }
            let opening = sample.opening.map { HUDSourceWatchPlayback.clipTime(elapsed: $0, length: document.animation.entrance.lastKeyTime) }
                ?? document.animation.entrance.lastKeyTime
            let closing = sample.closing.map { HUDSourceWatchPlayback.clipTime(elapsed: $0, length: document.animation.exit.lastKeyTime) }
            var pose = try document.animation.pose(entranceTime: opening, ambientTime: sample.ambient,
                exitTime: closing, canvasResolution: camera.layout.canvasSize)
            buttons.apply(to: &pose, at: sample.hover ?? 0)
            document.applyMacButtonAvailability(to: &pose)
            let layout = HUDSourceWatchLayout(document: document) { id, _ in try? text.preferredSize(on: id) }
            let layoutReport = try layout.apply(to: &pose, worldRoot: camera.worldRoot)
            let resolved = try document.scene.resolve(overrides: pose.transforms)
            let inheritedAlpha = document.inheritedAlpha(pose: pose)
            var geometryByButton: [HUDSourceID: [[String: Any]]] = [:]
            var failures: [String] = []
            for id in document.scene.traversalIDs {
                guard let owner = mainAncestor(id), let node = resolved[id], let rect = node.rect else { continue }
                let world = simd_mul(camera.worldRoot, node.worldMatrix)
                for component in document.components[id] ?? [] where component.enabled {
                    guard ["UIImage", "Image", "UIRawImage", "RawImage", "UIText", "NonDrawingGraphic"].contains(component.kind) else { continue }
                    var record: [String: Any] = ["id": id.rawValue, "path": node.node.path,
                        "componentID": component.id.rawValue, "kind": component.kind,
                        "active": node.activeInHierarchy, "rect": rectJSON(rect),
                        "localMatrix": matrix(node.localMatrix), "sceneMatrix": matrix(node.worldMatrix), "worldMatrix": matrix(world),
                        "worldRectCorners": rect.corners.map { worldPoint($0, matrix: world) },
                        "projectedRectCorners": rect.corners.map { project($0, world: world, camera: camera.camera, viewport: viewport) },
                        "inheritedCanvasAlpha": inheritedAlpha[id] ?? 1, "raycastTarget": component["m_RaycastTarget"].flag()]
                    let center = rect.origin + rect.size / 2
                    record["projectedCenter"] = project(SIMD3(center.x, center.y, 0), world: world, camera: camera.camera, viewport: viewport)
                    var masks: [[String: Any]] = []
                    var parent: HUDSourceID? = id
                    while let p = parent {
                        if document.component("RectMask2D", on: p) != nil, let n = resolved[p], let r = n.rect {
                            let m = simd_mul(camera.worldRoot, n.worldMatrix)
                            masks.append(["id": p.rawValue, "path": n.node.path, "rect": rectJSON(r),
                                "worldMatrix": matrix(m), "projectedCorners": r.corners.map { project($0, world: m, camera: camera.camera, viewport: viewport) }])
                        }
                        if p != id, document.component("Canvas", on: p)?["m_OverrideSorting"].flag() == true { break }
                        parent = document.scene.node(p)?.parentID
                    }
                    record["masks"] = masks
                    do {
                        if component.kind == "UIImage" || component.kind == "Image" {
                            let pivot = pose.transforms[id]?.pivot?.simd ?? node.node.transform.rect?.pivot.simd ?? SIMD2(0.5, 0.5)
                            let mesh = try HUDSourceImageGeometry.build(image: component, sprite: sprites[component.id], rect: rect,
                                pivot: pivot, fillAmount: pose.value("m_FillAmount", on: id, fallback: component["m_FillAmount"].float(1)))
                            record["imageVertices"] = mesh.positions.map { [Double($0.x), Double($0.y), Double($0.z), Double($0.w)] }
                            record["uv"] = mesh.uv.map { [Double($0.x), Double($0.y)] }
                            record["indices"] = mesh.indices
                            record["projectedImageVertices"] = mesh.positions.map { p in
                                project(SIMD3(Double(p.x), Double(p.y), Double(p.z)), world: world, camera: camera.camera, viewport: viewport)
                            }
                            if let sprite = document.spriteByComponent[component.id] {
                                record["sprite"] = ["id": sprite["id"].string ?? "", "rawSize": sprites[component.id].map { [$0.size.x, $0.size.y] } ?? [],
                                    "textureID": sprites[component.id]?.textureID ?? "",
                                    "textureRect": sprite["effective_render_data"]["textureRect"].object.mapValues { $0.float() }]
                            }
                        } else if component.kind == "UIText" {
                            record["literal"] = text.literal(on: id) ?? ""
                            record["preferredSize"] = try text.preferredSize(on: id).mapArray
                        }
                    } catch { record["geometryDiagnostic"] = String(describing: error); failures.append(node.node.path + ": " + String(describing: error)) }
                    record["sourceSampledProperties"] = pose.properties[id] ?? [:]
                    geometryByButton[owner, default: []].append(record)
                }
            }
            var buttonRecords: [[String: Any]] = []
            for button in document.buttons {
                guard let node = resolved[button.nodeID], let rect = node.rect else {
                    throw HUDSourceError.invalid("Unresolved original main button: \(button.path)")
                }
                let world = simd_mul(camera.worldRoot, node.worldMatrix)
                let center = rect.origin + rect.size / 2
                let projected = camera.camera.project(SIMD3(center.x, center.y, 0), world: world, viewport: viewport)
                buttonRecords.append(["id": button.nodeID.rawValue, "path": button.path, "label": button.label?.literal ?? "",
                    "active": node.activeInHierarchy, "rect": rectJSON(rect), "worldMatrix": matrix(world),
                    "worldCorners": rect.corners.map { worldPoint($0, matrix: world) },
                    "projectedCorners": rect.corners.map { project($0, world: world, camera: camera.camera, viewport: viewport) },
                    "projectedCenter": project(SIMD3(center.x, center.y, 0), world: world, camera: camera.camera, viewport: viewport),
                    "graphics": geometryByButton[button.nodeID] ?? []])
                let p = projected.map { String(format: "(%.3f,%.3f)", Double($0.point.x), Double($0.point.y)) } ?? "invalid projection"
                print(sample.name + " " + (button.label?.literal ?? node.node.name) + " center=" + p
                    + String(format: " rect=(%.3f,%.3f) active=", rect.size.x, rect.size.y) + String(node.activeInHierarchy))
            }
            reports.append(["sample": sample.name, "openingElapsed": optional(sample.opening), "openingClipTime": opening,
                "ambientClipTime": optional(sample.ambient), "closingElapsed": optional(sample.closing), "closingClipTime": optional(closing),
                "hoverElapsed": optional(sample.hover), "buttons": buttonRecords, "worldRoot": matrix(camera.worldRoot),
                "layoutDiagnostics": layoutReport.unverifiedCustomComponents.sorted(),
                "missingTextMetrics": layoutReport.missingTextMetrics.map(\.rawValue).sorted(),
                "geometryDiagnostics": failures, "unboundAnimationPaths": pose.unboundPaths.sorted(),
                "unregisteredAnimationBindings": pose.unregisteredBindings.sorted()])
        }
        let report: [String: Any] = ["schemaVersion": 1, "evidence": "Native Swift source geometry; no GPU or raster output",
            "commit": ProcessInfo.processInfo.environment["GITHUB_SHA"] ?? "local", "viewport": [1728, 1080], "mouseUnity": [864, 540],
            "cameraPolicy": "Source standard FOV with UIManager narrow-aspect runtime adjustment; settled original center-mouse gyro for every sampled wrapper pose",
            "matrixOrder": "JSON rows; matrices multiply column vectors; Unity +Y up; projected pixels +Y down",
            "canvasSize": [camera.layout.canvasSize.x, camera.layout.canvasSize.y], "worldScale": camera.layout.scale,
            "worldHeight": camera.layout.worldHeight, "originalFOVDegrees": cameraModel.verticalFieldOfViewDegrees,
            "standardFOVDegrees": cameraModel.verticalFieldOfViewDegrees,
            "runtimeFOVDegrees": camera.layout.runtimeVerticalFieldOfViewDegrees,
            "cameraNear": cameraModel.near, "cameraFar": cameraModel.far,
            "worldParent": matrix(cameraModel.worldParent), "rootPosition": [cameraModel.rootPosition.x, cameraModel.rootPosition.y, cameraModel.rootPosition.z],
            "gyroEulerDegrees": [euler.x, euler.y, euler.z], "gyroQuaternion": [rotation.x, rotation.y, rotation.z, rotation.w],
            "gyroDuration": cameraModel.gyro.duration, "gyroMaxPitch": cameraModel.gyro.maxPitch, "gyroMaxYaw": cameraModel.gyro.maxYaw,
            "referenceResolutionScale": cameraModel.referenceResolutionScale,
            "cameraWorld": matrix(cameraModel.cameraWorld), "view": matrix(camera.camera.view), "projection": matrix(camera.camera.projection),
            "viewProjection": matrix(camera.camera.viewProjection), "availability": "Shared document macOS policy; all 22 mapped buttons available",
            "recordingComparisonPassed": false, "samples": reports]
        try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
            .write(to: output.appendingPathComponent("source-layout-reference.json"), options: .atomic)
    }
    static func optional(_ value: Double?) -> Any { if let value { return value }; return NSNull() }
    static func matrix(_ m: simd_double4x4) -> [[Double]] { (0..<4).map { r in [m[0][r], m[1][r], m[2][r], m[3][r]] } }
    static func rectJSON(_ r: HUDSourceRect) -> [String: Any] { ["origin": [r.origin.x, r.origin.y], "size": [r.size.x, r.size.y]] }
    static func worldPoint(_ p: SIMD3<Double>, matrix: simd_double4x4) -> [Double] {
        let v: SIMD4<Double> = simd_mul(matrix, SIMD4<Double>(p.x, p.y, p.z, 1))
        return [v.x, v.y, v.z, v.w]
    }
    static func project(_ p: SIMD3<Double>, world: simd_double4x4, camera: HUDSourceCamera, viewport: CGRect) -> Any {
        guard let value = camera.project(p, world: world, viewport: viewport) else { return NSNull() }
        return ["point": [Double(value.point.x), Double(value.point.y)], "depth": value.depth, "clipW": value.clipW]
    }
}

private extension SIMD2 where Scalar == Double {
    var mapArray: [Double] { [x, y] }
}
