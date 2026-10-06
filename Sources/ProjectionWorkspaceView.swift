import AppKit
import QuartzCore

/// A full-screen, event-driven drawing plane. Strokes and media references live
/// in the session model; decoding and playback exist only while this view shows.
final class ProjectionWorkspaceView: NSView, HUDControlFeedbackHost {
    let model: ProjectionModel
    var onClose: (() -> Void)?
    var onEvent: ((ProjectionEvent) -> Void)?
    var onBackgroundChange: (() -> Void)?
    var shelfChoices: (() -> [NotesShelfMediaChoice])?
    var shelfAccess: ((UUID) throws -> ShelfFileAccess)?
    private(set) var active = false
    private(set) var secondaryMenu: NotesRetainedMenu?
    private(set) var toolbar = ProjectionToolbar()
    private let background = CALayer(), dots = CAShapeLayer(), strokes = HUDDecorativeContentLayer()
    private let mediaLayer = HUDDecorativeContentLayer(), cursor = CAShapeLayer(), liveStroke = CAShapeLayer()
    private let message = CATextLayer()
    private var nodes: [UUID: ProjectionMediaNode] = [:]
    private var tracking: NSTrackingArea?
    private var pendingStroke: NotesDrawingStroke?
    private var renderedStrokes: [NotesDrawingStroke] = []
    private var renderedStrokeSize = CGSize.zero
    private var strokeLayers: [CAShapeLayer] = []
    private var lastEraserPoint: CGPoint?
    private var eraseChanged = false
    private var gesture: MediaGesture?
    private var pointer: CGPoint?
    private var reduceMotion: Bool
    private var importGeneration = 0
    private var importing = false
    private var importTicket: ProjectionImportCancellation?
    private(set) var completedImportCountForVerification = 0
    private let importQueue = DispatchQueue(label: "EndfieldHUD.Projection.Import", qos: .utility)
    private var openPanel: NSOpenPanel?
    private var retiringMenu: CALayer?
    private var toolbarTopInset: CGFloat = 64
    private var dotSize = CGSize.zero
    private var lastBackgroundEnabled: Bool?
    private var pendingMenuEvent: ProjectionEvent?
    private var pendingBrushChange = false
    private enum MediaGesture { case move(UUID, CGPoint, CGRect), resize(UUID, CGPoint, CGRect), seek(UUID) }
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    init(model: ProjectionModel, frame: CGRect, reduceMotion: Bool) {
        self.model = model; self.reduceMotion = reduceMotion
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
        for item in [background, dots, mediaLayer, strokes, liveStroke, cursor, message, toolbar.artwork] {
            item.actions = ["position": NSNull(), "bounds": NSNull(), "contents": NSNull(), "opacity": NSNull(), "path": NSNull()]
            layer?.addSublayer(item)
        }
        background.name = "projection.background"; dots.name = "projection.dots"
        strokes.name = "projection.strokes"; mediaLayer.name = "projection.media"
        cursor.fillColor = nil; cursor.lineWidth = 1; cursor.zPosition = 500_000
        liveStroke.fillColor = nil; liveStroke.lineCap = .round; liveStroke.lineJoin = .round
        message.font = NSFont.systemFont(ofSize: 12, weight: .semibold); message.fontSize = 12
        message.foregroundColor = NSColor.white.cgColor; message.alignmentMode = .center; message.contentsScale = 2
        message.zPosition = 2_000_001
        toolbar.onAction = { [weak self] in self?.perform($0) }
        toolbar.onCancel = { [weak self] in self?.onClose?() }
        addSubview(toolbar)
        registerForDraggedTypes([.fileURL])
        setAccessibilityRole(.group); setAccessibilityLabel(L10n.text("Projection", "投影"))
        layoutWorkspace()
    }
    required init?(coder: NSCoder) { nil }
    deinit { importTicket?.cancel(); nodes.values.forEach { $0.presentation.dispose() } }

    func setActive(_ value: Bool) {
        guard active != value else { return }
        active = value
        if value { rebuildStrokes(); syncMedia(); window?.makeFirstResponder(self) }
        else {
            commitBrushChange()
            importGeneration += 1; importing = false
            importTicket?.cancel(); importTicket = nil
            openPanel?.cancel(nil); openPanel = nil
            pendingStroke = nil; gesture = nil; liveStroke.path = nil; cursor.path = nil
            closeMenu(animated: false)
            nodes.values.forEach { $0.presentation.setVisible(false) }
            removeAnimations(layer)
        }
    }
    func update(configuration: AppConfiguration) {
        reduceMotion = configuration.reduceMotion
        if reduceMotion { removeAnimations(layer) }
    }
    func dispose() {
        setActive(false); nodes.values.forEach { $0.presentation.dispose() }
        nodes.removeAll(); mediaLayer.sublayers?.forEach { $0.removeFromSuperlayer() }
        unregisterDraggedTypes()
    }
    func updateScreenGeometry(_ screen: NSScreen) {
        let safe: CGFloat
        if #available(macOS 12.0, *) { safe = screen.safeAreaInsets.top } else { safe = 0 }
        toolbarTopInset = ProjectionToolbar.topInset(safeAreaTop: safe, visibleTop: screen.frame.maxY - screen.visibleFrame.maxY)
        layoutWorkspace()
    }
    override func layout() { super.layout(); layoutWorkspace() }
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking { removeTrackingArea(tracking) }
        let next = NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeAlways, .inVisibleRect], owner: self)
        addTrackingArea(next); tracking = next
    }
    private func layoutWorkspace() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        background.frame = bounds; dots.frame = bounds; strokes.frame = bounds; mediaLayer.frame = bounds
        toolbar.frame.origin = CGPoint(x: max(6, (bounds.width - toolbar.contentSize.width) / 2), y: min(toolbarTopInset, max(6, bounds.height - toolbar.contentSize.height - 8)))
        toolbar.artwork.position = toolbar.frame.origin; toolbar.layoutAccessibility()
        message.frame = CGRect(x: max(10, bounds.midX - 280), y: toolbar.frame.maxY + 10, width: min(560, bounds.width - 20), height: 30)
        for item in model.media { model.setFrame(item.frame, id: item.id, in: bounds) }
        refreshBackground(); rebuildStrokes(); syncMedia(); positionMenu()
        CATransaction.commit()
    }
    func refreshBackground() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        background.backgroundColor = NSColor.black.withAlphaComponent(model.darkness).cgColor
        let changed = lastBackgroundEnabled != nil && lastBackgroundEnabled != model.backgroundEnabled
        lastBackgroundEnabled = model.backgroundEnabled
        background.isHidden = false; dots.isHidden = false
        background.opacity = model.backgroundEnabled ? 1 : 0; dots.opacity = background.opacity
        if changed, active, !reduceMotion {
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = model.backgroundEnabled ? 0 : 1
            fade.toValue = background.opacity; fade.duration = 0.18
            background.add(fade, forKey: "projection.background"); dots.add(fade, forKey: "projection.background")
        }
        if dotSize != bounds.size {
            dotSize = bounds.size; let path = CGMutablePath()
            for y in stride(from: CGFloat(12), through: bounds.height, by: 36) {
                for x in stride(from: CGFloat(12), through: bounds.width, by: 36) {
                    path.addRect(CGRect(x: x, y: y, width: 2, height: 2))
                }
            }
            dots.path = path
        }
        dots.fillColor = NSColor.white.withAlphaComponent(0.14).cgColor
        toolbar.update(model)
        CATransaction.commit()
        onBackgroundChange?()
    }
    private func rebuildStrokes() {
        let next = model.drawing.strokes
        if renderedStrokeSize == bounds.size, renderedStrokes == next { return }
        if renderedStrokeSize == bounds.size, next.count <= renderedStrokes.count {
            // Erasing only removes strokes. Keep every unchanged path/layer;
            // rebuilding a whole whiteboard for each eraser event is costly.
            var remaining: [CAShapeLayer] = [], index = 0
            for (old, layer) in zip(renderedStrokes, strokeLayers) {
                if index < next.count, old == next[index] { remaining.append(layer); index += 1 }
                else { layer.removeFromSuperlayer() }
            }
            if index == next.count { strokeLayers = remaining; renderedStrokes = next; return }
        }
        strokes.sublayers?.forEach { $0.removeFromSuperlayer() }
        strokeLayers = next.map(strokeLayer)
        strokeLayers.forEach { strokes.addSublayer($0) }
        renderedStrokes = next; renderedStrokeSize = bounds.size
    }
    private func strokeLayer(_ stroke: NotesDrawingStroke) -> CAShapeLayer {
        let result = CAShapeLayer(); result.path = NotesDrawing.path(stroke, size: bounds.size)
        result.strokeColor = stroke.color.color.cgColor; result.lineWidth = stroke.width
        result.fillColor = nil; result.lineCap = .round; result.lineJoin = .round
        return result
    }
    private func syncMedia() {
        let ids = Set(model.media.map(\.id))
        for id in Set(nodes.keys).subtracting(ids) { nodes.removeValue(forKey: id)?.dispose() }
        guard active else { return }
        for (index, item) in model.media.enumerated() {
            let node: ProjectionMediaNode
            if let existing = nodes[item.id] { node = existing }
            else {
                node = ProjectionMediaNode(item: item, host: self)
                node.onAction = { [weak self] action in
                    guard let self, self.active else { return }
                    if action == "remove" { self.model.removeMedia(item.id); self.syncMedia(); self.onEvent?(.mediaRemoved) }
                    else { self.nodes[item.id]?.presentation.togglePlayback() }
                }
                node.presentation.onStateChange = { [weak self, weak node] in
                    guard let self, self.active, let node else { return }; node.refreshProgress()
                }
                node.presentation.onProgress = { [weak node] in node?.refreshProgress() }
                nodes[item.id] = node; mediaLayer.addSublayer(node.root)
            }
            node.root.zPosition = CGFloat(index); node.layout(item.frame)
            node.presentation.setVisible(bounds.intersects(item.frame))
        }
    }
    func perform(_ action: String) {
        guard active else { return }
        window?.makeFirstResponder(self)
        switch action {
        case "close": onClose?()
        case "clear":
            let confirmation = ProjectionClearConfirmation()
            confirmation.onConfirm = { [weak self, weak confirmation] in
                guard let self, self.active, let confirmation, self.secondaryMenu === confirmation else { return }
                self.clearContent()
            }
            showMenu(confirmation)
        case "color":
            let chooser = NotesFormattingControls(kind: "color", dark: true, style: NotesTextStyle(color: NotesRGBA(model.color)))
            chooser.onChange = { [weak self, weak chooser] change in
                guard let self, self.active, let chooser, self.secondaryMenu === chooser, case .color(let color) = change else { return }
                self.model.color = color; self.pendingMenuEvent = .brushChanged
                self.toolbar.update(self.model); self.updateCursor()
            }
            showMenu(chooser)
        case "brush":
            let picker = ProjectionAdjustmentMenu(values: [.init(id: "brush", title: L10n.text("Brush thickness", "画笔粗细"), value: model.brushWidth, range: 1...80)])
            picker.onValue = { [weak self, weak picker] _, value in
                guard let self, self.active, let picker, self.secondaryMenu === picker else { return }; self.model.setBrushWidth(value); self.pendingMenuEvent = .brushChanged
                self.toolbar.update(self.model); self.updateCursor()
            }; showMenu(picker)
        case "eraser": model.erasing.toggle(); toolbar.update(model); updateCursor(); onEvent?(.brushChanged)
        case "background": model.backgroundEnabled.toggle(); refreshBackground(); onEvent?(.backgroundChanged)
        case "appearance":
            let picker = ProjectionAdjustmentMenu(values: [
                .init(id: "darkness", title: L10n.text("Darkness", "背景深度"), value: model.darkness * 100, range: 0...100),
                .init(id: "blur", title: L10n.text("Blur", "模糊"), value: model.blur * 100, range: 0...100)])
            picker.onValue = { [weak self, weak picker] id, value in
                guard let self, self.active, let picker, self.secondaryMenu === picker else { return }
                if id == "darkness" { self.model.setDarkness(value / 100) } else { self.model.setBlur(value / 100) }
                self.pendingMenuEvent = .backgroundChanged
                self.refreshBackground()
            }; showMenu(picker)
        case "media":
            let chooser = NotesMediaSourceChooser(dark: true)
            chooser.onChoose = { [weak self, weak chooser] shelf in
                guard let self, self.active, let chooser, self.secondaryMenu === chooser else { return }; self.closeMenu()
                if shelf { self.chooseShelf() } else { self.chooseFinder() }
            }; showMenu(chooser)
        default: break
        }
    }
    private func clearContent() {
        let hadPendingWork = importing || pendingStroke != nil
        importGeneration += 1; importing = false
        importTicket?.cancel(); importTicket = nil
        let panel = openPanel; openPanel = nil; panel?.cancel(nil)
        pendingStroke = nil; gesture = nil; eraseChanged = false; lastEraserPoint = nil
        liveStroke.path = nil; message.string = ""
        let changed = model.clearContent()
        rebuildStrokes(); syncMedia(); closeMenu(); updateCursor()
        if changed || hadPendingWork { onEvent?(.cleared) }
    }
    private func showMenu(_ next: NotesRetainedMenu) {
        if secondaryMenu != nil { closeMenu() }
        secondaryMenu = next
        next.onCancel = { [weak self, weak next] in
            guard let self, let next, self.secondaryMenu === next else { return }; self.closeMenu()
        }
        addSubview(next); layer?.addSublayer(next.artwork); next.artwork.zPosition = 3_000_000
        positionMenu(); animate(next.artwork, opening: true)
        window?.makeFirstResponder(next); updateCursor()
    }
    private func positionMenu() {
        guard let menu = secondaryMenu else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        menu.frame.origin = CGPoint(x: min(bounds.maxX - menu.contentSize.width - 8, max(8, toolbar.frame.midX - menu.contentSize.width / 2)), y: toolbar.frame.maxY + 12)
        menu.artwork.position = menu.frame.origin; menu.layoutAccessibility()
        CATransaction.commit()
    }
    func closeMenu(animated: Bool = true) {
        retiringMenu?.removeAllAnimations(); retiringMenu?.removeFromSuperlayer(); retiringMenu = nil
        guard let previous = secondaryMenu else { return }
        secondaryMenu = nil
        if let event = pendingMenuEvent { pendingMenuEvent = nil; onEvent?(event) }
        if animated, active, !reduceMotion {
            previous.detachInputKeepingArtwork(); retiringMenu = previous.artwork
            animate(previous.artwork, opening: false)
            CATransaction.begin(); CATransaction.setCompletionBlock { [weak self, weak artwork = previous.artwork] in
                guard let self, let artwork, self.retiringMenu === artwork else { return }
                artwork.removeFromSuperlayer(); self.retiringMenu = nil
            }
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 1; fade.toValue = 0; fade.duration = 0.14
            previous.artwork.opacity = 0; previous.artwork.add(fade, forKey: "projection.menu.close")
            CATransaction.commit()
        } else { previous.removeFromSuperview() }
        window?.makeFirstResponder(self); updateCursor()
    }
    private func animate(_ target: CALayer, opening: Bool) {
        guard !reduceMotion else { return }
        let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = opening ? 0 : 1; fade.toValue = opening ? 1 : 0
        let slide = CABasicAnimation(keyPath: "transform.translation.y"); slide.fromValue = opening ? -5 : 0; slide.toValue = opening ? 0 : -5
        let group = CAAnimationGroup(); group.animations = [fade, slide]; group.duration = 0.16
        group.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut); target.add(group, forKey: "projection.menu")
    }
    private func chooseFinder() {
        guard let window, !importing else { return }
        let panel = NSOpenPanel(); panel.canChooseDirectories = false; panel.allowsMultipleSelection = true
        panel.allowedFileTypes = NotesMediaFactory.supportedFileExtensions; panel.allowsOtherFileTypes = false
        openPanel = panel
        panel.beginSheetModal(for: window) { [weak self, weak panel] response in
            guard let self, self.active, let panel, self.openPanel === panel else { return }
            self.openPanel = nil
            if response == .OK { self.importMedia(urls: panel.urls) }
        }
    }
    private func chooseShelf() {
        let picker = NotesShelfMediaPicker(choices: shelfChoices?() ?? [], dark: true)
        picker.onSelect = { [weak self, weak picker] id in
            guard let self, self.active, let picker, self.secondaryMenu === picker else { return }; self.closeMenu()
            do {
                guard let access = try self.shelfAccess?(id) else { return }
                self.importMedia(urls: [access.url], access: access)
            } catch { self.showError(error.localizedDescription) }
        }; showMenu(picker)
    }
    func importMedia(urls: [URL], at point: CGPoint? = nil, access: ShelfFileAccess? = nil) {
        guard active, !importing, !urls.isEmpty else { access?.close(); return }
        guard urls.count <= ProjectionModel.maximumMedia - model.media.count else {
            access?.close(); showError(L10n.text("Projection supports up to 16 media items.", "投影最多可添加 16 个媒体。")); return
        }
        importing = true; importGeneration += 1
        let ticket = ProjectionImportCancellation(); importTicket = ticket
        let token = importGeneration, destination = point ?? CGPoint(x: bounds.midX, y: bounds.midY)
        message.string = L10n.text("Loading media…", "正在载入媒体…")
        importQueue.async { [weak self, access, ticket] in
            defer { access?.close() }
            let result = Result { try urls.map { url in
                guard !ticket.isCancelled else { throw CocoaError(.userCancelled) }
                return try NotesMediaFactory.makeReference(from: url)
            } }
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.completedImportCountForVerification += 1
                guard self.active, self.importGeneration == token, !ticket.isCancelled else { return }
                self.importing = false; self.importTicket = nil; self.message.string = ""
                switch result {
                case .success(let references):
                    for (index, reference) in references.enumerated() {
                        if self.model.addMedia(reference, at: CGPoint(x: destination.x + CGFloat(index) * 12, y: destination.y + CGFloat(index) * 12), in: self.bounds) != nil { self.onEvent?(.mediaAdded(reference.kind)) }
                    }
                    self.syncMedia()
                case .failure(let error): self.showError(error.localizedDescription)
                }
            }
        }
    }
    private func showError(_ text: String) { message.string = text }

    override func mouseMoved(with event: NSEvent) { pointer = convert(event.locationInWindow, from: nil); updateCursor(); refreshControlHighlights() }
    override func mouseEntered(with event: NSEvent) { mouseMoved(with: event) }
    override func mouseExited(with event: NSEvent) { pointer = nil; updateCursor(); refreshControlHighlights() }
    override func rightMouseDown(with event: NSEvent) { perform("eraser") }
    override func scrollWheel(with event: NSEvent) {
        guard active else { return }
        model.setBrushWidth(model.brushWidth + event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 0.12 : 1))
        pendingBrushChange = true
        if event.phase.contains(.ended) || event.momentumPhase.contains(.ended) { commitBrushChange() }
        toolbar.update(model); pointer = convert(event.locationInWindow, from: nil); updateCursor()
    }
    override func mouseDown(with event: NSEvent) {
        guard active else { return }
        window?.makeFirstResponder(self); let point = convert(event.locationInWindow, from: nil); pointer = point
        if secondaryMenu != nil { closeMenu(); return }
        commitBrushChange()
        message.string = ""
        for item in model.media.reversed() {
            guard let node = nodes[item.id] else { continue }
            let local = CGPoint(x: point.x - item.frame.minX, y: point.y - item.frame.minY)
            if node.closeRect.contains(local) { model.removeMedia(item.id); syncMedia(); onEvent?(.mediaRemoved); return }
            if node.playRect.contains(local), item.reference.kind != .image { node.presentation.togglePlayback(); return }
            if node.seekRect.insetBy(dx: 0, dy: -6).contains(local), item.reference.kind == .video {
                gesture = .seek(item.id); seek(item.id, point); return
            }
            if node.resizeRect.contains(local) { model.bringForward(item.id); gesture = .resize(item.id, point, item.frame); syncMedia(); return }
            if node.headerRect.contains(local) { model.bringForward(item.id); gesture = .move(item.id, point, item.frame); syncMedia(); return }
        }
        if model.erasing { lastEraserPoint = point; eraseChanged = model.erase(at: point, size: bounds.size); if eraseChanged { rebuildStrokes() } }
        else if let color = NotesRGBA(model.color) {
            pendingStroke = NotesDrawingStroke(points: [normalized(point)], width: model.brushWidth, color: color)
            updateLiveStroke()
        }
        updateCursor()
    }
    override func mouseDragged(with event: NSEvent) {
        guard active else { return }; let point = convert(event.locationInWindow, from: nil); pointer = point
        if let gesture {
            switch gesture {
            case .move(let id, let start, let frame): model.setFrame(frame.offsetBy(dx: point.x - start.x, dy: point.y - start.y), id: id, in: bounds)
            case .resize(let id, let start, let frame): model.setFrame(CGRect(origin: frame.origin, size: CGSize(width: frame.width + point.x - start.x, height: frame.height + point.y - start.y)), id: id, in: bounds)
            case .seek(let id): seek(id, point); return
            }; syncMedia(); return
        }
        if model.erasing {
            if lastEraserPoint.map({ hypot(point.x - $0.x, point.y - $0.y) >= max(1, model.brushWidth / 4) }) != false {
                lastEraserPoint = point
                if model.erase(at: point, size: bounds.size) { eraseChanged = true; rebuildStrokes() }
            }
        } else if var stroke = pendingStroke, stroke.points.count < NotesDrawing.maximumPointsPerStroke {
            let next = normalized(point)
            if let last = stroke.points.last, hypot((next.x - last.x) * bounds.width, (next.y - last.y) * bounds.height) >= 1 {
                stroke.points.append(next); pendingStroke = stroke; updateLiveStroke()
            }
        }
        updateCursor()
    }
    override func mouseUp(with event: NSEvent) {
        guard active else { return }
        gesture = nil
        if let stroke = pendingStroke {
            if model.append(stroke) {
                let retained = strokeLayer(stroke); strokes.addSublayer(retained)
                strokeLayers.append(retained); renderedStrokes.append(stroke); onEvent?(.strokeCompleted)
            }
            else { showError(L10n.text("This drawing has reached the stroke limit.", "此画布已达到笔画数量上限。")) }
        }
        if eraseChanged { onEvent?(.erased) }
        pendingStroke = nil; lastEraserPoint = nil; eraseChanged = false; liveStroke.path = nil
    }
    override func keyDown(with event: NSEvent) {
        if event.keyCode == 53 { if secondaryMenu != nil { closeMenu() } else { onClose?() }; return }
        super.keyDown(with: event)
    }
    private func normalized(_ point: CGPoint) -> NotesDrawingPoint {
        NotesDrawingPoint(x: min(1, max(0, point.x / max(1, bounds.width))), y: min(1, max(0, point.y / max(1, bounds.height))))
    }
    private func commitBrushChange() { if pendingBrushChange { pendingBrushChange = false; onEvent?(.brushChanged) } }
    private func updateLiveStroke() {
        guard let stroke = pendingStroke else { liveStroke.path = nil; return }
        liveStroke.path = NotesDrawing.path(stroke, size: bounds.size)
        liveStroke.strokeColor = stroke.color.color.cgColor; liveStroke.lineWidth = stroke.width
    }
    private func updateCursor() {
        guard active, let pointer, bounds.contains(pointer), !toolbar.frame.contains(pointer), secondaryMenu?.frame.contains(pointer) != true else { cursor.path = nil; return }
        let diameter = model.brushWidth
        cursor.path = CGPath(ellipseIn: CGRect(x: pointer.x - diameter / 2, y: pointer.y - diameter / 2, width: diameter, height: diameter), transform: nil)
        cursor.strokeColor = (model.erasing ? NSColor.white : model.color).cgColor
        cursor.lineDashPattern = model.erasing ? [3, 2] : nil
    }
    private func seek(_ id: UUID, _ point: CGPoint) {
        guard let item = model.media.first(where: { $0.id == id }), let node = nodes[id], let duration = item.reference.duration else { return }
        let local = point.x - item.frame.minX
        node.presentation.seek(to: min(1, max(0, (local - node.seekRect.minX) / max(1, node.seekRect.width))) * duration)
    }
    func refreshControlHighlights() {
        if let layer { HUDControlHighlightLayer.update(in: layer, point: pointer) }
        for node in nodes.values {
            HUDControlHighlightLayer.update(in: node.root, point: pointer.map { CGPoint(x: $0.x - node.root.frame.minX, y: $0.y - node.root.frame.minY) })
        }
    }
    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation { acceptedDrop(sender) ? .copy : [] }
    override func draggingUpdated(_ sender: NSDraggingInfo) -> NSDragOperation { draggingEntered(sender) }
    override func wantsPeriodicDraggingUpdates() -> Bool { false }
    override func prepareForDragOperation(_ sender: NSDraggingInfo) -> Bool { acceptedDrop(sender) }
    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        guard acceptedDrop(sender), let urls = sender.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL] else { return false }
        importMedia(urls: urls, at: convert(sender.draggingLocation, from: nil)); return true
    }
    private func acceptedDrop(_ sender: NSDraggingInfo) -> Bool {
        guard active, !importing, sender.draggingSourceOperationMask.contains(.copy),
              let urls = sender.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL], !urls.isEmpty,
              urls.count <= ProjectionModel.maximumMedia - model.media.count else { return false }
        return urls.allSatisfy(\.isFileURL)
    }
    private func removeAnimations(_ root: CALayer?) { root?.removeAllAnimations(); root?.sublayers?.forEach { removeAnimations($0) } }
    var mediaPresentationCount: Int { nodes.count }
    var mediaArtworkCountForVerification: Int { nodes.values.filter { $0.presentation.layer.contents != nil }.count }
    var hasLiveMediaResources: Bool { nodes.values.contains { $0.presentation.hasActiveDecoder || $0.presentation.hasScheduledFrame || $0.presentation.hasProgressObserver } }
    var backgroundVisibleForVerification: Bool { background.opacity > 0 && dots.opacity > 0 }
    var brushCursorDiameterForVerification: CGFloat { cursor.path?.boundingBox.width ?? 0 }
    var animationCountForVerification: Int {
        func count(_ value: CALayer) -> Int { (value.animationKeys()?.count ?? 0) + (value.sublayers ?? []).reduce(0) { $0 + count($1) } }
        return layer.map(count) ?? 0
    }
}

private final class ProjectionImportCancellation {
    private let lock = NSLock()
    private var cancelled = false
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    func cancel() { lock.lock(); cancelled = true; lock.unlock() }
}

private final class ProjectionMediaNode {
    let root = CALayer(), presentation: NotesMediaPresentation
    let face = CALayer(), title = CATextLayer(), play = CATextLayer(), close = CATextLayer(), time = CATextLayer()
    let rail = CALayer(), fill = CALayer(), thumb = CALayer(), resize = CAShapeLayer()
    var onAction: ((String) -> Void)?
    private let playButton = ProjectionMediaAXButton(), closeButton = ProjectionMediaAXButton()
    private var playHighlight: HUDControlHighlightLayer?, closeHighlight: HUDControlHighlightLayer?
    private var hasLayout = false
    private(set) var headerRect = CGRect.zero, closeRect = CGRect.zero, playRect = CGRect.zero, seekRect = CGRect.zero, resizeRect = CGRect.zero
    init(item: ProjectionMediaItem, host: NSView) {
        presentation = NotesMediaPresentation(reference: item.reference, maximumDimension: 768)
        root.actions = ["position": NSNull(), "bounds": NSNull()]
        for layer in [face, presentation.layer, title, close, play, time, rail, fill, thumb, resize] { root.addSublayer(layer) }
        for text in [title, close, play, time] {
            text.font = NSFont.systemFont(ofSize: 10, weight: .semibold); text.fontSize = 10
            text.foregroundColor = NSColor.white.cgColor; text.contentsScale = 2; text.truncationMode = .end
        }
        title.string = item.reference.displayName; close.string = "×"; close.fontSize = 17
        face.backgroundColor = NSColor(white: 0.06, alpha: 0.86).cgColor
        face.borderColor = HUDRuntimeAppearance.accent.withAlphaComponent(0.5).cgColor; face.borderWidth = 0.7
        rail.backgroundColor = NSColor.white.withAlphaComponent(0.2).cgColor
        fill.backgroundColor = HUDRuntimeAppearance.accent.cgColor; thumb.backgroundColor = NSColor.white.cgColor
        resize.fillColor = nil; resize.strokeColor = NSColor.white.withAlphaComponent(0.5).cgColor; resize.lineWidth = 1
        for button in [playButton, closeButton] { button.title = ""; button.isBordered = false; host.addSubview(button) }
        playButton.onPress = { [weak self] in self?.onAction?("play") }
        closeButton.onPress = { [weak self] in self?.onAction?("remove") }
        closeButton.setAccessibilityLabel(L10n.text("Remove media", "移除媒体"))
    }
    func layout(_ frame: CGRect) {
        if hasLayout, root.frame == frame { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        if hasLayout, root.frame.size == frame.size {
            root.frame = frame
            playButton.frame = playRect.offsetBy(dx: frame.minX, dy: frame.minY)
            closeButton.frame = closeRect.offsetBy(dx: frame.minX, dy: frame.minY)
            CATransaction.commit(); return
        }
        hasLayout = true
        root.frame = frame; face.frame = root.bounds
        let moving = presentation.reference.kind != .image
        headerRect = CGRect(x: 0, y: 0, width: frame.width, height: 24)
        closeRect = CGRect(x: frame.width - 23, y: 1, width: 22, height: 22)
        title.frame = CGRect(x: 7, y: 6, width: frame.width - 38, height: 14); close.frame = closeRect
        presentation.layer.frame = CGRect(x: 5, y: 25, width: frame.width - 10, height: max(1, frame.height - (moving ? 58 : 30)))
        playRect = moving ? CGRect(x: 7, y: frame.height - 25, width: 22, height: 22) : .zero
        play.frame = playRect; play.isHidden = !moving; play.fontSize = 15
        playButton.frame = playRect.offsetBy(dx: frame.minX, dy: frame.minY); playButton.isHidden = !moving
        closeButton.frame = closeRect.offsetBy(dx: frame.minX, dy: frame.minY)
        playHighlight?.removeFromSuperlayer(); closeHighlight?.removeFromSuperlayer()
        playHighlight = moving ? HUDControlHighlightLayer.add(to: root, rect: playRect, shape: .cutCorner, framed: true) : nil
        closeHighlight = HUDControlHighlightLayer.add(to: root, rect: closeRect, shape: .cutCorner, framed: true)
        seekRect = CGRect(x: 37, y: frame.height - 20, width: max(1, frame.width - 108), height: 12)
        time.frame = CGRect(x: frame.width - 62, y: frame.height - 21, width: 44, height: 16)
        rail.frame = CGRect(x: seekRect.minX, y: seekRect.midY, width: seekRect.width, height: 2)
        for layer in [rail, fill, thumb, time] { layer.isHidden = presentation.reference.kind != .video }
        resizeRect = CGRect(x: frame.width - 14, y: frame.height - 14, width: 14, height: 14)
        let path = CGMutablePath(); path.move(to: CGPoint(x: frame.width - 11, y: frame.height - 3)); path.addLine(to: CGPoint(x: frame.width - 3, y: frame.height - 11))
        path.move(to: CGPoint(x: frame.width - 6, y: frame.height - 3)); path.addLine(to: CGPoint(x: frame.width - 3, y: frame.height - 6)); resize.path = path
        refreshProgress(); CATransaction.commit()
    }
    func refreshProgress() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        play.string = presentation.isPlaying ? "Ⅱ" : "▶"
        playButton.setAccessibilityLabel(presentation.isPlaying ? L10n.text("Pause", "暂停") : L10n.text("Play", "播放"))
        let seconds = presentation.currentTime, duration = presentation.reference.duration ?? 0
        let fraction = duration > 0 ? min(1, max(0, seconds / duration)) : 0
        fill.frame = CGRect(x: seekRect.minX, y: seekRect.midY, width: seekRect.width * fraction, height: 2)
        thumb.frame = CGRect(x: seekRect.minX + seekRect.width * fraction - 2.5, y: seekRect.midY - 3, width: 5, height: 8)
        time.string = String(format: "%d:%02d", Int(seconds) / 60, Int(seconds) % 60)
        if let error = presentation.error { title.string = error.localizedDescription }
        CATransaction.commit()
    }
    func dispose() { presentation.dispose(); root.removeFromSuperlayer(); playButton.removeFromSuperview(); closeButton.removeFromSuperview() }
}

private final class ProjectionMediaAXButton: NSButton {
    var onPress: (() -> Void)?
    override init(frame: NSRect) { super.init(frame: frame); target = self; action = #selector(press) }
    convenience init() { self.init(frame: .zero) }
    required init?(coder: NSCoder) { nil }
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    @objc private func press() { onPress?() }
}
