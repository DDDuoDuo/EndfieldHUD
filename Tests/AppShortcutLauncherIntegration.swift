import AppKit
import Darwin

/// Runs against its own short-lived fixture bundle, never a user's application.
/// It invokes the production launcher, including its main-queue dispatch path.
private final class AppShortcutLauncherIntegration {
    private let applicationURL: URL
    private let stateURL: URL
    private let bundleIdentifier: String
    private var fixture: NSRunningApplication?
    private var assertions = 0
    private var completionCount = 0
    private var finished = false

    init(applicationURL: URL, stateURL: URL) {
        self.applicationURL = applicationURL.standardizedFileURL.resolvingSymlinksInPath()
        self.stateURL = stateURL
        bundleIdentifier = Bundle(url: applicationURL)?.bundleIdentifier ?? ""
    }

    func start() {
        guard !bundleIdentifier.isEmpty else { fail("Fixture bundle identifier is missing"); return }
        let config = NSWorkspace.OpenConfiguration()
        config.activates = false
        config.addsToRecentItems = false
        config.createsNewApplicationInstance = false
        config.allowsRunningApplicationSubstitution = false
        config.environment = ["ENDFIELD_REOPEN_TEST_STATE": stateURL.path]
        NSWorkspace.shared.openApplication(at: applicationURL, configuration: config) { [self] running, error in
            DispatchQueue.main.async {
                guard !self.finished else { return }
                guard let running, error == nil else {
                    self.fail("Fixture launch failed: \(error?.localizedDescription ?? "no application returned")")
                    return
                }
                self.fixture = running
                self.awaitState("windowless fixture startup") { state in
                    guard state["ready"] as? Bool == true else { return false }
                    self.check(state["visible"] as? Bool == false, "Fixture starts without a visible window")
                    self.check(state["reopenCount"] as? Int == 0, "Initial launch does not fabricate a reopen event")
                    self.check(state["pid"] as? Int32 == running.processIdentifier, "Fixture observations belong to the launched process")
                    self.reopenFixture()
                    return true
                }
            }
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 15) { [weak self] in self?.fail("Integration test timed out") }
    }

    private func reopenFixture() {
        guard !finished else { return }
        // Calling from a background queue is intentional: the launcher must
        // execute its AppKit work and return this completion on the main thread.
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            AppShortcutLauncher.launch(url: applicationURL) { [self] result in
                guard !finished else { return }
                completionCount += 1
                check(Thread.isMainThread, "Success is delivered on the main thread")
                guard case .success(let running) = result else {
                    fail("Production launcher failed to reopen the fixture: \(result)")
                    return
                }
                check(running.processIdentifier == fixture?.processIdentifier, "Reopen reuses the existing process ID")
                check(running.bundleURL?.standardizedFileURL.resolvingSymlinksInPath() == applicationURL,
                      "Reopen targets the exact chosen installation")
                check(NSRunningApplication.runningApplications(withBundleIdentifier: bundleIdentifier).count == 1,
                      "Reopen creates no duplicate fixture instance")
                awaitState("reopen event and window creation") { [self] state in
                    guard (state["reopenCount"] as? Int ?? 0) > 0, state["visible"] as? Bool == true else { return false }
                    check(state["pid"] as? Int32 == fixture?.processIdentifier, "The reused process handles the reopen event")
                    check(state["bundlePath"] as? String == applicationURL.path, "Window observations come from the exact selected bundle")
                    check(state["reopenCount"] as? Int == 1, "One shortcut request sends one reopen event")
                    check(completionCount == 1, "The launch completion fires once")
                    validateMissingApplication()
                    return true
                }
            }
        }
    }

    private func validateMissingApplication() {
        guard !finished else { return }
        let missing = applicationURL.deletingLastPathComponent().appendingPathComponent("Missing-\(UUID().uuidString).app")
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            AppShortcutLauncher.launch(url: missing) { [self] result in
                guard !finished else { return }
                check(Thread.isMainThread, "Validation errors are delivered on the main thread")
                if case .failure = result { check(true, "A missing app returns failure") }
                else { fail("A missing application unexpectedly succeeded"); return }
                finish(status: 0)
            }
        }
    }

    /// Bounded polling reads only this fixture's atomic status file. Window
    /// state is reported by the owning process, avoiding global window capture.
    private func awaitState(_ reason: String, deadline: Date = Date().addingTimeInterval(4),
                            predicate: @escaping ([String: Any]) -> Bool) {
        guard !finished else { return }
        if let data = try? Data(contentsOf: stateURL),
           let object = try? JSONSerialization.jsonObject(with: data), let state = object as? [String: Any],
           predicate(state) { return }
        guard Date() < deadline else { fail("Timed out waiting for \(reason)"); return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { [self] in
            awaitState(reason, deadline: deadline, predicate: predicate)
        }
    }

    private func check(_ condition: Bool, _ message: String) {
        guard !finished else { return }
        assertions += 1
        if !condition { fail(message) }
    }

    private func fail(_ message: String) {
        guard !finished else { return }
        fputs("FAIL: \(message)\n", stderr)
        finish(status: 1)
    }

    private func finish(status: Int32) {
        guard !finished else { return }
        finished = true
        // This identifier is generated per script run and belongs exclusively
        // to our disposable fixture; no user applications are touched.
        let fixtures = NSRunningApplication.runningApplications(withBundleIdentifier: bundleIdentifier)
            .filter { $0.bundleURL?.standardizedFileURL.resolvingSymlinksInPath() == applicationURL }
        fixtures.forEach { _ = $0.terminate() }
        waitForTermination(fixtures, deadline: Date().addingTimeInterval(3), status: status)
    }

    private func waitForTermination(_ fixtures: [NSRunningApplication], deadline: Date, status: Int32) {
        if fixtures.allSatisfy(\.isTerminated) {
            if status == 0 {
                print("PASS: \(assertions) launcher assertions; windowless running app reopened with the same PID, exact installation, one window, main-thread results; fixture terminated")
            }
            fflush(stdout)
            exit(status)
        }
        if Date() >= deadline {
            // Bound cleanup even if a test fixture crashes during shutdown.
            fixtures.filter { !$0.isTerminated }.forEach { _ = $0.forceTerminate() }
            fputs("FAIL: Disposable fixture did not terminate normally\n", stderr)
            exit(1)
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { [self] in
            waitForTermination(fixtures, deadline: deadline, status: status)
        }
    }
}

@main
private enum AppShortcutLauncherIntegrationMain {
    static func main() {
        guard CommandLine.arguments.count == 3 else {
            fputs("Usage: AppShortcutLauncherIntegration fixture.app state.json\n", stderr)
            exit(2)
        }
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let runner = AppShortcutLauncherIntegration(applicationURL: URL(fileURLWithPath: CommandLine.arguments[1]),
                                                    stateURL: URL(fileURLWithPath: CommandLine.arguments[2]))
        DispatchQueue.main.async { runner.start() }
        withExtendedLifetime(runner) { app.run() }
    }
}
