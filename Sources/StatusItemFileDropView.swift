import AppKit

/// Adds file-reference drops without replacing the status item's native button
/// or its menu. The owner supplies the shelf operation; this view never opens,
/// copies, moves or deletes the files advertised on the pasteboard.
final class StatusItemFileDropView: NSView {
    var onDropFiles: (([URL]) -> Bool)?
    // System/legacy drags may advertise an array of paths on one item or a
    // file URL through public.url. Register each documented reference format.
    static let legacyFilenamesType = NSPasteboard.PasteboardType("NSFilenamesPboardType")
    static let fileReferenceTypes: [NSPasteboard.PasteboardType] = [.fileURL, .URL, legacyFilenamesType]

    private weak var statusButton: NSStatusBarButton?
    private var windowBridge: StatusItemFileDropWindowBridge?
    private var attemptedSequence: Int?
    private var releaseState = StatusItemFileDropReleaseState()
    private var releaseMonitors: [Any] = []
    private(set) var isDropTarget = false

    init(statusButton: NSStatusBarButton) {
        self.statusButton = statusButton
        super.init(frame: statusButton.bounds)
        autoresizingMask = [.width, .height]
        registerForDraggedTypes(Self.fileReferenceTypes)
        setAccessibilityElement(false)
    }

    required init?(coder: NSCoder) { nil }

    deinit { stopReleaseMonitoring() }

    override func viewWillMove(toWindow newWindow: NSWindow?) {
        clearReleaseRecovery()
        windowBridge?.detach()
        windowBridge = nil
        super.viewWillMove(toWindow: newWindow)
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        guard let window else { return }
        // macOS leaves padding around NSStatusBarButton inside its native
        // status window. Drops in that part never reach the button's subviews.
        // Both routes share this view's validation and once-per-drag delivery.
        windowBridge = StatusItemFileDropWindowBridge(window: window, destination: self)
    }

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
        if operation.isEmpty { clearReleaseRecovery() }
        else { trackDockReleaseIfNeeded(sender) }
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

    override func draggingExited(_ sender: NSDraggingInfo?) {
        guard sender != nil else { endDrop(); return }
        setDropTarget(false)
        // Dock sends exited immediately before ended on a release over the
        // status item. Preserve only an already-observed release for that pair.
        releaseState.exit()
        if !releaseState.needsRelease { stopReleaseMonitoring() }
    }
    override func draggingEnded(_ sender: NSDraggingInfo) {
        defer { endDrop() }
        guard releaseState.hasObservedRelease, let window, let identity = releaseIdentity(sender),
              releaseState.permits(identity, endedInside: window.frame.contains(
                window.convertPoint(toScreen: sender.draggingLocation))) else { return }
        // Some macOS Dock stacks omit prepare/perform for a status item, while
        // delivering entered/updated, the physical release, then exited/ended.
        // Never treat ended alone as a drop: Escape follows that same path.
        // Reuse full native-URL validation and the normal once-per-drag guard.
        _ = performDragOperation(sender)
    }
    override func concludeDragOperation(_ sender: NSDraggingInfo?) { endDrop() }

    private func acceptedOperation(_ sender: NSDraggingInfo) -> NSDragOperation {
        guard onDropFiles != nil, attemptedSequence != sender.draggingSequenceNumber,
              !fileURLs(sender).isEmpty else { return [] }
        let allowed = sender.draggingSourceOperationMask
        // Copy/link here describe accepting references. Never return move or
        // delete, which could tell the source to remove its original files.
        if allowed.contains(.copy) { return .copy }
        if allowed.contains(.link) { return .link }
        // Generic permits an application-defined operation. Ours saves only
        // references; it never reports move/delete back to the drag source.
        if allowed.contains(.generic) { return .generic }
        return []
    }

    static func validatedLegacyPaths(_ propertyList: Any?) -> [String]? {
        guard let paths = propertyList as? [String], !paths.isEmpty,
              paths.allSatisfy({ $0.hasPrefix("/") && !$0.contains("\0") }) else { return nil }
        return paths
    }

    private func fileURLs(_ sender: NSDraggingInfo) -> [URL] {
        let pasteboard = sender.draggingPasteboard
        // AppKit expands a board-level legacy path list into native file-URL
        // items. Validate the original list before allowing its conversion.
        var legacyCount: Int?
        if pasteboard.types?.contains(Self.legacyFilenamesType) == true {
            guard let paths = Self.validatedLegacyPaths(pasteboard.propertyList(forType: Self.legacyFilenamesType)) else { return [] }
            legacyCount = paths.count
        }
        guard let items = pasteboard.pasteboardItems, !items.isEmpty else { return [] }
        let expectedCount = legacyCount ?? items.count
        guard items.count == expectedCount else { return [] }
        func valid(_ url: URL) -> Bool {
            url.isFileURL && url.path.hasPrefix("/") && !url.path.contains("\0")
        }
        for item in items {
            let type: NSPasteboard.PasteboardType
            if item.types.contains(.fileURL) { type = .fileURL }
            else if item.types.contains(.URL) { type = .URL }
            else { return [] }
            guard let value = item.string(forType: type),
                  let url = URL(string: value), valid(url) else { return [] }
        }
        // The file-URLs-only option filters by public.file-url representation,
        // so it drops file URLs advertised as public.url. Validate every item
        // first, then preserve the native NSURL read and drag-granted metadata.
        // Counts still reject a partially decoded or mixed file/text batch.
        guard let native = pasteboard.readObjects(forClasses: [NSURL.self], options: [:]) as? [URL],
              native.count == expectedCount, native.allSatisfy(valid) else { return [] }
        return native
    }

    private func setDropTarget(_ value: Bool) {
        guard isDropTarget != value else { return }
        isDropTarget = value
        needsDisplay = true
    }

    private func endDrop() {
        clearReleaseRecovery()
        setDropTarget(false)
    }

    private func releaseIdentity(_ sender: NSDraggingInfo) -> StatusItemFileDropReleaseState.Identity? {
        guard let window else { return nil }
        let board = sender.draggingPasteboard
        let files = fileURLs(sender)
        guard !files.isEmpty else { return nil }
        return .init(sequence: sender.draggingSequenceNumber, pasteboard: board.name,
                     changeCount: board.changeCount, windowNumber: window.windowNumber, files: files)
    }

    private func trackDockReleaseIfNeeded(_ sender: NSDraggingInfo) {
        // Observed on native Downloads-stack file drags. This is a routing
        // hint only; the data still must pass the documented NSURL reader.
        guard sender.draggingPasteboard.types?.contains(.init("local-file-url")) == true,
              let identity = releaseIdentity(sender) else { clearReleaseRecovery(); return }
        guard releaseState.begin(identity, timestamp: ProcessInfo.processInfo.systemUptime) else { return }
        stopReleaseMonitoring()
        // Mouse-only monitors need no keyboard/accessibility permission. They
        // exist solely during this accepted drag, never while the app is idle.
        if let token = NSEvent.addGlobalMonitorForEvents(matching: .leftMouseUp, handler: { [weak self] event in
            self?.recordRelease(event)
        }) { releaseMonitors.append(token) }
        if let token = NSEvent.addLocalMonitorForEvents(matching: .leftMouseUp, handler: { [weak self] event in
            self?.recordRelease(event)
            return event
        }) { releaseMonitors.append(token) }
    }

    private func recordRelease(_ event: NSEvent) {
        guard let window else { clearReleaseRecovery(); return }
        // Inspect this event's position, not the pointer's possibly newer
        // position by the time a global notification reaches the main thread.
        let point = event.cgEvent?.unflippedLocation
            ?? event.window?.convertPoint(toScreen: event.locationInWindow)
            ?? event.locationInWindow
        releaseState.observeRelease(timestamp: event.timestamp,
                                    inside: window.frame.contains(point))
        if !releaseState.needsRelease { stopReleaseMonitoring() }
    }

    private func stopReleaseMonitoring() {
        releaseMonitors.forEach(NSEvent.removeMonitor)
        releaseMonitors.removeAll()
    }

    private func clearReleaseRecovery() {
        stopReleaseMonitoring()
        releaseState.reset()
    }
}

/// A release is evidence only for the accepted native session and its unchanged
/// pasteboard. It never delivers files itself and cannot accept ended/cancelled
/// sessions without an intervening mouse-up inside the destination.
struct StatusItemFileDropReleaseState {
    struct Identity: Equatable {
        let sequence: Int
        let pasteboard: NSPasteboard.Name
        let changeCount: Int
        let windowNumber: Int
        let files: [URL]
    }

    private var identity: Identity?
    private var entered: TimeInterval = 0
    private var releasedInside = false
    var needsRelease: Bool { identity != nil && !releasedInside }
    var hasObservedRelease: Bool { identity != nil && releasedInside }

    mutating func begin(_ value: Identity, timestamp: TimeInterval) -> Bool {
        guard identity != value else { return false }
        identity = value
        entered = timestamp
        releasedInside = false
        return true
    }

    mutating func observeRelease(timestamp: TimeInterval, inside: Bool) {
        guard needsRelease, timestamp >= entered else { return }
        if inside { releasedInside = true }
        else { reset() }
    }

    mutating func exit() {
        if !releasedInside { reset() }
    }

    func permits(_ value: Identity, endedInside: Bool) -> Bool {
        releasedInside && identity == value && endedInside
    }

    mutating func reset() { identity = nil; releasedInside = false }
}

/// Covers the complete native status-item window without replacing its button
/// or menu. NSWindow delegates receive drag messages for its non-view padding.
private final class StatusItemFileDropWindowBridge: NSObject, NSWindowDelegate, NSDraggingDestination {
    private weak var window: NSWindow?
    private weak var destination: StatusItemFileDropView?
    private weak var previousDelegate: NSWindowDelegate?

    init(window: NSWindow, destination: StatusItemFileDropView) {
        self.window = window
        self.destination = destination
        previousDelegate = window.delegate
        super.init()
        window.delegate = self
        window.registerForDraggedTypes(StatusItemFileDropView.fileReferenceTypes)
    }

    func detach() {
        if let window, window.delegate === self { window.delegate = previousDelegate }
        // NSWindow has no public getter or selective unregister for drag types.
        // Do not erase an existing delegate's registrations. These registrations
        // carry no object references and end with the native status-item window.
        destination = nil
        window = nil
    }

    deinit { detach() }

    override func responds(to selector: Selector!) -> Bool {
        super.responds(to: selector) || previousDelegate?.responds(to: selector) == true
    }

    override func forwardingTarget(for selector: Selector!) -> Any? {
        if let previousDelegate, previousDelegate.responds(to: selector) { return previousDelegate }
        return super.forwardingTarget(for: selector)
    }

    func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation { destination?.draggingEntered(sender) ?? [] }
    func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation { destination?.draggingUpdated(sender) ?? [] }
    func draggingExited(_ sender: NSDraggingInfo?) { destination?.draggingExited(sender) }
    func draggingEnded(_ sender: NSDraggingInfo) { destination?.draggingEnded(sender) }
    func prepareForDragOperation(_ sender: NSDraggingInfo) -> Bool { destination?.prepareForDragOperation(sender) ?? false }
    func performDragOperation(_ sender: NSDraggingInfo) -> Bool { destination?.performDragOperation(sender) ?? false }
    func concludeDragOperation(_ sender: NSDraggingInfo?) { destination?.concludeDragOperation(sender) }
    func wantsPeriodicDraggingUpdates() -> Bool { false }
}
