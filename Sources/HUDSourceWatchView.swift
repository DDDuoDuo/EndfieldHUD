import AppKit
import MetalKit
import QuartzCore
import simd

/// The live source menu. A single display clock owns wrapper clips, button
/// Animator states, gyroscope and shader time; rendering and hits use one frame.
final class HUDSourceWatchView: NSView {
    static let backdropPreparationTimeout: TimeInterval = 3
    struct ButtonAction {
        let source: HUDSourceWatchButton
        let target: HUDNavigationTarget
        var module: HUDModule { target.module ?? .addApp }
        init(source: HUDSourceWatchButton, module: HUDModule) {
            self.source = source; target = .module(module)
        }
        init(source: HUDSourceWatchButton, target: HUDNavigationTarget) {
            self.source = source; self.target = target
        }
    }
    private static let actions: [String: HUDModule] = [
        "CharInfoBtnShadow": .profile, "ActivityBtnShadow": .activityMonitor,
        "GachaBtnShadow": .addApp, "PurchaseBtnNode": .storage,
        "AdventureBookBtnShadow": .notes, "BattlePassShadow": .workMode,
        "DomainBtnShadow": .map, "FriendBtn1Shadow": .profile,
        "EquipBtnShadow": .volume, "CharfomationBtnNode": .addApp,
        "WikiBtnShadow": .about, "ValuablesBtnShadow": .fileShelf,
        "StarShopBtnShadow": .clipboard, "NarrateBtnShadow": .eventLog,
        "BackPackBtnShadow": .fileShelf, "AchievementBtn2Shadow": .activityMonitor,
        "GemEnhanceBtnShadow": .display, "MissionBtnShadow": .notes,
        "MapBtnShadow": .map, "SNSBtnShadow": .addApp,
        "QuestionnaireBtnShadow": .about, "GameToolShadow": .hotkeys,
    ]
    // Paths resolve the actual LuaReference settingBtn/mailBtn/announcementBtn;
    // the numeric TopLeft names are not their visual or sibling order.
    private static let auxiliaryActions: [String: HUDModule] = [
        "TopLeftBtnNode/TopLeftBtn4/btn_4Node/btn": .system,
        "TopLeftBtnNode/TopLeftBtn1/btn_1Node/btn": .notes,
        "TopLeftBtnNode/TopLeftBtn3/btn_3Node/btn": .eventLog,
    ]
    let document: HUDSourceWatchDocument
    let renderer: HUDSourceMetalRenderer
    let cameraModel: HUDSourceWatchCamera
    let frameBuilder: HUDSourceWatchFrameBuilder
    let playback: HUDSourceWatchPlayback
    let buttonAnimation: HUDSourceWatchButtonAnimation
    let selectableColor: HUDSourceSelectableColor
    private var gyro: HUDSourceWatchGyroMotion
    private var animatorButtons: [HUDSourceID: HUDSourceID] = [:]
    private var actionsByID: [HUDSourceID: ButtonAction] = [:]
    private var closeButtonIDs: Set<HUDSourceID> = []
    private var quitButtonIDs: Set<HUDSourceID> = []
    private var desktopProfileLabelIDs: Set<HUDSourceID> = []
    private struct ProfileKey: Equatable {
        let strings: [String]
        let values: [Double]
        let avatar: ObjectIdentifier?
        let background: ObjectIdentifier?
    }
    private var desktopProfileKey: ProfileKey?
    private var desktopProfileImageKeys: [String: ProfileKey] = [:]
    private var desktopProfileBackgroundArtwork: CGImage?
    private var desktopProfileInput: (UserProfile, NSImage?, NSImage?, Int32)?
    var onQuit: (() -> Void)?
    private var lastAcceptedClick: [HUDSourceID: TimeInterval] = [:]
    private var accessibilityButtons: [HUDSourceID: HUDSourceWatchAccessibilityButton] = [:]
    private var desktopScrollButtons: [Int: HUDSourceWatchAccessibilityButton] = [:]
    private var desktopScrollFrames: [Int: CGRect] = [:]
    private var desktopScrollEnabled: [Int: Bool] = [:]
    private var desktopScrollHidden: [Int: Bool] = [:]
    private var observers: [NSObjectProtocol] = []
    private var timer: Timer?
    private let desktopMode: Bool
    private var desktopEntries: [HUDDesktopWatchNavigation.Entry] = []
    private var desktopSupplementalButtons: [HUDSourceWatchButton] = []
    private var desktopRightEntryIndices: [Int] = []
    private var desktopButtons: [HUDSourceWatchButton] { document.buttons + desktopSupplementalButtons }
    private var desktopNavigation: HUDSourceDesktopNavigationLayout?
    private var desktopBindings: [HUDSourceID: Int] = [:]
    private var desktopBindingPosition: Double?
    private var desktopLabels: [HUDSourceID: (container: CALayer, text: CATextLayer, clip: CAShapeLayer)] = [:]
    private var desktopLabelButtons: [HUDSourceID: HUDSourceID] = [:]
    private var desktopIcons: [HUDSourceID: (container: CALayer, content: CALayer, vector: CAShapeLayer, image: CALayer, clip: CAShapeLayer)] = [:]
    private static var desktopReportIconArtwork: CGImage?
    private var desktopIconIDs: [HUDSourceID: HUDSourceID] = [:]
    private lazy var desktopRightButtonIDs = Set(desktopButtons.filter { $0.path.contains("/RightBottomNode/") }.map(\.nodeID))
    private struct DesktopLabelClip {
        let points: [CGPoint]
        let path: CGPath?
    }
    private var desktopButtonClips: [HUDSourceID: DesktopLabelClip] = [:]
    private var desktopViewportClip: DesktopLabelClip?
    private var lastDesktopProjection: (root: simd_double4x4, bounds: CGRect, scroll: Double)?
    private var desktopProjectionWasAnimating = true
    private var lastAccessibilityFrames: [HUDSourceID: CGRect] = [:]
    private var lastAccessibilityEnabled: [HUDSourceID: Bool] = [:]
    private var lastAccessibilityHidden: [HUDSourceID: Bool] = [:]
    private var lastAccessibilityGeometry: (root: simd_double4x4, bounds: CGRect, windowFrame: CGRect, scroll: Double, input: Bool, presentation: UInt64)?
    private var accessibilityGeometryWasAnimating = true
    private struct AccessibilityQueryKey: Equatable {
        let frame: Int
        let bounds: CGRect
        let windowFrame: CGRect?
        let input: Bool
        let visible: Bool
    }
    private var lastAccessibilityQuery: AccessibilityQueryKey?
    private var preparingAccessibilityGeometry = false
    private(set) var accessibilityGeometryUpdateCount = 0
    private var idleTick = false
    private var settledButtonPose: (wrapper: HUDSourceWatchPose, final: HUDSourceWatchPose, reduced: Bool, generation: UInt64)?
    private var settledRenderPacket: (playback: UInt64, buttons: UInt64, presentation: UInt64)?
    private var lastHitQuery: (point: CGPoint?, presentation: UInt64, interactive: Bool)?
    private lazy var ambientRotationIDs = Set(document.animation.ambient.curves
        .filter { $0.group == "m_RotationCurves" }.flatMap(\.nodeIDs))
        .union(desktopMode ? HUDSourceDesktopAmbientMotion.triangleIDs(in: document.scene) : [])
    private var desktopOpeningSequence: UInt64 = 0
    private var desktopScrollMotion = HUDSourceDesktopScrollMotion()
    private var desktopScrollIndicatorState: [Int: Bool] = [:]
    private var pressedScrollDirection: Int?
    private var pressedIndustryLogo = false
    private var logoFlickerStarted: TimeInterval?
    private let logoAccessibility = HUDSourceWatchAccessibilityButton()
    private lazy var logoNodeIDs = document.scene.nodes.filter {
        $0.path.contains("/MiddleDecoNode/") && ["EndfieldText", "EndfieldTextGlow"].contains($0.name)
    }.map(\.id)
    private var logoIsAnimating: Bool { logoFlickerStarted != nil }

    private lazy var desktopScrollIndicatorIDs: [Int: HUDSourceID] = {
        var result: [Int: HUDSourceID] = [:]
        for node in document.scene.nodes where node.path.contains("/RightBottomNode/DecoLine/") {
            if node.path.hasSuffix("/UpLineNode/UpLine") { result[-1] = node.id }
            if node.path.hasSuffix("/BottonLineNode/BottonLine") { result[1] = node.id }
        }
        return result
    }()
    private lazy var desktopHoverFeedback = HUDSourceDesktopHoverFeedback(document: document, selectable: selectableColor)
    private(set) var cumulativeFrameBuildSeconds: TimeInterval = 0
    private(set) var maximumFrameBuildSeconds: TimeInterval = 0
    var onUnhandledKey: ((NSEvent) -> Void)?
    var onPointerMove: (() -> Void)?
    /// Maps the shared native design plane to the same rendered center camera.
    var onDesktopCenterPlane: ((CATransform3D) -> Void)?
    /// Source bottom-card contours in this view's coordinates, after the
    /// center-plane callback has updated the native overlay projection.
    var onDesktopBottomSilhouettes: (([[CGPoint]]) -> Void)?
    var desktopMapOcclusionEnabled = false {
        didSet {
            guard desktopMapOcclusionEnabled != oldValue else { return }
            lastDesktopBottomProjection = nil
            if desktopMapOcclusionEnabled, let frame = renderedFrame, let camera = renderedCamera {
                updateDesktopBottomSilhouettes(frame: frame, camera: camera)
            } else if !desktopMapOcclusionEnabled {
                projectedDesktopBottomSilhouettes = []; desktopBottomHitPaths = []
                onDesktopBottomSilhouettes?([])
            }
        }
    }
    private struct DesktopBottomSilhouette {
        let nodeID: HUDSourceID
        let polygons: [[SIMD2<Double>]]
    }
    private struct DesktopBottomProjectionKey: Equatable {
        let nodeIDs: [HUDSourceID]
        let worlds: [simd_double4x4]
        let rects: [HUDSourceRect]
        let camera: simd_double4x4
        let bounds: CGRect
    }
    private lazy var desktopBottomSilhouettes = makeDesktopBottomSilhouettes()
    private var lastDesktopBottomProjection: DesktopBottomProjectionKey?
    private var projectedDesktopBottomSilhouettes: [[CGPoint]] = []
    private var desktopBottomHitPaths: [CGPath] = []
    /// Original banner plane, expressed in a fixed 528.28 × 122 native canvas.
    var onDesktopStatusPlane: ((CATransform3D) -> Void)?
    var isDesktopPointerLocked: (() -> Bool)?
    private lazy var desktopCenterID = document.scene.nodes.first { $0.name == "MiddleDecoNode" }?.id
    private var desktopPlaneCalibration: (bounds: CGRect, unitsPerPoint: Double)?
    private var lastDesktopCenterWorld: simd_double4x4?
    private var lastDesktopCenterBounds: CGRect?
    private lazy var desktopStatusID = document.scene.nodes.first { $0.name == "BannerNode" && $0.path.contains("/RightBottomNode/") }?.id
    private var lastDesktopStatusProjection: (world: simd_double4x4, rect: HUDSourceRect, bounds: CGRect)?
    var pointerIsAnimatingForVerification: Bool { gyro.isAnimating }
    private var lastReportedPointerPoint: CGPoint?
    var selectedDesktopModule: HUDModule = .power {
        didSet { if selectedDesktopModule != oldValue { updateDesktopSelection(previousModule: oldValue) } }
    }
    private var sourceBackdrop: HUDSourceWatchBackdrop?
    private var backdropAdapter: HUDSourceWatchBackdrop?
    private var backdropTask: Task<Void, Never>?
    private var backdropGeneration: UInt64 = 0
    private struct BackdropGeometry: Equatable {
        let windowNumber: Int
        let rectangle: CGRect
        let drawableSize: CGSize
        let backingScale: CGFloat
        let screenNumber: UInt32
    }
    private var backdropGeometry: BackdropGeometry?
    private struct PendingOpening {
        let heldTime: Double
        let ready: () -> Void
        let completion: () -> Void
    }
    private var pendingOpening: PendingOpening?
    /// Explicit native verification supplies completion timing, never desktop pixels.
    var backdropPreparationForVerification: ((@escaping () -> Void) -> Void)? {
        didSet {
            if let _ = backdropPreparationForVerification {
                precondition(CommandLine.arguments.contains("--ui-test"))
            }
        }
    }
    var isPreparingBackdrop: Bool { pendingOpening != nil }
    var backdropPreparingForVerification: Bool { isPreparingBackdrop }
    var backdropStartForVerification: Double { backdropTransitionStart }
    private var backdropPreparationDeadline: DispatchWorkItem?
    private var backdropTransitionStart: Double = 0
    private(set) var backdropDiagnostics: [String] = []
    /// Native verification and exported previews never read the desktop or
    /// invoke its screen-capture permission APIs.
    private var canCaptureDesktopBackdrop: Bool {
        // The integrated desktop shell retains the native live blur behind
        // Metal. Reusing that surface avoids screen-capture startup latency.
        guard !desktopMode else { return false }
        let process = ProcessInfo.processInfo
        guard process.environment["GITHUB_ACTIONS"] != "true", process.environment["CI"] != "true" else { return false }
        return !CommandLine.arguments.contains { $0 == "--ui-test" || $0.hasSuffix("smoke-test")
            || $0 == "--smoke-test" || $0.hasPrefix("--render-") }
    }
    private var tracking: NSTrackingArea?
    private let cursorBitmap: CGImage
    private let cursorHotspotPixels: CGPoint
    private var sourceCursor: NSCursor?
    private var cursorBackingScale: CGFloat = 0
    private var previousCursor: NSCursor?
    private var cursorRectsActive = false
    var sourceCursorOwnedForVerification: Bool { sourceCursor.map { NSCursor.current === $0 } ?? false }
    private var hovered: HUDSourceID?
    private var pressed: HUDSourceID?
    private var bannerPointerPixels: SIMD2<Float>?
    private var lastPose: HUDSourceWatchPose?
    private var renderedFrame: HUDSourceWatchFrameBuilder.Frame?
    private var renderedCamera: HUDSourceWatchCamera.Frame?
    private var verticalNormalizedPosition: Double = 1
    private let epoch = CACurrentMediaTime()
    private var lastReducedMotion = HUDRuntimeAppearance.reduceMotion
    private var lastAmbientEnabled = HUDRuntimeAppearance.ambientEnabled
    private var lastMotionConfiguration: AppConfiguration?
    private(set) var diagnostics: [String] = []
    private(set) var renderedFrameCount = 0
    var onAction: ((ButtonAction) -> Void)?
    var onClose: (() -> Void)?
    var onBackgroundMouseDown: ((NSEvent) -> Void)?
    var onBackgroundMouseUp: ((NSEvent) -> Void)?
    private var forwardingBackgroundPress = false
    var onFailure: ((String) -> Void)?
    var widgetState: HUDSourceWatchWidgets.State {
        get { frameBuilder.widgetState }
        set { cancelBannerPointer(); frameBuilder.widgetState = newValue; refreshPlaybackScheduling() }
    }
    var inputEnabled = false {
        didSet {
            guard inputEnabled != oldValue else { return }
            if !inputEnabled { cancelBannerPointer(); hovered = nil; pressed = nil; updateAnimatorStates(at: now) }
            refreshSourceCursor()
            refreshPlaybackScheduling()
        }
    }
    /// Screen-point injection is limited to this application's view/fixtures.
    var pointerLocationProvider: () -> CGPoint = { NSEvent.mouseLocation }
    var hasDisplayTimerForVerification: Bool { timer != nil }
    var currentFrameForVerification: HUDSourceWatchFrameBuilder.Frame? { renderedFrame }
    var currentCameraForVerification: HUDSourceWatchCamera.Frame? { renderedCamera }
    /// Exercise the production inverse raycast against projected source
    /// geometry, including original masks; this never synthesizes CA tracks.
    var visibleMainButtonForVerification: HUDSourceID? {
        guard let frame = renderedFrame, let camera = renderedCamera else { return nil }
        for hit in frame.hits where document.buttons.contains(where: { $0.nodeID == hit.buttonID }) {
            let middle = hit.rect.origin + hit.rect.size * 0.5
            let local = SIMD3<Double>(middle.x, middle.y, 0)
            guard let point = camera.camera.project(local, world: hit.world, viewport: bounds)?.point,
                  bounds.contains(point), frame.button(at: point, camera: camera.camera, viewport: bounds) == hit.buttonID else { continue }
            return hit.buttonID
        }
        return nil
    }
    private var now: Double { CACurrentMediaTime() - epoch }
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    private var isOnScreen: Bool {
        guard let window, !isHiddenOrHasHiddenAncestor, !window.isMiniaturized else { return false }
        return window.occlusionState.contains(.visible)
    }
    private var canAdvanceTransition: Bool {
        guard playback.phase == .opening || playback.phase == .closing,
              let window, window.isVisible, !window.isMiniaturized,
              !isHiddenOrHasHiddenAncestor else { return false }
        // WindowServer may publish its first visible occlusion state after the
        // opening callback. A finite transition must still own its clock.
        return true
    }

    init(frame: CGRect, document: HUDSourceWatchDocument? = nil, desktopMode: Bool = false,
         desktopNavigationEntries: [HUDDesktopWatchNavigation.Entry]? = nil) throws {
        var startup = HUDStartupTrace.begin()
        self.desktopMode = desktopMode
        let document = try document ?? (desktopMode ? HUDSourceWatchDocument.desktop() : HUDSourceWatchDocument())
        self.document = document
        HUDStartupTrace.end("source.document", since: &startup)
        renderer = try HUDSourceMetalRenderer(frame: CGRect(origin: .zero, size: frame.size),
            resourceRoot: document.root.deletingLastPathComponent(), recordStartupTimings: HUDStartupTrace.enabled)
        HUDStartupTrace.end("source.renderer", since: &startup)
        if HUDStartupTrace.enabled {
            for phase in renderer.initializationPhaseMilliseconds.keys.sorted() {
                fputs(String(format: "HUD startup renderer.%@ %.2f ms\n", phase,
                    renderer.initializationPhaseMilliseconds[phase]!), stderr)
            }
        }
        if desktopMode, CommandLine.arguments.contains("--ui-test"), CommandLine.arguments.contains("--legacy-source-uniforms") {
            renderer.setPreparedUniformsEnabledForVerification(false)
        }
        if desktopMode, CommandLine.arguments.contains("--ui-test") {
            let merged = CommandLine.arguments.contains("--merge-source-batches")
            let original = CommandLine.arguments.contains("--original-source-batches")
            guard !(merged && original) else { throw HUDSourceError.invalid("Conflicting source batch verification modes") }
            if original { renderer.setAdjacentBatchMergingEnabledForVerification(false) }
            else if merged { renderer.setAdjacentBatchMergingEnabledForVerification(true) }
        }
        cameraModel = try HUDSourceWatchCamera(runtimeRoot: document.runtimeRoot)
        gyro = try HUDSourceWatchGyroMotion(initialRotation: cameraModel.rootRotation)
        guard let cursorURL = HUDResources.url(for: "WatchSource/Cursor/player-default-icon_mouse.png"),
              let image = NSImage(contentsOf: cursorURL),
              let bitmap = image.cgImage(forProposedRect: nil, context: nil, hints: nil),
              let cursorManifestURL = HUDResources.url(for: "WatchSource/Cursor/player-default.json") else {
            throw HUDSourceError.invalid("Missing original Watch cursor resource")
        }
        let cursorManifest = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self,
            from: Data(contentsOf: cursorManifestURL))
        guard bitmap.width == Int(cursorManifest["width"].float()),
              bitmap.height == Int(cursorManifest["height"].float()),
              let hotspot = cursorManifest["typed_prefix_fields"].array.first(where: { $0["name"].string == "cursorHotspot" }),
              let hotspotX = hotspot["value"]["x"].number,
              let hotspotY = hotspot["value"]["y"].number else {
            throw HUDSourceError.invalid("Invalid original Watch cursor manifest")
        }
        cursorBitmap = bitmap
        cursorHotspotPixels = CGPoint(x: hotspotX, y: hotspotY)
        HUDStartupTrace.end("source.cameraCursor", since: &startup)
        frameBuilder = try HUDSourceWatchFrameBuilder(document: document, renderer: renderer, includeDomain: !desktopMode,
            includeSourceText: !desktopMode,
            additionalAmbientRotationIDs: desktopMode ? HUDSourceDesktopAmbientMotion.triangleIDs(in: document.scene) : [])
        HUDStartupTrace.end("source.frameBuilder", since: &startup)
        renderer.configureDesktopAccent(desktopMode ? HUDRuntimeAppearance.accent : nil)
        playback = HUDSourceWatchPlayback(animation: document.animation)
        buttonAnimation = try HUDSourceWatchButtonAnimation(document: document)
        HUDStartupTrace.end("source.buttonControllers", since: &startup)
        selectableColor = try HUDSourceSelectableColor(document: document)
        HUDStartupTrace.end("source.controllers", since: &startup)
        if desktopMode {
            frameBuilder.desktopHiddenNodes = document.desktopHiddenNodeIDs
            frameBuilder.desktopTextOverrides = Dictionary(uniqueKeysWithValues: document.scene.nodes.compactMap { node in
                document.component("UIText", on: node.id) == nil ? nil : (node.id, "")
            })
        }
        super.init(frame: frame)
        if desktopMode {
            for (name, module) in [("TechtreeBtn", HUDModule.storage), ("ReportBtn", .activityMonitor)] {
                guard let node = document.scene.nodes.first(where: { $0.name == name }),
                      let label = document.scene.nodes.first(where: { $0.path.hasPrefix(node.path + "/") && $0.name == "BtnName" }) else { continue }
                desktopSupplementalButtons.append(HUDSourceWatchButton(nodeID: node.id, path: node.path,
                    labels: [.init(nodeID: label.id, textID: "desktop." + module.rawValue, literal: module.title)]))
                // Keep the Report's authored black shadow beneath its matching
                // source glyph; the desktop layer adds only the soft white glow.
                for shadow in document.scene.nodes where shadow.path.hasPrefix(node.path + "/") && ["IconShadow", "ForbidIcon", "LockIcon"].contains(shadow.name) {
                    if module == .activityMonitor && shadow.name == "IconShadow" { continue }
                    frameBuilder.desktopHiddenNodes.insert(shadow.id)
                }
            }
        }
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
        addSubview(renderer)
        setAccessibilityElement(false)
        func buttonAncestor(_ initial: HUDSourceID) -> HUDSourceID? {
            var id: HUDSourceID? = initial
            while let current = id {
                if document.component("UIButton", on: current) != nil { return current }
                id = document.scene.node(current)?.parentID
            }
            return nil
        }
        for source in document.animators {
            animatorButtons[source.rootID] = buttonAncestor(source.rootID)
        }
        for button in desktopButtons {
            guard let node = document.scene.node(button.nodeID),
                  let module = Self.actions[node.name] ?? (desktopMode ? ["TechtreeBtn": HUDModule.storage, "ReportBtn": .activityMonitor][node.name] : nil) else {
                throw HUDSourceError.invalid("Unmapped original Watch main button: \(button.path)")
            }
            actionsByID[button.nodeID] = ButtonAction(source: button, module: module)
            let element = HUDSourceWatchAccessibilityButton()
            element.setAccessibilityRole(.button)
            element.setAccessibilityLabel(button.label?.literal ?? node.name)
            element.setAccessibilityHelp(module.title)
            element.setAccessibilityParent(self)
            element.performPress = { [weak self] in
                guard let self else { return false }
                return self.performClick(on: button.nodeID, at: self.now)
            }
            accessibilityButtons[button.nodeID] = element
            updateAccessibilityVisibility(button.nodeID, visible: false)
        }
        for id in document.scene.traversalIDs {
            guard let node = document.scene.node(id), document.component("UIButton", on: id) != nil else { continue }
            if node.path.hasSuffix("/CloseButtonNode/Btn_BackNode") || node.path.hasSuffix("/FullScreenCloseBtn") {
                closeButtonIDs.insert(id)
            }
            if let entry = Self.auxiliaryActions.first(where: { node.path.hasSuffix("/" + $0.key) }) {
                let source = HUDSourceWatchButton(nodeID: id, path: node.path, labels: [])
                actionsByID[id] = ButtonAction(source: source, module: entry.value)
            }
            if desktopMode && node.name == "QuitBtn" { quitButtonIDs.insert(id) }
            if document.widgets?.profileButtonIDs.contains(id) == true || document.desktopProfileCard?.buttonIDs.contains(id) == true {
                let source = HUDSourceWatchButton(nodeID: id, path: node.path, labels: [])
                actionsByID[id] = ButtonAction(source: source, module: .profile)
            }
            if document.widgets?.bannerButtonIDs.contains(id) == true {
                // A desktop reference news card opens the existing event log.
                // Game JumpOut IDs and account eligibility are not inferred.
                let source = HUDSourceWatchButton(nodeID: id, path: node.path, labels: [])
                actionsByID[id] = ButtonAction(source: source, module: .eventLog)
            }
        }
        if desktopMode {
            let ids = quitButtonIDs.union(document.desktopProfileCard.map { [$0.scene.rootID] } ?? [])
            for id in ids {
                let element = HUDSourceWatchAccessibilityButton()
                element.setAccessibilityRole(.button)
                element.setAccessibilityLabel(quitButtonIDs.contains(id) ? L10n.text("Quit EndfieldHUD", "退出 EndfieldHUD") : HUDModule.profile.title)
                element.setAccessibilityParent(self)
                element.performPress = { [weak self] in
                    guard let self else { return false }
                    return self.accessibilityHitIDs(for: id).contains { self.performClick(on: $0, at: self.now) }
                }
                accessibilityButtons[id] = element
                updateAccessibilityVisibility(id, visible: false)
            }
        }
        setAccessibilityChildren(accessibilityButtons.values.map { $0 })
        if desktopMode {
            setDesktopNavigation(desktopNavigationEntries ?? HUDDesktopWatchNavigation.entries(shortcuts: []))
            // Binding already applies selection/glow styles, and the renderer
            // already owns the current accent. Do not remeasure every caption
            // again when the owner starts the first visible motion clock.
            playback.ambientMotionEnabled = HUDRuntimeAppearance.ambientEnabled
            if HUDRuntimeAppearance.reduceMotion {
                _ = try gyro.retarget(eulerDegrees: .zero, at: now, duration: cameraModel.gyro.duration, reduceMotion: true)
            }
            lastMotionConfiguration = HUDRuntimeAppearance.configuration
            for direction in [-1, 1] {
                let element = HUDSourceWatchAccessibilityButton()
                element.setAccessibilityRole(.button)
                element.setAccessibilityLabel(direction < 0 ? L10n.text("Scroll modules up", "向上滚动模块") : L10n.text("Scroll modules down", "向下滚动模块"))
                element.setAccessibilityParent(self)
                element.performPress = { [weak self] in self?.scrollDesktopNavigation(direction) ?? false }
                element.setAccessibilityHidden(true)
                element.setAccessibilityEnabled(false)
                desktopScrollHidden[direction] = true
                desktopScrollEnabled[direction] = false
                desktopScrollButtons[direction] = element
            }
            logoAccessibility.setAccessibilityRole(.button)
            logoAccessibility.setAccessibilityLabel("ENDFIELD INDUSTRIES")
            logoAccessibility.setAccessibilityParent(self)
            logoAccessibility.performPress = { [weak self] in self?.flickerIndustryLogo() ?? false }
            setAccessibilityChildren(accessibilityButtons.keys.sorted { $0.rawValue < $1.rawValue }.compactMap { accessibilityButtons[$0] }
                + [-1, 1].compactMap { desktopScrollButtons[$0] } + [logoAccessibility])
            for element in Array(accessibilityButtons.values) + Array(desktopScrollButtons.values) + [logoAccessibility] {
                element.prepareGeometry = { [weak self] in self?.prepareCurrentAccessibilityGeometry() }
            }
        }
        if #available(macOS 14.0, *), canCaptureDesktopBackdrop {
            do {
                // Compile the original filter and HDR materials while the view
                // is being created, before a visible opening animation starts.
                backdropAdapter = try HUDSourceWatchBackdrop(renderer: renderer,
                    resourceRoot: document.root.deletingLastPathComponent())
                try renderer.enableSourceRGBHDR()
                try renderer.disableSourceRGBHDR()
            } catch {
                backdropAdapter = nil; try? renderer.disableSourceRGBHDR()
                backdropDiagnostics = [String(describing: error)]
            }
        }
        HUDStartupTrace.end("source.controls", since: &startup)
    }
    required init?(coder: NSCoder) { fatalError("Use the source-resource initializer") }
    deinit {
        backdropTask?.cancel()
        backdropPreparationDeadline?.cancel()
        if let sourceCursor, NSCursor.current === sourceCursor { previousCursor?.set() }
        timer?.invalidate(); observers.forEach { NotificationCenter.default.removeObserver($0) }
    }

    override func layout() {
        super.layout()
        renderer.frame = bounds
        renderer.drawableSize = CGSize(width: bounds.width * (window?.backingScaleFactor ?? 1),
                                       height: bounds.height * (window?.backingScaleFactor ?? 1))
        refreshBackdropGeometry()
        render(at: now)
    }
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        restoreSourceCursor()
        if window == nil { cancelBannerPointer(); pressed = nil }
        observers.forEach { NotificationCenter.default.removeObserver($0) }; observers.removeAll()
        if let window {
            for name in [NSWindow.didChangeOcclusionStateNotification, NSWindow.didMiniaturizeNotification,
                         NSWindow.didDeminiaturizeNotification, NSWindow.willCloseNotification,
                         NSWindow.didResignKeyNotification, NSWindow.didBecomeKeyNotification,
                         NSWindow.didMoveNotification, NSWindow.didResizeNotification,
                         NSWindow.didChangeScreenNotification] {
                observers.append(NotificationCenter.default.addObserver(forName: name, object: window, queue: .main) { [weak self] note in
                    guard let self else { return }
                    if note.name == NSWindow.willCloseNotification { self.conceal() }
                    else {
                        if note.name == NSWindow.didResignKeyNotification || note.name == NSWindow.didMiniaturizeNotification {
                            self.cancelBannerPointer(); self.pressed = nil; self.updateAnimatorStates(at: self.now)
                        }
                        self.refreshBackdropGeometry()
                        self.refreshPlaybackScheduling()
                    }
                })
            }
        } else {
            cancelPendingOpening()
            cancelBackdropCapture(); sourceBackdrop = nil; backdropGeometry = nil
            try? renderer.disableSourceRGBHDR()
            stopTimer()
        }
        refreshBackdropGeometry()
        refreshPlaybackScheduling()
    }
    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties(); needsLayout = true
        refreshBackdropGeometry()
        restoreSourceCursor(); sourceCursor = nil; cursorRectsActive = false
        refreshSourceCursor()
    }
    override func updateTrackingAreas() {
        if let tracking { removeTrackingArea(tracking) }
        let area = NSTrackingArea(rect: bounds, options: [.mouseEnteredAndExited, .mouseMoved, .cursorUpdate, .activeAlways, .inVisibleRect], owner: self)
        tracking = area; addTrackingArea(area); super.updateTrackingAreas()
    }

    private func randomizeDesktopAmbient() {
        guard desktopMode else { return }
        desktopOpeningSequence &+= 1
        let seed = CommandLine.arguments.contains("--ui-test")
            ? 0x454e444649454c44 ^ desktopOpeningSequence : UInt64.random(in: UInt64.min...UInt64.max)
        playback.desktopAmbientMotion = HUDSourceDesktopAmbientMotion(animation: document.animation, seed: seed)
        playback.ambientMotionEnabled = HUDRuntimeAppearance.ambientEnabled
        settledButtonPose = nil; settledRenderPacket = nil
    }

    private func refreshDesktopGlowStyles() {
        guard desktopMode else { return }
        var styles: [HUDSourceID: HUDSourceWatchFrameBuilder.DesktopGraphicStyle] = [:]
        // These separate hover sprites carry the luminous edge. Preserve the
        // authored face/highlight colors and neutral outer shadows exactly.
        for id in desktopHoverFeedback.sideEdgeIDs { styles[id] = .init(opacity: HUDSourceDesktopHoverFeedback.sideEdgeOpacity) }
        for (direction, id) in desktopScrollIndicatorIDs {
            let enabled = desktopScrollIndicatorState[direction] == true
            styles[id] = .init(tint: SIMD3(repeating: enabled ? 1 : 0.32), opacity: enabled ? 1 : 0.65)
        }
        for node in document.scene.nodes where node.path.contains("/MiddleDecoNode/") {
            if node.name == "triangle_fx1" || node.name == "RingFoMesh" {
                styles[node.id] = .init(opacity: 0.88)
            } else if node.name == "EndfieldTextGlow" { styles[node.id] = .init(opacity: 0.78) }
        }
        frameBuilder.desktopGraphicStyles = styles
    }

    private func refreshDesktopHoverStyles(selectableTints: [HUDSourceID: SIMD4<Float>]) {
        guard desktopMode else { return }
        let opacities = desktopHoverFeedback.opacities(selectableTints: selectableTints)
        guard opacities.contains(where: { frameBuilder.desktopGraphicStyles[$0.key] != .init(opacity: $0.value) }) else { return }
        var styles = frameBuilder.desktopGraphicStyles
        for (id, opacity) in opacities { styles[id] = .init(opacity: opacity) }
        frameBuilder.desktopGraphicStyles = styles
    }

    func open(ready: @escaping () -> Void = {}, completion: @escaping () -> Void = {}) {
        isHidden = false
        randomizeDesktopAmbient()
        selectableColor.reset(at: now)
        cancelPendingOpening()
        // A complete Watch close/reopen reconstructs the Banner widget in the
        // game; retain desktop artwork/profile policy while restarting runtime.
        frameBuilder.resetWidgetBannerForPanelCreation()
        backdropTransitionStart = now
        hovered = nil; pressed = nil
        if let preparation = backdropPreparationForVerification {
            precondition(CommandLine.arguments.contains("--ui-test") && !canCaptureDesktopBackdrop)
            cancelBackdropCapture()
            let generation = backdropGeneration
            holdOpeningForBackdrop(ready: ready, completion: completion)
            preparation { [weak self] in
                precondition(Thread.isMainThread)
                guard let self, self.backdropGeneration == generation else { return }
                self.startPendingOpening()
            }
            return
        }
        if #available(macOS 14.0, *), canCaptureDesktopBackdrop,
           HUDSourceDesktopBackdrop.preflightPermission() == .granted {
            // Hold the original initial pose while preparing its real input.
            // Menu and blur start together; capture latency cannot consume the
            // source's short 0.133-second background entrance.
            holdOpeningForBackdrop(ready: ready, completion: completion)
            requestDesktopBackdrop()
            return
        }
        ready()
        buttonAnimation.reset(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion)
        playback.open(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion, completion: completion)
        updateAnimatorStates(at: now)
        refreshPlaybackScheduling()
        requestDesktopBackdrop()
    }
    func showStable() {
        isHidden = false
        if playback.phase == .concealed { randomizeDesktopAmbient() }
        if playback.phase == .concealed { selectableColor.reset(at: now) }
        cancelPendingOpening()
        frameBuilder.resetWidgetBannerClock()
        backdropTransitionStart = now
        playback.showStable(at: now)
        // Completing an entrance suspends the wrapper before selecting its
        // stable pose. Keep its successfully prepared pixels and HDR pipelines
        // when that suspension has not changed the window's capture geometry.
        if sourceBackdrop == nil || backdropGeometry == nil || backdropGeometry != currentBackdropGeometry() {
            requestDesktopBackdrop()
        }
        refreshPlaybackScheduling()
    }
    func close(completion: @escaping () -> Void = {}) {
        pressedScrollDirection = nil
        desktopScrollMotion.reset(to: verticalNormalizedPosition, at: now)
        verticalNormalizedPosition = desktopScrollMotion.position
        cancelPendingOpening()
        cancelBackdropCapture()
        backdropTransitionStart = now
        inputEnabled = false
        playback.close(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion) { [weak self] in
            self?.stopTimer(); try? self?.frameBuilder.deactivateWidgetBanner()
            self?.isHidden = true; self?.refreshSourceCursor(); completion()
        }
        refreshPlaybackScheduling()
    }
    func conceal() {
        logoFlickerStarted = nil; pressedIndustryLogo = false
        pressedScrollDirection = nil
        desktopScrollMotion.reset(to: verticalNormalizedPosition, at: now)
        cancelPendingOpening()
        cancelBackdropCapture()
        try? frameBuilder.deactivateWidgetBanner()
        playback.conceal(); inputEnabled = false; hovered = nil; pressed = nil
        stopTimer(); isHidden = true; refreshSourceCursor()
        try? gyro.stop(at: now)
        renderedFrame = nil; renderedCamera = nil; lastPose = nil
    }
    func suspendForConcealment() {
        logoFlickerStarted = nil; pressedIndustryLogo = false
        cancelPendingOpening()
        cancelBackdropCapture()
        frameBuilder.resetWidgetBannerClock()
        // Cancellation drops wrapper callbacks before input invalidation can
        // schedule a render. Preserve the last drawable until the owner hides
        // the view or chooses a new stable/opening pose.
        playback.conceal(); inputEnabled = false; stopTimer(); try? gyro.stop(at: now)
        refreshSourceCursor()
    }
    func refreshPointerForVerification() { render(at: now) }

    func renderedImageForVerification() throws -> CGImage {
        guard !isHidden, playback.phase != .concealed else {
            throw HUDSourceError.invalid("Cannot capture a concealed source Watch menu")
        }
        render(at: now)
        guard let frame = renderedFrame, !frame.batches.isEmpty, playback.phase != .concealed else {
            throw HUDSourceError.invalid("Source Watch did not produce a renderable frame: \(diagnostics.joined(separator: "; "))")
        }
        renderer.draw()
        let image = try renderer.copyDrawableImage()
        guard renderer.diagnostics.isEmpty else {
            throw HUDSourceError.invalid("Source Watch GPU skipped content: \(renderer.diagnostics.joined(separator: "; "))")
        }
        return image
    }
    func refreshMotionPreferences() {
        let reduce = HUDRuntimeAppearance.reduceMotion
        let ambient = HUDRuntimeAppearance.ambientEnabled
        if desktopMode, lastMotionConfiguration == HUDRuntimeAppearance.configuration,
           reduce == lastReducedMotion, ambient == lastAmbientEnabled {
            // Module switches resume the native pointer bridge through this
            // entry point too. An unchanged preference set must not remeasure
            // every caption, invalidate the settled scene and restart Metal.
            refreshSourceCursor()
            if timer == nil { refreshPlaybackScheduling() }
            return
        }
        lastMotionConfiguration = HUDRuntimeAppearance.configuration
        settledRenderPacket = nil
        if desktopMode {
            updateDesktopSelection()
            if let input = desktopProfileInput { setDesktopProfile(input.0, avatar: input.1, background: input.2, avatarOrientation: input.3) }
        }
        renderer.configureDesktopAccent(desktopMode ? HUDRuntimeAppearance.accent : nil)
        refreshDesktopGlowStyles()
        playback.ambientMotionEnabled = !desktopMode || HUDRuntimeAppearance.ambientEnabled
        if reduce && desktopMode {
            desktopScrollMotion.reset(to: desktopScrollMotion.target, at: now)
            verticalNormalizedPosition = desktopScrollMotion.position
        }
        if desktopMode && (HUDRuntimeAppearance.configuration.lowPowerVisualMode || HUDRuntimeAppearance.configuration.blurAmount == 0) {
            cancelBackdropCapture(); sourceBackdrop = nil; try? renderer.disableSourceRGBHDR()
        }
        if desktopMode || reduce != lastReducedMotion || HUDRuntimeAppearance.ambientEnabled != lastAmbientEnabled {
            lastReducedMotion = reduce; lastAmbientEnabled = HUDRuntimeAppearance.ambientEnabled
            if reduce { _ = try? gyro.retarget(eulerDegrees: .zero, at: now, duration: cameraModel.gyro.duration, reduceMotion: true) }
            refreshPlaybackScheduling()
        }
    }

    /// Language changes update the existing shell in place. User app names,
    /// their UUID bindings, scroll position and the selected module are kept.
    func refreshDesktopLanguage() {
        guard desktopMode else { return }
        desktopEntries = HUDDesktopWatchNavigation.entries(shortcuts: desktopEntries.compactMap(\.shortcut))
        bindDesktopButtons(desktopBindings)
        for id in quitButtonIDs {
            accessibilityButtons[id]?.setAccessibilityLabel(L10n.text("Quit EndfieldHUD", "退出 EndfieldHUD"))
        }
        if let card = document.desktopProfileCard {
            accessibilityButtons[card.scene.rootID]?.setAccessibilityLabel(HUDModule.profile.title)
        }
        for (direction, element) in desktopScrollButtons {
            element.setAccessibilityLabel(direction < 0 ? L10n.text("Scroll modules up", "向上滚动模块")
                : L10n.text("Scroll modules down", "向下滚动模块"))
        }
        if let input = desktopProfileInput {
            setDesktopProfile(input.0, avatar: input.1, background: input.2, avatarOrientation: input.3)
        }
        refreshPlaybackScheduling()
    }

    private func refreshPlaybackScheduling() {
        stopTimer()
        refreshSourceCursor()
        guard !isHidden, playback.phase != .concealed else { return }
        render(at: now)
        guard pendingOpening == nil else { return }
        guard isOnScreen || canAdvanceTransition else { return }
        let finite = playback.phase == .opening || playback.phase == .closing || gyro.isAnimating || buttonAnimation.requiresFrames(at: now)
            || frameBuilder.requiresWidgetFrames || selectableColor.requiresFrames(at: now) || desktopScrollMotion.requiresFrames || logoIsAnimating
        guard !HUDRuntimeAppearance.reduceMotion && (finite || HUDRuntimeAppearance.ambientEnabled) else { return }
        // The low-power setting must reduce CPU-side scene preparation too,
        // not only suppress blur and ambient shaders. Normal mode retains its
        // existing 60 Hz motion / 30 Hz ambient sampling.
        let interval = desktopMode && HUDRuntimeAppearance.configuration.lowPowerVisualMode ? 1.0 / 30 : 1.0 / 60
        let timer = Timer(timeInterval: interval, repeats: true) { [weak self] _ in
            guard let self else { return }
            guard self.isOnScreen || self.canAdvanceTransition, self.playback.phase != .concealed else { self.stopTimer(); return }
            let time = self.now
            let finite = self.playback.phase != .visible || self.gyro.isAnimating
                || self.buttonAnimation.requiresFrames(at: time) || self.selectableColor.requiresFrames(at: time)
                || self.frameBuilder.requiresWidgetFrames || self.desktopScrollMotion.requiresFrames || self.logoIsAnimating
            self.idleTick.toggle()
            if finite || self.idleTick { self.render(at: time) }
            if !HUDRuntimeAppearance.ambientEnabled && self.playback.phase == .visible && !self.gyro.isAnimating
                && !self.buttonAnimation.requiresFrames(at: self.now) && !self.frameBuilder.requiresWidgetFrames
                && !self.selectableColor.requiresFrames(at: self.now) && !self.desktopScrollMotion.requiresFrames && !self.logoIsAnimating {
                self.stopTimer()
            }
        }
        timer.tolerance = 0.002
        RunLoop.main.add(timer, forMode: .common); self.timer = timer
    }
    private func stopTimer() { timer?.invalidate(); timer = nil }

    private func cancelBackdropCapture() {
        backdropGeneration &+= 1
        backdropTask?.cancel(); backdropTask = nil
    }

    private func cancelPendingOpening() {
        backdropPreparationDeadline?.cancel(); backdropPreparationDeadline = nil
        pendingOpening = nil
    }

    private func holdOpeningForBackdrop(ready: @escaping () -> Void, completion: @escaping () -> Void) {
        pendingOpening = PendingOpening(heldTime: now, ready: ready, completion: completion)
        armBackdropPreparationDeadline()
        playback.open(at: now, reduceMotion: false)
        refreshPlaybackScheduling()
    }

    private func armBackdropPreparationDeadline() {
        backdropPreparationDeadline?.cancel()
        let deadline = DispatchWorkItem { [weak self] in
            guard let self, self.pendingOpening != nil,
                  !self.isHidden, self.playback.phase == .opening else { return }
            // ScreenCaptureKit cancellation need not abort its system request.
            // Invalidate its generation before starting the bounded fallback.
            self.cancelBackdropCapture()
            self.sourceBackdrop = nil
            self.backdropDiagnostics = ["Desktop backdrop preparation timed out; using the system backdrop"]
            try? self.renderer.disableSourceRGBHDR()
            self.startPendingOpening()
        }
        backdropPreparationDeadline = deadline
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.backdropPreparationTimeout, execute: deadline)
    }

    private func currentBackdropGeometry() -> BackdropGeometry? {
        guard let window, bounds.width > 0, bounds.height > 0,
              renderer.drawableSize.width > 0, renderer.drawableSize.height > 0 else { return nil }
        let screenNumber = (window.screen?.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value ?? 0
        return BackdropGeometry(windowNumber: window.windowNumber,
            rectangle: window.convertToScreen(convert(bounds, to: nil)),
            drawableSize: renderer.drawableSize, backingScale: window.backingScaleFactor,
            screenNumber: screenNumber)
    }

    private func refreshBackdropGeometry() {
        guard canCaptureDesktopBackdrop, !isHidden, playback.phase != .concealed,
              let geometry = currentBackdropGeometry(), geometry != backdropGeometry else { return }
        // An old display crop must never stretch across a moved/resized HUD.
        if playback.phase == .closing {
            cancelBackdropCapture(); sourceBackdrop = nil; backdropGeometry = geometry
            try? renderer.disableSourceRGBHDR()
        } else { requestDesktopBackdrop() }
    }

    private func requestDesktopBackdrop() {
        cancelBackdropCapture()
        defer { if backdropTask == nil { startPendingOpening() } }
        guard canCaptureDesktopBackdrop,
              !desktopMode || (!HUDRuntimeAppearance.configuration.lowPowerVisualMode && HUDRuntimeAppearance.configuration.blurAmount > 0) else { return }
        sourceBackdrop = nil
        backdropGeometry = nil
        do { try renderer.disableSourceRGBHDR() }
        catch { backdropDiagnostics = [String(describing: error)]; return }
        guard #available(macOS 14.0, *), let window,
              let geometry = currentBackdropGeometry(),
              HUDSourceDesktopBackdrop.preflightPermission() == .granted else { return }
        backdropGeometry = geometry
        let generation = backdropGeneration
        let rectangle = geometry.rectangle
        backdropTask = Task { @MainActor [weak self, weak window] in
            defer {
                if let self, self.backdropGeneration == generation {
                    self.backdropTask = nil
                    self.startPendingOpening()
                }
            }
            do {
                // Allow the just-opened HUD to enter WindowServer's inventory.
                // This delay owns no display loop and cancellation ends it.
                try await Task.sleep(nanoseconds: 33_333_333)
                guard let window else { return }
                let captured = try await HUDSourceDesktopBackdrop().captureBelowHUD(
                    window: window, appKitGlobalRect: rectangle)
                try Task.checkCancellation()
                guard let self, self.backdropGeneration == generation,
                      !self.isHidden, self.playback.phase != .concealed && self.playback.phase != .closing,
                      self.window === window,
                      self.currentBackdropGeometry() == geometry else { return }
                let backdrop: HUDSourceWatchBackdrop
                if let preparedAdapter = self.backdropAdapter { backdrop = preparedAdapter }
                else {
                    backdrop = try HUDSourceWatchBackdrop(renderer: self.renderer,
                        resourceRoot: self.document.root.deletingLastPathComponent())
                    self.backdropAdapter = backdrop
                }
                try backdrop.prepare(frame: captured, drawableSize: geometry.drawableSize)
                self.sourceBackdrop = backdrop; self.backdropDiagnostics = []
                self.render(at: self.now)
            } catch is CancellationError {
                // Concealment invalidates the capture without touching a newer task.
            } catch {
                guard let self, self.backdropGeneration == generation else { return }
                self.backdropDiagnostics = [String(describing: error)]
                NSLog("Source Watch desktop backdrop: %@", String(describing: error))
            }
        }
    }

    private func startPendingOpening() {
        guard let pending = pendingOpening else { return }
        cancelPendingOpening()
        guard !isHidden, playback.phase == .opening else { return }
        frameBuilder.resetWidgetBannerClock()
        backdropTransitionStart = now
        pending.ready()
        buttonAnimation.reset(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion)
        playback.open(at: now, reduceMotion: HUDRuntimeAppearance.reduceMotion, completion: pending.completion)
        updateAnimatorStates(at: now)
        refreshPlaybackScheduling()
    }

    private func backdropAlpha(at time: Double, reduceMotion: Bool) -> Float {
        guard !reduceMotion else { return playback.phase == .concealed ? 0 : 1 }
        let elapsed = max(0, time - backdropTransitionStart)
        switch playback.phase {
        case .opening: return Float(document.blurAnimation.entrance.alpha(at: elapsed) ?? document.blurAnimation.entrance.endAlpha)
        case .closing: return Float(document.blurAnimation.exit.alpha(at: elapsed) ?? document.blurAnimation.exit.endAlpha)
        case .visible: return 1
        case .concealed: return 0
        }
    }

    private func render(at requestedTime: Double) {
        let started = CACurrentMediaTime()
        defer {
            let duration = CACurrentMediaTime() - started
            cumulativeFrameBuildSeconds += duration
            maximumFrameBuildSeconds = max(maximumFrameBuildSeconds, duration)
        }
        guard bounds.width > 0, bounds.height > 0, !isHidden, playback.phase != .concealed else { return }
        let time = pendingOpening?.heldTime ?? requestedTime
        if desktopMode && playback.phase == .visible { verticalNormalizedPosition = desktopScrollMotion.advance(at: time) }
        do {
            let screen = SIMD2<Double>(Double(bounds.width), Double(bounds.height))
            let reduce = pendingOpening == nil && HUDRuntimeAppearance.reduceMotion
            let pointerPoint = window.map { window in
                convert(window.convertPoint(fromScreen: pointerLocationProvider()), from: nil)
            }
            if desktopMode, let pointerPoint, pointerPoint != lastReportedPointerPoint {
                lastReportedPointerPoint = pointerPoint
                onPointerMove?()
            }
            if desktopMode, isDesktopPointerLocked?() == true { try gyro.stop(at: time) }
            // Match the finite-transition clock while WindowServer's first
            // occlusion update is pending, so entrance/exit tilt follows input.
            if !reduce, (isOnScreen || canAdvanceTransition), window != nil, isDesktopPointerLocked?() != true {
                guard let p = pointerPoint else { throw HUDSourceError.invalid("Source Watch pointer conversion failed") }
                var euler = try cameraModel.gyro.targetEuler(mouseUnity: SIMD2(Double(p.x), Double(bounds.height - p.y)), screenSize: screen)
                if desktopMode {
                    let c = HUDRuntimeAppearance.configuration
                    euler *= 1.25 * max(0, min(2, c.parallaxIntensity)) * max(0, min(2, c.perspectiveIntensity))
                }
                _ = try gyro.retarget(eulerDegrees: euler, at: time, duration: cameraModel.gyro.duration)
            }
            gyro.finishIfNeeded(at: time)
            var camera = try cameraModel.frame(screenSize: screen, localRotation: gyro.rotation(at: time))
            if desktopMode {
                let c = HUDRuntimeAppearance.configuration
                let translation = HUDSourceGeometry.translation(SIMD3<Double>(camera.layout.canvasSize.x * c.hudOffsetX,
                    -camera.layout.canvasSize.y * c.hudOffsetY, 0))
                camera = HUDSourceWatchCamera.Frame(camera: camera.camera,
                    worldRoot: camera.worldRoot * translation * HUDSourceGeometry.scale(SIMD3<Double>(repeating: c.hudScale)),
                    layout: camera.layout)
                updateDesktopBindings(at: time)
                refreshDesktopScrollIndicators()
            }
            let tints = selectableColor.colors(at: time, reduceMotion: reduce)
            refreshDesktopHoverStyles(selectableTints: tints)
            refreshIndustryLogo(at: time)
            let settled = desktopMode && playback.phase == .visible
                && !buttonAnimation.requiresFrames(at: time) && !selectableColor.requiresFrames(at: time)
            let usesAmbient = !reduce && HUDRuntimeAppearance.ambientEnabled
            let ambient = usesAmbient ? playback.sampleAmbient(at: time) : nil
            var wrapperPose: HUDSourceWatchPose?
            var finalPose: HUDSourceWatchPose?
            var frame: HUDSourceWatchFrameBuilder.Frame
            if settled, let packet = settledRenderPacket,
               packet.playback == playback.generation, packet.buttons == buttonAnimation.stateGeneration,
               (!usesAmbient || ambient != nil),
               let current = try frameBuilder.buildSettledMotion(ambient, expectedRevision: packet.presentation,
                   worldRoot: camera.worldRoot, canvasResolution: camera.layout.canvasSize,
                   verticalNormalizedPosition: verticalNormalizedPosition, desktopNavigation: desktopNavigation, selectableTints: tints) {
                frame = current
            } else {
                guard var pose = try playback.sample(at: time, canvasResolution: camera.layout.canvasSize, reduceMotion: reduce) else {
                    stopTimer(); return
                }
                // Low-power/ambient-off freezes wrapper-loop and shader clocks.
                if !HUDRuntimeAppearance.ambientEnabled, playback.phase == .visible {
                    pose = try document.animation.pose(entranceTime: document.animation.entrance.lastKeyTime,
                        ambientTime: nil, exitTime: nil, canvasResolution: camera.layout.canvasSize)
                }
                wrapperPose = pose
                applyDesktopButtons(to: &pose, at: time, reduceMotion: reduce)
                frame = try frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                    verticalNormalizedPosition: verticalNormalizedPosition, desktopNavigation: desktopNavigation,
                    domainAnimationState: .init(ambientTime: reduce || !HUDRuntimeAppearance.ambientEnabled ? 0 : time),
                    widgetTime: time, selectableTints: tints)
                finalPose = pose
            }
            // Stationary pointers still raycast during gyro/wrapper/button
            // motion. A sparse ambient frame has identical hit geometry.
            let interactive = inputEnabled && playback.phase == .visible && isOnScreen
            let nextHover: HUDSourceID?
            if let previous = lastHitQuery, previous.point == pointerPoint,
               previous.presentation == frameBuilder.presentationRevision, previous.interactive == interactive {
                nextHover = hovered
            } else if interactive, let pointerPoint, bounds.contains(pointerPoint) {
                nextHover = mapFilteredButton(frame.button(at: pointerPoint, camera: camera.camera, viewport: bounds), at: pointerPoint)
            } else { nextHover = nil }
            if nextHover != hovered {
                hovered = nextHover; updateAnimatorStates(at: time)
                let tints = selectableColor.colors(at: time, reduceMotion: reduce)
                refreshDesktopHoverStyles(selectableTints: tints)
                guard var pose = try wrapperPose ?? playback.sample(at: time, canvasResolution: camera.layout.canvasSize, reduceMotion: reduce) else {
                    stopTimer(); return
                }
                applyDesktopButtons(to: &pose, at: time, reduceMotion: reduce)
                frame = try frameBuilder.build(pose: pose, worldRoot: camera.worldRoot,
                    verticalNormalizedPosition: verticalNormalizedPosition, desktopNavigation: desktopNavigation,
                    domainAnimationState: .init(ambientTime: reduce || !HUDRuntimeAppearance.ambientEnabled ? 0 : time),
                    widgetTime: time, selectableTints: tints)
                finalPose = pose
            }
            lastHitQuery = (pointerPoint, frameBuilder.presentationRevision, interactive)
            if settled && !buttonAnimation.requiresFrames(at: time) && !selectableColor.requiresFrames(at: time) {
                settledRenderPacket = (playback.generation, buttonAnimation.stateGeneration, frameBuilder.presentationRevision)
            } else { settledRenderPacket = nil }
            if let finalPose { lastPose = finalPose }
            renderedFrame = frame; renderedCamera = camera
            diagnostics = frame.diagnostics
            let position = cameraModel.cameraWorld.columns.3
            // Retain the converted Vulkan program's final Y negation and
            // compensate once in the GPU matrix. CPU render/hit stays +Y up.
            var gpuProjection = HUDSourceGeometry.floatMatrix(camera.camera.projection)
            var gpuVP = HUDSourceGeometry.floatMatrix(camera.camera.viewProjection)
            for column in 0..<4 { gpuProjection[column].y = -gpuProjection[column].y; gpuVP[column].y = -gpuVP[column].y }
            var batches = frame.batches
            let blurAmount = desktopMode ? Float(HUDRuntimeAppearance.configuration.blurAmount) : 1
            if let background = sourceBackdrop?.batch(alpha: backdropAlpha(at: time, reduceMotion: reduce) * blurAmount) {
                // Source WatchBlur's UI3D category sorts before Watch's Window
                // category; its screen-space batch uses its own projection.
                batches.insert(background, at: 0)
            }
            renderer.submit(camera: HUDSourceMetalRenderer.Camera(
                viewProjection: gpuVP,
                viewNoTranslationProjection: try HUDSourceWatchCamera.viewNoTranslationProjection(
                    gpuProjection: gpuProjection, view: camera.camera.view),
                worldSpacePosition: SIMD3(Float(position.x), Float(position.y), Float(position.z)),
                timeSeconds: reduce || !HUDRuntimeAppearance.ambientEnabled ? 0 : Float(time),
                // The original freshly constructed offscreen UI color pass
                // has a zeroed payload and writes this camera-relative tuple.
                renderPathInjected: 1, flipX: 0, flipY: 0,
                projection: gpuProjection, inverseView: HUDSourceGeometry.floatMatrix(cameraModel.shaderCameraToWorld),
                uiProjectionParameters: try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: gpuProjection,
                    near: Float(cameraModel.near), far: Float(cameraModel.far))),
                batches: batches, structureToken: frame.batchStructureToken,
                dynamicPrefixCount: batches.count - frame.batches.count)
            renderedFrameCount += 1
            if desktopMode {
                updateDesktopLabels(frame: frame, camera: camera)
                try updateDesktopCenterPlane(frame: frame, camera: camera)
                updateDesktopBottomSilhouettes(frame: frame, camera: camera)
                updateDesktopStatusPlane(frame: frame, camera: camera)
            }
            // Native AX clients request exact current geometry on demand.
            // Ordinary pointer frames do not need to project and polygon-clip
            // every accessibility element when no client is reading it.
            if !desktopMode { updateAccessibility(frame: frame, camera: camera.camera) }
        } catch {
            diagnostics = [String(describing: error)]
            stopTimer(); playback.conceal(); inputEnabled = false
            onFailure?(String(describing: error))
        }
    }

    private func updateAnimatorStates(at time: Double) {
        settledButtonPose = nil
        settledRenderPacket = nil
        let reduce = HUDRuntimeAppearance.reduceMotion
        let tintHovered = desktopMode ? desktopHoverFeedback.groupedButton(hovered) : hovered
        let tintPressed = desktopMode ? desktopHoverFeedback.groupedButton(pressed) : pressed
        for binding in selectableColor.bindings {
            let desired: HUDSourceSelectableColor.State = !binding.sourceInteractable ? .disabled
                : (binding.buttonNodeID == tintPressed ? .pressed : (binding.buttonNodeID == tintHovered ? .highlighted : .normal))
            if selectableColor.state(on: binding.buttonNodeID) != desired {
                selectableColor.setState(desired, on: binding.buttonNodeID, at: time, reduceMotion: reduce)
            }
        }
        for (root, button) in animatorButtons {
            if buttonAnimation.state(on: root) == .disabled { continue }
            let desired: HUDSourceWatchButtonAnimation.State = button == pressed ? .pressed : (button == hovered ? .highlighted : .normal)
            buttonAnimation.setState(desired, on: root, at: time, reduceMotion: reduce)
            buttonAnimation.setHovered(button == hovered, on: root, at: time, reduceMotion: reduce)
        }
    }

    /// Settled controllers contribute hundreds of constant channels. Preserve
    /// their exact sampled result while only the wrapper's decorative rotations
    /// advance; every input/state/layout change takes the authoritative path.
    func applyDesktopButtons(to pose: inout HUDSourceWatchPose, at time: Double, reduceMotion: Bool, forceRebuild: Bool = false) {
        if forceRebuild {
            buttonAnimation.apply(to: &pose, at: time, reduceMotion: reduceMotion)
            document.applyMacButtonAvailability(to: &pose)
            return
        }
        let settled = !forceRebuild && desktopMode && playback.phase == .visible && !buttonAnimation.requiresFrames(at: time)
        var signature = pose
        if settled {
            for id in ambientRotationIDs { signature.transforms[id]?.localRotation = nil }
            if let cached = settledButtonPose, cached.reduced == reduceMotion,
               cached.generation == buttonAnimation.stateGeneration,
               cached.wrapper.transforms == signature.transforms, cached.wrapper.properties == signature.properties,
               cached.wrapper.unboundPaths == signature.unboundPaths,
               cached.wrapper.unregisteredBindings == signature.unregisteredBindings {
                var final = cached.final
                for id in ambientRotationIDs { final.transforms[id]?.localRotation = pose.transforms[id]?.localRotation }
                pose = final
                return
            }
        }
        buttonAnimation.apply(to: &pose, at: time, reduceMotion: reduceMotion)
        document.applyMacButtonAvailability(to: &pose)
        if settled { settledButtonPose = (signature, pose, reduceMotion, buttonAnimation.stateGeneration) }
        else { settledButtonPose = nil }
    }

    private func point(_ event: NSEvent) -> CGPoint { convert(event.locationInWindow, from: nil) }
    private func bannerScreenPixels(_ point: CGPoint) -> SIMD2<Float> {
        let scale = window?.backingScaleFactor ?? 1
        return SIMD2(Float(point.x * scale), Float(point.y * scale))
    }
    private func bannerViewportPoint(_ point: CGPoint, requireHit: Bool) -> (local: SIMD2<Double>, panelWidth: Float)? {
        guard inputEnabled, playback.phase == .visible, isOnScreen,
              let widgets = document.widgets, let frame = renderedFrame, let camera = renderedCamera,
              let viewport = frame.node(widgets.bannerListNodeID), viewport.activeInHierarchy,
              let rect = viewport.rect, let panelRect = frame.node(document.scene.rootID)?.rect else { return nil }
        let world = simd_mul(camera.worldRoot, viewport.worldMatrix)
        let local = requireHit ? camera.camera.hit(point, world: world, rect: rect, viewport: bounds)
            : camera.camera.pointOnPlane(point, world: world, viewport: bounds)
        guard let local else { return nil }
        // UIStep's cumulative-distance gate uses its owning LuaPanel width,
        // distinct from the narrow ScrollRect viewport width used by rubber.
        return (local, Float(panelRect.size.x))
    }
    private func cancelBannerPointer() {
        bannerPointerPixels = nil; frameBuilder.cancelWidgetBannerPointer()
    }
    /// Cursor ownership follows the visible HUD, not whether its controls have
    /// enabled yet. A nonactivating panel can be key while NSApp is inactive.
    var presentedSourceCursor: NSCursor? {
        guard playback.phase != .concealed, !isHiddenOrHasHiddenAncestor,
              let window, window.isVisible, !window.isMiniaturized,
              window.isKeyWindow, !window.ignoresMouseEvents else { return nil }
        let scale = max(window.backingScaleFactor, 1)
        if sourceCursor == nil || cursorBackingScale != scale {
            let image = NSImage(cgImage: cursorBitmap,
                size: NSSize(width: CGFloat(cursorBitmap.width) / scale,
                             height: CGFloat(cursorBitmap.height) / scale))
            sourceCursor = NSCursor(image: image,
                hotSpot: NSPoint(x: cursorHotspotPixels.x / scale, y: cursorHotspotPixels.y / scale))
            cursorBackingScale = scale
        }
        return sourceCursor
    }
    override func resetCursorRects() {
        super.resetCursorRects()
        if let cursor = presentedSourceCursor { addCursorRect(visibleRect, cursor: cursor) }
    }
    func refreshSourceCursor(force: Bool = false) {
        let cursor = presentedSourceCursor
        if let cursor, previousCursor == nil {
            // A registered cursor rect may have installed it before this event.
            // Never record our own cursor as the value to restore on exit.
            previousCursor = NSCursor.current === cursor ? .arrow : NSCursor.current
        }
        let active = cursor != nil
        if cursorRectsActive != active {
            cursorRectsActive = active
            window?.invalidateCursorRects(for: self)
            if desktopMode, let host = superview { window?.invalidateCursorRects(for: host) }
        }
        guard let cursor, let window else { restoreSourceCursor(); return }
        let p = convert(window.convertPoint(fromScreen: pointerLocationProvider()), from: nil)
        guard visibleRect.contains(p) else { restoreSourceCursor(); return }
        // Native editors retain their text-selection cursor. This event-driven
        // check is not polled by the source render clock.
        let host = desktopMode ? (superview ?? self) : self
        var hit = host.hitTest(convert(p, to: host.superview))
        while let view = hit, view !== host {
            if view is NSTextView || (view as? NSTextField)?.isEditable == true {
                restoreSourceCursor(); return
            }
            hit = view.superview
        }
        // AppKit's cursor-region dispatch can replace the displayed cursor
        // after a view handler, even while NSCursor.current still refers to it.
        // Only the window's post-event/reset path forces reapplication; idle
        // rendering never polls or reuploads cursor images.
        if force || NSCursor.current !== cursor { cursor.set() }
    }
    private func restoreSourceCursor() {
        guard let previousCursor else { return }
        // Never overwrite a cursor already chosen by an editor, drag or another
        // application after this panel loses ownership.
        if let sourceCursor, NSCursor.current === sourceCursor { previousCursor.set() }
        self.previousCursor = nil
    }
    private func button(at point: CGPoint) -> HUDSourceID? {
        guard inputEnabled, playback.phase == .visible, let frame = renderedFrame, let camera = renderedCamera else { return nil }
        return mapFilteredButton(frame.button(at: point, camera: camera.camera, viewport: bounds), at: point)
    }
    private func mapFilteredButton(_ id: HUDSourceID?, at point: CGPoint) -> HUDSourceID? {
        guard desktopMapOcclusionEnabled, let id, let target = actionsByID[id]?.target,
              target == .module(.storage) || target == .module(.activityMonitor) else { return id }
        // The map remains visible in the transparent parts of these source
        // quads. Navigation and native map handlers must share its cutout edge.
        return desktopBottomHitPaths.contains { $0.contains(point) } ? id : nil
    }
    func navigationTarget(at point: CGPoint) -> HUDNavigationTarget? {
        button(at: point).flatMap { actionsByID[$0]?.target }
    }
    var desktopNavigationTargets: [HUDNavigationTarget] { desktopEntries.map(\.target) }
    private func refreshDesktopScrollIndicators() {
        let scrollable = (renderedFrame?.layoutReport.scroll?.hiddenLength ?? 0) > 0
        let up = scrollable && desktopScrollMotion.canScroll(-1), down = scrollable && desktopScrollMotion.canScroll(1)
        guard desktopScrollIndicatorState[-1] != up || desktopScrollIndicatorState[1] != down else { return }
        desktopScrollIndicatorState = [-1: up, 1: down]; refreshDesktopGlowStyles()
    }
    private func desktopScrollDirection(at point: CGPoint) -> Int? {
        guard desktopMode, inputEnabled, playback.phase == .visible,
              let frame = renderedFrame, let camera = renderedCamera,
              (frame.layoutReport.scroll?.hiddenLength ?? 0) > 0 else { return nil }
        return desktopScrollIndicatorIDs.first { _, id in
            guard let node = frame.node(id), node.activeInHierarchy, let rect = node.rect else { return false }
            return camera.camera.hit(point, world: camera.worldRoot * node.worldMatrix, rect: rect, viewport: bounds) != nil
        }?.key
    }
    private func sourceScrollUnitsPerPoint(node: HUDSourceResolvedNode, rect: HUDSourceRect,
                                           camera: HUDSourceWatchCamera.Frame) -> Double {
        let world = camera.worldRoot * node.worldMatrix
        let points = [SIMD3(rect.origin.x, rect.origin.y, 0), SIMD3(rect.origin.x, rect.origin.y + rect.size.y, 0)]
            .compactMap { camera.camera.project($0, world: world, viewport: bounds)?.point }
        guard points.count == 2 else { return 1 }
        return rect.size.y / max(1, hypot(Double(points[1].x - points[0].x), Double(points[1].y - points[0].y)))
    }
    @discardableResult func scrollDesktopNavigation(_ direction: Int, animated: Bool = false) -> Bool {
        guard desktopMode, inputEnabled, playback.phase == .visible,
              let frame = renderedFrame, let camera = renderedCamera, let scroll = frame.layoutReport.scroll,
              scroll.hiddenLength > 0, let node = frame.node(scroll.viewportID), let rect = node.rect,
              desktopScrollMotion.canScroll(direction) else { return false }
        let delta = -Double(direction) * 32 * sourceScrollUnitsPerPoint(node: node, rect: rect, camera: camera) / max(1, scroll.hiddenLength)
        desktopScrollMotion.scroll(by: delta, hiddenLength: scroll.hiddenLength, at: now,
            reduceMotion: !animated || HUDRuntimeAppearance.reduceMotion)
        verticalNormalizedPosition = desktopScrollMotion.position
        refreshDesktopScrollIndicators(); refreshPlaybackScheduling()
        return true
    }
    func desktopPointForVerification(target: HUDNavigationTarget) -> CGPoint? {
        guard let frame = renderedFrame, let camera = renderedCamera else { return nil }
        for hit in frame.hits where actionsByID[hit.buttonID]?.target == target {
            let p = hit.rect.origin + hit.rect.size * 0.5
            if let point = camera.camera.project(SIMD3(p.x, p.y, 0), world: hit.world, viewport: bounds)?.point,
               navigationTarget(at: point) == target { return point }
        }
        return nil
    }
    var desktopNavigationForVerification: [HUDDesktopWatchNavigation.Entry] { desktopEntries }
    var desktopAccessibilityLabelsForVerification: [String] {
        (Array(accessibilityButtons.values) + Array(desktopScrollButtons.values)).compactMap { $0.accessibilityLabel() }
    }
    var desktopAccessibilityAvailabilityForVerification: [(hidden: Bool, enabled: Bool)] {
        precondition(CommandLine.arguments.contains("--ui-test"))
        return (Array(accessibilityButtons.values) + Array(desktopScrollButtons.values) + [logoAccessibility]).map {
            ($0.isAccessibilityHidden(), $0.isAccessibilityEnabled())
        }
    }
    var desktopProfileCaptionsForVerification: [String] {
        desktopProfileLabelIDs.compactMap { desktopLabels[$0]?.text.string as? String }
    }
    func desktopPresentationForVerification(target: HUDNavigationTarget)
        -> (caption: String, captionVisible: Bool, wrapped: Bool, fontSize: CGFloat, captionSize: CGSize,
            image: CGImage?, vectorVisible: Bool)? {
        guard let button = desktopButtons.first(where: { actionsByID[$0.nodeID]?.target == target }),
              let label = button.label, let text = desktopLabels[label.nodeID],
              let iconID = desktopIconIDs[button.nodeID], let icon = desktopIcons[iconID] else { return nil }
        let image: CGImage? = icon.image.contents.flatMap { contents in
            guard CFGetTypeID(contents as CFTypeRef) == CGImage.typeID else { return nil }
            return (contents as! CGImage)
        }
        return (text.text.string as? String ?? "",
                renderedFrame?.node(label.nodeID)?.activeInHierarchy == true && !text.container.isHidden && text.container.opacity > 0,
                text.text.isWrapped, text.text.fontSize, text.text.bounds.size, image, !icon.vector.isHidden)
    }
    var desktopProfilePointForVerification: CGPoint? {
        guard let card = document.desktopProfileCard else { return nil }
        return projectedPointForVerification(buttonIDs: card.buttonIDs)
    }
    var desktopQuitPointForVerification: CGPoint? {
        projectedPointForVerification(buttonIDs: quitButtonIDs)
    }
    private func projectedPointForVerification(buttonIDs: Set<HUDSourceID>) -> CGPoint? {
        guard let frame = renderedFrame, let camera = renderedCamera else { return nil }
        for hit in frame.hits where buttonIDs.contains(hit.buttonID) {
            let p = hit.rect.origin + hit.rect.size * 0.5
            guard let point = camera.camera.project(SIMD3(p.x, p.y, 0), world: hit.world, viewport: bounds)?.point,
                  let target = frame.button(at: point, camera: camera.camera, viewport: bounds),
                  buttonIDs.contains(target) else { continue }
            return point
        }
        return nil
    }
    /// Both mouse release and AX activation pass the actual resolved source
    /// raycast/masks and the original UIButton per-instance click cooldown.
    @discardableResult private func performClick(on id: HUDSourceID, at time: TimeInterval, point: CGPoint? = nil) -> Bool {
        guard inputEnabled, playback.phase == .visible, !isHidden,
              let frame = renderedFrame, let camera = renderedCamera,
              frame.node(id)?.activeInHierarchy == true,
              let component = document.component("UIButton", on: id), component.enabled,
              component["m_Interactable"].flag(true),
              actionsByID[id] != nil || closeButtonIDs.contains(id) || quitButtonIDs.contains(id) else { return false }
        let eligible: Bool
        if let point {
            eligible = bounds.contains(point) && frame.button(at: point, camera: camera.camera, viewport: bounds) == id
        } else { eligible = frame.hits.filter { $0.buttonID == id }.contains { hit in
            // A masked/offscreen button cannot be activated by accessibility.
            // Probe its interior using the same source matrices as mouse hits.
            [SIMD2<Double>(0.5, 0.5), SIMD2(0.25, 0.25), SIMD2(0.75, 0.25),
             SIMD2(0.25, 0.75), SIMD2(0.75, 0.75)].contains { fraction in
                let p = hit.rect.origin + hit.rect.size * fraction
                guard let projected = camera.camera.project(SIMD3<Double>(p.x, p.y, 0), world: hit.world, viewport: bounds)?.point,
                      bounds.contains(projected) else { return false }
                return frame.button(at: projected, camera: camera.camera, viewport: bounds) == id
            }
        } }
        guard eligible else { return false }
        let cooldown = component["_clickCd"].float()
        guard cooldown.isFinite, cooldown >= 0,
              lastAcceptedClick[id].map({ time > $0 + cooldown }) ?? true else { return false }
        lastAcceptedClick[id] = time
        if let action = actionsByID[id] { onAction?(action) }
        else if quitButtonIDs.contains(id) { onQuit?() }
        else if closeButtonIDs.contains(id) { onClose?() }
        else { return false }
        return true
    }
    private func refreshInteractionScheduling() {
        // Pointer state is timestamped by updateAnimatorStates at the input
        // event, not at the next frame. An existing display clock will sample
        // that same animation on its next tick. Rebuilding synchronously here
        // adds a second scene traversal and restarts the clock for every hover,
        // press and release, delaying the very input that triggered it.
        if desktopMode, timer != nil { return }
        refreshPlaybackScheduling()
    }

    private func updateHover(_ event: NSEvent, forceRefresh: Bool = false) {
        refreshSourceCursor()
        let next = button(at: point(event))
        if next != hovered {
            hovered = next; updateAnimatorStates(at: now)
            refreshInteractionScheduling()
        }
        else if forceRefresh || (timer == nil && !HUDRuntimeAppearance.reduceMotion) { refreshInteractionScheduling() }
    }
    override func mouseEntered(with event: NSEvent) { updateHover(event) }
    override func mouseMoved(with event: NSEvent) { onPointerMove?(); updateHover(event) }
    override func mouseExited(with event: NSEvent) { restoreSourceCursor(); hovered = nil; updateAnimatorStates(at: now); refreshInteractionScheduling() }
    override func cursorUpdate(with event: NSEvent) { refreshSourceCursor() }
    override func mouseDown(with event: NSEvent) {
        forwardingBackgroundPress = false
        pressedIndustryLogo = inputEnabled && playback.phase == .visible && industryLogoContains(point(event))
        if pressedIndustryLogo { window?.makeFirstResponder(self); return }
        if let direction = desktopScrollDirection(at: point(event)) { pressedScrollDirection = direction; return }
        pressedScrollDirection = nil
        if desktopMode, !isHidden, playback.phase != .concealed,
           !sourceControlContains(point(event)),
           let onBackgroundMouseDown {
            // The native host owns dismissal and editing outside source
            // controls. Keep the original window-space event coordinates.
            forwardingBackgroundPress = true
            onBackgroundMouseDown(event)
            return
        }
        window?.makeFirstResponder(self)
        pressed = button(at: point(event)); hovered = pressed
        cancelBannerPointer()
        if let pressed, document.widgets?.bannerButtonIDs.contains(pressed) == true,
           bannerViewportPoint(point(event), requireHit: true) != nil {
            let pixels = bannerScreenPixels(point(event))
            do {
                try frameBuilder.initializeWidgetBannerPointer(at: now, screenPosition: pixels)
                bannerPointerPixels = pixels
            } catch { diagnostics.append("Source banner pointer: \(error)") }
        }
        updateAnimatorStates(at: now); refreshInteractionScheduling()
    }
    override func mouseDragged(with event: NSEvent) {
        if let previous = bannerPointerPixels {
            let p = point(event), pixels = bannerScreenPixels(p)
            bannerPointerPixels = pixels
            if let viewport = bannerViewportPoint(p, requireHit: false) {
                do {
                    let result = try frameBuilder.dragWidgetBannerPointer(at: now, screenPosition: pixels,
                        frameDelta: pixels - previous, viewportLocalX: Float(viewport.local.x))
                    if result == .began, pressed != document.widgets?.bannerListNodeID {
                        // InputSystem cancels the pressed child only when its
                        // pointerPress differs from the viewport pointerDrag.
                        pressed = nil; updateAnimatorStates(at: now)
                    }
                } catch { cancelBannerPointer(); diagnostics.append("Source banner drag: \(error)") }
            }
        }
        updateHover(event, forceRefresh: true)
    }
    override func mouseUp(with event: NSEvent) {
        if pressedIndustryLogo {
            pressedIndustryLogo = false
            if industryLogoContains(point(event)) { _ = flickerIndustryLogo() }
            return
        }
        if let direction = pressedScrollDirection {
            pressedScrollDirection = nil
            if desktopScrollDirection(at: point(event)) == direction { _ = scrollDesktopNavigation(direction, animated: true) }
            return
        }
        if forwardingBackgroundPress {
            forwardingBackgroundPress = false
            onBackgroundMouseUp?(event)
            return
        }
        let released = button(at: point(event)), down = pressed
        let previous = bannerPointerPixels
        pressed = nil; hovered = released; updateAnimatorStates(at: now)
        // Original release dispatches PointerUp/Click before EndDrag. A mapped
        // macOS action can hide the view and cancel its pending drag first.
        if let down, released == down { performClick(on: down, at: now, point: point(event)) }
        if let previous, bannerPointerPixels != nil, inputEnabled,
           let viewport = bannerViewportPoint(point(event), requireHit: false) {
            let pixels = bannerScreenPixels(point(event)), scale = window?.backingScaleFactor ?? 1
            do {
                try frameBuilder.endWidgetBannerPointer(at: now, screenPosition: pixels,
                    frameDelta: pixels - previous, screenWidth: Float(bounds.width * scale),
                    panelRectWidth: viewport.panelWidth, reduceMotion: HUDRuntimeAppearance.reduceMotion)
            } catch { diagnostics.append("Source banner release: \(error)") }
        }
        cancelBannerPointer(); refreshInteractionScheduling()
    }
    override func keyDown(with event: NSEvent) {
        if let onUnhandledKey { onUnhandledKey(event) }
        else if inputEnabled, event.keyCode == 53 { onClose?() } else { super.keyDown(with: event) }
    }
    override func scrollWheel(with event: NSEvent) {
        guard inputEnabled, playback.phase == .visible, let frame = renderedFrame,
              let camera = renderedCamera, let scroll = frame.layoutReport.scroll,
              scroll.hiddenLength > 0, let node = frame.node(scroll.viewportID), let rect = node.rect else {
            super.scrollWheel(with: event); return
        }
        let inside = camera.camera.hit(point(event), world: camera.worldRoot * node.worldMatrix, rect: rect, viewport: bounds) != nil
        guard inside || (desktopMode && (desktopScrollMotion.isGestureActive
            || (desktopScrollMotion.ownsMomentum && !event.momentumPhase.isEmpty))) else { super.scrollWheel(with: event); return }
        let delta = event.hasPreciseScrollingDeltas ? Double(event.scrollingDeltaY) : Double(event.scrollingDeltaY) * 10
        if desktopMode {
            func phase(_ value: NSEvent.Phase) -> HUDSourceDesktopScrollMotion.Phase {
                if value.contains(.cancelled) { return .cancelled }
                if value.contains(.ended) { return .ended }
                if value.contains(.began) { return .began }
                return value.isEmpty ? .none : .changed
            }
            if event.phase.contains(.began) && event.momentumPhase.isEmpty && !inside {
                desktopScrollMotion.reset(to: verticalNormalizedPosition, at: now)
                verticalNormalizedPosition = desktopScrollMotion.position
                refreshPlaybackScheduling(); super.scrollWheel(with: event); return
            }
            desktopScrollMotion.gesture(by: delta * sourceScrollUnitsPerPoint(node: node, rect: rect, camera: camera) / max(1, scroll.hiddenLength),
                hiddenLength: scroll.hiddenLength, at: now, phase: phase(event.phase), momentum: phase(event.momentumPhase),
                reduceMotion: HUDRuntimeAppearance.reduceMotion)
            verticalNormalizedPosition = desktopScrollMotion.position
            refreshDesktopScrollIndicators()
            if timer == nil || HUDRuntimeAppearance.reduceMotion { refreshPlaybackScheduling() }
        } else {
            guard inside else { super.scrollWheel(with: event); return }
            verticalNormalizedPosition = HUDSourceWatchLayout(document: document).scrolledPosition(verticalNormalizedPosition, delta: delta, info: scroll)
            render(at: now)
        }
    }

    private func industryLogoPolygon() -> [CGPoint] {
        guard desktopMode, let frame = renderedFrame, let camera = renderedCamera,
              let id = logoNodeIDs.first, let node = frame.node(id), node.activeInHierarchy,
              let rect = node.rect else { return [] }
        return rect.corners.compactMap {
            camera.camera.project($0, world: camera.worldRoot * node.worldMatrix, viewport: bounds)?.point
        }
    }
    private func industryLogoContains(_ point: CGPoint) -> Bool {
        let polygon = industryLogoPolygon()
        return polygon.count == 4 && Self.polygonPath(polygon).contains(point)
    }
    @discardableResult private func flickerIndustryLogo() -> Bool {
        guard desktopMode, inputEnabled, playback.phase == .visible else { return false }
        // A small, finite signature flicker on the existing render clock.
        // Reduce Motion keeps the logo steady, while still accepting the click.
        guard !HUDRuntimeAppearance.reduceMotion else { return true }
        logoFlickerStarted = now
        refreshPlaybackScheduling()
        return true
    }
    private func refreshIndustryLogo(at time: TimeInterval) {
        guard desktopMode else { return }
        let elapsed = logoFlickerStarted.map { max(0, time - $0) }
        let opacity: Float
        if let elapsed, elapsed < 0.46 {
            switch elapsed {
            case 0.05..<0.09: opacity = 0.22
            case 0.20..<0.25: opacity = 0.38
            default: opacity = 1
            }
        } else { logoFlickerStarted = nil; opacity = 1 }
        // Avoid copying the style dictionary on every idle frame, while still
        // restoring a logo whose finite flicker was interrupted by concealment.
        guard logoNodeIDs.contains(where: {
            frameBuilder.desktopGraphicStyles[$0] != .init(opacity: opacity * (document.scene.node($0)?.name == "EndfieldTextGlow" ? 0.78 : 1))
        }) else { return }
        var styles = frameBuilder.desktopGraphicStyles
        for id in logoNodeIDs {
            styles[id] = .init(opacity: opacity * (document.scene.node(id)?.name == "EndfieldTextGlow" ? 0.78 : 1))
        }
        frameBuilder.desktopGraphicStyles = styles
    }

    private func sourceControlContains(_ point: CGPoint) -> Bool {
        if desktopScrollDirection(at: point) != nil || industryLogoContains(point) { return true }
        guard let frame = renderedFrame, let camera = renderedCamera else { return true }
        // Disabled or deploying source cards must still consume their region;
        // the ordinary source path separately gates their actions on input.
        if mapFilteredButton(frame.button(at: point, camera: camera.camera, viewport: bounds), at: point) != nil { return true }
        guard let scroll = frame.layoutReport.scroll,
              let viewport = frame.node(scroll.viewportID), viewport.activeInHierarchy,
              let rect = viewport.rect else { return false }
        // Empty gaps between cards still belong to the scroll viewport.
        return camera.camera.hit(point, world: camera.worldRoot * viewport.worldMatrix,
                                 rect: rect, viewport: bounds) != nil
    }
    func setDesktopNavigation(_ entries: [HUDDesktopWatchNavigation.Entry]) {
        guard desktopMode else { return }
        guard entries.count != desktopEntries.count || zip(entries, desktopEntries).contains(where: {
            $0.target != $1.target || $0.title != $1.title || $0.shortcut?.iconPreset != $1.shortcut?.iconPreset
                || $0.shortcut?.icon !== $1.shortcut?.icon
        }) else { return }
        let rightIndices = entries.indices.filter { $0 >= 4 && entries[$0].target.module?.group != .bottom }
        if rightIndices.count != desktopRightEntryIndices.count {
            desktopNavigation = try? HUDSourceDesktopNavigationLayout(document: document, entryCount: rightIndices.count)
            verticalNormalizedPosition = 1
            desktopScrollMotion.reset(to: 1, at: now)
        }
        desktopRightEntryIndices = rightIndices
        desktopEntries = entries
        // Rebinding changes meaning, so an in-flight pointer press must not
        // activate the replacement item after a saved app is edited or removed.
        pressed = nil; hovered = nil; lastAcceptedClick.removeAll()
        desktopBindings.removeAll()
        updateDesktopBindings(at: now)
        refreshPlaybackScheduling()
    }

    private func updateDesktopBindings(at time: Double) {
        guard desktopBindingPosition != verticalNormalizedPosition || desktopBindings.isEmpty else { return }
        desktopBindingPosition = verticalNormalizedPosition
        var bindings = Dictionary(uniqueKeysWithValues: zip(document.buttons.prefix(4), desktopEntries.prefix(4).indices).map { ($0.nodeID, $1) })
        if let desktopNavigation {
            bindings.merge(desktopNavigation.sample(normalizedPosition: verticalNormalizedPosition).assignments.mapValues { desktopRightEntryIndices[$0] }) { _, right in right }
        }
        for button in desktopSupplementalButtons {
            let target: HUDNavigationTarget = .module(button.path.hasSuffix("/TechtreeBtn") ? .storage : .activityMonitor)
            if let index = desktopEntries.firstIndex(where: { $0.target == target }) { bindings[button.nodeID] = index }
        }
        guard bindings != desktopBindings else { return }
        pressed = nil; hovered = nil; lastAcceptedClick.removeAll()
        buttonAnimation.reset(at: time, reduceMotion: HUDRuntimeAppearance.reduceMotion)
        selectableColor.reset(at: time)
        bindDesktopButtons(bindings)
        updateAnimatorStates(at: time)
    }

    private func bindDesktopButtons(_ bindings: [HUDSourceID: Int]) {
        desktopBindings = bindings
        lastDesktopProjection = nil
        lastAccessibilityGeometry = nil
        CATransaction.begin(); CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        for button in desktopButtons {
            guard let index = bindings[button.nodeID], desktopEntries.indices.contains(index) else {
                frameBuilder.desktopHiddenNodes.insert(button.nodeID)
                actionsByID.removeValue(forKey: button.nodeID)
                updateAccessibilityVisibility(button.nodeID, visible: false)
                for label in button.labels { desktopLabels[label.nodeID]?.container.isHidden = true }
                if let iconID = desktopIconIDs[button.nodeID] { desktopIcons[iconID]?.container.isHidden = true }
                continue
            }
            frameBuilder.desktopHiddenNodes.remove(button.nodeID)
            let entry = desktopEntries[index]
            actionsByID[button.nodeID] = ButtonAction(source: button, target: entry.target)
            accessibilityButtons[button.nodeID]?.setAccessibilityLabel(entry.title)
            accessibilityButtons[button.nodeID]?.setAccessibilityHelp(entry.target.module?.title ?? (L10n.text("Open ", "打开 ") + entry.title))
            let isReport = button.path.hasSuffix("/ReportBtn") && entry.target == .module(.activityMonitor)
            let reportArtwork = isReport ? desktopReportArtwork() : nil
            if !isReport || reportArtwork != nil,
               let iconID = desktopIconIDs[button.nodeID] ?? document.scene.nodes.first(where: {
                $0.path.hasPrefix(button.path + "/") && (entry.target.module?.group == .bottom
                    ? $0.path.contains("/IconShadow/") && $0.name.trimmingCharacters(in: .whitespaces) == "Icon"
                    : ["Icon", "Icon01"].contains($0.name))
            })?.id {
                desktopIconIDs[button.nodeID] = iconID
                frameBuilder.desktopHiddenNodes.insert(iconID)
                let iconLayers: (container: CALayer, content: CALayer, vector: CAShapeLayer, image: CALayer, clip: CAShapeLayer)
                if let existing = desktopIcons[iconID] { iconLayers = existing }
                else {
                    let container = CALayer(), content = CALayer(), vector = CAShapeLayer(), image = CALayer(), clip = CAShapeLayer()
                    container.name = "desktop.watch.icon." + iconID.rawValue
                    container.zPosition = 10; container.addSublayer(content); container.mask = clip
                    content.anchorPoint = .zero; content.position = .zero
                    content.bounds = CGRect(x: 0, y: 0, width: 32, height: 32)
                    vector.frame = content.bounds; image.frame = content.bounds
                    content.addSublayer(vector); content.addSublayer(image)
                    vector.fillRule = .evenOdd; vector.lineCap = .round; vector.lineJoin = .round
                    // AppKit can replace contents when scale/appearance changes
                    // outside this binding transaction. These artwork layers
                    // never own an implicit contents transition.
                    image.actions = ["contents": NSNull(), "bounds": NSNull(), "position": NSNull()]
                    image.contentsGravity = .resizeAspect
                    layer?.addSublayer(container)
                    iconLayers = (container, content, vector, image, clip); desktopIcons[iconID] = iconLayers
                }
                let ink = entry.target.module?.group == .bottom ? NSColor.white : NSColor(white: 0.12, alpha: 1)
                iconLayers.vector.fillColor = ink.cgColor; iconLayers.vector.strokeColor = ink.cgColor
                let sourceImage: CGImage?
                if isReport {
                    sourceImage = reportArtwork
                } else if let shortcut = entry.shortcut {
                    iconLayers.vector.path = HUDSourceDesktopIconLayout.path(AppShortcutArtwork.path(for: shortcut.iconPreset, in: iconLayers.content.bounds))
                    iconLayers.vector.lineWidth = 32 / 17
                    iconLayers.vector.fillColor = nil
                    sourceImage = AppShortcutArtwork.image(for: shortcut.iconPreset, original: shortcut.icon,
                        size: 96, color: ink)
                } else {
                    iconLayers.vector.path = HUDSourceDesktopIconLayout.path(HUDNavigationEntry.iconPath(for: entry.target.module ?? .addApp))
                    iconLayers.vector.lineWidth = 0
                    sourceImage = HUDNavigationEntry.gameIcon(for: entry.target.module)?.cgImage(size: 96, tint: ink)
                }
                // Report retains its full authored canvas, size and padding.
                iconLayers.image.contents = isReport ? sourceImage : sourceImage.map(HUDSourceDesktopIconLayout.image)
                iconLayers.image.transform = CATransform3DIdentity
                iconLayers.image.frame = isReport ? iconLayers.content.bounds : iconLayers.content.bounds.insetBy(dx: 3, dy: 3)
                let artworkScale: CGFloat = entry.target.module == .workMode ? 0.9 : 1
                iconLayers.vector.transform = CATransform3DMakeScale(artworkScale, artworkScale, 1)
                iconLayers.image.transform = CATransform3DMakeScale(artworkScale, artworkScale, 1)
                iconLayers.image.isHidden = iconLayers.image.contents == nil
                iconLayers.vector.isHidden = !iconLayers.image.isHidden
            }
            guard let label = button.label else { continue }
            let layers: (container: CALayer, text: CATextLayer, clip: CAShapeLayer)
            if let existing = desktopLabels[label.nodeID] { layers = existing }
            else {
                let container = CALayer(), text = CATextLayer(), clip = CAShapeLayer()
                container.name = "desktop.watch.label." + label.nodeID.rawValue
                container.zPosition = 10; container.masksToBounds = false
                container.addSublayer(text); container.mask = clip
                text.anchorPoint = .zero; text.position = .zero
                text.font = NSFont.systemFont(ofSize: 22, weight: .medium); text.fontSize = 22
                text.actions = ["contents": NSNull(), "bounds": NSNull(), "position": NSNull()]
                text.alignmentMode = .center; text.truncationMode = .end
                text.foregroundColor = NSColor(white: 0.12, alpha: 1).cgColor
                layer?.addSublayer(container)
                layers = (container, text, clip); desktopLabels[label.nodeID] = layers
            }
            var caption = entry.title
            if !L10n.isCJK {
                switch entry.target.module {
                case .fileShelf: caption = "Temporary\nFile Shelf"
                case .clipboard: caption = "Clipboard\nCache"
                case .activityMonitor: caption = "Activity\nMonitor"
                case .profile: caption = "Personal\nProfile"
                default: break
                }
            } else if L10n.resolvedLanguage == .japanese && entry.target.module == .fileShelf {
                caption = "一時ファイル\nシェルフ"
            }
            layers.text.string = caption
            layers.text.isWrapped = entry.target.module != nil && !(L10n.isChinese && entry.target.module == .fileShelf)
            layers.text.truncationMode = entry.target.module == nil ? .end : .none
            desktopLabelButtons[label.nodeID] = button.nodeID
        }
        updateDesktopSelection()
    }

    /// Profile persistence stays in UserProfileStore. This binds only the
    /// compact card's visible fields; work-duration ticks do not upload images.
    func setDesktopProfile(_ profile: UserProfile, avatar: NSImage?, background: NSImage?, avatarOrientation: Int32 = 1) {
        guard desktopMode, let card = document.desktopProfileCard else { return }
        desktopProfileInput = (profile, avatar, background, avatarOrientation)
        let accent = profile.resolvedAccent(fallback: HUDRuntimeAppearance.accent).usingColorSpace(.sRGB) ?? .white
        let key = ProfileKey(strings: [profile.name, profile.tag, profile.uid, L10n.text("Authority", "权限等级"), L10n.text("MAX", "满级")],
            values: [Double(profile.permissionLevel), profile.avatarZoom, profile.avatarOffsetX, profile.avatarOffsetY,
                profile.thumbnailOffsetX, profile.thumbnailOffsetY, Double(avatarOrientation),
                Double(accent.redComponent), Double(accent.greenComponent), Double(accent.blueComponent)],
            avatar: avatar.map(ObjectIdentifier.init), background: background.map(ObjectIdentifier.init))
        guard desktopProfileKey != key else { return }
        desktopProfileKey = key
        let level = min(60, max(1, profile.permissionLevel))
        let captions = ["managerName": profile.name + " #" + profile.tag, "managerNumber": "UID: " + profile.uid,
            "managerLevel": String(level), "managerLevelLabel": L10n.text("Authority", "权限等级"),
            "progressTxt": level == 60 ? L10n.text("MAX", "满级") : ""]
        CATransaction.begin(); CATransaction.setDisableActions(true)
        for (binding, caption) in captions {
            guard let id = card.node(binding), let component = document.component("UIText", on: id),
                  let rect = document.scene.node(id)?.transform.rect else { continue }
            let layers: (container: CALayer, text: CATextLayer, clip: CAShapeLayer)
            if let existing = desktopLabels[id] { layers = existing }
            else {
                let container = CALayer(), text = CATextLayer(), clip = CAShapeLayer()
                container.name = "desktop.profile." + binding; container.zPosition = 10
                container.addSublayer(text); container.mask = clip; layer?.addSublayer(container)
                text.anchorPoint = .zero; text.position = .zero
                text.truncationMode = .end
                layers = (container, text, clip); desktopLabels[id] = layers
            }
            var size = CGFloat(component["m_fontSize"].float(16))
            while size > 10 && (caption as NSString).size(withAttributes: [.font: NSFont.systemFont(ofSize: size, weight: .medium)]).width > CGFloat(rect.sizeDelta.x) { size -= 0.5 }
            layers.text.string = caption; layers.text.font = NSFont.systemFont(ofSize: size, weight: .medium); layers.text.fontSize = size
            layers.text.alignmentMode = ["managerLevelLabel", "progressTxt"].contains(binding) ? .right : .left
            layers.text.foregroundColor = (binding == "progressTxt" ? accent : .white).cgColor
            desktopProfileLabelIDs.insert(id); desktopLabelButtons[id] = card.scene.rootID
        }
        CATransaction.commit()
        var properties: [HUDSourceID: [String: Double]] = [:]
        let highlightID = desktopHoverFeedback.profileHighlightNodeID
        for id in card.artworkGlowNodeIDs { properties[id] = ["m_Color.a": id == highlightID ? 1 : 0] }
        if let id = card.backgroundNodeID, let sprite = card.defaultBackgroundSprite?["id"].string {
            frameBuilder.desktopSprites[id] = sprite
        }
        if let slider = card.node("levelSlider") { properties[slider] = ["m_FillAmount": Double(level) / 60] }
        let colored = Set([card.node("levelSlider"), card.node("headFrameImg")].compactMap { $0 })
            .union(card.scene.nodes.filter { ["IconRight", "ArrowImage"].contains($0.name) }.map(\.id))
        for id in colored {
            properties[id, default: [:]].merge(["m_Color.r": Double(accent.redComponent), "m_Color.g": Double(accent.greenComponent),
                "m_Color.b": Double(accent.blueComponent)]) { _, current in current }
        }
        do {
            for (kind, image, size, offset, zoom, orientation) in [
                ("avatar", avatar, CGSize(width: 136, height: 136), CGPoint(x: profile.avatarOffsetX, y: profile.avatarOffsetY), profile.avatarZoom, avatarOrientation),
                ("background", background, CGSize(width: 412, height: 158), CGPoint(x: profile.thumbnailOffsetX, y: profile.thumbnailOffsetY), 1.0, Int32(1))] {
                let id = kind == "avatar" ? card.node("playerHead") : card.backgroundNodeID
                guard let id else { continue }
                let imageKey = ProfileKey(strings: [], values: [Double(size.width), Double(size.height), Double(offset.x), Double(offset.y), zoom, Double(orientation)]
                    + (kind == "background" ? [Double(accent.redComponent), Double(accent.greenComponent), Double(accent.blueComponent)] : []),
                    avatar: kind == "avatar" ? image.map(ObjectIdentifier.init) : nil,
                    background: kind == "background" ? image.map(ObjectIdentifier.init) : nil)
                if desktopProfileImageKeys[kind] != imageKey {
                    var bitmap: CGImage?
                    if let original = image?.cgImage(forProposedRect: nil, context: nil, hints: nil) {
                        bitmap = HUDPortraitArtwork.renderedImage(original, targetSize: size, zoom: zoom, offset: offset, contentsScale: 2, orientation: orientation)
                    } else if kind == "avatar" {
                        let portrait = HUDPortraitArtwork.makeLayer(image: nil, profile: nil, size: size, ink: .white, accent: .white, contentsScale: 2, includeFrame: false)
                        let raster = CGContext(data: nil, width: 272, height: 272, bitsPerComponent: 8, bytesPerRow: 0,
                            space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
                        raster?.translateBy(x: 0, y: 272); raster?.scaleBy(x: 2, y: -2)
                        if let raster { portrait.sublayers?.first?.render(in: raster) }
                        bitmap = raster?.makeImage()
                    } else { bitmap = nil }
                    if kind == "background" {
                        let source = try profileBackgroundArtwork(card)
                        let themed = try HUDSourceProfileArtwork.themedBackgroundArtwork(source, accent: accent)
                        bitmap = try bitmap.map { try HUDSourceProfileArtwork.compositedBackground($0, artwork: themed) } ?? themed
                        if let highlightID, let device = renderer.device {
                            let highlight = try HUDSourceProfileArtwork.hoverArtwork(source, accent: accent)
                            let texture = try HUDSourceProfileArtwork.makeTexture(highlight, device: device)
                            let name = "desktop.profile.hover"
                            try renderer.registerTexture(named: name, texture: texture, filterMode: 1, wrapU: 1, wrapV: 1)
                            frameBuilder.desktopImages[highlightID] = .init(texture: name, size: SIMD2(Float(highlight.width), Float(highlight.height)))
                            frameBuilder.desktopNormalMaterialNodes.insert(highlightID)
                        }
                    }
                    if let bitmap, let device = renderer.device {
                        let texture = try HUDSourceProfileArtwork.makeTexture(bitmap, device: device)
                        let name = "desktop.profile." + kind
                        try renderer.registerTexture(named: name, texture: texture, filterMode: 1, wrapU: 1, wrapV: 1)
                        frameBuilder.desktopImages[id] = .init(texture: name, size: SIMD2(Float(bitmap.width), Float(bitmap.height)))
                    } else { frameBuilder.desktopImages.removeValue(forKey: id) }
                    desktopProfileImageKeys[kind] = imageKey
                }
            }
        } catch { desktopProfileKey = nil; diagnostics.append("Profile artwork: " + String(describing: error)) }
        frameBuilder.desktopProperties = properties
        settledRenderPacket = nil; lastDesktopProjection = nil
        refreshPlaybackScheduling()
    }

    private func profileBackgroundArtwork(_ card: HUDSourceDesktopProfileCard) throws -> CGImage {
        if let artwork = desktopProfileBackgroundArtwork { return artwork }
        guard let sprite = card.defaultBackgroundSprite, let textureID = sprite["texture"]["id"].string,
              let texture = card.sprites["source_textures"].array.first(where: { $0["id"].string == textureID }),
              let rawFile = texture["raw"]["file"].string, texture["texture_format"].float() == 25 else {
            throw HUDSourceError.invalid("Selected profile artwork is unavailable")
        }
        // Every selected BC7 source already ships its exact decoded mip chain
        // for Intel compatibility; reuse those pixels instead of packaging a PNG.
        let filename = URL(fileURLWithPath: rawFile).deletingPathExtension().lastPathComponent + ".bgra-mips.bin"
        let data = try HUDSourceResourceData.read(document.root.deletingLastPathComponent().appendingPathComponent("Textures/" + filename))
        let raw = sprite["raw_sprite"]["m_Rect"], rendered = sprite["effective_render_data"]
        let textureRect = rendered["textureRect"], offset = rendered["textureRectOffset"]
        let artwork = try HUDSourceProfileArtwork.backgroundArtwork(bgra: data,
            textureWidth: Int(texture["width"].float()), textureHeight: Int(texture["height"].float()),
            spriteRect: CGRect(x: textureRect["x"].float() - offset["x"].float(),
                y: textureRect["y"].float() - offset["y"].float(), width: raw["width"].float(), height: raw["height"].float()))
        desktopProfileBackgroundArtwork = artwork
        return artwork
    }

    private func desktopReportArtwork() -> CGImage? {
        if let image = Self.desktopReportIconArtwork { return image }
        guard let node = document.scene.nodes.first(where: { $0.path.hasSuffix("/ReportBtn/Icon/IconShadow/Icon") }),
              let component = document.component("UIImage", on: node.id),
              let sprite = document.spriteByComponent[component.id],
              let textureID = sprite["texture"]["id"].string,
              let texture = document.sprites["source_textures"].array.first(where: { $0["id"].string == textureID }),
              let file = texture["png"]["file"].string, texture["texture_format"].float() == 25 else { return nil }
        do {
            let filename = URL(fileURLWithPath: file).deletingPathExtension().lastPathComponent + ".bgra-mips.bin"
            let bytes = try HUDSourceResourceData.read(document.root.deletingLastPathComponent().appendingPathComponent("Textures/" + filename))
            let rect = sprite["raw_sprite"]["m_Rect"]
            let original = try HUDSourceProfileArtwork.backgroundArtwork(bgra: bytes,
                textureWidth: Int(texture["width"].float()), textureHeight: Int(texture["height"].float()),
                spriteRect: CGRect(x: rect["x"].float(), y: rect["y"].float(),
                    width: rect["width"].float(), height: rect["height"].float()))
            guard let context = CGContext(data: nil, width: original.width, height: original.height,
                bitsPerComponent: 8, bytesPerRow: original.width * 4,
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
            // Bake once into this tiny bitmap. Projection and inherited opacity
            // follow the existing icon plane, without a live blur or new timer.
            context.setShadow(offset: .zero, blur: 3,
                color: NSColor.white.withAlphaComponent(0.65).cgColor)
            context.draw(original, in: CGRect(x: 0, y: 0, width: original.width, height: original.height))
            Self.desktopReportIconArtwork = context.makeImage()
            return Self.desktopReportIconArtwork
        } catch {
            diagnostics.append("Report icon artwork: " + String(describing: error))
            return nil // Preserve the original Metal glyph if extraction fails.
        }
    }

    private func desktopCaptionSize(_ original: CGSize, target: HUDNavigationTarget?) -> CGSize {
        guard target?.module == .fileShelf, L10n.isCJK, !L10n.isChinese else { return original }
        // Match the widest authored caption instead of shrinking the longer
        // Japanese/Korean shelf name into a single narrow source line.
        return CGSize(width: max(original.width, 124), height: max(original.height, 56))
    }

    private func updateDesktopSelection(previousModule: HUDModule? = nil) {
        // Base glow geometry depends on the document/preferences, not module
        // selection. Preserve the in-progress hover/logo fields on tab swaps.
        if previousModule == nil { refreshDesktopGlowStyles() }
        func selectionChanged(_ target: HUDNavigationTarget?) -> Bool {
            guard let previousModule else { return true }
            return target == .module(previousModule) || target == .module(selectedDesktopModule)
        }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        for (id, element) in accessibilityButtons {
            guard let target = actionsByID[id]?.target, selectionChanged(target) else { continue }
            element.setAccessibilityValue(target == .module(selectedDesktopModule)
                ? L10n.text("Selected", "已选择") : L10n.text("Not selected", "未选择"))
        }
        for (labelID, layers) in desktopLabels {
            if desktopProfileLabelIDs.contains(labelID) { continue }
            let target = desktopLabelButtons[labelID].flatMap { actionsByID[$0]?.target }
            guard selectionChanged(target) else { continue }
            let selected = target == .module(selectedDesktopModule)
            let bottom = target?.module?.group == .bottom
            let right = desktopLabelButtons[labelID].flatMap { actionsByID[$0]?.source.path.contains("/RightBottomNode/") } ?? false
            var size: CGFloat = bottom ? 20 : right ? 22 : 26
            let weight: NSFont.Weight = selected ? .bold : .medium
            if target?.module == .fileShelf && L10n.isChinese { size = min(size, 20) }
            if let rect = document.scene.node(labelID)?.transform.rect,
               let caption = layers.text.string as? String {
                let area = desktopCaptionSize(CGSize(width: rect.sizeDelta.x, height: rect.sizeDelta.y), target: target)
                let width = max(1, area.width - 2), height = max(1, area.height - 2)
                while size > 10 {
                    let font = NSFont.systemFont(ofSize: size, weight: weight)
                    let measured: CGSize
                    if layers.text.isWrapped {
                        measured = (caption as NSString).boundingRect(with: CGSize(width: width, height: .greatestFiniteMagnitude),
                            options: [.usesLineFragmentOrigin, .usesFontLeading], attributes: [.font: font]).size
                    } else { measured = (caption as NSString).size(withAttributes: [.font: font]) }
                    if measured.height <= height && measured.width <= width + 0.5 { break }
                    size -= 0.5
                }
            }
            layers.text.font = NSFont.systemFont(ofSize: size, weight: weight)
            layers.text.fontSize = size
            layers.text.foregroundColor = (bottom ? NSColor.white : selected
                ? HUDRuntimeAppearance.accent.blended(withFraction: 0.5, of: .black) ?? .black
                : NSColor(white: 0.12, alpha: 1)).cgColor
        }
    }

    /// The transparent UIButton quad is larger than the visible plate. Cache
    /// the exported sprite mesh's boundary once so map clipping and pointer
    /// precedence preserve its curved edge and transparent cutouts.
    private func makeDesktopBottomSilhouettes() -> [DesktopBottomSilhouette] {
        guard desktopMode else { return [] }
        return ["/TechtreeBtn/Icon", "/ReportBtn/Icon"].compactMap { suffix in
            guard let node = document.scene.nodes.first(where: { $0.path.hasSuffix(suffix) }),
                  let image = document.component("UIImage", on: node.id),
                  let sprite = document.spriteByComponent[image.id],
                  Int(image["m_Type"].float()) == 0, !image["m_PreserveAspect"].flag() else { return nil }
            let raw = sprite["raw_sprite"], mesh = sprite["decoded_render_mesh"]
            let size = SIMD2(raw["m_Rect"]["width"].float(), raw["m_Rect"]["height"].float())
            let ppu = raw["m_PixelsToUnits"].float(100), pivot = raw["m_Pivot"].vector2
            guard size.x > 0, size.y > 0, ppu > 0 else { return nil }
            let vertices = mesh["vertices"].array.compactMap { vertex -> SIMD2<Double>? in
                let values = vertex.array
                guard values.count >= 2, let x = values[0].number, let y = values[1].number,
                      x.isFinite, y.isFinite else { return nil }
                return SIMD2(x, y) * ppu / size + pivot
            }
            guard !vertices.isEmpty, vertices.count == mesh["vertices"].array.count else { return nil }
            let triangles = mesh["triangles"].array.flatMap { $0.array }.map { triangle in
                triangle.array.compactMap { $0.number.flatMap { Int(exactly: $0) } }
            }
            guard !triangles.isEmpty, triangles.allSatisfy({ $0.count == 3 && $0.allSatisfy(vertices.indices.contains) }) else { return nil }
            var counts: [Int: Int] = [:], directed: [(Int, Int)] = []
            func edgeKey(_ a: Int, _ b: Int) -> Int { min(a, b) * vertices.count + max(a, b) }
            for triangle in triangles {
                for index in 0..<3 {
                    let a = triangle[index], b = triangle[(index + 1) % 3]
                    counts[edgeKey(a, b), default: 0] += 1; directed.append((a, b))
                }
            }
            var edges = directed.filter { counts[edgeKey($0.0, $0.1)] == 1 }
            var polygons: [[SIMD2<Double>]] = []
            while !edges.isEmpty {
                let first = edges.removeFirst()
                var loop = [first.0], next = first.1
                while next != first.0 {
                    loop.append(next)
                    guard let index = edges.firstIndex(where: { $0.0 == next }) else { return nil }
                    next = edges.remove(at: index).1
                }
                guard loop.count >= 3 else { return nil }
                polygons.append(loop.map { vertices[$0] })
            }
            return DesktopBottomSilhouette(nodeID: node.id, polygons: polygons)
        }
    }

    private func updateDesktopBottomSilhouettes(frame: HUDSourceWatchFrameBuilder.Frame,
                                                 camera: HUDSourceWatchCamera.Frame) {
        guard desktopMode, desktopMapOcclusionEnabled else { return }
        let visible = desktopBottomSilhouettes.compactMap { silhouette -> (DesktopBottomSilhouette, HUDSourceResolvedNode, HUDSourceRect)? in
            guard let node = frame.node(silhouette.nodeID), node.activeInHierarchy, let rect = node.rect,
                  !frameBuilder.desktopHiddenNodes.contains(silhouette.nodeID),
                  (frame.inheritedAlpha[silhouette.nodeID] ?? 1) > 0.001 else { return nil }
            return (silhouette, node, rect)
        }
        let worlds = visible.map { camera.worldRoot * $0.1.worldMatrix }
        let key = DesktopBottomProjectionKey(nodeIDs: visible.map { $0.0.nodeID }, worlds: worlds,
            rects: visible.map { $0.2 }, camera: camera.camera.viewProjection, bounds: bounds)
        guard key != lastDesktopBottomProjection else { return }
        lastDesktopBottomProjection = key
        var polygons: [[CGPoint]] = []
        for (index, entry) in visible.enumerated() {
            for polygon in entry.0.polygons {
                let points = polygon.compactMap { normalized -> CGPoint? in
                    let local = entry.2.origin + normalized * entry.2.size
                    return camera.camera.project(SIMD3(local.x, local.y, 0), world: worlds[index], viewport: bounds)?.point
                }
                if points.count == polygon.count { polygons.append(points) }
            }
        }
        projectedDesktopBottomSilhouettes = polygons
        desktopBottomHitPaths = polygons.map(Self.polygonPath)
        onDesktopBottomSilhouettes?(polygons)
    }

    func bottomButtonContains(_ point: CGPoint) -> Bool {
        desktopMapOcclusionEnabled && inputEnabled && playback.phase == .visible
            && desktopBottomHitPaths.contains { $0.contains(point) }
    }

    private func updateDesktopCenterPlane(frame: HUDSourceWatchFrameBuilder.Frame,
                                          camera: HUDSourceWatchCamera.Frame) throws {
        guard onDesktopCenterPlane != nil, let id = desktopCenterID, let node = frame.node(id) else { return }
        let world = camera.worldRoot * node.worldMatrix
        guard lastDesktopCenterWorld != world || lastDesktopCenterBounds != bounds else { return }
        if desktopPlaneCalibration?.bounds != bounds {
            // Calibrate once against the deployed source plane. Wrapper scale
            // and live gyro must remain in the projection, never normalized out.
            let neutral = try cameraModel.frame(screenSize: SIMD2(Double(bounds.width), Double(bounds.height)),
                                                localRotation: cameraModel.rootRotation)
            let pose = try document.animation.pose(entranceTime: document.animation.entrance.lastKeyTime,
                ambientTime: nil, exitTime: nil, canvasResolution: neutral.layout.canvasSize)
            guard let resting = try document.scene.resolve(overrides: pose.transforms)[id],
                  let origin = neutral.camera.project(.zero, world: neutral.worldRoot * resting.worldMatrix, viewport: bounds)?.point,
                  let step = neutral.camera.project(SIMD3(1, 0, 0), world: neutral.worldRoot * resting.worldMatrix, viewport: bounds)?.point else { return }
            let pixelsPerUnit = hypot(step.x - origin.x, step.y - origin.y)
            guard pixelsPerUnit > 0.0001 else { return }
            let designScale = max(0.1, min(bounds.width / 1100, bounds.height / 740, 1.15))
            desktopPlaneCalibration = (bounds, Double(designScale / pixelsPerUnit))
        }
        guard let unit = desktopPlaneCalibration?.unitsPerPoint else { return }
        let corners = [SIMD3(-500 * unit, 320 * unit, 0), SIMD3(500 * unit, 320 * unit, 0),
                       SIMD3(500 * unit, -320 * unit, 0), SIMD3(-500 * unit, -320 * unit, 0)].compactMap {
            camera.camera.project($0, world: world, viewport: bounds)?.point
        }
        guard corners.count == 4,
              abs((corners[1].x-corners[0].x)*(corners[3].y-corners[0].y)
                  - (corners[1].y-corners[0].y)*(corners[3].x-corners[0].x)) > 0.001 else { return }
        let projection = Self.projectiveTextTransform(corners, size: CGSize(width: 1000, height: 640))
        lastDesktopCenterWorld = world; lastDesktopCenterBounds = bounds
        onDesktopCenterPlane?(projection)
    }

    private func updateDesktopStatusPlane(frame: HUDSourceWatchFrameBuilder.Frame, camera: HUDSourceWatchCamera.Frame) {
        guard onDesktopStatusPlane != nil, let id = desktopStatusID, let node = frame.node(id), let rect = node.rect else { return }
        // The source banner remains hidden; only its authored transform anchors
        // desktop status. Its game artwork/controllers are never activated.
        let world = camera.worldRoot * node.worldMatrix
        if let previous = lastDesktopStatusProjection,
           previous.world == world && previous.rect == rect && previous.bounds == bounds { return }
        let x = rect.origin.x, y = rect.origin.y, w = rect.size.x, h = rect.size.y
        let corners = [SIMD3(x,y+h,0), SIMD3(x+w,y+h,0), SIMD3(x+w,y,0), SIMD3(x,y,0)].compactMap {
            camera.camera.project($0, world: world, viewport: bounds)?.point
        }
        guard corners.count == 4, w > 0, h > 0,
              abs((corners[1].x-corners[0].x)*(corners[3].y-corners[0].y)
                  - (corners[1].y-corners[0].y)*(corners[3].x-corners[0].x)) > 0.001 else { return }
        lastDesktopStatusProjection = (world, rect, bounds)
        onDesktopStatusPlane?(Self.projectiveTextTransform(corners, size: CGSize(width: 528.28, height: 122)))
    }

    private func updateDesktopLabels(frame: HUDSourceWatchFrameBuilder.Frame, camera: HUDSourceWatchCamera.Frame) {
        let animated = playback.phase != .visible || buttonAnimation.requiresFrames(at: now) || selectableColor.requiresFrames(at: now)
        if !animated, !desktopProjectionWasAnimating, let prior = lastDesktopProjection,
           prior.root == camera.worldRoot, prior.bounds == bounds, prior.scroll == verticalNormalizedPosition { return }
        desktopProjectionWasAnimating = animated
        lastDesktopProjection = (camera.worldRoot, bounds, verticalNormalizedPosition)
        CATransaction.begin(); CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        let viewportPoints = [CGPoint(x: bounds.minX, y: bounds.minY), CGPoint(x: bounds.maxX, y: bounds.minY),
                              CGPoint(x: bounds.maxX, y: bounds.maxY), CGPoint(x: bounds.minX, y: bounds.maxY)]
        if desktopViewportClip?.points != viewportPoints {
            desktopViewportClip = DesktopLabelClip(points: viewportPoints, path: Self.polygonPath(viewportPoints))
        }
        let viewportClip = desktopViewportClip!
        let textScale = max(2, window?.backingScaleFactor ?? 2)
        // Labels and icons share their button's masks. Project that exact mask
        // chain once per frame, and keep its immutable path when unchanged.
        var frameClips: [HUDSourceID: DesktopLabelClip] = [:]
        func clip(for button: HUDSourceID) -> DesktopLabelClip {
            if let existing = frameClips[button] { return existing }
            let points = desktopClipPolygon(button: button, frame: frame, camera: camera.camera)
            let result: DesktopLabelClip
            if let previous = desktopButtonClips[button], previous.points == points { result = previous }
            else if points == viewportPoints { result = viewportClip }
            else { result = DesktopLabelClip(points: points, path: points.isEmpty ? nil : Self.polygonPath(points)) }
            desktopButtonClips[button] = result
            frameClips[button] = result
            return result
        }
        func present(_ container: CALayer, mask: CAShapeLayer, clip: DesktopLabelClip, opacity: Float) {
            if container.frame != bounds { container.frame = bounds }
            if container.isHidden { container.isHidden = false }
            if mask.frame != bounds { mask.frame = bounds }
            if mask.path !== clip.path { mask.path = clip.path }
            if container.opacity != opacity { container.opacity = opacity }
        }
        for (id, layers) in desktopLabels {
            guard let node = frame.node(id), node.activeInHierarchy, let rect = node.rect,
                  let button = desktopLabelButtons[id], actionsByID[button] != nil else {
                if !layers.container.isHidden { layers.container.isHidden = true }
                continue
            }
            let world = simd_mul(camera.worldRoot, node.worldMatrix)
            let area = desktopCaptionSize(CGSize(width: rect.size.x, height: rect.size.y), target: actionsByID[button]?.target)
            let width = Double(area.width), height = Double(area.height)
            let x = rect.origin.x + (rect.size.x - width) / 2, y = rect.origin.y + (rect.size.y - height) / 2
            let corners = [SIMD3(x, y + height, 0), SIMD3(x + width, y + height, 0),
                           SIMD3(x + width, y, 0), SIMD3(x, y, 0)].compactMap {
                camera.camera.project($0, world: world, viewport: bounds)?.point
            }
            guard corners.count == 4, width > 0, height > 0 else {
                if !layers.container.isHidden { layers.container.isHidden = true }
                continue
            }
            let projectedClip = desktopProfileLabelIDs.contains(id) ? viewportClip : clip(for: button)
            guard projectedClip.path != nil else {
                if !layers.container.isHidden { layers.container.isHidden = true }
                continue
            }
            present(layers.container, mask: layers.clip, clip: projectedClip, opacity: Float(frame.inheritedAlpha[id] ?? 1))
            let textBounds = CGRect(x: 0, y: 0, width: width, height: height)
            if layers.text.bounds != textBounds { layers.text.bounds = textBounds }
            if layers.text.contentsScale != textScale { layers.text.contentsScale = textScale }
            layers.text.transform = Self.projectiveTextTransform(corners, size: CGSize(width: width, height: height))
        }
        for (buttonID, iconID) in desktopIconIDs {
            guard let layers = desktopIcons[iconID] else { continue }
            guard frame.node(buttonID)?.activeInHierarchy == true, actionsByID[buttonID] != nil,
                  let node = frame.node(iconID), let rect = node.rect else {
                if !layers.container.isHidden { layers.container.isHidden = true }
                continue
            }
            let world = simd_mul(camera.worldRoot, node.worldMatrix)
            let right = desktopRightButtonIDs.contains(buttonID)
            let w = right ? 80.0 : rect.size.x, h = right ? 80.0 : rect.size.y
            let x = rect.origin.x + (rect.size.x - w) / 2, y = rect.origin.y + (rect.size.y - h) / 2
            let corners = [SIMD3(x,y+h,0), SIMD3(x+w,y+h,0), SIMD3(x+w,y,0), SIMD3(x,y,0)].compactMap {
                camera.camera.project($0, world: world, viewport: bounds)?.point
            }
            guard corners.count == 4 else {
                if !layers.container.isHidden { layers.container.isHidden = true }
                continue
            }
            let projectedClip = clip(for: buttonID)
            guard projectedClip.path != nil else {
                if !layers.container.isHidden { layers.container.isHidden = true }
                continue
            }
            present(layers.container, mask: layers.clip, clip: projectedClip, opacity: Float(frame.inheritedAlpha[iconID] ?? 1))
            layers.content.transform = Self.projectiveTextTransform(corners, size: CGSize(width: 32, height: 32))
        }
    }

    func renderDesktopLabels(in context: CGContext) {
        // CALayer.render flattens perspective transforms. Rasterize only the
        // local content, then explicitly project its image for exported PNGs.
        // On screen Core Animation continues to use the exact homography.
        for layers in desktopLabels.values where !layers.container.isHidden {
            // CATextLayer inherits the flipped AppKit host when drawing its
            // glyphs. Detached probes have the opposite raster convention.
            Self.renderProjectedContent(layers.text, opacity: layers.container.opacity, clip: layers.clip.path,
                                        flippedRaster: layers.text.contentsAreFlipped(), in: context)
        }
        for layers in desktopIcons.values where !layers.container.isHidden {
            Self.renderProjectedContent(layers.content, opacity: layers.container.opacity, clip: layers.clip.path,
                                        flippedRaster: layers.image.isHidden, in: context)
        }
    }

    static func renderProjectedContent(_ content: CALayer, opacity: Float, clip: CGPath?,
                                       flippedRaster: Bool = false, projection: CATransform3D? = nil,
                                       rasterBounds: CGRect? = nil, subdivisions: Int = 4, in context: CGContext) {
        let region = rasterBounds ?? CGRect(origin: .zero, size: content.bounds.size)
        let size = region.size
        let scale: CGFloat = min(3, 4096 / max(1, max(size.width, size.height)))
        guard size.width > 0, size.height > 0, opacity > 0,
              let raster = CGContext(data: nil, width: max(1, Int(ceil(size.width * scale))),
                height: max(1, Int(ceil(size.height * scale))), bitsPerComponent: 8, bytesPerRow: 0,
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return }
        if flippedRaster { raster.translateBy(x: 0, y: CGFloat(raster.height)); raster.scaleBy(x: scale, y: -scale) }
        else { raster.scaleBy(x: scale, y: scale) }
        raster.translateBy(x: -region.minX, y: -region.minY)
        content.render(in: raster)
        guard let image = raster.makeImage() else { return }
        let t = CATransform3DConcat(CATransform3DMakeTranslation(region.minX, region.minY, 0), projection ?? content.transform)
        func project(_ p: CGPoint) -> CGPoint {
            let w = p.x * t.m14 + p.y * t.m24 + t.m44
            return CGPoint(x: (p.x * t.m11 + p.y * t.m21 + t.m41) / w,
                           y: (p.x * t.m12 + p.y * t.m22 + t.m42) / w)
        }
        func triangle(_ a: CGPoint, _ b: CGPoint, _ c: CGPoint) {
            let p = project(a), q = project(b), r = project(c)
            let x1 = b.x-a.x, y1 = b.y-a.y, x2 = c.x-a.x, y2 = c.y-a.y
            let determinant = x1*y2-x2*y1
            guard abs(determinant) > 0.000001 else { return }
            let dx1 = q.x-p.x, dy1 = q.y-p.y, dx2 = r.x-p.x, dy2 = r.y-p.y
            let m11 = (dx1*y2-dx2*y1)/determinant, m12 = (dy1*y2-dy2*y1)/determinant
            let m21 = (x1*dx2-x2*dx1)/determinant, m22 = (x1*dy2-x2*dy1)/determinant
            context.saveGState()
            context.addPath(polygonPath([p,q,r])); context.clip()
            context.concatenate(CGAffineTransform(a: m11, b: m12, c: m21, d: m22,
                tx: p.x-m11*a.x-m21*a.y, ty: p.y-m12*a.x-m22*a.y))
            context.translateBy(x: 0, y: size.height); context.scaleBy(x: 1, y: -1)
            context.draw(image, in: CGRect(origin: .zero, size: size))
            context.restoreGState()
        }
        context.saveGState()
        if let clip { context.addPath(clip); context.clip() }
        context.setAlpha(CGFloat(opacity)); context.setShouldAntialias(false); context.interpolationQuality = .high
        let divisions = CGFloat(max(1, subdivisions))
        for row in 0..<max(1, subdivisions) { for column in 0..<max(1, subdivisions) {
            let x = CGFloat(column) * size.width / divisions, y = CGFloat(row) * size.height / divisions
            let a = CGPoint(x: x, y: y), b = CGPoint(x: x+size.width/divisions, y: y)
            let c = CGPoint(x: x+size.width/divisions, y: y+size.height/divisions), d = CGPoint(x: x, y: y+size.height/divisions)
            triangle(a,b,c); triangle(a,c,d)
        } }
        context.restoreGState()
    }

    private func desktopClipPolygon(button: HUDSourceID, frame: HUDSourceWatchFrameBuilder.Frame,
                                    camera: HUDSourceCamera) -> [CGPoint] {
        var result = [CGPoint(x: bounds.minX, y: bounds.minY), CGPoint(x: bounds.maxX, y: bounds.minY),
                      CGPoint(x: bounds.maxX, y: bounds.maxY), CGPoint(x: bounds.minX, y: bounds.maxY)]
        guard let hit = frame.hits.first(where: { $0.buttonID == button }) else { return [] }
        for mask in hit.masks {
            let clip = mask.rect.corners.compactMap { camera.project($0, world: mask.world, viewport: bounds)?.point }
            guard clip.count == 4 else { return [] }
            result = Self.clipPolygon(result, to: clip)
        }
        return result
    }

    /// Projected masks are convex quadrilaterals. Intersect the actual tilted
    /// edges, rather than their larger axis-aligned bounding rectangle.
    private static func clipPolygon(_ subject: [CGPoint], to clip: [CGPoint]) -> [CGPoint] {
        func cross(_ a: CGPoint, _ b: CGPoint, _ p: CGPoint) -> CGFloat {
            (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)
        }
        let area = clip.indices.reduce(CGFloat(0)) { sum, i in
            let a = clip[i], b = clip[(i + 1) % clip.count]
            return sum + a.x * b.y - b.x * a.y
        }
        let orientation: CGFloat = area >= 0 ? 1 : -1
        var output = subject
        for i in clip.indices {
            let a = clip[i], b = clip[(i + 1) % clip.count], input = output
            output = []; guard var previous = input.last else { return [] }
            var previousDistance = orientation * cross(a, b, previous)
            for current in input {
                let distance = orientation * cross(a, b, current)
                if (distance >= 0) != (previousDistance >= 0) {
                    let fraction = previousDistance / (previousDistance - distance)
                    output.append(CGPoint(x: previous.x + (current.x - previous.x) * fraction,
                                          y: previous.y + (current.y - previous.y) * fraction))
                }
                if distance >= 0 { output.append(current) }
                previous = current; previousDistance = distance
            }
        }
        return output
    }

    private static func polygonPath(_ points: [CGPoint]) -> CGPath {
        let path = CGMutablePath()
        if let first = points.first { path.move(to: first); points.dropFirst().forEach { path.addLine(to: $0) }; path.closeSubpath() }
        return path
    }

    private static func projectiveTextTransform(_ p: [CGPoint], size: CGSize) -> CATransform3D {
        let dx1 = p[1].x-p[2].x, dx2 = p[3].x-p[2].x
        let dy1 = p[1].y-p[2].y, dy2 = p[3].y-p[2].y
        let sx = p[0].x-p[1].x+p[2].x-p[3].x, sy = p[0].y-p[1].y+p[2].y-p[3].y
        let determinant = dx1*dy2-dx2*dy1
        let g = abs(determinant) > 0.000001 ? (sx*dy2-dx2*sy)/determinant : 0
        let h = abs(determinant) > 0.000001 ? (dx1*sy-sx*dy1)/determinant : 0
        var t = CATransform3DIdentity
        t.m11 = (p[1].x-p[0].x+g*p[1].x)/size.width
        t.m12 = (p[1].y-p[0].y+g*p[1].y)/size.width
        t.m21 = (p[3].x-p[0].x+h*p[3].x)/size.height
        t.m22 = (p[3].y-p[0].y+h*p[3].y)/size.height
        t.m41 = p[0].x; t.m42 = p[0].y
        t.m14 = g/size.width; t.m24 = h/size.height
        return t
    }

    private func accessibilityHitIDs(for id: HUDSourceID) -> Set<HUDSourceID> {
        if let card = document.desktopProfileCard, id == card.scene.rootID { return card.buttonIDs }
        return [id]
    }

    private func updateAccessibilityVisibility(_ id: HUDSourceID, visible: Bool) {
        guard let element = accessibilityButtons[id] else { return }
        if lastAccessibilityHidden[id] != !visible {
            element.setAccessibilityHidden(!visible); lastAccessibilityHidden[id] = !visible
        }
        let enabled = visible && inputEnabled
        if lastAccessibilityEnabled[id] != enabled {
            element.setAccessibilityEnabled(enabled); lastAccessibilityEnabled[id] = enabled
        }
    }

    override func accessibilityChildren() -> [Any]? {
        prepareCurrentAccessibilityGeometry()
        return super.accessibilityChildren()
    }

    override func accessibilityHitTest(_ point: NSPoint) -> Any? {
        prepareCurrentAccessibilityGeometry()
        return super.accessibilityHitTest(point)
    }

    private func prepareCurrentAccessibilityGeometry() {
        guard desktopMode, !preparingAccessibilityGeometry else { return }
        let visible = !isHiddenOrHasHiddenAncestor && playback.phase != .concealed && window?.isVisible == true
        let key = AccessibilityQueryKey(frame: renderedFrameCount, bounds: bounds, windowFrame: window?.frame,
            input: inputEnabled, visible: visible)
        guard lastAccessibilityQuery != key else { return }
        preparingAccessibilityGeometry = true
        defer { preparingAccessibilityGeometry = false }
        lastAccessibilityQuery = key
        guard visible, let frame = renderedFrame, let camera = renderedCamera else {
            for id in accessibilityButtons.keys { updateAccessibilityVisibility(id, visible: false) }
            for (direction, element) in desktopScrollButtons {
                element.setAccessibilityHidden(true); element.setAccessibilityEnabled(false)
                desktopScrollHidden[direction] = true; desktopScrollEnabled[direction] = false
            }
            logoAccessibility.setAccessibilityHidden(true); logoAccessibility.setAccessibilityEnabled(false)
            lastAccessibilityGeometry = nil
            return
        }
        updateAccessibility(frame: frame, camera: camera.camera)
    }

    /// Compare queried element geometry with the original complete projection.
    /// Used after pointer motion/scroll by the isolated integration fixtures.
    @discardableResult
    func verifyCurrentAccessibilityGeometryForVerification() throws -> Int {
        precondition(CommandLine.arguments.contains("--ui-test"))
        guard desktopMode, let frame = renderedFrame, let camera = renderedCamera else {
            throw HUDSourceError.invalid("Accessibility verification needs a visible desktop source frame")
        }
        struct State: Equatable { let frame: CGRect; let hidden: Bool; let enabled: Bool }
        let elements = Array(accessibilityButtons.values) + Array(desktopScrollButtons.values) + [logoAccessibility]
        func states() -> [State] {
            elements.map { State(frame: $0.accessibilityFrame(), hidden: $0.isAccessibilityHidden(), enabled: $0.isAccessibilityEnabled()) }
        }
        lastAccessibilityQuery = nil
        let queried = states()
        lastAccessibilityGeometry = nil
        updateAccessibility(frame: frame, camera: camera.camera)
        guard queried == states() else {
            throw HUDSourceError.invalid("Queried accessibility geometry differs from current full projection")
        }
        return elements.count
    }

    private func updateAccessibility(frame: HUDSourceWatchFrameBuilder.Frame, camera: HUDSourceCamera) {
        guard let window else { return }
        if desktopMode, let root = renderedCamera?.worldRoot {
            let animated = playback.phase != .visible || buttonAnimation.requiresFrames(at: now)
            if !animated, !accessibilityGeometryWasAnimating, let prior = lastAccessibilityGeometry,
               prior.root == root, prior.bounds == bounds, prior.windowFrame == window.frame,
               prior.scroll == verticalNormalizedPosition, prior.input == inputEnabled,
               prior.presentation == frameBuilder.presentationRevision { return }
            accessibilityGeometryWasAnimating = animated
            lastAccessibilityGeometry = (root, bounds, window.frame, verticalNormalizedPosition, inputEnabled, frameBuilder.presentationRevision)
        }
        accessibilityGeometryUpdateCount += 1
        if desktopMode {
            let polygon = industryLogoPolygon()
            let rect = Self.polygonPath(polygon).boundingBoxOfPath
            logoAccessibility.setAccessibilityHidden(polygon.count != 4 || isHidden)
            logoAccessibility.setAccessibilityEnabled(inputEnabled)
            if !rect.isNull { logoAccessibility.setAccessibilityFrame(window.convertToScreen(convert(rect, to: nil))) }
        }
        var scrollArea: CGRect?
        if desktopMode, let scroll = frame.layoutReport.scroll, scroll.hiddenLength > 0,
           let viewport = frame.node(scroll.viewportID), viewport.activeInHierarchy, let rect = viewport.rect,
           let root = renderedCamera?.worldRoot {
            let points = rect.corners.compactMap { camera.project($0, world: root * viewport.worldMatrix, viewport: bounds)?.point }
            if points.count == 4 {
                let x = points.map(\.x), y = points.map(\.y)
                let area = CGRect(x: x.min()!, y: y.min()!, width: x.max()! - x.min()!, height: y.max()! - y.min()!).intersection(bounds)
                if !area.isNull && !area.isEmpty { scrollArea = area }
            }
        }
        for (direction, element) in desktopScrollButtons {
            let hidden = scrollArea == nil
            if desktopScrollHidden[direction] != hidden { element.setAccessibilityHidden(hidden); desktopScrollHidden[direction] = hidden }
            let enabled = !hidden && inputEnabled && desktopScrollMotion.canScroll(direction)
            if desktopScrollEnabled[direction] != enabled { element.setAccessibilityEnabled(enabled); desktopScrollEnabled[direction] = enabled }
            guard let area = scrollArea else { continue }
            let height = min(20, area.height)
            let edge = CGRect(x: area.minX, y: direction < 0 ? area.minY : area.maxY - height, width: area.width, height: height)
            let screen = window.convertToScreen(convert(edge, to: nil))
            if desktopScrollFrames[direction] != screen { element.setAccessibilityFrame(screen); desktopScrollFrames[direction] = screen }
        }
        let viewClip = [CGPoint(x: bounds.minX, y: bounds.minY), CGPoint(x: bounds.maxX, y: bounds.minY),
                        CGPoint(x: bounds.maxX, y: bounds.maxY), CGPoint(x: bounds.minX, y: bounds.maxY)]
        for (id, element) in accessibilityButtons {
            guard !desktopMode || actionsByID[id] != nil || quitButtonIDs.contains(id) else {
                updateAccessibilityVisibility(id, visible: false); continue
            }
            let hitIDs = accessibilityHitIDs(for: id)
            let points = frame.hits.filter { hitIDs.contains($0.buttonID) }.flatMap { hit -> [CGPoint] in
                var polygon = hit.rect.corners.compactMap { camera.project($0, world: hit.world, viewport: bounds)?.point }
                guard polygon.count == 4 else { return [] }
                polygon = Self.clipPolygon(polygon, to: viewClip)
                for mask in hit.masks {
                    let clip = mask.rect.corners.compactMap { camera.project($0, world: mask.world, viewport: bounds)?.point }
                    guard clip.count == 4 else { return [] }
                    polygon = Self.clipPolygon(polygon, to: clip)
                }
                return polygon
            }
            guard let first = points.first else {
                updateAccessibilityVisibility(id, visible: false); continue
            }
            let minX = points.map(\.x).min() ?? first.x, maxX = points.map(\.x).max() ?? first.x
            let minY = points.map(\.y).min() ?? first.y, maxY = points.map(\.y).max() ?? first.y
            let rect = CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
            guard !rect.isEmpty, !rect.isNull else {
                updateAccessibilityVisibility(id, visible: false); continue
            }
            let screen = window.convertToScreen(convert(rect, to: nil))
            if lastAccessibilityFrames[id] != screen {
                element.setAccessibilityFrame(screen); lastAccessibilityFrames[id] = screen
            }
            updateAccessibilityVisibility(id, visible: true)
        }
    }

}

private final class HUDSourceWatchAccessibilityButton: NSAccessibilityElement {
    var performPress: (() -> Bool)?
    var prepareGeometry: (() -> Void)?
    override func accessibilityFrame() -> NSRect { prepareGeometry?(); return super.accessibilityFrame() }
    override func isAccessibilityHidden() -> Bool { prepareGeometry?(); return super.isAccessibilityHidden() }
    override func isAccessibilityEnabled() -> Bool { prepareGeometry?(); return super.isAccessibilityEnabled() }
    override func accessibilityPerformPress() -> Bool {
        prepareGeometry?()
        guard !super.isAccessibilityHidden(), super.isAccessibilityEnabled() else { return false }
        return performPress?() ?? false
    }
}
