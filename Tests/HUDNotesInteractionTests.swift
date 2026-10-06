import AppKit
import QuartzCore

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
            HUDRuntimeAppearance.configuration.reduceMotion = false
            // macOS accessibility settings remain authoritative on hosted CI.
            let menuMotionEnabled = !HUDRuntimeAppearance.reduceMotion
            input.setActive(true)
            canvas.perform(actionID: "tool:text")
            let note = store.notes[0]
            let noteRect = canvas.contentViewport(for: note.id)!
            let scroll = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first!.scrollView
            let editor = scroll.documentView as! NSTextView
            let projection = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first!
            check(projection.artwork.frame == noteRect && projection.artwork.superlayer === canvas.workspaceLayer,
                  "A new note editor renders directly on the projected workspace plane")
            check(!input.isInputLocked && projection.artwork.borderColor == HUDRuntimeAppearance.accent.cgColor,
                  "The inline editor allows pointer tilt and follows the selected theme color")
            editor.insertText("Ink", replacementRange: NSRange(location: 0, length: 0))
            check((editor.textStorage?.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor)?.isEqual(NSColor.white) == true,
                  "Typing into a dark note uses explicit readable white ink")
            editor.string = "Draft text\n中文便笺"
            editor.setSelectedRange(NSRange(location: 2, length: 5))
            let selection = editor.selectedRange()
            let oldFontSize = editor.font!.pointSize
            let transform = CGAffineTransform(scaleX: 1.25, y: 1.25)
            input.workspaceProject = { $0.applying(transform).offsetBy(dx: 40, dy: 70) }
            input.layoutAccessibility()
            check(scroll.frame.size == noteRect.size && editor.font!.pointSize == oldFontSize,
                  "Reprojection transforms owned artwork while native TextKit dimensions and fonts remain stable")
            check(scroll.documentView === editor && editor.string == "Draft text\n中文便笺" && editor.selectedRange() == selection,
                  "Reprojection preserves the active editor, unsaved text and selected range")
            check(editor.minSize.height == scroll.contentSize.height && editor.frame.width == scroll.contentSize.width,
                  "Text wrapping and minimum height follow the new projected viewport")
            HUDRuntimeAppearance.configuration.accentHex = "E059AC"
            input.layoutAccessibility()
            check(projection.artwork.borderColor == HUDRuntimeAppearance.accent.cgColor && editor.selectedRange() == selection,
                  "An appearance refresh updates the editor edge without replacing the field or selection")
            input.finishEditing()
            check(store.notes[0].text == "Draft text\n中文便笺" && scroll.superview == nil && !input.isInputLocked,
                  "Finishing a reprojected editor commits once and releases its native input surface")
            check(store.notes[0].richText == nil, "Default system text remains a plain legacy note after reprojection")

            canvas.perform(actionID: "note:\(note.id.uuidString):pin")
            canvas.setPresentation(notesSelected: false, animated: false)
            check(input.mouseDownInWorkspace(at: CGPoint(x: note.x + 30, y: note.y + 40), clickCount: 2),
                  "Pinned text remains editable while another center module is selected")
            let pinnedScroll = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first!.scrollView
            let pinnedEditor = pinnedScroll.documentView as! NSTextView
            pinnedEditor.string = "Pinned draft"
            input.deactivate()
            check(store.notes[0].text == "Pinned draft" && store.notes[0].isPinned && pinnedScroll.superview == nil,
                  "Closing commits pinned-note edits without unpinning or losing the pending text")
            check(!input.isInputLocked && host.subviews.allSatisfy(\.isHidden),
                  "Deactivation leaves no native editor or visible accessibility controls behind")

            input.setActive(true); canvas.setPresentation(notesSelected: true, animated: false)
            input.workspaceProject = { $0 }
            canvas.perform(actionID: "note:\(note.id.uuidString):edit")
            let richScroll = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first!.scrollView
            let richEditor = richScroll.documentView as! NSTextView
            richEditor.string = "First range 第二段"
            richEditor.textStorage!.addAttribute(.foregroundColor, value: NSColor.systemGreen,
                                                range: NSRange(location: 0, length: 2))
            let middleRange = NSRange(location: 6, length: 5)
            richEditor.setSelectedRange(middleRange)
            input.applyFormat(.size(48))
            richEditor.layoutManager!.ensureLayout(for: richEditor.textContainer!)
            let middleGlyph = richEditor.layoutManager!.glyphIndexForCharacter(at: middleRange.location)
            check(richEditor.defaultParagraphStyle?.maximumLineHeight == 0
                  && richEditor.layoutManager!.lineFragmentRect(forGlyphAt: middleGlyph, effectiveRange: nil).height > 40,
                  "The first format in a paragraph's middle immediately releases the fixed plain line height")
            check(richEditor.selectedRange() == middleRange
                  && (richEditor.textStorage!.attribute(.font, at: 0, effectiveRange: nil) as? NSFont)?.pointSize == 12
                  && (richEditor.textStorage!.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor)?.isEqual(NSColor.systemGreen) == true,
                  "Paragraph normalization preserves the selected range and unrelated character fonts and colors")
            input.applyFormat(.size(12))
            richEditor.setSelectedRange(NSRange(location: 8, length: 0))
            input.applyFormat(.size(48))
            richEditor.insertText("X", replacementRange: NSRange(location: 8, length: 0))
            richEditor.layoutManager!.ensureLayout(for: richEditor.textContainer!)
            let insertedGlyph = richEditor.layoutManager!.glyphIndexForCharacter(at: 8)
            check((richEditor.textStorage!.attribute(.font, at: 8, effectiveRange: nil) as? NSFont)?.pointSize == 48
                  && richEditor.layoutManager!.lineFragmentRect(forGlyphAt: insertedGlyph, effectiveRange: nil).height > 40,
                  "A large character typed mid-paragraph is fully laid out before any reprojection")
            richEditor.insertText("", replacementRange: NSRange(location: 8, length: 1))
            input.applyFormat(.size(12))
            richEditor.setSelectedRange(NSRange(location: 0, length: 5))
            check(canvas.accessibleActions.filter { $0.id.contains(":format") }.allSatisfy { $0.rect.width == $0.rect.height },
                  "Editing tools use square controls")
            let richBytesBefore = try Data(contentsOf: directory.appendingPathComponent("notes.sqlite3"))
            input.applyFormat(.size(28)); input.applyFormat(.bold); input.applyFormat(.color(.systemRed))
            let afterFormat = richEditor.textStorage!
            check((afterFormat.attribute(.font, at: 0, effectiveRange: nil) as? NSFont)?.pointSize == 28
                  && (afterFormat.attribute(.font, at: 7, effectiveRange: nil) as? NSFont)?.pointSize == 12,
                  "Formatting changes the selected range without flattening its neighbours")
            check(try Data(contentsOf: directory.appendingPathComponent("notes.sqlite3")) == richBytesBefore,
                  "Formatting a draft does not write the database until the edit commits")
            richEditor.setSelectedRange(NSRange(location: 0, length: 10))
            input.applyFormat(.bold)
            var allBold = true
            richEditor.textStorage!.enumerateAttribute(.font, in: NSRange(location: 0, length: 10)) { value, _, _ in
                allBold = allBold && (value as? NSFont).map { NSFontManager.shared.traits(of: $0).contains(.boldFontMask) } == true
            }
            check(allBold, "Bold makes a mixed selection consistently bold rather than inverting each run")
            input.applyFormat(.bold)
            var anyBold = false
            richEditor.textStorage!.enumerateAttribute(.font, in: NSRange(location: 0, length: 10)) { value, _, _ in
                anyBold = anyBold || (value as? NSFont).map { NSFontManager.shared.traits(of: $0).contains(.boldFontMask) } == true
            }
            check(!anyBold, "A second Bold action clears the uniform selection while preserving font sizes")
            richEditor.setSelectedRange(NSRange(location: 0, length: 5))
            let formatButton = canvas.accessibleActions.first { $0.id.hasSuffix(":formatSpecial") }!
            let selectedText = richEditor.selectedRange()
            check(input.mouseDownInWorkspace(at: CGPoint(x: formatButton.rect.midX, y: formatButton.rect.midY), clickCount: 1)
                  && richScroll.documentView === richEditor && richEditor.selectedRange() == selectedText
                  && host.subviews.contains { $0 is NotesFormattingControls },
                  "The special-format toolbar opens its secondary controls without committing or replacing the live editor")
            richEditor.setSelectedRange(NSRange(location: 7, length: 0))
            input.applyFormat(.size(19)); input.applyFormat(.italic); input.applyFormat(.underline)
            input.applyFormat(.strikethrough); input.applyFormat(.color(.systemBlue))
            let pendingTyping = NotesRichText.capture(NSAttributedString(string: "x", attributes: richEditor.typingAttributes), defaultColor: .white).runs[0].style
            let sizeButton = canvas.accessibleActions.first { $0.id.hasSuffix(":formatSize") }!
            _ = input.mouseDownInWorkspace(at: CGPoint(x: sizeButton.rect.midX, y: sizeButton.rect.midY), clickCount: 1)
            let sizeControls = host.subviews.compactMap { $0 as? NotesFormattingControls }.first!
            let sizeReveal = sizeControls.artwork.animation(forKey: "notes.controls.reveal")
            check(menuMotionEnabled ? (sizeReveal as? CAAnimationGroup)?.duration == 0.14 : sizeReveal == nil,
                  "Formatting uses one finite reveal only when motion is enabled")
            check(sizeControls.selectedValue == "19",
                  "Reopening formatting at the caret shows the pending typing size instead of an adjacent character's size")
            check(sizeControls.artwork.superlayer === canvas.workspaceLayer && sizeControls.layer?.backgroundColor == nil
                  && !input.isInputLocked && sizeControls.subviews.allSatisfy { !($0 is NSPopUpButton) },
                  "Formatting uses retained workspace artwork with active tilt and no native popup background")
            input.workspaceProject = { $0.applying(CGAffineTransform(scaleX: 0.75, y: 0.75)) }
            input.layoutAccessibility()
            check((richEditor.textStorage!.attribute(.font, at: 0, effectiveRange: nil) as? NSFont)?.pointSize == 28
                  && (richEditor.textStorage!.attribute(.foregroundColor, at: 0, effectiveRange: nil) as? NSColor)?.isEqual(NSColor.systemRed) == true,
                  "Menu parallax moves the input surface without recapturing mixed runs or changing explicit colors")
            sizeControls.onClose?()
            check(sizeControls.superview == nil && !input.capturesPointer
                  && (sizeControls.artwork.animation(forKey: "notes.controls.dismiss") != nil) == menuMotionEnabled,
                  "Closing releases formatting input immediately and respects the motion preference")
            check((richEditor.textStorage!.attribute(.font, at: 0, effectiveRange: nil) as? NSFont)?.pointSize == 28
                  && (richEditor.textStorage!.attribute(.font, at: 7, effectiveRange: nil) as? NSFont)?.pointSize == 12,
                  "Closing the moving menu leaves every logical rich-text font intact")
            let restoredTyping = NotesRichText.capture(NSAttributedString(string: "x", attributes: richEditor.typingAttributes), scale: 1, defaultColor: .white).runs[0].style
            check(restoredTyping == pendingTyping && richEditor.selectedRange() == NSRange(location: 7, length: 0),
                  "Mixed-font reprojection preserves the caret's separate pending size, traits, color and decoration")
            check(richEditor.defaultParagraphStyle?.maximumLineHeight == 0
                  && richEditor.defaultParagraphStyle?.lineSpacing == 1,
                  "Rich editor reprojection keeps natural mixed-font line heights instead of clipping to the plain-note height")
            input.finishEditing()
            let persistedRich = try NotesStore(directory: directory).notes.first { $0.id == note.id }!
            check(persistedRich.richText?.isValid(for: persistedRich.text) == true
                  && persistedRich.richText?.runs.first?.style.fontSize == 28
                  && !host.subviews.contains { $0 is NotesFormattingControls },
                  "Commit preserves logical mixed attributes and retires the secondary formatting controls")
            check(!canvas.accessibleActions.contains { $0.id.contains(":format") }, "Settled text notes hide all editing tools")
            let shelfChoices = [NotesShelfMediaChoice(id: UUID(), title: "Owned test.png", detail: "PNG", isSupported: true, isAvailable: true),
                NotesShelfMediaChoice(id: UUID(), title: "Missing test.mov", detail: "MOV", isSupported: true, isAvailable: false)]
            input.workspaceProject = { $0 }
            input.moduleToWorkspace = { $0.applying(CGAffineTransform(scaleX: 1.25, y: 1.25)).offsetBy(dx: 30, dy: 40) }
            input.presentShelfMedia(choices: shelfChoices, at: .zero, onSelect: { _ in })
            check(host.subviews.contains { $0 is NotesShelfMediaPicker } && !input.isInputLocked && input.capturesPointer,
                  "Shelf selection stays in the existing HUD host without freezing HUD tilt")
            let picker = host.subviews.first { $0 is NotesShelfMediaPicker }!
            let shelfArtwork = (picker as! NotesShelfMediaPicker).artwork
            check(shelfArtwork.superlayer === canvas.workspaceLayer
                  && canvas.workspaceLayer.sublayers!.filter { $0 !== shelfArtwork && $0.name != "notes.secondaryMenu" }.allSatisfy { $0.zPosition < shelfArtwork.zPosition },
                  "The Shelf submenu occupies the notes plane above every note and deletion control")
            check((shelfArtwork.animation(forKey: "notes.controls.reveal") != nil) == menuMotionEnabled
                  && shelfArtwork.bounds.size == CGSize(width: 340, height: 260) && shelfArtwork.affineTransform().a == 1.25,
                  "Menu conversion scales retained geometry without changing logical row and hit-test dimensions")
            let pickerFrame = picker.frame
            input.workspaceProject = { $0.offsetBy(dx: 23, dy: 37) }
            input.layoutAccessibility()
            check(picker.frame == pickerFrame.offsetBy(dx: 23, dy: 37),
                  "The Shelf submenu follows the topmost notes plane projection while it remains open")
            let insidePicker = CGPoint(x: picker.frame.midX, y: picker.frame.midY)
            check(input.hitTestMenu(at: insidePicker) != nil && !input.dismissMenuIfOutside(at: insidePicker),
                  "Inside menu clicks retain their native target")
            input.workspaceUnproject = { _ in CGPoint(x: -100, y: -100) }
            check(input.hitTestMenu(at: insidePicker) == nil, "Projected menu hit tests use the inverse plane instead of the bounding rectangle")
            input.workspaceUnproject = nil
            check(input.dismissMenuIfOutside(at: CGPoint(x: 790, y: 590)) && !input.capturesPointer,
                  "The first outside click dismisses the Notes menu before invoking a control behind it")
            check(picker.superview == nil && (shelfArtwork.animation(forKey: "notes.controls.dismiss") != nil) == menuMotionEnabled,
                  "Shelf dismissal releases input and animates only when motion is enabled")
            canvas.perform(actionID: "tool:image")
            let source = host.subviews.first { $0 is NotesMediaSourceChooser }!
            check(!input.isInputLocked && input.capturesPointer, "The media-source submenu also keeps parallax active")
            let sourceFrame = source.frame
            input.workspaceProject = { $0.offsetBy(dx: 34, dy: 46) }; input.layoutAccessibility()
            check(source.frame == sourceFrame.offsetBy(dx: 11, dy: 9), "The media-source menu reprojects without replacing its native controls")
            let sourceArtwork = (source as! NotesMediaSourceChooser).artwork
            check(sourceArtwork.superlayer === canvas.workspaceLayer && (sourceArtwork.animation(forKey: "notes.controls.reveal") != nil) == menuMotionEnabled,
                  "The media-source picker shares the retained plane and motion preference of formatting and Shelf")
            (source as! NotesMediaSourceChooser).onCancel?()
            check(source.superview == nil && (sourceArtwork.animation(forKey: "notes.controls.dismiss") != nil) == menuMotionEnabled
                  && shelfArtwork.superlayer == nil,
                  "Source cancellation animates out and bounds retirement to a single outgoing menu")
            input.deactivate()
            check(!host.subviews.contains { $0 is NotesShelfMediaPicker } && !input.isInputLocked,
                  "Closing cancels Shelf selection and releases its input lock")
            check(sourceArtwork.superlayer == nil && sourceArtwork.animationKeys() == nil,
                  "Deactivation removes outgoing submenu artwork and its finite animation immediately")
            input.setActive(true); HUDRuntimeAppearance.configuration.reduceMotion = true
            canvas.perform(actionID: "tool:image")
            let reducedSource = host.subviews.compactMap { $0 as? NotesMediaSourceChooser }.first!
            check(reducedSource.artwork.animationKeys() == nil, "Reduce Motion skips submenu reveal movement and fade")
            reducedSource.onCancel?()
            check(reducedSource.artwork.superlayer == nil && reducedSource.artwork.animationKeys() == nil,
                  "Reduce Motion dismisses menus without leaving an outgoing layer")
            input.deactivate(); HUDRuntimeAppearance.configuration.reduceMotion = false

            let scrollingDirectory = directory.appendingPathComponent("scrolling")
            let scrollingStore = try NotesStore(directory: scrollingDirectory)
            let longText = (0..<80).map { "Paragraph \($0) 中文 👩🏽‍💻 scrolls within this note." }.joined(separator: "\n")
            let textNote = CanvasNote(kind: .text, text: longText, x: 30, y: 40, width: 205, height: 140)
            let task = NoteChecklistItem(text: "一个跨越多行的待办事项，保留中文与 emoji 👨‍👩‍👧‍👦，内容超过一行时自动换行。 A long task remains editable.")
            let taskNote = CanvasNote(kind: .todo, items: [task], x: 350, y: 40, width: 225, height: 170)
            try scrollingStore.upsert(textNote); try scrollingStore.upsert(taskNote)
            let scrollingCanvas = NotesCanvas(store: scrollingStore, reduceMotion: { true })
            scrollingCanvas.setWorkspaceBounds(host.bounds)
            let scrollingInput = HUDNotesInteraction(canvas: scrollingCanvas, host: host)
            scrollingInput.workspaceProject = { $0 }
            scrollingInput.setActive(true)
            defer { scrollingInput.deactivate() }
            let textPoint = CGPoint(x: textNote.x + 30, y: textNote.y + 45)
            check(scrollingInput.scroll(at: textPoint, delta: 28.75), "The interaction passes fractional view scrolling into the settled text card")
            check(scrollingInput.mouseDownInWorkspace(at: textPoint, clickCount: 2), "A scrolled note opens its native inline editor")
            let liveScroll = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first!.scrollView
            let liveEditor = liveScroll.documentView as! NSTextView
            check(abs(liveScroll.contentView.bounds.minY - 28.75) <= 1,
                  "Entering editing restores the canvas's logical reading position")
            check(liveEditor.textContainerInset == .zero && liveEditor.textContainer?.lineFragmentPadding == 0
                  && liveScroll.scrollerStyle == .overlay, "Native text uses the same wrapping width without a hidden padding or scroller-width change")
            liveEditor.setSelectedRange(NSRange(location: 2, length: 5))
            let liveSelection = liveEditor.selectedRange(), savedBeforeWheel = scrollingStore.notes
            let database = scrollingDirectory.appendingPathComponent("notes.sqlite3")
            let bytesBeforeWheel = try Data(contentsOf: database)
            check(scrollingInput.scroll(at: textPoint, delta: 42.125), "Bubbled wheel input is consumed over an editing card")
            check(liveScroll.documentView === liveEditor && liveScroll.superview?.superview === host
                  && liveEditor.selectedRange() == liveSelection && !scrollingInput.isInputLocked,
                  "Wheel input preserves the live native editor and selection without freezing tilt")
            check(try Data(contentsOf: database) == bytesBeforeWheel && scrollingStore.notes == savedBeforeWheel,
                  "Native or bubbled editor scrolling never commits an edit")
            liveScroll.contentView.scroll(to: CGPoint(x: 0, y: 65.5))
            liveScroll.reflectScrolledClipView(liveScroll.contentView)
            check(liveScroll.contentView.bounds.minY > 60 && liveEditor.selectedRange() == liveSelection,
                  "The native scroll view can move through long content without disturbing selection")
            let nativeReadingOffset = liveScroll.contentView.bounds.minY
            scrollingInput.finishEditing()
            check(abs(scrollingCanvas.scrollOffset(for: textNote.id) - nativeReadingOffset) < 0.01
                  && scrollingStore.notes.first { $0.id == textNote.id }?.text == longText,
                  "Finishing editing carries the viewport position back without changing the text")

            scrollingCanvas.perform(actionID: "note:\(taskNote.id.uuidString):editItem:\(task.id.uuidString)")
            let taskScroll = host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.first!.scrollView
            let taskEditor = taskScroll.documentView as! NSTextView
            let taskAction = scrollingCanvas.accessibleActions.first { $0.id == "note:\(taskNote.id.uuidString):editItem:\(task.id.uuidString)" }!
            check(taskScroll.frame.size == taskAction.rect.size && taskScroll.frame.height > 19,
                  "Wrapped task editors and projected accessibility actions use one measured, clipped rectangle")
            check(!taskScroll.hasHorizontalScroller && !taskEditor.isHorizontallyResizable
                  && taskEditor.textContainer?.widthTracksTextView == true,
                  "Checklist editors wrap and scroll long text while retaining Return-to-save behavior")
            let taskBytesBefore = try Data(contentsOf: database)
            let taskPoint = CGPoint(x: taskAction.rect.midX, y: taskAction.rect.midY)
            let taskConsumed = scrollingInput.scroll(at: taskPoint, delta: -12.5)
            let taskBytesAfter = try Data(contentsOf: database)
            check(taskConsumed && taskScroll.documentView === taskEditor && taskBytesAfter == taskBytesBefore,
                  "Scrolling a wrapped TODO cannot destroy the editor or write its draft")
            let movedSelection = NSRange(location: 0, length: min(4, (taskEditor.string as NSString).length))
            taskEditor.setSelectedRange(movedSelection)
            scrollingInput.workspaceProject = { $0.applying(CGAffineTransform(scaleX: 0.75, y: 0.75)) }
            scrollingInput.layoutAccessibility()
            check(taskScroll.documentView === taskEditor && taskEditor.selectedRange() == movedSelection
                  && taskScroll.frame.size == taskAction.rect.size,
                  "Wrapped task reprojection retains its native document and selection at the shared bounds")
            scrollingInput.deactivate()
            check(host.subviews.compactMap { $0 as? HUDProjectedTextEditor }.isEmpty && !scrollingInput.isInputLocked,
                  "Hiding the HUD removes all text scroll views without a hidden editor or background scroll task")
        } catch { fatalError("Notes native interaction fixture failed: \(error)") }
        return count
    }
}
