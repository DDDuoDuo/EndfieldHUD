import AppKit
import Metal

/// The portrait keeps its colors. The custom card photo is dimmed inside the
/// authored panel, while its outer decoration and alpha silhouette stay intact.
enum HUDSourceProfileArtwork {
    enum Failure: Error { case invalidMask, cannotRender }

    static func backgroundArtwork(bgra: Data, textureWidth: Int, textureHeight: Int,
                               spriteRect: CGRect) throws -> CGImage {
        guard textureWidth > 0, textureWidth <= 16384, textureHeight > 0, textureHeight <= 16384,
              [spriteRect.minX, spriteRect.minY, spriteRect.width, spriteRect.height].allSatisfy(\.isFinite),
              spriteRect.minX >= 0, spriteRect.minY >= 0, spriteRect.width > 0, spriteRect.height > 0,
              spriteRect.maxX <= CGFloat(textureWidth), spriteRect.maxY <= CGFloat(textureHeight) else { throw Failure.invalidMask }
        let x = Int(spriteRect.minX), y = Int(spriteRect.minY)
        let width = Int(spriteRect.width), height = Int(spriteRect.height)
        guard textureWidth > 0, textureHeight > 0, width > 0, height > 0,
              spriteRect == CGRect(x: x, y: y, width: width, height: height),
              x >= 0, y >= 0, x + width <= textureWidth, y + height <= textureHeight,
              bgra.count >= textureWidth * textureHeight * 4 else { throw Failure.invalidMask }
        var rgba = Data(repeating: 0, count: width * height * 4)
        // The decoded source mip has bottom-origin rows. CGImage uses top-origin
        // rows, just like the cropped user image produced by HUDPortraitArtwork.
        rgba.withUnsafeMutableBytes { target in
            bgra.withUnsafeBytes { source in
                let output = target.bindMemory(to: UInt8.self), input = source.bindMemory(to: UInt8.self)
                for row in 0..<height { for column in 0..<width {
                    let target = (row * width + column) * 4
                    let source = ((y + height - row - 1) * textureWidth + x + column) * 4
                    output[target] = input[source + 2]; output[target + 1] = input[source + 1]
                    output[target + 2] = input[source]; output[target + 3] = input[source + 3]
                } }
            }
        }
        guard let provider = CGDataProvider(data: rgba as CFData),
              let result = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                bytesPerRow: width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue), provider: provider,
                decode: nil, shouldInterpolate: true, intent: .defaultIntent) else { throw Failure.cannotRender }
        return result
    }

    static func compositedBackground(_ image: CGImage, artwork: CGImage) throws -> CGImage {
        let context = try bitmapContext(width: image.width, height: image.height)
        let photo = try bitmapContext(width: image.width, height: image.height)
        let bounds = CGRect(x: 0, y: 0, width: image.width, height: image.height)
        context.interpolationQuality = .high
        context.draw(artwork, in: bounds)
        photo.draw(image, in: bounds)
        photo.setBlendMode(.sourceAtop)
        photo.setFillColor(CGColor(gray: 0, alpha: 0.48))
        photo.fill(bounds)
        guard let darkened = photo.makeImage() else { throw Failure.cannotRender }
        // business_card_topic_normal_1: its inner rounded panel is (20,22) to
        // (507,182) in the 530×204 sprite. Keep the actual outer source pixels;
        // replacing the whole sprite erased the stripes, edge and corner cuts.
        let sx = bounds.width / 530, sy = bounds.height / 204
        let panel = CGRect(x: 20 * sx, y: 22 * sy, width: 487 * sx, height: 160 * sy)
        context.addPath(CGPath(roundedRect: panel, cornerWidth: 22 * sx, cornerHeight: 22 * sy, transform: nil))
        context.clip()
        context.setBlendMode(.sourceAtop) // Preserve the authored alpha even at rounded edges.
        context.draw(darkened, in: bounds)
        guard let result = context.makeImage() else { throw Failure.cannotRender }
        return result
    }

    /// The selected source card stores its yellow edge in the bitmap. Replace
    /// that chroma without tinting the neutral panel or a user's photograph.
    static func themedBackgroundArtwork(_ artwork: CGImage, accent: NSColor) throws -> CGImage {
        guard let accent = accent.usingColorSpace(.sRGB) else { throw Failure.cannotRender }
        let context = try bitmapContext(width: artwork.width, height: artwork.height)
        context.draw(artwork, in: CGRect(x: 0, y: 0, width: artwork.width, height: artwork.height))
        guard let data = context.data else { throw Failure.cannotRender }
        let pixels = data.assumingMemoryBound(to: UInt8.self)
        let tint = [accent.redComponent, accent.greenComponent, accent.blueComponent]
        for y in 0..<artwork.height { for x in 0..<artwork.width {
            let index = y * context.bytesPerRow + x * 4
            let yellow = max(0, min(Int(pixels[index]), Int(pixels[index + 1])) - Int(pixels[index + 2]))
            guard yellow > 0 else { continue }
            // Pixels are premultiplied; the chroma and neutral component are
            // already scaled by source alpha and must remain that way.
            for channel in 0..<3 {
                let neutral = Int(pixels[index + channel]) - (channel < 2 ? yellow : 0)
                pixels[index + channel] = UInt8(min(255, max(0, neutral + Int((CGFloat(yellow) * tint[channel]).rounded()))))
            }
        } }
        guard let result = context.makeImage() else { throw Failure.cannotRender }
        return result
    }

    /// A normal-alpha hover plate carries a 5% interior wash and a stronger
    /// outer edge. The source ColorTint controls its finite enter/exit fade.
    static func hoverArtwork(_ artwork: CGImage, accent: NSColor) throws -> CGImage {
        let context = try bitmapContext(width: artwork.width, height: artwork.height)
        let bounds = CGRect(x: 0, y: 0, width: artwork.width, height: artwork.height)
        context.draw(artwork, in: bounds)
        context.setBlendMode(.sourceIn)
        context.setFillColor(accent.withAlphaComponent(0.38).cgColor); context.fill(bounds)
        let sx = bounds.width / 530, sy = bounds.height / 204
        let panel = CGRect(x: 20 * sx, y: 22 * sy, width: 487 * sx, height: 160 * sy)
        context.addPath(CGPath(roundedRect: panel, cornerWidth: 22 * sx, cornerHeight: 22 * sy, transform: nil))
        context.clip()
        context.setBlendMode(.destinationIn)
        context.setFillColor(CGColor(gray: 1, alpha: 0.05 / 0.38)); context.fill(bounds)
        guard let result = context.makeImage() else { throw Failure.cannotRender }
        return result
    }

    struct TexturePixels {
        let width: Int
        let height: Int
        let rgba: Data
    }

    static func texturePixels(_ image: CGImage) throws -> TexturePixels {
        let context = try bitmapContext(width: image.width, height: image.height)
        context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        guard let base = context.data else { throw Failure.cannotRender }
        let input = base.assumingMemoryBound(to: UInt8.self)
        var data = Data(repeating: 0, count: image.width * image.height * 4)
        data.withUnsafeMutableBytes { raw in
            let output = raw.bindMemory(to: UInt8.self)
            for row in 0..<image.height { for column in 0..<image.width {
                let source = row * context.bytesPerRow + column * 4
                let target = ((image.height - row - 1) * image.width + column) * 4
                let alpha = Int(input[source + 3])
                // The source UI shader premultiplies sampled RGB by alpha.
                // Supply straight alpha so translucent artwork is multiplied once.
                for channel in 0..<3 {
                    output[target + channel] = alpha == 0 ? 0 : UInt8(min(255, (Int(input[source + channel]) * 255 + alpha / 2) / alpha))
                }
                output[target + 3] = UInt8(alpha)
            } }
        }
        return TexturePixels(width: image.width, height: image.height, rgba: data)
    }

    static func makeTexture(_ image: CGImage, device: MTLDevice) throws -> MTLTexture {
        let pixels = try texturePixels(image)
        // MTKTextureLoader's CGImage initializer can return rgba8Unorm despite
        // SRGB=true. Explicit sRGB storage prevents a second gamma encoding when
        // the source shader writes to its sRGB drawable (128 became about 188).
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm_srgb,
            width: pixels.width, height: pixels.height, mipmapped: false)
        descriptor.usage = .shaderRead
        // Keep the hardware default: shared on Apple, managed on Intel/AMD.
        guard let texture = device.makeTexture(descriptor: descriptor) else { throw Failure.cannotRender }
        pixels.rgba.withUnsafeBytes { raw in
            texture.replace(region: MTLRegionMake2D(0, 0, pixels.width, pixels.height), mipmapLevel: 0,
                withBytes: raw.baseAddress!, bytesPerRow: pixels.width * 4)
        }
        return texture
    }

    private static func bitmapContext(width: Int, height: Int) throws -> CGContext {
        guard width > 0, height > 0, width <= 16384, height <= 16384,
              let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { throw Failure.cannotRender }
        return context
    }
}
