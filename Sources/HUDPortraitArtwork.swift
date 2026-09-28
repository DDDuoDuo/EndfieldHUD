import AppKit
import QuartzCore
import CoreImage

/// One portrait crop and frame renderer serves the profile and shell card.
/// Geometry stays vector-based; the imported image is decoded by the store once.
enum HUDPortraitArtwork {
    private final class RenderedPortrait {
        // Retaining the source also prevents an object-identifier cache key
        // from being reused for another imported image.
        let source: CGImage
        let output: CGImage
        init(source: CGImage, output: CGImage) { self.source = source; self.output = output }
    }
    private static let renderContext = CIContext(options: [.cacheIntermediates: false])
    private static let renderedCache: NSCache<NSString, RenderedPortrait> = {
        let cache = NSCache<NSString, RenderedPortrait>()
        cache.countLimit = 6; cache.totalCostLimit = 8 * 1024 * 1024
        return cache
    }()

    /// Crop the native-resolution source before making a small display bitmap.
    /// Avoids enlarging a 512px thumbnail or uploading a full-source texture at 20×.
    static func renderedImage(_ image: CGImage, targetSize: CGSize, zoom: Double,
                              offset: CGPoint, contentsScale: CGFloat, orientation: Int32 = 1) -> CGImage? {
        guard targetSize.width.isFinite, targetSize.height.isFinite,
              targetSize.width > 0, targetSize.height > 0 else { return nil }
        let orientation = (1...8).contains(orientation) ? orientation : 1
        let source = CIImage(cgImage: image).oriented(forExifOrientation: orientation)
        let extent = source.extent
        let selection = crop(imageSize: extent.size, targetSize: targetSize, zoom: zoom, offset: offset)
        // Public portrait offsets are top-left based; Core Image is bottom-left.
        let region = CGRect(x: extent.minX + selection.minX * extent.width,
                            y: extent.minY + (1 - selection.maxY) * extent.height,
                            width: selection.width * extent.width, height: selection.height * extent.height)
        let scale = contentsScale.isFinite ? min(8, max(1, contentsScale)) : 2
        // Small oversampling allowance preserves detail during perspective lift.
        let width = max(1, min(1024, Int(ceil(targetSize.width * scale * 1.35))))
        let height = max(1, min(1024, Int(ceil(targetSize.height * scale * 1.35))))
        let key = "\(ObjectIdentifier(image))-\(orientation)-\(region)-\(width)x\(height)" as NSString
        if let existing = renderedCache.object(forKey: key) { return existing.output }
        let sx = CGFloat(width) / region.width, sy = CGFloat(height) / region.height
        let selected = source.cropped(to: region).transformed(by: CGAffineTransform(translationX: -region.minX, y: -region.minY))
        let scaled = selected.applyingFilter("CILanczosScaleTransform", parameters: [kCIInputScaleKey: sy, kCIInputAspectRatioKey: sx / sy])
        guard let output = renderContext.createCGImage(scaled, from: CGRect(x: 0, y: 0, width: width, height: height)) else { return nil }
        renderedCache.setObject(RenderedPortrait(source: image, output: output), forKey: key,
                                cost: output.bytesPerRow * output.height)
        return output
    }
    static func crop(imageSize: CGSize, targetSize: CGSize, zoom: Double, offset: CGPoint) -> CGRect {
        guard imageSize.width.isFinite, imageSize.height.isFinite,
              targetSize.width.isFinite, targetSize.height.isFinite,
              imageSize.width > 0, imageSize.height > 0, targetSize.width > 0, targetSize.height > 0 else {
            return CGRect(x: 0, y: 0, width: 1, height: 1)
        }
        let magnification = CGFloat(zoom.isFinite ? min(20, max(1, zoom)) : 1)
        let fit = max(targetSize.width / imageSize.width, targetSize.height / imageSize.height)
        let width = min(1, targetSize.width / (imageSize.width * fit)) / magnification
        let height = min(1, targetSize.height / (imageSize.height * fit)) / magnification
        let x = offset.x.isFinite ? min(1, max(-1, offset.x)) : 0
        let y = offset.y.isFinite ? min(1, max(-1, offset.y)) : 0
        return CGRect(x: (1 - width) * (x + 1) / 2, y: (1 - height) * (y + 1) / 2,
                      width: width, height: height)
    }

    static func makeLayer(image: CGImage?, profile: UserProfile?, size: CGSize,
                          ink: NSColor, accent: NSColor, contentsScale: CGFloat, orientation: Int32 = 1) -> CALayer {
        let root = CALayer(); root.name = "profile.portrait"
        root.bounds = CGRect(origin: .zero, size: size)
        root.allowsGroupOpacity = false
        let unit = min(size.width / 66, size.height / 71)
        let photo = CALayer(); photo.name = "portrait.image"; photo.frame = root.bounds
        photo.backgroundColor = NSColor(white: 0.22, alpha: 0.85).cgColor
        photo.masksToBounds = true; photo.contentsScale = contentsScale
        root.addSublayer(photo)
        if let image {
            photo.contents = renderedImage(image, targetSize: size,
                zoom: profile?.avatarZoom ?? 1,
                offset: CGPoint(x: profile?.avatarOffsetX ?? 0, y: profile?.avatarOffsetY ?? 0),
                contentsScale: contentsScale, orientation: orientation)
            photo.contentsGravity = .resize
        } else {
            let silhouette = CGMutablePath()
            silhouette.addEllipse(in: CGRect(x: size.width * 0.36, y: size.height * 0.20,
                                             width: size.width * 0.30, height: size.height * 0.31))
            silhouette.move(to: CGPoint(x: size.width * 0.18, y: size.height * 0.86))
            silhouette.addCurve(to: CGPoint(x: size.width * 0.83, y: size.height * 0.86),
                control1: CGPoint(x: size.width * 0.24, y: size.height * 0.45),
                control2: CGPoint(x: size.width * 0.77, y: size.height * 0.45))
            silhouette.closeSubpath()
            photo.addSublayer(shape(silhouette, fill: ink.withAlphaComponent(0.63), scale: contentsScale))
        }
        // Four soft inner shadows darken the edges without dimming the face.
        let vignette = CALayer(); vignette.name = "portrait.vignette"; vignette.frame = photo.bounds
        let w = size.width, h = size.height
        let gradients: [(CGRect, CGPoint, CGPoint)] = [
            (CGRect(x: 0, y: 0, width: w, height: h * 0.21), CGPoint(x: 0.5, y: 0), CGPoint(x: 0.5, y: 1)),
            (CGRect(x: 0, y: h * 0.79, width: w, height: h * 0.21), CGPoint(x: 0.5, y: 1), CGPoint(x: 0.5, y: 0)),
            (CGRect(x: 0, y: 0, width: w * 0.21, height: h), CGPoint(x: 0, y: 0.5), CGPoint(x: 1, y: 0.5)),
            (CGRect(x: w * 0.79, y: 0, width: w * 0.21, height: h), CGPoint(x: 1, y: 0.5), CGPoint(x: 0, y: 0.5))
        ]
        for (rect, start, end) in gradients {
            let edge = CAGradientLayer(); edge.frame = rect; edge.startPoint = start; edge.endPoint = end
            edge.colors = [NSColor.black.withAlphaComponent(0.67).cgColor, NSColor.black.withAlphaComponent(0.20).cgColor, NSColor.clear.cgColor]
            edge.locations = [0, 0.42, 1]; vignette.addSublayer(edge)
        }
        photo.addSublayer(vignette)

        let frame = CALayer(); frame.name = "portrait.frame"; frame.frame = root.bounds
        let outline = CGPath(rect: root.bounds.insetBy(dx: -2.8 * unit, dy: -2.8 * unit), transform: nil)
        frame.addSublayer(shape(outline, stroke: ink.withAlphaComponent(0.90), width: 0.9 * unit, scale: contentsScale))
        let edge = CGPath(rect: root.bounds.insetBy(dx: -1 * unit, dy: -1 * unit), transform: nil)
        frame.addSublayer(shape(edge, stroke: accent.withAlphaComponent(0.43), width: 0.65 * unit, scale: contentsScale))
        func line(_ points: [CGPoint], color: NSColor, width: CGFloat) {
            let path = CGMutablePath(); if let first = points.first { path.move(to: first) }
            points.dropFirst().forEach { path.addLine(to: $0) }
            frame.addSublayer(shape(path, stroke: color, width: width * unit, scale: contentsScale))
        }
        // Broken corner rails and the registration cross from the reference.
        line([CGPoint(x: -6 * unit, y: h - 5 * unit), CGPoint(x: -6 * unit, y: -5 * unit), CGPoint(x: 16 * unit, y: -5 * unit)], color: ink.withAlphaComponent(0.78), width: 1.8)
        line([CGPoint(x: -3 * unit, y: 16 * unit), CGPoint(x: -3 * unit, y: -9 * unit)], color: accent, width: 2.4)
        line([CGPoint(x: -10 * unit, y: 1 * unit), CGPoint(x: 7 * unit, y: 1 * unit)], color: accent, width: 2.4)
        line([CGPoint(x: 1 * unit, y: -9 * unit), CGPoint(x: 1 * unit, y: -3 * unit), CGPoint(x: 17 * unit, y: -3 * unit)], color: accent.withAlphaComponent(0.76), width: 1.7)
        let marker = CGRect(x: -17 * unit, y: -2 * unit, width: 7 * unit, height: 3.5 * unit)
        frame.addSublayer(shape(CGPath(rect: marker, transform: nil), fill: accent, scale: contentsScale))
        line([CGPoint(x: w + 4 * unit, y: h - 7 * unit), CGPoint(x: w + 4 * unit, y: h + 4 * unit), CGPoint(x: w - 7 * unit, y: h + 4 * unit)], color: ink.withAlphaComponent(0.77), width: 1.8)
        line([CGPoint(x: w + 7 * unit, y: h - 4 * unit), CGPoint(x: w + 7 * unit, y: h + 7 * unit), CGPoint(x: w - 4 * unit, y: h + 7 * unit)], color: ink.withAlphaComponent(0.43), width: 2.1)
        root.addSublayer(frame)
        return root
    }

    private static func shape(_ path: CGPath, fill: NSColor? = nil, stroke: NSColor? = nil,
                              width: CGFloat = 1, scale: CGFloat) -> CAShapeLayer {
        let result = CAShapeLayer(); result.path = path; result.fillColor = fill?.cgColor
        result.strokeColor = stroke?.cgColor; result.lineWidth = width; result.contentsScale = scale
        return result
    }
}

enum HUDProfileLevelArtwork {
    enum Kind { case authority, exploration }
    static func makeLayer(_ kind: Kind, frame: CGRect, color: NSColor, contentsScale: CGFloat) -> CAShapeLayer {
        let result = CAShapeLayer(); result.frame = frame; result.contentsScale = contentsScale
        let path = CGMutablePath()
        func polygon(_ points: [(CGFloat, CGFloat)]) {
            guard let first = points.first else { return }; path.move(to: CGPoint(x: first.0, y: first.1))
            for p in points.dropFirst() { path.addLine(to: CGPoint(x: p.0, y: p.1)) }; path.closeSubpath()
        }
        switch kind {
        case .authority:
            // Three staggered folded plates, with the front face left open.
            polygon([(5,10),(15,17),(15,30),(5,23)])
            path.move(to: CGPoint(x: 10, y: 6)); path.addLine(to: CGPoint(x: 22, y: 14)); path.addLine(to: CGPoint(x: 22, y: 28))
            path.move(to: CGPoint(x: 17, y: 2)); path.addLine(to: CGPoint(x: 29, y: 10)); path.addLine(to: CGPoint(x: 29, y: 25))
            result.fillColor = nil; result.strokeColor = color.cgColor
            result.lineWidth = frame.width / 34 * 2.7; result.lineJoin = .miter
        case .exploration:
            // Solid eye crest with a circular aperture and serrated side fins.
            // Keep the aperture separate from the fins so overlapping geometry
            // does not punch unintended even-odd holes through the crest.
            let eye = CGMutablePath()
            eye.addEllipse(in: CGRect(x: 7, y: 6, width: 20, height: 20))
            eye.addEllipse(in: CGRect(x: 13, y: 12, width: 8, height: 8))
            var eyeTransform = CGAffineTransform(scaleX: frame.width / 34, y: frame.height / 32)
            let aperture = CAShapeLayer(); aperture.path = eye.copy(using: &eyeTransform)
            aperture.fillColor = color.cgColor; aperture.fillRule = .evenOdd
            aperture.contentsScale = contentsScale; result.addSublayer(aperture)
            polygon([(8,8),(12,4),(12,10)])
            polygon([(22,8),(25,4),(26,12)])
            polygon([(8,24),(12,28),(12,22)])
            polygon([(22,24),(25,28),(26,20)])
            polygon([(5,5),(7,10),(6,16),(7,22),(5,27),(3,21),(3,11)])
            polygon([(1,8),(2,13),(2,19),(1,24),(0,20),(0,12)])
            polygon([(29,5),(27,10),(28,16),(27,22),(29,27),(31,21),(31,11)])
            polygon([(33,8),(32,13),(32,19),(33,24),(34,20),(34,12)])
            result.fillColor = color.cgColor
        }
        var transform = CGAffineTransform(scaleX: frame.width / 34, y: frame.height / 32)
        result.path = path.copy(using: &transform)
        result.name = kind == .authority ? "profile.level.authority" : "profile.level.exploration"
        return result
    }
}
