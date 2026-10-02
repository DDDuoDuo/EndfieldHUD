import AppKit
import CoreGraphics
import CryptoKit
import Foundation
import MetalKit
import simd

/// Independent synthetic GPU regression. Never invokes desktop capture,
/// display enumeration, window enumeration or permission APIs.
@main
enum VerifySourceBackdropGPU {
    private static var frostedProgress: [[String: Any]] = []
    private static var rawImageProgress: [[String: Any]] = []
    private static var tileProgress: [[String: Any]] = []
    private static var sidecarProgress: [String: Any] = [:]
    private static var formatProgress: [String: Any] = [:]
    private enum AttachmentRounding: String, CaseIterable, Hashable {
        case nearestEven, towardZero, halfThenTowardZero
    }
    // Selected only by the independent, untouched copy-shader format probe.
    // No image-dependent fitting or tolerance changes are permitted.
    private static var attachmentRounding: AttachmentRounding = .nearestEven
    private struct StageReadback {
        let name: String
        let width: Int
        let height: Int
        let stride: Int
        let buffer: MTLBuffer
    }
    private static var lastStageReadbacks: [StageReadback] = []
    private static var diagnosticOutput: URL?
    private struct Raster {
        let width: Int
        let height: Int
        var pixels: [SIMD3<Float>]
        subscript(_ x: Int, _ y: Int) -> SIMD3<Float> { pixels[y * width + x] }
    }
    private static let horizontalWeights: [Float] = [
        0.01621622033417224884, 0.05405405163764953613, 0.12162162363529205322,
        0.19459459185600280762, 0.22702702879905700684, 0.19459459185600280762,
        0.12162162363529205322, 0.05405405163764953613, 0.01621622033417224884]
    private static let verticalOffsets: [Float] = [-3.230769157409668, -1.384615421295166, 0, 1.384615421295166, 3.230769157409668]
    private static let verticalWeights: [Float] = [0.07027027010917664, 0.31621623039245605,
        0.22702702879905700684, 0.31621623039245605, 0.07027027010917664]

    private static func require(_ condition: Bool, _ message: String) throws {
        if !condition { throw HUDSourceError.invalid(message) }
    }
    private static func sample(_ image: Raster, u: Float, v: Float) -> SIMD3<Float> {
        // Bilinear clamp at texel centers, specified independently of the GPU helper.
        let x = u * Float(image.width) - 0.5, y = v * Float(image.height) - 0.5
        let ix = Int(floor(x)), iy = Int(floor(y))
        let fx = x - Float(ix), fy = y - Float(iy)
        let x0 = max(0, min(image.width - 1, ix)), x1 = max(0, min(image.width - 1, ix + 1))
        let y0 = max(0, min(image.height - 1, iy)), y1 = max(0, min(image.height - 1, iy + 1))
        return (image[x0, y0] * (1 - fx) + image[x1, y0] * fx) * (1 - fy) +
            (image[x0, y1] * (1 - fx) + image[x1, y1] * fx) * fy
    }
    private static func threshold(_ color: SIMD3<Float>) -> SIMD3<Float> {
        color / (max(max(max(color.x, color.y), color.z), 2.5) / 2.5)
    }
    private static func unsignedFloat(_ value: Float, mantissaBits: Int,
                                      rounding: AttachmentRounding? = nil) -> Float {
        // R/G: 5-bit exponent + 6-bit fraction; B: 5+5. No alpha.
        guard value > 0 else { return 0 }
        let selected = rounding ?? attachmentRounding
        let source = selected == .halfThenTowardZero ?
            unsignedFloat(value, mantissaBits: 10, rounding: .nearestEven) : value
        let x = Double(source), minNormal = pow(2.0, -14.0)
        let exponent: Double
        if x < minNormal { exponent = 1 - 15 - Double(mantissaBits) }
        else { exponent = floor(log2(x)) - Double(mantissaBits) }
        let unit = pow(2.0, exponent)
        return Float((x / unit).rounded(selected == .nearestEven ? .toNearestOrEven : .towardZero) * unit)
    }
    private static func rgbHDR(_ color: SIMD3<Float>) -> SIMD3<Float> {
        SIMD3(unsignedFloat(color.x, mantissaBits: 6), unsignedFloat(color.y, mantissaBits: 6),
              unsignedFloat(color.z, mantissaBits: 5))
    }
    private static func filtered(_ input: Raster, width: Int, height: Int,
                                 horizontal: Bool, wrongInputTexelSize: Bool) -> Raster {
        var output = Raster(width: width, height: height, pixels: Array(repeating: .zero, count: width * height))
        for y in 0..<height {
            for x in 0..<width {
                let u = (Float(x) + 0.5) / Float(width)
                // The original Vulkan-derived fullscreen vertex outputs UV1-v.
                // Metal clip +Y is the top row: each original H/V draw reverses Y.
                let v = 1 - (Float(y) + 0.5) / Float(height)
                var color = SIMD3<Float>.zero
                if horizontal {
                    let step = 1 / Float(wrongInputTexelSize ? input.width : width)
                    for index in 0..<9 {
                        color += threshold(sample(input, u: u + Float(index - 4) * step, v: v)) * horizontalWeights[index]
                    }
                } else {
                    let step = 1 / Float(wrongInputTexelSize ? input.height : height)
                    for index in 0..<5 {
                        color += threshold(sample(input, u: u, v: v + verticalOffsets[index] * step)) * verticalWeights[index]
                    }
                }
                output.pixels[y * width + x] = rgbHDR(color)
            }
        }
        return output
    }
    private static func sRGBByte(_ value: Float) -> UInt8 {
        let x = max(0, min(1, Double(value)))
        let encoded = x <= 0.0031308 ? x * 12.92 : 1.055 * pow(x, 1 / 2.4) - 0.055
        return UInt8((encoded * 255).rounded(.toNearestOrEven))
    }
    private static func oracle(_ input: Raster, outputWidth: Int, outputHeight: Int,
                               wrongInputTexelSize: Bool = false, wrongFinalFlip: Bool = false) -> [UInt8] {
        var current = input
        // Integer ceiling is independent of the production helper's floating size construction.
        for divisor in [4, 8, 16] {
            let width = (input.width + divisor - 1) / divisor
            let height = (input.height + divisor - 1) / divisor
            current = filtered(current, width: width, height: height, horizontal: true,
                               wrongInputTexelSize: wrongInputTexelSize)
            current = filtered(current, width: width, height: height, horizontal: false,
                               wrongInputTexelSize: wrongInputTexelSize)
        }
        var result = [UInt8](); result.reserveCapacity(outputWidth * outputHeight * 4)
        for y in 0..<outputHeight {
            for x in 0..<outputWidth {
                let u = (Float(x) + 0.5) / Float(outputWidth)
                let v = (Float(y) + 0.5) / Float(outputHeight)
                // Independent final top-left API contract, not a call to helper's ScaleBias.
                let color = sample(current, u: u, v: wrongFinalFlip ? 1 - v : v)
                result += [sRGBByte(color.x), sRGBByte(color.y), sRGBByte(color.z), 255]
            }
        }
        return result
    }
    private static func pattern(width: Int, height: Int, constant: Bool) -> Raster {
        var pixels: [SIMD3<Float>] = []
        for y in 0..<height {
            for x in 0..<width {
                if constant { pixels.append(SIMD3(0.125, 0.5, 1)); continue }
                var color = SIMD3<Float>(0.03 + 0.75 * Float(x) / Float(width - 1),
                    0.02 + 0.85 * Float(y) / Float(height - 1),
                    (x > width / 5 && x < width / 2 && y < height * 2 / 5) ? 0.9 : 0.02)
                // Bright asymmetric patch independently exercises per-tap threshold,
                // rather than clipping a completed blurred pixel at the end.
                if x > width / 3 && x < width / 2 && y > height / 5 && y < height / 3 {
                    color = SIMD3(6, 1.5, 3)
                }
                pixels.append(color)
            }
        }
        return Raster(width: width, height: height, pixels: pixels)
    }
    private static func read(device: MTLDevice, queue: MTLCommandQueue, helper: HUDSourceFrostedGlass,
                             input: Raster, outputWidth: Int, outputHeight: Int) throws -> [UInt8] {
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba32Float,
            width: input.width, height: input.height, mipmapped: false)
        descriptor.storageMode = .shared; descriptor.usage = .shaderRead
        guard let texture = device.makeTexture(descriptor: descriptor), let command = queue.makeCommandBuffer() else {
            throw HUDSourceError.invalid("Cannot allocate synthetic FrostedGlass input")
        }
        let floats = input.pixels.flatMap { [$0.x, $0.y, $0.z, Float(1)] }
        floats.withUnsafeBytes { texture.replace(region: MTLRegionMake2D(0, 0, input.width, input.height), mipmapLevel: 0,
            withBytes: $0.baseAddress!, bytesPerRow: input.width * 16) }
        lastStageReadbacks = []
        let output = try helper.encode(command: command, input: texture,
            outputSize: CGSize(width: CGFloat(outputWidth), height: CGFloat(outputHeight)), passObserver: { name, stage in
                try require(stage.pixelFormat == .rg11b10Float, "FrostedGlass intermediate format differs")
                let stride = ((stage.width * 4 + 255) / 256) * 256
                guard let buffer = device.makeBuffer(length: stride * stage.height, options: .storageModeShared),
                      let blit = command.makeBlitCommandEncoder() else {
                    throw HUDSourceError.invalid("Cannot allocate original FrostedGlass stage readback")
                }
                blit.copy(from: stage, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
                    sourceSize: MTLSize(width: stage.width, height: stage.height, depth: 1), to: buffer,
                    destinationOffset: 0, destinationBytesPerRow: stride, destinationBytesPerImage: stride * stage.height)
                blit.endEncoding()
                lastStageReadbacks.append(StageReadback(name: name, width: stage.width, height: stage.height,
                    stride: stride, buffer: buffer))
            })
        try require(output.pixelFormat == .rgba8Unorm_srgb && output.mipmapLevelCount == 1 &&
            output.width == outputWidth && output.height == outputHeight, "Synthetic capture output format/size differs")
        let stride = ((outputWidth * 4 + 255) / 256) * 256
        guard let buffer = device.makeBuffer(length: stride * outputHeight, options: .storageModeShared),
              let blit = command.makeBlitCommandEncoder() else {
            throw HUDSourceError.invalid("Cannot allocate synthetic capture readback")
        }
        blit.copy(from: output, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
            sourceSize: MTLSize(width: outputWidth, height: outputHeight, depth: 1), to: buffer,
            destinationOffset: 0, destinationBytesPerRow: stride, destinationBytesPerImage: stride * outputHeight)
        blit.endEncoding(); command.commit(); command.waitUntilCompleted()
        if let error = command.error { throw error }
        try require(command.status == .completed, "Synthetic FrostedGlass GPU command did not complete")
        let pointer = buffer.contents().assumingMemoryBound(to: UInt8.self)
        var pixels: [UInt8] = []
        for y in 0..<outputHeight { pixels += Array(UnsafeBufferPointer(start: pointer + y * stride, count: outputWidth * 4)) }
        return pixels
    }
    private static func componentCode(_ value: Float, fractionBits: Int) -> UInt32 {
        guard value > 0 else { return 0 }
        let bits = value.bitPattern
        let exponent = Int((bits >> 23) & 255) - 127 + 15
        if exponent <= 0 {
            return UInt32((Double(value) / pow(2, Double(-14 - fractionBits))).rounded(.towardZero))
        }
        return UInt32(exponent << fractionBits) | ((bits & 0x7fffff) >> UInt32(23 - fractionBits))
    }
    private static func packedWord(_ color: SIMD3<Float>, rounding: AttachmentRounding) -> UInt32 {
        componentCode(unsignedFloat(color.x, mantissaBits: 6, rounding: rounding), fractionBits: 6) |
            (componentCode(unsignedFloat(color.y, mantissaBits: 6, rounding: rounding), fractionBits: 6) << 11) |
            (componentCode(unsignedFloat(color.z, mantissaBits: 5, rounding: rounding), fractionBits: 5) << 22)
    }
    /// A format-only experiment, separate from FrostedGlass. Original Blit
    /// outputs float32 samples without arithmetic. Pixel-center nearest sampling
    /// and a float32 render-target control isolate attachment conversion.
    private static func verifyAttachmentConversion(device: MTLDevice, root: URL) throws -> [String: Any] {
        let hdr = root.appendingPathComponent("HDR")
        let manifest = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: hdr.appendingPathComponent("FrostedGlass/manifest.json")))
        let copy = manifest["capture_copy"]
        func function(_ stage: HUDSourceJSONValue) throws -> MTLFunction {
            guard let file = stage["files"].array.first(where: { $0["path"].string?.hasSuffix(".metal") == true }),
                  let path = file["path"].string, !path.contains(".."), !path.hasPrefix("/"),
                  let hash = file["sha256"].string, let entry = stage["function"].string else {
                throw HUDSourceError.invalid("Attachment probe source module unavailable")
            }
            let bytes = try Data(contentsOf: hdr.appendingPathComponent(path))
            try require(SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined() == hash,
                "Attachment probe original Blit hash differs")
            guard let text = String(data: bytes, encoding: .utf8),
                  let result = try device.makeLibrary(source: text, options: nil).makeFunction(name: entry) else {
                throw HUDSourceError.invalid("Attachment probe original Blit function unavailable")
            }
            return result
        }
        let vertex = try function(copy["stages"]["vertex"]), fragment = try function(copy["stages"]["fragment"])
        let resources = copy["stages"]["fragment"]["resources"].array
        guard let textureIndex = resources.first(where: { $0["source_name"].string == "_BlitTexture" })?["msl_index"].number,
              let samplerIndex = resources.first(where: { $0["source_name"].string == "sampler_LinearClamp" })?["msl_index"].number,
              copy["stages"]["vertex"]["uniforms"].array.count == 1,
              copy["stages"]["fragment"]["uniforms"].array.count == 1,
              let vertexIndex = copy["stages"]["vertex"]["uniforms"].array[0]["msl_index"].number,
              let fragmentIndex = copy["stages"]["fragment"]["uniforms"].array[0]["msl_index"].number,
              let queue = device.makeCommandQueue(), let command = queue.makeCommandBuffer() else {
            throw HUDSourceError.invalid("Attachment probe named bindings unavailable")
        }
        var colors: [SIMD3<Float>] = []
        // Exact dyadic values, deliberately off midpoint boundaries. The last
        // two fractions distinguish direct RTZ from float16-before-RTZ.
        let fractions: [Float] = [0.015625, 0.125, 0.375, 0.625, 0.875, 0.984375, 0.9921875]
        for exponent in [-5, -3, -1, 0, 2] {
            let base = Float(pow(2.0, Double(exponent)))
            for mantissa in [1, 17, 41, 59] {
                for fraction in fractions {
                    colors.append(SIMD3(base * (1 + (Float(mantissa) + fraction) / 64),
                        base * (1 + (Float(63 - mantissa) + fraction) / 64),
                        base * (1 + (Float(mantissa % 31) + fraction) / 32)))
                }
            }
        }
        let width = colors.count, height = 2
        colors += colors.reversed().map { SIMD3($0.y, $0.z, $0.x) }
        let inputDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba32Float,
            width: width, height: height, mipmapped: false)
        inputDescriptor.storageMode = .shared; inputDescriptor.usage = .shaderRead
        guard let input = device.makeTexture(descriptor: inputDescriptor) else {
            throw HUDSourceError.invalid("Attachment probe float32 input unavailable")
        }
        let floats = colors.flatMap { [$0.x, $0.y, $0.z, Float(1)] }
        floats.withUnsafeBytes { input.replace(region: MTLRegionMake2D(0, 0, width, height), mipmapLevel: 0,
            withBytes: $0.baseAddress!, bytesPerRow: width * 16) }
        let samplerDescriptor = MTLSamplerDescriptor()
        samplerDescriptor.minFilter = .nearest; samplerDescriptor.magFilter = .nearest
        samplerDescriptor.mipFilter = .notMipmapped
        samplerDescriptor.sAddressMode = .clampToEdge; samplerDescriptor.tAddressMode = .clampToEdge
        guard let sampler = device.makeSamplerState(descriptor: samplerDescriptor) else {
            throw HUDSourceError.invalid("Attachment probe nearest sampler unavailable")
        }
        var constants = Data(repeating: 0, count: 64)
        [Float(1), -1, 0, 1].withUnsafeBytes { constants.replaceSubrange(0..<16, with: $0) }
        var outputs: [(MTLPixelFormat, MTLBuffer, Int)] = []
        for (format, bytesPerPixel) in [(MTLPixelFormat.rgba32Float, 16), (.rg11b10Float, 4)] {
            let pipelineDescriptor = MTLRenderPipelineDescriptor()
            pipelineDescriptor.vertexFunction = vertex; pipelineDescriptor.fragmentFunction = fragment
            pipelineDescriptor.colorAttachments[0].pixelFormat = format
            var reflection: MTLRenderPipelineReflection?
            let pipeline = try device.makeRenderPipelineState(descriptor: pipelineDescriptor,
                options: .argumentInfo, reflection: &reflection)
            for (stage, arguments) in [(copy["stages"]["vertex"], reflection?.vertexArguments),
                                       (copy["stages"]["fragment"], reflection?.fragmentArguments)] {
                let uniform = stage["uniforms"].array[0]
                guard let slot = uniform["msl_index"].number,
                      let argument = arguments?.first(where: { $0.type == .buffer && $0.index == Int(slot) }),
                      let structure = argument.bufferStructType else {
                    throw HUDSourceError.invalid("Attachment probe reflection unavailable")
                }
                try require(argument.bufferDataSize >= 52 && argument.bufferDataSize <= 64,
                    "Attachment probe source constant size differs")
                for member in uniform["members"].array {
                    try require(structure.members.first(where: { $0.name == member["name"].string })?.offset ==
                        Int(member["offset"].number ?? -1), "Attachment probe source member offset differs")
                }
            }
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: format,
                width: width, height: height, mipmapped: false)
            descriptor.storageMode = .private; descriptor.usage = .renderTarget
            let stride = ((width * bytesPerPixel + 255) / 256) * 256
            guard let texture = device.makeTexture(descriptor: descriptor),
                  let buffer = device.makeBuffer(length: stride * height, options: .storageModeShared) else {
                throw HUDSourceError.invalid("Attachment probe targets unavailable")
            }
            let pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = texture
            pass.colorAttachments[0].loadAction = .dontCare; pass.colorAttachments[0].storeAction = .store
            guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else {
                throw HUDSourceError.invalid("Attachment probe encoder unavailable")
            }
            encoder.setRenderPipelineState(pipeline); encoder.setCullMode(.none)
            constants.withUnsafeBytes {
                encoder.setVertexBytes($0.baseAddress!, length: $0.count, index: Int(vertexIndex))
                encoder.setFragmentBytes($0.baseAddress!, length: $0.count, index: Int(fragmentIndex))
            }
            encoder.setFragmentTexture(input, index: Int(textureIndex))
            encoder.setFragmentSamplerState(sampler, index: Int(samplerIndex))
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
            encoder.endEncoding()
            guard let blit = command.makeBlitCommandEncoder() else {
                throw HUDSourceError.invalid("Attachment probe readback unavailable")
            }
            blit.copy(from: texture, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
                sourceSize: MTLSize(width: width, height: height, depth: 1), to: buffer,
                destinationOffset: 0, destinationBytesPerRow: stride, destinationBytesPerImage: stride * height)
            blit.endEncoding(); outputs.append((format, buffer, stride))
        }
        command.commit(); command.waitUntilCompleted()
        if let error = command.error { throw error }
        try require(command.status == .completed, "Attachment probe did not complete")
        let control = Data(bytes: outputs[0].1.contents(), count: outputs[0].2 * height)
        let packed = Data(bytes: outputs[1].1.contents(), count: outputs[1].2 * height)
        var identityFailures = 0, mismatches = Dictionary(uniqueKeysWithValues: AttachmentRounding.allCases.map { ($0, 0) })
        var samples: [[String: Any]] = []
        for index in colors.indices {
            let y = index / width, x = index % width
            let actual = word(packed, y * outputs[1].2 + x * 4)
            let value = colors[index]
            for channel in 0..<4 {
                if word(control, y * outputs[0].2 + x * 16 + channel * 4) != floats[index * 4 + channel].bitPattern {
                    identityFailures += 1
                }
            }
            var predictions: [String: Any] = [:]
            for mode in AttachmentRounding.allCases {
                let prediction = packedWord(value, rounding: mode)
                predictions[mode.rawValue] = Int(prediction)
                if prediction != actual { mismatches[mode, default: 0] += 1 }
            }
            samples.append(["x": x, "y": y, "inputRGB": [value.x, value.y, value.z],
                "actualWord": Int(actual), "predictedWords": predictions])
        }
        let matches = AttachmentRounding.allCases.filter { mismatches[$0] == 0 }
        formatProgress = ["name": "original-copy-format-only", "shaderProgram": 1,
            "input": "Exact positive-normal dyadic float32 values; 280 pixels; no filter or blend",
            "sampler": "Nearest, pixel centers; diagnostic-only mapping",
            "float32IdentityFailures": identityFailures,
            "candidateMismatchPixels": Dictionary(uniqueKeysWithValues: mismatches.map { ($0.key.rawValue, $0.value) }),
            "recognizedModes": matches.map(\.rawValue), "samples": samples,
            "boundary": "Empirical attachment conversion on this device; not a claim about source Windows driver or MSL texture-write rounding",
            "passed": identityFailures == 0 && matches.count == 1]
        try require(identityFailures == 0, "Format-only source copy failed float32 bitwise identity control")
        try require(matches.count == 1, "Format-only HDR conversion did not uniquely match an independently specified rounding mode")
        attachmentRounding = matches[0]
        return formatProgress
    }
    private static func verifyStages(input: Raster, caseName: String) throws -> [[String: Any]] {
        var reports: [[String: Any]] = [], actualInput = input
        try require(lastStageReadbacks.count == 6, "Original FrostedGlass did not expose six diagnostic intermediates")
        for stage in lastStageReadbacks {
            let bytes = Data(bytes: stage.buffer.contents(), count: stage.stride * stage.height)
            let expected = filtered(actualInput, width: stage.width, height: stage.height,
                horizontal: stage.name.hasSuffix("horizontal"), wrongInputTexelSize: false)
            var actual = Raster(width: stage.width, height: stage.height,
                pixels: Array(repeating: .zero, count: stage.width * stage.height))
            var maxCodeError = 0, failures = 0, samples: [[String: Any]] = []
            for y in 0..<stage.height {
                for x in 0..<stage.width {
                    let packed = word(bytes, y * stage.stride + x * 4)
                    let codes = [packed & 2047, (packed >> 11) & 2047, (packed >> 22) & 1023]
                    let value = SIMD3(packedComponent(codes[0], fractionBits: 6),
                        packedComponent(codes[1], fractionBits: 6), packedComponent(codes[2], fractionBits: 5))
                    actual.pixels[y * stage.width + x] = value
                    var expectedCodes: [Int] = []
                    for channel in 0..<3 {
                        let code = componentCode(expected[x, y][channel], fractionBits: channel == 2 ? 5 : 6)
                        expectedCodes.append(Int(code))
                        let error = abs(Int(codes[channel]) - Int(code))
                        maxCodeError = max(maxCodeError, error)
                        // Single-pass, actual-input comparison: one adjacent code
                        // permits a fixed-function interpolation/float32 boundary.
                        // It cannot accumulate six-pass biased rounding errors.
                        if error > 1 || !value[channel].isFinite { failures += 1 }
                    }
                    if x == 0 || (x == stage.width / 2 && y == stage.height / 2) ||
                        (x == stage.width - 1 && y == stage.height - 1) {
                        samples.append(["x": x, "y": y, "actualCodes": codes.map(Int.init),
                            "expectedCodes": expectedCodes])
                    }
                }
            }
            let file = "\(caseName)-\(stage.name).rg11b10.bin"
            if let diagnosticOutput { try bytes.write(to: diagnosticOutput.appendingPathComponent(file)) }
            reports.append(["stage": stage.name, "size": [stage.width, stage.height],
                "cpuInput": stage.name == "level0.horizontal" ? "Original float32 fixture" : "Previous actual GPU intermediate",
                "attachmentRounding": attachmentRounding.rawValue, "maximumComponentCodeError": maxCodeError,
                "toleranceAdjacentCodes": 1, "failureCount": failures, "passed": failures == 0,
                "readback": file, "rowBytes": stage.stride,
                "sha256": SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined(), "samples": samples])
            actualInput = actual
        }
        return reports
    }
    private static func verifyFrosted(device: MTLDevice, root: URL) throws -> [[String: Any]] {
        guard let queue = device.makeCommandQueue() else { throw HUDSourceError.invalid("Synthetic GPU queue unavailable") }
        let helper = try HUDSourceFrostedGlass(device: device, resourceRoot: root, mode: .desktopDisplayPixels)
        var rejectedScene = false
        do { _ = try HUDSourceFrostedGlass(device: device, resourceRoot: root, mode: .sourceScene) }
        catch { rejectedScene = true }
        try require(rejectedScene, "Missing original scene LUT was silently bypassed")
        var reports: [[String: Any]] = []
        for (name, width, height, outWidth, outHeight, constant) in [
            ("odd-asymmetric-threshold", 131, 77, 159, 93, false),
            ("odd-nonsquare-second-scale", 59, 35, 91, 57, false),
            ("constant-exact-powers", 63, 41, 63, 41, true)] {
            let input = pattern(width: width, height: height, constant: constant)
            let expected = oracle(input, outputWidth: outWidth, outputHeight: outHeight)
            let actual = try read(device: device, queue: queue, helper: helper, input: input, outputWidth: outWidth, outputHeight: outHeight)
            let stages = try verifyStages(input: input, caseName: name)
            try require(actual.count == expected.count, "FrostedGlass GPU byte count differs")
            var maximum = 0, square = 0.0, failures = 0
            // Four bytes allow two R/G 6-bit and B 5-bit intermediate rounding
            // boundaries, fixed-function bilinear precision and final sRGB UNORM.
            // Alpha is separately required to be exact, not given this tolerance.
            let tolerance = constant ? 1 : 4
            for index in actual.indices {
                let error = abs(Int(actual[index]) - Int(expected[index]))
                maximum = max(maximum, error); square += Double(error * error)
                if index % 4 == 3 { if actual[index] != 255 { failures += 1 } }
                else if error > tolerance { failures += 1 }
            }
            var controls: [String: Any] = [:]
            if !constant {
                let wrongSize = oracle(input, outputWidth: outWidth, outputHeight: outHeight, wrongInputTexelSize: true)
                let wrongFlip = oracle(input, outputWidth: outWidth, outputHeight: outHeight, wrongFinalFlip: true)
                let sizeError = zip(expected, wrongSize).map { abs(Int($0.0) - Int($0.1)) }.max() ?? 0
                let flipError = zip(expected, wrongFlip).map { abs(Int($0.0) - Int($0.1)) }.max() ?? 0
                try require(sizeError > tolerance * 2 && flipError > tolerance * 2,
                    "Synthetic oracle fails to distinguish target texel size or final row order")
                controls = ["wrongInputTexelSizeMaximumDifference": sizeError, "wrongFinalYFlipMaximumDifference": flipError]
            }
            var samples: [[String: Any]] = []
            for sy in 0..<5 {
                for sx in 0..<5 {
                    let x = (outWidth - 1) * sx / 4, y = (outHeight - 1) * sy / 4, offset = (y * outWidth + x) * 4
                    samples.append(["x": x, "y": y, "expectedRGBA": Array(expected[offset..<offset + 4]).map(Int.init),
                        "actualRGBA": Array(actual[offset..<offset + 4]).map(Int.init)])
                }
            }
            reports.append(["name": name, "input": [width, height], "output": [outWidth, outHeight],
                "levelSizes": [4, 8, 16].map { [(width + $0 - 1) / $0, (height + $0 - 1) / $0] },
                "comparedBytes": actual.count, "maximumByteError": maximum,
                "rmsByteError": sqrt(square / Double(actual.count)), "toleranceRGBBytes": tolerance,
                "alphaMustEqual": 255, "failureCount": failures, "controls": controls, "samples": samples,
                "attachmentRounding": attachmentRounding.rawValue, "stages": stages,
                "passed": failures == 0 && stages.allSatisfy { $0["passed"] as? Bool == true }])
            frostedProgress = reports
            try require(stages.allSatisfy { $0["passed"] as? Bool == true }, "Independent FrostedGlass per-pass oracle failed \(name)")
            try require(failures == 0, "Independent FrostedGlass oracle failed \(name): \(failures) bytes, max error \(maximum)")
        }
        return reports
    }

    private static func image(width: Int, height: Int, space: CGColorSpace,
                              top: [UInt8], bottom: [UInt8]) throws -> CGImage {
        var data = Data(); data.reserveCapacity(width * height * 4)
        for y in 0..<height { for _ in 0..<width { data.append(contentsOf: (y < height / 2 ? top : bottom) + [255]) } }
        guard let provider = CGDataProvider(data: data as CFData),
              let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                  bytesPerRow: width * 4, space: space,
                  bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue | CGBitmapInfo.byteOrder32Big.rawValue),
                  provider: provider, decode: nil, shouldInterpolate: false, intent: .relativeColorimetric) else {
            throw HUDSourceError.invalid("Cannot create synthetic ICC tile")
        }
        return image
    }
    private static func tile(_ image: CGImage, id: UInt32, rect: CGRect, scale: CGFloat) -> HUDSourceDesktopBackdrop.Tile {
        HUDSourceDesktopBackdrop.Tile(displayID: id, image: image, requestedAppKitRect: rect,
            requestedCGGlobalRect: CGRect(x: rect.minX, y: -rect.maxY, width: rect.width, height: rect.height),
            capturedAppKitRect: rect, capturedCGGlobalRect: CGRect(x: rect.minX, y: -rect.maxY, width: rect.width, height: rect.height),
            sourceRect: CGRect(origin: .zero, size: rect.size), pointPixelScale: scale)
    }
    private static func verifyCompositeTiles(device: MTLDevice) throws -> [[String: Any]] {
        guard let sRGB = CGColorSpace(name: CGColorSpace.sRGB), let p3 = CGColorSpace(name: CGColorSpace.displayP3),
              let icc = sRGB.copyICCData(), let reconstructedRGB = CGColorSpace(iccData: icc),
              let queue = device.makeCommandQueue() else { throw HUDSourceError.invalid("Synthetic color profiles unavailable") }
        // A profile may have no standard name after ICC reconstruction. Its actual
        // RGB model is accepted; untagged/unsupported models must throw explicitly.
        _ = try HUDSourceWatchBackdrop.requireDesktopRGBProfile(reconstructedRGB)
        for (name, profile) in [("nil", Optional<CGColorSpace>.none),
                                ("gray", Optional(CGColorSpaceCreateDeviceGray()))] {
            var rejected = false
            do { _ = try HUDSourceWatchBackdrop.requireDesktopRGBProfile(profile) }
            catch { rejected = true }
            try require(rejected, "Unsupported synthetic desktop profile was accepted: " + name)
        }
        let rect = CGRect(x: -73, y: 24, width: 64, height: 48)
        let top: [UInt8] = [32, 200, 64], bottom: [UInt8] = [220, 40, 24]
        let singleImage = try image(width: 128, height: 96, space: sRGB, top: top, bottom: bottom)
        let reconstructedImage = try image(width: 128, height: 96, space: reconstructedRGB, top: top, bottom: bottom)
        let left = CGRect(x: rect.minX, y: rect.minY, width: 32, height: 48)
        let right = CGRect(x: rect.minX + 32, y: rect.minY, width: 32, height: 48)
        let leftImage = try image(width: 64, height: 96, space: sRGB, top: top, bottom: bottom)
        // sRGB and Display-P3 share the white point and transfer for neutral RGB.
        // Neutral 128 is an independent ICC invariant, not a second CGContext oracle.
        let rightImage = try image(width: 32, height: 48, space: p3, top: [128, 128, 128], bottom: [128, 128, 128])
        var reports: [[String: Any]] = []
        for (name, tiles) in [("single-retina-negative-origin", [tile(singleImage, id: 101, rect: rect, scale: 2)]),
            ("two-ICC-tiles-mixed-native-scales", [tile(leftImage, id: 101, rect: left, scale: 2),
                                                   tile(rightImage, id: 202, rect: right, scale: 1)]),
            ("reconstructed-RGB-ICC-profile", [tile(reconstructedImage, id: 303, rect: rect, scale: 2)])] {
            let frame = HUDSourceDesktopBackdrop.Frame(requestedRect: rect, hudWindowID: 999, excludedWindowIDs: [],
                tiles: tiles, captureStartUptime: 0, captureEndUptime: 0)
            let composite = try HUDSourceWatchBackdrop.compositeDesktop(frame: frame,
                drawableSize: CGSize(width: 128, height: 96), sourceMaterialID: "synthetic-source-material")
            var loaderDiagnostic: [String: Any] = [:]
            do {
                let automatic = try MTKTextureLoader(device: device).newTexture(cgImage: composite.image,
                    options: [.origin: MTKTextureLoader.Origin.topLeft.rawValue, .SRGB: NSNumber(value: true),
                        .generateMipmaps: NSNumber(value: false), .textureUsage: NSNumber(value: MTLTextureUsage.shaderRead.rawValue)])
                loaderDiagnostic = ["returnedPixelFormat": String(describing: automatic.pixelFormat),
                    "returnedPixelFormatRawValue": automatic.pixelFormat.rawValue,
                    "width": automatic.width, "height": automatic.height, "mipmapLevelCount": automatic.mipmapLevelCount,
                    "acceptedByPreviousGuard": automatic.pixelFormat == .rgba8Unorm_srgb || automatic.pixelFormat == .bgra8Unorm_srgb]
            } catch { loaderDiagnostic = ["error": String(describing: error)] }
            let reportData = try JSONEncoder().encode(composite.report)
            let geometryReport = try JSONSerialization.jsonObject(with: reportData)
            var current: [String: Any] = ["name": name, "passed": false, "legacyAutomaticLoaderDiagnostic": loaderDiagnostic,
                "image": ["bitsPerComponent": composite.image.bitsPerComponent, "bitsPerPixel": composite.image.bitsPerPixel,
                    "bytesPerRow": composite.image.bytesPerRow, "bitmapInfoRawValue": composite.image.bitmapInfo.rawValue,
                    "alphaInfoRawValue": composite.image.alphaInfo.rawValue,
                    "colorSpaceName": composite.image.colorSpace?.name.map { $0 as String } ?? "unnamed",
                    "colorSpaceModel": composite.image.colorSpace.map { Int($0.model.rawValue) } ?? -1],
                "geometryReport": geometryReport,
                "profileContract": "Explicit RGB profile required; nil/gray rejected; RGB ICC reconstruction accepted without a name whitelist",
                "upload": "Explicit RGBA8 sRGB byte upload; previous loader format is diagnostic only"]
            tileProgress = reports + [current]
            let texture = try HUDSourceWatchBackdrop.uploadDesktopComposite(composite, device: device)
            current["uploadedPixelFormat"] = String(describing: texture.pixelFormat)
            current["uploadedPixelFormatRawValue"] = texture.pixelFormat.rawValue
            tileProgress = reports + [current]
            try require(texture.pixelFormat == .rgba8Unorm_srgb && texture.mipmapLevelCount == 1,
                "Explicit synthetic composite upload changed its RGBA8 sRGB format")
            let stride = 512
            guard let buffer = device.makeBuffer(length: stride * 96, options: .storageModeShared),
                  let command = queue.makeCommandBuffer(), let blit = command.makeBlitCommandEncoder() else {
                throw HUDSourceError.invalid("Synthetic tile readback unavailable")
            }
            blit.copy(from: texture, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
                sourceSize: MTLSize(width: 128, height: 96, depth: 1), to: buffer, destinationOffset: 0,
                destinationBytesPerRow: stride, destinationBytesPerImage: stride * 96)
            blit.endEncoding(); command.commit(); command.waitUntilCompleted()
            if let error = command.error { throw error }
            try require(command.status == .completed, "Synthetic tile GPU readback incomplete")
            let ptr = buffer.contents().assumingMemoryBound(to: UInt8.self)
            var uploadByteMismatches = 0
            for y in 0..<96 { for x in 0..<512 {
                if ptr[y * stride + x] != composite.rgba8[y * composite.bytesPerRow + x] { uploadByteMismatches += 1 }
            } }
            current["uploadComparedBytes"] = composite.rgba8.count
            current["uploadByteMismatches"] = uploadByteMismatches
            tileProgress = reports + [current]
            try require(uploadByteMismatches == 0, "Explicit ICC composite GPU upload changed its original RGBA byte rows")
            var samples: [[String: Any]] = []
            for y in [12, 36, 60, 84] {
                for x in [16, 48, 80, 112] {
                    let expected = tiles.count == 2 && x >= 64 ? [128, 128, 128] : (y < 48 ? top : bottom).map(Int.init)
                    let offset = y * stride + x * 4
                    let actual = texture.pixelFormat == .bgra8Unorm_srgb ? [Int(ptr[offset + 2]), Int(ptr[offset + 1]), Int(ptr[offset])] :
                        [Int(ptr[offset]), Int(ptr[offset + 1]), Int(ptr[offset + 2])]
                    samples.append(["x": x, "y": y, "expectedRGB": expected, "actualRGB": actual, "actualAlpha": Int(ptr[offset + 3])])
                    current["samples"] = samples
                    tileProgress = reports + [current]
                    try require(zip(expected, actual).allSatisfy { abs($0.0 - $0.1) <= 2 } && ptr[offset + 3] == 255,
                        "Synthetic tile origin/ICC/crop mismatch \(name) at \(x),\(y): expected \(expected), actual \(actual)")
                }
            }
            current["passed"] = true
            current["coverage"] = "Whole tiles, mixed native scales, negative AppKit origin, sRGB/P3 neutral ICC invariant; no actual display data"
            reports.append(current)
            tileProgress = reports
        }
        return reports
    }

    private static func linear(_ byte: UInt8) -> Float {
        let value = Double(byte) / 255
        return Float(value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4))
    }
    private static func packedComponent(_ word: UInt32, fractionBits: Int) -> Float {
        let fraction = word & ((1 << fractionBits) - 1), exponent = Int(word >> fractionBits)
        if exponent == 0 { return Float(Double(fraction) * pow(2, Double(1 - 15 - fractionBits))) }
        if exponent == 31 { return fraction == 0 ? .infinity : .nan }
        return Float((1 + Double(fraction) / Double(1 << fractionBits)) * pow(2, Double(exponent - 15)))
    }
    private static func verifyOriginalSidecarParsing(root: URL) throws -> [String: Any] {
        let supplied = #"{"id":9223372036854775806,"positive":Infinity,"negative":-Infinity,"nan":NaN,"literal":"Infinity -Infinity NaN","escaped":"quote\" Infinity","items":[Infinity,-Infinity,NaN]}"#
        let parsed = try HUDSourceWatchBackdrop.originalSidecarObject(from: Data(supplied.utf8))
        try require((parsed["id"] as? NSNumber)?.stringValue == "9223372036854775806" &&
            parsed["positive"] as? String == "Infinity" && parsed["negative"] as? String == "-Infinity" &&
            parsed["nan"] as? String == "NaN" && parsed["literal"] as? String == "Infinity -Infinity NaN" &&
            parsed["escaped"] as? String == "quote\" Infinity" &&
            parsed["items"] as? [String] == ["Infinity", "-Infinity", "NaN"],
            "Original sidecar normalization changed text, integer IDs or non-finite value identity")
        for malformed in [#"{"value":InfinitySuffix}"#, #"{Infinity:1}"#, #"{"value":1Infinity}"#, #"{"value":"unterminated}"#] {
            var rejected = false
            do { _ = try HUDSourceWatchBackdrop.originalSidecarObject(from: Data(malformed.utf8)) }
            catch { rejected = true }
            try require(rejected, "Original sidecar normalization made malformed JSON valid")
        }
        let file = root.appendingPathComponent("HDR/WatchBlur/scene.json")
        let bytes = try Data(contentsOf: file)
        let scene = try HUDSourceWatchBackdrop.originalSidecarObject(from: bytes)
        guard let objects = scene["objects"] as? [[String: Any]] else {
            throw HUDSourceError.invalid("Original WatchBlur complete scene objects unavailable")
        }
        func nonFiniteCount(_ value: Any) -> Int {
            if let string = value as? String { return ["Infinity", "-Infinity", "NaN"].contains(string) ? 1 : 0 }
            if let array = value as? [Any] { return array.reduce(0) { $0 + nonFiniteCount($1) } }
            if let object = value as? [String: Any] { return object.values.reduce(0) { $0 + nonFiniteCount($1) } }
            return 0
        }
        let count = nonFiniteCount(scene)
        let clipNames = objects.filter { $0["type"] as? String == "AnimationClip" }.compactMap {
            ($0["data"] as? [String: Any])?["m_Name"] as? String
        }.sorted()
        try require(count == 6 && objects.count == 23 && clipNames == ["watch_blur_in", "watch_blur_out"],
            "Original WatchBlur sidecar lost its complete animation/non-finite evidence")
        let result: [String: Any] = ["passed": true, "syntheticIdentityChecks": true,
            "malformedControlsRejected": 4, "originalSceneSHA256": SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined(),
            "originalObjectCount": objects.count, "preservedNonFiniteValues": count,
            "originalClipNames": clipNames,
            "contract": "Quote only bare non-finite value tokens outside strings, then strictly parse the complete object; no resource rewrite or clip exclusion"]
        sidecarProgress = result
        return result
    }
    private static func word(_ bytes: Data, _ offset: Int) -> UInt32 {
        UInt32(bytes[offset]) | (UInt32(bytes[offset + 1]) << 8) |
            (UInt32(bytes[offset + 2]) << 16) | (UInt32(bytes[offset + 3]) << 24)
    }
    private static func verifyRawImage(root: URL) throws -> [[String: Any]] {
        guard let sRGB = CGColorSpace(name: CGColorSpace.sRGB), let p3 = CGColorSpace(name: CGColorSpace.displayP3) else {
            throw HUDSourceError.invalid("Synthetic RawImage profiles unavailable")
        }
        // Validate the independent stencil oracle against immutable source values,
        // before sampling the production renderer. Do not synthesize a stencil writer.
        let material = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: root.appendingPathComponent("HDR/WatchBlur/material-runtime.json")))
        let passes = material["static_pass_states"].array
        try require(passes.map { $0["name"].string ?? "" } == ["Default", "Default-Stencil-Alpha-Blend"] &&
            passes.map { $0["state"]["stencilRef"]["value"].number ?? -1 } == [0, 32] &&
            passes.allSatisfy { $0["state"]["stencilOp"]["comp"]["value"].number == 3 &&
                $0["state"]["stencilOp"]["pass"]["value"].number == 0 }, "Original RawImage two-pass stencil fixture contract differs")
        let saved = material["serialized"]["m_SavedProperties"]["m_Floats"].array
        let stencilAlpha = saved.first { $0.array.first?.string == "_StencilAlpha" }?.array.last?.number
        try require(stencilAlpha.map { abs($0 - 0.15) < 0.000001 } == true,
                    "Original stencil-alpha coefficient differs")
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let viewport = CGRect(x: 0, y: 0, width: 128, height: 96)
        let renderer = try HUDSourceMetalRenderer(frame: viewport, resourceRoot: root, sceneColorMode: .sourceRGBHDR)
        renderer.autoResizeDrawable = false
        let backdrop = try HUDSourceWatchBackdrop(renderer: renderer, resourceRoot: root)
        let window = NSWindow(contentRect: viewport, styleMask: [.borderless], backing: .buffered, defer: false)
        window.isOpaque = false; window.backgroundColor = .clear; window.hasShadow = false
        window.contentView = renderer; window.orderFront(nil)
        defer { window.orderOut(nil) }
        renderer.drawableSize = CGSize(width: 128, height: 96)
        app.finishLaunching(); window.displayIfNeeded()
        let camera = HUDSourceMetalRenderer.Camera(viewProjection: matrix_identity_float4x4,
            viewNoTranslationProjection: matrix_identity_float4x4, worldSpacePosition: .zero,
            timeSeconds: 0, renderPathInjected: 0, flipX: 0, flipY: 0,
            projection: matrix_identity_float4x4, inverseView: matrix_identity_float4x4,
            uiProjectionParameters: SIMD4(1, 0.3, 200, 0.005))
        let rect = CGRect(x: -73, y: 24, width: 64, height: 48)
        let top: [UInt8] = [32, 200, 64], bottom: [UInt8] = [220, 40, 24]
        let one = try image(width: 128, height: 96, space: sRGB, top: top, bottom: bottom)
        let leftRect = CGRect(x: rect.minX, y: rect.minY, width: 32, height: 48)
        let rightRect = CGRect(x: rect.minX + 32, y: rect.minY, width: 32, height: 48)
        let left = try image(width: 64, height: 96, space: sRGB, top: top, bottom: bottom)
        let right = try image(width: 32, height: 48, space: p3, top: [128, 128, 128], bottom: [128, 128, 128])
        var reports: [[String: Any]] = []
        for (name, tiles) in [("single-synthetic-raw-background", [tile(one, id: 101, rect: rect, scale: 2)]),
            ("two-synthetic-ICC-raw-background", [tile(left, id: 101, rect: leftRect, scale: 2),
                                                    tile(right, id: 202, rect: rightRect, scale: 1)])] {
            let frame = HUDSourceDesktopBackdrop.Frame(requestedRect: rect, hudWindowID: 999, excludedWindowIDs: [],
                tiles: tiles, captureStartUptime: 0, captureEndUptime: 0)
            try backdrop.prepare(frame: frame, drawableSize: CGSize(width: 128, height: 96))
            // Specify the fixture's expected display pixel raster directly; do not
            // derive expected colors from compositeDesktop or production texture data.
            var input = Raster(width: 128, height: 96, pixels: [])
            for y in 0..<96 {
                for x in 0..<128 {
                    let bytes = tiles.count == 2 && x >= 64 ? [UInt8(128), 128, 128] : (y < 48 ? top : bottom)
                    input.pixels.append(SIMD3(linear(bytes[0]), linear(bytes[1]), linear(bytes[2])))
                }
            }
            let capture = oracle(input, outputWidth: 128, outputHeight: 96)
            let tint = linear(76)
            for (alpha, stencil) in [(Float(1), UInt32(0)), (Float(0.5), 0), (Float(0), 0), (Float(0.5), 32)] {
                renderer.clearStencil = stencil
                guard let batch = backdrop.batch(alpha: alpha) else {
                    throw HUDSourceError.invalid("Prepared synthetic RawImage batch unavailable")
                }
                renderer.submit(camera: camera, batches: [batch]); renderer.draw()
                _ = try renderer.copyDrawableImage() // Reads this fixture's Metal drawable only.
                let raw = try renderer.copySceneColorReadback()
                guard let final = renderer.drawableReadbackBGRA, let finalReport = renderer.drawableReadbackReport else {
                    throw HUDSourceError.invalid("Synthetic RawImage final GPU pixels unavailable")
                }
                try require(raw.width == 128 && raw.height == 96 && finalReport.width == 128 && finalReport.height == 96 &&
                    raw.pixelFormat == "rg11b10Float" && renderer.diagnostics.isEmpty,
                    "Synthetic RawImage draw size/format/diagnostics differ")
                let graphicAlpha = (alpha * 255).rounded(.toNearestOrEven) / 255
                // clear0: Equal0 Default draws, Equal32 second pass rejects.
                // Explicit counterfactual clear32: Default rejects; second pass
                // alone draws alpha*sourceStencilAlpha. This tests both source
                // passes without asserting the normal game clears stencil32.
                let contribution = graphicAlpha * (stencil == 32 ? Float(stencilAlpha!) : 1)
                var maxRawError: Float = 0, maxFinalError = 0, failures = 0
                var samples: [[String: Any]] = []
                for y in 0..<96 {
                    for x in 0..<128 {
                        let ci = (y * 128 + x) * 4
                        let expected = rgbHDR(SIMD3(linear(capture[ci]), linear(capture[ci + 1]), linear(capture[ci + 2])) *
                            tint * contribution)
                        let packed = word(raw.data, y * raw.rowBytes + x * 4)
                        let actual = SIMD3(packedComponent(packed & 2047, fractionBits: 6),
                            packedComponent((packed >> 11) & 2047, fractionBits: 6),
                            packedComponent((packed >> 22) & 1023, fractionBits: 5))
                        let fi = y * finalReport.rowBytes + x * 4
                        let expectedFinal = [Int(sRGBByte(expected.z)), Int(sRGBByte(expected.y)), Int(sRGBByte(expected.x)), 255]
                        let actualFinal = (0..<4).map { Int(final[fi + $0]) }
                        for channel in 0..<3 {
                            let error = abs(actual[channel] - expected[channel])
                            maxRawError = max(maxRawError, error)
                            // Final 11/11/10 rounding plus preceding filter/ICC precision.
                            if !actual[channel].isFinite || error > 0.0025 { failures += 1 }
                            let byteError = abs(actualFinal[channel] - expectedFinal[channel])
                            maxFinalError = max(maxFinalError, byteError)
                            if byteError > 4 { failures += 1 }
                        }
                        if actualFinal[3] != 255 { failures += 1 }
                        if [0, 31, 63, 95, 127].contains(x) && [0, 23, 47, 71, 95].contains(y) {
                            samples.append(["x": x, "y": y, "expectedLinearRGB": [expected.x, expected.y, expected.z],
                                "actualLinearRGB": [actual.x, actual.y, actual.z],
                                "expectedFinalBGRA": expectedFinal, "actualFinalBGRA": actualFinal])
                        }
                    }
                }
                reports.append(["name": name, "alpha": alpha, "clearStencil": Int(stencil),
                    "counterfactualStencil32": stencil == 32, "sourcePassCount": passes.count,
                    "sourceMaterial": batch.material, "sourceNode": batch.sourceNodeID ?? "",
                    "canvasGammaToLinearTint": tint, "graphicAlphaColor32": graphicAlpha,
                    "expectedContribution": contribution, "maximumRawRGBError": maxRawError,
                    "rawTolerance": 0.0025, "maximumFinalByteError": maxFinalError, "finalTolerance": 4,
                    "comparedPixels": 128 * 96, "failureCount": failures, "passed": failures == 0, "samples": samples])
                rawImageProgress = reports
                try require(failures == 0, "Synthetic RawImage HDR/tint/alpha/stencil/UV failed \(name) alpha\(alpha) stencil\(stencil): \(failures) checks, raw\(maxRawError), final\(maxFinalError)")
            }
        }
        return reports
    }

    static func main() throws {
        guard CommandLine.arguments.count == 2 else { throw HUDSourceError.invalid("Expected synthetic GPU output directory") }
        let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        diagnosticOutput = output
        var report: [String: Any] = ["schemaVersion": 1, "passed": false,
            "input": "Synthetic file-memory pixels only; no desktop/screen/permission APIs invoked",
            "oracle": "Independent CPU 9tap H / 5 bilinear-tap V; per-tap threshold; target-level texel size; two Y flips per level; unsigned RGB11/11/10 quantization after each pass; final top-left bilinear sRGB copy",
            "modeBoundary": "No-LUT desktop adapter is tested. Original dynamic scene LUT/compute mode is not claimed equivalent.",
            "rawImageTwoPassGPUVerified": false,
            "rawImageBoundary": "Synthetic original RawImage two-pass draw on the fixture's own drawable. No live game scene, screen capture or account state."]
        do {
            try HUDSourceDesktopBackdropPureChecks.runPureChecks()
            report["pureCoordinateChecksPassed"] = true
            guard let device = MTLCreateSystemDefaultDevice(), let root = HUDResources.url(for: "WatchSource") else {
                throw HUDSourceError.invalid("Synthetic Metal device/source resources unavailable")
            }
            report["device"] = device.name
            report["attachmentConversionProbe"] = try verifyAttachmentConversion(device: device, root: root)
            report["frostedTests"] = try verifyFrosted(device: device, root: root)
            report["tileTests"] = try verifyCompositeTiles(device: device)
            report["sourceSidecarParsing"] = try verifyOriginalSidecarParsing(root: root)
            report["rawImageTests"] = try verifyRawImage(root: root)
            report["rawImageTwoPassGPUVerified"] = true
            report["passed"] = true
            try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
                .write(to: output.appendingPathComponent("source-backdrop-gpu-regression.json"))
            print("Independent synthetic FrostedGlass, ICC/tile and two-pass RawImage HDR GPU checks passed")
        } catch {
            report["error"] = String(describing: error)
            if report["attachmentConversionProbe"] == nil { report["attachmentConversionProbe"] = formatProgress }
            if report["frostedTests"] == nil { report["frostedTests"] = frostedProgress }
            if report["tileTests"] == nil { report["tileTests"] = tileProgress }
            if report["sourceSidecarParsing"] == nil { report["sourceSidecarParsing"] = sidecarProgress }
            if report["rawImageTests"] == nil { report["rawImageTests"] = rawImageProgress }
            try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
                .write(to: output.appendingPathComponent("source-backdrop-gpu-regression.json"))
            throw error
        }
    }
}
