import AppKit

/// Drives the redesigned Map module through the host's native pointer, wheel
/// and key paths in a diagnostic HUD with temporary stores. No user map, note,
/// event log, pointer location or running application is modified.
enum MapInteractionHUDVerification {
    private static var session: Session?
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        let value = Session(overlay: overlay); session = value; value.start()
    }
    private final class Session {
        let overlay: OverlayController
        var view: SystemHUDView!
        var map: WorldMapCanvas { view.worldMapForVerification }
        var config = AppConfiguration.defaults
        var fixtures: [CanvasNote] = []
        var count = 0
        var inputMonitor: Any?
        var done = false
        /// Content-box px: the crosshair, bare terrain, and a point between the
        /// circle and the shell's ring (the HUD's own empty space).
        let centre = WorldMapCanvas.mapCenter, empty = CGPoint(x: 200, y: 250)
        let corner = CGPoint(x: WorldMapCanvas.mapCenter.x - 283, y: WorldMapCanvas.mapCenter.y - 283)
        /// The zoom-in button, inside the circle.
        var column: CGPoint {
            let rect = map.accessibilityItems.first { $0.id == "map.zoomIn" }?.rect ?? .null
            return CGPoint(x: rect.midX, y: rect.midY)
        }
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
            self.config = config
            overlay.settingsController!.update { $0 = config }
            check(WorldMapStore.applicationDirectory().path.hasPrefix(FileManager.default.temporaryDirectory.path),
                  "The map archive is the diagnostic temporary one")
            open("Map deployment") { [self] in
                check(view.sourceWatchForVerification != nil, "The original source shell hosts Map")
                check(view.reportGeometryMatchesSelectionForVerification, "The Map design box projection and native input agree")
                check(view.designCanvasForVerification(.map) === map && map.root.bounds.size == HUDMetric.box,
                      "Map is the redesigned centre module in the 590 x 510 content box")
                func layers(_ layer: CALayer) -> [CALayer] { [layer] + (layer.sublayers ?? []).flatMap(layers) }
                check(map.surface.layer.mask?.contents != nil && map.hitBounds.contains(CGPoint(x: centre.x, y: WorldMapCanvas.circleFrame.maxY - 1))
                      && abs(WorldMapSurface.radius - 216 * 1.14 / HUDDesignGeometry.baseUnit) < 1e-6,
                      "The round map has v1.2.0's shipped size, fades at its edge through an image mask and takes input down to its bottom")
                check(WorldMapCanvas.onMap(CGPoint(x: 295, y: 40)) && layers(map.surface.layer).contains { $0.name == "map.scrim" && !$0.isHidden && $0.contents != nil },
                      "The header lies over the map's top with a cached dark veil behind it")
                let inside = map.accessibilityItems.filter { ["map.zoomIn", "map.zoomOut", "map.reset", "map.addPin"].contains($0.id) }
                check(inside.count == 4 && inside.allSatisfy { item in
                          [CGPoint(x: item.rect.minX, y: item.rect.minY), CGPoint(x: item.rect.maxX, y: item.rect.maxY)].allSatisfy(WorldMapCanvas.onMap) },
                      "The zoom column and the centre placement sit inside the circle, as in v1.2.0")
                check(WorldMapPinArtwork.playerGlyph != nil && WorldMapPinArtwork.playerHalo != nil && WorldMapPinArtwork.playerBeam != nil,
                      "All three original player textures are present in the curated bundle")
                check(!layers(map.root).contains { ["map.label", "map.labels", "map.chip"].contains($0.name ?? "") },
                      "The map names no countries: no label or region chip layers")
                check(map.pins.isEmpty && stored().pins.isEmpty && overlay.notesForVerification.isEmpty, "Map and Notes fixtures start empty")
                check(same(map.viewport, WorldMapSurface.constrained(WorldMapViewport())), "A new map opens on the default camera")
                let ids = view.designControlIDsForVerification(.map)
                check(["map.zoomIn", "map.zoomOut", "map.reset", "map.addPin"].allSatisfy(ids.contains) && !ids.contains("map.removePin"),
                      "The zoom column and the centre placement are live without a selected pin")
                settled("initial map raster") { [self] in checkOcclusion() }
            }
        }
        /// v1.2.0's map occlusion: the bottom plates are cut out of the map and
        /// own their visible faces; their transparent parts stay map.
        func checkOcclusion() {
            guard let source = view.sourceWatchForVerification, let frame = source.currentFrameForVerification,
                  let camera = source.currentCameraForVerification else { fail("Map occlusion requires the live source frame") }
            check(source.desktopMapOcclusionEnabled && view.mapOcclusionHidesForVerification(view.designBoxViewPointForVerification(centre)) == false,
                  "Map occlusion is on and keeps the map at its centre")
            // The plates' raycast quads, sampled without occlusion, where they lie on the map.
            let targets: [HUDNavigationTarget] = [.module(.storage), .module(.activityMonitor)]
            source.desktopMapOcclusionEnabled = false
            var samples: [CGPoint] = []
            for hit in frame.hits {
                for y in 1...9 { for x in 1...9 {
                    let p = hit.rect.origin + hit.rect.size * SIMD2(Double(x) / 10, Double(y) / 10)
                    guard let point = camera.camera.project(SIMD3(p.x, p.y, 0), world: hit.world, viewport: source.bounds)?.point,
                          let target = source.navigationTarget(at: point), targets.contains(target),
                          let box = view.designBoxPointForVerification(point), WorldMapCanvas.onMap(box) else { continue }
                    samples.append(point)
                } }
            }
            source.desktopMapOcclusionEnabled = true
            let faces = samples.filter { source.bottomButtonContains($0) }, gaps = samples.filter { !source.bottomButtonContains($0) }
            check(!faces.isEmpty, "Storage and Activity Monitor overlap the map")
            let before = map.viewport, pins = map.pins
            check(faces.allSatisfy { view.hitTest(view.convert($0, to: view.superview)) === source }
                  && faces.contains { view.mapOcclusionHidesForVerification($0) == true },
                  "Presses on the bottom plates over the map reach the source shell, and the plates are cut out of the map")
            check(gaps.allSatisfy { view.hitTest(view.convert($0, to: view.superview)) === view && view.mapOcclusionHidesForVerification($0) == false },
                  "The plates' transparent quad parts stay map: drawn and pressable")
            check(same(map.viewport, before) && map.pins == pins, "Probing the plates leaves the map unchanged")
            checkRecenter()
        }
        func checkRecenter() {
            let before = map.viewport, x = (139.6917 + 180) / 360, y = (90 - 35.6895) / 180, tokyo = box(x, y)
            check(WorldMapCanvas.onMap(tokyo), "Tokyo lies on the default map")
            check(!WorldMapCanvas.onMap(corner) && map.hitBounds.contains(corner), "The probe corner lies outside the circle, in its square")
            check(map.cursor(at: tokyo) == .openHand && map.cursor(at: CGPoint(x: 295, y: 40)) == .openHand && map.cursor(at: column) == nil
                  && map.cursor(at: corner) == nil, "Terrain, also under the header, shows an open hand; the zoom column and the circle's outside keep the HUD pointer")
            click(corner); rightClick(corner)
            check(same(map.viewport, before) && map.pins.isEmpty && events(.mapRecentered) == 0,
                  "Presses outside the circle neither recenter nor place a marker")
            click(tokyo)
            check(same(map.viewport, WorldMapSurface.constrained(WorldMapViewport(centerX: x, centerY: y, zoom: before.zoom))),
                  "A terrain click recenters on Tokyo without changing zoom (\(map.viewport))")
            check(events(.mapRecentered) == 1 && same(stored().viewport, map.viewport),
                  "A committed recenter is saved and records one location-free event")
            settled("recenter raster") { [self] in checkZoomControls() }
        }
        func checkZoomControls() {
            let start = map.viewport
            perform("map.zoomIn")
            check(same(map.viewport, WorldMapSurface.zoomed(start, at: WorldMapSurface.center, factor: 1.5)) && same(stored().viewport, map.viewport),
                  "Zoom in steps x1.5 about the crosshair and saves the camera")
            perform("map.zoomOut")
            check(same(map.viewport, start) && same(stored().viewport, start), "Zoom out returns to the previous camera and saves it")
            for _ in 0..<4 { perform("map.zoomOut") }
            check(map.viewport.zoom == WorldMapViewport.minZoom && enabled("map.zoomOut") == false && enabled("map.zoomIn") == true,
                  "Zoom out stops at the whole world and disables its button")
            for _ in 0..<12 { perform("map.zoomIn") }
            check(map.viewport.zoom == WorldMapViewport.maxZoom && enabled("map.zoomIn") == false && enabled("map.zoomOut") == true,
                  "Zoom in stops at street level and disables its button")
            perform("map.reset")
            check(same(map.viewport, WorldMapSurface.constrained(WorldMapViewport())) && same(stored().viewport, map.viewport)
                  && enabled("map.zoomIn") == true && enabled("map.zoomOut") == true, "Reset view restores and saves the default camera")
            settled("reset raster") { [self] in checkWheel() }
        }
        func checkWheel() {
            let point = CGPoint(x: 400, y: 250), before = map.viewport
            scroll(30, at: view.designBoxViewPointForVerification(corner))
            check(same(map.viewport, before) && !map.isInputLocked, "The wheel outside the circle does not zoom the map")
            let event = scroll(30, at: view.designBoxViewPointForVerification(point))
            let notches = Double(event.scrollingDeltaY) / (event.hasPreciseScrollingDeltas ? 10 : 1)
            check(notches > 0 && same(map.viewport, WorldMapSurface.zoomed(before, at: local(point), factor: pow(1.12, min(6, notches)))),
                  "A wheel step zooms in x1.12 per notch about the pointer")
            check(map.isInputLocked && same(stored().viewport, before), "The wheel gesture holds its camera write until input pauses")
            let zoom = map.viewport.zoom
            scroll(-30, at: view.designBoxViewPointForVerification(point))
            check(map.viewport.zoom < zoom, "The opposite wheel direction zooms back out")
            wait("wheel gesture release", until: { [self] in !map.isInputLocked }) { [self] in
                check(same(stored().viewport, map.viewport), "The wheel gesture is saved once input pauses")
                settled("wheel raster") { [self] in checkDrag() }
            }
        }
        func checkDrag() {
            let start = CGPoint(x: 300, y: 300), middle = CGPoint(x: 320, y: 315), end = CGPoint(x: 340, y: 330)
            let before = map.viewport, recentered = events(.mapRecentered)
            check(map.cursor(at: start) == .openHand && !map.isDragging, "An open hand precedes the press")
            send(.leftMouseDown, start); send(.leftMouseDragged, middle)
            check(map.isDragging && map.cursor(at: middle) == .closedHand
                  && same(map.viewport, WorldMapSurface.panned(before, by: CGPoint(x: 20, y: 15))),
                  "Dragging pans the camera live under a closed hand")
            send(.leftMouseDragged, end); send(.leftMouseUp, end)
            check(!map.isDragging && map.cursor(at: end) == .openHand
                  && same(map.viewport, WorldMapSurface.panned(before, by: CGPoint(x: 40, y: 30))),
                  "Release ends the pan under the pointer and restores the open hand")
            check(events(.mapRecentered) == recentered && same(stored().viewport, map.viewport),
                  "The pan is saved once without a click recenter on release")
            settled("pan raster") { [self] in checkKeyboard() }
        }
        func checkKeyboard() {
            let before = map.viewport
            key(124, "\u{F703}")
            let east = WorldMapSurface.panned(before, by: CGPoint(x: -40, y: 0))
            check(same(map.viewport, east) && map.isInputLocked, "Right arrow pans east and holds the camera write")
            key(126, "\u{F700}")
            let north = WorldMapSurface.panned(east, by: CGPoint(x: 0, y: 40))
            check(same(map.viewport, north), "Up arrow pans north")
            key(24, "=")
            let closer = WorldMapSurface.zoomed(north, at: WorldMapSurface.center, factor: 1.5)
            check(same(map.viewport, closer) && !map.isInputLocked && same(stored().viewport, closer),
                  "+ commits the pending pan and zooms in one saved step")
            key(27, "-"); key(123, "\u{F702}"); key(125, "\u{F701}")
            // The same steps in reverse; pans clamp at the poles, so the
            // expectation follows the surface's own rules rather than `before`.
            let back = WorldMapSurface.panned(WorldMapSurface.panned(
                WorldMapSurface.zoomed(closer, at: WorldMapSurface.center, factor: 1 / 1.5), by: CGPoint(x: 40, y: 0)), by: CGPoint(x: 0, y: -40))
            check(same(map.viewport, back), "- and the opposite arrows reverse the zoom and pans")
            wait("key gesture release", until: { [self] in !map.isInputLocked }) { [self] in
                check(same(stored().viewport, back), "Arrow panning is saved once input pauses")
                settled("keyboard raster") { [self] in checkRecoveryMenu() }
            }
        }
        func checkRecoveryMenu() {
            let gauge = view.accountGaugeForVerification, date = Date(), camera = map.viewport, recentered = events(.mapRecentered)
            gauge.update(value: "42 / 360", accessibilityLabel: "Fixture sanity", visible: true, accent: .systemYellow, scale: 2,
                sanity: HypergryphSanityPresentation(game: .endfield, current: 42, maximum: 360, observedAt: date,
                    nextRecoveryAt: date.addingTimeInterval(49), fullRecoveryAt: date.addingTimeInterval(136993),
                    isRefreshing: false, refreshAvailable: true), at: date)
            let wallet = view.accountGaugePointForVerification(CGPoint(x: 90, y: 20))
            post(.leftMouseDown, at: wallet); post(.leftMouseUp, at: wallet)
            check(gauge.isPopoverOpen, "A native press on the sanity wallet opens its recovery menu")
            rightClick(centre)
            scroll(-40, at: view.designBoxViewPointForVerification(centre))
            key(24, "=")
            check(gauge.isPopoverOpen && map.pins.isEmpty && same(map.viewport, camera) && !map.isInputLocked,
                  "The recovery menu blocks right-click pins, wheel zoom and key zoom behind it")
            click(centre)
            check(!gauge.isPopoverOpen && same(map.viewport, camera) && events(.mapRecentered) == recentered,
                  "The first press outside the recovery menu only closes it")
            view.accountControllerForVerification.perform(.selectHeaderMode(.workMode), window: nil)
            checkPins()
        }
        func checkPins() {
            rightClick(centre)
            guard map.pins.count == 1, let pin = map.pins.first else { fail("A native right-click places one marker") }
            let world = WorldMapSurface.world(at: local(centre), camera: map.viewport)
            check(pin.style == .yellow && map.showsPinCoordinates
                  && abs(pin.x - WorldMapStore.wrappedX(world.x)) < 1e-5 && abs(pin.y - world.y) < 1e-5,
                  "A native right-click places a selected yellow marker under the pointer")
            let ids = view.designControlIDsForVerification(.map)
            check(ids.contains("map.pin:" + pin.id.uuidString) && ids.contains("map.removePin") && stored().pins == map.pins,
                  "The saved marker and its remove action are exposed")
            rightClick(column)
            check(map.pins.count == 1, "Right-clicking the zoom column places no marker")
            let camera = map.viewport
            func marker(_ name: String) -> CALayer? {
                func layers(_ layer: CALayer) -> [CALayer] { [layer] + (layer.sublayers ?? []).flatMap(layers) }
                return layers(map.surface.layer).first { $0.name == "map.pin." + pin.id.uuidString }?.sublayers?.first { $0.name == name }
            }
            for style: MapPinStyle in [.green, .player, .yellow] {
                click(box(pin.x, pin.y))
                check(map.pins.count == 1 && map.pins[0].id == pin.id && map.pins[0].style == style && map.showsPinCoordinates
                      && same(map.viewport, camera), "Clicking the selected marker cycles it to \(style.rawValue) and keeps the camera")
                check((marker("map.pin.player")?.isHidden == false) == (style == .player) && (marker("map.pin.ring")?.isHidden == true) == (style == .player)
                      && (marker("map.pin.ring") as? CAShapeLayer)?.strokeColor == WorldMapPinArtwork.color(for: style).cgColor,
                      "The \(style.rawValue) marker is drawn with v1.2.0's artwork")
            }
            check(events(.mapPinStyleChanged) == 3 && stored().pins == map.pins, "Each style change is saved and records one event")
            click(empty)
            check(!map.showsPinCoordinates && same(map.viewport, camera) && events(.mapRecentered) == 1
                  && !view.designControlIDsForVerification(.map).contains("map.removePin"),
                  "Clicking the map with a selection deselects without recentering")
            click(box(pin.x, pin.y))
            check(map.showsPinCoordinates && map.pins[0].style == .yellow && events(.mapPinStyleChanged) == 3,
                  "Clicking an unselected marker selects it without cycling")
            key(51, "\u{7F}")
            check(map.pins.isEmpty && stored().pins.isEmpty && view.designContext.overlay.toastState.current?.title == "PIN DELETED",
                  "Delete removes the selected marker with its result toast")
            rightClick(centre); rightClick(centre)
            check(map.pins.isEmpty && stored().pins.isEmpty, "Right-clicking a marker removes it")
            perform("map.addPin")
            guard map.pins.count == 1, let placed = map.pins.first else { fail("The centre placement action adds one marker") }
            check(map.showsPinCoordinates && abs(placed.x - WorldMapStore.wrappedX(map.viewport.centerX)) < 1e-9
                  && abs(placed.y - map.viewport.centerY) < 1e-9, "The centre placement action adds a selected marker at the crosshair")
            perform("map.pin:" + placed.id.uuidString)
            check(map.pins.first?.style == .green && events(.mapPinStyleChanged) == 4, "The marker action cycles the selected marker")
            key(53, "\u{1B}")
            check(!map.showsPinCoordinates && overlay.systemPhase == .open, "Esc deselects the marker without closing the HUD")
            perform("map.pin:" + placed.id.uuidString)
            check(map.showsPinCoordinates && map.pins.first?.style == .green, "The marker action selects an unselected marker without cycling")
            perform("map.removePin")
            check(map.pins.isEmpty && !map.showsPinCoordinates && stored().pins.isEmpty, "The remove action deletes the selected marker")
            rightClick(centre)
            guard let kept = map.pins.first else { fail("A native right-click places the kept marker") }
            click(box(kept.x, kept.y))
            check(map.pins.count == 1 && map.pins[0].style == .green && stored().pins == map.pins && events(.mapPinStyleChanged) == 5,
                  "The kept marker is saved in its cycled style")
            checkNotes()
        }
        func checkNotes() {
            check(overlay.notesForVerification.isEmpty, "Map input never writes Notes")
            let targets = [CGPoint(x: 140, y: 300), CGPoint(x: 450, y: 300)]
            let text = CanvasNote(kind: .text, text: (1...70).map { "Line \($0): bounded scrolling fixture" }.joined(separator: "\n"),
                                  width: 150, height: 110, isPinned: true)
            let todo = CanvasNote(kind: .todo, items: (1...20).map { NoteChecklistItem(text: "Wrapped task \($0) with enough text to continue on another line") },
                                  width: 180, height: 110, isPinned: true)
            fixtures = [pinned(text, over: targets[0]), pinned(todo, over: targets[1])]
            check(overlay.visibleNotesForVerification.isSuperset(of: fixtures.map(\.id)), "Pinned note fixtures are shown over the map")
            let camera = map.viewport, pins = map.pins
            for (note, target) in zip(fixtures, targets) {
                let point = view.designBoxViewPointForVerification(target)
                check(view.notesWorkspacePointForVerification(point).map { view.notesCanvasForVerification.contains(noteID: note.id, point: $0) } == true,
                      "A pinned \(note.kind.rawValue) note covers its map point")
                let before = view.noteScrollOffsetForVerification(note.id)
                scroll(-1, at: point)
                let distance = view.noteScrollOffsetForVerification(note.id) - before
                check(distance > 0 && distance < 10, "One precise scroll step moves the pinned \(note.kind.rawValue) note continuously")
                rightClick(target)
                check(same(map.viewport, camera) && !map.isInputLocked && map.pins == pins,
                      "The pinned \(note.kind.rawValue) note takes wheel and right-click input before the map")
            }
            check(fixturesUnchanged, "Scrolling preserves stored note text and task content")
            checkDeparture()
        }
        func checkDeparture() {
            let start = CGPoint(x: 295, y: 150), end = CGPoint(x: 325, y: 170), recentered = events(.mapRecentered)
            send(.leftMouseDown, start); send(.leftMouseDragged, end)
            check(map.isDragging, "A pan is in progress before leaving Map")
            overlay.selectSystemModule(.notes)
            wait("Map departure", until: { [self] in overlay.systemSelectedModule == .notes && !overlay.isSwitchingSystemModule }) { [self] in
                post(.leftMouseUp, at: view.designBoxViewPointForVerification(end))
                check(!map.isDragging && !map.isActive && map.cursor(at: start) == nil && view.designControlIDsForVerification(.map).isEmpty,
                      "Leaving Map cancels the pan, its cursor and its controls")
                let saved = stored()
                check(same(saved.viewport, map.viewport) && events(.mapRecentered) == recentered,
                      "The interrupted pan is saved without a click recenter")
                close("HUD close") { [self] in
                    open("Map reopening") { [self] in
                        check(map.pins == saved.pins && same(map.viewport, saved.viewport) && !map.showsPinCoordinates,
                              "Reopening restores the saved marker and camera without a selection")
                        check(overlay.visibleNotesForVerification.isSuperset(of: fixtures.map(\.id)), "Pinned notes still cover the reopened map")
                        settled("reopened map raster") { [self] in
                            close("final HUD close") { [self] in
                                check(fixturesUnchanged, "Map input leaves the pinned note fixtures unchanged")
                                cleanup()
                                print("PASS: \(count) Map native HUD assertions; terrain recenter, zoom column and limits, wheel zoom and pan with grab cursor, keyboard camera, marker add/cycle/select/delete, recovery-menu and pinned-note occlusion, saved camera and markers after reopen and clean close")
                                MapInteractionHUDVerification.session = nil; NSApp.terminate(nil)
                            }
                        }
                    }
                }
            }
        }
        var fixturesUnchanged: Bool {
            fixtures.allSatisfy { note in
                overlay.notesForVerification.contains { $0.id == note.id && $0.text == note.text && $0.items == note.items && $0.isPinned }
            }
        }
        func pinned(_ note: CanvasNote, over target: CGPoint) -> CanvasNote {
            guard let local = view.notesWorkspacePointForVerification(view.designBoxViewPointForVerification(target)) else {
                fail("A map point has no Notes workspace projection")
            }
            var note = note
            note.x = Double(local.x) - note.width / 2; note.y = Double(local.y) - note.height / 2
            do { try view.installNoteForVerification(note) } catch { fail("Could not insert an isolated pinned note: \(error)") }
            return note
        }
        func open(_ label: String, then completion: @escaping () -> Void) {
            overlay.initialModuleRequest = .map
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: config), "The isolated Map HUD opens (\(label))")
            wait(label, timeout: 15, until: { [self] in
                overlay.systemPhase == .open && !overlay.isSwitchingSystemModule
                    && NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first?.worldMapForVerification.isActive == true
            }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first
                completion()
            }
        }
        func close(_ label: String, then completion: @escaping () -> Void) {
            overlay.closeSystemOverlay()
            wait(label, until: { [self] in overlay.systemPhase == .closed }) { [self] in
                check(overlay.lastClosedAnimationCount == 0 && !overlay.lastClosedSourceTimerActive,
                      "No map or source animation survives closing (\(label))")
                completion()
            }
        }
        func settled(_ label: String, then completion: @escaping () -> Void) {
            wait(label, until: { [self] in map.isRasterSettledForVerification }, then: completion)
        }
        /// The archive as a fresh launch reads it from disk.
        func stored() -> WorldMapStore {
            do { return try WorldMapStore(directory: WorldMapStore.applicationDirectory()) }
            catch { fail("Could not read the isolated map archive: \(error)") }
        }
        func same(_ a: WorldMapViewport, _ b: WorldMapViewport) -> Bool {
            abs(a.centerX - b.centerX) < 1e-5 && abs(a.centerY - b.centerY) < 1e-5 && abs(a.zoom - b.zoom) < 1e-5
        }
        /// A world point (0...1 on both axes) in content-box px under the current camera.
        func box(_ x: Double, _ y: Double) -> CGPoint {
            let point = WorldMapSurface.screen(x: x, y: y, camera: map.viewport)
            return CGPoint(x: point.x + WorldMapCanvas.mapFrame.minX, y: point.y + WorldMapCanvas.mapFrame.minY)
        }
        func local(_ box: CGPoint) -> CGPoint { CGPoint(x: box.x - WorldMapCanvas.mapFrame.minX, y: box.y - WorldMapCanvas.mapFrame.minY) }
        func perform(_ id: String) { view.performDesignActionForVerification(.map, id) }
        func enabled(_ id: String) -> Bool? { map.accessibilityItems.first { $0.id == id }?.enabled }
        func events(_ kind: SystemEventKind) -> Int { overlay.eventLog.events.filter { $0.kind == kind }.count }
        func click(_ box: CGPoint) { send(.leftMouseDown, box); send(.leftMouseUp, box) }
        func rightClick(_ box: CGPoint) { send(.rightMouseDown, box); send(.rightMouseUp, box) }
        func send(_ type: NSEvent.EventType, _ box: CGPoint) { post(type, at: view.designBoxViewPointForVerification(box)) }
        func post(_ type: NSEvent.EventType, at point: CGPoint) {
            guard let window = view.window, view.bounds.contains(point),
                  let event = NSEvent.mouseEvent(with: type, location: view.convert(point, to: nil), modifierFlags: [],
                    timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                    context: nil, eventNumber: 1, clickCount: 1, pressure: 1) else { fail("Invalid projected fixture event") }
            window.ignoresMouseEvents = false
            switch type {
            case .leftMouseDown: view.mouseDown(with: event)
            case .leftMouseDragged: view.mouseDragged(with: event)
            case .leftMouseUp: view.mouseUp(with: event)
            case .rightMouseDown: view.rightMouseDown(with: event)
            case .rightMouseUp: view.rightMouseUp(with: event)
            default: break
            }
            window.ignoresMouseEvents = true
        }
        @discardableResult func scroll(_ amount: Int32, at point: CGPoint) -> NSEvent {
            guard let wheel = CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 1, wheel1: amount, wheel2: 0, wheel3: 0),
                  let initial = NSEvent(cgEvent: wheel) else { fail("Could not create a scroll fixture") }
            let target = view.convert(point, to: nil), observed = initial.locationInWindow
            wheel.location = CGPoint(x: wheel.location.x + target.x - observed.x, y: wheel.location.y - target.y + observed.y)
            guard let event = NSEvent(cgEvent: wheel) else { fail("Could not wrap a scroll fixture") }
            view.scrollWheel(with: event)
            return event
        }
        func key(_ code: UInt16, _ text: String) {
            guard let event = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                    windowNumber: view.window!.windowNumber, context: nil, characters: text, charactersIgnoringModifiers: text,
                    isARepeat: false, keyCode: code) else { fail("Could not create a key fixture") }
            view.keyDown(with: event)
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
