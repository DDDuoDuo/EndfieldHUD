import AppKit
import AVFoundation

/// Media Assembly (spec §3.10): the 448 × 412 stage on the left and the
/// Adjust / Filters / Stickers panel on the right, 610 px across the box
/// (margin −10). Edits live in the session's MediaAssemblyController; its one
/// Core Image worker renders the latest preview, so a slider drag only writes
/// adjustments and the stage takes the rendered preview in place.
final class MediaAssemblyCanvas: HUDDesignCanvas {
    enum Tab: Int, CaseIterable { case adjust, filters, stickers }
    let controller: MediaAssemblyController
    var shelfChoices: (() -> [NotesShelfMediaChoice])?
    var shelfAccess: ((UUID) throws -> ShelfFileAccess)?
    private(set) var tab: Tab = .adjust
    private(set) var tool: MediaAssemblyTool?
    private(set) var viewport = MediaAssemblyViewport()
    private(set) var selectedStickerID: UUID?
    private(set) var isPresented = false
    /// Built when first needed, so a Media Assembly that is never shown adds
    /// nothing to the HUD's opening.
    private lazy var stage: MediaAssemblyStage = {
        let stage = MediaAssemblyStage(env: context.env)
        wire(stage)
        return stage
    }()
    private var panel: NSSavePanel?
    override var isPresentingPanel: Bool { panel != nil }

    private enum Drag {
        case pan(CGPoint)
        case move(UUID, start: CGPoint, original: MediaAssemblySticker)
        case scale(UUID), rotate(UUID)
        case crop(Int)
    }
    private var drag: Drag?
    private var sourceID: UUID?
    /// The geometry the shown preview was rendered with (spec §3.10 transitions).
    private var shownGeometry = MediaAssemblyGeometry()
    private var geometryAnimationEnd: CFTimeInterval = 0
    private var pendingPreview: DispatchWorkItem?
    private var animateNextLayout = false
    private var revealOnPreview = false
    /// The outgoing source's picture leaves with its page; the next page drops it.
    private var clearOnBuild = false
    private var closing = false
    private var closeWork: DispatchWorkItem?
    /// An overwrite reloads the written file; it keeps the stage in place.
    private var reloadingAfterOverwrite = false
    private enum ExportOutcome: Equatable { case saved(String), overwritten }
    /// The progress shown while exporting; it eases toward the real progress.
    private var exportShown: Double?
    private var exportOutcome: ExportOutcome?
    private var exportTimer: Timer?
    private var lastError: String?
    private var handlingChange = false, changePending = false, quiet = false

    private struct Structure: Equatable {
        let language: AppLanguage, document: UUID?, video: Bool, busy: Bool, exporting: Bool, closing: Bool
        let tab: Tab, tool: MediaAssemblyTool?
    }
    private var renderedStructure: Structure?
    private var binders: [() -> Void] = []
    private var parameterRows: [MediaAssemblyParameterRow] = []

    override var hitBounds: CGRect { CGRect(x: -10, y: 0, width: HUDMetric.box.width + 20, height: HUDMetric.box.height) }
    override var isInputLocked: Bool { super.isInputLocked || drag != nil }
    static let stageOrigin = CGPoint(x: -10, y: HUDMetric.headerHeight)
    static let panelFrame = CGRect(x: 448, y: HUDMetric.headerHeight, width: 152, height: MediaAssemblyStage.size.height)

    init(context: HUDDesignContext, controller: MediaAssemblyController) {
        self.controller = controller
        super.init(module: .mediaAssembly, context: context)
        controller.onChange = { [weak self] in self?.controllerChanged() }
    }

    deinit { exportTimer?.invalidate(); closeWork?.cancel(); pendingPreview?.cancel(); panel?.cancel(nil) }

    // MARK: Presentation

    /// While presented the session renders previews; hidden, the controller
    /// drops its decoders and the stage keeps its last artwork for the exit.
    func setPresented(_ value: Bool) {
        guard value != isPresented else { return }
        isPresented = value
        if !value {
            drag = nil; stopExportTicker()
            if let work = closeWork { work.cancel(); finishClose() }
            panel?.cancel(nil); panel = nil
        }
        controller.setActive(value)
        if value { controllerChanged() }
    }

    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        setPresented(true)
        return super.makeContent(for: module, style: style)
    }

    override func willLeave() { setPresented(false) }
    override func didActivate() {
        if controller.isExporting, exportShown == nil { exportShown = controller.progress }
        if exportShown != nil { startExportTicker() }
        refreshInPlace()
    }
    override func didDeactivate() { drag = nil; stage.setHovered(nil); stopExportTicker() }
    override func recordingModeChanged() {}

    // MARK: Controller changes

    private var structure: Structure {
        Structure(language: L10n.resolvedLanguage, document: controller.document?.id, video: controller.document?.isVideo == true,
                  busy: controller.isBusy, exporting: isExporting, closing: closing, tab: tab, tool: tool)
    }
    private var isExporting: Bool { controller.isExporting || exportShown != nil }
    /// Edits need a loaded source, no import, export or close in progress.
    var isEditable: Bool { controller.document != nil && !controller.isBusy && !isExporting && !closing }

    private func controllerChanged() {
        guard isPresented else { return }
        if handlingChange { changePending = true; return }
        handlingChange = true
        repeat { changePending = false; processChange() } while changePending
        handlingChange = false
    }

    private func processChange() {
        if controller.document?.id != sourceID { sourceChanged() }
        // A pending page swap builds from the latest state anyway.
        else if !quiet, !isTransitioning, renderedLanguage != nil, structure != renderedStructure { rerender() }
        else { refreshInPlace() }
        reportError()
    }

    /// A new (or no) source: every view edit resets and the page enters from
    /// below (spec §3.10 `subnav(0, 1)`); an overwrite's reload stays put.
    private func sourceChanged() {
        let reload = reloadingAfterOverwrite
        reloadingAfterOverwrite = false
        sourceID = controller.document?.id
        viewport.reset(); selectedStickerID = nil; drag = nil
        pendingPreview?.cancel(); pendingPreview = nil; geometryAnimationEnd = 0
        if tool == .crop { quietly { controller.setCropPreview(false) } }
        tool = nil
        shownGeometry = targetGeometry
        if !reload { clearOnBuild = true; revealOnPreview = true }
        guard renderedLanguage != nil else { return }
        if controller.document != nil, !reload, isActive { navigate(dx: 0, dy: 1) } else { rerender() }
    }

    private func refreshInPlace() {
        guard renderedLanguage != nil else { return }
        header.setCounter(counterText)
        binders.forEach { $0() }
        layoutStage()
    }

    private func reportError() {
        var text = controller.error?.localizedDescription
        if case MediaAssemblyError.cancelled? = controller.error { text = nil }
        guard text != lastError else { return }
        lastError = text
        if let text, isActive { toast(icon: .warn, "Notice", "提示", detail: text) }
    }

    /// Runs controller calls whose change notifications must not rebuild the
    /// page (the caller navigates or re-renders itself).
    private func quietly(_ body: () -> Void) {
        let previous = quiet; quiet = true; body(); quiet = previous
    }

    private var counterText: String {
        guard let document = controller.document else { return "EMPTY" }
        if let progress = exportShown ?? (controller.isExporting ? controller.progress : nil) {
            return "\(Int((min(1, max(0, progress)) * 100).rounded(.down)))%"
        }
        return document.isVideo ? "VID 01" : "IMG 01"
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        binders = []; parameterRows = []
        renderedStructure = structure
        let document = controller.document
        if clearOnBuild { clearOnBuild = false; stage.setImage(nil, crossfade: false); stage.setContentOpacity(1, animated: false) }
        buildHeader(document: document)
        HUDMotion.perform {
            stage.layer.frame = CGRect(origin: Self.stageOrigin, size: MediaAssemblyStage.size)
            page.layer.addSublayer(stage.layer)
        }
        page.stagger(stage.layer)
        if let seek = stage.configurePlayback(duration: document?.isVideo == true ? document?.duration : nil) {
            seek.onChange = { [weak self] value, final in
                guard let self else { return }
                if final { self.controller.seek(to: value) }
                else { self.stage.setPlayback(visible: true, playing: self.controller.isPlaying, time: value, duration: self.duration) }
            }
            hostBubble(seek)
        }
        relabelStageControls()
        page.add(stage.controls)
        let panel = HUDLayer.plain("media.panel")
        HUDMotion.perform {
            panel.frame = Self.panelFrame
            panel.opacity = document == nil || isExporting ? 0.35 : 1
            page.layer.addSublayer(panel)
        }
        page.stagger(panel)
        buildPanel(page, panel)
        layoutStage()
    }

    private func buildHeader(document: MediaAssemblyDocument?) {
        let exporting = isExporting
        let open = HUDButton(id: "open", title: L10n.text("Open", "打开"), icon: .open, kind: .secondary, size: .small, env: env)
        open.place(at: .zero)
        open.isEnabled = !exporting && !closing
        open.onActivate = { [weak self, weak open] in if let self, let open { self.openSourceMenu(anchor: open) } }
        let close = HUDIconButton(id: "close", label: L10n.text("Close source", "关闭源文件"), icon: .x, env: env)
        close.isEnabled = document != nil && !exporting && !closing
        close.onActivate = { [weak self] in self?.confirmClose() }
        let export = HUDButton(id: "export", title: exporting ? L10n.text("Cancel", "取消") : L10n.text("Export", "导出"),
                               icon: exporting ? .x : .export, kind: .primary, size: .small, env: env)
        export.place(at: .zero)
        export.isEnabled = controller.isExporting || (!exporting && document != nil && !controller.isBusy && !closing)
        export.onActivate = { [weak self, weak export] in if let self, let export { self.exportPressed(anchor: export) } }
        header.configure(crumbs: ["MEDIA ASSEMBLY"], code: "MOD-05", title: HUDModuleTitle("Media Assembly", "影像加工"),
                         counter: counterText, showsBack: false, actions: [open, close, export])
    }

    private func wire(_ stage: MediaAssemblyStage) {
        stage.zoomOut.onActivate = { [weak self] in self?.zoomStep(0.8) }
        stage.zoomIn.onActivate = { [weak self] in self?.zoomStep(1.25) }
        stage.zoomValue.onActivate = { [weak self] in self?.resetZoom() }
        stage.play.onActivate = { [weak self] in self?.controller.togglePlayback() }
        stage.pause.onActivate = { [weak self] in self?.controller.togglePlayback() }
        stage.open.onActivate = { [weak self] in guard let self else { return }; self.openSourceMenu(anchor: self.stage.open) }
    }

    /// Stage controls outlive pages; their labels follow the UI language.
    private func relabelStageControls() {
        stage.zoomOut.label = L10n.text("Zoom out", "缩小"); stage.zoomIn.label = L10n.text("Zoom in", "放大")
        stage.zoomValue.label = L10n.text("Reset zoom", "重置缩放")
        stage.play.label = L10n.text("Play", "播放"); stage.pause.label = L10n.text("Pause", "暂停")
        stage.seek?.label = L10n.text("Playback position", "播放进度")
        stage.open.title = L10n.text("Open", "打开")
        HUDMotion.perform { stage.open.layer.frame.origin.x = ((MediaAssemblyStage.size.width - stage.open.layer.bounds.width) / 2).rounded() }
        let playable = isEditable && controller.document?.isVideo == true && tool != .crop
        stage.play.isEnabled = playable; stage.pause.isEnabled = playable; stage.seek?.isEnabled = playable && tool != .trim
    }

    // MARK: Panel

    private func buildPanel(_ page: HUDDesignPage, _ panel: CALayer) {
        let enabled = isEditable
        let tabs = HUDTabs(id: "mediaTab", titles: [(L10n.text("Adjust", "调整"), nil), (L10n.text("Filters", "滤镜"), nil),
                                                   (L10n.text("Stickers", "贴纸"), nil)],
                           selectedIndex: tab.rawValue, width: Self.panelFrame.width, keycaps: false, compact: true, env: env)
        tabs.onSelect = { [weak self] index, direction in self?.selectTab(Tab(rawValue: index) ?? .adjust, direction: direction) }
        tabs.controls.forEach { $0.isEnabled = enabled }
        HUDMotion.perform { tabs.layer.frame.origin = .zero; panel.addSublayer(tabs.layer) }
        page.add(tabs.controls)
        if enabled { page.tabs = tabs }
        switch (tab, tool) {
        case (.adjust, nil): buildTiles(page, panel, enabled: enabled)
        case (.adjust, .crop?): buildCropPage(page, panel, enabled: enabled)
        case (.adjust, let tool?): buildSliderPage(tool, page, panel, enabled: enabled)
        case (.filters, _): buildFilters(page, panel, enabled: enabled)
        case (.stickers, _): buildStickers(page, panel, enabled: enabled && controller.document?.isVideo != true)
        }
    }

    private func buildTiles(_ page: HUDDesignPage, _ panel: CALayer, enabled: Bool) {
        let width = (Self.panelFrame.width - 6) / 2
        var tiles: [(MediaAssemblyToolTile, CGRect)] = []
        let grid: [(String, String, HUDIcon)] = [
            ("crop", MediaAssemblyTool.crop.title, .grid), ("rotate", L10n.text("Rotate", "旋转"), .rotate),
            ("mirror", L10n.text("Mirror", "镜像"), .mirror), ("adjust", MediaAssemblyTool.adjust.title, .gear),
            ("curves", MediaAssemblyTool.curves.title, .lyric), ("levels", MediaAssemblyTool.levels.title, .apps)]
        for (index, item) in grid.enumerated() {
            let frame = CGRect(x: CGFloat(index % 2) * (width + 6), y: 42 + CGFloat(index / 2) * 68, width: width, height: 62)
            tiles.append((MediaAssemblyToolTile(id: "tool:" + item.0, title: item.1, icon: item.2, size: frame.size, env: env), frame))
        }
        var y: CGFloat = 42 + 3 * 68
        var wide: [(String, String, HUDIcon)] = [("reset", L10n.text("Reset all", "全部重置"), .reset)]
        if controller.document?.isVideo == true { wide.insert(("trim", MediaAssemblyTool.trim.title, .timer), at: 0) }
        for item in wide {
            let frame = CGRect(x: 0, y: y, width: Self.panelFrame.width, height: 36)
            tiles.append((MediaAssemblyToolTile(id: "tool:" + item.0, title: item.1, icon: item.2, size: frame.size, env: env), frame))
            y += 42
        }
        for (tile, frame) in tiles {
            tile.isEnabled = enabled
            let name = String(tile.id.dropFirst(5))
            tile.onActivate = { [weak self] in self?.toolTile(name) }
            HUDMotion.perform { tile.layer.frame = frame; panel.addSublayer(tile.layer) }
            page.add(tile); page.stagger(tile.layer)
            if name == "mirror" {
                tile.isOn = controller.adjustments.mirrored
                binders.append { [weak self, weak tile] in if let self { tile?.isOn = self.controller.adjustments.mirrored } }
            }
        }
    }

    /// Back, the tool's name and (except Crop) an icon-only reset (spec §3.10 `.thd`).
    private func buildToolHeader(_ tool: MediaAssemblyTool, _ page: HUDDesignPage, _ panel: CALayer, enabled: Bool) {
        let back = HUDIconButton(id: "toolBack", label: L10n.text("Back", "返回"), icon: .back, style: .back, env: env)
        back.onActivate = { [weak self] in self?.closeTool() }
        let resettable = tool != .crop
        let name = HUDTextLayer(tool.title, style: HUDTextStyle(size: 13, weight: .bold, color: HUDColor.titleText, tracking: 0.06, uppercase: true))
        HUDMotion.perform {
            back.layer.frame = CGRect(x: 0, y: 42, width: 30, height: 30)
            name.lineFrame = CGRect(x: 38, y: 50.5, width: Self.panelFrame.width - 38 - (resettable ? 54 : 0), height: 13)
            panel.addSublayer(back.layer); panel.addSublayer(name)
        }
        back.isEnabled = enabled
        page.add(back)
        guard resettable else { return }
        let reset = HUDButton(id: "toolReset", title: "", icon: .reset, kind: .secondary, size: .small, env: env)
        reset.label = L10n.text("Reset", "重置")
        reset.place(at: CGPoint(x: Self.panelFrame.width - reset.preferredWidth, y: 44))
        reset.isEnabled = enabled
        reset.onActivate = { [weak self] in self?.resetTool(tool) }
        HUDMotion.perform { panel.addSublayer(reset.layer) }
        page.add(reset)
    }

    private func buildCropPage(_ page: HUDDesignPage, _ panel: CALayer, enabled: Bool) {
        buildToolHeader(.crop, page, panel, enabled: enabled)
        let reset = HUDButton(id: "cropReset", title: L10n.text("Reset crop", "重置裁剪"), icon: .reset, kind: .secondary, env: env)
        reset.place(at: CGPoint(x: 0, y: 78), width: Self.panelFrame.width)
        reset.onActivate = { [weak self] in self?.controller.updateAdjustments { $0.crop = MediaAssemblyCrop() } }
        let done = HUDButton(id: "cropDone", title: L10n.text("Done", "完成"), icon: .check, kind: .primary, env: env)
        done.place(at: CGPoint(x: 0, y: 116), width: Self.panelFrame.width)
        done.onActivate = { [weak self] in self?.closeTool() }
        for button in [reset, done] {
            button.isEnabled = enabled
            HUDMotion.perform { panel.addSublayer(button.layer) }
            page.add(button); page.stagger(button.layer)
        }
    }

    private func buildSliderPage(_ tool: MediaAssemblyTool, _ page: HUDDesignPage, _ panel: CALayer, enabled: Bool) {
        buildToolHeader(tool, page, panel, enabled: enabled)
        let scroll = page.scrollArea(id: "media.tool." + tool.rawValue, frame: CGRect(x: 0, y: 78, width: Self.panelFrame.width, height: 330), in: panel)
        let parameters = MediaAssemblyParameter.parameters(for: tool, duration: duration)
        for (index, parameter) in parameters.enumerated() {
            let row = MediaAssemblyParameterRow(parameter, value: parameter.read(controller.adjustments), width: Self.panelFrame.width - 6, env: env)
            HUDMotion.perform { row.layer.frame.origin = CGPoint(x: 0, y: CGFloat(index) * (MediaAssemblyParameterRow.height + 2)); scroll.content.addSublayer(row.layer) }
            hostBubble(row.slider)
            row.slider.isEnabled = enabled
            row.slider.onChange = { [weak self, weak row] value, final in if let self, let row { self.parameterChanged(row, value: value, final: final) } }
            page.add(row.slider); page.stagger(row.layer)
            parameterRows.append(row)
        }
        scroll.setContentHeight(CGFloat(parameters.count) * (MediaAssemblyParameterRow.height + 2) - 2)
        page.restoreOffset(of: scroll)
        binders.append { [weak self] in
            guard let self else { return }
            for row in self.parameterRows { row.setValue(row.parameter.read(self.controller.adjustments)) }
        }
    }

    private func buildFilters(_ page: HUDDesignPage, _ panel: CALayer, enabled: Bool) {
        let scroll = page.scrollArea(id: "media.filters", frame: CGRect(x: 0, y: 42, width: Self.panelFrame.width, height: 366), in: panel)
        let size = CGSize(width: 69, height: 72)
        var cells: [(MediaAssemblyFilter, MediaAssemblyPresetCell)] = []
        for (index, filter) in MediaAssemblyFilter.allCases.enumerated() {
            let cell = MediaAssemblyPresetCell(id: "filter:" + filter.rawValue, label: filter.title, title: filter.title,
                                               image: MediaAssemblyAssetCatalog.filterThumbnail(filter), size: size, env: env)
            cell.isSelected = controller.adjustments.filter == filter
            cell.isEnabled = enabled
            cell.onActivate = { [weak self] in self?.selectFilter(filter) }
            HUDMotion.perform {
                cell.layer.frame.origin = CGPoint(x: 3 + CGFloat(index % 2) * (size.width + 6), y: 3 + CGFloat(index / 2) * (size.height + 6))
                scroll.content.addSublayer(cell.layer)
            }
            page.add(cell); page.stagger(cell.layer)
            cells.append((filter, cell))
        }
        let rows = (cells.count + 1) / 2
        scroll.setContentHeight(6 + CGFloat(rows) * (size.height + 6) - 6)
        page.restoreOffset(of: scroll)
        binders.append { [weak self] in
            guard let self else { return }
            for (filter, cell) in cells { cell.isSelected = self.controller.adjustments.filter == filter }
        }
    }

    private func buildStickers(_ page: HUDDesignPage, _ panel: CALayer, enabled: Bool) {
        let scroll = page.scrollArea(id: "media.stickers", frame: CGRect(x: 0, y: 42, width: Self.panelFrame.width, height: 366), in: panel)
        // Video exports carry no stickers: the grid stays visible but inert.
        if controller.document?.isVideo == true { HUDMotion.perform { scroll.layer.opacity = 0.35 } }
        let size = CGSize(width: 44, height: 52)
        let kinds = MediaAssemblyStickerKind.allCases
        for (index, kind) in kinds.enumerated() {
            let cell = MediaAssemblyPresetCell(id: "sticker:" + kind.rawValue, label: kind.title, title: nil,
                                               image: MediaAssemblyAssetCatalog.stickerThumbnail(kind), size: size, env: env)
            cell.isEnabled = enabled
            cell.onActivate = { [weak self] in self?.addSticker(kind) }
            HUDMotion.perform {
                cell.layer.frame.origin = CGPoint(x: 3 + CGFloat(index % 3) * (size.width + 6), y: 3 + CGFloat(index / 3) * (size.height + 6))
                scroll.content.addSublayer(cell.layer)
            }
            page.add(cell); page.stagger(cell.layer)
        }
        scroll.setContentHeight(6 + CGFloat((kinds.count + 2) / 3) * (size.height + 6) - 6)
        page.restoreOffset(of: scroll)
    }

    // MARK: Panel actions

    private func selectTab(_ next: Tab, direction: CGFloat) {
        guard next != tab else { return }
        quietly { leaveTool() }
        tab = next
        navigate(dx: direction)
    }

    private func toolTile(_ name: String) {
        guard isEditable else { return }
        switch name {
        case "rotate":
            animateNextLayout = true
            controller.updateAdjustments { $0.rotationQuarterTurns = ($0.rotationQuarterTurns + 1) % 4 }
            animateNextLayout = false
        case "mirror":
            animateNextLayout = true
            controller.updateAdjustments { $0.mirrored.toggle() }
            animateNextLayout = false
        case "reset": confirmReset()
        default: if let next = MediaAssemblyTool(rawValue: name) { openTool(next) }
        }
    }

    /// Crop, Adjust, Curves, Levels and Trim open forward (`subnav(1)`).
    private func openTool(_ next: MediaAssemblyTool) {
        guard isEditable, tool == nil else { return }
        if next == .crop { quietly { controller.setCropPreview(true) } }
        tool = next
        navigate(dx: 1)
    }

    /// Back / Done / Esc return to the tiles (`subnav(−1)`).
    func closeTool() {
        guard tool != nil else { return }
        quietly { leaveTool() }
        navigate(dx: -1)
    }

    private func leaveTool() {
        if tool == .crop { controller.setCropPreview(false) }
        tool = nil
    }

    private func resetTool(_ tool: MediaAssemblyTool) {
        guard isEditable else { return }
        let parameters = MediaAssemblyParameter.parameters(for: tool, duration: duration)
        if tool == .trim, let document = controller.document {
            controller.updateTrim(start: 0, end: document.duration, scrubTo: 0); return
        }
        var value = controller.adjustments
        for parameter in parameters { parameter.write(&value, parameter.defaultValue) }
        controller.updateAdjustments(value)
    }

    private func parameterChanged(_ row: MediaAssemblyParameterRow, value: Double, final: Bool) {
        guard isEditable else { return }
        var next = controller.adjustments
        row.parameter.write(&next, value)
        row.showDragValue(row.parameter.read(next))
        if tool == .trim, let document = controller.document {
            let start = next.trimStart, end = next.trimEnd ?? document.duration
            if start != controller.adjustments.trimStart || end != (controller.adjustments.trimEnd ?? document.duration) {
                controller.updateTrim(start: start, end: end, scrubTo: row.parameter.id == "trimStart" ? start : end)
            }
        } else if next != controller.adjustments {
            controller.updateAdjustments(next)
        }
        if final { row.setValue(row.parameter.read(controller.adjustments)) }
    }

    /// Double-clicking a tool slider resets that value (spec §3.10).
    override func pointerDown(at point: CGPoint, event: NSEvent? = nil) -> Bool {
        if let event, event.clickCount == 2, isActive, !isMenuOpen, !isPopoverOpen, !isTransitioning,
           let row = parameterRows.first(where: { hits($0.slider, point) }) {
            parameterChanged(row, value: row.parameter.defaultValue, final: true)
            return true
        }
        return super.pointerDown(at: point, event: event)
    }

    private func hits(_ control: HUDControl, _ point: CGPoint) -> Bool {
        control.isEnabled && control.layer.isVisible(in: root) && control.layer.isUnclipped(point, in: root)
            && control.contains(control.layer.convert(point, from: root))
    }

    private func selectFilter(_ filter: MediaAssemblyFilter) {
        guard isEditable else { return }
        controller.updateAdjustments { $0.filter = filter }
    }

    /// A sticker joins at the centre (.25 of the image, unrotated) and is selected.
    func addSticker(_ kind: MediaAssemblyStickerKind) {
        guard isEditable, controller.document?.isVideo == false else { return }
        guard controller.adjustments.stickers.count < 16 else { toast(icon: .x, "Sticker limit", "最多 16 个贴纸"); return }
        let item = MediaAssemblySticker(kind: kind, x: 0.5, y: 0.5, size: 0.25, rotation: 0)
        selectedStickerID = item.id
        controller.updateAdjustments { $0.stickers.append(item) }
    }

    private func confirmReset() {
        confirm(HUDConfirmRequest(id: "mediaReset", title: L10n.text("Reset all edits?", "重置所有编辑？"),
                                  message: L10n.text("Filters, adjustments, crop and stickers.", "滤镜、调整、裁剪与贴纸。"),
                                  confirmTitle: L10n.text("Reset", "重置"), icon: .reset)) { [weak self] in
            guard let self, self.isEditable else { return }
            self.selectedStickerID = nil
            self.controller.reset()
            self.toast(icon: .reset, "Edits reset", "已重置")
        }
    }

    // MARK: Source: open, close, drops

    private func openSourceMenu(anchor: HUDControl) {
        guard !isExporting else { return }
        let items = [HUDMenu.Item(id: "finder", title: L10n.text("Choose in Finder", "从 Finder 选择")),
                     HUDMenu.Item(id: "shelf", title: L10n.text("Choose from Shelf", "从暂存架选择"))]
        openMenu(id: "open", items: items, anchor: anchor) { [weak self, weak anchor] item in
            guard let self else { return }
            if item.id == "finder" { self.chooseFile() } else if let anchor { self.openShelfMenu(anchor: anchor) }
        }
    }

    /// The shelf's images and videos; files that went missing are listed disabled.
    private func openShelfMenu(anchor: HUDControl) {
        let choices = (shelfChoices?() ?? []).filter(\.isSupported)
        let items = choices.isEmpty ? [HUDMenu.Item(id: "empty", title: L10n.text("Shelf is empty", "暂存架为空"), enabled: false)]
            : choices.map { HUDMenu.Item(id: $0.id.uuidString, title: $0.title, enabled: $0.isAvailable) }
        openMenu(id: "shelf", items: items, anchor: anchor) { [weak self] item in
            guard let self, let id = UUID(uuidString: item.id), !self.isExporting else { return }
            do {
                if let access = try self.shelfAccess?(id) { self.controller.importURL(access.url, release: { access.close() }) }
            } catch { self.controller.report(error) }
        }
    }

    private func chooseFile() {
        guard let window = context.host?.window else { return }
        let picker = NSOpenPanel(); picker.canChooseDirectories = false; picker.allowsMultipleSelection = false
        picker.allowedFileTypes = NotesMediaFactory.supportedFileExtensions
        panel = picker
        picker.beginSheetModal(for: window) { [weak self, weak picker] response in
            guard let self, let picker, self.panel === picker else { return }
            self.panel = nil
            if self.isPresented, response == .OK, let url = picker.url { self.importFiles([url]) }
        }
    }

    /// Drops and the Finder panel: the first file replaces the source.
    func importFiles(_ urls: [URL]) {
        guard !isExporting, let url = urls.first else { return }
        closePopups()
        controller.importURL(url)
    }

    /// Closing always asks first; with edits it warns that they are discarded.
    /// A module switch or HUD close dismisses the bar and keeps the session.
    private func confirmClose() {
        guard let document = controller.document, !isExporting, !closing else { return }
        let edited = controller.adjustments != MediaAssemblyAdjustments()
        confirm(HUDConfirmRequest(id: "mediaClose", title: L10n.text("Close this file?", "关闭此文件？"),
                                  message: edited ? L10n.text("Unexported edits are discarded.", "未导出的编辑将被丢弃。") : document.name,
                                  confirmTitle: L10n.text("Close", "关闭"), danger: edited, icon: .x)) { [weak self] in
            guard let self, self.controller.document?.id == document.id else { return }
            self.closeSource()
        }
    }

    /// The picture fades (.16 s), then the empty stage shows (spec §3.10 Close).
    private func closeSource() {
        guard controller.document != nil, !isExporting, !closing else { return }
        closePopups()
        closing = true; drag = nil
        stage.setContentOpacity(0, animated: true)
        rerender()
        let work = DispatchWorkItem { [weak self] in self?.finishClose() }
        closeWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + (env.reducesMotion ? 0 : 0.16), execute: work)
    }

    private func finishClose() {
        closeWork = nil; closing = false
        if !controller.closeDocument(), renderedLanguage != nil { rerender() }
    }

    // MARK: Export

    private func exportPressed(anchor: HUDControl) {
        if controller.isExporting { controller.cancelExport(); return }
        guard let document = controller.document, !controller.isBusy, !isExporting else { return }
        var items = [HUDMenu.Item(id: "saveAs", title: document.isAnimated ? L10n.text("Save first frame as…", "将首帧另存为…")
                                                                         : L10n.text("Save As…", "另存为…"))]
        if document.reference.kind != .gif { items.append(HUDMenu.Item(id: "overwrite", title: L10n.text("Overwrite original", "覆盖原文件"))) }
        openMenu(id: "export", items: items, anchor: anchor) { [weak self] item in
            guard let self, self.controller.document?.id == document.id else { return }
            if item.id == "saveAs" { self.saveAs(document) } else { self.confirmOverwrite(document) }
        }
    }

    private func saveAs(_ document: MediaAssemblyDocument) {
        guard let window = context.host?.window else { return }
        let picker = NSSavePanel(); picker.directoryURL = document.sourceURL.deletingLastPathComponent()
        picker.nameFieldStringValue = document.suggestedFilename
        picker.allowedFileTypes = controller.exportExtensions()
        picker.canCreateDirectories = true
        panel = picker
        picker.beginSheetModal(for: window) { [weak self, weak picker] response in
            guard let self, let picker, self.panel === picker else { return }
            self.panel = nil
            // NSSavePanel already asked before replacing an existing file.
            if response == .OK, let url = picker.url {
                self.startExport(to: url, overwrite: FileManager.default.fileExists(atPath: url.path), document: document, original: false)
            }
        }
    }

    private func confirmOverwrite(_ document: MediaAssemblyDocument) {
        confirm(HUDConfirmRequest(id: "mediaOverwrite", title: L10n.text("Replace the original file?", "覆盖原文件？"),
                                  message: document.name, confirmTitle: L10n.text("Overwrite", "覆盖"), danger: true, icon: .export)) { [weak self] in
            self?.startExport(to: document.sourceURL, overwrite: true, document: document, original: true)
        }
    }

    private func startExport(to url: URL, overwrite: Bool, document: MediaAssemblyDocument, original: Bool) {
        guard isPresented, !controller.isBusy, !isExporting, controller.document?.id == document.id else { return }
        exportShown = 0; exportOutcome = nil
        let name = url.lastPathComponent
        controller.export(to: url, overwrite: overwrite, expectedDocumentID: document.id) { [weak self] result in
            self?.exportFinished(result, name: name, original: original)
        }
        if !controller.isExporting { exportShown = nil }
        startExportTicker()
    }

    private func exportFinished(_ result: Result<URL, Error>, name: String, original: Bool) {
        switch result {
        case .success(let url):
            if url.standardizedFileURL.resolvingSymlinksInPath() == controller.document?.sourceURL.standardizedFileURL.resolvingSymlinksInPath() {
                reloadingAfterOverwrite = true
            }
            exportOutcome = original ? .overwritten : .saved(name)
            if exportShown == nil || !isActive { finishExportDisplay() } else { startExportTicker() }
        case .failure:
            exportShown = nil; exportOutcome = nil; stopExportTicker()
            if renderedLanguage != nil { rerender() }
        }
    }

    /// The gauge eases toward the real progress on the demo's 90 ms cadence,
    /// so even an instant export reads `EXPORTING n%` before the toast.
    private func startExportTicker() {
        guard exportTimer == nil, isActive, exportShown != nil else { return }
        let timer = Timer(timeInterval: 0.09, repeats: true) { [weak self] _ in self?.exportTick() }
        RunLoop.main.add(timer, forMode: .common)
        exportTimer = timer
    }

    private func stopExportTicker() { exportTimer?.invalidate(); exportTimer = nil }

    private func exportTick() {
        guard var shown = exportShown else { stopExportTicker(); return }
        let target = controller.isExporting ? controller.progress : (exportOutcome != nil ? 1 : shown)
        if env.reducesMotion { shown = max(shown, target) } else if target > shown { shown = min(target, shown + max(0.07, (target - shown) * 0.35)) }
        exportShown = shown
        header.setCounter(counterText)
        stage.setExport(shown, title: L10n.text("Exporting", "正在导出"))
        if !controller.isExporting, exportOutcome != nil, shown >= 1 { finishExportDisplay() }
    }

    private func finishExportDisplay() {
        stopExportTicker()
        let outcome = exportOutcome
        exportShown = nil; exportOutcome = nil
        if renderedLanguage != nil { rerender() }
        guard isActive, let outcome else { return }
        switch outcome {
        case .saved(let name): toast(icon: .export, "Exported", "已导出", detail: name)
        case .overwritten: toast(icon: .export, "Exported", "已覆盖原文件")
        }
    }

    // MARK: Stage

    private var targetGeometry: MediaAssemblyGeometry { MediaAssemblyGeometry(controller.adjustments, cropPreview: controller.isCropPreview) }
    private var duration: Double { controller.document?.duration ?? 0 }

    /// The size of what the stage shows, before the transition transform.
    private var shownSize: CGSize {
        if let image = stage.image { return CGSize(width: image.width, height: image.height) }
        guard let document = controller.document else { return MediaAssemblyStage.size }
        let crop = shownGeometry.crop
        let size = CGSize(width: document.pixelSize.width * crop.width, height: document.pixelSize.height * crop.height)
        return shownGeometry.quarterTurns % 2 == 0 ? size : CGSize(width: size.height, height: size.width)
    }

    /// The picture's frame on the stage, as the edits will show it.
    private var layoutSize: CGSize {
        let size = shownSize
        return MediaAssemblyPictureTransform.delta(from: shownGeometry, to: targetGeometry).turns % 2 == 0
            ? size : CGSize(width: size.height, height: size.width)
    }
    var imageRect: CGRect { viewport.imageRect(size: layoutSize, in: MediaAssemblyStage.bounds) }

    /// Stickers live in the output (cropped) frame; while the crop tool shows
    /// the whole picture they sit inside the crop box.
    private func stickerBase(_ image: CGRect) -> CGRect {
        guard shownGeometry.crop == MediaAssemblyCrop(), controller.adjustments.crop != MediaAssemblyCrop() else { return image }
        let crop = displayCropRect
        return CGRect(x: image.minX + crop.minX * image.width, y: image.minY + crop.minY * image.height,
                      width: crop.width * image.width, height: crop.height * image.height)
    }
    var displayCropRect: CGRect {
        let p = controller.adjustments
        return MediaAssemblyViewport.displayCrop(p.crop, quarterTurns: p.rotationQuarterTurns, mirrored: p.mirrored)
    }
    private var selectedSticker: MediaAssemblySticker? { controller.adjustments.stickers.first { $0.id == selectedStickerID } }

    /// Takes a newly rendered preview, unless a rotate / mirror transition is
    /// still turning the old one: then it waits for the transition's end.
    private func takePreview() {
        guard let preview = controller.preview, preview !== stage.image else { return }
        let wait = geometryAnimationEnd - CACurrentMediaTime()
        if wait > 0.005 {
            guard pendingPreview == nil else { return }
            let work = DispatchWorkItem { [weak self] in self?.pendingPreview = nil; self?.layoutStage() }
            pendingPreview = work
            DispatchQueue.main.asyncAfter(deadline: .now() + wait, execute: work)
            return
        }
        let sameLook = shownGeometry == targetGeometry
        shownGeometry = targetGeometry
        clearOnBuild = false
        stage.setImage(preview, crossfade: sameLook && !revealOnPreview)
        if revealOnPreview { revealOnPreview = false; stage.setContentOpacity(0, animated: false); stage.setContentOpacity(1, animated: true) }
    }

    /// Every stage layer from the current state, in place.
    private func layoutStage() {
        guard renderedLanguage != nil else { return }
        let document = controller.document
        stage.setEmpty(document == nil && !closing, subtitle: L10n.text("Open an image or video", "打开图片或视频"), subtitleLanguage: nil)
        relabelStageControls()
        guard let document else {
            stage.setStickers([], base: .zero); stage.setSelection(nil, base: .zero); stage.setCrop(nil, image: .zero)
            stage.setExport(nil, title: ""); return
        }
        if isPresented { takePreview() }
        let delta = MediaAssemblyPictureTransform.delta(from: shownGeometry, to: targetGeometry)
        let transform = stage.transform.nearest(turns: delta.turns, reflected: delta.reflected)
        let animated = animateNextLayout && transform != stage.transform && !env.reducesMotion
        let rect = imageRect
        stage.layoutPicture(in: rect, transform: transform, animated: animated)
        if animated { geometryAnimationEnd = CACurrentMediaTime() + 0.25 }
        let base = stickerBase(rect)
        let adjustments = controller.adjustments
        stage.setStickers(adjustments.stickers, base: base)
        stage.setSelection(tool == .crop ? nil : selectedSticker, base: base)
        stage.setCrop(tool == .crop ? displayCropRect : nil, image: rect)
        stage.setChips(source: document.name, filter: adjustments.filter == .none ? nil : adjustments.filter.title,
                       stickers: adjustments.stickers.count)
        stage.setZoom(viewport.zoom)
        stage.setPlayer(controller.player, playing: controller.isPlaying)
        stage.setPlayback(visible: document.isVideo && !isExporting && tool != .trim, playing: controller.isPlaying,
                          time: controller.currentTime, duration: document.duration)
        stage.setExport(isExporting ? (exportShown ?? controller.progress) : nil, title: L10n.text("Exporting", "正在导出"))
    }

    private func stagePoint(_ point: CGPoint) -> CGPoint { CGPoint(x: point.x - Self.stageOrigin.x, y: point.y - Self.stageOrigin.y) }

    private func zoomStep(_ factor: CGFloat) {
        guard controller.document != nil else { return }
        viewport.zoom(by: factor, size: layoutSize, in: MediaAssemblyStage.bounds)
        layoutStage()
    }

    @discardableResult func zoom(by factor: CGFloat, at point: CGPoint? = nil) -> Bool {
        guard controller.document != nil else { return false }
        if let point { viewport.magnify(by: factor, at: point, size: layoutSize, in: MediaAssemblyStage.bounds) }
        else { viewport.zoom(by: factor, size: layoutSize, in: MediaAssemblyStage.bounds) }
        layoutStage(); return true
    }

    private func resetZoom() { viewport.reset(); layoutStage() }

    // MARK: Stage gestures (box px)

    override func handlePointerDown(at point: CGPoint, event: NSEvent?) -> Bool {
        guard hitBounds.contains(point) else { return false }
        let local = stagePoint(point)
        guard MediaAssemblyStage.bounds.contains(local), controller.document != nil, !closing else { return true }
        beginDrag(at: local)
        if drag != nil { context.onLockParallax?() }
        return true
    }

    private func beginDrag(at local: CGPoint) {
        let rect = imageRect, base = stickerBase(rect)
        let editable = isEditable && controller.document?.isVideo == false
        if editable, tool != .crop, let item = selectedSticker,
           let handle = MediaAssemblyViewport.handle(at: local, sticker: MediaAssemblyViewport.stickerRect(item, in: base), rotation: item.rotation) {
            switch handle {
            case .delete: removeSticker(item.id)
            case .rotate: drag = .rotate(item.id)
            case .corner: drag = .scale(item.id)
            }
            return
        }
        if isEditable, tool == .crop,
           let corner = MediaAssemblyViewport.cropHandleOrigins(displayCropRect, image: rect, stage: MediaAssemblyStage.size)
            .firstIndex(where: { CGRect(origin: $0, size: CGSize(width: 14, height: 14)).insetBy(dx: -5, dy: -5).contains(local) }) {
            drag = .crop(corner); return
        }
        if editable, let item = controller.adjustments.stickers.reversed().first(where: { item in
            let frame = MediaAssemblyViewport.stickerRect(item, in: base)
            return frame.contains(MediaAssemblyViewport.unrotate(local, around: CGPoint(x: frame.midX, y: frame.midY), degrees: item.rotation))
        }) {
            select(item.id)
            drag = .move(item.id, start: local, original: item)
            return
        }
        if selectedStickerID != nil { select(nil) }
        if tool != .crop { drag = .pan(local) }
    }

    override func handlePointerDragged(to point: CGPoint) {
        guard let drag else { return }
        let local = stagePoint(point), rect = imageRect, base = stickerBase(rect)
        switch drag {
        case .pan(let previous):
            viewport.move(by: CGPoint(x: local.x - previous.x, y: local.y - previous.y), size: layoutSize, in: MediaAssemblyStage.bounds)
            self.drag = .pan(local)
            layoutStage()
        case .move(let id, let start, var item):
            item.x = min(1, max(0, item.x + Double((local.x - start.x) / max(1, base.width))))
            item.y = min(1, max(0, item.y + Double((local.y - start.y) / max(1, base.height))))
            replaceSticker(id, item)
        case .scale(let id):
            guard var item = sticker(id) else { return }
            let centre = CGPoint(x: base.minX + base.width * item.x, y: base.minY + base.height * item.y)
            item.size = min(1, max(0.05, Double(hypot(local.x - centre.x, local.y - centre.y) * 2.squareRoot() / max(1, min(base.width, base.height)))))
            replaceSticker(id, item)
        case .rotate(let id):
            guard var item = sticker(id) else { return }
            let centre = CGPoint(x: base.minX + base.width * item.x, y: base.minY + base.height * item.y)
            item.rotation = Double(atan2(local.y - centre.y, local.x - centre.x) * 180 / .pi + 90).rounded()
            replaceSticker(id, item)
        case .crop(let corner):
            let point = CGPoint(x: (local.x - rect.minX) / max(1, rect.width), y: (local.y - rect.minY) / max(1, rect.height))
            let display = MediaAssemblyViewport.dragCrop(displayCropRect, corner: corner, to: point)
            controller.updateAdjustments { p in
                p.crop = MediaAssemblyViewport.sourceCrop(display, quarterTurns: p.rotationQuarterTurns, mirrored: p.mirrored)
            }
        }
    }

    override func handlePointerUp(at point: CGPoint) {
        guard drag != nil else { return }
        drag = nil
        context.onInputStateChange?()
    }

    override func handlePointerMoved(at point: CGPoint?) {
        guard drag == nil, tool != .crop, let point, let item = selectedSticker else { stage.setHovered(nil); return }
        let base = stickerBase(imageRect)
        stage.setHovered(MediaAssemblyViewport.handle(at: stagePoint(point), sticker: MediaAssemblyViewport.stickerRect(item, in: base),
                                                      rotation: item.rotation))
    }

    /// Mouse wheel: × 1.1 per notch about the pointer (1–20); trackpad
    /// scrolling pans, with ⌘ or ⌥ it zooms.
    override func handleScroll(at point: CGPoint, delta: CGPoint, event: NSEvent?) -> Bool {
        let local = stagePoint(point)
        guard MediaAssemblyStage.bounds.contains(local), controller.document != nil else { return false }
        let precise = event?.hasPreciseScrollingDeltas ?? false
        let modifier = !(event?.modifierFlags.intersection([.command, .option]).isEmpty ?? true)
        if precise, !modifier {
            viewport.move(by: CGPoint(x: -delta.x, y: -delta.y), size: layoutSize, in: MediaAssemblyStage.bounds)
            layoutStage(); return true
        }
        guard delta.y != 0 else { return true }
        let factor: CGFloat = precise ? exp(-delta.y * 0.01) : (delta.y < 0 ? 1.1 : 1 / 1.1)
        return zoom(by: factor, at: local)
    }

    override func handleMagnify(at point: CGPoint, magnification: CGFloat) -> Bool {
        let local = stagePoint(point)
        guard MediaAssemblyStage.bounds.contains(local) else { return false }
        return zoom(by: 1 + magnification, at: local)
    }

    // MARK: Stickers

    private func sticker(_ id: UUID) -> MediaAssemblySticker? { controller.adjustments.stickers.first { $0.id == id } }

    private func select(_ id: UUID?) {
        selectedStickerID = id
        layoutStage()
    }

    private func replaceSticker(_ id: UUID, _ item: MediaAssemblySticker) {
        controller.updateAdjustments { p in
            if let index = p.stickers.firstIndex(where: { $0.id == id }) { p.stickers[index] = item }
        }
    }

    private func removeSticker(_ id: UUID) {
        guard isEditable else { return }
        if selectedStickerID == id { selectedStickerID = nil }
        controller.updateAdjustments { $0.stickers.removeAll { $0.id == id } }
    }

    /// Keyboard / accessibility nudges of the selected sticker.
    @discardableResult func adjustSelectedSticker(dx: Double = 0, dy: Double = 0, size: Double = 0, rotation: Double = 0) -> Bool {
        guard isEditable, var item = selectedSticker else { return false }
        item.x = min(1, max(0, item.x + dx)); item.y = min(1, max(0, item.y + dy))
        item.size = min(1, max(0.05, item.size + size))
        item.rotation = (item.rotation + rotation).truncatingRemainder(dividingBy: 360)
        replaceSticker(item.id, item)
        return true
    }

    // MARK: Keyboard (spec §3.10)

    override func handleKey(_ event: NSEvent) -> Bool {
        let flags = event.modifierFlags.intersection([.command, .control, .option, .shift])
        guard !flags.contains(.command), !flags.contains(.control) else { return false }
        if event.keyCode == 53 {
            if tool != nil { closeTool(); return true }
            if selectedStickerID != nil { select(nil); return true }
            return false
        }
        guard let document = controller.document else { return false }
        let option = flags.contains(.option), shift = flags.contains(.shift), step = shift ? 0.05 : 0.01
        if selectedStickerID != nil {
            switch event.keyCode {
            case 51, 117: if let id = selectedStickerID { removeSticker(id) }; return true
            case 123: return option ? adjustSelectedSticker(rotation: shift ? -15 : -3) : adjustSelectedSticker(dx: -step)
            case 124: return option ? adjustSelectedSticker(rotation: shift ? 15 : 3) : adjustSelectedSticker(dx: step)
            case 125: return option ? adjustSelectedSticker(size: -step) : adjustSelectedSticker(dy: step)
            case 126: return option ? adjustSelectedSticker(size: step) : adjustSelectedSticker(dy: -step)
            default: break
            }
        }
        switch event.charactersIgnoringModifiers {
        case "+", "=": zoomStep(1.25); return true
        case "-": zoomStep(0.8); return true
        default: break
        }
        guard document.isVideo, flags.isEmpty, isEditable, tool != .crop else { return false }
        switch event.keyCode {
        case 49: controller.togglePlayback(); return true
        case 123, 124: controller.seek(to: controller.currentTime + (event.keyCode == 123 ? -5 : 5)); return true
        default: return false
        }
    }

    // MARK: Accessibility

    /// Activates a visible control by id and settles any page change (scripted checks).
    func perform(_ id: String) {
        settleSwap()
        performAccessibilityAction(id)
        settleSwap()
    }

    /// The selected sticker and the crop box are adjustable without a pointer.
    override var accessibilityItems: [HUDAccessibilityItem] {
        var items = super.accessibilityItems
        guard !items.isEmpty, isEditable else { return items }
        let offset = Self.stageOrigin
        if tool == .crop {
            let crop = displayCropRect, frame = imageRect.offsetBy(dx: offset.x, dy: offset.y)
            let edges = [("X₁", crop.minX), ("Y₁", crop.minY), ("X₂", crop.maxX), ("Y₂", crop.maxY)]
            for (index, edge) in edges.enumerated() {
                items.append(HUDAccessibilityItem(id: "crop.edge\(index)", label: L10n.text("Crop", "裁剪") + " " + edge.0,
                    role: .slider(value: Double(edge.1), minimum: 0, maximum: 1, step: 0.01, description: String(format: "%.2f", edge.1)), rect: frame))
            }
        } else if let item = selectedSticker {
            let frame = MediaAssemblyViewport.stickerRect(item, in: stickerBase(imageRect)).offsetBy(dx: offset.x, dy: offset.y)
            let sliders: [(String, String, Double, Double, Double, Double)] = [
                ("x", L10n.text("Sticker", "贴纸") + " X", item.x, 0, 1, 0.01), ("y", L10n.text("Sticker", "贴纸") + " Y", item.y, 0, 1, 0.01),
                ("size", L10n.text("Size", "大小"), item.size, 0.05, 1, 0.01), ("rotation", L10n.text("Rotate", "旋转"), item.rotation, -360, 360, 3)]
            for slider in sliders {
                items.append(HUDAccessibilityItem(id: "sticker." + slider.0, label: slider.1,
                    role: .slider(value: slider.2, minimum: slider.3, maximum: slider.4, step: slider.5,
                                  description: String(format: slider.0 == "rotation" ? "%.0f°" : "%.2f", slider.2)), rect: frame))
            }
        }
        return items
    }

    override func adjustAccessibilitySlider(_ id: String, value: Double) {
        guard value.isFinite else { return }
        if id.hasPrefix("sticker."), let item = selectedSticker {
            switch id {
            case "sticker.x": adjustSelectedSticker(dx: value - item.x)
            case "sticker.y": adjustSelectedSticker(dy: value - item.y)
            case "sticker.size": adjustSelectedSticker(size: value - item.size)
            default: adjustSelectedSticker(rotation: value - item.rotation)
            }
            return
        }
        if id.hasPrefix("crop.edge"), let index = Int(id.dropFirst(9)), isEditable {
            var crop = displayCropRect
            let v = CGFloat(min(1, max(0, value)))
            switch index {
            case 0: crop = CGRect(x: min(v, crop.maxX - 0.05), y: crop.minY, width: crop.maxX - min(v, crop.maxX - 0.05), height: crop.height)
            case 1: crop = CGRect(x: crop.minX, y: min(v, crop.maxY - 0.05), width: crop.width, height: crop.maxY - min(v, crop.maxY - 0.05))
            case 2: crop.size.width = max(0.05, v - crop.minX)
            default: crop.size.height = max(0.05, v - crop.minY)
            }
            controller.updateAdjustments { p in
                p.crop = MediaAssemblyViewport.sourceCrop(crop, quarterTurns: p.rotationQuarterTurns, mirrored: p.mirrored)
            }
            return
        }
        super.adjustAccessibilitySlider(id, value: value)
    }

    // MARK: Testing

    var stageForTesting: MediaAssemblyStage { stage }
    var parameterRowsForTesting: [MediaAssemblyParameterRow] { parameterRows }
}
