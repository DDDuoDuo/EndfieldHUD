import Foundation
import CoreGraphics

enum WorldMapPathGeometryTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func vertices(_ path: CGPath) -> Int {
            var result = 0
            path.applyWithBlock { if $0.pointee.type != .closeSubpath { result += 1 } }
            return result
        }
        func pointsStayInside(_ path: CGPath, _ rect: CGRect) -> Bool {
            var valid = true
            path.applyWithBlock {
                let element = $0.pointee
                if element.type == .moveToPoint || element.type == .addLineToPoint {
                    let p = element.points[0]
                    valid = valid && p.x.isFinite && p.y.isFinite && rect.insetBy(dx: -0.000_001,dy: -0.000_001).contains(p)
                }
            }
            return valid
        }
        let viewport = CGRect(x: 10,y: 10,width: 100,height: 100)
        var window = WorldMapGeometryWindow()
        check(window.update(visibleBounds: viewport, scale: 4), "The first camera creates a finite padded working set")
        let firstBounds = window.bounds
        check(!window.update(visibleBounds: viewport.offsetBy(dx: 10,dy: 5),scale: 4) && window.bounds == firstBounds,
              "Panning within padding retains the same geometry window")
        check(window.update(visibleBounds: viewport.offsetBy(dx: 80,dy: 5),scale: 4), "Leaving padding refreshes the working set")
        check(window.update(visibleBounds: viewport,scale: 8) && window.tolerance*8 <= 0.28,
              "Zooming chooses a subpixel level of detail")
        check(!window.update(visibleBounds: .null,scale: 1) && !window.update(visibleBounds: viewport,scale: .nan),
              "Invalid camera geometry cannot replace the working set")

        let rectangle = CGPath(rect: CGRect(x: -100,y: -100,width: 200,height: 200),transform: nil)
        let region = CGRect(x: -10,y: -10,width: 20,height: 20)
        let clippedRectangle = WorldMapPathGeometry(rectangle).clippedPolygon(to: region,tolerance: 0)
        check(clippedRectangle.fillPath.contains(.zero,using: .evenOdd), "A country enclosing the viewport stays filled even when all original vertices are offscreen")
        check(clippedRectangle.linePath.isEmpty, "Viewport clipping never invents a coastline around an enclosing country")
        check(pointsStayInside(clippedRectangle.fillPath,region), "The clipped fill has no huge offscreen coordinates")

        let donut = CGMutablePath()
        donut.addRect(CGRect(x: -100,y: -100,width: 200,height: 200))
        donut.addRect(CGRect(x: -3,y: -20,width: 6,height: 40))
        let donutGeometry = WorldMapPathGeometry(donut).clippedPolygon(to: region,tolerance: 0.01)
        for x in [-9.0,-2.0,0.0,2.0,9.0] {
            for y in [-9.0,0.0,9.0] {
                let point = CGPoint(x: x,y: y)
                check(donutGeometry.fillPath.contains(point,using: .evenOdd) == donut.contains(point,using: .evenOdd),
                      "Interior holes survive a viewport that intersects both sides of the hole")
            }
        }
        check(pointsStayInside(donutGeometry.linePath,region), "Clipped hole edges stay in the working region")

        let fork = CGMutablePath()
        let forkPoints = [CGPoint(x:0,y:0),CGPoint(x:10,y:0),CGPoint(x:10,y:10),CGPoint(x:7,y:10),
                          CGPoint(x:7,y:3),CGPoint(x:3,y:3),CGPoint(x:3,y:10),CGPoint(x:0,y:10)]
        fork.move(to:forkPoints[0]); forkPoints.dropFirst().forEach { fork.addLine(to:$0) }; fork.closeSubpath()
        let cut = CGRect(x:-1,y:5,width:12,height:4)
        let forkGeometry = WorldMapPathGeometry(fork).clippedPolygon(to:cut,tolerance:0)
        for x in [1.0,5.0,9.0] {
            let point=CGPoint(x:x,y:7)
            check(forkGeometry.fillPath.contains(point,using:.evenOdd) == fork.contains(point,using:.evenOdd),
                  "Clipping a concave country preserves separate visible land pieces and the sea between them")
        }

        let lines=CGMutablePath()
        lines.move(to:CGPoint(x:-1_000,y:0)); lines.addLine(to:CGPoint(x:1_000,y:0))
        lines.move(to:CGPoint(x:0,y:-1_000)); lines.addLine(to:CGPoint(x:0,y:1_000))
        lines.move(to:CGPoint(x:-1_000,y:30)); lines.addLine(to:CGPoint(x:1_000,y:30))
        let clippedLines=WorldMapPathGeometry(lines).clippedLines(to:region,tolerance:0.1)
        check(vertices(clippedLines)==4, "Horizontal and vertical zero-height run bounds are not discarded; offscreen runs are excluded")
        check(pointsStayInside(clippedLines,region), "Long contour segments are clipped at the working-set boundary")

        do {
            let countries=try WorldMapCountries.load(), terrain=try WorldMapTerrain.load()
            let china=countries.countries.first { $0.id == "CHN" }!
            let indexed=WorldMapPathGeometry(china.path)
            let center=CGPoint(x:(114.0579+180)/360*1024,y:(90-22.5431)/180*512)
            for zoom in [2.1,8.0,72.0,128.0] {
                let scale=440*zoom/1024
                let visible=CGRect(x:center.x-234/scale,y:center.y-234/scale,width:468/scale,height:468/scale)
                var local=WorldMapGeometryWindow(); local.update(visibleBounds:visible,scale:scale)
                let geometry=indexed.clippedPolygon(to:local.bounds,tolerance:local.tolerance)
                check(pointsStayInside(geometry.fillPath,local.bounds) && pointsStayInside(geometry.linePath,local.bounds),
                      "Real country geometry stays bounded at overview and city zoom")
                check(geometry.fillPath.contains(center,using:.evenOdd), "Shenzhen remains on the raised country face after clipping and simplification")
                if zoom >= 72 {
                    check(vertices(geometry.fillPath)<2_500 && vertices(geometry.linePath)<2_500,
                          "City zoom never submits the entire national outline")
                } else if zoom == 2.1 {
                    check(vertices(geometry.fillPath)<2_000, "The world overview does not render city-resolution coastline geometry")
                }
                let contourCount=terrain.bands.filter {$0.elevation != 0}.map {
                    let path=WorldMapPathGeometry($0.contourPath).clippedLines(to:local.bounds,tolerance:local.tolerance)
                    check(pointsStayInside(path,local.bounds), "Every real contour band is clipped before rendering")
                    return vertices(path)
                }.reduce(0,+)
                if zoom>=72 { check(contourCount<1_000,"A city viewport submits only nearby terrain contour segments") }
            }
        } catch { fatalError("Real map geometry fixture failed: \(error)") }
        return count
    }
}
