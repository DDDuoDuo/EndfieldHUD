import Foundation
import CoreGraphics
import simd

enum HUDSourceDesktopNavigationLayoutTests {
    static func run() -> Int {
        var assertions = 0
        func check(_ value: Bool, _ message: String) {
            assertions += 1; precondition(value, message)
        }
        do {
            var gesture = HUDSourceDesktopScrollMotion()
            gesture.reset(to: 0.5, at: 0)
            gesture.gesture(by: -0.1, hiddenLength: 1000, at: 0.01, phase: .began, momentum: .none, reduceMotion: false)
            check(gesture.position == 0.4 && gesture.isGestureActive && gesture.requiresFrames,
                  "Precise scrolling follows the finger immediately on the shared display clock")
            _ = gesture.advance(at: 0.5)
            check(gesture.position == 0.4, "A held gesture must not drift toward a spring target")
            gesture.gesture(by: 2, hiddenLength: 1000, at: 0.6, phase: .changed, momentum: .none, reduceMotion: false)
            let rubber = gesture.position
            check(rubber > 1 && (rubber - 1) * 1000 > 200 && (rubber - 1) * 1000 < 240 && !gesture.canScroll(-1),
                  "Precise edge movement uses bounded native-style rubber and disables its limit arrow")
            gesture.gesture(by: 2, hiddenLength: 1000, at: 0.7, phase: .changed, momentum: .none, reduceMotion: false)
            check(gesture.position > rubber && gesture.position - rubber < 0.025, "Rubber resistance increases near its finite limit")
            gesture.gesture(by: 0, hiddenLength: 1000, at: 0.8, phase: .ended, momentum: .none, reduceMotion: false)
            check(!gesture.isGestureActive && gesture.isAnimating, "Finger release starts one finite rebound")
            _ = gesture.advance(at: 2)
            gesture.gesture(by: 0.2, hiddenLength: 1000, at: 2.1, phase: .none, momentum: .began, reduceMotion: false)
            check(gesture.position == 1 && !gesture.isAnimating, "The inertial tail cannot restart an already completed edge rebound")
            gesture.gesture(by: 0, hiddenLength: 1000, at: 2.2, phase: .none, momentum: .ended, reduceMotion: false)
            check(!gesture.acceptsGestureContinuation, "A zero-delta momentum end releases gesture ownership")
            gesture.gesture(by: -0.4, hiddenLength: 1000, at: 3, phase: .began, momentum: .none, reduceMotion: false)
            check(abs(gesture.position - 0.6) < 1e-12, "A fresh direct gesture interrupts the old inertial suppression")
            gesture.gesture(by: -2, hiddenLength: 1000, at: 3.1, phase: .none, momentum: .changed, reduceMotion: false)
            check(gesture.position < 0 && !gesture.isGestureActive && gesture.isAnimating && !gesture.canScroll(1),
                  "Momentum rebounds immediately on crossing the opposite boundary")
            gesture.gesture(by: 0, hiddenLength: 1000, at: 3.2, phase: .cancelled, momentum: .none, reduceMotion: false)
            check(gesture.position == 0 && !gesture.requiresFrames && !gesture.acceptsGestureContinuation,
                  "Cancellation clamps the strip and cancels all finite scrolling")
            gesture.reset(to: 1, at: 4)
            gesture.gesture(by: 2, hiddenLength: 1000, at: 4, phase: .began, momentum: .none, reduceMotion: true)
            check(gesture.position == 1 && !gesture.isAnimating, "Reduce Motion preserves a clamped gesture with no bounce")
            var scroll = HUDSourceDesktopScrollMotion()
            scroll.reset(to: 1, at: 0)
            scroll.scroll(by: -0.3, hiddenLength: 1200, at: 0, reduceMotion: false)
            check(scroll.position == 1 && scroll.isAnimating, "Wheel input starts a finite response without jumping to its target")
            let first = scroll.advance(at: 1.0 / 60)
            check(first < 1 && first > 0.7, "Scroll approaches its target smoothly")
            _ = scroll.advance(at: 2)
            check(scroll.position == 0.7 && !scroll.isAnimating, "Scroll settles exactly and releases its animation clock")
            scroll.reset(to: 1, at: 3)
            scroll.scroll(by: 5, hiddenLength: 20_000, at: 3, reduceMotion: false)
            scroll.scroll(by: 5, hiddenLength: 20_000, at: 3, reduceMotion: false)
            check((scroll.position - 1) * 20_000 > 144 && (scroll.position - 1) * 20_000 <= 180.0001,
                  "Repeated wheel input reaches the enlarged pixel bound even with many shortcuts")
            _ = scroll.advance(at: 5)
            check(scroll.position == 1 && !scroll.isAnimating, "Elastic edge returns to the exact valid endpoint")
            scroll.scroll(by: -0.4, hiddenLength: 1200, at: 6, reduceMotion: true)
            check(scroll.position == 0.6 && !scroll.isAnimating, "Reduce Motion scrolls immediately without a spring")
            let unchanged = scroll.position
            scroll.scroll(by: .nan, hiddenLength: 1200, at: 7, reduceMotion: false)
            check(scroll.position == unchanged, "Invalid wheel input cannot poison presentation state")
            var a = HUDSourceDesktopScrollMotion(), b = HUDSourceDesktopScrollMotion()
            a.reset(to: 1, at: 0); b.reset(to: 1, at: 0)
            a.scroll(by: -0.5, hiddenLength: 1000, at: 0, reduceMotion: false)
            b.scroll(by: -0.5, hiddenLength: 1000, at: 0, reduceMotion: false)
            for frame in 1...10 { _ = a.advance(at: Double(frame) / 60) }
            _ = b.advance(at: 1.0 / 6)
            check(abs(a.position - b.position) < 1e-12, "Spring sampling is independent of dropped display ticks")
            for hiddenLength in [1.5, 200.0, 1000.0, 20_000.0] {
                for endpoint in [0.0, 1.0] {
                    let direction = endpoint == 0 ? -1.0 : 1.0
                    var edge = HUDSourceDesktopScrollMotion()
                    edge.reset(to: endpoint, at: 0)
                    edge.gesture(by: direction * 10_000 / hiddenLength, hiddenLength: hiddenLength,
                        at: 0, phase: .began, momentum: .none, reduceMotion: false)
                    let travel = abs(edge.position - endpoint)
                    check(travel * hiddenLength > 230 && travel * hiddenLength < 240,
                          "Both precise edges have consistent source-unit travel even when a short list barely overflows")
                    edge.gesture(by: 0, hiddenLength: hiddenLength, at: 0.1,
                        phase: .ended, momentum: .none, reduceMotion: false)
                    _ = edge.advance(at: 2)
                    check(edge.position == endpoint && !edge.requiresFrames,
                          "The enlarged edge response settles exactly and stops requesting frames")
                    edge.gesture(by: direction, hiddenLength: hiddenLength, at: 2.1,
                        phase: .none, momentum: .began, reduceMotion: false)
                    check(edge.position == endpoint && !edge.requiresFrames,
                          "Neither edge restarts its enlarged rebound for a late momentum tail")
                    edge.gesture(by: 0, hiddenLength: hiddenLength, at: 2.2,
                        phase: .none, momentum: .ended, reduceMotion: false)
                    check(!edge.acceptsGestureContinuation, "Momentum end releases either edge after the enlarged rebound")
                    edge.gesture(by: direction, hiddenLength: hiddenLength, at: 3,
                        phase: .began, momentum: .none, reduceMotion: true)
                    edge.gesture(by: 0, hiddenLength: hiddenLength, at: 3.1,
                        phase: .ended, momentum: .none, reduceMotion: true)
                    check(edge.position == endpoint && !edge.requiresFrames,
                          "Reduce Motion clamps either enlarged edge and has no remaining frame demand")
                    edge.scroll(by: direction * 10_000 / hiddenLength, hiddenLength: hiddenLength, at: 4, reduceMotion: false)
                    edge.scroll(by: direction * 10_000 / hiddenLength, hiddenLength: hiddenLength, at: 4, reduceMotion: false)
                    let wheelTravel = abs(edge.position - endpoint)
                    check(wheelTravel * hiddenLength > 179 && wheelTravel * hiddenLength <= 180 + 1e-8,
                          "Both wheel edges remain visible on short and long lists within the source-unit cap")
                    _ = edge.advance(at: 6)
                    check(edge.position == endpoint && !edge.requiresFrames,
                          "The enlarged wheel response settles exactly on both boundaries")
                }
            }
            let path = CGMutablePath(); path.addRect(CGRect(x: 25, y: 10, width: 8, height: 20))
            let fitted = HUDSourceDesktopIconLayout.path(path).boundingBoxOfPath
            check(abs(max(fitted.width, fitted.height) - 26) < 1e-10 && abs(fitted.midX - 16) < 1e-10 && abs(fitted.midY - 16) < 1e-10,
                  "Vector icons share centered visible bounds without stretching")
            let document = try HUDSourceWatchDocument(includeWidgets: false)
            let sourceLayout = HUDSourceWatchLayout(document: document)
            let stablePose = try document.animation.pose(entranceTime: document.animation.entrance.lastKeyTime,
                ambientTime: nil, exitTime: nil, canvasResolution: SIMD2(2400, 1350))
            for count in [8, 18, 1000] {
                let navigation = try HUDSourceDesktopNavigationLayout(document: document, entryCount: count)
                for endpoint in [0.0, 1.0] {
                    var restingPose = stablePose
                    let resting = try sourceLayout.apply(to: &restingPose, verticalNormalizedPosition: endpoint,
                        desktopNavigation: navigation)
                    guard let info = resting.scroll, info.contentID == navigation.contentID else {
                        preconditionFailure("The desktop row pool must own the measured source scroll viewport")
                    }
                    check(info.hiddenLength > 1, "Even the shortest desktop list measures its actual viewport inset")
                    let restingNodes = try document.scene.resolve(overrides: restingPose.transforms)
                    let restingY = restingNodes[navigation.contentID]!.localMatrix.columns.3.y
                    for precise in [false, true] {
                        var motion = HUDSourceDesktopScrollMotion()
                        motion.reset(to: endpoint, at: 0)
                        let delta = (endpoint == 0 ? -1.0 : 1.0) * 10_000 / info.hiddenLength
                        if precise {
                            motion.gesture(by: delta, hiddenLength: info.hiddenLength, at: 0,
                                phase: .began, momentum: .none, reduceMotion: false)
                        } else {
                            for _ in 0..<2 {
                                motion.scroll(by: delta, hiddenLength: info.hiddenLength, at: 0, reduceMotion: false)
                            }
                        }
                        var stretchedPose = stablePose
                        let stretched = try sourceLayout.apply(to: &stretchedPose, verticalNormalizedPosition: motion.position,
                            desktopNavigation: navigation)
                        let stretchedNodes = try document.scene.resolve(overrides: stretchedPose.transforms)
                        let visibleTravel = abs(stretchedNodes[navigation.contentID]!.localMatrix.columns.3.y - restingY)
                        let expectedTravel = abs(motion.position - endpoint) * info.hiddenLength
                        check(abs((stretched.scroll?.normalizedPosition ?? .nan) - motion.position) < 1e-9,
                              "Source layout preserves the model's complete edge response instead of silently clamping it")
                        check(abs(visibleTravel - expectedTravel) < 0.01 && visibleTravel > (precise ? 230 : 179),
                              "Both real source edges visibly move by the full bounded travel on short and long lists")
                        check(navigation.sample(normalizedPosition: motion.position).assignments
                            == navigation.sample(normalizedPosition: endpoint).assignments,
                              "Pronounced elastic motion keeps the fixed row pool assigned to the same valid edge entries")
                    }
                }
            }
            let desktop = try HUDSourceWatchDocument(includeWidgets: false, includeSourceText: false, includeDesktopProfile: true)
            let selectable = try HUDSourceSelectableColor(document: desktop)
            let feedback = HUDSourceDesktopHoverFeedback(document: desktop, selectable: selectable)
            let card = desktop.desktopProfileCard!
            check(feedback.sideEdgeIDs.count == desktop.buttons.count && HUDSourceDesktopHoverFeedback.sideEdgeOpacity < 0.25,
                  "Every authored side-button luminous edge is substantially quieter")
            check(feedback.sideEdgeIDs.allSatisfy { desktop.scene.node($0)?.path.hasSuffix("/HoverHint/NaviHint/Img") == true },
                  "Edge attenuation never targets side faces, neutral shadows, or central ambient decoration")
            check(card.buttonIDs.allSatisfy { feedback.groupedButton($0) == card.scene.rootID },
                  "Every profile hit region drives one consistent card highlight")
            check(feedback.profileHighlightNodeID.flatMap { card.scene.node($0) }?.parentID == card.scene.rootID
                && feedback.profileHighlightNodeID.flatMap { card.scene.node($0) }?.transform.rect?.sizeDelta == HUDSourceVector2(412, 158),
                  "The grouped profile fade targets the entire source card plate, not the portrait's smaller light")
            let initialFeedback = feedback.opacities(selectableTints: selectable.colors(at: 0))
            check(initialFeedback.count == 8 && initialFeedback.values.allSatisfy { $0 == 1 },
                  "Profile decoration and quit background preserve their authored idle opacity")
            let changedIDs = Set(initialFeedback.keys)
            let excludedIDs = Set([card.node("playerHead"), card.backgroundNodeID].compactMap { $0 }).union(card.artworkGlowNodeIDs)
            check(changedIDs.isDisjoint(with: excludedIDs), "Hover feedback never changes personal artwork or reactivates its broad additive lights")
            let quit = selectable.bindings.first { desktop.scene.node($0.buttonNodeID)?.name == "QuitBtn" }!
            check(feedback.groupedButton(quit.buttonNodeID) == quit.buttonNodeID,
                  "Quit retains its own independent hit and ColorTint state")
            for id in [card.scene.rootID, quit.buttonNodeID] { selectable.setState(.highlighted, on: id, at: 1) }
            let midFeedback = feedback.opacities(selectableTints: selectable.colors(at: 1.05))
            let hoveredFeedback = feedback.opacities(selectableTints: selectable.colors(at: 1.2))
            check(initialFeedback.keys.allSatisfy { midFeedback[$0]! > initialFeedback[$0]! && midFeedback[$0]! < hoveredFeedback[$0]! },
                  "Both added highlights follow the existing finite fade instead of jumping or adding an independent clock")
            check(!selectable.requiresFrames(at: 1.2), "A stationary hover releases finite frame demand after the authored fade")
            for id in [card.scene.rootID, quit.buttonNodeID] { selectable.setState(.normal, on: id, at: 2) }
            check(feedback.opacities(selectableTints: selectable.colors(at: 2.2)) == initialFeedback,
                  "Pointer exit restores the exact idle decoration and quit background")
            selectable.setState(.highlighted, on: card.scene.rootID, at: 3, reduceMotion: true)
            check(!selectable.requiresFrames(at: 3), "Reduce Motion applies card feedback immediately without animation demand")
            let motion = HUDSourceDesktopAmbientMotion(animation: document.animation, seed: 1234)
            let same = HUDSourceDesktopAmbientMotion(animation: document.animation, seed: 1234)
            let other = HUDSourceDesktopAmbientMotion(animation: document.animation, seed: 5678)
            check(motion.channels.map(\.rate) == same.channels.map(\.rate), "An opening retains deterministic sampled speeds")
            check(motion.channels.map(\.rate) != other.channels.map(\.rate), "Another opening selects different decorative motion")
            check(motion.channels.filter { $0.curve == nil }.count == 6, "Each of the six triangles has independent local motion")
            check(motion.channels.contains { $0.rate < 0 } && motion.channels.contains { $0.rate > 0 }, "Decoration supports both rotation directions")
            let playback = HUDSourceWatchPlayback(animation: document.animation)
            playback.desktopAmbientMotion = motion; playback.showStable(at: 0)
            for time in [0.0, 0.01, 0.3, 2.0, 100_000.0] {
                let full = try playback.sample(at: time, canvasResolution: SIMD2(2400, 1350), reduceMotion: false)!
                let sparse = playback.sampleAmbient(at: time)!
                check(sparse.transforms.count == motion.channels.count && sparse.transforms.allSatisfy {
                    full.transforms[$0.key]?.localRotation == $0.value.localRotation
                },
                      "Sparse and complete clocks use the same per-opening decorative channels")
                check(try sparse.transforms.values.allSatisfy { value in
                    guard let rotation = value.localRotation else { return false }
                    return HUDSourceGeometry.isFinite(try rotation.matrix())
                }, "Decorative rotation remains finite for long-running openings")
            }
            playback.ambientMotionEnabled = false
            check(playback.sampleAmbient(at: 3)?.transforms.isEmpty == true, "Ambient-off stops all randomized decoration")
            let reduced = try playback.sample(at: 3, canvasResolution: SIMD2(2400, 1350), reduceMotion: true)!
            let still = try document.animation.pose(entranceTime: document.animation.entrance.lastKeyTime,
                ambientTime: nil, exitTime: nil, canvasResolution: SIMD2(2400, 1350))
            check(reduced.transforms == still.transforms, "Reduce Motion preserves the stationary authored pose")
            for count in [0, 1, 8, 18, 19, 100, 1000] {
                let layout = try HUDSourceDesktopNavigationLayout(document: document, entryCount: count)
                var reached = Set<Int>()
                var lastHeight = 0.0
                for index in 0...1000 {
                    let normalized = 1 - Double(index) / 1000
                    let sample = layout.sample(normalizedPosition: normalized)
                    reached.formUnion(sample.assignments.values)
                    check(sample.assignments.count <= 18 && Set(sample.assignments.values).count == sample.assignments.count,
                          "Virtual navigation binds each entry once in a bounded source pool")
                    check(sample.assignments.values.allSatisfy { $0 >= 0 && $0 < count },
                          "Virtual navigation never binds an absent shortcut")
                    check(sample.contentHeight >= layout.viewportHeight && (index == 0 || sample.contentHeight == lastHeight),
                          "Scrolling retains a stable, valid logical content extent")
                    lastHeight = sample.contentHeight
                }
                check(reached == Set(0..<count), "Every shortcut and the trailing Add App remains reachable")
                let bottom = layout.sample(normalizedPosition: 0)
                if count > 0 { check(bottom.assignments.values.contains(count - 1), "Bottom of navigation reaches its last entry") }
                var pose = try document.animation.pose(entranceTime: document.animation.entrance.lastKeyTime,
                    ambientTime: nil, exitTime: nil, canvasResolution: SIMD2(2400, 1350))
                layout.apply(to: &pose, normalizedPosition: 0)
                let nodes = try document.scene.resolve(overrides: pose.transforms)
                check(nodes[layout.contentID]?.rect?.size.y == bottom.contentHeight,
                      "The real source content rect uses the complete logical height")
                for row in layout.rows {
                    check(nodes[row.id]?.activeInHierarchy == (bottom.logicalRows[row.id] != nil),
                          "Only assigned physical rows render and receive input")
                }
                for button in document.buttons where button.path.contains("/RightBottomNode/") {
                    guard let caption = button.label else { preconditionFailure("Every recyclable plate needs a caption plane") }
                    check(nodes[caption.nodeID]?.activeInHierarchy == (bottom.assignments[button.nodeID] != nil),
                          "Every assigned slot displays its caption, including the authored inactive BackPack label; unassigned slots remain hidden")
                }
            }
        } catch { fatalError("Desktop source navigation: \(error)") }
        return assertions
    }
}
