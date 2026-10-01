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

    // These tiny GPU fixtures use independently specified texels and blend
    // equations. They do not sample the menu or substitute for its rendering.
    private static func half(_ word: UInt16) -> Double {
        let exponent = Int((word >> 10) & 31), fraction = Int(word & 1023)
        let magnitude: Double
        if exponent == 0 { magnitude = Double(fraction) * pow(2, -24) }
        else if exponent == 31 { magnitude = fraction == 0 ? .infinity : .nan }
        else { magnitude = (1 + Double(fraction) / 1024) * pow(2, Double(exponent - 15)) }
        return word & 0x8000 == 0 ? magnitude : -magnitude
    }

    private static func unsignedFloat(_ word: UInt32, mantissaBits: Int) -> Double {
        let mask = (UInt32(1) << mantissaBits) - 1
        let exponent = Int(word >> mantissaBits), fraction = Int(word & mask)
        if exponent == 0 { return Double(fraction) * pow(2, Double(1 - 15 - mantissaBits)) }
        if exponent == 31 { return fraction == 0 ? .infinity : .nan }
        return (1 + Double(fraction) / Double(UInt32(1) << mantissaBits)) * pow(2, Double(exponent - 15))
    }

    private static func littleWord(_ data: Data, at offset: Int) -> UInt32 {
        UInt32(data[offset]) | (UInt32(data[offset + 1]) << 8) |
            (UInt32(data[offset + 2]) << 16) | (UInt32(data[offset + 3]) << 24)
    }

    private static func encodedByte(_ linear: Double) -> Double {
        let value = max(0, min(1, linear))
        return ((value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1 / 2.4) - 0.055) * 255).rounded()
    }

    private static func hdrReport(_ readback: HUDSourceMetalRenderer.SceneColorReadback,
                                 finalBGRA: Data, finalReport: HUDSourceDrawableReadback.Report) throws -> [String: Any] {
        guard readback.pixelFormat == "rg11b10Float", readback.width > 0, readback.height > 0,
              readback.rowBytes >= readback.width * 4,
              readback.data.count == readback.rowBytes * readback.height,
              finalReport.width == readback.width, finalReport.height == readback.height,
              finalReport.rowBytes >= readback.width * 4,
              finalBGRA.count == finalReport.rowBytes * finalReport.height else {
            throw HUDSourceError.invalid("Invalid original RGB HDR readback dimensions")
        }
        var samples: [[String: Any]] = []
        for yStep in 0..<4 {
            for xStep in 0..<5 {
                let x = (readback.width - 1) * xStep / 4, y = (readback.height - 1) * yStep / 3
                let offset = y * readback.rowBytes + x * 4
                let word = littleWord(readback.data, at: offset)
                let rgb = [unsignedFloat(word & 2047, mantissaBits: 6),
                    unsignedFloat((word >> 11) & 2047, mantissaBits: 6),
                    unsignedFloat((word >> 22) & 1023, mantissaBits: 5)]
                guard rgb.allSatisfy(\.isFinite) else {
                    throw HUDSourceError.invalid("Nonfinite original HDR sample at \(x),\(y)")
                }
                let finalOffset = y * finalReport.rowBytes + x * 4
                let measured = (0..<4).map { Int(finalBGRA[finalOffset + $0]) }
                let predicted = [Int(encodedByte(rgb[2])), Int(encodedByte(rgb[1])), Int(encodedByte(rgb[0])), 255]
                guard zip(measured, predicted).allSatisfy({ abs($0.0 - $0.1) <= 2 }) else {
                    throw HUDSourceError.invalid("HDR menu upright composite/color mismatch at \(x),\(y): actual BGRA \(measured), expected \(predicted)")
                }
                samples.append(["x": x, "y": y, "packedWord": String(format: "%08x", word),
                    "rawBytes": (0..<4).map { Int(readback.data[offset + $0]) }, "linearRGB": rgb,
                    "finalBGRA": measured, "independentExpectedFinalBGRA": predicted])
            }
        }
        return ["width": readback.width, "height": readback.height, "rowBytes": readback.rowBytes,
            "pixelFormat": readback.pixelFormat, "implicitSampledAlpha": 1,
            "rowOrder": "Original Metal texture memory rows; no vertical conversion",
            "representation": "Raw source RGB scene before composite; no transfer, tone mapping or unpremultiplication",
            "finalDrawableComparison": "Same-coordinate sampled RGB through sRGB encoding and UNORM clamp; alpha 1; tolerance 2 bytes",
            "finalDrawableComparisonPassed": true,
            "coverage": "20 bounded sample points; not a full-image range or finite-value scan", "samples": samples]
    }

    private static func verifyComposite(device: MTLDevice, resourceRoot: URL) throws -> [[String: Any]] {
        guard let queue = device.makeCommandQueue() else { throw HUDSourceError.invalid("Cannot allocate composite fixture queue") }
        let base = [0.125, 0.25, 0.375, 0.5]
        // Exact unsigned-float powers, with visibly different rows and columns.
        let packed: [UInt32] = [
            (15 << 6) | ((13 << 6) << 11),
            ((15 << 6) << 11) | ((14 << 5) << 22),
            (14 << 6) | ((16 << 5) << 22),
            (13 << 6) | ((14 << 6) << 11) | ((15 << 5) << 22)]
        let rgbTexels = [[1.0, 0.25, 0, 1], [0, 1, 0.5, 1], [0.5, 0, 2, 1], [0.25, 0.5, 1, 1]]
        // Fractional, >1 and negative alpha exercise the original fragment's
        // alpha clamp, destination Load, and SrcAlpha blend for RGB AND alpha.
        let halfWords: [UInt16] = [0x4000, 0x3800, 0x3400, 0x3400,
            0x3400, 0x3c00, 0x3800, 0x3e00, 0x4400, 0x3000, 0x3800, 0xb800,
            0x3800, 0x4000, 0x3c00, 0x3800]
        let rgbaTexels = [[2.0, 0.5, 0.25, 0.25], [0.25, 1, 0.5, 1.5],
            [4, 0.125, 0.5, -0.5], [0.5, 2, 1, 0.5]]
        func texture(_ format: MTLPixelFormat, usage: MTLTextureUsage, storage: MTLStorageMode) throws -> MTLTexture {
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: format, width: 2, height: 2, mipmapped: false)
            descriptor.usage = usage; descriptor.storageMode = storage
            guard let texture = device.makeTexture(descriptor: descriptor) else {
                throw HUDSourceError.invalid("Cannot allocate independent composite fixture texture")
            }
            return texture
        }
        let rgbInput = try texture(.rg11b10Float, usage: .shaderRead, storage: .shared)
        packed.withUnsafeBytes { rgbInput.replace(region: MTLRegionMake2D(0, 0, 2, 2), mipmapLevel: 0,
            withBytes: $0.baseAddress!, bytesPerRow: 8) }
        let rgbaInput = try texture(.rgba16Float, usage: .shaderRead, storage: .shared)
        halfWords.withUnsafeBytes { rgbaInput.replace(region: MTLRegionMake2D(0, 0, 2, 2), mipmapLevel: 0,
            withBytes: $0.baseAddress!, bytesPerRow: 16) }
        var reports: [[String: Any]] = []
        for outputFormat in [MTLPixelFormat.rgba16Float, .bgra8Unorm_srgb] {
            let isSRGB = outputFormat == .bgra8Unorm_srgb
            let isNormalized = outputFormat == .bgra8Unorm || isSRGB
            func normalizedByte(_ value: Double) -> Double {
                (max(0, min(1, value)) * 255).rounded()
            }
            // Apple MTLPixelFormat documents sRGB read/write conversion and linear alpha:
            // https://developer.apple.com/documentation/metal/mtlpixelformat
            // Decode the initialized attachment's stored values before the blend. A
            // plain UNORM attachment has the same bounded range without sRGB transfer.
            let destinationLinear = base.enumerated().map { channel, value -> Double in
                guard isNormalized else { return value }
                guard isSRGB && channel < 3 else { return normalizedByte(value) / 255 }
                let stored = encodedByte(value) / 255
                return stored <= 0.04045 ? stored / 12.92 : pow((stored + 0.055) / 1.055, 2.4)
            }
            let composite = try HUDSourceUIComposite(device: device, resourceRoot: resourceRoot, outputPixelFormat: outputFormat)
            for (inputName, input, texels) in [("rg11b10Float", rgbInput, rgbTexels), ("rgba16Float", rgbaInput, rgbaTexels)] {
                for flipY in [Float(0), 1] {
                    for flipX in [Float(0), 1] {
                        let destination = try texture(outputFormat, usage: .renderTarget, storage: .private)
                        guard let buffer = device.makeBuffer(length: 512, options: .storageModeShared),
                              let command = queue.makeCommandBuffer() else {
                            throw HUDSourceError.invalid("Cannot allocate composite fixture readback")
                        }
                        let initialize = MTLRenderPassDescriptor()
                        initialize.colorAttachments[0].texture = destination
                        initialize.colorAttachments[0].loadAction = .clear; initialize.colorAttachments[0].storeAction = .store
                        initialize.colorAttachments[0].clearColor = MTLClearColor(red: base[0], green: base[1], blue: base[2], alpha: base[3])
                        guard let clear = command.makeRenderCommandEncoder(descriptor: initialize) else {
                            throw HUDSourceError.invalid("Cannot initialize composite fixture destination")
                        }
                        clear.endEncoding()
                        try composite.encode(command: command, input: input, destination: destination, flipX: flipX, flipY: flipY)
                        guard let blit = command.makeBlitCommandEncoder() else {
                            throw HUDSourceError.invalid("Cannot read independent composite fixture")
                        }
                        blit.copy(from: destination, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
                            sourceSize: MTLSize(width: 2, height: 2, depth: 1), to: buffer, destinationOffset: 0,
                            destinationBytesPerRow: 256, destinationBytesPerImage: 512)
                        blit.endEncoding(); command.commit(); command.waitUntilCompleted()
                        if let error = command.error { throw error }
                        guard command.status == .completed else { throw HUDSourceError.invalid("Independent composite fixture did not complete") }
                        let data = Data(bytes: buffer.contents(), count: 512)
                        var actual: [[Double]] = [], expected: [[Double]] = []
                        var maxError = 0.0
                        for y in 0..<2 {
                            for x in 0..<2 {
                                let sourceX = flipX == 0 ? x : 1 - x, sourceY = flipY == 0 ? 1 - y : y
                                let source = texels[sourceY * 2 + sourceX], alpha = max(0, min(1, source[3]))
                                // The actual native fixed-function UNORM regression proves
                                // source RGB is bounded before blending, not just at storage:
                                // G=2,a=.5,D=.25 gives linear .625 / sRGB byte207, not255.
                                // The independent floating-point attachment retains G=1.125.
                                var predicted = (0..<3).map { channel -> Double in
                                    let sourceColor = isNormalized ? max(0, min(1, source[channel])) : source[channel]
                                    return sourceColor * alpha + destinationLinear[channel] * (1 - alpha)
                                }
                                predicted.append(alpha * alpha + destinationLinear[3] * (1 - alpha))
                                let offset = y * 256 + x * (outputFormat == .rgba16Float ? 8 : 4)
                                let measured: [Double]
                                if outputFormat == .rgba16Float {
                                    measured = (0..<4).map { channel in
                                        let index = offset + channel * 2
                                        return half(UInt16(data[index]) | (UInt16(data[index + 1]) << 8))
                                    }
                                } else {
                                    measured = [Double(data[offset + 2]), Double(data[offset + 1]), Double(data[offset]), Double(data[offset + 3])]
                                    predicted = (0..<3).map { isSRGB ? encodedByte(predicted[$0]) : normalizedByte(predicted[$0]) } + [normalizedByte(predicted[3])]
                                }
                                let error = zip(measured, predicted).map { abs($0.0 - $0.1) }.max() ?? .infinity
                                guard measured.allSatisfy(\.isFinite), error <= (outputFormat == .rgba16Float ? 0.004 : 2) else {
                                    throw HUDSourceError.invalid("Original composite mismatch: \(inputName), \(outputFormat), flip(\(flipX),\(flipY)), texel(\(x),\(y)); actual \(measured), expected \(predicted)")
                                }
                                maxError = max(maxError, error); actual.append(measured); expected.append(predicted)
                            }
                        }
                        reports.append(["inputPixelFormat": inputName,
                            "outputPixelFormat": outputFormat == .rgba16Float ? "rgba16Float" : (isSRGB ? "bgra8Unorm_srgb" : "bgra8Unorm"),
                            "flipX": Int(flipX), "flipY": Int(flipY),
                            "rowMapping": flipY == 0 ? "Vertically reversed input memory rows" : "Preserves input memory rows",
                            "pixelOrder": "Row-major top-left, top-right, bottom-left, bottom-right; RGBA channels",
                            "units": outputFormat == .rgba16Float ? "Linear floating-point" : (isSRGB ? "sRGB RGB bytes / linear alpha byte" : "Linear UNORM RGB / alpha bytes"),
                            "sourceRGBBeforeBlend": isNormalized ? "Clamp in linear space to [0,1]" : "Unbounded floating-point RGB",
                            "destinationBeforeBlend": destinationLinear,
                            "outputTransfer": isSRGB ? "Linear blend, then sRGB RGB encode; alpha remains linear" : "No sRGB conversion",
                            "actual": actual, "independentExpected": expected, "maxAbsoluteError": maxError, "passed": true])
                    }
                }
            }
        }
        return reports
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
        guard let resourceRoot = HUDResources.url(for: "WatchSource") else {
            throw HUDSourceError.invalid("Original composite fixture resources unavailable")
        }
        var hdrRenderer: HUDSourceMetalRenderer?
        var hdrBuilder: HUDSourceWatchFrameBuilder?
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
            ("domain-region02-lv008-hover", nil, 0, nil, nil),
            ("hdr-stable", nil, 0, nil, nil), ("hdr-hover-hold", nil, 0, nil, 1),
            ("widgets-reference", nil, 0, nil, nil), ("widgets-weapon-reference", nil, 0, nil, nil),
            ("widgets-banner-000", nil, 0, nil, nil), ("widgets-banner-4000", nil, 4, nil, nil),
            ("widgets-banner-midpoint", nil, 4.1, nil, nil),
            ("widgets-banner-endpoint", nil, 4.200000002980232, nil, nil),
            ("widgets-banner-reverse-midpoint", nil, 4.4, nil, nil),
            ("widgets-banner-reverse-endpoint", nil, 4.500000002980232, nil, nil),
            ("widgets-banner-tint-normal", nil, 0, nil, nil),
            ("widgets-banner-tint-hover-000", nil, 0, nil, nil),
            ("widgets-banner-tint-hover-midpoint", nil, 0.05000000074505806, nil, nil),
            ("widgets-banner-tint-hover-endpoint", nil, 0.10000000149011612, nil, nil),
            ("widgets-banner-tint-pressed-midpoint", nil, 0.15000000223517418, nil, nil),
            ("widgets-banner-tint-pressed-endpoint", nil, 0.20000000298023224, nil, nil)
        ]
        for (name, opening, ambient, closing, hover) in samples {
            trace("resolving " + name)
            let isHDR = name.hasPrefix("hdr-")
            if isHDR && hdrRenderer == nil {
                trace("loading independent original RGB HDR renderer")
                let originalHDR = try HUDSourceMetalRenderer(frame: viewport, resourceRoot: resourceRoot,
                    sceneColorMode: .sourceRGBHDR)
                hdrBuilder = try HUDSourceWatchFrameBuilder(document: document, renderer: originalHDR)
                hdrRenderer = originalHDR
                window.contentView = originalHDR
                originalHDR.drawableSize = CGSize(width: size.x, height: size.y)
                window.displayIfNeeded()
                try abiEncoder.encode(originalHDR.constantBufferABI)
                    .write(to: output.appendingPathComponent("source-hdr-constant-buffer-abi.json"))
            }
            let activeRenderer: HUDSourceMetalRenderer
            let activeBuilder: HUDSourceWatchFrameBuilder
            if isHDR {
                guard let hdrRenderer, let hdrBuilder else { throw HUDSourceError.invalid("Original HDR fixture not initialized") }
                activeRenderer = hdrRenderer; activeBuilder = hdrBuilder
            } else {
                activeRenderer = renderer
                activeBuilder = name.hasPrefix("domain-region02-lv008-") ? region02Builder : builder
            }
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
            let isWidgetFixture = name.hasPrefix("widgets-")
            activeBuilder.widgetState = isWidgetFixture ? .recordReference : .desktopReference
            if name == "widgets-weapon-reference" {
                activeBuilder.widgetState.bannerArtwork = "weapon_typhoeus_banner"
            }
            let isBannerFixture = name.hasPrefix("widgets-banner-")
            if isBannerFixture {
                activeBuilder.widgetState.bannerArtworks = ["yvonne_banner", "weapon_typhoeus_banner"]
                if name == "widgets-banner-reverse-midpoint" {
                    try activeBuilder.selectWidgetBanner(index: 0, at: 4.3)
                }
            }
            let colors = try HUDSourceSelectableColor(document: document)
            let isColorFixture = name.hasPrefix("widgets-banner-tint-")
            if isColorFixture {
                guard let cell = document.widgets?.bannerInstances.first else {
                    throw HUDSourceError.invalid("Original banner ColorTint target missing")
                }
                if name != "widgets-banner-tint-normal" {
                    colors.setState(.highlighted, on: cell.buttonNodeID, at: 0)
                }
                if name.hasPrefix("widgets-banner-tint-pressed-") {
                    colors.setState(.pressed, on: cell.buttonNodeID, at: 0.10000000149011612)
                }
            }
            let frame = try activeBuilder.build(pose: pose, worldRoot: view.worldRoot, domainAnimationState: domainState,
                                                widgetTime: ambient ?? opening ?? closing ?? 0,
                                                selectableTints: colors.colors(at: ambient ?? opening ?? closing ?? 0))
            if let missing = frame.diagnostics.first(where: { $0.hasPrefix("Unresolved original UIImage Sprite:") }) {
                throw HUDSourceError.invalid("Active source artwork was skipped: " + missing)
            }
            var widgetRegression: [String: Any] = [:]
            if isWidgetFixture {
                guard let widgets = document.widgets,
                      let root = frame.resolved[widgets.sourceScene.rootID], root.activeInHierarchy,
                      root.rect?.size == SIMD2<Double>(364, 128) else {
                    throw HUDSourceError.invalid("Original BP13 widget is missing, inactive or resized")
                }
                let widgetCAB = "CAB-7979328e8a85d73c8b989cdca5a79bf8:"
                let requiredNodes = [widgetCAB + "-6424786528150829925", widgetCAB + "6805299908380312731",
                    widgetCAB + "4639931523971466395", widgets.bannerInstances[0].imageNodeID.rawValue]
                var textures: [String: String] = [:]
                for id in requiredNodes {
                    guard let batch = frame.batches.first(where: { $0.sourceNodeID == id }),
                          let texture = batch.textureOverrides["_MainTex"] else {
                        throw HUDSourceError.invalid("Explicit widget artwork did not produce its original batch: " + id)
                    }
                    textures[id] = texture
                }
                let hits = frame.hits.filter { widgets.profileButtonIDs.contains($0.buttonID) }
                guard !hits.isEmpty else { throw HUDSourceError.invalid("Original widget has no profile raycasts") }
                widgetRegression = ["sourceRootID": widgets.sourceScene.rootID.rawValue,
                    "localSize": [364, 128], "drawnArtworkTextures": textures,
                    "profileHitCount": hits.count, "accountData": "Controlled generic fixture; no personal name or UID"]
                if isBannerFixture {
                    guard let sample = activeBuilder.widgetBannerSample else {
                        throw HUDSourceError.invalid("Original banner playback sample missing")
                    }
                    let expectedPosition: Double
                    switch name {
                    case "widgets-banner-midpoint": expectedPosition = Double(Float(sin(Double(Float(0.5) * Float(1.5707963705062866)))))
                    case "widgets-banner-endpoint": expectedPosition = 1
                    case "widgets-banner-reverse-midpoint": expectedPosition = Double(Float(1) - Float(sin(Double(Float(0.5) * Float(1.5707963705062866)))))
                    default: expectedPosition = 0
                    }
                    guard abs(sample.normalizedPosition - expectedPosition) < 0.000001,
                          widgets.bannerInstances.allSatisfy({ frame.resolved[$0.rootID]?.activeInHierarchy == true }),
                          frame.resolved[widgets.bannerImageNodeID]?.activeInHierarchy == false,
                          widgets.bannerInstances.allSatisfy({ instance in frame.batches.contains { $0.sourceNodeID == instance.imageNodeID.rawValue } }) else {
                        throw HUDSourceError.invalid("Original banner clone or OutSine transition regression: " + name)
                    }
                    widgetRegression["bannerNormalizedPosition"] = sample.normalizedPosition
                    widgetRegression["bannerExpectedNormalizedPosition"] = expectedPosition
                    widgetRegression["bannerSelectedPage"] = sample.selectedIndex
                    widgetRegression["bannerFrameOrder"] = "Adapter tween, sampled center callback, then hold tick; original cross-frame scheduling remains unverified"
                }
                if isColorFixture {
                    let expectedAlpha: Float
                    switch name {
                    case "widgets-banner-tint-hover-midpoint": expectedAlpha = 0.1
                    case "widgets-banner-tint-hover-endpoint": expectedAlpha = 0.2
                    case "widgets-banner-tint-pressed-midpoint": expectedAlpha = 0.12
                    case "widgets-banner-tint-pressed-endpoint": expectedAlpha = 0.04
                    default: expectedAlpha = 0
                    }
                    let lights = frame.batches.filter { $0.sourceNodeID == widgets.bannerInstances[0].lightNodeID.rawValue }
                    let matchesAlpha = expectedAlpha == 0 ? lights.isEmpty
                        : (!lights.isEmpty && lights.allSatisfy { abs($0.color.w - expectedAlpha) < 0.000001 })
                    guard matchesAlpha else {
                        throw HUDSourceError.invalid("Source ColorTint did not multiply the original Light alpha: " + name)
                    }
                    widgetRegression["bannerLightExpectedAlpha"] = expectedAlpha
                    widgetRegression["bannerLightBatchAlpha"] = lights.map { $0.color.w }
                    widgetRegression["colorTint"] = "Adapter independent CanvasRenderer RGBA channel; original Graphic alpha 0.2 retained; original final vertex packing remains unobserved"
                }
            }
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
                        let gpuValue = activeRenderer.gpuMaterialValue(sourceValue, property: property, materialKey: batch.material)
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
            activeRenderer.submit(camera: clock, batches: frame.batches)
            activeRenderer.draw()
            let image = try activeRenderer.copyDrawableImage()
            guard let pixelReport = activeRenderer.drawableReadbackReport else {
                throw HUDSourceError.invalid("Source GPU fixture has no raw pixel report for \(name)")
            }
            let pixelReportFile = name + "-raw-pixel-report.json"
            try abiEncoder.encode(pixelReport).write(to: output.appendingPathComponent(pixelReportFile))
            if name == "stable" {
                guard let rawPixels = activeRenderer.drawableReadbackBGRA else {
                    throw HUDSourceError.invalid("Source GPU fixture has no stable raw pixel buffer")
                }
                try rawPixels.write(to: output.appendingPathComponent("stable-raw.bgra"))
            }
            var hdrFiles: [String: Any] = [:]
            if isHDR {
                let raw = try activeRenderer.copySceneColorReadback()
                guard let finalBGRA = activeRenderer.drawableReadbackBGRA else {
                    throw HUDSourceError.invalid("HDR fixture has no final raw drawable")
                }
                let dataFile = name + "-scene-raw.rg11b10f", reportFile = name + "-scene-raw-report.json"
                try raw.data.write(to: output.appendingPathComponent(dataFile))
                try JSONSerialization.data(withJSONObject: hdrReport(raw, finalBGRA: finalBGRA, finalReport: pixelReport), options: [.prettyPrinted, .sortedKeys])
                    .write(to: output.appendingPathComponent(reportFile))
                hdrFiles = ["rawScenePixels": dataFile, "rawSceneReport": reportFile,
                    "originalCompositeProgram": 726, "expectedUprightCompositeFlipX": 0,
                    "expectedUprightCompositeFlipY": 1,
                    "backdrop": "Fixture black initialized attachment; not the game's live scene or desktop"]
            }
            guard activeRenderer.diagnostics.isEmpty else {
                throw HUDSourceError.invalid("Source GPU fixture skipped content in \(name): \(activeRenderer.diagnostics.joined(separator: "; "))")
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
            let diagnostics = frame.diagnostics + activeRenderer.diagnostics
            manifest.append(["file": file, "rawPixelReport": pixelReportFile, "batches": frame.batches.count, "hits": frame.hits.count,
                "sceneColorMode": isHDR ? "sourceRGBHDR" : "directLDR", "hdr": hdrFiles,
                "widgetFixture": isWidgetFixture ? "Explicit original artwork and generic profile; no personal name or UID" : "No inferred game account fields",
                "bannerArtwork": activeBuilder.widgetState.bannerArtwork.map { $0 as Any } ?? NSNull(),
                "bannerArtworks": activeBuilder.widgetState.bannerArtworks.map { $0 as Any } ?? NSNull(),
                "widgetRegression": widgetRegression,
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
        guard let device = renderer.device else { throw HUDSourceError.invalid("Composite fixture Metal device unavailable") }
        trace("verifying independent composite direction, Load, alpha clamp and HDR colors")
        let compositeTests = try verifyComposite(device: device, resourceRoot: resourceRoot)
        let compositeReport: [String: Any] = ["schemaVersion": 1, "sourceProgram": 726,
            "shader": "Unmodified original base UberPost_CompositeUI; no volume keywords",
            "fixture": "Independent 2x2 exact-power texels; initialized destination RGBA=(.125,.25,.375,.5)",
            "independentBlendEquation": "a=clamp(source.a,0,1); S=clamp(source.rgb,0,1) for UNORM attachments, otherwise source.rgb; RGB=S*a+decodedDestination.rgb*(1-a); A=a*a+storedDestination.a*(1-a)",
            "attachmentPolicy": "Floating-point RGB remains unbounded; UNORM source RGB is clamped before blending; sRGB UNORM destination is decoded before the linear blend and encoded afterward; plain UNORM has no sRGB transfer",
            "formatDocumentation": "https://developer.apple.com/documentation/metal/mtlpixelformat",
            "preblendClampEvidence": "Native GPU regression: sourceG=2,a=.5,destinationG=.25 yields sRGB G=207; postblend-only clamp predicts255. Floating-point G=1.125 remains independently tested. Apple public format documentation does not specify this fixed-function ordering explicitly.",
            "orientation": "Metal clip +Y is the top row: flipY=0 reverses input rows, flipY=1 preserves them",
            "passed": true, "tests": compositeTests]
        let compositeFile = "source-composite-gpu-regression.json"
        try JSONSerialization.data(withJSONObject: compositeReport, options: [.prettyPrinted, .sortedKeys])
            .write(to: output.appendingPathComponent(compositeFile))
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
            "hdrCompositeRegression": compositeFile,
            "hdrBoundary": "Original RGB target and base composite on a black fixture backdrop; optional game postprocess volume state and desktop backdrop are not captured",
            "availability": "All 22 mapped macOS functions enabled; game account locks and notifications absent",
            "commit": ProcessInfo.processInfo.environment["GITHUB_SHA"] ?? "local", "frames": manifest]
        try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
            .write(to: output.appendingPathComponent("source-preview-manifest.json"))
        window.orderOut(nil)
    }
}
