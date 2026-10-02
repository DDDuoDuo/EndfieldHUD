import AppKit
import ImageIO

enum HUDWatchArtworkTests {
    static func run() -> Int {
        var checks = 0
        func check(_ condition: Bool, _ message: String) { checks += 1; precondition(condition, message) }
        let color = NSColor(deviceRed: 0.2, green: 0.65, blue: 0.85, alpha: 1)
        for asset in HUDWatchArtwork.Asset.allCases {
            let expectedSize = asset == .midRing ? (1004, 1004) : (143, 123)
            guard let url = HUDResources.url(for: "Watch/\(asset.rawValue).png"),
                  let source = CGImageSourceCreateWithURL(url as CFURL, nil),
                  let original = CGImageSourceCreateImageAtIndex(source, 0, nil),
                  let tinted = HUDWatchArtwork.image(asset, tint: color),
                  let repeated = HUDWatchArtwork.image(asset, tint: color),
                  let translucent = HUDWatchArtwork.image(asset, tint: color.withAlphaComponent(0.5)),
                  let originalPixels = pixels(original), let tintedPixels = pixels(tinted),
                  let translucentPixels = pixels(translucent) else {
                preconditionFailure("Every Watch sprite must resolve and decode from the bundled/source resources")
            }
            check(original.width == expectedSize.0 && original.height == expectedSize.1,
                  "Watch resource dimensions match the inspected Sprite crop")
            check(tinted.width == original.width && tinted.height == original.height,
                  "Tinting keeps the source pixels and aspect ratio")
            check(tinted === repeated, "Repeated theme/control updates reuse the cached bitmap")
            check(tinted !== translucent, "Tint alpha is part of the cache key")

            var preservesAlpha = true, appliesColor = true, appliesTintAlpha = true
            var transparentCount = 0, visibleCount = 0, partialCount = 0
            for offset in stride(from: 0, to: originalPixels.count, by: 4) {
                let alpha = Int(originalPixels[offset + 3])
                let tintedAlpha = Int(tintedPixels[offset + 3])
                preservesAlpha = preservesAlpha && abs(tintedAlpha - alpha) <= 1
                appliesTintAlpha = appliesTintAlpha && abs(Int(translucentPixels[offset + 3]) - Int((Double(alpha) * 0.5).rounded())) <= 1
                if alpha == 0 { transparentCount += 1 }
                else if alpha < 255 { partialCount += 1 }
                if alpha >= 128 {
                    visibleCount += 1
                    let rgb = [color.redComponent, color.greenComponent, color.blueComponent]
                    for channel in 0..<3 {
                        let expected = Int((CGFloat(tintedAlpha) * rgb[channel]).rounded())
                        appliesColor = appliesColor && abs(Int(tintedPixels[offset + channel]) - expected) <= 2
                    }
                }
            }
            check(visibleCount > 0 && transparentCount > 0 && partialCount > 0,
                  "Both sprites retain visible detail, empty areas, and antialiased edges")
            check(preservesAlpha, "Theme tint preserves every source alpha pixel, including the triangle orientation and hollow regions")
            check(appliesColor, "Source colors are replaced by the requested theme color")
            check(appliesTintAlpha, "Theme opacity multiplies sprite alpha without filling transparent regions")
        }
        check(HUDResources.url(for: "Watch/SOURCES.json") != nil, "Watch source metadata ships beside the artwork")

        // Distinct small variants exercise the item limit independently of the
        // byte budget. A cache hit must protect that theme from the next eviction.
        let triangleColors = (0..<8).map { NSColor(deviceRed: 0.03 + CGFloat($0) * 0.07, green: 0.23, blue: 0.57, alpha: 1) }
        let triangleImages = triangleColors.compactMap { HUDWatchArtwork.image(.triangle, tint: $0) }
        check(triangleImages.count == 8, "Eight small theme variants can be rendered")
        check(HUDWatchArtwork.image(.triangle, tint: triangleColors[0]) === triangleImages[0],
              "A recently used small variant keeps its bitmap")
        let extraTriangle = HUDWatchArtwork.image(.triangle, tint: NSColor(deviceRed: 0.91, green: 0.23, blue: 0.57, alpha: 1))
        guard let recreatedTriangle = HUDWatchArtwork.image(.triangle, tint: triangleColors[1]),
              let previousTrianglePixels = pixels(triangleImages[1]),
              let recreatedTrianglePixels = pixels(recreatedTriangle) else {
            preconditionFailure("An evicted small theme must render again")
        }
        check(extraTriangle != nil && recreatedTriangle !== triangleImages[1]
              && HUDWatchArtwork.image(.triangle, tint: triangleColors[0]) === triangleImages[0],
              "The ninth small variant evicts the least recently used theme while retaining the refreshed one")
        check(previousTrianglePixels == recreatedTrianglePixels, "Eviction and re-rendering preserve the exact theme pixels")

        // Five full-size rings exceed 16 MiB even though they fit the item
        // count. This checks the byte budget through the same public API.
        let ringColors = (0..<4).map { NSColor(deviceRed: 0.09 + CGFloat($0) * 0.14, green: 0.32, blue: 0.71, alpha: 1) }
        let ringImages = ringColors.compactMap { HUDWatchArtwork.image(.midRing, tint: $0) }
        check(ringImages.count == 4, "Four native ring themes fit the byte budget")
        check(HUDWatchArtwork.image(.midRing, tint: ringColors[0]) === ringImages[0],
              "Using a ring theme protects it from the next byte-budget eviction")
        let extraRing = HUDWatchArtwork.image(.midRing, tint: NSColor(deviceRed: 0.87, green: 0.32, blue: 0.71, alpha: 1))
        guard let recreatedRing = HUDWatchArtwork.image(.midRing, tint: ringColors[1]),
              let previousRingPixels = pixels(ringImages[1]),
              let recreatedRingPixels = pixels(recreatedRing) else {
            preconditionFailure("An evicted native ring theme must render again")
        }
        check(extraRing != nil && recreatedRing !== ringImages[1]
              && HUDWatchArtwork.image(.midRing, tint: ringColors[0]) === ringImages[0],
              "The fifth native ring evicts an old ring before the item count reaches eight")
        check(previousRingPixels == recreatedRingPixels, "Byte-budget eviction retains the rendered artwork's appearance")
        return checks
    }

    private static func pixels(_ image: CGImage) -> [UInt8]? {
        let byteCount = image.width * image.height * 4
        guard let context = CGContext(data: nil, width: image.width, height: image.height,
            bitsPerComponent: 8, bytesPerRow: image.width * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue),
              let data = context.data else { return nil }
        context.setBlendMode(.copy)
        context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        return Array(UnsafeBufferPointer(start: data.assumingMemoryBound(to: UInt8.self), count: byteCount))
    }
}
