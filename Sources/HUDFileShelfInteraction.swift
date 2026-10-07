import AppKit
import Quartz
import Darwin

/// AppKit supplies only the file chooser, Quick Look, drag session and native
/// accessibility actions. The shelf itself remains in the projected HUD layer.
final class HUDFileShelfInteraction: NSObject, NSDraggingSource, QLPreviewPanelDataSource, QLPreviewPanelDelegate {
    private let canvas: FileShelfCanvas
    private let store: FileShelfStore?
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var onLock: (() -> Void)?
    var onDragSessionBegan: (() -> Void)?
    /// Release HUD pointer ownership before entering AppKit's nested drag loop.
    var onDragSessionWillBegin: (() -> Void)?
    /// Called with true only after a destination accepted the native copy. The
    /// controller owns closing/reopening animations and restores focus on cancel.
    var onDragSessionEnded: ((Bool) -> Void)?
    /// Transfers the access lease to the HUD owner, which keeps it alive until
    /// the closing animation finishes and Finder receives the file URL.
    var onRevealRequested: ((ShelfFileAccess) -> Void)?

    private var active = false
    private var externalDrag = false
    private var dragCandidate: (id: UUID, point: CGPoint)?
    private var dragLeases: [ShelfFileAccess] = []
    private var dragSession: NSDraggingSession?
    // NSDraggingSource can outlive the current HUD. Hold both source and access
    // until AppKit ends the session, even if the parent closes/deactivates first.
    private var dragRetainer: HUDFileShelfInteraction?
    private(set) var isDraggingOut = false
    private var chooser: NSOpenPanel?
    private var dialogGeneration = 0
    private var buttons: [String: HUDShelfActionButton] = [:]

    private var previewRequested = false
    private var closingPreview = false
    private var previewID: UUID?
    private var previewLease: ShelfFileAccess?
    private var previewItem: HUDShelfPreviewItem?
    private weak var requestedPreviewPanel: QLPreviewPanel?
    private weak var controlledPreviewPanel: QLPreviewPanel?
    private var previousPreviewLevel: NSWindow.Level?
    private var previewGeneration = 0

    var isPresentingPanel: Bool { chooser != nil || previewRequested || closingPreview || controlledPreviewPanel?.isVisible == true }
    var isInputLocked: Bool { dragCandidate != nil || externalDrag || isDraggingOut || isPresentingPanel }

    init(canvas: FileShelfCanvas, store: FileShelfStore?, host: NSView) {
        self.canvas = canvas; self.store = store; self.host = host
        super.init()
        canvas.onChooseFiles = { [weak self] in self?.chooseFiles() }
        canvas.onPreview = { [weak self] in self?.preview(id: $0) }
        canvas.onReveal = { [weak self] in self?.reveal(id: $0) }
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
    }

    func setActive(_ value: Bool) {
        guard active != value else { return }
        if value { active = true; canvas.activate(); layoutAccessibility() }
        else { deactivate() }
    }

    func deactivate() {
        active = false; dragCandidate = nil; externalDrag = false
        dialogGeneration += 1
        let panel = chooser
        chooser = nil
        panel?.cancel(nil)
        closePreview(restoreFocus: false)
        canvas.deactivate()
        buttons.values.forEach { $0.isHidden = true }
        // In particular, do not release dragLeases or dragRetainer here. The
        // destination may still need the URL after the HUD has closed.
    }

    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, !isDraggingOut, CGRect(x: 0, y: 0, width: 400, height: 334).contains(point) else { return false }
        onLock?()
        let candidate = event.clickCount == 1 ? canvas.itemAt(point: point) : nil
        dragCandidate = candidate.map { ($0, point) }
        _ = canvas.mouseDown(at: point, clickCount: event.clickCount, modifiers: event.modifierFlags)
        if !isPresentingPanel { host?.window?.makeFirstResponder(host) }
        return true
    }

    func mouseDragged(to point: CGPoint, event: NSEvent) {
        guard active, !isDraggingOut, let candidate = dragCandidate else { return }
        let distance = hypot(point.x - candidate.point.x, point.y - candidate.point.y)
        guard distance >= 5 else { return }
        dragCandidate = nil
        beginOutgoingDrag(id: candidate.id, event: event)
    }

    func mouseUp() { dragCandidate = nil; canvas.finishPointerSelection() }

    func keyDown(_ event: NSEvent) -> Bool {
        guard active, !isDraggingOut, chooser == nil else { return false }
        let modifiers = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if modifiers.isEmpty || modifiers == [.shift] {
            let movement: Int?
            switch event.keyCode {
            case 123: movement = -1
            case 124: movement = 1
            case 125: movement = 2
            case 126: movement = -2
            default: movement = nil
            }
            if let movement {
                dragCandidate = nil; onLock?()
                canvas.selectNext(movement, extending: modifiers.contains(.shift)); return true
            }
        }
        if modifiers.isEmpty {
            let distance: CGFloat?
            switch event.keyCode {
            case 121: distance = FileShelfCanvas.contentRect.height - 40
            case 116: distance = -FileShelfCanvas.contentRect.height + 40
            case 115: distance = -CGFloat.greatestFiniteMagnitude
            case 119: distance = CGFloat.greatestFiniteMagnitude
            default: distance = nil
            }
            if let distance {
                dragCandidate = nil; onLock?(); canvas.scrollBy(distance); return true
            }
        }
        if modifiers.isEmpty && event.keyCode == 49 { canvas.previewSelection(); return true }
        if modifiers.isEmpty && (event.keyCode == 51 || event.keyCode == 117) {
            closePreview(restoreFocus: false)
            canvas.deleteSelection(); return true
        }
        if modifiers == [.command] {
            switch event.charactersIgnoringModifiers?.lowercased() {
            case "r": canvas.revealSelection(); return true
            case "v": return importPasteboard(.general)
            default: break
            }
        }
        return false
    }

    func beginExternalDrag() {
        guard active, !externalDrag, !isDraggingOut else { return }
        dragCandidate = nil
        onLock?(); externalDrag = true
        canvas.setDropTarget(true)
    }

    func endExternalDrag() { externalDrag = false; canvas.setDropTarget(false) }

    static func acceptsFiles(_ pasteboard: NSPasteboard) -> Bool {
        pasteboard.canReadObject(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true])
    }

    @discardableResult func importPasteboard(_ pasteboard: NSPasteboard) -> Bool {
        guard active, !isDraggingOut else { return false }
        let urls = pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        guard !urls.isEmpty else { return false }
        onLock?()
        return canvas.importURLs(urls)
    }

    private func chooseFiles() {
        guard active, !isDraggingOut, chooser == nil, let window = host?.window else { return }
        dragCandidate = nil
        closePreview(restoreFocus: false)
        onLock?()
        let panel = NSOpenPanel()
        panel.title = L10n.text("Add to Temporary File Shelf", "添加到文件暂存架")
        panel.prompt = L10n.text("Add references", "添加引用")
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        panel.allowedFileTypes = nil
        panel.allowsOtherFileTypes = true
        panel.resolvesAliases = false
        panel.level = NSWindow.Level(rawValue: max(panel.level.rawValue, window.level.rawValue + 1))
        dialogGeneration += 1
        let token = dialogGeneration
        chooser = panel
        panel.beginSheetModal(for: window) { [weak self, weak panel] response in
            guard let self, self.dialogGeneration == token else { return }
            self.chooser = nil
            guard self.active, self.host?.window != nil else { return }
            if response == .OK, let urls = panel?.urls { _ = self.canvas.importURLs(urls) }
            self.restoreHUDFocus()
        }
    }

    private func beginOutgoingDrag(id: UUID, event: NSEvent) {
        guard active, let host, host.window != nil, let store else { return }
        dragDiagnostic("begin")
        closePreview(restoreFocus: false)
        var accesses: [ShelfFileAccess] = []
        do {
            let ids = canvas.dragSelection(primaryID: id)
            // Resolve every reference before starting. A failure never silently
            // exports only part of the user's selection, and acquired scopes are
            // balanced by the catch branch below.
            for itemID in ids { accesses.append(try store.access(id: itemID)) }
            guard !accesses.isEmpty else { return }
            let point = host.convert(event.locationInWindow, from: nil)
            let projected = canvas.cardRect(for: id).map { project?($0) ?? $0 }
            let side = min(72, max(36, (projected?.height ?? 52) * 0.7))
            let drags = zip(ids, accesses).enumerated().map { index, entry -> NSDraggingItem in
                let (itemID, access) = entry
                let drag = NSDraggingItem(pasteboardWriter: access.url as NSURL)
                let image = canvas.icon(for: itemID) ?? NSWorkspace.shared.icon(forFile: access.url.path)
                let offset = CGFloat(min(index, 4)) * 5
                drag.setDraggingFrame(CGRect(x: point.x - side / 2 + offset, y: point.y - side / 2 + offset,
                                            width: side, height: side), contents: image)
                return drag
            }
            onLock?()
            canvas.beginSelectionDrag()
            dragLeases = accesses
            isDraggingOut = true
            dragRetainer = self
            onDragSessionWillBegin?()
            // willBegin requests the closing animation only after AppKit has
            // acquired the real source window and started its drag tracking.
            dragSession = host.beginDraggingSession(with: drags, event: event, source: self)
            dragSession?.animatesToStartingPositionsOnCancelOrFail = true
            dragSession?.draggingFormation = .pile
        } catch {
            accesses.forEach { $0.close() }
            canvas.showError(error.localizedDescription)
        }
    }

    func draggingSession(_ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation {
        .copy
    }

    func ignoreModifierKeys(for session: NSDraggingSession) -> Bool { true }

    func draggingSession(_ session: NSDraggingSession, willBeginAt screenPoint: NSPoint) {
        dragDiagnostic("willBegin")
        onDragSessionBegan?()
    }

    func draggingSession(_ session: NSDraggingSession, endedAt screenPoint: NSPoint, operation: NSDragOperation) {
        dragDiagnostic("ended operation=\(operation.rawValue)")
        // Finder and other receivers have finished consuming the file URL when
        // AppKit sends this callback. The original file was never moved by us.
        let succeeded = operation.contains(.copy)
        if let onDragSessionEnded { onDragSessionEnded(succeeded) }
        dragSession = nil
        dragLeases.forEach { $0.close() }; dragLeases.removeAll()
        isDraggingOut = false
        if !succeeded, onDragSessionEnded == nil, active {
            NSApp.activate(ignoringOtherApps: true)
            restoreHUDFocus()
        }
        dragRetainer = nil
    }

    private func dragDiagnostic(_ message: String) {
        guard CommandLine.arguments.contains("--ui-test") else { return }
        print("FileShelf drag \(message)")
        fflush(stdout)
    }

    private func reveal(id: UUID) {
        guard active, !isDraggingOut, let store else { return }
        dragCandidate = nil
        closePreview(restoreFocus: false)
        do {
            let access = try store.access(id: id)
            if let onRevealRequested { onRevealRequested(access) }
            else {
                defer { access.close() }
                NSWorkspace.shared.activateFileViewerSelecting([access.url])
            }
        } catch { canvas.showError(error.localizedDescription) }
    }

    private func preview(id: UUID) {
        guard active, !isDraggingOut, chooser == nil, let store, let host, let window = host.window else { return }
        dragCandidate = nil
        if previewRequested, previewID == id {
            closePreview(restoreFocus: true)
            return
        }
        closePreview(restoreFocus: false)
        do {
            let access = try store.access(id: id)
            guard let panel = QLPreviewPanel.shared() else { access.close(); return }
            previewLease = access
            previewID = id
            previewItem = HUDShelfPreviewItem(url: access.url,
                title: store.items.first(where: { $0.id == id })?.name ?? access.url.lastPathComponent)
            previewRequested = true
            requestedPreviewPanel = panel
            previewGeneration += 1
            let token = previewGeneration
            onLock?()
            window.makeKeyAndOrderFront(nil)
            window.makeFirstResponder(host)
            // Quick Look may not establish its responder controller until the
            // shared panel is ordered on screen. Content/delegate/level changes
            // remain exclusively in the granted begin-control callback below.
            panel.makeKeyAndOrderFront(nil)
            panel.updateController()
            DispatchQueue.main.async { [weak self, weak panel] in
                guard let self, let panel, self.previewGeneration == token, self.previewRequested else { return }
                guard self.controlledPreviewPanel !== panel else { return }
                // A missing responder controller must not leave an empty Quick
                // Look window covering the HUD. This request owns only its own
                // pending presentation, and cannot dismiss a later preview.
                self.closePreview(restoreFocus: true)
                self.canvas.showError(L10n.text("Quick Look could not open. Select the file and try again.", "无法打开快速查看，请选中文件后重试。"))
            }
        } catch { canvas.showError(error.localizedDescription) }
    }

    func acceptsPreviewPanelControl() -> Bool { active && previewRequested && previewLease != nil && !isDraggingOut }

    override func beginPreviewPanelControl(_ panel: QLPreviewPanel!) {
        guard let panel, acceptsPreviewPanelControl() else { return }
        controlledPreviewPanel = panel
        previousPreviewLevel = panel.level
        panel.dataSource = self
        panel.delegate = self
        panel.level = NSWindow.Level(rawValue: max(panel.level.rawValue, (host?.window?.level.rawValue ?? NSWindow.Level.statusBar.rawValue) + 1))
        panel.reloadData()
    }

    override func endPreviewPanelControl(_ panel: QLPreviewPanel!) {
        guard let panel, controlledPreviewPanel === panel else { return }
        clearPreviewControl(panel)
        releasePreviewAccess()
    }

    func numberOfPreviewItems(in panel: QLPreviewPanel!) -> Int { previewItem == nil ? 0 : 1 }

    func previewPanel(_ panel: QLPreviewPanel!, previewItemAt index: Int) -> QLPreviewItem! {
        index == 0 ? previewItem : nil
    }

    func previewPanel(_ panel: QLPreviewPanel!, handle event: NSEvent!) -> Bool {
        guard let event, event.type == .keyDown else { return false }
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        if flags.isEmpty && (event.keyCode == 49 || event.keyCode == 53) {
            closePreview(restoreFocus: true); return true
        }
        if flags == [.command], event.charactersIgnoringModifiers?.lowercased() == "r", let id = previewID {
            reveal(id: id); return true
        }
        return false
    }

    func previewPanel(_ panel: QLPreviewPanel!, sourceFrameOnScreenFor item: QLPreviewItem!) -> NSRect {
        guard let id = previewID, let rect = canvas.cardRect(for: id), let host, let window = host.window else { return .zero }
        return window.convertToScreen(host.convert(project?(rect) ?? rect, to: nil))
    }

    func windowWillClose(_ notification: Notification) {
        guard let panel = notification.object as? QLPreviewPanel, controlledPreviewPanel === panel else { return }
        let alreadyClosing = closingPreview
        closingPreview = true
        defer { closingPreview = alreadyClosing }
        clearPreviewControl(panel)
        releasePreviewAccess()
        panel.updateController()
        if active { restoreHUDFocus() }
    }

    private func closePreview(restoreFocus: Bool) {
        let alreadyClosing = closingPreview
        closingPreview = true
        defer { closingPreview = alreadyClosing }
        previewRequested = false
        if let panel = controlledPreviewPanel ?? requestedPreviewPanel {
            panel.orderOut(nil)
            panel.updateController()
            if controlledPreviewPanel === panel { clearPreviewControl(panel) }
        }
        releasePreviewAccess()
        if restoreFocus, active { restoreHUDFocus() }
    }

    private func clearPreviewControl(_ panel: QLPreviewPanel) {
        guard controlledPreviewPanel === panel else { return }
        // These writes occur either under our granted control or inside its end
        // callback, as required by the shared Quick Look panel contract.
        panel.dataSource = nil
        panel.delegate = nil
        if let level = previousPreviewLevel { panel.level = level }
        previousPreviewLevel = nil
        controlledPreviewPanel = nil
    }

    private func releasePreviewAccess() {
        previewGeneration += 1
        previewRequested = false
        previewID = nil
        previewItem = nil
        requestedPreviewPanel = nil
        previewLease?.close(); previewLease = nil
    }

    private func restoreHUDFocus() {
        guard active, !isDraggingOut, let host else { return }
        host.window?.makeKeyAndOrderFront(nil)
        host.window?.makeFirstResponder(host)
    }

    func layoutAccessibility() {
        guard active, let host else { return }
        defer { HUDControlHighlightLayer.requestRefresh(on: host) }
        let actions = canvas.accessibleActions
        let help = canvas.accessibilityStatus ?? L10n.text(
            "Scroll to browse. Arrow keys select; Shift-click or Shift-arrow extends the selection. Space previews. Command-R reveals in Finder. Delete removes only shelf references.",
            "滚动浏览，方向键选择，Shift 点击或 Shift 加方向键多选。空格键预览，Command-R 在访达中显示，Delete 仅移除暂存架引用。")
        let wanted = Set(actions.map(\.id))
        for id in Array(buttons.keys) where !wanted.contains(id) {
            buttons.removeValue(forKey: id)?.removeFromSuperview()
        }
        for action in actions {
            let button: HUDShelfActionButton
            if let existing = buttons[action.id] { button = existing }
            else {
                button = HUDShelfActionButton(frame: .zero)
                button.actionID = action.id
                button.target = self; button.action = #selector(activateAction(_:))
                button.title = ""; button.isBordered = false
                button.projectedFrame = { [weak self, weak host, weak button] in
                    guard let self, let host, let button, let window = host.window else { return .zero }
                    return window.convertToScreen(host.convert(self.project?(button.sourceRect) ?? button.sourceRect, to: nil))
                }
                host.addSubview(button)
                buttons[action.id] = button
            }
            button.isHidden = false
            button.sourceRect = action.rect
            if button.accessibilityLabel() != action.label { button.setAccessibilityLabel(action.label) }
            if button.accessibilityHelp() != help { button.setAccessibilityHelp(help) }
            if let itemID = UUID(uuidString: String(action.id.split(separator: ":").dropFirst().first ?? "")), action.id.hasSuffix(":select") {
                button.setAccessibilityValue(canvas.selectedIDs.contains(itemID) ? L10n.text("Selected", "已选择") : nil)
            }
            let frame = project?(action.rect) ?? action.rect
            if button.frame != frame { button.frame = frame }
        }
    }

    @objc private func activateAction(_ sender: HUDShelfActionButton) {
        guard active, !isDraggingOut else { return }
        dragCandidate = nil
        onLock?()
        canvas.perform(actionID: sender.actionID)
    }
}

private final class HUDShelfPreviewItem: NSObject, QLPreviewItem {
    let previewItemURL: URL?
    let previewItemTitle: String?
    init(url: URL, title: String) { previewItemURL = url; previewItemTitle = title; super.init() }
}

private final class HUDShelfActionButton: NSButton {
    override func draw(_ dirtyRect: NSRect) {}
    var actionID = ""
    var sourceRect = CGRect.zero
    var projectedFrame: (() -> CGRect)?
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}
