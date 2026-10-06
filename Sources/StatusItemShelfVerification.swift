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
            overlay.receiveStatusItemFiles(urls)
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
            checkClosed("Drops never reopen the dismissed HUD")
            check(overlay.shelfCountForVerification == 6, "Every accepted unique file remains in the shelf")
            check(files.allSatisfy { (try? Data(contentsOf: $0)) == payload },
                  "Shelf drops never modify, move or delete originals")
            check(overlay.eventLog.events.filter { $0.kind == .shelfAdded }.count == 6,
                  "Each added reference logs once; duplicate and rejected drops do not")
            overlay.onSystemClosed = nil
            overlay.systemPointerLocationProviderForVerification = nil
            try! FileManager.default.removeItem(at: directory)
            print("PASS: \(assertions) status-item shelf assertions; quiet closed/closing drops, retained open shell, atomic validation, deduplication, originals, clean idle")
            NSApp.terminate(nil)
        }
        func verifyClosingDrop() {
            overlay.selectSystemModule(.map, animated: false)
            overlay.closeSystemOverlay()
            let phase = overlay.systemPhase, shell = overlay.systemShellIdentity
            check(drop([files[4]]), "A closing HUD still accepts a shelf reference")
            check(overlay.systemPhase == phase && overlay.systemShellIdentity == shell,
                  "Drop preserves the in-flight closing animation")
            awaitState("ordinary close", until: { overlay.systemPhase == .closed }) {
                later(0.25) {
                    checkClosed("Closing drop does not queue a new entrance")
                    check(drop([files[5]]), "Another closed drop succeeds")
                    later(0.25, finish)
                }
            }
        }
        func verifyOpeningDrop() {
            overlay.closeSystemOverlay()
            awaitState("close before entrance test", until: { overlay.systemPhase == .closed }) {
                overlay.initialModuleRequest = .map
                check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "Explicit opening works")
                let shell = overlay.systemShellIdentity, phase = overlay.systemPhase
                check(drop([files[3]]), "Entrance accepts a drop")
                check(overlay.systemShellIdentity == shell && overlay.systemPhase == phase,
                      "Drop preserves the existing entrance and shell")
                awaitState("entrance shelf reveal", until: {
                    overlay.systemPhase == .open && overlay.systemSelectedModule == .fileShelf
                }) {
                    check(overlay.shelfCountForVerification == 4, "Opening drop is stored once")
                    // A drop queued while open must not win over immediate dismissal.
                    check(drop([files[0]]), "A duplicate open drop succeeds")
                    verifyClosingDrop()
                }
            }
        }
        func verifyOpenDrop() {
            checkSingleShell()
            overlay.selectSystemModule(.map, animated: false)
            rejectInvalidBatches(expectedCount: 2)
            let shell = overlay.systemShellIdentity
            check(drop([files[2]]), "Open HUD accepts a new file")
            check(overlay.systemSelectedModule == .map && overlay.systemShellIdentity == shell,
                  "Drop callback does not synchronously navigate or replace the shell")
            awaitState("open shelf reveal", until: { overlay.systemSelectedModule == .fileShelf }) {
                check(overlay.systemShellIdentity == shell && overlay.shelfCountForVerification == 3,
                      "Already open HUD reveals the shelf in its retained shell")
                check(drop([files[0], files[2]]), "Duplicate references are accepted")
                check(overlay.shelfCountForVerification == 3, "Duplicates do not create more records")
                later(HUDModuleContent.transitionDuration + 0.15, verifyOpeningDrop)
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
        later(0.25) {
            checkClosed("A successful menu-bar drop keeps the HUD closed after callbacks settle")
            overlay.initialModuleRequest = .map
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration),
                  "User can explicitly open the HUD after a quiet drop")
            awaitState("explicit HUD open", until: { overlay.systemPhase == .open }, then: verifyOpenDrop)
        }
    }
}
