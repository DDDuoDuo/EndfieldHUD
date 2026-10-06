import AppKit

/// A small immutable glow bitmap is uploaded once per choice. No live blur,
/// extra animation clock, or full-resolution custom texture is retained.
enum HUDCenterLogoPresentation {
    static func image(_ source: CGImage) -> CGImage? {
        // Match the authored 300 × 65 EndfieldText slot at 2×. Fitting the
        // original file canvas made padded/square presets much smaller and
        // the former 4:1 bitmap also distorted the slot's 60:13 proportions.
        // Normalize visible artwork to this same footprint for every choice.
        guard let content = visibleArtwork(source),
              let context = CGContext(data: nil, width: 600, height: 130,
            bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        context.interpolationQuality = .high
        context.setShadow(offset: .zero, blur: 7, color: NSColor.white.withAlphaComponent(0.7).cgColor)
        context.draw(content, in: CGRect(x: 0, y: 0, width: 600, height: 130))
        return context.makeImage()
    }

    /// Runs only when a choice/revision changes, never on pointer or animation
    /// frames. Inspect canonical alpha bytes so transparent file margins do not
    /// determine a logo's displayed size. Custom artwork keeps its own colors.
    private static func visibleArtwork(_ source: CGImage) -> CGImage? {
        let width = source.width, height = source.height
        guard let context = CGContext(data: nil, width: width, height: height,
            bitsPerComponent: 8, bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue),
              let bytes = context.data?.assumingMemoryBound(to: UInt8.self) else { return nil }
        context.draw(source, in: CGRect(x: 0, y: 0, width: width, height: height))
        var left = width, bottom = height, right = -1, top = -1
        for y in 0..<height {
            for x in 0..<width where bytes[y * context.bytesPerRow + x * 4 + 3] > 8 {
                left = min(left, x); right = max(right, x)
                bottom = min(bottom, y); top = max(top, y)
            }
        }
        guard right >= left, top >= bottom else { return nil }
        return context.makeImage()?.cropping(to: CGRect(x: left, y: bottom,
            width: right - left + 1, height: top - bottom + 1))
    }
}
