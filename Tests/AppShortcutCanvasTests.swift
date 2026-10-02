import AppKit
import QuartzCore

enum AppShortcutCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !value { fatalError(message, file: file, line: line) }
        }
        let manager = FileManager.default
        let root = manager.temporaryDirectory.appendingPathComponent("AppShortcutCanvasTests-\(UUID().uuidString)", isDirectory: true)
        let language = L10n.language
        defer { try? manager.removeItem(at: root); L10n.language = language }
        L10n.language = .english
        let pasteboard = NSPasteboard(name: .init("AppShortcutCanvas-\(UUID().uuidString)"))
        defer { pasteboard.clearContents(); pasteboard.releaseGlobally() }
        func fixture(_ index: Int) throws -> URL {
            let url = root.appendingPathComponent("Fixture\(index).app", isDirectory: true)
            let binary = url.appendingPathComponent("Contents/MacOS/Fixture")
            try manager.createDirectory(at: binary.deletingLastPathComponent(), withIntermediateDirectories: true)
            let info = ["CFBundlePackageType": "APPL", "CFBundleExecutable": "Fixture", "CFBundleIdentifier": "test.canvas.\(index)", "CFBundleDisplayName": "Fixture \(index)"]
            try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0).write(to: url.appendingPathComponent("Contents/Info.plist"))
            try Data("#!/bin/sh\nexit 0\n".utf8).write(to: binary)
            try manager.setAttributes([.posixPermissions: 0o700], ofItemAtPath: binary.path)
            return url
        }
        func center(_ rect: CGRect) -> CGPoint { CGPoint(x: rect.midX, y: rect.midY) }
        do {
            let store = try AppShortcutStore(directory: root.appendingPathComponent("saved", isDirectory: true))
            let app = try fixture(0)
            let canvas = AppShortcutCanvas(store: store, reduceMotion: { true })
            let originalLayer = canvas.makeContent(for: .addApp, style: HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2))
            var launches: [UUID] = []; var saves: [UUID] = []; var removals: [String] = []
            var chooserRequests = 0; var renameRequests = 0
            canvas.onLaunch = { launches.append($0) }; canvas.onSaved = { saves.append($0.id) }; canvas.onRemoved = { removals.append($0) }
            canvas.onChooseApplication = { chooserRequests += 1 }; canvas.onEditName = { _, _ in renameRequests += 1 }
            let preparedHeading = originalLayer.sublayers?.first?.sublayers?.compactMap { $0 as? CATextLayer }.first
            canvas.activate()
            check(preparedHeading != nil
                  && originalLayer.sublayers?.first?.sublayers?.contains { $0 === preparedHeading } == true,
                  "Enabling application input retains the artwork that just completed its module reveal")
            check(canvas.itemCount == 0 && !canvas.isEditing, "Empty application shelf has no draft or launch target")
            check(!canvas.mouseDown(at: CGPoint(x: -1, y: 10)), "Outside clicks remain with the shell")
            canvas.perform(actionID: "apps:choose")
            check(chooserRequests == 1 && launches.isEmpty, "Choosing an application never launches one")
            check(canvas.importURLs([app]) && canvas.isEditing && store.items.isEmpty, "A valid drop opens an unsaved draft")
            check(canvas.draftName == "Fixture 0" && canvas.draftIcon == .original && renameRequests == 1, "Draft retrieves app name and starts editing with original icon")
            let iconActions = canvas.accessibleActions.filter { $0.id.hasPrefix("apps:icon:") }
            check(iconActions.count == 14 && Set(iconActions.map(\.id)).count == 14, "Original and thirteen preset icon buttons are separately accessible")
            let textBubbleAction = iconActions.first { $0.id == "apps:icon:textBubble" }!
            check(textBubbleAction.label == "Text bubble" && textBubbleAction.rect == CGRect(x: 336, y: 195, width: 48, height: 48),
                  "The icon-only text bubble fills the last slot in the existing seven-column, two-row grid")
            func visibleLabels(_ layer: CALayer) -> [String] {
                let own = (layer as? CATextLayer)?.string as? String
                return (own.map { [$0] } ?? []) + (layer.sublayers ?? []).flatMap(visibleLabels)
            }
            let draftLabels = visibleLabels(canvas.layer)
            check(!AppShortcutIcon.allCases.map(\.title).contains(where: draftLabels.contains), "Preset captions are absent from visible artwork while accessibility retains their names")
            check(!draftLabels.contains(where: { $0.contains("Drop an application") || $0.contains("Choose or drop") }), "Application helper prose is not rendered")
            check(canvas.accessibleActions.allSatisfy { canvas.layer.bounds.contains($0.rect) }, "All draft controls fit the projected canvas")
            for icon in AppShortcutIcon.allCases {
                canvas.perform(actionID: "apps:icon:" + icon.rawValue)
                check(canvas.draftIcon == icon && store.items.isEmpty && launches.isEmpty, "Changing every preset only changes the draft")
            }
            canvas.setDraftName("My renamed app")
            canvas.perform(actionID: "apps:icon:star")
            canvas.perform(actionID: "apps:save")
            let saved = store.items[0]
            check(saved.name == "My renamed app" && saved.iconPreset == .star && saves == [saved.id], "Save persists the renamed app and chosen preset")
            check(!canvas.isEditing && canvas.itemCount == 1 && launches.isEmpty, "Save returns to cards without launching")
            let launch = canvas.accessibleActions.first { $0.id == "apps:\(saved.id.uuidString):launch" }!
            _ = canvas.mouseDown(at: center(launch.rect))
            check(launches == [saved.id], "Card activation delegates the exact saved identity to the close-then-launch owner")
            canvas.perform(actionID: "apps:\(saved.id.uuidString):edit")
            check(canvas.editingID == saved.id && canvas.draftName == saved.name && canvas.draftIcon == .star, "Editing restores the current name and preset")
            canvas.setDraftName("Discarded")
            canvas.perform(actionID: "apps:cancel")
            check(store.items[0] == saved && !canvas.isEditing, "Cancel preserves the saved record")
            canvas.perform(actionID: "apps:\(saved.id.uuidString):edit")
            canvas.setDraftName(" ")
            canvas.perform(actionID: "apps:save")
            check(canvas.isEditing && store.items[0] == saved && canvas.accessibilityStatus.contains("name"), "Invalid names remain editable without overwriting the saved shortcut")
            canvas.setDraftName("Updated 中文")
            canvas.perform(actionID: "apps:icon:original")
            canvas.perform(actionID: "apps:save")
            check(store.items[0].id == saved.id && store.items[0].name == "Updated 中文" && store.items[0].iconPreset == .original, "Existing shortcuts preserve identity and can restore their original icon")
            check(!canvas.importURLs([root.appendingPathComponent("missing.app")]) && !canvas.isEditing, "Invalid or missing apps show an error rather than a false draft")
            check(!canvas.importURLs([app, app]), "Multi-app drops request one customizable application")
            for index in 1..<9 {
                let item = try fixture(index)
                _ = try store.save(candidate: store.inspect(url: item), name: "App \(index)", iconPreset: .grid)
            }
            canvas.refreshFromStore()
            let before = canvas.cardRect(for: saved.id)!
            check(canvas.scroll(at: CGPoint(x: 50, y: 90), delta: 3), "Saved cards accept continuous wheel movement")
            check(canvas.scrollOffset == 3 && canvas.cardRect(for: saved.id)!.height == before.height - 3, "A tiny wheel sample moves exactly three points without pagination")
            _ = canvas.scroll(at: CGPoint(x: 50, y: 90), delta: 10_000)
            check(canvas.cardRect(for: store.items.last!.id) != nil && canvas.cardRect(for: saved.id) == nil, "Continuous scroll reaches the final shortcut and removes hidden AX actions")
            check(canvas.accessibleActions.allSatisfy { canvas.layer.bounds.contains($0.rect) }, "Scrolled native action frames remain clipped to the canvas")
            check(!canvas.scroll(at: CGPoint(x: 10, y: 10), delta: 12) && !canvas.scroll(at: CGPoint(x: 50, y: 90), delta: .nan), "Header and invalid scrolling do not alter cards")
            L10n.language = .simplifiedChinese
            check(canvas.makeContent(for: .addApp, style: HUDModuleContentStyle(dark: false, accent: .systemGreen, contentsScale: 3)) === originalLayer, "Theme and localization preserve the retained HUD surface")
            check(canvas.accessibleActions.first?.label == "选择应用", "Application actions have Chinese labels and stable IDs")
            canvas.perform(actionID: "apps:\(saved.id.uuidString):remove")
            check(removals == ["Updated 中文"] && manager.fileExists(atPath: app.path), "Removing a shortcut leaves the original application untouched")
            let host = NSView(frame: CGRect(x: 0, y: 0, width: 400, height: 334))
            let bridge = HUDAppShortcutInteraction(canvas: canvas, host: host)
            pasteboard.clearContents(); pasteboard.writeObjects([app as NSURL])
            check(HUDAppShortcutInteraction.acceptsApplications(pasteboard), "Native application URL drops are accepted")
            check(!bridge.importPasteboard(pasteboard), "An inactive module cannot import a drop")
            bridge.setActive(true); bridge.beginExternalDrag()
            check(bridge.isInputLocked, "Dragging an app freezes shell input")
            check(bridge.importPasteboard(pasteboard) && canvas.isEditing, "An active native drop opens a draft without copying the app")
            bridge.endExternalDrag(); bridge.deactivate()
            check(!bridge.isInputLocked && host.subviews.allSatisfy(\.isHidden), "Deactivation releases drag state and hides native action proxies")
            let _ = NSApplication.shared
            let window = NSWindow(contentRect: CGRect(x: -10000, y: -10000, width: 500, height: 400), styleMask: .borderless, backing: .buffered, defer: false)
            window.isReleasedWhenClosed = false; window.contentView = host
            defer { window.close() }
            bridge.project = { $0.offsetBy(dx: 10, dy: 15) }
            bridge.setActive(true)
            check(bridge.importPasteboard(pasteboard), "A window-backed drop presents the in-HUD editor")
            let field = host.subviews.compactMap { $0 as? NSTextField }.first!
            check(field.frame == AppShortcutCanvas.nameRect.offsetBy(dx: 10, dy: 15) && bridge.isInputLocked,
                  "The temporary native name field follows HUD projection and freezes parallax")
            bridge.project = { $0.offsetBy(dx: 20, dy: 25) }; bridge.layoutAccessibility()
            check(field.frame == AppShortcutCanvas.nameRect.offsetBy(dx: 20, dy: 25), "Changing HUD projection repositions a live editor")
            field.stringValue = "Cancelled edit"
            let cancelEvent = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: window.windowNumber, context: nil, characters: "\u{1b}", charactersIgnoringModifiers: "\u{1b}", isARepeat: false, keyCode: 53)!
            check(bridge.keyDown(cancelEvent) && canvas.draftName == "Fixture 0" && field.superview == nil,
                  "Escape cancels only the temporary name edit")
            canvas.perform(actionID: "apps:name")
            let secondField = host.subviews.compactMap { $0 as? NSTextField }.first!
            secondField.stringValue = "Native name"
            let returnEvent = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: window.windowNumber, context: nil, characters: "\r", charactersIgnoringModifiers: "\r", isARepeat: false, keyCode: 36)!
            check(bridge.keyDown(returnEvent) && canvas.draftName == "Native name" && secondField.superview == nil,
                  "Return commits the native name while keeping it an unsaved draft")
            canvas.perform(actionID: "apps:save")
            check(store.items.last?.name == "Native name" && launches.count == 1, "Saving a native rename does not launch the application")
            bridge.deactivate()
            pasteboard.clearContents(); pasteboard.setString("/Applications/Example.app", forType: .string)
            check(!HUDAppShortcutInteraction.acceptsApplications(pasteboard), "Plain text paths do not masquerade as Finder drops")
            pasteboard.clearContents(); pasteboard.writeObjects([root as NSURL])
            check(!HUDAppShortcutInteraction.acceptsApplications(pasteboard), "Ordinary folders cannot initiate an app drop")
            let reopened = try AppShortcutStore(directory: root.appendingPathComponent("saved", isDirectory: true))
            check(reopened.items == store.items, "Canvas edits persist across relaunch")
            for icon in AppShortcutIcon.allCases {
                let path = AppShortcutArtwork.path(for: icon, in: CGRect(x: 10, y: 20, width: 28, height: 28))
                check(!path.isEmpty && path.boundingBoxOfPath.minX >= 10 && path.boundingBoxOfPath.minY >= 20,
                      "Every preset shares a nonempty origin-aware vector path with navigation")
            }
            var bubbleSubpaths = 0, bubbleCurves = 0
            AppShortcutArtwork.path(for: .textBubble, in: CGRect(x: 0, y: 0, width: 28, height: 28)).applyWithBlock { element in
                if element.pointee.type == .moveToPoint { bubbleSubpaths += 1 }
                if element.pointee.type == .addQuadCurveToPoint { bubbleCurves += 1 }
            }
            check(bubbleSubpaths == 3 && bubbleCurves == 4,
                  "The text bubble has a rounded speech outline and two separate short text strokes")

            var reduced = false
            let motion = AppShortcutCanvas(store: store, reduceMotion: { reduced })
            let motionHost = NSView(frame: CGRect(x: 0, y: 0, width: 400, height: 334))
            motionHost.wantsLayer = true; motionHost.layer?.addSublayer(motion.layer)
            window.contentView = motionHost
            var editorRequests = 0
            motion.onEditName = { _, _ in editorRequests += 1; check(!motion.isTransitioning, "Native editor is requested only after the artwork is settled") }
            motion.activate()
            check(motion.importURLs([app]) && motion.isTransitioning && motion.activeAnimationCount == 4,
                  "List-to-draft uses finite depth movement and opposing mask reveals")
            check(editorRequests == 0 && motion.accessibleActions.isEmpty, "Incoming editor and action frames stay hidden during the local shutter")
            let deadline = Date().addingTimeInterval(1)
            while motion.isTransitioning && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
            check(!motion.isTransitioning && motion.activeAnimationCount == 0 && editorRequests == 1 && motion.layer.sublayers?.count == 1,
                  "Completed transition removes outgoing artwork and then presents the editor")
            motion.perform(actionID: "apps:icon:camera")
            check(motion.activeAnimationCount == 2 && motion.draftIcon == .camera,
                  "Preset changes register their border and bring the preview forward with finite tracks")
            motion.perform(actionID: "apps:cancel")
            check(motion.isTransitioning && !motion.isEditing && motion.activeAnimationCount == 4,
                  "Cancel returns to the list through the opposite mechanical shutter")
            motion.deactivate()
            check(!motion.isTransitioning && motion.activeAnimationCount == 0 && motion.layer.sublayers?.count == 1,
                  "Deactivation cancels all transitions and releases the outgoing artwork")
            motion.activate(); _ = motion.importURLs([app]); motion.deactivate()
            RunLoop.main.run(until: Date().addingTimeInterval(0.04))
            check(editorRequests == 1, "Cancelled transition completions cannot open a hidden native editor")
            reduced = true; motion.activate(); _ = motion.importURLs([app])
            check(!motion.isTransitioning && motion.activeAnimationCount == 0 && editorRequests == 2,
                  "Reduced Motion commits the new screen immediately without animated editor delay")
            motion.perform(actionID: "apps:icon:bolt")
            check(motion.activeAnimationCount == 0, "Reduced Motion suppresses preset registration movement too")
            motion.deactivate()
        } catch { fatalError("AppShortcut canvas fixture failure: \(error)") }
        return count
    }
}
