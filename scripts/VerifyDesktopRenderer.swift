import AppKit
import MetalKit
import simd

/// Uses only shipped source assets and its own window, never desktop capture.
@main
struct VerifyDesktopRenderer {
    struct Sample {
        var name: String
        var opening: Double? = nil
        var ambient: Double = 0
        var closing: Double? = nil
        var state: HUDSourceWatchButtonAnimation.State = .normal
        var elapsed: Double = 0
        var mouse = SIMD2<Double>(0.5, 0.5)
        var scroll: Double = 1
        var accent: NSColor? = nil
        var stencil: HUDSourceMetalRenderer.StencilState? = nil
        var colorMask: UInt8? = nil
        var textureCase: String? = nil
        var uniformCase: String? = nil
        var profileColorCase: String? = nil
    }
    static func main() throws {
        guard CommandLine.arguments.count >= 3 else { throw HUDSourceError.invalid("Expected resource root and output") }
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        let output = URL(fileURLWithPath: CommandLine.arguments[2])
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        let app = NSApplication.shared; app.setActivationPolicy(.accessory)
        let size = SIMD2<Double>(1728, 1080)
        let viewport = CGRect(x: 0, y: 0, width: size.x, height: size.y)
        let document = try HUDSourceWatchDocument(resourceRoot: root.appendingPathComponent("Scene"), includeWidgets: false,
            includeSourceText: false, includeDesktopProfile: true)
        let renderer = try HUDSourceMetalRenderer(frame: viewport, resourceRoot: root)
        // The desktop default may evolve; this executable's baseline must
        // always exercise original draws, with only the merge variant opting in.
        renderer.setAdjacentBatchMergingEnabledForVerification(false)
        #if HUD_SOURCE_PREPARED_UNIFORM_VERIFY
        renderer.verifyPreparedUniformBytesForVerification = true
        #endif
        #if HUD_SOURCE_ADJACENT_MERGE_VERIFY
        renderer.setAdjacentBatchMergingEnabledForVerification(true)
        renderer.verifyMergedGeometryBytesForVerification = true
        renderer.verifyPreparedUniformBytesForVerification = true
        #endif
        let initialStatistics = renderer.resourceStatisticsForVerification
        let textures = try JSONSerialization.jsonObject(with: HUDSourceResourceData.read(root.appendingPathComponent("textures.json"))) as! [[String: Any]]
        let selection = try JSONSerialization.jsonObject(with: HUDSourceResourceData.read(root.appendingPathComponent("runtime-selection.json"))) as! [String: Any]
        var textureAliases: [String: String] = [:]
        for index in selection["texture_indices"] as! [Int] {
            let texture = textures[index], id = texture["path_id"] as! String
            guard renderer.containsTexture(named: id) else { throw HUDSourceError.invalid("Missing available texture descriptor") }
            if let cab = texture["cab"] as? String {
                guard renderer.containsTexture(named: cab + ":" + id) else { throw HUDSourceError.invalid("Missing available CAB texture alias") }
                textureAliases[id] = cab + ":" + id
                textureAliases[cab + ":" + id] = id
            }
        }
        let inventoryStatistics = renderer.resourceStatisticsForVerification
        guard initialStatistics == inventoryStatistics else { throw HUDSourceError.invalid("Texture inventory query allocated resources") }
        guard let device = renderer.device,
              let dynamic = device.makeTexture(descriptor: .texture2DDescriptor(pixelFormat: .rgba8Unorm, width: 2, height: 2, mipmapped: false)) else {
            throw HUDSourceError.invalid("Cannot create dynamic texture fixture")
        }
        let texels: [UInt8] = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255]
        texels.withUnsafeBytes { dynamic.replace(region: MTLRegionMake2D(0, 0, 2, 2), mipmapLevel: 0, withBytes: $0.baseAddress!, bytesPerRow: 8) }
        try renderer.registerTexture(named: "__dynamic_fixture", texture: dynamic, filterMode: 0, wrapU: 1, wrapV: 1)
        let builder = try HUDSourceWatchFrameBuilder(document: document, renderer: renderer, includeDomain: false, includeSourceText: false)
        guard let profile = document.desktopProfileCard, profile.scene.nodes.count == 45 else {
            throw HUDSourceError.invalid("Expected the exact selected desktop profile card")
        }
        let profileNodeIDs = Set(profile.scene.nodes.map { $0.id.rawValue })
        builder.desktopHiddenNodes = document.desktopHiddenNodeIDs
        builder.desktopTextOverrides = Dictionary(uniqueKeysWithValues: document.scene.nodes.compactMap { node in
            document.component("UIText", on: node.id) == nil ? nil : (node.id, "")
        })
        for button in document.buttons {
            if let icon = document.scene.nodes.first(where: { $0.path.hasPrefix(button.path + "/") && ["Icon", "Icon01"].contains($0.name) }) {
                builder.desktopHiddenNodes.insert(icon.id)
            }
        }
        for name in ["TechtreeBtn", "ReportBtn"] {
            guard let node = document.scene.nodes.first(where: { $0.name == name }) else { continue }
            for child in document.scene.nodes where child.path.hasPrefix(node.path + "/") && ["IconShadow", "ForbidIcon", "LockIcon"].contains(child.name) {
                builder.desktopHiddenNodes.insert(child.id)
            }
        }
        let navigation = try HUDSourceDesktopNavigationLayout(document: document, entryCount: 40)
        let runtime = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: HUDSourceResourceData.read(document.root.appendingPathComponent("runtime-root-camera.json")))
        let model = try HUDSourceWatchCamera(runtimeRoot: runtime)
        let buttons = try HUDSourceWatchButtonAnimation(document: document)
        guard let button = document.buttons.first else { throw HUDSourceError.invalid("Missing desktop button") }
        let window = NSWindow(contentRect: viewport, styleMask: [.borderless], backing: .buffered, defer: false)
        window.isOpaque = false; window.backgroundColor = .clear; window.hasShadow = false
        window.contentView = renderer; window.orderFront(nil)
        renderer.drawableSize = CGSize(width: size.x, height: size.y)
        app.finishLaunching(); window.displayIfNeeded()
        var samples: [Sample] = []
        for t in [0.0, 0.1, 0.25, 0.5, 0.75, 1.0] { samples.append(Sample(name: "opening-\(t)", opening: t)) }
        for t in [0.0, 0.125, 0.5, 1.25, 5, document.animation.ambient.lastKeyTime / 2] {
            samples.append(Sample(name: "ambient-\(t)", ambient: t))
        }
        for state in [HUDSourceWatchButtonAnimation.State.highlighted, .pressed, .disabled] {
            for t in [0.0, 1 / 30.0, 0.1, 0.5] { samples.append(Sample(name: "\(state.rawValue)-\(t)", state: state, elapsed: t)) }
        }
        for t in [0.0, 0.1, 0.25, 0.5] { samples.append(Sample(name: "closing-\(t)", closing: t)) }
        for (i, mouse) in [SIMD2<Double>(0, 0), SIMD2<Double>(0, 1), SIMD2<Double>(1, 0), SIMD2<Double>(1, 1)].enumerated() {
            samples.append(Sample(name: "gyro-\(i)", ambient: 0.5, mouse: mouse))
        }
        for (i, accent) in [NSColor.systemBlue, .systemPink, .systemGreen].enumerated() {
            samples.append(Sample(name: "accent-\(i)", ambient: 0.5, accent: accent))
        }
        for t in [0.0, 0.25, 0.5] { samples.append(Sample(name: "scroll-\(t)", ambient: 0.5, scroll: t)) }
        // Exercise source stencil equality/failure/write masks and per-batch
        // color mask pipelines, not merely the default desktop batch stream.
        samples.append(Sample(name: "stencil-equal", stencil: .init(reference: 32, compare: 3, pass: 2, fail: 0, readMask: 255, writeMask: 255)))
        samples.append(Sample(name: "stencil-not-equal", stencil: .init(reference: 16, compare: 6, pass: 2, fail: 1, readMask: 15, writeMask: 15)))
        samples.append(Sample(name: "stencil-never", stencil: .init(reference: 1, compare: 1, pass: 2, fail: 2, readMask: 255, writeMask: 255)))
        samples.append(Sample(name: "stencil-color-mask", stencil: .init(reference: 32, compare: 8, pass: 2, fail: 0), colorMask: 5))
        samples.append(Sample(name: "texture-path-aliases", textureCase: "aliases"))
        samples.append(Sample(name: "texture-dynamic-registration", textureCase: "dynamic"))
        for test in ["partial", "empty", "restore", "world-instances", "material-switch", "unused-property", "steady-repeat"] {
            samples.append(Sample(name: "uniform-" + test, ambient: 0.75, uniformCase: test))
        }
        for test in ["grow", "move", "tint", "shrink", "grow-again"] {
            samples.append(Sample(name: "merge-geometry-" + test, ambient: 0.75, uniformCase: test))
        }
        for test in ["vertex-yellow", "material-yellow", "vertex-green"] {
            samples.append(Sample(name: "profile-" + test + "-neutral", ambient: 0.75, profileColorCase: test))
            samples.append(Sample(name: "profile-" + test + "-blue", ambient: 0.75, accent: .blue, profileColorCase: test))
        }
        samples.append(Sample(name: "profile-global-policy-control", ambient: 0.75, accent: .blue, profileColorCase: "global-control"))
        samples.append(Sample(name: "profile-mixed-policy", ambient: 0.75, accent: .blue, profileColorCase: "mixed"))
        samples.append(Sample(name: "profile-policy-restored", ambient: 0.75, accent: .blue, profileColorCase: "vertex-yellow"))
        // This last case must restore the original combined attachment before
        // drawing, proving the unsupported batch-state fallback as well.
        samples.append(Sample(name: "stencil-depth-failure-fallback", stencil: .init(reference: 16, compare: 8, pass: 2, fail: 0, depthFail: 2)))
        var reports: [[String: Any]] = []
        var textureIDs = Set<String>()
        var profilePixels: [String: Data] = [:]
        var profileBatchCount = 0
        for sample in samples {
            let euler = try model.gyro.targetEuler(mouseUnity: sample.mouse * size, screenSize: size)
            let rotation = try HUDSourceWatchCamera.quaternion(eulerDegrees: euler)
            let view = try model.frame(screenSize: size, localRotation: rotation)
            buttons.reset(at: 0)
            if sample.state != .normal { buttons.setState(sample.state, on: button.nodeID, at: 0) }
            var pose = try document.animation.pose(entranceTime: sample.opening ?? document.animation.entrance.lastKeyTime,
                ambientTime: sample.ambient, exitTime: sample.closing, canvasResolution: view.layout.canvasSize)
            document.applyMacButtonAvailability(to: &pose)
            buttons.apply(to: &pose, at: sample.elapsed, reduceMotion: false)
            if let card = document.desktopProfileCard, let test = sample.uniformCase {
                if ["grow", "grow-again"].contains(test), let avatar = card.node("playerHead") {
                    builder.desktopImages[avatar] = .init(texture: "__dynamic_fixture", size: SIMD2(2, 2))
                } else if test == "move", let background = card.scene.nodes.first(where: { $0.name == "BgImage" }) {
                    builder.desktopImages[background.id] = .init(texture: "__dynamic_fixture", size: SIMD2(2, 2))
                } else if test == "tint", let slider = card.node("levelSlider") {
                    builder.desktopProperties[slider] = ["m_FillAmount": 0.23, "m_Color.r": 0.2, "m_Color.g": 0.6, "m_Color.b": 0.9]
                } else if test == "shrink" {
                    builder.desktopImages = [:]; builder.desktopProperties = [:]
                }
            }
            let frame = try builder.build(pose: pose, worldRoot: view.worldRoot,
                verticalNormalizedPosition: sample.scroll, desktopNavigation: navigation)
            var batches = frame.batches
            for batch in batches {
                let isProfile = batch.sourceNodeID.map(profileNodeIDs.contains) ?? false
                guard batch.appliesDesktopAccent != isProfile else {
                    throw HUDSourceError.invalid("Profile/global accent scope is incorrect: \(batch.mesh)")
                }
            }
            profileBatchCount = max(profileBatchCount, batches.filter { $0.sourceNodeID.map(profileNodeIDs.contains) ?? false }.count)
            if let state = sample.stencil {
                for i in batches.indices { batches[i].stencilOverrides = state; batches[i].colorWriteMask = sample.colorMask }
            }
            if let test = sample.textureCase {
                for i in batches.indices {
                    if test == "dynamic" { batches[i].textureOverrides["_MainTex"] = "__dynamic_fixture" }
                    else {
                        for pass in try renderer.previewPasses(for: batches[i]) {
                            for (slot, id) in pass.textures {
                                if let alias = textureAliases[id] {
                                    batches[i].textureOverrides[slot] = alias
                                }
                            }
                        }
                    }
                }
            }
            if let test = sample.uniformCase {
                for i in batches.indices {
                    if test == "partial" || test == "empty" {
                        // Partial/empty arrays replace the complete source
                        // field, even for matrices and time. They must not
                        // inherit bytes from a previous prepared payload.
                        let value: [Float] = test == "empty" ? [] : [0.25, 0.5]
                        batches[i].uniformOverrides["_Color"] = value
                        batches[i].uniformOverrides["_UITime"] = value
                        batches[i].uniformOverrides["unity_ObjectToWorld"] = value
                    } else if test == "unused-property" {
                        batches[i].uniformOverrides["__not_a_shader_field"] = [Float(i)]
                    }
                }
                if test == "world-instances", let first = batches.first {
                    var second = first
                    second.world.columns.3.x += 0.15
                    second.uniformOverrides["_Color"] = [0.2, 0.4, 0.6, 0.8]
                    batches.insert(second, at: 1)
                }
                if test == "material-switch", batches.count > 1 {
                    let material = batches[0].material
                    batches[0].material = batches[1].material
                    batches[1].material = material
                }
                if ["grow", "move", "tint", "shrink", "grow-again"].contains(test),
                   var template = batches.first(where: { $0.mesh.hasPrefix("ui/") && $0.material.hasPrefix("CAB-311c0d8d") }) {
                    // Public registration mutates source geometry between
                    // cached frames. Sizes grow/shrink and colors diverge.
                    let count = test == "shrink" ? 2 : 5
                    for n in 0..<count {
                        let name = "ui/__merge_verification/" + String(n)
                        let x = Float(n) * 5 + (test == "move" ? 1.25 : 0)
                        let positions: [SIMD4<Float>] = [SIMD4(x, 0, 0, 1), SIMD4(x + 3, 0, 0, 1),
                            SIMD4(x + 3, 3, 0, 1), SIMD4(x, 3, 0, 1)]
                        try renderer.registerGeometry(named: name, positions: positions,
                            uv: [SIMD2(0, 0), SIMD2(1, 0), SIMD2(1, 1), SIMD2(0, 1)], indices: [0, 1, 2, 0, 2, 3])
                        template.mesh = name
                        template.color = test == "tint" ? SIMD4(0.2, Float(n + 1) / 6, 0.8, 0.75) : SIMD4(repeating: 1)
                        batches.append(template)
                    }
                }
            }
            if let test = sample.profileColorCase {
                batches = batches.filter { $0.sourceNodeID.map(profileNodeIDs.contains) ?? false }
                guard !batches.isEmpty else { throw HUDSourceError.invalid("Profile fixture has no drawable card batches") }
                let local = test == "vertex-green" ? SIMD4<Float>(0, 1, 0, 1) : SIMD4<Float>(1, 1, 0, 1)
                for i in batches.indices {
                    batches[i].color = test == "material-yellow" ? SIMD4(repeating: 1) : local
                    batches[i].uniformOverrides["_Color"] = test == "material-yellow" ? [1, 1, 0, 1] : [1, 1, 1, 1]
                    if test == "global-control" { batches[i].appliesDesktopAccent = true }
                }
                if test == "mixed", let first = batches.first(where: { $0.mesh.hasPrefix("ui/") }) {
                    var global = first
                    global.appliesDesktopAccent = true
                    global.uniformOverrides["_Color"] = [1, 1, 0, 1]
                    var local = global; local.appliesDesktopAccent = false
                    // Equal mesh/world/material/overrides but distinct policy:
                    // neither prepared cells nor merge groups may alias it.
                    batches.insert(contentsOf: [global, local, global, local], at: 0)
                }
            }
            for batch in batches {
                for pass in try renderer.previewPasses(for: batch) { textureIDs.formUnion(pass.textures.values) }
            }
            var projection = HUDSourceGeometry.floatMatrix(view.camera.projection)
            var vp = HUDSourceGeometry.floatMatrix(view.camera.viewProjection)
            for c in 0..<4 { projection[c].y = -projection[c].y; vp[c].y = -vp[c].y }
            let p = model.cameraWorld.columns.3
            let camera = HUDSourceMetalRenderer.Camera(viewProjection: vp,
                viewNoTranslationProjection: try HUDSourceWatchCamera.viewNoTranslationProjection(gpuProjection: projection, view: view.camera.view),
                worldSpacePosition: SIMD3(Float(p.x), Float(p.y), Float(p.z)), timeSeconds: Float(sample.ambient), renderPathInjected: 1, flipX: 0, flipY: 0,
                projection: projection, inverseView: HUDSourceGeometry.floatMatrix(model.shaderCameraToWorld),
                uiProjectionParameters: try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: projection, near: Float(model.near), far: Float(model.far)))
            renderer.configureDesktopAccent(sample.accent)
            #if HUD_SOURCE_INDEX_TOPOLOGY_VERIFY
            let structureToken = HUDSourceMetalRenderer.BatchStructureToken()
            renderer.submit(camera: camera, batches: batches, structureToken: structureToken); renderer.draw()
            #else
            renderer.submit(camera: camera, batches: batches); renderer.draw()
            #endif
            _ = try renderer.copyDrawableImage()
            guard let pixels = renderer.drawableReadbackBGRA, let pixelReport = renderer.drawableReadbackReport,
                  renderer.diagnostics.isEmpty else { throw HUDSourceError.invalid("Desktop draw failed: \(sample.name) \(renderer.diagnostics)") }
            #if HUD_SOURCE_INDEX_TOPOLOGY_VERIFY
            renderer.submit(camera: camera, batches: batches, structureToken: structureToken); renderer.draw()
            _ = try renderer.copyDrawableImage()
            guard renderer.diagnostics.isEmpty, renderer.drawableReadbackBGRA == pixels else {
                throw HUDSourceError.invalid("Retained structure-token draw changed pixels: \(sample.name)")
            }
            #endif
            if sample.uniformCase != nil || sample.profileColorCase != nil {
                // Exercise unchanged cells as well as the changed-input draw.
                renderer.submit(camera: camera, batches: batches); renderer.draw()
                _ = try renderer.copyDrawableImage()
                guard renderer.diagnostics.isEmpty else { throw HUDSourceError.invalid("Repeated uniform draw failed: \(renderer.diagnostics)") }
            }
            // Ignore only unused row padding; every actual RGBA pixel compares.
            var packed = Data()
            for y in 0..<pixelReport.height {
                packed.append(pixels[(y * pixelReport.rowBytes)..<(y * pixelReport.rowBytes + pixelReport.width * 4)])
            }
            try packed.write(to: output.appendingPathComponent(sample.name + ".bgra"))
            if let test = sample.profileColorCase {
                profilePixels[sample.name] = packed
                if test != "global-control" && test != "mixed" {
                    let targetCount = packed.withUnsafeBytes { bytes -> Int in
                        let b = bytes.bindMemory(to: UInt8.self)
                        return stride(from: 0, to: b.count, by: 4).reduce(0) { count, i in
                            let target = test == "vertex-green"
                                ? b[i + 1] > 100 && b[i + 2] < 40 && b[i] < 40
                                : b[i + 2] > 100 && b[i + 1] > 100 && b[i] < 40
                            return count + (target && b[i + 3] > 100 ? 1 : 0)
                        }
                    }
                    guard targetCount > 100 else {
                        throw HUDSourceError.invalid("Independent local profile color oracle failed: \(sample.name), target pixels=\(targetCount)")
                    }
                }
            }
            reports.append(["name": sample.name, "batches": batches.count, "depthFormat": renderer.depthStencilPixelFormat.rawValue,
                "attachmentBytes": renderer.depthStencilTexture?.allocatedSize ?? 0,
                "batchStates": batches.map { ["mesh": $0.mesh, "material": $0.material,
                    "appliesDesktopAccent": String($0.appliesDesktopAccent),
                    "stencil": $0.stencilOverrides.map { String(describing: $0) } ?? "source", "colorMask": String($0.colorWriteMask ?? 15)] },
                "frameDiagnostics": frame.diagnostics])
            print("Rendered", sample.name, batches.count, renderer.depthStencilPixelFormat.rawValue)
        }
        for test in ["vertex-yellow", "material-yellow", "vertex-green"] {
            guard profilePixels["profile-" + test + "-neutral"] == profilePixels["profile-" + test + "-blue"] else {
                throw HUDSourceError.invalid("Global blue changed independent profile color: " + test)
            }
        }
        guard profilePixels["profile-vertex-yellow-blue"] == profilePixels["profile-policy-restored"],
              profilePixels["profile-vertex-yellow-blue"] != profilePixels["profile-global-policy-control"],
              profileBatchCount > 0 else {
            throw HUDSourceError.invalid("Profile policy cache invalidation/control proof failed")
        }
        print("Verified profile policy scope for", profileBatchCount, "drawable card batches from", profileNodeIDs.count,
            "selected nodes; independent yellow/green pixels and global blue control")
        #if HUD_SOURCE_INDEX_TOPOLOGY_VERIFY
        try renderer.verifyImmutableIndexReuseForVerification()
        try renderer.verifyAdjacentPlanForVerification()
        try renderer.verifyBatchStructureContractForVerification()
        print("Verified unchanged index sharing and changed in-flight topology isolation")
        print("Verified adjacent merge dependencies, retained geometry, world motion and vertex tint updates")
        print("Verified producer token replacement, backdrop dependencies, external geometry and accent invalidation")
        #endif
        #if HUD_SOURCE_PREPARED_UNIFORM_VERIFY
        try renderer.verifyPreparedUniformFieldOrderForVerification()
        guard renderer.verifiedPreparedUniformByteCount > 10_000,
              renderer.preparedUniformHitCount > 0 else {
            throw HUDSourceError.invalid("Prepared uniform byte verification did not exercise real draws")
        }
        print("Verified", renderer.verifiedPreparedUniformByteCount, "exact uniform payloads")
        #endif
        #if HUD_SOURCE_ADJACENT_MERGE_VERIFY
        guard renderer.verifiedMergedGeometryByteCount > 10_000,
              renderer.mergedBatchCount > 0, renderer.mergedGeometryReuseCount > 0 else {
            throw HUDSourceError.invalid("Adjacent merge verification did not exercise geometry/reuse")
        }
        print("Verified", renderer.verifiedMergedGeometryByteCount, "merged geometry bytes")
        #endif
        try JSONSerialization.data(withJSONObject: ["frames": reports, "usedTextureIDs": textureIDs.sorted(),
            "initialStatistics": initialStatistics, "inventoryStatistics": inventoryStatistics,
            "statistics": renderer.resourceStatisticsForVerification], options: [.prettyPrinted, .sortedKeys])
            .write(to: output.appendingPathComponent("report.json"))
        window.orderOut(nil)
    }
}
