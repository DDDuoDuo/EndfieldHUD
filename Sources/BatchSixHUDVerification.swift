import AppKit

/// Runs only in the diagnostic preferences/store domain, never against the
/// user's open books, documents, notes or currently running application.
enum BatchSixHUDVerification {
    private static var session: Session?
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        let value = Session(overlay); session = value; value.start()
    }
    private final class Session {
        let overlay: OverlayController
        let fixture = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-Batch6-\(UUID())", isDirectory: true)
        var view: SystemHUDView!
        var count = 0, done = false
        var monitor: Any?
        var documentID: UUID?
        var config = AppConfiguration.defaults
        init(_ overlay: OverlayController) { self.overlay = overlay }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate, .leftMouseDown, .leftMouseUp, .leftMouseDragged, .rightMouseDown, .rightMouseUp, .scrollWheel, .keyDown, .keyUp]) { _ in nil }
            try! FileManager.default.createDirectory(at: fixture, withIntermediateDirectories: true)
            try! String(repeating: "Reader fixture: long-form text. 中文読書테스트.\n", count: 400).write(to: fixture.appendingPathComponent("reader.txt"), atomically: true, encoding: .utf8)
            config.closeOnFocusLost = false; config.language = .english
            overlay.settingsController!.update { $0 = config }; overlay.initialModuleRequest = .reader
            let screen = NSScreen.main!.frame
            overlay.systemPointerLocationProviderForVerification = { CGPoint(x: screen.midX + 150, y: screen.midY + 70) }
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: config), "Diagnostic HUD opens Reader")
            wait("Reader deployment", until: { self.overlay.systemPhase == .open }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                check(view.sourceWatchForVerification != nil, "Original source shell remains active")
                check(view.readerCanvasForVerification.controller.books.isEmpty, "Reader starts with isolated storage")
                view.readerInteractionForVerification!.importFiles([fixture.appendingPathComponent("reader.txt")])
                wait("TXT render", until: { self.view.readerCanvasForVerification.controller.current != nil }) { [self] in testReader() }
            }
        }
        func testReader() {
            let canvas = view.readerCanvasForVerification
            check(canvas.controller.pages.count <= 3 && !canvas.controller.pages.isEmpty, "Reader bounds page cache to three")
            check(canvas.controller.error == nil, "Fixture decodes without failure")
            let preferences = canvas.controller.preferences
            check(preferences.fontSize == 10 && preferences.lineSpacing == 2 && preferences.margin == 16,
                  "Fresh reader uses requested 10/2/16 typography defaults")
            canvas.perform("open")
            let openMenu = view.readerInteractionForVerification!.secondaryMenu!
            check(openMenu.artwork.position == CGPoint(x: 12, y: 44), "Open menu anchors immediately below Open")
            openMenu.onCancel?()
            snapshot("reader")
            canvas.perform("bookmark")
            wait("Committed bookmark", until: { self.overlay.eventLog.events.contains { $0.kind == .readerAction && $0.metadata == ["action": "bookmarked"] } }) { [self] in testReaderMenu() }
        }
        func testReaderMenu() {
            let canvas = view.readerCanvasForVerification
            check(overlay.eventLog.events.first { $0.kind == .readerAction }?.metadata == ["action": "bookmarked"], "Bookmark logs no book title or path")
            canvas.perform("settings")
            check(view.readerInteractionForVerification!.capturesPointer, "Reading settings captures pointer")
            let menu = view.readerInteractionForVerification!.secondaryMenu!
            check(menu.artwork.superlayer === canvas.layer, "Reader secondary menu shares its tilted plane")
            check((menu.artwork.animationKeys()?.count ?? 0) > 0, "Reader secondary menu opens with animation")
            menu.onCancel?()
            let interaction = view.readerInteractionForVerification!
            let point = CGPoint(x: canvas.viewport.midX, y: canvas.viewport.midY)
            var preferences = canvas.controller.preferences; preferences.vertical = false
            canvas.controller.setPreferences(preferences)
            let original = canvas.controller.current!.location
            _ = interaction.scroll(at: point, deltaX: 0, deltaY: 300)
            check(canvas.controller.current!.location == original, "Vertical scrolling cannot turn horizontal pages")
            let key = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
                windowNumber: view.window!.windowNumber, context: nil, characters: "", charactersIgnoringModifiers: "", isARepeat: false, keyCode: 124)!
            check(interaction.keyDown(key), "Right arrow is routed to the reader")
            wait("Animated reader page turn", until: { canvas.controller.current?.location != original }) { [self] in
                check(canvas.pageTurnSequence > 0 && canvas.isTurningPage, "Attached page receives horizontal turn animation")
                preferences.vertical = true; canvas.controller.setPreferences(preferences)
                check(!canvas.isTurningPage, "Vertical reading removes the flip animation")
                beginArchive()
            }
        }
        func beginArchive() {
            overlay.selectSystemModule(.archive)
            wait("Archive transition", until: { self.overlay.systemSelectedModule == .archive && !self.overlay.isSwitchingSystemModule }) { [self] in testArchive() }
        }
        func testArchive() {
            let canvas = view.archiveCanvasForVerification
            check(!view.readerCanvasForVerification.controller.active, "Leaving Reader releases active decoding ownership")
            check(canvas.controller.entries.isEmpty, "Archive starts with isolated data")
            canvas.perform("new")
            let menu = view.subviews.compactMap { $0 as? NotesRetainedMenu }.first!
            check(menu.artwork.superlayer === canvas.layer, "Archive template menu stays on same tilted plane")
            menu.perform("uncategorized")
            wait("New document", until: { self.view.archiveCanvasForVerification.controller.selected != nil }) { [self] in
                documentID = canvas.controller.selected!.id
                let editors = view.subviews.compactMap { $0 as? HUDProjectedTextEditor }
                check(editors.count == 3, "Title/date/body all use projected native text")
                check(editors.filter { $0.placeholder == "Title" || $0.placeholder == "Content" }.count == 2,
                      "Empty title and body provide grey projected placeholders")
                check(editors.allSatisfy { $0.scrollView.alphaValue == 0 }, "Native TextKit backing cannot duplicate at screen origin")
                let body = editors.first { $0.logicalRect == canvas.bodyRect }!
                check(body.artwork.superlayer === canvas.layer, "Long-form editor inherits source perspective")
                view.window?.makeFirstResponder(body.textView)
                check(canvas.formattingField == "body", "Focusing body exposes the shared square formatting controls")
                body.textView.string = "Batch six fixture\n中文・日本語・한국어\n" + String(repeating: "Persistent journal body.\n", count: 100)
                view.archiveInteractionForVerification!.textDidChange(Notification(name: NSText.didChangeNotification, object: body.textView))
                check(canvas.controller.selected!.body.hasPrefix("Batch six fixture"), "Projected edit updates document")
                check(body.textView.frame.height > body.logicalRect.height, "Body scrolls without stretching HUD")
                body.captureVisibleArtwork(); let captures = body.captureCount
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.22) { [weak self] in self?.snapshot("archive-editor") }
                for _ in 0..<60 { body.refreshProjection() }
                check(body.captureCount == captures, "Live tilt does not recapture text glyphs")
                check(body.retainedPixelCount <= HUDProjectedTextEditor.maximumPixels, "Text raster is bounded")
                canvas.perform("delete")
                view.subviews.compactMap { $0 as? NotesRetainedMenu }.first!.perform("cancel")
                check(canvas.controller.selected?.id == documentID, "Delete requires explicit confirmation")
                let closedCanvas = canvas
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [self] in overlay.closeSystemOverlay() }
                wait("Archive close", until: { self.overlay.systemPhase == .closed }) { [self] in
                    check(closedCanvas.presentation == nil, "Closed archive holds no media decoder")
                    check(overlay.lastClosedAnimationCount == 0, "Closed HUD retains no animations")
                    overlay.drainDocumentWrites { [self] ok in
                        check(ok, "Close drains final document write")
                        check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: config), "HUD reopens safely")
                        wait("Reopen archive", until: { self.overlay.systemPhase == .open }) { [self] in
                            view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                            wait("Restored document", until: { self.view.archiveCanvasForVerification.controller.selected?.id == self.documentID }) { [self] in finish() }
                        }
                    }
                }
            }
        }
        func finish() {
            check(view.archiveCanvasForVerification.controller.selected?.body.hasPrefix("Batch six fixture") == true, "Body survives HUD destruction and reopen")
            view.archiveCanvasForVerification.perform("back")
            wait("Archive gallery", until: { self.view.archiveCanvasForVerification.controller.selected == nil }) { [self] in
                snapshot("archive-gallery")
                finishGallery()
            }
        }
        func finishGallery() {
            check(overlay.notesForVerification.isEmpty, "Reader and Archive never write Notes data")
            check(EndfieldGameIcon.database.sourceImage() != nil && EndfieldGameIcon.readerBook.sourceImage() != nil, "Original-game Database and Reader resources are bundled")
            overlay.closeSystemOverlay()
            wait("Final close", until: { self.overlay.systemPhase == .closed }) { [self] in
                overlay.drainDocumentWrites { [self] ok in
                    check(ok, "Final shutdown has no pending document edits")
                    done = true; if let monitor { NSEvent.removeMonitor(monitor) }
                    overlay.systemPointerLocationProviderForVerification = nil
                    try? FileManager.default.removeItem(at: fixture)
                    print("PASS: \(count) Batch6 native assertions; Reader, projected Archive, menus, persistence, source icons and closed lifecycle")
                    BatchSixHUDVerification.session = nil; NSApp.terminate(nil)
                }
            }
        }
        func snapshot(_ name: String) {
            guard let value = ProcessInfo.processInfo.environment["HUD_BATCH6_PREVIEW_DIR"] else { return }
            let directory = URL(fileURLWithPath: value, isDirectory: true)
            do { try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                try view.writePNG(to: directory.appendingPathComponent(name + ".png"), scale: 1,
                                  background: NSColor(white: 0.08, alpha: 1).cgColor)
            } catch { fail("Preview: \(error)") }
        }
        func wait(_ label: String, until condition: @escaping () -> Bool, then action: @escaping () -> Void) {
            let deadline = Date().addingTimeInterval(15)
            func poll() {
                guard !done else { return }
                NSApp.windows.compactMap { $0 as? NSPanel }.forEach { $0.ignoresMouseEvents = true }
                if condition() { action(); return }
                if Date() > deadline { fail("Timed out: " + label) }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
        }
        func check(_ value: Bool, _ message: String) { count += 1; if !value { fail(message) } }
        func fail(_ message: String) -> Never {
            fputs("FAIL: Batch6 \(count): \(message)\n", stderr); done = true
            if let monitor { NSEvent.removeMonitor(monitor) }; overlay.forceCloseSystemOverlay(); preconditionFailure(message)
        }
    }
}
