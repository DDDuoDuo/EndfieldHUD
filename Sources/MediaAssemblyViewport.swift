import AppKit

/// View-only transforms. Zoom/pan never change output pixels or ask Core Image
/// to render; the same small preview texture is retained until an edit changes it.
struct MediaAssemblyViewport {
    private(set) var zoom: CGFloat = 1
    private(set) var pan = CGPoint.zero
    static let zoomRange: ClosedRange<CGFloat> = 1...20
    mutating func reset() { zoom = 1; pan = .zero }
    func imageRect(size: CGSize, in viewport: CGRect) -> CGRect {
        guard size.width > 0, size.height > 0 else { return viewport }
        let fit = min(viewport.width / size.width, viewport.height / size.height) * zoom
        let width = size.width * fit, height = size.height * fit
        return CGRect(x: viewport.midX - width / 2 + pan.x, y: viewport.midY - height / 2 + pan.y, width: width, height: height)
    }
    mutating func magnify(by factor: CGFloat, at point: CGPoint, size: CGSize, in viewport: CGRect) {
        guard factor.isFinite, factor > 0 else { return }
        let old = imageRect(size: size, in: viewport), next = min(Self.zoomRange.upperBound, max(Self.zoomRange.lowerBound, zoom * factor))
        let ratio = next / zoom
        zoom = next
        pan.x = point.x - (point.x - old.midX) * ratio - viewport.midX
        pan.y = point.y - (point.y - old.midY) * ratio - viewport.midY
        clamp(size: size, in: viewport)
    }
    mutating func move(by delta: CGPoint, size: CGSize, in viewport: CGRect) {
        guard delta.x.isFinite, delta.y.isFinite else { return }
        pan.x += delta.x; pan.y += delta.y; clamp(size: size, in: viewport)
    }
    private mutating func clamp(size: CGSize, in viewport: CGRect) {
        let rect = imageRect(size: size, in: viewport)
        let x = max(0, (rect.width - viewport.width) / 2), y = max(0, (rect.height - viewport.height) / 2)
        pan.x = min(x, max(-x, pan.x)); pan.y = min(y, max(-y, pan.y))
    }
    static func stickerRect(_ sticker: MediaAssemblySticker, in image: CGRect) -> CGRect {
        let size = sticker.kind.pixelSize
        let factor = min(image.width, image.height) * sticker.size / max(size.width, size.height)
        let width = size.width * factor, height = size.height * factor
        return CGRect(x: image.minX + image.width * sticker.x - width / 2,
                      y: image.minY + image.height * sticker.y - height / 2, width: width, height: height)
    }
    static func unrotate(_ point: CGPoint, around center: CGPoint, degrees: Double) -> CGPoint {
        let a = -degrees * .pi / 180, x = point.x-center.x, y = point.y-center.y
        return CGPoint(x: center.x + x*cos(a)-y*sin(a), y: center.y+x*sin(a)+y*cos(a))
    }
    /// The engine crops upright pixels first, mirrors second, then rotates clockwise.
    static func displayPoint(_ source: CGPoint, quarterTurns: Int, mirrored: Bool) -> CGPoint {
        let p = CGPoint(x: mirrored ? 1-source.x : source.x, y: source.y)
        switch (quarterTurns % 4 + 4) % 4 {
        case 1: return CGPoint(x: 1-p.y, y: p.x)
        case 2: return CGPoint(x: 1-p.x, y: 1-p.y)
        case 3: return CGPoint(x: p.y, y: 1-p.x)
        default: return p
        }
    }
    static func sourcePoint(_ display: CGPoint, quarterTurns: Int, mirrored: Bool) -> CGPoint {
        let p: CGPoint
        switch (quarterTurns % 4 + 4) % 4 {
        case 1: p = CGPoint(x: display.y, y: 1-display.x)
        case 2: p = CGPoint(x: 1-display.x, y: 1-display.y)
        case 3: p = CGPoint(x: 1-display.y, y: display.x)
        default: p = display
        }
        return CGPoint(x: mirrored ? 1-p.x : p.x, y: p.y)
    }
    static func mapRect(_ rect: CGRect, using transform: (CGPoint) -> CGPoint) -> CGRect {
        let points = [CGPoint(x:rect.minX,y:rect.minY),CGPoint(x:rect.maxX,y:rect.minY),CGPoint(x:rect.minX,y:rect.maxY),CGPoint(x:rect.maxX,y:rect.maxY)].map(transform)
        let xs = points.map(\.x), ys = points.map(\.y)
        return CGRect(x:xs.min()!,y:ys.min()!,width:xs.max()!-xs.min()!,height:ys.max()!-ys.min()!)
    }
    static func displayCrop(_ crop: MediaAssemblyCrop, quarterTurns: Int, mirrored: Bool) -> CGRect {
        mapRect(CGRect(x:crop.x,y:crop.y,width:crop.width,height:crop.height)) { displayPoint($0,quarterTurns:quarterTurns,mirrored:mirrored) }
    }
    static func sourceCrop(_ display: CGRect, quarterTurns: Int, mirrored: Bool) -> MediaAssemblyCrop {
        let r = mapRect(display) { sourcePoint($0,quarterTurns:quarterTurns,mirrored:mirrored) }
        let x = min(0.99,max(0,r.minX)),y = min(0.99,max(0,r.minY))
        return MediaAssemblyCrop(x:x,y:y,width:min(max(0.01,r.width),1-x),height:min(max(0.01,r.height),1-y))
    }

}
