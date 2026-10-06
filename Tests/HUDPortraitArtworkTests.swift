import AppKit

enum HUDPortraitArtworkTests {
    private final class Lifetime { var released = false }

    /// A 16 MiB imported image whose real backing-storage release is observable,
    /// independent of Swift wrapper identity or autorelease timing.
    private static func image(lifetime: Lifetime, pixel: UInt32) -> CGImage {
        let side = 2048, bytes = side * side * 4
        let storage = calloc(bytes, 1)!
        storage.assumingMemoryBound(to: UInt32.self).initialize(repeating: pixel, count: side * side)
        let provider = CGDataProvider(dataInfo: Unmanaged.passRetained(lifetime).toOpaque(), data: storage,
            size: bytes, releaseData: { info, data, _ in
                Unmanaged<Lifetime>.fromOpaque(info!).takeRetainedValue().released = true
                free(UnsafeMutableRawPointer(mutating: data))
            })!
        return CGImage(width: side, height: side, bitsPerComponent: 8, bitsPerPixel: 32,
            bytesPerRow: side * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
            provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)!
    }

    private static func pixels(_ image: CGImage) -> Data {
        let context = CGContext(data: nil, width: image.width, height: image.height,
            bitsPerComponent: 8, bytesPerRow: image.width * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        return Data(bytes: context.data!, count: context.bytesPerRow * context.height)
    }

    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) {
            count += 1; if !value { fatalError(message) }
        }
        let firstLifetime = Lifetime()
        var retainedCrop: CGImage?
        var originalPixels = Data()
        weak var oldSource: CGImage?
        autoreleasepool {
            let source = image(lifetime: firstLifetime, pixel: 0xFF0000FF)
            oldSource = source
            retainedCrop = HUDPortraitArtwork.renderedImage(source, targetSize: CGSize(width: 74, height: 74),
                zoom: 20, offset: CGPoint(x: 0.2, y: -0.1), contentsScale: 2)
            guard let crop = retainedCrop else { fatalError("High-resolution portrait crop must render") }
            originalPixels = pixels(crop)
            let repeated = HUDPortraitArtwork.renderedImage(source, targetSize: CGSize(width: 74, height: 74),
                zoom: 20, offset: CGPoint(x: 0.2, y: -0.1), contentsScale: 2)
            check(repeated.map(pixels) == originalPixels, "Warm portrait crop retains exact pixels at 20x zoom")
            check(!firstLifetime.released, "Current imported portrait remains alive while its owner retains it")
        }
        check(oldSource == nil && firstLifetime.released,
            "Reset/replacement must release the full-resolution original even while its crop stays cached")
        check(retainedCrop.map(pixels) == originalPixels,
            "Releasing the source must not change or invalidate the displayed cropped image")
        let secondLifetime = Lifetime()
        autoreleasepool {
            let source = image(lifetime: secondLifetime, pixel: 0xFFFF0000)
            let replacement = HUDPortraitArtwork.renderedImage(source, targetSize: CGSize(width: 74, height: 74),
                zoom: 20, offset: CGPoint(x: 0.2, y: -0.1), contentsScale: 2)
            check(replacement != nil && replacement.map(pixels) != originalPixels,
                "A replacement avatar with the same crop settings never reuses the old avatar's pixels")
        }
        check(secondLifetime.released, "Replacement backing storage is not retained by the crop cache")
        let backgroundLifetime = Lifetime()
        autoreleasepool {
            let source = image(lifetime: backgroundLifetime, pixel: 0xFF00FFFF)
            let target = CGSize(width: 412, height: 158)
            let offset = CGPoint(x: 1, y: -1)
            let crop = HUDPortraitArtwork.crop(imageSize: CGSize(width: source.width, height: source.height),
                targetSize: target, zoom: 20, offset: offset)
            check(abs(crop.maxX - 1) < 0.000001 && crop.minY == 0
                  && crop.width > 0 && crop.height > 0 && crop.width <= 0.05,
                  "Thumbnail extreme positions and20x zoom crop within the existing image")
            let thumbnail = HUDPortraitArtwork.renderedImage(source, targetSize: target,
                zoom: 20, offset: offset, contentsScale: 2)
            check(thumbnail != nil && thumbnail!.width <= 1024 && thumbnail!.height <= 1024,
                  "Zoomed source-card thumbnails remain bounded display bitmaps")
            let warm = HUDPortraitArtwork.renderedImage(source, targetSize: target,
                zoom: 20, offset: offset, contentsScale: 2)
            check(warm === thumbnail, "Unchanged thumbnail geometry reuses its cached crop instead of rasterizing again")
        }
        check(backgroundLifetime.released, "Cached thumbnail crops do not retain an old background image")
        return count
    }
}
