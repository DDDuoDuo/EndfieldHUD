import AppKit

/// A small immutable glow bitmap is uploaded once per choice. No live blur,
/// extra animation clock, or full-resolution custom texture is retained.
enum HUDCenterLogoPresentation {
    static func image(_ source: CGImage) -> CGImage? {
        let size = CGSize(width: 512, height: 128)
        guard let context = CGContext(data: nil, width: 512, height: 128,
            bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        let scale = min(480 / CGFloat(source.width), 104 / CGFloat(source.height))
        let content = CGSize(width: CGFloat(source.width) * scale, height: CGFloat(source.height) * scale)
        let rect = CGRect(x: (size.width - content.width) / 2, y: (size.height - content.height) / 2,
                          width: content.width, height: content.height)
        context.interpolationQuality = .high
        context.setShadow(offset: .zero, blur: 7, color: NSColor.white.withAlphaComponent(0.7).cgColor)
        context.draw(source, in: rect)
        return context.makeImage()
    }
}
