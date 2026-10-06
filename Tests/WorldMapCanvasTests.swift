import AppKit
import ImageIO
import QuartzCore

enum WorldMapCanvasTests {
    static func run() -> Int {
        // This suite inspects model-layer animation registrations without a
        // presentation host. Keep their transaction open across raster waits;
        // otherwise Core Animation discards detached-tree tracks at commit.
        CATransaction.begin(); CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func near(_ a: Double, _ b: Double) -> Bool { abs(a - b) < 0.000_000_1 }
        func nearPoint(_ a: CGPoint?, _ b: CGPoint) -> Bool {
            guard let a else { return false }
            return near(a.x, b.x) && near(a.y, b.y)
        }
        func descendants(_ root: CALayer) -> [CALayer] {
            [root] + (root.sublayers ?? []).flatMap(descendants) + (root.mask.map(descendants) ?? [])
        }
        func marker(_ canvas: WorldMapCanvas, _ id: UUID) -> CALayer? {
            descendants(canvas.layer).first { $0.name == "map.pin." + id.uuidString }
        }
        func animatedLayers(_ canvas: WorldMapCanvas) -> [CALayer] {
            descendants(canvas.layer).filter { !($0.animationKeys() ?? []).isEmpty }
        }
        func waitForRaster(_ canvas: WorldMapCanvas) {
            let deadline = Date().addingTimeInterval(3)
            while Date() < deadline && !descendants(canvas.layer).contains(where: { $0.name == "map.raster.detail" && $0.contents != nil }) {
                _ = RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.01))
            }
            check(descendants(canvas.layer).contains { $0.name == "map.raster.detail" && $0.contents != nil },
                  "The bounded background painter publishes visible geography")
        }
        // Tiny triangular coastline used by the real retained canvas and painter.
        var bytes = Data("EHUDMAP1".utf8)
        func u32(_ value: UInt32) { for byte in 0..<4 { bytes.append(UInt8(truncatingIfNeeded: value >> (byte * 8))) } }
        func u16(_ value: UInt16) { bytes.append(UInt8(truncatingIfNeeded: value)); bytes.append(UInt8(value >> 8)) }
        u32(1); u32(0); u32(1); u32(0)
        bytes.append(1); u32(3)
        for point: (UInt16, UInt16) in [(16_000, 16_000), (48_000, 16_000), (32_000, 48_000)] { u16(point.0); u16(point.1) }
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("WorldMapCanvasTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: root) }
        let oldLanguage = L10n.language
        defer { L10n.language = oldLanguage }
        L10n.language = .english
        do {
            let terrain = try WorldMapTerrain(data: bytes)
            let store = try WorldMapStore(directory: root.appendingPathComponent("navigation"))
            let shenzhen = store.viewport
            let longitude = shenzhen.centerX * 360 - 180
            let latitude = 90 - shenzhen.centerY * 180
            check((113...115).contains(longitude) && (22...23).contains(latitude) && shenzhen.zoom == 3,
                  "First use starts near Shenzhen at the requested 3× zoom")
            try store.setViewport(WorldMapViewport(centerX: 0.5, centerY: 0.5, zoom: 2.1))
            let canvas = WorldMapCanvas(store: store, terrain: terrain, loadsTerrain: false, reduceMotion: { false }, ambient: { true })
            var changes = 0
            canvas.onChange = { changes += 1 }
            let cyan = NSColor(srgbRed: 0.15, green: 0.83, blue: 0.72, alpha: 1)
            let style = HUDModuleContentStyle(dark: true, accent: cyan, contentsScale: 2)
            let retained = canvas.makeContent(for: .map, style: style)
            check(retained.bounds.size == WorldMapGeometry.size && retained.mask != nil, "The map stays within the central HUD's circular content layer")
            let edgeMask = retained.mask as? CAGradientLayer
            check(edgeMask?.type == .radial && edgeMask?.colors?.count == 3
                  && near(edgeMask?.locations?[1].doubleValue ?? 0, 1 - WorldMapGeometry.edgeFeatherWidth / WorldMapGeometry.radius),
                  "One retained radial mask feathers the circular edge without allocating terrain-sized blur images")
            check(animatedLayers(canvas).isEmpty, "An inactive map starts without animation")
            check(!canvas.containsMapPoint(CGPoint(x: -1, y: 220)) && canvas.containsMapPoint(CGPoint(x: 220, y: 370)),
                  "The complete visible map circle accepts gestures while exterior points remain excluded")
            check(!canvas.mouseDown(at: CGPoint(x: CGFloat.nan, y: 220)) && !canvas.rightMouseDown(at: CGPoint(x: 220, y: CGFloat.infinity)),
                  "Invalid pointer coordinates never start a drag or create a pin")
            check(!canvas.accessibleActions.first(where: { $0.id == "map:zoomOut" })!.enabled
                  && canvas.accessibleActions.first(where: { $0.id == "map:zoomIn" })!.enabled,
                  "The 2.1× world view disables further zooming out while leaving zoom in available")
            check(canvas.accessibleActions.first(where: { $0.id == "map:reset" })?.label == "Reset zoom to 3×",
                  "The reset control describes the new 3× destination")
            let zoomButton = canvas.accessibleActions.first { $0.id == "map:zoomIn" }!.rect
            let buttonCenter = CGPoint(x: zoomButton.midX, y: zoomButton.midY)
            check(canvas.rightMouseDown(at: buttonCenter) && canvas.pins.isEmpty, "Right-clicking a HUD map control does not put a pin underneath it")
            check(canvas.mouseDown(at: buttonCenter) && !canvas.isDragging && store.viewport.zoom > 2.1,
                  "Left-clicking zoom changes and persists scale without beginning map panning")
            let locationBeforeReset = canvas.viewport
            canvas.perform(actionID: "map:reset")
            let resetCamera = canvas.viewport
            check(resetCamera.centerX == locationBeforeReset.centerX && resetCamera.centerY == locationBeforeReset.centerY
                  && resetCamera.zoom == 3 && store.viewport == resetCamera && resetCamera != shenzhen,
                  "Reset restores 3× at the current location without recentering Shenzhen")
            check(canvas.accessibleActions.first(where: { $0.id == "map:zoomIn" })!.enabled
                  && canvas.accessibleActions.first(where: { $0.id == "map:zoomOut" })!.enabled,
                  "Shenzhen starts between the zoom limits with both zoom controls available")

            canvas.zoom(at: WorldMapGeometry.center, factor: 100)
            check(canvas.viewport.zoom == 128 && !canvas.accessibleActions.first(where: { $0.id == "map:zoomIn" })!.enabled
                  && canvas.accessibleActions.first(where: { $0.id == "map:zoomOut" })!.enabled,
                  "Street-level zoom clamps at 128× and updates the control enabled states")
            canvas.perform(actionID: "map:zoomIn")
            check(canvas.viewport.zoom == 128 && store.viewport.zoom == 128,
                  "Accessible zoom-in at the upper limit remains bounded and saves the final camera")
            canvas.zoom(at: WorldMapGeometry.center, factor: 0.001)
            check(canvas.viewport.zoom == 2.1 && !canvas.accessibleActions.first(where: { $0.id == "map:zoomOut" })!.enabled,
                  "Zooming out cannot reveal beyond the 2.1× minimum world view")
            canvas.perform(actionID: "map:zoomOut")
            check(canvas.viewport.zoom == 2.1 && store.viewport.zoom == 2.1,
                  "Accessible zoom-out at the lower limit preserves the bounded camera")
            canvas.perform(actionID: "map:reset")
            check(canvas.viewport == resetCamera && store.viewport == resetCamera,
                  "Reset restores 3× at the current location after visiting either zoom boundary")

            canvas.zoom(at: WorldMapGeometry.center, factor: 1.25)
            check(near(canvas.viewport.zoom, 3.75) && store.viewport.zoom == 3, "Continuous zoom changes the camera without writing on every update")
            canvas.endGesture()
            check(store.viewport == canvas.viewport, "The completed zoom gesture persists the camera")
            let placedAt = CGPoint(x: 269, y: 216)
            let beforePlacement = canvas.viewport
            let expectedWorld = WorldMapGeometry.world(at: placedAt, viewport: beforePlacement)
            check(canvas.rightMouseDown(at: placedAt) && canvas.pins.count == 1, "Right-click places a pin on the current terrain")
            let pin = canvas.pins[0]
            check(near(pin.x, expectedWorld.x) && near(pin.y, expectedWorld.y) && canvas.selectedPinID == pin.id,
                  "Pin placement uses world coordinates at the clicked point and selects the result")
            check(canvas.showsPinCoordinates && descendants(retained).contains { $0.name == "map.pin.coordinates" },
                  "Adding a pin displays its coordinates")
            check(nearPoint(marker(canvas, pin.id)?.position, placedAt), "The new marker is drawn where the user clicked")
            let dot = marker(canvas, pin.id)!.sublayers!.compactMap { $0 as? CAShapeLayer }.first { $0.name == "map.pin.dot" }!
            check(dot.fillColor == WorldMapPinArtwork.yellow.cgColor, "New pins use the requested yellow marker color")
            check(try WorldMapStore(directory: root.appendingPathComponent("navigation")).pins == [pin], "Placing a pin saves it immediately for relaunch")

            check(canvas.mouseDown(at: CGPoint(x: 303, y: 264)) && canvas.isDragging, "Dragging empty terrain begins a pan")
            check(!canvas.showsPinCoordinates && canvas.selectedPinID == pin.id
                  && !descendants(retained).contains { $0.name == "map.pin.coordinates" },
                  "Left-clicking terrain dismisses coordinates while preserving the selected pin")
            let savedCamera = store.viewport
            canvas.mouseDragged(to: CGPoint(x: 331, y: 271))
            check(canvas.viewport != savedCamera && store.viewport == savedCamera, "Panning moves the map without writing every pointer update")
            check(canvas.pins == [pin], "Panning never changes the persistent pin location")
            let afterPanPoint = WorldMapGeometry.screen(x: pin.x, y: pin.y, viewport: canvas.viewport)
            check(marker(canvas, pin.id)?.position == afterPanPoint && afterPanPoint != placedAt,
                  "A pin follows its terrain location as the camera pans")
            canvas.mouseUp()
            check(!canvas.isDragging && store.viewport == canvas.viewport, "Mouse release ends and saves the pan")
            let zoomAnchor = CGPoint(x: 250, y: 200)
            let anchorWorld = WorldMapGeometry.world(at: zoomAnchor, viewport: canvas.viewport)
            canvas.zoom(at: zoomAnchor, factor: 1.6)
            let afterZoomAnchor = WorldMapGeometry.world(at: zoomAnchor, viewport: canvas.viewport)
            check(near(anchorWorld.x, afterZoomAnchor.x) && near(anchorWorld.y, afterZoomAnchor.y),
                  "Zoom retains the world position under the pointer")
            check(canvas.pins == [pin] && marker(canvas, pin.id)?.position == WorldMapGeometry.screen(x: pin.x, y: pin.y, viewport: canvas.viewport),
                  "Pins retain world coordinates and move correctly during zoom")
            canvas.endGesture()
            let invalidCamera = canvas.viewport
            canvas.zoom(at: WorldMapGeometry.center, factor: .nan)
            canvas.zoom(at: WorldMapGeometry.center, factor: -2)
            canvas.zoom(at: CGPoint(x: 220, y: 437), factor: 2)
            check(canvas.viewport == invalidCamera, "Invalid zoom and scrolling outside the map circle do not alter the camera")
            _ = canvas.keyDown(keyCode: 53)
            check(canvas.selectedPinID == nil && !canvas.keyDown(keyCode: 51), "Escape clears selection and Delete without selection preserves pins")
            let pinScreen = WorldMapGeometry.screen(x: pin.x, y: pin.y, viewport: canvas.viewport)
            canvas.perform(actionID: "map:pin:" + pin.id.uuidString)
            check(canvas.showsPinCoordinates && canvas.pins[0].style == .green,
                  "The accessible pin action advances its style while exposing its coordinates")
            check(canvas.mouseDown(at: pinScreen) && canvas.selectedPinID == pin.id && !canvas.isDragging && !canvas.showsPinCoordinates,
                  "Left-clicking an existing marker cycles style and retains selection without dragging")
            check(canvas.pins[0].style == .player, "Pointer and accessibility pin actions use the same style cycle")
            check(canvas.rightMouseDown(at: pinScreen) && canvas.pins.isEmpty && canvas.selectedPinID == nil,
                  "Right-clicking an existing pin removes it directly even while coordinates are hidden")
            check(try WorldMapStore(directory: root.appendingPathComponent("navigation")).pins.isEmpty, "Pin removal persists across relaunch")
            canvas.perform(actionID: "map:addPin")
            check(canvas.pins.count == 1 && near(canvas.pins[0].x, canvas.viewport.centerX) && near(canvas.pins[0].y, canvas.viewport.centerY),
                  "The accessible add action places a marker at the current map center")
            check(canvas.mouseDown(at: buttonCenter) && !canvas.showsPinCoordinates && canvas.pins.count == 1,
                  "Left-clicking a map toolbar control also hides coordinates while keeping its normal action")
            check(canvas.keyDown(keyCode: 51) && canvas.pins.isEmpty && canvas.selectedPinID == nil,
                  "Keyboard Delete still removes the selected pin after its coordinates are hidden")
            check(canvas.rightMouseDown(at: placedAt) && canvas.showsPinCoordinates
                  && canvas.rightMouseDown(at: placedAt) && canvas.pins.isEmpty && !canvas.showsPinCoordinates,
                  "A second right click at a newly placed pin removes it without leaving coordinate chrome")
            canvas.perform(actionID: "map:addPin")
            let sameLayer = canvas.makeContent(for: .map, style: HUDModuleContentStyle(dark: false, accent: .systemPink, contentsScale: 3))
            check(sameLayer === retained && sameLayer.mask === edgeMask && marker(canvas, canvas.pins[0].id) != nil,
                  "Theme changes keep the same canvas, feather mask and saved markers")
            let pinkDot = marker(canvas, canvas.pins[0].id)!.sublayers!.compactMap { $0 as? CAShapeLayer }.first { $0.name == "map.pin.dot" }!
            check(pinkDot.fillColor == WorldMapPinArtwork.yellow.cgColor, "Explicit yellow/green marker styles stay distinct across HUD themes")
            check(changes > 0, "Map interactions notify the HUD to refresh projected controls")

            let clickDirectory = root.appendingPathComponent("clicks")
            let clickStore = try WorldMapStore(directory: clickDirectory)
            let clicks = WorldMapCanvas(store: clickStore, terrain: terrain, loadsTerrain: false,
                                        reduceMotion: { true }, ambient: { false })
            _ = clicks.makeContent(for: .map, style: style)
            var recenterEvents = 0, styleEvents: [MapPinStyle] = []
            clicks.onRecenter = { recenterEvents += 1 }
            clicks.onPinStyleChanged = { styleEvents.append($0) }
            let clickPoint = CGPoint(x: 301, y: 260), beforeClick = clicks.viewport
            check(clicks.mouseDown(at: clickPoint) && clicks.viewport == beforeClick,
                  "An empty-map press waits for release before deciding between click and drag")
            clicks.mouseDragged(to: CGPoint(x: clickPoint.x + 1, y: clickPoint.y + 1))
            check(clicks.viewport == beforeClick, "Small pointer jitter cannot begin a pan")
            clicks.mouseUp()
            let afterClick = WorldMapGeometry.recentered(beforeClick, at: clickPoint)
            check(clicks.viewport == afterClick && clickStore.viewport == afterClick && recenterEvents == 1,
                  "A click recenters and emits one event only after saving the unchanged zoom")
            _ = clicks.mouseDown(at: WorldMapGeometry.center); clicks.mouseUp()
            check(recenterEvents == 1, "Clicking the current map center does not emit a no-op event")
            _ = clicks.mouseDown(at: clickPoint)
            clicks.mouseDragged(to: CGPoint(x: clickPoint.x + 20, y: clickPoint.y - 10))
            let dragged = clicks.viewport
            clicks.mouseUp()
            check(clicks.viewport == dragged && clickStore.viewport == dragged && recenterEvents == 1,
                  "A completed drag saves the pan without additionally recentring its initial press")
            _ = clicks.mouseDown(at: clickPoint); clicks.deactivate()
            check(clicks.viewport == dragged && recenterEvents == 1 && !clicks.isDragging,
                  "Deactivation cancels a pending click instead of navigating while hidden")
            _ = clicks.rightMouseDown(at: WorldMapGeometry.center)
            let cyclingPin = clicks.pins[0], cyclingLayer = marker(clicks, clicks.pins[0].id)!
            check(WorldMapPinArtwork.playerGlyph?.width == 90 && WorldMapPinArtwork.playerGlyph?.height == 92
                  && WorldMapPinArtwork.playerHalo?.width == 191 && WorldMapPinArtwork.playerBeam?.width == 256,
                  "Player artwork resolves the original bounded arrow, halo and beam resources")
            let playerAssetPaths = ["sprites/icon_char---2308601083109874541.png",
                "sprites/deco_readio_mask--2444265073359569955.png",
                "textures/T_fx_mask_02_M--4275033587688225551.png"]
            let playerAssetURLs = playerAssetPaths.compactMap { HUDResources.url(for: "WatchSource/Scene/Domain/" + $0) }
            let playerAssetBytes = try playerAssetURLs.reduce(0) { total, url in
                total + (try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0)
            }
            check(playerAssetURLs.count == 3 && playerAssetBytes == 39_766,
                  "Player materials still use the three unchanged source PNGs without a generated asset")
            let beamSource = CGImageSourceCreateWithURL(playerAssetURLs[2] as CFURL, nil)!
            let rawBeam = CGImageSourceCreateImageAtIndex(beamSource, 0, nil)!
            let rawContext = CGContext(data: nil, width: rawBeam.width, height: rawBeam.height,
                bitsPerComponent: 8, bytesPerRow: rawBeam.width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)!
            rawContext.draw(rawBeam, in: CGRect(x: 0, y: 0, width: rawBeam.width, height: rawBeam.height))
            let sourcePixels = rawContext.data!.assumingMemoryBound(to: UInt8.self)
            let tintedBeam = WorldMapPinArtwork.playerBeam!
            let tintedPixels = tintedBeam.dataProvider!.data! as Data
            var matchesMaterial = true, darkPixels = 0, gradientPixels = 0, brightPixels = 0
            for y in 0..<rawBeam.height {
                for x in 0..<rawBeam.width {
                    let sourceOffset = (y * rawBeam.width + x) * 4, targetOffset = y * tintedBeam.bytesPerRow + x * 4
                    let intensity = sourcePixels[sourceOffset]
                    let expected = [intensity, UInt8((Double(intensity) * 0.9898965359).rounded()),
                                    UInt8((Double(intensity) * 0.3056603670).rounded()), intensity]
                    matchesMaterial = matchesMaterial && Array(tintedPixels[targetOffset..<targetOffset + 4]) == expected
                    if intensity == 0 { darkPixels += 1 }
                    else if intensity == 255 { brightPixels += 1 }
                    else { gradientPixels += 1 }
                }
            }
            check(matchesMaterial && darkPixels > 0 && gradientPixels > 0 && brightPixels > 0,
                  "The cached beam multiplies the source gradient by the authored yellow tint with transparent black pixels")
            for selected in [MapPinStyle.green, .player, .yellow] {
                _ = clicks.mouseDown(at: WorldMapGeometry.center)
                check(clicks.pins[0].style == selected && clicks.viewport == dragged && marker(clicks, cyclingPin.id) === cyclingLayer,
                      "Each pin click cycles the saved style without moving the camera or replacing its layer")
                let player = cyclingLayer.sublayers?.first { $0.name == "map.pin.player" }
                if selected == .player {
                    let beam = cyclingLayer.sublayers?.first { $0.name == "map.pin.playerBeam" }
                    check(player?.contents != nil && player?.isHidden == false && beam?.contents != nil
                          && beam!.frame.maxY < 0 && beam!.frame.height > player!.frame.height,
                          "The player glyph and source beam are attached above the same geographic anchor")
                    check(clicks.accessibleActions.first { $0.id == "map:pin:" + cyclingPin.id.uuidString }?.label.hasPrefix("Player marker") == true,
                          "The native pin accessibility label announces its new visual style")
                } else if let player {
                    check(player.isHidden, "Returning to a dot hides the retained player artwork")
                }
            }
            check(styleEvents == [.green, .player, .yellow] && cyclingLayer.sublayers?.count == 7,
                  "Style events occur once per committed change and all three styles use a bounded retained layer set")
            let externalBytes = Data("external map edit".utf8)
            try externalBytes.write(to: clickDirectory.appendingPathComponent("map.json"))
            _ = clicks.mouseDown(at: WorldMapGeometry.center)
            _ = clicks.mouseDown(at: clickPoint); clicks.mouseUp()
            check(clicks.pins == [cyclingPin] && clicks.viewport == dragged && recenterEvents == 1
                  && styleEvents.count == 3,
                  "Failed pin/recenter commits preserve visible state and produce no successful-action events")

            let busyStore = try WorldMapStore(directory: root.appendingPathComponent("lifecycle"))
            for index in 0..<24 {
                let screen = CGPoint(x: 160 + Double(index % 8) * 17, y: 178 + Double(index / 8) * 38)
                let world = WorldMapGeometry.world(at: screen, viewport: busyStore.viewport)
                try busyStore.addPin(x: world.x, y: world.y)
            }
            var reduced = false, ambient = true
            let busy = WorldMapCanvas(store: busyStore, terrain: terrain, loadsTerrain: false, reduceMotion: { reduced }, ambient: { ambient })
            _ = busy.makeContent(for: .map, style: style)
            check(animatedLayers(busy).isEmpty, "Many saved pins do not animate before the map activates")
            busy.prepareForPresentation()
            waitForRaster(busy)
            check(busy.isRasterSettledForVerification && animatedLayers(busy).isEmpty,
                  "The map paints during opening before interaction or pin animation starts")
            busy.deactivate()
            check(animatedLayers(busy).isEmpty, "An interrupted preparation leaves no animations")
            busy.prepareForPresentation()
            busy.activate()
            waitForRaster(busy)
            let pulses = animatedLayers(busy).filter { $0.animation(forKey: "map.pulse") != nil }
            check(!pulses.isEmpty && pulses.count <= 12, "Visible map pulse animations have a bounded compositor workload")
            check(pulses.count < busy.pins.count, "More pins do not create an animation for every saved point")
            let target = busy.pins[0]
            busy.perform(actionID: "map:pin:" + target.id.uuidString)
            check(marker(busy, target.id)!.sublayers!.contains { $0.animation(forKey: "map.pulse") != nil },
                  "The selected marker keeps animated feedback within the bounded pulse budget")
            let retainedMarker = marker(busy, target.id)!
            let retainedPulse = retainedMarker.sublayers!.first { $0.animation(forKey: "map.pulse") != nil }!
            let phase = retainedPulse.animation(forKey: "map.pulse")!.copy() as! CAAnimation
            phase.timeOffset = 0.37
            retainedPulse.add(phase, forKey: "map.pulse")
            let expectedSavedPins = busy.pins
            busy.zoom(at: WorldMapGeometry.center, factor: 2)
            check(marker(busy, target.id) === retainedMarker
                  && near(retainedPulse.animation(forKey: "map.pulse")?.timeOffset ?? -1, 0.37),
                  "Moving the camera retains each visible marker and does not restart its existing pulse phase")
            check(animatedLayers(busy).filter { $0.animation(forKey: "map.pulse") != nil }.count <= 12,
                  "Zooming a dense set of local pins retains the bounded compositor pulse budget")
            busy.perform(actionID: "map:zoomOut")
            check(animatedLayers(busy).contains { $0.name == "map.raster.detail" && $0.animation(forKey: "transform") != nil },
                  "Toolbar zoom animates the retained terrain camera explicitly")
            busy.deactivate()
            check(animatedLayers(busy).isEmpty && busyStore.viewport == busy.viewport, "Closing the map stops animations and saves any unfinished camera gesture")
            busy.activate()
            check(busy.pins == expectedSavedPins && !animatedLayers(busy).isEmpty, "Reopening restores saved pins and resumes visible animation")
            reduced = true
            _ = busy.makeContent(for: .map, style: style)
            check(animatedLayers(busy).isEmpty, "Reduce Motion removes pulse animations while keeping markers visible")
            reduced = false; ambient = false
            _ = busy.makeContent(for: .map, style: style)
            check(animatedLayers(busy).isEmpty && marker(busy, target.id) != nil, "Disabling ambient animation preserves static markers")
            busy.deactivate()

            let unavailable = WorldMapCanvas(store: nil, error: "Saved map unavailable", terrain: terrain, loadsTerrain: false)
            _ = unavailable.makeContent(for: .map, style: style)
            check(unavailable.accessibilityStatus == "Saved map unavailable" && !unavailable.accessibleActions.first { $0.id == "map:addPin" }!.enabled,
                  "A storage failure remains visible and disables pin creation")
            unavailable.perform(actionID: "map:addPin")
            check(unavailable.pins.isEmpty, "An unavailable store cannot silently accept unsaved markers")

            let countries = try WorldMapCountries.load()
            let countryOnly = WorldMapCanvas(store: try WorldMapStore(directory: root.appendingPathComponent("countries-only")),
                                            countries: countries, loadsTerrain: false, reduceMotion: { false }, ambient: { false })
            _ = countryOnly.makeContent(for: .map, style: style)
            countryOnly.activate(); waitForRaster(countryOnly)
            check(descendants(countryOnly.layer).contains { $0.name == "map.raster.detail" && $0.contents != nil }
                  && !descendants(countryOnly.layer).contains { $0.name?.hasPrefix("map.highlight") == true },
                  "Country-only terrain bakes center feedback into one image with no extra highlight plates or bitmap cache")
            check(!descendants(countryOnly.layer).contains { $0.name == "map.shenzhen.label" },
                  "Map does not add place-name text over the country faces")
            countryOnly.zoom(at: WorldMapGeometry.center, factor: 0.001)
            let countryLocation = countryOnly.viewport
            countryOnly.perform(actionID: "map:reset")
            check(countryOnly.viewport.zoom == WorldMapViewport.defaultZoom
                  && countryOnly.viewport.centerX == countryLocation.centerX && countryOnly.viewport.centerY == countryLocation.centerY,
                  "Reset restores 3× at the current location, including a camera constrained by the overview's polar bounds")
            let chromeArtwork = countryOnly.controlHighlightRoot.sublayers ?? []
            HUDControlHighlightLayer.update(in: countryOnly.controlHighlightRoot, point: CGPoint(x: 37, y: 195))
            check(HUDControlHighlightLayer.highlightedCount(in: countryOnly.controlHighlightRoot) == 1,
                  "Map zoom feedback is visible before navigation")
            countryOnly.zoom(at: WorldMapGeometry.center, factor: 1.1)
            check(zip(chromeArtwork, countryOnly.controlHighlightRoot.sublayers ?? []).allSatisfy { $0 === $1 }
                  && chromeArtwork.count == countryOnly.controlHighlightRoot.sublayers?.count,
                  "Continuous zoom retains control artwork instead of rebuilding text and buttons")
            check(HUDControlHighlightLayer.highlightedCount(in: countryOnly.controlHighlightRoot) == 1,
                  "Continuous zoom preserves the hovered control feedback")
            countryOnly.updateRenderScale(2)
            check(chromeArtwork.first === countryOnly.controlHighlightRoot.sublayers?.first,
                  "An unchanged display scale does not recreate map controls")
            countryOnly.perform(actionID: "map:zoomOut")
            check(animatedLayers(countryOnly).contains { $0.name == "map.raster.detail" && $0.animation(forKey: "transform") != nil },
                  "Country faces and hatch details move as a single retained camera image")
            countryOnly.zoom(at: WorldMapGeometry.center, factor: 1.01)
            check(descendants(countryOnly.layer).allSatisfy { $0.animation(forKey: "position") == nil && $0.animation(forKey: "transform") == nil },
                  "Direct map gestures immediately cancel earlier toolbar easing, including country masks")
            countryOnly.perform(actionID: "map:zoomOut")
            countryOnly.deactivate()
            check(descendants(countryOnly.layer).allSatisfy { $0.animation(forKey: "position") == nil && $0.animation(forKey: "transform") == nil },
                  "Leaving the map cancels pending country and mask camera animations")

            let inputStore = try WorldMapStore(directory: root.appendingPathComponent("projected-input"))
            let inputPin = try inputStore.addPin(x: inputStore.viewport.centerX, y: inputStore.viewport.centerY)
            let inputCanvas = WorldMapCanvas(store: inputStore, terrain: terrain, countries: countries,
                                            loadsTerrain: false, reduceMotion: { true }, ambient: { false })
            _ = inputCanvas.makeContent(for: .map, style: style)
            let host = NSView(frame: CGRect(x: 0, y: 0, width: 600, height: 600))
            var inputTime: TimeInterval = 100, projectionCount = 0
            let input = HUDWorldMapInteraction(canvas: inputCanvas, host: host, timeSource: { inputTime })
            input.project = { projectionCount += 1; return $0.offsetBy(dx: 11, dy: 17) }
            input.setActive(true)
            func proxy(for id: String) -> NSButton? {
                guard let label = inputCanvas.accessibleActions.first(where: { $0.id == id })?.label else { return nil }
                return host.subviews.compactMap { $0 as? NSButton }.first { $0.accessibilityLabel() == label }
            }
            let pinActionID = "map:pin:" + inputPin.id.uuidString
            let pinProxy = proxy(for: pinActionID)!
            let zoomProxy = proxy(for: "map:zoomIn")!
            let initialProxyCount = host.subviews.count
            for _ in 0..<6 { input.layoutAccessibility() }
            check(proxy(for: pinActionID) === pinProxy && proxy(for: "map:zoomIn") === zoomProxy
                  && host.subviews.count == initialProxyCount,
                  "Unchanged map accessibility refreshes retain existing native proxies without accumulating views")
            let down = NSEvent.mouseEvent(with: .leftMouseDown, location: .zero, modifierFlags: [], timestamp: 0,
                                          windowNumber: 0, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
            check(input.mouseDown(at: CGPoint(x: 260, y: 250), event: down) && input.isInputLocked,
                  "A native map drag begins immediately and holds the projected input pose")
            let beforeDragProjectionCount = projectionCount, beforeDragViewport = inputCanvas.viewport
            for step in 1...12 {
                input.mouseDragged(to: CGPoint(x: 260 + Double(step), y: 250 + Double(step) * 2 / 3))
            }
            check(inputCanvas.viewport != beforeDragViewport && projectionCount == beforeDragProjectionCount,
                  "A burst of pointer samples updates the camera immediately without repeatedly projecting native accessibility controls")
            inputTime += 1.0 / 12 + 0.0001
            input.mouseDragged(to: CGPoint(x: 274, y: 259))
            let sampledAction = inputCanvas.accessibleActions.first { $0.id == pinActionID }!
            check(projectionCount > beforeDragProjectionCount && proxy(for: pinActionID) === pinProxy
                  && pinProxy.frame == sampledAction.rect.offsetBy(dx: 11, dy: 17),
                  "An ongoing drag refreshes retained accessibility geometry at the bounded sampling interval")
            let sampledProjectionCount = projectionCount
            input.mouseDragged(to: CGPoint(x: 276, y: 261))
            let movedAction = inputCanvas.accessibleActions.first { $0.id == pinActionID }!
            check(projectionCount == sampledProjectionCount,
                  "Further samples in the same accessibility interval do not trigger another projection pass")
            input.mouseUp()
            check(!input.isInputLocked && inputStore.viewport == inputCanvas.viewport
                  && pinProxy.frame == movedAction.rect.offsetBy(dx: 11, dy: 17),
                  "Ending the native drag flushes exact accessibility positions, releases its lock, and preserves the final camera checkpoint")
            input.project = { $0.offsetBy(dx: 43, dy: 29) }
            input.layoutAccessibility()
            check(pinProxy.frame == movedAction.rect.offsetBy(dx: 43, dy: 29),
                  "Changing HUD projection updates retained accessibility frames without recreating their controls")
            input.project = { projectionCount += 1; return $0.offsetBy(dx: 43, dy: 29) }
            input.layoutAccessibility()
            let wheelCG = CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 1, wheel1: -1, wheel2: 0, wheel3: 0)!
            let wheel = NSEvent(cgEvent: wheelCG)!
            let beforeWheelProjectionCount = projectionCount, beforeWheelZoom = inputCanvas.viewport.zoom
            for _ in 0..<12 { _ = input.scroll(at: WorldMapGeometry.center, event: wheel) }
            check(input.isInputLocked && inputCanvas.viewport.zoom < beforeWheelZoom
                  && projectionCount == beforeWheelProjectionCount,
                  "Wheel samples apply immediately while native accessibility projection remains bounded")
            let endCG = CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 1, wheel1: 0, wheel2: 0, wheel3: 0)!
            endCG.setIntegerValueField(.scrollWheelEventScrollPhase, value: Int64(CGScrollPhase.ended.rawValue))
            let ended = NSEvent(cgEvent: endCG)!
            _ = input.scroll(at: WorldMapGeometry.center, event: ended)
            let afterWheelAction = inputCanvas.accessibleActions.first { $0.id == pinActionID }!
            check(!input.isInputLocked && inputStore.viewport == inputCanvas.viewport
                  && pinProxy.frame == afterWheelAction.rect.offsetBy(dx: 43, dy: 29),
                  "A zero-delta wheel end flushes exact final accessibility positions and saves the camera")
            let controlRoot = inputCanvas.controlHighlightRoot
            check(controlRoot !== inputCanvas.layer
                  && !descendants(controlRoot).contains { $0.name?.hasPrefix("map.country") == true
                      || $0.name?.hasPrefix("map.contour") == true || $0.name?.hasPrefix("map.pin.") == true },
                  "Map hover checks have a dedicated root that excludes terrain and every marker")
            let zoomRect = inputCanvas.accessibleActions.first { $0.id == "map:zoomIn" }!.rect
            HUDControlHighlightLayer.update(in: controlRoot, point: CGPoint(x: zoomRect.midX, y: zoomRect.midY))
            check(HUDControlHighlightLayer.highlightedCount(in: controlRoot) == 1,
                  "The small chrome subtree still highlights the exact hovered map control")
            HUDControlHighlightLayer.update(in: controlRoot, point: nil)
            check(HUDControlHighlightLayer.highlightedCount(in: controlRoot) == 0,
                  "Clearing map hover state needs no traversal of the geographic artwork")
            input.deactivate()
            check(host.subviews.allSatisfy(\.isHidden) && !input.isInputLocked,
                  "Deactivation hides every retained map accessibility proxy and releases input")
            input.setActive(true)
            check(proxy(for: "map:zoomIn") === zoomProxy && !zoomProxy.isHidden,
                  "Reopening reuses map accessibility controls and makes them available again")
            let beforeCancelledClick = inputCanvas.viewport
            _ = input.mouseDown(at: CGPoint(x: 320, y: 300), event: down)
            input.deactivate()
            check(inputCanvas.viewport == beforeCancelledClick && !inputCanvas.isDragging,
                  "The native interaction bridge cancels a pending click during deactivation")
        } catch { fatalError("World map canvas fixture failed: \(error)") }
        return count
    }
}
