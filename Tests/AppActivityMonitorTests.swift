import Foundation

enum AppActivityMonitorTests {
    static func run() -> Int {
        var count = 0
        func check(_ result: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !result { fatalError(message, file: file, line: line) }
        }
        func wait(_ predicate: () -> Bool) -> Bool {
            let end = Date().addingTimeInterval(3)
            while !predicate(), Date() < end { RunLoop.current.run(until: Date().addingTimeInterval(0.003)) }
            return predicate()
        }
        func drain() { RunLoop.current.run(until: Date().addingTimeInterval(0.04)) }
        let app = AppActivityIdentity(id: "demo", name: "Demo", bundleIdentifier: "org.demo", bundleURL: URL(fileURLWithPath: "/Applications/Demo.app"), processIDs: [1])
        func process(_ pid: Int32 = 1, start: UInt64 = 10, ticks: UInt64 = 0, memory: UInt64 = 1_000,
                     read: UInt64 = 0, write: UInt64 = 0) -> AppActivityProcessReading {
            AppActivityProcessReading(pid: pid, startID: start, startUptime: Double(start), cpuTicks: ticks,
                                      memoryBytes: memory, diskReadBytes: read, diskWriteBytes: write)
        }
        func raw(_ time: Double, processes: [AppActivityProcessReading], members: [Int32]? = nil,
                 network: AppActivityNetworkReading? = nil) -> AppActivityRawSample {
            AppActivityRawSample(timestamp: Date(timeIntervalSince1970: time), uptime: time, apps: [app],
                members: [app.id: members ?? processes.map(\.pid)], processes: Dictionary(uniqueKeysWithValues: processes.map { ($0.pid, $0) }),
                secondsPerCPUTick: 0.001, network: network, statusNotes: [])
        }
        let first = raw(20, processes: [process(), process(2, ticks: 100, memory: 2000)])
        let second = raw(22, processes: [process(ticks: 2500, read: 200, write: 400), process(2, ticks: 1600, memory: 2500, read: 600, write: 800)],
            network: AppActivityNetworkReading(uptime: 21, intervalStart: 20,
                rates: [1: AppActivityNetworkRates(received: 100, sent: 200), 2: AppActivityNetworkRates(received: 300, sent: 400)], unknownPIDs: []))
        let initial = AppActivityDerivation.snapshot(first, previous: nil).items[0]
        check(initial.cpuPercent == nil && initial.diskReadBytesPerSecond == nil && initial.memoryBytes == 3000,
              "Initial app readings expose memory but require a real CPU/disk baseline")
        let next = AppActivityDerivation.snapshot(second, previous: first).items[0]
        check(next.cpuPercent == 200, "App CPU converts Mach ticks and can exceed100% for multiple busy cores")
        check(next.memoryBytes == 3500 && next.processIDs == [1, 2], "Helpers aggregate once beneath the outer app identity")
        check(next.diskReadBytesPerSecond == 400 && next.diskWriteBytesPerSecond == 600,
              "App disk rates sum stable process deltas using elapsed seconds")
        check(next.downloadBytesPerSecond == 400 && next.uploadBytesPerSecond == 600,
              "App network sums per-PID summaries from a complete fresh sample")
        let replaced = AppActivityDerivation.snapshot(raw(22, processes: [process(start: 11, ticks: 2500)]), previous: raw(20, processes: [process()])).items[0]
        check(replaced.cpuPercent == nil && replaced.diskReadBytesPerSecond == nil, "PID reuse cannot attribute previous process CPU or disk")
        let changed = AppActivityDerivation.snapshot(raw(22, processes: [process(ticks: 20)]), previous: first).items[0]
        check(changed.cpuPercent == nil && changed.memoryBytes == 1000, "Helper exits rebaseline rates while preserving current footprint")
        let denied = AppActivityDerivation.snapshot(raw(22, processes: [process()], members: [1, 2]), previous: first).items[0]
        check(denied.cpuPercent == nil && denied.memoryBytes == nil && denied.diskReadBytesPerSecond == nil,
              "Permission-limited app readings remain unavailable rather than partial totals")
        var incomplete = second; incomplete.inventoryComplete = false
        let incompleteItem = AppActivityDerivation.snapshot(incomplete, previous: first).items[0]
        check(incompleteItem.cpuPercent == nil && incompleteItem.memoryBytes == nil && incompleteItem.downloadBytesPerSecond == nil,
              "A failed helper inventory never promotes main-process-only counters to complete app totals")
        let rolled = AppActivityDerivation.snapshot(raw(23, processes: [process(ticks: 1), process(2, ticks: 1)]), previous: second).items[0]
        check(rolled.cpuPercent == nil && rolled.diskWriteBytesPerSecond == nil, "Rollback never creates negative or wrapped app rates")
        let missingNetwork = AppActivityDerivation.snapshot(raw(22, processes: [process()]), previous: first).items[0]
        check(missingNetwork.downloadBytesPerSecond == nil, "Unavailable network is distinct from idle")
        for network in [AppActivityNetworkReading(uptime: 12, intervalStart: 11, rates: [:], unknownPIDs: []),
                        AppActivityNetworkReading(uptime: 21, intervalStart: 20, rates: [:], unknownPIDs: [1]),
                        AppActivityNetworkReading(uptime: 21, intervalStart: 5, rates: [:], unknownPIDs: [])] {
            check(AppActivityDerivation.snapshot(raw(22, processes: [process()], network: network), previous: first).items[0].uploadBytesPerSecond == nil,
                  "Stale network, new socket owners and processes born after the traffic interval stay unknown")
        }
        let idle = AppActivityDerivation.snapshot(raw(22, processes: [process()],
            network: AppActivityNetworkReading(uptime: 21, intervalStart: 20, rates: [:], unknownPIDs: [])), previous: first).items[0]
        check(idle.downloadBytesPerSecond == 0, "No sockets in two complete frames is a genuine idle network reading")
        check(AppActivityCatalog.outerAppURL(URL(fileURLWithPath: "/Applications/Demo.app/Contents/Frameworks/Demo Helper.app"))?.path == "/Applications/Demo.app",
              "Embedded helper apps normalize to their original app bundle")
        check(AppActivityCatalog.outerAppURL(URL(fileURLWithPath: "/usr/bin/demo")) == nil,
              "Non-app executables are not invented as user application bundles")
        check(PublicAppActivityBackend.owner(path: "/Applications/Demo.app/Contents/Frameworks/helper", apps: [app]) == app.id,
              "Bundle-contained non-AppKit helper processes attach to the owning app")
        check(PublicAppActivityBackend.owner(path: "/Applications/Demo.app.extra/helper", apps: [app]) == nil,
              "A prefix lookalike outside the app directory cannot be misattributed")

        let fixture = AppActivityMonitor.fixture()
        var sortNotifications = 0
        let sortObserver = fixture.observe { _ in sortNotifications += 1 }
        fixture.setSort(.cpu)
        check(sortNotifications == 1, "Reapplying the current app sort does not republish or rebuild unchanged rows")
        fixture.setSort(.memory, descending: true)
        check(fixture.snapshot.items.map(\.name) == ["Safari", "Music", "Finder"], "Memory sort compares numeric byte values, not UInt64 bit-pattern Double initializers")
        fixture.setSort(.memory, descending: false)
        check(fixture.snapshot.items.map(\.name) == ["Finder", "Music", "Safari"], "Ascending memory reverses real byte values")
        fixture.setSort(.name, descending: false)
        check(fixture.snapshot.items.map(\.name) == ["Finder", "Music", "Safari"], "Name sort is deterministic and localized")
        let beforeRepeatedSort = sortNotifications
        for _ in 0..<30 { fixture.setSort(.name, descending: false) }
        check(sortNotifications == beforeRepeatedSort, "Repeated panel activation with identical sort settings stays quiet")
        fixture.removeObserver(sortObserver)
        for key in AppActivitySortKey.allCases {
            let sorted = AppActivityDerivation.sorted([initial, next], by: key, descending: false)
            check(sorted.count == 2, "Each exposed sort key retains all app rows")
            if key != .name && key != .memory {
                check(sorted.first == next, "Unavailable metrics sort last, including ascending order")
            }
        }
        func traffic(_ name: String, first: Double?, second: Double?) -> AppActivityItem {
            AppActivityItem(id: name, name: name, bundleIdentifier: nil, bundleURL: nil, processIDs: [], cpuPercent: nil,
                memoryBytes: nil, uploadBytesPerSecond: first, downloadBytesPerSecond: second,
                diskReadBytesPerSecond: first, diskWriteBytesPerSecond: second, statusNotes: [])
        }
        let trafficItems = [traffic("Mostly receive", first: 1, second: 20), traffic("Mostly send", first: 15, second: 1),
                            traffic("Partial", first: nil, second: 1000), traffic("Overflow", first: .greatestFiniteMagnitude, second: .greatestFiniteMagnitude),
                            traffic("Invalid", first: -1, second: 1000)]
        for key in [AppActivitySortKey.network, .disk] {
            let descending = AppActivityDerivation.sorted(trafficItems, by: key, descending: true)
            let ascending = AppActivityDerivation.sorted(trafficItems, by: key, descending: false)
            check(Array(descending.prefix(2)).map(\.name) == ["Mostly receive", "Mostly send"],
                  "Combined activity sorting counts both directions rather than favoring the first direction")
            check(Array(ascending.prefix(2)).map(\.name) == ["Mostly send", "Mostly receive"],
                  "Ascending combined activity reverses complete totals and leaves partial or invalid totals last")
        }

        let decoder = AppActivityNetworkDecoder()
        let header = ",bytes_in,bytes_out,\n"
        decoder.append(Data((header + "Demo.1,100,200,\n").utf8), at: 20)
        check(decoder.read(at: 20.5) == nil, "A partial stream cannot invent a frame boundary from idle time")
        decoder.append(Data((header + "Demo.1,500,800,\nA, Comma.Name.2,50,70,\n").utf8), at: 21)
        decoder.append(Data(header.utf8), at: 22)
        check(decoder.read(at: 22)?.rates[1] == AppActivityNetworkRates(received: 400, sent: 600),
              "Explicit header boundaries yield correct per-process byte rates")
        check(decoder.read(at: 22)?.unknownPIDs == [2], "New socket owners are baseline-only, not since-boot spikes")
        decoder.append(Data("Demo.1,600,900,\nA, Comma.Name.2,80,90,\n".utf8), at: 22)
        decoder.append(Data(header.utf8), at: 23)
        check(decoder.read(at: 23)?.rates[2] == AppActivityNetworkRates(received: 30, sent: 20),
              "Dots and commas in process names cannot corrupt the numeric PID suffix")
        decoder.append(Data("Demo.1,2,3,\n".utf8), at: 23)
        decoder.append(Data(header.utf8), at: 24)
        check(decoder.read(at: 24)?.unknownPIDs == [1, 2], "Counter rollback and exited owners remain unknown")
        check(decoder.read(at: 29) == nil, "Stale nettop data expires instead of showing a frozen live rate")
        decoder.append(Data("bad data\n".utf8), at: 24)
        decoder.append(Data(header.utf8), at: 25)
        check(decoder.read(at: 25) == nil, "Malformed frames invalidate the complete network baseline")
        decoder.append(Data(repeating: 65, count: 1_048_577), at: 25)
        check(decoder.read(at: 25) == nil, "Oversized partial CSV is bounded and discarded")
        let timed = AppActivityNetworkDecoder()
        let timedHeader = "time,,bytes_in,bytes_out,\n"
        timed.append(Data((timedHeader + "12:00:00.000000,Demo.1,100,200,\n" + timedHeader
            + "12:00:01.000000,Demo.1,500,600,\n" + timedHeader).utf8), at: 103, wallClockSeconds: 43_203)
        check(timed.read(at: 103)?.rates[1] == AppActivityNetworkRates(received: 400, sent: 400),
              "Buffered frames use producer timestamps instead of zero time between pipe chunks")
        check(timed.read(at: 103)?.uptime == 101 && timed.read(at: 103)?.intervalStart == 100,
              "Producer time maps onto monotonic uptime, preserving freshness and PID-start checks")
        check(AppActivityNetworkDecoder.secondsOfDay("23:59:59.5") == 86_399.5
              && AppActivityNetworkDecoder.secondsOfDay("24:00:00") == nil, "Nettop timestamps use bounded explicit parsing")

        let crowded = AppActivityNetworkDecoder()
        func largeFrame(delta: Int) -> Data {
            Data((header + (1000..<3048).map { "App.\($0),\($0 + delta),\($0 * 2 + delta * 2),\n" }.joined()).utf8)
        }
        crowded.append(largeFrame(delta: 0), at: 50)
        let fragmented = largeFrame(delta: 250)
        for offset in stride(from: 0, to: fragmented.count, by: 83) {
            crowded.append(fragmented.subdata(in: offset..<min(offset + 83, fragmented.count)), at: 51)
        }
        crowded.append(Data(header.utf8), at: 52)
        check(crowded.read(at: 52)?.rates.count == 2048
              && crowded.read(at: 52)?.rates[1000] == AppActivityNetworkRates(received: 250, sent: 500)
              && crowded.read(at: 52)?.rates[3047] == AppActivityNetworkRates(received: 250, sent: 500),
              "Single-pass CSV compaction preserves complete busy frames across arbitrary partial-line chunks")

        let backend = FakeBackend(app: app), clock = FakeClock()
        let monitor = AppActivityMonitor(catalog: { [app] }, backend: backend, schedule: clock.schedule)
        var delivered = 0, onMain = true
        let token = monitor.observe { _ in delivered += 1; onMain = onMain && Thread.isMainThread }
        check(delivered == 1 && backend.reads == 0 && clock.starts == 0, "Hidden Apps monitor constructs no reader or timer")
        monitor.activate(); monitor.activate()
        check(wait { delivered == 2 } && backend.starts == 1 && backend.reads == 1 && clock.starts == 1,
              "Activation starts one backend and one visible-only timer")
        clock.fire(); check(wait { delivered == 3 }, "Each timer pulse publishes one app snapshot")
        check(onMain && backend.offMain, "Catalog/UI observers remain main-thread while all backend operations are off main")
        monitor.deactivate(); let oldDelivered = delivered, oldReads = backend.reads
        clock.fireRemoved(); drain()
        check(wait { backend.stops >= 2 } && backend.reads == oldReads && delivered == oldDelivered,
              "Hiding Apps stops its backend/network helper and rejects queued callbacks")
        monitor.activate(); check(wait { delivered == oldDelivered + 1 } && monitor.snapshot.items.first?.cpuPercent == nil,
              "Reopening Apps begins a fresh per-process baseline")
        monitor.removeObserver(token); clock.fire(); drain()
        check(delivered == oldDelivered + 1, "Removed observers receive no later readings")
        monitor.deactivate()
        let saved = fixture.snapshot; fixture.activate(); drain(); fixture.deactivate()
        check(fixture.snapshot == saved, "Fixture activation does not read real apps or start nettop")
        return count
    }

    private final class FakeClock {
        var callback: (() -> Void)?
        var removed: [() -> Void] = []
        var starts = 0
        func schedule(_ action: @escaping () -> Void) -> () -> Void {
            starts += 1; callback = action
            return { [weak self] in self?.removed.append(action); self?.callback = nil }
        }
        func fire() { callback?() }
        func fireRemoved() { removed.forEach { $0() } }
    }
    private final class FakeBackend: AppActivityBackend {
        let app: AppActivityIdentity
        private let lock = NSLock()
        private var counts = ["start": 0, "stop": 0, "read": 0]
        private var workerOnly = true
        init(app: AppActivityIdentity) { self.app = app }
        var starts: Int { return get("start") }
        var stops: Int { return get("stop") }
        var reads: Int { return get("read") }
        var offMain: Bool { lock.lock(); defer { lock.unlock() }; return workerOnly }
        private func get(_ key: String) -> Int { lock.lock(); defer { lock.unlock() }; return counts[key] ?? 0 }
        private func record(_ key: String) {
            lock.lock(); counts[key, default: 0] += 1; workerOnly = workerOnly && !Thread.isMainThread; lock.unlock()
        }
        func start() { record("start") }
        func stop() { record("stop") }
        func sample(apps: [AppActivityIdentity]) -> AppActivityRawSample {
            record("read"); let value = reads
            let reading = AppActivityProcessReading(pid: 1, startID: 1, startUptime: 1, cpuTicks: UInt64(value * 100),
                memoryBytes: 1000, diskReadBytes: UInt64(value * 200), diskWriteBytes: UInt64(value * 300))
            return AppActivityRawSample(timestamp: Date(), uptime: Double(value + 10), apps: [app], members: [app.id: [1]],
                processes: [1: reading], secondsPerCPUTick: 0.001, network: nil, statusNotes: [])
        }
    }
}
