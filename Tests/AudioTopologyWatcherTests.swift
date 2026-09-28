import Foundation
import CoreAudio

/// A queue-bound fake HAL exercises lifecycle and malformed snapshots without
/// querying hardware, registering system listeners or changing host audio.
enum AudioTopologyWatcherTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func wait(_ predicate: () -> Bool, timeout: TimeInterval = 3) -> Bool {
            let deadline = Date().addingTimeInterval(timeout)
            while !predicate() && Date() < deadline {
                RunLoop.current.run(until: Date().addingTimeInterval(0.003))
            }
            return predicate()
        }
        func drain(_ duration: TimeInterval = 0.04) {
            RunLoop.current.run(until: Date().addingTimeInterval(duration))
        }
        let state = FakeState()
        let watcher = AudioTopologyWatcher { queue in FakeBackend(state: state, queue: queue) }
        let log = SystemEventLog()
        let recorder = SystemEventRecorder(log: log)
        var snapshots: [[String: String]] = []
        var deliveredOnMain = true
        watcher.onSnapshot = { value in
            deliveredOnMain = deliveredOnMain && Thread.isMainThread
            snapshots.append(value); recorder.receiveAudioDevices(value)
        }
        check(state.operationCount == 0, "Constructing an audio topology watcher performs no HAL work")
        watcher.start(); watcher.start()
        check(wait { snapshots.count == 1 }, "Starting publishes one complete snapshot asynchronously")
        check(snapshots.first == ["physical-1": "Built-in Speakers", "physical-2": "USB Audio"],
              "Only physical devices are published; aggregate, virtual and owned private routes are ignored")
        check(log.events.isEmpty, "The first valid audio snapshot is a quiet event-log baseline")
        check(state.constructions == 1 && state.listens == 1 && state.activeListeners == 1,
              "Repeated start neither constructs a second backend nor adds duplicate listeners")
        check(state.everyOperationOffMain && deliveredOnMain,
              "Backend construction, reads and listener registration are off main; delivery is on main")
        check(state.metadataRequests(for: 10) == 0 && state.metadataRequests(for: 11) == 0,
              "Virtual and aggregate devices are excluded before their missing name/UID can invalidate physical topology")
        check(state.nameRequests(for: 12) == 0,
              "An owned private UID is excluded even if its transport is not reported as aggregate")
        check(state.unexpectedOperations == 0,
              "Topology observation never reads processes or streams, checks writable controls or writes any audio property")

        let beforeBurst = state.listReads
        state.emit(repetitions: 25)
        check(wait { snapshots.count == 2 }, "A burst of device-list notifications eventually refreshes topology")
        drain(0.18)
        check(state.listReads == beforeBurst + 1 && snapshots.count == 2 && log.events.isEmpty,
              "HAL bursts coalesce to one read and unchanged physical topology creates no log entries")

        func invalid(_ message: String, mutate: () -> Void, restore: () -> Void) {
            let oldSnapshots = snapshots.count, oldReads = state.listReads
            mutate(); state.emit()
            check(wait { state.listReads > oldReads }, "Invalid snapshot fixture reached the worker: " + message)
            drain()
            check(snapshots.count == oldSnapshots && log.events.isEmpty, message)
            restore()
        }
        invalid("A failed device-list read never publishes an empty snapshot or false disconnect", mutate: {
            state.setList(nil)
        }, restore: { state.restoreList() })
        invalid("A malformed device-list byte count leaves the last good topology intact", mutate: {
            state.setList(Data([1, 2, 3]))
        }, restore: { state.restoreList() })
        invalid("An oversized device inventory is rejected before allocating or walking its entries", mutate: {
            state.setList(Data(count: 4097 * 4))
        }, restore: { state.restoreList() })
        invalid("A missing physical transport value cannot misclassify an incomplete device", mutate: {
            state.setTransport(2, nil)
        }, restore: { state.setTransport(2, bytes(UInt32(kAudioDeviceTransportTypeUSB))) })
        invalid("A malformed physical transport value is not substituted with unknown transport", mutate: {
            state.setTransport(2, Data([0]))
        }, restore: { state.setTransport(2, bytes(UInt32(kAudioDeviceTransportTypeUSB))) })
        invalid("A failed physical UID read does not drop one device and invent a disconnection", mutate: {
            state.setUID(2, nil)
        }, restore: { state.setUID(2, "physical-2") })
        invalid("A blank physical UID cannot become an unstable dictionary identity", mutate: {
            state.setUID(2, " \n ")
        }, restore: { state.setUID(2, "physical-2") })
        invalid("Duplicate physical UIDs invalidate the snapshot instead of merging unrelated devices", mutate: {
            state.setUID(2, "physical-1")
        }, restore: { state.setUID(2, "physical-2") })
        invalid("Missing human device labels do not produce an incomplete topology snapshot", mutate: {
            state.setName(2, nil)
        }, restore: { state.setName(2, "USB Audio") })
        invalid("Whitespace-only human labels leave the last valid inventory intact", mutate: {
            state.setName(2, " \n ")
        }, restore: { state.setName(2, "USB Audio") })

        var expectedSnapshots = snapshots.count + 1
        state.setList(bytes([UInt32(1), 2])); state.emit()
        check(wait { snapshots.count == expectedSnapshots } && log.events.isEmpty,
              "Removing only internal aggregate/virtual devices creates no public topology events")
        expectedSnapshots += 1
        state.setList(bytes([UInt32(1)])); state.emit()
        check(wait { snapshots.count == expectedSnapshots } && log.events.map(\.kind) == [.audioDeviceDisconnected]
              && log.events.first?.metadata == ["device": "USB Audio"],
              "A complete physical removal logs one disconnection after prior invalid snapshots were ignored")
        expectedSnapshots += 1
        state.setList(bytes([UInt32(1), 2])); state.emit()
        check(wait { snapshots.count == expectedSnapshots } && log.events.first?.kind == .audioDeviceConnected
              && log.events.count == 2,
              "Reconnecting the same physical identity produces exactly one fresh connection event")
        expectedSnapshots += 1
        state.setUID(2, " physical-2 \n"); state.setName(2, " USB Audio \n"); state.emit()
        check(wait { snapshots.count == expectedSnapshots } && log.events.count == 2,
              "Harmless surrounding metadata whitespace cannot manufacture a disconnect/reconnect pair")

        watcher.stop(); watcher.stop()
        check(wait { state.removals == 1 && state.destructions == 1 },
              "Stopping removes the single listener and releases the backend on its worker")
        let stoppedReads = state.listReads, stoppedSnapshots = snapshots.count
        state.emitRemoved(); drain(0.2)
        check(state.listReads == stoppedReads && snapshots.count == stoppedSnapshots && state.activeListeners == 0,
              "Late callbacks from a removed listener cannot read or deliver after stop")
        check(state.everyOperationOffMain && state.unexpectedOperations == 0,
              "Listener removal and backend destruction also remain off main without any audio writes")

        watcher.start()
        check(wait { snapshots.count == stoppedSnapshots + 1 } && state.constructions == 2 && state.activeListeners == 1,
              "Restart creates one fresh worker-owned backend and one active listener")
        let beforeOldCallbacks = state.listReads
        state.emitRemoved(); drain(0.2)
        check(state.listReads == beforeOldCallbacks && log.events.count == 2,
              "A prior generation's removed listener cannot refresh or log into a restarted session")
        watcher.stop()
        check(wait { state.destructions == 2 && state.activeListeners == 0 }, "Restarted watcher shuts down cleanly")

        // Hold a fake HAL read while main keeps responding. Stop must invalidate
        // delivery immediately, not wait synchronously for the worker.
        let blocked = FakeState()
        let gate = ReadGate(); blocked.blockNextListRead(gate)
        let delayed = AudioTopologyWatcher { queue in FakeBackend(state: blocked, queue: queue) }
        var delayedSnapshots: [[String: String]] = []
        delayed.onSnapshot = { delayedSnapshots.append($0) }
        delayed.start()
        check(wait { gate.didEnter }, "The blocking fixture reaches its off-main device read")
        var mainHeartbeat = false
        DispatchQueue.main.async { mainHeartbeat = true }
        check(wait { mainHeartbeat }, "A blocked HAL read does not prevent main-thread work")
        delayed.stop()
        check(delayedSnapshots.isEmpty && blocked.activeListeners == 1,
              "Stop invalidates delivery immediately while retaining a backend still inside a HAL read")
        gate.release.signal()
        check(wait { blocked.destructions == 1 }, "The retained backend cleans up when the blocked read eventually returns")
        drain()
        check(delayedSnapshots.isEmpty && blocked.everyOperationOffMain,
              "A late completed read cannot publish a snapshot from the stopped generation")

        let restartState = FakeState()
        let restartGate = ReadGate(); restartState.blockNextListRead(restartGate)
        let restarting = AudioTopologyWatcher { queue in FakeBackend(state: restartState, queue: queue) }
        var restartSnapshots = 0
        restarting.onSnapshot = { _ in restartSnapshots += 1 }
        restarting.start()
        check(wait { restartGate.didEnter }, "The rapid restart fixture begins in a held old-generation read")
        restarting.stop(); restarting.start()
        check(restartState.constructions == 1, "Restart cannot create overlapping backends while the old worker is blocked")
        restartGate.release.signal()
        check(wait { restartSnapshots == 1 && restartState.constructions == 2 && restartState.destructions == 1 },
              "Rapid stop/start discards the old result and delivers only the new generation after serial cleanup")
        check(restartState.maximumActiveListeners == 1 && restartState.activeListeners == 1,
              "Rapid restart never overlaps device-list listeners")
        restarting.stop()
        check(wait { restartState.destructions == 2 }, "Rapid restart cleanup releases both backend generations")

        let lifetime = FakeState()
        var owned: AudioTopologyWatcher? = AudioTopologyWatcher { queue in FakeBackend(state: lifetime, queue: queue) }
        weak var weakWatcher = owned
        var lifetimeSnapshots = 0
        owned?.onSnapshot = { _ in lifetimeSnapshots += 1 }; owned?.start()
        check(wait { lifetimeSnapshots == 1 }, "The lifetime fixture establishes a snapshot")
        owned = nil
        check(weakWatcher == nil && wait { lifetime.destructions == 1 && lifetime.removals == 1 },
              "Deinit enqueues listener removal without a callback retention cycle")
        check(lifetime.everyOperationOffMain, "Deinit never releases the live HAL backend on the main thread")
        return count
    }

    private static func bytes<T>(_ value: T) -> Data {
        var copy = value
        return withUnsafeBytes(of: &copy) { Data($0) }
    }
    private static func bytes(_ values: [UInt32]) -> Data { values.withUnsafeBytes { Data($0) } }

    private final class ReadGate {
        let release = DispatchSemaphore(value: 0)
        private let lock = NSLock()
        private var entered = false
        var didEnter: Bool { lock.lock(); defer { lock.unlock() }; return entered }
        func block() {
            lock.lock(); entered = true; lock.unlock()
            _ = release.wait(timeout: .now() + 5)
        }
    }

    private final class FakeState {
        private let lock = NSLock()
        private var queue: DispatchQueue?
        private var list: Data? = bytes([UInt32(1), 2, 10, 11, 12])
        private var transports: [UInt32: Data] = [1: bytes(UInt32(kAudioDeviceTransportTypeBuiltIn)),
            2: bytes(UInt32(kAudioDeviceTransportTypeUSB)), 10: bytes(UInt32(kAudioDeviceTransportTypeAggregate)),
            11: bytes(UInt32(kAudioDeviceTransportTypeVirtual)), 12: bytes(UInt32(kAudioDeviceTransportTypeUSB))]
        private var uids: [UInt32: String] = [1: "physical-1", 2: "physical-2", 12: "org.endfieldcharge.private-test"]
        private var names: [UInt32: String] = [1: "Built-in Speakers", 2: "USB Audio"]
        private var callbacks: [UUID: () -> Void] = [:]
        private var removed: [() -> Void] = []
        private var operations: [(String, AudioHALProperty?, Bool)] = []
        private var maximumListeners = 0
        private var gate: ReadGate?
        private func locked<T>(_ body: () -> T) -> T { lock.lock(); defer { lock.unlock() }; return body() }
        var operationCount: Int { locked { operations.count } }
        var constructions: Int { count("init") }
        var destructions: Int { count("deinit") }
        var listens: Int { count("listen") }
        var removals: Int { count("remove") }
        var activeListeners: Int { locked { callbacks.count } }
        var maximumActiveListeners: Int { locked { maximumListeners } }
        var listReads: Int { locked { operations.filter { $0.0 == "read" && $0.1?.selector == kAudioHardwarePropertyDevices }.count } }
        var everyOperationOffMain: Bool { locked { operations.allSatisfy { !$0.2 } } }
        var unexpectedOperations: Int {
            locked {
                operations.filter { operation, property, _ in
                    if ["init", "deinit", "remove"].contains(operation) { return false }
                    guard let property else { return true }
                    switch operation {
                    case "read": return ![kAudioHardwarePropertyDevices, kAudioDevicePropertyTransportType].contains(property.selector)
                    case "string": return ![kAudioDevicePropertyDeviceUID, kAudioObjectPropertyName].contains(property.selector)
                    case "listen": return property.object != UInt32(kAudioObjectSystemObject) || property.selector != kAudioHardwarePropertyDevices
                    default: return true
                    }
                }.count
            }
        }
        func metadataRequests(for id: UInt32) -> Int { locked { operations.filter { $0.0 == "string" && $0.1?.object == id }.count } }
        func nameRequests(for id: UInt32) -> Int { locked { operations.filter { $0.0 == "string" && $0.1?.object == id && $0.1?.selector == kAudioObjectPropertyName }.count } }
        private func count(_ name: String) -> Int { locked { operations.filter { $0.0 == name }.count } }
        func record(_ name: String, _ property: AudioHALProperty? = nil) { locked { operations.append((name, property, Thread.isMainThread)) } }
        func bind(_ queue: DispatchQueue) { locked { self.queue = queue }; record("init") }
        func setList(_ value: Data?) { locked { list = value } }
        func restoreList() { setList(bytes([UInt32(1), 2, 10, 11, 12])) }
        func setTransport(_ id: UInt32, _ value: Data?) { locked { transports[id] = value } }
        func setUID(_ id: UInt32, _ value: String?) { locked { uids[id] = value } }
        func setName(_ id: UInt32, _ value: String?) { locked { names[id] = value } }
        func blockNextListRead(_ value: ReadGate) { locked { gate = value } }
        func read(_ property: AudioHALProperty) throws -> Data {
            record("read", property)
            if property.selector == kAudioHardwarePropertyDevices {
                let held: ReadGate? = locked { let result = gate; gate = nil; return result }
                held?.block()
                guard let result = locked({ list }) else { throw AudioDeviceError.unavailable }; return result
            }
            guard property.selector == kAudioDevicePropertyTransportType,
                  let result = locked({ transports[property.object] }) else { throw AudioDeviceError.unavailable }
            return result
        }
        func string(_ property: AudioHALProperty) throws -> String {
            record("string", property)
            let value: String? = locked {
                if property.selector == kAudioDevicePropertyDeviceUID { return uids[property.object] }
                if property.selector == kAudioObjectPropertyName { return names[property.object] }
                return nil
            }
            guard let value else { throw AudioDeviceError.unavailable }; return value
        }
        func listen(_ property: AudioHALProperty, callback: @escaping () -> Void) -> UUID {
            record("listen", property)
            return locked {
                let token = UUID(); callbacks[token] = callback
                maximumListeners = max(maximumListeners, callbacks.count); return token
            }
        }
        func remove(_ token: UUID) {
            record("remove")
            locked { if let callback = callbacks.removeValue(forKey: token) { removed.append(callback) } }
        }
        func emit(repetitions: Int = 1) {
            let delivery = locked { (queue, Array(callbacks.values)) }
            delivery.0?.async { for _ in 0..<repetitions { delivery.1.forEach { $0() } } }
        }
        func emitRemoved() {
            let delivery = locked { (queue, removed) }
            delivery.0?.async { delivery.1.forEach { $0() } }
        }
    }

    private final class FakeBackend: AudioHALBackend {
        let state: FakeState
        init(state: FakeState, queue: DispatchQueue) { self.state = state; state.bind(queue) }
        var supportsProcessActivity: Bool { false }
        func has(_ property: AudioHALProperty) -> Bool { state.record("has", property); return false }
        func writable(_ property: AudioHALProperty) -> Bool { state.record("writable", property); return false }
        func read(_ property: AudioHALProperty) throws -> Data { try state.read(property) }
        func string(_ property: AudioHALProperty) throws -> String { try state.string(property) }
        func sourceName(device: UInt32, source: UInt32, scope: UInt32) throws -> String {
            state.record("sourceName"); throw AudioDeviceError.unsupported
        }
        func write(_ property: AudioHALProperty, data: Data) throws { state.record("write", property); throw AudioDeviceError.unsupported }
        func listen(_ property: AudioHALProperty, changed: @escaping () -> Void) throws -> UUID { state.listen(property, callback: changed) }
        func removeListener(_ token: UUID) { state.remove(token) }
        deinit { state.record("deinit") }
    }
}
