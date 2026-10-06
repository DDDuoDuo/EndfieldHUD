import Foundation
import Darwin

enum SystemActivityMonitorTests {
    static func run() -> Int {
        var checks = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            checks += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func wait(_ predicate: () -> Bool, timeout: TimeInterval = 3) -> Bool {
            let deadline = Date().addingTimeInterval(timeout)
            while !predicate() && Date() < deadline { RunLoop.current.run(until: Date().addingTimeInterval(0.002)) }
            return predicate()
        }
        func drain() { RunLoop.current.run(until: Date().addingTimeInterval(0.04)) }
        func counters(_ read: UInt64, _ write: UInt64) -> SystemActivityByteCounters {
            SystemActivityByteCounters(received: read, sent: write)
        }
        let memory = SystemActivityMemoryCounters(physicalBytes: 100_000, pageSize: 10,
            internalPages: 200, purgeablePages: 20, wiredPages: 40, compressedPages: 30)
        func raw(_ uptime: TimeInterval, cpu: SystemActivityCPUTicks? = SystemActivityCPUTicks(user: 0, system: 0, idle: 0, nice: 0),
                 mem: SystemActivityMemoryCounters? = memory,
                 network: [String: SystemActivityByteCounters]? = ["en0": SystemActivityByteCounters(received: 0, sent: 0)],
                 disk: [String: SystemActivityByteCounters]? = ["disk1": SystemActivityByteCounters(received: 0, sent: 0)]) -> SystemActivityRawSample {
            SystemActivityRawSample(timestamp: Date(timeIntervalSince1970: uptime), uptime: uptime,
                                    cpu: cpu, memory: mem, network: network, disk: disk)
        }
        let base = raw(10)
        let current = raw(12, cpu: SystemActivityCPUTicks(user: 10, system: 5, idle: 80, nice: 5),
                          network: ["en0": counters(600, 200)], disk: ["disk1": counters(4000, 8000)])
        let first = SystemActivityDerivation.snapshot(base, previous: nil)
        check(first.cpuPercent == nil && first.uploadBytesPerSecond == nil && first.diskReadBytesPerSecond == nil,
              "The first reading establishes counters without inventing zero or since-boot rates")
        check(first.memoryUsedBytes == 2500 && first.memoryTotalBytes == 100_000 && first.memoryCompressedBytes == 300,
              "Memory counts non-purgeable anonymous, wired and physical compressed pages exactly once")
        check(first.statusNotes.count == 3, "Initial status describes rate baselines without marking valid memory unavailable")
        let next = SystemActivityDerivation.snapshot(current, previous: base)
        check(next.cpuPercent == 20, "CPU includes user, kernel and nice time across all cores, normalized to 100%")
        check(next.downloadBytesPerSecond == 300 && next.uploadBytesPerSecond == 100,
              "64-bit network counter deltas divide by elapsed monotonic seconds")
        check(next.diskReadBytesPerSecond == 2000 && next.diskWriteBytesPerSecond == 4000,
              "Disk read and write rates retain their direction")
        check(next.statusNotes.isEmpty && next.uptime == 12 && next.timestamp == current.timestamp,
              "Valid snapshots retain sample timing and report no invented unavailable reason")
        let rollback = raw(13, cpu: SystemActivityCPUTicks(user: 2, system: 6, idle: 81, nice: 6),
                           network: ["en0": counters(2, 210)], disk: ["disk1": counters(10, 9000)])
        let rolled = SystemActivityDerivation.snapshot(rollback, previous: current)
        check(rolled.cpuPercent == nil && rolled.downloadBytesPerSecond == nil && rolled.diskReadBytesPerSecond == nil,
              "Counter wrap/rollback produces a baseline gap, never a negative rate or huge spike")
        check(rolled.memoryUsedBytes == 2500, "One unavailable delta does not suppress instantaneous memory")
        for elapsed in [0.0, -1, 16, Double.nan, Double.infinity] {
            let gap = SystemActivityDerivation.snapshot(raw(10 + elapsed), previous: base)
            check(gap.cpuPercent == nil && gap.uploadBytesPerSecond == nil && gap.diskWriteBytesPerSecond == nil,
                  "Invalid monotonic intervals and sleep gaps never produce misleading averaged rates")
        }
        let topology = SystemActivityDerivation.snapshot(raw(11, network: ["en1": counters(900, 100)], disk: ["disk2": counters(100, 100)]), previous: base)
        check(topology.uploadBytesPerSecond == nil && topology.diskReadBytesPerSecond == nil,
              "Interface or disk replacement resets the whole affected counter baseline")
        let missing = SystemActivityDerivation.snapshot(raw(11, cpu: nil, mem: nil, network: nil, disk: nil), previous: base)
        check(missing.cpuPercent == nil && missing.memoryUsedBytes == nil && missing.downloadBytesPerSecond == nil
              && missing.diskWriteBytesPerSecond == nil && missing.statusNotes.count == 4,
              "Failed OS reads remain unavailable, not fake idle readings")
        check(SystemActivityDerivation.snapshot(current, previous: raw(10, cpu: nil, network: nil, disk: nil)).cpuPercent == nil,
              "Recovery after a failed read establishes a fresh baseline")
        check(SystemActivityDerivation.rates([:], [:], elapsed: 1)?.received == 0,
              "A successful empty active-interface inventory can truthfully report zero network traffic")
        check(SystemActivityDerivation.cpuPercent(base.cpu, base.cpu) == nil, "No elapsed CPU ticks is unknown rather than idle")
        check(SystemActivityDerivation.cpuPercent(SystemActivityCPUTicks(user: 1, system: 0, idle: 0, nice: 0), base.cpu) == 100,
              "An all-busy interval reaches exactly 100%")
        check(SystemActivityDerivation.cpuPercent(SystemActivityCPUTicks(user: 0, system: 0, idle: 1, nice: 0), base.cpu) == 0,
              "An all-idle valid interval reports a real zero")
        check(SystemActivityDerivation.rates(["a": counters(.max, 0), "b": counters(1, 0)],
                                            ["a": counters(0, 0), "b": counters(0, 0)], elapsed: 1) == nil,
              "Malformed cumulative sums cannot overflow or crash")
        check(SystemActivityMemoryCounters(physicalBytes: 100, pageSize: 10, internalPages: 100, purgeablePages: 1000,
                                           wiredPages: 3, compressedPages: 2).bytes?.used == 50,
              "Purgeable races cannot subtract below zero or undercount wired/compressed RAM")
        check(SystemActivityMemoryCounters(physicalBytes: 100, pageSize: 10, internalPages: 100, purgeablePages: 0,
                                           wiredPages: 3, compressedPages: 2).bytes?.used == 100,
              "Memory is bounded by physical RAM during independently sampled counter changes")
        check(SystemActivityMemoryCounters(physicalBytes: 100, pageSize: .max, internalPages: 2, purgeablePages: 0,
                                           wiredPages: 0, compressedPages: 0).bytes == nil,
              "Malformed page multiplication remains unavailable")
        check(SystemActivityReader.nonnegativeInteger(NSNumber(value: UInt64.max)) == UInt64.max,
              "Storage's full-width integer counter survives NSNumber conversion")
        for bad: Any in [NSNumber(value: -1), NSNumber(value: true), NSNumber(value: 3.5), NSNumber(value: Double.nan), "42"] {
            check(SystemActivityReader.nonnegativeInteger(bad) == nil, "Storage counters reject non-integer, negative and nonnumeric values")
        }

        func interface(_ index: UInt16, read: UInt64 = 5_000_000_000, write: UInt64 = 6_000_000_000,
                       flags: Int32 = IFF_UP | IFF_RUNNING, version: UInt8 = UInt8(RTM_VERSION)) -> Data {
            var info = if_msghdr2()
            info.ifm_msglen = UInt16(MemoryLayout<if_msghdr2>.size)
            info.ifm_version = version; info.ifm_type = UInt8(RTM_IFINFO2)
            info.ifm_index = index; info.ifm_flags = flags
            info.ifm_data.ifi_ibytes = read; info.ifm_data.ifi_obytes = write
            return withUnsafeBytes(of: &info) { Data($0) }
        }
        let names: [UInt16: String] = [1: "en0", 2: "en1", 3: "utun0", 4: "awdl0", 5: "bridge0", 6: "lo0", 7: "ppp0"]
        let interfaces = [1, 2, 3, 4, 5, 6, 7].reduce(into: Data()) { $0.append(interface(UInt16($1))) }
        let decoded = SystemActivityReader.networkCounters(interfaces) { names[$0] }
        check(decoded?.count == 3 && decoded?["1:en0"]?.received == 5_000_000_000 && decoded?["7:ppp0"]?.sent == 6_000_000_000,
              "BSD parser preserves 64-bit Ethernet/Wi-Fi/PPP counters while excluding duplicate VPN/bridge/peer-to-peer traffic")
        check(SystemActivityReader.networkCounters(interface(1, flags: IFF_UP)) { names[$0] }?.isEmpty == true,
              "An interface without a running link is excluded")
        check(SystemActivityReader.networkCounters(interface(1, flags: IFF_UP | IFF_RUNNING | IFF_LOOPBACK)) { names[$0] }?.isEmpty == true,
              "Loopback cannot enter network totals even with an unexpected name")
        for data in [Data([0, 0, 0, 0]), Data([1]), Data(interface(1).dropLast()), interface(1, version: 0), interface(1) + interface(1)] {
            check(SystemActivityReader.networkCounters(data) { names[$0] } == nil,
                  "Malformed, partial, duplicate or incompatible interface records cannot make a partial traffic report")
        }
        check(SystemActivityReader.networkCounters(interface(1)) { _ in nil } == nil,
              "An interface disappearing during identity lookup invalidates that baseline")

        let fake = FakeSampler(), clock = FakeClock()
        let monitor = SystemActivityMonitor(sampler: fake.read, cadenceSchedule: clock.scheduleAtCadence)
        var callbacks: [SystemActivitySnapshot] = [], allMain = true
        let observer = monitor.observe { callbacks.append($0); allMain = allMain && Thread.isMainThread }
        check(callbacks == [.unavailable] && fake.readCount == 0 && clock.schedules == 0 && !monitor.isActive,
              "Construction and observation neither sample nor schedule hidden monitoring")
        monitor.activate(); monitor.activate()
        check(wait { monitor.history.count == 1 }, "Activation delivers the first asynchronous sample")
        check(clock.schedules == 1 && fake.readCount == 1 && monitor.snapshot.cpuPercent == nil && clock.intervals == [1],
              "Repeated activation owns exactly one timer and one fresh baseline")
        clock.fire()
        check(wait { monitor.history.count == 2 }, "One timer event produces one sample")
        check(monitor.snapshot.cpuPercent == 15 && monitor.snapshot.downloadBytesPerSecond == 100,
              "Worker-derived samples compute deltas across the real elapsed interval")
        check(allMain && fake.allOffMain, "Every OS-like read stays off main and every observer callback stays on main")
        for index in 0..<65 {
            let count = fake.readCount
            clock.fire()
            check(wait { fake.readCount > count && monitor.snapshot.uptime == Double(index + 3) }, "History sample \(index) delivered")
        }
        check(monitor.history.count == 60 && monitor.history.first?.uptime == 8,
              "History retains only the latest 60 readings")
        let visibleHistory = monitor.history
        monitor.deactivate(); monitor.deactivate()
        let stoppedCallbacks = callbacks.count, stoppedReads = fake.readCount
        clock.fireCancelled(); drain()
        check(!monitor.isActive && monitor.isRunning && clock.cancellations == 1 && clock.intervals == [1, 5]
              && fake.readCount == stoppedReads && callbacks.count == stoppedCallbacks && monitor.history == visibleHistory,
              "Closing retains history and replaces visible sampling with exactly one coalesced five-second timer")
        clock.fire()
        check(wait { callbacks.count == stoppedCallbacks + 1 } && monitor.snapshot.cpuPercent == 15 && monitor.history.count == 60,
              "Background sampling continues existing counter baselines and bounded history")
        monitor.activate()
        check(clock.intervals == [1, 5, 1] && monitor.snapshot.cpuPercent == 15 && monitor.history.count == 60,
              "Reopening changes cadence without clearing history or replacing valid rates with a baseline")
        let resumedReads = fake.readCount
        clock.fireCancelled(); drain()
        check(fake.readCount == resumedReads, "A stopped timer cannot sample into the reopened generation")
        monitor.removeObserver(observer); clock.fire()
        check(wait { fake.readCount == resumedReads + 1 && monitor.snapshot.uptime == Double(resumedReads + 1) } && callbacks.count == stoppedCallbacks + 1,
              "Observer removal leaves no stale callback")
        monitor.shutdown(); let shutReads = fake.readCount
        clock.fire(); clock.fireCancelled(); drain()
        check(!monitor.isRunning && !monitor.isActive && fake.readCount == shutReads,
              "Explicit shutdown cancels all sampling including queued timer callbacks")

        let blocked = FakeSampler(), blockedClock = FakeClock(), gate = DispatchSemaphore(value: 0)
        blocked.nextGate = gate
        let delayed = SystemActivityMonitor(sampler: blocked.read, schedule: blockedClock.schedule)
        var received: [SystemActivitySnapshot] = []
        delayed.observe { received.append($0) }; delayed.activate()
        check(wait { blocked.readCount == 1 }, "A held read starts on the serial worker")
        for _ in 0..<20 { blockedClock.fire() }
        drain()
        check(blocked.readCount == 1 && received.count == 1, "Timer bursts never queue overlapping or catch-up samples")
        delayed.shutdown(); delayed.activate(); blockedClock.fireCancelled()
        check(blocked.readCount == 1, "A rapid reopen cannot overlap the previous generation's blocked read")
        gate.signal()
        check(wait { received.count == 2 } && blocked.readCount == 2 && received.last?.cpuPercent == nil,
              "Late results are discarded and rapid reopening takes one fresh baseline after the old read finishes")
        delayed.shutdown()

        let dyingFake = FakeSampler(), dyingClock = FakeClock()
        var owned: SystemActivityMonitor? = SystemActivityMonitor(sampler: dyingFake.read, schedule: dyingClock.schedule)
        weak var weakMonitor = owned
        owned?.activate(); check(wait { owned?.history.count == 1 }, "Lifetime fixture becomes active")
        owned = nil
        check(weakMonitor == nil && dyingClock.cancellations == 1, "Deinitialization releases the monitor and cancels its timer")
        let fixture = SystemActivityMonitor.fixture(), savedFixture = fixture.snapshot
        fixture.activate(); drain(); fixture.deactivate(); fixture.activate(); drain()
        check(fixture.snapshot == savedFixture && fixture.history.count == 60 && fixture.snapshot.cpuPercent != nil,
              "Preview fixture is deterministic and never replaces its graph with hardware readings or timers")
        fixture.shutdown()
        let backgroundFake = FakeSampler(), backgroundClock = FakeClock()
        let background = SystemActivityMonitor(sampler: backgroundFake.read, cadenceSchedule: backgroundClock.scheduleAtCadence)
        background.start(); background.start()
        check(wait { background.history.count == 1 } && backgroundClock.intervals == [5] && !background.isActive && background.isRunning,
              "App startup can collect background history before the first HUD opening")
        background.shutdown()
        let retained = background.history
        background.start()
        check(wait { background.history.count == retained.count + 1 } && background.history.first == retained.first && background.snapshot.cpuPercent == nil,
              "True shutdown/restart retains past history but leaves a real baseline gap instead of joining sleep time")
        background.shutdown()

        let sharedFake = FakeSampler(), sharedClock = FakeClock()
        let shared = SystemActivityMonitor(sampler: sharedFake.read, cadenceSchedule: sharedClock.scheduleAtCadence)
        shared.start()
        check(wait { shared.history.count == 1 } && sharedClock.intervals == [5],
              "Alert metrics reuse the session monitor's existing background history")
        let sharedHistory = shared.history
        shared.setAlertActive(true); shared.setAlertActive(true)
        check(shared.isActive && shared.isAlertActive && sharedClock.intervals == [5, 1],
              "A visible metric alert requests one shared 1 Hz timer, idempotently")
        shared.activate(); shared.deactivate()
        check(shared.isActive && sharedClock.intervals == [5, 1],
              "Opening and closing Activity Monitor cannot disable a visible metric alert or replace its timer")
        shared.activate(); shared.setAlertActive(false)
        check(shared.isActive && !shared.isAlertActive && sharedClock.intervals == [5, 1],
              "Closing the alert cannot disable a visible Activity Monitor")
        shared.deactivate()
        check(!shared.isActive && shared.isRunning && sharedClock.intervals == [5, 1, 5]
              && shared.history == sharedHistory, "The last visible consumer releases fast sampling without dropping history")
        shared.setAlertActive(true); shared.activate(); shared.shutdown()
        check(!shared.isActive && !shared.isRunning && !shared.isAlertActive,
              "Shutdown clears both visible demands and cancels the shared timer")
        let afterSharedShutdown = sharedFake.readCount
        sharedClock.fireCancelled(); drain()
        check(sharedFake.readCount == afterSharedShutdown, "Canceled alert-era timers cannot sample after shutdown")
        shared.start()
        check(wait { shared.history.count == sharedHistory.count + 1 } && !shared.isActive && sharedClock.intervals.last == 5,
              "Resume does not retain a hidden alert's or Activity canvas's stale fast-sampling demand")
        shared.shutdown()
        return checks
    }

    private final class FakeClock {
        var callback: (() -> Void)?
        var removed: [() -> Void] = []
        var schedules = 0
        var cancellations = 0
        var intervals: [TimeInterval] = []
        func scheduleAtCadence(_ interval: TimeInterval, _ callback: @escaping () -> Void) -> () -> Void {
            intervals.append(interval); return schedule(callback)
        }
        func schedule(_ callback: @escaping () -> Void) -> () -> Void {
            schedules += 1; self.callback = callback
            return { [weak self] in
                guard let self else { return }; self.cancellations += 1
                self.removed.append(callback); self.callback = nil
            }
        }
        func fire() { callback?() }
        func fireCancelled() { removed.forEach { $0() } }
    }

    private final class FakeSampler {
        private let lock = NSLock()
        private var count = 0
        private var offMain = true
        private var gate: DispatchSemaphore?
        var readCount: Int { lock.lock(); defer { lock.unlock() }; return count }
        var allOffMain: Bool { lock.lock(); defer { lock.unlock() }; return offMain }
        var nextGate: DispatchSemaphore? {
            get { lock.lock(); defer { lock.unlock() }; return gate }
            set { lock.lock(); gate = newValue; lock.unlock() }
        }
        func read() -> SystemActivityRawSample {
            lock.lock(); count += 1; let value = count, held = gate; gate = nil
            offMain = offMain && !Thread.isMainThread; lock.unlock()
            held?.wait()
            let cpu = SystemActivityCPUTicks(user: UInt64(value * 10), system: UInt64(value * 5), idle: UInt64(value * 85), nice: 0)
            return SystemActivityRawSample(timestamp: Date(timeIntervalSince1970: Double(value)), uptime: Double(value), cpu: cpu,
                memory: SystemActivityMemoryCounters(physicalBytes: 100_000, pageSize: 10, internalPages: 20,
                    purgeablePages: 2, wiredPages: 4, compressedPages: 3),
                network: ["en0": SystemActivityByteCounters(received: UInt64(value * 100), sent: UInt64(value * 50))],
                disk: ["1": SystemActivityByteCounters(received: UInt64(value * 1000), sent: UInt64(value * 500))])
        }
    }
}
