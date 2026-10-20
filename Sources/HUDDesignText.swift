import AppKit
import CoreText
import QuartzCore

/// Builds, measures and lays out centre text. Every run is HarmonyOS Sans with
/// the region's Noto cascade and carries its language tag (HUDFontResolver).
enum HUDText {
    struct Run: Equatable {
        var text: String
        var style: HUDTextStyle
        init(_ text: String, _ style: HUDTextStyle) { self.text = text; self.style = style }
    }

    static func displayed(_ text: String, _ style: HUDTextStyle) -> String {
        style.uppercase ? text.uppercased(with: Locale(identifier: "en_US_POSIX")) : text
    }

    static func font(_ style: HUDTextStyle, language: String, scale: CGFloat = 1) -> CTFont {
        HUDFontResolver.font(size: style.size * scale, weight: style.weight, language: language, tabular: style.tabular)
    }

    static func attributed(_ runs: [Run], language: String, scale: CGFloat = 1) -> NSAttributedString {
        let result = NSMutableAttributedString()
        for run in runs where !run.text.isEmpty {
            let size = run.style.size * scale
            result.append(NSAttributedString(string: displayed(run.text, run.style), attributes: [
                NSAttributedString.Key(kCTFontAttributeName as String): font(run.style, language: language, scale: scale),
                NSAttributedString.Key(kCTForegroundColorAttributeName as String): run.style.color,
                NSAttributedString.Key(kCTKernAttributeName as String): run.style.tracking * size,
                NSAttributedString.Key(kCTLanguageAttributeName as String): language
            ]))
        }
        return result
    }

    /// Advance width of one line, including the tracking after the last glyph
    /// (as CSS letter-spacing does).
    static func width(_ runs: [Run], language: String = HUDTextLanguage.tag()) -> CGFloat {
        let string = attributed(runs, language: language)
        guard string.length > 0 else { return 0 }
        return ceil(CTLineGetTypographicBounds(CTLineCreateWithAttributedString(string), nil, nil, nil) * 2) / 2
    }

    static func width(_ text: String, _ style: HUDTextStyle, language: String = HUDTextLanguage.tag()) -> CGFloat {
        width([Run(text, style)], language: language)
    }

    /// Vertical metrics of a CSS line box: the strut is the primary (Latin) face.
    struct LineMetrics: Equatable {
        let height: CGFloat, ascent: CGFloat, descent: CGFloat
        /// Distance from the top of the line box to the baseline.
        var baseline: CGFloat { (height - ascent - descent) / 2 + ascent }
    }

    static func metrics(_ style: HUDTextStyle, language: String = HUDTextLanguage.tag(), scale: CGFloat = 1) -> LineMetrics {
        let font = self.font(style, language: language, scale: scale)
        return LineMetrics(height: style.size * scale * style.lineHeight, ascent: CTFontGetAscent(font), descent: CTFontGetDescent(font))
    }

    /// Greedy line breaking for wrapped captions; the last allowed line is
    /// truncated with an ellipsis in the same attributes.
    static func lines(_ string: NSAttributedString, width: CGFloat, maximumLines: Int, truncate: Bool) -> [CTLine] {
        guard string.length > 0 else { return [] }
        let available = max(1, Double(width))
        if maximumLines == 1 {
            let line = CTLineCreateWithAttributedString(string)
            return [truncate ? truncated(line, string: string, width: available) : line]
        }
        let typesetter = CTTypesetterCreateWithAttributedString(string)
        var result: [CTLine] = [], start = 0
        while start < string.length {
            let last = maximumLines > 0 && result.count == maximumLines - 1
            let count = last ? string.length - start : max(1, CTTypesetterSuggestLineBreak(typesetter, start, available))
            var line = CTTypesetterCreateLine(typesetter, CFRange(location: start, length: count))
            if last, truncate {
                line = truncated(line, string: string.attributedSubstring(from: NSRange(location: start, length: count)), width: available)
            }
            result.append(line)
            start += count
        }
        return result
    }

    private static func truncated(_ line: CTLine, string: NSAttributedString, width: Double) -> CTLine {
        guard CTLineGetTypographicBounds(line, nil, nil, nil) > width + 0.5 else { return line }
        let attributes = string.attributes(at: max(0, string.length - 1), effectiveRange: nil)
        let token = CTLineCreateWithAttributedString(NSAttributedString(string: "…", attributes: attributes))
        return CTLineCreateTruncatedLine(line, width, .end, token) ?? line
    }
}

/// A CoreText-backed text layer shared by every redesigned module. It draws
/// once per change (never per frame), supports mixed runs, tracking, uppercase,
/// tabular figures, truncation, shrink-to-fit and wrapping, and lays out CSS
/// line boxes: `lineFrame` is the box, the layer itself is padded so glyph
/// overhangs (accents, CJK descenders) are never clipped.
final class HUDTextLayer: CALayer {
    enum Alignment { case left, center, right }

    var runs: [HUDText.Run] = [] { didSet { if runs != oldValue { setNeedsDisplay() } } }
    /// nil follows the UI language; a language name sets its own region.
    var language: String? { didSet { if language != oldValue { setNeedsDisplay() } } }
    var alignment: Alignment = .left { didSet { if alignment != oldValue { setNeedsDisplay() } } }
    var maximumLines = 1 { didSet { if maximumLines != oldValue { setNeedsDisplay() } } }
    var truncates = true { didSet { if truncates != oldValue { setNeedsDisplay() } } }
    /// Shrink a single line down to this fraction of its size before truncating.
    var minimumScale: CGFloat = 1 { didSet { if minimumScale != oldValue { setNeedsDisplay() } } }
    private(set) var renderedLanguage = ""

    var text: String {
        get { runs.map(\.text).joined() }
        set { runs = [HUDText.Run(newValue, textStyle)] }
    }
    var textStyle: HUDTextStyle {
        get { runs.first?.style ?? HUDType.rowLabel }
        set { runs = runs.isEmpty ? [HUDText.Run("", newValue)] : runs.map { HUDText.Run($0.text, newValue) } }
    }
    var color: CGColor {
        get { textStyle.color }
        set { textStyle = textStyle.colored(newValue) }
    }

    /// The CSS line box in the superlayer; the layer's own frame adds padding.
    var lineFrame: CGRect = .zero {
        didSet {
            guard lineFrame != oldValue else { return }
            frame = lineFrame.insetBy(dx: -padding.width, dy: -padding.height)
        }
    }
    private var padding: CGSize {
        let size = runs.map(\.style.size).max() ?? 12
        return CGSize(width: ceil(size * 0.25), height: ceil(size * 0.45))
    }

    override init() {
        super.init()
        needsDisplayOnBoundsChange = true
        isOpaque = false
        contentsGravity = .center
    }
    override init(layer: Any) { super.init(layer: layer) }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    override func action(forKey event: String) -> CAAction? { HUDLayer.action(forKey: event, of: self) }

    convenience init(_ text: String = "", style: HUDTextStyle, alignment: Alignment = .left) {
        self.init()
        runs = [HUDText.Run(text, style)]
        self.alignment = alignment
    }

    /// Single-line natural width at full size.
    var preferredWidth: CGFloat { HUDText.width(runs, language: resolvedLanguage) }
    private var resolvedLanguage: String { language ?? HUDTextLanguage.tag() }

    override func display() {
        renderedLanguage = resolvedLanguage
        super.display()
    }

    override func draw(in ctx: CGContext) {
        let box = bounds.insetBy(dx: padding.width, dy: padding.height)
        guard !runs.isEmpty, box.width > 0 else { return }
        let language = renderedLanguage.isEmpty ? resolvedLanguage : renderedLanguage
        var scale: CGFloat = 1
        if maximumLines == 1, minimumScale < 1 {
            let natural = HUDText.width(runs, language: language)
            if natural > box.width { scale = max(minimumScale, box.width / natural) }
        }
        let string = HUDText.attributed(runs, language: language, scale: scale)
        let lines = HUDText.lines(string, width: box.width, maximumLines: maximumLines, truncate: truncates)
        guard let first = runs.first else { return }
        let metrics = HUDText.metrics(first.style, language: language, scale: scale)
        ctx.saveGState()
        // Draw in a top-down space whichever way Core Animation flipped this context.
        if ctx.ctm.d > 0 { ctx.translateBy(x: 0, y: bounds.height); ctx.scaleBy(x: 1, y: -1) }
        ctx.textMatrix = CGAffineTransform(scaleX: 1, y: -1)
        ctx.setShouldSmoothFonts(false)
        ctx.setAllowsAntialiasing(true)
        let total = metrics.height * CGFloat(lines.count)
        var top = box.minY + (box.height - total) / 2
        for line in lines {
            let width = CGFloat(CTLineGetTypographicBounds(line, nil, nil, nil))
            let x: CGFloat
            switch alignment {
            case .left: x = box.minX
            case .center: x = box.midX - width / 2
            case .right: x = box.maxX - width
            }
            ctx.textPosition = CGPoint(x: x, y: top + metrics.baseline)
            CTLineDraw(line, ctx)
            top += metrics.height
        }
        ctx.restoreGState()
    }
}

/// Layers of the redesign never animate implicitly: every change is either
/// immediate or an explicit, finite HUDMotion animation.
enum HUDLayer {
    static func action(forKey event: String, of layer: CALayer) -> CAAction? { NSNull() }

    private final class Actionless: NSObject, CALayerDelegate {
        func action(for layer: CALayer, forKey event: String) -> CAAction? { NSNull() }
    }
    private static let actionless = Actionless()

    static func plain(_ name: String? = nil) -> CALayer { configure(CALayer(), name) }
    static func shape(_ name: String? = nil) -> CAShapeLayer { configure(CAShapeLayer(), name) }
    static func gradient(_ name: String? = nil) -> CAGradientLayer { configure(CAGradientLayer(), name) }

    @discardableResult
    static func configure<T: CALayer>(_ layer: T, _ name: String? = nil) -> T {
        layer.delegate = actionless
        layer.name = name
        return layer
    }
}
