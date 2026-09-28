import AppKit
import CoreAudio

struct AudioDeviceInfo: Identifiable, Equatable {
    var id: UInt32
    var name: String
    var uid: String = ""
    var outputChannels: Int = 0
    var inputChannels: Int = 0
    var isHeadphones = false
    var isBluetooth = false
    var canBeDefaultOutput = true
    var canBeDefaultInput = true
    var transportType: UInt32? = nil
}

struct AudioApplicationInfo: Identifiable, Equatable {
    var id: UInt32
    var pid: Int32
    var bundleIdentifier: String? = nil
    var name: String
    var isRunningOutput = true
    var isRunningInput = false
    /// Current, or last confirmed while this exact process object/PID remains
    /// live, output device IDs. An empty list never invents a default route.
    var outputDeviceIDs: [UInt32] = []
    /// Presentation only. A helper keeps its own HAL object/PID and bundle ID.
    var applicationURL: URL? = nil
    var icon: NSImage? = nil
}

struct AudioApplicationAppearance {
    var name: String?
    var applicationURL: URL?
    var icon: NSImage?
}

/// Resolve nested helpers through their actual enclosing app bundle, never by
/// deleting words from a process name or guessing a bundle-ID prefix. The
/// bounded cache also prevents repeated icon/Info.plist reads during dragging.
final class AudioApplicationAppearanceResolver {
    private var cache: [URL: AudioApplicationAppearance] = [:]
    private let loadIcon: (URL) -> NSImage?
    init(loadIcon: @escaping (URL) -> NSImage? = { NSWorkspace.shared.icon(forFile: $0.path) }) {
        self.loadIcon = loadIcon
    }
    func resolve(bundleURL: URL?, executableURL: URL?) -> AudioApplicationAppearance {
        precondition(Thread.isMainThread)
        for candidate in [bundleURL, executableURL].compactMap({ $0 }) where candidate.isFileURL {
            let source = candidate.standardizedFileURL
            if let cached = cache[source] {
                if cached.applicationURL != nil { return cached }
                continue
            }
            var cursor = source
            var outermost: (URL, Bundle)?
            // AppTranslocation and nested helper paths work without assuming
            // /Applications. A malformed/deep path cannot cause an unbounded walk.
            for _ in 0..<64 {
                if cursor.pathExtension.lowercased() == "app", let bundle = Bundle(url: cursor),
                   bundle.object(forInfoDictionaryKey: "CFBundlePackageType") as? String == "APPL" {
                    outermost = (cursor, bundle)
                }
                let parent = cursor.deletingLastPathComponent()
                if parent.path == cursor.path { break }
                cursor = parent
            }
            let result: AudioApplicationAppearance
            if let (url, bundle) = outermost {
                if let shared = cache[url], shared.applicationURL == url {
                    result = shared
                } else {
                    let candidates = [bundle.object(forInfoDictionaryKey: "CFBundleDisplayName") as? String,
                                      bundle.object(forInfoDictionaryKey: "CFBundleName") as? String,
                                      url.deletingPathExtension().lastPathComponent]
                    let name = candidates.compactMap { $0?.trimmingCharacters(in: .whitespacesAndNewlines) }.first { !$0.isEmpty }
                    result = AudioApplicationAppearance(name: name, applicationURL: url, icon: loadIcon(url))
                }
            } else {
                result = AudioApplicationAppearance()
            }
            if cache.count >= 128 { cache.removeAll(keepingCapacity: true) }
            cache[source] = result
            if let url = result.applicationURL { cache[url] = result; return result }
        }
        return AudioApplicationAppearance()
    }
}

struct AudioDeviceSnapshot: Equatable {
    var outputs: [AudioDeviceInfo] = []
    var inputs: [AudioDeviceInfo] = []
    var defaultOutputID: UInt32? = nil
    var defaultInputID: UInt32? = nil
    var outputVolume: Double? = nil
    var canSetOutputVolume = false
    var outputMuted: Bool? = nil
    var canSetOutputMute = false
    var balance: Double? = nil
    var canSetBalance = false
    var canSetDefaultOutput = false
    var canSetDefaultInput = false
    var activeApplications: [AudioApplicationInfo] = []
    var availableApplications: [AudioApplicationInfo] = []
    var applicationActivitySupported = false
    var applicationActivityMessage: String? = nil
}

/// An injectable description of a public HAL property. Element zero is the
/// main element on every supported macOS version (including 10.15).
struct AudioHALProperty: Hashable {
    var object: UInt32
    var selector: UInt32
    var scope: UInt32 = kAudioObjectPropertyScopeGlobal
    var element: UInt32 = 0
    var address: AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: element)
    }
}

protocol AudioHALBackend: AnyObject {
    var supportsProcessActivity: Bool { get }
    func has(_ property: AudioHALProperty) -> Bool
    func writable(_ property: AudioHALProperty) -> Bool
    func read(_ property: AudioHALProperty) throws -> Data
    func string(_ property: AudioHALProperty) throws -> String
    func sourceName(device: UInt32, source: UInt32, scope: UInt32) throws -> String
    func write(_ property: AudioHALProperty, data: Data) throws
    func listen(_ property: AudioHALProperty, changed: @escaping () -> Void) throws -> UUID
    func removeListener(_ token: UUID)
}

enum AudioDeviceError: LocalizedError {
    case unavailable, unsupported, deviceChanged, invalidValue, readback, hal(OSStatus), rollback
    var errorDescription: String? { message(chinese: L10n.isChinese) }
    func message(chinese: Bool) -> String {
        func localized(_ english: String, _ chineseText: String) -> String { chinese ? chineseText : english }
        switch self {
        case .unavailable: return localized("Audio device information is unavailable.", "音频设备信息不可用。")
        case .unsupported: return localized("This device does not support this control.", "此设备不支持该控制。")
        case .deviceChanged: return localized("The audio device changed. Try the control again.", "音频设备已更改，请重试。")
        case .invalidValue: return localized("The audio control value is invalid.", "音频控制值无效。")
        case .readback: return localized("The device could not confirm the audio change.", "无法确认设备的音频更改。")
        case .hal(let code): return localized("macOS could not complete the audio operation", "macOS 无法完成音频操作") + " (\(code))."
        case .rollback: return localized("The audio change failed and could not be fully restored. Check the current device levels.", "音频更改失败且未能完全恢复，请检查当前设备音量。")
        }
    }
}

/// Thin public Core Audio access. CF values are transferred at +1 by HAL and
/// released here. No IOProc, audio tap, stream capture or driver is created.
final class CoreAudioHALBackend: AudioHALBackend {
    private struct Listener { let property: AudioHALProperty; let block: AudioObjectPropertyListenerBlock }
    private var listeners: [UUID: Listener] = [:]
    private let listenerQueue: DispatchQueue
    init(listenerQueue: DispatchQueue = .main) { self.listenerQueue = listenerQueue }
    var supportsProcessActivity: Bool { if #available(macOS 14.2, *) { return true }; return false }

    func has(_ property: AudioHALProperty) -> Bool {
        var address = property.address
        return AudioObjectHasProperty(property.object, &address)
    }
    func writable(_ property: AudioHALProperty) -> Bool {
        var address = property.address
        var value = DarwinBoolean(false)
        return AudioObjectIsPropertySettable(property.object, &address, &value) == noErr && value.boolValue
    }
    func read(_ property: AudioHALProperty) throws -> Data {
        var address = property.address
        var size: UInt32 = 0
        try checked(AudioObjectGetPropertyDataSize(property.object, &address, 0, nil, &size))
        guard size <= 4 * 1_048_576 else { throw AudioDeviceError.unavailable }
        guard size > 0 else { return Data() }
        var data = Data(count: Int(size))
        try data.withUnsafeMutableBytes { raw in
            try checked(AudioObjectGetPropertyData(property.object, &address, 0, nil, &size, raw.baseAddress!))
        }
        guard size <= data.count else { throw AudioDeviceError.unavailable }
        data.count = Int(size)
        return data
    }
    func string(_ property: AudioHALProperty) throws -> String {
        var address = property.address
        var value: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        try checked(AudioObjectGetPropertyData(property.object, &address, 0, nil, &size, &value))
        guard let value else { throw AudioDeviceError.unavailable }
        return value.takeRetainedValue() as String
    }
    func sourceName(device: UInt32, source: UInt32, scope: UInt32) throws -> String {
        var identifier = source
        var result: Unmanaged<CFString>?
        var address = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyDataSourceNameForIDCFString, mScope: scope, mElement: 0)
        try withUnsafeMutablePointer(to: &identifier) { input in
            try withUnsafeMutablePointer(to: &result) { output in
                var translation = AudioValueTranslation(mInputData: input, mInputDataSize: UInt32(MemoryLayout<UInt32>.size),
                    mOutputData: output, mOutputDataSize: UInt32(MemoryLayout<Unmanaged<CFString>?>.size))
                var size = UInt32(MemoryLayout<AudioValueTranslation>.size)
                try checked(AudioObjectGetPropertyData(device, &address, 0, nil, &size, &translation))
            }
        }
        guard let result else { throw AudioDeviceError.unavailable }
        return result.takeRetainedValue() as String
    }
    func write(_ property: AudioHALProperty, data: Data) throws {
        var address = property.address
        try data.withUnsafeBytes { raw in
            guard let pointer = raw.baseAddress else { throw AudioDeviceError.invalidValue }
            try checked(AudioObjectSetPropertyData(property.object, &address, 0, nil, UInt32(data.count), pointer))
        }
    }
    func listen(_ property: AudioHALProperty, changed: @escaping () -> Void) throws -> UUID {
        var address = property.address
        let block: AudioObjectPropertyListenerBlock = { _, _ in changed() }
        try checked(AudioObjectAddPropertyListenerBlock(property.object, &address, listenerQueue, block))
        let token = UUID(); listeners[token] = Listener(property: property, block: block); return token
    }
    func removeListener(_ token: UUID) {
        guard let listener = listeners.removeValue(forKey: token) else { return }
        var address = listener.property.address
        AudioObjectRemovePropertyListenerBlock(listener.property.object, &address, listenerQueue, listener.block)
    }
    private func checked(_ status: OSStatus) throws { if status != noErr { throw AudioDeviceError.hal(status) } }
    deinit { for token in Array(listeners.keys) { removeListener(token) } }
}

/// Main-thread presentation facade. Production HAL work, including listener
/// removal and final teardown, belongs exclusively to one serial worker.
/// Fixture constructors remain synchronous and never instantiate a real HAL.
final class AudioDeviceController {
    private(set) var snapshot = AudioDeviceSnapshot()
    private(set) var statusMessage: String?
    private(set) var isRunning = false
    private var observers: [UUID: () -> Void] = [:]
    private var synchronous: AudioDeviceStateEngine?
    private var worker: AudioDeviceWorker?
    private var generation = 0
    private let appearances = AudioApplicationAppearanceResolver()

    convenience init() {
        self.init(asynchronousBackend: { CoreAudioHALBackend(listenerQueue: $0) })
    }
    /// Injection point for blocking-HAL regression tests; construction happens
    /// on the worker, exactly as in production.
    init(asynchronousBackend: @escaping (DispatchQueue) -> AudioHALBackend) {
        let owner = AudioDeviceWorker(factory: asynchronousBackend)
        worker = owner
        owner.deliver = { [weak self] generation, snapshot, status in
            self?.receive(generation: generation, snapshot: snapshot, status: status)
        }
    }
    init(backend: AudioHALBackend) {
        precondition(!(backend is CoreAudioHALBackend), "Use the asynchronous production initializer for Core Audio")
        let engine = AudioDeviceStateEngine(backend: backend)
        synchronous = engine
        _ = engine.observe { [weak self, weak engine] in
            guard let self, let engine else { return }
            self.receive(generation: self.generation, snapshot: engine.snapshot, status: engine.statusMessage)
        }
    }
    init(snapshot: AudioDeviceSnapshot) {
        let engine = AudioDeviceStateEngine(snapshot: snapshot)
        synchronous = engine; self.snapshot = snapshot
        _ = engine.observe { [weak self, weak engine] in
            guard let self, let engine else { return }
            self.receive(generation: self.generation, snapshot: engine.snapshot, status: engine.statusMessage)
        }
    }
    static func fixture() -> AudioDeviceController { AudioDeviceStateEngine.fixture() }
    @discardableResult func observe(_ action: @escaping () -> Void) -> UUID {
        requireMain(); let token = UUID(); observers[token] = action; return token
    }
    func removeObserver(_ token: UUID) { requireMain(); observers.removeValue(forKey: token) }
    func start() {
        requireMain(); guard !isRunning else { return }; isRunning = true
        if let synchronous { synchronous.start() }
        else { generation = worker!.replaceSession(with: .start, chinese: L10n.isChinese) }
    }
    func stop() {
        requireMain(); isRunning = false
        if let synchronous { synchronous.stop() }
        else { generation = worker!.replaceSession(with: .stop, chinese: L10n.isChinese) }
    }
    func refresh() {
        requireMain()
        if let synchronous { synchronous.refresh() }
        else { worker!.submit(.refresh, chinese: L10n.isChinese) }
    }
    /// In production true means queued for validation. Readback/status arrives
    /// asynchronously; fixture setters retain their synchronous success result.
    @discardableResult func setOutputVolume(_ value: Double) -> Bool {
        requireMain(); if let synchronous { return synchronous.setOutputVolume(value) }
        guard isRunning, value.isFinite, snapshot.canSetOutputVolume, let id = snapshot.defaultOutputID else { return false }
        worker!.submit(.volume(value, id), chinese: L10n.isChinese); return true
    }
    @discardableResult func setOutputMuted(_ value: Bool) -> Bool {
        requireMain(); if let synchronous { return synchronous.setOutputMuted(value) }
        guard isRunning, snapshot.canSetOutputMute, let id = snapshot.defaultOutputID else { return false }
        worker!.submit(.mute(value, id), chinese: L10n.isChinese); return true
    }
    @discardableResult func setBalance(_ value: Double) -> Bool {
        requireMain(); if let synchronous { return synchronous.setBalance(value) }
        guard isRunning, value.isFinite, snapshot.canSetBalance, let id = snapshot.defaultOutputID else { return false }
        worker!.submit(.balance(value, id), chinese: L10n.isChinese); return true
    }
    @discardableResult func setDefaultOutput(_ id: UInt32) -> Bool {
        requireMain(); if let synchronous { return synchronous.setDefaultOutput(id) }
        guard isRunning, snapshot.canSetDefaultOutput, snapshot.outputs.contains(where: { $0.id == id && $0.canBeDefaultOutput }) else { return false }
        worker!.submit(.output(id), chinese: L10n.isChinese); return true
    }
    @discardableResult func setDefaultInput(_ id: UInt32) -> Bool {
        requireMain(); if let synchronous { return synchronous.setDefaultInput(id) }
        guard isRunning, snapshot.canSetDefaultInput, snapshot.inputs.contains(where: { $0.id == id && $0.canBeDefaultInput }) else { return false }
        worker!.submit(.input(id), chinese: L10n.isChinese); return true
    }
    private func receive(generation: Int, snapshot incoming: AudioDeviceSnapshot, status: String?) {
        requireMain(); guard self.generation == generation else { return }
        var next = incoming
        // AppKit decoration is separate from worker-owned Core Audio metadata.
        if worker != nil {
            var presentation: [Int32: AudioApplicationAppearance] = [:]
            for app in next.activeApplications + next.availableApplications where presentation[app.pid] == nil {
                let running = NSRunningApplication(processIdentifier: app.pid)
                var appearance = appearances.resolve(bundleURL: running?.bundleURL, executableURL: running?.executableURL)
                let name = running?.localizedName?.trimmingCharacters(in: .whitespacesAndNewlines)
                appearance.name = appearance.name ?? name.flatMap { $0.isEmpty ? nil : $0 } ?? app.name
                presentation[app.pid] = appearance
            }
            func decorated(_ applications: [AudioApplicationInfo]) -> [AudioApplicationInfo] {
                applications.map { app in
                    var value = app
                    value.name = presentation[app.pid]?.name ?? app.name
                    value.applicationURL = presentation[app.pid]?.applicationURL
                    value.icon = presentation[app.pid]?.icon
                    return value
                }.sorted {
                    let order = $0.name.localizedStandardCompare($1.name)
                    return order == .orderedSame ? $0.id < $1.id : order == .orderedAscending
                }
            }
            next.activeApplications = decorated(next.activeApplications)
            next.availableApplications = decorated(next.availableApplications)
        }
        guard snapshot != next || statusMessage != status else { return }
        snapshot = next; statusMessage = status
        Array(observers.values).forEach { $0() }
    }
    private func requireMain() { precondition(Thread.isMainThread, "AudioDeviceController must be used on the main thread") }
    deinit { worker?.shutdown() }
}

private enum AudioDeviceCommand {
    case start, stop, refresh, volume(Double, UInt32), mute(Bool, UInt32), balance(Double, UInt32), output(UInt32), input(UInt32)
    var key: Int {
        switch self {
        case .start, .stop: return 0
        case .refresh: return 1
        case .volume: return 2
        case .mute: return 3
        case .balance: return 4
        case .output: return 5
        case .input: return 6
        }
    }
}

/// A bounded mailbox avoids accumulating drag samples behind a stalled HAL
/// call. Its lock only protects values; no HAL or callback runs while held.
private final class AudioDeviceWorker {
    private struct Job { let generation: Int; let command: AudioDeviceCommand; let chinese: Bool }
    let queue = DispatchQueue(label: "io.github.endfieldcharge.audio-metadata", qos: .userInitiated)
    var deliver: ((Int, AudioDeviceSnapshot, String?) -> Void)?
    private let factory: (DispatchQueue) -> AudioHALBackend
    private let lock = NSLock()
    private var jobs: [Job] = []
    private var generation = 0
    private var scheduled = false
    private var closed = false
    private var operationGeneration = 0 // worker queue only
    private var engine: AudioDeviceStateEngine? // created and released on worker
    init(factory: @escaping (DispatchQueue) -> AudioHALBackend) { self.factory = factory }

    func replaceSession(with command: AudioDeviceCommand, chinese: Bool) -> Int {
        lock.lock(); generation += 1; let next = generation; jobs.removeAll()
        jobs.append(Job(generation: next, command: command, chinese: chinese))
        let needsDispatch = !scheduled; scheduled = true; lock.unlock()
        if needsDispatch { queue.async { self.drain() } }
        return next
    }
    func submit(_ command: AudioDeviceCommand, chinese: Bool) {
        lock.lock()
        guard !closed else { lock.unlock(); return }
        jobs.removeAll { $0.command.key == command.key }
        jobs.append(Job(generation: generation, command: command, chinese: chinese))
        let needsDispatch = !scheduled; scheduled = true; lock.unlock()
        if needsDispatch { queue.async { self.drain() } }
    }
    func shutdown() {
        lock.lock(); closed = true; generation += 1; jobs.removeAll(); lock.unlock()
        // The queued owner outlives any blocked operation and owns destruction.
        queue.async { self.engine?.stop(); self.engine = nil }
    }
    private func isCurrent(_ value: Int) -> Bool {
        lock.lock(); defer { lock.unlock() }; return !closed && generation == value
    }
    private func drain() {
        dispatchPrecondition(condition: .onQueue(queue))
        while true {
            lock.lock()
            guard !jobs.isEmpty else { scheduled = false; lock.unlock(); return }
            let job = jobs.removeFirst(); lock.unlock()
            guard isCurrent(job.generation) else { continue }
            operationGeneration = job.generation
            if case .stop = job.command { engine?.stop(); continue }
            if engine == nil {
                let backend = AudioDeviceGuardedBackend(base: factory(queue)) { [weak self] in
                    guard let self else { return false }; return self.isCurrent(self.operationGeneration)
                }
                let value = AudioDeviceStateEngine(backend: backend, executionQueue: queue, chinese: job.chinese)
                engine = value
                _ = value.observe { [weak self] in self?.publish() }
            }
            guard let engine else { continue }
            engine.chinese = job.chinese
            switch job.command {
            case .start: engine.stop(); engine.start()
            case .stop: break
            case .refresh: engine.refresh()
            case .volume(let value, let id): if engine.snapshot.defaultOutputID == id { _ = engine.setOutputVolume(value) }
            case .mute(let value, let id): if engine.snapshot.defaultOutputID == id { _ = engine.setOutputMuted(value) }
            case .balance(let value, let id): if engine.snapshot.defaultOutputID == id { _ = engine.setBalance(value) }
            case .output(let id): _ = engine.setDefaultOutput(id)
            case .input(let id): _ = engine.setDefaultInput(id)
            }
            publish()
        }
    }
    private func publish() {
        guard isCurrent(operationGeneration), let engine else { return }
        let snapshot = engine.snapshot, status = engine.statusMessage, token = operationGeneration
        let deliver = deliver
        DispatchQueue.main.async { deliver?(token, snapshot, status) }
    }
}

/// Cancelling cannot interrupt an in-flight system call, but prevents every
/// subsequent read/write/listener installation from the cancelled activation.
private final class AudioDeviceGuardedBackend: AudioHALBackend {
    private let base: AudioHALBackend
    private let allowed: () -> Bool
    private var inTransaction = false // accessed only on the HAL worker
    init(base: AudioHALBackend, allowed: @escaping () -> Bool) { self.base = base; self.allowed = allowed }
    private var mayAccess: Bool { inTransaction || allowed() }
    var isCurrent: Bool { allowed() }
    var supportsProcessActivity: Bool { mayAccess && base.supportsProcessActivity }
    func has(_ property: AudioHALProperty) -> Bool { mayAccess && base.has(property) }
    func writable(_ property: AudioHALProperty) -> Bool { mayAccess && base.writable(property) }
    func read(_ property: AudioHALProperty) throws -> Data { try check(); return try base.read(property) }
    func string(_ property: AudioHALProperty) throws -> String { try check(); return try base.string(property) }
    func sourceName(device: UInt32, source: UInt32, scope: UInt32) throws -> String { try check(); return try base.sourceName(device: device, source: source, scope: scope) }
    func write(_ property: AudioHALProperty, data: Data) throws { try check(); try base.write(property, data: data) }
    func listen(_ property: AudioHALProperty, changed: @escaping () -> Void) throws -> UUID { try check(); return try base.listen(property, changed: changed) }
    func removeListener(_ token: UUID) { base.removeListener(token) }
    func beginTransaction() throws { try check(); inTransaction = true }
    func endTransaction() { inTransaction = false }
    private func check() throws { guard mayAccess else { throw AudioDeviceError.deviceChanged } }
}

/// Serial executor-owned state machine. Production uses a dedicated worker;
/// deterministic fixtures use main. It never publishes mutable HAL objects.
private final class AudioDeviceStateEngine {
    private(set) var snapshot = AudioDeviceSnapshot()
    private(set) var statusMessage: String?
    private(set) var isRunning = false
    private let backend: AudioHALBackend?
    private let executionQueue: DispatchQueue?
    var chinese: Bool?
    private var observers: [UUID: () -> Void] = [:]
    private var listeners: [AudioHALProperty: UUID] = [:]
    private var pendingRefresh = false
    private var generation = 0
    private var channelRatios: [String: [Double]] = [:]
    private var rememberedBalance: [String: Double] = [:]
    private struct KnownOutputProcess {
        let pid: Int32
        let bundleIdentifier: String?
        let devices: [UInt32]
    }
    private var knownOutputProcesses: [UInt32: KnownOutputProcess] = [:]
    private static let system = UInt32(kAudioObjectSystemObject)
    private static let output = UInt32(kAudioDevicePropertyScopeOutput)
    private static let input = UInt32(kAudioDevicePropertyScopeInput)

    init(backend: AudioHALBackend, executionQueue: DispatchQueue? = nil, chinese: Bool? = nil) {
        self.backend = backend; self.executionQueue = executionQueue; self.chinese = chinese
    }
    /// Diagnostics use no real HAL, including during activation and mutations.
    init(snapshot: AudioDeviceSnapshot) { backend = nil; executionQueue = nil; self.snapshot = snapshot }
    static func fixture() -> AudioDeviceController {
        var state = AudioDeviceSnapshot()
        state.outputs = [AudioDeviceInfo(id: 10, name: "MacBook Speakers", outputChannels: 2),
                         AudioDeviceInfo(id: 20, name: "USB Headphones", outputChannels: 2, isHeadphones: true)]
        state.inputs = [AudioDeviceInfo(id: 30, name: "MacBook Microphone", inputChannels: 1)]
        state.defaultOutputID = 10; state.defaultInputID = 30
        state.outputVolume = 0.55; state.canSetOutputVolume = true
        state.outputMuted = false; state.canSetOutputMute = true
        state.balance = 0; state.canSetBalance = true
        state.canSetDefaultOutput = true; state.canSetDefaultInput = true
        state.applicationActivitySupported = true
        state.activeApplications = [AudioApplicationInfo(id: 40, pid: 100, name: "Music", outputDeviceIDs: [10])]
        state.availableApplications = state.activeApplications
        return AudioDeviceController(snapshot: state)
    }
    @discardableResult func observe(_ action: @escaping () -> Void) -> UUID {
        requireMain(); let token = UUID(); observers[token] = action; return token
    }
    func removeObserver(_ token: UUID) { requireMain(); observers.removeValue(forKey: token) }
    func start() { requireMain(); guard !isRunning else { return }; isRunning = true; generation += 1; setStatus(nil); refresh() }
    func stop() {
        requireMain(); isRunning = false; generation += 1; pendingRefresh = false
        for token in listeners.values { backend?.removeListener(token) }; listeners.removeAll()
    }

    func refresh() {
        requireMain()
        guard let backend else { return }
        var next = AudioDeviceSnapshot()
        var desired: Set<AudioHALProperty> = [systemProperty(kAudioHardwarePropertyDevices),
            systemProperty(kAudioHardwarePropertyDefaultOutputDevice), systemProperty(kAudioHardwarePropertyDefaultInputDevice)]
        do {
            let ids: [UInt32] = try array(systemProperty(kAudioHardwarePropertyDevices))
            for id in ids where id != 0 {
                let uid = (try? backend.string(property(id, kAudioDevicePropertyDeviceUID))) ?? "\(id)"
                // Private app routes are implementation details, not selectable
                // outputs. Avoid querying their streams while HAL builds them.
                guard !uid.hasPrefix("EndfieldCharge.AppAudio.") else { continue }
                desired.insert(wildcard(id))
                guard uint(property(id, kAudioDevicePropertyDeviceIsAlive)) == 1 else { continue }
                let outputChannels = channelCount(device: id, scope: Self.output)
                let inputChannels = channelCount(device: id, scope: Self.input)
                guard outputChannels > 0 || inputChannels > 0 else { continue }
                let transport = uint(property(id, kAudioDevicePropertyTransportType))
                let device = AudioDeviceInfo(id: id, name: (try? backend.string(property(id, kAudioObjectPropertyName))) ?? localized("Audio device", "音频设备"),
                    uid: uid,
                    outputChannels: outputChannels, inputChannels: inputChannels,
                    isHeadphones: headphones(id, desired: &desired),
                    isBluetooth: transport == kAudioDeviceTransportTypeBluetooth || transport == kAudioDeviceTransportTypeBluetoothLE,
                    canBeDefaultOutput: uint(property(id, kAudioDevicePropertyDeviceCanBeDefaultDevice, scope: Self.output)) == 1,
                    canBeDefaultInput: uint(property(id, kAudioDevicePropertyDeviceCanBeDefaultDevice, scope: Self.input)) == 1,
                    transportType: transport)
                if outputChannels > 0 { next.outputs.append(device) }
                if inputChannels > 0 { next.inputs.append(device) }
            }
            next.outputs.sort { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
            next.inputs.sort { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
            next.defaultOutputID = uint(systemProperty(kAudioHardwarePropertyDefaultOutputDevice)).flatMap { id in next.outputs.contains { $0.id == id } ? id : nil }
            next.defaultInputID = uint(systemProperty(kAudioHardwarePropertyDefaultInputDevice)).flatMap { id in next.inputs.contains { $0.id == id } ? id : nil }
            next.canSetDefaultOutput = backend.writable(systemProperty(kAudioHardwarePropertyDefaultOutputDevice))
            next.canSetDefaultInput = backend.writable(systemProperty(kAudioHardwarePropertyDefaultInputDevice))
            if let device = next.outputs.first(where: { $0.id == next.defaultOutputID }) {
                let plan = volumePlan(device)
                next.outputVolume = plan.values.max()
                next.canSetOutputVolume = plan.complete && !plan.addresses.isEmpty && plan.addresses.allSatisfy(backend.writable)
                let mute = mutePlan(device)
                if mute.complete, !mute.values.isEmpty {
                    next.outputMuted = mute.values.allSatisfy { $0 == 1 }
                    next.canSetOutputMute = mute.addresses.allSatisfy(backend.writable)
                }
                let pan = property(device.id, kAudioDevicePropertyStereoPan, scope: Self.output)
                if let value = scalar(pan) { next.balance = value * 2 - 1; next.canSetBalance = backend.writable(pan) }
                else if let stereo = stereoChannels(device), (stereo.values.max() ?? 0) > 0 {
                    next.balance = AudioVolumeMath.balance(left: stereo.values[0], right: stereo.values[1])
                    next.canSetBalance = stereo.addresses.allSatisfy(backend.writable)
                }
                if let value = next.balance { rememberedBalance[device.uid] = value }
                if plan.addresses.count > 1, let peak = plan.values.max(), peak > 0 { channelRatios[device.uid] = plan.values.map { $0 / peak } }
            }
            readApplications(into: &next, desired: &desired)
            publish(next)
        } catch {
            publish(next)
            setStatus(errorMessage(error))
        }
        if isRunning { reconcileListeners(desired) }
    }

    @discardableResult func setOutputVolume(_ value: Double) -> Bool {
        change {
            guard value.isFinite else { throw AudioDeviceError.invalidValue }
            guard snapshot.canSetOutputVolume else { throw AudioDeviceError.unsupported }
            let value = min(1, max(0, value))
            guard backend != nil else { snapshot.outputVolume = value; return }
            let device = try currentOutput()
            let plan = volumePlan(device)
            guard plan.complete, !plan.addresses.isEmpty else { throw AudioDeviceError.unsupported }
            let values: [Double]
            if plan.addresses.count == 1 { values = [value] }
            else {
                let peak = plan.values.max() ?? 0
                let ratios = peak > 0 ? plan.values.map { $0 / peak } : (channelRatios[device.uid] ?? Array(repeating: 1, count: plan.values.count))
                values = ratios.count == plan.values.count ? ratios.map { value * $0 } : Array(repeating: value, count: plan.values.count)
                channelRatios[device.uid] = ratios
            }
            try apply(zip(plan.addresses, values).map { ($0.0, bytes(Float32($0.1))) }, kind: .scalar)
        }
    }
    @discardableResult func setOutputMuted(_ muted: Bool) -> Bool {
        change {
            guard snapshot.canSetOutputMute else { throw AudioDeviceError.unsupported }
            guard backend != nil else { snapshot.outputMuted = muted; return }
            let device = try currentOutput()
            let plan = mutePlan(device)
            guard plan.complete, !plan.addresses.isEmpty else { throw AudioDeviceError.unsupported }
            try apply(plan.addresses.map { ($0, bytes(UInt32(muted ? 1 : 0))) }, kind: .boolean)
        }
    }
    @discardableResult func setBalance(_ value: Double) -> Bool {
        change {
            guard value.isFinite else { throw AudioDeviceError.invalidValue }
            guard snapshot.canSetBalance else { throw AudioDeviceError.unsupported }
            let balance = min(1, max(-1, value))
            guard let backend else { snapshot.balance = balance; return }
            let device = try currentOutput()
            let pan = property(device.id, kAudioDevicePropertyStereoPan, scope: Self.output)
            if scalar(pan) != nil {
                guard backend.writable(pan) else { throw AudioDeviceError.unsupported }
                try apply([(pan, bytes(Float32((balance + 1) / 2)))], kind: .scalar)
            } else {
                guard let stereo = stereoChannels(device), (stereo.values.max() ?? 0) > 0 else { throw AudioDeviceError.unsupported }
                let levels = AudioVolumeMath.stereo(volume: stereo.values.max() ?? 0, balance: balance)
                try apply(zip(stereo.addresses, levels).map { ($0.0, bytes(Float32($0.1))) }, kind: .scalar)
                channelRatios[device.uid] = AudioVolumeMath.stereo(volume: 1, balance: balance)
            }
            rememberedBalance[device.uid] = balance
        }
    }
    @discardableResult func setDefaultOutput(_ id: UInt32) -> Bool { setDefault(id, output: true) }
    @discardableResult func setDefaultInput(_ id: UInt32) -> Bool { setDefault(id, output: false) }

    private func setDefault(_ id: UInt32, output: Bool) -> Bool {
        change {
            let devices = output ? snapshot.outputs : snapshot.inputs
            guard let device = devices.first(where: { $0.id == id }),
                  output ? device.canBeDefaultOutput && snapshot.canSetDefaultOutput : device.canBeDefaultInput && snapshot.canSetDefaultInput else { throw AudioDeviceError.unsupported }
            guard backend != nil else {
                if output { snapshot.defaultOutputID = id } else { snapshot.defaultInputID = id }; return
            }
            guard uint(property(id, kAudioDevicePropertyDeviceIsAlive)) == 1,
                  uint(property(id, kAudioDevicePropertyDeviceCanBeDefaultDevice, scope: output ? Self.output : Self.input)) == 1 else { throw AudioDeviceError.deviceChanged }
            let address = systemProperty(output ? kAudioHardwarePropertyDefaultOutputDevice : kAudioHardwarePropertyDefaultInputDevice)
            try apply([(address, bytes(id))], kind: .exact)
        }
    }

    private struct VolumePlan { let addresses: [AudioHALProperty]; let values: [Double]; let complete: Bool }
    private struct MutePlan { let addresses: [AudioHALProperty]; let values: [UInt32]; let complete: Bool }
    private func mutePlan(_ device: AudioDeviceInfo) -> MutePlan {
        guard let backend else { return MutePlan(addresses: [], values: [], complete: false) }
        let main = property(device.id, kAudioDevicePropertyMute, scope: Self.output)
        let mainValue = uint(main).flatMap { $0 <= 1 ? $0 : nil }
        if let value = mainValue, backend.writable(main) { return MutePlan(addresses: [main], values: [value], complete: true) }
        let channels = (1...max(1, device.outputChannels)).map { property(device.id, kAudioDevicePropertyMute, scope: Self.output, element: UInt32($0)) }
        let pairs = channels.compactMap { address in uint(address).flatMap { $0 <= 1 ? (address, $0) : nil } }
        if pairs.count == device.outputChannels, pairs.allSatisfy({ backend.writable($0.0) }) {
            return MutePlan(addresses: pairs.map(\.0), values: pairs.map(\.1), complete: true)
        }
        if let value = mainValue { return MutePlan(addresses: [main], values: [value], complete: true) }
        return MutePlan(addresses: pairs.map(\.0), values: pairs.map(\.1), complete: pairs.count == device.outputChannels)
    }
    private func volumePlan(_ device: AudioDeviceInfo) -> VolumePlan {
        guard let backend else { return VolumePlan(addresses: [], values: [], complete: false) }
        let main = property(device.id, kAudioDevicePropertyVolumeScalar, scope: Self.output)
        if let value = scalar(main), backend.writable(main) { return VolumePlan(addresses: [main], values: [value], complete: true) }
        let channels = (1...max(1, device.outputChannels)).map { property(device.id, kAudioDevicePropertyVolumeScalar, scope: Self.output, element: UInt32($0)) }
        let pairs = channels.compactMap { address in scalar(address).map { (address, $0) } }
        if pairs.count == device.outputChannels, pairs.allSatisfy({ backend.writable($0.0) }) {
            return VolumePlan(addresses: pairs.map(\.0), values: pairs.map(\.1), complete: true)
        }
        if let value = scalar(main) { return VolumePlan(addresses: [main], values: [value], complete: true) }
        return VolumePlan(addresses: pairs.map(\.0), values: pairs.map(\.1), complete: pairs.count == device.outputChannels)
    }
    private func stereoChannels(_ device: AudioDeviceInfo) -> VolumePlan? {
        guard device.outputChannels == 2 else { return nil }
        let preferred: [UInt32] = (try? array(property(device.id, kAudioDevicePropertyPreferredChannelsForStereo, scope: Self.output))) ?? [1, 2]
        guard preferred.count == 2, Set(preferred) == Set([UInt32(1), 2]) else { return nil }
        let addresses = preferred.map { property(device.id, kAudioDevicePropertyVolumeScalar, scope: Self.output, element: $0) }
        let values = addresses.compactMap(scalar)
        return values.count == 2 ? VolumePlan(addresses: addresses, values: values, complete: true) : nil
    }
    private func currentOutput() throws -> AudioDeviceInfo {
        guard let id = snapshot.defaultOutputID, uint(systemProperty(kAudioHardwarePropertyDefaultOutputDevice)) == id,
              uint(property(id, kAudioDevicePropertyDeviceIsAlive)) == 1,
              let device = snapshot.outputs.first(where: { $0.id == id }) else { throw AudioDeviceError.deviceChanged }
        return device
    }
    private enum Verification { case scalar, boolean, exact }
    private func apply(_ changes: [(AudioHALProperty, Data)], kind: Verification) throws {
        guard let backend else { return }
        var originals: [Data] = []
        for change in changes {
            guard backend.has(change.0), backend.writable(change.0) else { throw AudioDeviceError.unsupported }
            originals.append(try backend.read(change.0))
        }
        // A cancelled queued request never starts writing. Once a batch starts,
        // its readback/rollback must finish even if the UI closes mid-write.
        // No cancellation lock is held across these potentially blocking calls.
        let guarded = backend as? AudioDeviceGuardedBackend
        try guarded?.beginTransaction()
        defer { guarded?.endTransaction() }
        var attempted = 0
        do {
            for change in changes {
                attempted += 1
                try backend.write(change.0, data: change.1)
                let actual = try backend.read(change.0)
                switch kind {
                case .scalar: guard let value: Float32 = decode(actual), value.isFinite, (0...1).contains(value) else { throw AudioDeviceError.readback }
                case .boolean: guard let value: UInt32 = decode(actual), value <= 1, actual == change.1 else { throw AudioDeviceError.readback }
                case .exact: guard actual == change.1 else { throw AudioDeviceError.readback }
                }
            }
        } catch {
            var restored = true
            for index in (0..<attempted).reversed() {
                do { try backend.write(changes[index].0, data: originals[index]) }
                catch { restored = false }
            }
            if !restored { throw AudioDeviceError.rollback }; throw error
        }
    }
    private func change(_ operation: () throws -> Void) -> Bool {
        requireMain()
        do { try operation(); statusMessage = nil; refresh(); notify(); return true }
        catch { refresh(); setStatus(errorMessage(error)); return false }
    }

    private func channelCount(device: UInt32, scope: UInt32) -> Int {
        guard let data = try? backend?.read(property(device, kAudioDevicePropertyStreamConfiguration, scope: scope)), data.count >= MemoryLayout<AudioBufferList>.size else { return 0 }
        let pointer = UnsafeMutableRawPointer.allocate(byteCount: data.count, alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { pointer.deallocate() }; data.copyBytes(to: pointer.assumingMemoryBound(to: UInt8.self), count: data.count)
        let list = pointer.assumingMemoryBound(to: AudioBufferList.self)
        let count = Int(list.pointee.mNumberBuffers)
        let offset = MemoryLayout<AudioBufferList>.offset(of: \.mBuffers) ?? 8
        guard count <= (data.count - offset) / MemoryLayout<AudioBuffer>.stride else { return 0 }
        let result = UnsafeMutableAudioBufferListPointer(list).reduce(0) { $0 + Int($1.mNumberChannels) }
        return min(256, result)
    }
    private func headphones(_ device: UInt32, desired: inout Set<AudioHALProperty>) -> Bool {
        let streams: [UInt32] = (try? array(property(device, kAudioDevicePropertyStreams, scope: Self.output))) ?? []
        var confirmed = false
        for stream in streams {
            desired.insert(wildcard(stream))
            if uint(property(stream, kAudioStreamPropertyTerminalType)) == kAudioStreamTerminalTypeHeadphones { confirmed = true }
        }
        if let source = uint(property(device, kAudioDevicePropertyDataSource, scope: Self.output)),
           let name = try? backend?.sourceName(device: device, source: source, scope: Self.output) {
            confirmed = confirmed || name.range(of: "headphone", options: .caseInsensitive) != nil || name.contains("耳机")
        }
        let jack = uint(property(device, kAudioDevicePropertyJackIsConnected, scope: Self.output))
        return confirmed && jack != 0
    }
    private func readApplications(into next: inout AudioDeviceSnapshot, desired: inout Set<AudioHALProperty>) {
        guard let backend, backend.supportsProcessActivity,
              backend.has(systemProperty(kAudioHardwarePropertyProcessObjectList)) else {
            next.applicationActivityMessage = localized("Active audio apps require macOS 14.2 or later and compatible Core Audio support.", "活跃音频应用需要 macOS 14.2 或更新版本及兼容的 Core Audio 支持。")
            return
        }
        desired.insert(systemProperty(kAudioHardwarePropertyProcessObjectList))
        guard let processes: [UInt32] = try? array(systemProperty(kAudioHardwarePropertyProcessObjectList)) else {
            next.applicationActivityMessage = localized("Active audio app information is unavailable.", "活跃音频应用信息不可用。")
            return
        }
        next.applicationActivitySupported = true
        let liveObjects = Set(processes)
        var nextKnownProcesses = knownOutputProcesses.filter { liveObjects.contains($0.key) }
        func usableName(_ value: String?) -> String? {
            guard let value = value?.trimmingCharacters(in: .whitespacesAndNewlines), !value.isEmpty else { return nil }
            return value
        }
        for id in processes where id != 0 {
            desired.insert(wildcard(id))
            let output = uint(property(id, kAudioProcessPropertyIsRunningOutput)) == 1
            let input = uint(property(id, kAudioProcessPropertyIsRunningInput)) == 1
            guard let raw = uint(property(id, kAudioProcessPropertyPID)), raw > 0, raw <= Int32.max else {
                // Do not expose an unidentified process, but a transient PID
                // query failure need not erase its previous identity. The next
                // successful query validates the PID before reusing any fact.
                continue
            }
            let pid = Int32(raw)
            let bundle = usableName(try? backend.string(property(id, kAudioProcessPropertyBundleID)))
            let applicationName = executionQueue == nil ? usableName(NSRunningApplication(processIdentifier: pid)?.localizedName) : nil
            let name = applicationName ?? bundle ?? "PID \(pid)"
            let currentDevices: [UInt32] = (try? array(property(id, kAudioProcessPropertyDevices, scope: Self.output))) ?? []
            let outputDevices = Array(Set(currentDevices.filter { $0 != 0 })).sorted()
            if let previous = nextKnownProcesses[id], previous.pid != pid
                || (previous.bundleIdentifier != nil && bundle != nil && previous.bundleIdentifier != bundle) {
                nextKnownProcesses.removeValue(forKey: id)
            }
            if !outputDevices.isEmpty, pid != ProcessInfo.processInfo.processIdentifier {
                nextKnownProcesses[id] = KnownOutputProcess(pid: pid, bundleIdentifier: bundle, devices: outputDevices)
            }
            let rememberedDevices = nextKnownProcesses[id]?.devices ?? []
            let app = AudioApplicationInfo(id: id, pid: pid, bundleIdentifier: bundle, name: name,
                isRunningOutput: output, isRunningInput: input,
                outputDeviceIDs: outputDevices.isEmpty ? rememberedDevices : outputDevices)
            if output || input { next.activeApplications.append(app) }
            if !app.outputDeviceIDs.isEmpty, pid != ProcessInfo.processInfo.processIdentifier {
                next.availableApplications.append(app)
            }
        }
        next.activeApplications.sort { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
        next.availableApplications.sort { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
        if (backend as? AudioDeviceGuardedBackend)?.isCurrent != false {
            knownOutputProcesses = nextKnownProcesses
        }
    }

    private func reconcileListeners(_ desired: Set<AudioHALProperty>) {
        guard let backend else { return }
        for address in Array(listeners.keys) where !desired.contains(address) {
            if let token = listeners.removeValue(forKey: address) { backend.removeListener(token) }
        }
        for address in desired where listeners[address] == nil {
            do { listeners[address] = try backend.listen(address) { [weak self] in self?.scheduleRefresh() } }
            catch { setStatus(localized("Some audio changes could not be observed. Reopen Volume to refresh.", "无法监听部分音频更改，请重新打开音量模块刷新。")) }
        }
    }
    private func scheduleRefresh() {
        if let executionQueue {
            executionQueue.async { [weak self] in self?.enqueueRefresh() }
        } else if Thread.isMainThread { enqueueRefresh() }
        else { DispatchQueue.main.async { [weak self] in self?.enqueueRefresh() } }
    }
    private func enqueueRefresh() {
        requireMain()
        guard isRunning, !pendingRefresh else { return }; pendingRefresh = true
        let token = generation
        (executionQueue ?? .main).async { [weak self] in
            guard let self, self.isRunning, self.generation == token else { return }
            self.pendingRefresh = false; self.refresh()
        }
    }
    private func publish(_ next: AudioDeviceSnapshot) { if snapshot != next { snapshot = next; notify() } }
    private func setStatus(_ value: String?) { if statusMessage != value { statusMessage = value; notify() } }
    private func notify() { Array(observers.values).forEach { $0() } }
    private func requireMain() {
        if let executionQueue { dispatchPrecondition(condition: .onQueue(executionQueue)) }
        else { precondition(Thread.isMainThread, "Synchronous audio fixtures require the main thread") }
    }
    private func localized(_ english: String, _ chineseText: String) -> String {
        (chinese ?? L10n.isChinese) ? chineseText : english
    }
    private func errorMessage(_ error: Error) -> String {
        if let value = error as? AudioDeviceError { return value.message(chinese: chinese ?? L10n.isChinese) }
        return error.localizedDescription
    }
    private func property(_ id: UInt32, _ selector: UInt32, scope: UInt32 = kAudioObjectPropertyScopeGlobal, element: UInt32 = 0) -> AudioHALProperty {
        AudioHALProperty(object: id, selector: selector, scope: scope, element: element)
    }
    private func systemProperty(_ selector: UInt32) -> AudioHALProperty { property(Self.system, selector) }
    private func wildcard(_ id: UInt32) -> AudioHALProperty {
        property(id, kAudioObjectPropertySelectorWildcard, scope: kAudioObjectPropertyScopeWildcard, element: kAudioObjectPropertyElementWildcard)
    }
    private func uint(_ property: AudioHALProperty) -> UInt32? { guard let data = try? backend?.read(property) else { return nil }; return decode(data) }
    private func scalar(_ property: AudioHALProperty) -> Double? {
        guard let data = try? backend?.read(property), let value: Float32 = decode(data), value.isFinite, (0...1).contains(value) else { return nil }; return Double(value)
    }
    private func array<T>(_ property: AudioHALProperty) throws -> [T] {
        guard let data = try backend?.read(property), data.count % MemoryLayout<T>.size == 0 else { throw AudioDeviceError.unavailable }
        return stride(from: 0, to: data.count, by: MemoryLayout<T>.size).compactMap { decode(data.subdata(in: $0..<($0 + MemoryLayout<T>.size))) }
    }
    private func decode<T>(_ data: Data) -> T? {
        guard data.count == MemoryLayout<T>.size else { return nil }
        let pointer = UnsafeMutableRawPointer.allocate(byteCount: data.count, alignment: MemoryLayout<T>.alignment)
        defer { pointer.deallocate() }; data.copyBytes(to: pointer.assumingMemoryBound(to: UInt8.self), count: data.count)
        return pointer.load(as: T.self)
    }
    private func bytes<T>(_ value: T) -> Data { var value = value; return withUnsafeBytes(of: &value) { Data($0) } }
    deinit { for token in listeners.values { backend?.removeListener(token) } }
}

enum AudioVolumeMath {
    static func balance(left: Double, right: Double, fallback: Double = 0) -> Double {
        guard left.isFinite, right.isFinite else { return 0 }
        let peak = max(left, right)
        guard peak > 0 else { return min(1, max(-1, fallback.isFinite ? fallback : 0)) }
        return min(1, max(-1, (right - left) / peak))
    }
    static func stereo(volume: Double, balance: Double) -> [Double] {
        let volume = min(1, max(0, volume.isFinite ? volume : 0))
        let balance = min(1, max(-1, balance.isFinite ? balance : 0))
        return balance >= 0 ? [volume * (1 - balance), volume] : [volume, volume * (1 + balance)]
    }
}
