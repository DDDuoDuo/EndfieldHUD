import AppKit
import QuartzCore

enum HUDModuleTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        let glyphs = CATextLayer()
        let artwork = CALayer()
        let scaleCases: [(CGFloat, CGFloat, CGFloat)] = [
            (1, 2, 1), (1.5, 3, 1.5), (2, 3, 2), (2.35, 4, 2.35), (4, 4, 4), (100, 4, 4)
        ]
        for (base, textScale, artworkScale) in scaleCases {
            check(HUDRenderScale.contentScale(for: glyphs, baseScale: base) == textScale,
                  "Perspective text rounds up its 35-percent resolution reserve within the four-times cap")
            check(HUDRenderScale.contentScale(for: artwork, baseScale: base) == artworkScale,
                  "Plain artwork retains the base scale without allocating the glyph-only resolution reserve")
        }
        let invalidScales: [CGFloat] = [.nan, .infinity, -.infinity]
        for invalid in invalidScales {
            check(HUDRenderScale.contentScale(for: glyphs, baseScale: invalid) == 3
                  && HUDRenderScale.contentScale(for: artwork, baseScale: invalid) == 2,
                  "Nonfinite display scales use a finite Retina fallback for text and artwork")
        }
        let priorLanguage = L10n.language
        defer { L10n.language = priorLanguage }
        L10n.language = .english
        check(HUDModule.allCases.count == 24, "The module registry includes the sixteen preserved identities plus Now Playing, Projection, Reader, Archive, Media Assembly, Calendar Closure's Minigame and Account Linking")
        check(Set(HUDModule.allCases.map(\.rawValue)).count == 24, "Every module has a unique stable identity")
        check(Set(HUDModule.allCases.map(\.title)).count == 24, "Every English navigation title is distinct")
        check(HUDModule.allCases.filter { $0.group == .right } == [.notes, .fileShelf, .clipboard, .volume, .workMode, .eventLog, .map, .addApp, .nowPlaying, .account, .projection, .reader, .archive, .mediaAssembly, .calendar, .minigame],
              "The sixteen utility modules stay on the requested right side")
        check(HUDModule.allCases.filter { $0.group == .left } == [.system, .display, .hotkeys, .about],
              "The four settings modules stay on the requested left side")
        check(HUDModule.allCases.filter { $0.group == .bottom } == [.storage, .activityMonitor],
              "Storage and Activity Monitor occupy the bottom")
        check(HUDModule.power.group == .power && HUDModule.profile.group == .power,
              "Battery and identity entry points keep their independent lower-shell navigation group")
        L10n.language = .simplifiedChinese
        check(Set(HUDModule.allCases.map(\.title)).count == 24 && HUDModule.power.title == "电源",
              "All modules also have distinct Chinese titles")
        L10n.language = .english

        var state = HUDModuleSelectionState()
        check(state.selectedModule == .power && !state.isTransitioning && state.pendingModule == nil,
              "The existing Power screen is initially committed")
        check(state.request(.power) == nil && state.generation == 0, "Selecting the current screen is a no-op")
        let first = state.request(.notes)!
        check(first.from == .power && first.to == .notes && state.selectedModule == .power,
              "A transition records its endpoints without prematurely committing")
        check(state.request(.clipboard) == nil && state.pendingModule == .clipboard,
              "A click during animation queues its destination")
        _ = state.request(.storage)
        check(state.pendingModule == .storage && state.requestedModule == .storage,
              "A rapid later click replaces the older pending destination")
        check(state.complete(generation: first.generation - 1) == nil && state.selectedModule == .power,
              "A stale completion cannot change selection")
        let second = state.complete(generation: first.generation)!
        check(second.from == .notes && second.to == .storage && second.generation > first.generation,
              "Only the latest queued destination starts after the first swap")
        _ = state.request(.notes)
        check(state.pendingModule == .notes, "Clicking the committed source during a swap queues a return")
        _ = state.request(.storage)
        check(state.pendingModule == nil, "Choosing the current transition destination clears an obsolete queue")
        check(state.complete(generation: second.generation) == nil && state.selectedModule == .storage && !state.isTransitioning,
              "Final completion commits the requested destination")
        let cancelled = state.request(.about)!
        _ = state.request(.display)
        check(state.cancel() == .storage && state.pendingModule == nil && !state.isTransitioning,
              "Cancellation restores the committed screen and discards the queue")
        _ = state.complete(generation: cancelled.generation)
        check(state.selectedModule == .storage, "A cancelled completion cannot revive a swap")
        _ = state.request(.volume)
        _ = state.request(.power)
        state.settle(on: state.requestedModule)
        check(state.selectedModule == .power && !state.isTransitioning && state.pendingModule == nil,
              "Immediate settling commits the latest request")

        for from in HUDModule.allCases {
            for to in HUDModule.allCases {
                var pair = HUDModuleSelectionState(selectedModule: from)
                if let transition = pair.request(to) {
                    check(from != to && transition.from == from && transition.to == to,
                          "All cross-module transitions retain the correct endpoints")
                    _ = pair.complete(generation: transition.generation)
                } else { check(from == to, "Only identical selection skips a new transition") }
                check(pair.selectedModule == to && !pair.isTransitioning, "Every module pair reaches its requested screen")
            }
        }

        let power = CALayer()
        let batteryValue = CATextLayer()
        batteryValue.string = "REAL_BATTERY_SENTINEL"
        power.addSublayer(batteryValue)
        let content = HUDModuleContent(powerLayer: power, reduceMotion: { false })
        let host = content.layer
        check(host.frame == CGRect(x: 280, y: 100, width: 440, height: 440),
              "The shared center expands to a full circular timer viewport")
        check(HUDModule.workMode.contentFrame == host.frame,
              "Work Mode fills the shared center without another inner panel")
        check(HUDModule.notes.contentFrame == CGRect(x: 300, y: 152, width: 400, height: 334)
              && HUDModule.fileShelf.contentFrame == HUDModule.notes.contentFrame,
              "Existing canvas modules retain saved object coordinates within the larger center")
        check(content.selectedModule == .power && content.contentLayerCount == 1 && content.registeredModuleCount == HUDModule.allCases.count,
              "A persistent host starts with the adopted live Power content")
        check(power.superlayer != nil && power.sublayers?.first === batteryValue,
              "Adopting Power preserves its real-data layers")
        for module in HUDModule.allCases {
            var completed = false
            content.select(module: module, animated: false) { completed = true }
            check(content.selectedModule == module && completed && content.contentLayerCount == 1,
                  "Immediate module selection commits one screen and invokes its completion")
            check(content.activeTransitionAnimationCount == 0 && content.layer === host,
                  "Immediate selection does not animate or replace the center host")
        }
        check(content.register(TestFactory(), for: .about), "A future implementation can register its own screen factory")
        check(!content.register(TestFactory(), for: .power) && content.registeredModuleCount == HUDModule.allCases.count,
              "The live Power screen cannot be accidentally replaced by a placeholder factory")
        content.select(module: .about, animated: false)
        check(content.layer.sublayers?.first?.sublayers?.first?.name == "test.custom.module",
              "Selection uses the explicitly registered replacement factory")
        content.select(module: .notes, animated: false)
        let stableWrapper = content.layer.sublayers?.first
        L10n.language = .simplifiedChinese
        content.update(dark: false, accent: .systemGreen, contentsScale: .nan)
        let labels = stableWrapper?.sublayers?.first?.sublayers?.compactMap { ($0 as? CATextLayer)?.string as? String } ?? []
        check(content.layer === host && content.layer.sublayers?.first === stableWrapper,
              "Theme and language repaint preserves the active host and wrapper")
        check(labels.contains("便笺") && labels.contains("尚未配置"), "Visible placeholders change language immediately")
        check((batteryValue.string as? String) == "REAL_BATTERY_SENTINEL", "Placeholder repaint never fabricates or changes Power data")

        for dark in [true, false] {
            content.update(dark: dark, accent: .systemBlue, contentsScale: 2.35)
            let rendered = stableWrapper?.sublayers?.first
            let textLayers = rendered?.sublayers?.compactMap { $0 as? CATextLayer } ?? []
            check(!textLayers.isEmpty && textLayers.allSatisfy { $0.contentsScale == 4 },
                  "Theme rebuilding preserves the requested sharper scale on visible module text")
            check(rendered?.contentsScale == 2.35 && stableWrapper?.contentsScale == 2.35,
                  "Theme rebuilding keeps the supplied base scale on plain center containers")
        }

        content.select(module: .power, animated: false)
        var obsoleteCallback = 0
        var latestCallback = 0
        content.select(module: .notes, animated: true) { obsoleteCallback += 1 }
        do {
            check(content.isTransitioning && content.contentLayerCount == 2 && content.activeTransitionAnimationCount == 5,
                  "A mechanical swap owns two screens, two transforms, two shutter masks and one finite registration group")
            let wrappers = content.layer.sublayers!
            let outgoing = wrappers[0], incoming = wrappers[1]
            guard let mask = incoming.mask as? CAShapeLayer,
                  let reveal = mask.animation(forKey: "module.shutter") as? CAKeyframeAnimation,
                  let paths = reveal.values as? [CGPath], paths.count == 5,
                  let lock = incoming.animation(forKey: "module.transform") as? CABasicAnimation,
                  let seam = incoming.sublayers?.first(where: { $0.name == "module.registration" }) as? CAShapeLayer,
                  let registration = seam.animation(forKey: "module.registration") as? CAAnimationGroup else {
                fatalError("Incoming modules require a segmented mask, content registration and a fine frontier")
            }
            func topology(_ path: CGPath) -> [CGPathElementType] {
                var result: [CGPathElementType] = []; path.applyWithBlock { result.append($0.pointee.type) }; return result
            }
            check(paths.allSatisfy { topology($0) == topology(paths[0]) }
                  && topology(paths[0]).filter { $0 == .closeSubpath }.count == 6,
                  "All reveal frames morph the same six closed shutters without incompatible path topology")
            let samples = stride(from: 11, through: 429, by: 22).flatMap { x in
                stride(from: 11, through: 429, by: 22).map { y in CGPoint(x: x, y: y) }
            }
            check(samples.allSatisfy { !paths[0].contains($0) && paths.last!.contains($0) },
                  "The mask begins completely closed and ends with complete content coverage")
            check(zip(paths, paths.dropFirst()).allSatisfy { earlier, later in
                samples.allSatisfy { !earlier.contains($0) || later.contains($0) }
            }, "The mechanical reveal never hides content that has already arrived")
            let departure = outgoing.animation(forKey: "module.transform")!
            let retract = outgoing.mask!.animation(forKey: "module.shutter")!
            check(lock.duration == HUDModuleContent.transitionDuration && departure.duration == lock.duration
                  && retract.duration == lock.duration && reveal.duration == lock.duration,
                  "Incoming and outgoing content share one finite handoff duration")
            let transforms = [lock.fromValue, lock.toValue].compactMap { ($0 as? NSValue)?.caTransform3DValue }
            check(transforms.count == 2 && CATransform3DIsIdentity(transforms.last!)
                  && transforms.allSatisfy { $0.m21 == 0 },
                  "Content approaches once and settles at identity without shear corrections")
            check(transforms.allSatisfy { abs($0.m41) <= 33 && abs($0.m42) <= 3 && $0.m43 >= -55 && $0.m43 <= 0 },
                  "Signal registration keeps content displacement and depth within deliberate limits")
            check(seam.fillColor == nil && seam.lineWidth <= 0.7 && seam.opacity == 0
                  && (seam.strokeColor?.alpha ?? 1) <= 0.38,
                  "Registration is a faint unfilled frontier with no opaque colored rectangles")
            check(registration.animations?.count == 2 && registration.duration == HUDModuleContent.transitionDuration
                  && registration.animations?.allSatisfy { $0.duration == registration.duration } == true
                  && registration.repeatCount == 0 && registration.isRemovedOnCompletion,
                  "The only decorative frontier combines finite path and opacity tracks")
            check(incoming.opacity == 1 && outgoing.opacity == 1
                  && (incoming.animationKeys() ?? []).allSatisfy { $0 != "module.opacity" },
                  "Content stays opaque throughout the shutter transition instead of crossfading")
            content.update(dark: true, accent: .systemRed, contentsScale: 2)
            check(incoming.sublayers?.last === seam && seam.animation(forKey: "module.registration") != nil,
                  "An in-flight theme repaint preserves the registration layer and its finite animation")
            content.select(module: .clipboard, animated: true) { obsoleteCallback += 1 }
            content.select(module: .storage, animated: true) { latestCallback += 1 }
            check(content.contentLayerCount == 2, "Rapid clicks do not accumulate hidden screen layers")
            content.settle()
            check(content.selectedModule == .storage && obsoleteCallback == 0 && latestCallback == 1,
                  "Settling keeps the latest request and drops obsolete callbacks")
            check(content.contentLayerCount == 1 && content.activeTransitionAnimationCount == 0,
                  "Settling cleans both layer and mask animations")
            check(content.layer.sublayers?.first?.mask == nil
                  && content.layer.sublayers?.first?.sublayers?.contains { $0.name == "module.registration" } == false,
                  "Settling removes the temporary frontier and restores an unmasked content screen")
            content.select(module: .about, animated: true) { obsoleteCallback += 1 }
            content.select(module: .volume, animated: true) { obsoleteCallback += 1 }
            content.cancel()
            check(content.selectedModule == .storage && content.contentLayerCount == 1 && !content.isTransitioning,
                  "Close cleanup restores one committed screen")
            check(content.activeTransitionAnimationCount == 0 && obsoleteCallback == 0,
                  "Cancelled queued completions and animations do not survive")
        }
        content.cancel()

        for destination in [HUDModule.notes, .system, .storage, .power] {
            content.select(module: destination == .power ? .storage : .power, animated: false)
            content.select(module: destination, animated: true)
            let incoming = content.layer.sublayers!.last!
            let paths = ((incoming.mask?.animation(forKey: "module.shutter") as? CAKeyframeAnimation)?.values as? [CGPath])!
            check(!paths[0].contains(CGPoint(x: 220, y: 220)) && paths.last!.contains(CGPoint(x: 220, y: 220))
                  && paths.last!.boundingBoxOfPath == CGRect(x: 0, y: 0, width: 440, height: 440),
                  "Left, right, upper and lower entry shutters all finish with complete viewport coverage")
            content.cancel()
            check(content.contentLayerCount == 1 && content.activeTransitionAnimationCount == 0,
                  "Every navigation direction cancels without a surviving mask or registration track")
        }
        let reduced = HUDModuleContent(powerLayer: CALayer(), reduceMotion: { true })
        var reducedCompletions = 0
        reduced.select(module: .notes, animated: true) { reducedCompletions += 1 }
        check(!reduced.isTransitioning && reduced.selectedModule == .notes && reduced.contentLayerCount == 1
              && reduced.activeTransitionAnimationCount == 0 && reducedCompletions == 1,
              "Reduce Motion bypasses registration and shutters with one immediate completion")
        check(reduced.layer.sublayers?.first?.mask == nil
              && reduced.layer.sublayers?.first?.sublayers?.contains { $0.name == "module.registration" } == false,
              "Reduced-motion content contains neither a mask nor an invisible decorative frontier")
        content.select(module: .power, animated: false)
        content.select(module: .map, animated: true)
        content.select(module: .notes, animated: true)
        check(content.isPresenting(.power) && content.isPresenting(.map) && !content.isPresenting(.notes),
              "Presentation membership retains visible map artwork while a different destination is only queued")
        content.settle()
        check(content.isPresenting(.notes) && !content.isPresenting(.map),
              "Settling releases outgoing map membership so foreground masks can be removed")
        content.select(module: .power, animated: false)
        var discardedNaturalCompletion = 0, finalNaturalCompletion = 0
        var presented: [(HUDModule, HUDModule, Bool)] = []
        content.onPresentationChange = { presented.append(($0, $1, $2)) }
        content.select(module: .notes, animated: true) { discardedNaturalCompletion += 1 }
        content.select(module: .display, animated: true) { finalNaturalCompletion += 1 }
        check(presented.count == 1 && presented[0].0 == .power && presented[0].1 == .notes && presented[0].2,
              "Workspace artwork starts with the actual incoming module, not a later queued click")
        let finishDeadline = Date().addingTimeInterval(1.5)
        while content.isTransitioning && Date() < finishDeadline { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
        check(!content.isTransitioning && content.selectedModule == .display && discardedNaturalCompletion == 0 && finalNaturalCompletion == 1,
              "Natural animation completion drains the latest queued destination exactly once")
        check(presented.count == 2 && presented[1].0 == .notes && presented[1].1 == .display && presented[1].2,
              "Queued module artwork begins only when that module's own handoff starts")
        check(content.contentLayerCount == 1 && content.activeTransitionAnimationCount == 0
              && content.layer.sublayers?.first?.mask == nil
              && CATransform3DIsIdentity(content.layer.sublayers!.first!.transform)
              && content.layer.sublayers?.first?.sublayers?.contains { $0.name == "module.registration" } == false,
              "The natural final frame leaves one clean, opaque, unmasked screen with no registration layers")
        content.select(module: .notes, animated: true)
        content.cancel()
        check(presented.last!.1 == .display && !presented.last!.2,
              "Cancelling a swap restores external artwork to the last committed module")
        content.select(module: .notes, animated: false)
        check(presented.last!.1 == .notes && !presented.last!.2,
              "Immediate selection settles external artwork without starting an animation")
        return count
    }

    private struct TestFactory: HUDModuleContentFactory {
        func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
            let layer = CALayer()
            layer.name = "test.custom.module"
            return layer
        }
    }
}
