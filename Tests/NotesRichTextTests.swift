import AppKit

enum NotesRichTextTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let text = "第一段 👩🏽‍💻\nSecond paragraph"
        let boundary = ("第一段 👩🏽‍💻\n" as NSString).length
        var first = NotesTextStyle(); first.fontName = "Menlo-Regular"; first.fontSize = 24
        first.color = NotesRGBA(red: 0.8, green: 0.15, blue: 0.2); first.bold = true
        var second = NotesTextStyle(); second.fontSize = 10; second.italic = true
        second.underline = true; second.strikethrough = true
        let rich = NotesRichText(runs: [NotesTextRun(location: 0, length: boundary, style: first),
            NotesTextRun(location: boundary, length: (text as NSString).length - boundary, style: second)])
        check(rich.isValid(for: text), "UTF-16 Unicode runs validate against canonical text")
        let encoded = try! JSONEncoder().encode(rich)
        check(try! JSONDecoder().decode(NotesRichText.self, from: encoded) == rich, "All per-range formatting round-trips without RTF or attachments")
        let display = rich.attributed(text, scale: 0.75, defaultColor: .white)
        check(display.string == text, "Formatting never replaces or normalizes user text")
        check((display.attribute(.font, at: 0, effectiveRange: nil) as? NSFont)?.pointSize == 18,
              "Render scale changes point size without changing logical storage size")
        check((display.attribute(.strikethroughStyle, at: boundary, effectiveRange: nil) as? NSNumber)?.intValue == 1,
              "The second range preserves independent strike and underline")
        let captured = NotesRichText.capture(display, scale: 0.75, defaultColor: .white)
        check(captured.isValid(for: text) && captured.runs.first?.style.fontSize == 24
              && captured.runs.last?.style.fontSize == 10, "Capturing projected native editing restores mixed logical font sizes")
        check(captured.runs.first?.style.color == first.color && captured.runs.last?.style.color == nil,
              "Only explicit colors persist; theme defaults remain adaptive")
        var invalid = rich; invalid.runs[1].location = 0
        check(!invalid.isValid(for: text), "Overlapping attribute ranges are rejected")
        invalid = rich; invalid.runs[0].length = Int.max
        check(!invalid.isValid(for: text), "Oversized UTF-16 ranges are rejected without integer overflow")
        invalid = rich; invalid.runs[0].style.fontSize = .infinity
        check(!invalid.isValid(for: text), "Nonfinite font sizes cannot reach native layout")
        invalid = rich; invalid.version = 99
        check(!invalid.isValid(for: text), "Unknown rich-text payload versions are rejected")
        var badColor = first; badColor.color = NotesRGBA(red: -1, green: 0, blue: 0)
        check(!badColor.isValid, "Invalid color components are rejected")
        let plain = NotesRichText().attributed(text, defaultColor: .black)
        check(plain.string == text && plain.length == (text as NSString).length,
              "A plain legacy note gains default display attributes without data migration loss")
        let manager = NSFontManager.shared
        for traits: NSFontTraitMask in [[], [.boldFontMask], [.italicFontMask], [.boldFontMask, .italicFontMask]] {
            var systemFont = NSFont.systemFont(ofSize: 18)
            if traits.contains(.boldFontMask) { systemFont = manager.convert(systemFont, toHaveTrait: .boldFontMask) }
            if traits.contains(.italicFontMask) { systemFont = manager.convert(systemFont, toHaveTrait: .italicFontMask) }
            let value = NotesRichText.capture(NSAttributedString(string: "System", attributes: [.font: systemFont]), defaultColor: .black)
            let storedStyle = value.runs[0].style
            check(storedStyle.fontName == nil, "System fonts persist without unregistered private PostScript names")
            let restored = value.attributed("System", defaultColor: .black).attribute(.font, at: 0, effectiveRange: nil) as! NSFont
            check(restored.familyName == systemFont.familyName && restored.pointSize == 18
                  && manager.traits(of: restored).intersection([.boldFontMask, .italicFontMask]) == traits,
                  "System regular, bold, italic and bold-italic fonts round-trip without a Times fallback")
        }
        var earlySystem = NotesTextStyle(); earlySystem.fontName = ".SFNS-Bold"; earlySystem.bold = true
        let safeSystem = earlySystem.attributes(defaultColor: .black)[.font] as! NSFont
        check(safeSystem.familyName == NSFont.systemFont(ofSize: 12).familyName
              && manager.traits(of: safeSystem).contains(.boldFontMask),
              "Early private system names render through public system-font APIs without a stored-data rewrite")
        return count
    }
}
