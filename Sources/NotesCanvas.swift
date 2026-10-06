import AppKit
import CoreText
import QuartzCore

struct NotesEditRequest {
    let noteID: UUID
    let itemID: UUID?
    let text: String
    /// The editor rectangle in the full overlay workspace.
    let rect: CGRect
    let fontSize: CGFloat
    let multiline: Bool
    var space: NotesCoordinateSpace = .workspace
    /// Logical content offset; view scrolling is session state, never note data.
    var scrollOffset: CGFloat = 0
    var richText: NotesRichText? = nil
}

enum NotesCoordinateSpace { case module, workspace }

struct NotesCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
    var space: NotesCoordinateSpace = .workspace
}

enum NotesCanvasEvent: String {
    case createdText, createdTODO, createdMedia, createdDrawing
    case editedText, formattedText, editedTODO, drawingEdited, deletedNote, mediaPlayback
}

/// Immutable line breaks shared by height measurement and visible-line drawing.
/// Only visible lines receive backing layers; long documents never create a
/// document-height bitmap. Cached entries are bounded by NotesCanvas.
enum NotesTextMetrics {
    static func lineHeight(fontSize: CGFloat) -> CGFloat {
        let font = NSFont.systemFont(ofSize: fontSize)
        return ceil(font.ascender - font.descender + max(0, font.leading)) + 1
    }
}

private final class NotesWrappedText {
    let text: String
    let lineHeight: CGFloat
    let ranges: [NSRange]
    let origins: [CGFloat]
    let heights: [CGFloat]
    let attributed: NSAttributedString
    private let string: NSString
    var height: CGFloat { (origins.last ?? 0) + (heights.last ?? lineHeight) }
    var rangeBytes: Int { ranges.capacity * MemoryLayout<NSRange>.stride
        + (origins.capacity + heights.capacity) * MemoryLayout<CGFloat>.stride }

    init(text: String, width: CGFloat, fontSize: CGFloat, richText: NotesRichText? = nil) {
        self.text = text
        string = text as NSString
        let font = NSFont.systemFont(ofSize: fontSize)
        lineHeight = NotesTextMetrics.lineHeight(fontSize: fontSize)
        attributed = richText?.attributed(text, defaultColor: .clear)
            ?? NSAttributedString(string: text, attributes: [.font: font])
        let typesetter = CTTypesetterCreateWithAttributedString(attributed)
        var lines: [NSRange] = []
        var position = 0
        while position < string.length {
            var length = CTTypesetterSuggestLineBreak(typesetter, position, Double(max(1, width)))
            if length <= 0 {
                length = string.rangeOfComposedCharacterSequence(at: position).length
            }
            length = min(length, string.length - position)
            lines.append(NSRange(location: position, length: length))
            position += length
        }
        if lines.isEmpty || text.hasSuffix("\n") || text.hasSuffix("\r") {
            lines.append(NSRange(location: string.length, length: 0))
        }
        ranges = lines
        var positions: [CGFloat] = [], sizes: [CGFloat] = [], y: CGFloat = 0
        for range in lines {
            let lineSize: CGFloat
            if richText != nil, range.length > 0 {
                var ascent: CGFloat = 0, descent: CGFloat = 0, leading: CGFloat = 0
                let line = CTLineCreateWithAttributedString(attributed.attributedSubstring(from: range))
                _ = CTLineGetTypographicBounds(line, &ascent, &descent, &leading)
                lineSize = max(1, ceil(ascent + descent + max(0, leading)) + 1)
            } else { lineSize = lineHeight }
            positions.append(y); sizes.append(lineSize); y += lineSize
        }
        origins = positions; heights = sizes
    }

    func string(at line: Int) -> String {
        string.substring(with: ranges[line]).trimmingCharacters(in: .newlines)
    }

    func visibleLines(from minimum: CGFloat, to maximum: CGFloat) -> Range<Int> {
        var low = 0, high = origins.count
        while low < high {
            let middle = (low + high) / 2
            if origins[middle] + heights[middle] <= minimum { low = middle + 1 } else { high = middle }
        }
        let first = low
        high = origins.count
        while low < high {
            let middle = (low + high) / 2
            if origins[middle] < maximum { low = middle + 1 } else { high = middle }
        }
        return first..<low
    }

    func attributedLine(at index: Int, defaultColor: NSColor, strikethrough: Bool) -> NSAttributedString {
        var range = ranges[index]
        while range.length > 0, (CharacterSet.newlines as NSCharacterSet).characterIsMember(string.character(at: range.location + range.length - 1)) {
            range.length -= 1
        }
        let result = NSMutableAttributedString(attributedString: attributed.attributedSubstring(from: range))
        let full = NSRange(location: 0, length: result.length)
        result.enumerateAttribute(.foregroundColor, in: full) { value, range, _ in
            if value == nil || (value as? NSColor)?.isEqual(NSColor.clear) == true {
                result.addAttribute(.foregroundColor, value: defaultColor, range: range)
            }
        }
        if strikethrough { result.addAttribute(.strikethroughStyle, value: NSUnderlineStyle.single.rawValue, range: full) }
        return result
    }
}

/// All checklist paint, pointer, editor and accessibility geometry uses this
/// measured index. It mirrors current rows only, with no historical layouts.
private struct NotesContentGeometry {
    let width: CGFloat
    let kind: NoteKind
    let rowOrigins: [CGFloat]
    let rowHeights: [CGFloat]
    let height: CGFloat

    func visibleRows(offset: CGFloat, height viewportHeight: CGFloat) -> Range<Int> {
        var low = 0, high = rowOrigins.count
        while low < high {
            let middle = (low + high) / 2
            if rowOrigins[middle] + rowHeights[middle] <= offset { low = middle + 1 }
            else { high = middle }
        }
        let first = low
        high = rowOrigins.count
        while low < high {
            let middle = (low + high) / 2
            if rowOrigins[middle] < offset + viewportHeight { low = middle + 1 }
            else { high = middle }
        }
        return first..<low
    }
}

/// A retained scene inside the common HUD. Note geometry is rendered in logical
/// points; the host projects input and supplies a native text view only while editing.
final class NotesCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    let workspaceLayer = CALayer()
    private(set) var workspaceBounds = CGRect(x: 0, y: 0, width: 400, height: 334)
    private(set) var notesSelected = true
    private(set) var pendingDeletionID: UUID?
    private var creationPoint: CGPoint?
    private var presentationGeneration = 0
    private var retiringLayers: [CALayer] = []
    private let reduceMotion: () -> Bool
    private let deletionControls = CALayer()
    var onEdit: ((NotesEditRequest) -> Void)?
    var onChooseImage: ((CGPoint) -> Void)?
    var onChooseShelfMedia: ((CGPoint) -> Void)?
    var onFormat: ((UUID, String) -> Void)?
    var onAction: ((NotesCanvasEvent) -> Void)?
    var onChange: (() -> Void)?
    private(set) var selectedNoteID: UUID?
    var isDragging: Bool { drag != nil || drawingGesture != nil || mediaSeek != nil }
    var hasSelection: Bool { selectedNoteID.map { visibleNoteIDs.contains($0) } ?? false }
    func clearSelection() { select(nil) }
    var noteCount: Int { notes.count }
    var mediaSeekActions: [NotesCanvasAction] {
        visibleNotes.compactMap { original in
            guard original.media?.kind == .video, let duration = original.media?.duration, duration > 0 else { return nil }
            let item = displayed(original)
            return NotesCanvasAction(id: item.id.uuidString, label: L10n.text("Playback position", "播放进度"),
                rect: mediaSeekRect(item).offsetBy(dx: item.x, dy: item.y))
        }
    }
    func mediaPosition(for id: UUID) -> Double { nodes[id]?.media?.currentTime ?? 0 }
    func mediaDuration(for id: UUID) -> Double { note(id)?.media?.duration ?? 0 }
    func seekMedia(id: UUID, to value: Double) {
        guard isVisible, visibleNoteIDs.contains(id), value.isFinite else { return }
        nodes[id]?.media?.seek(to: value)
    }
    var accessibleActions: [NotesCanvasAction] {
        var output = notesSelected ? toolbarActions() : []
        for original in visibleNotes.sorted(by: { $0.zIndex < $1.zIndex }) {
            let note = displayed(original)
            output.append(NotesCanvasAction(id: noteAction(note, "select"), label: noteLabel(note), rect: rect(note)))
            output.append(contentsOf: actions(for: note))
            if note.id == selectedNoteID {
                let corner = CGRect(x: note.x + note.width - 16, y: note.y + note.height - 16, width: 16, height: 16)
                output.append(NotesCanvasAction(id: noteAction(note, "grow"), label: L10n.text("Enlarge note", "放大便笺"), rect: corner))
                output.append(NotesCanvasAction(id: noteAction(note, "shrink"), label: L10n.text("Reduce note size", "缩小便笺"), rect: corner.offsetBy(dx: -17, dy: 0)))
            }
        }
        output.append(contentsOf: deletionActions())
        return output
    }
    private var visibleNotes: [CanvasNote] { notes.filter { notesSelected || $0.isPinned } }
    var visibleNoteIDs: Set<UUID> { Set(visibleNotes.map(\.id)) }
    /// Cards sweep at their own screen position instead of treating the whole
    /// full-screen workspace as one centered object.
    var deploymentLayers: [CALayer] {
        visibleNotes.compactMap { nodes[$0.id]?.layer } + [deletionControls]
    }

    private enum Tool: String { case text, todo, image, drawing }
    private struct Drag {
        let original: CanvasNote
        let start: CGPoint
        let resizing: Bool
        var changed: Bool
    }
    private final class NoteNode {
        let layer = CALayer()
        let viewport = CALayer()
        let content = CALayer()
        let footer = CATextLayer()
        let mediaRail = CALayer()
        let mediaFill = CALayer()
        let mediaHandle = CALayer()
        let formatSwatch = CALayer()
        let scrollThumb = CALayer()
        var geometry: NotesContentGeometry?
        var textLines: [Int: CATextLayer] = [:]
        var rows: [UUID: ChecklistRowNode] = [:]
        var imageName: String?
        var image: CGImage?
        var media: NotesMediaPresentation?
        var mediaReference: NotesMediaReference?
        let drawingLayer = HUDDecorativeContentLayer()
        let brushPreview = CAShapeLayer()
        init() {
            viewport.name = "notes.content.viewport"
            viewport.masksToBounds = true
            content.name = "notes.content.scroll"
            viewport.addSublayer(content)
            scrollThumb.name = "notes.content.scrollThumb"
            scrollThumb.cornerRadius = 1
            viewport.addSublayer(scrollThumb)
        }
    }
    private final class ChecklistRowNode {
        let layer = CALayer()
        var lines: [Int: CATextLayer] = [:]
    }
    private struct TextLayoutKey: Hashable {
        let note: UUID
        let item: UUID?
        let width: CGFloat
        let fontSize: CGFloat
    }

    private let store: NotesStore?
    private var notes: [CanvasNote]
    private var nodes: [UUID: NoteNode] = [:]
    private let canvasLayer = CALayer()
    private let heading = CATextLayer()
    private let status = CATextLayer()
    private let emptyHint = CATextLayer()
    private let toolbar = CALayer()
    private var drag: Drag?
    private struct MediaSeek { let id: UUID; var seconds: Double }
    private var mediaSeek: MediaSeek?
    private var editingColor: NSColor?
    private struct DrawingGesture {
        var note: CanvasNote
        var stroke: NotesDrawingStroke
        var erased = false
    }
    private var drawingGesture: DrawingGesture?
    private var drawingWidth: CGFloat = 8
    private var drawingColor = NotesRGBA(red: 0.98, green: 0.83, blue: 0.12)
    private var erasing = false
    private var pointerNote: UUID?
    private var isVisible = false
    private var editing: NotesEditRequest?
    private var scrollOffsets: [UUID: CGFloat] = [:]
    private var textLayouts: [TextLayoutKey: NotesWrappedText] = [:]
    private var textLayoutOrder: [TextLayoutKey] = []
    static let textLayoutCacheLimit = 64
    static let textLayoutRangeBudget = 16 * 1024 * 1024
    private let layoutRangeBudget: Int
    private(set) var cachedTextLayoutRangeBytes = 0
    private(set) var textLayoutBuildCount = 0
    var cachedTextLayoutCount: Int { textLayouts.count }
    private var errorMessage: String?
    private var unsavedNoteIDs: Set<UUID> = []
    private var dark = true
    private var accent = NSColor(srgbRed: 0.98, green: 0.83, blue: 0.12, alpha: 1)
    private var scale: CGFloat = 2
    private var renderedLanguage = L10n.resolvedLanguage
    private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.11, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.65 : 0.37, alpha: 1) }
    private var border: NSColor { NSColor(white: dark ? 0.72 : 0.24, alpha: dark ? 0.28 : 0.24) }
    private let headerHeight: CGFloat = 24

    init(store: NotesStore?, error: String? = nil, notesSelected: Bool = true,
         textLayoutRangeBudget: Int = NotesCanvas.textLayoutRangeBudget,
         reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.reduceMotion = reduceMotion
        layoutRangeBudget = max(1, textLayoutRangeBudget)
        self.notesSelected = notesSelected
        self.store = store
        notes = store?.notes ?? []
        errorMessage = error
        super.init()
        withoutActions {
            layer.name = "module.notes.canvas"
            layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
            layer.allowsGroupOpacity = false
            workspaceLayer.name = "notes.workspace"
            workspaceLayer.frame = workspaceBounds
            workspaceLayer.masksToBounds = false
            canvasLayer.frame = workspaceBounds
            canvasLayer.masksToBounds = false
            workspaceLayer.addSublayer(canvasLayer)
            deletionControls.name = "notes.deleteConfirmation"
            deletionControls.zPosition = 1_000_001
            workspaceLayer.addSublayer(deletionControls)
            heading.frame = CGRect(x: 11, y: 0, width: 250, height: 19)
            heading.fontSize = 15
            heading.font = NSFont.systemFont(ofSize: 15, weight: .semibold)
            layer.addSublayer(heading)
            status.frame = CGRect(x: 12, y: 20, width: 376, height: 13)
            status.fontSize = 9.5
            status.font = NSFont.systemFont(ofSize: 9.5)
            status.truncationMode = .end
            layer.addSublayer(status)
            emptyHint.frame = CGRect(x: 24, y: 96, width: 336, height: 52)
            emptyHint.fontSize = 13
            emptyHint.font = NSFont.systemFont(ofSize: 13, weight: .medium)
            emptyHint.alignmentMode = .center
            emptyHint.isWrapped = true
            emptyHint.isHidden = true
            toolbar.frame = layer.bounds
            layer.addSublayer(toolbar)
            repaint()
        }
    }

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        updateAppearance(style: style)
        return layer
    }

    /// Isolated native input checks need an actual retained note above another
    /// control; diagnostic launches already use a separate temporary database.
    func installNoteForVerification(_ note: CanvasNote) throws {
        precondition(CommandLine.arguments.contains("--ui-test"))
        guard let store else { throw NotesStoreError.invalidRecord }
        let item = NotesGeometry.constrained(note, in: workspaceBounds)
        try store.upsert(item)
        replace(item)
        withoutActions { render(item) }
        onChange?()
    }

    /// Pinned cards belong to the retained workspace, so their appearance must
    /// update even when a different center module is selected.
    func updateAppearance(style: HUDModuleContentStyle) {
        guard dark != style.dark || !accent.isEqual(style.accent)
                || scale != style.contentsScale || renderedLanguage != L10n.resolvedLanguage else { return }
        dark = style.dark
        accent = style.accent
        scale = style.contentsScale
        renderedLanguage = L10n.resolvedLanguage
        withoutActions {
            repaint()
            renderDeletionControls()
        }
    }

    func updateRenderScale(_ value: CGFloat) {
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard scale != next else { return }
        scale = next
        withoutActions { repaint() }
    }

    /// The center keeps only the heading and add buttons. Cards live independently
    /// in the overlay-sized workspace and remain clear of center-content clipping.
    func setWorkspaceBounds(_ bounds: CGRect, creationPoint: CGPoint? = nil) {
        guard bounds.width > 0, bounds.height > 0,
              [bounds.minX, bounds.minY, bounds.width, bounds.height].allSatisfy({ $0.isFinite }) else { return }
        guard workspaceBounds != bounds || self.creationPoint != creationPoint else { return }
        workspaceBounds = bounds
        self.creationPoint = creationPoint
        withoutActions {
            workspaceLayer.frame = bounds
            canvasLayer.frame = CGRect(origin: .zero, size: bounds.size)
            for item in notes { render(item) }
            renderDeletionControls()
        }
        onChange?()
    }

    func setPresentation(notesSelected selected: Bool, animated: Bool = true,
                         direction: CGPoint = CGPoint(x: 0, y: 1)) {
        guard notesSelected != selected else {
            if !animated || reduceMotion() { settlePresentation() }
            return
        }
        mouseUp()
        pendingDeletionID = nil
        notesSelected = selected
        presentationGeneration += 1
        let generation = presentationGeneration
        if let selectedNoteID, !visibleNoteIDs.contains(selectedNoteID) { self.selectedNoteID = nil }
        for item in notes {
            let visible = selected || item.isPinned
            let wasVisible = nodes[item.id].map { !$0.layer.isHidden && $0.layer.opacity > 0 } ?? false
            if visible && !wasVisible { withoutActions { render(item) } }
            guard let node = nodes[item.id] else { continue }
            node.media?.setVisible(isVisible && visible, preserveArtworkOnHide: animated && !reduceMotion())
            guard visible != wasVisible else { continue }
            node.layer.removeAllAnimations()
            if visible {
                withoutActions { node.layer.isHidden = false; node.layer.opacity = 1 }
                if animated { animatePresentation(node.layer, appearing: true, direction: direction) }
            } else if animated && !reduceMotion() {
                CATransaction.begin()
                CATransaction.setCompletionBlock { [weak self, weak node] in
                    guard let self, self.presentationGeneration == generation else { return }
                    self.withoutActions { node?.layer.isHidden = true }
                }
                animatePresentation(node.layer, appearing: false, direction: direction)
                withoutActions { node.layer.opacity = 0 }
                CATransaction.commit()
            } else { withoutActions { node.layer.isHidden = true; node.layer.opacity = 0 } }
        }
        withoutActions { renderDeletionControls() }
        onChange?()
    }

    private func settlePresentation() {
        presentationGeneration += 1
        withoutActions {
            for item in notes {
                guard let card = nodes[item.id]?.layer else { continue }
                card.removeAnimation(forKey: "notes.section")
                card.isHidden = !notesSelected && !item.isPinned
                card.opacity = card.isHidden ? 0 : 1
            }
        }
    }

    /// Center module input is deliberately limited to add controls; no placing mode.
    @discardableResult
    func mouseDown(at point: CGPoint, clickCount: Int) -> Bool {
        guard notesSelected, let action = toolbarActions().first(where: { $0.rect.contains(point) }) else { return false }
        perform(actionID: action.id)
        return true
    }

    func containsWorkspacePoint(_ point: CGPoint) -> Bool {
        deletionActions().contains { $0.rect.contains(point) } || topNote(at: point) != nil
    }

    @discardableResult
    func mouseDownInWorkspace(at point: CGPoint, clickCount: Int) -> Bool {
        if let action = deletionActions().first(where: { $0.rect.contains(point) }) {
            perform(actionID: action.id); return true
        }
        guard let note = topNote(at: point) else { return false }
        select(note.id)
        if resizeRect(displayed(note)).contains(point) {
            drag = Drag(original: displayed(note), start: point, resizing: true, changed: false)
            return true
        }
        let displayedNote = displayed(note)
        if note.media?.kind == .video, let duration = note.media?.duration, duration > 0,
           mediaSeekRect(displayedNote).offsetBy(dx: displayedNote.x, dy: displayedNote.y).contains(point) {
            mediaSeek = MediaSeek(id: note.id, seconds: 0); updateMediaSeek(at: point); return true
        }
        if note.kind == .drawing, drawingViewport(displayedNote).offsetBy(dx: displayedNote.x, dy: displayedNote.y).contains(point) {
            drawingGesture = DrawingGesture(note: note,
                stroke: NotesDrawingStroke(points: [], width: Double(drawingWidth), color: drawingColor))
            updateDrawing(at: point)
            return true
        }
        if let action = actions(for: displayed(note)).reversed().first(where: { $0.rect.contains(point) }) {
            if action.id == noteAction(note, "edit") && clickCount < 2 {
                drag = Drag(original: displayed(note), start: point, resizing: false, changed: false)
            } else { perform(actionID: action.id) }
            return true
        }
        drag = Drag(original: displayed(note), start: point, resizing: false, changed: false)
        return true
    }

    func mouseDragged(to point: CGPoint) {
        if mediaSeek != nil { updateMediaSeek(at: point); return }
        if drawingGesture != nil { updateDrawing(at: point); return }
        guard var gesture = drag, point.x.isFinite, point.y.isFinite,
              let index = notes.firstIndex(where: { $0.id == gesture.original.id }) else { return }
        let dx = Double(point.x - gesture.start.x)
        let dy = Double(point.y - gesture.start.y)
        guard gesture.changed || abs(dx) + abs(dy) > 1 else { return }
        var note = gesture.original
        if gesture.resizing {
            note.width += dx
            note.height += dy
        } else {
            note.x += dx
            note.y += dy
        }
        note = NotesGeometry.constrained(note, in: workspaceBounds)
        // Selection may already have raised the note; retain its committed order.
        note.zIndex = notes[index].zIndex
        notes[index] = note
        gesture.changed = true
        drag = gesture
        withoutActions {
            if gesture.resizing { render(note) }
            else { nodes[note.id]?.layer.frame = localRect(note) }
            renderDeletionControls()
        }
    }

    func mouseUp() {
        if let gesture = mediaSeek {
            mediaSeek = nil; nodes[gesture.id]?.media?.seek(to: gesture.seconds)
            if let item = note(gesture.id), let node = nodes[gesture.id] { withoutActions { updateMediaProgress(item, node: node) } }
            return
        }
        if var gesture = drawingGesture {
            drawingGesture = nil
            if !erasing && !gesture.stroke.points.isEmpty {
                var drawing = gesture.note.drawing ?? NotesDrawing()
                if drawing.append(gesture.stroke) { gesture.note.drawing = drawing; gesture.erased = true }
                else { errorMessage = L10n.text("Drawing limit reached. Create another drawing note.", "画画容量已满，请新建画画便笺。") }
            }
            if gesture.erased {
                replace(gesture.note)
                if save(gesture.note) { onAction?(.drawingEdited) }
            }
            withoutActions { if let item = note(gesture.note.id) { render(item) }; updateStatus() }
            onChange?(); return
        }
        guard let gesture = drag else { return }
        drag = nil
        if gesture.changed, let note = note(gesture.original.id) { save(note) }
        onChange?()
    }

    /// Preserve trackpad fractions and native momentum. Only the retained
    /// content origin and newly visible rows change; no timer or save occurs.
    @discardableResult
    func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard delta.isFinite, let original = topNote(at: point), original.kind != .image else { return false }
        if original.kind == .drawing {
            drawingWidth = min(80, max(1, drawingWidth + delta * 0.25))
            mouseMoved(at: point); return true
        }
        guard delta != 0, drag == nil, editing?.noteID != original.id else { return true }
        let note = displayed(original), geometry = contentGeometry(for: displayed(original))
        let maximum = max(0, geometry.height - contentViewport(note).height)
        let previous = scrollOffsets[note.id] ?? 0
        let next = min(maximum, max(0, previous + delta))
        guard previous != next else { return true }
        scrollOffsets[note.id] = next
        withoutActions { layoutScrollableContent(note) }
        onChange?()
        return true
    }

    func contentViewport(for noteID: UUID) -> CGRect? {
        guard let note = note(noteID), note.kind != .image else { return nil }
        let item = displayed(note)
        return contentViewport(item).offsetBy(dx: item.x, dy: item.y)
    }

    func scrollOffset(for noteID: UUID) -> CGFloat { scrollOffsets[noteID] ?? 0 }

    func noteRect(for noteID: UUID) -> CGRect? { note(noteID).map { rect(displayed($0)) } }
    func noteKind(for noteID: UUID) -> NoteKind? { note(noteID)?.kind }
    func formatAction(at point: CGPoint) -> String? {
        guard let item = topNote(at: point) else { return nil }
        return actions(for: item).first { $0.id.contains(":format") && $0.rect.contains(point) }?.id
    }
    func setVisible(_ visible: Bool) {
        isVisible = visible
        let visibleIDs = visibleNoteIDs
        for (id, node) in nodes { node.media?.setVisible(visible && visibleIDs.contains(id), preserveArtworkOnHide: !reduceMotion()) }
        if !visible { mouseMoved(at: nil) }
    }
    func setDrawingColor(_ color: NSColor) {
        if let value = NotesRGBA(color) {
            drawingColor = value
            if let id = selectedNoteID { withoutActions { nodes[id]?.formatSwatch.backgroundColor = color.cgColor } }
        }
    }
    var currentDrawingColor: NSColor { drawingColor.color }
    func setEditingColor(_ color: NSColor) {
        guard editingColor?.isEqual(color) != true else { return }
        editingColor = color
        if let id = editing?.noteID { withoutActions { nodes[id]?.formatSwatch.backgroundColor = color.cgColor } }
    }

    @discardableResult func rightMouseDown(at point: CGPoint) -> Bool {
        guard let original = topNote(at: point) else { return false }
        let item = displayed(original)
        guard item.kind == .drawing, drawingViewport(item).offsetBy(dx: item.x, dy: item.y).contains(point) else { return false }
        if drawingGesture != nil { mouseUp() }
        erasing.toggle(); mouseMoved(at: point); return true
    }

    func mouseMoved(at point: CGPoint?) {
        if let previous = pointerNote { nodes[previous]?.brushPreview.isHidden = true }
        pointerNote = nil
        guard let point, let original = topNote(at: point), original.kind == .drawing,
              let node = nodes[original.id] else { return }
        let item = displayed(original)
        let viewport = drawingViewport(item), local = CGPoint(x: point.x - item.x, y: point.y - item.y)
        guard viewport.contains(local) else { return }
        pointerNote = item.id
        withoutActions {
            let radius = drawingWidth / 2
            node.brushPreview.isHidden = false
            node.brushPreview.frame = node.layer.bounds
            node.brushPreview.path = CGPath(ellipseIn: CGRect(x: local.x - radius, y: local.y - radius,
                width: drawingWidth, height: drawingWidth), transform: nil)
            node.brushPreview.fillColor = nil
            node.brushPreview.strokeColor = (erasing ? NSColor.systemRed : primary).cgColor
            node.brushPreview.lineWidth = 1
        }
    }

    func contains(noteID: UUID, point: CGPoint) -> Bool {
        topNote(at: point)?.id == noteID
    }

    func deleteSelection() {
        guard let id = selectedNoteID else { return }
        requestDeletion(id)
    }

    func setEditing(_ request: NotesEditRequest?) {
        let previous = editing?.noteID
        editing = request
        if request == nil { editingColor = nil }
        withoutActions {
            if let id = previous, let item = note(id) { render(item) }
            if let request = request, request.noteID != previous, let item = note(request.noteID) { render(item) }
        }
    }

    func finishEditing(_ request: NotesEditRequest, text: String, scrollOffset: CGFloat? = nil, richText: NotesRichText? = nil) {
        guard var item = note(request.noteID) else { return }
        let previous = item
        if let id = request.itemID, let index = item.items.firstIndex(where: { $0.id == id }) {
            item.items[index].text = text
        } else if request.itemID == nil && item.kind == .text {
            item.text = text
            item.richText = richText
        } else { return }
        editing = nil
        replace(item)
        if save(item), previous != item { onAction?(item.kind == .todo ? .editedTODO : previous.richText != item.richText ? .formattedText : .editedText) }
        if let scrollOffset, scrollOffset.isFinite,
           request.itemID == nil || scrollOffset > 0 || request.scrollOffset > 0 {
            let geometry = contentGeometry(for: displayed(item))
            let base = request.itemID.flatMap { id in item.items.firstIndex { $0.id == id } }
                .map { geometry.rowOrigins[$0] + 3 } ?? 0
            scrollOffsets[item.id] = min(max(0, geometry.height - contentViewport(displayed(item)).height),
                                         max(0, base + scrollOffset))
        }
        withoutActions { render(item) }
        onChange?()
    }

    @discardableResult
    func importImages(urls: [URL], at point: CGPoint) -> Bool {
        guard let store = store else { reportUnavailable(); return false }
        var inserted = false
        for (index, url) in urls.enumerated() {
            do {
                let position = CGPoint(x: point.x + CGFloat(index % 4) * 12, y: point.y + CGFloat(index % 4) * 12)
                let imported = try store.importImage(from: url, at: position, bounds: workspaceBounds)
                inserted = true
                replace(imported)
                select(imported.id)
                withoutActions { render(imported); updateStatus() }
            if let card = nodes[imported.id]?.layer { animateCard(card, appearing: true) }
            } catch { report(error) }
        }
        onChange?()
        return inserted
    }

    @discardableResult
    func importImage(data: Data, at point: CGPoint) -> Bool {
        guard let store = store else { reportUnavailable(); return false }
        do {
            let imported = try store.importImage(data: data, at: point, bounds: workspaceBounds)
            replace(imported)
            select(imported.id)
            withoutActions { render(imported); updateStatus() }
            if let card = nodes[imported.id]?.layer { animateCard(card, appearing: true) }
            onChange?()
            return true
        } catch { report(error); return false }
    }

    @discardableResult func importMedia(reference: NotesMediaReference, at point: CGPoint) -> Bool {
        guard let store else { reportUnavailable(); return false }
        do {
            let item = try store.importMedia(reference: reference, at: point, bounds: workspaceBounds)
            replace(item); select(item.id)
            withoutActions { render(item); updateStatus() }
            if let card = nodes[item.id]?.layer { animateCard(card, appearing: true) }
            onChange?(); onAction?(.createdMedia); return true
        } catch { report(error); return false }
    }
    func reportImportError(_ error: Error) { report(error) }

    private func drawingViewport(_ item: CanvasNote) -> CGRect {
        CGRect(x: 5, y: 29, width: CGFloat(item.width) - 10, height: max(1, CGFloat(item.height) - 56))
    }
    private func updateDrawing(at point: CGPoint) {
        guard var gesture = drawingGesture, point.x.isFinite, point.y.isFinite else { return }
        let item = displayed(gesture.note), viewport = drawingViewport(item)
        let local = CGPoint(x: min(viewport.width, max(0, point.x - item.x - viewport.minX)),
                            y: min(viewport.height, max(0, point.y - item.y - viewport.minY)))
        if erasing {
            var drawing = gesture.note.drawing ?? NotesDrawing()
            if drawing.erase(at: local, radius: drawingWidth / 2, in: viewport.size) {
                gesture.note.drawing = drawing; gesture.erased = true
                if let node = nodes[item.id] { withoutActions { drawStrokes(drawing, on: node, size: viewport.size) } }
            }
        } else {
            let next = NotesDrawingPoint(x: Double(local.x / viewport.width), y: Double(local.y / viewport.height))
            if let last = gesture.stroke.points.last,
               hypot((last.x - next.x) * viewport.width, (last.y - next.y) * viewport.height) < 0.8 { return }
            if gesture.stroke.points.count < NotesDrawing.maximumPointsPerStroke {
                gesture.stroke.points.append(next)
                if let node = nodes[item.id] {
                    withoutActions {
                        let live: CAShapeLayer
                        if let existing = node.drawingLayer.sublayers?.last as? CAShapeLayer, existing.name == "notes.drawing.live" { live = existing }
                        else { live = CAShapeLayer(); live.name = "notes.drawing.live"; node.drawingLayer.addSublayer(live) }
                        styleStroke(live, stroke: gesture.stroke, size: viewport.size)
                    }
                }
            } else { errorMessage = L10n.text("Stroke limit reached. Release to start a new stroke.", "笔画长度已满，松开后可开始下一笔。") }
        }
        drawingGesture = gesture
        mouseMoved(at: point)
    }
    private func styleStroke(_ layer: CAShapeLayer, stroke: NotesDrawingStroke, size: CGSize) {
        layer.frame = CGRect(origin: .zero, size: size)
        layer.path = NotesDrawing.path(stroke, size: size)
        layer.fillColor = nil; layer.strokeColor = stroke.color.color.cgColor
        layer.lineWidth = CGFloat(stroke.width); layer.lineCap = .round; layer.lineJoin = .round
    }
    private func drawStrokes(_ drawing: NotesDrawing, on node: NoteNode, size: CGSize) {
        node.drawingLayer.sublayers?.forEach { $0.removeFromSuperlayer() }
        for stroke in drawing.strokes {
            let shape = CAShapeLayer(); styleStroke(shape, stroke: stroke, size: size)
            node.drawingLayer.addSublayer(shape)
        }
    }

    /// Called before switching sections or closing. Finishes an active geometry
    /// gesture immediately; no pending animation, task, or timer survives.
    func cancelInteraction() {
        mouseUp(); pendingDeletionID = nil
        presentationGeneration += 1
        cancelAnimations()
        mouseMoved(at: nil)
        withoutActions {
            for item in notes { render(item) }
            renderDeletionControls()
        }
    }

    func perform(actionID: String) {
        let pieces = actionID.split(separator: ":").map(String.init)
        if pieces.count == 2, pieces[0] == "tool", let chosen = Tool(rawValue: pieces[1]) {
            guard notesSelected else { return }
            guard store != nil else { reportUnavailable(); return }
            let offset = CGFloat(notes.count % 7) * 18
            let origin = creationPoint ?? CGPoint(x: workspaceBounds.midX - 95, y: workspaceBounds.midY - 60)
            let point = CGPoint(x: origin.x + offset, y: origin.y + offset)
            switch chosen {
            case .text: create(kind: .text, at: point)
            case .todo: create(kind: .todo, at: point)
            case .image: onChooseImage?(point)
            case .drawing: create(kind: .drawing, at: point)
            }
            animateToolbar(actionID)
            return
        }
        guard pieces.count >= 3, pieces[0] == "note", let id = UUID(uuidString: pieces[1]), note(id) != nil, visibleNoteIDs.contains(id) else { return }
        select(id)
        guard var item = note(id) else { return }
        switch pieces[2] {
        case "select": return
        case "delete": requestDeletion(id); return
        case "cancelDelete":
            pendingDeletionID = nil; withoutActions { renderDeletionControls() }; onChange?(); return
        case "confirmDelete":
            guard pendingDeletionID == id else { return }
            delete(id); return
        case "pin": item.isPinned.toggle()
        case "edit": beginEditing(item); return
        case "formatFont", "formatSize", "formatColor", "formatSpecial":
            if item.kind == .text, editing?.noteID != item.id { beginEditing(item) }
            onFormat?(item.id, String(pieces[2].dropFirst(6)).lowercased()); return
        case "mediaPlayback":
            if let media = nodes[item.id]?.media {
                let previous = media.state
                media.togglePlayback()
                if media.state != previous { onAction?(.mediaPlayback) }
            }
            return
        case "grow", "shrink":
            let step = pieces[2] == "grow" ? 20.0 : -20.0
            item.width += step
            item.height += step
            item = NotesGeometry.constrained(item, in: workspaceBounds)
        case "add":
            guard item.kind == .todo else { return }
            let child = NoteChecklistItem(text: "")
            item.items.append(child)
            replace(item)
            save(item)
            scrollOffsets[id] = max(0, contentGeometry(for: displayed(item)).height - contentViewport(displayed(item)).height)
            withoutActions { render(item) }
            animateMutation(item.id)
            beginEditing(item, itemID: child.id)
            onChange?()
            return
        case "check", "up", "down", "remove", "editItem":
            guard pieces.count == 4, let childID = UUID(uuidString: pieces[3]),
                  let index = item.items.firstIndex(where: { $0.id == childID }) else { return }
            switch pieces[2] {
            case "check": item.items[index].isChecked.toggle()
            case "up": if index > 0 { item.items.swapAt(index, index - 1) }
            case "down": if index + 1 < item.items.count { item.items.swapAt(index, index + 1) }
            case "remove": item.items.remove(at: index)
            default: beginEditing(item, itemID: childID); return
            }
        default: return
        }
        replace(item)
        save(item)
        withoutActions { render(item); updateStatus(); renderDeletionControls() }
        animateMutation(item.id)
        onChange?()
    }

    private func create(kind: NoteKind, at point: CGPoint) {
        var item = CanvasNote(kind: kind)
        item.x = Double(point.x)
        item.y = Double(point.y)
        item.width = kind == .drawing ? 300 : kind == .todo ? 228 : 210
        item.height = kind == .drawing ? 240 : kind == .todo ? 154 : 140
        item.zIndex = nextZIndex()
        if kind == .todo { item.items = [NoteChecklistItem(text: "")] }
        if kind == .drawing { item.drawing = NotesDrawing() }
        item = NotesGeometry.constrained(item, in: workspaceBounds)
        replace(item)
        if save(item) { onAction?(kind == .drawing ? .createdDrawing : kind == .todo ? .createdTODO : .createdText) }
        select(item.id)
        withoutActions { render(item); updateStatus() }
        if let card = nodes[item.id]?.layer { animateCard(card, appearing: true) }
        beginEditing(item, itemID: item.items.first?.id)
        onChange?()
    }

    private func beginEditing(_ original: CanvasNote, itemID: UUID? = nil) {
        let item = displayed(original)
        let request: NotesEditRequest
        if let id = itemID, let index = item.items.firstIndex(where: { $0.id == id }) {
            let geometry = contentGeometry(for: item), viewport = contentViewport(item)
            var offset = scrollOffsets[item.id] ?? 0
            let top = geometry.rowOrigins[index], bottom = top + geometry.rowHeights[index]
            if top < offset || bottom > offset + viewport.height {
                offset = geometry.rowHeights[index] > viewport.height ? top : max(0, bottom - viewport.height)
                offset = min(max(0, geometry.height - viewport.height), offset)
                scrollOffsets[item.id] = offset
                withoutActions { layoutScrollableContent(item) }
            }
            let full = checklistTextRect(item, geometry: geometry, index: index, offset: offset)
            let editRect = full.intersection(viewport).offsetBy(dx: item.x, dy: item.y)
            request = NotesEditRequest(noteID: item.id, itemID: id, text: item.items[index].text,
                rect: editRect, fontSize: 11, multiline: false, scrollOffset: max(0, viewport.minY - full.minY))
        } else {
            guard item.kind == .text else { return }
            request = NotesEditRequest(noteID: item.id, itemID: nil, text: item.text,
                rect: contentViewport(item, editingText: true).offsetBy(dx: item.x, dy: item.y), fontSize: 12, multiline: true,
                scrollOffset: scrollOffsets[item.id] ?? 0, richText: item.richText)
        }
        onEdit?(request)
    }

    private func select(_ id: UUID?) {
        let old = selectedNoteID
        if pendingDeletionID != id { pendingDeletionID = nil; withoutActions { renderDeletionControls() } }
        selectedNoteID = id
        if let id = id, var item = note(id) {
            let maximum = notes.map(\.zIndex).max() ?? 0
            if item.zIndex < maximum {
                item.zIndex = nextZIndex()
                replace(item)
                save(item)
            }
        }
        withoutActions {
            if let old = old, let item = note(old) { render(item) }
            if let id = id, old != id, let item = note(id) { render(item) }
        }
        if old != id { onChange?() }
    }

    private func delete(_ id: UUID) {
        guard let store = store else { reportUnavailable(); return }
        do {
            try store.delete(id: id)
            nodes[id]?.media?.dispose()
            notes.removeAll { $0.id == id }
            if let card = nodes.removeValue(forKey: id)?.layer {
                retiringLayers.append(card)
                if reduceMotion() { card.removeFromSuperlayer(); retiringLayers.removeAll { $0 === card } }
                else {
                    CATransaction.begin()
                    CATransaction.setCompletionBlock { [weak self] in
                        card.removeFromSuperlayer()
                        self?.retiringLayers.removeAll { $0 === card }
                    }
                    animateCard(card, appearing: false)
                    withoutActions { card.opacity = 0 }
                    CATransaction.commit()
                }
            }
            pendingDeletionID = nil
            scrollOffsets.removeValue(forKey: id)
            for key in textLayoutOrder.filter({ $0.note == id }) { removeTextLayout(key) }
            unsavedNoteIDs.remove(id)
            if selectedNoteID == id { selectedNoteID = nil }
            if editing?.noteID == id { editing = nil }
            withoutActions { updateStatus(); renderDeletionControls() }
            onChange?()
            onAction?(.deletedNote)
        } catch { report(error) }
    }

    @discardableResult private func save(_ item: CanvasNote) -> Bool {
        guard let store = store else { reportUnavailable(); return false }
        do {
            try store.upsert(item)
            unsavedNoteIDs.remove(item.id)
            if unsavedNoteIDs.isEmpty { errorMessage = nil }
            withoutActions { updateStatus() }
            return true
        } catch {
            unsavedNoteIDs.insert(item.id)
            report(error)
            return false
        }
    }

    private func nextZIndex() -> Int {
        let maximum = notes.map(\.zIndex).max() ?? -1
        guard maximum == Int.max else { return maximum + 1 }
        // Exceptionally old or externally edited data can exhaust integer order.
        // Compact while retaining relative order before allocating the next slot.
        let ordered = notes.sorted { $0.zIndex == $1.zIndex ? $0.createdAt < $1.createdAt : $0.zIndex < $1.zIndex }
        for (index, original) in ordered.enumerated() {
            var compacted = original
            compacted.zIndex = index
            replace(compacted)
            save(compacted)
            withoutActions { nodes[compacted.id]?.layer.zPosition = CGFloat(index) + 1 }
        }
        return notes.count
    }

    private func replace(_ item: CanvasNote) {
        if let index = notes.firstIndex(where: { $0.id == item.id }) {
            let previous = notes[index]
            if previous.kind != item.kind || previous.width != item.width || previous.text != item.text
                || previous.items != item.items || previous.richText != item.richText {
                nodes[item.id]?.geometry = nil
            }
            if previous.richText != item.richText {
                for key in textLayoutOrder.filter({ $0.note == item.id && $0.item == nil }) { removeTextLayout(key) }
            }
            notes[index] = item
        }
        else { notes.append(item) }
    }
    private func note(_ id: UUID) -> CanvasNote? { notes.first { $0.id == id } }
    private func topNote(at point: CGPoint) -> CanvasNote? {
        visibleNotes.sorted { $0.zIndex > $1.zIndex }.first { rect(displayed($0)).contains(point) }
    }
    private func rect(_ item: CanvasNote) -> CGRect { CGRect(x: item.x, y: item.y, width: item.width, height: item.height) }
    private func displayed(_ item: CanvasNote) -> CanvasNote { NotesGeometry.constrained(item, in: workspaceBounds) }
    private func localRect(_ item: CanvasNote) -> CGRect { rect(displayed(item)).offsetBy(dx: -workspaceBounds.minX, dy: -workspaceBounds.minY) }
    private func resizeRect(_ item: CanvasNote) -> CGRect { CGRect(x: item.x + item.width - 15, y: item.y + item.height - 15, width: 15, height: 15) }

    private func contentViewport(_ item: CanvasNote, editingText: Bool? = nil) -> CGRect {
        let width = CGFloat(item.width), height = CGFloat(item.height)
        let reservesFormatting = editingText ?? (editing?.noteID == item.id)
        return item.kind == .todo
            ? CGRect(x: 5, y: headerHeight + 3, width: width - 10, height: max(1, height - headerHeight - 31))
            : CGRect(x: 9, y: 29, width: width - 18,
                     height: max(1, height - (item.kind == .text && !reservesFormatting ? 37 : 58)))
    }

    private func wrappedText(_ text: String, noteID: UUID, itemID: UUID? = nil,
                             width: CGFloat, fontSize: CGFloat, richText: NotesRichText? = nil) -> NotesWrappedText {
        let key = TextLayoutKey(note: noteID, item: itemID, width: width, fontSize: fontSize)
        if let cached = textLayouts[key], cached.text == text {
            if textLayoutOrder.last != key { textLayoutOrder.removeAll { $0 == key }; textLayoutOrder.append(key) }
            return cached
        }
        // A resize or font/content edit supersedes the previous measurement of
        // this exact note/task. Release it before allocating the replacement.
        for obsolete in textLayoutOrder.filter({ $0.note == noteID && $0.item == itemID }) {
            removeTextLayout(obsolete)
        }
        let layout = NotesWrappedText(text: text, width: width, fontSize: fontSize, richText: richText)
        textLayoutBuildCount += 1
        textLayouts[key] = layout
        cachedTextLayoutRangeBytes += layout.rangeBytes
        textLayoutOrder.append(key)
        // Keep the just-used layout even when one unusually long document alone
        // exceeds the budget; otherwise each fractional scroll would re-typeset it.
        while textLayoutOrder.count > 1 && (textLayoutOrder.count > Self.textLayoutCacheLimit
            || cachedTextLayoutRangeBytes > layoutRangeBudget) {
            removeTextLayout(textLayoutOrder[0])
        }
        return layout
    }

    private func removeTextLayout(_ key: TextLayoutKey) {
        if let layout = textLayouts.removeValue(forKey: key) {
            cachedTextLayoutRangeBytes -= layout.rangeBytes
        }
        textLayoutOrder.removeAll { $0 == key }
    }

    private func contentGeometry(for item: CanvasNote) -> NotesContentGeometry {
        let width = CGFloat(item.width)
        if let cached = nodes[item.id]?.geometry, cached.width == width, cached.kind == item.kind { return cached }
        var origins: [CGFloat] = [], heights: [CGFloat] = [], height: CGFloat = 0
        if item.kind == .todo {
            for child in item.items {
                let layout = wrappedText(child.text, noteID: item.id, itemID: child.id,
                                         width: max(20, width - 89), fontSize: 11)
                let rowHeight = max(25, layout.height + 8)
                origins.append(height); heights.append(rowHeight); height += rowHeight
            }
        } else if item.kind == .text {
            height = wrappedText(item.text, noteID: item.id, width: width - 18, fontSize: 12, richText: item.richText).height
        }
        let geometry = NotesContentGeometry(width: width, kind: item.kind,
            rowOrigins: origins, rowHeights: heights, height: height)
        nodes[item.id]?.geometry = geometry
        return geometry
    }

    private func checklistTextRect(_ item: CanvasNote, geometry: NotesContentGeometry, index: Int, offset: CGFloat) -> CGRect {
        CGRect(x: 28, y: contentViewport(item).minY + geometry.rowOrigins[index] - offset + 3,
               width: max(20, CGFloat(item.width) - 89), height: geometry.rowHeights[index] - 6)
    }

    private func noteAction(_ item: CanvasNote, _ verb: String) -> String { "note:\(item.id.uuidString):\(verb)" }
    private func noteLabel(_ item: CanvasNote) -> String {
        let title = item.kind == .text ? L10n.text("Text", "文字") : item.kind == .todo ? L10n.text("Checklist", "待办") : item.kind == .drawing ? L10n.text("Drawing", "画画") : L10n.text("Image/Video", "图片/视频")
        return item.kind == .text && !item.text.isEmpty ? "\(title): \(item.text.prefix(64))" : title
    }

    private func toolbarActions() -> [NotesCanvasAction] {
        let values = [("text", L10n.text("Text", "文字")), ("todo", L10n.text("TODO", "待办")),
                      ("image", L10n.text("Image/Video", "图片/视频")), ("drawing", L10n.text("Drawing", "画画"))]
        return values.enumerated().map { index, value in
            NotesCanvasAction(id: "tool:" + value.0, label: value.1,
                rect: CGRect(x: 7 + index * 98, y: 294, width: 92, height: 31), space: .module)
        }
    }

    private func actions(for original: CanvasNote) -> [NotesCanvasAction] {
        let item = displayed(original)
        var output = [NotesCanvasAction(id: noteAction(item, "pin"), label: item.isPinned ? L10n.text("Unpin note", "取消固定") : L10n.text("Pin note", "固定便笺"), rect: CGRect(x: item.x + item.width - 46, y: item.y + 2, width: 21, height: 20)),
                      NotesCanvasAction(id: noteAction(item, "delete"), label: L10n.text("Delete note", "删除便笺"), rect: CGRect(x: item.x + item.width - 24, y: item.y + 2, width: 21, height: 20))]
        if item.kind == .text {
            output.append(NotesCanvasAction(id: noteAction(item, "edit"), label: L10n.text("Edit text", "编辑文字"),
                rect: contentViewport(item).offsetBy(dx: item.x, dy: item.y)))
            let formatLabels = [("formatSize", L10n.text("Font size", "字号")), ("formatFont", L10n.text("Font", "字体")),
                                ("formatColor", L10n.text("Color", "颜色")), ("formatSpecial", L10n.text("Text style", "特殊"))]
            let width = 22.0
            for (index, value) in formatLabels.enumerated() where editing?.noteID == item.id {
                output.append(NotesCanvasAction(id: noteAction(item, value.0), label: value.1,
                    rect: CGRect(x: item.x + 6 + Double(index) * width, y: item.y + item.height - 25, width: 20, height: 20)))
            }
        } else if item.kind == .drawing, item.id == selectedNoteID {
            output.append(NotesCanvasAction(id: noteAction(item, "formatColor"), label: L10n.text("Drawing color", "画笔颜色"),
                rect: CGRect(x: item.x + 6, y: item.y + item.height - 24, width: 20, height: 20)))
        } else if item.kind == .image, item.media?.kind != .image, item.media != nil {
            output.append(NotesCanvasAction(id: noteAction(item, "mediaPlayback"),
                label: nodes[item.id]?.media?.isPlaying == true ? L10n.text("Pause", "暂停") : L10n.text("Play", "播放"),
                rect: CGRect(x: item.x + 6, y: item.y + item.height - 24, width: 66, height: 19)))
        } else if item.kind == .todo {
            let geometry = contentGeometry(for: item), viewport = contentViewport(item)
            let offset = min(max(0, geometry.height - viewport.height), scrollOffsets[item.id] ?? 0)
            func append(_ action: NotesCanvasAction) {
                let clipped = action.rect.intersection(viewport)
                guard !clipped.isNull, clipped.width > 1, clipped.height > 1 else { return }
                output.append(NotesCanvasAction(id: action.id, label: action.label,
                    rect: clipped.offsetBy(dx: item.x, dy: item.y)))
            }
            for index in geometry.visibleRows(offset: offset, height: viewport.height) {
                let child = item.items[index]
                let y = viewport.minY + geometry.rowOrigins[index] - offset
                let suffix = ":\(child.id.uuidString)"
                append(NotesCanvasAction(id: noteAction(item, "check") + suffix, label: child.isChecked ? L10n.text("Uncheck", "取消勾选") + " " + child.text : L10n.text("Check", "勾选") + " " + child.text, rect: CGRect(x: 5, y: y, width: 21, height: 22)))
                append(NotesCanvasAction(id: noteAction(item, "editItem") + suffix, label: L10n.text("Edit item", "编辑事项") + " " + child.text,
                    rect: checklistTextRect(item, geometry: geometry, index: index, offset: offset)))
                let controls: [(String, String, CGFloat)] = [("up", L10n.text("Move up", "上移"), 58), ("down", L10n.text("Move down", "下移"), 40), ("remove", L10n.text("Delete item", "删除事项"), 22)]
                for (verb, label, trailing) in controls {
                    append(NotesCanvasAction(id: noteAction(item, verb) + suffix, label: label, rect: CGRect(x: CGFloat(item.width) - trailing, y: y, width: 17, height: 22)))
                }
            }
            output.append(NotesCanvasAction(id: noteAction(item, "add"), label: L10n.text("Add item", "添加事项"), rect: CGRect(x: item.x + 7, y: item.y + item.height - 26, width: item.width - 29, height: 21)))
        }
        return output
    }

    private func repaint() {
        heading.string = HUDSectionHeading.text(L10n.text("NOTES", "便笺"))
        heading.foregroundColor = primary.cgColor
        heading.contentsScale = HUDRenderScale.contentScale(for: heading, baseScale: scale)
        status.contentsScale = HUDRenderScale.contentScale(for: status, baseScale: scale)
        canvasLayer.backgroundColor = nil
        canvasLayer.borderColor = nil
        emptyHint.foregroundColor = muted.cgColor
        emptyHint.contentsScale = HUDRenderScale.contentScale(for: emptyHint, baseScale: scale)
        for item in notes { render(item) }
        renderToolbar()
        updateStatus()
    }

    private func updateStatus() {
        let text: String
        if let error = errorMessage { text = L10n.text("Could not save: ", "无法保存：") + error }
        else if store == nil { text = L10n.text("Notes storage unavailable", "便笺存储不可用") }
        else { text = "" }
        status.string = text
        status.foregroundColor = errorMessage == nil ? muted.cgColor : NSColor.systemOrange.cgColor
        emptyHint.isHidden = true
    }

    private func renderToolbar() {
        toolbar.sublayers?.forEach { $0.removeFromSuperlayer() }
        for action in toolbarActions() {
            let plate = CAShapeLayer()
            plate.name = action.id
            plate.frame = action.rect
            plate.path = CGPath(roundedRect: plate.bounds, cornerWidth: 3, cornerHeight: 3, transform: nil)
            plate.fillColor = NSColor(white: dark ? 0.16 : 0.84, alpha: 1).cgColor
            plate.strokeColor = border.cgColor
            plate.lineWidth = 0.8
            toolbar.addSublayer(plate)
            HUDControlHighlightLayer.add(to: plate, rect: plate.bounds)
            let symbol = action.id == "tool:text" ? "T" : action.id == "tool:todo" ? "☑" : action.id == "tool:drawing" ? "✎" : "▧"
            let color = primary
            let iconRect = CGRect(x: 9, y: 5, width: 18, height: 18)
            let gameIcon: EndfieldGameIcon? = action.id == "tool:text" ? .operationalManual : action.id == "tool:todo" ? .mission : nil
            if action.id == "tool:drawing" {
                // Match the neighboring game icons' visible ink, including their transparent inset.
                plate.addSublayer(HUDPencilArtwork.makeLayer(in: iconRect.insetBy(dx: 2, dy: 2),
                    color: color, contentsScale: scale))
            } else if gameIcon?.add(to: plate, rect: iconRect,
                             tint: color, contentsScale: scale) != true {
                addText(symbol, rect: iconRect, size: 14, color: color, parent: plate, weight: .semibold)
            }
            addText(action.label, rect: CGRect(x: 27, y: 8, width: 63, height: 17), size: 10, color: color, parent: plate, weight: .semibold)
        }
    }

    private func render(_ original: CanvasNote) {
        // Unpinned cards are not visible on another section. In particular,
        // do not decode all stored note images merely to summon the map.
        guard notesSelected || original.isPinned else {
            nodes[original.id]?.media?.setVisible(false)
            nodes[original.id]?.layer.isHidden = true
            nodes[original.id]?.layer.opacity = 0
            return
        }
        let item = displayed(original)
        let node: NoteNode
        if let existing = nodes[item.id] { node = existing }
        else { node = NoteNode(); nodes[item.id] = node; canvasLayer.addSublayer(node.layer) }
        let card = node.layer
        card.name = "notes.note.\(item.id.uuidString)"
        card.isHidden = !notesSelected && !item.isPinned
        card.opacity = card.isHidden ? 0 : 1
        card.frame = localRect(item)
        card.zPosition = CGFloat(item.zIndex) + 1
        card.allowsGroupOpacity = false
        card.masksToBounds = true
        card.cornerRadius = 3
        card.borderWidth = item.id == selectedNoteID ? 1.1 : 0.65
        card.borderColor = (item.id == selectedNoteID ? accent : border).cgColor
        card.backgroundColor = NSColor(white: dark ? 0.105 : 0.92, alpha: 1).cgColor
        card.sublayers?.forEach { $0.removeFromSuperlayer() }
        node.content.sublayers?.forEach { $0.removeFromSuperlayer() }
        node.rows.removeAll(); node.textLines.removeAll()
        let head = CALayer()
        head.frame = CGRect(x: 0, y: 0, width: item.width, height: Double(headerHeight))
        head.backgroundColor = NSColor(white: dark ? 0.16 : 0.82, alpha: 1).cgColor
        card.addSublayer(head)
        let title = item.kind == .text ? L10n.text("TEXT", "文字") : item.kind == .todo ? L10n.text("TODO", "待办") : item.kind == .drawing ? L10n.text("DRAWING", "画画") : L10n.text("IMAGE/VIDEO", "图片/视频")
        addText("⠿  " + title, rect: CGRect(x: 7, y: 6, width: item.width - 56, height: 14), size: 9, color: muted, parent: head, weight: .semibold)
        drawPin(in: CGRect(x: item.width - 41, y: 6, width: 12, height: 12), pinned: item.isPinned, parent: head)
        drawCross(in: CGRect(x: item.width - 17, y: 8, width: 7, height: 7), color: muted, parent: head)
        switch item.kind {
        case .text, .todo: layoutScrollableContent(item)
        case .drawing:
            node.drawingLayer.frame = drawingViewport(item)
            node.drawingLayer.masksToBounds = true
            card.addSublayer(node.drawingLayer)
            drawStrokes(item.drawing ?? NotesDrawing(), on: node, size: node.drawingLayer.bounds.size)
            node.brushPreview.isHidden = true
            card.addSublayer(node.brushPreview)
        case .image:
            if let reference = item.media {
                if node.mediaReference != reference {
                    node.media?.dispose(); node.mediaReference = reference
                    let presentation = NotesMediaPresentation(reference: reference)
                    node.media = presentation
                    presentation.onProgress = { [weak self, weak node] in
                        guard let self, let node, let current = self.note(item.id) else { return }
                        self.withoutActions { self.updateMediaProgress(current, node: node, animated: true) }
                    }
                    presentation.onStateChange = { [weak self, weak node] in
                        guard let self, let node, let current = self.note(item.id) else { return }
                        self.withoutActions { self.updateMediaFooter(current, node: node); self.updateMediaProgress(current, node: node) }
                        self.onChange?()
                    }
                }
                if let media = node.media {
                    media.layer.frame = CGRect(x: 5, y: 29, width: item.width - 10, height: max(1, item.height - 56))
                    card.addSublayer(media.layer); media.setVisible(isVisible)
                    updateMediaFooter(item, node: node)
                    updateMediaProgress(item, node: node)
                }
                break
            }
            if node.imageName != item.imageName || node.image == nil {
                node.imageName = item.imageName
                if let url = store?.imageURL(for: item), let image = NSImage(contentsOf: url) {
                    var imageRect = CGRect(origin: .zero, size: image.size)
                    node.image = image.cgImage(forProposedRect: &imageRect, context: nil, hints: nil)
                }
            }
            if let image = node.image {
                let bitmap = CALayer()
                bitmap.frame = CGRect(x: 5, y: 29, width: item.width - 10, height: item.height - 35)
                bitmap.contents = image
                bitmap.contentsGravity = .resizeAspect
                bitmap.magnificationFilter = .linear
                bitmap.minificationFilter = .trilinear
                card.addSublayer(bitmap)
            } else {
                addText(L10n.text("Image unavailable", "图片不可用"), rect: CGRect(x: 10, y: 40, width: item.width - 20, height: 35), size: 11, color: muted, parent: card, wrapped: true)
            }
        }
        for action in actions(for: item) where action.id.contains(":format") {
            let frame = action.rect.offsetBy(dx: -CGFloat(item.x), dy: -CGFloat(item.y))
            let plate = CALayer(); plate.frame = frame
            plate.borderWidth = 0.5; plate.borderColor = border.cgColor
            plate.backgroundColor = NSColor(white: dark ? 0.18 : 0.82, alpha: 1).cgColor
            card.addSublayer(plate)
            let kind = String(action.id.split(separator: ":").last ?? "")
            if kind == "formatColor" {
                let swatch = node.formatSwatch; swatch.frame = plate.bounds.insetBy(dx: 4, dy: 4)
                swatch.backgroundColor = (item.kind == .drawing ? drawingColor.color : editingColor ?? primary).cgColor
                plate.addSublayer(swatch)
            } else {
                let symbols = ["formatSize": "A↕", "formatFont": "Aa", "formatSpecial": "B"]
                addText(symbols[kind] ?? action.label, rect: CGRect(x: 2, y: 3, width: frame.width - 4, height: 15), size: 10, color: primary, parent: plate)
            }
        }
        for action in actions(for: item) where !action.id.contains(":edit") && action.id.split(separator: ":").count == 3 {
            HUDControlHighlightLayer.add(to: card, rect: action.rect.offsetBy(dx: -CGFloat(item.x), dy: -CGFloat(item.y)))
        }
        do {
            let grip = CAShapeLayer()
            grip.frame = card.bounds
            let path = CGMutablePath()
            for inset: CGFloat in [4, 8] {
                path.move(to: CGPoint(x: CGFloat(item.width) - 3 - inset, y: CGFloat(item.height) - 3))
                path.addLine(to: CGPoint(x: CGFloat(item.width) - 3, y: CGFloat(item.height) - 3 - inset))
            }
            grip.path = path
            grip.strokeColor = (item.id == selectedNoteID ? accent : muted).cgColor
            grip.lineWidth = 1
            card.addSublayer(grip)
        }
    }

    private func mediaSeekRect(_ item: CanvasNote) -> CGRect {
        CGRect(x: 78, y: item.height - 25, width: max(1, item.width - 97), height: 20)
    }
    private func updateMediaSeek(at point: CGPoint) {
        guard var gesture = mediaSeek, let item = note(gesture.id), let duration = item.media?.duration,
              let node = nodes[gesture.id], point.x.isFinite else { return }
        let rect = mediaSeekRect(displayed(item))
        gesture.seconds = min(1, max(0, (point.x - item.x - rect.minX) / rect.width)) * duration
        mediaSeek = gesture
        withoutActions { updateMediaProgress(item, node: node) }
    }
    private func updateMediaProgress(_ item: CanvasNote, node: NoteNode, animated: Bool = false) {
        guard item.media?.kind == .video, let duration = item.media?.duration, duration > 0, let media = node.media else { return }
        let rect = mediaSeekRect(item)
        if node.mediaRail.superlayer !== node.layer {
            for layer in [node.mediaRail, node.mediaFill, node.mediaHandle] { node.layer.addSublayer(layer) }
        }
        node.mediaRail.frame = CGRect(x: rect.minX, y: rect.midY - 1, width: rect.width, height: 2)
        node.mediaRail.backgroundColor = muted.withAlphaComponent(0.3).cgColor
        node.mediaFill.backgroundColor = accent.cgColor; node.mediaHandle.backgroundColor = primary.cgColor
        node.mediaFill.anchorPoint = CGPoint(x: 0, y: 0.5); node.mediaFill.position = CGPoint(x: rect.minX, y: rect.midY)
        node.mediaHandle.bounds = CGRect(x: 0, y: 0, width: 5, height: 8)
        let preview = mediaSeek?.id == item.id ? mediaSeek?.seconds : nil
        let width = rect.width * min(1, max(0, (preview ?? media.currentTime) / duration))
        let interpolate = animated && media.isPlaying && preview == nil && isVisible && !reduceMotion()
        let target = interpolate ? min(rect.width, width + rect.width / duration) : width
        node.mediaFill.removeAllAnimations(); node.mediaHandle.removeAllAnimations()
        node.mediaFill.bounds = CGRect(x: 0, y: 0, width: target, height: 2)
        node.mediaHandle.position = CGPoint(x: rect.minX + target, y: rect.midY)
        if interpolate {
            let fill = CABasicAnimation(keyPath: "bounds.size.width"); fill.fromValue = width; fill.toValue = target
            fill.duration = 1; fill.timingFunction = CAMediaTimingFunction(name: .linear)
            node.mediaFill.add(fill, forKey: "notes.media.progress")
            let thumb = CABasicAnimation(keyPath: "position.x"); thumb.fromValue = rect.minX + width; thumb.toValue = rect.minX + target
            thumb.duration = 1; thumb.timingFunction = fill.timingFunction; node.mediaHandle.add(thumb, forKey: "notes.media.progress")
        }
    }

    private func updateMediaFooter(_ item: CanvasNote, node: NoteNode) {
        if node.footer.superlayer !== node.layer { node.layer.addSublayer(node.footer) }
        node.footer.frame = CGRect(x: 8, y: item.height - 23, width: item.media?.kind == .video ? 65 : item.width - 18, height: 18)
        node.footer.font = NSFont.systemFont(ofSize: 10); node.footer.fontSize = 10
        node.footer.foregroundColor = primary.cgColor; node.footer.truncationMode = .end
        node.footer.contentsScale = scale
        if let error = node.media?.error { node.footer.string = error.localizedDescription }
        else if node.media?.state == .loading { node.footer.string = L10n.text("Loading media…", "正在载入媒体…") }
        else if item.media?.kind == .image { node.footer.string = "" }
        else { node.footer.string = node.media?.isPlaying == true ? L10n.text("Ⅱ Pause", "Ⅱ 暂停") : L10n.text("▶ Play", "▶ 播放") }
    }

    private func layoutScrollableContent(_ item: CanvasNote) {
        guard let node = nodes[item.id] else { return }
        let viewport = contentViewport(item), geometry = contentGeometry(for: item)
        let maximum = max(0, geometry.height - viewport.height)
        let offset = min(maximum, max(0, scrollOffsets[item.id] ?? 0))
        scrollOffsets[item.id] = offset
        if node.viewport.superlayer !== node.layer { node.layer.addSublayer(node.viewport) }
        node.viewport.frame = viewport
        node.content.frame = CGRect(origin: .zero, size: viewport.size)
        node.content.bounds = CGRect(x: 0, y: offset, width: viewport.width, height: viewport.height)
        node.scrollThumb.isHidden = maximum <= 0
        if maximum > 0 {
            let height = max(10, viewport.height * viewport.height / geometry.height)
            node.scrollThumb.frame = CGRect(x: viewport.width - 2, y: offset / maximum * (viewport.height - height),
                                           width: 2, height: height)
            node.scrollThumb.backgroundColor = muted.withAlphaComponent(0.5).cgColor
        }
        if item.kind == .text {
            let value = item.text.isEmpty ? L10n.text("Double-click to write…", "双击输入…") : item.text
            let layout = wrappedText(value, noteID: item.id, width: viewport.width, fontSize: 12, richText: item.richText)
            let visible = editing?.noteID == item.id ? 0..<0 : layout.visibleLines(from: offset, to: offset + viewport.height)
            updateTextLines(&node.textLines, parent: node.content, layout: layout, visible: visible,
                origin: .zero, width: viewport.width, fontSize: 12,
                color: item.text.isEmpty ? muted : primary, strikethrough: false)
            return
        }

        let visible = geometry.visibleRows(offset: offset, height: viewport.height)
        let wanted = Set(visible.map { item.items[$0].id })
        for id in Array(node.rows.keys) where !wanted.contains(id) {
            node.rows.removeValue(forKey: id)?.layer.removeFromSuperlayer()
        }
        for index in visible {
            let child = item.items[index]
            let row: ChecklistRowNode
            if let existing = node.rows[child.id] { row = existing }
            else {
                row = ChecklistRowNode(); node.rows[child.id] = row
                row.layer.name = "notes.todo.row." + child.id.uuidString
                node.content.addSublayer(row.layer)
                makeChecklistControls(item, index: index, parent: row.layer)
            }
            row.layer.frame = CGRect(x: 0, y: geometry.rowOrigins[index], width: viewport.width, height: geometry.rowHeights[index])
            let value = child.text.isEmpty ? L10n.text("New item…", "新事项…") : child.text
            let width = max(20, CGFloat(item.width) - 89)
            let layout = wrappedText(value, noteID: item.id, itemID: child.id, width: width, fontSize: 11)
            let origin = CGPoint(x: 23, y: 3)
            let lines = editing?.noteID == item.id && editing?.itemID == child.id ? 0..<0
                : layout.visibleLines(from: offset - geometry.rowOrigins[index] - origin.y,
                                      to: offset + viewport.height - geometry.rowOrigins[index] - origin.y)
            updateTextLines(&row.lines, parent: row.layer, layout: layout, visible: lines,
                origin: origin, width: width, fontSize: 11,
                color: child.isChecked || child.text.isEmpty ? muted : primary,
                strikethrough: child.isChecked && !child.text.isEmpty)
        }
        if node.footer.superlayer !== node.layer { node.layer.addSublayer(node.footer) }
        node.footer.frame = CGRect(x: 10, y: item.height - 24, width: item.width - 24, height: 19)
        node.footer.string = L10n.text("+ Add item", "+ 添加事项")
        node.footer.font = NSFont.systemFont(ofSize: 10.5, weight: .medium)
        node.footer.fontSize = 10.5; node.footer.foregroundColor = primary.cgColor
        node.footer.contentsScale = HUDRenderScale.contentScale(for: node.footer, baseScale: scale)
    }

    private func updateTextLines(_ lines: inout [Int: CATextLayer], parent: CALayer, layout: NotesWrappedText,
                                 visible: Range<Int>, origin: CGPoint, width: CGFloat, fontSize: CGFloat,
                                 color: NSColor, strikethrough: Bool) {
        for index in Array(lines.keys) where !visible.contains(index) { lines.removeValue(forKey: index)?.removeFromSuperlayer() }
        for index in visible {
            let label: CATextLayer
            if let existing = lines[index] { label = existing }
            else {
                label = CATextLayer(); lines[index] = label
                label.name = "notes.content.line.\(index)"
                label.isWrapped = false; label.truncationMode = .none
                label.font = NSFont.systemFont(ofSize: fontSize); label.fontSize = fontSize
                label.contentsScale = HUDRenderScale.contentScale(for: label, baseScale: scale)
                label.string = layout.attributedLine(at: index, defaultColor: color, strikethrough: strikethrough)
                parent.addSublayer(label)
            }
            label.frame = CGRect(x: origin.x, y: origin.y + layout.origins[index], width: width, height: layout.heights[index])
        }
    }

    private func makeChecklistControls(_ item: CanvasNote, index: Int, parent: CALayer) {
        let child = item.items[index]
        let check = CAShapeLayer()
        check.frame = CGRect(x: 5, y: 4, width: 12, height: 12)
        check.path = CGPath(roundedRect: check.bounds, cornerWidth: 2, cornerHeight: 2, transform: nil)
        check.fillColor = child.isChecked ? accent.cgColor : nil
        check.strokeColor = child.isChecked ? accent.cgColor : muted.cgColor
        check.lineWidth = 1
        parent.addSublayer(check)
        if child.isChecked {
            let tick = CAShapeLayer()
            tick.frame = check.bounds
            let path = CGMutablePath(); path.move(to: CGPoint(x: 2.5, y: 6)); path.addLine(to: CGPoint(x: 5, y: 9)); path.addLine(to: CGPoint(x: 10, y: 3))
            tick.path = path; tick.fillColor = nil; tick.strokeColor = NSColor(white: 0.1, alpha: 1).cgColor; tick.lineWidth = 1.4
            check.addSublayer(tick)
        }
        drawChevron(in: CGRect(x: item.width - 58, y: 7, width: 7, height: 5), up: true,
                    color: index > 0 ? muted : border, parent: parent)
        drawChevron(in: CGRect(x: item.width - 40, y: 7, width: 7, height: 5), up: false,
                    color: index + 1 < item.items.count ? muted : border, parent: parent)
        drawCross(in: CGRect(x: item.width - 22, y: 6, width: 7, height: 7), color: muted, parent: parent)
        HUDControlHighlightLayer.add(to: parent, rect: CGRect(x: 0, y: 0, width: 21, height: 22))
        for trailing in [63.0, 45.0, 27.0] {
            HUDControlHighlightLayer.add(to: parent, rect: CGRect(x: item.width - trailing, y: 0, width: 17, height: 22))
        }
    }

    private func requestDeletion(_ id: UUID) {
        guard visibleNoteIDs.contains(id) else { return }
        pendingDeletionID = id
        withoutActions { renderDeletionControls() }
        if !reduceMotion() {
            let rise = CABasicAnimation(keyPath: "transform.translation.y")
            rise.fromValue = -6; rise.toValue = 0; rise.duration = 0.16
            rise.timingFunction = CAMediaTimingFunction(name: .easeOut)
            deletionControls.add(rise, forKey: "notes.confirm.reveal")
        }
        onChange?()
    }

    private func deletionActions() -> [NotesCanvasAction] {
        guard let id = pendingDeletionID, let original = note(id), visibleNoteIDs.contains(id) else { return [] }
        let item = displayed(original)
        // Right-align beneath the header's delete button, not the card footer.
        // These same rectangles drive rendering, pointer hits and accessibility.
        let x = min(workspaceBounds.maxX - 56, max(workspaceBounds.minX, CGFloat(item.x + item.width) - 59))
        let y = min(workspaceBounds.maxY - 25, max(workspaceBounds.minY, CGFloat(item.y) + 27))
        return [NotesCanvasAction(id: noteAction(item, "cancelDelete"), label: L10n.text("Cancel deletion", "取消删除"),
                                  rect: CGRect(x: x, y: y, width: 25, height: 25)),
                NotesCanvasAction(id: noteAction(item, "confirmDelete"), label: L10n.text("Confirm deletion", "确认删除"),
                                  rect: CGRect(x: x + 31, y: y, width: 25, height: 25))]
    }

    private func renderDeletionControls() {
        deletionControls.sublayers?.forEach { $0.removeFromSuperlayer() }
        deletionControls.frame = CGRect(origin: .zero, size: workspaceBounds.size)
        let actions = deletionActions()
        deletionControls.isHidden = actions.isEmpty
        for (index, action) in actions.enumerated() {
            let plate = CAShapeLayer()
            plate.frame = action.rect.offsetBy(dx: -workspaceBounds.minX, dy: -workspaceBounds.minY)
            plate.path = CGPath(roundedRect: plate.bounds, cornerWidth: 3, cornerHeight: 3, transform: nil)
            plate.fillColor = NSColor(white: dark ? 0.15 : 0.91, alpha: 0.98).cgColor
            plate.strokeColor = (index == 1 ? accent : border).cgColor
            plate.lineWidth = 1
            deletionControls.addSublayer(plate)
            HUDControlHighlightLayer.add(to: plate, rect: plate.bounds)
            if index == 0 { drawCross(in: CGRect(x: 8, y: 8, width: 9, height: 9), color: primary, parent: plate) }
            else {
                let tick = CGMutablePath()
                tick.move(to: CGPoint(x: 6, y: 12)); tick.addLine(to: CGPoint(x: 10, y: 16)); tick.addLine(to: CGPoint(x: 19, y: 7))
                stroke(tick, in: plate.bounds, color: accent, parent: plate)
            }
        }
    }

    private func animateToolbar(_ id: String) {
        guard !reduceMotion(), let plate = toolbar.sublayers?.first(where: { $0.name == id }) else { return }
        let press = CAKeyframeAnimation(keyPath: "transform.translation.y")
        press.values = [0, 2, 0]; press.keyTimes = [0, 0.3, 1]; press.duration = 0.18
        press.timingFunctions = [CAMediaTimingFunction(name: .easeOut), CAMediaTimingFunction(name: .easeInEaseOut)]
        plate.add(press, forKey: "notes.add.press")
    }

    private func animateMutation(_ id: UUID) {
        guard !reduceMotion(), let card = nodes[id]?.layer, !card.isHidden else { return }
        let lift = CAKeyframeAnimation(keyPath: "transform.translation.y")
        lift.values = [0, -3, 0]; lift.keyTimes = [0, 0.32, 1]; lift.duration = 0.2
        lift.timingFunctions = [CAMediaTimingFunction(name: .easeOut), CAMediaTimingFunction(name: .easeInEaseOut)]
        card.add(lift, forKey: "notes.action")
    }

    private func animateCard(_ card: CALayer, appearing: Bool) {
        guard !reduceMotion() else { return }
        let scale = CABasicAnimation(keyPath: "transform.scale")
        scale.fromValue = appearing ? 0.94 : 1; scale.toValue = appearing ? 1 : 0.94
        let travel = CABasicAnimation(keyPath: "transform.translation.y")
        travel.fromValue = appearing ? 12 : 0; travel.toValue = appearing ? 0 : -8
        let opacity = CABasicAnimation(keyPath: "opacity")
        opacity.fromValue = appearing ? 0 : 1; opacity.toValue = appearing ? 1 : 0
        let group = CAAnimationGroup()
        group.animations = [scale, travel, opacity]; group.duration = 0.2
        group.timingFunction = CAMediaTimingFunction(name: appearing ? .easeOut : .easeIn)
        card.add(group, forKey: "notes.visibility")
    }

    /// Workspace cards are outside the center's clipping host. Give only the
    /// changing cards the same duration and direction as that module handoff;
    /// pinned cards remain attached to their existing workspace positions.
    private func animatePresentation(_ card: CALayer, appearing: Bool, direction: CGPoint) {
        guard !reduceMotion() else { return }
        let distance: CGFloat = appearing ? 18 : -14
        let offset = CATransform3DMakeTranslation(direction.x * distance, direction.y * distance, 0)
        let travel = CABasicAnimation(keyPath: "transform")
        travel.fromValue = NSValue(caTransform3D: appearing ? offset : CATransform3DIdentity)
        travel.toValue = NSValue(caTransform3D: appearing ? CATransform3DIdentity : offset)
        let opacity = CABasicAnimation(keyPath: "opacity")
        opacity.fromValue = appearing ? 0 : 1; opacity.toValue = appearing ? 1 : 0
        let group = CAAnimationGroup()
        group.animations = [travel, opacity]
        group.duration = HUDModuleContent.transitionDuration
        group.timingFunction = CAMediaTimingFunction(controlPoints: 0.18, 0.72, 0.26, 1)
        card.add(group, forKey: "notes.section")
    }

    var activeAnimationCount: Int {
        func count(_ layer: CALayer) -> Int {
            (layer.animationKeys()?.count ?? 0) + (layer.sublayers ?? []).reduce(0) { $0 + count($1) }
        }
        return count(workspaceLayer) + count(layer)
    }

    func cancelAnimations() {
        presentationGeneration += 1
        func remove(_ layer: CALayer) {
            layer.removeAllAnimations(); layer.sublayers?.forEach(remove)
        }
        remove(workspaceLayer); remove(layer)
        retiringLayers.forEach { $0.removeFromSuperlayer() }; retiringLayers.removeAll()
        withoutActions {
            for item in notes {
                nodes[item.id]?.layer.isHidden = !notesSelected && !item.isPinned
                nodes[item.id]?.layer.opacity = notesSelected || item.isPinned ? 1 : 0
            }
        }
    }

    private func addText(_ string: String, rect: CGRect, size: CGFloat, color: NSColor, parent: CALayer,
                         weight: NSFont.Weight = .regular, wrapped: Bool = false) {
        let text = CATextLayer()
        text.frame = rect
        text.string = string
        text.font = NSFont.systemFont(ofSize: size, weight: weight)
        text.fontSize = size
        text.foregroundColor = color.cgColor
        text.isWrapped = wrapped
        text.truncationMode = wrapped ? .none : .end
        text.contentsScale = HUDRenderScale.contentScale(for: text, baseScale: scale)
        parent.addSublayer(text)
    }

    private func drawPin(in rect: CGRect, pinned: Bool, parent: CALayer) {
        let path = CGMutablePath()
        path.move(to: CGPoint(x: 3, y: 1)); path.addLine(to: CGPoint(x: 9, y: 1))
        path.move(to: CGPoint(x: 4, y: 1)); path.addLine(to: CGPoint(x: 4, y: 5)); path.addLine(to: CGPoint(x: 2, y: 7)); path.addLine(to: CGPoint(x: 10, y: 7)); path.addLine(to: CGPoint(x: 8, y: 5)); path.addLine(to: CGPoint(x: 8, y: 1))
        path.move(to: CGPoint(x: 6, y: 7)); path.addLine(to: CGPoint(x: 6, y: 12))
        stroke(path, in: rect, color: pinned ? accent : muted, parent: parent)
    }
    private func drawCross(in rect: CGRect, color: NSColor, parent: CALayer) {
        let p = CGMutablePath(); p.move(to: .zero); p.addLine(to: CGPoint(x: rect.width, y: rect.height)); p.move(to: CGPoint(x: rect.width, y: 0)); p.addLine(to: CGPoint(x: 0, y: rect.height))
        stroke(p, in: rect, color: color, parent: parent)
    }
    private func drawChevron(in rect: CGRect, up: Bool, color: NSColor, parent: CALayer) {
        let p = CGMutablePath(); p.move(to: CGPoint(x: 0, y: up ? rect.height : 0)); p.addLine(to: CGPoint(x: rect.width / 2, y: up ? 0 : rect.height)); p.addLine(to: CGPoint(x: rect.width, y: up ? rect.height : 0))
        stroke(p, in: rect, color: color, parent: parent)
    }
    private func stroke(_ path: CGPath, in rect: CGRect, color: NSColor, parent: CALayer) {
        let shape = CAShapeLayer(); shape.frame = rect; shape.path = path; shape.fillColor = nil; shape.strokeColor = color.cgColor; shape.lineWidth = 1; shape.lineCap = .round; shape.lineJoin = .round
        parent.addSublayer(shape)
    }
    private func reportUnavailable() {
        errorMessage = L10n.text("Notes storage is unavailable.", "便笺存储不可用。")
        withoutActions { updateStatus() }
        onChange?()
    }
    private func report(_ error: Error) {
        errorMessage = error.localizedDescription
        withoutActions { updateStatus() }
        onChange?()
    }
    private func withoutActions(_ action: () -> Void) {
        CATransaction.begin(); CATransaction.setDisableActions(true); action(); CATransaction.commit()
    }
}
