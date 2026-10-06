import AppKit

enum NotesDrawingTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let color = NotesRGBA(red: 1, green: 0.8, blue: 0)
        let stroke = NotesDrawingStroke(points: [NotesDrawingPoint(x: 0.1, y: 0.1), NotesDrawingPoint(x: 0.9, y: 0.9)], width: 8, color: color)
        var drawing = NotesDrawing()
        check(drawing.append(stroke), "A finite vector stroke appends to the drawing")
        let data = try! JSONEncoder().encode(drawing)
        check(try! JSONDecoder().decode(NotesDrawing.self, from: data) == drawing, "Drawing points, thickness and colors survive persistence")
        check(NotesDrawing.path(stroke, size: CGSize(width: 200, height: 100)).boundingBox == CGRect(x: 20, y: 10, width: 160, height: 80),
              "Normalized drawing points resize with the card without losing strokes")
        check(!drawing.erase(at: CGPoint(x: 10, y: 80), radius: 2, in: CGSize(width: 100, height: 100)),
              "An eraser away from a stroke leaves it intact")
        check(drawing.erase(at: CGPoint(x: 50, y: 50), radius: 2, in: CGSize(width: 100, height: 100)) && drawing.strokes.isEmpty,
              "Segment intersection erases a stroke even far from its sampled endpoints")
        var invalid = stroke; invalid.points[0].x = .nan
        check(!drawing.append(invalid) && drawing.strokes.isEmpty, "Invalid input cannot corrupt an existing drawing")
        invalid = stroke; invalid.width = 0
        check(!drawing.append(invalid), "Unsupported zero-width strokes are rejected")
        invalid = stroke; invalid.points = Array(repeating: stroke.points[0], count: NotesDrawing.maximumPointsPerStroke + 1)
        check(!drawing.append(invalid), "A single stroke has a bounded point count")
        let dot = NotesDrawingStroke(points: [NotesDrawingPoint(x: 0.5, y: 0.5)], width: 3, color: color)
        check(!NotesDrawing.path(dot, size: CGSize(width: 100, height: 100)).isEmpty,
              "A click produces a drawable dot instead of an empty path")
        drawing.strokes = Array(repeating: dot, count: NotesDrawing.maximumStrokes)
        let before = drawing
        check(!drawing.append(dot) && drawing == before, "Capacity rejects new drawing work without dropping old strokes")
        drawing.version = 2
        check(!drawing.isValid, "Unknown drawing payload versions are rejected")
        return count
    }
}
