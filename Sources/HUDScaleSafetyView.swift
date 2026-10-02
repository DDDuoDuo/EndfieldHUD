import AppKit
import QuartzCore

/// The same confirmation card as Quit, kept at a safe screen size even while
/// previewing an extreme HUD scale or position. Only its pointer tilt changes.
final class HUDScaleSafetyView: NSView {
    private let controller: HUDSettingsController
    private var observation: UUID?
    private let confirmation: HUDQuitConfirmationView
    var onPointerMove: (() -> Void)? {
        didSet { confirmation.onPointerMove = onPointerMove }
    }
    override var isFlipped: Bool { true }
    init(controller: HUDSettingsController, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.controller = controller
        confirmation = HUDQuitConfirmationView(reduceMotion: reduceMotion)
        super.init(frame: .zero)
        wantsLayer = true
        layer?.zPosition = 3_000_000
        autoresizingMask = [.width, .height]
        addSubview(confirmation)
        confirmation.onConfirm = { [weak self] in self?.controller.confirmLayout() }
        confirmation.onCancel = { [weak self] in self?.controller.revertLayout() }
        isHidden = true
        observation = controller.addObserver { [weak self] in self?.refresh() }
        refresh()
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    deinit { if let observation { controller.removeObserver(observation) } }
    override func layout() {
        super.layout()
        confirmation.frame = bounds
    }
    private func refresh() {
        guard let remaining = controller.layoutConfirmationRemaining else {
            guard !isHidden else { return }
            confirmation.dismiss { [weak self] in
                guard let self, self.controller.layoutConfirmationRemaining == nil else { return }
                self.isHidden = true
            }
            return
        }
        isHidden = false
        let title: String, message: String
        if controller.isPositionPreviewPending {
            title = L10n.text("Keep this position?", "保留此位置？")
            message = L10n.text("Keep this position? Reverts in \(remaining)s", "保留此位置？\(remaining) 秒后自动恢复")
        } else {
            title = L10n.text("Keep this UI scale?", "保留此缩放？")
            message = L10n.text("Keep this UI scale? Reverts in \(remaining)s", "保留此缩放？\(remaining) 秒后自动恢复")
        }
        let dark = controller.configuration.theme == .dark || (controller.configuration.theme == .system
            && effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua)
        confirmation.configure(dark: dark, accent: controller.configuration.accentColor)
        confirmation.setContent(title: title, message: message, cancel: L10n.text("Revert ⎋", "恢复 ⎋"),
                                confirm: L10n.text("Keep ↵", "保留 ↵"), focusConfirm: true)
        if !confirmation.isPresented || confirmation.isDismissing { confirmation.show(); onPointerMove?() }
    }
    func handleKey(_ event: NSEvent) -> Bool {
        confirmation.handleKey(event)
    }
    override func hitTest(_ point: NSPoint) -> NSView? {
        guard confirmation.isPresented, !isHidden else { return nil }
        return confirmation.hitTest(convert(point, from: superview))
    }
    func setPointer(_ point: CGPoint, parallax: CGFloat, perspective: CGFloat) {
        confirmation.setPointer(point, parallax: parallax, perspective: perspective)
    }
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
