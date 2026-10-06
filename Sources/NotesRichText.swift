import AppKit

/// Closed, attachment-free formatting. Ranges use UTF-16, like NSTextStorage.
/// Plain note text remains the canonical string in the existing database column.
struct NotesRGBA: Codable, Equatable {
    var red: Double
    var green: Double
    var blue: Double
    var alpha: Double = 1
    var isValid: Bool { [red, green, blue, alpha].allSatisfy { $0.isFinite && (0...1).contains($0) } }
    var color: NSColor { NSColor(srgbRed: red, green: green, blue: blue, alpha: alpha) }
    init(red: Double, green: Double, blue: Double, alpha: Double = 1) {
        self.red = red; self.green = green; self.blue = blue; self.alpha = alpha
    }
    init?(_ color: NSColor) {
        guard let value = color.usingColorSpace(.sRGB) else { return nil }
        self.init(red: Double(value.redComponent), green: Double(value.greenComponent),
                  blue: Double(value.blueComponent), alpha: Double(value.alphaComponent))
    }
}

struct NotesTextStyle: Codable, Equatable {
    var fontName: String? = nil
    var fontSize: Double = 12
    var color: NotesRGBA? = nil
    var bold = false
    var italic = false
    var underline = false
    var strikethrough = false
    var isValid: Bool {
        fontSize.isFinite && (6...144).contains(fontSize)
            && (fontName.map { !$0.isEmpty && $0.utf8.count <= 256 } ?? true)
            && (color?.isValid ?? true)
    }
    static func changingTrait(_ trait: NSFontTraitMask, enabled: Bool, on font: NSFont) -> NSFont {
        let manager = NSFontManager.shared
        if font.fontName.hasPrefix(".") {
            var traits = manager.traits(of: font)
            if enabled { traits.insert(trait) } else { traits.remove(trait) }
            var result = NSFont.systemFont(ofSize: font.pointSize, weight: traits.contains(.boldFontMask) ? .bold : .regular)
            if traits.contains(.italicFontMask) { result = manager.convert(result, toHaveTrait: .italicFontMask) }
            return result
        }
        return enabled ? manager.convert(font, toHaveTrait: trait) : manager.convert(font, toNotHaveTrait: trait)
    }
    func attributes(scale: CGFloat = 1, defaultColor: NSColor) -> [NSAttributedString.Key: Any] {
        // AppKit's private system names (for example .SFNS-Bold) are not
        // registered PostScript fonts. Resolve them through the public system
        // font API, including any early payloads that retained such a name.
        var font = fontName.flatMap { $0.hasPrefix(".") ? nil : NSFont(name: $0, size: CGFloat(fontSize) * scale) }
            ?? NSFont.systemFont(ofSize: CGFloat(fontSize) * scale)
        if bold { font = Self.changingTrait(.boldFontMask, enabled: true, on: font) }
        if italic { font = Self.changingTrait(.italicFontMask, enabled: true, on: font) }
        var result: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color?.color ?? defaultColor]
        if underline { result[.underlineStyle] = NSUnderlineStyle.single.rawValue }
        if strikethrough { result[.strikethroughStyle] = NSUnderlineStyle.single.rawValue }
        return result
    }
}

struct NotesTextRun: Codable, Equatable {
    var location: Int
    var length: Int
    var style: NotesTextStyle
    var range: NSRange { NSRange(location: location, length: length) }
}

struct NotesRichText: Codable, Equatable {
    var version = 1
    var runs: [NotesTextRun] = []
    static let maximumRuns = 50_000
    func isValid(for text: String) -> Bool {
        guard version == 1, runs.count <= Self.maximumRuns else { return false }
        let length = (text as NSString).length
        var previousEnd = 0
        for run in runs {
            guard run.location >= previousEnd, run.length > 0, run.location <= length,
                  run.length <= length - run.location, run.style.isValid else { return false }
            previousEnd = run.location + run.length
        }
        return true
    }
    func attributed(_ text: String, scale: CGFloat = 1, defaultColor: NSColor) -> NSAttributedString {
        let result = NSMutableAttributedString(string: text,
            attributes: NotesTextStyle().attributes(scale: scale, defaultColor: defaultColor))
        guard isValid(for: text) else { return result }
        for run in runs { result.setAttributes(run.style.attributes(scale: scale, defaultColor: defaultColor), range: run.range) }
        return result
    }
    static func capture(_ text: NSAttributedString, scale: CGFloat = 1, defaultColor: NSColor) -> NotesRichText {
        var result = NotesRichText()
        text.enumerateAttributes(in: NSRange(location: 0, length: text.length)) { attributes, range, _ in
            let font = attributes[.font] as? NSFont ?? .systemFont(ofSize: 12 * scale)
            let traits = NSFontManager.shared.traits(of: font)
            let foreground = attributes[.foregroundColor] as? NSColor
            var style = NotesTextStyle()
            style.fontName = font.fontName.hasPrefix(".") ? nil : font.fontName
            style.fontSize = min(144, max(6, Double(font.pointSize / max(0.01, scale))))
            style.color = foreground.flatMap { $0.isEqual(defaultColor) ? nil : NotesRGBA($0) }
            style.bold = traits.contains(.boldFontMask); style.italic = traits.contains(.italicFontMask)
            style.underline = ((attributes[.underlineStyle] as? NSNumber)?.intValue ?? 0) != 0
            style.strikethrough = ((attributes[.strikethroughStyle] as? NSNumber)?.intValue ?? 0) != 0
            if let last = result.runs.last, last.style == style, last.location + last.length == range.location {
                result.runs[result.runs.count - 1].length += range.length
            } else { result.runs.append(NotesTextRun(location: range.location, length: range.length, style: style)) }
        }
        return result
    }
}
