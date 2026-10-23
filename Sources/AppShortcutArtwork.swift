import AppKit
import QuartzCore

/// The same retained pencil is used by drawing and shortcut-edit controls.
enum HUDPencilArtwork {
    private static let path: CGPath = {
        let pencil = CGMutablePath()
        pencil.addLines(between: [CGPoint(x: 2, y: 10), CGPoint(x: 12, y: 0),
            CGPoint(x: 15, y: 3), CGPoint(x: 5, y: 13), CGPoint(x: 1, y: 14)])
        pencil.closeSubpath()
        pencil.move(to: CGPoint(x: 0, y: 16)); pencil.addLine(to: CGPoint(x: 16, y: 16))
        return pencil
    }()

    static func makeLayer(in rect: CGRect, color: NSColor, contentsScale: CGFloat) -> CAShapeLayer {
        let layer = CAShapeLayer(); layer.name = "hud.pencil"; layer.frame = rect
        var fit = CGAffineTransform(scaleX: rect.width / 16, y: rect.height / 16)
        layer.path = path.copy(using: &fit)
        layer.strokeColor = color.cgColor; layer.fillColor = nil; layer.lineWidth = 1.1
        layer.contentsScale = contentsScale
        return layer
    }
}

/// Shared game artwork for matching presets. Presets without a game asset
/// (and an application whose own icon is unavailable) use Endfield-style
/// glyphs: solid, chamfered silhouettes with cut-outs, rasterized like the
/// game icons; presentations keep the trimmed rasters (`trimmedImage`).
enum AppShortcutArtwork {
    /// The picker and every navigation presentation resolve the same preset.
    /// Original application icons retain their colors; presets use the
    /// button's foreground tint.
    static func image(for icon: AppShortcutIcon, original: NSImage? = nil,
                      size: CGFloat, color: NSColor) -> CGImage? {
        if icon == .original, let original {
            var proposed = CGRect(x: 0, y: 0, width: size, height: size)
            return original.cgImage(forProposedRect: &proposed, context: nil, hints: nil)
        }
        if let game = gameIcon(for: icon) { return game.cgImage(size: size, tint: color) }
        return glyphImage(icon, size: size, color: color)
    }

    /// A preset's artwork trimmed to its visible bounds (cached), so game icons
    /// with different source padding and the glyphs fill the same box.
    static func trimmedImage(for icon: AppShortcutIcon, color: NSColor) -> CGImage? {
        HUDSourceDesktopIconLayout.trimmed("preset." + icon.rawValue, tint: color.cgColor) {
            image(for: icon, size: 96, color: color)
        }
    }

    /// Glyph parts on a 28-point grid, top-down: solids, then cut-outs (`cut`).
    private static func glyph(_ icon: AppShortcutIcon) -> [(path: CGPath, cut: Bool)]? {
        func poly(_ values: [CGFloat]) -> CGPath {
            let path = CGMutablePath()
            path.addLines(between: stride(from: 0, to: values.count, by: 2).map { CGPoint(x: values[$0], y: values[$0 + 1]) })
            path.closeSubpath(); return path
        }
        func union(_ paths: [CGPath]) -> CGPath {
            let result = CGMutablePath(); paths.forEach { result.addPath($0) }; return result
        }
        func rect(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> CGPath { CGPath(rect: CGRect(x: x, y: y, width: w, height: h), transform: nil) }
        let all: HUDShape.Corners = [.topLeft, .topRight, .bottomLeft, .bottomRight]
        switch icon {
        case .music:
            // Two beamed notes: slanted heads, square stems, a heavy beam with a slot.
            return [(union([poly([2,20, 11,18, 11,25, 2,27]), rect(8, 7, 3, 13),
                            poly([16,16, 25,14, 25,21, 16,23]), rect(22, 3, 3, 13), poly([8,6, 25,1.5, 25,7.5, 8,12])]), false),
                    (poly([12,8.6, 21,6.2, 21,7.8, 12,10.2]), true)]
        case .play:
            // An octagonal plate with an inner frame and a play arrow cut out.
            let frame = HUDShape.chamfer(CGRect(x: 4.5, y: 4.5, width: 19, height: 19), cut: 5.5, corners: all)
            return [(HUDShape.chamfer(CGRect(x: 1, y: 1, width: 26, height: 26), cut: 8, corners: all), false),
                    (frame.copy(strokingWithWidth: 1.4, lineCap: .butt, lineJoin: .miter, miterLimit: 4), true),
                    (poly([11,8.5, 20.5,14, 11,19.5]), true)]
        case .brush:
            // A flat brush on the diagonal: handle with a slot, ferrule and tip.
            var turn = CGAffineTransform(translationX: 14, y: 14).rotated(by: .pi / 4).translatedBy(x: -14, y: -14)
            let parts = [poly([11,0, 17,0, 17,15, 11,15]), rect(9.5, 16.5, 9, 4.5), poly([9.5,22.5, 18.5,22.5, 18.5,25, 14,30.5, 9.5,25])]
            return [(union(parts.compactMap { $0.copy(using: &turn) }), false),
                    (rect(13.2, 2.5, 1.6, 9.5).copy(using: &turn) ?? rect(0, 0, 0, 0), true)]
        case .camera:
            // A chamfered body and prism with a ringed lens and a flash window.
            return [(union([HUDShape.chamfer(CGRect(x: 1, y: 8, width: 26, height: 17), cut: 4, corners: [.topRight, .bottomLeft]),
                            poly([8,8, 10.5,3.5, 17.5,3.5, 20,8])]), false),
                    (CGPath(ellipseIn: CGRect(x: 7.5, y: 9.5, width: 13, height: 13), transform: nil), true),
                    (CGPath(ellipseIn: CGRect(x: 10.5, y: 12.5, width: 7, height: 7), transform: nil), false),
                    (rect(21.5, 11, 3, 2), true)]
        case .original:
            // An application without its own icon: three cells and a diamond.
            return [(union([HUDShape.chamfer(CGRect(x: 2, y: 2, width: 10.5, height: 10.5), cut: 3),
                            HUDShape.chamfer(CGRect(x: 15.5, y: 2, width: 10.5, height: 10.5), cut: 3),
                            HUDShape.chamfer(CGRect(x: 2, y: 15.5, width: 10.5, height: 10.5), cut: 3),
                            poly([20.75,14.5, 27,20.75, 20.75,27, 14.5,20.75])]), false)]
        default: return nil
        }
    }

    /// Fits a glyph's solid bounds into the centred 24/28 of `rect`.
    private static func fitted(_ parts: [(path: CGPath, cut: Bool)], in rect: CGRect) -> [(path: CGPath, cut: Bool)] {
        let bounds = parts.filter { !$0.cut }.reduce(CGRect.null) { $0.union($1.path.boundingBoxOfPath) }
        guard !bounds.isNull, bounds.width > 0, bounds.height > 0 else { return parts }
        let scale = min(rect.width, rect.height) * 24 / 28 / max(bounds.width, bounds.height)
        var transform = CGAffineTransform(translationX: rect.midX, y: rect.midY).scaledBy(x: scale, y: scale)
            .translatedBy(x: -bounds.midX, y: -bounds.midY)
        return parts.map { ($0.path.copy(using: &transform) ?? $0.path, $0.cut) }
    }

    private static func glyphImage(_ icon: AppShortcutIcon, size: CGFloat, color: NSColor) -> CGImage? {
        let pixels = max(8, min(512, Int(size.rounded(.up))))
        guard let parts = glyph(icon), let space = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        context.translateBy(x: 0, y: CGFloat(pixels)); context.scaleBy(x: 1, y: -1)
        context.setFillColor(color.cgColor)
        for part in fitted(parts, in: CGRect(x: 0, y: 0, width: pixels, height: pixels)) {
            context.setBlendMode(part.cut ? .clear : .normal)
            context.addPath(part.path); context.fillPath()
        }
        return context.makeImage()
    }

    static func gameIcon(for icon: AppShortcutIcon) -> EndfieldGameIcon? {
        switch icon {
        case .bolt: return .power
        case .star: return .medal
        case .terminal: return .environmentMonitoring
        case .globe: return .store
        case .folder: return .depot
        case .code: return .aic
        case .game: return .operatorProfile
        case .grid: return .worldMap
        case .textBubble: return .baker
        default: return nil
        }
    }
    static func path(for icon: AppShortcutIcon, in rect: CGRect) -> CGPath {
        if let parts = glyph(icon) {
            let result = CGMutablePath()
            fitted(parts, in: rect).forEach { result.addPath($0.path) }
            return result
        }
        let p = CGMutablePath()
        func line(_ points: [CGPoint], closed: Bool = false) { guard let first = points.first else { return }; p.move(to: first); for point in points.dropFirst() { p.addLine(to: point) }; if closed { p.closeSubpath() } }
        func points(_ values: [CGFloat]) -> [CGPoint] { stride(from: 0, to: values.count, by: 2).map { CGPoint(x: values[$0], y: values[$0 + 1]) } }
        switch icon {
        case .grid:
            for x in [CGFloat(3), 16] { for y in [CGFloat(3), 16] { p.addRect(CGRect(x: x, y: y, width: 9, height: 9)) } }
        case .bolt: line(points([16,1,5,16,13,16,11,27,24,11,16,11]), closed: true)
        case .star:
            for i in 0..<10 { let angle = CGFloat(i) * .pi / 5 - .pi / 2; let radius: CGFloat = i % 2 == 0 ? 12 : 5.2; let point = CGPoint(x: 14 + cos(angle) * radius, y: 14 + sin(angle) * radius); if i == 0 { p.move(to: point) } else { p.addLine(to: point) } }; p.closeSubpath()
        case .terminal:
            p.addRoundedRect(in: CGRect(x: 2, y: 4, width: 24, height: 20), cornerWidth: 2, cornerHeight: 2)
            line(points([7,10,11,14,7,18])); line(points([14,19,21,19]))
        case .globe:
            p.addEllipse(in: CGRect(x: 2, y: 2, width: 24, height: 24)); p.addEllipse(in: CGRect(x: 9, y: 2, width: 10, height: 24))
            line(points([2,14,26,14])); line(points([5,7,23,7])); line(points([5,21,23,21]))
        case .folder: line(points([2,7,11,7,14,10,26,10,24,24,3,24,2,7]), closed: true); line(points([2,7,2,4,11,4,14,7,24,7,24,10]))
        case .code: line(points([8,6,1,14,8,22])); line(points([20,6,27,14,20,22])); line(points([17,3,11,25]))
        case .game: p.addRoundedRect(in: CGRect(x: 1, y: 7, width: 26, height: 16), cornerWidth: 5, cornerHeight: 5); line(points([5,15,13,15])); line(points([9,11,9,19])); p.addEllipse(in: CGRect(x: 18, y: 12, width: 2, height: 2)); p.addEllipse(in: CGRect(x: 22, y: 16, width: 2, height: 2))
        case .textBubble:
            p.move(to: CGPoint(x: 6, y: 4)); p.addLine(to: CGPoint(x: 22, y: 4))
            p.addQuadCurve(to: CGPoint(x: 26, y: 8), control: CGPoint(x: 26, y: 4))
            p.addLine(to: CGPoint(x: 26, y: 18))
            p.addQuadCurve(to: CGPoint(x: 22, y: 22), control: CGPoint(x: 26, y: 22))
            p.addLine(to: CGPoint(x: 13, y: 22)); p.addLine(to: CGPoint(x: 6, y: 26))
            p.addLine(to: CGPoint(x: 7, y: 22)); p.addLine(to: CGPoint(x: 6, y: 22))
            p.addQuadCurve(to: CGPoint(x: 2, y: 18), control: CGPoint(x: 2, y: 22))
            p.addLine(to: CGPoint(x: 2, y: 8))
            p.addQuadCurve(to: CGPoint(x: 6, y: 4), control: CGPoint(x: 2, y: 4)); p.closeSubpath()
            line(points([7,10,21,10])); line(points([7,15,17,15]))
        default: break
        }
        var transform = CGAffineTransform(translationX: rect.minX, y: rect.minY)
            .scaledBy(x: rect.width / 28, y: rect.height / 28)
        return p.copy(using: &transform) ?? p
    }

    /// A preset's trimmed raster and its frame in `rect`, which stands for the
    /// HUD's 32-point icon box: the same optical size as the side lists.
    static func placedImage(for icon: AppShortcutIcon, in rect: CGRect, color: NSColor) -> (image: CGImage, frame: CGRect)? {
        guard let image = trimmedImage(for: icon, color: color) else { return nil }
        let glyph = HUDSourceDesktopIconLayout.glyphFrame(CGSize(width: image.width, height: image.height))
        let unit = rect.width / HUDSourceDesktopIconLayout.box
        return (image, CGRect(x: rect.minX + glyph.minX * unit, y: rect.minY + glyph.minY * unit,
                              width: glyph.width * unit, height: glyph.height * unit))
    }
}
