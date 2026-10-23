import AppKit

/// Uses diagnostic stores, a throwaway pen-settings domain, synthetic pointer
/// input and the real surface handoff.
enum ProjectionHUDVerification {
    private static var session: Session?
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        let value = Session(overlay); session = value; value.start()
    }
    private final class Session {
        let overlay: OverlayController
        var count = 0, done = false
        var monitor: Any?
        var configuration = AppConfiguration.defaults
        let settingsDomain = "EndfieldCharge.Smoke.Projection.\(UUID().uuidString)"
        var settings: ProjectionSettings { ProjectionSettings.shared }
        var page: HUDDesignCanvas? { NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first?.designCanvasForVerification(.projection) }
        var ids: [String] { overlay.designControlIDsForVerification(.projection) }
        init(_ overlay: OverlayController) { self.overlay = overlay }
        func item(_ id: String) -> HUDAccessibilityItem? { page?.accessibilityItems.first { $0.id == id } }
        func press(_ id: String) { overlay.performDesignActionForVerification(.projection, id) }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate,
                .leftMouseDown, .leftMouseUp, .leftMouseDragged, .rightMouseDown, .rightMouseUp, .scrollWheel, .keyDown, .keyUp]) { _ in nil }
            configuration.closeOnFocusLost = false; configuration.language = .english
            overlay.settingsController!.update { $0 = configuration }
            // The pen preferences persist; this run keeps them out of the app's own defaults.
            ProjectionSettings.shared = ProjectionSettings(defaults: UserDefaults(suiteName: settingsDomain)!)
            let frame = NSScreen.main!.frame
            overlay.systemPointerLocationProviderForVerification = { CGPoint(x: frame.midX, y: frame.midY) }
            overlay.initialModuleRequest = .notes
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD opens in isolation")
            wait("HUD entrance", until: { self.overlay.systemPhase == .open }) { self.beginProjection() }
        }
        func beginProjection() {
            check(overlay.notesForVerification.isEmpty, "No real notes are loaded")
            overlay.selectSystemModule(.projection)
            wait("Projection page", until: { self.ids.contains("open") }) { [self] in
                check(overlay.systemPhase == .open && !overlay.isProjectionActive && overlay.projectionForVerification == nil,
                      "Selecting Projection shows its centre page without opening the workspace")
                check(Set(ProjectionSettings.palette.map { String(format: "color:%06X", $0) }
                          + ["pen", "eraser", "background", "media", "clear", "penWidth", "dotted"]).isSubset(of: ids)
                      && item("clear")?.enabled == false, "Pen toolbar, width, background and launcher are live; Clear all waits for a drawing")
                press("eraser")
                check(settings.values.erasing, "Eraser is a pen tool of the centre page")
                press("color:22A6F2")
                check(settings.values.color == 0x22A6F2 && !settings.values.erasing, "A swatch picks the pen colour and returns to the pen")
                page?.adjustAccessibilitySlider("penWidth", value: 20)
                check(settings.values.width == 20, "The pen width slider sets the workspace brush")
                press("dotted")
                check(!settings.values.background && item("dotted")?.role == .toggle(false), "The Dotted row turns the background off")
                press("background")
                check(settings.values.background && item("dotted")?.role == .toggle(true), "The Background tool and the Dotted row share one setting")
                press("open")
                check(overlay.systemPhase == .closing && overlay.isProjectionActive
                      && overlay.projectionForVerification?.workspace == nil, "HUD exit finishes before projection is created")
                check(!overlay.isIdleForUpdate, "Updater cannot interrupt a projection handoff")
                wait("projection entry", until: { self.overlay.projectionForVerification?.workspace?.active == true }) { self.checkWorkspace() }
            }
        }
        func checkWorkspace() {
            guard let controller = overlay.projectionForVerification, let workspace = controller.workspace else { fail("Projection missing") }
            check(overlay.systemPhase == .closed && overlay.systemSourceWatchForVerification == nil,
                  "Projection releases the old source renderer and full-display HUD")
            check(overlay.lastClosedAnimationCount == 0 && !overlay.lastClosedSourceTimerActive,
                  "HUD ambient/display clocks stop before the whiteboard")
            check(controller.model.brushWidth == 20 && !controller.model.erasing && controller.model.backgroundEnabled,
                  "The workspace takes the centre page's pen and background")
            check(controller.backdropEnabledForVerification && workspace.backgroundVisibleForVerification,
                  "Default background includes native blur, darkening and static dots")
            check(workspace.window?.sharingType == .readOnly, "Projection allows normal screen mirroring/capture")
            let toolbar = workspace.toolbar, frame = workspace.toolbarFrame
            check(frame.height == ProjectionToolbar.height && frame.minY >= 64 && frame.maxX <= workspace.bounds.maxX,
                  "The redesign toolbar stays below the camera housing near the top")
            let toolIDs = toolbar.controls.map(\.id)
            check(Set(ProjectionSettings.palette.map { String(format: "color:%06X", $0) }
                      + ["color:custom", "pen", "eraser", "brush", "background", "appearance", "media", "clear", "close"]) == Set(toolIDs)
                  && toolIDs.last == "close" && (toolbar.control("close") as? HUDButton)?.title == "Return",
                  "Swatches, custom colour, tools, Clear all and Return share one toolbar panel")
            check(!overlay.isIdleForUpdate, "Presented whiteboard stays outside updater idle state")
            draw(on: workspace)
            check(controller.model.drawing.strokes.count == 1, "Real full-screen input commits a drawing stroke")
            check(overlay.eventLog.events.first?.kind == .projectionAction
                  && overlay.eventLog.events.first?.metadata == ["action": "drawingEdited"], "Projection event omits drawing coordinates")
            overlay.show(persistent: false, duration: 3, replay: true)
            check(!overlay.isVisible, "Battery popup cannot cover the workspace")
            workspace.perform("background")
            wait("transparent background", until: { !controller.backdropEnabledForVerification }) { [self] in
                check(!workspace.backgroundVisibleForVerification && !controller.model.backgroundEnabled,
                      "Background off disables dots, darkening and native material")
                check(!settings.values.background && settings.values.color == 0x22A6F2 && settings.values.width == 20,
                      "Workspace toolbar changes flow back to the centre page's settings without drifting the pen")
                workspace.perform("color:custom")
                check(workspace.popupID == "color" && workspace.animationCountForVerification > 0,
                      "Custom colour opens the shared colour picker with its pop-over entrance")
                let escape = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
                    windowNumber: workspace.window!.windowNumber, context: nil, characters: "\u{1b}",
                    charactersIgnoringModifiers: "\u{1b}", isARepeat: false, keyCode: 53)!
                workspace.window!.keyDown(with: escape)
                check(workspace.popupID == nil && controller.isPresented,
                      "Escape dismisses a secondary menu before leaving Projection")
                controller.onClose?()
                check(overlay.systemPhase == .closed && overlay.isProjectionActive,
                      "Return waits for the projection fade before opening the HUD")
                wait("return entrance", until: { self.overlay.systemPhase == .open }) { [self] in
                    check(overlay.systemSelectedModule == .projection && controller.workspace == nil && !controller.isPresented,
                          "Return shows the Projection page and releases the projection surface")
                    check(!workspace.active && !workspace.hasLiveMediaResources && workspace.animationCountForVerification == 0,
                          "Closed workspace has no playback resources, menus or animations")
                    wait("returned page", until: { self.ids.contains("open") }) { [self] in
                        check(item("dotted")?.role == .toggle(false) && item("clear")?.enabled == true,
                              "The centre page reflects the workspace background and the session drawing")
                        press("open")
                        wait("second projection", until: { controller.workspace?.active == true }) { self.checkRoundTrip(controller) }
                    }
                }
            }
        }
        func checkRoundTrip(_ controller: ProjectionController) {
            check(controller.model.drawing.strokes.count == 1 && !controller.model.backgroundEnabled,
                  "Session drawing/background survive round trips without a new database")
            let workspace = controller.workspace!
            workspace.perform("clear")
            check(workspace.popupID == "clear", "Clear all asks in a pop-over first")
            workspace.perform("clear:confirm")
            check(controller.model.drawing.strokes.isEmpty && controller.model.media.isEmpty
                  && !controller.model.backgroundEnabled && !workspace.hasLiveMediaResources,
                  "Clear all removes content and playback while retaining background preferences")
            check(overlay.eventLog.events.first?.kind == .projectionAction
                  && overlay.eventLog.events.first?.metadata == ["action": "cleared"],
                  "Clear all emits only the safe Projection action category")
            draw(on: workspace)
            check(controller.model.drawing.strokes.count == 1, "The cleared workspace accepts a new stroke")
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration),
                  "Summon hotkey also returns from projection")
            wait("hotkey return", until: { self.overlay.systemPhase == .open && self.item("clear")?.enabled == true }) { [self] in
                press("clear")
                let hud = page?.context.overlay
                check(hud?.capturesInput == true && hud?.confirmState.request?.id == "projectionClear"
                      && hud?.confirmState.request?.title == "Clear the projection canvas?", "The centre Clear all asks on the shared confirm bar")
                hud?.decide(.confirm)
                wait("centre clear", until: { controller.model.drawing.strokes.isEmpty }) { [self] in
                    check(hud?.toastState.current?.title == "Canvas cleared" && item("clear")?.enabled == false && controller.model.media.isEmpty,
                          "Confirming clears the session drawing from the centre page")
                    press("media")
                    wait("media projection", until: { controller.workspace?.active == true }) { self.checkExternal(controller) }
                }
            }
        }
        func checkExternal(_ controller: ProjectionController) {
            check(controller.workspace?.popupID == "media" && !settings.mediaInsertRequested,
                  "Insert media opens the workspace on its media chooser")
            var externalCount = 0
            overlay.closeForExternalPresentation { [self] in
                check(controller.workspace == nil && overlay.systemPhase == .closed,
                      "External updater UI waits until the projection window is released")
                externalCount += 1
            }
            check(externalCount == 0, "External UI cannot bypass the workspace closing fade")
            wait("external presentation", until: { externalCount == 1 }) { [self] in
                check(!overlay.isProjectionActive, "External presentation does not reopen the HUD behind it")
                _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration)
                wait("shutdown setup", until: { self.overlay.systemPhase == .open && self.ids.contains("open") }) { [self] in
                    press("open")
                    overlay.closeForExternalPresentation { externalCount += 1 }
                    overlay.forceCloseSystemOverlay()
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [self] in
                        check(!overlay.isProjectionActive && overlay.systemPhase == .closed && controller.workspace == nil,
                              "Sleep/shutdown cancels a pending handoff without resurrecting either window")
                        check(externalCount == 1, "Shutdown also cancels deferred external UI")
                        check(overlay.notesForVerification.isEmpty && overlay.isIdleForUpdate,
                              "Projection never writes notes and returns to idle")
                        cleanup(); print("PASS: \(count) isolated Projection HUD assertions; centre page pen, width, background and launchers, confirm-bar clear, workspace handoff and return, external presentation, shutdown")
                        NSApp.terminate(nil)
                    }
                }
            }
        }
        func draw(on workspace: ProjectionWorkspaceView) {
            let point = CGPoint(x: 100, y: 200), end = CGPoint(x: 230, y: 260)
            send(.mouseMoved, point, to: workspace)
            send(.leftMouseDown, point, to: workspace); send(.leftMouseDragged, end, to: workspace)
            send(.leftMouseUp, end, to: workspace)
        }
        func send(_ type: NSEvent.EventType, _ point: CGPoint, to view: ProjectionWorkspaceView) {
            let event = NSEvent.mouseEvent(with: type, location: view.convert(point, to: nil), modifierFlags: [], timestamp: 0,
                windowNumber: view.window!.windowNumber, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
            switch type {
            case .mouseMoved: view.mouseMoved(with: event)
            case .leftMouseDown: view.mouseDown(with: event)
            case .leftMouseDragged: view.mouseDragged(with: event)
            default: view.mouseUp(with: event)
            }
        }
        func wait(_ label: String, until condition: @escaping () -> Bool, then action: @escaping () -> Void) {
            let deadline = ProcessInfo.processInfo.systemUptime + 15
            func poll() {
                guard !done else { return }
                NSApp.windows.compactMap { $0 as? NSPanel }.forEach { $0.ignoresMouseEvents = true }
                if condition() { action(); return }
                if ProcessInfo.processInfo.systemUptime > deadline { fail("Timed out: " + label) }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
        }
        func check(_ value: Bool, _ message: String) { count += 1; if !value { fail(message) } }
        func cleanup() {
            done = true; if let monitor { NSEvent.removeMonitor(monitor) }; overlay.systemPointerLocationProviderForVerification = nil
            UserDefaults.standard.removePersistentDomain(forName: settingsDomain)
        }
        func fail(_ message: String) -> Never {
            fputs("FAIL: Projection \(count): \(message)\n", stderr)
            cleanup(); overlay.forceCloseSystemOverlay(); preconditionFailure(message)
        }
    }
}
