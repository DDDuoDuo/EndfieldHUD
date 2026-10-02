import AppKit

/// One shared panel. The charging HUD rests idle; the summonable Power HUD
/// owns ambient Core Animation tracks only while fully open.
final class OverlayController: NSObject {
    private let panel: PositionPanel
    private let root: PositionCanvas
    private let indicator: ChargeIndicatorView
    private let cancelButton = PositionActionButton(confirm: false)
    private let confirmButton = PositionActionButton(confirm: true)
    private var dismissal: DispatchWorkItem?
    private var generation = 0
    private var configuration = AppConfiguration.defaults
    private var draftAnchor: NSPoint?
    private var draftScreenID: UInt32?
    private var dragOrigin: NSPoint?
    private var dragMouseOrigin: NSPoint?
    private var presentationCompleted = false
    private var requestedDuration: Double = 5
    private var systemView: SystemHUDView?
    private lazy var mapStore: Result<WorldMapStore, Error> = Result { try WorldMapStore(directory: WorldMapStore.applicationDirectory()) }
    private lazy var notesStore: Result<NotesStore, Error> = Result { try NotesStore(directory: NotesStore.applicationDirectory()) }
    private lazy var shelfStore: Result<FileShelfStore, Error> = Result { try FileShelfStore(directory: FileShelfStore.applicationDirectory()) }
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
    let storage: StorageController
    let activity: SystemActivityMonitor
    let appActivity: AppActivityMonitor
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
            && !shelfDragPresentation.isActive && applicationUpdateCompletion == nil && !quitRequested
    }
    var onQuitAccepted: (() -> Void)?
    var onQuitAfterSystemClose: (() -> Void)?
    var initialModuleRequest: HUDModule?
    let workMode = WorkModeController()
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
    private var lastSystemModule: HUDModule = .map
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
    func performStorageActionForVerification(_ id: String) { systemView?.performStorageActionForVerification(id) }
    func addAppShortcutForVerification(_ url: URL) throws -> AppShortcut {
        let store = try appShortcutStore.get()
        return try store.save(candidate: store.inspect(url: url), name: "Launch test", iconPreset: .original)
    }
    func performAppShortcutActionForVerification(_ id: String) { systemView?.performAppShortcutActionForVerification(id) }
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
    var onPositionEditFinished: ((OverlayPosition?) -> Void)?

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
        storage = diagnostic ? .fixture() : StorageController()
        activity = diagnostic ? .fixture() : SystemActivityMonitor()
        appActivity = diagnostic ? .fixture() : AppActivityMonitor()
        let log = SystemEventLog(directory: diagnostic ? nil : SystemEventLog.applicationDirectory())
        eventLog = log
        eventRecorder = SystemEventRecorder(log: log)
        let size = ChargeIndicatorView.canvasSize
        panel = PositionPanel(contentRect: NSRect(origin: .zero, size: size),
                              styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        root = PositionCanvas(frame: NSRect(origin: .zero, size: size))
        indicator = ChargeIndicatorView(frame: NSRect(origin: .zero, size: size))
        super.init()
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
        root.addSubview(cancelButton)
        root.addSubview(confirmButton)
        panel.contentView = root
        cancelButton.target = self
        cancelButton.action = #selector(discardPosition)
        confirmButton.target = self
        confirmButton.action = #selector(confirmPosition)
        cancelButton.isHidden = true
        confirmButton.isHidden = true
        root.onMouseDown = { [weak self] event in
            guard let self = self, self.isEditingPosition else { return }
            self.dragMouseOrigin = self.panel.convertPoint(toScreen: event.locationInWindow)
            self.dragOrigin = self.draftAnchor
            if CommandLine.arguments.contains("--ui-test") {
                print("Position drag began")
                fflush(stdout)
            }
        }
        root.onMouseDragged = { [weak self] event in self?.dragPosition(event: event) }
        root.onMouseUp = { [weak self] in
            self?.dragOrigin = nil
            self?.dragMouseOrigin = nil
        }
        root.onConfirm = { [weak self] in self?.confirmPosition() }
        root.onCancel = { [weak self] in self?.discardPosition() }
        root.onAppearanceChange = { [weak self] in
            guard let self = self else { return }
            self.cancelButton.dark = self.isDark
            self.confirmButton.dark = self.isDark
        }
    }

    func update(snapshot: BatterySnapshot, configuration: AppConfiguration, preview: Bool = false) {
        let displayChanged = self.configuration.hudDisplayUUID != configuration.hudDisplayUUID
            || self.configuration.openOnActiveDisplay != configuration.openOnActiveDisplay
        let geometryChanged = self.configuration.scale != configuration.scale
            || self.configuration.placement != configuration.placement
            || self.configuration.customPosition != configuration.customPosition
        self.configuration = configuration.normalized
        indicator.set(snapshot: snapshot, configuration: self.configuration, preview: preview)
        systemView?.set(snapshot: snapshot, configuration: self.configuration)
        cancelButton.dark = isDark
        confirmButton.dark = isDark
        cancelButton.setAccessibilityLabel(L10n.text("Discard position", "取消位置更改"))
        confirmButton.setAccessibilityLabel(L10n.text("Confirm position", "确认位置"))
        cancelButton.toolTip = L10n.text("Discard (Esc)", "取消（Esc）")
        confirmButton.toolTip = L10n.text("Confirm (Return)", "确认（回车）")
        if displayChanged, isSystemOverlayActive,
           let screen = HUDDisplayPolicy.targetScreen(configuration: self.configuration) {
            systemScreenID = HUDDisplayPolicy.displayID(for: screen)
            reposition()
            systemView?.animateDisplayArrival()
        } else if geometryChanged || isEditingPosition { reposition() }
    }

    /// Explicit previews replay the entrance even if an always-visible HUD is already on screen.
    func show(persistent: Bool, duration: Double, replay: Bool = false) {
        guard !isEditingPosition, !isSystemOverlayActive else { return }
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
            return
        }
        indicator.animateExit { [weak self] in
            guard let self = self, self.generation == token else { return }
            self.panel.orderOut(nil)
            self.isVisible = false
        }
    }

    func reposition() {
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
            anchor = OverlayGeometry.clampedAnchor(draft, screen: screen.visibleFrame,
                                                   scale: configuration.scale, editing: true)
            draftAnchor = anchor
            draftScreenID = screen.id
        } else {
            anchor = OverlayGeometry.anchor(configuration: configuration, screen: screen.visibleFrame, editing: false)
        }
        layout(anchor: anchor)
    }

    func beginPositionEditing(snapshot: BatterySnapshot, configuration: AppConfiguration) {
        guard !isSystemOverlayActive else { closeSystemOverlay(); return }
        guard !isEditingPosition else { panel.makeKeyAndOrderFront(nil); return }
        update(snapshot: snapshot, configuration: configuration, preview: true)
        dismissal?.cancel()
        dismissal = nil
        generation += 1
        indicator.cancelAnimations()
        guard let screen = selectedScreen() else { return }
        draftScreenID = screen.id
        draftAnchor = OverlayGeometry.anchor(configuration: configuration, screen: screen.visibleFrame, editing: true)
        isEditingPosition = true
        isVisible = true
        isPersistent = true
        presentationCompleted = true
        panel.allowsKey = true
        panel.ignoresMouseEvents = false
        root.editing = true
        cancelButton.isHidden = false
        confirmButton.isHidden = false
        indicator.setStage(.compact, animated: false)
        reposition()
        NSApp.activate(ignoringOtherApps: true)
        panel.makeKeyAndOrderFront(nil)
        panel.makeFirstResponder(root)
        if CommandLine.arguments.contains("--ui-test"), let anchor = draftAnchor {
            let desktopTop = NSScreen.screens.first?.frame.maxY ?? 0
            print("Position editor center: \(anchor.x), \(desktopTop - anchor.y) from desktop top-left")
            fflush(stdout)
        }
    }

    func cancelPositionEditing() {
        if isEditingPosition { finishPositionEditing(position: nil) }
    }

    func confirmEditedPosition() { confirmPosition() }

    /// Both presentations share this controller's one NSPanel and battery stream.
    @discardableResult func toggleSystemOverlay(snapshot: BatterySnapshot, configuration: AppConfiguration) -> Bool {
        guard !quitRequested, !isEditingPosition, !appLaunchInFlight else { return false }
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
        quitRequested = true
        pendingAppLaunch = nil
        pendingShelfReveal?.close(); pendingShelfReveal = nil
        openStorageAfterClose = false; afterSystemClose = nil
        cancelPositionEditing()
        if systemPhase == .closed {
            hide(animated: false)
            DispatchQueue.main.async(execute: completion)
        } else {
            applicationUpdateCompletion = completion
            closeSystemOverlay()
        }
    }

    func cancelApplicationUpdate() {
        applicationUpdateCompletion = nil
        if !quitAfterSystemClose { quitRequested = false }
    }

    func closeSystemOverlayForFocusLoss(activatedApplication: NSRunningApplication? = nil) {
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
            let requestedScreen = HUDDisplayPolicy.targetScreen(configuration: configuration)
            guard let screen = requestedScreen else {
                systemState.forceClose(); return
            }
            previousApplication = NSWorkspace.shared.frontmostApplication
            systemScreenID = (screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value
            let view = SystemHUDView(frame: NSRect(origin: .zero, size: screen.frame.size),
                                     notesStore: notesStore, shelfStore: shelfStore, clipboard: clipboard,
                                     audio: audio, perAppAudio: perAppAudio, workMode: workMode, eventLog: eventLog,
                                     storage: storage, activity: activity, appActivity: appActivity, appShortcuts: appShortcutStore,
                                     settings: settingsController, profile: profileStore, mapStore: mapStore,
                                     initialConfiguration: configuration, initialSnapshot: snapshot ?? .unavailable,
                                     initialModule: initialModuleRequest ?? lastSystemModule)
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
                (lastSystemModule == .notes && HUDNotesInteraction.acceptsImages(dragBoard)))
            initialModuleRequest = nil
            if let appLaunchError { view.showAppShortcutError(appLaunchError) }
            view.onLaunchAppShortcut = { [weak self] id in self?.requestAppShortcutLaunch(id) }
            view.onClose = { [weak self] in self?.closeSystemOverlay() }
            view.onQuitConfirmed = { [weak self] in self?.requestQuit() }
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
            panel.alphaValue = 1
            panel.contentView = view
            panel.setFrame(screen.frame, display: false)
            panel.allowsKey = true
            panel.ignoresMouseEvents = false
            panel.acceptsMouseMovedEvents = true
            NSApp.activate(ignoringOtherApps: true)
            panel.makeKeyAndOrderFront(nil)
            panel.makeFirstResponder(view)
            // Preparation has its own bounded fallback. The finite source
            // entrance deadline starts only when the real/fallback input is ready.
            armTransitionDeadline(after: HUDSourceWatchView.backdropPreparationTimeout + SystemHUDView.entranceDuration + 0.2) { [weak self] in
                self?.finishSystemOpening(token)
            }
            view.animateEntrance(ready: { [weak self, weak view] in
                guard let self, let view, self.systemView === view,
                      self.systemState.phase == .opening, self.systemState.generation == token else { return }
                self.armTransitionDeadline(after: SystemHUDView.entranceDuration + 0.2) { [weak self] in
                    self?.finishSystemOpening(token)
                }
            }) { [weak self] in self?.finishSystemOpening(token) }
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
                self.lastSystemModule = .addApp
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
        case .restore(let token):
            shelfDragDeadline?.cancel(); shelfDragDeadline = nil
            if closeAfterShelfDrag || forceCloseAfterShelfDrag { finishConcealedShelfClose(); return }
            panel.alphaValue = 1
            panel.ignoresMouseEvents = false
            NSApp.activate(ignoringOtherApps: true)
            panel.makeKeyAndOrderFront(nil)
            panel.makeFirstResponder(view)
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

    private func finishSystemOpening(_ token: Int) {
        guard systemState.phase == .opening, systemState.generation == token else { return }
        transitionDeadline?.cancel()
        transitionDeadline = nil
        systemView?.showStable(preservingChargeAnimation: true, preservingPointerMotion: true)
        let action = systemState.didOpen(token)
        eventLog.record(kind: .overlayOpened)
        logSystemPhase()
        systemView?.interactionEnabled = systemState.phase == .open
        performSystemAction(action)
    }

    private func finishSystemClosing(_ token: Int) {
        guard systemState.didClose(token) else { return }
        logSystemPhase()
        transitionDeadline?.cancel()
        transitionDeadline = nil
        tearDownSystemPresentation(restoreFocus: true, notify: true)
    }

    private func logSystemPhase() {
        onSystemActivityChange?()
        guard CommandLine.arguments.contains("--ui-test") else { return }
        print("Power overlay: \(systemState.phase.rawValue)")
        fflush(stdout)
    }

    private func tearDownSystemPresentation(restoreFocus: Bool, notify: Bool) {
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
        if let action, notify { DispatchQueue.main.async(execute: action) }
        if notify { onSystemClosed?() }
        if openStorage && notify { _ = openSystemStorage() }
        if let shelfReveal, notify { revealShelfFile(shelfReveal.url) }
        if let appLaunch { completeAppShortcutHandoff(appLaunch) }
    }

    @objc private func discardPosition() { finishPositionEditing(position: nil) }
    @objc private func confirmPosition() {
        guard isEditingPosition, let anchor = draftAnchor, let screen = selectedScreen() else { return }
        finishPositionEditing(position: OverlayGeometry.normalizedPosition(anchor: anchor, screen: screen))
    }

    private func finishPositionEditing(position: OverlayPosition?) {
        guard isEditingPosition else { return }
        isEditingPosition = false
        root.editing = false
        panel.allowsKey = false
        panel.ignoresMouseEvents = true
        cancelButton.isHidden = true
        confirmButton.isHidden = true
        draftAnchor = nil
        draftScreenID = nil
        dragOrigin = nil
        dragMouseOrigin = nil
        hide(animated: false)
        if CommandLine.arguments.contains("--ui-test") {
            print("Position edit finished: \(String(describing: position))")
            fflush(stdout)
        }
        onPositionEditFinished?(position)
    }

    private func dragPosition(event: NSEvent) {
        guard isEditingPosition, let origin = dragOrigin, let mouseOrigin = dragMouseOrigin else { return }
        let mouse = panel.convertPoint(toScreen: event.locationInWindow)
        let proposed = NSPoint(x: origin.x + mouse.x - mouseOrigin.x,
                               y: origin.y + mouse.y - mouseOrigin.y)
        let screens = availableScreens()
        let target = screens.first(where: { $0.visibleFrame.contains(mouse) })
            ?? screens.first(where: { $0.id == draftScreenID }) ?? screens.first
        guard let screen = target else { return }
        draftScreenID = screen.id
        draftAnchor = OverlayGeometry.clampedAnchor(proposed, screen: screen.visibleFrame,
                                                    scale: configuration.scale, editing: true)
        layout(anchor: draftAnchor!)
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

    private func layout(anchor: NSPoint) {
        let scale = CGFloat(configuration.scale)
        let canvas = ChargeIndicatorView.canvasSize
        let width = canvas.width * scale
        let canvasHeight = canvas.height * scale
        let height = isEditingPosition ? max(canvasHeight, 66 * scale + 34) : canvasHeight
        // The indicator's center stays at the saved anchor when edit controls appear below it.
        panel.setFrame(NSRect(x: anchor.x - width / 2, y: anchor.y + canvasHeight / 2 - height,
                              width: width, height: height), display: false)
        root.frame = NSRect(x: 0, y: 0, width: width, height: height)
        indicator.frame = NSRect(x: 0, y: 0, width: width, height: canvasHeight)
        indicator.bounds = NSRect(origin: .zero, size: canvas)
        cancelButton.frame = NSRect(x: width / 2 - 34, y: 66 * scale, width: 28, height: 28)
        confirmButton.frame = NSRect(x: width / 2 + 6, y: 66 * scale, width: 28, height: 28)
    }

    private var isDark: Bool {
        switch configuration.theme {
        case .dark: return true
        case .light: return false
        case .system: return NSApp.effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
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

private final class PositionCanvas: NSView {
    var editing = false
    var onMouseDown: ((NSEvent) -> Void)?
    var onMouseDragged: ((NSEvent) -> Void)?
    var onMouseUp: (() -> Void)?
    var onConfirm: (() -> Void)?
    var onCancel: (() -> Void)?
    var onAppearanceChange: (() -> Void)?
    override var isFlipped: Bool { true }
    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        onAppearanceChange?()
    }
    override var acceptsFirstResponder: Bool { editing }
    override var mouseDownCanMoveWindow: Bool { false }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { editing }
    override func hitTest(_ point: NSPoint) -> NSView? {
        let result = super.hitTest(point)
        guard editing, let hit = result else { return result }
        return hit is NSButton ? hit : self
    }
    override func mouseDown(with event: NSEvent) { if editing { onMouseDown?(event) } }
    override func mouseDragged(with event: NSEvent) { if editing { onMouseDragged?(event) } }
    override func mouseUp(with event: NSEvent) { onMouseUp?() }
    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 { onCancel?() }
        else if event.keyCode == 36 || event.keyCode == 76 { onConfirm?() }
        else { super.keyDown(with: event) }
    }
}

private final class PositionActionButton: NSButton {
    let confirm: Bool
    override var isFlipped: Bool { true }
    var dark = true { didSet { needsDisplay = true } }
    init(confirm: Bool) {
        self.confirm = confirm
        super.init(frame: .zero)
        isBordered = false
        title = ""
        setButtonType(.momentaryChange)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    override func draw(_ dirtyRect: NSRect) {
        (dark ? NSColor(white: 0.12, alpha: 0.97) : NSColor(white: 0.96, alpha: 0.98)).setFill()
        let circle = NSBezierPath(ovalIn: bounds.insetBy(dx: 1, dy: 1))
        circle.fill()
        (dark ? NSColor.white : NSColor.black).withAlphaComponent(isHighlighted ? 0.5 : 0.85).setStroke()
        let symbol = NSBezierPath()
        symbol.lineWidth = 1.7
        symbol.lineCapStyle = .round
        symbol.lineJoinStyle = .round
        if confirm {
            symbol.move(to: NSPoint(x: 8, y: 14))
            symbol.line(to: NSPoint(x: 12, y: 18))
            symbol.line(to: NSPoint(x: 20, y: 10))
        } else {
            symbol.move(to: NSPoint(x: 9, y: 9)); symbol.line(to: NSPoint(x: 19, y: 19))
            symbol.move(to: NSPoint(x: 9, y: 19)); symbol.line(to: NSPoint(x: 19, y: 9))
        }
        symbol.stroke()
    }
}
