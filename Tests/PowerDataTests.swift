import Foundation
import IOKit.ps

enum PowerDataTests {
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
        return assertionCount
    }
}
