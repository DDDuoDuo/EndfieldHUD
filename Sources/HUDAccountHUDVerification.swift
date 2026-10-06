import AppKit

/// Opt-in native fixture. No login, credential acceptance, real account, remote
/// refresh, or real profile store is used by this verification path.
enum HUDAccountHUDVerification {
    private static var session: Session?
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        let value = Session(overlay); session = value; value.start()
    }
    private final class Session {
        let overlay: OverlayController
        var view: SystemHUDView!
        var configuration = AppConfiguration.defaults
        var count = 0, done = false
        var monitor: Any?
        init(_ overlay: OverlayController) { self.overlay = overlay }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate, .leftMouseDown, .leftMouseUp, .leftMouseDragged, .rightMouseDown, .rightMouseUp, .scrollWheel, .keyDown, .keyUp]) { _ in nil }
            configuration.language = .english; configuration.closeOnFocusLost = false
            overlay.settingsController!.update { $0 = configuration }; overlay.initialModuleRequest = .account
            if let screen = NSScreen.main?.frame {
                overlay.systemPointerLocationProviderForVerification = { CGPoint(x: screen.midX + 160, y: screen.midY - 60) }
            }
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD opens Account")
            wait("deployment", until: { self.overlay.systemPhase == .open }) { [self] in
                guard let native = NSApp.windows.compactMap({ $0.contentView as? SystemHUDView }).first else { fail("No fixture HUD") }
                view = native
                check(view.sourceWatchForVerification != nil, "Account preserves the source shell")
                let canvas = view.accountCanvasForVerification, gauge = view.accountGaugeForVerification
                check(!canvas.presentation.isLinked && canvas.presentation.roles.isEmpty, "Fixture starts without linked credentials or game accounts")
                check(!gauge.layer.isHidden && gauge.value.contains(" / "), "Work duration gauge is present even without an account")
                check(gauge.layer.superlayer?.name != "hud.clock.panel", "Stamina gauge does not replace the existing clock panel")
                check(gauge.layer.position == HUDAccountGauge.headerPosition && 270 + gauge.layer.frame.maxX == 730,
                      "Source stamina gauge mirrors the ENDFIELDHUD heading across the central axis")
                check(HUDAccountGauge.sourceArtworkAvailable, "The original source sanity icon and wallet textures load")
                check(HUDAccountGauge.sourceNumberFont == "HarmonyOS Sans SC Medium", "Packaged source wallet font matches the original game")
                check(canvas.layer.bounds.size == CGSize(width: 400, height: 334), "Account shares standard central HUD geometry")
                let now = Date()
                let recovery = HypergryphSanityPresentation(game: .endfield, current: 42, maximum: 360,
                    observedAt: now, nextRecoveryAt: now.addingTimeInterval(49),
                    fullRecoveryAt: now.addingTimeInterval(38 * 3600 + 193), isRefreshing: false, refreshAvailable: true)
                gauge.update(value: "42 / 360", accessibilityLabel: "Sanity 42 of 360", visible: true,
                    accent: .systemYellow, scale: 2, sanity: recovery, at: now)
                let walletPoint = view.accountGaugePointForVerification(CGPoint(x: 90, y: 20))
                check(view.hitTest(view.convert(walletPoint, to: view.superview)) === view,
                      "Real view hit testing keeps the projected wallet above the source-shell surface")
                click(walletPoint)
                check(view.window?.firstResponder === view, "Opening recovery menu takes keyboard input away from editors behind it")
                check(gauge.isPopoverOpen && gauge.nextRecoveryText == "00:49" && gauge.fullRecoveryText == "38:03:13",
                      "Projected wallet click opens source-style recovery countdowns")
                check(view.subviews.compactMap { $0 as? NSButton }.contains { !$0.isHidden && $0.accessibilityLabel() == "Refresh" },
                      "Recovery refresh has an enabled native accessibility peer")
                snapshot("account-sanity-recovery")
                let escape = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
                    windowNumber: view.window!.windowNumber, context: nil, characters: "\u{1b}",
                    charactersIgnoringModifiers: "\u{1b}", isARepeat: false, keyCode: 53)!
                view.keyDown(with: escape)
                check(!gauge.isPopoverOpen && overlay.systemPhase == .open, "ESC dismisses only the recovery menu")
                click(walletPoint)
                let outsidePoint = view.accountGaugePointForVerification(CGPoint(x: -20, y: 60))
                let menuPoint = view.accountGaugePointForVerification(CGPoint(x: 30, y: 70))
                check(view.hitTest(view.convert(menuPoint, to: view.superview)) === view
                      && view.hitTest(view.convert(outsidePoint, to: view.superview)) === view,
                      "The visible tooltip captures both its surface and outside clicks before underlying source controls")
                click(outsidePoint)
                check(!gauge.isPopoverOpen && overlay.systemPhase == .open,
                      "Outside recovery click is consumed without closing the HUD")
                gauge.perform("toggle"); gauge.dismiss(animated: false)
                check(gauge.layer.sublayers?.first { $0.name == "hud.account.stamina.recovery" }?.animationKeys() == nil,
                      "Tooltip teardown leaves no retained animation tracks")
                view.accountControllerForVerification.perform(.selectHeaderMode(.workMode), window: nil)
                snapshot("account-unlinked")
                canvas.perform("header")
                check(view.accountInteractionForVerification?.capturesPointer == true && view.accountInteractionForVerification?.isInputLocked == false, "Account secondary menus capture input while tilt continues")
                check(canvas.popoverBounds.map { canvas.layer.bounds.contains($0) } == true, "Gauge dropdown fits the central surface")
                snapshot("account-header-menu")
                canvas.perform("header:hidden")
                check(gauge.layer.isHidden, "Header hidden preference removes the complete gauge")
                view.accountControllerForVerification.perform(.selectHeaderMode(.workMode), window: nil)
                check(!gauge.layer.isHidden, "Work duration preference restores the unlinked fallback")
                // Region selection has no network side effect while unlinked.
                view.accountControllerForVerification.perform(.selectRegion(.global), window: nil)
                check(canvas.presentation.region == .global && !canvas.presentation.isLinked, "International selection stays isolated and unlinked")
                let profileBefore = view.profileForVerification
                var fixture = HUDAccountPresentation()
                fixture.isLinked = true; fixture.region = .global; fixture.status = .connected; fixture.accountName = "Fixture Endministrator"
                fixture.roles = [.init(id: "test-endfield", title: "Endfield · Fixture"), .init(id: "test-arknights", title: "Arknights · Fixture")]
                fixture.selectedRoleID = "test-endfield"; fixture.headerMode = .endfield; fixture.lastSync = "Updated 10/05 12:00:00"
                canvas.update(fixture)
                snapshot("account-connected-fixture")
                canvas.perform("role")
                check(canvas.accessibleActions.count == 2 && canvas.accessibleActions.allSatisfy { $0.id.hasPrefix("role:") }, "Role dropdown masks controls behind it")
                snapshot("account-role-menu")
                canvas.dismissPopover(animated: false)
                canvas.perform("disconnect")
                check(canvas.accessibleActions.contains { $0.id == "menu:disconnect" }, "Disconnect opens the same tilted confirmation style")
                snapshot("account-disconnect-confirmation")
                canvas.perform("menu:cancel")
                check(view.profileForVerification == profileBefore, "UI fixture never changes personal data")
                canvas.update(view.accountControllerForVerification.presentation)
                finish()
            }
        }
        func finish() {
            overlay.closeSystemOverlay()
            wait("close", until: { self.overlay.systemPhase == .closed }) { [self] in
                check(overlay.lastClosedAnimationCount == 0, "Closed HUD keeps no retained animation tracks")
                check(view.accountCanvasForVerification.isPopoverOpen == false, "Closing dismisses secondary account controls")
                check(overlay.notesForVerification.isEmpty, "Account verification leaves isolated Notes untouched")
                done = true
                if let monitor { NSEvent.removeMonitor(monitor) }
                overlay.systemPointerLocationProviderForVerification = nil
                print("PASS: \(count) Account native assertions; retained menus, source gauge, preferences, isolation and close")
                HUDAccountHUDVerification.session = nil; NSApp.terminate(nil)
            }
        }
        func click(_ point: CGPoint) {
            guard let window = view.window, let event = NSEvent.mouseEvent(with: .leftMouseDown,
                location: view.convert(point, to: nil), modifierFlags: [], timestamp: 0,
                windowNumber: window.windowNumber, context: nil, eventNumber: 0, clickCount: 1, pressure: 1) else { fail("Fixture click") }
            view.mouseDown(with: event)
        }
        func snapshot(_ name: String) {
            guard let output = ProcessInfo.processInfo.environment["HUD_ACCOUNT_PREVIEW_DIR"] else { return }
            let directory = URL(fileURLWithPath: output)
            do {
                try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                try view.writePNG(to: directory.appendingPathComponent(name + ".png"), scale: 1, background: NSColor(white: 0.08, alpha: 1).cgColor)
            } catch { fail("Snapshot: \(error)") }
        }
        func wait(_ label: String, until condition: @escaping () -> Bool, then action: @escaping () -> Void) {
            let deadline = Date().addingTimeInterval(20)
            func poll() {
                guard !done else { return }
                NSApp.windows.compactMap { $0 as? NSPanel }.forEach { $0.ignoresMouseEvents = true }
                if condition() { action(); return }
                if Date() > deadline { fail("Timed out: " + label) }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
        }
        func check(_ value: Bool, _ message: String) { count += 1; if !value { fail(message) } }
        func fail(_ message: String) -> Never {
            done = true; fputs("FAIL: Account \(count): \(message)\n", stderr)
            if let monitor { NSEvent.removeMonitor(monitor) }; overlay.forceCloseSystemOverlay(); preconditionFailure(message)
        }
    }
}
