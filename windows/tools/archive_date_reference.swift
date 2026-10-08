import Foundation

// Compiled beside the byte-for-byte date methods extracted from ArchiveCanvas.
@main enum ArchiveDateReference {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else { throw NSError(domain: "ArchiveDates", code: 1) }
        let dates = ["2001-01-01", "2026-10-08", "2024-02-29", "2023-02-29", "2000-02-29", "1900-02-29",
            "1600-01-01", "1582-10-04", "1582-10-10", "1582-10-15", "0001-01-01", "0000-01-01",
            "10000-01-01", "2026-1-01", "26-01-01", "2026-00-01", "2026-13-01", "2026-04-31",
            "2026-10-08 ", " 2026-10-08", "2026-10-08x", "２０２６-１０-０８", "", "\u{0}2026-10-08",
            "2011-12-30", "2026-03-08", "2026-11-01"]
        let seconds: [Double] = [0, -1, 86399, 86400, 813196800, -12622780800, 252424080000]
        var rows: [[String: Any]] = []
        for zone in ["Etc/UTC", "Asia/Shanghai", "America/Los_Angeles", "Pacific/Apia"] {
            NSTimeZone.default = TimeZone(identifier: zone)!
            for text in dates {
                let parsed = ArchiveCanvas.parseDate(text)
                rows.append(["zone": zone, "text": text, "seconds": parsed.map { $0.timeIntervalSinceReferenceDate } as Any? ?? NSNull()])
            }
            for value in seconds {
                rows.append(["zone": zone, "seconds": value, "formatted": ArchiveCanvas.dateString(Date(timeIntervalSinceReferenceDate: value))])
            }
        }
        try JSONSerialization.data(withJSONObject: ["schemaVersion": 1, "rows": rows], options: [.sortedKeys, .prettyPrinted])
            .write(to: URL(fileURLWithPath: CommandLine.arguments[1]), options: .atomic)
        print("PASS \(rows.count) source Archive date cases")
    }
}
