import Foundation

/// These identifiers are saved in preferences. The alert's mode remains tied
/// to power connection; selecting a metric only changes its compact readings.
enum HUDChargeMetric: String, Codable, CaseIterable {
    case battery, ram, cpu, network, disk

    var requiresTelemetry: Bool { self != .battery }

    var title: String {
        switch self {
        case .battery: return L10n.text("Battery", "电池")
        case .ram: return L10n.text("RAM", "RAM")
        case .cpu: return "CPU"
        case .network: return L10n.text("Network", "网络")
        case .disk: return L10n.text("Disk", "磁盘")
        }
    }
}

/// A pure projection of an existing sample. It never reads hardware, schedules
/// work or substitutes the Mac's battery percentage for a different metric.
struct HUDChargeMetricReading: Equatable {
    let primary: String
    /// Includes the separator when a second reading is present.
    let secondary: String
    let unit: String
    let trailing: String
    /// A real bounded fraction only: traffic and disk I/O have no fixed maximum.
    let progress: Double?
    let accessibilityValue: String

    static func resolve(metric: HUDChargeMetric, battery: BatterySnapshot,
                        telemetry: SystemActivitySnapshot?) -> HUDChargeMetricReading {
        let unavailable = L10n.text("Unavailable", "不可用")
        switch metric {
        case .battery:
            let capacity = battery.hasBattery ? battery.capacity : nil
            let current = capacity.map { grouped($0.current) } ?? "—"
            let maximum = capacity.map { grouped($0.maximum) } ?? "—"
            let percent = battery.hasBattery ? battery.percentage.flatMap { (0...100).contains($0) ? Double($0) : nil } : nil
            let value = percent.map { decimal($0) + "%" } ?? "—"
            return Self(primary: current, secondary: "/" + maximum, unit: capacity?.unit.rawValue ?? "",
                        trailing: value, progress: percent.map { $0 / 100 },
                        accessibilityValue: metric.title + ": " + (percent == nil ? unavailable : value))
        case .ram:
            let total = telemetry?.memoryTotalBytes.flatMap { $0 > 0 ? $0 : nil }
            let used = telemetry?.memoryUsedBytes.flatMap { value -> UInt64? in
                guard let total, value <= total else { return nil }; return value
            }
            // Match the installed-memory convention (8/16/32 GB), using the
            // actual physical byte count rather than rounding to a model size.
            let gib = 1_073_741_824.0
            let current = used.map { decimal(Double($0) / gib) } ?? "—"
            let maximum = total.map { decimal(Double($0) / gib) } ?? "—"
            let fraction = used.flatMap { used in total.map { Double(used) / Double($0) } }
            let percentage = fraction.map { decimal($0 * 100) + "%" } ?? "—"
            return Self(primary: current, secondary: "/" + maximum, unit: "GB", trailing: percentage,
                        progress: fraction, accessibilityValue: metric.title + ": " + current + "/" + maximum + " GB, "
                            + (fraction == nil ? unavailable : percentage))
        case .cpu:
            let percent = telemetry?.cpuPercent.flatMap { $0.isFinite && (0...100).contains($0) ? $0 : nil }
            let value = percent.map { decimal($0) + "%" } ?? "—"
            return Self(primary: "CPU", secondary: "", unit: "", trailing: value,
                        progress: percent.map { $0 / 100 },
                        accessibilityValue: "CPU: " + (percent == nil ? unavailable : value))
        case .network:
            return rates(first: telemetry?.uploadBytesPerSecond, second: telemetry?.downloadBytesPerSecond,
                         trailing: "↑/↓", firstName: L10n.text("Upload ", "上传 "),
                         secondName: L10n.text("Download ", "下载 "))
        case .disk:
            return rates(first: telemetry?.diskReadBytesPerSecond, second: telemetry?.diskWriteBytesPerSecond,
                         trailing: "R/W", firstName: L10n.text("Read ", "读取 "),
                         secondName: L10n.text("Write ", "写入 "))
        }
    }

    private static func rates(first: Double?, second: Double?, trailing: String,
                              firstName: String, secondName: String) -> HUDChargeMetricReading {
        let first = first.flatMap { $0.isFinite && $0 >= 0 ? $0 : nil }
        let second = second.flatMap { $0.isFinite && $0 >= 0 ? $0 : nil }
        let largest = max(first ?? 0, second ?? 0)
        let units = ["B/s", "KB/s", "MB/s", "GB/s", "TB/s"]
        var index = 0, divisor = 1.0
        while index < units.count - 1 && largest / divisor >= 1000 { index += 1; divisor *= 1000 }
        let a = first.map { decimal($0 / divisor, keepingFraction: true) } ?? "—"
        let b = second.map { decimal($0 / divisor, keepingFraction: true) } ?? "—"
        let unit = units[index], unavailable = L10n.text("Unavailable", "不可用")
        return Self(primary: a, secondary: "/" + b, unit: unit, trailing: trailing, progress: nil,
                    accessibilityValue: firstName + (first == nil ? unavailable : a + " " + unit)
                        + ", " + secondName + (second == nil ? unavailable : b + " " + unit))
    }

    private static func decimal(_ value: Double, keepingFraction: Bool = false) -> String {
        let result = String(format: value >= 10_000 ? "%.1e" : "%.1f", locale: Locale(identifier: "en_US_POSIX"), value)
        return !keepingFraction && result.hasSuffix(".0") ? String(result.dropLast(2)) : result
    }

    private static func grouped(_ value: Int) -> String {
        let digits = String(value)
        return digits.reversed().enumerated().map { index, character in
            (index > 0 && index % 3 == 0 ? "," : "") + String(character)
        }.joined().reversed().map(String.init).joined()
    }
}
