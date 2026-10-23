import Foundation
import IOKit.ps

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

/// The Battery screen's extra readings (spec §3.22), parsed from the same
/// power-source report and AppleSmartBattery read as the capacity. Every
/// reader rejects booleans, sentinels and physically implausible values, so
/// unsupported hardware shows "—" instead of an invented number.
enum BatteryDiagnosticsParsing {
    /// AppleSmartBattery `CycleCount`.
    static func cycleCount(_ value: Any?) -> Int? {
        guard let number = integer(value), number <= 100_000 else { return nil }
        return number
    }

    /// AppleSmartBattery `Temperature`, in hundredths of a degree Celsius.
    static func temperatureCelsius(_ value: Any?) -> Double? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        let celsius = number.doubleValue / 100
        return celsius.isFinite && (-40...150).contains(celsius) ? celsius : nil
    }

    /// IOPSCopyExternalPowerAdapterDetails `Watts`.
    static func adapterWatts(_ details: [String: Any]?) -> Int? {
        guard let number = integer(details?[kIOPSPowerAdapterWattsKey]), (1...1000).contains(number) else { return nil }
        return number
    }

    /// IOPS `Time to Full Charge` / `Time to Empty`: minutes, −1 while macOS
    /// is still calculating (nil here).
    static func minutes(_ value: Any?) -> Int? {
        guard let number = integer(value), number <= 100 * 60 else { return nil }
        return number
    }

    private static func integer(_ value: Any?) -> Int? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        let value = number.doubleValue
        guard value.isFinite, value >= 0, value <= 1_000_000, value.rounded(.towardZero) == value else { return nil }
        return Int(value)
    }
}

/// The one battery level rule for the whole HUD: above 70 % green, 20–70 %
/// yellow, below 20 % red (the shell ring, the charge bar and the Battery page).
enum BatteryLevelTone: Equatable {
    case green
    case yellow
    case red

    static func forPercentage(_ percentage: Int?) -> BatteryLevelTone? {
        guard let percentage = percentage, (0...100).contains(percentage) else { return nil }
        if percentage > 70 { return .green }
        if percentage >= 20 { return .yellow }
        return .red
    }
}
