import AppKit

enum ProjectionEvent {
    case strokeCompleted, erased, brushChanged, backgroundChanged, cleared
    case mediaAdded(NotesMediaKind), mediaRemoved
}

struct ProjectionMediaItem {
    let id: UUID
    let reference: NotesMediaReference
    var frame: CGRect
}

/// Session-only content. Reuses the Notes vector format and decoder limits, but
/// never opens or changes the user's Notes database or source media files.
final class ProjectionModel {
    static let maximumMedia = 16
    private(set) var drawing = NotesDrawing()
    private(set) var media: [ProjectionMediaItem] = []
    var color: NSColor
    private(set) var brushWidth: CGFloat = 5
    var erasing = false
    var backgroundEnabled = true
    private(set) var darkness: Double
    private(set) var blur: Double

    init(configuration: AppConfiguration) {
        color = HUDRuntimeAppearance.accent
        darkness = configuration.backgroundDarkness
        blur = configuration.blurAmount
    }

    func setBrushWidth(_ width: CGFloat) { if width.isFinite { brushWidth = min(80, max(1, width)) } }
    func setDarkness(_ value: Double) { if value.isFinite { darkness = min(1, max(0, value)) } }
    func setBlur(_ value: Double) { if value.isFinite { blur = min(1, max(0, value)) } }
    @discardableResult func append(_ stroke: NotesDrawingStroke) -> Bool { drawing.append(stroke) }
    @discardableResult func erase(at point: CGPoint, size: CGSize) -> Bool {
        drawing.erase(at: point, radius: brushWidth / 2, in: size)
    }
    @discardableResult func addMedia(_ reference: NotesMediaReference, at point: CGPoint, in bounds: CGRect) -> UUID? {
        guard reference.isValid, media.count < Self.maximumMedia, bounds.width > 0, bounds.height > 0,
              [bounds.minX, bounds.minY, bounds.width, bounds.height].allSatisfy(\.isFinite) else { return nil }
        let width = min(360, max(110, bounds.width * 0.4))
        let height = min(max(110, bounds.height * 0.55), max(100, width * CGFloat(reference.pixelHeight) / CGFloat(reference.pixelWidth) + 52))
        let item = ProjectionMediaItem(id: UUID(), reference: reference,
            frame: Self.constrain(CGRect(x: point.x - width / 2, y: point.y - height / 2, width: width, height: height), to: bounds))
        media.append(item); return item.id
    }
    func setFrame(_ frame: CGRect, id: UUID, in bounds: CGRect) {
        guard let index = media.firstIndex(where: { $0.id == id }) else { return }
        media[index].frame = Self.constrain(frame, to: bounds)
    }
    func bringForward(_ id: UUID) {
        guard let index = media.firstIndex(where: { $0.id == id }), index != media.count - 1 else { return }
        media.append(media.remove(at: index))
    }
    @discardableResult func removeMedia(_ id: UUID) -> Bool {
        let count = media.count; media.removeAll { $0.id == id }; return count != media.count
    }
    /// Clearing owns session references only; original media and presentation
    /// preferences remain unchanged.
    @discardableResult func clearContent() -> Bool {
        let changed = !drawing.strokes.isEmpty || !media.isEmpty
        drawing = NotesDrawing(); media.removeAll()
        return changed
    }
    static func constrain(_ frame: CGRect, to bounds: CGRect) -> CGRect {
        guard !bounds.isEmpty, [bounds.minX, bounds.minY, bounds.width, bounds.height].allSatisfy(\.isFinite) else { return .zero }
        let width = min(bounds.width, max(110, frame.width.isFinite ? frame.width : 240))
        let height = min(bounds.height, max(100, frame.height.isFinite ? frame.height : 180))
        let x = frame.minX.isFinite ? frame.minX : bounds.minX
        let y = frame.minY.isFinite ? frame.minY : bounds.minY
        return CGRect(x: min(bounds.maxX - width, max(bounds.minX, x)),
                      y: min(bounds.maxY - height, max(bounds.minY, y)), width: width, height: height)
    }
}
