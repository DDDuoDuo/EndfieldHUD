import QuartzCore

enum HUDDeploymentFlickerTests {
    static func run() -> Int {
        var count = 0
        func check(_ result: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !result { fatalError(message, file: file, line: line) }
        }
        let first = HUDDeploymentFlicker.sequence(opening: true, seed: 73)
        check(first == HUDDeploymentFlicker.sequence(opening: true, seed: 73),
              "A supplied seed reproduces the same finite dropout sequence")
        check(first != HUDDeploymentFlicker.sequence(opening: true, seed: 74),
              "Different deployments receive different stagger, pulse widths and dropout levels")
        for opening in [true, false] {
            for seed in 0..<64 {
                let sequence = HUDDeploymentFlicker.sequence(opening: opening, delay: 0.12, seed: UInt64(seed))
                check(sequence.keyTimes.count == sequence.opacityOffsets.count
                      && sequence.keyTimes.first == 0 && sequence.keyTimes.last == 1
                      && zip(sequence.keyTimes, sequence.keyTimes.dropFirst()).allSatisfy { $0 < $1 },
                      "Every generated dropout uses matched values and strictly increasing normalized key times")
                check(sequence.opacityOffsets.first == 0 && sequence.opacityOffsets.last == 0
                      && sequence.opacityOffsets.allSatisfy { (-0.8...0).contains($0) }
                      && sequence.opacityOffsets.contains { $0 < -0.4 },
                      "Signal dropouts dim locally and return to an unchanged baseline without brightness flashes")
                check(sequence.duration > 0 && sequence.delay >= 0.12
                      && sequence.duration + sequence.delay <= 0.12 + (opening ? 0.5 : 0.3),
                      "Random local staggering never outlasts the bounded deployment interval")
            }
        }
        for invalid in [Double.nan, Double.infinity, -Double.infinity] {
            let sequence = HUDDeploymentFlicker.sequence(opening: true, duration: invalid, delay: invalid, seed: 1)
            check(sequence.duration.isFinite && sequence.delay.isFinite && sequence.duration > 0 && sequence.delay >= 0,
                  "Invalid timing input safely falls back to a finite deployment interval")
        }

        let scene = CALayer(); scene.bounds = CGRect(x: 0, y: 0, width: 1000, height: 640)
        let top = CALayer(), middle = CALayer(), bottom = CALayer()
        for (item, y) in [(top, CGFloat(30)), (middle, CGFloat(310)), (bottom, CGFloat(590))] {
            item.frame = CGRect(x: 100, y: y, width: 80, height: 20); scene.addSublayer(item)
        }
        for opening in [true, false] {
            HUDDeploymentFlicker.applySweep(to: [bottom, top, middle], in: scene,
                                           opening: opening, duration: 0.11, delay: 0.03, span: 0.24, seed: 4)
            let times = [top, middle, bottom].map { $0.animation(forKey: HUDDeploymentFlicker.animationKey)!.beginTime }
            check(opening ? times[0] < times[1] && times[1] < times[2] : times[0] > times[1] && times[1] > times[2],
                  "Sweep follows vertical position, opening downwards and closing upwards regardless of input order")
            for layer in [top, middle, bottom] {
                let track = layer.animation(forKey: HUDDeploymentFlicker.animationKey) as! CAKeyframeAnimation
                let values = track.values!.map { ($0 as! NSNumber).doubleValue }
                check(opening ? values.first == -1 && values.last == 0 : values.first == 0 && values.last == -1,
                      "Opening hides before its turn; closing finishes fully hidden")
                check(opening ? track.fillMode == .backwards && track.isRemovedOnCompletion
                              : track.fillMode == .both && !track.isRemovedOnCompletion,
                      "Delayed reveal and completed shutoff retain the correct visibility outside their finite interval")
                check(track.repeatCount == 0 && track.repeatDuration == 0 && layer.opacity == 1,
                      "Directional gating never loops or mutates the reusable model layer")
            }
        }
        HUDDeploymentFlicker.cancel(on: [top, middle, bottom])
        check([top, middle, bottom].allSatisfy { $0.animation(forKey: HUDDeploymentFlicker.animationKey) == nil && $0.opacity == 1 },
              "Cancellation clears retained closing gates before a new deployment")
        HUDDeploymentFlicker.applySweep(to: [top, middle, bottom], in: scene, opening: false,
                                       duration: 0.11, delay: 0, span: 0.24, reducedMotion: true)
        check([top, middle, bottom].allSatisfy { $0.animationKeys() == nil && $0.opacity == 1 },
              "Reduce Motion leaves the final settled scene ungated")
        check(HUDDeploymentFlicker.sweepDelay(y: -.infinity, range: 0...640, opening: true, span: 0.2) == 0,
              "Invalid sweep geometry is safe")

        let card = CALayer(), label = CALayer(), ring = CALayer(), hidden = CALayer()
        card.opacity = 0.6; card.addSublayer(label); hidden.isHidden = true
        let movement = CABasicAnimation(keyPath: "transform.translation.x")
        movement.fromValue = 20; movement.toValue = 0; movement.duration = 0.5
        card.add(movement, forKey: "existing.motion")
        HUDDeploymentFlicker.apply(to: [card, label, ring, card, hidden], opening: true, seed: 5)
        let animation = card.animation(forKey: HUDDeploymentFlicker.animationKey) as! CAKeyframeAnimation
        check(animation.isAdditive && !animation.isCumulative && animation.keyPath == "opacity"
              && animation.repeatCount == 0 && animation.repeatDuration == 0 && animation.isRemovedOnCompletion,
              "Flicker is one removable additive opacity track, without loops or geometry changes")
        check(animation.values!.compactMap { ($0 as? NSNumber)?.doubleValue }.allSatisfy { $0 <= 0 && $0 > -0.6 },
              "Dropout strength respects the supplied group's translucent model opacity")
        check(card.opacity == 0.6 && card.animation(forKey: "existing.motion") != nil,
              "Installing flicker preserves the card's baseline opacity and unrelated movement")
        check(label.animation(forKey: HUDDeploymentFlicker.animationKey) == nil
              && hidden.animation(forKey: HUDDeploymentFlicker.animationKey) == nil,
              "Descendant labels and hidden groups avoid extra multiplied opacity tracks")
        let ringAnimation = ring.animation(forKey: HUDDeploymentFlicker.animationKey) as! CAKeyframeAnimation
        check(animation.keyTimes != ringAnimation.keyTimes || animation.values!.description != ringAnimation.values!.description,
              "Separate visible groups do not pulse in lockstep")
        for seed in 0..<8 { HUDDeploymentFlicker.apply(to: [card, ring], opening: false, seed: UInt64(seed)) }
        check(card.animationKeys()?.count == 2 && ring.animationKeys()?.count == 1,
              "Repeated deployment replaces the previous flicker instead of accumulating tracks")
        HUDDeploymentFlicker.apply(to: [card, ring], opening: true, reducedMotion: true)
        check(card.animation(forKey: HUDDeploymentFlicker.animationKey) == nil
              && ring.animation(forKey: HUDDeploymentFlicker.animationKey) == nil
              && card.animation(forKey: "existing.motion") != nil && card.opacity == 0.6,
              "Reduce Motion removes only flicker and leaves the settled model intact")
        return count
    }
}
