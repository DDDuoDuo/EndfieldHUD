import AppKit
import SQLite3

enum ArchiveCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        func wait(_ predicate: () -> Bool) {
            let until = Date().addingTimeInterval(3)
            while !predicate(), Date() < until { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
            check(predicate(), "Archive async action completes")
        }
        func descendants(_ layer: CALayer) -> [CALayer] { [layer] + (layer.sublayers ?? []).flatMap(descendants) }
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("ArchiveCanvas-\(UUID())", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let controller = ArchiveController(store: ArchiveStore(directory: directory))
        let canvas = ArchiveCanvas(controller: controller)
        let _ = NSApplication.shared
        let host = NSView(frame: CGRect(x: 0, y: 0, width: 500, height: 500)); host.wantsLayer = true
        let window = NSWindow(contentRect: CGRect(x: -10000, y: -10000, width: 500, height: 500),
                              styleMask: .borderless, backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false; window.contentView = host
        defer { window.close() }
        let input = HUDArchiveInteraction(canvas: canvas, host: host)
        input.project = { $0.offsetBy(dx: 30, dy: 25) }; input.unproject = { CGPoint(x: $0.x - 30, y: $0.y - 25) }
        host.layer!.addSublayer(canvas.makeContent(for: .archive, style: .init(dark: true, accent: .systemGreen, contentsScale: 2)))
        input.setActive(true)
        RunLoop.main.run(until: Date().addingTimeInterval(0.05))
        check(canvas.actions.contains { $0.id == "new" }, "Empty Archive offers a retained create button")
        check(canvas.actions.filter { $0.id == "all" || $0.id == "uncategorized" }.count == 2 && !canvas.actions.contains { $0.id.hasPrefix("category:") },
              "Fresh Archive exposes only All and Uncategorized without invented default categories")
        let remove = canvas.actions.first { $0.id == "deleteCategory" }!
        check(!remove.enabled && remove.rect.minX == ArchiveCanvas.addRect.maxX + 6 && remove.rect.midY == ArchiveCanvas.addRect.midY,
              "Category minus sits immediately beside Add and is disabled for All")
        canvas.perform("deleteCategory"); check(!input.capturesPointer, "All cannot open a category removal prompt")
        canvas.perform("uncategorized"); canvas.perform("deleteCategory")
        check(!input.capturesPointer, "Uncategorized cannot be deleted")
        canvas.perform("all")
        canvas.perform("new")
        var createMenu = host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!
        check(createMenu.bounds.size == createMenu.contentSize && createMenu.artwork.sublayers?.last?.bounds.size == createMenu.contentSize,
              "Projected menus retain logical content bounds instead of expanding to the host window")
        check(input.capturesPointer, "New document menu captures the whole pointer plane")
        check(host.subviews.contains { $0 is NotesRetainedMenu }, "Archive uses the existing retained menu family")
        let underneathButton = canvas.actions.first { $0.id == "new" }!.rect
        let outside = CGPoint(x: underneathButton.midX, y: underneathButton.midY)
        let click = NSEvent.mouseEvent(with: .leftMouseDown, location: outside, modifierFlags: [], timestamp: 0,
            windowNumber: window.windowNumber, context: nil, eventNumber: 0, clickCount: 1, pressure: 1)!
        check(input.mouseDown(at: outside, event: click) && !input.capturesPointer && controller.selected == nil,
              "Clicking a control behind a category menu only dismisses the menu, without clicking through")
        canvas.perform("new"); createMenu = host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!
        check(!createMenu.items.contains { $0.id == "journal" || $0.id == "research" },
              "Document creation offers user categories instead of fixed template buttons")
        createMenu.perform("newCategory")
        let categoryName = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first!
        categoryName.textView.string = "Research"; input.textDidChange(Notification(name: NSText.didChangeNotification, object: categoryName.textView))
        check(categoryName.artwork.zPosition > createMenu.artwork.zPosition && categoryName.placeholder == L10n.text("Category name", "分类名称"),
              "New category uses a projected name editor above its retained menu")
        createMenu.perform("create")
        let researchCategory = controller.categories.first { $0.name == "Research" }!.id
        check(controller.selected?.categoryID == researchCategory, "Creating a category and document assigns the selected user category")
        check(host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.count == 3, "Title, date and long-form body use projected native editors")
        let body = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first { $0.logicalRect == canvas.bodyRect }!
        check(body.artwork.superlayer === canvas.layer, "Body glyphs share the module's actual tilted plane")
        check(body.textView.string.isEmpty && body.placeholder == L10n.text("Content", "内容"),
              "Content hint is a grey placeholder rather than editable saved text")
        count += formattingChecks(canvas: canvas, input: input, body: body, host: host)
        body.textView.string = "Research answer\n" + String(repeating: "Long text supports scroll.\n", count: 80)
        input.textDidChange(Notification(name: NSText.didChangeNotification, object: body.textView))
        check(controller.selected?.body.hasPrefix("Research answer") == true, "Native edits update only the selected archive")
        check(body.textView.frame.height > body.logicalRect.height, "Long text uses a scrollable viewport")
        let captures = body.captureCount
        for _ in 0..<120 { input.layoutAccessibility() }
        check(body.captureCount == captures, "Pointer tilt does not rasterize the text again")
        body.captureVisibleArtwork()
        check(body.retainedPixelCount <= HUDProjectedTextEditor.maximumPixels, "Text raster memory stays bounded")
        let title = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first { $0.logicalRect == ArchiveCanvas.titleRect }!
        check(title.textView.string.isEmpty && title.placeholder == L10n.text("Title", "标题"),
              "Title hint remains separate from a new empty document")
        title.textView.string = "Preserved title"; title.textView.setSelectedRange(NSRange(location: 3, length: 2))
        input.textDidChange(Notification(name: NSText.didChangeNotification, object: title.textView))
        controller.flush(); wait { !controller.hasUnsavedChanges && !controller.isBusy }
        check(host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.contains { $0 === title }
                && title.textView.selectedRange() == NSRange(location: 3, length: 2),
              "An asynchronous save preserves the live editor and its selection")
        RunLoop.main.run(until: Date().addingTimeInterval(0.02))
        let stableCaptureCount = title.captureCount
        for _ in 0..<30 { controller.onChange?() }
        RunLoop.main.run(until: Date().addingTimeInterval(0.02))
        check(title.captureCount == stableCaptureCount, "Unchanged store notifications do not recapture TextKit artwork")
        check(canvas.layer.sublayers?.first { $0.name == "archive.face" }?.animation(forKey: "archive.transition") == nil,
              "Save acknowledgments do not replay a dimming transition")
        _ = canvas.makeContent(for: .archive, style: .init(dark: false, accent: .systemGreen, contentsScale: 2))
        check(title.textView.textStorage?.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor == .black
                && body.textView.textStorage?.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor == .black
                && title.textView.selectedRange() == NSRange(location: 3, length: 2),
              "Appearance changes recolor retained native text without resetting selection")
        let oldLanguage = L10n.language; L10n.language = .english
        _ = canvas.makeContent(for: .archive, style: .init(dark: false, accent: .systemGreen, contentsScale: 2))
        check(title.textView.accessibilityLabel() == "Title", "Language changes update existing category document labels immediately")
        L10n.language = oldLanguage
        check(ArchiveCanvas.parseDate("2026-02-30") == nil && ArchiveCanvas.parseDate("2026-2-03") == nil,
              "Date editing rejects invalid or ambiguous noncanonical dates")
        check(ArchiveCanvas.parseDate("2026-10-04").map(ArchiveCanvas.dateString) == "2026-10-04",
              "Archive dates use a stable Gregorian round trip")

        // Only a tiny isolated PNG is decoded; no player or user media is used.
        let imageURL = directory.appendingPathComponent("fixture.png")
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 8, pixelsHigh: 8, bitsPerSample: 8,
            samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        try! bitmap.representation(using: .png, properties: [:])!.write(to: imageURL)
        let reference = try! NotesMediaFactory.makeReference(from: imageURL)
        count += thumbnailChecks(reference: reference, image: bitmap.cgImage!)
        let retainedText = body.textView; retainedText.setSelectedRange(NSRange(location: 4, length: 3))
        var entry = controller.selected!; entry.media = [reference]; controller.update(entry); canvas.render()
        let mediaCounter = canvas.layer.sublayers!.first { $0.name == "archive.face" }!.sublayers!.first { $0.name == "archive.media.counter" } as! CATextLayer
        let previousMedia = canvas.actions.first { $0.id == "mediaPrevious" }!.rect, nextMedia = canvas.actions.first { $0.id == "mediaNext" }!.rect
        check(mediaCounter.string as? String == "1/1" && mediaCounter.alignmentMode == .center
                && mediaCounter.frame.midX == (previousMedia.maxX + nextMedia.minX) / 2,
              "The attachment count is centered in the space between its two arrows")
        canvas.formattingField = "body"
        let attachedFormatting = canvas.actions.filter { $0.id.hasPrefix("format") }.map(\.rect)
        check(canvas.bodyRect.maxY < ArchiveCanvas.mediaRect.minY
                && ArchiveCanvas.mediaRect.maxY < ArchiveCanvas.seekRect.minY
                && attachedFormatting.count == 4
                && attachedFormatting.allSatisfy { !canvas.bodyRect.intersects($0) && !ArchiveCanvas.mediaRect.intersects($0) && !ArchiveCanvas.seekRect.intersects($0) },
              "Attachment layout keeps text, media, seek and every formatting control in separate usable rows")
        let resizedBody = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first { $0.logicalRect == canvas.bodyRect }!
        check(resizedBody.textView === retainedText && retainedText.selectedRange() == NSRange(location: 4, length: 3)
              && host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.contains { $0 === title },
              "Adding an attachment resizes only the body viewport and retains TextKit selection and other fields")
        wait { canvas.presentation?.layer.contents != nil }
        check(Set(canvas.actions.map(\.id)).count == canvas.actions.count,
              "Synchronous media loading notifications cannot recursively duplicate actions")
        let face = canvas.layer.sublayers!.first { $0.name == "archive.face" }!
        for _ in 0..<100 { canvas.presentation?.onProgress?() }
        check(canvas.layer.sublayers!.contains { $0 === face }, "Media progress updates preserve the existing face")
        canvas.perform("addMedia")
        let sourceMenu = host.subviews.compactMap { $0 as? NotesMediaSourceChooser }.first!
        let mediaAnchor = canvas.actions.first { $0.id == "addMedia" }!.rect
        check(sourceMenu.artwork.frame.minY == mediaAnchor.maxY + 6 && sourceMenu.artwork.frame.maxX == mediaAnchor.maxX,
              "The attachment source menu anchors directly below the document plus on the same projected plane")
        sourceMenu.perform("close")
        let outgoing = canvas.presentation!
        input.deactivate()
        check(canvas.presentation == nil && !outgoing.hasActiveDecoder && outgoing.layer.contents != nil,
              "Hiding releases media decoding while retaining the existing bounded poster for the exit")
        check(canvas.layer.sublayers?.contains { $0.name == "archive.outgoingText" } == true
              && host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.isEmpty,
              "Closing retains bounded text artwork after releasing all native editors")
        wait { outgoing.layer.contents == nil && canvas.layer.sublayers?.contains { $0.name == "archive.outgoingText" } != true }
        input.setActive(true); wait { controller.selected != nil && canvas.presentation?.layer.contents != nil }
        func sql(_ command: String) {
            var db: OpaquePointer?
            precondition(sqlite3_open(directory.appendingPathComponent("archive.sqlite").path, &db) == SQLITE_OK)
            defer { sqlite3_close(db) }
            precondition(sqlite3_exec(db, command, nil, nil, nil) == SQLITE_OK)
        }
        controller.flush(); wait { !controller.isBusy && !controller.hasUnsavedChanges }
        sql("CREATE TRIGGER reject_canvas_write BEFORE INSERT ON entries BEGIN SELECT RAISE(FAIL, 'synthetic'); END")
        let liveBody = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first { $0.logicalRect == canvas.bodyRect }!
        liveBody.textView.string += "Retry preserves this change."
        input.textDidChange(Notification(name: NSText.didChangeNotification, object: liveBody.textView)); controller.flush()
        wait { controller.error != nil }
        check(canvas.actions.contains { $0.id == "retry" }, "A failed save exposes an in-HUD retry action")
        sql("DROP TRIGGER reject_canvas_write"); canvas.perform("retry")
        wait { controller.error == nil && !controller.hasUnsavedChanges }
        check(!canvas.actions.contains { $0.id == "retry" } && controller.selected?.body.hasSuffix("Retry preserves this change.") == true,
              "Retry saves the retained draft without requiring another edit, then removes the recovery control")
        let id = controller.selected!.id
        let documentBeforeMove = controller.selected!
        canvas.perform("moveCategory")
        let moveMenu = host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!
        moveMenu.perform("newCategory")
        let moveName = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first { $0.textView.accessibilityLabel() == L10n.text("Category name", "分类名称") }!
        moveName.textView.string = "Observations"; moveMenu.perform("create")
        let movedCategory = controller.categories.first { $0.name == "Observations" }!.id
        check(controller.selected?.id == id && controller.selected?.categoryID == movedCategory
                && controller.selected?.body == documentBeforeMove.body && controller.selected?.media == documentBeforeMove.media,
              "The top-right category menu moves a document without replacing its content or attachments")
        canvas.perform("moveCategory"); host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!.perform(researchCategory.uuidString)
        check(controller.selected?.categoryID == researchCategory, "Existing categories can be selected directly in the move dropdown")
        canvas.perform("moveCategory"); host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!.perform(movedCategory.uuidString)
        controller.flush(); wait { !controller.isBusy && !controller.hasUnsavedChanges }
        check((try! controller.store.entry(id))?.categoryID == movedCategory,
              "A category chosen through the menu is persisted")
        canvas.perform("back"); wait { controller.selected == nil && !controller.isBusy }
        wait {
            canvas.layer.sublayers?.first { $0.name == "archive.face" }.map { descendants($0).contains { $0.frame.height == 52 && $0.contents != nil } } == true
        }
        check(canvas.layer.sublayers?.first { $0.name == "archive.face" }.map { descendants($0).contains { $0.frame.height == 52 && $0.isHidden } } == true,
              "An attached image supplies a gallery thumbnail and hides the default archive icon")
        let backdrop = canvas.layer.sublayers!.first { $0.name == "archive.backdrop" }!
        let categoryTransition = withPausedLayerClock(canvas.layer) {
            canvas.perform("category:\(researchCategory)")
            return canvas.layer.sublayers?.first { $0.name == "archive.face" }?.animation(forKey: "archive.transition")
        }
        check(canvas.actions.filter { $0.id.hasPrefix("entry:") }.isEmpty,
              "Moving a document removes it from the previous category")
        check(canvas.layer.sublayers!.contains { $0 === backdrop } && backdrop.opacity == 1 && (backdrop.animationKeys() ?? []).isEmpty,
              "Category crossfades retain one continuously opaque backdrop")
        check((canvas.layer.sublayers?.contains { $0.name == "archive.outgoingFace" } == true) == !HUDRuntimeAppearance.reduceMotion
                && (categoryTransition != nil) == !HUDRuntimeAppearance.reduceMotion,
              "Category changes retain an outgoing transition only when motion is enabled")
        canvas.perform("category:\(movedCategory)")
        check(canvas.actions.contains { $0.id == "entry:\(id)" }, "Destination category contains the moved document")
        canvas.perform("entry:\(id)"); wait { controller.selected?.id == id }
        let removableCategory = controller.createCategory(named: "Removable")!
        controller.moveSelected(to: removableCategory); controller.flush(); wait { !controller.isBusy && !controller.hasUnsavedChanges }
        let preservedDocument = controller.selected!
        canvas.perform("back"); wait { !controller.isBusy }; canvas.perform("category:\(removableCategory)")
        check(canvas.actions.first { $0.id == "deleteCategory" }?.enabled == true, "Selecting a custom category enables the adjacent minus")
        canvas.perform("deleteCategory")
        let removalMenu = host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!
        check(input.capturesPointer && controller.categories.contains { $0.id == removableCategory }, "Category removal waits for retained-menu confirmation")
        removalMenu.perform("cancel")
        check(controller.categories.contains { $0.id == removableCategory }, "Cancelling category deletion preserves its documents and category")
        canvas.perform("deleteCategory"); host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!.perform("delete")
        wait { !controller.isBusy && !controller.hasUnsavedChanges }
        let afterCategoryRemoval = try! controller.store.entry(id)!
        check(afterCategoryRemoval.categoryID == nil && afterCategoryRemoval.body == preservedDocument.body && afterCategoryRemoval.media == preservedDocument.media
                && !controller.categories.contains { $0.id == removableCategory },
              "Confirmed category removal keeps the document and attachments in Uncategorized")
        check(canvas.filtersUncategorized && canvas.actions.contains { $0.id == "entry:\(id)" }, "After category removal the gallery reveals the preserved uncategorized documents")
        canvas.perform("entry:\(id)"); wait { controller.selected?.id == id }
        canvas.perform("delete")
        host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!.perform("cancel")
        check(controller.selected?.id == id, "Cancel deletion preserves document")
        canvas.perform("delete")
        host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!.perform("delete")
        wait { controller.selected == nil && !controller.entries.contains { $0.id == id } }
        check(host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.isEmpty, "Deleting releases editors")
        input.deactivate()
        check(!input.capturesPointer && canvas.presentation == nil, "Hidden archive releases native menus and media")
        controller.drainPendingWrites(timeout: 2) { _ in }
        RunLoop.main.run(until: Date().addingTimeInterval(0.1))
        for index in 0..<9 {
            var entry = ArchiveEntry(template: .journal); entry.title = "Fixture \(index)"
            try! controller.store.save(entry)
        }
        input.setActive(true); wait { controller.entries.count == 9 }
        canvas.perform("all")
        check(canvas.actions.filter { $0.id.hasPrefix("entry:") }.count == 6, "Gallery retains at most six document cards")
        check(canvas.galleryScrollMaximum > 0 && canvas.galleryScrollThumb != nil
                && ArchiveCanvas.galleryScrollerRect.minX > ArchiveCanvas.galleryRect.maxX
                && canvas.layer.bounds.contains(ArchiveCanvas.galleryScrollerRect),
              "Long document galleries expose a scrollbar in their own right gutter inside the module")
        let thumb = canvas.galleryScrollThumb!, grab = CGPoint(x: thumb.midX, y: thumb.midY)
        check(input.mouseDown(at: grab, event: click) && input.capturesPointer, "Grabbing the gallery thumb captures pointer movement")
        input.mouseDragged(to: CGPoint(x: grab.x, y: ArchiveCanvas.galleryScrollerRect.maxY + 500))
        check(canvas.scrollOffset == canvas.galleryScrollMaximum, "Dragging beyond the track clamps the document gallery to its final row")
        input.mouseUp(); check(!input.capturesPointer, "Releasing the gallery thumb ends its drag capture")
        let scroller = host.subviews.compactMap { $0 as? NSSlider }.first { $0.accessibilityLabel() == L10n.text("Documents", "档案") }!
        scroller.setAccessibilityValue(NSNumber(value: 0))
        check(canvas.scrollOffset == 0 && scroller.doubleValue == 0, "The gallery's native accessibility value scrolls the same retained content")
        let gallery = descendants(canvas.layer).first { $0.name == "archive.gallery" }!
        let retainedCard = gallery.sublayers!.first!
        let cardY = retainedCard.frame.minY, unchangedFace = canvas.layer.sublayers!.first { $0.name == "archive.face" }!
        canvas.scroll(delta: 0.375)
        check(abs(canvas.scrollOffset - 0.375) < 0.0001 && abs(retainedCard.frame.minY - (cardY - 0.375)) < 0.0001,
              "Gallery wheel input moves retained artwork by fractional pixels without waiting for a row threshold")
        check(canvas.layer.sublayers!.contains { $0 === unchangedFace } && gallery.sublayers!.contains { $0 === retainedCard },
              "Continuous scrolling preserves the face and existing card layers instead of crossfading pages")
        canvas.scroll(delta: 125)
        check(canvas.actions.filter { $0.id.hasPrefix("entry:") }.allSatisfy { ArchiveCanvas.galleryRect.contains($0.rect) },
              "Partially visible cards share clipped drawing, click and accessibility rectangles")
        canvas.scroll(delta: .nan); canvas.scroll(delta: .infinity)
        check(canvas.actions.filter { $0.id.hasPrefix("entry:") }.count <= 8 && gallery.sublayers!.count <= 8,
              "Fractional gallery scrolling retains at most four partial rows rather than accumulating cards")
        for index in 0..<12 { _ = controller.createCategory(named: "Scroll category \(index)") }
        wait { !controller.isBusy && !controller.hasUnsavedChanges }
        let sidebar = descendants(canvas.layer).first { $0.name == "archive.categories" }!
        check(sidebar.bounds.minX < 0 && sidebar.bounds.maxX > ArchiveCanvas.categoriesRect.width,
              "The category viewport includes horizontal border and hover overflow instead of cutting the buttons")
        let retainedCategory = sidebar.sublayers!.first!, initialY = sidebar.sublayers!.first!.frame.minY
        canvas.scroll(at: CGPoint(x: 50, y: 100), delta: 0.625)
        check(abs(canvas.categoryScrollOffset - 0.625) < 0.0001 && abs(retainedCategory.frame.minY - (initialY - 0.625)) < 0.0001,
              "Sidebar categories scroll continuously by the same logical pixel delta")
        check(sidebar.sublayers!.count <= 8 && canvas.actions.filter { $0.id.hasPrefix("category:") || $0.id == "all" || $0.id == "uncategorized" }.allSatisfy { ArchiveCanvas.categoriesRect.contains($0.rect) },
              "Sidebar retains only clipped visible category rows")
        canvas.perform("new")
        let scrollMenu = host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!
        check(scroller.isHidden, "An open category menu hides the gallery's underlying accessibility scroller")
        let choiceID = controller.categories[0].id.uuidString
        let oldChoice = scrollMenu.items.first { $0.id == choiceID }!.rect
        let menuRow = descendants(scrollMenu.artwork).first { $0.name == "archive.categoryMenu.rows" }!.sublayers![1]
        let menuClip = descendants(scrollMenu.artwork).first { $0.name == "archive.categoryMenu.rows" }!
        check(menuClip.bounds.minX < menuRow.frame.minX && menuClip.bounds.maxX > menuRow.frame.maxX,
              "Category chooser borders and hover feedback fit inside the expanded horizontal clip")
        _ = input.scroll(at: CGPoint(x: 150, y: 100), delta: 0.5)
        check(abs(scrollMenu.items.first { $0.id == choiceID }!.rect.minY - (oldChoice.minY - 0.5)) < 0.0001,
              "Category chooser scrolls by fractional pixels with matching hit and accessibility geometry")
        check(descendants(scrollMenu.artwork).first { $0.name == "archive.categoryMenu.rows" }!.sublayers!.contains { $0 === menuRow },
              "Sub-row menu scrolling retains its existing text and highlight layers")
        _ = input.scroll(at: CGPoint(x: 150, y: 100), delta: 10000)
        check(scrollMenu.items.filter { UUID(uuidString: $0.id) != nil }.count <= 7, "Category chooser bounds its retained visible row count at the end of a large scroll")
        scrollMenu.perform("close")
        check(!scroller.isHidden, "Dismissing the chooser restores the gallery's accessibility scroller")
        canvas.perform("category:\(researchCategory)")
        check(canvas.actions.filter { $0.id.hasPrefix("entry:") }.isEmpty, "Custom category filter cannot leak uncategorized entries")
        input.deactivate()
        return count
    }

    private static func formattingChecks(canvas: ArchiveCanvas, input: HUDArchiveInteraction,
                                         body: HUDProjectedTextEditor, host: NSView) -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let text = body.textView, controller = canvas.controller
        let plain = NotesTextStyle().attributes(defaultColor: .white)
        text.textStorage!.setAttributedString(NSAttributedString(string: "Alpha beta gamma", attributes: plain))
        text.typingAttributes = plain
        input.textDidChange(Notification(name: NSText.didChangeNotification, object: text))
        host.window!.makeFirstResponder(text)
        check(canvas.formattingField == "body" && body.hitTest(CGPoint(x: body.logicalRect.midX + 30, y: body.logicalRect.midY + 25)) === text,
              "First focus immediately shows formatting and the visually suppressed native editor still receives projected clicks")
        check(canvas.actions.filter { $0.id.hasPrefix("format") }.count == 4,
              "Editing exposes the four existing Notes formatting controls")
        let formattingRects = canvas.actions.filter { $0.id.hasPrefix("format") }.map(\.rect)
        check(formattingRects.allSatisfy { !canvas.bodyRect.intersects($0) && $0.maxY <= 406 },
              "All four formatting squares clear the body and the lower HUD battery-control exclusion band")
        let selected = NSRange(location: 6, length: 4)
        text.setSelectedRange(selected)
        func menu(_ id: String) -> NotesFormattingControls {
            canvas.perform(id)
            return host.subviews.compactMap { $0 as? NotesFormattingControls }.first!
        }
        func bold(at index: Int) -> Bool {
            NSFontManager.shared.traits(of: text.textStorage!.attribute(.font, at: index, effectiveRange: nil) as! NSFont).contains(.boldFontMask)
        }
        let (special, reveal) = withPausedLayerClock(canvas.layer) {
            let special = menu("formatSpecial")
            return (special, special.artwork.animation(forKey: "archive.menu") as? CABasicAnimation)
        }
        check(special.artwork.superlayer === canvas.layer,
              "Formatting uses the existing Notes submenu on the tilted archive plane")
        if HUDRuntimeAppearance.reduceMotion {
            check(reveal == nil, "Reduced motion opens formatting without a reveal animation")
        } else {
            check(reveal?.keyPath == "opacity" && reveal?.duration == 0.16
                    && reveal?.fromValue as? Int == 0 && reveal?.toValue as? Int == 1,
                  "Formatting reveals with the finite retained submenu fade")
        }
        check(special.artwork.frame.maxY < formattingRects.map(\.minY).min()!,
              "Formatting popovers end above the visible formatting row instead of obscuring its buttons")
        text.undoManager!.removeAllActions(); text.undoManager!.beginUndoGrouping()
        special.perform("trait:0")
        text.undoManager!.endUndoGrouping()
        check(bold(at: 6) && !bold(at: 0) && text.selectedRange() == selected,
              "Bold affects only the selected range and retains that selection")
        check(controller.selected?.bodyRichText?.runs.contains { $0.style.bold && $0.range == selected } == true,
              "Selected rich formatting immediately updates the persisted draft representation")
        text.undoManager!.undo()
        check(!bold(at: 6) && controller.selected?.bodyRichText?.runs.allSatisfy { !$0.style.bold } == true,
              "Undo restores both native text attributes and the saved draft")
        text.undoManager!.redo()
        check(bold(at: 6) && controller.selected?.bodyRichText?.runs.contains { $0.style.bold } == true,
              "Redo restores rich formatting and its saved draft")
        for id in ["trait:1", "trait:2", "trait:3"] { special.perform(id) }
        let traits = NotesFormattingEditor.style(in: text, defaultColor: .white)
        check(traits.bold && traits.italic && traits.underline && traits.strikethrough,
              "Italic, underline and strikethrough combine with bold on the same selection")
        menu("formatFont").onChange?(.font("Helvetica"))
        menu("formatSize").onChange?(.size(24))
        let color = NSColor(srgbRed: 0.1, green: 0.5, blue: 0.9, alpha: 1)
        menu("formatColor").onChange?(.color(color))
        let styled = NotesFormattingEditor.style(in: text, defaultColor: .white)
        check(styled.fontName?.contains("Helvetica") == true && styled.fontSize == 24 && styled.color == NotesRGBA(color),
              "Archive reuses Notes font, size and color changes")
        check(text.textStorage!.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor == .white,
              "Formatting leaves unrelated runs unchanged")
        host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!.perform("close")
        check(host.window!.firstResponder === text && text.selectedRange() == selected,
              "Closing a formatting menu restores the editor and selected range")
        text.setSelectedRange(NSRange(location: text.string.utf16.count, length: 0))
        let previous = NSAttributedString(attributedString: text.textStorage!)
        menu("formatSize").onChange?(.size(30))
        menu("formatColor").onChange?(.color(color))
        check(text.textStorage!.isEqual(to: previous), "Caret-only formatting changes future typing without restyling existing text")
        host.subviews.compactMap { $0 as? NotesRetainedMenu }.first!.perform("close")
        text.insertText("!", replacementRange: text.selectedRange())
        let last = text.textStorage!.attributes(at: text.textStorage!.length - 1, effectiveRange: nil)
        check((last[.font] as? NSFont)?.pointSize == 30 && NotesRGBA(last[.foregroundColor] as! NSColor) == NotesRGBA(color),
              "Newly inserted text uses the separate pending typing size and color")
        controller.flush()
        let deadline = Date().addingTimeInterval(3)
        while (controller.isBusy || controller.hasUnsavedChanges), Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
        check(!controller.isBusy && !controller.hasUnsavedChanges, "Rich document flush completes")
        let stored = try! controller.store.entry(controller.selected!.id)!
        check(stored.body == text.string && stored.bodyRichText == controller.selected?.bodyRichText
                && stored.bodyStyle.fontSize == 30 && stored.bodyStyle.color == NotesRGBA(color),
              "SQLite round trip preserves content, mixed rich runs and future typing style")
        _ = canvas.makeContent(for: .archive, style: .init(dark: false, accent: .systemGreen, contentsScale: 2))
        check(NotesRGBA(text.textStorage!.attribute(.foregroundColor, at: 6, effectiveRange: nil) as! NSColor) == NotesRGBA(color)
                && text.textStorage!.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor == .black,
              "Changing appearance recolors default ink while preserving explicit user colors")
        _ = canvas.makeContent(for: .archive, style: .init(dark: true, accent: .systemGreen, contentsScale: 2))
        text.textStorage!.setAttributedString(NSAttributedString(string: "", attributes: plain)); text.typingAttributes = plain
        input.textDidChange(Notification(name: NSText.didChangeNotification, object: text))
        return count
    }

    /// Inspect finite tracks without racing the offscreen window's render server
    /// on slower CI hosts. Production animation timing is left unchanged.
    private static func withPausedLayerClock<T>(_ layer: CALayer, _ body: () -> T) -> T {
        let speed = layer.speed, offset = layer.timeOffset, begin = layer.beginTime
        let paused = layer.convertTime(CACurrentMediaTime(), from: nil)
        CATransaction.begin(); CATransaction.setDisableActions(true)
        layer.speed = 0; layer.timeOffset = paused; CATransaction.commit()
        defer {
            CATransaction.begin(); CATransaction.setDisableActions(true)
            layer.speed = speed; layer.timeOffset = offset; layer.beginTime = begin
            CATransaction.commit()
        }
        return body()
    }

    private static func thumbnailChecks(reference: NotesMediaReference, image: CGImage) -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let gate = DispatchSemaphore(value: 0), began = DispatchSemaphore(value: 0)
        let lock = NSLock(); var decoded = 0, concurrent = 0, maximumConcurrent = 0
        let thumbnails = ArchiveGalleryThumbnails { _ in
            lock.lock(); decoded += 1; concurrent += 1; maximumConcurrent = max(maximumConcurrent, concurrent); let first = decoded == 1; lock.unlock()
            if first { began.signal(); _ = gate.wait(timeout: .now() + 3) }
            lock.lock(); concurrent -= 1; lock.unlock()
            return image
        }
        let first = (0..<20).map { _ in (UUID(), reference) }
        thumbnails.setWanted(first)
        check(began.wait(timeout: .now() + 1) == .success && thumbnails.pendingCount <= 5,
              "Gallery bounds visible thumbnail work to six requests with one decode")
        for _ in 0..<20 { thumbnails.setWanted(first) }
        check(thumbnails.pendingCount <= 5, "Repeated paints do not queue the in-flight thumbnail again")
        let second = (0..<6).map { _ in (UUID(), reference) }
        thumbnails.setWanted(second)
        check(thumbnails.pendingCount <= 6, "Scrolling replaces pending thumbnail work instead of accumulating pages")
        gate.signal()
        let deadline = Date().addingTimeInterval(3)
        while thumbnails.count < 6, Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
        check(thumbnails.count == 6 && !thumbnails.contains(first[0].0),
              "A stale decoder result does not populate the newly visible gallery")
        lock.lock(); let maximum = maximumConcurrent, calls = decoded; lock.unlock()
        check(maximum == 1 && calls == 7, "Only one native decode executes at a time and repeated paints add no duplicate work")
        check(second.allSatisfy { thumbnails.image(for: $0.0, reference: reference) != nil },
              "Every visible document receives its own bounded thumbnail")
        thumbnails.clear()
        check(thumbnails.count == 0 && thumbnails.pendingCount == 0, "Hiding releases all thumbnail bitmaps and pending work")
        return count
    }
}
