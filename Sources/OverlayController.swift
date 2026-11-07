import AppKit
import Darwin

/// One shared panel. The charging HUD rests idle; the summonable Power HUD
/// owns ambient Core Animation tracks only while fully open.
final class OverlayController: NSObject {
    private let panel: PositionPanel
    private let root: PositionCanvas
    private let indicator: ChargeIndicatorView
    private var dismissal: DispatchWorkItem?
    private var generation = 0
    private var configuration = AppConfiguration.defaults
    // The bar editor's drafts: nothing is saved until Confirm.
    private var draftAnchor: NSPoint?
    private var draftScreenID: UInt32?
    private var draftScale: Double = 1
    private var draftLength: Double?
    /// Whether the bar was dragged; Size or Length alone keeps the placement.
    private var draftMoved = false
    /// The Length slider's value as last shown; a press that leaves it there
    /// keeps a classic (nil) bar classic.
    private var shownLength: Double?
    /// The strip's screen frame, held still while one of its sliders has the pointer.
    private var editorStripFrame: NSRect?
    private static let editorSizeID = "chargeBar.size", editorLengthID = "chargeBar.length"
    private var presentationCompleted = false
    private var requestedDuration: Double = 5
    private var documentTerminationInProgress = false
    private var archiveDocuments: ArchiveController?
    private var readerDocuments: ReaderController?
    private var mediaAssemblyDocuments: MediaAssemblyController?
    private var calendarDocuments: HUDCalendarController?
    private lazy var accountController: HypergryphAccountController = {
        let controller = HypergryphAccountController(profile: try? profileStore.get(), fileURL: HypergryphAccountController.applicationFile(),
            vault: Self.isDiagnosticRun ? HypergryphMemoryCredentialVault() : HypergryphAccountKeychain())
        controller.onEvent = { [weak self] action in self?.eventLog.record(kind: .accountAction, metadata: ["action": action]) }
        return controller
    }()
    private lazy var minigameSession: OrbiPomSession = {
        let value = OrbiPomSession(defaults: CommandLine.arguments.contains("--ui-test") ? nil : .standard)
        value.onEvent = { [weak self] action in self?.eventLog.record(kind:.minigameAction, metadata:["action":action]) }
        return value
    }()
    private var mediaAssemblyController: MediaAssemblyController {
        if let mediaAssemblyDocuments { return mediaAssemblyDocuments }
        let value = MediaAssemblyController()
        value.onEvent = { [weak self] action in self?.eventLog.record(kind: .mediaAssemblyAction, metadata: ["action": action]) }
        mediaAssemblyDocuments = value; return value
    }
    private var calendarController: HUDCalendarController {
        if let calendarDocuments { return calendarDocuments }
        let value = HUDCalendarController()
        value.onEvent = { [weak self] action in self?.eventLog.record(kind: .calendarAction, metadata: ["action": action]) }
        calendarDocuments = value; return value
    }
    func startCalendarReminders() { calendarController.startIfExisting() }
    private var archiveController: ArchiveController {
        if let archiveDocuments { return archiveDocuments }
        let value = ArchiveController(store: ArchiveStore(directory: ArchiveStore.applicationDirectory()))
        archiveDocuments = value; return value
    }
    private var readerController: ReaderController {
        if let readerDocuments { return readerDocuments }
        let value = ReaderController(loadStore: { try ReaderStore(directory: ReaderStore.applicationDirectory()) })
        readerDocuments = value; return value
    }
    /// The view is released after each close; document writers outlive its
    /// animation and remain available for failed-write recovery and quit drain.
    func drainDocumentWrites(completion: @escaping (Bool) -> Void) {
        mediaAssemblyDocuments?.cancelExport()
        systemView?.prepareDocumentWriteDrain()
        var pending = 3, succeeded = true
        func finished(_ value: Bool) { succeeded = succeeded && value; pending -= 1; if pending == 0 { completion(succeeded) } }
        if let archiveDocuments { archiveDocuments.drainPendingWrites(timeout: 3, completion: finished) } else { finished(true) }
        if let readerDocuments { readerDocuments.drainPendingWrites(timeout: 3, completion: finished) } else { finished(true) }
        if let calendarDocuments { calendarDocuments.drainPendingWrites(timeout: 3, completion: finished) } else { finished(true) }
    }
    private var systemView: SystemHUDView?
    private var latestBatterySnapshot: BatterySnapshot = .unavailable
    private var projection: ProjectionController?
    private var projectionHandoff = false
    private var projectionGeneration = 0
    private var projectionScreenID: CGDirectDisplayID?
    private var returningProjectionScreenID: CGDirectDisplayID?
    private var projectionPreviousApplication: NSRunningApplication?
    private var projectionExternalAction: (() -> Void)?
    var isProjectionActive: Bool { projectionHandoff || projection?.isPresented == true }
    var projectionForVerification: ProjectionController? { projection }
    private final class ClosedHeapCleanupTicket {
        private let lock = NSLock()
        private var cancelled = false
        func cancel() { lock.lock(); cancelled = true; lock.unlock() }
        func begin() -> Bool { lock.lock(); defer { lock.unlock() }; return !cancelled }
    }
    private var closedHeapCleanup: DispatchWorkItem?
    private var closedHeapCleanupTicket: ClosedHeapCleanupTicket?
    private var closedHeapCleanupGeneration: UInt64 = 0
    private var closedHeapCleanupInFlight = false
    private(set) var closedHeapCleanupRunsForVerification = 0
    private(set) var closedHeapCleanupLastRunGenerationForVerification: UInt64?
    private(set) var lastClosedHeapCleanupMillisecondsForVerification = 0.0
    var closedHeapCleanupPendingForVerification: Bool { closedHeapCleanup != nil }
    var closedHeapCleanupActiveForVerification: Bool { closedHeapCleanup != nil || closedHeapCleanupInFlight }
    func rescheduleClosedHeapCleanupForVerification() -> UInt64 {
        precondition(CommandLine.arguments.contains("--ui-test") && Thread.isMainThread
            && systemPhase == .closed && systemView == nil)
        cancelClosedHeapCleanup()
        scheduleClosedHeapCleanup()
        return closedHeapCleanupGeneration
    }
    private lazy var mapStore: Result<WorldMapStore, Error> = Result { try WorldMapStore(directory: WorldMapStore.applicationDirectory()) }
    private lazy var notesStore: Result<NotesStore, Error> = Result { try NotesStore(directory: NotesStore.applicationDirectory()) }
    private lazy var shelfStore: Result<FileShelfStore, Error> = Result { try FileShelfStore(directory: FileShelfStore.applicationDirectory()) }
    private var pendingShelfDropPresentation: Set<UUID>?
    private lazy var appShortcutStore: Result<AppShortcutStore, Error> = Result { try AppShortcutStore(directory: AppShortcutStore.applicationDirectory()) }
    private var pendingAppLaunch: (name: String, url: URL)?
    private var pendingShelfReveal: ShelfFileAccess?
    var revealShelfFile: (URL) -> Void = { NSWorkspace.shared.activateFileViewerSelecting([$0]) }
    private var appLaunchInFlight = false
    private var appLaunchGeneration = 0
    private var appLaunchError: String?
    var launchAppShortcut: (URL, @escaping (Result<NSRunningApplication, Error>) -> Void) -> Void = AppShortcutLauncher.launch
    // History belongs to the app session, so closing or changing HUD sections
    // never restarts monitoring or discards captured items.
    let clipboard: ClipboardWatcher
    let audio: AudioDeviceController
    let perAppAudio: PerAppAudioController
    let nowPlaying: NowPlayingController
    let storage: StorageController
    let activity: SystemActivityMonitor
    let appActivity: AppActivityMonitor
    private var chargeMetricObserver: UUID?
    private var openStorageAfterClose = false
    var openSystemStorage: () -> Bool = { SystemStorageSettings.open() }
    var settingsController: HUDSettingsController?
    var afterSystemClose: (() -> Void)?
    private var quitRequested = false
    private var quitAfterSystemClose = false
    private var applicationUpdateCompletion: (() -> Void)?
    var onSystemActivityChange: (() -> Void)?
    var isIdleForUpdate: Bool {
        systemPhase == .closed && !isEditingPosition && !appLaunchInFlight
            && !shelfDragPresentation.isActive && pendingShelfDropPresentation == nil
            && applicationUpdateCompletion == nil && !quitRequested && !isProjectionActive
    }
    var onQuitAccepted: (() -> Void)?
    var onQuitAfterSystemClose: (() -> Void)?
    var initialModuleRequest: HUDModule?
    let workMode = WorkModeController()
    /// Facility alerts outlive each HUD view, so a dismissed alert stays dismissed.
    let facilityAlerts = FacilityAlertMonitor()
    /// UI tests, smoke tests and renders: no live system alerts, real
    /// keychain or day-stamped preferences.
    static let isDiagnosticRun = CommandLine.arguments.contains { $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-") }
    private let profileStore: Result<UserProfileStore, Error>
    var workFocusStatusMessage: String? {
        didSet { systemView?.workFocusStatusMessage = workFocusStatusMessage }
    }
    var workFocusNeedsAccessibilityPermission = false {
        didSet { systemView?.workFocusNeedsAccessibilityPermission = workFocusNeedsAccessibilityPermission }
    }
    var isPresentingFocusSystemControls: (() -> Bool)?
    var onRequestFocusAccess: (() -> Void)?

    let eventLog: SystemEventLog
    let eventRecorder: SystemEventRecorder
    private var workEventObserver: UUID?
    private var closeAfterShelfDrag = false
    private var forceCloseAfterShelfDrag = false
    private var shelfDragPresentation = ShelfDragPresentationState()
    private var shelfDragDeadline: DispatchWorkItem?
    /// "Last opened" (Display > HUD elements > Open on): kept across relaunches,
    /// except in diagnostic runs, which never touch the user's preferences.
    private var lastSystemModule: HUDModule = HUDOverlayLayout.lastModule(in: OverlayController.lastModuleDefaults) {
        didSet { HUDOverlayLayout.saveLastModule(lastSystemModule, to: Self.lastModuleDefaults) }
    }
    private static var lastModuleDefaults: UserDefaults? { isDiagnosticRun ? nil : .standard }
    private var systemState = SystemOverlayState()
    private var transitionDeadline: DispatchWorkItem?
    private var previousApplication: NSRunningApplication?
    private var systemScreenID: CGDirectDisplayID?
    var systemPhase: SystemOverlayPhase { systemState.phase }
    var isSystemOverlayActive: Bool { systemState.isActive }
    var systemAnimationCount: Int { systemView?.activeAnimationCount ?? 0 }
    var systemAmbientAnimationCount: Int { systemView?.ambientAnimationCount ?? 0 }
    var systemParallaxAnimationCount: Int { systemView?.parallaxAnimationCount ?? 0 }
    var systemChargeHitRectForVerification: CGRect { systemView?.chargeHitRectForVerification ?? .zero }
    var systemChargeFollowsDialRetractionForVerification: Bool { systemView?.chargeFollowsDialRetractionForVerification ?? false }
    func setSystemChargeHoveredForVerification(_ hovered: Bool) { systemView?.setChargeHoveredForVerification(hovered) }
    var systemChargeStageForVerification: OverlayStage? { systemView?.chargeStageForVerification }
    var systemDeploymentAnimationCount: Int { systemView?.deploymentAnimationCount ?? 0 }
    var systemWorkModeDialDiameterForVerification: CGFloat { systemView?.workModeDialDiameterForVerification ?? 0 }
    var systemWorkModeAnimationCount: Int { systemView?.workModeAnimationCount ?? 0 }
    var systemTelemetryAnimationCount: Int { systemView?.telemetryAnimationCount ?? 0 }
    func addAppShortcutForVerification(_ url: URL) throws -> AppShortcut {
        let store = try appShortcutStore.get()
        return try store.save(candidate: store.inspect(url: url), name: "Launch test", iconPreset: .original)
    }
    func performDesignActionForVerification(_ module: HUDModule, _ id: String) {
        systemView?.performDesignActionForVerification(module, id)
    }
    func designControlIDsForVerification(_ module: HUDModule) -> [String] { systemView?.designControlIDsForVerification(module) ?? [] }
    func activateAppNavigationForVerification(_ id: UUID) { systemView?.activateAppNavigationForVerification(id) }
    var appNavigationTargetsForVerification: [HUDNavigationTarget] { systemView?.appNavigationTargetsForVerification ?? [] }
    var systemAppLaunchErrorForVerification: String? { appLaunchError }
    var systemCenterContentCount: Int { systemView?.centerContentCount ?? 0 }
    var isSwitchingSystemModule: Bool { systemView?.isSwitchingModule ?? false }
    var systemShellIdentity: ObjectIdentifier? { systemView?.shellIdentity }
    var systemCenterHostIdentity: ObjectIdentifier? { systemView?.centerHostIdentity }
    var systemAmbientStartTime: TimeInterval? { systemView?.ambientStartTime }
    var systemSelectedModule: HUDModule? { systemView?.selectedModule }
    var systemSourceWatchForVerification: HUDSourceWatchView? { systemView?.sourceWatchForVerification }
    var systemBackdropPreparationForVerification: ((@escaping () -> Void) -> Void)? {
        didSet {
            if let _ = systemBackdropPreparationForVerification {
                precondition(CommandLine.arguments.contains("--ui-test"))
            }
            systemView?.sourceWatchForVerification?.backdropPreparationForVerification = systemBackdropPreparationForVerification
        }
    }
    var systemSourceFailureForVerification: String? { systemView?.sourceFailureForVerification }
    private(set) var lastClosedSourceTimerActive = false
    private(set) var lastClosedSourcePhase: HUDSourceWatchPlayback.Phase?
    var systemReportGeometryMatchesSelectionForVerification: Bool { systemView?.reportGeometryMatchesSelectionForVerification ?? false }
    var systemPresentationGeneration: Int { systemState.generation }
    var systemFiniteAnimationKeys: [String] {
        func keys(in item: CALayer) -> [String] {
            let own = (item.animationKeys() ?? []).filter { !$0.hasPrefix("ambient.") && !$0.hasPrefix("parallax.") && !$0.hasPrefix("workMode.") && !$0.hasPrefix("telemetry.") }
                .map { "\(item.name ?? "layer").\($0)" }
            return own + (item.sublayers ?? []).flatMap { keys(in: $0) } + (item.mask.map { keys(in: $0) } ?? [])
        }
        return systemView?.layer.map { keys(in: $0) } ?? []
    }
    private(set) var lastClosedAnimationCount = 0
    /// A diagnostic screen-coordinate source; production always reads AppKit.
    var systemPointerLocationProviderForVerification: (() -> CGPoint)? {
        didSet { systemView?.pointerLocationProvider = systemPointerLocationProviderForVerification ?? { NSEvent.mouseLocation } }
    }
    var systemPointerTargetForVerification: CGPoint? { systemView?.pointerTargetForVerification }
    var systemCurrentPointerTargetForVerification: CGPoint? { systemView?.currentPointerTargetForVerification }
    func systemSpatialPoseMatchesPointerForVerification(_ point: CGPoint) -> Bool {
        systemView?.spatialPoseMatchesPointerForVerification(point) ?? false
    }
    func setSystemPointerForVerification(_ point: CGPoint) { systemView?.setPointerForVerification(point) }
    func selectSystemModule(_ module: HUDModule, animated: Bool = true) {
        systemView?.selectModule(module, animated: animated)
    }

    /// Menu-bar drops share the shelf's bookmark store even when no HUD exists.
    /// A drop never summons the HUD; only an already open/opening HUD reveals the shelf.
    @discardableResult
    func receiveStatusItemFiles(_ urls: [URL]) -> Bool {
        guard !urls.isEmpty, urls.allSatisfy(\.isFileURL), canPresentShelfDrop else { return false }
        if systemView == nil, case .failure = shelfStore {
            shelfStore = Result { try FileShelfStore(directory: FileShelfStore.applicationDirectory()) }
        }
        let previousIDs = Set((try? shelfStore.get().items.map(\.id)) ?? [])
        if let systemView {
            guard systemView.importShelfFiles(urls) else { return false }
        } else {
            do {
                let store = try shelfStore.get()
                try store.add(urls: urls)
                for item in store.items where !previousIDs.contains(item.id) {
                    eventLog.record(kind: .shelfAdded, metadata: ["filename": item.name])
                }
            } catch { return false }
        }
        let addedIDs = Set((try? shelfStore.get().items.map(\.id)) ?? []).subtracting(previousIDs)
            .union(pendingShelfDropPresentation ?? [])
        if systemPhase == .open || systemPhase == .opening {
            pendingShelfDropPresentation = addedIDs
            DispatchQueue.main.async { [weak self] in self?.presentPendingShelfDrop() }
        }
        return true
    }

    private func presentPendingShelfDrop() {
        guard let addedIDs = pendingShelfDropPresentation else { return }
        guard canPresentShelfDrop else { pendingShelfDropPresentation = nil; return }
        switch systemPhase {
        case .closed, .closing:
            // A user dismissal wins over a deferred drop callback.
            pendingShelfDropPresentation = nil
        case .open:
            pendingShelfDropPresentation = nil
            systemView?.revealShelfItems(addedIDs)
            selectSystemModule(.fileShelf)
        case .opening:
            break // The existing entrance completion consumes the request once.
        }
    }

    private var canPresentShelfDrop: Bool {
        !quitRequested && !isEditingPosition && !appLaunchInFlight
            && pendingAppLaunch == nil && pendingShelfReveal == nil
            && !openStorageAfterClose && afterSystemClose == nil
            && !shelfDragPresentation.isActive && systemView?.isDraggingShelfItem != true
    }
    var visibleNotesForVerification: Set<UUID> { systemView?.visibleNotesForVerification ?? [] }
    var notesForVerification: [CanvasNote] { (try? notesStore.get().notes) ?? [] }
    func projectNotesPointForVerification(_ point: CGPoint) -> CGPoint {
        systemView?.projectNotesPointForVerification(point) ?? .zero
    }
    func notesWorkspacePointForVerification(_ point: CGPoint) -> CGPoint? {
        systemView?.notesWorkspacePointForVerification(point)
    }
    var notesSpatialPoseMatchesPanelsForVerification: Bool {
        systemView?.notesSpatialPoseMatchesPanelsForVerification ?? false
    }
    var notesFollowRetractionForVerification: Bool { systemView?.notesFollowRetractionForVerification ?? false }
    var notesDeploymentRestoredForVerification: Bool { systemView?.notesDeploymentRestoredForVerification ?? false }
    var shelfCountForVerification: Int { (try? shelfStore.get().items.count) ?? 0 }
    var shelfScrollOffsetForVerification: CGFloat? { systemView?.shelfScrollOffsetForVerification }
    var shelfSelectedCountForVerification: Int { systemView?.shelfSelectedCountForVerification ?? 0 }
    var shelfDragPhaseForVerification: ShelfDragPresentationState.Phase { shelfDragPresentation.phase }
    var systemWindowVisibleForVerification: Bool { panel.isVisible }
    func performNoteActionForVerification(_ action: String) { systemView?.performNoteActionForVerification(action) }
    func dropFilesOnShelfNavigationForVerification(_ pasteboard: NSPasteboard) -> Bool {
        systemView?.dropFilesOnShelfNavigationForVerification(pasteboard) ?? false
    }
    func beginShelfDragForVerification() { performShelfDragAction(shelfDragPresentation.begin()) }
    func finishShelfDragForVerification(delivered: Bool) { finishShelfDrag(delivered: delivered) }
    func revealShelfSelectionForVerification() { systemView?.revealShelfSelectionForVerification() }
    var systemPresentedRingScale: CGFloat? { systemView?.presentedRingScale }
    var onSystemClosed: (() -> Void)?
    private(set) var isVisible = false
    private(set) var isPersistent = false
    private(set) var isEditingPosition = false
    var onPositionEditFinished: ((ChargeBarLayout?) -> Void)?

    override init() {
        let diagnostic = CommandLine.arguments.contains { argument in
            ["--ui-test", "--smoke-test", "--system-smoke-test", "--navigation-smoke-test",
             "--render-system-preview", "--render-preview"].contains(argument)
        }
        profileStore = Result { try UserProfileStore(directory: UserProfileStore.applicationDirectory()) }
        clipboard = ClipboardWatcher(store: ClipboardStore(),
                                     pasteboard: diagnostic ? .withUniqueName() : .general)
        audio = diagnostic ? .fixture() : AudioDeviceController()
        perAppAudio = diagnostic ? .fixture() : PerAppAudioController()
        nowPlaying = diagnostic ? .fixture() : NowPlayingController()
        storage = diagnostic ? .fixture() : StorageController()
        let liveTelemetry = CommandLine.arguments.contains("--ui-test")
            && CommandLine.arguments.contains("--live-telemetry-benchmark")
        activity = diagnostic && !liveTelemetry ? .fixture() : SystemActivityMonitor()
        appActivity = diagnostic && !liveTelemetry ? .fixture() : AppActivityMonitor()
        let log = SystemEventLog(directory: diagnostic ? nil : SystemEventLog.applicationDirectory())
        eventLog = log
        eventRecorder = SystemEventRecorder(log: log)
        let size = ChargeIndicatorView.canvasSize
        panel = PositionPanel(contentRect: NSRect(origin: .zero, size: size),
                              styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        root = PositionCanvas(frame: NSRect(origin: .zero, size: size))
        indicator = ChargeIndicatorView(frame: NSRect(origin: .zero, size: size))
        super.init()
        // The centre's fonts register off the main thread while the HUD is
        // still closed, so its first opening does not wait for them.
        HUDFontRegistry.prewarm()
        if let profile = try? profileStore.get() {
            workMode.restoreTrackedWorkSeconds(profile.profile.accumulatedWorkSeconds)
            workMode.onTrackedWorkSecondsChanged = { [weak profile] seconds in
                do { try profile?.setWorkSeconds(seconds) }
                catch { NSLog("Could not save Work Mode hours: %@", error.localizedDescription) }
            }
        }
        eventRecorder.receiveWork(workMode.snapshot)
        workEventObserver = workMode.observe { [weak self] in
            guard let self else { return }
            self.eventRecorder.receiveWork(self.workMode.snapshot)
        }
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false
        panel.level = .statusBar
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .ignoresCycle]
        panel.ignoresMouseEvents = true
        panel.hidesOnDeactivate = false
        panel.isReleasedWhenClosed = false
        panel.animationBehavior = .none
        panel.isMovable = false
        panel.isMovableByWindowBackground = false
        panel.onResignKey = { [weak self] in self?.closeSystemOverlayForFocusLoss() }
        root.addSubview(indicator)
        panel.contentView = root
        root.onMouseDown = { [weak self] event in self?.editorPointerDown(event) }
        root.onMouseDragged = { [weak self] event in self?.editorPointerDragged(event) }
        root.onMouseUp = { [weak self] event in self?.editorPointerUp(event) }
        root.onKey = { [weak self] keyCode in self?.root.editor?.key(keyCode) ?? false }
        // System Theme: the strip's pills bake their colours, so a new
        // palette rebuilds the strip (the drafts live here).
        root.onAppearanceChange = { [weak self] in
            guard let self, self.isEditingPosition, HUDRuntimeAppearance.applyPalette() else { return }
            self.installPositionEditor(); self.reposition()
        }
    }

    deinit {
        projection?.forceClose()
        if let chargeMetricObserver { activity.removeObserver(chargeMetricObserver) }
        activity.setAlertActive(false)
        cancelClosedHeapCleanup()
    }

    func update(snapshot: BatterySnapshot, configuration: AppConfiguration, preview: Bool = false) {
        latestBatterySnapshot = snapshot
        let displayChanged = self.configuration.hudDisplayUUID != configuration.hudDisplayUUID
            || self.configuration.openOnActiveDisplay != configuration.openOnActiveDisplay
        let geometryChanged = self.configuration.scale != configuration.scale
            || self.configuration.placement != configuration.placement
            || self.configuration.customPosition != configuration.customPosition
            || self.configuration.chargeBarLength != configuration.chargeBarLength
        self.configuration = configuration.normalized
        facilityAlerts.isEnabled = self.configuration.facilityAlerts && !Self.isDiagnosticRun
        projection?.update(configuration: self.configuration)
        indicator.set(snapshot: snapshot, configuration: barConfiguration, preview: preview)
        systemView?.set(snapshot: snapshot, configuration: self.configuration)
        reconcileChargeMetricTelemetry()
        if displayChanged, isSystemOverlayActive,
           let screen = HUDDisplayPolicy.targetScreen(configuration: self.configuration) {
            systemScreenID = HUDDisplayPolicy.displayID(for: screen)
            reposition()
            systemView?.animateDisplayArrival()
        } else if geometryChanged || isEditingPosition { reposition() }
    }

    /// Explicit previews replay the entrance even if an always-visible HUD is already on screen.
    func show(persistent: Bool, duration: Double, replay: Bool = false) {
        guard !isEditingPosition, !isSystemOverlayActive, !isProjectionActive else { return }
        dismissal?.cancel()
        dismissal = nil
        generation += 1
        let token = generation
        isPersistent = persistent
        requestedDuration = duration.isFinite ? min(60, max(1, duration)) : 5
        reposition()
        if !isVisible || replay || !presentationCompleted {
            isVisible = true
            presentationCompleted = false
            panel.orderFrontRegardless()
            indicator.animateEntrance { [weak self] in
                guard let self = self, self.generation == token, !self.isEditingPosition else { return }
                self.presentationCompleted = true
                self.scheduleDismissal(token: token)
            }
        } else {
            scheduleDismissal(token: token)
        }
        reconcileChargeMetricTelemetry()
    }

    private var chargeMetricNeedsTelemetry: Bool {
        configuration.alertMetric.requiresTelemetry && panel.isVisible && panel.alphaValue > 0
            && (isVisible || systemView != nil)
    }

    /// Attach only while this shared panel displays the selected metric. The
    /// sampler's separate Activity Monitor demand remains independently owned.
    private func reconcileChargeMetricTelemetry() {
        let needed = chargeMetricNeedsTelemetry
        activity.setAlertActive(needed)
        if needed {
            if chargeMetricObserver == nil {
                chargeMetricObserver = activity.observe { [weak self] snapshot in
                    guard let self, self.chargeMetricNeedsTelemetry else { return }
                    self.presentChargeMetric(snapshot)
                }
            } else { presentChargeMetric(activity.snapshot) }
        } else {
            if let observer = chargeMetricObserver { activity.removeObserver(observer); chargeMetricObserver = nil }
            indicator.setMetric(configuration.alertMetric, telemetry: nil)
            systemView?.setChargeMetric(configuration.alertMetric, telemetry: nil)
        }
    }

    private func presentChargeMetric(_ snapshot: SystemActivitySnapshot) {
        if isVisible { indicator.setMetric(configuration.alertMetric, telemetry: snapshot) }
        systemView?.setChargeMetric(configuration.alertMetric, telemetry: snapshot)
    }

    private func scheduleDismissal(token: Int) {
        guard !isPersistent, !isEditingPosition else { return }
        let work = DispatchWorkItem { [weak self] in
            guard let self = self, self.generation == token else { return }
            self.hide()
        }
        dismissal = work
        DispatchQueue.main.asyncAfter(deadline: .now() + requestedDuration, execute: work)
    }

    func hide(animated: Bool = true) {
        guard !isEditingPosition, !isSystemOverlayActive else { return }
        dismissal?.cancel()
        dismissal = nil
        generation += 1
        let token = generation
        isPersistent = false
        presentationCompleted = false
        guard isVisible else { return }
        if !animated {
            indicator.cancelAnimations()
            indicator.setStage(.hidden, animated: false)
            panel.orderOut(nil)
            isVisible = false
            reconcileChargeMetricTelemetry()
            return
        }
        indicator.animateExit { [weak self] in
            guard let self = self, self.generation == token else { return }
            self.panel.orderOut(nil)
            self.isVisible = false
            self.reconcileChargeMetricTelemetry()
        }
    }

    func reposition() {
        if isProjectionActive, !isSystemOverlayActive {
            if let screen = NSScreen.screens.first(where: { HUDDisplayPolicy.displayID(for: $0) == projectionScreenID })
                ?? HUDDisplayPolicy.targetScreen(configuration: configuration) {
                projectionScreenID = HUDDisplayPolicy.displayID(for: screen)
                projection?.reposition(on: screen)
            } else { forceCloseSystemOverlay() }
            return
        }
        if isSystemOverlayActive {
            let screens = NSScreen.screens
            let current = screens.first { HUDDisplayPolicy.displayID(for: $0) == systemScreenID }
            let screen = configuration.hudDisplayUUID != nil
                ? HUDDisplayPolicy.targetScreen(configuration: configuration, screens: screens)
                : current ?? HUDDisplayPolicy.targetScreen(configuration: configuration, screens: screens)
            if let screen = screen {
                systemScreenID = HUDDisplayPolicy.displayID(for: screen)
                panel.setFrame(screen.frame, display: false)
                systemView?.frame = NSRect(origin: .zero, size: screen.frame.size)
            } else { forceCloseSystemOverlay() }
            return
        }
        guard let screen = selectedScreen() else { return }
        let anchor: NSPoint
        if isEditingPosition, let draft = draftAnchor {
            anchor = OverlayGeometry.editorAnchor(draft: draft, moved: draftMoved, configuration: barConfiguration,
                                                  screen: screen.visibleFrame)
            draftAnchor = anchor
            draftScreenID = screen.id
            syncLengthSlider(screen: screen.visibleFrame)
        } else {
            anchor = OverlayGeometry.anchor(configuration: configuration, screen: screen.visibleFrame, editing: false)
        }
        layout(anchor: anchor, screen: screen.visibleFrame)
    }

    /// The configuration the bar draws with: the editor's Size and Length
    /// drafts while editing, the saved ones otherwise.
    private var barConfiguration: AppConfiguration {
        guard isEditingPosition else { return configuration }
        var value = configuration
        value.scale = draftScale
        value.chargeBarLength = draftLength
        return value.normalized
    }

    private func barWidth(on screen: NSRect) -> CGFloat {
        let bar = barConfiguration
        return OverlayGeometry.barWidth(length: bar.chargeBarLength, screen: screen, scale: bar.scale)
    }

    /// The strip flips above the bar near the bottom edge, so the editor no
    /// longer reserves room below it (`editing: false`).
    private func clampedDraftAnchor(_ point: NSPoint, on screen: OverlayScreen) -> NSPoint {
        OverlayGeometry.clampedAnchor(point, configuration: barConfiguration, screen: screen.visibleFrame, editing: false)
    }

    func beginPositionEditing(snapshot: BatterySnapshot, configuration: AppConfiguration) {
        guard !isProjectionActive else { returnFromProjection(); return }
        guard !isSystemOverlayActive else { closeSystemOverlay(); return }
        guard !isEditingPosition else { panel.makeKeyAndOrderFront(nil); return }
        update(snapshot: snapshot, configuration: configuration, preview: true)
        dismissal?.cancel()
        dismissal = nil
        generation += 1
        indicator.cancelAnimations()
        guard let screen = selectedScreen() else { return }
        draftScreenID = screen.id
        draftAnchor = OverlayGeometry.anchor(configuration: self.configuration, screen: screen.visibleFrame, editing: false)
        draftScale = self.configuration.scale
        draftLength = self.configuration.chargeBarLength
        draftMoved = false
        isEditingPosition = true
        isVisible = true
        isPersistent = true
        presentationCompleted = true
        panel.allowsKey = true
        panel.ignoresMouseEvents = false
        root.editing = true
        installPositionEditor()
        indicator.setStage(.compact, animated: false)
        reposition()
        root.editor?.show()
        NSApp.activate(ignoringOtherApps: true)
        panel.makeKeyAndOrderFront(nil)
        panel.makeFirstResponder(root)
        reconcileChargeMetricTelemetry()
        if CommandLine.arguments.contains("--ui-test"), let anchor = draftAnchor {
            let desktopTop = NSScreen.screens.first?.frame.maxY ?? 0
            print("Position editor center: \(anchor.x), \(desktopTop - anchor.y) from desktop top-left")
            fflush(stdout)
        }
    }

    func cancelPositionEditing() {
        if isEditingPosition { finishPositionEditing(layout: nil) }
    }

    func confirmEditedPosition() { confirmPosition() }

    /// One serialized handoff. Neither surface is retained visibly behind the
    /// other, and the existing HUD entrance/exit remains the source of timing.
    func openProjection() {
        guard systemPhase == .open, !isProjectionActive, !quitRequested,
              afterSystemClose == nil, !shelfDragPresentation.isActive else { return }
        projectionGeneration &+= 1
        let token = projectionGeneration
        projectionHandoff = true
        projectionScreenID = systemScreenID
        projectionPreviousApplication = previousApplication
        afterSystemClose = { [weak self] in
            guard let self, self.projectionGeneration == token, self.projectionHandoff,
                  !self.quitRequested, self.systemPhase == .closed else { return }
            guard let screen = NSScreen.screens.first(where: { HUDDisplayPolicy.displayID(for: $0) == self.projectionScreenID })
                    ?? HUDDisplayPolicy.targetScreen(configuration: self.configuration) else {
                self.cancelProjection(); return
            }
            self.cancelClosedHeapCleanup()
            if self.finishProjectionExternalAction() { return }
            if self.projection == nil {
                self.projection = ProjectionController(configuration: self.configuration, shelfChoices: { [weak self] in
                    let extensions = Set(NotesMediaFactory.supportedFileExtensions)
                    return ((try? self?.shelfStore.get().items) ?? []).map { item in
                        NotesShelfMediaChoice(id: item.id, title: item.name, detail: item.typeDescription,
                            isSupported: !item.isDirectory && extensions.contains(URL(fileURLWithPath: item.lastKnownPath).pathExtension.lowercased()),
                            isAvailable: item.availabilityError == nil)
                    }
                }, shelfAccess: { [weak self] id in
                    guard let self else { throw CocoaError(.userCancelled) }
                    return try self.shelfStore.get().access(id: id)
                })
                self.projection?.onClose = { [weak self] in self?.returnFromProjection() }
                self.projection?.onEvent = { [weak self] event in self?.recordProjectionEvent(event) }
            }
            // The centre page's pen colour, width and background are the workspace's.
            if let model = self.projection?.model { ProjectionSettings.shared.attach(model) }
            self.projectionScreenID = HUDDisplayPolicy.displayID(for: screen)
            self.projection?.update(configuration: self.configuration)
            self.projection?.present(on: screen)
            if ProjectionSettings.shared.takeMediaInsertRequest() { self.projection?.workspace?.perform("media") }
            self.projectionHandoff = false
            self.onSystemActivityChange?()
        }
        closeSystemOverlay()
    }

    private func returnFromProjection() {
        guard !projectionHandoff, let projection, projection.isPresented, !quitRequested else { return }
        projectionHandoff = true
        let token = projectionGeneration
        projection.dismiss { [weak self] in
            guard let self, self.projectionGeneration == token, self.projectionHandoff, !self.quitRequested else { return }
            if self.finishProjectionExternalAction() { return }
            self.returningProjectionScreenID = self.projectionScreenID
            self.projectionHandoff = false
            self.performSystemAction(self.systemState.toggle(), snapshot: self.latestBatterySnapshot)
            if let previous = self.projectionPreviousApplication { self.previousApplication = previous }
            self.projectionPreviousApplication = nil
            self.projectionScreenID = nil
        }
    }

    private func cancelProjection() {
        if projectionHandoff { afterSystemClose = nil }
        projectionGeneration &+= 1
        projectionHandoff = false
        projection?.forceClose()
        projectionExternalAction = nil
        projectionScreenID = nil; returningProjectionScreenID = nil; projectionPreviousApplication = nil
    }

    /// Updater UI must not appear behind a screen-wide projection. A request
    /// during either handoff is consumed by that handoff's existing completion.
    func closeForExternalPresentation(_ action: @escaping () -> Void) {
        if isProjectionActive {
            projectionExternalAction = action
            if !projectionHandoff { returnFromProjection() }
        } else if systemPhase == .closed { action() }
        else { afterSystemClose = action; closeSystemOverlay() }
    }

    @discardableResult private func finishProjectionExternalAction() -> Bool {
        guard let action = projectionExternalAction else { return false }
        projectionExternalAction = nil; projectionHandoff = false
        projectionScreenID = nil; returningProjectionScreenID = nil; projectionPreviousApplication = nil
        action(); onSystemActivityChange?()
        return true
    }

    private func recordProjectionEvent(_ event: ProjectionEvent) {
        let action: String
        switch event {
        case .strokeCompleted: action = "drawingEdited"
        case .erased: action = "erased"
        case .brushChanged: action = "brushChanged"; ProjectionSettings.shared.capture()
        case .backgroundChanged: action = "backgroundChanged"; ProjectionSettings.shared.capture()
        case .cleared: action = "cleared"
        case .mediaAdded: action = "mediaAdded"
        case .mediaRemoved: action = "mediaRemoved"
        }
        eventLog.record(kind: .projectionAction, metadata: ["action": action])
    }

    /// Both presentations share this controller's one NSPanel and battery stream.
    @discardableResult func toggleSystemOverlay(snapshot: BatterySnapshot, configuration: AppConfiguration) -> Bool {
        guard !quitRequested, !isEditingPosition, !appLaunchInFlight else { return false }
        if isProjectionActive {
            update(snapshot: snapshot, configuration: configuration)
            returnFromProjection()
            return true
        }
        if systemView?.isDraggingShelfItem == true || shelfDragPresentation.isActive { closeAfterShelfDrag = true; return true }
        if !isSystemOverlayActive {
            hide(animated: false)
            update(snapshot: snapshot, configuration: configuration)
        }
        performSystemAction(systemState.toggle(), snapshot: snapshot)
        return true
    }

    private func requestQuit() {
        guard !quitRequested, systemPhase == .open else { return }
        quitRequested = true
        quitAfterSystemClose = true
        pendingAppLaunch = nil
        pendingShelfReveal?.close(); pendingShelfReveal = nil
        openStorageAfterClose = false
        afterSystemClose = nil
        onQuitAccepted?()
        closeSystemOverlay()
    }

    var systemQuitConfirmationVisibleForVerification: Bool { systemView?.quitConfirmationVisibleForVerification ?? false }
    func presentQuitConfirmationForVerification() { systemView?.presentQuitConfirmation() }
    func answerQuitConfirmationForVerification(_ confirm: Bool) { systemView?.answerQuitConfirmationForVerification(confirm) }

    func closeSystemOverlay() {
        // AppKit owns the native drag loop. Its source view and window must
        // survive until endedAt, including a dismissal requested mid-drag.
        if systemView?.isDraggingShelfItem == true || shelfDragPresentation.isActive { closeAfterShelfDrag = true; return }
        // No entrance is running while the source holds its initial pose for
        // background input. Cancel that wait immediately rather than queueing
        // dismissal behind an input that may never arrive.
        let heldOpening = systemView?.isPreparingSourceBackdrop == true
        performSystemAction(systemState.requestClose(interruptOpening: heldOpening))
    }

    /// Accepted updater relaunch only. Keep the current panel alive through its
    /// closing animation, discard unrelated handoffs, then return to Sparkle.
    func closeForApplicationUpdate(completion: @escaping () -> Void) {
        guard applicationUpdateCompletion == nil else { return }
        cancelClosedHeapCleanup()
        quitRequested = true
        pendingAppLaunch = nil
        pendingShelfReveal?.close(); pendingShelfReveal = nil
        openStorageAfterClose = false; afterSystemClose = nil
        cancelProjection()
        cancelPositionEditing()
        if systemPhase == .closed {
            hide(animated: false)
            DispatchQueue.main.async(execute: completion)
        } else {
            applicationUpdateCompletion = completion
            closeSystemOverlay()
        }
    }

    func prepareForDocumentTermination() {
        documentTerminationInProgress = true
        systemView?.interactionEnabled = false
    }
    func cancelTerminationAfterDocumentFailure() {
        documentTerminationInProgress = false
        applicationUpdateCompletion = nil; quitRequested = false; quitAfterSystemClose = false
        if systemPhase == .open { systemView?.interactionEnabled = true }
    }

    func cancelApplicationUpdate() {
        applicationUpdateCompletion = nil
        if !quitAfterSystemClose { quitRequested = false }
    }

    func closeSystemOverlayForFocusLoss(activatedApplication: NSRunningApplication? = nil) {
        guard !isProjectionActive else { return }
        guard configuration.closeOnFocusLost else { return }
        // The public Focus adapter briefly opens Control Center. Other app
        // activations keep their normal dismissal policy even during that task.
        if isPresentingFocusSystemControls?() == true {
            let front = activatedApplication ?? NSWorkspace.shared.frontmostApplication
            if front?.bundleIdentifier == "com.apple.controlcenter"
                || front?.processIdentifier == ProcessInfo.processInfo.processIdentifier { return }
        }
        guard systemView?.isPresentingModulePanel != true else { return }
        // A file can be picked up in Finder, then the HUD summoned while it is
        // held. Drag tracking may change key focus before delivering the drop.
        if systemView?.isAwaitingFileDrop == true || systemView?.isDraggingShelfItem == true || shelfDragPresentation.isActive { return }
        closeSystemOverlay()
    }

    /// Sleep/session shutdown and termination cannot wait for visible animations.
    func forceCloseSystemOverlay() {
        cancelProjection()
        pendingShelfDropPresentation = nil
        cancelClosedHeapCleanup()
        pendingAppLaunch = nil
        pendingShelfReveal?.close(); pendingShelfReveal = nil
        appLaunchGeneration &+= 1
        appLaunchInFlight = false
        guard isSystemOverlayActive else { return }
        if systemView?.isDraggingShelfItem == true {
            forceCloseAfterShelfDrag = true
            shelfDragDeadline?.cancel(); shelfDragDeadline = nil
            systemView?.interactionEnabled = false
            systemView?.cancelAnimations()
            panel.alphaValue = 0
            panel.ignoresMouseEvents = true
            reconcileChargeMetricTelemetry()
            return
        }
        systemState.forceClose()
        logSystemPhase()
        transitionDeadline?.cancel()
        transitionDeadline = nil
        systemView?.interactionEnabled = false
        systemView?.cancelAnimations()
        tearDownSystemPresentation(restoreFocus: false, notify: false)
    }


    private func performSystemAction(_ action: SystemOverlayState.Action, snapshot: BatterySnapshot? = nil) {
        switch action {
        case .none: return
        case .open(let token):
            var startup = HUDStartupTrace.begin()
            cancelClosedHeapCleanup()
            logSystemPhase()
            if case .failure = mapStore {
                mapStore = Result { try WorldMapStore(directory: WorldMapStore.applicationDirectory()) }
            }
            if case .failure = notesStore {
                notesStore = Result { try NotesStore(directory: NotesStore.applicationDirectory()) }
            }
            if case .failure = shelfStore {
                shelfStore = Result { try FileShelfStore(directory: FileShelfStore.applicationDirectory()) }
            }
            if case .failure = appShortcutStore {
                appShortcutStore = Result { try AppShortcutStore(directory: AppShortcutStore.applicationDirectory()) }
            }
            let requestedScreen = returningProjectionScreenID.flatMap { id in
                NSScreen.screens.first { HUDDisplayPolicy.displayID(for: $0) == id }
            } ?? HUDDisplayPolicy.targetScreen(configuration: configuration)
            returningProjectionScreenID = nil
            guard let screen = requestedScreen else {
                systemState.forceClose(); return
            }
            previousApplication = NSWorkspace.shared.frontmostApplication
            systemScreenID = (screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value
            HUDStartupTrace.end("open.preparation", since: &startup)
            let opening = HUDOverlayLayout.initialModule(request: initialModuleRequest, configuration: configuration, last: lastSystemModule)
            let view = SystemHUDView(frame: NSRect(origin: .zero, size: screen.frame.size),
                                     notesStore: notesStore, shelfStore: shelfStore, clipboard: clipboard,
                                     audio: audio, perAppAudio: perAppAudio, workMode: workMode, eventLog: eventLog, nowPlaying: nowPlaying,
                                     storage: storage, activity: activity, appActivity: appActivity, appShortcuts: appShortcutStore,
                                     settings: settingsController, profile: profileStore, mapStore: mapStore,
                                     archive: archiveController, reader: readerController,
                                     mediaAssembly: mediaAssemblyController, calendar: calendarController, minigame: minigameSession, account: accountController,
                                     facility: facilityAlerts, initialConfiguration: configuration, initialSnapshot: snapshot ?? .unavailable,
                                     initialModule: opening)
            HUDStartupTrace.end("open.fullNativeView", since: &startup)
            systemView = view
            view.sourceWatchForVerification?.backdropPreparationForVerification = systemBackdropPreparationForVerification
            if let pointerLocationProvider = systemPointerLocationProviderForVerification {
                view.pointerLocationProvider = pointerLocationProvider
            }
            view.workFocusStatusMessage = workFocusStatusMessage
            view.workFocusNeedsAccessibilityPermission = workFocusNeedsAccessibilityPermission
            view.onRequestFocusAccess = { [weak self] in
                guard let self else { return }
                self.afterSystemClose = self.onRequestFocusAccess
                self.closeSystemOverlay()
            }
            let dragBoard = NSPasteboard(name: .drag)
            view.summonedDuringFileDrag = NSEvent.pressedMouseButtons & 1 != 0 && (
                HUDFileShelfInteraction.acceptsFiles(dragBoard) ||
                (opening == .notes && HUDNotesInteraction.acceptsImages(dragBoard)))
            initialModuleRequest = nil
            if let appLaunchError { view.showAppShortcutError(appLaunchError) }
            view.onLaunchAppShortcut = { [weak self] id in self?.requestAppShortcutLaunch(id) }
            view.onClose = { [weak self] in self?.closeSystemOverlay() }
            view.onQuitConfirmed = { [weak self] in self?.requestQuit() }
            view.onOpenProjection = { [weak self] in self?.openProjection() }
            view.onOpenSystemStorage = { [weak self] in
                guard let self, self.systemPhase == .open else { return }
                self.openStorageAfterClose = true
                self.closeSystemOverlay()
            }
            view.onToggle = { [weak self] in
                guard let self = self, !self.quitRequested else { return }
                self.performSystemAction(self.systemState.toggle())
            }
            view.onShelfDragSessionBegan = { [weak self, weak view] in
                guard let self, let view, self.systemView === view else { return }
                self.performShelfDragAction(self.shelfDragPresentation.begin())
            }
            view.onShelfDragSessionEnded = { [weak self, weak view] delivered in
                guard let self, let view, self.systemView === view else { return }
                let token = self.shelfDragPresentation.generation
                // Source leases and AppKit's session are released after its end
                // callback returns. Never tear down that source inside callback.
                DispatchQueue.main.async { [weak self, weak view] in
                    guard let self, let view, self.systemView === view,
                          self.shelfDragPresentation.generation == token else { return }
                    self.finishShelfDrag(delivered: delivered)
                }
            }
            view.onShelfReveal = { [weak self] access in
                guard let self, self.systemPhase == .open else { access.close(); return }
                self.pendingShelfReveal?.close()
                self.pendingShelfReveal = access
                self.closeSystemOverlay()
            }
            HUDStartupTrace.end("open.callbacks", since: &startup)
            panel.alphaValue = 1
            panel.contentView = view
            panel.setFrame(screen.frame, display: false)
            panel.allowsKey = true
            panel.ignoresMouseEvents = false
            panel.acceptsMouseMovedEvents = true
            NSApp.activate(ignoringOtherApps: true)
            panel.makeKeyAndOrderFront(nil)
            panel.makeFirstResponder(view)
            reconcileChargeMetricTelemetry()
            HUDStartupTrace.end("open.attachAndActivate", since: &startup)
            // Preparation has its own bounded fallback. The finite source
            // entrance deadline starts only when the real/fallback input is ready.
            armTransitionDeadline(after: HUDSourceWatchView.backdropPreparationTimeout + SystemHUDView.entranceDuration + 0.2) { [weak self] in
                self?.finishSystemOpening(token)
            }
            view.playsBootLogOnOpen = consumeDailyBootLog()
            view.animateEntrance(ready: { [weak self, weak view] in
                guard let self, let view, self.systemView === view,
                      self.systemState.phase == .opening, self.systemState.generation == token else { return }
                self.armTransitionDeadline(after: SystemHUDView.entranceDuration + 0.2) { [weak self] in
                    self?.finishSystemOpening(token)
                }
            }) { [weak self] in self?.finishSystemOpening(token) }
            HUDStartupTrace.end("open.beginEntrance", since: &startup)
        case .close(let token):
            logSystemPhase()
            systemView?.interactionEnabled = false
            armTransitionDeadline(after: SystemHUDView.exitDuration + 0.2) { [weak self] in
                self?.finishSystemClosing(token)
            }
            systemView?.animateExit { [weak self] in self?.finishSystemClosing(token) }
        }
    }

    private func requestAppShortcutLaunch(_ id: UUID) {
        guard systemState.phase == .open, systemView != nil,
              pendingAppLaunch == nil, !appLaunchInFlight else { return }
        do {
            let store = try appShortcutStore.get()
            guard let item = store.items.first(where: { $0.id == id }) else { return }
            let url = try store.resolvedURL(for: id)
            appLaunchError = nil
            pendingAppLaunch = (item.name, url)
            systemView?.engageAppShortcut(id)
            closeSystemOverlay()
        } catch { systemView?.showAppShortcutError(error.localizedDescription) }
    }

    private func completeAppShortcutHandoff(_ request: (name: String, url: URL)) {
        // Both the animation completion and deadline converge on one teardown.
        // The panel is already unordered and its content released at this point.
        appLaunchGeneration &+= 1
        let token = appLaunchGeneration
        appLaunchInFlight = true
        launchAppShortcut(request.url) { [weak self] result in
            guard let self, self.appLaunchGeneration == token else { return }
            self.appLaunchInFlight = false
            switch result {
            case .success:
                self.eventLog.record(kind: .appShortcutOpened, metadata: ["app": request.name])
            case .failure(let error):
                self.appLaunchError = error.localizedDescription
                // Keep a failed target editable and report the error in the HUD.
                // A forced shutdown invalidates this completion before it can reopen.
                guard self.systemState.phase == .closed else { return }
                // An explicit request: a chosen Open on module cannot replace it.
                self.initialModuleRequest = .addApp
                self.hide(animated: false)
                self.performSystemAction(self.systemState.toggle())
            }
        }
    }

    private func performShelfDragAction(_ action: ShelfDragPresentationState.Action) {
        guard let view = systemView else { return }
        switch action {
        case .none: break
        case .retract(let token):
            view.interactionEnabled = false
            panel.ignoresMouseEvents = true
            let completion = { [weak self] in
                guard let self else { return }
                self.performShelfDragAction(self.shelfDragPresentation.didRetract(token))
            }
            armShelfDragDeadline(after: SystemHUDView.exitDuration + 0.2, completion: completion)
            view.animateExit(completion: completion)
        case .hide:
            shelfDragDeadline?.cancel(); shelfDragDeadline = nil
            view.stopMotionForConcealment()
            panel.orderOut(nil)
            reconcileChargeMetricTelemetry()
        case .restore(let token):
            shelfDragDeadline?.cancel(); shelfDragDeadline = nil
            if closeAfterShelfDrag || forceCloseAfterShelfDrag { finishConcealedShelfClose(); return }
            panel.alphaValue = 1
            panel.ignoresMouseEvents = false
            NSApp.activate(ignoringOtherApps: true)
            panel.makeKeyAndOrderFront(nil)
            panel.makeFirstResponder(view)
            reconcileChargeMetricTelemetry()
            let completion = { [weak self] in
                guard let self, self.shelfDragPresentation.didRestore(token) else { return }
                self.shelfDragDeadline?.cancel(); self.shelfDragDeadline = nil
                self.systemView?.showStable(preservingChargeAnimation: true, preservingPointerMotion: true)
                self.systemView?.interactionEnabled = true
                let close = self.closeAfterShelfDrag
                self.closeAfterShelfDrag = false
                if close { self.closeSystemOverlay() }
            }
            armShelfDragDeadline(after: HUDSourceWatchView.backdropPreparationTimeout + SystemHUDView.entranceDuration + 0.2, completion: completion)
            view.animateEntrance(ready: { [weak self, weak view] in
                guard let self, let view, self.systemView === view,
                      self.shelfDragPresentation.generation == token else { return }
                self.armShelfDragDeadline(after: SystemHUDView.entranceDuration + 0.2, completion: completion)
            }, completion: completion)
        case .close: finishConcealedShelfClose()
        }
    }

    private func finishShelfDrag(delivered: Bool) {
        if forceCloseAfterShelfDrag { finishConcealedShelfClose(); return }
        performShelfDragAction(shelfDragPresentation.ended(delivered: delivered))
    }

    private func finishConcealedShelfClose() {
        let forced = forceCloseAfterShelfDrag
        shelfDragDeadline?.cancel(); shelfDragDeadline = nil
        shelfDragPresentation.reset()
        if forced { systemState.forceClose() }
        else {
            _ = systemState.requestClose()
            _ = systemState.didClose(systemState.generation)
        }
        logSystemPhase()
        // Exit already completed. Release the retained source without replaying
        // it or stealing focus back from the successful drop destination.
        tearDownSystemPresentation(restoreFocus: false, notify: !forced)
    }

    private func armShelfDragDeadline(after delay: Double, completion: @escaping () -> Void) {
        shelfDragDeadline?.cancel()
        let deadline = DispatchWorkItem(block: completion)
        shelfDragDeadline = deadline
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: deadline)
    }

    private func armTransitionDeadline(after delay: Double, completion: @escaping () -> Void) {
        transitionDeadline?.cancel()
        let work = DispatchWorkItem(block: completion)
        transitionDeadline = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }

    /// The boot log plays on the first open of each day (spec §3.28), unless it
    /// is turned off or motion is reduced. Diagnostic runs never touch the date.
    private func consumeDailyBootLog() -> Bool {
        guard configuration.dailyBootLog, !HUDRuntimeAppearance.reduceMotion, !Self.isDiagnosticRun else { return false }
        let parts = Calendar.current.dateComponents([.year, .month, .day], from: Date())
        let today = String(format: "%04d-%02d-%02d", parts.year ?? 0, parts.month ?? 0, parts.day ?? 0)
        let key = "dailyBootLogLastDay"
        guard UserDefaults.standard.string(forKey: key) != today else { return false }
        UserDefaults.standard.set(today, forKey: key)
        return true
    }

    private func finishSystemOpening(_ token: Int) {
        guard systemState.phase == .opening, systemState.generation == token else { return }
        transitionDeadline?.cancel()
        transitionDeadline = nil
        systemView?.showStable(preservingChargeAnimation: true, preservingPointerMotion: true,
                               preservingNowPlayingPresentation: true)
        let action = systemState.didOpen(token)
        logSystemPhase()
        systemView?.interactionEnabled = systemState.phase == .open && !documentTerminationInProgress
        performSystemAction(action)
        presentPendingShelfDrop()
    }

    private func finishSystemClosing(_ token: Int) {
        guard systemState.didClose(token) else { return }
        logSystemPhase()
        transitionDeadline?.cancel()
        transitionDeadline = nil
        // A later explicit handoff takes priority over a pending shelf reveal.
        // Teardown clears these handoff fields, so test before consuming them.
        if !canPresentShelfDrop { pendingShelfDropPresentation = nil }
        tearDownSystemPresentation(restoreFocus: true, notify: true)
        presentPendingShelfDrop()
    }

    private func logSystemPhase() {
        onSystemActivityChange?()
        guard CommandLine.arguments.contains("--ui-test") else { return }
        print("Power overlay: \(systemState.phase.rawValue)")
        fflush(stdout)
    }

    private func tearDownSystemPresentation(restoreFocus: Bool, notify: Bool) {
        cancelClosedHeapCleanup()
        let shouldQuit = quitAfterSystemClose
        let updateCompletion = applicationUpdateCompletion
        applicationUpdateCompletion = nil
        let shouldExit = shouldQuit || updateCompletion != nil
        quitAfterSystemClose = false
        let appLaunch = notify && !shouldExit ? pendingAppLaunch : nil
        pendingAppLaunch = nil
        let shelfReveal = pendingShelfReveal
        pendingShelfReveal = nil
        defer { shelfReveal?.close() }
        let shouldRestore = !shouldExit && restoreFocus && appLaunch == nil && shelfReveal == nil && !openStorageAfterClose && afterSystemClose == nil && NSApp.isActive && panel.isKeyWindow
        lastSystemModule = systemView?.selectedModule ?? lastSystemModule
        systemView?.cancelAnimations()
        lastClosedAnimationCount = systemView?.activeAnimationCount ?? 0
        lastClosedSourceTimerActive = systemView?.sourceWatchForVerification?.hasDisplayTimerForVerification ?? false
        lastClosedSourcePhase = systemView?.sourceWatchForVerification?.playback.phase
        panel.orderOut(nil)
        panel.alphaValue = 1
        closeAfterShelfDrag = false
        forceCloseAfterShelfDrag = false
        shelfDragDeadline?.cancel(); shelfDragDeadline = nil
        shelfDragPresentation.reset()
        panel.contentView = root
        systemView = nil // Release the full-display layer backing while idle.
        reconcileChargeMetricTelemetry()
        settingsController?.close()
        panel.allowsKey = false
        panel.ignoresMouseEvents = true
        panel.acceptsMouseMovedEvents = false
        systemScreenID = nil
        if shouldRestore, let previous = previousApplication, !previous.isTerminated,
           previous.processIdentifier != ProcessInfo.processInfo.processIdentifier {
            previous.activate(options: [.activateIgnoringOtherApps])
        }
        previousApplication = nil
        reposition()
        let action = afterSystemClose
        afterSystemClose = nil
        let openStorage = openStorageAfterClose
        openStorageAfterClose = false
        if let updateCompletion {
            DispatchQueue.main.async(execute: updateCompletion)
            return
        }
        if shouldQuit {
            // The dedicated one-shot completion runs only after the window and
            // all HUD layers are gone. Never restore charging or launch a handoff.
            onQuitAfterSystemClose?()
            return
        }
        if notify { scheduleClosedHeapCleanup() }
        if let action, notify { DispatchQueue.main.async(execute: action) }
        if notify { onSystemClosed?() }
        if openStorage && notify { _ = openSystemStorage() }
        if let shelfReveal, notify { revealShelfFile(shelfReveal.url) }
        if let appLaunch { completeAppShortcutHandoff(appLaunch) }
    }

    private func cancelClosedHeapCleanup() {
        closedHeapCleanupGeneration &+= 1
        closedHeapCleanup?.cancel(); closedHeapCleanup = nil
        closedHeapCleanupTicket?.cancel(); closedHeapCleanupTicket = nil
    }

    private func scheduleClosedHeapCleanup() {
        let generation = closedHeapCleanupGeneration
        let ticket = ClosedHeapCleanupTicket()
        closedHeapCleanupTicket = ticket
        let work = DispatchWorkItem { [weak self, ticket] in
            guard let self, self.closedHeapCleanupGeneration == generation,
                  self.systemPhase == .closed, self.systemView == nil else { return }
            self.closedHeapCleanup = nil
            guard !self.closedHeapCleanupInFlight else {
                self.closedHeapCleanupTicket = nil
                return
            }
            self.closedHeapCleanupInFlight = true
            // The centre's shared rasters served only the released pages; the
            // next open redraws the few its first screen shows.
            HUDArtwork.releaseCachedImages()
            // Frame construction leaves reusable malloc pages after the views
            // are released. Return only free pages once, off the main thread;
            // live shader/document/image caches remain ready for the next open.
            // The map's decoded geography is dropped first (it decodes again,
            // off the main thread, while the next map deploys).
            DispatchQueue.global(qos: .utility).async { [weak self, ticket] in
                let elapsed: Double?
                if ticket.begin() {
                    let began = CACurrentMediaTime()
                    WorldMapSurface.releaseSharedData()
                    _ = malloc_zone_pressure_relief(nil, 0)
                    elapsed = (CACurrentMediaTime() - began) * 1000
                } else { elapsed = nil }
                DispatchQueue.main.async { [weak self, ticket] in
                    guard let self else { return }
                    self.closedHeapCleanupInFlight = false
                    if self.closedHeapCleanupTicket === ticket { self.closedHeapCleanupTicket = nil }
                    if let elapsed {
                        self.closedHeapCleanupRunsForVerification += 1
                        self.closedHeapCleanupLastRunGenerationForVerification = generation
                        self.lastClosedHeapCleanupMillisecondsForVerification = elapsed
                    }
                }
            }
        }
        closedHeapCleanup = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5, execute: work)
    }

    private func discardPosition() { finishPositionEditing(layout: nil) }
    private func confirmPosition() {
        guard isEditingPosition, let anchor = draftAnchor, let screen = selectedScreen() else { return }
        let bar = barConfiguration
        let position = draftMoved ? OverlayGeometry.normalizedPosition(anchor: anchor, screen: screen) : nil
        finishPositionEditing(layout: ChargeBarLayout(position: position, scale: bar.scale, length: bar.chargeBarLength))
    }

    private func finishPositionEditing(layout: ChargeBarLayout?) {
        guard isEditingPosition else { return }
        isEditingPosition = false
        root.editing = false
        root.install(nil)
        panel.allowsKey = false
        panel.ignoresMouseEvents = true
        draftAnchor = nil
        draftScreenID = nil
        shownLength = nil
        editorStripFrame = nil
        hide(animated: false)
        // Drop the drafts' Size and Length; a confirmed layout arrives as configuration.
        indicator.set(snapshot: latestBatterySnapshot, configuration: configuration)
        if CommandLine.arguments.contains("--ui-test") {
            print("Position edit finished: \(String(describing: layout))")
            fflush(stdout)
        }
        onPositionEditFinished?(layout)
    }

    /// The strip: Size (the bar's scale, as the former Settings row) and
    /// Length (a share of this display's usable width), then Cancel and Confirm.
    private func installPositionEditor() {
        let size = HUDPlacementEditor.Slider(id: Self.editorSizeID, title: L10n.text("Size", "大小"), value: draftScale * 100,
                                             range: HUDSlider.Range(minimum: OverlayGeometry.scaleRange.lowerBound * 100,
                                                                    maximum: OverlayGeometry.scaleRange.upperBound * 100, step: 1),
                                             format: .percent)
        let length = HUDPlacementEditor.Slider(id: Self.editorLengthID, title: L10n.text("Length", "长度"), value: 100,
                                               range: HUDSlider.Range(minimum: 0, maximum: 100, step: 1), format: .percent)
        root.env.renderScale = panel.backingScaleFactor
        let editor = HUDPlacementEditor(id: "chargeBar", sliders: [size, length], env: root.env)
        editor.onChange = { [weak self] id, value, final in self?.editorChanged(id, value: value, final: final) }
        editor.onDecision = { [weak self] decision in
            if decision == .confirm { self?.confirmPosition() } else { self?.discardPosition() }
        }
        root.install(editor)
    }

    /// The Length slider's range follows the display and Size (200 pt minimum).
    private func syncLengthSlider(screen: NSRect) {
        guard let editor = root.editor else { return }
        let range = OverlayGeometry.lengthRange(screen: screen, scale: barConfiguration.scale)
        let minimum = min(100, (range.lowerBound * 100).rounded(.up))
        let slider = HUDSlider.Range(minimum: minimum, maximum: 100, step: 1)
        let value = slider.clamp(OverlayGeometry.length(of: barConfiguration, screen: screen) * 100)
        editor.setRange(slider, value: value, for: Self.editorLengthID)
        shownLength = value
    }

    private func editorChanged(_ id: String, value: Double, final: Bool) {
        guard isEditingPosition else { return }
        if id == Self.editorSizeID { draftScale = value / 100 }
        else if id == Self.editorLengthID {
            guard let next = OverlayGeometry.draftLength(reported: value, shown: shownLength) else { return }
            draftLength = next
        }
        indicator.set(snapshot: latestBatterySnapshot, configuration: barConfiguration, preview: true)
        reposition()
    }

    private func editorPointerDown(_ event: NSEvent) {
        guard isEditingPosition, let editor = root.editor, let layer = root.layer, let anchor = draftAnchor else { return }
        editor.pointerDown(at: root.convert(event.locationInWindow, from: nil), in: layer,
                           pointer: panel.convertPoint(toScreen: event.locationInWindow), anchor: anchor)
        if editor.isDragging, CommandLine.arguments.contains("--ui-test") {
            print("Position drag began")
            fflush(stdout)
        }
    }

    private func editorPointerDragged(_ event: NSEvent) {
        guard isEditingPosition, let editor = root.editor, let layer = root.layer else { return }
        let mouse = panel.convertPoint(toScreen: event.locationInWindow)
        guard let proposed = editor.pointerDragged(to: root.convert(event.locationInWindow, from: nil), in: layer,
                                                   pointer: mouse) else { return }
        let screens = availableScreens()
        let target = screens.first(where: { $0.visibleFrame.contains(mouse) })
            ?? screens.first(where: { $0.id == draftScreenID }) ?? screens.first
        guard let screen = target else { return }
        let changedScreen = screen.id != draftScreenID
        draftScreenID = screen.id
        let anchor = clampedDraftAnchor(proposed, on: screen)
        draftAnchor = anchor
        draftMoved = true
        if changedScreen { syncLengthSlider(screen: screen.visibleFrame) }
        layout(anchor: anchor, screen: screen.visibleFrame)
    }

    private func editorPointerUp(_ event: NSEvent) {
        guard let editor = root.editor, let layer = root.layer else { return }
        let released = editor.isAdjusting
        editor.pointerUp(at: root.convert(event.locationInWindow, from: nil), in: layer)
        // The strip, held still under a slider, settles beside the bar again.
        if released, isEditingPosition { editorStripFrame = nil; reposition() }
    }

    private func availableScreens() -> [OverlayScreen] {
        NSScreen.screens.compactMap { screen in
            guard let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber else { return nil }
            return OverlayScreen(id: number.uint32Value, visibleFrame: screen.visibleFrame)
        }
    }

    private func selectedScreen() -> OverlayScreen? {
        let screens = availableScreens()
        let requested = isEditingPosition ? draftScreenID
            : (configuration.placement == .custom ? configuration.customPosition.screenID : nil)
        if let id = requested, let match = screens.first(where: { $0.id == id }) { return match }
        return screens.first(where: { CGDisplayIsBuiltin($0.id) != 0 }) ?? screens.first
    }

    /// The panel holds the bar's canvas, centred on the anchor; while editing
    /// it also holds the strip beside the bar (`HUDPlacementEditor.frame`).
    private func layout(anchor: NSPoint, screen: NSRect) {
        let bar = barConfiguration
        let scale = CGFloat(bar.scale)
        let width = barWidth(on: screen)
        indicator.setBarWidth(width)
        let canvas = NSSize(width: OverlayGeometry.canvasWidth(barWidth: width), height: ChargeIndicatorView.canvasSize.height)
        let barFrame = NSRect(x: anchor.x - canvas.width * scale / 2, y: anchor.y - canvas.height * scale / 2,
                              width: canvas.width * scale, height: canvas.height * scale)
        var frame = barFrame
        var strip: NSRect?
        if isEditingPosition, let editor = root.editor {
            if let held = editorStripFrame, editor.isAdjusting { strip = held } else {
                // Spaced for the largest Size, so a Size change never moves
                // the strip vertically or slides the bar under it; y-down for
                // the shared placement rule.
                let reach = OverlayGeometry.editorReach(anchor: anchor, screen: screen)
                let body = NSRect(x: anchor.x - width * scale / 2, y: reach.lowerBound,
                                  width: width * scale, height: reach.upperBound - reach.lowerBound)
                func flipped(_ rect: NSRect) -> NSRect { NSRect(x: rect.minX, y: -rect.maxY, width: rect.width, height: rect.height) }
                let size = NSSize(width: editor.size.width, height: editor.size.height + HUDPlacementEditor.bubbleClearance)
                strip = flipped(HUDPlacementEditor.frame(size: size, beside: flipped(body), within: flipped(screen), gap: 10))
            }
            editorStripFrame = strip
            frame = frame.union(strip!)
        }
        panel.setFrame(frame, display: false)
        root.frame = NSRect(origin: .zero, size: frame.size)
        indicator.frame = NSRect(x: barFrame.minX - frame.minX, y: frame.maxY - barFrame.maxY,
                                 width: barFrame.width, height: barFrame.height)
        indicator.bounds = NSRect(origin: .zero, size: canvas)
        if let strip, let editor = root.editor {
            HUDMotion.perform {
                editor.layer.frame = NSRect(x: strip.minX - frame.minX,
                                            y: frame.maxY - strip.maxY + HUDPlacementEditor.bubbleClearance,
                                            width: editor.size.width, height: editor.size.height)
            }
            root.updateAccessibility()
        }
    }
}

private final class PositionPanel: NSPanel {
    var allowsKey = false
    var onResignKey: (() -> Void)?
    override var canBecomeKey: Bool { allowsKey }
    override var canBecomeMain: Bool { false }
    override func resignKey() { super.resignKey(); onResignKey?() }
}

/// The alert panel's content. While editing it hosts the bar editor's strip
/// (HUDPlacementEditor) in its layer: pointer, hover, keys and VoiceOver
/// stand-ins for the strip; any other press drags the bar.
private final class PositionCanvas: NSView, HUDControlFeedbackHost {
    var editing = false
    let env = HUDDesignEnvironment()
    private(set) var editor: HUDPlacementEditor?
    private var standIns: HUDAccessibilityStandIns?
    private var tracking: NSTrackingArea?
    private var pointer: CGPoint?
    var onMouseDown: ((NSEvent) -> Void)?
    var onMouseDragged: ((NSEvent) -> Void)?
    var onMouseUp: ((NSEvent) -> Void)?
    /// Returns true when the key was handled (Esc, Return).
    var onKey: ((UInt16) -> Bool)?
    var onAppearanceChange: (() -> Void)?
    override var isFlipped: Bool { true }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    /// Shows a strip (nil removes it). Layers exist only while editing.
    func install(_ next: HUDPlacementEditor?) {
        editor?.cancelPointer()
        editor?.layer.removeFromSuperlayer()
        editor = next
        if let next, let layer {
            HUDMotion.perform { next.layer.zPosition = 10; layer.addSublayer(next.layer) }
        }
        if next == nil { pointer = nil }
        updateAccessibility()
    }

    func updateAccessibility() {
        guard let editor, let layer else { standIns?.update([]); return }
        let standIns = self.standIns ?? HUDAccessibilityStandIns(host: self)
        if self.standIns == nil {
            standIns.onPerform = { [weak self] id in self?.editor?.perform(id) }
            standIns.onAdjust = { [weak self] id, value in self?.editor?.adjust(id, to: value) }
            self.standIns = standIns
        }
        standIns.update(editor.accessibilityItems(in: layer))
    }

    func refreshControlHighlights() {
        guard let editor, let layer else { return }
        editor.updateHighlights(in: layer, point: pointer)
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        onAppearanceChange?()
    }
    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        let scale = window?.backingScaleFactor ?? 2
        guard scale != env.renderScale else { return }
        env.renderScale = scale
        if let editor { env.applyScale(to: editor.layer) }
    }
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking { removeTrackingArea(tracking) }
        let next = NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeAlways, .inVisibleRect],
                                  owner: self)
        addTrackingArea(next); tracking = next
    }
    override var acceptsFirstResponder: Bool { editing }
    override var mouseDownCanMoveWindow: Bool { false }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { editing }
    override func hitTest(_ point: NSPoint) -> NSView? {
        let result = super.hitTest(point)
        guard editing, result != nil else { return result }
        return self
    }
    override func mouseMoved(with event: NSEvent) {
        guard editing, editor != nil else { return }
        pointer = convert(event.locationInWindow, from: nil); refreshControlHighlights()
    }
    override func mouseEntered(with event: NSEvent) { mouseMoved(with: event) }
    override func mouseExited(with event: NSEvent) { pointer = nil; refreshControlHighlights() }
    override func mouseDown(with event: NSEvent) { if editing { onMouseDown?(event) } }
    override func mouseDragged(with event: NSEvent) {
        guard editing else { return }
        pointer = convert(event.locationInWindow, from: nil)
        onMouseDragged?(event)
    }
    override func mouseUp(with event: NSEvent) { onMouseUp?(event) }
    override func keyDown(with event: NSEvent) {
        if editing, onKey?(event.keyCode) == true { return }
        super.keyDown(with: event)
    }
}
