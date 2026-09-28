import AppKit
import UserNotifications
#if canImport(Sparkle)
import Sparkle
#endif

/// GitHub supplies release metadata; Sparkle alone verifies, downloads, replaces
/// and relaunches the app. No GitHub API response can authorize executable code.
final class HUDUpdateController: NSObject, UNUserNotificationCenterDelegate {
    private(set) var state = HUDUpdateState()
    var onChange: ((HUDUpdateState) -> Void)?
    var onOpenAbout: (() -> Void)?
    var isSafeToRestart: () -> Bool = { false }
    var prepareForRestart: ((@escaping () -> Void) -> Void)?
    var presentUpdateUI: ((@escaping () -> Void) -> Void)?
    var onRestartCancelled: (() -> Void)?
    private let client = HUDGitHubReleaseClient()
    private let defaults: UserDefaults
    private let bundle: Bundle
    private var cancelCheck: (() -> Void)?
    private var checkGeneration = 0
    private var metadataChecking = false
    private var metadataIsCurrent = false
    private var stopped = false
    private var initialCheck: DispatchWorkItem?
    private var developmentTimer: Timer?
    private var installHandler: (() -> Void)?
    private var signedVersion: String?
    private var signedError: String?
    private var pendingNotification = false
    private static let notificationID = "EndfieldHUD.update.available"
    private lazy var installGate = HUDUpdateInstallGate { delay, action in
        let work = DispatchWorkItem(block: action)
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
        return { work.cancel() }
    }
    #if canImport(Sparkle)
    private var sparkle: SPUStandardUpdaterController?
    #endif

    init(defaults: UserDefaults = .standard, bundle: Bundle = .main) {
        self.defaults = defaults; self.bundle = bundle
        super.init()
        state.automaticallyInstalls = defaults.object(forKey: "HUDUpdateAutomaticallyInstall") as? Bool ?? true
    }

    func start() {
        precondition(Thread.isMainThread)
        stopped = false
        UNUserNotificationCenter.current().delegate = self
        // A developer executable must never overwrite itself or a test fixture.
        #if HUD_RELEASE && canImport(Sparkle)
        if Self.canInstall(bundleURL: bundle.bundleURL) {
            let controller = SPUStandardUpdaterController(startingUpdater: false, updaterDelegate: self, userDriverDelegate: self)
            sparkle = controller
            do {
                try controller.updater.start()
                controller.updater.automaticallyDownloadsUpdates = state.automaticallyInstalls
                state.installationSupported = true
            } catch {
                signedError = L10n.text("The signed updater could not start.", "签名更新服务无法启动。")
                sparkle = nil
            }
        }
        #endif
        // Sparkle owns the release scheduler. Development/read-only copies only
        // discover releases and cannot download or replace an application.
        if !state.installationSupported {
            let timer = Timer(timeInterval: 6 * 60 * 60, repeats: true) { [weak self] _ in self?.checkMetadata() }
            timer.tolerance = 5 * 60
            RunLoop.main.add(timer, forMode: .common)
            developmentTimer = timer
        }
        let first = DispatchWorkItem { [weak self] in self?.checkMetadata() }
        initialCheck = first
        DispatchQueue.main.asyncAfter(deadline: .now() + 5, execute: first)
        publish()
    }

    func stop() {
        stopped = true; checkGeneration += 1
        metadataChecking = false; pendingNotification = false
        initialCheck?.cancel(); initialCheck = nil
        developmentTimer?.invalidate(); developmentTimer = nil
        cancelCheck?(); cancelCheck = nil
        installGate.reset(); installHandler = nil
    }

    func checkForUpdates() {
        guard !stopped else { return }
        if state.hasUpdate, signedVersion != nil {
            showInstaller(); return
        }
        signedError = nil
        checkMetadata()
        #if canImport(Sparkle)
        if let updater = sparkle?.updater, updater.canCheckForUpdates {
            let show = { updater.checkForUpdates() }
            if let presentUpdateUI { presentUpdateUI(show) } else { show() }
        }
        #endif
    }

    func setAutomaticallyInstalls(_ enabled: Bool) {
        state.automaticallyInstalls = enabled
        defaults.set(enabled, forKey: "HUDUpdateAutomaticallyInstall")
        #if canImport(Sparkle)
        sparkle?.updater.automaticallyDownloadsUpdates = enabled
        #endif
        if installHandler != nil {
            state.detail = enabled
                ? L10n.text("Installs after 30 idle seconds, or when you quit.", "空闲 30 秒后或退出时自动安装。")
                : L10n.text("Already downloaded; installs on next quit. Automatic restarts are off.", "更新已下载，将在下次退出时安装。已关闭自动重启。")
        }
        publish(); environmentDidChange()
    }

    /// Called by existing HUD, timer and audio lifecycle events; no idle polling.
    func environmentDidChange() {
        installGate.evaluate(eligible: canAutomaticallyRestart, recheck: { [weak self] in
            self?.canAutomaticallyRestart ?? false
        }, install: { [weak self] in
            guard let self, let handler = self.installHandler else { return }
            self.installHandler = nil
            self.state.phase = .installing; self.publish()
            handler()
        })
    }
    private var canAutomaticallyRestart: Bool {
        #if canImport(Sparkle)
        if let updater = sparkle?.updater, !updater.automaticallyDownloadsUpdates { return false }
        #endif
        return !stopped && state.installationSupported && state.automaticallyInstalls
            && installHandler != nil && isSafeToRestart()
    }

    private func checkMetadata() {
        guard !stopped, !metadataChecking else { return }
        metadataChecking = true; checkGeneration += 1
        let token = checkGeneration
        if !state.hasUpdate { state.phase = .checking }
        publish()
        cancelCheck = client.check(currentTag: HUDGitHubRelease.currentTag(in: bundle), enabled: true) { [weak self] result in
            guard let self, !self.stopped, token == self.checkGeneration else { return }
            self.metadataChecking = false; self.cancelCheck = nil
            if self.signedVersion == nil {
                self.state.detail = nil
            }
            switch result {
            case .disabled: break
            case .current(let release):
                self.metadataIsCurrent = true
                self.state.latestVersion = release.tag
                self.state.releaseURL = release.url
                if self.signedVersion == nil { self.state.phase = self.signedError == nil ? .current : .failed }
            case .available(let release):
                self.metadataIsCurrent = false
                self.state.latestVersion = release.tag; self.state.releaseURL = release.url
                if self.signedVersion == nil { self.state.phase = .available; self.state.updateVersion = release.tag }
                self.notify(version: release.tag)
            case .failed(let error):
                self.metadataIsCurrent = false
                if self.signedVersion == nil { self.state.phase = .failed }
                self.state.detail = Self.releaseErrorDescription(error)
            }
            if let signedError = self.signedError { self.state.detail = signedError }
            self.publish()
        }
    }

    private func showInstaller() {
        #if canImport(Sparkle)
        guard let updater = sparkle?.updater, updater.canCheckForUpdates else { return }
        // Use the existing HUD close transition before showing Sparkle's signed
        // install/relaunch UI. No silent focus change in the middle of the HUD.
        let show = { updater.checkForUpdates() }
        if let presentUpdateUI { presentUpdateUI(show) } else { show() }
        #endif
    }

    private func notify(version: String) {
        let version = version.hasPrefix("v") ? String(version.dropFirst()) : version
        guard defaults.string(forKey: "HUDUpdateLastNotifiedRelease") != version, !pendingNotification else { return }
        pendingNotification = true
        let center = UNUserNotificationCenter.current()
        center.delegate = self
        center.getNotificationSettings { [weak self] settings in
            func deliver(_ allowed: Bool) {
                DispatchQueue.main.async {
                    guard let self, !self.stopped else { return }
                    self.pendingNotification = false
                    self.defaults.set(version, forKey: "HUDUpdateLastNotifiedRelease")
                    guard allowed else { return } // Persistent menu/About indicator still works.
                    let content = UNMutableNotificationContent()
                    content.title = L10n.text("EndfieldHUD update available", "EndfieldHUD 发现新版本")
                    content.body = self.state.installationSupported && self.state.automaticallyInstalls
                        ? L10n.text("\(version) is available. Signed updates install automatically when the HUD, Work Mode and audio controls are idle.",
                                    "\(version) 已发布。签名更新将在浮层、工作模式和音量控制空闲时自动安装。")
                        : L10n.text("\(version) is available. Open About to see the release.", "\(version) 已发布。打开关于页面查看。")
                    center.add(UNNotificationRequest(identifier: Self.notificationID, content: content, trigger: nil))
                }
            }
            switch settings.authorizationStatus {
            case .authorized, .provisional: deliver(true)
            case .notDetermined: center.requestAuthorization(options: [.alert]) { granted, _ in deliver(granted) }
            default: deliver(false)
            }
        }
    }

    func userNotificationCenter(_ center: UNUserNotificationCenter, didReceive response: UNNotificationResponse,
                                withCompletionHandler completionHandler: @escaping () -> Void) {
        if response.notification.request.identifier == Self.notificationID {
            DispatchQueue.main.async { [weak self] in self?.onOpenAbout?() }
        }
        completionHandler()
    }
    func userNotificationCenter(_ center: UNUserNotificationCenter, willPresent notification: UNNotification,
                                withCompletionHandler completionHandler: @escaping (UNNotificationPresentationOptions) -> Void) {
        if #available(macOS 11, *) { completionHandler([.banner, .list]) }
        else { completionHandler([.alert]) }
    }

    private func publish() { onChange?(state) }
    private static func releaseErrorDescription(_ error: HUDGitHubReleaseError) -> String {
        switch error {
        case .rateLimited: return L10n.text("GitHub is limiting requests. Try again later.", "GitHub 暂时限制请求，请稍后重试。")
        case .noPublishedReleases: return L10n.text("No published release found on GitHub.", "GitHub 上暂无已发布版本。")
        case .noMatchingReleases: return L10n.text("No release is available for this update channel.", "此更新通道暂无已发布版本。")
        case .invalidCurrentVersion: return L10n.text("This build has no valid release version.", "此构建没有有效的版本号。")
        default: return L10n.text("Could not check GitHub. Check your connection and retry.", "无法检查 GitHub。请检查网络后重试。")
        }
    }
    static func canInstall(bundleURL: URL) -> Bool {
        let path = bundleURL.resolvingSymlinksInPath().path
        let personalApplications = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications").path + "/"
        guard path.hasPrefix("/Applications/") || path.hasPrefix(personalApplications),
              bundleURL.pathExtension == "app" else { return false }
        return (try? bundleURL.resourceValues(forKeys: [.volumeIsReadOnlyKey]).volumeIsReadOnly) == false
    }
    deinit { initialCheck?.cancel(); developmentTimer?.invalidate(); cancelCheck?() }
}

#if canImport(Sparkle)
extension HUDUpdateController: SPUUpdaterDelegate, SPUStandardUserDriverDelegate {
    var supportsGentleScheduledUpdateReminders: Bool { true }
    func allowedChannels(for updater: SPUUpdater) -> Set<String> {
        guard let tag = HUDGitHubRelease.currentTag(in: bundle), let version = HUDReleaseVersion(tag) else { return [] }
        return version.prerelease.isEmpty ? [] : ["preview"]
    }
    func updater(_ updater: SPUUpdater, mayPerform updateCheck: SPUUpdateCheck) throws { checkMetadata() }
    func updater(_ updater: SPUUpdater, didFindValidUpdate item: SUAppcastItem) {
        signedError = nil; signedVersion = item.displayVersionString
        state.phase = .available
        state.updateVersion = item.displayVersionString; state.detail = nil
        publish(); notify(version: item.displayVersionString)
    }
    func updater(_ updater: SPUUpdater, willDownloadUpdate item: SUAppcastItem, with request: NSMutableURLRequest) {
        state.phase = .downloading; publish()
    }
    func updater(_ updater: SPUUpdater, willInstallUpdateOnQuit item: SUAppcastItem,
                 immediateInstallationBlock immediateInstallHandler: @escaping () -> Void) -> Bool {
        installGate.reset(); installHandler = immediateInstallHandler
        state.phase = .ready
        state.detail = L10n.text("Installs after 30 idle seconds, or when you quit.", "空闲 30 秒后或退出时自动安装。")
        publish()
        DispatchQueue.main.async { [weak self] in self?.environmentDidChange() }
        return true
    }
    func updater(_ updater: SPUUpdater, shouldPostponeRelaunchForUpdate item: SUAppcastItem,
                 untilInvokingBlock installHandler: @escaping () -> Void) -> Bool {
        guard let prepareForRestart else { return false }
        state.phase = .installing; publish()
        DispatchQueue.main.async { prepareForRestart(installHandler) }
        return true
    }
    func updater(_ updater: SPUUpdater, didAbortWithError error: Error) {
        let nsError = error as NSError
        if nsError.domain == SUSparkleErrorDomain && nsError.code == SUError.noUpdateError.rawValue { return }
        onRestartCancelled?()
        installHandler = nil; installGate.reset(); signedVersion = nil
        signedError = L10n.text("Signed update unavailable. Retry or view the GitHub release.", "签名更新暂不可用。请重试或查看 GitHub 版本。")
        state.phase = .failed; state.detail = signedError; publish()
    }
    func updaterDidNotFindUpdate(_ updater: SPUUpdater, error: Error) {
        let previousError = signedError
        signedError = nil
        if let previousError, state.detail == previousError { state.detail = nil }
        if metadataIsCurrent && !metadataChecking { state.phase = .current; publish() }
        if state.phase == .checking && !metadataChecking { state.phase = .current; publish() }
        if state.phase == .available {
            state.detail = L10n.text("Waiting for a compatible signed update package.", "正在等待兼容的签名更新包。")
            publish()
        }
    }
    func standardUserDriverShouldHandleShowingScheduledUpdate(_ update: SUAppcastItem, andInImmediateFocus immediateFocus: Bool) -> Bool {
        false // Do not steal focus; our menu/About indicator and notification advertise it.
    }
    func standardUserDriverWillHandleShowingUpdate(_ handleShowingUpdate: Bool, forUpdate update: SUAppcastItem, state: SPUUserUpdateState) {
        notify(version: update.displayVersionString)
    }
    func updater(_ updater: SPUUpdater, didFinishUpdateCycleFor updateCheck: SPUUpdateCheck, error: Error?) {
        let enabled = updater.automaticallyDownloadsUpdates
        if state.automaticallyInstalls != enabled {
            state.automaticallyInstalls = enabled
            defaults.set(enabled, forKey: "HUDUpdateAutomaticallyInstall")
            publish(); environmentDidChange()
        }
    }
}
#endif
