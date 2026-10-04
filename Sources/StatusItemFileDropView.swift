import AppKit

/// Adds file-reference drops without replacing the status item's native button
/// or its menu. The owner supplies the shelf operation; this view never opens,
/// copies, moves or deletes the files advertised on the pasteboard.
final class StatusItemFileDropView: NSView {
    var onDropFiles: (([URL]) -> Bool)?

    private weak var statusButton: NSStatusBarButton?
    private var attemptedSequence: Int?
    private(set) var isDropTarget = false

    init(statusButton: NSStatusBarButton) {
        self.statusButton = statusButton
        super.init(frame: statusButton.bounds)
        autoresizingMask = [.width, .height]
        registerForDraggedTypes([.fileURL])
        setAccessibilityElement(false)
    }

    required init?(coder: NSCoder) { nil }

    override var acceptsFirstResponder: Bool { false }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        statusButton?.acceptsFirstMouse(for: event) ?? false
    }

    // Hit testing must reach this registered view for file drags. Forward the
    // original mouse events so AppKit still owns native menu/button tracking.
    override func mouseDown(with event: NSEvent) { statusButton?.mouseDown(with: event) }
    override func mouseUp(with event: NSEvent) { statusButton?.mouseUp(with: event) }
    override func mouseDragged(with event: NSEvent) { statusButton?.mouseDragged(with: event) }
    override func rightMouseDown(with event: NSEvent) { statusButton?.rightMouseDown(with: event) }
    override func rightMouseUp(with event: NSEvent) { statusButton?.rightMouseUp(with: event) }
    override func rightMouseDragged(with event: NSEvent) { statusButton?.rightMouseDragged(with: event) }
    override func menu(for event: NSEvent) -> NSMenu? { statusButton?.menu(for: event) }

    override func draw(_ dirtyRect: NSRect) {
        guard isDropTarget else { return }
        NSColor.selectedContentBackgroundColor.withAlphaComponent(0.22).setFill()
        NSBezierPath(roundedRect: bounds.insetBy(dx: 1, dy: 1), xRadius: 4, yRadius: 4).fill()
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation { draggingUpdated(sender) }
    override func wantsPeriodicDraggingUpdates() -> Bool { false }

    override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        let operation = acceptedOperation(sender)
        setDropTarget(!operation.isEmpty)
        return operation
    }

    override func prepareForDragOperation(_ sender: NSDraggingInfo) -> Bool {
        let accepted = !acceptedOperation(sender).isEmpty
        if !accepted { endDrop() }
        return accepted
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        defer { endDrop() }
        guard !acceptedOperation(sender).isEmpty, let onDropFiles else { return false }
        let files = fileURLs(sender)
        // Reserve before invoking the owner: a reentrant/repeated callback for
        // this native drag must not import its references a second time.
        attemptedSequence = sender.draggingSequenceNumber
        return onDropFiles(files)
    }

    override func draggingExited(_ sender: NSDraggingInfo?) { endDrop() }
    override func draggingEnded(_ sender: NSDraggingInfo) { endDrop() }
    override func concludeDragOperation(_ sender: NSDraggingInfo?) { endDrop() }

    private func acceptedOperation(_ sender: NSDraggingInfo) -> NSDragOperation {
        guard onDropFiles != nil, attemptedSequence != sender.draggingSequenceNumber,
              !fileURLs(sender).isEmpty else { return [] }
        let allowed = sender.draggingSourceOperationMask
        // Copy/link here describe accepting references. Never return move or
        // delete, which could tell the source to remove its original files.
        if allowed.contains(.copy) { return .copy }
        if allowed.contains(.link) { return .link }
        return []
    }

    private func fileURLs(_ sender: NSDraggingInfo) -> [URL] {
        let pasteboard = sender.draggingPasteboard
        guard let items = pasteboard.pasteboardItems, !items.isEmpty else { return [] }
        let parsed = items.compactMap { item -> URL? in
            guard let value = item.string(forType: .fileURL),
                  let url = URL(string: value), url.isFileURL, !url.path.isEmpty else { return nil }
            return url
        }
        guard parsed.count == items.count,
              let files = pasteboard.readObjects(forClasses: [NSURL.self],
                  options: [.urlReadingFileURLsOnly: true]) as? [URL],
              files.count == items.count, files.allSatisfy(\.isFileURL) else { return [] }
        // Keep AppKit's URL objects, including any drag-granted access metadata.
        // The strings above validate types; they are not the imported references.
        return files
    }

    private func setDropTarget(_ value: Bool) {
        guard isDropTarget != value else { return }
        isDropTarget = value
        needsDisplay = true
    }

    private func endDrop() {
        setDropTarget(false)
    }
}
