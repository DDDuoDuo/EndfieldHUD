import AppKit
import ServiceManagement

/// Login settings are read from the operating system, never inferred from a saved toggle.
/// Construction and status reads have no side effects. Registration follows the application’s saved launch-at-login preference.
final class LoginItemManager {
    private let fileManager = FileManager.default
    private let bundle: Bundle

    init(bundle: Bundle = .main) {
        self.bundle = bundle
    }

    private var identifier: String {
        bundle.bundleIdentifier ?? "io.github.endfieldcharge.EndfieldCharge"
    }

    private var legacyURL: URL {
        fileManager.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/LaunchAgents", isDirectory: true)
            .appendingPathComponent(identifier + ".plist")
    }

    private var hasLegacyItem: Bool {
        guard let data = try? Data(contentsOf: legacyURL),
              let plist = try? PropertyListSerialization.propertyList(from: data, format: nil),
              let dictionary = plist as? [String: Any],
              dictionary["Label"] as? String == identifier,
              dictionary["RunAtLoad"] as? Bool == true,
              let arguments = dictionary["ProgramArguments"] as? [String],
              let executable = arguments.first else { return false }
        return executable == bundle.executableURL?.path && fileManager.isExecutableFile(atPath: executable)
    }

    var isEnabled: Bool {
        if #available(macOS 13.0, *) {
            return SMAppService.mainApp.status == .enabled || hasLegacyItem
        }
        return hasLegacyItem
    }

    var requiresApproval: Bool {
        if #available(macOS 13.0, *) {
            return SMAppService.mainApp.status == .requiresApproval
        }
        return false
    }

    var statusDescription: String {
        if requiresApproval {
            return L10n.text("Approval needed in System Settings → Login Items.",
                             "请在系统设置 → 登录项中允许启动。")
        }
        if isEnabled {
            return L10n.text("Enabled", "已启用")
        }
        return L10n.text("Disabled", "已停用")
    }

    // The properties read live OS state; provided as a semantic refresh hook for callers.
    func refreshStatus() {}

    func setEnabled(_ enabled: Bool) throws {
        if enabled { try validateInstallation() }
        if #available(macOS 13.0, *) {
            let service = SMAppService.mainApp
            if enabled {
                switch service.status {
                case .enabled, .requiresApproval:
                    break
                case .notRegistered, .notFound:
                    try service.register()
                @unknown default:
                    try service.register()
                }
                // Migrate a previous macOS installation only after registration succeeds.
                // Pending approval still retains the legacy item until the modern service works.
                if service.status == .enabled { try removeLegacyItem() }
            } else {
                if service.status == .enabled || service.status == .requiresApproval {
                    try service.unregister()
                }
                try removeLegacyItem()
            }
        } else if enabled {
            guard let executable = bundle.executableURL?.path else {
                throw LoginItemError.missingExecutable
            }
            let propertyList: [String: Any] = [
                "Label": identifier,
                "ProgramArguments": [executable, "--login"],
                "RunAtLoad": true,
                "KeepAlive": false,
                "LimitLoadToSessionType": "Aqua",
                "ProcessType": "Interactive"
            ]
            let data = try PropertyListSerialization.data(fromPropertyList: propertyList,
                                                          format: .xml, options: 0)
            try fileManager.createDirectory(at: legacyURL.deletingLastPathComponent(),
                                            withIntermediateDirectories: true, attributes: nil)
            try data.write(to: legacyURL, options: .atomic)
            try fileManager.setAttributes([.posixPermissions: 0o644], ofItemAtPath: legacyURL.path)
        } else {
            try removeLegacyItem()
        }
    }

    func openLoginSettings() {
        if #available(macOS 13.0, *) {
            SMAppService.openSystemSettingsLoginItems()
        }
    }

    private func removeLegacyItem() throws {
        if fileManager.fileExists(atPath: legacyURL.path) {
            try fileManager.removeItem(at: legacyURL)
        }
    }

    private func validateInstallation() throws {
        let appURL = bundle.bundleURL.resolvingSymlinksInPath().standardizedFileURL
        let applicationDirectories = [
            URL(fileURLWithPath: "/Applications", isDirectory: true),
            fileManager.homeDirectoryForCurrentUser.appendingPathComponent("Applications", isDirectory: true)
        ].map { $0.resolvingSymlinksInPath().standardizedFileURL.path + "/" }
        guard appURL.pathExtension == "app",
              applicationDirectories.contains(where: { appURL.path.hasPrefix($0) }) else {
            throw LoginItemError.installFirst
        }
        guard let executable = bundle.executableURL,
              fileManager.isExecutableFile(atPath: executable.path) else {
            throw LoginItemError.missingExecutable
        }
    }
}

private enum LoginItemError: LocalizedError {
    case installFirst
    case missingExecutable

    var errorDescription: String? {
        switch self {
        case .installFirst:
            return L10n.text(
                "Move EndfieldHUD.app into Applications (or your home Applications folder), open it from there, then enable Launch at login.",
                "请将 EndfieldHUD.app 移到“应用程序”文件夹（或个人目录中的“应用程序”文件夹），从那里打开后再启用“登录时启动”。")
        case .missingExecutable:
            return L10n.text("The app’s executable could not be found. Build or reinstall EndfieldHUD.app, then try again.",
                             "找不到应用程序的可执行文件。请重新构建或安装 EndfieldHUD.app 后再试。")
        }
    }
}
