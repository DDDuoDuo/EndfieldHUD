import AppKit

/// User artwork keeps its color and crop. Only the authored card's alpha
/// silhouette is applied; no game tint, glow or image filter is baked in.
enum HUDSourceProfileArtwork {
    enum Failure: Error { case invalidMask, cannotRender }

    static func backgroundMask(bgra: Data, textureWidth: Int, textureHeight: Int,
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
        var rgba = Data(repeating: 255, count: width * height * 4)
        // The decoded source mip has bottom-origin rows. CGImage uses top-origin
        // rows, just like the cropped user image produced by HUDPortraitArtwork.
        rgba.withUnsafeMutableBytes { target in
            bgra.withUnsafeBytes { source in
                let output = target.bindMemory(to: UInt8.self), input = source.bindMemory(to: UInt8.self)
                for row in 0..<height { for column in 0..<width {
                    output[(row * width + column) * 4 + 3] = input[((y + height - row - 1) * textureWidth + x + column) * 4 + 3]
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

    static func applyingBackgroundMask(_ mask: CGImage, to image: CGImage) throws -> CGImage {
        guard let context = CGContext(data: nil, width: image.width, height: image.height,
            bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { throw Failure.cannotRender }
        let bounds = CGRect(x: 0, y: 0, width: image.width, height: image.height)
        context.interpolationQuality = .high
        context.draw(image, in: bounds)
        context.setBlendMode(.destinationIn)
        context.draw(mask, in: bounds)
        guard let result = context.makeImage() else { throw Failure.cannotRender }
        return result
    }
}
