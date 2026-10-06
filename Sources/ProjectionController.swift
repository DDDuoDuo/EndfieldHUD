import AppKit

/// The HUD owns the handoff: it finishes closing before calling `present`, and
/// reopens only after `dismiss` completes. This owner never changes HUD state.
final class ProjectionController {
    let model: ProjectionModel
    var onClose: (() -> Void)?
    var onEvent: ((ProjectionEvent) -> Void)?
    private(set) var isPresented = false
    private(set) var workspace: ProjectionWorkspaceView?
    private var panel: ProjectionPanel?
    private var backdrop: HUDBackgroundBlurView?
    private var configuration: AppConfiguration
    private let shelfChoices: () -> [NotesShelfMediaChoice]
    private let shelfAccess: (UUID) throws -> ShelfFileAccess
    private var presentationSerial = 0
    private var closing = false
    private var backdropEnabled: Bool?

    init(configuration: AppConfiguration, shelfChoices: @escaping () -> [NotesShelfMediaChoice],
         shelfAccess: @escaping (UUID) throws -> ShelfFileAccess) {
        self.configuration = configuration
        self.model = ProjectionModel(configuration: configuration)
        self.shelfChoices = shelfChoices; self.shelfAccess = shelfAccess
    }
    deinit {
        workspace?.dispose(); panel?.orderOut(nil)
    }
    func update(configuration: AppConfiguration) { self.configuration = configuration; workspace?.update(configuration: configuration) }
    func present(on screen: NSScreen) {
        guard !isPresented else { return }
        presentationSerial += 1; let serial = presentationSerial
        isPresented = true; closing = false
        let panel = ProjectionPanel(contentRect: screen.frame, styleMask: [.borderless], backing: .buffered, defer: false)
        panel.level = .screenSaver; panel.isOpaque = false; panel.backgroundColor = .clear
        panel.hasShadow = false; panel.isReleasedWhenClosed = false
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .ignoresCycle]
        panel.sharingType = .readOnly; panel.hidesOnDeactivate = false; panel.acceptsMouseMovedEvents = true
        let container = NSView(frame: CGRect(origin: .zero, size: screen.frame.size))
        container.wantsLayer = true; container.layer?.backgroundColor = NSColor.clear.cgColor
        let effect = HUDBackgroundBlurView(frame: container.bounds)
        effect.autoresizingMask = [.width, .height]; effect.material = .hudWindow
        effect.blendingMode = .behindWindow; effect.state = .active
        container.addSubview(effect)
        let view = ProjectionWorkspaceView(model: model, frame: container.bounds, reduceMotion: configuration.reduceMotion)
        view.updateScreenGeometry(screen)
        view.autoresizingMask = [.width, .height]; view.shelfChoices = shelfChoices; view.shelfAccess = shelfAccess
        view.onClose = { [weak self] in guard self?.closing == false else { return }; self?.onClose?() }
        view.onEvent = { [weak self] in self?.onEvent?($0) }
        view.onBackgroundChange = { [weak self] in self?.updateBackdrop() }
        container.addSubview(view); panel.contentView = container
        self.panel = panel; workspace = view; backdrop = effect
        updateBackdrop(); panel.alphaValue = configuration.reduceMotion ? 1 : 0
        NSApp.activate(ignoringOtherApps: true); panel.makeKeyAndOrderFront(nil); panel.makeFirstResponder(view)
        view.setActive(true)
        if !configuration.reduceMotion {
            NSAnimationContext.runAnimationGroup { context in context.duration = 0.18; panel.animator().alphaValue = 1 } completionHandler: { [weak self, weak panel] in
                guard let self, self.presentationSerial == serial, !self.closing else { return }
                panel?.alphaValue = 1
            }
        }
    }
    func reposition(on screen: NSScreen) {
        guard isPresented, !closing else { return }
        panel?.setFrame(screen.frame, display: true); workspace?.updateScreenGeometry(screen)
    }
    func dismiss(animated: Bool = true, completion: @escaping () -> Void = {}) {
        guard isPresented, let panel else { completion(); return }
        guard !closing else { return }
        closing = true; presentationSerial += 1
        let serial = presentationSerial
        workspace?.setActive(false)
        let finish = { [weak self] in
            guard let self, self.presentationSerial == serial else { return }
            self.releaseWindow(); completion()
        }
        if animated, !configuration.reduceMotion {
            NSAnimationContext.runAnimationGroup { context in context.duration = 0.16; panel.animator().alphaValue = 0 } completionHandler: { finish() }
        } else { finish() }
    }
    /// Shutdown/cancellation must not call the return-to-HUD callback.
    func forceClose() { presentationSerial += 1; releaseWindow() }
    private func releaseWindow() {
        workspace?.dispose(); workspace = nil
        backdrop?.state = .inactive; backdrop?.removeFromSuperview(); backdrop = nil
        backdropEnabled = nil
        panel?.orderOut(nil); panel?.contentView = nil; panel?.close(); panel = nil
        isPresented = false; closing = false
    }
    private func updateBackdrop() {
        guard let backdrop else { return }
        let enabled = model.backgroundEnabled && model.blur > 0
        // Public NSVisualEffectView has no blur-radius API. Opacity blends its
        // native live material with the desktop, exactly as the HUD's control.
        let changed = backdropEnabled != nil && backdropEnabled != enabled
        backdropEnabled = enabled
        if changed, !configuration.reduceMotion, !closing {
            backdrop.isHidden = false; backdrop.state = .active
            let serial = presentationSerial
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.18; backdrop.animator().alphaValue = enabled ? model.blur : 0
            } completionHandler: { [weak self, weak backdrop] in
                guard let self, self.presentationSerial == serial, self.backdropEnabled == enabled, let backdrop else { return }
                backdrop.isHidden = !enabled; backdrop.state = enabled ? .active : .inactive
            }
        } else {
            backdrop.isHidden = !enabled; backdrop.state = enabled ? .active : .inactive
            backdrop.alphaValue = enabled ? model.blur : 0
        }
    }
    var backdropEnabledForVerification: Bool { backdrop?.isHidden == false && backdrop?.state == .active }
}

private final class ProjectionPanel: NSPanel {
    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }
    override func sendEvent(_ event: NSEvent) {
        if event.type == .keyDown, event.keyCode == 53, attachedSheet == nil,
           event.modifierFlags.intersection([.command, .control, .option, .shift]).isEmpty {
            (contentView?.subviews.last as? ProjectionWorkspaceView)?.keyDown(with: event)
            return
        }
        super.sendEvent(event)
    }
    override func cancelOperation(_ sender: Any?) {
        guard let workspace = contentView?.subviews.last as? ProjectionWorkspaceView else { return }
        if workspace.secondaryMenu != nil { workspace.closeMenu() } else { workspace.onClose?() }
    }
}
