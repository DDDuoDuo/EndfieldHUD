import AppKit

/// User-initiated handoff through LaunchServices. The modern pane identifier is
/// declared by Storage.appex; its extension metadata explicitly enables the
/// x-apple.systempreferences scheme. No AppleScript or private framework call.
enum SystemStorageSettings {
    static func candidates(for version: OperatingSystemVersion) -> [URL] {
        if version.majorVersion >= 13 {
            return [URL(string: "x-apple.systempreferences:com.apple.settings.Storage")!,
                    URL(fileURLWithPath: "/System/Applications/System Settings.app", isDirectory: true)]
        }
        // Catalina through Monterey expose the detailed report through the
        // Storage Management utility. System Information is a manual fallback
        // when that utility is missing; it does not promise a selected tab.
        return [URL(fileURLWithPath: "/System/Library/CoreServices/Applications/Storage Management.app", isDirectory: true),
                URL(fileURLWithPath: "/System/Applications/Utilities/System Information.app", isDirectory: true)]
    }

    /// True means macOS accepted a launch request. A settings URL can be
    /// accepted without the requested pane becoming visible on a future OS.
    @discardableResult
    static func open(version: OperatingSystemVersion = ProcessInfo.processInfo.operatingSystemVersion,
                     using opener: (URL) -> Bool = { NSWorkspace.shared.open($0) }) -> Bool {
        for url in candidates(for: version) where opener(url) { return true }
        return false
    }
}
