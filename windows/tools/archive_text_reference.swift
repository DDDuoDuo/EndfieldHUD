import AppKit

// The companion shell compiles the unchanged ArchiveCategory declaration from
// current Sources/ArchiveStore.swift. Category trim/folding are its own methods;
// no expected category algorithm is reproduced in this fixture.
@main enum ArchiveTextReference {
    static func main() {
        do {
            guard CommandLine.arguments.count == 2,
                  ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil else {
                throw NSError(domain: "ArchiveTextReference", code: 1)
            }
            NSApplication.shared.setActivationPolicy(.prohibited)
            let texts = ["", "👩🏽‍💻é中文", "🏳️‍🌈🇨🇳1️⃣", "\u{200B}\n  名字\t", "é", "e\u{301}", "\u{0085}\u{00A0} name\u{3000}"]
            let pairs = [("Straße", "STRASSE"), ("İ", "i"), ("i", "I"), ("é", "e\u{301}"),
                         ("Ａ", "ａ"), ("Ａ", "A"), ("ﬀ", "ff"), ("σ", "ς"),
                         ("ᾲ", "ὰι"), ("\u{200B} test ", "test"), ("K", "k")]
            let font = NSFont.systemFont(ofSize: 10, weight: .medium)
            let captions = ["全部", "未分类", "Category 0", "Category 13", "分类中文 / 긴 이름", "A very long name"]
            let result: [String: Any] = [
                "schemaVersion": 1,
                "text": texts.map { ["text": $0, "characters": $0.count,
                                     "trimmed": $0.trimmingCharacters(in: .whitespacesAndNewlines)] as [String: Any] },
                "pairs": pairs.map { ["left": $0.0, "right": $0.1,
                                      "equal": ArchiveCategory.nameKey($0.0) == ArchiveCategory.nameKey($0.1)] as [String: Any] },
                "captionWidths": captions.map { ["text": $0, "width": ($0 as NSString).size(withAttributes: [.font: font]).width] as [String: Any] },
                "captionFont": ["familyName": font.familyName ?? "", "postScriptName": font.fontName,
                                "pointSize": font.pointSize, "weight": "medium"],
                "windowsCreated": false,
                "limitations": ["Detached native category/NSString metrics; not cross-platform font raster parity.",
                                "Finite Unicode cases; newly introduced graphemes depend on each OS Unicode version."]
            ]
            guard NSApp.windows.isEmpty else { throw NSError(domain: "ArchiveTextReference", code: 2) }
            try JSONSerialization.data(withJSONObject: result, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
                .write(to: URL(fileURLWithPath: CommandLine.arguments[1]), options: .atomic)
            print("PASS detached native Archive text reference")
        } catch { fputs("Archive text reference failed: \(error)\n", stderr); exit(1) }
    }
}
