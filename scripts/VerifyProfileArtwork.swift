import AppKit
import CoreImage
import MetalKit

/// Offscreen proof of the production profile upload through the original UI
/// fragment shader. No window, screen capture, profile files or user data.
@main
struct VerifyProfileArtwork {
    static func main() throws {
        guard CommandLine.arguments.count == 2, let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue() else { fatalError("Expected WatchSource resource root and Metal device") }
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        let fragment = try String(contentsOf: root.appendingPathComponent("Shaders/HGRP_UI_Default-ss0-p18-seg0-program194.fragment.metal"), encoding: .utf8)
            .replacingOccurrences(of: "fragment main0_out main0(", with: "fragment main0_out profileFragment(")
        let vertex = """
        struct ProfileProbeVertex {
            float4 position [[position]];
            float4 color [[user(locn0)]];
            float2 uv [[user(locn1)]];
        };
        vertex ProfileProbeVertex profileVertex(uint id [[vertex_id]]) {
            float2 p[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };
            return { float4(p[id],0,1), float4(1), float2(.5) };
        }
        """
        let library = try device.makeLibrary(source: fragment + vertex, options: nil)
        let pipeline = MTLRenderPipelineDescriptor()
        pipeline.vertexFunction = library.makeFunction(name: "profileVertex")
        pipeline.fragmentFunction = library.makeFunction(name: "profileFragment")
        pipeline.colorAttachments[0].pixelFormat = .rgba8Unorm_srgb
        let state = try device.makeRenderPipelineState(descriptor: pipeline)
        let sampler = device.makeSamplerState(descriptor: MTLSamplerDescriptor())!
        let perDraw = device.makeBuffer(length: 144, options: .storageModeShared)!
        let perMaterial = device.makeBuffer(length: 196, options: .storageModeShared)!
        memset(perDraw.contents(), 0, 144); memset(perMaterial.contents(), 0, 196)
        let outputDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm_srgb, width: 1, height: 1, mipmapped: false)
        outputDescriptor.usage = [.renderTarget, .shaderRead]
        let output = device.makeTexture(descriptor: outputDescriptor)!
        func render(_ texture: MTLTexture) -> [UInt8] {
            let pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = output; pass.colorAttachments[0].loadAction = .clear
            pass.colorAttachments[0].storeAction = .store
            let command = queue.makeCommandBuffer()!, encoder = command.makeRenderCommandEncoder(descriptor: pass)!
            encoder.setRenderPipelineState(state)
            encoder.setFragmentBuffer(perDraw, offset: 0, index: 0)
            encoder.setFragmentBuffer(perMaterial, offset: 0, index: 1)
            encoder.setFragmentTexture(texture, index: 0); encoder.setFragmentSamplerState(sampler, index: 0)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3); encoder.endEncoding()
            if output.storageMode == .managed { let blit = command.makeBlitCommandEncoder()!; blit.synchronize(resource: output); blit.endEncoding() }
            command.commit(); command.waitUntilCompleted()
            precondition(command.status == .completed, "Original source shader failed: \(String(describing: command.error))")
            var bytes = [UInt8](repeating: 0, count: 4)
            output.getBytes(&bytes, bytesPerRow: 4, from: MTLRegionMake2D(0, 0, 1, 1), mipmapLevel: 0)
            return bytes
        }
        func image(_ rgba: [UInt8]) -> CGImage {
            CGImage(width: 1, height: 1, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: 4,
                space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
                provider: CGDataProvider(data: Data(rgba) as CFData)!, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
        }
        let context = CIContext(options: [.cacheIntermediates: false])
        var passed = 0
        for rgb: [UInt8] in [[0, 32, 64], [64, 128, 192], [128, 128, 128], [255, 255, 255], [120, 20, 200]] {
            let rgba = rgb + [255], original = image(rgba)
            let cropped = context.createCGImage(CIImage(cgImage: original), from: CGRect(x: 0, y: 0, width: 1, height: 1))!
            for input in [original, cropped] {
                let texture = try HUDSourceProfileArtwork.makeTexture(input, device: device)
                precondition(texture.pixelFormat == .rgba8Unorm_srgb)
                let result = render(texture)
                precondition(zip(rgba, result).allSatisfy { abs(Int($0) - Int($1)) <= 1 }, "Avatar color changed: \(rgba) → \(result)")
                passed += 1
            }
        }
        let diagnostic = image([128, 128, 128, 255])
        let previous = try MTKTextureLoader(device: device).newTexture(cgImage: diagnostic, options: [.SRGB: true])
        print("Previous CGImage loader: format \(previous.pixelFormat.rawValue), gray128 → \(render(previous)[0])")
        let transparent = try HUDSourceProfileArtwork.makeTexture(image([128, 128, 128, 128]), device: device)
        let sample = render(transparent)
        precondition(abs(Int(sample[0]) - 93) <= 1 && sample[3] == 128, "Source alpha must premultiply once in linear light")
        passed += 1
        let authoredBytes = try Data(contentsOf: root.appendingPathComponent("Textures/business_card_topic_normal_1--4a226705---4827637915678035611.bgra-mips.bin"))
        let authored = try HUDSourceProfileArtwork.backgroundArtwork(bgra: authoredBytes, textureWidth: 532, textureHeight: 204,
            spriteRect: CGRect(x: 0, y: 0, width: 530, height: 204))
        // At native sprite size, every outer stripe/corner must survive the
        // same color-management/upload conversion as its unmodified source.
        let photoContext = CGContext(data: nil, width: 530, height: 204, bitsPerComponent: 8, bytesPerRow: 530 * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        photoContext.draw(image([160, 200, 240, 255]), in: CGRect(x: 0, y: 0, width: 530, height: 204))
        let background = try HUDSourceProfileArtwork.compositedBackground(photoContext.makeImage()!, artwork: authored)
        let originalPixels = Array(try HUDSourceProfileArtwork.texturePixels(authored).rgba)
        let compositedPixels = Array(try HUDSourceProfileArtwork.texturePixels(background).rgba)
        var decorationPixels = 0
        for y in 0..<204 { for x in 0..<530 {
            let i = (y * 530 + x) * 4
            precondition(originalPixels[i + 3] == compositedPixels[i + 3], "Custom background changed the card silhouette")
            if x < 19 || x > 508 || y < 21 || y > 183 {
                precondition((0..<4).allSatisfy { abs(Int(originalPixels[i + $0]) - Int(compositedPixels[i + $0])) <= 2 },
                    "Custom background erased authored border decoration")
                if originalPixels[i + 3] > 0 { decorationPixels += 1 }
            }
        } }
        precondition(decorationPixels > 500, "The proof must exercise visible original decoration")
        print("Preserved authored outer decoration: \(decorationPixels) visible pixels; all alpha pixels unchanged")
        passed += 1
        print("Profile upload through original fragment shader: \(passed) passed; gray128 → 128")
    }
}
