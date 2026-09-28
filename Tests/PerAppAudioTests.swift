import AppKit
import CoreAudio

enum PerAppAudioTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !value { fatalError(message, file: file, line: line) }
        }
        let app = AudioApplicationInfo(id: 7, pid: 1001, name: "Test app")
        let output = AudioDeviceInfo(id: 10, name: "Test output", uid: "test-output", outputChannels: 2)
        let factory = TestRouteFactory()
        let controller = PerAppAudioController(factory: factory)
        var notifications = 0
        let observer = controller.observe { notifications += 1 }
        check(controller.start(application: app, output: output), "Explicit start creates an independent route")
        check(controller.sessions.first?.state == .preparing && controller.sessions.first?.gain == 1,
              "A new route remains preparing at unity gain until its audio path confirms readiness")
        check(controller.setGain(0.5, processID: 7) && controller.sessions.first?.gain == 0.5
              && factory.routes[0].gain == 1, "A preparing slider stores intent without applying gain before activation")
        factory.routes[0].event(.active)
        check(factory.routes[0].gain == 0.5, "Activation applies the latest preparing slider value")
        check(controller.sessions.first?.state == .active && controller.setGain(0.3, processID: 7)
              && factory.routes[0].gain == 0.3, "Active session gain reaches the actual route instance")
        check(controller.setGain(9, processID: 7) && factory.routes[0].gain == 1,
              "Per-app gain is attenuation-only and cannot amplify above unity")
        let gainWrites = factory.routes[0].gainWrites, unityNotifications = notifications
        for _ in 0..<30 { check(controller.setGain(1, processID: 7), "Repeated unity slider input remains accepted") }
        check(factory.routes.count == 1 && !factory.routes[0].isStopped
              && factory.routes[0].gainWrites == gainWrites && notifications == unityNotifications,
              "Unchanged unity gain preserves the live route without queued gain jobs or redundant UI notifications")
        check(!controller.setGain(.nan, processID: 7), "Nonfinite gain values never reach audio processing")
        check(!controller.start(application: app, output: output), "A process cannot be tapped twice by this controller")
        let warningNotifications = notifications
        check(controller.setGain(1, processID: 7) && controller.statusMessage == nil
              && factory.routes[0].gainWrites == gainWrites && notifications == warningNotifications + 1,
              "An unchanged valid gain still clears an earlier warning without touching audio processing")
        let second = AudioApplicationInfo(id: 8, pid: 1002, name: "Second app")
        check(controller.start(application: second, output: output), "A second explicitly selected app gets a separate route")
        factory.routes[1].event(.active)
        controller.setGain(0.2, processID: 7); controller.setGain(0.7, processID: 8)
        check(factory.routes[0].gain == 0.2 && factory.routes[1].gain == 0.7, "Two app gains remain independent")
        controller.stop(processID: 7)
        factory.routes[0].event(.active)
        check(controller.sessions.count == 1 && controller.sessions[0].processID == 8,
              "A stale readiness event cannot revive a stopped route")
        check(controller.start(application: app, output: output), "A stopped app can be explicitly started again")
        factory.routes[0].event(.failed("Old callback"))
        check(controller.sessions.first { $0.processID == 7 }?.state == .preparing,
              "A previous generation's error cannot corrupt a replacement session")
        factory.routes[2].cannotStop = true
        controller.stop(processID: 7)
        check(controller.sessions.first { $0.processID == 7 }?.state == .failed
              && !controller.start(application: app, output: output), "Failed cleanup retains ownership and blocks overlapping replacement taps")
        let recreatedHALProcess = AudioApplicationInfo(id: 70, pid: app.pid, name: app.name)
        check(!controller.start(application: recreatedHALProcess, output: output) && factory.routes.count == 3,
              "Failed cleanup also blocks the same PID when HAL gives it a different process object ID")
        factory.routes[2].cannotStop = false
        controller.stopAll(reason: "Sleep")
        check(controller.sessions.isEmpty && controller.statusMessage == "Sleep", "Sleep-style teardown removes all routes without restarting them")
        var unsupported = output; unsupported.isBluetooth = true
        check(controller.availability(application: app, output: unsupported) != nil
              && !controller.start(application: app, output: unsupported), "Unsupported wireless output is explained before requesting capture")
        let own = AudioApplicationInfo(id: 99, pid: ProcessInfo.processInfo.processIdentifier, name: "Self")
        check(!controller.start(application: own, output: output), "Self capture is rejected to prevent feedback")
        factory.isSupported = false
        check(!controller.start(application: app, output: output) && controller.capabilityMessage != nil,
              "Older-system support gates do not create fake adjustable sessions")
        controller.removeObserver(observer)
        let oldNotifications = notifications
        controller.stopAll()
        check(notifications == oldNotifications, "Removed observers receive no session notifications")

        let initialFactory = TestRouteFactory()
        let initialController = PerAppAudioController(factory: initialFactory)
        check(!initialController.start(application: app, output: output, initialGain: .nan)
              && initialFactory.routes.isEmpty, "A nonfinite initial gain cannot create an audio route")
        check(initialController.start(application: app, output: output, initialGain: -0.4)
              && initialController.sessions.first?.gain == 0 && initialFactory.routes[0].gain == 1,
              "Initial slider intent is clamped without forwarding audio before activation")
        initialController.setGain(0.25, processID: app.id)
        initialFactory.routes[0].event(.active)
        check(initialFactory.routes[0].gain == 0.25, "The newest preparing slider edit supersedes its initial requested gain")
        initialController.stopAll()

        for inputPlanar in [false, true] {
            for outputPlanar in [false, true] {
                let source = StereoBuffers(frames: 300, planar: inputPlanar)
                let destination = StereoBuffers(frames: 300, planar: outputPlanar)
                source.fill(left: 0.8, right: -0.4)
                let state = PerAppAudioPCM.allocate()
                check(PerAppAudioPCM.render(input: source.list, output: destination.list, state: state)
                      && destination.sample(frame: 299, channel: 0) == 0,
                      "Priming validates each planar/interleaved pairing without duplicating the app's original output")
                check(PerAppAudioPCM.load(state, PerAppAudioPCM.signalSlot) == 1, "Nonzero input marks a viable captured signal")
                PerAppAudioPCM.store(state, PerAppAudioPCM.modeSlot, 1)
                PerAppAudioPCM.setGain(0.25, state: state)
                check(PerAppAudioPCM.render(input: source.list, output: destination.list, state: state), "Float32 stereo buffers render successfully")
                check(abs(destination.sample(frame: 299, channel: 0) - 0.2) < 0.00001
                      && abs(destination.sample(frame: 299, channel: 1) + 0.1) < 0.00001,
                      "Real PCM attenuation preserves left/right mapping in every buffer layout")
                check(abs(destination.sample(frame: 0, channel: 0) - 0.2) < 0.00001,
                      "The first forwarded sample starts at the requested gain instead of unity")
                PerAppAudioPCM.setGain(0, state: state)
                _ = PerAppAudioPCM.render(input: source.list, output: destination.list, state: state)
                check(destination.sample(frame: 299, channel: 0) == 0 && destination.sample(frame: 299, channel: 1) == 0,
                      "Gain zero produces digital silence after the bounded ramp")
                PerAppAudioPCM.setGain(1, state: state)
                source.set(frame: 299, channel: 0, value: .nan)
                source.set(frame: 299, channel: 1, value: .infinity)
                _ = PerAppAudioPCM.render(input: source.list, output: destination.list, state: state)
                check(destination.sample(frame: 299, channel: 0) == 0 && destination.sample(frame: 299, channel: 1) == 0,
                      "Nonfinite captured samples cannot enter the hardware output")
                PerAppAudioPCM.setGain(.nan, state: state)
                check(Float32(bitPattern: UInt32(bitPattern: PerAppAudioPCM.load(state, PerAppAudioPCM.gainSlot))) == 1,
                      "The atomic gain boundary ignores invalid values")
                PerAppAudioPCM.release(state)
                let mutedAtStart = PerAppAudioPCM.allocate()
                PerAppAudioPCM.setGain(0, state: mutedAtStart)
                PerAppAudioPCM.store(mutedAtStart, PerAppAudioPCM.modeSlot, 1)
                _ = PerAppAudioPCM.render(input: source.list, output: destination.list, state: mutedAtStart)
                check(destination.sample(frame: 0, channel: 0) == 0 && destination.sample(frame: 299, channel: 1) == 0,
                      "Initial zero gain is silent from the first sample in every planar/interleaved layout")
                PerAppAudioPCM.release(mutedAtStart)
            }
        }
        let mismatchInput = StereoBuffers(frames: 16, planar: false)
        let mismatchOutput = StereoBuffers(frames: 32, planar: false)
        mismatchOutput.fill(left: 1, right: 1)
        let mismatchState = PerAppAudioPCM.allocate()
        check(!PerAppAudioPCM.render(input: mismatchInput.list, output: mismatchOutput.list, state: mismatchState)
              && mismatchOutput.sample(frame: 31, channel: 1) == 0
              && PerAppAudioPCM.load(mismatchState, PerAppAudioPCM.faultSlot) == 1,
              "Unexpected frame counts clear output and request fail-safe teardown")
        PerAppAudioPCM.release(mismatchState)

        if #available(macOS 14.2, *) {
            func core(_ hardware: TestHardware, events: @escaping (PerAppAudioRouteEvent) -> Void = { _ in }) -> CorePerAppAudioRoute {
                CorePerAppAudioRoute(application: app, output: output, hardware: hardware, event: events)
            }
            do {
                let hardware = TestHardware()
                var becameActive = false
                let route = core(hardware) { if case .active = $0 { becameActive = true } }
                try route.begin()
                check(hardware.log == ["tap.unmuted", "aggregate", "io.create", "io.start"] && !becameActive,
                      "Actual engine startup leaves the source unmuted until a running callback proves usable audio")
                check(hardware.tapProcesses == [7] && hardware.privateTap && !hardware.exclusive,
                      "The tap includes only the explicitly selected process and is never a global tap")
                check(hardware.privateAggregate && hardware.subdeviceCount == 1,
                      "The engine creates one private aggregate without changing system defaults or broad device routing")
                check(hardware.stackedAggregate && !hardware.tapAutoStart && !hardware.hasChannelOverrides,
                      "The private single-output aggregate stacks live device channels without a tap-autostart wait")
                check(hardware.aliveReadCount > 0, "Aggregate liveness is confirmed before creating its IOProc")
                check(hardware.metadataWrites == 0, "Startup never writes original device volume, sample rate or system defaults")
                let pcm = StereoBuffers(frames: 128, planar: false)
                let out = StereoBuffers(frames: 128, planar: false)
                pcm.fill(left: 0.2, right: -0.2)
                _ = PerAppAudioPCM.render(input: pcm.list, output: out.list, state: hardware.context!)
                route.serviceHealth(now: ProcessInfo.processInfo.systemUptime)
                check(becameActive && hardware.log.last == "tap.mutedWhenTapped",
                      "A validated nonzero priming callback is the only path that suppresses original playback")
                route.setGain(0.4)
                _ = PerAppAudioPCM.render(input: pcm.list, output: out.list, state: hardware.context!)
                check(abs(out.sample(frame: 127, channel: 0) - 0.08) < 0.00001,
                      "Core route gain reaches its live render context")
                check(route.stop() == nil && route.isStopped, "Normal stop releases every owned audio resource")
                check(Array(hardware.log.suffix(5)) == ["tap.unmuted", "io.stop", "io.destroy", "aggregate.destroy", "tap.destroy"],
                      "Stop restores original playback before stopping and destroying the processing route")
                let stoppedLog = hardware.log
                hardware.fireRemoved()
                route.serviceHealth(now: ProcessInfo.processInfo.systemUptime + 99)
                check(hardware.log == stoppedLog, "Stale topology callbacks and watchdog work cannot revive a dismantled route")

                for failure in ["tap", "aggregate", "io.create", "io.start"] {
                    let fake = TestHardware(); fake.failAt = failure
                    let failing = core(fake)
                    var rejected = false
                    do { try failing.begin() } catch { rejected = true }
                    check(rejected && failing.isStopped && !fake.log.contains("tap.mutedWhenTapped"),
                          "Every startup failure rolls back without muting the original app")
                }
                let silent = TestHardware(); let silentRoute = core(silent)
                try silentRoute.begin()
                silentRoute.serviceHealth(now: ProcessInfo.processInfo.systemUptime + 61)
                check(silentRoute.isStopped && !silent.log.contains("tap.mutedWhenTapped"),
                      "Silent or permission-denied capture times out without suppressing direct playback")
                let wrongEncoding = TestHardware(); let encodingRoute = core(wrongEncoding)
                try encodingRoute.begin()
                wrongEncoding.corruptFormat(stream: 110)
                wrongEncoding.fire(AudioHALProperty(object: 110, selector: kAudioStreamPropertyVirtualFormat))
                check(encodingRoute.isStopped && !wrongEncoding.log.contains("tap.mutedWhenTapped"),
                      "In-place format changes are observed even when stream IDs, channel counts and rates remain unchanged")
                let startupRace = TestHardware(); let racingRoute = core(startupRace)
                try racingRoute.begin()
                startupRace.corruptFormat(stream: 110)
                PerAppAudioPCM.store(startupRace.context!, PerAppAudioPCM.signalSlot, 1)
                racingRoute.serviceHealth(now: ProcessInfo.processInfo.systemUptime)
                check(racingRoute.isStopped && !startupRace.log.contains("tap.mutedWhenTapped"),
                      "Formats are revalidated immediately before muting, closing the pre-listener startup race")
                let changedStreams = TestHardware(); let streamsRoute = core(changedStreams)
                try streamsRoute.begin()
                changedStreams.changePhysicalStreamID()
                PerAppAudioPCM.store(changedStreams.context!, PerAppAudioPCM.signalSlot, 1)
                streamsRoute.serviceHealth(now: ProcessInfo.processInfo.systemUptime)
                check(streamsRoute.isStopped && !changedStreams.log.contains("tap.mutedWhenTapped"),
                      "Pre-arm revalidation cannot silently adopt stream IDs that lack installed format listeners")
                let reversed = TestHardware(); reversed.reverseStereo()
                let reversedRoute = core(reversed)
                var stereoRejected = false
                do { try reversedRoute.begin() } catch { stereoRejected = true }
                check(stereoRejected && reversed.log.isEmpty, "Reversed hardware stereo mappings are refused before creating a tap")
                let restore = TestHardware(); let restoreRoute = core(restore)
                try restoreRoute.begin()
                restore.unmuteFailures = 2
                check(restoreRoute.stop() == nil && restore.log.dropFirst().filter { $0 == "tap.unmuted" }.count == 3,
                      "Transient unmute failures are retried before teardown")
                let persistent = TestHardware(); let persistentRoute = core(persistent)
                try persistentRoute.begin()
                persistent.unmuteFailures = 20
                check(persistentRoute.stop() != nil && persistentRoute.isStopped,
                      "Persistent unmute-property failure is reported even when destroying the tap safely restores playback")
                let armFailure = TestHardware(); let armRoute = core(armFailure)
                try armRoute.begin()
                armFailure.failAt = "arm"
                PerAppAudioPCM.store(armFailure.context!, PerAppAudioPCM.signalSlot, 1)
                armRoute.serviceHealth(now: ProcessInfo.processInfo.systemUptime)
                check(armRoute.isStopped && armFailure.log.contains("tap.unmuted"),
                      "A failed mute transition rolls back instead of leaving a half-enabled route")
                let stalled = TestHardware(); let stalledRoute = core(stalled)
                try stalledRoute.begin()
                PerAppAudioPCM.store(stalled.context!, PerAppAudioPCM.signalSlot, 1)
                stalledRoute.serviceHealth(now: ProcessInfo.processInfo.systemUptime)
                stalledRoute.serviceHealth(now: ProcessInfo.processInfo.systemUptime + 2)
                check(stalledRoute.isStopped && Array(stalled.log.suffix(5)).first == "tap.unmuted",
                      "A stalled active callback restores direct playback and dismantles its route")
                let deviceChange = TestHardware(); let deviceRoute = core(deviceChange)
                try deviceRoute.begin()
                deviceChange.changeDefault()
                deviceChange.fire(AudioHALProperty(object: 1, selector: kAudioHardwarePropertyDefaultOutputDevice))
                check(deviceRoute.isStopped, "Changing the default output stops the route instead of silently redirecting audio")
                let death = TestHardware(); let deathRoute = core(death)
                try deathRoute.begin()
                death.changePID()
                death.fire(AudioHALProperty(object: 1, selector: kAudioHardwarePropertyProcessObjectList))
                check(deathRoute.isStopped, "Process disappearance or HAL object reuse restores original playback and releases capture")
                let cleanup = TestHardware(); let cleanupRoute = core(cleanup)
                try cleanupRoute.begin()
                cleanup.failDestroy = true
                check(cleanupRoute.stop() != nil && !cleanupRoute.isStopped, "Failed resource removal retains state for a visible Retry Stop")
                cleanup.failDestroy = false
                check(cleanupRoute.stop() == nil && cleanupRoute.isStopped, "Retry Stop can finish a previously refused cleanup")

                let delayedReady = TestHardware(); delayedReady.readyAfterAliveReads = 2
                let delayedReadyRoute = core(delayedReady)
                try delayedReadyRoute.begin()
                check(delayedReady.aliveReadCount == 3 && delayedReady.log.contains("io.create"),
                      "Startup waits for aggregate liveness before creating a callback")
                _ = delayedReadyRoute.stop()
                let neverReady = TestHardware(); neverReady.readyAfterAliveReads = Int.max
                let neverReadyRoute = CorePerAppAudioRoute(application: app, output: output, hardware: neverReady,
                    readinessTimeout: 0, event: { _ in })
                var readinessRejected = false
                do { try neverReadyRoute.begin() } catch { readinessRejected = true }
                check(readinessRejected && neverReadyRoute.isStopped && !neverReady.log.contains("io.create")
                      && neverReady.log.contains("tap.destroy"),
                      "An aggregate readiness timeout cleans startup resources without starting audio")
                let readyCancellation = PerAppAudioCancellation()
                let cancelledReady = TestHardware(); cancelledReady.onAliveRead = { readyCancellation.cancel() }
                let cancelledReadyRoute = CorePerAppAudioRoute(application: app, output: output, hardware: cancelledReady,
                    cancellation: readyCancellation, event: { _ in })
                var readinessCancelled = false
                do { try cancelledReadyRoute.begin() } catch { readinessCancelled = true }
                check(readinessCancelled && cancelledReadyRoute.isStopped && !cancelledReady.log.contains("io.create"),
                      "Cancellation observed at readiness prevents the next blocking resource creation")

                func pump(until condition: () -> Bool, timeout: TimeInterval = 2) -> Bool {
                    let end = Date(timeIntervalSinceNow: timeout)
                    while !condition(), Date() < end {
                        _ = RunLoop.main.run(mode: .default, before: Date(timeIntervalSinceNow: 0.005))
                    }
                    return condition()
                }
                let pendingGainHardware = TestHardware(); pendingGainHardware.signalAtStart = true
                let armEntered = DispatchSemaphore(value: 0), releaseArm = DispatchSemaphore(value: 0)
                pendingGainHardware.armEntered = armEntered; pendingGainHardware.armGate = releaseArm
                var pendingBecameActive = false, firstForwardedSample: Float32?
                let pendingGainRoute = AsyncPerAppAudioRoute(event: { value in
                    if case .active = value { pendingBecameActive = true }
                }) { queue, cancellation, event in
                    CorePerAppAudioRoute(application: app, output: output, hardware: pendingGainHardware,
                        executionQueue: queue, cancellation: cancellation, chinese: false) { value in
                        if case .active = value, let context = pendingGainHardware.context {
                            let input = StereoBuffers(frames: 16, planar: false)
                            let output = StereoBuffers(frames: 16, planar: false)
                            input.fill(left: 0.8, right: 0.8)
                            _ = PerAppAudioPCM.render(input: input.list, output: output.list, state: context)
                            let sample = output.sample(frame: 0, channel: 0)
                            DispatchQueue.main.async { firstForwardedSample = sample }
                        }
                        event(value)
                    }
                }
                pendingGainRoute.setGain(0.8)
                try pendingGainRoute.begin()
                check(armEntered.wait(timeout: .now() + 1) == .success,
                      "The worker can prepare a route while its requested slider gain is pending")
                pendingGainRoute.setGain(0)
                releaseArm.signal()
                check(pump(until: { pendingBecameActive && firstForwardedSample != nil }) && firstForwardedSample == 0,
                      "A slider edit during blocked arming reaches the first output sample before queued gain work runs")
                _ = pendingGainRoute.stop()
                check(pump(until: { pendingGainRoute.isStopped }), "The pending-gain fixture releases its route normally")

                let startBlocked = TestHardware()
                let startEntered = DispatchSemaphore(value: 0), releaseStart = DispatchSemaphore(value: 0)
                startBlocked.startEntered = startEntered; startBlocked.startGate = releaseStart
                var starts = 0, failures = 0
                let asynchronous = AsyncPerAppAudioRoute(startTimeout: 0.08, stopTimeout: 0.08, event: { event in
                    if case .active = event { starts += 1 }
                    if case .failed = event { failures += 1 }
                }) { queue, cancellation, event in
                    CorePerAppAudioRoute(application: app, output: output, hardware: startBlocked,
                        executionQueue: queue, cancellation: cancellation, chinese: false, event: event)
                }
                try asynchronous.begin()
                check(startEntered.wait(timeout: .now() + 1) == .success && !startBlocked.didStartOnMain,
                      "AudioDeviceStart runs on a worker and begin returns while the driver is blocked")
                var mainHeartbeat = false
                DispatchQueue.main.async { mainHeartbeat = true }
                check(pump(until: { mainHeartbeat }), "The main run loop keeps processing UI work during blocked startup")
                check(pump(until: { asynchronous.isStopping && failures > 0 }) && !asynchronous.isStopped,
                      "A startup timeout cancels without claiming blocked HAL resources were released")
                check(starts == 0 && !startBlocked.log.contains("tap.mutedWhenTapped")
                      && !startBlocked.log.contains("tap.destroy"),
                      "Blocked startup never mutes the original app or races teardown against AudioDeviceStart")
                releaseStart.signal()
                check(pump(until: { asynchronous.isStopped }) && starts == 0,
                      "Late AudioDeviceStart completion finishes queued cancellation without becoming active")
                check(!startBlocked.log.contains("tap.mutedWhenTapped") && startBlocked.log.contains("tap.destroy"),
                      "Cancelled startup keeps the source unmuted and cleans resources on its worker")

                let stopBlocked = TestHardware()
                stopBlocked.signalAtStart = true
                let stopEntered = DispatchSemaphore(value: 0), releaseStop = DispatchSemaphore(value: 0)
                stopBlocked.stopEntered = stopEntered; stopBlocked.stopGate = releaseStop
                let deferredFactory = AsyncTestFactory(hardware: stopBlocked)
                let deferred = PerAppAudioController(factory: deferredFactory)
                check(deferred.start(application: app, output: output)
                      && pump(until: { deferred.sessions.first?.state == .active }),
                      "A worker route reports active only after the real core validates synthetic input")
                deferred.stop(processID: app.id)
                check(deferred.sessions.first?.state == .stopping
                      && stopEntered.wait(timeout: .now() + 1) == .success && !stopBlocked.didStopOnMain,
                      "Stop returns immediately with a stopping state while HAL cleanup waits off main")
                check(!deferred.setGain(0.2, processID: app.id)
                      && !deferred.start(application: app, output: output),
                      "A pending cleanup retains ownership and disables gain and replacement routes")
                check(pump(until: { deferred.sessions.first?.error != nil })
                      && deferred.sessions.first?.state == .stopping,
                      "A cleanup timeout remains visibly pending instead of reporting a false completed stop")
                releaseStop.signal()
                check(pump(until: { deferred.sessions.isEmpty }),
                      "The worker's eventual stopped event removes the pending session")

                let faultBlocked = TestHardware()
                faultBlocked.faultAtStart = true
                let faultStopEntered = DispatchSemaphore(value: 0), releaseFaultStop = DispatchSemaphore(value: 0)
                faultBlocked.stopEntered = faultStopEntered; faultBlocked.stopGate = releaseFaultStop
                let faultController = PerAppAudioController(factory: AsyncTestFactory(hardware: faultBlocked))
                _ = faultController.start(application: app, output: output)
                check(pump(until: { faultController.sessions.first?.state == .stopping })
                      && faultStopEntered.wait(timeout: .now() + 1) == .success,
                      "An internal PCM fault reports stopping before its HAL cleanup can block")
                check(pump(until: { faultController.sessions.first?.error?.contains(L10n.text("Waiting for macOS", "正在等待 macOS")) == true }),
                      "Blocked cleanup triggered by a core fault gets the independent main-thread deadline")
                releaseFaultStop.signal()
                check(pump(until: { faultController.sessions.isEmpty }),
                      "Internal-fault cleanup releases ownership only after HAL returns")

                let cancelled = PerAppAudioCancellation()
                cancelled.cancel()
                var armedAfterCancel = false
                check(!cancelled.performUnlessCancelled { armedAfterCancel = true } && !armedAfterCancel,
                      "The final renderer publication is atomic with respect to cancellation")
            } catch { fatalError("Injected per-app engine test failed: \(error)") }
        }
        return count
    }

    private final class TestRouteFactory: PerAppAudioRouteFactory {
        var isSupported = true
        var routes: [TestRoute] = []
        func make(application: AudioApplicationInfo, output: AudioDeviceInfo, event: @escaping (PerAppAudioRouteEvent) -> Void) throws -> PerAppAudioRoute {
            let route = TestRoute(event: event); routes.append(route); return route
        }
    }
    private final class TestRoute: PerAppAudioRoute {
        let callback: (PerAppAudioRouteEvent) -> Void
        var isStopped = true, cannotStop = false
        var isActive = false
        var gain = 1.0
        var gainWrites = 0
        init(event: @escaping (PerAppAudioRouteEvent) -> Void) { self.callback = event }
        func event(_ value: PerAppAudioRouteEvent) {
            if case .active = value { isActive = true }
            callback(value)
        }
        func begin() throws { isStopped = false }
        func setGain(_ value: Double) { gainWrites += 1; if isActive { gain = value } }
        func stop() -> String? { if cannotStop { return "Retry Stop" }; isStopped = true; return nil }
    }

    @available(macOS 14.2, *)
    private final class AsyncTestFactory: PerAppAudioRouteFactory {
        let isSupported = true
        let hardware: TestHardware
        init(hardware: TestHardware) { self.hardware = hardware }
        func make(application: AudioApplicationInfo, output: AudioDeviceInfo, event: @escaping (PerAppAudioRouteEvent) -> Void) throws -> PerAppAudioRoute {
            AsyncPerAppAudioRoute(startTimeout: 1, stopTimeout: 0.08, event: event) { [hardware] queue, cancellation, event in
                CorePerAppAudioRoute(application: application, output: output, hardware: hardware,
                    executionQueue: queue, cancellation: cancellation, chinese: false, event: event)
            }
        }
    }

    private final class StereoBuffers {
        let list: UnsafeMutablePointer<AudioBufferList>
        let planes: [UnsafeMutablePointer<Float32>]
        let planar: Bool
        init(frames: Int, planar: Bool) {
            self.planar = planar
            let count = planar ? 2 : 1
            let bytes = (MemoryLayout<AudioBufferList>.offset(of: \.mBuffers) ?? 8) + count * MemoryLayout<AudioBuffer>.stride
            list = UnsafeMutableRawPointer.allocate(byteCount: bytes, alignment: MemoryLayout<AudioBufferList>.alignment).assumingMemoryBound(to: AudioBufferList.self)
            list.pointee.mNumberBuffers = UInt32(count)
            planes = (0..<count).map { _ in
                let plane = UnsafeMutablePointer<Float32>.allocate(capacity: frames * (planar ? 1 : 2))
                plane.initialize(repeating: 0, count: frames * (planar ? 1 : 2)); return plane
            }
            let buffers = UnsafeMutableAudioBufferListPointer(list)
            for index in 0..<count { buffers[index] = AudioBuffer(mNumberChannels: planar ? 1 : 2, mDataByteSize: UInt32(frames * (planar ? 4 : 8)), mData: planes[index]) }
        }
        func fill(left: Float32, right: Float32) {
            let first = UnsafeMutableAudioBufferListPointer(list)[0]
            let frames = Int(first.mDataByteSize / (first.mNumberChannels * 4))
            for frame in 0..<frames { set(frame: frame, channel: 0, value: left); set(frame: frame, channel: 1, value: right) }
        }
        func set(frame: Int, channel: Int, value: Float32) { planes[planar ? channel : 0][planar ? frame : frame * 2 + channel] = value }
        func sample(frame: Int, channel: Int) -> Float32 { planes[planar ? channel : 0][planar ? frame : frame * 2 + channel] }
        deinit { planes.forEach { $0.deallocate() }; UnsafeMutableRawPointer(list).deallocate() }
    }

    @available(macOS 14.2, *)
    private final class TestHardware: PerAppAudioHardware, AudioHALBackend {
        var metadata: AudioHALBackend { self }
        let supportsProcessActivity = true
        var log: [String] = [], failAt: String?
        var context: UnsafeMutablePointer<PerAppAudioRenderState>?
        var tapProcesses: [UInt32] = []
        var privateTap = false, exclusive = true, privateAggregate = false
        var stackedAggregate = false, tapAutoStart = false, hasChannelOverrides = false
        var subdeviceCount = 0, metadataWrites = 0, unmuteFailures = 0
        var aliveReadCount = 0, readyAfterAliveReads = 0
        var onAliveRead: (() -> Void)?
        var failDestroy = false
        var startEntered: DispatchSemaphore?, startGate: DispatchSemaphore?
        var stopEntered: DispatchSemaphore?, stopGate: DispatchSemaphore?
        var armEntered: DispatchSemaphore?, armGate: DispatchSemaphore?
        var didStartOnMain = false, didStopOnMain = false, signalAtStart = false, faultAtStart = false
        var data: [AudioHALProperty: Data] = [:]
        var callbacks: [UUID: (AudioHALProperty, () -> Void)] = [:]
        var removed: [() -> Void] = []
        init() {
            put(p(1, kAudioHardwarePropertyDefaultOutputDevice), UInt32(10))
            put(p(10, kAudioDevicePropertyDeviceIsAlive), UInt32(1))
            put(p(10, kAudioDevicePropertyTransportType), UInt32(kAudioDeviceTransportTypeBuiltIn))
            put(p(10, kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeInput), buffers(0))
            put(p(10, kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput), buffers(2))
            putArray(p(10, kAudioDevicePropertyPreferredChannelsForStereo, kAudioDevicePropertyScopeOutput), [1, 2])
            put(p(7, kAudioProcessPropertyPID), Int32(1001))
            putArray(p(7, kAudioProcessPropertyDevices, kAudioDevicePropertyScopeOutput), [10])
            putArray(p(10, kAudioDevicePropertyStreams, kAudioDevicePropertyScopeOutput), [110])
            putArray(p(200, kAudioDevicePropertyStreams, kAudioDevicePropertyScopeInput), [210])
            putArray(p(200, kAudioDevicePropertyStreams, kAudioDevicePropertyScopeOutput), [220])
            put(p(200, kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeInput), buffers(2))
            put(p(200, kAudioDevicePropertyStreamConfiguration, kAudioDevicePropertyScopeOutput), buffers(2))
            put(p(200, kAudioDevicePropertyBufferFrameSize), UInt32(256))
            put(p(100, kAudioTapPropertyFormat), format())
            for id: UInt32 in [110, 210, 220] { put(p(id, kAudioStreamPropertyVirtualFormat), format()) }
        }
        func createTap(_ description: CATapDescription, id: inout AudioObjectID) -> OSStatus {
            log.append("tap.unmuted")
            tapProcesses = description.processes; privateTap = description.isPrivate; exclusive = description.isExclusive
            if failAt == "tap" { return -1 }; id = 100; return noErr
        }
        func destroyTap(_ id: AudioObjectID) -> OSStatus { log.append("tap.destroy"); return failDestroy ? -1 : noErr }
        func createAggregate(_ description: CFDictionary, id: inout AudioObjectID) -> OSStatus {
            log.append("aggregate")
            let value = description as NSDictionary
            privateAggregate = value[kAudioAggregateDeviceIsPrivateKey] as? Bool == true
            stackedAggregate = value[kAudioAggregateDeviceIsStackedKey] as? Bool == true
            tapAutoStart = value[kAudioAggregateDeviceTapAutoStartKey] as? Bool == true
            let subdevices = value[kAudioAggregateDeviceSubDeviceListKey] as? [[String: Any]] ?? []
            subdeviceCount = subdevices.count
            hasChannelOverrides = subdevices.contains { $0[kAudioSubDeviceInputChannelsKey] != nil || $0[kAudioSubDeviceOutputChannelsKey] != nil }
            if failAt == "aggregate" { return -1 }; id = 200; return noErr
        }
        func destroyAggregate(_ id: AudioObjectID) -> OSStatus { log.append("aggregate.destroy"); if !failDestroy { context = nil }; return failDestroy ? -1 : noErr }
        func createIO(_ device: AudioObjectID, context: UnsafeMutablePointer<PerAppAudioRenderState>?, id: inout AudioDeviceIOProcID?) -> OSStatus {
            log.append("io.create"); if failAt == "io.create" { return -1 }; self.context = context; id = perAppAudioIOProc; return noErr
        }
        func destroyIO(_ device: AudioObjectID, id: AudioDeviceIOProcID) -> OSStatus { log.append("io.destroy"); return failDestroy ? -1 : noErr }
        func startIO(_ device: AudioObjectID, id: AudioDeviceIOProcID) -> OSStatus {
            log.append("io.start"); didStartOnMain = Thread.isMainThread
            startEntered?.signal(); startGate?.wait()
            if signalAtStart, let context { PerAppAudioPCM.store(context, PerAppAudioPCM.signalSlot, 1) }
            if faultAtStart, let context { PerAppAudioPCM.store(context, PerAppAudioPCM.faultSlot, 1) }
            return failAt == "io.start" ? -1 : noErr
        }
        func stopIO(_ device: AudioObjectID, id: AudioDeviceIOProcID) -> OSStatus {
            log.append("io.stop"); didStopOnMain = Thread.isMainThread
            stopEntered?.signal(); stopGate?.wait()
            return failDestroy ? -1 : noErr
        }
        func setDescription(_ device: AudioObjectID, description: CATapDescription) -> OSStatus {
            let unmuted = description.muteBehavior == .unmuted
            log.append(unmuted ? "tap.unmuted" : "tap.mutedWhenTapped")
            if !unmuted { armEntered?.signal(); armGate?.wait() }
            if !unmuted, failAt == "arm" { return -1 }
            if unmuted, unmuteFailures > 0 { unmuteFailures -= 1; return -1 }; return noErr
        }
        func has(_ property: AudioHALProperty) -> Bool { true }
        func writable(_ property: AudioHALProperty) -> Bool { property.selector == kAudioTapPropertyDescription }
        func read(_ property: AudioHALProperty) throws -> Data {
            if property.object == 200, property.selector == kAudioDevicePropertyDeviceIsAlive {
                aliveReadCount += 1; onAliveRead?()
                var alive: UInt32 = aliveReadCount > readyAfterAliveReads ? 1 : 0
                return withUnsafeBytes(of: &alive) { Data($0) }
            }
            guard let value = data[property] else { throw AudioDeviceError.unavailable }; return value
        }
        func string(_ property: AudioHALProperty) throws -> String { "test-output" }
        func sourceName(device: UInt32, source: UInt32, scope: UInt32) throws -> String { "" }
        func write(_ property: AudioHALProperty, data: Data) throws { metadataWrites += 1; throw AudioDeviceError.unsupported }
        func listen(_ property: AudioHALProperty, changed: @escaping () -> Void) throws -> UUID { let id = UUID(); callbacks[id] = (property, changed); return id }
        func removeListener(_ token: UUID) { if let callback = callbacks.removeValue(forKey: token) { removed.append(callback.1) } }
        func fire(_ property: AudioHALProperty) { Array(callbacks.values).filter { $0.0 == property }.forEach { $0.1() } }
        func fireRemoved() { removed.forEach { $0() } }
        func corruptFormat(stream: UInt32) { var changed = format(); changed.mFormatFlags = kAudioFormatFlagIsSignedInteger; put(p(stream, kAudioStreamPropertyVirtualFormat), changed) }
        func changePhysicalStreamID() { putArray(p(10, kAudioDevicePropertyStreams, kAudioDevicePropertyScopeOutput), [111]); put(p(111, kAudioStreamPropertyVirtualFormat), format()) }
        func reverseStereo() { putArray(p(10, kAudioDevicePropertyPreferredChannelsForStereo, kAudioDevicePropertyScopeOutput), [2, 1]) }
        func changeDefault() { put(p(1, kAudioHardwarePropertyDefaultOutputDevice), UInt32(20)) }
        func changePID() { put(p(7, kAudioProcessPropertyPID), Int32(9009)) }
        private func p(_ id: UInt32, _ selector: UInt32, _ scope: UInt32 = kAudioObjectPropertyScopeGlobal) -> AudioHALProperty { AudioHALProperty(object: id, selector: selector, scope: scope) }
        private func put<T>(_ property: AudioHALProperty, _ value: T) {
            if let value = value as? Data { data[property] = value }
            else { var value = value; data[property] = withUnsafeBytes(of: &value) { Data($0) } }
        }
        private func putArray(_ property: AudioHALProperty, _ value: [UInt32]) { data[property] = value.withUnsafeBytes { Data($0) } }
        private func buffers(_ channels: UInt32) -> Data {
            var list = AudioBufferList(mNumberBuffers: channels == 0 ? 0 : 1, mBuffers: AudioBuffer(mNumberChannels: channels, mDataByteSize: 0, mData: nil))
            return withUnsafeBytes(of: &list) { Data($0) }
        }
        private func format() -> AudioStreamBasicDescription {
            AudioStreamBasicDescription(mSampleRate: 48_000, mFormatID: kAudioFormatLinearPCM,
                mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, mBytesPerPacket: 8,
                mFramesPerPacket: 1, mBytesPerFrame: 8, mChannelsPerFrame: 2, mBitsPerChannel: 32, mReserved: 0)
        }
    }
}
