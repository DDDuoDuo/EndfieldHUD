import AppKit
import MetalKit
import ImageIO
import UniformTypeIdentifiers
import simd

/// Captures only the fixture's Metal drawable. No desktop screenshots,
/// account data or source recording is read on the CI host.
@main
enum RenderSourceWatchPreviews {
    private static func trace(_ message: String) {
        FileHandle.standardError.write(Data(("Source GPU fixture: " + message + "\n").utf8))
    }
    static func main() throws {
        do { try capture() }
        catch {
            if CommandLine.arguments.count == 2 {
                let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
                try? FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
                let report: [String: Any] = ["schemaVersion": 1,
                    "commit": ProcessInfo.processInfo.environment["GITHUB_SHA"] ?? "local",
                    "captureSucceeded": false, "recordingPixelComparisonPassed": false,
                    "metalDevice": MTLCreateSystemDefaultDevice()?.name ?? "Unavailable",
                    "error": String(describing: error)]
                if let data = try? JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys]) {
                    try? data.write(to: output.appendingPathComponent("source-preview-failure.json"))
                }
            }
            throw error
        }
    }
    private static func capture() throws {
        guard CommandLine.arguments.count == 2 else { throw HUDSourceError.invalid("Expected preview output directory") }
        let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let size = SIMD2<Double>(1728, 1080), viewport = CGRect(x: 0, y: 0, width: size.x, height: size.y)
        trace("loading original scene")
        let document = try HUDSourceWatchDocument()
        let runtime = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: document.root.appendingPathComponent("runtime-root-camera.json")))
        let camera = try HUDSourceWatchCamera(runtimeRoot: runtime)
        trace("loading original Metal programs, materials and mip chains")
        let renderer = try HUDSourceMetalRenderer(frame: viewport)
        trace("building original image, text and Domain geometry")
        let builder = try HUDSourceWatchFrameBuilder(document: document, renderer: renderer)
        // A separate explicit fixture exercises the source's four-slot model.
        // This does not assign an account's current level to the default view.
        let region02 = try HUDSourceWatchDomain(resourceRoot: document.root.appendingPathComponent("Domain"),
            domainName: "Region02", loadedLevelIDs: ["map02_lv008"])
        let region02Builder = try HUDSourceWatchFrameBuilder(document: document, renderer: renderer, domain: region02)
        let levelClips = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: region02.root.appendingPathComponent("level-clips.json")))
        guard let fourSlotInstance = levelClips["instances"].array.first(where: { instance in
            instance["is_level_model_root"].flag() && instance["levels"].array.contains {
                $0["domain"].string == "region02" && $0["level_id"].string == "map02_lv008"
            }
        }), let fourSlotRootString = fourSlotInstance["root_node_id"].string,
              let fourSlotRenderer = region02.components[HUDSourceID(rawValue: fourSlotRootString)]?
                .first(where: { $0.kind == "MeshRenderer" }) else {
            throw HUDSourceError.invalid("Missing original Region02 lv008 model renderer")
        }
        let fourSlotRoot = HUDSourceID(rawValue: fourSlotRootString)
        let fourSlotMaterialIDs = fourSlotRenderer["m_Materials"].array.compactMap { $0.targetID?.rawValue }
        guard fourSlotMaterialIDs.count == 4,
              let selectedClipID = fourSlotInstance["wrapper_clip_fields"]["_animationIn"].string,
              let selectedClip = levelClips["clips"].array.first(where: { $0["id"].string == selectedClipID }),
              let hoverClip = levelClips["clips"].array.first(where: { $0["name"].string == "regionmap3d_map_hover" }),
              let hoverEndpointTime = hoverClip["last_key_time"].number else {
            throw HUDSourceError.invalid("Missing original four-slot material/animation contract")
        }
        // Read the authored endpoint keys independently of the runtime sampler.
        // The regression checks renderer-wide propagation, plus the material's
        // real GPU color conversion, for every original slot's actual batch.
        func endpointProperties(_ clip: HUDSourceJSONValue) throws -> [String: [Float]] {
            var channels: [String: [Int: Float]] = [:]
            for curve in clip["curves"].array where curve["path"].string == "" && curve["class_id"].number == 23 {
                guard let attribute = curve["attribute"].string,
                      let key = curve["raw"]["curve"]["m_Curve"].array.last,
                      let value = key["value"].number, value.isFinite else {
                    throw HUDSourceError.invalid("Invalid original four-slot endpoint key")
                }
                let parts = attribute.split(separator: ".").map(String.init)
                guard parts.first == "material", parts.count == 2 || parts.count == 3 else { continue }
                let property = parts[1]
                guard ["_OuterColor", "_InnerColor", "_Lightness"].contains(property) else { continue }
                let index: Int
                if parts.count == 2 { index = 0 }
                else {
                    guard let component = ["r": 0, "g": 1, "b": 2, "a": 3][parts[2]] else {
                        throw HUDSourceError.invalid("Unexpected original material color channel")
                    }
                    index = component
                }
                channels[property, default: [:]][index] = Float(value)
            }
            var result: [String: [Float]] = [:]
            for (property, values) in channels {
                let count = property == "_Lightness" ? 1 : 4
                guard values.count == count, (0..<count).allSatisfy({ values[$0] != nil }) else {
                    throw HUDSourceError.invalid("Incomplete original four-slot endpoint")
                }
                result[property] = (0..<count).map { values[$0]! }
            }
            return result
        }
        let selectedEndpoint = try endpointProperties(selectedClip)
        let hoverEndpoint = try endpointProperties(hoverClip)
        guard Set(selectedEndpoint.keys) == Set(["_OuterColor", "_InnerColor", "_Lightness"]),
              hoverEndpoint["_Lightness"] != nil else {
            throw HUDSourceError.invalid("Original four-slot endpoint properties differ")
        }
        func placement(_ domain: String) throws -> HUDSourceID {
            guard let node = document.scene.nodes.first(where: {
                $0.path.hasSuffix("/Map/RegionRoot/RegionMask/MoveRoot/" + domain)
            }) else { throw HUDSourceError.invalid("Missing source fixture Domain placement") }
            return node.id
        }
        let region01Placement = try placement("Region01"), region02Placement = try placement("Region02")
        let abiEncoder = JSONEncoder()
        abiEncoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try abiEncoder.encode(renderer.constantBufferABI)
            .write(to: output.appendingPathComponent("source-constant-buffer-abi.json"))
        trace("source resources loaded")
        let buttons = try HUDSourceWatchButtonAnimation(document: document)
        let window = NSWindow(contentRect: viewport, styleMask: [.borderless], backing: .buffered, defer: false)
        window.isOpaque = false; window.backgroundColor = .clear; window.hasShadow = false
        window.contentView = renderer; window.orderFront(nil)
        renderer.drawableSize = CGSize(width: size.x, height: size.y)
        app.finishLaunching(); window.displayIfNeeded()
        let gyroEuler = try camera.gyro.targetEuler(mouseUnity: size / 2, screenSize: size)
        let gyro = try HUDSourceWatchCamera.quaternion(eulerDegrees: gyroEuler)
        let view = try camera.frame(screenSize: size, localRotation: gyro)
        let gpuY = HUDSourceGeometry.scale(SIMD3<Double>(1, -1, 1))
        let gpuProjection = HUDSourceGeometry.floatMatrix(simd_mul(gpuY, view.camera.projection))
        let gpu = HUDSourceMetalRenderer.Camera(viewProjection: HUDSourceGeometry.floatMatrix(simd_mul(gpuY, view.camera.viewProjection)),
            viewNoTranslationProjection: try HUDSourceWatchCamera.viewNoTranslationProjection(
                gpuProjection: gpuProjection, view: view.camera.view),
            worldSpacePosition: SIMD3(Float(camera.cameraWorld.columns.3.x), Float(camera.cameraWorld.columns.3.y), Float(camera.cameraWorld.columns.3.z)),
            timeSeconds: 0, renderPathInjected: 1, flipX: 0, flipY: 0,
            projection: gpuProjection, inverseView: HUDSourceGeometry.floatMatrix(camera.shaderCameraToWorld),
            uiProjectionParameters: try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: gpuProjection,
                near: Float(camera.near), far: Float(camera.far)))
        var manifest: [[String: Any]] = []
        func timestamp(_ value: Double?) -> Any {
            if let value { return value }
            return NSNull()
        }
        guard let battlepass = document.buttons.first(where: { $0.label?.literal == "通行证" }) else {
            throw HUDSourceError.invalid("Missing original Battle Pass button")
        }
        let samples: [(String, Double?, Double?, Double?, Double?)] = [
            ("opening-000", 0, nil, nil, nil), ("opening-100", 0.1, nil, nil, nil),
            ("opening-250", 0.25, nil, nil, nil), ("opening-500", 0.5, nil, nil, nil),
            ("stable", nil, 0, nil, nil), ("ambient-midpoint", nil, document.animation.ambient.lastKeyTime / 2, nil, nil),
            ("hover-000", nil, 0, nil, 0), ("hover-033", nil, 0, nil, 1 / 30),
            ("hover-067", nil, 0, nil, 2 / 30), ("hover-100", nil, 0, nil, 0.1),
            ("hover-hold", nil, 0, nil, 1), ("closing-100", nil, 0, 0.1, nil),
            ("closing-250", nil, 0, 0.25, nil),
            ("domain-selected-000", nil, 0, nil, nil),
            ("domain-selected-083", nil, 0, nil, nil),
            ("domain-selected-167", nil, 0, nil, nil),
            ("domain-hover-hold", nil, 0, nil, nil),
            ("domain-region02-lv008-selected", nil, 0, nil, nil),
            ("domain-region02-lv008-hover", nil, 0, nil, nil)
        ]
        for (name, opening, ambient, closing, hover) in samples {
            trace("resolving " + name)
            buttons.reset(at: 0)
            if hover != nil { buttons.setHovered(true, on: battlepass.nodeID, at: 0, reduceMotion: false) }
            var pose = try document.animation.pose(entranceTime: opening.map {
                HUDSourceWatchPlayback.clipTime(elapsed: $0, length: document.animation.entrance.lastKeyTime)
            } ?? document.animation.entrance.lastKeyTime, ambientTime: ambient,
                exitTime: closing.map { HUDSourceWatchPlayback.clipTime(elapsed: $0, length: document.animation.exit.lastKeyTime) },
                canvasResolution: view.layout.canvasSize)
            document.applyMacButtonAvailability(to: &pose)
            buttons.apply(to: &pose, at: hover ?? 0, reduceMotion: false)
            let isFourSlotFixture = name.hasPrefix("domain-region02-lv008-")
            if isFourSlotFixture {
                var disabled = pose.transforms[region01Placement] ?? HUDSourceTransformOverride()
                disabled.active = false; pose.transforms[region01Placement] = disabled
                var enabled = pose.transforms[region02Placement] ?? HUDSourceTransformOverride()
                enabled.active = true; pose.transforms[region02Placement] = enabled
            }
            var domainState = HUDSourceDomainAnimation.State(ambientTime: ambient ?? opening ?? closing ?? 0)
            if isFourSlotFixture {
                domainState.currentLevelID = "map02_lv008"
                if name.hasSuffix("-hover") { domainState.hoverClipTimes = ["map02_lv008": hoverEndpointTime] }
            } else if name.hasPrefix("domain-") {
                domainState.currentLevelID = "map01_lv001"
                if name == "domain-selected-000" { domainState.selectionElapsed = 0 }
                if name == "domain-selected-083" { domainState.selectionElapsed = 0.1666666716337204 / 2 }
                if name == "domain-hover-hold" { domainState.hoverClipTimes = ["map01_lv001": 0.1666666716337204] }
            }
            let activeBuilder = isFourSlotFixture ? region02Builder : builder
            let frame = try activeBuilder.build(pose: pose, worldRoot: view.worldRoot, domainAnimationState: domainState)
            var materialRegression: [[String: Any]] = []
            if isFourSlotFixture {
                guard frame.resolved[region02Placement]?.activeInHierarchy == true,
                      frame.resolved[region01Placement]?.activeInHierarchy == false else {
                    throw HUDSourceError.invalid("Explicit four-slot fixture placement was not applied")
                }
                let targetBatches = frame.batches.filter { $0.sourceNodeID == fourSlotRoot.rawValue }
                guard targetBatches.count == 4, targetBatches.map(\.material) == fourSlotMaterialIDs else {
                    throw HUDSourceError.invalid("Four-slot source renderer did not produce its four original batches")
                }
                var sourceExpected = selectedEndpoint
                if name.hasSuffix("-hover") { sourceExpected.merge(hoverEndpoint) { _, hover in hover } }
                for (slot, batch) in targetBatches.enumerated() {
                    var expected: [String: [Float]] = [:]
                    for (property, sourceValue) in sourceExpected {
                        let gpuValue = renderer.gpuMaterialValue(sourceValue, property: property, materialKey: batch.material)
                        guard let actual = batch.uniformOverrides[property], actual.count == gpuValue.count,
                              zip(actual, gpuValue).allSatisfy({ $0.0.isFinite && abs($0.0 - $0.1) < 0.000001 }) else {
                            throw HUDSourceError.invalid("Renderer-wide \(property) missing/different on source lv008 slot \(slot)")
                        }
                        expected[property] = gpuValue
                    }
                    materialRegression.append(["slot": slot, "nodeID": fourSlotRoot.rawValue,
                        "materialID": batch.material, "expectedGPUValues": expected.mapValues { $0.map(Double.init) },
                        "actualGPUValues": batch.uniformOverrides.filter { expected[$0.key] != nil }
                            .mapValues { $0.map(Double.init) },
                        "indexFirst": batch.indexRange?.lowerBound ?? -1, "indexCount": batch.indexRange?.count ?? -1])
                }
                try JSONSerialization.data(withJSONObject: materialRegression, options: [.prettyPrinted, .sortedKeys])
                    .write(to: output.appendingPathComponent(name + "-material-regression.json"))
            }
            if name == "stable" {
                let batches: [[String: Any]] = frame.batches.map { batch in
                    let matrix = (0..<4).map { column in (0..<4).map { row in Double(batch.world[column][row]) } }
                    return ["mesh": batch.mesh, "material": batch.material, "worldColumns": matrix,
                        "sourceNodeID": batch.sourceNodeID.map { $0 as Any } ?? NSNull(),
                        "color": [batch.color.x, batch.color.y, batch.color.z, batch.color.w].map { Double($0) },
                        "uniformOverrides": batch.uniformOverrides.mapValues { $0.map { Double($0) } },
                        "textureOverrides": batch.textureOverrides,
                        "colorWriteMask": batch.colorWriteMask.map { Int($0) } ?? -1]
                }
                try JSONSerialization.data(withJSONObject: batches, options: [.prettyPrinted, .sortedKeys])
                    .write(to: output.appendingPathComponent("source-batches-stable.json"))
            }
            trace("drawing " + name)
            var clock = gpu; clock.timeSeconds = Float(ambient ?? opening ?? closing ?? 0)
            renderer.submit(camera: clock, batches: frame.batches)
            renderer.draw()
            let image = try renderer.copyDrawableImage()
            guard let pixelReport = renderer.drawableReadbackReport else {
                throw HUDSourceError.invalid("Source GPU fixture has no raw pixel report for \(name)")
            }
            let pixelReportFile = name + "-raw-pixel-report.json"
            try abiEncoder.encode(pixelReport).write(to: output.appendingPathComponent(pixelReportFile))
            if name == "stable" {
                guard let rawPixels = renderer.drawableReadbackBGRA else {
                    throw HUDSourceError.invalid("Source GPU fixture has no stable raw pixel buffer")
                }
                try rawPixels.write(to: output.appendingPathComponent("stable-raw.bgra"))
            }
            guard renderer.diagnostics.isEmpty else {
                throw HUDSourceError.invalid("Source GPU fixture skipped content in \(name): \(renderer.diagnostics.joined(separator: "; "))")
            }
            let file = name + ".png"
            guard let destination = CGImageDestinationCreateWithURL(output.appendingPathComponent(file) as CFURL, UTType.png.identifier as CFString, 1, nil) else {
                throw HUDSourceError.invalid("Cannot create source preview PNG")
            }
            CGImageDestinationAddImage(destination, image, nil)
            guard CGImageDestinationFinalize(destination) else { throw HUDSourceError.invalid("Cannot save source preview") }
            let geometry: [[String: Any]] = document.buttons.compactMap { button in
                guard let node = frame.resolved[button.nodeID], let rect = node.rect else { return nil }
                let world = simd_mul(view.worldRoot, node.worldMatrix)
                let corners = rect.corners.compactMap { view.camera.project($0, world: world, viewport: viewport)?.point }
                return ["id": button.nodeID.rawValue, "label": button.label?.literal ?? "", "active": node.activeInHierarchy,
                    "corners": corners.map { [Double($0.x), Double($0.y)] }]
            }
            let diagnostics = frame.diagnostics + renderer.diagnostics
            manifest.append(["file": file, "rawPixelReport": pixelReportFile, "batches": frame.batches.count, "hits": frame.hits.count,
                "canvasSize": [view.layout.canvasSize.x, view.layout.canvasSize.y], "worldScale": view.layout.scale,
                "standardVerticalFOV": camera.verticalFieldOfViewDegrees,
                "runtimeVerticalFOV": view.layout.runtimeVerticalFieldOfViewDegrees,
                "buttonGeometry": geometry, "diagnostics": diagnostics,
                "unverifiedLayout": frame.layoutReport.unverifiedCustomComponents.sorted(),
                "openingElapsed": timestamp(opening), "ambientClipTime": timestamp(ambient),
                "domainCurrentLevel": domainState.currentLevelID.map { $0 as Any } ?? NSNull(),
                "domainName": activeBuilder.domain.domainName, "rendererWideMaterialRegression": materialRegression,
                "domainSelectionElapsed": timestamp(domainState.selectionElapsed), "domainHoverClipTimes": domainState.hoverClipTimes,
                "closingElapsed": timestamp(closing), "hoverElapsed": timestamp(hover)])
            print(name + ": " + String(frame.batches.count) + " source batches; " + String(diagnostics.count) + " diagnostics")
        }
        let report: [String: Any] = ["schemaVersion": 1, "width": 1728, "height": 1080,
            "capture": "Actual Metal drawable GPU readback", "fixtureMouse": [864, 540],
            "pngRepresentation": "Opaque black matte retaining raw encoded premultiplied RGB; original alpha preserved in raw-pixel reports",
            "stableRawPixels": "stable-raw.bgra; BGRA8_sRGB raw blit with rowBytes from stable-raw-pixel-report.json",
            "uiProjectionParameters": gpu.uiProjectionParameters.map { [Double($0.x), Double($0.y), Double($0.z), Double($0.w)] } ?? [],
            "perPassTuple": [1, 0, 0, 0],
            "viewNoTranslationProjectionColumns": (0..<4).map { column in
                (0..<4).map { row in Double(gpu.viewNoTranslationProjection[column][row]) }
            },
            "inverseViewBasis": "Default source Camera.cameraToWorldMatrix: Transform.localToWorld * Scale(1,1,-1)",
            "originalWrapperEase": "OutQuad finite; Linear loop", "recordingPixelComparisonPassed": false,
            "availability": "All 22 mapped macOS functions enabled; game account locks and notifications absent",
            "commit": ProcessInfo.processInfo.environment["GITHUB_SHA"] ?? "local", "frames": manifest]
        try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
            .write(to: output.appendingPathComponent("source-preview-manifest.json"))
        window.orderOut(nil)
    }
}
