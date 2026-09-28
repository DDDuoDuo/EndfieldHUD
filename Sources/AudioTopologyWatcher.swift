import AppKit
import CoreAudio

/// A single device-list listener. This never inspects processes, streams,
/// clipboard contents or audio samples, and performs every HAL call off main.
final class AudioTopologyWatcher {
    var onSnapshot: (([String: String]) -> Void)?
    private let worker = DispatchQueue(label: "EndfieldCharge.eventLog.audioDevices", qos: .utility)
    private var generation = 0 // main-thread delivery gate
    private var running = false
    private let state: WorkerState

    init(backendFactory: ((DispatchQueue) -> AudioHALBackend)? = nil) {
        state = WorkerState(factory: backendFactory ?? { CoreAudioHALBackend(listenerQueue: $0) })
    }

    func start() {
        precondition(Thread.isMainThread)
        guard !running else { return }
        running = true; generation += 1
        let token = generation, queue = worker, state = self.state
        worker.async { [weak self] in
            state.start(queue: queue) { [weak self] devices in
                DispatchQueue.main.async { [weak self] in
                    guard let self, self.running, self.generation == token else { return }
                    self.onSnapshot?(devices)
                }
            }
        }
    }

    func stop() {
        precondition(Thread.isMainThread)
        running = false; generation += 1
        let state = self.state
        worker.async { state.stop() }
    }

    deinit {
        let state = self.state
        worker.async { state.stop() }
    }

    private final class WorkerState {
        let factory: (DispatchQueue) -> AudioHALBackend
        private var backend: AudioHALBackend?
        private var listener: UUID?
        private var pending: DispatchWorkItem?
        private var generation = 0
        private var callback: (([String: String]) -> Void)?
        private let property = AudioHALProperty(object: UInt32(kAudioObjectSystemObject), selector: kAudioHardwarePropertyDevices)

        init(factory: @escaping (DispatchQueue) -> AudioHALBackend) { self.factory = factory }

        func start(queue: DispatchQueue, callback: @escaping ([String: String]) -> Void) {
            stop()
            backend = factory(queue); self.callback = callback
            let token = generation
            listener = try? backend?.listen(property) { [weak self] in
                // The backend uses this serial queue. Coalesce HAL bursts, not
                // a polling timer; private aggregate setup produces no entries.
                guard let self, self.generation == token else { return }
                self.pending?.cancel()
                let work = DispatchWorkItem { [weak self] in
                    guard let self, self.generation == token else { return }
                    self.pending = nil; self.refresh()
                }
                self.pending = work
                queue.asyncAfter(deadline: .now() + 0.12, execute: work)
            }
            refresh()
        }

        func stop() {
            generation += 1; pending?.cancel(); pending = nil
            if let listener { backend?.removeListener(listener) }
            listener = nil; callback = nil; backend = nil
        }

        private func refresh() {
            guard let backend, let data = try? backend.read(property),
                  data.count % MemoryLayout<UInt32>.size == 0, data.count <= 4096 * 4 else { return }
            var ids = [UInt32](repeating: 0, count: data.count / 4)
            _ = ids.withUnsafeMutableBytes { data.copyBytes(to: $0) }
            var devices: [String: String] = [:]
            for id in ids {
                guard let transportData = try? backend.read(AudioHALProperty(object: id, selector: kAudioDevicePropertyTransportType)),
                      transportData.count == MemoryLayout<UInt32>.size else { return }
                var transport: UInt32 = 0
                _ = withUnsafeMutableBytes(of: &transport) { transportData.copyBytes(to: $0) }
                // Virtual/aggregate routes are implementation details, not a
                // user's physical device connection or disconnection.
                if transport == kAudioDeviceTransportTypeAggregate || transport == kAudioDeviceTransportTypeVirtual { continue }
                guard let rawUID = try? backend.string(AudioHALProperty(object: id, selector: kAudioDevicePropertyDeviceUID)) else { return }
                let uid = rawUID.trimmingCharacters(in: .whitespacesAndNewlines)
                guard !uid.isEmpty else { return } // An incomplete read is not a disconnect.
                if uid.hasPrefix("org.endfieldcharge.") || uid.hasPrefix("EndfieldCharge.") { continue }
                guard let rawName = try? backend.string(AudioHALProperty(object: id, selector: kAudioObjectPropertyName)) else { return }
                let name = rawName.trimmingCharacters(in: .whitespacesAndNewlines)
                guard !name.isEmpty, devices[uid] == nil else { return }
                devices[uid] = name
            }
            callback?(devices)
        }
    }
}
