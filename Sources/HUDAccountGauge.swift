import AppKit
import ImageIO
import QuartzCore

/// Original MoneyCell artwork and a retained recovery tooltip. Its numbers and labels are real text (HarmonyOS Sans
/// Medium through HUDText), drawn at the HUD's render scale. The HUD's
/// existing visible clock supplies updates; this object owns no timer.
final class HUDAccountGauge {
    struct Action {
        let id: String, label: String, rect: CGRect, enabled: Bool
    }
    let layer = CALayer()
    private let icon = CALayer(), number = CALayer()
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
    /// Opens the Work Mode module when the bar shows Work Mode time.
    var onOpenWorkMode: (() -> Void)?
    private(set) var isPopoverOpen = false
    private(set) var value = ""
    private(set) var accessibilityLabel = ""
    private(set) var updateCount = 0
    /// Pixels per point of the drawn number and tooltip text.
    private(set) var renderScale: CGFloat = 0
    /// The HUD's backing x design scale, from the last `update` or `setScale`.
    private var baseScale: CGFloat = 2
    /// The gauge's drawn size relative to its 168 x 42 design. A placement
    /// that scales the gauge sets this, so the text is redrawn at
    /// the new size instead of being stretched.
    var sizeFactor: CGFloat = 1 {
        didSet {
            let clamped = sizeFactor.isFinite ? min(4, max(0.25, sizeFactor)) : 1
            if sizeFactor != clamped { sizeFactor = clamped }
            if sizeFactor != oldValue { setScale(baseScale) }
        }
    }
    /// The raster scale for the current base scale and size, by the HUD's
    /// glyph rule (the tilted plane's oversample, capped like other text).
    var targetScale: CGFloat { HUDRenderScale.glyphScale(baseScale: baseScale * sizeFactor) }
    static let size = CGSize(width: 168, height: 42)
    /// The recovery menu grows left from the wallet's right edge, away from
    /// the adjacent clock, to fit its labels and values (`Layout`).
    private(set) var popoverRect = Layout.frame(width: Layout.minimumWidth)
    private var refreshRect: CGRect { Layout.refresh(width: popoverRect.width) }
    // Mirror the heading's x=270 inset around the 1,000-point HUD plane.
    static let headerPosition = CGPoint(x: 292, y: 0)
    /// The face that draws the value and the recovery text.
    static var sourceNumberFont: String? { CTFontCopyFullName(HUDText.font(numberStyle, language: HUDTextLanguage.tag())) as String }
    private static var numberStyle: HUDTextStyle { HUDTextStyle(size: 18, weight: .medium, color: NSColor.white.cgColor) }
    private(set) var numberRenderCount = 0
    private(set) var tooltipRenderCount = 0
    private(set) var tooltipRasterCount = 0
    private(set) var nextRecoveryText = "", fullRecoveryText = ""
    var canOpen: Bool { presentation != nil && !layer.isHidden }
    /// Without sanity data the bar shows Work Mode time and acts as a link to Work Mode.
    var opensWorkMode: Bool { presentation == nil && !layer.isHidden }
    var isInteractive: Bool { canOpen || opensWorkMode }
    var accessibleActions: [Action] {
        guard isInteractive else { return [] }
        var actions = [Action(id: "toggle", label: accessibilityLabel, rect: CGRect(origin: .zero, size: Self.size), enabled: true)]
        if isPopoverOpen {
            actions.append(Action(id: "refresh", label: L10n.text("Refresh", "刷新"),
                rect: refreshRect.offsetBy(dx: popoverRect.minX, dy: popoverRect.minY),
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
        icon.name = "hud.account.stamina.item_ap"
        icon.frame = CGRect(x: 3, y: -6, width: 54, height: 54); icon.contents = Artwork.icon; icon.contentsGravity = .resizeAspect; layer.addSublayer(icon)
        number.name = "hud.account.stamina.value"; number.frame = CGRect(x: 39, y: 6, width: 121, height: 30)
        number.contentsGravity = .resize; number.actions = ["contents": NSNull()]; layer.addSublayer(number)
        feedback = HUDControlHighlightLayer.add(to: layer, rect: barRect, shape: .rounded)
        if let silhouette = Artwork.backgroundMask {
            feedback.useAlphaSilhouette(silhouette,
                contentsCenter: CGRect(x: 29.0 / 60, y: 0, width: 2.0 / 60, height: 1))
        }
        // One neutral hover whatever the bar shows (sanity or the Work Mode link).
        feedback.accentOverride = NSColor(white: 0.75, alpha: 1)
        popover.name = "hud.account.stamina.recovery"; popover.frame = popoverRect
        popover.backgroundColor = NSColor(white: 0.065, alpha: 0.98).cgColor
        popover.cornerRadius = 4; popover.isHidden = true; popover.zPosition = 10
        layer.addSublayer(popover)
        for (item, name) in [(nextLabel, "next.label"), (fullLabel, "full.label"), (nextValue, "next.value"), (fullValue, "full.value")] {
            item.name = "hud.account.stamina.recovery." + name; item.contentsGravity = .resize; item.actions = ["contents": NSNull()]; popover.addSublayer(item)
        }
        refresh.name = "hud.account.stamina.refresh"
        refresh.bounds = CGRect(x: 0, y: 0, width: 22, height: 22)
        let refreshFrame = Layout.refresh(width: Layout.minimumWidth)
        refresh.position = CGPoint(x: refreshFrame.midX, y: refreshFrame.midY)
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
        refreshFeedback = HUDControlHighlightLayer.add(to: popover, rect: refreshFrame, shape: .rounded)
        refreshFeedback.accentOverride = NSColor(white: 0.75, alpha: 1)
    }

    func update(value next: String, accessibilityLabel label: String, visible: Bool, accent: NSColor, scale: CGFloat,
                sanity: HypergryphSanityPresentation? = nil, at date: Date = Date()) {
        baseScale = scale.isFinite ? scale : 2
        let scale = targetScale
        let next = String(next.prefix(24)), wasAvailable = canOpen, wasLink = opensWorkMode
        presentation = sanity; self.date = date
        if value != next || accessibilityLabel != label || layer.isHidden == visible || renderScale != scale {
            CATransaction.begin(); CATransaction.setDisableActions(true)
            if value != next || renderScale != scale { drawNumber(next, scale: scale) }
            value = next; accessibilityLabel = label; renderScale = scale; updateCount += 1
            layer.isHidden = !visible
            CATransaction.commit()
        }
        if !canOpen { dismiss(animated: false) }
        if isPopoverOpen { renderTooltip() }
        if wasAvailable != canOpen || wasLink != opensWorkMode { onChange?() }
    }

    /// Follows a HUD Scale, layout or display change between clock ticks.
    /// `scale` is the HUD's backing x design scale; an unchanged result
    /// draws nothing.
    func setScale(_ scale: CGFloat) {
        baseScale = scale.isFinite ? scale : 2
        let scale = targetScale
        guard renderScale != 0, renderScale != scale else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        drawNumber(value, scale: scale); renderScale = scale
        CATransaction.commit()
        if isPopoverOpen { renderTooltip() }
    }

    private func drawNumber(_ text: String, scale: CGFloat) {
        // Source Text autosizes between 18 and 30 points, then the complete
        // wallet is displayed at 0.6x. Extreme local work durations shrink
        // further to stay inside the bar.
        number.contents = Self.renderText(text, style: Self.numberStyle, in: number.bounds.size, scale: scale,
                                          rightAligned: true, minimumFit: 4.0 / 18)
        number.contentsScale = scale; numberRenderCount += 1
    }

    func perform(_ id: String) {
        if opensWorkMode { if id == "toggle" { onOpenWorkMode?() }; return }
        guard canOpen else { return }
        if id == "toggle" { setOpen(!isPopoverOpen) }
        else if id == "refresh", isPopoverOpen, presentation?.refreshAvailable == true,
                presentation?.isRefreshing != true { onRefresh?() }
    }
    @discardableResult func mouseDown(at point: CGPoint?) -> Bool {
        guard isInteractive else { return false }
        if let point, CGRect(origin: .zero, size: Self.size).contains(point) { perform("toggle"); return true }
        if isPopoverOpen {
            if let point, popoverRect.contains(point) {
                if refreshRect.offsetBy(dx: popoverRect.minX, dy: popoverRect.minY).contains(point) { perform("refresh") }
            } else { dismiss() }
            return true
        }
        return false
    }
    func hover(at point: CGPoint?) {
        HUDControlHighlightLayer.update(in: layer, point: isInteractive ? point : nil)
    }
    func dismiss(animated: Bool = true) { setOpen(false, animated: animated) }
    private func setOpen(_ value: Bool, animated: Bool = true) {
        guard isPopoverOpen != value else {
            guard !value && !animated else { return }
            // Runs on every clock tick: touch the layer only when a close is
            // still showing, so a tick opens no implicit transaction.
            if !popover.isHidden || popover.animationKeys() != nil {
                CATransaction.begin(); CATransaction.setDisableActions(true)
                popover.removeAllAnimations(); popover.isHidden = true
                CATransaction.commit()
            }
            // The Work Mode link keeps its hover across the clock's updates.
            if !isInteractive { hover(at: nil) }
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
        let fullWidthColon = [.simplifiedChinese, .traditionalChinese, .japanese].contains(L10n.resolvedLanguage)
        let colon = fullWidthColon ? "：" : ":"
        let first = L10n.text("Next recovery", "下次回复") + colon
        let second = L10n.text("Full recovery", "全部回复") + colon
        nextRecoveryText = Self.countdown(until: presentation.nextRecoveryAt, at: date, hours: false)
        fullRecoveryText = Self.countdown(until: presentation.fullRecoveryAt, at: date, hours: true)
        let key = [first, second, nextRecoveryText, fullRecoveryText, String(Double(renderScale)), String(presentation.refreshAvailable), String(presentation.isRefreshing)].joined(separator: "|")
        guard key != renderedTooltip else { return }
        renderedTooltip = key; tooltipRenderCount += 1
        // HarmonyOS Sans, with the region's Noto for CJK labels.
        let layout = Layout(labels: [first, second].map(Layout.labelWidth), values: [nextRecoveryText, fullRecoveryText].map(Layout.labelWidth))
        CATransaction.begin(); CATransaction.setDisableActions(true)
        if popoverRect != layout.frame {
            popoverRect = layout.frame; popover.frame = layout.frame
            refresh.position = CGPoint(x: refreshRect.midX, y: refreshRect.midY)
            refreshFeedback.frame.origin = refreshRect.origin
        }
        for (index, pair) in [(nextLabel, first), (fullLabel, second), (nextValue, nextRecoveryText), (fullValue, fullRecoveryText)].enumerated() {
            let (item, text) = pair, frame = layout.cell(index)
            let contentKey = text + "|" + String(Double(renderScale)) + "|" + String(Double(frame.width))
            if item.frame != frame { item.frame = frame }
            guard tooltipContentKeys[index] != contentKey else { continue }
            item.contents = Self.renderText(text, style: Layout.style, in: frame.size, scale: renderScale)
            item.contentsScale = renderScale; tooltipContentKeys[index] = contentKey; tooltipRasterCount += 1
        }
        refresh.contentsScale = renderScale
        let enabled = presentation.refreshAvailable && !presentation.isRefreshing
        refresh.opacity = enabled ? 1 : 0.38; refreshFeedback.setEnabled(enabled)
        CATransaction.commit(); onChange?()
    }

    /// The recovery menu: two rows of label and value, the refresh button at
    /// the right. Columns take their measured text; the menu widens (up to
    /// `maximumWidth`) before labels shrink, so nothing leaves the panel.
    private struct Layout {
        static let textSize: CGFloat = 14.4
        static let minimumWidth: CGFloat = 224, maximumWidth: CGFloat = 300
        static let height: CGFloat = 65, rowHeight: CGFloat = 25, rows: [CGFloat] = [6, 33]
        static let leading: CGFloat = 10, columnGap: CGFloat = 14, refreshGap: CGFloat = 10, trailing: CGFloat = 6
        static let refreshSize = CGSize(width: 23, height: 25)
        /// Room beside each measured column, so text is never shrunk to fit.
        static let slack: CGFloat = 2
        /// Room for the longest countdowns ("00:00", "000:00:00") so the menu
        /// keeps its width while they count down.
        static let valueTemplate = "000:00:00"
        let frame: CGRect, labelWidth: CGFloat, valueWidth: CGFloat

        init(labels: [CGFloat], values: [CGFloat]) {
            let value = ceil(max(values.max() ?? 0, Self.labelWidth(Self.valueTemplate))) + Self.slack
            let fixed = Self.leading + Self.columnGap + Self.refreshGap + Self.refreshSize.width + Self.trailing
            let width = min(Self.maximumWidth, max(Self.minimumWidth, fixed + ceil(labels.max() ?? 0) + Self.slack + value))
            frame = Self.frame(width: width)
            valueWidth = value
            labelWidth = width - fixed - value
        }

        static func frame(width: CGFloat) -> CGRect {
            CGRect(x: HUDAccountGauge.size.width - width, y: 40, width: width, height: height)
        }
        static func refresh(width: CGFloat) -> CGRect {
            CGRect(origin: CGPoint(x: width - trailing - refreshSize.width, y: 19), size: refreshSize)
        }
        /// Label, label, value, value.
        func cell(_ index: Int) -> CGRect {
            let y = Self.rows[index % 2]
            return index < 2 ? CGRect(x: Self.leading, y: y, width: labelWidth, height: Self.rowHeight)
                : CGRect(x: Self.leading + labelWidth + Self.columnGap, y: y, width: valueWidth, height: Self.rowHeight)
        }

        static var style: HUDTextStyle { HUDTextStyle(size: textSize, weight: .medium, color: NSColor.white.cgColor) }
        static func labelWidth(_ text: String) -> CGFloat { HUDText.width(text, style) }
    }

    static var sourceArtworkAvailable: Bool { Artwork.icon != nil && Artwork.background != nil && Artwork.decoration != nil }

    /// One line of text in the standard faces (HarmonyOS Sans, the region's
    /// Noto for CJK), shrunk to fit and centred on its ink. One small raster
    /// per changed value; no font atlas, timer or render loop.
    private static func renderText(_ text: String, style: HUDTextStyle, in size: CGSize, scale: CGFloat,
                                   rightAligned: Bool = false, minimumFit: CGFloat = 0.6) -> CGImage? {
        guard !text.isEmpty else { return nil }
        let natural = HUDText.width(text, style)
        let fit = natural > size.width ? max(minimumFit, size.width / natural) : 1
        let line = CTLineCreateWithAttributedString(HUDText.attributed([HUDText.Run(text, style)], language: HUDTextLanguage.tag(), scale: fit))
        let width = Int(ceil(size.width * scale)), height = Int(ceil(size.height * scale))
        guard width > 0, height > 0, let space = CGColorSpace(name: CGColorSpace.sRGB),
              let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0,
                                      space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        context.scaleBy(x: scale, y: scale)
        let ink = CTLineGetBoundsWithOptions(line, .useGlyphPathBounds)
        let advance = CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
        context.textPosition = CGPoint(x: rightAligned ? size.width - advance : 0, y: (size.height - ink.height) / 2 - ink.minY)
        CTLineDraw(line, context)
        return context.makeImage()
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
