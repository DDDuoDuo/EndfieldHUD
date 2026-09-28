import AppKit
import CoreAudio

struct CorePerAppAudioFactory: PerAppAudioRouteFactory {
    var isSupported: Bool { if #available(macOS 14.2, *) { return true }; return false }
    func make(application: AudioApplicationInfo, output: AudioDeviceInfo,
              event: @escaping (PerAppAudioRouteEvent) -> Void) throws -> PerAppAudioRoute {
        if #available(macOS 14.2, *) {
            let chinese = L10n.isChinese
            return AsyncPerAppAudioRoute(event: event) { queue, cancellation, event in
                CorePerAppAudioRoute(application: application, output: output, executionQueue: queue,
                    cancellation: cancellation, chinese: chinese, event: event)
            }
        }
        throw PerAppAudioEngineError.message(L10n.text("App audio routing requires macOS 14.2 or later.", "应用音频路由需要 macOS 14.2 或更新版本。"))
    }
}

enum PerAppAudioEngineError: LocalizedError {
    case message(String)
    var errorDescription: String? {
        switch self {
        case .message(let message): return message
        }
    }
}

/// Control-thread cancellation only. This lock is never used by the real-time
/// IOProc; the callback continues to use its preallocated atomic render state.
final class PerAppAudioCancellation {
    private let lock = NSLock()
    private var cancelled = false
    private var requestedGain = 1.0
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    var gain: Double { lock.lock(); defer { lock.unlock() }; return requestedGain }
    func cancel() { lock.lock(); cancelled = true; lock.unlock() }
    func setGain(_ value: Double) {
        guard value.isFinite else { return }
        lock.lock(); requestedGain = min(1, max(0, value)); lock.unlock()
    }
    /// The action is limited to publishing renderer state. Never put a HAL
    /// call, observer callback, queue dispatch or blocking operation here.
    func performUnlessCancelled(_ action: () -> Void) -> Bool {
        performUnlessCancelled { _ in action() }
    }
    func performUnlessCancelled(_ action: (Double) -> Void) -> Bool {
        lock.lock(); defer { lock.unlock() }
        guard !cancelled else { return false }
        action(requestedGain); return true
    }
}

/// Main-thread facade: no HAL query, mutation, synchronous queue dispatch or
/// driver wait occurs here. The worker owns the core until cleanup completes.
/// A timed-out AudioDeviceStart cannot be safely interrupted; cancellation
/// prevents later arming and resources stay owned until that call returns.
final class AsyncPerAppAudioRoute: PerAppAudioRoute {
    typealias Factory = (DispatchQueue, PerAppAudioCancellation, @escaping (PerAppAudioRouteEvent) -> Void) -> PerAppAudioRoute
    private(set) var isStopped = true
    private(set) var isStopping = false
    private let event: (PerAppAudioRouteEvent) -> Void
    private let worker: Worker
    private let startTimeout: TimeInterval
    private let stopTimeout: TimeInterval
    private var startDeadline: DispatchWorkItem?
    private var stopDeadline: DispatchWorkItem?
    private var began = false
    private var terminalFailure: String?
    private var gainUpdatePending = false
    private var gainRevision: UInt64 = 0

    init(startTimeout: TimeInterval = 15, stopTimeout: TimeInterval = 5,
         event: @escaping (PerAppAudioRouteEvent) -> Void, makeRoute: @escaping Factory) {
        precondition(Thread.isMainThread)
        self.startTimeout = startTimeout; self.stopTimeout = stopTimeout; self.event = event
        worker = Worker(factory: makeRoute)
        worker.report = { [weak self] update in self?.receive(update) }
    }

    func begin() throws {
        precondition(Thread.isMainThread)
        guard !began else { throw PerAppAudioEngineError.message(L10n.text("This route has already been started.", "此路由已经启动过。")) }
        began = true; isStopped = false
        let deadline = DispatchWorkItem { [weak self] in
            guard let self, !self.isStopped, !self.isStopping else { return }
            self.worker.cancellation.cancel()
            let detail = L10n.text("macOS did not finish starting audio processing. Cancellation is pending; the original app was not muted. Quit EndfieldCharge if macOS remains unresponsive.",
                "macOS 未能完成音频处理启动，正在等待取消；应用原始输出未被静音。若系统持续无响应，请退出 EndfieldCharge。")
            self.terminalFailure = detail
            self.event(.failed(detail))
            _ = self.stop()
        }
        startDeadline = deadline
        DispatchQueue.main.asyncAfter(deadline: .now() + startTimeout, execute: deadline)
        let owner = worker
        owner.queue.async { owner.start() }
    }

    func setGain(_ value: Double) {
        precondition(Thread.isMainThread)
        guard value.isFinite, (!began || !isStopped), !isStopping, !worker.cancellation.isCancelled else { return }
        // This mailbox is readable by the worker even if startup is inside a
        // blocked HAL call. It is not an audio forwarding or capture action.
        worker.cancellation.setGain(value)
        gainRevision &+= 1
        scheduleGainUpdate()
    }
    private func scheduleGainUpdate() {
        guard began else { return }
        guard !gainUpdatePending, !isStopped, !isStopping, !worker.cancellation.isCancelled else { return }
        gainUpdatePending = true
        let revision = gainRevision
        let owner = worker
        owner.queue.async {
            if !owner.cancellation.isCancelled { owner.route?.setGain(owner.cancellation.gain) }
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.gainUpdatePending = false
                if self.gainRevision != revision { self.scheduleGainUpdate() }
            }
        }
    }

    func stop() -> String? {
        precondition(Thread.isMainThread)
        worker.cancellation.cancel()
        startDeadline?.cancel(); startDeadline = nil
        guard !isStopped else { return nil }
        guard !isStopping else { return terminalFailure }
        isStopping = true
        let deadline = DispatchWorkItem { [weak self] in
            guard let self, self.isStopping, !self.isStopped else { return }
            let detail = L10n.text("Waiting for macOS to release the audio route. The app remains responsive; quit EndfieldCharge if audio is affected. No replacement route will start while cleanup is pending.",
                "正在等待 macOS 释放音频路由。界面仍可操作；若声音受影响，请退出 EndfieldCharge。清理完成前不会启动替代路由。")
            self.terminalFailure = detail
            self.event(.failed(detail))
        }
        stopDeadline = deadline
        DispatchQueue.main.asyncAfter(deadline: .now() + stopTimeout, execute: deadline)
        let owner = worker
        owner.queue.async { owner.stop() }
        return nil
    }

    private enum Update {
        case started
        case event(PerAppAudioRouteEvent, stopped: Bool)
    }
    private func receive(_ update: Update) {
        precondition(Thread.isMainThread)
        switch update {
        case .started:
            startDeadline?.cancel(); startDeadline = nil
        case .event(let value, let stopped):
            switch value {
            case .active:
                guard !isStopping, !worker.cancellation.isCancelled else { return }
                startDeadline?.cancel(); startDeadline = nil
                event(.active)
            case .failed(let detail):
                startDeadline?.cancel(); startDeadline = nil
                stopDeadline?.cancel(); stopDeadline = nil
                isStopped = stopped; isStopping = false
                terminalFailure = detail
                event(.failed(detail))
            case .stopped(let detail):
                startDeadline?.cancel(); startDeadline = nil
                stopDeadline?.cancel(); stopDeadline = nil
                isStopped = stopped; isStopping = false
                event(.stopped(detail ?? terminalFailure))
            }
        }
    }

    private final class Worker {
        let queue = DispatchQueue(label: "io.github.endfieldcharge.app-audio.\(UUID().uuidString)", qos: .userInitiated)
        let cancellation = PerAppAudioCancellation()
        let factory: Factory
        var route: PerAppAudioRoute?
        var report: ((Update) -> Void)?
        init(factory: @escaping Factory) { self.factory = factory }
        func start() {
            dispatchPrecondition(condition: .onQueue(queue))
            guard !cancellation.isCancelled else { send(.event(.stopped(nil), stopped: true)); return }
            route = factory(queue, cancellation) { [weak self] event in
                guard let self else { return }
                dispatchPrecondition(condition: .onQueue(self.queue))
                let stopped = self.route?.isStopped ?? true
                self.send(.event(event, stopped: stopped))
                if stopped { self.route = nil }
            }
            do {
                try route?.begin()
                if cancellation.isCancelled { stop(); return }
                send(.started)
            } catch {
                let cleanup = route?.stop()
                let stopped = route?.isStopped ?? true
                if cancellation.isCancelled {
                    send(.event(.stopped(cleanup), stopped: stopped))
                } else {
                    let detail = cleanup.map { error.localizedDescription + " " + $0 } ?? error.localizedDescription
                    send(.event(.failed(detail), stopped: stopped))
                }
                if stopped { route = nil }
            }
        }
        func stop(notify: Bool = true) {
            dispatchPrecondition(condition: .onQueue(queue))
            let error = route?.stop()
            let stopped = route?.isStopped ?? true
            if notify { send(.event(.stopped(error), stopped: stopped)) }
            if stopped { route = nil }
        }
        private func send(_ update: Update) {
            let report = self.report
            DispatchQueue.main.async { report?(update) }
        }
    }

    deinit {
        startDeadline?.cancel(); stopDeadline?.cancel()
        worker.cancellation.cancel()
        // Capture the owner, never self: even final cleanup and core deinit
        // remain on its worker, including when a HAL call is still blocked.
        let owner = worker
        owner.queue.async { owner.stop(notify: false) }
    }
}

@available(macOS 14.2, *)
protocol PerAppAudioHardware: AnyObject {
    var metadata: AudioHALBackend { get }
    func createTap(_ description: CATapDescription, id: inout AudioObjectID) -> OSStatus
    func destroyTap(_ id: AudioObjectID) -> OSStatus
    func createAggregate(_ description: CFDictionary, id: inout AudioObjectID) -> OSStatus
    func destroyAggregate(_ id: AudioObjectID) -> OSStatus
    func createIO(_ device: AudioObjectID, context: UnsafeMutablePointer<PerAppAudioRenderState>?, id: inout AudioDeviceIOProcID?) -> OSStatus
    func destroyIO(_ device: AudioObjectID, id: AudioDeviceIOProcID) -> OSStatus
    func startIO(_ device: AudioObjectID, id: AudioDeviceIOProcID) -> OSStatus
    func stopIO(_ device: AudioObjectID, id: AudioDeviceIOProcID) -> OSStatus
    func setDescription(_ device: AudioObjectID, description: CATapDescription) -> OSStatus
}

@available(macOS 14.2, *)
final class SystemPerAppAudioHardware: PerAppAudioHardware {
    let metadata: AudioHALBackend = CoreAudioHALBackend()
    func createTap(_ description: CATapDescription, id: inout AudioObjectID) -> OSStatus { AudioHardwareCreateProcessTap(description, &id) }
    func destroyTap(_ id: AudioObjectID) -> OSStatus { AudioHardwareDestroyProcessTap(id) }
    func createAggregate(_ description: CFDictionary, id: inout AudioObjectID) -> OSStatus { AudioHardwareCreateAggregateDevice(description, &id) }
    func destroyAggregate(_ id: AudioObjectID) -> OSStatus { AudioHardwareDestroyAggregateDevice(id) }
    func createIO(_ device: AudioObjectID, context: UnsafeMutablePointer<PerAppAudioRenderState>?, id: inout AudioDeviceIOProcID?) -> OSStatus {
        AudioDeviceCreateIOProcID(device, perAppAudioIOProc, context, &id)
    }
    func destroyIO(_ device: AudioObjectID, id: AudioDeviceIOProcID) -> OSStatus { AudioDeviceDestroyIOProcID(device, id) }
    func startIO(_ device: AudioObjectID, id: AudioDeviceIOProcID) -> OSStatus { AudioDeviceStart(device, id) }
    func stopIO(_ device: AudioObjectID, id: AudioDeviceIOProcID) -> OSStatus { AudioDeviceStop(device, id) }
    func setDescription(_ device: AudioObjectID, description: CATapDescription) -> OSStatus {
        var address = AudioHALProperty(object: device, selector: kAudioTapPropertyDescription).address
        // HAL expects the address of an Objective-C object pointer. Keep the
        // description alive for the synchronous setter without transferring
        // ownership or treating Swift reference storage as arbitrary bytes.
        return withExtendedLifetime(description) {
            var object = Unmanaged.passUnretained(description).toOpaque()
            return withUnsafePointer(to: &object) {
                AudioObjectSetPropertyData(device, &address, 0, nil, UInt32(MemoryLayout<UnsafeMutableRawPointer>.size), $0)
            }
        }
    }
}

@available(macOS 14.2, *)
final class CorePerAppAudioRoute: PerAppAudioRoute {
    private let application: AudioApplicationInfo
    private let output: AudioDeviceInfo
    private let event: (PerAppAudioRouteEvent) -> Void
    private let hardware: PerAppAudioHardware
    private let executionQueue: DispatchQueue?
    private let cancellation: PerAppAudioCancellation?
    private let chinese: Bool
    private let readinessTimeout: TimeInterval
    private var hal: AudioHALBackend { hardware.metadata }
    private var tap: AudioObjectID = 0
    private var aggregate: AudioObjectID = 0
    private var description: CATapDescription?
    private var ioProc: AudioDeviceIOProcID?
    private var renderState: UnsafeMutablePointer<PerAppAudioRenderState>?
    private var listeners: [UUID] = []
    private var watchdog: DispatchSourceTimer?
    private var primingStarted: TimeInterval = 0
    private var lastCallback: TimeInterval = 0
    private var lastTicks: Int32 = 0
    private var activated = false
    private var requestedGain = 1.0
    private var beginning = false
    private var stopping = false
    private var generation = 0
    private var formatProperties: Set<AudioHALProperty> = []
    var isStopped: Bool { tap == 0 && aggregate == 0 && ioProc == nil }

    init(application: AudioApplicationInfo, output: AudioDeviceInfo, hardware: PerAppAudioHardware = SystemPerAppAudioHardware(),
         executionQueue: DispatchQueue? = nil, cancellation: PerAppAudioCancellation? = nil, chinese: Bool? = nil,
         readinessTimeout: TimeInterval = 2,
         event: @escaping (PerAppAudioRouteEvent) -> Void) {
        self.application = application; self.output = output; self.hardware = hardware; self.event = event
        self.executionQueue = executionQueue; self.cancellation = cancellation; self.chinese = chinese ?? L10n.isChinese
        self.readinessTimeout = max(0, readinessTimeout)
    }

    func begin() throws {
        requireExecution()
        guard isStopped, !beginning else { throw message("This route is already running.", "此路由已在运行。") }
        beginning = true; defer { beginning = false }
        generation += 1
        do {
            try checkCancellation()
            try validatePhysicalRoute()
            let description = CATapDescription(stereoMixdownOfProcesses: [application.id])
            description.name = "EndfieldCharge App Audio"
            description.isPrivate = true
            description.muteBehavior = .unmuted
            self.description = description
            let create = hardware.createTap(description, id: &tap)
            guard create == noErr, tap != 0 else {
                throw message("Could not create this app's audio tap. Check System Audio Recording permission in Privacy & Security, then retry.",
                              "无法创建此应用的音频 Tap，请检查“隐私与安全性”中的“系统音频录制”权限后重试。")
            }
            try checkCancellation()
            guard hal.writable(property(tap, kAudioTapPropertyDescription)) else {
                throw message("This macOS audio tap cannot safely switch its mute behavior.", "此 macOS 音频 Tap 无法安全切换静音行为。")
            }
            let tapUID = try hal.string(property(tap, kAudioTapPropertyUID))
            guard !tapUID.isEmpty else { throw unsupportedFormat() }
            let tapFormat: AudioStreamBasicDescription = try read(property(tap, kAudioTapPropertyFormat))
            guard validPCM(tapFormat) else { throw unsupportedFormat() }
            let physicalFormat = try streamFormat(device: output.id, scope: kAudioDevicePropertyScopeOutput)
            guard validPCM(physicalFormat), abs(physicalFormat.mSampleRate - tapFormat.mSampleRate) < 0.001 else { throw unsupportedFormat() }
            let uid = UUID().uuidString
            let config: [String: Any] = [
                kAudioAggregateDeviceNameKey: "EndfieldCharge App Route",
                kAudioAggregateDeviceUIDKey: "EndfieldCharge.AppAudio.\(uid)",
                kAudioAggregateDeviceIsPrivateKey: true,
                // This is a concatenated aggregate with one physical output,
                // not a multi-output device mirroring data to every subdevice.
                kAudioAggregateDeviceIsStackedKey: true,
                kAudioAggregateDeviceMainSubDeviceKey: output.uid,
                kAudioAggregateDeviceTapAutoStartKey: false,
                // Use live hardware channel counts. Preflight excludes input
                // devices, and aggregate layout is validated again below.
                kAudioAggregateDeviceSubDeviceListKey: [[kAudioSubDeviceUIDKey: output.uid]],
                kAudioAggregateDeviceTapListKey: [[kAudioSubTapUIDKey: tapUID,
                    kAudioSubTapDriftCompensationKey: true]]
            ]
            try check(hardware.createAggregate(config as CFDictionary, id: &aggregate))
            try checkCancellation()
            guard aggregate != 0 else { throw unsupportedFormat() }
            try waitUntilAggregateReady()
            try checkCancellation()
            guard try channels(aggregate, scope: kAudioDevicePropertyScopeInput) == 2,
                  try channels(aggregate, scope: kAudioDevicePropertyScopeOutput) == 2 else { throw unsupportedFormat() }
            let inputFormat = try streamFormat(device: aggregate, scope: kAudioDevicePropertyScopeInput)
            let outputFormat = try streamFormat(device: aggregate, scope: kAudioDevicePropertyScopeOutput)
            guard validPCM(inputFormat), validPCM(outputFormat),
                  abs(inputFormat.mSampleRate - tapFormat.mSampleRate) < 0.001,
                  abs(outputFormat.mSampleRate - tapFormat.mSampleRate) < 0.001 else { throw unsupportedFormat() }
            try validateFormats()
            let bufferFrames: UInt32 = try read(property(aggregate, kAudioDevicePropertyBufferFrameSize))
            guard bufferFrames > 0, bufferFrames <= 16_384 else { throw unsupportedFormat() }
            try checkCancellation()
            renderState = PerAppAudioPCM.allocate()
            try check(hardware.createIO(aggregate, context: renderState, id: &ioProc))
            try checkCancellation()
            guard let ioProc else { throw unsupportedFormat() }
            try installListeners()
            primingStarted = ProcessInfo.processInfo.systemUptime
            lastCallback = primingStarted; lastTicks = 0; activated = false
            // The tap remains unmuted and the renderer emits silence until a
            // valid callback contains actual selected-process audio. Permission
            // denial or silence therefore cannot mute the original app.
            try checkCancellation()
            try check(hardware.startIO(aggregate, id: ioProc))
            // AudioDeviceStart is synchronous and some HAL/permission paths
            // can block. Never arm a route after a main-thread cancellation.
            try checkCancellation()
            startWatchdog()
        } catch {
            let cleanup = stop()
            let detail = describe(error)
            throw PerAppAudioEngineError.message(cleanup.map { detail + " " + $0 } ?? detail)
        }
    }

    func setGain(_ value: Double) {
        requireExecution()
        guard value.isFinite else { return }
        // The mailbox may contain a newer main-thread edit than this queued
        // call. Never overwrite that intent with an earlier queued slider value.
        requestedGain = cancellation?.gain ?? min(1, max(0, value))
        if let renderState, activated { PerAppAudioPCM.setGain(requestedGain, state: renderState) }
    }

    func stop() -> String? {
        requireExecution()
        guard !stopping else { return nil }
        stopping = true; defer { stopping = false }
        generation += 1
        watchdog?.cancel(); watchdog = nil
        for token in listeners { hal.removeListener(token) }; listeners.removeAll()
        if let renderState { PerAppAudioPCM.store(renderState, PerAppAudioPCM.modeSlot, 0) }
        var failures: [OSStatus] = []
        // Restore the selected app's direct output before dismantling the
        // callback. mutedWhenTapped additionally stops suppressing it as soon
        // as the IOProc stops reading, even if this property update fails.
        if tap != 0, let description {
            description.muteBehavior = .unmuted
            var restored = false
            var status: OSStatus = noErr
            for _ in 0..<3 {
                status = writeDescription(description)
                if status == noErr || status == kAudioHardwareBadObjectError { restored = true; break }
            }
            if !restored { failures.append(status) }
        }
        if aggregate != 0, let ioProc {
            let stopped = hardware.stopIO(aggregate, id: ioProc)
            if stopped != noErr && stopped != kAudioHardwareBadObjectError { failures.append(stopped) }
            let destroyed = hardware.destroyIO(aggregate, id: ioProc)
            if destroyed == noErr || destroyed == kAudioHardwareBadObjectError { self.ioProc = nil }
            else { failures.append(destroyed) }
        }
        if aggregate != 0 {
            let status = hardware.destroyAggregate(aggregate)
            if status == noErr || status == kAudioHardwareBadObjectError { aggregate = 0; ioProc = nil }
            else { failures.append(status) }
        }
        if tap != 0 {
            let status = hardware.destroyTap(tap)
            if status == noErr || status == kAudioHardwareBadObjectError { tap = 0 }
            else { failures.append(status) }
        }
        activated = false
        if ioProc == nil, aggregate == 0, let renderState {
            PerAppAudioPCM.release(renderState); self.renderState = nil
        }
        if isStopped { description = nil }
        guard !failures.isEmpty else { return nil }
        let detail = failures.map(String.init).joined(separator: ", ")
        if !isStopped {
            return localized("The route could not be fully removed. Retry Stop; quit EndfieldCharge if audio remains affected.",
                             "未能完全移除音频路由，请重试停止；若音频仍受影响，请退出 EndfieldCharge。") + " (\(detail))"
        }
        return localized("The route was removed, but macOS reported a cleanup error.", "音频路由已移除，但 macOS 报告了清理错误。") + " (\(detail))"
    }

    private func validatePhysicalRoute() throws {
        let current: UInt32 = try read(property(UInt32(kAudioObjectSystemObject), kAudioHardwarePropertyDefaultOutputDevice))
        let alive: UInt32 = try read(property(output.id, kAudioDevicePropertyDeviceIsAlive))
        let transport: UInt32 = try read(property(output.id, kAudioDevicePropertyTransportType))
        let uid = try hal.string(property(output.id, kAudioDevicePropertyDeviceUID))
        guard current == output.id, alive == 1, uid == output.uid, !uid.isEmpty,
              transport == kAudioDeviceTransportTypeBuiltIn || transport == kAudioDeviceTransportTypeUSB,
              try channels(output.id, scope: kAudioDevicePropertyScopeInput) == 0,
              try channels(output.id, scope: kAudioDevicePropertyScopeOutput) == 2 else {
            throw message("This experimental route requires the current built-in or USB stereo output, with no input channels. The device may have changed.",
                          "此实验性路由需要当前内建或 USB 双声道输出设备，且该设备不能带输入声道。设备可能已更改。")
        }
        let stereo = try array(property(output.id, kAudioDevicePropertyPreferredChannelsForStereo, scope: kAudioDevicePropertyScopeOutput))
        guard stereo == [1, 2] else {
            throw message("This experimental route requires the device's standard left/right stereo channel mapping.", "此实验性路由需要设备使用标准左右双声道映射。")
        }
        try validateProcess()
    }
    private func waitUntilAggregateReady() throws {
        // Aggregate creation may finish before its device becomes alive. A
        // short-lived listener wakes this worker early; short timed waits also
        // notice cancellation. No readiness timer remains after startup.
        // A wedged HAL getter can still block this worker, so the facade's
        // independent main-thread deadline continues to own cancellation.
        let address = property(aggregate, kAudioDevicePropertyDeviceIsAlive)
        let changed = DispatchSemaphore(value: 0)
        let listener = try? hal.listen(address) { changed.signal() }
        defer { if let listener { hal.removeListener(listener) } }
        let deadline = ProcessInfo.processInfo.systemUptime + readinessTimeout
        while true {
            try checkCancellation()
            let alive: UInt32
            do { alive = try read(address) } catch { alive = 0 }
            if alive == 1 { return }
            let remaining = deadline - ProcessInfo.processInfo.systemUptime
            guard remaining > 0 else {
                throw message("The private audio device did not become ready. Its route was cancelled; try again after the audio device settles.",
                              "私有音频设备未能就绪，路由已取消。请等待音频设备稳定后重试。")
            }
            _ = changed.wait(timeout: .now() + min(0.02, remaining))
        }
    }
    private func validateProcess() throws {
        let pid: Int32 = try read(property(application.id, kAudioProcessPropertyPID))
        let devices: [UInt32] = try array(property(application.id, kAudioProcessPropertyDevices, scope: kAudioDevicePropertyScopeOutput))
        guard pid == application.pid, pid != ProcessInfo.processInfo.processIdentifier,
              Set(devices) == Set([output.id]) else {
            throw message("This process must output only to the selected device. Processes using another or several devices are not supported.",
                          "此进程必须仅向所选设备输出，暂不支持使用其他或多个设备的进程。")
        }
        if let expected = application.bundleIdentifier, !expected.isEmpty {
            guard (try? hal.string(property(application.id, kAudioProcessPropertyBundleID))) == expected else {
                throw message("The selected audio process changed. Select the app again.", "所选音频进程已更改，请重新选择应用。")
            }
        }
    }
    private func installListeners() throws {
        let token = generation
        let global = UInt32(kAudioObjectSystemObject)
        let addresses = [property(global, kAudioHardwarePropertyDefaultOutputDevice),
            property(global, kAudioHardwarePropertyDevices), property(global, kAudioHardwarePropertyProcessObjectList),
            property(output.id, kAudioDevicePropertyDeviceIsAlive), property(output.id, kAudioDevicePropertyNominalSampleRate),
            property(output.id, kAudioDevicePropertyStreams, scope: kAudioDevicePropertyScopeOutput),
            property(output.id, kAudioDevicePropertyPreferredChannelsForStereo, scope: kAudioDevicePropertyScopeOutput),
            property(application.id, kAudioProcessPropertyDevices, scope: kAudioDevicePropertyScopeOutput),
            property(aggregate, kAudioDevicePropertyNominalSampleRate),
            property(aggregate, kAudioDevicePropertyStreams, scope: kAudioDevicePropertyScopeInput),
            property(aggregate, kAudioDevicePropertyStreams, scope: kAudioDevicePropertyScopeOutput)] + Array(formatProperties)
        for address in addresses {
            let listener = try hal.listen(address) { [weak self] in
                guard let self else { return }
                self.enqueuePropertyChange {
                guard self.generation == token, !self.stopping, !self.beginning else { return }
                // Device/process topology changes require a fresh explicit
                // start, never silently redirect audio to another output.
                do {
                    try self.validatePhysicalRoute()
                    if address.object == self.output.id || address.object == self.aggregate || self.formatProperties.contains(address) {
                        throw self.message("The audio device format changed. Start this route again if needed.", "音频设备格式已更改，如有需要请重新启动此路由。")
                    }
                } catch { self.fail(self.describe(error)) }
                }
            }
            listeners.append(listener)
        }
    }
    private func startWatchdog() {
        let token = generation
        let timer = DispatchSource.makeTimerSource(queue: executionQueue ?? .main)
        timer.schedule(deadline: .now() + 0.1, repeating: 0.1, leeway: .milliseconds(20))
        timer.setEventHandler { [weak self] in
            guard let self, self.generation == token else { return }
            self.serviceHealth(now: ProcessInfo.processInfo.systemUptime)
        }
        watchdog = timer; timer.resume()
    }
    /// Worker health service, separable from the timer for deterministic
    /// injected-hardware tests. It never reads or logs captured sample data.
    func serviceHealth(now: TimeInterval) {
            requireExecution()
            guard !stopping, let state = renderState else { return }
            guard cancellation?.isCancelled != true else { _ = stop(); return }
            if PerAppAudioPCM.load(state, PerAppAudioPCM.faultSlot) != 0 {
                self.fail(self.message("The audio callback format changed. Stopping processing and restoring direct playback.", "音频回调格式已更改，正在停止处理并恢复原始播放。").localizedDescription); return
            }
            let ticks = PerAppAudioPCM.load(state, PerAppAudioPCM.tickSlot)
            if ticks != self.lastTicks { self.lastTicks = ticks; self.lastCallback = now }
            if self.activated {
                if now - self.lastCallback > 1 { self.fail(self.message("Audio processing stopped responding. Stopping this route.", "音频处理停止响应，正在停止此路由。").localizedDescription) }
            } else if PerAppAudioPCM.load(state, PerAppAudioPCM.signalSlot) != 0 {
                do {
                    try self.validatePhysicalRoute()
                    try self.validateFormats()
                    try self.checkCancellation()
                    guard let description = self.description else { throw self.unsupportedFormat() }
                    description.muteBehavior = .mutedWhenTapped
                    try self.check(self.writeDescription(description))
                    try self.checkCancellation()
                    let arm: (Double) -> Void = { gain in
                        self.requestedGain = gain
                        PerAppAudioPCM.setGain(gain, state: state)
                        PerAppAudioPCM.store(state, PerAppAudioPCM.modeSlot, 1)
                        self.activated = true
                    }
                    if let cancellation = self.cancellation {
                        guard cancellation.performUnlessCancelled(arm) else { try self.checkCancellation(); return }
                    } else { arm(self.requestedGain) }
                    self.event(.active)
                } catch { self.fail(self.describe(error)) }
            } else if now - self.primingStarted > 60 {
                self.fail(self.message("No usable app audio arrived. Allow System Audio Recording, play audio, and try again. The app's original output was not muted.",
                    "未收到可用的应用音频。请允许系统音频录制、播放音频后重试，应用原始输出未被静音。").localizedDescription)
            }
    }
    private func fail(_ detail: String) {
        // Publish intent before any potentially blocking HAL teardown so the
        // facade can disable controls and start its independent stop deadline.
        event(.failed(detail))
        let cleanup = stop()
        event(.stopped(cleanup.map { detail + " " + $0 } ?? detail))
    }
    private func writeDescription(_ description: CATapDescription) -> OSStatus {
        hardware.setDescription(tap, description: description)
    }
    private func validPCM(_ format: AudioStreamBasicDescription) -> Bool {
        let noninterleaved = format.mFormatFlags & kAudioFormatFlagIsNonInterleaved != 0
        return format.mFormatID == kAudioFormatLinearPCM && format.mFormatFlags & kAudioFormatFlagIsFloat != 0
            && format.mFormatFlags & kAudioFormatFlagIsBigEndian == 0 && format.mBitsPerChannel == 32
            && format.mChannelsPerFrame == 2 && format.mFramesPerPacket == 1
            && format.mBytesPerFrame == (noninterleaved ? 4 : 8)
            && format.mSampleRate.isFinite && (8_000...192_000).contains(format.mSampleRate)
    }
    private func validateFormats() throws {
        let tapProperty = property(tap, kAudioTapPropertyFormat)
        let tapFormat: AudioStreamBasicDescription = try read(tapProperty)
        guard validPCM(tapFormat) else { throw unsupportedFormat() }
        var properties: Set<AudioHALProperty> = [tapProperty]
        for (device, scope) in [(output.id, UInt32(kAudioDevicePropertyScopeOutput)),
                                 (aggregate, UInt32(kAudioDevicePropertyScopeInput)),
                                 (aggregate, UInt32(kAudioDevicePropertyScopeOutput))] {
            let streams = try array(property(device, kAudioDevicePropertyStreams, scope: scope))
            guard streams.count == 1 else { throw unsupportedFormat() }
            let address = property(streams[0], kAudioStreamPropertyVirtualFormat)
            let format: AudioStreamBasicDescription = try read(address)
            guard validPCM(format), abs(format.mSampleRate - tapFormat.mSampleRate) < 0.001 else { throw unsupportedFormat() }
            properties.insert(address)
        }
        guard formatProperties.isEmpty || formatProperties == properties else {
            throw message("The audio streams changed during startup. Start this route again.", "音频流在启动过程中发生更改，请重新启动此路由。")
        }
        formatProperties = properties
    }
    private func streamFormat(device: UInt32, scope: UInt32) throws -> AudioStreamBasicDescription {
        let streams: [UInt32] = try array(property(device, kAudioDevicePropertyStreams, scope: scope))
        guard streams.count == 1 else { throw unsupportedFormat() }
        return try read(property(streams[0], kAudioStreamPropertyVirtualFormat))
    }
    private func channels(_ device: UInt32, scope: UInt32) throws -> Int {
        let data = try hal.read(property(device, kAudioDevicePropertyStreamConfiguration, scope: scope))
        guard data.count >= 4 else { throw unsupportedFormat() }
        let pointer = UnsafeMutableRawPointer.allocate(byteCount: max(data.count, MemoryLayout<AudioBufferList>.size), alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { pointer.deallocate() }; data.copyBytes(to: pointer.assumingMemoryBound(to: UInt8.self), count: data.count)
        let list = pointer.assumingMemoryBound(to: AudioBufferList.self)
        if list.pointee.mNumberBuffers == 0 { return 0 }
        let offset = MemoryLayout<AudioBufferList>.offset(of: \.mBuffers) ?? 8
        guard data.count >= offset, Int(list.pointee.mNumberBuffers) <= (data.count - offset) / MemoryLayout<AudioBuffer>.stride else { throw unsupportedFormat() }
        return UnsafeMutableAudioBufferListPointer(list).reduce(0) { $0 + Int($1.mNumberChannels) }
    }
    private func read<T>(_ property: AudioHALProperty) throws -> T {
        let data = try hal.read(property)
        guard data.count == MemoryLayout<T>.size else { throw unsupportedFormat() }
        let pointer = UnsafeMutableRawPointer.allocate(byteCount: data.count, alignment: MemoryLayout<T>.alignment)
        defer { pointer.deallocate() }; data.copyBytes(to: pointer.assumingMemoryBound(to: UInt8.self), count: data.count)
        return pointer.load(as: T.self)
    }
    private func array(_ property: AudioHALProperty) throws -> [UInt32] {
        let data = try hal.read(property)
        guard data.count % 4 == 0 else { throw unsupportedFormat() }
        var result = [UInt32](repeating: 0, count: data.count / 4)
        _ = result.withUnsafeMutableBytes { data.copyBytes(to: $0) }; return result
    }
    private func property(_ id: UInt32, _ selector: UInt32, scope: UInt32 = kAudioObjectPropertyScopeGlobal) -> AudioHALProperty {
        AudioHALProperty(object: id, selector: selector, scope: scope)
    }
    private func unsupportedFormat() -> PerAppAudioEngineError {
        message("This experimental route requires matching Float32 stereo streams and sample rates. This audio device or format is not supported.",
                "此实验性路由需要格式和采样率匹配的 Float32 双声道音频流，暂不支持此音频设备或格式。")
    }
    private func localized(_ english: String, _ chinese: String) -> String { self.chinese ? chinese : english }
    private func describe(_ error: Error) -> String {
        // AudioDeviceError normally formats using the main-owned language.
        // Worker failures use this route's immutable language snapshot instead.
        if let device = error as? AudioDeviceError {
            if case .hal(let status) = device {
                return localized("macOS could not read the audio device (\(status)).", "macOS 无法读取音频设备（\(status)）。")
            }
            return localized("The audio device changed or could not provide valid route information.", "音频设备已更改或无法提供有效的路由信息。")
        }
        return error.localizedDescription
    }
    private func message(_ english: String, _ chinese: String) -> PerAppAudioEngineError { .message(localized(english, chinese)) }
    private func check(_ status: OSStatus) throws {
        if status != noErr { throw message("macOS could not complete the audio route (\(status)).", "macOS 无法完成音频路由（\(status)）。") }
    }
    private func checkCancellation() throws {
        if cancellation?.isCancelled == true { throw message("Audio routing was cancelled.", "音频路由已取消。") }
    }
    private func requireExecution() {
        if let executionQueue { dispatchPrecondition(condition: .onQueue(executionQueue)) }
        else { precondition(Thread.isMainThread) }
    }
    private func enqueuePropertyChange(_ action: @escaping () -> Void) {
        if let executionQueue { executionQueue.async(execute: action) }
        else { action() }
    }
    deinit {
        _ = stop()
        // If HAL refuses to relinquish its IOProc, its C callback may still own
        // this POD pointer. Intentionally keep that small context alive until
        // process exit instead of risking use-after-free in the audio thread.
        // Private taps/aggregates are process-owned; quit remains the final
        // recovery path when macOS refuses every normal cleanup operation.
        if let renderState { PerAppAudioPCM.store(renderState, PerAppAudioPCM.modeSlot, 0) }
    }
}
