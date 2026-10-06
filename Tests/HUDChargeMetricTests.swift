import Foundation

enum HUDChargeMetricTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) { count += 1; precondition(condition, message) }
        let previousLanguage = L10n.language
        L10n.language = .english
        defer { L10n.language = previousLanguage }

        let battery = BatterySnapshot(percentage: 50, isPluggedIn: true, isCharging: false,
            isFullyCharged: false, hasBattery: true,
            capacity: BatteryCapacityReading(current: 2400, maximum: 4800, unit: .milliampHours))
        func sample(cpu: Double? = 23.5, used: UInt64? = 6 * 1_073_741_824,
                    total: UInt64? = 8 * 1_073_741_824, upload: Double? = 6000,
                    download: Double? = 19_000, read: Double? = 6_000_000,
                    write: Double? = 19_000_000) -> SystemActivitySnapshot {
            SystemActivitySnapshot(timestamp: Date(timeIntervalSince1970: 1), uptime: 1, cpuPercent: cpu,
                memoryUsedBytes: used, memoryTotalBytes: total, memoryCompressedBytes: 0,
                uploadBytesPerSecond: upload, downloadBytesPerSecond: download,
                diskReadBytesPerSecond: read, diskWriteBytesPerSecond: write, statusNotes: [])
        }
        func reading(_ metric: HUDChargeMetric, _ value: SystemActivitySnapshot? = sample()) -> HUDChargeMetricReading {
            HUDChargeMetricReading.resolve(metric: metric, battery: battery, telemetry: value)
        }

        for metric in HUDChargeMetric.allCases {
            check((try? JSONDecoder().decode(HUDChargeMetric.self, from: JSONEncoder().encode(metric))) == metric,
                  "Every alert metric has a stable Codable identifier")
            check(metric.requiresTelemetry == (metric != .battery), "Only nonbattery readings require shared telemetry")
        }
        check((try? JSONDecoder().decode(HUDChargeMetric.self, from: Data("\"future\"".utf8))) == nil,
              "An unknown saved metric is rejected for the configuration decoder to default safely")
        let capacity = reading(.battery, nil)
        check(capacity.primary == "2,400" && capacity.secondary == "/4,800" && capacity.unit == "mAh"
              && capacity.trailing == "50%" && capacity.progress == 0.5, "Battery preserves capacity units and its real percentage")
        let ram = reading(.ram)
        check(ram.primary == "6" && ram.secondary == "/8" && ram.unit == "GB" && ram.trailing == "75%"
              && ram.progress == 0.75, "RAM shows used/physical GB and memory usage, not the battery percentage")
        let cpu = reading(.cpu)
        check(cpu.primary == "CPU" && cpu.secondary.isEmpty && cpu.unit.isEmpty && cpu.trailing == "23.5%"
              && cpu.progress == 0.235, "CPU shows total normalized system load")
        let network = reading(.network)
        check(network.primary == "6.0" && network.secondary == "/19.0" && network.unit == "KB/s"
              && network.trailing == "↑/↓" && network.progress == nil, "Network pairs upload/download in shared units without a fabricated percentage")
        check(network.accessibilityValue == "Upload 6.0 KB/s, Download 19.0 KB/s", "Network directions are explicit to accessibility clients")
        let disk = reading(.disk)
        check(disk.primary == "6.0" && disk.secondary == "/19.0" && disk.unit == "MB/s" && disk.trailing == "R/W"
              && disk.progress == nil, "Disk pairs read/write rates without borrowing the battery ring percentage")
        check(disk.accessibilityValue == "Read 6.0 MB/s, Write 19.0 MB/s", "Disk directions remain explicit")

        for metric in HUDChargeMetric.allCases where metric.requiresTelemetry {
            let unknown = reading(metric, nil)
            check(unknown.progress == nil && unknown.accessibilityValue.contains("Unavailable"),
                  "A missing sample stays unavailable rather than showing invented zero or demo data")
            check(reading(metric, .unavailable) == unknown, "The initial monitor snapshot matches the unavailable representation")
        }
        for invalid in [Double.nan, .infinity, -.infinity, -1, 100.1] {
            check(reading(.cpu, sample(cpu: invalid)).trailing == "—", "Invalid CPU readings cannot escape the 0–100% contract")
        }
        check(reading(.cpu, sample(cpu: 0)).trailing == "0%" && reading(.cpu, sample(cpu: 100)).progress == 1,
              "Valid empty and full CPU values remain real data")
        check(reading(.ram, sample(used: 9, total: 8)).primary == "—"
              && reading(.ram, sample(used: 9, total: 8)).progress == nil, "RAM above physical capacity is unavailable")
        check(reading(.ram, sample(total: 0)).secondary == "/—", "Zero physical capacity is not a usable denominator")
        check(reading(.ram, sample(used: nil)).secondary == "/8", "A known RAM total survives a missing used reading")
        check(reading(.ram, sample(used: 0)).progress == 0, "An explicitly reported zero memory value is retained")

        for invalid in [Double.nan, .infinity, -.infinity, -1] {
            let partial = reading(.network, sample(upload: invalid, download: 19_000))
            check(partial.primary == "—" && partial.secondary == "/19.0" && partial.unit == "KB/s",
                  "Invalid upload values do not discard a separately valid download reading")
            check(reading(.disk, sample(read: 10, write: invalid)).secondary == "/—", "A missing disk direction stays unknown")
        }
        let idle = reading(.network, sample(upload: 0, download: 0))
        check(idle.primary == "0.0" && idle.secondary == "/0.0" && idle.unit == "B/s", "Real idle traffic is distinct from unavailable counters")
        check(reading(.network, sample(upload: 1_000, download: 1)).secondary == "/0.0",
              "Paired rates share one clearly displayed unit")
        let enormous = reading(.disk, sample(read: Double.greatestFiniteMagnitude, write: 1))
        check(enormous.primary.count < 12 && enormous.unit == "TB/s", "Finite extreme values use bounded scientific text instead of enormous strings")
        let missingBattery = HUDChargeMetricReading.resolve(metric: .battery, battery: .unavailable, telemetry: sample())
        check(missingBattery.primary == "—" && missingBattery.secondary == "/—" && missingBattery.trailing == "—"
              && missingBattery.progress == nil, "A valid system sample cannot invent battery capacity")
        return count
    }
}
