import AppKit

/// Native editors are hosted in an offscreen, unordered window. These checks
/// do not show the HUD, open image choosers or touch the general clipboard.
enum HUDNotesInteractionTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("HUDNotesInteractionTests-\(UUID().uuidString)")
        let previousAppearance = HUDRuntimeAppearance.configuration
        defer {
            HUDRuntimeAppearance.configuration = previousAppearance
            try? FileManager.default.removeItem(at: directory)
        }
        do {
            let store = try NotesStore(directory: directory)
            let canvas = NotesCanvas(store: store, reduceMotion: { true })
            canvas.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 800, height: 600), creationPoint: CGPoint(x: 100, y: 80))
            let host = NSView(frame: CGRect(x: 0, y: 0, width: 800, height: 600))
            let _ = NSApplication.shared
            let window = NSWindow(contentRect: CGRect(x: -10000, y: -10000, width: 800, height: 600),
                                  styleMask: .borderless, backing: .buffered, defer: false)
            window.isReleasedWhenClosed = false; window.contentView = host
            defer { window.close() }
            let input = HUDNotesInteraction(canvas: canvas, host: host)
            defer { input.deactivate() }
            input.workspaceProject = { $0.offsetBy(dx: 10, dy: 15) }
            input.isDark = { true }
            HUDRuntimeAppearance.configuration.accentHex = "26BACC"
            input.setActive(true)
            canvas.perform(actionID: "tool:text")
            let note = store.notes[0]
            let noteRect = CGRect(x: note.x + 9, y: note.y + 29, width: note.width - 18, height: note.height - 40)
            let scroll = host.subviews.compactMap { $0 as? NSScrollView }.first!
            let editor = scroll.documentView as! NSTextView
            check(scroll.frame == noteRect.offsetBy(dx: 10, dy: 15).insetBy(dx: -2, dy: -2),
                  "A new native note editor starts on the projected workspace note")
            check(input.isInputLocked && scroll.layer?.borderColor == HUDRuntimeAppearance.accent.cgColor,
                  "The inline editor holds the pointer pose and follows the selected theme color")
            editor.string = "Draft text\n中文便笺"
            editor.setSelectedRange(NSRange(location: 2, length: 5))
            let selection = editor.selectedRange()
            let oldFontSize = editor.font!.pointSize
            let transform = CGAffineTransform(scaleX: 1.25, y: 1.25)
            input.workspaceProject = { $0.applying(transform).offsetBy(dx: 40, dy: 70) }
            input.layoutAccessibility()
            let expected = noteRect.applying(transform).offsetBy(dx: 40, dy: 70).insetBy(dx: -2, dy: -2)
            check(scroll.frame == expected && editor.font!.pointSize > oldFontSize,
                  "Committing a new parallax pose or display scale realigns and scales the existing native editor")
            check(scroll.documentView === editor && editor.string == "Draft text\n中文便笺" && editor.selectedRange() == selection,
                  "Reprojection preserves the active editor, unsaved text and selected range")
            check(editor.minSize.height == scroll.contentSize.height && editor.frame.width == scroll.contentSize.width,
                  "Text wrapping and minimum height follow the new projected viewport")
            HUDRuntimeAppearance.configuration.accentHex = "E059AC"
            input.layoutAccessibility()
            check(scroll.layer?.borderColor == HUDRuntimeAppearance.accent.cgColor && editor.selectedRange() == selection,
                  "An appearance refresh updates the editor edge without replacing the field or selection")
            input.finishEditing()
            check(store.notes[0].text == "Draft text\n中文便笺" && scroll.superview == nil && !input.isInputLocked,
                  "Finishing a reprojected editor commits once and releases its native input surface")

            canvas.perform(actionID: "note:\(note.id.uuidString):pin")
            canvas.setPresentation(notesSelected: false, animated: false)
            check(input.mouseDownInWorkspace(at: CGPoint(x: note.x + 30, y: note.y + 40), clickCount: 2),
                  "Pinned text remains editable while another center module is selected")
            let pinnedScroll = host.subviews.compactMap { $0 as? NSScrollView }.first!
            let pinnedEditor = pinnedScroll.documentView as! NSTextView
            pinnedEditor.string = "Pinned draft"
            input.deactivate()
            check(store.notes[0].text == "Pinned draft" && store.notes[0].isPinned && pinnedScroll.superview == nil,
                  "Closing commits pinned-note edits without unpinning or losing the pending text")
            check(!input.isInputLocked && host.subviews.allSatisfy(\.isHidden),
                  "Deactivation leaves no native editor or visible accessibility controls behind")
        } catch { fatalError("Notes native interaction fixture failed: \(error)") }
        return count
    }
}
