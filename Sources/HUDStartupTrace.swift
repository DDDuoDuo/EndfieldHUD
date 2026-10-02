import Foundation
import QuartzCore

/// Opt-in, bounded launch diagnostics. No timer or retained frame history.
enum HUDStartupTrace {
    static let enabled = CommandLine.arguments.contains("--ui-test") && CommandLine.arguments.contains("--startup-timing")
    static func begin() -> TimeInterval { enabled ? CACurrentMediaTime() : 0 }
    static func end(_ phase: String, since start: inout TimeInterval) {
        guard enabled else { return }
        let now = CACurrentMediaTime()
        fputs(String(format: "HUD startup %@ %.2f ms\n", phase, (now - start) * 1000), stderr)
        start = now
    }
}
