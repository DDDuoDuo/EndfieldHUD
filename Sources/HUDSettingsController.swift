import AppKit

struct HUDCredit {
    let name: String
    let role: String
    let url: URL?
}

struct HUDAboutInformation {
    let name: String
    let version: String
    let author: String
    let licenseName: String
    let repositoryURL: URL?
    let credits: [HUDCredit]

    static func current(bundle: Bundle = .main) -> HUDAboutInformation {
        let version = bundle.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "Development"
        let build = bundle.object(forInfoDictionaryKey: "CFBundleVersion") as? String
        let rawRepository = bundle.object(forInfoDictionaryKey: "HUDRepositoryURL") as? String
        let repository = rawRepository.flatMap(URL.init(string:)).flatMap { url -> URL? in
            guard url.scheme == "https", url.host == "github.com", !url.path.isEmpty, url.path != "/" else { return nil }
            return url
        }
        return HUDAboutInformation(
            name: "EndfieldHUD", version: build.map { "\(version) (\($0))" } ?? version,
            author: bundle.object(forInfoDictionaryKey: "HUDAuthor") as? String ?? "DDDuoDuo",
            licenseName: "MIT", repositoryURL: repository,
            credits: [
                HUDCredit(name: "Arknights: Endfield", role: L10n.text("Unofficial fan project", "非官方同人项目"),
                          url: nil),
                HUDCredit(name: "llynxxx", role: L10n.text("Inspiration", "灵感来源"),
                          url: URL(string: "https://www.bilibili.com/video/BV1DBaP6yEHs/")),
                HUDCredit(name: "QinAnze / 氰氨锗", role: L10n.text("Inspiration", "灵感来源"),
                          url: URL(string: "https://github.com/QinAnze/zmd-charge")),
                HUDCredit(name: "Ronit Singh · FineTune", role: L10n.text("Audio architecture reference", "音频架构参考"),
                          url: URL(string: "https://github.com/ronitsingh10/FineTune"))
            ])
    }
}

protocol HUDSettingsTimer: AnyObject { func invalidate() }
extension Timer: HUDSettingsTimer {}

/// Shared settings state for the four HUD modules. Rendering stays in the HUD;
/// platform operations are injected by the application coordinator.
/// Layout preview never touches persistent preferences until explicitly accepted.
final class HUDSettingsController {
    typealias ScheduleTimer = (_ interval: TimeInterval, _ action: @escaping () -> Void) -> HUDSettingsTimer
    static let scaleConfirmationDuration: TimeInterval = 12
    static let layoutConfirmationDuration = scaleConfirmationDuration
    static let presetAccentHexes = ["FAD41F", "6EDFE8", "A8E58B", "C9A2FF", "FF8F78"]

    private let store: ConfigurationStore
    private let clock: () -> TimeInterval
    private let scheduleTimer: ScheduleTimer
    private var storeObservation: UUID?
    private var observers: [UUID: () -> Void] = [:]
    private struct Layout: Equatable {
        let scale: Double
        let x: Double
        let y: Double
        init(_ configuration: AppConfiguration) {
            scale = configuration.hudScale; x = configuration.hudOffsetX; y = configuration.hudOffsetY
        }
        func apply(to configuration: inout AppConfiguration) {
            configuration.hudScale = scale; configuration.hudOffsetX = x; configuration.hudOffsetY = y
        }
    }
    private var layoutPreview: Layout?
    private var layoutDeadline: TimeInterval?
    private var layoutTimer: HUDSettingsTimer?
    private var lastLayoutSecond: Int?
    private var isWritingStore = false
    private var lastPublishedConfiguration: AppConfiguration

    var onConfigurationChange: ((AppConfiguration) -> Void)?
    var onChange: (() -> Void)?
    /// Return an error string on failure. The previous preference is retained.
    var onLaunchAtLoginChange: ((Bool) -> String?)?
    var onShortcutChange: ((SummonShortcut) -> String?)?
    var onShortcutCaptureChange: ((Bool) -> Void)?
    var onEditBatteryPosition: (() -> Void)?
    var onOpenLink: ((URL) -> Void)?
    var onCheckForUpdates: (() -> Void)?
    var onAutomaticUpdatesChange: ((Bool) -> Void)?
    private(set) var updateState = HUDUpdateState()
    var loginStatusProvider: (() -> String)?
    /// Return nil while available, or the platform's current registration error.
    var shortcutRegistrationStatusProvider: (() -> String?)?
    private(set) var status: String?
    private var shortcutChangeError: String?
    private var shortcutRegistrationStatus: String?
    var shortcutStatus: String? { shortcutChangeError ?? shortcutRegistrationStatus }
    private(set) var loginStatus: String?
    private(set) var isCapturingShortcut = false

    var configuration: AppConfiguration {
        var result = store.configuration
        layoutPreview?.apply(to: &result)
        return result
    }

    var layoutConfirmationRemaining: Int? {
        guard let layoutDeadline else { return nil }
        return max(0, Int(ceil(layoutDeadline - clock())))
    }
    var scaleConfirmationRemaining: Int? { layoutConfirmationRemaining }
    var isScalePreviewPending: Bool { layoutPreview.map { $0.scale != store.configuration.hudScale } ?? false }
    var isPositionPreviewPending: Bool {
        layoutPreview.map { $0.x != store.configuration.hudOffsetX || $0.y != store.configuration.hudOffsetY } ?? false
    }

    private var cachedAbout: (language: AppLanguage, information: HUDAboutInformation)?
    var about: HUDAboutInformation {
        let language = L10n.resolvedLanguage
        if let cachedAbout, cachedAbout.language == language { return cachedAbout.information }
        let information = HUDAboutInformation.current()
        cachedAbout = (language, information)
        return information
    }
    var presetAccentHexes: [String] { Self.presetAccentHexes }

    init(store: ConfigurationStore, clock: (() -> TimeInterval)? = nil, scheduleTimer: ScheduleTimer? = nil) {
        self.store = store
        self.clock = clock ?? { ProcessInfo.processInfo.systemUptime }
        self.scheduleTimer = scheduleTimer ?? Self.scheduleOnMainRunLoop
        lastPublishedConfiguration = store.configuration
        storeObservation = store.addObserver { [weak self] _ in
            guard let self, !self.isWritingStore else { return }
            self.publish()
        }
    }

    deinit {
        layoutTimer?.invalidate()
        if let storeObservation { store.removeObserver(storeObservation) }
    }

    @discardableResult
    func addObserver(_ observer: @escaping () -> Void) -> UUID {
        let token = UUID()
        observers[token] = observer
        return token
    }

    func removeObserver(_ token: UUID) { observers.removeValue(forKey: token) }

    /// General edits may accompany a layout preview. Save those edits while
    /// keeping confirmed scale and position on disk, even if the app exits abruptly.
    func update(_ mutation: (inout AppConfiguration) -> Void) {
        var candidate = configuration
        mutation(&candidate)
        let layoutChanged = Layout(candidate) != Layout(configuration)
        let requestedLayout = Layout(candidate.normalized)
        Layout(store.configuration).apply(to: &candidate)
        applyPersistent(candidate)
        if layoutChanged { previewLayout(requestedLayout) }
    }

    func previewScale(_ scale: Double) {
        var candidate = configuration
        candidate.hudScale = scale
        previewLayout(Layout(candidate.normalized))
    }

    func previewPosition(x: Double, y: Double) {
        var candidate = configuration
        candidate.hudOffsetX = x; candidate.hudOffsetY = y
        previewLayout(Layout(candidate.normalized))
    }

    private func previewLayout(_ requested: Layout) {
        guard requested != Layout(store.configuration) else { revertLayout(); return }
        layoutTimer?.invalidate()
        layoutPreview = requested
        layoutDeadline = clock() + Self.layoutConfirmationDuration
        lastLayoutSecond = Int(Self.layoutConfirmationDuration)
        status = nil
        layoutTimer = scheduleTimer(0.25) { [weak self] in self?.checkLayoutTimeout() }
        publish()
    }

    func confirmScale() { confirmLayout() }

    func confirmLayout() {
        // A delayed click cannot commit a preview whose safety deadline expired.
        guard let layout = layoutPreview, let deadline = layoutDeadline, clock() < deadline else {
            checkLayoutTimeout()
            return
        }
        clearLayoutPreview()
        var candidate = store.configuration
        layout.apply(to: &candidate)
        status = nil
        isWritingStore = true
        store.update(candidate)
        isWritingStore = false
        publish()
    }

    func revertScale() { revertLayout() }

    func revertLayout() {
        guard layoutPreview != nil else { return }
        clearLayoutPreview()
        publish()
    }

    func checkScaleTimeout() { checkLayoutTimeout() }

    func checkLayoutTimeout() {
        guard let deadline = layoutDeadline else { return }
        if clock() >= deadline {
            let positionChanged = isPositionPreviewPending
            clearLayoutPreview()
            status = positionChanged ? L10n.text("UI layout restored", "已恢复界面布局")
                : L10n.text("UI scale restored", "已恢复界面缩放")
            publish()
        } else if layoutConfirmationRemaining != lastLayoutSecond {
            lastLayoutSecond = layoutConfirmationRemaining
            publish()
        }
    }

    /// Restore only app preferences. Notes, shelf items, clipboard and shortcuts
    /// are separate data stores and must not be deleted by this action.
    func restoreDefaults() {
        clearLayoutPreview()
        endShortcutCapture()
        applyPersistent(.defaults, forceShortcutRegistration: true)
    }

    @discardableResult
    func setShortcut(_ shortcut: SummonShortcut) -> String? {
        if let error = shortcut.validationError ?? onShortcutChange?(shortcut) {
            shortcutChangeError = error
            status = error
            publish()
            return error
        }
        var candidate = configuration
        candidate.summonShortcut = shortcut
        Layout(store.configuration).apply(to: &candidate)
        // Explicit capture/retry must validate even an unchanged stored chord:
        // a prior startup registration may have failed, or capture suspended it.
        // Its successful registration above must not run a second time here.
        applyPersistent(candidate, shortcutAlreadyApplied: true)
        return shortcutStatus
    }

    func beginShortcutCapture() {
        guard !isCapturingShortcut else { return }
        isCapturingShortcut = true
        shortcutChangeError = nil
        onShortcutCaptureChange?(true)
        publish()
    }

    func endShortcutCapture() {
        guard isCapturingShortcut else { return }
        isCapturingShortcut = false
        onShortcutCaptureChange?(false)
        publish()
    }

    func editBatteryPosition() { onEditBatteryPosition?() }
    func openLink(_ url: URL) { onOpenLink?(url) }
    func checkForUpdates() { onCheckForUpdates?() }
    func toggleAutomaticUpdates() { onAutomaticUpdatesChange?(!updateState.automaticallyInstalls) }
    func receiveUpdateState(_ state: HUDUpdateState) {
        guard updateState != state else { return }
        updateState = state; publish()
    }

    func refreshExternalStatus() {
        let login = loginStatusProvider?()
        let shortcut = shortcutRegistrationStatusProvider?()
        guard login != loginStatus || shortcut != shortcutRegistrationStatus else { return }
        loginStatus = login
        shortcutRegistrationStatus = shortcut
        publish()
    }

    func setStatus(_ message: String?) {
        status = message
        publish()
    }

    /// Called when the overlay closes, including focus loss and shortcut close.
    func close() {
        revertLayout()
        endShortcutCapture()
    }

    private func applyPersistent(_ proposed: AppConfiguration, shortcutAlreadyApplied: Bool = false,
                                 forceShortcutRegistration: Bool = false) {
        let previous = store.configuration
        var candidate = proposed
        status = nil
        if !shortcutAlreadyApplied && (forceShortcutRegistration || candidate.summonShortcut != previous.summonShortcut) {
            let error = candidate.summonShortcut.validationError ?? onShortcutChange?(candidate.summonShortcut)
            shortcutChangeError = error
            if let error {
                candidate.summonShortcut = previous.summonShortcut
                status = error
            }
        } else { shortcutChangeError = nil }
        if candidate.launchAtLogin != previous.launchAtLogin,
           let error = onLaunchAtLoginChange?(candidate.launchAtLogin) {
            candidate.launchAtLogin = previous.launchAtLogin
            status = error
        }
        candidate = candidate.normalized
        isWritingStore = true
        store.update(candidate)
        isWritingStore = false
        loginStatus = loginStatusProvider?()
        shortcutRegistrationStatus = shortcutRegistrationStatusProvider?()
        publish()
    }

    private func clearLayoutPreview() {
        layoutTimer?.invalidate()
        layoutTimer = nil
        layoutPreview = nil
        layoutDeadline = nil
        lastLayoutSecond = nil
    }

    private func publish() {
        let next = configuration
        if next != lastPublishedConfiguration {
            lastPublishedConfiguration = next
            onConfigurationChange?(next)
        }
        onChange?()
        Array(observers.values).forEach { $0() }
    }

    private static func scheduleOnMainRunLoop(interval: TimeInterval, action: @escaping () -> Void) -> HUDSettingsTimer {
        let timer = Timer(timeInterval: interval, repeats: true) { _ in action() }
        timer.tolerance = 0.04
        RunLoop.main.add(timer, forMode: .common)
        return timer
    }
}
