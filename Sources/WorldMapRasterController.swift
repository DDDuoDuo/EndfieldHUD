import AppKit
import QuartzCore

/// A single worker and one retained detail image. Camera events only transform
/// four image layers; superseded requests never form a rendering queue.
final class WorldMapRasterController {
    let layer = CALayer()
    private let detail = CALayer()
    private let backgrounds = (-1...1).map { _ in CALayer() }
    private let queue = DispatchQueue(label: "EndfieldHUD.map-paint", qos: .userInitiated)
    private let worker = Worker()
    private var token = Cancellation()
    private var scheduled: DispatchWorkItem?
    private var terrain: WorldMapTerrain?
    private var countries: WorldMapCountries?
    private var revision = 0
    private var frameRevision = -1
    private var backdropRevision = -1
    private var dark = true
    private var accent = NSColor.yellow.cgColor
    private var contentsScale: CGFloat = 2
    private var viewport = WorldMapViewport()
    private(set) var frame: WorldMapRasterFrame?
    private(set) var completedPaintCount = 0
    private(set) var isPainting = false
    private var active = false
    private var interacting = false
    private var exactRequested = false
    private var lastPaintFinished: TimeInterval = 0
    private var cooldown: TimeInterval = 0.125
    private var cameraAnimationEnds: TimeInterval = 0
    var isSettled: Bool { frame?.viewport == viewport && frameRevision == revision && !isPainting && scheduled == nil && !needsPaint }

    private final class Cancellation {
        private let lock = NSLock()
        private var cancelled = false
        func cancel() { lock.lock(); cancelled = true; lock.unlock() }
        var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    }
    /// Mutable geometry caches are confined to queue, including their teardown.
    private final class Worker {
        var painter: WorldMapRasterPainter?
        var revision = -1
    }
    init() {
        layer.name = "map.raster"; layer.frame = CGRect(origin: .zero, size: WorldMapGeometry.size)
        for image in backgrounds + [detail] {
            image.anchorPoint = .zero; image.contentsGravity = .resize
            image.minificationFilter = .linear; image.magnificationFilter = .linear
            image.name = image === detail ? "map.raster.detail" : "map.raster.backdrop"
            layer.addSublayer(image)
        }
    }
    deinit { scheduled?.cancel(); token.cancel() }

    func setData(terrain: WorldMapTerrain?, countries: WorldMapCountries?) {
        self.terrain = terrain; self.countries = countries
        invalidate()
    }
    func configure(dark: Bool, accent: CGColor, contentsScale: CGFloat) {
        let scale = min(2.2, max(1, contentsScale))
        guard self.dark != dark || self.accent != accent || self.contentsScale != scale else { return }
        self.dark = dark; self.accent = accent; self.contentsScale = scale
        invalidate()
    }
    private func invalidate() {
        revision += 1; token.cancel(); token = Cancellation()
        exactRequested = true
        requestPaint(immediate: true)
    }
    func activate() {
        active = true
        requestPaint(immediate: true)
    }
    func deactivate() {
        active = false; interacting = false; scheduled?.cancel(); scheduled = nil
        token.cancel(); token = Cancellation()
        (backgrounds + [detail]).forEach { $0.removeAllAnimations() }
        // Keep the small last image for the shell's closing/reopening animation,
        // but release geographic working caches once the in-flight paint ends.
        queue.async { [worker] in worker.painter = nil; worker.revision = -1 }
    }
    func update(viewport: WorldMapViewport, animated: Bool = false) {
        if self.viewport != viewport {
            exactRequested = false
            if active { interacting = !animated }
        }
        self.viewport = viewport
        cameraAnimationEnds = animated ? CACurrentMediaTime() + 0.18 : 0
        applyCamera(animated: animated)
        requestPaint()
    }
    func finishGesture() {
        interacting = false
        exactRequested = frame?.viewport != viewport || frameRevision != revision
        // Coalesce repeated arrow keys and end-phase notifications. The camera
        // and persistence are already current; only final detail waits briefly.
        cameraAnimationEnds = max(cameraAnimationEnds, CACurrentMediaTime() + 0.08)
        requestPaint(immediate: true)
    }

    /// Screen-space placement also handles crossing the antimeridian. Pins use
    /// the same nearest-copy projection, keeping markers attached while painting.
    static func placement(of frame: WorldMapRasterFrame, in viewport: WorldMapViewport) -> (position: CGPoint, scale: CGFloat) {
        let ratio = CGFloat(viewport.zoom / frame.viewport.zoom)
        var dx = frame.viewport.centerX - viewport.centerX; dx -= dx.rounded()
        return (CGPoint(x: 220 + (frame.screenRect.minX - 220) * ratio + dx * 440 * viewport.zoom,
                        y: 220 + (frame.screenRect.minY - 220) * ratio + (frame.viewport.centerY - viewport.centerY) * 220 * viewport.zoom), ratio)
    }
    private func applyCamera(animated: Bool = false) {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        if let frame {
            let p = Self.placement(of: frame, in: viewport)
            let start = animated ? WorldMapLayerCoordinates.capture(detail) : nil
            WorldMapLayerCoordinates.apply(detail, position: p.position,
                transform: CGAffineTransform(scaleX: p.scale, y: p.scale), from: start)
        }
        let factor = CGFloat(440 * viewport.zoom) / 1024
        for (index, image) in backgrounds.enumerated() {
            let start = animated && image.contents != nil ? WorldMapLayerCoordinates.capture(image) : nil
            image.bounds = CGRect(origin: .zero, size: WorldMapTerrain.worldSize)
            WorldMapLayerCoordinates.apply(image,
                position: CGPoint(x: 220 + (Double(index - 1) - viewport.centerX) * 440 * viewport.zoom,
                                  y: 220 - viewport.centerY * 220 * viewport.zoom),
                transform: CGAffineTransform(scaleX: factor, y: factor), from: start)
        }
        CATransaction.commit()
    }
    private var needsPaint: Bool {
        guard let frame, frameRevision == revision else { return true }
        if !interacting && frame.pixelsPerPoint + 0.002 < contentsScale { return true }
        if exactRequested { return frame.viewport != viewport }
        let p = Self.placement(of: frame, in: viewport)
        if p.scale < 0.84 || p.scale > 1.18 { return true }
        let coverage = CGRect(origin: p.position, size: CGSize(width: frame.screenRect.width * p.scale,
                                                              height: frame.screenRect.height * p.scale))
        return !coverage.contains(CGRect(origin: .zero, size: WorldMapGeometry.size).insetBy(dx: -36, dy: -36))
    }
    private func requestPaint(immediate: Bool = false) {
        guard active, terrain != nil || countries != nil else { return }
        guard needsPaint else { scheduled?.cancel(); scheduled = nil; return }
        guard !isPainting else { return }
        if immediate { scheduled?.cancel(); scheduled = nil }
        guard scheduled == nil else { return }
        let now = CACurrentMediaTime()
        let delay = max(cameraAnimationEnds - now, immediate ? 0 : max(0, cooldown - (now - lastPaintFinished)))
        if delay < 0.001 { startPaint(); return }
        let task = DispatchWorkItem { [weak self] in self?.scheduled = nil; self?.startPaint() }
        scheduled = task; DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: task)
    }
    private func startPaint() {
        guard active, !isPainting, needsPaint else { return }
        if cameraAnimationEnds > CACurrentMediaTime() { requestPaint(); return }
        isPainting = true
        // Motion uses a smaller texture; once input stops, one sharp image
        // replaces it. Labels and markers are native layers at full resolution.
        let request = WorldMapRasterRequest(viewport: viewport, dark: dark, accent: accent,
                                            contentsScale: interacting ? min(1.4, contentsScale) : contentsScale)
        let version = revision, cancellation = token, needsBackdrop = backdropRevision != revision
        let terrain = terrain, countries = countries
        queue.async { [weak self, worker] in
            let started = CACurrentMediaTime()
            let result: (WorldMapRasterFrame?, CGImage?) = autoreleasepool {
                guard !cancellation.isCancelled else { return (nil, nil) }
                if worker.revision != version || worker.painter == nil {
                    worker.painter = WorldMapRasterPainter(terrain: terrain, countries: countries); worker.revision = version
                }
                let backdrop = needsBackdrop ? worker.painter?.renderBackdrop(dark: request.dark, isCancelled: { cancellation.isCancelled }) : nil
                return (worker.painter?.render(request, isCancelled: { cancellation.isCancelled }), backdrop)
            }
            let elapsed = CACurrentMediaTime() - started
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.isPainting = false; self.lastPaintFinished = CACurrentMediaTime()
                // Rendering gets at most a seventeenth of one core during sustained
                // camera movement. Existing pixels continue tracking every event.
                self.cooldown = max(0.125, elapsed * 16)
                // Allocation failure should not create an automatic retry loop.
                // A later camera/style change can try again with fresh state.
                if result.0 == nil && !cancellation.isCancelled { return }
                // A toolbar action may have begun after this job started.
                // Replacing its coordinate frame mid-animation would snap the
                // terrain ahead of the still-animating native markers.
                if self.cameraAnimationEnds > CACurrentMediaTime() {
                    self.requestPaint(immediate: true); return
                }
                if self.active, !cancellation.isCancelled, version == self.revision, let frame = result.0 {
                    // A gesture can return to an already-current texture while
                    // an older camera is still being painted. Keep the correct
                    // image rather than replacing it and painting it again.
                    if let current = self.frame, self.frameRevision == version,
                       current.viewport == self.viewport, current.pixelsPerPoint + 0.002 >= self.contentsScale,
                       frame.viewport != self.viewport {
                        self.exactRequested = false; self.requestPaint(); return
                    }
                    CATransaction.begin(); CATransaction.setDisableActions(true)
                    self.frame = frame; self.frameRevision = version; self.completedPaintCount += 1
                    self.detail.removeAllAnimations()
                    self.detail.bounds = CGRect(origin: .zero, size: frame.screenRect.size)
                    self.detail.contents = frame.image
                    if let backdrop = result.1 {
                        self.backdropRevision = version
                        self.backgrounds.forEach { $0.contents = backdrop }
                    }
                    self.applyCamera(); CATransaction.commit()
                    if frame.viewport == self.viewport { self.exactRequested = false }
                }
                // Only inspect the latest camera, never replay old wheel samples.
                self.requestPaint(immediate: self.exactRequested)
            }
        }
    }
}
