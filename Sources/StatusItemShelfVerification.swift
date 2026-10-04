import AppKit

/// Live HUD handoff checks using only the diagnostic shelf and owned temporary
/// files. No Finder actions, global input, permission changes, or user stores.
enum StatusItemShelfVerification {
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        var assertions = 0
        func check(_ value: Bool, _ message: String) {
            assertions += 1
            guard value else {
                fputs("FAIL: Status-item shelf assertion \(assertions): \(message)\n", stderr)
                fflush(stderr)
                preconditionFailure(message)
            }
        }
        func later(_ delay: TimeInterval, _ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: action)
        }
        func awaitState(_ description: String, timeout: TimeInterval = 8,
                        until condition: @escaping () -> Bool, then completion: @escaping () -> Void) {
            let deadline = ProcessInfo.processInfo.systemUptime + timeout
            func poll() {
                if condition() { completion(); return }
                if ProcessInfo.processInfo.systemUptime >= deadline {
                    check(false, "Timed out waiting for " + description)
                    return
                }
                later(0.03, poll)
            }
            later(0.03, poll)
        }
        var configuration = AppConfiguration.defaults
        configuration.closeOnFocusLost = false
        configuration.reduceMotion = false
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("EndfieldHUD-StatusItemShelf-\(UUID().uuidString)", isDirectory: true)
        let payload = Data("Status item drop fixture\n".utf8)
        let files = (0..<8).map { directory.appendingPathComponent("fixture-\($0).txt") }
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            for file in files { try payload.write(to: file) }
        } catch { preconditionFailure("Could not create owned drop fixtures: \(error)") }
        let absent = directory.appendingPathComponent("missing.txt")
        let remote = URL(string: "https://example.invalid/not-a-file")!
        func drop(_ urls: [URL]) -> Bool {
            overlay.receiveStatusItemFiles(urls, snapshot: .unavailable, configuration: configuration)
        }
        func checkClosed(_ message: String) {
            check(overlay.systemPhase == .closed && overlay.systemShellIdentity == nil
                  && !overlay.systemWindowVisibleForVerification, message)
            check(overlay.lastClosedAnimationCount == 0 && overlay.systemAnimationCount == 0
                  && !overlay.lastClosedSourceTimerActive, "A closed shelf handoff retains no scene animations or display timer")
        }
        func checkSingleShell() {
            check(NSApp.windows.filter { $0.contentView is SystemHUDView }.count == 1,
                  "The drop uses one existing overlay panel, never a duplicate HUD")
        }
        func rejectInvalidBatches(expectedCount: Int) {
            let phase = overlay.systemPhase, shell = overlay.systemShellIdentity, module = overlay.systemSelectedModule
            for batch in [[], [remote], [absent], [files[7], remote], [files[7], absent]] as [[URL]] {
                check(!drop(batch), "Empty, remote and unavailable file batches are rejected")
                check(overlay.shelfCountForVerification == expectedCount,
                      "An invalid batch does not import its otherwise-valid prefix")
                check(overlay.systemPhase == phase && overlay.systemShellIdentity == shell
                      && overlay.systemSelectedModule == module, "Rejected drops do not change the HUD lifecycle or tab")
            }
        }
        func finish() {
            checkClosed("All drop scenarios finish with a fully closed HUD")
            check(overlay.shelfCountForVerification == 8,
                  "Cancelling presentation preserves all accepted files without adding rejected ones")
            check(files.allSatisfy { (try? Data(contentsOf: $0)) == payload },
                  "Shelf drops never modify, move or delete the original files")
            overlay.onSystemClosed = nil
            overlay.systemPointerLocationProviderForVerification = nil
            do { try FileManager.default.removeItem(at: directory) }
            catch { preconditionFailure("Could not remove owned drop fixtures: \(error)") }
            print("PASS: \(assertions) status-item shelf HUD assertions; atomic drops, duplicate references, deferred opening, retained shell, opening/closing handoff, explicit handoff priority, newest page/selection, forced cancellation, clean idle")
            NSApp.terminate(nil)
        }
        func verifyNewestClosedDrop() {
            checkClosed("Forced cancellation stays closed after queued entrance callbacks settle")
            check(overlay.shelfCountForVerification == 7,
                  "Cancelled presentations retain the seven already accepted references")
            check(drop([files[7]]), "A closed shelf with more than one page accepts its newest file")
            check(overlay.systemPhase == .closed && overlay.shelfCountForVerification == 8,
                  "The newest closed drop is persisted before deferred presentation")
            awaitState("newest shelf page after closed drop", until: {
                overlay.systemPhase == .open && overlay.systemSelectedModule == .fileShelf
            }) {
                check(overlay.shelfPageForVerification == 1 && overlay.shelfSelectedCountForVerification == 1,
                      "A closed drop opens the newest page and selects its one imported item")
                checkSingleShell()
                overlay.closeSystemOverlay()
                awaitState("last multi-page shelf close", until: { overlay.systemPhase == .closed }, then: finish)
            }
        }
        func verifyForcedCancellation() {
            overlay.closeSystemOverlay()
            awaitState("clean close before forced cancellation", until: { overlay.systemPhase == .closed }) {
                checkClosed("The final ordinary close completes before cancellation tests")
                check(drop([files[5]]), "A closed HUD accepts a file before its deferred presentation")
                check(overlay.systemPhase == .closed, "Closed drop callback returns before any new HUD opens")
                overlay.forceCloseSystemOverlay()
                later(0.15) {
                    checkClosed("Force-close cancels a drop queued while already closed")
                    overlay.initialModuleRequest = .map
                    check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration),
                          "An ordinary open still works after cancelling a queued drop")
                    check(drop([files[6]]), "Opening HUD accepts one last file")
                    overlay.forceCloseSystemOverlay()
                    checkClosed("Force-close releases the opening shell immediately")
                    later(SystemHUDView.entranceDuration + SystemHUDView.exitDuration + 0.4, verifyNewestClosedDrop)
                }
            }
        }
        func verifyExplicitClosingHandoff() {
            var handoffs = 0
            overlay.closeSystemOverlay()
            guard overlay.systemPhase == .closing else {
                check(HUDRuntimeAppearance.reduceMotion && overlay.systemPhase == .closed,
                      "Only reduced motion can omit the closing interval needed for the handoff race")
                verifyForcedCancellation()
                return
            }
            check(drop([files[0]]), "A duplicate drop is accepted while a normal close is pending")
            overlay.afterSystemClose = {
                handoffs += 1
                checkClosed("An explicit application handoff runs after a complete close")
            }
            awaitState("explicit close handoff", until: { handoffs == 1 }) {
                later(SystemHUDView.entranceDuration + 0.3) {
                    check(handoffs == 1, "The explicit close handoff runs exactly once")
                    checkClosed("A queued shelf presentation cannot reopen over an explicit close handoff")
                    check(overlay.shelfCountForVerification == 5,
                          "Explicit handoff priority preserves accepted references without duplicating them")
                    verifyForcedCancellation()
                }
            }
        }
        func verifyClosingHandoff() {
            let originalShell = overlay.systemShellIdentity
            overlay.selectSystemModule(.map, animated: false)
            var closedNotifications = 0
            overlay.onSystemClosed = {
                closedNotifications += 1
                checkClosed("A drop during closing waits for full teardown before reopening")
            }
            overlay.closeSystemOverlay()
            let phase = overlay.systemPhase
            check(phase == .closing || HUDRuntimeAppearance.reduceMotion && phase == .closed,
                  "Ordinary close enters its animation before a new drop is handled")
            check(drop([files[4]]), "Closing HUD accepts a file for its next presentation")
            if phase == .closing {
                check(overlay.systemPhase == .closing && overlay.systemShellIdentity == originalShell
                      && overlay.systemWindowVisibleForVerification,
                      "A drop does not tear down or interrupt the active closing animation")
            } else {
                check(overlay.systemPhase == .closed, "Reduced-motion close still defers the new opening")
            }
            awaitState("closing drop to reopen the shelf", until: {
                overlay.systemPhase == .open && overlay.systemSelectedModule == .fileShelf
            }) {
                check(closedNotifications == 1 && overlay.shelfCountForVerification == 5,
                      "Closing handoff completes once and imports its file once")
                checkSingleShell()
                overlay.onSystemClosed = nil
                verifyExplicitClosingHandoff()
            }
        }
        func verifyOpeningHandoff() {
            overlay.closeSystemOverlay()
            awaitState("ordinary close before opening-drop test", until: { overlay.systemPhase == .closed }) {
                checkClosed("Open-HUD drops can still close normally")
                overlay.initialModuleRequest = .map
                check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration),
                      "A fresh ordinary HUD opening starts")
                let originalShell = overlay.systemShellIdentity, phase = overlay.systemPhase
                check(originalShell != nil && (phase == .opening || HUDRuntimeAppearance.reduceMotion && phase == .open),
                      "The opening-drop fixture has a live shell")
                check(drop([files[3]]), "A drop is accepted while the HUD entrance is in progress")
                check(overlay.systemShellIdentity == originalShell && overlay.systemPhase == phase,
                      "Opening drop neither replaces the shell nor interrupts its entrance")
                check(overlay.systemSelectedModule == .map, "Drop callback does not switch tabs synchronously during entrance")
                awaitState("opening drop to reveal the shelf", until: {
                    overlay.systemPhase == .open && overlay.systemSelectedModule == .fileShelf
                }) {
                    check(overlay.systemShellIdentity == originalShell && overlay.shelfCountForVerification == 4,
                          "Entrance completion reveals the shelf in the same shell with one new reference")
                    checkSingleShell()
                    verifyClosingHandoff()
                }
            }
        }
        func verifyOpenHandoff() {
            let originalShell = overlay.systemShellIdentity
            overlay.selectSystemModule(.map, animated: false)
            rejectInvalidBatches(expectedCount: 2)
            check(drop([files[2]]), "An open HUD accepts a new file")
            check(overlay.systemPhase == .open && overlay.systemShellIdentity == originalShell
                  && overlay.systemSelectedModule == .map,
                  "Open drop callback returns without closing or synchronously switching the HUD")
            awaitState("open drop to select the shelf", until: { overlay.systemSelectedModule == .fileShelf }) {
                check(overlay.systemPhase == .open && overlay.systemShellIdentity == originalShell
                      && overlay.shelfCountForVerification == 3,
                      "An open drop switches only the section and retains the original shell")
                checkSingleShell()
                check(drop([files[0], files[2]]), "Previously cached files are accepted as successful drops")
                check(overlay.shelfCountForVerification == 3, "Existing file references are not duplicated")
                later(HUDModuleContent.transitionDuration + 0.15, verifyOpeningHandoff)
            }
        }

        if let screen = NSScreen.main?.frame {
            let pointer = CGPoint(x: screen.midX, y: screen.midY)
            overlay.systemPointerLocationProviderForVerification = { pointer }
        }
        checkClosed("Status-item drop fixture starts without a HUD")
        check(overlay.shelfCountForVerification == 0, "The diagnostic shelf begins empty")
        rejectInvalidBatches(expectedCount: 0)
        check(drop([files[0], files[0], files[1]]), "A closed HUD accepts multiple files and repeated references")
        check(overlay.shelfCountForVerification == 2, "One batch saves only unique file references")
        check(overlay.systemPhase == .closed && overlay.systemShellIdentity == nil,
              "The AppKit drop callback completes before the HUD is created")
        awaitState("closed drop to open the file shelf", until: {
            overlay.systemPhase == .open && overlay.systemSelectedModule == .fileShelf
        }) {
            checkSingleShell()
            verifyOpenHandoff()
        }
    }
}
