import AppKit
import CoreAudio
import QuartzCore

enum VolumeCanvasTests {
    static func run() -> Int {
        _ = NSApplication.shared // NSControl dispatches accessibility actions through NSApp.
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        let previousLanguage = L10n.language
        defer { L10n.language = previousLanguage }
        L10n.language = .english
        var fixture = AudioDeviceSnapshot()
        fixture.outputs = (1...8).map { index in
            var item = AudioDeviceInfo(id: UInt32(index), name: "Output \(index)", outputChannels: 2,
                                       isHeadphones: index == 2, isBluetooth: index == 3)
            item.canBeDefaultOutput = index != 8
            item.transportType = index == 3 ? kAudioDeviceTransportTypeBluetooth : kAudioDeviceTransportTypeBuiltIn
            return item
        }
        fixture.inputs = [AudioDeviceInfo(id: 20, name: "Built-in Microphone", inputChannels: 1),
                          AudioDeviceInfo(id: 21, name: "USB Microphone", inputChannels: 2)]
        fixture.defaultOutputID = 1; fixture.defaultInputID = 20
        fixture.outputVolume = 0.4; fixture.canSetOutputVolume = true
        fixture.outputMuted = false; fixture.canSetOutputMute = true
        fixture.balance = 0; fixture.canSetBalance = true
        fixture.canSetDefaultOutput = true; fixture.canSetDefaultInput = true
        fixture.applicationActivitySupported = true
        fixture.activeApplications = [AudioApplicationInfo(id: 1, pid: 101, name: "Music", outputDeviceIDs: [1]),
                                      AudioApplicationInfo(id: 2, pid: 102, name: "Voice", isRunningOutput: false, isRunningInput: true),
                                      AudioApplicationInfo(id: 3, pid: 103, name: "Browser", outputDeviceIDs: [1])]
        fixture.availableApplications = fixture.activeApplications.filter { !$0.outputDeviceIDs.isEmpty }
        let controller = AudioDeviceController(snapshot: fixture)
        let canvas = VolumeCanvas(controller: controller)
        let dark = HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2)
        let light = HUDModuleContentStyle(dark: false, accent: .systemGreen, contentsScale: 2.35)
        func center(_ rect: CGRect) -> CGPoint { CGPoint(x: rect.midX, y: rect.midY) }
        func strings(_ layer: CALayer) -> [String] {
            (layer as? CATextLayer).flatMap { $0.string as? String }.map { [$0] } ?? []
                + (layer.sublayers ?? []).flatMap(strings)
        }
        func animations(_ layer: CALayer) -> [CAAnimation] {
            (layer.animationKeys() ?? []).compactMap { layer.animation(forKey: $0) }
                + (layer.sublayers ?? []).flatMap(animations)
                + (layer.mask.map(animations) ?? [])
        }
        let beforeRendering = controller.snapshot
        let persistentLayer = canvas.makeContent(for: .volume, style: dark)
        check(controller.snapshot == beforeRendering && !controller.isRunning,
              "Constructing or rendering volume controls never writes audio or starts hardware observation")
        canvas.activate()
        check(controller.isRunning && controller.snapshot == beforeRendering, "Activation observes existing fixture state without changing it")
        canvas.perform(actionID: "audio:headphones")
        check(canvas.accessibleSliders.count == 2 && canvas.accessibleSliders.allSatisfy(\.enabled),
              "Writable stereo output exposes volume and balance controls")
        check(!canvas.mouseDown(at: CGPoint(x: -1, y: 40)), "The audio canvas does not consume input outside its host")
        let volume = canvas.accessibleSliders.first { $0.id == "volume" }!
        _ = canvas.mouseDown(at: CGPoint(x: volume.rect.minX + 6, y: volume.rect.midY))
        check(canvas.isDragging && controller.snapshot.outputVolume == 0,
              "Dragging starts from the pointer's true scalar position")
        canvas.mouseDragged(to: CGPoint(x: volume.rect.maxX + 300, y: volume.rect.midY))
        check(controller.snapshot.outputVolume == 1, "Dragging beyond the track clamps volume to its supported maximum")
        canvas.mouseUp()
        check(!canvas.isDragging, "Mouse-up releases an active hardware-control gesture")
        check(!canvas.setSlider(id: "volume", value: .nan) && controller.snapshot.outputVolume == 1,
              "Nonfinite values cannot reach a hardware setter")
        _ = canvas.mouseDown(at: center(volume.rect))
        var unlockNotifications = 0
        canvas.onChange = { if !canvas.isDragging { unlockNotifications += 1 } }
        _ = controller.setDefaultOutput(2)
        let changedOutputVolume = controller.snapshot.outputVolume
        check(!canvas.isDragging && unlockNotifications > 0,
              "An observed default-output change cancels the previous gesture and notifies the input bridge")
        canvas.mouseDragged(to: CGPoint(x: volume.rect.maxX + 100, y: volume.rect.midY))
        check(controller.snapshot.outputVolume == changedOutputVolume,
              "Further samples from the old gesture cannot adjust the replacement output")
        _ = canvas.mouseDown(at: CGPoint(x: volume.rect.minX + 6, y: volume.rect.midY))
        check(canvas.isDragging && controller.snapshot.outputVolume == 0,
              "A fresh mouse-down can deliberately adjust the replacement output")
        canvas.mouseUp()
        _ = controller.setDefaultOutput(1)
        _ = canvas.setSlider(id: "balance", value: -0.6)
        check(controller.snapshot.balance.map { abs($0 + 0.6) < 0.001 } == true,
              "Balance uses the controller's left/right -1...1 range")
        canvas.nudgeSelectedSlider(by: 1)
        check(controller.snapshot.balance.map { abs($0 + 0.56) < 0.001 } == true,
              "Keyboard nudges the selected slider in bounded increments")
        canvas.perform(actionID: "audio:mute")
        check(controller.snapshot.outputMuted == true, "Mute is an explicit user action")
        canvas.perform(actionID: "audio:mute")
        check(controller.snapshot.outputMuted == false, "The same action unmutes supported output")

        canvas.perform(actionID: "audio:output")
        check(canvas.isChoosingDevice && canvas.accessibleSliders.isEmpty,
              "The inline output chooser replaces only center controls")
        check(canvas.accessibleActions.filter { $0.id.hasPrefix("audio:output:") }.count == 6,
              "Device chooser pages contain six readable rows")
        canvas.perform(actionID: "audio:next")
        check(canvas.pageIndex == 1 && canvas.accessibleActions.filter { $0.id.hasPrefix("audio:output:") }.count == 2,
              "Additional devices remain reachable on the next page")
        let unusable = canvas.accessibleActions.first { $0.id == "audio:output:8" }!
        check(!unusable.enabled, "An output ineligible for the default role is visibly disabled")
        _ = canvas.mouseDown(at: center(unusable.rect))
        check(controller.snapshot.defaultOutputID == 1 && canvas.isChoosingDevice,
              "Clicking an unsupported default device never attempts a switch")
        canvas.perform(actionID: "audio:output:7")
        check(controller.snapshot.defaultOutputID == 7 && !canvas.isChoosingDevice,
              "Selecting an eligible output applies the device and returns to the retained main scene")
        canvas.perform(actionID: "audio:input")
        canvas.perform(actionID: "audio:input:21")
        check(controller.snapshot.defaultInputID == 21 && !canvas.isChoosingDevice,
              "Input selection operates independently from default output")
        canvas.perform(actionID: "audio:output")
        for _ in 0..<30 { _ = canvas.scroll(at: CGPoint(x: 20, y: 50), delta: 0.5) }
        check(canvas.pageIndex == 0, "Small trackpad samples do not skip device pages")
        _ = canvas.scroll(at: CGPoint(x: 20, y: 50), delta: 30)
        check(canvas.pageIndex == 1, "Accumulated scrolling advances one device page")
        check(canvas.dismissChooser() && !canvas.dismissChooser(), "Only an open inline chooser consumes its dismiss command")
        _ = controller.setDefaultOutput(1)
        canvas.perform(actionID: "audio:applications")
        check(strings(canvas.layer).contains("Music") && strings(canvas.layer).contains("Browser")
              && !strings(canvas.layer).contains("Voice"),
              "Only adjustable output applications appear in the app list")
        check(!canvas.accessibleActions.contains { $0.id == "audio:next" || $0.id == "audio:previous" },
              "The application list uses continuous scrolling instead of page buttons")
        canvas.perform(actionID: "audio:headphones")
        check(strings(canvas.layer).contains("Output 2") && strings(canvas.layer).contains("Output 3"),
              "Headphone and Bluetooth metadata remain available alongside app controls")
        let stateBeforeRepaint = controller.snapshot
        L10n.language = .simplifiedChinese
        let updated = canvas.makeContent(for: .volume, style: light)
        canvas.updateRenderScale(3)
        check(updated === persistentLayer && controller.snapshot == stateBeforeRepaint,
              "Theme, language and scale changes preserve hardware state and the shared content layer")
        check(canvas.accessibleSliders.first?.label == "输出音量" && canvas.accessibleActions.contains { $0.id == "audio:output" && $0.label == "选择输出设备" },
              "Native control labels localize without changing command identities")
        check(animations(canvas.layer).count <= 2 && animations(canvas.layer).allSatisfy {
            $0.duration > 0 && $0.duration <= 0.2 && $0.repeatCount == 0 && $0.repeatDuration == 0
        }, "Audio actions use bounded depth and mask transitions without independent idle animation")
        L10n.language = .english

        let host = NSView(frame: CGRect(x: 0, y: 0, width: 400, height: 334))
        let input = HUDVolumeInteraction(canvas: canvas, host: host)
        input.project = { $0 }
        var locks = 0; input.onLock = { locks += 1 }
        input.setActive(true)
        let native = host.subviews.compactMap { $0 as? NSSlider }
        check(native.count == 2 && native.allSatisfy(\.isEnabled), "The projected scene exposes two real native accessibility sliders")
        let nativeVolume = native.first { $0.accessibilityLabel() == "Output volume" }!
        nativeVolume.setAccessibilityValue(NSNumber(value: 0.65))
        check(controller.snapshot.outputVolume.map { abs($0 - 0.65) < 0.001 } == true,
              "Accessibility setting a slider dispatches the validated audio setter")
        check(nativeVolume.accessibilityPerformIncrement() && controller.snapshot.outputVolume.map { abs($0 - 0.67) < 0.001 } == true,
              "VoiceOver increment performs a real bounded control change")
        let down = NSEvent.mouseEvent(with: .leftMouseDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: 0,
                                     context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
        _ = input.mouseDown(at: center(volume.rect), event: down)
        check(input.isInputLocked && locks > 0, "Dragging native audio controls freezes parallax for accurate input")
        input.mouseUp()
        check(!input.isInputLocked && !input.isPresentingPanel, "Release restores pointer motion without creating another editor window")
        canvas.perform(actionID: "audio:output")
        let escape = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: 0,
                                     context: nil, characters: "", charactersIgnoringModifiers: "", isARepeat: false, keyCode: 53)!
        check(input.keyDown(escape) && !input.keyDown(escape), "Escape backs out of a device chooser before the shell handles dismissal")
        input.deactivate()
        check(!controller.isRunning && host.subviews.allSatisfy(\.isHidden), "Leaving the module stops observation and hides its native controls")
        check(animations(canvas.layer).isEmpty, "Leaving Volume removes both content and mask transitions")
        let afterDeactivation = controller.snapshot
        check(!input.mouseDown(at: center(volume.rect), event: down) && controller.snapshot == afterDeactivation,
              "An inactive audio module cannot write volume through pointer input")

        var unsupported = AudioDeviceSnapshot()
        unsupported.outputs = [AudioDeviceInfo(id: 40, name: "Digital output", outputChannels: 2)]
        unsupported.defaultOutputID = 40
        unsupported.applicationActivityMessage = "Detection unavailable on this macOS version"
        let unavailableController = AudioDeviceController(snapshot: unsupported)
        let unavailable = VolumeCanvas(controller: unavailableController)
        _ = unavailable.makeContent(for: .volume, style: dark)
        unavailable.activate()
        let unsupportedBefore = unavailableController.snapshot
        check(unavailable.accessibleSliders.allSatisfy { !$0.enabled && $0.value == nil },
              "Unsupported volume and balance expose unavailable values, not fabricated slider readings")
        check(!unavailable.setSlider(id: "volume", value: 0.8) && !unavailable.setSlider(id: "balance", value: 0.4),
              "Disabled hardware controls reject value writes")
        unavailable.perform(actionID: "audio:mute")
        unavailable.perform(actionID: "audio:output")
        check(unavailableController.snapshot == unsupportedBefore && !unavailable.isChoosingDevice,
              "Unavailable mute and default-device roles stay read-only")
        unavailable.perform(actionID: "audio:applications")
        check(strings(unavailable.layer).contains("Detection unavailable on this macOS version"),
              "Unavailable process detection gives the controller's honest capability explanation")
        let disabledHost = NSView(frame: host.bounds)
        let disabledInput = HUDVolumeInteraction(canvas: unavailable, host: disabledHost)
        disabledInput.setActive(true)
        let disabledSlider = disabledHost.subviews.compactMap { $0 as? NSSlider }.first!
        check(!disabledSlider.isEnabled && disabledSlider.accessibilityValue() as? String == "Unavailable",
              "Disabled native sliders announce Unavailable instead of a false midpoint")
        disabledSlider.setAccessibilityValue(NSNumber(value: 0.2))
        check(!disabledSlider.accessibilityPerformIncrement() && unavailableController.snapshot == unsupportedBefore,
              "Accessibility cannot bypass unsupported control guards")
        disabledInput.deactivate()

        // A real controller over an in-memory HAL tests capability changes on
        // the same device; these calls never reach the machine's audio system.
        let backend = MutableAudioFixture()
        let changingController = AudioDeviceController(backend: backend)
        let changing = VolumeCanvas(controller: changingController)
        changing.activate()
        let changingVolume = changing.accessibleSliders.first { $0.id == "volume" }!
        _ = changing.mouseDown(at: center(changingVolume.rect))
        check(changing.isDragging, "The capability fixture begins with writable main volume")
        backend.canWriteVolume = false
        changingController.refresh()
        let writesAtRemoval = backend.writeCount
        check(!changing.isDragging && !changing.accessibleSliders.first(where: { $0.id == "volume" })!.enabled,
              "Removing write support on the same output cancels its gesture")
        backend.canWriteVolume = true
        changingController.refresh()
        changing.mouseDragged(to: CGPoint(x: changingVolume.rect.maxX, y: changingVolume.rect.midY))
        check(backend.writeCount == writesAtRemoval,
              "Restoring a capability does not revive a cancelled volume gesture")
        let changingBalance = changing.accessibleSliders.first { $0.id == "balance" }!
        _ = changing.mouseDown(at: center(changingBalance.rect))
        check(changing.isDragging, "A new balance gesture begins on a writable stereo pan")
        backend.canWriteBalance = false
        changingController.refresh()
        let writesAtPanRemoval = backend.writeCount
        changing.mouseDragged(to: CGPoint(x: changingBalance.rect.maxX, y: changingBalance.rect.midY))
        check(!changing.isDragging && backend.writeCount == writesAtPanRemoval,
              "Balance capability loss cancels the gesture before further hardware writes")
        changing.deactivate()

        // All routes below use isolated factories. Rendering, pointer and AX
        // tests never create a process tap or write the machine's audio state.
        let appController = AudioDeviceController(snapshot: fixture)
        let appRoutes = PerAppAudioController.fixture()
        let appCanvas = VolumeCanvas(controller: appController, perAppAudio: appRoutes)
        let appLayer = appCanvas.makeContent(for: .volume, style: dark)
        let appHost = NSView(frame: host.bounds)
        let appInput = HUDVolumeInteraction(canvas: appCanvas, host: appHost)
        appInput.project = { $0 }; appInput.setActive(true)
        func appSlider(_ scene: VolumeCanvas, _ id: UInt32 = 1) -> VolumeCanvasSlider? {
            scene.accessibleSliders.first { $0.id == "app:\(id)" }
        }
        func nativeAppSlider(_ view: NSView, _ name: String = "Music") -> NSSlider? {
            view.subviews.compactMap { $0 as? NSSlider }.first { $0.accessibilityLabel()?.hasPrefix(name + " · PID ") == true }
        }
        check(appRoutes.sessions.isEmpty && appController.snapshot == fixture,
              "Opening and rendering app rows never starts capture or changes hardware")
        check(strings(appCanvas.layer).allSatisfy { !$0.localizedCaseInsensitiveContains("experimental") },
              "The normal app-volume interface contains no experimental-mode description")
        check(appCanvas.accessibleSliders.count == 4 && appSlider(appCanvas)?.value == 1
              && appSlider(appCanvas)?.enabled == true && appSlider(appCanvas, 2) == nil,
              "Adjustable applications are visible by default alongside hardware controls while input-only rows are hidden")
        check(!appCanvas.accessibleActions.contains { $0.id.hasPrefix("audio:app:") || $0.id == "audio:review-route" },
              "Direct app sliders require no detail or confirmation submenu")
        appCanvas.perform(actionID: "audio:app:1")
        appCanvas.perform(actionID: "audio:review-route")
        appCanvas.perform(actionID: "audio:enable-route")
        check(appRoutes.sessions.isEmpty, "Obsolete submenu commands cannot implicitly start routing")
        check(!appInput.keyDown(escape), "The main app rows leave Escape available to close the HUD")
        check(appCanvas.setSlider(id: "app:1", value: 1) && appRoutes.sessions.isEmpty,
              "An untouched 100% app slider represents direct playback without starting capture")
        let initialAppSlider = appSlider(appCanvas)!
        _ = appInput.mouseDown(at: center(initialAppSlider.rect), event: down)
        check(appRoutes.sessions.first?.state == .active
              && appRoutes.sessions.first.map { abs($0.gain - 0.5) < 0.001 } == true,
              "The first direct drag starts one route at the chosen position instead of resetting to unity")
        check(appInput.isInputLocked && appCanvas.selectedSliderID == "app:1",
              "The first app drag locks parallax and selects its keyboard target")
        check(appCanvas.accessibleSliders.contains { $0.id == "volume" }
              && !appCanvas.accessibleActions.contains { $0.id.hasPrefix("audio:stop:") },
              "Routing leaves hardware controls visible without adding an X or Stop button")
        appInput.mouseDragged(to: CGPoint(x: initialAppSlider.rect.minX + 6 + (initialAppSlider.rect.width - 12) * 0.8,
                                         y: initialAppSlider.rect.midY))
        check(appRoutes.sessions.count == 1 && appRoutes.sessions.first.map { abs($0.gain - 0.8) < 0.001 } == true,
              "Subsequent drag samples update the same existing route")
        appInput.mouseUp()
        let left = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: 0,
                                   context: nil, characters: "", charactersIgnoringModifiers: "", isARepeat: false, keyCode: 123)!
        check(appInput.keyDown(left) && appRoutes.sessions.first.map { abs($0.gain - 0.78) < 0.001 } == true,
              "Arrow keys adjust the selected app directly after its first drag")
        check(appController.snapshot == fixture, "App gain leaves global hardware volume untouched")
        let nativeApp = nativeAppSlider(appHost)!
        check(nativeApp.accessibilityLabel() == "Music · PID 101 volume" && nativeApp.isEnabled,
              "The native app slider includes its process name and PID for unambiguous accessibility")
        nativeApp.setAccessibilityValue(NSNumber(value: 0.42))
        check(appRoutes.sessions.first.map { abs($0.gain - 0.42) < 0.001 } == true && appController.snapshot == fixture,
              "Native accessibility changes only the app's gain")
        check(nativeApp.accessibilityPerformDecrement()
              && appRoutes.sessions.first.map { abs($0.gain - 0.4) < 0.001 } == true,
              "VoiceOver decrement follows the same bounded direct control path")
        check(!appCanvas.setSlider(id: "app:1", value: .nan)
              && !appCanvas.setSlider(id: "app:2", value: 0.2),
              "Nonfinite values and input-only app sliders cannot create or change routes")
        _ = appCanvas.setSlider(id: "app:1", value: 4)
        check(appRoutes.sessions.count == 1 && appRoutes.sessions.first?.state == .active && appSlider(appCanvas)?.value == 1,
              "Values above unity clamp to 100% while retaining the enabled route")
        _ = appInput.mouseDown(at: center(initialAppSlider.rect), event: down)
        appRoutes.stop(processID: 1)
        check(!appInput.isInputLocked && appRoutes.sessions.isEmpty,
              "A route removed externally cancels the old drag even when its row becomes available to start again")
        appInput.mouseDragged(to: CGPoint(x: initialAppSlider.rect.maxX, y: initialAppSlider.rect.midY))
        check(appRoutes.sessions.isEmpty, "Old drag samples cannot resurrect a removed route")
        nativeApp.setAccessibilityValue(NSNumber(value: 0.35))
        check(appRoutes.sessions.first?.gain == 0.35,
              "A fresh explicit accessibility edit can start the still-visible app row again")
        L10n.language = .simplifiedChinese
        let localizedAppLayer = appCanvas.makeContent(for: .volume, style: light)
        appInput.layoutAccessibility()
        check(localizedAppLayer === appLayer && appSlider(appCanvas)?.label == "Music · PID 101 音量"
              && !appCanvas.accessibleActions.contains { $0.id.hasPrefix("audio:stop:") },
              "Direct app labels localize without adding stop controls or replacing the retained layer")
        L10n.language = .english
        _ = appCanvas.makeContent(for: .volume, style: dark)
        appInput.deactivate()
        check(appRoutes.sessions.first?.gain == 0.35 && !appController.isRunning
              && appHost.subviews.allSatisfy(\.isHidden) && animations(appLayer).isEmpty,
              "Hiding Volume stops metadata observation and animation while preserving the explicit route")
        appInput.setActive(true)
        check(appCanvas.selectedSliderID == "volume" && appSlider(appCanvas)?.value == 0.35,
              "Reopening resets the keyboard target but preserves direct app gain")
        check(appInput.keyDown(left) && appController.snapshot.outputVolume.map { abs($0 - 0.38) < 0.001 } == true
              && appRoutes.sessions.first?.gain == 0.35,
              "Reopened hardware arrows do not change app gain")
        _ = appCanvas.setSlider(id: "app:1", value: 1)
        check(appRoutes.sessions.count == 1 && appRoutes.sessions.first?.gain == 1 && appSlider(appCanvas)?.enabled == true
              && !appCanvas.accessibleActions.contains { $0.id == "audio:stop:1" },
              "Returning to 100% keeps the route and its live app row available")
        _ = appCanvas.setSlider(id: "app:1", value: 0.3)
        appCanvas.perform(actionID: "audio:output")
        var outputWhenRoutesStopped: UInt32?
        let stopObserver = appRoutes.observe {
            if appRoutes.sessions.isEmpty { outputWhenRoutesStopped = appController.snapshot.defaultOutputID }
        }
        appCanvas.perform(actionID: "audio:output:2")
        appRoutes.removeObserver(stopObserver)
        check(appRoutes.sessions.isEmpty && outputWhenRoutesStopped == 1 && appController.snapshot.defaultOutputID == 2,
              "An explicit output change stops existing routes before changing the hardware default")
        _ = appController.setDefaultOutput(3)
        check(appSlider(appCanvas) == nil
              && !appCanvas.setSlider(id: "app:1", value: 0.2),
              "Bluetooth output hides unadjustable app rows and rejects their stale slider commands")
        appInput.deactivate()

        var noOutputFixture = fixture; noOutputFixture.defaultOutputID = nil
        let noOutput = VolumeCanvas(controller: AudioDeviceController(snapshot: noOutputFixture), perAppAudio: .fixture())
        noOutput.activate()
        check(appSlider(noOutput) == nil && !noOutput.setSlider(id: "app:1", value: 0.2),
              "A missing default output hides unadjustable app rows")
        noOutput.deactivate()
        let unsupportedRoutes = PerAppAudioController(factory: UnsupportedAppAudioFixture())
        let unsupportedApps = VolumeCanvas(controller: AudioDeviceController(snapshot: fixture), perAppAudio: unsupportedRoutes)
        unsupportedApps.activate()
        check(appSlider(unsupportedApps) == nil && !unsupportedApps.setSlider(id: "app:1", value: 0.2)
              && unsupportedRoutes.sessions.isEmpty,
              "Older macOS hides unadjustable app rows and cannot start them through stale commands")
        unsupportedApps.deactivate()
        var unsupportedTransport = fixture
        unsupportedTransport.outputs[0].transportType = kAudioDeviceTransportTypeHDMI
        let transportRoutes = PerAppAudioController.fixture()
        let transportCanvas = VolumeCanvas(controller: AudioDeviceController(snapshot: unsupportedTransport), perAppAudio: transportRoutes)
        transportCanvas.activate()
        check(appSlider(transportCanvas) == nil && !transportCanvas.setSlider(id: "app:1", value: 0.4)
              && transportRoutes.sessions.isEmpty,
              "An unsupported transport hides unadjustable apps before a user can attempt routing")
        transportCanvas.deactivate()

        let quietRoutes = PerAppAudioController.fixture()
        _ = quietRoutes.start(application: fixture.activeApplications[0], output: fixture.outputs[0], initialGain: 0.25)
        var quietSnapshot = fixture; quietSnapshot.activeApplications = []
        quietSnapshot.availableApplications = [AudioApplicationInfo(id: 1, pid: 101, name: "Music",
            isRunningOutput: false, outputDeviceIDs: [1])]
        let quiet = VolumeCanvas(controller: AudioDeviceController(snapshot: quietSnapshot), perAppAudio: quietRoutes)
        quiet.activate()
        check(appSlider(quiet)?.enabled == true && appSlider(quiet)?.value == 0.25 && strings(quiet.layer).contains("Music"),
              "An enabled route keeps its direct row when its app becomes silent")
        check(appSlider(quiet)?.enabled == true && !quiet.accessibleActions.contains { $0.id.hasPrefix("audio:stop:") },
              "A silent route remains adjustable without an X button")
        _ = quiet.setSlider(id: "app:1", value: 1)
        check(quietRoutes.sessions.count == 1 && appSlider(quiet)?.enabled == true && appSlider(quiet)?.value == 1,
              "Returning a silent app to 100% retains its route and adjustable row")
        check(quiet.setSlider(id: "app:1", value: 0.2) && quietRoutes.sessions.first?.gain == 0.2,
              "A retained idle app changes gain through its existing route")
        _ = quiet.setSlider(id: "app:1", value: 1)
        quiet.deactivate(); quietRoutes.stopAll()

        var departedSnapshot = fixture
        departedSnapshot.activeApplications = []; departedSnapshot.availableApplications = []
        let departedRoutes = PerAppAudioController.fixture()
        _ = departedRoutes.start(application: fixture.activeApplications[0], output: fixture.outputs[0])
        let departed = VolumeCanvas(controller: AudioDeviceController(snapshot: departedSnapshot), perAppAudio: departedRoutes)
        departed.activate()
        check(appSlider(departed)?.enabled == true,
              "An owned route retains a way to restore direct playback even when discovery loses its process")
        _ = departed.setSlider(id: "app:1", value: 1)
        check(departedRoutes.sessions.count == 1 && appSlider(departed)?.value == 1,
              "A temporarily undiscovered owned process also retains its route at full volume")
        departedRoutes.stop(processID: 1)
        check(departedRoutes.sessions.isEmpty && appSlider(departed) == nil,
              "Only a departed process loses its row after its final owned route is removed")
        departed.deactivate()

        var scrollingFixture = fixture
        scrollingFixture.availableApplications = []
        for index in 0..<10 {
            let app = AudioApplicationInfo(id: UInt32(100 + index), pid: Int32(1_000 + index), name: "Scrollable \(index)",
                                           isRunningOutput: index % 2 == 0, outputDeviceIDs: [UInt32(1)])
            scrollingFixture.availableApplications.append(app)
        }
        scrollingFixture.activeApplications = scrollingFixture.availableApplications.filter(\.isRunningOutput)
        scrollingFixture.availableApplications += [
            AudioApplicationInfo(id: 200, pid: 2_000, name: "Input only", isRunningOutput: false, isRunningInput: true),
            AudioApplicationInfo(id: 201, pid: 2_001, name: "Other output", outputDeviceIDs: [2]),
            AudioApplicationInfo(id: 202, pid: 2_002, name: "Multiple outputs", outputDeviceIDs: [1, 2]),
            AudioApplicationInfo(id: 203, pid: ProcessInfo.processInfo.processIdentifier, name: "Own process", outputDeviceIDs: [1])]
        let scrollController = AudioDeviceController(snapshot: scrollingFixture)
        let scrollRoutes = PerAppAudioController.fixture()
        let scrollCanvas = VolumeCanvas(controller: scrollController, perAppAudio: scrollRoutes)
        let scrollHost = NSView(frame: host.bounds)
        let scrollInput = HUDVolumeInteraction(canvas: scrollCanvas, host: scrollHost)
        scrollInput.project = { $0 }; scrollInput.setActive(true)
        let viewport = scrollCanvas.applicationViewport
        check(scrollRoutes.sessions.isEmpty && !scrollCanvas.accessibleActions.contains {
            $0.id == "audio:next" || $0.id == "audio:previous" || $0.id.hasPrefix("audio:stop:")
        }, "A long app list opens without capture, X buttons or paging controls")
        check(appSlider(scrollCanvas, 100)?.enabled == true && appSlider(scrollCanvas, 101)?.enabled == true,
              "Both active and known idle output clients expose adjustable rows")
        check(!scrollCanvas.setSlider(id: "app:109", value: 0.3),
              "An offscreen row cannot receive stale pointer or accessibility writes")
        let rowBeforeScroll = appSlider(scrollCanvas, 100)!.rect
        check(scrollCanvas.scroll(at: center(viewport), delta: 0.5)
              && abs(scrollCanvas.applicationScrollOffset - 0.5) < 0.0001
              && abs(appSlider(scrollCanvas, 100)!.rect.minY - rowBeforeScroll.minY + 0.5) < 0.0001,
              "A sub-point trackpad sample moves app rows continuously without a page threshold")
        _ = scrollCanvas.scroll(at: center(viewport), delta: 9.5)
        let partial = appSlider(scrollCanvas, 100)!
        check(partial.rect.minY < viewport.minY && partial.visibleRect?.minY == viewport.minY
              && partial.visibleRect!.height < partial.rect.height,
              "A partial row retains full slider geometry but clips its visible and interactive rectangle")
        let allClipped = scrollCanvas.accessibleSliders.filter { $0.id.hasPrefix("app:") }
        check(allClipped.allSatisfy { slider in
            guard let clipped = slider.visibleRect else { return false }
            return !clipped.isNull && viewport.contains(clipped)
        }, "Every accessible app slider remains inside the app viewport")
        let partialNative = nativeAppSlider(scrollHost, "Scrollable 0")!
        check(partialNative.frame == partial.visibleRect!,
              "Projected native accessibility bounds use the clipped row rather than overlapping surrounding controls")
        _ = scrollInput.mouseDown(at: CGPoint(x: partial.rect.midX, y: viewport.minY - 1), event: down)
        check(scrollRoutes.sessions.isEmpty, "The clipped-off portion of an app slider cannot start routing")
        _ = scrollInput.mouseDown(at: center(partial.visibleRect!), event: down)
        check(scrollInput.isInputLocked && scrollRoutes.sessions.first?.processID == 100,
              "The visible part of a clipped slider retains correct pointer hit testing")
        let heldGain = scrollRoutes.sessions.first!.gain
        _ = scrollCanvas.scroll(at: center(viewport), delta: 0.5)
        scrollInput.mouseDragged(to: CGPoint(x: partial.rect.maxX + 20, y: partial.rect.midY))
        check(!scrollInput.isInputLocked && scrollRoutes.sessions.first?.gain == heldGain,
              "Scrolling cancels an in-flight drag before old pointer samples can change its row")
        let beforeInvalidScroll = scrollCanvas.applicationScrollOffset
        check(!scrollCanvas.scroll(at: CGPoint(x: viewport.midX, y: viewport.minY - 1), delta: 20)
              && !scrollCanvas.scroll(at: center(viewport), delta: .nan)
              && scrollCanvas.applicationScrollOffset == beforeInvalidScroll,
              "Out-of-viewport and nonfinite scroll input cannot move the list")
        _ = scrollCanvas.scroll(at: center(viewport), delta: 10_000)
        check(abs(scrollCanvas.applicationScrollOffset - (10 * 31 - viewport.height)) < 0.0001
              && appSlider(scrollCanvas, 109) != nil && appSlider(scrollCanvas, 100) == nil,
              "Continuous scrolling clamps to the last adjustable row without including filtered processes")
        partialNative.setAccessibilityValue(NSNumber(value: 0.1))
        check(partialNative.superview == nil && scrollRoutes.sessions.first?.gain == heldGain,
              "Detached native controls cannot adjust app rows scrolled out of view")
        check([UInt32(200), 201, 202, 203].allSatisfy { appSlider(scrollCanvas, $0) == nil }
              && !strings(scrollCanvas.layer).contains("Input only") && !strings(scrollCanvas.layer).contains("Own process"),
              "Input-only, different-route, multi-output and own-process entries remain filtered")
        _ = scrollCanvas.scroll(at: center(viewport), delta: -10_000)
        check(scrollCanvas.applicationScrollOffset == 0 && appSlider(scrollCanvas, 100) != nil,
              "Reverse scrolling clamps cleanly at the first adjustable row")
        _ = scrollCanvas.setSlider(id: "app:100", value: 1)
        check(scrollRoutes.sessions.count == 1 && scrollRoutes.sessions.first?.gain == 1 && appSlider(scrollCanvas, 100) != nil,
              "Returning a scrolled app to 100% keeps its route and live row")
        scrollInput.deactivate(); scrollInput.setActive(true)
        check(scrollCanvas.applicationScrollOffset == 0 && scrollRoutes.sessions.count == 1 && scrollRoutes.sessions.first?.gain == 1,
              "Reopening starts at the top and preserves a route at full volume")
        scrollInput.deactivate(); scrollRoutes.stopAll()

        let appIcon = NSImage(size: CGSize(width: 32, height: 32))
        appIcon.addRepresentation(NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 32, pixelsHigh: 32,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
            bytesPerRow: 128, bitsPerPixel: 32)!)
        let parentURL = URL(fileURLWithPath: "/isolated-fixture/哔哩哔哩.app")
        var brandedFixture = fixture
        brandedFixture.activeApplications = (1...2).map { index in
            AudioApplicationInfo(id: UInt32(index), pid: Int32(100 + index), bundleIdentifier: "test.helper.\(index)",
                name: "哔哩哔哩", outputDeviceIDs: [1], applicationURL: parentURL, icon: appIcon)
        }
        brandedFixture.availableApplications = brandedFixture.activeApplications
        let brandedRoutes = PerAppAudioController.fixture()
        let branded = VolumeCanvas(controller: AudioDeviceController(snapshot: brandedFixture), perAppAudio: brandedRoutes)
        branded.activate()
        func iconLayers(_ root: CALayer) -> [CALayer] {
            (root.name?.hasPrefix("volume.app.icon.") == true ? [root] : []) + (root.sublayers ?? []).flatMap(iconLayers)
        }
        check(iconLayers(branded.layer).count == 2 && iconLayers(branded.layer).allSatisfy { $0.contents != nil },
              "Each audio process renders its actual cached parent-app icon when available")
        check(appSlider(branded, 1)?.label == "哔哩哔哩 · PID 101 volume"
              && appSlider(branded, 2)?.label == "哔哩哔哩 · PID 102 volume"
              && strings(branded.layer).contains("PID 101") && strings(branded.layer).contains("PID 102"),
              "Helpers sharing a friendly app name retain distinct visible and accessible process identities")
        _ = branded.setSlider(id: "app:1", value: 0.2)
        _ = branded.setSlider(id: "app:2", value: 0.7)
        check(brandedRoutes.sessions.count == 2 && brandedRoutes.sessions.map(\.gain) == [0.2, 0.7]
              && brandedRoutes.sessions.allSatisfy { $0.applicationURL == parentURL && $0.icon === appIcon },
              "Friendly labels never merge helper routes and session ownership retains their icon metadata")
        var absentFixture = fixture
        absentFixture.activeApplications = []; absentFixture.availableApplications = []
        let absent = VolumeCanvas(controller: AudioDeviceController(snapshot: absentFixture), perAppAudio: brandedRoutes)
        absent.activate()
        check(iconLayers(absent.layer).count == 2 && strings(absent.layer).contains("哔哩哔哩"),
              "Owned routes preserve parent names and icons when discovery temporarily omits their processes")
        absent.deactivate(); branded.deactivate(); brandedRoutes.stopAll()

        let pendingFactory = PendingAppAudioFixture()
        let pendingRoutes = PerAppAudioController(factory: pendingFactory)
        let pendingCanvas = VolumeCanvas(controller: AudioDeviceController(snapshot: fixture), perAppAudio: pendingRoutes)
        let pendingHost = NSView(frame: host.bounds)
        let pendingInput = HUDVolumeInteraction(canvas: pendingCanvas, host: pendingHost)
        pendingInput.setActive(true)
        let pendingSlider = appSlider(pendingCanvas)!
        _ = pendingCanvas.setSlider(id: "app:1", value: 1)
        check(pendingFactory.makeCalls == 0 && pendingRoutes.sessions.isEmpty,
              "An untouched full-volume slider never even constructs an audio route")
        _ = pendingInput.mouseDown(at: center(pendingSlider.rect), event: down)
        check(pendingRoutes.sessions.first?.state == .preparing && pendingRoutes.sessions.first?.gain == 0.5
              && pendingFactory.route?.gainWrites.isEmpty == true,
              "The first drag stores chosen gain while preparation has not activated audio processing")
        check(appSlider(pendingCanvas)?.enabled == true && nativeAppSlider(pendingHost)?.isEnabled == true
              && !pendingCanvas.accessibleActions.contains { $0.id.hasPrefix("audio:stop:") },
              "Preparing keeps a real adjustable row without a stop button")
        pendingInput.mouseDragged(to: CGPoint(x: pendingSlider.rect.minX + 6 + (pendingSlider.rect.width - 12) * 0.2,
                                             y: pendingSlider.rect.midY))
        check(pendingRoutes.sessions.first.map { abs($0.gain - 0.2) < 0.001 } == true
              && pendingFactory.route?.gainWrites.isEmpty == true,
              "Drag updates during preparation replace pending gain without writing an unready route")
        nativeAppSlider(pendingHost)!.setAccessibilityValue(NSNumber(value: 0.31))
        check(pendingRoutes.sessions.first?.gain == 0.31 && pendingFactory.route?.gainWrites.isEmpty == true,
              "Native accessibility can queue the latest gain before activation")
        let pendingRoute = pendingFactory.route!
        for value in [1.0, 0.6, 1.0, 0.31] { _ = pendingCanvas.setSlider(id: "app:1", value: value) }
        check(pendingFactory.makeCalls == 1 && pendingFactory.route === pendingRoute && pendingRoute.stopCalls == 0
              && pendingRoutes.sessions.first?.state == .preparing,
              "Repeated unity crossings during preparation never cancel or recreate the route")
        pendingFactory.route!.emit(.active)
        check(pendingRoutes.sessions.first?.state == .active && pendingFactory.route?.gainWrites == [0.31]
              && appSlider(pendingCanvas)?.value == 0.31,
              "Activation applies exactly the latest requested gain instead of the initial value")
        for value in [1.0, 0.2, 1.0, 0.4, 1.0] { _ = pendingCanvas.setSlider(id: "app:1", value: value) }
        check(pendingFactory.makeCalls == 1 && pendingFactory.route === pendingRoute && pendingRoute.stopCalls == 0
              && pendingRoutes.sessions.first?.state == .active && pendingRoutes.sessions.first?.gain == 1
              && Array(pendingRoute.gainWrites.suffix(5)) == [1, 0.2, 1, 0.4, 1] && pendingInput.isInputLocked,
              "Repeated unity crossings on an active route write only gain without start/stop churn")
        pendingFactory.route!.emit(.failed("The selected process ended"))
        check(!pendingInput.isInputLocked && pendingRoutes.sessions.first?.state == .failed
              && appSlider(pendingCanvas)?.enabled == true && nativeAppSlider(pendingHost)?.isEnabled == true,
              "Async failure cancels the active gesture while retaining the native cleanup/retry slider")
        check(pendingCanvas.accessibilityStatus == "The selected process ended"
              && appSlider(pendingCanvas)?.help?.isEmpty == false,
              "A failed row preserves its error and accessible cleanup guidance")
        _ = pendingCanvas.setSlider(id: "app:1", value: 1)
        check(pendingRoutes.sessions.isEmpty && appSlider(pendingCanvas)?.enabled == true,
              "Clearing a failed route makes the same row available for an explicit new edit")
        pendingInput.deactivate()
        check(animations(pendingCanvas.layer).isEmpty && pendingHost.subviews.allSatisfy(\.isHidden),
              "Deactivation hides native app controls and removes finite transitions")

        let stoppingFactory = DeferredStopAudioFixture()
        let stoppingRoutes = PerAppAudioController(factory: stoppingFactory)
        let stoppingController = AudioDeviceController(snapshot: fixture)
        let stoppingCanvas = VolumeCanvas(controller: stoppingController, perAppAudio: stoppingRoutes)
        let stoppingHost = NSView(frame: host.bounds)
        let stoppingInput = HUDVolumeInteraction(canvas: stoppingCanvas, host: stoppingHost)
        stoppingInput.setActive(true)
        _ = stoppingCanvas.setSlider(id: "app:1", value: 0.4)
        let stoppingRoute = stoppingFactory.route!
        let stoppingNative = nativeAppSlider(stoppingHost)!
        _ = stoppingInput.mouseDown(at: center(appSlider(stoppingCanvas)!.rect), event: down)
        stoppingRoutes.stop(processID: 1)
        check(stoppingRoutes.sessions.first?.state == .stopping && stoppingRoute.isStopping && !stoppingRoute.isStopped,
              "An explicit lifecycle stop retains ownership until asynchronous restoration actually completes")
        check(!stoppingInput.isInputLocked && appSlider(stoppingCanvas)?.enabled == false && !stoppingNative.isEnabled,
              "Pending cleanup immediately disables direct gain and releases its drag lock")
        check(!stoppingCanvas.accessibleActions.contains { $0.id.hasPrefix("audio:stop:") }
              && stoppingNative.accessibilityHelp()?.contains("Waiting") == true,
              "Pending restoration has no stop button and describes its disabled slider to accessibility")
        let gainWritesBeforeStop = stoppingRoute.gainWrites
        stoppingNative.setAccessibilityValue(NSNumber(value: 0.9))
        stoppingInput.mouseDragged(to: center(appSlider(stoppingCanvas)!.rect))
        check(!stoppingCanvas.setSlider(id: "app:1", value: 0.8)
              && stoppingRoute.gainWrites == gainWritesBeforeStop && stoppingController.snapshot == fixture,
              "Stale drag and native gain writes cannot reach a stopping route")
        check(!stoppingCanvas.setSlider(id: "app:1", value: 1) && stoppingRoute.stopCalls == 1,
              "The disabled restoring slider cannot enqueue duplicate cleanup")
        stoppingRoute.emit(.active)
        check(stoppingRoutes.sessions.first?.state == .stopping && appSlider(stoppingCanvas)?.enabled == false,
              "Late activation cannot re-enable a stopping route")
        L10n.language = .simplifiedChinese
        _ = stoppingCanvas.makeContent(for: .volume, style: light)
        check(appSlider(stoppingCanvas)?.enabled == false && strings(stoppingCanvas.layer).contains("正在停止…"),
              "The disabled restoration state localizes in the retained app row")
        L10n.language = .english
        stoppingRoute.completeStop()
        check(stoppingRoutes.sessions.isEmpty && appSlider(stoppingCanvas)?.enabled == true
              && !stoppingCanvas.accessibleActions.contains { $0.id == "audio:stop:1" },
              "Cleanup confirmation removes the session and restores the available direct row")
        stoppingRoute.emit(.active)
        check(stoppingRoutes.sessions.isEmpty, "An old activation callback cannot recreate a removed session")
        _ = stoppingCanvas.setSlider(id: "app:1", value: 0.4)
        let retryRoute = stoppingFactory.route!
        stoppingRoutes.stopAll()
        retryRoute.completeStop(error: "Cleanup needs another attempt", resourcesRemain: true)
        check(stoppingRoutes.sessions.first?.state == .failed && appSlider(stoppingCanvas)?.enabled == true
              && stoppingCanvas.accessibilityStatus == "Cleanup needs another attempt",
              "Cleanup failure retains the 100% retry path on its slider")
        _ = stoppingCanvas.setSlider(id: "app:1", value: 1)
        check(stoppingRoutes.sessions.first?.state == .stopping && retryRoute.stopCalls == 2,
              "A cleanup retry returns to the waiting state")
        retryRoute.completeStop()
        stoppingInput.deactivate()
        check(stoppingRoutes.sessions.isEmpty && animations(stoppingCanvas.layer).isEmpty
              && stoppingHost.subviews.allSatisfy(\.isHidden),
              "Successful retry and deactivation leave no active route or visible native controls")
        return count
    }

    private struct UnsupportedAppAudioFixture: PerAppAudioRouteFactory {
        let isSupported = false
        func make(application: AudioApplicationInfo, output: AudioDeviceInfo,
                  event: @escaping (PerAppAudioRouteEvent) -> Void) throws -> PerAppAudioRoute {
            throw AudioDeviceError.unsupported
        }
    }

    private final class PendingAppAudioFixture: PerAppAudioRouteFactory {
        let isSupported = true
        var route: PendingAppAudioRoute?
        private(set) var makeCalls = 0
        func make(application: AudioApplicationInfo, output: AudioDeviceInfo,
                  event: @escaping (PerAppAudioRouteEvent) -> Void) throws -> PerAppAudioRoute {
            makeCalls += 1
            let value = PendingAppAudioRoute(event: event)
            route = value
            return value
        }
    }

    private final class PendingAppAudioRoute: PerAppAudioRoute {
        private let event: (PerAppAudioRouteEvent) -> Void
        private(set) var isStopped = true
        init(event: @escaping (PerAppAudioRouteEvent) -> Void) { self.event = event }
        private(set) var gainWrites: [Double] = []
        private var active = false
        private(set) var stopCalls = 0
        func begin() throws { isStopped = false }
        func setGain(_ value: Double) { if active { gainWrites.append(value) } }
        func stop() -> String? { stopCalls += 1; isStopped = true; active = false; return nil }
        func emit(_ value: PerAppAudioRouteEvent) {
            if case .active = value { active = true }
            event(value)
        }
    }

    private final class DeferredStopAudioFixture: PerAppAudioRouteFactory {
        let isSupported = true
        var route: DeferredStopAudioRoute?
        func make(application: AudioApplicationInfo, output: AudioDeviceInfo,
                  event: @escaping (PerAppAudioRouteEvent) -> Void) throws -> PerAppAudioRoute {
            let value = DeferredStopAudioRoute(event: event)
            route = value
            return value
        }
    }

    private final class DeferredStopAudioRoute: PerAppAudioRoute {
        private let event: (PerAppAudioRouteEvent) -> Void
        private(set) var isStopped = true
        private(set) var isStopping = false
        private(set) var stopCalls = 0
        private(set) var gainWrites = 0
        init(event: @escaping (PerAppAudioRouteEvent) -> Void) { self.event = event }
        func begin() throws { isStopped = false; event(.active) }
        func setGain(_ value: Double) { gainWrites += 1 }
        func stop() -> String? { stopCalls += 1; isStopping = true; return nil }
        func emit(_ value: PerAppAudioRouteEvent) { event(value) }
        func completeStop(error: String? = nil, resourcesRemain: Bool = false) {
            isStopping = false; isStopped = !resourcesRemain
            event(.stopped(error))
        }
    }

    private final class MutableAudioFixture: AudioHALBackend {
        let supportsProcessActivity = false
        var canWriteVolume = true
        var canWriteBalance = true
        var writeCount = 0
        private var volume: Float32 = 0.4
        private var pan: Float32 = 0.5
        private let device: UInt32 = 100
        private func data<T>(_ value: T) -> Data { var copy = value; return withUnsafeBytes(of: &copy) { Data($0) } }
        func read(_ property: AudioHALProperty) throws -> Data {
            if property.object == UInt32(kAudioObjectSystemObject) {
                if property.selector == kAudioHardwarePropertyDevices || property.selector == kAudioHardwarePropertyDefaultOutputDevice { return data(device) }
                if property.selector == kAudioHardwarePropertyDefaultInputDevice { return data(UInt32(0)) }
            }
            guard property.object == device else { throw AudioDeviceError.unavailable }
            switch property.selector {
            case kAudioDevicePropertyDeviceIsAlive: return data(UInt32(1))
            case kAudioDevicePropertyDeviceCanBeDefaultDevice: return data(UInt32(property.scope == kAudioDevicePropertyScopeOutput ? 1 : 0))
            case kAudioDevicePropertyStreamConfiguration:
                return data(AudioBufferList(mNumberBuffers: 1, mBuffers: AudioBuffer(mNumberChannels: property.scope == kAudioDevicePropertyScopeOutput ? 2 : 0, mDataByteSize: 0, mData: nil)))
            case kAudioDevicePropertyVolumeScalar:
                guard property.element == 0 else { throw AudioDeviceError.unavailable }; return data(volume)
            case kAudioDevicePropertyStereoPan: return data(pan)
            default: throw AudioDeviceError.unavailable
            }
        }
        func has(_ property: AudioHALProperty) -> Bool { (try? read(property)) != nil }
        func writable(_ property: AudioHALProperty) -> Bool {
            guard property.object == device, property.element == 0, property.scope == kAudioDevicePropertyScopeOutput else { return false }
            return (property.selector == kAudioDevicePropertyVolumeScalar && canWriteVolume)
                || (property.selector == kAudioDevicePropertyStereoPan && canWriteBalance)
        }
        func string(_ property: AudioHALProperty) throws -> String { "Mutable test output" }
        func sourceName(device: UInt32, source: UInt32, scope: UInt32) throws -> String { throw AudioDeviceError.unavailable }
        func write(_ property: AudioHALProperty, data: Data) throws {
            guard writable(property), data.count == MemoryLayout<Float32>.size else { throw AudioDeviceError.unsupported }
            var value: Float32 = 0
            _ = withUnsafeMutableBytes(of: &value) { data.copyBytes(to: $0) }
            if property.selector == kAudioDevicePropertyVolumeScalar { volume = value } else { pan = value }
            writeCount += 1
        }
        func listen(_ property: AudioHALProperty, changed: @escaping () -> Void) throws -> UUID { UUID() }
        func removeListener(_ token: UUID) {}
    }
}
