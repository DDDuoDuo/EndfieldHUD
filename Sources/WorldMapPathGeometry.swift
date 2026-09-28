import Foundation
import CoreGraphics

/// A viewport-sized vector working set. Extra geometry is kept outside the
/// visible circle so ordinary dragging only changes retained layer transforms.
struct WorldMapGeometryWindow {
    private(set) var bounds = CGRect.null
    private(set) var detailScale: CGFloat = 0
    var tolerance: CGFloat { detailScale > 0 ? 0.28 / detailScale : 0 }

    @discardableResult mutating func update(visibleBounds: CGRect, scale: CGFloat) -> Bool {
        guard scale.isFinite, scale > 0, !visibleBounds.isNull, !visibleBounds.isEmpty else { return false }
        let detail = pow(2, ceil(log2(scale)))
        guard detailScale != detail || !bounds.contains(visibleBounds) else { return false }
        bounds = visibleBounds.insetBy(dx: -128 / scale, dy: -128 / scale)
        detailScale = detail
        return true
    }
}

struct WorldMapClippedGeometry {
    let fillPath: CGPath
    /// Only original geographic edges; clipping never introduces a coastline.
    let linePath: CGPath
}

/// Linear paths decoded once, with bounds for short runs of segments. Queries
/// visit intersecting runs instead of submitting whole-world geometry to Core
/// Animation. Country levels of detail are cached at two adjacent zoom levels.
final class WorldMapPathGeometry {
    private struct Chunk {
        let start: Int
        let end: Int
        let bounds: CGRect
    }
    private struct Ring {
        let points: [CGPoint]
        let closed: Bool
        let bounds: CGRect
        let chunks: [Chunk]
        init(points: [CGPoint], closed: Bool) {
            self.points = points; self.closed = closed
            bounds = WorldMapPathGeometry.bounds(points)
            var runs: [Chunk] = []
            if points.count > 1 {
                for start in stride(from: 0, to: points.count - 1, by: 64) {
                    let end = min(points.count - 1, start + 64)
                    runs.append(Chunk(start: start, end: end,
                                      bounds: WorldMapPathGeometry.bounds(points[start...end])))
                }
            }
            chunks = runs
        }
    }
    private let rings: [Ring]
    private var levels: [(CGFloat, [Ring])] = []

    init(_ path: CGPath) {
        var decoded: [Ring] = [], points: [CGPoint] = []
        func append(_ closed: Bool) {
            guard points.count > 1 else { points.removeAll(keepingCapacity: true); return }
            if closed, points.last == points.first { points.removeLast() }
            decoded.append(Ring(points: points, closed: closed)); points.removeAll(keepingCapacity: true)
        }
        // Both bundled datasets contain straight segments exclusively.
        path.applyWithBlock { pointer in
            let element = pointer.pointee
            switch element.type {
            case .moveToPoint: append(false); points.append(element.points[0])
            case .addLineToPoint: points.append(element.points[0])
            case .closeSubpath: append(true)
            case .addQuadCurveToPoint: points.append(element.points[1])
            case .addCurveToPoint: points.append(element.points[2])
            @unknown default: break
            }
        }
        append(false)
        rings = decoded
    }

    func clippedLines(to rect: CGRect, tolerance: CGFloat) -> CGPath {
        let result = CGMutablePath()
        for ring in rings where Self.intersects(ring.bounds, rect) {
            Self.appendLines(ring, to: result, rect: rect, tolerance: tolerance)
        }
        return result
    }

    func clippedPolygon(to rect: CGRect, tolerance: CGFloat) -> WorldMapClippedGeometry {
        let fill = CGMutablePath(), edges = CGMutablePath()
        for ring in simplifiedRings(tolerance: tolerance) where Self.intersects(ring.bounds, rect) {
            if ring.closed {
                let points = rect.contains(ring.bounds) ? ring.points : Self.clipPolygon(ring.points, to: rect)
                if points.count >= 3 {
                    fill.move(to: points[0]); points.dropFirst().forEach { fill.addLine(to: $0) }; fill.closeSubpath()
                }
            }
            Self.appendLines(ring, to: edges, rect: rect, tolerance: 0)
        }
        return WorldMapClippedGeometry(fillPath: fill, linePath: edges)
    }

    private func simplifiedRings(tolerance: CGFloat) -> [Ring] {
        guard tolerance > 0 else { return rings }
        if let index = levels.firstIndex(where: { $0.0 == tolerance }) {
            let hit = levels.remove(at: index); levels.append(hit); return hit.1
        }
        let simplified = rings.map { ring -> Ring in
            guard ring.points.count > 8 else { return ring }
            let result: [CGPoint]
            if ring.closed {
                // A closed ring is split into two arcs so a zero-length first
                // to last chord cannot erase a narrow island or interior hole.
                let origin = ring.points[0]
                let split = ring.points.indices.max { Self.distanceSquared(ring.points[$0], origin) < Self.distanceSquared(ring.points[$1], origin) }!
                let first = Self.simplify(Array(ring.points[0...split]), tolerance: tolerance)
                let second = Self.simplify(Array(ring.points[split...]) + [origin], tolerance: tolerance)
                result = Array(first.dropLast()) + Array(second.dropLast())
            } else { result = Self.simplify(ring.points, tolerance: tolerance) }
            // Keep tiny closed features rather than fabricating degenerate fill.
            return result.count >= (ring.closed ? 3 : 2) ? Ring(points: result, closed: ring.closed) : ring
        }
        levels.append((tolerance, simplified))
        if levels.count > 2 { levels.removeFirst() }
        return simplified
    }

    private static func appendLines(_ ring: Ring, to result: CGMutablePath, rect: CGRect, tolerance: CGFloat) {
        var run: [CGPoint] = []
        func flush() {
            guard run.count > 1 else { run.removeAll(keepingCapacity: true); return }
            let output = tolerance > 0 ? simplify(run, tolerance: tolerance) : run
            result.move(to: output[0]); output.dropFirst().forEach { result.addLine(to: $0) }
            run.removeAll(keepingCapacity: true)
        }
        func segment(_ first: CGPoint, _ last: CGPoint) {
            guard let (a, b) = clipSegment(first, last, to: rect) else { flush(); return }
            if run.last != a { flush(); run.append(a) }
            run.append(b)
        }
        for chunk in ring.chunks {
            guard intersects(chunk.bounds, rect) else { flush(); continue }
            for index in chunk.start..<chunk.end { segment(ring.points[index], ring.points[index + 1]) }
        }
        if ring.closed, let first = ring.points.first, let last = ring.points.last { segment(last, first) }
        flush()
    }

    private static func clipSegment(_ a: CGPoint, _ b: CGPoint, to r: CGRect) -> (CGPoint, CGPoint)? {
        if r.contains(a), r.contains(b) { return (a,b) }
        let dx = b.x - a.x, dy = b.y - a.y
        var low: CGFloat = 0, high: CGFloat = 1
        for (p,q): (CGFloat, CGFloat) in [(-dx,a.x-r.minX),(dx,r.maxX-a.x),(-dy,a.y-r.minY),(dy,r.maxY-a.y)] {
            if p == 0 { if q < 0 { return nil } }
            else {
                let ratio = q / p
                if p < 0 { if ratio > high { return nil }; low = max(low,ratio) }
                else { if ratio < low { return nil }; high = min(high,ratio) }
            }
        }
        return (CGPoint(x: a.x + low * dx, y: a.y + low * dy), CGPoint(x: a.x + high * dx, y: a.y + high * dy))
    }

    private static func clipPolygon(_ input: [CGPoint], to rect: CGRect) -> [CGPoint] {
        var output = input
        for side in 0..<4 {
            guard let last = output.last else { break }
            let source = output; output.removeAll(keepingCapacity: true)
            func inside(_ p: CGPoint) -> Bool {
                switch side { case 0: return p.x >= rect.minX; case 1: return p.x <= rect.maxX
                case 2: return p.y >= rect.minY; default: return p.y <= rect.maxY }
            }
            func crossing(_ a: CGPoint, _ b: CGPoint) -> CGPoint {
                if side < 2 {
                    let x = side == 0 ? rect.minX : rect.maxX
                    return CGPoint(x: x, y: a.y + (b.y-a.y) * (x-a.x) / (b.x-a.x))
                }
                let y = side == 2 ? rect.minY : rect.maxY
                return CGPoint(x: a.x + (b.x-a.x) * (y-a.y) / (b.y-a.y), y: y)
            }
            var previous = last, previousInside = inside(last)
            for current in source {
                let currentInside = inside(current)
                if currentInside != previousInside { output.append(crossing(previous,current)) }
                if currentInside { output.append(current) }
                previous = current; previousInside = currentInside
            }
        }
        return output
    }

    private static func simplify(_ input: [CGPoint], tolerance: CGFloat) -> [CGPoint] {
        guard input.count > 2, tolerance > 0 else { return input }
        var keep = [Bool](repeating: false, count: input.count)
        keep[0] = true; keep[input.count-1] = true
        let threshold = tolerance * tolerance
        var stack: [(Int,Int)] = [(0,input.count-1)]
        while let (first,last) = stack.popLast() {
            guard last > first + 1 else { continue }
            let a = input[first], b = input[last], dx = b.x-a.x, dy = b.y-a.y
            let length = dx*dx + dy*dy
            var furthest = threshold, selected = 0
            for index in (first+1)..<last {
                let p = input[index]
                let t = length > 0 ? min(1,max(0,((p.x-a.x)*dx + (p.y-a.y)*dy)/length)) : 0
                let px = p.x-a.x-t*dx, py = p.y-a.y-t*dy, distance = px*px + py*py
                if distance > furthest { furthest = distance; selected = index }
            }
            if selected != 0 { keep[selected] = true; stack.append((first,selected)); stack.append((selected,last)) }
        }
        return input.indices.compactMap { keep[$0] ? input[$0] : nil }
    }
    private static func bounds<C: Collection>(_ points: C) -> CGRect where C.Element == CGPoint {
        guard let first = points.first else { return .null }
        var minX = first.x, maxX = first.x, minY = first.y, maxY = first.y
        for p in points { minX = min(minX,p.x); maxX = max(maxX,p.x); minY = min(minY,p.y); maxY = max(maxY,p.y) }
        return CGRect(x: minX,y: minY,width: maxX-minX,height: maxY-minY)
    }
    private static func intersects(_ a: CGRect, _ b: CGRect) -> Bool {
        // CGRect.intersects can discard horizontal / vertical contour runs.
        !a.isNull && a.minX <= b.maxX && a.maxX >= b.minX && a.minY <= b.maxY && a.maxY >= b.minY
    }
    private static func distanceSquared(_ a: CGPoint, _ b: CGPoint) -> CGFloat {
        let x=a.x-b.x, y=a.y-b.y; return x*x+y*y
    }
}
