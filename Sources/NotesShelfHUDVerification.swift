import AppKit

/// Isolated stores and a private pasteboard; no real user notes or files change.
enum NotesShelfHUDVerification {
    static func run(overlay: OverlayController) {
        var assertions = 0
        func check(_ result: Bool, _ message: String) {
            assertions += 1
            if !result {
                fputs("FAIL: Notes/Shelf assertion \(assertions): \(message)\n", stderr)
                fflush(stderr)
                preconditionFailure(message)
            }
        }
        // Drive the same screen-coordinate provider as the live source camera.
        // Updating only a native target no longer moves the shared HUD plane.
        let screen = NSScreen.main?.frame ?? CGRect(x: 0, y: 0, width: 1280, height: 800)
        var screenPointer = CGPoint(x: screen.midX, y: screen.midY)
        overlay.systemPointerLocationProviderForVerification = { screenPointer }
        func setPointer(_ point: CGPoint) {
            screenPointer = CGPoint(x: screen.midX + point.x * screen.width / 2,
                                    y: screen.midY - point.y * screen.height / 2)
            overlay.setSystemPointerForVerification(point)
        }
        func later(_ delay: Double, _ body: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: body)
        }
        func verifyNotesRetraction(_ note: CanvasNote, scales: [Double], completion: @escaping () -> Void) {
            guard let scale = scales.first else {
                overlay.update(snapshot: .unavailable, configuration: .defaults)
                completion()
                return
            }
            var configuration = AppConfiguration.defaults
            configuration.hudScale = scale
            overlay.update(snapshot: .unavailable, configuration: configuration)
            later(0.15) {
                let shell = overlay.systemShellIdentity
                overlay.beginShelfDragForVerification()
                if !HUDRuntimeAppearance.reduceMotion {
                    check(overlay.notesFollowRetractionForVerification,
                          "Notes fold with panel timing while retaining layout scale \(scale) and a safe full-screen projection")
                    setPointer(CGPoint(x: 0.7, y: -0.6))
                    check(overlay.notesSpatialPoseMatchesPanelsForVerification,
                          "Notes keep following the pointer during retraction")
                }
                overlay.finishShelfDragForVerification(delivered: false)
                later(SystemHUDView.exitDuration + SystemHUDView.entranceDuration + 0.3) {
                    check(overlay.systemShellIdentity == shell && overlay.notesDeploymentRestoredForVerification,
                          "Reopening a cancelled drag restores note deployment without losing scale \(scale)")
                    check(overlay.visibleNotesForVerification.contains(note.id)
                          && overlay.notesForVerification.first?.x == note.x
                          && overlay.notesForVerification.first?.y == note.y,
                          "Pinned notes keep their saved screen position across closing and reopening")
                    verifyNotesRetraction(note, scales: Array(scales.dropFirst()), completion: completion)
                }
            }
        }
        func verifyPinnedNotesProjection(_ note: CanvasNote, completion: @escaping () -> Void) {
            let reduced = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
            // The far corner exercises the free workspace outside the center
            // circle, while the header pair checks rotation rather than translation.
            let points = [CGPoint(x: note.x + 12, y: note.y + 12),
                          CGPoint(x: note.x + 92, y: note.y + 12),
                          CGPoint(x: 24, y: 48)]
            setPointer(CGPoint(x: 0.8, y: -0.7))
            later(0.2) {
                check(overlay.notesSpatialPoseMatchesPanelsForVerification,
                      "Pinned notes share the side buttons' pointer pose and response after switching sections")
                let projected = points.map { overlay.projectNotesPointForVerification($0) }
                let roundTrips = zip(points, projected).allSatisfy { original, shown in
                    guard let recovered = overlay.notesWorkspacePointForVerification(shown) else { return false }
                    return hypot(recovered.x - original.x, recovered.y - original.y) < 0.001
                }
                check(roundTrips, "Projected note input recovers full-screen coordinates inside and outside the circle")
                check(reduced || abs(projected[1].y - projected[0].y) > 0.1,
                      "Note headers tilt with the HUD instead of merely sliding on screen")
                setPointer(CGPoint(x: -0.8, y: 0.7))
                later(0.2) {
                    let reversed = overlay.projectNotesPointForVerification(points[0])
                    check(overlay.notesSpatialPoseMatchesPanelsForVerification
                          && overlay.visibleNotesForVerification.contains(note.id),
                          "Pinned notes remain visible and follow the opposite pointer pose")
                    check(reduced || hypot(reversed.x - projected[0].x, reversed.y - projected[0].y) > 1,
                          "Changing pointer side moves the projected note surface")
                    verifyNotesRetraction(note, scales: [0.2, 1, 2], completion: completion)
                }
            }
        }
        _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
        later(0.85) {
            let shell = overlay.systemShellIdentity
            overlay.selectSystemModule(.notes, animated: false)
            overlay.performNoteActionForVerification("tool:todo")
            guard let note = overlay.notesForVerification.first else { preconditionFailure("Direct add failed") }
            check(overlay.visibleNotesForVerification.contains(note.id), "New notes render in the full-screen workspace")
            check(note.x > 400, "Default placement uses full-screen coordinates beyond the old box")
            overlay.selectSystemModule(.map, animated: false)
            overlay.selectSystemModule(.notes)
            let animatingNotes = overlay.systemFiniteAnimationKeys.filter { $0.hasSuffix(".notes.section") }
            check(HUDRuntimeAppearance.reduceMotion || !animatingNotes.isEmpty,
                  "Map-to-Notes starts the workspace arrival with its center handoff")
            overlay.selectSystemModule(.activityMonitor)
            check(HUDRuntimeAppearance.reduceMotion || overlay.systemFiniteAnimationKeys.filter { $0.hasSuffix(".notes.section") } == animatingNotes,
                  "A queued module click does not cancel the Notes arrival already in progress")
            overlay.selectSystemModule(.notes)
            later(0.5) {
                check(overlay.visibleNotesForVerification.contains(note.id),
                      "Replacing the queued destination with Notes preserves the note after settlement")
                overlay.performNoteActionForVerification("note:\(note.id):pin")
                overlay.selectSystemModule(.activityMonitor)
                later(0.5) {
                    check(overlay.visibleNotesForVerification == [note.id], "Pinned note survives a real center transition")
                    check(overlay.systemShellIdentity == shell, "Notes do not replace the shell")
                    verifyPinnedNotesProjection(note) {
                        overlay.performNoteActionForVerification("note:\(note.id):delete")
                        check(overlay.notesForVerification.count == 1, "Delete first asks for confirmation")
                        overlay.performNoteActionForVerification("note:\(note.id):cancelDelete")
                        check(overlay.notesForVerification.count == 1, "Cancel keeps the note")
                        overlay.performNoteActionForVerification("note:\(note.id):delete")
                        overlay.performNoteActionForVerification("note:\(note.id):confirmDelete")
                        check(overlay.notesForVerification.isEmpty, "Confirmation removes the note")
                        let pasteboard = NSPasteboard.withUniqueName()
                        pasteboard.writeObjects([URL(fileURLWithPath: "/System/Applications/Calculator.app") as NSURL])
                        check(overlay.dropFilesOnShelfNavigationForVerification(pasteboard), "A shelf tile accepts files from another module")
                        check(overlay.systemSelectedModule == .fileShelf && overlay.shelfCountForVerification == 1,
                              "Tile drop stores a reference and selects the shelf")
                        pasteboard.releaseGlobally()
                        later(0.5) {
                            overlay.beginShelfDragForVerification()
                            check(overlay.shelfDragPhaseForVerification == .retracting && overlay.systemWindowVisibleForVerification,
                                  "Outgoing drag animates while the original source window is visible")
                            overlay.finishShelfDragForVerification(delivered: false)
                            check(overlay.shelfDragPhaseForVerification == .retracting,
                                  "A fast cancellation still finishes the closing animation")
                            later(SystemHUDView.exitDuration + SystemHUDView.entranceDuration + 0.5) {
                                check(overlay.shelfDragPhaseForVerification == .idle && overlay.systemWindowVisibleForVerification,
                                      "Cancelled drag completes an entrance animation")
                                check(overlay.systemShellIdentity == shell && overlay.systemPhase == .open,
                                      "Cancelled drag restores the same shell")
                                overlay.beginShelfDragForVerification()
                                later(SystemHUDView.exitDuration + 0.3) {
                                    check(overlay.shelfDragPhaseForVerification == .hidden && !overlay.systemWindowVisibleForVerification,
                                          "Completed exit unorders the window while retaining the drag source")
                                    overlay.finishShelfDragForVerification(delivered: true)
                                    check(overlay.systemPhase == .closed && overlay.systemShellIdentity == nil,
                                          "Successful drag releases the source after closing")
                                    check(overlay.lastClosedAnimationCount == 0, "No hidden animations remain")
                                    // Finder handoff uses the same close-before-activate
                                    // contract as app shortcuts, with the bookmark alive.
                                    var reveals = 0
                                    overlay.revealShelfFile = { url in
                                        reveals += 1
                                        check(overlay.systemPhase == .closed && !overlay.systemWindowVisibleForVerification,
                                              "Finder activates only after the HUD finishes closing")
                                        check(url.lastPathComponent == "Calculator.app", "Reveal preserves the referenced URL")
                                    }
                                    _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
                                    later(0.85) {
                                        overlay.selectSystemModule(.fileShelf, animated: false)
                                        overlay.revealShelfSelectionForVerification()
                                        check(reveals == 0 && overlay.systemPhase == .closing,
                                              "Reveal starts an animated close without early Finder activation")
                                        later(SystemHUDView.exitDuration + 0.3) {
                                            check(reveals == 1, "Finder handoff runs exactly once")
                                            print("PASS: \(assertions) Notes/Shelf HUD assertions")
                                            NSApp.terminate(nil)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
