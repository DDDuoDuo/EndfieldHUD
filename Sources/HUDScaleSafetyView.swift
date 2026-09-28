import AppKit
import QuartzCore

/// A screen-aligned recovery control: scaling the HUD cannot move or shrink it.
final class HUDScaleSafetyView: NSView {
    private let controller: HUDSettingsController
    private var observation: UUID?
    private var visibilityGeneration = 0
    private let label = NSTextField(labelWithString: "")
    private let keep = NSButton(title: "", target: nil, action: nil)
    private let revert = NSButton(title: "", target: nil, action: nil)
    override var isFlipped: Bool { true }
    init(controller: HUDSettingsController) {
        self.controller = controller
        super.init(frame: .zero)
        wantsLayer = true
        layer?.zPosition = 3_000_000
        layer?.cornerRadius = 10
        layer?.backgroundColor = NSColor(white: 0.1, alpha: 0.98).cgColor
        layer?.borderColor = NSColor.systemYellow.withAlphaComponent(0.7).cgColor
        layer?.borderWidth = 1
        label.font = .systemFont(ofSize: 12, weight: .semibold)
        label.textColor = .white
        keep.bezelStyle = .rounded; revert.bezelStyle = .rounded
        keep.target = self; keep.action = #selector(confirm)
        revert.target = self; revert.action = #selector(cancel)
        addSubview(label); addSubview(keep); addSubview(revert)
        isHidden = true
        observation = controller.addObserver { [weak self] in self?.refresh() }
        refresh()
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    deinit { if let observation { controller.removeObserver(observation) } }
    override func layout() {
        super.layout()
        label.frame = CGRect(x: 14, y: 9, width: bounds.width - 28, height: 18)
        revert.frame = CGRect(x: bounds.width - 204, y: 35, width: 90, height: 27)
        keep.frame = CGRect(x: bounds.width - 110, y: 35, width: 96, height: 27)
    }
    private func refresh() {
        layer?.borderColor = controller.configuration.accentColor.withAlphaComponent(0.7).cgColor
        guard let remaining = controller.layoutConfirmationRemaining else {
            guard !isHidden else { return }
            visibilityGeneration += 1
            let generation = visibilityGeneration
            if HUDRuntimeAppearance.reduceMotion { isHidden = true; alphaValue = 1; return }
            NSAnimationContext.runAnimationGroup({ context in
                context.duration = 0.14
                self.animator().alphaValue = 0
            }, completionHandler: { [weak self] in
                guard let self, self.visibilityGeneration == generation else { return }
                self.isHidden = true
                self.alphaValue = 1
            })
            return
        }
        let wasHidden = isHidden || alphaValue < 1
        visibilityGeneration += 1
        isHidden = false
        alphaValue = 1
        if controller.isPositionPreviewPending {
            label.stringValue = L10n.text("Keep this position? Reverts in \(remaining)s", "保留此位置？\(remaining) 秒后自动恢复")
        } else {
            label.stringValue = L10n.text("Keep this UI scale? Reverts in \(remaining)s", "保留此缩放？\(remaining) 秒后自动恢复")
        }
        keep.title = L10n.text("Keep ↵", "保留 ↵")
        revert.title = L10n.text("Revert ⎋", "恢复 ⎋")
        if wasHidden && !HUDRuntimeAppearance.reduceMotion {
            let animation = CABasicAnimation(keyPath: "opacity")
            animation.fromValue = 0; animation.toValue = 1; animation.duration = 0.18
            layer?.add(animation, forKey: "settings.safety")
        }
    }
    func handleKey(_ event: NSEvent) -> Bool {
        guard controller.layoutConfirmationRemaining != nil else { return false }
        if event.keyCode == 53 { controller.revertLayout(); return true }
        if event.keyCode == 36 || event.keyCode == 76 { controller.confirmLayout(); return true }
        return false
    }
    override func hitTest(_ point: NSPoint) -> NSView? {
        controller.layoutConfirmationRemaining == nil ? nil : super.hitTest(point)
    }
    @objc private func confirm() { controller.confirmLayout() }
    @objc private func cancel() { controller.revertLayout() }
}

/// A passive native material behind the drawing; never intercepts HUD input.
final class HUDBackgroundBlurView: NSVisualEffectView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}

/// Separates the opening/closing envelope from the chosen material intensity.
/// Status updates can refresh the material without interrupting its fade.
final class HUDBackgroundContainerView: NSView {
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
