import Foundation

enum SystemStorageSettingsTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !value { fatalError(message, file: file, line: line) }
        }
        func version(_ major: Int, _ minor: Int = 0) -> OperatingSystemVersion {
            OperatingSystemVersion(majorVersion: major, minorVersion: minor, patchVersion: 0)
        }
        for major in [13, 14, 15, 26] {
            let urls = SystemStorageSettings.candidates(for: version(major))
            check(urls.first?.absoluteString == "x-apple.systempreferences:com.apple.settings.Storage",
                  "Modern macOS requests the registered Storage settings extension")
            check(urls.count == 2 && urls.last?.path == "/System/Applications/System Settings.app",
                  "An unhandled pane request has one System Settings app fallback")
        }
        for os in [version(10, 15), version(11), version(12)] {
            let urls = SystemStorageSettings.candidates(for: os)
            check(urls.first?.path == "/System/Library/CoreServices/Applications/Storage Management.app",
                  "Pre-Ventura systems open the legacy Storage Management utility")
            check(urls.count == 2 && urls.allSatisfy(\.isFileURL) && urls.last?.lastPathComponent == "System Information.app",
                  "The legacy fallback uses a local utility rather than an unsupported modern preference pane")
        }
        var attempts: [URL] = []
        check(SystemStorageSettings.open(version: version(15), using: { attempts.append($0); return true }) && attempts.count == 1,
              "Accepted primary requests never also launch the fallback")
        attempts.removeAll()
        check(SystemStorageSettings.open(version: version(15), using: { attempts.append($0); return attempts.count == 2 })
              && attempts == SystemStorageSettings.candidates(for: version(15)),
              "A rejected primary request tries the fallback in order")
        attempts.removeAll()
        check(!SystemStorageSettings.open(version: version(10, 15), using: { attempts.append($0); return false })
              && attempts.count == 2, "Failure remains explicit when both legacy launch requests are rejected")
        check(attempts.allSatisfy { $0.host == nil || $0.host?.isEmpty == true },
              "Launch candidates never use a remote website or upload local data")
        return count
    }
}
