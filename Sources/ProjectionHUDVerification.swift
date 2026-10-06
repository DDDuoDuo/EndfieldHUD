import AppKit

/// Uses diagnostic stores, synthetic pointer input and the real surface handoff.
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
        init(_ overlay: OverlayController) { self.overlay = overlay }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate,
                .leftMouseDown, .leftMouseUp, .leftMouseDragged, .rightMouseDown, .rightMouseUp, .scrollWheel, .keyDown, .keyUp]) { _ in nil }
            configuration.closeOnFocusLost = false; configuration.language = .english
            overlay.settingsController!.update { $0 = configuration }
            let frame = NSScreen.main!.frame
            overlay.systemPointerLocationProviderForVerification = { CGPoint(x: frame.midX, y: frame.midY) }
            overlay.initialModuleRequest = .notes
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD opens in isolation")
            wait("HUD entrance", until: { self.overlay.systemPhase == .open }) { self.beginProjection() }
        }
        func beginProjection() {
            check(overlay.notesForVerification.isEmpty, "No real notes are loaded")
            overlay.selectSystemModule(.projection)
            check(overlay.systemPhase == .closing && overlay.isProjectionActive
                  && overlay.projectionForVerification?.workspace == nil, "HUD exit finishes before projection is created")
            check(!overlay.isIdleForUpdate, "Updater cannot interrupt a projection handoff")
            wait("projection entry", until: { self.overlay.projectionForVerification?.workspace?.active == true }) { [self] in
                guard let controller = overlay.projectionForVerification, let workspace = controller.workspace else { fail("Projection missing") }
                check(overlay.systemPhase == .closed && overlay.systemSourceWatchForVerification == nil,
                      "Projection releases the old source renderer and full-display HUD")
                check(overlay.lastClosedAnimationCount == 0 && !overlay.lastClosedSourceTimerActive,
                      "HUD ambient/display clocks stop before the whiteboard")
                check(controller.backdropEnabledForVerification && workspace.backgroundVisibleForVerification,
                      "Default background includes native blur, darkening and static dots")
                check(workspace.window?.sharingType == .readOnly, "Projection allows normal screen mirroring/capture")
                let toolbar = workspace.toolbar
                check(toolbar.frame.width == 336 && toolbar.frame.height == 42 && toolbar.frame.minY >= 64,
                      "Compact rounded toolbar stays below the camera housing near the top")
                check(toolbar.items.count == 8 && toolbar.items.allSatisfy { toolbar.bounds.contains($0.rect) }
                      && toolbar.items.firstIndex { $0.id == "clear" } == toolbar.items.firstIndex { $0.id == "eraser" }.map { $0 + 1 },
                      "Clear all sits beside Eraser within the compact toolbar")
                check(toolbar.items.last?.title == "Return" && toolbar.artwork.sublayers?.allSatisfy { $0.cornerRadius == 10 } == true,
                      "Return label and rounded toolbar layers survive native presentation")
                check(!overlay.isIdleForUpdate, "Presented whiteboard stays outside updater idle state")
                let point = CGPoint(x: 100, y: 200), end = CGPoint(x: 230, y: 260)
                send(.mouseMoved, point, to: workspace)
                send(.leftMouseDown, point, to: workspace); send(.leftMouseDragged, end, to: workspace)
                send(.leftMouseUp, end, to: workspace)
                check(controller.model.drawing.strokes.count == 1, "Real full-screen input commits a drawing stroke")
                check(overlay.eventLog.events.first?.kind == .projectionAction
                      && overlay.eventLog.events.first?.metadata == ["action": "drawingEdited"], "Projection event omits drawing coordinates")
                overlay.show(persistent: false, duration: 3, replay: true)
                check(!overlay.isVisible, "Battery popup cannot cover the workspace")
                workspace.perform("background")
                wait("transparent background", until: { !controller.backdropEnabledForVerification }) { [self] in
                    check(!workspace.backgroundVisibleForVerification && !controller.model.backgroundEnabled,
                          "Background off disables dots, darkening and native material")
                    workspace.perform("color")
                    check(workspace.secondaryMenu != nil && workspace.secondaryMenu?.artwork.animationKeys()?.isEmpty == false,
                          "Projection uses animated retained secondary menus")
                    let escape = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
                        windowNumber: workspace.window!.windowNumber, context: nil, characters: "\u{1b}",
                        charactersIgnoringModifiers: "\u{1b}", isARepeat: false, keyCode: 53)!
                    workspace.window!.keyDown(with: escape)
                    check(workspace.secondaryMenu == nil && controller.isPresented,
                          "Escape dismisses a secondary menu before leaving Projection")
                    controller.onClose?()
                    check(overlay.systemPhase == .closed && overlay.isProjectionActive,
                          "Return waits for the projection fade before opening the HUD")
                    wait("return entrance", until: { self.overlay.systemPhase == .open }) { [self] in
                        check(overlay.systemSelectedModule == .notes && controller.workspace == nil && !controller.isPresented,
                              "Return preserves the selected module and releases the projection surface")
                        check(!workspace.active && !workspace.hasLiveMediaResources && workspace.animationCountForVerification == 0,
                              "Closed workspace has no playback resources, menus or animations")
                        overlay.selectSystemModule(.projection)
                        wait("second projection", until: { controller.workspace?.active == true }) { [self] in
                            check(controller.model.drawing.strokes.count == 1 && !controller.model.backgroundEnabled,
                                  "Session drawing/background survive round trips without a new database")
                            let reopenedWorkspace = controller.workspace!
                            reopenedWorkspace.perform("clear")
                            (reopenedWorkspace.secondaryMenu as? ProjectionClearConfirmation)?.perform("confirm")
                            check(controller.model.drawing.strokes.isEmpty && controller.model.media.isEmpty
                                  && !controller.model.backgroundEnabled && !reopenedWorkspace.hasLiveMediaResources,
                                  "Clear all removes content and playback while retaining background preferences")
                            check(overlay.eventLog.events.first?.kind == .projectionAction
                                  && overlay.eventLog.events.first?.metadata == ["action": "cleared"],
                                  "Clear all emits only the safe Projection action category")
                            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration),
                                  "Summon hotkey also returns from projection")
                            wait("hotkey return", until: { self.overlay.systemPhase == .open }) { [self] in
                                overlay.selectSystemModule(.projection)
                                wait("external presentation setup", until: { controller.workspace?.active == true }) { [self] in
                                    var externalCount = 0
                                    overlay.closeForExternalPresentation {
                                        check(controller.workspace == nil && overlay.systemPhase == .closed,
                                              "External updater UI waits until the projection window is released")
                                        externalCount += 1
                                    }
                                    check(externalCount == 0, "External UI cannot bypass the workspace closing fade")
                                    wait("external presentation", until: { externalCount == 1 }) { [self] in
                                        check(!overlay.isProjectionActive, "External presentation does not reopen the HUD behind it")
                                        _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration)
                                        wait("shutdown setup", until: { self.overlay.systemPhase == .open }) { [self] in
                                            overlay.selectSystemModule(.projection)
                                            overlay.closeForExternalPresentation { externalCount += 1 }
                                            overlay.forceCloseSystemOverlay()
                                            DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [self] in
                                                check(!overlay.isProjectionActive && overlay.systemPhase == .closed && controller.workspace == nil,
                                                      "Sleep/shutdown cancels a pending handoff without resurrecting either window")
                                                check(externalCount == 1, "Shutdown also cancels deferred external UI")
                                                check(overlay.notesForVerification.isEmpty && overlay.isIdleForUpdate,
                                                      "Projection never writes notes and returns to idle")
                                                cleanup(); print("PASS: \(count) isolated Projection HUD assertions")
                                                NSApp.terminate(nil)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
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
        func cleanup() { done = true; if let monitor { NSEvent.removeMonitor(monitor) }; overlay.systemPointerLocationProviderForVerification = nil }
        func fail(_ message: String) -> Never {
            fputs("FAIL: Projection \(count): \(message)\n", stderr)
            cleanup(); overlay.forceCloseSystemOverlay(); preconditionFailure(message)
        }
    }
}
