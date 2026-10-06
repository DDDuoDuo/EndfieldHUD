import AppKit

/// Direct native events in a diagnostic HUD with temporary stores. No user
/// map, event log, pointer location or running application is modified.
enum MapInteractionHUDVerification {
    private static var session: Session?
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        let value = Session(overlay: overlay); session = value; value.start()
    }
    private final class Session {
        let overlay: OverlayController
        var view: SystemHUDView!
        var count = 0
        var inputMonitor: Any?
        var done = false
        init(overlay: OverlayController) { self.overlay = overlay }
        func start() {
            inputMonitor = NSEvent.addLocalMonitorForEvents(matching: [
                .mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate,
                .leftMouseDown, .leftMouseUp, .leftMouseDragged,
                .rightMouseDown, .rightMouseUp, .rightMouseDragged,
                .scrollWheel, .magnify, .keyDown, .keyUp, .flagsChanged
            ]) { _ in nil }
            let frame = NSScreen.main?.frame ?? CGRect(x: 0, y: 0, width: 1280, height: 800)
            overlay.systemPointerLocationProviderForVerification = { CGPoint(x: frame.midX, y: frame.midY) }
            var config = AppConfiguration.defaults
            config.closeOnFocusLost = false; config.language = .english
            overlay.settingsController!.update { $0 = config }
            overlay.initialModuleRequest = .map
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: config), "The isolated Map HUD opens")
            wait("Map deployment", timeout: 15, until: { [self] in
                overlay.systemPhase == .open && !overlay.isSwitchingSystemModule
            }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first
                check(view != nil && view.sourceWatchForVerification != nil, "The original source shell hosts Map")
                check(view.reportGeometryMatchesSelectionForVerification, "Map projection and native input agree")
                check(view.worldMapForVerification.pins.isEmpty, "Map fixture starts without user pins")
                let viewport = view.worldMapForVerification.viewport
                let tokyo = WorldMapGeometry.screen(x: (139.6917 + 180) / 360, y: (90 - 35.6895) / 180, viewport: viewport)
                wait("center-only map raster", until: { [self] in view.worldMapForVerification.isRasterSettledForVerification }) { [self] in
                    let raster = view.worldMapForVerification.layer.sublayers!.first { $0.name == "map.raster" }!
                    let detail = raster.sublayers!.first { $0.name == "map.raster.detail" }!
                    let image = detail.contents as AnyObject?
                    send(.mouseMoved, local: tokyo)
                    check(view.worldMapForVerification.viewport == viewport && detail.contents as AnyObject? === image,
                          "Moving over another country preserves the camera and center-highlight image")
                    check(raster.sublayers?.count == 4 && raster.sublayers?.contains { $0.name?.hasPrefix("map.highlight") == true } == false,
                          "The map retains only its original four raster layers without pointer highlight plates")
                    click(local: tokyo)
                    let moved = view.worldMapForVerification.viewport
                    // At 3×, Tokyo lies above the existing camera's polar
                    // margin. Keep that boundary so the map never exposes a
                    // rectangular edge while still centering its longitude.
                    let expectedY = max(1 / viewport.zoom, (90 - 35.6895) / 180)
                    check(abs(moved.zoom - viewport.zoom) < 0.000001
                          && abs(moved.centerX - (139.6917 + 180) / 360) < 0.0001
                          && abs(moved.centerY - expectedY) < 0.0001,
                          "Projected terrain click recenters within the polar margin without changing zoom (\(moved))")
                    check(overlay.eventLog.events.filter { $0.kind == .mapRecentered }.count == 1,
                          "A committed recenter records one location-free event")
                    wait("recenter raster", until: { [self] in view.worldMapForVerification.isRasterSettledForVerification }) { [self] in
                        checkPins()
                    }
                }
            }
        }
        func checkPins() {
            let map = view.worldMapForVerification
            let point = WorldMapGeometry.center
            let gauge = view.accountGaugeForVerification, date = Date()
            gauge.update(value: "42 / 360", accessibilityLabel: "Fixture sanity", visible: true, accent: .systemYellow, scale: 2,
                sanity: HypergryphSanityPresentation(game: .endfield, current: 42, maximum: 360, observedAt: date,
                    nextRecoveryAt: date.addingTimeInterval(49), fullRecoveryAt: date.addingTimeInterval(136993),
                    isRefreshing: false, refreshAvailable: true), at: date)
            gauge.perform("toggle")
            let beforeMenuCamera = map.viewport
            send(.rightMouseDown, local: point)
            check(gauge.isPopoverOpen && map.pins.isEmpty, "Recovery menu blocks right-click pin mutations behind it")
            guard let wheel = CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 1, wheel1: -40, wheel2: 0, wheel3: 0),
                  let initial = NSEvent(cgEvent: wheel) else { fail("Could not create recovery-menu scroll fixture") }
            let target = view.convert(view.modulePointForVerification(point), to: nil), observed = initial.locationInWindow
            wheel.location = CGPoint(x: wheel.location.x + target.x - observed.x, y: wheel.location.y - target.y + observed.y)
            guard let scroll = NSEvent(cgEvent: wheel) else { fail("Could not wrap recovery-menu scroll") }
            view.scrollWheel(with: scroll)
            check(map.viewport == beforeMenuCamera && gauge.isPopoverOpen, "Recovery menu consumes scrolling without zooming the map")
            let key = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
                windowNumber: view.window!.windowNumber, context: nil, characters: "=", charactersIgnoringModifiers: "=", isARepeat: false, keyCode: 24)!
            view.keyDown(with: key)
            check(map.viewport == beforeMenuCamera, "Recovery menu blocks keyboard zoom behind it")
            gauge.dismiss(animated: false)
            view.accountControllerForVerification.perform(.selectHeaderMode(.workMode), window: nil)
            send(.rightMouseDown, local: point); send(.rightMouseUp, local: point)
            check(map.pins.count == 1 && map.pins[0].style == .yellow, "A native right-click creates the default yellow marker")
            let pin = map.pins[0], viewport = map.viewport
            for style: MapPinStyle in [.green, .player, .yellow] {
                click(local: WorldMapGeometry.screen(x: pin.x, y: pin.y, viewport: map.viewport))
                check(map.pins.count == 1 && map.pins[0].id == pin.id && map.pins[0].style == style,
                      "Pin click cycles to \(style.rawValue) without creating another pin")
                check(map.viewport == viewport && !map.showsPinCoordinates,
                      "Cycling a marker keeps camera/zoom and hides coordinates")
            }
            check(overlay.eventLog.events.filter { $0.kind == .mapPinStyleChanged }.count == 3,
                  "Each committed style change records one bounded event")
            check(WorldMapPinArtwork.playerGlyph != nil && WorldMapPinArtwork.playerHalo != nil && WorldMapPinArtwork.playerBeam != nil,
                  "All three original player textures are present in the curated bundle")
            send(.rightMouseDown, local: point); send(.rightMouseUp, local: point)
            check(map.pins.isEmpty, "Right-clicking the same marker still removes it")
            let start = CGPoint(x: 195, y: 210), end = CGPoint(x: 215, y: 224)
            let expected = WorldMapGeometry.panned(map.viewport, delta: CGPoint(x: end.x - start.x, y: end.y - start.y))
            send(.leftMouseDown, local: start); send(.leftMouseDragged, local: end); send(.leftMouseUp, local: end)
            check(abs(map.viewport.centerX - expected.centerX) < 0.0001 && abs(map.viewport.centerY - expected.centerY) < 0.0001,
                  "Dragging pans once without a second recenter on release")
            check(overlay.eventLog.events.filter { $0.kind == .mapRecentered }.count == 1,
                  "Drag frames and release do not create click-recenter events")
            overlay.selectSystemModule(.notes)
            wait("Map departure", until: { [self] in !overlay.isSwitchingSystemModule && overlay.systemSelectedModule == .notes }) { [self] in
                check(!map.isDragging, "Leaving Map cancels active camera input")
                checkNotesScroll()
                overlay.closeSystemOverlay()
                wait("HUD close", until: { [self] in overlay.systemPhase == .closed }) { [self] in
                    check(overlay.lastClosedAnimationCount == 0 && !overlay.lastClosedSourceTimerActive,
                          "No map or source animation survives closing")
                    cleanup()
                    print("PASS: \(count) Map/Notes native HUD assertions; center-only map/recenter, pin cycle, original player textures, drag/delete, private events, continuous note scrolling and clean close")
                    MapInteractionHUDVerification.session = nil; NSApp.terminate(nil)
                }
            }
        }
        func checkNotesScroll() {
            let text = CanvasNote(kind: .text, text: (1...70).map { "Line \($0): bounded scrolling fixture" }.joined(separator: "\n"),
                                  x: 150, y: 120, width: 240, height: 150)
            let todo = CanvasNote(kind: .todo, items: (1...20).map { NoteChecklistItem(text: "Wrapped task \($0) with enough text to continue on another line") },
                                  x: 420, y: 120, width: 220, height: 150)
            do { try view.installNoteForVerification(text); try view.installNoteForVerification(todo) }
            catch { fail("Could not insert isolated Notes fixtures: \(error)") }
            for note in [text, todo] {
                let point = view.projectNotesPointForVerification(CGPoint(x: note.x + 70, y: note.y + 60))
                check(view.bounds.contains(point), "A note scroll target fits the projected workspace")
                let before = view.noteScrollOffsetForVerification(note.id)
                guard let wheel = CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 1, wheel1: -1, wheel2: 0, wheel3: 0),
                      let initial = NSEvent(cgEvent: wheel) else { fail("Could not create note scroll fixture") }
                let target = view.convert(point, to: nil), observed = initial.locationInWindow
                wheel.location = CGPoint(x: wheel.location.x + target.x - observed.x,
                                         y: wheel.location.y - target.y + observed.y)
                guard let event = NSEvent(cgEvent: wheel) else { fail("Could not wrap note wheel event") }
                view.scrollWheel(with: event)
                let distance = view.noteScrollOffsetForVerification(note.id) - before
                check(distance > 0 && distance < 10,
                      "One precise native scroll step moves \(note.kind.rawValue) continuously instead of jumping a row")
            }
            check(overlay.notesForVerification.first { $0.id == text.id }?.text == text.text
                  && overlay.notesForVerification.first { $0.id == todo.id }?.items == todo.items,
                  "Scrolling preserves stored text and task content")
        }
        func click(local: CGPoint) { send(.leftMouseDown, local: local); send(.leftMouseUp, local: local) }
        func send(_ type: NSEvent.EventType, local: CGPoint) {
            let point = view.modulePointForVerification(local)
            guard let window = view.window, view.bounds.contains(point),
                  let event = NSEvent.mouseEvent(with: type, location: view.convert(point, to: nil), modifierFlags: [],
                    timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                    context: nil, eventNumber: 1, clickCount: 1, pressure: 1) else { fail("Invalid projected fixture event") }
            window.ignoresMouseEvents = false
            switch type {
            case .mouseMoved: view.mouseMoved(with: event)
            case .leftMouseDown: view.mouseDown(with: event)
            case .leftMouseDragged: view.mouseDragged(with: event)
            case .leftMouseUp: view.mouseUp(with: event)
            case .rightMouseDown: view.rightMouseDown(with: event)
            case .rightMouseUp: view.rightMouseUp(with: event)
            default: break
            }
            window.ignoresMouseEvents = true
        }
        func wait(_ label: String, timeout: TimeInterval = 10, until condition: @escaping () -> Bool, then completion: @escaping () -> Void) {
            let deadline = ProcessInfo.processInfo.systemUptime + timeout
            func poll() {
                guard !done else { return }
                NSApp.windows.compactMap { $0 as? NSPanel }.forEach { $0.ignoresMouseEvents = true }
                if condition() { completion(); return }
                if ProcessInfo.processInfo.systemUptime > deadline { fail("Timed out: " + label) }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.03, execute: poll)
        }
        func check(_ result: Bool, _ message: String) { count += 1; if !result { fail(message) } }
        func cleanup() {
            done = true
            if let inputMonitor { NSEvent.removeMonitor(inputMonitor) }; inputMonitor = nil
            overlay.systemPointerLocationProviderForVerification = nil
        }
        func fail(_ message: String) -> Never {
            fputs("FAIL: Map HUD assertion \(count): \(message)\n", stderr); fflush(stderr)
            cleanup(); overlay.forceCloseSystemOverlay(); preconditionFailure(message)
        }
    }
}
