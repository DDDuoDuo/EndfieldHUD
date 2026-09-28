import Foundation

enum BatteryCapacityUnit: String {
    case milliampHours = "mAh"
    case percent = "%"
}

struct BatteryCapacityReading: Equatable {
    let current: Int
    let maximum: Int
    let unit: BatteryCapacityUnit

    /// Raw gauge capacities form a matching pair; a design capacity or nominal
    /// health estimate is not interchangeable with the gauge's full capacity.
    /// These registry keys are optional, so newer or unsupported hardware falls
    /// back to an explicitly labelled percentage instead of invented energy.
    /// Registry units: apple-oss-distributions/xnu, IOPMPowerSource.h.
    /// Raw-pair mapping: apple-oss-distributions/PowerManagement,
    /// AppleSmartBatteryManager/AppleSmartBattery.cpp; also documented by
    /// Hammerspoon's hs.battery.capacity() and hs.battery.maxCapacity().
    static func fromRegistry(_ properties: [String: Any], percentage: Int?) -> BatteryCapacityReading? {
        if let raw = milliampHours(current: properties["AppleRawCurrentCapacity"],
                                  maximum: properties["AppleRawMaxCapacity"]) {
            return raw
        }
        // Intel-era IOPMPowerSource exposes mAh here. Apple Silicon commonly
        // exposes a 0...100 scale instead, which must never be labelled mAh.
        if let legacy = milliampHours(current: properties["CurrentCapacity"],
                                     maximum: properties["MaxCapacity"]) {
            return legacy
        }
        guard let percentage = percentage, (0...100).contains(percentage) else { return nil }
        return BatteryCapacityReading(current: percentage, maximum: 100, unit: .percent)
    }

    private static func milliampHours(current: Any?, maximum: Any?) -> BatteryCapacityReading? {
        guard let current = capacityInteger(current), let maximum = capacityInteger(maximum),
              maximum > 100, current <= maximum else { return nil }
        return BatteryCapacityReading(current: current, maximum: maximum, unit: .milliampHours)
    }

    private static func capacityInteger(_ value: Any?) -> Int? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        let value = number.doubleValue
        // A generous physical bound also rejects signed/unsigned sentinel
        // values and avoids trapping when converting malformed numbers to Int.
        guard value.isFinite, value >= 0, value <= 1_000_000, value.rounded(.towardZero) == value else { return nil }
        return Int(value)
    }
}

enum BatteryLevelTone: Equatable {
    case green
    case yellow
    case red

    static func forPercentage(_ percentage: Int?) -> BatteryLevelTone? {
        guard let percentage = percentage, (0...100).contains(percentage) else { return nil }
        if percentage > 50 { return .green }
        if percentage >= 20 { return .yellow }
        return .red
    }
}
