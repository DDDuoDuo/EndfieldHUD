import AppKit
import QuartzCore

/// Actual detached VolumeCanvas, synthetic controller/routes only. This exports
/// source layers and controls, not copied drawing helpers or expected values.
@main enum VolumeReference {
    static func require(_ value: @autoclosure () -> Bool, _ message: String) throws {
        if !value() { throw HUDSourceError.invalid(message) }
    }
    static func write(_ value: Any, _ url: URL) throws {
        try JSONSerialization.data(withJSONObject: value, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]).write(to: url, options: .atomic)
    }
    static func run() throws {
        try require(Thread.isMainThread && CommandLine.arguments.count == 3 && CommandLine.arguments[1] == "--output", "Use isolated volume_reference.sh")
        try require(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil, "Temporary home is required")
        let output = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.ambientAnimation = false
        configuration.reduceMotion = false; configuration.launchAtLogin = false
        HUDRuntimeAppearance.configuration = configuration; L10n.language = .english
        var state = AudioDeviceSnapshot()
        for number in 0..<8 {
            var device = AudioDeviceInfo(id: UInt32(number + 10), name: "Output \(number)")
            device.outputChannels = 2; device.isHeadphones = number % 2 == 1; device.isBluetooth = number == 3
            state.outputs.append(device)
            var input = AudioDeviceInfo(id: UInt32(number + 100), name: "Input \(number)")
            input.inputChannels = 1; state.inputs.append(input)
        }
        state.defaultOutputID = 10; state.defaultInputID = 100
        state.outputVolume = 0.55; state.canSetOutputVolume = true; state.outputMuted = false; state.canSetOutputMute = true
        state.balance = 0; state.canSetBalance = true; state.canSetDefaultOutput = true; state.canSetDefaultInput = true
        state.applicationActivitySupported = true
        state.availableApplications = (0..<12).map { AudioApplicationInfo(id: UInt32($0 + 40), pid: Int32($0 + 1000), name: "App \($0)", outputDeviceIDs: [10]) }
        state.activeApplications = state.availableApplications
        let controller = AudioDeviceController(snapshot: state), perApp = PerAppAudioController.fixture()
        let canvas = VolumeCanvas(controller: controller, perAppAudio: perApp)
        defer { canvas.deactivate(); perApp.stopAll() }
        let encoder = try ModuleReferenceLayerEncoder(output: output)
        _ = canvas.makeContent(for: .volume, style: HUDModuleContentStyle(dark: true, accent: configuration.accentColor, contentsScale: 2))
        canvas.activate()
        var entries: [[String: Any]] = []
        func emit(_ name: String) throws {
            let actions: [[String: Any]] = canvas.accessibleActions.map { ["id": $0.id, "label": $0.label, "rect": ModuleReferenceLayerEncoder.rect($0.rect), "enabled": $0.enabled] }
            let sliders: [[String: Any]] = canvas.accessibleSliders.map { ["id": $0.id, "label": $0.label, "rect": ModuleReferenceLayerEncoder.rect($0.rect), "visibleRect": $0.visibleRect.map(ModuleReferenceLayerEncoder.rect) as Any? ?? NSNull(), "value": $0.value as Any? ?? NSNull(), "minimum": $0.minimum, "maximum": $0.maximum, "enabled": $0.enabled] }
            let root = try encoder.encode(canvas.layer, id: "volume/" + name)
            var row: [String: Any] = ["schemaVersion": 1, "state": name, "root": root, "actions": actions, "sliders": sliders, "pageIndex": canvas.pageIndex, "scrollOffset": canvas.applicationScrollOffset]
            if let content = canvas.layer.sublayers?.first, let move = content.animation(forKey: "action.volume.depth") as? CABasicAnimation {
                guard let from = move.fromValue as? NSValue, let to = move.toValue as? NSValue else { throw HUDSourceError.invalid("Original action endpoints missing") }
                var first: [Float] = [0,0], second: [Float] = [0,0]
                first.withUnsafeMutableBufferPointer { move.timingFunction!.getControlPoint(at: 1, values: $0.baseAddress!) }
                second.withUnsafeMutableBufferPointer { move.timingFunction!.getControlPoint(at: 2, values: $0.baseAddress!) }
                row["actionAnimation"] = ["from": ModuleReferenceLayerEncoder.transform(from.caTransform3DValue), "to": ModuleReferenceLayerEncoder.transform(to.caTransform3DValue), "duration": move.duration, "curve": first + second]
            }
            try write(row, output.appendingPathComponent("volume-" + name + ".json"))
            entries.append(["file": "volume-" + name + ".json", "state": name])
        }
        try emit("main")
        _ = canvas.scroll(at: CGPoint(x: 20, y: 260), delta: 30); try emit("applications-scroll")
        canvas.perform(actionID: "audio:headphones"); try emit("headphones")
        canvas.perform(actionID: "audio:output"); try emit("output-chooser")
        _ = canvas.scroll(at: CGPoint(x: 20, y: 70), delta: 40); try emit("output-next")
        _ = canvas.dismissChooser(); canvas.perform(actionID: "audio:input"); try emit("input-chooser")
        _ = canvas.dismissChooser(); canvas.perform(actionID: "audio:applications")
        _ = canvas.setSlider(id: "volume", value: 0); try emit("zero-volume")
        configuration.theme = .light; HUDRuntimeAppearance.configuration = configuration
        _ = canvas.makeContent(for: .volume, style: HUDModuleContentStyle(dark: false, accent: configuration.accentColor, contentsScale: 2)); try emit("light")
        try write(["windowCount": NSApp.windows.count, "sourceMetadataLimitations": encoder.unsupported], output.appendingPathComponent("export-diagnostics.json"))
        let unsupported = encoder.unsupported.filter { $0["category"] != "metadata-only-animation" && $0["feature"] != "layer subclass HUDControlHighlightLayer" }
        try require(NSApp.windows.isEmpty && unsupported.isEmpty, "Detached export must not create windows or unsupported layers")
        try write(["schemaVersion": 1, "entries": entries, "rasterAssets": encoder.rasterAssets, "windowCreated": false, "realAudioActivated": false,
                   "sourceMetadataLimitations": encoder.unsupported,
                   "limitations": ["Model-layer source reference; native text raster/font and intermediate CoreAnimation rendering remain separate gates."]], output.appendingPathComponent("volume-reference.json"))
    }
    static func main() { do { try run(); print("PASS detached actual-source Volume reference") } catch { fputs("Volume source export failed: \(error)\n", stderr); exit(1) } }
}
