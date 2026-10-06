import AppKit
import PDFKit
import zlib

enum ReaderTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        func rejects(_ message: String, _ body: () throws -> Void) { do { try body(); check(false, message) } catch { check(true, message) } }
        check(ReaderStore.applicationDirectory(arguments: ["--render-reader"]) == ReaderStore.applicationDirectory(arguments: ["--reader-smoke-test"]), "All native previews share one isolated diagnostic reader directory per process")
        check(ReaderStore.applicationDirectory(arguments: ["--render-reader"]) != ReaderStore.applicationDirectory(arguments: []), "Preview invocations cannot reach the real reader library")
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-ReaderTests-" + UUID().uuidString)
        try! FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let zipURL = directory.appendingPathComponent("probe.zip")
        let plain = Data("Bounded native EPUB inflate ✓".utf8)
        try! makeZIP([("entry.txt", plain)], compressed: true).write(to: zipURL)
        let archive = try! ReaderZIP(url: zipURL)
        check(try! archive.data("entry.txt") == plain, "Raw DEFLATE uses native zlib with exact output and CRC checks")
        check(archive.entries.count == 1, "Only the central-directory inventory is retained")
        for name in ["../outside", "/absolute", "folder/../../escape", "C:\\bad", "bad\0name"] {
            try! makeZIP([(name, plain)]).write(to: zipURL)
            rejects("Unsafe archive member path is rejected before extraction can exist") { _ = try ReaderZIP(url: zipURL) }
        }
        try! makeZIP([("same", plain), ("same", plain)]).write(to: zipURL)
        rejects("Duplicate ZIP members cannot create ambiguous package metadata") { _ = try ReaderZIP(url: zipURL) }
        var damaged = makeZIP([("payload", plain)])
        damaged[30 + "payload".utf8.count] ^= 0x04; try! damaged.write(to: zipURL)
        rejects("Corrupt stored entry fails the CRC instead of reaching XML or a decoder") { _ = try ReaderZIP(url: zipURL).data("payload") }
        damaged = makeZIP([("payload", plain)]); damaged[30] = 0x78; try! damaged.write(to: zipURL)
        rejects("Local and central filenames must match") { _ = try ReaderZIP(url: zipURL).data("payload") }
        damaged = makeZIP([("payload", plain)])
        let central = damaged.range(of: Data([0x50, 0x4b, 0x01, 0x02]))!.lowerBound
        damaged.replaceSubrange(central + 38..<central + 42, with: le32(0xa0000000)); try! damaged.write(to: zipURL)
        rejects("Symlinks never enter the EPUB file namespace") { _ = try ReaderZIP(url: zipURL) }
        damaged = makeZIP([("payload", plain)]); damaged.removeLast(3); try! damaged.write(to: zipURL)
        rejects("Truncated footer is rejected") { _ = try ReaderZIP(url: zipURL) }
        check(try! ReaderZIP.resolve("../image.png#cover", relativeTo: "Book/Text/chapter.xhtml") == "Book/image.png", "Package-relative resources normalize only within the archive")
        rejects("Remote resource references cannot cause network loads") { _ = try ReaderZIP.resolve("https://example.com/image.png", relativeTo: "Book/chapter.xhtml") }
        rejects("Percent-encoded traversal cannot escape the archive root") { _ = try ReaderZIP.resolve("%2e%2e/%2e%2e/secret", relativeTo: "Book/chapter.xhtml") }
        let prose = (0..<250).map { "Chapter \($0): 中文、日本語、한국어 and 👩🏽‍💻 reading text.\n" }.joined()
        let textURL = directory.appendingPathComponent("Novel.txt"); try! Data(prose.utf8).write(to: textURL)
        let document = try! ReaderDocument(access: ReaderFileAccess(url: textURL))
        let size = CGSize(width: 376, height: 334), preferences = ReaderPreferences()
        check(preferences.fontSize == 10 && preferences.lineSpacing == 2 && preferences.margin == 16,
              "New readers start at font size 10, line spacing 2 and margins 16")
        var legacy = preferences; legacy.rightToLeft = true; legacy.continuous = false
        let legacyRoundTrip = try! JSONDecoder().decode(ReaderPreferences.self, from: JSONEncoder().encode(legacy))
        check(legacyRoundTrip == legacy && !legacyRoundTrip.vertical, "Version-one reader preferences keep their exact saved values and map paged reading to horizontal")
        let first = try! document.render(at: ReaderLocation(), preferences: preferences, size: size, dark: true)
        check(first.location == ReaderLocation() && (first.next?.character ?? 0) > 0 && first.previous == nil,
              "Text layout begins at a stable UTF-16 anchor and computes an actual next-page boundary")
        check(first.image.width == 752 && first.image.height == 668, "Page raster memory is bounded by the logical viewport")
        let second = try! document.render(at: first.next!, preferences: preferences, size: size, dark: true)
        check(second.location.character > first.location.character && second.previous != nil, "Forward/backward text pages retain ordered anchors")
        var larger = preferences; larger.fontSize = 24; larger.lineSpacing = 8; larger.margin = 28
        let resized = try! document.render(at: ReaderLocation(), preferences: larger, size: size, dark: true)
        check(resized.next!.character < first.next!.character, "Font, line spacing and margins actually change pagination")
        check(try! document.location(at: 0.6).character > first.next!.character, "Progress navigation addresses the complete document without eager pagination")
        let image = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 8, pixelsHigh: 8, bitsPerSample: 8, samplesPerPixel: 4,
                                    hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        for y in 0..<8 { for x in 0..<8 { image.setColor(NSColor(deviceRed: 0, green: 0, blue: 1, alpha: 1), atX: x, y: y) } }
        let png = image.representation(using: .png, properties: [:])!
        let epubURL = directory.appendingPathComponent("Comic.epub")
        func epub(_ body: String, encoding: String.Encoding = .utf8) -> Data { makeZIP([
            ("mimetype", Data("application/epub+zip".utf8)),
            ("META-INF/container.xml", Data("<container><rootfiles><rootfile full-path=\"Book/package.opf\"/></rootfiles></container>".utf8)),
            ("Book/package.opf", Data("<package xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><metadata><dc:title>Fixture book</dc:title></metadata><manifest><item id=\"chapter\" href=\"Text/one.xhtml\" media-type=\"application/xhtml+xml\"/></manifest><spine><itemref idref=\"chapter\"/></spine></package>".utf8)),
            ("Book/Text/one.xhtml", body.data(using: encoding)!), ("Book/image.png", png)]) }
        try! epub("<html><head><title>Ignored heading</title></head><body><p>Hello book.</p><script>never execute this</script><img src=\"../image.png\"/><p>Final page.</p></body></html>").write(to: epubURL)
        let package = try! ReaderEPUB(url: epubURL), blocks = try! package.blocks(0)
        check(package.title == "Fixture book" && package.spine.count == 1 && blocks.count == 3, "EPUB container, OPF spine, text and inline images use native parsing")
        if case .text(let value) = blocks[0] { check(!value.contains("never execute"), "Scripts are omitted from native reading content") } else { check(false, "Text block expected") }
        let comic = try! ReaderDocument(access: ReaderFileAccess(url: epubURL))
        let illustration = try! comic.render(at: ReaderLocation(section: 0, block: 1), preferences: preferences, size: size, dark: false)
        check(illustration.next?.block == 2 && illustration.image.width == 752, "Embedded EPUB comics render through bounded ImageIO thumbnails")
        try! epub("<?xml version=\"1.0\" encoding=\"UTF-16\"?><html><body><p>Unicode 小说 日本語 👩🏽‍💻</p></body></html>", encoding: .utf16).write(to: epubURL)
        let utf16Blocks = try! ReaderEPUB(url: epubURL).blocks(0)
        if case .text(let value) = utf16Blocks[0] { check(value.contains("小说 日本語 👩🏽‍💻"), "UTF-16 EPUB chapters retain their text when safely normalized to native UTF-8 XML") }
        else { check(false, "UTF-16 text block expected") }
        try! epub("<!DOCTYPE html [<!ENTITY x 'expansion'>]><html><body>&x;</body></html>").write(to: epubURL)
        rejects("EPUB entity declarations are rejected before XML expansion") { _ = try ReaderEPUB(url: epubURL).blocks(0) }
        let pdfURL = directory.appendingPathComponent("Pages.pdf")
        var mediaBox = CGRect(x: 0, y: 0, width: 200, height: 300)
        let pdf = CGContext(pdfURL as CFURL, mediaBox: &mediaBox, nil)!
        for _ in 0..<3 { pdf.beginPDFPage(nil); pdf.setFillColor(NSColor.red.cgColor); pdf.fill(CGRect(x: 20, y: 20, width: 100, height: 100)); pdf.endPDFPage() }; pdf.closePDF()
        let nativePDF = try! ReaderDocument(access: ReaderFileAccess(url: pdfURL))
        let pdfPage = try! nativePDF.render(at: ReaderLocation(section: 1), preferences: preferences, size: size, dark: true)
        check(nativePDF.sectionCount == 3 && pdfPage.previous?.section == 0 && pdfPage.next?.section == 2, "PDFKit renders native pages with correct neighboring anchors")
        let enlarged = try! nativePDF.render(at: ReaderLocation(section: 1), preferences: preferences, size: size, dark: true,
            imageView: ReaderImageView(zoom: 12, pan: CGPoint(x: 200, y: -120)))
        check(enlarged.isIllustration && enlarged.image.width == pdfPage.image.width && enlarged.image.height == pdfPage.image.height,
              "PDF zoom rasterizes one bounded viewport instead of allocating a zoom-sized full page")
        check(illustration.isIllustration && !first.isIllustration, "Zoom controls are available for PDF and EPUB manga, not novel text")
        let boundedView = ReaderImageView(zoom: 1000, pan: CGPoint(x: 1e12, y: -1e12)).clamped(to: size)
        check(boundedView.zoom == 12 && boundedView.pan.x == size.width * 11 / 2 && boundedView.pan.y == -size.height * 11 / 2,
              "Zoom and pan remain finite and constrained to the image surface")
        let libraryURL = directory.appendingPathComponent("Library"), store = try! ReaderStore(directory: libraryURL)
        let book = try! store.add(url: textURL, title: "Novel")
        try! store.saveProgress(second.location, progress: second.progress, id: book.id); try! store.toggleBookmark(id: book.id)
        try! store.setPreferences(larger)
        let restored = try! ReaderStore(directory: libraryURL)
        check(restored.books.first?.location == second.location && restored.books.first?.bookmarks.count == 1 && restored.preferences == larger,
              "Independent reader library persists exact progress, bookmarks and formatting")
        check(try! ReaderFileAccess(book: restored.books[0]).url.resolvingSymlinksInPath() == textURL.resolvingSymlinksInPath(), "Saved reference resolves to the original, uncopied book")
        let libraryFile = libraryURL.appendingPathComponent("library.json"), original = try! Data(contentsOf: libraryFile)
        try! Data("{damaged".utf8).write(to: libraryFile)
        rejects("A malformed library is preserved rather than reset") { _ = try ReaderStore(directory: libraryURL) }
        check(try! Data(contentsOf: libraryFile) == Data("{damaged".utf8), "Corrupt reader data remains untouched")
        rejects("Concurrent changes cannot be overwritten by an old reader owner") { try store.setPreferences(preferences) }
        try! original.write(to: libraryFile)
        try! restored.remove(book.id)
        check(restored.books.isEmpty && FileManager.default.fileExists(atPath: textURL.path), "Removing a library reference never removes the user's book")

        let controller = ReaderController(store: restored)
        let canvas = ReaderCanvas(controller: controller), host = NSView(frame: CGRect(x: 0, y: 0, width: 400, height: 440))
        canvas.updateRenderScale(3)
        check(canvas.accessibleActions.isEmpty, "An unopened reader defers control artwork when the HUD sets render scale")
        host.wantsLayer = true; host.layer?.addSublayer(canvas.layer)
        let interaction = ReaderInteraction(canvas: canvas, host: host)
        interaction.project = { $0 }; interaction.unproject = { $0 }
        interaction.setPresented(true); interaction.setActive(true)
        var imported = 0; canvas.onEvent = { if case .imported = $0 { imported += 1 } }
        func wait(_ predicate: () -> Bool) -> Bool {
            let deadline = Date().addingTimeInterval(8)
            while !predicate(), Date() < deadline { RunLoop.current.run(until: Date().addingTimeInterval(0.01)) }; return predicate()
        }
        let lazyLock = NSLock(); var lazyLoads = 0, lazyLoadedOnMain = true
        let lazy = ReaderController(loadStore: {
            lazyLock.lock(); lazyLoads += 1; lazyLoadedOnMain = Thread.isMainThread; lazyLock.unlock()
            return try ReaderStore(directory: directory.appendingPathComponent("LazyLibrary"))
        })
        check(lazyLoads == 0, "Constructing a HUD-owned reader controller does not open its library")
        lazy.setActive(true)
        check(wait { !lazy.loading }, "A presented reader completes its lazy library read")
        lazyLock.lock(); let lazyResult = lazyLoads == 1 && !lazyLoadedOnMain; lazyLock.unlock()
        check(lazyResult, "Reader library initialization is performed once on its owned worker")
        lazy.setActive(false)
        interaction.importFiles([textURL])
        check(wait { controller.current != nil && !controller.loading }, "Reader opens a real book asynchronously using its serial worker")
        check(imported == 1 && controller.pages.count <= 3, "Import emits one content-free event and owns at most three visible/neighbor pages")
        let before = controller.current!.location
        _ = canvas.scroll(at: CGPoint(x: 100, y: 100), delta: 335)
        check(controller.current?.location != before, "Positive-down wheel navigation consumes the cached next page immediately")
        check(canvas.pageTurnSequence == 0, "Vertical navigation does not install a page-flip animation")
        check(canvas.scrollOffsetForVerification == 1, "Crossing a cached vertical page boundary preserves the scroll remainder")
        canvas.perform("open")
        check(interaction.secondaryMenu?.artwork.position == CGPoint(x: 12, y: 44), "Open's source chooser is anchored directly below the Open button")
        interaction.closeMenu(animated: false)
        canvas.perform("settings")
        check(interaction.secondaryMenu?.items.contains { $0.id == "horizontal" } == true && interaction.secondaryMenu?.items.contains { $0.id == "vertical" } == true,
              "Reading settings offer horizontal pages and vertical scrolling as the two directions")
        check(interaction.secondaryMenu?.items.contains { $0.id == "mode" || $0.id == "direction" } == false,
              "Legacy RTL and redundant continuous-scroll controls are not displayed")
        check(interaction.secondaryMenu?.items.first { $0.id == "close" }?.rect.size == CGSize(width: 23, height: 23), "Reader menus use the shared square close-button size")
        check(interaction.secondaryMenu != nil && interaction.secondaryMenu?.artwork.superlayer === canvas.layer, "Reader options use the same retained projected menu plane")
        check(interaction.secondaryMenu?.contentSize.width == 248 && interaction.secondaryMenu?.artwork.position == CGPoint(x: 144, y: 44),
              "Reading settings are compact and anchored beneath their own toolbar button")
        check(interaction.secondaryMenu?.items.allSatisfy { interaction.secondaryMenu!.bounds.contains($0.rect) } == true,
              "Narrow reading settings keep every target and close button inside their face")
        let event = NSEvent.mouseEvent(with: .leftMouseDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: 0, context: nil, eventNumber: 0, clickCount: 1, pressure: 0)!
        check(interaction.mouseDown(at: CGPoint(x: -1, y: -1), event: event) && interaction.secondaryMenu == nil,
              "An outside click dismisses the reader menu without reaching controls behind it")
        interaction.deactivate()
        check(!controller.active && controller.pages.isEmpty && controller.current == nil, "Hidden reader releases published decoded pages and stops new worker requests")
        var modalDrained: Bool?
        controller.drainPendingWrites { modalDrained = $0 }
        let modalDeadline = Date().addingTimeInterval(4)
        while modalDrained == nil, Date() < modalDeadline { _ = RunLoop.current.run(mode: .modalPanel, before: Date().addingTimeInterval(0.02)) }
        check(modalDrained == true, "Progress acknowledgments and drains run inside AppKit's modal termination loop")
        let committed = try! ReaderStore(directory: libraryURL)
        check(committed.books.first?.location != before, "Closing before prefetch completes still saves the already displayed cached page")
        var drained = false; controller.drainPendingWrites { drained = $0 }; check(wait { drained }, "All committed reader progress drains without recurring timers")
        interaction.setPresented(true)
        check(wait { controller.current != nil }, "Reference-only library can reopen after full decoder teardown")
        check(imported == 1, "Automatic reopen does not log a false import")
        let firstBookID = controller.book!.id, firstBookLocation = controller.current!.location
        let otherURL = directory.appendingPathComponent("Second.txt"); try! Data((prose + prose).utf8).write(to: otherURL)
        controller.open(url: otherURL)
        check(wait { controller.current != nil && controller.book?.id != firstBookID && !controller.loading }, "A second library book opens independently")
        let secondBookID = controller.book!.id
        controller.jump(progress: 0.72)
        check(wait { (controller.current?.progress ?? 0) > 0.65 && !controller.loading }, "Second book stores a distinct reading position")
        let secondBookLocation = controller.current!.location
        var bothSaved = false; controller.drainPendingWrites { bothSaved = $0 }
        check(wait { bothSaved }, "Multiple books commit through the same bounded writer")
        controller.select(firstBookID)
        check(wait { controller.book?.id == firstBookID && controller.current?.location == firstBookLocation && !controller.loading },
              "Selecting a library book cannot inherit the previously visible book's page")
        controller.select(secondBookID)
        check(wait { controller.book?.id == secondBookID && controller.current?.location == secondBookLocation && !controller.loading },
              "Each book restores its own exact UTF-16 reading anchor")
        var settled = false; controller.drainPendingWrites { settled = $0 }; check(wait { settled }, "Both book positions are committed before the failure fixture")
        controller.select(firstBookID)
        check(wait { controller.book?.id == firstBookID && controller.current != nil && !controller.loading }, "Failure fixture returns to its first book")
        var selectedSaved = false; controller.drainPendingWrites { selectedSaved = $0 }; check(wait { selectedSaved }, "Selected reference commits before temporarily changing the fixture archive")
        let beforeFailure = try! Data(contentsOf: libraryFile)
        try! Data("temporary external fixture edit".utf8).write(to: libraryFile, options: .atomic)
        controller.next()
        let failedLocation = controller.current!.location
        check(wait { controller.error != nil }, "A rejected progress commit stays pending without overwriting externally changed data")
        try! beforeFailure.write(to: libraryFile, options: .atomic)
        controller.select(secondBookID)
        check(wait { controller.book?.id == secondBookID && controller.current != nil && !controller.loading }, "Restored archive permits another book to open")
        controller.next()
        let otherNewLocation = controller.current!.location
        var recovered = false; controller.drainPendingWrites { recovered = $0 }
        check(wait { recovered }, "The shared writer retries failed progress for every affected book")
        let recoveredStore = try! ReaderStore(directory: libraryURL)
        check(recoveredStore.books.first { $0.id == firstBookID }?.location == failedLocation,
              "A successful second-book commit cannot erase the first book's failed position")
        check(recoveredStore.books.first { $0.id == secondBookID }?.location == otherNewLocation,
              "Per-book coalescing preserves the most recently displayed position for both books")
        interaction.setActive(true)
        var horizontal = controller.preferences; horizontal.vertical = false
        controller.setPreferences(horizontal)
        controller.jump(progress: 0)
        check(wait { controller.current?.location.character == 0 && !controller.loading }, "Horizontal fixture resets to the beginning")
        let horizontalStart = controller.current!.location
        _ = canvas.scroll(at: CGPoint(x: 100, y: 100), deltaX: 0, deltaY: 1000, phase: .began)
        check(controller.current?.location == horizontalStart, "Vertical wheel movement cannot change a horizontal reader's page")
        let turnsBefore = canvas.pageTurnSequence
        _ = canvas.scroll(at: CGPoint(x: 100, y: 100), deltaX: 70, deltaY: 0, phase: .began)
        check(controller.current?.location != horizontalStart, "A horizontal trackpad gesture turns a cached page")
        let horizontalSecond = controller.current!.location
        check(HUDRuntimeAppearance.reduceMotion || canvas.pageTurnSequence == turnsBefore + 1, "Horizontal page turns install one finite directional animation")
        _ = canvas.scroll(at: CGPoint(x: 100, y: 100), deltaX: 700, deltaY: 0, phase: .changed, momentumPhase: .changed)
        check(controller.current?.location == horizontalSecond, "Momentum cannot accidentally turn several pages after one swipe")
        func key(_ code: UInt16, modifiers: NSEvent.ModifierFlags = []) -> NSEvent { NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: modifiers, timestamp: 0,
            windowNumber: 0, context: nil, characters: "", charactersIgnoringModifiers: "", isARepeat: false, keyCode: code)! }
        for modifier: NSEvent.ModifierFlags in [.command, .control, .option, .shift] {
            check(!interaction.keyDown(key(123, modifiers: modifier)) && !interaction.keyDown(key(124, modifiers: modifier)),
                  "Modified arrows remain available to summon shortcuts and macOS navigation")
        }
        check(controller.current?.location == horizontalSecond, "Passing modified navigation chords through does not turn a page")
        check(interaction.keyDown(key(123)), "Left arrow is routed to the horizontal reader")
        check(wait { controller.current?.location == horizontalStart }, "Left arrow restores the prior page")
        check(!interaction.keyDown(key(125)), "Down arrow cannot turn a horizontal page")
        check(interaction.keyDown(key(124)), "Right arrow turns a horizontal reader page")
        check(wait { controller.current?.location != horizontalStart }, "Right arrow advances the actual reading anchor")
        canvas.perform("bookmarks")
        check(interaction.secondaryMenu?.items.contains { $0.id == "up" || $0.id == "down" } == false, "Bookmarks have no meaningless pagination footer buttons")
        check(interaction.secondaryMenu?.contentSize.width == 240 && interaction.secondaryMenu?.artwork.position == CGPoint(x: 152, y: 44),
              "Bookmarks use a narrower panel immediately below the bookmarks button")
        interaction.closeMenu(animated: false)
        interaction.deactivate()

        let imageStore = try! ReaderStore(directory: directory.appendingPathComponent("ImageLibrary"))
        for index in 0..<9 {
            let file = directory.appendingPathComponent("Small-\(index).txt"); try! Data("A short generated fixture.".utf8).write(to: file)
            _ = try! imageStore.add(url: file, title: "Fixture \(index)")
        }
        _ = try! imageStore.add(url: pdfURL, title: "Manga PDF")
        let imageController = ReaderController(store: imageStore), imageCanvas = ReaderCanvas(controller: imageController)
        let imageInteraction = ReaderInteraction(canvas: imageCanvas, host: host)
        imageInteraction.project = { $0 }; imageInteraction.unproject = { $0 }
        imageInteraction.setPresented(true); imageInteraction.setActive(true)
        check(wait { imageController.current != nil && !imageController.loading }, "Manga opens on its isolated decoder")
        check(!imageInteraction.keyDown(key(49, modifiers: .command)) && !imageInteraction.keyDown(key(125, modifiers: .control)),
              "Vertical mode does not consume modified Space or arrow shortcuts")
        imageCanvas.perform("library")
        let firstRows = imageInteraction.secondaryMenu?.items.map(\.id)
        check(imageInteraction.secondaryMenu?.items.contains { $0.id == "up" || $0.id == "down" } == false, "Library lists replace the old up/down footer controls")
        _ = imageInteraction.scroll(at: CGPoint(x: 100, y: 100), deltaX: 0, deltaY: 70)
        check(imageInteraction.secondaryMenu?.items.map(\.id) != firstRows, "Scrolling reaches additional library entries with a bounded visible row count")
        check((imageInteraction.secondaryMenu?.items.count ?? 0) <= 13, "Long libraries retain only six visible rows and their deletion controls")
        imageInteraction.closeMenu(animated: false)
        check(imageCanvas.magnify(at: CGPoint(x: 200, y: 215), amount: 4), "Pinch magnification is accepted on manga pages")
        check(imageCanvas.imageView.zoom == 5, "Manga zoom responds immediately without waiting for a decoder")
        _ = imageCanvas.mouseDown(at: CGPoint(x: 200, y: 215))
        imageCanvas.mouseDragged(to: CGPoint(x: 242, y: 240)); imageCanvas.mouseUp()
        check(imageCanvas.imageView.pan == CGPoint(x: 42, y: 25), "Dragging pans the enlarged manga surface")
        check(wait { imageController.imageDetail?.view == imageCanvas.imageView }, "A settled zoom produces one detail image on the existing worker")
        check(imageController.imageDetail?.page.image.width == 752 && imageController.pages.count <= 3,
              "The zoom detail and neighboring page cache have fixed bitmap bounds")
        let zoomControls = imageCanvas.accessibleActions.filter { $0.id.hasPrefix("zoom") }
        check(zoomControls.count == 3 && zoomControls.allSatisfy { imageCanvas.layer.bounds.insetBy(dx: 8, dy: 8).contains($0.rect.insetBy(dx: -3, dy: -3)) },
              "Every manga zoom button and detached highlight frame has a safe margin from the module clip")
        check(zoomControls.allSatisfy { !$0.rect.intersects(imageCanvas.viewport) && !$0.rect.intersects(imageCanvas.progressRect) },
              "Zoom buttons do not cover reading content or the progress target")
        let enlargedStart = imageController.current!.location, zoomTurns = imageCanvas.pageTurnSequence
        for _ in 0..<4 where imageController.current?.location == enlargedStart {
            _ = imageCanvas.scroll(at: CGPoint(x: 200, y: 215), deltaX: 0, deltaY: 668)
        }
        check(wait { imageController.current?.location.section == enlargedStart.section + 1 }, "Vertical scrolling reaches the next enlarged manga page rather than stopping at its lower edge")
        check(imageCanvas.imageView.zoom == 5 && imageCanvas.imageView.pan.x == 42 && imageCanvas.pageTurnSequence == zoomTurns,
              "Vertical manga page crossing preserves magnification and horizontal position without a page-flip animation")
        let enlargedSecond = imageController.current!.location
        for _ in 0..<4 where imageController.current?.location == enlargedSecond {
            _ = imageCanvas.scroll(at: CGPoint(x: 200, y: 215), deltaX: 0, deltaY: -668)
        }
        check(wait { imageController.current?.location == enlargedStart } && imageCanvas.imageView.zoom == 5,
              "Scrolling backward across enlarged page boundaries also retains zoom")
        var pagedManga = imageController.preferences; pagedManga.vertical = false
        imageController.setPreferences(pagedManga)
        check(wait { !imageController.loading }, "Manga can switch navigation axis with its existing zoom")
        let zoomBeforeFlip = imageCanvas.imageView
        imageCanvas.perform("next")
        check(wait { imageController.current?.location.section == enlargedStart.section + 1 } && imageCanvas.imageView == zoomBeforeFlip,
              "Horizontal manga page flips preserve both magnification and pan")
        check(wait { imageController.imageDetail?.view == zoomBeforeFlip && imageController.imageDetail?.page.location == imageController.current?.location },
              "A flipped enlarged page receives its own bounded detail raster instead of stale pixels")
        check(imageController.pages.count <= 3 && imageController.imageDetail?.page.image.width == 752,
              "Cross-page zoom still owns only three base pages and one viewport-sized detail image")
        let horizontalMangaLocation = imageController.current!.location, horizontalPan = imageCanvas.imageView.pan
        _ = imageCanvas.scroll(at: CGPoint(x: 200, y: 215), deltaX: 0, deltaY: 80)
        check(imageController.current?.location == horizontalMangaLocation && imageCanvas.imageView.zoom == 5
              && imageCanvas.imageView.pan.y == max(-imageCanvas.viewport.height * 2, horizontalPan.y - 80),
              "A vertical wheel can reach the lower part of an enlarged horizontal page without turning it or resetting zoom")
        _ = imageCanvas.scroll(at: CGPoint(x: 200, y: 215), deltaX: 0, deltaY: 5000)
        check(imageController.current?.location == horizontalMangaLocation, "Vertical pan at a horizontal manga page's lower edge never becomes a page turn")
        imageCanvas.perform("zoomReset")
        check(imageCanvas.imageView == ReaderImageView() && imageController.imageDetail == nil, "Reset zoom releases the extra detail raster")
        _ = imageCanvas.scroll(at: CGPoint(x: 200, y: 215), deltaX: 0, deltaY: 5000)
        check(imageCanvas.imageView == ReaderImageView() && imageController.current?.location == horizontalMangaLocation,
              "At 1× horizontal reading still ignores ordinary vertical wheel input")
        imageCanvas.perform("zoomIn")
        imageInteraction.deactivate()
        check(imageController.imageDetail == nil && imageController.pages.isEmpty, "Closing a zoomed manga cancels detail work and releases page rasters")
        return count
    }
    private static func le16(_ value: UInt16) -> Data { Data([UInt8(value & 255), UInt8(value >> 8)]) }
    private static func le32(_ value: UInt32) -> Data { Data([UInt8(value & 255), UInt8((value >> 8) & 255), UInt8((value >> 16) & 255), UInt8(value >> 24)]) }
    private static func makeZIP(_ items: [(String, Data)], compressed: Bool = false) -> Data {
        var data = Data(), table = Data()
        for (name, contents) in items {
            let filename = Data(name.utf8), offset = UInt32(data.count), crc = contents.withUnsafeBytes { UInt32(crc32(0, $0.bindMemory(to: Bytef.self).baseAddress, uInt($0.count))) }
            var body = contents
            if compressed {
                body = Data(count: contents.count * 2 + 128); var stream = z_stream()
                precondition(deflateInit2_(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY, ZLIB_VERSION, Int32(MemoryLayout<z_stream>.size)) == Z_OK)
                let status = contents.withUnsafeBytes { source in body.withUnsafeMutableBytes { target -> Int32 in
                    stream.next_in = UnsafeMutablePointer(mutating: source.bindMemory(to: Bytef.self).baseAddress); stream.avail_in = uInt(source.count)
                    stream.next_out = target.bindMemory(to: Bytef.self).baseAddress; stream.avail_out = uInt(target.count); return deflate(&stream, Z_FINISH)
                } }
                precondition(status == Z_STREAM_END); let bytes = stream.total_out; deflateEnd(&stream); body = body.prefix(Int(bytes))
            }
            data += le32(0x04034b50) + le16(20) + le16(0) + le16(compressed ? 8 : 0) + le16(0) + le16(0)
            data += le32(crc) + le32(UInt32(body.count)) + le32(UInt32(contents.count)) + le16(UInt16(filename.count)) + le16(0) + filename + body
            table += le32(0x02014b50) + le16(20) + le16(20) + le16(0) + le16(compressed ? 8 : 0) + le16(0) + le16(0)
            table += le32(crc) + le32(UInt32(body.count)) + le32(UInt32(contents.count)) + le16(UInt16(filename.count))
            table += le16(0) + le16(0) + le16(0) + le16(0) + le32(0) + le32(offset) + filename
        }
        let offset = UInt32(data.count); data += table
        data += le32(0x06054b50) + le16(0) + le16(0) + le16(UInt16(items.count)) + le16(UInt16(items.count))
        data += le32(UInt32(table.count)) + le32(offset) + le16(0); return data
    }
}
