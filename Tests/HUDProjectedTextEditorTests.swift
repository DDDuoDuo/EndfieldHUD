import AppKit
import QuartzCore

enum HUDProjectedTextEditorTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let _ = NSApplication.shared
        let host = ProjectedTestHost(frame: CGRect(x: 0, y: 0, width: 800, height: 600))
        host.wantsLayer = true
        let window = NSWindow(contentRect: CGRect(x: -10000, y: -10000, width: 800, height: 600),
                              styleMask: .borderless, backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false; window.contentView = host
        defer { window.close() }
        let rect = CGRect(x: 120, y: 80, width: 240, height: 100)
        let text = HUDProjectedTextView(frame: .zero)
        text.isRichText = true; text.allowsUndo = true; text.font = .systemFont(ofSize: 18)
        text.textColor = .white; text.insertionPointColor = .yellow
        text.string = "Hello 世界 👩🏽‍💻\nSecond line"
        let editor = HUDProjectedTextEditor(textView: text, rect: rect, host: host, parent: host.layer!)
        defer { editor.dispose() }
        var offset = CGPoint(x: 43, y: 27)
        editor.project = { $0.offsetBy(dx: offset.x, dy: offset.y) }
        editor.unproject = { CGPoint(x: $0.x - offset.x, y: $0.y - offset.y) }
        window.makeFirstResponder(text)
        editor.resizeDocument(); editor.captureVisibleArtwork()
        check(editor.captureCount == 1 && editor.retainedPixelCount > 0 && editor.retainedPixelCount <= HUDProjectedTextEditor.maximumPixels,
              "Only the bounded visible viewport is retained")
        let glyphLayer = editor.artwork.sublayers![0]
        let image = glyphLayer.contents as! CGImage
        let bytes = image.dataProvider!.data! as Data
        check(bytes.contains { $0 != 0 }, "Public native display caching actually renders the text artwork")
        let original = text.string, font = text.font, selection = NSRange(location: 6, length: 2)
        text.setSelectedRange(selection)
        let before = editor.captureCount
        for step in 0..<120 { offset.x = CGFloat(step); editor.refreshProjection() }
        check(editor.captureCount == before && text.string == original && text.font == font && text.selectedRange() == selection,
              "Pose changes neither rasterize text nor alter native text, fonts, selection or layout")
        let nativePoint = CGPoint(x: 8, y: 9)
        let source = CGPoint(x: rect.minX + nativePoint.x + offset.x, y: rect.minY + nativePoint.y + offset.y)
        check(editor.textPoint(host: source) == nativePoint, "Projected pointer coordinates invert to stable TextKit coordinates")
        check(editor.hitTest(source) === text && editor.hitTest(CGPoint(x: 799, y: 599)) == nil,
              "Only the actual projected input rectangle captures pointer events")
        var actual = NSRange()
        let first = text.firstRect(forCharacterRange: NSRange(location: 6, length: 0), actualRange: &actual)
        offset.y += 41
        editor.refreshProjection()
        let moved = text.firstRect(forCharacterRange: NSRange(location: 6, length: 0), actualRange: &actual)
        check(abs(moved.minX - first.minX) < 0.01 && abs(moved.minY - first.minY + 41) < 0.01,
              "IME candidate coordinates follow the projected text plane in screen coordinates")
        check(text.accessibilityFrame() == editor.projectedScreenFrame, "VoiceOver exposes the projected editor frame")
        check(editor.scrollView.accessibilityFrame() == editor.projectedScreenFrame,
              "The native accessibility scroll-area parent follows the same projected frame as its editor")
        let axBefore = text.accessibilityFrame(for: NSRange(location: 0, length: 5))
        offset.x += 17; editor.refreshProjection()
        let axAfter = text.accessibilityFrame(for: NSRange(location: 0, length: 5))
        check(abs(axAfter.minX - axBefore.minX - 17) < 0.01,
              "Accessibility range geometry is projected exactly once")
        let pointerInText = CGPoint(x: 13, y: 9)
        let pointerInHost = CGPoint(x: rect.minX + pointerInText.x + offset.x, y: rect.minY + pointerInText.y + offset.y)
        let pointerInWindow = host.convert(pointerInHost, to: nil)
        let click = NSEvent.mouseEvent(with: .leftMouseDown, location: pointerInWindow,
            modifierFlags: [], timestamp: 1, windowNumber: window.windowNumber, context: nil,
            eventNumber: 1, clickCount: 1, pressure: 1)!
        let expectedIndex = text.characterIndexForInsertion(at: pointerInText)
        text.mouseDown(with: click); text.mouseUp(with: click)
        check(text.selectedRange() == NSRange(location: expectedIndex, length: 0),
              "A pointer click selects the native insertion index after inverse projection")
        text.drawInsertionPoint(in: CGRect(x: 20, y: 3, width: 1, height: 20), color: .yellow, turnedOn: true)
        check(editor.artwork.sublayers![1].frame == CGRect(x: 20, y: 3, width: 1, height: 20)
              && !editor.artwork.sublayers![1].isHidden,
              "The native insertion-point callback draws a separate projected caret")
        text.setSelectedRange(NSRange(location: 6, length: 2))
        text.setMarkedText("输入", selectedRange: NSRange(location: 2, length: 0), replacementRange: NSRange(location: NSNotFound, length: 0))
        let composing = text.string, marked = text.markedRange(), selected = text.selectedRange()
        offset.x += 80; editor.refreshProjection()
        check(text.hasMarkedText() && text.string == composing && text.markedRange() == marked && text.selectedRange() == selected,
              "Projection does not replace text storage or interrupt Chinese marked text")
        text.insertText("中文", replacementRange: marked)
        check(!text.hasMarkedText() && text.string.contains("中文"), "Native marked text can commit after reprojection")
        let current = text.string
        text.undoManager?.removeAllActions(); text.undoManager?.beginUndoGrouping()
        text.breakUndoCoalescing(); text.insertText("x", replacementRange: NSRange(location: (text.string as NSString).length, length: 0))
        text.undoManager?.endUndoGrouping(); text.undoManager?.undo()
        check(text.string == current, "Native undo survives the retained-artwork input adapter")
        text.string = String(repeating: "A long line of text\n", count: 20_000)
        editor.resizeDocument(); editor.captureVisibleArtwork()
        check(editor.retainedPixelCount <= HUDProjectedTextEditor.maximumPixels && glyphLayer.contents != nil,
              "Long notes retain a viewport image rather than a document-sized bitmap")
        editor.scroll(delta: -50.5)
        check(abs(editor.scrollView.contentView.bounds.minY - 50.5) < 0.01, "The native viewport keeps fractional scroll offsets")
        editor.dispose()
        check(editor.superview == nil && editor.artwork.superlayer == nil && editor.retainedPixelCount == 0,
              "Disposal releases native surfaces, projected artwork and cached pixels")
        let fieldText = HUDProjectedTextView(frame: .zero)
        fieldText.font = .systemFont(ofSize: 20); fieldText.alignment = .center
        fieldText.string = "30:00"
        let field = HUDProjectedTextEditor(textView: fieldText, rect: rect, host: host, parent: host.layer!)
        defer { field.dispose() }
        field.configureSingleLine()
        check(fieldText.accessibilityRole() == .textField, "Single-line editors retain the native text-field accessibility role")
        let used = fieldText.layoutManager!.usedRect(for: fieldText.textContainer!)
        check(used.minX.isFinite && used.maxX <= rect.width && fieldText.frame.width == rect.width,
              "Centered single-line fields retain finite visible glyph positions")
        fieldText.string = String(repeating: "A long name ", count: 80)
        field.resizeDocument(); field.captureVisibleArtwork()
        check(fieldText.frame.width > rect.width && fieldText.frame.height == rect.height
              && field.retainedPixelCount <= HUDProjectedTextEditor.maximumPixels,
              "Long single-line values scroll horizontally without expanding the retained bitmap")
        let end = NSRange(location: (fieldText.string as NSString).length, length: 0)
        fieldText.setSelectedRange(end); fieldText.scrollRangeToVisible(end)
        check(field.scrollView.contentView.bounds.minX > 0,
              "Native caret reveal can reach the end of a long single-line value")
        count += compositionChecks()
        return count
    }

    /// Render the complete native view tree, not just the projected glyph image.
    /// The text view deliberately paints through TextKit without the draw guard:
    /// AppKit's independently retained ContentLayer is another path that can do
    /// this. Composition must keep those native pixels out of the HUD regardless.
    private static func compositionChecks() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let host = ProjectedTestHost(frame: CGRect(x: 0, y: 0, width: 600, height: 400))
        host.wantsLayer = true; host.layer!.backgroundColor = NSColor.black.cgColor
        let window = NSWindow(contentRect: CGRect(x: -10000, y: -10000, width: 600, height: 400),
                              styleMask: .borderless, backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false; window.contentView = host
        defer { window.close() }
        let text = AlwaysDrawingProjectedTextView(frame: .zero)
        text.font = .systemFont(ofSize: 20); text.textColor = .white; text.allowsUndo = true
        text.string = "Native editor regression"
        let rect = CGRect(x: 230, y: 180, width: 300, height: 80)
        let editor = HUDProjectedTextEditor(textView: text, rect: rect, host: host, parent: host.layer!)
        defer { editor.dispose() }
        window.makeFirstResponder(text)
        editor.resizeDocument(); editor.captureVisibleArtwork()
        func snapshot() -> NSBitmapImageRep {
            host.displayIfNeeded(); CATransaction.flush()
            let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 600, pixelsHigh: 400,
                bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                colorSpaceName: .deviceRGB, bytesPerRow: 2400, bitsPerPixel: 32)!
            bitmap.bitmapData!.initialize(repeating: 0, count: 2400 * 400)
            host.cacheDisplay(in: host.bounds, to: bitmap)
            return bitmap
        }
        func visiblePixels(_ bitmap: NSBitmapImageRep, _ rect: CGRect) -> Int {
            var result = 0
            for y in Int(rect.minY)..<Int(rect.maxY) {
                for x in Int(rect.minX)..<Int(rect.maxX) {
                    let color = bitmap.colorAt(x: x, y: y)!.usingColorSpace(.deviceRGB)!
                    if color.alphaComponent > 0.1 && max(color.redComponent, color.greenComponent, color.blueComponent) > 0.1 { result += 1 }
                }
            }
            return result
        }
        let nativeOrigin = CGRect(x: 0, y: 0, width: 300, height: 80)
        let composed = snapshot()
        check(visiblePixels(composed, nativeOrigin) == 0,
              "Even independently painted native TextKit pixels cannot leak at the screen origin")
        check(visiblePixels(composed, rect) > 100,
              "The same complete native-window snapshot retains visible projected text")
        check(window.firstResponder === text && text.accessibilityFrame() == editor.projectedScreenFrame,
              "Composition suppression retains native focus and projected accessibility geometry")
        text.insertText(" edited", replacementRange: NSRange(location: (text.string as NSString).length, length: 0))
        editor.captureVisibleArtwork()
        let edited = snapshot()
        check(visiblePixels(edited, nativeOrigin) == 0 && visiblePixels(edited, rect) > 100,
              "Editing and backing-layer refresh do not recreate the origin duplicate")
        text.string = ""; editor.placeholder = "Title"; editor.placeholderColor = NSColor(white: 0.5, alpha: 1)
        editor.captureVisibleArtwork()
        let empty = snapshot()
        check(text.string.isEmpty && visiblePixels(empty, nativeOrigin) == 0 && visiblePixels(empty, rect) > 15,
              "An empty editor displays its grey placeholder only in projected artwork")
        text.string = "Real text"; editor.captureVisibleArtwork()
        check(editor.artwork.sublayers!.last!.isHidden && text.string == "Real text",
              "Real input hides placeholder without inserting it into the document")
        return count
    }
}
private final class ProjectedTestHost: NSView { override var isFlipped: Bool { true } }
private final class AlwaysDrawingProjectedTextView: HUDProjectedTextView {
    override func draw(_ dirtyRect: NSRect) {
        guard let manager = layoutManager, let container = textContainer else { return }
        let range = manager.glyphRange(for: container)
        manager.drawBackground(forGlyphRange: range, at: textContainerOrigin)
        manager.drawGlyphs(forGlyphRange: range, at: textContainerOrigin)
    }
}
