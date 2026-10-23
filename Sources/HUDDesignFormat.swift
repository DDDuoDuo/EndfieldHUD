import Foundation

/// Text patterns and number formats of the redesign (spec §1.8).
enum HUDFormat {
    private static let posix = Locale(identifier: "en_US_POSIX")

    /// Zero-padded to two digits: 6 → "06", 10 → "10".
    static func pad2(_ value: Int) -> String { value < 10 && value >= 0 ? "0\(value)" : "\(value)" }

    /// mm:ss with padded minutes that may exceed 59 (60:00).
    static func clock(seconds: Int) -> String {
        let total = max(0, seconds)
        return pad2(total / 60) + ":" + pad2(total % 60)
    }

    /// en-US thousands separators: 3262 → "3,262".
    static func grouped(_ value: Int) -> String {
        let formatter = NumberFormatter()
        formatter.locale = Locale(identifier: "en_US"); formatter.numberStyle = .decimal
        return formatter.string(from: NSNumber(value: value)) ?? "\(value)"
    }

    /// GB with one decimal; from 1000 GB, TB with two (82.4 GB, 1.86 TB).
    static func storage(gigabytes: Double) -> String {
        gigabytes >= 1000 ? String(format: "%.2f TB", gigabytes / 1000) : String(format: "%.1f GB", gigabytes)
    }

    /// Dotted date `2026.10.07`, optionally with ` 21:04`.
    static func dotted(_ date: Date, time: Bool = false, calendar: Calendar = .current) -> String {
        let c = calendar.dateComponents([.year, .month, .day, .hour, .minute], from: date)
        let day = "\(c.year ?? 0)." + pad2(c.month ?? 0) + "." + pad2(c.day ?? 0)
        return time ? day + " " + pad2(c.hour ?? 0) + ":" + pad2(c.minute ?? 0) : day
    }

    /// Uppercase English weekday, `MONDAY`.
    static func weekday(_ date: Date, calendar: Calendar = .current) -> String {
        let formatter = DateFormatter(); formatter.locale = posix; formatter.calendar = calendar
        formatter.timeZone = calendar.timeZone; formatter.dateFormat = "EEEE"
        return formatter.string(from: date).uppercased(with: posix)
    }

    /// Calendar heading `October 2026`.
    static func monthHeading(_ date: Date, calendar: Calendar = .current) -> String {
        let formatter = DateFormatter(); formatter.locale = posix; formatter.calendar = calendar
        formatter.timeZone = calendar.timeZone; formatter.dateFormat = "MMMM yyyy"
        return formatter.string(from: date)
    }
}

/// Slider and readout formatters (spec §1.8 `FMT`).
enum HUDValueFormat: Equatable {
    case scale, percent, signedPercent, balance, seconds, pixels, page, signed, integer, clock
    /// Two decimals (Media Assembly tool values).
    case decimal

    func string(_ value: Double) -> String {
        let rounded = Int(value.rounded())
        switch self {
        case .scale: return String(format: "%.2f×", value)
        case .percent: return "\(rounded)%"
        case .signedPercent: return rounded > 0 ? "+\(rounded)%" : "\(rounded)%"
        case .balance: return abs(value) < 3 ? "C" : (value < 0 ? "L \(abs(rounded))" : "R \(rounded)")
        case .seconds: return "\(rounded) s"
        case .pixels: return "\(rounded) px"
        case .page: return "P. \(rounded)"
        case .signed: return rounded > 0 ? "+\(rounded)" : "\(rounded)"
        case .integer: return "\(rounded)"
        case .clock: return HUDFormat.clock(seconds: rounded)
        case .decimal: return String(format: "%.2f", value)
        }
    }
}
