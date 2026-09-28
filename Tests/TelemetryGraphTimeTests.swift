import AppKit
import QuartzCore

enum TelemetryGraphTimeTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func near(_ a: CGFloat, _ b: CGFloat) -> Bool { abs(a - b) < 0.000001 }
        let graph = TelemetryGraph(name: "time", frame: CGRect(x: 0, y: 0, width: 590, height: 100), colors: [.yellow, .cyan])
        let line = graph.layer.sublayers!.first { $0.name == "time.line.0" } as! CAShapeLayer
        let second = graph.layer.sublayers!.first { $0.name == "time.line.1" } as! CAShapeLayer
        let area = graph.layer.sublayers!.first { $0.name == "time.area.0" } as! CAShapeLayer
        let marker = graph.layer.sublayers!.first { $0.name == "time.latest.0" }!
        let layerIDs = graph.layer.sublayers!.map(ObjectIdentifier.init)
        func points(_ path: CGPath?) -> [CGPoint] {
            var result: [CGPoint] = []
            path?.applyWithBlock {
                if $0.pointee.type == .moveToPoint || $0.pointee.type == .addLineToPoint { result.append($0.pointee.points[0]) }
            }
            return result
        }
        func coverage() -> CGPath { (line.mask as! CAShapeLayer).path! }
        graph.update(series: [[10, 20, 30]], ceiling: 100, animated: false)
        let regular = points(line.path)
        check(regular.count == 60 && near(regular[57].x, 570) && near(regular[58].x, 580) && near(regular[59].x, 590),
              "Callers without timestamps retain the existing equally spaced graph contract")
        graph.update(series: [[10, 20, 30]], ceiling: 100, animated: false, timestamps: [100, 101, 106])
        var timed = points(line.path)
        check(timed.count == 60 && near(timed[57].x, 530) && near(timed[58].x, 540) && near(timed[59].x, 590),
              "A one-second interval followed by a five-second interval occupies proportional graph widths")
        check(near(timed[0].x, 0) && timed[56].x < timed[57].x && near(timed[57].y, 90)
              && near(timed[58].y, 80) && near(timed[59].y, 70),
              "Padding remains left of the first real timestamp without altering measured values")
        check(coverage().contains(CGPoint(x: 535, y: 50)) && coverage().contains(CGPoint(x: 580, y: 50))
              && !coverage().contains(CGPoint(x: 529, y: 50)),
              "The valid coverage uses actual adjacent elapsed-time spans and excludes padded history")
        check(marker.position == timed.last! && !marker.isHidden, "The latest marker stays attached to the latest actual timestamp")
        graph.update(series: [[10, nil, 30]], ceiling: 100, animated: false, timestamps: [100, 101, 106])
        check(coverage().isEmpty, "A missing intermediate reading masks both adjacent elapsed intervals instead of connecting across the gap")
        graph.update(series: [[10, 20, nil, 40, 50]], ceiling: 100, animated: false, timestamps: [100, 101, 106, 111, 112])
        check(coverage().contains(CGPoint(x: 475, y: 50)) && coverage().contains(CGPoint(x: 585, y: 50))
              && !coverage().contains(CGPoint(x: 505, y: 50)) && !coverage().contains(CGPoint(x: 550, y: 50)),
              "Separated valid islands retain their exact time spans with unavailable intervals fully hidden")
        graph.update(series: [[10, 20, nil]], ceiling: 100, animated: false, timestamps: [100, 101, 106])
        check(marker.isHidden && !coverage().contains(CGPoint(x: 570, y: 50)), "An unavailable newest sample hides its marker and elapsed interval")
        let values = (0..<60).map { Optional(Double($0)) }
        let times = (0..<60).map { TimeInterval($0 * 5) }
        graph.update(series: [values], ceiling: 100, animated: false, timestamps: times)
        timed = points(line.path)
        check(timed.count == 60 && near(timed[0].x, 0) && near(timed[59].x, 590)
              && near(timed[1].x - timed[0].x, 10),
              "A five-minute retained history expands the window rather than discarding older slower readings")
        var mixedTimes: [TimeInterval] = [0]
        for index in 1..<60 { mixedTimes.append(mixedTimes.last! + (index < 30 ? 1 : 5)) }
        graph.update(series: [values, values], ceiling: 100, animated: false, timestamps: mixedTimes)
        timed = points(line.path)
        check(near((timed[30].x - timed[29].x) / (timed[1].x - timed[0].x), 5)
              && near(timed[0].x, 0) && near(timed.last!.x, 590),
              "Mixed foreground and background cadence remains five-to-one even in a full retained history")
        check(points(second.path).map(\.x) == timed.map(\.x), "Paired graph series share the same time axis")
        let longTimes = (0..<75).map { TimeInterval($0 * 2) }
        graph.update(series: [(0..<75).map { Optional(Double($0)) }], ceiling: 100, animated: false, timestamps: longTimes)
        timed = points(line.path)
        check(timed.count == 60 && near(timed[0].x, 0) && near(timed[0].y, 85) && near(timed.last!.y, 26),
              "Truncating longer inputs keeps timestamps aligned with the same latest sixty values")
        graph.update(series: [[42]], ceiling: 100, animated: false, timestamps: [700])
        check(points(line.path).count == 60 && near(points(line.path).last!.x, 590) && coverage().isEmpty && !marker.isHidden,
              "One measured value has a right-edge marker but no fabricated connecting segment")
        graph.update(series: [[]], ceiling: 100, animated: false, timestamps: [])
        check(points(line.path).count == 60 && marker.isHidden && coverage().isEmpty,
              "An empty timestamped history preserves path topology and stays invisible")
        for invalid in [[100.0, 100, 101], [100, 99, 101], [100, .nan, 102], [100, 101, .infinity], [-1, 0, 1], [100, 101]] {
            graph.update(series: [[10, 20, 30]], ceiling: 100, animated: false, timestamps: invalid)
            check(points(line.path).map(\.x) == regular.map(\.x),
                  "Malformed, nonmonotonic or insufficient timestamps fall back safely to the legacy positions")
        }
        graph.update(series: [[10, 20, 30]], ceiling: 100, animated: false, timestamps: [100, 101, 106])
        let oldLine = line.path!, oldArea = area.path!
        graph.update(series: [[10, 20, 40]], ceiling: 100, animated: true, timestamps: [101, 106, 107])
        let lineAnimation = line.animation(forKey: "telemetry.path") as? CABasicAnimation
        let areaAnimation = area.animation(forKey: "telemetry.path") as? CABasicAnimation
        check(lineAnimation != nil && areaAnimation != nil && lineAnimation?.duration == 0.32
              && points(oldLine).count == points(line.path).count && points(oldArea).count == points(area.path).count,
              "Time-aware paths preserve fixed line and fill topology for bounded visual interpolation")
        check(graph.layer.sublayers!.map(ObjectIdentifier.init) == layerIDs, "Time-axis changes reuse the same graph layers")
        graph.update(series: [[10, 20, 40]], ceiling: 100, animated: false, timestamps: [101, 106, 107])
        check(line.animationKeys()?.isEmpty != false && area.animationKeys()?.isEmpty != false,
              "Nonanimated updates remove prior interpolation tracks")
        return count
    }
}
