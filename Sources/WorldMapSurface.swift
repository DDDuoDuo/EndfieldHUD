import AppKit
import QuartzCore

/// The world map of the Map module, in map px (content-box px from the
/// square's corner). It fills the whole centre backdrop (HUDCentreBackdrop,
/// the disc Personal ID's art fills) and fades out with its ramp; the camera
/// centre is the disc's centre. The v1.2.0 viewport (WorldMapGeometry, 440
/// units, a 216-unit circle) keeps its scale `unitScale`: its circle, at the
/// shell's inner rim, is where presses, the wheel and pins count, and the pins
/// (yellow, green and the player arrow) keep their v1.2.0 sizes, colours and
/// pulses. It holds the retained raster of WorldMapRasterController (its
/// padded detail image already covers the whole disc), the pins and the
/// camera. Pointer events only move retained layers. While active, up to 12
/// pins pulse as in v1.2.0 (Reduce Motion and disabled ambient motion stop
/// them; nothing animates while hidden). The layer is decorative to the hover pass.
final class WorldMapSurface {
    /// Content-box px per v1.2.0 map unit: its 440-unit viewport was scaled
    /// 1.14 on the source shell's plane, where one box px is
    /// HUDDesignGeometry.baseUnit design units.
    static let unitScale: CGFloat = 1.14 * 1920 * 1.15 / 1470
    /// The backdrop disc's square.
    static let size = HUDCentreBackdrop.frame.size
    static let center = CGPoint(x: size.width / 2, y: size.height / 2)
    /// The input circle (v1.2.0's round map), concentric with the disc.
    static let radius = WorldMapGeometry.radius * unitScale
    /// Pins are drawn out to here; beyond it the disc has faded out.
    static let visibleRadius = HUDCentreBackdrop.radius
    /// Map px per raster point (the v1.2.0 scale).
    static let rasterScale = unitScale
    /// The demo's 1× (whole world) up to the archive's 128× street level.
    static let zoomRange = WorldMapViewport.minZoom...WorldMapViewport.maxZoom
    /// v1.2.0 hit radius: the player arrow is larger than the round pins.
    static func pinHitRadius(_ style: MapPinStyle) -> CGFloat { (style == .player ? 13 : 9) * unitScale }
    /// Manhattan distance a press must travel before it pans (spec: > 3 px).
    static let dragThreshold: CGFloat = 3
    /// Pins that pulse at once (v1.2.0): the selected one first, then the newest.
    static let pulsingPinLimit = 12

    let layer = HUDDecorativeContentLayer()
    var onCamera: ((_ animated: Bool) -> Void)?
    var onPins: (() -> Void)?
    var onError: ((String) -> Void)?
    var onRecenter: (() -> Void)?
    var onPinStyleChanged: ((MapPinStyle) -> Void)?
    private(set) var camera: WorldMapViewport
    private(set) var selectedPinID: UUID?
    var pins: [MapPin] { store?.pins ?? [] }
    var isDragging: Bool { drag != nil }
    var isRasterSettledForVerification: Bool { raster.isSettled }
    /// v1.2.0's pin pulses now running: a live loop while the map is shown.
    var pulseAnimationCount: Int {
        markers.values.reduce(0) { $0 + ($1.sublayers ?? []).filter { $0.animation(forKey: "map.pulse") != nil }.count }
    }
    private let store: WorldMapStore?
    private var accent: CGColor
    private let reduceMotion: () -> Bool
    private let ambient: () -> Bool
    private let loadsTerrain: Bool
    private let raster = WorldMapRasterController()
    private let rasterHost = CALayer()
    private let scrim = CALayer()
    private let pinLayer = CALayer()
    private let fade = CALayer()
    private var markers: [UUID: CALayer] = [:]
    private var markerStyles: [UUID: MapPinStyle] = [:]
    private var pulsingIDs: Set<UUID> = []
    /// What the pulses were last set up for; camera events that keep it skip them.
    private var pulseKey: (visible: Set<UUID>, selected: UUID?, running: Bool)?
    private var active = false
    private var terrain: WorldMapTerrain?
    private var countries: WorldMapCountries?
    private var terrainRequested = false
    private var contentsScale: CGFloat = 2
    private var drag: (start: CGPoint, camera: WorldMapViewport, moved: Bool)?
    private var relievedPaintCount = 0
    private static let loaderQueue = DispatchQueue(label: "EndfieldHUD.world-map", qos: .utility)
    private static var cachedTerrain: Result<WorldMapTerrain, Error>?
    private static var cachedCountries: Result<WorldMapCountries, Error>?

    init(store: WorldMapStore?, terrain: WorldMapTerrain? = nil, countries: WorldMapCountries? = nil, loadsTerrain: Bool = true,
         accent: CGColor, reduceMotion: @escaping () -> Bool, ambient: @escaping () -> Bool) {
        self.store = store; self.terrain = terrain; self.countries = countries
        self.loadsTerrain = loadsTerrain; self.accent = accent; self.reduceMotion = reduceMotion; self.ambient = ambient
        camera = Self.constrained(store?.viewport ?? WorldMapViewport())
        withoutActions {
            layer.name = "map.surface"; layer.bounds = CGRect(origin: .zero, size: Self.size); layer.masksToBounds = true
            fade.name = "map.fade"; fade.frame = layer.bounds; fade.contentsGravity = .resize
            layer.mask = fade
            rasterHost.name = "map.raster.host"; rasterHost.bounds = CGRect(origin: .zero, size: WorldMapGeometry.size)
            scrim.name = "map.scrim"; scrim.contentsGravity = .resize; scrim.isHidden = true
            pinLayer.name = "map.pins"; pinLayer.frame = layer.bounds
            rasterHost.addSublayer(raster.layer)
            [rasterHost, scrim, pinLayer].forEach(layer.addSublayer)
        }
        raster.setData(terrain: terrain, countries: countries)
        raster.configure(dark: true, accent: accent, contentsScale: contentsScale * Self.rasterScale)
        applyCamera(animated: false, notify: false)
    }

    // MARK: Camera geometry (map px)

    /// Map px per world unit at `zoom` (the world is 2 : 1).
    static func unit(_ zoom: Double) -> CGSize {
        CGSize(width: 440 * zoom * Double(rasterScale), height: 220 * zoom * Double(rasterScale))
    }

    /// Zoom within `zoomRange`, west/east wrap; the poles stay inside
    /// v1.2.0's viewport square once the world is taller than it, so the
    /// camera moves as it did there (the disc's faded rim may show past a pole).
    static func constrained(_ value: WorldMapViewport) -> WorldMapViewport {
        var next = (try? value.normalized()) ?? WorldMapViewport()
        next.zoom = min(zoomRange.upperBound, max(zoomRange.lowerBound, next.zoom))
        let half = Double(WorldMapGeometry.center.y * unitScale) / Double(unit(next.zoom).height)
        next.centerY = half >= 0.5 ? 0.5 : min(1 - half, max(half, next.centerY))
        return next
    }

    static func screen(x: Double, y: Double, camera: WorldMapViewport) -> CGPoint {
        let unit = unit(camera.zoom)
        var dx = x - camera.centerX; dx -= dx.rounded()
        return CGPoint(x: center.x + dx * unit.width, y: center.y + (y - camera.centerY) * unit.height)
    }

    static func world(at point: CGPoint, camera: WorldMapViewport) -> CGPoint {
        let unit = unit(camera.zoom)
        return CGPoint(x: camera.centerX + Double(point.x - center.x) / unit.width,
                       y: camera.centerY + Double(point.y - center.y) / unit.height)
    }

    static func panned(_ camera: WorldMapViewport, by delta: CGPoint) -> WorldMapViewport {
        guard delta.x.isFinite, delta.y.isFinite else { return camera }
        let unit = unit(camera.zoom)
        var next = camera
        next.centerX -= Double(delta.x) / unit.width; next.centerY -= Double(delta.y) / unit.height
        return constrained(next)
    }

    /// Keeps the world point under `point` in place (unless a pole stops it).
    static func zoomed(_ camera: WorldMapViewport, at point: CGPoint, factor: Double) -> WorldMapViewport {
        guard point.x.isFinite, point.y.isFinite, factor.isFinite, factor > 0 else { return camera }
        let anchor = world(at: point, camera: camera)
        var next = camera
        next.zoom = min(zoomRange.upperBound, max(zoomRange.lowerBound, camera.zoom * factor))
        let unit = unit(next.zoom)
        next.centerX = anchor.x - Double(point.x - center.x) / unit.width
        next.centerY = anchor.y - Double(point.y - center.y) / unit.height
        return constrained(next)
    }

    /// Inside the input circle (map px): only there do presses, wheel and pins count.
    static func contains(_ point: CGPoint) -> Bool { contains(point, within: radius) }

    private static func contains(_ point: CGPoint, within limit: CGFloat) -> Bool {
        point.x.isFinite && point.y.isFinite && hypot(point.x - center.x, point.y - center.y) <= limit
    }

    func screen(_ world: CGPoint) -> CGPoint { Self.screen(x: world.x, y: world.y, camera: camera) }
    func world(at point: CGPoint) -> CGPoint { Self.world(at: point, camera: camera) }
    var canZoomIn: Bool { camera.zoom < Self.zoomRange.upperBound }
    var canZoomOut: Bool { camera.zoom > Self.zoomRange.lowerBound }

    // MARK: Lifecycle

    /// Backing scale of a map px; the raster is painted at its own point scale.
    func setContentsScale(_ value: CGFloat) {
        guard value.isFinite, value > 0, value != contentsScale else { return }
        contentsScale = value
        raster.configure(dark: true, accent: accent, contentsScale: value * Self.rasterScale)
        withoutActions { markers.values.forEach { $0.sublayers?.forEach { $0.contentsScale = value } } }
    }

    /// The theme colour of the highlighted country (a page build passes the
    /// current accent; an unchanged one costs nothing).
    func setAccent(_ value: CGColor) {
        guard value != accent else { return }
        accent = value
        raster.configure(dark: true, accent: value, contentsScale: contentsScale * Self.rasterScale)
    }

    /// A subtle dark veil over the top of the circle, behind the module
    /// header drawn there, down to `bottom` (map px) and fading out over
    /// `feather` below it. One cached column image, stretched across.
    func setHeaderScrim(bottom: CGFloat, feather: CGFloat = 32, alpha: CGFloat = 0.5) {
        let frame = CGRect(x: 0, y: 0, width: Self.size.width, height: bottom + feather)
        let rows = Int(ceil(frame.height))
        guard rows > 0, bottom >= 0, let context = CGContext(data: nil, width: 1, height: rows, bitsPerComponent: 8, bytesPerRow: 0,
                                                space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let ramp = CGGradient(colorsSpace: CGColorSpaceCreateDeviceGray(),
                                    colors: [CGColor(gray: 0.04, alpha: alpha), CGColor(gray: 0.04, alpha: alpha), CGColor(gray: 0.04, alpha: 0)] as CFArray,
                                    locations: [0, bottom / frame.height, 1]) else { return }
        // Bitmap rows run bottom-up: the opaque end is the layer's top.
        context.drawLinearGradient(ramp, start: CGPoint(x: 0, y: CGFloat(rows)), end: .zero, options: [])
        let image = context.makeImage()
        withoutActions { scrim.frame = frame; scrim.contents = image; scrim.isHidden = false }
    }

    /// Starts the bounded painter (and the one-time data load) before input is enabled.
    func prepareForPresentation() {
        if fade.contents == nil { withoutActions { fade.contents = HUDCentreBackdrop.mask } }
        raster.activate()
        requestData()
    }

    /// The module is shown and takes input: pins may pulse.
    func activate() {
        guard !active else { return }
        active = true
        withoutActions { updatePins(animated: false) }
    }

    func deactivate() {
        cancelDrag()
        endGesture()
        releaseWorkingSet()
        active = false; pulseKey = nil
        withoutActions { removeAnimations(layer) }
    }

    /// Ends the painter's geographic working set while the map takes no
    /// input, as the 1.2 map did whenever module input stopped (including at
    /// the end of the opening). The image on screen stays; the next paint
    /// (a gesture, a style change) builds the working set again.
    func releaseWorkingSet() {
        raster.deactivate()
        // A painter's geometry spans tens of MB of short-lived allocations.
        // Once it is released (on the paint queue), return those free pages
        // once, off the main thread, as the closed HUD does for its views.
        guard raster.completedPaintCount != relievedPaintCount else { return }
        relievedPaintCount = raster.completedPaintCount
        DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + 0.5) { _ = malloc_zone_pressure_relief(nil, 0) }
    }

    /// The closed HUD keeps no map: the decoded terrain and country paths
    /// (about 10 MB) are dropped and decoded again, off the main thread, when
    /// a map is next shown. Call off the main thread; it waits for a load.
    static func releaseSharedData() {
        loaderQueue.sync { cachedTerrain = nil; cachedCountries = nil }
    }

    private func requestData() {
        guard terrain == nil || countries == nil, !terrainRequested, loadsTerrain else { return }
        terrainRequested = true
        Self.loaderQueue.async { [weak self] in
            if Self.cachedTerrain == nil { Self.cachedTerrain = Result { try WorldMapTerrain.load() } }
            if Self.cachedCountries == nil { Self.cachedCountries = Result { try WorldMapCountries.load() } }
            let terrain = Self.cachedTerrain!, countries = Self.cachedCountries!
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                switch terrain {
                case .success(let data): self.terrain = data
                case .failure: self.onError?(L10n.text("Terrain data could not be loaded.", "无法载入地形数据。"))
                }
                switch countries {
                case .success(let data): self.countries = data
                case .failure: self.onError?(L10n.text("Country shapes could not be loaded.", "无法载入地图轮廓。"))
                }
                self.raster.setData(terrain: self.terrain, countries: self.countries)
            }
        }
    }

    // MARK: Pointer and keyboard (map px)

    func beginDrag(at point: CGPoint) { drag = (point, camera, false) }

    func drag(to point: CGPoint) {
        guard var current = drag, point.x.isFinite, point.y.isFinite else { return }
        let delta = CGPoint(x: point.x - current.start.x, y: point.y - current.start.y)
        guard current.moved || abs(delta.x) + abs(delta.y) > Self.dragThreshold else { return }
        current.moved = true; drag = current
        let next = Self.panned(current.camera, by: delta)
        guard next != camera else { return }
        camera = next
        applyCamera(animated: false)
    }

    /// Ends a press. Returns its start point when it was a click, not a pan.
    @discardableResult func endDrag() -> CGPoint? {
        guard let current = drag else { return nil }
        drag = nil
        if current.moved { endGesture(); return nil }
        return current.start
    }

    /// An interrupted press neither pans further nor counts as a click.
    func cancelDrag() {
        guard let current = drag else { return }
        drag = nil
        if current.moved { endGesture() }
    }

    func zoom(at point: CGPoint, factor: Double, animated: Bool = false) {
        let next = Self.zoomed(camera, at: point, factor: factor)
        guard next != camera else { return }
        camera = next
        applyCamera(animated: animated && !reduceMotion())
    }

    func pan(by delta: CGPoint) {
        let next = Self.panned(camera, by: delta)
        guard next != camera else { return }
        camera = next
        applyCamera(animated: false)
    }

    /// Default zoom and centre (spec "Reset").
    func reset() {
        let next = Self.constrained(WorldMapViewport())
        guard next != camera else { return }
        camera = next
        applyCamera(animated: !reduceMotion())
        endGesture()
    }

    /// Centres the clicked terrain without changing zoom. The camera is
    /// committed first, so a failed save never claims a recenter.
    func recenter(at point: CGPoint) {
        let target = world(at: point)
        let next = Self.constrained(WorldMapViewport(centerX: target.x, centerY: target.y, zoom: camera.zoom))
        guard next != camera, let store else { return }
        do {
            try store.setViewport(next)
            camera = next
            applyCamera(animated: !reduceMotion())
            raster.finishGesture()
            onRecenter?()
        } catch { onError?(error.localizedDescription) }
    }

    /// Final detail and one archive write once a gesture or key step settles.
    func endGesture() {
        raster.finishGesture()
        guard let store else { return }
        do { try store.setViewport(camera) } catch { onError?(error.localizedDescription) }
    }

    // MARK: Pins

    /// The nearest pin within its hit radius of `point`; pins beyond the input circle are not hittable.
    func pin(at point: CGPoint) -> MapPin? {
        var best: (pin: MapPin, distance: CGFloat)?
        for pin in pins {
            let p = Self.screen(x: pin.x, y: pin.y, camera: camera)
            guard Self.contains(p) else { continue }
            let distance = hypot(p.x - point.x, p.y - point.y)
            if distance < Self.pinHitRadius(pin.style), distance < best?.distance ?? .infinity { best = (pin, distance) }
        }
        return best?.pin
    }

    /// Adds a yellow pin at `point` and selects it. False beyond the poles or on failure.
    @discardableResult func addPin(at point: CGPoint) -> Bool {
        guard let store else { return false }
        let location = world(at: point)
        guard (0...1).contains(location.y) else { return false }
        do {
            let pin = try store.addPin(x: location.x, y: location.y)
            selectedPinID = pin.id
            pinsChanged()
            return true
        } catch { onError?(error.localizedDescription); return false }
    }

    @discardableResult func removePin(id: UUID) -> Bool {
        guard let store, pins.contains(where: { $0.id == id }) else { return false }
        do {
            try store.removePin(id: id)
            if selectedPinID == id { selectedPinID = nil }
            pinsChanged()
            return true
        } catch { onError?(error.localizedDescription); return false }
    }

    /// Selects a pin; the selected pin cycles yellow → green → player arrow,
    /// with v1.2.0's short turn-in (Reduce Motion: an instant change).
    func activatePin(id: UUID) {
        guard pins.contains(where: { $0.id == id }) else { return }
        guard selectedPinID == id else { selectedPinID = id; pinsChanged(); return }
        guard let store else { return }
        do {
            guard let updated = try store.cyclePinStyle(id: id) else { return }
            pinsChanged()
            if active, !reduceMotion(), let marker = markers[id] {
                let turn = CABasicAnimation(keyPath: "transform.rotation.z"); turn.fromValue = -0.12; turn.toValue = 0
                let opacity = CABasicAnimation(keyPath: "opacity"); opacity.fromValue = 0.45; opacity.toValue = 1
                let change = CAAnimationGroup(); change.animations = [turn, opacity]; change.duration = 0.16
                change.timingFunction = CAMediaTimingFunction(name: .easeOut)
                marker.add(change, forKey: "map.style")
            }
            onPinStyleChanged?(updated.style)
        } catch { onError?(error.localizedDescription) }
    }

    func deselect() {
        guard selectedPinID != nil else { return }
        selectedPinID = nil
        pinsChanged()
    }

    var selectedPin: MapPin? { pins.first { $0.id == selectedPinID } }

    private func pinsChanged() {
        withoutActions { updatePins(animated: false); onPins?() }
    }

    // MARK: Layers

    /// The raster square follows its own constrained camera; the host maps it
    /// onto the map camera (scale, wrap and the vertical polar offset). The
    /// owner's chrome updates join the same transaction: one commit per event.
    private func applyCamera(animated: Bool, notify: Bool = true) {
        let r = WorldMapGeometry.constrained(camera), unit = Self.unit(camera.zoom)
        var dx = r.centerX - camera.centerX; dx -= dx.rounded()
        let scale = Self.rasterScale * CGFloat(camera.zoom / r.zoom)
        CATransaction.begin(); CATransaction.setDisableActions(true)
        raster.update(viewport: r, animated: animated)
        WorldMapLayerCoordinates.apply(rasterHost,
            position: CGPoint(x: Self.center.x + dx * unit.width, y: Self.center.y + (r.centerY - camera.centerY) * unit.height),
            transform: CGAffineTransform(scaleX: scale, y: scale),
            from: animated ? WorldMapLayerCoordinates.capture(rasterHost) : nil)
        updatePins(animated: animated)
        if notify { onCamera?(animated) }
        CATransaction.commit()
    }

    /// v1.2.0's pins: those on the disc are drawn and fade out with it. The
    /// selected pin is above the others with a brighter ring; up to 12 pins pulse.
    private func updatePins(animated: Bool) {
        var visible: [MapPin] = []
        for pin in pins {
            let point = Self.screen(x: pin.x, y: pin.y, camera: camera)
            guard Self.contains(point, within: Self.visibleRadius) else { continue }
            visible.append(pin)
            let marker = markers[pin.id] ?? makeMarker(pin.id)
            if markerStyles[pin.id] != pin.style { style(marker, as: pin.style); markerStyles[pin.id] = pin.style }
            let z: CGFloat = pin.id == selectedPinID ? 1 : 0
            if marker.zPosition != z { marker.zPosition = z }
            Self.move(marker, to: point, animated: animated)
        }
        let ids = Set(visible.map(\.id))
        for id in Array(markers.keys) where !ids.contains(id) {
            markers.removeValue(forKey: id)?.removeFromSuperlayer(); markerStyles.removeValue(forKey: id)
        }
        let pulses = active && !reduceMotion() && ambient()
        if let key = pulseKey, key.visible == ids, key.selected == selectedPinID, key.running == pulses { return }
        if pulseKey.map({ $0.visible != ids || $0.selected != selectedPinID }) ?? true {
            pulsingIDs = Set(visible.sorted { ($0.id == selectedPinID ? 1 : 0, $0.createdAt) > ($1.id == selectedPinID ? 1 : 0, $1.createdAt) }
                .prefix(Self.pulsingPinLimit).map(\.id))
        }
        pulseKey = (ids, selectedPinID, pulses)
        for pin in visible {
            guard let ring = markers[pin.id]?.sublayers?.first(where: { $0.name == "map.pin.pulse" }) else { continue }
            let pulsing = pulses && pulsingIDs.contains(pin.id)
            let opacity: Float = pulsing ? 0 : (pin.id == selectedPinID ? 0.65 : 0.25)
            if ring.opacity != opacity { ring.opacity = opacity }
            if pulsing, ring.animation(forKey: "map.pulse") == nil {
                let growth = CABasicAnimation(keyPath: "transform.scale"); growth.fromValue = 0.65; growth.toValue = 2.7
                let fade = CAKeyframeAnimation(keyPath: "opacity"); fade.values = [0.05, 0.65, 0]; fade.keyTimes = [0, 0.12, 1]
                let pulse = CAAnimationGroup(); pulse.animations = [growth, fade]; pulse.duration = 1.7; pulse.repeatCount = .infinity
                pulse.timingFunction = CAMediaTimingFunction(name: .easeOut)
                ring.add(pulse, forKey: "map.pulse")
            } else if !pulsing, ring.animation(forKey: "map.pulse") != nil { ring.removeAnimation(forKey: "map.pulse") }
        }
    }

    /// Moves a marker with the camera. Pointer events only set the
    /// position; an animated camera eases from what is on screen now.
    static func move(_ item: CALayer, to point: CGPoint, animated: Bool) {
        if animated {
            WorldMapLayerCoordinates.apply(item, position: point, transform: .identity, from: WorldMapLayerCoordinates.capture(item))
            return
        }
        if item.animation(forKey: "position") != nil { item.removeAnimation(forKey: "position") }
        if item.position != point { item.position = point }
    }

    /// A v1.2.0 marker in its units × `unitScale`: a dark contrast disc, the
    /// pulse ring, the ring and the dot; the player arrow's halo, beam and
    /// glyph are added the first time a pin becomes the player.
    private func makeMarker(_ id: UUID) -> CALayer {
        let k = Self.unitScale
        func circle(_ name: String, _ r: CGFloat, lineWidth: CGFloat = 0) -> CAShapeLayer {
            let shape = CAShapeLayer(); shape.name = name
            shape.path = CGPath(ellipseIn: CGRect(x: -r * k, y: -r * k, width: 2 * r * k, height: 2 * r * k), transform: nil)
            shape.lineWidth = lineWidth * k; shape.contentsScale = contentsScale
            return shape
        }
        let marker = CALayer()
        marker.name = "map.pin." + id.uuidString
        marker.bounds = CGRect(x: -24 * k, y: -24 * k, width: 48 * k, height: 48 * k)
        let contrast = circle("map.pin.contrast", 5.3)
        contrast.fillColor = CGColor(gray: 0.06, alpha: 0.82)
        let pulse = circle("map.pin.pulse", 8, lineWidth: 0.55), ring = circle("map.pin.ring", 4, lineWidth: 1.2)
        pulse.fillColor = nil; ring.fillColor = nil
        [contrast, pulse, ring, circle("map.pin.dot", 1.8)].forEach(marker.addSublayer)
        pinLayer.addSublayer(marker); markers[id] = marker
        return marker
    }

    private func style(_ marker: CALayer, as style: MapPinStyle) {
        let player = style == .player, color = WorldMapPinArtwork.color(for: style).cgColor, k = Self.unitScale
        if player, !(marker.sublayers ?? []).contains(where: { $0.name == "map.pin.player" }) {
            let halo = CALayer(); halo.name = "map.pin.playerHalo"
            halo.frame = CGRect(x: -24 * k, y: -26 * k, width: 48 * k, height: 52 * k)
            halo.contents = WorldMapPinArtwork.playerHalo; halo.opacity = 0.149
            // The original's narrow vertical beam (its material tint applied
            // once in the shared image), screen-blended over the map.
            let beam = CALayer(); beam.name = "map.pin.playerBeam"
            beam.frame = CGRect(x: -3 * k, y: -106 * k, width: 6 * k, height: 102 * k)
            beam.contents = WorldMapPinArtwork.playerBeam
            beam.compositingFilter = "screenBlendMode"; beam.opacity = 0.72
            let glyph = CALayer(); glyph.name = "map.pin.player"
            glyph.frame = CGRect(x: -12 * k, y: -12 * k, width: 24 * k, height: 24 * k)
            glyph.contents = WorldMapPinArtwork.playerGlyph; glyph.contentsGravity = .resizeAspect
            [halo, beam, glyph].forEach { $0.contentsScale = contentsScale }
            marker.insertSublayer(halo, at: 0); marker.addSublayer(beam); marker.addSublayer(glyph)
        }
        for item in marker.sublayers ?? [] {
            switch item.name {
            case "map.pin.contrast", "map.pin.ring": item.isHidden = player
            case "map.pin.dot": item.isHidden = player; (item as? CAShapeLayer)?.fillColor = color
            case "map.pin.pulse": break
            default: item.isHidden = !player
            }
            if item.name == "map.pin.ring" || item.name == "map.pin.pulse" { (item as? CAShapeLayer)?.strokeColor = color }
        }
    }

    private func removeAnimations(_ item: CALayer) {
        item.removeAllAnimations(); item.sublayers?.forEach(removeAnimations)
    }

    private func withoutActions(_ body: () -> Void) {
        CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit()
    }
}
