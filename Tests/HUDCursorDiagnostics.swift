// Opt-in, bounded diagnostic using the full production OverlayController.
// No event filtering, cursor warping, global event posting, or direct cursorUpdate calls.
import AppKit
import ObjectiveC
import ScreenCaptureKit
import CoreMedia
import Darwin

private enum CursorTrace {
    static let began = CACurrentMediaTime()
    static var rows: [[String: Any]] = []
    static var counts: [String: Int] = [:]
    static var active = false
    static var busy = false
    static weak var source: HUDSourceWatchView?
    static weak var knownHUDCursor: NSCursor?
    static weak var window: NSWindow?
    static var stage = "launch"
    static var context: [String] = []
    static var setterStacks: [String: Int] = [:]
    static func object(_ value: AnyObject?) -> String {
        guard let value else { return "nil" }
        return "\(type(of: value))@\(Unmanaged.passUnretained(value).toOpaque())"
    }
    static func cursor(_ value: NSCursor) -> String {
        if value === NSCursor.arrow { return "arrow" }
        if value === NSCursor.iBeam { return "iBeam" }
        if value === NSCursor.dragCopy { return "dragCopy" }
        if value === knownHUDCursor { return "Endfield" }
        return object(value)
    }
    static func record(_ kind: String, _ extra: [String: Any] = [:]) {
        guard active, !busy else { return }; busy = true; defer { busy = false }
        counts[kind, default: 0] += 1
        guard rows.count < 16000 else { return }
        var row = extra
        row["time"] = CACurrentMediaTime() - began; row["kind"] = kind; row["stage"] = stage
        row["threadMain"] = Thread.isMainThread
        if Thread.isMainThread {
            row["cursor"] = cursor(NSCursor.current); row["key"] = window?.isKeyWindow ?? false
            row["appActive"] = NSApp.isActive
            row["context"] = context
        }
        rows.append(row)
    }
    static func event(_ event: NSEvent) -> [String: Any] {
        var value: [String: Any] = ["eventType": event.type.rawValue,
            "eventTimestamp": event.timestamp, "windowNumber": event.windowNumber,
            "location": [event.locationInWindow.x, event.locationInWindow.y]]
        if [.mouseEntered, .mouseExited, .cursorUpdate].contains(event.type) {
            value["trackingArea"] = object(event.trackingArea)
            value["trackingOwner"] = object(event.trackingArea?.owner)
            value["trackingOptions"] = event.trackingArea?.options.rawValue ?? 0
        }
        return value
    }
    static func hookVoid(_ type: AnyClass, _ selector: Selector) {
        guard let method = class_getInstanceMethod(type, selector) else { return }
        let original = unsafeBitCast(method_getImplementation(method),
            to: (@convention(c) (AnyObject, Selector) -> Void).self)
        let name = "\(NSStringFromClass(type)).\(NSStringFromSelector(selector))"
        let block: @convention(block) (AnyObject) -> Void = { receiver in
            guard active, Thread.isMainThread else { original(receiver, selector); return }
            context.append(name); defer { context.removeLast() }
            var fields: [String: Any] = ["receiver": object(receiver)]
            if let selected = receiver as? NSCursor {
                let label = cursor(selected); fields["selected"] = label
                setterStacks[label, default: 0] += 1
                if setterStacks[label, default: 0] <= 30 { fields["stack"] = Thread.callStackSymbols.prefix(18).map { $0 } }
            }
            record(name + ".before", fields)
            original(receiver, selector)
            record(name + ".after", ["receiver": object(receiver)])
        }
        let replacement = imp_implementationWithBlock(block)
        if !class_addMethod(type, selector, replacement, method_getTypeEncoding(method)) {
            method_setImplementation(class_getInstanceMethod(type, selector)!, replacement)
        }
    }
    static func hookObject(_ type: AnyClass, _ selector: Selector) {
        guard let method = class_getInstanceMethod(type, selector) else { return }
        let original = unsafeBitCast(method_getImplementation(method),
            to: (@convention(c) (AnyObject, Selector, AnyObject) -> Void).self)
        let name = "\(NSStringFromClass(type)).\(NSStringFromSelector(selector))"
        let block: @convention(block) (AnyObject, AnyObject) -> Void = { receiver, argument in
            guard active, Thread.isMainThread else { original(receiver, selector, argument); return }
            let input = argument as? NSEvent
            let interesting = input.map { [.cursorUpdate, .mouseEntered, .mouseExited, .mouseMoved,
                .leftMouseDown, .leftMouseUp, .leftMouseDragged].contains($0.type) } ?? true
            guard interesting else { original(receiver, selector, argument); return }
            context.append(name); defer { context.removeLast() }
            var fields = input.map(event) ?? ["argument": object(argument)]
            fields["receiver"] = object(receiver)
            if let area = argument as? NSTrackingArea {
                fields["owner"] = object(area.owner); fields["options"] = area.options.rawValue
            }
            record(name + ".before", fields)
            original(receiver, selector, argument)
            record(name + ".after", ["receiver": object(receiver)])
        }
        let replacement = imp_implementationWithBlock(block)
        if !class_addMethod(type, selector, replacement, method_getTypeEncoding(method)) {
            method_setImplementation(class_getInstanceMethod(type, selector)!, replacement)
        }
    }
    static func install() {
        // Install subclass overrides before base implementations, so captured
        // original IMPs remain the genuine implementation at that class level.
        for type in [SystemHUDView.self, HUDSourceWatchView.self] as [AnyClass] {
            hookVoid(type, #selector(NSView.updateTrackingAreas)); hookVoid(type, #selector(NSView.layout))
            hookObject(type, #selector(NSResponder.cursorUpdate(with:)))
        }
        hookVoid(NSCursor.self, #selector(NSCursor.set))
        if let cursorType = object_getClass(NSCursor.self) {
            hookVoid(cursorType, #selector(NSCursor.hide)); hookVoid(cursorType, #selector(NSCursor.unhide))
        }
        hookVoid(NSWindow.self, #selector(NSWindow.resetCursorRects))
        hookObject(NSWindow.self, #selector(NSWindow.invalidateCursorRects(for:)))
        hookObject(NSWindow.self, #selector(NSWindow.sendEvent(_:)))
        hookObject(NSView.self, #selector(NSView.addTrackingArea(_:)))
        hookObject(NSView.self, #selector(NSView.removeTrackingArea(_:)))
        hookObject(NSView.self, #selector(NSResponder.cursorUpdate(with:)))
        active = true
    }
}

@available(macOS 15.2, *)
private final class CursorWindowCapture: NSObject, SCStreamOutput, SCStreamDelegate, SCRecordingOutputDelegate {
    let queue = DispatchQueue(label: "EndfieldHUD.CursorDiagnosticCapture")
    var stream: SCStream?
    var recording: SCRecordingOutput?
    var frames = 0
    var failure: String?
    var finished: (() -> Void)?
    @MainActor func start(window: NSWindow, output: URL) async throws {
        guard CGPreflightScreenCaptureAccess() else {
            CursorTrace.record("capture.unavailable", ["reason": "No existing screen-capture permission; not requesting it"]); return
        }
        let content = try await SCShareableContent.currentProcess
        guard let own = content.windows.first(where: { $0.windowID == CGWindowID(window.windowNumber) }),
              own.owningApplication?.processID == getpid() else { throw NSError(domain: "own-window", code: 1) }
        let filter = SCContentFilter(desktopIndependentWindow: own)
        guard filter.includedWindows.map(\.windowID) == [own.windowID] else { throw NSError(domain: "own-window", code: 2) }
        let config = SCStreamConfiguration()
        config.width = Int(window.frame.width * window.backingScaleFactor)
        config.height = Int(window.frame.height * window.backingScaleFactor)
        config.minimumFrameInterval = CMTime(value: 1, timescale: 60)
        config.showsCursor = true; config.capturesAudio = false; config.includeChildWindows = false
        config.ignoreShadowsSingleWindow = true; config.shouldBeOpaque = true; config.queueDepth = 3
        let stream = SCStream(filter: filter, configuration: config, delegate: self); self.stream = stream
        try stream.addStreamOutput(self, type: .screen, sampleHandlerQueue: queue)
        let file = SCRecordingOutputConfiguration()
        file.outputURL = output.appendingPathComponent("full-hud.mp4")
        file.outputFileType = .mp4; file.videoCodecType = .h264
        let recording = SCRecordingOutput(configuration: file, delegate: self); self.recording = recording
        try stream.addRecordingOutput(recording)
        try await stream.startCapture()
        CursorTrace.record("capture.started", ["ownWindowID": own.windowID, "ownProcessID": getpid(),
            "width": config.width, "height": config.height, "scope": "currentProcess + desktopIndependentWindow, exact own window only"])
    }
    func stream(_ stream: SCStream, didOutputSampleBuffer buffer: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, CMSampleBufferIsValid(buffer) else { return }; frames += 1
    }
    func stream(_ stream: SCStream, didStopWithError error: Error) { failure = String(describing: error) }
    func recordingOutputDidFinishRecording(_ recordingOutput: SCRecordingOutput) { DispatchQueue.main.async { self.finished?() } }
    func recordingOutput(_ recordingOutput: SCRecordingOutput, didFailWithError error: Error) {
        failure = String(describing: error); DispatchQueue.main.async { self.finished?() }
    }
}

#if !HUD_CURSOR_DIAGNOSTICS
@main
#endif
enum HUDCursorDiagnostics {
    static func main() {
        precondition(CommandLine.arguments.contains("--ui-test"), "Isolated stores are required")
        let app = NSApplication.shared; app.setActivationPolicy(.accessory)
        let session = Session(); app.delegate = session
        withExtendedLifetime(session) { app.run() }
    }
    private final class Session: NSObject, NSApplicationDelegate {
        let overlay = OverlayController()
        var configuration = AppConfiguration.defaults
        var originalFrame = CGRect.zero
        let initialPointer = NSEvent.mouseLocation
        let output: URL = {
            let arguments = CommandLine.arguments
            let index = arguments.firstIndex(of: "--output")!
            return URL(fileURLWithPath: arguments[index + 1], isDirectory: true)
        }()
        var capture: AnyObject?
        var pointerTimer: Timer?
        var stopping = false
        var didFinish = false
        let duration: Double = {
            let arguments = CommandLine.arguments
            guard let index = arguments.firstIndex(of: "--duration"), index + 1 < arguments.count,
                  let value = Double(arguments[index + 1]), value.isFinite else { return 15 }
            return min(90, max(15, value))
        }()
        func applicationDidFinishLaunching(_ notification: Notification) {
            try! FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
            let manual = CommandLine.arguments.contains("--manual")
            if !manual { CursorTrace.install() }
            configuration.closeOnFocusLost = false
            configuration.reduceMotion = false; configuration.lowPowerVisualMode = false
            configuration.ambientAnimation = true
            overlay.systemBackdropPreparationForVerification = { ready in ready() }
            overlay.initialModuleRequest = .map
            _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: configuration)
            guard let source = overlay.systemSourceWatchForVerification, let window = source.window else {
                CursorTrace.record("failure", ["message": overlay.systemSourceFailureForVerification ?? "Missing source/window"])
                finish(); return
            }
            CursorTrace.source = source; CursorTrace.window = window; originalFrame = window.frame
            snapshot("opening")
            if manual {
                overlay.onSystemClosed = { [weak self] in self?.finish() }
                later(duration) { self.stop() }
                return
            }
            later(2) { self.snapshot("map-stable"); self.startCapture() }
            later(4) { self.overlay.selectSystemModule(.eventLog, animated: false); self.snapshot("event-log") }
            later(5) {
                source.superview?.needsLayout = true; source.superview?.layoutSubtreeIfNeeded()
                self.snapshot("explicit-full-layout")
            }
            if CommandLine.arguments.contains("--rendered-motion") {
                later(6) { self.startLocalPointerMotion(window: window) }
            } else {
                later(6) { window.setFrameOrigin(self.originalFrame.origin + CGPoint(x: 8, y: 8)); self.snapshot("own-window-moved-8pt") }
                later(7) { window.setFrame(self.originalFrame, display: true); self.snapshot("own-window-restored") }
            }
            later(8) { self.overlay.selectSystemModule(.power, animated: false); self.snapshot("power") }
            later(9) { self.overlay.update(snapshot: .unavailable, configuration: self.configuration); self.snapshot("unchanged-configuration-update") }
            later(10) { self.overlay.selectSystemModule(.notes, animated: false); self.snapshot("notes") }
            later(11) { self.overlay.selectSystemModule(.profile, animated: false); self.snapshot("profile") }
            later(12) { self.overlay.selectSystemModule(.map, animated: false); self.snapshot("map-return") }
            later(duration) { self.stop() }
            DispatchQueue.main.asyncAfter(deadline: .now() + duration + 4) { self.finish() }
        }
        func snapshot(_ name: String) {
            CursorTrace.stage = name
            guard let source = overlay.systemSourceWatchForVerification, let window = source.window,
                  let host = source.superview else { return }
            if source.sourceCursorOwnedForVerification { CursorTrace.knownHUDCursor = NSCursor.current }
            let pointer = NSEvent.mouseLocation, local = host.convert(window.convertPoint(fromScreen: pointer), from: nil)
            var ancestry: [String] = [], hit = host.hitTest(host.convert(local, to: host.superview))
            while let view = hit { ancestry.append(CursorTrace.object(view)); hit = view.superview }
            var regions: [[String: Any]] = []
            func scan(_ view: NSView) {
                for area in view.trackingAreas {
                    regions.append(["view": CursorTrace.object(view), "owner": CursorTrace.object(area.owner),
                        "area": CursorTrace.object(area), "options": area.options.rawValue,
                        "frame": NSStringFromRect(view.frame), "visibleRect": NSStringFromRect(view.visibleRect),
                        "hidden": view.isHiddenOrHasHiddenAncestor, "alpha": view.alphaValue])
                }
                view.subviews.forEach(scan)
            }
            scan(host)
            CursorTrace.record("stage", ["name": name, "frontmostPID": NSWorkspace.shared.frontmostApplication?.processIdentifier ?? -1,
                "pointer": [pointer.x, pointer.y], "pointerUnchanged": pointer == initialPointer,
                "windowFrame": NSStringFromRect(window.frame), "windowLevel": window.level.rawValue,
                "windowOpaque": window.isOpaque, "windowIgnoresMouse": window.ignoresMouseEvents,
                "sourcePhase": String(describing: source.playback.phase), "sourceHidden": source.isHiddenOrHasHiddenAncestor,
                "cursorSets": source.sourceCursorSetCountForVerification, "hitAncestry": ancestry, "trackingRegions": regions])
            print("Cursor diagnostic: \(name), key=\(window.isKeyWindow), pointerUnchanged=\(pointer == initialPointer)"); fflush(stdout)
        }
        func startCapture() {
            guard CommandLine.arguments.contains("--record"), let window = CursorTrace.window else { return }
            if #available(macOS 15.2, *) {
                let value = CursorWindowCapture(); capture = value
                value.finished = { self.finish() }
                Task { @MainActor in
                    do { try await value.start(window: window, output: self.output) }
                    catch { CursorTrace.record("capture.failure", ["error": String(describing: error)]) }
                }
            }
        }
        func startLocalPointerMotion(window: NSWindow) {
            // This tests the drawn cursor's movement through the production
            // local event monitor. It does not imitate hardware motion or
            // establish genuine cursor arbitration at these synthetic points.
            snapshot("app-local-synthetic-pointer-motion")
            let anchor = window.convertPoint(fromScreen: NSEvent.mouseLocation)
            var sample = 0
            let timer = Timer(timeInterval: 1.0 / 60, repeats: true) { [weak self, weak window] timer in
                guard let self, let window, !self.stopping else { timer.invalidate(); return }
                let angle = Double(sample) / 120 * .pi * 2
                let point = sample < 120 ? CGPoint(x: anchor.x + cos(angle) * 36, y: anchor.y + sin(angle) * 36) : anchor
                if let event = NSEvent.mouseEvent(with: .mouseMoved, location: point, modifierFlags: [],
                    timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                    context: nil, eventNumber: 10000 + sample, clickCount: 0, pressure: 0) {
                    CursorTrace.record("synthetic.appLocalMouseMoved", ["location": [point.x, point.y], "sample": sample])
                    NSApp.postEvent(event, atStart: false)
                }
                sample += 1
                if sample > 120 { timer.invalidate(); self.pointerTimer = nil }
            }
            pointerTimer = timer; RunLoop.main.add(timer, forMode: .common)
        }
        func stop() {
            guard !stopping else { return }; stopping = true; snapshot("finished-before-close")
            if #available(macOS 15.2, *), let capture = capture as? CursorWindowCapture, let stream = capture.stream {
                Task { @MainActor in try? await stream.stopCapture(); DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self.finish() } }
            } else { finish() }
        }
        func finish() {
            guard !didFinish else { return }; didFinish = true
            pointerTimer?.invalidate(); pointerTimer = nil
            overlay.forceCloseSystemOverlay(); CursorTrace.record("closed"); CursorTrace.active = false
            let trace = CursorTrace.rows.compactMap { try? JSONSerialization.data(withJSONObject: $0, options: [.sortedKeys]) }
                .map { String(decoding: $0, as: UTF8.self) }.joined(separator: "\n") + "\n"
            try! trace.write(to: output.appendingPathComponent("trace.jsonl"), atomically: true, encoding: .utf8)
            var report: [String: Any] = ["processID": getpid(), "counts": CursorTrace.counts,
                "traceRows": CursorTrace.rows.count, "traceLimitReached": CursorTrace.rows.count == 16000,
                "requestedDuration": duration, "initialPointer": [initialPointer.x, initialPointer.y],
                "finalPointer": [NSEvent.mouseLocation.x, NSEvent.mouseLocation.y],
                "scope": "Full production OverlayController with --ui-test isolated stores and backdrop override; no input filtering, global input, cursor warping, or manual cursorUpdate",
                "appLocalSyntheticMovement": CommandLine.arguments.contains("--rendered-motion"),
                "limits": "Own-window ScreenCaptureKit API capture, not Shift-Command-5 UI workflow; instrumentation affects timing; current cursor identity does not prove displayed cursor pixels"]
            if #available(macOS 15.2, *), let capture = capture as? CursorWindowCapture {
                capture.queue.sync { report["captureFrames"] = capture.frames; report["captureFailure"] = capture.failure ?? "" }
            }
            try! JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
                .write(to: output.appendingPathComponent("report.json"))
            print("Cursor diagnostic finished: \(output.path)"); fflush(stdout)
            NSApp.terminate(nil)
        }
        func later(_ seconds: Double, _ body: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + seconds) { if !self.stopping && !self.didFinish { body() } }
        }
    }
}

private extension CGPoint {
    static func + (lhs: CGPoint, rhs: CGPoint) -> CGPoint { CGPoint(x: lhs.x + rhs.x, y: lhs.y + rhs.y) }
}
