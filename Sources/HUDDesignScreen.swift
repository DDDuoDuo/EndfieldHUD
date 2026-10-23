import AppKit
import QuartzCore

/// Where the redesigned centre sits on the HUD (spec §0, §1.4). The host draws
/// in a 1000 × 640 design space with the disc centre at (500, 320). The demo's
/// stage is the deployed HUD on the reference display (1470 × 956 pt at 1.15 pt
/// per design unit) captured 1920 px wide: its ring (426.5 px radius) and the
/// shell's measure the same, so one content px is 1470 / (1920 × 1.15) units.
enum HUDDesignGeometry {
    static let baseUnit: CGFloat = 1470 / (1920 * 1.15)
    static let discCentre = CGPoint(x: 500, y: 320)
    /// The box's top-left relative to the disc centre, in content px.
    static let boxOffset = CGPoint(x: -295, y: -305)

    /// The content box in design coordinates for one content px = `unit`.
    static func boxRect(unit: CGFloat) -> CGRect {
        CGRect(x: discCentre.x + boxOffset.x * unit, y: discCentre.y + boxOffset.y * unit,
               width: HUDMetric.box.width * unit, height: HUDMetric.box.height * unit)
    }
}

/// Host services shared by every redesigned module (one per HUD): rendering
/// environment, the confirm bar / toast / dim overlay on the tilted plane,
/// projection to the host view, and the privacy state of Recording mode.
final class HUDDesignContext {
    let env: HUDDesignEnvironment
    let overlay: HUDCenterOverlay
    /// Design units per content px, including the large-display adaptation.
    private(set) var unit: CGFloat = HUDDesignGeometry.baseUnit
    weak var host: NSView?
    /// Content-box rect → host view rect (bounds of its projection).
    var project: ((CGRect) -> CGRect)?
    /// Host view point → content-box point; nil when it misses the plane.
    var unproject: ((CGPoint) -> CGPoint?)?
    /// Recording mode (spec §3.26): private content is redacted.
    private(set) var recordingMode = false
    /// Menus, pop-overs, editors or drags changed: the host refreshes pointer
    /// capture, accessibility stand-ins and its parallax lock.
    var onInputStateChange: (() -> Void)?
    /// A gesture or editor that needs a still plane began.
    var onLockParallax: (() -> Void)?
    private var canvases: [ObjectIdentifier: () -> HUDDesignCanvas?] = [:]

    init(env: HUDDesignEnvironment = HUDDesignEnvironment(), overlay: HUDCenterOverlay? = nil) {
        self.env = env
        self.overlay = overlay ?? HUDCenterOverlay(env: env)
    }

    fileprivate func register(_ canvas: HUDDesignCanvas) {
        canvases[ObjectIdentifier(canvas)] = { [weak canvas] in canvas }
    }

    var registeredCanvases: [HUDDesignCanvas] { canvases.values.compactMap { $0() } }

    /// Backing scale of a box px: window backing × design scale × unit.
    func update(unit next: CGFloat, renderScale: CGFloat) {
        let unitChanged = abs(next - unit) > 0.0001
        let scale = renderScale.isFinite ? min(8, max(1, renderScale * next)) : 2
        let scaleChanged = abs(scale - env.renderScale) > 0.001
        unit = next
        guard unitChanged || scaleChanged else { return }
        env.renderScale = scale
        env.applyScale(to: overlay.layer)
        registeredCanvases.forEach { $0.renderScaleChanged() }
    }

    private var paletteGeneration = HUDColor.paletteGeneration

    /// The theme colour changed (`HUDColor.applyAccent`): the shared overlay
    /// recolours, the active canvas rebuilds its page now and every other
    /// built canvas rebuilds when it is next shown.
    func refreshPalette() {
        guard paletteGeneration != HUDColor.paletteGeneration else { return }
        paletteGeneration = HUDColor.paletteGeneration
        overlay.applyPalette()
        registeredCanvases.forEach { $0.paletteChanged() }
    }

    func setRecordingMode(_ on: Bool) {
        guard on != recordingMode else { return }
        recordingMode = on
        registeredCanvases.forEach { $0.recordingModeChanged() }
    }

    func confirm(_ request: HUDConfirmRequest, onConfirm: @escaping () -> Void, onCancel: (() -> Void)? = nil) {
        registeredCanvases.forEach { $0.closePopups() }
        overlay.confirm(request, onConfirm: onConfirm, onCancel: onCancel)
        onInputStateChange?()
    }

    func toast(_ request: HUDToastRequest, completion: (() -> Void)? = nil) {
        overlay.showToast(request, completion: completion)
    }

    /// Box rect in host view coordinates, or the rect itself without a host.
    func projected(_ rect: CGRect) -> CGRect { project?(rect) ?? rect }
}

/// One page of a module: its layers in content-box px (the header occupies
/// y 0–81; the body starts at `HUDMetric.headerHeight`), the controls it
/// routes pointer input to, the stagger order and its scroll areas.
final class HUDDesignPage {
    let layer = HUDLayer.plain("design.page")
    private(set) var controls: [HUDControl] = []
    private(set) var staggered: [CALayer] = []
    private(set) var scrollAreas: [(id: String, area: HUDScrollArea)] = []
    fileprivate var restoredOffsets: [String: CGFloat] = [:]
    /// The first tab bar of the page (Q / E cycle it).
    var tabs: HUDTabs?
    /// Esc activates this when set (the header's back button).
    var back: (() -> Void)?

    init() {
        HUDMotion.perform { layer.frame = CGRect(origin: .zero, size: HUDMetric.box) }
    }

    /// Routes input to `control` (later controls are on top).
    func add(_ control: HUDControl) { controls.append(control) }
    func add(_ controls: [HUDControl]) { self.controls.append(contentsOf: controls) }
    func remove(_ control: HUDControl) { controls.removeAll { $0 === control } }

    /// Adds `layer` to the stagger in document order (spec §1.10 `.st`).
    func stagger(_ layer: CALayer) { staggered.append(layer) }

    /// A scroll area whose offset survives in-place re-renders of the page.
    func scrollArea(id: String, frame: CGRect, in parent: CALayer? = nil) -> HUDScrollArea {
        let area = HUDScrollArea(frame: frame)
        HUDMotion.perform { (parent ?? layer).addSublayer(area.layer) }
        scrollAreas.append((id, area))
        return area
    }

    /// Call after filling a scroll area so a re-render keeps its position.
    func restoreOffset(of area: HUDScrollArea) {
        guard let id = scrollAreas.first(where: { $0.area === area })?.id, let offset = restoredOffsets[id] else { return }
        area.setOffset(offset)
    }
}

/// The redesigned module base (spec §1.10–§1.12, §2). A subclass builds pages
/// with `buildPage`; this class owns the content box, header, page swaps,
/// menus and pop-overs, the slider bubble layer, pointer routing, the keyboard
/// precedence and accessibility. Coordinates are content-box px.
class HUDDesignCanvas: NSObject, HUDModuleContentFactory, HUDDirectionalContent {
    let module: HUDModule
    let context: HUDDesignContext
    var env: HUDDesignEnvironment { context.env }
    /// The content box (590 × 510 px); the host scales it to design units.
    let root = HUDLayer.plain("design.root")
    /// The shared header, made with the canvas's first page: most canvases
    /// of an opening are never shown, so they build no header layers.
    private(set) lazy var header: HUDModuleHeader = {
        let header = HUDModuleHeader(env: context.env)
        HUDMotion.perform { root.insertSublayer(header.layer, below: top); header.layer.isHidden = !showsHeader }
        return header
    }()
    /// Menus and pop-overs (z 30) and the slider bubble (z 40).
    let top = HUDLayer.plain("design.top")
    private(set) var page = HUDDesignPage()
    private let router = HUDInteractionRouter()
    private(set) var isActive = false
    private(set) var renderedLanguage: AppLanguage?
    /// `HUDColor.paletteGeneration` of the installed page.
    private var renderedPalette = HUDColor.paletteGeneration
    /// The installed page was built for another language or theme colour.
    private var isStale: Bool { renderedLanguage != L10n.resolvedLanguage || renderedPalette != HUDColor.paletteGeneration }
    /// False for modules without the shared header (Now Playing).
    var showsHeader = true { didSet { HUDMotion.perform { header.layer.isHidden = !showsHeader } } }
    private var swapWork: DispatchWorkItem?
    private var leavingPage: CALayer?
    private var menuState: (menu: HUDMenu, anchor: HUDControl?)?
    private var popoverState: (popover: HUDPopover, anchor: HUDControl?, controls: [HUDControl])?
    private(set) var textFields: [HUDDesignTextField] = []
    var isTransitioning: Bool { swapWork != nil }
    var isMenuOpen: Bool { menuState != nil }
    var isPopoverOpen: Bool { popoverState != nil }
    /// A pointer press on the canvas should not reach the shell beneath it.
    var capturesPointer: Bool { menuState != nil || popoverState != nil }
    var isDragging: Bool { router.isTracking }
    var isEditingText: Bool { textFields.contains { $0.isEditing } }
    /// The host freezes parallax while this is true.
    var isInputLocked: Bool { isDragging || isEditingText }
    /// A native panel (file chooser, colour wheel) is open; the HUD stays up meanwhile.
    var isPresentingPanel: Bool { false }
    /// Presses and wheel inside this rect belong to the module rather than the
    /// shell (box px). Modules that overflow the box widen it.
    var hitBounds: CGRect { CGRect(origin: .zero, size: HUDMetric.box) }

    init(module: HUDModule, context: HUDDesignContext) {
        self.module = module; self.context = context
        super.init()
        context.register(self)
        HUDMotion.perform {
            root.bounds = CGRect(origin: .zero, size: HUDMetric.box)
            root.masksToBounds = false
            top.frame = root.bounds; top.zPosition = HUDLayerOrder.menu
            root.addSublayer(page.layer); root.addSublayer(top)
        }
    }

    // MARK: Subclass hooks

    /// Builds the current page: configure `header`, add layers to `page.layer`,
    /// register controls, stagger targets and scroll areas.
    func buildPage(_ page: HUDDesignPage) {}
    /// The module became (or stopped being) the interactive, selected module.
    func didActivate() {}
    func didDeactivate() {}
    /// Leaving the module: stop timers and loops (spec §1.10 cleanup).
    func willLeave() {}
    /// Keyboard, precedence 8 (spec §1.12). Return true when consumed.
    func handleKey(_ event: NSEvent) -> Bool { false }
    /// Keyboard, precedence 3: hotkey capture swallows keys before everything.
    func interceptKey(_ event: NSEvent) -> Bool { false }
    /// Keyboard, precedence 11: module extras after Esc and Q / E.
    func handleExtraKey(_ event: NSEvent) -> Bool { false }
    /// Pointer surfaces that are not controls (map, stage, board).
    func handlePointerDown(at point: CGPoint, event: NSEvent?) -> Bool { false }
    func handlePointerDragged(to point: CGPoint) {}
    func handlePointerUp(at point: CGPoint) {}
    func handleRightPointerDown(at point: CGPoint) -> Bool { false }
    func handlePointerMoved(at point: CGPoint?) {}
    /// Wheel before the page's scroll areas (zoom surfaces). Delta is box px, +down.
    func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool { false }
    func handleMagnify(at point: CGPoint, magnification: CGFloat) -> Bool { false }
    /// The pointer this module shows at a box point; nil keeps the HUD's own.
    func cursor(at point: CGPoint?) -> NSCursor? { nil }
    /// Recording mode changed (spec §3.26); the default re-renders in place.
    func recordingModeChanged() { if renderedLanguage != nil { rerender() } }
    /// The backing scale changed; the default re-renders in place.
    func renderScaleChanged() { if renderedLanguage != nil { rerender() } }
    /// The theme colour changed: the active module rebuilds at once, the
    /// others when next shown (`makeContent` / `setActive`).
    func paletteChanged() {
        closePopups()
        if isActive, renderedLanguage != nil { rerender() }
    }

    // MARK: HUDModuleContentFactory, HUDDirectionalContent

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        revealSettledPage()
        if isStale { rerender() }
        return root
    }

    var contentUnit: CGFloat { context.unit }

    func playEntrance(from offset: CGPoint) {
        guard !env.reducesMotion else { return }
        if showsHeader { header.playEntrance() }
        HUDMotion.stagger(page.staggered, from: offset, env: env)
    }

    func prepareToLeave() {
        closePopups()
        endEditing(commit: true)
        cancelGestures()
        settleSwap(concealed: true)
        willLeave()
    }

    // MARK: Activation

    /// Scroll offsets of pages left by sub-navigation, and those the page
    /// being navigated to restores.
    private var retainedOffsets: [String: CGFloat] = [:], pendingOffsets: [String: CGFloat] = [:]

    func setActive(_ active: Bool) {
        guard active != isActive else { return }
        isActive = active
        if active {
            revealSettledPage()
            if isStale { rerender() }
            didActivate()
        } else {
            closePopups(); endEditing(commit: true); cancelGestures(); settleSwap(concealed: true)
            retainedOffsets = [:]
            didDeactivate()
        }
        context.onInputStateChange?()
    }

    // MARK: Rendering

    /// In-place re-render (spec §1.10): no entrance, scroll offsets kept.
    /// During a sub-page swap the page has already left: the re-rendered one
    /// stays hidden until the swap lands its own page with the entrance.
    func rerender() {
        install(makePage(restoring: currentOffsets))
        if swapWork != nil { HUDMotion.hold(page.layer, key: Self.holdKey) }
    }

    private var currentOffsets: [String: CGFloat] {
        Dictionary(page.scrollAreas.map { ($0.id, $0.area.offset) }, uniquingKeysWith: { a, _ in a })
    }

    /// Sub-navigation (spec §1.10 `subnav`): the box content leaves .13 s
    /// against the vector, the next page renders after 130 ms and enters with
    /// the header and stagger. dx/dy: +1 forward / below, −1 back / above.
    func navigate(dx: CGFloat, dy: CGFloat = 0) {
        let offset = HUDShellPosition.subpageOffset(dx: dx, dy: dy)
        closePopups(); endEditing(commit: true); cancelGestures()
        // Going back (dx < 0) returns each scroll area to where it was left.
        retainedOffsets.merge(currentOffsets) { _, new in new }
        pendingOffsets = dx < 0 ? retainedOffsets : [:]
        guard !env.reducesMotion, root.superlayer != nil else {
            if let work = swapWork { work.cancel(); swapWork = nil; finishLeave() }
            install(makePage(restoring: pendingOffsets)); return
        }
        // A swap in flight (Q / E pressed again) is retargeted and lands on
        // time: settling it would show the next page at full opacity only to
        // send it out again.
        if let work = swapWork { work.cancel() } else {
            let old = page.layer
            leavingPage = old
            HUDMotion.leave(old, toward: offset, env: env, key: "hud.page.leave")
            if showsHeader { HUDMotion.leave(header.layer, toward: offset, env: env, key: "hud.header.leave") }
            router.set([])
            swapDeadline = .now() + HUDDuration.leave
        }
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            // One commit (see HUDModuleContent's swap): the header restored by
            // finishLeave and the new page must not show before their entrance.
            CATransaction.begin()
            self.swapWork = nil
            self.finishLeave()
            self.install(self.makePage(restoring: self.pendingOffsets))
            self.playEntrance(from: offset)
            self.context.onInputStateChange?()
            CATransaction.commit()
        }
        swapWork = work
        DispatchQueue.main.asyncAfter(deadline: swapDeadline, execute: work)
    }

    /// Completes a pending sub-navigation at once. `concealed` is for a module
    /// that leaves or stops being active: the new page and the header had
    /// already left, so they stay hidden until the module shows again
    /// (makeContent, setActive) instead of popping in while it leaves.
    func settleSwap(concealed: Bool = false) {
        guard let work = swapWork else { return }
        work.cancel(); swapWork = nil
        CATransaction.begin()
        finishLeave()
        install(makePage(restoring: pendingOffsets))
        if concealed {
            HUDMotion.hold(page.layer, key: Self.holdKey)
            if showsHeader { HUDMotion.hold(header.layer, key: Self.holdKey) }
        }
        CATransaction.commit()
    }

    private static let holdKey = "hud.page.hold"
    private var swapDeadline = DispatchTime.now()

    private func revealSettledPage() {
        guard swapWork == nil else { return }
        HUDMotion.perform {
            page.layer.removeAnimation(forKey: Self.holdKey)
            if showsHeader { header.layer.removeAnimation(forKey: Self.holdKey) }
        }
    }

    private func finishLeave() {
        HUDMotion.perform {
            leavingPage?.removeAnimation(forKey: "hud.page.leave")
            leavingPage?.removeFromSuperlayer()
            header.layer.removeAnimation(forKey: "hud.header.leave")
        }
        leavingPage = nil
    }

    private func makePage(restoring offsets: [String: CGFloat]) -> HUDDesignPage {
        let next = HUDDesignPage()
        next.restoredOffsets = offsets
        textFields.removeAll { !$0.isEditing }
        buildPage(next)
        for (id, area) in next.scrollAreas { if let offset = offsets[id] { area.setOffset(offset) } }
        return next
    }

    private func install(_ next: HUDDesignPage) {
        renderedLanguage = L10n.resolvedLanguage
        if renderedPalette != HUDColor.paletteGeneration {
            renderedPalette = HUDColor.paletteGeneration
            if showsHeader { header.applyPalette() }
        }
        let old = page
        page = next
        HUDMotion.perform {
            if old.layer !== leavingPage { old.layer.removeFromSuperlayer() }
            root.insertSublayer(next.layer, at: 0)
        }
        for control in old.controls where !next.controls.contains(where: { $0 === control }) { control.clearInteractionState() }
        env.applyScale(to: next.layer)
        env.applyScale(to: header.layer)
        refreshRouter()
        context.onInputStateChange?()
    }

    func refreshRouter() {
        var controls = page.controls
        if showsHeader { controls.insert(contentsOf: header.controls, at: 0) }
        controls += textFields
        if let state = popoverState { controls += state.controls + (state.popover.close.map { [$0] } ?? []) }
        if let state = menuState { controls += state.menu.controls }
        router.set(controls)
    }

    // MARK: Menus and pop-overs

    /// Opens `items` under `anchor` (spec §2.6). Picking calls `onPick` and closes.
    @discardableResult
    func openMenu(id: String, items: [HUDMenu.Item], anchor: HUDControl, minimumWidth: CGFloat? = nil,
                  onPick: @escaping (HUDMenu.Item) -> Void) -> HUDMenu {
        let anchorRect = anchor.rect(in: root)
        return openMenu(id: id, items: items, anchorRect: anchorRect, anchor: anchor, minimumWidth: minimumWidth, onPick: onPick)
    }

    /// A context menu at a point or rect without a dropdown field.
    @discardableResult
    func openMenu(id: String, items: [HUDMenu.Item], anchorRect: CGRect, anchor: HUDControl? = nil,
                  minimumWidth: CGFloat? = nil, onPick: @escaping (HUDMenu.Item) -> Void) -> HUDMenu {
        closePopups()
        let menu = HUDMenu(id: id, items: items, minimumWidth: minimumWidth ?? anchorRect.width, env: env)
        menu.place(anchor: anchorRect, in: HUDMetric.box)
        menu.onPick = { [weak self, weak menu] item in
            guard let self, let menu, self.menuState?.menu === menu else { return }
            self.closeMenu()
            onPick(item)
        }
        menu.onClose = { [weak self] in self?.closeMenu() }
        HUDMotion.perform { top.addSublayer(menu.layer) }
        (anchor as? HUDDropdown)?.isOpen = true
        menuState = (menu, anchor)
        menu.playEntrance()
        refreshRouter()
        context.onInputStateChange?()
        return menu
    }

    func closeMenu() {
        guard let state = menuState else { return }
        menuState = nil
        (state.anchor as? HUDDropdown)?.isOpen = false
        state.menu.controls.forEach { $0.clearInteractionState() }
        HUDMotion.perform { state.menu.layer.removeFromSuperlayer() }
        refreshRouter()
        context.onInputStateChange?()
    }

    /// Shows a pop-over with its own controls (spec §2.17).
    func openPopover(_ popover: HUDPopover, anchor: HUDControl?, anchorRect: CGRect? = nil, controls: [HUDControl]) {
        closePopups()
        popover.place(anchor: anchorRect ?? anchor?.rect(in: root) ?? .zero, in: HUDMetric.box)
        popover.onClose = { [weak self, weak popover] in
            guard let self, self.popoverState?.popover === popover else { return }
            self.closePopover()
        }
        HUDMotion.perform { top.addSublayer(popover.layer) }
        env.applyScale(to: popover.layer)
        popoverState = (popover, anchor, controls)
        popover.playEntrance()
        refreshRouter()
        context.onInputStateChange?()
    }

    func closePopover() {
        guard let state = popoverState else { return }
        popoverState = nil
        (state.controls + (state.popover.close.map { [$0] } ?? [])).forEach { $0.clearInteractionState() }
        HUDMotion.perform { state.popover.layer.removeFromSuperlayer() }
        refreshRouter()
        context.onInputStateChange?()
    }

    func closePopups() { closeMenu(); closePopover() }

    // MARK: Text fields

    func register(_ field: HUDDesignTextField) {
        if !textFields.contains(where: { $0 === field }) { textFields.append(field) }
        field.canvas = self
    }

    func endEditing(commit: Bool) { textFields.forEach { $0.endEditing(commit: commit) } }

    /// The native editor under a host view point, for the host's hitTest.
    func textView(atHost point: CGPoint) -> NSView? {
        for field in textFields where field.isEditing {
            if let view = field.editorHitTest(host: point) { return view }
        }
        return nil
    }

    func refreshTextFieldProjection() { textFields.forEach { $0.refreshProjection() } }

    // MARK: Pointer input (content-box px)

    @discardableResult
    func pointerDown(at point: CGPoint, event: NSEvent? = nil) -> Bool {
        guard isActive, swapWork == nil else { return false }
        // The first press outside an open menu or pop-over only closes it; a
        // press on its anchor closes it too (the anchor toggles).
        if let state = menuState {
            if state.menu.layer.frame.contains(point) { router.pointerDown(at: point, in: root) } else { closeMenu() }
            return true
        }
        if let state = popoverState {
            if state.popover.layer.frame.contains(point) { router.pointerDown(at: point, in: root) } else { closePopover() }
            return true
        }
        let editing = textFields.filter { $0.isEditing }
        if !editing.isEmpty, !editing.contains(where: { $0.rect(in: root).contains(point) }) {
            editing.forEach { $0.endEditing(commit: true) }
        }
        if router.pointerDown(at: point, in: root) {
            if router.isTracking { context.onLockParallax?() }
            context.onInputStateChange?()
            return true
        }
        if handlePointerDown(at: point, event: event) { context.onInputStateChange?(); return true }
        return false
    }

    func pointerDragged(to point: CGPoint) {
        guard isActive else { return }
        if router.isTracking { router.pointerDragged(to: point, in: root) } else { handlePointerDragged(to: point) }
    }

    func pointerUp(at point: CGPoint) {
        guard isActive else { return }
        if router.isTracking { router.pointerUp(at: point, in: root); context.onInputStateChange?() }
        else { handlePointerUp(at: point) }
    }

    func rightPointerDown(at point: CGPoint) -> Bool {
        guard isActive, swapWork == nil else { return false }
        if capturesPointer { closePopups(); return true }
        return handleRightPointerDown(at: point)
    }

    func pointerMoved(at point: CGPoint?) { if isActive { handlePointerMoved(at: point) } }

    /// An enabled control is drawn under this point; the host gives such a
    /// press to the module before the shell's cards and navigation beneath it.
    func containsControl(atBox point: CGPoint) -> Bool {
        isActive && swapWork == nil && router.control(at: point, in: root) != nil
    }

    /// Wheel: an open menu, then the module's zoom surfaces, then scroll areas.
    func scroll(at point: CGPoint, delta: CGPoint, event: NSEvent? = nil) -> Bool {
        guard isActive else { return false }
        if let state = menuState {
            if state.menu.layer.frame.contains(point) { state.menu.scroll(by: delta.y) }
            return true
        }
        if popoverState != nil { return true }
        if handleScroll(at: point, delta: delta, event: event) { return true }
        for (_, area) in page.scrollAreas.reversed() where area.layer.isVisible(in: root) {
            let local = area.layer.convert(point, from: root)
            if area.layer.bounds.contains(local) { return area.scroll(by: delta.y) || true }
        }
        return false
    }

    func magnify(at point: CGPoint, magnification: CGFloat) -> Bool {
        guard isActive, !capturesPointer else { return false }
        return handleMagnify(at: point, magnification: magnification)
    }

    private func cancelGestures() {
        for control in router.controls { (control as? HUDSlider)?.cancelDrag(); control.clearInteractionState() }
        router.cancel()
    }

    // MARK: Keyboard (spec §1.12; the host handles the boot log and confirm bar first)

    func keyDown(_ event: NSEvent) -> Bool {
        guard isActive else { return false }
        if interceptKey(event) { return true }
        if let state = menuState { return state.menu.handleKey(keyCode: event.keyCode) }
        if popoverState != nil {
            if event.keyCode == 53 { closePopover() }
            return event.keyCode == 53
        }
        if isEditingText { return false }
        if handleKey(event) { return true }
        let flags = event.modifierFlags.intersection([.command, .control, .option])
        guard flags.isEmpty else { return false }
        if event.keyCode == 53 {
            if let back = page.back { back(); return true }
            return false
        }
        if let tabs = page.tabs, let key = event.charactersIgnoringModifiers?.lowercased() {
            if key == "q" { tabs.cycle(-1); return true }
            if key == "e" { tabs.cycle(1); return true }
        }
        return handleExtraKey(event)
    }

    // MARK: Accessibility

    var accessibilityItems: [HUDAccessibilityItem] {
        guard isActive, swapWork == nil else { return [] }
        return router.accessibilityItems(in: root)
    }

    /// Accessibility stand-in callbacks: a button press or slider value.
    func performAccessibilityAction(_ id: String) {
        guard let control = router.controls.first(where: { $0.id == id }) else { return }
        control.activate()
    }

    func adjustAccessibilitySlider(_ id: String, value: Double) {
        guard let slider = router.controls.first(where: { $0.id == id }) as? HUDSlider else { return }
        let before = slider.value
        slider.setValue(value)
        if slider.value != before { slider.onChange?(slider.value, true) }
    }

    // MARK: Shared overlay

    func confirm(_ request: HUDConfirmRequest, onConfirm: @escaping () -> Void, onCancel: (() -> Void)? = nil) {
        endEditing(commit: true); cancelGestures()
        context.confirm(request, onConfirm: onConfirm, onCancel: onCancel)
    }

    func toast(_ request: HUDToastRequest, completion: (() -> Void)? = nil) { context.toast(request, completion: completion) }

    /// The result toast (Appendix C). Outside English it keeps the game's
    /// bilingual pattern: the UI-language title over the English caption.
    /// English UIs show the English title alone (no other language appears in
    /// English). `detail` (a key combination, a file name) is the caption.
    func toast(icon: HUDIcon, _ english: L10n.Text, _ chinese: L10n.Text, detail: String? = nil) {
        let isEnglish = L10n.resolvedLanguage == .english
        let caption = detail ?? (isEnglish ? "" : english.value)
        context.toast(HUDToastRequest(icon: icon, title: L10n.text(english, chinese), caption: caption))
    }

    // MARK: Helpers for subclasses

    /// The header's back button, wired to `action` and Esc.
    func wireBack(_ page: HUDDesignPage, _ action: @escaping () -> Void) {
        header.back?.onActivate = action
        page.back = action
    }

    /// Connects a slider's floating bubble to the top layer.
    func hostBubble(_ slider: HUDSlider) { slider.bubbleHost = top }
}

extension HUDModuleTitle {
    /// A title from its source pair, localised for the current UI language.
    init(_ english: L10n.Text, _ simplifiedChinese: L10n.Text) {
        self.init(english: english.value, simplifiedChinese: simplifiedChinese.value,
                  localized: L10n.text(english, simplifiedChinese))
    }
}

extension CALayer {
    /// The point (in `root`) is inside every clipping ancestor up to `root`.
    func isUnclipped(_ point: CGPoint, in root: CALayer) -> Bool {
        var layer: CALayer? = superlayer
        while let current = layer, current !== root {
            if current.masksToBounds, !current.bounds.contains(current.convert(point, from: root)) { return false }
            layer = current.superlayer
        }
        return true
    }
}
