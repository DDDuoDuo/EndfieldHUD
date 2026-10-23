import AppKit
import QuartzCore

/// The centre's own TODO list and text note (spec §3.6) with its tab and
/// Hide/Show state. The desktop cards stay in the NotesStore database.
struct NotesCentreDocument: Codable, Equatable {
    enum Tab: String, Codable { case text, todo }
    static let maximumTaskLength = 60

    var version = 1
    var tasks: [NoteChecklistItem] = []
    var text = ""
    /// Per-run formatting of `text` (any font, size, colour, B / I / U / S);
    /// after the first edit the runs cover the whole text.
    var richText: NotesRichText? = nil
    /// The whole-note style of the first redesign build: the base of text
    /// without runs.
    var style = NotesNoteStyle()
    var tab: Tab = .todo
    /// Hide: the TEXT / TODO area is folded away and only the add bar stays.
    var isFolded = false
    /// Tasks that carry the orange dot.
    var flagged: Set<UUID> = []

    init() {}
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        version = try c.decodeIfPresent(Int.self, forKey: .version) ?? 1
        tasks = try c.decodeIfPresent([NoteChecklistItem].self, forKey: .tasks) ?? []
        text = try c.decodeIfPresent(String.self, forKey: .text) ?? ""
        richText = try c.decodeIfPresent(NotesRichText.self, forKey: .richText)
        style = try c.decodeIfPresent(NotesNoteStyle.self, forKey: .style) ?? NotesNoteStyle()
        tab = try c.decodeIfPresent(Tab.self, forKey: .tab) ?? .todo
        isFolded = try c.decodeIfPresent(Bool.self, forKey: .isFolded) ?? false
        flagged = try c.decodeIfPresent(Set<UUID>.self, forKey: .flagged) ?? []
    }

    var isValid: Bool { version == 1 && Set(tasks.map(\.id)).count == tasks.count && style.isValid }
    var doneCount: Int { tasks.filter(\.isChecked).count }
    /// An open task carries the orange dot (the TODO tab shows one too).
    func showsDot(_ task: NoteChecklistItem) -> Bool { flagged.contains(task.id) && !task.isChecked }
    var hasDot: Bool { tasks.contains(where: showsDot) }
}

/// centre.json beside notes.sqlite3. It is written atomically after an edit,
/// never on a timer; a file that cannot be read is set aside, not overwritten.
/// Without a directory (storage unavailable) the document lives in memory.
final class NotesCentreStore {
    static let fileName = "centre.json"
    private(set) var document = NotesCentreDocument()
    let url: URL?
    /// Why the stored document could not be read (it was moved aside).
    private(set) var loadError: String?

    init(directory: URL?) {
        guard let directory else { url = nil; return }
        let url = directory.appendingPathComponent(Self.fileName)
        self.url = url
        guard let data = try? Data(contentsOf: url) else { return }
        if let value = try? JSONDecoder().decode(NotesCentreDocument.self, from: data), value.isValid { document = value; return }
        let stamp = ISO8601DateFormatter().string(from: Date()).replacingOccurrences(of: ":", with: "-")
        try? FileManager.default.moveItem(at: url, to: directory.appendingPathComponent("centre-unreadable-\(stamp).json"))
        loadError = L10n.text("The centre notes could not be read and were set aside.", "无法读取中央便笺，原文件已另存。")
    }

    /// Applies `change`; the document changes even when writing fails (the
    /// next edit writes everything again), and the error is rethrown.
    func update(_ change: (inout NotesCentreDocument) -> Void) throws {
        var next = document
        change(&next)
        guard next != document, next.isValid else { return }
        document = next
        guard let url else { return }
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try encoder.encode(next).write(to: url, options: .atomic)
    }
}

/// The redesigned Notes centre (spec §3.6): TEXT / TODO tabs, the TODO task
/// cards with the add field, the rich text note with its format bar,
/// Hide/Show, and the add bar that creates notes on the desktop. The desktop
/// cards themselves are NotesWorkspace; the host's workspace calls are
/// forwarded to it.
final class NotesCanvas: HUDDesignCanvas, NSTextViewDelegate {
    let workspace: NotesWorkspace
    let centre: NotesCentreStore
    /// The Media add button's two sources; HUDNotesInteraction runs them.
    var onChooseFiles: ((CGPoint) -> Void)?
    var onChooseShelf: ((CGPoint) -> Void)?
    /// Note events for the event log (creation, edits, deletion).
    var onAction: ((NotesCanvasEvent) -> Void)?
    private let storageError: String?
    private var tabs: HUDTabs?
    private var formatBar: NotesFormatBar?
    private var cards: [UUID: HUDTaskCard] = [:]
    private weak var taskList: HUDScrollArea?
    private weak var body: CALayer?
    private weak var bodyContent: CALayer?
    private weak var addBar: CALayer?
    private weak var mediaButton: NotesAddButton?
    private var addField: HUDDesignTextField?
    /// The TEXT editor: a projected rich-text view kept across re-renders so
    /// focus, selection, composition and undo survive (Archive's body area).
    private var noteArea: ArchiveBodyArea?
    private var noteEdited = false
    /// The task whose title is being edited in place, and its field.
    private var editingTask: (id: UUID, field: HUDDesignTextField)?
    private var reorder: Reorder?
    private var expandedBodyHeight: CGFloat = 0
    private var reportedStatus: String?
    private var reportedLoadProblem = false

    /// A card lifted by its grip: it follows the pointer and the others close up.
    private struct Reorder {
        let id: UUID
        let grab: CGFloat
        var index: Int
        var order: [UUID]
    }

    init(context: HUDDesignContext, store: NotesStore?, error: String? = nil, notesSelected: Bool = true,
         textLayoutRangeBudget: Int = NotesWorkspace.textLayoutRangeBudget,
         reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }, centre: NotesCentreStore? = nil) {
        workspace = NotesWorkspace(store: store, error: error, notesSelected: notesSelected,
                                   textLayoutRangeBudget: textLayoutRangeBudget, reduceMotion: reduceMotion)
        self.centre = centre ?? NotesCentreStore(directory: store?.directoryURL)
        storageError = store == nil ? (error ?? L10n.text("Notes storage is unavailable.", "便笺存储不可用。")) : nil
        super.init(module: .notes, context: context)
        workspace.onAction = { [weak self] event in self?.onAction?(event); self?.workspaceDidAct(event) }
        workspace.onChooseImage = { [weak self] point in self?.presentMediaSources(at: point) }
        workspace.onStatusChange = { [weak self] in self?.reportStatus() }
    }

    deinit { noteArea?.dispose() }

    // MARK: Workspace, as the host addresses it

    var workspaceLayer: CALayer { workspace.workspaceLayer }
    var accessibleActions: [NotesCanvasAction] { workspace.accessibleActions }
    var deploymentLayers: [CALayer] { workspace.deploymentLayers }
    var visibleNoteIDs: Set<UUID> { workspace.visibleNoteIDs }
    /// Centre controls (a menu scroll bar, a task grip) or a desktop card are
    /// being dragged. The host routes drags to the workspace bridge while this
    /// is true; it forwards the centre's part back (HUDNotesInteraction).
    override var isDragging: Bool { super.isDragging || workspace.isDragging }
    func perform(actionID: String) { workspace.perform(actionID: actionID) }
    func contains(noteID: UUID, point: CGPoint) -> Bool { workspace.contains(noteID: noteID, point: point) }
    func containsWorkspacePoint(_ point: CGPoint) -> Bool { workspace.containsWorkspacePoint(point) }
    func setPresentation(notesSelected: Bool, animated: Bool = true, direction: CGPoint = CGPoint(x: 0, y: 1)) {
        workspace.setPresentation(notesSelected: notesSelected, animated: animated, direction: direction)
    }
    func setWorkspaceBounds(_ bounds: CGRect, creationPoint: CGPoint? = nil) { workspace.setWorkspaceBounds(bounds, creationPoint: creationPoint) }
    func installNoteForVerification(_ note: CanvasNote) throws { try workspace.installNoteForVerification(note) }
    func scrollOffset(for noteID: UUID) -> CGFloat { workspace.scrollOffset(for: noteID) }
    func clearSelection() { workspace.clearSelection() }
    func updateAppearance(style: HUDModuleContentStyle) { workspace.updateAppearance(style: style) }
    /// The desktop notes draw in screen points at the window's backing scale.
    func setWorkspaceBackingScale(_ value: CGFloat) { workspace.updateRenderScale(value) }
    func reportImportError(_ error: Error) { workspace.reportImportError(error) }

    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        workspace.updateAppearance(style: style)
        let content = super.makeContent(for: module, style: style)
        attachNote()
        return content
    }

    // MARK: Lifecycle

    /// Storage problems are shown once, the first time Notes opens.
    override func didActivate() {
        attachNote()
        guard !reportedLoadProblem, let error = storageError ?? centre.loadError else { return }
        reportedLoadProblem = true
        toast(icon: .warn, "Notice", "提示", detail: error)
    }

    override func didDeactivate() {
        cancelReorder()
        saveNote()
        noteArea?.suspend()
    }

    override func willLeave() { saveNote() }

    override func recordingModeChanged() {
        endEditing(commit: true)
        super.recordingModeChanged()
    }

    // MARK: Page

    private var document: NotesCentreDocument { centre.document }

    private var counterText: String {
        document.tab == .todo ? HUDFormat.pad2(document.doneCount) + "/" + HUDFormat.pad2(document.tasks.count) : "01"
    }

    override func buildPage(_ page: HUDDesignPage) {
        cards = [:]; tabs = nil; formatBar = nil; reorder = nil
        if let editing = editingTask, !editing.field.isEditing { editingTask = nil }
        let doc = document
        let folded = doc.isFolded && storageError == nil
        header.configure(crumbs: ["NOTES"], code: "MOD-01", title: HUDModuleTitle("Notes", "便笺"), counter: counterText,
                         showsBack: false, actions: storageError == nil ? [foldButton(folded)] : [])
        let clip = HUDLayer.plain("notes.body"), content = HUDLayer.plain("notes.body.content")
        HUDMotion.perform {
            clip.masksToBounds = true; clip.anchorPoint = CGPoint(x: 0.5, y: 0)
            clip.addSublayer(content); page.layer.addSublayer(clip)
        }
        var staggered: [CALayer] = []
        let contentHeight: CGFloat
        var showsNote = false
        if storageError != nil {
            let empty = HUDEmptyState.bilingual("NOTES UNAVAILABLE", "便笺存储不可用", size: CGSize(width: 586, height: 250), env: env)
            HUDMotion.perform { empty.frame.origin = CGPoint(x: 2, y: 4); content.addSublayer(empty) }
            staggered.append(empty)
            contentHeight = 4 + 250 + 2
        } else {
            let bar = HUDTabs(id: "notes.tabs", titles: [(L10n.text("TEXT", "文字"), .text), (L10n.text("TODO", "待办"), .todo)],
                              selectedIndex: doc.tab == .text ? 0 : 1, width: 586, env: env)
            bar.tabs[1].hasNews = doc.hasDot
            bar.onSelect = { [weak self] index, direction in self?.selectTab(index == 0 ? .text : .todo, direction: direction) }
            HUDMotion.perform { bar.layer.frame.origin = CGPoint(x: 2, y: 4); content.addSublayer(bar.layer) }
            page.add(bar.controls)
            tabs = bar
            if !folded { page.tabs = bar }
            staggered.append(bar.layer)
            let top: CGFloat = 4 + 34 + 10
            showsNote = doc.tab == .text
            let height = showsNote ? buildNote(page, in: content, top: top, staggered: &staggered)
                                   : buildTasks(page, in: content, top: top, staggered: &staggered)
            contentHeight = top + height + 2
        }
        if !showsNote || context.recordingMode { noteArea?.suspend() }
        expandedBodyHeight = contentHeight
        HUDMotion.perform {
            clip.bounds = CGRect(x: 0, y: 0, width: HUDMetric.box.width, height: folded ? 6 : contentHeight)
            clip.position = CGPoint(x: HUDMetric.box.width / 2, y: HUDMetric.headerHeight)
            content.frame = CGRect(x: 0, y: 0, width: HUDMetric.box.width, height: contentHeight)
            content.opacity = folded ? 0 : 1
            content.setValue(folded ? -8 : 0, forKeyPath: "transform.translation.y")
        }
        body = clip; bodyContent = content
        let addBar = buildAddBar(page)
        HUDMotion.perform { addBar.frame.origin = CGPoint(x: 0, y: addBarTop(folded: folded)) }
        if !folded { staggered.forEach(page.stagger) }
        addBar.sublayers?.forEach(page.stagger)
        if context.recordingMode { HUDRedaction.playEntrance(in: [content], env: env) }
        if showsNote, isActive, !folded { attachNote() }
    }

    private func addBarTop(folded: Bool) -> CGFloat {
        HUDMetric.headerHeight + (folded ? 6 + 2 : expandedBodyHeight + 10)
    }

    private func foldButton(_ folded: Bool) -> HUDButton {
        let button = HUDButton(id: "notes.fold", title: folded ? L10n.text("Show", "显示") : L10n.text("Hide", "隐藏"),
                               icon: folded ? .eye : .eyeoff, kind: .secondary, size: .small, env: env)
        button.place(at: .zero)
        button.onActivate = { [weak self] in self?.setFolded(!folded) }
        return button
    }

    // MARK: TODO

    /// The TODO tab: task cards (250 px scroll) and the add form; returns its height.
    private func buildTasks(_ page: HUDDesignPage, in content: CALayer, top: CGFloat, staggered: inout [CALayer]) -> CGFloat {
        let doc = document
        let listHeight: CGFloat
        if doc.tasks.isEmpty {
            listHeight = 126
            let empty = HUDEmptyState.bilingual("NO TASKS", "待办为空", size: CGSize(width: 586, height: listHeight), env: env)
            HUDMotion.perform { empty.frame.origin = CGPoint(x: 2, y: top); content.addSublayer(empty) }
            staggered.append(empty)
        } else {
            let natural = CGFloat(doc.tasks.count) * HUDTaskCard.height + CGFloat(doc.tasks.count - 1) * HUDMetric.rowGap
            listHeight = min(250, natural)
            // 4 px of headroom keep the orange dot (−3, −3) unclipped.
            let list = page.scrollArea(id: "notes.tasks", frame: CGRect(x: 2, y: top - 4, width: 586, height: listHeight + 4), in: content)
            for (index, task) in doc.tasks.enumerated() {
                let card = HUDTaskCard(id: "task:" + task.id.uuidString, title: task.text, done: task.isChecked,
                                       attention: doc.showsDot(task), redacted: context.recordingMode, width: 580, env: env)
                card.onToggle = { [weak self] done in self?.setTask(task.id, done: done) }
                card.onDelete = { [weak self] in self?.deleteTask(task.id) }
                // The card's left edge: drag to reorder; a press opens the item menu (edit, orange dot,
                // move, delete), which is also how keyboard and VoiceOver reach those actions.
                let grip = HUDDragGrip(id: "task:" + task.id.uuidString + ":grip",
                                       label: L10n.text("Item options", "事项选项") + " " + (context.recordingMode ? "" : task.text),
                                       size: CGSize(width: 14, height: HUDTaskCard.height), dotsAt: CGPoint(x: 4, y: 25), env: env)
                grip.onActivate = { [weak self, weak grip] in self?.openTaskMenu(task.id, anchor: grip) }
                grip.onDrag = { [weak self, weak grip] phase, point in
                    guard let self, let grip else { return }
                    self.dragTask(task.id, phase: phase, at: self.root.convert(point, from: grip.layer))
                }
                HUDMotion.perform {
                    card.layer.frame.origin = CGPoint(x: 0, y: slotY(index))
                    grip.layer.frame.origin = .zero
                    card.layer.addSublayer(grip.layer)
                    list.content.addSublayer(card.layer)
                }
                page.add(card.controls); page.add(grip)
                staggered.append(card.layer)
                cards[task.id] = card
            }
            list.setContentHeight(4 + natural)
            page.restoreOffset(of: list)
            taskList = list
            if let editing = editingTask, let card = cards[editing.id] { placeTaskField(editing.field, on: card) }
        }
        let formTop = top + listHeight + 8
        let form = HUDLayer.plain("notes.addForm")
        let add = HUDButton(id: "notes.addTask", title: L10n.text("Add", "添加"), icon: .plus, kind: .primary, env: env)
        add.onActivate = { [weak self] in self?.submitTask() }
        let field = taskField(width: 586 - 8 - add.preferredWidth)
        HUDMotion.perform {
            form.frame = CGRect(x: 2, y: formTop, width: 586, height: 32)
            field.layer.frame.origin = .zero
            form.addSublayer(field.layer)
            content.addSublayer(form)
        }
        add.place(at: CGPoint(x: 586 - add.preferredWidth, y: 0))
        HUDMotion.perform { form.addSublayer(add.layer) }
        register(field)
        page.add(add)
        staggered.append(form)
        return listHeight + 8 + 32
    }

    /// The top of the card at `index` in the list content (4 px headroom).
    private func slotY(_ index: Int) -> CGFloat { 4 + CGFloat(index) * (HUDTaskCard.height + HUDMetric.rowGap) }

    /// The add-item field survives re-renders with its draft and focus.
    private func taskField(width: CGFloat) -> HUDDesignTextField {
        if let field = addField, field.frame.layer.bounds.width == width { return field }
        var options = HUDDesignTextField.Options()
        options.maximumLength = NotesCentreDocument.maximumTaskLength
        options.inset = CGSize(width: 12, height: 0)
        let field = HUDDesignTextField(id: "notes.newTask", label: L10n.text("Add item", "添加事项"), text: addField?.text ?? "",
                                       placeholder: L10n.text("+ Add item", "+ 添加事项"), size: CGSize(width: width, height: 32),
                                       options: options, env: env)
        field.onReturn = { [weak self] in self?.submitTask() }
        addField = field
        return field
    }

    /// The item menu (grip press, right-click): edit, orange dot, move, delete.
    private func openTaskMenu(_ id: UUID, anchor: HUDControl?, at point: CGPoint? = nil) {
        guard let index = document.tasks.firstIndex(where: { $0.id == id }), let card = cards[id] else { return }
        if isMenuOpen { closeMenu(); return }
        let count = document.tasks.count
        let items = [HUDMenu.Item(id: "edit", title: L10n.text("Edit", "编辑"), enabled: !context.recordingMode),
                     HUDMenu.Item(id: "dot", title: L10n.text("Orange dot", "橙色标记"), selected: document.flagged.contains(id)),
                     HUDMenu.Item(id: "up", title: L10n.text("Move up", "上移"), separatorBefore: true, enabled: index > 0),
                     HUDMenu.Item(id: "down", title: L10n.text("Move down", "下移"), enabled: index + 1 < count),
                     HUDMenu.Item(id: "delete", title: L10n.text("Delete", "删除"), danger: true, separatorBefore: true)]
        let anchorRect = point.map { CGRect(origin: $0, size: .zero) } ?? anchor?.rect(in: root) ?? card.rect(in: root)
        openMenu(id: "notes.task", items: items, anchorRect: anchorRect, anchor: anchor, minimumWidth: 168) { [weak self] item in
            guard let self else { return }
            switch item.id {
            case "edit": self.beginTaskEdit(id)
            case "dot": self.setDot(id, on: !self.document.flagged.contains(id))
            case "up": self.moveTask(id, by: -1)
            case "down": self.moveTask(id, by: 1)
            default: self.deleteTask(id)
            }
        }
    }

    private func setDot(_ id: UUID, on: Bool) {
        save { doc in if on { doc.flagged.insert(id) } else { doc.flagged.remove(id) } }
        refreshDots()
    }

    /// The orange dots in place: one per flagged open card, one on the TODO tab.
    private func refreshDots() {
        let doc = document
        for task in doc.tasks { cards[task.id]?.setAttention(doc.showsDot(task)) }
        tabs?.tabs[1].hasNews = doc.hasDot
    }

    private func moveTask(_ id: UUID, by step: Int) {
        guard let index = document.tasks.firstIndex(where: { $0.id == id }) else { return }
        let target = min(max(0, index + step), document.tasks.count - 1)
        guard target != index else { return }
        save { doc in doc.tasks.insert(doc.tasks.remove(at: index), at: target) }
        rerender()
        if let list = taskList { list.reveal(CGRect(x: 0, y: slotY(target), width: 580, height: HUDTaskCard.height)) }
    }

    // MARK: TODO: editing an item in place

    private func beginTaskEdit(_ id: UUID) {
        guard !context.recordingMode, let task = document.tasks.first(where: { $0.id == id }), let card = cards[id] else { return }
        editingTask?.field.endEditing(commit: true)
        var options = HUDDesignTextField.Options()
        options.light = true
        options.maximumLength = NotesCentreDocument.maximumTaskLength
        options.selectsAllOnBegin = true
        options.style = HUDTaskCard.titleStyle
        options.inset = CGSize(width: 8, height: 0)
        let width = 580 - HUDTaskCard.sideWidth - 18
        let field = HUDDesignTextField(id: "task:" + id.uuidString + ":edit", label: L10n.text("Edit item", "编辑事项"), text: task.text,
                                       size: CGSize(width: width, height: 24), options: options, env: env)
        field.onCommit = { [weak self] text in self?.finishTaskEdit(id, text: text) }
        field.onCancel = { [weak self] in self?.finishTaskEdit(id, text: nil) }
        editingTask = (id, field)
        placeTaskField(field, on: card)
        register(field)
        refreshRouter()
        field.beginEditing()
    }

    private func placeTaskField(_ field: HUDDesignTextField, on card: HUDTaskCard) {
        HUDMotion.perform { field.layer.frame.origin = CGPoint(x: 10, y: 11); card.layer.addSublayer(field.layer) }
        env.applyScale(to: field.layer)
    }

    /// A non-blank edit replaces the title; Esc (nil) or a blank one keeps it.
    private func finishTaskEdit(_ id: UUID, text: String?) {
        guard let editing = editingTask, editing.id == id else { return }
        editingTask = nil
        HUDMotion.perform { editing.field.layer.removeFromSuperlayer() }
        let value = (text ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
        if !value.isEmpty {
            save { doc in if let index = doc.tasks.firstIndex(where: { $0.id == id }) { doc.tasks[index].text = String(value.prefix(NotesCentreDocument.maximumTaskLength)) } }
        }
        if isActive { rerender() }
    }

    // MARK: TODO: reordering by the grip

    /// Lift: the card rises over the others. Drag: it follows the pointer and
    /// the others glide to their new slots. Drop: it settles into its slot.
    private func dragTask(_ id: UUID, phase: HUDDragGrip.Phase, at point: CGPoint) {
        guard let list = taskList else { return }
        let local = list.content.convert(point, from: root)
        switch phase {
        case .began:
            guard let card = cards[id], let index = document.tasks.firstIndex(where: { $0.id == id }) else { return }
            closePopups(); editingTask?.field.endEditing(commit: true)
            reorder = Reorder(id: id, grab: local.y - slotY(index), index: index, order: document.tasks.map(\.id))
            HUDMotion.perform { card.layer.zPosition = 10 }
            HUDMotion.animate(card.layer, "transform.scale", to: 1.015, duration: HUDDuration.hover, env: env)
            context.onLockParallax?()
        case .moved:
            guard var state = reorder, state.id == id, let card = cards[id] else { return }
            // Past the list's edges the list scrolls under the card.
            let visible = list.layer.bounds.height, edge = list.layer.convert(point, from: root).y
            if edge < 0 { list.setOffset(max(0, list.offset + edge * 0.5)); list.noteScroll() }
            else if edge > visible { list.setOffset(min(list.maximumOffset, list.offset + (edge - visible) * 0.5)); list.noteScroll() }
            let y = min(max(slotY(0), local.y - state.grab), slotY(state.order.count - 1))
            HUDMotion.perform { card.layer.position.y = y + card.layer.bounds.height / 2 }
            let target = min(max(0, Int(((y - 4) / (HUDTaskCard.height + HUDMetric.rowGap)).rounded())), state.order.count - 1)
            guard target != state.index else { return }
            state.order.remove(at: state.index); state.order.insert(id, at: target); state.index = target
            reorder = state
            for (slot, other) in state.order.enumerated() where other != id {
                guard let layer = cards[other]?.layer else { continue }
                HUDMotion.animate(layer, "position.y", to: slotY(slot) + layer.bounds.height / 2, duration: 0.18, env: env)
            }
        case .ended:
            guard let state = reorder, state.id == id else { return }
            reorder = nil
            if let card = cards[id] {
                HUDMotion.animate(card.layer, "position.y", to: slotY(state.index) + card.layer.bounds.height / 2, duration: 0.18, env: env)
                HUDMotion.animate(card.layer, "transform.scale", to: 1, duration: 0.18, env: env)
                HUDMotion.perform { card.layer.zPosition = 0 }
            }
            guard state.order != document.tasks.map(\.id) else { return }
            save { doc in
                let byID = Dictionary(uniqueKeysWithValues: doc.tasks.map { ($0.id, $0) })
                let moved = state.order.compactMap { byID[$0] }
                if moved.count == doc.tasks.count { doc.tasks = moved }
            }
            // The page order (VoiceOver) follows once the card has landed.
            DispatchQueue.main.asyncAfter(deadline: .now() + (env.reducesMotion ? 0 : 0.2)) { [weak self] in
                guard let self, self.isActive, self.reorder == nil, self.document.tab == .todo else { return }
                self.rerender()
            }
        }
    }

    /// A lifted card returns to its slot (the module was left mid-drag).
    private func cancelReorder() {
        guard reorder != nil else { return }
        reorder = nil
        let order = document.tasks.map(\.id)
        for (slot, id) in order.enumerated() {
            guard let layer = cards[id]?.layer else { continue }
            HUDMotion.perform {
                layer.position.y = slotY(slot) + layer.bounds.height / 2
                layer.zPosition = 0; layer.transform = CATransform3DIdentity
            }
        }
    }

    // MARK: TEXT

    /// The TEXT tab: the note box (format bar, then the 200 px rich text area); returns its height.
    private func buildNote(_ page: HUDDesignPage, in content: CALayer, top: CGFloat, staggered: inout [CALayer]) -> CGFloat {
        let doc = document
        let box = HUDLayer.plain("notes.note")
        let bar = NotesFormatBar(id: "notes.format", state: noteFormatState, canvas: self, env: env)
        bar.onFormat = { [weak self] change in self?.applyFormat(change) }
        formatBar = bar
        let rule = HUDLayer.plain("notes.note.rule")
        HUDMotion.perform {
            box.frame = CGRect(x: 2, y: top, width: 586, height: 43 + 200)
            box.backgroundColor = HUDColor.rgb(0x1C1C1C)
            box.borderWidth = 1; box.borderColor = HUDColor.ringField
            bar.layer.frame.origin = CGPoint(x: 8, y: 8)
            rule.frame = CGRect(x: 0, y: 42, width: 586, height: 1); rule.backgroundColor = HUDColor.separator
            [bar.layer, rule].forEach(box.addSublayer)
            content.addSublayer(box)
        }
        page.add(bar.controls)
        if context.recordingMode {
            let block = HUDRedaction.block(doc.text, size: CGSize(width: 586, height: 200), env: env)
            HUDMotion.perform { block.frame.origin = CGPoint(x: 0, y: 43); box.addSublayer(block) }
        } else {
            let area = noteEditor()
            area.formatBar = bar
            area.setSize(CGSize(width: 586, height: 200))
            HUDMotion.perform { area.layer.frame.origin = CGPoint(x: 0, y: 43); box.addSublayer(area.layer) }
            page.add(area)
        }
        staggered.append(box)
        return 43 + 200
    }

    /// The TEXT editor, created once with the stored text and runs.
    private func noteEditor() -> ArchiveBodyArea {
        if let noteArea { return noteArea }
        let doc = document
        let area = ArchiveBodyArea(id: "notes.text", label: L10n.text("Edit text note", "编辑文字便笺"),
                                   placeholder: L10n.text("Start writing", "开始书写"), env: env)
        HUDMotion.perform { area.layer.borderWidth = 0; area.layer.backgroundColor = nil }
        area.canvas = self
        let base = doc.style.textStyle
        let view = area.textView
        view.textStorage?.setAttributedString(ArchiveRichText.attributed(doc.text, rich: doc.richText, base: base))
        let length = view.textStorage?.length ?? 0
        view.typingAttributes = length > 0 ? view.textStorage?.attributes(at: length - 1, effectiveRange: nil) ?? ArchiveRichText.attributes(base)
            : ArchiveRichText.attributes(base)
        view.setSelectedRange(NSRange(location: length, length: 0))
        view.delegate = self
        area.textChanged()
        area.onFocusChange = { [weak self] focused in
            guard let self else { return }
            if focused { self.context.onLockParallax?() } else { self.saveNote() }
            self.formatBar?.show(self.noteFormatState)
            self.context.onInputStateChange?()
        }
        noteArea = area
        return area
    }

    /// Projects the editor while the TEXT tab is on screen and interactive.
    private func attachNote() {
        guard isActive, let area = noteArea, document.tab == .text, !document.isFolded, !context.recordingMode,
              area.layer.superlayer != nil else { return }
        area.attach()
    }

    /// What the bar shows: the selection or typing style while editing, else the first character's.
    private var noteFormatState: NotesFormatState {
        let style = noteArea.map { ArchiveRichText.displayedStyle(in: $0.textView, focused: $0.isFocused) }
            ?? document.richText?.runs.first?.style ?? document.style.textStyle
        return NotesFormatState(family: ArchiveRichText.family(of: style), size: style.fontSize, bold: style.bold, italic: style.italic,
                                underline: style.underline, strikethrough: style.strikethrough, color: ArchiveRichText.hex(style.color))
    }

    /// Formatting applies to the selection while editing (the typing style at
    /// a caret), otherwise to the whole note.
    private func applyFormat(_ change: NotesFormatChange) {
        guard let area = noteArea, !context.recordingMode else { return }
        let view = area.textView
        let range = area.isFocused ? view.selectedRange() : NSRange(location: 0, length: view.textStorage?.length ?? 0)
        ArchiveRichText.apply(change, to: view, range: range, keepSelection: area.isFocused)
        noteEdited = true
        saveNote()
        formatBar?.show(noteFormatState)
    }

    /// Writes the note when it changed (focus loss, a format change, leaving).
    private func saveNote() {
        guard noteEdited, let view = noteArea?.textView, !view.hasMarkedText(), let storage = view.textStorage else { return }
        noteEdited = false
        let text = view.string, rich = ArchiveRichText.capture(storage)
        save { doc in doc.text = text; doc.richText = text.isEmpty ? nil : rich }
    }

    func textDidChange(_ notification: Notification) {
        guard let view = notification.object as? NSTextView, view === noteArea?.textView else { return }
        noteEdited = true
        noteArea?.textChanged()
    }

    func textViewDidChangeSelection(_ notification: Notification) {
        guard let view = notification.object as? NSTextView, view === noteArea?.textView else { return }
        formatBar?.show(noteFormatState)
    }

    func textDidEndEditing(_ notification: Notification) {
        guard let view = notification.object as? NSTextView, view === noteArea?.textView else { return }
        saveNote()
    }

    func textView(_ textView: NSTextView, doCommandBy selector: Selector) -> Bool {
        guard textView === noteArea?.textView, !textView.hasMarkedText() else { return false }
        if selector == #selector(NSResponder.cancelOperation(_:)) { noteArea?.blur(); return true }
        return false
    }

    // MARK: Text input routing (the TEXT editor is not a HUDDesignTextField)

    override var isEditingText: Bool { super.isEditingText || noteArea?.isFocused == true }

    override func endEditing(commit: Bool) {
        super.endEditing(commit: commit)
        noteArea?.blur()
    }

    override func textView(atHost point: CGPoint) -> NSView? {
        if let view = super.textView(atHost: point) { return view }
        guard isActive, !isTransitioning, let area = noteArea, area.layer.isVisible(in: root) else { return nil }
        if let target = area.pressTarget(host: point) { return target }
        return capturesPointer ? nil : area.hitTest(host: point)
    }

    override func refreshTextFieldProjection() {
        super.refreshTextFieldProjection()
        noteArea?.refreshProjection()
    }

    @discardableResult
    override func pointerDown(at point: CGPoint, event: NSEvent? = nil) -> Bool {
        guard isActive, !isTransitioning else { return super.pointerDown(at: point, event: event) }
        // The note keeps focus while the format bar edits its selection.
        if let area = noteArea, area.isFocused, !isMenuOpen, !isPopoverOpen,
           !(formatBar?.formatsSelection(at: point, in: root) ?? false) { area.blur() }
        // A double-click on a task card edits its title in place.
        if event?.clickCount == 2, !isMenuOpen, !isPopoverOpen, let id = taskID(at: point),
           let card = cards[id], !card.action.rect(in: root).contains(point), !card.delete.rect(in: root).contains(point) {
            beginTaskEdit(id); return true
        }
        return super.pointerDown(at: point, event: event)
    }

    override func handleRightPointerDown(at point: CGPoint) -> Bool {
        guard let id = taskID(at: point) else { return false }
        openTaskMenu(id, anchor: nil, at: point)
        return true
    }

    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        guard let area = noteArea, area.layer.isVisible(in: root), area.rect(in: root).contains(point) else { return false }
        area.scroll(by: delta.y)
        return true
    }

    /// The task card under a box point, when the list shows it there.
    private func taskID(at point: CGPoint) -> UUID? {
        guard document.tab == .todo, let list = taskList, list.layer.isVisible(in: root),
              list.layer.convert(point, from: root).y >= 0, list.layer.bounds.contains(list.layer.convert(point, from: root)) else { return nil }
        return cards.first { $0.value.rect(in: root).contains(point) }?.key
    }

    // MARK: Add bar

    /// Text / TODO / Media / Drawing: chamfered light buttons that create desktop notes.
    private func buildAddBar(_ page: HUDDesignPage) -> CALayer {
        let bar = HUDLayer.plain("notes.addBar")
        let width = (HUDMetric.box.width - 3 * 8) / 4
        let kinds: [(String, String, HUDIcon)] = [("text", L10n.text("Text", "文字"), .text), ("todo", L10n.text("TODO", "待办"), .todo),
                                                 ("image", L10n.text("Media", "媒体"), .image), ("drawing", L10n.text("Drawing", "画画"), .pen)]
        HUDMotion.perform { bar.frame = CGRect(x: 0, y: 0, width: HUDMetric.box.width, height: 40); page.layer.addSublayer(bar) }
        for (index, kind) in kinds.enumerated() {
            let button = NotesAddButton(id: "notes.new." + kind.0, title: kind.1, icon: kind.2, size: CGSize(width: width, height: 40), env: env)
            button.isEnabled = storageError == nil
            button.onActivate = { [weak self] in self?.workspace.perform(actionID: "tool:" + kind.0) }
            HUDMotion.perform { button.layer.frame.origin = CGPoint(x: CGFloat(index) * (width + 8), y: 0); bar.addSublayer(button.layer) }
            page.add(button)
            if kind.0 == "image" { mediaButton = button }
        }
        addBar = bar
        return bar
    }

    // MARK: Actions

    private func save(_ change: (inout NotesCentreDocument) -> Void) {
        do { try centre.update(change) }
        catch { toast(icon: .warn, "Notice", "提示", detail: L10n.text("Could not save: ", "无法保存：") + error.localizedDescription) }
    }

    private func selectTab(_ tab: NotesCentreDocument.Tab, direction: CGFloat) {
        guard tab != document.tab else { return }
        saveNote()
        save { $0.tab = tab }
        navigate(dx: direction)
    }

    /// Hide/Show (spec §3.6): the area folds to nothing over .32 s (fading in
    /// .24 s, rising 8 px) and the add bar slides up to 2 px under the header.
    private func setFolded(_ folded: Bool) {
        endEditing(commit: true); closePopups()
        saveNote()
        save { $0.isFolded = folded }
        header.configure(crumbs: ["NOTES"], code: "MOD-01", title: HUDModuleTitle("Notes", "便笺"), counter: counterText,
                         showsBack: false, actions: [foldButton(folded)])
        page.tabs = folded ? nil : tabs
        if folded { noteArea?.suspend() } else { attachNote() }
        if let body {
            HUDMotion.animate(body, "bounds.size.height", to: folded ? 6 : expandedBodyHeight, duration: 0.32, env: env)
        }
        if let content = bodyContent {
            HUDMotion.animate(content, "opacity", to: Float(folded ? 0 : 1), duration: 0.24, timing: HUDEasing.ease, env: env)
            HUDMotion.animate(content, "transform.translation.y", to: folded ? -8 : 0, duration: 0.32, env: env)
        }
        if let addBar {
            HUDMotion.animate(addBar, "position.y", to: addBarTop(folded: folded) + addBar.bounds.height / 2, duration: 0.32, env: env)
        }
        refreshRouter()
        context.onInputStateChange?()
    }

    private func setTask(_ id: UUID, done: Bool) {
        save { doc in if let index = doc.tasks.firstIndex(where: { $0.id == id }) { doc.tasks[index].isChecked = done } }
        header.setCounter(counterText)
        refreshDots()
        context.onInputStateChange?()
    }

    private func deleteTask(_ id: UUID) {
        save { doc in doc.tasks.removeAll { $0.id == id }; doc.flagged.remove(id) }
        rerender()
    }

    /// Enter or Add: a non-blank item joins the list, which scrolls to it, and
    /// the field takes focus again for the next one.
    private func submitTask() {
        guard let field = addField else { return }
        if field.isEditing { field.endEditing(commit: true) }
        let text = field.text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        save { $0.tasks.append(NoteChecklistItem(text: String(text.prefix(NotesCentreDocument.maximumTaskLength)))) }
        field.setText("")
        rerender()
        if let list = taskList { list.setOffset(list.maximumOffset) }
        DispatchQueue.main.async { [weak self, weak field] in
            guard let self, self.isActive, let field, field === self.addField else { return }
            field.beginEditing()
        }
    }

    // MARK: Media and status

    /// Media: Choose in Finder or from the File Shelf, from a menu on the button.
    private func presentMediaSources(at point: CGPoint) {
        guard isActive, let anchor = mediaButton else { return }
        let items = [HUDMenu.Item(id: "finder", title: L10n.text("Choose in Finder", "从 Finder 选择")),
                     HUDMenu.Item(id: "shelf", title: L10n.text("Choose from Shelf", "从暂存架选择"))]
        openMenu(id: "notes.media", items: items, anchor: anchor) { [weak self] item in
            if item.id == "finder" { self?.onChooseFiles?(point) } else { self?.onChooseShelf?(point) }
        }
    }

    /// The File Shelf's items as a menu (long shelves get the scroll bar);
    /// folders, other kinds and missing files are listed but disabled.
    func presentShelfMedia(choices: [NotesShelfMediaChoice], onSelect: @escaping (UUID) -> Void) {
        guard isActive, let anchor = mediaButton else { return }
        guard !choices.isEmpty else { toast(icon: .folder, "Shelf empty", "暂存架为空"); return }
        let items = choices.map { HUDMenu.Item(id: $0.id.uuidString, title: $0.title, enabled: $0.isSupported && $0.isAvailable) }
        openMenu(id: "notes.shelf", items: items, anchor: anchor, minimumWidth: 260) { item in
            if let id = UUID(uuidString: item.id) { onSelect(id) }
        }
    }

    private func workspaceDidAct(_ event: NotesCanvasEvent) {
        guard isActive else { return }
        switch event {
        case .createdText: toast(icon: .text, "New text note", "已在桌面创建便笺")
        case .createdTODO: toast(icon: .todo, "New TODO note", "已在桌面创建便笺")
        case .createdMedia: toast(icon: .image, "New media note", "已在桌面创建便笺")
        case .createdDrawing: toast(icon: .pen, "New drawing note", "已在桌面创建便笺")
        default: break
        }
    }

    private func reportStatus() {
        let status = workspace.statusMessage
        defer { reportedStatus = status }
        guard isActive, let status, status != reportedStatus else { return }
        toast(icon: .warn, "Notice", "提示", detail: status)
    }

    // MARK: Verification

    var taskCardsForTesting: [UUID: HUDTaskCard] { cards }
    var addFieldForTesting: HUDDesignTextField? { addField }
    var noteAreaForTesting: ArchiveBodyArea? { noteArea }
    var formatBarForTesting: NotesFormatBar? { formatBar }
    var editingTaskForTesting: HUDDesignTextField? { editingTask?.field }
    func tabForTesting(_ index: Int) -> HUDTabs.Tab? { tabs?.tabs[index] }
    func submitTaskForTesting(_ text: String) { addField?.setText(text); submitTask() }
    func applyFormatForTesting(_ change: NotesFormatChange) { applyFormat(change) }
    func openTaskMenuForTesting(_ id: UUID) { openTaskMenu(id, anchor: nil) }
    func dragTaskForTesting(_ id: UUID, toSlot slot: Int) {
        guard let card = cards[id] else { return }
        let start = root.convert(CGPoint(x: 7, y: 30), from: card.layer)
        dragTask(id, phase: .began, at: start)
        let distance = CGFloat(slot - (document.tasks.firstIndex { $0.id == id } ?? 0)) * (HUDTaskCard.height + HUDMetric.rowGap)
        dragTask(id, phase: .moved, at: CGPoint(x: start.x, y: start.y + distance))
        dragTask(id, phase: .ended, at: CGPoint(x: start.x, y: start.y + distance))
    }
}

/// An add-bar button (spec §3.6 `.addbar button`): 40 px, the light gradient
/// with an 8 px chamfer, a 16 px icon and a 500 13 px label; hover lifts it
/// 2 px and brightens it.
final class NotesAddButton: HUDControl {
    private let brighten = HUDLayer.shape("add.brighten")

    init(id: String, title: String, icon: HUDIcon, size: CGSize, env: HUDDesignEnvironment) {
        super.init(id: id, label: title, env: env)
        let bounds = CGRect(origin: .zero, size: size), shape = HUDShape.chamfer(bounds, cut: 8)
        let plate = HUDLayer.plain("add.plate")
        let glyph = HUDIconLayer(icon, size: 16, color: HUDColor.nearBlack)
        let text = HUDTextLayer(title, style: HUDTextStyle(size: 13, weight: .medium, color: HUDColor.nearBlack))
        HUDMotion.perform {
            layer.bounds = bounds
            plate.frame = bounds
            plate.showArtwork(HUDArtwork.plate(HUDColor.lightButton, size: size, cut: 8, scale: env.renderScale), scale: env.renderScale)
            brighten.frame = bounds; brighten.path = shape; brighten.fillColor = HUDColor.white(0.07); brighten.opacity = 0
            let textWidth = ceil(text.preferredWidth), start = (size.width - 16 - 8 - textWidth) / 2
            glyph.frame = CGRect(x: start, y: (size.height - 16) / 2, width: 16, height: 16)
            text.lineFrame = CGRect(x: start + 24, y: (size.height - 13) / 2, width: textWidth + 1, height: 13)
            [plate, brighten, glyph, text].forEach(layer.addSublayer)
        }
        setTarget(bounds, path: shape)
        env.applyScale(to: layer)
    }

    override func hoverChanged() { applyHoverMotion(lift: 2, pressScale: 1, brighten: brighten) }
    override func enabledChanged() { HUDMotion.perform { layer.opacity = isEnabled ? 1 : 0.35 } }
}
