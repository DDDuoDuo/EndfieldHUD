import AppKit
import QuartzCore

/// Render context shared by a canvas and its components. Reduce Motion is the
/// system setting or the app's own (HUDRuntimeAppearance); tests inject it.
final class HUDDesignEnvironment {
    var renderScale: CGFloat
    private let reduceMotionProvider: () -> Bool

    init(renderScale: CGFloat = 2, reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.renderScale = renderScale
        reduceMotionProvider = reduceMotion
    }

    var reducesMotion: Bool { reduceMotionProvider() }

    /// Backing scale for one layer (text is oversampled for the tilted plane).
    func scale(for layer: CALayer) -> CGFloat { HUDRenderScale.contentScale(for: layer, baseScale: renderScale) }

    /// Applies the backing scale to a subtree; called when it is built or the scale changes.
    func applyScale(to layer: CALayer) {
        if !(layer is CATransformLayer), !layer.isArtwork { layer.contentsScale = scale(for: layer) }
        layer.sublayers?.forEach(applyScale)
        layer.mask.map(applyScale)
    }
}

private extension CALayer {
    /// Cached raster motifs carry the scale they were drawn at.
    var isArtwork: Bool { contents != nil && !(self is HUDTextLayer) }
}

/// The motion system (spec §1.9). Every animation is explicit, finite and keyed
/// "hud.*"; model values always hold the end state, so Reduce Motion simply
/// skips the animation and an interrupted one never leaves a layer mid-way.
enum HUDMotion {
    static func perform(_ changes: () -> Void) {
        CATransaction.begin(); CATransaction.setDisableActions(true); changes(); CATransaction.commit()
    }

    /// Moves `keyPath` to `value`, animating from what is on screen now.
    static func animate(_ layer: CALayer, _ keyPath: String, to value: Any?, duration: CFTimeInterval,
                        timing: CAMediaTimingFunction = HUDEasing.standard, delay: CFTimeInterval = 0,
                        env: HUDDesignEnvironment, key: String? = nil) {
        let key = key ?? "hud." + keyPath
        let from = (layer.presentation() ?? layer).value(forKeyPath: keyPath)
        perform { layer.setValue(value, forKeyPath: keyPath) }
        layer.removeAnimation(forKey: key)
        guard !env.reducesMotion, duration > 0, layer.superlayer != nil || layer.presentation() != nil else { return }
        let animation = CABasicAnimation(keyPath: keyPath)
        animation.fromValue = from; animation.toValue = value
        animation.duration = duration; animation.timingFunction = timing
        if delay > 0 { animation.beginTime = layer.convertTime(CACurrentMediaTime(), from: nil) + delay; animation.fillMode = .backwards }
        layer.add(animation, forKey: key)
    }

    /// stIn(dx, dy): fades in (opaque at 60 %) while sliding from `offset` to rest.
    static func enter(_ layer: CALayer, from offset: CGPoint, duration: CFTimeInterval = HUDDuration.entrance,
                      delay: CFTimeInterval = 0, timing: CAMediaTimingFunction = HUDEasing.standard,
                      env: HUDDesignEnvironment, key: String = "hud.enter") {
        layer.removeAnimation(forKey: key)
        guard !env.reducesMotion else { return }
        let opacity = CAKeyframeAnimation(keyPath: "opacity")
        opacity.values = [0, layer.opacity, layer.opacity]; opacity.keyTimes = [0, 0.6, 1]
        var animations: [CAAnimation] = [opacity]
        for (axis, distance) in [("x", offset.x), ("y", offset.y)] where distance != 0 {
            let move = CABasicAnimation(keyPath: "transform.translation." + axis)
            move.fromValue = distance; move.toValue = 0; move.isAdditive = true; move.timingFunction = timing
            animations.append(move)
        }
        let group = CAAnimationGroup()
        group.animations = animations; group.duration = duration; group.timingFunction = timing
        if delay > 0 { group.beginTime = layer.convertTime(CACurrentMediaTime(), from: nil) + delay; group.fillMode = .backwards }
        layer.add(group, forKey: key)
    }

    /// The list stagger: 36 ms apart after 60 ms, index capped at 14 (spec §1.10).
    static func staggerDelay(_ index: Int) -> CFTimeInterval {
        HUDDuration.staggerBase + Double(min(max(0, index), HUDDuration.staggerCap)) * HUDDuration.staggerStep
    }

    static func stagger(_ layers: [CALayer], from offset: CGPoint, env: HUDDesignEnvironment) {
        for (index, layer) in layers.enumerated() { enter(layer, from: offset, delay: staggerDelay(index), env: env) }
    }

    /// The leave: .13 s to transparent while moving −0.6 × the new direction.
    /// Holds its end state; the caller removes the layer when the swap happens.
    static func leave(_ layer: CALayer, toward offset: CGPoint, env: HUDDesignEnvironment, key: String = "hud.leave") {
        guard !env.reducesMotion else { return }
        let fade = CABasicAnimation(keyPath: "opacity")
        fade.fromValue = (layer.presentation() ?? layer).opacity; fade.toValue = 0
        var animations: [CAAnimation] = [fade]
        for (axis, distance) in [("x", -0.6 * offset.x), ("y", -0.6 * offset.y)] where distance != 0 {
            let move = CABasicAnimation(keyPath: "transform.translation." + axis)
            move.fromValue = 0; move.toValue = distance; move.isAdditive = true
            animations.append(move)
        }
        let group = CAAnimationGroup()
        group.animations = animations; group.duration = HUDDuration.leave; group.timingFunction = HUDEasing.ease
        group.fillMode = .forwards; group.isRemovedOnCompletion = false
        layer.add(group, forKey: key)
    }

    enum WipeOrigin { case left, top, centerX }

    /// clip-path wipes (flick, menuIn, tabOn, bandIn): a temporary mask grows
    /// from `origin`; the mask is removed when it finishes, so a settled layer
    /// never pays for offscreen masking.
    static func wipe(_ layer: CALayer, from origin: WipeOrigin, duration: CFTimeInterval,
                     timing: CAMediaTimingFunction = HUDEasing.standard, delay: CFTimeInterval = 0,
                     fadeFrom: Float? = nil, env: HUDDesignEnvironment, key: String = "hud.wipe") {
        removeWipe(layer, key: key)
        guard !env.reducesMotion else { return }
        let mask = HUDLayer.plain("hud.wipe.mask")
        mask.backgroundColor = HUDColor.black(1)
        let bounds = layer.bounds
        switch origin {
        case .left: mask.anchorPoint = CGPoint(x: 0, y: 0.5)
        case .top: mask.anchorPoint = CGPoint(x: 0.5, y: 0)
        case .centerX: mask.anchorPoint = CGPoint(x: 0.5, y: 0.5)
        }
        mask.bounds = CGRect(origin: .zero, size: bounds.size)
        mask.position = CGPoint(x: bounds.minX + bounds.width * mask.anchorPoint.x, y: bounds.minY + bounds.height * mask.anchorPoint.y)
        layer.mask = mask
        let grow = CABasicAnimation(keyPath: origin == .top ? "transform.scale.y" : "transform.scale.x")
        grow.fromValue = 0; grow.toValue = 1; grow.duration = duration; grow.timingFunction = timing
        let begin = delay > 0 ? layer.convertTime(CACurrentMediaTime(), from: nil) + delay : 0
        if delay > 0 { grow.beginTime = begin; grow.fillMode = .backwards }
        CATransaction.begin()
        CATransaction.setCompletionBlock { [weak layer, weak mask] in
            guard let layer, let mask, layer.mask === mask else { return }
            perform { layer.mask = nil }
        }
        mask.add(grow, forKey: key)
        if let fadeFrom {
            let fade = CABasicAnimation(keyPath: "opacity")
            fade.fromValue = fadeFrom * layer.opacity; fade.toValue = layer.opacity
            fade.duration = duration; fade.timingFunction = timing
            if delay > 0 { fade.beginTime = begin; fade.fillMode = .backwards }
            layer.add(fade, forKey: key + ".fade")
        }
        CATransaction.commit()
    }

    static func removeWipe(_ layer: CALayer, key: String = "hud.wipe") {
        layer.removeAnimation(forKey: key + ".fade")
        if layer.mask?.name == "hud.wipe.mask" { perform { layer.mask = nil } }
    }

    /// ruleIn: scaleX(0) → none. The layer's anchor sets the origin (left or centre).
    static func grow(_ layer: CALayer, duration: CFTimeInterval, delay: CFTimeInterval = 0,
                     timing: CAMediaTimingFunction = HUDEasing.standard, env: HUDDesignEnvironment, key: String = "hud.grow") {
        layer.removeAnimation(forKey: key)
        guard !env.reducesMotion else { return }
        let grow = CABasicAnimation(keyPath: "transform.scale.x")
        grow.fromValue = 0; grow.toValue = 1; grow.duration = duration; grow.timingFunction = timing
        if delay > 0 { grow.beginTime = layer.convertTime(CACurrentMediaTime(), from: nil) + delay; grow.fillMode = .backwards }
        layer.add(grow, forKey: key)
    }

    /// pop: scale(.4) rotate(−12°), transparent → rest.
    static func pop(_ layer: CALayer, duration: CFTimeInterval = HUDDuration.pop, delay: CFTimeInterval = 0, env: HUDDesignEnvironment) {
        layer.removeAnimation(forKey: "hud.pop")
        guard !env.reducesMotion else { return }
        let scale = CABasicAnimation(keyPath: "transform.scale"); scale.fromValue = 0.4; scale.toValue = 1
        let rotate = CABasicAnimation(keyPath: "transform.rotation.z")
        rotate.fromValue = -12 * CGFloat.pi / 180; rotate.toValue = 0; rotate.isAdditive = true
        let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 0; fade.toValue = layer.opacity
        let group = CAAnimationGroup()
        group.animations = [scale, rotate, fade]; group.duration = duration; group.timingFunction = HUDEasing.snap
        if delay > 0 { group.beginTime = layer.convertTime(CACurrentMediaTime(), from: nil) + delay; group.fillMode = .backwards }
        layer.add(group, forKey: "hud.pop")
    }
}

/// Shell positions of the modules (spec Appendix A, stage px). The direction of
/// a module switch points from the previous module's place to the new one's.
enum HUDShellPosition {
    static let discCentre = CGPoint(x: 975, y: 575)
    static let travel: CGFloat = 44

    static func position(_ module: HUDModule) -> CGPoint {
        switch module {
        case .system: return CGPoint(x: 417, y: 396)
        case .display: return CGPoint(x: 376, y: 520)
        case .hotkeys: return CGPoint(x: 357, y: 655)
        case .about: return CGPoint(x: 380, y: 784)
        case .notes: return CGPoint(x: 1444, y: 416)
        case .fileShelf: return CGPoint(x: 1550, y: 413)
        case .clipboard: return CGPoint(x: 1485, y: 520)
        case .archive: return CGPoint(x: 1592, y: 515)
        case .mediaAssembly: return CGPoint(x: 1485, y: 621)
        case .minigame: return CGPoint(x: 1593, y: 616)
        case .nowPlaying: return CGPoint(x: 1442, y: 730)
        case .volume: return CGPoint(x: 1552, y: 725)
        case .projection: return CGPoint(x: 1381, y: 837)
        case .reader: return CGPoint(x: 1490, y: 833)
        case .workMode: return CGPoint(x: 1330, y: 945)
        case .calendar: return CGPoint(x: 1440, y: 945)
        case .map: return CGPoint(x: 1290, y: 1050)
        case .eventLog: return CGPoint(x: 1400, y: 1050)
        case .profile: return CGPoint(x: 540, y: 900)
        case .account: return CGPoint(x: 1360, y: 1150)
        case .power: return CGPoint(x: 960, y: 805)
        case .addApp: return CGPoint(x: 1320, y: 1250)
        case .storage: return CGPoint(x: 837, y: 906)
        case .activityMonitor: return CGPoint(x: 1100, y: 900)
        }
    }

    /// normalize(B − A) × 44; the first module of a session starts from the disc centre.
    static func entranceOffset(from previous: HUDModule?, to next: HUDModule) -> CGPoint {
        offset(from: previous.map(position) ?? discCentre, to: position(next))
    }

    static func offset(from a: CGPoint, to b: CGPoint) -> CGPoint {
        let dx = b.x - a.x, dy = b.y - a.y, length = hypot(dx, dy)
        guard length > 0.001 else { return .zero }
        return CGPoint(x: dx / length * travel, y: dy / length * travel)
    }

    /// Inside a module: forward from the right, back from the left, or vertical.
    static func subpageOffset(dx: CGFloat, dy: CGFloat = 0) -> CGPoint {
        offset(from: .zero, to: CGPoint(x: dx, y: dy))
    }
}
