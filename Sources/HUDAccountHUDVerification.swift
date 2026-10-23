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
        var fixture = HUDAccountPresentation()
        var profileBefore: UserProfile?
        var count = 0, done = false
        var monitor: Any?
        init(_ overlay: OverlayController) { self.overlay = overlay }
        var canvas: HUDAccountCanvas { view.accountCanvasForVerification }
        var ids: [String] { view.designControlIDsForVerification(.account) }
        var settled: Bool { canvas.isActive && !canvas.isTransitioning }
        func item(_ id: String) -> HUDAccessibilityItem? { canvas.accessibilityItems.first { $0.id == id } }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching: [.mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate, .leftMouseDown, .leftMouseUp, .leftMouseDragged, .rightMouseDown, .rightMouseUp, .scrollWheel, .keyDown, .keyUp]) { _ in nil }
            configuration.language = .english; configuration.closeOnFocusLost = false
            overlay.settingsController!.update { $0 = configuration }; overlay.initialModuleRequest = .account
            if let screen = NSScreen.main?.frame {
                overlay.systemPointerLocationProviderForVerification = { CGPoint(x: screen.midX + 160, y: screen.midY - 60) }
            }
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD opens Account")
            wait("deployment", until: { [self] in
                overlay.systemPhase == .open && !overlay.isSwitchingSystemModule
                    && NSApp.windows.compactMap({ $0.contentView as? SystemHUDView }).first?.accountCanvasForVerification.isActive == true
            }) { [self] in
                guard let native = NSApp.windows.compactMap({ $0.contentView as? SystemHUDView }).first else { fail("No fixture HUD") }
                view = native
                check(view.sourceWatchForVerification != nil, "Account preserves the source shell")
                let gauge = view.accountGaugeForVerification
                check(!canvas.presentation.isLinked && canvas.presentation.roles.isEmpty, "Fixture starts without linked credentials or game accounts")
                check(view.designCanvasForVerification(.account) === canvas && canvas.root.bounds.size == HUDMetric.box,
                      "Account is the redesigned centre module in the 590 x 510 content box")
                check(ids.contains("connect") && ids.contains("region:0") && ids.contains("region:1")
                      && !ids.contains { ["sanity", "role", "header", "refresh", "disconnect"].contains($0) },
                      "The unlinked page offers only the region switch and Connect")
                check(item("region:0")?.role == .tab(selected: true), "Mainland China is the fixture's default region")
                check(!gauge.layer.isHidden && gauge.value.contains(" / "), "Work duration gauge is present even without an account")
                check(gauge.layer.superlayer?.name != "hud.clock.panel", "Stamina gauge does not replace the existing clock panel")
                check(gauge.layer.position == HUDAccountGauge.headerPosition && 270 + gauge.layer.frame.maxX == 730,
                      "Source stamina gauge mirrors the ENDFIELDHUD heading across the central axis")
                check(HUDAccountGauge.sourceArtworkAvailable, "The original source sanity icon and wallet textures load")
                check(HUDAccountGauge.sourceNumberFont == "HarmonyOS Sans SC Medium", "Packaged source wallet font matches the original game")
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
                escape()
                check(!gauge.isPopoverOpen && overlay.systemPhase == .open, "ESC dismisses only the recovery menu")
                click(walletPoint)
                let outsidePoint = view.accountGaugePointForVerification(CGPoint(x: gauge.popoverRect.minX - 20, y: 60))
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
                // Region selection has no network side effect while unlinked.
                view.performDesignActionForVerification(.account, "region:1")
                check(view.accountControllerForVerification.presentation.region == .global
                      && canvas.presentation.region == .global && !canvas.presentation.isLinked,
                      "The Global segment selects the international region locally and stays unlinked")
                wait("Global page", until: { self.settled }) { [self] in
                    check(item("region:1")?.role == .tab(selected: true) && ids.contains("connect") && !ids.contains("disconnect"),
                          "Each region keeps its own unlinked page with Connect")
                    // The badge's collapse refreshes the host's account state; let it
                    // settle before the presentation fixture is installed.
                    wait("the charge badge's hold and collapse", timeout: 12, until: { [self] in
                        overlay.systemChargeStageForVerification == .circle
                            && abs(overlay.systemChargeHitRectForVerification.width - HUDChargeBadge.circleHitRect.width) < 0.1
                    }) { self.linkFixture() }
                }
            }
        }
        func linkFixture() {
            profileBefore = view.profileForVerification
            let now = Date(timeIntervalSinceReferenceDate: Date().timeIntervalSinceReferenceDate.rounded())
            let role = HypergryphRole(region: .global, game: .endfield, bindingUID: "fixture", roleID: "fixture")
            fixture.isLinked = true; fixture.region = .global; fixture.status = .connected; fixture.accountName = "Fixture Endministrator"
            fixture.roles = [.init(id: "test-endfield", title: "Endfield · Fixture"), .init(id: "test-arknights", title: "Arknights · Fixture")]
            fixture.selectedRoleID = "test-endfield"; fixture.headerMode = .endfield; fixture.lastSync = "Updated 10/05 12:00:00"
            fixture.snapshot = HypergryphProfileSnapshot(role: role, observedAt: now,
                stamina: HypergryphStamina(current: 42, maximum: 360, fullRecoveryAt: now.addingTimeInterval(318 * 432)))
            canvas.now = { now }
            canvas.update(fixture)
            let linked = view.designContext.overlay.toastState.current
            check(linked?.title == "Account linked" && linked?.caption == "Global", "Linking announces the region in the shared toast")
            wait("linked page", until: { self.settled }) { [self] in
                check(canvas.presentation == fixture && !ids.contains("connect")
                      && ["disconnect", "region:1", "sanity", "role", "header", "refresh", "syncProfile", "field:name", "field:avatar"].allSatisfy(ids.contains),
                      "The linked page shows sanity, role, header display, sync and field controls with Disconnect")
                check(item("sanity")?.label == "Sanity 42 / 360" && item("role")?.role == .popUp("Endfield · Fixture")
                      && item("header")?.role == .popUp("Arknights: Endfield") && item("field:levels")?.role == .toggle(true),
                      "Linked controls read the fixture's sanity, role, header mode and synced fields")
                snapshot("account-connected-fixture")
                view.performDesignActionForVerification(.account, "sanity")
                let recovery = view.designContext.overlay.toastState.current
                check(recovery?.title == "Next 00:07:12" && recovery?.caption == "All recovered 38:09:36",
                      "The sanity plate shows the projected next and full recovery countdowns")
                view.performDesignActionForVerification(.account, "role")
                check(canvas.isMenuOpen && ids.filter { $0.hasPrefix("role:") } == ["role:test-endfield", "role:test-arknights"]
                      && item("role:test-endfield")?.role == .menuItem(selected: true),
                      "The Game role dropdown lists the fixture roles with the selection marked")
                snapshot("account-role-menu")
                escape()
                check(!canvas.isMenuOpen && overlay.systemPhase == .open && canvas.presentation == fixture,
                      "ESC closes only the role menu without choosing a role")
                view.performDesignActionForVerification(.account, "header")
                check(canvas.isMenuOpen && ids.filter { $0.hasPrefix("header:") } == HUDAccountPresentation.HeaderMode.allCases.map { "header:" + $0.rawValue }
                      && item("header:endfield")?.role == .menuItem(selected: true),
                      "The Header display dropdown offers every gauge mode with the current one marked")
                check(canvas.capturesPointer && !canvas.isInputLocked, "Account menus capture input while tilt continues")
                guard let region = item("region:0")?.rect else { fail("Region segment has no rect") }
                let behind = view.designBoxViewPointForVerification(CGPoint(x: region.midX, y: region.midY))
                check(view.hitTest(view.convert(behind, to: view.superview)) === view, "An open Account menu captures presses on the HUD")
                snapshot("account-header-menu")
                click(behind)
                check(!canvas.isMenuOpen && view.accountControllerForVerification.presentation.region == .global && canvas.presentation == fixture,
                      "The first press outside the menu only closes it; the Region segment behind it is not pressed")
                view.performDesignActionForVerification(.account, "disconnect")
                let state = view.designContext.overlay.confirmState
                check(state.isOpen && state.request?.id == "disconnect" && state.request?.title == "Disconnect this account?"
                      && state.request?.message == "Global" && state.request?.confirmTitle == "Disconnect" && state.request?.danger == true,
                      "Disconnect asks on the shared confirm bar")
                check(view.hitTest(view.convert(behind, to: view.superview)) === view, "The confirm bar captures presses on the HUD")
                snapshot("account-disconnect-confirmation")
                escape()
                check(!view.designContext.overlay.confirmState.isOpen && canvas.presentation == fixture
                      && !view.accountControllerForVerification.presentation.isLinked,
                      "ESC cancels the disconnect without an unlink request")
                wait("confirm bar hiding", until: { !self.view.designContext.overlay.capturesInput }) { [self] in
                    view.performDesignActionForVerification(.account, "header")
                    view.performDesignActionForVerification(.account, "header:hidden")
                    check(!canvas.isMenuOpen && view.accountControllerForVerification.presentation.headerMode == .hidden
                          && view.accountGaugeForVerification.layer.isHidden,
                          "Header display Hidden saves the preference and removes the complete gauge")
                    check(canvas.presentation == view.accountControllerForVerification.presentation && !canvas.presentation.isLinked,
                          "A controller change replaces the fixture with the real, still unlinked presentation")
                    view.accountControllerForVerification.perform(.selectHeaderMode(.workMode), window: nil)
                    check(!view.accountGaugeForVerification.layer.isHidden, "Work duration preference restores the unlinked fallback")
                    wait("unlinked page", until: { self.settled }) { [self] in
                        check(ids.contains("connect") && !ids.contains("header"), "The real presentation returns to the unlinked page")
                        check(view.profileForVerification == profileBefore, "UI fixture never changes personal data")
                        canvas.now = Date.init
                        finish()
                    }
                }
            }
        }
        func finish() {
            overlay.closeSystemOverlay()
            wait("close", until: { self.overlay.systemPhase == .closed }) { [self] in
                check(overlay.lastClosedAnimationCount == 0, "Closed HUD keeps no retained animation tracks")
                let shared = view.designContext.overlay
                check(!canvas.isActive && !canvas.isMenuOpen && !shared.capturesInput && shared.toastState.current == nil
                      && !view.accountGaugeForVerification.isPopoverOpen,
                      "Closing deactivates Account and clears its menus, confirm bar, toast and recovery menu")
                check(overlay.notesForVerification.isEmpty, "Account verification leaves isolated Notes untouched")
                done = true
                if let monitor { NSEvent.removeMonitor(monitor) }
                overlay.systemPointerLocationProviderForVerification = nil
                print("PASS: \(count) Account native assertions; source gauge, region, sanity, role/header menus, disconnect confirm, preferences, isolation and close")
                HUDAccountHUDVerification.session = nil; NSApp.terminate(nil)
            }
        }
        func click(_ point: CGPoint) {
            guard let window = view.window else { fail("Fixture click") }
            for type in [NSEvent.EventType.leftMouseDown, .leftMouseUp] {
                guard let event = NSEvent.mouseEvent(with: type, location: view.convert(point, to: nil), modifierFlags: [], timestamp: 0,
                    windowNumber: window.windowNumber, context: nil, eventNumber: 0, clickCount: 1,
                    pressure: type == .leftMouseDown ? 1 : 0) else { fail("Fixture click") }
                if type == .leftMouseDown { view.mouseDown(with: event) } else { view.mouseUp(with: event) }
            }
        }
        func escape() {
            guard let event = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
                windowNumber: view.window!.windowNumber, context: nil, characters: "\u{1b}",
                charactersIgnoringModifiers: "\u{1b}", isARepeat: false, keyCode: 53) else { fail("Fixture key") }
            view.keyDown(with: event)
        }
        func snapshot(_ name: String) {
            guard let output = ProcessInfo.processInfo.environment["HUD_ACCOUNT_PREVIEW_DIR"] else { return }
            let directory = URL(fileURLWithPath: output)
            do {
                try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                try view.writePNG(to: directory.appendingPathComponent(name + ".png"), scale: 1, background: NSColor(white: 0.08, alpha: 1).cgColor)
            } catch { fail("Snapshot: \(error)") }
        }
        func wait(_ label: String, timeout: TimeInterval = 20, until condition: @escaping () -> Bool, then action: @escaping () -> Void) {
            let deadline = Date().addingTimeInterval(timeout)
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
