import AppKit

/// Exports behaviour of the UNCHANGED Mac sources for the Windows audio port:
///  - SystemEventRecorder.receiveAudioDevices / recordTopology ordering;
///  - AudioVolumeMath.balance and AudioVolumeMath.stereo;
///  - localizedStandardCompare ordering of endpoint/app names (en).
/// The redesigned Volume canvas (HUDVolumeInteraction was removed by the 1.3.0
/// redesign) is exported by utility_canvases_reference.swift instead.
/// Synthetic fixtures only: an in-memory event log; no HAL, window, file or audio access.
@main enum AudioReference {
    static func require(_ value: @autoclosure () -> Bool, _ message: String) throws {
        if !value() { throw HUDSourceError.invalid(message) }
    }
    static func json(_ value: Any) throws -> Data {
        try JSONSerialization.data(withJSONObject: value, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes])
    }

    static func topology() -> [[String: Any]] {
        let log = SystemEventLog()
        let recorder = SystemEventRecorder(log: log)
        let steps: [[String: String]] = [
            ["{0.0.0.00000000}.{a}": "Speakers (Synthetic Audio)", "{0.0.1.00000000}.{m}": "Microphone (Synthetic Audio)"],
            ["{0.0.0.00000000}.{a}": "Speakers (Synthetic Audio)", "{0.0.1.00000000}.{m}": "Microphone (Synthetic Audio)",
             "{0.0.0.00000000}.{h}": "Headphones (Synthetic USB)", "{0.0.1.00000000}.{h}": "Headset Microphone (Synthetic USB)"],
            ["{0.0.0.00000000}.{a}": "Speakers (Synthetic Audio)", "{0.0.0.00000000}.{b}": "Buds (Synthetic Bluetooth)"],
            ["{0.0.0.00000000}.{a}": "Speakers (Synthetic Audio)", "{0.0.0.00000000}.{b}": "Buds (Synthetic Bluetooth)"],
            [:],
            ["{0.0.0.00000000}.{z}": "扬声器 (合成)", "{0.0.0.00000000}.{a}": "Speakers (Synthetic Audio)"],
        ]
        var rows: [[String: Any]] = []
        for devices in steps {
            let before = log.events.count
            recorder.receiveAudioDevices(devices)
            // The log is newest-first; report this step in recording order.
            let recorded = log.events.prefix(log.events.count - before).reversed().map { event -> [String: Any] in
                ["kind": event.kind == .audioDeviceConnected ? "connected" : "disconnected", "device": event.metadata["device"] ?? ""]
            }
            rows.append(["devices": devices.map { ["id": $0.key, "name": $0.value] }.sorted { ($0["id"] as! String) < ($1["id"] as! String) }, "events": recorded])
        }
        return rows
    }

    static func balance() -> [[String: Any]] {
        var rows: [[String: Any]] = []
        let levels: [Double] = [0, 0.1, 0.25, 0.5, 0.75, 0.9, 1]
        for left in levels { for right in levels {
            rows.append(["left": left, "right": right, "balance": AudioVolumeMath.balance(left: left, right: right)])
        } }
        return rows
    }
    static func stereo() -> [[String: Any]] {
        var rows: [[String: Any]] = []
        for volume in [0, 0.3, 0.55, 1] as [Double] { for value in [-1, -0.6, -0.25, 0, 0.25, 0.6, 1] as [Double] {
            rows.append(["volume": volume, "balance": value, "levels": AudioVolumeMath.stereo(volume: volume, balance: value)])
        } }
        return rows
    }
    static func ordering() -> [String] {
        let names = ["Speakers 10", "speakers 2", "Speakers 1", "Headphones", "headphones B", "Headphones A", "USB Audio 12",
                     "USB Audio 3", "Bluetooth Buds", "Line In 05", "Line In 4", "Zeta", "alpha"]
        return names.sorted { $0.localizedStandardCompare($1) == .orderedAscending }
    }

    static func run() throws {
        let arguments = CommandLine.arguments
        try require(Thread.isMainThread && arguments.count >= 5 && arguments[1] == "--output" && arguments[3] == "--source-authority",
                    "Use isolated audio_reference.sh")
        try require(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil, "Temporary home is required")
        let output = URL(fileURLWithPath: arguments[2], isDirectory: true)
        NSApplication.shared.setActivationPolicy(.prohibited)
        L10n.language = .english

        let document: [String: Any] = ["schemaVersion": 1, "sourceAuthority": arguments[4],
            "topology": topology(), "balance": balance(), "stereo": stereo(), "ordering": ordering(),
            "windowCreated": !NSApp.windows.isEmpty, "realAudioActivated": false]
        try require(NSApp.windows.isEmpty, "Detached export must not create windows")
        try json(document).write(to: output.appendingPathComponent("audio-endpoint-source.json"), options: .atomic)
    }
    static func main() { do { try run(); print("PASS detached actual-source audio reference") } catch { fputs("Audio source export failed: \(error)\n", stderr); exit(1) } }
}
