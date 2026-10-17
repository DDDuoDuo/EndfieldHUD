import AppKit
import QuartzCore

// Compile alongside the unchanged original source. Detached synthetic views;
// no app/window, first responder, input context activation, data or capture.
@main struct WorkModeFieldReference {
    static func main() throws {
        precondition(CommandLine.arguments.count == 2)
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        var rows: [[String: Any]] = []
        let texts = ["", "30:00", "1", "123456789012345678901234567890", "中文😀e\u{301}", "first\nsecond", "long first line then\nsecond", "first\r\nsecond", "trailing   "]
        for size in [CGFloat(13), 18, 46] {
            for height in [CGFloat(20), 32, 68] {
                for width in [CGFloat(80), 312] {
                    for value in texts {
                        let host = NSView(frame: CGRect(x: 0, y: 0, width: 512, height: 256))
                        let parent = CALayer()
                        let view = HUDProjectedTextView(frame: .zero)
                        let font = NSFont.monospacedDigitSystemFont(ofSize: size, weight: .medium)
                        view.font = font; view.alignment = .center; view.string = value
                        let editor = HUDProjectedTextEditor(textView: view, rect: CGRect(x: 0, y: 0, width: width, height: height), host: host, parent: parent)
                        editor.configureSingleLine()
                        let container = view.textContainer!, manager = view.layoutManager!
                        manager.ensureLayout(for: container)
                        let storageWidth = view.textStorage!.size().width
                        let expectedWidth = max(width, ceil(storageWidth) + 11)
                        let expectedInsetY = max(0, (height - font.ascender + font.descender - font.leading) / 2)
                        precondition(view.frame.width == expectedWidth && view.frame.height == height)
                        precondition(view.textContainerInset == NSSize(width: 3, height: expectedInsetY))
                        precondition(container.containerSize.width == expectedWidth - 6 && container.maximumNumberOfLines == 1)
                        let glyphRange = manager.glyphRange(for: container)
                        let characters = manager.characterRange(forGlyphRange: glyphRange, actualGlyphRange: nil)
                        rows.append(["fontSize": size, "viewport": [width, height], "text": value,
                                     "font": ["name": font.fontName, "ascender": font.ascender, "descender": font.descender, "leading": font.leading],
                                     "storageWidth": storageWidth, "inset": [view.textContainerInset.width, view.textContainerInset.height],
                                     "documentSize": [view.frame.width, view.frame.height],
                                     "containerSize": [container.containerSize.width, container.containerSize.height],
                                     "laidOutUTF16": [characters.location, characters.length],
                                     "maximumLines": container.maximumNumberOfLines])
                        precondition(editor.captureCount == 0 && editor.retainedPixelCount == 0 && host.window == nil)
                        editor.dispose()
                    }
                }
            }
        }
        let result: [String: Any] = ["schema": 1, "source": "Sources/HUDProjectedTextEditor.swift", "detached": true, "captureCount": 0, "rows": rows]
        try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys]).write(to: output.appendingPathComponent("reference.json"))
        print("\(rows.count) original single-line field cases passed; no window or pixel capture")
    }
}
