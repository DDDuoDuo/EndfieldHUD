import AppKit
import CoreText
import QuartzCore

/// Paths of the shape language (spec §0, §2.3). Coordinates are the HUD's
/// top-down layer space: "top" is the minimum y.
enum HUDShape {
    struct Corners: OptionSet {
        let rawValue: Int
        static let topLeft = Corners(rawValue: 1), topRight = Corners(rawValue: 2)
        static let bottomRight = Corners(rawValue: 4), bottomLeft = Corners(rawValue: 8)
    }

    /// "Chamfer N": the listed corners cut at 45°, N px on each leg. The spec's
    /// default is the top-right corner alone.
    static func chamfer(_ rect: CGRect, cut: CGFloat, corners: Corners = .topRight) -> CGPath {
        let n = max(0, min(cut, rect.width / 2, rect.height / 2))
        let path = CGMutablePath()
        path.move(to: CGPoint(x: rect.minX + (corners.contains(.topLeft) ? n : 0), y: rect.minY))
        if corners.contains(.topRight) {
            path.addLine(to: CGPoint(x: rect.maxX - n, y: rect.minY)); path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY + n))
        } else { path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY)) }
        if corners.contains(.bottomRight) {
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - n)); path.addLine(to: CGPoint(x: rect.maxX - n, y: rect.maxY))
        } else { path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY)) }
        if corners.contains(.bottomLeft) {
            path.addLine(to: CGPoint(x: rect.minX + n, y: rect.maxY)); path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY - n))
        } else { path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY)) }
        if corners.contains(.topLeft) { path.addLine(to: CGPoint(x: rect.minX, y: rect.minY + n)) }
        path.closeSubpath()
        return path
    }

    /// Vertical 1 px ticks every `pitch`, from x = 0 through the last that fits.
    static func tickLadder(width: CGFloat, height: CGFloat, pitch: CGFloat, tick: CGFloat = 1) -> CGPath {
        let path = CGMutablePath()
        var x: CGFloat = 0
        while x <= width - tick + 0.001 { path.addRect(CGRect(x: x, y: 0, width: tick, height: height)); x += pitch }
        return path
    }

    /// `count` ticks spread evenly over `width` (both ends included).
    static func ticks(count: Int, width: CGFloat, height: CGFloat, tick: CGFloat = 1) -> CGPath {
        let path = CGMutablePath()
        guard count > 1 else { return path }
        for index in 0..<count {
            let x = (width - tick) * CGFloat(index) / CGFloat(count - 1)
            path.addRect(CGRect(x: x, y: 0, width: tick, height: height))
        }
        return path
    }

    /// Crosshair `+`: two 1 px lines crossing at the centre of `size`.
    static func crosshair(size: CGFloat = 11) -> CGPath {
        let path = CGMutablePath(), mid = (size - 1) / 2
        path.addRect(CGRect(x: mid, y: 0, width: 1, height: size))
        path.addRect(CGRect(x: 0, y: mid, width: size, height: 1))
        return path
    }

    /// Two-leg corner mark (L) of `length` and `width` at the given corner of `rect`.
    static func cornerMark(_ rect: CGRect, length: CGFloat, width: CGFloat, corner: Corners) -> CGPath {
        let path = CGMutablePath()
        let x = corner.contains(.topRight) || corner.contains(.bottomRight) ? rect.maxX - length : rect.minX
        let y = corner.contains(.bottomLeft) || corner.contains(.bottomRight) ? rect.maxY - length : rect.minY
        let vx = corner.contains(.topRight) || corner.contains(.bottomRight) ? rect.maxX - width : rect.minX
        let hy = corner.contains(.bottomLeft) || corner.contains(.bottomRight) ? rect.maxY - width : rect.minY
        path.addRect(CGRect(x: x, y: hy, width: length, height: width))
        path.addRect(CGRect(x: vx, y: y, width: width, height: length))
        return path
    }

    /// Down-pointing triangle (dropdown caret, bubble arrow).
    static func triangleDown(width: CGFloat, height: CGFloat) -> CGPath {
        let path = CGMutablePath()
        path.addLines(between: [CGPoint(x: 0, y: 0), CGPoint(x: width, y: 0), CGPoint(x: width / 2, y: height)])
        path.closeSubpath()
        return path
    }
}

/// Small raster motifs, drawn once per (motif, size, scale) and shared by every
/// layer that shows them: hatch tails, the micro ENDFIELD tag and the tri-strip.
enum HUDArtwork {
    enum Hatch: Hashable { case dark, light }

    private struct Key: Hashable { let name: String; let width: CGFloat; let height: CGFloat; let scale: CGFloat }
    private static var cache: [Key: CGImage] = [:]
    private static let lock = NSRecursiveLock()

    /// repeating-linear-gradient(-45deg, c 0 1px, transparent 1px 3.5px).
    static func hatch(_ kind: Hatch, size: CGSize, scale: CGFloat) -> CGImage? {
        // The light hatch follows Theme: the key names it (the cache is also dropped on a change).
        image("hatch.\(kind).\(kind == .light && HUDColor.isLight)", size: size, scale: scale) { ctx, size in
            drawHatch(kind, in: CGRect(origin: .zero, size: size), ctx: ctx)
        }
    }

    /// Stripes run bottom-left to top-right, 1 px thick and 3.5 px apart across them.
    static func drawHatch(_ kind: Hatch, in rect: CGRect, ctx: CGContext) {
        ctx.saveGState()
        ctx.clip(to: rect)
        ctx.setFillColor(kind == .dark ? HUDColor.black(0.42) : HUDColor.foreground(0.14))
        let spacing = 3.5 * CGFloat(2).squareRoot(), thickness = CGFloat(2).squareRoot()
        var offset = rect.minX - rect.height
        while offset < rect.maxX + rect.height {
            let path = CGMutablePath()
            path.addLines(between: [CGPoint(x: offset, y: rect.maxY), CGPoint(x: offset + thickness, y: rect.maxY),
                                    CGPoint(x: offset + thickness + rect.height, y: rect.minY), CGPoint(x: offset + rect.height, y: rect.minY)])
            path.closeSubpath(); ctx.addPath(path); ctx.fillPath()
            offset += spacing
        }
        ctx.restoreGState()
    }

    /// The micro ENDFIELD tag (spec §2.2): black label, then a 44 × 7 dark hatch.
    static var microTagSize: CGSize { CGSize(width: microTagLabelWidth + 44, height: 7) }
    private static var microTagLabelWidth: CGFloat { HUDText.width("ENDFIELD", HUDType.microTag) + 8 }

    static func microTag(inverted: Bool = false, scale: CGFloat) -> CGImage? {
        let labelWidth = microTagLabelWidth, size = microTagSize
        return image("microTag.\(inverted)", size: size, scale: scale) { ctx, size in
            ctx.setFillColor(inverted ? HUDColor.microTagText : HUDColor.microTagBlack)
            ctx.fill(CGRect(x: 0, y: 0, width: labelWidth, height: 7))
            let style = HUDType.microTag.colored(inverted ? HUDColor.nearBlack : HUDColor.microTagText)
            draw("ENDFIELD", style: style, in: CGRect(x: 4, y: 0, width: labelWidth - 4, height: 7), ctx: ctx)
            drawHatch(.dark, in: CGRect(x: labelWidth, y: 0, width: 44, height: 7), ctx: ctx)
        }
    }

    /// Hard three-band strip (magenta, mint, yellow), vertical bands top to bottom.
    static func triStrip(size: CGSize, scale: CGFloat) -> CGImage? {
        image("triStrip", size: size, scale: scale) { ctx, size in
            for (index, color) in HUDColor.triStrip.enumerated() {
                ctx.setFillColor(color)
                ctx.fill(CGRect(x: 0, y: size.height * CGFloat(index) / 3, width: size.width, height: size.height / 3))
            }
        }
    }

    /// A light plate: a vertical gradient clipped to a chamfer, optionally with
    /// the 1 px top highlight (rgba(255,255,255,.75)). Rows, light and primary
    /// buttons, cards and the About hero share these cached rasters.
    static func plate(_ colors: [CGColor], locations: [CGFloat]? = nil, size: CGSize, cut: CGFloat,
                      corners: HUDShape.Corners = .topRight, highlight: Bool = false, scale: CGFloat) -> CGImage? {
        let key = "plate.\(colors.map { $0.components?.map { String(format: "%.3f", $0) }.joined(separator: ",") ?? "" })"
            + ".\(locations ?? []).\(cut).\(corners.rawValue).\(highlight)"
        return image(key, size: size, scale: scale) { ctx, size in
            let rect = CGRect(origin: .zero, size: size)
            ctx.addPath(HUDShape.chamfer(rect, cut: cut, corners: corners)); ctx.clip()
            if let gradient = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: colors as CFArray,
                                         locations: locations ?? colors.indices.map { CGFloat($0) / CGFloat(max(1, colors.count - 1)) }) {
                ctx.drawLinearGradient(gradient, start: .zero, end: CGPoint(x: 0, y: size.height), options: [])
            }
            if highlight { ctx.setFillColor(HUDColor.white(0.75)); ctx.fill(CGRect(x: 0, y: 0, width: size.width, height: 1)) }
        }
    }

    /// Drops every cached raster once the HUD has closed and released its
    /// layers. The images, and the render copies Core Animation attaches to
    /// them, would otherwise outlive every page that showed them.
    static func releaseCachedImages() {
        lock.lock(); defer { lock.unlock() }
        cache.removeAll()
    }

    /// Draws `body` in a top-down space of `size` points at `scale`, cached.
    private static func image(_ name: String, size: CGSize, scale: CGFloat, body: (CGContext, CGSize) -> Void) -> CGImage? {
        let scale = max(1, min(8, scale.isFinite ? scale.rounded(.up) : 2))
        let key = Key(name: name, width: size.width, height: size.height, scale: scale)
        lock.lock(); defer { lock.unlock() }
        if let cached = cache[key] { return cached }
        let width = Int(ceil(size.width * scale)), height = Int(ceil(size.height * scale))
        guard width > 0, height > 0, let space = CGColorSpace(name: CGColorSpace.sRGB),
              let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.translateBy(x: 0, y: CGFloat(height)); ctx.scaleBy(x: scale, y: -scale)
        ctx.setAllowsAntialiasing(true); ctx.setShouldAntialias(true)
        body(ctx, size)
        let image = ctx.makeImage()
        cache[key] = image
        return image
    }

    private static func draw(_ text: String, style: HUDTextStyle, in rect: CGRect, ctx: CGContext) {
        let string = HUDText.attributed([HUDText.Run(text, style)], language: HUDTextLanguage.simplifiedChinese)
        let line = CTLineCreateWithAttributedString(string)
        let metrics = HUDText.metrics(style, language: HUDTextLanguage.simplifiedChinese)
        ctx.saveGState()
        ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
        ctx.textPosition = CGPoint(x: rect.minX, y: rect.minY + (rect.height - metrics.height) / 2 + metrics.baseline)
        CTLineDraw(line, ctx)
        ctx.restoreGState()
    }
}

/// A hatched panel (empty states) painted straight into its own backing
/// store, with the pixels a cached `HUDArtwork.hatch` image of its size would
/// have. Such panels are large and unique to their page: a shared CGImage
/// would be held twice, by the cache and by Core Animation's render copy.
final class HUDHatchLayer: CALayer {
    let kind: HUDArtwork.Hatch
    /// The rounded scale cached motifs use; `applyScale` leaves it alone.
    let artworkScale: CGFloat

    init(_ kind: HUDArtwork.Hatch, scale: CGFloat) {
        self.kind = kind
        artworkScale = max(1, min(8, scale.isFinite ? scale.rounded(.up) : 2))
        super.init()
        contentsScale = artworkScale
        needsDisplayOnBoundsChange = true
    }

    override init(layer: Any) {
        let source = layer as? HUDHatchLayer
        kind = source?.kind ?? .light; artworkScale = source?.artworkScale ?? 2
        super.init(layer: layer)
    }

    required init?(coder: NSCoder) { nil }

    override func draw(in ctx: CGContext) {
        ctx.saveGState()
        // The motif is drawn top-down, whichever way Core Animation flipped this context.
        if ctx.ctm.d > 0 { ctx.translateBy(x: 0, y: bounds.height); ctx.scaleBy(x: 1, y: -1) }
        ctx.setAllowsAntialiasing(true); ctx.setShouldAntialias(true)
        HUDArtwork.drawHatch(kind, in: CGRect(origin: .zero, size: bounds.size), ctx: ctx)
        ctx.restoreGState()
    }
}

extension CALayer {
    /// Shows a cached raster motif at its natural size without resampling.
    func showArtwork(_ image: CGImage?, scale: CGFloat) {
        contents = image
        contentsScale = max(1, min(8, scale.isFinite ? scale.rounded(.up) : 2))
        contentsGravity = .resize
        magnificationFilter = .linear
    }
}

/// The full-centre backdrop that Personal ID's background art and the Map
/// fill, in content-box px: a 430 px disc around the shell's disc centre,
/// reaching past the content box on every side, opaque to 52 % of its radius
/// and fading through 55 % at 74 % to nothing at the rim (the demo's
/// radial-gradient(circle 430px, #000 0 52%, rgba(0,0,0,.55) 74%, transparent)).
enum HUDCentreBackdrop {
    static let centre = CGPoint(x: 295, y: 304)
    static let radius: CGFloat = 430
    static let frame = CGRect(x: centre.x - radius, y: centre.y - radius, width: 2 * radius, height: 2 * radius)
    /// The ramp as a layer mask over `frame`, one shared image at half
    /// resolution (the soft ramp spans some 200 px, so it scales up smoothly).
    static let mask: CGImage? = {
        let pixels = Int(radius), space = CGColorSpaceCreateDeviceGray()
        guard let context = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let ramp = CGGradient(colorsSpace: space, colors: [CGColor(gray: 0, alpha: 1), CGColor(gray: 0, alpha: 1),
                                                                 CGColor(gray: 0, alpha: 0.55), CGColor(gray: 0, alpha: 0)] as CFArray,
                                    locations: [0, 0.52, 0.74, 1]) else { return nil }
        let middle = CGPoint(x: CGFloat(pixels) / 2, y: CGFloat(pixels) / 2)
        context.drawRadialGradient(ramp, startCenter: middle, startRadius: 0, endCenter: middle, endRadius: CGFloat(pixels) / 2, options: [])
        return context.makeImage()
    }()
}
