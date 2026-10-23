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
        var documentID: UUID?, categoryID: UUID?
        var config = AppConfiguration.defaults
        var reader: ReaderCanvas { view.readerCanvasForVerification }
        var archive: ArchiveCanvas { view.archiveCanvasForVerification }
        var confirm: HUDConfirmRequest? { view.designContext.overlay.confirmState.request }
        var toast: HUDToastRequest? { view.designContext.overlay.toastState.current }
        init(_ overlay: OverlayController) { self.overlay = overlay }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate, .leftMouseDown, .leftMouseUp, .leftMouseDragged, .rightMouseDown, .rightMouseUp, .scrollWheel, .keyDown, .keyUp]) { _ in nil }
            try! FileManager.default.createDirectory(at: fixture, withIntermediateDirectories: true)
            try! String(repeating: "Reader fixture: long-form text. 中文読書테스트.\n", count: 400).write(to: fixture.appendingPathComponent("reader.txt"), atomically: true, encoding: .utf8)
            let context = CGContext(data: nil, width: 96, height: 64, bitsPerComponent: 8, bytesPerRow: 0,
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            context.setFillColor(NSColor.systemTeal.cgColor); context.fill(CGRect(x: 0, y: 0, width: 96, height: 64))
            try! NSBitmapImageRep(cgImage: context.makeImage()!).representation(using: .png, properties: [:])!.write(to: fixture.appendingPathComponent("archive.png"))
            config.closeOnFocusLost = false; config.language = .english
            overlay.settingsController!.update { $0 = config }; overlay.initialModuleRequest = .reader
            let screen = NSScreen.main!.frame
            overlay.systemPointerLocationProviderForVerification = { CGPoint(x: screen.midX + 150, y: screen.midY + 70) }
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: config), "Diagnostic HUD opens Reader")
            wait("Reader deployment", until: { self.overlay.systemPhase == .open }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                check(view.sourceWatchForVerification != nil, "Original source shell remains active")
                wait("Reader library", until: { self.reader.isActive && !self.reader.isTransitioning && !self.reader.controller.loading }) { [self] in testReaderOpen() }
            }
        }
        func testReaderOpen() {
            check(reader.controller.books.isEmpty && reader.tab == .library, "Reader starts on an empty isolated library")
            run(.reader, [("readerTabs:0", { [self] in
                check(reader.tab == .open && ids(.reader).contains("chooseFile") && ids(.reader).contains("fromShelf"), "Open tab offers Choose file and From shelf")
            })]) { [self] in
                reader.importFiles([fixture.appendingPathComponent("reader.txt")])
                wait("TXT import", until: { self.reader.controller.books.count == 1 }) { [self] in
                    check(toast?.title == "Book added" && toast?.caption == "reader.txt", "Dropped book joins the library with a toast")
                    wait("TXT render", until: { self.reader.tab == .reading && !self.reader.isTransitioning && self.reader.controller.current != nil
                        && self.ids(.reader).contains("bookmark") }) { [self] in testReading() }
                }
            }
        }
        func testReading() {
            let controller = reader.controller, preferences = controller.preferences
            check(!controller.pages.isEmpty && controller.pages.count <= 3 && controller.error == nil, "Fixture decodes into a page cache bounded to three")
            check(preferences.fontSize == 10 && preferences.lineSpacing == 2 && preferences.margin == 16 && preferences.vertical,
                  "Fresh reader reads top to bottom with the 10/2/16 typography defaults")
            check(overlay.eventLog.events.contains { $0.kind == .readerAction && $0.metadata == ["action": "imported"] }, "Import logs no book title or path")
            snapshot("reader")
            run(.reader, [("bookmark", {})]) { [self] in
                wait("Committed bookmark", until: { self.overlay.eventLog.events.contains { $0.kind == .readerAction && $0.metadata == ["action": "bookmarked"] } }) { [self] in
                    check(controller.book?.bookmarks.count == 1 && toast?.title == "Bookmarked", "Bookmark commits the current page without logging its title")
                    testSettings()
                }
            }
        }
        func testSettings() {
            let controller = reader.controller
            run(.reader, [
                ("settings", { [self] in
                    check(reader.isPopoverOpen && reader.capturesPointer, "Reading settings open as a pointer-capturing pop-over")
                    check(["direction:0", "direction:1", "font", "fontSize:plus", "lineSpacing:minus", "margin:plus"].allSatisfy(ids(.reader).contains),
                          "Settings show direction, font and the three steppers")
                    check(item(.reader, "fontSize:minus")?.enabled == false, "Font size cannot step below 10")
                }),
                ("fontSize:plus", { [self] in check(controller.preferences.fontSize == 11, "Font size + steps the live preferences") }),
                ("direction:0", { [self] in
                    check(!controller.preferences.vertical, "Left to right switches to horizontal pages")
                    view.keyDown(with: key(53))
                    check(!reader.isPopoverOpen, "Esc closes reading settings")
                })
            ]) { [self] in testTurns() }
        }
        func testTurns() {
            let controller = reader.controller, original = controller.current!.location
            let middle = CGPoint(x: ReaderCanvas.pageFrame.midX, y: ReaderCanvas.pageFrame.midY)
            check(reader.scroll(at: middle, delta: CGPoint(x: 0, y: 300)) && controller.current?.location == original, "Vertical wheel cannot turn horizontal pages")
            check(!reader.keyDown(key(125)), "Down arrow is left alone while reading left to right")
            let turns = reader.pageTurnSequence
            view.keyDown(with: key(124))
            check(reader.pageTurnSequence == turns + 1 && reader.isTurningPage == !reader.env.reducesMotion,
                  "Right arrow turns one page, sliding only when motion is enabled")
            wait("Horizontal page turn", until: { controller.current != nil && controller.current?.location != original && !self.reader.isTurningPage }) { [self] in
                run(.reader, [("settings", {}), ("direction:1", { [self] in view.keyDown(with: key(53)) })]) { [self] in
                    let page = controller.current!.location, turns = reader.pageTurnSequence
                    view.keyDown(with: key(125))
                    check(controller.preferences.vertical && !reader.isPopoverOpen && reader.pageTurnSequence == turns + 1, "Top to bottom turns pages with the down arrow")
                    wait("Vertical page turn", until: { controller.current?.location != page && !self.reader.isTurningPage }) { [self] in testLibrary() }
                }
            }
        }
        func testLibrary() {
            let book = reader.controller.book!, tile = "book:" + book.id.uuidString
            run(.reader, [
                ("readerTabs:1", { [self] in check(reader.tab == .library && ids(.reader).contains(tile), "Library lists the imported book as a tile") }),
                (tile + ":more", { [self] in
                    check(reader.isMenuOpen && ["open", "relink", "remove"].allSatisfy { ids(.reader).contains("bookMenu:" + $0) }, "Book menu offers Open, Relink and Remove")
                }),
                ("bookMenu:remove", { [self] in
                    check(confirm?.id == "readerRemove" && confirm?.danger == true && !reader.isMenuOpen, "Removing a book asks on the shared confirm bar")
                    view.keyDown(with: key(53))
                })
            ]) { [self] in
                wait("Remove cancelled", until: { !self.view.designContext.overlay.capturesInput }) { [self] in
                    check(reader.controller.books.map(\.id) == [book.id], "Esc keeps the book in the library")
                    testMarks()
                }
            }
        }
        func testMarks() {
            let mark = reader.controller.book!.bookmarks[0], row = "mark:" + mark.id.uuidString
            run(.reader, [("readerTabs:3", { [self] in check(reader.tab == .marks && ids(.reader).contains(row + ":remove"), "Marks lists the saved bookmark") }),
                          (row + ":go", {})]) { [self] in
                wait("Bookmark jump", until: { self.reader.tab == .reading && !self.reader.isTransitioning && self.reader.controller.current?.location == mark.location }) { [self] in
                    beginArchive()
                }
            }
        }
        func beginArchive() {
            overlay.selectSystemModule(.archive)
            wait("Archive transition", until: { self.overlay.systemSelectedModule == .archive && !self.overlay.isSwitchingSystemModule
                && self.archive.isActive && !self.archive.isTransitioning }) { [self] in testCategories() }
        }
        func testCategories() {
            check(!reader.controller.active, "Leaving Reader releases active decoding ownership")
            check(archive.controller.entries.isEmpty && archive.controller.categories.isEmpty, "Archive starts with isolated data")
            check(item(.archive, "renameCategory")?.enabled == false && item(.archive, "removeCategory")?.enabled == false, "The fixed All filter cannot be renamed or removed")
            run(.archive, [("addCategory", {})]) { [self] in
                guard let id = archive.controller.categories.first?.id else { fail("+ adds a category") }
                categoryID = id
                check(archive.controller.categories.map(\.name) == ["Category 1"] && toast?.title == "Category added" && toast?.caption == "Category 1", "+ adds Category 1 with a toast")
                run(.archive, [
                    ("category:" + id.uuidString, { [self] in
                        check(archive.filter == .category(id) && archive.controller.categories.first?.isNew == false, "Opening a category filters by it and clears NEW")
                    }),
                    ("renameCategory", { [self] in
                        guard let field = archive.textFields.first(where: { $0.id == "categoryName" }), field.isEditing else { fail("Rename edits the name in place") }
                        field.setText("Journal fixture"); field.endEditing(commit: true)
                        check(archive.controller.categories.first?.name == "Journal fixture" && toast?.title == "Category renamed", "Committing the field renames the category")
                    }),
                    ("new", {})
                ]) { [self] in testEditor() }
            }
        }
        func testEditor() {
            guard let entry = archive.controller.selected, let title = archive.textFields.first(where: { $0.id == "title" }) else { fail("New opens a document") }
            documentID = entry.id
            check(entry.categoryID == categoryID && entry.template == .journal, "New files a journal into the open category")
            check(["back", "category", "title", "format.font", "format.size", "format.bold", "format.italic", "format.underline", "format.strikethrough", "format.color.custom", "media", "classify", "delete"].allSatisfy(ids(.archive).contains),
                  "Editor shows the header, format bar and footer controls")
            check(title.text.isEmpty && title.placeholder == "Title", "Empty title shows the Title placeholder")
            run(.archive, [("title", {})]) { [self] in
                check(title.isEditing, "Title edits in place")
                type("Batch six title")
                check(archive.controller.selected?.title == "Batch six title", "Typing updates the document title")
                (view.window?.firstResponder as? NSTextView)?.keyDown(with: key(48, "\t"))
                guard !title.isEditing, let body = view.window?.firstResponder as? ArchiveBodyTextView,
                      let editor = view.subviews.compactMap({ $0 as? HUDProjectedTextEditor }).first(where: { $0.textView === body }) else { fail("Tab moves from the title to the body") }
                type("Batch six fixture\n中文・日本語・한국어\n" + String(repeating: "Persistent journal body.\n", count: 100))
                check(archive.controller.selected?.body.hasPrefix("Batch six fixture") == true, "Typing updates the document body")
                check(editor.scrollView.alphaValue == 0 && sequence(first: editor.artwork, next: { $0.superlayer }).contains { $0 === archive.root },
                      "Body text is drawn only on the module's tilted plane")
                check(body.frame.height > editor.logicalRect.height && archive.bodyRect == ArchiveCanvas.bodyRect, "Long body scrolls inside the fixed editor")
                editor.captureVisibleArtwork(); let captures = editor.captureCount
                for _ in 0..<60 { editor.refreshProjection() }
                check(editor.captureCount == captures && editor.retainedPixelCount <= HUDProjectedTextEditor.maximumPixels, "Live tilt reuses one bounded text raster")
                body.doCommand(by: #selector(NSResponder.cancelOperation(_:)))
                check(view.window?.firstResponder !== body, "Esc leaves the body")
                testFiling()
            }
        }
        func testFiling() {
            let id = categoryID!.uuidString
            run(.archive, [
                ("format.bold", { [self] in check(item(.archive, "format.bold")?.role == .toggle(true), "Bold formats the whole unfocused document") }),
                ("category", { [self] in
                    check(archive.isMenuOpen && ids(.archive).contains("moveTo:uncategorized") && ids(.archive).contains("moveTo:" + id), "Category menu lists Uncategorized and the custom category")
                }),
                ("moveTo:uncategorized", { [self] in
                    check(archive.controller.selected?.categoryID == nil && toast?.title == "Moved" && toast?.caption == "Uncategorized", "Moving the document re-files it")
                })
            ]) { [self] in
                archive.importFiles([fixture.appendingPathComponent("archive.png")])
                wait("Archive media", until: { self.archive.controller.selected?.media.count == 1 && self.archive.presentation != nil && self.ids(.archive).contains("removeMedia") }) { [self] in
                    check(toast?.title == "Media added" && archive.bodyRect.height < ArchiveCanvas.bodyRect.height, "Dropped image joins the document above a shorter body")
                    run(.archive, [("delete", { [self] in
                        check(confirm?.id == "deleteDocument" && confirm?.message == "Batch six title" && confirm?.danger == true, "Delete asks on the shared confirm bar")
                        view.keyDown(with: key(53))
                    })]) { [self] in
                        wait("Delete cancelled", until: { !self.view.designContext.overlay.capturesInput }) { [self] in
                            check(archive.controller.selected?.id == documentID, "Esc keeps the document")
                            DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [self] in snapshot("archive-editor"); closeAndReopen() }
                        }
                    }
                }
            }
        }
        func closeAndReopen() {
            weak var closed = archive
            overlay.closeSystemOverlay()
            wait("Archive close", until: { self.overlay.systemPhase == .closed }) { [self] in
                check(closed?.presentation == nil, "Closed archive holds no media decoder")
                check(overlay.lastClosedAnimationCount == 0, "Closed HUD retains no animations")
                overlay.drainDocumentWrites { [self] ok in
                    check(ok, "Close drains final document write")
                    check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: config), "HUD reopens safely")
                    wait("Reopen archive", until: { self.overlay.systemPhase == .open }) { [self] in
                        view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                        wait("Restored document", until: { self.archive.controller.selected?.id == self.documentID && self.archive.isActive
                            && !self.archive.isTransitioning && self.ids(.archive).contains("delete") }) { [self] in finish() }
                    }
                }
            }
        }
        func finish() {
            let entry = archive.controller.selected!, actions = overlay.eventLog.events.filter { $0.kind == .archiveAction }
            check(entry.title == "Batch six title" && entry.body.hasPrefix("Batch six fixture") && entry.media.count == 1 && entry.categoryID == nil,
                  "Title, body, media and filing survive HUD destruction and reopen")
            check(actions.allSatisfy { $0.metadata.keys.allSatisfy { $0 == "action" } } && actions.contains { $0.metadata == ["action": "created"] }
                  && actions.contains { $0.metadata == ["action": "mediaAdded"] }, "Archive logs actions without titles or text")
            run(.archive, [("back", {})]) { [self] in
                wait("Archive gallery", until: { self.archive.controller.selected == nil && !self.archive.isTransitioning && self.ids(.archive).contains("doc:" + self.documentID!.uuidString) }) { [self] in
                    snapshot("archive-gallery")
                    finishGallery()
                }
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
                    print("PASS: \(count) Batch6 native assertions; Reader tabs, drop, bookmarks, settings and page turns, Archive categories, editor, media, confirm bar, persistence and closed lifecycle")
                    BatchSixHUDVerification.session = nil; NSApp.terminate(nil)
                }
            }
        }
        func ids(_ module: HUDModule) -> [String] { view.designControlIDsForVerification(module) }
        func item(_ module: HUDModule, _ id: String) -> HUDAccessibilityItem? {
            view.designCanvasForVerification(module)?.accessibilityItems.first { $0.id == id }
        }
        /// Presses visible controls in order, verifying after each settles.
        func run(_ module: HUDModule, _ steps: [(String, () -> Void)], then next: @escaping () -> Void) {
            guard let step = steps.first else { next(); return }
            check(ids(module).contains(step.0), "\(module.rawValue) shows \(step.0)")
            view.performDesignActionForVerification(module, step.0)
            wait(step.0 + " settles", until: { self.view.designCanvasForVerification(module)?.isTransitioning == false }) { [self] in
                step.1(); run(module, Array(steps.dropFirst()), then: next)
            }
        }
        func key(_ code: UInt16, _ characters: String = "", _ flags: NSEvent.ModifierFlags = []) -> NSEvent {
            NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: flags, timestamp: ProcessInfo.processInfo.systemUptime,
                             windowNumber: view.window!.windowNumber, context: nil, characters: characters,
                             charactersIgnoringModifiers: characters, isARepeat: false, keyCode: code)!
        }
        /// Types through the focused native editor, as the keyboard does.
        func type(_ text: String) {
            guard let editor = view.window?.firstResponder as? NSTextView else { fail("A projected native editor has keyboard focus") }
            editor.insertText(text, replacementRange: NSRange(location: NSNotFound, length: 0))
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
