import AppKit
import UniformTypeIdentifiers

/// The native panels Settings opens (theme colour wheel, center logo import)
/// and the shortcut-capture bridge. Pointer, keyboard and accessibility
/// routing belong to the shared design canvas (HUDDesignCanvas).
final class HUDSettingsInteraction: NSObject {
    let canvas: HUDSettingsCanvas
    private weak var host: NSView?
    var onLock: (() -> Void)?
    private var active = false
    private var colorPanel: NSColorPanel?
    private var colorCloseObserver: NSObjectProtocol?
    private var logoPanel: NSOpenPanel?
    private var logoImportGeneration = 0
    private static let logoImportQueue = DispatchQueue(label: "EndfieldHUD.logo-import", qos: .utility)
    var isInputLocked: Bool { isPresentingPanel }
    var isPresentingPanel: Bool { colorPanel != nil || logoPanel != nil }
    var isCapturingShortcut: Bool { active && canvas.isCapturingShortcut }

    init(canvas: HUDSettingsCanvas, host: NSView) {
        self.canvas = canvas; self.host = host
        super.init()
        canvas.onChooseColor = { [weak self] in self?.chooseColor($0) }
        canvas.onChooseLogo = { [weak self] in self?.chooseLogo() }
        canvas.onLogoSelection = { [weak self] in self?.logoImportGeneration += 1 }
        canvas.onCaptureChanged = { [weak self] _ in self?.host?.window?.makeFirstResponder(self?.host) }
    }
    deinit {
        if let colorCloseObserver { NotificationCenter.default.removeObserver(colorCloseObserver) }
        if let colorPanel { colorPanel.setTarget(nil); colorPanel.setAction(nil) }
    }
    func setActive(_ value: Bool) {
        guard value != active else { return }
        if value { active = true } else { deactivate() }
    }
    func deactivate() {
        active = false; logoImportGeneration += 1
        logoPanel?.cancel(nil); logoPanel = nil
        dismissColorPanel()
    }
    /// The host's capture monitor: the chord is consumed before key equivalents.
    func keyDown(_ event: NSEvent) -> Bool {
        guard active, !isPresentingPanel, canvas.isCapturingShortcut else { return false }
        return canvas.capture(event)
    }

    private func chooseColor(_ initial: NSColor) {
        guard active, colorPanel == nil, let window = host?.window else { return }
        onLock?()
        let panel = NSColorPanel.shared
        colorPanel = panel
        panel.setTarget(nil); panel.setAction(nil)
        panel.color = initial; panel.showsAlpha = false; panel.mode = .wheel; panel.isContinuous = true
        panel.title = L10n.text("Theme color", "主题颜色")
        panel.level = NSWindow.Level(rawValue: max(NSWindow.Level.floating.rawValue, window.level.rawValue + 1))
        panel.setTarget(self); panel.setAction(#selector(colorChanged(_:)))
        colorCloseObserver = NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: panel, queue: .main) { [weak self] _ in
            guard let self else { return }
            self.clearColorPanelOwnership()
            if self.active { self.host?.window?.makeKeyAndOrderFront(nil); self.host?.window?.makeFirstResponder(self.host) }
        }
        panel.makeKeyAndOrderFront(nil)
    }
    private func chooseLogo() {
        guard active, !isPresentingPanel, let window = host?.window else { return }
        onLock?()
        let panel = NSOpenPanel(); logoPanel = panel
        if #available(macOS 11.0, *) { panel.allowedContentTypes = [.image] }
        else { panel.allowedFileTypes = ["public.image"] }
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false; panel.canChooseFiles = true
        panel.title = L10n.text("Center logo", "中心标志")
        panel.level = NSWindow.Level(rawValue: window.level.rawValue + 1)
        logoImportGeneration += 1
        let generation = logoImportGeneration, previous = canvas.customLogoRevision
        let selection = canvas.selectedLogo
        panel.beginSheetModal(for: window) { [weak self, weak panel] response in
            guard let self, self.logoPanel === panel else { return }
            let url = response == .OK ? panel?.url : nil
            self.logoPanel = nil
            self.host?.window?.makeFirstResponder(self.host)
            guard let url, self.active else { return }
            Self.logoImportQueue.async { [weak self] in
                let result = Result { try HUDCenterLogoStore.shared.importImage(from: url, replacing: previous) }
                DispatchQueue.main.async { [weak self] in
                    guard let self, self.active, self.logoImportGeneration == generation,
                          self.canvas.selectedLogo == selection, self.canvas.customLogoRevision == previous else { return }
                    switch result {
                    case .success(let revision): self.canvas.setCustomLogo(revision: revision)
                    case .failure(let error): self.canvas.showImportError(error.localizedDescription)
                    }
                }
            }
        }
    }
    @objc private func colorChanged(_ sender: NSColorPanel) {
        guard active, sender === colorPanel else { return }
        canvas.setCustomColor(sender.color)
    }
    private func dismissColorPanel() {
        guard let panel = colorPanel else { return }
        clearColorPanelOwnership(); panel.orderOut(nil)
    }
    private func clearColorPanelOwnership() {
        colorPanel?.setTarget(nil); colorPanel?.setAction(nil); colorPanel = nil
        if let colorCloseObserver { NotificationCenter.default.removeObserver(colorCloseObserver) }
        colorCloseObserver = nil
    }
}
