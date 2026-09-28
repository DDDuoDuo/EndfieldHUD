import AppKit
import QuartzCore

struct HUDModuleContentStyle {
    let dark: Bool
    let accent: NSColor
    let contentsScale: CGFloat
}

/// A factory returns one host-local screen. Future modules can register their
/// own implementation without changing navigation or the mechanical shell.
protocol HUDModuleContentFactory {
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer
}

final class HUDModuleContent {
    static let transitionDuration: TimeInterval = 0.30
    static let viewportFrame = CGRect(x: 280, y: 100, width: 440, height: 440)
    let layer = CALayer()
    var selectedModule: HUDModule { state.selectedModule }
    var isTransitioning: Bool { state.isTransitioning }
    var registeredModuleCount: Int { factories.count + 1 } // Existing live Power screen.
    var contentLayerCount: Int { layer.sublayers?.count ?? 0 }
    var activeContentLayer: CALayer? { current.wrapper.sublayers?.first }
    var activeTransitionAnimationCount: Int {
        func count(_ item: CALayer) -> Int {
            let own = (item.animationKeys() ?? []).filter { $0.hasPrefix("module.") }.count
            return own + (item.sublayers ?? []).reduce(0) { $0 + count($1) } + (item.mask.map(count) ?? 0)
        }
        return count(layer)
    }

    private var state = HUDModuleSelectionState()
    private let powerLayer: CALayer
    private var factories: [HUDModule: HUDModuleContentFactory] = [:]
    private var current: Screen
    private var incoming: Screen?
    private var latestCompletion: (() -> Void)?
    private var style = HUDModuleContentStyle(dark: true,
        accent: NSColor(srgbRed: 0.85, green: 0.95, blue: 0.42, alpha: 1), contentsScale: 2)
    private var languageIsChinese = L10n.isChinese
    private let shouldReduceMotion: () -> Bool

    init(powerLayer: CALayer, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.powerLayer = powerLayer
        shouldReduceMotion = reduceMotion
        current = Screen(module: .power)
        withoutActions {
            layer.name = "module.host"
            layer.frame = Self.viewportFrame
            layer.masksToBounds = true
            layer.allowsGroupOpacity = false
            current.wrapper.addSublayer(powerLayer)
            powerLayer.frame = Self.localContentFrame(.power)
            layer.addSublayer(current.wrapper)
        }
        let placeholder = HUDPlaceholderContentFactory()
        for module in HUDModule.allCases where module != .power { factories[module] = placeholder }
    }

    deinit { removeAnimations(in: layer) }

    /// Power remains owned by the existing battery view and cannot be replaced.
    @discardableResult
    func register(_ factory: HUDModuleContentFactory, for module: HUDModule) -> Bool {
        precondition(Thread.isMainThread)
        guard module != .power else { return false }
        factories[module] = factory
        for screen in [current, incoming].compactMap({ $0 }) where screen.module == module { rebuild(screen) }
        return true
    }

    /// A newer request replaces the pending destination and completion. Only the
    /// final requested screen calls back, after all necessary swaps have settled.
    func select(module: HUDModule, animated: Bool, completion: (() -> Void)? = nil) {
        precondition(Thread.isMainThread)
        latestCompletion = completion
        guard animated, !shouldReduceMotion() else {
            settle(on: module, notify: true)
            return
        }
        if let transition = state.request(module) { begin(transition) }
        else if !state.isTransitioning { finishCallback() }
    }

    /// Commit the most recent request immediately, including a queued request.
    func settle() {
        precondition(Thread.isMainThread)
        settle(on: state.requestedModule, notify: true)
    }

    /// Close/detach cleanup: invalidate completions and restore the last fully
    /// committed screen. No hidden incoming layer or transition survives.
    func cancel() {
        precondition(Thread.isMainThread)
        state.cancel()
        latestCompletion = nil
        removeAnimations(in: layer)
        withoutActions {
            incoming?.wrapper.removeFromSuperlayer()
            incoming = nil
            normalize(current)
        }
    }

    func update(dark: Bool, accent: NSColor, contentsScale: CGFloat) {
        precondition(Thread.isMainThread)
        let color = accent.usingColorSpace(.deviceRGB) ?? accent
        let scale = contentsScale.isFinite ? min(8, max(1, contentsScale)) : 2
        let repaint = style.dark != dark || !style.accent.isEqual(color) || languageIsChinese != L10n.isChinese
        style = HUDModuleContentStyle(dark: dark, accent: color, contentsScale: scale)
        languageIsChinese = L10n.isChinese
        for screen in [current, incoming].compactMap({ $0 }) {
            if repaint && screen.module != .power { rebuild(screen) }
            applyScale(to: screen.wrapper)
        }
    }

    private func begin(_ transition: HUDModuleSelectionState.Transition) {
        let next = makeScreen(transition.to)
        incoming = next
        let direction = Self.direction(from: transition.from, to: transition.to)
        let reveal = makeMask()
        let retract = makeMask()
        withoutActions {
            next.wrapper.mask = reveal
            current.wrapper.mask = retract
            layer.addSublayer(next.wrapper)
            reveal.path = Self.shutterPath(progress: 1, direction: direction)
            retract.path = Self.shutterPath(progress: 0, direction: CGPoint(x: -direction.x, y: -direction.y))
            current.wrapper.transform = Self.offset(direction: CGPoint(x: -direction.x, y: -direction.y), distance: 25, depth: -55)
        }
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak self] in
            // A removal can complete a transaction synchronously. Deferring also
            // prevents a queued swap joining this transaction's completion group.
            DispatchQueue.main.async { [weak self] in self?.finish(transition) }
        }
        addContentLock(to: next.wrapper, direction: direction)
        addTransform(to: current.wrapper, from: CATransform3DIdentity, to: current.wrapper.transform, duration: 0.20)
        addShutter(to: reveal, direction: direction, revealing: true)
        addShutter(to: retract, direction: CGPoint(x: -direction.x, y: -direction.y), revealing: false)
        addRegistration(to: next.wrapper, direction: direction)
        CATransaction.commit()
    }

    private func finish(_ transition: HUDModuleSelectionState.Transition) {
        guard state.isTransitioning, state.generation == transition.generation,
              let next = incoming, next.module == transition.to else { return }
        // Sublayer removal otherwise adds Core Animation's default host
        // transition after the explicit mechanical wipe has already finished.
        withoutActions {
            removeAnimations(in: current.wrapper)
            current.wrapper.removeFromSuperlayer()
            current = next
            incoming = nil
            normalize(current)
        }
        if let following = state.complete(generation: transition.generation) { begin(following) }
        else { finishCallback() }
    }

    private func settle(on module: HUDModule, notify: Bool) {
        state.settle(on: module)
        removeAnimations(in: layer)
        let chosen: Screen
        if current.module == module { chosen = current }
        else if let next = incoming, next.module == module { chosen = next }
        else { chosen = makeScreen(module) }
        withoutActions {
            for child in layer.sublayers ?? [] { child.removeFromSuperlayer() }
            current = chosen
            incoming = nil
            normalize(chosen)
            layer.addSublayer(chosen.wrapper)
        }
        if notify { finishCallback() }
    }

    private func finishCallback() {
        let callback = latestCompletion
        latestCompletion = nil
        callback?()
    }

    private func makeScreen(_ module: HUDModule) -> Screen {
        let screen = Screen(module: module)
        rebuild(screen)
        return screen
    }

    private func rebuild(_ screen: Screen) {
        withoutActions {
            // Keep an in-flight registration frontier attached when a theme or
            // language repaint replaces the actual content beneath it.
            for child in screen.wrapper.sublayers ?? [] where child.name != "module.registration" { child.removeFromSuperlayer() }
            let content = screen.module == .power ? powerLayer
                : factories[screen.module]!.makeContent(for: screen.module, style: style)
            content.frame = Self.localContentFrame(screen.module)
            screen.wrapper.insertSublayer(content, at: 0)
            for child in screen.wrapper.sublayers ?? [] where child.name == "module.registration" {
                (child as? CAShapeLayer)?.strokeColor = registrationColor.cgColor
            }
            applyScale(to: content)
        }
    }

    private func normalize(_ screen: Screen) {
        withoutActions {
            removeAnimations(in: screen.wrapper)
            screen.wrapper.transform = CATransform3DIdentity
            screen.wrapper.opacity = 1
            screen.wrapper.mask = nil
            screen.wrapper.sublayers?.filter { $0.name == "module.registration" }.forEach { $0.removeFromSuperlayer() }
        }
    }

    private func makeMask() -> CAShapeLayer {
        let mask = CAShapeLayer()
        mask.frame = CGRect(origin: .zero, size: Self.viewportFrame.size)
        mask.fillColor = NSColor.black.cgColor
        return mask
    }

    private func addTransform(to layer: CALayer, from: CATransform3D, to: CATransform3D, duration: Double) {
        let animation = CABasicAnimation(keyPath: "transform")
        animation.fromValue = NSValue(caTransform3D: from)
        animation.toValue = NSValue(caTransform3D: to)
        animation.duration = duration
        animation.timingFunction = CAMediaTimingFunction(controlPoints: 0.18, 0.72, 0.26, 1)
        layer.add(animation, forKey: "module.transform")
    }

    private static let registrationTimes: [NSNumber] = [0, 0.16, 0.23, 0.43, 0.50, 0.79, 1]
    private static let revealProgress: [CGFloat] = [0, 0.18, 0.165, 0.53, 0.515, 0.88, 1]
    private static let shutterLags: [CGFloat] = [0.025, 0.13, 0, 0.08, 0.035, 0.105]
    private var registrationColor: NSColor { NSColor(white: style.dark ? 0.77 : 0.22, alpha: 0.38) }

    /// The content itself registers in two small corrections. It remains fully
    /// opaque; staggered shutter geometry, rather than bright overlaid bars,
    /// determines which parts can be seen during the mechanical handoff.
    private func addContentLock(to wrapper: CALayer, direction: CGPoint) {
        let values: [(CGFloat, CGFloat, CGFloat, CGFloat)] = [
            (32, -54, 0, 0), (7, -13, 1.8, 0.004), (10, -15, -1.2, -0.003),
            (2.2, -4, 0.7, 0.002), (3.4, -5, -0.4, -0.001), (0.4, -0.6, 0, 0)
        ]
        var transforms = values.map { distance, depth, lateral, shear -> NSValue in
            var transform = Self.offset(direction: direction, distance: distance, depth: depth, tilt: distance / 32)
            transform = CATransform3DTranslate(transform, -direction.y * lateral, direction.x * lateral, 0)
            if direction.x != 0 { transform.m21 += shear } else { transform.m12 += shear }
            return NSValue(caTransform3D: transform)
        }
        transforms.append(NSValue(caTransform3D: CATransform3DIdentity))
        let animation = CAKeyframeAnimation(keyPath: "transform")
        animation.values = transforms
        animation.keyTimes = Self.registrationTimes
        animation.timingFunctions = Self.registrationTimingFunctions()
        animation.duration = Self.transitionDuration
        wrapper.add(animation, forKey: "module.transform")
    }

    private func addShutter(to mask: CAShapeLayer, direction: CGPoint, revealing: Bool) {
        let progress: [CGFloat] = revealing ? Self.revealProgress : [1, 0.84, 0.85, 0.40, 0.415, 0.10, 0]
        let animation = CAKeyframeAnimation(keyPath: "path")
        animation.values = progress.map { Self.shutterPath(progress: $0, direction: direction) }
        animation.keyTimes = Self.registrationTimes
        animation.timingFunctions = Self.registrationTimingFunctions()
        animation.duration = revealing ? Self.transitionDuration : 0.22
        mask.add(animation, forKey: "module.shutter")
    }

    /// Fine neutral seams sit just inside the real reveal frontier. They follow
    /// the same stagger and corrections as the mask, then disappear completely.
    private func addRegistration(to wrapper: CALayer, direction: CGPoint) {
        let seam = CAShapeLayer()
        seam.name = "module.registration"; seam.frame = wrapper.bounds
        seam.path = Self.registrationPath(progress: 1, direction: direction)
        seam.fillColor = nil; seam.strokeColor = registrationColor.cgColor
        seam.lineWidth = 0.7; seam.lineCap = .butt; seam.lineJoin = .miter
        seam.contentsScale = style.contentsScale
        seam.opacity = 0
        wrapper.addSublayer(seam)
        let path = CAKeyframeAnimation(keyPath: "path")
        path.values = Self.revealProgress.map { Self.registrationPath(progress: $0, direction: direction) }
        path.keyTimes = Self.registrationTimes
        path.timingFunctions = Self.registrationTimingFunctions()
        path.duration = Self.transitionDuration
        let opacity = CAKeyframeAnimation(keyPath: "opacity")
        opacity.values = [0, 0.48, 0.24, 0.42, 0.25, 0.10, 0]
        opacity.keyTimes = Self.registrationTimes
        opacity.duration = Self.transitionDuration
        let group = CAAnimationGroup(); group.animations = [path, opacity]
        group.duration = Self.transitionDuration
        seam.add(group, forKey: "module.registration")
    }

    private static func registrationTimingFunctions() -> [CAMediaTimingFunction] {
        [.easeOut, .linear, .easeOut, .linear, .easeOut, .easeInEaseOut].map { CAMediaTimingFunction(name: $0) }
    }

    private static func shutterExtent(_ progress: CGFloat, index: Int) -> CGFloat {
        let lag = shutterLags[index]
        return min(1, max(0, progress * (1 + lag) - lag)) * viewportFrame.width
    }

    /// Every keyframe has identical polygon topology. At the endpoints the
    /// six shutters collapse to zero area or tile the entire viewport exactly.
    private static func shutterPath(progress: CGFloat, direction: CGPoint) -> CGPath {
        let path = CGMutablePath(), length = viewportFrame.width
        let step = length / CGFloat(shutterLags.count)
        for index in shutterLags.indices {
            let edge = shutterExtent(progress, index: index)
            let low = max(0, CGFloat(index) * step - 0.3)
            let high = min(length, CGFloat(index + 1) * step + 0.3)
            let bevel = min(5, edge * 0.15, (length - edge) * 0.15)
            let points = [CGPoint(x: 0, y: low), CGPoint(x: edge, y: low),
                          CGPoint(x: edge, y: high - bevel), CGPoint(x: max(0, edge - bevel), y: high), CGPoint(x: 0, y: high)]
                .map { oriented($0, direction: direction) }
            path.move(to: points[0]); points.dropFirst().forEach { path.addLine(to: $0) }; path.closeSubpath()
        }
        return path
    }

    private static func registrationPath(progress: CGFloat, direction: CGPoint) -> CGPath {
        let path = CGMutablePath(), step = viewportFrame.width / CGFloat(shutterLags.count)
        for index in shutterLags.indices {
            let edge = max(0, shutterExtent(progress, index: index) - 0.8)
            let low = CGFloat(index) * step + 12, high = CGFloat(index + 1) * step - 12
            let points = [CGPoint(x: max(0, edge - 4), y: low), CGPoint(x: edge, y: low),
                          CGPoint(x: edge, y: high), CGPoint(x: max(0, edge - 4), y: high)]
                .map { oriented($0, direction: direction) }
            path.move(to: points[0]); points.dropFirst().forEach { path.addLine(to: $0) }
        }
        return path
    }

    private static func oriented(_ point: CGPoint, direction: CGPoint) -> CGPoint {
        let length = viewportFrame.width
        if direction.x != 0 { return CGPoint(x: direction.x < 0 ? point.x : length - point.x, y: point.y) }
        return CGPoint(x: point.y, y: direction.y < 0 ? point.x : length - point.x)
    }

    private func removeAnimations(in item: CALayer) {
        for key in item.animationKeys() ?? [] where key.hasPrefix("module.") { item.removeAnimation(forKey: key) }
        item.sublayers?.forEach { removeAnimations(in: $0) }
        if let mask = item.mask { removeAnimations(in: mask) }
    }

    private func applyScale(to item: CALayer) {
        if !(item is CATransformLayer) {
            item.contentsScale = HUDRenderScale.contentScale(for: item, baseScale: style.contentsScale)
        }
        item.sublayers?.forEach { applyScale(to: $0) }
        if let mask = item.mask { applyScale(to: mask) }
    }

    private static func direction(from: HUDModule, to: HUDModule) -> CGPoint {
        if from.group == to.group {
            let a = HUDModule.allCases.firstIndex(of: from) ?? 0
            let b = HUDModule.allCases.firstIndex(of: to) ?? 0
            return CGPoint(x: 0, y: b > a ? 1 : -1)
        }
        switch to.group {
        case .left: return CGPoint(x: -1, y: 0)
        case .right: return CGPoint(x: 1, y: 0)
        case .bottom: return CGPoint(x: 0, y: 1)
        case .power:
            switch from.group {
            case .left: return CGPoint(x: 1, y: 0)
            case .right: return CGPoint(x: -1, y: 0)
            case .bottom, .power: return CGPoint(x: 0, y: -1)
            }
        }
    }

    private static func offset(direction: CGPoint, distance: CGFloat, depth: CGFloat, tilt: CGFloat = 1) -> CATransform3D {
        var result = CATransform3DIdentity
        result.m34 = -1 / 600
        result = CATransform3DTranslate(result, direction.x * distance, direction.y * distance, depth)
        result = CATransform3DRotate(result, -direction.y * 0.13 * tilt, 1, 0, 0)
        return CATransform3DRotate(result, direction.x * 0.16 * tilt, 0, 1, 0)
    }

    private static func localContentFrame(_ module: HUDModule) -> CGRect {
        module.contentFrame.offsetBy(dx: -viewportFrame.minX, dy: -viewportFrame.minY)
    }

    private func withoutActions(_ action: () -> Void) {
        CATransaction.begin(); CATransaction.setDisableActions(true); action(); CATransaction.commit()
    }

    private final class Screen {
        let module: HUDModule
        let wrapper = CALayer()
        init(module: HUDModule) {
            self.module = module
            wrapper.name = "module.screen." + module.rawValue
            wrapper.frame = CGRect(origin: .zero, size: HUDModuleContent.viewportFrame.size)
            wrapper.allowsGroupOpacity = false
        }
    }
}

/// Deliberately noninteractive diagrams: no sample readings, enabled controls,
/// recorded content, or implied services appear in an unfinished module.
private struct HUDPlaceholderContentFactory: HUDModuleContentFactory {
    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        let root = CALayer()
        root.name = "module.placeholder." + module.rawValue
        root.bounds = CGRect(x: 0, y: 0, width: 400, height: 334)
        let primary = NSColor(white: style.dark ? 0.92 : 0.13, alpha: 1)
        let muted = NSColor(white: style.dark ? 0.61 : 0.40, alpha: 1)
        let line = style.accent.withAlphaComponent(style.dark ? 0.72 : 0.82)
        label(module.title, frame: CGRect(x: 20, y: 20, width: 360, height: 32), size: 23,
              color: primary, parent: root, scale: style.contentsScale)
        label(L10n.text("Not configured yet", "尚未配置"), frame: CGRect(x: 20, y: 270, width: 360, height: 25),
              size: 14, color: muted, parent: root, scale: style.contentsScale)
        let art = CAShapeLayer()
        art.frame = CGRect(x: 110, y: 88, width: 180, height: 138)
        art.path = motif(module)
        art.fillColor = nil
        art.strokeColor = line.cgColor
        art.lineWidth = 1.8
        art.lineJoin = .round
        art.lineCap = .round
        root.addSublayer(art)
        return root
    }

    private func label(_ text: String, frame: CGRect, size: CGFloat, color: NSColor, parent: CALayer, scale: CGFloat) {
        let layer = CATextLayer()
        layer.frame = frame
        layer.string = text
        layer.font = NSFont.systemFont(ofSize: size, weight: .medium)
        layer.fontSize = size
        layer.foregroundColor = color.cgColor
        layer.alignmentMode = .center
        layer.truncationMode = .end
        layer.contentsScale = HUDRenderScale.contentScale(for: layer, baseScale: scale)
        parent.addSublayer(layer)
    }

    private func motif(_ module: HUDModule) -> CGPath {
        let p = CGMutablePath()
        func line(_ points: [CGPoint]) {
            guard let first = points.first else { return }
            p.move(to: first); points.dropFirst().forEach { p.addLine(to: $0) }
        }
        func rect(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) { p.addRect(CGRect(x: x, y: y, width: w, height: h)) }
        func circle(_ x: CGFloat, _ y: CGFloat, _ r: CGFloat) { p.addEllipse(in: CGRect(x: x-r, y: y-r, width: r*2, height: r*2)) }
        switch module {
        case .notes:
            line([CGPoint(x: 45,y: 5), CGPoint(x: 116,y: 5), CGPoint(x: 139,y: 28), CGPoint(x: 139,y: 130), CGPoint(x: 45,y: 130), CGPoint(x: 45,y: 5)])
            line([CGPoint(x: 116,y: 5), CGPoint(x: 116,y: 28), CGPoint(x: 139,y: 28)])
            for y: CGFloat in [54, 75, 96] { line([CGPoint(x: 63,y: y), CGPoint(x: 118,y: y)]) }
        case .fileShelf:
            for y: CGFloat in [42, 76, 110] { line([CGPoint(x: 14,y: y-13), CGPoint(x: 14,y: y+10), CGPoint(x: 166,y: y+10), CGPoint(x: 166,y: y-13)]) }
            line([CGPoint(x: 34,y: 29), CGPoint(x: 34,y: 6), CGPoint(x: 75,y: 6), CGPoint(x: 85,y: 16), CGPoint(x: 147,y: 16), CGPoint(x: 147,y: 29)])
        case .clipboard:
            rect(45, 17, 92, 112); rect(69, 7, 44, 21)
            for y: CGFloat in [53, 79, 105] { rect(60,y-4,8,8); line([CGPoint(x: 79,y:y), CGPoint(x:121,y:y)]) }
        case .volume:
            line([CGPoint(x: 20,y: 51), CGPoint(x: 49,y: 51), CGPoint(x: 81,y: 24), CGPoint(x: 81,y: 114), CGPoint(x: 49,y: 87), CGPoint(x: 20,y: 87), CGPoint(x: 20,y: 51)])
            for r: CGFloat in [30, 53, 76] { p.addArc(center: CGPoint(x: 80,y:69), radius: r, startAngle: -.pi/3, endAngle: .pi/3, clockwise: false) }
        case .workMode:
            rect(19, 42, 142, 83); rect(63, 18, 54, 24)
            line([CGPoint(x: 19,y:70), CGPoint(x:75,y:85), CGPoint(x:105,y:85), CGPoint(x:161,y:70)])
            rect(75, 76, 30, 23)
        case .eventLog:
            line([CGPoint(x: 38,y: 16), CGPoint(x:38,y:124)])
            for y: CGFloat in [28, 70, 112] { circle(38,y,7); line([CGPoint(x: 61,y:y-6), CGPoint(x: 146,y:y-6)]); line([CGPoint(x: 61,y:y+8), CGPoint(x: 116,y:y+8)]) }
        case .map:
            line([CGPoint(x: 20,y: 30), CGPoint(x: 65,y: 10), CGPoint(x: 115,y: 30), CGPoint(x: 160,y: 10), CGPoint(x: 160,y: 112), CGPoint(x: 115,y: 132), CGPoint(x: 65,y: 112), CGPoint(x: 20,y: 132), CGPoint(x: 20,y: 30)])
            line([CGPoint(x: 65,y: 10), CGPoint(x: 65,y: 112)])
            line([CGPoint(x: 115,y: 30), CGPoint(x: 115,y: 132)])
        case .addApp:
            rect(28,18,51,45); rect(101,18,51,45); rect(28,85,51,45)
            line([CGPoint(x: 102,y:108), CGPoint(x: 153,y:108)]); line([CGPoint(x: 127,y:84), CGPoint(x:127,y:132)])
        case .system:
            rect(49, 29, 82, 80); rect(65,45,50,48)
            for x: CGFloat in [62, 90, 118] { line([CGPoint(x:x,y:12),CGPoint(x:x,y:29)]); line([CGPoint(x:x,y:109),CGPoint(x:x,y:126)]) }
            for y: CGFloat in [41, 69, 97] { line([CGPoint(x:32,y:y),CGPoint(x:49,y:y)]); line([CGPoint(x:131,y:y),CGPoint(x:148,y:y)]) }
        case .display:
            rect(13, 15, 154, 95); rect(22,24,136,71)
            line([CGPoint(x:90,y:110), CGPoint(x:90,y:130)]); line([CGPoint(x:56,y:130), CGPoint(x:124,y:130)])
        case .hotkeys:
            rect(6, 20, 67, 48); rect(89, 20, 67, 48); rect(47, 87, 126, 42)
            line([CGPoint(x:26,y:43),CGPoint(x:53,y:43)]); line([CGPoint(x:116,y:34),CGPoint(x:116,y:54)])
            line([CGPoint(x:105,y:44),CGPoint(x:128,y:44)]); line([CGPoint(x:67,y:108),CGPoint(x:152,y:108)])
        case .about:
            circle(90,69,58); circle(90,43,4)
            line([CGPoint(x:79,y:65),CGPoint(x:91,y:65),CGPoint(x:91,y:99)])
            line([CGPoint(x:77,y:100),CGPoint(x:104,y:100)])
        case .storage:
            p.addEllipse(in: CGRect(x:29,y:10,width:122,height:31))
            for y: CGFloat in [48,82,116] {
                p.move(to: CGPoint(x:29,y:y-17)); p.addLine(to: CGPoint(x:29,y:y));
                p.addCurve(to: CGPoint(x:151,y:y), control1: CGPoint(x:29,y:y+22), control2: CGPoint(x:151,y:y+22))
                p.addLine(to: CGPoint(x:151,y:y-17))
            }
        case .activityMonitor:
            rect(9, 19, 162, 107)
            line([CGPoint(x:24,y:78),CGPoint(x:49,y:78),CGPoint(x:64,y:48),CGPoint(x:84,y:105),CGPoint(x:107,y:35),CGPoint(x:129,y:78),CGPoint(x:157,y:78)])
        case .profile:
            circle(90, 40, 24); rect(48, 82, 84, 44)
        case .power: break // The real battery screen is injected, never fabricated.
        }
        return p
    }
}
