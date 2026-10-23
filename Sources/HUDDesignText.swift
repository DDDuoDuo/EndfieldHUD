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
            framePadding = padding
            frame = lineFrame.insetBy(dx: -padding.width, dy: -padding.height)
        }
    }
    /// The padding `lineFrame` was laid out with: runs set afterwards (a
    /// larger style) must not shrink the box their text is fitted into.
    private var framePadding: CGSize?
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

    /// Unlike CATextLayer, a drawn CALayer keeps its old bitmap when the
    /// backing scale changes, and `.center` gravity then shows that bitmap at
    /// old/new of its size: text shrank or grew after a display, space or
    /// design-scale change. Redraw at the new resolution instead.
    override var contentsScale: CGFloat {
        didSet { if contentsScale != oldValue { setNeedsDisplay() } }
    }

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
        let inset = framePadding ?? padding
        let box = bounds.insetBy(dx: inset.width, dy: inset.height)
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

/// CATextLayer text outside the redesigned centre (the shell's labels, the
/// identity card, the Notes and Projection workspaces), set in the standard
/// faces. `fontSize`, `weight`, `tabular` and `oblique` pick the cached font
/// and its CJK cascade follows the UI language whenever the layer draws, so
/// callers never assign `font` themselves. Strings, colour, alignment,
/// wrapping and truncation keep CATextLayer's meaning.
final class HUDStandardTextLayer: CATextLayer {
    var weight: HUDFontWeight = .regular { didSet { if weight != oldValue { resolveFont() } } }
    var tabular = false { didSet { if tabular != oldValue { resolveFont() } } }
    /// Slanted as a synthesized italic.
    var oblique = false { didSet { if oblique != oldValue { resolveFont() } } }
    /// A face the user chose (an editor's own font) in place of the standard ones.
    var face: NSFont? { didSet { if face != oldValue { resolveFont() } } }
    override var fontSize: CGFloat { didSet { if fontSize != oldValue { resolveFont() } } }

    override init() { super.init(); resolveFont() }
    override init(layer: Any) { super.init(layer: layer) }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    convenience init(size: CGFloat, weight: HUDFontWeight = .regular, tabular: Bool = false) {
        self.init()
        self.weight = weight; self.tabular = tabular; fontSize = size
    }

    override func display() {
        resolveFont()
        super.display()
    }

    /// A redraw (new text, a new render scale) never crossfades: the implicit
    /// contents fade shows the old bitmap at the new contentsScale, twice its
    /// size when the scale halves. Other properties keep their actions.
    override func action(forKey event: String) -> CAAction? {
        event == "contents" ? NSNull() : super.action(forKey: event)
    }

    private func resolveFont() {
        let resolved = face ?? HUDFontResolver.font(size: fontSize, weight: weight, language: HUDTextLanguage.tag(),
                                                    tabular: tabular, oblique: oblique)
        if font !== resolved { font = resolved }
    }

    /// CATextLayer sets HarmonyOS Sans on CoreText's frame line height for the
    /// face (1.44 em), which sinks text a quarter em below where the system
    /// face sat and drops the second line of two-line captions. Lines here
    /// use the faces' own ascent and descent, as AppKit measures them, top
    /// aligned like CATextLayer.
    override func draw(in ctx: CGContext) {
        let text = drawnText()
        guard text.length > 0, bounds.width > 0 else { return }
        let typesetter = CTTypesetterCreateWithAttributedString(text)
        // Unwrapped text still breaks at its own line separators.
        let breakWidth = isWrapped ? Double(bounds.width) : 1e7
        var lines: [(line: CTLine, start: Int, ascent: CGFloat, height: CGFloat)] = [], start = 0, top: CGFloat = 0
        while start < text.length {
            let count = max(1, CTTypesetterSuggestLineBreak(typesetter, start, breakWidth))
            let strut = metrics(of: text, in: NSRange(location: start, length: count))
            if let last = lines.last, top + strut.height > bounds.height + 0.5 {
                // Out of room: like CATextLayer, the last line that fits ends truncated.
                if truncationMode != .none {
                    let rest = CTTypesetterCreateLine(typesetter, CFRange(location: last.start, length: text.length - last.start))
                    lines[lines.count - 1].line = truncated(rest, text: text, force: true)
                }
                break
            }
            var line = CTTypesetterCreateLine(typesetter, CFRange(location: start, length: count))
            if !isWrapped { line = truncated(line, text: text) }
            lines.append((line, start, strut.ascent, strut.height))
            top += strut.height; start += count
        }
        ctx.saveGState()
        // Draw y-up whichever way Core Animation flipped this context: a
        // flipped text matrix would drop a slanted face's own matrix.
        if ctx.ctm.d < 0 { ctx.translateBy(x: 0, y: bounds.height); ctx.scaleBy(x: 1, y: -1) }
        ctx.textMatrix = .identity
        ctx.setShouldSmoothFonts(false)
        ctx.setAllowsAntialiasing(true)
        top = 0
        for item in lines {
            let width = CGFloat(CTLineGetTypographicBounds(item.line, nil, nil, nil) - CTLineGetTrailingWhitespaceWidth(item.line))
            let x: CGFloat
            switch alignmentMode {
            case .center: x = (bounds.width - width) / 2
            case .right: x = bounds.width - width
            default: x = 0
            }
            ctx.textPosition = CGPoint(x: x, y: bounds.height - top - item.ascent)
            CTLineDraw(item.line, ctx)
            top += item.height
        }
        ctx.restoreGState()
    }

    /// The string with CATextLayer's defaults where runs leave them out. No
    /// language tag: the cascade already picks the region's Noto, and
    /// untagged text keeps CATextLayer's strict kana line breaks.
    private func drawnText() -> NSAttributedString {
        let result: NSMutableAttributedString
        switch string {
        case let attributed as NSAttributedString: result = NSMutableAttributedString(attributedString: attributed)
        case let plain as String: result = NSMutableAttributedString(string: plain)
        default: return NSAttributedString()
        }
        let color = NSAttributedString.Key(kCTForegroundColorAttributeName as String)
        result.enumerateAttributes(in: NSRange(location: 0, length: result.length)) { attributes, range, _ in
            if attributes[.font] == nil, let font { result.addAttribute(.font, value: font, range: range) }
            if attributes[.foregroundColor] == nil, attributes[color] == nil {
                result.addAttribute(color, value: foregroundColor ?? CGColor(gray: 1, alpha: 1), range: range)
            }
        }
        return result
    }

    /// The line box of a range: the largest ascent and descent of the fonts
    /// its runs name (fallback CJK glyphs sit inside the Latin face's box).
    private func metrics(of text: NSAttributedString, in range: NSRange) -> (ascent: CGFloat, height: CGFloat) {
        var ascent: CGFloat = 0, descent: CGFloat = 0, leading: CGFloat = 0
        text.enumerateAttribute(.font, in: range) { value, _, _ in
            guard let font = value as? NSFont else { return }
            ascent = max(ascent, font.ascender); descent = max(descent, -font.descender); leading = max(leading, font.leading)
        }
        return (ascent, ascent + descent + leading)
    }

    /// `force` marks text that continues past the line even when it fits.
    private func truncated(_ line: CTLine, text: NSAttributedString, force: Bool = false) -> CTLine {
        let type: CTLineTruncationType
        switch truncationMode {
        case .start: type = .start
        case .middle: type = .middle
        case .end: type = .end
        default: return line
        }
        guard force || CTLineGetTypographicBounds(line, nil, nil, nil) > Double(bounds.width) + 0.5 else { return line }
        let attributes = text.attributes(at: max(0, CTLineGetStringRange(line).location), effectiveRange: nil)
        let token = CTLineCreateWithAttributedString(NSAttributedString(string: "…", attributes: attributes))
        return CTLineCreateTruncatedLine(line, Double(bounds.width), type, token) ?? line
    }

    /// Redraws every standard text layer under `root` after the UI language
    /// changed, so text whose string stayed the same (names, numbers) also
    /// takes the new region's CJK face.
    static func languageChanged(in root: CALayer) {
        if let text = root as? HUDStandardTextLayer { text.setNeedsDisplay() }
        root.sublayers?.forEach(languageChanged)
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
