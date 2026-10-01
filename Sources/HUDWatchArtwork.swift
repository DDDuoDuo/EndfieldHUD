import AppKit
import ImageIO

/// WatchPanel_PC sprites, decoded at their exported native size. Provenance is
/// recorded in Resources/Watch/SOURCES.json; tinting keeps the source alpha.
enum HUDWatchArtwork {
    enum Asset: String, CaseIterable {
        case midRing = "ui_mid_ring"
        case triangle = "ui_triangle_fx"

        fileprivate var dimensions: (width: Int, height: Int) {
            switch self {
            case .midRing: return (1004, 1004)
            case .triangle: return (143, 123)
            }
        }
    }

    private struct Variant {
        let image: CGImage
        let cost: Int
    }

    // There are only two source keys. A lock-protected LRU makes both variant
    // limits strict and cache hits deterministic, including under memory pressure.
    private static var sources: [Asset: CGImage] = [:]
    private static let lock = NSLock()
    private static let variantCountLimit = 8
    private static let variantCostLimit = 16 * 1_024 * 1_024
    private static var variants: [String: Variant] = [:]
    private static var variantOrder: [String] = [] // Least recently used first.
    private static var variantCost = 0

    static func image(_ asset: Asset, tint: NSColor) -> CGImage? {
        guard let rgb = tint.usingColorSpace(.deviceRGB) else { return nil }
        let components = [rgb.redComponent, rgb.greenComponent, rgb.blueComponent, rgb.alphaComponent]
        guard components.allSatisfy({ $0.isFinite }) else { return nil }
        let key = "\(asset.rawValue):\(components.map { String(describing: $0) }.joined(separator: ":"))"

        lock.lock()
        defer { lock.unlock() }
        if let cached = variants[key] {
            if let index = variantOrder.firstIndex(of: key) { variantOrder.remove(at: index) }
            variantOrder.append(key)
            return cached.image
        }
        guard let source = sourceImage(asset),
              let context = CGContext(data: nil, width: source.width, height: source.height,
                bitsPerComponent: 8, bytesPerRow: source.width * 4,
                space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { return nil }
        let bounds = CGRect(x: 0, y: 0, width: source.width, height: source.height)
        // No transform or resampling: Unity's exported Sprite crop/orientation is
        // retained. Layer geometry controls its display size and rotation.
        context.draw(source, in: bounds)
        context.setBlendMode(.sourceIn)
        context.setFillColor(rgb.cgColor)
        context.fill(bounds)
        guard let image = context.makeImage() else { return nil }
        let cost = image.bytesPerRow * image.height
        // A future oversized source can still be displayed without entering the
        // cache. Evict only old variants; source pixels and layer contents survive.
        if cost <= variantCostLimit {
            while !variantOrder.isEmpty && (variants.count >= variantCountLimit || variantCost + cost > variantCostLimit) {
                let oldest = variantOrder.removeFirst()
                if let removed = variants.removeValue(forKey: oldest) { variantCost -= removed.cost }
            }
            variants[key] = Variant(image: image, cost: cost)
            variantOrder.append(key)
            variantCost += cost
        }
        return image
    }

    /// Called while holding lock. Validate dimensions before asking ImageIO to
    /// allocate decoded pixels; missing/mismatched assets leave vector fallbacks.
    private static func sourceImage(_ asset: Asset) -> CGImage? {
        if let cached = sources[asset] { return cached }
        let dimensions = asset.dimensions
        guard let url = HUDResources.url(for: "Watch/\(asset.rawValue).png"),
              let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              (properties[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue == dimensions.width,
              (properties[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue == dimensions.height,
              let image = CGImageSourceCreateImageAtIndex(source, 0,
                [kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else { return nil }
        sources[asset] = image
        return image
    }
}
