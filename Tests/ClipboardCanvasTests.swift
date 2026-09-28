import AppKit
import ImageIO
import QuartzCore

enum ClipboardCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        let priorLanguage = L10n.language
        defer { L10n.language = priorLanguage }
        L10n.language = .english
        let pasteboard = NSPasteboard(name: NSPasteboard.Name("EndfieldCharge-ClipboardCanvasTests-\(UUID().uuidString)"))
        defer { pasteboard.clearContents(); pasteboard.releaseGlobally() }
        let store = ClipboardStore(capacity: 12)
        let canvas = ClipboardCanvas(store: store, reduceMotion: { true })
        var copies: [UUID] = []
        var changes = 0
        canvas.onChange = { changes += 1 }
        canvas.onCopy = { copies.append($0); return store.copy(id: $0, to: pasteboard) }
        func capture(_ value: String) {
            pasteboard.clearContents(); pasteboard.setString(value, forType: .string)
            check(store.capture(from: pasteboard), "Fixture content captures on an isolated pasteboard")
        }
        func action(_ id: UUID, _ verb: String) -> String { "clipboard:\(id.uuidString):\(verb)" }
        func center(_ rect: CGRect) -> CGPoint { CGPoint(x: rect.midX, y: rect.midY) }
        func commands(_ verb: String) -> [ClipboardCanvasAction] { canvas.accessibleActions.filter { $0.id.hasSuffix(":" + verb) } }
        func animationCount(_ layer: CALayer) -> Int {
            (layer.animationKeys()?.count ?? 0) + (layer.sublayers ?? []).reduce(0) { $0 + animationCount($1) }
        }
        let dark = HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2)
        let light = HUDModuleContentStyle(dark: false, accent: .systemGreen, contentsScale: 2.35)
        let persistent = canvas.makeContent(for: .clipboard, style: dark)
        canvas.activate()
        check(canvas.itemCount == 0 && canvas.pageCount == 1 && canvas.pageIndex == 0, "Empty clipboard renders a valid first page")
        check(!canvas.mouseDown(at: CGPoint(x: -1, y: 60)) && canvas.mouseDown(at: CGPoint(x: 50, y: 10)),
              "Only input within the common content host is consumed")
        check(copies.isEmpty, "Empty and heading clicks never restore an arbitrary clipboard item")
        for index in 0..<10 { capture("Entry \(index) – 中文\nSecond line") }
        check(canvas.itemCount == 10 && canvas.pageCount == 2 && commands("copy").count == 6,
              "Observed captures update six readable rows with the remainder on a second page")
        let first = store.items[0]
        for item in store.items.prefix(6) {
            check(canvas.rowRect(for: item.id).map(canvas.layer.bounds.contains) == true,
                  "Every visible clipboard row fits within the projected host")
        }
        let firstRect = canvas.rowRect(for: first.id)!
        _ = canvas.mouseDown(at: CGPoint(x: firstRect.minX + 90, y: firstRect.midY))
        check(copies == [first.id] && canvas.selectedID == first.id && canvas.accessibilityStatus == "Copied",
              "Clicking a preview restores the correct item, selects it and reports success")
        check(pasteboard.string(forType: .string) == "Entry 9 – 中文\nSecond line",
              "A short preview restores complete Unicode content rather than preview text")
        let pin = canvas.accessibleActions.first { $0.id == action(first.id, "pin") }!
        _ = canvas.mouseDown(at: center(pin.rect))
        check(store.items.first { $0.id == first.id }?.isPinned == true && copies.count == 1,
              "Clicking a row's pin control does not copy its content")
        check(canvas.accessibleActions.contains { $0.id == action(first.id, "pin") && $0.label.hasPrefix("Unpin:") },
              "Pin feedback changes the same accessible action to Unpin")
        canvas.selectNext(1)
        check(canvas.selectedID == store.items[1].id && copies.count == 1, "Arrow selection moves without replacing the system clipboard")
        canvas.copySelection()
        check(copies.last == store.items[1].id, "Return-style copy uses the selected row")
        canvas.perform(actionID: "clipboard:next")
        check(canvas.pageIndex == 1 && canvas.selectedID == nil && commands("copy").count == 4,
              "Next page exposes the remaining rows without retaining a hidden selection")
        canvas.copyVisibleItem(at: 0)
        check(copies.last == store.items[6].id, "Number shortcuts restore the intended row on the visible page")
        let copyCount = copies.count
        canvas.copyVisibleItem(at: 5)
        check(copies.count == copyCount, "An empty numbered slot cannot copy a different item")
        canvas.perform(actionID: "clipboard:next")
        check(canvas.pageIndex == 1 && !canvas.accessibleActions.contains { $0.id == "clipboard:next" }, "Pagination stops at the last page")
        canvas.perform(actionID: "clipboard:previous")
        for _ in 0..<30 { _ = canvas.scroll(at: CGPoint(x: 30, y: 60), delta: 0.5) }
        check(canvas.pageIndex == 0, "Small trackpad samples do not skip pages")
        _ = canvas.scroll(at: CGPoint(x: 30, y: 60), delta: -10)
        _ = canvas.scroll(at: CGPoint(x: 30, y: 60), delta: 30)
        check(canvas.pageIndex == 0, "Scroll reversal resets accumulated movement")
        _ = canvas.scroll(at: CGPoint(x: 30, y: 60), delta: 12)
        check(canvas.pageIndex == 1, "The scroll threshold advances one page")
        check(!canvas.scroll(at: CGPoint(x: 30, y: 10), delta: 60) && !canvas.scroll(at: CGPoint(x: 30, y: 60), delta: .nan),
              "Heading and invalid scroll input cannot change pages")
        canvas.perform(actionID: "clipboard:previous")
        let IDs = store.items.map(\.id)
        L10n.language = .simplifiedChinese
        let updated = canvas.makeContent(for: .clipboard, style: light)
        canvas.updateRenderScale(3)
        check(updated === persistent && store.items.map(\.id) == IDs,
              "Language, theme and scale changes preserve both the layer and clipboard entries")
        check(canvas.accessibleActions.contains { $0.id == "clipboard:clear" && $0.label == "清空未固定项" },
              "Chinese translations keep stable action identities")
        check(animationCount(canvas.layer) == 0, "Clipboard rows add no idle animation tracks")
        L10n.language = .english

        canvas.perform(actionID: "clipboard:clear")
        check(store.items.count == 10 && canvas.accessibleActions.contains { $0.id == "clipboard:confirmClear" },
              "Clear first offers an inline confirmation")
        canvas.perform(actionID: "clipboard:cancelClear")
        check(store.items.count == 10 && !canvas.accessibleActions.contains { $0.id == "clipboard:confirmClear" },
              "Cancelling clear keeps every history item")
        canvas.perform(actionID: "clipboard:clear")
        canvas.perform(actionID: "clipboard:confirmClear")
        check(store.items.count == 1 && store.items[0].id == first.id && canvas.pageCount == 1,
              "Clear unpinned retains the pinned entry and recomputes page bounds")
        canvas.perform(actionID: action(first.id, "copy"))
        canvas.deleteSelection()
        check(store.items.isEmpty && canvas.selectedID == nil, "Explicit delete can remove a selected pinned item")

        canvas.deactivate()
        let hiddenChanges = changes
        capture("Captured while another module is selected")
        check(changes == hiddenChanges && canvas.itemCount == 0,
              "A hidden clipboard module observes dirtiness without rendering or rebuilding native actions")
        canvas.activate()
        check(canvas.itemCount == 1 && changes > hiddenChanges, "Activation refreshes captures received while hidden")
        canvas.onCopy = { _ in false }
        canvas.copyVisibleItem(at: 0)
        check(canvas.accessibilityStatus == "Could not restore this item", "Failed restoration never reports Copied")
        canvas.onCopy = { store.copy(id: $0, to: pasteboard) }

        // All supported kinds render from metadata or existing thumbnails only.
        pasteboard.clearContents(); pasteboard.writeObjects([URL(string: "https://example.com/clipboard")! as NSURL])
        check(store.capture(from: pasteboard), "A URL fixture captures")
        pasteboard.clearContents(); pasteboard.setData(imageData(), forType: .png)
        check(store.capture(from: pasteboard), "An image fixture captures")
        let file = FileManager.default.temporaryDirectory.appendingPathComponent("ClipboardCanvas-\(UUID().uuidString).txt")
        try! Data("File contents remain outside the canvas".utf8).write(to: file)
        defer { try? FileManager.default.removeItem(at: file) }
        pasteboard.clearContents(); pasteboard.writeObjects([file as NSURL])
        check(store.capture(from: pasteboard), "A file-reference fixture captures")
        check(Set(store.items.map(\.kind)) == Set(ClipboardKind.allCases) && canvas.itemCount == 4,
              "Text, URL, cached image and file previews coexist in the HUD")
        check(store.items.first { $0.kind == .image }?.thumbnail != nil && commands("copy").count == 4,
              "The image row uses the store's cached thumbnail and exposes the same restore action")
        L10n.language = .simplifiedChinese
        _ = canvas.makeContent(for: .clipboard, style: light)
        check(canvas.accessibleActions.contains { $0.label.hasPrefix("复制：图片 · ") },
              "An image captured in English relocalizes its preview in Chinese")
        L10n.language = .english

        weak var releasedCanvas: ClipboardCanvas?
        do { let temporary = ClipboardCanvas(store: store); temporary.activate(); releasedCanvas = temporary }
        check(releasedCanvas == nil, "The store observer does not retain an abandoned HUD canvas")
        capture("Observer cleanup remains safe")
        check(canvas.itemCount == 5, "The surviving canvas still observes after another observer is removed")

        let inputStore = ClipboardStore()
        for text in ["Keyboard older", "Keyboard newer"] {
            pasteboard.clearContents(); pasteboard.setString(text, forType: .string)
            _ = inputStore.capture(from: pasteboard)
        }
        let inputCanvas = ClipboardCanvas(store: inputStore)
        let host = NSView(frame: CGRect(x: 0, y: 0, width: 400, height: 334))
        let input = HUDClipboardInteraction(canvas: inputCanvas, host: host)
        input.project = { $0 }
        var inputCopies: [UUID] = []
        var locks = 0
        inputCanvas.onCopy = { inputCopies.append($0); return true }
        input.onLock = { locks += 1 }
        input.setActive(true)
        func key(_ code: UInt16, _ character: String = "", flags: NSEvent.ModifierFlags = []) -> NSEvent {
            NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: flags, timestamp: 0, windowNumber: 0,
                             context: nil, characters: character, charactersIgnoringModifiers: character, isARepeat: false, keyCode: code)!
        }
        check(input.keyDown(key(125)) && inputCanvas.selectedID == inputStore.items[0].id,
              "Native Down selects the first row without requiring a standard list view")
        check(input.keyDown(key(36, "\r")) && inputCopies == [inputStore.items[0].id],
              "Native Return restores the selected entry through the clipboard service callback")
        check(input.keyDown(key(19, "2")) && inputCopies.last == inputStore.items[1].id,
              "A native number shortcut maps to the visible second row")
        check(!input.keyDown(key(53)) && !input.keyDown(key(50, "~", flags: [.shift])),
              "Escape and the global HUD toggle remain owned by the shared shell")
        check(!input.keyDown(key(8, "c", flags: [.command])), "Modified shortcuts are not consumed as row commands")
        check(input.keyDown(key(117)) && inputStore.items.count == 1,
              "Native Forward Delete removes only the selected cached item")
        let down = NSEvent.mouseEvent(with: .leftMouseDown, location: CGPoint(x: 120, y: 60), modifierFlags: [], timestamp: 0,
                                     windowNumber: 0, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
        check(input.mouseDown(at: CGPoint(x: 120, y: 60), event: down) && input.isInputLocked,
              "A native row click freezes pointer depth while the mouse is held")
        input.mouseUp()
        check(!input.isInputLocked && locks > 0, "Mouse-up releases the clipboard interaction lock")
        check(!host.subviews.isEmpty, "The active scene exposes native accessibility controls")
        inputStore.clearUnpinned()
        check(host.subviews.isEmpty, "Eviction removes obsolete native row actions")
        input.deactivate()
        check(!input.keyDown(key(125)) && !input.mouseDown(at: CGPoint(x: 120, y: 60), event: down),
              "An inactive clipboard module cannot intercept another module's keyboard or pointer input")
        var reducedFeedback = false
        let feedbackStore = ClipboardStore()
        for value in ["Feedback one", "Feedback two", "Feedback three"] {
            pasteboard.clearContents(); pasteboard.setString(value, forType: .string); _ = feedbackStore.capture(from: pasteboard)
        }
        let feedbackCanvas = ClipboardCanvas(store: feedbackStore, reduceMotion: { reducedFeedback })
        feedbackCanvas.onCopy = { _ in true }; feedbackCanvas.activate()
        func feedbackLayers(_ root: CALayer) -> [CALayer] {
            [root] + (root.sublayers ?? []).flatMap(feedbackLayers) + (root.mask.map(feedbackLayers) ?? [])
        }
        func feedbackTracks() -> [CAAnimation] {
            feedbackLayers(feedbackCanvas.layer).flatMap { layer in (layer.animationKeys() ?? []).compactMap { layer.animation(forKey: $0) } }
        }
        feedbackCanvas.copyVisibleItem(at: 0)
        check(feedbackTracks().count == 2, "Successful clipboard copy adds a row depth engagement and outline registration")
        let pinnedFeedbackID = feedbackStore.items[1].id
        feedbackCanvas.perform(actionID: "clipboard:\(pinnedFeedbackID.uuidString):pin")
        check(feedbackStore.items.first { $0.id == pinnedFeedbackID }?.isPinned == true && feedbackTracks().count == 2,
              "Pinning updates its saved state immediately and registers only the affected row")
        feedbackCanvas.deleteSelection()
        check(feedbackStore.items.count == 2 && feedbackTracks().contains { ($0 as? CAPropertyAnimation)?.keyPath == "position" },
              "Deleting a selected item slides the retained rows into their new slots")
        feedbackCanvas.perform(actionID: "clipboard:clear")
        check(feedbackTracks().contains { ($0 as? CAPropertyAnimation)?.keyPath == "sublayerTransform" },
              "Clear confirmation engages the compact bottom toolbar")
        feedbackCanvas.perform(actionID: "clipboard:cancelClear")
        feedbackCanvas.perform(actionID: "clipboard:clear"); feedbackCanvas.perform(actionID: "clipboard:confirmClear")
        check(feedbackStore.items.map(\.id) == [pinnedFeedbackID] && !feedbackTracks().isEmpty,
              "Animated clear preserves pinned items and applies the original clear semantics")
        check(feedbackTracks().allSatisfy { $0.duration <= 0.26 && $0.repeatCount == 0 && $0.repeatDuration == 0
            && ($0 as? CAPropertyAnimation)?.keyPath != "opacity" }, "Action feedback stays finite and mechanical without blinking or crossfade")
        reducedFeedback = true; feedbackCanvas.updateRenderScale(2)
        check(feedbackTracks().isEmpty, "Reduce Motion immediately clears every clipboard action and page track")
        feedbackCanvas.copyVisibleItem(at: 0)
        check(feedbackTracks().isEmpty, "Reduced Motion still copies without starting feedback")
        reducedFeedback = false; feedbackCanvas.copyVisibleItem(at: 0); feedbackCanvas.deactivate()
        check(feedbackTracks().isEmpty, "Closing Clipboard removes all finite feedback instead of leaving hidden animations")
        return count
    }

    private static func imageData() -> Data {
        let context = CGContext(data: nil, width: 8, height: 8, bitsPerComponent: 8, bytesPerRow: 32,
                                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.setFillColor(NSColor.systemYellow.cgColor); context.fill(CGRect(x: 0, y: 0, width: 8, height: 8))
        let result = NSMutableData()
        let destination = CGImageDestinationCreateWithData(result, "public.png" as CFString, 1, nil)!
        CGImageDestinationAddImage(destination, context.makeImage()!, nil)
        precondition(CGImageDestinationFinalize(destination))
        return result as Data
    }
}
