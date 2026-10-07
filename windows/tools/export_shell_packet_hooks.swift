// Inserted only into a hash-checked BUILD COPY of HUDSourceMetalRenderer.swift.
// This file is never compiled as a standalone source or shipped in the app.
#if HUD_SHELL_PACKET_EXPORT
    private var shellPacketSamplers: [ObjectIdentifier: [String: Any]] = [:]
    private var shellPacketDepthStates: [ObjectIdentifier: [String: Any]] = [:]

    func shellPacketVertexOffsets() -> [String: Int] {
        ["position": MemoryLayout<Vertex>.offset(of: \Vertex.position)!, "uv": MemoryLayout<Vertex>.offset(of: \Vertex.uv)!,
         "color": MemoryLayout<Vertex>.offset(of: \Vertex.color)!, "normal": MemoryLayout<Vertex>.offset(of: \Vertex.normal)!,
         "uv1": MemoryLayout<Vertex>.offset(of: \Vertex.uv1)!]
    }

    func shellPacketVertexColor(_ batch: Batch) -> SIMD4<Float> { vertexColor(for: batch) }

    func shellPacketDrawable() throws -> (descriptor: [String: Any], data: Data) {
        guard renderedFrameGeneration == submittedFrameGeneration, let data = drawableReadbackBGRA,
              let report = drawableReadbackReport else { throw Failure.message("No completed raw shell drawable") }
        guard data.count == report.rowBytes * report.height else { throw Failure.message("Raw drawable stride mismatch") }
        return (["width": report.width, "height": report.height, "rowBytes": report.rowBytes,
            "pixelFormat": "bgra8Unorm_srgb", "representation": "unmodified GPU sRGB render-target bytes; linear-premultiplied RGB encoded by attachment",
            "blackMatteApplied": false, "desktopCapture": false], data)
    }

    func shellPacketSubmit(_ batches: [Batch], time: Float) throws {
        guard var camera else { throw Failure.message("No prior actual desktop camera") }
        camera.timeSeconds = time
        submit(camera: camera, batches: batches)
        draw()
        _ = try copyDrawableImage()
        guard diagnostics.isEmpty else { throw Failure.message("Shell draw failed: " + diagnostics.joined(separator: "; ")) }
    }

    private func shellPacketRecordSampler(_ state: MTLSamplerState, _ d: MTLSamplerDescriptor) {
        shellPacketSamplers[ObjectIdentifier(state)] = [
            "minFilter": Int(d.minFilter.rawValue), "magFilter": Int(d.magFilter.rawValue),
            "mipFilter": Int(d.mipFilter.rawValue), "addressU": Int(d.sAddressMode.rawValue),
            "addressV": Int(d.tAddressMode.rawValue), "addressW": Int(d.rAddressMode.rawValue),
            "maxAnisotropy": d.maxAnisotropy, "lodMinClamp": d.lodMinClamp,
            "lodMaxClamp": d.lodMaxClamp, "normalizedCoordinates": d.normalizedCoordinates,
            "compareFunction": Int(d.compareFunction.rawValue)]
    }

    private func shellPacketRecordDepth(_ state: MTLDepthStencilState, _ d: MTLDepthStencilDescriptor) {
        func stencil(_ s: MTLStencilDescriptor?) -> Any {
            guard let s else { return NSNull() }
            return ["compare": Int(s.stencilCompareFunction.rawValue),
                "failure": Int(s.stencilFailureOperation.rawValue),
                "depthFailure": Int(s.depthFailureOperation.rawValue),
                "pass": Int(s.depthStencilPassOperation.rawValue),
                "readMask": s.readMask, "writeMask": s.writeMask] as [String: Any]
        }
        shellPacketDepthStates[ObjectIdentifier(state)] = ["depthCompare": Int(d.depthCompareFunction.rawValue),
            "depthWrite": d.isDepthWriteEnabled, "front": stencil(d.frontFaceStencil), "back": stencil(d.backFaceStencil)]
    }

    func shellPacketCamera() throws -> [String: Any] {
        guard let camera else { throw Failure.message("Missing submitted shell camera") }
        func matrix(_ m: simd_float4x4) -> [[Float]] { (0..<4).map { c in (0..<4).map { m[c][$0] } } }
        var row: [String: Any] = ["viewProjection": matrix(camera.viewProjection),
            "viewNoTranslationProjection": matrix(camera.viewNoTranslationProjection),
            "worldSpacePosition": [camera.worldSpacePosition.x, camera.worldSpacePosition.y, camera.worldSpacePosition.z],
            "timeSeconds": camera.timeSeconds, "renderPathInjected": camera.renderPathInjected,
            "flipX": camera.flipX, "flipY": camera.flipY,
            "sceneColorMode": sceneColorMode == .directLDR ? "directLDR" : "sourceRGBHDR",
            "depthStencilPixelFormat": Int(depthStencilPixelFormat.rawValue),
            "sceneColorPixelFormat": Int(sceneColorPixelFormat.rawValue), "drawablePixelFormat": Int(colorPixelFormat.rawValue),
            "clearColor": [clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha],
            "clearDepth": clearDepth, "clearStencil": clearStencil,
            "postprocess": sceneColorMode == .sourceRGBHDR ? ["UberPost_CompositeUI"] : []]
        if let m = camera.projection { row["projection"] = matrix(m) }
        if let m = camera.inverseView { row["inverseView"] = matrix(m) }
        if let v = camera.uiProjectionParameters { row["uiProjectionParameters"] = [v.x, v.y, v.z, v.w] }
        if let a = desktopAccentLinear { row["desktopAccentLinear"] = [a.x, a.y, a.z] }
        return row
    }

    func shellPacketUniforms(_ batch: Batch) throws -> [(descriptor: [String: Any], data: Data)] {
        guard let camera, let device, let material = materials[batch.material] else {
            throw Failure.message("Missing shell uniform context")
        }
        updateUniformCamera(camera)
        var result: [(descriptor: [String: Any], data: Data)] = []
        for pass in material.passes {
            try prepare(pass: pass, values: material.values, propertyTypes: materialPropertyTypes[batch.material] ?? [:], device: device)
            for plan in pass.uniformPlans {
                let stage = plan.vertex ? "vertex" : "fragment"
                guard let uniform = pass.shader.stages[stage]?.uniforms.first(where: { $0.index == plan.index }) else {
                    throw Failure.message("Shell logical uniform name unavailable")
                }
                let descriptor: [String: Any] = ["passID": pass.id, "shaderKey": pass.shaderKey,
                    "stage": stage, "index": plan.index, "bufferName": uniform.name, "byteCount": plan.byteCount,
                    "needsWorld": plan.needsWorld, "needsCamera": plan.needsCamera, "needsTime": plan.needsTime,
                    "fields": plan.fields.map { f in ["name": f.name, "offset": f.offset,
                        "value": f.value as Any? ?? NSNull(), "isColor": f.isColor,
                        "dynamic": String(describing: f.dynamic)] as [String: Any] }]
                result.append((descriptor, uniformData(plan: plan, batch: batch, camera: camera)))
            }
        }
        return result
    }

    func shellPacketMaterial(_ name: String) throws -> [String: Any] {
        guard let material = materials[name] else { throw Failure.message("Missing shell material: " + name) }
        let passes: [[String: Any]] = try material.passes.map { p in
            guard let depth = shellPacketDepthStates[ObjectIdentifier(p.depth)],
                  let specification = Self.shaderSpecifications.first(where: { $0.0 == p.shaderKey }),
                  let a = p.pipelineDescriptor.colorAttachments[0] else {
                throw Failure.message("Unrecorded shell pass descriptor: " + p.id)
            }
            var stages: [String: Any] = [:]
            var attributes: [[String: Int]] = []
            if let vertex = p.pipelineDescriptor.vertexDescriptor {
                for index in 0..<31 {
                    guard let a = vertex.attributes[index], a.format != .invalid else { continue }
                    attributes.append(["attribute": index, "format": Int(a.format.rawValue), "offset": a.offset,
                        "bufferIndex": a.bufferIndex, "stride": vertex.layouts[a.bufferIndex].stride])
                }
            }
            for (name, s) in p.shader.stages {
                stages[name] = ["file": s.file, "function": s.function,
                    "uniforms": s.uniforms.map { u in ["name": u.name, "size": u.size, "index": u.index,
                        "fields": u.fields.map { ["name": $0.name, "offset": $0.offset] as [String: Any] }] as [String: Any] }]
            }
            return ["id": p.id, "shaderKey": p.shaderKey, "shader": p.shader.shader,
                "shaderDescriptorFile": specification.1, "shaderDescriptor": try object(specification.1), "stages": stages,
                "vertexAttributes": attributes,
                "textureBindings": p.shader.textures.map { t in ["name": t.name, "index": t.index,
                    "samplerIndex": t.sampler_index ?? t.index, "stage": t.stage ?? "fragment"] as [String: Any] },
                "blend": ["enabled": a.isBlendingEnabled, "sourceRGB": Int(a.sourceRGBBlendFactor.rawValue),
                    "destinationRGB": Int(a.destinationRGBBlendFactor.rawValue),
                    "sourceAlpha": Int(a.sourceAlphaBlendFactor.rawValue), "destinationAlpha": Int(a.destinationAlphaBlendFactor.rawValue),
                    "rgbOperation": Int(a.rgbBlendOperation.rawValue), "alphaOperation": Int(a.alphaBlendOperation.rawValue),
                    "writeMask": Int(a.writeMask.rawValue)] as [String: Any],
                "depthStencil": depth, "stencilReference": p.stencilReference, "cull": Int(p.cull.rawValue)]
        }
        return ["id": name, "values": material.values, "textures": material.textures,
            "propertyTypes": (materialPropertyTypes[name] ?? [:]).mapValues { ["type": $0.type, "flags": $0.flags] }, "passes": passes]
    }

    func shellPacketTexture(_ name: String) throws -> (descriptor: [String: Any], levels: [Data]) {
        guard let device, let asset = try ensureTexture(named: name, device: device),
              let sampler = shellPacketSamplers[ObjectIdentifier(asset.sampler)] else {
            throw Failure.message("Missing shell texture/sampler: " + name)
        }
        let t = asset.texture
        guard t.textureType == .type2D, t.arrayLength == 1, t.sampleCount == 1,
              t.storageMode == .shared || t.storageMode == .managed else {
            throw Failure.message("Shell export requires CPU-readable source texture: " + name)
        }
        let format: String, bytesPerPixel: Int, compressed: Bool, sRGB: Bool
        switch t.pixelFormat {
        case .rgba8Unorm: (format, bytesPerPixel, compressed, sRGB) = ("rgba8Unorm", 4, false, false)
        case .rgba8Unorm_srgb: (format, bytesPerPixel, compressed, sRGB) = ("rgba8Unorm_srgb", 4, false, true)
        case .bgra8Unorm: (format, bytesPerPixel, compressed, sRGB) = ("bgra8Unorm", 4, false, false)
        case .bgra8Unorm_srgb: (format, bytesPerPixel, compressed, sRGB) = ("bgra8Unorm_srgb", 4, false, true)
        case .r8Unorm: (format, bytesPerPixel, compressed, sRGB) = ("r8Unorm", 1, false, false)
        case .bc7_rgbaUnorm: (format, bytesPerPixel, compressed, sRGB) = ("bc7_rgbaUnorm", 0, true, false)
        case .bc7_rgbaUnorm_srgb: (format, bytesPerPixel, compressed, sRGB) = ("bc7_rgbaUnorm_srgb", 0, true, true)
        default: throw Failure.message("Unsupported actual shell texture format: \(t.pixelFormat.rawValue)")
        }
        var levels: [Data] = [], mips: [[String: Any]] = []
        for level in 0..<t.mipmapLevelCount {
            let w = max(1, t.width >> level), h = max(1, t.height >> level)
            let rowBytes = compressed ? ((w + 3) / 4) * 16 : w * bytesPerPixel
            let count = rowBytes * (compressed ? (h + 3) / 4 : h)
            guard count <= 128 * 1024 * 1024 else { throw Failure.message("Unbounded shell texture") }
            var data = Data(count: count)
            data.withUnsafeMutableBytes { t.getBytes($0.baseAddress!, bytesPerRow: rowBytes,
                from: MTLRegionMake2D(0, 0, w, h), mipmapLevel: level) }
            levels.append(data); mips.append(["level": level, "width": w, "height": h, "rowBytes": rowBytes])
        }
        return (["id": name, "width": t.width, "height": t.height, "pixelFormat": format,
            "sRGB": sRGB, "sampler": sampler, "mips": mips,
            "alphaStorage": "unaltered actual source texture; shader owns alpha operations",
            "rowOrigin": "actual Metal level rows; original source UVs retained"], levels)
    }
#endif
