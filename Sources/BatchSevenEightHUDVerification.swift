import AppKit

/// Opt-in native integration checks. All stores use the --ui-test temporary
/// domain; Calendar substitutes its no-op notification adapter before startup.
enum BatchSevenEightHUDVerification {
    private static var session: Session?
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        let value = Session(overlay); session = value; value.start()
    }
    private final class Session {
        let overlay: OverlayController
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-Batch78-" + UUID().uuidString)
        var view: SystemHUDView!
        var configuration = AppConfiguration.defaults
        var count = 0, done = false
        var monitor: Any?, eventID: UUID?
        var media: MediaAssemblyCanvas { view.mediaAssemblyCanvasForVerification }
        var calendar: HUDCalendarCanvas { view.calendarCanvasForVerification }
        var archive: ArchiveCanvas { view.archiveCanvasForVerification }
        var confirm: HUDConfirmRequest? { view.designContext.overlay.confirmState.request }
        var toast: HUDToastRequest? { view.designContext.overlay.toastState.current }
        init(_ overlay: OverlayController) { self.overlay = overlay }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate, .leftMouseDown, .leftMouseUp, .leftMouseDragged, .rightMouseDown, .rightMouseUp, .scrollWheel, .keyDown, .keyUp]) { _ in nil }
            try! FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let context = CGContext(data: nil, width: 800, height: 500, bitsPerComponent: 8, bytesPerRow: 0,
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            context.setFillColor(NSColor.systemTeal.cgColor); context.fill(CGRect(x: 0, y: 0, width: 800, height: 500))
            context.setFillColor(NSColor.systemOrange.cgColor); context.fill(CGRect(x: 120, y: 80, width: 260, height: 260))
            let bitmap = NSBitmapImageRep(cgImage: context.makeImage()!)
            try! bitmap.representation(using: .png, properties: [:])!.write(to: directory.appendingPathComponent("fixture.png"))
            configuration.language = .english; configuration.closeOnFocusLost = false
            overlay.settingsController!.update { $0 = configuration }; overlay.initialModuleRequest = .mediaAssembly
            let screen = NSScreen.main!.frame
            overlay.systemPointerLocationProviderForVerification = { CGPoint(x: screen.midX + 160, y: screen.midY - 60) }
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD opens Media Assembly")
            wait("deployment", until: { self.overlay.systemPhase == .open }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                check(view.sourceWatchForVerification != nil, "Original source shell remains active")
                overlay.settingsController!.update { $0.theme = .light }
                for module in [HUDModule.storage,.activityMonitor] {
                    check((view.sourceWatchForVerification!.desktopCaptionColorForVerification(target:.module(module))?.redComponent ?? 1) < 0.2, "Light theme gives \(module.rawValue) dark source-button text")
                }
                overlay.settingsController!.update { $0.theme = .dark }
                for module in [HUDModule.storage,.activityMonitor] {
                    check((view.sourceWatchForVerification!.desktopCaptionColorForVerification(target:.module(module))?.redComponent ?? 0) > 0.9, "Dark theme restores \(module.rawValue) white source-button text")
                }

                check(EndfieldGameIcon.mediaAssembly.sourceImage() != nil && EndfieldGameIcon.calendar.sourceImage() != nil, "Both original-game icons are bundled")
                wait("Media Assembly", until: { self.media.isActive && !self.media.isTransitioning }) { [self] in testEmpty() }
            }
        }
        func testEmpty() {
            check(media.controller.document == nil && item(.mediaAssembly, "close")?.enabled == false && item(.mediaAssembly, "export")?.enabled == false,
                  "Empty Media Assembly disables Close and Export")
            run(.mediaAssembly, [("open", { [self] in
                check(media.isMenuOpen && ids(.mediaAssembly).contains("open:finder") && ids(.mediaAssembly).contains("open:shelf"), "Open offers Finder and the Shelf")
                view.keyDown(with: key(53))
                check(!media.isMenuOpen, "Esc closes the source menu")
            })]) { [self] in
                media.importFiles([directory.appendingPathComponent("fixture.png")])
                wait("image preview", until: { self.media.controller.preview != nil && self.media.isEditable && !self.media.isTransitioning }) { [self] in testMedia() }
            }
        }
        func testMedia() {
            let controller = media.controller
            check(controller.document?.name == "fixture.png" && controller.error == nil, "Native import decodes isolated fixture")
            check(controller.previewPixelCount <= 1600 * 1600, "Preview remains bounded")
            check(media.accessibilityItems.filter(\.enabled).count >= 8 && item(.mediaAssembly, "export")?.enabled == true, "Editing and export controls are available")
            check(media.accessibilityItems.allSatisfy { $0.rect.maxY <= HUDMetric.box.height }, "Editing controls stay inside the content box")
            let preview = controller.preview, rendered = controller.completedPreviewCount
            run(.mediaAssembly, [
                ("mediaTab:2", { [self] in check(media.tab == .stickers, "Stickers tab opens") }),
                ("sticker:" + MediaAssemblyStickerKind.sticker1.rawValue, { [self] in
                    check(media.selectedStickerID != nil && controller.adjustments.stickers.count == 1 && ids(.mediaAssembly).contains("sticker.size"), "Original sticker is inserted and selected")
                    view.keyDown(with: key(124)); view.keyDown(with: key(124, "", .option))
                    let sticker = controller.adjustments.stickers[0]
                    check(abs(sticker.x - 0.51) < 0.001 && sticker.rotation == 3, "Arrow keys nudge and Option-arrow rotates the selected sticker")
                }),
                ("zoomIn", { [self] in
                    check(media.viewport.zoom == 1.25, "Zoom in steps the stage")
                    check(controller.preview === preview && controller.completedPreviewCount == rendered && controller.pendingPreviewCount == 0,
                          "Sticker and zoom edits reuse the rendered preview")
                }),
                ("zoomReset", { [self] in
                    view.keyDown(with: key(53))
                    check(media.viewport.zoom == 1 && media.selectedStickerID == nil, "Reset zoom and Esc restore the stage")
                })
            ]) { [self] in testTools() }
        }
        func testTools() {
            let controller = media.controller
            run(.mediaAssembly, [
                ("mediaTab:0", {}),
                ("tool:adjust", { [self] in
                    check(media.tool == .adjust && media.parameterRowsForTesting.count == 8 && ids(.mediaAssembly).contains("toolBack"), "All eight color/exposure controls are present")
                    let rect = media.parameterRowsForTesting[0].slider.rect(in: media.root)
                    check(media.pointerDown(at: CGPoint(x: rect.minX + rect.width * 0.75, y: rect.midY)) && abs(controller.adjustments.brightness - 0.5) < 0.02,
                          "Pressing the brightness track sets its value")
                    media.pointerDragged(to: CGPoint(x: rect.midX, y: rect.midY)); media.pointerUp(at: CGPoint(x: rect.midX, y: rect.midY))
                    check(controller.adjustments.brightness == 0, "Slider drag routes on the projected canvas")
                }),
                ("toolBack", {}),
                ("tool:crop", { [self] in
                    check(media.tool == .crop && media.stageForTesting.isCropVisible && controller.isCropPreview && ids(.mediaAssembly).contains("crop.edge0"),
                          "Crop uses four direct handles on the whole picture")
                }),
                ("cropDone", { [self] in check(media.tool == nil && !media.stageForTesting.isCropVisible && !controller.isCropPreview, "Done leaves the crop tool") }),
                ("mediaTab:1", { [self] in check(media.tab == .filters, "Filters tab opens") }),
                ("filter:" + MediaAssemblyFilter.special1.rawValue, { [self] in
                    check(controller.adjustments.filter == .special1, "Original Photo Mode filters apply from the Filters grid")
                    check(MediaAssemblyAssetCatalog.filterThumbnail(.special1) != nil && MediaAssemblyAssetCatalog.stickerImage(.sticker7) != nil, "Authentic Photo Mode assets are bundled")
                }),
                ("mediaTab:0", {}),
                ("tool:rotate", {}),
                ("tool:mirror", { [self] in check(controller.adjustments.rotationQuarterTurns == 1 && controller.adjustments.mirrored, "Rotate and mirror apply without touching original") })
            ]) { [self] in
                wait("edited preview", until: { controller.preview?.height == 800 && controller.preview?.width == 500 }) { [self] in
                    snapshot("media-assembly")
                    testExport()
                }
            }
        }
        func testExport() {
            let controller = media.controller, source = directory.appendingPathComponent("fixture.png"), original = try! Data(contentsOf: source)
            run(.mediaAssembly, [
                ("export", { [self] in check(media.isMenuOpen && ids(.mediaAssembly).contains("export:saveAs"), "Export offers Save As… and Overwrite original") }),
                ("export:overwrite", { [self] in
                    check(confirm?.id == "mediaOverwrite" && confirm?.message == "fixture.png" && confirm?.danger == true, "Overwriting asks on the shared confirm bar")
                    view.keyDown(with: key(53))
                })
            ]) { [self] in
                wait("Overwrite cancelled", until: { !self.view.designContext.overlay.capturesInput }) { [self] in
                    // Save As… hands the NSSavePanel URL to this export; the panel itself cannot be scripted.
                    controller.export(to: directory.appendingPathComponent("export.png"), overwrite: false) { [self] result in
                        check((try? result.get()) != nil, "HUD controller exports edited image")
                        check(try! Data(contentsOf: source) == original && original != Data(contentsOf: directory.appendingPathComponent("export.png")),
                              "Cancelled overwrite and Save As preserve source bytes")
                        beginCalendar()
                    }
                }
            }
        }
        func beginCalendar() {
            overlay.selectSystemModule(.calendar)
            wait("Calendar transition", until: { self.overlay.systemSelectedModule == .calendar && !self.overlay.isSwitchingSystemModule
                && self.calendar.isActive && !self.calendar.isTransitioning && !self.calendar.controller.busy }) { [self] in
                check(media.controller.preview == nil && !media.controller.hasActivePlaybackObserver, "Leaving Media releases preview and playback observers")
                let controller = calendar.controller, today = controller.today, title = calendar.titleFieldForTesting, details = calendar.detailsFieldForTesting
                check(controller.events.isEmpty && calendar.selectedDay == today, "Calendar begins on today in an isolated empty store")
                guard let next = today.advanced(1, zone: controller.zone), let target = next.month == today.month ? next : today.advanced(-1, zone: controller.zone) else { fail("Calendar fixture day") }
                run(.calendar, [
                    ("calendar.day:" + target.string, { [self] in check(calendar.selectedDay == target && calendar.eventsForSelectedDay.isEmpty, "A day cell selects its date") }),
                    ("calendar.add", { [self] in
                        check(title.isEditing && controller.events.isEmpty, "Add without a title focuses Title instead of saving")
                        type("Isolated reminder fixture")
                        (view.window?.firstResponder as? NSTextView)?.keyDown(with: key(36, "\r"))
                        check(!title.isEditing && title.text == "Isolated reminder fixture", "Return commits the typed title")
                    }),
                    ("calendar.details", { [self] in check(details.isEditing, "Details edits in place"); details.setText("Fixture details"); details.endEditing(commit: true) }),
                    ("calendar.add", {})
                ]) { [self] in
                    wait("Calendar save", until: { controller.events.count == 1 && !controller.busy && controller.permission != .unknown }) { [self] in
                        let event = controller.events[0]; eventID = event.id
                        check(event.title == "Isolated reminder fixture" && event.details == "Fixture details" && event.day == target, "Add commits title, details and civil date")
                        check(toast?.title == "Event added" && title.text.isEmpty && details.text.isEmpty, "Adding announces the event and clears the fields")
                        check(controller.permission == .unavailable, "Diagnostic calendar never requests or schedules real notifications")
                        check(calendar.eventsForSelectedDay.count == 1 && ids(.calendar).contains("calendar.event:" + event.id.uuidString), "Selected date displays saved event")
                        check(overlay.eventLog.events.contains { $0.kind == .calendarAction && $0.metadata == ["action": "created"] }, "Calendar logs no title or date")
                        testCalendarEdit(event)
                    }
                }
            }
        }
        func testCalendarEdit(_ event: HUDCalendarEvent) {
            let controller = calendar.controller, plate = "calendar.event:" + event.id.uuidString, details = calendar.detailsFieldForTesting
            run(.calendar, [
                (plate, { [self] in
                    check(calendar.editingID == event.id && calendar.titleFieldForTesting.text == event.title && ids(.calendar).contains("calendar.delete"),
                          "Pressing the plate opens the event in the fields")
                }),
                ("calendar.details", { details.setText("Edited fixture details"); details.endEditing(commit: true) }),
                ("calendar.save", {})
            ]) { [self] in
                wait("Calendar edit", until: { controller.events.first?.details == "Edited fixture details" && !controller.busy && self.calendar.editingID == nil }) { [self] in
                    check(toast?.title == "Event saved" && overlay.eventLog.events.contains { $0.kind == .calendarAction && $0.metadata == ["action": "edited"] },
                          "Save edits the open event")
                    run(.calendar, [(plate, {}), ("calendar.delete", { [self] in
                        check(confirm?.id == "calendar.delete" && confirm?.message == event.title && confirm?.danger == true, "Delete asks on the shared confirm bar")
                        view.keyDown(with: key(53))
                    })]) { [self] in
                        wait("Delete cancelled", until: { !self.view.designContext.overlay.capturesInput }) { [self] in
                            check(controller.events.map(\.id) == [event.id], "Esc keeps the event")
                            run(.calendar, [(plate, { [self] in check(calendar.editingID == nil, "Pressing the open plate again closes it") }),
                                            ("calendar.reminders", { [self] in check(toast?.title == "Reminders off", "Reminder refresh reports the no-op adapter") })]) { [self] in
                                snapshot("calendar")
                                beginArchive()
                            }
                        }
                    }
                }
            }
        }
        func beginArchive() {
            overlay.selectSystemModule(.archive)
            wait("Archive transition", until: { self.overlay.systemSelectedModule == .archive && !self.overlay.isSwitchingSystemModule
                && self.archive.isActive && !self.archive.isTransitioning }) { [self] in
                let rects = ["addCategory", "renameCategory", "removeCategory"].compactMap { item(.archive, $0)?.rect }
                check(archive.controller.categories.isEmpty, "Fresh Archive has no custom categories")
                check(item(.archive, "renameCategory")?.enabled == false && item(.archive, "removeCategory")?.enabled == false, "Fixed All category cannot be renamed or deleted")
                check(rects.count == 3 && rects[1].minX > rects[0].maxX && rects[2].minX > rects[1].maxX && Set(rects.map(\.minY)).count == 1,
                      "Add, rename and minus sit in one row")
                run(.archive, [("addCategory", {})]) { [self] in
                    guard let id = archive.controller.categories.first?.id else { fail("+ adds a category") }
                    run(.archive, [("category:" + id.uuidString, {}), ("removeCategory", { [self] in
                        check(confirm?.id == "removeCategory" && confirm?.title == "Remove Category 1?" && confirm?.danger == true, "Minus asks before removing the category")
                        view.keyDown(with: key(36))
                    })]) { [self] in
                        wait("Category removed", until: { self.archive.controller.categories.isEmpty && !self.view.designContext.overlay.capturesInput && !self.archive.isTransitioning }) { [self] in
                            check(archive.filter == .all && !ids(.archive).contains("category:" + id.uuidString) && toast?.title == "Category removed",
                                  "Enter removes the category and returns to All")
                            snapshot("archive")
                            closeAndReopen()
                        }
                    }
                }
            }
        }
        func closeAndReopen() {
            overlay.closeSystemOverlay()
            wait("close", until: { self.overlay.systemPhase == .closed }) { [self] in
                check(overlay.lastClosedAnimationCount == 0, "Closed HUD keeps no retained animation tracks")
                overlay.drainDocumentWrites { [self] success in
                    check(success, "Document and calendar writes drain successfully")
                    overlay.initialModuleRequest = .calendar
                    check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD reopens Calendar")
                    wait("reopen", until: { self.overlay.systemPhase == .open }) { [self] in
                        view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                        wait("restored event", until: { self.calendar.controller.events.contains { $0.id == self.eventID && $0.details == "Edited fixture details" } }) { [self] in finish() }
                    }
                }
            }
        }
        func finish() {
            check(overlay.notesForVerification.isEmpty, "New modules never mutate Notes")
            check(media.controller.document?.name == "fixture.png", "Media edit session survives HUD close")
            overlay.closeSystemOverlay()
            wait("final close", until: { self.overlay.systemPhase == .closed }) { [self] in
                overlay.drainDocumentWrites { [self] success in
                    check(success, "Final pending writes completed"); done = true
                    if let monitor { NSEvent.removeMonitor(monitor) }
                    overlay.systemPointerLocationProviderForVerification = nil
                    try? FileManager.default.removeItem(at: directory)
                    print("PASS: \(count) Batch7/8 native assertions; Media Assembly stickers, tools, filters and export, Calendar add/edit/delete, Archive categories, confirm bar, isolation and close/reopen")
                    BatchSevenEightHUDVerification.session = nil; NSApp.terminate(nil)
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
            guard let output = ProcessInfo.processInfo.environment["HUD_BATCH78_PREVIEW_DIR"] else { return }
            let destination = URL(fileURLWithPath: output)
            do { try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)
                try view.writePNG(to: destination.appendingPathComponent(name + ".png"), scale: 1, background: NSColor(white: 0.08, alpha: 1).cgColor)
            } catch { fail("Snapshot: \(error)") }
        }
        func wait(_ label: String, until condition: @escaping () -> Bool, then action: @escaping () -> Void) {
            let deadline = Date().addingTimeInterval(20)
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
            done = true; fputs("FAIL: Batch7/8 \(count): \(message)\n", stderr)
            if let monitor { NSEvent.removeMonitor(monitor) }; overlay.forceCloseSystemOverlay(); preconditionFailure(message)
        }
    }
}
