import Foundation
import IOKit
import IOKit.ps

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

/// Main-thread owner of one IOKit notification source. There is no timer,
/// subprocess, background queue, or periodic battery polling.
final class BatteryMonitor {
    var onChange: ((BatterySnapshot) -> Void)?

    private var notificationSource: CFRunLoopSource?
    private var lastSnapshot: BatterySnapshot?

    init() {}

    func start() {
        precondition(Thread.isMainThread)
        guard notificationSource == nil else { return }

        if let source = IOPSNotificationCreateRunLoopSource({ context in
            guard let context = context else { return }
            Unmanaged<BatteryMonitor>.fromOpaque(context).takeUnretainedValue().refresh()
        }, Unmanaged.passUnretained(self).toOpaque())?.takeRetainedValue() {
            notificationSource = source
            CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        } else {
            NSLog("EndfieldCharge: Could not subscribe to power-source notifications.")
        }

        refresh()
    }

    func refresh() {
        precondition(Thread.isMainThread)
        let snapshot = readSnapshot()
        guard snapshot != lastSnapshot else { return }
        lastSnapshot = snapshot
        onChange?(snapshot)
    }

    func stop() {
        precondition(Thread.isMainThread)
        removeNotificationSource()
        lastSnapshot = nil
    }

    deinit {
        // The context is unretained. Invalidate the source before this object
        // disappears so there can never be a callback to a stale pointer.
        removeNotificationSource()
    }

    private func removeNotificationSource() {
        guard let source = notificationSource else { return }
        CFRunLoopRemoveSource(CFRunLoopGetMain(), source, .commonModes)
        CFRunLoopSourceInvalidate(source)
        notificationSource = nil
    }

    private func readSnapshot() -> BatterySnapshot {
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

    private func readCapacityProperties() -> [String: Any] {
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
