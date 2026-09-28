import Foundation

enum DeviceBatteryKind: Equatable {
    case mac
    case accessory
}

enum DeviceBatteryAvailability: Equatable {
    case available
    case unavailable
    case noBattery
}

struct DeviceBatteryReading: Equatable {
    let id: String
    let kind: DeviceBatteryKind
    let name: String
    let percentage: Int?
    let availability: DeviceBatteryAvailability
}

enum AccessoryBatteryCapability: Equatable {
    /// This provider does not expose accessory batteries. This says nothing
    /// about whether the user owns or has connected a particular accessory.
    case notProvided
}

/// A pure adapter for the shared host snapshot. Opening the device popup does
/// not create another battery monitor, start polling, enumerate peripherals,
/// connect Bluetooth, or request permission. A future explicitly enabled
/// accessory provider can contribute real readings through the same model.
struct DeviceBatteryProvider {
    let accessoryCapability: AccessoryBatteryCapability = .notProvided

    func readings(for snapshot: BatterySnapshot) -> [DeviceBatteryReading] {
        let percentage = snapshot.hasBattery
            ? snapshot.percentage.flatMap { (0...100).contains($0) ? $0 : nil }
            : nil
        let availability: DeviceBatteryAvailability = snapshot.hasBattery
            ? (percentage == nil ? .unavailable : .available)
            : .noBattery

        // This stable, nonidentifying ID also works on a desktop Mac. Unknown
        // accessories are not represented by invented names or percentages.
        return [DeviceBatteryReading(id: "host-mac", kind: .mac, name: "This Mac",
                                     percentage: percentage, availability: availability)]
    }
}
