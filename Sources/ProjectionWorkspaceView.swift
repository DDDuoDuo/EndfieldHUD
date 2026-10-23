import AppKit
import QuartzCore

/// A full-screen, event-driven drawing plane. Strokes and media references live
/// in the session model; decoding and playback exist only while this view shows.
/// Its chrome is the redesign's own: the toolbar panel, menus and pop-overs
/// (HUDMenu / HUDPopover, the shared custom-colour picker) routed here like a
/// centre page's, with VoiceOver stand-ins; nothing native but this view.
final class ProjectionWorkspaceView: NSView, HUDControlFeedbackHost {
    let model: ProjectionModel
    var onClose: (() -> Void)?
    var onEvent: ((ProjectionEvent) -> Void)?
    var onBackgroundChange: (() -> Void)?
    var shelfChoices: (() -> [NotesShelfMediaChoice])?
    var shelfAccess: ((UUID) throws -> ShelfFileAccess)?
    private(set) var active = false
    let env: HUDDesignEnvironment
    /// The app's Reduce Motion for this workspace's controls (env reads it).
    private final class MotionSetting { var reduced = false }
    private let motion = MotionSetting()
    let toolbar: ProjectionToolbar
    /// The toolbar, the open menu or pop-over and the slider bubble.
    private let chrome = HUDLayer.plain("projection.chrome")
    private let router = HUDInteractionRouter()
    private var standIns: HUDAccessibilityStandIns?
    /// The open menu or pop-over: the action that opened it and its controls.
    private var popup: (id: String, layer: CALayer, controls: [HUDControl], menu: HUDMenu?)?
    var popupID: String? { popup?.id }
    private var chromeTracking = false
    private let background = CALayer(), dots = CAShapeLayer(), strokes = HUDDecorativeContentLayer()
    private let mediaLayer = HUDDecorativeContentLayer(), cursor = CAShapeLayer(), liveStroke = CAShapeLayer()
    private let message = HUDTextLayer(style: HUDTextStyle(size: 12, weight: .bold, color: HUDColor.onDarkTag, tracking: 0.04), alignment: .center)
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
        motion.reduced = reduceMotion
        env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { [motion] in motion.reduced || HUDRuntimeAppearance.reduceMotion })
        toolbar = ProjectionToolbar(env: env)
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
        for item in [background, dots, mediaLayer, strokes, liveStroke, cursor] {
            item.actions = ["position": NSNull(), "bounds": NSNull(), "contents": NSNull(), "opacity": NSNull(), "path": NSNull()]
            layer?.addSublayer(item)
        }
        HUDMotion.perform {
            chrome.zPosition = 3_000_000; chrome.addSublayer(toolbar.layer); chrome.addSublayer(message)
            layer?.addSublayer(chrome)
        }
        background.name = "projection.background"; dots.name = "projection.dots"
        strokes.name = "projection.strokes"; mediaLayer.name = "projection.media"
        cursor.fillColor = nil; cursor.lineWidth = 1; cursor.zPosition = 500_000
        liveStroke.fillColor = nil; liveStroke.lineCap = .round; liveStroke.lineJoin = .round
        toolbar.onAction = { [weak self] in self?.perform($0) }
        let standIns = HUDAccessibilityStandIns(host: self)
        standIns.onPerform = { [weak self] id in self?.performControl(id) }
        standIns.onAdjust = { [weak self] id, value in
            guard let slider = self?.router.controls.first(where: { $0.id == id }) as? HUDSlider else { return }
            slider.setValue(value); slider.onChange?(value, true)
        }
        self.standIns = standIns
        refreshRouter()
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
            closePopup(animated: false); router.cancel(); chromeTracking = false
            nodes.values.forEach { $0.presentation.setVisible(false) }
            removeAnimations(layer)
        }
    }
    func update(configuration: AppConfiguration) {
        reduceMotion = configuration.reduceMotion; motion.reduced = reduceMotion
        if reduceMotion { removeAnimations(layer) }
    }
    func dispose() {
        setActive(false); nodes.values.forEach { $0.presentation.dispose() }
        nodes.removeAll(); mediaLayer.sublayers?.forEach { $0.removeFromSuperlayer() }
        standIns?.update([]); unregisterDraggedTypes()
    }
    func updateScreenGeometry(_ screen: NSScreen) {
        let safe: CGFloat
        if #available(macOS 12.0, *) { safe = screen.safeAreaInsets.top } else { safe = 0 }
        toolbarTopInset = ProjectionToolbar.topInset(safeAreaTop: safe, visibleTop: screen.frame.maxY - screen.visibleFrame.maxY)
        layoutWorkspace()
    }
    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        let scale = window?.backingScaleFactor ?? 2
        guard scale != env.renderScale else { return }
        env.renderScale = scale; env.applyScale(to: chrome)
    }
    override func layout() { super.layout(); layoutWorkspace() }
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking { removeTrackingArea(tracking) }
        let next = NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeAlways, .inVisibleRect], owner: self)
        addTrackingArea(next); tracking = next
    }
    /// The toolbar's frame in view coordinates (= chrome coordinates).
    var toolbarFrame: CGRect { toolbar.layer.frame }
    private func layoutWorkspace() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        background.frame = bounds; dots.frame = bounds; strokes.frame = bounds; mediaLayer.frame = bounds; chrome.frame = bounds
        let size = toolbar.contentSize
        toolbar.layer.frame = CGRect(origin: CGPoint(x: max(6, ((bounds.width - size.width) / 2).rounded()),
                                                     y: min(toolbarTopInset, max(6, bounds.height - size.height - 8))), size: size)
        message.lineFrame = CGRect(x: max(10, bounds.midX - 280), y: toolbarFrame.maxY + 18, width: min(560, bounds.width - 20), height: 12)
        for item in model.media { model.setFrame(item.frame, id: item.id, in: bounds) }
        refreshBackground(); rebuildStrokes(); syncMedia(); positionPopup()
        CATransaction.commit()
        updateAccessibility()
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
                node = ProjectionMediaNode(item: item, host: self, env: env)
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

    // MARK: Actions

    /// Toolbar actions (and the harness's): color:RRGGBB, color:custom (or
    /// color), pen, eraser, brush, background, appearance, media, clear,
    /// clear:confirm, close.
    func perform(_ action: String) {
        guard active else { return }
        window?.makeFirstResponder(self)
        if action.hasPrefix("color:"), let hex = UInt32(action.dropFirst(6), radix: 16) {
            setPen(color: hex); return
        }
        // A second press on the control that opened the menu or pop-over closes it.
        if let popup, popup.id == action || (popup.id == "color" && action == "color:custom") { closePopup(); return }
        switch action {
        case "close": onClose?()
        case "pen": model.erasing = false; toolbar.update(model); updateCursor(); onEvent?(.brushChanged)
        case "eraser": model.erasing.toggle(); toolbar.update(model); updateCursor(); onEvent?(.brushChanged)
        case "background": model.backgroundEnabled.toggle(); refreshBackground(); onEvent?(.backgroundChanged)
        case "color", "color:custom":
            let picker = NotesFormatBar.colorPicker(id: "penColor", title: L10n.text("PEN COLOR", "画笔颜色"),
                                                    color: NotesFormatBar.hex(model.color.cgColor), env: env, bubbleHost: chrome) { [weak self] hex in
                self?.setPen(color: hex, closing: false)
            }
            showPopover("color", picker.popover, controls: picker.controls, anchor: "color:custom")
        case "brush":
            let slider = adjustment("brush", L10n.text("Brush thickness", "画笔粗细"), value: Double(model.brushWidth), range: 1...80, format: .pixels) { [weak self] value in
                guard let self else { return }
                self.model.setBrushWidth(CGFloat(value)); self.pendingMenuEvent = .brushChanged
                self.toolbar.update(self.model); self.updateCursor()
            }
            showPopover("brush", popover(L10n.text("BRUSH", "画笔"), [slider]), controls: [slider], anchor: "brush")
        case "appearance":
            let darkness = adjustment("darkness", L10n.text("Darkness", "背景深度"), value: model.darkness * 100, range: 0...100, format: .percent) { [weak self] value in
                self?.model.setDarkness(value / 100); self?.pendingMenuEvent = .backgroundChanged; self?.refreshBackground()
            }
            let blur = adjustment("blur", L10n.text("Blur", "模糊"), value: model.blur * 100, range: 0...100, format: .percent) { [weak self] value in
                self?.model.setBlur(value / 100); self?.pendingMenuEvent = .backgroundChanged; self?.refreshBackground()
            }
            showPopover("appearance", popover(L10n.text("BACKGROUND", "背景"), [darkness, blur]), controls: [darkness, blur], anchor: "appearance")
        case "media":
            showMenu("media", items: [HUDMenu.Item(id: "finder", title: L10n.text("Choose in Finder", "从 Finder 选择")),
                                      HUDMenu.Item(id: "shelf", title: L10n.text("Choose from Shelf", "从暂存架选择"))], anchor: "media") { [weak self] item in
                guard let self else { return }
                self.closePopup()
                if item.id == "shelf" { self.chooseShelf() } else { self.chooseFinder() }
            }
        case "clear": showClearConfirmation()
        case "clear:confirm": clearContent()
        default: break
        }
    }

    private func setPen(color hex: UInt32, closing: Bool = true) {
        if closing, popup?.id == "color" { closePopup() }
        model.color = NotesFormatBar.color(hex); model.erasing = false
        toolbar.update(model); updateCursor()
        if popup?.id == "color" { pendingMenuEvent = .brushChanged } else { onEvent?(.brushChanged) }
    }

    /// One labelled slider row of a workspace pop-over (label above, slider below).
    private func adjustment(_ id: String, _ title: String, value: Double, range: ClosedRange<Double>, format: HUDValueFormat,
                            change: @escaping (Double) -> Void) -> HUDSlider {
        let slider = HUDSlider(id: id, label: title, value: min(range.upperBound, max(range.lowerBound, value)),
                               range: HUDSlider.Range(minimum: range.lowerBound, maximum: range.upperBound, step: 1),
                               format: format, width: 236, dark: true, showsTicks: false, env: env)
        slider.bubbleHost = chrome
        slider.onChange = { value, _ in change(value) }
        return slider
    }

    /// A pop-over of labelled sliders, 54 px a row.
    private func popover(_ title: String, _ sliders: [HUDSlider]) -> HUDPopover {
        let result = HUDPopover(title: title, width: 260, contentHeight: CGFloat(sliders.count) * 54 - 8, env: env)
        HUDMotion.perform {
            for (index, slider) in sliders.enumerated() {
                let label = HUDTextLayer(slider.label, style: HUDTextStyle(size: 12, weight: .regular, color: HUDColor.tone(0xDDDDDD)))
                label.lineFrame = CGRect(x: 0, y: CGFloat(index) * 54, width: 236, height: 12)
                slider.layer.frame.origin = CGPoint(x: 0, y: CGFloat(index) * 54 + 16)
                result.content.addSublayer(label); result.content.addSublayer(slider.layer)
            }
        }
        env.applyScale(to: result.layer)
        return result
    }

    private func showClearConfirmation() {
        let result = HUDPopover(title: L10n.text("CLEAR ALL", "清空"), width: 280, contentHeight: 18 + 12 + HUDButton.Size.small.height, env: env)
        let text = HUDTextLayer(L10n.text("Clear drawings and media?", "清空绘画和媒体？"), style: HUDTextStyle(size: 12.5, weight: .regular, color: HUDColor.menuText))
        let cancel = HUDButton(id: "clear:cancel", title: L10n.text("Cancel", "取消"), kind: .secondary, size: .small, env: env)
        let confirm = HUDButton(id: "clear:confirm", title: L10n.text("Clear all", "清空"), icon: .trash, kind: .primary, size: .small, env: env)
        confirm.place(at: CGPoint(x: 256 - confirm.preferredWidth, y: 30))
        cancel.place(at: CGPoint(x: confirm.layer.frame.minX - 8 - cancel.preferredWidth, y: 30))
        cancel.onActivate = { [weak self] in self?.closePopup() }
        confirm.onActivate = { [weak self] in self?.clearContent() }
        HUDMotion.perform {
            text.lineFrame = CGRect(x: 0, y: 2, width: 256, height: 12.5)
            [text, cancel.layer, confirm.layer].forEach(result.content.addSublayer)
        }
        env.applyScale(to: result.layer)
        showPopover("clear", result, controls: [cancel, confirm], anchor: "clear")
    }

    private func clearContent() {
        let hadPendingWork = importing || pendingStroke != nil
        importGeneration += 1; importing = false
        importTicket?.cancel(); importTicket = nil
        let panel = openPanel; openPanel = nil; panel?.cancel(nil)
        pendingStroke = nil; gesture = nil; eraseChanged = false; lastEraserPoint = nil
        liveStroke.path = nil; showMessage("")
        let changed = model.clearContent()
        rebuildStrokes(); syncMedia(); closePopup(); updateCursor()
        if changed || hadPendingWork { onEvent?(.cleared) }
    }

    // MARK: Menus and pop-overs

    private func showPopover(_ id: String, _ popover: HUDPopover, controls: [HUDControl], anchor: String) {
        popover.onClose = { [weak self] in self?.closePopup() }
        open(id, layer: popover.layer, controls: controls + (popover.close.map { [$0] } ?? []), menu: nil, anchor: anchor)
        positionPopup()
        if !reduceMotion { popover.playEntrance() }
    }

    private func showMenu(_ id: String, items: [HUDMenu.Item], anchor: String, pick: @escaping (HUDMenu.Item) -> Void) {
        let menu = HUDMenu(id: id, items: items, minimumWidth: 200, env: env)
        menu.onPick = pick
        menu.onClose = { [weak self] in self?.closePopup() }
        open(id, layer: menu.layer, controls: menu.controls, menu: menu, anchor: anchor)
        positionPopup()
        if !reduceMotion { menu.playEntrance() }
    }

    private var popupAnchor: String?

    private func open(_ id: String, layer: CALayer, controls: [HUDControl], menu: HUDMenu?, anchor: String) {
        if popup != nil { closePopup(animated: false) }
        popup = (id, layer, controls, menu); popupAnchor = anchor
        HUDMotion.perform { layer.zPosition = 1; chrome.addSublayer(layer) }
        toolbar.setOpen(id)
        refreshRouter(); updateCursor(); refreshControlHighlights()
    }

    /// Below its toolbar control, kept on screen.
    private func positionPopup() {
        guard let popup else { return }
        let anchor = popupAnchor.flatMap { toolbar.control($0) }.map { chrome.convert($0.rect(in: $0.layer), from: $0.layer) } ?? toolbarFrame
        let size = popup.layer.bounds.size
        let x = min(max(8, anchor.maxX - size.width), bounds.width - size.width - 8)
        HUDMotion.perform { popup.layer.frame = CGRect(origin: CGPoint(x: popup.menu == nil ? x : min(anchor.minX, bounds.width - size.width - 8), y: toolbarFrame.maxY + 8), size: size) }
    }

    func closePopup(animated: Bool = true) {
        guard let previous = popup else { return }
        popup = nil; popupAnchor = nil
        toolbar.setOpen(nil)
        if let event = pendingMenuEvent { pendingMenuEvent = nil; onEvent?(event) }
        previous.controls.forEach { ($0 as? HUDSlider)?.cancelDrag(); $0.clearInteractionState() }
        if animated, active, !reduceMotion {
            let layer = previous.layer
            CATransaction.begin()
            CATransaction.setCompletionBlock { [weak layer] in layer?.removeFromSuperlayer() }
            HUDMotion.animate(layer, "opacity", to: Float(0), duration: 0.14, timing: HUDEasing.ease, env: env, key: "projection.menu.close")
            CATransaction.commit()
        } else { previous.layer.removeFromSuperlayer() }
        refreshRouter(); window?.makeFirstResponder(self); updateCursor(); refreshControlHighlights()
    }

    private func refreshRouter() {
        router.set(toolbar.controls + (popup?.controls ?? []))
        updateAccessibility()
    }

    private func updateAccessibility() {
        standIns?.update(router.accessibilityItems(in: chrome))
    }

    /// Activates a chrome control by id (VoiceOver stand-ins, tests).
    func performControl(_ id: String) {
        guard active, let control = router.controls.first(where: { $0.id == id }) else { return }
        control.activate()
    }

    /// The chrome (toolbar, menu or pop-over) under a point.
    private func chromeContains(_ point: CGPoint) -> Bool {
        toolbarFrame.contains(point) || popup?.layer.frame.contains(point) == true
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
        let choices = shelfChoices?() ?? []
        guard !choices.isEmpty else { showMessage(L10n.text("The shelf has no images or videos.", "暂存架中没有图片或视频。")); return }
        showMenu("shelf", items: choices.map { HUDMenu.Item(id: $0.id.uuidString, title: $0.title, enabled: $0.isSupported && $0.isAvailable) },
                 anchor: "media") { [weak self] item in
            guard let self, let id = UUID(uuidString: item.id) else { return }
            self.closePopup()
            do {
                guard let access = try self.shelfAccess?(id) else { return }
                self.importMedia(urls: [access.url], access: access)
            } catch { self.showMessage(error.localizedDescription) }
        }
    }
    func importMedia(urls: [URL], at point: CGPoint? = nil, access: ShelfFileAccess? = nil) {
        guard active, !importing, !urls.isEmpty else { access?.close(); return }
        guard urls.count <= ProjectionModel.maximumMedia - model.media.count else {
            access?.close(); showMessage(L10n.text("Projection supports up to 16 media items.", "投影最多可添加 16 个媒体。")); return
        }
        importing = true; importGeneration += 1
        let ticket = ProjectionImportCancellation(); importTicket = ticket
        let token = importGeneration, destination = point ?? CGPoint(x: bounds.midX, y: bounds.midY)
        showMessage(L10n.text("Loading media…", "正在载入媒体…"))
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
                self.importing = false; self.importTicket = nil; self.showMessage("")
                switch result {
                case .success(let references):
                    for (index, reference) in references.enumerated() {
                        if self.model.addMedia(reference, at: CGPoint(x: destination.x + CGFloat(index) * 12, y: destination.y + CGFloat(index) * 12), in: self.bounds) != nil { self.onEvent?(.mediaAdded(reference.kind)) }
                    }
                    self.syncMedia()
                case .failure(let error): self.showMessage(error.localizedDescription)
                }
            }
        }
    }
    private func showMessage(_ text: String) {
        HUDMotion.perform { message.text = text }
        env.applyScale(to: message)
    }
    var messageForVerification: String { message.text }
    var popupControlsForVerification: [HUDControl] { popup?.controls ?? [] }
    var popupLayerForVerification: CALayer? { popup?.layer }

    // MARK: Input

    override func mouseMoved(with event: NSEvent) { pointer = convert(event.locationInWindow, from: nil); updateCursor(); refreshControlHighlights() }
    override func mouseEntered(with event: NSEvent) { mouseMoved(with: event) }
    override func mouseExited(with event: NSEvent) { pointer = nil; updateCursor(); refreshControlHighlights() }
    override func rightMouseDown(with event: NSEvent) { if popup == nil { perform("eraser") } }
    override func scrollWheel(with event: NSEvent) {
        guard active else { return }
        let point = convert(event.locationInWindow, from: nil)
        if let menu = popup?.menu, popup?.layer.frame.contains(point) == true {
            menu.scroll(by: -event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12)); return
        }
        if chromeContains(point) { return }
        model.setBrushWidth(model.brushWidth + event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 0.12 : 1))
        pendingBrushChange = true
        if event.phase.contains(.ended) || event.momentumPhase.contains(.ended) { commitBrushChange() }
        toolbar.update(model); pointer = point; updateCursor()
    }
    override func mouseDown(with event: NSEvent) {
        guard active else { return }
        window?.makeFirstResponder(self); let point = convert(event.locationInWindow, from: nil); pointer = point
        if chromeContains(point) {
            chromeTracking = true
            router.pointerDown(at: point, in: chrome)
            HUDControlHighlightLayer.update(in: chrome, point: point, pressed: true)
            return
        }
        if popup != nil { closePopup(); return }
        commitBrushChange()
        showMessage("")
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
        if chromeTracking { router.pointerDragged(to: point, in: chrome); return }
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
        if chromeTracking {
            chromeTracking = false
            let point = convert(event.locationInWindow, from: nil)
            HUDControlHighlightLayer.update(in: chrome, point: point)
            router.pointerUp(at: point, in: chrome)
            return
        }
        gesture = nil
        if let stroke = pendingStroke {
            if model.append(stroke) {
                let retained = strokeLayer(stroke); strokes.addSublayer(retained)
                strokeLayers.append(retained); renderedStrokes.append(stroke); onEvent?(.strokeCompleted)
            }
            else { showMessage(L10n.text("This drawing has reached the stroke limit.", "此画布已达到笔画数量上限。")) }
        }
        if eraseChanged { onEvent?(.erased) }
        pendingStroke = nil; lastEraserPoint = nil; eraseChanged = false; liveStroke.path = nil
    }
    override func keyDown(with event: NSEvent) {
        if let menu = popup?.menu, menu.handleKey(keyCode: event.keyCode) { return }
        if event.keyCode == 53 { if popup != nil { closePopup() } else { onClose?() }; return }
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
        guard active, let pointer, bounds.contains(pointer), !chromeContains(pointer) else { cursor.path = nil; return }
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
        HUDControlHighlightLayer.update(in: chrome, point: pointer)
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

/// One media item on the plane: the redesign's dark panel with a hairline
/// ring and the theme corner mark, its title, × and play / pause icons, and
/// the theme-coloured progress rail of a video.
private final class ProjectionMediaNode {
    let root = CALayer(), presentation: NotesMediaPresentation
    let face = HUDLayer.plain("projection.media.face"), corner = HUDLayer.shape("projection.media.corner")
    let title: HUDTextLayer, time: HUDTextLayer
    let play = HUDIconLayer(.play, size: 12, color: HUDColor.onDarkTag), close = HUDIconLayer(.x, size: 11, color: HUDColor.onDarkTag)
    let rail = HUDLayer.plain(), fill = HUDLayer.plain(), thumb = HUDLayer.plain(), resize = HUDLayer.shape()
    var onAction: ((String) -> Void)?
    private let env: HUDDesignEnvironment
    private let playButton = ProjectionMediaAXButton(), closeButton = ProjectionMediaAXButton()
    private var playHighlight: HUDControlHighlightLayer?, closeHighlight: HUDControlHighlightLayer?
    private var hasLayout = false, shownPlaying: Bool?
    private(set) var headerRect = CGRect.zero, closeRect = CGRect.zero, playRect = CGRect.zero, seekRect = CGRect.zero, resizeRect = CGRect.zero
    init(item: ProjectionMediaItem, host: NSView, env: HUDDesignEnvironment) {
        self.env = env
        presentation = NotesMediaPresentation(reference: item.reference, maximumDimension: 768)
        title = HUDTextLayer(item.reference.displayName, style: HUDTextStyle(size: 10.5, weight: .bold, color: HUDColor.onDarkTag, tracking: 0.04))
        time = HUDTextLayer(style: HUDTextStyle(size: 10, weight: .bold, color: HUDColor.rgb(0xD9D9D9), tabular: true), alignment: .right)
        root.actions = ["position": NSNull(), "bounds": NSNull()]
        for layer in [face, presentation.layer, title, close, play, time, rail, fill, thumb, resize, corner] { root.addSublayer(layer) }
        HUDMotion.perform {
            // Media cards float over the dimmed desktop: dark in both themes.
            face.backgroundColor = HUDColor.rgb(0x141414, 0.9)
            face.borderWidth = 1; face.borderColor = HUDColor.rgb(0x3D3D3D)
            corner.fillColor = HUDColor.yellow
            rail.backgroundColor = HUDColor.white(0.2)
            fill.backgroundColor = HUDColor.yellow; thumb.backgroundColor = HUDColor.white(1)
            resize.fillColor = nil; resize.strokeColor = HUDColor.white(0.5); resize.lineWidth = 1
        }
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
        corner.frame = CGRect(x: -1, y: -1, width: 10, height: 10)
        corner.path = HUDShape.cornerMark(corner.bounds, length: 10, width: 2, corner: .topLeft)
        let moving = presentation.reference.kind != .image
        headerRect = CGRect(x: 0, y: 0, width: frame.width, height: 24)
        closeRect = CGRect(x: frame.width - 23, y: 1, width: 22, height: 22)
        title.lineFrame = CGRect(x: 8, y: 7, width: frame.width - 40, height: 10.5)
        close.position = CGPoint(x: closeRect.midX, y: closeRect.midY)
        presentation.layer.frame = CGRect(x: 5, y: 25, width: frame.width - 10, height: max(1, frame.height - (moving ? 58 : 30)))
        playRect = moving ? CGRect(x: 7, y: frame.height - 25, width: 22, height: 22) : .zero
        play.position = CGPoint(x: playRect.midX, y: playRect.midY); play.isHidden = !moving
        playButton.frame = playRect.offsetBy(dx: frame.minX, dy: frame.minY); playButton.isHidden = !moving
        closeButton.frame = closeRect.offsetBy(dx: frame.minX, dy: frame.minY)
        playHighlight?.removeFromSuperlayer(); closeHighlight?.removeFromSuperlayer()
        playHighlight = moving ? HUDControlHighlightLayer.add(to: root, rect: playRect, shape: .cutCorner, framed: true) : nil
        closeHighlight = HUDControlHighlightLayer.add(to: root, rect: closeRect, shape: .cutCorner, framed: true)
        seekRect = CGRect(x: 37, y: frame.height - 20, width: max(1, frame.width - 108), height: 12)
        time.lineFrame = CGRect(x: frame.width - 62, y: frame.height - 18, width: 44, height: 10)
        rail.frame = CGRect(x: seekRect.minX, y: seekRect.midY, width: seekRect.width, height: 2)
        for layer in [rail, fill, thumb, time] { layer.isHidden = presentation.reference.kind != .video }
        resizeRect = CGRect(x: frame.width - 14, y: frame.height - 14, width: 14, height: 14)
        let path = CGMutablePath(); path.move(to: CGPoint(x: frame.width - 11, y: frame.height - 3)); path.addLine(to: CGPoint(x: frame.width - 3, y: frame.height - 11))
        path.move(to: CGPoint(x: frame.width - 6, y: frame.height - 3)); path.addLine(to: CGPoint(x: frame.width - 3, y: frame.height - 6)); resize.path = path
        env.applyScale(to: root)
        refreshProgress(); CATransaction.commit()
    }
    func refreshProgress() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        if shownPlaying != presentation.isPlaying {
            shownPlaying = presentation.isPlaying
            play.set(presentation.isPlaying ? .pause : .play, size: 12)
            playButton.setAccessibilityLabel(presentation.isPlaying ? L10n.text("Pause", "暂停") : L10n.text("Play", "播放"))
        }
        let seconds = presentation.currentTime, duration = presentation.reference.duration ?? 0
        let fraction = duration > 0 ? min(1, max(0, seconds / duration)) : 0
        fill.frame = CGRect(x: seekRect.minX, y: seekRect.midY, width: seekRect.width * fraction, height: 2)
        thumb.frame = CGRect(x: seekRect.minX + seekRect.width * fraction - 2.5, y: seekRect.midY - 3, width: 5, height: 8)
        let clock = String(format: "%d:%02d", Int(seconds) / 60, Int(seconds) % 60)
        if time.text != clock { time.text = clock }
        if let error = presentation.error, title.text != error.localizedDescription { title.text = error.localizedDescription }
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
