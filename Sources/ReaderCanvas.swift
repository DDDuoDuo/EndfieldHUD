import AppKit
import QuartzCore

final class ReaderCanvas: NSObject, HUDModuleContentFactory {
    struct Action { let id: String; let label: String; let rect: CGRect; let enabled: Bool }
    let layer = CALayer()
    let controller: ReaderController
    var onChange: (() -> Void)?
    var onMenu: ((String) -> Void)?
    var onEvent: ((ReaderEvent) -> Void)?
    private(set) var accessibleActions: [Action] = []
    let viewport = CGRect(x: 12, y: 48, width: 376, height: 334)
    var progressRect: CGRect { canZoom ? CGRect(x: 165, y: 416, width: 171, height: 22) : CGRect(x: 55, y: 389, width: 290, height: 22) }
    var isDragging: Bool { dragProgress != nil || panStart != nil }
    private(set) var imageView = ReaderImageView()
    var canZoom: Bool { controller.current?.isIllustration == true }
    private(set) var pageTurnSequence = 0
    var isTurningPage: Bool { pageClip.animation(forKey: kCATransition) != nil }
    var scrollOffsetForVerification: CGFloat { offset }
    var accessibilityText: String { controller.error ?? controller.current?.summary ?? L10n.text("Open a book to begin reading.", "打开书籍开始阅读。") }
    private let controls = CALayer(), pageClip = CALayer(), title = CATextLayer(), status = CATextLayer(), percent = CATextLayer()
    private let pictures = [CALayer(), CALayer(), CALayer()]
    private let rail = CALayer(), fill = CALayer(), thumb = CALayer()
    private var dark = true, active = false
    private var artworkPrepared = false
    private var anchor: ReaderLocation?, anchorBook: UUID?, offset: CGFloat = 0, dragProgress: Double?
    private var pendingImageView: ReaderImageView?
    private var pendingOffset: CGFloat?, turnDirection = 0
    private var panStart: (point: CGPoint, pan: CGPoint)?
    private var detailWork: DispatchWorkItem?
    private var horizontalRemainder: CGFloat = 0, turnConsumed = false
    private var lastWheelTurn: TimeInterval = 0
    private var releaseArtwork: DispatchWorkItem?
    private var renderScale: CGFloat = 2
    init(controller: ReaderController) {
        self.controller = controller; super.init()
        layer.bounds = CGRect(x: 0, y: 0, width: 400, height: 440); layer.name = "reader"
        for item in [pageClip, controls, title, status, percent, rail, fill, thumb] {
            item.actions = ["position": NSNull(), "bounds": NSNull(), "contents": NSNull(), "opacity": NSNull()]; layer.addSublayer(item)
        }
        pageClip.frame = viewport; pageClip.masksToBounds = true; pageClip.cornerRadius = 3
        for image in pictures { image.contentsGravity = .resize; image.actions = ["contents": NSNull(), "position": NSNull(), "bounds": NSNull()]; pageClip.addSublayer(image) }
        controller.onChange = { [weak self] in self?.paint() }
        controller.onEvent = { [weak self] in self?.onEvent?($0) }
    }
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        artworkPrepared = true; dark = style.dark; renderScale = style.contentsScale; controller.setLayout(size: viewport.size, dark: dark); paint(); return layer
    }
    func updateRenderScale(_ scale: CGFloat) {
        guard scale.isFinite else { return }
        let value = min(3, max(1, scale)); guard renderScale != value else { return }
        renderScale = value; paint()
    }
    func activate() { releaseArtwork?.cancel(); releaseArtwork = nil; active = true; artworkPrepared = true; controller.setActive(true); paint() }
    func deactivate() {
        active = false; dragProgress = nil; panStart = nil; detailWork?.cancel(); detailWork = nil; offset = 0; imageView = ReaderImageView(); controller.setActive(false)
        removeAnimations(layer)
        let work = DispatchWorkItem { [weak self] in
            guard let self, !self.active else { return }; self.pictures.forEach { $0.contents = nil }; self.releaseArtwork = nil
        }
        releaseArtwork?.cancel(); releaseArtwork = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.35, execute: work)
    }
    func perform(_ id: String) {
        switch id {
        case "previous": turnPage(-1)
        case "next": turnPage(1)
        case "zoomIn": zoom(by: 1.3, at: CGPoint(x: viewport.midX, y: viewport.midY))
        case "zoomOut": zoom(by: 1 / 1.3, at: CGPoint(x: viewport.midX, y: viewport.midY))
        case "zoomReset": setImageView(ReaderImageView())
        case "bookmark": controller.toggleBookmark()
        default: onMenu?(id)
        }
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        if progressRect.contains(point), controller.current != nil { dragProgress = fraction(point); paintProgress(); return true }
        if let action = accessibleActions.first(where: { $0.enabled && $0.rect.contains(point) }) { perform(action.id); return true }
        if viewport.contains(point), canZoom, imageView.zoom > 1 { panStart = (point, imageView.pan) }
        return viewport.contains(point)
    }
    func mouseDragged(to point: CGPoint) {
        if dragProgress != nil { dragProgress = fraction(point); paintProgress() }
        else if let panStart {
            var view = imageView; view.pan = CGPoint(x: panStart.pan.x + point.x - panStart.point.x, y: panStart.pan.y + point.y - panStart.point.y)
            setImageView(view)
        }
    }
    func mouseUp() {
        if let value = dragProgress { dragProgress = nil; turnDirection = 0; setImageView(ReaderImageView()); controller.jump(progress: value) }
        panStart = nil
    }
    // Retained for programmatic vertical navigation and existing integrations.
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        scroll(at: point, deltaX: 0, deltaY: delta)
    }
    @discardableResult func scroll(at point: CGPoint, deltaX: CGFloat, deltaY: CGFloat,
                                    phase: NSEvent.Phase = [], momentumPhase: NSEvent.Phase = [],
                                    precision: Bool = true, zoomModifier: Bool = false,
                                    timestamp: TimeInterval = ProcessInfo.processInfo.systemUptime) -> Bool {
        guard viewport.contains(point), controller.current != nil, deltaX.isFinite, deltaY.isFinite else { return false }
        if zoomModifier, canZoom { zoom(by: exp(-deltaY * 0.012), at: point); return true }
        if !controller.preferences.vertical, imageView.zoom > 1, abs(deltaY) >= abs(deltaX) / 1.1 {
            // Enlarged horizontal pages still need access to their lower area.
            // Vertical gestures pan only; page turns remain horizontal/arrows.
            var view = imageView; view.pan.x -= deltaX; view.pan.y -= deltaY; setImageView(view); return true
        }
        if controller.preferences.vertical {
            // Trackpad momentum supplies the curve; native scroll events move
            // cached page layers directly without a page-turn crossfade.
            if imageView.zoom > 1 {
                scrollZoomed(deltaX: deltaX, deltaY: deltaY); return true
            }
            offset = min(viewport.height * 2, max(-viewport.height, offset + deltaY))
            if offset >= viewport.height, controller.current?.next != nil {
                pendingOffset = offset - viewport.height; turnDirection = 0; controller.next()
            } else if offset < 0, controller.current?.previous != nil {
                pendingOffset = viewport.height + offset; turnDirection = 0; controller.previous()
            } else {
                offset = min(controller.current?.next == nil ? 0 : viewport.height, max(0, offset)); positionPages(animated: !precision)
            }
        } else {
            if phase.contains(.began) { horizontalRemainder = 0; turnConsumed = false }
            if !momentumPhase.isEmpty { return true }
            if phase.contains(.ended) || phase.contains(.cancelled) { horizontalRemainder = 0; return true }
            // Vertical wheel motion never turns a horizontal reader page.
            guard abs(deltaX) > abs(deltaY) * 1.1 else { return true }
            if phase.isEmpty, timestamp - lastWheelTurn >= 0.24 { turnConsumed = false }
            guard !turnConsumed else { return true }
            horizontalRemainder += deltaX
            if abs(horizontalRemainder) >= (precision ? 60 : 24) {
                turnConsumed = true; lastWheelTurn = timestamp
                turnPage(horizontalRemainder > 0 ? 1 : -1); horizontalRemainder = 0
            }
        }
        return true
    }
    private func scrollZoomed(deltaX: CGFloat, deltaY: CGFloat) {
        let pageHeight = viewport.height * imageView.zoom, extent = (pageHeight - viewport.height) / 2
        // The current page's scroll position may extend one viewport beyond
        // its clamped pan while a cached neighboring page enters the viewport.
        var position = extent - imageView.pan.y + offset + min(viewport.height * 2, max(-viewport.height * 2, deltaY))
        var target = imageView; target.pan.x -= deltaX
        var direction = 0
        if position >= pageHeight, controller.current?.next != nil { position -= pageHeight; direction = 1 }
        else if position < 0, controller.current?.previous != nil { position += pageHeight; direction = -1 }
        else if controller.current?.next == nil { position = min(pageHeight - viewport.height, position) }
        position = max(0, min(pageHeight - 0.001, position))
        let inside = min(pageHeight - viewport.height, position)
        target.pan.y = extent - inside
        if direction != 0 {
            pendingImageView = target.clamped(to: viewport.size); pendingOffset = position - inside; turnDirection = 0
            if direction > 0 { controller.next() } else { controller.previous() }
        } else { setImageView(target, scrollOffset: position - inside) }
    }
    func turnPage(_ direction: Int) {
        guard direction > 0 ? controller.current?.next != nil : controller.current?.previous != nil else { return }
        turnDirection = controller.preferences.vertical ? 0 : direction
        pendingOffset = nil; pendingImageView = nil
        if canZoom, imageView.zoom > 1 {
            var next = imageView
            if controller.preferences.vertical { next.pan.y = viewport.height * (next.zoom - 1) / 2 * (direction > 0 ? 1 : -1) }
            pendingImageView = next
        }
        if direction > 0 { controller.next() } else { controller.previous() }
    }
    @discardableResult func magnify(at point: CGPoint, amount: CGFloat) -> Bool {
        guard viewport.contains(point), canZoom, amount.isFinite else { return false }
        zoom(by: max(0.05, 1 + amount), at: point); return true
    }
    private func zoom(by amount: CGFloat, at point: CGPoint) {
        guard canZoom, amount.isFinite else { return }
        let old = imageView.zoom, next = min(12, max(1, old * amount)), ratio = next / old
        let point = CGPoint(x: point.x - viewport.midX, y: point.y - viewport.midY)
        var view = imageView; view.zoom = next
        view.pan = CGPoint(x: point.x - (point.x - view.pan.x) * ratio, y: point.y - (point.y - view.pan.y) * ratio)
        setImageView(view)
    }
    private func setImageView(_ value: ReaderImageView, scrollOffset: CGFloat = 0) {
        let value = value.clamped(to: viewport.size)
        guard value != imageView || offset != scrollOffset else { return }
        let changed = value != imageView
        imageView = value; offset = scrollOffset; pendingOffset = nil; pendingImageView = nil
        if changed { controller.cancelImageDetail(); scheduleImageDetail() }
        positionPages(animated: false)
    }
    private func scheduleImageDetail() {
        detailWork?.cancel(); detailWork = nil
        guard imageView.zoom > 1 else { return }
        let value = imageView
        // One debounced, viewport-sized detail raster after interaction;
        // dragging never queues a render per pointer event.
        let work = DispatchWorkItem { [weak self] in
            guard let self, self.active, self.imageView == value else { return }
            self.detailWork = nil; self.controller.requestImageDetail(value)
        }
        detailWork = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.12, execute: work)
    }
    private func paint() {
        guard artworkPrepared else { return }
        if !active, anchor != nil { onChange?(); return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        let ink = NSColor(white: dark ? 0.93 : 0.08, alpha: 1), changed = anchor != controller.current?.location
        let animateTurn = changed && anchor != nil && controller.current != nil && turnDirection != 0
        if changed {
            let sameBook = anchorBook == controller.book?.id && anchor != nil
            offset = pendingOffset ?? 0; pendingOffset = nil
            imageView = sameBook && canZoom ? (pendingImageView ?? imageView) : ReaderImageView()
            pendingImageView = nil; anchor = controller.current?.location; anchorBook = controller.book?.id
            panStart = nil; scheduleImageDetail()
        }
        controls.sublayers?.forEach { $0.removeFromSuperlayer() }
        let hasBook = controller.current != nil
        let bookmarked = controller.book?.bookmarks.contains { $0.location == controller.current?.location } == true
        accessibleActions = [
            Action(id: "open", label: L10n.text("Open", "打开"), rect: CGRect(x: 12, y: 8, width: 64, height: 30), enabled: true),
            Action(id: "library", label: L10n.text("Library", "书库"), rect: CGRect(x: 85, y: 8, width: 64, height: 30), enabled: true),
            Action(id: "settings", label: L10n.text("Reading", "阅读设置"), rect: CGRect(x: 158, y: 8, width: 84, height: 30), enabled: true),
            Action(id: "bookmark", label: bookmarked ? "★" : "☆", rect: CGRect(x: 251, y: 8, width: 36, height: 30), enabled: hasBook),
            Action(id: "bookmarks", label: L10n.text("Bookmarks", "书签"), rect: CGRect(x: 296, y: 8, width: 92, height: 30), enabled: hasBook),
            Action(id: "previous", label: "‹", rect: CGRect(x: 12, y: 389, width: 30, height: 24), enabled: controller.current?.previous != nil),
            Action(id: "next", label: "›", rect: CGRect(x: 358, y: 389, width: 30, height: 24), enabled: controller.current?.next != nil)]
        if canZoom {
            accessibleActions += [Action(id: "zoomOut", label: "−", rect: CGRect(x: 154, y: 389, width: 25, height: 24), enabled: true),
                Action(id: "zoomReset", label: "1:1", rect: CGRect(x: 183, y: 389, width: 35, height: 24), enabled: true),
                Action(id: "zoomIn", label: "+", rect: CGRect(x: 222, y: 389, width: 25, height: 24), enabled: true)]
        }
        for action in accessibleActions {
            let plate = CALayer(); plate.frame = action.rect
            plate.backgroundColor = ink.withAlphaComponent(0.07).cgColor; controls.addSublayer(plate)
            HUDControlHighlightLayer.add(to: controls, rect: action.rect, shape: .cutCorner, enabled: action.enabled, framed: true)
            let text = CATextLayer(); configure(text, action.label, rect: CGRect(x: action.rect.minX + 3, y: action.rect.midY - 7,
                width: action.rect.width - 6, height: 14), size: 11, color: ink.withAlphaComponent(action.enabled ? 1 : 0.3)); text.alignmentMode = .center; controls.addSublayer(text)
        }
        pageClip.backgroundColor = NSColor(white: dark ? 0.07 : 0.95, alpha: 0.85).cgColor
        if let current = controller.current {
            pictures[0].contents = current.image
            pictures[1].contents = controller.pages.first { $0.location == current.next }?.image
            pictures[2].contents = controller.pages.first { $0.location == current.previous }?.image
        } else { pictures.forEach { $0.contents = nil } }
        positionPages(animated: false)
        configure(title, controller.book?.title ?? L10n.text("E-Reader", "阅读器"), rect: CGRect(x: 12, y: 421, width: canZoom ? 142 : 304, height: 16), size: 10, color: ink.withAlphaComponent(0.7))
        configure(percent, "\(Int((controller.current?.progress ?? 0) * 100))%", rect: CGRect(x: 347, y: 421, width: 41, height: 16), size: 10, color: ink.withAlphaComponent(0.7)); percent.alignmentMode = .right
        let info = controller.error ?? (controller.loading ? L10n.text("Loading book…", "正在载入书籍…") : controller.current == nil ? L10n.text("Open a book to begin reading.", "打开书籍开始阅读。") : "")
        configure(status, info, rect: CGRect(x: 27, y: 185, width: 346, height: 70), size: 12, color: ink); status.isWrapped = true; status.alignmentMode = .center
        status.isHidden = info.isEmpty; paintProgress()
        if controller.preferences.vertical { pageClip.removeAnimation(forKey: kCATransition) }
        if animateTurn, active, !HUDRuntimeAppearance.reduceMotion {
            let flip = CATransition(); flip.type = .push; flip.subtype = turnDirection > 0 ? .fromRight : .fromLeft
            flip.duration = 0.26; flip.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            pageClip.add(flip, forKey: kCATransition); pageTurnSequence += 1
        }
        if changed { turnDirection = 0 }
        CATransaction.commit(); onChange?()
    }
    private func positionPages(animated: Bool) {
        let detailed = controller.imageDetail.flatMap { $0.view == imageView && $0.page.location == controller.current?.location ? $0.page : nil }
        if let current = controller.current { pictures[0].contents = detailed?.image ?? current.image }
        for (index, picture) in pictures.enumerated() {
            let neighbor = index == 0 ? CGFloat(0) : index == 1 ? CGFloat(1) : CGFloat(-1)
            let old = picture.presentation()?.position ?? picture.position
            CATransaction.begin(); CATransaction.setDisableActions(true)
            picture.isHidden = index != 0 && !controller.preferences.vertical
            if imageView.zoom > 1 {
                if index == 0, detailed != nil {
                    picture.frame = CGRect(x: 0, y: -offset, width: viewport.width, height: viewport.height)
                } else {
                    picture.frame = CGRect(x: viewport.width * (1 - imageView.zoom) / 2 + imageView.pan.x,
                        y: viewport.height * (1 - imageView.zoom) / 2 + imageView.pan.y + neighbor * viewport.height * imageView.zoom - offset,
                        width: viewport.width * imageView.zoom, height: viewport.height * imageView.zoom)
                }
            } else { picture.frame = CGRect(x: 0, y: neighbor * viewport.height - offset, width: viewport.width, height: viewport.height) }
            CATransaction.commit()
            if animated, !HUDRuntimeAppearance.reduceMotion {
                let slide = CABasicAnimation(keyPath: "position"); slide.fromValue = old; slide.toValue = picture.position
                slide.duration = 0.10; slide.timingFunction = CAMediaTimingFunction(name: .easeOut); picture.add(slide, forKey: "reader.scroll")
            }
        }
    }
    private func paintProgress() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        let value = dragProgress ?? controller.current?.progress ?? 0
        rail.frame = CGRect(x: progressRect.minX, y: progressRect.midY, width: progressRect.width, height: 2)
        rail.backgroundColor = NSColor.gray.withAlphaComponent(0.4).cgColor
        fill.frame = CGRect(x: progressRect.minX, y: progressRect.midY, width: progressRect.width * value, height: 2)
        fill.backgroundColor = HUDRuntimeAppearance.accent.cgColor
        thumb.frame = CGRect(x: progressRect.minX + progressRect.width * value - 2.5, y: progressRect.midY - 3, width: 5, height: 8)
        thumb.backgroundColor = NSColor.white.cgColor; thumb.isHidden = controller.current == nil
        CATransaction.commit()
    }
    private func fraction(_ point: CGPoint) -> Double { Double(min(1, max(0, (point.x - progressRect.minX) / progressRect.width))) }
    private func configure(_ label: CATextLayer, _ text: String, rect: CGRect, size: CGFloat, color: NSColor) {
        label.frame = rect; label.string = text; label.font = NSFont.systemFont(ofSize: size, weight: .semibold)
        label.fontSize = size; label.foregroundColor = color.cgColor; label.contentsScale = renderScale; label.truncationMode = .end
    }
    private func removeAnimations(_ layer: CALayer) { layer.removeAllAnimations(); layer.sublayers?.forEach { removeAnimations($0) } }
}
