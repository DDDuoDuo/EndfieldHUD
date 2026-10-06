import Foundation

enum WorldMapGeometryTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func near(_ first: Double, _ second: Double) -> Bool { abs(first - second) < 0.000_001 }
        func samePoint(_ first: CGPoint, _ second: CGPoint) -> Bool {
            near(Double(first.x), Double(second.x)) && near(Double(first.y), Double(second.y))
        }
        func wrappedDifference(_ first: Double, _ second: Double) -> Double {
            let delta = first - second
            return abs(delta - delta.rounded())
        }

        let initial = WorldMapViewport()
        check(WorldMapGeometry.constrained(initial) == initial, "The Shenzhen default needs no polar adjustment")
        check(near(initial.centerX * 360 - 180, 114.0579) && near(90 - initial.centerY * 180, 22.5431) && initial.zoom == 3,
              "The starting map camera centers Shenzhen, China at 3×")
        let normalized = WorldMapGeometry.constrained(WorldMapViewport(centerX: -2.03, centerY: -10, zoom: 4))
        check(near(normalized.centerX, 0.97) && near(normalized.centerY, 0.25),
              "Longitude wraps across repeated worlds while the north pole stays within the viewport")
        let south = WorldMapGeometry.constrained(WorldMapViewport(centerX: 3.14, centerY: 10, zoom: 4))
        check(near(south.centerX, 0.14) && near(south.centerY, 0.75),
              "The south pole is constrained by the visible map height")
        for invalid in [WorldMapViewport(centerX: .nan), WorldMapViewport(centerY: .infinity), WorldMapViewport(zoom: -.infinity)] {
            check(WorldMapGeometry.constrained(invalid) == initial,
                  "A malformed saved view has a finite centered fallback")
        }

        let nearSeam = WorldMapViewport(centerX: 0.99, centerY: 0.5, zoom: 4)
        for camera in [nearSeam, WorldMapViewport(centerX: 0.01, centerY: 0.5, zoom: 128),
                       WorldMapViewport(centerX: 0.5, centerY: 0.5, zoom: 2.1)] {
            for point in [CGPoint(x: 42, y: 220), CGPoint(x: 403, y: 240), WorldMapGeometry.center] {
                let target = WorldMapGeometry.world(at: point, viewport: camera)
                let next = WorldMapGeometry.recentered(camera, at: point)
                check(next.zoom == camera.zoom && wrappedDifference(next.centerX, target.x) < 0.000001,
                      "Click recenter preserves zoom and the clicked longitude across either date-line seam")
                check(next == WorldMapGeometry.constrained(WorldMapViewport(centerX: target.x, centerY: target.y, zoom: camera.zoom)),
                      "Click recenter applies the same polar guard as other camera navigation")
            }
        }
        for invalid in [CGPoint(x: CGFloat.nan, y: 220), CGPoint(x: 0, y: 0), CGPoint(x: 440, y: 220)] {
            check(WorldMapGeometry.recentered(nearSeam, at: invalid) == nearSeam,
                  "Off-map and nonfinite clicks cannot move the camera")
        }
        let eastOfSeam = WorldMapGeometry.screen(x: 0.01, y: 0.5, viewport: nearSeam)
        check(near(Double(eastOfSeam.x), 255.2) && eastOfSeam.y == 220,
              "A pin just east of the antimeridian draws near a west-of-seam viewport")
        let westOfSeam = WorldMapGeometry.screen(x: 0.99, y: 0.5,
                                               viewport: WorldMapViewport(centerX: 0.01, centerY: 0.5, zoom: 4))
        check(near(Double(westOfSeam.x), 184.8), "The nearest wrapped pin copy is used in the other direction too")

        for zoom in [WorldMapViewport.minZoom, 8.0, 24.0, WorldMapViewport.maxZoom] {
            for centerX in [0.01, 0.5, 0.99] {
                let viewport = WorldMapViewport(centerX: centerX, centerY: 0.5, zoom: zoom)
                for point in [CGPoint(x: 45, y: 170), CGPoint(x: 220, y: 220), CGPoint(x: 385, y: 300)] {
                    let world = WorldMapGeometry.world(at: point, viewport: viewport)
                    let restored = WorldMapGeometry.screen(x: WorldMapStore.wrappedX(Double(world.x)),
                                                          y: Double(world.y), viewport: viewport)
                    check(samePoint(point, restored), "Screen/world round trips preserve pin position at every zoom and longitude seam")
                }
            }
        }

        for zoom in [WorldMapViewport.minZoom, 3.0, 24.0, WorldMapViewport.maxZoom] {
            for latitude in [-100.0, 0.0, 0.5, 1.0, 100.0] {
                let camera = WorldMapGeometry.constrained(WorldMapViewport(centerX: 0.99, centerY: latitude, zoom: zoom))
                let top = WorldMapGeometry.world(at: CGPoint(x: 220, y: 0), viewport: camera)
                let bottom = WorldMapGeometry.world(at: CGPoint(x: 220, y: WorldMapGeometry.size.height), viewport: camera)
                check(top.y >= -0.000_000_1 && bottom.y <= 1.000_000_1,
                      "The entire map frame remains filled with terrain at every zoom and either pole")
            }
        }

        let panningStart = WorldMapViewport(centerX: 0.02, centerY: 0.5, zoom: 4)
        let terrainBefore = WorldMapGeometry.world(at: CGPoint(x: 220, y: 220), viewport: panningStart)
        let panned = WorldMapGeometry.panned(panningStart, delta: CGPoint(x: 50, y: -23))
        let terrainAfter = WorldMapGeometry.screen(x: Double(terrainBefore.x), y: Double(terrainBefore.y), viewport: panned)
        check(panned.centerX > 0.99 && samePoint(terrainAfter, CGPoint(x: 270, y: 197)),
              "Dragging moves terrain with the pointer even while crossing the longitude seam")
        let unpanned = WorldMapGeometry.panned(panned, delta: CGPoint(x: -50, y: 23))
        check(wrappedDifference(unpanned.centerX, panningStart.centerX) < 0.000_001
              && near(unpanned.centerY, panningStart.centerY), "Opposite drags restore the original location without seam drift")
        for delta in [CGPoint(x: CGFloat.infinity, y: 0), CGPoint(x: 0, y: CGFloat.nan)] {
            check(WorldMapGeometry.panned(panningStart, delta: delta) == panningStart,
                  "Nonfinite drag events do not corrupt the saved viewport")
        }

        for point in [CGPoint(x: 89, y: 131), CGPoint(x: 349, y: 304), CGPoint(x: 220, y: 220)] {
            let anchor = WorldMapGeometry.world(at: point, viewport: nearSeam)
            let zoomed = WorldMapGeometry.zoomed(nearSeam, at: point, factor: 1.7)
            let anchorAfter = WorldMapGeometry.world(at: point, viewport: zoomed)
            check(wrappedDifference(Double(anchor.x), Double(anchorAfter.x)) < 0.000_001
                  && near(Double(anchor.y), Double(anchorAfter.y)),
                  "Zoom keeps the same geographic feature under an off-center pointer")
            let returned = WorldMapGeometry.zoomed(zoomed, at: point, factor: 1 / 1.7)
            check(near(returned.zoom, nearSeam.zoom)
                  && wrappedDifference(returned.centerX, nearSeam.centerX) < 0.000_001
                  && near(returned.centerY, nearSeam.centerY), "A zoom in/out pair restores the camera without accumulating drift")
        }
        let maximum = WorldMapGeometry.zoomed(nearSeam, at: WorldMapGeometry.center, factor: Double.greatestFiniteMagnitude)
        check(maximum.zoom == 128 && maximum.centerX.isFinite && maximum.centerY.isFinite,
              "An extreme zoom event clamps safely to the maximum detail level")
        let minimum = WorldMapGeometry.zoomed(nearSeam, at: WorldMapGeometry.center, factor: Double.leastNonzeroMagnitude)
        check(minimum.zoom == 2.1 && minimum.centerY == 0.5, "Zooming fully out stops before exposing the rectangular terrain boundary")
        for factor in [0.0, -1.0, Double.nan, Double.infinity] {
            check(WorldMapGeometry.zoomed(nearSeam, at: WorldMapGeometry.center, factor: factor) == nearSeam,
                  "Invalid zoom factors leave the visible map untouched")
        }
        check(WorldMapGeometry.zoomed(nearSeam, at: CGPoint(x: CGFloat.nan, y: 0), factor: 2) == nearSeam,
              "A malformed pointer coordinate cannot poison the zoom anchor")
        let poleZoom = WorldMapGeometry.zoomed(WorldMapViewport(centerX: 0.5, centerY: 0.25, zoom: 4),
                                               at: CGPoint(x: 220, y: 435), factor: 0.5)
        check(near(poleZoom.centerY, 1 / 2.1) && poleZoom.zoom == 2.1,
              "Polar limits take precedence over anchoring while zooming out")

        check(WorldMapGeometry.contains(WorldMapGeometry.center)
              && WorldMapGeometry.contains(CGPoint(x: 436, y: 220)), "The map accepts its center and visible circular edge")
        check(!WorldMapGeometry.contains(CGPoint(x: 0, y: 0))
              && !WorldMapGeometry.contains(CGPoint(x: 437, y: 220))
              && !WorldMapGeometry.contains(CGPoint(x: CGFloat.nan, y: 220)),
              "Pointer events in the surrounding HUD and malformed events cannot interact with terrain")
        check(WorldMapGeometry.coordinateDescription(x: 0.25, y: 0.25) == "45.00°N  90.00°W"
              && WorldMapGeometry.coordinateDescription(x: 0.75, y: 0.75) == "45.00°S  90.00°E",
              "Pin coordinates use the correct latitude and longitude hemispheres")
        check(WorldMapGeometry.coordinateDescription(x: 1.75, y: 0.75)
              == WorldMapGeometry.coordinateDescription(x: -0.25, y: 0.75),
              "Coordinate labels are identical for wrapped copies of the same location")
        return count
    }
}
