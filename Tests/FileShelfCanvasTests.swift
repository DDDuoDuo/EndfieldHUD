import AppKit
import QuartzCore

enum FileShelfCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldCharge-ShelfCanvasTests-\(UUID().uuidString)")
        try! FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let previousLanguage = L10n.language
        defer { L10n.language = previousLanguage }
        L10n.language = .english
        let store = try! FileShelfStore(directory: directory.appendingPathComponent("metadata"))
        let canvas = FileShelfCanvas(store: store)
        let dark = HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2)
        let light = HUDModuleContentStyle(dark: false, accent: .systemGreen, contentsScale: 2.35)
        func action(_ id: UUID, _ verb: String) -> String { "shelf:\(id.uuidString):\(verb)" }
        func center(_ rect: CGRect) -> CGPoint { CGPoint(x: rect.midX, y: rect.midY) }
        func actions(_ verb: String) -> [ShelfCanvasAction] { canvas.accessibleActions.filter { $0.id.hasSuffix(":" + verb) } }
        func animationCount(_ layer: CALayer) -> Int {
            (layer.animationKeys()?.count ?? 0) + (layer.sublayers ?? []).reduce(0) { $0 + animationCount($1) }
        }
        func descendants(_ layer: CALayer) -> [CALayer] {
            [layer] + (layer.sublayers ?? []).flatMap(descendants)
        }
        var chooses = 0
        var previews: [UUID] = []
        var reveals: [UUID] = []
        var addedNames: [String] = []
        var removedNames: [String] = []
        var clearedCounts: [Int] = []
        canvas.onItemsAdded = { addedNames += $0 }
        canvas.onItemRemoved = { removedNames.append($0) }
        canvas.onShelfCleared = { clearedCounts.append($0) }
        canvas.onChooseFiles = { chooses += 1 }
        canvas.onPreview = { previews.append($0) }
        canvas.onReveal = { reveals.append($0) }
        check(canvas.itemCount == 0 && canvas.scrollOffset == 0, "An empty shelf rests at the beginning of its collection")
        check(!canvas.mouseDown(at: CGPoint(x: -1, y: 45), clickCount: 1), "Input outside the shelf is not consumed")
        check(canvas.mouseDown(at: CGPoint(x: 100, y: 10), clickCount: 1) && canvas.itemCount == 0, "Heading input cannot add an object")
        canvas.perform(actionID: "shelf:add")
        check(chooses == 1 && store.items.isEmpty, "Add files opens the native picker without creating a fake reference")
        canvas.perform(actionID: "shelf:confirmClear")
        check(store.items.isEmpty, "An unrequested clear confirmation is harmless")

        var urls: [URL] = []
        for index in 0..<13 {
            let name = index == 0 ? "中文 and a long original filename document.txt" : "Document \(index).txt"
            let url = directory.appendingPathComponent(name)
            try! Data("Contents of original \(index)\n".utf8).write(to: url)
            urls.append(url)
        }
        check(canvas.importURLs(urls), "Normal Finder file URLs import into the shelf")
        check(addedNames == urls.map(\.lastPathComponent), "Successful import reports only basenames for the action log")
        check(canvas.itemCount == 13 && store.items.count == 13,
              "Thirteen references remain available in one continuous collection")
        let deferred = FileShelfCanvas(store: store, reduceMotion: { true })
        func deferredCards() -> [CALayer] { descendants(deferred.layer).filter { $0.name?.hasPrefix("shelf.card.") == true } }
        check(deferred.itemCount == 13 && deferredCards().isEmpty,
              "Constructing an unopened shelf retains metadata without preparing Finder icon cards")
        deferred.updateRenderScale(3); deferred.refreshFromStore(); deferred.deactivate()
        check(deferredCards().isEmpty && deferred.accessibleActions.contains { $0.id.hasSuffix(":select") },
              "Hidden shelf scale and state updates preserve semantic actions without rendering cards")
        _ = deferred.makeContent(for: .fileShelf, style: HUDModuleContentStyle(dark: true, accent: .cyan, contentsScale: 3))
        let firstPreparedCard = deferredCards().first!
        let preparedLabel = descendants(firstPreparedCard).compactMap { $0 as? CATextLayer }.first!
        check(deferredCards().count <= 8 && preparedLabel.contentsScale == HUDRenderScale.contentScale(for: preparedLabel, baseScale: 3),
              "First shelf presentation prepares only intersecting rows at the selected backing scale")
        deferred.activate()
        check(deferredCards().first === firstPreparedCard,
              "Enabling shelf input after its reveal keeps the prepared card artwork intact")
        deferred.deactivate(); deferred.updateRenderScale(2); deferred.refreshFromStore()
        check(deferredCards().first === firstPreparedCard,
              "Hiding and updating the shelf does not rebuild its retained cards")
        deferred.activate()
        check(deferredCards().count <= 8 && deferredCards().first !== firstPreparedCard,
              "Shelf activation refreshes its displayed cards after deferred state updates")
        deferred.deactivate()
        check(canvas.scrollOffset == 312 && canvas.selectedID == store.items.last?.id,
              "Import reveals and selects the last added reference")
        check(actions("select").count == 7 && canvas.cardRect(for: store.items[0].id) == nil
              && canvas.cardRect(for: store.items.last!.id)?.height == 74,
              "The bottom shows only intersecting cards and fully reveals the final reference")
        check(canvas.importURLs([urls[0]]) && store.items.count == 13,
              "Dropping an already referenced URL succeeds without duplicating cards")
        check(addedNames.count == 13, "Duplicate drops do not fabricate added-file events")
        let importedSelection = canvas.selectedIDs
        canvas.scrollBy(-CGFloat.greatestFiniteMagnitude)
        check(canvas.scrollOffset == 0 && canvas.selectedIDs == importedSelection,
              "Scrolling to the beginning preserves the selected references, including offscreen members")
        check(!canvas.accessibleActions.contains { $0.id == "shelf:previous" || $0.id == "shelf:next" },
              "The shelf exposes no previous or next page controls")
        canvas.perform(actionID: "shelf:previous"); canvas.perform(actionID: "shelf:next")
        check(canvas.scrollOffset == 0 && canvas.selectedIDs == importedSelection,
              "Obsolete page actions cannot jump the viewport or clear a selection")
        check(actions("select").count == 8, "The first viewport exposes six full cards and the clipped edge of the following row")
        let first = store.items[0]
        for item in store.items.prefix(6) {
            let rect = canvas.cardRect(for: item.id)!
            check(FileShelfCanvas.contentRect.contains(rect), "Every visible hit rectangle is clipped to the collection viewport")
            check(canvas.itemAt(point: CGPoint(x: rect.minX + 18, y: rect.minY + 22)) == item.id,
                  "Each card body resolves to its native drag candidate")
        }
        let firstRect = canvas.cardRect(for: first.id)!
        canvas.perform(actionID: action(first.id, "select"))
        check(canvas.accessibleActions.first { $0.id == action(first.id, "select") }?.label.contains(first.name) == true,
              "Accessibility preserves a filename that is visually truncated")
        for control in canvas.accessibleActions.filter({ $0.id.hasPrefix("shelf:\(first.id.uuidString):") && !$0.id.hasSuffix(":select") }) {
            check(canvas.itemAt(point: center(control.rect)) == nil,
                  "Quick Look, reveal and remove controls cannot accidentally initiate a drag")
        }
        let body = CGPoint(x: firstRect.minX + 40, y: firstRect.minY + 20)
        _ = canvas.mouseDown(at: body, clickCount: 1)
        check(canvas.selectedID == first.id && previews.isEmpty, "A single body click selects without opening Quick Look")
        _ = canvas.mouseDown(at: body, clickCount: 2)
        check(previews == [first.id], "Double-click dispatches Quick Look for the correct reference")
        canvas.revealSelection()
        canvas.previewSelection()
        check(reveals == [first.id] && previews == [first.id, first.id],
              "Keyboard preview and reveal use the selected persistent identity")
        check(canvas.icon(for: first.id) != nil, "Cards and native drag sessions share a cached Finder icon")
        check(canvas.itemAt(point: CGPoint(x: 199, y: 50)) == nil,
              "Spacing between object cards never produces a drag candidate")

        let third = store.items[2], fourth = store.items[3]
        let thirdBody = CGPoint(x: canvas.cardRect(for: third.id)!.minX + 30, y: canvas.cardRect(for: third.id)!.minY + 20)
        let fourthBody = CGPoint(x: canvas.cardRect(for: fourth.id)!.minX + 30, y: canvas.cardRect(for: fourth.id)!.minY + 20)
        _ = canvas.mouseDown(at: thirdBody, clickCount: 1, modifiers: [.shift])
        canvas.finishPointerSelection()
        check(canvas.selectedIDs == Set(store.items.prefix(3).map(\.id)), "Shift-click selects the inclusive range from the first selected card")
        check(canvas.dragSelection(primaryID: third.id) == store.items.prefix(3).map(\.id),
              "Dragging a selected range emits every reference once in stable shelf order")
        _ = canvas.mouseDown(at: body, clickCount: 1)
        check(canvas.selectedIDs.count == 3, "Mouse-down on a selected card preserves its group until drag or mouse-up")
        canvas.beginSelectionDrag(); canvas.finishPointerSelection()
        check(canvas.selectedIDs.count == 3, "Starting a native group drag cancels deferred single-card collapse")
        _ = canvas.mouseDown(at: fourthBody, clickCount: 1, modifiers: [.command])
        check(canvas.selectedIDs.count == 4, "Command-click can add a separate card to the current drag selection")
        _ = canvas.mouseDown(at: fourthBody, clickCount: 1, modifiers: [.command])
        check(canvas.selectedIDs.count == 3 && !canvas.selectedIDs.contains(fourth.id), "Command-click can remove a card from the group")
        check(canvas.dragSelection(primaryID: fourth.id) == [fourth.id], "An unselected drag candidate never exports a different selected group")
        _ = canvas.mouseDown(at: body, clickCount: 1); canvas.finishPointerSelection()
        check(canvas.selectedIDs == [first.id], "A plain completed click collapses the group to the clicked card")
        _ = canvas.mouseDown(at: thirdBody, clickCount: 1, modifiers: [.shift])
        _ = canvas.mouseDown(at: CGPoint(x: 200, y: 280), clickCount: 1)
        check(canvas.selectedIDs.isEmpty && canvas.selectedID == nil, "Clicking empty shelf space clears all selected cards")
        canvas.perform(actionID: action(first.id, "select"))

        let persistentLayer = canvas.makeContent(for: .fileShelf, style: dark)
        let previousIDs = store.items.map(\.id)
        let previousPaths = store.items.map(\.lastKnownPath)
        canvas.setDropTarget(true)
        canvas.setDropTarget(true)
        canvas.setDropTarget(false)
        canvas.updateRenderScale(3)
        L10n.language = .simplifiedChinese
        let updated = canvas.makeContent(for: .fileShelf, style: light)
        check(updated === persistentLayer && store.items.map(\.id) == previousIDs && store.items.map(\.lastKnownPath) == previousPaths,
              "Theme, language, backing scale and drop feedback preserve the retained layer and references")
        check(canvas.selectedID == first.id && canvas.accessibleActions.contains { $0.id == "shelf:add" && $0.label == "添加文件" },
              "Localized actions retain both their stable IDs and the selected item")
        check(animationCount(canvas.layer) == 0, "The shelf adds no idle animation or per-card transition tracks")
        L10n.language = .english

        check(!canvas.scroll(at: CGPoint(x: 20, y: 12), delta: 1), "Scrolling the heading does not move the collection")
        check(!canvas.scroll(at: CGPoint(x: 20, y: 50), delta: .nan), "Nonfinite scroll input cannot corrupt the offset")
        let retainedCard = descendants(canvas.layer).first { $0.name == "shelf.card.\(first.id.uuidString)" }!
        let cardY = retainedCard.position.y
        for _ in 0..<30 { _ = canvas.scroll(at: CGPoint(x: 20, y: 50), delta: 0.5) }
        check(canvas.scrollOffset == 15 && retainedCard.position.y == cardY - 15
              && descendants(canvas.layer).contains { $0 === retainedCard },
              "Fractional trackpad movement continuously repositions retained card artwork")
        _ = canvas.scroll(at: CGPoint(x: 20, y: 50), delta: -10)
        check(canvas.scrollOffset == 5 && canvas.scroll(at: CGPoint(x: 20, y: 50), delta: 30) && canvas.scrollOffset == 35,
              "Reversing direction applies the exact delta without an accumulated page threshold")
        check(canvas.scroll(at: CGPoint(x: 20, y: 50), delta: 12) && canvas.scrollOffset == 47,
              "Successive wheel events remain a continuous distance")
        canvas.scrollBy(13)
        let partial = canvas.cardRect(for: first.id)!
        let clippedPreview = canvas.accessibleActions.first { $0.id == action(first.id, "preview") }!
        check(partial.minY == FileShelfCanvas.contentRect.minY && partial.height == 18
              && clippedPreview.rect.minY == 40 && clippedPreview.rect.maxY == 53,
              "Partly clipped cards keep controls attached to the full card geometry instead of shifting them below its visible edge")
        check(canvas.itemAt(point: center(clippedPreview.rect)) == nil
              && canvas.itemAt(point: CGPoint(x: 30, y: 38)) == nil,
              "Clipped command slots and artwork outside the viewport cannot begin a native drag")
        check(canvas.accessibleActions.filter { $0.id.hasPrefix("shelf:") && $0.id.split(separator: ":").count == 3 }
              .allSatisfy { FileShelfCanvas.contentRect.contains($0.rect) },
              "Every visible card and inline accessibility control stays inside the collection mask")
        check(animationCount(canvas.layer) == 0, "Wheel movement adds no page transitions or idle animation")
        canvas.scrollBy(CGFloat.greatestFiniteMagnitude)
        check(canvas.scrollOffset == 312 && canvas.cardRect(for: store.items.last!.id)?.height == 74,
              "Continuous scrolling clamps at the bottom with the last object fully reachable")
        let last = store.items.last!
        let lastRect = canvas.cardRect(for: last.id)!
        _ = canvas.mouseDown(at: CGPoint(x: lastRect.minX + 30, y: lastRect.minY + 20), clickCount: 1, modifiers: [.shift])
        check(canvas.selectedIDs == Set(store.items.map(\.id)) && canvas.dragSelection(primaryID: last.id) == store.items.map(\.id),
              "Shift-click spans offscreen rows and dragging exports every selected reference in shelf order")
        canvas.revealItems([first.id, store.items[4].id])
        check(canvas.selectedIDs == [first.id, store.items[4].id] && canvas.cardRect(for: store.items[4].id)?.height == 74,
              "A menu-bar drop can reveal its selected references without page navigation")
        canvas.perform(actionID: action(first.id, "select"))
        check(canvas.scrollOffset == 0 && canvas.selectedID == first.id && canvas.cardRect(for: first.id)?.height == 74,
              "Selecting an offscreen object scrolls its whole row into view")
        canvas.scrollBy(50); canvas.perform(actionID: action(first.id, "select"))
        check(canvas.scrollOffset == 0, "Reselecting an already selected offscreen object also restores its visible row")
        let originalBytes = try! Data(contentsOf: urls[0])
        canvas.deleteSelection()
        check(removedNames == [first.name], "A successful removal reports the removed basename once")
        check(store.items.count == 12 && canvas.itemCount == 12 && canvas.scrollOffset == 0 && canvas.selectedID == nil,
              "Delete removes a reference and clamps the remaining collection")
        check((try! Data(contentsOf: urls[0])) == originalBytes,
              "Removing a shelf reference never deletes or edits the original file")

        let missing = store.items[0]
        let missingURL = URL(fileURLWithPath: missing.lastKnownPath)
        try! FileManager.default.removeItem(at: missingURL)
        canvas.activate()
        let missingRect = canvas.cardRect(for: missing.id)!
        check(canvas.itemCount == 12 && store.items.first { $0.id == missing.id }?.availabilityError != nil,
              "Reopening retains unavailable references instead of silently dropping them")
        check(canvas.itemAt(point: center(missingRect)) == nil,
              "An unavailable reference cannot begin a broken drag")
        check(!canvas.accessibleActions.contains { $0.id == action(missing.id, "preview") || $0.id == action(missing.id, "reveal") },
              "Unavailable cards suppress commands that need file access")
        let previewCount = previews.count
        canvas.perform(actionID: action(missing.id, "preview"))
        _ = canvas.mouseDown(at: CGPoint(x: missingRect.minX + 18, y: missingRect.minY + 20), clickCount: 2)
        check(previews.count == previewCount && canvas.selectedID == missing.id,
              "Unavailable cards can be selected but cannot preview stale paths")
        check(canvas.dragSelection(primaryID: missing.id).isEmpty, "Unavailable selected cards cannot create native pasteboard writers")
        canvas.perform(actionID: action(missing.id, "remove"))
        check(canvas.itemCount == 11 && store.items.allSatisfy { $0.id != missing.id },
              "Unavailable entries can still be removed from metadata")

        canvas.perform(actionID: "shelf:clear")
        check(canvas.itemCount == 11 && canvas.accessibleActions.contains { $0.id == "shelf:confirmClear" },
              "Clear all first offers an inline confirmation without changing references")
        canvas.perform(actionID: "shelf:cancelClear")
        check(clearedCounts.isEmpty, "A cancelled clear does not emit an action event")
        check(canvas.itemCount == 11 && !canvas.accessibleActions.contains { $0.id == "shelf:confirmClear" },
              "Cancel keeps every reference")
        canvas.perform(actionID: "shelf:clear")
        canvas.deactivate()
        check(!canvas.accessibleActions.contains { $0.id == "shelf:confirmClear" },
              "Leaving the module cancels its temporary clear confirmation")
        canvas.perform(actionID: "shelf:clear")
        canvas.perform(actionID: "shelf:confirmClear")
        check(store.items.isEmpty && canvas.itemCount == 0 && canvas.scrollOffset == 0,
              "Confirmed clear removes all references and resets the empty collection offset")
        check(clearedCounts == [11], "Confirmed clear reports the actual removed count once")
        check(urls.dropFirst(2).allSatisfy { FileManager.default.fileExists(atPath: $0.path) },
              "Clear all leaves every existing source file in Finder")
        let reopened = try! FileShelfStore(directory: directory.appendingPathComponent("metadata"))
        check(reopened.items.isEmpty, "Shelf removals persist across a new store connection")
        let unavailable = FileShelfCanvas(store: nil, error: "Storage failed")
        unavailable.onItemsAdded = { addedNames += $0 }
        unavailable.onChooseFiles = { chooses += 1 }
        let previousChooses = chooses
        unavailable.perform(actionID: "shelf:add")
        check(chooses == previousChooses && !unavailable.importURLs([urls[0]]),
              "A failed store cannot advertise a successful import or create an unsaved item")
        check(addedNames.count == 13, "A failed import does not emit a successful action event")

        let groupStore = try! FileShelfStore(directory: directory.appendingPathComponent("group-metadata"))
        let animated = FileShelfCanvas(store: groupStore, reduceMotion: { false })
        animated.activate()
        check(animated.importURLs(Array(urls.suffix(4))), "A second bounded shelf imports the group-selection fixtures")
        let collection = animated.layer.sublayers!.first { $0.animation(forKey: HUDSubsectionTransition.movementKey) != nil }
        check(collection != nil, "A successful import receives a finite mechanical content reveal")
        let firstGroup = groupStore.items[0], thirdGroup = groupStore.items[2]
        animated.perform(actionID: action(firstGroup.id, "select"))
        let groupRect = animated.cardRect(for: thirdGroup.id)!
        _ = animated.mouseDown(at: CGPoint(x: groupRect.minX + 20, y: groupRect.minY + 20), clickCount: 1, modifiers: [.shift])
        check(animated.selectedIDs.count == 3, "Active selection supports the same three-item group as the windowless canvas")
        check(animationCount(animated.layer) > 0, "Selection changes animate card depth without an idle timer")
        animated.deleteSelection()
        check(groupStore.items.count == 1 && animated.selectedIDs.isEmpty, "Delete removes every selected reference and clears stale selection IDs")
        check(urls.suffix(4).allSatisfy { FileManager.default.fileExists(atPath: $0.path) }, "Group deletion preserves every original Finder item")
        animated.perform(actionID: "shelf:clear")
        check(animated.layer.sublayers?.contains { $0.animation(forKey: "shelf.toolbar.reveal") != nil } == true,
              "Opening inline clear confirmation uses a short toolbar depth reveal")
        animated.deactivate()
        check(animationCount(animated.layer) == 0 && animated.layer.sublayers?.allSatisfy { ($0.mask?.animationKeys() ?? []).isEmpty } == true,
              "Deactivation removes every finite action and collection animation")
        let reduced = FileShelfCanvas(store: groupStore, reduceMotion: { true })
        reduced.activate(); reduced.perform(actionID: action(groupStore.items[0].id, "select")); reduced.setDropTarget(true)
        reduced.perform(actionID: "shelf:clear")
        check(animationCount(reduced.layer) == 0, "Reduced Motion applies selections and confirmations immediately without tracks")
        reduced.deactivate()

        let cacheStore = try! FileShelfStore(directory: directory.appendingPathComponent("cache-metadata"))
        let cacheCanvas = FileShelfCanvas(store: cacheStore, reduceMotion: { true })
        let cacheURLs = (0..<30).map { index -> URL in
            let url = directory.appendingPathComponent("Cache \(index).txt")
            try! Data("\(index)".utf8).write(to: url); return url
        }
        check(cacheCanvas.importURLs(cacheURLs) && cacheCanvas.cachedIconCount == 0,
              "An unopened long shelf stores references without resolving icons for hidden cards")
        _ = cacheCanvas.makeContent(for: .fileShelf, style: dark); cacheCanvas.activate()
        cacheCanvas.scrollBy(-CGFloat.greatestFiniteMagnitude)
        for _ in 0..<16 {
            cacheCanvas.scrollBy(79.5)
            check(descendants(cacheCanvas.layer).filter { $0.name?.hasPrefix("shelf.card.") == true }.count <= 8
                  && cacheCanvas.cachedIconCount <= 24,
                  "Scrolling a long shelf retains only visible card layers and a bounded icon cache")
        }
        for item in cacheStore.items { _ = cacheCanvas.icon(for: item.id) }
        check(cacheCanvas.cachedIconCount == 24,
              "Exporting a large selected group cannot grow the shared Finder icon cache indefinitely")
        let rememberedOffset = cacheCanvas.scrollOffset
        cacheCanvas.deactivate(); cacheCanvas.activate()
        check(cacheCanvas.scrollOffset == rememberedOffset && animationCount(cacheCanvas.layer) == 0,
              "Closing and reopening preserves the scroll position without restarting a page effect")
        cacheCanvas.deactivate()
        return count
    }
}
