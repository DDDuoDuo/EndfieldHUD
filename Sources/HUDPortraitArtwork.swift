import AppKit
import QuartzCore
import CoreImage

/// One portrait crop and the selected source avatar frame serve both profiles.
/// The imported image is decoded by the store once.
enum HUDPortraitArtwork {
    private final class RenderedPortrait {
        // The store owns the current full-resolution avatar. Cropped cache
        // entries must not keep replaced/reset imports alive. A weak identity
        // check also rejects a key whose original object's address was reused.
        weak var source: CGImage?
        let output: CGImage
        init(source: CGImage, output: CGImage) { self.source = source; self.output = output }
    }
    private static let renderContext = CIContext(options: [.cacheIntermediates: false])
    private static let renderedCache: NSCache<NSString, RenderedPortrait> = {
        let cache = NSCache<NSString, RenderedPortrait>()
        cache.countLimit = 6; cache.totalCostLimit = 8 * 1024 * 1024
        return cache
    }()
    // The same decoded source mip and untrimmed 254×254 sprite rect used by
    // HeadFrameImg in desktop-profile-card.json. Keep the transparent padding:
    // the exported trimmed PNG would move the frame relative to the portrait.
    private static let sourceFrame: CGImage? = {
        guard let url = HUDResources.url(for: "WatchSource/Textures/icon_user_avatar_frame_endfield_1--63c7ff92--7647223879671712896.bgra-mips.bin"),
              let data = try? HUDSourceResourceData.read(url) else { return nil }
        return try? HUDSourceProfileArtwork.backgroundArtwork(bgra: data, textureWidth: 256, textureHeight: 256,
            spriteRect: CGRect(x: 1, y: 1, width: 254, height: 254))
    }()

    static func frameImage(accent: NSColor) -> CGImage? {
        guard let sourceFrame, let rgb = accent.usingColorSpace(.sRGB) else { return nil }
        let key = "source-frame-\(rgb.redComponent)-\(rgb.greenComponent)-\(rgb.blueComponent)" as NSString
        if let existing = renderedCache.object(forKey: key), existing.source === sourceFrame { return existing.output }
        guard let context = CGContext(data: nil, width: sourceFrame.width, height: sourceFrame.height,
            bitsPerComponent: 8, bytesPerRow: sourceFrame.width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { return nil }
        let bounds = CGRect(x: 0, y: 0, width: sourceFrame.width, height: sourceFrame.height)
        context.draw(sourceFrame, in: bounds)
        guard let data = context.data else { return nil }
        let pixels = data.assumingMemoryBound(to: UInt8.self)
        let tint = [rgb.redComponent, rgb.greenComponent, rgb.blueComponent]
        for y in 0..<sourceFrame.height { for x in 0..<sourceFrame.width {
            let index = y * context.bytesPerRow + x * 4
            // Keep the selected frame's dark keyline and shadow pixels dark.
            for channel in 0..<3 { pixels[index + channel] = UInt8((CGFloat(pixels[index + channel]) * tint[channel]).rounded()) }
        } }
        guard let output = context.makeImage() else { return nil }
        renderedCache.setObject(RenderedPortrait(source: sourceFrame, output: output), forKey: key,
                                cost: output.bytesPerRow * output.height)
        return output
    }

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
        if let existing = renderedCache.object(forKey: key), existing.source === image { return existing.output }
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
                          ink: NSColor, accent: NSColor, contentsScale: CGFloat, orientation: Int32 = 1,
                          includeFrame: Bool = true) -> CALayer {
        let root = CALayer(); root.name = "profile.portrait"
        root.bounds = CGRect(origin: .zero, size: size)
        root.allowsGroupOpacity = false
        let side = min(size.width, size.height)
        let photo = CALayer(); photo.name = "portrait.image"
        photo.frame = CGRect(x: (size.width - side) / 2, y: (size.height - side) / 2, width: side, height: side)
        photo.backgroundColor = NSColor(white: 0.22, alpha: 0.85).cgColor
        photo.masksToBounds = true; photo.contentsScale = contentsScale
        root.addSublayer(photo)
        if let image {
            photo.contents = renderedImage(image, targetSize: photo.bounds.size,
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
        guard includeFrame else { return root }
        // Exact HeadFrameImg placement around the source's 136×136 photo.
        let unit = side / 136
        let frame = CALayer(); frame.name = "portrait.frame"
        frame.frame = CGRect(x: photo.frame.midX + 0.4164 * unit - 195 * unit / 2,
            y: photo.frame.midY - 195 * unit / 2, width: 195 * unit, height: 195 * unit)
        frame.contents = frameImage(accent: accent)
        frame.contentsScale = contentsScale; frame.contentsGravity = .resize
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
