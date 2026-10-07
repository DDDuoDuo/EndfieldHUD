import AppKit
import QuartzCore

/// A real TextKit editor at stable logical dimensions, with its visible artwork
/// on an independently owned HUD layer. AppKit owns the text view's backing
/// layer; it is never transformed. Only the small visible viewport is cached.
final class HUDProjectedTextEditor: NSView {
    static let maximumPixels = 2_097_152
    let textView: HUDProjectedTextView
    let scrollView: NSScrollView
    let artwork = CALayer()
    private let glyphs = CALayer()
    private let caret = CALayer()
    private let scrollIndicator = CALayer()
    private let placeholderLayer = CATextLayer()
    /// Placeholder is artwork only: it never enters the native text storage,
    /// selection, undo history or a saved document.
    var placeholder: String = "" { didSet { invalidateArtwork() } }
    var placeholderColor: NSColor = .gray { didSet { invalidateArtwork() } }
    private weak var coordinateHost: NSView?
    var project: ((CGRect) -> CGRect)?
    var unproject: ((CGPoint) -> CGPoint?)?
    let logicalRect: CGRect
    private var observers: [NSObjectProtocol] = []
    private var refreshQueued = false
    private var disposed = false
    private(set) var captureCount = 0
    private(set) var retainedPixelCount = 0
    private var caretRect = CGRect.zero
    private var caretVisible = false
    private var singleLine = false
    override var isFlipped: Bool { true }

    init(textView: HUDProjectedTextView, rect: CGRect, host: NSView, parent: CALayer) {
        self.textView = textView; logicalRect = rect; coordinateHost = host
        scrollView = HUDProjectedTextScrollView(frame: CGRect(origin: .zero, size: rect.size))
        super.init(frame: host.bounds)
        (scrollView as? HUDProjectedTextScrollView)?.projectedFrame = { [weak self] in self?.projectedScreenFrame ?? .zero }
        autoresizingMask = [.width, .height]
        scrollView.borderType = .noBorder; scrollView.drawsBackground = false
        scrollView.hasVerticalScroller = false; scrollView.hasHorizontalScroller = false
        scrollView.scrollerStyle = .overlay
        // NSTextView's cacheDisplay can populate AppKit-owned backing layers.
        // draw(_:) alone cannot stop those layers from subsequently appearing
        // at the native origin. Suppress the entire native subtree at composition
        // time; the real editor remains attached for focus, TextKit, IME and AX.
        // Capturing the child text view still obtains full-opacity glyphs.
        scrollView.wantsLayer = true
        scrollView.alphaValue = 0
        scrollView.contentView.postsBoundsChangedNotifications = true
        textView.frame = CGRect(origin: .zero, size: rect.size)
        textView.drawsBackground = false
        textView.isHorizontallyResizable = false; textView.isVerticallyResizable = true
        textView.minSize = NSSize(width: 0, height: rect.height)
        textView.maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
        textView.textContainerInset = .zero
        textView.textContainer?.lineFragmentPadding = 0
        textView.textContainer?.widthTracksTextView = true
        textView.textContainer?.containerSize = NSSize(width: rect.width, height: .greatestFiniteMagnitude)
        textView.autoresizingMask = [.width]
        scrollView.documentView = textView; addSubview(scrollView)
        textView.projectionOwner = self
        artwork.frame = rect; artwork.masksToBounds = true; artwork.cornerRadius = 3
        artwork.zPosition = 90; artwork.borderWidth = 1
        glyphs.frame = artwork.bounds; glyphs.contentsGravity = .resize
        artwork.addSublayer(glyphs); artwork.addSublayer(caret)
        scrollIndicator.backgroundColor = NSColor.gray.withAlphaComponent(0.65).cgColor
        scrollIndicator.cornerRadius = 1.5; artwork.addSublayer(scrollIndicator); parent.addSublayer(artwork)
        placeholderLayer.isWrapped = true; placeholderLayer.truncationMode = .end
        artwork.addSublayer(placeholderLayer)
        host.addSubview(self)
        let center = NotificationCenter.default
        for name in [NSText.didChangeNotification, NSTextView.didChangeSelectionNotification] {
            observers.append(center.addObserver(forName: name, object: textView, queue: .main) { [weak self] _ in self?.invalidateArtwork() })
        }
        observers.append(center.addObserver(forName: NSView.boundsDidChangeNotification,
            object: scrollView.contentView, queue: .main) { [weak self] _ in
                self?.invalidateArtwork(); self?.refreshProjection()
        })
        invalidateArtwork()
    }
    required init?(coder: NSCoder) { nil }
    deinit { observers.forEach(NotificationCenter.default.removeObserver); artwork.removeFromSuperlayer() }

    func dispose() {
        guard !disposed else { return }; disposed = true
        observers.forEach(NotificationCenter.default.removeObserver); observers.removeAll()
        textView.projectionOwner = nil; glyphs.contents = nil; retainedPixelCount = 0
        (scrollView as? HUDProjectedTextScrollView)?.projectedFrame = nil
        artwork.removeFromSuperlayer(); scrollView.removeFromSuperview(); removeFromSuperview()
    }
    func setAppearance(background: NSColor, border: NSColor, radius: CGFloat = 3) {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        artwork.backgroundColor = background.cgColor; artwork.borderColor = border.cgColor; artwork.cornerRadius = radius
        CATransaction.commit()
    }
    /// Called as the HUD moves. This deliberately does not re-layout or redraw
    /// text: the parent layer already supplies the entire perspective transform.
    func refreshProjection() {
        guard !disposed else { return }
        textView.inputContext?.invalidateCharacterCoordinates()
    }
    /// NSTextField normally provides horizontal scrolling through its shared
    /// field editor. Explicit text views need the same unbounded logical line.
    func configureSingleLine() {
        singleLine = true
        textView.setAccessibilityRole(.textField)
        textView.isHorizontallyResizable = true; textView.isVerticallyResizable = false
        textView.textContainer?.widthTracksTextView = false
        textView.textContainer?.maximumNumberOfLines = 1
        textView.textContainer?.containerSize = NSSize(width: logicalRect.width, height: logicalRect.height)
        let font = textView.font ?? .systemFont(ofSize: 13)
        textView.textContainerInset = NSSize(width: 3, height: max(0, (logicalRect.height - font.ascender + font.descender - font.leading) / 2))
        resizeDocument()
    }
    func resizeDocument() {
        guard let manager = textView.layoutManager, let container = textView.textContainer else { return }
        // A finite width preserves center/right paragraph alignment. An
        // effectively infinite width would place centered glyphs far offscreen.
        let singleLineWidth = singleLine ? max(logicalRect.width, ceil(textView.textStorage?.size().width ?? 0) + textView.textContainerInset.width * 2 + 5) : logicalRect.width
        if singleLine {
            container.containerSize = NSSize(width: singleLineWidth - textView.textContainerInset.width * 2,
                                            height: logicalRect.height)
        }
        manager.ensureLayout(for: container)
        let used = manager.usedRect(for: container)
        let height = singleLine ? logicalRect.height : max(logicalRect.height, ceil(used.maxY + textView.textContainerInset.height * 2) + 1)
        let width = singleLine ? singleLineWidth : logicalRect.width
        if textView.frame.size != NSSize(width: width, height: height) {
            textView.setFrameSize(NSSize(width: width, height: height))
        }
        invalidateArtwork()
    }
    func invalidateArtwork() {
        guard !disposed, !refreshQueued else { return }; refreshQueued = true
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }; self.refreshQueued = false
            self.captureVisibleArtwork()
        }
    }
    /// Synchronous entry also permits deterministic offscreen verification.
    func captureVisibleArtwork() {
        guard !disposed, logicalRect.width > 0, logicalRect.height > 0 else { return }
        let viewport = scrollView.contentView.bounds
        guard viewport.width.isFinite, viewport.height.isFinite, viewport.width > 0, viewport.height > 0 else { return }
        // A small fixed oversample keeps the live editor as crisp as the HUD's
        // text layers. Pose changes never create differently sized snapshots.
        let desired = min(4, ceil((coordinateHost?.window?.backingScaleFactor ?? 2) * 1.35))
        let scale = min(desired, sqrt(CGFloat(Self.maximumPixels) / max(1, viewport.width * viewport.height)),
                        4096 / max(viewport.width, viewport.height))
        let width = max(1, Int(floor(viewport.width * scale))), height = max(1, Int(floor(viewport.height * scale)))
        guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: width * 4, bitsPerPixel: 32) else { return }
        bitmap.size = viewport.size
        bitmap.bitmapData?.initialize(repeating: 0, count: width * height * 4)
        textView.capturingArtwork = true
        textView.cacheDisplay(in: viewport, to: bitmap)
        textView.capturingArtwork = false
        guard let image = bitmap.cgImage else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        glyphs.contents = image; glyphs.contentsScale = scale
        updatePlaceholder(scale: scale)
        updateCaretLayer()
        let overflow = max(0, textView.frame.height - viewport.height)
        scrollIndicator.isHidden = overflow <= 0
        let thumb = max(16, viewport.height * viewport.height / max(1, textView.frame.height))
        scrollIndicator.frame = CGRect(x: max(0, viewport.width - 4), y: overflow > 0 ? (viewport.height - thumb) * viewport.minY / overflow : 0, width: 3, height: thumb)
        CATransaction.commit()
        retainedPixelCount = width * height; captureCount += 1
    }
    private func updatePlaceholder(scale: CGFloat) {
        placeholderLayer.isHidden = !textView.string.isEmpty || placeholder.isEmpty
        guard !placeholderLayer.isHidden else { placeholderLayer.string = nil; return }
        let font = textView.font ?? .systemFont(ofSize: 13)
        let inset = textView.textContainerInset
        placeholderLayer.frame = artwork.bounds.insetBy(dx: inset.width, dy: inset.height)
        placeholderLayer.font = font; placeholderLayer.fontSize = font.pointSize
        placeholderLayer.foregroundColor = placeholderColor.cgColor
        placeholderLayer.contentsScale = scale
        placeholderLayer.alignmentMode = textView.alignment == .center ? .center : textView.alignment == .right ? .right : .left
        placeholderLayer.string = placeholder
    }
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? {
        guard !disposed, let source = logicalPoint(convert(point, from: superview)), logicalRect.contains(source) else { return nil }
        return textView
    }
    fileprivate func textCursor(atWindowPoint point: NSPoint) -> NSCursor? {
        guard !disposed, !isHiddenOrHasHiddenAncestor, !textView.isHiddenOrHasHiddenAncestor,
              textView.isEditable || textView.isSelectable,
              let host = coordinateHost, superview === host,
              let window, host.window === window, textView.window === window,
              visibleRect.contains(convert(point, from: nil)),
              hitTest(host.convert(point, from: nil)) === textView else { return nil }
        return .iBeam
    }
    func logicalPoint(_ point: CGPoint) -> CGPoint? { unproject?(point) ?? (unproject == nil ? point : nil) }
    func textPoint(screen: CGPoint) -> CGPoint? {
        guard let host = coordinateHost, let window = host.window else { return nil }
        return textPoint(host: host.convert(window.convertPoint(fromScreen: screen), from: nil))
    }
    func textPoint(host point: CGPoint) -> CGPoint? {
        guard let source = logicalPoint(point) else { return nil }
        let offset = scrollView.contentView.bounds.origin
        return CGPoint(x: source.x - logicalRect.minX + offset.x, y: source.y - logicalRect.minY + offset.y)
    }
    func textPoint(event: NSEvent) -> CGPoint? {
        guard let host = coordinateHost else { return nil }
        return textPoint(host: host.convert(event.locationInWindow, from: nil))
    }
    func screenRect(text rect: CGRect) -> CGRect {
        guard let host = coordinateHost, let window = host.window else { return .zero }
        let offset = scrollView.contentView.bounds.origin
        let source = rect.offsetBy(dx: logicalRect.minX - offset.x, dy: logicalRect.minY - offset.y)
        return window.convertToScreen(host.convert(project?(source) ?? source, to: nil))
    }
    func nativeScreenRectToProjected(_ rect: CGRect) -> CGRect {
        guard let window = textView.window else { return .zero }
        return screenRect(text: textView.convert(window.convertFromScreen(rect), from: nil))
    }
    var projectedScreenFrame: CGRect { screenRect(text: scrollView.contentView.bounds) }
    func insertionPoint(_ rect: CGRect, color: NSColor, visible: Bool) {
        caretRect = rect; caretVisible = visible
        caret.backgroundColor = color.cgColor
        CATransaction.begin(); CATransaction.setDisableActions(true); updateCaretLayer(); CATransaction.commit()
    }
    private func updateCaretLayer() {
        caret.frame = caretRect.offsetBy(dx: -scrollView.contentView.bounds.minX, dy: -scrollView.contentView.bounds.minY)
        caret.isHidden = !caretVisible || textView.selectedRange().length != 0
    }
    func scroll(delta: CGFloat) {
        let clip = scrollView.contentView
        let maximum = max(0, textView.bounds.height - clip.bounds.height)
        clip.scroll(to: CGPoint(x: 0, y: min(maximum, max(0, clip.bounds.minY - delta))))
        scrollView.reflectScrolledClipView(clip)
    }
    func scroll(event: NSEvent) {
        guard let host = coordinateHost else { return }
        let point = host.convert(event.locationInWindow, from: nil)
        let delta = event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12)
        let from = logicalPoint(point), to = logicalPoint(CGPoint(x: point.x, y: point.y + delta))
        scroll(delta: from.flatMap { start in to.map { $0.y - start.y } } ?? delta)
    }
}

private final class HUDProjectedTextScrollView: NSScrollView {
    var projectedFrame: (() -> CGRect)?
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

/// NSTextInputClient, TextKit, rich text, undo and marked-text composition remain
/// native. Only pointer coordinates and public screen-coordinate queries differ.
class HUDProjectedTextView: NSTextView {
    fileprivate weak var projectionOwner: HUDProjectedTextEditor?
    fileprivate var capturingArtwork = false
    private var selectionAnchor = NSRange(location: 0, length: 0)
    private var pointerGranularity: NSSelectionGranularity = .selectByCharacter
    /// The HUD's existing cursor owner uses the projected input region. The
    /// invisible native text/scroll rectangles cannot choose it for us. This
    /// query neither changes the cursor nor creates a competing tracking area.
    func projectedCursor(atWindowPoint point: NSPoint) -> NSCursor? {
        projectionOwner?.textCursor(atWindowPoint: point)
    }
    override func draw(_ dirtyRect: NSRect) {
        if capturingArtwork { super.draw(dirtyRect) }
        else { projectionOwner?.invalidateArtwork() }
    }
    override func drawInsertionPoint(in rect: NSRect, color: NSColor, turnedOn flag: Bool) {
        projectionOwner?.insertionPoint(rect, color: color, visible: flag)
    }
    override func firstRect(forCharacterRange range: NSRange, actualRange: NSRangePointer?) -> NSRect {
        let native = super.firstRect(forCharacterRange: range, actualRange: actualRange)
        return projectionOwner?.nativeScreenRectToProjected(native) ?? native
    }
    override func characterIndex(for point: NSPoint) -> Int {
        guard let local = projectionOwner?.textPoint(screen: point) else { return super.characterIndex(for: point) }
        return characterIndexForInsertion(at: local)
    }
    override func accessibilityFrame() -> NSRect { projectionOwner?.projectedScreenFrame ?? super.accessibilityFrame() }
    override func accessibilityFrame(for range: NSRange) -> NSRect {
        let native = super.accessibilityFrame(for: range)
        return projectionOwner?.nativeScreenRectToProjected(native) ?? native
    }
    override func accessibilityRange(for point: NSPoint) -> NSRange {
        guard let local = projectionOwner?.textPoint(screen: point) else { return super.accessibilityRange(for: point) }
        let index = characterIndexForInsertion(at: local), text = string as NSString
        return index < text.length ? text.rangeOfComposedCharacterSequence(at: index) : NSRange(location: index, length: 0)
    }
    override func mouseDown(with event: NSEvent) {
        guard let point = projectionOwner?.textPoint(event: event) else { return }
        window?.makeFirstResponder(self)
        if hasMarkedText() { inputContext?.discardMarkedText(); unmarkText() }
        let index = characterIndexForInsertion(at: point)
        pointerGranularity = event.clickCount >= 3 ? .selectByParagraph : event.clickCount == 2 ? .selectByWord : .selectByCharacter
        let proposed = NSRange(location: index, length: 0)
        if event.modifierFlags.contains(.shift) {
            selectionAnchor = NSRange(location: selectedRange().location, length: 0)
            selectPointerRange(index: index)
        } else {
            selectionAnchor = selectionRange(forProposedRange: proposed, granularity: pointerGranularity)
            setSelectedRange(selectionAnchor, affinity: .downstream, stillSelecting: true)
        }
        projectionOwner?.invalidateArtwork()
    }
    override func mouseDragged(with event: NSEvent) {
        guard let point = projectionOwner?.textPoint(event: event) else { return }
        selectPointerRange(index: characterIndexForInsertion(at: point))
        scrollRangeToVisible(selectedRange()); projectionOwner?.invalidateArtwork()
    }
    private func selectPointerRange(index: Int) {
        let edge = selectionRange(forProposedRange: NSRange(location: index, length: 0), granularity: pointerGranularity)
        let lower = min(selectionAnchor.location, edge.location), upper = max(NSMaxRange(selectionAnchor), NSMaxRange(edge))
        setSelectedRange(NSRange(location: lower, length: upper - lower), affinity: .downstream, stillSelecting: true)
    }
    override func mouseUp(with event: NSEvent) {
        setSelectedRange(selectedRange(), affinity: .downstream, stillSelecting: false)
        projectionOwner?.invalidateArtwork()
    }
    override func scrollWheel(with event: NSEvent) {
        projectionOwner?.scroll(event: event)
    }
}
