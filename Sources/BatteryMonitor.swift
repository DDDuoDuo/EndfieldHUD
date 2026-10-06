import Foundation
import IOKit
import IOKit.ps
import notify

struct BatterySnapshot: Equatable {
    let percentage: Int?
    let isPluggedIn: Bool
    let isCharging: Bool
    let isFullyCharged: Bool
    let hasBattery: Bool
    let capacity: BatteryCapacityReading?
    /// Optional category reported by macOS, never a calculated health percent.
    let healthCategory: String?

    var levelTone: BatteryLevelTone? { BatteryLevelTone.forPercentage(percentage) }
    /// The user-facing alert denotes connection to external power immediately,
    /// including charger negotiation, optimized-charging pauses and full charge.
    /// isCharging remains the OS's separate, actual charging-state diagnostic.
    var isChargeMode: Bool { hasBattery && isPluggedIn }

    init(percentage: Int?, isPluggedIn: Bool, isCharging: Bool,
         isFullyCharged: Bool, hasBattery: Bool, capacity: BatteryCapacityReading? = nil,
         healthCategory: String? = nil) {
        self.percentage = percentage
        self.isPluggedIn = isPluggedIn
        self.isCharging = isCharging
        self.isFullyCharged = isFullyCharged
        self.hasBattery = hasBattery
        self.capacity = hasBattery ? capacity : nil
        self.healthCategory = hasBattery ? Self.normalizedHealthCategory(healthCategory) : nil
    }

    static let unavailable = BatterySnapshot(
        percentage: nil,
        isPluggedIn: false,
        isCharging: false,
        isFullyCharged: false,
        hasBattery: false
    )

    /// Kept separate from IOKit access so malformed or partial hardware reports
    /// can be tested without requiring a particular Mac or changing its power.
    static func fromPowerSources(_ sources: [[String: Any]]) -> BatterySnapshot {
        let battery = sources.first { description in
            let type = description[kIOPSTypeKey] as? String
            let transport = description[kIOPSTransportTypeKey] as? String
            // Older reports may omit Type. Never fall back to an explicitly
            // identified UPS, even if it claims to use an internal transport.
            let isInternal = type == kIOPSInternalBatteryType
                || (type == nil && transport == kIOPSInternalType)
            let isPresent = (description[kIOPSIsPresentKey] as? NSNumber)?.boolValue ?? true
            return isInternal && isPresent
        }
        guard let description = battery else { return .unavailable }

        let percentage: Int?
        if let current = (description[kIOPSCurrentCapacityKey] as? NSNumber)?.doubleValue,
           let maximum = (description[kIOPSMaxCapacityKey] as? NSNumber)?.doubleValue,
           current.isFinite, maximum.isFinite, current >= 0, maximum > 0 {
            // Divide before multiplying to avoid overflow with capacity units
            // other than percent. Clamp overfull hardware reports to 100%.
            percentage = Int((min(current / maximum, 1) * 100).rounded())
        } else {
            percentage = nil
        }

        let reportedCharging = (description[kIOPSIsChargingKey] as? NSNumber)?.boolValue ?? false
        let powerState = description[kIOPSPowerSourceStateKey] as? String
        let isPluggedIn = powerState == kIOPSACPowerValue
            || (powerState == nil && reportedCharging)
        let isCharging = isPluggedIn && reportedCharging
        let reportedFull = (description[kIOPSIsChargedKey] as? NSNumber)?.boolValue ?? false

        return BatterySnapshot(
            percentage: percentage,
            isPluggedIn: isPluggedIn,
            isCharging: isCharging,
            isFullyCharged: isPluggedIn && !isCharging && (reportedFull || percentage == 100),
            hasBattery: true,
            capacity: BatteryCapacityReading.fromRegistry([:], percentage: percentage),
            // A specific condition takes precedence over the broad estimate.
            // macOS can also publish "Check Battery" in BatteryHealth itself;
            // preserve nonempty OS strings instead of assuming Good/Fair/Poor.
            healthCategory: normalizedHealthCategory(description[kIOPSBatteryHealthConditionKey])
                ?? normalizedHealthCategory(description[kIOPSBatteryHealthKey])
        )
    }

    private static func normalizedHealthCategory(_ value: Any?) -> String? {
        guard let value = value as? String else { return nil }
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }
}

/// Main-thread owner of one public power-source notification subscription.
/// Changes are coalesced before reading IOKit; there is no periodic polling.
final class BatteryMonitor {
    /// Subscribers deliver on the main thread and return their cancellation.
    typealias Subscribe = (_ name: String, _ handler: @escaping () -> Void) -> (() -> Void)?
    typealias ScheduleRefresh = (_ delay: TimeInterval, _ action: @escaping () -> Void) -> (() -> Void)

    var onChange: ((BatterySnapshot) -> Void)?

    private let readSnapshot: () -> BatterySnapshot
    private let subscribe: Subscribe
    private let scheduleRefresh: ScheduleRefresh
    private let uptime: () -> TimeInterval
    private var cancelNotifications: (() -> Void)?
    private var cancelScheduledRefresh: (() -> Void)?
    private var lastSnapshot: BatterySnapshot?
    private var lastReadTime: TimeInterval?
    private var isStarted = false
    private var generation: UInt64 = 0
    private var refreshSequence: UInt64 = 0
    private static let minimumRefreshInterval: TimeInterval = 0.25

    init(readSnapshot: (() -> BatterySnapshot)? = nil,
         subscribe: Subscribe? = nil, scheduleRefresh: ScheduleRefresh? = nil,
         uptime: (() -> TimeInterval)? = nil) {
        self.readSnapshot = readSnapshot ?? Self.readSystemSnapshot
        self.subscribe = subscribe ?? Self.subscribeToPowerChanges
        self.scheduleRefresh = scheduleRefresh ?? { delay, action in
            let work = DispatchWorkItem(block: action)
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
            return { work.cancel() }
        }
        self.uptime = uptime ?? { ProcessInfo.processInfo.systemUptime }
    }

    func start() {
        precondition(Thread.isMainThread)
        guard !isStarted else { return }
        isStarted = true
        generation &+= 1
        let activeGeneration = generation

        // IOPSNotificationCreateRunLoopSource follows percent/time-remaining
        // changes. Charging can start after AC connects while both stay the
        // same, so observe all source attributes, including Is Charging.
        cancelNotifications = subscribe(kIOPSNotifyAnyPowerSource) { [weak self] in
            self?.enqueueRefresh(generation: activeGeneration)
        }
        if cancelNotifications == nil {
            isStarted = false // Permit a later start() to retry registration.
            NSLog("EndfieldCharge: Could not subscribe to power-source notifications.")
        }

        refresh()
    }

    func refresh() {
        precondition(Thread.isMainThread)
        cancelPendingRefresh()
        lastReadTime = uptime()
        let snapshot = readSnapshot()
        guard snapshot != lastSnapshot else { return }
        lastSnapshot = snapshot
        onChange?(snapshot)
    }

    func stop() {
        precondition(Thread.isMainThread)
        isStarted = false
        generation &+= 1
        cancelPendingRefresh()
        lastReadTime = nil
        let cancel = cancelNotifications
        cancelNotifications = nil
        cancel?()
        lastSnapshot = nil
    }

    deinit {
        cancelScheduledRefresh?()
        cancelNotifications?()
    }

    private func enqueueRefresh(generation activeGeneration: UInt64) {
        precondition(Thread.isMainThread)
        guard isStarted, generation == activeGeneration, cancelScheduledRefresh == nil else { return }
        let elapsed = lastReadTime.map { max(0, uptime() - $0) } ?? Self.minimumRefreshInterval
        let delay = max(0, Self.minimumRefreshInterval - elapsed)
        guard delay > 0 else { refresh(); return }

        // This notification also covers attributes we don't display. Bound
        // registry work to 4 Hz, with one trailing read of the newest state.
        // Once events stop, no more work is scheduled.
        refreshSequence &+= 1
        let sequence = refreshSequence
        cancelScheduledRefresh = scheduleRefresh(delay) { [weak self] in
            guard let self = self, self.isStarted, self.generation == activeGeneration,
                  self.refreshSequence == sequence else { return }
            self.cancelScheduledRefresh = nil
            self.refresh()
        }
    }

    private func cancelPendingRefresh() {
        refreshSequence &+= 1
        let cancel = cancelScheduledRefresh
        cancelScheduledRefresh = nil
        cancel?()
    }

    private static func subscribeToPowerChanges(name: String, handler: @escaping () -> Void) -> (() -> Void)? {
        var token = Int32(NOTIFY_TOKEN_INVALID)
        guard notify_register_dispatch(name, &token, DispatchQueue.main, { _ in handler() }) == NOTIFY_STATUS_OK else {
            return nil
        }
        return { _ = notify_cancel(token) }
    }

    private static func readSystemSnapshot() -> BatterySnapshot {
        guard let info = IOPSCopyPowerSourcesInfo()?.takeRetainedValue(),
              let sources = IOPSCopyPowerSourcesList(info)?.takeRetainedValue() as? [CFTypeRef] else {
            return .unavailable
        }

        let descriptions = sources.compactMap { source -> [String: Any]? in
            IOPSGetPowerSourceDescription(info, source)?.takeUnretainedValue() as? [String: Any]
        }
        let snapshot = BatterySnapshot.fromPowerSources(descriptions)
        guard snapshot.hasBattery else { return snapshot }
        return BatterySnapshot(
            percentage: snapshot.percentage,
            isPluggedIn: snapshot.isPluggedIn,
            isCharging: snapshot.isCharging,
            isFullyCharged: snapshot.isFullyCharged,
            hasBattery: true,
            capacity: BatteryCapacityReading.fromRegistry(readCapacityProperties(), percentage: snapshot.percentage),
            healthCategory: snapshot.healthCategory
        )
    }

    private static func readCapacityProperties() -> [String: Any] {
        // Zero is the default IOKit main port on all supported macOS versions.
        // The public registry API reads only four numeric properties, without
        // collecting battery identifiers or loading the full registry payload.
        let service = IOServiceGetMatchingService(0, IOServiceMatching("AppleSmartBattery"))
        guard service != 0 else { return [:] }
        defer { IOObjectRelease(service) }
        var properties: [String: Any] = [:]
        for key in ["AppleRawCurrentCapacity", "AppleRawMaxCapacity", "CurrentCapacity", "MaxCapacity"] {
            if let value = IORegistryEntryCreateCFProperty(service, key as CFString, kCFAllocatorDefault, 0)?.takeRetainedValue() {
                properties[key] = value
            }
        }
        return properties
    }
}
