import AppKit
import QuartzCore

enum EventLogCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        let oldLanguage = L10n.language
        defer { L10n.language = oldLanguage }
        L10n.language = .english
        let store = SystemEventLog(), canvas = EventLogCanvas(store: SystemEventLog())
        let empty = canvas.makeContent(for: .eventLog, style: HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2))
        canvas.activate()
        check(empty.bounds == CGRect(x: 0, y: 0, width: 400, height: 334) && canvas.itemCount == 0, "Event Log uses the compact common content frame")
        check(canvas.accessibleActions.count == 8 && !canvas.accessibleActions.contains { $0.id == "eventLog:clear" }, "Empty log offers categories without a clear action")
        check(!canvas.mouseDown(at: CGPoint(x: -1, y: 100)) && !canvas.mouseDown(at: CGPoint(x: CGFloat.nan, y: 100)), "Invalid and external clicks are ignored")
        let view = EventLogCanvas(store: store, reduceMotion: { true })
        var changes = 0
        view.onChange = { changes += 1 }
        let persistent = view.makeContent(for: .eventLog, style: HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2))
        view.activate()
        for index in 0..<20 {
            store.record(kind: index % 2 == 0 ? .clipboardCopied : .shelfAdded,
                         metadata: index % 2 == 0 ? ["kind": "image"] : ["filename": "File \(index).png"])
        }
        check(view.itemCount == 20 && view.filteredCount == 20 && changes >= 21, "Active canvas observes incoming events")
        func rowActions() -> [EventLogCanvasAction] { view.accessibleActions.filter { $0.id.hasPrefix("eventLog:row:") } }
        check(rowActions().count == 4 && rowActions().allSatisfy { EventLogCanvas.viewport.contains($0.rect) }, "Only visible rows have clipped native accessibility frames")
        let initialRow = rowActions()[0]
        check(view.mouseDown(at: CGPoint(x: initialRow.rect.midX, y: initialRow.rect.midY)) && view.selectedID == store.events[0].id, "Click selects an event without side effects")
        check(view.scroll(at: CGPoint(x: 25, y: 120), delta: 0.5) && view.scrollOffset == 0.5, "Trackpad movement scrolls continuously below one row")
        check(!view.scroll(at: CGPoint(x: 25, y: 20), delta: 50) && !view.scroll(at: CGPoint(x: 25, y: 120), delta: .nan), "Header and invalid deltas do not scroll")
        view.scrollBy(125)
        let anchor = rowActions()[0].id, offset = view.scrollOffset
        store.record(kind: .overlayOpened)
        check(rowActions()[0].id == anchor && view.scrollOffset == offset + 55, "New arrivals preserve the currently viewed row")
        view.scrollBy(CGFloat.greatestFiniteMagnitude)
        check(rowActions().last?.id.hasSuffix(store.events.last!.id.uuidString) == true, "Scrolling reaches the oldest event")
        view.selectNext(-1)
        check(view.selectedID == store.events[0].id && view.rowRect(for: store.events[0].id) != nil, "Keyboard selection brings its row into view")
        view.perform(actionID: "eventLog:category:clipboard")
        check(view.selectedCategory == .clipboard && view.filteredCount == 10 && view.scrollOffset == 0 && view.selectedID == nil, "Category changes reset selection and scroll")
        check(rowActions().allSatisfy { $0.label.contains("Clipboard item copied") }, "Filtered rows contain only the chosen category")
        view.perform(actionID: "eventLog:category:invalid")
        check(view.selectedCategory == .clipboard, "Unknown action IDs cannot select a fabricated category")
        view.perform(actionID: "eventLog:category:audio")
        check(view.filteredCount == 0 && rowActions().isEmpty && view.accessibleActions.contains { $0.id == "eventLog:clear" }, "An empty category can still clear the complete saved log")
        view.perform(actionID: "eventLog:category:all")
        view.perform(actionID: "eventLog:confirmClear")
        check(store.events.count == 21, "Confirmation cannot clear without an explicit clear request")
        view.perform(actionID: "eventLog:clear")
        check(view.accessibleActions.contains { $0.id == "eventLog:confirmClear" } && store.events.count == 21, "Clear first shows an inline confirmation")
        check(view.cancelConfirmation() && !view.cancelConfirmation(), "Escape cancels only an open confirmation")
        view.perform(actionID: "eventLog:clear")
        view.perform(actionID: "eventLog:cancelClear")
        check(store.events.count == 21, "Cancel preserves history")
        L10n.language = .simplifiedChinese
        let light = view.makeContent(for: .eventLog, style: HUDModuleContentStyle(dark: false, accent: .systemGreen, contentsScale: 2.35))
        view.updateRenderScale(3)
        check(light === persistent && view.itemCount == 21, "Theme, language and scale keep the retained canvas and data")
        check(view.accessibleActions.contains { $0.id == "eventLog:category:clipboard" && $0.label == "Clipboard" }, "Category controls stay English in Chinese app mode")
        check(rowActions().first?.label.contains("Overlay opened") == true, "Event accessibility labels remain English")
        func animations(_ layer: CALayer) -> Int { (layer.animationKeys()?.count ?? 0) + (layer.sublayers ?? []).reduce(0) { $0 + animations($1) } }
        check(animations(view.layer) == 0, "The log adds no idle animation or polling")
        let activeChanges = changes
        view.deactivate()
        store.record(kind: .workStarted)
        check(changes == activeChanges && view.itemCount == 21, "Hidden canvas unsubscribes instead of rebuilding")
        view.activate()
        check(view.itemCount == 22 && changes > activeChanges, "Reopening catches up with the shared log")
        view.perform(actionID: "eventLog:clear")
        view.perform(actionID: "eventLog:confirmClear")
        check(store.events.isEmpty && view.itemCount == 0 && view.filteredCount == 0 && view.scrollOffset == 0, "Confirmed clear removes all history and leaves no clear event")

        _ = NSApplication.shared
        let host = NSView(frame: CGRect(x: 0, y: 0, width: 400, height: 334))
        let interactionStore = SystemEventLog()
        let interactionCanvas = EventLogCanvas(store: interactionStore)
        let interaction = HUDEventLogInteraction(canvas: interactionCanvas, host: host)
        var locks = 0
        interaction.onLock = { locks += 1 }
        interaction.project = { $0.offsetBy(dx: 10, dy: 20) }
        for _ in 0..<12 { interactionStore.record(kind: .moduleOpened, metadata: ["module": "notes"]) }
        interaction.setActive(true)
        check(host.subviews.filter { !$0.isHidden }.count == interactionCanvas.accessibleActions.count, "Every visible control receives a native accessibility button")
        check(host.subviews.allSatisfy { $0.frame.minX >= 22 }, "Native controls use projected coordinates")
        let clearButton = host.subviews.compactMap { $0 as? NSButton }.first { $0.accessibilityLabel() == "清空日志" }!
        clearButton.performClick(nil)
        check(interactionCanvas.accessibleActions.contains { $0.id == "eventLog:confirmClear" } && locks == 1, "Native accessibility activation opens the same clear confirmation")
        func key(_ code: UInt16) -> NSEvent {
            NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: 0,
                             context: nil, characters: "", charactersIgnoringModifiers: "", isARepeat: false, keyCode: code)!
        }
        check(interaction.keyDown(key(53)) && interactionStore.events.count == 12, "Escape dismisses clear through the native bridge")
        check(interaction.keyDown(key(125)) && interactionCanvas.selectedID == interactionStore.events[0].id, "Native Down selects the first event")
        check(interaction.keyDown(key(121)) && interactionCanvas.scrollOffset > 0, "Page Down scrolls through history")
        check(interaction.keyDown(key(115)) && interactionCanvas.scrollOffset == 0, "Home scrolls to the newest event")
        check(!interaction.keyDown(key(36)), "Return does not accidentally clear history")
        interaction.deactivate()
        check(host.subviews.allSatisfy(\.isHidden) && !interaction.isInputLocked && !interaction.keyDown(key(125)), "Inactive native controls are hidden and cannot act")
        interaction.setActive(true)
        check(host.subviews.contains { !$0.isHidden }, "Native controls reactivate without replacing the shell")
        interaction.deactivate()
        var reducedFeedback = false
        let feedbackStore = SystemEventLog()
        feedbackStore.record(kind: .overlayOpened)
        let feedbackCanvas = EventLogCanvas(store: feedbackStore, reduceMotion: { reducedFeedback })
        feedbackCanvas.activate()
        func feedbackLayers(_ root: CALayer) -> [CALayer] {
            [root] + (root.sublayers ?? []).flatMap(feedbackLayers) + (root.mask.map(feedbackLayers) ?? [])
        }
        func feedbackTracks() -> [CAAnimation] {
            feedbackLayers(feedbackCanvas.layer).flatMap { layer in (layer.animationKeys() ?? []).compactMap { layer.animation(forKey: $0) } }
        }
        feedbackCanvas.selectNext(1)
        check(feedbackTracks().count == 1 && (feedbackTracks()[0] as? CAPropertyAnimation)?.keyPath == "strokeEnd",
              "Selecting an event traces its outline without moving the readable text")
        feedbackCanvas.perform(actionID: "eventLog:clear")
        check(feedbackTracks().count > 0 && feedbackLayers(feedbackCanvas.layer).filter { $0.frame.minY < 296 }
            .allSatisfy { $0.animation(forKey: "action.eventLog.confirmation") == nil },
              "Clear confirmation moves only its bottom controls, leaving category chips and heading stationary")
        check(feedbackStore.events.count == 1, "Animation does not bypass clear confirmation")
        _ = feedbackCanvas.cancelConfirmation()
        feedbackCanvas.perform(actionID: "eventLog:clear"); feedbackCanvas.perform(actionID: "eventLog:confirmClear")
        check(feedbackStore.events.isEmpty && feedbackTracks().contains { ($0 as? CAPropertyAnimation)?.keyPath == "path" },
              "Confirmed clear reveals the empty destination through the mechanical row shutter")
        check(feedbackTracks().allSatisfy { $0.duration <= 0.26 && $0.repeatCount == 0 && $0.repeatDuration == 0
            && ($0 as? CAPropertyAnimation)?.keyPath != "opacity" }, "Event action effects are finite and never blink or crossfade")
        reducedFeedback = true; feedbackCanvas.updateRenderScale(2)
        check(feedbackTracks().isEmpty, "Reduce Motion settles clear and row effects even without a scale change")
        feedbackStore.record(kind: .overlayOpened); feedbackCanvas.perform(actionID: "eventLog:clear")
        check(feedbackTracks().isEmpty, "Reduced Motion keeps clear confirmation immediate")
        reducedFeedback = false; _ = feedbackCanvas.cancelConfirmation(); feedbackCanvas.deactivate()
        check(feedbackTracks().isEmpty, "Hiding Event Log cancels every clear and selection effect")

        var reducedFilters = false
        let filterStore = SystemEventLog()
        for index in 0..<16 {
            filterStore.record(kind: index % 2 == 0 ? .clipboardCopied : .shelfAdded,
                metadata: index % 2 == 0 ? ["kind": "text"] : ["filename": "Row \(index).txt"])
        }
        let filters = EventLogCanvas(store: filterStore, reduceMotion: { reducedFilters })
        filters.activate()
        let pages = filters.layer.sublayers!.filter { ($0.name ?? "").hasPrefix("eventLog.rows.") }
        let pageIDs = pages.map(ObjectIdentifier.init)
        func filterTracks() -> [CAAnimation] {
            feedbackLayers(filters.layer).flatMap { item in
                (item.animationKeys() ?? []).filter { $0.hasPrefix("subsection.") }.compactMap { item.animation(forKey: $0) }
            }
        }
        func rowIDs(_ page: CALayer) -> [ObjectIdentifier] { (page.sublayers ?? []).map(ObjectIdentifier.init) }
        check(pages.count == 2 && pages.filter { !$0.isHidden }.count == 1,
              "The log owns two bounded row planes with just the current page exposed at rest")
        let outgoing = pages[0], outgoingRows = rowIDs(pages[0])
        filters.perform(actionID: "eventLog:category:clipboard")
        check(pages.allSatisfy { !$0.isHidden } && filterTracks().count == 4 && rowIDs(outgoing) == outgoingRows,
              "Category changes retain outgoing artwork until the new rows replace its pixels")
        check(filterTracks().filter { ($0 as? CAPropertyAnimation)?.keyPath == "path" }.allSatisfy { $0 is CABasicAnimation },
              "Filtering uses matching single-edge masks instead of independently staggered row shutters")
        filterStore.record(kind: .clipboardCopied, metadata: ["kind": "image"])
        check(rowIDs(outgoing) == outgoingRows && filters.filteredCount == 9
              && filters.accessibleActions.contains { $0.id == "eventLog:row:" + filterStore.events[0].id.uuidString },
              "A live arrival updates the destination while outgoing rows remain frozen")
        check(filters.scroll(at: CGPoint(x: 80, y: 180), delta: 75) && filters.scrollOffset == 75
              && rowIDs(outgoing) == outgoingRows && filterTracks().count == 4,
              "Scrolling during a filter hand-off changes only incoming rows without restarting the transition")
        for value in ["files", "audio", "all", "clipboard", "files", "audio"] {
            filters.perform(actionID: "eventLog:category:" + value)
            check(pages.map(ObjectIdentifier.init) == pageIDs && filterTracks().count == 4,
                  "Rapid filter changes reuse two planes and replace the four finite transition tracks")
        }
        check(filters.selectedCategory == .audio && filters.filteredCount == 0,
              "A rapid switch into an empty category keeps the requested filter and empty-state destination")
        reducedFilters = true; filters.updateRenderScale(2)
        check(filterTracks().isEmpty && pages.filter { !$0.isHidden }.count == 1
              && pages.filter(\.isHidden).allSatisfy { ($0.sublayers ?? []).isEmpty },
              "Reduce Motion settles the newest category and releases older row artwork immediately")
        reducedFilters = false
        filters.perform(actionID: "eventLog:category:all")
        filters.perform(actionID: "eventLog:clear"); filters.perform(actionID: "eventLog:confirmClear")
        let limit = Date().addingTimeInterval(1)
        while (!filterTracks().isEmpty || pages.filter { !$0.isHidden }.count != 1) && Date() < limit {
            RunLoop.current.run(until: Date().addingTimeInterval(0.004))
        }
        // Allow the same-deadline, bounded cleanup to release the hidden plane.
        RunLoop.current.run(until: Date().addingTimeInterval(0.01))
        check(filterStore.events.isEmpty && filterTracks().isEmpty && pages.filter { !$0.isHidden }.count == 1
              && pages.filter(\.isHidden).allSatisfy { ($0.sublayers ?? []).isEmpty },
              "Completed clear hands off to its empty state and releases all previous row text")
        filters.perform(actionID: "eventLog:category:files"); filters.deactivate()
        let hiddenCount = filters.itemCount
        filterStore.record(kind: .shelfAdded, metadata: ["filename": "Hidden.txt"])
        check(filterTracks().isEmpty && filters.itemCount == hiddenCount && pages.filter { !$0.isHidden }.count == 1,
              "Deactivation cancels any filter completion and stops hidden store-driven redraws")
        return count
    }
}
