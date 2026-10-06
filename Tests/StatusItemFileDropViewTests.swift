import AppKit

enum StatusItemFileDropViewTests {
    static func run() -> Int {
        _ = NSApplication.shared
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !value { fatalError(message, file: file, line: line) }
        }
        let pasteboard = NSPasteboard(name: .init("EndfieldHUD.StatusItemFileDropTests.\(UUID().uuidString)"))
        defer { pasteboard.releaseGlobally() }
        let button = RecordingStatusButton(frame: NSRect(x: 0, y: 0, width: 24, height: 24))
        let view = StatusItemFileDropView(statusButton: button)
        button.addSubview(view)
        let sender = DragInfo(pasteboard: pasteboard)
        let urls = [URL(fileURLWithPath: "/tmp/A file's name.txt"),
                    URL(fileURLWithPath: "/tmp/中文 folder", isDirectory: true)]
        var deliveries: [[URL]] = []
        view.onDropFiles = { deliveries.append($0); return true }

        check(view.frame == button.bounds && view.autoresizingMask == [.width, .height],
              "The drop overlay fills the existing status button and follows size changes")
        check(Set(view.registeredDraggedTypes) == Set(StatusItemFileDropView.fileReferenceTypes),
              "Native file URL, public URL and legacy filename-reference drags register a destination")
        check(!view.wantsPeriodicDraggingUpdates(), "A stationary drag does not request periodic work")
        check(!view.acceptsFirstResponder && !view.isAccessibilityElement(),
              "The transparent drop target adds neither a keyboard stop nor a duplicate accessibility element")
        check(button.hitTest(NSPoint(x: 12, y: 12)) === view,
              "Hit testing reaches the registered overlay, rather than bypassing its file drop destination")

        pasteboard.clearContents()
        check(view.draggingEntered(sender).isEmpty && !view.isDropTarget, "An empty pasteboard is rejected without feedback")
        check(pasteboard.writeObjects(urls.map { $0 as NSURL }), "The private pasteboard contains multiple file URL objects")
        check(view.draggingUpdated(sender) == .copy && view.isDropTarget,
              "Both file and folder references are accepted, with safe copy feedback")
        check(view.prepareForDragOperation(sender), "A valid native drop prepares successfully")
        check(deliveries.isEmpty, "Hover and preparation do not import files")
        check(view.performDragOperation(sender), "A successful shelf callback accepts the drop")
        check(deliveries == [urls], "The owner receives each original URL once in pasteboard order, without changing paths")
        check(!view.isDropTarget, "Successful completion clears drop feedback immediately")
        check(!view.performDragOperation(sender) && deliveries.count == 1,
              "A repeated perform callback cannot deliver a drag twice")
        view.concludeDragOperation(sender)
        view.draggingEnded(sender)
        check(!view.performDragOperation(sender) && deliveries.count == 1,
              "Conclusion and drag-end callbacks do not reopen a completed drag for delivery")

        sender.draggingSequenceNumber += 1
        check(view.draggingEntered(sender) == .copy && view.isDropTarget, "A later drag with the same files can be accepted")
        view.draggingExited(sender)
        check(!view.isDropTarget, "Leaving the menu icon clears hover feedback")
        check(view.draggingEntered(sender) == .copy, "A cancelled hover may reenter before the drag is delivered")
        view.concludeDragOperation(sender)
        check(!view.isDropTarget, "A conclusion callback clears unfinished feedback")
        _ = view.draggingEntered(sender)
        view.draggingEnded(sender)
        check(!view.isDropTarget, "Native drag cancellation clears unfinished feedback")

        sender.draggingSequenceNumber += 1
        view.onDropFiles = { deliveries.append($0); return false }
        _ = view.draggingEntered(sender)
        check(!view.performDragOperation(sender) && !view.isDropTarget,
              "Shelf rejection propagates failure and clears feedback")
        check(!view.performDragOperation(sender) && deliveries.count == 2,
              "A rejected callback is still delivered only once")

        sender.draggingSequenceNumber += 1
        view.onDropFiles = { files in
            deliveries.append(files)
            check(!view.performDragOperation(sender), "A reentrant callback cannot deliver its own drag again")
            return true
        }
        check(view.performDragOperation(sender) && deliveries.count == 3,
              "A valid direct perform callback delivers once even without preceding hover")

        sender.draggingSequenceNumber += 1
        view.onDropFiles = { deliveries.append($0); return true }
        sender.draggingSourceOperationMask = [.move, .delete]
        check(view.draggingEntered(sender).isEmpty && !view.performDragOperation(sender),
              "Move-only sources are rejected so the destination cannot authorize deleting originals")
        check(deliveries.count == 3 && !view.isDropTarget, "Rejected operations have no callback or feedback")
        sender.draggingSourceOperationMask = .link
        check(view.draggingUpdated(sender) == .link, "A reference-link source can deliver references without a move")
        view.draggingExited(nil)
        sender.draggingSourceOperationMask = .copy
        view.onDropFiles = nil
        check(view.draggingEntered(sender).isEmpty && !view.prepareForDragOperation(sender),
              "An unconfigured destination rejects file drops")
        view.onDropFiles = { deliveries.append($0); return true }

        func reject(_ items: [NSPasteboardWriting], _ message: String) {
            sender.draggingSequenceNumber += 1
            pasteboard.clearContents()
            check(pasteboard.writeObjects(items), "The invalid-drop fixture is written")
            check(view.draggingEntered(sender).isEmpty && !view.prepareForDragOperation(sender)
                    && !view.performDragOperation(sender) && !view.isDropTarget, message)
        }
        reject(["file:///tmp/text-is-not-a-file-drop.txt" as NSString], "Plain text resembling a file URL is rejected")
        reject([URL(string: "https://example.com/file.txt")! as NSURL], "Remote URL drops are rejected")
        reject([urls[0] as NSURL, "ordinary text" as NSString], "A mixed file/text drop is rejected as a whole")
        let malformed = NSPasteboardItem()
        malformed.setString("https://example.com/not-a-file", forType: .fileURL)
        reject([malformed], "A non-file URL falsely advertised as a file URL is rejected")
        check(deliveries.count == 3, "Invalid pasteboards never reach the shelf callback")

        func acceptReferences(_ items: [NSPasteboardWriting], expected: [URL], operation: NSDragOperation = .copy) {
            sender.draggingSequenceNumber += 1
            sender.draggingSourceOperationMask = operation
            pasteboard.clearContents()
            check(pasteboard.writeObjects(items), "The alternate reference fixture is written")
            check(view.draggingEntered(sender) == operation && view.prepareForDragOperation(sender),
                  "Documented alternate file references prepare without importing")
            let before = deliveries.count
            check(view.performDragOperation(sender) && deliveries.count == before + 1 && deliveries.last == expected,
                  "An alternate file-reference batch is delivered once, completely and in order")
            check(!view.performDragOperation(sender) && !view.isDropTarget,
                  "Alternate representations retain duplicate-callback protection and clear feedback")
        }
        let publicFile = NSPasteboardItem()
        publicFile.setString(urls[0].absoluteString, forType: .URL)
        acceptReferences([publicFile], expected: [urls[0]])
        acceptReferences([urls[0] as NSURL], expected: [urls[0]], operation: .generic)
        sender.draggingSourceOperationMask = .copy

        // Legacy paths must use the board-level API. They are not UTIs that
        // NSPasteboardItem permits callers to write directly; AppKit expands
        // this representation into native URL items for the destination.
        sender.draggingSequenceNumber += 1
        pasteboard.clearContents()
        pasteboard.declareTypes([StatusItemFileDropView.legacyFilenamesType], owner: nil)
        check(pasteboard.setPropertyList(urls.map(\.path), forType: StatusItemFileDropView.legacyFilenamesType),
              "The legacy board-level filename array is written")
        check(view.draggingEntered(sender) == .copy && view.prepareForDragOperation(sender),
              "A board-level legacy array prepares all its file references")
        let beforeLegacy = deliveries.count
        check(view.performDragOperation(sender) && deliveries.count == beforeLegacy + 1
                && deliveries.last == urls.map { URL(fileURLWithPath: $0.path) },
              "All legacy path references are imported in order through native NSURL reading")
        check(!view.performDragOperation(sender) && !view.isDropTarget,
              "Legacy arrays preserve exactly-once delivery and clear feedback")

        // AppKit canonicalizes legacy lists when written and itself raises an
        // Objective-C exception for non-string entries. Exercise malformed raw
        // values directly, without asking AppKit to serialize invalid paths.
        let invalidPathArrays: [Any] = [[], ["relative/file.txt"], [urls[0].path, 42],
                                       [urls[0].path, "/tmp/bad\0name"], "not an array"]
        for paths in invalidPathArrays {
            check(StatusItemFileDropView.validatedLegacyPaths(paths) == nil,
                  "Malformed legacy path arrays reject the entire batch before native URL reading")
        }
        for paths in [[], ["relative/file.txt"]] {
            sender.draggingSequenceNumber += 1
            pasteboard.clearContents()
            pasteboard.declareTypes([StatusItemFileDropView.legacyFilenamesType], owner: nil)
            check(pasteboard.setPropertyList(paths, forType: StatusItemFileDropView.legacyFilenamesType),
                  "The invalid legacy board-level fixture is written")
            check(view.draggingEntered(sender).isEmpty && !view.prepareForDragOperation(sender)
                    && !view.performDragOperation(sender) && !view.isDropTarget,
                  "Empty or relative legacy paths reject the entire drop")
        }
        reject([URL(string: "https://example.com/file")! as NSURL], "Registering public.url still rejects web links")
        let mixedPublicFile = NSPasteboardItem()
        mixedPublicFile.setString(urls[0].absoluteString, forType: .URL)
        reject([mixedPublicFile, URL(string: "https://example.com/file")! as NSURL],
               "A public.url file reference mixed with a web URL rejects atomically")

        sender.draggingSequenceNumber += 1
        pasteboard.clearContents(); _ = pasteboard.writeObjects(urls.map { $0 as NSURL })
        _ = view.draggingEntered(sender)
        pasteboard.clearContents(); _ = pasteboard.setString("changed", forType: .string)
        check(view.draggingUpdated(sender).isEmpty && !view.isDropTarget,
              "A changed pasteboard is revalidated and clears feedback during a drag")

        for type in [NSEvent.EventType.leftMouseDown, .leftMouseUp, .leftMouseDragged,
                     .rightMouseDown, .rightMouseUp, .rightMouseDragged] {
            let event = NSEvent.mouseEvent(with: type, location: .zero, modifierFlags: [.control],
                timestamp: 1, windowNumber: 0, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
            switch type {
            case .leftMouseDown: view.mouseDown(with: event)
            case .leftMouseUp: view.mouseUp(with: event)
            case .leftMouseDragged: view.mouseDragged(with: event)
            case .rightMouseDown: view.rightMouseDown(with: event)
            case .rightMouseUp: view.rightMouseUp(with: event)
            default: view.rightMouseDragged(with: event)
            }
            check(button.events.last === event, "The existing button receives the exact \(type.rawValue) event and modifiers")
        }
        let event = button.events[0]
        let menu = NSMenu()
        button.menu = menu
        check(view.menu(for: event) === menu && button.menu === menu,
              "Context-menu lookup retains the existing button's native menu")
        check(view.acceptsFirstMouse(for: event), "First-click eligibility delegates to the status button")

        weak var releasedButton: RecordingStatusButton?
        var survivingView: StatusItemFileDropView?
        autoreleasepool {
            let temporary = RecordingStatusButton(frame: .zero)
            releasedButton = temporary
            survivingView = StatusItemFileDropView(statusButton: temporary)
        }
        check(releasedButton == nil, "The drop overlay does not retain its parent button")
        survivingView?.mouseDown(with: event)
        check(survivingView?.menu(for: event) == nil, "A released button makes forwarding harmless")

        let nativeWindow = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 40, height: 36),
                                    styleMask: [.borderless], backing: .buffered, defer: false)
        let previousDelegate = RecordingWindowDelegate()
        nativeWindow.delegate = previousDelegate
        nativeWindow.contentView?.addSubview(button)
        guard let bridge = nativeWindow.delegate as? NSDraggingDestination else {
            fatalError("Attaching the status-button drop view installs native window drag routing")
        }
        check(nativeWindow.delegate !== previousDelegate,
              "The native window covers status-item padding outside the smaller button")
        check(nativeWindow.delegate?.windowShouldClose?(nativeWindow) == false && previousDelegate.closeQueries == 1,
              "Non-drag delegate methods still reach the original window delegate")
        check(bridge.wantsPeriodicDraggingUpdates?() == false,
              "The native window route does not add periodic drag work")
        sender.draggingSequenceNumber += 1
        pasteboard.clearContents(); _ = pasteboard.writeObjects(urls.map { $0 as NSURL })
        let beforeWindowDrop = deliveries.count
        check(bridge.draggingEntered?(sender) == .copy && bridge.prepareForDragOperation?(sender) == true,
              "Native status-window padding accepts the same validated file batch")
        _ = view.draggingUpdated(sender)
        check(bridge.performDragOperation?(sender) == true && deliveries.count == beforeWindowDrop + 1,
              "Moving from native padding into the button still delivers one batch")
        check(!view.performDragOperation(sender) && bridge.performDragOperation?(sender) == false,
              "Window and button routes share exactly-once delivery")
        bridge.concludeDragOperation?(sender)
        check(!view.isDropTarget, "Native window conclusion clears button feedback")

        weak var releasedBridge = nativeWindow.delegate
        button.removeFromSuperview()
        check(nativeWindow.delegate === previousDelegate,
              "Detaching the button restores the existing native window delegate")
        // The local protocol reference retains this bridge until this scope
        // ends; detachment must nevertheless make its callbacks harmless.
        sender.draggingSequenceNumber += 1
        check(bridge.draggingEntered?(sender) == [] && bridge.performDragOperation?(sender) == false,
              "A detached bridge cannot retain or deliver to its former view")
        check(releasedBridge != nil, "The test keeps the detached bridge alive without retaining its window or view")

        // Dock can omit native prepare/perform for menu-bar destinations.
        // Recovery requires a mouse-up in this exact accepted session; ended
        // alone is also sent for Escape and is never sufficient evidence.
        typealias Release = StatusItemFileDropReleaseState
        let identity = Release.Identity(sequence: 100, pasteboard: pasteboard.name,
                                        changeCount: 4, windowNumber: 20, files: urls)
        var release = Release()
        check(!release.needsRelease && !release.permits(identity, endedInside: true),
              "An idle release gate has no monitors to request and never imports")
        check(release.begin(identity, timestamp: 10) && release.needsRelease,
              "A validated native candidate arms one release observation")
        check(!release.begin(identity, timestamp: 11),
              "Repeated updates for one accepted drag do not recreate event monitors")
        release.observeRelease(timestamp: 9, inside: true)
        check(!release.permits(identity, endedInside: true) && release.needsRelease,
              "A mouse-up predating entry cannot complete a later drag")
        release.exit()
        release.observeRelease(timestamp: 12, inside: true)
        check(!release.permits(identity, endedInside: true) && !release.needsRelease,
              "Escape/exit while held disarms recovery before any later mouse release")
        _ = release.begin(identity, timestamp: 20)
        release.observeRelease(timestamp: 21, inside: false)
        check(!release.permits(identity, endedInside: true) && !release.needsRelease,
              "A release outside the menu-bar target cannot be recovered at drag end")
        _ = release.begin(identity, timestamp: 30)
        release.observeRelease(timestamp: 31, inside: true)
        release.exit()
        check(release.permits(identity, endedInside: true) && !release.needsRelease,
              "Observed inside release survives Dock's exited/ended pair without retaining monitors")
        check(!release.permits(identity, endedInside: false),
              "The native drag-end position must still be inside the target")
        let changedIdentities: [Release.Identity] = [
            .init(sequence: 101, pasteboard: identity.pasteboard, changeCount: 4, windowNumber: 20, files: urls),
            .init(sequence: 100, pasteboard: .init("different"), changeCount: 4, windowNumber: 20, files: urls),
            .init(sequence: 100, pasteboard: identity.pasteboard, changeCount: 5, windowNumber: 20, files: urls),
            .init(sequence: 100, pasteboard: identity.pasteboard, changeCount: 4, windowNumber: 21, files: urls),
            .init(sequence: 100, pasteboard: identity.pasteboard, changeCount: 4, windowNumber: 20, files: Array(urls.reversed()))
        ]
        for changed in changedIdentities {
            check(!release.permits(changed, endedInside: true),
                  "Changing sequence, pasteboard, contents, destination or URL order invalidates release evidence")
        }
        release.reset()
        check(!release.permits(identity, endedInside: true) && !release.needsRelease,
              "Normal perform/cleanup invalidates recovery, preserving canonical exactly-once delivery")
        _ = release.begin(identity, timestamp: 40)
        release.observeRelease(timestamp: 41, inside: true)
        check(release.begin(changedIdentities[0], timestamp: 42) && release.needsRelease
                && !release.permits(changedIdentities[0], endedInside: true),
              "A new session must observe its own release even with unchanged file references")
        return count
    }

    private final class RecordingWindowDelegate: NSObject, NSWindowDelegate {
        var closeQueries = 0
        func windowShouldClose(_ sender: NSWindow) -> Bool { closeQueries += 1; return false }
    }

    private final class RecordingStatusButton: NSStatusBarButton {
        var events: [NSEvent] = []
        override func mouseDown(with event: NSEvent) { events.append(event) }
        override func mouseUp(with event: NSEvent) { events.append(event) }
        override func mouseDragged(with event: NSEvent) { events.append(event) }
        override func rightMouseDown(with event: NSEvent) { events.append(event) }
        override func rightMouseUp(with event: NSEvent) { events.append(event) }
        override func rightMouseDragged(with event: NSEvent) { events.append(event) }
        override func menu(for event: NSEvent) -> NSMenu? { menu }
        override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
    }

    private final class DragInfo: NSObject, NSDraggingInfo {
        let draggingPasteboard: NSPasteboard
        var draggingSequenceNumber = 1
        var draggingSourceOperationMask: NSDragOperation = [.copy, .move, .delete]
        var draggingDestinationWindow: NSWindow? { nil }
        var draggingLocation: NSPoint { .zero }
        var draggedImageLocation: NSPoint { .zero }
        var draggedImage: NSImage? { nil }
        var draggingSource: Any? { nil }
        var draggingFormation: NSDraggingFormation = .none
        var animatesToDestination = false
        var numberOfValidItemsForDrop = 0
        var springLoadingHighlight: NSSpringLoadingHighlight { .none }
        init(pasteboard: NSPasteboard) { draggingPasteboard = pasteboard }
        func slideDraggedImage(to screenPoint: NSPoint) {}
        override func namesOfPromisedFilesDropped(atDestination dropDestination: URL) -> [String]? { nil }
        func resetSpringLoading() {}
        func enumerateDraggingItems(options enumOpts: NSDraggingItemEnumerationOptions, for view: NSView?,
            classes classArray: [AnyClass], searchOptions: [NSPasteboard.ReadingOptionKey: Any] = [:],
            using block: (NSDraggingItem, Int, UnsafeMutablePointer<ObjCBool>) -> Void) {}
    }
}
