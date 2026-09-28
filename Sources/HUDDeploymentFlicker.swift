import QuartzCore

/// Brief, independently timed signal dropouts during deployment and retraction.
/// Callers supply visual groups, not the screen-sized backdrop. Additive opacity
/// leaves each group's model appearance and existing reveal tracks untouched.
enum HUDDeploymentFlicker {
    static let animationKey = "deployment.signalFlicker"

    struct Sequence: Equatable {
        let keyTimes: [Double]
        let opacityOffsets: [Double]
        let duration: TimeInterval
        let delay: TimeInterval
    }

    /// Seedable for deterministic checks; normal deployments choose a fresh seed.
    /// The requested duration includes each group's small random start offset.
    static func sequence(opening: Bool, duration: TimeInterval? = nil,
                         delay: TimeInterval = 0, seed: UInt64) -> Sequence {
        var random = Generator(state: seed)
        let maximum: TimeInterval = opening ? 0.5 : 0.3
        let fallback: TimeInterval = opening ? 0.42 : 0.23
        let requested = duration ?? fallback
        let total = requested.isFinite ? min(maximum, max(0.08, requested)) : fallback
        let initialDelay = delay.isFinite ? max(0, delay) : 0
        let stagger = random.unit() * total * 0.14
        let additionalPulse = Int(random.unit() * 2)
        let pulseCount = opening ? 2 + (total >= 0.25 ? additionalPulse : 0) : 1 + additionalPulse
        let cell = 0.84 / Double(pulseCount)
        var times = [0.0], offsets = [0.0]

        for pulse in 0..<pulseCount {
            let start = 0.04 + Double(pulse) * cell + random.unit() * cell * 0.12
            let attack = cell * (0.05 + random.unit() * 0.025)
            let hold = cell * (0.45 + random.unit() * 0.10)
            let release = cell * (0.06 + random.unit() * 0.03)
            let depth = opening && pulse > 0 ? 0.23 + random.unit() * 0.38 : 0.42 + random.unit() * 0.36
            times += [start, start + attack, start + attack + hold, start + attack + hold + release]
            offsets += [0, -depth, -depth, 0]
        }
        times.append(1); offsets.append(0)
        return Sequence(keyTimes: times, opacityOffsets: offsets,
                        duration: total - stagger, delay: initialDelay + stagger)
    }

    static func apply(to layers: [CALayer], opening: Bool,
                      duration: TimeInterval? = nil, delay: TimeInterval = 0,
                      reducedMotion: Bool = false, seed: UInt64? = nil,
                      gateVisibility: Bool = false,
                      layerDelay: ((CALayer) -> TimeInterval)? = nil) {
        cancel(on: layers)
        guard !reducedMotion else { return }
        let seed = seed ?? UInt64.random(in: UInt64.min...UInt64.max)
        let now = CACurrentMediaTime()
        // Avoid stacking a dropout on both a card and its labels if a caller
        // includes an ancestor as well as one of that ancestor's descendants.
        let candidates = Set(layers.map(ObjectIdentifier.init))
        var seen = Set<ObjectIdentifier>()
        for (index, layer) in layers.enumerated() {
            guard seen.insert(ObjectIdentifier(layer)).inserted,
                  !layer.isHidden, layer.opacity.isFinite, layer.opacity > 0 else { continue }
            var ancestor = layer.superlayer
            var hasSelectedAncestor = false
            while let parent = ancestor {
                if candidates.contains(ObjectIdentifier(parent)) { hasSelectedAncestor = true; break }
                ancestor = parent.superlayer
            }
            guard !hasSelectedAncestor else { continue }
            let sequence = sequence(opening: opening, duration: duration, delay: delay + (layerDelay?(layer) ?? 0),
                                    seed: seed &+ UInt64(index) &* 0x9E3779B97F4A7C15)
            let baseline = Double(min(1, layer.opacity))
            let flicker = CAKeyframeAnimation(keyPath: "opacity")
            let offsets = gateVisibility ? gatedOffsets(sequence.opacityOffsets, opening: opening) : sequence.opacityOffsets
            flicker.values = offsets.map { $0 * baseline }
            flicker.keyTimes = sequence.keyTimes.map(NSNumber.init(value:))
            flicker.duration = sequence.duration
            flicker.beginTime = layer.convertTime(now, from: nil) + sequence.delay
            flicker.calculationMode = .linear
            flicker.isAdditive = true
            flicker.isCumulative = false
            // Opening's backward fill hides the artwork until its own turn.
            // Closing's forward fill keeps it off after its turn; normal HUD
            // cancellation removes the finite completed track before reuse.
            flicker.fillMode = gateVisibility && !opening ? .both : .backwards
            flicker.isRemovedOnCompletion = !gateVisibility || opening
            layer.add(flicker, forKey: animationKey)
        }
    }

    /// Preserve irregular local dropouts, but send their leading edge through
    /// the scene in screen order. The caller supplies the settled scene before
    /// changing deployment transforms, so folded geometry cannot reorder it.
    static func applySweep(to layers: [CALayer], in coordinateSpace: CALayer,
                           opening: Bool, duration: TimeInterval, delay: TimeInterval,
                           span: TimeInterval, verticalRange: ClosedRange<CGFloat> = 0...640,
                           reducedMotion: Bool = false, seed: UInt64? = nil) {
        apply(to: layers, opening: opening, duration: duration, delay: delay,
              reducedMotion: reducedMotion, seed: seed, gateVisibility: true) { layer in
            let bounds = visualBounds(of: layer)
            let center = layer.convert(CGPoint(x: bounds.midX, y: bounds.midY), to: coordinateSpace)
            return sweepDelay(y: center.y, range: verticalRange, opening: opening, span: span)
        }
    }

    static func gatedOffsets(_ offsets: [Double], opening: Bool) -> [Double] {
        guard !offsets.isEmpty else { return [] }
        var result = offsets
        if opening { result[0] = -1 }
        else { result[result.count - 1] = -1 }
        return result
    }

    static func sweepDelay(y: CGFloat, range: ClosedRange<CGFloat>, opening: Bool,
                           span: TimeInterval) -> TimeInterval {
        guard y.isFinite, range.lowerBound.isFinite, range.upperBound.isFinite,
              range.upperBound > range.lowerBound, span.isFinite else { return 0 }
        let fraction = min(1, max(0, (y - range.lowerBound) / (range.upperBound - range.lowerBound)))
        return Double(opening ? fraction : 1 - fraction) * max(0, span)
    }

    private static func visualBounds(of layer: CALayer) -> CGRect {
        if let shape = layer as? CAShapeLayer, let path = shape.path { return path.boundingBoxOfPath }
        if !layer.bounds.isEmpty { return layer.bounds }
        return (layer.sublayers ?? []).reduce(CGRect.null) { bounds, child in
            let childBounds = visualBounds(of: child)
            return childBounds.isNull ? bounds : bounds.union(child.convert(childBounds, to: layer))
        }.nonNull
    }

    static func cancel(on layers: [CALayer]) {
        for layer in layers { layer.removeAnimation(forKey: animationKey) }
    }

    private struct Generator {
        var state: UInt64
        mutating func unit() -> Double {
            state &+= 0x9E3779B97F4A7C15
            var value = state
            value = (value ^ (value >> 30)) &* 0xBF58476D1CE4E5B9
            value = (value ^ (value >> 27)) &* 0x94D049BB133111EB
            value ^= value >> 31
            return Double(value >> 11) / Double(UInt64(1) << 53)
        }
    }
}

private extension CGRect {
    var nonNull: CGRect { isNull ? .zero : self }
}
