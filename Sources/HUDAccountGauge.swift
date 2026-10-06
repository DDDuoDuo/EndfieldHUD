import AppKit
import ImageIO
import QuartzCore

/// Original MoneyCell artwork and a retained recovery tooltip. The HUD's
/// existing visible clock supplies updates; this object owns no timer.
final class HUDAccountGauge {
    struct Action {
        let id: String, label: String, rect: CGRect, enabled: Bool
    }
    let layer = CALayer()
    private let number = CALayer()
    private let feedback: HUDControlHighlightLayer
    private let popover = CALayer()
    private let nextLabel = CALayer(), fullLabel = CALayer()
    private let nextValue = CALayer(), fullValue = CALayer()
    private let refresh = CAShapeLayer()
    private let refreshFeedback: HUDControlHighlightLayer
    private var presentation: HypergryphSanityPresentation?
    private var date = Date()
    private var renderedTooltip = ""
    private var tooltipContentKeys = [String](repeating: "", count: 4)
    var onRefresh: (() -> Void)?
    var onChange: (() -> Void)?
    private(set) var isPopoverOpen = false
    private(set) var value = ""
    private(set) var accessibilityLabel = ""
    private(set) var updateCount = 0
    private var renderScale: CGFloat = 0
    static let size = CGSize(width: 168, height: 42)
    // Expand left from the wallet's right edge, away from the adjacent clock.
    static let popoverRect = CGRect(x: size.width - 224, y: 40, width: 224, height: 65)
    static let refreshRect = CGRect(x: 195, y: 19, width: 23, height: 25)
    // Mirror the heading's x=270 inset around the 1,000-point HUD plane.
    static let headerPosition = CGPoint(x: 292, y: 0)
    static var sourceNumberFont: String? { Numerals.font.map { $0.family + " " + $0.style } }
    private(set) var numberRenderCount = 0
    private(set) var tooltipRenderCount = 0
    private(set) var tooltipRasterCount = 0
    private(set) var nextRecoveryText = "", fullRecoveryText = ""
    var canOpen: Bool { presentation != nil && !layer.isHidden }
    var accessibleActions: [Action] {
        guard canOpen else { return [] }
        var actions = [Action(id: "toggle", label: accessibilityLabel, rect: CGRect(origin: .zero, size: Self.size), enabled: true)]
        if isPopoverOpen {
            actions.append(Action(id: "refresh", label: L10n.text("Refresh", "刷新"),
                rect: Self.refreshRect.offsetBy(dx: Self.popoverRect.minX, dy: Self.popoverRect.minY),
                enabled: presentation?.refreshAvailable == true && presentation?.isRefreshing != true))
        }
        return actions
    }

    init() {
        layer.name = "hud.account.stamina"; layer.bounds = CGRect(origin: .zero, size: Self.size)
        layer.anchorPoint = .zero; layer.isHidden = true
        let barRect = CGRect(x: 0, y: 6, width: 168, height: 30)
        for (name, image, border) in [("back", Artwork.background, CGFloat(29)), ("deco", Artwork.decoration, CGFloat(28))] {
            let bar = CALayer(); bar.name = "hud.account.stamina." + name; bar.frame = barRect
            bar.contents = image; bar.contentsGravity = .resize
            bar.contentsCenter = CGRect(x: border / 60, y: 0, width: (60 - border * 2) / 60, height: 1)
            layer.addSublayer(bar)
        }
        let icon = CALayer(); icon.name = "hud.account.stamina.item_ap"
        icon.frame = CGRect(x: 3, y: -6, width: 54, height: 54); icon.contents = Artwork.icon; icon.contentsGravity = .resizeAspect; layer.addSublayer(icon)
        number.name = "hud.account.stamina.value"; number.frame = CGRect(x: 39, y: 6, width: 121, height: 30)
        number.contentsGravity = .resize; number.actions = ["contents": NSNull()]; layer.addSublayer(number)
        feedback = HUDControlHighlightLayer.add(to: layer, rect: barRect, shape: .rounded)
        if let silhouette = Artwork.backgroundMask {
            feedback.useAlphaSilhouette(silhouette,
                contentsCenter: CGRect(x: 29.0 / 60, y: 0, width: 2.0 / 60, height: 1))
        }
        feedback.accentOverride = NSColor(white: 0.75, alpha: 1)
        popover.name = "hud.account.stamina.recovery"; popover.frame = Self.popoverRect
        popover.backgroundColor = NSColor(white: 0.065, alpha: 0.98).cgColor
        popover.cornerRadius = 4; popover.isHidden = true; popover.zPosition = 10
        layer.addSublayer(popover)
        for (item, frame) in [(nextLabel, CGRect(x: 9, y: 6, width: 99, height: 25)),
                              (fullLabel, CGRect(x: 9, y: 33, width: 99, height: 25)),
                              (nextValue, CGRect(x: 109, y: 6, width: 83, height: 25)),
                              (fullValue, CGRect(x: 109, y: 33, width: 83, height: 25))] {
            item.frame = frame; item.contentsGravity = .resize; item.actions = ["contents": NSNull()]; popover.addSublayer(item)
        }
        refresh.name = "hud.account.stamina.refresh"
        refresh.bounds = CGRect(x: 0, y: 0, width: 22, height: 22)
        refresh.position = CGPoint(x: Self.refreshRect.midX, y: Self.refreshRect.midY)
        refresh.fillColor = NSColor(white: 0.9, alpha: 1).cgColor
        // Same centered, filled clockwise-arrow geometry as Storage refresh.
        let shaft = CGMutablePath(), center = CGPoint(x: 11, y: 11), radius: CGFloat = 6.2
        shaft.addArc(center: center, radius: radius, startAngle: .pi / 3,
                     endAngle: .pi * 65 / 36, clockwise: false)
        let path = CGMutablePath()
        path.addPath(shaft.copy(strokingWithWidth: 1.7, lineCap: .round,
                               lineJoin: .round, miterLimit: 1))
        path.move(to: CGPoint(x: 18.05, y: 10.1))
        path.addLine(to: CGPoint(x: 13.25, y: 10.1))
        path.addLine(to: CGPoint(x: 18.05, y: 5.3)); path.closeSubpath()
        refresh.path = path
        popover.addSublayer(refresh)
        refreshFeedback = HUDControlHighlightLayer.add(to: popover, rect: Self.refreshRect, shape: .rounded)
        refreshFeedback.accentOverride = NSColor(white: 0.75, alpha: 1)
    }

    func update(value next: String, accessibilityLabel label: String, visible: Bool, accent: NSColor, scale: CGFloat,
                sanity: HypergryphSanityPresentation? = nil, at date: Date = Date()) {
        let scale = scale.isFinite ? min(3, max(1, scale)) : 2
        let next = String(next.prefix(24)), wasAvailable = canOpen
        presentation = sanity; self.date = date
        if value != next || accessibilityLabel != label || layer.isHidden == visible || renderScale != scale {
            CATransaction.begin(); CATransaction.setDisableActions(true)
            if value != next || renderScale != scale {
                number.contents = Numerals.render(next, in: number.bounds.size, scale: scale)
                number.contentsScale = scale; numberRenderCount += 1
            }
            value = next; accessibilityLabel = label; renderScale = scale; updateCount += 1
            layer.isHidden = !visible
            CATransaction.commit()
        }
        if !canOpen { dismiss(animated: false) }
        if isPopoverOpen { renderTooltip() }
        if wasAvailable != canOpen { onChange?() }
    }

    func perform(_ id: String) {
        guard canOpen else { return }
        if id == "toggle" { setOpen(!isPopoverOpen) }
        else if id == "refresh", isPopoverOpen, presentation?.refreshAvailable == true,
                presentation?.isRefreshing != true { onRefresh?() }
    }
    @discardableResult func mouseDown(at point: CGPoint?) -> Bool {
        guard canOpen else { return false }
        if let point, CGRect(origin: .zero, size: Self.size).contains(point) { perform("toggle"); return true }
        if isPopoverOpen {
            if let point, Self.popoverRect.contains(point) {
                if Self.refreshRect.offsetBy(dx: Self.popoverRect.minX, dy: Self.popoverRect.minY).contains(point) { perform("refresh") }
            } else { dismiss() }
            return true
        }
        return false
    }
    func hover(at point: CGPoint?) {
        HUDControlHighlightLayer.update(in: layer, point: canOpen ? point : nil)
    }
    func dismiss(animated: Bool = true) { setOpen(false, animated: animated) }
    private func setOpen(_ value: Bool, animated: Bool = true) {
        guard isPopoverOpen != value else {
            if !value && !animated { popover.removeAllAnimations(); popover.isHidden = true; hover(at: nil) }
            return
        }
        isPopoverOpen = value
        if value { renderTooltip() }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        popover.removeAllAnimations(); popover.isHidden = false; popover.opacity = value ? 1 : 0
        if animated && !HUDRuntimeAppearance.reduceMotion {
            let alpha = CABasicAnimation(keyPath: "opacity"); alpha.fromValue = value ? 0 : 1; alpha.toValue = value ? 1 : 0
            let shift = CABasicAnimation(keyPath: "transform.translation.y"); shift.fromValue = value ? -5 : 0; shift.toValue = value ? 0 : -3
            let group = CAAnimationGroup(); group.animations = [alpha, shift]; group.duration = 0.16
            group.timingFunction = CAMediaTimingFunction(name: .easeOut); popover.add(group, forKey: "account.gauge.menu")
        } else { popover.isHidden = !value }
        CATransaction.commit(); onChange?()
    }
    static func countdown(until deadline: Date?, at date: Date, hours: Bool) -> String {
        guard let deadline else { return "—" }
        let remaining = deadline.timeIntervalSince(date)
        guard remaining.isFinite else { return "—" }
        let total = Int(min(359_999_999, max(0, ceil(remaining))))
        if hours { return String(format: "%02d:%02d:%02d", total / 3600, total / 60 % 60, total % 60) }
        return String(format: "%02d:%02d", total / 60, total % 60)
    }
    private func renderTooltip() {
        guard let presentation else { return }
        let colon = L10n.isCJK ? "：" : ":"
        let first = L10n.text("Next recovery", "下次回复") + colon
        let second = L10n.text("Full recovery", "全部回复") + colon
        nextRecoveryText = Self.countdown(until: presentation.nextRecoveryAt, at: date, hours: false)
        fullRecoveryText = Self.countdown(until: presentation.fullRecoveryAt, at: date, hours: true)
        let key = [first, second, nextRecoveryText, fullRecoveryText, String(Double(renderScale)), String(presentation.refreshAvailable), String(presentation.isRefreshing)].joined(separator: "|")
        guard key != renderedTooltip else { return }
        renderedTooltip = key; tooltipRenderCount += 1
        CATransaction.begin(); CATransaction.setDisableActions(true)
        for (index, pair) in [(nextLabel, first), (fullLabel, second), (nextValue, nextRecoveryText), (fullValue, fullRecoveryText)].enumerated() {
            let (item, text) = pair, contentKey = text + "|" + String(Double(renderScale))
            guard tooltipContentKeys[index] != contentKey else { continue }
            item.contents = Numerals.render(text, in: item.bounds.size, scale: renderScale, preferredSize: 14.4, rightAligned: false)
                ?? localizedText(text, size: item.bounds.size, scale: renderScale)
            item.contentsScale = renderScale; tooltipContentKeys[index] = contentKey; tooltipRasterCount += 1
        }
        refresh.contentsScale = renderScale
        let enabled = presentation.refreshAvailable && !presentation.isRefreshing
        refresh.opacity = enabled ? 1 : 0.38; refreshFeedback.setEnabled(enabled)
        CATransaction.commit(); onChange?()
    }
    private func localizedText(_ text: String, size: CGSize, scale: CGFloat) -> CGImage? {
        // The source SC atlas lacks several JP/TW/KR glyphs. Keep translations
        // legible using native locale fallback; numbers remain original glyphs.
        let image = NSImage(size: size)
        image.lockFocus()
        (text as NSString).draw(in: CGRect(x: 0, y: 3, width: size.width, height: size.height),
            withAttributes: [.font: NSFont.systemFont(ofSize: 14.4, weight: .medium), .foregroundColor: NSColor(white: 0.92, alpha: 1)])
        image.unlockFocus(); return image.cgImage(forProposedRect: nil, context: nil, hints: nil)
    }
    static var sourceArtworkAvailable: Bool { Artwork.icon != nil && Artwork.background != nil && Artwork.decoration != nil }
    /// The source wallet uses HarmonyOS Sans SC Medium, not a monospaced
    /// macOS font. Its compact original SDF glyphs cover the wallet and tooltip. Keep
    /// one tiny raster per changed value; no font atlas, timer or render loop.
    private enum Numerals {
        struct Glyph: Decodable {
            let character: String
            let width, height, bearingX, bearingY, advance: Double
            let columns, rows: Int
            let samples: Data
        }
        struct Font: Decodable {
            let schema: Int
            let family, style: String
            let pointSize, padding, gradientScale: Double
            let glyphs: [Glyph]
        }
        static let font: Font? = {
            guard let url = HUDResources.url(for: "WatchSource/Scene/account-numerals.json"),
                  let data = try? HUDSourceResourceData.read(url), data.count <= 65_536,
                  let value = try? JSONDecoder().decode(Font.self, from: data), value.schema == 1,
                  value.pointSize == 42, value.padding == 4, value.gradientScale == 5,
                  value.family == "HarmonyOS Sans SC", value.style == "Medium",
                  Set(value.glyphs.map(\.character)) == Set("0123456789/ —:：下次回复全部Next recoveryFull".map(String.init)),
                  value.glyphs.allSatisfy({ glyph in
                      [glyph.width, glyph.height, glyph.bearingX, glyph.bearingY, glyph.advance].allSatisfy { $0.isFinite && abs($0) <= 64 }
                      && glyph.advance > 0 && (0...64).contains(glyph.columns) && (0...64).contains(glyph.rows)
                      && glyph.samples.count == glyph.columns * glyph.rows
                  }) else { return nil }
            return value
        }()

        static func render(_ text: String, in size: CGSize, scale: CGFloat, preferredSize: Double = 18, rightAligned: Bool = true) -> CGImage? {
            guard let font, !text.isEmpty else { return nil }
            let glyphs = text.compactMap { character in font.glyphs.first { $0.character == String(character) } }
            guard glyphs.count == text.count else { return nil }
            let width = Int(ceil(size.width * scale)), height = Int(ceil(size.height * scale))
            let advance = glyphs.reduce(0) { $0 + $1.advance }
            // Source Text autosizes between 18–30 points, then the complete
            // wallet is displayed at 0.6×. Fit by source advances, not count.
            // Extreme local work durations can shrink further to stay readable.
            let pointSize = min(preferredSize, max(4, (Double(size.width) - 2) * font.pointSize / advance))
            let factor = pointSize / font.pointSize * Double(scale)
            let ascent = glyphs.map(\.bearingY).max() ?? 0
            let descent = glyphs.map { $0.bearingY - $0.height }.min() ?? 0
            let baseline = (Double(height) - (ascent - descent) * factor) / 2 + ascent * factor
            var x = rightAligned ? Double(width) - advance * factor : 0, pixels = [UInt8](repeating: 0, count: width * height * 4)
            for glyph in glyphs {
                defer { x += glyph.advance * factor }
                guard glyph.columns > 0, glyph.rows > 0, glyph.width > 0, glyph.height > 0 else { continue }
                let left = x + (glyph.bearingX - font.padding) * factor
                let top = baseline - (glyph.bearingY + font.padding) * factor
                let drawnWidth = (glyph.width + 2 * font.padding) * factor
                let drawnHeight = (glyph.height + 2 * font.padding) * factor
                let bytes = [UInt8](glyph.samples)
                let minX = max(0, Int(floor(left))), maxX = min(width, Int(ceil(left + drawnWidth)))
                let minY = max(0, Int(floor(top))), maxY = min(height, Int(ceil(top + drawnHeight)))
                guard minX < maxX, minY < maxY else { continue }
                for py in minY..<maxY {
                    for px in minX..<maxX {
                        let sx = min(Double(glyph.columns - 1), max(0, (Double(px) + 0.5 - left) / drawnWidth * Double(glyph.columns) - 0.5))
                        let sy = min(Double(glyph.rows - 1), max(0, (Double(py) + 0.5 - top) / drawnHeight * Double(glyph.rows) - 0.5))
                        let x0 = Int(sx), y0 = Int(sy), x1 = min(x0 + 1, glyph.columns - 1), y1 = min(y0 + 1, glyph.rows - 1)
                        let dx = sx - Double(x0), dy = sy - Double(y0)
                        let upper = Double(bytes[y0 * glyph.columns + x0]) * (1 - dx) + Double(bytes[y0 * glyph.columns + x1]) * dx
                        let lower = Double(bytes[y1 * glyph.columns + x0]) * (1 - dx) + Double(bytes[y1 * glyph.columns + x1]) * dx
                        let distance = (upper * (1 - dy) + lower * dy) / 255 - 0.5
                        let alpha = UInt8((min(1, max(0, distance * font.gradientScale * factor + 0.5)) * 255).rounded())
                        let offset = (py * width + px) * 4
                        for channel in 0..<4 { pixels[offset + channel] = max(pixels[offset + channel], alpha) }
                    }
                }
            }
            guard let provider = CGDataProvider(data: Data(pixels) as CFData), let colorSpace = CGColorSpace(name: CGColorSpace.sRGB) else { return nil }
            return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                           bytesPerRow: width * 4, space: colorSpace,
                           bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                           provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
        }
    }

    private enum Artwork {
        static let icon = load("item_ap--2524b69d--8210737671276829403.png", crop: CGRect(x: 1, y: 3, width: 80, height: 80))
        static let backgroundMask = load("bg_walletbar_1--cfe92272--8572312840272182184.png", crop: CGRect(x: 1, y: 1, width: 60, height: 50))
        static let background = tinted(backgroundMask, white: 0.2666666806, alpha: 0.6980392337)
        static let decoration = tinted(load("bg_walletbar_2--cfe92272--7683125525266349835.png", crop: CGRect(x: 1, y: 1, width: 60, height: 50)), white: 0.2117647082, alpha: 0.1019607857)
        private static func load(_ name: String, crop: CGRect) -> CGImage? {
            guard let url = HUDResources.url(for: "WatchSource/Scene/sprites/source-textures/" + name),
                  let source = CGImageSourceCreateWithURL(url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary),
                  let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
                  let width = properties[kCGImagePropertyPixelWidth] as? Int,
                  let height = properties[kCGImagePropertyPixelHeight] as? Int, width > 0, height > 0, width <= 128, height <= 128,
                  let image = CGImageSourceCreateImageAtIndex(source, 0, [kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else { return nil }
            return image.cropping(to: crop)
        }
        private static func tinted(_ image: CGImage?, white: CGFloat, alpha: CGFloat) -> CGImage? {
            guard let image, let space = CGColorSpace(name: CGColorSpace.sRGB),
                  let context = CGContext(data: nil, width: image.width, height: image.height, bitsPerComponent: 8,
                      bytesPerRow: image.width * 4, space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
            let rect = CGRect(x: 0, y: 0, width: image.width, height: image.height)
            context.draw(image, in: rect); context.setBlendMode(.sourceIn)
            context.setFillColor(NSColor(white: white, alpha: alpha).cgColor); context.fill(rect)
            return context.makeImage()
        }
    }
}
