import Foundation
import IOKit.ps

enum PowerDataTests {
    private final class NotificationHarness {
        final class Job {
            let deadline: TimeInterval
            let action: () -> Void
            var canceled = false
            init(deadline: TimeInterval, action: @escaping () -> Void) {
                self.deadline = deadline
                self.action = action
            }
        }
        var now: TimeInterval = 0
        var names: [String] = []
        var handlers: [() -> Void] = []
        var cancellations = 0
        var jobs: [Job] = []

        func subscribe(_ name: String, _ handler: @escaping () -> Void) -> (() -> Void)? {
            names.append(name)
            handlers.append(handler)
            return { self.cancellations += 1 }
        }
        func schedule(_ delay: TimeInterval, _ action: @escaping () -> Void) -> (() -> Void) {
            let job = Job(deadline: now + delay, action: action)
            jobs.append(job)
            return { job.canceled = true }
        }
        func advance(_ interval: TimeInterval) {
            let end = now + interval
            while let job = jobs.filter({ !$0.canceled && $0.deadline <= end }).min(by: { $0.deadline < $1.deadline }) {
                job.canceled = true
                now = job.deadline
                job.action()
            }
            now = end
        }
    }

    static func run() -> Int {
        var assertionCount = 0
        func expect<T: Equatable>(_ actual: T, _ expected: T, _ message: String,
                                  file: StaticString = #file, line: UInt = #line) {
            assertionCount += 1
            guard actual == expected else {
                fatalError("\(message): expected \(expected), received \(actual)", file: file, line: line)
            }
        }
        func source(health: Any? = nil, condition: Any? = nil) -> [String: Any] {
            var result: [String: Any] = [
                kIOPSTypeKey: kIOPSInternalBatteryType,
                kIOPSIsPresentKey: true,
                kIOPSCurrentCapacityKey: 50,
                kIOPSMaxCapacityKey: 100,
                kIOPSPowerSourceStateKey: kIOPSBatteryPowerValue
            ]
            result[kIOPSBatteryHealthKey] = health
            result[kIOPSBatteryHealthConditionKey] = condition
            return result
        }
        func snapshot(_ percentage: Int?, hasBattery: Bool = true,
                      health: String? = nil) -> BatterySnapshot {
            BatterySnapshot(percentage: percentage, isPluggedIn: false, isCharging: false,
                            isFullyCharged: false, hasBattery: hasBattery, healthCategory: health)
        }

        expect(BatterySnapshot.fromPowerSources([source()]).healthCategory, nil,
               "Absent health remains unavailable")
        for category in [kIOPSGoodValue, kIOPSFairValue, kIOPSPoorValue, kIOPSCheckBatteryValue] {
            expect(BatterySnapshot.fromPowerSources([source(health: category)]).healthCategory, category,
                   "Preserve macOS health category")
        }
        expect(BatterySnapshot.fromPowerSources([source(health: kIOPSGoodValue,
                                                        condition: kIOPSCheckBatteryValue)]).healthCategory,
               kIOPSCheckBatteryValue, "Specific condition takes precedence over broad estimate")
        expect(BatterySnapshot.fromPowerSources([source(condition: kIOPSPermanentFailureValue)]).healthCategory,
               kIOPSPermanentFailureValue, "Condition is usable without a health estimate")
        expect(BatterySnapshot.fromPowerSources([source(health: kIOPSCheckBatteryValue,
                                                        condition: " \n ")]).healthCategory,
               kIOPSCheckBatteryValue, "Empty condition does not hide a reported health value")
        expect(BatterySnapshot.fromPowerSources([source(health: " \n ")]).healthCategory, nil,
               "Whitespace-only health is unavailable")
        expect(BatterySnapshot.fromPowerSources([source(health: " Fair \n")]).healthCategory, "Fair",
               "Trim surrounding whitespace from health")
        expect(BatterySnapshot.fromPowerSources([source(health: 85, condition: true)]).healthCategory, nil,
               "Numeric or boolean health is not an invented percentage")
        expect(BatterySnapshot.fromPowerSources([source(health: kIOPSGoodValue,
                                                        condition: ["bad": "shape"])]).healthCategory,
               kIOPSGoodValue, "Malformed condition falls back to valid health")
        expect(BatterySnapshot.fromPowerSources([source(health: "Future OS Category")]).healthCategory,
               "Future OS Category", "Do not reinterpret a future OS health category")
        var ups = source(health: kIOPSPoorValue)
        ups[kIOPSTypeKey] = kIOPSUPSType
        expect(BatterySnapshot.fromPowerSources([ups]).healthCategory, nil,
               "An external UPS does not supply the Mac's battery health")
        expect(snapshot(50, hasBattery: false, health: kIOPSGoodValue).healthCategory, nil,
               "A missing internal battery cannot have battery health")
        expect(snapshot(50, health: " \n ").healthCategory, nil,
               "Directly constructed empty health is normalized")
        expect(snapshot(50, health: kIOPSGoodValue) == snapshot(50, health: kIOPSFairValue), false,
               "A health-only change is observable by snapshot deduplication")

        let provider = DeviceBatteryProvider()
        let hostRows = provider.readings(for: snapshot(50))
        expect(hostRows.count, 1, "Only the known host is listed")
        expect(hostRows[0].id, "host-mac", "Host identity does not expose a hardware identifier")
        expect(hostRows[0].kind, .mac, "Host row identifies the Mac")
        expect(hostRows[0].name, "This Mac", "Host name does not claim an unverified model")
        expect(hostRows[0].percentage, 50, "Host row uses the live percentage")
        expect(hostRows[0].availability, .available, "Valid capacity is available")
        expect(provider.accessoryCapability, .notProvided,
               "Accessory capability is distinct from actual connected devices")
        let desktop = provider.readings(for: snapshot(50, hasBattery: false))[0]
        expect(desktop.percentage, nil, "A desktop cannot acquire a fake battery percentage")
        expect(desktop.availability, .noBattery, "A missing battery is distinct from unavailable capacity")
        let partial = provider.readings(for: snapshot(nil))[0]
        expect(partial.percentage, nil, "An unknown capacity stays unknown")
        expect(partial.availability, .unavailable, "A partial battery report is not a missing battery")
        for percentage in [-1, 101, Int.max] {
            let invalid = provider.readings(for: snapshot(percentage))[0]
            expect(invalid.percentage, nil, "Malformed capacity is never shown as real data")
            expect(invalid.availability, .unavailable, "Malformed capacity is unavailable")
        }
        for percentage in [0, 100] {
            let valid = provider.readings(for: snapshot(percentage))[0]
            expect(valid.percentage, percentage, "Real empty and full capacities are retained")
            expect(valid.availability, .available, "Zero is available when explicitly reported")
        }
        expect(provider.readings(for: .unavailable).count, 1,
               "Missing host telemetry never invents connected accessories")

        // AC connection and actual charging can arrive as separate OS changes
        // without a percentage change. The monitor must receive the latter.
        func powerSnapshot(plugged: Bool, charging: Bool) -> BatterySnapshot {
            var report = source()
            report[kIOPSPowerSourceStateKey] = plugged ? kIOPSACPowerValue : kIOPSBatteryPowerValue
            report[kIOPSIsChargingKey] = charging
            return BatterySnapshot.fromPowerSources([report])
        }
        let connectedWaiting = powerSnapshot(plugged: true, charging: false)
        expect(connectedWaiting.isChargeMode, true, "Charge Mode begins when AC connects, before actual battery charging starts")
        expect(connectedWaiting.isCharging, false, "The UI connection mode does not overwrite actual charging diagnostics")
        expect(powerSnapshot(plugged: true, charging: true).isChargeMode, true, "Actual charging retains the connected mode")
        expect(powerSnapshot(plugged: false, charging: false).isChargeMode, false, "Disconnecting AC immediately chooses Power Mode")
        expect(BatterySnapshot(percentage: 100, isPluggedIn: true, isCharging: false,
                               isFullyCharged: true, hasBattery: true).isChargeMode, true,
               "A full battery connected to AC retains Charge Mode")
        expect(BatterySnapshot(percentage: nil, isPluggedIn: true, isCharging: true,
                               isFullyCharged: false, hasBattery: false).isChargeMode, false,
               "Missing battery data cannot fabricate a connected charging banner")
        let events = NotificationHarness()
        var current = powerSnapshot(plugged: false, charging: false)
        var reads = 0
        var delivered: [BatterySnapshot] = []
        let monitor = BatteryMonitor(readSnapshot: { reads += 1; return current },
                                     subscribe: events.subscribe, scheduleRefresh: events.schedule,
                                     uptime: { events.now })
        monitor.onChange = { delivered.append($0) }
        monitor.start()
        expect(events.names, [kIOPSNotifyAnyPowerSource],
               "Subscribe to every power-source attribute, including delayed charging-only changes")
        expect(delivered, [current], "Starting reports a baseline immediately")
        monitor.start()
        expect(events.handlers.count, 1, "Repeated start does not create another registration")
        expect(reads, 1, "Repeated start does not read again")

        let transitions: [(plugged: Bool, charging: Bool, presentation: DisplayAction)] = [
            (true, false, .showTransient),
            (true, true, .keepCurrent),
            (true, false, .keepCurrent),
            (true, true, .keepCurrent),
            (false, false, .showTransient),
        ]
        for (plugged, charging, presentation) in transitions {
            let previous = delivered.last!
            current = powerSnapshot(plugged: plugged, charging: charging)
            let before = reads
            for _ in 0..<40 { events.handlers[0]() }
            expect(events.jobs.filter { !$0.canceled }.count, 1,
                   "An attribute burst creates only one finite trailing refresh")
            events.advance(0.125)
            expect(reads, before, "Attribute bursts cannot read faster than four times per second")
            events.advance(0.125)
            expect(reads, before + 1, "One trailing read consumes the latest state")
            expect(delivered.last, current, "Plugged, charging, paused and unplugged transitions are delivered")
            expect(delivered.last?.percentage, 50, "Charging transitions do not require a capacity change")
            expect(DisplayPolicy.action(for: current, previous: previous, mode: .whenChargingStarts), presentation,
                   "Connection changes present once; negotiated, paused and resumed charging update without replay")
        }
        current = powerSnapshot(plugged: true, charging: false)
        events.handlers[0]()
        current = powerSnapshot(plugged: true, charging: true)
        events.handlers[0]()
        events.advance(0.25)
        expect(delivered.last, current, "A coalesced burst reads the final charging state, not the first event's state")
        let beforeRedundant = delivered.count
        for _ in 0..<40 { events.handlers[0]() }
        events.advance(0.25)
        expect(delivered.count, beforeRedundant, "An unrelated source attribute cannot repaint unchanged content")
        let beforeIdle = reads
        events.advance(60)
        expect(reads, beforeIdle, "No periodic reads remain after notifications stop")
        current = powerSnapshot(plugged: false, charging: false)
        events.handlers[0]()
        expect(reads, beforeIdle + 1, "A first notification after idle performs one immediate read")
        expect(delivered.last, current, "A first notification after idle refreshes immediately")

        current = powerSnapshot(plugged: true, charging: false)
        events.handlers[0]()
        let oldJob = events.jobs.last!
        let beforeStop = reads
        monitor.stop()
        monitor.stop()
        expect(events.cancellations, 1, "Stop cancels the native registration exactly once")
        expect(oldJob.canceled, true, "Stop cancels a pending trailing refresh")
        events.handlers[0]()
        oldJob.action() // Simulate a callback already handed to the main queue.
        expect(reads, beforeStop, "Queued callbacks cannot read after stop")
        monitor.start()
        expect(delivered.last, current, "Restart establishes a fresh baseline immediately")
        expect(events.handlers.count, 2, "Restart owns exactly one new subscription")
        current = powerSnapshot(plugged: true, charging: true)
        events.handlers[1]()
        let beforeStale = reads
        events.handlers[0]()
        oldJob.action()
        expect(reads, beforeStale, "Previous subscription and trailing work cannot affect a restarted monitor")
        events.advance(0.25)
        expect(delivered.last, current, "A stale callback cannot cancel the restarted monitor's pending state")
        current = powerSnapshot(plugged: false, charging: false)
        events.handlers[1]()
        let beforeManual = reads
        let supersededJob = events.jobs.last!
        monitor.refresh()
        expect(delivered.last, current, "An explicit wake refresh remains immediate")
        supersededJob.action()
        expect(reads, beforeManual + 1, "Explicit refresh cancels redundant trailing work, even if already queued")
        monitor.stop()
        expect(events.cancellations, 2, "Every successful registration is paired with cancellation")

        let disposal = NotificationHarness()
        var disposable: BatteryMonitor? = BatteryMonitor(readSnapshot: { current },
            subscribe: disposal.subscribe, scheduleRefresh: disposal.schedule, uptime: { disposal.now })
        weak var weakMonitor = disposable
        disposable?.start()
        disposal.handlers[0]()
        disposable = nil
        expect(weakMonitor == nil, true, "The subscriber and pending work do not retain the monitor")
        expect(disposal.cancellations, 1, "Deinitialization cancels the native registration")
        expect(disposal.jobs.last?.canceled, true, "Deinitialization cancels trailing work")
        return assertionCount
    }
}
