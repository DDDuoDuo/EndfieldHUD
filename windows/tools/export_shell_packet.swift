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
    static func layoutDocument(_ document: HUDSourceWatchDocument) -> [String: Any] {
        // Export the mounted desktop document. The raw game scene omits the
        // desktop profile and is not a substitute for these runtime bindings.
        let components = Dictionary(uniqueKeysWithValues: document.components.map { id, records in
            (id.rawValue, records.map { record -> [String: Any] in
                ["id": record.id.rawValue, "type": record.type, "script": record.script as Any? ?? NSNull(),
                 "data": record.data.mapValues(json)]
            })
        })
        let buttons: [[String: Any]] = document.buttons.map { button in
            ["node_id": button.nodeID.rawValue, "path": button.path,
             "labels": button.labels.map { label -> [String: Any] in
                ["node_id": label.nodeID.rawValue, "text_id": label.textID, "cn_literal": label.literal as Any? ?? NSNull()]
             }]
        }
        let animators: [[String: Any]] = document.animators.map { animator in
            ["root_node_id": animator.rootID.rawValue, "controller_name": animator.controllerName,
             "states": animator.states.map { ["name": $0.name, "bound_clip_id": $0.clipID.rawValue] }]
        }
        return ["components": components,
                "spriteByComponent": Dictionary(uniqueKeysWithValues: document.spriteByComponent.map { ($0.key.rawValue, json($0.value)) }),
                "buttons": buttons, "animators": animators]
    }
    static func poseJSON(_ pose: HUDSourceWatchPose) -> [String: Any] {
        func v2(_ v: HUDSourceVector2?) -> Any { v.map { [$0.x, $0.y] } ?? NSNull() as Any }
        func v3(_ v: HUDSourceVector3?) -> Any { v.map { [$0.x, $0.y, $0.z] } ?? NSNull() as Any }
        let transforms = Dictionary(uniqueKeysWithValues: pose.transforms.map { id, t in
            (id.rawValue, ["position": v3(t.localPosition), "scale": v3(t.localScale),
                "rotation": t.localRotation.map { [$0.x, $0.y, $0.z, $0.w] } ?? NSNull() as Any,
                "anchored": v3(t.anchoredPosition3D), "anchorMin": v2(t.anchorMin), "anchorMax": v2(t.anchorMax),
                "sizeDelta": v2(t.sizeDelta), "pivot": v2(t.pivot), "active": t.active.map { $0 as Any } ?? NSNull(),
                "components": Dictionary(uniqueKeysWithValues: t.positionComponents.map { (String($0.key), $0.value) })] as [String: Any])
        })
        return ["transforms": transforms, "properties": Dictionary(uniqueKeysWithValues: pose.properties.map { ($0.key.rawValue, $0.value) }),
                "unbound": pose.unboundPaths.sorted(), "unregistered": pose.unregisteredBindings.sorted()]
    }
    static func tintsJSON(_ values: [HUDSourceID: SIMD4<Float>]) -> [String: [Float]] {
        Dictionary(uniqueKeysWithValues: values.map { ($0.key.rawValue, [$0.value.x, $0.value.y, $0.value.z, $0.value.w]) })
    }
    static func desktopSettings(_ builder: HUDSourceWatchFrameBuilder) -> [String: Any] {
        ["hiddenNodes": builder.desktopHiddenNodes.map(\.rawValue).sorted(),
         "properties": Dictionary(uniqueKeysWithValues: builder.desktopProperties.map { ($0.key.rawValue, $0.value) }),
         "sprites": Dictionary(uniqueKeysWithValues: builder.desktopSprites.map { ($0.key.rawValue, $0.value) }),
         "images": Dictionary(uniqueKeysWithValues: builder.desktopImages.map { id, image in
            (id.rawValue, ["texture": image.texture, "size": [image.size.x, image.size.y],
                "displaySize": image.displaySize.map { [$0.x, $0.y] } ?? NSNull() as Any] as [String: Any]) }),
         "normalMaterialNodes": builder.desktopNormalMaterialNodes.map(\.rawValue).sorted(),
         "graphicStyles": Dictionary(uniqueKeysWithValues: builder.desktopGraphicStyles.map { id, style in
            (id.rawValue, ["tint": style.tint.map { [$0.x, $0.y, $0.z] } ?? NSNull() as Any, "opacity": style.opacity] as [String: Any]) })]
    }
    static func desktopTextureDependencies(_ view: HUDSourceWatchView) throws -> [String] {
        let metadata = try view.document.renderMetadata(), builder = view.frameBuilder
        var ids: Set<String> = ["__white"]
        // Permanent desktop exclusions override every animation. Authored
        // inactivity and zero alpha do not: hover/press clips can reveal them.
        func excluded(_ id: HUDSourceID) -> Bool {
            var current: HUDSourceID? = id
            while let node = current {
                if builder.desktopHiddenNodes.contains(node) { return true }
                current = view.document.scene.node(node)?.parentID
            }
            return false
        }
        for node in view.document.scene.traversalIDs where !excluded(node) {
            let components = view.document.components[node] ?? []
            for component in components where component.enabled {
                if component.kind == "UIRawImage" || component.kind == "RawImage" {
                    ids.insert(component["m_Texture"].targetID?.rawValue ?? "__white")
                } else if component.kind == "UIImage" || component.kind == "Image" {
                    if let image = builder.desktopImages[node] { ids.insert(image.texture) }
                    else if let replacement = builder.desktopSprites[node] {
                        guard let sprite = metadata.sourceSprites[replacement] else {
                            throw HUDSourceError.invalid("Missing desktop replacement Sprite: " + replacement)
                        }
                        ids.insert(sprite.textureID)
                    } else if let sprite = metadata.sprites[component.id] { ids.insert(sprite.textureID) }
                }
            }
            // The original soft-mask reads its UIImage Sprite independently of
            // that graphic's alpha or a desktop display-texture replacement.
            if view.document.component("UISoftMask", on: node) != nil,
               let image = components.first(where: { $0.kind == "UIImage" }),
               let texture = view.document.spriteByComponent[image.id]?["texture"]["id"].string {
                ids.insert(texture)
            }
        }
        return ids.sorted()
    }
    static func frameBuilderDocument(_ view: HUDSourceWatchView) throws -> [String: Any] {
        let metadata = try view.document.renderMetadata()
        func sprite(_ value: HUDSourceImageGeometry.Sprite) -> [String: Any] {
            func v(_ x: SIMD4<Double>) -> [Double] { [x.x, x.y, x.z, x.w] }
            return ["size": [value.size.x, value.size.y], "padding": v(value.padding), "border": v(value.border),
                "outer": v(value.outer), "inner": v(value.inner), "pixelsPerUnit": value.pixelsPerUnit, "textureID": value.textureID]
        }
        var variants: [String: [String: String]] = [:], propertyTypes: [String: Any] = [:]
        for base in Set(metadata.materials.keys.map(\.rawValue) + ["__ui_default"]).sorted() {
            for clip in [false, true] { for soft in [false, true] {
                guard let key = view.renderer.materialKey(named: base, clipRect: clip, alphaClip: false, softMask: soft) else { continue }
                variants[base, default: [:]]["\(clip ? 1 : 0)\(soft ? 1 : 0)"] = key
                if propertyTypes[key] == nil { propertyTypes[key] = try view.renderer.shellPacketMaterial(key)["propertyTypes"] }
            } }
        }
        let profileHover: Any = view.document.desktopProfileCard.map { card in
            ["rootID": card.scene.rootID.rawValue, "buttonIDs": card.buttonIDs.map(\.rawValue).sorted(),
             "nodeIDs": card.scene.nodes.map { $0.id.rawValue }.sorted()] as [String: Any]
        } ?? NSNull()
        return ["scope": "includeDomain=false; includeSourceText=false; widgets=nil; original mounted desktop frame builder",
            "profileHover": profileHover,
            "desktopTextureDependencies": try desktopTextureDependencies(view),
            "sprites": Dictionary(uniqueKeysWithValues: metadata.sprites.map { ($0.key.rawValue, sprite($0.value)) }),
            "sourceSprites": metadata.sourceSprites.mapValues(sprite),
            "textureSizes": metadata.textureSizes.mapValues { [$0.x, $0.y] },
            "materials": Dictionary(uniqueKeysWithValues: metadata.materials.map { ($0.key.rawValue, json($0.value)) }),
            "materialVariants": variants, "materialPropertyTypes": propertyTypes,
            "sourceMeshNames": Dictionary(uniqueKeysWithValues: view.renderer.sourceMeshNames.map { ($0.key.rawValue, $0.value) }),
            "ambientRotationNodes": Set(view.document.animation.ambient.curves.filter { $0.group == "m_RotationCurves" }.flatMap(\.nodeIDs))
                .union(HUDSourceDesktopAmbientMotion.triangleIDs(in: view.document.scene)).map(\.rawValue).sorted(),
            "profileNodeIDs": view.document.desktopProfileCard?.scene.nodes.map { $0.id.rawValue }.sorted() ?? [],
            "defaultSelectableTints": tintsJSON(try HUDSourceSelectableColor(document: view.document).colors(at: 0)),
            "desktopSettings": desktopSettings(view.frameBuilder)]
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
        var desktopTemplateFrames: [String] = []
        var catalogTemplateShapes: Set<String> = []
        var materialTemplateShapes: [String: String] = [:]
        var verificationOracles: [[String: Any]] = []
        var writtenBlobs: [String: [String: Any]] = [:]
        let layers: ModuleReferenceLayerEncoder
        let validationReadbacks: Bool
        init(_ output: URL, validationReadbacks: Bool = true) throws {
            self.output = output; self.validationReadbacks = validationReadbacks
            layers = try ModuleReferenceLayerEncoder(output: output)
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
        func templateShape(_ batch: HUDSourceMetalRenderer.Batch, renderer: HUDSourceMetalRenderer) throws -> String {
            if materialTemplateShapes[batch.material] == nil {
                // Includes actual shader stages, attributes, bindings, blend,
                // depth, stencil, cull and constant material field values.
                let material = try renderer.shellPacketMaterial(batch.material)
                materialTemplateShapes[batch.material] = Reference.hash(try JSONSerialization.data(withJSONObject: material, options: [.sortedKeys]))
            }
            let stencil: Any = batch.stencilOverrides.map {
                ["reference": Int($0.reference), "compare": $0.compare, "pass": $0.pass,
                 "fail": $0.fail, "depthFail": $0.depthFail, "readMask": Int($0.readMask), "writeMask": Int($0.writeMask)]
            } ?? NSNull() as Any
            let shape: [String: Any] = ["material": batch.material,
                "sourcePasses": materialTemplateShapes[batch.material]!,
                "geometry": batch.mesh.hasPrefix("ui/") ? "source-dynamic-ui" : batch.mesh,
                "uniforms": batch.uniformOverrides.mapValues(\.count), "textures": batch.textureOverrides.keys.sorted(),
                "stencil": stencil, "colorWriteMask": batch.colorWriteMask.map { Int($0) } as Any? ?? NSNull(),
                "indexRange": batch.indexRange.map { [$0.lowerBound, $0.upperBound] } as Any? ?? NSNull()]
            return Reference.hash(try JSONSerialization.data(withJSONObject: shape, options: [.sortedKeys]))
        }
        func frame(_ frame: HUDSourceWatchFrameBuilder.Frame, camera: HUDSourceWatchCamera.Frame,
                   view: HUDSourceWatchView, name: String, native: Bool, builderInput: [String: Any]? = nil,
                   templateCoverage: [String: Any]? = nil) throws {
            if builderInput != nil && validationReadbacks {
                // GPU oracle only: excluded from the shipping packet graph.
                // Submit is synchronous and this reads that exact original draw.
                let raw = try view.renderer.shellPacketDrawable()
                var oracle = raw.descriptor
                oracle.merge(try blob(raw.data, path: "verification/" + name + ".raw-bgra.bin")) { _, new in new }
                oracle["name"] = name; verificationOracles.append(oracle)
            }
            var value = try Reference.frameJSON(frame, camera: camera, bounds: view.bounds, view: view, nativeHitQueries: native)
            var batches = value["batches"] as! [[String: Any]]
            for (i, batch) in frame.batches.enumerated() {
                if name.hasSuffix("-top") || name.hasSuffix("-arbitrary-0") || name.hasSuffix("-arbitrary-2") || templateCoverage != nil {
                    catalogTemplateShapes.insert(try templateShape(batch, renderer: view.renderer))
                }
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
            if let templateCoverage { value["templateOnly"] = true; value["templateCoverage"] = templateCoverage }
            if let builderInput { value["builderInput"] = builderInput }
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
            if validationReadbacks {
                let raw = try view.renderer.shellPacketDrawable()
                var oracle = raw.descriptor
                oracle.merge(try blob(raw.data, path: "verification/" + name + ".raw-bgra.bin")) { _, new in new }
                oracle["name"] = name; verificationOracles.append(oracle)
            }
            guard let frame = view.currentFrameForVerification, let camera = view.currentCameraForVerification else {
                throw HUDSourceError.invalid("Missing actual desktop draw")
            }
            try self.frame(frame, camera: camera, view: view, name: name, native: true)
        }
        func desktopTemplates(_ view: HUDSourceWatchView, name: String) throws {
            let camera = try view.cameraModel.frame(screenSize: SIMD2(Double(view.bounds.width), Double(view.bounds.height)))
            let entries = view.desktopNavigationForVerification
            let count = entries.indices.filter { $0 >= 4 && entries[$0].target.module?.group != .bottom }.count
            let navigation = try HUDSourceDesktopNavigationLayout(document: view.document, entryCount: count)
            for state in HUDSourceWatchButtonAnimation.State.allCases {
                let buttons = try HUDSourceWatchButtonAnimation(document: view.document)
                let selectable = try HUDSourceSelectableColor(document: view.document)
                let tintState: HUDSourceSelectableColor.State = state == .highlighted ? .highlighted
                    : state == .pressed ? .pressed : state == .disabled ? .disabled : .normal
                for id in buttons.instanceIDs where buttons.state(on: id) != .disabled {
                    buttons.setHovered(state == .highlighted || state == .pressed, on: id, at: 0)
                    buttons.setState(state, on: id, at: 0)
                }
                for binding in selectable.bindings where binding.sourceInteractable {
                    selectable.setState(tintState, on: binding.buttonNodeID, at: 0)
                }
                for (sample, time) in [0.0, 0.07, 0.4, 2.0].enumerated() {
                    var pose = try view.document.animation.pose(entranceTime: view.document.animation.entrance.lastKeyTime,
                        ambientTime: nil, exitTime: nil, canvasResolution: camera.layout.canvasSize)
                    buttons.apply(to: &pose, at: time); view.document.applyMacButtonAvailability(to: &pose)
                    let tints = selectable.colors(at: time)
                    for (scrollIndex, scroll) in [1.0, 0.5, 0.0].enumerated() {
                        let source = try view.frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                            verticalNormalizedPosition: scroll, desktopNavigation: navigation,
                            selectableTints: tints, forceRebuild: true)
                        var selected: [HUDSourceMetalRenderer.Batch] = [], pending = catalogTemplateShapes
                        for batch in source.batches {
                            if pending.insert(try templateShape(batch, renderer: view.renderer)).inserted { selected.append(batch) }
                        }
                        guard !selected.isEmpty else { continue }
                        let subset = HUDSourceWatchFrameBuilder.Frame(resolved: source.resolved, batches: selected,
                            hits: [], layoutReport: source.layoutReport, diagnostics: source.diagnostics,
                            inheritedAlpha: source.inheritedAlpha)
                        try view.renderer.shellPacketSubmit(selected, time: 0)
                        let frameName = name + "-templates-\(state.rawValue.lowercased())-\(sample)-\(scrollIndex)"
                        try frame(subset, camera: camera, view: view, name: frameName, native: false,
                            templateCoverage: ["scope": "Original independent controller states exercised together for template coverage only; not a whole UI reference frame",
                                "state": state.rawValue, "time": time, "scroll": scroll,
                                "sourceBatchCount": source.batches.count, "retainedTemplateCount": selected.count])
                        desktopTemplateFrames.append(frameName)
                    }
                }
            }
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
                    let tints = view.selectableColor.colors(at: 0, reduceMotion: true)
                    let frame = try view.frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                        verticalNormalizedPosition: 1, desktopNavigation: navigation,
                        selectableTints: tints, forceRebuild: true)
                    try view.renderer.shellPacketSubmit(frame.batches, time: 0)
                    try self.frame(frame, camera: camera, view: view, name: name + "-\(phase)-\(step)", native: false,
                        builderInput: ["pose": poseJSON(pose), "scroll": 1, "entryCount": count,
                            "selectableTints": tintsJSON(tints), "desktopSettings": desktopSettings(view.frameBuilder)])
                }
            }
            // Original seeded decorative motion, independently rebuilt at each
            // sample so the optimized port is checked against the full builder.
            let ambientMotion = HUDSourceDesktopAmbientMotion(animation: view.document.animation, seed: 0x5eed)
            for (index, time) in [0.0, 0.371, 1.113].enumerated() {
                var pose = try view.document.animation.pose(entranceTime: view.document.animation.entrance.lastKeyTime,
                    ambientTime: nil, exitTime: nil, canvasResolution: camera.layout.canvasSize)
                view.applyDesktopButtons(to: &pose, at: 0, reduceMotion: true, forceRebuild: true)
                var ambient = HUDSourceWatchPose(transforms: [:]); ambientMotion.apply(at: time, to: &ambient)
                ambientMotion.apply(at: time, to: &pose)
                let tints = view.selectableColor.colors(at: 0, reduceMotion: true)
                let frame = try view.frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                    verticalNormalizedPosition: 0.37, desktopNavigation: navigation, selectableTints: tints, forceRebuild: true)
                try view.renderer.shellPacketSubmit(frame.batches, time: 0)
                let input: [String: Any] = ["pose": poseJSON(pose), "ambientPose": poseJSON(ambient),
                    "canvasResolution": [camera.layout.canvasSize.x, camera.layout.canvasSize.y],
                    "scroll": 0.37, "entryCount": count, "selectableTints": tintsJSON(tints),
                    "desktopSettings": desktopSettings(view.frameBuilder)]
                try self.frame(frame, camera: camera, view: view, name: name + "-ambient-\(index)", native: false, builderInput: input)
                if index == 2 {
                    let tilted = HUDSourceWatchCamera.Frame(camera: camera.camera,
                        worldRoot: try HUDSourceQuaternion(0.02, -0.04, 0.01, 0.99895).matrix() * camera.worldRoot, layout: camera.layout)
                    let tiltedFrame = try view.frameBuilder.build(pose: pose, worldRoot: tilted.worldRoot,
                        verticalNormalizedPosition: 0.37, desktopNavigation: navigation, selectableTints: tints, forceRebuild: true)
                    try view.renderer.shellPacketSubmit(tiltedFrame.batches, time: 0)
                    try self.frame(tiltedFrame, camera: tilted, view: view, name: name + "-ambient-2-tilted-0", native: false, builderInput: input)
                }
            }
            // Arbitrary source animation/hover/scroll inputs. These samples run
            // the actual builder and export its input, never interpolate a saved
            // raster or choose a prebuilt frame for the Windows runtime.
            let selectable = try HUDSourceSelectableColor(document: view.document)
            for id in selectable.instanceIDs { selectable.setState(.highlighted, on: id, at: 0) }
            let samples: [(Double, Double, Double)] = [(0.137, 0.37, 0.019), (0.419, -0.12, 0.057), (0.683, 1.11, 0.123)]
            for (index, sample) in samples.enumerated() {
                var pose = try view.document.animation.pose(entranceTime: sample.0, ambientTime: nil, exitTime: nil,
                    canvasResolution: camera.layout.canvasSize)
                view.applyDesktopButtons(to: &pose, at: 0, reduceMotion: true, forceRebuild: true)
                let tints = selectable.colors(at: sample.2)
                let frame = try view.frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                    verticalNormalizedPosition: sample.1, desktopNavigation: navigation, selectableTints: tints, forceRebuild: true)
                try view.renderer.shellPacketSubmit(frame.batches, time: 0)
                try self.frame(frame, camera: camera, view: view, name: name + "-arbitrary-\(index)", native: false,
                    builderInput: ["pose": poseJSON(pose), "scroll": sample.1, "entryCount": count,
                        "selectableTints": tintsJSON(tints), "desktopSettings": desktopSettings(view.frameBuilder)])
                for (tiltIndex, q) in [HUDSourceQuaternion(0.02, -0.04, 0.01, 0.99895), HUDSourceQuaternion(-0.04, 0.06, -0.02, 0.9972)].enumerated() {
                    let tilted = HUDSourceWatchCamera.Frame(camera: camera.camera,
                        worldRoot: try q.matrix() * camera.worldRoot, layout: camera.layout)
                    let tiltedFrame = try view.frameBuilder.build(pose: pose, worldRoot: tilted.worldRoot,
                        verticalNormalizedPosition: sample.1, desktopNavigation: navigation, selectableTints: tints, forceRebuild: true)
                    try view.renderer.shellPacketSubmit(tiltedFrame.batches, time: 0)
                    try self.frame(tiltedFrame, camera: tilted, view: view, name: name + "-arbitrary-\(index)-tilted-\(tiltIndex)", native: false,
                        builderInput: ["pose": poseJSON(pose), "scroll": sample.1, "entryCount": count,
                            "selectableTints": tintsJSON(tints), "desktopSettings": desktopSettings(view.frameBuilder)])
                }
            }
        }
    }

    static func run() throws {
        var args = Array(CommandLine.arguments.dropFirst()), output: URL?, sizes: [CGSize] = []
        var validationReadbacks = true
        while !args.isEmpty {
            let option = args.removeFirst()
            if option == "--ui-test" || option == "--export-shell-packet" { continue }
            if option == "--no-validation-readback" { validationReadbacks = false; continue }
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
        let pack = try Pack(output, validationReadbacks: validationReadbacks)
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
                // Capture original resources for every potentially visible
                // desktop graphic, including initially inactive hover hints.
                for id in try desktopTextureDependencies(view) {
                    try pack.texture(id, renderer: view.renderer)
                }
                try pack.checkpoints(view, name: name)
                view.refreshPointerForVerification()
                var steps = 0
                while view.scrollDesktopNavigation(1, animated: false) { steps += 1; try require(steps <= 128, "Unbounded shell scroll") }
                try pack.stable(view, name: name + "-bottom")
                if animation == nil { try pack.desktopTemplates(view, name: name) }
                if animation == nil {
                    let data: [String: Any] = ["library": try object(view.document.library), "scene": try object(view.document.scene),
                        "runtimeRoot": json(view.document.runtimeRoot), "controllerTransitions": json(view.document.controllerTransitions),
                        "mountedDocument": layoutDocument(view.document),
                        "frameBuilder": try frameBuilderDocument(view),
                        "playback": ["finiteEase": "OutQuad", "openingDuration": view.document.animation.entrance.lastKeyTime,
                            "closingDuration": view.document.animation.exit.lastKeyTime, "ambientDuration": view.document.animation.ambient.lastKeyTime,
                            "ambientCheckpointsEnabled": true] as [String: Any],
                        "limitations": ["Native overlay transition curves are not inferred from stable snapshots",
                            "Seeded desktop ambient samples use the original source motion adapter", "Central module canvases are separate packets"]]
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
            "enabled": validationReadbacks, "frames": pack.verificationOracles], to: output.appendingPathComponent("verification-oracles.json"))
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
            "frames": pack.frames, "desktopTemplateFrames": pack.desktopTemplateFrames,
            "animation": animation!, "modules": modules, "nativeRasterAssets": rasters,
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
