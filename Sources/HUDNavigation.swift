import AppKit
import QuartzCore

/// Persistent vector navigation in the shared 1000 × 640 design space.
/// The host owns accessible NSButtons, input routing, and the common depth plane.
final class HUDNavigation {
    static let selectionTransitionDuration: TimeInterval = 0.22
    let layer = CALayer()
    /// The bottom sectors share the central instrument's depth and parallax.
    /// Both navigation roots retain the same design-space coordinates.
    let bottomLayer = CALayer()
    private(set) var entries: [HUDNavigationEntry]
    private(set) var selectedModule: HUDModule
    var onLayoutChange: (() -> Void)?
    let rightViewport = CGRect(x: 754, y: 150, width: 244, height: 370)
    let scrollUpRect = CGRect(x: 860, y: 124, width: 36, height: 20)
    let scrollDownRect = CGRect(x: 860, y: 530, width: 36, height: 20)
    private(set) var rightScrollOffset: CGFloat = 0
    var maxRightScrollOffset: CGFloat {
        max(0, CGFloat((rightEntries.count - 1) / 2) * 90 + 74 + 24 + 18 - rightViewport.height)
    }
    private(set) var isScrollGestureActive = false
    var canScrollUp: Bool { rightScrollOffset > 0.01 }
    var canScrollDown: Bool { rightScrollOffset < maxRightScrollOffset - 0.01 }
    var visibleEntries: [HUDNavigationEntry] { entries.filter(\.isVisible) }
    private var hoveredTarget: HUDNavigationTarget?
    private var rawScrollOffset: CGFloat = 0
    private var ownsMomentum = false
    // Finger-up and its inertial tail share one boundary rebound. Once the
    // spring owns that release, residual momentum must not grab the cards back.
    private var suppressesMomentum = false
    private struct LayoutState: Equatable {
        let visible: Set<HUDNavigationTarget>
        let up: Bool
        let down: Bool
    }
    private var lastLayoutState: LayoutState?
    private var scrollAnimationGeneration = 0
    private let rightClip = CALayer()
    private let scrollUp = CAShapeLayer()
    private let scrollDown = CAShapeLayer()
    private var scrollHighlights: [Bool: HUDControlHighlightLayer] = [:]
    private var dark = true
    private var accent = NSColor(srgbRed: 0.98, green: 0.83, blue: 0.12, alpha: 1)
    private var contentsScale: CGFloat = 2
    private var rightEntries: [HUDNavigationEntry] { entries.filter { $0.group == .right } }
    private static let rightModules: [HUDModule] = [.notes, .fileShelf, .clipboard, .volume, .workMode, .eventLog, .map, .addApp]

    init(selected: HUDModule = .power) {
        selectedModule = selected
        var items: [(HUDModule, CGRect)] = []
        let left: [HUDModule] = [.system, .display, .hotkeys, .about]
        for (index, module) in Self.rightModules.enumerated() {
            items.append((module, Self.rightRect(index: index, offset: 0)))
        }
        let leftOffsets = [54, 16, 22, 60]
        for (index, module) in left.enumerated() {
            items.append((module, CGRect(x: leftOffsets[index], y: 147 + index * 95, width: 180, height: 84)))
        }
        items.append((.storage, CGRect(x: 350, y: 463, width: 144, height: 65)))
        items.append((.activityMonitor, CGRect(x: 506, y: 463, width: 144, height: 65)))
        items.append((.power, HUDChargeBadge.compactHitRect))
        items.append((.profile, CGRect(x: 78, y: 564, width: 240, height: 82)))
        entries = items.map { HUDNavigationEntry(module: $0.0, rect: $0.1) }
        layer.name = "hud.navigation"
        layer.frame = CGRect(x: 0, y: 0, width: 1000, height: 640)
        layer.masksToBounds = false
        layer.allowsGroupOpacity = false
        layer.shouldRasterize = false
        bottomLayer.name = "hud.navigation.bottom"
        bottomLayer.frame = layer.bounds
        bottomLayer.masksToBounds = false
        bottomLayer.allowsGroupOpacity = false
        bottomLayer.shouldRasterize = false
        rightClip.name = "hud.navigation.rightViewport"
        rightClip.frame = layer.bounds
        rightClip.allowsGroupOpacity = false
        let clip = CAGradientLayer()
        clip.name = "navigation.scrollFeather"
        clip.frame = rightViewport
        clip.startPoint = CGPoint(x: 0.5, y: 0)
        clip.endPoint = CGPoint(x: 0.5, y: 1)
        clip.locations = [0, NSNumber(value: 28 / Double(rightViewport.height)),
                          NSNumber(value: 1 - 42 / Double(rightViewport.height)), 1]
        clip.colors = [NSColor.clear.cgColor, NSColor.black.cgColor,
                       NSColor.black.cgColor, NSColor.clear.cgColor]
        rightClip.mask = clip
        layer.addSublayer(rightClip)
        for entry in entries {
            if entry.group == .right { entry.clipRect = rightViewport }
            let parent = entry.group == .bottom ? bottomLayer : (entry.group == .right ? rightClip : layer)
            parent.addSublayer(entry.layer)
            // These entries keep native navigation semantics; dedicated shell
            // renderers own the charge capsule and identity card artwork.
            entry.layer.isHidden = entry.module == .power || entry.module == .profile
            entry.setSelected(entry.module == selected, animated: false)
        }
        for (shape, rect, up) in [(scrollUp, scrollUpRect, true), (scrollDown, scrollDownRect, false)] {
            shape.name = up ? "navigation.scrollUp" : "navigation.scrollDown"
            shape.frame = rect
            let path = CGMutablePath()
            for offset in [CGFloat(0), 5] {
                let points = [CGPoint(x: 9, y: 9 + offset), CGPoint(x: 18, y: 2 + offset), CGPoint(x: 27, y: 9 + offset)]
                for (index, point) in points.enumerated() {
                    let mirrored = CGPoint(x: point.x, y: up ? point.y : rect.height - point.y)
                    if index == 0 { path.move(to: mirrored) } else { path.addLine(to: mirrored) }
                }
            }
            let baseline = CAShapeLayer()
            baseline.name = "navigation.scrollBaseline"
            baseline.frame = shape.bounds
            let line = CGMutablePath()
            let lineY: CGFloat = up ? rect.height - 1 : 1
            line.move(to: CGPoint(x: rightViewport.minX - rect.minX, y: lineY))
            line.addLine(to: CGPoint(x: rightViewport.maxX - rect.minX, y: lineY))
            baseline.path = line; baseline.fillColor = nil; baseline.lineWidth = 0.65
            let echo = CAShapeLayer()
            echo.name = "navigation.scrollEcho"
            echo.frame = shape.bounds.offsetBy(dx: -1.5, dy: 3)
            echo.path = path; echo.fillColor = nil; echo.lineWidth = 2.2
            echo.lineJoin = .miter; echo.lineCap = .butt
            let glyph = CAShapeLayer()
            glyph.name = "navigation.scrollChevron"
            glyph.frame = shape.bounds
            glyph.path = path; glyph.fillColor = nil; glyph.lineWidth = 2.2
            glyph.lineJoin = .miter; glyph.lineCap = .butt
            for item in [baseline, echo, glyph] { shape.addSublayer(item) }
            layer.addSublayer(shape)
            scrollHighlights[up] = HUDControlHighlightLayer.add(to: layer, rect: rect.insetBy(dx: -4, dy: -1), shape: .cutCorner)
        }
        layoutRight(animated: false)
        reveal(selected, animated: false)
        update(dark: true, accent: NSColor(srgbRed: 0.98, green: 0.83, blue: 0.12, alpha: 1), contentsScale: 2)
        notifyLayoutChange()
    }

    func select(_ module: HUDModule, animated: Bool = true) {
        reveal(module, animated: animated)
        guard module != selectedModule || entries.contains(where: { $0.module == nil && $0.isSelected }) else { return }
        selectedModule = module
        let animate = animated && !HUDRuntimeAppearance.reduceMotion
        for entry in entries where entry.isSelected != (entry.module == module) {
            entry.setSelected(entry.module == module, animated: animate && entry.isVisible)
        }
    }

    /// An app click receives the existing selected-depth response while the
    /// controller closes. It never changes the active center module.
    func pressShortcut(id: UUID, animated: Bool = true) {
        guard let entry = entries.first(where: { $0.target == .appShortcut(id) }) else { return }
        entry.setSelected(true, animated: animated && entry.isVisible)
    }

    func update(dark: Bool, accent: NSColor, contentsScale: CGFloat) {
        self.dark = dark
        self.accent = accent; self.contentsScale = contentsScale
        for entry in entries { entry.update(dark: dark, accent: accent, contentsScale: contentsScale) }
        let scale = contentsScale.isFinite ? min(8, max(1, contentsScale)) : 2
        for arrow in [scrollUp, scrollDown] {
            arrow.contentsScale = scale
            for shape in arrow.sublayers ?? [] { shape.contentsScale = scale }
        }
        updateScrollIndicators()
    }

    /// Pointer feedback moves only the previous and current layered cards.
    func hover(_ module: HUDModule?) { hoverTarget(module.map(HUDNavigationTarget.module)) }

    func hoverTarget(_ target: HUDNavigationTarget?) {
        let target = target.flatMap { candidate in visibleEntries.contains { $0.target == candidate } ? candidate : nil }
        guard target != hoveredTarget else { return }
        if let previous = hoveredTarget { entries.first { $0.target == previous }?.setHovered(false) }
        hoveredTarget = target
        if let target { entries.first { $0.target == target }?.setHovered(true) }
    }

    /// Only app entries are inserted/removed. Existing module and app layers
    /// survive edits, reordering and reflow so pointer depth is uninterrupted.
    func updateAppShortcuts(_ shortcuts: [HUDAppShortcutPresentation], animated: Bool = true) {
        var seen = Set<UUID>()
        let shortcuts = shortcuts.filter { seen.insert($0.id).inserted }
        let previousTargets = rightEntries.map(\.target)
        let previousByTarget = Dictionary(uniqueKeysWithValues: entries.map { ($0.target, $0) })
        let appEntries = shortcuts.enumerated().map { index, shortcut -> HUDNavigationEntry in
            let target = HUDNavigationTarget.appShortcut(shortcut.id)
            let entry = previousByTarget[target] ?? HUDNavigationEntry(target: target,
                rect: Self.rightRect(index: Self.rightModules.count - 1 + index, offset: rightScrollOffset))
            entry.clipRect = rightViewport
            entry.updateShortcut(shortcut)
            entry.update(dark: dark, accent: accent, contentsScale: contentsScale)
            return entry
        }
        let modules = entries.filter { $0.module != nil }
        let right = Self.rightModules.filter { $0 != .addApp }.compactMap { module in
            modules.first { $0.module == module }
        } + appEntries + modules.filter { $0.module == .addApp }
        let wanted = Set(right.map(\.target))
        if let hoveredTarget, !wanted.contains(hoveredTarget), hoveredTarget.group == .right { hoverTarget(nil) }
        for entry in entries where entry.module == nil && !wanted.contains(entry.target) {
            entry.cancelAnimations(); entry.layer.removeFromSuperlayer()
        }
        entries = right + modules.filter { $0.group != .right }
        for entry in right where entry.layer.superlayer !== rightClip { rightClip.addSublayer(entry.layer) }
        if previousTargets != right.map(\.target) {
            scrollAnimationGeneration += 1
            isScrollGestureActive = false; ownsMomentum = false; suppressesMomentum = false
            rightScrollOffset = bounded(rightScrollOffset)
            if selectedModule == .addApp {
                // Saving takes place in Add App. Keep the edited/inserted tail
                // and its final configuration tile visible in one reflow.
                let finalRect = Self.rightRect(index: right.count - 1, offset: rightScrollOffset)
                let safeTop = rightViewport.minY + 12, safeBottom = rightViewport.maxY - 18
                if finalRect.minY < safeTop { rightScrollOffset = bounded(rightScrollOffset + finalRect.minY - safeTop) }
                else if finalRect.maxY > safeBottom { rightScrollOffset = bounded(rightScrollOffset + finalRect.maxY - safeBottom) }
            }
            rawScrollOffset = rightScrollOffset
            layoutRight(animated: animated)
        }
        notifyLayoutChange(force: true)
    }

    /// Closing commits a legal scroll position and cancels finite motion. A
    /// pending animation completion cannot revive the hidden navigation.
    func cancelAnimations() {
        scrollAnimationGeneration += 1
        isScrollGestureActive = false
        ownsMomentum = false
        suppressesMomentum = false
        rightScrollOffset = bounded(rightScrollOffset)
        rawScrollOffset = rightScrollOffset
        hoveredTarget = nil
        for entry in entries { entry.setHovered(false, animated: false) }
        layoutRight(animated: false)
        for entry in entries { entry.cancelAnimations() }
    }

    /// Native controls use only the visible part of a tile's finite movement
    /// envelope. The exact pointer target still uses its presentation outline.
    func clippedRect(for entry: HUDNavigationEntry) -> CGRect? {
        guard entry.isVisible else { return nil }
        let rect = entry.group == .right ? entry.interactionRect.intersection(rightViewport) : entry.interactionRect
        return !rect.isNull && !rect.isEmpty ? rect : nil
    }

    /// Reserve a stable native hit envelope for the full curved scroll path.
    /// Exact clicks and accessibility frames still use the displayed outline,
    /// so overlapping envelopes never activate a clipped or adjacent card.
    func nativeHitRect(for entry: HUDNavigationEntry) -> CGRect {
        guard let index = rightEntries.firstIndex(where: { $0.target == entry.target }) else {
            return entry.interactionRect.union(entry.selectionEnvelope)
        }
        let range = (-CGFloat(58))...(maxRightScrollOffset + 58)
        let widestOffset = min(range.upperBound, max(range.lowerBound, CGFloat(211 + index / 2 * 90) - 320))
        var envelope = CGRect.null
        for offset in [range.lowerBound, widestOffset, range.upperBound] {
            let rect = Self.rightRect(index: index, offset: offset)
            envelope = envelope.union(HUDNavigationEntry.poseEnvelope(for: rect, rotation: 0))
        }
        return envelope.intersection(rightViewport)
    }

    /// `point` is already inverse-projected through the host's common plane.
    func hitTarget(point: CGPoint, includingBottom: Bool = true) -> HUDNavigationTarget? {
        hitTarget(point: point, candidates: visibleEntries.filter { includingBottom || $0.group != .bottom })
    }

    func hitTest(point: CGPoint, includingBottom: Bool = true) -> HUDModule? {
        hitTarget(point: point, includingBottom: includingBottom)?.module
    }

    /// Retain hover ownership through the layered lift corridor, including app
    /// cards, without treating them as the Add App configuration module.
    func hoverHitTarget(point: CGPoint, includingBottom: Bool = true) -> HUDNavigationTarget? {
        guard point.x.isFinite, point.y.isFinite else { return nil }
        if let hoveredTarget, let entry = visibleEntries.first(where: { $0.target == hoveredTarget }),
           entry.group == .left || entry.group == .right,
           (entry.group != .right || rightViewport.contains(point)), entry.selectionEnvelope.contains(point) {
            return entry.target
        }
        return hitTarget(point: point, includingBottom: includingBottom)
    }

    func hoverHitTest(point: CGPoint, includingBottom: Bool = true) -> HUDModule? {
        hoverHitTarget(point: point, includingBottom: includingBottom)?.module
    }

    /// The host inverse-projects through corePlane for these two sector faces.
    func hitTestBottom(point: CGPoint) -> HUDModule? {
        hitTarget(point: point, candidates: visibleEntries.filter { $0.group == .bottom })?.module
    }

    private func hitTarget(point: CGPoint, candidates: [HUDNavigationEntry]) -> HUDNavigationTarget? {
        guard point.x.isFinite, point.y.isFinite else { return nil }
        if let selected = candidates.first(where: { $0.module == selectedModule }),
           (selected.group != .right || rightViewport.contains(point)), selected.contains(point) {
            return selected.target
        }
        return candidates.reversed().first(where: {
            $0.module != selectedModule && ($0.group != .right || rightViewport.contains(point)) && $0.contains(point)
        })?.target
    }

    func containsNavigationPoint(_ point: CGPoint, includingBottom: Bool = true) -> Bool {
        hitTarget(point: point, includingBottom: includingBottom) != nil
            || rightViewport.contains(point) || scrollUpRect.contains(point) || scrollDownRect.contains(point)
    }

    @discardableResult func scroll(at point: CGPoint, delta: CGFloat,
                                   phase: NSEvent.Phase = [], momentumPhase: NSEvent.Phase = []) -> Bool {
        guard point.x.isFinite, point.y.isFinite, delta.isFinite else { return false }
        let directBegan = phase.contains(.began) && momentumPhase.isEmpty
        if directBegan {
            ownsMomentum = false
            suppressesMomentum = false
            if !rightViewport.contains(point) {
                if isScrollGestureActive { settleScroll(animated: true) }
                return false
            }
        }
        guard isScrollGestureActive || rightViewport.contains(point)
            || (ownsMomentum && !momentumPhase.isEmpty) else { return false }
        let cancelled = phase.contains(.cancelled) || momentumPhase.contains(.cancelled)
        let isMomentum = !momentumPhase.isEmpty
        let ended = isMomentum ? momentumPhase.contains(.ended) : phase.contains(.ended)
        let hasPhase = !phase.isEmpty || isMomentum
        if cancelled { cancelAnimations(); notifyLayoutChange(); return true }
        if !hasPhase {
            guard abs(delta) > 0.001 else { return false }
            scrollPixels(delta, animated: false)
            return true
        }
        if isMomentum && suppressesMomentum {
            // Consume the entire inertial tail, even outside the viewport.
            // Neither momentum-began nor a late zero-delta end restarts the spring.
            if ended { ownsMomentum = false; suppressesMomentum = false }
            return true
        }
        if directBegan || (!isScrollGestureActive && (!ended || abs(delta) > 0.001)) {
            interruptScrollAnimation()
            rawScrollOffset = uncompressed(rightScrollOffset)
            isScrollGestureActive = true
        }
        ownsMomentum = true
        if abs(delta) > 0.001 {
            rawScrollOffset = min(maxRightScrollOffset + 10_000, max(-10_000, rawScrollOffset + delta))
            rightScrollOffset = rubberBanded(rawScrollOffset)
            hover(nil)
            layoutRight(animated: false)
            notifyLayoutChange()
        }
        let beyondBoundary = rightScrollOffset != bounded(rightScrollOffset)
        if isMomentum && beyondBoundary {
            // Momentum no longer has a finger holding the strip outside its
            // bounds. Rebound now, instead of waiting for a long inertial tail.
            suppressesMomentum = !ended
            settleScroll(animated: true)
        } else if ended {
            if beyondBoundary && !isMomentum { suppressesMomentum = true }
            settleScroll(animated: true)
        }
        if isMomentum && ended { ownsMomentum = false; suppressesMomentum = false }
        return true
    }

    /// Compatibility for native arrow buttons: a small pixel nudge, never a
    /// row/page snap. Repeated arrows eventually expose every partial tile.
    func scrollRows(_ delta: Int, animated: Bool = true) {
        scrollPixels(CGFloat(delta) * 32, animated: animated)
    }

    func scrollPixels(_ delta: CGFloat, animated: Bool = true) {
        guard delta.isFinite else { return }
        interruptScrollAnimation()
        isScrollGestureActive = false
        ownsMomentum = false
        suppressesMomentum = false
        let next = bounded(rightScrollOffset + delta)
        guard next != rightScrollOffset else { return }
        rightScrollOffset = next; rawScrollOffset = next
        hover(nil)
        layoutRight(animated: animated)
        notifyLayoutChange()
    }

    @discardableResult func handleScrollClick(at point: CGPoint) -> Bool {
        if scrollUpRect.contains(point) { scrollRows(-1); return true }
        if scrollDownRect.contains(point) { scrollRows(1); return true }
        return false
    }

    private func reveal(_ module: HUDModule, animated: Bool) {
        guard let index = rightEntries.firstIndex(where: { $0.module == module }) else { return }
        let rect = Self.rightRect(index: index, offset: bounded(rightScrollOffset))
        let safeTop = rightViewport.minY + 12
        let safeBottom = rightViewport.maxY - 18
        if rect.minY < safeTop { scrollPixels(rect.minY - safeTop, animated: animated) }
        else if rect.maxY > safeBottom { scrollPixels(rect.maxY - safeBottom, animated: animated) }
    }

    private static func rightRect(index: Int, offset: CGFloat) -> CGRect {
        let y = CGFloat(174 + index / 2 * 90) - offset
        let centerY = y + 37
        // Follow the circular shell with a clear radial gap. The second
        // column continues the same card row, as in the reference video.
        let radius: CGFloat = 306
        let dy = centerY - 320
        let x = 508 + sqrt(max(0, radius * radius - dy * dy))
        return CGRect(x: x + CGFloat(index % 2) * 84, y: y, width: 78, height: 74)
    }

    private func layoutRight(animated: Bool, spring: Bool = false) {
        let animate = animated && !HUDRuntimeAppearance.reduceMotion
        if animate {
            scrollAnimationGeneration += 1
            let token = scrollAnimationGeneration
            CATransaction.begin()
            CATransaction.setCompletionBlock { [weak self] in
                guard let self, self.scrollAnimationGeneration == token else { return }
                // Shrink native movement envelopes after the finite animation.
                for entry in self.entries where entry.group == .right { entry.finishMovement() }
                self.notifyLayoutChange(force: true)
            }
        }
        for (index, entry) in rightEntries.enumerated() {
            let rect = Self.rightRect(index: index, offset: rightScrollOffset)
            entry.setGeometry(rect: rect, rotation: 0, animated: animate, spring: spring)
        }
        if animate { CATransaction.commit() }
        updateScrollIndicators()
    }

    private func bounded(_ value: CGFloat) -> CGFloat { min(maxRightScrollOffset, max(0, value)) }
    private func rubberBanded(_ value: CGFloat) -> CGFloat {
        let limit: CGFloat = 58
        let bound = bounded(value), excess = value - bound
        return bound + excess / (1 + abs(excess) / limit)
    }
    private func uncompressed(_ value: CGFloat) -> CGFloat {
        let bound = bounded(value), excess = value - bound
        return bound + excess / max(0.001, 1 - abs(excess) / 58)
    }
    private func interruptScrollAnimation() {
        scrollAnimationGeneration += 1
        let previousOffset = rightScrollOffset
        if let first = entries.first(where: { $0.module == Self.rightModules[0] }),
           first.layer.animation(forKey: "navigation.position") != nil, let shown = first.layer.presentation() {
            rightScrollOffset = Self.rightRect(index: 0, offset: 0).midY - shown.position.y
        }
        for entry in entries where entry.group == .right { entry.cancelAnimations() }
        if previousOffset != rightScrollOffset { layoutRight(animated: false) }
    }
    private func settleScroll(animated: Bool) {
        isScrollGestureActive = false
        let target = bounded(rightScrollOffset)
        rawScrollOffset = target
        guard target != rightScrollOffset else { return }
        rightScrollOffset = target
        layoutRight(animated: animated, spring: true)
        notifyLayoutChange()
    }

    private func notifyLayoutChange(force: Bool = false) {
        let state = LayoutState(visible: Set(visibleEntries.map(\.target)), up: canScrollUp, down: canScrollDown)
        guard force || state != lastLayoutState else { return }
        lastLayoutState = state
        // The host reserves nativeHitRect once; per-pixel movement only needs
        // native updates when clipping visibility or arrow availability changes.
        onLayoutChange?()
    }

    private func updateScrollIndicators() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        let color = NSColor(white: dark ? 0.90 : 0.18, alpha: 1)
        for arrow in [scrollUp, scrollDown] {
            for case let shape as CAShapeLayer in arrow.sublayers ?? [] {
                let alpha: CGFloat = shape.name == "navigation.scrollBaseline" ? 0.42
                    : (shape.name == "navigation.scrollEcho" ? 0.19 : 1)
                shape.strokeColor = color.withAlphaComponent(alpha).cgColor
            }
        }
        scrollUp.opacity = canScrollUp ? 0.82 : 0.18
        scrollDown.opacity = canScrollDown ? 0.82 : 0.18
        scrollHighlights[true]?.setEnabled(canScrollUp)
        scrollHighlights[false]?.setEnabled(canScrollDown)
        CATransaction.commit()
    }
}

final class HUDNavigationEntry {
    // Selection keeps its bounded expansion; hover adds a small layered lift.
    private static let maximumSelectionScale: CGFloat = 1.06
    let target: HUDNavigationTarget
    var module: HUDModule? { target.module }
    var group: HUDModuleGroup { target.group }
    var navigationTitle: String { shortcut?.name ?? module?.navigationTitle ?? "" }
    private var shortcut: HUDAppShortcutPresentation?
    private(set) var rect: CGRect
    fileprivate var clipRect: CGRect?
    var isVisible: Bool {
        guard let clipRect else { return true }
        // Direct scroll samples commit model geometry immediately. A stale
        // presentation frame must not leave an offscreen native button enabled.
        return projectedRect.intersects(clipRect)
            || (layer.animation(forKey: "navigation.position") != nil && presentationRect.intersects(clipRect))
    }
    /// This base owns scrolling, connectors and the subdued underplate. Only
    /// faceLayer lifts above the backing; clipped glyphs lift a little farther.
    let layer = CALayer()
    let faceLayer = CALayer()
    private(set) var isSelected = false
    private var isHovered = false

    /// Bounding boxes are in navigation-local coordinates. The host can union
    /// these before projecting through its parent plane for a native hit view.
    var projectedRect: CGRect { projectedBounds(using: faceLayer, origin: layer.position) }
    var presentationRect: CGRect {
        projectedBounds(using: faceLayer.presentation() ?? faceLayer,
                        origin: (layer.presentation() ?? layer).position)
    }
    /// Includes both ends of a finite scroll, so native input remains reachable
    /// while the shared plane and tile positions interpolate independently.
    var interactionRect: CGRect { movementEnvelope.union(projectedRect).union(presentationRect) }

    fileprivate var selectionEnvelope: CGRect { Self.poseEnvelope(for: rect, rotation: rotation) }

    fileprivate static func poseEnvelope(for rect: CGRect, rotation: CGFloat) -> CGRect {
        let c = abs(cos(rotation)), s = abs(sin(rotation))
        let width = (rect.width * c + rect.height * s) * maximumSelectionScale
        let height = (rect.width * s + rect.height * c) * maximumSelectionScale
        return CGRect(x: rect.midX - width / 2 - 2, y: rect.midY - height / 2 - 4, width: width + 4, height: height + 4)
    }

    private let contentClip = CALayer()
    private let contentLayer = CALayer()
    private let hoverLight = CAShapeLayer()
    private let side = CAShapeLayer()
    private let connectors = CAShapeLayer()
    private let plate = CAShapeLayer()
    private let inset = CAShapeLayer()
    private let bevel = CAShapeLayer()
    private let marker = CAShapeLayer()
    private let bottomStripe = CAShapeLayer()
    private let bottomTechnical = CAShapeLayer()
    private let icon = CAShapeLayer()
    private let appIcon = CALayer()
    private let title = CATextLayer()
    private let subtitle = CATextLayer()
    private let outline: CGPath
    private let labelSize: CGFloat
    private var dark = true
    private var accent = NSColor(srgbRed: 0.98, green: 0.83, blue: 0.12, alpha: 1)
    private var rotation: CGFloat
    private var movementEnvelope: CGRect
    private var selectionGeneration: UInt64 = 0
    private var selectionCleanup: DispatchWorkItem?
    private var backingGeneration: UInt64 = 0
    private var backingCleanup: DispatchWorkItem?
    private var hasLayeredHover: Bool { group == .left || group == .right }

    deinit {
        selectionCleanup?.cancel()
        backingCleanup?.cancel()
    }

    fileprivate convenience init(module: HUDModule, rect: CGRect) {
        self.init(target: .module(module), rect: rect)
    }

    fileprivate init(target: HUDNavigationTarget, rect: CGRect) {
        self.target = target
        let module = target.module
        let group = target.group
        self.rect = rect
        movementEnvelope = rect
        switch module {
        case .system: rotation = -0.015
        case .display: rotation = -0.008
        case .hotkeys: rotation = 0.004
        case .about: rotation = 0.016
        default: rotation = 0
        }
        let slant = Self.slant(for: module ?? .addApp)
        outline = group == .bottom
            ? Self.bottomSectorPath(size: rect.size, mirrored: module == .activityMonitor)
            : Self.platePath(size: rect.size, slant: slant)
        labelSize = group == .left ? 13.5 : (group == .right ? 10 : 11.5)
        layer.name = "hud.navigation.\(target.identifier)"
        layer.bounds = CGRect(origin: .zero, size: rect.size)
        layer.position = CGPoint(x: rect.midX, y: rect.midY)
        layer.masksToBounds = false
        layer.allowsGroupOpacity = false
        layer.shouldRasterize = false

        faceLayer.name = "navigation.face"
        faceLayer.frame = layer.bounds
        faceLayer.masksToBounds = false
        faceLayer.allowsGroupOpacity = false
        faceLayer.shouldRasterize = false

        side.frame = layer.bounds.offsetBy(dx: group == .right ? -8 : 8, dy: 10)
        side.isHidden = group == .bottom
        side.name = "navigation.backingPlate"
        side.path = outline
        side.lineWidth = 0.6
        if group == .left {
            connectors.name = "navigation.connectors"
            connectors.frame = layer.bounds
            let strands = CGMutablePath()
            let endX = max(rect.width + 30, 300 - rect.minX)
            let bend = (320 - rect.midY) * 0.055
            for index in 0..<5 {
                let y = rect.height / 2 + CGFloat(index - 2) * 3
                strands.move(to: CGPoint(x: rect.width - 12, y: y))
                strands.addLine(to: CGPoint(x: rect.width + 9, y: y))
                strands.addLine(to: CGPoint(x: rect.width + 23, y: y + bend))
                strands.addLine(to: CGPoint(x: endX, y: y + bend))
            }
            connectors.path = strands
            connectors.fillColor = nil; connectors.lineWidth = 0.65
            connectors.lineJoin = .round; connectors.lineCap = .butt
            let fade = CAGradientLayer()
            fade.name = "navigation.connectorFade"
            fade.frame = CGRect(x: 0, y: 0, width: endX, height: rect.height)
            fade.startPoint = CGPoint(x: 0, y: 0.5); fade.endPoint = CGPoint(x: 1, y: 0.5)
            fade.colors = [NSColor.black.cgColor, NSColor.black.cgColor, NSColor.clear.cgColor]
            fade.locations = [0, NSNumber(value: Double((endX - 16) / endX)), 1]
            connectors.mask = fade
            layer.addSublayer(connectors)
        }
        plate.name = "navigation.plate"
        plate.frame = layer.bounds
        plate.path = outline
        plate.lineWidth = hasLayeredHover ? 0 : 0.65
        inset.frame = layer.bounds
        inset.name = hasLayeredHover ? "navigation.outerBorder" : "navigation.innerBorder"
        inset.path = group == .bottom
            ? Self.bottomSectorPath(size: rect.size, mirrored: module == .activityMonitor, inset: 2.5)
            : Self.platePath(size: rect.size, slant: slant, inset: hasLayeredHover ? -3 : 4)
        inset.fillColor = nil
        inset.lineWidth = 0.5
        bevel.frame = layer.bounds
        let edge = CGMutablePath()
        edge.move(to: CGPoint(x: max(0, slant) + 7, y: 2.5))
        edge.addLine(to: CGPoint(x: rect.width + min(0, slant) - 7, y: 2.5))
        bevel.path = hasLayeredHover ? nil : edge
        bevel.fillColor = nil
        bevel.lineWidth = 0.8
        let iconSize: CGFloat
        let iconCenter: CGPoint
        if module == .power {
            iconSize = 31
            iconCenter = CGPoint(x: 29, y: 29)
            title.frame = CGRect(x: 57, y: 10, width: rect.width - 71, height: 23)
            title.alignmentMode = .left
            subtitle.frame = CGRect(x: 57, y: 34, width: rect.width - 71, height: 17)
            subtitle.fontSize = 10
            subtitle.font = NSFont.systemFont(ofSize: 10, weight: .bold)
            subtitle.alignmentMode = .left
        } else if group == .bottom {
            iconSize = module == .storage ? 30 : 24
            iconCenter = CGPoint(x: module == .storage ? 60 : rect.width - 60, y: 23)
            title.frame = module == .storage ? CGRect(x: 73, y: 15, width: 65, height: 22)
                : CGRect(x: 5, y: 15, width: 64, height: 22)
            title.alignmentMode = .center
            bevel.path = nil
            bottomStripe.name = "navigation.bottomStripe"
            bottomStripe.frame = layer.bounds
            bottomStripe.path = Self.bottomSectorPath(size: rect.size, mirrored: module == .activityMonitor, stripe: true)
            bottomStripe.lineWidth = 0
            bottomTechnical.name = "navigation.bottomTechnical"
            bottomTechnical.frame = layer.bounds
            let technical = CGMutablePath()
            if module == .storage {
                technical.addRect(CGRect(x: rect.width - 12, y: 54, width: 2.6, height: 2.6))
                for index in 0..<3 {
                    technical.addRect(CGRect(x: rect.width - 27 + CGFloat(index) * 3.2, y: 56, width: 1, height: 1.2))
                }
            } else {
                // Sparse stencil microtype in the flat yellow inner band.
                let glyphs = [[7, 4, 6, 4, 7], [7, 4, 6, 4, 4], [2, 2, 2, 0, 2], [7, 1, 7, 4, 7], [7, 1, 3, 1, 7]]
                for (index, glyph) in glyphs.enumerated() {
                    for (row, bits) in glyph.enumerated() {
                        for column in 0..<3 where bits & (1 << (2 - column)) != 0 {
                            technical.addRect(CGRect(x: 10 + CGFloat(index) * 3.4 + CGFloat(column) * 0.6,
                                                     y: 53.5 + CGFloat(row) * 0.6, width: 0.66, height: 0.66))
                        }
                    }
                }
            }
            bottomTechnical.path = technical
            bottomTechnical.lineWidth = 0
        } else {
            iconSize = group == .left ? 34 : 23
            iconCenter = CGPoint(x: rect.width / 2, y: group == .left ? 29 : 22)
            title.frame = CGRect(x: 8, y: group == .left ? 52 : 42,
                                 width: rect.width - 16, height: 24)
            title.alignmentMode = .center
        }
        marker.name = "navigation.iconRing"
        marker.frame = layer.bounds
        let highlight = CGMutablePath()
        let haloRadius = iconSize * 0.65
        highlight.addArc(center: iconCenter, radius: haloRadius,
                         startAngle: -.pi * 0.90, endAngle: -.pi * 0.35, clockwise: false)
        highlight.addArc(center: iconCenter, radius: haloRadius,
                         startAngle: .pi * 0.08, endAngle: .pi * 0.60, clockwise: false)
        marker.path = highlight
        marker.fillColor = nil
        marker.lineWidth = group == .bottom ? 1.8 : 2.5
        icon.bounds = CGRect(x: 0, y: 0, width: 32, height: 32)
        icon.position = iconCenter
        icon.setAffineTransform(CGAffineTransform(scaleX: iconSize / 32, y: iconSize / 32))
        icon.name = "navigation.icon"
        icon.path = Self.iconPath(for: module ?? .addApp)
        appIcon.name = "navigation.appIcon"
        appIcon.bounds = icon.bounds; appIcon.position = icon.position
        appIcon.transform = icon.transform; appIcon.contentsGravity = .resizeAspect
        appIcon.isHidden = true
        if group == .bottom {
            icon.shadowPath = icon.path
            icon.shadowColor = NSColor.black.cgColor
            icon.shadowOpacity = 0.28
            icon.shadowRadius = 0.8
            icon.shadowOffset = CGSize(width: 0.8, height: 1.3)
            appIcon.shadowColor = NSColor.black.cgColor
            appIcon.shadowOpacity = icon.shadowOpacity
            appIcon.shadowRadius = icon.shadowRadius
            appIcon.shadowOffset = icon.shadowOffset
        }
        icon.fillRule = group == .bottom ? .nonZero : .evenOdd
        icon.lineWidth = 0
        title.fontSize = module == .power ? 14 : labelSize
        title.truncationMode = .none
        title.isWrapped = false
        layer.addSublayer(side)
        layer.addSublayer(faceLayer)
        for item in [plate, bottomStripe, inset, bevel, bottomTechnical] {
            item.allowsGroupOpacity = false
            item.shouldRasterize = false
            faceLayer.addSublayer(item)
        }
        if hasLayeredHover {
            hoverLight.name = "navigation.hoverLight"
            hoverLight.frame = layer.bounds; hoverLight.path = outline
            hoverLight.lineWidth = 0; hoverLight.opacity = 0
            faceLayer.addSublayer(hoverLight)
            contentClip.name = "navigation.contentClip"
            contentClip.frame = layer.bounds
            contentClip.allowsGroupOpacity = false
            let clip = CAShapeLayer()
            clip.frame = layer.bounds; clip.path = outline; clip.fillColor = NSColor.black.cgColor
            contentClip.mask = clip
            contentLayer.name = "navigation.contents"
            contentLayer.frame = layer.bounds
            contentLayer.allowsGroupOpacity = false
            contentClip.addSublayer(contentLayer)
            faceLayer.addSublayer(contentClip)
        }
        for item in [marker, icon, appIcon, title, subtitle] {
            item.allowsGroupOpacity = false
            item.shouldRasterize = false
            (hasLayeredHover ? contentLayer : faceLayer).addSublayer(item)
        }
        side.allowsGroupOpacity = false
        side.shouldRasterize = false
    }

    fileprivate func updateShortcut(_ presentation: HUDAppShortcutPresentation) {
        guard target == .appShortcut(presentation.id) else { return }
        let imageChanged = shortcut?.icon !== presentation.icon || shortcut?.iconPreset != presentation.iconPreset
        shortcut = presentation
        withoutActions {
            icon.path = AppShortcutArtwork.path(for: presentation.iconPreset, in: icon.bounds)
            icon.lineWidth = 32 / 17; icon.lineCap = .round; icon.lineJoin = .round
            if imageChanged || appIcon.contents == nil, let image = presentation.icon {
                var proposed = CGRect(x: 0, y: 0, width: 64, height: 64)
                appIcon.contents = image.cgImage(forProposedRect: &proposed, context: nil, hints: nil)
            }
            if presentation.icon == nil { appIcon.contents = nil }
            appIcon.isHidden = presentation.iconPreset != .original || appIcon.contents == nil
            icon.isHidden = !appIcon.isHidden
            updateTitle(); applyColors()
        }
    }

    fileprivate func setSelected(_ selected: Bool, animated: Bool) {
        isSelected = selected
        updatePose(animated: animated, rising: selected)
    }

    /// Retarget every layer from its rendered pose. Hover separates the planes
    /// without enlarging them, and selection never adds a spring or overshoot.
    private func updatePose(animated: Bool, rising: Bool) {
        let previousFace = faceLayer.presentation()?.transform ?? faceLayer.transform
        let previousContent = contentLayer.presentation()?.transform ?? contentLayer.transform
        let highlightLayers = [plate, bottomStripe]
        let previousColors = highlightLayers.map { $0.presentation()?.fillColor ?? $0.fillColor }
        cancelSelectionCleanup()
        let nextFace = faceTransform()
        withoutActions {
            self.faceLayer.transform = nextFace
            self.contentLayer.transform = self.contentTransform()
            self.layer.opacity = 1
            self.layer.zPosition = self.group == .bottom ? 0 : (self.isSelected ? 1 : (self.isHovered ? 0.5 : 0))
            self.applyColors()
        }
        faceLayer.removeAnimation(forKey: "navigation.transform")
        contentLayer.removeAnimation(forKey: "navigation.contentTransform")
        for shape in highlightLayers { shape.removeAnimation(forKey: "navigation.highlight") }
        let duration: TimeInterval = rising && isSelected ? HUDNavigation.selectionTransitionDuration : 0.18
        updateBackingPose(animated: animated, duration: duration)
        guard animated, !HUDRuntimeAppearance.reduceMotion else { return }
        if group == .bottom {
            // The inner-circle sectors stay fixed. Only their fill highlight
            // changes, preserving clearance around the full-size timer dial.
            for (index, shape) in highlightLayers.enumerated() {
                guard let from = previousColors[index], let to = shape.fillColor, !CFEqual(from, to) else { continue }
                let highlight = CABasicAnimation(keyPath: "fillColor")
                highlight.fromValue = from; highlight.toValue = to
                highlight.duration = duration; highlight.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                shape.add(highlight, forKey: "navigation.highlight")
            }
        } else if !CATransform3DEqualToTransform(previousFace, nextFace) {
            let expansion = CABasicAnimation(keyPath: "transform")
            expansion.fromValue = NSValue(caTransform3D: previousFace)
            expansion.toValue = NSValue(caTransform3D: nextFace)
            expansion.duration = duration
            expansion.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            expansion.isRemovedOnCompletion = true; expansion.fillMode = .removed
            expansion.beginTime = faceLayer.convertTime(CACurrentMediaTime(), from: nil)
            faceLayer.add(expansion, forKey: "navigation.transform")
        }
        if hasLayeredHover, !CATransform3DEqualToTransform(previousContent, contentLayer.transform) {
            let lift = CABasicAnimation(keyPath: "transform")
            lift.fromValue = NSValue(caTransform3D: previousContent)
            lift.toValue = NSValue(caTransform3D: contentLayer.transform)
            lift.duration = duration; lift.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            lift.isRemovedOnCompletion = true
            contentLayer.add(lift, forKey: "navigation.contentTransform")
        }
        let token = selectionGeneration
        let cleanup = DispatchWorkItem { [weak self] in
            guard let self, self.selectionGeneration == token else { return }
            self.selectionCleanup = nil
            self.faceLayer.removeAnimation(forKey: "navigation.transform")
            self.contentLayer.removeAnimation(forKey: "navigation.contentTransform")
            self.plate.removeAnimation(forKey: "navigation.highlight")
            self.bottomStripe.removeAnimation(forKey: "navigation.highlight")
        }
        selectionCleanup = cleanup
        DispatchQueue.main.asyncAfter(deadline: .now() + duration + 0.02, execute: cleanup)
    }

    /// The underplate follows by less than one point while the face and glyphs
    /// lift farther, preserving the quiet leftward shadow on right-side cards.
    private func updateBackingPose(animated: Bool, duration: TimeInterval) {
        let previous = side.presentation()?.transform ?? side.transform
        cancelBackingCleanup()
        let next = backingTransform()
        withoutActions { self.side.transform = next }
        side.removeAnimation(forKey: "navigation.backingTransform")
        guard group != .bottom, animated, !HUDRuntimeAppearance.reduceMotion,
              !CATransform3DEqualToTransform(previous, next) else { return }
        let expansion = CABasicAnimation(keyPath: "transform")
        expansion.fromValue = NSValue(caTransform3D: previous)
        expansion.toValue = NSValue(caTransform3D: next)
        expansion.duration = duration; expansion.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        expansion.isRemovedOnCompletion = true; expansion.fillMode = .removed
        expansion.beginTime = side.convertTime(CACurrentMediaTime(), from: nil)
        side.add(expansion, forKey: "navigation.backingTransform")
        let token = backingGeneration
        let cleanup = DispatchWorkItem { [weak self] in
            guard let self, self.backingGeneration == token else { return }
            self.backingCleanup = nil
            self.side.removeAnimation(forKey: "navigation.backingTransform")
        }
        backingCleanup = cleanup
        DispatchQueue.main.asyncAfter(deadline: .now() + duration + 0.02, execute: cleanup)
    }

    private func cancelSelectionCleanup() {
        selectionGeneration &+= 1
        selectionCleanup?.cancel()
        selectionCleanup = nil
    }

    private func cancelBackingCleanup() {
        backingGeneration &+= 1
        backingCleanup?.cancel()
        backingCleanup = nil
    }

    fileprivate func setGeometry(rect: CGRect, rotation: CGFloat, animated: Bool, spring: Bool) {
        let shown = layer.presentation() ?? layer
        let previousPosition = shown.position
        let previousRect = presentationRect
        cancelAnimations()
        self.rect = rect
        self.rotation = rotation
        withoutActions {
            layer.position = CGPoint(x: rect.midX, y: rect.midY)
            faceLayer.transform = faceTransform()
            contentLayer.transform = contentTransform()
            side.transform = backingTransform()
            // The common clip retains partially visible cards and their
            // backing plates throughout a continuous gesture and rebound.
            layer.isHidden = false
        }
        movementEnvelope = animated ? previousRect.union(projectedRect) : projectedRect
        guard animated else { return }
        if previousPosition != layer.position {
            let move: CABasicAnimation
            if spring {
                let bounce = CASpringAnimation(keyPath: "position")
                bounce.mass = 1; bounce.stiffness = 210; bounce.damping = 21; bounce.initialVelocity = 0
                bounce.duration = 0.48
                move = bounce
            } else {
                move = CABasicAnimation(keyPath: "position")
                move.duration = 0.18
                move.timingFunction = CAMediaTimingFunction(controlPoints: 0.20, 0.75, 0.30, 1)
            }
            move.fromValue = NSValue(point: previousPosition); move.toValue = NSValue(point: layer.position)
            layer.add(move, forKey: "navigation.position")
        }
    }

    fileprivate func update(dark: Bool, accent: NSColor, contentsScale: CGFloat) {
        self.dark = dark
        self.accent = accent
        let scale = contentsScale.isFinite && contentsScale > 0 ? max(1, min(8, contentsScale)) : 2
        withoutActions {
            self.updateTitle()
            self.subtitle.string = self.module == .power ? L10n.text("Device Battery", "设备电量") : ""
            self.applyColors()
            self.layer.contentsScale = HUDRenderScale.contentScale(for: self.layer, baseScale: scale)
            for item in [self.faceLayer, self.contentClip, self.contentLayer, self.hoverLight, self.connectors, self.side, self.plate, self.inset, self.bevel, self.marker, self.icon, self.appIcon, self.title, self.subtitle, self.bottomStripe, self.bottomTechnical] {
                item.contentsScale = HUDRenderScale.contentScale(for: item, baseScale: scale)
            }
        }
    }

    fileprivate func cancelAnimations() {
        cancelSelectionCleanup()
        cancelBackingCleanup()
        for item in [layer, faceLayer, contentLayer, side, plate, bottomStripe, hoverLight] {
            for key in item.animationKeys() ?? [] where key.hasPrefix("navigation.") {
                item.removeAnimation(forKey: key)
            }
        }
        finishMovement()
    }

    fileprivate func finishMovement() { movementEnvelope = projectedRect }

    fileprivate func setHovered(_ hovered: Bool, animated: Bool = true) {
        guard hovered != isHovered else { return }
        isHovered = hovered
        updatePose(animated: animated, rising: hovered)
    }

    private func updateTitle() {
        var text = shortcut?.name ?? module?.title ?? ""
        var size: CGFloat = module == .power ? 14 : labelSize
        let hasTwoLines = !L10n.isChinese && (module == .fileShelf || module == .clipboard)
        if hasTwoLines {
            text = module == .fileShelf ? "Temporary\nFile Shelf" : "Clipboard\nCache"
            size = 9.5
            title.frame = CGRect(x: 7, y: 39, width: rect.width - 14, height: 29)
        } else if group == .right {
            title.frame = CGRect(x: 7, y: 43, width: rect.width - 14, height: 22)
        }
        let bottomWrapped = module == .activityMonitor && !L10n.isChinese
        if bottomWrapped { text = "Activity\nMonitor" }
        if group == .bottom {
            title.frame.origin.y = bottomWrapped ? 8 : 15
            title.frame.size.height = bottomWrapped ? 29 : 22
        }
        if shortcut != nil { title.frame = CGRect(x: 7, y: 39, width: rect.width - 14, height: 30) }
        title.truncationMode = shortcut == nil ? .none : .end
        title.isWrapped = hasTwoLines || bottomWrapped || shortcut != nil
        title.string = text
        title.fontSize = size
        title.font = NSFont.systemFont(ofSize: size, weight: .bold)
    }

    private func applyColors() {
        let isPower = module == .power
        let isBottom = group == .bottom
        let foreground = (isPower && dark) || isBottom ? NSColor(white: 0.96, alpha: 1) : NSColor(white: 0.12, alpha: 1)
        let silver = NSColor(white: isSelected ? 0.93 : (isHovered ? 0.89 : (dark ? 0.79 : 0.88)), alpha: 0.95)
        // Keep the translucent sector face, bright lower band and hover depth
        // while deriving their hue from the live theme rather than fixed gold.
        let sectorBase = accent.blended(withFraction: isHovered ? 0.02 : (isSelected ? 0.055 : 0.19),
                                        of: isHovered ? .white : .black) ?? accent
        let sectorFill = sectorBase.withAlphaComponent(isHovered ? 0.90 : (isSelected ? 0.78 : 0.48))
        func accentTint(_ fraction: CGFloat, alpha: CGFloat) -> NSColor {
            (accent.blended(withFraction: fraction, of: .white) ?? accent).withAlphaComponent(alpha)
        }
        let powerFill = NSColor(white: dark ? 0.16 : 0.85, alpha: 0.96)
        hoverLight.fillColor = NSColor(white: 1, alpha: 0.22).cgColor
        hoverLight.strokeColor = nil
        hoverLight.opacity = isHovered ? 0.72 : 0
        side.fillColor = NSColor(white: dark ? 0.82 : 0.55, alpha: isSelected ? 0.16 : (isHovered ? 0.145 : 0.13)).cgColor
        side.strokeColor = NSColor(white: dark ? 0.94 : 0.27, alpha: isSelected ? 0.28 : (isHovered ? 0.265 : 0.25)).cgColor
        connectors.strokeColor = NSColor(white: dark ? 0.76 : 0.28, alpha: 0.42).cgColor
        plate.fillColor = (isPower ? powerFill : (isBottom ? sectorFill : silver)).cgColor
        plate.strokeColor = (isBottom ? accentTint(0.03, alpha: 0.90)
            : NSColor(white: isPower && dark ? 0.72 : 0.25, alpha: 0.65)).cgColor
        inset.strokeColor = (isBottom ? accentTint(0.12, alpha: 0.80)
            : isSelected || isHovered ? (hasLayeredHover ? NSColor(white: 1, alpha: 0.78)
                : accent.withAlphaComponent(0.68))
            : NSColor(white: isPower && dark ? 0.83 : 1, alpha: 0.38)).cgColor
        bevel.strokeColor = NSColor(white: 1, alpha: isPower ? 0.22 : 0.67).cgColor
        marker.strokeColor = (isBottom ? NSColor(white: 0.98, alpha: 0.85)
            : accent.withAlphaComponent(1)).cgColor
        marker.opacity = isBottom ? 0 : (isSelected ? 1 : (isHovered ? 0.55 : 0))
        bottomStripe.fillColor = accentTint(isHovered ? 0.24 : (isSelected ? 0.12 : 0.04),
                                           alpha: isSelected || isHovered ? 1 : 0.88).cgColor
        bottomTechnical.fillColor = NSColor(white: 0.09, alpha: 0.72).cgColor
        icon.fillColor = shortcut == nil ? foreground.cgColor : nil
        icon.strokeColor = shortcut == nil ? nil : foreground.cgColor
        let gameIcon = shortcut.flatMap { AppShortcutArtwork.gameIcon(for: $0.iconPreset) }
            ?? (shortcut == nil ? Self.gameIcon(for: module) : nil)
        if let image = gameIcon?.cgImage(size: 96, tint: foreground) {
            appIcon.contents = image
            appIcon.isHidden = false
            icon.isHidden = true
        } else if shortcut?.iconPreset != .original {
            appIcon.isHidden = true
            icon.isHidden = false
        }
        title.foregroundColor = foreground.cgColor
        subtitle.foregroundColor = foreground.withAlphaComponent(0.62).cgColor
    }

    private static func gameIcon(for module: HUDModule?) -> EndfieldGameIcon? {
        switch module {
        case .notes: return .mission
        case .fileShelf: return .depot
        case .clipboard: return .archive
        case .eventLog: return .story
        case .storage: return .factory
        case .workMode: return .strength
        case .map: return .region
        // Keyboard, volume, display, plus and settings retain their
        // precise action glyphs where the source category has no equivalent.
        default: return nil
        }
    }

    fileprivate func contains(_ point: CGPoint) -> Bool {
        guard isVisible else { return false }
        let shown = faceLayer.presentation() ?? faceLayer
        guard let local = inverseProject(point, through: shown, origin: (layer.presentation() ?? layer).position) else { return false }
        return outline.contains(local, using: .winding, transform: .identity)
    }

    private func inverseProject(_ point: CGPoint, through shown: CALayer, origin: CGPoint) -> CGPoint? {
        let x = point.x - origin.x, y = point.y - origin.y
        let t = shown.transform
        // Invert the face in its base plane; the host already removes the
        // common HUD perspective. The same map supports affine hinge poses.
        let a = t.m11 - x * t.m14, b = t.m21 - x * t.m24
        let c = t.m12 - y * t.m14, d = t.m22 - y * t.m24
        let u = x * t.m44 - t.m41, v = y * t.m44 - t.m42
        let determinant = a * d - b * c
        guard determinant.isFinite, abs(determinant) > 0.000001 else { return nil }
        let local = CGPoint(x: (u * d - b * v) / determinant + shown.bounds.width * shown.anchorPoint.x + shown.bounds.minX,
                            y: (a * v - u * c) / determinant + shown.bounds.height * shown.anchorPoint.y + shown.bounds.minY)
        return local.x.isFinite && local.y.isFinite ? local : nil
    }

    private func projectedBounds(using shown: CALayer, origin: CGPoint) -> CGRect {
        let bounds = shown.bounds
        let anchor = CGPoint(x: bounds.minX + bounds.width * shown.anchorPoint.x,
                             y: bounds.minY + bounds.height * shown.anchorPoint.y)
        let t = shown.transform
        let corners = [CGPoint(x: bounds.minX, y: bounds.minY), CGPoint(x: bounds.maxX, y: bounds.minY),
                       CGPoint(x: bounds.maxX, y: bounds.maxY), CGPoint(x: bounds.minX, y: bounds.maxY)]
        let points = corners.compactMap { point -> CGPoint? in
            let x = point.x - anchor.x, y = point.y - anchor.y
            let denominator = t.m14 * x + t.m24 * y + t.m44
            guard denominator.isFinite, abs(denominator) > 0.000001 else { return nil }
            let result = CGPoint(x: (t.m11 * x + t.m21 * y + t.m41) / denominator + origin.x,
                                 y: (t.m12 * x + t.m22 * y + t.m42) / denominator + origin.y)
            return result.x.isFinite && result.y.isFinite ? result : nil
        }
        guard points.count == 4, let first = points.first else { return rect }
        let minX = points.reduce(first.x) { min($0, $1.x) }, maxX = points.reduce(first.x) { max($0, $1.x) }
        let minY = points.reduce(first.y) { min($0, $1.y) }, maxY = points.reduce(first.y) { max($0, $1.y) }
        return CGRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
    }

    private func faceTransform() -> CATransform3D {
        let scale: CGFloat = group == .bottom ? 1 : (isSelected ? 1.045 : 1)
        var pose = planarScale(scale)
        if hasLayeredHover && isHovered {
            pose.m41 = group == .right ? 1.25 : -1.25
            pose.m42 = -3
        }
        return pose
    }

    private func backingTransform() -> CATransform3D {
        let scale: CGFloat = group == .bottom ? 1 : (isSelected ? 1.012 : 1)
        var pose = planarScale(scale)
        if hasLayeredHover && isHovered {
            pose.m41 = group == .right ? 0.35 : -0.35
            pose.m42 = -0.8
        }
        return pose
    }

    private func contentTransform() -> CATransform3D {
        guard hasLayeredHover && isHovered else { return CATransform3DIdentity }
        // A pronounced additional lift separates glyphs from the face without
        // scaling. The stationary outline mask keeps them inside the card.
        return CATransform3DMakeTranslation(group == .right ? 1.1 : -1.1, -3.8, 0)
    }

    private func planarScale(_ scale: CGFloat) -> CATransform3D {
        // Preserve the tile's authored resting angle and selected size.
        CATransform3DMakeAffineTransform(CGAffineTransform(rotationAngle: rotation).scaledBy(x: scale, y: scale))
    }

    private static func slant(for module: HUDModule) -> CGFloat {
        switch module {
        case .system: return 9
        case .display: return 5
        case .hotkeys: return 1
        case .about: return -8
        case .storage: return 2
        case .activityMonitor: return -2
        case .power: return 2
        default: return -2
        }
    }

    /// Mirrored sectors follow the bottom of the actual central 208-point
    /// circle, rather than a rectangular tile with a cosmetic rounded corner.
    /// Kept in local coordinates so face/backing motion and exact hit inversion
    /// use the same curved outline.
    private static func bottomSectorPath(size: CGSize, mirrored: Bool, inset: CGFloat = 0, stripe: Bool = false) -> CGPath {
        let radius: CGFloat = 208
        let center = CGPoint(x: size.width + 6, y: -143)
        let outerAngle = acos(-center.x / radius)
        let innerAngle = acos((size.width - center.x) / radius)
        let outer = CGPoint(x: 0, y: center.y + radius * sin(outerAngle))
        let inner = CGPoint(x: size.width, y: center.y + radius * sin(innerAngle))
        let path = CGMutablePath()
        if stripe {
            // A flat inner band tapers to the circular lower edge, rather than
            // wrapping a second curved ribbon around the entire wedge.
            let top: CGFloat = 44
            let angle = CGFloat.pi - asin((top - center.y) / radius)
            let endpoint = CGPoint(x: center.x + radius * cos(angle), y: top)
            path.move(to: endpoint); path.addLine(to: CGPoint(x: size.width, y: top))
            path.addLine(to: inner)
            path.addArc(center: center, radius: radius, startAngle: innerAngle, endAngle: angle, clockwise: false)
        } else {
            path.move(to: outer)
            path.addLine(to: CGPoint(x: size.width * 0.62, y: 3))
            path.addLine(to: CGPoint(x: size.width - 2, y: 8))
            path.addQuadCurve(to: CGPoint(x: size.width, y: 10), control: CGPoint(x: size.width, y: 8))
            path.addLine(to: inner)
            path.addArc(center: center, radius: radius, startAngle: innerAngle, endAngle: outerAngle, clockwise: false)
        }
        path.closeSubpath()
        var transform = CGAffineTransform(translationX: inset, y: inset)
            .scaledBy(x: (size.width - 2 * inset) / size.width, y: (size.height - 2 * inset) / size.height)
        var result = path.copy(using: &transform)!
        if mirrored {
            var reflection = CGAffineTransform(a: -1, b: 0, c: 0, d: 1, tx: size.width, ty: 0)
            result = result.copy(using: &reflection)!
        }
        return result
    }

    private static func platePath(size: CGSize, slant: CGFloat, inset: CGFloat = 0) -> CGPath {
        let margin = inset + 1.5
        let points = [CGPoint(x: margin + max(slant, 0), y: margin),
                      CGPoint(x: size.width - margin + min(slant, 0), y: margin),
                      CGPoint(x: size.width - margin - max(slant, 0), y: size.height - margin),
                      CGPoint(x: margin - min(slant, 0), y: size.height - margin)]
        let path = CGMutablePath()
        let radius: CGFloat = size.height > 65 ? 4 : 3
        for index in points.indices {
            let vertex = points[index]
            let previous = points[(index + points.count - 1) % points.count]
            let next = points[(index + 1) % points.count]
            func toward(_ other: CGPoint) -> CGPoint {
                let dx = other.x - vertex.x, dy = other.y - vertex.y
                let factor = radius / max(1, hypot(dx, dy))
                return CGPoint(x: vertex.x + dx * factor, y: vertex.y + dy * factor)
            }
            let entry = toward(previous), exit = toward(next)
            if index == 0 { path.move(to: entry) } else { path.addLine(to: entry) }
            path.addQuadCurve(to: exit, control: vertex)
        }
        path.closeSubpath()
        return path
    }

    private static func iconPath(for module: HUDModule) -> CGPath {
        let path = CGMutablePath()
        func polygon(_ points: CGPoint...) {
            guard let first = points.first else { return }
            path.move(to: first)
            for point in points.dropFirst() { path.addLine(to: point) }
            path.closeSubpath()
        }
        func p(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: x, y: y) }
        func box(_ x: CGFloat, _ y: CGFloat, _ width: CGFloat, _ height: CGFloat) {
            path.addRect(CGRect(x: x, y: y, width: width, height: height))
        }
        func stroke(_ source: CGPath, width: CGFloat) {
            path.addPath(source.copy(strokingWithWidth: width, lineCap: .butt, lineJoin: .miter, miterLimit: 4))
        }
        // These solid geometric silhouettes are authored here. Interior
        // subpaths cut clear holes using even-odd fill, rather than thin glyphs.
        switch module {
        case .notes:
            polygon(p(5, 2), p(22, 2), p(29, 9), p(29, 30), p(5, 30))
            polygon(p(22, 4), p(22, 10), p(27, 10))
            box(9, 14, 15, 3); box(9, 20, 15, 3); box(9, 26, 10, 2)
        case .fileShelf:
            polygon(p(3, 5), p(13, 5), p(17, 9), p(28, 9), p(28, 12), p(3, 12))
            polygon(p(2, 15), p(30, 15), p(26, 28), p(5, 28))
            box(12, 18, 10, 3)
        case .clipboard:
            polygon(p(5, 7), p(10, 7), p(10, 11), p(23, 11), p(23, 7), p(28, 7), p(28, 30), p(5, 30))
            box(11, 3, 11, 6); box(9, 15, 15, 3); box(9, 22, 12, 3)
        case .volume:
            polygon(p(2, 12), p(8, 12), p(17, 5), p(17, 27), p(8, 20), p(2, 20))
            for radius in [CGFloat(7), 12] {
                let wave = CGMutablePath()
                wave.addArc(center: p(17, 16), radius: radius, startAngle: -.pi / 4, endAngle: .pi / 4, clockwise: false)
                stroke(wave, width: 3)
            }
        case .workMode:
            polygon(p(2, 3), p(12, 3), p(12, 7), p(6, 7), p(6, 13), p(2, 13))
            polygon(p(20, 3), p(30, 3), p(30, 13), p(26, 13), p(26, 7), p(20, 7))
            polygon(p(2, 20), p(6, 20), p(6, 26), p(12, 26), p(12, 30), p(2, 30))
            polygon(p(26, 20), p(30, 20), p(30, 30), p(20, 30), p(20, 26), p(26, 26))
            polygon(p(16, 10), p(22, 16), p(16, 22), p(10, 16))
        case .eventLog:
            for y in [CGFloat(4), 14, 24] {
                box(3, y, 5, 5); box(12, y + 1, 17, 3)
            }
        case .map:
            polygon(p(2, 7), p(11, 3), p(21, 7), p(30, 3), p(30, 25), p(21, 29), p(11, 25), p(2, 29))
            box(10, 7, 2, 14); box(20, 11, 2, 14)
        case .addApp:
            path.addRoundedRect(in: CGRect(x: 3, y: 3, width: 26, height: 26), cornerWidth: 3, cornerHeight: 3)
            box(7, 7, 18, 18)
            polygon(p(14, 10), p(18, 10), p(18, 14), p(22, 14), p(22, 18), p(18, 18),
                    p(18, 22), p(14, 22), p(14, 18), p(10, 18), p(10, 14), p(14, 14))
        case .system:
            box(7, 7, 18, 18); box(12, 12, 8, 8)
            for v in [CGFloat(9), 19] {
                box(v, 1, 4, 5); box(v, 26, 4, 5)
                box(1, v, 5, 4); box(26, v, 5, 4)
            }
        case .display:
            path.addRoundedRect(in: CGRect(x: 2, y: 4, width: 28, height: 20), cornerWidth: 2, cornerHeight: 2)
            box(6, 8, 20, 12); box(13, 25, 6, 2); box(8, 28, 16, 3)
        case .hotkeys:
            path.addRoundedRect(in: CGRect(x: 1, y: 6, width: 30, height: 21), cornerWidth: 2, cornerHeight: 2)
            for x in [CGFloat(5), 11, 17, 23] { box(x, 10, 4, 4) }
            box(6, 19, 20, 3)
        case .about:
            path.addEllipse(in: CGRect(x: 2, y: 2, width: 28, height: 28))
            box(14, 7, 4, 4); box(14, 14, 4, 11)
        case .storage:
            box(12, 2, 8, 8); box(14.5, 11, 3, 4)
            box(5, 15, 22, 3)
            for x in [CGFloat(5), 15, 25] { box(x, 19, 2, 3) }
            for x in [CGFloat(2), 12, 22] { box(x, 23, 8, 7) }
        case .activityMonitor:
            box(3, 23, 5, 7); box(13, 18, 5, 12); box(23, 12, 5, 18)
            let growth = CGMutablePath()
            growth.move(to: p(3, 17)); growth.addLine(to: p(12, 8))
            growth.addLine(to: p(19, 13)); growth.addLine(to: p(28, 2))
            growth.move(to: p(20, 2)); growth.addLine(to: p(28, 2)); growth.addLine(to: p(28, 10))
            stroke(growth, width: 2.8)
        case .profile:
            path.addEllipse(in: CGRect(x: 11, y: 2, width: 10, height: 10))
            path.addRoundedRect(in: CGRect(x: 5, y: 16, width: 22, height: 14), cornerWidth: 5, cornerHeight: 5)
        case .power:
            let ring = CGMutablePath()
            ring.addArc(center: p(16, 17), radius: 11, startAngle: -.pi * 0.28, endAngle: .pi * 1.28, clockwise: false)
            stroke(ring, width: 4.2)
            box(14, 1, 4, 15)
        }
        return path
    }

    private func withoutActions(_ changes: () -> Void) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        changes()
        CATransaction.commit()
    }
}
