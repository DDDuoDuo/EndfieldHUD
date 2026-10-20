import Foundation
import Darwin
import IOKit

struct SystemActivitySnapshot: Equatable {
    let timestamp: Date
    let uptime: TimeInterval
    let cpuPercent: Double?
    let memoryUsedBytes: UInt64?
    let memoryTotalBytes: UInt64?
    let memoryCompressedBytes: UInt64?
    let uploadBytesPerSecond: Double?
    let downloadBytesPerSecond: Double?
    let diskReadBytesPerSecond: Double?
    let diskWriteBytesPerSecond: Double?
    let statusNotes: [String]
    /// The busiest GPU's total load, 0–100 (SystemActivityDerivation.gpuPercent):
    /// where the driver keeps per-process GPU time, the interval average of
    /// every client's GPU time (so it agrees with the Apps column; nil while
    /// collecting that baseline, like CPU), otherwise the driver's own
    /// utilisation figure. nil where neither exists. Recorded in `history`
    /// with every sample, like CPU and RAM. Adds no status note.
    var gpuPercent: Double? = nil

    static let unavailable = SystemActivitySnapshot(
        timestamp: .distantPast, uptime: 0, cpuPercent: nil,
        memoryUsedBytes: nil, memoryTotalBytes: nil, memoryCompressedBytes: nil,
        uploadBytesPerSecond: nil, downloadBytesPerSecond: nil,
        diskReadBytesPerSecond: nil, diskWriteBytesPerSecond: nil,
        statusNotes: ["Waiting for a sample"])
}

struct SystemActivityCPUTicks: Equatable {
    let user: UInt64
    let system: UInt64
    let idle: UInt64
    let nice: UInt64
    var values: [UInt64] { [user, system, idle, nice] }
}

/// Cumulative counters for one identified interface or block-storage driver.
struct SystemActivityByteCounters: Equatable {
    let received: UInt64
    let sent: UInt64
}

struct SystemActivityMemoryCounters: Equatable {
    let physicalBytes: UInt64
    let pageSize: UInt64
    let internalPages: UInt64
    let purgeablePages: UInt64
    let wiredPages: UInt64
    let compressedPages: UInt64

    var bytes: (used: UInt64, total: UInt64, compressed: UInt64)? {
        guard physicalBytes > 0, pageSize > 0 else { return nil }
        // Anonymous resident pages + wired + the physical compressor footprint.
        // File-backed cache and reclaimable purgeable pages are excluded. Never
        // add total_uncompressed_pages_in_compressor: those aren't resident RAM.
        let resident = internalPages - min(internalPages, purgeablePages)
        guard let pages = SystemActivityDerivation.sum([resident, wiredPages, compressedPages]),
              let used = SystemActivityDerivation.product(pages, pageSize),
              let compressed = SystemActivityDerivation.product(compressedPages, pageSize) else { return nil }
        return (min(physicalBytes, used), physicalBytes, min(physicalBytes, compressed))
    }
}

/// A single bounded read, with identities retained so topology changes cannot
/// produce a false traffic spike. nil means unavailable; an empty network map
/// means the successful read found no active supported interfaces.
struct SystemActivityRawSample {
    let timestamp: Date
    let uptime: TimeInterval
    let cpu: SystemActivityCPUTicks?
    let memory: SystemActivityMemoryCounters?
    let network: [String: SystemActivityByteCounters]?
    let disk: [String: SystemActivityByteCounters]?
    var gpu: SystemActivityGPUReading? = nil
}

enum SystemActivityDerivation {
    static func snapshot(_ current: SystemActivityRawSample, previous: SystemActivityRawSample?) -> SystemActivitySnapshot {
        let elapsed = previous.map { current.uptime - $0.uptime }
        // Sleep or a severely delayed worker leaves a gap, not a misleading
        // long-interval average. A new baseline is established by this sample.
        let validInterval = elapsed.map { $0.isFinite && $0 > 0 && $0 <= 15 } ?? false
        let cpu = validInterval ? cpuPercent(current.cpu, previous?.cpu) : nil
        let network = validInterval ? rates(current.network, previous?.network, elapsed: elapsed!) : nil
        let disk = validInterval ? rates(current.disk, previous?.disk, elapsed: elapsed!) : nil
        let memory = current.memory?.bytes
        var notes: [String] = []
        if cpu == nil { notes.append(current.cpu == nil ? "CPU unavailable" : "Collecting CPU baseline") }
        if memory == nil { notes.append("RAM unavailable") }
        if network == nil { notes.append(current.network == nil ? "Network unavailable" : "Collecting network baseline") }
        if disk == nil { notes.append(current.disk == nil ? "Disk activity unavailable" : "Collecting disk baseline") }
        return SystemActivitySnapshot(timestamp: current.timestamp, uptime: current.uptime, cpuPercent: cpu,
            memoryUsedBytes: memory?.used, memoryTotalBytes: memory?.total, memoryCompressedBytes: memory?.compressed,
            uploadBytesPerSecond: network?.sent, downloadBytesPerSecond: network?.received,
            diskReadBytesPerSecond: disk?.received, diskWriteBytesPerSecond: disk?.sent, statusNotes: notes,
            gpuPercent: gpuPercent(current.gpu, previous: previous?.gpu))
    }

    /// Client GPU time when the driver keeps it: every client's growth over
    /// the interval (up to the 15 s background gap CPU also accepts), summed
    /// per GPU, at most 100%, the busiest GPU. A first reading, or one after
    /// a gap or a fallback reading, is a baseline (nil). Without client time
    /// (Intel/AMD drivers without AppUsage, or a failed walk) the driver's
    /// utilisation figure, which needs no baseline.
    static func gpuPercent(_ current: SystemActivityGPUReading?, previous: SystemActivityGPUReading?) -> Double? {
        guard let current else { return nil }
        guard let clients = current.clients else { return GPUActivityDerivation.busiest(current.utilizations) }
        guard let prior = previous?.clients else { return nil }
        return GPUActivityDerivation.deltas(clients, since: prior, maximumInterval: 15)?.totalPercent()
    }

    static func cpuPercent(_ current: SystemActivityCPUTicks?, _ previous: SystemActivityCPUTicks?) -> Double? {
        guard let current, let previous else { return nil }
        let pairs = zip(current.values, previous.values)
        guard pairs.allSatisfy({ $0 >= $1 }) else { return nil }
        let deltas = pairs.map { $0 - $1 }
        guard let total = sum(deltas), total > 0 else { return nil }
        return min(100, max(0, (Double(total - deltas[2]) / Double(total)) * 100))
    }

    static func rates(_ current: [String: SystemActivityByteCounters]?, _ previous: [String: SystemActivityByteCounters]?,
                      elapsed: TimeInterval) -> (received: Double, sent: Double)? {
        guard let current, let previous, elapsed.isFinite, elapsed > 0,
              Set(current.keys) == Set(previous.keys) else { return nil }
        var received: [UInt64] = [], sent: [UInt64] = []
        for (key, value) in current {
            guard let prior = previous[key], value.received >= prior.received, value.sent >= prior.sent else { return nil }
            received.append(value.received - prior.received); sent.append(value.sent - prior.sent)
        }
        guard let input = sum(received), let output = sum(sent) else { return nil }
        let result = (Double(input) / elapsed, Double(output) / elapsed)
        return result.0.isFinite && result.1.isFinite ? result : nil
    }

    static func sum(_ values: [UInt64]) -> UInt64? {
        var result: UInt64 = 0
        for value in values {
            let next = result.addingReportingOverflow(value)
            guard !next.overflow else { return nil }; result = next.partialValue
        }
        return result
    }

    static func product(_ a: UInt64, _ b: UInt64) -> UInt64? {
        let result = a.multipliedReportingOverflow(by: b)
        return result.overflow ? nil : result.partialValue
    }
}

/// One serial read at 1 Hz while visible, coalesced to every five seconds while
/// hidden. History belongs to the app session, not a canvas. Animation never
/// samples. start/shutdown own lifetime; activate/deactivate only change cadence.
final class SystemActivityMonitor {
    typealias Sampler = () -> SystemActivityRawSample
    typealias Schedule = (@escaping () -> Void) -> (() -> Void)
    typealias CadenceSchedule = (TimeInterval, @escaping () -> Void) -> (() -> Void)
    private(set) var snapshot = SystemActivitySnapshot.unavailable
    private(set) var history: [SystemActivitySnapshot] = []
    private(set) var isActive = false
    private(set) var isRunning = false
    private var activityVisible = false
    private(set) var isAlertActive = false
    var samplingInterval: TimeInterval { isActive ? 1 : 5 }
    static let historyLimit = 60
    private var observers: [UUID: (SystemActivitySnapshot) -> Void] = [:]
    private let worker = DispatchQueue(label: "EndfieldCharge.activity.samples", qos: .utility)
    private let state: WorkerState
    private let schedule: CadenceSchedule
    private var cancelTimer: (() -> Void)?
    private var generation = 0
    private var timerGeneration = 0
    private var reading = false
    private var needsFreshSample = false
    private var fixtureSamples: [SystemActivitySnapshot]?

    init(sampler: @escaping Sampler = SystemActivityReader.read, schedule: Schedule? = nil,
         cadenceSchedule: CadenceSchedule? = nil) {
        state = WorkerState(sampler: sampler)
        self.schedule = cadenceSchedule ?? schedule.map { legacy in { _, action in legacy(action) } } ?? Self.scheduleAtCadence
    }

    @discardableResult
    func observe(_ callback: @escaping (SystemActivitySnapshot) -> Void) -> UUID {
        precondition(Thread.isMainThread)
        let token = UUID(); observers[token] = callback; callback(snapshot); return token
    }

    func removeObserver(_ token: UUID) {
        precondition(Thread.isMainThread); observers.removeValue(forKey: token)
    }

    func start() {
        precondition(Thread.isMainThread)
        guard !isRunning else { return }
        isRunning = true; generation += 1
        if let fixtureSamples { history = fixtureSamples; snapshot = fixtureSamples.last ?? .unavailable; return }
        reschedule()
        needsFreshSample = true; requestSample()
    }

    func activate() {
        precondition(Thread.isMainThread)
        guard !activityVisible else { return }
        activityVisible = true
        updateVisibleDemand()
    }

    func deactivate() {
        precondition(Thread.isMainThread)
        guard activityVisible else { return }
        activityVisible = false
        updateVisibleDemand()
    }

    /// The charge alert and Activity Monitor share one worker and one timer.
    /// Either visible consumer keeps 1 Hz sampling alive; neither owns the
    /// other consumer's demand or the session's background history.
    func setAlertActive(_ value: Bool) {
        precondition(Thread.isMainThread)
        guard isAlertActive != value else { return }
        isAlertActive = value
        updateVisibleDemand()
    }

    private func updateVisibleDemand() {
        let visible = activityVisible || isAlertActive
        guard isActive != visible else { return }
        isActive = visible
        if !isRunning, visible { start() }
        else if isRunning { reschedule() }
    }

    func shutdown() {
        precondition(Thread.isMainThread)
        guard isRunning else { return }
        isRunning = false; isActive = false; activityVisible = false; isAlertActive = false
        generation += 1; timerGeneration += 1; needsFreshSample = false
        cancelTimer?(); cancelTimer = nil
    }

    private func reschedule() {
        cancelTimer?(); cancelTimer = nil; timerGeneration += 1
        guard isRunning, fixtureSamples == nil else { return }
        let token = timerGeneration
        cancelTimer = schedule(samplingInterval) { [weak self] in
            guard let self, self.isRunning, self.timerGeneration == token else { return }
            self.requestSample()
        }
    }

    private func requestSample() {
        precondition(Thread.isMainThread)
        guard isRunning, !reading else { return }
        reading = true; needsFreshSample = false
        let token = generation, state = self.state
        worker.async { [weak self] in
            let result = state.read(generation: token)
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }
                self.reading = false
                guard self.isRunning else { return }
                guard self.generation == token else {
                    if self.needsFreshSample { self.requestSample() }; return
                }
                self.snapshot = result
                self.history.append(result)
                if self.history.count > Self.historyLimit { self.history.removeFirst(self.history.count - Self.historyLimit) }
                // Snapshot callbacks may remove themselves or close the overlay.
                for (id, callback) in Array(self.observers) {
                    guard self.isRunning, self.generation == token else { break }
                    if self.observers[id] != nil { callback(result) }
                }
            }
        }
    }

    deinit { cancelTimer?() }

    /// Deterministic previews and navigation tests never start the real reader,
    /// allocate a sampling timer or depend on the host machine's current load.
    static func fixture() -> SystemActivityMonitor {
        let monitor = SystemActivityMonitor(sampler: {
            SystemActivityRawSample(timestamp: .distantPast, uptime: 0, cpu: nil, memory: nil, network: nil, disk: nil)
        }, schedule: { _ in {} })
        let samples = (0..<60).map { index -> SystemActivitySnapshot in
            let phase = Double(index) / 6
            return SystemActivitySnapshot(timestamp: Date(timeIntervalSince1970: 1_700_000_000 + Double(index)),
                uptime: 1000 + Double(index), cpuPercent: 24 + sin(phase) * 9,
                memoryUsedBytes: 10_200_000_000 + UInt64(index) * 100_000,
                memoryTotalBytes: 17_179_869_184, memoryCompressedBytes: 760_000_000,
                uploadBytesPerSecond: 80_000 + cos(phase) * 20_000,
                downloadBytesPerSecond: 1_400_000 + sin(phase / 2) * 700_000,
                diskReadBytesPerSecond: 2_000_000 + cos(phase) * 1_300_000,
                diskWriteBytesPerSecond: 300_000 + sin(phase) * 140_000, statusNotes: [],
                gpuPercent: 30 + sin(phase * 0.7 + 1) * 8)
        }
        monitor.fixtureSamples = samples; monitor.history = samples
        monitor.snapshot = samples.last ?? .unavailable
        return monitor
    }

    static func scheduleEverySecond(_ callback: @escaping () -> Void) -> () -> Void {
        scheduleAtCadence(1, callback)
    }

    static func scheduleAtCadence(_ interval: TimeInterval, _ callback: @escaping () -> Void) -> () -> Void {
        let timer = DispatchSource.makeTimerSource(queue: .main)
        timer.schedule(deadline: .now() + interval, repeating: interval,
                       leeway: .milliseconds(interval >= 5 ? 1000 : 100))
        timer.setEventHandler(handler: callback); timer.resume()
        return { timer.setEventHandler {}; timer.cancel() }
    }

    private final class WorkerState {
        let sampler: Sampler
        var generation: Int?
        var previous: SystemActivityRawSample?
        init(sampler: @escaping Sampler) { self.sampler = sampler }
        func read(generation: Int) -> SystemActivitySnapshot {
            if self.generation != generation { previous = nil; self.generation = generation }
            let current = sampler()
            let result = SystemActivityDerivation.snapshot(current, previous: previous)
            previous = current; return result
        }
    }
}

/// Documented Mach statistics, BSD interface counters and public I/O Registry
/// storage statistics, plus the GPU driver's I/O Registry utilisation
/// (GPUActivitySampler). No task ports, process enumeration, shell commands,
/// packet inspection, directory enumeration or full-disk reads are involved.
enum SystemActivityReader {
    static func read() -> SystemActivityRawSample {
        let cpu = readCPU(), memory = readMemory(), network = readNetwork(), disk = readDisk()
        let gpu = GPUActivitySampler.system.systemReading()
        return SystemActivityRawSample(timestamp: Date(), uptime: ProcessInfo.processInfo.systemUptime,
                                       cpu: cpu, memory: memory, network: network, disk: disk, gpu: gpu)
    }

    private static func readCPU() -> SystemActivityCPUTicks? {
        let host = mach_host_self(); defer { mach_port_deallocate(mach_task_self_, host) }
        var info = host_cpu_load_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<host_cpu_load_info_data_t>.size / MemoryLayout<integer_t>.size)
        let expected = count
        let status = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics(host, HOST_CPU_LOAD_INFO, $0, &count)
            }
        }
        guard status == KERN_SUCCESS, count >= expected else { return nil }
        return SystemActivityCPUTicks(user: UInt64(info.cpu_ticks.0), system: UInt64(info.cpu_ticks.1),
                                      idle: UInt64(info.cpu_ticks.2), nice: UInt64(info.cpu_ticks.3))
    }

    private static func readMemory() -> SystemActivityMemoryCounters? {
        let host = mach_host_self(); defer { mach_port_deallocate(mach_task_self_, host) }
        var info = vm_statistics64_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.size / MemoryLayout<integer_t>.size)
        let expected = count
        let status = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(host, HOST_VM_INFO64, $0, &count)
            }
        }
        var pageSize: vm_size_t = 0
        guard status == KERN_SUCCESS, count >= expected,
              host_page_size(host, &pageSize) == KERN_SUCCESS, pageSize > 0 else { return nil }
        return SystemActivityMemoryCounters(physicalBytes: ProcessInfo.processInfo.physicalMemory, pageSize: UInt64(pageSize),
            internalPages: UInt64(info.internal_page_count), purgeablePages: UInt64(info.purgeable_count),
            wiredPages: UInt64(info.wire_count), compressedPages: UInt64(info.compressor_page_count))
    }

    private static func readNetwork() -> [String: SystemActivityByteCounters]? {
        var mib: [Int32] = [CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0]
        // Two bounded attempts tolerate an interface added between size/read.
        for _ in 0..<2 {
            var size = 0
            guard sysctl(&mib, u_int(mib.count), nil, &size, nil, 0) == 0,
                  size > 0, size <= 4 * 1024 * 1024 else { return nil }
            var data = Data(count: size)
            let result = data.withUnsafeMutableBytes { sysctl(&mib, u_int(mib.count), $0.baseAddress, &size, nil, 0) }
            if result != 0 { if errno == ENOMEM { continue }; return nil }
            guard size <= data.count else { return nil }
            data.count = size
            return networkCounters(data) { index in
                var name = [CChar](repeating: 0, count: Int(IFNAMSIZ))
                return if_indextoname(UInt32(index), &name) == nil ? nil : String(cString: name)
            }
        }
        return nil
    }

    /// Decode each AF_LINK record once with 64-bit counters. Ethernet/Wi-Fi and
    /// PPP cover normal physical traffic; tunnel, loopback, bridge and peer-to-
    /// peer interfaces are excluded to avoid counting the same packets twice.
    static func networkCounters(_ data: Data, interfaceName: (UInt16) -> String?) -> [String: SystemActivityByteCounters]? {
        var offset = 0, result: [String: SystemActivityByteCounters] = [:]
        while offset < data.count {
            guard data.count - offset >= 4 else { return nil }
            var length: UInt16 = 0
            _ = withUnsafeMutableBytes(of: &length) { data.copyBytes(to: $0, from: offset..<(offset + 2)) }
            let count = Int(length)
            guard count >= 4, count <= data.count - offset else { return nil }
            if data[offset + 3] == UInt8(RTM_IFINFO2) {
                guard count >= MemoryLayout<if_msghdr2>.size else { return nil }
                var info = if_msghdr2()
                _ = withUnsafeMutableBytes(of: &info) {
                    data.copyBytes(to: $0, from: offset..<(offset + MemoryLayout<if_msghdr2>.size))
                }
                guard info.ifm_version == UInt8(RTM_VERSION) else { return nil }
                let flags = info.ifm_flags
                if flags & (IFF_UP | IFF_RUNNING) == (IFF_UP | IFF_RUNNING), flags & IFF_LOOPBACK == 0 {
                    guard let name = interfaceName(info.ifm_index) else { return nil }
                    if name.hasPrefix("en") || name.hasPrefix("ppp") {
                        let key = "\(info.ifm_index):\(name)"
                        guard result[key] == nil else { return nil }
                        result[key] = SystemActivityByteCounters(received: info.ifm_data.ifi_ibytes, sent: info.ifm_data.ifi_obytes)
                    }
                }
            }
            offset += count
        }
        return result
    }

    private static func readDisk() -> [String: SystemActivityByteCounters]? {
        var iterator: io_iterator_t = 0
        guard IOServiceGetMatchingServices(0, IOServiceMatching("IOBlockStorageDriver"), &iterator) == KERN_SUCCESS else { return nil }
        defer { IOObjectRelease(iterator) }
        var result: [String: SystemActivityByteCounters] = [:]
        var count = 0
        while true {
            let service = IOIteratorNext(iterator)
            guard service != 0 else { break }
            defer { IOObjectRelease(service) }
            count += 1; guard count <= 1024 else { return nil }
            var id: UInt64 = 0
            guard IORegistryEntryGetRegistryEntryID(service, &id) == KERN_SUCCESS else { return nil }
            guard let stats = IORegistryEntryCreateCFProperty(service, "Statistics" as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue() as? [String: Any],
                  let read = nonnegativeInteger(stats["Bytes (Read)"]),
                  let written = nonnegativeInteger(stats["Bytes (Write)"]) else { continue }
            result[String(id)] = SystemActivityByteCounters(received: read, sent: written)
        }
        // An empty registry result is unsupported/unavailable, not idle disks.
        return result.isEmpty ? nil : result
    }

    static func nonnegativeInteger(_ value: Any?) -> UInt64? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID(),
              number.doubleValue.isFinite, number.doubleValue >= 0,
              CFNumberIsFloatType(number) == false else { return nil }
        return number.uint64Value
    }
}

/// One system GPU reading (GPUActivitySampler.systemReading): every client's
/// cumulative GPU time where the driver keeps it, otherwise one 0–100
/// PerformanceStatistics utilisation per IOAccelerator that reported one.
struct SystemActivityGPUReading: Equatable {
    let utilizations: [Double]
    var clients: GPUActivityClientReading? = nil
}

/// One IOAccelerator user client: a GPU device connection a process opened
/// (Metal, OpenGL, Core Animation), with its cumulative GPU time.
struct GPUActivityClientUsage: Equatable {
    /// Registry entry ID of the IOAccelerator (GPU) the client belongs to.
    let accelerator: UInt64
    let pid: Int32
    let gpuTimeNanoseconds: UInt64
}

/// One walk of the accelerators' user clients, limited to the PIDs asked for
/// and keyed by each client's registry entry ID. Entry IDs are never reused
/// during a boot, so a client that closed and one that opened (or a reused
/// PID's new client) can never be mistaken for one another.
struct GPUActivityClientReading: Equatable {
    let uptime: TimeInterval
    let clients: [UInt64: GPUActivityClientUsage]
}

/// Pure GPU policy, shared by the system total and the Apps column.
enum GPUActivityDerivation {
    /// PerformanceStatistics keys in order of preference: Apple silicon and
    /// current AMD/Intel drivers publish "Device Utilization %", older AMD
    /// drivers "GPU Activity(%)", some drivers only the renderer's share.
    static let utilizationKeys = ["Device Utilization %", "GPU Activity(%)", "Renderer Utilization %"]

    /// The first valid utilisation among utilizationKeys, at most 100.
    static func utilization(_ statistics: NSDictionary) -> Double? {
        for key in utilizationKeys {
            guard let number = statistics[key] as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { continue }
            let value = number.doubleValue
            if value.isFinite, value >= 0 { return min(100, value) }
        }
        return nil
    }

    /// Several GPUs (integrated + discrete, an eGPU) report the busiest one:
    /// a sum could pass 100% and an average would let an idle or powered-down
    /// GPU dilute the one actually drawing. On a dual-GPU MacBook Pro the
    /// busiest is the active one.
    static func busiest(_ utilizations: [Double]) -> Double? {
        utilizations.filter { $0.isFinite && $0 >= 0 }.max().map { min(100, $0) }
    }

    /// "pid 123, WindowServer" → 123.
    static func creatorPID(_ creator: String) -> Int32? {
        guard creator.hasPrefix("pid ") else { return nil }
        guard let pid = Int32(creator.dropFirst(4).prefix { $0 != "," }), pid > 0 else { return nil }
        return pid
    }

    /// The sum of an "AppUsage" array's per-queue "accumulatedGPUTime" (ns).
    /// nil unless every entry is a dictionary with a valid integer time.
    static func accumulatedGPUTime(_ appUsage: Any?) -> UInt64? {
        guard let entries = appUsage as? NSArray, entries.count <= 4096 else { return nil }
        var total: UInt64 = 0
        for entry in entries {
            guard let usage = entry as? NSDictionary,
                  let time = SystemActivityReader.nonnegativeInteger(usage["accumulatedGPUTime"]) else { return nil }
            let next = total.addingReportingOverflow(time)
            guard !next.overflow else { return nil }; total = next.partialValue
        }
        return total
    }

    /// The given processes' GPU load between two client walks (`deltas`,
    /// then `GPUActivityDeltas.percent`), over at most 5 s like app CPU.
    static func percent(_ current: GPUActivityClientReading, since previous: GPUActivityClientReading,
                        pids: Set<Int32>) -> Double? {
        deltas(current, since: previous)?.percent(pids: pids)
    }

    /// Every process's GPU-time growth per GPU between two client walks,
    /// grouped once so each app's figure only visits its own PIDs. A client
    /// present in only one walk (opened or closed meanwhile), whose owner
    /// differs or whose time went backwards adds nothing. nil for an invalid
    /// or stale interval (longer than `maximumInterval`).
    static func deltas(_ current: GPUActivityClientReading, since previous: GPUActivityClientReading,
                       maximumInterval: TimeInterval = 5) -> GPUActivityDeltas? {
        let elapsed = current.uptime - previous.uptime
        guard elapsed.isFinite, elapsed > 0, elapsed <= maximumInterval else { return nil }
        var growth: [Int32: [UInt64: UInt64]] = [:]
        for (id, usage) in current.clients {
            guard let old = previous.clients[id], old.pid == usage.pid, old.accelerator == usage.accelerator,
                  usage.gpuTimeNanoseconds >= old.gpuTimeNanoseconds else { continue }
            growth[usage.pid, default: [:]][usage.accelerator, default: 0].saturatingAdd(usage.gpuTimeNanoseconds - old.gpuTimeNanoseconds)
        }
        return GPUActivityDeltas(elapsed: elapsed, nanoseconds: growth)
    }
}

/// GPU-time growth (ns) by process and GPU over `elapsed` seconds.
struct GPUActivityDeltas: Equatable {
    let elapsed: TimeInterval
    let nanoseconds: [Int32: [UInt64: UInt64]]

    /// The (distinct) processes' load: their growth summed per GPU over the elapsed
    /// time, at most 100% (overlapping command queues), the busiest GPU's
    /// figure (like GPUActivityDerivation.busiest). Processes without GPU
    /// clients are a real 0%.
    func percent<PIDs: Sequence>(pids: PIDs) -> Double? where PIDs.Element == Int32 {
        var busy: [UInt64: UInt64] = [:]
        for pid in pids {
            guard let gpus = nanoseconds[pid] else { continue }
            for (gpu, time) in gpus { busy[gpu, default: 0].saturatingAdd(time) }
        }
        return load(busy)
    }

    /// Every process's load together: the system total, by the same rule.
    func totalPercent() -> Double? {
        var busy: [UInt64: UInt64] = [:]
        for gpus in nanoseconds.values { for (gpu, time) in gpus { busy[gpu, default: 0].saturatingAdd(time) } }
        return load(busy)
    }

    private func load(_ busy: [UInt64: UInt64]) -> Double? {
        guard elapsed.isFinite, elapsed > 0 else { return nil }
        let percent = Double(busy.values.max() ?? 0) / (elapsed * 1_000_000_000) * 100
        return percent.isFinite ? min(100, max(0, percent)) : nil
    }
}

private extension UInt64 {
    mutating func saturatingAdd(_ value: UInt64) {
        let next = addingReportingOverflow(value); self = next.overflow ? .max : next.partialValue
    }
}

/// I/O Registry GPU reader. Both sources are undocumented driver properties
/// (the ones Activity Monitor's own GPU figures come from), so every value is
/// validated and anything missing is nil, never a fabricated zero:
///  - Per process (and the system total where it exists): the
///    accelerator's IOService children, one user client per
///    GPU device a process opened (AGXDeviceUserClient on Apple silicon),
///    naming its creator in "IOUserClientCreator" ("pid N, name") and
///    counting its GPU time in "AppUsage" ("accumulatedGPUTime" per command
///    queue, nanoseconds). Drivers without AppUsage give no per-app reading.
///  - Fallback total: each IOAccelerator service's "PerformanceStatistics"
///    (GPUActivityDerivation.utilizationKeys). Its "Device Utilization %"
///    is a short-window figure that read high at partial load on an M2
///    (about 83% for a GPU busy half the time), hence client time first.
/// Accelerator handles are matched once and kept, rematched every 30 s (an
/// eGPU attached or removed) and at most 5 s after a failed read. A total
/// reading is then one property read per GPU. A client walk visits each child
/// once: a client's creator is read once in its lifetime, its AppUsage only
/// while its PID is asked for (every client for the system total). Each
/// instance belongs to one serial sampling worker (the lock only guards
/// against misuse); nothing here touches main. The system and Apps readers
/// use separate instances, so the total never depends on the Apps panel.
final class GPUActivitySampler {
    /// SystemActivityReader's instance, used on SystemActivityMonitor's worker.
    static let system = GPUActivitySampler()

    /// The system total's reading: every client's GPU time where the driver
    /// keeps AppUsage, else the PerformanceStatistics utilisation figures.
    func systemReading() -> SystemActivityGPUReading? {
        if let clients = clients(pids: nil) { return SystemActivityGPUReading(utilizations: [], clients: clients) }
        return utilization()
    }
    static let rematchInterval: TimeInterval = 30
    static let maximumAccelerators = 8, maximumClients = 4096
    private struct Accelerator { let service: io_service_t; let id: UInt64 }
    private enum AppUsageSupport { case unknown, supported, unsupported }
    private let lock = NSLock()
    private var accelerators: [Accelerator] = []
    private var matchTime: TimeInterval = -.infinity
    private var nextMatch: TimeInterval = -.infinity
    private var creators: [UInt64: Int32] = [:] // client entry ID → creator PID (0: none)
    private var appUsage = AppUsageSupport.unknown

    deinit { accelerators.forEach { IOObjectRelease($0.service) } }

    /// One utilisation per GPU that reports one; nil when none does.
    func utilization() -> SystemActivityGPUReading? {
        lock.lock(); defer { lock.unlock() }
        matchIfNeeded()
        var values: [Double] = []
        for accelerator in accelerators {
            guard let statistics = IORegistryEntryCreateCFProperty(accelerator.service, "PerformanceStatistics" as CFString,
                                                                  kCFAllocatorDefault, 0)?.takeRetainedValue() as? NSDictionary else {
                retrySoon(); continue
            }
            if let value = GPUActivityDerivation.utilization(statistics) { values.append(value) }
        }
        return values.isEmpty ? nil : SystemActivityGPUReading(utilizations: values)
    }

    /// The cumulative GPU time of the given processes' clients (nil: every
    /// client, including any without a creator, which then has PID 0). nil
    /// where no client carries AppUsage (no per-process GPU time).
    func clients(pids: Set<Int32>?) -> GPUActivityClientReading? {
        lock.lock(); defer { lock.unlock() }
        matchIfNeeded()
        guard !accelerators.isEmpty, appUsage != .unsupported else { return nil }
        var result: [UInt64: GPUActivityClientUsage] = [:], live = Set<UInt64>(), visited = 0
        for accelerator in accelerators {
            var iterator: io_iterator_t = 0
            guard IORegistryEntryGetChildIterator(accelerator.service, kIOServicePlane, &iterator) == KERN_SUCCESS else {
                retrySoon(); return nil
            }
            defer { IOObjectRelease(iterator) }
            // A registry change mid-walk invalidates the iterator: walk once
            // more (entries are keyed by ID, so revisits are harmless).
            for _ in 0..<2 {
                while visited < Self.maximumClients {
                    let child = IOIteratorNext(iterator)
                    guard child != 0 else { break }
                    defer { IOObjectRelease(child) }
                    visited += 1
                    var id: UInt64 = 0
                    guard IORegistryEntryGetRegistryEntryID(child, &id) == KERN_SUCCESS else { continue }
                    live.insert(id)
                    let pid: Int32
                    if let known = creators[id] { pid = known } else {
                        let creator = IORegistryEntryCreateCFProperty(child, "IOUserClientCreator" as CFString,
                                                                      kCFAllocatorDefault, 0)?.takeRetainedValue() as? String
                        pid = creator.flatMap(GPUActivityDerivation.creatorPID) ?? 0
                        creators[id] = pid
                    }
                    let wanted = pids.map { pid > 0 && $0.contains(pid) } ?? true
                    // Until one client shows this driver keeps AppUsage, any client may.
                    guard wanted || (pid > 0 && appUsage == .unknown),
                          let time = GPUActivityDerivation.accumulatedGPUTime(IORegistryEntryCreateCFProperty(
                              child, "AppUsage" as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue()) else { continue }
                    appUsage = .supported
                    if wanted { result[id] = GPUActivityClientUsage(accelerator: accelerator.id, pid: pid, gpuTimeNanoseconds: time) }
                }
                guard IOIteratorIsValid(iterator) == 0 else { break }
                IOIteratorReset(iterator)
            }
        }
        if creators.count > live.count { creators = creators.filter { live.contains($0.key) } }
        guard appUsage == .supported else { appUsage = .unsupported; return nil }
        return GPUActivityClientReading(uptime: ProcessInfo.processInfo.systemUptime, clients: result)
    }

    /// Drops every handle and cache (the Apps panel closed); the next read rematches.
    func reset() {
        lock.lock(); defer { lock.unlock() }
        accelerators.forEach { IOObjectRelease($0.service) }
        accelerators = []; creators = [:]; appUsage = .unknown; matchTime = -.infinity; nextMatch = -.infinity
    }

    private func retrySoon() { nextMatch = min(nextMatch, matchTime + 5) }

    private func matchIfNeeded() {
        let now = ProcessInfo.processInfo.systemUptime
        guard now >= nextMatch else { return }
        var matched: [Accelerator] = [], iterator: io_iterator_t = 0
        if IOServiceGetMatchingServices(0, IOServiceMatching("IOAccelerator"), &iterator) == KERN_SUCCESS {
            while true {
                let service = IOIteratorNext(iterator)
                guard service != 0 else { break }
                var id: UInt64 = 0
                if matched.count < Self.maximumAccelerators, IORegistryEntryGetRegistryEntryID(service, &id) == KERN_SUCCESS {
                    matched.append(Accelerator(service: service, id: id))
                } else { IOObjectRelease(service) }
            }
            IOObjectRelease(iterator)
        }
        // A changed GPU set, or a driver found without AppUsage, is probed again.
        if matched.map(\.id) != accelerators.map(\.id) || appUsage == .unsupported { appUsage = .unknown }
        accelerators.forEach { IOObjectRelease($0.service) }
        accelerators = matched; matchTime = now
        nextMatch = now + (matched.isEmpty ? 5 : Self.rematchInterval)
    }
}
