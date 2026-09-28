import AppKit
import QuartzCore

enum WorldMapRasterControllerTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !value { fatalError(message, file: file, line: line) }
        }
        func wait(_ condition: () -> Bool, timeout: TimeInterval = 3) -> Bool {
            let end = Date().addingTimeInterval(timeout)
            while !condition(), Date() < end { RunLoop.current.run(until: Date().addingTimeInterval(0.005)) }
            return condition()
        }
        func drain(_ interval: TimeInterval = 0.16) { RunLoop.current.run(until: Date().addingTimeInterval(interval)) }
        func near(_ a: CGFloat, _ b: CGFloat) -> Bool { abs(a - b) < 0.000_001 }
        func layerCount(_ layer: CALayer) -> Int { 1 + (layer.sublayers ?? []).reduce(0) { $0 + layerCount($1) } }
        func imageBytes(_ frame: WorldMapRasterFrame?) -> Data? {
            guard let image = frame?.image, let data = image.dataProvider?.data else { return nil }
            return data as Data
        }
        func fixture(_ bounds: CGRect, id: String) throws -> WorldMapCountries {
            var bytes = Data("EHUDCTY1".utf8)
            func u32(_ value: UInt32) { for index in 0..<4 { bytes.append(UInt8(truncatingIfNeeded: value >> (index * 8))) } }
            func u16(_ value: UInt16) { bytes.append(UInt8(truncatingIfNeeded: value)); bytes.append(UInt8(value >> 8)) }
            u32(1); bytes.append(UInt8(id.utf8.count)); bytes.append(contentsOf: id.utf8)
            u16(UInt16(id.utf8.count)); bytes.append(contentsOf: id.utf8)
            u32(1); u32(1); u32(4)
            for p in [CGPoint(x: bounds.minX, y: bounds.minY), CGPoint(x: bounds.maxX, y: bounds.minY),
                      CGPoint(x: bounds.maxX, y: bounds.maxY), CGPoint(x: bounds.minX, y: bounds.maxY)] {
                u32(UInt32((p.x * 16777215).rounded())); u32(UInt32((p.y * 16777215).rounded()))
            }
            return try WorldMapCountries(data: bytes)
        }
        do {
            let land = try fixture(CGRect(x: 0.3, y: 0.3, width: 0.4, height: 0.4), id: "ONE")
            let island = try fixture(CGRect(x: 0.43, y: 0.44, width: 0.14, height: 0.12), id: "TWO")
            let initial = WorldMapViewport(centerX: 0.5, centerY: 0.5, zoom: 4)
            let controller = WorldMapRasterController()
            controller.setData(terrain: nil, countries: land)
            controller.configure(dark: true, accent: NSColor.yellow.cgColor, contentsScale: 1)
            controller.update(viewport: initial)
            drain()
            check(!controller.isPainting && controller.frame == nil && controller.completedPaintCount == 0,
                  "An inactive map does not start background drawing when its data or camera changes")
            controller.activate()
            check(controller.isPainting, "Activating a populated map starts its first asynchronous paint")
            check(wait { controller.frame?.viewport == initial && !controller.isPainting },
                  "The first map texture arrives through the main run loop without a window")
            check(controller.completedPaintCount == 1 && layerCount(controller.layer) == 5,
                  "A detailed map retains one texture and three shared background image layers")

            let before = controller.completedPaintCount
            var slightlyMoved = initial; slightlyMoved.centerX += 0.001
            controller.update(viewport: slightlyMoved)
            check(controller.frame?.viewport == initial && !controller.isPainting,
                  "A small pan reuses padded pixels immediately instead of repainting every camera sample")
            let detail = controller.layer.sublayers!.first { $0.name == "map.raster.detail" }!
            let frame = controller.frame!
            let point = CGPoint(x: 0.51, y: 0.49)
            let sourceScreen = WorldMapGeometry.screen(x: point.x, y: point.y, viewport: frame.viewport)
            let inTexture = CGPoint(x: sourceScreen.x - frame.screenRect.minX, y: sourceScreen.y - frame.screenRect.minY)
            let actual = detail.convert(inTexture, to: controller.layer)
            let expected = WorldMapGeometry.screen(x: point.x, y: point.y, viewport: slightlyMoved)
            check(near(actual.x, expected.x) && near(actual.y, expected.y),
                  "The retained texture moves on the same geographic plane as pin positions while a repaint is pending")
            controller.finishGesture()
            check(wait { controller.frame?.viewport == slightlyMoved && !controller.isPainting },
                  "Gesture end produces an exact camera texture even when the old padding already covered the view")
            check(controller.completedPaintCount == before + 1,
                  "Finishing one small pan schedules one exact paint")

            let countBeforeBurst = controller.completedPaintCount
            var latest = slightlyMoved
            for index in 0..<1000 {
                latest = WorldMapViewport(centerX: 0.5 + sin(Double(index) * 0.07) * 0.02,
                    centerY: 0.5, zoom: 2.1 + Double(index % 113))
                controller.update(viewport: latest)
            }
            check(controller.completedPaintCount == countBeforeBurst && controller.frame?.viewport != latest,
                  "A burst of camera input schedules asynchronous replacement without drawing synchronously for each event")
            check(layerCount(controller.layer) == 5, "One thousand camera updates do not add compositor layers")
            controller.finishGesture()
            check(wait { controller.frame?.viewport == latest && !controller.isPainting },
                  "Backpressure eventually paints the newest camera instead of replaying the thousand intermediate inputs")
            check(controller.completedPaintCount <= countBeforeBurst + 2,
                  "The burst produces at most the in-flight image plus the latest requested image")
            let retainedImages = Set((controller.layer.sublayers ?? []).compactMap { $0.contents.map { ObjectIdentifier($0 as AnyObject) } })
            check(retainedImages.count <= 2, "Backdrop copies share one image rather than retaining separate world textures")

            // All revisions are issued before returning to the main loop. Even
            // if an obsolete worker result finishes quickly, it cannot publish.
            let stale = WorldMapRasterController()
            stale.update(viewport: initial); stale.setData(terrain: nil, countries: land); stale.activate()
            stale.configure(dark: false, accent: NSColor.cyan.cgColor, contentsScale: 1)
            stale.setData(terrain: nil, countries: island)
            check(wait { stale.frame != nil && !stale.isPainting }, "A superseding data/theme request finishes after cancelling the obsolete paint")
            let expectedImage = WorldMapRasterPainter(terrain: nil, countries: island).render(
                WorldMapRasterRequest(viewport: initial, dark: false, accent: NSColor.cyan.cgColor, contentsScale: 1))
            check(stale.completedPaintCount == 1 && imageBytes(stale.frame) == imageBytes(expectedImage),
                  "Only the newest country data and theme pixels are published; stale worker results are discarded")
            let stableCount = stale.completedPaintCount
            for _ in 0..<20 { stale.configure(dark: false, accent: NSColor.cyan.cgColor, contentsScale: 1); stale.update(viewport: initial) }
            drain()
            check(stale.completedPaintCount == stableCount && !stale.isPainting,
                  "Unchanged configuration and camera do not create an idle repaint loop")

            let oldImage = imageBytes(stale.frame), published = stale.completedPaintCount
            stale.configure(dark: true, accent: NSColor.magenta.cgColor, contentsScale: 1)
            stale.deactivate()
            check(wait { !stale.isPainting }, "A closed map lets its cancelled in-flight worker finish cleanup")
            drain()
            check(stale.completedPaintCount == published && imageBytes(stale.frame) == oldImage,
                  "Closing during a paint prevents late publication while retaining the previous closing-animation texture")
            stale.activate()
            check(wait { stale.completedPaintCount == published + 1 && !stale.isPainting },
                  "Reopening resumes the latest unpainted revision after worker cache cleanup")
            let reopened = WorldMapRasterPainter(terrain: nil, countries: island).render(
                WorldMapRasterRequest(viewport: initial, dark: true, accent: NSColor.magenta.cgColor, contentsScale: 1))
            check(imageBytes(stale.frame) == imageBytes(reopened), "Reopening cannot resurrect pixels from the cancelled theme")

            let interrupted = WorldMapRasterController()
            interrupted.update(viewport: initial); interrupted.setData(terrain: nil, countries: land)
            interrupted.activate(); interrupted.deactivate(); interrupted.activate()
            check(wait { interrupted.frame != nil && !interrupted.isPainting },
                  "Immediate close/reopen during the first paint does not strand the controller in its busy state")
            check(interrupted.completedPaintCount == 1, "The cancelled first session never publishes into the reopened map")

            let quality = WorldMapRasterController()
            quality.configure(dark: true, accent: NSColor.yellow.cgColor, contentsScale: 2.2)
            quality.setData(terrain: nil, countries: island); quality.update(viewport: initial); quality.activate()
            check(wait { quality.isSettled } && quality.frame!.pixelsPerPoint >= 2.19,
                  "An idle map paints at the configured sharp texture resolution")
            let sharpCount = quality.completedPaintCount
            let magnified = WorldMapViewport(centerX: 0.5, centerY: 0.5, zoom: 8)
            quality.update(viewport: magnified)
            check(wait { quality.completedPaintCount > sharpCount && !quality.isPainting },
                  "Continuous camera movement receives a replacement texture without waiting for gesture end")
            check(quality.frame!.pixelsPerPoint <= 1.401,
                  "A moving map uses the bounded lower-resolution texture budget")
            quality.finishGesture()
            check(wait { quality.isSettled } && quality.frame!.viewport == magnified && quality.frame!.pixelsPerPoint >= 2.19,
                  "Stopping restores exact final geography and full detail even when the low-resolution frame matched the camera")
            drain(0.5)
            let correctImage = quality.frame!.image, correctCount = quality.completedPaintCount
            quality.update(viewport: WorldMapViewport(centerX: 0.52, centerY: 0.5, zoom: 12))
            check(quality.isPainting, "The away-and-back regression starts with an actual in-flight obsolete camera paint")
            quality.update(viewport: magnified); quality.finishGesture()
            check(wait { quality.isSettled }, "Returning to an already-correct retained frame drains obsolete work and settles")
            check(quality.completedPaintCount == correctCount && quality.frame!.image === correctImage,
                  "An obsolete completed frame cannot replace the correct retained image after the camera moves away and back")

            var disposable: WorldMapRasterController? = WorldMapRasterController()
            weak var released = disposable
            disposable?.setData(terrain: nil, countries: land); disposable?.activate(); disposable = nil
            check(released == nil, "The background worker and scheduled callbacks do not retain a discarded map controller")
            drain()

            // Crossing ±180° must choose the nearest wrapped copy rather than
            // throwing the detailed texture one complete world away from pins.
            let source = WorldMapViewport(centerX: 0.995, centerY: 0.5, zoom: 8)
            let destination = WorldMapViewport(centerX: 0.005, centerY: 0.51, zoom: 16)
            let seamFrame = WorldMapRasterFrame(image: frame.image, viewport: source, screenRect: frame.screenRect,
                worldRect: frame.worldRect, pixelsPerPoint: 1, geometryMilliseconds: 0, drawingMilliseconds: 0)
            let placement = WorldMapRasterController.placement(of: seamFrame, in: destination)
            let seamPin = CGPoint(x: 0.998, y: 0.5)
            let prior = WorldMapGeometry.screen(x: seamPin.x, y: seamPin.y, viewport: source)
            let transported = CGPoint(x: placement.position.x + (prior.x - seamFrame.screenRect.minX) * placement.scale,
                y: placement.position.y + (prior.y - seamFrame.screenRect.minY) * placement.scale)
            let destinationPin = WorldMapGeometry.screen(x: seamPin.x, y: seamPin.y, viewport: destination)
            check(near(transported.x, destinationPin.x) && near(transported.y, destinationPin.y),
                  "Pan/zoom across the antimeridian keeps raster land and geographic pins aligned")
            controller.deactivate(); stale.deactivate(); interrupted.deactivate(); quality.deactivate()
            drain()
        } catch { fatalError("Raster controller fixture failed: \(error)") }
        return count
    }
}
