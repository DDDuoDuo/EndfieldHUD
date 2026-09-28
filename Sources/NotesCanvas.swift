import AppKit
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
}

enum NotesCoordinateSpace { case module, workspace }

struct NotesCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
    var space: NotesCoordinateSpace = .workspace
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
    var onChange: (() -> Void)?
    private(set) var selectedNoteID: UUID?
    var isDragging: Bool { drag != nil }
    var hasSelection: Bool { selectedNoteID.map { visibleNoteIDs.contains($0) } ?? false }
    func clearSelection() { select(nil) }
    var noteCount: Int { notes.count }
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

    private enum Tool: String { case text, todo, image }
    private struct Drag {
        let original: CanvasNote
        let start: CGPoint
        let resizing: Bool
        var changed: Bool
    }
    private final class NoteNode {
        let layer = CALayer()
        var imageName: String?
        var image: CGImage?
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
    private var editing: NotesEditRequest?
    private var rowOffsets: [UUID: Int] = [:]
    private var errorMessage: String?
    private var unsavedNoteIDs: Set<UUID> = []
    private var dark = true
    private var accent = NSColor(srgbRed: 0.98, green: 0.83, blue: 0.12, alpha: 1)
    private var scale: CGFloat = 2
    private var languageIsChinese = L10n.isChinese
    private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.11, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.65 : 0.37, alpha: 1) }
    private var border: NSColor { NSColor(white: dark ? 0.72 : 0.24, alpha: dark ? 0.28 : 0.24) }
    private let headerHeight: CGFloat = 24
    private let rowHeight: CGFloat = 25

    init(store: NotesStore?, error: String? = nil, notesSelected: Bool = true, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.reduceMotion = reduceMotion
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

    /// Pinned cards belong to the retained workspace, so their appearance must
    /// update even when a different center module is selected.
    func updateAppearance(style: HUDModuleContentStyle) {
        guard dark != style.dark || !accent.isEqual(style.accent)
                || scale != style.contentsScale || languageIsChinese != L10n.isChinese else { return }
        dark = style.dark
        accent = style.accent
        scale = style.contentsScale
        languageIsChinese = L10n.isChinese
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

    func setPresentation(notesSelected selected: Bool, animated: Bool = true) {
        guard notesSelected != selected else { return }
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
            guard visible != wasVisible else { continue }
            node.layer.removeAllAnimations()
            if visible {
                withoutActions { node.layer.isHidden = false; node.layer.opacity = 1 }
                if animated { animateCard(node.layer, appearing: true) }
            } else if animated && !reduceMotion() {
                CATransaction.begin()
                CATransaction.setCompletionBlock { [weak self, weak node] in
                    guard self?.presentationGeneration == generation else { return }
                    node?.layer.isHidden = true
                }
                animateCard(node.layer, appearing: false)
                withoutActions { node.layer.opacity = 0 }
                CATransaction.commit()
            } else { withoutActions { node.layer.isHidden = true; node.layer.opacity = 0 } }
        }
        withoutActions { renderDeletionControls() }
        onChange?()
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
        guard let gesture = drag else { return }
        drag = nil
        if gesture.changed, let note = note(gesture.original.id) { save(note) }
        onChange?()
    }

    /// Checklist overflow moves in discrete rows, without a display timer.
    @discardableResult
    func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard delta.isFinite, abs(delta) > 0.01, let note = topNote(at: point), note.kind == .todo else { return false }
        let maximum = max(0, note.items.count - visibleRowCount(displayed(note)))
        guard maximum > 0 else { return true }
        let previous = rowOffsets[note.id] ?? 0
        let next = min(maximum, max(0, previous + (delta > 0 ? 1 : -1)))
        guard previous != next else { return true }
        rowOffsets[note.id] = next
        withoutActions { render(note) }
        onChange?()
        return true
    }

    func deleteSelection() {
        guard let id = selectedNoteID else { return }
        requestDeletion(id)
    }

    func setEditing(_ request: NotesEditRequest?) {
        let previous = editing?.noteID
        editing = request
        withoutActions {
            if let id = previous, let item = note(id) { render(item) }
            if let request = request, request.noteID != previous, let item = note(request.noteID) { render(item) }
        }
    }

    func finishEditing(_ request: NotesEditRequest, text: String) {
        guard var item = note(request.noteID) else { return }
        if let id = request.itemID, let index = item.items.firstIndex(where: { $0.id == id }) {
            item.items[index].text = text
        } else if request.itemID == nil && item.kind == .text {
            item.text = text
        } else { return }
        editing = nil
        replace(item)
        save(item)
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

    /// Called before switching sections or closing. Finishes an active geometry
    /// gesture immediately; no pending animation, task, or timer survives.
    func cancelInteraction() {
        mouseUp(); pendingDeletionID = nil
        presentationGeneration += 1
        cancelAnimations()
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
            rowOffsets[id] = max(0, item.items.count - visibleRowCount(displayed(item)))
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
        item.width = kind == .todo ? 228 : 162
        item.height = kind == .todo ? 154 : 104
        item.zIndex = nextZIndex()
        if kind == .todo { item.items = [NoteChecklistItem(text: "")] }
        item = NotesGeometry.constrained(item, in: workspaceBounds)
        replace(item)
        save(item)
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
            let visible = visibleRowCount(item)
            let offset = rowOffsets[item.id] ?? 0
            if index < offset || index >= offset + visible {
                rowOffsets[item.id] = max(0, index - visible + 1)
                withoutActions { render(item) }
            }
            let row = index - (rowOffsets[item.id] ?? 0)
            let editRect = CGRect(x: item.x + 28, y: item.y + Double(headerHeight + CGFloat(row) * rowHeight + 5),
                                  width: max(36, item.width - 89), height: 19)
            request = NotesEditRequest(noteID: item.id, itemID: id, text: item.items[index].text, rect: editRect, fontSize: 11, multiline: false)
        } else {
            guard item.kind == .text else { return }
            request = NotesEditRequest(noteID: item.id, itemID: nil, text: item.text,
                rect: CGRect(x: item.x + 9, y: item.y + 29, width: item.width - 18, height: item.height - 40), fontSize: 12, multiline: true)
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
            rowOffsets.removeValue(forKey: id)
            unsavedNoteIDs.remove(id)
            if selectedNoteID == id { selectedNoteID = nil }
            if editing?.noteID == id { editing = nil }
            withoutActions { updateStatus(); renderDeletionControls() }
            onChange?()
        } catch { report(error) }
    }

    private func save(_ item: CanvasNote) {
        guard let store = store else { reportUnavailable(); return }
        do {
            try store.upsert(item)
            unsavedNoteIDs.remove(item.id)
            if unsavedNoteIDs.isEmpty { errorMessage = nil }
            withoutActions { updateStatus() }
        } catch {
            unsavedNoteIDs.insert(item.id)
            report(error)
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
        if let index = notes.firstIndex(where: { $0.id == item.id }) { notes[index] = item }
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
    private func visibleRowCount(_ item: CanvasNote) -> Int { max(1, Int((CGFloat(item.height) - headerHeight - 28) / rowHeight)) }
    private func noteAction(_ item: CanvasNote, _ verb: String) -> String { "note:\(item.id.uuidString):\(verb)" }
    private func noteLabel(_ item: CanvasNote) -> String {
        let title = item.kind == .text ? L10n.text("Text", "文字") : item.kind == .todo ? L10n.text("Checklist", "待办") : L10n.text("Image", "图片")
        return item.kind == .text && !item.text.isEmpty ? "\(title): \(item.text.prefix(64))" : title
    }

    private func toolbarActions() -> [NotesCanvasAction] {
        [NotesCanvasAction(id: "tool:text", label: L10n.text("Text", "文字"), rect: CGRect(x: 39, y: 294, width: 102, height: 31), space: .module),
         NotesCanvasAction(id: "tool:todo", label: L10n.text("TODO", "待办"), rect: CGRect(x: 149, y: 294, width: 102, height: 31), space: .module),
         NotesCanvasAction(id: "tool:image", label: L10n.text("Image", "图片"), rect: CGRect(x: 259, y: 294, width: 102, height: 31), space: .module)]
    }

    private func actions(for original: CanvasNote) -> [NotesCanvasAction] {
        let item = displayed(original)
        var output = [NotesCanvasAction(id: noteAction(item, "pin"), label: item.isPinned ? L10n.text("Unpin note", "取消固定") : L10n.text("Pin note", "固定便笺"), rect: CGRect(x: item.x + item.width - 46, y: item.y + 2, width: 21, height: 20)),
                      NotesCanvasAction(id: noteAction(item, "delete"), label: L10n.text("Delete note", "删除便笺"), rect: CGRect(x: item.x + item.width - 24, y: item.y + 2, width: 21, height: 20))]
        if item.kind == .text {
            output.append(NotesCanvasAction(id: noteAction(item, "edit"), label: L10n.text("Edit text", "编辑文字"), rect: CGRect(x: item.x + 4, y: item.y + 25, width: item.width - 8, height: item.height - 30)))
        } else if item.kind == .todo {
            let visible = visibleRowCount(item)
            let offset = min(max(0, item.items.count - visible), rowOffsets[item.id] ?? 0)
            for index in offset..<min(item.items.count, offset + visible) {
                let child = item.items[index]
                let y = item.y + Double(headerHeight + CGFloat(index - offset) * rowHeight + 3)
                let suffix = ":\(child.id.uuidString)"
                output.append(NotesCanvasAction(id: noteAction(item, "check") + suffix, label: child.isChecked ? L10n.text("Uncheck", "取消勾选") + " " + child.text : L10n.text("Check", "勾选") + " " + child.text, rect: CGRect(x: item.x + 5, y: y, width: 21, height: 22)))
                output.append(NotesCanvasAction(id: noteAction(item, "editItem") + suffix, label: L10n.text("Edit item", "编辑事项") + " " + child.text, rect: CGRect(x: item.x + 28, y: y, width: max(20, item.width - 89), height: 22)))
                let controls: [(String, String, Double)] = [("up", L10n.text("Move up", "上移"), 58), ("down", L10n.text("Move down", "下移"), 40), ("remove", L10n.text("Delete item", "删除事项"), 22)]
                for (verb, label, trailing) in controls {
                    output.append(NotesCanvasAction(id: noteAction(item, verb) + suffix, label: label, rect: CGRect(x: item.x + item.width - trailing, y: y, width: 17, height: 22)))
                }
            }
            output.append(NotesCanvasAction(id: noteAction(item, "add"), label: L10n.text("Add item", "添加事项"), rect: CGRect(x: item.x + 7, y: item.y + item.height - 26, width: item.width - 29, height: 21)))
        }
        return output
    }

    private func repaint() {
        heading.string = L10n.text("NOTES", "便笺")
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
            let symbol = action.id == "tool:text" ? "T" : action.id == "tool:todo" ? "☑" : "▧"
            let color = primary
            let gameIcon: EndfieldGameIcon? = action.id == "tool:text" ? .operationalManual : action.id == "tool:todo" ? .mission : nil
            if gameIcon?.add(to: plate, rect: CGRect(x: 9, y: 7, width: 18, height: 18),
                             tint: color, contentsScale: scale) != true {
                addText(symbol, rect: CGRect(x: 9, y: 7, width: 18, height: 18), size: 14, color: color, parent: plate, weight: .semibold)
            }
            addText(action.label, rect: CGRect(x: 31, y: 8, width: 65, height: 17), size: 11.5, color: color, parent: plate, weight: .semibold)
        }
    }

    private func render(_ original: CanvasNote) {
        // Unpinned cards are not visible on another section. In particular,
        // do not decode all stored note images merely to summon the map.
        guard notesSelected || original.isPinned else {
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
        let head = CALayer()
        head.frame = CGRect(x: 0, y: 0, width: item.width, height: Double(headerHeight))
        head.backgroundColor = NSColor(white: dark ? 0.16 : 0.82, alpha: 1).cgColor
        card.addSublayer(head)
        let title = item.kind == .text ? L10n.text("TEXT", "文字") : item.kind == .todo ? L10n.text("TODO", "待办") : L10n.text("IMAGE", "图片")
        addText("⠿  " + title, rect: CGRect(x: 7, y: 6, width: item.width - 56, height: 14), size: 9, color: muted, parent: head, weight: .semibold)
        drawPin(in: CGRect(x: item.width - 41, y: 6, width: 12, height: 12), pinned: item.isPinned, parent: head)
        drawCross(in: CGRect(x: item.width - 17, y: 8, width: 7, height: 7), color: muted, parent: head)
        switch item.kind {
        case .text:
            if editing?.noteID != item.id {
                addText(item.text.isEmpty ? L10n.text("Double-click to write…", "双击输入…") : item.text,
                    rect: CGRect(x: 9, y: 29, width: item.width - 18, height: item.height - 40), size: 12,
                    color: item.text.isEmpty ? muted : primary, parent: card, wrapped: true)
            }
        case .todo: renderChecklist(item, parent: card)
        case .image:
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
        for action in actions(for: item) where !action.id.contains(":edit") {
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

    private func renderChecklist(_ item: CanvasNote, parent: CALayer) {
        let count = visibleRowCount(item)
        let offset = min(max(0, item.items.count - count), rowOffsets[item.id] ?? 0)
        rowOffsets[item.id] = offset
        for index in offset..<min(item.items.count, offset + count) {
            let child = item.items[index]
            let y = headerHeight + CGFloat(index - offset) * rowHeight + 7
            let check = CAShapeLayer()
            check.frame = CGRect(x: 10, y: y, width: 12, height: 12)
            check.path = CGPath(roundedRect: check.bounds, cornerWidth: 2, cornerHeight: 2, transform: nil)
            check.fillColor = child.isChecked ? accent.cgColor : nil
            check.strokeColor = child.isChecked ? accent.cgColor : muted.cgColor
            check.lineWidth = 1
            parent.addSublayer(check)
            if child.isChecked {
                let tick = CAShapeLayer()
                tick.frame = check.bounds
                let p = CGMutablePath(); p.move(to: CGPoint(x: 2.5, y: 6)); p.addLine(to: CGPoint(x: 5, y: 9)); p.addLine(to: CGPoint(x: 10, y: 3))
                tick.path = p; tick.fillColor = nil; tick.strokeColor = NSColor(white: 0.1, alpha: 1).cgColor; tick.lineWidth = 1.4
                check.addSublayer(tick)
            }
            if !(editing?.noteID == item.id && editing?.itemID == child.id) {
                addText(child.text.isEmpty ? L10n.text("New item…", "新事项…") : child.text,
                    rect: CGRect(x: 28, y: y - 1, width: max(20, item.width - 89), height: 18), size: 11,
                    color: child.isChecked || child.text.isEmpty ? muted : primary, parent: parent)
            }
            drawChevron(in: CGRect(x: item.width - 53, y: Double(y) + 3, width: 7, height: 5), up: true,
                        color: index > 0 ? muted : border, parent: parent)
            drawChevron(in: CGRect(x: item.width - 35, y: Double(y) + 3, width: 7, height: 5), up: false,
                        color: index + 1 < item.items.count ? muted : border, parent: parent)
            drawCross(in: CGRect(x: item.width - 17, y: Double(y) + 2, width: 7, height: 7), color: muted, parent: parent)
        }
        let footer = item.items.count > count ? "\(offset + 1)–\(min(item.items.count, offset + count))/\(item.items.count)" : ""
        addText(L10n.text("+ Add item", "+ 添加事项"), rect: CGRect(x: 10, y: item.height - 24, width: item.width - 68, height: 19), size: 10.5, color: primary, parent: parent, weight: .medium)
        if !footer.isEmpty { addText(footer, rect: CGRect(x: item.width - 66, y: item.height - 22, width: 47, height: 17), size: 9, color: muted, parent: parent) }
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
        let x = min(workspaceBounds.maxX - 60, max(workspaceBounds.minX, CGFloat(item.x + item.width / 2) - 28))
        let y = min(workspaceBounds.maxY - 27, CGFloat(item.y + item.height) + 5)
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
