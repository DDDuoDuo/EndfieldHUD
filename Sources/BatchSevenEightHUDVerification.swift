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
                let canvas = view.mediaAssemblyCanvasForVerification
                canvas.perform("open")
                let menu = view.mediaAssemblyInteractionForVerification!.secondaryMenu!
                check(menu.artwork.superlayer === canvas.layer, "Media source chooser shares tilted HUD plane")
                check(menu.artwork.position.y == 41, "Media chooser anchors below Open")
                menu.onCancel?()
                view.mediaAssemblyInteractionForVerification!.importFiles([directory.appendingPathComponent("fixture.png")])
                wait("image preview", until: { self.view.mediaAssemblyCanvasForVerification.controller.preview != nil }) { [self] in testMedia() }
            }
        }
        func testMedia() {
            let canvas = view.mediaAssemblyCanvasForVerification, controller = canvas.controller
            check(controller.document?.name == "fixture.png" && controller.error == nil, "Native import decodes isolated fixture")
            check(controller.previewPixelCount <= 1600 * 1600, "Preview remains bounded")
            check(canvas.actions.filter { $0.enabled }.count >= 8, "Editing/export controls are available")
            check(canvas.actions.allSatisfy { $0.rect.maxY <= 400 }, "Editing controls clear the battery bar")
            canvas.perform("adjust")
            check(canvas.inlineParameters.count == 8, "All eight color/exposure controls are present")
            check(view.mediaAssemblyInteractionForVerification!.secondaryMenu == nil && canvas.activeTool == .adjust, "Adjust replaces the inline drawer without a secondary menu")
            let rect = canvas.parameterControls.first!.rect
            canvas.mouseDown(at:CGPoint(x:rect.minX+rect.width*0.75,y:rect.midY))
            canvas.mouseDragged(to:CGPoint(x:rect.midX,y:rect.midY)); canvas.mouseUp()
            check(controller.adjustments.brightness == 0, "Slider drag routes on the projected canvas")
            canvas.perform("crop")
            check(canvas.cropHandlePoints.count == 4 && canvas.inlineParameters.isEmpty && view.mediaAssemblyInteractionForVerification!.secondaryMenu == nil, "Crop uses four direct handles instead of slider menus")
            canvas.perform("filters")
            check(canvas.drawer == .filters && canvas.actions.contains { $0.id == "filter:sp_filter_1" }, "Original Photo Mode filters appear in the retained left drawer")
            check(MediaAssemblyAssetCatalog.filterThumbnail(.special1) != nil && MediaAssemblyAssetCatalog.stickerImage(.sticker7) != nil, "Authentic Photo Mode assets are bundled")
            canvas.perform("filters")
            let pendingColorEdits = controller.pendingPreviewCount
            canvas.perform("sticker:sticker_1")
            check(canvas.selectedStickerID != nil && controller.adjustments.stickers.count == 1, "Original sticker can be inserted")
            _ = canvas.adjustSelectedSticker(dx:0.1,dy:0.05,size:0.1,rotation:25)
            _ = canvas.zoom(by:2,at:CGPoint(x:220,y:198))
            check(canvas.viewport.zoom == 2 && controller.pendingPreviewCount == pendingColorEdits, "Zoom/sticker transforms use retained artwork without pixel recomputation")
            canvas.perform("zoomReset")
            canvas.perform("rotate"); canvas.perform("mirror")
            check(controller.adjustments.rotationQuarterTurns == 1 && controller.adjustments.mirrored, "Rotate and mirror apply without touching original")
            controller.updateAdjustments { $0.brightness = 0.12 }
            wait("edited preview", until: { controller.preview?.height == 800 && controller.preview?.width == 500 }) { [self] in
                snapshot("media-assembly")
                controller.export(to: directory.appendingPathComponent("export.png"), overwrite: false) { [self] result in
                    check((try? result.get()) != nil, "HUD controller exports edited image")
                    check(try! Data(contentsOf: directory.appendingPathComponent("fixture.png")) != Data(contentsOf: directory.appendingPathComponent("export.png")), "Save As preserves source bytes")
                    beginCalendar()
                }
            }
        }
        func beginCalendar() {
            overlay.selectSystemModule(.calendar)
            wait("Calendar transition", until: { self.overlay.systemSelectedModule == .calendar && !self.overlay.isSwitchingSystemModule && !self.view.calendarCanvasForVerification.controller.busy }) { [self] in
                check(view.mediaAssemblyCanvasForVerification.controller.preview == nil && !view.mediaAssemblyCanvasForVerification.controller.hasActivePlaybackObserver, "Leaving Media releases preview and playback observers")
                let canvas = view.calendarCanvasForVerification
                check(canvas.controller.events.isEmpty, "Calendar begins in an isolated empty store")
                canvas.perform("new")
                let input = view.calendarInteractionForVerification!, menu = input.secondaryMenu!
                check(menu.artwork.superlayer === canvas.layer && input.capturesPointer, "Calendar editor captures input on the tilted plane")
                check(input.editorForVerification("title")!.scrollView.alphaValue == 0, "Calendar uses projection-only text rendering")
                let title = input.editorForVerification("title")!
                title.textView.string = "Isolated reminder fixture"
                input.textDidChange(Notification(name: NSText.didChangeNotification, object: title.textView))
                let tomorrow = canvas.controller.today.advanced(1, zone: canvas.controller.zone)!
                let date = input.editorForVerification("date")!
                date.textView.string = tomorrow.string
                input.textDidChange(Notification(name: NSText.didChangeNotification, object: date.textView))
                menu.perform("save")
                wait("Calendar save", until: { self.view.calendarCanvasForVerification.controller.events.count == 1 && self.view.calendarInteractionForVerification!.secondaryMenu == nil }) { [self] in
                    let event = canvas.controller.events[0]; eventID = event.id
                    check(event.title == "Isolated reminder fixture" && event.day == tomorrow, "Editor commits title and civil date")
                    check(canvas.controller.permission == .unavailable, "Diagnostic calendar never requests or schedules real notifications")
                    canvas.perform("day:" + tomorrow.string)
                    check(canvas.eventsForSelectedDay.count == 1, "Selected date displays saved event")
                    check(overlay.eventLog.events.contains { $0.kind == .calendarAction && $0.metadata == ["action": "created"] }, "Calendar logs no title or date")
                    snapshot("calendar")
                    beginArchive()
                }
            }
        }
        func beginArchive() {
            overlay.selectSystemModule(.archive)
            wait("Archive transition", until: { self.overlay.systemSelectedModule == .archive && !self.overlay.isSwitchingSystemModule }) { [self] in
                let canvas = view.archiveCanvasForVerification
                check(canvas.controller.categories.isEmpty, "Fresh Archive has no custom categories")
                check(canvas.actions.contains { $0.id == "deleteCategory" && !$0.enabled }, "Fixed All category cannot be deleted")
                check(ArchiveCanvas.removeCategoryRect.minX > ArchiveCanvas.addRect.maxX && ArchiveCanvas.removeCategoryRect.minY == ArchiveCanvas.addRect.minY, "Minus is immediately beside Add")
                snapshot("archive")
                closeAndReopen()
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
                        wait("restored event", until: { self.view.calendarCanvasForVerification.controller.events.contains { $0.id == self.eventID } }) { [self] in finish() }
                    }
                }
            }
        }
        func finish() {
            check(overlay.notesForVerification.isEmpty, "New modules never mutate Notes")
            check(view.mediaAssemblyCanvasForVerification.controller.document?.name == "fixture.png", "Media edit session survives HUD close")
            overlay.closeSystemOverlay()
            wait("final close", until: { self.overlay.systemPhase == .closed }) { [self] in
                overlay.drainDocumentWrites { [self] success in
                    check(success, "Final pending writes completed"); done = true
                    if let monitor { NSEvent.removeMonitor(monitor) }
                    overlay.systemPointerLocationProviderForVerification = nil
                    try? FileManager.default.removeItem(at: directory)
                    print("PASS: \(count) Batch7/8 native assertions; editing, export, projected menus, Calendar CRUD, isolation and close/reopen")
                    BatchSevenEightHUDVerification.session = nil; NSApp.terminate(nil)
                }
            }
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
