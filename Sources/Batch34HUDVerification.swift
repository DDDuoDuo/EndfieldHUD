import AppKit

/// Native projection, handoff and lifecycle checks in the existing isolated app.
enum Batch34HUDVerification {
    private static var session: Session?
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        let value = Session(overlay); session = value; value.start()
    }
    private final class Session {
        let overlay: OverlayController
        var view: SystemHUDView!
        var count = 0, done = false
        var monitor: Any?
        var drawingID: UUID?
        init(_ overlay: OverlayController) { self.overlay = overlay }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate,
                .leftMouseDown, .leftMouseUp, .leftMouseDragged, .rightMouseDown, .rightMouseUp, .scrollWheel, .keyDown, .keyUp]) { _ in nil }
            let frame = NSScreen.main!.frame
            overlay.systemPointerLocationProviderForVerification = { CGPoint(x: frame.midX, y: frame.midY) }
            var config = AppConfiguration.defaults; config.closeOnFocusLost = false; config.language = .english
            overlay.settingsController!.update { $0 = config }; overlay.initialModuleRequest = .notes
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: config), "Isolated HUD opens")
            wait("deployment", until: { self.overlay.systemPhase == .open }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                if CommandLine.arguments.contains("--compact-viewport-smoke-test") {
                    view.window?.setContentSize(NSSize(width: 1024, height: 768))
                    view.frame.size = NSSize(width: 1024, height: 768)
                    view.layoutSubtreeIfNeeded()
                }
                check(view.sourceWatchForVerification != nil, "Original source shell renders")
                check(overlay.notesForVerification.isEmpty, "No real Notes loaded")
                if CommandLine.arguments.contains("--compact-viewport-smoke-test") {
                    // Give AppKit's window-size notification and retained
                    // projection one turn to settle before testing the editor.
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [self] in testDrawing() }
                } else { testDrawing() }
            }
        }
        func testDrawing() {
            let note = CanvasNote(kind: .drawing, x: 170, y: 165, width: 230, height: 180, isPinned: true, drawing: NotesDrawing())
            drawingID = note.id
            do { try view.installNoteForVerification(note) } catch { fail("Fixture insertion: \(error)") }
            let start = CGPoint(x: note.x + 40, y: note.y + 70), end = CGPoint(x: note.x + 170, y: note.y + 90)
            send(.mouseMoved, workspace: start)
            send(.leftMouseDown, workspace: start)
            send(.leftMouseDragged, workspace: end)
            check(overlay.notesForVerification.first { $0.id == note.id }?.drawing?.strokes.isEmpty == true,
                  "Drawing movement does not persist each frame")
            send(.leftMouseUp, workspace: end)
            check(overlay.notesForVerification.first { $0.id == note.id }?.drawing?.strokes.count == 1,
                  "Projected native drag commits one stroke")
            check(overlay.eventLog.events.filter { $0.kind == .noteAction }.last?.metadata == ["action": "drawingEdited"],
                  "Drawing log excludes coordinates and content")
            send(.rightMouseDown, workspace: start)
            send(.leftMouseDown, workspace: start); send(.leftMouseDragged, workspace: end); send(.leftMouseUp, workspace: end)
            check(overlay.notesForVerification.first { $0.id == note.id }?.drawing?.strokes.isEmpty == true,
                  "Projected right-click enables erasing")
            let rich = NotesRichText(runs: [NotesTextRun(location: 0, length: 5, style: NotesTextStyle(fontSize: 24, bold: true))])
            let text = CanvasNote(kind: .text, text: "Mixed normal\nSecond line\nThird line\nFourth line\nFifth line\nSixth line", x: 440, y: 165, width: 250, height: 160, richText: rich)
            do { try view.installNoteForVerification(text) } catch { fail("Rich fixture insertion: \(error)") }
            check(!view.notesCanvasForVerification.accessibleActions.contains { $0.id.contains(text.id.uuidString) && $0.id.contains(":format") },
                  "Formatting stays hidden outside text editing")
            view.notesCanvasForVerification.perform(actionID: "note:\(text.id.uuidString):edit")
            testProjectedEditor(label: "notes") { [self] in testFormatting(textID: text.id) }
        }
        func testFormatting(textID: UUID) {
            let controls = view.notesCanvasForVerification.accessibleActions.filter { $0.id.contains(textID.uuidString) && $0.id.contains(":format") }
            check(controls.count == 4, "Four formatting controls share the text note")
            let button = controls.first { $0.id.hasSuffix(":formatSpecial") }!
            send(.leftMouseDown, workspace: CGPoint(x: button.rect.midX, y: button.rect.midY))
            check(view.subviews.contains { $0 is NotesDesktopPopup }, "Projected formatting action opens in-HUD controls")
            // The redesigned centre lists Media's sources in its own menu (NotesCanvas).
            view.notesCanvasForVerification.perform(actionID: "tool:image")
            check(view.notesCanvasForVerification.isMenuOpen && !view.subviews.contains { $0 is NotesDesktopPopup },
                  "Add media closes the desktop formatting controls and opens the centre's source menu")
            if let i = CommandLine.arguments.firstIndex(of: "--preview-directory"), CommandLine.arguments.indices.contains(i + 1) {
                let directory = URL(fileURLWithPath: CommandLine.arguments[i + 1], isDirectory: true)
                do {
                    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                    try view.writePNG(to: directory.appendingPathComponent("notes-menu.png"), scale: 1, presentation: false, background: .black)
                } catch { fail("Notes menu preview: \(error)") }
            }
            overlay.selectSystemModule(.profile)
            wait("profile editor", until: { self.overlay.systemSelectedModule == .profile && !self.overlay.isSwitchingSystemModule
                && self.view.designControlIDsForVerification(.profile).contains("profile:name") }) { [self] in
                view.performDesignActionForVerification(.profile, "profile:name")
                testProjectedEditor(label: "profile") { [self] in leaveNotes(pinnedNote: drawingID!) }
            }
        }
        func leaveNotes(pinnedNote: UUID) {
            overlay.selectSystemModule(.nowPlaying)
            let music = view.nowPlayingCanvasForVerification
            check(music.isPresented && music.isActive == HUDRuntimeAppearance.reduceMotion,
                  "Incoming music prepares before animated navigation, or enables input immediately when motion is reduced")
            wait("Now Playing", until: { self.overlay.systemSelectedModule == .nowPlaying && !self.overlay.isSwitchingSystemModule }) { [self] in
                check(!view.subviews.contains { $0 is NotesDesktopPopup }, "Leaving Notes retires formatting controls")
                check(!view.notesCanvasForVerification.isMenuOpen, "Leaving Notes closes the media menu")
                check(view.visibleNotesForVerification == [pinnedNote], "Pinned drawing persists across module transition")
                // The persistence check is complete. Notes use screen points
                // while music uses a scaled HUD plane, so a retained pin can
                // cover a native transport target on a smaller test display.
                view.performNoteActionForVerification("note:\(pinnedNote.uuidString):pin")
                wait("independent music controls", until: { self.view.visibleNotesForVerification.isEmpty }) { [self] in
                    check(view.visibleNotesForVerification.isEmpty
                          && overlay.notesForVerification.first { $0.id == pinnedNote }?.isPinned == false,
                          "Drawing remains saved and no note pin occludes the independent music controls")
                    testPlayback()
                }
            }
        }
        func testProjectedEditor(label: String, then completion: @escaping () -> Void) {
            guard let editor = view.subviews.compactMap({ $0 as? HUDProjectedTextEditor }).first else {
                fail("The " + label + " field has a shared projected editor")
            }
            // A redesigned field holds its tilted plane still while it edits
            // (HUDDesignCanvas.isInputLocked); the Notes editor keeps following
            // the pointer tilt. The source camera stops with its next frame.
            guard label != "notes" else { probeProjectedEditor(editor, label: label, holdsPlane: false, then: completion); return }
            check(view.designCanvasForVerification(.profile)?.isInputLocked == true,
                  "The " + label + " field holds its tilted plane still while it edits")
            wait(label + " held plane", until: { self.view.sourceWatchForVerification?.pointerIsAnimatingForVerification == false }) { [self] in
                probeProjectedEditor(editor, label: label, holdsPlane: true, then: completion)
            }
        }
        func probeProjectedEditor(_ editor: HUDProjectedTextEditor, label: String, holdsPlane: Bool, then completion: @escaping () -> Void) {
            let text = editor.textView
            check(view.window?.firstResponder === text && editor.artwork.superlayer != nil,
                  "The " + label + " TextKit editor owns focus and retained HUD artwork")
            let end = (text.string as NSString).length
            text.setSelectedRange(NSRange(location: end, length: 0))
            text.setMarkedText("中文", selectedRange: NSRange(location: 2, length: 0),
                               replacementRange: NSRange(location: NSNotFound, length: 0))
            let marked = text.markedRange(), selected = text.selectedRange(), draft = text.string
            editor.resizeDocument(); editor.captureVisibleArtwork()
            var actual = NSRange()
            let before = text.firstRect(forCharacterRange: marked, actualRange: &actual)
            let stillPlane = HUDRuntimeAppearance.reduceMotion || holdsPlane
            let probeStarted = ProcessInfo.processInfo.systemUptime
            let screen = view.window!.screen!.frame
            overlay.systemPointerLocationProviderForVerification = { CGPoint(x: screen.minX + screen.width * 0.78,
                                                                             y: screen.minY + screen.height * 0.7) }
            view.setPointerForVerification(CGPoint(x: 0.56, y: -0.4))
            wait(label + " editing projection", until: {
                // Reduced motion and a held plane keep the projection still.
                // Observe it through a short pointer-update interval instead
                // of waiting for movement that is intentionally suppressed.
                if stillPlane { return ProcessInfo.processInfo.systemUptime - probeStarted >= 0.25 }
                var range = NSRange()
                let after = text.firstRect(forCharacterRange: marked, actualRange: &range)
                return abs(after.midX - before.midX) + abs(after.midY - before.midY) > 1
            }) { [self] in
                var range = NSRange()
                let after = text.firstRect(forCharacterRange: marked, actualRange: &range)
                let stable = abs(after.minX - before.minX) < 0.1 && abs(after.minY - before.minY) < 0.1
                    && abs(after.width - before.width) < 0.1 && abs(after.height - before.height) < 0.1
                let valid = after.minX.isFinite && after.minY.isFinite
                    && after.width.isFinite && after.height.isFinite && after.width > 0 && after.height > 0
                check(valid && (stillPlane ? stable : abs(after.midX - before.midX) + abs(after.midY - before.midY) > 1),
                      "The " + label + " candidate rectangle follows pointer tilt, or remains stable on a held plane or with reduced motion: \(before) -> \(after)")
                check(view.window?.firstResponder === text && text.hasMarkedText() && text.markedRange() == marked
                      && text.selectedRange() == selected && text.string == draft,
                      "The " + label + " composition, selection and focus survive pointer updates")
                check(editor.artwork.frame == editor.logicalRect && editor.scrollView.frame.size == editor.logicalRect.size,
                      "The " + label + " glyphs inherit HUD perspective without changing native layout dimensions")
                let local = CGPoint(x: editor.logicalRect.midX, y: editor.logicalRect.midY)
                let projected: CGPoint
                if label == "notes" { projected = view.projectNotesPointForVerification(local) }
                else {
                    guard let canvas = view.designCanvasForVerification(.profile), let plane = editor.artwork.superlayer else {
                        fail("The " + label + " editor sits on the redesigned card")
                    }
                    projected = view.designBoxViewPointForVerification(canvas.root.convert(local, from: plane))
                }
                check(view.hitTest(view.convert(projected, to: view.superview)) === text,
                      "Native hit testing selects the " + label + " editor on its actual tilted plane")
                check(editor.retainedPixelCount > 0 && editor.retainedPixelCount <= HUDProjectedTextEditor.maximumPixels,
                      "The " + label + " viewport artwork stays bounded while editing")
                if let index = CommandLine.arguments.firstIndex(of: "--preview-directory"), CommandLine.arguments.indices.contains(index + 1) {
                    let directory = URL(fileURLWithPath: CommandLine.arguments[index + 1], isDirectory: true)
                    do {
                        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                        try view.writePNG(to: directory.appendingPathComponent(label + "-editor.png"), scale: 1, presentation: false, background: .black)
                    } catch { fail("Projected editor preview: \(error)") }
                }
                text.insertText("中文", replacementRange: marked)
                check(!text.hasMarkedText(), "The " + label + " native composition commits after the projection check")
                overlay.systemPointerLocationProviderForVerification = { CGPoint(x: screen.midX, y: screen.midY) }
                view.setPointerForVerification(.zero)
                completion()
            }
        }
        func testPlayback() {
            let canvas = view.nowPlayingCanvasForVerification
            func item(_ id: String) -> HUDAccessibilityItem? { canvas.accessibilityItems.first { $0.id == id } }
            func press(_ id: String) { view.performDesignActionForVerification(.nowPlaying, id) }
            func shown(_ box: CGRect) -> CGRect {
                let a = view.designBoxViewPointForVerification(box.origin), b = view.designBoxViewPointForVerification(CGPoint(x: box.maxX, y: box.maxY))
                return CGRect(x: min(a.x, b.x), y: min(a.y, b.y), width: abs(b.x - a.x), height: abs(b.y - a.y))
            }
            func centre(_ rect: CGRect) -> CGPoint { CGPoint(x: rect.midX, y: rect.midY) }
            func seeks() -> Int { overlay.eventLog.events.filter { $0.kind == .playbackAction && $0.metadata["action"] == "seek" }.count }
            let buttons = ["previous", "playPause", "next", "volume", "lyrics"]
            check(overlay.nowPlaying.snapshot.application?.source == .music, "Injected player supplies metadata without a real app")
            check(canvas.albumCoverAvailableForVerification, "Synthetic album cover is displayed without accessing a real media library")
            check(canvas.panelFrameForVerification == CGRect(x: 55, y: -84, width: 480, height: 480) && !canvas.showsHeader,
                  "The 480 px cover reaches above the box and the page has no module header")
            let ids = view.designControlIDsForVerification(.nowPlaying)
            check(Set(buttons + ["seek"]).isSubset(of: ids) && !ids.contains("refresh"),
                  "Transport, seek, volume and lyrics are live controls without a refresh button")
            view.setChargeHoveredForVerification(true)
            let charge = view.chargeViewRectForVerification
            check(buttons.compactMap(item).allSatisfy { !shown($0.rect).intersects(charge) },
                  "Music controls stay clear of the expanded battery capsule on the source HUD plane")
            view.setChargeHoveredForVerification(false)
            let fixed = (buttons + ["seek"]).compactMap { item($0)?.rect }
            press("volume")
            check(canvas.isVolumeOpen && canvas.capturesPointer && item("appVolume").map { $0.rect.height > $0.rect.width } == true,
                  "Volume opens a vertical pop-up that takes the next press")
            let volume = item("appVolume")?.rect
            press("lyrics")
            check(!canvas.showsLyrics && (buttons + ["seek"]).compactMap { item($0)?.rect } == fixed && item("appVolume")?.rect == volume,
                  "Hiding lyrics leaves the seek bar, buttons and volume pop-up in place")
            press("lyrics")
            check(canvas.showsLyrics && (buttons + ["seek"]).compactMap { item($0)?.rect } == fixed,
                  "Showing lyrics also keeps every control in place")
            sendBox(.leftMouseDown, point: centre(canvas.panelFrameForVerification)); sendBox(.leftMouseUp, point: centre(canvas.panelFrameForVerification))
            check(!canvas.isVolumeOpen && overlay.nowPlaying.snapshot.track?.isPlaying != true,
                  "The first press outside the pop-up only closes it")
            check(overlay.audio.isRunning && !canvas.progressClockActiveForVerification, "Shared audio starts; paused media has no clock")
            check(overlay.perAppAudio.sessions.isEmpty, "Opening Now Playing creates no audio tap")
            guard let play = item("playPause")?.rect else { fail("Play is a visible control") }
            let playPoint = view.designBoxViewPointForVerification(centre(play))
            check(view.hitTest(view.convert(playPoint, to: view.superview)) === view,
                  "Foreground music control owns native hit testing above source navigation")
            if let index = CommandLine.arguments.firstIndex(of: "--preview-directory"), CommandLine.arguments.indices.contains(index + 1) {
                let directory = URL(fileURLWithPath: CommandLine.arguments[index + 1], isDirectory: true)
                do { try view.writePNG(to: directory.appendingPathComponent("music-controls.png"), scale: 1, presentation: false, background: .black) }
                catch { fail("Music control preview: \(error)") }
            }
            sendBox(.leftMouseDown, point: centre(play)); sendBox(.leftMouseUp, point: centre(play))
            check(view.selectedModule == .nowPlaying && overlay.nowPlaying.snapshot.track?.isPlaying == true && canvas.progressClockActiveForVerification,
                  "A real press on Play starts playback and its visible progress clock")
            view.keyDown(with: space()); check(overlay.nowPlaying.snapshot.track?.isPlaying == false, "Space pauses through the host's key route")
            view.keyDown(with: space()); check(overlay.nowPlaying.snapshot.track?.isPlaying == true, "Space resumes playback")
            guard let seek = item("seek")?.rect else { fail("The timeline is a visible control") }
            let start = CGPoint(x: seek.minX + 2, y: seek.midY), end = CGPoint(x: seek.midX, y: seek.midY)
            let before = seeks()
            sendBox(.leftMouseDown, point: start)
            for index in 0...20 { sendBox(.leftMouseDragged, point: CGPoint(x: start.x + (end.x - start.x) * CGFloat(index) / 20, y: start.y)) }
            check(seeks() == before, "Seek movement previews locally")
            sendBox(.leftMouseUp, point: end)
            check(seeks() == before + 1, "Seek release emits one successful command")
            overlay.selectSystemModule(.volume)
            wait("audio handoff", until: { self.overlay.systemSelectedModule == .volume && !self.overlay.isSwitchingSystemModule }) { [self] in
                check(!canvas.isPresented && !canvas.albumCoverAvailableForVerification, "Leaving Now Playing releases the displayed cover")
                check(overlay.audio.isRunning && !canvas.progressClockActiveForVerification, "Volume retains shared observation while playback clock stops")
                overlay.selectSystemModule(.nowPlaying)
                check(canvas.isPresented && canvas.isActive == HUDRuntimeAppearance.reduceMotion && canvas.albumCoverAvailableForVerification,
                      "Returning restores cached music before animated input activation, or immediately with reduced motion")
                wait("reverse audio handoff", until: { self.overlay.systemSelectedModule == .nowPlaying && !self.overlay.isSwitchingSystemModule }) { [self] in
                    check(canvas.isActive && canvas.albumCoverAvailableForVerification, "Returning restores the cached album cover")
                    check(overlay.audio.isRunning && canvas.progressClockActiveForVerification, "Reverse handoff keeps audio and restores progress")
                    overlay.closeSystemOverlay()
                    wait("close", until: { self.overlay.systemPhase == .closed }) { [self] in
                        check(!canvas.isPresented && !canvas.progressClockActiveForVerification && !overlay.audio.isRunning,
                              "Closing retires progress and shared audio observation")
                        check(overlay.lastClosedAnimationCount == 0 && !overlay.lastClosedSourceTimerActive, "All shell animation clocks retire")
                        reopenMusic()
                    }
                }
            }
        }
        func reopenMusic() {
            overlay.initialModuleRequest = .nowPlaying
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: overlay.settingsController!.configuration),
                  "Music can be summoned as the initial section")
            view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
            let music = view.nowPlayingCanvasForVerification
            check(music.isPresented && !music.isActive, "Music observation starts during HUD opening while playback input remains disabled")
            check(music.presentationStartCountForVerification == 1, "Opening starts music presentation once")
            wait("music reopening", until: { self.overlay.systemPhase == .open }) { [self] in
                check(music.isActive && music.presentationStartCountForVerification == 1,
                      "Music input activates after opening without restarting observation")
                overlay.closeSystemOverlay()
                wait("music reclose", until: { self.overlay.systemPhase == .closed }) { [self] in
                    check(!music.isPresented && !music.isActive && !music.progressClockActiveForVerification
                          && !overlay.audio.isRunning && overlay.lastClosedAnimationCount == 0,
                          "Repeated close removes every music observation, control and animation")
                    cleanup(); print("PASS: \(count) batch 3/4 native HUD assertions; drawing, menus, playback, seek, cached presentation, audio handoff and reopen")
                    Batch34HUDVerification.session = nil; NSApp.terminate(nil)
                }
            }
        }
        func send(_ type: NSEvent.EventType, workspace: CGPoint) { deliver(type, point: view.projectNotesPointForVerification(workspace)) }
        func sendBox(_ type: NSEvent.EventType, point: CGPoint) { deliver(type, point: view.designBoxViewPointForVerification(point)) }
        func space() -> NSEvent {
            NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                             windowNumber: view.window!.windowNumber, context: nil, characters: " ", charactersIgnoringModifiers: " ",
                             isARepeat: false, keyCode: 49)!
        }
        func deliver(_ type: NSEvent.EventType, point: CGPoint) {
            check(view.bounds.contains(point), "Native projected fixture fits HUD")
            let event = NSEvent.mouseEvent(with: type, location: view.convert(point, to: nil), modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: view.window!.windowNumber, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
            switch type {
            case .mouseMoved: view.mouseMoved(with: event)
            case .leftMouseDown: view.mouseDown(with: event)
            case .leftMouseDragged: view.mouseDragged(with: event)
            case .leftMouseUp: view.mouseUp(with: event)
            case .rightMouseDown: view.rightMouseDown(with: event)
            default: break
            }
        }
        func wait(_ label: String, until condition: @escaping () -> Bool, then action: @escaping () -> Void) {
            let deadline = ProcessInfo.processInfo.systemUptime + 15
            func poll() {
                guard !done else { return }
                NSApp.windows.compactMap { $0 as? NSPanel }.forEach { $0.ignoresMouseEvents = true }
                if condition() { action(); return }
                if ProcessInfo.processInfo.systemUptime > deadline { fail("Timed out: " + label) }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
        }
        func check(_ value: Bool, _ message: String) { count += 1; if !value { fail(message) } }
        func cleanup() { done = true; if let monitor { NSEvent.removeMonitor(monitor) }; overlay.systemPointerLocationProviderForVerification = nil }
        func fail(_ message: String) -> Never { fputs("FAIL: Batch 3/4 \(count): \(message)\n", stderr); cleanup(); overlay.forceCloseSystemOverlay(); preconditionFailure(message) }
    }
}
