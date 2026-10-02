import AppKit
import QuartzCore

/// Shared confirmation surface. Its readable size and screen center are kept
/// independent of HUD layout previews, while the card follows pointer tilt.
final class HUDQuitConfirmationView: NSView {
    var onCancel: (() -> Void)?
    var onConfirm: (() -> Void)?
    var onPointerMove: (() -> Void)?
    private(set) var isPresented = false
    private let shouldReduceMotion: () -> Bool
    private let card = HUDQuitCardView()
    private let cardPlate = CAShapeLayer()
    private let headerLine = CALayer()
    private let titleLabel = NSTextField(labelWithString: "")
    private let messageLabel = NSTextField(wrappingLabelWithString: "")
    private let captionLabel = NSTextField(labelWithString: "ENDFIELDHUD / SYSTEM")
    private let cancelButton = HUDQuitActionButton(title: "", target: nil, action: nil)
    private let quitButton = HUDQuitActionButton(title: "", target: nil, action: nil)
    private let cancelPlate = CAShapeLayer()
    private let quitPlate = CAShapeLayer()
    private var generation = 0
    private var finishing = false
    var isDismissing: Bool { finishing }
    private var submitted = false
    private var dark = true
    private var accent = NSColor.systemYellow
    private var pointerTracking: NSTrackingArea?
    private weak var priorResponder: NSResponder?
    private var customContent: (title: String, message: String, cancel: String, confirm: String)?
    private var initiallyFocusConfirm = false
    private var pressedAction: Int?
    override var isFlipped: Bool { true }

    init(frame: NSRect = .zero, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        shouldReduceMotion = reduceMotion
        super.init(frame: frame)
        wantsLayer = true
        layer?.zPosition = 3_100_000
        layer?.backgroundColor = NSColor.black.withAlphaComponent(0.30).cgColor
        autoresizingMask = [.width, .height]
        card.wantsLayer = true
        card.layer?.addSublayer(cardPlate)
        card.layer?.addSublayer(headerLine)
        card.layer?.addSublayer(cancelPlate)
        card.layer?.addSublayer(quitPlate)
        addSubview(card)
        titleLabel.font = .systemFont(ofSize: 18, weight: .bold)
        messageLabel.font = .systemFont(ofSize: 12, weight: .medium)
        messageLabel.maximumNumberOfLines = 3
        captionLabel.font = .monospacedSystemFont(ofSize: 8, weight: .semibold)
        for label in [titleLabel, messageLabel, captionLabel] {
            label.isSelectable = false
            label.setAccessibilityElement(false)
            card.addSubview(label)
        }
        for button in [cancelButton, quitButton] {
            button.isBordered = false
            button.bezelStyle = .regularSquare
            button.focusRingType = .none
            button.font = .systemFont(ofSize: 12, weight: .semibold)
            button.target = self
            button.onFeedback = { [weak self] in self?.updateButtonAppearance(animated: true) }
            button.onPointerMove = { [weak self] in self?.onPointerMove?() }
            button.onKey = { [weak self] event in self?.handleKey(event) ?? false }
            button.projectedFrame = { [weak self, weak button] in
                guard let self, let button, let window = self.window else { return .zero }
                let corners = [CGPoint(x: button.frame.minX, y: button.frame.minY), CGPoint(x: button.frame.maxX, y: button.frame.minY),
                               CGPoint(x: button.frame.maxX, y: button.frame.maxY), CGPoint(x: button.frame.minX, y: button.frame.maxY)]
                    .map { self.convert(self.projectCardPoint($0), to: nil) }
                let x = corners.map(\.x), y = corners.map(\.y)
                return window.convertToScreen(CGRect(x: x.min()!, y: y.min()!, width: x.max()! - x.min()!, height: y.max()! - y.min()!))
            }
            card.addSubview(button)
        }
        cancelButton.action = #selector(cancel)
        quitButton.action = #selector(confirm)
        cancelButton.nextKeyView = quitButton
        quitButton.nextKeyView = cancelButton
        setAccessibilityElement(true)
        setAccessibilityRole(.group)
        isHidden = true
        configure(dark: true, accent: accent)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    func configure(dark: Bool, accent: NSColor) {
        self.dark = dark; self.accent = accent
        titleLabel.stringValue = customContent?.title ?? L10n.text("Quit EndfieldHUD?", "退出 EndfieldHUD？")
        messageLabel.stringValue = customContent?.message ?? L10n.text("The application will quit after the HUD closes.", "界面收起后，将退出应用。")
        cancelButton.title = customContent?.cancel ?? L10n.text("Cancel", "取消")
        quitButton.title = customContent?.confirm ?? L10n.text("Quit", "退出")
        cancelButton.setAccessibilityLabel(customContent?.cancel ?? L10n.text("Cancel quitting EndfieldHUD", "取消退出 EndfieldHUD"))
        quitButton.setAccessibilityLabel(customContent?.confirm ?? L10n.text("Quit EndfieldHUD application", "退出 EndfieldHUD 应用"))
        setAccessibilityLabel(titleLabel.stringValue)
        setAccessibilityHelp(messageLabel.stringValue)
        let primary = NSColor(white: dark ? 0.95 : 0.10, alpha: 1)
        titleLabel.textColor = primary
        messageLabel.textColor = NSColor(white: dark ? 0.73 : 0.32, alpha: 1)
        captionLabel.textColor = NSColor(white: dark ? 0.53 : 0.44, alpha: 1)
        for button in [cancelButton, quitButton] {
            button.attributedTitle = NSAttributedString(string: button.title,
                attributes: [.font: NSFont.systemFont(ofSize: 12, weight: .semibold), .foregroundColor: primary])
        }
        withoutActions {
            cardPlate.fillColor = NSColor(white: dark ? 0.095 : 0.90, alpha: 0.98).cgColor
            cardPlate.strokeColor = accent.withAlphaComponent(0.58).cgColor
            cardPlate.lineWidth = 0.8
            headerLine.backgroundColor = accent.withAlphaComponent(0.82).cgColor
            layer?.backgroundColor = NSColor.black.withAlphaComponent(dark ? 0.30 : 0.22).cgColor
        }
        updateButtonAppearance(animated: false)
        needsLayout = true
    }

    func setContent(title: String, message: String, cancel: String, confirm: String, focusConfirm: Bool) {
        customContent = (title, message, cancel, confirm)
        initiallyFocusConfirm = focusConfirm
        configure(dark: dark, accent: accent)
    }

    /// No additional display timer: the HUD's pointer stream supplies updates.
    func setPointer(_ point: CGPoint, parallax: CGFloat = 1, perspective: CGFloat = 1) {
        guard isPresented else { return }
        let target = HUDMotionMath.transform(normalizedPoint: point, depth: 0, travel: 2,
            reducedMotion: shouldReduceMotion(), parallaxIntensity: parallax * 1.25,
            perspectiveIntensity: perspective,
            projectionBounds: CGRect(x: -210, y: -110, width: 420, height: 220))
        guard let plate = card.layer else { return }
        if shouldReduceMotion() { plate.removeAnimation(forKey: "confirmation.pointer") }
        guard !CATransform3DEqualToTransform(plate.transform, target) else { return }
        let previous = plate.presentation()?.transform ?? plate.transform
        withoutActions { plate.transform = target }
        if !shouldReduceMotion() {
            let change = CABasicAnimation(keyPath: "transform")
            change.fromValue = NSValue(caTransform3D: previous); change.toValue = NSValue(caTransform3D: target)
            change.duration = 0.10; change.timingFunction = CAMediaTimingFunction(name: .easeOut)
            plate.add(change, forKey: "confirmation.pointer")
        } else { plate.removeAnimation(forKey: "confirmation.pointer") }
    }

    override func layout() {
        super.layout()
        let width = min(390, max(220, bounds.width - 40))
        let height: CGFloat = 190
        card.frame = CGRect(x: (bounds.width - width) / 2, y: (bounds.height - height) / 2, width: width, height: height)
        let titleInset: CGFloat = 23
        titleLabel.frame = CGRect(x: titleInset, y: 42, width: width - titleInset * 2, height: 28)
        messageLabel.frame = CGRect(x: titleInset, y: 78, width: width - titleInset * 2, height: 42)
        captionLabel.frame = CGRect(x: titleInset, y: 18, width: width - titleInset * 2, height: 13)
        let gap: CGFloat = 12, buttonWidth = (width - 46 - gap) / 2
        cancelButton.frame = CGRect(x: 23, y: 137, width: buttonWidth, height: 32)
        quitButton.frame = CGRect(x: 23 + buttonWidth + gap, y: 137, width: buttonWidth, height: 32)
        withoutActions {
            // AppKit's backing layer starts with a top-left anchor. Keep the
            // readable card centered while rotating it about its own center.
            card.layer?.anchorPoint = CGPoint(x: 0.5, y: 0.5)
            card.layer?.position = CGPoint(x: card.frame.midX, y: card.frame.midY)
            cardPlate.frame = card.bounds
            cardPlate.path = Self.cutCorner(card.bounds.insetBy(dx: 0.5, dy: 0.5), corner: 10)
            headerLine.frame = CGRect(x: 23, y: 0, width: 62, height: 2)
            cancelPlate.path = Self.cutCorner(cancelButton.frame.insetBy(dx: -2, dy: -2), corner: 4)
            quitPlate.path = Self.cutCorner(quitButton.frame.insetBy(dx: -2, dy: -2), corner: 4)
        }
    }

    func show() {
        generation += 1
        finishing = false; submitted = false
        let wasVisible = isPresented && !isHidden
        if !wasVisible { priorResponder = window?.firstResponder }
        isPresented = true; isHidden = false; alphaValue = 1
        cancelButton.isEnabled = true; quitButton.isEnabled = true
        removeTransitionAnimations()
        if shouldReduceMotion() {
            card.layer?.removeAnimation(forKey: "confirmation.pointer")
            withoutActions { card.layer?.transform = CATransform3DIdentity }
        }
        configure(dark: dark, accent: accent)
        layoutSubtreeIfNeeded()
        pressedAction = nil
        window?.makeFirstResponder(initiallyFocusConfirm ? quitButton : cancelButton)
        updateButtonAppearance(animated: false)
        if !wasVisible && !shouldReduceMotion() {
            animate(layer, keyPath: "opacity", from: 0, to: 1, duration: 0.16, key: "quit.reveal")
            animate(card.layer, keyPath: "transform.translation.y", from: 12, to: 0, duration: 0.20, key: "quit.depth")
        }
        NSAccessibility.post(element: self, notification: .layoutChanged)
    }

    /// isPresented deliberately remains true until the fade finishes. Input
    /// cannot reach the underlying HUD between clicking Cancel and its reveal.
    func dismiss(animated: Bool = true, completion: (() -> Void)? = nil) {
        guard isPresented else { completion?(); return }
        generation += 1
        let requestedGeneration = generation
        finishing = true; submitted = true
        cancelButton.isEnabled = false; quitButton.isEnabled = false
        removeTransitionAnimations()
        let finish = { [weak self] in
            guard let self, self.generation == requestedGeneration else { return }
            self.isHidden = true; self.alphaValue = 1; self.isPresented = false
            self.finishing = false
            self.removeTransitionAnimations()
            self.card.layer?.removeAnimation(forKey: "confirmation.pointer")
            self.withoutActions { self.card.layer?.transform = CATransform3DIdentity }
            if let previous = self.priorResponder, self.window?.firstResponder === self.cancelButton || self.window?.firstResponder === self.quitButton {
                self.window?.makeFirstResponder(previous)
            }
            self.priorResponder = nil
            completion?()
        }
        guard animated, !shouldReduceMotion(), window != nil else { finish(); return }
        animate(layer, keyPath: "opacity", from: 1, to: 0, duration: 0.14, key: "quit.reveal", hold: true)
        animate(card.layer, keyPath: "transform.translation.y", from: 0, to: 8, duration: 0.14, key: "quit.depth", hold: true)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.14, execute: finish)
    }

    /// The owner calls this before routing HUD shortcuts. Unhandled keys remain
    /// consumed while modal, including when the confirmation is retracting.
    @discardableResult func handleKey(_ event: NSEvent) -> Bool {
        guard isPresented else { return false }
        guard !finishing, !submitted, !event.isARepeat else { return true }
        switch event.keyCode {
        case 53: cancel()
        case 48:
            let next = window?.firstResponder === cancelButton ? quitButton : cancelButton
            window?.makeFirstResponder(next); updateButtonAppearance(animated: true)
        case 123: window?.makeFirstResponder(cancelButton); updateButtonAppearance(animated: true)
        case 124: window?.makeFirstResponder(quitButton); updateButtonAppearance(animated: true)
        case 36, 76, 49:
            // Cancel is initially focused. Quitting via keyboard requires first
            // deliberately moving focus to the Quit button.
            if window?.firstResponder === quitButton { confirm() } else { cancel() }
        default: break
        }
        return true
    }

    @objc func cancel() {
        guard isPresented, !finishing, !submitted else { return }
        submitted = true; onCancel?()
    }
    @objc func confirm() {
        guard isPresented, !finishing, !submitted else { return }
        submitted = true; onConfirm?()
    }

    override func keyDown(with event: NSEvent) { if !handleKey(event) { super.keyDown(with: event) } }
    override func cancelOperation(_ sender: Any?) { cancel() }
    override func hitTest(_ point: NSPoint) -> NSView? {
        guard isPresented, !isHidden else { return nil }
        return self // Route through the displayed projective card, not AppKit's flat frames.
    }
    override func mouseDown(with event: NSEvent) {
        onPointerMove?()
        guard !finishing, !submitted else { return }
        pressedAction = action(at: convert(event.locationInWindow, from: nil))
        if let action = pressedAction {
            let button = action == 0 ? cancelButton : quitButton
            window?.makeFirstResponder(button); button.highlight(true)
        }
    }
    override func mouseUp(with event: NSEvent) {
        let selected = action(at: convert(event.locationInWindow, from: nil))
        let pressed = pressedAction; pressedAction = nil
        cancelButton.highlight(false); quitButton.highlight(false)
        guard let pressed, pressed == selected else { return }
        if pressed == 0 { cancel() } else { confirm() }
    }
    override func rightMouseDown(with event: NSEvent) { onPointerMove?() }
    override func otherMouseDown(with event: NSEvent) { onPointerMove?() }
    override func scrollWheel(with event: NSEvent) {}
    override func mouseMoved(with event: NSEvent) {
        onPointerMove?()
        let index = action(at: convert(event.locationInWindow, from: nil))
        cancelButton.setHovered(index == 0); quitButton.setHovered(index == 1)
    }
    override func mouseEntered(with event: NSEvent) { onPointerMove?() }
    override func mouseDragged(with event: NSEvent) { onPointerMove?() }

    private func projectCardPoint(_ point: CGPoint) -> CGPoint {
        guard let plate = card.layer?.presentation() ?? card.layer else { return card.convert(point, to: self) }
        // Core Animation includes the real anchor, position, flipped backing
        // coordinates and presentation transform, including a reveal in flight.
        return plate.convert(point, to: plate.superlayer)
    }

    private func action(at point: CGPoint) -> Int? {
        guard let plate = card.layer?.presentation() ?? card.layer else { return nil }
        let local = plate.convert(point, from: plate.superlayer)
        guard local.x.isFinite, local.y.isFinite else { return nil }
        if cancelButton.frame.contains(local) { return 0 }
        if quitButton.frame.contains(local) { return 1 }
        return nil
    }
    func actionPointForVerification(confirm: Bool) -> CGPoint {
        let frame = confirm ? quitButton.frame : cancelButton.frame
        return projectCardPoint(CGPoint(x: frame.midX, y: frame.midY))
    }
    var cardTransformForVerification: CATransform3D { card.layer?.transform ?? CATransform3DIdentity }
    override func updateTrackingAreas() {
        if let pointerTracking { removeTrackingArea(pointerTracking) }
        let area = NSTrackingArea(rect: .zero, options: [.activeAlways, .inVisibleRect, .mouseMoved, .mouseEnteredAndExited], owner: self, userInfo: nil)
        addTrackingArea(area); pointerTracking = area
        super.updateTrackingAreas()
    }

    private func updateButtonAppearance(animated: Bool) {
        CATransaction.begin()
        CATransaction.setDisableActions(!animated || shouldReduceMotion() || !isPresented)
        CATransaction.setAnimationDuration(0.12)
        for (button, plate) in [(cancelButton, cancelPlate), (quitButton, quitPlate)] {
            let engaged = button.hovered || button.isHighlighted
            let focused = window?.firstResponder === button
            plate.fillColor = (engaged ? accent.withAlphaComponent(0.20) : NSColor(white: dark ? 0.9 : 0.1, alpha: 0.065)).cgColor
            plate.strokeColor = (focused || engaged ? accent.withAlphaComponent(0.90) : NSColor(white: dark ? 0.7 : 0.3, alpha: 0.44)).cgColor
            plate.lineWidth = focused ? 1.25 : 0.7
        }
        CATransaction.commit()
    }

    private func removeTransitionAnimations() {
        layer?.removeAnimation(forKey: "quit.reveal")
        card.layer?.removeAnimation(forKey: "quit.depth")
    }
    private func animate(_ layer: CALayer?, keyPath: String, from: CGFloat, to: CGFloat, duration: TimeInterval, key: String, hold: Bool = false) {
        let animation = CABasicAnimation(keyPath: keyPath)
        animation.fromValue = from; animation.toValue = to; animation.duration = duration
        animation.timingFunction = CAMediaTimingFunction(controlPoints: 0.2, 0.7, 0.3, 1)
        if hold { animation.isRemovedOnCompletion = false; animation.fillMode = .forwards }
        layer?.add(animation, forKey: key)
    }
    private static func cutCorner(_ rect: CGRect, corner: CGFloat) -> CGPath {
        let path = CGMutablePath()
        path.move(to: CGPoint(x: rect.minX + corner, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - corner))
        path.addLine(to: CGPoint(x: rect.maxX - corner, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.minY + corner))
        path.closeSubpath(); return path
    }
    private func withoutActions(_ body: () -> Void) {
        CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit()
    }
}

private final class HUDQuitCardView: NSView {
    override var isFlipped: Bool { true }
}

private final class HUDQuitActionButton: NSButton {
    var projectedFrame: (() -> NSRect)?
    var onFeedback: (() -> Void)?
    var onKey: ((NSEvent) -> Bool)?
    var onPointerMove: (() -> Void)?
    private(set) var hovered = false
    private var pointerTracking: NSTrackingArea?
    override var acceptsFirstResponder: Bool { true }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
    func setHovered(_ value: Bool) { guard value != hovered else { return }; hovered = value; onFeedback?() }
    override func becomeFirstResponder() -> Bool { let result = super.becomeFirstResponder(); onFeedback?(); return result }
    override func resignFirstResponder() -> Bool { let result = super.resignFirstResponder(); onFeedback?(); return result }
    override func keyDown(with event: NSEvent) { if onKey?(event) != true { super.keyDown(with: event) } }
    override func highlight(_ flag: Bool) { super.highlight(flag); onFeedback?() }
    override func mouseEntered(with event: NSEvent) { hovered = true; onFeedback?(); onPointerMove?() }
    override func mouseExited(with event: NSEvent) { hovered = false; onFeedback?(); onPointerMove?() }
    override func mouseMoved(with event: NSEvent) { onPointerMove?() }
    override func updateTrackingAreas() {
        if let pointerTracking { removeTrackingArea(pointerTracking) }
        let area = NSTrackingArea(rect: .zero, options: [.activeAlways, .inVisibleRect, .mouseEnteredAndExited, .mouseMoved], owner: self, userInfo: nil)
        addTrackingArea(area); pointerTracking = area
        super.updateTrackingAreas()
    }
}
