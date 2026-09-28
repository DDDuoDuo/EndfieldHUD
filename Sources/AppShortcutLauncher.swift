import AppKit

/// Called only after the HUD's closing animation completes. Public AppKit APIs
/// preserve normal LaunchServices/Gatekeeper behavior and target the selected
/// installation rather than another application sharing its bundle identifier.
enum AppShortcutLauncher {
    static func launch(url: URL, completion: @escaping (Result<NSRunningApplication, Error>) -> Void) {
        guard Thread.isMainThread else {
            DispatchQueue.main.async { launch(url: url, completion: completion) }
            return
        }
        let scoped = url.startAccessingSecurityScopedResource()
        let finish: (Result<NSRunningApplication, Error>) -> Void = { result in
            if scoped { url.stopAccessingSecurityScopedResource() }
            if Thread.isMainThread { completion(result) }
            else { DispatchQueue.main.async { completion(result) } }
        }
        let metadata: (name: String, bundleIdentifier: String?)
        do { metadata = try AppShortcutStore.applicationMetadata(at: url) }
        catch { finish(.failure(error)); return }
        let canonical = url.standardizedFileURL.resolvingSymlinksInPath()
        if let running = NSWorkspace.shared.runningApplications.first(where: {
            !$0.isTerminated && $0.bundleURL?.standardizedFileURL.resolvingSymlinksInPath() == canonical
        }) {
            _ = running.unhide()
            if #available(macOS 14.0, *) {
                NSApp.yieldActivation(to: running)
            }
        } else if #available(macOS 14.0, *), let identifier = metadata.bundleIdentifier {
            NSApp.yieldActivation(toApplicationWithBundleIdentifier: identifier)
        }
        // Activation alone cannot recreate a closed window. Going through
        // LaunchServices also gives an existing process the normal reopen event
        // used by Finder/Dock, while reusing this exact application installation.
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.activates = true
        configuration.createsNewApplicationInstance = false
        configuration.allowsRunningApplicationSubstitution = false
        NSWorkspace.shared.openApplication(at: url, configuration: configuration) { running, error in
            if let error { finish(.failure(error)) }
            else if let running { finish(.success(running)) }
            else { finish(.failure(AppShortcutStoreError.unavailable(L10n.text("macOS did not return an application.", "macOS 未返回应用。")))) }
        }
    }
}
