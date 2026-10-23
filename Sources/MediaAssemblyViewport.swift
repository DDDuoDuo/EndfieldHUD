import AppKit
import AVFoundation

/// View-only transforms. Zoom/pan never change output pixels or ask Core Image
/// to render; the same small preview texture is retained until an edit changes it.
struct MediaAssemblyViewport {
    private(set) var zoom: CGFloat = 1
    private(set) var pan = CGPoint.zero
    static let zoomRange: ClosedRange<CGFloat> = 1...20
    mutating func reset() { zoom = 1; pan = .zero }
    /// The media fitted into `viewport` (spec §3.10: 412 × zoom for a square
    /// picture), offset by the pan.
    func imageRect(size: CGSize, in viewport: CGRect) -> CGRect {
        guard size.width > 0, size.height > 0 else { return viewport }
        let fit = min(viewport.width / size.width, viewport.height / size.height) * zoom
        let width = size.width * fit, height = size.height * fit
        return CGRect(x: viewport.midX - width / 2 + pan.x, y: viewport.midY - height / 2 + pan.y, width: width, height: height)
    }
    /// Zooms keeping `point` still (wheel and pinch).
    mutating func magnify(by factor: CGFloat, at point: CGPoint, size: CGSize, in viewport: CGRect) {
        guard factor.isFinite, factor > 0 else { return }
        let old = imageRect(size: size, in: viewport), next = min(Self.zoomRange.upperBound, max(Self.zoomRange.lowerBound, zoom * factor))
        let ratio = next / zoom
        zoom = next
        pan.x = point.x - (point.x - old.midX) * ratio - viewport.midX
        pan.y = point.y - (point.y - old.midY) * ratio - viewport.midY
        clamp(size: size, in: viewport)
    }
    /// The zoom buttons and keys: the pan is kept and clamped (spec §3.10).
    mutating func zoom(by factor: CGFloat, size: CGSize, in viewport: CGRect) {
        guard factor.isFinite, factor > 0 else { return }
        zoom = min(Self.zoomRange.upperBound, max(Self.zoomRange.lowerBound, zoom * factor))
        clamp(size: size, in: viewport)
    }
    mutating func move(by delta: CGPoint, size: CGSize, in viewport: CGRect) {
        guard delta.x.isFinite, delta.y.isFinite else { return }
        pan.x += delta.x; pan.y += delta.y; clamp(size: size, in: viewport)
    }
    /// ±max(0, (side − stage) / 2) on each axis: no pan at zoom 1.
    private mutating func clamp(size: CGSize, in viewport: CGRect) {
        let rect = imageRect(size: size, in: viewport)
        let x = max(0, (rect.width - viewport.width) / 2), y = max(0, (rect.height - viewport.height) / 2)
        pan.x = min(x, max(-x, pan.x)); pan.y = min(y, max(-y, pan.y))
    }
    static func stickerRect(_ sticker: MediaAssemblySticker, in image: CGRect) -> CGRect {
        let size = sticker.kind.pixelSize
        let factor = min(image.width, image.height) * sticker.size / max(size.width, size.height)
        let width = size.width * factor, height = size.height * factor
        return CGRect(x: image.minX + image.width * sticker.x - width / 2,
                      y: image.minY + image.height * sticker.y - height / 2, width: width, height: height)
    }
    static func unrotate(_ point: CGPoint, around center: CGPoint, degrees: Double) -> CGPoint {
        let a = -degrees * .pi / 180, x = point.x-center.x, y = point.y-center.y
        return CGPoint(x: center.x + x*cos(a)-y*sin(a), y: center.y+x*sin(a)+y*cos(a))
    }
    /// The engine crops upright pixels first, mirrors second, then rotates clockwise.
    static func displayPoint(_ source: CGPoint, quarterTurns: Int, mirrored: Bool) -> CGPoint {
        let p = CGPoint(x: mirrored ? 1-source.x : source.x, y: source.y)
        switch (quarterTurns % 4 + 4) % 4 {
        case 1: return CGPoint(x: 1-p.y, y: p.x)
        case 2: return CGPoint(x: 1-p.x, y: 1-p.y)
        case 3: return CGPoint(x: p.y, y: 1-p.x)
        default: return p
        }
    }
    static func sourcePoint(_ display: CGPoint, quarterTurns: Int, mirrored: Bool) -> CGPoint {
        let p: CGPoint
        switch (quarterTurns % 4 + 4) % 4 {
        case 1: p = CGPoint(x: display.y, y: 1-display.x)
        case 2: p = CGPoint(x: 1-display.x, y: 1-display.y)
        case 3: p = CGPoint(x: 1-display.y, y: display.x)
        default: p = display
        }
        return CGPoint(x: mirrored ? 1-p.x : p.x, y: p.y)
    }
    static func mapRect(_ rect: CGRect, using transform: (CGPoint) -> CGPoint) -> CGRect {
        let points = [CGPoint(x:rect.minX,y:rect.minY),CGPoint(x:rect.maxX,y:rect.minY),CGPoint(x:rect.minX,y:rect.maxY),CGPoint(x:rect.maxX,y:rect.maxY)].map(transform)
        let xs = points.map(\.x), ys = points.map(\.y)
        return CGRect(x:xs.min()!,y:ys.min()!,width:xs.max()!-xs.min()!,height:ys.max()!-ys.min()!)
    }
    static func displayCrop(_ crop: MediaAssemblyCrop, quarterTurns: Int, mirrored: Bool) -> CGRect {
        mapRect(CGRect(x:crop.x,y:crop.y,width:crop.width,height:crop.height)) { displayPoint($0,quarterTurns:quarterTurns,mirrored:mirrored) }
    }
    static func sourceCrop(_ display: CGRect, quarterTurns: Int, mirrored: Bool) -> MediaAssemblyCrop {
        let r = mapRect(display) { sourcePoint($0,quarterTurns:quarterTurns,mirrored:mirrored) }
        let x = min(0.99,max(0,r.minX)),y = min(0.99,max(0,r.minY))
        return MediaAssemblyCrop(x:x,y:y,width:min(max(0.01,r.width),1-x),height:min(max(0.01,r.height),1-y))
    }

    /// A crop corner dragged to `point` (normalized display space); the box
    /// keeps at least .05 of each side (spec §3.10). Corners: 0 ⌜ 1 ⌝ 2 ⌞ 3 ⌟.
    static func dragCrop(_ crop: CGRect, corner: Int, to point: CGPoint) -> CGRect {
        let u = min(1, max(0, point.x)), v = min(1, max(0, point.y))
        var x0 = crop.minX, y0 = crop.minY, x1 = crop.maxX, y1 = crop.maxY
        if corner % 2 == 0 { x0 = min(u, x1 - 0.05) } else { x1 = max(u, x0 + 0.05) }
        if corner < 2 { y0 = min(v, y1 - 0.05) } else { y1 = max(v, y0 + 0.05) }
        return CGRect(x: x0, y: y0, width: x1 - x0, height: y1 - y0)
    }

    /// Top-left corners of the four 14 × 14 crop L-handles in the stage,
    /// offset −3 / −11 and clamped to 4 … stage − 18.
    static func cropHandleOrigins(_ crop: CGRect, image: CGRect, stage: CGSize) -> [CGPoint] {
        [(0, 0), (1, 0), (0, 1), (1, 1)].map { (u: CGFloat, v: CGFloat) in
            CGPoint(x: min(stage.width - 18, max(4, image.minX + (crop.minX + u * crop.width) * image.width - (u > 0 ? 11 : 3))),
                    y: min(stage.height - 18, max(4, image.minY + (crop.minY + v * crop.height) * image.height - (v > 0 ? 11 : 3))))
        }
    }

    /// Handle centres in the selected sticker's unrotated frame: ↻ 34 px above,
    /// × at (−22, −22) from the top-right, the corner squares at (−4, −4).
    static func handleCentres(size: CGSize) -> [(MediaAssemblyHandle, CGPoint)] {
        [(.rotate, CGPoint(x: size.width / 2, y: -25)), (.delete, CGPoint(x: size.width + 13, y: -13)),
         (.corner(0), CGPoint(x: -0.5, y: -0.5)), (.corner(1), CGPoint(x: size.width + 0.5, y: -0.5)),
         (.corner(2), CGPoint(x: -0.5, y: size.height + 0.5)), (.corner(3), CGPoint(x: size.width + 0.5, y: size.height + 0.5))]
    }

    static func handle(at point: CGPoint, sticker rect: CGRect, rotation: Double) -> MediaAssemblyHandle? {
        let local = unrotate(point, around: CGPoint(x: rect.midX, y: rect.midY), degrees: rotation)
        let p = CGPoint(x: local.x - rect.minX, y: local.y - rect.minY)
        return handleCentres(size: rect.size).first { handle, centre in
            switch handle {
            case .corner: return abs(p.x - centre.x) <= 7 && abs(p.y - centre.y) <= 7
            default: return hypot(p.x - centre.x, p.y - centre.y) <= 11
            }
        }?.0
    }
}

enum MediaAssemblyHandle: Equatable { case rotate, delete, corner(Int) }

/// What a rendered preview shows: its quarter turns, mirror and crop (the
/// full frame while the crop tool shows the uncropped picture).
struct MediaAssemblyGeometry: Equatable {
    var quarterTurns = 0, mirrored = false, crop = MediaAssemblyCrop()

    init(quarterTurns: Int = 0, mirrored: Bool = false, crop: MediaAssemblyCrop = MediaAssemblyCrop()) {
        self.quarterTurns = (quarterTurns % 4 + 4) % 4; self.mirrored = mirrored; self.crop = crop
    }
    init(_ adjustments: MediaAssemblyAdjustments, cropPreview: Bool) {
        self.init(quarterTurns: adjustments.rotationQuarterTurns, mirrored: adjustments.mirrored,
                  crop: cropPreview ? MediaAssemblyCrop() : adjustments.crop)
    }
}

/// The rotation and flips the stage lays over the shown preview, so a rotate
/// or mirror edit turns the picture at once (.25 s E) while the engine renders
/// it; the rendered preview then replaces it with no visible change.
struct MediaAssemblyPictureTransform: Equatable {
    /// Clockwise degrees, cumulative so repeated turns keep spinning forward.
    var angle: CGFloat = 0
    var scaleX: CGFloat = 1, scaleY: CGFloat = 1

    var swapsAxes: Bool { Int((angle / 90).rounded()) % 2 != 0 }
    var isIdentity: Bool { Int((angle / 90).rounded()) % 4 == 0 && scaleX == 1 && scaleY == 1 }

    /// The display change from `shown` to `target`: quarter turns, plus a
    /// horizontal reflection applied first when the mirror differs
    /// (R(t1)·M·R(−t0) = R(t1 + t0)·M).
    static func delta(from shown: MediaAssemblyGeometry, to target: MediaAssemblyGeometry) -> (turns: Int, reflected: Bool) {
        if shown.mirrored == target.mirrored { return (((target.quarterTurns - shown.quarterTurns) % 4 + 4) % 4, false) }
        return ((target.quarterTurns + shown.quarterTurns) % 4, true)
    }

    /// The representation of the change closest to this one: the least turning
    /// (clockwise on a tie), then the fewest flipped axes, so a mirror flips
    /// along one axis and a rotation keeps turning the same way.
    func nearest(turns: Int, reflected: Bool) -> MediaAssemblyPictureTransform {
        let base = CGFloat(turns) * 90
        let candidates: [(CGFloat, CGFloat, CGFloat)] = reflected ? [(base, -1, 1), (base + 180, 1, -1)] : [(base, 1, 1), (base + 180, -1, -1)]
        var best = self, bestCost = CGFloat.infinity
        for (degrees, sx, sy) in candidates {
            let below = degrees + 360 * ((angle - degrees) / 360).rounded(.down), above = below + 360
            let unwrapped = angle - below < above - angle ? below : above
            let cost = abs(unwrapped - angle) + (sx != scaleX ? 45 : 0) + (sy != scaleY ? 45 : 0)
            if cost < bestCost { bestCost = cost; best = MediaAssemblyPictureTransform(angle: unwrapped, scaleX: sx, scaleY: sy) }
        }
        return best
    }
}

/// The 448 × 412 stage (spec §3.10): dot grid, the edited picture (and the
/// video while it plays), stickers in image space, the selection handles, the
/// crop overlay, info chips, the zoom / playback / export bar and the empty
/// state. Built once per canvas and moved into each page; every change is an
/// in-place layer update.
final class MediaAssemblyStage {
    static let size = CGSize(width: 448, height: 412)
    static var bounds: CGRect { CGRect(origin: .zero, size: size) }
    let layer = HUDLayer.plain("media.stage")
    let zoomOut: HUDIconButton
    let zoomIn: HUDIconButton
    let zoomValue: MediaAssemblyZoomValue
    let play: HUDIconButton
    let pause: HUDIconButton
    let open: HUDButton
    private(set) var seek: HUDSlider?
    private let env: HUDDesignEnvironment
    private let grid = HUDLayer.configure(CAReplicatorLayer(), "media.grid")
    private let content = HUDLayer.configure(HUDDecorativeContentLayer(), "media.content")
    private let rotator = HUDLayer.plain("media.rotator")
    private let picture = HUDLayer.plain("media.picture")
    private var video: AVPlayerLayer?
    private let stickers = HUDLayer.configure(HUDDecorativeContentLayer(), "media.stickers")
    private var stickerLayers: [UUID: CALayer] = [:]
    private let crop = HUDLayer.plain("media.crop")
    private let cropShade = HUDLayer.shape("media.crop.shade")
    private let cropBox = HUDLayer.shape("media.crop.box")
    private let cropThirds = HUDLayer.shape("media.crop.thirds")
    private let cropHandles = (0..<4).map { _ in HUDLayer.shape("media.crop.handle") }
    private let selection = HUDLayer.plain("media.selection")
    private let outline = HUDLayer.plain("media.selection.outline")
    private var handles: [(MediaAssemblyHandle, CALayer)] = []
    private var hoveredHandle: MediaAssemblyHandle?
    private let chips = HUDLayer.plain("media.chips")
    private var chipTitles: [String] = []
    private let zoomGroup = HUDLayer.plain("media.zoom")
    private let playback = HUDLayer.plain("media.playback")
    private let time = HUDTextLayer(style: HUDTextStyle(size: 11, weight: .bold, color: HUDColor.textOnDark, tabular: true), alignment: .right)
    private let exportGroup = HUDLayer.plain("media.export")
    private let exportText = HUDTextLayer(style: HUDTextStyle(size: 11, weight: .medium, color: HUDColor.accentInk, tracking: 0.06,
                                                              uppercase: true, tabular: true))
    private let exportCells = (0..<10).map { _ in HUDLayer.plain("media.export.cell") }
    private let empty = HUDLayer.plain("media.empty")
    private let emptyTitle = HUDTextLayer(style: HUDType.emptyTitle, alignment: .center)
    private let emptySubtitle = HUDTextLayer(style: HUDType.labelCJK, alignment: .center)
    private var emptyScale: CGFloat = 0
    private var rotatorAngle: CGFloat = 0
    private(set) var transform = MediaAssemblyPictureTransform()
    private var pictureRect = CGRect.null
    private(set) var image: CGImage?
    private(set) var isEmpty = false

    init(env: HUDDesignEnvironment) {
        self.env = env
        zoomOut = HUDIconButton(id: "zoomOut", label: L10n.text("Zoom out", "缩小"), icon: .minus, env: env)
        zoomIn = HUDIconButton(id: "zoomIn", label: L10n.text("Zoom in", "放大"), icon: .plus, env: env)
        zoomValue = MediaAssemblyZoomValue(env: env)
        play = HUDIconButton(id: "play", label: L10n.text("Play", "播放"), icon: .play, env: env)
        pause = HUDIconButton(id: "pause", label: L10n.text("Pause", "暂停"), icon: .pause, env: env)
        open = HUDButton(id: "emptyOpen", title: L10n.text("Open", "打开"), icon: .open, kind: .primary, size: .small, env: env)
        let size = Self.size
        HUDMotion.perform {
            layer.bounds = Self.bounds; layer.masksToBounds = true
            layer.backgroundColor = HUDColor.tone(0x0C0C0C)
            // A layer border paints above the sublayers: the inset 1 px ring.
            layer.borderWidth = 1; layer.borderColor = HUDColor.ringField

            // Dot grid: radial-gradient(#2a2a2a 1px, transparent 1.4px) on a 16 px pitch.
            let row = HUDLayer.configure(CAReplicatorLayer())
            row.frame = CGRect(x: 0, y: 0, width: size.width, height: 16)
            row.instanceCount = Int(ceil(size.width / 16)); row.instanceTransform = CATransform3DMakeTranslation(16, 0, 0)
            let dot = HUDLayer.plain("media.grid.dot")
            dot.frame = CGRect(x: 8 - 1.2, y: 8 - 1.2, width: 2.4, height: 2.4); dot.cornerRadius = 1.2
            dot.backgroundColor = HUDColor.tone(0x2A2A2A)
            row.addSublayer(dot)
            grid.frame = Self.bounds
            grid.instanceCount = Int(ceil(size.height / 16)); grid.instanceTransform = CATransform3DMakeTranslation(0, 16, 0)
            grid.addSublayer(row)

            content.frame = Self.bounds
            picture.contentsGravity = .resize; picture.minificationFilter = .trilinear
            rotator.addSublayer(picture); content.addSublayer(rotator)
            stickers.frame = Self.bounds; content.addSublayer(stickers)

            crop.frame = Self.bounds; crop.isHidden = true
            cropShade.frame = Self.bounds; cropShade.fillRule = .evenOdd; cropShade.fillColor = HUDColor.black(0.5)
            cropBox.frame = Self.bounds; cropBox.fillColor = HUDColor.yellow
            cropThirds.frame = Self.bounds; cropThirds.fillColor = HUDColor.yellow.copy(alpha: 0.5)
            [cropShade, cropThirds, cropBox].forEach(crop.addSublayer)
            for handle in cropHandles {
                handle.bounds = CGRect(x: 0, y: 0, width: 14, height: 14); handle.anchorPoint = .zero
                handle.fillColor = HUDColor.yellow; handle.zPosition = 2
                crop.addSublayer(handle)
            }

            selection.isHidden = true
            outline.borderWidth = 1; outline.borderColor = HUDColor.yellow
            selection.addSublayer(outline)
            chips.frame = CGRect(x: 12, y: 12, width: size.width - 24, height: 18)

            zoomGroup.frame = CGRect(x: 10, y: size.height - 10 - 38, width: 134, height: 38)
            zoomGroup.backgroundColor = HUDColor.tone(0x0E0E0E, 0.82); zoomGroup.borderWidth = 1; zoomGroup.borderColor = HUDColor.ringField
            zoomOut.layer.frame.origin = CGPoint(x: 4, y: 4)
            zoomValue.layer.frame.origin = CGPoint(x: 38, y: 4)
            zoomIn.layer.frame.origin = CGPoint(x: 100, y: 4)
            [zoomOut.layer, zoomValue.layer, zoomIn.layer].forEach(zoomGroup.addSublayer)

            playback.frame = CGRect(x: 150, y: size.height - 10 - 38, width: size.width - 160, height: 38)
            playback.backgroundColor = zoomGroup.backgroundColor; playback.borderWidth = 1; playback.borderColor = HUDColor.ringField
            play.layer.frame.origin = CGPoint(x: 4, y: 4); pause.layer.frame.origin = CGPoint(x: 4, y: 4)
            time.lineFrame = CGRect(x: playback.bounds.width - 10 - 76, y: 13.5, width: 76, height: 11)
            [play.layer, pause.layer, time].forEach(playback.addSublayer)
            playback.isHidden = true

            exportGroup.backgroundColor = HUDColor.tone(0x0E0E0E, 0.86); exportGroup.borderWidth = 1; exportGroup.borderColor = HUDColor.ringField
            exportGroup.addSublayer(exportText)
            for cell in exportCells { exportGroup.addSublayer(cell) }
            exportGroup.isHidden = true

            empty.frame = Self.bounds; empty.isHidden = true
            emptyTitle.lineFrame = CGRect(x: 0, y: 165.25, width: size.width, height: 20)
            emptySubtitle.lineFrame = CGRect(x: 0, y: 195.25, width: size.width, height: 11.5)
            [emptyTitle, emptySubtitle, open.layer].forEach(empty.addSublayer)

            [grid, content, crop, selection, chips, zoomGroup, playback, exportGroup, empty].forEach(layer.addSublayer)
        }
        buildHandles()
        open.place(at: .zero)
        HUDMotion.perform { open.layer.frame.origin = CGPoint(x: ((size.width - open.layer.bounds.width) / 2).rounded(), y: 220.75) }
        zoomValue.setZoom(1)
        env.applyScale(to: layer)
    }

    /// The controls the page routes to (hidden ones never receive input).
    var controls: [HUDControl] { [zoomOut, zoomValue, zoomIn, play, pause] + (seek.map { [$0] } ?? []) + [open] }

    // MARK: Empty state

    /// `[ NO SOURCE ]` with its line and the Open button; hides everything else.
    func setEmpty(_ value: Bool, subtitle: String, subtitleLanguage: String?) {
        isEmpty = value
        HUDMotion.perform {
            empty.isHidden = !value
            [grid, content, chips, zoomGroup].forEach { $0.isHidden = value }
            // The canvas lays out crop, selection, playback and export again after this.
            if value { [crop, selection, playback, exportGroup].forEach { $0.isHidden = true } }
            let bracket = HUDType.emptyTitle.colored(HUDColor.accentInk)
            emptyTitle.runs = [HUDText.Run("[ ", bracket), HUDText.Run("NO SOURCE", HUDType.emptyTitle), HUDText.Run(" ]", bracket)]
            emptySubtitle.text = subtitle; emptySubtitle.language = subtitleLanguage
            if value, emptyScale != env.renderScale {
                emptyScale = env.renderScale
                empty.showArtwork(HUDArtwork.hatch(.light, size: Self.size, scale: env.renderScale), scale: env.renderScale)
            }
        }
        env.applyScale(to: empty)
    }

    // MARK: Picture

    /// Shows a newly rendered preview; filter and adjustment changes cross-fade (.12 s).
    func setImage(_ next: CGImage?, crossfade: Bool) {
        let previous = image
        image = next
        HUDMotion.perform { picture.contents = next }
        picture.removeAnimation(forKey: "media.contents")
        guard crossfade, previous != nil, next != nil, !env.reducesMotion else { return }
        let fade = CABasicAnimation(keyPath: "contents")
        fade.fromValue = previous; fade.toValue = next; fade.duration = 0.12; fade.timingFunction = HUDEasing.ease
        picture.add(fade, forKey: "media.contents")
    }

    /// Places the picture in `rect` with `transform` over the shown preview;
    /// `animated` plays the rotate / mirror transition (.25 s E).
    func layoutPicture(in rect: CGRect, transform next: MediaAssemblyPictureTransform, animated: Bool) {
        // Unchanged geometry must not cut a running transition short.
        guard rect != pictureRect || next != transform || animated else { return }
        transform = next; pictureRect = rect
        let local = next.swapsAxes ? CGSize(width: rect.height, height: rect.width) : rect.size
        let bounds = CGRect(origin: .zero, size: local), centre = CGPoint(x: rect.midX, y: rect.midY)
        let duration: CFTimeInterval = animated ? 0.25 : 0
        let targets = [picture] + (video.map { [$0] } ?? [])
        HUDMotion.animate(rotator, "bounds", to: NSValue(rect: bounds), duration: duration, env: env, key: "media.bounds")
        HUDMotion.animate(rotator, "position", to: NSValue(point: centre), duration: duration, env: env, key: "media.position")
        for item in targets {
            HUDMotion.animate(item, "bounds", to: NSValue(rect: bounds), duration: duration, env: env, key: "media.bounds")
            HUDMotion.animate(item, "position", to: NSValue(point: CGPoint(x: local.width / 2, y: local.height / 2)),
                              duration: duration, env: env, key: "media.position")
            HUDMotion.animate(item, "transform.scale.x", to: next.scaleX, duration: duration, env: env, key: "media.scaleX")
            HUDMotion.animate(item, "transform.scale.y", to: next.scaleY, duration: duration, env: env, key: "media.scaleY")
        }
        rotate(to: next.angle * .pi / 180, animated: animated)
    }

    /// Rotation keeps its own cumulative angle: Core Animation reads a turned
    /// layer's angle back only modulo 360°.
    private func rotate(to radians: CGFloat, animated: Bool) {
        var from = rotatorAngle
        if rotator.animation(forKey: "media.rotate") != nil,
           let shown = (rotator.presentation()?.value(forKeyPath: "transform.rotation.z") as? NSNumber).map({ CGFloat($0.doubleValue) }) {
            from = shown + 2 * .pi * ((rotatorAngle - shown) / (2 * .pi)).rounded()
        }
        rotatorAngle = radians
        HUDMotion.perform { rotator.setValue(radians, forKeyPath: "transform.rotation.z") }
        rotator.removeAnimation(forKey: "media.rotate")
        guard animated, !env.reducesMotion, abs(from - radians) > 0.0001 else { return }
        let turn = CABasicAnimation(keyPath: "transform.rotation.z")
        turn.fromValue = from; turn.toValue = radians; turn.duration = 0.25; turn.timingFunction = HUDEasing.standard
        rotator.add(turn, forKey: "media.rotate")
    }

    /// The playing video shares the picture's place; it exists only while a
    /// player does, so image editing never initializes AVFoundation layers.
    func setPlayer(_ player: AVPlayer?, playing: Bool) {
        if player != nil, video == nil {
            let output = AVPlayerLayer(); output.videoGravity = .resize
            HUDMotion.perform {
                output.bounds = picture.bounds; output.position = picture.position
                output.setValue(transform.scaleX, forKeyPath: "transform.scale.x")
                output.setValue(transform.scaleY, forKeyPath: "transform.scale.y")
                rotator.addSublayer(output)
            }
            video = output
        }
        HUDMotion.perform {
            video?.player = player
            video?.isHidden = !playing || player == nil
        }
    }

    /// Image, stickers and selection fade (.16 s): out before the source
    /// closes, in when a new source's first preview arrives.
    func setContentOpacity(_ value: Float, animated: Bool) {
        for item in [content, selection] {
            HUDMotion.animate(item, "opacity", to: value, duration: animated ? 0.16 : 0, timing: HUDEasing.ease, env: env, key: "media.fade")
        }
    }

    // MARK: Stickers, selection and crop

    /// Sticker artwork in image space; `base` is the output frame on the stage.
    func setStickers(_ items: [MediaAssemblySticker], base: CGRect) {
        let ids = Set(items.map(\.id))
        HUDMotion.perform {
            for id in Array(stickerLayers.keys) where !ids.contains(id) { stickerLayers.removeValue(forKey: id)?.removeFromSuperlayer() }
            for item in items {
                let artwork = stickerLayers[item.id] ?? {
                    let layer = HUDLayer.plain("media.sticker")
                    layer.contents = MediaAssemblyAssetCatalog.stickerImage(item.kind)
                    layer.contentsGravity = .resizeAspect; layer.minificationFilter = .trilinear
                    stickers.addSublayer(layer); stickerLayers[item.id] = layer
                    return layer
                }()
                let rect = MediaAssemblyViewport.stickerRect(item, in: base)
                artwork.bounds = CGRect(origin: .zero, size: rect.size)
                artwork.position = CGPoint(x: rect.midX, y: rect.midY)
                artwork.setAffineTransform(CGAffineTransform(rotationAngle: item.rotation * .pi / 180))
            }
            // Later stickers paint on top, in the model's order.
            for (index, item) in items.enumerated() { stickerLayers[item.id]?.zPosition = CGFloat(index) }
        }
    }

    /// The yellow outline with ↻, × and the corner squares, turned with the sticker.
    func setSelection(_ item: MediaAssemblySticker?, base: CGRect) {
        guard let item else { HUDMotion.perform { selection.isHidden = true }; setHovered(nil); return }
        let rect = MediaAssemblyViewport.stickerRect(item, in: base)
        HUDMotion.perform {
            selection.isHidden = isEmpty
            selection.bounds = CGRect(origin: .zero, size: rect.size)
            selection.position = CGPoint(x: rect.midX, y: rect.midY)
            selection.setAffineTransform(CGAffineTransform(rotationAngle: item.rotation * .pi / 180))
            outline.frame = selection.bounds
            for (kind, centre) in MediaAssemblyViewport.handleCentres(size: rect.size) {
                handles.first { $0.0 == kind }?.1.position = centre
            }
        }
    }

    private func buildHandles() {
        HUDMotion.perform {
            for (kind, _) in MediaAssemblyViewport.handleCentres(size: .zero) {
                let handle = HUDLayer.plain("media.handle")
                switch kind {
                case .corner:
                    // 7 × 7 white with a 1 px black ring outside it.
                    handle.bounds = CGRect(x: 0, y: 0, width: 9, height: 9); handle.backgroundColor = HUDColor.black(1)
                    let face = HUDLayer.plain(); face.frame = CGRect(x: 1, y: 1, width: 7, height: 7); face.backgroundColor = HUDColor.white(1)
                    handle.addSublayer(face)
                default:
                    // 18 px rgba(0,0,0,.85) circle with a 1 px #555 ring outside it.
                    handle.bounds = CGRect(x: 0, y: 0, width: 20, height: 20); handle.cornerRadius = 10
                    handle.borderWidth = 1; handle.borderColor = HUDColor.rgb(0x555555)
                    let face = HUDLayer.plain(); face.frame = CGRect(x: 1, y: 1, width: 18, height: 18); face.cornerRadius = 9
                    face.backgroundColor = HUDColor.black(0.85)
                    let glyph = HUDTextLayer(kind == .rotate ? "↻" : "×",
                                             style: HUDTextStyle(size: 12, weight: .regular, color: HUDColor.white(1), lineHeight: 1.5), alignment: .center)
                    glyph.lineFrame = CGRect(x: 0, y: 0, width: 18, height: 18)
                    face.addSublayer(glyph); handle.addSublayer(face)
                }
                selection.addSublayer(handle)
                handles.append((kind, handle))
            }
        }
    }

    /// Handles grow to 1.12 under the pointer (.2 s E).
    func setHovered(_ handle: MediaAssemblyHandle?) {
        guard handle != hoveredHandle else { return }
        hoveredHandle = handle
        for (kind, layer) in handles {
            HUDMotion.animate(layer, "transform.scale", to: kind == handle ? 1.12 : 1, duration: 0.2, env: env, key: "media.handle.hover")
        }
    }

    /// The crop overlay for a normalized display crop over `image`; nil hides it.
    func setCrop(_ rect: CGRect?, image: CGRect) {
        HUDMotion.perform {
            crop.isHidden = rect == nil || isEmpty
            guard let rect else { return }
            let box = CGRect(x: image.minX + rect.minX * image.width, y: image.minY + rect.minY * image.height,
                             width: rect.width * image.width, height: rect.height * image.height)
            let shade = CGMutablePath(); shade.addRect(Self.bounds); shade.addRect(box)
            cropShade.path = shade
            let border = CGMutablePath()
            border.addRect(CGRect(x: box.minX, y: box.minY, width: box.width, height: 1))
            border.addRect(CGRect(x: box.minX, y: box.maxY - 1, width: box.width, height: 1))
            border.addRect(CGRect(x: box.minX, y: box.minY, width: 1, height: box.height))
            border.addRect(CGRect(x: box.maxX - 1, y: box.minY, width: 1, height: box.height))
            cropBox.path = border
            let thirds = CGMutablePath()
            for fraction in [CGFloat(1) / 3, CGFloat(2) / 3] {
                thirds.addRect(CGRect(x: box.minX + box.width * fraction, y: box.minY + 1, width: 1, height: max(0, box.height - 2)))
                thirds.addRect(CGRect(x: box.minX + 1, y: box.minY + box.height * fraction, width: max(0, box.width - 2), height: 1))
            }
            cropThirds.path = thirds
            let corners: [HUDShape.Corners] = [.topLeft, .topRight, .bottomLeft, .bottomRight]
            for (index, origin) in MediaAssemblyViewport.cropHandleOrigins(rect, image: image, stage: Self.size).enumerated() {
                cropHandles[index].position = origin
                cropHandles[index].path = HUDShape.cornerMark(CGRect(x: 0, y: 0, width: 14, height: 14), length: 14, width: 3, corner: corners[index])
            }
        }
    }

    // MARK: Chips, zoom, playback, export

    /// Dark source name, yellow filter name and the outline `n/16` count.
    func setChips(source: String, filter: String?, stickers count: Int) {
        let titles = [source, filter ?? "", count > 0 ? "\(count)/16" : ""]
        guard titles != chipTitles else { return }
        chipTitles = titles
        let extra = [filter, count > 0 ? "\(count)/16" : nil].compactMap { $0 }.map { HUDText.width(HUDText.displayed($0, HUDType.chip), HUDType.chip) + 14 + 6 }
        let room = chips.bounds.width - extra.reduce(0, +) - 14
        let variants: [HUDChip.Variant] = [.dark, .yellow, .outline]
        HUDMotion.perform {
            chips.sublayers?.forEach { $0.removeFromSuperlayer() }
            var x: CGFloat = 0
            for (index, title) in titles.enumerated() where !title.isEmpty {
                let text = index == 0 ? Self.fitted(title, width: room) : title
                let chip = HUDChip(text, variant: variants[index], env: env)
                chip.layer.frame.origin = CGPoint(x: x, y: 0)
                chips.addSublayer(chip.layer)
                x += chip.layer.bounds.width + 6
            }
        }
    }

    /// Shortens a file name in the middle, keeping its extension, to `width`.
    static func fitted(_ name: String, width: CGFloat) -> String {
        let style = HUDType.chip
        func fits(_ text: String) -> Bool { HUDText.width(HUDText.displayed(text, style), style) <= width }
        guard !fits(name) else { return name }
        let ext = (name as NSString).pathExtension, stem = Array(ext.isEmpty ? name : (name as NSString).deletingPathExtension)
        let suffix = ext.isEmpty ? "" : "." + ext
        var low = 0, high = stem.count
        while low < high {
            let mid = (low + high + 1) / 2
            if fits(String(stem.prefix(mid)) + "…" + suffix) { low = mid } else { high = mid - 1 }
        }
        return String(stem.prefix(low)) + "…" + suffix
    }

    func setZoom(_ zoom: CGFloat) { zoomValue.setZoom(zoom) }

    /// Creates the seek slider for a video of `duration` seconds (nil: images).
    func configurePlayback(duration: Double?) -> HUDSlider? {
        guard let duration else {
            HUDMotion.perform { seek?.layer.removeFromSuperlayer(); playback.isHidden = true }
            seek = nil; return nil
        }
        if let seek, seek.range.maximum == max(0.1, duration) { return seek }
        let slider = HUDSlider(id: "seek", label: L10n.text("Playback position", "播放进度"), value: 0,
                               range: HUDSlider.Range(minimum: 0, maximum: max(0.1, duration), step: 0.01), format: .clock,
                               width: playback.bounds.width - 42 - 8 - 76 - 10, dark: true, showsReadout: false, env: env)
        HUDMotion.perform {
            seek?.layer.removeFromSuperlayer()
            slider.layer.frame.origin = CGPoint(x: 42, y: 4)
            playback.addSublayer(slider.layer)
        }
        seek = slider
        return slider
    }

    func setPlayback(visible: Bool, playing: Bool, time: Double, duration: Double) {
        // While the seek thumb is dragged the readout follows it.
        let current = seek.map { $0.isDragging ? $0.value : time } ?? time
        HUDMotion.perform {
            playback.isHidden = !visible || isEmpty
            play.layer.isHidden = playing; pause.layer.isHidden = !playing
            self.time.text = HUDFormat.clock(seconds: Int(current.rounded(.down))) + " / " + HUDFormat.clock(seconds: Int(duration.rounded(.up)))
        }
        if let seek, !seek.isDragging { seek.setValue(current) }
    }

    /// `EXPORTING n%` with ten segments, right-aligned in the bar; nil hides it.
    func setExport(_ progress: Double?, title: String) {
        HUDMotion.perform {
            exportGroup.isHidden = progress == nil || isEmpty
            guard let progress else { return }
            let percent = Int((min(1, max(0, progress)) * 100).rounded(.down))
            exportText.text = title + " \(percent)%"
            let style = exportText.textStyle
            let textWidth = ceil(HUDText.width(HUDText.displayed(title + " 100%", style), style))
            let width = 10 + textWidth + 8 + 110 + 10
            exportGroup.frame = CGRect(x: Self.size.width - 10 - width, y: Self.size.height - 10 - 38 + 6.75, width: width, height: 24.5)
            exportText.lineFrame = CGRect(x: 10, y: 6.75, width: textWidth + 1, height: 11)
            let cell = (110 - 9 * 2) / CGFloat(10)
            for (index, layer) in exportCells.enumerated() {
                layer.frame = CGRect(x: 10 + textWidth + 8 + CGFloat(index) * (cell + 2), y: 10.25, width: cell, height: 4)
                layer.backgroundColor = Double(index) < Double(percent) / 10 ? HUDColor.yellow : HUDColor.separator
            }
        }
        env.applyScale(to: exportText)
    }

    var exportTextForTesting: String? { exportGroup.isHidden ? nil : exportText.text }
    var chipTitlesForTesting: [String] { chipTitles.filter { !$0.isEmpty } }
    var isCropVisible: Bool { !crop.isHidden }
    var isSelectionVisible: Bool { !selection.isHidden }
    var hasVideoLayer: Bool { video != nil }
}
