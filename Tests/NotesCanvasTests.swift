import AppKit
import ImageIO
import QuartzCore

enum NotesCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldCharge-CanvasTests-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: directory) }
        let oldLanguage = L10n.language
        defer { L10n.language = oldLanguage }
        L10n.language = .english
        func store(_ name: String) -> NotesStore { try! NotesStore(directory: directory.appendingPathComponent(name)) }
        func action(_ note: CanvasNote, _ verb: String, item: UUID? = nil) -> String {
            "note:\(note.id.uuidString):\(verb)" + (item.map { ":\($0.uuidString)" } ?? "")
        }
        func bodyPoint(_ note: CanvasNote) -> CGPoint { CGPoint(x: note.x + 30, y: note.y + 40) }
        func headerPoint(_ note: CanvasNote) -> CGPoint { CGPoint(x: note.x + 18, y: note.y + 11) }
        func corner(_ note: CanvasNote) -> CGPoint { CGPoint(x: note.x + note.width - 5, y: note.y + note.height - 5) }
        let dark = HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2)
        let light = HUDModuleContentStyle(dark: false, accent: .systemGreen, contentsScale: 2.35)

        // Text edits and geometry use the same public canvas commands as the HUD.
        let textStore = store("text")
        let textCanvas = NotesCanvas(store: textStore, reduceMotion: { true })
        textCanvas.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 1200, height: 800), creationPoint: CGPoint(x: 45, y: 58))
        var editRequests: [NotesEditRequest] = []
        textCanvas.onEdit = { editRequests.append($0) }
        check(!textCanvas.mouseDown(at: CGPoint(x: -10, y: 50), clickCount: 1), "Input outside the module is not consumed")
        check(!textCanvas.mouseDown(at: CGPoint(x: 100, y: 10), clickCount: 1) && textCanvas.noteCount == 0,
              "The heading cannot create a note")
        textCanvas.perform(actionID: "tool:text")
        check(textStore.notes.count == 1, "Pressing Text immediately creates a note without a placing mode")
        check(!textCanvas.mouseDownInWorkspace(at: CGPoint(x: 500, y: 500), clickCount: 1), "Empty workspace input does not create additional notes")
        check(textStore.notes.count == 1 && textStore.notes[0].kind == .text,
              "The direct-add command creates and persists one text note")
        var textNote = textStore.notes[0]
        let createdAt = textNote.createdAt
        check(textNote.x == 45 && textNote.y == 58 && textCanvas.selectedNoteID == textNote.id,
              "A new note is selected at the requested canvas position")
        check(editRequests.count == 1 && editRequests[0].noteID == textNote.id
              && editRequests[0].itemID == nil && editRequests[0].multiline,
              "A new text note requests an inline multiline editor")
        textCanvas.finishEditing(editRequests[0], text: "Plan\n中文便笺 📝")
        check(textStore.notes[0].text == "Plan\n中文便笺 📝", "Editing preserves newlines and Unicode in the database")
        let readingViewport = textCanvas.contentViewport(for: textNote.id)!
        check(readingViewport.maxY == textNote.y + textNote.height - 8
              && readingViewport.height == editRequests[0].rect.height + 21,
              "Settled text uses the unused formatting footer and leaves only an eight-point bottom inset")
        textCanvas.setEditing(editRequests[0])
        check(textCanvas.contentViewport(for: textNote.id) == editRequests[0].rect,
              "Only an active editor reserves the exact footer used by its formatting controls")
        textCanvas.setEditing(nil)
        check(textCanvas.contentViewport(for: textNote.id) == readingViewport,
              "Leaving text editing restores the full reading area without changing the note's saved size")
        _ = textCanvas.mouseDownInWorkspace(at: bodyPoint(textNote), clickCount: 1)
        check(textCanvas.isDragging && editRequests.count == 1, "A single text-body click can drag without opening an editor")
        textCanvas.mouseUp()
        _ = textCanvas.mouseDownInWorkspace(at: bodyPoint(textNote), clickCount: 2)
        check(!textCanvas.isDragging && editRequests.count == 2 && editRequests.last?.text == "Plan\n中文便笺 📝",
              "Double-clicking existing text opens its current content")
        textCanvas.finishEditing(editRequests.last!, text: "Revised text")

        let start = headerPoint(textNote)
        _ = textCanvas.mouseDownInWorkspace(at: start, clickCount: 1)
        textCanvas.mouseDragged(to: CGPoint(x: start.x + 60, y: start.y + 35))
        check(textStore.notes[0].x == 45 && textStore.notes[0].y == 58,
              "Dragging does not write intermediate pointer positions to SQLite")
        textCanvas.mouseUp()
        textNote = textStore.notes[0]
        check(textNote.x == 105 && textNote.y == 93 && !textCanvas.isDragging,
              "Mouse-up commits the final dragged position")
        let oldSize = CGSize(width: textNote.width, height: textNote.height)
        let resizeStart = corner(textNote)
        _ = textCanvas.mouseDownInWorkspace(at: resizeStart, clickCount: 1)
        textCanvas.mouseDragged(to: CGPoint(x: resizeStart.x + 24, y: resizeStart.y + 17))
        check(textStore.notes[0].width == oldSize.width && textStore.notes[0].height == oldSize.height,
              "Resizing also commits only when the gesture ends")
        textCanvas.cancelInteraction()
        textNote = textStore.notes[0]
        check(textNote.width == oldSize.width + 24 && textNote.height == oldSize.height + 17 && !textCanvas.isDragging,
              "Section-close cleanup commits an active resize")
        check(textNote.createdAt == createdAt && textNote.text == "Revised text",
              "Editing and geometry preserve creation time and content")

        textCanvas.perform(actionID: action(textNote, "pin"))
        textNote = textStore.notes[0]
        let pinnedRect = CGRect(x: textNote.x, y: textNote.y, width: textNote.width, height: textNote.height)
        _ = textCanvas.mouseDownInWorkspace(at: headerPoint(textNote), clickCount: 1)
        textCanvas.mouseDragged(to: CGPoint(x: 380, y: 250))
        textCanvas.mouseUp()
        textCanvas.perform(actionID: action(textNote, "grow"))
        let pinned = textStore.notes[0]
        check(pinned.isPinned && CGRect(x: pinned.x, y: pinned.y, width: pinned.width, height: pinned.height) != pinnedRect,
              "Pinned notes remain draggable and resizable")
        check(textCanvas.accessibleActions.contains { $0.id == action(textNote, "grow") },
              "Pinned notes retain active resize actions")
        _ = textCanvas.mouseDownInWorkspace(at: bodyPoint(pinned), clickCount: 2)
        textCanvas.finishEditing(editRequests.last!, text: "Pinned text remains editable")
        check(textStore.notes[0].isPinned && textStore.notes[0].text == "Pinned text remains editable",
              "Pinning preserves visibility without preventing edits")
        textCanvas.perform(actionID: action(pinned, "pin"))
        check(!textStore.notes[0].isPinned, "Unpin restores normal manipulation")

        let persistentLayer = textCanvas.makeContent(for: .notes, style: dark)
        let persistedBeforeRepaint = textStore.notes
        L10n.language = .simplifiedChinese
        let repainted = textCanvas.makeContent(for: .notes, style: light)
        check(repainted === persistentLayer && textStore.notes == persistedBeforeRepaint,
              "Theme and language updates retain the canvas layer and all note data")
        check(textCanvas.accessibleActions.contains { $0.id == "tool:text" && $0.label == "文字" },
              "The text tool localizes without changing its action identity")
        let reloadedStore = try! NotesStore(directory: directory.appendingPathComponent("text"))
        let reopenedCanvas = NotesCanvas(store: reloadedStore)
        check(reloadedStore.notes == textStore.notes && reopenedCanvas.noteCount == 1,
              "Recreating store and canvas restores content, geometry, order, pin and creation time")

        let unopenedNotes = NotesCanvas(store: reloadedStore, notesSelected: false, reduceMotion: { true })
        func noteLayers(_ canvas: NotesCanvas) -> [CALayer] {
            canvas.workspaceLayer.sublayers!.flatMap { $0.sublayers ?? [] }.filter { $0.name?.hasPrefix("notes.note.") == true }
        }
        check(unopenedNotes.visibleNoteIDs.isEmpty && noteLayers(unopenedNotes).isEmpty,
              "Opening another module does not construct hidden unpinned note artwork")
        unopenedNotes.updateAppearance(style: light)
        unopenedNotes.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 1000, height: 640))
        check(noteLayers(unopenedNotes).isEmpty, "Hidden note scale and workspace changes do not decode card images")
        unopenedNotes.setPresentation(notesSelected: true, animated: false)
        check(noteLayers(unopenedNotes).count == reloadedStore.notes.count && !noteLayers(unopenedNotes)[0].isHidden,
              "Entering Notes materializes its deferred cards with the current style")

        // The retained workspace is visible before Notes is ever selected.
        let pinnedStore = store("pinned-appearance")
        let pinnedNote = CanvasNote(kind: .text, text: "Visible on every module", isPinned: true)
        try! pinnedStore.upsert(pinnedNote)
        let pinnedCanvas = NotesCanvas(store: pinnedStore, reduceMotion: { true })
        pinnedCanvas.setPresentation(notesSelected: false, animated: false)
        let pinnedCard = pinnedCanvas.workspaceLayer.sublayers!.flatMap { $0.sublayers ?? [] }
            .first { $0.name == "notes.note.\(pinnedNote.id.uuidString)" }!
        func pinStroke() -> CGColor? {
            pinnedCard.sublayers?.first?.sublayers?.compactMap { $0 as? CAShapeLayer }.first?.strokeColor
        }
        let blue = HUDModuleContentStyle(dark: true, accent: .systemBlue, contentsScale: 2)
        pinnedCanvas.updateAppearance(style: blue)
        check(!pinnedCanvas.notesSelected && pinnedCanvas.visibleNoteIDs == [pinnedNote.id]
              && !pinnedCard.isHidden && pinStroke() == blue.accent.cgColor,
              "A persisted pinned note uses the configured color before the Notes module is opened")
        let existingHeader = pinnedCard.sublayers!.first!
        pinnedCanvas.updateAppearance(style: blue)
        check(pinnedCard.sublayers?.first === existingHeader,
              "Unchanged workspace appearance does not rebuild notes on unrelated HUD updates")
        pinnedCanvas.updateAppearance(style: light)
        check(!pinnedCanvas.notesSelected && pinStroke() == light.accent.cgColor
              && pinnedCard.backgroundColor == NSColor(white: 0.92, alpha: 1).cgColor,
              "Pinned notes adopt new accent and light-mode colors while another module remains selected")
        pinnedCanvas.setPresentation(notesSelected: true, animated: false)
        _ = pinnedCanvas.makeContent(for: .notes, style: light)
        check(pinStroke() == light.accent.cgColor && pinnedStore.notes == [pinnedNote],
              "Entering Notes does not change pinned colors or persist any appearance into note data")
        pinnedCanvas.setPresentation(notesSelected: false, animated: false)
        pinnedCanvas.updateAppearance(style: blue)
        check(pinStroke() == blue.accent.cgColor
              && pinnedCard.backgroundColor == NSColor(white: 0.105, alpha: 1).cgColor,
              "Reopening the workspace on another module preserves the current dark appearance")

        textCanvas.deleteSelection()
        check(textStore.notes.count == 1 && textCanvas.pendingDeletionID == textNote.id,
              "Delete first requests confirmation without removing data")
        check(textCanvas.accessibleActions.contains { $0.id == action(textNote, "confirmDelete") }
              && textCanvas.accessibleActions.contains { $0.id == action(textNote, "cancelDelete") },
              "The pending note exposes a compact cancel and confirm pair")
        let deleteButton = textCanvas.accessibleActions.first { $0.id == action(textNote, "delete") }!.rect
        let confirmButton = textCanvas.accessibleActions.first { $0.id == action(textNote, "confirmDelete") }!.rect
        check(confirmButton.minY > deleteButton.maxY && confirmButton.minY - deleteButton.maxY <= 8
              && abs(confirmButton.maxX - deleteButton.maxX) < 1,
              "Delete confirmation is anchored immediately beneath the header cross")
        textCanvas.perform(actionID: action(textNote, "cancelDelete"))
        check(textStore.notes.count == 1 && textCanvas.pendingDeletionID == nil, "Cancel preserves the note")
        textCanvas.deleteSelection()
        textCanvas.perform(actionID: action(textNote, "confirmDelete"))
        check(textStore.notes.isEmpty && textCanvas.noteCount == 0 && !textCanvas.hasSelection,
              "Deleting selected text removes the database row and its canvas selection")
        L10n.language = .english

        // Selection must bring overlapping cards forward, including action-driven selection.
        let orderStore = store("order")
        let behind = CanvasNote(kind: .text, text: "Behind", x: 30, y: 55, zIndex: 2)
        let front = CanvasNote(kind: .text, text: "Front", x: 40, y: 60, zIndex: 7)
        try! orderStore.upsert(behind); try! orderStore.upsert(front)
        let orderCanvas = NotesCanvas(store: orderStore, reduceMotion: { true })
        _ = orderCanvas.mouseDownInWorkspace(at: CGPoint(x: 80, y: 105), clickCount: 1)
        orderCanvas.mouseUp()
        check(orderCanvas.selectedNoteID == front.id, "Overlapping pointer input selects the visually frontmost note")
        orderCanvas.perform(actionID: action(behind, "select"))
        check(orderCanvas.selectedNoteID == behind.id && orderStore.notes.last?.id == behind.id,
              "Accessibility selection also raises the chosen card persistently")
        orderCanvas.perform(actionID: action(front, "pin"))
        check(orderStore.notes.last?.id == front.id && orderStore.notes.last?.isPinned == true,
              "Pinning a rear card retains the z-order raised by its selection")
        let orderedReload = try! NotesStore(directory: directory.appendingPathComponent("order"))
        check(orderedReload.notes.last?.id == front.id, "Raised z-order survives a new database connection")

        // Checklists exercise child identities, ordering and overflow independently of note order.
        let todoStore = store("todo")
        let todoCanvas = NotesCanvas(store: todoStore, reduceMotion: { true })
        todoCanvas.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 1200, height: 800), creationPoint: CGPoint(x: 35, y: 52))
        var todoEdit: NotesEditRequest?
        todoCanvas.onEdit = { todoEdit = $0 }
        todoCanvas.perform(actionID: "tool:todo")
        var todo = todoStore.notes[0]
        check(todo.kind == .todo && todo.items.count == 1 && todoEdit?.itemID == todo.items[0].id
              && todoEdit?.multiline == false, "A new checklist opens an inline editor for its first item")
        todoCanvas.finishEditing(todoEdit!, text: "First")
        for title in ["Second", "Third"] {
            todoCanvas.perform(actionID: action(todo, "add"))
            todoCanvas.finishEditing(todoEdit!, text: title)
        }
        todo = todoStore.notes[0]
        let first = todo.items[0].id, second = todo.items[1].id, third = todo.items[2].id
        check(todo.items.map(\.text) == ["First", "Second", "Third"]
              && Set(todo.items.map(\.id)).count == 3, "Adding checklist items persists unique identities and entered text")
        todoCanvas.perform(actionID: action(todo, "check", item: second))
        check(todoStore.notes[0].items[1].isChecked, "Checking an item persists its completion state")
        todoCanvas.perform(actionID: action(todo, "check", item: second))
        check(!todoStore.notes[0].items[1].isChecked, "The same item can be unchecked")
        todoCanvas.perform(actionID: action(todo, "up", item: third))
        check(todoStore.notes[0].items.map(\.id) == [first, third, second], "Move-up changes checklist order without changing identities")
        todoCanvas.perform(actionID: action(todo, "down", item: first))
        check(todoStore.notes[0].items.map(\.id) == [third, first, second], "Move-down can reorder the first checklist item")
        todoCanvas.perform(actionID: action(todo, "up", item: third))
        todoCanvas.perform(actionID: action(todo, "down", item: second))
        check(todoStore.notes[0].items.map(\.id) == [third, first, second], "Moves beyond either checklist boundary are harmless")
        todoCanvas.perform(actionID: action(todo, "remove", item: second))
        check(todoStore.notes[0].items.map(\.id) == [third, first], "Deleting a checklist item preserves its neighbours")
        for index in 0..<7 {
            todoCanvas.perform(actionID: action(todo, "add"))
            todoCanvas.finishEditing(todoEdit!, text: "Extra \(index)")
        }
        let latest = todoStore.notes[0].items.last!.id
        let latestEdit = todoEdit!
        check(todoCanvas.workspaceBounds.contains(latestEdit.rect) && latestEdit.itemID == latest,
              "Adding beyond the visible rows scrolls the new item's editor into the canvas")
        let visibleBeforeScroll = Set(todoCanvas.accessibleActions.map(\.id))
        check(todoCanvas.scroll(at: bodyPoint(todo), delta: -30), "An overflowing checklist consumes continuous scrolling")
        check(Set(todoCanvas.accessibleActions.map(\.id)) != visibleBeforeScroll,
              "Scrolling exposes a different set of real checklist controls")
        todoCanvas.perform(actionID: action(todo, "editItem", item: third))
        check(todoEdit?.itemID == third && todoCanvas.workspaceBounds.contains(todoEdit!.rect),
              "Editing an offscreen item reveals the correct row")
        let todoReload = try! NotesStore(directory: directory.appendingPathComponent("todo"))
        check(todoReload.notes[0].items == todoStore.notes[0].items, "Checklist text, order and completion survive reopening")

        func descendants(_ root: CALayer) -> [CALayer] {
            [root] + (root.sublayers ?? []).flatMap(descendants)
        }
        func contentLines(_ root: CALayer) -> [CATextLayer] {
            descendants(root).compactMap { $0 as? CATextLayer }.filter { $0.name?.hasPrefix("notes.content.line.") == true }
        }
        let smoothStore = store("smooth-scroll")
        let longText = (0..<100).map { "Line \($0): 中文便笺 👩🏽‍💻 wraps without ellipsis." }.joined(separator: "\n")
        let longNote = CanvasNote(kind: .text, text: longText, x: 25, y: 35, width: 190, height: 135, isPinned: true)
        try! smoothStore.upsert(longNote)
        let smooth = NotesCanvas(store: smoothStore, reduceMotion: { true })
        let initialLayers = noteLayers(smooth)
        let longCard = initialLayers[0]
        let viewportLayer = descendants(longCard).first { $0.name == "notes.content.viewport" }!
        let scrollingLayer = descendants(longCard).first { $0.name == "notes.content.scroll" }!
        let firstLine = contentLines(longCard).first!
        let measuredBefore = smooth.textLayoutBuildCount
        let database = directory.appendingPathComponent("smooth-scroll/notes.sqlite3")
        let bytesBeforeScroll = try! Data(contentsOf: database)
        let dateBeforeScroll = try! FileManager.default.attributesOfItem(atPath: database.path)[.modificationDate] as! Date
        check(viewportLayer.masksToBounds && contentLines(longCard).count < 12,
              "A long text note clips a bounded set of visible line layers instead of allocating a full-document bitmap")
        for _ in 0..<4 { check(smooth.scroll(at: bodyPoint(longNote), delta: 0.375), "Every fractional text delta is consumed") }
        check(smooth.scrollOffset(for: longNote.id) == 1.5 && scrollingLayer.bounds.minY == 1.5,
              "Subpoint trackpad deltas accumulate continuously in the retained content origin")
        check(contentLines(longCard).contains { $0 === firstLine } && noteLayers(smooth)[0] === longCard
              && smooth.textLayoutBuildCount == measuredBefore,
              "Steady scrolling retains the card and visible lines without remeasuring document text")
        check(!smooth.scroll(at: bodyPoint(longNote), delta: .nan) && !smooth.scroll(at: bodyPoint(longNote), delta: .infinity),
              "Invalid scroll deltas cannot corrupt content geometry")
        _ = smooth.scroll(at: bodyPoint(longNote), delta: 100_000)
        let lastText = contentLines(longCard).compactMap { ($0.string as? NSAttributedString)?.string }.joined()
        check(lastText.contains("Line 99") && contentLines(longCard).allSatisfy { $0.truncationMode == .none },
              "Continuous scrolling reaches the final wrapped text without substituting ellipses")
        let bottomOffset = smooth.scrollOffset(for: longNote.id)
        _ = smooth.scroll(at: bodyPoint(longNote), delta: 100)
        check(smooth.scrollOffset(for: longNote.id) == bottomOffset, "Scrolling past the text end clamps without an idle animation")
        smooth.setPresentation(notesSelected: false, animated: false)
        _ = smooth.scroll(at: bodyPoint(longNote), delta: -0.25)
        check(smooth.scrollOffset(for: longNote.id) == bottomOffset - 0.25,
              "Pinned text scrolls continuously while another module is selected")
        let bytesAfterScroll = try! Data(contentsOf: database)
        let dateAfterScroll = try! FileManager.default.attributesOfItem(atPath: database.path)[.modificationDate] as! Date
        check(bytesAfterScroll == bytesBeforeScroll && dateAfterScroll == dateBeforeScroll
              && smoothStore.notes == [longNote], "Scrolling and pin presentation never write note content, geometry or view offsets")
        check(smooth.activeAnimationCount == 0, "Continuous scrolling adds no animation clock")

        let buildsBeforeResize = smooth.textLayoutBuildCount
        for index in 0..<70 {
            smooth.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 110 + CGFloat(index) / 2, height: 300))
            check(smooth.cachedTextLayoutCount == 1,
                  "Resizing one long note evicts its obsolete width layout instead of retaining historical line ranges")
        }
        check(smooth.textLayoutBuildCount >= buildsBeforeResize + 70
              && smoothStore.notes == [longNote],
              "More than 64 distinct reflows retain a single current layout without changing saved text or geometry")

        let budgetStore = store("layout-byte-budget")
        let budgetText = (0..<65).map { "Line \($0)" }.joined(separator: "\n")
        try! budgetStore.upsert(CanvasNote(kind: .text, text: budgetText + "\nDocument 0",
            x: 12, y: 12, width: 190, height: 135, zIndex: 0))
        // Rich line layout retains origins/heights as well as NSRange values.
        // Size the fixture for two real documents; Swift array capacity differs
        // across toolchains, so a fixed 4 KB may now describe one oversized item.
        let oneDocumentBytes = NotesCanvas(store: budgetStore, reduceMotion: { true }).cachedTextLayoutRangeBytes
        let twoDocumentBudget = oneDocumentBytes * 2
        for index in 1..<6 {
            try! budgetStore.upsert(CanvasNote(kind: .text, text: budgetText + "\nDocument \(index)",
                x: 12, y: 12, width: 190, height: 135, zIndex: index))
        }
        let budgetCanvas = NotesCanvas(store: budgetStore, textLayoutRangeBudget: twoDocumentBudget, reduceMotion: { true })
        check(oneDocumentBytes > 0 && budgetCanvas.cachedTextLayoutRangeBytes <= twoDocumentBudget
                && budgetCanvas.cachedTextLayoutCount > 0 && budgetCanvas.cachedTextLayoutCount < 6,
              "The range-byte budget evicts older distinct documents before reaching the entry-count limit")
        let oversized = NotesCanvas(store: smoothStore, textLayoutRangeBudget: 64, reduceMotion: { true })
        let oversizedBuilds = oversized.textLayoutBuildCount
        check(oversized.cachedTextLayoutCount == 1 && oversized.cachedTextLayoutRangeBytes > 64,
              "One oversized current document retains all line ranges rather than truncating text to the cache budget")
        _ = oversized.scroll(at: bodyPoint(longNote), delta: 0.375)
        check(oversized.cachedTextLayoutCount == 1 && oversized.textLayoutBuildCount == oversizedBuilds,
              "An oversized current layout is retained across fractional scrolling instead of repeatedly typesetting it")

        let wrappedStore = store("wrapped-todo")
        let wrappedTitle = "跨行任务 👨‍👩‍👧‍👦: a long checklist task must wrap onto several lines without truncating its content."
        let wrappedItem = NoteChecklistItem(text: wrappedTitle, isChecked: true)
        let shortItem = NoteChecklistItem(text: "Second row")
        let wrappedNote = CanvasNote(kind: .todo, items: [wrappedItem, shortItem]
            + (0..<90).map { NoteChecklistItem(text: "Task \($0)") }, x: 20, y: 25, width: 230, height: 210)
        try! wrappedStore.upsert(wrappedNote)
        let wrapped = NotesCanvas(store: wrappedStore, reduceMotion: { true })
        let wrappedCard = noteLayers(wrapped)[0]
        let firstRow = descendants(wrappedCard).first { $0.name == "notes.todo.row." + wrappedItem.id.uuidString }!
        let firstRowLines = contentLines(firstRow)
        check(firstRowLines.count > 1 && firstRow.frame.height > 25,
              "Long Chinese and emoji tasks occupy measured multiline rows")
        check(firstRowLines.allSatisfy { line in
            guard let text = line.string as? NSAttributedString, text.length > 0 else { return false }
            return text.attribute(.strikethroughStyle, at: 0, effectiveRange: nil) as? Int == NSUnderlineStyle.single.rawValue
        }, "Every visible line of a completed task has a real text strikethrough")
        let rowEditID = action(wrappedNote, "editItem", item: wrappedItem.id)
        let secondEditID = action(wrappedNote, "editItem", item: shortItem.id)
        let firstRect = wrapped.accessibleActions.first { $0.id == rowEditID }!.rect
        let secondRect = wrapped.accessibleActions.first { $0.id == secondEditID }!.rect
        check(firstRect.height > 22 && secondRect.minY >= firstRect.maxY,
              "Variable row heights keep multiline editing and neighbouring task hit regions disjoint")
        let wrappedBuilds = wrapped.textLayoutBuildCount
        let rowIdentity = firstRow
        _ = wrapped.scroll(at: bodyPoint(wrappedNote), delta: 8.25)
        let clippedFirst = wrapped.accessibleActions.first { $0.id == rowEditID }!.rect
        let body = wrapped.contentViewport(for: wrappedNote.id)!
        check(body.contains(clippedFirst) && abs(clippedFirst.minY - body.minY) < 0.001,
              "Partially visible task editing and accessibility rectangles clip to the same body viewport")
        check(descendants(wrappedCard).contains { $0 === rowIdentity } && wrapped.textLayoutBuildCount == wrappedBuilds,
              "Fractional checklist scrolling retains visible row layers and their measured text")
        check(wrapped.cachedTextLayoutCount <= NotesCanvas.textLayoutCacheLimit
              && descendants(wrappedCard).filter { $0.name?.hasPrefix("notes.todo.row.") == true }.count < 10,
              "Large checklists keep both the line-layout cache and visible row layer count bounded")
        wrapped.perform(actionID: action(wrappedNote, "check", item: wrappedItem.id))
        let unchecked = descendants(wrappedCard).first { $0.name == "notes.todo.row." + wrappedItem.id.uuidString }!
        check(contentLines(unchecked).allSatisfy { ($0.string as? NSAttributedString)?.attribute(.strikethroughStyle, at: 0, effectiveRange: nil) == nil },
              "Unchecking removes strikethrough without changing task content")
        var wrappedEdit: NotesEditRequest?
        wrapped.onEdit = { wrappedEdit = $0 }
        let lastItem = wrappedNote.items.last!
        wrapped.perform(actionID: action(wrappedNote, "editItem", item: lastItem.id))
        check(wrappedEdit?.itemID == lastItem.id && body.contains(wrappedEdit!.rect),
              "Editing an offscreen variable row reveals the exact clipped editor region")
        check(wrappedEdit!.rect == wrapped.accessibleActions.first { $0.id == action(wrappedNote, "editItem", item: lastItem.id) }!.rect,
              "Task drawing geometry, native editor requests and accessibility share the same measured row rectangle")
        wrapped.perform(actionID: action(wrappedNote, "shrink"))
        let resizedBody = wrapped.contentViewport(for: wrappedNote.id)!
        check(wrapped.accessibleActions.filter { $0.id.split(separator: ":").count == 4 }.allSatisfy { resizedBody.contains($0.rect) },
              "Resize reflow clamps scrolling and clips every task action to the resized content area")
        let finalWrapped = try! NotesStore(directory: directory.appendingPathComponent("wrapped-todo"))
        check(finalWrapped.notes[0].items[0].text == wrappedTitle && finalWrapped.notes[0].items.count == 92,
              "Wrapping, scroll offsets and completion styling preserve all original checklist records")

        // Both image entry paths end in the same movable/resizable persistent object.
        let imageStore = store("image")
        let imageCanvas = NotesCanvas(store: imageStore, reduceMotion: { true })
        imageCanvas.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 1200, height: 800), creationPoint: CGPoint(x: 72, y: 92))
        var imageRequests: [CGPoint] = []
        imageCanvas.onChooseImage = { imageRequests.append($0) }
        imageCanvas.perform(actionID: "tool:image")
        check(imageRequests.count == 1 && imageCanvas.noteCount == 0, "The Image toolbar opens a chooser without a placeholder record")
        check(imageRequests.last == CGPoint(x: 72, y: 92), "The direct image chooser uses the workspace insertion position")
        imageCanvas.importImage(data: Data("invalid image".utf8), at: CGPoint(x: 72, y: 92))
        check(imageStore.notes.isEmpty && imageCanvas.noteCount == 0, "Invalid dropped image data cannot create a broken card")
        let png = imageData()
        imageCanvas.importImage(data: png, at: CGPoint(x: 72, y: 92))
        var image = imageStore.notes[0]
        check(image.kind == .image && imageCanvas.selectedNoteID == image.id
              && imageStore.imageURL(for: image) != nil, "Image bytes become an owned persistent image note")
        let imageStart = headerPoint(image)
        _ = imageCanvas.mouseDownInWorkspace(at: imageStart, clickCount: 1)
        imageCanvas.mouseDragged(to: CGPoint(x: imageStart.x - 30, y: imageStart.y - 20))
        imageCanvas.mouseUp()
        image = imageStore.notes[0]
        check(image.x == 42 && image.y == 72, "Image notes use the same direct dragging interaction")
        let imageWidth = image.width
        imageCanvas.perform(actionID: action(image, "grow"))
        check(imageStore.notes[0].width > imageWidth, "Image notes can be resized through the shared canvas controls")
        let file = directory.appendingPathComponent("source.png")
        try! png.write(to: file)
        imageCanvas.importImages(urls: [file], at: CGPoint(x: 190, y: 120))
        try! FileManager.default.removeItem(at: file)
        check(imageStore.notes.count == 2 && imageStore.notes.allSatisfy { imageStore.imageURL(for: $0) != nil },
              "Chosen files are copied into note storage and survive removal of the source image")
        let imageReload = try! NotesStore(directory: directory.appendingPathComponent("image"))
        check(NotesCanvas(store: imageReload).noteCount == 2 && imageReload.notes == imageStore.notes,
              "Image cards restore with their full geometry and order")

        // Pinned notes remain independent of the center section; hidden notes do not intercept clicks.
        orderCanvas.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 1400, height: 900))
        orderCanvas.setPresentation(notesSelected: false, animated: false)
        check(orderCanvas.visibleNoteIDs == [front.id], "Only pinned notes remain visible on other tabs")
        check(!orderCanvas.accessibleActions.contains { $0.id.hasPrefix("tool:") }
              && !orderCanvas.accessibleActions.contains { $0.id == action(behind, "select") },
              "Other tabs omit Notes toolbar and unpinned-note accessibility controls")
        let pinnedStart = headerPoint(orderStore.notes.first { $0.id == front.id }!)
        _ = orderCanvas.mouseDownInWorkspace(at: pinnedStart, clickCount: 1)
        orderCanvas.mouseDragged(to: CGPoint(x: pinnedStart.x + 750, y: pinnedStart.y + 400))
        orderCanvas.mouseUp()
        let movedPinned = orderStore.notes.first { $0.id == front.id }!
        check(movedPinned.x > 700 && movedPinned.y > 400 && movedPinned.isPinned,
              "Pinned notes can be moved across the full display while another module is selected")
        check(orderCanvas.containsWorkspacePoint(headerPoint(movedPinned)), "Full-screen note hit testing follows saved geometry")
        orderCanvas.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 400, height: 334))
        check(orderStore.notes.first { $0.id == front.id } == movedPinned,
              "Temporary screen-size clamping never rewrites saved note geometry")
        orderCanvas.setPresentation(notesSelected: true, animated: false)
        check(orderCanvas.visibleNoteIDs == [front.id, behind.id], "Returning to Notes reveals every note")
        check(orderCanvas.workspaceLayer.superlayer == nil && orderCanvas.layer.sublayers?.contains(where: { $0 === orderCanvas.workspaceLayer }) != true,
              "Workspace is a separate root that cannot inherit the center's clipping mask")
        let animated = NotesCanvas(store: orderStore, reduceMotion: { false })
        animated.perform(actionID: action(front, "pin"))
        check(animated.activeAnimationCount > 0, "Note actions have finite depth motion")
        animated.cancelAnimations()
        check(animated.activeAnimationCount == 0, "Closing cancels all note action animations")

        let presentationStore = store("section-presentation")
        let looseNote = CanvasNote(kind: .text, text: "Arrive with Notes")
        let fixedNote = CanvasNote(kind: .text, text: "Stay pinned", isPinned: true)
        try! presentationStore.upsert(looseNote); try! presentationStore.upsert(fixedNote)
        let storedPresentationNotes = presentationStore.notes
        let presentation = NotesCanvas(store: presentationStore, notesSelected: false, reduceMotion: { false })
        presentation.setPresentation(notesSelected: true, direction: CGPoint(x: 0, y: -1))
        let cards = noteLayers(presentation)
        let incoming = cards.first { $0.name == "notes.note.\(looseNote.id.uuidString)" }!
        let stationary = cards.first { $0.name == "notes.note.\(fixedNote.id.uuidString)" }!
        let arrival = incoming.animation(forKey: "notes.section") as! CAAnimationGroup
        let arrivalTravel = arrival.animations!.first as! CABasicAnimation
        let arrivalOpacity = arrival.animations!.last as! CABasicAnimation
        check(arrival.duration == HUDModuleContent.transitionDuration
              && (arrivalTravel.fromValue as! NSValue).caTransform3DValue.m42 < 0
              && (arrivalOpacity.fromValue as! NSNumber).floatValue == 0,
              "Map-to-Notes cards start transparent and travel with the center's direction and duration")
        check(stationary.animationKeys()?.isEmpty != false && !stationary.isHidden && stationary.opacity == 1,
              "Pinned notes remain fully visible without joining the section arrival")
        presentation.setPresentation(notesSelected: false, direction: CGPoint(x: 0, y: 1))
        check(incoming.animation(forKey: "notes.section")?.duration == HUDModuleContent.transitionDuration
              && !incoming.isHidden && incoming.opacity == 0,
              "Leaving Notes retains outgoing cards for the whole matching fade")
        presentation.setPresentation(notesSelected: true, direction: CGPoint(x: 0, y: -1))
        presentation.setPresentation(notesSelected: true, animated: false)
        check(incoming.animationKeys()?.isEmpty != false && !incoming.isHidden && incoming.opacity == 1,
              "Immediate settlement after a reversed request removes the card track without losing its final visibility")
        check(presentationStore.notes == storedPresentationNotes,
              "Section presentation changes never rewrite note data or pin state")

        let unavailable = NotesCanvas(store: nil, error: "Read-only storage")
        _ = unavailable.mouseDown(at: CGPoint(x: 80, y: 80), clickCount: 1)
        check(unavailable.noteCount == 0, "Unavailable persistence cannot create a note that appears saved")

        let drawingStore = store("drawing")
        let drawingCanvas = NotesCanvas(store: drawingStore, reduceMotion: { true })
        drawingCanvas.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 800, height: 600), creationPoint: CGPoint(x: 40, y: 60))
        var drawingEvents: [NotesCanvasEvent] = []
        drawingCanvas.onAction = { drawingEvents.append($0) }
        drawingCanvas.perform(actionID: "tool:drawing")
        let drawingNote = drawingStore.notes[0]
        check(drawingNote.kind == .drawing && drawingNote.drawing == NotesDrawing(),
              "Drawing toolbar creates a persistent spatial drawing note")
        let drawingDatabase = directory.appendingPathComponent("drawing/notes.sqlite3")
        let drawingBytes = try! Data(contentsOf: drawingDatabase)
        let drawingStart = CGPoint(x: drawingNote.x + 35, y: drawingNote.y + 55)
        drawingCanvas.mouseMoved(at: drawingStart)
        _ = drawingCanvas.scroll(at: drawingStart, delta: 8)
        _ = drawingCanvas.mouseDownInWorkspace(at: drawingStart, clickCount: 1)
        for step in 1...12 { drawingCanvas.mouseDragged(to: CGPoint(x: drawingStart.x + Double(step) * 5, y: drawingStart.y + Double(step) * 2)) }
        check(try! Data(contentsOf: drawingDatabase) == drawingBytes && drawingStore.notes[0].drawing?.strokes.isEmpty == true,
              "Brush preview, thickness scrolling and live stroke samples never write to SQLite")
        drawingCanvas.mouseUp()
        check(drawingStore.notes[0].drawing?.strokes.count == 1
              && drawingStore.notes[0].drawing?.strokes[0].width == 10,
              "Mouse-up commits one vector stroke with the wheel-adjusted brush size")
        check(drawingEvents == [.createdDrawing, .drawingEdited], "Drawing logs successful commits rather than pointer or progress samples")
        let drawingCard = drawingCanvas.workspaceLayer.sublayers!.flatMap { $0.sublayers ?? [] }
            .first { $0.name == "notes.note.\(drawingNote.id.uuidString)" }!
        let retainedDrawingHeader = drawingCard.sublayers!.first!
        let savedDrawing = drawingStore.notes[0]
        for index in 0..<20 { drawingCanvas.setDrawingColor(NSColor(calibratedHue: Double(index) / 20, saturation: 1, brightness: 1, alpha: 1)) }
        check(drawingCard.sublayers?.first === retainedDrawingHeader && drawingStore.notes[0] == savedDrawing,
              "Color-wheel dragging changes only the retained swatch and never rebuilds stored stroke geometry or saves the note")
        check(drawingCanvas.rightMouseDown(at: drawingStart), "Right-click inside a drawing switches to the eraser")
        _ = drawingCanvas.mouseDownInWorkspace(at: drawingStart, clickCount: 1); drawingCanvas.mouseUp()
        check(drawingStore.notes[0].drawing?.strokes.isEmpty == true,
              "The eraser removes intersecting strokes and commits at gesture end")
        drawingCanvas.setVisible(false)
        check(drawingCanvas.activeAnimationCount == 0, "Hiding a drawing introduces no animation or sampler lifetime")
        return count
    }

    private static func imageData() -> Data {
        let context = CGContext(data: nil, width: 4, height: 3, bitsPerComponent: 8, bytesPerRow: 0,
                                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.setFillColor(NSColor.systemYellow.cgColor)
        context.fill(CGRect(x: 0, y: 0, width: 4, height: 3))
        let output = NSMutableData()
        let destination = CGImageDestinationCreateWithData(output, "public.png" as CFString, 1, nil)!
        CGImageDestinationAddImage(destination, context.makeImage()!, nil)
        precondition(CGImageDestinationFinalize(destination))
        return output as Data
    }
}
