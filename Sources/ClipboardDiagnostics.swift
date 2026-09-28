import AppKit
import ImageIO

/// Synthetic samples for the existing isolated UI test mode. The caller must
/// supply that mode's private pasteboard, never the user's system clipboard.
enum ClipboardDiagnostics {
    static func seed(_ watcher: ClipboardWatcher) {
        guard CommandLine.arguments.contains("--ui-test"),
              watcher.pasteboard.name != NSPasteboard.Name.general else { return }
        let board = watcher.pasteboard
        for text in ["A short copied note", "Checklist for the next build", "A multiline\ntext sample",
                     "中文剪贴板示例", "A long preview that stays compact: " + String(repeating: "sample content ", count: 18),
                     "Copy, pin, delete", "Latest clipboard entry"] {
            board.clearContents()
            board.setString(text, forType: .string)
            watcher.checkForChanges()
        }
        if let context = CGContext(data: nil, width: 160, height: 100, bitsPerComponent: 8,
                                   bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
                                   bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) {
            context.setFillColor(NSColor(white: 0.12, alpha: 1).cgColor)
            context.fill(CGRect(x: 0, y: 0, width: 160, height: 100))
            context.setFillColor(NSColor.systemYellow.cgColor)
            context.fill(CGRect(x: 15, y: 18, width: 130, height: 15))
            context.fill(CGRect(x: 15, y: 44, width: 90, height: 38))
            let data = NSMutableData()
            if let image = context.makeImage(),
               let destination = CGImageDestinationCreateWithData(data, "public.png" as CFString, 1, nil) {
                CGImageDestinationAddImage(destination, image, nil)
                if CGImageDestinationFinalize(destination) {
                    board.clearContents(); board.setData(data as Data, forType: .png)
                    watcher.checkForChanges()
                }
            }
        }
        board.clearContents()
        board.writeObjects([Bundle.main.bundleURL as NSURL])
        watcher.checkForChanges()
        board.clearContents()
        board.setString("https://example.com/project/clipboard", forType: .string)
        watcher.checkForChanges()
    }
}
