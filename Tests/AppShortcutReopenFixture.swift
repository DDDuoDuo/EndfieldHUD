import AppKit

/// A disposable app used only by test-app-shortcut-launcher.sh. It intentionally
/// launches without a window, then creates one only for a real reopen event.
/// All observations are its own state; no Accessibility or other-app inspection.
private final class ReopenFixtureDelegate: NSObject, NSApplicationDelegate {
    private var window: NSWindow?
    private var reopenCount = 0
    private var observationURL: URL? {
        ProcessInfo.processInfo.environment["ENDFIELD_REOPEN_TEST_STATE"].map { URL(fileURLWithPath: $0) }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        publish(ready: true)
        // Never leave the fixture running if its parent test is interrupted.
        DispatchQueue.main.asyncAfter(deadline: .now() + 20) { NSApp.terminate(nil) }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows: Bool) -> Bool {
        reopenCount += 1
        if window == nil {
            let created = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 360, height: 130),
                                   styleMask: [.titled, .closable], backing: .buffered, defer: false)
            created.title = "EndfieldCharge — Isolated Reopen Test"
            created.isReleasedWhenClosed = false
            let label = NSTextField(labelWithString: "Disposable shortcut regression fixture")
            label.frame = NSRect(x: 20, y: 52, width: 320, height: 24)
            created.contentView?.addSubview(label)
            created.center()
            window = created
        }
        window?.makeKeyAndOrderFront(nil)
        publish(ready: true)
        return false
    }

    private func publish(ready: Bool) {
        guard let observationURL else { NSApp.terminate(nil); return }
        let state: [String: Any] = [
            "pid": ProcessInfo.processInfo.processIdentifier,
            "ready": ready,
            "reopenCount": reopenCount,
            "visible": window?.isVisible == true,
            "bundlePath": Bundle.main.bundleURL.standardizedFileURL.resolvingSymlinksInPath().path
        ]
        do {
            let data = try JSONSerialization.data(withJSONObject: state, options: [.sortedKeys])
            try data.write(to: observationURL, options: [.atomic])
        } catch { NSApp.terminate(nil) }
    }
}

@main
private enum AppShortcutReopenFixture {
    static func main() {
        let app = NSApplication.shared
        let delegate = ReopenFixtureDelegate()
        app.setActivationPolicy(.regular)
        app.delegate = delegate
        withExtendedLifetime(delegate) { app.run() }
    }
}
