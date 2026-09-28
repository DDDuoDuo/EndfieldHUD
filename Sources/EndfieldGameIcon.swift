import AppKit
import ImageIO
import QuartzCore

/// Original, bundled Endfield UI artwork. Source and copyright are recorded in
/// Resources/AppIconSources/EndfieldWiki/SOURCES.json; no network is used at runtime.
enum EndfieldGameIcon: String, CaseIterable {
    case operatorProfile = "Operator_icon"
    case depot = "Depot_icon"
    case guide = "Guide_icon"
    case headhunt = "Headhunt_icon"
    case info = "Info_icon"
    case mission = "Mission_Icon"
    case operationalManual = "Operational_Manual_icon"
    case protocolPass = "PP_icon"
    case region = "Region_icon"
    case store = "Store_icon"
    case story = "Story_icon"
    case archive = "Archive_Icon"
    case aic = "AIC_Icon"
    case factory = "Factory_icon"
    case environmentMonitoring = "Environment_Monitoring"
    case gear = "Gear_icon"
    case weapon = "Weapon_icon"
    case worldMap = "World_Map_Icon"
    case dijiang = "Dijiang_Icon"
    case valleyIV = "Valley_IV_icon"
    case wuling = "Wuling_icon"
    case medal = "Medal_Icon"
    case power = "Power_icon"
    case sanity = "Sanity_icon"
    case origeometry = "Origeometry_icon"
    case oroberyl = "Oroberyl_icon"
    case credits = "Credits_icon"
    case baker = "Baker_Icon"
    case exclamationMark = "Exclamation_mark_map_icon"
    case strength = "STR"

    private static var sources: [String: CGImage] = [:]
    private static let variants: NSCache<NSString, NSImage> = {
        let cache = NSCache<NSString, NSImage>()
        cache.countLimit = 192
        cache.totalCostLimit = 12 * 1_024 * 1_024
        return cache
    }()

    func sourceImage() -> CGImage? {
        if let cached = Self.sources[rawValue] { return cached }
        let relative = "AppIconSources/EndfieldWiki/\(rawValue).png"
        guard let url = HUDResources.url(for: relative),
              let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, [kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else { return nil }
        Self.sources[rawValue] = image
        return image
    }

    func image(size: CGFloat = 64, tint: NSColor? = nil) -> NSImage {
        let pixels = max(8, min(1_024, Int(size.rounded(.up))))
        let rgb = tint?.usingColorSpace(.deviceRGB)
        let colorKey = rgb.map { "\($0.redComponent):\($0.greenComponent):\($0.blueComponent):\($0.alphaComponent)" } ?? "original"
        let key = "\(rawValue):\(pixels):\(colorKey)" as NSString
        if let cached = Self.variants.object(forKey: key) { return cached }
        guard let source = sourceImage(),
              let context = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8,
                bytesPerRow: pixels * 4, space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return NSImage(size: NSSize(width: pixels, height: pixels)) }
        let bounds = CGRect(x: 0, y: 0, width: pixels, height: pixels)
        let ratio = min(bounds.width / CGFloat(source.width), bounds.height / CGFloat(source.height))
        let rect = CGRect(x: (bounds.width - CGFloat(source.width) * ratio) / 2,
                          y: (bounds.height - CGFloat(source.height) * ratio) / 2,
                          width: CGFloat(source.width) * ratio, height: CGFloat(source.height) * ratio)
        context.interpolationQuality = .high
        context.draw(source, in: rect)
        if let tint = rgb {
            context.setBlendMode(.sourceIn)
            context.setFillColor(tint.cgColor)
            context.fill(bounds)
        }
        guard let cg = context.makeImage() else { return NSImage(size: bounds.size) }
        let rendered = NSImage(cgImage: cg, size: bounds.size)
        Self.variants.setObject(rendered, forKey: key, cost: pixels * pixels * 4)
        return rendered
    }

    func cgImage(size: CGFloat = 64, tint: NSColor? = nil) -> CGImage? {
        guard sourceImage() != nil else { return nil }
        return image(size: size, tint: tint).cgImage(forProposedRect: nil, context: nil, hints: nil)
    }

    @discardableResult
    func add(to parent: CALayer, rect: CGRect, tint: NSColor, contentsScale: CGFloat) -> Bool {
        guard let image = cgImage(size: max(rect.width, rect.height) * contentsScale, tint: tint) else { return false }
        let layer = CALayer()
        layer.name = "endfield.icon.\(rawValue)"
        layer.frame = rect
        layer.contents = image
        layer.contentsScale = contentsScale
        layer.contentsGravity = .resizeAspect
        parent.addSublayer(layer)
        return true
    }
}
