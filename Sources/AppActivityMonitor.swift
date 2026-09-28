import AppKit
import Darwin

enum AppActivitySortKey: String, CaseIterable {
    case name, cpu, memory, network, disk, upload, download, diskRead, diskWrite
}

struct AppActivityItem: Equatable {
    let id: String
    let name: String
    let bundleIdentifier: String?
    let bundleURL: URL?
    let processIDs: [Int32]
    /// Like Activity Monitor: 100% is one logical CPU, not the whole machine.
    let cpuPercent: Double?
    let memoryBytes: UInt64?
    let uploadBytesPerSecond: Double?
    let downloadBytesPerSecond: Double?
    let diskReadBytesPerSecond: Double?
    let diskWriteBytesPerSecond: Double?
    let statusNotes: [String]
}

struct AppActivitySnapshot: Equatable {
    let timestamp: Date
    let uptime: TimeInterval
    let items: [AppActivityItem]
    let statusNotes: [String]
    static let empty = AppActivitySnapshot(timestamp: .distantPast, uptime: 0, items: [], statusNotes: ["Waiting for app readings"])
}

struct AppActivityIdentity: Equatable {
    let id: String
    let name: String
    let bundleIdentifier: String?
    let bundleURL: URL?
    let processIDs: [Int32]
}

struct AppActivityProcessReading: Equatable {
    let pid: Int32
    let startID: UInt64
    let startUptime: TimeInterval
    let cpuTicks: UInt64
    let memoryBytes: UInt64
    let diskReadBytes: UInt64
    let diskWriteBytes: UInt64
}

struct AppActivityNetworkRates: Equatable {
    let received: Double
    let sent: Double
}

struct AppActivityNetworkReading {
    let uptime: TimeInterval
    let intervalStart: TimeInterval
    let rates: [Int32: AppActivityNetworkRates]
    let unknownPIDs: Set<Int32>
}

struct AppActivityRawSample {
    let timestamp: Date
    let uptime: TimeInterval
    let apps: [AppActivityIdentity]
    let members: [String: [Int32]]
    let processes: [Int32: AppActivityProcessReading]
    let secondsPerCPUTick: Double
    let network: AppActivityNetworkReading?
    let statusNotes: [String]
    var inventoryComplete = true
}

enum AppActivityDerivation {
    static func snapshot(_ current: AppActivityRawSample, previous: AppActivityRawSample?) -> AppActivitySnapshot {
        let elapsed = previous.map { current.uptime - $0.uptime } ?? 0
        let intervalValid = elapsed.isFinite && elapsed > 0 && elapsed <= 5
        let items = current.apps.map { app -> AppActivityItem in
            let pids = Array(Set(current.members[app.id] ?? app.processIDs)).sorted()
            let readings = pids.compactMap { current.processes[$0] }
            let complete = current.inventoryComplete && !pids.isEmpty && readings.count == pids.count
            let priorPIDs = Set(previous?.members[app.id] ?? [])
            let sameMembership = Set(pids) == priorPIDs
            var cpu: Double?, memory: UInt64?, read: Double?, write: Double?, upload: Double?, download: Double?
            var notes: [String] = []
            if complete {
                memory = SystemActivityDerivation.sum(readings.map(\.memoryBytes))
                if intervalValid, sameMembership, current.secondsPerCPUTick.isFinite, current.secondsPerCPUTick > 0 {
                    var ticks: [UInt64] = [], reads: [UInt64] = [], writes: [UInt64] = []
                    var valid = true
                    for value in readings {
                        guard let old = previous?.processes[value.pid], old.startID == value.startID,
                              value.cpuTicks >= old.cpuTicks, value.diskReadBytes >= old.diskReadBytes,
                              value.diskWriteBytes >= old.diskWriteBytes else { valid = false; break }
                        ticks.append(value.cpuTicks - old.cpuTicks)
                        reads.append(value.diskReadBytes - old.diskReadBytes); writes.append(value.diskWriteBytes - old.diskWriteBytes)
                    }
                    if valid {
                        if let total = SystemActivityDerivation.sum(ticks) {
                            let percent = Double(total) * current.secondsPerCPUTick / elapsed * 100
                            if percent.isFinite { cpu = max(0, percent) }
                        }
                        read = SystemActivityDerivation.sum(reads).map { Double($0) / elapsed }
                        write = SystemActivityDerivation.sum(writes).map { Double($0) / elapsed }
                    }
                }
                if let network = current.network, current.uptime - network.uptime >= 0,
                   current.uptime - network.uptime <= 4,
                   readings.allSatisfy({ $0.startUptime <= network.intervalStart && !network.unknownPIDs.contains($0.pid) }) {
                    // A PID absent in both complete nettop frames had no listed
                    // TCP/UDP sockets. Added/removed socket owners are unknown.
                    let values = pids.map { network.rates[$0] ?? AppActivityNetworkRates(received: 0, sent: 0) }
                    let input = values.reduce(0) { $0 + $1.received }, output = values.reduce(0) { $0 + $1.sent }
                    if input.isFinite && output.isFinite { download = input; upload = output }
                }
            }
            if !complete { notes.append("Some app processes are unavailable or protected") }
            else if cpu == nil { notes.append("Collecting app baseline") }
            if memory == nil { notes.append("Memory unavailable") }
            if download == nil { notes.append("Network unavailable or collecting baseline") }
            return AppActivityItem(id: app.id, name: app.name, bundleIdentifier: app.bundleIdentifier,
                bundleURL: app.bundleURL, processIDs: pids, cpuPercent: cpu, memoryBytes: memory,
                uploadBytesPerSecond: upload, downloadBytesPerSecond: download,
                diskReadBytesPerSecond: read, diskWriteBytesPerSecond: write, statusNotes: notes)
        }
        return AppActivitySnapshot(timestamp: current.timestamp, uptime: current.uptime, items: items, statusNotes: current.statusNotes)
    }

    static func sorted(_ items: [AppActivityItem], by key: AppActivitySortKey, descending: Bool) -> [AppActivityItem] {
        func combinedRate(_ first: Double?, _ second: Double?) -> Double? {
            guard let first, let second, first.isFinite, second.isFinite, first >= 0, second >= 0 else { return nil }
            let total = first + second
            return total.isFinite ? total : nil
        }
        func number(_ item: AppActivityItem) -> Double? {
            switch key {
            case .name: return nil
            case .cpu: return item.cpuPercent
            case .memory: return item.memoryBytes.map { Double($0) }
            case .network: return combinedRate(item.uploadBytesPerSecond, item.downloadBytesPerSecond)
            case .disk: return combinedRate(item.diskReadBytesPerSecond, item.diskWriteBytesPerSecond)
            case .upload: return item.uploadBytesPerSecond
            case .download: return item.downloadBytesPerSecond
            case .diskRead: return item.diskReadBytesPerSecond
            case .diskWrite: return item.diskWriteBytesPerSecond
            }
        }
        return items.sorted { lhs, rhs in
            if key != .name {
                let a = number(lhs), b = number(rhs)
                if let a, let b, a != b { return descending ? a > b : a < b }
                if (a == nil) != (b == nil) { return a != nil } // Unknown always last.
            }
            let names = lhs.name.localizedStandardCompare(rhs.name)
            if names != .orderedSame { return key == .name && descending ? names == .orderedDescending : names == .orderedAscending }
            return lhs.id < rhs.id
        }
    }
}

protocol AppActivityBackend: AnyObject {
    func start()
    func stop()
    func sample(apps: [AppActivityIdentity]) -> AppActivityRawSample
}

/// Application detail is opt-in via visibility: 1 Hz libproc readings and one
/// 1 Hz nettop summary stream, both entirely stopped when that detail is hidden.
final class AppActivityMonitor {
    private(set) var snapshot = AppActivitySnapshot.empty
    private(set) var isActive = false
    private(set) var sortKey: AppActivitySortKey = .cpu
    private(set) var sortDescending = true
    private let catalog: () -> [AppActivityIdentity]
    private let schedule: SystemActivityMonitor.Schedule
    private let worker = DispatchQueue(label: "EndfieldCharge.activity.apps", qos: .utility)
    private let state: WorkerState
    private var observers: [UUID: (AppActivitySnapshot) -> Void] = [:]
    private var cancelTimer: (() -> Void)?
    private var generation = 0
    private var inFlight = false
    private var needsRestart = false
    private var lastCatalog: [AppActivityIdentity] = []
    private var catalogTime: TimeInterval = -.infinity
    private var fixedFixture = false

    init(catalog: @escaping () -> [AppActivityIdentity] = AppActivityCatalog.read,
         backend: AppActivityBackend = PublicAppActivityBackend(),
         schedule: @escaping SystemActivityMonitor.Schedule = SystemActivityMonitor.scheduleEverySecond) {
        self.catalog = catalog; self.schedule = schedule; state = WorkerState(backend: backend)
    }

    @discardableResult func observe(_ callback: @escaping (AppActivitySnapshot) -> Void) -> UUID {
        precondition(Thread.isMainThread)
        let id = UUID(); observers[id] = callback; callback(snapshot); return id
    }
    func removeObserver(_ id: UUID) { precondition(Thread.isMainThread); observers.removeValue(forKey: id) }

    func setSort(_ key: AppActivitySortKey, descending: Bool = true) {
        precondition(Thread.isMainThread)
        guard sortKey != key || sortDescending != descending else { return }
        sortKey = key; sortDescending = descending
        publish(snapshot)
    }

    func activate() {
        precondition(Thread.isMainThread)
        guard !isActive else { return }
        isActive = true; generation += 1; catalogTime = -.infinity
        guard !fixedFixture else { return }
        let token = generation
        cancelTimer = schedule { [weak self] in
            guard let self, self.isActive, self.generation == token else { return }; self.requestSample()
        }
        needsRestart = true; requestSample()
    }

    func deactivate() {
        precondition(Thread.isMainThread)
        guard isActive else { return }
        isActive = false; generation += 1; needsRestart = false
        cancelTimer?(); cancelTimer = nil
        let state = self.state; worker.async { state.stop() }
    }

    private func requestSample() {
        guard isActive, !inFlight else { return }
        inFlight = true; needsRestart = false
        let now = ProcessInfo.processInfo.systemUptime
        if now - catalogTime >= 5 { lastCatalog = Array(catalog().prefix(256)); catalogTime = now }
        let apps = lastCatalog, token = generation, state = self.state
        worker.async { [weak self] in
            let result = state.read(apps: apps, generation: token)
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }; self.inFlight = false
                guard self.isActive else { return }
                guard self.generation == token else {
                    if self.needsRestart { self.requestSample() }; return
                }
                self.publish(result)
            }
        }
    }

    private func publish(_ value: AppActivitySnapshot) {
        snapshot = AppActivitySnapshot(timestamp: value.timestamp, uptime: value.uptime,
            items: AppActivityDerivation.sorted(value.items, by: sortKey, descending: sortDescending), statusNotes: value.statusNotes)
        let token = generation
        for (id, action) in Array(observers) where observers[id] != nil {
            guard generation == token else { break }; action(snapshot)
        }
    }

    deinit {
        cancelTimer?()
        let state = self.state; worker.async { state.stop() }
    }

    static func fixture() -> AppActivityMonitor {
        let monitor = AppActivityMonitor(catalog: { [] }, backend: FixtureBackend(), schedule: { _ in {} })
        monitor.fixedFixture = true
        let items = [("Finder", "com.apple.finder", 2.4, 82_000_000), ("Safari", "com.apple.Safari", 18.6, 720_000_000),
                     ("Music", "com.apple.Music", 4.1, 164_000_000)].enumerated().map { index, app in
            AppActivityItem(id: app.1, name: app.0, bundleIdentifier: app.1, bundleURL: nil,
                processIDs: [Int32(index + 100)], cpuPercent: app.2, memoryBytes: UInt64(app.3),
                uploadBytesPerSecond: Double(index * 900), downloadBytesPerSecond: Double(index * 82_000),
                diskReadBytesPerSecond: Double(index * 200_000), diskWriteBytesPerSecond: Double(index * 15_000), statusNotes: [])
        }
        monitor.publish(AppActivitySnapshot(timestamp: Date(timeIntervalSince1970: 1_700_000_000), uptime: 1000, items: items, statusNotes: []))
        return monitor
    }

    private final class WorkerState {
        let backend: AppActivityBackend
        var generation: Int?
        var previous: AppActivityRawSample?
        init(backend: AppActivityBackend) { self.backend = backend }
        func read(apps: [AppActivityIdentity], generation: Int) -> AppActivitySnapshot {
            if self.generation != generation { stop(); self.generation = generation; backend.start() }
            let current = backend.sample(apps: apps)
            let result = AppActivityDerivation.snapshot(current, previous: previous)
            previous = current; return result
        }
        func stop() { backend.stop(); generation = nil; previous = nil }
    }
    private final class FixtureBackend: AppActivityBackend {
        func start() {}
        func stop() {}
        func sample(apps: [AppActivityIdentity]) -> AppActivityRawSample {
            AppActivityRawSample(timestamp: .distantPast, uptime: 0, apps: [], members: [:], processes: [:],
                                 secondsPerCPUTick: 1, network: nil, statusNotes: [])
        }
    }
}

enum AppActivityCatalog {
    /// Read AppKit metadata on main, at most once per five seconds. Icon loading
    /// is left to the view's bundle-URL cache, rather than decoding every tick.
    static func read() -> [AppActivityIdentity] {
        precondition(Thread.isMainThread)
        var groups: [String: AppActivityIdentity] = [:]
        for app in NSWorkspace.shared.runningApplications where !app.isTerminated {
            guard let url = app.bundleURL, let root = outerAppURL(url), app.processIdentifier > 0 else { continue }
            let id = root.path
            let prior = groups[id]
            let isMainBundle = url.standardizedFileURL == root
            let bundle = isMainBundle ? nil : Bundle(url: root)
            let name = isMainBundle ? app.localizedName :
                (bundle?.object(forInfoDictionaryKey: "CFBundleDisplayName") as? String
                 ?? bundle?.object(forInfoDictionaryKey: "CFBundleName") as? String)
            let pids = Array(Set((prior?.processIDs ?? []) + [app.processIdentifier])).sorted()
            groups[id] = AppActivityIdentity(id: id,
                name: (isMainBundle ? name : prior?.name ?? name) ?? root.deletingPathExtension().lastPathComponent,
                bundleIdentifier: (isMainBundle ? app.bundleIdentifier : prior?.bundleIdentifier ?? bundle?.bundleIdentifier),
                bundleURL: root, processIDs: pids)
        }
        return groups.values.sorted { $0.id < $1.id }
    }

    static func outerAppURL(_ url: URL) -> URL? {
        guard url.isFileURL else { return nil }
        var root = URL(fileURLWithPath: "/", isDirectory: true)
        for component in url.standardizedFileURL.pathComponents.dropFirst() {
            root.appendPathComponent(component)
            if component.lowercased().hasSuffix(".app") { return root }
        }
        return nil
    }
}

final class PublicAppActivityBackend: AppActivityBackend {
    private let network = AppActivityNetworkStream()
    private var owners: [Int32: String] = [:]
    private var lastStarts: [Int32: UInt64] = [:]
    private var inventoryTime: TimeInterval = -.infinity
    private var catalogIDs: [String] = []
    private var inventoryAvailable = true
    private static let secondsPerTick: Double = {
        var timebase = mach_timebase_info_data_t(); mach_timebase_info(&timebase)
        return Double(timebase.numer) / Double(timebase.denom) / 1_000_000_000
    }()

    func start() { inventoryTime = -.infinity; network.start() }
    func stop() { network.stop(); owners.removeAll(); lastStarts.removeAll(); catalogIDs = []; inventoryTime = -.infinity }

    func sample(apps: [AppActivityIdentity]) -> AppActivityRawSample {
        let now = ProcessInfo.processInfo.systemUptime
        let ids = apps.map(\.id)
        if now - inventoryTime >= 5 || ids != catalogIDs {
            inventoryTime = now; catalogIDs = ids
            if let pids = Self.processIDs() {
                var next: [Int32: String] = [:]
                for pid in pids {
                    if let path = Self.path(pid: pid), let owner = Self.owner(path: path, apps: apps) { next[pid] = owner }
                }
                owners = next; inventoryAvailable = true
            } else { inventoryAvailable = false; owners = [:] }
        }
        // A protected path lookup need not hide a known running app itself.
        for app in apps { for pid in app.processIDs { owners[pid] = app.id } }
        let validIDs = Set(ids)
        owners = owners.filter { validIDs.contains($0.value) }
        var members: [String: [Int32]] = [:], readings: [Int32: AppActivityProcessReading] = [:]
        for (pid, owner) in Array(owners) {
            guard let reading = Self.readProcess(pid: pid) else {
                // A missing/protected process remains unknown until the bounded
                // inventory refresh distinguishes exit from access denial.
                members[owner, default: []].append(pid); continue
            }
            if let start = lastStarts[pid], start != reading.startID {
                guard let path = Self.path(pid: pid), Self.owner(path: path, apps: apps) == owner else {
                    owners.removeValue(forKey: pid); lastStarts.removeValue(forKey: pid); continue
                }
            }
            lastStarts[pid] = reading.startID
            members[owner, default: []].append(pid); readings[pid] = reading
        }
        lastStarts = lastStarts.filter { owners[$0.key] != nil }
        let tick = ProcessInfo.processInfo.systemUptime
        var notes = inventoryAvailable ? [] : ["App helper inventory unavailable"]
        let traffic = network.read(at: tick)
        if traffic == nil { notes.append("Network: waiting for macOS per-process counters") }
        return AppActivityRawSample(timestamp: Date(), uptime: tick, apps: apps, members: members, processes: readings,
            secondsPerCPUTick: Self.secondsPerTick, network: traffic, statusNotes: notes, inventoryComplete: inventoryAvailable)
    }

    static func owner(path: String, apps: [AppActivityIdentity]) -> String? {
        apps.filter { app in
            guard let root = app.bundleURL?.path else { return false }
            return path.hasPrefix(root + "/")
        }.max(by: { ($0.bundleURL?.path.count ?? 0) < ($1.bundleURL?.path.count ?? 0) })?.id
    }

    static func readProcess(pid: Int32) -> AppActivityProcessReading? {
        guard pid > 0 else { return nil }
        var usage = rusage_info_v2()
        let status = withUnsafeMutablePointer(to: &usage) {
            $0.withMemoryRebound(to: rusage_info_t?.self, capacity: 1) { proc_pid_rusage(pid, RUSAGE_INFO_V2, $0) }
        }
        guard status == 0, usage.ri_proc_start_abstime > 0,
              let cpu = SystemActivityDerivation.sum([usage.ri_user_time, usage.ri_system_time]) else { return nil }
        return AppActivityProcessReading(pid: pid, startID: usage.ri_proc_start_abstime,
            startUptime: Double(usage.ri_proc_start_abstime) * secondsPerTick,
            cpuTicks: cpu, memoryBytes: usage.ri_phys_footprint,
            diskReadBytes: usage.ri_diskio_bytesread, diskWriteBytes: usage.ri_diskio_byteswritten)
    }

    private static func processIDs() -> [Int32]? {
        var pids = [Int32](repeating: 0, count: 8192)
        let size = Int32(pids.count * MemoryLayout<Int32>.size)
        let bytes = proc_listpids(UInt32(PROC_UID_ONLY), getuid(), &pids, size)
        guard bytes > 0, bytes < size, bytes % Int32(MemoryLayout<Int32>.size) == 0 else { return nil }
        return Array(Set(pids.prefix(Int(bytes) / MemoryLayout<Int32>.size).filter { $0 > 0 })).sorted()
    }

    private static func path(pid: Int32) -> String? {
        // PROC_PIDPATHINFO_MAXSIZE is the C macro 4 * MAXPATHLEN; Swift cannot
        // import its parenthesized macro expansion from this SDK.
        var buffer = [CChar](repeating: 0, count: 4 * Int(MAXPATHLEN))
        let count = proc_pidpath(pid, &buffer, UInt32(buffer.count))
        return count > 0 ? String(cString: buffer) : nil
    }
}

/// Bounded decoder for the documented nettop CSV per-process summary. It never
/// receives socket endpoints (-P) or resolves addresses (-n). Only the next CSV
/// header commits a complete frame; arbitrary pipe chunk boundaries never do.
final class AppActivityNetworkDecoder {
    private var buffer = Data()
    private var current: [Int32: SystemActivityByteCounters] = [:]
    private var collecting = false
    private var invalid = false
    private var frameTime: TimeInterval = 0
    private var timedFrame = false
    private var mappedFrameTime = false
    private var wallSeconds: Double?
    private var previous: (time: TimeInterval, values: [Int32: SystemActivityByteCounters])?
    private(set) var latest: AppActivityNetworkReading?

    func append(_ data: Data, at uptime: TimeInterval, wallClockSeconds: Double? = nil) {
        guard data.count <= 1_048_576, buffer.count <= 1_048_576 - data.count else { reset(); return }
        buffer.append(data); wallSeconds = wallClockSeconds
        var consumed = buffer.startIndex
        while let newline = buffer[consumed...].firstIndex(of: 10) {
            let line = buffer[consumed..<newline]
            consumed = buffer.index(after: newline)
            guard line.count <= 16_384, let text = String(data: line, encoding: .utf8) else { invalid = true; continue }
            consume(text.trimmingCharacters(in: .newlines), at: uptime)
        }
        // Compact once per received chunk. Removing every line individually
        // repeatedly moved the entire remaining nettop frame in memory.
        if consumed != buffer.startIndex { buffer.removeSubrange(buffer.startIndex..<consumed) }
    }

    func read(at uptime: TimeInterval) -> AppActivityNetworkReading? {
        guard let latest, uptime >= latest.uptime, uptime - latest.uptime <= 4 else { return nil }
        return latest
    }

    func reset() {
        buffer.removeAll(keepingCapacity: false); current = [:]; collecting = false
        invalid = false; previous = nil; latest = nil
    }

    private func consume(_ line: String, at uptime: TimeInterval) {
        if line.hasPrefix(",bytes_in,bytes_out") || line.hasPrefix("time,,bytes_in,bytes_out") {
            if collecting { finish() }
            current = [:]; collecting = true; invalid = false; frameTime = uptime
            timedFrame = line.hasPrefix("time,"); mappedFrameTime = false; return
        }
        guard collecting, !line.isEmpty else { return }
        var columns = line.split(separator: ",", omittingEmptySubsequences: false).map(String.init)
        while columns.last == "" { columns.removeLast() }
        if timedFrame {
            guard !columns.isEmpty, let time = Self.secondsOfDay(columns.removeFirst()), let wallSeconds else { invalid = true; return }
            if !mappedFrameTime {
                var age = wallSeconds - time
                if age < -43_200 { age += 86_400 }; if age > 43_200 { age -= 86_400 }
                // Map the producer timestamp onto monotonic uptime. This also
                // handles several frames delivered together by stdio buffering.
                guard age >= -0.1, age <= 10 else { invalid = true; return }
                frameTime = uptime - max(0, age); mappedFrameTime = true
            }
        }
        guard columns.count >= 3, let sent = UInt64(columns.removeLast()), let received = UInt64(columns.removeLast()),
              let pidText = columns.joined(separator: ",").split(separator: ".").last,
              let pid = Int32(pidText), pid > 0, current[pid] == nil, current.count < 8192 else { invalid = true; return }
        current[pid] = SystemActivityByteCounters(received: received, sent: sent)
    }

    static func secondsOfDay(_ text: String) -> Double? {
        let parts = text.split(separator: ":")
        guard parts.count == 3, let hours = Int(parts[0]), let minutes = Int(parts[1]), let seconds = Double(parts[2]),
              (0..<24).contains(hours), (0..<60).contains(minutes), seconds >= 0, seconds < 60 else { return nil }
        return Double(hours * 3600 + minutes * 60) + seconds
    }

    private func finish() {
        defer { collecting = false; current = [:] }
        guard !invalid else { previous = nil; latest = nil; return }
        defer { previous = (frameTime, current) }
        guard let prior = previous else { return }
        let elapsed = frameTime - prior.time
        guard elapsed > 0, elapsed <= 5 else { latest = nil; return }
        var rates: [Int32: AppActivityNetworkRates] = [:]
        var unknown = Set(current.keys).symmetricDifference(Set(prior.values.keys))
        for (pid, counters) in current {
            guard let old = prior.values[pid] else { continue }
            guard counters.received >= old.received, counters.sent >= old.sent else { unknown.insert(pid); continue }
            rates[pid] = AppActivityNetworkRates(received: Double(counters.received - old.received) / elapsed,
                                                 sent: Double(counters.sent - old.sent) / elapsed)
        }
        latest = AppActivityNetworkReading(uptime: frameTime, intervalStart: prior.time, rates: rates, unknownPIDs: unknown)
    }
}

/// One child process, owned only for the visible Apps panel. There is no shell,
/// elevated permission, private NetworkStatistics framework or repeated spawn.
final class AppActivityNetworkStream {
    private let lock = NSLock()
    private var decoder = AppActivityNetworkDecoder()
    private var process: Process?
    private var source: DispatchSourceRead?
    private let inputQueue = DispatchQueue(label: "EndfieldCharge.activity.appNetwork", qos: .utility)
    private var generation = 0

    func start() {
        stop()
        // nettop block-buffers ordinary pipes, delaying several samples. A
        // private POSIX pseudo-terminal keeps its documented -L CSV line
        // buffered without a shell, interactive UI or repeated child launches.
        var master: Int32 = -1, slave: Int32 = -1
        guard openpty(&master, &slave, nil, nil, nil) == 0 else { return }
        _ = fcntl(master, F_SETFD, FD_CLOEXEC); _ = fcntl(slave, F_SETFD, FD_CLOEXEC)
        _ = fcntl(master, F_SETFL, O_NONBLOCK)
        let output = FileHandle(fileDescriptor: slave, closeOnDealloc: true)
        let child = Process()
        child.executableURL = URL(fileURLWithPath: "/usr/bin/nettop")
        child.arguments = ["-P", "-L", "0", "-s", "1", "-n", "-x", "-t", "external", "-J", "time,bytes_in,bytes_out"]
        child.standardInput = FileHandle.nullDevice
        child.standardOutput = output; child.standardError = FileHandle.nullDevice
        let input = DispatchSource.makeReadSource(fileDescriptor: master, queue: inputQueue)
        lock.lock(); generation += 1; let token = generation; process = child; source = input; lock.unlock()
        input.setEventHandler { [weak self] in self?.receive(master, token: token) }
        input.setCancelHandler { Darwin.close(master) }
        input.resume()
        child.terminationHandler = { [weak self] _ in
            guard let self else { return }; self.lock.lock(); defer { self.lock.unlock() }
            if self.generation == token { self.decoder.reset(); self.source?.cancel() }
        }
        do { try child.run() } catch { stop() }
        try? output.close()
    }

    private func receive(_ descriptor: Int32, token: Int) {
        var bytes = [UInt8](repeating: 0, count: 65_536)
        // Bound one callback so unexpected child output cannot starve cleanup.
        for _ in 0..<8 {
            let count = Darwin.read(descriptor, &bytes, bytes.count)
            if count < 0 && (errno == EAGAIN || errno == EINTR) { return }
            lock.lock()
            guard generation == token else { lock.unlock(); return }
            if count <= 0 { decoder.reset(); source?.cancel(); lock.unlock(); return }
            let components: Set<Calendar.Component> = [.hour, .minute, .second, .nanosecond]
            let date = Calendar.current.dateComponents(components, from: Date())
            let hours = Double(date.hour ?? 0) * 3600
            let minutes = Double(date.minute ?? 0) * 60
            let seconds = Double(date.second ?? 0) + Double(date.nanosecond ?? 0) / 1_000_000_000
            decoder.append(Data(bytes.prefix(count)), at: ProcessInfo.processInfo.systemUptime,
                           wallClockSeconds: hours + minutes + seconds)
            lock.unlock()
        }
    }

    func read(at uptime: TimeInterval) -> AppActivityNetworkReading? {
        lock.lock(); defer { lock.unlock() }; return decoder.read(at: uptime)
    }

    func stop() {
        lock.lock(); generation += 1
        let child = process, input = source
        process = nil; source = nil; decoder.reset(); lock.unlock()
        input?.cancel()
        child?.terminationHandler = nil
        if child?.isRunning == true { child?.terminate() }
    }

    deinit { stop() }
}
