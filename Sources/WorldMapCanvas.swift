import AppKit
import QuartzCore

struct WorldMapAction {
    let id: String
    let label: String
    let rect: CGRect
    let enabled: Bool
}

/// Geography is painted by a bounded background worker. Input transforms its
/// retained image immediately; native pins and controls remain independent.
final class WorldMapCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    var onChange: (() -> Void)?
    private let store: WorldMapStore?
    private let raster = WorldMapRasterController()
    private let pinsHost = CALayer()
    private let chrome = CALayer()
    var controlHighlightRoot: CALayer { chrome }
    var isRasterSettledForVerification: Bool { raster.isSettled }
    private var terrain: WorldMapTerrain?
    private var countries: WorldMapCountries?
    private var terrainRequested = false
    private let loadsTerrain: Bool
    private var chromeKey: String?
    private var chromeStatus: CATextLayer?
    private var statusZoom: Double?
    private var pinLabels: [UUID: String] = [:]
    private var labeledPins: [MapPin] = []
    private var pinLabelLanguage: AppLanguage?
    private var pinStyleRevision = 0
    private var markerStyleRevisions: [UUID: Int] = [:]
    private var animatedPinIDs: Set<UUID> = []
    private var pulseVisibleIDs: Set<UUID> = []
    private var pulseSelection: UUID?
    private var pinLayers: [UUID: CALayer] = [:]
    private var active = false
    private var dark = true
    private var accent = HUDRuntimeAppearance.accent
    private var scale: CGFloat = 2
    private let reduceMotion: () -> Bool
    private let ambient: () -> Bool
    private var message: String?
    private(set) var viewport: WorldMapViewport
    private(set) var selectedPinID: UUID?
    private(set) var showsPinCoordinates = false
    private var dragStart: CGPoint?
    private var dragViewport: WorldMapViewport?
    var isDragging: Bool { dragStart != nil }
    var pins: [MapPin] { store?.pins ?? [] }
    private static let deleteRect = CGRect(x: 316, y: 303, width: 21, height: 21)
    private static let loaderQueue = DispatchQueue(label: "EndfieldHUD.world-map", qos: .utility)
    private static var cachedTerrain: Result<WorldMapTerrain, Error>?
    private static var cachedCountries: Result<WorldMapCountries, Error>?

    init(store: WorldMapStore?, error: String? = nil, terrain: WorldMapTerrain? = nil, countries: WorldMapCountries? = nil,
         loadsTerrain: Bool = true, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion },
         ambient: @escaping () -> Bool = { HUDRuntimeAppearance.ambientEnabled }) {
        self.store = store; message = error; self.terrain = terrain; self.countries = countries; self.loadsTerrain = loadsTerrain
        self.reduceMotion = reduceMotion; self.ambient = ambient
        viewport = WorldMapGeometry.constrained(store?.viewport ?? WorldMapViewport())
        super.init()
        layer.name = "map.canvas"; layer.bounds = CGRect(origin: .zero, size: WorldMapGeometry.size)
        pinsHost.name = "map.pins"; pinsHost.frame = layer.bounds
        chrome.name = "map.controls"; chrome.frame = layer.bounds
        // One retained compositor gradient softens the circumference without
        // a live blur filter or an additional geography-sized bitmap.
        let mask = CAGradientLayer(); mask.name = "map.edge.feather"; mask.frame = layer.bounds
        mask.type = .radial
        mask.startPoint = CGPoint(x: 0.5, y: 0.5)
        mask.endPoint = CGPoint(x: 0.5 + WorldMapGeometry.radius / WorldMapGeometry.size.width,
                                y: 0.5 + WorldMapGeometry.radius / WorldMapGeometry.size.height)
        mask.colors = [NSColor.black.cgColor, NSColor.black.cgColor, NSColor.clear.cgColor]
        mask.locations = [0, NSNumber(value: Double(1 - WorldMapGeometry.edgeFeatherWidth / WorldMapGeometry.radius)), 1]
        layer.mask = mask
        raster.setData(terrain: terrain, countries: countries)
        layer.addSublayer(raster.layer)
        layer.addSublayer(pinsHost); layer.addSublayer(chrome)
    }
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; accent = style.accent; scale = style.contentsScale
        pinStyleRevision += 1
        withoutActions { installTerrain(); renderPins(); renderChrome() }
        requestTerrain(); return layer
    }
    func updateRenderScale(_ value: CGFloat) {
        guard value.isFinite else { return }
        let next = min(8, max(1, value))
        guard scale != next else { return }
        scale = next; pinStyleRevision += 1
        withoutActions { updateTerrainTransform(); renderPins(); renderChrome() }
    }
    /// Populate the retained image during deployment, before input is enabled.
    /// This schedules the same bounded worker; it does not start pin animation.
    func prepareForPresentation() {
        raster.activate()
        requestTerrain()
    }
    func activate() {
        guard !active else { return }; active = true
        raster.activate()
        requestTerrain(); withoutActions { renderPins(); renderChrome() }; onChange?()
    }
    func deactivate() {
        mouseUp(); endGesture(); active = false
        pinLayers.values.forEach { stopAnimations($0) }
        raster.deactivate(); chrome.removeAllAnimations()
    }
    private func requestTerrain() {
        guard (terrain == nil || countries == nil), !terrainRequested, loadsTerrain else { return }
        terrainRequested = true
        Self.loaderQueue.async { [weak self] in
            if Self.cachedTerrain == nil { Self.cachedTerrain = Result { try WorldMapTerrain.load() } }
            if Self.cachedCountries == nil { Self.cachedCountries = Result { try WorldMapCountries.load() } }
            let result = Self.cachedTerrain!
            let countryResult = Self.cachedCountries!
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                switch result {
                case .success(let data): self.terrain = data
                case .failure: self.message = L10n.text("Terrain data could not be loaded.", "无法载入地形数据。")
                }
                switch countryResult {
                case .success(let data): self.countries = data
                case .failure: self.message = L10n.text("Country shapes could not be loaded.", "无法载入地图轮廓。")
                }
                self.raster.setData(terrain: self.terrain, countries: self.countries)
                self.withoutActions { self.installTerrain(); self.renderChrome(); self.renderPins() }
                self.onChange?()
            }
        }
    }
    var accessibilityStatus: String {
        message ?? L10n.text("Map", "地图")
    }
    private var toolbarActions: [WorldMapAction] {
        let controls: [(String, String, CGFloat, Bool)] = [
            ("zoomIn", L10n.text("Zoom in", "放大"), 184, viewport.zoom < WorldMapViewport.maxZoom),
            ("zoomOut", L10n.text("Zoom out", "缩小"), 212, viewport.zoom > WorldMapViewport.minZoom),
            ("reset", L10n.text("Reset zoom to 3×", "重置缩放至3×"), 240, true)
        ]
        var actions = controls.map { WorldMapAction(id: "map:" + $0.0, label: $0.1,
            rect: CGRect(x: 26, y: $0.2, width: 22, height: 22), enabled: $0.3) }
        actions.append(WorldMapAction(id: "map:addPin", label: L10n.text("Place pin at map center", "在地图中心放置标记"),
            rect: CGRect(x: 392, y: 207, width: 22, height: 22), enabled: store != nil && pins.count < WorldMapStore.maximumPinCount))
        if selectedPinID != nil && showsPinCoordinates {
            actions.append(WorldMapAction(id: "map:deletePin", label: L10n.text("Remove selected pin", "移除选中标记"), rect: Self.deleteRect, enabled: true))
        }
        return actions
    }
    var accessibleActions: [WorldMapAction] {
        var actions = toolbarActions
        let currentPins = pins
        if labeledPins != currentPins || pinLabelLanguage != L10n.resolvedLanguage {
            labeledPins = currentPins; pinLabelLanguage = L10n.resolvedLanguage
            pinLabels = Dictionary(uniqueKeysWithValues: currentPins.enumerated().map { index, pin in
                (pin.id, L10n.text("Pin ", "标记 ") + String(index + 1) + ", "
                    + WorldMapGeometry.coordinateDescription(x: pin.x, y: pin.y))
            })
        }
        for pin in currentPins {
            let p = WorldMapGeometry.screen(x: pin.x, y: pin.y, viewport: viewport)
            guard containsMapPoint(p), !actions.contains(where: { $0.rect.insetBy(dx: -9, dy: -9).contains(p) }) else { continue }
            actions.append(WorldMapAction(id: "map:pin:" + pin.id.uuidString,
                label: pinLabels[pin.id] ?? "",
                rect: CGRect(x: p.x - 9, y: p.y - 9, width: 18, height: 18), enabled: true))
        }
        return actions
    }
    func containsMapPoint(_ point: CGPoint) -> Bool {
        WorldMapGeometry.contains(point)
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard containsMapPoint(point) else { return false }
        if let action = accessibleActions.first(where: { $0.rect.contains(point) }) {
            if action.enabled { perform(actionID: action.id) }
            hidePinCoordinates(); return true
        }
        hidePinCoordinates()
        dragStart = point; dragViewport = viewport; return true
    }
    func mouseDragged(to point: CGPoint) {
        guard let start = dragStart, let original = dragViewport else { return }
        let next = WorldMapGeometry.panned(original, delta: CGPoint(x: point.x - start.x, y: point.y - start.y))
        guard next != viewport else { return }; viewport = next
        updateCamera()
    }
    func mouseUp() {
        guard isDragging else { return }; dragStart = nil; dragViewport = nil; endGesture()
    }
    func endGesture() {
        raster.finishGesture()
        guard let store else { return }
        do { try store.setViewport(viewport) }
        catch { showError(error.localizedDescription) }
    }
    @discardableResult func rightMouseDown(at point: CGPoint) -> Bool {
        guard containsMapPoint(point) else { return false }
        if let action = toolbarActions.first(where: { $0.rect.contains(point) }) {
            if action.id == "map:deletePin" { perform(actionID: action.id) }
            return true
        }
        // A newly placed pin may be close to another marker or the coordinate
        // card. Deletion targets the marker itself, independent of AX spacing.
        for pin in pins.reversed() {
            let location = WorldMapGeometry.screen(x: pin.x, y: pin.y, viewport: viewport)
            if CGRect(x: location.x - 9, y: location.y - 9, width: 18, height: 18).contains(point) {
                removePin(id: pin.id); return true
            }
        }
        addPin(at: point); return true
    }
    func zoom(at point: CGPoint, factor: Double) {
        guard containsMapPoint(point) else { return }
        let next = WorldMapGeometry.zoomed(viewport, at: point, factor: factor)
        guard next != viewport else { return }; viewport = next
        updateCamera()
    }
    func perform(actionID: String) {
        if actionID.hasPrefix("map:pin:"), let id = UUID(uuidString: String(actionID.dropFirst(8))), pins.contains(where: { $0.id == id }) {
            selectedPinID = id; showsPinCoordinates = true
            withoutActions { renderPins(); renderChrome() }; onChange?(); return
        }
        switch actionID {
        case "map:zoomIn", "map:zoomOut":
            viewport = WorldMapGeometry.zoomed(viewport, at: WorldMapGeometry.center, factor: actionID == "map:zoomIn" ? 1.3 : 1 / 1.3)
            updateCamera(animated: true); endGesture()
        case "map:reset":
            viewport.zoom = WorldMapViewport.defaultZoom
            viewport = WorldMapGeometry.constrained(viewport)
            updateCamera(animated: true); endGesture()
        case "map:addPin": addPin(at: WorldMapGeometry.center)
        case "map:deletePin":
            guard let id = selectedPinID else { return }; removePin(id: id)
        default: break
        }
    }
    func keyDown(keyCode: UInt16) -> Bool {
        switch keyCode {
        case 123: viewport = WorldMapGeometry.panned(viewport, delta: CGPoint(x: 32, y: 0))
        case 124: viewport = WorldMapGeometry.panned(viewport, delta: CGPoint(x: -32, y: 0))
        case 125: viewport = WorldMapGeometry.panned(viewport, delta: CGPoint(x: 0, y: -32))
        case 126: viewport = WorldMapGeometry.panned(viewport, delta: CGPoint(x: 0, y: 32))
        case 24, 69: perform(actionID: "map:zoomIn"); return true
        case 27, 78: perform(actionID: "map:zoomOut"); return true
        case 51, 117: guard selectedPinID != nil else { return false }; perform(actionID: "map:deletePin"); return true
        case 53:
            guard selectedPinID != nil else { return false }
            selectedPinID = nil; showsPinCoordinates = false
            withoutActions { renderPins(); renderChrome() }; onChange?(); return true
        default: return false
        }
        updateCamera(); endGesture(); return true
    }
    private func addPin(at point: CGPoint) {
        guard let store else { return }
        let location = WorldMapGeometry.world(at: point, viewport: viewport)
        guard (0...1).contains(location.y) else { return }
        do {
            let pin = try store.addPin(x: location.x, y: location.y)
            selectedPinID = pin.id; showsPinCoordinates = true; message = nil
            withoutActions { renderPins(); renderChrome() }; onChange?()
        } catch { showError(error.localizedDescription) }
    }
    private func hidePinCoordinates() {
        guard showsPinCoordinates else { return }
        showsPinCoordinates = false
        withoutActions { renderChrome() }; onChange?()
    }
    private func removePin(id: UUID) {
        guard let store else { return }
        do {
            try store.removePin(id: id)
            if selectedPinID == id { selectedPinID = nil; showsPinCoordinates = false }
            message = nil
            withoutActions { renderPins(); renderChrome() }; onChange?()
        } catch { showError(error.localizedDescription) }
    }
    private func showError(_ text: String) {
        message = text; withoutActions { renderChrome() }; onChange?()
    }
    private func installTerrain() {
        layer.backgroundColor = NSColor(white: dark ? 0.12 : 0.81, alpha: 0.89).cgColor
        raster.configure(dark: dark, accent: accent.cgColor, contentsScale: scale)
        updateTerrainTransform()
    }
    private func updateTerrainTransform(animated: Bool = false) {
        raster.configure(dark: dark, accent: accent.cgColor, contentsScale: scale)
        raster.update(viewport: viewport, animated: animated)
    }
    private func updateCamera(animated: Bool = false) {
        // Drag/scroll already supplies continuous positions. Avoid easing
        // behind the pointer and never animate a second terrain snapshot.
        let animate = animated && !reduceMotion()
        CATransaction.begin(); CATransaction.setDisableActions(true)
        updateTerrainTransform(animated: animate); renderPins(animated: animate); renderChrome()
        CATransaction.commit(); onChange?()
    }
    private func renderPins(animated: Bool = false) {
        let visible = pins.filter { pin in
            let point = WorldMapGeometry.screen(x: pin.x, y: pin.y, viewport: viewport)
            return WorldMapGeometry.contains(point)
        }
        let ids = Set(visible.map(\.id))
        for id in Array(pinLayers.keys) where !ids.contains(id) {
            pinLayers.removeValue(forKey: id)?.removeFromSuperlayer(); markerStyleRevisions.removeValue(forKey: id)
        }
        // Bounded compositor-only pulses. Other markers keep their static ring.
        if ids != pulseVisibleIDs || selectedPinID != pulseSelection {
            pulseVisibleIDs = ids; pulseSelection = selectedPinID
            animatedPinIDs = Set(visible.sorted { ($0.id == selectedPinID ? 1 : 0, $0.createdAt) > ($1.id == selectedPinID ? 1 : 0, $1.createdAt) }.prefix(12).map(\.id))
        }
        let pulsesEnabled = active && !reduceMotion() && ambient()
        for pin in visible {
            let marker: CALayer
            let start: WorldMapLayerCoordinates.Presentation?
            if let existing = pinLayers[pin.id] {
                marker = existing; start = animated ? WorldMapLayerCoordinates.capture(existing) : nil
            }
            else {
                start = nil
                marker = CALayer(); marker.name = "map.pin." + pin.id.uuidString
                marker.bounds = CGRect(x: -24, y: -24, width: 48, height: 48)
                let contrast = CAShapeLayer(); contrast.name = "map.pin.contrast"
                contrast.path = CGPath(ellipseIn: CGRect(x: -5.3, y: -5.3, width: 10.6, height: 10.6), transform: nil)
                contrast.fillColor = NSColor(white: 0.06, alpha: 0.82).cgColor
                marker.addSublayer(contrast)
                let halo = CAShapeLayer(); halo.name = "map.pin.pulse"
                halo.path = CGPath(ellipseIn: CGRect(x: -8, y: -8, width: 16, height: 16), transform: nil)
                halo.fillColor = nil; halo.lineWidth = 0.55; marker.addSublayer(halo)
                let ring = CAShapeLayer(); ring.name = "map.pin.ring"
                ring.path = CGPath(ellipseIn: CGRect(x: -4, y: -4, width: 8, height: 8), transform: nil)
                ring.fillColor = nil; ring.lineWidth = 1.2; marker.addSublayer(ring)
                let dot = CAShapeLayer(); dot.name = "map.pin.dot"
                dot.path = CGPath(ellipseIn: CGRect(x: -1.8, y: -1.8, width: 3.6, height: 3.6), transform: nil)
                marker.addSublayer(dot); pinsHost.addSublayer(marker); pinLayers[pin.id] = marker
            }
            WorldMapLayerCoordinates.apply(marker, position: WorldMapGeometry.screen(x: pin.x, y: pin.y, viewport: viewport),
                                           transform: .identity, from: start)
            marker.zPosition = pin.id == selectedPinID ? 1 : 0
            let updateStyle = markerStyleRevisions[pin.id] != pinStyleRevision
            for shape in marker.sublayers?.compactMap({ $0 as? CAShapeLayer }) ?? [] {
                if updateStyle { shape.contentsScale = scale }
                if shape.name == "map.pin.contrast" { continue }
                if updateStyle {
                    if shape.name == "map.pin.dot" { shape.fillColor = accent.cgColor }
                    else { shape.strokeColor = accent.cgColor }
                }
                guard shape.name == "map.pin.pulse" else { continue }
                let pulses = pulsesEnabled && animatedPinIDs.contains(pin.id)
                shape.opacity = pulses ? 0 : (pin.id == selectedPinID ? 0.65 : 0.25)
                if pulses, shape.animation(forKey: "map.pulse") == nil {
                    let growth = CABasicAnimation(keyPath: "transform.scale"); growth.fromValue = 0.65; growth.toValue = 2.7
                    let fade = CAKeyframeAnimation(keyPath: "opacity"); fade.values = [0.05, 0.65, 0]; fade.keyTimes = [0, 0.12, 1]
                    let pulse = CAAnimationGroup(); pulse.animations = [growth, fade]; pulse.duration = 1.7; pulse.repeatCount = .infinity
                    pulse.timingFunction = CAMediaTimingFunction(name: .easeOut); shape.add(pulse, forKey: "map.pulse")
                } else if !pulses { shape.removeAnimation(forKey: "map.pulse") }
            }
            markerStyleRevisions[pin.id] = pinStyleRevision
        }
    }
    private func renderChrome() {
        let key = "\(dark)|\(accent)|\(scale)|\(L10n.resolvedLanguage)|\(viewport.zoom < WorldMapViewport.maxZoom)|\(viewport.zoom > WorldMapViewport.minZoom)|\(pins.count)|\(selectedPinID?.uuidString ?? "")|\(showsPinCoordinates)|\(message ?? "")|\(terrain != nil)"
        guard key != chromeKey else { updateZoomStatus(); return }; chromeKey = key
        chrome.sublayers?.forEach { $0.removeFromSuperlayer() }
        let ink = NSColor(white: dark ? 0.95 : 0.13, alpha: 1)
        text(L10n.text("MAP", "地图"), rect: CGRect(x: 122, y: 37, width: 196, height: 17), size: 11, color: ink, alignment: .center)
        chromeStatus = text("", rect: CGRect(x: 115, y: 55, width: 210, height: 13), size: 8, color: ink.withAlphaComponent(0.55), alignment: .center)
        statusZoom = nil; updateZoomStatus()
        for action in toolbarActions {
            let plate = CAShapeLayer(); plate.path = CGPath(roundedRect: action.rect, cornerWidth: 2, cornerHeight: 2, transform: nil)
            plate.fillColor = NSColor(white: dark ? 0.08 : 0.94, alpha: 0.88).cgColor; chrome.addSublayer(plate)
            HUDControlHighlightLayer.add(to: chrome, rect: action.rect, shape: .cutCorner, enabled: action.enabled, framed: true)
            let symbol: String
            switch action.id {
            case "map:zoomIn": symbol = "+"
            case "map:zoomOut": symbol = "−"
            case "map:reset": symbol = "⌖"
            case "map:deletePin": symbol = "×"
            default: symbol = "⊙"
            }
            text(symbol, rect: action.rect.insetBy(dx: 1, dy: 2), size: 15, color: ink.withAlphaComponent(action.enabled ? 0.90 : 0.3), alignment: .center)
        }
        if showsPinCoordinates, let pin = pins.first(where: { $0.id == selectedPinID }) {
            let caption = CALayer(); caption.name = "map.pin.coordinates"; caption.frame = CGRect(x: 100, y: 301, width: 211, height: 26)
            caption.backgroundColor = NSColor(white: dark ? 0.06 : 0.94, alpha: 0.88).cgColor; chrome.addSublayer(caption)
            text(WorldMapGeometry.coordinateDescription(x: pin.x, y: pin.y), rect: CGRect(x: 106, y: 309, width: 202, height: 15), size: 9, color: accent, alignment: .center)
        }
        if let message {
            text(message, rect: CGRect(x: 97, y: 330, width: 246, height: 22), size: 7, color: ink.withAlphaComponent(0.53), alignment: .center)
        }
    }
    private func updateZoomStatus() {
        guard statusZoom != viewport.zoom else { return }; statusZoom = viewport.zoom
        chromeStatus?.string = terrain == nil && loadsTerrain ? L10n.text("LOADING TERRAIN", "载入地形")
            : String(format: "%.2f×  /  %02d ", viewport.zoom, pins.count) + L10n.text("PINS", "标记")
    }
    @discardableResult private func text(_ text: String, rect: CGRect, size: CGFloat, color: NSColor, alignment: CATextLayerAlignmentMode) -> CATextLayer {
        let item = CATextLayer(); item.frame = rect; item.string = text; item.font = NSFont.systemFont(ofSize: size, weight: .semibold)
        item.fontSize = size; item.foregroundColor = color.cgColor; item.alignmentMode = alignment
        item.contentsScale = HUDRenderScale.contentScale(for: item, baseScale: scale); item.isWrapped = true; chrome.addSublayer(item)
        return item
    }
    private func stopAnimations(_ item: CALayer) {
        item.removeAllAnimations(); item.sublayers?.forEach(stopAnimations)
        if let mask = item.mask { stopAnimations(mask) }
    }
    private func withoutActions(_ body: () -> Void) { CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit() }
}
