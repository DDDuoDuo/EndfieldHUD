import AppKit

final class HUDMediaAssemblyInteraction: NSObject {
    let canvas: MediaAssemblyCanvas
    private weak var host: NSView?
    var project: ((CGRect) -> CGRect)?
    var unproject: ((CGPoint) -> CGPoint?)?
    var shelfChoices: (() -> [NotesShelfMediaChoice])?
    var shelfAccess: ((UUID) throws -> ShelfFileAccess)?
    private var active = false, presented = false
    private(set) var secondaryMenu: NotesRetainedMenu?
    private var menuOrigin = CGPoint.zero
    private var retiring: CALayer?
    private var retireWork: DispatchWorkItem?
    private var panel: NSSavePanel?
    private var buttons: [String: MediaAssemblyAXButton] = [:]
    private var seekSlider: MediaAssemblySeekSlider?
    private var stickerSliders: [MediaAssemblyStickerAXSlider] = []
    private var parameterSliders: [String:MediaAssemblyStickerAXSlider] = [:]
    var capturesPointer: Bool { secondaryMenu != nil || canvas.isDragging }
    var isPresentingPanel: Bool { panel != nil }
    init(canvas: MediaAssemblyCanvas, host: NSView) {
        self.canvas = canvas; self.host = host; super.init()
        canvas.onChange = { [weak self] in self?.layoutAccessibility() }
        canvas.onAction = { [weak self] in self?.show($0) }
    }
    deinit { retireWork?.cancel(); retiring?.removeFromSuperlayer(); secondaryMenu?.removeFromSuperview(); panel?.cancel(nil); buttons.values.forEach { $0.removeFromSuperview() }; seekSlider?.removeFromSuperview(); stickerSliders.forEach { $0.removeFromSuperview() }; parameterSliders.values.forEach { $0.removeFromSuperview() } }
    func setPresented(_ value: Bool) {
        guard presented != value else { return }; presented = value
        if !value { setActive(false) }; canvas.setPresented(value)
    }
    func setActive(_ value: Bool) {
        guard active != value else { return }; active = value
        if value { setPresented(true); layoutAccessibility() }
        else { closeMenu(animated: false); panel?.cancel(nil); panel = nil; buttons.values.forEach { $0.isHidden = true }; seekSlider?.isHidden = true; stickerSliders.forEach { $0.isHidden = true }; parameterSliders.values.forEach { $0.isHidden = true } }
    }
    func deactivate() { setActive(false); setPresented(false) }
    @discardableResult func mouseDown(at point: CGPoint, event: NSEvent) -> Bool {
        guard active else { return false }
        if let menu = secondaryMenu {
            let local = CGPoint(x: point.x - menuOrigin.x, y: point.y - menuOrigin.y)
            if menu.bounds.contains(local) { menu.activate(at: local) } else { closeMenu() }; return true
        }
        host?.window?.makeFirstResponder(host); return canvas.mouseDown(at: point)
    }
    func mouseDragged(to point: CGPoint) {
        guard active else { return }
        if secondaryMenu == nil { canvas.mouseDragged(to: point) }
    }
    func mouseUp() { canvas.mouseUp() }
    func keyDown(_ event: NSEvent) -> Bool {
        guard active else { return false }
        if event.keyCode == 53, secondaryMenu != nil { closeMenu(); return true }
        if event.keyCode == 53, canvas.activeTool != nil { canvas.perform("toolBack"); return true }
        if secondaryMenu == nil, canvas.selectedStickerID != nil {
            if event.keyCode == 51 || event.keyCode == 117 { canvas.removeSelectedSticker(); return true }
            if event.keyCode == 53 { canvas.selectSticker(nil); return true }
            let option = event.modifierFlags.contains(.option), shift = event.modifierFlags.contains(.shift)
            let step = shift ? 0.05 : 0.01
            switch event.keyCode {
            case 123: return option ? canvas.adjustSelectedSticker(rotation: -5) : canvas.adjustSelectedSticker(dx: -step)
            case 124: return option ? canvas.adjustSelectedSticker(rotation: 5) : canvas.adjustSelectedSticker(dx: step)
            case 125: return option ? canvas.adjustSelectedSticker(size: -step) : canvas.adjustSelectedSticker(dy: step)
            case 126: return option ? canvas.adjustSelectedSticker(size: step) : canvas.adjustSelectedSticker(dy: -step)
            default: break
            }
        }
        if secondaryMenu == nil, event.charactersIgnoringModifiers == "+" || event.charactersIgnoringModifiers == "=" { canvas.perform("zoomIn"); return true }
        if secondaryMenu == nil, event.charactersIgnoringModifiers == "-" { canvas.perform("zoomOut"); return true }
        if event.keyCode == 49, secondaryMenu == nil, event.modifierFlags.intersection([.command,.control,.option,.shift]).isEmpty,
           canvas.controller.document?.isVideo == true { canvas.perform("play"); return true }
        if (event.keyCode == 123 || event.keyCode == 124), secondaryMenu == nil,
           event.modifierFlags.intersection([.command,.control,.option,.shift]).isEmpty, canvas.controller.document?.isVideo == true {
            canvas.controller.seek(to: canvas.controller.currentTime + (event.keyCode == 123 ? -5 : 5)); return true
        }
        return false
    }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat, deltaX: CGFloat = 0, zoom: Bool = false) -> Bool {
        guard active else { return false }
        if let menu = secondaryMenu as? NotesShelfMediaPicker { menu.scroll(delta: delta) }
        if secondaryMenu != nil { return true }
        return canvas.scroll(at: point, deltaX: deltaX, deltaY: delta, zoom: zoom)
    }
    @discardableResult func magnify(at point: CGPoint, event: NSEvent) -> Bool {
        guard active, secondaryMenu == nil else { return false }
        return canvas.zoom(by: 1 + event.magnification, at: point)
    }
    func hitTestMenu(at point: CGPoint) -> NSView? { secondaryMenu?.hitTest(point) }
    func importFiles(_ urls: [URL]) {
        guard active, !canvas.controller.isExporting, let url = urls.first else { return }
        closeMenu(); canvas.controller.importURL(url)
    }
    func layoutAccessibility() {
        guard active, let host else { return }
        for action in canvas.actions {
            let button = buttons[action.id] ?? MediaAssemblyAXButton(frame: .zero)
            if buttons[action.id] == nil { buttons[action.id] = button; host.addSubview(button) }
            button.frame = project?(action.rect) ?? action.rect; button.title = ""; button.isBordered = false
            button.isHidden = secondaryMenu != nil; button.isEnabled = action.enabled; button.setAccessibilityLabel(action.id == "closeMedia" ? L10n.text("Close", "关闭") : action.title)
            button.onPress = { [weak self] in self?.canvas.perform(action.id) }
        }
        let ids = Set(canvas.actions.map(\.id)); for (id,button) in buttons where !ids.contains(id) { button.isHidden = true }
        if let document = canvas.controller.document, document.isVideo {
            let slider = seekSlider ?? MediaAssemblySeekSlider(frame: .zero)
            if seekSlider == nil { seekSlider = slider; slider.target = self; slider.action = #selector(seekChanged(_:)); slider.isContinuous = false; host.addSubview(slider) }
            slider.minValue = canvas.controller.adjustments.trimStart
            slider.maxValue = max(slider.minValue, (canvas.controller.adjustments.trimEnd ?? document.duration) - 0.001)
            slider.doubleValue = canvas.controller.currentTime; slider.isHidden = secondaryMenu != nil || canvas.activeTool == .trim; slider.isEnabled = !canvas.controller.isExporting
            slider.setAccessibilityLabel(L10n.text("Playback position", "播放进度")); slider.frame = project?(MediaAssemblyCanvas.seekRect) ?? MediaAssemblyCanvas.seekRect
        } else { seekSlider?.isHidden = true }
        layoutStickerAccessibility(host); layoutParameterAccessibility(host)
        positionMenu(); HUDControlHighlightLayer.requestRefresh(on: host)
    }
    private func layoutStickerAccessibility(_ host: NSView) {
        guard let item = canvas.controller.adjustments.stickers.first(where: { $0.id == canvas.selectedStickerID }), secondaryMenu == nil else {
            stickerSliders.forEach { $0.isHidden = true }; return
        }
        let labels = [L10n.text("Sticker", "贴纸") + " X", L10n.text("Sticker", "贴纸") + " Y", L10n.text("Size", "大小"), L10n.text("Rotate", "旋转")]
        let values = [item.x,item.y,item.size,item.rotation]
        if stickerSliders.isEmpty {
            for i in 0..<4 {
                let slider = MediaAssemblyStickerAXSlider(frame: .zero); slider.tag = i; host.addSubview(slider); stickerSliders.append(slider)
                slider.onValue = { [weak self] value in
                    guard let self, self.active, let s = self.canvas.controller.adjustments.stickers.first(where: { $0.id == self.canvas.selectedStickerID }) else { return }
                    switch i {
                    case 0: _ = self.canvas.adjustSelectedSticker(dx: value-s.x)
                    case 1: _ = self.canvas.adjustSelectedSticker(dy: value-s.y)
                    case 2: _ = self.canvas.adjustSelectedSticker(size: value-s.size)
                    default: _ = self.canvas.adjustSelectedSticker(rotation: value-s.rotation)
                    }
                }
            }
        }
        for (i,slider) in stickerSliders.enumerated() {
            slider.minValue = i == 3 ? -360 : i == 2 ? 0.02 : 0; slider.maxValue = i == 3 ? 360 : 1
            slider.doubleValue = values[i]; slider.increment = i == 3 ? 5 : 0.01
            slider.setAccessibilityLabel(labels[i]); slider.isHidden = false
            slider.isEnabled = !canvas.controller.isBusy && !canvas.controller.isExporting
            slider.frame = project?(MediaAssemblyViewport.stickerRect(item,in:canvas.imageRect)) ?? .zero
        }
    }
    private func layoutParameterAccessibility(_ host: NSView) {
        parameterSliders.values.forEach { $0.isHidden = true }
        guard secondaryMenu == nil else { return }
        func slider(_ id: String,_ title: String,_ range: ClosedRange<Double>,_ value: Double,_ step: Double,_ rect: CGRect,_ change: @escaping (Double) -> Void) {
            let control = parameterSliders[id] ?? MediaAssemblyStickerAXSlider(frame:.zero)
            if parameterSliders[id] == nil { parameterSliders[id] = control; host.addSubview(control) }
            control.minValue = range.lowerBound; control.maxValue = range.upperBound; control.doubleValue = value; control.increment = step
            control.setAccessibilityLabel(title); control.isHidden = false; control.isEnabled = !canvas.controller.isBusy && !canvas.controller.isExporting
            control.frame = project?(rect) ?? rect; control.onValue = change
        }
        for item in canvas.parameterControls {
            guard let p = canvas.inlineParameters.first(where: { $0.id == item.parameter.id }) else { continue }
            slider(p.id,p.title,p.range,p.value,p.step,item.rect) { [weak self] value in self?.canvas.setParameter(p.id,to:value) }
        }
        if canvas.activeTool == .trim,let doc = canvas.controller.document {
            let p = canvas.controller.adjustments,end = p.trimEnd ?? doc.duration
            slider("trimStart",L10n.text("Start","开始"),0...max(0,end-0.05),p.trimStart,0.05,canvas.trimRangeRect) { [weak self] value in self?.canvas.setTrimEndpoint(beginning:true,to:value) }
            slider("trimEnd",L10n.text("End","结束"),(p.trimStart+0.05)...doc.duration,end,0.05,canvas.trimRangeRect) { [weak self] value in self?.canvas.setTrimEndpoint(beginning:false,to:value) }
        }
        if canvas.activeTool == .crop {
            let p = canvas.controller.adjustments.crop
            let values = [p.x,p.y,p.x+p.width,p.y+p.height],titles = ["X₁","Y₁","X₂","Y₂"]
            for index in 0..<4 {
                slider("cropEdge\(index)",L10n.text("Crop","裁剪")+" "+titles[index],0...1,values[index],0.01,MediaAssemblyCanvas.previewRect) { [weak self] value in
                    guard let self else { return }; var p = self.canvas.controller.adjustments
                    let right = p.crop.x+p.crop.width,bottom = p.crop.y+p.crop.height
                    switch index {
                    case 0: p.crop.x = min(value,right-0.01); p.crop.width = right-p.crop.x
                    case 1: p.crop.y = min(value,bottom-0.01); p.crop.height = bottom-p.crop.y
                    case 2: p.crop.width = max(0.01,value-p.crop.x)
                    default: p.crop.height = max(0.01,value-p.crop.y)
                    }
                    self.canvas.controller.updateAdjustments(p)
                }
            }
        }
    }
    @objc private func seekChanged(_ slider: NSSlider) { guard active, slider.isEnabled else { return }; canvas.controller.seek(to: slider.doubleValue) }
    private func show(_ id: String) {
        guard active else { return }
        switch id {
        case "open":
            let menu = NotesMediaSourceChooser(dark: canvas.dark)
            menu.onChoose = { [weak self] shelf in self?.closeMenu(); if shelf { self?.showShelf() } else { self?.chooseFile() } }
            present(menu, anchor: "open")
        case "export": showExport()
        case "closeMedia":
            closeMenu(animated:false); panel?.cancel(nil); panel = nil
        default: break
        }
    }
    private func chooseFile() {
        guard let window = host?.window else { return }
        let picker = NSOpenPanel(); picker.canChooseDirectories = false; picker.allowsMultipleSelection = false
        picker.allowedFileTypes = NotesMediaFactory.supportedFileExtensions; panel = picker
        picker.beginSheetModal(for: window) { [weak self, weak picker] response in
            guard let self, let picker, self.panel === picker else { return }; self.panel = nil
            if self.active, response == .OK, let url = picker.url { self.canvas.controller.importURL(url) }
        }
    }
    private func showShelf() {
        let menu = NotesShelfMediaPicker(choices: shelfChoices?() ?? [], dark: canvas.dark)
        menu.onSelect = { [weak self] id in
            guard let self else { return }; self.closeMenu()
            do {
                if let access = try self.shelfAccess?(id) { self.canvas.controller.importURL(access.url, release: { access.close() }) }
            } catch { self.canvas.controller.report(error) }
        }
        present(menu, anchor: "open")
    }
    private func showExport() {
        guard !canvas.controller.isBusy, !canvas.controller.isExporting, let document = canvas.controller.document else { return }
        let animated = document.reference.kind == .gif && document.reference.frameCount > 1
        let title = animated ? L10n.text("GIF · Export first frame", "GIF · 导出首帧") : L10n.text("Export", "导出")
        var commands = [("saveAs", L10n.text("Save As…", "另存为…"))]
        if document.reference.kind != .gif { commands.append(("overwrite", L10n.text("Overwrite original", "覆盖原文件"))) }
        let menu = MediaAssemblyControlMenu(title: title, commands: commands, dark: canvas.dark)
        menu.onCommand = { [weak self] action in
            guard let self, self.active, !self.canvas.controller.isBusy, self.canvas.controller.document?.id == document.id else { return }; self.closeMenu()
            if action == "saveAs" { self.saveAs() } else { self.confirmOverwrite(document: document) }
        }; present(menu, anchor: "export")
    }
    private func saveAs() {
        guard active, !canvas.controller.isBusy, !canvas.controller.isExporting, let window = host?.window, let document = canvas.controller.document else { return }
        let picker = NSSavePanel(); picker.directoryURL = document.sourceURL.deletingLastPathComponent()
        picker.nameFieldStringValue = document.suggestedFilename
        picker.allowedFileTypes = canvas.controller.exportExtensions()
        picker.canCreateDirectories = true; panel = picker
        picker.beginSheetModal(for: window) { [weak self, weak picker] response in
            guard let self, let picker, self.panel === picker else { return }; self.panel = nil
            if self.active, response == .OK, !self.canvas.controller.isBusy, self.canvas.controller.document?.id == document.id, let url = picker.url {
                // NSSavePanel already obtains explicit replacement confirmation.
                self.canvas.controller.export(to: url, overwrite: FileManager.default.fileExists(atPath: url.path), expectedDocumentID: document.id) { _ in }
            }
        }
    }
    private func confirmOverwrite(document: MediaAssemblyDocument) {
        let menu = MediaAssemblyControlMenu(title: L10n.text("Replace the original file?", "覆盖原文件？"), commands: [
            ("cancel", L10n.text("Cancel", "取消")), ("confirm", L10n.text("Overwrite original", "覆盖原文件"))], dark: canvas.dark)
        menu.onCommand = { [weak self] action in
            guard let self else { return }; self.closeMenu()
            if action == "confirm", self.active, !self.canvas.controller.isBusy,
               self.canvas.controller.document?.id == document.id {
                self.canvas.controller.export(to: document.sourceURL, overwrite: true, expectedDocumentID: document.id) { _ in }
            }
        }; present(menu, anchor: "export")
    }
    private func present(_ menu: NotesRetainedMenu, anchor: String) {
        closeMenu(animated: false); guard let host else { return }; secondaryMenu = menu
        let button = canvas.actions.first { $0.id == anchor }?.rect ?? CGRect(x: 12,y: 331,width:66,height:28)
        let proposedY = button.maxY + 6
        menuOrigin = CGPoint(x: min(max(8,button.minX),432-menu.contentSize.width),
            y: proposedY + menu.contentSize.height <= 420 ? proposedY : max(42,button.minY-menu.contentSize.height-8))
        host.addSubview(menu); canvas.layer.addSublayer(menu.artwork); menu.artwork.zPosition = 3_000_000
        menu.hostToLocal = { [weak self] point in guard let self, let p = self.unproject?(point) ?? (self.unproject == nil ? point : nil) else { return nil }; return CGPoint(x:p.x-self.menuOrigin.x,y:p.y-self.menuOrigin.y) }
        menu.projectLocal = { [weak self] rect in guard let self else { return .zero }; let p = rect.offsetBy(dx:self.menuOrigin.x,dy:self.menuOrigin.y); return self.project?(p) ?? p }
        menu.onCancel = { [weak self] in self?.closeMenu() }; positionMenu(); menu.paint(); animate(menu.artwork, from:0,to:1)
        host.window?.makeFirstResponder(menu); layoutAccessibility()
    }
    private func positionMenu() {
        guard let menu = secondaryMenu else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        menu.frame = project?(CGRect(origin:menuOrigin,size:menu.contentSize)) ?? CGRect(origin:menuOrigin,size:menu.contentSize)
        menu.bounds = CGRect(origin:.zero,size:menu.contentSize); menu.artwork.position = menuOrigin; menu.layoutAccessibility(); CATransaction.commit()
    }
    private func closeMenu(animated: Bool = true) {
        retireWork?.cancel(); retireWork = nil; retiring?.removeFromSuperlayer(); retiring = nil
        guard let menu = secondaryMenu else { return }; secondaryMenu = nil
        if animated && !HUDRuntimeAppearance.reduceMotion {
            menu.detachInputKeepingArtwork(); retiring = menu.artwork; animate(menu.artwork,from:1,to:0)
            let work = DispatchWorkItem { [weak self] in self?.retiring?.removeFromSuperlayer(); self?.retiring = nil; self?.retireWork = nil }
            retireWork = work; DispatchQueue.main.asyncAfter(deadline:.now()+0.18,execute:work)
        } else { menu.removeFromSuperview() }
        host?.window?.makeFirstResponder(host); layoutAccessibility()
    }
    private func animate(_ layer: CALayer, from: Float, to: Float) {
        CATransaction.begin(); CATransaction.setDisableActions(true); layer.opacity = to; CATransaction.commit()
        guard !HUDRuntimeAppearance.reduceMotion else { return }
        let a = CABasicAnimation(keyPath:"opacity"); a.fromValue = from; a.toValue = to; a.duration = 0.16; layer.add(a,forKey:"media.menu")
    }
}
private final class MediaAssemblyAXButton: NSButton {
    var onPress: (() -> Void)?
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func accessibilityPerformPress() -> Bool { guard isEnabled else { return false }; onPress?(); return true }
    override func keyDown(with event: NSEvent) { if event.keyCode == 36 || event.keyCode == 49 { onPress?() } else { super.keyDown(with:event) } }
}

private final class MediaAssemblySeekSlider: NSSlider {
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func setAccessibilityValue(_ value: Any?) {
        guard isEnabled, let number = value as? NSNumber, number.doubleValue.isFinite else { return }
        doubleValue = min(maxValue, max(minValue, number.doubleValue)); _ = sendAction(action, to: target)
    }
    override func accessibilityPerformIncrement() -> Bool { adjust(5) }
    override func accessibilityPerformDecrement() -> Bool { adjust(-5) }
    private func adjust(_ delta: Double) -> Bool { guard isEnabled else { return false }; setAccessibilityValue(NSNumber(value: doubleValue + delta)); return true }
}

private final class MediaAssemblyStickerAXSlider: NSSlider {
    var onValue: ((Double) -> Void)?
    var increment = 0.01
    override func draw(_ dirtyRect: NSRect) {}
    override func hitTest(_ point: NSPoint) -> NSView? { nil }
    override func setAccessibilityValue(_ value: Any?) {
        guard isEnabled, let number = value as? NSNumber, number.doubleValue.isFinite else { return }
        doubleValue = min(maxValue,max(minValue,number.doubleValue)); onValue?(doubleValue)
    }
    override func keyDown(with event:NSEvent) {
        if event.keyCode == 123 || event.keyCode == 125 { _ = adjust(-increment) }
        else if event.keyCode == 124 || event.keyCode == 126 { _ = adjust(increment) }
        else { super.keyDown(with:event) }
    }
    override func accessibilityPerformIncrement() -> Bool { adjust(increment) }
    override func accessibilityPerformDecrement() -> Bool { adjust(-increment) }
    private func adjust(_ value: Double) -> Bool { guard isEnabled else { return false }; setAccessibilityValue(NSNumber(value:doubleValue+value)); return true }
}
