import AppKit
import QuartzCore

enum WorkModeTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func near(_ first: Double, _ second: Double) -> Bool { abs(first - second) < 0.00001 }
        let clock = WorkTestClock()
        let controller = WorkModeController(clock: { clock.now }, scheduleTimer: clock.schedule)
        var notifications = 0
        let observer = controller.observe { notifications += 1 }
        defer { controller.removeObserver(observer); controller.shutdown() }
        check(controller.snapshot.kind == .countdown && controller.snapshot.phase == .idle
              && controller.snapshot.duration == 1800 && controller.snapshot.timeText == "30:00"
              && !controller.snapshot.isActive && clock.live.isEmpty,
              "Work Mode starts idle at thirty minutes without any timer")
        check(controller.chooseCountdown(seconds: 300), "A five-minute countdown can be selected")
        let beforeInvalid = controller.snapshot
        for invalid in [0.0, -1, .nan, .infinity, 86_401] {
            check(!controller.chooseCountdown(seconds: invalid) && controller.snapshot == beforeInvalid,
                  "An invalid duration cannot alter the active session configuration")
        }
        controller.start()
        check(controller.snapshot.phase == .running && controller.snapshot.isActive,
              "Starting enters the active Work Mode state")
        check(clock.live.count == 1 && !clock.live[0].repeats && clock.live[0].interval == 300,
              "A hidden countdown owns one completion deadline and no periodic updates")
        let firstCompletion = clock.live[0]
        controller.start()
        check(clock.live.count == 1 && clock.live[0] === firstCompletion,
              "Starting an already running countdown cannot restart its deadline")
        clock.now += 123.75
        check(near(controller.snapshot.elapsed, 123.75) && near(controller.snapshot.remaining, 176.25)
              && controller.snapshot.timeText == "02:57",
              "Countdown time is computed from the continuous clock, independently of timer delivery")
        controller.pause()
        let pausedElapsed = controller.snapshot.elapsed
        check(controller.snapshot.phase == .paused && controller.snapshot.isActive && clock.live.isEmpty,
              "Pausing preserves Work Mode while removing every timer")
        clock.now += 400
        check(controller.snapshot.elapsed == pausedElapsed, "Paused time does not include time spent waiting or asleep")
        controller.resume()
        check(controller.snapshot.phase == .running && clock.live.count == 1
              && near(clock.live[0].interval, 176.25), "Resume schedules only the remaining countdown duration")
        let resumedCompletion = clock.live[0]
        controller.setVisible(true)
        check(clock.live.count == 2 && clock.live.filter(\.repeats).count == 1 && clock.live.contains { $0 === resumedCompletion },
              "Showing the countdown adds one text-update timer without replacing its finish deadline")
        clock.now += 1.2
        clock.fireRepeating()
        check(near(controller.snapshot.elapsed, 124.95) && clock.live.count == 2,
              "Visible ticks update elapsed time without accumulating timers")
        let notificationCount = notifications
        clock.fireRepeating()
        check(notifications == notificationCount, "A duplicate tick inside one display second does not notify observers again")
        controller.setVisible(false)
        check(clock.live.count == 1 && clock.live[0] === resumedCompletion,
              "Hiding Work Mode stops its periodic tick while retaining the one countdown deadline")
        controller.setSuspended(true)
        check(clock.live.isEmpty && controller.isSuspended, "Suspending removes both countdown and visible-update timers")
        clock.now += 500
        controller.setSuspended(false)
        check(controller.snapshot.phase == .completed && controller.snapshot.remaining == 0
              && controller.snapshot.timeText == "00:00" && !controller.snapshot.isActive && clock.live.isEmpty,
              "Wake catches up elapsed sleep time and completes an expired countdown without restarting timers")
        check(notifications > 0, "Session state and visible time are observable")
        controller.reset()
        check(controller.snapshot.phase == .idle && controller.snapshot.elapsed == 0 && controller.snapshot.remaining == 300,
              "Reset returns the configured countdown to its initial duration")

        controller.chooseStopwatch()
        controller.start()
        check(controller.snapshot.kind == .stopwatch && controller.snapshot.phase == .running && clock.live.isEmpty,
              "A hidden stopwatch owns no application-wide one-second timer")
        clock.now += 300
        check(controller.snapshot.timeText == "05:00" && near(controller.snapshot.elapsed, 300),
              "A hidden stopwatch remains accurate without periodic work")
        controller.setVisible(true)
        check(clock.live.count == 1 && clock.live[0].repeats && clock.live[0].interval == 1,
              "A visible stopwatch owns exactly one one-second text-update timer")
        clock.now += 3600.4
        clock.fireRepeating()
        check(controller.snapshot.timeText == "1:05:00" && near(controller.snapshot.elapsed, 3900.4),
              "Stopwatch display expands to hours while retaining fractional timing accuracy")
        controller.pause()
        let beforeWait = controller.snapshot
        clock.now += 90
        check(controller.snapshot == beforeWait && clock.live.isEmpty, "Pausing the stopwatch freezes its value and removes its tick")
        controller.resume()
        clock.now += 2.1
        controller.stop()
        check(controller.snapshot.phase == .stopped && !controller.snapshot.isActive
              && near(controller.snapshot.elapsed, 3902.5) && clock.live.isEmpty,
              "Stop exits active Work Mode and preserves a stopped elapsed-time summary")
        controller.start()
        check(controller.snapshot.elapsed == 0 && controller.snapshot.phase == .running,
              "Starting a stopped session begins a fresh measurement")
        firstCompletion.fireEvenIfInvalidated()
        check(controller.snapshot.kind == .stopwatch && controller.snapshot.phase == .running,
              "A stale callback from a replaced countdown cannot complete the new stopwatch")
        controller.reset()
        check(controller.snapshot.phase == .idle && !controller.snapshot.isActive && clock.live.isEmpty,
              "Reset clears an active stopwatch and removes all timer work")
        controller.chooseCountdown(seconds: 86_400)
        check(controller.snapshot.timeText == "24:00:00", "The supported maximum duration displays a full twenty-four hours")

        let deadlineClock = WorkTestClock()
        let deadline = WorkModeController(clock: { deadlineClock.now }, scheduleTimer: deadlineClock.schedule)
        deadline.chooseCountdown(seconds: 1)
        deadline.start()
        let originalDeadline = deadlineClock.live[0]
        deadlineClock.now += 0.75
        originalDeadline.fire()
        check(deadline.snapshot.phase == .running && deadlineClock.live.count == 1
              && near(deadlineClock.live[0].interval, 0.25),
              "An early timer firing reschedules the remainder instead of finishing early")
        deadlineClock.now += 0.3
        deadlineClock.live[0].fire()
        check(deadline.snapshot.phase == .completed && deadlineClock.live.isEmpty,
              "A hidden countdown completes once its continuous-clock deadline is actually reached")
        deadline.shutdown()

        let valid: [(String, Double)] = [("0:01", 1), ("1", 60), ("1.5", 90), ("1,5", 90),
                                       (" 25 ", 1500), ("1:02", 62), ("1440:00", 86_400)]
        for (text, seconds) in valid { check(WorkModeDuration.parse(text) == seconds, "Custom durations accept validated minutes or min:sec") }
        for text in ["", "0", "-1", "0:00", "0.01", "1:60", "-1:30", "2.5:00", "1:2.2", "1:2:3", "1440:01", "nan", "inf", "tomorrow"] {
            check(WorkModeDuration.parse(text) == nil, "Malformed or out-of-range custom input cannot start a timer")
        }
        check(WorkModeDuration.editText(1) == "0:01" && WorkModeDuration.editText(86_400) == "1440:00",
              "The inline editor retains precise seconds at both allowed boundaries")

        let sceneClock = WorkTestClock()
        let sceneController = WorkModeController(clock: { sceneClock.now }, scheduleTimer: sceneClock.schedule)
        let canvas = WorkModeCanvas(controller: sceneController)
        let style = HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2)
        let scene = canvas.makeContent(for: .workMode, style: style)
        var editRequests = 0
        canvas.onEditDuration = { _ in editRequests += 1 }
        check(canvas.accessibleActions.filter { $0.id.hasPrefix("work:preset:") }.map(\.id)
              == ["work:preset:5", "work:preset:30", "work:preset:60"],
              "The countdown exposes exactly five-, thirty-, and sixty-minute presets")
        check(scene.bounds == CGRect(x: 0, y: 0, width: 440, height: 440),
              "Work Mode fills the shared central circular viewport")
        check(canvas.accessibleActions.allSatisfy { action in
            let rect = action.rect
            let corners = [CGPoint(x: rect.minX, y: rect.minY), CGPoint(x: rect.maxX, y: rect.minY),
                           CGPoint(x: rect.minX, y: rect.maxY), CGPoint(x: rect.maxX, y: rect.maxY)]
            return corners.allSatisfy { hypot($0.x - 220, $0.y - 220) < 215 }
        }, "Every timer control remains inside the full-size dial")
        func layers(_ layer: CALayer) -> [CALayer] { [layer] + (layer.sublayers ?? []).flatMap(layers) }
        let chargeBadgeReservedArea = HUDChargeBadge.compactHitRect.offsetBy(dx: -280, dy: -100)
        check(canvas.accessibleActions.allSatisfy { !$0.rect.intersects(chargeBadgeReservedArea) },
              "Idle timer controls leave the shared bottom-center charge badge unobstructed")
        check(canvas.durationEditorRect == CGRect(x: 64, y: 184, width: 312, height: 68),
              "Custom duration editing retains the original idle clock position")
        guard let dial = layers(scene).first(where: { $0.name == "workMode.ring" }) as? CAShapeLayer,
              let path = dial.path else { fatalError("The retained Work Mode dial must contain its explicit arc path") }
        check(path.boundingBoxOfPath == CGRect(x: 5, y: 5, width: 430, height: 430),
              "The countdown ring spans the main inner HUD circle")
        var endpoints: [CGPoint] = []
        path.applyWithBlock { pointer in
            let element = pointer.pointee
            switch element.type {
            case .moveToPoint, .addLineToPoint: endpoints.append(element.points[0])
            case .addQuadCurveToPoint: endpoints.append(element.points[1])
            case .addCurveToPoint: endpoints.append(element.points[2])
            case .closeSubpath: break
            @unknown default: break
            }
        }
        check(endpoints.count == 5, "The dial path has one start and four explicit quarter-circle arcs")
        let clockwisePoints = [CGPoint(x: 220, y: 5), CGPoint(x: 435, y: 220), CGPoint(x: 220, y: 435),
                               CGPoint(x: 5, y: 220), CGPoint(x: 220, y: 5)]
        check(zip(endpoints, clockwisePoints).allSatisfy { hypot($0.x - $1.x, $0.y - $1.y) < 0.001 },
              "The actual path advances top→right→bottom→left in flipped coordinates, so a decreasing endpoint travels counter-clockwise")
        check(dial.strokeEnd == 1 && CATransform3DIsIdentity(dial.transform),
              "An idle countdown begins as one full ring with its endpoint at twelve o'clock")
        canvas.perform(actionID: "work:custom")
        check(editRequests == 1, "Custom duration requests inline native input rather than a separate editor window")
        canvas.perform(actionID: "work:focus")
        check(!canvas.accessibleActions.contains { $0.id == "work:focus" } && sceneController.snapshot.phase == .idle
              && !layers(scene).contains { $0.name == "workMode.focusHelp" },
              "The removed manual Focus action and explanatory text are absent")
        let beforeError = sceneController.snapshot
        let statusBeforeError = canvas.accessibilityStatus
        check(!canvas.setCustomDuration("0:00") && sceneController.snapshot.duration == 1800,
              "Invalid inline duration input preserves the previous configuration")
        check(canvas.accessibilityStatus != statusBeforeError, "Invalid custom input shows a validation message")
        canvas.cancelCustomEditing()
        check(canvas.accessibilityStatus == statusBeforeError && sceneController.snapshot == beforeError,
              "Cancelling invalid custom input clears the message without mutating the session")
        check(canvas.setCustomDuration("0:03") && sceneController.snapshot.duration == 3,
              "Valid inline duration input updates the configured countdown")
        canvas.activate()
        canvas.perform(actionID: "work:start")
        check(sceneController.snapshot.phase == .running && canvas.accessibleActions.contains { $0.id == "work:pause" },
              "Start morphs the same retained canvas to its running controls")
        check(canvas.accessibleActions.map(\.id) == ["work:pause", "work:reset"],
              "Running sessions expose no hidden mode, preset, custom, or manual Focus controls")
        guard let configuration = layers(scene).first(where: { $0.name == "workMode.configuration" }),
              let clockViewport = layers(scene).first(where: { $0.name == "workMode.clockViewport" }),
              let phaseLabel = layers(scene).first(where: { $0.name == "workMode.phase" }) as? CATextLayer else {
            fatalError("Work Mode needs stable configuration and clock layout layers")
        }
        check(canvas.accessibleActions.allSatisfy { !$0.rect.intersects(chargeBadgeReservedArea) },
              "Running timer controls also remain above the persistent charge badge")
        check(configuration.opacity == 0 && clockViewport.position.y == 166 && near(Double(clockViewport.transform.m11), 1.28),
              "Start expands the timer upward into the faded configuration area")
        check(phaseLabel.string as? String == "RUNNING", "Running status uses the requested literal English label")
        let reduceMotion = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        let runningKeys = layers(scene).flatMap { $0.animationKeys() ?? [] }
        check(runningKeys.filter { $0.hasPrefix("workMode.") }.count == (reduceMotion ? 0 : 1),
              "A visible running dial owns at most one continuous Core Animation track")
        if !reduceMotion {
            guard let countdownAnimation = dial.animation(forKey: "workMode.countdown") as? CABasicAnimation else {
                fatalError("A running countdown needs its remaining-arc animation")
            }
            check(countdownAnimation.keyPath == "strokeEnd"
                  && (countdownAnimation.fromValue as? NSNumber)?.doubleValue == 1
                  && (countdownAnimation.toValue as? NSNumber)?.doubleValue == 0
                  && countdownAnimation.duration == 3,
                  "The timer removes its remaining arc from full to empty, reversing the actual path counter-clockwise")
            let feedback = layers(scene).flatMap { item in
                (item.animationKeys() ?? []).filter { $0.hasPrefix("workFeedback.") }.compactMap { item.animation(forKey: $0) }
            }
            check(!feedback.isEmpty && feedback.allSatisfy {
                $0.duration <= 0.22 && ($0 as? CAPropertyAnimation)?.keyPath == "transform"
            }, "Action feedback is a bounded mechanical transform, never an opacity fade")
            check(feedback.allSatisfy { $0 is CABasicAnimation && !($0 is CASpringAnimation) },
                  "Work Mode buttons and clock settle monotonically without keyframe bounce or springs")
            let expandingButton = layers(scene).compactMap { $0.animation(forKey: "workFeedback.expand") as? CABasicAnimation }.first
            check(expandingButton != nil && (expandingButton?.fromValue as? NSValue)?.caTransform3DValue.m11 == 0.985
                  && (expandingButton?.toValue as? NSValue).map { CATransform3DIsIdentity($0.caTransform3DValue) } == true,
                  "Timer buttons expand once to their normal size without pressing down or overshooting")
        } else {
            check(!runningKeys.contains { $0.hasPrefix("workFeedback.") }, "Reduce Motion omits mechanical button feedback")
        }
        let currentDeadline = sceneClock.live.first { !$0.repeats }
        check(canvas.setCustomDuration("0:03") && sceneController.snapshot.phase == .running
              && sceneClock.live.first(where: { !$0.repeats }) === currentDeadline,
              "Committing an unchanged custom duration preserves the active countdown and deadline")
        canvas.perform(actionID: "work:countdown")
        check(sceneController.snapshot.phase == .running,
              "Clicking the already selected timer kind cannot reset a running session")
        let protectedRunning = sceneController.snapshot
        canvas.perform(actionID: "work:stopwatch")
        canvas.perform(actionID: "work:preset:60")
        check(sceneController.snapshot == protectedRunning && !canvas.setCustomDuration("0:10"),
              "Hidden controls and a stale custom editor cannot replace a running session")
        let identities = layers(scene).map(ObjectIdentifier.init)
        sceneClock.now += 1.5
        sceneClock.fireRepeating()
        check(layers(scene).map(ObjectIdentifier.init) == identities,
              "A visible one-second update retains every layer instead of rebuilding the scene")
        canvas.perform(actionID: "work:pause")
        check(dial.strokeEnd == 0.5 && dial.animation(forKey: "workMode.countdown") == nil,
              "Pausing freezes the actual remaining half-circle instead of changing it to elapsed progress")
        check(configuration.opacity == 0 && clockViewport.position.y == 166 && phaseLabel.string as? String == "PAUSED"
              && canvas.accessibleActions.map(\.id) == ["work:resume", "work:reset"],
              "Pausing preserves the expanded timer and hides configuration controls")
        canvas.perform(actionID: "work:resume")
        if !reduceMotion {
            check((dial.animation(forKey: "workMode.countdown") as? CABasicAnimation)?.fromValue as? Double == 0.5,
                  "Resume continues removing the remaining arc from its preserved endpoint")
        }
        canvas.deactivate()
        check(layers(scene).allSatisfy { ($0.animationKeys() ?? []).allSatisfy { !$0.hasPrefix("workMode.") && !$0.hasPrefix("workFeedback.") && !$0.hasPrefix("workLayout.") } }
              && sceneClock.live.filter(\.repeats).isEmpty,
              "Hiding the module removes its ring animations and text tick before a section transition")
        sceneClock.now += 5
        sceneClock.live.first?.fire()
        check(sceneController.snapshot.phase == .completed && canvas.layer === scene,
              "A hidden completed session keeps its controller and retained canvas identity")
        check(dial.strokeEnd == 0.5, "A hidden canvas performs no drawing work when its session completes")
        canvas.activate()
        check(dial.strokeEnd == 0, "Reopening a completed countdown displays an empty remaining ring")
        check(configuration.opacity == 1 && clockViewport.position.y == 218.5 && CATransform3DIsIdentity(clockViewport.transform)
              && canvas.accessibleActions.contains { $0.id == "work:countdown" },
              "A completed session restores the normal timer layout and configuration")
        canvas.perform(actionID: "work:stopwatch")
        check(dial.strokeEnd == 0.045 && !canvas.accessibleActions.contains { $0.id.hasPrefix("work:preset:") },
              "Stopwatch mode retains a subtle marker and removes countdown-only controls")
        canvas.perform(actionID: "work:start")
        check((dial.animation(forKey: "workMode.stopwatch") != nil) == !reduceMotion
              && dial.animation(forKey: "workMode.countdown") == nil,
              "A running stopwatch replaces the countdown track with its single marker rotation")
        canvas.deactivate()
        check(layers(scene).allSatisfy { ($0.animationKeys() ?? []).isEmpty } && sceneClock.live.isEmpty,
              "A hidden stopwatch has neither animation tracks nor timers")
        let previousLanguage = L10n.language
        L10n.language = .simplifiedChinese
        _ = canvas.makeContent(for: .workMode, style: HUDModuleContentStyle(dark: false, accent: .systemYellow, contentsScale: 2))
        check(!canvas.accessibleActions.contains { $0.id == "work:focus" }
              && phaseLabel.string as? String == "RUNNING" && canvas.accessibilityStatus.contains("RUNNING"),
              "Chinese UI omits manual Focus while retaining the English running status")
        L10n.language = previousLanguage
        sceneController.shutdown()

        let resetClock = WorkTestClock()
        let resetController = WorkModeController(clock: { resetClock.now }, scheduleTimer: resetClock.schedule)
        _ = resetController.chooseCountdown(seconds: 120)
        let resetCanvas = WorkModeCanvas(controller: resetController)
        let resetScene = resetCanvas.makeContent(for: .workMode, style: HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2))
        func sessionActions() -> [String] {
            resetCanvas.accessibleActions.filter {
                ["work:start", "work:pause", "work:resume", "work:stop", "work:reset"].contains($0.id)
            }.map(\.id)
        }
        resetCanvas.activate()
        resetCanvas.perform(actionID: "work:start")
        resetClock.now += 12
        resetClock.fireRepeating()
        check(sessionActions() == ["work:pause", "work:reset"] && resetController.snapshot.elapsed == 12,
              "A running timer presents only Pause and Reset as session actions")
        resetCanvas.perform(actionID: "work:reset")
        check(resetController.snapshot.phase == .idle && resetController.snapshot.elapsed == 0
              && resetController.snapshot.remaining == 120 && resetClock.live.isEmpty,
              "Reset stops a running countdown, restores its configured duration and cancels all timers")
        check(layers(resetScene).allSatisfy { ($0.animationKeys() ?? []).allSatisfy { !$0.hasPrefix("workMode.") } },
              "Reset removes the running countdown animation from the retained canvas")
        resetCanvas.perform(actionID: "work:start")
        resetClock.now += 30
        resetCanvas.perform(actionID: "work:pause")
        check(sessionActions() == ["work:resume", "work:reset"] && resetController.snapshot.elapsed == 30,
              "A paused timer presents only Resume and Reset while retaining its elapsed time")
        let pausedBeforeStaleAction = resetController.snapshot
        resetCanvas.perform(actionID: "work:stop")
        check(resetController.snapshot == pausedBeforeStaleAction,
              "A stale native Stop action cannot mutate a session after Stop was removed")
        resetCanvas.perform(actionID: "work:reset")
        check(resetController.snapshot.phase == .idle && resetController.snapshot.elapsed == 0
              && resetController.snapshot.remaining == 120 && resetClock.live.isEmpty,
              "Reset also discards paused countdown progress and returns to the configured starting time")
        resetCanvas.deactivate(); resetCanvas.activate()
        resetCanvas.perform(actionID: "work:stopwatch")
        resetCanvas.perform(actionID: "work:start")
        resetClock.now += 17
        resetClock.fireRepeating()
        resetCanvas.perform(actionID: "work:reset")
        check(resetController.snapshot.kind == .stopwatch && resetController.snapshot.phase == .idle
              && resetController.snapshot.elapsed == 0 && resetClock.live.isEmpty,
              "Reset stops the running stopwatch and clears its elapsed time without changing modes")
        check(layers(resetScene).allSatisfy { ($0.animationKeys() ?? []).allSatisfy { !$0.hasPrefix("workMode.") } },
              "The reset stopwatch leaves no running marker animation")
        L10n.language = .simplifiedChinese
        resetCanvas.perform(actionID: "work:start")
        check(resetCanvas.accessibleActions.contains { $0.id == "work:reset" && $0.label == "重置" }
              && !resetCanvas.accessibleActions.contains { $0.id == "work:stop" || $0.label == "结束" },
              "The Chinese running controls use Reset and omit the removed End action")
        L10n.language = previousLanguage
        resetCanvas.deactivate()
        resetController.shutdown()

        let layoutClock = WorkTestClock()
        let layoutController = WorkModeController(clock: { layoutClock.now }, scheduleTimer: layoutClock.schedule)
        var preferReducedMotion = false
        let layoutCanvas = WorkModeCanvas(controller: layoutController, reduceMotion: { preferReducedMotion })
        let layoutScene = layoutCanvas.makeContent(for: .workMode, style: style)
        layoutCanvas.activate()
        let layoutConfiguration = layers(layoutScene).first { $0.name == "workMode.configuration" }!
        let layoutViewport = layers(layoutScene).first { $0.name == "workMode.clockViewport" }!
        layoutCanvas.perform(actionID: "work:start")
        let layoutAnimations = layers(layoutScene).flatMap { item in
            (item.animationKeys() ?? []).filter { $0.hasPrefix("workLayout.") }.compactMap { item.animation(forKey: $0) }
        }
        check(layoutAnimations.count == 4 && layoutAnimations.allSatisfy {
            $0.duration == WorkModeCanvas.layoutTransitionDuration && $0.repeatCount == 0 && $0.isRemovedOnCompletion
        }, "Expansion uses four finite, automatically removed layout tracks")
        let fade = layoutConfiguration.animation(forKey: "workLayout.opacity") as? CABasicAnimation
        check((fade?.fromValue as? NSNumber)?.floatValue == 1 && (fade?.toValue as? NSNumber)?.floatValue == 0,
              "Starting fades the existing mode and preset group gradually to transparent")
        layoutCanvas.perform(actionID: "work:pause")
        layoutCanvas.perform(actionID: "work:reset")
        check(layoutConfiguration.opacity == 1 && layoutViewport.position.y == 218.5
              && !layoutCanvas.accessibleActions.contains { $0.id == "work:custom" },
              "Reset reverses the layout while keeping still-fading-in configuration inaccessible")
        layoutCanvas.perform(actionID: "work:start")
        check(layoutViewport.position.y == 166 && layoutConfiguration.opacity == 0,
              "Starting during retraction retargets the same retained layers")
        RunLoop.main.run(until: Date().addingTimeInterval(WorkModeCanvas.layoutTransitionDuration + 0.08))
        check(layoutCanvas.accessibleActions.map(\.id) == ["work:pause", "work:reset"],
              "A stale reset completion cannot expose configuration over a restarted timer")
        let layoutRing = layers(layoutScene).first { $0.name == "workMode.ring" }!
        let existingDuration = layoutRing.animation(forKey: "workMode.countdown")?.duration
        let layoutRevision = layoutController.revision
        layoutClock.now += 2
        layoutCanvas.setFocusStatusMessage("Focus automation is unavailable")
        let focusStatus = layers(layoutScene).first { $0.name == "workMode.focusStatus" }!
        check(!focusStatus.isHidden && layoutCanvas.accessibilityStatus.contains("Focus automation is unavailable")
              && layoutController.revision == layoutRevision && layoutRing.animation(forKey: "workMode.countdown")?.duration == existingDuration,
              "A real Focus error updates only its status and accessibility help without retiming the clock")
        let headingFrame = layers(layoutScene).first { $0.name == "workMode.heading" }!.frame
        check(focusStatus.frame.maxY < headingFrame.minY
              && layoutCanvas.accessibleActions.allSatisfy { !$0.rect.intersects(focusStatus.frame) },
              "Focus errors stay above the heading and cannot obscure session actions")
        var permissionRequests = 0
        layoutCanvas.onRequestFocusAccess = { permissionRequests += 1 }
        layoutCanvas.setFocusStatusMessage("Allow Accessibility to control Focus.", needsAccessibilityPermission: true)
        check(layoutCanvas.accessibleActions.contains { $0.id == "work:focusAccess" && $0.rect == focusStatus.frame },
              "Only a permission-required status exposes a projected Accessibility settings action")
        layoutCanvas.perform(actionID: "work:focusAccess")
        check(permissionRequests == 1 && layoutController.revision == layoutRevision
              && layoutRing.animation(forKey: "workMode.countdown")?.duration == existingDuration,
              "The explicit permission action never toggles Focus or retimes the active countdown")
        layoutCanvas.setFocusStatusMessage(nil)
        check(focusStatus.isHidden && !layoutCanvas.accessibleActions.contains { $0.id == "work:focus" },
              "Normal operation keeps the Focus status empty and adds no manual control")
        layoutCanvas.perform(actionID: "work:focusAccess")
        check(permissionRequests == 1 && !layoutCanvas.accessibleActions.contains { $0.id == "work:focusAccess" },
              "A stale or hidden permission action cannot reopen System Settings")
        layoutCanvas.perform(actionID: "work:reset")
        RunLoop.main.run(until: Date().addingTimeInterval(WorkModeCanvas.layoutTransitionDuration + 0.08))
        check(layoutCanvas.accessibleActions.contains { $0.id == "work:custom" } && layoutConfiguration.opacity == 1,
              "Finishing a reset fade restores top controls for clicks and accessibility")
        layoutCanvas.perform(actionID: "work:start")
        preferReducedMotion = true
        layoutCanvas.refreshMotionPreference()
        check(layers(layoutScene).allSatisfy { ($0.animationKeys() ?? []).isEmpty }
              && layoutViewport.position.y == 166 && layoutConfiguration.opacity == 0,
              "Enabling Reduce Motion settles expansion instantly and removes all motion")
        layoutCanvas.perform(actionID: "work:reset")
        check(layoutViewport.position.y == 218.5 && layoutConfiguration.opacity == 1
              && layoutCanvas.accessibleActions.contains { $0.id == "work:custom" },
              "Reduce Motion restores configuration immediately on reset")
        layoutCanvas.perform(actionID: "work:start")
        layoutCanvas.perform(actionID: "work:pause")
        L10n.language = .simplifiedChinese
        _ = layoutCanvas.makeContent(for: .workMode, style: style)
        check(layoutCanvas.accessibilityStatus.contains("PAUSED"), "Paused status stays English in Chinese localization")
        L10n.language = previousLanguage
        let nativeHost = NSView(frame: WorkModeDialGeometry.canvas)
        let nativeInteraction = HUDWorkModeInteraction(canvas: layoutCanvas, host: nativeHost)
        nativeInteraction.setActive(true)
        check(nativeHost.subviews.compactMap { $0 as? NSButton }.filter { !$0.isHidden }.count == 2,
              "A paused session exposes only Resume and Reset as native accessibility buttons")
        layoutCanvas.perform(actionID: "work:reset")
        check(nativeHost.subviews.compactMap { $0 as? NSButton }.filter { !$0.isHidden }.count == 8,
              "An instant reduced-motion reset restores the six configuration controls")
        layoutCanvas.perform(actionID: "work:start")
        check(nativeHost.subviews.compactMap { $0 as? NSButton }.filter { !$0.isHidden }.count == 2,
              "Starting immediately removes hidden configuration buttons from the accessibility tree")
        nativeInteraction.deactivate()
        check(nativeHost.subviews.allSatisfy(\.isHidden), "Closing the interaction hides every native control")
        layoutCanvas.deactivate(); layoutController.shutdown()

        let lifetimeClock = WorkTestClock()
        let lifetime = WorkModeController(clock: { lifetimeClock.now }, scheduleTimer: lifetimeClock.schedule)
        check(lifetime.restoreTrackedWorkSeconds(3600) && !lifetime.restoreTrackedWorkSeconds(7200),
              "Lifetime Work Mode accounting is seeded from persistence once without replacing an established baseline")
        var checkpoints: [TimeInterval] = []
        var observedTotalsAgree = true
        lifetime.onTrackedWorkSecondsChanged = { total in
            checkpoints.append(total)
            observedTotalsAgree = observedTotalsAgree && near(lifetime.trackedWorkSeconds, total)
        }
        lifetime.chooseStopwatch(); lifetime.start()
        check(lifetimeClock.live.isEmpty, "Lifetime tracking adds no hidden stopwatch polling or persistence timer")
        let beforeRunning = checkpoints.count
        lifetimeClock.now += 30.25
        check(near(lifetime.trackedWorkSeconds, 3630.25) && checkpoints.count == beforeRunning,
              "The live lifetime total includes active time without requiring a disk checkpoint")
        lifetime.pause()
        check(near(checkpoints.last ?? 0, 3630.25), "Pausing checkpoints the absolute active lifetime total")
        lifetimeClock.now += 600
        check(near(lifetime.trackedWorkSeconds, 3630.25), "Paused time never contributes to the personal card's work hours")
        lifetime.resume(); lifetimeClock.now += 19.75; lifetime.reset()
        check(near(lifetime.trackedWorkSeconds, 3650) && near(checkpoints.last ?? 0, 3650),
              "Reset records the resumed running interval without counting the preceding pause or losing partial work")
        lifetime.chooseCountdown(seconds: 5); lifetime.start(); lifetimeClock.now += 10; lifetime.refresh()
        check(lifetime.snapshot.phase == .completed && near(lifetime.trackedWorkSeconds, 3655),
              "Delayed countdown completion credits its configured duration only, excluding time after the deadline")
        lifetime.reset(); lifetime.reset(); lifetime.chooseStopwatch()
        check(near(lifetime.trackedWorkSeconds, 3655), "Repeated reset and timer-kind changes cannot count a completed session twice")
        lifetime.start(); lifetimeClock.now += 12; lifetime.chooseCountdown(seconds: 60)
        check(near(lifetime.trackedWorkSeconds, 3667) && lifetime.snapshot.phase == .idle,
              "Replacing an active timer retains the work performed before its kind or duration changed")
        lifetime.start(); lifetime.setVisible(true)
        let beforeVisibleTick = checkpoints.count
        lifetimeClock.now += 4; lifetimeClock.fireRepeating()
        check(checkpoints.count == beforeVisibleTick && near(lifetime.trackedWorkSeconds, 3671),
              "One-second text updates calculate totals but never cause per-second persistence writes")
        lifetime.setVisible(false)
        check(near(checkpoints.last ?? 0, 3671), "Hiding the module checkpoints running time using the existing visibility event")
        lifetime.setSuspended(true); lifetimeClock.now += 100; lifetime.setSuspended(false)
        check(lifetime.snapshot.phase == .completed && near(lifetime.trackedWorkSeconds, 3727),
              "A countdown spanning system sleep credits running time through its deadline with no sleep-time polling")
        lifetime.chooseStopwatch(); lifetime.start(); lifetimeClock.now += 3
        var persistedLifetime = 3727.0
        var failNextCheckpoint = true
        lifetime.onTrackedWorkSecondsChanged = { total in
            if failNextCheckpoint { failNextCheckpoint = false; return }
            persistedLifetime = total
        }
        lifetime.stop()
        check(near(lifetime.trackedWorkSeconds, 3730) && persistedLifetime == 3727,
              "A failed persistence sink leaves the entire absolute lifetime total available in memory")
        lifetime.reset()
        check(persistedLifetime == 3730, "An idle checkpoint retries the same absolute total without adding it twice")
        lifetime.start(); lifetimeClock.now += 2; lifetime.shutdown()
        lifetimeClock.now += 100; lifetime.shutdown()
        check(persistedLifetime == 3732 && lifetime.trackedWorkSeconds == 3732 && lifetimeClock.live.isEmpty,
              "Shutdown flushes the final interval once and no longer accrues time after stopping the controller")
        check(observedTotalsAgree, "Synchronous persistence observers see the updated lifetime total without transient double counting")
        for invalidSeed in [-1.0, Double.nan, .infinity] {
            let invalid = WorkModeController(clock: { lifetimeClock.now }, scheduleTimer: lifetimeClock.schedule)
            check(!invalid.restoreTrackedWorkSeconds(invalidSeed) && invalid.trackedWorkSeconds == 0,
                  "Invalid persisted lifetime values cannot contaminate the continuous-clock accounting")
            invalid.shutdown()
        }

        let ownershipClock = WorkTestClock()
        var temporary: WorkModeController? = WorkModeController(clock: { ownershipClock.now }, scheduleTimer: ownershipClock.schedule)
        temporary?.start()
        weak var releasedController = temporary
        temporary = nil
        check(releasedController == nil && ownershipClock.live.isEmpty,
              "A scheduled deadline cannot retain its controller after the app owner releases it")
        return count
    }
}

private final class WorkTestClock {
    var now: TimeInterval = 1000
    var timers: [WorkTestTimer] = []
    var live: [WorkTestTimer] { timers.filter { !$0.invalidated } }
    func schedule(interval: TimeInterval, repeats: Bool, tolerance: TimeInterval, action: @escaping () -> Void) -> WorkModeTimer {
        let timer = WorkTestTimer(interval: interval, repeats: repeats, action: action)
        timers.append(timer); return timer
    }
    func fireRepeating() { for timer in live where timer.repeats { timer.fire() } }
}

private final class WorkTestTimer: WorkModeTimer {
    let interval: TimeInterval
    let repeats: Bool
    private let action: () -> Void
    private(set) var invalidated = false
    init(interval: TimeInterval, repeats: Bool, action: @escaping () -> Void) {
        self.interval = interval; self.repeats = repeats; self.action = action
    }
    func fire() {
        guard !invalidated else { return }
        if !repeats { invalidated = true }
        action()
    }
    func fireEvenIfInvalidated() { action() }
    func invalidate() { invalidated = true }
}
