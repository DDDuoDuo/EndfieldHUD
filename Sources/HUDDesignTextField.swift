import AppKit
import Carbon
import QuartzCore

/// The redesign's text field (spec §2.11): a dark (or light) field with the
/// yellow focus ring and bar, red when invalid. While idle it is a drawn
/// label; editing attaches a real TextKit editor projected onto the plane
/// (HUDProjectedTextEditor), so IME, selection, undo and VoiceOver work.
final class HUDDesignTextField: HUDControl {
    struct Options {
        var light = false
        var multiline = false
        /// Return commits (single line always; multi-line when true, Shift-Return inserts a line).
        var returnCommits = true
        var secure = false
        var maximumLength: Int? = nil
        var selectsAllOnBegin = false
        var style = HUDType.field
        var placeholderColor = HUDColor.muted
        var inset = CGSize(width: 10, height: 0)
        /// User-styled text (the Notes note): drawn and edited with these
        /// attributes (any installed font, bold, italic, underline, colour,
        /// paragraph style) instead of `style`; the idle text is top-aligned.
        var attributes: [NSAttributedString.Key: Any]? = nil
        /// Esc ends editing and keeps the text (a note) instead of restoring it.
        var escapeCommits = false
        /// A bare text area inside its own panel: no fill, ring or focus bar.
        var plain = false
        /// Replaces the yellow focus ring, bar and caret (Personal ID's card accent).
        var accent: CGColor? = nil
        /// Replaces the dark field fill (Personal ID's #212121 inline editor).
        var background: CGColor? = nil
        /// Right-aligned numbers (Personal ID levels and counters).
        var alignment: HUDTextLayer.Alignment = .left
    }

    let options: Options
    let frame: HUDFieldFrame
    private(set) var text: String
    var placeholder: String { didSet { renderIdle() } }
    var onBegin: (() -> Void)?
    var onChange: ((String) -> Void)?
    /// Return, Tab, a click elsewhere or leaving the module.
    var onCommit: ((String) -> Void)?
    /// Esc: the edit is discarded and the previous text restored.
    var onCancel: (() -> Void)?
    /// Return committed the edit (after onCommit): forms submit on it.
    var onReturn: (() -> Void)?
    /// Tab committed a single-line edit (after onCommit); true with Shift.
    var onTab: ((Bool) -> Void)?
    weak var canvas: HUDDesignCanvas?
    private let idle = HUDTextLayer()
    private let styledIdle = HUDStyledTextLayer()
    /// The releasing click, while it activates the field (caret placement).
    private var pressPoint: CGPoint?
    private var textView: HUDDesignNativeTextView?
    private var editor: HUDProjectedTextEditor?
    private var original = ""
    private var invalid = false
    private var finishing = false
    private let delegateProxy = HUDDesignTextDelegate()
    var isEditing: Bool { editor != nil }

    init(id: String, label: String, text: String, placeholder: String = "", size: CGSize, options: Options = Options(),
         env: HUDDesignEnvironment) {
        self.options = options; self.text = text; self.placeholder = placeholder
        frame = HUDFieldFrame(size: size, light: options.light, plain: options.plain)
        if let accent = options.accent { frame.focusColor = accent }
        super.init(id: id, label: label, env: env, layer: frame.layer)
        HUDMotion.perform {
            if let background = options.background { frame.layer.backgroundColor = background }
            idle.maximumLines = options.multiline ? 0 : 1
            frame.layer.addSublayer(options.attributes == nil ? idle : styledIdle)
            frame.layer.masksToBounds = true
        }
        setTarget(frame.layer.bounds)
        delegateProxy.didChange = { [weak self] in self?.textDidChange($0) }
        delegateProxy.didEnd = { [weak self] in self?.textDidEndEditing($0) }
        renderIdle()
    }

    deinit { disposeEditor() }

    private var textRect: CGRect {
        let bounds = frame.layer.bounds
        if options.multiline { return bounds.insetBy(dx: options.inset.width, dy: max(6, options.inset.height)) }
        return CGRect(x: options.inset.width, y: 0, width: bounds.width - options.inset.width * 2, height: bounds.height)
    }

    private var textStyle: HUDTextStyle {
        options.light ? options.style.colored(HUDColor.ink) : options.style
    }

    func setText(_ value: String) {
        text = bounded(value)
        if let textView, !textView.hasMarkedText(), textView.string != text { textView.string = text; editor?.resizeDocument() }
        renderIdle()
    }

    func setInvalid(_ value: Bool, shake: Bool = false) {
        invalid = value
        frame.setState(value ? .invalid : (isEditing ? .focused : .normal))
        guard shake, !env.reducesMotion else { return }
        let animation = CAKeyframeAnimation(keyPath: "transform.translation.x")
        animation.values = [0, -6, 6, -6, 6, 0]; animation.keyTimes = [0, 0.2, 0.4, 0.6, 0.8, 1]
        animation.duration = 0.32; animation.isAdditive = true
        frame.layer.add(animation, forKey: "hud.shake")
    }

    private func renderIdle() {
        let rect = textRect
        if var attributes = options.attributes {
            let shown = options.secure ? String(repeating: "•", count: text.count) : text
            if shown.isEmpty { attributes[.foregroundColor] = NSColor(cgColor: options.placeholderColor) }
            HUDMotion.perform {
                styledIdle.isHidden = isEditing
                styledIdle.frame = rect
                styledIdle.text = NSAttributedString(string: shown.isEmpty ? placeholder : shown, attributes: attributes)
                styledIdle.contentsScale = env.scale(for: styledIdle)
            }
            return
        }
        HUDMotion.perform {
            idle.isHidden = isEditing
            let shown = options.secure ? String(repeating: "•", count: text.count) : text
            if shown.isEmpty {
                idle.runs = [HUDText.Run(placeholder, textStyle.colored(options.placeholderColor))]
            } else {
                idle.runs = [HUDText.Run(shown, textStyle)]
            }
            let lineHeight = textStyle.size * max(1.2, textStyle.lineHeight)
            idle.lineFrame = options.multiline ? CGRect(x: rect.minX, y: rect.minY, width: rect.width, height: rect.height)
                : CGRect(x: rect.minX, y: (rect.height - lineHeight) / 2, width: rect.width, height: lineHeight)
            idle.alignment = options.alignment
        }
        if options.multiline {
            HUDMotion.perform { idle.maximumLines = max(1, Int(rect.height / (textStyle.size * 1.5))) }
        }
        env.applyScale(to: idle)
    }

    // MARK: Editing

    override func activate() {
        guard isEnabled else { return }
        beginEditing()
        onActivate?()
    }

    /// Attaches the native editor. `selection` restores an earlier caret.
    func beginEditing(selection: NSRange? = nil) {
        guard isEnabled, editor == nil, let canvas, let host = canvas.context.host, host.window != nil else { return }
        canvas.textFields.filter { $0 !== self && $0.isEditing }.forEach { $0.endEditing(commit: true) }
        original = text
        let view = HUDDesignNativeTextView(frame: .zero)
        view.isRichText = false; view.importsGraphics = false; view.allowsUndo = !options.secure
        view.isEditable = true; view.isSelectable = true
        view.isAutomaticQuoteSubstitutionEnabled = false; view.isAutomaticDashSubstitutionEnabled = false
        view.isContinuousSpellCheckingEnabled = false; view.isGrammarCheckingEnabled = false
        view.isAutomaticSpellingCorrectionEnabled = false
        let font = HUDText.font(textStyle, language: HUDTextLanguage.tag()) as NSFont
        view.font = font
        view.textColor = NSColor(cgColor: textStyle.color) ?? .white
        view.insertionPointColor = options.light ? NSColor(cgColor: HUDColor.ink) ?? .black : NSColor(cgColor: options.accent ?? HUDColor.accentInk) ?? .yellow
        view.alignment = options.alignment == .right ? .right : options.alignment == .center ? .center : .left
        view.selectedTextAttributes = [.backgroundColor: NSColor(cgColor: HUDColor.yellow.copy(alpha: 0.35) ?? HUDColor.yellow) ?? .selectedTextBackgroundColor]
        view.string = text
        if let attributes = options.attributes {
            view.font = attributes[.font] as? NSFont ?? font
            view.textColor = attributes[.foregroundColor] as? NSColor ?? view.textColor
            view.defaultParagraphStyle = attributes[.paragraphStyle] as? NSParagraphStyle
            view.typingAttributes = attributes
            view.textStorage?.setAttributes(attributes, range: NSRange(location: 0, length: view.textStorage?.length ?? 0))
        }
        view.secure = options.secure
        view.delegate = delegateProxy
        view.setAccessibilityLabel(label)
        if !options.multiline { view.textContainer?.maximumNumberOfLines = 1; view.textContainer?.lineBreakMode = .byClipping }
        view.onKey = { [weak self] event in self?.handleEditorKey(event) ?? false }
        textView = view
        let surface = HUDProjectedTextEditor(textView: view, rect: textRect, host: host, parent: frame.layer)
        if !options.multiline { surface.configureSingleLine() }
        surface.project = { [weak self] rect in
            guard let self, let canvas = self.canvas else { return rect }
            return canvas.context.projected(canvas.root.convert(rect, from: self.frame.layer))
        }
        surface.unproject = { [weak self] point in
            guard let self, let canvas = self.canvas, let box = canvas.context.unproject?(point) else { return nil }
            return self.frame.layer.convert(box, from: canvas.root)
        }
        surface.setAppearance(background: .clear, border: .clear, radius: 0)
        surface.placeholder = placeholder
        surface.placeholderColor = NSColor(cgColor: options.placeholderColor) ?? .gray
        editor = surface
        surface.resizeDocument()
        frame.setState(invalid ? .invalid : .focused)
        renderIdle()
        host.window?.makeFirstResponder(view)
        if let selection, selection.location <= view.string.utf16.count {
            view.setSelectedRange(NSRange(location: selection.location, length: min(selection.length, view.string.utf16.count - selection.location)))
        } else if options.selectsAllOnBegin { view.selectAll(nil) }
        else if let press = pressPoint, frame.layer.bounds.contains(press) {
            // A click places the caret where it landed, as a native field does.
            let index = view.characterIndexForInsertion(at: CGPoint(x: press.x - textRect.minX, y: press.y - textRect.minY))
            view.setSelectedRange(NSRange(location: min(index, view.string.utf16.count), length: 0))
        } else { view.setSelectedRange(NSRange(location: view.string.utf16.count, length: 0)) }
        if options.secure { EnableSecureEventInput() }
        canvas.context.onLockParallax?()
        canvas.context.onInputStateChange?()
        onBegin?()
    }

    /// The caret / selection while editing (Archive restores it, item 14).
    var selectedRange: NSRange? { textView?.selectedRange() }

    func endEditing(commit: Bool) {
        guard !finishing, let view = textView else { return }
        finishing = true
        defer { finishing = false }
        view.unmarkText()
        if commit { text = bounded(view.string) } else { text = original }
        disposeEditor()
        frame.setState(invalid ? .invalid : .normal)
        renderIdle()
        if let host = canvas?.context.host, host.window?.firstResponder === view { host.window?.makeFirstResponder(host) }
        canvas?.context.onInputStateChange?()
        if commit { onCommit?(text) } else { onCancel?() }
    }

    private func disposeEditor() {
        if options.secure, textView != nil { DisableSecureEventInput() }
        textView?.delegate = nil; textView?.onKey = nil
        editor?.dispose()
        editor = nil; textView = nil
    }

    func refreshProjection() { editor?.refreshProjection() }

    func editorHitTest(host point: CGPoint) -> NSView? {
        guard let editor, let host = canvas?.context.host else { return nil }
        return editor.hitTest(editor.convert(point, from: host))
    }

    private func bounded(_ value: String) -> String {
        var result = options.multiline ? value : value.components(separatedBy: .newlines).joined(separator: " ")
        if let limit = options.maximumLength, result.count > limit { result = String(result.prefix(limit)) }
        return result
    }

    private func handleEditorKey(_ event: NSEvent) -> Bool {
        guard let view = textView, !view.hasMarkedText() else { return false }
        switch event.keyCode {
        case 53: endEditing(commit: options.escapeCommits); return true
        case 36, 76:
            let shift = event.modifierFlags.contains(.shift)
            if !options.multiline || (options.returnCommits && !shift) { endEditing(commit: true); onReturn?(); return true }
            return false
        case 48:
            guard !options.multiline else { return false }
            endEditing(commit: true); onTab?(event.modifierFlags.contains(.shift)); return true
        default: return false
        }
    }

    private func textDidChange(_ notification: Notification) {
        guard let view = textView, notification.object as? NSTextView === view else { return }
        if !view.hasMarkedText() {
            let limited = bounded(view.string)
            if limited != view.string {
                let selection = view.selectedRange()
                view.string = limited
                view.setSelectedRange(NSRange(location: min(selection.location, limited.utf16.count), length: 0))
            }
            text = limited
        }
        if invalid { setInvalid(false) }
        editor?.resizeDocument()
        onChange?(view.string)
    }

    private func textDidEndEditing(_ notification: Notification) {
        guard let view = textView, notification.object as? NSTextView === view, !finishing else { return }
        DispatchQueue.main.async { [weak self, weak view] in
            guard let self, let view, self.textView === view, view.window?.firstResponder !== view else { return }
            self.endEditing(commit: true)
        }
    }

    override func pointerDown(at point: CGPoint) -> Bool { isEnabled && !isEditing }
    override func pointerUp(at point: CGPoint) { pressPoint = point; super.pointerUp(at: point); pressPoint = nil }
    override var accessibilityRole: HUDAccessibilityItem.Role? { isEditing ? nil : .button }
    var idleTextForTesting: String { idle.text }
    var editorForTesting: HUDProjectedTextEditor? { editor }
}

/// The idle face of a field with `Options.attributes`: the text laid out by
/// TextKit exactly as its editor lays it out (zero padding, the field's
/// width), drawn once per change, top-aligned and clipped to the field.
private final class HUDStyledTextLayer: CALayer {
    var text = NSAttributedString() { didSet { if !text.isEqual(to: oldValue) { setNeedsDisplay() } } }

    override init() { super.init(); needsDisplayOnBoundsChange = true; isOpaque = false; masksToBounds = true }
    override init(layer: Any) { super.init(layer: layer) }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    override func action(forKey event: String) -> CAAction? { HUDLayer.action(forKey: event, of: self) }
    /// A drawn layer keeps its old bitmap when the backing scale changes; redraw sharp.
    override var contentsScale: CGFloat { didSet { if contentsScale != oldValue { setNeedsDisplay() } } }

    override func draw(in ctx: CGContext) {
        guard text.length > 0, bounds.width > 0 else { return }
        let storage = NSTextStorage(attributedString: text), manager = NSLayoutManager()
        let container = NSTextContainer(size: CGSize(width: bounds.width, height: .greatestFiniteMagnitude))
        container.lineFragmentPadding = 0
        manager.addTextContainer(container); storage.addLayoutManager(manager)
        ctx.saveGState()
        if ctx.ctm.d > 0 { ctx.translateBy(x: 0, y: bounds.height); ctx.scaleBy(x: 1, y: -1) }
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
        let glyphs = manager.glyphRange(for: container)
        manager.drawBackground(forGlyphRange: glyphs, at: .zero)
        manager.drawGlyphs(forGlyphRange: glyphs, at: .zero)
        NSGraphicsContext.restoreGraphicsState()
        ctx.restoreGState()
    }
}

/// NSTextView's delegate must be an NSObject; HUDControl is not.
private final class HUDDesignTextDelegate: NSObject, NSTextViewDelegate {
    var didChange: ((Notification) -> Void)?
    var didEnd: ((Notification) -> Void)?
    func textDidChange(_ notification: Notification) { didChange?(notification) }
    func textDidEndEditing(_ notification: Notification) { didEnd?(notification) }
}

/// The native view behind a projected field: routes Return / Esc / Tab to
/// the field and, in secure mode, draws bullets and refuses copying.
final class HUDDesignNativeTextView: HUDProjectedTextView, NSLayoutManagerDelegate {
    var onKey: ((NSEvent) -> Bool)?
    var secure = false {
        didSet {
            layoutManager?.delegate = secure ? self : nil
            setAccessibilitySubrole(secure ? .secureTextField : nil)
        }
    }

    override func keyDown(with event: NSEvent) {
        if onKey?(event) == true { return }
        super.keyDown(with: event)
    }

    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        if SummonShortcut.active.matches(event: event) { return false }
        return super.performKeyEquivalent(with: event)
    }

    override func copy(_ sender: Any?) { if !secure { super.copy(sender) } }
    override func cut(_ sender: Any?) { if !secure { super.cut(sender) } }

    func layoutManager(_ layoutManager: NSLayoutManager, shouldGenerateGlyphs glyphs: UnsafePointer<CGGlyph>,
                       properties props: UnsafePointer<NSLayoutManager.GlyphProperty>, characterIndexes charIndexes: UnsafePointer<Int>,
                       font aFont: NSFont, forGlyphRange glyphRange: NSRange) -> Int {
        guard secure else { return 0 }
        var bullet: CGGlyph = 0
        var character: UniChar = 0x2022
        guard CTFontGetGlyphsForCharacters(aFont as CTFont, &character, &bullet, 1) else { return 0 }
        let replaced = [CGGlyph](repeating: bullet, count: glyphRange.length)
        replaced.withUnsafeBufferPointer { buffer in
            layoutManager.setGlyphs(buffer.baseAddress!, properties: props, characterIndexes: charIndexes, font: aFont, forGlyphRange: glyphRange)
        }
        return glyphRange.length
    }
}
