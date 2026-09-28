import AppKit

/// Run explicitly with --ui-test --app-shortcut-smoke-test. LaunchServices is
/// injected: this harness never launches a target app or changes real shortcuts.
enum AppShortcutHUDVerification {
    static func run(overlay: OverlayController) {
        var assertions = 0
        func check(_ value: Bool, _ message: String) { assertions += 1; precondition(value, message) }
        func later(_ delay: TimeInterval, _ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: action)
        }
        let target = URL(fileURLWithPath: "/System/Applications/TextEdit.app")
        let item: AppShortcut
        do { item = try overlay.addAppShortcutForVerification(target) }
        catch { preconditionFailure("App shortcut fixture: \(error)") }
        var launches = 0
        var observedCompletion: ((Result<NSRunningApplication, Error>) -> Void)?
        overlay.launchAppShortcut = { url, completion in
            launches += 1
            check(overlay.systemPhase == .closed && overlay.systemShellIdentity == nil,
                  "Launch waits until the HUD has closed and released its presentation")
            check(overlay.lastClosedAnimationCount == 0, "Close animation is completely retired before launch")
            check(url == target, "The chosen app installation is passed through")
            observedCompletion = completion
        }
        _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
        later(SystemHUDView.entranceDuration + 0.3) {
            let shell = overlay.systemShellIdentity, host = overlay.systemCenterHostIdentity
            overlay.selectSystemModule(.addApp)
            later(HUDModuleContent.transitionDuration + 0.3) {
                check(overlay.systemSelectedModule == .addApp && overlay.systemShellIdentity == shell
                      && overlay.systemCenterHostIdentity == host, "Add App retains the shared shell and host")
                check(Array(overlay.appNavigationTargetsForVerification.suffix(2)) == [.appShortcut(item.id), .module(.addApp)],
                      "The saved application is a distinct right tile before the final Add App tile")
                overlay.selectSystemModule(.notes, animated: false)
                overlay.activateAppNavigationForVerification(item.id)
                overlay.activateAppNavigationForVerification(item.id)
                check(overlay.systemPhase == .closing && launches == 0, "Click begins closing without activating a target")
                later(SystemHUDView.exitDuration + 0.3) {
                    check(launches == 1 && overlay.systemPhase == .closed, "Repeated click launches only once")
                    check(!overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults),
                          "A pending OS launch cannot race a reopened HUD")
                    observedCompletion?(.success(NSRunningApplication.current))
                    check(overlay.eventLog.events.contains { $0.kind == .appShortcutOpened && $0.detail == item.name },
                          "Successful launch adds an app action event")
                    _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
                    later(SystemHUDView.entranceDuration + 0.3) {
                        check(overlay.systemSelectedModule == .notes, "Launching a navigation shortcut preserves the last content module")
                        overlay.activateAppNavigationForVerification(item.id)
                        overlay.forceCloseSystemOverlay()
                        later(SystemHUDView.exitDuration + 0.3) {
                            check(launches == 1 && overlay.systemPhase == .closed, "Forced close cancels an unlaunched request")
                            _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
                            later(SystemHUDView.entranceDuration + 0.3) {
                                overlay.activateAppNavigationForVerification(item.id)
                                later(SystemHUDView.exitDuration + 0.3) {
                                    check(launches == 2, "A later explicit launch is accepted")
                                    observedCompletion?(.failure(NSError(domain: "ShortcutFixture", code: 1,
                                        userInfo: [NSLocalizedDescriptionKey: "Test launch failure"])))
                                    later(SystemHUDView.entranceDuration + 0.3) {
                                        check(overlay.systemPhase == .open && overlay.systemSelectedModule == .addApp
                                              && overlay.systemAppLaunchErrorForVerification == "Test launch failure",
                                              "An OS launch failure returns to an editable shortcut with the error")
                                        overlay.activateAppNavigationForVerification(item.id)
                                        later(SystemHUDView.exitDuration + 0.3) {
                                            check(launches == 3, "Retry hands off after another complete close")
                                            overlay.forceCloseSystemOverlay()
                                            observedCompletion?(.failure(NSError(domain: "ShortcutFixture", code: 2)))
                                            check(overlay.systemPhase == .closed, "A stale launch callback cannot reopen after shutdown")
                                            print("PASS: \(assertions) app-shortcut HUD assertions; shared shell, close-first launch, duplicate-click gate, cancellation, errors and retry")
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
