import AppKit
import ImageIO

/// The original supplied artwork is retained verbatim. Atlas selections are
/// source rectangles, prepared into small images at build time so opening the
/// picker never decodes the 59-megapixel sheet. Final variants use a bounded cache.
enum HUDApplicationIcon: String, CaseIterable {
    case endfield, battery, originium, orundum, perlica, rhodesIsland, babel, rhineLab, blacksteel, penguinLogistics, ddd
    case eliteOps, alphaFour, alphaSix, humanResources, sweep, projectRed, rimBilliton
    case kjerag, ursusStudents, obsidianFestival, dossoles, contingencyContract, ambienceSynesthesia, coralCoast, vitafield
    case ccBarrenland, ccPyrite, ccCinder, ccLeadSeal, ccSpectrum, ccPineSoot
    case rhodesKitchen, cambrian, marthe, icefieldMessenger

    // Preserve the original faction/resource roster alongside actual Endfield
    // menu artwork, including every previously saved raw value.
    case gameOperator, gameDepot, gameGuide, gameHeadhunt, gameInfo, gameMission
    case gameOperationalManual, gameProtocolPass, gameRegion, gameStore, gameStory
    case gameArchive, gameAIC, gameFactory, gameEnvironmentMonitoring, gameGear
    case gameWeapon, gameWorldMap, gameDijiang, gameValleyIV, gameWuling, gameMedal
    case gamePower, gameSanity, gameOrigeometry, gameOroberyl, gameCredits
    case gameBaker, gameExclamationMark, gameStrength

    static var pickerCases: [HUDApplicationIcon] {
        // Keep the retired hand-drawn currency artwork available for saved
        // preferences, without offering it alongside the verified game assets.
        allCases.filter { $0 != .originium && $0 != .orundum && ($0.gameIcon == nil || $0.gameIcon?.sourceImage() != nil) }
    }

    var gameIcon: EndfieldGameIcon? {
        switch self {
        case .gameOperator: return .operatorProfile
        case .gameDepot: return .depot
        case .gameGuide: return .guide
        case .gameHeadhunt: return .headhunt
        case .gameInfo: return .info
        case .gameMission: return .mission
        case .gameOperationalManual: return .operationalManual
        case .gameProtocolPass: return .protocolPass
        case .gameRegion: return .region
        case .gameStore: return .store
        case .gameStory: return .story
        case .gameArchive: return .archive
        case .gameAIC: return .aic
        case .gameFactory: return .factory
        case .gameEnvironmentMonitoring: return .environmentMonitoring
        case .gameGear: return .gear
        case .gameWeapon: return .weapon
        case .gameWorldMap: return .worldMap
        case .gameDijiang: return .dijiang
        case .gameValleyIV: return .valleyIV
        case .gameWuling: return .wuling
        case .gameMedal: return .medal
        case .gamePower: return .power
        case .gameSanity: return .sanity
        case .gameOrigeometry: return .origeometry
        case .gameOroberyl: return .oroberyl
        case .gameCredits: return .credits
        case .gameBaker: return .baker
        case .gameExclamationMark: return .exclamationMark
        case .gameStrength: return .strength
        default: return nil
        }
    }

    var title: String {
        switch self {
        case .gameOperator: return "Operator"
        case .gameDepot: return "Depot"
        case .gameGuide: return "Guide"
        case .gameHeadhunt: return "Headhunt"
        case .gameInfo: return "Info"
        case .gameMission: return "Mission"
        case .gameOperationalManual: return "Operational Manual"
        case .gameProtocolPass: return "Protocol Pass"
        case .gameRegion: return "Region"
        case .gameStore: return "Store"
        case .gameStory: return "Story"
        case .gameArchive: return "Archive"
        case .gameAIC: return "AIC"
        case .gameFactory: return "Factory"
        case .gameEnvironmentMonitoring: return "Environment Monitoring"
        case .gameGear: return "Gear"
        case .gameWeapon: return "Weapon"
        case .gameWorldMap: return "World Map"
        case .gameDijiang: return "Dijiang"
        case .gameValleyIV: return "Valley IV"
        case .gameWuling: return "Wuling"
        case .gameMedal: return "Medal"
        case .gamePower: return "Power"
        case .gameSanity: return "Sanity"
        case .gameOrigeometry: return "Origeometry"
        case .gameOroberyl: return "Oroberyl"
        case .gameCredits: return "Credits"
        case .gameBaker: return "Baker"
        case .gameExclamationMark: return "Exclamation Mark"
        case .gameStrength: return "Strength"
        case .endfield: return "Endfield"
        case .battery: return L10n.text("Battery", "电池")
        case .originium: return L10n.text("Originium", "源石")
        case .orundum: return L10n.text("Orundum", "合成玉")
        case .perlica: return L10n.text("Perlica", "佩丽卡")
        case .rhodesIsland: return L10n.text("Rhodes Island", "罗德岛")
        case .babel: return L10n.text("Babel", "巴别塔")
        case .rhineLab: return L10n.text("Rhine Lab", "莱茵生命")
        case .blacksteel: return L10n.text("Blacksteel", "黑钢国际")
        case .penguinLogistics: return L10n.text("Penguin Logistics", "企鹅物流")
        case .ddd: return "D.D.D."
        case .eliteOps: return L10n.text("Elite Operators", "精英干员")
        case .alphaFour: return L10n.text("Reserve Team A4", "预备行动组 A4")
        case .alphaSix: return L10n.text("Reserve Team A6", "预备行动组 A6")
        case .humanResources: return L10n.text("Human Resources", "人力资源部")
        case .sweep: return "S.W.E.E.P."
        case .projectRed: return "Project Red"
        case .rimBilliton: return L10n.text("Rim Billiton", "雷姆必拓")
        case .kjerag: return L10n.text("Kjerag", "谢拉格")
        case .ursusStudents: return L10n.text("Ursus Student Group", "乌萨斯学生自治团")
        case .obsidianFestival: return L10n.text("Obsidian Festival", "黑曜石音乐节")
        case .dossoles: return L10n.text("Dossoles", "多索雷斯")
        case .contingencyContract: return L10n.text("Contingency Contract", "危机合约")
        case .ambienceSynesthesia: return L10n.text("Ambience Synesthesia", "音律联觉")
        case .coralCoast: return L10n.text("Coral Coast", "珊瑚海岸")
        case .vitafield: return L10n.text("Vitafield", "生命之地")
        case .ccBarrenland: return L10n.text("Contingency Contract: Barrenland", "危机合约：荒芜行动")
        case .ccPyrite: return L10n.text("Contingency Contract: Pyrite", "危机合约：黄铁行动")
        case .ccCinder: return L10n.text("Contingency Contract: Cinder", "危机合约：燃灰行动")
        case .ccLeadSeal: return L10n.text("Contingency Contract: Lead Seal", "危机合约：铅封行动")
        case .ccSpectrum: return L10n.text("Contingency Contract: Spectrum", "危机合约：光谱行动")
        case .ccPineSoot: return L10n.text("Contingency Contract: Pine Soot", "危机合约：松烟行动")
        case .rhodesKitchen: return L10n.text("Rhodes Kitchen", "罗德厨房")
        case .cambrian: return L10n.text("Cambrian", "寒武纪")
        case .marthe: return L10n.text("Marthe", "玛尔特")
        case .icefieldMessenger: return L10n.text("Icefield Messenger", "冰原信使")
        }
    }

    private var atlasCell: (column: Int, row: Int)? {
        switch self {
        case .babel: return (1, 0)
        case .rhineLab: return (4, 1)
        case .blacksteel: return (5, 1)
        case .penguinLogistics: return (7, 2)
        case .ddd: return (7, 4)
        case .eliteOps: return (3, 0)
        case .alphaFour: return (4, 0)
        case .alphaSix: return (5, 0)
        case .humanResources: return (6, 0)
        case .sweep: return (8, 0)
        case .projectRed: return (9, 0)
        case .rimBilliton: return (6, 1)
        case .kjerag: return (2, 3)
        case .ursusStudents: return (3, 3)
        case .obsidianFestival: return (1, 5)
        case .dossoles: return (3, 5)
        case .contingencyContract: return (2, 5)
        case .ambienceSynesthesia: return (4, 6)
        case .coralCoast: return (7, 7)
        case .vitafield: return (8, 7)
        case .ccBarrenland: return (2, 8)
        case .ccPyrite: return (3, 8)
        case .ccCinder: return (6, 8)
        case .ccLeadSeal: return (8, 8)
        case .ccSpectrum: return (4, 9)
        case .ccPineSoot: return (8, 9)
        case .rhodesKitchen: return (0, 7)
        case .cambrian: return (2, 7)
        case .marthe: return (6, 7)
        case .icefieldMessenger: return (9, 7)
        default: return nil
        }
    }
    private static var sourceCache: [String: CGImage] = [:]
    private static let renderCache: NSCache<NSString, NSImage> = {
        let cache = NSCache<NSString, NSImage>()
        cache.countLimit = 192
        cache.totalCostLimit = 32 * 1024 * 1024
        return cache
    }()
    private static var atlasLoaded = false

    func menuBarImage() -> NSImage {
        // A 20-point canvas leaves native padding in the square status item.
        image(size: 20, menuBar: true)
    }

    func image(size: CGFloat = 64, menuBar: Bool = false) -> NSImage {
        let dimension = max(16, min(1024, Int(size.rounded())))
        let cacheKey = "\(rawValue):\(dimension):\(menuBar)"
        if let cached = Self.renderCache.object(forKey: cacheKey as NSString) { return cached }
        let image: NSImage
        if self == .battery {
            image = Self.batteryImage(size: CGFloat(dimension), menuBar: menuBar)
        } else if self == .originium || self == .orundum {
            image = Self.resourceImage(size: CGFloat(dimension), menuBar: menuBar, crystal: self == .originium)
        } else if let source = sourceImage() {
            image = rendered(source: source, size: dimension, menuBar: menuBar)
        } else {
            image = Self.batteryImage(size: CGFloat(dimension), menuBar: menuBar)
        }
        image.isTemplate = menuBar && self != .perlica
        let pixels = dimension * (menuBar ? 2 : 1)
        Self.renderCache.setObject(image, forKey: cacheKey as NSString, cost: pixels * pixels * 4)
        return image
    }

    private func sourceImage() -> CGImage? {
        if let gameIcon { return gameIcon.sourceImage() }
        if let cached = Self.sourceCache[rawValue] { return cached }
        if atlasCell != nil {
            if let prepared = Self.readSource(name: "AppIconSources/Factions/\(rawValue).png", maximumDimension: 512) {
                Self.sourceCache[rawValue] = prepared
                return prepared
            }
            // Compatibility fallback for a source checkout without prepared assets.
            Self.loadAtlasSelections()
            return Self.sourceCache[rawValue]
        }
        let name: String
        switch self {
        case .endfield: name = "EndfieldIndustriesSource.png"
        case .perlica: name = "AppIconSources/Perlica.png"
        case .rhodesIsland: name = "AppIconSources/RhodesIsland.png"
        default: return nil
        }
        guard let original = Self.readSource(name: name, maximumDimension: 512) else { return nil }
        let detached = Self.detachedImage(original)
        Self.sourceCache[rawValue] = detached
        return detached
    }

    private static func readSource(name: String, maximumDimension: Int) -> CGImage? {
        guard let url = HUDResources.url(for: name),
              let source = CGImageSourceCreateWithURL(url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary) else { return nil }
        return CGImageSourceCreateThumbnailAtIndex(source, 0, [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceThumbnailMaxPixelSize: maximumDimension,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceShouldCacheImmediately: true
        ] as CFDictionary)
    }

    private static func loadAtlasSelections() {
        guard !atlasLoaded else { return }
        atlasLoaded = true
        // The 10 × 14 source has 59 megapixels. One thumbnail decode supplies
        // every 512-pixel emblem; no later picker hover/status update rereads it.
        guard let original = readSource(name: "AppIconSources/FactionAtlas.png", maximumDimension: 512 * 14) else { return }
        let unit = CGFloat(original.width) / 10
        let inset = unit * 5 / 649.7
        for preset in allCases {
            guard let cell = preset.atlasCell else { continue }
            let rect = CGRect(x: CGFloat(cell.column) * unit + inset,
                              y: CGFloat(cell.row) * unit + inset,
                              width: unit - inset * 2, height: unit - inset * 2).integral
            guard let crop = original.cropping(to: rect) else { continue }
            sourceCache[preset.rawValue] = detachedImage(crop)
        }
    }

    private static func detachedImage(_ selection: CGImage) -> CGImage? {
        // Crops can retain the whole sheet; draw only the selected emblem into
        // independent storage so the sheet is released after the batch load.
        let scale = min(1, 512 / CGFloat(max(selection.width, selection.height)))
        let width = max(1, Int(CGFloat(selection.width) * scale)), height = max(1, Int(CGFloat(selection.height) * scale))
        guard let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
            bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        context.interpolationQuality = .high
        context.draw(selection, in: CGRect(x: 0, y: 0, width: width, height: height))
        return context.makeImage()
    }

    private func rendered(source: CGImage, size: Int, menuBar: Bool) -> NSImage {
        let pixels = size * (menuBar ? 2 : 1)
        let space = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8,
            bytesPerRow: pixels * 4, space: space,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return Self.batteryImage(size: CGFloat(size), menuBar: menuBar) }
        context.interpolationQuality = .high
        let bounds = CGRect(x: 0, y: 0, width: pixels, height: pixels)
        if !menuBar {
            context.addPath(CGPath(roundedRect: bounds.insetBy(dx: CGFloat(pixels) * 0.035, dy: CGFloat(pixels) * 0.035),
                cornerWidth: CGFloat(pixels) * 0.19, cornerHeight: CGFloat(pixels) * 0.19, transform: nil))
            context.clip()
            context.setFillColor(NSColor(white: 0.055, alpha: 1).cgColor); context.fill(bounds)
        }
        let margin: CGFloat = self == .perlica ? (menuBar ? 0 : 0.035) : (menuBar ? 0.025 : 0.07)
        let target = bounds.insetBy(dx: CGFloat(pixels) * margin, dy: CGFloat(pixels) * margin)
        let ratio = min(target.width / CGFloat(source.width), target.height / CGFloat(source.height))
        let destination = CGRect(x: target.midX - CGFloat(source.width) * ratio / 2,
            y: target.midY - CGFloat(source.height) * ratio / 2,
            width: CGFloat(source.width) * ratio, height: CGFloat(source.height) * ratio)
        context.draw(source, in: destination)
        if menuBar && self != .perlica, let bytes = context.data?.assumingMemoryBound(to: UInt8.self) {
            // Template images use alpha alone. Convert white artwork on the
            // supplied black sheets into an adaptive macOS menu-bar silhouette.
            for i in stride(from: 0, to: pixels * pixels * 4, by: 4) {
                bytes[i + 3] = max(bytes[i], max(bytes[i + 1], bytes[i + 2]))
                bytes[i] = 0; bytes[i + 1] = 0; bytes[i + 2] = 0
            }
        }
        guard let cg = context.makeImage() else { return Self.batteryImage(size: CGFloat(size), menuBar: menuBar) }
        return NSImage(cgImage: cg, size: NSSize(width: size, height: size))
    }

    /// Original vector interpretations of the two resource items; the supplied
    /// faction sheet contains emblems rather than currency artwork.
    private static func resourceImage(size: CGFloat, menuBar: Bool, crystal: Bool) -> NSImage {
        NSImage(size: NSSize(width: size, height: size), flipped: false) { _ in
            NSGraphicsContext.saveGraphicsState(); defer { NSGraphicsContext.restoreGraphicsState() }
            let transform = NSAffineTransform(); transform.scale(by: size / 100); transform.concat()
            func polygon(_ points: [CGPoint], _ color: NSColor) {
                guard let first = points.first else { return }
                let path = NSBezierPath(); path.move(to: first)
                points.dropFirst().forEach { path.line(to: $0) }; path.close(); color.setFill(); path.fill()
            }
            if !menuBar {
                NSColor(white: 0.055, alpha: 1).setFill()
                NSBezierPath(roundedRect: CGRect(x: 3.5, y: 3.5, width: 93, height: 93), xRadius: 19, yRadius: 19).fill()
            }
            let ink = menuBar ? NSColor.black : (crystal
                ? NSColor(srgbRed: 0.98, green: 0.66, blue: 0.18, alpha: 1)
                : NSColor(srgbRed: 0.91, green: 0.20, blue: 0.27, alpha: 1))
            if crystal {
                polygon([CGPoint(x: 17, y: 24), CGPoint(x: 14, y: 56), CGPoint(x: 24, y: 74),
                         CGPoint(x: 35, y: 55), CGPoint(x: 34, y: 21)], ink)
                polygon([CGPoint(x: 38, y: 18), CGPoint(x: 36, y: 66), CGPoint(x: 52, y: 91),
                         CGPoint(x: 64, y: 67), CGPoint(x: 60, y: 20)], ink)
                polygon([CGPoint(x: 65, y: 20), CGPoint(x: 68, y: 55), CGPoint(x: 82, y: 73),
                         CGPoint(x: 87, y: 50), CGPoint(x: 79, y: 25)], ink)
                if !menuBar {
                    let lit = NSColor(srgbRed: 1, green: 0.90, blue: 0.50, alpha: 1)
                    polygon([CGPoint(x: 24, y: 69), CGPoint(x: 20, y: 54), CGPoint(x: 22, y: 29), CGPoint(x: 26, y: 52)], lit)
                    polygon([CGPoint(x: 51, y: 85), CGPoint(x: 42, y: 63), CGPoint(x: 44, y: 25), CGPoint(x: 49, y: 60)], lit)
                    polygon([CGPoint(x: 81, y: 67), CGPoint(x: 72, y: 52), CGPoint(x: 71, y: 27), CGPoint(x: 77, y: 48)], lit)
                }
            } else {
                polygon([CGPoint(x: 14, y: 48), CGPoint(x: 32, y: 78), CGPoint(x: 69, y: 80),
                         CGPoint(x: 88, y: 49), CGPoint(x: 69, y: 22), CGPoint(x: 33, y: 20)], ink)
                if !menuBar {
                    polygon([CGPoint(x: 18, y: 49), CGPoint(x: 34, y: 74), CGPoint(x: 65, y: 76),
                             CGPoint(x: 51, y: 52)], NSColor(srgbRed: 1, green: 0.51, blue: 0.51, alpha: 1))
                    polygon([CGPoint(x: 52, y: 48), CGPoint(x: 83, y: 49), CGPoint(x: 67, y: 26),
                             CGPoint(x: 36, y: 24)], NSColor(srgbRed: 0.64, green: 0.10, blue: 0.18, alpha: 1))
                } else if let context = NSGraphicsContext.current?.cgContext {
                    // Facet seams remain clear in a template, not white ink.
                    context.setBlendMode(.clear); context.setLineWidth(3.5)
                    context.move(to: CGPoint(x: 32, y: 75)); context.addLine(to: CGPoint(x: 51, y: 50))
                    context.addLine(to: CGPoint(x: 68, y: 24)); context.move(to: CGPoint(x: 18, y: 48))
                    context.addLine(to: CGPoint(x: 84, y: 49)); context.strokePath()
                }
            }
            return true
        }
    }

    private static func batteryImage(size: CGFloat, menuBar: Bool) -> NSImage {
        NSImage(size: NSSize(width: size, height: size), flipped: false) { _ in
            NSGraphicsContext.saveGraphicsState(); defer { NSGraphicsContext.restoreGraphicsState() }
            let transform = NSAffineTransform(); transform.scale(by: size / 18); transform.concat()
            if !menuBar {
                let original = NSAffineTransform(); original.scale(by: 18 / 512); original.concat()
                NSColor(srgbRed: 0.075, green: 0.09, blue: 0.085, alpha: 1).setFill()
                NSBezierPath(roundedRect: CGRect(x: 24, y: 24, width: 464, height: 464), xRadius: 106, yRadius: 106).fill()
                let accent = NSColor(srgbRed: 217/255, green: 243/255, blue: 107/255, alpha: 1)
                let ring = NSBezierPath(ovalIn: CGRect(x: 104, y: 104, width: 304, height: 304)); ring.lineWidth = 20
                accent.withAlphaComponent(0.16).setStroke(); ring.stroke()
                let progress = NSBezierPath(); progress.appendArc(withCenter: CGPoint(x: 256, y: 256), radius: 152,
                    startAngle: 90, endAngle: -185, clockwise: true)
                progress.lineWidth = 20; progress.lineCapStyle = .round; accent.setStroke(); progress.stroke()
                let bolt = NSBezierPath(); bolt.move(to: CGPoint(x: 280, y: 368))
                for p in [CGPoint(x: 187, y: 240), CGPoint(x: 245, y: 240), CGPoint(x: 226, y: 148), CGPoint(x: 327, y: 283), CGPoint(x: 267, y: 283)] { bolt.line(to: p) }
                bolt.close(); accent.setFill(); bolt.fill(); return true
            }
            let color = NSColor.black
            color.setStroke(); color.setFill()
            let outline = NSBezierPath(roundedRect: CGRect(x: 1.5, y: 4, width: 14, height: 10), xRadius: 2, yRadius: 2)
            outline.lineWidth = 1.3; outline.stroke()
            NSBezierPath(roundedRect: CGRect(x: 16, y: 7, width: 1.5, height: 4), xRadius: 0.5, yRadius: 0.5).fill()
            let bolt = NSBezierPath(); bolt.move(to: CGPoint(x: 9.8, y: 13))
            for p in [CGPoint(x: 5.8, y: 8.5), CGPoint(x: 8.3, y: 8.5), CGPoint(x: 7.4, y: 5), CGPoint(x: 11.4, y: 9.5), CGPoint(x: 8.9, y: 9.5)] { bolt.line(to: p) }
            bolt.close(); bolt.fill(); return true
        }
    }
}
