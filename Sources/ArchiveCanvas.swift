import AppKit
import QuartzCore

/// The reference's bracketed document gallery and focused dark reading column,
/// fitted to the existing HUD plane. Clipped visible rows reuse retained layers.
final class ArchiveCanvas: HUDModuleContentFactory {
    let layer = CALayer()
    let controller: ArchiveController
    var onChange: (() -> Void)?
    var onAction: ((String) -> Void)?
    var onBeforeAction: (() -> Void)?
    private(set) var actions: [NotesRetainedMenu.Item] = []
    private(set) var dark = true
    private var accent = HUDRuntimeAppearance.accent
    private var scale: CGFloat = 2
    private(set) var scrollOffset: CGFloat = 0
    private(set) var categoryScrollOffset: CGFloat = 0
    private var gallery = CALayer(), categoryList = CALayer()
    private let galleryScrollTrack = CALayer(), galleryScrollKnob = CALayer()
    private var galleryNodes: [UUID: CALayer] = [:]
    private var categoryNodes: [String: CALayer] = [:]
    private var displayedEntries: [ArchiveSummary] = []
    private var visibleGalleryEntries: [ArchiveSummary] = []
    private var wantedThumbnailIDs: Set<UUID> = []
    private var visibleThumbnailReferences: [UUID: NotesMediaReference] = [:]
    private var displayedCategories: [(String, String)] = []
    private(set) var categoryID: UUID?
    private(set) var filtersUncategorized = false
    var formattingField: String? { didSet { if oldValue != formattingField { render() } } }
    var formattingColor = NSColor.white
    private let backdrop = CALayer()
    private var retiringFace: CALayer?
    private var retireFaceWork: DispatchWorkItem?
    private let thumbnails = ArchiveGalleryThumbnails()
    private var thumbnailSlots: [UUID: (CALayer, CALayer)] = [:]
    private var metadataRequests: Set<UUID> = []
    private var thumbnailGeneration = 0
    var statusMessage: String?
    var mediaIndex = 0
    private(set) var presentation: NotesMediaPresentation?
    private var presentationKey: String?
    private var visible = false
    private var hasPresented = false
    private var retiringMedia: NotesMediaPresentation?
    private var retireMediaWork: DispatchWorkItem?
    private var mediaRenderWork: DispatchWorkItem?
    private var renderedSelection: UUID?
    private var progressFill: CALayer?
    static let galleryRect = CGRect(x: 101, y: 49, width: 275, height: 357)
    static let galleryScrollerRect = CGRect(x: 380, y: 49, width: 8, height: 357)
    static let categoriesRect = CGRect(x: 14, y: 49, width: 75, height: 238)
    static let addRect = CGRect(x: 14, y: 302, width: 30, height: 29)
    static let removeCategoryRect = CGRect(x: 50, y: 302, width: 30, height: 29)
    static let bodyRect = CGRect(x: 24, y: 111, width: 352, height: 264)
    var bodyRect: CGRect { var rect = Self.bodyRect; if !(controller.selected?.media.isEmpty ?? true) { rect.size.height = 147 }; return rect }
    static let titleRect = CGRect(x: 24, y: 48, width: 352, height: 30)
    static let dateRect = CGRect(x: 24, y: 81, width: 180, height: 23)
    static let mediaRect = CGRect(x: 24, y: 268, width: 250, height: 93)
    static let seekRect = CGRect(x: 24, y: 367, width: 250, height: 12)
    var galleryScrollMaximum: CGFloat { max(0, CGFloat((displayedEntries.count + 1) / 2) * 122 - 9 - Self.galleryRect.height) }
    var galleryScrollThumb: CGRect? {
        guard controller.selected == nil, galleryScrollMaximum > 0 else { return nil }
        let track = Self.galleryScrollerRect
        let height = max(24, track.height * Self.galleryRect.height / (Self.galleryRect.height + galleryScrollMaximum))
        return CGRect(x: track.minX, y: track.minY + (track.height - height) * scrollOffset / galleryScrollMaximum, width: track.width, height: height)
    }
    func setGalleryScrollOffset(_ value: CGFloat) {
        guard value.isFinite else { return }; scroll(delta: min(galleryScrollMaximum, max(0, value)) - scrollOffset)
    }
    var filtered: [ArchiveSummary] {
        controller.entries.filter { filtersUncategorized ? $0.categoryID == nil : (categoryID == nil || $0.categoryID == categoryID) }
    }
    init(controller: ArchiveController) {
        self.controller = controller; layer.name = "archive.canvas"; layer.bounds = CGRect(x: 0, y: 0, width: 400, height: 440)
        backdrop.name = "archive.backdrop"; backdrop.frame = layer.bounds; backdrop.cornerRadius = 4
        layer.addSublayer(backdrop)
        thumbnails.onChange = { [weak self] id, image in self?.setThumbnail(image, for: id) }
        controller.onChange = { [weak self] in
            guard let self, self.visible else { return }
            self.render(animated: self.renderedSelection != self.controller.selected?.id)
        }
    }
    deinit {
        mediaRenderWork?.cancel(); retireMediaWork?.cancel(); retireFaceWork?.cancel(); thumbnails.clear()
        presentation?.onStateChange = nil; presentation?.onProgress = nil; presentation?.dispose()
        retiringMedia?.dispose()
    }
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; accent = style.accent; scale = style.contentsScale; render(); return layer
    }
    func updateRenderScale(_ value: CGFloat) {
        guard value.isFinite else { return }; scale = min(8, max(1, value))
        func update(_ layer: CALayer) { if layer is CATextLayer { layer.contentsScale = scale }; layer.sublayers?.forEach(update) }
        update(layer)
    }
    func setVisible(_ value: Bool) {
        guard visible != value else { return }; visible = value
        if value {
            hasPresented = true
            retireMediaWork?.cancel(); retireMediaWork = nil
            retiringMedia?.dispose(); retiringMedia = nil
            controller.activate()
        } else {
            thumbnailGeneration += 1; metadataRequests.removeAll(); thumbnails.clear(); thumbnailSlots.removeAll()
            wantedThumbnailIDs.removeAll(); visibleThumbnailReferences.removeAll()
            retireFaceWork?.cancel(); retireFaceWork = nil; retiringFace?.removeFromSuperlayer(); retiringFace = nil
            mediaRenderWork?.cancel(); mediaRenderWork = nil
            retireMediaWork?.cancel(); retiringMedia?.dispose(); retiringMedia = nil
            if let media = presentation {
                media.onStateChange = nil; media.onProgress = nil
                media.setVisible(false, preserveArtworkOnHide: true)
                retiringMedia = media; presentation = nil; presentationKey = nil
            }
            controller.deactivate()
            let work = DispatchWorkItem { [weak self] in
                guard let self, !self.visible else { return }
                self.retiringMedia?.dispose(); self.retiringMedia = nil; self.retireMediaWork = nil
            }
            retireMediaWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.6, execute: work)
        }
    }
    func scroll(at point: CGPoint? = nil, delta: CGFloat) {
        guard controller.selected == nil, delta.isFinite, delta != 0 else { return }
        if let point, Self.categoriesRect.contains(point) {
            let next = min(max(0, CGFloat(displayedCategories.count) * 34 - 7 - Self.categoriesRect.height), max(0, categoryScrollOffset + delta))
            guard next != categoryScrollOffset else { return }; categoryScrollOffset = next
        } else {
            guard point == nil || Self.galleryRect.contains(point!) || Self.galleryScrollerRect.contains(point!) else { return }
            let next = min(max(0, CGFloat((displayedEntries.count + 1) / 2) * 122 - 9 - Self.galleryRect.height), max(0, scrollOffset + delta))
            guard next != scrollOffset else { return }; scrollOffset = next
        }
        retireFaceWork?.cancel(); retireFaceWork = nil; retiringFace?.removeFromSuperlayer(); retiringFace = nil
        layer.sublayers?.first { $0.name == "archive.face" }?.removeAnimation(forKey: "archive.transition")
        CATransaction.begin(); CATransaction.setDisableActions(true)
        layoutCollections(); CATransaction.commit()
        loadVisibleThumbnails(); onChange?()
    }
    func perform(_ id: String) {
        if !id.hasPrefix("format") { onBeforeAction?() }
        if id == "all" || id == "uncategorized" || id.hasPrefix("category:") {
            categoryID = id.hasPrefix("category:") ? UUID(uuidString: String(id.dropFirst(9))) : nil
            filtersUncategorized = id == "uncategorized"; scrollOffset = 0; render(animated: true)
        } else if id.hasPrefix("entry:"), let uuid = UUID(uuidString: String(id.dropFirst(6))) { controller.select(uuid) }
        else if id == "back" { controller.select(nil) }
        else if id == "deleteCategory" { if let id = categoryID, controller.categories.contains(where: { $0.id == id }) { onAction?("deleteCategory") } }
        else if id == "mediaPrevious" || id == "mediaNext", let entry = controller.selected, !entry.media.isEmpty {
            mediaIndex = (mediaIndex + (id == "mediaNext" ? 1 : entry.media.count - 1)) % entry.media.count; render(animated: true)
        } else if id == "mediaPlay" { presentation?.togglePlayback(); render() }
        else if id == "retry" { controller.retryPendingSaves() }
        else { onAction?(id) }
    }
    func render(animated: Bool = false) {
        // Async write acknowledgments must not repaint a concealed canvas or
        // replace its bounded outgoing artwork with the gallery during close.
        guard visible || !hasPresented || retireMediaWork == nil else { return }
        if renderedSelection != controller.selected?.id { mediaIndex = 0; statusMessage = nil }
        renderedSelection = controller.selected?.id
        CATransaction.begin(); CATransaction.setDisableActions(true)
        // Projected editors and retained secondary menus own their layers.
        retireFaceWork?.cancel(); retireFaceWork = nil; retiringFace?.removeFromSuperlayer(); retiringFace = nil
        let oldFace = layer.sublayers?.first { $0.name == "archive.face" }
        let transitioning = animated && !HUDRuntimeAppearance.reduceMotion && oldFace != nil
        if transitioning { oldFace?.name = "archive.outgoingFace"; retiringFace = oldFace }
        else { oldFace?.removeFromSuperlayer() }
        let face = CALayer(); face.name = "archive.face"; face.frame = layer.bounds; layer.insertSublayer(face, above: backdrop)
        backdrop.backgroundColor = NSColor(white: dark ? 0.065 : 0.94, alpha: 0.96).cgColor
        let ink = NSColor(white: dark ? 0.94 : 0.12, alpha: 1)
        actions = []; progressFill = nil; thumbnailSlots.removeAll()
        if let id = categoryID, !controller.categories.contains(where: { $0.id == id }) { categoryID = nil; filtersUncategorized = true; scrollOffset = 0 }
        func text(_ value: String, _ rect: CGRect, _ size: CGFloat = 11, _ color: NSColor? = nil) {
            let label = CATextLayer(); label.frame = rect; label.string = value; label.font = NSFont.systemFont(ofSize: size, weight: .medium)
            label.fontSize = size; label.foregroundColor = (color ?? ink).cgColor; label.contentsScale = scale
            label.truncationMode = .end; label.isWrapped = false; face.addSublayer(label)
        }
        func bar(_ rect: CGRect, _ color: NSColor) { let bar = CALayer(); bar.frame = rect; bar.backgroundColor = color.cgColor; face.addSublayer(bar) }
        func action(_ id: String, _ title: String, _ rect: CGRect, selected: Bool = false, enabled: Bool = true) {
            actions.append(.init(id: id, title: title, rect: rect, enabled: enabled, selected: selected))
            bar(rect, selected ? accent.withAlphaComponent(0.28) : ink.withAlphaComponent(0.07))
            text(title, rect.insetBy(dx: 4, dy: 5), 10, enabled ? ink : ink.withAlphaComponent(0.3))
            (face.sublayers?.last as? CATextLayer)?.alignmentMode = .center
            HUDControlHighlightLayer.add(to: face, rect: rect, shape: .cutCorner, enabled: enabled, framed: true)
        }
        text("// " + HUDModule.archive.title, CGRect(x: 14, y: 10, width: 239, height: 22), 14)
        bar(CGRect(x: 14, y: 35, width: 372, height: 1), ink.withAlphaComponent(0.24))
        bar(CGRect(x: 14, y: 35, width: 58, height: 2), accent)
        if let entry = controller.selected {
            action("back", "‹", CGRect(x: 265, y: 5, width: 26, height: 25))
            action("moveCategory", "▾", CGRect(x: 296, y: 5, width: 26, height: 25))
            action("addMedia", "+", CGRect(x: 327, y: 5, width: 26, height: 25))
            action("delete", "×", CGRect(x: 358, y: 5, width: 26, height: 25))
            // Editors replace these labels after the incoming transition.
            text(entry.title.isEmpty ? L10n.text("Title", "标题") : entry.title,
                 Self.titleRect, 17, entry.title.isEmpty ? ink.withAlphaComponent(0.4) : ink)
            text(Self.dateString(entry.date), Self.dateRect, 10, ink.withAlphaComponent(0.55))
            if entry.body.isEmpty { text(L10n.text("Content", "内容"), bodyRect, 12, ink.withAlphaComponent(0.4)) }
            syncMedia(entry)
            if !entry.media.isEmpty {
                if let presentation { face.addSublayer(presentation.layer); presentation.layer.frame = Self.mediaRect }
                action("mediaPrevious", "‹", CGRect(x: 284, y: 270, width: 28, height: 26))
                action("mediaNext", "›", CGRect(x: 345, y: 270, width: 28, height: 26))
                text("\(mediaIndex + 1)/\(entry.media.count)", CGRect(x: 312, y: 276, width: 33, height: 16), 9)
                (face.sublayers?.last as? CATextLayer)?.alignmentMode = .center
                face.sublayers?.last?.name = "archive.media.counter"
                action("removeMedia", "×", CGRect(x: 345, y: 310, width: 28, height: 26))
                if entry.media[mediaIndex].kind != .image {
                    action("mediaPlay", presentation?.isPlaying == true ? "Ⅱ" : "▷", CGRect(x: 284, y: 310, width: 28, height: 26))
                    let duration = entry.media[mediaIndex].duration ?? 1
                    bar(CGRect(x: 24, y: 372, width: 250, height: 2), ink.withAlphaComponent(0.2))
                    let fill = CALayer(); fill.frame = CGRect(x: 24, y: 372, width: 250 * min(1, (presentation?.currentTime ?? 0) / max(0.01, duration)), height: 2)
                    fill.backgroundColor = accent.cgColor; face.addSublayer(fill); progressFill = fill
                }
                if let error = presentation?.error { text(error.localizedDescription, CGRect(x: 24, y: 347, width: 245, height: 22), 9) }
            }
        } else {
            clearMedia()
            displayedCategories = [("all", L10n.text("All", "全部")), ("uncategorized", L10n.text("Uncategorized", "未分类"))]
                + controller.categories.map { ("category:\($0.id)", $0.name) }
            displayedEntries = filtered
            gallery = CALayer(); gallery.name = "archive.gallery"; gallery.frame = Self.galleryRect; gallery.masksToBounds = true
            categoryList = CALayer(); categoryList.name = "archive.categories"
            // Include the small hover/border expansion in the horizontal clip.
            // Rows still share one bounded vertical scrolling viewport.
            categoryList.frame = Self.categoriesRect.insetBy(dx: -3, dy: -3)
            categoryList.bounds = CGRect(x: -3, y: -3, width: Self.categoriesRect.width + 6, height: Self.categoriesRect.height + 6)
            categoryList.masksToBounds = true
            face.addSublayer(gallery); face.addSublayer(categoryList)
            galleryScrollTrack.name = "archive.gallery.scrollTrack"; galleryScrollKnob.name = "archive.gallery.scrollThumb"
            galleryScrollTrack.frame = Self.galleryScrollerRect.insetBy(dx: 3, dy: 0)
            galleryScrollTrack.backgroundColor = ink.withAlphaComponent(0.14).cgColor
            galleryScrollKnob.backgroundColor = ink.withAlphaComponent(0.65).cgColor; galleryScrollKnob.cornerRadius = 2
            face.addSublayer(galleryScrollTrack); face.addSublayer(galleryScrollKnob)
            galleryNodes.removeAll(); categoryNodes.removeAll()
            layoutCollections()
            action("new", "+", Self.addRect)
            action("deleteCategory", "−", Self.removeCategoryRect, enabled: categoryID != nil)
            text(String(format: "%03d", displayedEntries.count), CGRect(x: 14, y: 344, width: 80, height: 43), 27, accent)
            text(L10n.text("Documents", "档案"), CGRect(x: 14, y: 380, width: 80, height: 20), 10)
            if displayedEntries.isEmpty { text(L10n.text("Create a document", "新建档案"), CGRect(x: 130, y: 189, width: 235, height: 30), 15) }
        }
        if controller.selected != nil, formattingField != nil {
            for (index, pair) in [("formatSize", "A↕"), ("formatFont", "Aa"), ("formatColor", ""), ("formatSpecial", "B")].enumerated() {
                let rect = CGRect(x: 24 + index * 26, y: 382, width: 22, height: 22)
                actions.append(.init(id: pair.0, title: pair.1, rect: rect))
                let plate = CALayer(); plate.frame = rect; plate.borderWidth = 0.5; plate.borderColor = accent.withAlphaComponent(0.6).cgColor
                plate.backgroundColor = NSColor(white: dark ? 0.18 : 0.82, alpha: 1).cgColor; face.addSublayer(plate)
                if pair.0 == "formatColor" { let color = CALayer(); color.frame = plate.bounds.insetBy(dx: 4, dy: 4); color.backgroundColor = formattingColor.cgColor; plate.addSublayer(color) }
                else { let label = CATextLayer(); label.frame = CGRect(x: 2, y: 3, width: 18, height: 15); label.string = pair.1; label.font = NSFont.systemFont(ofSize: 10); label.fontSize = 10; label.foregroundColor = ink.cgColor; label.contentsScale = scale; plate.addSublayer(label) }
                HUDControlHighlightLayer.add(to: face, rect: rect)
            }
        }
        if let error = statusMessage ?? controller.error {
            text(error, CGRect(x: 14, y: 421, width: controller.error == nil ? 372 : 332, height: 18), 9, .systemOrange)
            if controller.error != nil { action("retry", "↻", CGRect(x: 354, y: 416, width: 32, height: 22)) }
        }
        else if formattingField == nil { text("ENDFIELD  /  ARCHIVE", CGRect(x: 14, y: 425, width: 230, height: 13), 7, ink.withAlphaComponent(0.3)) }
        CATransaction.commit()
        if transitioning {
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 0; fade.toValue = 1; fade.duration = 0.18; face.add(fade, forKey: "archive.transition")
            if let oldFace { let exit = CABasicAnimation(keyPath: "opacity"); exit.fromValue = oldFace.presentation()?.opacity ?? 1; exit.toValue = 0; exit.duration = 0.18; oldFace.opacity = 0; oldFace.add(exit, forKey: "archive.transition") }
            let work = DispatchWorkItem { [weak self, weak oldFace] in guard let self, self.retiringFace === oldFace else { return }; oldFace?.removeFromSuperlayer(); self.retiringFace = nil; self.retireFaceWork = nil }
            retireFaceWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.2, execute: work)
        }
        loadVisibleThumbnails()
        onChange?()
    }
    private func label(_ value: String, rect: CGRect, size: CGFloat, color: NSColor, parent: CALayer, centered: Bool = false) {
        let text = CATextLayer(); text.frame = rect; text.string = value; text.font = NSFont.systemFont(ofSize: size, weight: .medium)
        text.fontSize = size; text.foregroundColor = color.cgColor; text.contentsScale = scale; text.truncationMode = .end
        if centered { text.alignmentMode = .center }; parent.addSublayer(text)
    }
    private func plate(_ rect: CGRect, color: NSColor, parent: CALayer) {
        let value = CALayer(); value.frame = rect; value.backgroundColor = color.cgColor; parent.addSublayer(value)
    }
    private func layoutCollections() {
        guard controller.selected == nil else { return }
        scrollOffset = min(max(0, CGFloat((displayedEntries.count + 1) / 2) * 122 - 9 - Self.galleryRect.height), max(0, scrollOffset))
        galleryScrollTrack.isHidden = galleryScrollThumb == nil; galleryScrollKnob.isHidden = galleryScrollThumb == nil
        if let thumb = galleryScrollThumb { galleryScrollKnob.frame = thumb.insetBy(dx: 2, dy: 0) }
        categoryScrollOffset = min(max(0, CGFloat(displayedCategories.count) * 34 - 7 - Self.categoriesRect.height), max(0, categoryScrollOffset))
        actions.removeAll { $0.id.hasPrefix("entry:") || $0.id.hasPrefix("category:") || $0.id == "all" || $0.id == "uncategorized" }
        let start = min(displayedEntries.count, Int(floor(scrollOffset / 122)) * 2)
        let end = min(displayedEntries.count, Int(ceil((scrollOffset + Self.galleryRect.height) / 122)) * 2)
        visibleGalleryEntries = []
        let wanted = Set(displayedEntries[start..<max(start, end)].map(\.id))
        for id in Array(galleryNodes.keys) where !wanted.contains(id) { galleryNodes.removeValue(forKey: id)?.removeFromSuperlayer(); thumbnailSlots.removeValue(forKey: id) }
        for index in start..<max(start, end) {
            let entry = displayedEntries[index]
            let local = CGRect(x: CGFloat(index % 2) * 143, y: CGFloat(index / 2) * 122 - scrollOffset, width: 132, height: 113)
            let hit = local.offsetBy(dx: Self.galleryRect.minX, dy: Self.galleryRect.minY).intersection(Self.galleryRect)
            guard !hit.isNull, hit.height >= 2 else { galleryNodes.removeValue(forKey: entry.id)?.removeFromSuperlayer(); thumbnailSlots.removeValue(forKey: entry.id); continue }
            let node: CALayer
            if let retained = galleryNodes[entry.id] { node = retained }
            else {
                node = CALayer(); node.name = "archive.card.\(entry.id)"; node.bounds = CGRect(x: 0, y: 0, width: 132, height: 113)
                node.backgroundColor = NSColor(white: dark ? 0.86 : 1, alpha: 0.93).cgColor
                plate(CGRect(x: 5, y: 5, width: 18, height: 3), color: .black, parent: node)
                plate(CGRect(x: 5, y: 5, width: 3, height: 17), color: .black, parent: node)
                let fallback = CALayer(); fallback.frame = CGRect(x: 4, y: 4, width: 124, height: 52); node.addSublayer(fallback)
                EndfieldGameIcon.archive.add(to: fallback, rect: CGRect(x: 43, y: 8, width: 39, height: 39), tint: .darkGray, contentsScale: scale)
                let poster = CALayer(); poster.frame = fallback.frame; poster.contentsGravity = .resizeAspectFill; poster.masksToBounds = true; node.addSublayer(poster)
                thumbnailSlots[entry.id] = (poster, fallback)
                setThumbnail(thumbnails.image(for: entry.id, reference: entry.thumbnail), for: entry.id)
                plate(CGRect(x: 4, y: 58, width: 124, height: 23), color: .black.withAlphaComponent(0.85), parent: node)
                label(entry.title.isEmpty ? L10n.text("Untitled", "未命名") : entry.title, rect: CGRect(x: 10, y: 63, width: 112, height: 18), size: 10, color: .white, parent: node)
                label(Self.dateString(entry.date), rect: CGRect(x: 9, y: 89, width: 115, height: 18), size: 9, color: .darkGray, parent: node)
                HUDControlHighlightLayer.add(to: node, rect: node.bounds, shape: .cutCorner, framed: true)
                gallery.addSublayer(node); galleryNodes[entry.id] = node
            }
            node.frame = local; visibleGalleryEntries.append(entry)
            actions.append(.init(id: "entry:\(entry.id)", title: entry.title.isEmpty ? L10n.text("Untitled", "未命名") : entry.title, rect: hit))
        }
        let firstCategory = min(displayedCategories.count, Int(floor(categoryScrollOffset / 34)))
        let lastCategory = min(displayedCategories.count, Int(ceil((categoryScrollOffset + Self.categoriesRect.height) / 34)))
        let categoryIDs = Set(displayedCategories[firstCategory..<max(firstCategory, lastCategory)].map { $0.0 })
        for id in Array(categoryNodes.keys) where !categoryIDs.contains(id) { categoryNodes.removeValue(forKey: id)?.removeFromSuperlayer() }
        let ink = NSColor(white: dark ? 0.94 : 0.12, alpha: 1)
        for index in firstCategory..<max(firstCategory, lastCategory) {
            let pair = displayedCategories[index]
            let local = CGRect(x: 0, y: CGFloat(index) * 34 - categoryScrollOffset, width: 75, height: 27)
            let hit = local.offsetBy(dx: Self.categoriesRect.minX, dy: Self.categoriesRect.minY).intersection(Self.categoriesRect)
            guard !hit.isNull, hit.height >= 2 else { categoryNodes.removeValue(forKey: pair.0)?.removeFromSuperlayer(); continue }
            let selected = pair.0 == "all" ? categoryID == nil && !filtersUncategorized
                : pair.0 == "uncategorized" ? filtersUncategorized : pair.0 == "category:\(categoryID?.uuidString ?? "")"
            let node: CALayer
            if let retained = categoryNodes[pair.0] { node = retained }
            else {
                node = CALayer(); node.name = "archive.category.\(pair.0)"; node.bounds = CGRect(x: 0, y: 0, width: 75, height: 27)
                node.backgroundColor = (selected ? accent.withAlphaComponent(0.28) : ink.withAlphaComponent(0.07)).cgColor
                let measured = (pair.1 as NSString).size(withAttributes: [.font: NSFont.systemFont(ofSize: 10, weight: .medium)]).width
                let fontSize = min(10, max(8, 10 * (node.bounds.width - 8) / max(1, measured)))
                label(pair.1, rect: node.bounds.insetBy(dx: 4, dy: 5), size: fontSize, color: ink, parent: node, centered: true)
                HUDControlHighlightLayer.add(to: node, rect: node.bounds, shape: .cutCorner, framed: true)
                categoryList.addSublayer(node); categoryNodes[pair.0] = node
            }
            node.frame = local; actions.append(.init(id: pair.0, title: pair.1, rect: hit, selected: selected))
        }
    }
    private func setThumbnail(_ image: CGImage?, for id: UUID) {
        guard let (poster, fallback) = thumbnailSlots[id] else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        poster.contents = image; fallback.isHidden = image != nil
        CATransaction.commit()
    }
    private func loadVisibleThumbnails() {
        guard visible, controller.selected == nil else { thumbnails.setWanted([]); wantedThumbnailIDs.removeAll(); visibleThumbnailReferences.removeAll(); return }
        // At fractional offsets up to eight cards can touch the clip; decode
        // only the six most visible thumbnails, retaining the existing bound.
        let summaries = visibleGalleryEntries.sorted {
            let a = galleryNodes[$0.id]?.frame.intersection(gallery.bounds).height ?? 0
            let b = galleryNodes[$1.id]?.frame.intersection(gallery.bounds).height ?? 0
            return a == b ? $0.id.uuidString < $1.id.uuidString : a > b
        }.prefix(6)
        wantedThumbnailIDs = Set(summaries.map(\.id))
        visibleThumbnailReferences = visibleThumbnailReferences.filter { wantedThumbnailIDs.contains($0.key) }
        thumbnails.setWanted(summaries.compactMap { item in (item.thumbnail ?? visibleThumbnailReferences[item.id]).map { (item.id, $0) } })
        let generation = thumbnailGeneration
        for item in summaries where item.mediaCount > 0 && item.thumbnail == nil && visibleThumbnailReferences[item.id] == nil && !metadataRequests.contains(item.id) && !thumbnails.contains(item.id) {
            metadataRequests.insert(item.id)
            controller.loadThumbnail(for: item.id) { [weak self] reference in
                guard let self, self.visible, self.thumbnailGeneration == generation else { return }
                self.metadataRequests.remove(item.id)
                if let reference, self.wantedThumbnailIDs.contains(item.id), self.thumbnailSlots[item.id] != nil {
                    self.visibleThumbnailReferences[item.id] = reference; self.thumbnails.request(item.id, reference: reference)
                }
            }
        }
    }
    private func clearMedia() {
        mediaRenderWork?.cancel(); mediaRenderWork = nil
        let old = presentation; presentation = nil; presentationKey = nil
        old?.onStateChange = nil; old?.onProgress = nil; old?.dispose()
    }
    private func syncMedia(_ entry: ArchiveEntry) {
        guard visible, !entry.media.isEmpty else { clearMedia(); return }
        mediaIndex = min(max(0, mediaIndex), entry.media.count - 1)
        let reference = entry.media[mediaIndex], key = "\(entry.id):\(mediaIndex):\(reference.bookmark.hashValue)"
        guard key != presentationKey else { return }
        clearMedia(); presentationKey = key
        let media = NotesMediaPresentation(reference: reference, maximumDimension: 512); presentation = media
        media.onStateChange = { [weak self, weak media] in
            guard let self, let media, self.visible, self.presentation === media, self.mediaRenderWork == nil else { return }
            // Loading fires synchronously from setVisible. Defer and coalesce
            // state paints so they cannot re-enter construction of this face.
            let work = DispatchWorkItem { [weak self, weak media] in
                guard let self else { return }; self.mediaRenderWork = nil
                guard self.visible, let media, self.presentation === media else { return }; self.render()
            }
            self.mediaRenderWork = work; DispatchQueue.main.async(execute: work)
        }
        media.onProgress = { [weak self] in self?.updateProgress() }; media.setVisible(true)
    }
    private func updateProgress() {
        guard let presentation, let duration = presentation.reference.duration, let progressFill else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        progressFill.frame.size.width = 250 * min(1, max(0, presentation.currentTime / max(0.01, duration)))
        CATransaction.commit()
    }
    static func dateString(_ date: Date) -> String {
        dateFormatter().string(from: date)
    }
    static func parseDate(_ string: String) -> Date? {
        let formatter = dateFormatter()
        guard let date = formatter.date(from: string), formatter.string(from: date) == string else { return nil }
        return date
    }
    private static func dateFormatter() -> DateFormatter {
        let formatter = DateFormatter(); formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.calendar = Calendar(identifier: .gregorian); formatter.dateFormat = "yyyy-MM-dd"; formatter.isLenient = false
        return formatter
    }
}

/// Static posters only: at most six waiting requests, one native decode and a
/// twelve-image LRU. Paging replaces pending work; hiding discards every bitmap.
final class ArchiveGalleryThumbnails {
    var onChange: ((UUID, CGImage?) -> Void)?
    private struct Cached { let reference: NotesMediaReference; let image: CGImage? }
    private let queue = DispatchQueue(label: "EndfieldHUD.ArchivePosters", qos: .utility)
    private let decode: (NotesMediaReference) -> CGImage?
    private var cache: [UUID: Cached] = [:], order: [UUID] = []
    private var wanted: [UUID: NotesMediaReference] = [:], pending: [UUID: NotesMediaReference] = [:]
    private var working = false, generation = 0
    private var inFlight: (UUID, NotesMediaReference)?
    var count: Int { cache.count }
    var pendingCount: Int { pending.count }
    init(decode: @escaping (NotesMediaReference) -> CGImage? = { try? NotesMediaFactory.thumbnail(for: $0, maximumDimension: 256) }) { self.decode = decode }
    func contains(_ id: UUID) -> Bool { cache[id] != nil }
    func image(for id: UUID, reference: NotesMediaReference?) -> CGImage? {
        guard let cached = cache[id], reference == nil || cached.reference == reference else { return nil }
        return cached.image
    }
    func setWanted(_ values: [(UUID, NotesMediaReference)]) {
        wanted = Dictionary(values.prefix(6), uniquingKeysWith: { _, last in last })
        pending = pending.filter { wanted[$0.key] == $0.value }
        for (id, reference) in wanted where cache[id]?.reference != reference && !(inFlight?.0 == id && inFlight?.1 == reference) { pending[id] = reference }
        pump()
    }
    func request(_ id: UUID, reference: NotesMediaReference) {
        guard wanted[id] != nil || wanted.count < 6 else { return }
        wanted[id] = reference
        if let cached = cache[id], cached.reference == reference { onChange?(id, cached.image); return }
        if inFlight?.0 != id || inFlight?.1 != reference { pending[id] = reference }; pump()
    }
    func clear() { generation += 1; inFlight = nil; wanted.removeAll(); pending.removeAll(); cache.removeAll(); order.removeAll() }
    private func pump() {
        guard !working, let next = pending.first else { return }
        pending.removeValue(forKey: next.key); working = true; inFlight = (next.key, next.value)
        let captured = generation, decode = self.decode
        queue.async { [weak self] in
            let image = decode(next.value)
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }; self.working = false; self.inFlight = nil
                if self.generation == captured, self.wanted[next.key] == next.value {
                    self.cache[next.key] = Cached(reference: next.value, image: image)
                    self.order.removeAll { $0 == next.key }; self.order.append(next.key)
                    while self.order.count > 12 { self.cache.removeValue(forKey: self.order.removeFirst()) }
                    self.onChange?(next.key, image)
                }
                self.pump()
            }
        }
    }
}
