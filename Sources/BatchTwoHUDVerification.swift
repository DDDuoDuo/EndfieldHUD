import AppKit

/// Native smoke checks run only in the isolated --ui-test app. They exercise
/// projected pointer routing and the original source renderer, not a mock HUD.
enum BatchTwoHUDVerification {
    private static var session: Session?

    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        precondition(session == nil && overlay.systemPhase == .closed)
        let next = Session(overlay: overlay)
        session = next
        next.start()
    }

    private final class Session {
        let overlay: OverlayController
        private var view: SystemHUDView!
        private var source: HUDSourceWatchView!
        private var originalShell: ObjectIdentifier?
        private var originalSource: ObjectIdentifier?
        private var originalLogo: HUDSourceID?
        private var assertions = 0
        private var waitGeneration = 0
        private var finished = false
        private var physicalInputMonitor: Any?
        private var restingBadgeRect = CGRect.zero

        init(overlay: OverlayController) { self.overlay = overlay }

        func start() {
            check(overlay.settingsController != nil, "The isolated app configured its shared settings controller")
            // Directly delivered fixture events bypass this monitor. Real
            // keyboard/mouse input must not retarget the fixture mid-check.
            physicalInputMonitor = NSEvent.addLocalMonitorForEvents(matching: [
                .mouseMoved, .mouseEntered, .mouseExited, .cursorUpdate,
                .leftMouseDown, .leftMouseUp, .leftMouseDragged,
                .rightMouseDown, .rightMouseUp, .rightMouseDragged,
                .otherMouseDown, .otherMouseUp, .otherMouseDragged,
                .scrollWheel, .magnify, .rotate, .swipe, .pressure,
                .keyDown, .keyUp, .flagsChanged
            ]) { _ in nil }
            if let screen = NSScreen.main?.frame {
                let pointer = CGPoint(x: screen.midX, y: screen.midY)
                overlay.systemPointerLocationProviderForVerification = { pointer }
            }
            var configuration = overlay.settingsController!.configuration
            configuration.closeOnFocusLost = false
            configuration.hudScale = 1; configuration.hudOffsetX = 0; configuration.hudOffsetY = 0
            configuration.clockStyle = .digital; configuration.centerLogo = .endfield
            configuration.centerLogoRevision = nil; configuration.alertMetric = .battery
            configuration.language = .english
            overlay.settingsController!.update { $0 = configuration }
            overlay.initialModuleRequest = .power
            check(overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration), "HUD opens")
            ignoreNativeInput()
            waitUntil("source HUD deployment", timeout: 15, condition: { [self] in
                overlay.systemPhase == .open && !overlay.isSwitchingSystemModule
                    && NSApp.windows.contains { $0.contentView is SystemHUDView }
            }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                check(view.sourceFailureForVerification == nil && view.sourceWatchForVerification != nil,
                      "The native source shell loaded without a fallback")
                source = view.sourceWatchForVerification!
                originalShell = overlay.systemShellIdentity; originalSource = ObjectIdentifier(source)
                check(view.clockIsInStatusPanelForVerification, "Clock text occupies its projected source banner")
                originalLogo = source.desktopCenterLogoStateForVerification.nodeID
                check(originalLogo != nil, "The authored EndfieldText logo node exists")
                checkClock(index: 0)
            }
        }

        private func checkClock(index: Int) {
            // Begin with a changed style and finish at the original: every
            // indicator is exercised as a real change, including Digital.
            let choices: [HUDClockStyle] = [.split, .dial, .rail, .stacked, .digital]
            guard index < choices.count else { checkClockOcclusion(); return }
            let style = choices[index], rect = HUDClockStyleArtwork.indicatorRect(choices[index].index)
            guard let point = view.clockPointForVerification(CGPoint(x: rect.midX, y: rect.midY)) else {
                fail("Clock indicator has no source projection")
            }
            check(view.bounds.contains(point), "Clock indicator \(style.index + 1) is inside the native window")
            let button = view.subviews.compactMap { $0 as? HUDClockActionButton }.first { $0.styleIndex == style.index }
            check(button != nil && button!.frame.contains(point) && !button!.isHidden && button!.isEnabled,
                  "Projected clock pointer and native accessibility geometry agree")
            click(at: point)
            waitUntil("clock style \(style.rawValue)", condition: { [self] in
                view.clockStyleForVerification == style && overlay.settingsController?.configuration.clockStyle == style
            }) { [self] in
                check(view.clockTransitionIsHorizontalForVerification, "Clock pages slide horizontally inside their retained clipped viewport")
                check(view.clockTransitionKeepsFrameStationaryForVerification,
                      "The frame, indicator bars and Work Mode badge stay outside the moving clock page")
                check(overlay.systemSelectedModule == .power, "Indicator selection keeps the current center module")
                check(button?.accessibilityValue() as? String == L10n.text("Selected", "已选择"),
                      "Clock accessibility selects the clicked indicator")
                checkRetainedShell()
                checkClock(index: index + 1)
            }
        }

        private func checkClockOcclusion() {
            let rect = HUDClockStyleArtwork.body
            guard let point = view.clockPointForVerification(CGPoint(x: rect.midX, y: rect.midY)) else {
                fail("Clock body has no source projection")
            }
            let pin: UUID
            do {
                guard let id = try view.installPinnedNoteForVerification(over: point) else {
                    fail("Could not project the isolated pinned-note fixture")
                }
                pin = id
            } catch { fail("Could not save the isolated pinned-note fixture: \(error)") }
            check(overlay.visibleNotesForVerification.contains(pin)
                  && overlay.notesForVerification.contains { $0.id == pin && $0.isPinned },
                  "An actual persisted pinned note covers the clock on another module")
            let style = view.clockStyleForVerification
            click(at: point)
            scrollClock(at: point)
            check(overlay.systemSelectedModule == .power && !overlay.isSwitchingSystemModule
                  && view.clockStyleForVerification == style,
                  "A pinned note consumes clock-body clicks and horizontal scrolling beneath it")
            overlay.performNoteActionForVerification("note:\(pin.uuidString):delete")
            overlay.performNoteActionForVerification("note:\(pin.uuidString):confirmDelete")
            waitUntil("removing the occluding note", condition: { [self] in
                !overlay.visibleNotesForVerification.contains(pin)
                    && !overlay.notesForVerification.contains { $0.id == pin }
            }) { [self] in
                overlay.selectSystemModule(.profile)
                waitUntil("Profile module for popup input", condition: { [self] in
                    overlay.systemSelectedModule == .profile && !overlay.isSwitchingSystemModule
                }) { [self] in
                    view.performProfileActionForVerification("profile:menu")
                    check(view.profilePopoverOpenForVerification, "The retained profile popover opens")
                    guard let currentPoint = view.clockPointForVerification(CGPoint(x: rect.midX, y: rect.midY)) else {
                        fail("Clock projection disappeared during profile navigation")
                    }
                    scrollClock(at: currentPoint)
                    check(view.profilePopoverOpenForVerification && view.clockStyleForVerification == style,
                          "An open profile popover consumes clock scrolling")
                    click(at: currentPoint)
                    waitUntil("profile popup dismissing outside", condition: { [self] in
                        !view.profilePopoverOpenForVerification
                    }) { [self] in
                        check(overlay.systemSelectedModule == .profile && !overlay.isSwitchingSystemModule,
                              "The first clock-area click dismisses the profile popover without opening Work Mode")
                        checkRetainedShell()
                        scrollClock(at: currentPoint)
                        waitUntil("uncovered clock scrolling", condition: { [self] in
                            view.clockStyleForVerification != style
                        }) { [self] in
                            check(overlay.systemSelectedModule == .profile,
                                  "An uncovered horizontal clock swipe changes style without changing modules")
                            checkClockBody()
                        }
                    }
                }
            }
        }

        private func checkClockBody() {
            let rect = HUDClockStyleArtwork.body
            guard let point = view.clockPointForVerification(CGPoint(x: rect.midX, y: rect.midY)) else {
                fail("Clock body has no source projection")
            }
            click(at: point)
            waitUntil("clock body opening Work Mode", condition: { [self] in
                overlay.systemSelectedModule == .workMode && !overlay.isSwitchingSystemModule
            }) { [self] in
                check(overlay.systemCenterContentCount == 1, "Clock body switches only the retained center to Work Mode")
                checkRetainedShell()
                overlay.selectSystemModule(.power)
                waitUntil("Power module", condition: { [self] in
                    overlay.systemSelectedModule == .power && !overlay.isSwitchingSystemModule
                        && view.powerSettingsPointForVerification != nil
                }) { [self] in
                    click(at: view.powerSettingsPointForVerification!)
                    waitUntil("Power battery-settings shortcut", condition: { [self] in
                        overlay.systemSelectedModule == .display && !overlay.isSwitchingSystemModule
                            && !view.settingsIsTransitioningForVerification
                            && view.settingsActionIDsForVerification.contains("metric")
                    }) { [self] in
                        check(view.settingsActionIDsForVerification.contains("back"),
                              "The Power shortcut opens the Battery subsection with its parent navigation")
                        checkRetainedShell()
                        checkLogo(index: 0)
                    }
                }
            }
        }

        private func checkLogo(index: Int) {
            let choices: [HUDCenterLogo] = [.rhodesIsland, .babel, .rhineLab, .endfield]
            guard index < choices.count else { awaitRestingBadge(); return }
            let choice = choices[index]
            overlay.settingsController!.update { $0.centerLogo = choice }
            waitUntil("center logo \(choice.rawValue)", condition: { [self] in
                source.desktopCenterLogoStateForVerification.key == choice.rawValue + ":"
            }) { [self] in
                do {
                    let bitmap = try source.renderedImageForVerification()
                    check(bitmap.width > 0 && bitmap.height > 0, "Selected center logo reaches an actual GPU drawable")
                } catch { fail("Logo render failed: \(error)") }
                let state = source.desktopCenterLogoStateForVerification
                check(state.nodeID == originalLogo && state.nodeID != nil
                      && source.currentFrameForVerification?.node(state.nodeID!)?.activeInHierarchy == true,
                      "Logo swaps keep the original projected source node alive")
                check(state.overridden == (choice != .endfield) && state.glowHidden == (choice != .endfield),
                      "Preset artwork uses its baked glow and Endfield restores the original image/glow pair")
                checkRetainedShell()
                checkLogo(index: index + 1)
            }
        }

        private func awaitRestingBadge() {
            waitUntil("the badge's fixed hold and collapse", timeout: 12, condition: { [self] in
                overlay.systemChargeStageForVerification == .circle
                    && abs(overlay.systemChargeHitRectForVerification.width - HUDChargeBadge.circleHitRect.width) < 0.1
            }) { [self] in
                restingBadgeRect = overlay.systemChargeHitRectForVerification
                check(!restingBadgeRect.isEmpty, "The resting charge circle keeps its current hit area")
                checkMetric(index: 0)
            }
        }

        private func checkMetric(index: Int) {
            let choices: [HUDChargeMetric] = [.ram, .cpu, .network, .disk, .battery]
            guard index < choices.count else { checkHoveredMetric(); return }
            let metric = choices[index]
            view.performSettingsActionForVerification("metric")
            waitUntil("reading chooser", condition: { [self] in
                checkRestingBadge()
                return !view.settingsIsTransitioningForVerification
                    && view.settingsActionIDsForVerification.contains("metric:" + metric.rawValue)
            }) { [self] in
                view.performSettingsActionForVerification("metric:" + metric.rawValue)
                checkRestingBadge()
                waitUntil("reading \(metric.rawValue)", condition: { [self] in
                    checkRestingBadge()
                    return !view.settingsIsTransitioningForVerification
                        && overlay.settingsController?.configuration.alertMetric == metric
                        && readingLabelMatches(metric)
                }) { [self] in
                    check(view.settingsActionIDsForVerification.contains("metric"),
                          "Reading selection returns to Battery settings")
                    checkRetainedShell()
                    checkMetric(index: index + 1)
                }
            }
        }

        private func checkHoveredMetric() {
            overlay.setSystemChargeHoveredForVerification(true)
            waitUntil("hover expansion", condition: { [self] in
                overlay.systemChargeStageForVerification == .compact
                    && abs(overlay.systemChargeHitRectForVerification.width - HUDChargeBadge.compactHitRect.width) < 0.1
            }) { [self] in
                let expanded = overlay.systemChargeHitRectForVerification
                overlay.settingsController!.update { $0.alertMetric = .ram }
                waitUntil("hovered metric update", condition: { [self] in
                    check(overlay.systemChargeStageForVerification == .compact
                          && rectMatches(overlay.systemChargeHitRectForVerification, expanded),
                          "Changing a hovered reading preserves the expanded capsule and its animation state")
                    return readingLabelMatches(.ram)
                }) { [self] in
                    overlay.settingsController!.update { $0.alertMetric = .battery }
                    overlay.setSystemChargeHoveredForVerification(false)
                    waitUntil("pointer-exit collapse", condition: { [self] in
                        overlay.systemChargeStageForVerification == .circle
                            && rectMatches(overlay.systemChargeHitRectForVerification, restingBadgeRect)
                    }) { [self] in close() }
                }
            }
        }

        private func readingLabelMatches(_ metric: HUDChargeMetric) -> Bool {
            let prefix: String
            switch metric {
            case .network: prefix = L10n.text("Upload ", "上传 ")
            case .disk: prefix = L10n.text("Read ", "读取 ")
            default: prefix = metric.title + ":"
            }
            return view.chargeAccessibilityLabelForVerification.hasPrefix("EndfieldHUD, " + prefix)
        }

        private func checkRestingBadge() {
            check(overlay.systemChargeStageForVerification == .circle
                  && rectMatches(overlay.systemChargeHitRectForVerification, restingBadgeRect),
                  "Reading/tab updates preserve the collapsed badge without replaying its charge sequence")
        }

        private func close() {
            overlay.closeSystemOverlay()
            waitUntil("HUD close", timeout: 12, condition: { [self] in overlay.systemPhase == .closed }) { [self] in
                check(overlay.lastClosedAnimationCount == 0 && overlay.systemAnimationCount == 0,
                      "Closing removes every retained UI animation")
                check(!overlay.lastClosedSourceTimerActive && overlay.lastClosedSourcePhase == .concealed,
                      "The source renderer's display clock is stopped while the HUD is hidden")
                cleanup()
                print("PASS: \(assertions) batch-two HUD assertions; projected five-style clock clicks; Work Mode body action; Power settings shortcut; source logo swaps/restoration; badge metric lifecycle; clean close")
                BatchTwoHUDVerification.session = nil
                NSApp.terminate(nil)
            }
        }

        private func click(at point: CGPoint) {
            guard point.x.isFinite, point.y.isFinite, view.bounds.contains(point), let window = view.window else {
                fail("Native control point is outside the active HUD")
            }
            ignoreNativeInput()
            let location = view.convert(point, to: nil)
            for type in [NSEvent.EventType.leftMouseDown, .leftMouseUp] {
                guard let event = NSEvent.mouseEvent(with: type, location: location, modifierFlags: [],
                    timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                    context: nil, eventNumber: 1, clickCount: 1, pressure: type == .leftMouseDown ? 1 : 0) else {
                    fail("Could not construct the fixture's native pointer event")
                }
                if type == .leftMouseDown { view.mouseDown(with: event) } else { view.mouseUp(with: event) }
            }
        }

        private func scrollClock(at point: CGPoint) {
            guard view.bounds.contains(point), let wheel = CGEvent(scrollWheelEvent2Source: nil,
                units: .pixel, wheelCount: 2, wheel1: 0, wheel2: -40, wheel3: 0),
                let initial = NSEvent(cgEvent: wheel) else { fail("Could not create the native horizontal scroll event") }
            // A directly delivered CG event has no assigned AppKit window.
            // Translate its Cocoa location to this window's local coordinates;
            // Quartz's vertical screen axis runs opposite to AppKit's.
            let target = view.convert(point, to: nil), observed = initial.locationInWindow
            wheel.location = CGPoint(x: wheel.location.x + target.x - observed.x,
                                     y: wheel.location.y - target.y + observed.y)
            guard let event = NSEvent(cgEvent: wheel) else { fail("Could not wrap the horizontal scroll event") }
            let actual = view.convert(event.locationInWindow, from: nil)
            check(abs(actual.x - point.x) < 0.1 && abs(actual.y - point.y) < 0.1
                  && event.scrollingDeltaX != 0,
                  "The native wheel fixture hits the projected clock with a horizontal delta")
            view.scrollWheel(with: event)
        }

        private func checkRetainedShell() {
            check(overlay.systemShellIdentity == originalShell && view.sourceWatchForVerification.map(ObjectIdentifier.init) == originalSource,
                  "The shell and source renderer survive settings/module changes")
        }
        private func rectMatches(_ a: CGRect, _ b: CGRect) -> Bool {
            abs(a.minX - b.minX) < 0.1 && abs(a.minY - b.minY) < 0.1
                && abs(a.width - b.width) < 0.1 && abs(a.height - b.height) < 0.1
        }
        private func ignoreNativeInput() {
            NSApp.windows.compactMap { $0 as? NSPanel }.forEach { $0.ignoresMouseEvents = true }
        }
        private func waitUntil(_ label: String, timeout: TimeInterval = 8,
                               condition: @escaping () -> Bool, completion: @escaping () -> Void) {
            waitGeneration += 1
            let generation = waitGeneration, deadline = ProcessInfo.processInfo.systemUptime + timeout
            func poll() {
                guard !self.finished, self.waitGeneration == generation else { return }
                self.ignoreNativeInput()
                if condition() { completion(); return }
                if ProcessInfo.processInfo.systemUptime >= deadline { self.fail("Timed out waiting for \(label)") }
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05, execute: poll)
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.02, execute: poll)
        }
        private func cleanup() {
            finished = true; waitGeneration += 1
            if let physicalInputMonitor { NSEvent.removeMonitor(physicalInputMonitor) }
            physicalInputMonitor = nil; overlay.systemPointerLocationProviderForVerification = nil
        }
        private func check(_ value: Bool, _ message: String) {
            assertions += 1
            if !value { fail(message) }
        }
        private func fail(_ message: String) -> Never {
            fputs("FAIL: batch-two assertion \(assertions): \(message); phase=\(overlay.systemPhase.rawValue), module=\(String(describing: overlay.systemSelectedModule)), badge=\(String(describing: overlay.systemChargeStageForVerification))\n", stderr)
            fflush(stderr)
            cleanup(); overlay.forceCloseSystemOverlay()
            preconditionFailure(message)
        }
    }
}
