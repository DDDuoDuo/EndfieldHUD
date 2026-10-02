import AppKit
import QuartzCore
import Quartz

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
    private var quitConfirmation: HUDQuitConfirmationView?
    private var quitConfirmationPending = false
    private var quitCommitted = false
    private var allowsModuleInput: Bool { interactionEnabled && !transitioning && !quitConfirmationPending && !quitCommitted }
    var onOpenSystemStorage: (() -> Void)?
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
    private var usesSourceShell: Bool { sourceWatch != nil && sourceWatchFailureReason == nil }
    var sourceWatchForVerification: HUDSourceWatchView? { sourceWatch }
    var isPreparingSourceBackdrop: Bool { sourceWatch?.isPreparingBackdrop == true }
    var sourceFailureForVerification: String? { sourceWatchFailureReason }
    var workFocusStatusMessage: String? {
        didSet { workCanvas.setFocusStatusMessage(workFocusStatusMessage) }
    }
    var interactionEnabled = false {
        didSet {
            updateButtonStates()
            if interactionEnabled { scheduleVisibleMotion(); refreshChargeHover(); headerClock.setActive(window != nil) }
            else {
                deactivateModuleInput()
                if motion.isPointerFollowing {
                    motion.startPointerFollowing(reducedMotion: HUDRuntimeAppearance.reduceMotion,
                                                 initialPoint: currentPointerTarget())
                }
                headerClock.setActive(false)
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
    private let artwork = HUDMechanicalArtwork()
    private let distantPlane = HUDDepthPlane(name: "distant", depth: -95, travel: -12, lag: 0.40)
    private let rearPlane = HUDDepthPlane(name: "rear", depth: -52, travel: -10, lag: 0.36)
    private let secondaryPlane = HUDDepthPlane(name: "secondary", depth: -24, travel: 3, lag: 0.32)
    private let framePlane = HUDDepthPlane(name: "frame", depth: 0, travel: 8, lag: 0.28)
    private let innerPlane = HUDDepthPlane(name: "inner", depth: 26, travel: 16, lag: 0.25)
    private let panelsPlane = HUDDepthPlane(name: "panels", depth: 36, travel: 16, lag: 0.27)
    private let corePlane = HUDDepthPlane(name: "core", depth: 54, travel: 24, lag: 0.22)
    // Follow the central dial, but paint behind navigation when the user makes
    // the profile image wider than the circular well.
    private let profileBackgroundPlane = HUDDepthPlane(name: "profileBackground", depth: 54, travel: 24, lag: 0.22)
    private let markersPlane = HUDDepthPlane(name: "markers", depth: 68, travel: 28, lag: 0.20)
    private let glassPlane = HUDDepthPlane(name: "glass", depth: 86, travel: 32, lag: 0.18)
    // Match the central content's full foreground movement, rather than the
    // recessed well's restrained travel, while keeping the arcs drawn on top.
    private lazy var rimPlane = HUDDepthPlane(name: "rim", depth: corePlane.depth,
                                              travel: corePlane.travel, lag: corePlane.lag)
    // The badge shares the dial pose and follows its deployment transform.
    private lazy var chargePlane = HUDDepthPlane(name: "charge", depth: corePlane.depth,
                                                  travel: corePlane.travel, lag: corePlane.lag)
    private var depthPlanes: [HUDDepthPlane] {
        [distantPlane, rearPlane, secondaryPlane, framePlane, innerPlane,
         profileBackgroundPlane, panelsPlane, corePlane, markersPlane, glassPlane, rimPlane, chargePlane]
    }
    // Notes keep screen-sized persistent geometry, with the same camera pose
    // and response as the side buttons, outside the center's circular mask.
    private lazy var notesPlane = HUDDepthPlane(name: "notes", depth: panelsPlane.depth,
                                                travel: panelsPlane.travel, lag: panelsPlane.lag)
    private lazy var motion = HUDMotionController(planes: depthPlanes + [notesPlane])
    private var ring: CALayer { framePlane.deployment }
    private let progress = CAShapeLayer()
    private let actionFeedback = CAShapeLayer()
    private var clickFeedbackMonitor: Any?
    private var settingsCaptureMonitor: Any?
    private(set) var clickFeedbackCountForVerification = 0
    private var motionPreferenceObserver: NSObjectProtocol?
    private let core = CALayer()
    private let navigation = HUDNavigation()
    private let chargeBadge = HUDChargeBadge()
    private let identityCard = HUDIdentityCard()
    private let closeHUDButton = HUDIdentityCloseButton(frame: .zero)
    private var moduleContent: HUDModuleContent!
    private let notesCanvas: NotesCanvas
    private let notesWorkspace = CALayer()
    private let notesLayout = CALayer()
    private let notesCoordinates = CALayer()
    private let settingsController: HUDSettingsController?
    private var settingsCanvases: [HUDModule: HUDSettingsCanvas] = [:]
    private var settingsInteractions: [HUDModule: HUDSettingsInteraction] = [:]
    private var scaleSafety: HUDScaleSafetyView?
    private var shelfNavigationDropTarget = false
    private var notesInteraction: HUDNotesInteraction?
    private let shelfCanvas: FileShelfCanvas
    private var shelfInteraction: HUDFileShelfInteraction?
    private let profileStore: UserProfileStore?
    private let profileCanvas: PersonalProfileCanvas
    // The profile backdrop can extend beyond the center viewport while sharing
    // the dial's parallax and deployment pose. Text remains in the module host.
    private let profileBackgroundHost = CALayer()
    private var profileInteraction: HUDPersonalProfileInteraction?
    private var profileObserver: UUID?
    private let appShortcutStore: AppShortcutStore?
    private let appShortcutCanvas: AppShortcutCanvas
    private var appShortcutInteraction: HUDAppShortcutInteraction?
    private let clipboardCanvas: ClipboardCanvas
    private var clipboardInteraction: HUDClipboardInteraction?
    private let eventLog: SystemEventLog
    private let eventLogCanvas: EventLogCanvas
    private var eventLogInteraction: HUDEventLogInteraction?
    private let mapCanvas: WorldMapCanvas
    private var mapInteraction: HUDWorldMapInteraction?
    private let volumeCanvas: VolumeCanvas
    private var volumeInteraction: HUDVolumeInteraction?
    private let storageCanvas: StorageCanvas
    private var storageInteraction: HUDTelemetryInteraction?
    private let activityCanvas: ActivityMonitorCanvas
    private let industryWordmark = CALayer()
    private var activityInteraction: HUDTelemetryInteraction?
    private let workCanvas: WorkModeCanvas
    private var workInteraction: HUDWorkModeInteraction?
    private let workModeController: WorkModeController
    private var workObserver: UUID?
    private var displayedWorkPhase: WorkModePhase?
    private var workBadge = CATextLayer()
    var isPresentingModulePanel: Bool {
        notesInteraction?.isPresentingPanel == true || shelfInteraction?.isPresentingPanel == true
            || volumeInteraction?.isPresentingPanel == true || workInteraction?.isPresentingPanel == true
            || appShortcutInteraction?.isPresentingPanel == true || profileInteraction?.isPresentingPanel == true
            || settingsInteractions.values.contains { $0.isPresentingPanel }
    }
    var isDraggingShelfItem: Bool { shelfInteraction?.isDraggingOut == true }
    private var isModuleInputLocked: Bool {
        notesInteraction?.isInputLocked == true || shelfInteraction?.isInputLocked == true || clipboardInteraction?.isInputLocked == true
            || volumeInteraction?.isInputLocked == true || workInteraction?.isInputLocked == true || eventLogInteraction?.isInputLocked == true
            || storageInteraction?.isInputLocked == true || activityInteraction?.isInputLocked == true
            || appShortcutInteraction?.isInputLocked == true || profileInteraction?.isInputLocked == true
            || mapInteraction?.isInputLocked == true
            || settingsInteractions.values.contains { $0.isInputLocked }
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
    private var profileIsInteractive: Bool {
        allowsModuleInput && selectedModule == .profile && !moduleContent.isTransitioning
    }
    private var appShortcutsAreInteractive: Bool {
        allowsModuleInput && selectedModule == .addApp && !moduleContent.isTransitioning
    }
    private var shelfIsInteractive: Bool {
        allowsModuleInput && selectedModule == .fileShelf && !moduleContent.isTransitioning
    }
    private var notesAreInteractive: Bool {
        allowsModuleInput && selectedModule == .notes && !moduleContent.isTransitioning
    }
    private var clipboardIsInteractive: Bool {
        allowsModuleInput && selectedModule == .clipboard && !moduleContent.isTransitioning
    }
    private var volumeIsInteractive: Bool {
        allowsModuleInput && selectedModule == .volume && !moduleContent.isTransitioning
    }
    private var eventLogIsInteractive: Bool {
        allowsModuleInput && selectedModule == .eventLog && !moduleContent.isTransitioning
    }
    private var mapIsInteractive: Bool {
        window != nil && allowsModuleInput && selectedModule == .map && !moduleContent.isTransitioning
    }
    private var workIsInteractive: Bool {
        allowsModuleInput && selectedModule == .workMode && !moduleContent.isTransitioning
    }
    private var storageIsInteractive: Bool {
        window != nil && allowsModuleInput && selectedModule == .storage && !moduleContent.isTransitioning
    }
    private var activityIsInteractive: Bool {
        window != nil && allowsModuleInput && selectedModule == .activityMonitor && !moduleContent.isTransitioning
    }
    private var navigationButtons: [HUDNavigationTarget: HUDNavigationHitButton] = [:]
    private var navigationScrollButtons: [Int: HUDNavigationScrollButton] = [:]
    private(set) var selectedModule: HUDModule = .power
    private var percent = CATextLayer()
    private var centerState = CATextLayer()
    private var centerSource = CATextLayer()
    private let laptop = CAShapeLayer()
    private let header = CALayer()
    private let headerClock = HUDClock()
    private var clockTime = CATextLayer()
    private var clockDate = CATextLayer()
    private let footer = CALayer()
    private var foregroundShapes: [CAShapeLayer] = []
    private var panelShapes: [CAShapeLayer] = []
    private var structuralShapes: [CAShapeLayer] = []
    private var primaryTexts: [CATextLayer] = []
    private var mutedTexts: [CATextLayer] = []
    private var accentTexts: [CATextLayer] = []
    private var capacityLabel = CATextLayer()
    private var healthLabel = CATextLayer()
    private var titleLabel = CATextLayer()
    private var batteryHeading = CATextLayer()
    private var hintLabel = CATextLayer()
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
    private var designScale: CGFloat = 1
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
         workMode: WorkModeController, eventLog: SystemEventLog = SystemEventLog(),
         storage: StorageController = .fixture(), activity: SystemActivityMonitor = .fixture(), appActivity: AppActivityMonitor = .fixture(),
         appShortcuts: Result<AppShortcutStore, Error> = Result { try AppShortcutStore(directory: AppShortcutStore.applicationDirectory()) },
         settings: HUDSettingsController? = nil,
         profile: Result<UserProfileStore, Error> = Result { try UserProfileStore(directory: UserProfileStore.applicationDirectory()) },
         mapStore: Result<WorldMapStore, Error> = Result { try WorldMapStore(directory: WorldMapStore.applicationDirectory()) },
         initialConfiguration: AppConfiguration = .defaults,
         initialSnapshot: BatterySnapshot = .unavailable, initialModule: HUDModule = .power) {
        configuration = initialConfiguration.normalized
        snapshot = initialSnapshot
        HUDRuntimeAppearance.configuration = configuration
        switch mapStore {
        case .success(let store): mapCanvas = WorldMapCanvas(store: store)
        case .failure(let error): mapCanvas = WorldMapCanvas(store: nil, error: error.localizedDescription)
        }
        self.profileStore = try? profile.get()
        switch profile {
        case .success(let store): profileCanvas = PersonalProfileCanvas(store: store, workSeconds: { workMode.trackedWorkSeconds })
        case .failure(let error): profileCanvas = PersonalProfileCanvas(store: nil, error: error.localizedDescription, workSeconds: { workMode.trackedWorkSeconds })
        }
        settingsController = settings
        self.eventLog = eventLog
        self.appShortcutStore = try? appShortcuts.get()
        storageCanvas = StorageCanvas(controller: storage)
        activityCanvas = ActivityMonitorCanvas(controller: activity, apps: appActivity)
        eventLogCanvas = EventLogCanvas(store: eventLog)
        switch notesStore {
        case .success(let store): notesCanvas = NotesCanvas(store: store, notesSelected: initialModule == .notes)
        case .failure(let error): notesCanvas = NotesCanvas(store: nil, error: error.localizedDescription, notesSelected: initialModule == .notes)
        }
        switch shelfStore {
        case .success(let store): shelfCanvas = FileShelfCanvas(store: store)
        case .failure(let error): shelfCanvas = FileShelfCanvas(store: nil, error: error.localizedDescription)
        }
        switch appShortcuts {
        case .success(let store): appShortcutCanvas = AppShortcutCanvas(store: store)
        case .failure(let error): appShortcutCanvas = AppShortcutCanvas(store: nil, error: error.localizedDescription)
        }
        clipboardCanvas = ClipboardCanvas(store: clipboard.store)
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
        volumeCanvas = VolumeCanvas(controller: audio, perAppAudio: perAppAudio)
        workModeController = workMode
        workCanvas = WorkModeCanvas(controller: workMode)
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
        refreshAppNavigation(animated: false)
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
        // Pointer feedback is screen-aligned and above all projected content,
        // including native inline text editors and freely positioned notes.
        actionFeedback.zPosition = 2_000_000
        layer?.addSublayer(actionFeedback)
        configureNotesInteraction()
        configureShelfInteraction(store: try? shelfStore.get())
        configureClipboardInteraction()
        configureEventLogInteraction()
        configureMapInteraction()
        configureVolumeInteraction()
        configureWorkInteraction()
        configureTelemetryInteractions()
        configureAppShortcutInteraction()
        configureProfileInteraction()
        refreshIdentityProfile()
        profileObserver = profileStore?.observe { [weak self] in self?.refreshIdentityProfile() }
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
                    self.artwork.frame.removeAnimation(forKey: "section.index")
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
        headerClock.setFormat(configuration.clockFormat)
        motion.configure(parallax: CGFloat(configuration.parallaxIntensity),
                         perspective: CGFloat(configuration.perspectiveIntensity),
                         ambient: HUDRuntimeAppearance.ambientEnabled)
        // Install the configured appearance once, then build only the requested
        // center. Production no longer constructs with defaults and repaints it
        // immediately before the first frame can be presented.
        notesCanvas.setPresentation(notesSelected: initialModule == .notes, animated: false)
        updateContent()
        configureSourceWatch()
        prepareInitialModule(initialModule)
        withoutActions { self.backdrop.opacity = 0; self.canvas.opacity = 0 }
    }

    deinit {
        if let clickFeedbackMonitor { NSEvent.removeMonitor(clickFeedbackMonitor) }
        if let settingsCaptureMonitor { NSEvent.removeMonitor(settingsCaptureMonitor) }
        if let workObserver { workModeController.removeObserver(workObserver) }
        if let profileObserver { profileStore?.removeObserver(profileObserver) }
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
            self.designScale = max(0.1, min(self.bounds.width / 1100, self.bounds.height / 740, 1.15)) * CGFloat(self.configuration.hudScale)
            self.designOrigin = CGPoint(x: (self.bounds.width - 1000 * self.designScale) / 2,
                                        y: (self.bounds.height - 640 * self.designScale) / 2 - Self.verticalLift * self.designScale)
            let offset = CGPoint(x: self.bounds.width * CGFloat(self.configuration.hudOffsetX),
                                 y: self.bounds.height * CGFloat(self.configuration.hudOffsetY))
            self.designOrigin.x += offset.x
            self.designOrigin.y += offset.y
            self.canvas.position = CGPoint(x: self.bounds.midX + offset.x,
                                           y: self.bounds.midY - Self.verticalLift * self.designScale + offset.y)
            self.canvas.setAffineTransform(CGAffineTransform(scaleX: self.designScale, y: self.designScale))
            self.notesLayout.position = self.canvas.position
            self.notesLayout.setAffineTransform(CGAffineTransform(scaleX: self.designScale, y: self.designScale))
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
        }
        notesInteraction?.layoutAccessibility()
        shelfInteraction?.layoutAccessibility()
        clipboardInteraction?.layoutAccessibility()
        eventLogInteraction?.layoutAccessibility()
        mapInteraction?.layoutAccessibility()
        volumeInteraction?.layoutAccessibility()
        workInteraction?.layoutAccessibility()
        storageInteraction?.layoutAccessibility()
        activityInteraction?.layoutAccessibility()
        appShortcutInteraction?.layoutAccessibility()
        profileInteraction?.layoutAccessibility()
        settingsInteractions.values.forEach { $0.layoutAccessibility() }
        scaleSafety?.frame = CGRect(x: max(12, (bounds.width - 430) / 2), y: max(12, (bounds.height - 72) / 2),
                                    width: min(430, bounds.width - 24), height: 72)
        updateTrackingAreas()
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
        updateContent()
    }

    func set(snapshot: BatterySnapshot, configuration: AppConfiguration) {
        let old = self.configuration
        let previousReduceMotion = HUDRuntimeAppearance.reduceMotion
        self.snapshot = snapshot
        self.configuration = configuration
        headerClock.setFormat(configuration.clockFormat)
        HUDRuntimeAppearance.configuration = configuration
        sourceWatch?.refreshMotionPreferences()
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
    func showStable(preservingChargeAnimation: Bool = false, preservingPointerMotion: Bool = false) {
        cancelAnimations(preservingChargeAnimation: preservingChargeAnimation, preservingPointerMotion: preservingPointerMotion)
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
            for plane in self.depthPlanes {
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
        updateSourceOverviewPresentation(stable: true)
        updateButtonStates()
        if let projection = sourceCenterProjection { applySourceCenterProjection(projection) }
    }

    func animateEntrance(ready: @escaping () -> Void = {}, completion: @escaping () -> Void = {}) {
        showStable()
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
            (profileBackgroundPlane, 0.32, 0.21, CGFloat(0.82), CGFloat(30)),
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
            (corePlane, 0.11, 0.20, 0.65), (profileBackgroundPlane, 0.11, 0.20, 0.65),
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
        let machinery = artwork.groups.flatMap { $0.sublayers ?? [] } + [progress]
        let cards = navigation.visibleEntries.filter { $0.module != .power && $0.module != .profile }.map(\.layer)
            + (identityCard.layer.sublayers ?? [])
        let readouts = moduleContent.layer.sublayers?.flatMap { wrapper in
            (wrapper.sublayers ?? []).flatMap { $0.sublayers ?? [] }
        } ?? []
        let profileBackdrop = selectedModule == .profile ? [profileCanvas.backgroundLayer] : []
        let lettering = (header.sublayers ?? []) + (footer.sublayers ?? []) + [industryWordmark]
        let notes = notesCanvas.deploymentLayers
        HUDDeploymentFlicker.applySweep(to: machinery + cards + readouts + profileBackdrop + lettering + notes,
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
            sourceWatch?.inputEnabled = allowsModuleInput
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

    func cancelAnimations(preserveClickFeedback: Bool = false, preservingChargeAnimation: Bool = false, preservingPointerMotion: Bool = false) {
        sourceEntranceReady = nil
        headerClock.setActive(false)
        sourceWatch?.suspendForConcealment()
        deactivateModuleInput()
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
        if quitConfirmationPending {
            quitConfirmation?.dismiss(animated: false)
            quitConfirmationPending = false
        }
        func remove(_ item: CALayer) {
            if preserveClickFeedback && item === actionFeedback { return }
            if preservingChargeAnimation && item === chargeBadge.layer { return }
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
        sourceWatch?.suspendForConcealment()
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
        guard let moduleContent else { return false }
        let size = selectedModule.contentFrame.size
        let probes = [CGPoint.zero, CGPoint(x: size.width / 2, y: size.height / 2),
                      CGPoint(x: size.width, y: size.height)]
        let inputMatchesProjection = probes.allSatisfy { point in
            let projected = projectCenterRect(CGRect(origin: point, size: .zero)).origin
            guard let recovered = centerPoint(designPoint(projected)) else { return false }
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
            && moduleContent.layer.position == CGPoint(x: 500, y: reportCenterY)
            && CATransform3DEqualToTransform(moduleContent.layer.transform,
                                            CATransform3DMakeScale(reportScale, reportScale, 1))
    }

    var workModeDialDiameterForVerification: CGFloat {
        guard selectedModule == .workMode else { return 0 }
        return WorkModeDialGeometry.radius * 2 * moduleContent.layer.transform.m11
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
    var ambientStartTime: CFTimeInterval? { artwork.rearRotor.animation(forKey: "ambient.rearRotation")?.beginTime }

    var ambientAnimationCount: Int { motion.ambientAnimationCount }
    var parallaxAnimationCount: Int { motion.parallaxAnimationCount + (usesSourceShell && sourceWatch?.pointerIsAnimatingForVerification == true ? 1 : 0) }
    var workModeAnimationCount: Int {
        func count(_ item: CALayer) -> Int {
            (item.animationKeys() ?? []).filter { $0.hasPrefix("workMode.") }.count
                + (item.sublayers ?? []).reduce(0) { $0 + count($1) }
        }
        return count(workCanvas.layer)
    }
    var telemetryAnimationCount: Int { activityCanvas.animationCount + storageCanvas.animationCount }
    var deploymentAnimationCount: Int { activeAnimationCount - ambientAnimationCount - parallaxAnimationCount - workModeAnimationCount - telemetryAnimationCount - chargeBadge.animationCount }
    func performStorageActionForVerification(_ id: String) {
        guard storageIsInteractive else { return }
        storageCanvas.perform(actionID: id)
    }

    func performAppShortcutActionForVerification(_ id: String) {
        guard appShortcutsAreInteractive else { return }
        appShortcutCanvas.perform(actionID: id)
    }

    func performSettingsActionForVerification(_ id: String) {
        settingsCanvases[selectedModule]?.perform(actionID: id)
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
            return (depthPlanes + [notesPlane]).allSatisfy { CATransform3DEqualToTransform($0.spatial.transform, projection) }
                && reportGeometryMatchesSelectionForVerification
        }
        return (depthPlanes + [notesPlane]).allSatisfy { plane in
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
    private var chargeSourceOffset: CGFloat { usesSourceShell ? 38 : 0 }
    private var displayedChargeHitRect: CGRect { chargeBadge.hitRect.offsetBy(dx: 0, dy: chargeSourceOffset) }
    var chargeHitRectForVerification: CGRect { displayedChargeHitRect }
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
        activateNavigationTarget(sender.navigationTarget)
    }

    private func activateNavigationTarget(_ target: HUDNavigationTarget) {
        guard allowsModuleInput else { return }
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
        deactivateModuleInput()
        selectedModule = module
        let shouldAnimate = animated && !HUDRuntimeAppearance.reduceMotion
        navigation.select(module, animated: shouldAnimate)
        moduleContent.select(module: module, animated: shouldAnimate) { [weak self] in
            guard let self = self, self.interactionEnabled, !self.transitioning else { return }
            self.eventLog.record(kind: .moduleOpened, metadata: ["module": module.rawValue])
            self.updateButtonStates()
        }
        if shouldAnimate && !usesSourceShell {
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
        notesCanvas.setPresentation(notesSelected: module == .notes, animated: shouldAnimate)
        updateButtonStates()
        updateNavigationGeometry()
        scheduleVisibleMotion()
    }

    private func updateModulePresentation(restoreSourceOverview: Bool = true) {
        if restoreSourceOverview { updateSourceOverviewPresentation(stable: sourceOverviewPresented) }
        updateProfileBackgroundPresentation()
        withoutActions {
            self.moduleContent.layer.position = CGPoint(x: 500, y: self.reportCenterY)
            self.moduleContent.layer.transform = CATransform3DMakeScale(self.reportScale, self.reportScale, 1)
            self.profileBackgroundHost.position = self.moduleContent.layer.position
            self.moduleContrast.position = self.moduleContent.layer.position
            self.moduleContrast.transform = self.moduleContent.layer.transform
            self.moduleContrast.isHidden = !self.usesSourceShell
            self.chargeBadge.layer.position = CGPoint(x: HUDChargeBadge.frame.midX,
                                                       y: HUDChargeBadge.frame.midY + self.chargeSourceOffset)
            self.industryWordmark.backgroundColor = (self.currentDark ? NSColor.white : NSColor(white: 0.12, alpha: 1)).cgColor
            let color = self.selectedModule == .power ? self.currentBatteryTone : self.currentAccent
            self.artwork.update(dark: self.currentDark, chargeColor: color, accentColor: self.currentAccent)
            self.progress.strokeColor = color.cgColor
            self.progress.isHidden = self.usesSourceShell || self.selectedModule == .workMode
            self.progress.strokeEnd = self.selectedModule == .power ? CGFloat(self.snapshot.percentage ?? 0) / 100 : 0.12
            self.setAccessibilityLabel(L10n.text("System interface", "系统界面") + ", " + self.selectedModule.title)
        }
    }

    private func updateProfileBackgroundPresentation() {
        let target: Float = selectedModule == .profile ? 1 : 0
        guard profileBackgroundHost.opacity != target else { return }
        let previous = profileBackgroundHost.presentation()?.opacity ?? profileBackgroundHost.opacity
        let priorTravel = profileBackgroundHost.presentation()?.transform.m41 ?? profileBackgroundHost.transform.m41
        let animated = allowsModuleInput && !HUDRuntimeAppearance.reduceMotion
        withoutActions {
            profileBackgroundHost.removeAnimation(forKey: "profile.section.opacity")
            profileBackgroundHost.removeAnimation(forKey: "profile.section.travel")
            profileBackgroundHost.opacity = target
        }
        guard animated else { return }
        let fade = CABasicAnimation(keyPath: "opacity")
        fade.fromValue = previous; fade.toValue = target; fade.duration = 0.24
        fade.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        profileBackgroundHost.add(fade, forKey: "profile.section.opacity")
        let travel = CABasicAnimation(keyPath: "transform.translation.x")
        travel.fromValue = target == 1 && previous < 0.001 ? 16 : priorTravel
        travel.toValue = target == 1 ? 0 : -16
        travel.duration = 0.24; travel.timingFunction = CAMediaTimingFunction(name: .easeOut)
        profileBackgroundHost.add(travel, forKey: "profile.section.travel")
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking = tracking { removeTrackingArea(tracking) }
        let area = NSTrackingArea(rect: bounds,
                                  options: [.mouseEnteredAndExited, .mouseMoved, .activeAlways, .inVisibleRect],
                                  owner: self, userInfo: nil)
        addTrackingArea(area)
        tracking = area
    }

    override func mouseEntered(with event: NSEvent) { updateHover(event) }
    override func mouseMoved(with event: NSEvent) { updateHover(event) }
    override func mouseExited(with event: NSEvent) {
        navigation.hover(nil)
        identityCard.resetInteraction(animated: true)
        chargeBadge.setHovered(false)
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
        let menuCapturesPointer = profileIsInteractive && profileInteraction?.capturesPointer == true
        let hoveredTarget = menuCapturesPointer ? nil : navigationTargetAtDesignPoint(point, forHover: true)
        navigation.hoverTarget(hoveredTarget)
        identityCard.setHovered(menuCapturesPointer ? nil : navigationPoint(point).flatMap { identityCard.target(at: $0) })
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
        if let moduleContent {
            // The map's terrain has no controls. Follow only its short wrapper
            // ancestry while clearing transitions, never its country geometry.
            var mapAncestors = Set<ObjectIdentifier>()
            var ancestor: CALayer? = mapCanvas.layer
            while let item = ancestor {
                mapAncestors.insert(ObjectIdentifier(item)); ancestor = item.superlayer
            }
            func clear(_ item: CALayer) {
                if item === mapCanvas.layer {
                    HUDControlHighlightLayer.update(in: mapCanvas.controlHighlightRoot, point: nil)
                } else if mapAncestors.contains(ObjectIdentifier(item)) {
                    item.sublayers?.forEach(clear)
                } else {
                    HUDControlHighlightLayer.update(in: item, point: nil)
                }
            }
            clear(moduleContent.layer)
        }
        HUDControlHighlightLayer.update(in: notesCanvas.workspaceLayer, point: nil)
        HUDControlHighlightLayer.update(in: navigation.layer, point: nil)
    }

    func refreshControlHighlights() {
        guard let window else { clearControlHighlights(); return }
        updateControlHighlights(at: convert(window.convertPoint(fromScreen: pointerLocationProvider()), from: nil))
    }

    private func updateControlHighlights(at location: CGPoint, pressed: Bool = false) {
        guard allowsModuleInput, !moduleContent.isTransitioning else {
            clearControlHighlights(); return
        }
        let notePoint = notesWorkspaceIsInteractive ? notesWorkspacePoint(location) : nil
        let menuCapturesPointer = profileIsInteractive && profileInteraction?.capturesPointer == true
        let notesCoverPointer = !menuCapturesPointer && notePoint.map { notesCanvas.containsWorkspacePoint($0) } == true
        HUDControlHighlightLayer.update(in: notesCanvas.workspaceLayer,
                                        point: notesCoverPointer ? notePoint : nil, pressed: pressed)
        HUDControlHighlightLayer.update(in: navigation.layer,
                point: notesCoverPointer || menuCapturesPointer ? nil : navigationPoint(designPoint(location)), pressed: pressed)
        if let content = moduleContent.activeContentLayer {
            let root = content === mapCanvas.layer ? mapCanvas.controlHighlightRoot : content
            let point = notesCoverPointer ? nil : centerPoint(designPoint(location))
            HUDControlHighlightLayer.update(in: root,
                point: point.map { root.convert($0, from: content) }, pressed: pressed)
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
        if quitConfirmationPending { return super.hitTest(point) }
        if profileIsInteractive, profileInteraction?.capturesPointer == true,
           bounds.contains(local) {
            return profileInteraction?.hitTestEditor(at: local) ?? self
        }
        guard usesSourceShell, sourceOverviewPresented, bounds.contains(local) else { return super.hitTest(point) }
        let native = super.hitTest(point)
        if let native, native !== self, native !== sourceWatch,
           !native.isDescendant(of: blurBackdrop), native !== blurBackdrop { return native }
        let design = designPoint(local)
        if let center = centerPoint(design), CGRect(origin: .zero, size: selectedModule.contentFrame.size).contains(center) { return self }
        if notesWorkspaceIsInteractive, let note = notesWorkspacePoint(local), notesCanvas.containsWorkspacePoint(note) { return self }
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
            self.workInteraction?.layoutAccessibility()
            self.appShortcutInteraction?.layoutAccessibility()
            self.profileInteraction?.layoutAccessibility()
        }
    }

    override func mouseDown(with event: NSEvent) {
        guard !quitConfirmationPending, !quitCommitted else { return }
        summonedDuringFileDrag = false
        // The controller can queue an outside-click dismissal while the finite
        // entrance finishes; controls remain disabled throughout the transition.
        guard interactionEnabled || transitioning else { return }
        let location = convert(event.locationInWindow, from: nil)
        let p = designPoint(location)
        if profileIsInteractive, profileInteraction?.capturesPointer == true {
            _ = profileInteraction?.mouseDown(at: centerPoint(p) ?? CGPoint(x: -1, y: -1), event: event)
            return
        }
        if notesWorkspaceIsInteractive, let local = notesWorkspacePoint(location),
           notesInteraction?.mouseDownInWorkspace(at: local, clickCount: event.clickCount) == true { return }
        if notesWorkspaceIsInteractive {
            notesInteraction?.finishEditing()
            notesCanvas.clearSelection()
        }
        if !usesSourceShell, allowsModuleInput, let local = navigationPoint(p),
           identityCard.target(at: local) == .close { presentQuitConfirmation(); return }
        if let target = navigationTargetAtDesignPoint(p), target == .module(.power) || target == .module(.profile) {
            activateNavigationTarget(target); return
        }
        if !usesSourceShell, allowsModuleInput, let local = navigationPoint(p),
           navigation.handleScrollClick(at: local) { return }
        if profileIsInteractive, let local = centerPoint(p),
           profileInteraction?.mouseDown(at: local, event: event) == true { return }
        if appShortcutsAreInteractive, let local = centerPoint(p),
           appShortcutInteraction?.mouseDown(at: local, event: event) == true { return }
        if let settings = settingsInteraction, let local = centerPoint(p),
           settings.mouseDown(at: local, event: event) { return }
        if notesAreInteractive, let local = centerPoint(p),
           notesInteraction?.mouseDown(at: local, clickCount: event.clickCount) == true { return }
        if shelfIsInteractive, let local = centerPoint(p),
           shelfInteraction?.mouseDown(at: local, event: event) == true { return }
        if clipboardIsInteractive, let local = centerPoint(p),
           clipboardInteraction?.mouseDown(at: local, event: event) == true { return }
        if eventLogIsInteractive, let local = centerPoint(p),
           eventLogInteraction?.mouseDown(at: local, event: event) == true { return }
        if mapIsInteractive, navigationTargetAtDesignPoint(p) == nil, let local = centerPoint(p),
           mapInteraction?.mouseDown(at: local, event: event) == true { return }
        if volumeIsInteractive, let local = centerPoint(p),
           volumeInteraction?.mouseDown(at: local, event: event) == true { return }
        if storageIsInteractive, let local = centerPoint(p),
           storageInteraction?.mouseDown(at: local, event: event) == true { return }
        if activityIsInteractive, let local = centerPoint(p),
           activityInteraction?.mouseDown(at: local, event: event) == true { return }
        if workIsInteractive, let local = centerPoint(p),
           workInteraction?.mouseDown(at: local, event: event) == true { return }
        notesInteraction?.finishEditing()
        if let target = navigationTargetAtDesignPoint(p) { activateNavigationTarget(target); return }
        if !usesSourceShell, let local = navigationPoint(p), navigation.containsNavigationPoint(local, includingBottom: false) { return }
        let dx = p.x - 500, dy = p.y - 320
        let insideRing = dx * dx + dy * dy < 290 * 290
        if !insideRing { onClose?() }
    }

    override func keyDown(with event: NSEvent) {
        if quitConfirmation?.handleKey(event) == true { return }
        guard allowsModuleInput, !event.isARepeat else { return }
        let modifiers = event.modifierFlags.intersection([.command, .option, .control, .shift])
        if scaleSafety?.handleKey(event) == true { return }
        if settingsInteraction?.keyDown(event) == true { return }
        if appShortcutsAreInteractive && appShortcutInteraction?.keyDown(event) == true { return }
        if profileIsInteractive && profileInteraction?.keyDown(event) == true { return }
        if notesWorkspaceIsInteractive && notesInteraction?.keyDown(event) == true { return }
        if shelfIsInteractive && shelfInteraction?.keyDown(event) == true { return }
        if clipboardIsInteractive && clipboardInteraction?.keyDown(event) == true { return }
        if eventLogIsInteractive && eventLogInteraction?.keyDown(event) == true { return }
        if mapIsInteractive && mapInteraction?.keyDown(event) == true { return }
        if volumeIsInteractive && volumeInteraction?.keyDown(event) == true { return }
        if workIsInteractive && workInteraction?.keyDown(event) == true { return }
        if storageIsInteractive && storageInteraction?.keyDown(event) == true { return }
        if activityIsInteractive && activityInteraction?.keyDown(event) == true { return }
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
            let planes = depthPlanes + [notesPlane]
            let transforms = planes.map { $0.spatial.transform }
            CATransaction.begin(); CATransaction.setDisableActions(true)
            planes.forEach { $0.spatial.transform = CATransform3DIdentity }
            // The footer deliberately lives below the 640-point design box.
            // Include its real unprojected frame without moving any live layer.
            let nativeRasterBounds = canvas.bounds.union(hintLabel.convert(hintLabel.bounds, to: canvas)).insetBy(dx: -2, dy: -2)
            HUDSourceWatchView.renderProjectedContent(canvas, opacity: canvas.opacity, clip: nil,
                flippedRaster: true, projection: projection, rasterBounds: nativeRasterBounds, subdivisions: 16, in: context)
            var notesProjection = CATransform3DMakeTranslation(-designOrigin.x, -designOrigin.y, 0)
            notesProjection = CATransform3DConcat(notesProjection, CATransform3DMakeScale(1 / designScale, 1 / designScale, 1))
            notesProjection = CATransform3DConcat(notesProjection, projection)
            HUDSourceWatchView.renderProjectedContent(notesWorkspace, opacity: notesWorkspace.opacity, clip: nil,
                flippedRaster: true, projection: notesProjection, subdivisions: 16, in: context)
            for (plane, transform) in zip(planes, transforms) { plane.spatial.transform = transform }
            CATransaction.commit()
        } else {
            context.saveGState()
            context.translateBy(x: designOrigin.x, y: designOrigin.y)
            context.scaleBy(x: designScale, y: designScale)
            (presentation ? canvas.presentation() ?? canvas : canvas).render(in: context)
            context.restoreGState()
            (presentation ? notesWorkspace.presentation() ?? notesWorkspace : notesWorkspace).render(in: context)
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
            let view = try HUDSourceWatchView(frame: bounds, desktopMode: true)
            view.isHidden = true
            view.pointerLocationProvider = { [weak self] in self?.pointerLocationProvider() ?? NSEvent.mouseLocation }
            view.onAction = { [weak self] action in
                guard let self, self.allowsModuleInput else { return }
                self.activateNavigationTarget(action.target)
                self.window?.makeFirstResponder(self)
            }
            view.onClose = { [weak self] in
                guard let self, self.allowsModuleInput else { return }
                self.onClose?()
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
            view.onDesktopCenterPlane = { [weak self] projection in self?.applySourceCenterProjection(projection) }
            view.isDesktopPointerLocked = { [weak self] in self?.isModuleInputLocked ?? false }
            view.layer?.zPosition = -1000
            blurBackdrop.layer?.zPosition = -2000
            backdrop.zPosition = -1500
            sourceWatch = view
            refreshIdentityProfile()
            refreshAppNavigation(animated: false)
            addSubview(view, positioned: .below, relativeTo: closeHUDButton)
        } catch { presentSourceFailure(String(describing: error)) }
    }

    /// Keep one plane for painting, inverse hits, editors and accessibility.
    /// The source camera already includes HUD scale, position and gyro easing.
    private func applySourceCenterProjection(_ projection: CATransform3D) {
        guard usesSourceShell, designScale > 0 else { return }
        sourceCenterProjection = projection
        let local = HUDMotionMath.sourcePlaneTransform(projection, origin: designOrigin, scale: designScale)
        if let previous = motion.externalProjection, CATransform3DEqualToTransform(previous, local),
           (depthPlanes + [notesPlane]).allSatisfy({ CATransform3DEqualToTransform($0.spatial.transform, local) }) { return }
        motion.setExternalProjection(local)
        // Inline fields are real AppKit views; keep them on the final displayed
        // surface when an edit or direct manipulation freezes pointer input.
        if isModuleInputLocked {
            notesInteraction?.layoutAccessibility()
            workInteraction?.layoutAccessibility()
            appShortcutInteraction?.layoutAccessibility()
            profileInteraction?.layoutAccessibility()
            settingsInteraction?.layoutAccessibility()
        }
    }

    private func presentSourceFailure(_ reason: String) {
        let wasRetracting = retracting
        let wasPresented = sourceOverviewPresented
        let handler = transitionCompletion
        let ready = sourceEntranceReady
        transitionCompletion = nil; sourceEntranceReady = nil
        sourceWatchFailureReason = reason
        sourceCenterProjection = nil
        motion.setExternalProjection(nil)
        sourceWatch?.conceal()
        refreshAppNavigation(animated: false)
        let field = sourceWatchFailure ?? NSTextField(wrappingLabelWithString: "")
        NSLog("Source Watch render failure: %@", reason)
        field.stringValue = L10n.text("The original menu could not be rendered. Please check the log.", "原始菜单无法渲染，请查看日志。")
        field.textColor = .labelColor
        field.alignment = .center
        field.font = .systemFont(ofSize: 15)
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
            self.artwork.groups.forEach { $0.isHidden = overview }
            self.identityCard.layer.isHidden = overview
            self.navigation.layer.isHidden = overview
            self.navigation.bottomLayer.isHidden = overview
            self.industryWordmark.isHidden = overview
            self.progress.isHidden = overview || self.selectedModule == .workMode
        }
        headerClock.setActive(window != nil && interactionEnabled)
        sourceWatchFailure?.isHidden = !overview || !sourceOverviewPresented || sourceWatchFailureReason == nil
        guard let sourceWatch else { return }
        if overview && sourceOverviewPresented && sourceWatchFailureReason == nil {
            sourceWatch.isHidden = false
            if stable, sourceWatch.playback.phase == .concealed { sourceWatch.showStable() }
            sourceWatch.inputEnabled = allowsModuleInput
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
        distantPlane.content.addSublayer(artwork.distant)
        rearPlane.content.addSublayer(artwork.rear)
        secondaryPlane.content.addSublayer(artwork.secondary)
        framePlane.content.addSublayer(artwork.frame)
        innerPlane.content.addSublayer(artwork.inner)
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
    }

    private func registerTriangleMotion() {
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
        core.frame = CGRect(x: 300, y: 152, width: 400, height: 334)
        // The content controller adopts this existing battery page after it is built.
        batteryHeading = text("", rect: CGRect(x: 0, y: 8, width: 400, height: 22), size: 11,
                              parent: core, role: .muted, alignment: .center)
        laptop.frame = CGRect(x: 163, y: 42, width: 74, height: 51)
        let machine = CGMutablePath()
        machine.addRoundedRect(in: CGRect(x: 12, y: 2, width: 50, height: 34), cornerWidth: 2, cornerHeight: 2)
        machine.move(to: CGPoint(x: 12, y: 38)); machine.addLine(to: CGPoint(x: 4, y: 45))
        machine.addLine(to: CGPoint(x: 70, y: 45)); machine.addLine(to: CGPoint(x: 62, y: 38)); machine.closeSubpath()
        laptop.path = machine; laptop.fillColor = nil; laptop.lineWidth = 2
        core.addSublayer(laptop); foregroundShapes.append(laptop)
        percent = text("—", rect: CGRect(x: 0, y: 96, width: 400, height: 96), size: 82,
                       parent: core, role: .primary, alignment: .center, weight: .light)
        centerState = text("", rect: CGRect(x: 0, y: 196, width: 400, height: 27), size: 18,
                           parent: core, role: .primary, alignment: .center, weight: .medium)
        centerSource = text("", rect: CGRect(x: 0, y: 232, width: 400, height: 22), size: 11,
                            parent: core, role: .muted, alignment: .center)
    }

    private func buildPanels() {
        capacityLabel = text("", rect: CGRect(x: 0, y: 264, width: 400, height: 20), size: 12,
                             parent: core, role: .primary, alignment: .center)
        healthLabel = text("", rect: CGRect(x: 0, y: 291, width: 400, height: 20), size: 10,
                           parent: core, role: .muted, alignment: .center)
        moduleContent = HUDModuleContent(powerLayer: core)
        moduleContent.register(notesCanvas, for: .notes)
        moduleContent.register(shelfCanvas, for: .fileShelf)
        moduleContent.register(clipboardCanvas, for: .clipboard)
        moduleContent.register(eventLogCanvas, for: .eventLog)
        moduleContent.register(mapCanvas, for: .map)
        moduleContent.register(volumeCanvas, for: .volume)
        moduleContent.register(workCanvas, for: .workMode)
        moduleContent.register(storageCanvas, for: .storage)
        moduleContent.register(activityCanvas, for: .activityMonitor)
        moduleContent.register(appShortcutCanvas, for: .addApp)
        moduleContent.register(profileCanvas, for: .profile)
        chargePlane.content.addSublayer(chargeBadge.layer)
        chargeBadge.onHitRegionChanged = { [weak self] in
            self?.refreshChargeHover()
            self?.updateButtonStates()
        }
        if let settingsController {
            for module: HUDModule in [.system, .display, .hotkeys, .about] {
                let settings = HUDSettingsCanvas(module: module, controller: settingsController)
                settingsCanvases[module] = settings
                moduleContent.register(settings, for: module)
            }
        }
        profileBackgroundHost.name = "profile.backdrop.host"
        profileBackgroundHost.frame = HUDModuleContent.viewportFrame
        profileBackgroundHost.position = CGPoint(x: 500, y: 294)
        profileBackgroundHost.transform = CATransform3DMakeScale(0.86, 0.86, 1)
        profileBackgroundHost.masksToBounds = false
        profileBackgroundHost.opacity = 0
        profileCanvas.backgroundLayer.frame = HUDModule.profile.contentFrame.offsetBy(
            dx: -HUDModuleContent.viewportFrame.minX, dy: -HUDModuleContent.viewportFrame.minY)
        profileBackgroundHost.addSublayer(profileCanvas.backgroundLayer)
        profileBackgroundPlane.content.addSublayer(profileBackgroundHost)
        moduleContrast.name = "module.contrast"
        moduleContrast.frame = HUDModuleContent.viewportFrame
        moduleContrast.type = .radial
        moduleContrast.startPoint = CGPoint(x: 0.5, y: 0.5)
        moduleContrast.endPoint = CGPoint(x: 1, y: 1)
        moduleContrast.locations = [0, 0.60, 1]
        corePlane.content.addSublayer(moduleContrast)
        corePlane.content.addSublayer(moduleContent.layer)
        corePlane.content.addSublayer(navigation.bottomLayer)
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
        header.frame = CGRect(x: 270, y: 2, width: 600, height: 74)
        corePlane.content.addSublayer(header)
        titleLabel = text("ENDFIELDHUD", rect: CGRect(x: 0, y: 0, width: 300, height: 27), size: 20,
                          parent: header, role: .primary, weight: .semibold)
        _ = text("SYSTEM INTERFACE", rect: CGRect(x: 0, y: 31, width: 300, height: 18),
                 size: 9, parent: header, role: .muted)
        clockTime = text("", rect: CGRect(x: 340, y: 0, width: 220, height: 27), size: 20,
                         parent: header, role: .primary, alignment: .right, weight: .semibold)
        clockDate = text("", rect: CGRect(x: 340, y: 31, width: 220, height: 18), size: 9,
                         parent: header, role: .muted, alignment: .right)
        clockTime.name = "hud.clock.time"; clockDate.name = "hud.clock.date"
        clockTime.actions = ["contents": NSNull()]; clockDate.actions = ["contents": NSNull()]
        headerClock.onChange = { [weak self] reading in
            self?.withoutActions { self?.clockTime.string = reading.time; self?.clockDate.string = reading.date }
        }
        workBadge = text("", rect: CGRect(x: 320, y: 55, width: 240, height: 18), size: 10,
                         parent: header, role: .accent, alignment: .right, weight: .semibold)
        workBadge.name = "hud.workMode.badge"
        workBadge.actions = ["contents": NSNull()]
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

    }

    private func buildNavigation() {
        panelsPlane.content.addSublayer(navigation.layer)
        panelsPlane.content.addSublayer(identityCard.layer)
        navigation.onLayoutChange = { [weak self] in
            self?.updateNavigationGeometry()
            self?.updateButtonStates()
        }
        synchronizeNavigationButtons()
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

    private func refreshAppNavigation(animated: Bool) {
        let items = (appShortcutStore?.items ?? []).map { item in
            HUDAppShortcutPresentation(id: item.id, name: item.name, iconPreset: item.iconPreset,
                icon: item.iconPreset == .original ? appShortcutStore?.icon(for: item.id) : nil)
        }
        // The source row pool owns live app navigation. Populate the retained
        // fallback's native cards only when that shell is actually in use.
        navigation.updateAppShortcuts(usesSourceShell ? [] : items, animated: animated && !usesSourceShell)
        sourceWatch?.setDesktopNavigation(HUDDesktopWatchNavigation.entries(shortcuts: items))
        synchronizeNavigationButtons()
        updateNavigationGeometry()
        updateButtonStates()
    }

    private func synchronizeNavigationButtons() {
        let targets = Set(navigation.entries.map(\.target))
        for target in Array(navigationButtons.keys) where !targets.contains(target) {
            navigationButtons.removeValue(forKey: target)?.removeFromSuperview()
        }
        for entry in navigation.entries where navigationButtons[entry.target] == nil {
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

    func presentQuitConfirmation() {
        guard allowsModuleInput, window != nil else { return }
        quitConfirmationPending = true
        updateButtonStates()
        clearControlHighlights()
        navigation.hover(nil); identityCard.resetInteraction(animated: true)
        let prompt = quitConfirmation ?? HUDQuitConfirmationView(frame: bounds)
        quitConfirmation = prompt
        prompt.frame = bounds
        prompt.autoresizingMask = [.width, .height]
        prompt.configure(dark: currentDark, accent: currentAccent)
        prompt.onPointerMove = { [weak self] in self?.followCurrentPointer() }
        prompt.onCancel = { [weak self] in self?.cancelQuitConfirmation() }
        prompt.onConfirm = { [weak self] in self?.confirmQuit() }
        if prompt.superview == nil { addSubview(prompt, positioned: .above, relativeTo: nil) }
        prompt.show()
    }

    private func cancelQuitConfirmation() {
        guard quitConfirmationPending, !quitCommitted else { return }
        quitConfirmation?.dismiss(animated: true) { [weak self] in
            guard let self, !self.quitCommitted else { return }
            self.quitConfirmationPending = false
            self.updateButtonStates()
            if self.interactionEnabled { self.window?.makeFirstResponder(self) }
        }
    }

    private func confirmQuit() {
        guard quitConfirmationPending, !quitCommitted else { return }
        quitCommitted = true
        // Accept synchronously so focus loss cannot cancel a queued quit while
        // the confirmation fades. The owner's normal closing path removes it.
        onQuitConfirmed?()
    }

    var quitConfirmationVisibleForVerification: Bool { quitConfirmationPending }
    func answerQuitConfirmationForVerification(_ confirm: Bool) {
        if confirm { confirmQuit() } else { cancelQuitConfirmation() }
    }

    @objc private func scrollNavigation(_ sender: NSButton) {
        guard allowsModuleInput, sender.isEnabled else { return }
        navigation.scrollRows(sender.tag)
    }

    private func updateButtonStates() {
        sourceWatch?.inputEnabled = usesSourceShell && sourceOverviewPresented && allowsModuleInput
        notesInteraction?.setActive(notesWorkspaceIsInteractive)
        shelfInteraction?.setActive(shelfIsInteractive)
        clipboardInteraction?.setActive(clipboardIsInteractive)
        eventLogInteraction?.setActive(eventLogIsInteractive)
        mapInteraction?.setActive(mapIsInteractive)
        volumeInteraction?.setActive(volumeIsInteractive)
        workInteraction?.setActive(workIsInteractive)
        storageInteraction?.setActive(storageIsInteractive)
        activityInteraction?.setActive(activityIsInteractive)
        appShortcutInteraction?.setActive(appShortcutsAreInteractive)
        profileInteraction?.setActive(profileIsInteractive)
        for (module, input) in settingsInteractions {
            input.setActive(allowsModuleInput && !moduleContent.isTransitioning && selectedModule == module)
        }
        closeHUDButton.isHidden = usesSourceShell
        closeHUDButton.isEnabled = allowsModuleInput && !usesSourceShell
        closeHUDButton.setAccessibilityLabel(L10n.text("Quit EndfieldHUD", "退出 EndfieldHUD"))
        let visible = Set(navigation.visibleEntries.map(\.target))
        for entry in navigation.entries {
            guard let button = navigationButtons[entry.target] else { continue }
            button.isHidden = (usesSourceShell && entry.module != .power) || !visible.contains(entry.target)
            button.isEnabled = allowsModuleInput && !button.isHidden
            button.setAccessibilityLabel(entry.navigationTitle)
            if entry.module == .power { button.setAccessibilityHelp(chargeBadge.accessibilityLabel) }
            button.setAccessibilityValue(entry.isSelected ? L10n.text("Selected", "已选择") : L10n.text("Not selected", "未选择"))
        }
        for (direction, button) in navigationScrollButtons {
            button.isHidden = usesSourceShell
            button.isEnabled = allowsModuleInput && (direction < 0 ? navigation.canScrollUp : navigation.canScrollDown)
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
        if usesSourceShell {
            return sourceWatch?.navigationTarget(at: CGPoint(x: designOrigin.x + point.x * designScale,
                                                            y: designOrigin.y + point.y * designScale))
        }
        let target = forHover ? navigation.hoverHitTarget(point: local, includingBottom: false)
            : navigation.hitTarget(point: local, includingBottom: false)
        return target == .module(.power) || target == .module(.profile) ? nil : target
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
        closeHUDButton.frame = viewRect(projectedBounds(identityCard.closeRect.insetBy(dx: -10, dy: -10), through: transforms))
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
            let panelColor = dark ? NSColor(white: 0.10, alpha: 0.96) : NSColor(white: 0.94, alpha: 0.98)
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
            for shape in self.panelShapes { shape.fillColor = panelColor.cgColor; shape.strokeColor = foreground.withAlphaComponent(0.18).cgColor }
            for shape in self.foregroundShapes { shape.strokeColor = foreground.cgColor }
            for shape in self.structuralShapes { shape.strokeColor = yellow.cgColor }
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
            self.currentAccent = yellow
            self.currentBatteryTone = tone
            self.artwork.update(dark: dark, chargeColor: self.selectedModule == .power ? tone : yellow, accentColor: yellow)
            let contentScale = (self.window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2) * self.designScale
            self.navigation.update(dark: dark, accent: yellow, contentsScale: contentScale)
            self.chargeBadge.update(snapshot: self.snapshot, configuration: self.configuration, dark: dark, contentsScale: contentScale)
            self.identityCard.update(dark: dark, accent: yellow, contentsScale: contentScale)
            self.notesCanvas.updateAppearance(style: HUDModuleContentStyle(dark: dark, accent: yellow,
                contentsScale: self.window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2))
            self.moduleContent?.update(dark: dark, accent: yellow, contentsScale: contentScale)
            self.updateButtonStates()
            self.progress.strokeColor = (self.selectedModule == .power ? tone : yellow).cgColor
            self.progress.isHidden = self.selectedModule == .workMode
            self.progress.strokeEnd = self.selectedModule == .power ? CGFloat(self.snapshot.percentage ?? 0) / 100 : 0.12
            self.percent.string = self.snapshot.percentage.map { "\($0)%" } ?? "—"
            self.percent.foregroundColor = tone.cgColor
            let state: String
            if !self.snapshot.hasBattery { state = L10n.text("No internal battery", "无内置电池") }
            else if self.snapshot.isCharging { state = L10n.text("CHARGING", "正在充电") }
            else if self.snapshot.isFullyCharged { state = L10n.text("FULLY CHARGED", "已充满") }
            else if self.snapshot.isPluggedIn { state = L10n.text("POWER CONNECTED", "已连接电源") }
            else { state = L10n.text("ON BATTERY", "电池供电") }
            let source = self.snapshot.isPluggedIn ? L10n.text("External power", "外接电源")
                : (self.snapshot.hasBattery ? L10n.text("Battery power", "电池供电") : L10n.text("Unavailable", "暂无数据"))
            self.centerState.string = state
            self.centerSource.string = L10n.text("SOURCE  /  ", "电源  /  ") + source
            self.batteryHeading.string = L10n.text("// BATTERY STATUS", "// 电池状态")
            let number = NumberFormatter(); number.numberStyle = .decimal
            if let capacity = self.snapshot.capacity {
                let current = number.string(from: NSNumber(value: capacity.current)) ?? "\(capacity.current)"
                let maximum = number.string(from: NSNumber(value: capacity.maximum)) ?? "\(capacity.maximum)"
                self.capacityLabel.string = "\(current) / \(maximum) \(capacity.unit.rawValue)"
            } else { self.capacityLabel.string = "—" }
            self.healthLabel.string = self.snapshot.healthCategory ?? L10n.text("Unavailable", "暂无数据")
            self.updateWorkPresentation(force: true)
            self.hintLabel.string = "ESC / \(self.configuration.summonShortcut.displayName.uppercased()) / " + L10n.text("CLICK OUTSIDE TO CLOSE", "点击外侧关闭")
            self.setAccessibilityLabel(L10n.text("System interface", "系统界面") + ", " + self.selectedModule.title)
            self.updateContentsScale()
        }
    }

    private enum TextRole { case primary, muted, accent }
    private func text(_ value: String, rect: CGRect, size: CGFloat, parent: CALayer,
                      role: TextRole, alignment: CATextLayerAlignmentMode = .left,
                      weight: NSFont.Weight = .regular) -> CATextLayer {
        let label = CATextLayer()
        label.frame = rect; label.string = value; label.font = NSFont.monospacedSystemFont(ofSize: size, weight: weight)
        label.fontSize = size; label.alignmentMode = alignment; label.truncationMode = .end
        label.isWrapped = false
        parent.addSublayer(label)
        switch role {
        case .primary: primaryTexts.append(label)
        case .muted: mutedTexts.append(label)
        case .accent: accentTexts.append(label)
        }
        return label
    }

    private func plate(in parent: CALayer, rect: CGRect, cut: CGFloat) -> CAShapeLayer {
        let item = CAShapeLayer(); item.frame = parent.bounds
        let path = CGMutablePath()
        path.move(to: CGPoint(x: rect.minX + cut, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX - cut, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY)); path.closeSubpath()
        item.path = path; item.lineWidth = 1
        parent.addSublayer(item); panelShapes.append(item)
        return item
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
        let scale = (window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2) * designScale
        shelfCanvas.updateRenderScale(scale)
        clipboardCanvas.updateRenderScale(scale)
        eventLogCanvas.updateRenderScale(scale)
        mapCanvas.updateRenderScale(scale)
        volumeCanvas.updateRenderScale(scale)
        workCanvas.updateRenderScale(scale)
        storageCanvas.updateRenderScale(scale)
        activityCanvas.updateRenderScale(scale)
        profileCanvas.updateRenderScale(scale)
        // Keep the factory's scale current too, so a page created after layout
        // uses the same glyph resolution as pages already on screen.
        moduleContent?.update(dark: currentDark, accent: currentAccent, contentsScale: scale)
        func apply(_ item: CALayer) {
            if !(item is CATransformLayer) {
                let target = HUDRenderScale.contentScale(for: item, baseScale: scale)
                if item.contentsScale != target { item.contentsScale = target }
            }
            item.sublayers?.forEach(apply)
            if let mask = item.mask { apply(mask) }
        }
        apply(canvas)
        chargeBadge.update(snapshot: snapshot, configuration: configuration, dark: currentDark, contentsScale: scale)
        identityCard.update(dark: currentDark, accent: currentAccent, contentsScale: scale)
        notesCanvas.updateRenderScale(window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2)
    }

    private func configureNotesInteraction() {
        let input = HUDNotesInteraction(canvas: notesCanvas, host: self)
        input.project = { [weak self] rect in
            guard let self = self else { return .zero }
            return self.projectCenterRect(rect)
        }
        input.workspaceProject = { [weak self] rect in self?.projectNotesRect(rect) ?? .zero }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        input.onToggle = { [weak self] in self?.onToggle?() }
        input.isDark = { [weak self] in self?.currentDark ?? true }
        notesInteraction = input
    }

    private func configureSettingsInteraction() {
        guard let settingsController else { return }
        for (module, canvas) in settingsCanvases {
            let input = HUDSettingsInteraction(canvas: canvas, host: self)
            input.project = { [weak self] rect in self?.projectCenterRect(rect) ?? .zero }
            input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
            settingsInteractions[module] = input
        }
        let safety = HUDScaleSafetyView(controller: settingsController)
        addSubview(safety)
        scaleSafety = safety
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
        input.project = { [weak self] rect in
            guard let self = self else { return .zero }
            return self.projectCenterRect(rect)
        }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        input.onDragSessionBegan = { [weak self] in self?.onShelfDragSessionBegan?() }
        input.onDragSessionEnded = { [weak self] delivered in self?.onShelfDragSessionEnded?(delivered) }
        input.onRevealRequested = { [weak self] url in self?.onShelfReveal?(url) }
        shelfInteraction = input
    }

    private func coreDesignPoint(_ point: CGPoint) -> CGPoint? {
        let transform = corePlane.spatial.presentation()?.transform ?? corePlane.spatial.transform
        guard let local = Self.unproject(CGPoint(x: point.x - 500, y: point.y - 320), transform: transform) else { return nil }
        return CGPoint(x: local.x + 500, y: local.y + 320)
    }

    private var reportScale: CGFloat { selectedModule == .workMode || selectedModule == .map ? 1 : 0.86 }
    private var reportCenterY: CGFloat {
        guard selectedModule == .workMode || selectedModule == .map else { return usesSourceShell ? 285 : 294 }
        // Leave room for the source shell's lower buttons without shrinking the
        // map or countdown. Drawing, editors and hit-testing share this origin.
        return usesSourceShell ? 285 : 320
    }
    private func reportRect(_ rect: CGRect) -> CGRect {
        CGRect(x: 500 + (rect.minX - 500) * reportScale, y: reportCenterY + (rect.minY - 320) * reportScale,
               width: rect.width * reportScale, height: rect.height * reportScale)
    }
    private func centerPoint(_ point: CGPoint) -> CGPoint? {
        guard let local = coreDesignPoint(point) else { return nil }
        let frame = selectedModule.contentFrame
        return CGPoint(x: (local.x - 500) / reportScale + 500 - frame.minX,
                       y: (local.y - reportCenterY) / reportScale + 320 - frame.minY)
    }

    private func configureClipboardInteraction() {
        let input = HUDClipboardInteraction(canvas: clipboardCanvas, host: self)
        input.project = { [weak self] rect in
            guard let self = self else { return .zero }
            return self.projectCenterRect(rect)
        }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        clipboardInteraction = input
    }

    private func configureVolumeInteraction() {
        let input = HUDVolumeInteraction(canvas: volumeCanvas, host: self)
        input.project = { [weak self] rect in self?.projectCenterRect(rect) ?? .zero }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        volumeInteraction = input
    }

    private func configureEventLogInteraction() {
        let input = HUDEventLogInteraction(canvas: eventLogCanvas, host: self)
        input.project = { [weak self] rect in self?.projectCenterRect(rect) ?? .zero }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        eventLogInteraction = input
    }

    private func configureWorkInteraction() {
        let input = HUDWorkModeInteraction(canvas: workCanvas, host: self)
        input.project = { [weak self] rect in self?.projectCenterRect(rect) ?? .zero }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        input.onToggle = { [weak self] in self?.onToggle?() }
        workInteraction = input
    }

    private func configureAppShortcutInteraction() {
        let input = HUDAppShortcutInteraction(canvas: appShortcutCanvas, host: self)
        input.project = { [weak self] rect in self?.projectCenterRect(rect) ?? .zero }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        input.onToggle = { [weak self] in self?.onToggle?() }
        input.isDark = { [weak self] in self?.currentDark ?? true }
        appShortcutCanvas.onLaunch = { [weak self] id in self?.onLaunchAppShortcut?(id) }
        appShortcutCanvas.onSaved = { [weak self] _ in self?.refreshAppNavigation(animated: true) }
        appShortcutCanvas.onRemoved = { [weak self] _ in self?.refreshAppNavigation(animated: true) }
        appShortcutInteraction = input
    }

    private func configureProfileInteraction() {
        let input = HUDPersonalProfileInteraction(canvas: profileCanvas, host: self)
        input.project = { [weak self] rect in self?.projectCenterRect(rect) ?? .zero }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        input.onToggle = { [weak self] in self?.onToggle?() }
        input.isDark = { [weak self] in self?.currentDark ?? true }
        profileCanvas.onGeometryPreview = { [weak self] profile in
            guard let self, let store = self.profileStore else { return }
            self.identityCard.setProfile(profile, avatar: store.image(for: .avatar), background: store.image(for: .background),
                                         avatarOrientation: store.imageOrientation(for: .avatar))
            self.sourceWatch?.setDesktopProfile(profile, avatar: store.image(for: .avatar), background: store.image(for: .background),
                                                avatarOrientation: store.imageOrientation(for: .avatar))
        }
        profileInteraction = input
    }

    private func configureMapInteraction() {
        let input = HUDWorldMapInteraction(canvas: mapCanvas, host: self)
        input.project = { [weak self] rect in self?.projectCenterRect(rect) ?? .zero }
        input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        mapInteraction = input
    }

    private func refreshIdentityProfile() {
        guard let store = profileStore else { return }
        identityCard.setProfile(store.profile, avatar: store.image(for: .avatar), background: store.image(for: .background),
                                avatarOrientation: store.imageOrientation(for: .avatar))
        sourceWatch?.setDesktopProfile(store.profile, avatar: store.image(for: .avatar), background: store.image(for: .background),
                                      avatarOrientation: store.imageOrientation(for: .avatar))
    }

    private func configureTelemetryInteractions() {
        let storage = HUDTelemetryInteraction(canvas: storageCanvas, host: self)
        let activity = HUDTelemetryInteraction(canvas: activityCanvas, host: self)
        for input in [storage, activity] {
            input.project = { [weak self] rect in self?.projectCenterRect(rect) ?? .zero }
            input.onLock = { [weak self] in self?.lockParallaxForActiveInput() }
        }
        storageCanvas.onOpenSystemStorage = { [weak self] in self?.onOpenSystemStorage?() }
        storageInteraction = storage; activityInteraction = activity
    }

    private func projectCenterRect(_ rect: CGRect) -> CGRect {
        let transform = corePlane.spatial.presentation()?.transform ?? corePlane.spatial.transform
        let frame = selectedModule.contentFrame
        return viewRect(projectedBounds(reportRect(rect.offsetBy(dx: frame.minX, dy: frame.minY)), through: [transform]))
    }

    private func installClickFeedbackMonitor() {
        guard clickFeedbackMonitor == nil else { return }
        // App-local pointer events only. Observing before responder dispatch also
        // covers NSButtons and NSTextViews without intercepting their behavior.
        clickFeedbackMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown, .leftMouseUp]) { [weak self] event in
            guard let self, let window = self.window, event.window === window,
                  window.isVisible, !self.retracting, self.interactionEnabled || self.transitioning else { return event }
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

    private func deactivateModuleInput() {
        clearControlHighlights()
        notesInteraction?.deactivate()
        shelfInteraction?.deactivate()
        clipboardInteraction?.deactivate()
        eventLogInteraction?.deactivate()
        mapInteraction?.deactivate()
        volumeInteraction?.deactivate()
        workInteraction?.deactivate()
        storageInteraction?.deactivate()
        activityInteraction?.deactivate()
        appShortcutInteraction?.deactivate()
        profileInteraction?.deactivate()
        settingsInteractions.values.forEach { $0.deactivate() }
    }

    private func updateWorkPresentation(force: Bool = false) {
        profileCanvas.refreshWorkDuration()
        let phase = workModeController.snapshot.phase
        guard force || displayedWorkPhase != phase else { return }
        displayedWorkPhase = phase
        let label: String
        switch phase {
        case .running: label = "WORK MODE / ACTIVE"
        case .paused: label = "WORK MODE / PAUSED"
        default: label = ""
        }
        withoutActions { workBadge.string = label; workBadge.isHidden = label.isEmpty }
        navigationButtons[.module(.workMode)]?.setAccessibilityHelp(label.isEmpty ? nil : label)
    }

    override func mouseDragged(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil)
        if notesWorkspaceIsInteractive, notesCanvas.isDragging, let local = notesWorkspacePoint(location) {
            notesInteraction?.mouseDraggedInWorkspace(to: local); return
        }
        guard let point = centerPoint(designPoint(location)) else { return }
        if let settings = settingsInteraction { settings.mouseDragged(to: point) }
        else if profileIsInteractive { profileInteraction?.mouseDragged(to: point) }
        else if mapIsInteractive { mapInteraction?.mouseDragged(to: point) }
        else if shelfIsInteractive { shelfInteraction?.mouseDragged(to: point, event: event) }
        else if volumeIsInteractive { volumeInteraction?.mouseDragged(to: point) }
        else if workIsInteractive { workInteraction?.mouseDragged(to: point, event: event) }
    }

    override func mouseUp(with event: NSEvent) {
        summonedDuringFileDrag = false
        notesInteraction?.mouseUp()
        shelfInteraction?.mouseUp()
        clipboardInteraction?.mouseUp()
        eventLogInteraction?.mouseUp()
        mapInteraction?.mouseUp()
        volumeInteraction?.mouseUp()
        workInteraction?.mouseUp()
        storageInteraction?.mouseUp()
        activityInteraction?.mouseUp()
        appShortcutInteraction?.mouseUp()
        profileInteraction?.mouseUp()
        settingsInteractions.values.forEach { $0.mouseUp() }
    }

    override func rightMouseDown(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil), design = designPoint(location)
        if notesWorkspaceIsInteractive, let point = notesWorkspacePoint(location), notesCanvas.containsWorkspacePoint(point) {
            super.rightMouseDown(with: event); return
        }
        if mapIsInteractive, navigationTargetAtDesignPoint(design) == nil, let point = centerPoint(design),
           mapInteraction?.rightMouseDown(at: point, event: event) == true { return }
        super.rightMouseDown(with: event)
    }

    override func magnify(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil), design = designPoint(location)
        if mapIsInteractive, navigationTargetAtDesignPoint(design) == nil, let point = centerPoint(design),
           mapInteraction?.magnify(at: point, event: event) == true { return }
        super.magnify(with: event)
    }

    override func scrollWheel(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil)
        let design = designPoint(location)
        if notesWorkspaceIsInteractive, let local = notesWorkspacePoint(location), notesCanvas.containsWorkspacePoint(local) {
            notesInteraction?.finishEditing()
            if notesCanvas.scroll(at: local, delta: -event.scrollingDeltaY) { return }
        }
        // Keep native trackpad momentum and gesture-end events, including
        // zero-delta events, so navigation can settle its elastic overscroll.
        let delta = -event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12)
        if !usesSourceShell, allowsModuleInput, let local = navigationPoint(design),
           navigation.scroll(at: local, delta: delta / designScale,
                             phase: event.phase, momentumPhase: event.momentumPhase) { return }
        guard let point = centerPoint(design) else {
            super.scrollWheel(with: event); return
        }
        if let settings = settingsInteraction {
            _ = settings.scroll(at: point, delta: delta / reportScale)
        } else if appShortcutsAreInteractive {
            _ = appShortcutInteraction?.scroll(at: point, delta: delta / reportScale)
        } else if shelfIsInteractive {
            _ = shelfCanvas.scroll(at: point, delta: delta)
        } else if clipboardIsInteractive {
            _ = clipboardCanvas.scroll(at: point, delta: delta)
        } else if eventLogIsInteractive {
            _ = eventLogCanvas.scroll(at: point, delta: delta)
        } else if mapIsInteractive {
            if mapInteraction?.scroll(at: point, event: event) != true { super.scrollWheel(with: event) }
        } else if activityIsInteractive {
            _ = activityCanvas.scroll(at: point, delta: delta / reportScale)
        } else if storageIsInteractive {
            _ = storageCanvas.scroll(at: point, delta: delta)
        } else if volumeIsInteractive {
            _ = volumeCanvas.scroll(at: point, delta: delta)
        } else { super.scrollWheel(with: event) }
    }

    private func notesDropPoint(_ sender: NSDraggingInfo) -> CGPoint? {
        guard notesAreInteractive else { return nil }
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

    private func acceptsShelfDrop(_ sender: NSDraggingInfo) -> Bool {
        if isShelfNavigationDrop(sender) { return true }
        guard shelfIsInteractive, !isDraggingShelfItem,
              let point = centerPoint(designPoint(convert(sender.draggingLocation, from: nil))),
              CGRect(x: 0, y: 0, width: 400, height: 334).contains(point) else { return false }
        return HUDFileShelfInteraction.acceptsFiles(sender.draggingPasteboard)
    }

    private func acceptsAppShortcutDrop(_ sender: NSDraggingInfo) -> Bool {
        guard appShortcutsAreInteractive,
              let point = centerPoint(designPoint(convert(sender.draggingLocation, from: nil))),
              CGRect(x: 0, y: 0, width: 400, height: 334).contains(point) else { return false }
        return HUDAppShortcutInteraction.acceptsApplications(sender.draggingPasteboard)
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation { draggingUpdated(sender) }

    override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation {
        guard sender.draggingSourceOperationMask.contains(.copy) else { endFileDrop(); return [] }
        let overShelf = isShelfNavigationDrop(sender)
        if shelfNavigationDropTarget != overShelf {
            shelfNavigationDropTarget = overShelf
            navigation.hoverTarget(overShelf ? .module(.fileShelf) : nil)
        }
        if overShelf {
            notesInteraction?.endExternalDrag()
            appShortcutInteraction?.endExternalDrag()
            motion.freezeParallax()
            return .copy
        }
        if acceptsAppShortcutDrop(sender) {
            appShortcutInteraction?.beginExternalDrag()
            return .copy
        }
        appShortcutInteraction?.endExternalDrag()
        if acceptsShelfDrop(sender) {
            shelfInteraction?.beginExternalDrag()
            return .copy
        }
        shelfInteraction?.endExternalDrag()
        if notesDropPoint(sender) != nil && HUDNotesInteraction.acceptsImages(sender.draggingPasteboard) {
            notesInteraction?.beginExternalDrag()
            return .copy
        }
        notesInteraction?.endExternalDrag()
        return []
    }

    private func endFileDrop() {
        if shelfNavigationDropTarget { navigation.hoverTarget(nil) }
        shelfNavigationDropTarget = false
        summonedDuringFileDrag = false
        notesInteraction?.endExternalDrag()
        shelfInteraction?.endExternalDrag()
        appShortcutInteraction?.endExternalDrag()
    }

    override func draggingExited(_ sender: NSDraggingInfo?) { endFileDrop() }
    override func draggingEnded(_ sender: NSDraggingInfo) { endFileDrop() }
    override func wantsPeriodicDraggingUpdates() -> Bool { false }

    override func prepareForDragOperation(_ sender: NSDraggingInfo) -> Bool {
        sender.draggingSourceOperationMask.contains(.copy) && (acceptsAppShortcutDrop(sender) || acceptsShelfDrop(sender)
            || (notesDropPoint(sender) != nil && HUDNotesInteraction.acceptsImages(sender.draggingPasteboard)))
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        defer { endFileDrop() }
        if isShelfNavigationDrop(sender) {
            return receiveShelfNavigationDrop(sender.draggingPasteboard,
                at: convert(sender.draggingLocation, from: nil))
        }
        if acceptsAppShortcutDrop(sender) { return appShortcutInteraction?.importPasteboard(sender.draggingPasteboard) ?? false }
        if acceptsShelfDrop(sender) { return shelfInteraction?.importPasteboard(sender.draggingPasteboard) ?? false }
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

    override func acceptsPreviewPanelControl(_ panel: QLPreviewPanel!) -> Bool {
        shelfInteraction?.acceptsPreviewPanelControl() ?? false
    }

    override func beginPreviewPanelControl(_ panel: QLPreviewPanel!) {
        shelfInteraction?.beginPreviewPanelControl(panel)
    }

    override func endPreviewPanelControl(_ panel: QLPreviewPanel!) {
        shelfInteraction?.endPreviewPanelControl(panel)
    }

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
