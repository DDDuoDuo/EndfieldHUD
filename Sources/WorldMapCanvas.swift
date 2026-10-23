import AppKit
import QuartzCore

/// The Map module of the redesign (spec §3.18): the live world map fills the
/// whole centre backdrop, the disc Personal ID's art fills, and fades out
/// with its ramp; it runs under the header (behind a subtle scrim) and under
/// the source shell's bottom plates (the host cuts their contours out of it).
/// Presses, the wheel and pinch count inside v1.2.0's round map, a circle
/// concentric with the disc at the shell's inner rim. The zoom column, the
/// centre placement button, the zoom readout and the coordinates box of the
/// selected pin sit where v1.2.0 had its controls; the pins are v1.2.0's
/// yellow, green and player markers. The map names no countries; the painter
/// still highlights the one under the centre. Geography, pins and camera
/// live in WorldMapSurface; this canvas places the controls and routes input.
/// Camera events move retained layers.
final class WorldMapCanvas: HUDDesignCanvas {
    var onPinStyleChanged: ((MapPinStyle) -> Void)?
    var onRecenter: (() -> Void)?
    let surface: WorldMapSurface
    private let storeAvailable: Bool
    /// Why the map archive could not be opened; shown once as a notice.
    private let message: String?
    private var messageShown = false
    private var zoomIn: HUDIconButton?, zoomOut: HUDIconButton?, placePin: HUDIconButton?
    /// Buttons and plates drawn on the map: presses there are not map presses.
    private var chrome: [CALayer] = []
    private var readout: (plate: CALayer, text: HUDTextLayer)?
    private var coordinates: (plate: CALayer, lines: [HUDTextLayer])?
    private var readoutWidths: [Int: CGFloat] = [:]
    private var gestureHeld = false
    private var gestureDeadline: TimeInterval = 0
    private var gestureEnd: DispatchWorkItem?

    /// The camera centre in content-box px: the backdrop disc's centre.
    static let mapCenter = HUDCentreBackdrop.centre
    /// The map square, the backdrop disc's; it overflows the box on every side.
    static let mapFrame = HUDCentreBackdrop.frame
    /// The input circle's bounding square: the module takes input there.
    static let circleFrame = CGRect(x: mapCenter.x - WorldMapSurface.radius, y: mapCenter.y - WorldMapSurface.radius,
                                    width: 2 * WorldMapSurface.radius, height: 2 * WorldMapSurface.radius)
    /// v1.2.0's map viewport (its 440 units) in content-box px: the source
    /// shell drew its centre 23 design units above the disc centre (box 295,
    /// 305). The controls keep v1.2.0's places in it.
    private static let v1Frame = CGRect(x: 295 - WorldMapGeometry.center.x * WorldMapSurface.unitScale,
                                        y: 305 - 23 / HUDDesignGeometry.baseUnit - WorldMapGeometry.center.y * WorldMapSurface.unitScale,
                                        width: WorldMapGeometry.size.width * WorldMapSurface.unitScale,
                                        height: WorldMapGeometry.size.height * WorldMapSurface.unitScale)
    /// A point of v1.2.0's map viewport (its 440 units) in content-box px.
    static func v1(_ x: CGFloat, _ y: CGFloat) -> CGPoint {
        CGPoint(x: v1Frame.minX + x * WorldMapSurface.unitScale, y: v1Frame.minY + y * WorldMapSurface.unitScale)
    }
    private static var readoutStyle: HUDTextStyle { HUDTextStyle(size: 11, weight: .bold, color: HUDColor.yellow, tabular: true) }
    private static let coordinateStyle = HUDTextStyle(size: 11, weight: .bold, color: HUDColor.onDarkTag, lineHeight: 1.5, tabular: true)

    init(context: HUDDesignContext, store: WorldMapStore?, error: String? = nil, terrain: WorldMapTerrain? = nil,
         countries: WorldMapCountries? = nil, loadsTerrain: Bool = true) {
        storeAvailable = store != nil; message = error
        surface = WorldMapSurface(store: store, terrain: terrain, countries: countries, loadsTerrain: loadsTerrain,
                                  accent: HUDColor.yellow, reduceMotion: { [env = context.env] in env.reducesMotion },
                                  ambient: { HUDRuntimeAppearance.ambientEnabled })
        super.init(module: .map, context: context)
        surface.onCamera = { [weak self] animated in self?.cameraChanged(animated: animated) }
        surface.onPins = { [weak self] in self?.pinsChanged() }
        surface.onError = { [weak self] text in self?.toast(icon: .warn, "Notice", "提示", detail: text) }
        surface.onRecenter = { [weak self] in self?.onRecenter?() }
        surface.onPinStyleChanged = { [weak self] style in self?.onPinStyleChanged?(style) }
    }

    deinit { gestureEnd?.cancel() }

    // MARK: Host compatibility (SystemHUDView, verification)

    var layer: CALayer { root }
    var pins: [MapPin] { surface.pins }
    var viewport: WorldMapViewport { surface.camera }
    var showsPinCoordinates: Bool { surface.selectedPin != nil }
    var isRasterSettledForVerification: Bool { surface.isRasterSettledForVerification }
    /// Starts geography while the shell deploys, before input is enabled.
    func prepareForPresentation() { surface.prepareForPresentation() }
    /// Module input stopped (the host calls this for every module switch and
    /// at the end of the opening): the painter's working set is released.
    func releaseRasterWorkingSet() { if !isActive { surface.releaseWorkingSet() } }

    override var isDragging: Bool { super.isDragging || surface.isDragging }
    override var isInputLocked: Bool { super.isInputLocked || gestureHeld }
    /// The input circle overflows the content box on every side.
    override var hitBounds: CGRect { super.hitBounds.union(Self.circleFrame) }

    /// Inside the input circle (content-box px): only there do presses, the wheel and pinch count.
    static func onMap(_ point: CGPoint) -> Bool {
        WorldMapSurface.contains(CGPoint(x: point.x - mapFrame.minX, y: point.y - mapFrame.minY))
    }

    /// On the map and not on a control or plate drawn over it.
    private func onTerrain(_ point: CGPoint) -> Bool {
        Self.onMap(point) && !chrome.contains { !$0.isHidden && $0.frame.contains(point) }
    }

    // MARK: Lifecycle

    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        let content = super.makeContent(for: module, style: style)
        surface.prepareForPresentation()
        return content
    }

    override func didActivate() {
        surface.prepareForPresentation()
        surface.activate()
        cameraChanged(animated: false)
        if let message, !messageShown { messageShown = true; toast(icon: .warn, "Notice", "提示", detail: message) }
    }

    override func didDeactivate() {
        releaseGesture(commit: false)
        surface.deactivate()
    }

    override func willLeave() { didDeactivate() }

    override func renderScaleChanged() {
        surface.setContentsScale(env.renderScale)
        env.applyScale(to: root)
    }

    /// Nothing on the map is private content (spec §3.26).
    override func recordingModeChanged() {}

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        header.configure(crumbs: ["MAP"], code: "MOD-13", title: HUDModuleTitle("Map", "地图"), counter: counterText, showsBack: false)
        let well = HUDLayer.plain("map.well")
        let crosshair = HUDEmptyState.crosshair()
        let plate = HUDLayer.plain("map.zoom.plate"), text = HUDTextLayer(style: Self.readoutStyle, alignment: .center)
        text.language = HUDTextLanguage.tag()
        let coordinatePlate = HUDLayer.plain("map.coordinates")
        let lines = (0..<3).map { HUDTextLayer(style: $0 == 0 ? Self.coordinateStyle.colored(HUDColor.yellow) : Self.coordinateStyle) }
        HUDMotion.perform {
            well.frame = CGRect(origin: .zero, size: HUDMetric.box)
            // v1.2.0's dark well; it fades out with the map (the disc's mask).
            surface.layer.frame = Self.mapFrame; surface.layer.backgroundColor = HUDColor.rgb(0x1F1F1F, 0.89)
            crosshair.position = Self.mapCenter
            plate.backgroundColor = HUDColor.counterBackground; plate.addSublayer(text)
            coordinatePlate.backgroundColor = HUDColor.rgb(0x121212, 0.92)
            coordinatePlate.borderWidth = 1; coordinatePlate.borderColor = HUDColor.rgb(0x444444)
            lines.forEach(coordinatePlate.addSublayer)
            [surface.layer, crosshair, plate, coordinatePlate].forEach(well.addSublayer)
            page.layer.addSublayer(well)
        }
        // The header is drawn over the map's top: a dark veil keeps it legible on light terrain.
        surface.setHeaderScrim(bottom: HUDMetric.headerHeight - Self.mapFrame.minY)
        surface.setContentsScale(env.renderScale)
        surface.setAccent(HUDColor.yellow)
        readout = (plate, text)
        coordinates = (coordinatePlate, lines)
        // v1.2.0's controls inside the circle, as redesign buttons at its
        // centres: the zoom column on the left (the spec's 4 px gap), the
        // centre placement on the right.
        let column = Self.v1(37, 223), pitch: CGFloat = 34
        let buttons: [(String, String, HUDIcon, CGPoint, () -> Void)] = [
            ("map.zoomIn", L10n.text("Zoom in", "放大"), .plus, CGPoint(x: column.x, y: column.y - pitch), { [weak self] in self?.zoomStep(1.5) }),
            ("map.zoomOut", L10n.text("Zoom out", "缩小"), .minus, column, { [weak self] in self?.zoomStep(1 / 1.5) }),
            ("map.reset", L10n.text("Reset view", "重置视图"), .reset, CGPoint(x: column.x, y: column.y + pitch), { [weak self] in self?.resetView() }),
            ("map.addPin", L10n.text("Place pin at map center", "在地图中心放置标记"), .rec, Self.v1(403, 218), { [weak self] in self?.addCenterPin() })
        ]
        let controls = buttons.map { item -> HUDIconButton in
            let button = HUDIconButton(id: item.0, label: item.1, icon: item.2, round: item.0 == "map.addPin", env: env)
            button.onActivate = item.4
            HUDMotion.perform { button.layer.position = item.3; well.addSublayer(button.layer) }
            page.add(button)
            return button
        }
        zoomIn = controls[0]; zoomOut = controls[1]; placePin = controls[3]
        placePin?.isEnabled = canPlacePin
        chrome = controls.map(\.layer) + [plate, coordinatePlate]
        page.stagger(well)
        updateCoordinates()
        cameraChanged(animated: false)
    }

    /// v1.2.0's title and readout sat centred at the top of the circle; the
    /// readout now sits just under the header that replaced its title.
    private static let readoutTop = HUDMetric.headerHeight + 10

    private var counterText: String { HUDFormat.pad2(surface.pins.count) + " " + L10n.text("PINS", "标记") }

    // MARK: Live chrome (in place; no re-render on camera or pin changes)

    private func cameraChanged(animated: Bool) {
        zoomIn?.isEnabled = surface.canZoomIn
        zoomOut?.isEnabled = surface.canZoomOut
        guard let readout else { return }
        let value = String(format: "%.1f×", surface.camera.zoom)
        guard readout.text.text != value else { return }
        HUDMotion.perform {
            readout.text.text = value
            // Tabular figures: the width depends only on the length.
            let width = readoutWidths[value.count] ?? ceil(readout.text.preferredWidth)
            readoutWidths[value.count] = width
            readout.plate.frame = CGRect(x: Self.mapCenter.x - (width + 14) / 2, y: Self.readoutTop, width: width + 14, height: 21)
            readout.text.lineFrame = CGRect(x: 7, y: 4, width: width, height: 13)
        }
    }

    private var canPlacePin: Bool { storeAvailable && surface.pins.count < WorldMapStore.maximumPinCount }

    private func pinsChanged() {
        placePin?.isEnabled = canPlacePin
        header.setCounter(counterText)
        updateCoordinates()
        context.onInputStateChange?()
    }

    private func updateCoordinates() {
        guard let coordinates else { return }
        let pins = surface.pins
        guard let index = pins.firstIndex(where: { $0.id == surface.selectedPinID }) else {
            HUDMotion.perform { coordinates.plate.isHidden = true }; return
        }
        let pin = pins[index]
        let latitude = 90 - pin.y * 180, longitude = WorldMapStore.wrappedX(pin.x) * 360 - 180
        let texts = [L10n.text("PIN", "标记") + " " + HUDFormat.pad2(index + 1),
                     String(format: "%@ %.2f", latitude < 0 ? "S" : "N", abs(latitude)),
                     String(format: "%@ %.2f", longitude < 0 ? "W" : "E", abs(longitude))]
        HUDMotion.perform {
            zip(coordinates.lines, texts).forEach { $0.text = $1 }
            let width = ceil(coordinates.lines.map(\.preferredWidth).max() ?? 0)
            let size = CGSize(width: width + 20, height: 3 * 16.5 + 12)
            // Where v1.2.0 showed the selected pin's coordinates: centred above the bottom plates.
            coordinates.plate.frame = CGRect(x: Self.mapCenter.x - size.width / 2, y: Self.v1(0, 301).y, width: size.width, height: size.height)
            for (row, line) in coordinates.lines.enumerated() {
                line.lineFrame = CGRect(x: 10, y: 6 + CGFloat(row) * 16.5, width: width + 1, height: 16.5)
            }
            coordinates.plate.isHidden = false
        }
        env.applyScale(to: coordinates.plate)
    }

    // MARK: Actions

    private func zoomStep(_ factor: Double) {
        releaseGesture(commit: true)
        surface.zoom(at: WorldMapSurface.center, factor: factor, animated: true)
        surface.endGesture()
    }

    private func resetView() {
        releaseGesture(commit: false)
        surface.reset()
    }

    private func deletePin(_ id: UUID) {
        guard surface.removePin(id: id) else { return }
        toast(icon: .trash, "PIN DELETED", "标记已删除")
    }

    private func addCenterPin() { if canPlacePin { addPin(at: WorldMapSurface.center) } }

    private func addPin(at point: CGPoint) {
        guard storeAvailable else {
            toast(icon: .warn, "Notice", "提示", detail: message ?? L10n.text("Map", "地图")); return
        }
        surface.addPin(at: point)
    }

    private func local(_ point: CGPoint) -> CGPoint {
        CGPoint(x: point.x - Self.mapFrame.minX, y: point.y - Self.mapFrame.minY)
    }

    // MARK: Pointer, wheel and keyboard

    override func handlePointerDown(at point: CGPoint, event: NSEvent?) -> Bool {
        guard onTerrain(point) else { return false }
        releaseGesture(commit: true)
        surface.beginDrag(at: local(point))
        context.onLockParallax?()
        return true
    }

    override func handlePointerDragged(to point: CGPoint) { surface.drag(to: local(point)) }

    override func handlePointerUp(at point: CGPoint) {
        guard surface.isDragging else { return }
        if let press = surface.endDrag() {
            if let pin = surface.pin(at: press) { surface.activatePin(id: pin.id) }
            else if surface.selectedPinID != nil { surface.deselect() }
            else { surface.recenter(at: press) }
        }
        context.onInputStateChange?()
    }

    override func handleRightPointerDown(at point: CGPoint) -> Bool {
        guard onTerrain(point) else { return false }
        guard !surface.isDragging else { return true }
        let target = local(point)
        if let pin = surface.pin(at: target) { deletePin(pin.id) } else { addPin(at: target) }
        return true
    }

    /// ×1.12 per wheel notch (a trackpad's ten points count as one), at the pointer.
    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        guard Self.onMap(point) else { return false }
        let notches: Double
        if let event { notches = Double(event.scrollingDeltaY) / (event.hasPreciseScrollingDeltas ? 10 : 1) }
        else { notches = Double(-delta.y) / 12 }
        if notches.isFinite, notches != 0 {
            surface.zoom(at: local(point), factor: pow(1.12, min(6, max(-6, notches))))
            holdGesture()
        }
        if let event, !event.phase.isDisjoint(with: [.ended, .cancelled]) || !event.momentumPhase.isDisjoint(with: [.ended, .cancelled]) {
            releaseGesture(commit: true)
        }
        return true
    }

    override func handleMagnify(at point: CGPoint, magnification: CGFloat) -> Bool {
        guard Self.onMap(point) else { return false }
        let amount = Double(magnification)
        if amount.isFinite, amount != 0 {
            surface.zoom(at: local(point), factor: min(2, max(0.5, 1 + amount)))
            holdGesture()
        }
        return true
    }

    /// Arrows pan, + / − zoom, Delete removes the selected pin, Esc deselects it.
    override func handleKey(_ event: NSEvent) -> Bool {
        guard event.modifierFlags.intersection([.command, .control, .option]).isEmpty else { return false }
        let step: CGFloat = 40
        switch event.keyCode {
        case 123: surface.pan(by: CGPoint(x: step, y: 0)); holdGesture()
        case 124: surface.pan(by: CGPoint(x: -step, y: 0)); holdGesture()
        case 125: surface.pan(by: CGPoint(x: 0, y: -step)); holdGesture()
        case 126: surface.pan(by: CGPoint(x: 0, y: step)); holdGesture()
        case 24, 69: zoomStep(1.5)
        case 27, 78: zoomStep(1 / 1.5)
        case 51, 117:
            guard let id = surface.selectedPinID else { return false }
            deletePin(id)
        case 53:
            guard surface.selectedPinID != nil else { return false }
            surface.deselect()
        default: return false
        }
        return true
    }

    /// Wheel, pinch and key steps send no end event: the camera is committed
    /// (one archive write, final detail) once input pauses for .18 s. One
    /// bounded task is re-armed instead of allocating one per event.
    private func holdGesture() {
        if !gestureHeld { gestureHeld = true; context.onLockParallax?() }
        gestureDeadline = ProcessInfo.processInfo.systemUptime + 0.18
        scheduleGestureEnd()
    }

    private func scheduleGestureEnd() {
        guard gestureEnd == nil, gestureHeld else { return }
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.gestureEnd = nil
            if ProcessInfo.processInfo.systemUptime < self.gestureDeadline { self.scheduleGestureEnd() }
            else { self.releaseGesture(commit: true) }
        }
        gestureEnd = work
        DispatchQueue.main.asyncAfter(deadline: .now() + max(0, gestureDeadline - ProcessInfo.processInfo.systemUptime), execute: work)
    }

    private func releaseGesture(commit: Bool) {
        gestureEnd?.cancel(); gestureEnd = nil
        guard gestureHeld else { return }
        gestureHeld = false
        if commit { surface.endGesture() }
        context.onInputStateChange?()
    }

    /// The map's pointer (spec: grab, grabbing while panning); nil leaves the
    /// HUD's own cursor. Box px; the host asks on cursor updates and presses.
    override func cursor(at point: CGPoint?) -> NSCursor? {
        if surface.isDragging { return .closedHand }
        guard isActive, let point, onTerrain(point) else { return nil }
        return .openHand
    }

    // MARK: Accessibility

    /// Pins and the centre placement are accessibility actions too: VoiceOver
    /// cannot right-click, and pins are drawn by the map rather than controls.
    override var accessibilityItems: [HUDAccessibilityItem] {
        var items = super.accessibilityItems
        guard isActive, !isTransitioning else { return items }
        let origin = Self.mapFrame.origin
        for (index, pin) in surface.pins.enumerated() {
            let point = surface.screen(CGPoint(x: pin.x, y: pin.y))
            guard WorldMapSurface.contains(point) else { continue }
            items.append(HUDAccessibilityItem(id: "map.pin:" + pin.id.uuidString,
                label: WorldMapPinArtwork.title(for: pin.style) + " " + HUDFormat.pad2(index + 1) + ", "
                    + WorldMapGeometry.coordinateDescription(x: pin.x, y: pin.y),
                role: .button, rect: CGRect(origin: CGPoint(x: origin.x + point.x, y: origin.y + point.y), size: .zero)
                    .insetBy(dx: -WorldMapSurface.pinHitRadius(pin.style), dy: -WorldMapSurface.pinHitRadius(pin.style))))
        }
        if surface.selectedPinID != nil, let plate = coordinates?.plate {
            items.append(HUDAccessibilityItem(id: "map.removePin", label: L10n.text("Remove selected pin", "移除选中标记"),
                                              role: .button, rect: plate.convert(plate.bounds, to: root)))
        }
        return items
    }

    override func performAccessibilityAction(_ id: String) {
        if id.hasPrefix("map.pin:"), let uuid = UUID(uuidString: String(id.dropFirst(8))) { surface.activatePin(id: uuid); return }
        switch id {
        case "map.removePin": if let selected = surface.selectedPinID { deletePin(selected) }
        default: super.performAccessibilityAction(id)
        }
    }
}
