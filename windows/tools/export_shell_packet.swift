import AppKit
import CryptoKit
import simd

/// Actual detached Mac desktop shell, retained as geometry and source programs.
/// This executable has no window, store, device provider, or desktop capture.
@main
enum ShellPacketExporter {
    typealias Reference = MacOSReferenceExporter
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
        try Reference.require(condition(), message)
    }
    static func object<T: Encodable>(_ value: T) throws -> Any {
        try JSONSerialization.jsonObject(with: HUDSourceJSON.encoder().encode(value))
    }
    static func json(_ value: HUDSourceJSONValue) -> Any {
        switch value {
        case .object(let v): return v.mapValues(json)
        case .array(let v): return v.map(json)
        case .string(let v): return v
        case .number(let v): return v.isFinite ? v as Any : (v.isNaN ? "NaN" : v > 0 ? "Infinity" : "-Infinity")
        case .bool(let v): return v
        case .null: return NSNull()
        }
    }
    static func floatBytes(_ values: [Float]) -> Data {
        var bytes = Data(capacity: values.count * 4)
        for value in values { var word = value.bitPattern.littleEndian; withUnsafeBytes(of: &word) { bytes.append(contentsOf: $0) } }
        return bytes
    }
    static func indexBytes(_ values: [UInt32]) -> Data {
        var bytes = Data(capacity: values.count * 4)
        for value in values { var word = value.littleEndian; withUnsafeBytes(of: &word) { bytes.append(contentsOf: $0) } }
        return bytes
    }

    final class Pack {
        let output: URL
        var meshes: [String: [String: Any]] = [:], materials: [String: [String: Any]] = [:]
        var textures: [String: [String: Any]] = [:], shaders: [String: [String: Any]] = [:]
        var frames: [[String: Any]] = []
        var verificationOracles: [[String: Any]] = []
        var writtenBlobs: [String: [String: Any]] = [:]
        let layers: ModuleReferenceLayerEncoder
        init(_ output: URL) throws {
            self.output = output; layers = try ModuleReferenceLayerEncoder(output: output)
        }
        func blob(_ data: Data, path: String) throws -> [String: Any] {
            try require(!path.hasPrefix("/") && !path.split(separator: "/").contains(".."), "Unsafe packet path")
            let digest = Reference.hash(data)
            if let existing = writtenBlobs[path] {
                try require(existing["sha256"] as? String == digest && existing["bytes"] as? Int == data.count,
                    "Conflicting retained packet asset: " + path)
                return existing
            }
            let target = output.appendingPathComponent(path)
            try FileManager.default.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
            try data.write(to: target, options: .atomic)
            let result: [String: Any] = ["file": path, "sha256": digest, "bytes": data.count]
            writtenBlobs[path] = result
            return result
        }
        func jsonBlob(_ value: Any, path: String) throws -> [String: Any] {
            try blob(JSONSerialization.data(withJSONObject: value, options: [.sortedKeys, .prettyPrinted, .withoutEscapingSlashes]), path: path)
        }
        func mesh(_ batch: HUDSourceMetalRenderer.Batch, renderer: HUDSourceMetalRenderer) throws -> String {
            // The packet retains original per-vertex color and batch tint
            // separately; use the original immutable buffer, not the last tint cache.
            var untinted = batch; untinted.color = SIMD4(repeating: 1)
            let g = try renderer.previewGeometry(for: untinted)
            var values: [Float] = []; values.reserveCapacity(g.positions.count * 10)
            for i in g.positions.indices {
                let p = g.positions[i], uv = g.uv[i], c = g.originalColors[i]
                values += [p.x,p.y,p.z,p.w,uv.x,uv.y,c.x,c.y,c.z,c.w]
            }
            try require(values.allSatisfy(\.isFinite), "Nonfinite shell vertex")
            try require(g.indices.allSatisfy { Int($0) < g.positions.count }, "Invalid shell index")
            let vertexData = floatBytes(values), indices = indexBytes(g.indices)
            let digest = Reference.hash(vertexData + indices + g.vertexBytes)
            let id = batch.mesh + "@" + digest
            if meshes[id] == nil {
                var vertices = try blob(vertexData, path: "mesh/" + digest + ".vertices.bin")
                vertices["stride"] = 40; vertices["fields"] = ["position": 0, "uv": 16, "color": 24]
                var index = try blob(indices, path: "mesh/" + digest + ".indices.bin"); index["format"] = "uint32-le"
                var original = try blob(g.vertexBytes, path: "mesh/" + digest + ".mac-vertices.bin")
                original["stride"] = g.vertexStride; original["offsets"] = renderer.shellPacketVertexOffsets()
                meshes[id] = ["id": id, "sourceMesh": batch.mesh, "vertexCount": g.positions.count,
                    "indexCount": g.indices.count, "vertices": vertices, "indices": index, "originalVertexBuffer": original]
            }
            return id
        }
        func texture(_ id: String, renderer: HUDSourceMetalRenderer) throws {
            guard textures[id] == nil else { return }
            let captured = try renderer.shellPacketTexture(id)
            var record = captured.descriptor, mips = record["mips"] as! [[String: Any]]
            for i in captured.levels.indices {
                let data = captured.levels[i]
                mips[i].merge(try blob(data, path: "texture/" + Reference.hash(data) + ".bin")) { _, new in new }
            }
            record["mips"] = mips; textures[id] = record
        }
        func shader(_ path: String, root: URL) throws {
            guard shaders[path] == nil else { return }
            try require(!path.hasPrefix("/") && !path.split(separator: "/").contains(".."), "Unsafe source shader path")
            var record = try blob(HUDSourceResourceData.read(root.appendingPathComponent(path)), path: "shader/" + path)
            record["sourcePath"] = path; shaders[path] = record
        }
        func material(_ id: String, renderer: HUDSourceMetalRenderer, root: URL) throws {
            guard materials[id] == nil else { return }
            let m = try renderer.shellPacketMaterial(id)
            for textureID in (m["textures"] as! [String: String]).values { try texture(textureID, renderer: renderer) }
            for pass in m["passes"] as! [[String: Any]] {
                try shader(pass["shaderDescriptorFile"] as! String, root: root)
                for stage in (pass["stages"] as! [String: [String: Any]]).values {
                    let path = stage["file"] as! String
                    try shader(path, root: root)
                    let spv = (path as NSString).deletingPathExtension + ".spv"
                    // Every selected program needs its original SPIR-V, not a
                    // hand-authored approximate Windows shader.
                    try shader(spv, root: root)
                }
            }
            materials[id] = m
        }
        func frame(_ frame: HUDSourceWatchFrameBuilder.Frame, camera: HUDSourceWatchCamera.Frame,
                   view: HUDSourceWatchView, name: String, native: Bool) throws {
            var value = try Reference.frameJSON(frame, camera: camera, bounds: view.bounds, view: view, nativeHitQueries: native)
            var batches = value["batches"] as! [[String: Any]]
            for (i, batch) in frame.batches.enumerated() {
                batches[i]["sourceMesh"] = batch.mesh
                batches[i]["mesh"] = try mesh(batch, renderer: view.renderer)
                let tint = view.renderer.shellPacketVertexColor(batch)
                batches[i]["gpuVertexColor"] = [tint.x, tint.y, tint.z, tint.w]
                try material(batch.material, renderer: view.renderer, root: view.document.root.deletingLastPathComponent())
                var uniforms: [[String: Any]] = []
                for buffer in try view.renderer.shellPacketUniforms(batch) {
                    var record = buffer.descriptor
                    record["payload"] = try blob(buffer.data, path: "uniform/" + Reference.hash(buffer.data) + ".bin")
                    uniforms.append(record)
                }
                batches[i]["uniforms"] = uniforms
                for pass in try view.renderer.previewPasses(for: batch) {
                    for id in pass.textures.values { try texture(id, renderer: view.renderer) }
                }
            }
            value["batches"] = batches; value["gpuCamera"] = try view.renderer.shellPacketCamera()
            value["nativeOverlayStateIncluded"] = native
            if native, let root = view.layer {
                var entries: [[String: Any]] = []
                for (index, layer) in (root.sublayers ?? []).enumerated() where layer !== view.renderer.layer {
                    entries.append(try layers.encode(layer, id: "desktop.native.\(index)"))
                }
                value["nativeLayers"] = ["bounds": ModuleReferenceLayerEncoder.rect(root.bounds),
                    "transform": ModuleReferenceLayerEncoder.transform(root.transform),
                    "sublayerTransform": ModuleReferenceLayerEncoder.transform(root.sublayerTransform),
                    "geometryFlipped": root.isGeometryFlipped, "children": entries]
                value["nativeNavigation"] = view.desktopNavigationForVerification.map {
                    ["target": $0.target.identifier, "title": $0.title, "sourceName": $0.sourceName,
                     "verifiedHitPoint": Reference.point(view.desktopPointForVerification(target: $0.target))] as [String: Any]
                }
                value["nativeProfileCaptions"] = view.desktopProfileCaptionsForVerification.sorted()
                if let card = view.document.desktopProfileCard {
                    value["nativeProfileBindings"] = Dictionary(uniqueKeysWithValues:
                        ["managerName", "managerNumber", "managerLevel", "managerLevelLabel", "progressTxt"].compactMap { key in
                            card.node(key).map { ("desktop.profile." + key, $0.rawValue) }
                        })
                }
            }
            var row = try jsonBlob(value, path: "frame/" + name + ".json")
            row["name"] = name; frames.append(row)
        }
        func stable(_ view: HUDSourceWatchView, name: String) throws {
            _ = try view.renderedImageForVerification() // waits only for this renderer's own draw; no pixels become a shell asset
            let raw = try view.renderer.shellPacketDrawable()
            var oracle = raw.descriptor
            oracle.merge(try blob(raw.data, path: "verification/" + name + ".raw-bgra.bin")) { _, new in new }
            oracle["name"] = name; verificationOracles.append(oracle)
            guard let frame = view.currentFrameForVerification, let camera = view.currentCameraForVerification else {
                throw HUDSourceError.invalid("Missing actual desktop draw")
            }
            try self.frame(frame, camera: camera, view: view, name: name, native: true)
        }
        func checkpoints(_ view: HUDSourceWatchView, name: String) throws {
            let camera = try view.cameraModel.frame(screenSize: SIMD2(Double(view.bounds.width), Double(view.bounds.height)))
            let entries = view.desktopNavigationForVerification
            let count = entries.indices.filter { $0 >= 4 && entries[$0].target.module?.group != .bottom }.count
            let navigation = try HUDSourceDesktopNavigationLayout(document: view.document, entryCount: count)
            let playback = HUDSourceWatchPlayback(animation: view.document.animation)
            playback.ambientMotionEnabled = false
            for phase in ["opening", "closing"] {
                let duration = phase == "opening" ? playback.animation.entrance.lastKeyTime : playback.animation.exit.lastKeyTime
                if phase == "opening" { playback.open(at: 0, reduceMotion: false) }
                else { playback.showStable(at: 0); playback.close(at: 0, reduceMotion: false) }
                for step in 0...4 {
                    let time = Double(step) * duration / 4
                    guard var pose = try playback.sample(at: time, canvasResolution: camera.layout.canvasSize, reduceMotion: false) else { continue }
                    view.applyDesktopButtons(to: &pose, at: 0, reduceMotion: true, forceRebuild: true)
                    let frame = try view.frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                        verticalNormalizedPosition: 1, desktopNavigation: navigation,
                        selectableTints: view.selectableColor.colors(at: 0, reduceMotion: true), forceRebuild: true)
                    try view.renderer.shellPacketSubmit(frame.batches, time: 0)
                    try self.frame(frame, camera: camera, view: view, name: name + "-\(phase)-\(step)", native: false)
                }
            }
        }
    }

    static func run() throws {
        var args = Array(CommandLine.arguments.dropFirst()), output: URL?, sizes: [CGSize] = []
        while !args.isEmpty {
            let option = args.removeFirst()
            if option == "--ui-test" || option == "--export-shell-packet" { continue }
            try require(!args.isEmpty, "Missing argument for " + option)
            let value = args.removeFirst()
            if option == "--output" { output = URL(fileURLWithPath: value, isDirectory: true) }
            else if option == "--size" {
                let pair = value.split(separator: "x").compactMap { Int($0) }
                try require(pair.count == 2 && pair.allSatisfy { (320...4096).contains($0) }, "Invalid viewport")
                sizes.append(CGSize(width: pair[0], height: pair[1]))
            } else { throw HUDSourceError.invalid("Unknown shell argument: " + option) }
        }
        guard let output else { throw HUDSourceError.invalid("Missing --output") }
        try require(CommandLine.arguments.contains("--ui-test"), "Isolated --ui-test mode required")
        if sizes.isEmpty { sizes = [CGSize(width: 1280, height: 800), CGSize(width: 1920, height: 1080)] }
        NSApplication.shared.setActivationPolicy(.prohibited); NSApp.appearance = NSAppearance(named: .darkAqua)
        L10n.language = .english
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.ambientAnimation = false
        configuration.reduceMotion = true; configuration.blurAmount = 0; configuration.hudScale = 1
        configuration.hudOffsetX = 0; configuration.hudOffsetY = 0; HUDRuntimeAppearance.configuration = configuration
        var profile = UserProfile(awakeningDate: Date(timeIntervalSince1970: 1_700_000_000), uid: "1000000000")
        profile.name = "Endministrator"; profile.tag = "0000"; profile.permissionLevel = 60
        profile.birthdayMonth = 1; profile.birthdayDay = 1
        let pack = try Pack(output)
        var animation: [String: Any]?
        for size in sizes {
            try autoreleasepool {
                let view = try HUDSourceWatchView(frame: CGRect(origin: .zero, size: size), desktopMode: true,
                    desktopNavigationEntries: HUDDesktopWatchNavigation.entries(shortcuts: []))
                defer { view.conceal() }
                try require(view.document.widgets == nil, "Raw game widgets cannot be the Windows desktop source")
                view.pointerLocationProvider = { CGPoint(x: size.width / 2, y: size.height / 2) }; view.isDesktopPointerLocked = { true }
                view.setDesktopProfile(profile, avatar: nil, background: nil); view.selectedDesktopModule = .power; view.inputEnabled = true
                view.showStable(); view.layoutSubtreeIfNeeded(); view.layout(); CATransaction.flush()
                let name = "desktop-shell-\(Int(size.width))x\(Int(size.height))"
                try pack.stable(view, name: name + "-top")
                // Hover is registered by the actual profile adapter but has
                // zero alpha at rest. Preserve it for subsequent live hover.
                for id in ["desktop.profile.avatar", "desktop.profile.background", "desktop.profile.hover"] {
                    try pack.texture(id, renderer: view.renderer)
                }
                try pack.checkpoints(view, name: name)
                view.refreshPointerForVerification()
                var steps = 0
                while view.scrollDesktopNavigation(1, animated: false) { steps += 1; try require(steps <= 128, "Unbounded shell scroll") }
                try pack.stable(view, name: name + "-bottom")
                if animation == nil {
                    let data: [String: Any] = ["library": try object(view.document.library), "scene": try object(view.document.scene),
                        "runtimeRoot": json(view.document.runtimeRoot), "controllerTransitions": json(view.document.controllerTransitions),
                        "playback": ["finiteEase": "OutQuad", "openingDuration": view.document.animation.entrance.lastKeyTime,
                            "closingDuration": view.document.animation.exit.lastKeyTime, "ambientDuration": view.document.animation.ambient.lastKeyTime,
                            "ambientCheckpointsEnabled": false] as [String: Any],
                        "limitations": ["Native overlay transition curves are not inferred from stable snapshots",
                            "Randomized desktop ambient motion remains an explicit Mac runtime adapter", "Central module canvases are separate packets"]]
                    animation = try pack.jsonBlob(data, path: "animation.json")
                }
                view.conceal()
                try require(view.window == nil && !view.hasDisplayTimerForVerification && !view.backdropPreparingForVerification
                    && view.sourceCursorSetCountForVerification == 0, "Shell exporter escaped detached fixture boundaries")
            }
        }
        let modules: [[String: Any]] = HUDModule.allCases.map {
            ["id": $0.rawValue, "title": $0.title, "group": $0.group.rawValue,
             "contentFrameIn1000x640DesignSpace": ModuleReferenceLayerEncoder.rect($0.contentFrame)]
        }
        let rasters: [[String: Any]] = try pack.layers.rasterAssets.map { asset in
            let path = asset["path"] as! String
            var result = asset
            result.merge(try pack.blob(Data(contentsOf: output.appendingPathComponent(path)), path: path)) { _, new in new }
            return result
        }
        // Raw target comparisons are validation evidence only. They are not
        // referenced by the shipping shell-packet asset graph.
        try Reference.writeJSON(["scope": "synthetic Mac renderer validation only; exclude from shipping assets",
            "frames": pack.verificationOracles], to: output.appendingPathComponent("verification-oracles.json"))
        try Reference.writeJSON(["schemaVersion": 1, "desktopMode": true,
            "scope": "actual Mac desktop shell draw packet; source geometry, texture mips, shaders, and separate native layers",
            "coordinates": ["matrices": "column-major arrays; column vectors", "canonicalVertices": "float32 little-endian position4 UV2 originalColor4; stride40",
                "indices": "uint32 little-endian", "uv": "unaltered source UVs; do not flip texture rows again",
                "states": "material defaults use numeric Metal enums; batch stencil overrides use original Unity enums; batch colorWriteMask uses Unity RGBA bits; explicit mapping required", "color": "original straight/linear vertex colors; source shaders own texture alpha",
                "camera": "logical CPU camera plus exact submitted gpuCamera; converted source shaders retain Vulkan Y negation"],
            "fixture": ["language": "english", "theme": "dark", "profileName": profile.name, "profileUID": profile.uid,
                "selectedModule": "power", "savedApps": 0, "ambientEnabled": false, "desktopBackdropCapture": false] as [String: Any],
            "isolation": ["windowCreated": false, "persistentStoresCreated": false, "userDefaultsAccessed": false,
                "clipboardAccessed": false, "nativeCursorSetCount": 0, "displayTimersAfterCleanup": 0],
            "meshes": pack.meshes.keys.sorted().map { pack.meshes[$0]! }, "textures": pack.textures.keys.sorted().map { pack.textures[$0]! },
            "materials": pack.materials.keys.sorted().map { pack.materials[$0]! }, "shaderAssets": pack.shaders.keys.sorted().map { pack.shaders[$0]! },
            "frames": pack.frames, "animation": animation!, "modules": modules, "nativeRasterAssets": rasters,
            "nativeLayerUnsupported": pack.layers.unsupported,
            "notVerified": ["Windows renderer output", "Cross-OS font metrics/raster equality", "Native overlay transition samples",
                "SystemHUDView central module canvases", "Live providers", "Captured backdrop/blur", "Randomized desktop ambient adapter"],
            "unresolvedTextures": []], to: output.appendingPathComponent("shell-packet.json"))
        print("Exported actual desktop shell: \(pack.frames.count) frames, \(pack.meshes.count) meshes, \(pack.textures.count) textures, \(pack.materials.count) materials")
    }
    static func main() {
        do { try run() } catch { fputs("Shell packet export failed: \(error)\n", stderr); exit(1) }
    }
}
