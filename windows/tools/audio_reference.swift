import AppKit

/// Exports behaviour of the UNCHANGED Mac sources for the Windows audio port:
///  - SystemEventRecorder.receiveAudioDevices / recordTopology ordering;
///  - AudioVolumeMath.balance and AudioVolumeMath.stereo;
///  - localizedStandardCompare ordering of endpoint/app names (en);
///  - the Volume accessibility contract (HUDVolumeInteraction projected
///    buttons/sliders: labels, help, values) for synthetic controller states.
/// Synthetic fixtures only: in-memory event log, AudioDeviceController(snapshot:)
/// and PerAppAudioController.fixture(); no HAL, window, file or audio access.
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

    static func accessibility(state snapshot: AudioDeviceSnapshot, name: String, prepare: (VolumeCanvas) -> Void = { _ in }) throws -> [String: Any] {
        let controller = AudioDeviceController(snapshot: snapshot), perApp = PerAppAudioController.fixture()
        let canvas = VolumeCanvas(controller: controller, perAppAudio: perApp)
        defer { canvas.deactivate(); perApp.stopAll() }
        _ = canvas.makeContent(for: .volume, style: HUDModuleContentStyle(dark: true, accent: HUDRuntimeAppearance.configuration.accentColor, contentsScale: 2))
        let host = NSView(frame: NSRect(x: 0, y: 0, width: 400, height: 334))
        let interaction = HUDVolumeInteraction(canvas: canvas, host: host)
        interaction.setActive(true)
        prepare(canvas)
        interaction.layoutAccessibility()
        var buttons: [[String: Any]] = [], sliders: [[String: Any]] = []
        for view in host.subviews where !view.isHidden {
            if let slider = view as? NSSlider {
                let value = slider.accessibilityValue()
                sliders.append(["label": slider.accessibilityLabel() ?? "", "help": slider.accessibilityHelp() ?? "", "enabled": slider.isEnabled,
                                "minimum": slider.minValue, "maximum": slider.maxValue,
                                "value": (value as? NSNumber).map { $0.doubleValue as Any } ?? NSNull(), "valueText": (value as? String) ?? NSNull()])
            } else if let button = view as? NSButton {
                buttons.append(["label": button.accessibilityLabel() ?? "", "help": button.accessibilityHelp() ?? "", "enabled": button.isEnabled])
            }
        }
        let actionOrder = canvas.accessibleActions.map(\.label), sliderOrder = canvas.accessibleSliders.map(\.label)
        buttons.sort { actionOrder.firstIndex(of: $0["label"] as! String)! < actionOrder.firstIndex(of: $1["label"] as! String)! }
        sliders.sort { sliderOrder.firstIndex(of: $0["label"] as! String)! < sliderOrder.firstIndex(of: $1["label"] as! String)! }
        try require(buttons.count == actionOrder.count && sliders.count == sliderOrder.count, "Every projected control is exported")
        return ["state": name, "status": canvas.accessibilityStatus, "buttons": buttons, "sliders": sliders]
    }

    static func base() -> AudioDeviceSnapshot {
        var state = AudioDeviceSnapshot()
        state.outputs = [AudioDeviceInfo(id: 10, name: "Speakers", outputChannels: 2),
                         AudioDeviceInfo(id: 11, name: "Headphones", outputChannels: 2, isHeadphones: true),
                         AudioDeviceInfo(id: 12, name: "Buds", outputChannels: 2, isBluetooth: true)]
        state.inputs = [AudioDeviceInfo(id: 20, name: "Microphone", inputChannels: 1)]
        state.defaultOutputID = 10; state.defaultInputID = 20
        state.outputVolume = 0.55; state.canSetOutputVolume = true; state.outputMuted = false; state.canSetOutputMute = true
        state.balance = -0.25; state.canSetBalance = true
        // Windows has no public default-device API: the chooser stays disabled.
        state.canSetDefaultOutput = false; state.canSetDefaultInput = false
        state.applicationActivitySupported = true
        state.availableApplications = [AudioApplicationInfo(id: 40, pid: 1000, name: "Player", outputDeviceIDs: [10]),
                                       AudioApplicationInfo(id: 41, pid: 1001, name: "PID 1001", outputDeviceIDs: [10])]
        state.activeApplications = state.availableApplications
        return state
    }

    static func run() throws {
        try require(Thread.isMainThread && CommandLine.arguments.count == 3 && CommandLine.arguments[1] == "--output", "Use isolated audio_reference.sh")
        try require(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil, "Temporary home is required")
        let output = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.ambientAnimation = false
        configuration.reduceMotion = true; configuration.launchAtLogin = false
        HUDRuntimeAppearance.configuration = configuration; L10n.language = .english

        var states: [[String: Any]] = []
        states.append(try accessibility(state: base(), name: "main"))
        states.append(try accessibility(state: base(), name: "app-active") { _ = $0.setSlider(id: "app:40", value: 0.5) })
        var fixed = base(); fixed.canSetOutputVolume = false; fixed.balance = nil; fixed.canSetBalance = false
        states.append(try accessibility(state: fixed, name: "fixed"))
        var centered = base(); centered.balance = 0.004
        states.append(try accessibility(state: centered, name: "centered"))
        var right = base(); right.balance = 0.6; right.outputVolume = nil; right.canSetOutputVolume = false
        states.append(try accessibility(state: right, name: "right-unavailable"))
        states.append(try accessibility(state: base(), name: "headphones") { $0.perform(actionID: "audio:headphones") })

        let document: [String: Any] = ["schemaVersion": 1, "sourceAuthority": "ca04f142185c7de40acd8523bdb563195d90a1d1",
            "topology": topology(), "balance": balance(), "stereo": stereo(), "ordering": ordering(), "accessibility": states,
            "windowCreated": !NSApp.windows.isEmpty, "realAudioActivated": false]
        try require(NSApp.windows.isEmpty, "Detached export must not create windows")
        try json(document).write(to: output.appendingPathComponent("audio-endpoint-source.json"), options: .atomic)
    }
    static func main() { do { try run(); print("PASS detached actual-source audio reference") } catch { fputs("Audio source export failed: \(error)\n", stderr); exit(1) } }
}
