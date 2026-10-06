import AppKit

struct NotesDrawingPoint: Codable, Equatable {
    var x: Double
    var y: Double
    var point: CGPoint { CGPoint(x: x, y: y) }
    var isValid: Bool { x.isFinite && y.isFinite && (0...1).contains(x) && (0...1).contains(y) }
}

struct NotesDrawingStroke: Codable, Equatable {
    var points: [NotesDrawingPoint]
    var width: Double
    var color: NotesRGBA
    var isValid: Bool { !points.isEmpty && points.count <= NotesDrawing.maximumPointsPerStroke
        && points.allSatisfy(\.isValid) && width.isFinite && (1...80).contains(width) && color.isValid }
}

/// Vector strokes commit only at mouse-up. Limits reject new work explicitly,
/// never discard existing strokes. Positions scale with a resized drawing card.
struct NotesDrawing: Codable, Equatable {
    var version = 1
    var strokes: [NotesDrawingStroke] = []
    static let maximumStrokes = 2_000
    static let maximumPointsPerStroke = 4_096
    static let maximumPoints = 100_000
    var isValid: Bool { version == 1 && strokes.count <= Self.maximumStrokes
        && strokes.allSatisfy(\.isValid) && strokes.reduce(0) { $0 + $1.points.count } <= Self.maximumPoints }
    mutating func append(_ stroke: NotesDrawingStroke) -> Bool {
        guard stroke.isValid, strokes.count < Self.maximumStrokes,
              strokes.reduce(0, { $0 + $1.points.count }) + stroke.points.count <= Self.maximumPoints else { return false }
        strokes.append(stroke); return true
    }
    mutating func erase(at point: CGPoint, radius: CGFloat, in size: CGSize) -> Bool {
        let before = strokes.count
        strokes.removeAll { stroke in
            let points = stroke.points.map { CGPoint(x: $0.x * size.width, y: $0.y * size.height) }
            let threshold = radius + CGFloat(stroke.width) / 2
            if points.count == 1 { return hypot(points[0].x - point.x, points[0].y - point.y) <= threshold }
            for pair in zip(points, points.dropFirst()) {
                let dx = pair.1.x - pair.0.x, dy = pair.1.y - pair.0.y
                let length = dx * dx + dy * dy
                let t = length == 0 ? 0 : min(1, max(0, ((point.x - pair.0.x) * dx + (point.y - pair.0.y) * dy) / length))
                if hypot(point.x - pair.0.x - t * dx, point.y - pair.0.y - t * dy) <= threshold { return true }
            }
            return false
        }
        return before != strokes.count
    }
    static func path(_ stroke: NotesDrawingStroke, size: CGSize) -> CGPath {
        let path = CGMutablePath()
        for (index, point) in stroke.points.enumerated() {
            let position = CGPoint(x: point.x * size.width, y: point.y * size.height)
            if index == 0 { path.move(to: position) } else { path.addLine(to: position) }
        }
        if stroke.points.count == 1, let first = stroke.points.first {
            path.addLine(to: CGPoint(x: first.x * size.width + 0.01, y: first.y * size.height))
        }
        return path
    }
}
