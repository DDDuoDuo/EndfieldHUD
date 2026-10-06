import AppKit
import QuartzCore

enum ProjectionTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let bounds = CGRect(x: 0, y: 0, width: 900, height: 650)
        let configuration = AppConfiguration.defaults
        let model = ProjectionModel(configuration: configuration)
        check(model.backgroundEnabled && model.media.isEmpty && model.drawing.strokes.isEmpty,
              "Projection starts with an independent empty session and optional background on")
        model.setBrushWidth(400); check(model.brushWidth == 80, "Brush uses the Notes maximum width")
        model.setBrushWidth(-2); check(model.brushWidth == 1, "Brush width cannot vanish")
        model.setBrushWidth(.nan); check(model.brushWidth == 1, "Nonfinite brush input preserves current width")
        model.setDarkness(2); model.setBlur(-1)
        check(model.darkness == 1 && model.blur == 0, "Background controls remain bounded")
        model.setDarkness(.nan); model.setBlur(.infinity)
        check(model.darkness == 1 && model.blur == 0, "Nonfinite background values cannot corrupt the session")
        let stroke = NotesDrawingStroke(points: [.init(x: 0.1, y: 0.2), .init(x: 0.7, y: 0.2)], width: 3, color: .init(red: 1, green: 0, blue: 0))
        check(model.append(stroke) && model.drawing.strokes.count == 1, "Projection reuses NotesDrawing without a database")
        check(!model.erase(at: CGPoint(x: 800, y: 600), size: bounds.size), "Eraser outside annotation leaves the drawing intact")
        check(model.erase(at: CGPoint(x: 350, y: 130), size: bounds.size), "Shared segment eraser removes the intended stroke")
        let reference = NotesMediaReference(kind: .image, bookmark: Data([1]), isSecurityScoped: false,
            lastKnownPath: "/tmp/EndfieldHUD-Projection-tests-no-source.png", displayName: "Fixture", pixelWidth: 400, pixelHeight: 200, duration: nil, frameCount: 1)
        check(model.addMedia(reference, at: CGPoint(x: -500, y: 900), in: bounds) != nil, "Valid references can be added without copying source files")
        check(bounds.contains(model.media[0].frame), "Offscreen insertion is clamped to the current display")
        let id = model.media[0].id
        model.setFrame(CGRect(x: 2000, y: -800, width: 8000, height: 6000), id: id, in: bounds)
        check(model.media[0].frame == bounds, "Resize cannot strand media outside the display")
        model.setFrame(CGRect(x: 20, y: 20, width: 2, height: 1), id: id, in: bounds)
        check(model.media[0].frame.size == CGSize(width: 110, height: 100), "Media retain usable minimum control dimensions")
        for _ in 1..<ProjectionModel.maximumMedia { _ = model.addMedia(reference, at: bounds.origin, in: bounds) }
        check(model.media.count == 16 && model.addMedia(reference, at: .zero, in: bounds) == nil,
              "Media ownership has a fixed session cap and never evicts existing items")
        check(model.media.first?.id == id, "Insertion preserves prior media identity")
        model.bringForward(id); check(model.media.last?.id == id, "Move interaction preserves stable identity while raising media")
        check(model.removeMedia(id) && !model.removeMedia(id), "Removal is idempotent and does not touch original files")
        check(ProjectionModel.constrain(CGRect(x: 0, y: 0, width: 400, height: 400), to: .zero) == .zero, "Disconnected empty display has safe geometry")

        let liveModel = ProjectionModel(configuration: configuration)
        let view = ProjectionWorkspaceView(model: liveModel, frame: bounds, reduceMotion: false)
        let window = NSWindow(contentRect: bounds, styleMask: [.borderless], backing: .buffered, defer: true)
        window.isReleasedWhenClosed = false; window.contentView = view
        view.setActive(true)
        check(view.active && view.backgroundVisibleForVerification, "Visible projection activates its retained artwork")
        check(view.toolbar.items.count == 8 && view.toolbar.items.allSatisfy { view.toolbar.bounds.contains($0.rect) },
              "Every toolbar tool has an accessible retained target within its plate")
        check(view.toolbar.contentSize == CGSize(width: 336, height: 42)
              && view.toolbar.frame.minY >= 64 && view.toolbar.frame.maxY < bounds.midY,
              "Compact toolbar stays centered near the top with camera-housing clearance")
        check(ProjectionToolbar.topInset(safeAreaTop: 32, visibleTop: 24) == 64
              && ProjectionToolbar.topInset(safeAreaTop: 70, visibleTop: 24) == 94
              && ProjectionToolbar.topInset(safeAreaTop: 0, visibleTop: 80) == 104
              && ProjectionToolbar.topInset(safeAreaTop: .nan, visibleTop: .infinity) == 64,
              "Toolbar clearance handles camera safe areas, tall menu bars and invalid display geometry")
        check(view.toolbar.items.map(\.id).contains("clear")
              && view.toolbar.items.firstIndex(where: { $0.id == "clear" }) == view.toolbar.items.firstIndex(where: { $0.id == "eraser" })! + 1
              && view.toolbar.items.last?.title == L10n.text("Return", "返回")
              && view.toolbar.items.last?.accessibilityTitle == L10n.text("Return", "返回"),
              "Clear all sits immediately beside Eraser and Return has the same visible and accessible label")
        check(view.toolbar.artwork.sublayers?.allSatisfy { $0.cornerRadius == 10 } == true,
              "Only the Projection toolbar's backing and face receive rounded outer edges")
        let toolbarFace = view.toolbar.artwork.sublayers!.last!
        let toolbarLabels = toolbarFace.sublayers!.filter { $0.name?.hasPrefix("projection.toolbar.label.") == true }
        check(toolbarLabels.count == 7, "Projection draws one centered glyph face for every text or symbol tool")
        for label in toolbarLabels {
            let item = view.toolbar.items.first { label.name == "projection.toolbar.label." + $0.id }!
            check(label.frame == item.rect, "Toolbar glyphs occupy the same full centered target as their button")
            let bitmap = label.contents as! CGImage, width = bitmap.width, height = bitmap.height
            var pixels = [UInt8](repeating: 0, count: width * height * 4)
            pixels.withUnsafeMutableBytes { bytes in
                let context = CGContext(data: bytes.baseAddress, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                    space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)!
                context.draw(bitmap, in: CGRect(x: 0, y: 0, width: width, height: height))
            }
            var left = width, right = -1, top = height, bottom = -1
            for y in 0..<height { for x in 0..<width where pixels[(y * width + x) * 4 + 3] > 20 {
                left = min(left, x); right = max(right, x); top = min(top, y); bottom = max(bottom, y)
            } }
            check(right >= left && abs(left - (width - 1 - right)) <= 2 && abs(top - (height - 1 - bottom)) <= 2,
                  "Rendered Projection glyph ink is centered horizontally and vertically, not just its line box")
        }
        var events: [ProjectionEvent] = []; view.onEvent = { events.append($0) }
        func event(_ type: NSEvent.EventType, _ point: CGPoint) -> NSEvent {
            NSEvent.mouseEvent(with: type, location: view.convert(point, to: nil), modifierFlags: [], timestamp: 0,
                              windowNumber: window.windowNumber, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
        }
        view.mouseMoved(with: event(.mouseMoved, CGPoint(x: 400, y: 250)))
        check(view.brushCursorDiameterForVerification == liveModel.brushWidth, "Pointer circle depicts the actual brush width")
        view.mouseDown(with: event(.leftMouseDown, CGPoint(x: 100, y: 250)))
        view.mouseDragged(with: event(.leftMouseDragged, CGPoint(x: 200, y: 250)))
        check(liveModel.drawing.strokes.isEmpty, "Pointer samples do not mutate committed drawing before mouse up")
        view.mouseUp(with: event(.leftMouseUp, CGPoint(x: 200, y: 250)))
        check(liveModel.drawing.strokes.count == 1 && events.count == 1, "Mouse up commits one vector stroke and one closed event")
        view.rightMouseDown(with: event(.rightMouseDown, CGPoint(x: 200, y: 250)))
        check(liveModel.erasing, "Right click toggles the same eraser state as the toolbar")
        view.mouseDown(with: event(.leftMouseDown, CGPoint(x: 150, y: 250)))
        view.mouseUp(with: event(.leftMouseUp, CGPoint(x: 150, y: 250)))
        check(liveModel.drawing.strokes.isEmpty, "Fullscreen eraser uses shared NotesDrawing segment tests")
        view.perform("background")
        check(!liveModel.backgroundEnabled && !view.backgroundVisibleForVerification,
              "Disabling background leaves only annotations and controls, no full-screen visual cover")
        view.perform("color")
        check(view.secondaryMenu is NotesFormattingControls, "Projection reuses the Notes color wheel instead of another picker service")
        check(view.secondaryMenu?.artwork.superlayer === view.layer && view.secondaryMenu?.artwork.zPosition == 3_000_000,
              "Color picker remains above media and drawing artwork")
        check(view.secondaryMenu?.artwork.animation(forKey: "projection.menu") != nil, "Secondary menu has a finite reveal")
        (view.secondaryMenu as? NotesFormattingControls)?.onChange?(.color(.red))
        let colorEvents = events.count
        (view.secondaryMenu as? NotesFormattingControls)?.onChange?(.color(.blue))
        check(events.count == colorEvents, "Color drag does not write an event on every pointer sample")
        view.closeMenu()
        check(events.count == colorEvents + 1 && view.secondaryMenu == nil, "Closing color chooser commits exactly one settings event")
        view.perform("brush")
        check(view.secondaryMenu is ProjectionAdjustmentMenu, "Brush has a retained direct adjustment control")
        (view.secondaryMenu as? ProjectionAdjustmentMenu)?.perform("plus:0")
        check(liveModel.brushWidth == 6, "Toolbar thickness adjustment changes the actual cursor and drawing width")
        view.perform("media")
        check(view.secondaryMenu is NotesMediaSourceChooser, "Media offers the exact existing Finder/Shelf choices")
        view.shelfChoices = { [] }; (view.secondaryMenu as? NotesMediaSourceChooser)?.onChoose?(true)
        check(view.secondaryMenu is NotesShelfMediaPicker, "Shelf picker is reused and only reads the supplied choices")
        view.perform("appearance")
        check((view.secondaryMenu as? ProjectionAdjustmentMenu)?.values.count == 2, "Background darkness and native blur remain independently adjustable")
        let adjust = view.secondaryMenu as? ProjectionAdjustmentMenu
        adjust?.perform("plus:0")
        check(liveModel.darkness > configuration.backgroundDarkness, "Background slider updates only the projection model")
        var noMotion = configuration; noMotion.reduceMotion = true; view.update(configuration: noMotion)
        view.closeMenu(animated: false); view.perform("brush")
        check(view.animationCountForVerification == 0, "Reduce Motion removes running finite transitions and prevents new ones")
        view.closeMenu(animated: false)
        // Real decoder/reference path, using only a tiny owned fixture. This
        // does not substitute a decoder, access real Notes or start playback.
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-ProjectionTests-" + UUID().uuidString)
        try! FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let imageURL = directory.appendingPathComponent("fixture.png"), invalidURL = directory.appendingPathComponent("invalid.txt")
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 8, pixelsHigh: 8, bitsPerSample: 8,
            samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        for y in 0..<8 { for x in 0..<8 { bitmap.setColor(.red, atX: x, y: y) } }
        try! bitmap.representation(using: .png, properties: [:])!.write(to: imageURL)
        try! Data("synthetic unsupported media".utf8).write(to: invalidURL)
        func wait(_ predicate: () -> Bool) -> Bool {
            let deadline = Date().addingTimeInterval(8)
            while !predicate(), Date() < deadline { RunLoop.current.run(until: Date().addingTimeInterval(0.01)) }
            return predicate()
        }
        view.importMedia(urls: [imageURL])
        check(wait { liveModel.media.count == 1 && view.mediaArtworkCountForVerification == 1 },
              "Real shared PNG import resolves its bookmark and decodes one bounded visible poster")
        check(view.mediaPresentationCount == 1 && liveModel.media[0].reference.kind == .image,
              "Projection creates one shared presentation for the committed image reference")
        let originalMedia = liveModel.media[0].frame
        let handle = CGPoint(x: originalMedia.minX + 40, y: originalMedia.minY + 12)
        view.mouseDown(with: event(.leftMouseDown, handle))
        view.mouseDragged(with: event(.leftMouseDragged, CGPoint(x: handle.x + 50, y: handle.y + 20)))
        view.mouseUp(with: event(.leftMouseUp, CGPoint(x: handle.x + 50, y: handle.y + 20)))
        let movedMedia = liveModel.media[0].frame
        check(movedMedia.minX > originalMedia.minX && movedMedia.minY > originalMedia.minY
              && view.mediaPresentationCount == 1, "Moving the media header reuses its player/poster owner")
        let grip = CGPoint(x: movedMedia.maxX - 3, y: movedMedia.maxY - 3)
        view.mouseDown(with: event(.leftMouseDown, grip))
        view.mouseDragged(with: event(.leftMouseDragged, CGPoint(x: grip.x + 35, y: grip.y + 20)))
        view.mouseUp(with: event(.leftMouseUp, CGPoint(x: grip.x + 35, y: grip.y + 20)))
        check(liveModel.media[0].frame.width > movedMedia.width && bounds.contains(liveModel.media[0].frame),
              "Media resize retains usable controls and remains inside the selected display")
        var completed = view.completedImportCountForVerification
        view.importMedia(urls: [imageURL, invalidURL])
        check(wait { view.completedImportCountForVerification > completed }, "Invalid media batch completes with an explicit error")
        check(liveModel.media.count == 1, "A mixed valid/invalid batch cannot partly import or replace existing objects")
        completed = view.completedImportCountForVerification
        view.importMedia(urls: [imageURL, imageURL])
        view.setActive(false)
        check(view.mediaArtworkCountForVerification == 0 && !view.hasLiveMediaResources,
              "Hiding cancels media playback/decode ownership and discards its bounded poster")
        view.setActive(true)
        check(wait { view.completedImportCountForVerification > completed }, "Canceled import work drains through its generation guard")
        check(liveModel.media.count == 1, "A late canceled import cannot alter a reopened session")
        view.setActive(false)
        check(!view.active && view.secondaryMenu == nil && view.animationCountForVerification == 0 && !view.hasLiveMediaResources,
              "Hidden projection owns no menu animation, playback item, GIF deadline or progress observer")
        view.setActive(true)
        check(view.model === liveModel && liveModel.color == .blue, "Reopening keeps session annotations and choices in RAM")
        _ = liveModel.append(stroke)
        view.perform("clear")
        let cancelledClear = view.secondaryMenu as! ProjectionClearConfirmation
        check(liveModel.media.count == 1 && liveModel.drawing.strokes.count == 1,
              "Opening the retained clear confirmation preserves all session content")
        let escape = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
            windowNumber: window.windowNumber, context: nil, characters: "\u{1b}", charactersIgnoringModifiers: "\u{1b}", isARepeat: false, keyCode: 53)!
        view.keyDown(with: escape); cancelledClear.perform("confirm")
        check(liveModel.media.count == 1 && liveModel.drawing.strokes.count == 1,
              "Cancel and a stale confirmation callback cannot clear the session")
        let savedDarkness = liveModel.darkness, savedBlur = liveModel.blur, savedWidth = liveModel.brushWidth
        let savedBackground = liveModel.backgroundEnabled, savedEraser = liveModel.erasing
        completed = view.completedImportCountForVerification
        view.importMedia(urls: [imageURL, imageURL])
        view.perform("clear")
        (view.secondaryMenu as? ProjectionClearConfirmation)?.perform("confirm")
        check(liveModel.media.isEmpty && liveModel.drawing.strokes.isEmpty && view.mediaPresentationCount == 0
              && !view.hasLiveMediaResources && view.secondaryMenu == nil,
              "Confirmed clear removes drawings, media and every player/decoder owner immediately")
        check(wait { view.completedImportCountForVerification > completed } && liveModel.media.isEmpty,
              "A canceled in-flight import cannot repopulate a cleared workspace")
        check(liveModel.darkness == savedDarkness && liveModel.blur == savedBlur && liveModel.brushWidth == savedWidth
              && liveModel.backgroundEnabled == savedBackground && liveModel.erasing == savedEraser && liveModel.color == .blue,
              "Clear all retains background, brush and color preferences")
        check(events.filter { if case .cleared = $0 { return true }; return false }.count == 1,
              "One confirmed content clear emits exactly one closed-category event")
        var staleShelfAccesses = 0
        view.shelfAccess = { _ in staleShelfAccesses += 1; throw CocoaError(.userCancelled) }
        view.perform("media"); (view.secondaryMenu as? NotesMediaSourceChooser)?.onChoose?(true)
        let staleShelfPicker = view.secondaryMenu as! NotesShelfMediaPicker
        view.perform("clear"); (view.secondaryMenu as? ProjectionClearConfirmation)?.perform("confirm")
        staleShelfPicker.onSelect?(UUID())
        check(staleShelfAccesses == 0 && view.secondaryMenu == nil && liveModel.media.isEmpty,
              "A retired shelf chooser cannot access a file or reopen work after clear")
        view.setActive(false); view.setActive(true)
        check(liveModel.media.isEmpty && liveModel.drawing.strokes.isEmpty && liveModel.color == .blue,
              "Reopening preserves the explicitly cleared session and its settings")
        view.dispose(); window.contentView = nil; window.close()
        return count
    }
}
