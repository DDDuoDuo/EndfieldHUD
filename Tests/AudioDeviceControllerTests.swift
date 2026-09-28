import AppKit
import CoreAudio

enum AudioDeviceControllerTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func close(_ actual: Double?, _ expected: Double) -> Bool { actual.map { abs($0 - expected) < 0.00001 } ?? false }
        let metadataDirectory = FileManager.default.temporaryDirectory.appendingPathComponent("AudioApplicationMetadata-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: metadataDirectory) }
        do {
            func app(_ path: String, _ name: String, package: String = "APPL") throws -> URL {
                let url = metadataDirectory.appendingPathComponent(path, isDirectory: true)
                try FileManager.default.createDirectory(at: url.appendingPathComponent("Contents"), withIntermediateDirectories: true)
                let plist = ["CFBundlePackageType": package, "CFBundleIdentifier": "test.\(UUID().uuidString)",
                             "CFBundleName": "Fallback", "CFBundleDisplayName": name]
                try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
                    .write(to: url.appendingPathComponent("Contents/Info.plist"))
                return url
            }
            let parent = try app("哔哩哔哩.app", "哔哩哔哩")
            let helper = try app("哔哩哔哩.app/Contents/Frameworks/哔哩哔哩 Helper.app", "哔哩哔哩 Helper")
            let anotherHelper = try app("哔哩哔哩.app/Contents/Frameworks/Renderer Helper.app", "Renderer Helper")
            let standalone = try app("Independent Helper.app", "Independent Helper")
            let fakeApp = try app("NotAnApplication.app", "Wrong", package: "BNDL")
            let image = NSImage(size: NSSize(width: 32, height: 32))
            var iconReads: [URL] = []
            let resolver = AudioApplicationAppearanceResolver { url in iconReads.append(url); return image }
            let nested = resolver.resolve(bundleURL: helper, executableURL: nil)
            check(nested.name == "哔哩哔哩" && nested.applicationURL == parent && nested.icon === image,
                  "A nested helper uses its enclosing application's display name and actual icon")
            let executable = helper.appendingPathComponent("Contents/MacOS/Helper")
            let fromExecutable = resolver.resolve(bundleURL: nil, executableURL: executable)
            check(fromExecutable.name == nested.name && fromExecutable.applicationURL == parent,
                  "An executable URL resolves the enclosing application when process bundle metadata is absent")
            _ = resolver.resolve(bundleURL: helper, executableURL: nil)
            let sibling = resolver.resolve(bundleURL: anotherHelper, executableURL: nil)
            check(sibling.name == "哔哩哔哩" && iconReads == [parent],
                  "Multiple helper paths share cached parent-app metadata and load its icon only once")
            let independent = resolver.resolve(bundleURL: standalone, executableURL: nil)
            check(independent.name == "Independent Helper" && independent.applicationURL == standalone,
                  "A real standalone app keeps Helper in its name instead of applying a guessed suffix rule")
            check(resolver.resolve(bundleURL: fakeApp, executableURL: nil).applicationURL == nil,
                  "A non-application bundle with an app suffix is not presented as a parent application")
            check(resolver.resolve(bundleURL: URL(string: "https://example.invalid/App.app")!, executableURL: nil).icon == nil,
                  "Remote metadata URLs are ignored without loading icons or network resources")
            check(resolver.resolve(bundleURL: nil, executableURL: metadataDirectory.appendingPathComponent("cli-tool")).name == nil,
                  "Unbundled audio processes leave their truthful name/PID fallback intact")
            let noIcon = AudioApplicationAppearanceResolver { _ in nil }.resolve(bundleURL: parent, executableURL: nil)
            check(noIcon.name == "哔哩哔哩" && noIcon.icon == nil,
                  "Missing icon data never prevents a valid friendly application name")
        } catch { fatalError("Metadata fixture failed: \(error)") }
        let backend = FakeAudioHAL()
        backend.installDevices()
        let controller = AudioDeviceController(backend: backend)
        var notifications = 0
        let observer = controller.observe { notifications += 1 }
        controller.refresh()
        check(backend.writes.isEmpty && backend.listeners.isEmpty, "Read-only refresh never writes hardware or installs hidden listeners")
        check(controller.snapshot.outputs.map(\.id) == [10, 20, 40], "Only alive devices with output channels are shown")
        check(controller.snapshot.inputs.map(\.id) == [30], "Input streams determine input-device membership")
        check(controller.snapshot.defaultOutputID == 10 && controller.snapshot.defaultInputID == 30, "Actual system defaults are reported")
        check(close(controller.snapshot.outputVolume, 0.6) && controller.snapshot.canSetOutputVolume, "Writable main volume is exposed")
        check(controller.snapshot.outputMuted == false && controller.snapshot.canSetOutputMute, "Main mute capability is derived from the hardware")
        check(close(controller.snapshot.balance, 0) && controller.snapshot.canSetBalance, "Native pan is mapped to a centered minus-one-to-one balance")
        check(controller.snapshot.outputs.first { $0.id == 20 }?.isHeadphones == true, "A headphone terminal with a connected jack is confirmed")
        check(controller.snapshot.outputs.first { $0.id == 40 }?.isBluetooth == true
              && controller.snapshot.outputs.first { $0.id == 40 }?.isHeadphones == false,
              "Bluetooth alone never claims that an unknown output is headphones")
        check(controller.snapshot.activeApplications.count == 1 && controller.snapshot.activeApplications[0].id == 100,
              "The app list contains running Core Audio IO processes only, excluding idle HAL clients")
        check(controller.snapshot.availableApplications.map(\.id) == [100]
              && controller.snapshot.availableApplications.first?.outputDeviceIDs == [10],
              "Output capability metadata identifies live output processes independently from activity")
        check(controller.snapshot.outputs.first(where: { $0.id == 10 })?.transportType == kAudioDeviceTransportTypeBuiltIn
              && controller.snapshot.outputs.first(where: { $0.id == 20 })?.transportType == kAudioDeviceTransportTypeUSB,
              "Public transport metadata distinguishes supported physical device families")
        let processBundle = p(100, kAudioProcessPropertyBundleID)
        backend.strings[processBundle] = ""
        controller.refresh()
        check(controller.snapshot.activeApplications.first?.name == "PID 500100"
              && controller.snapshot.activeApplications.first?.bundleIdentifier == nil,
              "A command-line audio process with an empty bundle ID remains identifiable by PID")
        backend.strings[processBundle] = " \t\n "
        controller.refresh()
        check(controller.snapshot.activeApplications.first?.name == "PID 500100"
              && controller.snapshot.activeApplications.first?.bundleIdentifier == nil,
              "Whitespace-only process metadata cannot create a blank application row")
        backend.strings[processBundle] = "  test.audio.100\n"
        controller.refresh()
        check(controller.snapshot.activeApplications.first?.name == "test.audio.100"
              && controller.snapshot.activeApplications.first?.bundleIdentifier == "test.audio.100",
              "A usable bundle identifier remains the process-name fallback after trimming surrounding whitespace")
        backend.strings.removeValue(forKey: processBundle)
        controller.refresh()
        check(controller.snapshot.activeApplications.first?.name == "PID 500100" && backend.writes.isEmpty,
              "Missing process metadata falls back to PID without any audio writes")
        backend.strings[processBundle] = "test.audio.100"
        controller.refresh()
        let beforeStart = notifications
        controller.start()
        let listenerCount = backend.listeners.count
        check(controller.isRunning && listenerCount > 0, "Visible activation installs event listeners")
        controller.start()
        check(backend.listeners.count == listenerCount && notifications == beforeStart, "Repeated start does not duplicate listeners or publish unchanged snapshots")
        backend.put(p(20, kAudioDevicePropertyJackIsConnected, output), UInt32(0))
        backend.emit()
        backend.emit()
        let readsBeforeDrain = backend.reads
        drainMainQueue()
        let oneRefreshReads = backend.reads - readsBeforeDrain
        check(oneRefreshReads > 0 && oneRefreshReads < 150, "A burst of property events coalesces into one bounded refresh")
        check(controller.snapshot.outputs.first { $0.id == 20 }?.isHeadphones == false, "A disconnected headphone jack is not listed as connected")
        controller.stop()
        check(!controller.isRunning && backend.listeners.isEmpty, "Closing the module removes every property listener")
        let stoppedReads = backend.reads
        backend.emitRemovedCallbacks()
        drainMainQueue()
        check(backend.reads == stoppedReads, "Already-queued old callbacks cannot read audio hardware after stop")
        controller.start(); backend.emit(); controller.stop()
        let pendingStoppedReads = backend.reads
        drainMainQueue()
        check(backend.reads == pendingStoppedReads, "Stopping cancels a refresh that was already queued before teardown")

        check(controller.setOutputVolume(3) && close(controller.snapshot.outputVolume, 1), "Finite volume values clamp to one")
        check(controller.setOutputVolume(-2) && close(controller.snapshot.outputVolume, 0), "Finite volume values clamp to zero")
        let finiteWrites = backend.writes.count
        check(!controller.setOutputVolume(.nan) && !controller.setBalance(.infinity) && backend.writes.count == finiteWrites,
              "Nonfinite controls are rejected before any HAL write")
        check(controller.setOutputMuted(true) && controller.snapshot.outputMuted == true, "Mute writes the default output's actual scope and reads it back")
        check(controller.setBalance(-1) && close(controller.snapshot.balance, -1), "Native pan supports full-left balance")
        check(controller.setDefaultOutput(20) && controller.snapshot.defaultOutputID == 20, "Output selection updates only the public default-output property")
        check(!controller.setDefaultInput(20) && controller.snapshot.defaultInputID == 30, "An output-only device cannot become the input")
        let afterInvalid = backend.writes.count
        check(!controller.setDefaultOutput(999) && backend.writes.count == afterInvalid, "Stale or unknown device IDs never produce writes")
        check(controller.snapshot.outputVolume == nil && !controller.snapshot.canSetOutputVolume,
              "Digital or fixed-level devices show unavailable volume rather than fake zero")
        check(!controller.setOutputVolume(0.4), "Unsupported digital-device volume writes are rejected")

        backend.put(system(kAudioHardwarePropertyDefaultOutputDevice), UInt32(10))
        controller.refresh()
        backend.put(system(kAudioHardwarePropertyDefaultOutputDevice), UInt32(20))
        let beforeRace = backend.writes.count
        check(!controller.setOutputVolume(0.7) && backend.writes.count == beforeRace && controller.snapshot.defaultOutputID == 20,
              "A changed default device is refreshed and never accidentally receives a stale volume gesture")
        backend.put(system(kAudioHardwarePropertyDefaultOutputDevice), UInt32(10))
        controller.refresh()
        backend.ignoreWrites.insert(system(kAudioHardwarePropertyDefaultOutputDevice))
        check(!controller.setDefaultOutput(20) && controller.snapshot.defaultOutputID == 10 && controller.statusMessage != nil,
              "A device-default change that fails readback is reported")
        backend.ignoreWrites.removeAll()

        // Remove native main controls to exercise actual channel fallback.
        backend.data.removeValue(forKey: p(10, kAudioDevicePropertyVolumeScalar, output))
        backend.data.removeValue(forKey: p(10, kAudioDevicePropertyStereoPan, output))
        backend.data.removeValue(forKey: p(10, kAudioDevicePropertyMute, output))
        let left = p(10, kAudioDevicePropertyVolumeScalar, output, 1)
        let right = p(10, kAudioDevicePropertyVolumeScalar, output, 2)
        backend.put(left, Float32(0.4), writable: true); backend.put(right, Float32(0.8), writable: true)
        backend.put(p(10, kAudioDevicePropertyMute, output, 1), UInt32(0), writable: true)
        backend.put(p(10, kAudioDevicePropertyMute, output, 2), UInt32(1), writable: true)
        controller.refresh()
        check(close(controller.snapshot.outputVolume, 0.8) && close(controller.snapshot.balance, 0.5), "Channel fallback reports peak volume and actual stereo balance")
        check(controller.setOutputVolume(0.6) && close(backend.float(left), 0.3) && close(backend.float(right), 0.6),
              "Master volume via channel fallback preserves the existing stereo ratio")
        check(controller.setBalance(-0.5) && close(backend.float(left), 0.6) && close(backend.float(right), 0.3),
              "Balance changes preserve peak level and do not turn into an unrelated global gain change")
        check(controller.setOutputMuted(true) && controller.snapshot.outputMuted == true,
              "When no main mute exists, complete writable per-channel mute controls are supported")
        check(controller.setOutputVolume(0) && controller.snapshot.balance == nil && !controller.snapshot.canSetBalance,
              "Zeroed channel pairs have no observable balance and do not offer a misleading balance control")
        let zeroWrites = backend.writes.count
        check(!controller.setBalance(0.5) && backend.writes.count == zeroWrites, "Changing channel balance at zero is explicitly unavailable")
        check(controller.setOutputVolume(0.8) && close(backend.float(left), 0.8) && close(backend.float(right), 0.4),
              "Raising volume from zero restores the previously observed channel ratio")

        let oldLeft = backend.data[left], oldRight = backend.data[right]
        backend.failOnceOnWrite = right
        check(!controller.setOutputVolume(0.5), "A second-channel write failure is reported")
        check(backend.data[left] == oldLeft && backend.data[right] == oldRight,
              "Partial channel writes roll back both attempted channels to their previous values")
        backend.failReadAfterWrite = left
        check(!controller.setOutputVolume(0.5) && backend.data[left] == oldLeft,
              "A readback error also rolls back the attempted volume change")
        backend.failReadAfterWrite = nil
        backend.writableProperties.remove(right)
        controller.refresh()
        check(!controller.snapshot.canSetOutputVolume && !controller.snapshot.canSetBalance,
              "Incomplete writable channel controls disable the whole-device operation")
        backend.writableProperties.insert(right)
        backend.put(p(10, kAudioDevicePropertyStreamConfiguration, output), buffers(6))
        controller.refresh()
        check(!controller.snapshot.canSetBalance && controller.snapshot.balance == nil,
              "Stereo fallback never adjusts two arbitrary channels on a multichannel surround device")

        backend.supportsProcessActivity = false
        controller.refresh()
        check(!controller.snapshot.applicationActivitySupported && controller.snapshot.applicationActivityMessage != nil
              && controller.snapshot.activeApplications.isEmpty,
              "Older macOS explicitly marks active-audio app metadata unavailable")
        backend.supportsProcessActivity = true
        backend.data.removeValue(forKey: system(kAudioHardwarePropertyProcessObjectList))
        controller.refresh()
        check(!controller.snapshot.applicationActivitySupported, "Runtime property availability is checked in addition to the OS gate")
        backend.data.removeValue(forKey: system(kAudioHardwarePropertyDevices))
        controller.refresh()
        check(controller.snapshot.outputs.isEmpty && controller.snapshot.outputVolume == nil && controller.statusMessage != nil,
              "Device enumeration failures cannot leave apparently live stale controls")
        controller.removeObserver(observer)
        let beforeUnobserved = notifications
        controller.refresh()
        check(notifications == beforeUnobserved, "Removed observers receive no further state changes")

        let fixture = AudioDeviceController.fixture()
        fixture.start()
        check(fixture.setOutputVolume(0.25) && close(fixture.snapshot.outputVolume, 0.25)
              && fixture.setDefaultOutput(20) && fixture.snapshot.defaultOutputID == 20,
              "Diagnostic fixtures mutate only their in-memory snapshots")
        fixture.stop()
        for step in -10...10 {
            let balance = Double(step) / 10
            let levels = AudioVolumeMath.stereo(volume: 0.7, balance: balance)
            check(abs(AudioVolumeMath.balance(left: levels[0], right: levels[1]) - balance) < 0.000001,
                  "Stereo math round-trips every bounded balance without changing peak volume")
        }
        let privateBackend = FakeAudioHAL()
        privateBackend.installDevices()
        privateBackend.data[system(kAudioHardwarePropertyDevices)] = [UInt32(10), 60].withUnsafeBytes { Data($0) }
        privateBackend.strings[p(60, kAudioDevicePropertyDeviceUID)] = "EndfieldCharge.AppAudio.test-route"
        privateBackend.put(p(60, kAudioDevicePropertyDeviceIsAlive), UInt32(1))
        privateBackend.put(p(60, kAudioDevicePropertyStreamConfiguration, output), buffers(2))
        let privateController = AudioDeviceController(backend: privateBackend)
        privateController.start()
        check(privateController.snapshot.outputs.map(\.id) == [10]
              && !privateBackend.readAddresses.contains(p(60, kAudioDevicePropertyStreamConfiguration, output)),
              "Owned private app aggregates are excluded before querying their stream layout")
        check(!privateController.setDefaultOutput(60), "An internal app route cannot become the user's selected output")
        privateController.stop()

        let processBackend = FakeAudioHAL()
        processBackend.installDevices()
        let processes = AudioDeviceController(backend: processBackend)
        processes.refresh()
        processBackend.put(p(100, kAudioProcessPropertyIsRunningOutput), UInt32(0))
        processBackend.data[p(100, kAudioProcessPropertyDevices, output)] = Data()
        processes.refresh()
        check(processes.snapshot.activeApplications.isEmpty
              && processes.snapshot.availableApplications.first?.id == 100
              && processes.snapshot.availableApplications.first?.isRunningOutput == false
              && processes.snapshot.availableApplications.first?.outputDeviceIDs == [10],
              "Previously observed output processes stay available while idle without faking active playback")
        processes.start(); processes.stop(); processes.start()
        check(processes.snapshot.availableApplications.map(\.id) == [100],
              "Closing and reopening Volume retains output capability for the same live process")
        processes.stop()
        processBackend.data.removeValue(forKey: p(100, kAudioProcessPropertyDevices, output))
        processes.refresh()
        check(processes.snapshot.availableApplications.first?.outputDeviceIDs == [10],
              "A temporarily unavailable output-device query does not remove a known idle process")
        processBackend.data[p(101, kAudioProcessPropertyDevices, output)] = bytes(UInt32(20))
        processes.refresh()
        check(processes.snapshot.availableApplications.map(\.id) == [100, 101]
              && processes.snapshot.activeApplications.isEmpty,
              "An idle process with a reported output device is available before it starts playing")
        processBackend.data[system(kAudioHardwarePropertyProcessObjectList)] = [UInt32(100), 101, 102, 103].withUnsafeBytes { Data($0) }
        processBackend.put(p(102, kAudioProcessPropertyPID), UInt32(500_102))
        processBackend.put(p(102, kAudioProcessPropertyIsRunningOutput), UInt32(0))
        processBackend.put(p(102, kAudioProcessPropertyIsRunningInput), UInt32(1))
        processBackend.data[p(102, kAudioProcessPropertyDevices, output)] = Data()
        processBackend.put(p(103, kAudioProcessPropertyPID), UInt32(ProcessInfo.processInfo.processIdentifier))
        processBackend.put(p(103, kAudioProcessPropertyIsRunningOutput), UInt32(1))
        processBackend.put(p(103, kAudioProcessPropertyIsRunningInput), UInt32(0))
        processBackend.data[p(103, kAudioProcessPropertyDevices, output)] = bytes(UInt32(10))
        processes.refresh()
        check(processes.snapshot.availableApplications.map(\.id) == [100, 101]
              && Set(processes.snapshot.activeApplications.map(\.id)) == Set([UInt32(102), 103]),
              "The available list excludes self and input-only clients while activity metadata remains accurate")
        processBackend.data.removeValue(forKey: p(100, kAudioProcessPropertyPID))
        processes.refresh()
        check(!processes.snapshot.availableApplications.contains(where: { $0.id == 100 }),
              "A process whose current identity cannot be read is not exposed for adjustment")
        processBackend.put(p(100, kAudioProcessPropertyPID), UInt32(500_100))
        processes.refresh()
        check(processes.snapshot.availableApplications.first(where: { $0.id == 100 })?.outputDeviceIDs == [10],
              "A recovered matching PID can reuse its confirmed output facts after a transient metadata failure")
        processBackend.put(p(100, kAudioProcessPropertyPID), UInt32(600_100))
        processes.refresh()
        check(!processes.snapshot.availableApplications.contains(where: { $0.id == 100 }),
              "A reused Core Audio process object cannot inherit another PID's output capability")
        processBackend.data[p(100, kAudioProcessPropertyDevices, output)] = [UInt32(20), 20, 0].withUnsafeBytes { Data($0) }
        processes.refresh()
        check(processes.snapshot.availableApplications.first(where: { $0.id == 100 })?.outputDeviceIDs == [20],
              "A new process can establish its own route, with duplicate and unknown-zero IDs normalized")
        processBackend.data[system(kAudioHardwarePropertyProcessObjectList)] = [UInt32(101), 102, 103].withUnsafeBytes { Data($0) }
        processes.refresh()
        check(!processes.snapshot.availableApplications.contains(where: { $0.id == 100 }),
              "Terminated process objects are pruned from the available list")
        processBackend.data[system(kAudioHardwarePropertyProcessObjectList)] = [UInt32(100), 101].withUnsafeBytes { Data($0) }
        processBackend.data[p(100, kAudioProcessPropertyDevices, output)] = Data()
        processes.refresh()
        check(!processes.snapshot.availableApplications.contains(where: { $0.id == 100 }),
              "Reappearing object IDs do not resurrect capability facts pruned after exit")
        processBackend.data[p(100, kAudioProcessPropertyDevices, output)] = bytes(UInt32(10))
        processes.refresh()
        processBackend.data[p(100, kAudioProcessPropertyDevices, output)] = Data()
        processBackend.strings[p(100, kAudioProcessPropertyBundleID)] = "test.replaced.bundle"
        processes.refresh()
        check(!processes.snapshot.availableApplications.contains(where: { $0.id == 100 }) && processBackend.writes.isEmpty,
              "Changed process identity discards remembered routing without querying or starting audio capture")

        // Exercise the production executor with a deliberately blocking fake
        // HAL. Every call remains in memory; no host audio device is queried.
        func waitUntil(_ predicate: () -> Bool, timeout: TimeInterval = 2) -> Bool {
            let deadline = ProcessInfo.processInfo.systemUptime + timeout
            while !predicate() && ProcessInfo.processInfo.systemUptime < deadline {
                RunLoop.main.run(until: Date().addingTimeInterval(0.002))
            }
            return predicate()
        }
        let audit = BlockingHALAudit()
        let firstRead = audit.blockNext(.read, property: p(10, kAudioDevicePropertyStreamConfiguration, output))
        var asynchronous: AudioDeviceController? = AudioDeviceController(asynchronousBackend: { queue in
            audit.setQueue(queue); return BlockingAudioHAL(audit: audit)
        })
        var asyncNotifications = 0
        var observersStayOnMain = true
        _ = asynchronous!.observe { asyncNotifications += 1; observersStayOnMain = observersStayOnMain && Thread.isMainThread }
        let startTime = ProcessInfo.processInfo.systemUptime
        asynchronous!.start()
        check(ProcessInfo.processInfo.systemUptime - startTime < 0.2 && asynchronous!.isRunning,
              "Production activation returns immediately instead of waiting for metadata HAL calls")
        check(firstRead.entered.wait(timeout: .now() + 2) == .success && asynchronous!.snapshot.outputs.isEmpty,
              "A blocked HAL read runs on the worker without publishing a partial snapshot")
        var mainHeartbeat = false
        DispatchQueue.main.async { mainHeartbeat = true }
        check(waitUntil({ mainHeartbeat }), "The main event queue continues while a device stream query is blocked")
        let readsAtStop = audit.readCount
        let stopTime = ProcessInfo.processInfo.systemUptime
        asynchronous!.stop()
        check(ProcessInfo.processInfo.systemUptime - stopTime < 0.2 && !asynchronous!.isRunning,
              "Stopping a blocked production refresh never waits for HAL teardown")
        firstRead.release.signal()
        let stoppedFence = audit.enqueueFence()
        check(waitUntil({ stoppedFence.value }) && audit.readCount == readsAtStop && asyncNotifications == 0,
              "Cancellation prevents subsequent HAL reads and drops the blocked activation's late snapshot")
        check(!asynchronous!.setOutputVolume(0.5), "Stopped production controls reject stale writes immediately")
        asynchronous!.start()
        check(waitUntil({ asynchronous!.snapshot.defaultOutputID == 10 && audit.listenerCount > 0 }),
              "A fresh activation observes devices after the previous blocked refresh finishes")
        check(audit.mainThreadAccesses == 0 && observersStayOnMain,
              "Backend construction and every HAL operation stay off main while observers remain on main")

        let cancelledProcessRead = audit.blockNext(.read, property: p(100, kAudioProcessPropertyPID))
        audit.makeProcessIdle()
        asynchronous!.refresh()
        check(cancelledProcessRead.entered.wait(timeout: .now() + 2) == .success,
              "An idle process refresh can be cancelled while resolving its live identity")
        asynchronous!.stop()
        cancelledProcessRead.release.signal()
        let processFence = audit.enqueueFence()
        check(waitUntil({ processFence.value }), "Cancelled process discovery finishes on its worker")
        asynchronous!.start()
        check(waitUntil({ asynchronous!.snapshot.availableApplications.first(where: { $0.id == 100 })?.isRunningOutput == false })
              && asynchronous!.snapshot.availableApplications.first(where: { $0.id == 100 })?.outputDeviceIDs == [10],
              "Closing during metadata discovery cannot erase remembered routing for an idle live process")

        let coalescedRead = audit.blockNext(.read, property: p(10, kAudioDevicePropertyStreamConfiguration, output))
        asynchronous!.refresh()
        check(coalescedRead.entered.wait(timeout: .now() + 2) == .success, "A subsequent refresh can be held independently of the UI")
        var accepted = true
        let writesBeforeDrag = audit.writeCount
        for value in 1...80 { accepted = asynchronous!.setOutputVolume(Double(value) / 100) && accepted }
        coalescedRead.release.signal()
        check(accepted && waitUntil({ close(asynchronous!.snapshot.outputVolume, 0.8) })
              && audit.writeCount == writesBeforeDrag + 1,
              "A blocked worker coalesces continuous slider input to the latest pending value")

        let cancelledRead = audit.blockNext(.read, property: p(10, kAudioDevicePropertyStreamConfiguration, output))
        asynchronous!.refresh()
        check(cancelledRead.entered.wait(timeout: .now() + 2) == .success, "The cancellation regression holds an active metadata refresh")
        let writesBeforeCancel = audit.writeCount
        let stableSnapshot = asynchronous!.snapshot
        _ = asynchronous!.setOutputVolume(0.2)
        _ = asynchronous!.setDefaultOutput(20)
        asynchronous!.stop()
        cancelledRead.release.signal()
        let cancelledFence = audit.enqueueFence()
        check(waitUntil({ cancelledFence.value }) && audit.writeCount == writesBeforeCancel
              && asynchronous!.snapshot == stableSnapshot && audit.listenerCount == 0,
              "Stopping invalidates queued gain/device changes, drops late state and removes listeners on the worker")

        asynchronous!.start()
        check(waitUntil({ audit.listenerCount > 0 }), "Reactivation restores worker-owned listeners")
        let blockedWrite = audit.blockNext(.write, property: p(10, kAudioDevicePropertyVolumeScalar, output))
        let writeTime = ProcessInfo.processInfo.systemUptime
        check(asynchronous!.setOutputVolume(0.3) && ProcessInfo.processInfo.systemUptime - writeTime < 0.2,
              "Production setters report enqueue acceptance without synchronously waiting for hardware")
        check(blockedWrite.entered.wait(timeout: .now() + 2) == .success, "The hardware-write fixture is blocked after its validated transaction begins")
        mainHeartbeat = false
        DispatchQueue.main.async { mainHeartbeat = true }
        check(waitUntil({ mainHeartbeat }), "A stalled hardware write cannot freeze the main event queue")
        let beforeStoppedWrite = asynchronous!.snapshot
        asynchronous!.stop()
        blockedWrite.release.signal()
        let writeFence = audit.enqueueFence()
        check(waitUntil({ writeFence.value }) && asynchronous!.snapshot == beforeStoppedWrite,
              "An already-started write finishes on its worker but cannot publish into a stopped activation")

        let teardownRead = audit.blockNext(.read, property: p(10, kAudioDevicePropertyStreamConfiguration, output))
        asynchronous!.start()
        check(teardownRead.entered.wait(timeout: .now() + 2) == .success, "A final blocked read exercises controller destruction")
        let releaseTime = ProcessInfo.processInfo.systemUptime
        asynchronous = nil
        check(ProcessInfo.processInfo.systemUptime - releaseTime < 0.2 && !audit.destroyed,
              "Releasing the controller never destroys a backend on main while its operation is blocked")
        teardownRead.release.signal()
        check(waitUntil({ audit.destroyed }) && audit.mainThreadAccesses == 0,
              "The worker retains backend ownership through blocked work and performs final teardown off main")

        let rollbackAudit = BlockingHALAudit()
        var rollbackController: AudioDeviceController? = AudioDeviceController(asynchronousBackend: { queue in
            rollbackAudit.setQueue(queue); return BlockingAudioHAL(audit: rollbackAudit, channelMode: true)
        })
        rollbackController!.start()
        check(waitUntil({ close(rollbackController!.snapshot.outputVolume, 0.8) }),
              "Asynchronous channel fallback starts from the actual stereo levels")
        let failedSecondWrite = rollbackAudit.blockNext(.write, property: p(10, kAudioDevicePropertyVolumeScalar, output, 2), fail: true)
        _ = rollbackController!.setOutputVolume(0.5)
        check(failedSecondWrite.entered.wait(timeout: .now() + 2) == .success, "The second channel can fail after the first channel changed")
        rollbackController!.stop()
        failedSecondWrite.release.signal()
        let rollbackFence = rollbackAudit.enqueueFence()
        check(waitUntil({ rollbackFence.value }) && close(rollbackAudit.leftLevel, 0.4) && close(rollbackAudit.rightLevel, 0.8),
              "Stopping mid-transaction still permits readback and rollback to restore both original channel levels")
        rollbackController = nil
        check(waitUntil({ rollbackAudit.destroyed }) && rollbackAudit.mainThreadAccesses == 0,
              "Rollback and final backend destruction never move onto the UI thread")
        return count
    }

    private static let output = UInt32(kAudioDevicePropertyScopeOutput)
    private static let input = UInt32(kAudioDevicePropertyScopeInput)
    private static func p(_ id: UInt32, _ selector: UInt32, _ scope: UInt32 = kAudioObjectPropertyScopeGlobal, _ element: UInt32 = 0) -> AudioHALProperty {
        AudioHALProperty(object: id, selector: selector, scope: scope, element: element)
    }
    private static func system(_ selector: UInt32) -> AudioHALProperty { p(UInt32(kAudioObjectSystemObject), selector) }
    private static func bytes<T>(_ value: T) -> Data { var value = value; return withUnsafeBytes(of: &value) { Data($0) } }
    private static func buffers(_ channels: UInt32) -> Data {
        bytes(AudioBufferList(mNumberBuffers: 1, mBuffers: AudioBuffer(mNumberChannels: channels, mDataByteSize: 0, mData: nil)))
    }
    private static func drainMainQueue() { RunLoop.main.run(until: Date().addingTimeInterval(0.015)) }

    private final class LockedFlag {
        private let lock = NSLock()
        private var stored = false
        var value: Bool { lock.lock(); defer { lock.unlock() }; return stored }
        func set() { lock.lock(); stored = true; lock.unlock() }
    }
    private final class HALGate {
        enum Kind { case read, write }
        let kind: Kind
        let property: AudioHALProperty
        let fail: Bool
        let entered = DispatchSemaphore(value: 0)
        let release = DispatchSemaphore(value: 0)
        init(kind: Kind, property: AudioHALProperty, fail: Bool) {
            self.kind = kind; self.property = property; self.fail = fail
            // An accidental synchronous regression fails promptly rather than
            // leaving the whole test executable deadlocked indefinitely.
            DispatchQueue.global().asyncAfter(deadline: .now() + 3) { [self] in release.signal() }
        }
    }
    private final class BlockingHALAudit {
        private let lock = NSLock()
        private var queue: DispatchQueue?
        private var gate: HALGate?
        private var mainCalls = 0, reads = 0, writes = 0, listeners = 0
        private var wasDestroyed = false
        private var idleProcess = false
        private var levels: [UInt32: Double] = [:]
        private func locked<T>(_ body: () -> T) -> T { lock.lock(); defer { lock.unlock() }; return body() }
        var mainThreadAccesses: Int { locked { mainCalls } }
        var readCount: Int { locked { reads } }
        var writeCount: Int { locked { writes } }
        var listenerCount: Int { locked { listeners } }
        var destroyed: Bool { locked { wasDestroyed } }
        var processIsIdle: Bool { locked { idleProcess } }
        func makeProcessIdle() { locked { idleProcess = true } }
        var leftLevel: Double? { locked { levels[1] } }
        var rightLevel: Double? { locked { levels[2] } }
        func setQueue(_ value: DispatchQueue) { locked { queue = value } }
        func touch() { locked { if Thread.isMainThread { mainCalls += 1 } } }
        func listened(_ delta: Int) { locked { listeners += delta } }
        func didDestroy() { touch(); locked { wasDestroyed = true } }
        func setLevel(_ value: Double?, channel: UInt32) { locked { levels[channel] = value } }
        func blockNext(_ kind: HALGate.Kind, property: AudioHALProperty, fail: Bool = false) -> HALGate {
            let result = HALGate(kind: kind, property: property, fail: fail)
            locked { gate = result }; return result
        }
        func access(_ kind: HALGate.Kind, property: AudioHALProperty) throws {
            let waiting: HALGate? = locked {
                if Thread.isMainThread { mainCalls += 1 }
                if kind == .read { reads += 1 } else { writes += 1 }
                guard let value = gate, value.kind == kind, value.property == property else { return nil }
                gate = nil; return value
            }
            if let waiting {
                waiting.entered.signal(); waiting.release.wait()
                if waiting.fail { throw AudioDeviceError.hal(-43) }
            }
        }
        func enqueueFence() -> LockedFlag {
            let flag = LockedFlag()
            let target = locked { queue! }
            target.async { flag.set() }; return flag
        }
    }
    private final class BlockingAudioHAL: AudioHALBackend {
        private let base = FakeAudioHAL()
        private let audit: BlockingHALAudit
        init(audit: BlockingHALAudit, channelMode: Bool = false) {
            self.audit = audit; audit.touch(); base.installDevices()
            if channelMode {
                base.data.removeValue(forKey: p(10, kAudioDevicePropertyVolumeScalar, output))
                base.data.removeValue(forKey: p(10, kAudioDevicePropertyStereoPan, output))
                base.put(p(10, kAudioDevicePropertyVolumeScalar, output, 1), Float32(0.4), writable: true)
                base.put(p(10, kAudioDevicePropertyVolumeScalar, output, 2), Float32(0.8), writable: true)
            }
        }
        var supportsProcessActivity: Bool { audit.touch(); return base.supportsProcessActivity }
        func has(_ property: AudioHALProperty) -> Bool { audit.touch(); return base.has(property) }
        func writable(_ property: AudioHALProperty) -> Bool { audit.touch(); return base.writable(property) }
        func read(_ property: AudioHALProperty) throws -> Data {
            try audit.access(.read, property: property)
            if property.object == 100, audit.processIsIdle {
                if property.selector == kAudioProcessPropertyIsRunningOutput { return bytes(UInt32(0)) }
                if property.selector == kAudioProcessPropertyDevices { return Data() }
            }
            return try base.read(property)
        }
        func string(_ property: AudioHALProperty) throws -> String { audit.touch(); return try base.string(property) }
        func sourceName(device: UInt32, source: UInt32, scope: UInt32) throws -> String {
            audit.touch(); return try base.sourceName(device: device, source: source, scope: scope)
        }
        func write(_ property: AudioHALProperty, data: Data) throws {
            try audit.access(.write, property: property)
            try base.write(property, data: data)
            if property.selector == kAudioDevicePropertyVolumeScalar {
                audit.setLevel(base.float(property), channel: property.element)
            }
        }
        func listen(_ property: AudioHALProperty, changed: @escaping () -> Void) throws -> UUID {
            audit.touch(); let token = try base.listen(property, changed: changed); audit.listened(1); return token
        }
        func removeListener(_ token: UUID) {
            audit.touch(); base.removeListener(token); audit.listened(-1)
        }
        deinit { audit.didDestroy() }
    }

    private final class FakeAudioHAL: AudioHALBackend {
        var supportsProcessActivity = true
        var data: [AudioHALProperty: Data] = [:]
        var strings: [AudioHALProperty: String] = [:]
        var writableProperties: Set<AudioHALProperty> = []
        var listeners: [UUID: () -> Void] = [:]
        var removed: [() -> Void] = []
        var reads = 0
        var readAddresses: [AudioHALProperty] = []
        var writes: [(AudioHALProperty, Data)] = []
        var failOnceOnWrite: AudioHALProperty?
        var failReadAfterWrite: AudioHALProperty?
        var pendingReadFailure: AudioHALProperty?
        var ignoreWrites: Set<AudioHALProperty> = []
        func has(_ property: AudioHALProperty) -> Bool { data[property] != nil || strings[property] != nil }
        func writable(_ property: AudioHALProperty) -> Bool { writableProperties.contains(property) && has(property) }
        func read(_ property: AudioHALProperty) throws -> Data {
            reads += 1
            readAddresses.append(property)
            if pendingReadFailure == property { pendingReadFailure = nil; throw AudioDeviceError.hal(-42) }
            guard let value = data[property] else { throw AudioDeviceError.unavailable }; return value
        }
        func string(_ property: AudioHALProperty) throws -> String {
            guard let string = strings[property] else { throw AudioDeviceError.unavailable }; return string
        }
        func sourceName(device: UInt32, source: UInt32, scope: UInt32) throws -> String { throw AudioDeviceError.unavailable }
        func write(_ property: AudioHALProperty, data value: Data) throws {
            writes.append((property, value))
            if failOnceOnWrite == property { failOnceOnWrite = nil; throw AudioDeviceError.hal(-43) }
            if !ignoreWrites.contains(property) { data[property] = value }
            if failReadAfterWrite == property { pendingReadFailure = property; failReadAfterWrite = nil }
        }
        func listen(_ property: AudioHALProperty, changed: @escaping () -> Void) throws -> UUID { let token = UUID(); listeners[token] = changed; return token }
        func removeListener(_ token: UUID) { if let callback = listeners.removeValue(forKey: token) { removed.append(callback) } }
        func emit() { Array(listeners.values).forEach { $0() } }
        func emitRemovedCallbacks() { removed.forEach { $0() } }
        func put<T>(_ property: AudioHALProperty, _ value: T, writable: Bool = false) {
            if let value = value as? Data { data[property] = value } else { data[property] = bytes(value) }
            if writable { writableProperties.insert(property) }
        }
        func float(_ property: AudioHALProperty) -> Double? {
            guard let value = data[property], value.count == 4 else { return nil }
            var result: Float32 = 0
            _ = withUnsafeMutableBytes(of: &result) { value.copyBytes(to: $0) }; return Double(result)
        }
        func installDevices() {
            data[system(kAudioHardwarePropertyDevices)] = [UInt32(10), 20, 30, 40, 50].withUnsafeBytes { Data($0) }
            put(system(kAudioHardwarePropertyDefaultOutputDevice), UInt32(10), writable: true)
            put(system(kAudioHardwarePropertyDefaultInputDevice), UInt32(30), writable: true)
            for id: UInt32 in [10, 20, 30, 40, 50] {
                put(p(id, kAudioDevicePropertyDeviceIsAlive), UInt32(id == 50 ? 0 : 1))
                put(p(id, kAudioDevicePropertyStreamConfiguration, output), buffers(id == 30 ? 0 : 2))
                put(p(id, kAudioDevicePropertyStreamConfiguration, input), buffers(id == 30 ? 1 : 0))
                put(p(id, kAudioDevicePropertyDeviceCanBeDefaultDevice, output), UInt32(id == 30 ? 0 : 1))
                put(p(id, kAudioDevicePropertyDeviceCanBeDefaultDevice, input), UInt32(id == 30 ? 1 : 0))
                strings[p(id, kAudioObjectPropertyName)] = "Device \(id)"
                strings[p(id, kAudioDevicePropertyDeviceUID)] = "test-\(id)"
            }
            put(p(10, kAudioDevicePropertyVolumeScalar, output), Float32(0.6), writable: true)
            put(p(10, kAudioDevicePropertyMute, output), UInt32(0), writable: true)
            put(p(10, kAudioDevicePropertyStereoPan, output), Float32(0.5), writable: true)
            put(p(10, kAudioDevicePropertyTransportType), UInt32(kAudioDeviceTransportTypeBuiltIn))
            put(p(20, kAudioDevicePropertyTransportType), UInt32(kAudioDeviceTransportTypeUSB))
            data[p(20, kAudioDevicePropertyStreams, output)] = bytes(UInt32(200))
            put(p(200, kAudioStreamPropertyTerminalType), UInt32(kAudioStreamTerminalTypeHeadphones))
            put(p(20, kAudioDevicePropertyJackIsConnected, output), UInt32(1))
            put(p(40, kAudioDevicePropertyTransportType), UInt32(kAudioDeviceTransportTypeBluetooth))
            data[system(kAudioHardwarePropertyProcessObjectList)] = [UInt32(100), 101].withUnsafeBytes { Data($0) }
            for id: UInt32 in [100, 101] {
                put(p(id, kAudioProcessPropertyPID), UInt32(500_000 + id))
                put(p(id, kAudioProcessPropertyIsRunningOutput), UInt32(id == 100 ? 1 : 0))
                put(p(id, kAudioProcessPropertyIsRunningInput), UInt32(0))
                data[p(id, kAudioProcessPropertyDevices, output)] = id == 100 ? bytes(UInt32(10)) : Data()
                strings[p(id, kAudioProcessPropertyBundleID)] = "test.audio.\(id)"
            }
        }
    }
}
