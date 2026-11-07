import AppKit
import QuartzCore

/// The persistent instrument shell shared by all sections. Only the center
/// content changes during navigation; shell layers and motion retain identity.
/// Finite deployment, pointer response, and ambient tracks have separate owners.
/// All motion stops before this view is hidden or released.
final class SystemHUDView: NSView, HUDControlFeedbackHost {
    // Controller deadlines cover the original Watch wrapper's 0.75s clip.
    // The other macOS module shells retain their existing deployment cues.
    static let entranceDuration: TimeInterval = 0.75
    static let exitDuration: TimeInterval = HUDChargeBadge.exitDuration
    var onClose: (() -> Void)?
    var onQuitConfirmed: (() -> Void)?
    private var quitPromptOpen = false
    private var quitCommitted = false
    private var allowsModuleInput: Bool { interactionEnabled && !transitioning && !quitCommitted }
    var onOpenSystemStorage: (() -> Void)?
    var onOpenProjection: (() -> Void)?
    var onLaunchAppShortcut: ((UUID) -> Void)?
    var onToggle: (() -> Void)?
    var onShelfDragSessionBegan: (() -> Void)?
    var onShelfDragSessionEnded: ((Bool) -> Void)?
    var onShelfReveal: ((ShelfFileAccess) -> Void)?
    // Read once at deployment/start, without waiting for a mouse-moved event.
    // The graphical harness can provide a stationary screen point.
    var pointerLocationProvider: () -> CGPoint = { NSEvent.mouseLocation }
    private var sourceWatch: HUDSourceWatchView?
    private var sourceWatchFailure: NSTextField?
    private var sourceWatchFailureReason: String?
    private var sourceOverviewPresented = false
    private var sourceEntranceReady: (() -> Void)?
    private var sourceCenterProjection: CATransform3D?
    private let moduleContrast = CAGradientLayer()
    /// Map occlusion (v1.2.0): the round map runs under the source shell's
    /// Storage and Activity Monitor plates, which are cut out of the centre.
    private let mapForegroundMask = CAShapeLayer()
    private let mapContrastMask = CAShapeLayer()
    private var sourceBottomSilhouettes: [[CGPoint]] = []
    private var usesSourceShell: Bool { sourceWatch != nil && sourceWatchFailureReason == nil }
    var sourceWatchForVerification: HUDSourceWatchView? { sourceWatch }
    var isPreparingSourceBackdrop: Bool { sourceWatch?.isPreparingBackdrop == true }
    var sourceFailureForVerification: String? { sourceWatchFailureReason }
    var legacyProgressHiddenForVerification: Bool { progress.isHidden }
    /// The quit confirm bar is drawn on the core plane, so it follows the source tilt itself.
    var confirmationFollowsSourceForVerification: Bool {
        quitPromptOpen && designContext.overlay.layer.superlayer === corePlane.content
    }
    var clockIsInStatusPanelForVerification: Bool {
        usesSourceShell && !statusPanel.isHidden && clockPage.layer.superlayer === statusPanel
            && clockTime.superlayer === clockPage.page && clockDate.superlayer === clockPage.page
            && workBadge.superlayer === statusPanel
            && sourceStatusProjection != nil && !(clockTime.string as? String ?? "").isEmpty
    }
    func clockPointForVerification(_ local: CGPoint) -> CGPoint? {
        guard usesSourceShell, !statusPanel.isHidden else { return nil }
        let design = HUDMotionMath.project(local, through: statusPanel.transform)
        return CGPoint(x: designOrigin.x + design.x * designScale, y: designOrigin.y + design.y * designScale)
    }
    var clockStyleForVerification: HUDClockStyle { configuration.clockStyle }
    var clockTransitionIsHorizontalForVerification: Bool {
        clockPage.hasContainedHorizontalTransition
    }
    var clockTransitionKeepsFrameStationaryForVerification: Bool {
        clockPage.layer.frame == HUDClockPageViewport.contentRect
            && clockStyleArtwork.selection.superlayer === statusPanel
            && clockStyleArtwork.layer.superlayer === clockPage.page
            && [statusPanel, statusPlate, statusFrame, statusUnderline, workBadge,
                clockStyleArtwork.selection, clockPage.layer].allSatisfy {
                $0.animation(forKey: kCATransition) == nil
            }
    }
    /// UI verification drives the redesigned modules through their real
    /// controls: ids are the canvases' control ids (menu rows `menu:item`).
    func designCanvasForVerification(_ module: HUDModule) -> HUDDesignCanvas? {
        precondition(CommandLine.arguments.contains("--ui-test")); return designCanvases[module]
    }
    func designControlIDsForVerification(_ module: HUDModule) -> [String] {
        designCanvases[module]?.accessibilityItems.map(\.id) ?? []
    }
    func performDesignActionForVerification(_ module: HUDModule, _ id: String) {
        designCanvases[module]?.performAccessibilityAction(id)
    }
    /// A content-box point (px) → this view's point, through the live plane.
    func designBoxViewPointForVerification(_ point: CGPoint) -> CGPoint {
        projectDesignBoxRect(CGRect(origin: point, size: .zero)).origin
    }
    /// This view's point → content-box px through the live plane.
    func designBoxPointForVerification(_ point: CGPoint) -> CGPoint? { designBoxPoint(point) }
    /// Whether the map occlusion hides the centre at this view point; nil without it.
    func mapOcclusionHidesForVerification(_ point: CGPoint) -> Bool? {
        guard let mask = moduleContent.layer.mask as? CAShapeLayer, let path = mask.path,
              let core = coreDesignPoint(designPoint(point)) else { return nil }
        let host = moduleContent.layer.frame.origin
        return !path.contains(CGPoint(x: core.x - host.x, y: core.y - host.y), using: .evenOdd)
    }
    func centerPointForVerification(_ point: CGPoint) -> CGPoint? {
        sourceCenterProjection.map { HUDMotionMath.project(point, through: $0) }
    }
    var notesCanvasForVerification: NotesCanvas {
        precondition(CommandLine.arguments.contains("--ui-test")); return notesCanvas
    }
    var nowPlayingCanvasForVerification: NowPlayingCanvas {
        precondition(CommandLine.arguments.contains("--ui-test")); return nowPlayingCanvas
    }
    var archiveCanvasForVerification: ArchiveCanvas {
        precondition(CommandLine.arguments.contains("--ui-test")); return archiveCanvas
    }
    var mediaAssemblyCanvasForVerification: MediaAssemblyCanvas {
        precondition(CommandLine.arguments.contains("--ui-test")); return mediaAssemblyCanvas
    }
    var profileForVerification: UserProfile? {
        precondition(CommandLine.arguments.contains("--ui-test")); return profileStore?.profile
    }
    var accountCanvasForVerification: HUDAccountCanvas {
        precondition(CommandLine.arguments.contains("--ui-test")); return accountCanvas
    }
    var accountControllerForVerification: HypergryphAccountController {
        precondition(CommandLine.arguments.contains("--ui-test")); return accountController
    }
    func accountGaugePointForVerification(_ point: CGPoint) -> CGPoint {
        precondition(CommandLine.arguments.contains("--ui-test"))
        return accountGaugeRect(CGRect(origin: point, size: .zero)).origin
    }
    var accountGaugeForVerification: HUDAccountGauge {
        precondition(CommandLine.arguments.contains("--ui-test")); return accountGauge
    }
    var minigameCanvasForVerification: OrbiPomCanvas {
        precondition(CommandLine.arguments.contains("--ui-test")); return minigameCanvas
    }
    var calendarCanvasForVerification: HUDCalendarCanvas {
        precondition(CommandLine.arguments.contains("--ui-test")); return calendarCanvas
    }
    var readerCanvasForVerification: ReaderCanvas {
        precondition(CommandLine.arguments.contains("--ui-test")); return readerCanvas
    }
    var worldMapForVerification: WorldMapCanvas {
        precondition(CommandLine.arguments.contains("--ui-test")); return mapCanvas
    }
    var workFocusStatusMessage: String? { didSet { updateFocusStatus() } }
    var workFocusNeedsAccessibilityPermission = false { didSet { updateFocusStatus() } }
    var onRequestFocusAccess: (() -> Void)?
    private func updateFocusStatus() {
        workCanvas.setFocusStatusMessage(workFocusStatusMessage, needsAccessibilityPermission: workFocusNeedsAccessibilityPermission)
    }
    var interactionEnabled = false {
        didSet {
            modulePresentationAllowed = interactionEnabled
            updateButtonStates()
            if interactionEnabled { scheduleVisibleMotion(); refreshChargeHover(); headerClock.setActive(window != nil); hudDidBecomeInteractive() }
            else {
                deactivateModuleInput()
                facilityBanner.hide(animated: false)
                if motion.isPointerFollowing {
                    motion.startPointerFollowing(reducedMotion: HUDRuntimeAppearance.reduceMotion,
                                                 initialPoint: currentPointerTarget())
                }
                headerClock.setActive(false); clockStyleArtwork.resetGesture(); clockPage.cancelTransition(); layoutClockControls()
            }
        }
    }

    private static let designSize = CGSize(width: 1000, height: 640)
    private static let verticalLift: CGFloat = 30
    private let backdrop = CALayer()
    private let backgroundBlur = HUDBackgroundBlurView(frame: .zero)
    private let blurBackdrop = HUDBackgroundContainerView(frame: .zero)
    private let canvas = CALayer()
    private let vignette = CAGradientLayer()
    // The source shell replaces this entire tree. Construct it only if its
    // native fallback is needed, including its otherwise unused bitmap tints.
    private var artwork: HUDMechanicalArtwork?
    private let distantPlane = HUDDepthPlane(name: "distant", depth: -95, travel: -12, lag: 0.40)
    private let rearPlane = HUDDepthPlane(name: "rear", depth: -52, travel: -10, lag: 0.36)
    private let secondaryPlane = HUDDepthPlane(name: "secondary", depth: -24, travel: 3, lag: 0.32)
    private let framePlane = HUDDepthPlane(name: "frame", depth: 0, travel: 8, lag: 0.28)
    private let innerPlane = HUDDepthPlane(name: "inner", depth: 26, travel: 16, lag: 0.25)
    private let panelsPlane = HUDDepthPlane(name: "panels", depth: 36, travel: 16, lag: 0.27)
    private let corePlane = HUDDepthPlane(name: "core", depth: 54, travel: 24, lag: 0.22)
    private let markersPlane = HUDDepthPlane(name: "markers", depth: 68, travel: 28, lag: 0.20)
    private let glassPlane = HUDDepthPlane(name: "glass", depth: 86, travel: 32, lag: 0.18)
    // Match the central content's full foreground movement, rather than the
    // recessed well's restrained travel, while keeping the arcs drawn on top.
    private lazy var rimPlane = HUDDepthPlane(name: "rim", depth: corePlane.depth,
                                              travel: corePlane.travel, lag: corePlane.lag)
    // The badge shares the dial pose and follows its deployment transform.
    private lazy var chargePlane = HUDDepthPlane(name: "charge", depth: corePlane.depth,
                                                  travel: corePlane.travel, lag: corePlane.lag)
    // Above everything else on the HUD (the clock, the footer, the alert
    // banner and pinned notes): the HUD elements editor strip and the Sanity
    // recovery menu. Same pose as core, so both stay on the elements they edit.
    private lazy var topPlane = HUDDepthPlane(name: "top", depth: corePlane.depth,
                                              travel: corePlane.travel, lag: corePlane.lag)
    private var depthPlanes: [HUDDepthPlane] {
        [distantPlane, rearPlane, secondaryPlane, framePlane, innerPlane,
         panelsPlane, corePlane, markersPlane, glassPlane, rimPlane, chargePlane]
    }
    // Notes keep screen-sized persistent geometry, with the same camera pose
    // and response as the side buttons, outside the center's circular mask.
    private lazy var notesPlane = HUDDepthPlane(name: "notes", depth: panelsPlane.depth,
                                                travel: panelsPlane.travel, lag: panelsPlane.lag)
    /// Every plane the pointer and the source camera drive. The top plane
    /// stays out of `depthPlanes`, so it is never added to `canvas`.
    private var drivenPlanes: [HUDDepthPlane] { depthPlanes + [notesPlane, topPlane] }
    private lazy var motion = HUDMotionController(planes: drivenPlanes)
    private var ring: CALayer { framePlane.deployment }
    private let progress = CAShapeLayer()
    private let actionFeedback = CAShapeLayer()
    private var clickFeedbackMonitor: Any?
    private var settingsCaptureMonitor: Any?
    private(set) var clickFeedbackCountForVerification = 0
    private var motionPreferenceObserver: NSObjectProtocol?
    private let navigation = HUDNavigation(prepareArtwork: false)
    private let chargeBadge = HUDChargeBadge()
    func setChargeMetric(_ metric: HUDChargeMetric, telemetry: SystemActivitySnapshot?) {
        chargeBadge.setMetric(metric, telemetry: telemetry)
    }
    private let identityCard = HUDIdentityCard(prepareArtwork: false)
    private let closeHUDButton = HUDIdentityCloseButton(frame: .zero)
    private var moduleContent: HUDModuleContent!
    /// The module `prepareInitialModule` selects; the module host builds only its screen.
    private let launchModule: HUDModule
    /// The redesigned centre (spec §1): shared services and converted modules.
    let designContext: HUDDesignContext
    private let facilityAlerts: FacilityAlertMonitor?
    private var designCanvases: [HUDModule: HUDDesignCanvas] = [:]
    private lazy var designStandIns = HUDAccessibilityStandIns(host: self)
    private lazy var designOverlayStandIns = HUDAccessibilityStandIns(host: self)
    /// Content px → design units. The display adaptation is in the design scale.
    private var designUnit: CGFloat { HUDDesignGeometry.baseUnit }
    private let notesCanvas: NotesCanvas
    private let notesWorkspace = CALayer()
    private let notesLayout = CALayer()
    /// The top plane's screen placement and scale, as `notesLayout` keeps the notes'.
    private let topLayout = CALayer()
    private let notesCoordinates = CALayer()
    private let settingsController: HUDSettingsController?
    private var settingsCanvases: [HUDModule: HUDSettingsCanvas] = [:]
    private var settingsInteractions: [HUDModule: HUDSettingsInteraction] = [:]
    private var shelfNavigationDropTarget = false
    private var externalFileDragActive = false
    private var notesInteraction: HUDNotesInteraction?
    private let shelfCanvas: FileShelfCanvas
    private var shelfInteraction: HUDFileShelfInteraction?
    private let profileStore: UserProfileStore?
    private let profileCanvas: PersonalProfileCanvas
    private var profileObserver: UUID?
    private lazy var profileEventRecorder = SystemEventRecorder(log: eventLog)
    private let appShortcutStore: AppShortcutStore?
    private let appShortcutCanvas: AppShortcutCanvas
    private let clipboardCanvas: ClipboardCanvas
    private let eventLog: SystemEventLog
    private let eventLogCanvas: EventLogCanvas
    private let mapCanvas: WorldMapCanvas
    private let volumeCanvas: VolumeCanvas
    private let nowPlayingCanvas: NowPlayingCanvas
    private var modulePresentationAllowed = false
    private let archiveCanvas: ArchiveCanvas
    private let mediaAssemblyCanvas: MediaAssemblyCanvas
    private let accountController: HypergryphAccountController
    private let accountCanvas: HUDAccountCanvas
    private let accountGauge = HUDAccountGauge()
    /// The Sanity recovery menu's place on the top plane, kept on the gauge's
    /// position and transform (`applyOverlayLayout`).
    private let accountPopoverHost = CALayer()
    private var accountGaugeControls: [HUDNavigationScrollButton] = []
    private var accountObserver: UUID?
    private let minigameCanvas: OrbiPomCanvas
    private let calendarCanvas: HUDCalendarCanvas
    private let readerCanvas: ReaderCanvas
    private let storageCanvas: StorageCanvas
    private let activityCanvas: ActivityMonitorCanvas
    private let industryWordmark = CALayer()
    private var legacyWordmarkMask: CALayer?
    private var legacyLogoKey: String?
    private let workCanvas: WorkModeCanvas
    private let batteryCanvas: BatteryCanvas
    private let projectionCanvas: ProjectionCanvas
    private let workModeController: WorkModeController
    private let calendarController: HUDCalendarController
    private var workObserver: UUID?
    private var displayedWorkPhase: WorkModePhase?
    private var workBadge = HUDStandardTextLayer()
    var isPresentingModulePanel: Bool {
        accountController.isPresentingAccountPanel || notesInteraction?.isPresentingPanel == true || shelfInteraction?.isPresentingPanel == true
            || settingsInteractions.values.contains { $0.isPresentingPanel } || designCanvases.values.contains { $0.isPresentingPanel }
    }
    var isDraggingShelfItem: Bool { shelfInteraction?.isDraggingOut == true }
    private var isModuleInputLocked: Bool {
        overlayEditor != nil || notesInteraction?.isInputLocked == true || shelfInteraction?.isInputLocked == true
            || settingsInteractions.values.contains { $0.isInputLocked } || activeDesignCanvas?.isInputLocked == true
            || arrangeDrag?.lifted == true
    }
    /// The HUD's own controls outside the placement editor: they wait while
    /// HUD elements are placed, for VoiceOver as for the pointer (`hitTest`).
    private var allowsInputBesidePlacement: Bool { allowsModuleInput && overlayEditor == nil }
    /// The selected redesigned module while it accepts input.
    private var activeDesignCanvas: HUDDesignCanvas? {
        guard allowsModuleInput, !moduleContent.isTransitioning, let canvas = designCanvases[selectedModule], canvas.isActive else { return nil }
        return canvas
    }
    var summonedDuringFileDrag = false
    var isAwaitingFileDrop: Bool { (summonedDuringFileDrag || shelfNavigationDropTarget) && NSEvent.pressedMouseButtons & 1 != 0 }
    private var notesWorkspaceIsInteractive: Bool {
        allowsModuleInput && !moduleContent.isTransitioning
    }
    private var settingsInteraction: HUDSettingsInteraction? {
        guard allowsModuleInput, !moduleContent.isTransitioning else { return nil }
        return settingsInteractions[selectedModule]
    }
    /// An open menu, pop-over or confirm bar takes the next press anywhere (spec §1.12).
    private var popupCapturesPointer: Bool {
        overlayEditor != nil || accountGauge.isPopoverOpen || (notesWorkspaceIsInteractive && notesInteraction?.capturesPointer == true)
            || activeDesignCanvas?.capturesPointer == true || designContext.overlay.capturesInput
    }
    private var navigationButtons: [HUDNavigationTarget: HUDNavigationHitButton] = [:]
    private var navigationScrollButtons: [Int: HUDNavigationScrollButton] = [:]
    private(set) var selectedModule: HUDModule = .power
    private let header = CALayer()
    private let statusPanel = CALayer()
    private let statusPlate = CAShapeLayer()
    private let statusFrame = CAShapeLayer()
    private let statusUnderline = CAShapeLayer()
    private var sourceStatusProjection: CATransform3D?
    private let headerClock = HUDClock()
    private let clockStyleArtwork = HUDClockStyleArtwork()
    private let clockPage = HUDClockPageViewport()
    private var clockControls: [HUDClockActionButton] = []
    private var clockHovered = false
    private var clockTime = HUDStandardTextLayer()
    private var clockDate = HUDStandardTextLayer()
    private let footer = CALayer()
    private var primaryTexts: [CATextLayer] = []
    private var mutedTexts: [CATextLayer] = []
    private var accentTexts: [CATextLayer] = []
    private var titleLabel = HUDStandardTextLayer()
    private var subtitleLabel = HUDStandardTextLayer()
    private var hintLabel = HUDStandardTextLayer()
    private var snapshot = BatterySnapshot.unavailable
    private var configuration = AppConfiguration.defaults
    private var currentAccent = NSColor.systemYellow
    private var currentBatteryTone = NSColor.systemGreen
    private var currentDark = true
    private var generation = 0
    private var transitioning = false
    private var retracting = false
    private var transitionCompletion: (() -> Void)?
    private var tracking: NSTrackingArea?
    private var cursorTracking: NSTrackingArea?
    private var designScale: CGFloat = 1
    /// Backing scale x design scale: the pixels per design point of the canvas.
    private var hudRasterScale: CGFloat { (window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2) * designScale }
    private var designOrigin = CGPoint.zero
    private var notesLayoutSize = CGSize.zero

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    override convenience init(frame frameRect: NSRect) {
        self.init(frame: frameRect,
                  notesStore: Result { try NotesStore(directory: NotesStore.applicationDirectory()) },
                  shelfStore: Result { try FileShelfStore(directory: FileShelfStore.applicationDirectory()) },
                  clipboard: ClipboardWatcher(store: ClipboardStore(), pasteboard: .withUniqueName()),
                  audio: .fixture(), perAppAudio: .fixture(), workMode: WorkModeController())
    }

    init(frame frameRect: NSRect, notesStore: Result<NotesStore, Error>, shelfStore: Result<FileShelfStore, Error>,
         clipboard: ClipboardWatcher, audio: AudioDeviceController, perAppAudio: PerAppAudioController,
         workMode: WorkModeController, eventLog: SystemEventLog = SystemEventLog(), nowPlaying: NowPlayingController = .fixture(),
         storage: StorageController = .fixture(), activity: SystemActivityMonitor = .fixture(), appActivity: AppActivityMonitor = .fixture(),
         appShortcuts: Result<AppShortcutStore, Error> = Result { try AppShortcutStore(directory: AppShortcutStore.applicationDirectory()) },
         settings: HUDSettingsController? = nil,
         profile: Result<UserProfileStore, Error> = Result { try UserProfileStore(directory: UserProfileStore.applicationDirectory()) },
         mapStore: Result<WorldMapStore, Error> = Result { try WorldMapStore(directory: WorldMapStore.applicationDirectory()) },
         archive: ArchiveController = ArchiveController(store: ArchiveStore(directory: ArchiveStore.applicationDirectory())),
         reader: ReaderController = ReaderController(loadStore: { try ReaderStore(directory: ReaderStore.applicationDirectory()) }),
         mediaAssembly: MediaAssemblyController = MediaAssemblyController(), calendar: HUDCalendarController = .fixture(),
         minigame: OrbiPomSession = OrbiPomSession(), account: HypergryphAccountController? = nil,
         facility: FacilityAlertMonitor? = nil, initialConfiguration: AppConfiguration = .defaults,
         initialSnapshot: BatterySnapshot = .unavailable, initialModule: HUDModule = .power) {
        var startupPhase = HUDStartupTrace.begin()
        launchModule = initialModule
        let context = HUDDesignContext()
        designContext = context
        facilityAlerts = facility
        accountCanvas = HUDAccountCanvas(context: context)
        batteryCanvas = BatteryCanvas(context: context)
        projectionCanvas = ProjectionCanvas(context: context)
        configuration = initialConfiguration.normalized
        snapshot = initialSnapshot
        HUDRuntimeAppearance.configuration = configuration
        // The context's shared overlay was built before Theme and the theme colour applied.
        context.refreshPalette()
        switch mapStore {
        case .success(let store): mapCanvas = WorldMapCanvas(context: context, store: store)
        case .failure(let error): mapCanvas = WorldMapCanvas(context: context, store: nil, error: error.localizedDescription)
        }
        self.profileStore = try? profile.get()
        profileStore?.hidesSystemAccount = configuration.recordingMode
        self.accountController = account ?? HypergryphAccountController(profile: try? profile.get())
        accountController.showsHeaderGauge = configuration.overlayLayout.showsSanity
        switch profile {
        case .success(let store): profileCanvas = PersonalProfileCanvas(context: context, store: store, workSeconds: { workMode.trackedWorkSeconds },
                                                                         minigameBest: { minigame.bestSoFar })
        case .failure(let error): profileCanvas = PersonalProfileCanvas(context: context, store: nil, error: error.localizedDescription,
                                                                        workSeconds: { workMode.trackedWorkSeconds }, minigameBest: { minigame.bestSoFar })
        }
        settingsController = settings
        self.eventLog = eventLog
        self.appShortcutStore = try? appShortcuts.get()
        archiveCanvas = ArchiveCanvas(context: context, controller: archive)
        readerCanvas = ReaderCanvas(context: context, controller: reader)
        mediaAssemblyCanvas = MediaAssemblyCanvas(context: context, controller: mediaAssembly)
        calendarCanvas = HUDCalendarCanvas(context: context, controller: calendar)
        minigameCanvas = OrbiPomCanvas(context: context, session: minigame)
        storageCanvas = StorageCanvas(context: context, controller: storage)
        activityCanvas = ActivityMonitorCanvas(context: context, controller: activity, apps: appActivity)
        eventLogCanvas = EventLogCanvas(context: context, store: eventLog)
        switch notesStore {
        case .success(let store): notesCanvas = NotesCanvas(context: context, store: store, notesSelected: initialModule == .notes)
        case .failure(let error): notesCanvas = NotesCanvas(context: context, store: nil, error: error.localizedDescription, notesSelected: initialModule == .notes)
        }
        switch shelfStore {
        case .success(let store): shelfCanvas = FileShelfCanvas(context: context, store: store)
        case .failure(let error): shelfCanvas = FileShelfCanvas(context: context, store: nil, error: error.localizedDescription)
        }
        switch appShortcuts {
        case .success(let store): appShortcutCanvas = AppShortcutCanvas(context: context, store: store)
        case .failure(let error): appShortcutCanvas = AppShortcutCanvas(context: context, store: nil, error: error.localizedDescription)
        }
        clipboardCanvas = ClipboardCanvas(context: context, store: clipboard.store)
        clipboardCanvas.onCopy = { id in
            let kind = clipboard.store.items.first { $0.id == id }?.kind.rawValue
            guard clipboard.copy(id: id) else { return false }
            eventLog.record(kind: .clipboardCopied, metadata: kind.map { ["kind": $0] } ?? [:])
            return true
        }
        shelfCanvas.onItemsAdded = { names in
            for name in names { eventLog.record(kind: .shelfAdded, metadata: ["filename": name]) }
        }
        shelfCanvas.onItemRemoved = { eventLog.record(kind: .shelfRemoved, metadata: ["filename": $0]) }
        shelfCanvas.onShelfCleared = { eventLog.record(kind: .shelfCleared, metadata: ["count": String($0)]) }
        volumeCanvas = VolumeCanvas(context: context, controller: audio, perAppAudio: perAppAudio)
        nowPlayingCanvas = NowPlayingCanvas(context: context, controller: nowPlaying, audio: audio, perAppAudio: perAppAudio)
        workModeController = workMode
        calendarController = calendar
        workCanvas = WorkModeCanvas(context: context, controller: workMode)
        HUDStartupTrace.end("native.canvases", since: &startupPhase)
        super.init(frame: frameRect)
        wantsLayer = true
        backgroundBlur.material = .hudWindow
        backgroundBlur.blendingMode = .behindWindow
        backgroundBlur.state = .active
        backgroundBlur.wantsLayer = true
        backgroundBlur.alphaValue = 0
        blurBackdrop.wantsLayer = true
        blurBackdrop.layer?.zPosition = -100
        blurBackdrop.layer?.opacity = 0
        blurBackdrop.addSubview(backgroundBlur)
        addSubview(blurBackdrop, positioned: .below, relativeTo: nil)
        layer?.addSublayer(backdrop)
        layer?.addSublayer(canvas)
        canvas.bounds = CGRect(origin: .zero, size: Self.designSize)
        canvas.isGeometryFlipped = false // AppKit already flips the backing layer.
        buildDepthLayers()
        buildRing()
        buildPanels()
        buildNavigation()
        notesWorkspace.name = "hud.notesWorkspace"
        // Keep screen placement/scale outside the deployment transform, just
        // as canvas does for the other planes. Retraction must not replace the
        // coordinate mapping of freely positioned notes.
        notesLayout.name = "notes.layout"
        notesLayout.bounds = canvas.bounds
        notesCoordinates.name = "notes.screenCoordinates"
        notesCoordinates.anchorPoint = .zero
        notesCoordinates.addSublayer(notesCanvas.workspaceLayer)
        notesPlane.content.addSublayer(notesCoordinates)
        notesLayout.addSublayer(notesPlane.deployment)
        notesWorkspace.addSublayer(notesLayout)
        layer?.addSublayer(notesWorkspace)
        topLayout.name = "hud.top.layout"
        topLayout.bounds = canvas.bounds
        topLayout.addSublayer(topPlane.deployment)
        topLayout.zPosition = 3_500_000
        layer?.addSublayer(topLayout)
        // Root order: canvas and pinned notes (0) < the top plane (3,500,000)
        // < pointer feedback (4,000,000) < the rendered cursor (5,000,000).
        // Pointer feedback is screen-aligned and above all projected content,
        // including native inline text editors, freely positioned notes, the
        // HUD elements strip and the recovery menu.
        actionFeedback.zPosition = 4_000_000
        layer?.addSublayer(actionFeedback)
        HUDStartupTrace.end("native.shell", since: &startupPhase)
        configureNotesInteraction(shelf: try? shelfStore.get())
        configureShelfInteraction(store: try? shelfStore.get())
        configureModuleCallbacks(shelf: try? shelfStore.get())
        if let profile = profileStore?.profile {
            profileEventRecorder.receiveProfileCrop(backgroundZoom: profile.backgroundZoom, thumbnailZoom: profile.thumbnailZoom)
        }
        profileObserver = profileStore?.observe { [weak self] in
            guard let self, let profile = self.profileStore?.profile else { return }
            self.profileEventRecorder.receiveProfileCrop(backgroundZoom: profile.backgroundZoom, thumbnailZoom: profile.thumbnailZoom)
            self.refreshIdentityProfile()
        }
        configureSettingsInteraction()
        workObserver = workMode.observe { [weak self] in self?.updateWorkPresentation() }
        registerForDraggedTypes([.fileURL, .png, .tiff])
        setAccessibilityElement(false)
        motionPreferenceObserver = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.accessibilityDisplayOptionsDidChangeNotification,
            object: nil, queue: .main) { [weak self] _ in
                guard let self = self else { return }
                self.notesInteraction?.finishEditing()
                self.notesInteraction?.mouseUp()
                self.workCanvas.refreshMotionPreference()
                self.motion.stop(freezePresentation: false)
                if HUDRuntimeAppearance.reduceMotion {
                    self.moduleContent?.settle()
                    self.navigation.cancelAnimations()
                    self.artwork?.frame.removeAnimation(forKey: "section.index")
                    self.selectedModule = self.moduleContent?.selectedModule ?? .power
                    self.navigation.select(self.selectedModule, animated: false)
                    self.updateContent()
                }
                if HUDRuntimeAppearance.reduceMotion, self.transitioning {
                    let completion = self.transitionCompletion
                    let wasRetracting = self.retracting
                    self.cancelAnimations()
                    if wasRetracting {
                        self.sourceOverviewPresented = false
                        self.sourceWatch?.conceal()
                        self.updateSourceOverviewPresentation(stable: false)
                        self.withoutActions { self.canvas.opacity = 0; self.backdrop.opacity = 0; self.blurBackdrop.layer?.opacity = 0; self.notesWorkspace.opacity = 0 }
                    } else { self.showStable() }
                    completion?()
                }
                self.scheduleVisibleMotion()
            }
        HUDStartupTrace.end("native.interactions", since: &startupPhase)
        headerClock.setFormat(configuration.clockFormat)
        motion.configure(parallax: CGFloat(configuration.parallaxIntensity),
                         perspective: CGFloat(configuration.perspectiveIntensity),
                         ambient: HUDRuntimeAppearance.ambientEnabled)
        // Install the configured appearance once, then build only the requested
        // center. Production no longer constructs with defaults and repaints it
        // immediately before the first frame can be presented.
        notesCanvas.setPresentation(notesSelected: initialModule == .notes, animated: false)
        updateContent()
        HUDStartupTrace.end("native.appearance", since: &startupPhase)
        configureSourceWatch()
        HUDStartupTrace.end("source.view", since: &startupPhase)
        prepareInitialModule(initialModule)
        HUDStartupTrace.end("native.initialModule", since: &startupPhase)
        withoutActions { self.backdrop.opacity = 0; self.canvas.opacity = 0 }
    }

    deinit {
        if let clickFeedbackMonitor { NSEvent.removeMonitor(clickFeedbackMonitor) }
        if let settingsCaptureMonitor { NSEvent.removeMonitor(settingsCaptureMonitor) }
        if let workObserver { workModeController.removeObserver(workObserver) }
        if let profileObserver { profileStore?.removeObserver(profileObserver) }
        if let accountObserver { accountController.removeObserver(accountObserver) }
        accountController.setVisible(false, accountModule: false)
        if let observer = motionPreferenceObserver { NSWorkspace.shared.notificationCenter.removeObserver(observer) }
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override func layout() {
        super.layout()
        sourceWatch?.frame = bounds
        sourceWatchFailure?.frame = CGRect(x: max(24, (bounds.width - 640) / 2), y: max(24, (bounds.height - 90) / 2),
                                          width: max(1, min(640, bounds.width - 48)), height: 90)
        if notesLayoutSize != bounds.size {
            notesInteraction?.finishEditing()
            notesInteraction?.mouseUp()
            notesLayoutSize = bounds.size
        }
        withoutActions {
            self.backdrop.frame = self.bounds
            self.blurBackdrop.frame = self.bounds
            self.backgroundBlur.frame = self.blurBackdrop.bounds
            self.notesWorkspace.frame = self.bounds
            self.notesCanvas.setWorkspaceBounds(self.bounds,
                creationPoint: CGPoint(x: self.bounds.midX - 100, y: self.bounds.midY - 120))
            self.vignette.frame = self.backdrop.bounds
            // As on Windows, the design canvas keeps its share of the source shell
            // on displays where the capped scale would leave it too small (checklist 13).
            let adaptation = self.usesSourceShell ? self.sourceWatch?.desktopContentScale(for: self.bounds.size) ?? 1 : 1
            self.designScale = HUDSourceWatchView.baseDesignScale(self.bounds.size) * CGFloat(self.configuration.hudScale) * adaptation
            self.designOrigin = CGPoint(x: (self.bounds.width - 1000 * self.designScale) / 2,
                                        y: (self.bounds.height - 640 * self.designScale) / 2 - Self.verticalLift * self.designScale)
            let offset = CGPoint(x: self.bounds.width * CGFloat(self.configuration.hudOffsetX),
                                 y: self.bounds.height * CGFloat(self.configuration.hudOffsetY))
            self.designOrigin.x += offset.x
            self.designOrigin.y += offset.y
            self.canvas.position = CGPoint(x: self.bounds.midX + offset.x,
                                           y: self.bounds.midY - Self.verticalLift * self.designScale + offset.y)
            self.canvas.setAffineTransform(CGAffineTransform(scaleX: self.designScale, y: self.designScale))
            for layout in [self.notesLayout, self.topLayout] {
                layout.position = self.canvas.position
                layout.setAffineTransform(CGAffineTransform(scaleX: self.designScale, y: self.designScale))
            }
            // Convert saved screen points into the shared design space before
            // applying perspective; note sizes stay independent of HUD scale.
            self.notesCoordinates.bounds = self.bounds
            self.notesCoordinates.position = CGPoint(x: -self.designOrigin.x / self.designScale,
                                                       y: -self.designOrigin.y / self.designScale)
            self.notesCoordinates.setAffineTransform(CGAffineTransform(scaleX: 1 / self.designScale,
                                                                        y: 1 / self.designScale))
            // Screen-sized notes extend beyond the shell in design space at
            // small UI scales. Bound their own lens without reducing their
            // matching side-button angles, travel or response speed.
            let notesProjectionBounds = CGRect(
                x: (self.bounds.minX - self.designOrigin.x) / self.designScale - 500,
                y: (self.bounds.minY - self.designOrigin.y) / self.designScale - 320,
                width: self.bounds.width / self.designScale,
                height: self.bounds.height / self.designScale)
            self.motion.setProjectionBounds(notesProjectionBounds, for: self.notesPlane)
            self.hintLabel.frame.origin.y = (self.bounds.height - self.designOrigin.y - 16) / self.designScale - self.hintLabel.bounds.height
            self.updateNavigationGeometry()
            self.updateContentsScale()
            if let projection = self.sourceCenterProjection { self.applySourceCenterProjection(projection) }
            if let projection = self.sourceStatusProjection { self.applySourceStatusProjection(projection) }
        }
        notesInteraction?.layoutAccessibility()
        layoutDesignAccessibility()
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        needsLayout = true
        updateContentsScale()
        if window == nil {
            removeClickFeedbackMonitor()
            removeSettingsCaptureMonitor()
            cancelAnimations()
            sourceWatch?.conceal()
            sourceOverviewPresented = false
        } else {
            installClickFeedbackMonitor()
            installSettingsCaptureMonitor()
            if interactionEnabled { scheduleVisibleMotion(); headerClock.setActive(true) }
        }
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        notesInteraction?.finishEditing()
        notesInteraction?.mouseUp()
        updateContentsScale()
    }

    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        notesInteraction?.finishEditing()
        // Theme = System: the redesigned pages follow macOS appearance too.
        if HUDRuntimeAppearance.applyPalette() {
            designContext.refreshPalette()
            if overlayEditor != nil, let draft = overlayDraft {
                installOverlayEditor(for: draft); layoutOverlayEditor(); layoutDesignAccessibility()
            }
        }
        updateContent()
    }

    func set(snapshot: BatterySnapshot, configuration: AppConfiguration) {
        let old = self.configuration
        let previousReduceMotion = HUDRuntimeAppearance.reduceMotion
        self.snapshot = snapshot
        self.configuration = configuration
        batteryCanvas.update(snapshot: snapshot, configuration: configuration)
        // Recording mode also swaps the macOS account's name and picture for
        // the defaults; the store's observers re-render every card.
        profileStore?.hidesSystemAccount = configuration.recordingMode
        designContext.setRecordingMode(configuration.recordingMode)
        sourceWatch?.desktopUIDRedacted = configuration.recordingMode
        identityCard.uidRedacted = configuration.recordingMode
        if old.language != configuration.language { facilityChanged() }
        headerClock.setFormat(configuration.clockFormat)
        HUDRuntimeAppearance.configuration = configuration
        designContext.refreshPalette()
        sourceWatch?.refreshMotionPreferences()
        sourceWatch?.setDesktopCenterLogo(configuration.centerLogo, revision: configuration.centerLogoRevision)
        // Display > Modules: the right-hand list rebinds its recycled rows in place.
        if old.moduleLayout != configuration.moduleLayout { refreshAppNavigation(animated: true) }
        // Display > HUD elements: relaid only when the layout changes.
        if old.overlayLayout != configuration.overlayLayout { applyOverlayLayout() }
        if accountController.showsHeaderGauge != configuration.overlayLayout.showsSanity {
            accountController.showsHeaderGauge = configuration.overlayLayout.showsSanity
            updateAccountGauge()
        }
        let scaleChanged = old.hudScale != configuration.hudScale
        let layoutChanged = scaleChanged || old.hudOffsetX != configuration.hudOffsetX || old.hudOffsetY != configuration.hudOffsetY
        let previousTransform = canvas.presentation()?.transform ?? canvas.transform
        let previousPosition = canvas.presentation()?.position ?? canvas.position
        motion.configure(parallax: CGFloat(configuration.parallaxIntensity),
                         perspective: CGFloat(configuration.perspectiveIntensity),
                         ambient: HUDRuntimeAppearance.ambientEnabled)
        if previousReduceMotion != HUDRuntimeAppearance.reduceMotion {
            motion.stop(freezePresentation: false)
            moduleContent?.settle()
            scheduleVisibleMotion()
        }
        if layoutChanged { needsLayout = true; layoutSubtreeIfNeeded() }
        if old.parallaxIntensity != configuration.parallaxIntensity || old.perspectiveIntensity != configuration.perspectiveIntensity {
            updateNavigationGeometry()
        }
        updateContent()
        if HUDRuntimeAppearance.reduceMotion || !interactionEnabled || !usesSourceShell { clockPage.cancelTransition() }
        if old.clockStyle != configuration.clockStyle {
            let forward = (configuration.clockStyle.index - old.clockStyle.index + 5) % 5 <= 2
            clockPage.transition(forward: forward,
                animated: usesSourceShell && interactionEnabled && !HUDRuntimeAppearance.reduceMotion)
        }
        if old.language != configuration.language {
            sourceWatch?.refreshDesktopLanguage()
            HUDStandardTextLayer.languageChanged(in: layer ?? canvas)
            refreshIdentityProfile()
            applyOverlayLayout() // The title's face, and so its width, follows the language.
        }
        if layoutChanged, interactionEnabled, !HUDRuntimeAppearance.reduceMotion {
            animate(canvas, "transform", from: previousTransform, to: canvas.transform, duration: 0.2)
            let movement = CABasicAnimation(keyPath: "position")
            movement.fromValue = NSValue(point: previousPosition); movement.toValue = NSValue(point: canvas.position)
            movement.duration = 0.2; movement.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            canvas.add(movement, forKey: "settings.scalePosition")
        }
    }

    /// Display selection moves the existing panel and retains the current
    /// section. A short arrival cue does not restart deployment or ambient tracks.
    func animateDisplayArrival() {
        needsLayout = true
        layoutSubtreeIfNeeded()
        guard allowsModuleInput, !HUDRuntimeAppearance.reduceMotion else { return }
        let fade = CABasicAnimation(keyPath: "opacity")
        fade.fromValue = 0.45; fade.toValue = 1; fade.duration = 0.18
        fade.timingFunction = CAMediaTimingFunction(name: .easeOut)
        canvas.add(fade, forKey: "settings.displayArrival")
    }

    /// Static model pose, also used for detached previews. Ambient motion starts
    /// only once the controller has enabled interaction on an attached view.
    func showStable(preservingChargeAnimation: Bool = false, preservingPointerMotion: Bool = false,
                    preparingSourceEntrance: Bool = false, preservingNowPlayingPresentation: Bool = false) {
        cancelAnimations(preservingChargeAnimation: preservingChargeAnimation, preservingPointerMotion: preservingPointerMotion,
                         preservingNowPlayingPresentation: preservingNowPlayingPresentation, preservingCursor: true)
        if interactionEnabled { modulePresentationAllowed = true }
        sourceOverviewPresented = true
        headerClock.setActive(window != nil)
        retracting = false
        if !preservingPointerMotion { motion.stop(freezePresentation: false) }
        layoutSubtreeIfNeeded()
        let pointer = currentPointerTarget()
        withoutActions {
            self.backdrop.opacity = 1
            self.blurBackdrop.layer?.opacity = 1
            self.canvas.opacity = 1
            self.notesWorkspace.opacity = 1
            self.notesWorkspace.transform = CATransform3DIdentity
            self.notesPlane.deployment.opacity = 1
            self.notesPlane.deployment.transform = CATransform3DIdentity
            for plane in self.depthPlanes + [self.topPlane] {
                plane.deployment.opacity = 1
                plane.deployment.transform = CATransform3DIdentity
                if !preservingPointerMotion {
                    plane.spatial.transform = HUDMotionMath.transform(normalizedPoint: pointer,
                        depth: plane.depth, travel: plane.travel,
                        reducedMotion: HUDRuntimeAppearance.reduceMotion,
                        parallaxIntensity: CGFloat(self.configuration.parallaxIntensity),
                        perspectiveIntensity: CGFloat(self.configuration.perspectiveIntensity))
                }
            }
            if !preservingPointerMotion {
                self.notesPlane.spatial.transform = HUDMotionMath.transform(normalizedPoint: pointer,
                    depth: self.notesPlane.depth, travel: self.notesPlane.travel,
                    reducedMotion: HUDRuntimeAppearance.reduceMotion,
                    parallaxIntensity: CGFloat(self.configuration.parallaxIntensity),
                    perspectiveIntensity: CGFloat(self.configuration.perspectiveIntensity),
                    projectionBounds: self.notesPlane.projectionBounds)
            }
            for item in self.contentGroups { item.opacity = 1; item.transform = CATransform3DIdentity }
        }
        if !preservingChargeAnimation { chargeBadge.setStable() }
        updateSourceOverviewPresentation(stable: !preparingSourceEntrance)
        updateButtonStates()
        if let projection = sourceCenterProjection { applySourceCenterProjection(projection) }
    }

    func animateEntrance(ready: @escaping () -> Void = {}, completion: @escaping () -> Void = {}) {
        // Prepare native planes without first building an invisible, fully
        // deployed Metal scene that open() immediately discards again.
        showStable(preparingSourceEntrance: usesSourceShell)
        // The first module of a session enters from the disc centre as the HUD
        // deploys (spec §1.10); today's boot log keeps the centre hidden instead.
        if !playsBootLogOnOpen {
            designCanvases[selectedModule]?.playEntrance(from: HUDShellPosition.entranceOffset(from: nil, to: selectedModule))
        }
        // Start metadata alongside deployment rather than after its completion.
        // The separate input bridge is still disabled throughout the reveal.
        modulePresentationAllowed = true
        nowPlayingCanvas.setPresented(window != nil && selectedModule == .nowPlaying)
        mediaAssemblyCanvas.setPresented(window != nil && selectedModule == .mediaAssembly)
        calendarCanvas.setPresented(window != nil && selectedModule == .calendar)
        if usesSourceShell {
            animateSourceEntrance(ready: ready, completion: completion)
            return
        }
        ready()
        motion.startPointerFollowing(reducedMotion: HUDRuntimeAppearance.reduceMotion, initialPoint: currentPointerTarget())
        // Start geography while the mechanical shell deploys. Input and pin
        // animation stay disabled until opening completes; raster work is
        // cancelled normally if the presentation is interrupted.
        if selectedModule == .map { mapCanvas.prepareForPresentation() }
        chargeBadge.animateEntrance()
        guard !HUDRuntimeAppearance.reduceMotion else {
            let token = generation
            DispatchQueue.main.async { [weak self] in
                guard self?.generation == token else { return }
                completion()
            }
            return
        }
        transitioning = true
        retracting = false
        transitionCompletion = completion
        navigationButtons.values.forEach { $0.isEnabled = false }
        let token = generation
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self] in
            guard let self = self, self.generation == token else { return }
            self.transitioning = false
            self.transitionCompletion = nil
            self.updateButtonStates()
            completion()
        }
        animate(backdrop, "opacity", from: 0, to: 1, duration: 0.25)
        animateBlur(from: 0, to: 1, duration: 0.45)
        // The reference first widens an inclined wireframe, then lifts it.
        // Hold the ellipse through the first part instead of easing immediately
        // to a face-on circle before the central content has appeared.
        let hardware: [(HUDDepthPlane, Double, Double, CGFloat)] = [
            (distantPlane, 0, 0.42, -100), (rearPlane, 0.015, 0.43, -90),
            (secondaryPlane, 0.035, 0.43, -70), (framePlane, 0.045, 0.43, -50),
            (innerPlane, 0.075, 0.41, -30), (markersPlane, 0.065, 0.43, 40),
            (rimPlane, 0.035, 0.43, -70)
        ]
        for (plane, delay, duration, depth) in hardware {
            animate(plane.deployment, "opacity", from: 0, to: 1, duration: 0.09, delay: delay)
            animateLift(plane.deployment, depth: depth, duration: duration, delay: delay)
        }
        for (plane, delay, duration, scale, depth) in [
            (panelsPlane, 0.19, 0.30, CGFloat(0.90), CGFloat(-35)),
            (corePlane, 0.32, 0.21, CGFloat(0.82), CGFloat(30)),
            (glassPlane, 0.34, 0.21, CGFloat(0.90), CGFloat(55))
        ] {
            animate(plane.deployment, "opacity", from: 0, to: 1, duration: 0.15, delay: delay)
            animate(plane.deployment, "transform", from: Self.tiltedTransform(scale: scale, depth: depth, degrees: 30),
                    to: CATransform3DIdentity, duration: duration, delay: delay)
        }
        enter(header, delay: 0.25, duration: 0.16)
        enter(navigation.layer, delay: 0.23, duration: 0.28)
        enter(footer, delay: 0.39, duration: 0.16)
        enter(notesWorkspace, delay: 0.32, duration: 0.21)
        addDeploymentFlicker(opening: true)
        CATransaction.commit()
    }

    func animateExit(completion: @escaping () -> Void = {}) {
        if usesSourceShell {
            animateSourceExit(completion: completion)
            return
        }
        let blurOpacity = blurBackdrop.layer?.presentation()?.opacity ?? blurBackdrop.layer?.opacity ?? 0
        // Freeze ambient rotations, but keep event-driven pointer response on
        // the spatial layers while the separate deployment wrappers retract.
        cancelAnimations(preserveClickFeedback: true, preservingPointerMotion: true)
        transitioning = true
        retracting = true
        transitionCompletion = completion
        navigationButtons.values.forEach { $0.isEnabled = false }
        let token = generation
        guard !HUDRuntimeAppearance.reduceMotion else {
            chargeBadge.setStable(visible: false)
            withoutActions { self.canvas.opacity = 0; self.backdrop.opacity = 0; self.blurBackdrop.layer?.opacity = 0; self.notesWorkspace.opacity = 0 }
            transitioning = false
            transitionCompletion = nil
            DispatchQueue.main.async { [weak self] in
                guard self?.generation == token else { return }
                completion()
            }
            return
        }
        var shellFinished = false
        var chargeFinished = false
        let finish: () -> Void = { [weak self] in
            guard let self, self.generation == token, shellFinished, chargeFinished else { return }
            self.transitioning = false
            self.transitionCompletion = nil
            completion()
        }
        chargeBadge.animateExit { chargeFinished = true; finish() }
        CATransaction.begin()
        CATransaction.setCompletionBlock { shellFinished = true; finish() }
        // Determine the sweep while the scene still has its settled geometry.
        addDeploymentFlicker(opening: false)
        let retraction: [(HUDDepthPlane, Double, Double, CGFloat)] = [
            (glassPlane, 0.12, 0.15, 0.55), (markersPlane, 0.035, 0.18, 0.35),
            (corePlane, 0.11, 0.20, 0.65),
            (panelsPlane, 0.15, 0.20, 0.88), (notesPlane, 0.15, 0.20, 0.88),
            (innerPlane, 0.065, 0.25, 0.12), (framePlane, 0.075, 0.325, 0.08),
            (secondaryPlane, 0.09, 0.30, 0.08), (rimPlane, 0.09, 0.30, 0.08), (rearPlane, 0.11, 0.29, 0.08),
            (distantPlane, 0.11, 0.29, 0.60)
        ]
        for (plane, delay, duration, scale) in retraction {
            let item = plane.deployment
            let oldTransform = item.presentation()?.transform ?? item.transform
            let oldOpacity = item.presentation()?.opacity ?? item.opacity
            // Notes already carry their bounded pointer perspective. They can
            // span the entire screen, far beyond the dial at small HUD scales;
            // a second lens during the steep fold could cross its near plane.
            let foldPerspective = plane !== notesPlane
            let target = Self.tiltedTransform(scale: scale, depth: -50, perspective: foldPerspective)
            withoutActions { item.transform = target; item.opacity = 0 }
            animateRetraction(item, from: oldTransform, to: target, duration: duration, delay: delay,
                              perspective: foldPerspective)
            let fade = CAKeyframeAnimation(keyPath: "opacity")
            fade.values = [oldOpacity, oldOpacity, 0]
            fade.keyTimes = [0, 0.45, 1]
            // Individual children own the bottom-to-top shutoff. Keep their
            // parent visible until that sweep has finished, including artwork
            // whose mechanical fold completes earlier.
            fade.duration = 0.06
            fade.beginTime = item.convertTime(CACurrentMediaTime(), from: nil) + 0.35
            fade.fillMode = .backwards
            fade.timingFunctions = [CAMediaTimingFunction(name: .linear), CAMediaTimingFunction(name: .easeInEaseOut)]
            item.add(fade, forKey: "deployment.exitOpacity")
        }
        // Keep the independent circle morph, but carry it inward with the main
        // dial instead of leaving a stationary badge at the old screen position.
        let chargeDeployment = chargePlane.deployment
        let chargeStart = chargeDeployment.presentation()?.transform ?? chargeDeployment.transform
        let chargeTarget = Self.tiltedTransform(scale: 0.08, depth: -50)
        withoutActions { chargeDeployment.transform = chargeTarget }
        animateRetraction(chargeDeployment, from: chargeStart, to: chargeTarget,
                          duration: 0.325, delay: 0.075)
        for item in [header, footer] {
            let opacity = item.presentation()?.opacity ?? item.opacity
            withoutActions { item.opacity = 0 }
            animate(item, "opacity", from: opacity, to: 0, duration: 0.06, delay: 0.35)
        }
        let backdropOpacity = backdrop.presentation()?.opacity ?? backdrop.opacity
        withoutActions { self.backdrop.opacity = 0; self.blurBackdrop.layer?.opacity = 0 }
        animate(backdrop, "opacity", from: backdropOpacity, to: 0, duration: 0.16, delay: Self.exitDuration - 0.16)
        animateBlur(from: blurOpacity, to: 0, duration: Self.exitDuration)
        CATransaction.commit()
    }

    private func addDeploymentFlicker(opening: Bool) {
        guard !HUDRuntimeAppearance.reduceMotion else { return }
        // A directional wave carries irregular local dropouts through the
        // scene. Backdrop/blur remain smooth; ambient geometry stays intact.
        let machinery = (artwork?.groups.flatMap { $0.sublayers ?? [] } ?? []) + [progress]
        let cards = navigation.visibleEntries.filter { $0.module != .power && $0.module != .profile }.map(\.layer)
            + (identityCard.layer.sublayers ?? [])
        let readouts = moduleContent.layer.sublayers?.flatMap { wrapper in
            (wrapper.sublayers ?? []).flatMap { $0.sublayers ?? [] }
        } ?? []
        let lettering = (header.sublayers ?? []) + (footer.sublayers ?? []) + [industryWordmark]
        let notes = notesCanvas.deploymentLayers
        HUDDeploymentFlicker.applySweep(to: machinery + cards + readouts + lettering + notes,
                                       in: canvas, opening: opening,
                                       duration: opening ? 0.13 : 0.11,
                                       delay: opening ? 0.20 : 0.01,
                                       span: opening ? 0.25 : 0.23)
    }

    private func scheduleVisibleMotion() {
        if usesSourceShell {
            motion.stop(freezePresentation: false)
            motion.startPointerFollowing(reducedMotion: HUDRuntimeAppearance.reduceMotion, initialPoint: currentPointerTarget())
            headerClock.setActive(window != nil && interactionEnabled)
            sourceWatch?.inputEnabled = allowsInputBesidePlacement
            sourceWatch?.refreshMotionPreferences()
            return
        }
        if transitioning, window != nil {
            motion.startPointerFollowing(reducedMotion: HUDRuntimeAppearance.reduceMotion,
                                         initialPoint: currentPointerTarget())
        }
        let token = generation
        // Leave the finite opening transaction before installing infinite tracks.
        DispatchQueue.main.async { [weak self] in
            guard let self = self, self.generation == token, self.window != nil,
                  self.interactionEnabled, !self.transitioning else { return }
            self.motion.start(reducedMotion: HUDRuntimeAppearance.reduceMotion,
                              initialPoint: self.currentPointerTarget())
        }
    }

    func stopMotionForConcealment() {
        motion.stop(freezePresentation: true)
        sourceWatch?.suspendForConcealment()
        headerClock.setActive(false)
    }

    func cancelAnimations(preserveClickFeedback: Bool = false, preservingChargeAnimation: Bool = false, preservingPointerMotion: Bool = false,
                          preservingNowPlayingPresentation: Bool = false, preservingCursor: Bool = false) {
        sourceEntranceReady = nil
        headerClock.setActive(false)
        sourceWatch?.suspendForConcealment(preservingCursor: preservingCursor)
        if !preservingNowPlayingPresentation { modulePresentationAllowed = false }
        deactivateModuleInput(preservingNowPlayingPresentation: preservingNowPlayingPresentation)
        generation += 1 // A transaction completion can fire when animations are removed.
        transitioning = false
        transitionCompletion = nil
        moduleContent?.cancel()
        selectedModule = moduleContent?.selectedModule ?? .power
        if !preservingChargeAnimation { chargeBadge.cancelAnimations() }
        identityCard.resetInteraction()
        navigation.cancelAnimations()
        navigation.select(selectedModule, animated: false)
        if preservingPointerMotion {
            motion.startPointerFollowing(reducedMotion: HUDRuntimeAppearance.reduceMotion, initialPoint: currentPointerTarget())
        } else { motion.stop(freezePresentation: true) }
        quitPromptOpen = false
        func remove(_ item: CALayer) {
            if preserveClickFeedback && item === actionFeedback { return }
            if preservingChargeAnimation && item === chargeBadge.layer { return }
            if preservingNowPlayingPresentation && (item === nowPlayingCanvas.root || item === archiveCanvas.root || item === readerCanvas.root
                || item === mediaAssemblyCanvas.root || item === calendarCanvas.root || item === minigameCanvas.root) { return }
            if preservingPointerMotion {
                for key in item.animationKeys() ?? [] where !key.hasPrefix("parallax.") { item.removeAnimation(forKey: key) }
            } else { item.removeAllAnimations() }
            item.sublayers?.forEach(remove)
            if let mask = item.mask { remove(mask) }
        }
        if let layer = layer { remove(layer) }
        // cancel() restores the last committed screen. Its host pose must be
        // restored as well when an interrupted request crossed Work Mode's
        // distinct presentation size, before input uses the committed mapping.
        if moduleContent != nil {
            notesCanvas.setPresentation(notesSelected: selectedModule == .notes, animated: false)
            // Restoring the committed module host must not reopen the source
            // overview that this cancellation just suspended. In particular,
            // a held initial pose must never submit a deployed stable frame.
            updateModulePresentation(restoreSourceOverview: false)
        }
        sourceWatch?.suspendForConcealment(preservingCursor: preservingCursor)
        updateButtonStates()
    }

    var visibleNotesForVerification: Set<UUID> { notesCanvas.visibleNoteIDs }
    var notesFollowRetractionForVerification: Bool {
        if usesSourceShell {
            return retracting && motion.externalProjection != nil
                && CATransform3DIsIdentity(notesPlane.deployment.transform)
                && CATransform3DEqualToTransform(notesPlane.spatial.transform, corePlane.spatial.transform)
                && CATransform3DEqualToTransform(notesLayout.transform, canvas.transform)
                && notesLayout.position == canvas.position
        }
        guard let notes = notesPlane.deployment.animation(forKey: "deployment.transform") as? CAKeyframeAnimation,
              let panels = panelsPlane.deployment.animation(forKey: "deployment.transform") as? CAKeyframeAnimation,
              let values = notes.values as? [NSValue], values.count == 3 else { return false }
        let middle = values[1].caTransform3DValue
        let end = values[2].caTransform3DValue
        return notes.duration == panels.duration && notes.keyTimes == panels.keyTimes
            && abs(notes.beginTime - panels.beginTime) < 0.02
            && abs(middle.m23) > 0.5 && abs(end.m23) > abs(middle.m23)
            && end.m14 == 0 && end.m24 == 0 && end.m34 == 0 && end.m44 == 1
            && CATransform3DEqualToTransform(notesLayout.transform, canvas.transform)
            && notesLayout.position == canvas.position
    }
    var notesDeploymentRestoredForVerification: Bool {
        CATransform3DIsIdentity(notesPlane.deployment.transform) && notesPlane.deployment.opacity == 1
            && CATransform3DEqualToTransform(notesLayout.transform, canvas.transform)
            && notesLayout.position == canvas.position
    }
    var notesSpatialPoseMatchesPanelsForVerification: Bool {
        // The near-plane guard may shorten only the notes lens at small HUD
        // scales. Their affine attitude, depth and pointer travel still match.
        let notes = notesPlane.spatial.transform
        let panels = panelsPlane.spatial.transform
        return notes.m11 == panels.m11 && notes.m12 == panels.m12 && notes.m13 == panels.m13
            && notes.m21 == panels.m21 && notes.m22 == panels.m22 && notes.m23 == panels.m23
            && notes.m31 == panels.m31 && notes.m32 == panels.m32 && notes.m33 == panels.m33
            && notes.m41 == panels.m41 && notes.m42 == panels.m42 && notes.m43 == panels.m43
            && notesPlane.pointerResponseDuration == panelsPlane.pointerResponseDuration
    }
    func projectNotesPointForVerification(_ point: CGPoint) -> CGPoint {
        let rect = projectNotesRect(CGRect(origin: point, size: .zero))
        return rect.origin
    }
    func notesWorkspacePointForVerification(_ point: CGPoint) -> CGPoint? { notesWorkspacePoint(point) }
    func performNoteActionForVerification(_ action: String) { notesCanvas.perform(actionID: action) }
    func installPinnedNoteForVerification(over point: CGPoint) throws -> UUID? {
        precondition(CommandLine.arguments.contains("--ui-test"))
        guard let local = notesWorkspacePoint(point) else { return nil }
        let note = CanvasNote(kind: .text, text: "Clock input occlusion fixture",
                              x: local.x - 110, y: local.y - 50, width: 220, height: 120, isPinned: true)
        try notesCanvas.installNoteForVerification(note)
        return notesCanvas.containsWorkspacePoint(local) ? note.id : nil
    }
    func installNoteForVerification(_ note: CanvasNote) throws {
        precondition(CommandLine.arguments.contains("--ui-test")); try notesCanvas.installNoteForVerification(note)
    }
    func noteScrollOffsetForVerification(_ id: UUID) -> CGFloat { notesCanvas.scrollOffset(for: id) }
    func revealShelfSelectionForVerification() {
        guard let action = shelfCanvas.accessibleActions.first(where: { $0.id.hasSuffix(":select") }) else { return }
        shelfCanvas.perform(actionID: action.id.replacingOccurrences(of: ":select", with: ":reveal"))
    }
    func dropFilesOnShelfNavigationForVerification(_ pasteboard: NSPasteboard) -> Bool {
        if usesSourceShell {
            guard let point = sourceWatch?.desktopPointForVerification(target: .module(.fileShelf)) else { return false }
            return receiveShelfNavigationDrop(pasteboard, at: point)
        }
        guard let entry = navigation.entries.first(where: { $0.module == .fileShelf }) else { return false }
        let rect = viewRect(projectedBounds(entry.rect, through: [panelsPlane.spatial.transform]))
        return receiveShelfNavigationDrop(pasteboard, at: CGPoint(x: rect.midX, y: rect.midY))
    }

    var reportGeometryMatchesSelectionForVerification: Bool {
        guard let moduleContent, designCanvases[selectedModule] != nil else { return false }
        // Drawing and input share one mapping: box px → view → box px.
        let box = HUDMetric.box
        let inputMatchesProjection = [CGPoint.zero, CGPoint(x: box.width / 2, y: box.height / 2), CGPoint(x: box.width, y: box.height)].allSatisfy { point in
            guard let recovered = designBoxPoint(projectDesignBoxRect(CGRect(origin: point, size: .zero)).origin) else { return false }
            return abs(recovered.x - point.x) < 0.001 && abs(recovered.y - point.y) < 0.001
        }
        let matchesSource: Bool
        if usesSourceShell, let projection = sourceCenterProjection {
            matchesSource = [CGPoint(x: 285, y: 105), CGPoint(x: 500, y: 320), CGPoint(x: 715, y: 535)].allSatisfy { point in
                let expected = HUDMotionMath.project(point, through: projection)
                let actual = viewRect(projectedBounds(CGRect(origin: point, size: .zero), through: [corePlane.spatial.transform])).origin
                return abs(actual.x - expected.x) < 0.001 && abs(actual.y - expected.y) < 0.001
            }
        } else { matchesSource = true }
        let chargeCenter = CGPoint(x: HUDChargeBadge.center.x, y: HUDChargeBadge.center.y + chargeSourceOffset)
        let chargeTransform = chargePlane.spatial.presentation()?.transform ?? chargePlane.spatial.transform
        let shownCharge = projectedBounds(CGRect(origin: chargeCenter, size: .zero), through: [chargeTransform]).origin
        let recoveredCharge = chargeDesignPoint(shownCharge)
        let chargeInputMatches = recoveredCharge.map {
            abs($0.x - HUDChargeBadge.center.x) < 0.001 && abs($0.y - HUDChargeBadge.center.y) < 0.001
        } ?? false
        return chargeInputMatches && matchesSource && inputMatchesProjection && moduleContent.selectedModule == selectedModule
            && moduleContent.layer.position == HUDDesignGeometry.discCentre && CATransform3DIsIdentity(moduleContent.layer.transform)
    }

    /// The drawn Work Mode ring in content-box px (0 off Work Mode).
    var workModeDialDiameterForVerification: CGFloat {
        guard selectedModule == .workMode else { return 0 }
        return workCanvas.page.layer.sublayers?.first { $0.name == "workMode.ring" }?.frame.width ?? 0
    }

    var activeAnimationCount: Int {
        func count(_ item: CALayer) -> Int {
            (item.animationKeys()?.count ?? 0) + (item.sublayers ?? []).reduce(0) { $0 + count($1) }
                + (item.mask.map(count) ?? 0)
        }
        return layer.map(count) ?? 0
    }

    var centerContentCount: Int { moduleContent.contentLayerCount }
    var isSwitchingModule: Bool { moduleContent.isTransitioning }
    var shellIdentity: ObjectIdentifier { ObjectIdentifier(canvas) }
    var centerHostIdentity: ObjectIdentifier { ObjectIdentifier(moduleContent.layer) }
    var ambientStartTime: CFTimeInterval? { artwork?.rearRotor.animation(forKey: "ambient.rearRotation")?.beginTime }

    var legacyArtworkLayerCountForVerification: Int {
        func count(_ layer: CALayer) -> Int { 1 + (layer.sublayers ?? []).reduce(0) { $0 + count($1) } }
        return artwork?.groups.reduce(0) { $0 + count($1) } ?? 0
    }

    var ambientAnimationCount: Int { motion.ambientAnimationCount }
    var parallaxAnimationCount: Int { motion.parallaxAnimationCount + (usesSourceShell && sourceWatch?.pointerIsAnimatingForVerification == true ? 1 : 0) }
    var workModeAnimationCount: Int {
        func count(_ item: CALayer) -> Int {
            (item.animationKeys() ?? []).filter { $0.hasPrefix("workMode.") }.count
                + (item.sublayers ?? []).reduce(0) { $0 + count($1) }
        }
        return count(workCanvas.root)
    }
    var telemetryAnimationCount: Int { activityCanvas.animationCount + storageCanvas.animationCount }
    /// Battery's charging cells: a live readout that loops while settled (spec §1.9).
    var batteryLiveAnimationCount: Int {
        var node: CALayer? = batteryCanvas.root
        while let current = node, current !== layer { node = current.superlayer }
        return node == nil ? 0 : batteryCanvas.liveAnimationCount
    }
    var deploymentAnimationCount: Int {
        activeAnimationCount - ambientAnimationCount - parallaxAnimationCount - workModeAnimationCount - telemetryAnimationCount
            - batteryLiveAnimationCount - chargeBadge.animationCount - mapCanvas.surface.pulseAnimationCount
    }

    func showAppShortcutError(_ message: String) {
        if interactionEnabled, selectedModule != .addApp { selectModule(.addApp) }
        appShortcutCanvas.showError(message)
    }

    /// Exercises the same bounded pointer path from the local lifecycle harness.
    func setPointerForVerification(_ point: CGPoint) {
        guard window != nil, interactionEnabled || transitioning else { return }
        motion.setParallax(normalizedPoint: point)
        if usesSourceShell { sourceWatch?.refreshPointerForVerification() }
    }

    /// Fixture renderer feedback without synthesizing system pointer events.
    func setNavigationHoverForVerification(_ module: HUDModule?) { navigation.hover(module) }

    var pointerTargetForVerification: CGPoint { motion.targetNormalizedPoint }
    var currentPointerTargetForVerification: CGPoint { currentPointerTarget() }
    func spatialPoseMatchesPointerForVerification(_ point: CGPoint) -> Bool {
        if usesSourceShell, let projection = motion.externalProjection {
            return drivenPlanes.allSatisfy { CATransform3DEqualToTransform($0.spatial.transform, projection) }
                && reportGeometryMatchesSelectionForVerification
        }
        return drivenPlanes.allSatisfy { plane in
            CATransform3DEqualToTransform(plane.spatial.transform,
                HUDMotionMath.transform(normalizedPoint: point, depth: plane.depth, travel: plane.travel,
                                        reducedMotion: HUDRuntimeAppearance.reduceMotion,
                                        parallaxIntensity: CGFloat(configuration.parallaxIntensity),
                                        perspectiveIntensity: CGFloat(configuration.perspectiveIntensity),
                                        projectionBounds: plane.projectionBounds))
        }
    }

    /// Used by the graphical lifecycle harness to verify actual interpolation.
    var chargeStageForVerification: OverlayStage { chargeBadge.stage }
    var chargeAccessibilityLabelForVerification: String { chargeBadge.accessibilityLabel }
    private var chargeSourceOffset: CGFloat { usesSourceShell ? 38 : 0 }
    private var displayedChargeHitRect: CGRect { chargeBadge.hitRect.offsetBy(dx: 0, dy: chargeSourceOffset) }
    var chargeHitRectForVerification: CGRect { displayedChargeHitRect }
    var chargeViewRectForVerification: CGRect {
        viewRect(projectedBounds(displayedChargeHitRect,
            through: [chargePlane.spatial.presentation()?.transform ?? chargePlane.spatial.transform]))
    }
    var chargeFollowsDialRetractionForVerification: Bool {
        if usesSourceShell {
            return retracting && motion.externalProjection != nil
                && CATransform3DEqualToTransform(chargePlane.spatial.transform, corePlane.spatial.transform)
                && CATransform3DEqualToTransform(chargePlane.deployment.transform, corePlane.deployment.transform)
        }
        guard let badge = chargePlane.deployment.animation(forKey: "deployment.transform") as? CAKeyframeAnimation,
              let dial = framePlane.deployment.animation(forKey: "deployment.transform") as? CAKeyframeAnimation else { return false }
        return badge.duration == dial.duration && badge.keyTimes == dial.keyTimes
            && CATransform3DEqualToTransform(chargePlane.deployment.transform, framePlane.deployment.transform)
    }
    func setChargeHoveredForVerification(_ hovered: Bool) { chargeBadge.setHovered(hovered) }
    var presentedRingScale: CGFloat? { ring.presentation()?.transform.m11 }

    @objc private func activateNavigationButton(_ sender: HUDNavigationHitButton) {
        guard sender.isEnabled, !sender.isHidden else { return }
        // Arrange on HUD: the right-hand tiles are dragged, not opened.
        if arrangingModules, sender.navigationTarget.group == .right { return }
        activateNavigationTarget(sender.navigationTarget)
    }

    private func activateNavigationTarget(_ target: HUDNavigationTarget) {
        guard allowsInputBesidePlacement else { return }
        if accountGauge.isPopoverOpen { accountGauge.dismiss(); return }
        notesInteraction?.finishEditing()
        notesCanvas.clearSelection()
        switch target {
        case .module(let module): selectModule(module)
        case .appShortcut(let id): onLaunchAppShortcut?(id)
        }
    }

    func engageAppShortcut(_ id: UUID) { navigation.pressShortcut(id: id) }

    var appNavigationTargetsForVerification: [HUDNavigationTarget] {
        if usesSourceShell, let sourceWatch { return sourceWatch.desktopNavigationTargets.filter { $0.group == .right } }
        return navigation.entries.filter { $0.group == .right }.map(\.target)
    }
    func activateAppNavigationForVerification(_ id: UUID) {
        if usesSourceShell {
            guard sourceWatch?.desktopNavigationTargets.contains(.appShortcut(id)) == true else { return }
            activateNavigationTarget(.appShortcut(id)); return
        }
        guard let button = navigationButtons[.appShortcut(id)] else { return }
        activateNavigationButton(button)
    }

    /// Restore before deployment so reopening Notes does not briefly show the
    /// battery page beneath the entrance animation.
    func prepareInitialModule(_ module: HUDModule) {
        guard window == nil, !interactionEnabled else { return }
        selectedModule = module
        navigation.select(module, animated: false)
        moduleContent.select(module: module, animated: false)
        notesCanvas.setPresentation(notesSelected: module == .notes, animated: false)
        updateModulePresentation()
    }

    func selectModule(_ module: HUDModule, animated: Bool = true) {
        guard allowsModuleInput else { return }
        guard module != selectedModule else { return }
        // A queued click must not cancel the workspace artwork already moving
        // with the current swap. Its input bridges are already inactive.
        if !moduleContent.isTransitioning { deactivateModuleInput() }
        selectedModule = module
        // Hydrate the incoming retained canvas before makeContent can repaint it
        // from the intentionally cleared inactive public model.
        nowPlayingCanvas.setPresented(module == .nowPlaying)
        mediaAssemblyCanvas.setPresented(module == .mediaAssembly)
        calendarCanvas.setPresented(module == .calendar)
        let shouldAnimate = animated && !HUDRuntimeAppearance.reduceMotion
        navigation.select(module, animated: shouldAnimate)
        moduleContent.select(module: module, animated: shouldAnimate) { [weak self] in
            guard let self = self, self.interactionEnabled, !self.transitioning else { return }
            self.updateMapOcclusionPresentation()
            self.updateButtonStates()
        }
        if shouldAnimate && !usesSourceShell, let artwork {
            // This frame is independent of the ambient rotors and pointer plane.
            let from = artwork.frame.presentation()?.transform ?? artwork.frame.transform
            let nudge = CAKeyframeAnimation(keyPath: "transform")
            let baseline = artwork.frame.transform
            nudge.values = [NSValue(caTransform3D: from),
                NSValue(caTransform3D: CATransform3DRotate(baseline, module.group == .left ? -0.065 : 0.065, 0, 0, 1)),
                NSValue(caTransform3D: baseline)]
            nudge.keyTimes = [0, 0.48, 1]
            nudge.duration = HUDModuleContent.transitionDuration
            nudge.timingFunctions = [CAMediaTimingFunction(name: .easeOut), CAMediaTimingFunction(name: .easeInEaseOut)]
            artwork.frame.add(nudge, forKey: "section.index")
        }
        updateModulePresentation()
        updateButtonStates()
        updateNavigationGeometry()
        scheduleVisibleMotion()
    }

    private func updateModulePresentation(restoreSourceOverview: Bool = true) {
        if restoreSourceOverview { updateSourceOverviewPresentation(stable: sourceOverviewPresented) }
        updateLegacyCenterLogo()
        fadeModuleContrast(visible: usesSourceShell && selectedModule != .nowPlaying)
        withoutActions {
            self.chargeBadge.layer.position = CGPoint(x: HUDChargeBadge.frame.midX,
                                                       y: HUDChargeBadge.frame.midY + self.chargeSourceOffset)
            self.industryWordmark.backgroundColor = (self.industryWordmark.contents != nil ? NSColor.clear : (self.currentDark ? NSColor.white : NSColor(white: 0.12, alpha: 1))).cgColor
            let color = self.selectedModule == .power ? self.currentBatteryTone : self.currentAccent
            self.artwork?.update(dark: self.currentDark, chargeColor: color, accentColor: self.currentAccent)
            self.progress.strokeColor = color.cgColor
            self.progress.isHidden = self.usesSourceShell || self.selectedModule == .workMode
            self.progress.strokeEnd = self.selectedModule == .power ? CGFloat(self.snapshot.percentage ?? 0) / 100 : 0.12
            self.setAccessibilityLabel(L10n.text("System interface", "系统界面") + ", " + self.selectedModule.title)
        }
        updateMapOcclusionPresentation()
    }

    /// The centre's contrast backdrop (none behind Now Playing's own cover)
    /// fades with the leaving module instead of switching at the click.
    private func fadeModuleContrast(visible: Bool) {
        let target: Float = visible ? 1 : 0
        guard moduleContrast.opacity != target else { return }
        let from = moduleContrast.presentation()?.opacity ?? moduleContrast.opacity
        // One transaction: a committed end value alone would show for a frame.
        withoutActions {
            self.moduleContrast.opacity = target
            guard self.window != nil, !HUDRuntimeAppearance.reduceMotion else { return }
            let fade = CABasicAnimation(keyPath: "opacity")
            fade.fromValue = from; fade.toValue = target; fade.duration = HUDDuration.leave
            self.moduleContrast.add(fade, forKey: "module.contrast")
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        // inVisibleRect follows layout automatically. Keep the same cursor
        // owner while native controls move with the projected scene.
        if tracking == nil {
            let area = NSTrackingArea(rect: .zero,
                options: [.mouseEnteredAndExited, .mouseMoved, .activeAlways, .inVisibleRect], owner: self)
            addTrackingArea(area); tracking = area
        }
        // Cursor updates require key-window tracking; activeAlways suppresses
        // them. Keep hover tracking independent for the nonactivating panel.
        if cursorTracking == nil {
            let area = NSTrackingArea(rect: .zero,
                options: [.cursorUpdate, .activeInKeyWindow, .inVisibleRect], owner: self)
            addTrackingArea(area); cursorTracking = area
        }
    }

    var preservesNativeDragCursor: Bool { externalFileDragActive || isDraggingShelfItem || isAwaitingFileDrop }

    override func cursorUpdate(with event: NSEvent) {
        // Preserve AppKit's actual file-drag cursor, while ordinary pressed
        // controls and note movement retain the HUD pointer.
        // Preserving a native drag/editor cursor is a handled ownership choice;
        // other unhandled cases continue through AppKit's responder chain.
        if sourceWatch?.refreshSourceCursor(fromCursorUpdate: event) == true { return }
        if let cursor = designCursor(at: convert(event.locationInWindow, from: nil)) { cursor.set() }
        else { super.cursorUpdate(with: event) }
    }

    /// The active module's own pointer at a view point (the map's grab hand);
    /// the source shell asks whenever it refreshes its cursor.
    private func designCursor(at location: CGPoint) -> NSCursor? {
        if let editor = overlayEditor {
            if editor.isDragging { return .closedHand }
            if let box = designBoxPoint(location), editor.stripContains(box, in: overlayEditorHost) { return nil }
            return coreDesignPoint(designPoint(location)).flatMap(overlayElement(at:)) != nil ? .openHand : nil
        }
        // Arrange on HUD: the open hand over a module button, the closed hand while one is dragged.
        if arrangingModules, arrangeDrag?.lifted == true { return .closedHand }
        if arrangeableModule(atView: location) != nil { return .openHand }
        guard let canvas = activeDesignCanvas, !canvas.capturesPointer, !designContext.overlay.capturesInput,
              let cursor = canvas.cursor(at: designBoxPoint(location)) else { return nil }
        // The shell's controls over a module (the bottom plates and the charge
        // badge over the map) and pinned notes keep their own pointer, except during a drag.
        if !canvas.isDragging, sourceWatch?.bottomButtonContains(location) == true
            || navigationTargetAtDesignPoint(designPoint(location)) != nil || notesCover(location) { return nil }
        return cursor
    }

    override func mouseEntered(with event: NSEvent) { updateHover(event) }
    override func mouseMoved(with event: NSEvent) {
        updateHover(event)
        activeDesignCanvas?.pointerMoved(at: designBoxPoint(convert(event.locationInWindow, from: nil)))
    }
    override func mouseExited(with event: NSEvent) {
        if !usesSourceShell {
            navigation.hover(nil)
            identityCard.resetInteraction(animated: true)
        }
        chargeBadge.setHovered(false)
        notesInteraction?.mouseMoved(at: nil)
        clearControlHighlights()
        if window != nil, interactionEnabled || transitioning, !isModuleInputLocked {
            motion.resetParallax(animated: true)
        }
    }

    private func followCurrentPointer() {
        guard let window, window.isVisible, !window.ignoresMouseEvents || transitioning,
              interactionEnabled || transitioning, !isModuleInputLocked else { return }
        motion.setParallax(normalizedPoint: currentPointerTarget())
    }

    private func updateHover(_ event: NSEvent) {
        followCurrentPointer()
        guard allowsModuleInput, window?.ignoresMouseEvents != true else { return }
        let location = convert(event.locationInWindow, from: nil)
        let point = designPoint(location)
        updateControlHighlights(at: location)
        let menuCapturesPointer = popupCapturesPointer
        let hoveredTarget = menuCapturesPointer ? nil : navigationTargetAtDesignPoint(point, forHover: true)
        if !usesSourceShell {
            navigation.hoverTarget(hoveredTarget)
            identityCard.setHovered(menuCapturesPointer ? nil : navigationPoint(point).flatMap { identityCard.target(at: $0) })
        }
        chargeBadge.setHovered(hoveredTarget == .module(.power))
    }

    private func refreshChargeHover() {
        guard allowsModuleInput, let window, !window.ignoresMouseEvents else { return }
        let point = convert(window.convertPoint(fromScreen: pointerLocationProvider()), from: nil)
        let notesCoverPointer = notesWorkspaceIsInteractive
            && notesWorkspacePoint(point).map { notesCanvas.containsWorkspacePoint($0) } == true
        let local = chargeDesignPoint(designPoint(point))
        chargeBadge.setHovered(!notesCoverPointer && local.map { chargeBadge.contains($0) } == true)
    }

    private func clearControlHighlights() {
        overlayEditor?.updateHighlights(in: overlayEditorHost, point: nil)
        accountGauge.hover(at: nil)
        if clockHovered { clockHovered = false; updateStatusPanel() }
        if let moduleContent { HUDControlHighlightLayer.update(in: moduleContent.layer, point: nil) }
        HUDControlHighlightLayer.update(in: notesCanvas.workspaceLayer, point: nil)
        if !usesSourceShell { HUDControlHighlightLayer.update(in: navigation.layer, point: nil) }
    }

    func refreshControlHighlights() {
        guard let window else { clearControlHighlights(); return }
        updateControlHighlights(at: convert(window.convertPoint(fromScreen: pointerLocationProvider()), from: nil))
    }

    private func updateControlHighlights(at location: CGPoint, pressed: Bool = false) {
        guard allowsModuleInput, !moduleContent.isTransitioning else {
            clearControlHighlights(); return
        }
        if let editor = overlayEditor {
            editor.updateHighlights(in: overlayEditorHost, point: designBoxPoint(location), pressed: pressed); return
        }
        accountGauge.hover(at: accountGaugePoint(location))
        let notePoint = notesWorkspaceIsInteractive ? notesWorkspacePoint(location) : nil
        let menuCapturesPointer = popupCapturesPointer
        let notesCoverPointer = !menuCapturesPointer && notePoint.map { notesCanvas.containsWorkspacePoint($0) } == true
        notesInteraction?.mouseMoved(at: notesCoverPointer ? notePoint : nil)
        let hoverClock = !menuCapturesPointer && !notesCoverPointer && clockPoint(location) != nil
        if hoverClock != clockHovered { clockHovered = hoverClock; updateStatusPanel() }
        HUDControlHighlightLayer.update(in: notesCanvas.workspaceLayer,
                                        point: notesCoverPointer || notesInteraction?.capturesPointer == true ? notePoint : nil, pressed: pressed)
        // The source shell owns its own button feedback. Its retained native
        // fallback is hidden, so traversing that layer tree does no visible work.
        if !usesSourceShell {
            HUDControlHighlightLayer.update(in: navigation.layer,
                point: notesCoverPointer || menuCapturesPointer ? nil : navigationPoint(designPoint(location)), pressed: pressed)
        }
        let overlayOpen = designContext.overlay.capturesInput
        HUDControlHighlightLayer.update(in: facilityBanner.layer, point: facilityBannerPoint(location), pressed: pressed)
        HUDControlHighlightLayer.update(in: facilityTags.layer, point: facilityTagPoint(location), pressed: pressed)
        HUDControlHighlightLayer.update(in: designContext.overlay.layer,
            point: overlayOpen ? designBoxPoint(location) : nil, pressed: pressed)
        if let content = moduleContent.activeContentLayer {
            HUDControlHighlightLayer.update(in: content, point: notesCoverPointer || overlayOpen ? nil : designBoxPoint(location), pressed: pressed)
        }
    }

    private func currentPointerTarget() -> CGPoint {
        guard let window = window else { return .zero }
        let pointInWindow = window.convertPoint(fromScreen: pointerLocationProvider())
        let point = designPoint(convert(pointInWindow, from: nil))
        return HUDMotionMath.normalizedPointer(location: point, center: HUDMotionMath.canvasCenter)
    }

    /// Capture a HUD popover before AppKit considers transparent navigation
    /// hit targets or a pinned note underneath it. The first outside click
    /// dismisses the menu instead of activating whatever is behind it.
    override func hitTest(_ point: NSPoint) -> NSView? {
        let local = convert(point, from: superview)
        if allowsModuleInput, overlayEditor != nil, bounds.contains(local) { return self }
        if allowsModuleInput, accountGauge.isPopoverOpen, bounds.contains(local) { return self }
        if allowsModuleInput, designContext.overlay.capturesInput || bootLog.isPlaying, bounds.contains(local) { return self }
        if allowsModuleInput, let point = facilityBannerPoint(local),
           facilityBannerRouter.control(at: point, in: facilityBanner.layer) != nil { return self }
        if allowsModuleInput, let point = facilityTagPoint(local),
           facilityTagRouter.control(at: point, in: facilityTags.layer) != nil { return self }
        if let canvas = activeDesignCanvas {
            if let editor = canvas.textView(atHost: local) { return editor }
            if canvas.capturesPointer, bounds.contains(local) { return self }
        }
        if notesWorkspaceIsInteractive, notesInteraction?.capturesPointer == true, bounds.contains(local) {
            return notesInteraction?.hitTestMenu(at: local) ?? self
        }
        if allowsModuleInput, accountGauge.isInteractive, let gaugePoint = accountGaugePoint(local),
           CGRect(origin: .zero, size: HUDAccountGauge.size).contains(gaugePoint) { return self }
        // Arrange on HUD: this view drags the right-hand tiles (native or drawn).
        if rightRowTarget(atView: local) != nil { return self }
        guard usesSourceShell, sourceOverviewPresented, bounds.contains(local) else { return super.hitTest(point) }
        let native = super.hitTest(point)
        if let native, native !== self, native !== sourceWatch,
           !native.isDescendant(of: blurBackdrop), native !== blurBackdrop { return native }
        let design = designPoint(local)
        // Pinned cards are painted above the map and source controls.
        if notesWorkspaceIsInteractive, let note = notesWorkspacePoint(local), notesCanvas.containsWorkspacePoint(note) { return self }
        if clockPoint(local) != nil { return self }
        if sourceWatch?.bottomButtonContains(local) == true { return sourceWatch }
        if designControlContains(local) || centreContains(local, module: selectedModule) { return self }
        if !usesSourceShell, let identity = navigationPoint(design), identityCard.target(at: identity) != nil { return self }
        if let charge = chargeDesignPoint(design), chargeBadge.contains(charge) { return self }
        return sourceWatch
    }

    private func lockParallaxForActiveInput() {
        // Interaction bridges request their lock before processing the action.
        // Decide after that action establishes a real drag/editor; opening a
        // submenu alone must never cancel the pointer animation.
        DispatchQueue.main.async { [weak self] in
            guard let self, self.allowsModuleInput,
                  self.isModuleInputLocked else { return }
            self.motion.freezeParallax()
            // Native inline fields were created in the action's earlier pose.
            // Align them to the committed surface before accepting text input.
            self.notesInteraction?.layoutAccessibility()
            self.activeDesignCanvas?.refreshTextFieldProjection()
        }
    }

    override func mouseDown(with event: NSEvent) {
        guard !quitCommitted else { return }
        summonedDuringFileDrag = false
        // The controller can queue an outside-click dismissal while the finite
        // entrance finishes; controls remain disabled throughout the transition.
        guard interactionEnabled || transitioning else { return }
        let location = convert(event.locationInWindow, from: nil)
        let p = designPoint(location)
        if bootLog.isPlaying { bootLog.skip(); return }
        // Placing the HUD elements takes every press: the strip or a drag.
        if overlayEditor != nil { if allowsModuleInput { overlayPointerDown(location) }; return }
        if allowsModuleInput, accountGauge.isPopoverOpen {
            _ = accountGauge.mouseDown(at: accountGaugePoint(location)); return
        }
        if allowsModuleInput, let point = facilityBannerPoint(location),
           facilityBannerRouter.pointerDown(at: point, in: facilityBanner.layer) { return }
        if allowsModuleInput, let point = facilityTagPoint(location),
           facilityTagRouter.pointerDown(at: point, in: facilityTags.layer) { return }
        // The confirm bar takes every press; an open menu or pop-over takes
        // the first press anywhere (spec §1.12).
        if allowsModuleInput, designContext.overlay.capturesInput {
            if let point = designBoxPoint(location) { designContext.overlay.router.pointerDown(at: point, in: designContext.overlay.layer) }
            return
        }
        if let canvas = activeDesignCanvas, canvas.capturesPointer {
            canvas.pointerDown(at: designBoxPoint(location) ?? CGPoint(x: -10_000, y: -10_000), event: event)
            window?.makeFirstResponder(self); return
        }
        if notesWorkspaceIsInteractive, notesInteraction?.dismissMenuIfOutside(at: location) == true { return }
        if allowsModuleInput, accountGauge.mouseDown(at: accountGaugePoint(location)) {
            window?.makeFirstResponder(self)
            return
        }
        // A press on the sanity bar never closes the HUD, also while it is still
        // opening or while the bar shows Work Mode time without a linked account.
        if let gauge = accountGaugePoint(location), CGRect(origin: .zero, size: HUDAccountGauge.size).contains(gauge) { return }
        if notesWorkspaceIsInteractive, let local = notesWorkspacePoint(location),
           notesInteraction?.mouseDownInWorkspace(at: local, clickCount: event.clickCount) == true { return }
        if notesWorkspaceIsInteractive {
            notesInteraction?.finishEditing()
            notesCanvas.clearSelection()
        }
        // Arrange on HUD: a press on a module button may start its drag; the
        // other tiles of the list do nothing.
        if rightRowTarget(atView: location) != nil {
            if let module = arrangeableModule(atView: location) {
                arrangeDrag = (module, location, false, configuration.moduleLayout)
            }
            window?.makeFirstResponder(self)
            return
        }
        // The clock is below popovers and workspace notes, just like the
        // navigation controls it sits beside.
        if allowsModuleInput, let clock = clockPoint(location) {
            if let index = HUDClockStyleArtwork.indicator(at: clock) { selectClockStyle(HUDClockStyle.allCases[index]) }
            else { selectModule(.workMode) }
            return
        }
        // A module's controls are foreground artwork (Now Playing's transport
        // row can cover the source profile card on compact displays): their
        // visible faces come first, navigation stays reachable around them.
        if designControlContains(location), designPointerDown(at: location, event: event) { return }
        if !usesSourceShell, allowsModuleInput, let local = navigationPoint(p),
           identityCard.target(at: local) == .close { presentQuitConfirmation(); return }
        if let target = navigationTargetAtDesignPoint(p), target == .module(.power) || target == .module(.profile) {
            activateNavigationTarget(target); return
        }
        if !usesSourceShell, allowsModuleInput, let local = navigationPoint(p),
           navigation.handleScrollClick(at: local) { return }
        if designPointerDown(at: location, event: event) { return }
        notesInteraction?.finishEditing()
        if let target = navigationTargetAtDesignPoint(p) { activateNavigationTarget(target); return }
        if !usesSourceShell, let local = navigationPoint(p), navigation.containsNavigationPoint(local, includingBottom: false) { return }
        // Empty space inside the displayed circle belongs to the HUD. Invert
        // the same live projection used to draw it before deciding dismissal.
        let center = coreDesignPoint(p) ?? p
        let dx = center.x - 500, dy = center.y - 320
        let insideRing = dx * dx + dy * dy < 310 * 310
        if !insideRing { onClose?() }
    }

    override func keyDown(with event: NSEvent) {
        if bootLog.isPlaying { bootLog.skip(); return }
        guard allowsModuleInput, !event.isARepeat else { return }
        let modifiers = event.modifierFlags.intersection([.command, .option, .control, .shift])
        if let editor = overlayEditor {
            // Esc cancels and Return confirms the placement; the summon shortcut still closes.
            if !(modifiers.isEmpty && editor.key(event.keyCode)), SummonShortcut.active.matches(event: event) { onToggle?() }
            return
        }
        if accountGauge.isPopoverOpen {
            if event.keyCode == 53 && modifiers.isEmpty { accountGauge.dismiss() }
            else if SummonShortcut.active.matches(event: event) { onToggle?() }
            else if [36, 49, 76].contains(event.keyCode) && modifiers.isEmpty { accountGauge.perform("refresh") }
            return
        }
        if designContext.overlay.handleKey(event.keyCode) { return }
        if let canvas = activeDesignCanvas, canvas.keyDown(event) { return }
        if settingsInteraction?.keyDown(event) == true { return }
        if notesWorkspaceIsInteractive && notesInteraction?.keyDown(event) == true { return }
        if event.keyCode == 53 && modifiers.isEmpty { onClose?() }
        else if SummonShortcut.active.matches(event: event) { onToggle?() }
        else { super.keyDown(with: event) }
    }

    func writePNG(to url: URL, scale: CGFloat = 2, presentation: Bool = false,
                  background: CGColor? = nil) throws {
        if window == nil { headerClock.setActive(true); headerClock.setActive(false) }
        layoutSubtreeIfNeeded()
        CATransaction.flush()
        let width = max(1, Int(bounds.width * scale)), height = max(1, Int(bounds.height * scale))
        guard let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                                      bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw NSError(domain: "PowerOverlay", code: 1)
        }
        context.translateBy(x: 0, y: CGFloat(height))
        context.scaleBy(x: scale, y: -scale)
        if let background {
            context.setFillColor(background)
            context.fill(bounds)
        }
        if usesSourceShell {
            guard let sourceWatch, !sourceWatch.isHidden, sourceWatchFailureReason == nil else {
                throw HUDSourceError.invalid("Source Watch is unavailable: \(sourceWatchFailureReason ?? "concealed")")
            }
            (presentation ? backdrop.presentation() ?? backdrop : backdrop).render(in: context)
            let source = try sourceWatch.renderedImageForVerification()
            // Drawable pixels already use raster top-to-bottom rows. Undo
            // the AppKit layer-tree flip before drawing that CGImage.
            context.saveGState()
            context.scaleBy(x: 1, y: -1)
            context.translateBy(x: 0, y: -bounds.height)
            context.draw(source, in: bounds)
            context.restoreGState()
            sourceWatch.renderDesktopLabels(in: context)
        }
        if !usesSourceShell { (presentation ? backdrop.presentation() ?? backdrop : backdrop).render(in: context) }
        if usesSourceShell, let projection = sourceCenterProjection {
            // CALayer.render drops perspective. Rasterize the unprojected
            // native surfaces and reuse the source-label export homography.
            // Model transforms are restored before this transaction commits;
            // neither the live render tree nor its animation clocks changes.
            let planes = drivenPlanes
            let transforms = planes.map { $0.spatial.transform }
            CATransaction.begin(); CATransaction.setDisableActions(true)
            planes.forEach { $0.spatial.transform = CATransform3DIdentity }
            let statusHidden = statusPanel.isHidden, bannerHidden = facilityBanner.layer.isHidden
            statusPanel.isHidden = true
            facilityBanner.layer.isHidden = true
            // The footer deliberately lives below the 640-point design box.
            // Include its real unprojected frame without moving any live layer.
            let footerBounds = hintLabel.isHidden ? canvas.bounds : canvas.bounds.union(hintLabel.convert(hintLabel.bounds, to: canvas))
            let nativeRasterBounds = footerBounds.insetBy(dx: -2, dy: -2)
            HUDSourceWatchView.renderProjectedContent(canvas, opacity: canvas.opacity, clip: nil,
                flippedRaster: true, projection: projection, rasterBounds: nativeRasterBounds, subdivisions: 16, in: context)
            statusPanel.isHidden = statusHidden
            facilityBanner.layer.isHidden = bannerHidden
            if let statusProjection = sourceStatusProjection {
                HUDSourceWatchView.renderProjectedContent(statusPanel, opacity: canvas.opacity, clip: nil,
                    flippedRaster: true, projection: CATransform3DConcat(CATransform3DMakeTranslation(86.28, 8, 0), statusProjection),
                    rasterBounds: statusPanel.bounds.insetBy(dx: 0, dy: -12), subdivisions: 12, in: context)
            }
            var notesProjection = CATransform3DMakeTranslation(-designOrigin.x, -designOrigin.y, 0)
            notesProjection = CATransform3DConcat(notesProjection, CATransform3DMakeScale(1 / designScale, 1 / designScale, 1))
            notesProjection = CATransform3DConcat(notesProjection, projection)
            HUDSourceWatchView.renderProjectedContent(notesWorkspace, opacity: notesWorkspace.opacity, clip: nil,
                flippedRaster: true, projection: notesProjection, subdivisions: 16, in: context)
            // The facility banner hangs flat in shell space, above the projected planes.
            if facilityBanner.isShowing, !bannerHidden {
                let banner = presentation ? facilityBanner.layer.presentation() ?? facilityBanner.layer : facilityBanner.layer
                context.saveGState()
                context.translateBy(x: designOrigin.x, y: designOrigin.y)
                context.scaleBy(x: designScale, y: designScale)
                context.translateBy(x: banner.frame.minX, y: banner.frame.minY)
                context.scaleBy(x: designUnit, y: designUnit)
                banner.render(in: context)
                context.restoreGState()
            }
            // The top plane (the HUD elements strip, the recovery menu) over all of it.
            HUDSourceWatchView.renderProjectedContent(topLayout, opacity: topLayout.opacity, clip: nil,
                flippedRaster: true, projection: projection, rasterBounds: canvas.bounds.insetBy(dx: -2, dy: -2),
                subdivisions: 16, in: context)
            for (plane, transform) in zip(planes, transforms) { plane.spatial.transform = transform }
            CATransaction.commit()
        } else {
            context.saveGState()
            context.translateBy(x: designOrigin.x, y: designOrigin.y)
            context.scaleBy(x: designScale, y: designScale)
            (presentation ? canvas.presentation() ?? canvas : canvas).render(in: context)
            context.restoreGState()
            (presentation ? notesWorkspace.presentation() ?? notesWorkspace : notesWorkspace).render(in: context)
            context.saveGState()
            context.translateBy(x: designOrigin.x, y: designOrigin.y)
            context.scaleBy(x: designScale, y: designScale)
            (presentation ? topLayout.presentation() ?? topLayout : topLayout).render(in: context)
            context.restoreGState()
        }
        guard let cgImage = context.makeImage(),
              let data = NSBitmapImageRep(cgImage: cgImage).representation(using: .png, properties: [:]) else {
            throw NSError(domain: "PowerOverlay", code: 2)
        }
        try data.write(to: url, options: .atomic)
    }

    private var contentGroups: [CALayer] { [header, navigation.layer, footer] }

    private func configureSourceWatch() {
        do {
            let items = appNavigationPresentations()
            let view = try HUDSourceWatchView(frame: bounds, desktopMode: true,
                desktopNavigationEntries: HUDDesktopWatchNavigation.entries(shortcuts: items))
            view.isHidden = true; view.desktopDark = currentDark
            view.pointerLocationProvider = { [weak self] in self?.pointerLocationProvider() ?? NSEvent.mouseLocation }
            view.onAction = { [weak self, unowned view] action in
                guard let self, self.allowsModuleInput else { return }
                // Arrange on HUD: the right-hand tiles are dragged, not opened (pointer and VoiceOver).
                if self.arrangingModules, view.isRowAction(action) { return }
                self.activateNavigationTarget(action.target)
                self.window?.makeFirstResponder(self)
            }
            view.onClose = { [weak self] in
                guard let self, self.allowsModuleInput else { return }
                if self.accountGauge.isPopoverOpen { self.accountGauge.dismiss() }
                else { self.onClose?() }
            }
            view.onQuit = { [weak self] in self?.presentQuitConfirmation() }
            view.onFailure = { [weak self] reason in
                guard let self else { return }
                self.presentSourceFailure(reason)
            }
            view.onUnhandledKey = { [weak self] event in self?.keyDown(with: event) }
            view.onBackgroundMouseDown = { [weak self] event in self?.mouseDown(with: event) }
            view.onBackgroundMouseUp = { [weak self] event in self?.mouseUp(with: event) }
            view.onPointerMove = { [weak self] in self?.followCurrentPointer() }
            view.designCursor = { [weak self] in self?.designCursor(at: $0) }
            view.onDesktopCenterPlane = { [weak self] projection in self?.applySourceCenterProjection(projection) }
            view.onDesktopStatusPlane = { [weak self] projection in self?.applySourceStatusProjection(projection) }
            view.onDesktopBottomSilhouettes = { [weak self] polygons in self?.applySourceMapOcclusion(polygons) }
            view.desktopMapOcclusionEnabled = selectedModule == .map
            view.isDesktopPointerLocked = { [weak self] in self?.isModuleInputLocked ?? false }
            view.layer?.zPosition = -1000
            blurBackdrop.layer?.zPosition = -2000
            backdrop.zPosition = -1500
            sourceWatch = view
            view.desktopUIDRedacted = configuration.recordingMode
            view.setDesktopCenterLogo(configuration.centerLogo, revision: configuration.centerLogoRevision)
            refreshIdentityProfile()
            refreshAppNavigation(animated: false, presentations: items)
            addSubview(view, positioned: .below, relativeTo: nil)
            needsLayout = true // The display adaptation is measured on the source shell.
        } catch { presentSourceFailure(String(describing: error)) }
    }

    /// Keep one plane for painting, inverse hits, editors and accessibility.
    /// The source camera already includes HUD scale, position and gyro easing.
    private func applySourceCenterProjection(_ projection: CATransform3D) {
        guard usesSourceShell, designScale > 0 else { return }
        sourceCenterProjection = projection
        let local = HUDMotionMath.sourcePlaneTransform(projection, origin: designOrigin, scale: designScale)
        if let previous = motion.externalProjection, CATransform3DEqualToTransform(previous, local),
           drivenPlanes.allSatisfy({ CATransform3DEqualToTransform($0.spatial.transform, local) }) { return }
        motion.setExternalProjection(local)
        scheduleProjectedControlLayout()
        // Inline fields are real AppKit views; keep them on the final displayed
        // surface when an edit or direct manipulation freezes pointer input.
        if isModuleInputLocked { notesInteraction?.layoutAccessibility() }
        activeDesignCanvas?.refreshTextFieldProjection()
    }

    private func updateMapOcclusionPresentation() {
        let enabled = usesSourceShell && moduleContent.isPresenting(.map)
        sourceWatch?.desktopMapOcclusionEnabled = enabled
        if enabled { applySourceMapOcclusion(sourceBottomSilhouettes) }
        else {
            withoutActions {
                self.moduleContent.layer.mask = nil
                self.moduleContrast.mask = nil
            }
        }
    }

    /// Cuts the bottom plates' contours out of the centre host and its
    /// contrast. The redesigned map overflows the host, so the kept region
    /// spans the whole core plane; the host and the contrast share a frame.
    private func applySourceMapOcclusion(_ polygons: [[CGPoint]]) {
        sourceBottomSilhouettes = polygons
        guard moduleContent.isPresenting(.map), usesSourceShell else { return }
        let host = moduleContent.layer
        let scale = host.transform.m11
        guard scale > 0 else { return }
        let kept = CGRect(x: -host.frame.minX, y: -host.frame.minY, width: 1000, height: 640).union(host.bounds)
        let path = CGMutablePath()
        path.addRect(kept)
        for polygon in polygons {
            let local = polygon.compactMap { point -> CGPoint? in
                let design = designPoint(point)
                // These contours come from the frame just submitted by the
                // source renderer, so invert its matching model projection.
                guard let plane = Self.unproject(CGPoint(x: design.x - 500, y: design.y - 320),
                    transform: corePlane.spatial.transform) else { return nil }
                let core = CGPoint(x: plane.x + 500, y: plane.y + 320)
                return CGPoint(x: (core.x - host.position.x) / scale + host.bounds.midX,
                               y: (core.y - host.position.y) / scale + host.bounds.midY)
            }
            guard local.count == polygon.count, let first = local.first else { continue }
            path.move(to: first)
            for point in local.dropFirst() { path.addLine(to: point) }
            path.closeSubpath()
        }
        withoutActions {
            for mask in [self.mapForegroundMask, self.mapContrastMask] {
                mask.bounds = kept; mask.position = CGPoint(x: kept.midX, y: kept.midY)
                mask.fillRule = .evenOdd; mask.fillColor = NSColor.white.cgColor
                mask.path = path
            }
            self.moduleContent.layer.mask = self.mapForegroundMask
            self.moduleContrast.mask = self.mapContrastMask
        }
    }

    private func applySourceStatusProjection(_ projection: CATransform3D) {
        guard usesSourceShell, designScale > 0 else { return }
        sourceStatusProjection = projection
        // Keep the compact clock slightly inset and below its authored banner anchor.
        var local = CATransform3DConcat(CATransform3DMakeTranslation(86.28, 8, 0), projection)
        local = CATransform3DConcat(local, CATransform3DMakeTranslation(-designOrigin.x, -designOrigin.y, 0))
        local = CATransform3DConcat(local, CATransform3DMakeScale(1 / designScale, 1 / designScale, 1))
        // Ambient frames repeat a settled projection; only a moved panel needs its stand-ins laid out again.
        guard !CATransform3DEqualToTransform(statusPanel.transform, local) else { return }
        withoutActions { self.statusPanel.transform = local }
        scheduleProjectedControlLayout()
    }

    /// The clock and account-gauge buttons are accessibility stand-ins on the
    /// projected planes (their accessibility frames are projected on demand).
    /// Their view frames follow the camera once it settles: moving views on
    /// every rendered frame makes AppKit re-resolve and set the cursor each
    /// display cycle, a third of the main thread while the pointer moves.
    private var projectedControlLayout: DispatchWorkItem?
    private var projectedControlDeadline: TimeInterval = 0
    private func scheduleProjectedControlLayout() {
        projectedControlDeadline = ProcessInfo.processInfo.systemUptime + 0.12
        if projectedControlLayout == nil { armProjectedControlLayout() }
    }
    private func armProjectedControlLayout() {
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.projectedControlLayout = nil
            guard ProcessInfo.processInfo.systemUptime >= self.projectedControlDeadline else { self.armProjectedControlLayout(); return }
            self.layoutAccountGaugeControls(); self.layoutClockControls()
        }
        projectedControlLayout = work
        let delay = max(0, projectedControlDeadline - ProcessInfo.processInfo.systemUptime)
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }

    private func clockPoint(_ point: CGPoint) -> CGPoint? {
        guard usesSourceShell, !statusPanel.isHidden,
              let local = Self.unproject(designPoint(point), transform: statusPanel.transform),
              HUDClockStyleArtwork.hitBounds.contains(local) else { return nil }
        return local
    }
    private func selectClockStyle(_ style: HUDClockStyle) {
        guard style != configuration.clockStyle else { return }
        settingsController?.update { $0.clockStyle = style }
    }
    @objc private func clockAction(_ sender: HUDClockActionButton) {
        guard allowsInputBesidePlacement else { return }
        if sender.styleIndex < 0 { selectModule(.workMode) }
        else { selectClockStyle(HUDClockStyle.allCases[sender.styleIndex]) }
    }
    private func layoutClockControls() {
        // Hidden while HUD elements are placed, as the gauge's controls are.
        let interactive = allowsInputBesidePlacement
        let hidden = !usesSourceShell || !interactive
        let language = L10n.resolvedLanguage
        let workLabel = L10n.text("Work Mode", "工作模式", language: language)
        let styleLabel = L10n.text("Clock style", "时钟样式", language: language)
        let selectedLabel = L10n.text("Selected", "已选择", language: language)
        for button in clockControls {
            if button.isHidden != hidden { button.isHidden = hidden }
            if button.isEnabled != interactive { button.isEnabled = interactive }
            let frame = clockButtonFrame(button)
            if button.frame != frame { button.frame = frame }
            let label = button.styleIndex < 0 ? workLabel : styleLabel + " \(button.styleIndex + 1)"
            if button.accessibilityLabel() != label { button.setAccessibilityLabel(label) }
            let value = button.styleIndex == configuration.clockStyle.index ? selectedLabel : ""
            if button.accessibilityValue() as? String != value { button.setAccessibilityValue(value) }
        }
    }

    private func clockButtonFrame(_ button: HUDClockActionButton) -> CGRect {
        let rect = button.styleIndex < 0 ? HUDClockStyleArtwork.body : HUDClockStyleArtwork.indicatorRect(button.styleIndex)
        return viewRect(HUDClockStyleArtwork.projectedBounds(of: rect, through: statusPanel.transform))
    }

    private func updateStatusPanel() {
        withoutActions {
            self.statusPanel.isHidden = !self.usesSourceShell
            if self.usesSourceShell {
                self.clockPage.install(time: self.clockTime, date: self.clockDate, artwork: self.clockStyleArtwork.layer)
                if self.workBadge.superlayer !== self.statusPanel { self.statusPanel.addSublayer(self.workBadge) }
                // The style artwork lays out and colours the time, date and status row.
                self.statusFrame.strokeColor = self.currentAccent.withAlphaComponent(0.70).cgColor
                self.statusUnderline.strokeColor = self.currentAccent.withAlphaComponent(0.75).cgColor
                self.clockStyleArtwork.layer.isHidden = false
                self.clockStyleArtwork.updateStatus(work: self.workBadge, workActive: self.displayedWorkPhase == .running
                    || self.displayedWorkPhase == .paused, recording: self.configuration.recordingMode,
                    accent: self.currentAccent)
                self.clockStyleArtwork.update(style: self.configuration.clockStyle, reading: self.headerClock.reading,
                    time: self.clockTime, date: self.clockDate, accent: self.currentAccent)
                self.statusUnderline.strokeColor = NSColor(white: 0.72, alpha: 0.55).cgColor
                self.statusFrame.lineWidth = self.clockHovered ? 2 : 1.5
                self.statusPlate.fillColor = NSColor(white: self.clockHovered ? 0.13 : 0.055, alpha: 0.78).cgColor
                if let projection = self.sourceStatusProjection { self.applySourceStatusProjection(projection) }
            } else {
                self.clockPage.cancelTransition()
                self.clockStyleArtwork.layer.isHidden = true
                if self.clockTime.superlayer !== self.header {
                    for item in [self.clockTime, self.clockDate, self.workBadge] { self.header.addSublayer(item) }
                }
                self.clockTime.foregroundColor = NSColor(white: self.currentDark ? 0.95 : 0.13, alpha: 1).cgColor
                self.clockDate.foregroundColor = NSColor(white: self.currentDark ? 0.55 : 0.40, alpha: 1).cgColor
                self.clockTime.frame = CGRect(x: 340, y: 0, width: 220, height: 27); self.clockTime.fontSize = 20
                self.clockDate.frame = CGRect(x: 340, y: 31, width: 220, height: 18); self.clockDate.fontSize = 9
                self.workBadge.frame = CGRect(x: 320, y: 55, width: 240, height: 18); self.workBadge.fontSize = 10
            }
        }
    }

    private func presentSourceFailure(_ reason: String) {
        let wasRetracting = retracting
        let wasPresented = sourceOverviewPresented
        let handler = transitionCompletion
        let ready = sourceEntranceReady
        transitionCompletion = nil; sourceEntranceReady = nil
        sourceWatchFailureReason = reason
        installLegacyNavigationControlsIfNeeded()
        installLegacyArtworkIfNeeded()
        sourceCenterProjection = nil
        motion.setExternalProjection(nil)
        sourceWatch?.conceal()
        refreshIdentityProfile()
        refreshAppNavigation(animated: false)
        let field = sourceWatchFailure ?? NSTextField(wrappingLabelWithString: "")
        NSLog("Source Watch render failure: %@", reason)
        field.stringValue = L10n.text("The original menu could not be rendered. Please check the log.", "原始菜单无法渲染，请查看日志。")
        field.textColor = .labelColor
        field.alignment = .center
        field.font = HUDFontResolver.nsFont(size: 15)
        field.setAccessibilityRole(.staticText)
        if sourceWatchFailure == nil { sourceWatchFailure = field; addSubview(field) }
        field.isHidden = !usesSourceShell || !sourceOverviewPresented
        needsLayout = true
        // Restore the retained native shell immediately. The same native
        // canvases and controls remain available after a runtime GPU failure.
        updateSourceOverviewPresentation(stable: false)
        updateModulePresentation(restoreSourceOverview: false)
        if wasPresented && !wasRetracting {
            showStable()
            if interactionEnabled { scheduleVisibleMotion() }
        } else {
            transitioning = false
            if wasRetracting {
                withoutActions { self.canvas.opacity = 0; self.notesWorkspace.opacity = 0; self.backdrop.opacity = 0 }
            }
            updateButtonStates()
        }
        // Preparation may fail before the owner has shown the window. Preserve
        // its ready/completion order and consume each callback exactly once.
        ready?()
        handler?()
    }

    private func updateSourceOverviewPresentation(stable: Bool) {
        let overview = usesSourceShell
        sourceWatch?.selectedDesktopModule = selectedModule
        if overview { motion.stop(freezePresentation: false) }
        withoutActions {
            self.canvas.isHidden = false
            self.backdrop.isHidden = false
            self.notesWorkspace.isHidden = false
            self.actionFeedback.isHidden = false
            self.artwork?.groups.forEach { $0.isHidden = overview }
            self.identityCard.layer.isHidden = overview
            self.navigation.layer.isHidden = overview
            self.navigation.bottomLayer.isHidden = overview
            self.industryWordmark.isHidden = overview
            self.progress.isHidden = overview || self.selectedModule == .workMode
        }
        updateStatusPanel()
        headerClock.setActive(window != nil && interactionEnabled)
        sourceWatchFailure?.isHidden = !overview || !sourceOverviewPresented || sourceWatchFailureReason == nil
        guard let sourceWatch else { return }
        if overview && sourceOverviewPresented && sourceWatchFailureReason == nil {
            sourceWatch.isHidden = false
            if stable, sourceWatch.playback.phase == .concealed { sourceWatch.showStable() }
            sourceWatch.inputEnabled = allowsInputBesidePlacement
        } else { sourceWatch.conceal() }
    }

    private func animateSourceEntrance(ready: @escaping () -> Void, completion: @escaping () -> Void) {
        transitioning = true; retracting = false; transitionCompletion = completion
        let token = generation
        motion.startPointerFollowing(reducedMotion: HUDRuntimeAppearance.reduceMotion, initialPoint: currentPointerTarget())
        withoutActions { self.canvas.opacity = 0; self.notesWorkspace.opacity = 0 }
        if selectedModule == .map { mapCanvas.prepareForPresentation() }
        updateButtonStates()
        guard let sourceWatch, sourceWatchFailureReason == nil else {
            ready()
            transitioning = false; transitionCompletion = nil
            DispatchQueue.main.async { [weak self] in
                guard self?.generation == token else { return }; completion()
            }
            return
        }
        sourceEntranceReady = ready
        sourceWatch.open(ready: { [weak self, weak sourceWatch] in
            guard let self, let sourceWatch, self.generation == token else { return }
            if !HUDRuntimeAppearance.reduceMotion {
                self.animateSourceBlur(sourceWatch.document.blurAnimation.entrance)
            }
            self.withoutActions { self.canvas.opacity = 1; self.notesWorkspace.opacity = 1 }
            if !HUDRuntimeAppearance.reduceMotion {
                self.animate(self.canvas, "opacity", from: 0, to: 1, duration: 0.24, delay: 0.20)
                self.animate(self.notesWorkspace, "opacity", from: 0, to: 1, duration: 0.24, delay: 0.20)
                self.chargeBadge.animateEntrance()
            }
            self.motion.startPointerFollowing(reducedMotion: HUDRuntimeAppearance.reduceMotion, initialPoint: self.currentPointerTarget())
            let callback = self.sourceEntranceReady
            self.sourceEntranceReady = nil
            callback?()
        }) { [weak self] in
            DispatchQueue.main.async { [weak self] in
                guard let self, self.generation == token else { return }
                self.transitioning = false; self.transitionCompletion = nil
                self.updateButtonStates(); completion()
            }
        }
    }

    private func animateSourceExit(completion: @escaping () -> Void) {
        let canvasOpacity = canvas.presentation()?.opacity ?? canvas.opacity
        let notesOpacity = notesWorkspace.presentation()?.opacity ?? notesWorkspace.opacity
        let heldOpening = sourceWatch?.isPreparingBackdrop == true
        cancelAnimations(preserveClickFeedback: true, preservingPointerMotion: true)
        transitioning = true; retracting = true; transitionCompletion = completion
        let token = generation
        updateButtonStates()
        let finish: () -> Void = { [weak self] in
            DispatchQueue.main.async { [weak self] in
                guard let self, self.generation == token else { return }
                self.sourceOverviewPresented = false
                self.transitioning = false; self.transitionCompletion = nil
                self.withoutActions { self.blurBackdrop.layer?.opacity = 0 }
                self.updateSourceOverviewPresentation(stable: false)
                self.updateButtonStates(); completion()
            }
        }
        withoutActions { self.blurBackdrop.layer?.opacity = 0; self.canvas.opacity = 0; self.notesWorkspace.opacity = 0 }
        guard let sourceWatch, sourceWatchFailureReason == nil else { finish(); return }
        if heldOpening {
            // The original exit clip begins from a fully deployed menu. An
            // opening held at its initial pose must disappear without sampling
            // that complete pose or briefly flashing its unprepared background.
            sourceWatch.conceal()
            finish()
            return
        }
        if !HUDRuntimeAppearance.reduceMotion {
            animateSourceBlur(sourceWatch.document.blurAnimation.exit)
            // All feature planes already inherit the original center wrapper
            // through the shared homography; another fold would double its tilt.
            animate(canvas, "opacity", from: canvasOpacity, to: 0, duration: 0.06, delay: 0.35)
            animate(notesWorkspace, "opacity", from: notesOpacity, to: 0, duration: 0.06, delay: 0.35)
            chargeBadge.animateExit()
        }
        sourceWatch.close(completion: finish)
    }

    private func buildDepthLayers() {
        vignette.type = .radial
        vignette.startPoint = CGPoint(x: 0.5, y: 0.46)
        vignette.endPoint = CGPoint(x: 1, y: 1)
        vignette.locations = [0, 0.5, 1]
        backdrop.addSublayer(vignette)
        for plane in depthPlanes { canvas.addSublayer(plane.deployment) }
    }

    private func installLegacyArtworkIfNeeded() {
        guard artwork == nil else { return }
        let artwork = HUDMechanicalArtwork()
        self.artwork = artwork
        distantPlane.content.addSublayer(artwork.distant)
        rearPlane.content.addSublayer(artwork.rear)
        secondaryPlane.content.addSublayer(artwork.secondary)
        framePlane.content.addSublayer(artwork.frame)
        // The fallback may be installed after the module layers. Preserve the
        // original ordering: mechanical artwork behind progress and controls.
        innerPlane.content.insertSublayer(artwork.inner, at: 0)
        markersPlane.content.addSublayer(artwork.markers)
        glassPlane.content.addSublayer(artwork.glass)
        rimPlane.content.addSublayer(artwork.rim)
        // Match the shared watch_loop phase. These are slow bounded excursions,
        // not independent full turns; registerAmbient owns pause/close cleanup.
        for (rotor, key) in [(artwork.rearRotor, "rearRotation"),
                             (artwork.secondaryRotor, "secondaryRotation"),
                             (artwork.innerGuideRotor, "innerRotation")] {
            motion.registerAmbient(layer: rotor, key: key, keyPath: "transform.rotation.z",
                                   fromValue: 0, toValue: HUDMechanicalArtwork.watchRingExcursion,
                                   duration: HUDMechanicalArtwork.watchLoopLegDuration, timingFunction: .linear)
        }
        registerTriangleMotion()
        motion.registerAmbient(layer: artwork.meshRotor, key: "meshRotation", keyPath: "transform.rotation.z",
                               fromValue: 0, toValue: HUDMechanicalArtwork.watchMeshExcursion,
                               duration: HUDMechanicalArtwork.watchLoopLegDuration, timingFunction: .linear)
        motion.registerAmbient(layer: artwork.indicatorGlow, key: "indicatorPulse", keyPath: "opacity",
                               fromValue: -0.12, toValue: 0.12, duration: 1.7)
        motion.registerAmbient(layer: artwork.scanLayer, key: "scan", keyPath: "transform.translation.y",
                               fromValue: -165, toValue: 165, duration: 6.1, beginOffset: -3.05)
        motion.registerAmbient(layer: artwork.highlightCarrier, key: "highlight", keyPath: "opacity",
                               fromValue: -0.06, toValue: 0.08, duration: 3.7)
        artwork.update(dark: currentDark,
            chargeColor: selectedModule == .power ? currentBatteryTone : currentAccent,
            accentColor: currentAccent)
        let scale = hudRasterScale
        func applyScale(_ layer: CALayer) {
            if !(layer is CATransformLayer) { layer.contentsScale = HUDRenderScale.contentScale(for: layer, baseScale: scale) }
            layer.sublayers?.forEach(applyScale)
            if let mask = layer.mask { applyScale(mask) }
        }
        artwork.groups.forEach(applyScale)
    }

    private func registerTriangleMotion() {
        guard let artwork else { return }
        for (index, rotor) in artwork.triangleRotors.enumerated() {
            motion.registerAmbient(layer: rotor, key: "triangleRotation.\(index)",
                                   keyPath: "transform.rotation.z", fromValue: 0,
                                   toValue: HUDMechanicalArtwork.watchRingExcursion,
                                   duration: HUDMechanicalArtwork.watchLoopLegDuration, timingFunction: .linear)
        }
    }

    private func buildRing() {
        progress.frame = CGRect(x: 250, y: 70, width: 500, height: 500)
        progress.path = Self.arc(center: CGPoint(x: 250, y: 250),
                                 radius: 196 * HUDMechanicalArtwork.instrumentScale,
                                 start: -.pi / 2, end: .pi * 1.5)
        progress.fillColor = nil; progress.lineWidth = 2.2
        innerPlane.content.addSublayer(progress)
    }

    private func buildPanels() {
        moduleContent = HUDModuleContent(initialModule: launchModule)
        moduleContent.onPresentationChange = { [weak self] from, to, animated in
            guard let self else { return }
            // Revealing a navigation row can refresh input after the earlier
            // cleanup. Once the host is transitioning, finish that cleanup
            // before adding the workspace animation, so it cannot cancel it.
            if animated { self.notesInteraction?.setActive(false) }
            self.notesCanvas.setPresentation(notesSelected: to == .notes, animated: animated,
                direction: HUDModuleContent.direction(from: from, to: to))
            self.updateMapOcclusionPresentation()
        }
        registerModule(notesCanvas, for: .notes)
        registerModule(shelfCanvas, for: .fileShelf)
        registerModule(clipboardCanvas, for: .clipboard)
        registerModule(eventLogCanvas, for: .eventLog)
        registerModule(mapCanvas, for: .map)
        registerModule(volumeCanvas, for: .volume)
        registerModule(nowPlayingCanvas, for: .nowPlaying)
        registerModule(archiveCanvas, for: .archive)
        registerModule(mediaAssemblyCanvas, for: .mediaAssembly)
        registerModule(calendarCanvas, for: .calendar)
        registerModule(minigameCanvas, for: .minigame)
        registerModule(accountCanvas, for: .account)
        registerModule(readerCanvas, for: .reader)
        registerModule(workCanvas, for: .workMode)
        registerModule(storageCanvas, for: .storage)
        registerModule(activityCanvas, for: .activityMonitor)
        registerModule(appShortcutCanvas, for: .addApp)
        registerModule(profileCanvas, for: .profile)
        registerModule(batteryCanvas, for: .power)
        registerModule(projectionCanvas, for: .projection)
        projectionCanvas.onOpen = { [weak self] in self?.onOpenProjection?() }
        batteryCanvas.update(snapshot: snapshot, configuration: configuration)
        designContext.setRecordingMode(configuration.recordingMode)
        chargePlane.content.addSublayer(chargeBadge.layer)
        chargeBadge.onHitRegionChanged = { [weak self] in
            self?.refreshChargeHover()
            self?.updateButtonStates()
        }
        if let settingsController {
            for module: HUDModule in [.system, .display, .hotkeys, .about] {
                let settings = HUDSettingsCanvas(module: module, controller: settingsController, context: designContext)
                settings.onEditOverlay = { [weak self] in self?.beginOverlayPlacement() }
                settings.onArrangeModules = { [weak self] in self?.setModuleArrangement($0) }
                settingsCanvases[module] = settings
                register(design: settings)
            }
        }
        moduleContrast.name = "module.contrast"
        moduleContrast.frame = HUDModuleContent.viewportFrame
        moduleContrast.type = .radial
        moduleContrast.startPoint = CGPoint(x: 0.5, y: 0.5)
        moduleContrast.endPoint = CGPoint(x: 1, y: 1)
        moduleContrast.locations = [0, 0.60, 1]
        corePlane.content.addSublayer(moduleContrast)
        corePlane.content.addSublayer(moduleContent.layer)
        corePlane.content.addSublayer(navigation.bottomLayer)
        corePlane.content.addSublayer(designContext.overlay.layer)
        // Confirms blur the module content behind them (a Core Image filter).
        layerUsesCoreImageFilters = true
        designContext.overlay.blurTarget = moduleContent.layer
        configureDesignContext()
        layoutDesignOverlay()
        installFacilityLayers()
        buildIndustryWordmark()
        actionFeedback.name = "hud.action.feedback"
        actionFeedback.bounds = CGRect(x: -10, y: -10, width: 20, height: 20)
        actionFeedback.fillColor = nil; actionFeedback.lineWidth = 1.2
        actionFeedback.opacity = 0
        let brackets = CGMutablePath()
        for sign in [-1.0, 1.0] {
            let s = CGFloat(sign)
            brackets.move(to: CGPoint(x: -8 * s, y: -3 * s))
            brackets.addLine(to: CGPoint(x: -8 * s, y: -8 * s))
            brackets.addLine(to: CGPoint(x: -3 * s, y: -8 * s))
        }
        actionFeedback.path = brackets
        // Leave the enlarged marker orbit clear of the English heading.
        header.frame = HUDOverlayGeometry.header
        header.zPosition = 20 // Above central module artwork.
        corePlane.content.addSublayer(header)
        titleLabel = text("ENDFIELDHUD", rect: CGRect(x: 0, y: 0, width: 300, height: 27), size: 20,
                          parent: header, role: .primary, weight: .semibold)
        subtitleLabel = text("SYSTEM INTERFACE", rect: CGRect(x: 0, y: 31, width: 300, height: 18),
                             size: 9, parent: header, role: .muted)
        clockTime = text("", rect: CGRect(x: 340, y: 0, width: 220, height: 27), size: 20,
                         parent: header, role: .primary, alignment: .right, weight: .semibold, tabular: true)
        clockDate = text("", rect: CGRect(x: 340, y: 31, width: 220, height: 18), size: 9,
                         parent: header, role: .muted, alignment: .right, tabular: true)
        clockTime.name = "hud.clock.time"; clockDate.name = "hud.clock.date"
        accountGauge.layer.position = HUDAccountGauge.headerPosition
        header.addSublayer(accountGauge.layer)
        // The recovery menu draws on the top plane, above the clock and the rest of the HUD.
        accountPopoverHost.name = "hud.account.recovery.host"
        accountPopoverHost.anchorPoint = .zero
        accountPopoverHost.bounds = CGRect(origin: .zero, size: HUDAccountGauge.size) // Placed by applyOverlayLayout.
        topPlane.content.addSublayer(accountPopoverHost)
        accountGauge.popoverHost = accountPopoverHost
        accountGauge.onRefresh = { [weak self] in self?.accountController.refresh(manual: true) }
        accountGauge.onChange = { [weak self] in self?.layoutAccountGaugeControls() }
        accountGauge.onOpenWorkMode = { [weak self] in self?.activateNavigationTarget(.module(.workMode)) }
        for index in 0..<2 {
            let button = HUDNavigationScrollButton(frame: .zero)
            button.tag = index; button.target = self; button.action = #selector(accountGaugeAction(_:))
            button.title = ""; button.isBordered = false; button.isHidden = true
            button.projectedFrame = { [weak self, weak button] in
                guard let self, let window = self.window, let button else { return .zero }
                let action = self.accountGauge.accessibleActions.first { $0.id == (button.tag == 0 ? "toggle" : "refresh") }
                guard let action else { return .zero }
                return window.convertToScreen(self.convert(self.accountGaugeRect(action.rect), to: nil))
            }
            addSubview(button); accountGaugeControls.append(button)
        }
        headerClock.onChange = { [weak self] reading in
            guard let self else { return }
            self.accountController.tick()
            self.updateAccountGauge()
            self.withoutActions {
                self.clockTime.string = reading.time; self.clockDate.string = reading.date
                if self.usesSourceShell {
                    self.clockStyleArtwork.update(style: self.configuration.clockStyle, reading: reading,
                        time: self.clockTime, date: self.clockDate, accent: self.currentAccent)
                }
            }
        }
        workBadge = text("", rect: CGRect(x: 320, y: 55, width: 240, height: 18), size: 10,
                         parent: header, role: .accent, alignment: .right, weight: .semibold)
        workBadge.name = "hud.workMode.badge"
        applyOverlayLayout()
        statusPanel.name = "hud.clock.panel"
        statusPanel.anchorPoint = .zero; statusPanel.position = .zero
        statusPanel.bounds = CGRect(x: 0, y: 0, width: 340, height: 122)
        statusPanel.isHidden = true
        let outer = CGPath(roundedRect: statusPanel.bounds.insetBy(dx: 0.8, dy: 0.8), cornerWidth: 17, cornerHeight: 17, transform: nil)
        statusPlate.path = CGPath(roundedRect: statusPanel.bounds.insetBy(dx: 7, dy: 7), cornerWidth: 12, cornerHeight: 12, transform: nil)
        statusPlate.fillColor = NSColor(white: 0.055, alpha: 0.78).cgColor
        statusPlate.strokeColor = NSColor(white: 0.70, alpha: 0.35).cgColor; statusPlate.lineWidth = 1
        statusFrame.path = outer; statusFrame.fillColor = nil; statusFrame.lineWidth = 1.5
        let marks = CGMutablePath()
        for i in 0..<5 {
            let r = HUDClockStyleArtwork.indicatorRect(i)
            marks.move(to: CGPoint(x: r.minX, y: 131)); marks.addLine(to: CGPoint(x: r.maxX - 7, y: 131))
        }
        statusUnderline.path = marks; statusUnderline.lineWidth = 3; statusUnderline.fillColor = nil
        for item in [statusPlate, statusFrame, statusUnderline] { statusPanel.addSublayer(item) }
        statusPanel.addSublayer(clockPage.layer)
        clockPage.install(time: clockTime, date: clockDate, artwork: clockStyleArtwork.layer)
        statusPanel.addSublayer(clockStyleArtwork.selection)
        statusPanel.addSublayer(clockStyleArtwork.status)
        canvas.addSublayer(statusPanel)
        for index in -1..<5 {
            let button = HUDClockActionButton(frame: .zero)
            button.styleIndex = index; button.target = self; button.action = #selector(clockAction(_:))
            button.title = ""; button.isBordered = false; button.isHidden = true
            button.projectedFrame = { [weak self, weak button] in
                guard let self, let window = self.window, let button else { return .zero }
                return window.convertToScreen(self.convert(self.clockButtonFrame(button), to: nil))
            }
            addSubview(button); clockControls.append(button)
        }
        footer.frame = CGRect(x: 0, y: 0, width: 1000, height: 640)
        canvas.addSublayer(footer)
        hintLabel = text("", rect: CGRect(x: 275, y: 622, width: 450, height: 18), size: 9,
                         parent: footer, role: .muted, alignment: .center)
    }

    private func buildIndustryWordmark() {
        industryWordmark.name = "hud.endfieldIndustries"
        industryWordmark.frame = CGRect(x: 425, y: 544, width: 150, height: 29)
        industryWordmark.backgroundColor = NSColor.white.cgColor
        // Display the original supplied lettering through a native viewport.
        // The source stays untouched; all triangle artwork falls outside it.
        if let url = Bundle.main.url(forResource: "EndfieldIndustriesSource", withExtension: "png"),
           let image = NSImage(contentsOf: url) {
            let viewport = CALayer(); viewport.frame = industryWordmark.bounds; viewport.masksToBounds = true
            let source = CALayer(); let scale: CGFloat = 150 / 430
            source.frame = CGRect(x: 0, y: -142 * scale, width: 150, height: 372 * scale)
            source.contents = image; source.contentsGravity = .resize
            viewport.addSublayer(source); industryWordmark.mask = viewport
        } else { industryWordmark.isHidden = true }
        corePlane.content.addSublayer(industryWordmark)
        legacyWordmarkMask = industryWordmark.mask
    }

    private func updateLegacyCenterLogo() {
        guard !usesSourceShell else { return }
        let key = configuration.centerLogo.rawValue + ":" + (configuration.centerLogoRevision ?? "")
        guard key != legacyLogoKey else { return }
        legacyLogoKey = key
        let bitmap = configuration.centerLogo.image(revision: configuration.centerLogoRevision).flatMap(HUDCenterLogoPresentation.image)
        withoutActions {
            self.industryWordmark.contents = bitmap
            let size = bitmap.map { HUDCenterLogoPresentation.displaySize(for: $0, height: 29) }
                ?? CGSize(width: 150, height: 29)
            self.industryWordmark.frame = CGRect(x: 500 - size.width / 2, y: 544, width: size.width, height: size.height)
            // Each choice matches the fallback default's height and center;
            // its visible width follows the original artwork's proportions.
            self.industryWordmark.contentsGravity = .resize
            self.industryWordmark.mask = bitmap == nil ? self.legacyWordmarkMask : nil
            self.industryWordmark.backgroundColor = bitmap == nil ? NSColor.white.cgColor : NSColor.clear.cgColor
        }
    }

    private func buildNavigation() {
        panelsPlane.content.addSublayer(navigation.layer)
        panelsPlane.content.addSublayer(identityCard.layer)
        navigation.onLayoutChange = { [weak self] in
            self?.updateNavigationGeometry()
            self?.updateButtonStates()
        }
        synchronizeNavigationButtons()
    }

    private func installLegacyNavigationControlsIfNeeded() {
        guard closeHUDButton.superview == nil else { return }
        navigation.prepareArtwork()
        identityCard.prepareArtwork()
        closeHUDButton.title = ""; closeHUDButton.isBordered = false
        closeHUDButton.target = self; closeHUDButton.action = #selector(closeFromIdentity)
        closeHUDButton.acceptsPoint = { [weak self] point in
            guard let self, let local = self.navigationPoint(self.designPoint(point)) else { return false }
            if self.notesWorkspaceIsInteractive, let note = self.notesWorkspacePoint(point),
               self.notesCanvas.containsWorkspacePoint(note) { return false }
            return self.identityCard.target(at: local) == .close
        }
        closeHUDButton.projectedAccessibilityFrame = { [weak self] in
            guard let self, let window = self.window else { return nil }
            let transform = self.panelsPlane.spatial.presentation()?.transform ?? self.panelsPlane.spatial.transform
            let rect = self.viewRect(self.projectedBounds(self.identityCard.closeRect, through: [transform]))
            return window.convertToScreen(self.convert(rect, to: nil))
        }
        addSubview(closeHUDButton)
        for direction in [-1, 1] {
            let button = HUDNavigationScrollButton(frame: .zero)
            button.tag = direction; button.title = ""; button.isBordered = false
            button.target = self; button.action = #selector(scrollNavigation(_:))
            button.projectedFrame = { [weak self] in
                guard let self, let window = self.window else { return .zero }
                let transform = self.panelsPlane.spatial.presentation()?.transform ?? self.panelsPlane.spatial.transform
                let rect = direction < 0 ? self.navigation.scrollUpRect : self.navigation.scrollDownRect
                return window.convertToScreen(self.convert(self.viewRect(self.projectedBounds(rect, through: [transform])), to: nil))
            }
            addSubview(button); navigationScrollButtons[direction] = button
        }
    }

    private func appNavigationPresentations() -> [HUDAppShortcutPresentation] {
        (appShortcutStore?.items ?? []).map { item in
            HUDAppShortcutPresentation(id: item.id, name: item.name, iconPreset: item.iconPreset,
                icon: item.iconPreset == .original ? appShortcutStore?.icon(for: item.id) : nil)
        }
    }

    /// `layout` defaults to the saved one, or the order a drag on the HUD previews.
    private func refreshAppNavigation(animated: Bool, presentations: [HUDAppShortcutPresentation]? = nil,
                                      layout: HUDModuleLayout? = nil) {
        let items = presentations ?? appNavigationPresentations()
        let layout = layout ?? arrangedLayout
        // The source row pool owns live app navigation. Populate the retained
        // fallback's native cards only when that shell is actually in use.
        navigation.updateAppShortcuts(usesSourceShell ? [] : items, animated: animated && !usesSourceShell, layout: layout)
        sourceWatch?.setDesktopNavigation(HUDDesktopWatchNavigation.entries(shortcuts: items, layout: layout))
        synchronizeNavigationButtons()
        updateNavigationGeometry()
        updateButtonStates()
    }

    private func synchronizeNavigationButtons() {
        let entries = navigation.entries.filter { sourceWatchFailureReason != nil || $0.module == .power }
        let targets = Set(entries.map(\.target))
        for target in Array(navigationButtons.keys) where !targets.contains(target) {
            navigationButtons.removeValue(forKey: target)?.removeFromSuperview()
        }
        for entry in entries where navigationButtons[entry.target] == nil {
            let button = HUDNavigationHitButton(navigationTarget: entry.target)
            button.title = ""
            button.isBordered = false
            button.bezelStyle = .regularSquare
            button.target = self
            button.action = #selector(activateNavigationButton(_:))
            button.isEnabled = false
            if entry.module == .power {
                button.setAccessibilityHelp(L10n.text("Open battery menu", "打开电池菜单"))
            }
            button.acceptsPoint = { [weak self] point in
                guard let self = self else { return false }
                if self.notesWorkspaceIsInteractive, let local = self.notesWorkspacePoint(point),
                   self.notesCanvas.containsWorkspacePoint(local) { return false }
                return self.navigationTargetAtDesignPoint(self.designPoint(point)) == entry.target
            }
            button.projectedAccessibilityFrame = { [weak self] in
                guard let self = self, let window = self.window else { return nil }
                guard let visible = entry.module == .power ? self.displayedChargeHitRect : self.navigation.clippedRect(for: entry) else { return nil }
                let plane = entry.module == .power ? self.chargePlane : (entry.group == .bottom ? self.corePlane : self.panelsPlane)
                let transform = plane.spatial.presentation()?.transform ?? plane.spatial.transform
                let rect = self.viewRect(self.projectedBounds(visible, through: [transform]))
                return window.convertToScreen(self.convert(rect, to: nil))
            }
            addSubview(button)
            navigationButtons[entry.target] = button
        }
    }

    @objc private func closeFromIdentity() { presentQuitConfirmation() }

    /// ⏻ asks with the confirm bar on the disc (spec §2.18), like every other
    /// confirmation; Esc or Cancel keep the HUD, Enter or Quit quit.
    func presentQuitConfirmation() {
        guard allowsInputBesidePlacement, window != nil else { return }
        if accountGauge.isPopoverOpen { accountGauge.dismiss(); return }
        clearControlHighlights()
        navigation.hover(nil); identityCard.resetInteraction(animated: true)
        quitPromptOpen = true
        designContext.confirm(HUDConfirmRequest(id: "quit", title: L10n.text("Quit EndfieldHUD?", "退出 EndfieldHUD？"),
                                                message: L10n.text("Notes, shelf files and settings are saved.", "便笺、暂存文件与设置均已保存。"),
                                                confirmTitle: L10n.text("Quit", "退出"), danger: true, icon: .power),
                              onConfirm: { [weak self] in self?.confirmQuit() },
                              onCancel: { [weak self] in self?.quitPromptOpen = false; self?.window?.makeFirstResponder(self) })
        window?.makeFirstResponder(self)
    }

    private func cancelQuitConfirmation() {
        guard quitPromptOpen, !quitCommitted else { return }
        designContext.overlay.decide(.cancel)
    }

    private func confirmQuit() {
        guard quitPromptOpen, !quitCommitted else { return }
        quitPromptOpen = false
        quitCommitted = true
        // Accept synchronously so focus loss cannot cancel a queued quit. The
        // owner's normal closing path removes the HUD.
        onQuitConfirmed?()
    }

    var quitConfirmationVisibleForVerification: Bool { quitPromptOpen }
    func answerQuitConfirmationForVerification(_ confirm: Bool) {
        designContext.overlay.decide(confirm ? .confirm : .cancel)
    }

    @objc private func scrollNavigation(_ sender: NSButton) {
        guard allowsModuleInput, sender.isEnabled else { return }
        navigation.scrollRows(sender.tag)
    }

    private func updateButtonStates() {
        accountController.setVisible(window != nil && interactionEnabled && !retracting, accountModule: selectedModule == .account)
        updateAccountPresentation()
        notesInteraction?.setVisible(window != nil && interactionEnabled)
        notesInteraction?.setActive(notesWorkspaceIsInteractive)
        // Presentation changes come first: Now Playing retires the shared audio
        // observer before Volume's page, activated below, starts its own.
        let presenting = window != nil && !retracting && modulePresentationAllowed
        nowPlayingCanvas.setPresented(presenting && selectedModule == .nowPlaying)
        mediaAssemblyCanvas.setPresented(presenting && selectedModule == .mediaAssembly)
        calendarCanvas.setPresented(presenting && selectedModule == .calendar)
        for (module, input) in settingsInteractions {
            input.setActive(allowsModuleInput && !moduleContent.isTransitioning && selectedModule == module)
        }
        // A cancelled presentation (cancelAnimations) keeps every module idle
        // until showStable or new interaction allows presentation again.
        let designInput = window != nil && modulePresentationAllowed && allowsModuleInput && !moduleContent.isTransitioning
        for (module, canvas) in designCanvases { canvas.setActive(designInput && selectedModule == module) }
        if !allowsModuleInput { designContext.overlay.reset() }
        layoutDesignAccessibility()
        updateNavigationButtonStates()
    }

    /// The quit, module and scroll buttons, native or drawn by the source
    /// watch, and the clock's controls; disabled while HUD elements are placed.
    private func updateNavigationButtonStates() {
        let allowsInput = allowsInputBesidePlacement
        layoutClockControls()
        sourceWatch?.inputEnabled = usesSourceShell && sourceOverviewPresented && allowsInput
        closeHUDButton.isHidden = usesSourceShell
        closeHUDButton.isEnabled = allowsInput && !usesSourceShell
        closeHUDButton.setAccessibilityLabel(L10n.text("Quit EndfieldHUD", "退出 EndfieldHUD"))
        let visible = Set(navigation.visibleEntries.map(\.target))
        for entry in navigation.entries {
            guard let button = navigationButtons[entry.target] else { continue }
            button.isHidden = (usesSourceShell && entry.module != .power) || !visible.contains(entry.target)
            button.isEnabled = allowsInput && !button.isHidden
            button.setAccessibilityLabel(entry.navigationTitle)
            if entry.module == .power { button.setAccessibilityHelp(chargeBadge.accessibilityLabel) }
            // Arrange on HUD: the module tiles say they are dragged instead.
            if arrangingModules, entry.group == .right, entry.module.map(HUDModuleLayout.configurable.contains) == true {
                button.setAccessibilityHelp(Self.arrangeHelp)
            } else if button.accessibilityHelp() == Self.arrangeHelp { button.setAccessibilityHelp(nil) }
            button.setAccessibilityValue(entry.isSelected ? L10n.text("Selected", "已选择") : L10n.text("Not selected", "未选择"))
        }
        for (direction, button) in navigationScrollButtons {
            button.isHidden = usesSourceShell
            button.isEnabled = allowsInput && (direction < 0 ? navigation.canScrollUp : navigation.canScrollDown)
            button.setAccessibilityLabel(direction < 0 ? L10n.text("Scroll modules up", "向上滚动模块") : L10n.text("Scroll modules down", "向下滚动模块"))
        }
    }

    /// Overlay hit-testing uses the displayed projective planes, including the
    /// selection lift, instead of assuming the transformed rows are flat.
    private func navigationTargetAtDesignPoint(_ point: CGPoint, forHover: Bool = false) -> HUDNavigationTarget? {
        if let local = chargeDesignPoint(point), chargeBadge.contains(local) { return .module(.power) }
        if !usesSourceShell, let local = coreDesignPoint(point), let module = navigation.hitTestBottom(point: local) { return .module(module) }
        guard let local = navigationPoint(point) else { return nil }
        // Retain hover across the short lift corridor so a stationary pointer
        // at the resting edge cannot repeatedly enter and leave the raised face.
        if !usesSourceShell, identityCard.target(at: local) == .profile { return .module(.profile) }
        if usesSourceShell { return sourceWatch?.navigationTarget(at: sourceWatchPoint(point)) }
        let target = forHover ? navigation.hoverHitTarget(point: local, includingBottom: false)
            : navigation.hitTarget(point: local, includingBottom: false)
        return target == .module(.power) || target == .module(.profile) ? nil : target
    }

    /// A design point in the source watch view's coordinates.
    private func sourceWatchPoint(_ point: CGPoint) -> CGPoint {
        CGPoint(x: designOrigin.x + point.x * designScale, y: designOrigin.y + point.y * designScale)
    }

    private func chargeDesignPoint(_ point: CGPoint) -> CGPoint? {
        let transform = chargePlane.spatial.presentation()?.transform ?? chargePlane.spatial.transform
        guard let local = Self.unproject(CGPoint(x: point.x - 500, y: point.y - 320), transform: transform) else { return nil }
        return CGPoint(x: local.x + 500, y: local.y + 320 - chargeSourceOffset)
    }

    private func navigationPoint(_ point: CGPoint) -> CGPoint? {
        let transform = panelsPlane.spatial.presentation()?.transform ?? panelsPlane.spatial.transform
        let centered = CGPoint(x: point.x - 500, y: point.y - 320)
        guard let local = Self.unproject(centered, transform: transform) else { return nil }
        return CGPoint(x: local.x + 500, y: local.y + 320)
    }

    private func updateNavigationGeometry() {
        guard !navigationButtons.isEmpty else { return }
        // The pointer range is bounded. Reserve each native button's complete
        // motion envelope at layout time instead of resizing 14 NSViews on
        // every mouse event. Exact clicks still use the presentation transform.
        let samples: [CGFloat] = [-1, -0.5, 0, 0.5, 1]
        let transforms = samples.flatMap { y in samples.map { x in
            HUDMotionMath.transform(normalizedPoint: CGPoint(x: x, y: y),
                depth: panelsPlane.depth, travel: panelsPlane.travel,
                parallaxIntensity: CGFloat(configuration.parallaxIntensity), perspectiveIntensity: CGFloat(configuration.perspectiveIntensity))
        } } + [CATransform3DIdentity]
        for entry in navigation.entries {
            guard let button = navigationButtons[entry.target] else { continue }
            let interaction = navigation.nativeHitRect(for: entry)
            guard !interaction.isNull, !interaction.isEmpty else { button.frame = .zero; continue }
            let rect = interaction.insetBy(dx: -12, dy: -10)
            let applicable = (entry.group == .bottom || entry.module == .power) ? samples.flatMap { y in samples.map { x in
                HUDMotionMath.transform(normalizedPoint: CGPoint(x: x, y: y), depth: corePlane.depth, travel: corePlane.travel,
                    parallaxIntensity: CGFloat(configuration.parallaxIntensity), perspectiveIntensity: CGFloat(configuration.perspectiveIntensity))
            } } + [CATransform3DIdentity] : transforms
            let envelope = projectedBounds(rect, through: applicable).insetBy(dx: -8, dy: -8)
            let frame = viewRect(envelope)
            if button.frame != frame { button.frame = frame }
        }
        if closeHUDButton.superview != nil {
            closeHUDButton.frame = viewRect(projectedBounds(identityCard.closeRect.insetBy(dx: -10, dy: -10), through: transforms))
        }
        for (direction, button) in navigationScrollButtons {
            let rect = direction < 0 ? navigation.scrollUpRect : navigation.scrollDownRect
            button.frame = viewRect(projectedBounds(rect, through: transforms))
        }
    }

    private func projectedBounds(_ rect: CGRect, through transforms: [CATransform3D]) -> CGRect {
        let corners = [CGPoint(x: rect.minX, y: rect.minY), CGPoint(x: rect.maxX, y: rect.minY),
                       CGPoint(x: rect.maxX, y: rect.maxY), CGPoint(x: rect.minX, y: rect.maxY)]
        var minX = CGFloat.greatestFiniteMagnitude, minY = minX
        var maxX = -minX, maxY = -minX
        for transform in transforms {
            for corner in corners {
                let p = HUDMotionMath.project(CGPoint(x: corner.x - 500, y: corner.y - 320), through: transform)
                minX = min(minX, p.x + 500); maxX = max(maxX, p.x + 500)
                minY = min(minY, p.y + 320); maxY = max(maxY, p.y + 320)
            }
        }
        return CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
    }

    private static func unproject(_ point: CGPoint, transform t: CATransform3D) -> CGPoint? {
        // Solve the 2D homography with perspective divide; a 4D inverse at z=0
        // would not recover a point from a tilted plane.
        let a = t.m11 - point.x * t.m14, b = t.m21 - point.x * t.m24
        let c = t.m12 - point.y * t.m14, d = t.m22 - point.y * t.m24
        let x = point.x * t.m44 - t.m41, y = point.y * t.m44 - t.m42
        let determinant = a * d - b * c
        guard determinant.isFinite, abs(determinant) > 0.00001 else { return nil }
        let result = CGPoint(x: (x * d - b * y) / determinant, y: (a * y - x * c) / determinant)
        return result.x.isFinite && result.y.isFinite ? result : nil
    }

    private func updateContent() {
        withoutActions {
            let dark: Bool
            switch self.configuration.theme {
            case .dark: dark = true
            case .light: dark = false
            case .system: dark = self.effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
            }
            let foreground = dark ? NSColor(white: 0.94, alpha: 1) : NSColor(white: 0.12, alpha: 1)
            let muted = dark ? NSColor(white: 0.66, alpha: 1) : NSColor(white: 0.39, alpha: 1)
            let selectedColor = self.configuration.accentColor
            let yellow = dark ? selectedColor : selectedColor.blended(withFraction: 0.35, of: .black) ?? selectedColor
            let contrast = dark ? NSColor.black : NSColor.white
            self.moduleContrast.colors = [contrast.withAlphaComponent(0.58).cgColor,
                contrast.withAlphaComponent(0.40).cgColor, contrast.withAlphaComponent(0).cgColor]
            self.backdrop.backgroundColor = (dark ? NSColor(white: 0.015, alpha: CGFloat(self.configuration.backgroundDarkness))
                : NSColor(white: 0.90, alpha: CGFloat(self.configuration.backgroundDarkness))).cgColor
            let blur = self.configuration.lowPowerVisualMode ? 0 : self.configuration.blurAmount
            self.backgroundBlur.alphaValue = CGFloat(blur)
            self.backgroundBlur.isHidden = blur == 0
            self.backgroundBlur.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
            self.vignette.colors = [NSColor.clear.cgColor,
                NSColor.black.withAlphaComponent(dark ? 0.06 : 0.02).cgColor,
                NSColor.black.withAlphaComponent(dark ? 0.38 : 0.14).cgColor]
            for label in self.primaryTexts { label.foregroundColor = foreground.cgColor }
            for label in self.mutedTexts { label.foregroundColor = muted.cgColor }
            for label in self.accentTexts { label.foregroundColor = yellow.cgColor }
            let tone: NSColor
            switch self.snapshot.levelTone {
            case .green?: tone = dark ? NSColor(srgbRed: 0.30, green: 0.90, blue: 0.50, alpha: 1) : NSColor(srgbRed: 0.04, green: 0.48, blue: 0.21, alpha: 1)
            case .yellow?: tone = dark ? NSColor(srgbRed: 0.98, green: 0.83, blue: 0.12, alpha: 1) : NSColor(srgbRed: 0.65, green: 0.47, blue: 0.03, alpha: 1)
            case .red?: tone = dark ? NSColor(srgbRed: 1, green: 0.32, blue: 0.31, alpha: 1) : NSColor(srgbRed: 0.72, green: 0.13, blue: 0.12, alpha: 1)
            case nil: tone = muted
            }
            self.currentDark = dark
            self.sourceWatch?.desktopDark = dark
            self.currentAccent = yellow
            self.updateLegacyCenterLogo()
            self.updateStatusPanel()
            self.currentBatteryTone = tone
            self.artwork?.update(dark: dark, chargeColor: self.selectedModule == .power ? tone : yellow, accentColor: yellow)
            let contentScale = self.hudRasterScale
            self.navigation.update(dark: dark, accent: yellow, contentsScale: contentScale)
            self.chargeBadge.update(snapshot: self.snapshot, configuration: self.configuration, dark: dark, contentsScale: contentScale)
            self.identityCard.update(dark: dark, accent: yellow, contentsScale: contentScale)
            self.notesCanvas.updateAppearance(style: HUDModuleContentStyle(dark: dark, accent: yellow,
                contentsScale: self.window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2))
            self.updateButtonStates()
            self.progress.strokeColor = (self.selectedModule == .power ? tone : yellow).cgColor
            self.progress.isHidden = self.usesSourceShell || self.selectedModule == .workMode
            self.progress.strokeEnd = self.selectedModule == .power ? CGFloat(self.snapshot.percentage ?? 0) / 100 : 0.12
            self.updateWorkPresentation(force: true)
            self.hintLabel.string = "ESC / \(self.configuration.summonShortcut.displayName.uppercased()) / " + L10n.text("CLICK OUTSIDE TO CLOSE", "点击外侧关闭")
            // Display > Close hint. The line sits alone below
            // the design box, so hiding it leaves no gap to close.
            self.hintLabel.isHidden = !self.configuration.showsCloseHint
            self.setAccessibilityLabel(L10n.text("System interface", "系统界面") + ", " + self.selectedModule.title)
            self.updateContentsScale()
        }
    }

    private enum TextRole { case primary, muted, accent }
    private func text(_ value: String, rect: CGRect, size: CGFloat, parent: CALayer,
                      role: TextRole, alignment: CATextLayerAlignmentMode = .left,
                      weight: HUDFontWeight = .regular, tabular: Bool = false) -> HUDStandardTextLayer {
        let label = HUDStandardTextLayer(size: size, weight: weight, tabular: tabular)
        label.frame = rect; label.string = value
        label.alignmentMode = alignment; label.truncationMode = .end
        label.isWrapped = false
        parent.addSublayer(label)
        switch role {
        case .primary: primaryTexts.append(label)
        case .muted: mutedTexts.append(label)
        case .accent: accentTexts.append(label)
        }
        return label
    }

    private static func arc(center: CGPoint, radius: CGFloat, start: CGFloat, end: CGFloat) -> CGPath {
        let p = CGMutablePath(); p.addArc(center: center, radius: radius, startAngle: start, endAngle: end, clockwise: false)
        return p
    }

    private static func tiltedTransform(scale: CGFloat, depth: CGFloat = 0, degrees: CGFloat = 70,
                                        perspective: Bool = true) -> CATransform3D {
        var result = CATransform3DIdentity
        if perspective { result.m34 = -1 / 1100 }
        result = CATransform3DTranslate(result, 0, 0, depth)
        result = CATransform3DRotate(result, degrees * .pi / 180, 1, 0, 0)
        return CATransform3DScale(result, scale, scale, scale)
    }

    private func animateLift(_ item: CALayer, depth: CGFloat, duration: TimeInterval, delay: TimeInterval) {
        let animation = CAKeyframeAnimation(keyPath: "transform")
        animation.values = [
            Self.tiltedTransform(scale: 0.20, depth: depth, degrees: 72),
            Self.tiltedTransform(scale: 0.76, depth: depth * 0.65, degrees: 62),
            Self.tiltedTransform(scale: 1, depth: depth * 0.2, degrees: 27),
            CATransform3DIdentity
        ].map { NSValue(caTransform3D: $0) }
        animation.keyTimes = [0, 0.32, 0.70, 1]
        animation.timingFunctions = [
            CAMediaTimingFunction(controlPoints: 0.2, 0.5, 0.45, 1),
            CAMediaTimingFunction(name: .easeInEaseOut), CAMediaTimingFunction(name: .easeOut)
        ]
        animation.duration = duration
        animation.beginTime = item.convertTime(CACurrentMediaTime(), from: nil) + delay
        animation.fillMode = .backwards
        item.add(animation, forKey: "deployment.transform")
    }

    private func animateRetraction(_ item: CALayer, from: CATransform3D, to: CATransform3D,
                                   duration: TimeInterval, delay: TimeInterval, perspective: Bool = true) {
        let animation = CAKeyframeAnimation(keyPath: "transform")
        animation.values = [NSValue(caTransform3D: from),
            NSValue(caTransform3D: Self.tiltedTransform(scale: 0.88, depth: -24, degrees: 53, perspective: perspective)),
            NSValue(caTransform3D: to)]
        animation.keyTimes = [0, 0.58, 1]
        animation.timingFunctions = [CAMediaTimingFunction(name: .easeInEaseOut), CAMediaTimingFunction(name: .easeIn)]
        animation.duration = duration
        animation.beginTime = item.convertTime(CACurrentMediaTime(), from: nil) + delay
        animation.fillMode = .backwards
        item.add(animation, forKey: "deployment.transform")
    }

    private func enter(_ item: CALayer, delay: TimeInterval, duration: TimeInterval) {
        animate(item, "opacity", from: 0, to: 1, duration: duration, delay: delay)
        animate(item, "transform", from: CATransform3DMakeTranslation(0, -26, 0),
                to: CATransform3DIdentity, duration: duration, delay: delay)
    }

    private func animateBlur(from: Float, to: Float, duration: TimeInterval) {
        guard let blurLayer = blurBackdrop.layer else { return }
        let fade = CABasicAnimation(keyPath: "opacity")
        fade.fromValue = from; fade.toValue = to; fade.duration = duration
        fade.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        blurLayer.add(fade, forKey: "deployment.opacity")
    }

    private func animateSourceBlur(_ track: HUDSourceWatchBlurAnimation.Track) {
        guard let blurLayer = blurBackdrop.layer else { return }
        let fade = CABasicAnimation(keyPath: "opacity")
        fade.fromValue = track.startAlpha; fade.toValue = track.endAlpha
        fade.duration = track.duration
        let c = track.controlPoints
        fade.timingFunction = CAMediaTimingFunction(controlPoints: c.x, c.y, c.z, c.w)
        blurLayer.add(fade, forKey: "deployment.opacity")
        withoutActions { self.backdrop.opacity = Float(track.endAlpha) }
        backdrop.add(fade, forKey: "deployment.opacity")
    }

    private func animate(_ item: CALayer, _ key: String, from: Any, to: Any,
                         duration: TimeInterval, delay: TimeInterval = 0) {
        let animation = CABasicAnimation(keyPath: key)
        // Box transforms explicitly for Core Animation's Objective-C boundary.
        animation.fromValue = (from as? CATransform3D).map { NSValue(caTransform3D: $0) } ?? from
        animation.toValue = (to as? CATransform3D).map { NSValue(caTransform3D: $0) } ?? to
        animation.duration = duration
        animation.beginTime = item.convertTime(CACurrentMediaTime(), from: nil) + delay
        animation.timingFunction = CAMediaTimingFunction(controlPoints: 0.20, 0.72, 0.22, 1)
        animation.fillMode = .backwards
        item.add(animation, forKey: "deployment." + key)
    }

    private func updateContentsScale() {
        let scale = hudRasterScale
        layoutDesignOverlay()
        layoutFacilityLayers()
        designContext.update(unit: designUnit, renderScale: scale)
        // Keep the factory's scale current too, so a page created after layout
        // uses the same glyph resolution as pages already on screen.
        moduleContent?.update(dark: currentDark, accent: currentAccent, contentsScale: scale)
        func apply(_ item: CALayer, magnification: CGFloat) {
            let magnification = magnification * overlayMagnification(of: item)
            if !(item is CATransformLayer) {
                let target = HUDRenderScale.contentScale(for: item, baseScale: scale, magnification: magnification)
                if item.contentsScale != target { item.contentsScale = target }
            }
            item.sublayers?.forEach { apply($0, magnification: magnification) }
            if let mask = item.mask { apply(mask, magnification: magnification) }
        }
        apply(canvas, magnification: 1)
        apply(topLayout, magnification: 1)
        chargeBadge.update(snapshot: snapshot, configuration: configuration, dark: currentDark, contentsScale: scale)
        identityCard.update(dark: currentDark, accent: currentAccent, contentsScale: scale)
        // HUD Scale, layout and display changes redraw the gauge's text and icon now.
        accountGauge.setScale(scale)
        notesCanvas.setWorkspaceBackingScale(window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2)
    }

    private func configureNotesInteraction(shelf: FileShelfStore?) {
        let input = HUDNotesInteraction(canvas: notesCanvas, host: self)
        input.workspaceProject = { [weak self] rect in self?.projectNotesRect(rect) ?? .zero }
        input.workspaceUnproject = { [weak self] point in self?.notesWorkspacePoint(point) }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        input.onToggle = { [weak self] in self?.onToggle?() }
        input.isDark = { [weak self] in self?.currentDark ?? true }
        notesCanvas.onAction = { [weak self] action in
            self?.eventLog.record(kind: .noteAction, metadata: ["action": action.rawValue])
        }
        input.onChooseShelfMedia = { [weak self, weak input, weak shelf] point in
            guard let self, let input else { return }
            let choices = Self.documentShelfChoices(shelf: shelf, extensions: Set(NotesMediaFactory.supportedFileExtensions))
            input.presentShelfMedia(choices: choices, at: point) { [weak self, weak input, weak shelf] id in
                guard let shelf, let input else { return }
                do {
                    let access = try shelf.access(id: id)
                    defer { access.close() }
                    // importMedia starts its own scoped access before dispatch.
                    input.importMedia(urls: [access.url], at: point)
                } catch { self?.notesCanvas.reportImportError(error) }
            }
        }
        notesInteraction = input
    }

    private func configureSettingsInteraction() {
        guard settingsController != nil else { return }
        for (module, canvas) in settingsCanvases {
            let input = HUDSettingsInteraction(canvas: canvas, host: self)
            input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
            settingsInteractions[module] = input
        }
        if window != nil { installSettingsCaptureMonitor() }
    }

    private func removeSettingsCaptureMonitor() {
        if let settingsCaptureMonitor { NSEvent.removeMonitor(settingsCaptureMonitor) }
        settingsCaptureMonitor = nil
    }

    private func installSettingsCaptureMonitor() {
        guard settingsCaptureMonitor == nil, settingsController != nil else { return }
        // Recording is local to this visible HUD. Consume the chord before
        // menu key equivalents such as Cmd-Q can act; no global key tap.
        settingsCaptureMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self, event.window === self.window, self.window?.isVisible == true,
                  let input = self.settingsInteraction, input.isCapturingShortcut else { return event }
            _ = input.keyDown(event)
            return nil
        }
    }

    /// Notes are stored in screen points, independently of the HUD's scale.
    /// Project through the same displayed plane for drawing, accessibility and
    /// native editors; invert that homography for direct manipulation.
    private func projectNotesRect(_ rect: CGRect) -> CGRect {
        let transform = notesPlane.spatial.presentation()?.transform ?? notesPlane.spatial.transform
        let designRect = CGRect(origin: designPoint(rect.origin),
                                size: CGSize(width: rect.width / designScale, height: rect.height / designScale))
        return viewRect(projectedBounds(designRect, through: [transform]))
    }

    private func notesWorkspacePoint(_ point: CGPoint) -> CGPoint? {
        let transform = notesPlane.spatial.presentation()?.transform ?? notesPlane.spatial.transform
        let design = designPoint(point)
        guard let local = Self.unproject(CGPoint(x: design.x - 500, y: design.y - 320), transform: transform) else { return nil }
        return CGPoint(x: designOrigin.x + (local.x + 500) * designScale,
                       y: designOrigin.y + (local.y + 320) * designScale)
    }

    private func configureShelfInteraction(store: FileShelfStore?) {
        let input = HUDFileShelfInteraction(canvas: shelfCanvas, store: store, host: self)
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        input.onDragSessionBegan = { [weak self] in self?.onShelfDragSessionBegan?() }
        input.onDragSessionWillBegin = { [weak self] in self?.sourceWatch?.refreshSourceCursor() }
        input.onDragSessionEnded = { [weak self] delivered in self?.onShelfDragSessionEnded?(delivered) }
        input.onRevealRequested = { [weak self] url in self?.onShelfReveal?(url) }
        shelfInteraction = input
    }

    private func coreDesignPoint(_ point: CGPoint) -> CGPoint? {
        let transform = corePlane.spatial.presentation()?.transform ?? corePlane.spatial.transform
        guard let local = Self.unproject(CGPoint(x: point.x - 500, y: point.y - 320), transform: transform) else { return nil }
        return CGPoint(x: local.x + 500, y: local.y + 320)
    }

    /// View point → redesigned content-box px through the live plane.
    private func designBoxPoint(_ location: CGPoint) -> CGPoint? {
        guard let local = coreDesignPoint(designPoint(location)) else { return nil }
        let box = HUDDesignGeometry.boxRect(unit: designUnit)
        return CGPoint(x: (local.x - box.minX) / designUnit, y: (local.y - box.minY) / designUnit)
    }
    /// Content-box px rect → bounds of its projection in view coordinates.
    private func projectDesignBoxRect(_ rect: CGRect) -> CGRect {
        let transform = corePlane.spatial.presentation()?.transform ?? corePlane.spatial.transform
        let box = HUDDesignGeometry.boxRect(unit: designUnit), u = designUnit
        let design = CGRect(x: box.minX + rect.minX * u, y: box.minY + rect.minY * u, width: rect.width * u, height: rect.height * u)
        return viewRect(projectedBounds(design, through: [transform]))
    }
    /// Every centre screen registers here; converted modules (HUDDesignCanvas)
    /// are also routed through the shared design path.
    private func registerModule(_ factory: HUDModuleContentFactory, for module: HUDModule) {
        if let design = factory as? HUDDesignCanvas { designCanvases[module] = design }
        moduleContent.register(factory, for: module)
    }
    private func register(design canvas: HUDDesignCanvas) { registerModule(canvas, for: canvas.module) }
    private func configureDesignContext() {
        designContext.host = self
        designContext.project = { [weak self] in self?.projectDesignBoxRect($0) ?? $0 }
        designContext.unproject = { [weak self] in self?.designBoxPoint($0) }
        designContext.onInputStateChange = { [weak self] in self?.designInputStateChanged() }
        designContext.onLockParallax = { [weak self] in self?.lockParallaxForActiveInput() }
        designContext.overlay.onChange = { [weak self] in self?.designInputStateChanged() }
        for standIns in [designStandIns, designOverlayStandIns] {
            standIns.project = { [weak self] in self?.projectDesignBoxRect($0) ?? $0 }
        }
        designStandIns.onPerform = { [weak self] in self?.activeDesignCanvas?.performAccessibilityAction($0) }
        designStandIns.onAdjust = { [weak self] in self?.activeDesignCanvas?.adjustAccessibilitySlider($0, value: $1) }
        designOverlayStandIns.onPerform = { [weak self] id in
            self?.designContext.overlay.router.controls.first { $0.id == id }?.activate()
        }
    }
    /// The overlay follows the box: same origin and scale on the core plane.
    private func layoutDesignOverlay() {
        let box = HUDDesignGeometry.boxRect(unit: designUnit)
        withoutActions {
            let overlay = designContext.overlay.layer
            overlay.anchorPoint = .zero
            overlay.position = box.origin
            overlay.transform = CATransform3DMakeScale(designUnit, designUnit, 1)
            // The HUD elements' editor strip shares the overlay's box coordinates.
            overlayEditorHost.anchorPoint = .zero
            overlayEditorHost.position = box.origin
            overlayEditorHost.transform = overlay.transform
        }
        moduleContent?.designUnit = designUnit
        layoutOverlayEditor()
    }
    private func designInputStateChanged() {
        layoutDesignAccessibility()
        HUDControlHighlightLayer.requestRefresh(on: self)
    }
    private func layoutDesignAccessibility() {
        let overlay = designContext.overlay
        if let canvas = activeDesignCanvas, !overlay.capturesInput, overlayEditor == nil { designStandIns.update(canvas.accessibilityItems) }
        else { designStandIns.hideAll() }
        if allowsModuleInput, overlay.capturesInput { designOverlayStandIns.update(overlay.router.accessibilityItems(in: overlay.layer)) }
        else { designOverlayStandIns.hideAll() }
        activeDesignCanvas?.refreshTextFieldProjection()
        if allowsInputBesidePlacement, facilityBanner.isShowing { facilityBannerStandIns.update(facilityBannerRouter.accessibilityItems(in: facilityBanner.layer)) }
        else { facilityBannerStandIns.hideAll() }
        if allowsInputBesidePlacement { facilityTagStandIns.update(facilityTagRouter.accessibilityItems(in: facilityTags.layer)) }
        else { facilityTagStandIns.hideAll() }
        if allowsModuleInput, let editor = overlayEditor { overlayEditorStandIns.update(editor.accessibilityItems(in: overlayEditorHost)) }
        else { overlayEditorStandIns.hideAll() }
    }

    // MARK: HUD elements (Display > HUD elements)

    /// Display > HUD elements > Edit position and size: the strip beside the
    /// elements sets their sizes; layers exist only while placing.
    private var overlayEditor: HUDPlacementEditor?
    /// The layout being placed; nil when not placing.
    private var overlayDraft: HUDOverlayLayout?
    private var overlayDragged: HUDOverlayLayout.Element?
    /// The element the strip sits beside: the last one dragged (nil: all three).
    private var overlayFocus: HUDOverlayLayout.Element?
    private let overlayEditorHost = CALayer()
    private let overlayFrames = CAShapeLayer()
    private lazy var overlayEditorStandIns: HUDAccessibilityStandIns = {
        let standIns = HUDAccessibilityStandIns(host: self)
        standIns.project = { [weak self] in self?.projectDesignBoxRect($0) ?? $0 }
        standIns.onPerform = { [weak self] id in self?.overlayEditor?.perform(id) }
        standIns.onAdjust = { [weak self] id, value in self?.overlayEditor?.adjust(id, to: value) }
        return standIns
    }()
    var isPlacingOverlayForVerification: Bool { overlayEditor != nil }
    /// The top plane sits above the canvas (the clock), pinned notes and
    /// below pointer feedback, in the core plane's pose and placement.
    private var topPlaneMatchesCoreForVerification: Bool {
        topLayout.superlayer === layer && topPlane.deployment.superlayer === topLayout
            && topLayout.zPosition > max(canvas.zPosition, notesWorkspace.zPosition) && topLayout.zPosition < actionFeedback.zPosition
            && CATransform3DEqualToTransform(topPlane.spatial.transform, corePlane.spatial.transform)
            && CATransform3DEqualToTransform(topLayout.transform, canvas.transform) && topLayout.position == canvas.position
    }
    /// The HUD elements strip and its marks draw on the top plane.
    var overlayEditorDrawsOnTopForVerification: Bool {
        overlayEditor != nil && overlayEditorHost.superlayer === topPlane.content
            && overlayFrames.superlayer === topPlane.content && topPlaneMatchesCoreForVerification
    }
    /// The Sanity recovery menu draws on the top plane, over the gauge.
    var accountPopoverDrawsOnTopForVerification: Bool {
        accountGauge.popoverLayer.superlayer === accountPopoverHost && accountPopoverHost.superlayer === topPlane.content
            && accountPopoverHost.position == overlayFrame(.sanity, shownOverlayLayout).origin
            && CATransform3DEqualToTransform(accountPopoverHost.transform, accountGauge.layer.transform)
            && topPlaneMatchesCoreForVerification
    }

    /// The layout on screen: the editor's draft while placing, else the saved one.
    private var shownOverlayLayout: HUDOverlayLayout { overlayDraft ?? configuration.overlayLayout }

    /// Places, sizes and fills the title, the Sanity gauge and the alert tags.
    /// Text is resized through its font size and the gauge redraws its text and
    /// icon at its size, so both stay sharp. Runs only when the layout, the
    /// language or the scale changes, or while placing.
    private func applyOverlayLayout() {
        let layout = shownOverlayLayout, origin = header.frame.origin
        withoutActions {
            let size = CGFloat(layout.title.size)
            titleLabel.string = layout.shownTitle; subtitleLabel.string = layout.shownSubtitle
            titleLabel.fontSize = 20 * size; subtitleLabel.fontSize = 9 * size
            let title = overlayFrame(.title, layout)
            let width = max(300 * size, title.width + 4)
            titleLabel.frame = CGRect(x: title.minX - origin.x, y: title.minY - origin.y, width: width, height: 27 * size)
            subtitleLabel.frame = CGRect(x: title.minX - origin.x, y: title.minY - origin.y + 31 * size, width: width, height: 18 * size)
            titleLabel.isHidden = !layout.showsTitle; subtitleLabel.isHidden = !layout.showsTitle
            let gauge = overlayFrame(.sanity, layout), scale = CGFloat(layout.sanity.size)
            accountGauge.layer.position = CGPoint(x: gauge.minX - origin.x, y: gauge.minY - origin.y)
            accountGauge.layer.transform = scale == 1 ? CATransform3DIdentity : CATransform3DMakeScale(scale, scale, 1)
            accountPopoverHost.position = gauge.origin
            accountPopoverHost.transform = accountGauge.layer.transform
        }
        accountGauge.sizeFactor = CGFloat(layout.sanity.size)
        accountGauge.popoverDirection = HUDAccountGauge.popoverDirection(gauge: overlayFrame(.sanity, layout), within: HUDOverlayGeometry.plane)
        layoutFacilityLayers()
        layoutAccountGaugeControls()
        layoutOverlayEditor()
    }

    /// How much a Display > HUD elements size enlarges `layer` by its
    /// transform (1 for every other layer), so its buffers grow with it.
    private func overlayMagnification(of layer: CALayer) -> CGFloat {
        layer === accountGauge.layer || layer === accountPopoverHost ? accountGauge.sizeFactor
            : layer === facilityTags.layer ? facilityTags.magnification : 1
    }

    /// An element's size at 100 %, in core-plane points.
    private func overlayNatural(_ element: HUDOverlayLayout.Element) -> CGSize {
        switch element {
        case .title:
            let size = max(0.01, titleLabel.fontSize / 20)
            return CGSize(width: ceil(max(titleLabel.measuredWidth, subtitleLabel.measuredWidth) / size), height: 49)
        case .sanity: return HUDAccountGauge.size
        case .alerts:
            return CGSize(width: HUDFacilityTagStack.width * designUnit,
                          height: max(HUDFacilityTagStack.tagHeight, facilityTags.preferredHeight) * designUnit)
        }
    }

    /// An element's placement as drawn: kept on the core plane at its
    /// current size and text.
    private func overlayPlacement(_ element: HUDOverlayLayout.Element, _ placement: HUDOverlayLayout.Placement) -> HUDOverlayLayout.Placement {
        HUDOverlayGeometry.clamped(element, placement, natural: overlayNatural(element))
    }

    /// Where an element is drawn: its placement, kept on the core plane.
    private func overlayFrame(_ element: HUDOverlayLayout.Element, _ layout: HUDOverlayLayout) -> CGRect {
        HUDOverlayGeometry.frame(element, overlayPlacement(element, layout[element]), natural: overlayNatural(element))
    }

    /// The element under a core-plane point; the gauge first (the smallest).
    private func overlayElement(at point: CGPoint) -> HUDOverlayLayout.Element? {
        let layout = shownOverlayLayout
        return [HUDOverlayLayout.Element.sanity, .title, .alerts].first { overlayFrame($0, layout).insetBy(dx: -6, dy: -6).contains(point) }
    }

    /// Starts placing the title, the Sanity gauge and the alert tags: drag one
    /// on the HUD, set sizes on the strip; Cancel or Esc drops the draft,
    /// Confirm or Return saves it. The rest of the HUD waits meanwhile. The
    /// strip and the marks draw above every other HUD layer, the clock included.
    func beginOverlayPlacement() {
        guard allowsModuleInput, overlayEditor == nil, !designContext.overlay.capturesInput else { return }
        activeDesignCanvas?.endEditing(commit: true)
        if activeDesignCanvas?.isMenuOpen == true { activeDesignCanvas?.closeMenu() }
        accountGauge.dismiss(animated: false)
        clearControlHighlights()
        let layout = configuration.overlayLayout
        overlayDraft = layout; overlayFocus = nil; overlayDragged = nil
        withoutActions {
            overlayFrames.name = "hud.elements.frames"; overlayFrames.zPosition = 39
            overlayEditorHost.name = "hud.elements.editor"; overlayEditorHost.zPosition = 40
            // On the top plane, so the clock, the footer, the alert banner and
            // pinned notes never cover the strip. Marks first: a snapshot
            // draws siblings in array order.
            topPlane.content.addSublayer(overlayFrames); topPlane.content.addSublayer(overlayEditorHost)
        }
        let editor = installOverlayEditor(for: layout)
        layoutDesignOverlay()
        applyOverlayLayout()
        editor.show()
        window?.makeFirstResponder(self)
        lockParallaxForActiveInput()
        designInputStateChanged()
        updateNavigationButtonStates()
    }

    /// Builds the strip for `layout` in place of the current one. Its pills,
    /// band and sliders bake their palette, so a System theme change while
    /// placing rebuilds it from the draft, as the battery bar editor does.
    @discardableResult private func installOverlayEditor(for layout: HUDOverlayLayout) -> HUDPlacementEditor {
        let range = HUDSlider.Range(minimum: HUDOverlayLayout.sizeRange.lowerBound * 100,
                                    maximum: HUDOverlayLayout.sizeRange.upperBound * 100, step: 1)
        let editor = HUDPlacementEditor(id: "hudElements", sliders: HUDOverlayLayout.Element.allCases.map {
            HUDPlacementEditor.Slider(id: $0.rawValue, title: $0.sizeTitle, value: layout[$0].size * 100, range: range, format: .percent)
        }, env: designContext.env)
        editor.onChange = { [weak self] id, value, _ in self?.overlaySizeChanged(id, value: value) }
        editor.onDecision = { [weak self] decision in self?.finishOverlayPlacement(confirm: decision == .confirm) }
        let previous = overlayEditor
        previous?.cancelPointer()
        overlayEditor = editor; overlayDragged = nil
        withoutActions {
            previous?.layer.removeFromSuperlayer()
            overlayFrames.fillColor = HUDColor.accentInk
            overlayEditorHost.addSublayer(editor.layer)
        }
        return editor
    }

    private func overlaySizeChanged(_ id: String, value: Double) {
        guard overlayDraft != nil, let element = HUDOverlayLayout.Element(rawValue: id) else { return }
        overlayDraft?[element].size = value / 100
        applyOverlayLayout()
    }

    /// Confirm saves the placements as drawn (kept on the plane); Cancel, Esc
    /// or the HUD closing drops them.
    private func finishOverlayPlacement(confirm: Bool) {
        guard let editor = overlayEditor, var draft = overlayDraft else { return }
        if confirm {
            for element in HUDOverlayLayout.Element.allCases { draft[element] = overlayPlacement(element, draft[element]) }
        }
        editor.cancelPointer()
        overlayEditor = nil; overlayDraft = nil; overlayDragged = nil; overlayFocus = nil
        withoutActions {
            editor.layer.removeFromSuperlayer(); overlayEditorHost.removeFromSuperlayer()
            overlayFrames.removeFromSuperlayer(); overlayFrames.path = nil
        }
        if confirm, draft != configuration.overlayLayout { settingsController?.update { $0.overlayLayout = draft } }
        applyOverlayLayout()
        designInputStateChanged()
        updateNavigationButtonStates()
    }

    private func cancelOverlayPlacement() { finishOverlayPlacement(confirm: false) }

    /// The corner marks around each element, and the strip beside the last
    /// one dragged (below it, or above it near the plane's bottom). The strip
    /// holds still while one of its sliders or an element has the pointer.
    private func layoutOverlayEditor() {
        guard let editor = overlayEditor else { return }
        let layout = shownOverlayLayout
        let frames = HUDOverlayLayout.Element.allCases.map { overlayFrame($0, layout).insetBy(dx: -6, dy: -6) }
        let marks = CGMutablePath()
        frames.forEach { marks.addPath(HUDArchiveTile.bracketPath($0, inset: 0)) }
        withoutActions { overlayFrames.path = marks }
        guard !editor.isAdjusting, !editor.isDragging else { return }
        let unit = designUnit, box = HUDDesignGeometry.boxRect(unit: unit)
        func local(_ rect: CGRect) -> CGRect {
            CGRect(x: (rect.minX - box.minX) / unit, y: (rect.minY - box.minY) / unit, width: rect.width / unit, height: rect.height / unit)
        }
        // Beside the last element dragged; at first beside all three while they
        // sit together, else beside the title.
        let union = frames.dropFirst().reduce(frames[0]) { $0.union($1) }
        let focus = overlayFocus ?? (union.height > HUDOverlayGeometry.plane.height / 3 ? .title : nil)
        let target = focus.map { frames[HUDOverlayLayout.Element.allCases.firstIndex(of: $0)!] } ?? union
        let size = CGSize(width: editor.size.width, height: editor.size.height + HUDPlacementEditor.bubbleClearance)
        let strip = HUDPlacementEditor.frame(size: size, beside: local(target), within: local(HUDOverlayGeometry.plane), gap: 8)
        withoutActions {
            editor.layer.frame = CGRect(x: strip.minX, y: strip.minY + HUDPlacementEditor.bubbleClearance,
                                        width: editor.size.width, height: editor.size.height)
        }
        if allowsModuleInput { overlayEditorStandIns.update(editor.accessibilityItems(in: overlayEditorHost)) }
    }

    private func overlayPointerDown(_ location: CGPoint) {
        guard let editor = overlayEditor, let draft = overlayDraft else { return }
        let far = CGPoint(x: -10_000, y: -10_000)
        let box = designBoxPoint(location) ?? far, core = coreDesignPoint(designPoint(location))
        if editor.stripContains(box, in: overlayEditorHost) {
            editor.pointerDown(at: box, in: overlayEditorHost, pointer: core ?? far, anchor: .zero)
        } else if let core, let element = overlayElement(at: core) {
            overlayDragged = element; overlayFocus = element
            // From where it is drawn: a size or text change since the last
            // drag may have pulled it in from the saved offset.
            let start = overlayPlacement(element, draft[element])
            editor.pointerDown(at: box, in: overlayEditorHost, pointer: core, anchor: CGPoint(x: start.x, y: start.y))
        }
    }

    private func overlayPointerDragged(_ location: CGPoint) {
        guard let editor = overlayEditor else { return }
        let box = designBoxPoint(location) ?? CGPoint(x: -10_000, y: -10_000), core = coreDesignPoint(designPoint(location))
        guard let proposed = editor.pointerDragged(to: box, in: overlayEditorHost, pointer: core ?? .zero),
              core != nil, let element = overlayDragged, var placement = overlayDraft?[element] else { return }
        placement.x = Double(proposed.x); placement.y = Double(proposed.y)
        overlayDraft?[element] = overlayPlacement(element, placement)
        applyOverlayLayout()
    }

    private func overlayPointerUp(_ location: CGPoint) {
        guard let editor = overlayEditor else { return }
        editor.pointerUp(at: designBoxPoint(location) ?? CGPoint(x: -10_000, y: -10_000), in: overlayEditorHost)
        overlayDragged = nil
        // The strip settles beside the element again.
        layoutOverlayEditor()
    }

    // MARK: Module arrangement (Display > Modules > Arrange on HUD)

    /// While Arrange on HUD is on, the right-hand module buttons are dragged
    /// to reorder instead of opening; saved apps and Add App do nothing.
    private var arrangingModules = false
    /// The module pressed, where, whether it has lifted, and the order it previews.
    private var arrangeDrag: (module: HUDModule, start: CGPoint, lifted: Bool, preview: HUDModuleLayout)?
    private static var arrangeHelp: String { L10n.text("Drag to reorder", "拖动以调整顺序") }
    /// The order the right-hand list shows: a lifted drag's preview, else the saved one.
    private var arrangedLayout: HUDModuleLayout {
        if let drag = arrangeDrag, drag.lifted { return drag.preview }
        return configuration.moduleLayout
    }

    private func setModuleArrangement(_ on: Bool) {
        endModuleArrangeDrag(save: false)
        guard on != arrangingModules else { return }
        arrangingModules = on
        updateArrangeMarks()
        updateNavigationButtonStates()
        if !on { updateWorkPresentation(force: true) }
        designInputStateChanged()
        sourceWatch?.refreshSourceCursor()
    }

    private func updateArrangeMarks() {
        let targets = arrangingModules ? Set(HUDModuleLayout.configurable.map { HUDNavigationTarget.module($0) }) : []
        let lifted = arrangeDrag.flatMap { $0.lifted ? HUDNavigationTarget.module($0.module) : nil }
        sourceWatch?.desktopArrangeTargets = targets
        sourceWatch?.desktopArrangeLifted = lifted
        navigation.setArrangeMarks(usesSourceShell ? [] : targets, lifted: lifted)
    }

    /// A pinned desktop note covers the view point (presses go to it).
    private func notesCover(_ location: CGPoint) -> Bool {
        notesWorkspaceIsInteractive && notesWorkspacePoint(location).map(notesCanvas.containsWorkspacePoint) == true
    }

    /// The right-hand list's tile under a view point while arranging, below
    /// menus, the confirm bar, pinned notes and the charge capsule.
    private func rightRowTarget(atView location: CGPoint) -> HUDNavigationTarget? {
        guard arrangingModules, allowsInputBesidePlacement, !moduleContent.isTransitioning, !popupCapturesPointer,
              !bootLog.isPlaying, bounds.contains(location), !notesCover(location) else { return nil }
        let p = designPoint(location)
        if let charge = chargeDesignPoint(p), chargeBadge.contains(charge) { return nil }
        let target: HUDNavigationTarget?
        if usesSourceShell {
            guard sourceOverviewPresented else { return nil }
            target = sourceWatch?.desktopRowTarget(at: sourceWatchPoint(p))
        } else {
            target = navigationPoint(p).flatMap { navigation.hitTarget(point: $0, includingBottom: false) }
                .flatMap { $0.group == .right ? $0 : nil }
        }
        return target.flatMap { HUDModuleLayout.listsTarget($0) ? $0 : nil }
    }

    /// The module button under a view point while arranging (it can be dragged).
    private func arrangeableModule(atView location: CGPoint) -> HUDModule? {
        guard let module = rightRowTarget(atView: location)?.module, HUDModuleLayout.configurable.contains(module) else { return nil }
        return module
    }

    /// Past the lift distance the pressed module follows the pointer: over
    /// another module it takes that place, over an app or Add App it goes
    /// after the last visible module. The list and the Modules page follow.
    private func dragArrangedModule(to location: CGPoint) {
        guard var drag = arrangeDrag else { return }
        if !drag.lifted {
            guard hypot(location.x - drag.start.x, location.y - drag.start.y) > HUDDragGrip.liftDistance else { return }
            drag.lifted = true; arrangeDrag = drag
            lockParallaxForActiveInput()
            updateArrangeMarks()
        }
        // Near the list's edges it scrolls toward the pointer (on pointer events only).
        if usesSourceShell { sourceWatch?.scrollDesktopNavigationTowardEdge(at: sourceWatchPoint(designPoint(location))) }
        else if let local = navigationPoint(designPoint(location)) {
            let viewport = navigation.rightViewport, band: CGFloat = 36
            if local.x >= viewport.minX, local.x <= viewport.maxX {
                if local.y < viewport.minY + band { navigation.scrollPixels(-min(12, viewport.minY + band - local.y), animated: false) }
                else if local.y > viewport.maxY - band { navigation.scrollPixels(min(12, local.y - viewport.maxY + band), animated: false) }
            }
        }
        if let next = drag.preview.arranging(drag.module, over: rightRowTarget(atView: location)) {
            drag.preview = next; arrangeDrag = drag
            refreshAppNavigation(animated: !HUDRuntimeAppearance.reduceMotion)
            settingsCanvases[.display]?.previewModuleOrder(next.order)
        }
        sourceWatch?.refreshSourceCursor()
    }

    /// Ends a press on a module button. A lifted drag saves its order (`save`)
    /// or the list returns to the saved one; a press without a lift does nothing.
    private func endModuleArrangeDrag(save: Bool) {
        guard let drag = arrangeDrag else { return }
        arrangeDrag = nil
        if drag.lifted {
            if save, drag.preview.order != configuration.moduleLayout.order, let settingsController {
                // The saved layout comes back through set(snapshot:configuration:), which rebinds the list.
                settingsController.update { $0.moduleLayout = HUDModuleLayout(order: drag.preview.order, hidden: $0.moduleLayout.hidden) }
            } else { refreshAppNavigation(animated: !HUDRuntimeAppearance.reduceMotion) }
            settingsCanvases[.display]?.previewModuleOrder(nil)
            updateArrangeMarks()
        }
    }

    // MARK: Facility alerts, the Recording mode tag and the daily boot log (spec §3.26, §3.28, §3.29)

    private lazy var facilityBanner = HUDFacilityBanner(env: designContext.env)
    private lazy var facilityTags = HUDFacilityTagStack(env: designContext.env)
    private lazy var bootLog = HUDBootLog(env: designContext.env)
    private let facilityBannerRouter = HUDInteractionRouter()
    private let facilityTagRouter = HUDInteractionRouter()
    private lazy var facilityBannerStandIns = HUDAccessibilityStandIns(host: self)
    private lazy var facilityTagStandIns = HUDAccessibilityStandIns(host: self)
    private var updatingFacility = false
    /// Set by the overlay controller before the first open of the day: the
    /// centre stays hidden until the boot log has played.
    var playsBootLogOnOpen = false {
        didSet { withoutActions { moduleContent?.layer.opacity = playsBootLogOnOpen ? 0 : 1 } }
    }

    private func installFacilityLayers() {
        facilityBanner.layer.zPosition = 1000
        canvas.addSublayer(facilityBanner.layer)
        corePlane.content.addSublayer(facilityTags.layer)
        withoutActions {
            bootLog.layer.frame = CGRect(origin: CGPoint(x: 10, y: 30), size: HUDBootLog.size)
            designContext.overlay.layer.addSublayer(bootLog.layer)
        }
        facilityBanner.onClick = { [weak self] in self?.openFacilityModule($0) }
        facilityTags.onOpen = { [weak self] in self?.openFacilityModule($0) }
        facilityTags.onDismiss = { [weak self] in
            guard let self, self.overlayEditor == nil else { return }
            self.facilityAlerts?.dismiss($0)
        }
        facilityBanner.onHidden = { [weak self] in self?.layoutDesignAccessibility() }
        bootLog.onFinish = { [weak self] in self?.finishBootLog() }
        facilityAlerts?.onChange = { [weak self] in self?.facilityChanged() }
        facilityBannerStandIns.project = { [weak self] rect in
            guard let self else { return rect }
            return self.viewRect(self.canvas.convert(rect, from: self.facilityBanner.layer))
        }
        facilityTagStandIns.project = { [weak self] rect in
            guard let self else { return rect }
            let transform = self.corePlane.spatial.presentation()?.transform ?? self.corePlane.spatial.transform
            return self.viewRect(self.projectedBounds(self.corePlane.content.convert(rect, from: self.facilityTags.layer), through: [transform]))
        }
        for standIns in [facilityBannerStandIns, facilityTagStandIns] {
            standIns.onPerform = { [weak self] id in
                guard let self else { return }
                (self.facilityBannerRouter.controls + self.facilityTagRouter.controls).first { $0.id == id }?.activate()
            }
        }
        facilityChanged()
    }

    /// The banner hangs flat at the top of the screen like the charge alert;
    /// the tags line up under the HUD title on the core plane.
    private func layoutFacilityLayers() {
        let unit = designUnit
        // Clear of the camera housing on notched displays (their safe area
        // starts below it); 14 pt from the top elsewhere.
        var safeTop: CGFloat = 0
        if #available(macOS 12.0, *) { safeTop = window?.screen?.safeAreaInsets.top ?? 0 }
        let bannerTop = max(14, safeTop + 10)
        let size = CGFloat(shownOverlayLayout.alerts.size)
        withoutActions {
            facilityBanner.layer.anchorPoint = CGPoint(x: 0.5, y: 0)
            facilityBanner.layer.position = CGPoint(x: 500, y: (bannerTop - designOrigin.y) / max(0.01, designScale))
            facilityBanner.layer.transform = CATransform3DMakeScale(unit, unit, 1)
            // Display > HUD elements places and sizes the column (top-right anchor).
            let tags = overlayFrame(.alerts, shownOverlayLayout)
            facilityTags.layer.anchorPoint = CGPoint(x: 1, y: 0)
            facilityTags.layer.position = CGPoint(x: tags.maxX, y: tags.minY)
            facilityTags.layer.transform = CATransform3DMakeScale(unit * size, unit * size, 1)
        }
        for item in [facilityBanner.layer, bootLog.layer] { designContext.env.applyScale(to: item) }
        facilityTags.magnification = size
        facilityTags.applyScale()
    }

    private func facilityBannerPoint(_ location: CGPoint) -> CGPoint? {
        guard facilityBanner.isShowing else { return nil }
        return facilityBanner.layer.convert(designPoint(location), from: canvas)
    }

    private func facilityTagPoint(_ location: CGPoint) -> CGPoint? {
        guard !facilityTagRouter.controls.isEmpty, let local = coreDesignPoint(designPoint(location)) else { return nil }
        return facilityTags.layer.convert(local, from: corePlane.content)
    }

    private func facilityChanged() {
        guard !updatingFacility else { return }
        updatingFacility = true
        defer { updatingFacility = false }
        let alerts = facilityAlerts?.alerts ?? []
        let listed = Set(alerts.map(\.kind))
        for kind in FacilityAlertKind.allCases where !listed.contains(kind) { facilityBanner.withdraw(kind) }
        if allowsModuleInput, !bootLog.isPlaying, let monitor = facilityAlerts {
            for alert in monitor.newAlerts { facilityBanner.show(alert); monitor.markPresented(alert.kind) }
        }
        facilityTags.isActive = window != nil
        facilityTags.update(alerts: facilityAlerts?.alerts ?? alerts, animated: allowsModuleInput)
        facilityBannerRouter.set([facilityBanner.control])
        facilityTagRouter.set(facilityTags.controls)
        // A taller column is kept on the plane where the column was moved.
        if shownOverlayLayout.alerts != HUDOverlayLayout.Placement() { layoutFacilityLayers() }
        // The alerts' corner marks, and a strip beside them, follow the column.
        layoutOverlayEditor()
        layoutDesignAccessibility()
    }

    private func openFacilityModule(_ kind: FacilityAlertKind) {
        guard allowsInputBesidePlacement else { return }
        selectModule(kind.module)
    }

    /// Opening finished: today's boot log, or the alerts raised meanwhile.
    private func hudDidBecomeInteractive() {
        facilityTags.isActive = true
        if playsBootLogOnOpen, !HUDRuntimeAppearance.reduceMotion {
            playsBootLogOnOpen = false
            withoutActions { moduleContent.layer.opacity = 0 }
            bootLog.play(bootLogLines(), version: Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.3.0")
            updateButtonStates()
        } else {
            if playsBootLogOnOpen { playsBootLogOnOpen = false }
            facilityChanged()
        }
    }

    /// The terminal wiped up: the centre enters from below, then the checks run.
    private func finishBootLog() {
        withoutActions { moduleContent.layer.opacity = 1 }
        designCanvases[selectedModule]?.playEntrance(from: HUDShellPosition.subpageOffset(dx: 0, dy: 1))
        facilityAlerts?.checkNow()
        facilityChanged()
        updateButtonStates()
    }

    /// The boot log's lines from data the HUD already has (spec §3.28).
    private func bootLogLines() -> [HUDBootLog.Line] {
        let now = Date(), calendar = Calendar.current
        let battery = snapshot.percentage.map { percent -> (percent: Int, charging: Bool, tone: CGColor) in
            let tone: CGColor
            switch snapshot.levelTone {
            case .green?: tone = HUDColor.green
            case .red?: tone = HUDColor.red
            default: tone = HUDColor.yellow
            }
            return (percent, snapshot.isCharging, tone)
        }
        let today = HUDCalendarDay(now)
        let events = calendarController.events.filter { $0.day == today }.map(\.title)
        let sanity = accountController.sanityPresentation().map {
            (current: $0.current, maximum: $0.maximum, nextIn: $0.nextRecoveryAt.map { $0.timeIntervalSince(now) })
        }
        // The streak counts consecutive days with Work Mode sessions, ending today or yesterday.
        let workDays = Set(eventLog.events.filter { [.workStarted, .workCompleted].contains($0.kind) }
            .map { calendar.startOfDay(for: $0.createdAt) })
        var day = calendar.startOfDay(for: now), streak = 0
        if !workDays.contains(day), let previous = calendar.date(byAdding: .day, value: -1, to: day) { day = previous }
        while workDays.contains(day), let previous = calendar.date(byAdding: .day, value: -1, to: day) { streak += 1; day = previous }
        let hours = workModeController.trackedWorkSeconds / 3600
        let work = streak > 0 || hours > 0 ? (days: streak, totalHours: hours) : nil
        var disk: (freePercent: Int, low: Bool)?
        if let reading = facilityAlerts?.readings.disk {
            disk = (reading.freePercent, reading.freeFraction < FacilityAlertMonitor.diskRaiseFreeFraction)
        } else if let values = try? URL(fileURLWithPath: "/").resourceValues(forKeys: [.volumeTotalCapacityKey, .volumeAvailableCapacityForImportantUsageKey]),
                  let total = values.volumeTotalCapacity, total > 0, let available = values.volumeAvailableCapacityForImportantUsage {
            let fraction = Double(available) / Double(total)
            disk = (Int((fraction * 100).rounded()), fraction < FacilityAlertMonitor.diskRaiseFreeFraction)
        }
        let name = profileStore?.profile.displayedName.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        return HUDBootLogContent.lines(date: now, uptime: ProcessInfo.processInfo.systemUptime, battery: battery,
                                       eventsToday: events, sanity: sanity, workStreak: work, disk: disk,
                                       operatorName: name.isEmpty ? UserProfile.defaultName : name)
    }

    /// A view point in the gauge's own (unscaled 168 x 42) coordinates.
    private func accountGaugePoint(_ point: CGPoint) -> CGPoint? {
        guard !accountGauge.layer.isHidden, let local = coreDesignPoint(designPoint(point)) else { return nil }
        let size = accountGauge.sizeFactor
        return CGPoint(x: (local.x - header.frame.minX - accountGauge.layer.position.x) / size,
                       y: (local.y - header.frame.minY - accountGauge.layer.position.y) / size)
    }
    private func accountGaugeRect(_ rect: CGRect) -> CGRect {
        let size = accountGauge.sizeFactor
        let design = CGRect(x: rect.minX * size, y: rect.minY * size, width: rect.width * size, height: rect.height * size)
            .offsetBy(dx: header.frame.minX + accountGauge.layer.position.x, dy: header.frame.minY + accountGauge.layer.position.y)
        let transform = corePlane.spatial.presentation()?.transform ?? corePlane.spatial.transform
        return viewRect(projectedBounds(design, through: [transform]))
    }
    @objc private func accountGaugeAction(_ sender: NSButton) {
        guard allowsInputBesidePlacement else { return }
        accountGauge.perform(sender.tag == 0 ? "toggle" : "refresh")
        window?.makeFirstResponder(self)
    }
    private func layoutAccountGaugeControls() {
        let actions = accountGauge.accessibleActions
        let interactive = allowsInputBesidePlacement
        for button in accountGaugeControls {
            let action = actions.first { $0.id == (button.tag == 0 ? "toggle" : "refresh") }
            let hidden = !interactive || action == nil
            let enabled = interactive && action?.enabled == true
            if button.isHidden != hidden { button.isHidden = hidden }
            if button.isEnabled != enabled { button.isEnabled = enabled }
            if let action {
                let rect = accountGaugeRect(action.rect)
                if button.frame != rect { button.frame = rect }
                if button.accessibilityLabel() != action.label { button.setAccessibilityLabel(action.label) }
                let value = action.id == "toggle" && accountGauge.isPopoverOpen
                    ? L10n.text("Next recovery", "下次回复") + " " + accountGauge.nextRecoveryText + "; "
                        + L10n.text("Full recovery", "全部回复") + " " + accountGauge.fullRecoveryText : nil
                if button.accessibilityValue() as? String != value { button.setAccessibilityValue(value) }
            }
        }
    }
    private func updateAccountGauge() {
        let value = accountController.gaugeValue(work: workModeController.snapshot)
        accountGauge.update(value: value.0, accessibilityLabel: value.1, visible: value.2,
            accent: currentAccent, scale: hudRasterScale,
            sanity: accountController.sanityPresentation())
        layoutAccountGaugeControls()
    }
    private func updateAccountPresentation() {
        accountCanvas.update(accountController.presentation)
        profileCanvas.gameSyncActive = accountController.gameSyncActive
        profileCanvas.syncedFields = accountController.gameSyncedFields
        updateAccountGauge()
    }
    /// Host services of the redesigned modules: account actions, the File
    /// Shelf as a document source, event-log records and outward links.
    private func configureModuleCallbacks(shelf: FileShelfStore?) {
        accountCanvas.onAction = { [weak self] action in
            guard let self else { return }; self.accountController.perform(action, window: self.window)
        }
        accountObserver = accountController.observe { [weak self] in self?.updateAccountPresentation() }
        updateAccountPresentation()
        let media = Set(NotesMediaFactory.supportedFileExtensions)
        archiveCanvas.shelfChoices = { [weak shelf] in Self.documentShelfChoices(shelf: shelf, extensions: media) }
        archiveCanvas.shelfAccess = { [weak shelf] id in
            guard let shelf else { throw ArchiveError.unavailable }; return try shelf.access(id: id)
        }
        archiveCanvas.controller.onEvent = { [weak self] action in
            self?.eventLog.record(kind: .archiveAction, metadata: ["action": action])
        }
        readerCanvas.shelfChoices = { [weak shelf] in Self.documentShelfChoices(shelf: shelf, extensions: ["pdf", "epub", "txt"]) }
        readerCanvas.shelfAccess = { [weak shelf] id in
            guard let shelf else { throw ReaderError.unavailable }; return try shelf.access(id: id)
        }
        readerCanvas.onEvent = { [weak self] event in
            let action: String
            switch event {
            case .imported: action = "imported"
            case .deleted: action = "deleted"
            case .bookmarkChanged: action = "bookmarked"
            case .preferencesChanged: action = "settings"
            // Page turns are too frequent for the log; older "progress" entries still read back.
            case .progressChanged: return
            }
            self?.eventLog.record(kind: .readerAction, metadata: ["action": action])
        }
        mediaAssemblyCanvas.shelfChoices = { [weak shelf] in Self.documentShelfChoices(shelf: shelf, extensions: media) }
        mediaAssemblyCanvas.shelfAccess = { [weak shelf] id in
            guard let shelf else { throw MediaAssemblyError.unavailable }; return try shelf.access(id: id)
        }
        if mediaAssemblyCanvas.controller.onEvent == nil {
            mediaAssemblyCanvas.controller.onEvent = { [weak self] action in
                self?.eventLog.record(kind: .mediaAssemblyAction, metadata: ["action": action])
            }
        }
        mapCanvas.onPinStyleChanged = { [weak self] style in
            self?.eventLog.record(kind: .mapPinStyleChanged, metadata: ["style": style.rawValue])
        }
        mapCanvas.onRecenter = { [weak self] in self?.eventLog.record(kind: .mapRecentered) }
        storageCanvas.onOpenSystemStorage = { [weak self] in self?.onOpenSystemStorage?() }
        batteryCanvas.onOpenBatteryAlert = { [weak self] in self?.openBatterySettings() }
        workCanvas.onRequestFocusAccess = { [weak self] in self?.onRequestFocusAccess?() }
        appShortcutCanvas.onLaunch = { [weak self] id in self?.onLaunchAppShortcut?(id) }
        appShortcutCanvas.onSaved = { [weak self] _ in self?.refreshAppNavigation(animated: true) }
        appShortcutCanvas.onRemoved = { [weak self] _ in self?.refreshAppNavigation(animated: true) }
        appShortcutCanvas.onReordered = { [weak self] in self?.refreshAppNavigation(animated: true) }
        profileCanvas.onGeometryPreview = { [weak self] profile in self?.applyIdentityProfile(profile) }
        nowPlayingCanvas.onEvent = { [weak self] event in
            let action: String, source: NowPlayingSource
            switch event {
            case .playPause(let value): action = "playPause"; source = value
            case .previous(let value): action = "previous"; source = value
            case .next(let value): action = "next"; source = value
            case .seek(let value): action = "seek"; source = value
            }
            self?.eventLog.record(kind: .playbackAction, metadata: ["action": action, "source": source.rawValue])
        }
    }

    private static func documentShelfChoices(shelf: FileShelfStore?, extensions: Set<String>) -> [NotesShelfMediaChoice] {
        (shelf?.items ?? []).map { item in
            NotesShelfMediaChoice(id: item.id, title: item.name, detail: item.typeDescription,
                isSupported: !item.isDirectory && extensions.contains(URL(fileURLWithPath: item.lastKnownPath).pathExtension.lowercased()),
                isAvailable: item.availabilityError == nil)
        }
    }

    /// Termination waits for document commits without opening another module or
    /// preserving its decoders. Each store owns its own bounded write barrier.
    func prepareDocumentWriteDrain() {
        // NSTextInputClient marked text must commit before the write barrier.
        designCanvases.values.forEach { $0.endEditing(commit: true) }
    }

    func drainDocumentWrites(completion: @escaping (Bool) -> Void) {
        let group = DispatchGroup()
        var succeeded = true
        let finished: (Bool) -> Void = { success in
            DispatchQueue.main.async { succeeded = succeeded && success; group.leave() }
        }
        group.enter(); archiveCanvas.controller.drainPendingWrites(timeout: 3, completion: finished)
        group.enter(); readerCanvas.controller.drainPendingWrites(timeout: 3, completion: finished)
        group.enter(); calendarCanvas.controller.drainPendingWrites(timeout: 3, completion: finished)
        group.notify(queue: .main) { completion(succeeded) }
    }

    private func refreshIdentityProfile() {
        guard let store = profileStore else { return }
        applyIdentityProfile(store.profile)
    }

    private func applyIdentityProfile(_ profile: UserProfile) {
        guard let store = profileStore else { return }
        if usesSourceShell {
            sourceWatch?.setDesktopProfile(profile, avatar: store.image(for: .avatar), background: store.image(for: .background),
                                          avatarOrientation: store.imageOrientation(for: .avatar))
        } else {
            identityCard.setProfile(profile, avatar: store.image(for: .avatar), background: store.image(for: .background),
                                    avatarOrientation: store.imageOrientation(for: .avatar))
        }
    }

    /// The Battery page's alert link: Settings > Display on its Battery alert page.
    private func openBatterySettings() {
        guard allowsModuleInput, !moduleContent.isTransitioning, let settings = settingsCanvases[.display] else { return }
        settings.showBatterySettings(); selectModule(.display)
    }
    private func installClickFeedbackMonitor() {
        guard clickFeedbackMonitor == nil else { return }
        // App-local pointer events only. Observing before responder dispatch also
        // covers NSButtons and NSTextViews without intercepting their behavior.
        clickFeedbackMonitor = NSEvent.addLocalMonitorForEvents(matching: [
            .mouseMoved, .mouseEntered, .mouseExited, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged,
            .leftMouseDown, .rightMouseDown, .otherMouseDown, .leftMouseUp, .rightMouseUp, .otherMouseUp
        ]) { [weak self] event in
            guard let self, let window = self.window, event.window === window,
                  window.isVisible else { return event }
            self.sourceWatch?.updateCursorPointer(with: event)
            guard [.leftMouseDown, .rightMouseDown, .otherMouseDown, .leftMouseUp].contains(event.type),
                  !self.retracting, self.interactionEnabled || self.transitioning else { return event }
            self.updateControlHighlights(at: self.convert(event.locationInWindow, from: nil), pressed: event.type != .leftMouseUp)
            if event.type == .leftMouseUp { return event }
            self.acknowledgeHUDClick(at: self.convert(event.locationInWindow, from: nil))
            return event
        }
    }

    private func removeClickFeedbackMonitor() {
        if let clickFeedbackMonitor { NSEvent.removeMonitor(clickFeedbackMonitor) }
        clickFeedbackMonitor = nil
    }

    var hasClickFeedbackMonitorForVerification: Bool { clickFeedbackMonitor != nil }

    private func acknowledgeHUDClick(at point: CGPoint) {
        guard bounds.contains(point), !HUDRuntimeAppearance.reduceMotion else { return }
        clickFeedbackCountForVerification += 1
        if CommandLine.arguments.contains("--ui-test") {
            print("HUD click feedback: \(clickFeedbackCountForVerification)")
            fflush(stdout)
        }
        withoutActions {
            actionFeedback.position = point
            actionFeedback.strokeColor = currentAccent.cgColor
            actionFeedback.lineWidth = 1.2
            actionFeedback.contentsScale = window?.backingScaleFactor ?? 2
        }
        let lift = CAKeyframeAnimation(keyPath: "transform.scale")
        lift.values = [0.7 * designScale, 1.15 * designScale, 1.3 * designScale]
        lift.keyTimes = [0, 0.4, 1]
        let opacity = CAKeyframeAnimation(keyPath: "opacity")
        opacity.values = [0, 0.9, 0]; opacity.keyTimes = [0, 0.2, 1]
        let action = CAAnimationGroup(); action.animations = [lift, opacity]; action.duration = 0.2
        actionFeedback.add(action, forKey: "action.engage")
    }

    private func deactivateModuleInput(preservingNowPlayingPresentation: Bool = false) {
        cancelOverlayPlacement()
        accountGauge.dismiss(animated: false)
        layoutAccountGaugeControls()
        clearControlHighlights()
        notesInteraction?.deactivate()
        if !preservingNowPlayingPresentation {
            nowPlayingCanvas.setPresented(false)
            mediaAssemblyCanvas.setPresented(false)
            calendarCanvas.setPresented(false)
        }
        settingsInteractions.values.forEach { $0.deactivate() }
        // A module switch closes the confirm bar and toast at once (spec §1.10).
        designCanvases.values.forEach { $0.setActive(false) }
        mapCanvas.releaseRasterWorkingSet()
        designContext.overlay.reset()
        layoutDesignAccessibility()
    }

    private func updateWorkPresentation(force: Bool = false) {
        profileCanvas.refreshWorkDuration()
        let phase = workModeController.snapshot.phase
        guard force || displayedWorkPhase != phase else { return }
        displayedWorkPhase = phase
        // English in every language, like the clock's RECORDING MODE; always
        // shown, grey while idle (the status row colours it).
        let label: String
        switch phase {
        case .running: label = "WORK MODE / ACTIVE"
        case .paused: label = "WORK MODE / PAUSED"
        default: label = "WORK MODE"
        }
        withoutActions { workBadge.string = label; workBadge.isHidden = false }
        updateStatusPanel()
        navigationButtons[.module(.workMode)]?.setAccessibilityHelp(phase == .running || phase == .paused ? label : nil)
    }

    override func mouseDragged(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil)
        if overlayEditor != nil { overlayPointerDragged(location); return }
        if arrangeDrag != nil { dragArrangedModule(to: location); return }
        if notesWorkspaceIsInteractive, notesCanvas.isDragging, let local = notesWorkspacePoint(location) {
            notesInteraction?.mouseDraggedInWorkspace(to: local); return
        }
        if allowsModuleInput, designContext.overlay.capturesInput {
            if let point = designBoxPoint(location) { designContext.overlay.router.pointerDragged(to: point, in: designContext.overlay.layer) }
            return
        }
        if let canvas = activeDesignCanvas {
            if let point = designBoxPoint(location) { canvas.pointerDragged(to: point) }
            return
        }
    }

    override func mouseUp(with event: NSEvent) {
        summonedDuringFileDrag = false
        let releaseLocation = convert(event.locationInWindow, from: nil), far = CGPoint(x: -10_000, y: -10_000)
        if overlayEditor != nil { overlayPointerUp(releaseLocation); return }
        if arrangeDrag != nil {
            endModuleArrangeDrag(save: true)
            sourceWatch?.refreshSourceCursor()
            return
        }
        facilityBannerRouter.pointerUp(at: facilityBannerPoint(releaseLocation) ?? far, in: facilityBanner.layer)
        facilityTagRouter.pointerUp(at: facilityTagPoint(releaseLocation) ?? far, in: facilityTags.layer)
        let release = designBoxPoint(releaseLocation) ?? far
        designContext.overlay.router.pointerUp(at: release, in: designContext.overlay.layer)
        if let canvas = activeDesignCanvas {
            canvas.pointerUp(at: release)
            sourceWatch?.refreshSourceCursor()
        }
        notesInteraction?.mouseUp()
    }

    override func rightMouseDown(with event: NSEvent) {
        guard !accountGauge.isPopoverOpen, overlayEditor == nil else { return }
        let location = convert(event.locationInWindow, from: nil), design = designPoint(location)
        if notesWorkspaceIsInteractive, let point = notesWorkspacePoint(location), notesCanvas.containsWorkspacePoint(point) {
            _ = notesInteraction?.rightMouseDown(at: point); return
        }
        if designContext.overlay.capturesInput { return }
        if let canvas = activeDesignCanvas, navigationTargetAtDesignPoint(design) == nil, let point = designBoxPoint(location),
           canvas.rightPointerDown(at: point) { return }
        super.rightMouseDown(with: event)
    }

    override func magnify(with event: NSEvent) {
        guard !accountGauge.isPopoverOpen, overlayEditor == nil else { return }
        let location = convert(event.locationInWindow, from: nil), design = designPoint(location)
        if designContext.overlay.capturesInput { return }
        if let canvas = activeDesignCanvas, navigationTargetAtDesignPoint(design) == nil, let point = designBoxPoint(location),
           canvas.magnify(at: point, magnification: event.magnification) { return }
        super.magnify(with: event)
    }

    /// Wheel delta in content-box px (+ moves content up / scrolls down).
    private func designScrollDelta(_ event: NSEvent) -> CGPoint {
        let line: CGFloat = event.hasPreciseScrollingDeltas ? 1 : 12
        let scale = max(0.01, designScale * designUnit)
        return CGPoint(x: -event.scrollingDeltaX * line / scale, y: -event.scrollingDeltaY * line / scale)
    }

    override func scrollWheel(with event: NSEvent) {
        guard !accountGauge.isPopoverOpen, overlayEditor == nil else { return }
        let location = convert(event.locationInWindow, from: nil)
        let design = designPoint(location)
        if designContext.overlay.capturesInput { return }
        if let canvas = activeDesignCanvas, canvas.capturesPointer {
            if let point = designBoxPoint(location) { _ = canvas.scroll(at: point, delta: designScrollDelta(event), event: event) }
            return
        }
        // Arrange on HUD: this view takes the presses on the drawn tiles, and
        // passes their wheel on to the list they belong to.
        if usesSourceShell, let sourceWatch, rightRowTarget(atView: location) != nil { sourceWatch.scrollWheel(with: event); return }
        if notesWorkspaceIsInteractive, let local = notesWorkspacePoint(location), notesCanvas.containsWorkspacePoint(local) {
            let delta = -event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12)
            // Match the actual tilted card's logical distance at this point.
            // Native editors keep their own scroll view, selection and undo.
            let projected = notesWorkspacePoint(CGPoint(x: location.x, y: location.y + delta))
            let logical = projected.map { $0.y - local.y } ?? delta / max(0.1, designScale)
            _ = notesInteraction?.scroll(at: local, delta: logical)
            // A note without overflow still covers the clock/navigation below.
            return
        }
        if allowsModuleInput, clockPoint(location) != nil {
            // A trackpad switches styles with a sideways swipe; a mouse wheel's
            // vertical notches do too (down like a leftward swipe), as on Windows.
            let wheel = !event.hasPreciseScrollingDeltas && event.scrollingDeltaX == 0
            let horizontal = -(wheel ? event.scrollingDeltaY : event.scrollingDeltaX) * (event.hasPreciseScrollingDeltas ? 1 : 12)
            if let direction = clockStyleArtwork.scroll(delta: horizontal, phase: event.phase,
                    momentum: event.momentumPhase, at: event.timestamp) {
                selectClockStyle(configuration.clockStyle.advanced(direction))
            }
            return
        }
        // Keep native trackpad momentum and gesture-end events, including
        // zero-delta events, so navigation can settle its elastic overscroll.
        let delta = -event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12)
        if !usesSourceShell, allowsModuleInput, let local = navigationPoint(design),
           navigation.scroll(at: local, delta: delta / designScale,
                             phase: event.phase, momentumPhase: event.momentumPhase) { return }
        if let canvas = activeDesignCanvas {
            if let point = designBoxPoint(location), canvas.hitBounds.contains(point),
               canvas.scroll(at: point, delta: designScrollDelta(event), event: event) { return }
            super.scrollWheel(with: event); return
        }
        super.scrollWheel(with: event)
    }

    private func notesDropPoint(_ sender: NSDraggingInfo) -> CGPoint? {
        guard activeDesignCanvas === notesCanvas else { return nil }
        let point = convert(sender.draggingLocation, from: nil)
        guard bounds.contains(point), navigationTargetAtDesignPoint(designPoint(point)) == nil else { return nil }
        return notesWorkspacePoint(point)
    }

    private func isShelfNavigationDrop(_ sender: NSDraggingInfo) -> Bool {
        guard allowsModuleInput, !isDraggingShelfItem,
              HUDFileShelfInteraction.acceptsFiles(sender.draggingPasteboard) else { return false }
        let point = designPoint(convert(sender.draggingLocation, from: nil))
        return navigationTargetAtDesignPoint(point) == .module(.fileShelf)
    }

    /// The point lands on the module's centre content: its box with the overflow it claims.
    private func centreContains(_ location: CGPoint, module: HUDModule) -> Bool {
        guard let canvas = designCanvases[module], let point = designBoxPoint(location) else { return false }
        return canvas.hitBounds.contains(point)
    }

    /// A control of the active module is drawn under this view point.
    private func designControlContains(_ location: CGPoint) -> Bool {
        guard let canvas = activeDesignCanvas, let point = designBoxPoint(location) else { return false }
        return canvas.containsControl(atBox: point)
    }

    /// A press for the active module; it takes keyboard focus unless it began editing.
    private func designPointerDown(at location: CGPoint, event: NSEvent) -> Bool {
        guard let canvas = activeDesignCanvas, let point = designBoxPoint(location), canvas.pointerDown(at: point, event: event) else { return false }
        if !canvas.isEditingText { window?.makeFirstResponder(self) }
        sourceWatch?.refreshSourceCursor()
        return true
    }

    private func acceptsShelfDrop(_ sender: NSDraggingInfo) -> Bool {
        if isShelfNavigationDrop(sender) { return true }
        guard activeDesignCanvas === shelfCanvas, !isDraggingShelfItem,
              centreContains(convert(sender.draggingLocation, from: nil), module: .fileShelf) else { return false }
        return HUDFileShelfInteraction.acceptsFiles(sender.draggingPasteboard)
    }

    private func acceptsAppShortcutDrop(_ sender: NSDraggingInfo) -> Bool {
        guard activeDesignCanvas === appShortcutCanvas,
              centreContains(convert(sender.draggingLocation, from: nil), module: .addApp) else { return false }
        return AppShortcutCanvas.acceptsApplications(sender.draggingPasteboard)
    }

    /// Files a document module takes from a drop: Archive inserts into the open
    /// document, Reader and Media Assembly open one file.
    private func documentDropURLs(_ sender: NSDraggingInfo, module: HUDModule) -> [URL] {
        guard sender.draggingSourceOperationMask.contains(.copy), let canvas = activeDesignCanvas, canvas.module == module,
              !canvas.capturesPointer, !canvas.isPresentingPanel else { return [] }
        switch module {
        case .archive: guard archiveCanvas.controller.selected != nil else { return [] }
        case .mediaAssembly: guard !mediaAssemblyCanvas.controller.isExporting else { return [] }
        case .reader: break
        default: return []
        }
        let location = convert(sender.draggingLocation, from: nil)
        guard bounds.contains(location), navigationTargetAtDesignPoint(designPoint(location)) == nil,
              centreContains(location, module: module),
              !(notesWorkspaceIsInteractive && notesWorkspacePoint(location).map { notesCanvas.containsWorkspacePoint($0) } == true) else { return [] }
        let extensions = module == .reader ? Set(["pdf", "epub", "txt"]) : Set(NotesMediaFactory.supportedFileExtensions)
        let urls = sender.draggingPasteboard.readObjects(forClasses: [NSURL.self],
            options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        return Array(urls.lazy.filter { $0.isFileURL && extensions.contains($0.pathExtension.lowercased()) }.prefix(module == .archive ? 16 : 1))
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation { draggingUpdated(sender) }

    override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        externalFileDragActive = true
        sourceWatch?.refreshSourceCursor()
        guard sender.draggingSourceOperationMask.contains(.copy) else {
            endFileDrop(preservingNativeDrag: true)
            return []
        }
        let overShelf = isShelfNavigationDrop(sender)
        if shelfNavigationDropTarget != overShelf {
            shelfNavigationDropTarget = overShelf
            navigation.hoverTarget(overShelf ? .module(.fileShelf) : nil)
        }
        if overShelf {
            notesInteraction?.endExternalDrag()
            appShortcutCanvas.setDropTarget(false)
            motion.freezeParallax()
            return .copy
        }
        if acceptsAppShortcutDrop(sender) {
            appShortcutCanvas.setDropTarget(true)
            return .copy
        }
        appShortcutCanvas.setDropTarget(false)
        if acceptsShelfDrop(sender) {
            shelfInteraction?.beginExternalDrag()
            return .copy
        }
        shelfInteraction?.endExternalDrag()
        if !documentDropURLs(sender, module: .mediaAssembly).isEmpty || !documentDropURLs(sender, module: .reader).isEmpty || !documentDropURLs(sender, module: .archive).isEmpty {
            notesInteraction?.endExternalDrag(); return .copy
        }
        if notesDropPoint(sender) != nil && HUDNotesInteraction.acceptsImages(sender.draggingPasteboard) {
            notesInteraction?.beginExternalDrag()
            return .copy
        }
        notesInteraction?.endExternalDrag()
        return []
    }

    private func endFileDrop(preservingNativeDrag: Bool = false) {
        // Rejecting a source operation clears drop feedback, but AppKit still
        // owns the pointer until that drag really exits or ends.
        if !preservingNativeDrag { externalFileDragActive = false }
        if shelfNavigationDropTarget { navigation.hoverTarget(nil) }
        shelfNavigationDropTarget = false
        summonedDuringFileDrag = false
        notesInteraction?.endExternalDrag()
        shelfInteraction?.endExternalDrag()
        appShortcutCanvas.setDropTarget(false)
        sourceWatch?.refreshSourceCursor()
    }

    override func draggingExited(_ sender: NSDraggingInfo?) { endFileDrop() }
    override func draggingEnded(_ sender: NSDraggingInfo) { endFileDrop() }
    override func wantsPeriodicDraggingUpdates() -> Bool { false }

    override func prepareForDragOperation(_ sender: NSDraggingInfo) -> Bool {
        sender.draggingSourceOperationMask.contains(.copy) && (acceptsAppShortcutDrop(sender) || acceptsShelfDrop(sender)
            || !documentDropURLs(sender, module: .mediaAssembly).isEmpty || !documentDropURLs(sender, module: .reader).isEmpty || !documentDropURLs(sender, module: .archive).isEmpty
            || (notesDropPoint(sender) != nil && HUDNotesInteraction.acceptsImages(sender.draggingPasteboard)))
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        defer { endFileDrop() }
        if isShelfNavigationDrop(sender) {
            return receiveShelfNavigationDrop(sender.draggingPasteboard,
                at: convert(sender.draggingLocation, from: nil))
        }
        if acceptsAppShortcutDrop(sender) { return appShortcutCanvas.importPasteboard(sender.draggingPasteboard) }
        if acceptsShelfDrop(sender) { return shelfInteraction?.importPasteboard(sender.draggingPasteboard) ?? false }
        let mediaURLs = documentDropURLs(sender, module: .mediaAssembly)
        if !mediaURLs.isEmpty { mediaAssemblyCanvas.importFiles(mediaURLs); return true }
        let readerURLs = documentDropURLs(sender, module: .reader)
        if !readerURLs.isEmpty { readerCanvas.importFiles(readerURLs); return true }
        let archiveURLs = documentDropURLs(sender, module: .archive)
        if !archiveURLs.isEmpty { archiveCanvas.importFiles(archiveURLs); return true }
        guard let point = notesDropPoint(sender) else { return false }
        return notesInteraction?.importPasteboard(sender.draggingPasteboard, at: point) ?? false
    }

    private func receiveShelfNavigationDrop(_ pasteboard: NSPasteboard, at point: CGPoint) -> Bool {
        guard allowsModuleInput, !isDraggingShelfItem,
              navigationTargetAtDesignPoint(designPoint(point)) == .module(.fileShelf),
              HUDFileShelfInteraction.acceptsFiles(pasteboard) else { return false }
        let urls = pasteboard.readObjects(forClasses: [NSURL.self],
            options: [.urlReadingFileURLsOnly: true]) as? [URL] ?? []
        guard !urls.isEmpty else { return false }
        let imported = shelfCanvas.importURLs(urls)
        selectModule(.fileShelf)
        return imported
    }

    @discardableResult
    func importShelfFiles(_ urls: [URL]) -> Bool {
        shelfCanvas.importURLs(urls)
    }

    func revealShelfItems(_ ids: Set<UUID>) { shelfCanvas.revealItems(ids) }
    var shelfScrollOffsetForVerification: CGFloat { shelfCanvas.scrollOffset }
    var shelfSelectedCountForVerification: Int { shelfCanvas.selectedIDs.count }

    private func viewRect(_ rect: CGRect) -> CGRect {
        CGRect(x: designOrigin.x + rect.minX * designScale, y: designOrigin.y + rect.minY * designScale,
               width: rect.width * designScale, height: rect.height * designScale)
    }

    private func designPoint(_ point: CGPoint) -> CGPoint {
        CGPoint(x: (point.x - designOrigin.x) / designScale, y: (point.y - designOrigin.y) / designScale)
    }

    private func withoutActions(_ body: () -> Void) {
        CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit()
    }
}

/// The projected arrow artwork receives mouse input through the common view;
/// these controls expose the same paging actions to keyboard and VoiceOver.
private final class HUDNavigationScrollButton: NSButton {
    var projectedFrame: (() -> CGRect)?
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityFrame() -> NSRect { projectedFrame?() ?? super.accessibilityFrame() }
}

/// Native keyboard/accessibility action with projected visual hit-testing.
private final class HUDNavigationHitButton: NSButton {
    let navigationTarget: HUDNavigationTarget
    var acceptsPoint: ((CGPoint) -> Bool)?
    var projectedAccessibilityFrame: (() -> NSRect?)?
    init(navigationTarget: HUDNavigationTarget) {
        self.navigationTarget = navigationTarget
        super.init(frame: .zero)
        setButtonType(.momentaryPushIn)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) is not supported") }
    override func accessibilityFrame() -> NSRect {
        projectedAccessibilityFrame?() ?? super.accessibilityFrame()
    }
    override func hitTest(_ point: NSPoint) -> NSView? {
        guard isEnabled, !isHidden, frame.contains(point), acceptsPoint?(point) != false else { return nil }
        return self
    }
}

/// Transparent native hit target for the red confirmed-quit control.
private final class HUDIdentityCloseButton: NSButton {
    var acceptsPoint: ((CGPoint) -> Bool)?
    var projectedAccessibilityFrame: (() -> NSRect?)?
    override func draw(_ dirtyRect: NSRect) {}
    override func accessibilityFrame() -> NSRect { projectedAccessibilityFrame?() ?? super.accessibilityFrame() }
    override func hitTest(_ point: NSPoint) -> NSView? {
        guard isEnabled, !isHidden, frame.contains(point), acceptsPoint?(point) == true else { return nil }
        return self
    }
}
