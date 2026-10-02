import Foundation
import CryptoKit
import Metal

/// Original FrostedGlass PS programs and original capture-copy program.
/// Desktop display pixels already contain display grading. Omitting the scene
/// LUT is an explicit input adapter, not the game's ordinary scene-color path.
final class HUDSourceFrostedGlass {
    enum Mode: Equatable {
        case desktopDisplayPixels
        case sourceScene
    }

    private struct Uniform {
        let name: String
        let index: Int
        let byteCount: Int
        let fields: [String: Int]
    }
    private struct Pass {
        let pipeline: MTLRenderPipelineState
        let vertexUniforms: [Uniform]
        let fragmentUniforms: [Uniform]
        let textureIndex: Int
        let samplerIndex: Int
    }
    private let device: MTLDevice
    private let horizontal: Pass
    private let vertical: Pass
    private let copy: Pass
    private let linearClamp: MTLSamplerState
    let mode: Mode

    init(device: MTLDevice, resourceRoot: URL, mode: Mode = .desktopDisplayPixels) throws {
        guard mode == .desktopDisplayPixels else {
            throw HUDSourceError.invalid("Source-scene FrostedGlass requires original dynamic exposure, log LUT and volume gates")
        }
        let root = resourceRoot.appendingPathComponent("HDR")
        let manifest = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: HUDSourceResourceData.read(root.appendingPathComponent("FrostedGlass/manifest.json")))
        let contract = manifest["source_contract"]
        guard contract["scale_factors"].array.compactMap(\.number) == [0.25, 0.125, 0.0625],
              contract["graphics_format"].number == 74,
              contract["color_threshold"].number == 2.5,
              contract["returned_level"].number == 2,
              contract["capture_graphics_format"].number == 4,
              contract["capture_filter"].number == 0, contract["capture_wrap"].number == 0,
              contract["capture_mip_count"].number == 1,
              contract["desktop_copy_scale_bias"].array.compactMap(\.number) == [1, -1, 0, 1],
              let h = manifest["variants"].array.first(where: { $0["program"].number == 3 }),
              let v = manifest["variants"].array.first(where: { $0["program"].number == 8 }),
              h["pass_index"].number == 0, v["pass_index"].number == 1,
              h["keywords"].array.isEmpty, v["keywords"].array.isEmpty else {
            throw HUDSourceError.invalid("Original desktop FrostedGlass PS contract differs")
        }
        let c = manifest["capture_copy"]
        guard c["program"].number == 1, c["pass_index"].number == 1,
              c["keywords"].array.isEmpty, c["source_shader"]["name"].string == "Hidden/HGRP/Blit" else {
            throw HUDSourceError.invalid("Original capture-copy contract differs")
        }
        horizontal = try Self.makePass(device: device, root: root, record: h, format: .rg11b10Float,
            textureName: "_InputTexture", samplerName: "s_linear_clamp_sampler", label: "Source FrostedGlass PS3 horizontal")
        vertical = try Self.makePass(device: device, root: root, record: v, format: .rg11b10Float,
            textureName: "_InputTexture", samplerName: "s_linear_clamp_sampler", label: "Source FrostedGlass PS8 vertical")
        copy = try Self.makePass(device: device, root: root, record: c, format: .rgba8Unorm_srgb,
            textureName: "_BlitTexture", samplerName: "sampler_LinearClamp", label: "Source extraction Blit pass1")
        for pass in [horizontal, vertical] {
            guard pass.vertexUniforms.isEmpty, pass.fragmentUniforms.count == 2,
                  pass.fragmentUniforms.first(where: { $0.name == "ShaderVariablesGlobal" })?.fields["_GlobalMipBias"] == 416,
                  pass.fragmentUniforms.first(where: { $0.name == "$Globals" })?.fields["_TexelSize"] == 0,
                  pass.fragmentUniforms.first(where: { $0.name == "$Globals" })?.fields["_ColorThreshold"] == 16 else {
                throw HUDSourceError.invalid("Original FrostedGlass uniform offsets differ")
            }
        }
        guard copy.vertexUniforms.count == 1, copy.fragmentUniforms.count == 1,
              copy.vertexUniforms[0].fields["_BlitScaleBias"] == 0,
              copy.fragmentUniforms[0].fields["_BlitMipLevel"] == 32 else {
            throw HUDSourceError.invalid("Original extraction uniform offsets differ")
        }
        // Explicit Metal mapping of the source separately named inline sampler.
        // Source targets have one mip. Native packed sampler flags remain in
        // the manifest; this is not a claim about unobserved driver presets.
        let sampling = MTLSamplerDescriptor()
        sampling.minFilter = .linear; sampling.magFilter = .linear
        sampling.mipFilter = .notMipmapped
        sampling.sAddressMode = .clampToEdge; sampling.tAddressMode = .clampToEdge
        sampling.rAddressMode = .clampToEdge
        guard let sampler = device.makeSamplerState(descriptor: sampling) else {
            throw HUDSourceError.invalid("Cannot allocate FrostedGlass source sampler mapping")
        }
        linearClamp = sampler
        self.device = device
        self.mode = mode
    }

    /// Encodes three H/V levels from the supplied input dimensions, then the
    /// original bilinear extraction pass into a full-size, single-mip sRGB RT.
    /// It does not capture the desktop or observe a game camera's live state.
    func encode(command: MTLCommandBuffer, input: MTLTexture, outputSize: CGSize,
                passObserver: ((String, MTLTexture) throws -> Void)? = nil) throws -> MTLTexture {
        guard input.textureType == .type2D, input.sampleCount == 1,
              input.width > 0, input.height > 0, input.mipmapLevelCount == 1,
              outputSize.width.isFinite, outputSize.height.isFinite,
              outputSize.width >= 1, outputSize.height >= 1,
              outputSize.width.rounded(.towardZero) == outputSize.width,
              outputSize.height.rounded(.towardZero) == outputSize.height,
              outputSize.width <= CGFloat(Int.max), outputSize.height <= CGFloat(Int.max) else {
            throw HUDSourceError.invalid("Invalid FrostedGlass input or capture dimensions")
        }
        var current = input
        for (level, factor) in [0.25, 0.125, 0.0625].enumerated() {
            let width = Int(ceil(Double(input.width) * factor))
            let height = Int(ceil(Double(input.height) * factor))
            let temp = try texture(width: width, height: height, format: .rg11b10Float, label: "FrostedGlass horizontal")
            let result = try texture(width: width, height: height, format: .rg11b10Float, label: "FrostedGlass vertical")
            try draw(command: command, pass: horizontal, input: current, output: temp, copyPass: false)
            // Optional regression readback hook. Observers encode their own
            // copies on this command; production callers leave it nil.
            try passObserver?("level\(level).horizontal", temp)
            try draw(command: command, pass: vertical, input: temp, output: result, copyPass: false)
            try passObserver?("level\(level).vertical", result)
            current = result
        }
        let capture = try texture(width: Int(outputSize.width), height: Int(outputSize.height),
            format: .rgba8Unorm_srgb, label: "UIBlurRT desktop-display capture")
        try draw(command: command, pass: copy, input: current, output: capture, copyPass: true)
        return capture
    }

    private func texture(width: Int, height: Int, format: MTLPixelFormat, label: String) throws -> MTLTexture {
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: format,
            width: width, height: height, mipmapped: false)
        descriptor.storageMode = .private
        descriptor.usage = [.shaderRead, .renderTarget]
        guard let texture = device.makeTexture(descriptor: descriptor) else {
            throw HUDSourceError.invalid("Cannot allocate source FrostedGlass target")
        }
        texture.label = label
        return texture
    }

    private func draw(command: MTLCommandBuffer, pass: Pass, input: MTLTexture, output: MTLTexture, copyPass: Bool) throws {
        let descriptor = MTLRenderPassDescriptor()
        descriptor.colorAttachments[0].texture = output
        descriptor.colorAttachments[0].loadAction = .dontCare
        descriptor.colorAttachments[0].storeAction = .store
        guard let encoder = command.makeRenderCommandEncoder(descriptor: descriptor) else {
            throw HUDSourceError.invalid("Cannot encode original FrostedGlass pass")
        }
        encoder.label = copyPass ? "Original capture-copy pass1" : "Original FrostedGlass PS H/V"
        encoder.setRenderPipelineState(pass.pipeline)
        encoder.setCullMode(.none)
        func data(_ uniform: Uniform) throws -> Data {
            var bytes = Data(repeating: 0, count: uniform.byteCount)
            func write(_ field: String, _ values: [Float]) throws {
                guard let offset = uniform.fields[field], offset >= 0,
                      offset + values.count * MemoryLayout<Float>.stride <= bytes.count else {
                    throw HUDSourceError.invalid("FrostedGlass named constant field unavailable")
                }
                values.withUnsafeBytes { raw in bytes.replaceSubrange(offset..<(offset + raw.count), with: raw) }
            }
            if copyPass {
                // The untouched source Vulkan fullscreen vertex negates Y
                // and emits UV.y=1-v. Six H/V passes flip twice per level.
                // The final seventh draw uses this top-left Metal RT adapter
                // to preserve the desktop input's row order. This value is
                // an API adapter, not an inferred game-driver live constant.
                try write("_BlitScaleBias", [1, -1, 0, 1])
                try write("_BlitMipLevel", [0])
            } else if uniform.name == "$Globals" {
                let width = Float(output.width), height = Float(output.height)
                try write("_TexelSize", [width, height, 1 / width, 1 / height])
                try write("_ColorThreshold", [2.5, 0, 0, 0])
            } else {
                // Display-pixel input and original single-mip intermediate RTs.
                try write("_GlobalMipBias", [0])
            }
            return bytes
        }
        do {
            for uniform in pass.vertexUniforms {
                let bytes = try data(uniform)
                bytes.withUnsafeBytes { encoder.setVertexBytes($0.baseAddress!, length: $0.count, index: uniform.index) }
            }
            for uniform in pass.fragmentUniforms {
                let bytes = try data(uniform)
                bytes.withUnsafeBytes { encoder.setFragmentBytes($0.baseAddress!, length: $0.count, index: uniform.index) }
            }
            encoder.setFragmentTexture(input, index: pass.textureIndex)
            encoder.setFragmentSamplerState(linearClamp, index: pass.samplerIndex)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3, instanceCount: 1)
            encoder.endEncoding()
        } catch {
            encoder.endEncoding()
            throw error
        }
    }

    private static func makePass(device: MTLDevice, root: URL, record: HUDSourceJSONValue,
                                 format: MTLPixelFormat, textureName: String, samplerName: String,
                                 label: String) throws -> Pass {
        func function(_ stage: HUDSourceJSONValue) throws -> MTLFunction {
            guard let file = stage["files"].array.first(where: { $0["path"].string?.hasSuffix(".metal") == true }),
                  let path = file["path"].string, !path.contains(".."), !path.hasPrefix("/"),
                  let expected = file["sha256"].string, let name = stage["function"].string else {
                throw HUDSourceError.invalid("Original FrostedGlass module unavailable")
            }
            let bytes = try HUDSourceResourceData.read(root.appendingPathComponent(path))
            let digest = SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
            guard digest == expected, let source = String(data: bytes, encoding: .utf8) else {
                throw HUDSourceError.invalid("Original FrostedGlass module hash differs")
            }
            let library = try device.makeLibrary(source: source, options: nil)
            guard let function = library.makeFunction(name: name) else {
                throw HUDSourceError.invalid("Original FrostedGlass entry point unavailable")
            }
            return function
        }
        let vertex = record["stages"]["vertex"], fragment = record["stages"]["fragment"]
        let state = record["state"], blend = state["rtBlend0"]
        guard blend["srcBlend"]["val"].number == 1, blend["destBlend"]["val"].number == 0,
              blend["srcBlendAlpha"]["val"].number == 1, blend["destBlendAlpha"]["val"].number == 0,
              blend["colMask"]["val"].number == 15,
              state["culling"]["val"].number == 0, state["zWrite"]["val"].number == 0,
              let textureSlot = fragment["resources"].array.first(where: {
                  $0["source_name"].string == textureName && $0["category"].string == "separate_images"
              })?["msl_index"].number,
              let samplerSlot = fragment["resources"].array.first(where: {
                  $0["source_name"].string == samplerName && $0["category"].string == "separate_samplers"
              })?["msl_index"].number else {
            throw HUDSourceError.invalid("Original FrostedGlass pass state or named binding differs")
        }
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.label = label
        descriptor.vertexFunction = try function(vertex)
        descriptor.fragmentFunction = try function(fragment)
        descriptor.colorAttachments[0].pixelFormat = format
        descriptor.colorAttachments[0].isBlendingEnabled = false
        descriptor.colorAttachments[0].writeMask = .all
        var reflection: MTLRenderPipelineReflection?
        let pipeline = try device.makeRenderPipelineState(descriptor: descriptor, options: .argumentInfo, reflection: &reflection)
        guard let reflection else { throw HUDSourceError.invalid("Compiled FrostedGlass reflection unavailable") }
        func uniforms(_ stage: HUDSourceJSONValue, _ arguments: [MTLArgument]?) throws -> [Uniform] {
            try stage["uniforms"].array.map { source in
                guard let name = source["name"].string, let slot = source["msl_index"].number,
                      let size = source["size"].number,
                      let argument = arguments?.first(where: { $0.type == .buffer && $0.index == Int(slot) }),
                      let structure = argument.bufferStructType,
                      argument.bufferDataSize >= Int(size),
                      argument.bufferDataSize <= ((Int(size) + 15) / 16) * 16 else {
                    throw HUDSourceError.invalid("Compiled FrostedGlass constant-buffer size differs")
                }
                // Metal may add only tail alignment padding (e.g. original
                // Blit block 52 bytes). Every original member offset must match.
                for member in source["members"].array {
                    guard let memberName = member["name"].string, let offset = member["offset"].number,
                          structure.members.first(where: { $0.name == memberName })?.offset == Int(offset) else {
                        throw HUDSourceError.invalid("Compiled FrostedGlass member offset differs")
                    }
                }
                var fields: [String: Int] = [:]
                for field in source["fields"].array {
                    guard let key = field["name"].string, let offset = field["offset"].number,
                          fields.updateValue(Int(offset), forKey: key) == nil else {
                        throw HUDSourceError.invalid("Original FrostedGlass named field is invalid")
                    }
                }
                return Uniform(name: name, index: Int(slot), byteCount: argument.bufferDataSize, fields: fields)
            }
        }
        return Pass(pipeline: pipeline, vertexUniforms: try uniforms(vertex, reflection.vertexArguments),
            fragmentUniforms: try uniforms(fragment, reflection.fragmentArguments),
            textureIndex: Int(textureSlot), samplerIndex: Int(samplerSlot))
    }
}
