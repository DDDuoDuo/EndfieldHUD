import AppKit
import QuartzCore

/// Recording mode's redaction (spec §3.26): a hatched black bar stands in for
/// private text. Bars wipe in from the left (`rdIn` .35 s E) and bars at least
/// 70 px wide carry a centred yellow `[ REDACTED ]` label. Under Reduce Motion
/// they simply appear.
enum HUDRedaction {
    static let labelMinimumWidth: CGFloat = 70
    static let entranceDuration: CFTimeInterval = 0.35
    static let barName = "redaction.bar"

    /// The default width for `text`: min(260, max(44, characters × 7.2)).
    static func width(for text: String) -> CGFloat { min(260, max(44, CGFloat(text.count) * 7.2)) }

    /// One bar of `size`; `labelled` defaults to the 70 px rule. `fill` and
    /// `stripe` retint the hatch (classified Archive lines: #0C0C0C / #262626).
    static func bar(size: CGSize, labelled: Bool? = nil, fill: CGColor = HUDColor.rgb(0x141414),
                    stripe: CGColor = HUDColor.rgb(0x2B2B2B), env: HUDDesignEnvironment) -> CALayer {
        let bar = HUDLayer.plain(barName)
        let stripes = HUDLayer.shape("redaction.stripes")
        let bounds = CGRect(origin: .zero, size: CGSize(width: max(1, size.width), height: max(1, size.height)))
        HUDMotion.perform {
            bar.bounds = bounds
            bar.backgroundColor = fill
            bar.borderWidth = 1; bar.borderColor = HUDColor.black(1)
            bar.masksToBounds = true
            stripes.frame = bounds; stripes.path = stripePath(bounds.size); stripes.fillColor = stripe
            bar.addSublayer(stripes)
            if labelled ?? (bounds.width >= labelMinimumWidth) {
                let label = HUDTextLayer("[ " + L10n.text("REDACTED", "已隐藏") + " ]", style: labelStyle, alignment: .center)
                label.truncates = false
                label.lineFrame = CGRect(x: 0, y: (bounds.height - labelStyle.size) / 2, width: bounds.width, height: labelStyle.size)
                bar.addSublayer(label)
            }
        }
        env.applyScale(to: bar)
        return bar
    }

    /// A bar standing in for one line of `style` text whose CSS line box is
    /// `line` (in the parent's coordinates): 1.1 em high, vertically centred,
    /// aligned like the text and never wider than the line box.
    static func bar(replacing line: CGRect, style: HUDTextStyle, width: CGFloat,
                    alignment: HUDTextLayer.Alignment = .left, env: HUDDesignEnvironment) -> CALayer {
        let size = CGSize(width: min(width, line.width), height: (style.size * 1.1).rounded())
        let bar = self.bar(size: size, env: env)
        let x: CGFloat
        switch alignment {
        case .left: x = line.minX
        case .center: x = line.midX - size.width / 2
        case .right: x = line.maxX - size.width
        }
        HUDMotion.perform { bar.frame.origin = CGPoint(x: x, y: line.midY - size.height / 2) }
        return bar
    }

    /// The multi-line block (Notes text): padding 16 × 14, 13 px bars 9 px
    /// apart, one per wrapped 46-character line, each max(60, characters ×
    /// 9.2) wide; only the first is labelled. Lines that do not fit are dropped.
    static func block(_ text: String, size: CGSize, env: HUDDesignEnvironment) -> CALayer {
        let block = HUDLayer.plain("redaction.block")
        let maximumWidth = max(1, size.width - 28)
        HUDMotion.perform {
            block.bounds = CGRect(origin: .zero, size: size)
            var y: CGFloat = 16
            for (index, line) in wrapped(text, limit: 46).enumerated() where y + 13 <= size.height - 16 + 0.5 {
                let bar = self.bar(size: CGSize(width: min(maximumWidth, max(60, CGFloat(line.count) * 9.2)), height: 13),
                                   labelled: index == 0, env: env)
                bar.frame.origin = CGPoint(x: 14, y: y)
                block.addSublayer(bar)
                y += 13 + 9
            }
        }
        return block
    }

    /// rdIn on every bar in `layers` (and their sublayers).
    static func playEntrance(in layers: [CALayer], env: HUDDesignEnvironment) {
        guard !env.reducesMotion else { return }
        func visit(_ layer: CALayer) {
            if layer.name == barName { HUDMotion.wipe(layer, from: .left, duration: entranceDuration, env: env, key: "hud.redaction"); return }
            layer.sublayers?.forEach(visit)
        }
        layers.forEach(visit)
    }

    /// Greedy word wrap at `limit` characters; longer words are split.
    static func wrapped(_ text: String, limit: Int) -> [String] {
        var lines: [String] = []
        for paragraph in text.components(separatedBy: .newlines) {
            var line = ""
            for word in paragraph.split(separator: " ", omittingEmptySubsequences: false).map(String.init) {
                var word = word
                while word.count > limit {
                    if !line.isEmpty { lines.append(line); line = "" }
                    lines.append(String(word.prefix(limit))); word = String(word.dropFirst(limit))
                }
                if line.isEmpty { line = word }
                else if line.count + 1 + word.count <= limit { line += " " + word }
                else { lines.append(line); line = word }
            }
            lines.append(line)
        }
        return lines
    }

    private static var labelStyle: HUDTextStyle { HUDTextStyle(size: 8, weight: .bold, color: HUDColor.yellow, tracking: 0.2) }

    /// repeating-linear-gradient(-45deg, #141414 0 3px, #2b2b2b 3px 6px): the
    /// lighter bands run bottom-left to top-right, 3 px wide every 6 px.
    private static func stripePath(_ size: CGSize) -> CGPath {
        let path = CGMutablePath(), period = 6 * CGFloat(2).squareRoot(), band = 3 * CGFloat(2).squareRoot()
        var x = -size.height - period
        while x < size.width + period {
            path.addLines(between: [CGPoint(x: x, y: size.height), CGPoint(x: x + band, y: size.height),
                                    CGPoint(x: x + band + size.height, y: 0), CGPoint(x: x + size.height, y: 0)])
            path.closeSubpath()
            x += period
        }
        return path
    }
}
