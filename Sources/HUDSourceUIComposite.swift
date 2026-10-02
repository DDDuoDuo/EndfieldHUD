import Foundation
import Metal

/// Original UberPost_CompositeUI pass 1, base variant 726. Optional volume
/// keywords require live source state and are deliberately not selected here.
final class HUDSourceUIComposite {
    private let pipeline: MTLRenderPipelineState
    private let sampler: MTLSamplerState
    private let constantsIndex: Int
    private let textureIndex: Int
    private let samplerIndex: Int
    private let outputPixelFormat: MTLPixelFormat

    init(device: MTLDevice, resourceRoot: URL, outputPixelFormat: MTLPixelFormat) throws {
        let root = resourceRoot.appendingPathComponent("HDR")
        let manifest = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: HUDSourceResourceData.read(root.appendingPathComponent("Composite/manifest.json")))
        guard manifest["pass_index"].number == 1,
              manifest["pass_name"].string == "UberPost_CompositeUI",
              let variant = manifest["variants"].array.first(where: { $0["program"].number == 726 }),
              variant["keywords"].array.isEmpty,
              manifest["runtime_contract"]["hdr_target"]["graphics_format"].number == 74 else {
            throw HUDSourceError.invalid("Original base UI composite contract unavailable")
        }
        let vertex = variant["stages"]["vertex"], fragment = variant["stages"]["fragment"]
        func function(_ stage: HUDSourceJSONValue) throws -> MTLFunction {
            guard let file = stage["files"].array.first(where: { $0["path"].string?.hasSuffix(".metal") == true })?["path"].string,
                  let name = stage["function"].string, !file.contains("..") else {
                throw HUDSourceError.invalid("Original UI composite stage unavailable")
            }
            let library = try device.makeLibrary(source: String(contentsOf: root.appendingPathComponent(file), encoding: .utf8), options: nil)
            guard let function = library.makeFunction(name: name) else {
                throw HUDSourceError.invalid("Original UI composite entry point unavailable")
            }
            return function
        }
        guard let constants = vertex["uniforms"].array.first,
              vertex["uniforms"].array.count == 1,
              constants["name"].string == "_PerPassConstants", constants["size"].number == 32,
              constants["fields"].array.first(where: { $0["name"].string == "_HGFlipX" })?["offset"].number == 4,
              constants["fields"].array.first(where: { $0["name"].string == "_HGFlipY" })?["offset"].number == 8,
              let constantSlot = constants["msl_index"].number,
              let inputSlot = fragment["resources"].array.first(where: { $0["source_name"].string == "_InputTexture" })?["msl_index"].number,
              let samplerSlot = fragment["resources"].array.first(where: { $0["source_name"].string == "sampler_LinearClamp" })?["msl_index"].number else {
            throw HUDSourceError.invalid("Original UI composite bindings differ")
        }
        let blend = manifest["state"]["rtBlend0"]
        guard blend["srcBlend"]["val"].number == 5, blend["destBlend"]["val"].number == 10,
              blend["srcBlendAlpha"]["val"].number == 5, blend["destBlendAlpha"]["val"].number == 10,
              blend["blendOp"]["val"].number == 0, blend["blendOpAlpha"]["val"].number == 0,
              blend["colMask"]["val"].number == 15 else {
            throw HUDSourceError.invalid("Original UI composite blend state differs")
        }
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.label = "Source UberPost_CompositeUI 726"
        descriptor.vertexFunction = try function(vertex)
        descriptor.fragmentFunction = try function(fragment)
        let color = descriptor.colorAttachments[0]!
        color.pixelFormat = outputPixelFormat
        color.isBlendingEnabled = true
        color.sourceRGBBlendFactor = .sourceAlpha
        color.destinationRGBBlendFactor = .oneMinusSourceAlpha
        color.sourceAlphaBlendFactor = .sourceAlpha
        color.destinationAlphaBlendFactor = .oneMinusSourceAlpha
        color.rgbBlendOperation = .add
        color.alphaBlendOperation = .add
        color.writeMask = .all
        var reflection: MTLRenderPipelineReflection?
        pipeline = try device.makeRenderPipelineState(descriptor: descriptor, options: .argumentInfo, reflection: &reflection)
        guard reflection?.vertexArguments?.first(where: { $0.type == .buffer && $0.index == Int(constantSlot) })?.bufferDataSize == 32 else {
            throw HUDSourceError.invalid("Compiled UI composite constant-buffer ABI differs")
        }
        // Platform mapping of the separately named sampler_LinearClamp. Its
        // shader uses explicit LOD0; no mip generation or anisotropic preset.
        let sampling = MTLSamplerDescriptor()
        sampling.minFilter = .linear; sampling.magFilter = .linear
        sampling.mipFilter = .notMipmapped
        sampling.sAddressMode = .clampToEdge; sampling.tAddressMode = .clampToEdge
        sampling.rAddressMode = .clampToEdge
        guard let sampler = device.makeSamplerState(descriptor: sampling) else {
            throw HUDSourceError.invalid("Cannot allocate UI composite sampler")
        }
        self.sampler = sampler
        self.constantsIndex = Int(constantSlot)
        self.textureIndex = Int(inputSlot)
        self.samplerIndex = Int(samplerSlot)
        self.outputPixelFormat = outputPixelFormat
    }

    /// The caller initializes the final attachment. The original pass loads
    /// that attachment; it does not clear it. Flip values describe this final
    /// RT adapter, independently of the earlier UI3D camera pass.
    func encode(command: MTLCommandBuffer, input: MTLTexture, destination: MTLTexture,
                flipX: Float = 0, flipY: Float = 0) throws {
        guard destination.pixelFormat == outputPixelFormat,
              input.width > 0, input.height > 0,
              [Float(0), 1].contains(flipX), [Float(0), 1].contains(flipY) else {
            throw HUDSourceError.invalid("Invalid UI composite target or orientation")
        }
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = destination
        pass.colorAttachments[0].loadAction = .load
        pass.colorAttachments[0].storeAction = .store
        guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else {
            throw HUDSourceError.invalid("Cannot encode original UI composite")
        }
        encoder.label = "Source UberPost_CompositeUI 726"
        encoder.setRenderPipelineState(pipeline)
        encoder.setCullMode(.none)
        var constants: [Float] = [0, flipX, flipY, 0, 0, 0, 0, 0]
        constants.withUnsafeMutableBytes { encoder.setVertexBytes($0.baseAddress!, length: $0.count, index: constantsIndex) }
        encoder.setFragmentTexture(input, index: textureIndex)
        encoder.setFragmentSamplerState(sampler, index: samplerIndex)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3, instanceCount: 1)
        encoder.endEncoding()
    }
}
