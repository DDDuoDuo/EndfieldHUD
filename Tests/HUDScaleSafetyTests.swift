import AppKit

enum HUDScaleSafetyTests {
    private final class NoopTimer: HUDSettingsTimer { func invalidate() {} }

    static func run() -> Int {
        _ = NSApplication.shared
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        func waitUntil(_ predicate: () -> Bool) -> Bool {
            let deadline = Date().addingTimeInterval(2)
            while !predicate(), Date() < deadline {
                RunLoop.current.run(until: Date().addingTimeInterval(0.01))
            }
            return predicate()
        }
        let suite = "EndfieldHUD.ScaleSafetyTests." + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = ConfigurationStore(defaults: defaults)
        var now: TimeInterval = 100
        let controller = HUDSettingsController(store: store, clock: { now }, scheduleTimer: { _, _ in NoopTimer() })
        let safety = HUDScaleSafetyView(controller: controller, reduceMotion: { false })
        let window = NSWindow(contentRect: CGRect(x: 0, y: 0, width: 900, height: 650), styleMask: .borderless, backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false; window.contentView = safety
        defer { window.contentView = nil; window.close() }
        let confirmation = safety.subviews.compactMap { $0 as? HUDQuitConfirmationView }.first!
        controller.previewScale(0.2)
        safety.layoutSubtreeIfNeeded(); confirmation.layoutSubtreeIfNeeded()
        check(confirmation.isPresented && !safety.isHidden, "An extreme preview presents the shared recovery card")
        let originalFrame = confirmation.subviews.first!.frame
        controller.revertLayout()
        check(confirmation.isDismissing, "Revert begins its finite dismissal while keeping input modal")
        controller.previewScale(2)
        check(confirmation.isPresented && !confirmation.isDismissing && !confirmation.isHidden,
              "A rapid new preview cancels old dismissal immediately and restores active controls")
        // This main-queue barrier runs after the superseded dismissal callback,
        // even when a busy CI host cannot service it at the requested deadline.
        var staleCompletionWindowPassed = false
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.18) { staleCompletionWindowPassed = true }
        check(waitUntil { staleCompletionWindowPassed }
                && confirmation.isPresented && !confirmation.isHidden && !safety.isHidden,
              "The stale fade completion cannot conceal the new preview")
        check(controller.configuration.hudScale == 2 && store.configuration.hudScale == 1,
              "Rapid re-preview keeps only transient layout, leaving persisted settings unchanged")
        check(confirmation.subviews.first!.frame == originalFrame,
              "The recovery card size and screen center are independent of previewed HUD scale")
        confirmation.confirm()
        check(store.configuration.hudScale == 2 && controller.layoutConfirmationRemaining == nil,
              "The reactivated Keep control confirms the latest preview")
        controller.previewPosition(x: 0.8, y: -0.8)
        check(confirmation.isPresented && !confirmation.isDismissing,
              "A position preview also interrupts the previous accepted-preview dismissal")
        now += HUDSettingsController.layoutConfirmationDuration + 1
        controller.checkLayoutTimeout()
        check(waitUntil { safety.isHidden && !confirmation.isPresented },
              "The current timeout dismisses the card and releases its input coverage")
        check(store.configuration.hudOffsetX == 0 && store.configuration.hudOffsetY == 0,
              "Timed-out position changes never reach persisted settings")
        return count
    }
}
