import AppKit
import QuartzCore

/// A real TextKit editor at stable logical dimensions, with its visible artwork
/// on an independently owned HUD layer. AppKit owns the text view's backing
/// layer; it is never transformed. Only the small visible viewport is cached.
final class HUDProjectedTextEditor: NSView {
    static let maximumPixels = 2_097_152
    /// Offscreen previews draw in a window that never becomes key.
    static var treatsWindowAsKeyForPreview = false
    let textView: HUDProjectedTextView
    let scrollView: NSScrollView
    let artwork = CALayer()
    private let glyphs = CALayer()
    private let caret = CALayer()
    private let scrollIndicator = CALayer()
    /// The indicator hides while idle and shows while the text scrolls.
    private lazy var scrollBar = HUDScrollBarVisibility(bar: scrollIndicator)
    private let placeholderLayer = HUDStandardTextLayer()
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
    /// The insertion point in text-view coordinates (from TextKit's layout).
    private var caretRect = CGRect.zero
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
        _ = scrollBar
        placeholderLayer.isWrapped = true; placeholderLayer.truncationMode = .end
        artwork.addSublayer(placeholderLayer)
        host.addSubview(self)
        let center = NotificationCenter.default
        for name in [NSText.didChangeNotification, NSTextView.didChangeSelectionNotification] {
            observers.append(center.addObserver(forName: name, object: textView, queue: .main) { [weak self] _ in self?.invalidateArtwork() })
        }
        // The caret shows only in the key window (as AppKit's does): it hides
        // when the HUD loses focus or closes, and shows again when it returns.
        if let window = host.window {
            for name in [NSWindow.didBecomeKeyNotification, NSWindow.didResignKeyNotification] {
                observers.append(center.addObserver(forName: name, object: window, queue: .main) { [weak self] _ in
                    self?.refreshCaret(restartBlink: true)
                })
            }
        }
        observers.append(center.addObserver(forName: NSView.boundsDidChangeNotification,
            object: scrollView.contentView, queue: .main) { [weak self] _ in
                self?.invalidateArtwork(); self?.refreshProjection(); self?.scrollBar.noteScroll()
        })
        invalidateArtwork()
    }
    required init?(coder: NSCoder) { nil }
    deinit { observers.forEach(NotificationCenter.default.removeObserver); artwork.removeFromSuperlayer() }

    func dispose() {
        guard !disposed else { return }; disposed = true
        observers.forEach(NotificationCenter.default.removeObserver); observers.removeAll()
        textView.projectionOwner = nil; glyphs.contents = nil; retainedPixelCount = 0
        caret.removeAllAnimations(); caret.isHidden = true; scrollBar.reset()
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
        let font = textView.font ?? HUDFontResolver.nsFont(size: 13)
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
        let font = textView.font ?? HUDFontResolver.nsFont(size: 13)
        let inset = textView.textContainerInset
        placeholderLayer.frame = artwork.bounds.insetBy(dx: inset.width, dy: inset.height)
        placeholderLayer.face = font; placeholderLayer.fontSize = font.pointSize
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
    /// AppKit drew its (invisible) insertion point. AppKit draws it only in
    /// the key window and, since macOS 14, without its blink phases, so the
    /// projected caret follows the layout instead (`updateCaretLayer`).
    func insertionPoint(_ rect: CGRect, color: NSColor, visible: Bool) { refreshCaret() }
    /// Focus, key window or selection changed.
    func refreshCaret(restartBlink: Bool = false) {
        guard !disposed else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true); updateCaretLayer(restartBlink: restartBlink); CATransaction.commit()
    }
    /// The caret shows while the editor is first responder of the key window
    /// with an empty selection. It blinks like AppKit's caret (a render-server
    /// opacity animation, no timer), solid for a moment after every move, and
    /// is removed with its animation whenever hidden. Reduce Motion keeps it solid.
    private func updateCaretLayer(restartBlink: Bool = false) {
        let window = textView.window
        let shown = (window?.isKeyWindow == true || Self.treatsWindowAsKeyForPreview) && window?.firstResponder === textView
            && textView.selectedRange().length == 0 && textView.isEditable
        if shown, let rect = textView.insertionRect() {
            // Moved (typing, arrows, a click): solid again, then blinking.
            if rect != caretRect { caretRect = rect; caret.removeAnimation(forKey: Self.blinkKey) }
            caret.backgroundColor = textView.insertionPointColor.cgColor
        }
        caret.frame = caretRect.offsetBy(dx: -scrollView.contentView.bounds.minX, dy: -scrollView.contentView.bounds.minY)
        let wasHidden = caret.isHidden
        caret.isHidden = !shown
        guard shown else { caret.removeAnimation(forKey: Self.blinkKey); return }
        guard restartBlink || wasHidden || caret.animation(forKey: Self.blinkKey) == nil else { return }
        caret.removeAnimation(forKey: Self.blinkKey)
        guard !HUDRuntimeAppearance.reduceMotion else { return }
        let blink = CAKeyframeAnimation(keyPath: "opacity")
        blink.values = [1, 1, 0, 0, 1]; blink.keyTimes = [0, 0.5, 0.58, 0.92, 1]
        blink.duration = 1.06; blink.repeatCount = .infinity
        caret.add(blink, forKey: Self.blinkKey)
    }
    private static let blinkKey = "hud.caret.blink"
    var caretStateForTesting: (visible: Bool, blinking: Bool, frame: CGRect) {
        (!caret.isHidden, caret.animation(forKey: Self.blinkKey) != nil, caret.frame)
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
    /// The insertion point of an empty selection in view coordinates, from
    /// the layout: a line-high bar at the caret's glyph edge (the extra line
    /// fragment after a final newline or in an empty text).
    func insertionRect() -> CGRect? {
        guard let manager = layoutManager, let container = textContainer else { return nil }
        let text = string as NSString, index = min(selectedRange().location, text.length)
        manager.ensureLayout(for: container)
        let origin = textContainerOrigin, width: CGFloat = 1.5
        let atEnd = index >= text.length
        if text.length == 0 || (atEnd && manager.extraLineFragmentTextContainer != nil) {
            let line = manager.extraLineFragmentRect
            var x = line.minX
            if text.length == 0 {
                switch alignment {
                case .right: x = line.maxX - width
                case .center: x = line.midX - width / 2
                default: break
                }
            }
            return CGRect(x: x + origin.x, y: line.minY + origin.y, width: width, height: max(1, line.height))
        }
        let glyph = manager.glyphIndexForCharacter(at: atEnd ? text.length - 1 : index)
        let line = manager.lineFragmentRect(forGlyphAt: glyph, effectiveRange: nil)
        let x = atEnd ? manager.boundingRect(forGlyphRange: NSRange(location: glyph, length: 1), in: container).maxX
            : line.minX + manager.location(forGlyphAt: glyph).x
        return CGRect(x: min(x, line.maxX - width) + origin.x, y: line.minY + origin.y, width: width, height: line.height)
    }
    // The window records the new first responder only after these return.
    override func becomeFirstResponder() -> Bool {
        let accepted = super.becomeFirstResponder()
        if accepted { DispatchQueue.main.async { [weak self] in self?.projectionOwner?.refreshCaret(restartBlink: true) } }
        return accepted
    }
    override func resignFirstResponder() -> Bool {
        let accepted = super.resignFirstResponder()
        if accepted { DispatchQueue.main.async { [weak self] in self?.projectionOwner?.refreshCaret() } }
        return accepted
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
    /// The standard editing shortcuts work without a menu bar (the HUD is an
    /// accessory app, often not active): cut, copy, paste, select all, undo, redo.
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        guard window?.firstResponder === self, let key = event.charactersIgnoringModifiers?.lowercased() else {
            return super.performKeyEquivalent(with: event)
        }
        switch (event.modifierFlags.intersection([.command, .shift, .option, .control]), key) {
        case ([.command], "x"): cut(nil)
        case ([.command], "c"): copy(nil)
        case ([.command], "v"): paste(nil)
        case ([.command], "a"): selectAll(nil)
        case ([.command], "z"): if allowsUndo, undoManager?.canUndo == true { undoManager?.undo() }
        case ([.command, .shift], "z"): if allowsUndo, undoManager?.canRedo == true { undoManager?.redo() }
        default: return super.performKeyEquivalent(with: event)
        }
        return true
    }
    /// A right-click places the caret where it landed (outside a selection)
    /// and opens a short editing menu at the pointer; AppKit's own would hit
    /// test the unprojected text.
    override func rightMouseDown(with event: NSEvent) {
        guard let point = projectionOwner?.textPoint(event: event) else { return }
        window?.makeFirstResponder(self)
        let index = characterIndexForInsertion(at: point), selection = selectedRange()
        if !(selection.length > 0 && index >= selection.location && index <= NSMaxRange(selection)) {
            setSelectedRange(NSRange(location: index, length: 0))
        }
        projectionOwner?.invalidateArtwork()
        if let menu = menu(for: event) { NSMenu.popUpContextMenu(menu, with: event, for: self) }
    }
    override func menu(for event: NSEvent) -> NSMenu? {
        let menu = NSMenu()
        for (title, action) in [(L10n.text("Cut", "剪切"), #selector(cut(_:))), (L10n.text("Copy", "复制"), #selector(copy(_:))),
                                (L10n.text("Paste", "粘贴"), #selector(paste(_:))), (L10n.text("Select All", "全选"), #selector(selectAll(_:)))] {
            let item = NSMenuItem(title: title, action: action, keyEquivalent: ""); item.target = self
            menu.addItem(item)
        }
        return menu
    }
}
