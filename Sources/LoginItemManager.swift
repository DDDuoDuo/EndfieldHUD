import AppKit
import ServiceManagement

struct LoginItemStatusSnapshot: Equatable {
    let isEnabled: Bool
    let requiresApproval: Bool
}

/// Service Management can block on IPC. A serial owner confines its objects and
/// legacy file operations; UI consumers read only a main-thread value snapshot.
/// Passive refreshes never register anything or wait for that owner queue.
final class LoginItemManager {
    private let queue: DispatchQueue
    private let readStatus: () -> LoginItemStatusSnapshot
    private let changeEnabled: (Bool) throws -> Void
    private var generation: UInt64 = 0
    private var readInFlight = false
    private var refreshPending = false
    private(set) var snapshot: LoginItemStatusSnapshot?
    var onStatusChange: (() -> Void)?

    init(bundle: Bundle = .main,
         queue: DispatchQueue = DispatchQueue(label: "EndfieldHUD.login-status", qos: .utility),
         readStatus: (() -> LoginItemStatusSnapshot)? = nil,
         changeEnabled: ((Bool) throws -> Void)? = nil) {
        let backend = LoginItemBackend(bundle: bundle)
        self.queue = queue
        self.readStatus = readStatus ?? { backend.readStatus() }
        self.changeEnabled = changeEnabled ?? { try backend.setEnabled($0) }
    }

    var isEnabled: Bool { snapshot?.isEnabled ?? false }
    var requiresApproval: Bool { snapshot?.requiresApproval ?? false }
    var statusDescription: String {
        precondition(Thread.isMainThread)
        guard let snapshot else { return "" }
        if snapshot.requiresApproval {
            return L10n.text("Approval needed in System Settings → Login Items.",
                             "请在系统设置 → 登录项中允许启动。")
        }
        return snapshot.isEnabled ? L10n.text("Enabled", "已启用") : L10n.text("Disabled", "已停用")
    }

    /// Preserve launch-at-login initialization without holding up first paint.
    /// Only the saved enabled preference can initiate registration here.
    func start(ensureEnabled: Bool, completion: @escaping (Error?) -> Void) {
        precondition(Thread.isMainThread && !readInFlight)
        enqueueRead(ensureEnabled: ensureEnabled, completion: completion)
    }

    func refreshStatus() {
        precondition(Thread.isMainThread)
        guard !readInFlight else { refreshPending = true; return }
        enqueueRead(ensureEnabled: false, completion: nil)
    }

    private func enqueueRead(ensureEnabled: Bool, completion: ((Error?) -> Void)?) {
        readInFlight = true
        let token = generation, reader = readStatus, change = changeEnabled
        queue.async { [weak self] in
            let result: (LoginItemStatusSnapshot, Error?) = autoreleasepool {
                let initial = reader()
                guard ensureEnabled && !initial.isEnabled else { return (initial, nil) }
                do { try change(true); return (reader(), nil) }
                catch { return (reader(), error) }
            }
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.readInFlight = false
                if self.generation == token {
                    let changed = self.snapshot != result.0
                    self.snapshot = result.0
                    completion?(result.1)
                    if changed { self.onStatusChange?() }
                }
                if self.refreshPending {
                    self.refreshPending = false
                    self.refreshStatus()
                }
            }
        }
    }

    /// Explicit edits keep the existing immediate error/rollback contract.
    /// Serialize with an in-flight read and invalidate its queued main callback.
    func setEnabled(_ enabled: Bool) throws {
        precondition(Thread.isMainThread)
        generation &+= 1
        let result: (LoginItemStatusSnapshot, Error?) = queue.sync {
            autoreleasepool {
                do { try changeEnabled(enabled); return (readStatus(), nil) }
                catch { return (readStatus(), error) }
            }
        }
        snapshot = result.0
        // The settings transaction publishes this new value after committing or
        // rolling back its preference; do not reenter it midway through mutation.
        if let error = result.1 { throw error }
    }

    func openLoginSettings() {
        if #available(macOS 13.0, *) { SMAppService.openSystemSettingsLoginItems() }
    }
}

/// All methods run exclusively on LoginItemManager's utility queue. No AppKit,
/// localization, observer, or mutable application state crosses that boundary.
private final class LoginItemBackend {
    private let fileManager = FileManager()
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

    func readStatus() -> LoginItemStatusSnapshot {
        if #available(macOS 13.0, *) {
            let status = SMAppService.mainApp.status
            return LoginItemStatusSnapshot(isEnabled: status == .enabled || hasLegacyItem,
                                           requiresApproval: status == .requiresApproval)
        }
        return LoginItemStatusSnapshot(isEnabled: hasLegacyItem, requiresApproval: false)
    }

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
