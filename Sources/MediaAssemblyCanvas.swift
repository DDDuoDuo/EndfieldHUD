import AppKit
import AVFoundation

/// The Photo Mode arrangement: one large image, a compact upper-left tool and
/// preset drawer, view controls below. Only explicit media edits invoke CI.
final class MediaAssemblyCanvas: NSObject, HUDModuleContentFactory {
    struct Action { let id: String; let title: String; let rect: CGRect; let enabled: Bool }
    enum Drawer: String { case tools, filters, stickers }
    enum Tool: String { case crop, adjust, curves, levels, trim }
    struct ParameterControl { let parameter: MediaAssemblyParameter; let rect: CGRect }
    private(set) var activeTool: Tool?
    private(set) var parameterControls: [ParameterControl] = []
    private let drawerClip = CALayer(), drawerContent = CALayer()
    private var parameterArtwork: [String: (CATextLayer, CALayer, CALayer)] = [:]
    private let cropOverlay = CALayer(), cropShade = CAShapeLayer(), cropBorder = CAShapeLayer()
    private var cropHandleLayers: [CALayer] = []
    private let drawerThumb = CALayer()
    private let trimRail = CALayer(), trimFill = CALayer(), trimStartHandle = CALayer(), trimEndHandle = CALayer(), trimText = CATextLayer()
    private enum Drag {
        case pan(CGPoint)
        case parameter(String)
        case trim(Bool)
        case crop(Int, CGPoint, CGRect)
        case move(UUID, CGPoint, MediaAssemblySticker)
        case scale(UUID, CGFloat, MediaAssemblySticker)
        case rotate(UUID, CGFloat, MediaAssemblySticker)
    }
    let layer = CALayer()
    let controller: MediaAssemblyController
    var onChange: (() -> Void)?
    var onAction: ((String) -> Void)?
    private(set) var actions: [Action] = []
    private(set) var dark = true
    private(set) var viewport = MediaAssemblyViewport()
    private(set) var drawer: Drawer?
    private(set) var selectedStickerID: UUID?
    private(set) var drawerOffset: CGFloat = 0
    private var scale: CGFloat = 2
    private var presented = false
    private var artworkPrepared = false
    private var controlKey = ""
    private var sourceID: UUID?
    private let stage = CALayer(), surface = CALayer(), picture = CALayer()
    private var video: AVPlayerLayer?
    private let controls = CALayer(), presets = CALayer(), selection = CALayer()
    private let selectionBorder = CAShapeLayer()
    private var handleLayers: [CALayer] = []
    private let heading = CATextLayer(), empty = CATextLayer(), status = CATextLayer()
    private let rail = CALayer(), fill = CALayer(), elapsed = CATextLayer(), zoomLabel = CATextLayer()
    private var stickerLayers: [UUID: CALayer] = [:]
    private var seeking = false
    private var seekValue: Double?
    private var drag: Drag?
    private var closingMedia: CALayer?
    var isDragging: Bool { seeking || drag != nil }
    static let previewRect = CGRect(x: 12, y: 42, width: 416, height: 312)
    static let seekRect = CGRect(x: 49, y: 335, width: 306, height: 17)
    static let presetRect = CGRect(x: 19, y: 84, width: 173, height: 234)
    var imageRect: CGRect { viewport.imageRect(size: imageSize, in: Self.previewRect) }
    private var imageSize: CGSize {
        if let image = controller.preview { return CGSize(width: image.width, height: image.height) }
        guard let doc = controller.document else { return Self.previewRect.size }
        let p = controller.adjustments, crop = controller.isCropPreview ? MediaAssemblyCrop() : p.crop
        let width = doc.pixelSize.width * crop.width, height = doc.pixelSize.height * crop.height
        return p.rotationQuarterTurns % 2 == 0 ? CGSize(width: width, height: height) : CGSize(width: height, height: width)
    }
    private var editable: Bool { controller.document != nil && !controller.isBusy && !controller.isExporting }
    private var ink: NSColor { NSColor(white: dark ? 0.94 : 0.1, alpha: 1) }
    init(controller: MediaAssemblyController) {
        self.controller = controller; super.init()
        layer.name = "mediaAssembly.canvas"; layer.bounds = CGRect(x: 0, y: 0, width: 440, height: 440)
        stage.frame = Self.previewRect; stage.masksToBounds = true; stage.cornerRadius = 4
        surface.masksToBounds = true; picture.contentsGravity = .resize
        surface.addSublayer(picture); stage.addSublayer(surface)
        for item in [stage, cropOverlay, heading, empty, controls, presets, selection, rail, fill, elapsed, zoomLabel, status] { layer.addSublayer(item) }
        cropOverlay.frame = Self.previewRect; cropOverlay.masksToBounds = true
        cropOverlay.addSublayer(cropShade); cropOverlay.addSublayer(cropBorder)
        cropShade.fillRule = .evenOdd; cropShade.fillColor = NSColor.black.withAlphaComponent(0.48).cgColor
        cropBorder.fillColor = nil; cropBorder.lineWidth = 1
        for _ in 0..<4 { let handle = CALayer(); cropOverlay.addSublayer(handle); cropHandleLayers.append(handle) }
        drawerClip.frame = Self.presetRect; drawerClip.masksToBounds = true; drawerClip.addSublayer(drawerContent)
        selection.masksToBounds = true; selection.frame = Self.previewRect
        selection.addSublayer(selectionBorder); selectionBorder.fillColor = nil; selectionBorder.lineWidth = 1
        for index in 0..<6 {
            let handle: CALayer = index < 2 ? CATextLayer() : CALayer()
            selection.addSublayer(handle); handleLayers.append(handle)
        }
        controller.onChange = { [weak self] in guard let self, self.presented else { return }; self.refresh() }
    }
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        artworkPrepared = true; dark = style.dark; scale = style.contentsScale; controlKey = ""; refresh(); return layer
    }
    func updateRenderScale(_ value: CGFloat) {
        guard value.isFinite, value > 0, scale != value else { return }
        scale = value; controlKey = ""; refresh()
    }
    func setPresented(_ value: Bool) {
        guard presented != value else { return }; presented = value
        // Keep the bounded poster/control artwork for the outgoing HUD wipe.
        // Decoding and controller caches stop immediately; layers die with the HUD.
        if !value { closingMedia?.removeFromSuperlayer(); closingMedia = nil; seeking = false; seekValue = nil; drag = nil; video?.player = nil; video?.isHidden = true }
        if value { artworkPrepared = true }
        controller.setActive(value)
        if !value { activeTool = nil; controller.setCropPreview(false) }
        if value { controlKey = ""; refresh() }
    }
    func perform(_ id: String) {
        if let next = Drawer(rawValue: id) {
            guard editable else { return }; closeTool(); drawer = drawer == next ? nil : next; drawerOffset = 0
            controlKey = ""; refresh(); animateDrawer(); return
        }
        if let tool = Tool(rawValue: id) {
            guard editable, tool != .trim || controller.document?.isVideo == true else { return }
            activeTool = tool; drawer = .tools; drawerOffset = 0; selectedStickerID = nil
            controller.setCropPreview(tool == .crop); controlKey = ""; refresh(); animateDrawer(); return
        }
        if id == "toolBack" { closeTool(); drawer = .tools; drawerOffset = 0; controlKey = ""; refresh(); animateDrawer(); return }
        if id == "cropReset" { var p = controller.adjustments; p.crop = MediaAssemblyCrop(); controller.updateAdjustments(p); return }
        if id == "trimReset", let document = controller.document { controller.updateTrim(start:0,end:document.duration,scrubTo:0); return }
        if id.hasPrefix("filter:"), let value = MediaAssemblyFilter(rawValue: String(id.dropFirst(7))) {
            guard editable else { return }; var p = controller.adjustments; p.filter = value; controller.updateAdjustments(p); return
        }
        if id.hasPrefix("sticker:"), let kind = MediaAssemblyStickerKind(rawValue: String(id.dropFirst(8))), editable,
           controller.document?.isVideo == false, controller.adjustments.stickers.count < 16 {
            var p = controller.adjustments; let item = MediaAssemblySticker(kind: kind)
            selectedStickerID = item.id; p.stickers.append(item); drawer = nil; controlKey = ""; controller.updateAdjustments(p); return
        }
        switch id {
        case "rotate": var p = controller.adjustments; p.rotationQuarterTurns = (p.rotationQuarterTurns + 1) % 4; controller.updateAdjustments(p)
        case "mirror": var p = controller.adjustments; p.mirrored.toggle(); controller.updateAdjustments(p)
        case "reset": viewport.reset(); selectedStickerID = nil; controller.reset(); refreshViewTransform(); onChange?()
        case "zoomIn": zoom(by: 1.25, at: CGPoint(x: Self.previewRect.midX,y:Self.previewRect.midY))
        case "zoomOut": zoom(by: 0.8, at: CGPoint(x: Self.previewRect.midX,y:Self.previewRect.midY))
        case "zoomReset": viewport.reset(); refreshViewTransform(); onChange?()
        case "deleteSticker": removeSelectedSticker()
        case "play": if activeTool != .crop { controller.togglePlayback() }
        case "cancelExport": controller.cancelExport()
        case "closeMedia": closeMedia()
        case "open", "export": closeTool(); controlKey = ""; refresh(); onAction?(id)
        default: onAction?(id)
        }
    }
    private func closeMedia() {
        guard !controller.isExporting, controller.document != nil || controller.isBusy else { return }
        closingMedia?.removeFromSuperlayer(); closingMedia = nil
        // Retain the existing small poster/texture references for the finite
        // disappearance; no screenshot, full-resolution decode or video survives.
        let outgoing = CALayer(); outgoing.name = "media.closingSource"
        outgoing.frame = Self.previewRect; outgoing.masksToBounds = true; outgoing.cornerRadius = stage.cornerRadius
        let base = CALayer(); base.frame = surface.frame; base.masksToBounds = true
        let poster = CALayer(); poster.frame = picture.frame; poster.contents = picture.contents; poster.contentsGravity = .resize
        base.addSublayer(poster)
        for sticker in stickerLayers.values {
            let copy = CALayer(); copy.bounds = sticker.bounds; copy.position = sticker.position
            copy.transform = sticker.transform; copy.contents = sticker.contents; copy.contentsGravity = sticker.contentsGravity
            base.addSublayer(copy)
        }
        outgoing.addSublayer(base)
        drag = nil; seeking = false; seekValue = nil
        onAction?("closeMedia")
        guard controller.closeDocument() else { return }
        guard poster.contents != nil, !HUDRuntimeAppearance.reduceMotion else { return }
        layer.insertSublayer(outgoing,above:stage); outgoing.opacity = 0; closingMedia = outgoing
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self,weak outgoing] in
            outgoing?.removeFromSuperlayer()
            if self?.closingMedia === outgoing { self?.closingMedia = nil }
        }
        let fade = CABasicAnimation(keyPath:"opacity"); fade.fromValue = 1; fade.toValue = 0; fade.duration = 0.16
        outgoing.add(fade,forKey:"media.close"); CATransaction.commit()
    }
    private func closeTool() { activeTool = nil; controller.setCropPreview(false) }
    func selectSticker(_ id: UUID?) { selectedStickerID = id; drawSelection(); onChange?() }
    func removeSelectedSticker() {
        guard editable, let id = selectedStickerID else { return }
        var p = controller.adjustments; p.stickers.removeAll { $0.id == id }; selectedStickerID = nil; controller.updateAdjustments(p)
    }
    @discardableResult func adjustSelectedSticker(dx: Double = 0, dy: Double = 0, size: Double = 0, rotation: Double = 0) -> Bool {
        guard editable, let id = selectedStickerID, let i = controller.adjustments.stickers.firstIndex(where: {$0.id == id}) else { return false }
        var p = controller.adjustments; p.stickers[i].x = min(1,max(0,p.stickers[i].x+dx)); p.stickers[i].y = min(1,max(0,p.stickers[i].y+dy))
        p.stickers[i].size = min(1,max(0.02,p.stickers[i].size+size)); p.stickers[i].rotation = (p.stickers[i].rotation+rotation).truncatingRemainder(dividingBy:360)
        controller.updateAdjustments(p); return true
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        if let close = actions.first(where: { $0.id == "closeMedia" && $0.enabled && $0.rect.contains(point) }) { perform(close.id); return true }
        if editable, activeTool == .crop, let index = cropHandlePoints.firstIndex(where: { hypot(point.x-$0.x,point.y-$0.y) <= 10 }) {
            drag = .crop(index,point,displayCropRect); return true
        }
        if editable, activeTool == .trim, trimRangeRect.insetBy(dx:-8,dy:-8).contains(point) {
            let start = trimRangeRect.minX + trimRangeRect.width * CGFloat(controller.adjustments.trimStart / max(0.05,controller.document?.duration ?? 0))
            let end = trimRangeRect.minX + trimRangeRect.width * CGFloat((controller.adjustments.trimEnd ?? controller.document?.duration ?? 0) / max(0.05,controller.document?.duration ?? 0))
            let beginning = abs(point.x-start) <= abs(point.x-end)
            drag = .trim(beginning); dragTrim(beginning,at:point); return true
        }
        if editable, let control = parameterControls.first(where: { $0.rect.contains(point) }) {
            drag = .parameter(control.parameter.id); dragParameter(control.parameter.id,at:point); return true
        }
        if controller.document?.isVideo == true, activeTool != .trim, !controller.isExporting, Self.seekRect.contains(point) {
            seeking = true; seekValue = fraction(point); refreshProgress(); return true
        }
        if let action = actions.first(where: { $0.enabled && $0.rect.contains(point) }) { perform(action.id); return true }
        guard editable, Self.previewRect.contains(point) else { return layer.bounds.contains(point) }
        if drawer != nil, drawerBounds.insetBy(dx:-4,dy:-4).contains(point) { return true }
        if let item = selectedSticker, let handle = hitHandle(point, item) {
            let center = stickerCenter(item), distance = max(1,hypot(point.x-center.x,point.y-center.y))
            if handle == "delete" { removeSelectedSticker(); return true }
            drag = handle == "rotate" ? .rotate(item.id,atan2(point.y-center.y,point.x-center.x),item) : .scale(item.id,distance,item)
            return true
        }
        if let item = controller.adjustments.stickers.reversed().first(where: { item in
            let rect = MediaAssemblyViewport.stickerRect(item,in:imageRect)
            return rect.contains(MediaAssemblyViewport.unrotate(point,around:CGPoint(x:rect.midX,y:rect.midY),degrees:item.rotation))
        }) {
            selectedStickerID = item.id; drawer = nil; controlKey = ""; drag = .move(item.id,point,item); refresh(); return true
        }
        selectedStickerID = nil; drag = .pan(point); drawSelection(); return true
    }
    func mouseDragged(to point: CGPoint) {
        if seeking { seekValue = fraction(point); refreshProgress(); return }
        guard let drag, editable else { return }
        switch drag {
        case .parameter(let id): dragParameter(id,at:point)
        case .trim(let beginning): dragTrim(beginning,at:point)
        case .crop(let corner,let start,let original):
            let delta = CGPoint(x:(point.x-start.x)/imageRect.width,y:(point.y-start.y)/imageRect.height)
            let left = corner == 0 || corner == 2, top = corner < 2
            let x = left ? min(original.maxX-0.01,max(0,original.minX+delta.x)) : min(1,max(original.minX+0.01,original.maxX+delta.x))
            let y = top ? min(original.maxY-0.01,max(0,original.minY+delta.y)) : min(1,max(original.minY+0.01,original.maxY+delta.y))
            let rect = CGRect(x:left ? x:original.minX,y:top ? y:original.minY,width:left ? original.maxX-x:x-original.minX,height:top ? original.maxY-y:y-original.minY)
            var p = controller.adjustments; p.crop = MediaAssemblyViewport.sourceCrop(rect,quarterTurns:p.rotationQuarterTurns,mirrored:p.mirrored); controller.updateAdjustments(p)
        case .pan(let previous):
            viewport.move(by:CGPoint(x:point.x-previous.x,y:point.y-previous.y),size:imageSize,in:Self.previewRect)
            self.drag = .pan(point); refreshViewTransform(); onChange?()
        case .move(let id,let start,var item):
            item.x = min(1,max(0,item.x+Double((point.x-start.x)/imageRect.width)))
            item.y = min(1,max(0,item.y+Double((point.y-start.y)/imageRect.height))); replaceSticker(id,item)
        case .scale(let id,let distance,var item):
            let center = stickerCenter(item)
            item.size = min(1,max(0.02,item.size * hypot(point.x-center.x,point.y-center.y)/distance)); replaceSticker(id,item)
        case .rotate(let id,let angle,var item):
            let center = stickerCenter(item)
            item.rotation = (item.rotation+Double((atan2(point.y-center.y,point.x-center.x)-angle)*180 / .pi)).truncatingRemainder(dividingBy:360)
            replaceSticker(id,item)
        }
    }
    func mouseUp() {
        if seeking, let seekValue { controller.seek(to: seekValue * (controller.document?.duration ?? 0)) }
        seeking = false; seekValue = nil; drag = nil; refreshProgress()
    }
    @discardableResult func zoom(by factor: CGFloat, at point: CGPoint) -> Bool {
        guard controller.document != nil, Self.previewRect.contains(point), factor.isFinite else { return false }
        viewport.magnify(by:factor,at:point,size:imageSize,in:Self.previewRect); refreshViewTransform(); onChange?(); return true
    }
    @discardableResult func scroll(at point: CGPoint, deltaX: CGFloat = 0, deltaY: CGFloat, zoom: Bool = false) -> Bool {
        if drawer != nil, Self.presetRect.contains(point) {
            let next = min(maxDrawerOffset,max(0,drawerOffset+deltaY))
            guard next != drawerOffset else { return true }
            drawerOffset = next
            if activeTool != nil { updateInlineScroll(animated: true); onChange?() }
            else { controlKey = ""; refresh() }
            return true
        }
        guard editable, Self.previewRect.contains(point) else { return layer.bounds.contains(point) }
        if zoom { return self.zoom(by:exp(-deltaY*0.01),at:point) }
        viewport.move(by:CGPoint(x:-deltaX,y:-deltaY),size:imageSize,in:Self.previewRect); refreshViewTransform(); onChange?(); return true
    }
    private func replaceSticker(_ id: UUID,_ item: MediaAssemblySticker) {
        var p = controller.adjustments; guard let index = p.stickers.firstIndex(where: {$0.id == id}) else { return }
        p.stickers[index] = item; controller.updateAdjustments(p)
    }
    private var selectedSticker: MediaAssemblySticker? { controller.adjustments.stickers.first { $0.id == selectedStickerID } }
    private func stickerCenter(_ item: MediaAssemblySticker) -> CGPoint { CGPoint(x:imageRect.minX+imageRect.width*item.x,y:imageRect.minY+imageRect.height*item.y) }
    private func stickerHandles(_ item: MediaAssemblySticker) -> [(String,CGPoint)] {
        let rect = MediaAssemblyViewport.stickerRect(item,in:imageRect), center = CGPoint(x:rect.midX,y:rect.midY)
        let handles = [("rotate",CGPoint(x:rect.midX,y:rect.minY-17)),("delete",CGPoint(x:rect.maxX+10,y:rect.minY-10))]
            + [CGPoint(x:rect.minX,y:rect.minY),CGPoint(x:rect.maxX,y:rect.minY),CGPoint(x:rect.minX,y:rect.maxY),CGPoint(x:rect.maxX,y:rect.maxY)].map { ("scale",$0) }
        let safe = Self.previewRect.insetBy(dx:9,dy:9)
        return handles.map { name,point in
            let rotated = MediaAssemblyViewport.unrotate(point,around:center,degrees:-item.rotation)
            return (name,CGPoint(x:min(safe.maxX,max(safe.minX,rotated.x)),y:min(safe.maxY,max(safe.minY,rotated.y))))
        }
    }
    private func hitHandle(_ point: CGPoint,_ item: MediaAssemblySticker) -> String? {
        stickerHandles(item).first { hypot(point.x-$0.1.x,point.y-$0.1.y) <= 8 }?.0
    }
    private func fraction(_ point: CGPoint) -> Double { min(1,max(0,Double((point.x-Self.seekRect.minX)/Self.seekRect.width))) }
    private func refresh() {
        // Hidden modules keep only their lightweight layer skeleton. In particular,
        // entering the HUD must not initialize AVFoundation's presentation stack.
        guard artworkPrepared else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        if sourceID != controller.document?.id { sourceID = controller.document?.id; viewport.reset(); selectedStickerID = nil; drawer = nil; activeTool = nil; controller.setCropPreview(false); drawerOffset = 0; controlKey = "" }
        stage.backgroundColor = NSColor(white:dark ? 0.03:0.92,alpha:0.6).cgColor
        picture.contents = controller.preview
        if presented, let player = controller.player {
            if video == nil {
                let output = AVPlayerLayer(); output.videoGravity = .resize
                surface.insertSublayer(output, above: picture); video = output
            }
            video?.player = player
        } else { video?.player = nil }
        video?.isHidden = !controller.isPlaying
        label(heading,HUDSectionHeading.text(L10n.text("Media Assembly","影像加工")),CGRect(x:12,y:9,width:328,height:22),14,ink)
        label(empty,controller.isBusy ? L10n.text("Loading…","正在载入…") : controller.document == nil ? L10n.text("Open an image or video","打开图片或视频") : "",CGRect(x:35,y:174,width:370,height:42),14,ink)
        empty.alignmentMode = .center; empty.isWrapped = true
        let error = controller.error?.localizedDescription ?? ""
        let progress = controller.isExporting ? L10n.text("Exporting","正在导出")+" \(Int(controller.progress*100))%" : ""
        label(status,error.isEmpty ? progress:error,CGRect(x:18,y:405,width:410,height:31),9,error.isEmpty ? ink:.systemOrange); status.isWrapped = true
        let key = "\(sourceID):\(controller.isBusy):\(editable):\(controller.document?.isVideo == true):\(controller.isPlaying):\(controller.isExporting):\(dark):\(HUDRuntimeAppearance.accent):\(L10n.resolvedLanguage):\(drawer):\(activeTool):\(activeTool == nil ? drawerOffset : 0):\(controller.adjustments.filter):\(controller.adjustments.stickers.count)"
        if key != controlKey { controlKey = key; rebuildControls() }
        refreshViewTransform(); refreshProgress(); updateInlineValues(); CATransaction.commit(); onChange?()
    }
    private func rebuildControls() {
        controls.sublayers?.forEach{$0.removeFromSuperlayer()}; presets.sublayers?.forEach{$0.removeFromSuperlayer()}; actions = []; parameterControls = []; parameterArtwork = [:]; drawerContent.sublayers?.forEach{$0.removeFromSuperlayer()}
        action("open",L10n.text("Open","打开"),CGRect(x:350,y:7,width:78,height:28),!controller.isExporting)
        action("closeMedia","×",CGRect(x:400,y:44,width:28,height:28),!controller.isExporting && (controller.document != nil || controller.isBusy))
        for (i,pair) in [("tools",L10n.text("Adjust","调整")),("filters",L10n.text("Filters","滤镜")),("stickers",L10n.text("Stickers","贴纸"))].enumerated() {
            action(pair.0,pair.1,CGRect(x:19+i*58,y:50,width:54,height:26),editable && (pair.0 != "stickers" || controller.document?.isVideo == false),selected:drawer?.rawValue == pair.0)
        }
        action("zoomOut","−",CGRect(x:104,y:371,width:26,height:28),controller.document != nil)
        action("zoomReset",L10n.text("Reset","重置"),CGRect(x:137,y:371,width:68,height:28),controller.document != nil,draw:false)
        action("zoomIn","+",CGRect(x:213,y:371,width:26,height:28),controller.document != nil)
        action(controller.isExporting ? "cancelExport":"export",controller.isExporting ? L10n.text("Cancel","取消"):L10n.text("Export","导出"),CGRect(x:350,y:371,width:78,height:28),controller.isExporting || editable,selected:true)
        if controller.document?.isVideo == true { action("play",controller.isPlaying ? "Ⅱ":"▷",CGRect(x:17,y:333,width:24,height:22),editable && activeTool != .crop) }
        if let drawer {
            let backdrop = CALayer(); backdrop.frame = drawerBounds.insetBy(dx:-3,dy:-3); backdrop.cornerRadius = 3
            backdrop.backgroundColor = NSColor(white:0.08,alpha:0.92).cgColor; presets.addSublayer(backdrop)
            if activeTool != nil { buildInlineControls() }
            else if drawer == .tools {
                let pairs = [("crop",L10n.text("Crop","裁剪")),("rotate",L10n.text("Rotate","旋转")),("mirror",L10n.text("Mirror","镜像")),("adjust",L10n.text("Adjust","调整")),("curves",L10n.text("Curves","曲线")),("levels",L10n.text("Levels","色阶")),("trim",L10n.text("Trim","剪辑")),("reset",L10n.text("Reset","重置"))]
                for (i,pair) in pairs.enumerated() { action(pair.0,pair.1,CGRect(x:25+(i%2)*81,y:94+(i/2)*47,width:74,height:34),editable && (pair.0 != "trim" || controller.document?.isVideo == true),parent:presets) }
            } else { buildPresetGrid(drawer) }
        }
    }
    private var maxDrawerOffset: CGFloat {
        if activeTool != nil { return max(0,CGFloat(inlineParameters.count)*52-Self.inlineScrollRect.height) }
        let count = drawer == .filters ? MediaAssemblyFilter.allCases.count : MediaAssemblyStickerKind.allCases.count
        return max(0,CGFloat((count+2)/3)*57-Self.presetRect.height)
    }
    private func buildPresetGrid(_ drawer: Drawer) {
        let clip = CALayer(); clip.frame = Self.presetRect; clip.masksToBounds = true; presets.addSublayer(clip)
        let count = drawer == .filters ? MediaAssemblyFilter.allCases.count : MediaAssemblyStickerKind.allCases.count
        for index in 0..<count {
            let local = CGRect(x:CGFloat(index%3)*57+3,y:CGFloat(index/3)*57+3-drawerOffset,width:49,height:49)
            let visible = local.intersection(clip.bounds); guard visible.height > 0 else { continue }
            let id: String, title: String, image: CGImage?, selected: Bool
            if drawer == .filters { let item = MediaAssemblyFilter.allCases[index]; id = "filter:"+item.rawValue; title = item.title; image = MediaAssemblyAssetCatalog.filterThumbnail(item); selected = controller.adjustments.filter == item }
            else { let item = MediaAssemblyStickerKind.allCases[index]; id = "sticker:"+item.rawValue; title = item.title; image = MediaAssemblyAssetCatalog.stickerThumbnail(item); selected = false }
            let face = CALayer(); face.frame = local; face.cornerRadius = 3; face.backgroundColor = NSColor.white.withAlphaComponent(selected ? 0.18:0.07).cgColor
            face.borderColor = HUDRuntimeAppearance.accent.cgColor; face.borderWidth = selected ? 1.5:0; clip.addSublayer(face)
            let bitmap = CALayer(); bitmap.frame = face.bounds.insetBy(dx:5,dy:5); bitmap.contents = image; bitmap.contentsGravity = .resizeAspect; face.addSublayer(bitmap)
            if image == nil { let mark = CATextLayer(); label(mark,"∅",face.bounds.insetBy(dx:5,dy:9),22,.white); mark.alignmentMode = .center; face.addSublayer(mark) }
            HUDControlHighlightLayer.add(to:clip,rect:local,shape:.cutCorner,enabled:editable,framed:false)
            actions.append(Action(id:id,title:title,rect:visible.offsetBy(dx:Self.presetRect.minX,dy:Self.presetRect.minY),enabled:editable && (drawer == .filters || controller.adjustments.stickers.count < 16)))
        }
        if maxDrawerOffset > 0 { let thumb = CALayer(); thumb.cornerRadius = 1; thumb.backgroundColor = NSColor.white.withAlphaComponent(0.4).cgColor
            let h = max(24,Self.presetRect.height*Self.presetRect.height/(Self.presetRect.height+maxDrawerOffset))
            thumb.frame = CGRect(x:Self.presetRect.maxX-1,y:Self.presetRect.minY+(Self.presetRect.height-h)*drawerOffset/maxDrawerOffset,width:2,height:h); presets.addSublayer(thumb) }
    }
    private func refreshViewTransform() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        viewport.move(by:.zero,size:imageSize,in:Self.previewRect)
        surface.frame = imageRect.offsetBy(dx:-Self.previewRect.minX,dy:-Self.previewRect.minY); picture.frame = surface.bounds; video?.frame = surface.bounds
        let ids = Set(controller.adjustments.stickers.map(\.id))
        for id in Array(stickerLayers.keys) where !ids.contains(id) { stickerLayers.removeValue(forKey:id)?.removeFromSuperlayer() }
        for item in controller.adjustments.stickers {
            let bitmap = stickerLayers[item.id] ?? CALayer()
            if stickerLayers[item.id] == nil { bitmap.contents = MediaAssemblyAssetCatalog.stickerImage(item.kind); bitmap.contentsGravity = .resizeAspect; stickerLayers[item.id] = bitmap; surface.addSublayer(bitmap) }
            let crop = displayCropRect
            let base = activeTool == .crop ? CGRect(x:surface.bounds.width*crop.minX,y:surface.bounds.height*crop.minY,width:surface.bounds.width*crop.width,height:surface.bounds.height*crop.height) : surface.bounds
            let rect = MediaAssemblyViewport.stickerRect(item,in:base)
            bitmap.bounds = CGRect(origin:.zero,size:rect.size); bitmap.position = CGPoint(x:rect.midX,y:rect.midY); bitmap.setAffineTransform(CGAffineTransform(rotationAngle:item.rotation * .pi / 180))
        }
        label(zoomLabel,String(format:"%.1f×",viewport.zoom),CGRect(x:137,y:378,width:68,height:16),11,ink); zoomLabel.alignmentMode = .center
        drawSelection(); drawCrop(); CATransaction.commit()
    }
    private func drawSelection() {
        selection.isHidden = selectedSticker == nil || drawer != nil
        guard let item = selectedSticker, drawer == nil else { return }
        let rect = MediaAssemblyViewport.stickerRect(item,in:imageRect), center = CGPoint(x:rect.midX,y:rect.midY)
        let path = CGMutablePath()
        for (i,p) in [CGPoint(x:rect.minX,y:rect.minY),CGPoint(x:rect.maxX,y:rect.minY),CGPoint(x:rect.maxX,y:rect.maxY),CGPoint(x:rect.minX,y:rect.maxY)].enumerated() {
            let q = MediaAssemblyViewport.unrotate(p,around:center,degrees:-item.rotation)
            let local = CGPoint(x:q.x-Self.previewRect.minX,y:q.y-Self.previewRect.minY)
            if i == 0 { path.move(to:local) } else { path.addLine(to:local) }
        }
        path.closeSubpath(); selectionBorder.path = path; selectionBorder.strokeColor = HUDRuntimeAppearance.accent.cgColor
        for (index,entry) in stickerHandles(item).enumerated() {
            let point = CGPoint(x:entry.1.x-Self.previewRect.minX,y:entry.1.y-Self.previewRect.minY), handle = handleLayers[index]
            if let text = handle as? CATextLayer {
                label(text,index == 0 ? "↻":"×",CGRect(x:point.x-8,y:point.y-8,width:16,height:16),13,.white)
                text.backgroundColor = NSColor.black.withAlphaComponent(0.85).cgColor; text.cornerRadius = 8; text.alignmentMode = .center
            } else { handle.frame = CGRect(x:point.x-3,y:point.y-3,width:6,height:6); handle.backgroundColor = NSColor.white.cgColor }
        }
    }
    private func refreshProgress() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        let isVideo = controller.document?.isVideo == true
        rail.isHidden = !isVideo || activeTool == .trim; fill.isHidden = rail.isHidden
        let duration = controller.document?.duration ?? 0, time = seekValue.map{$0*duration} ?? controller.currentTime
        rail.frame = CGRect(x:Self.seekRect.minX,y:Self.seekRect.midY,width:Self.seekRect.width,height:2); rail.backgroundColor = NSColor.gray.withAlphaComponent(0.6).cgColor
        fill.frame = CGRect(x:rail.frame.minX,y:rail.frame.minY,width:rail.frame.width*min(1,max(0,time/max(0.01,duration))),height:2); fill.backgroundColor = HUDRuntimeAppearance.accent.cgColor
        label(elapsed,isVideo && activeTool != .trim ? String(format:"%d:%02d",Int(max(0,time))/60,Int(max(0,time))%60):"",CGRect(x:364,y:337,width:55,height:15),9,.white)
        CATransaction.commit()
    }
    static let inlineScrollRect = CGRect(x: 25,y:120,width:161,height:191)
    var trimRangeRect: CGRect { CGRect(x:30,y:164,width:151,height:24) }
    private var drawerBounds: CGRect { activeTool == .crop ? CGRect(x:19,y:84,width:173,height:74) : Self.presetRect }
    var displayCropRect: CGRect { let p = controller.adjustments; return MediaAssemblyViewport.displayCrop(p.crop,quarterTurns:p.rotationQuarterTurns,mirrored:p.mirrored) }
    var cropHandlePoints: [CGPoint] {
        let r = displayCropRect, image = imageRect
        let safe = Self.previewRect.insetBy(dx:7,dy:7)
        return [CGPoint(x:r.minX,y:r.minY),CGPoint(x:r.maxX,y:r.minY),CGPoint(x:r.minX,y:r.maxY),CGPoint(x:r.maxX,y:r.maxY)].map { p in
            var point = CGPoint(x:min(safe.maxX,max(safe.minX,image.minX+p.x*image.width)),y:min(safe.maxY,max(safe.minY,image.minY+p.y*image.height)))
            // Keep all handles reachable when the small crop toolbar covers a corner.
            if drawerBounds.insetBy(dx:-9,dy:-9).contains(point) || point.y < 82 && point.x < 198 { point.x = 203 }
            return point
        }
    }
    private func drawCrop() {
        cropOverlay.isHidden = activeTool != .crop
        guard activeTool == .crop else { return }
        let r = displayCropRect, image = imageRect
        let rect = CGRect(x:image.minX+r.minX*image.width-Self.previewRect.minX,y:image.minY+r.minY*image.height-Self.previewRect.minY,width:r.width*image.width,height:r.height*image.height)
        let shade = CGMutablePath(); shade.addRect(cropOverlay.bounds); shade.addRect(rect); cropShade.path = shade
        let border = CGMutablePath(); border.addRect(rect)
        for fraction in [CGFloat(1.0/3),CGFloat(2.0/3)] {
            border.move(to:CGPoint(x:rect.minX+rect.width*fraction,y:rect.minY)); border.addLine(to:CGPoint(x:rect.minX+rect.width*fraction,y:rect.maxY))
            border.move(to:CGPoint(x:rect.minX,y:rect.minY+rect.height*fraction)); border.addLine(to:CGPoint(x:rect.maxX,y:rect.minY+rect.height*fraction))
        }
        cropBorder.path = border; cropBorder.strokeColor = HUDRuntimeAppearance.accent.cgColor
        for (index,point) in cropHandlePoints.enumerated() {
            let handle = cropHandleLayers[index]; handle.frame = CGRect(x:point.x-Self.previewRect.minX-4,y:point.y-Self.previewRect.minY-4,width:8,height:8)
            handle.backgroundColor = NSColor.white.cgColor; handle.borderColor = NSColor.black.cgColor; handle.borderWidth = 1
        }
    }
    var inlineParameters: [MediaAssemblyParameter] {
        let p = controller.adjustments
        func parameter(_ id: String,_ title: String,_ range: ClosedRange<Double>,_ value: Double,_ step: Double = 0.01) -> MediaAssemblyParameter { MediaAssemblyParameter(id:id,title:title,range:range,value:value,step:step) }
        switch activeTool {
        case .adjust: return [parameter("brightness",L10n.text("Brightness","亮度"),-1...1,p.brightness),parameter("contrast",L10n.text("Contrast","对比度"),0...4,p.contrast),parameter("saturation",L10n.text("Saturation","饱和度"),0...2,p.saturation),parameter("temperature",L10n.text("Temperature","色温"),2000...12000,p.temperature,100),parameter("tint",L10n.text("Tint","色调"),-200...200,p.tint,1),parameter("highlights",L10n.text("Highlights","高光"),0...1,p.highlights),parameter("shadows",L10n.text("Shadows","阴影"),0...1,p.shadows),parameter("exposure",L10n.text("Exposure","曝光度"),-4...4,p.exposure)]
        case .curves: return (0..<5).map { parameter("curve\($0)","\($0*25)%",0...1,p.curve[$0]) }
        case .levels: return [parameter("black",L10n.text("Black point","黑场"),0...0.99,p.levelsBlack),parameter("gamma",L10n.text("Midtones","中间调"),0.1...4,p.levelsGamma),parameter("white",L10n.text("White point","白场"),0.01...1,p.levelsWhite)]
        default: return []
        }
    }
    func setParameter(_ id: String,to value: Double) {
        guard editable, value.isFinite, let parameter = inlineParameters.first(where:{$0.id == id}) else { return }
        let value = min(parameter.range.upperBound,max(parameter.range.lowerBound,(value/parameter.step).rounded()*parameter.step))
        var p = controller.adjustments
        switch id {
        case "brightness": p.brightness = value
        case "contrast": p.contrast = value
        case "saturation": p.saturation = value
        case "temperature": p.temperature = value
        case "tint": p.tint = value
        case "highlights": p.highlights = value
        case "shadows": p.shadows = value
        case "exposure": p.exposure = value
        case "black": p.levelsBlack = min(value,p.levelsWhite-0.01)
        case "white": p.levelsWhite = max(value,p.levelsBlack+0.01)
        case "gamma": p.levelsGamma = value
        default: if id.hasPrefix("curve"),let index = Int(id.dropFirst(5)),p.curve.indices.contains(index) { p.curve[index] = value }
        }
        controller.updateAdjustments(p)
    }
    private func dragParameter(_ id: String,at point: CGPoint) {
        guard let p = inlineParameters.first(where:{$0.id == id}) else { return }
        let fraction = min(1,max(0,Double((point.x-Self.inlineScrollRect.minX-5)/(Self.inlineScrollRect.width-10))))
        setParameter(id,to:p.range.lowerBound+fraction*(p.range.upperBound-p.range.lowerBound))
    }
    func setTrimEndpoint(beginning: Bool,to value: Double) {
        guard editable, let doc = controller.document,doc.isVideo,value.isFinite else { return }
        let p = controller.adjustments,end = p.trimEnd ?? doc.duration
        let start = beginning ? min(end-0.050001,max(0,value)) : p.trimStart
        let finish = beginning ? end : max(start+0.050001,min(doc.duration,value))
        controller.updateTrim(start:start,end:finish,scrubTo:beginning ? start:finish)
    }
    private func dragTrim(_ beginning: Bool,at point: CGPoint) {
        let fraction = min(1,max(0,Double((point.x-trimRangeRect.minX)/trimRangeRect.width)))
        setTrimEndpoint(beginning:beginning,to:fraction*(controller.document?.duration ?? 0))
    }
    private func buildInlineControls() {
        guard let tool = activeTool else { return }
        let titles: [Tool:String] = [.crop:L10n.text("Crop","裁剪"),.adjust:L10n.text("Adjust","调整"),.curves:L10n.text("Curves","曲线"),.levels:L10n.text("Levels","色阶"),.trim:L10n.text("Trim","剪辑")]
        action("toolBack","‹",CGRect(x:25,y:90,width:25,height:24),parent:presets)
        let title = CATextLayer(); label(title,titles[tool] ?? "",CGRect(x:59,y:95,width:121,height:18),11,.white); presets.addSublayer(title)
        if tool == .crop { action("cropReset",L10n.text("Reset","重置"),CGRect(x:25,y:124,width:155,height:26),parent:presets); return }
        if tool == .trim {
            for item in [trimRail,trimFill,trimStartHandle,trimEndHandle,trimText] { presets.addSublayer(item) }
            action("trimReset",L10n.text("Reset","重置"),CGRect(x:25,y:204,width:155,height:26),parent:presets)
            return
        }
        drawerClip.frame = Self.inlineScrollRect; presets.addSublayer(drawerClip); presets.addSublayer(drawerThumb)
        for (index,p) in inlineParameters.enumerated() {
            let text = CATextLayer(), rail = CALayer(), fill = CALayer(), knob = CALayer()
            label(text,"",CGRect(x:5,y:CGFloat(index)*52+2,width:151,height:19),10,.white)
            rail.frame = CGRect(x:5,y:CGFloat(index)*52+32,width:151,height:2); rail.backgroundColor = NSColor.white.withAlphaComponent(0.25).cgColor
            for item in [text,rail,fill,knob] { drawerContent.addSublayer(item) }
            parameterArtwork[p.id] = (text,fill,knob)
        }
        updateInlineScroll(animated:false); updateInlineValues()
    }
    private func updateInlineScroll(animated: Bool) {
        let previous = drawerContent.presentation()?.position ?? drawerContent.position
        CATransaction.begin(); CATransaction.setDisableActions(true)
        drawerContent.anchorPoint = .zero; drawerContent.position = CGPoint(x:0,y:-drawerOffset)
        parameterControls = inlineParameters.enumerated().compactMap { index,p in
            let rect = CGRect(x:Self.inlineScrollRect.minX+5,y:Self.inlineScrollRect.minY+CGFloat(index)*52+22-drawerOffset,width:151,height:22).intersection(Self.inlineScrollRect)
            return rect.isNull || rect.height < 8 ? nil : ParameterControl(parameter:p,rect:rect)
        }
        drawerThumb.isHidden = maxDrawerOffset <= 0
        if maxDrawerOffset > 0 {
            let height = max(20,Self.inlineScrollRect.height*Self.inlineScrollRect.height/(Self.inlineScrollRect.height+maxDrawerOffset))
            drawerThumb.frame = CGRect(x:Self.presetRect.maxX-2,y:Self.inlineScrollRect.minY+(Self.inlineScrollRect.height-height)*drawerOffset/maxDrawerOffset,width:2,height:height)
            drawerThumb.backgroundColor = NSColor.white.withAlphaComponent(0.4).cgColor; drawerThumb.cornerRadius = 1
        }
        CATransaction.commit()
        if animated && !HUDRuntimeAppearance.reduceMotion {
            let animation = CABasicAnimation(keyPath:"position"); animation.fromValue = NSValue(point:previous); animation.toValue = NSValue(point:drawerContent.position); animation.duration = 0.08; animation.timingFunction = CAMediaTimingFunction(name:.easeOut); drawerContent.add(animation,forKey:"media.parameters.scroll")
        }
    }
    private func updateInlineValues() {
        for (index,p) in inlineParameters.enumerated() {
            guard let (text,fill,knob) = parameterArtwork[p.id] else { continue }
            let value = p.step >= 1 ? String(Int(p.value.rounded())) : String(format:"%.2f",p.value)
            text.string = p.title + "  " + value
            let width = 151 * CGFloat((p.value-p.range.lowerBound)/(p.range.upperBound-p.range.lowerBound))
            fill.frame = CGRect(x:5,y:CGFloat(index)*52+32,width:width,height:2); fill.backgroundColor = HUDRuntimeAppearance.accent.cgColor
            knob.frame = CGRect(x:5+width-3,y:CGFloat(index)*52+27,width:6,height:12); knob.backgroundColor = NSColor.white.cgColor
        }
        if activeTool == .trim {
            let rect = trimRangeRect,duration = max(0.05,controller.document?.duration ?? 0),p = controller.adjustments
            let x = rect.minX+rect.width*CGFloat(p.trimStart/duration),end = rect.minX+rect.width*CGFloat((p.trimEnd ?? duration)/duration)
            trimRail.frame = CGRect(x:rect.minX,y:rect.midY-2,width:rect.width,height:4); trimRail.backgroundColor = NSColor.white.withAlphaComponent(0.25).cgColor
            trimFill.frame = CGRect(x:x,y:rect.midY-2,width:max(0,end-x),height:4); trimFill.backgroundColor = HUDRuntimeAppearance.accent.cgColor
            trimStartHandle.frame = CGRect(x:x-4,y:rect.midY-9,width:8,height:18); trimEndHandle.frame = CGRect(x:end-4,y:rect.midY-9,width:8,height:18)
            trimStartHandle.backgroundColor = NSColor.white.cgColor; trimEndHandle.backgroundColor = NSColor.white.cgColor
            label(trimText,String(format:"%.2fs  —  %.2fs",p.trimStart,p.trimEnd ?? duration),CGRect(x:27,y:136,width:157,height:20),10,.white); trimText.alignmentMode = .center
        }
    }
    private func action(_ id: String,_ text: String,_ rect: CGRect,_ enabled: Bool = true,selected: Bool = false,draw: Bool = true,parent: CALayer? = nil) {
        actions.append(Action(id:id,title:text,rect:rect,enabled:enabled)); guard draw else { return }; let parent = parent ?? controls
        let plate = CALayer(); plate.frame = rect; plate.cornerRadius = 2; plate.backgroundColor = (selected ? HUDRuntimeAppearance.accent:NSColor(white:0.12,alpha:0.95)).cgColor; parent.addSublayer(plate)
        HUDControlHighlightLayer.add(to:parent,rect:rect,shape:.cutCorner,enabled:enabled,framed:true)
        let title = CATextLayer(), font = NSFont.systemFont(ofSize:10,weight:.semibold)
        let h = ceil(font.ascender-font.descender+font.leading)
        label(title,text,CGRect(x:rect.minX+3,y:rect.midY-h/2,width:rect.width-6,height:h),10,(selected ? NSColor.black:NSColor.white).withAlphaComponent(enabled ? 1:0.35)); title.alignmentMode = .center; parent.addSublayer(title)
    }
    private func animateDrawer() {
        guard !HUDRuntimeAppearance.reduceMotion else { return }
        let fade = CABasicAnimation(keyPath:"opacity"); fade.fromValue = 0; fade.toValue = 1; fade.duration = 0.16; presets.add(fade,forKey:"media.drawer")
    }
    private func label(_ layer: CATextLayer,_ value: String,_ frame: CGRect,_ size: CGFloat,_ color: NSColor) {
        layer.frame = frame; layer.string = value; layer.font = NSFont.systemFont(ofSize:size,weight:.medium); layer.fontSize = size
        layer.foregroundColor = color.cgColor; layer.contentsScale = scale; layer.truncationMode = .end
    }
}
