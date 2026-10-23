import AppKit
import Darwin

/// AppKit duties of the File Shelf: the file chooser, opening and revealing
/// references, the outgoing drag session and incoming drops. The shelf itself
/// is a redesigned canvas that the host routes like every other; its
/// properties card replaces Quick Look (Windows A1).
final class HUDFileShelfInteraction: NSObject, NSDraggingSource {
    private let canvas: FileShelfCanvas
    private let store: FileShelfStore?
    private weak var host: NSView?
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

    /// The bridge works while its canvas accepts input (the host routes it).
    private var active: Bool { canvas.isActive }
    private var externalDrag = false
    private var dragLeases: [ShelfFileAccess] = []
    private var dragSession: NSDraggingSession?
    // NSDraggingSource can outlive the current HUD. Hold both source and access
    // until AppKit ends the session, even if the parent closes/deactivates first.
    private var dragRetainer: HUDFileShelfInteraction?
    private(set) var isDraggingOut = false
    private var chooser: NSOpenPanel?
    private var dialogGeneration = 0

    var isPresentingPanel: Bool { chooser != nil }
    var isInputLocked: Bool { externalDrag || isDraggingOut || isPresentingPanel }

    init(canvas: FileShelfCanvas, store: FileShelfStore?, host: NSView) {
        self.canvas = canvas; self.store = store; self.host = host
        super.init()
        canvas.onChooseFiles = { [weak self] in self?.chooseFiles() }
        canvas.onOpen = { [weak self] in self?.open(id: $0) ?? false }
        canvas.onReveal = { [weak self] in self?.reveal(id: $0) ?? false }
        canvas.onBeginDrag = { [weak self] ids, primary, event in self?.beginOutgoingDrag(ids: ids, primary: primary, event: event) }
        canvas.onDeactivate = { [weak self] in self?.deactivate() }
    }

    /// Leaving the shelf cancels the chooser and any drop feedback.
    func deactivate() {
        endExternalDrag()
        dialogGeneration += 1
        let panel = chooser
        chooser = nil
        panel?.cancel(nil)
        // In particular, do not release dragLeases or dragRetainer here. The
        // destination may still need the URL after the HUD has closed.
    }

    // MARK: Incoming files

    func beginExternalDrag() {
        guard active, !externalDrag, !isDraggingOut else { return }
        onLock?(); externalDrag = true
        canvas.setDropTarget(true)
    }

    func endExternalDrag() { externalDrag = false; canvas.setDropTarget(false) }

    static func acceptsFiles(_ pasteboard: NSPasteboard) -> Bool {
        pasteboard.canReadObject(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true])
    }

    @discardableResult func importPasteboard(_ pasteboard: NSPasteboard) -> Bool {
        guard active, !isDraggingOut else { return false }
        onLock?()
        return canvas.importFiles(from: pasteboard)
    }

    private func chooseFiles() {
        guard active, !isDraggingOut, chooser == nil, let window = host?.window else { return }
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

    // MARK: Open and reveal

    private func open(id: UUID) -> Bool {
        guard active, !isDraggingOut, let store else { return false }
        do {
            let access = try store.access(id: id)
            defer { access.close() }
            guard NSWorkspace.shared.open(access.url) else {
                canvas.showError(L10n.text("This item could not be opened.", "无法打开此项目。"))
                return false
            }
            return true
        } catch { canvas.showError(error.localizedDescription); return false }
    }

    private func reveal(id: UUID) -> Bool {
        guard active, !isDraggingOut, let store else { return false }
        do {
            let access = try store.access(id: id)
            if let onRevealRequested { onRevealRequested(access) }
            else {
                defer { access.close() }
                NSWorkspace.shared.activateFileViewerSelecting([access.url])
            }
            return true
        } catch { canvas.showError(error.localizedDescription); return false }
    }

    // MARK: Outgoing drag

    private func beginOutgoingDrag(ids: [UUID], primary: UUID, event: NSEvent) {
        guard active, !isDraggingOut, let host, host.window != nil, let store else { return }
        dragDiagnostic("begin")
        var accesses: [ShelfFileAccess] = []
        do {
            // Resolve every reference before starting. A failure never silently
            // exports only part of the user's selection, and acquired scopes are
            // balanced by the catch branch below.
            for itemID in ids { accesses.append(try store.access(id: itemID)) }
            guard !accesses.isEmpty else { return }
            let point = host.convert(event.locationInWindow, from: nil)
            let projected = canvas.tileRect(for: primary).map { canvas.context.projected($0) }
            let side = min(72, max(36, (projected?.height ?? 52) * 0.6))
            let drags = accesses.enumerated().map { index, access -> NSDraggingItem in
                let drag = NSDraggingItem(pasteboardWriter: access.url as NSURL)
                let offset = CGFloat(min(index, 4)) * 5
                drag.setDraggingFrame(CGRect(x: point.x - side / 2 + offset, y: point.y - side / 2 + offset, width: side, height: side),
                                      contents: NSWorkspace.shared.icon(forFile: access.url.path))
                return drag
            }
            onLock?()
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

    func draggingSession(_ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation { .copy }

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

    private func restoreHUDFocus() {
        guard active, !isDraggingOut, let host else { return }
        host.window?.makeKeyAndOrderFront(nil)
        host.window?.makeFirstResponder(host)
    }
}
