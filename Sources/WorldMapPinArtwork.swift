import AppKit
import ImageIO

/// Three shared tiny images; no atlas, scene renderer or added resource copy.
/// The beam receives its original material tint once before layer compositing.
enum WorldMapPinArtwork {
    static let yellow = NSColor(srgbRed: 1, green: 0.9898965359, blue: 0.3056603670, alpha: 1)
    static let green = NSColor(srgbRed: 0.66, green: 0.95, blue: 0.20, alpha: 1)
    static func color(for style: MapPinStyle) -> NSColor { style == .green ? green : yellow }
    static func title(for style: MapPinStyle) -> String {
        switch style {
        case .yellow: return L10n.text("Yellow pin", "黄色标记")
        case .green: return L10n.text("Green pin", "绿色标记")
        case .player: return L10n.text("Player marker", "玩家标记")
        }
    }
    static let playerGlyph = load("sprites/icon_char---2308601083109874541.png")
    static let playerHalo = load("sprites/deco_readio_mask--2444265073359569955.png")
    static let playerBeam: CGImage? = {
        guard let source = load("textures/T_fx_mask_02_M--4275033587688225551.png") else { return nil }
        return tintedBeam(source)
    }()

    private static func tintedBeam(_ source: CGImage) -> CGImage? {
        guard let space = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(data: nil, width: source.width, height: source.height,
                bitsPerComponent: 8, bytesPerRow: source.width * 4, space: space,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue),
              let data = context.data else { return nil }
        context.draw(source, in: CGRect(x: 0, y: 0, width: source.width, height: source.height))
        let pixels = data.assumingMemoryBound(to: UInt8.self)
        for offset in stride(from: 0, to: source.width * source.height * 4, by: 4) {
            // The original opaque grayscale ramp multiplies the material's
            // yellow color. Keep those premultiplied RGB contributions while
            // making its black end transparent, including outside blending.
            let intensity = Double(pixels[offset])
            pixels[offset + 1] = UInt8((intensity * 0.9898965359).rounded())
            pixels[offset + 2] = UInt8((intensity * 0.3056603670).rounded())
            pixels[offset + 3] = pixels[offset]
        }
        return context.makeImage()
    }

    private static func load(_ relative: String) -> CGImage? {
        guard let url = HUDResources.url(for: "WatchSource/Scene/Domain/" + relative),
              let values = try? url.resourceValues(forKeys: [.fileSizeKey]),
              let bytes = values.fileSize, bytes > 0, bytes <= 128 * 1024,
              let source = CGImageSourceCreateWithURL(url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? Int,
              let height = properties[kCGImagePropertyPixelHeight] as? Int,
              width > 0, height > 0, width <= 256, height <= 256 else { return nil }
        return CGImageSourceCreateImageAtIndex(source, 0, [kCGImageSourceShouldCacheImmediately: true] as CFDictionary)
    }
}
