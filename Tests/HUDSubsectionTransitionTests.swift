import AppKit
import QuartzCore

enum HUDSubsectionTransitionTests {
    static func run() -> Int {
        var count = 0
        func check(_ result: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !result { fatalError(message, file: file, line: line) }
        }
        func layers(_ root: CALayer) -> [CALayer] {
            [root] + (root.sublayers ?? []).flatMap(layers) + (root.mask.map(layers) ?? [])
        }
        func tracks(_ root: CALayer) -> [CAAnimation] {
            layers(root).flatMap { layer in (layer.animationKeys() ?? []).filter { $0.hasPrefix("subsection.") }.compactMap { layer.animation(forKey: $0) } }
        }
        let content = CALayer(); content.bounds = CGRect(x: 0, y: 0, width: 400, height: 334)
        let viewport = CGRect(x: 12, y: 54, width: 376, height: 250)
        let transition = HUDSubsectionTransition(content: content, viewport: viewport)
        transition.reveal(direction: 1, animated: true)
        check(transition.animationCount == 2, "A subsection switch creates only one depth track and one mask reveal")
        let movement = content.animation(forKey: HUDSubsectionTransition.movementKey) as! CABasicAnimation
        check((movement.fromValue as! NSValue).caTransform3DValue.m41 > 0
              && (movement.fromValue as! NSValue).caTransform3DValue.m43 < 0,
              "Forward navigation approaches laterally from behind the content plane")
        let reveal = content.mask!.animation(forKey: HUDSubsectionTransition.revealKey) as! CAKeyframeAnimation
        let paths = reveal.values as! [CGPath]
        var elementCounts: [Int] = []
        for path in paths { var elements = 0; path.applyWithBlock { _ in elements += 1 }; elementCounts.append(elements) }
        check(Set(elementCounts).count == 1 && elementCounts[0] == 28,
              "Four staggered shutters retain matching path topology throughout the reveal")
        check(!paths[0].contains(CGPoint(x: viewport.midX, y: viewport.midY))
              && paths.last!.contains(CGPoint(x: viewport.minX + 1, y: viewport.minY + 1))
              && paths.last!.contains(CGPoint(x: viewport.maxX - 1, y: viewport.maxY - 1)),
              "The reveal starts closed and finishes exposing the whole destination without clipped corners")
        check(tracks(content).allSatisfy { $0.duration == 0.26 && $0.repeatCount == 0 && $0.repeatDuration == 0
            && ($0 as? CAPropertyAnimation)?.keyPath != "opacity" },
              "Subsection transitions are finite mechanical movement and masks, without a crossfade")
        for _ in 0..<12 { transition.reveal(direction: -1, animated: true) }
        check(transition.animationCount == 2
              && ((content.animation(forKey: HUDSubsectionTransition.movementKey) as! CABasicAnimation).fromValue as! NSValue).caTransform3DValue.m41 < 0,
              "Repeated reverse navigation replaces its tracks without accumulating animations")
        transition.reveal(direction: 1, animated: false)
        check(transition.animationCount == 0 && (content.mask as! CAShapeLayer).path!.contains(CGPoint(x: 30, y: 80)),
              "Reduced motion settles immediately onto fully visible destination content")

        let firstPage = CALayer(), secondPage = CALayer()
        firstPage.frame = content.bounds; secondPage.frame = content.bounds
        let handoff = HUDSubsectionHandoff(first: firstPage, second: secondPage, viewport: viewport)
        check(!firstPage.isHidden && secondPage.isHidden, "Retained page pairs start with one exposed page")
        for direction: CGFloat in [1, -1] {
            let selected = direction > 0 ? 1 : 0
            handoff.select(selected, direction: direction, animated: true)
            check(!firstPage.isHidden && !secondPage.isHidden && handoff.animationCount == 4,
                  "A page hand-off retains both pages behind their moving masks")
            let pages = [firstPage, secondPage], incoming = pages[selected], outgoing = pages[1 - selected]
            let incomingMask = incoming.mask!.animation(forKey: HUDSubsectionTransition.revealKey) as! CABasicAnimation
            let outgoingMask = outgoing.mask!.animation(forKey: HUDSubsectionTransition.revealKey) as! CABasicAnimation
            let inStart = (incomingMask.fromValue as! CGPath).boundingBoxOfPath
            let inEnd = (incomingMask.toValue as! CGPath).boundingBoxOfPath
            let outStart = (outgoingMask.fromValue as! CGPath).boundingBoxOfPath
            let outEnd = (outgoingMask.toValue as! CGPath).boundingBoxOfPath
            check(incomingMask.duration == outgoingMask.duration && incomingMask.timingFunction == outgoingMask.timingFunction,
                  "The two masks share one duration and timing curve")
            for progress in [CGFloat(0.1), 0.35, 0.5, 0.8, 0.95] {
                func interpolate(_ a: CGRect, _ b: CGRect) -> CGRect {
                    CGRect(x: a.minX + (b.minX - a.minX) * progress, y: a.minY,
                           width: a.width + (b.width - a.width) * progress, height: a.height)
                }
                let entering = interpolate(inStart, inEnd), leaving = interpolate(outStart, outEnd)
                let edgeDelta = direction > 0 ? entering.minX - leaving.maxX : leaving.minX - entering.maxX
                check(abs(edgeDelta) < 0.0001 && abs(entering.width + leaving.width - viewport.width) < 0.0001,
                      "Every intermediate hand-off edge has no overlap or empty gap")
            }
            check(incoming.animation(forKey: HUDSubsectionTransition.movementKey) != nil
                  && outgoing.animation(forKey: HUDSubsectionTransition.movementKey) != nil,
                  "Both pages move under their fixed mask coordinates instead of moving the clipping edge")
        }
        for index in 0..<12 { handoff.select(index % 2, direction: index % 2 == 1 ? 1 : -1, animated: true) }
        check(handoff.animationCount == 4, "Rapid reverse navigation replaces four tracks without duplicating pages or animations")
        handoff.settle()
        check(handoff.animationCount == 0 && firstPage.isHidden && !secondPage.isHidden
              && CATransform3DIsIdentity(firstPage.sublayerTransform) && CATransform3DIsIdentity(secondPage.sublayerTransform),
              "Cancellation settles the newest selection and resets both retained page transforms")

        var reduced = false
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        let activity = ActivityMonitorCanvas(controller: .fixture(), reduceMotion: { reduced })
        _ = activity.makeContent(for: .activityMonitor, style: style); activity.activate()
        activity.perform(actionID: "activity:apps")
        check(activity.isShowingApps && tracks(activity.layer).count == 4 && activity.animationCount == 4,
              "Activity's Apps tab exchanges outgoing and incoming reports behind complementary masks")
        activity.perform(actionID: "activity:overview")
        check(tracks(activity.layer).count == 4 && (activity.layer.animationKeys() ?? []).isEmpty
              && layers(activity.layer).filter { ($0.name ?? "").hasPrefix("activity.control.") || $0.name == "activity.heading" }
                .allSatisfy { ($0.animationKeys() ?? []).isEmpty },
              "Returning to Overview moves only its report content and leaves heading and tab controls stationary")
        reduced = true; activity.updateRenderScale(2)
        check(tracks(activity.layer).isEmpty, "Enabling Reduce Motion clears an in-flight Activity subsection transition")
        reduced = false; activity.perform(actionID: "activity:apps"); activity.deactivate()
        check(tracks(activity.layer).isEmpty, "Closing Activity clears all subsection tracks")

        let eventStore = SystemEventLog()
        eventStore.record(kind: .clipboardCopied, metadata: ["kind": "text"])
        let events = EventLogCanvas(store: eventStore, reduceMotion: { reduced })
        _ = events.makeContent(for: .eventLog, style: style); events.activate()
        events.perform(actionID: "eventLog:category:clipboard")
        check(events.filteredCount == 1 && tracks(events.layer).count == 4,
              "Event category switches exchange retained outgoing and incoming rows across one shared mask edge")
        reduced = true; events.updateRenderScale(2)
        check(tracks(events.layer).isEmpty, "Event filters settle when Reduce Motion changes without a scale change")
        reduced = false; events.perform(actionID: "eventLog:category:all"); events.deactivate()
        check(tracks(events.layer).isEmpty, "Hiding Event Log cancels its filter transition")

        let clipboardStore = ClipboardStore(capacity: 10)
        let pasteboard = NSPasteboard(name: NSPasteboard.Name("EndfieldCharge-Subsection-\(UUID().uuidString)"))
        defer { pasteboard.clearContents(); pasteboard.releaseGlobally() }
        for index in 0..<7 {
            pasteboard.clearContents(); pasteboard.setString("Subsection \(index)", forType: .string)
            _ = clipboardStore.capture(from: pasteboard)
        }
        let clipboard = ClipboardCanvas(store: clipboardStore, reduceMotion: { reduced })
        _ = clipboard.makeContent(for: .clipboard, style: style); clipboard.activate()
        clipboard.perform(actionID: "clipboard:next")
        check(clipboard.pageIndex == 1 && tracks(clipboard.layer).count == 2, "Clipboard pages use the shared reveal without changing pagination")
        clipboard.deactivate()
        check(tracks(clipboard.layer).isEmpty, "Closing Clipboard cancels its finite page transition")

        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldCharge-SubsectionShelf-\(UUID().uuidString)")
        try! FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let shelfStore = try! FileShelfStore(directory: directory.appendingPathComponent("metadata"))
        let shelf = FileShelfCanvas(store: shelfStore, reduceMotion: { reduced })
        let files = (0..<7).map { index -> URL in
            let url = directory.appendingPathComponent("Item \(index).txt")
            try! Data("Fixture".utf8).write(to: url); return url
        }
        _ = shelf.importURLs(files); _ = shelf.makeContent(for: .fileShelf, style: style); shelf.activate()
        shelf.perform(actionID: "shelf:previous")
        check(shelf.pageIndex == 0 && tracks(shelf.layer).count == 2, "Shelf pages reveal only their collection without changing the shared HUD")
        shelf.deactivate()
        check(tracks(shelf.layer).isEmpty, "Hiding Shelf cancels page motion even when no clear dialog or drop is active")
        return count
    }
}
