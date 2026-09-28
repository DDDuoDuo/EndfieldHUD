import AppKit
import QuartzCore

struct AppShortcutCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
}

/// Saved launchers and their editor share one retained surface in the HUD.
/// Choosing or dropping a bundle only opens a draft; activation is delegated
/// to the overlay owner so it can finish its closing animation first.
final class AppShortcutCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    var onChange: (() -> Void)?
    var onChooseApplication: (() -> Void)?
    var onEditName: ((CGRect, String) -> Void)?
    var onWillTransition: (() -> Void)?
    var onLaunch: ((UUID) -> Void)?
    var onSaved: ((AppShortcut) -> Void)?
    var onRemoved: ((String) -> Void)?
    private let store: AppShortcutStore?
    private(set) var items: [AppShortcut] = []
    private(set) var scrollOffset: CGFloat = 0
    private(set) var draftName = ""
    private(set) var draftIcon: AppShortcutIcon = .original
    private(set) var editingID: UUID?
    private var candidate: AppShortcutCandidate?
    private var errorMessage: String?
    private var dropTarget = false
    private var dark = true
    private var scale: CGFloat = 2
    private var icons: [UUID: CGImage] = [:]
    private var attemptedIcons = Set<UUID>()
    private var draftImage: CGImage?
    private var artwork = CALayer()
    private var rows = CALayer()
    private var outgoing: CALayer?
    private var active = false
    private let shouldReduceMotion: () -> Bool
    private var transitionGeneration = 0
    private var transitionCompletion: (() -> Void)?
    private(set) var isTransitioning = false
    static let transitionDuration: TimeInterval = 0.26
    private var yellow: NSColor { HUDRuntimeAppearance.accent }
    private static let listRect = CGRect(x: 12, y: 44, width: 376, height: 252)
    static let nameRect = CGRect(x: 78, y: 77, width: 304, height: 27)
    var isEditing: Bool { candidate != nil }
    var itemCount: Int { items.count }
    var accessibilityStatus: String { errorMessage ?? (isEditing ? L10n.text("Edit app shortcut", "编辑应用快捷方式") : L10n.text("Application shortcuts", "应用快捷方式")) }
    private var ink: NSColor { NSColor(white: dark ? 0.94 : 0.12, alpha: 1) }
    private var muted: NSColor { NSColor(white: dark ? 0.65 : 0.35, alpha: 1) }

    init(store: AppShortcutStore?, error: String? = nil,
         reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.store = store; self.errorMessage = error; items = store?.items ?? []
        shouldReduceMotion = reduceMotion
        super.init()
        layer.name = "module.addApp.canvas"
        layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
        layer.allowsGroupOpacity = false
        artwork.frame = layer.bounds; layer.addSublayer(artwork)
        rows.frame = Self.listRect; rows.masksToBounds = true; artwork.addSublayer(rows)
        repaint()
    }

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; scale = style.contentsScale
        if isTransitioning && shouldReduceMotion() { finishTransition(generation: transitionGeneration) }
        repaint(); return layer
    }
    func updateRenderScale(_ value: CGFloat) {
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard scale != next else { return }; scale = next; repaint()
    }
    func activate() { active = true; refreshFromStore() }
    func deactivate() { active = false; cancelTransitions(); dropTarget = false; repaint() }
    func refreshFromStore() {
        items = store?.items ?? []
        let retained = Set(items.map(\.id)); icons = icons.filter { retained.contains($0.key) }
        attemptedIcons.formIntersection(retained)
        scrollOffset = min(scrollOffset, maximumScroll)
        repaint(); onChange?()
    }
    func showError(_ message: String) { errorMessage = message; repaint(); onChange?() }
    func setDropTarget(_ value: Bool) { guard value != dropTarget else { return }; dropTarget = value; repaint() }

    @discardableResult func importURLs(_ urls: [URL]) -> Bool {
        guard let store else { showError(L10n.text("Shortcut storage is unavailable.", "快捷方式存储不可用。")); return false }
        guard urls.count == 1, let url = urls.first else {
            showError(L10n.text("Choose one application to customize.", "请选择一个应用进行设置。")); return false
        }
        do {
            let next = try store.inspect(url: url)
            beginDraft(next, item: nil)
            return true
        } catch { showError(error.localizedDescription); return false }
    }

    func setDraftName(_ value: String) { guard isEditing else { return }; draftName = value; repaint(); onChange?() }
    func cancelDraft() {
        guard isEditing else { return }
        changeScreen(direction: -1) {
            candidate = nil; editingID = nil; draftName = ""; draftIcon = .original; draftImage = nil; errorMessage = nil
        }
    }
    private func beginDraft(_ next: AppShortcutCandidate, item: AppShortcut?) {
        changeScreen(direction: 1, editNameAfter: true) {
            candidate = next; editingID = item?.id; draftName = item?.name ?? next.name
            draftIcon = item?.iconPreset ?? .original; draftImage = image(next.icon); errorMessage = nil
        }
    }
    private func edit(_ id: UUID) {
        guard let store, let item = items.first(where: { $0.id == id }) else { return }
        do { beginDraft(try store.inspect(url: store.resolvedURL(for: id)), item: item) }
        catch { showError(error.localizedDescription) }
    }
    private func save() {
        guard let store, let candidate else { return }
        do {
            let saved = try store.save(candidate: candidate, name: draftName, iconPreset: draftIcon, editingID: editingID)
            icons.removeValue(forKey: saved.id)
            attemptedIcons.remove(saved.id)
            cancelDraft(); refreshFromStore(); onSaved?(saved)
        } catch { showError(error.localizedDescription) }
    }

    var accessibleActions: [AppShortcutCanvasAction] {
        guard !isTransitioning else { return [] }
        var actions = [AppShortcutCanvasAction(id: "apps:choose", label: L10n.text("Choose application", "选择应用"), rect: CGRect(x: 270, y: 2, width: 118, height: 28))]
        if isEditing {
            actions.append(AppShortcutCanvasAction(id: "apps:name", label: L10n.text("Rename: ", "重命名：") + draftName, rect: Self.nameRect))
            for (index, icon) in AppShortcutIcon.allCases.enumerated() {
                actions.append(AppShortcutCanvasAction(id: "apps:icon:" + icon.rawValue,
                    label: icon.title + (icon == draftIcon ? L10n.text(", selected", "，已选择") : ""), rect: iconRect(index)))
            }
            actions.append(AppShortcutCanvasAction(id: "apps:cancel", label: L10n.text("Cancel", "取消"), rect: CGRect(x: 134, y: 274, width: 108, height: 28)))
            actions.append(AppShortcutCanvasAction(id: "apps:save", label: L10n.text("Save shortcut", "保存快捷方式"), rect: CGRect(x: 252, y: 274, width: 136, height: 28)))
        } else {
            for item in items {
                guard let rect = cardRect(for: item.id) else { continue }
                let full = rawCardRect(for: item.id)!
                let launch = CGRect(x: full.minX, y: full.minY, width: full.width - 70, height: full.height).intersection(rect)
                if launch.height >= 18 { actions.append(AppShortcutCanvasAction(id: action(item.id, "launch"), label: L10n.text("Open ", "打开 ") + item.name, rect: launch)) }
                let edit = CGRect(x: full.maxX - 64, y: full.minY + 10, width: 26, height: 26).intersection(rect)
                let remove = CGRect(x: full.maxX - 32, y: full.minY + 10, width: 26, height: 26).intersection(rect)
                if edit.height >= 18 { actions.append(AppShortcutCanvasAction(id: action(item.id, "edit"), label: L10n.text("Edit ", "编辑 ") + item.name, rect: edit)) }
                if remove.height >= 18 { actions.append(AppShortcutCanvasAction(id: action(item.id, "remove"), label: L10n.text("Remove shortcut: ", "移除快捷方式：") + item.name, rect: remove)) }
            }
        }
        return actions
    }

    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard point.x.isFinite, point.y.isFinite, layer.bounds.contains(point) else { return false }
        if let action = accessibleActions.first(where: { $0.rect.contains(point) }) { perform(actionID: action.id) }
        return true
    }
    func perform(actionID: String) {
        guard !isTransitioning else { return }
        if actionID == "apps:choose" { onChooseApplication?(); return }
        if actionID == "apps:cancel" { cancelDraft(); return }
        if actionID == "apps:save" { save(); return }
        if actionID == "apps:name", isEditing { onEditName?(Self.nameRect, draftName); return }
        if actionID.hasPrefix("apps:icon:"), isEditing,
           let icon = AppShortcutIcon(rawValue: String(actionID.dropFirst("apps:icon:".count))) {
            guard icon != draftIcon else { return }
            let previous = draftIcon
            draftIcon = icon; repaint(); animatePreset(from: previous, to: icon); onChange?(); return
        }
        guard !isEditing else { return }
        let fields = actionID.split(separator: ":")
        guard fields.count == 3, fields[0] == "apps", let id = UUID(uuidString: String(fields[1])),
              let item = items.first(where: { $0.id == id }) else { return }
        switch fields[2] {
        case "launch": onLaunch?(id)
        case "edit": edit(id)
        case "remove":
            do { try store?.remove(id: id); refreshFromStore(); onRemoved?(item.name) }
            catch { showError(error.localizedDescription) }
        default: break
        }
    }
    @discardableResult func scroll(at point: CGPoint, delta: CGFloat) -> Bool {
        guard !isEditing, !isTransitioning, Self.listRect.contains(point), delta.isFinite, abs(delta) > 0.001 else { return false }
        scrollOffset = min(maximumScroll, max(0, scrollOffset + delta))
        repaint(); onChange?(); return true
    }
    private var maximumScroll: CGFloat { max(0, CGFloat(items.count) * 52 - 6 - Self.listRect.height) }
    private func action(_ id: UUID, _ verb: String) -> String { "apps:\(id.uuidString):\(verb)" }
    private func rawCardRect(for id: UUID) -> CGRect? {
        guard let index = items.firstIndex(where: { $0.id == id }) else { return nil }
        return CGRect(x: 12, y: Self.listRect.minY + CGFloat(index) * 52 - scrollOffset, width: 376, height: 46)
    }
    func cardRect(for id: UUID) -> CGRect? {
        guard !isEditing, let rect = rawCardRect(for: id) else { return nil }
        let clipped = rect.intersection(Self.listRect)
        return clipped.isNull || clipped.height < 1 ? nil : clipped
    }
    private func iconRect(_ index: Int) -> CGRect {
        CGRect(x: 12 + CGFloat(index % 7) * 54, y: 140 + CGFloat(index / 7) * 55, width: 48, height: 48)
    }

    var activeAnimationCount: Int {
        func count(_ node: CALayer) -> Int {
            (node.animationKeys()?.filter { $0.hasPrefix("apps.") }.count ?? 0)
                + (node.sublayers ?? []).reduce(0) { $0 + count($1) }
                + (node.mask.map(count) ?? 0)
        }
        return count(layer)
    }

    /// Stop every finite local transition when this module leaves the shell.
    /// The new data remains committed, but a stale completion cannot open an
    /// AppKit field in a hidden section or a later draft.
    func cancelTransitions() {
        transitionGeneration += 1; transitionCompletion = nil; isTransitioning = false
        CATransaction.begin(); CATransaction.setDisableActions(true)
        removeAnimations(in: artwork); artwork.mask = nil; artwork.transform = CATransform3DIdentity
        if let outgoing { removeAnimations(in: outgoing); outgoing.removeFromSuperlayer() }
        outgoing = nil
        CATransaction.commit()
    }

    private func changeScreen(direction: CGFloat, editNameAfter: Bool = false, _ update: () -> Void) {
        onWillTransition?()
        cancelTransitions()
        CATransaction.begin(); CATransaction.setDisableActions(true)
        let previous = artwork
        artwork = CALayer(); artwork.frame = layer.bounds; artwork.allowsGroupOpacity = false
        rows = CALayer(); rows.frame = Self.listRect; rows.masksToBounds = true
        artwork.addSublayer(rows); layer.addSublayer(artwork)
        update(); repaint()
        CATransaction.commit()
        guard active, !shouldReduceMotion() else {
            previous.removeFromSuperlayer(); onChange?()
            if active && editNameAfter { onEditName?(Self.nameRect, draftName) }
            return
        }
        outgoing = previous; isTransitioning = true
        let token = transitionGeneration
        transitionCompletion = { [weak self] in
            guard let self, self.active, self.isEditing, editNameAfter else { return }
            self.onEditName?(Self.nameRect, self.draftName)
        }
        let reveal = CAShapeLayer(); reveal.frame = artwork.bounds
        reveal.fillColor = NSColor.black.cgColor
        reveal.path = shutterPath(progress: 1, direction: direction)
        let retract = CAShapeLayer(); retract.frame = previous.bounds
        retract.fillColor = NSColor.black.cgColor
        retract.path = shutterPath(progress: 0, direction: -direction)
        CATransaction.begin(); CATransaction.setDisableActions(true)
        artwork.mask = reveal; previous.mask = retract
        CATransaction.commit()
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self] in
            // Cancellation can synchronously finish a CA transaction. Leave
            // its completion group before attaching a native text editor.
            DispatchQueue.main.async { self?.finishTransition(generation: token) }
        }
        let timing = CAMediaTimingFunction(controlPoints: 0.2, 0.78, 0.27, 1)
        var approach = CATransform3DMakeTranslation(26 * direction, 0, -34)
        approach = CATransform3DRotate(approach, 0.045 * direction, 0, 1, 0)
        let arrival = CABasicAnimation(keyPath: "transform")
        arrival.fromValue = NSValue(caTransform3D: approach); arrival.toValue = NSValue(caTransform3D: CATransform3DIdentity)
        arrival.duration = Self.transitionDuration; arrival.timingFunction = timing
        artwork.add(arrival, forKey: "apps.transition.arrive")
        let departure = CABasicAnimation(keyPath: "transform")
        departure.fromValue = NSValue(caTransform3D: CATransform3DIdentity)
        departure.toValue = NSValue(caTransform3D: CATransform3DMakeTranslation(-18 * direction, 0, -28))
        departure.duration = Self.transitionDuration; departure.timingFunction = timing
        previous.add(departure, forKey: "apps.transition.depart")
        for (mask, from, to, travel) in [(reveal, CGFloat(0), CGFloat(1), direction), (retract, CGFloat(1), CGFloat(0), -direction)] {
            let wipe = CABasicAnimation(keyPath: "path")
            wipe.fromValue = shutterPath(progress: from, direction: travel)
            wipe.toValue = shutterPath(progress: to, direction: travel)
            wipe.duration = Self.transitionDuration; wipe.timingFunction = timing
            mask.add(wipe, forKey: "apps.transition.shutter")
        }
        CATransaction.commit()
        onChange?()
    }

    private func finishTransition(generation: Int) {
        guard active, isTransitioning, generation == transitionGeneration else { return }
        let completion = transitionCompletion
        cancelTransitions(); onChange?(); completion?()
    }
    private func shutterPath(progress: CGFloat, direction: CGFloat) -> CGPath {
        let width = layer.bounds.width, height = layer.bounds.height
        let edge = -24 + (width + 48) * progress
        let points = [CGPoint(x: 0, y: 0), CGPoint(x: edge, y: 0), CGPoint(x: edge - 20, y: height), CGPoint(x: 0, y: height)]
        let path = CGMutablePath()
        for (index, point) in points.enumerated() {
            let transformed = CGPoint(x: direction > 0 ? point.x : width - point.x, y: point.y)
            if index == 0 { path.move(to: transformed) } else { path.addLine(to: transformed) }
        }
        path.closeSubpath(); return path
    }
    private func animatePreset(from previous: AppShortcutIcon, to icon: AppShortcutIcon) {
        guard active, !shouldReduceMotion() else { return }
        let values = AppShortcutIcon.allCases
        let direction: CGFloat = (values.firstIndex(of: icon) ?? 0) > (values.firstIndex(of: previous) ?? 0) ? 1 : -1
        if let preview = artwork.sublayers?.first(where: { $0.name == "apps.preview" }) {
            let lock = CABasicAnimation(keyPath: "transform")
            lock.fromValue = NSValue(caTransform3D: CATransform3DMakeTranslation(direction * 7, 0, -18))
            lock.toValue = NSValue(caTransform3D: CATransform3DIdentity)
            lock.duration = 0.18; lock.timingFunction = CAMediaTimingFunction(name: .easeOut)
            preview.add(lock, forKey: "apps.preset.lock")
        }
        if let face = artwork.sublayers?.first(where: { $0.name == "apps.preset." + icon.rawValue }) {
            let rim = CAShapeLayer(); rim.frame = face.bounds; rim.fillColor = nil
            rim.strokeColor = yellow.cgColor; rim.lineWidth = 1.5
            rim.path = CGPath(roundedRect: rim.bounds.insetBy(dx: 0.75, dy: 0.75), cornerWidth: 3, cornerHeight: 3, transform: nil)
            rim.strokeEnd = 1; face.addSublayer(rim)
            let trace = CABasicAnimation(keyPath: "strokeEnd"); trace.fromValue = 0; trace.toValue = 1
            trace.duration = 0.18; trace.timingFunction = CAMediaTimingFunction(name: .easeOut)
            rim.add(trace, forKey: "apps.preset.register")
        }
    }
    private func removeAnimations(in node: CALayer) {
        node.removeAllAnimations(); node.sublayers?.forEach { removeAnimations(in: $0) }
        if let mask = node.mask { removeAnimations(in: mask) }
    }

    private func repaint() {
        CATransaction.begin(); CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        artwork.sublayers?.filter { $0 !== rows }.forEach { $0.removeFromSuperlayer() }
        rows.sublayers?.forEach { $0.removeFromSuperlayer() }
        rows.isHidden = isEditing
        text(L10n.text("APPLICATIONS", "应用快捷方式"), rect: CGRect(x: 12, y: 7, width: 248, height: 22), size: 15, weight: .bold, color: ink, parent: artwork)
        control(L10n.text("+ Choose app", "+ 选择应用"), rect: CGRect(x: 270, y: 2, width: 118, height: 28), parent: artwork)
        if isEditing { drawDraft() } else { drawList() }
        if let errorMessage {
            text(errorMessage, rect: CGRect(x: 12, y: 309, width: 376, height: 24), size: 10, color: dark ? .systemYellow : .systemRed, parent: artwork, wrapped: true)
        }
        if dropTarget {
            let border = CAShapeLayer(); border.frame = CGRect(x: 5, y: 36, width: 390, height: 266)
            border.path = CGPath(roundedRect: border.bounds, cornerWidth: 5, cornerHeight: 5, transform: nil)
            border.fillColor = yellow.withAlphaComponent(0.06).cgColor; border.strokeColor = yellow.cgColor; border.lineWidth = 2
            artwork.addSublayer(border)
        }
    }
    private func drawList() {
        if items.isEmpty {
            glyph(.grid, rect: CGRect(x: 171, y: 91, width: 58, height: 58), color: muted, parent: rows)
            return
        }
        for item in items {
            guard cardRect(for: item.id) != nil, let rect = rawCardRect(for: item.id) else { continue }
            let card = CALayer(); card.frame = rect.offsetBy(dx: -Self.listRect.minX, dy: -Self.listRect.minY)
            card.backgroundColor = NSColor(white: dark ? 0.95 : 0.15, alpha: dark ? 0.09 : 0.07).cgColor
            card.cornerRadius = 3; rows.addSublayer(card)
            HUDControlHighlightLayer.add(to: card, rect: CGRect(x: 0, y: 0, width: rect.width - 70, height: rect.height))
            if item.iconPreset == .original {
                if attemptedIcons.insert(item.id).inserted { icons[item.id] = image(store?.icon(for: item.id)) }
                drawImage(icons[item.id], rect: CGRect(x: 9, y: 7, width: 32, height: 32), parent: card)
            } else { glyph(item.iconPreset, rect: CGRect(x: 12, y: 10, width: 26, height: 26), color: yellow, parent: card) }
            text(item.name, rect: CGRect(x: 50, y: 7, width: 251, height: 17), size: 12, weight: .semibold, color: ink, parent: card)
            text(item.originalName, rect: CGRect(x: 50, y: 26, width: 251, height: 14), size: 9, color: muted, parent: card)
            control("✎", rect: CGRect(x: 312, y: 10, width: 26, height: 26), parent: card)
            control("×", rect: CGRect(x: 344, y: 10, width: 26, height: 26), parent: card)
        }
    }
    private func drawDraft() {
        let preview = CALayer(); preview.name = "apps.preview"; preview.frame = CGRect(x: 19, y: 55, width: 45, height: 45)
        artwork.addSublayer(preview)
        if draftIcon == .original { drawImage(draftImage, rect: preview.bounds, parent: preview) }
        else { glyph(draftIcon, rect: preview.bounds.insetBy(dx: 4, dy: 4), color: ink, parent: preview) }
        text(candidate?.name ?? "", rect: CGRect(x: 78, y: 48, width: 304, height: 21), size: 14, weight: .semibold, color: ink, parent: artwork)
        control(draftName.isEmpty ? L10n.text("Shortcut name", "快捷方式名称") : draftName, rect: Self.nameRect, parent: artwork, alignment: .left)
        text(L10n.text("ICON", "图标"), rect: CGRect(x: 12, y: 116, width: 180, height: 18), size: 10, weight: .bold, color: muted, parent: artwork)
        for (index, icon) in AppShortcutIcon.allCases.enumerated() {
            let rect = iconRect(index)
            let face = CALayer(); face.name = "apps.preset." + icon.rawValue; face.frame = rect; face.cornerRadius = 3
            face.backgroundColor = (icon == draftIcon ? yellow.withAlphaComponent(0.22) : NSColor(white: dark ? 1 : 0, alpha: 0.07)).cgColor
            face.borderWidth = icon == draftIcon ? 1.5 : 0.5
            face.borderColor = (icon == draftIcon ? yellow : muted.withAlphaComponent(0.3)).cgColor
            artwork.addSublayer(face)
            HUDControlHighlightLayer.add(to: face, rect: face.bounds)
            if icon == .original { drawImage(draftImage, rect: CGRect(x: 10, y: 10, width: 28, height: 28), parent: face) }
            else { glyph(icon, rect: CGRect(x: 12, y: 12, width: 24, height: 24), color: ink, parent: face) }
        }
        control(L10n.text("Cancel", "取消"), rect: CGRect(x: 134, y: 274, width: 108, height: 28), parent: artwork)
        control(L10n.text("Save shortcut", "保存快捷方式"), rect: CGRect(x: 252, y: 274, width: 136, height: 28), parent: artwork, highlighted: true)
    }
    private func text(_ value: String, rect: CGRect, size: CGFloat, weight: NSFont.Weight = .regular, color: NSColor, parent: CALayer, alignment: CATextLayerAlignmentMode = .left, wrapped: Bool = false) {
        let label = CATextLayer(); label.frame = rect; label.string = value
        label.font = NSFont.systemFont(ofSize: size, weight: weight); label.fontSize = size
        label.foregroundColor = color.cgColor; label.contentsScale = scale; label.alignmentMode = alignment
        label.isWrapped = wrapped; label.truncationMode = .end; parent.addSublayer(label)
    }
    private func control(_ title: String, rect: CGRect, parent: CALayer, highlighted: Bool = false, alignment: CATextLayerAlignmentMode = .center) {
        let face = CALayer(); face.frame = rect; face.cornerRadius = 3
        face.backgroundColor = (highlighted ? yellow : NSColor(white: dark ? 0.95 : 0.16, alpha: 0.12)).cgColor
        parent.addSublayer(face)
        HUDControlHighlightLayer.add(to: face, rect: face.bounds)
        text(title, rect: CGRect(x: 6, y: 6, width: rect.width - 12, height: rect.height - 8), size: 11, weight: .semibold,
             color: highlighted ? NSColor(white: 0.1, alpha: 1) : ink, parent: face, alignment: alignment)
    }
    private func image(_ image: NSImage?) -> CGImage? {
        guard let image else { return nil }
        var bounds = CGRect(x: 0, y: 0, width: 64, height: 64)
        return image.cgImage(forProposedRect: &bounds, context: nil, hints: nil)
    }
    private func drawImage(_ image: CGImage?, rect: CGRect, parent: CALayer) {
        guard let image else { glyph(.grid, rect: rect.insetBy(dx: 3, dy: 3), color: muted, parent: parent); return }
        let icon = CALayer(); icon.frame = rect; icon.contents = image; icon.contentsGravity = .resizeAspect; icon.contentsScale = scale
        parent.addSublayer(icon)
    }

    private func glyph(_ icon: AppShortcutIcon, rect: CGRect, color: NSColor, parent: CALayer) {
        parent.addSublayer(AppShortcutArtwork.makeGlyph(icon, rect: rect, color: color, contentsScale: scale))
    }
}
