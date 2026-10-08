import AppKit
import QuartzCore

// Compiled together with the unchanged source offset() body extracted by the
// companion script. Own synthetic layers only: no HUD/provider/user-data load.
private struct Probe {
    let direction: CGPoint
    let incoming: Bool
    let phase: Double
    let timing: String
    let layer: CALayer
    let from: CATransform3D
    let to: CATransform3D
}
private func matrix(_ m: CATransform3D) -> [Double] {
    [m.m11,m.m12,m.m13,m.m14,m.m21,m.m22,m.m23,m.m24,
     m.m31,m.m32,m.m33,m.m34,m.m41,m.m42,m.m43,m.m44].map(Double.init)
}
private func waitForCommit() {
    CATransaction.flush()
    RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.08))
}
@main private struct ModuleTransformReference {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else { throw NSError(domain: "module-transform-reference", code: 1, userInfo: [NSLocalizedDescriptionKey: "Pass one new JSON output path"]) }
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        guard !FileManager.default.fileExists(atPath: output.path) else { throw NSError(domain: "module-transform-reference", code: 2, userInfo: [NSLocalizedDescriptionKey: "Output must be new"]) }
        let app = NSApplication.shared
        app.setActivationPolicy(.prohibited)
        let minimumX = NSScreen.screens.map { $0.frame.minX }.min() ?? 0
        let panel = NSPanel(contentRect: NSRect(x: minimumX - 4096, y: 0, width: 440, height: 440),
                            styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: false)
        panel.isReleasedWhenClosed = false
        panel.ignoresMouseEvents = true
        panel.hidesOnDeactivate = false
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.animationBehavior = .none
        let view = NSView(frame: NSRect(x: 0, y: 0, width: 440, height: 440))
        view.wantsLayer = true
        panel.contentView = view
        defer { panel.orderOut(nil); panel.close() }
        guard let root = view.layer else { throw NSError(domain: "module-transform-reference", code: 3) }
        // A dense, deterministic grid also exposes CA's finite precision
        // timing-function inversion; the uneven samples avoid only round times.
        let phases = (0...100).map { Double($0) / 100 } + [0.0037, 0.1234567, 0.54321, 0.87654321]
        var probes: [Probe] = []
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for direction in [CGPoint(x: -1, y: 0), CGPoint(x: 1, y: 0), CGPoint(x: 0, y: -1), CGPoint(x: 0, y: 1)] {
            for incoming in [true, false] {
                let offset = OriginalModuleTransform.offset(direction: incoming ? direction : CGPoint(x: -direction.x, y: -direction.y),
                                                            distance: incoming ? 32 : 25, depth: incoming ? -54 : -55)
                let from = incoming ? offset : CATransform3DIdentity
                let to = incoming ? CATransform3DIdentity : offset
                for timing in ["source", "linear"] {
                    for phase in phases {
                        let layer = CALayer()
                        layer.frame = NSRect(x: 0, y: 0, width: 440, height: 440)
                        layer.backgroundColor = NSColor.white.cgColor
                        layer.transform = to
                        layer.speed = 0
                        layer.timeOffset = 1 + phase * 0.3
                        root.addSublayer(layer)
                        let animation = CABasicAnimation(keyPath: "transform")
                        animation.fromValue = NSValue(caTransform3D: from)
                        animation.toValue = NSValue(caTransform3D: to)
                        animation.beginTime = 1
                        animation.duration = 0.3
                        animation.fillMode = .both
                        animation.isRemovedOnCompletion = false
                        animation.timingFunction = timing == "source" ? CAMediaTimingFunction(controlPoints: 0.18, 0.72, 0.26, 1) : CAMediaTimingFunction(name: .linear)
                        layer.add(animation, forKey: "module.transform")
                        probes.append(Probe(direction: direction, incoming: incoming, phase: phase, timing: timing, layer: layer, from: from, to: to))
                    }
                }
            }
        }
        CATransaction.commit()
        panel.orderFrontRegardless()
        waitForCommit()
        guard !panel.isKeyWindow, !panel.isMainWindow, NSScreen.screens.allSatisfy({ !panel.frame.intersects($0.frame) }) else {
            throw NSError(domain: "module-transform-reference", code: 4, userInfo: [NSLocalizedDescriptionKey: "Oracle panel must remain offscreen and nonkey"])
        }
        var first: [[Double]] = []
        for probe in probes {
            guard let presented = probe.layer.presentation() else { throw NSError(domain: "module-transform-reference", code: 5, userInfo: [NSLocalizedDescriptionKey: "No genuine Core Animation presentation layer available"]) }
            first.append(matrix(presented.transform))
        }
        waitForCommit()
        var rows: [[String: Any]] = []
        var maximumRepeatDifference = 0.0
        for (index, probe) in probes.enumerated() {
            guard let presented = probe.layer.presentation() else { throw NSError(domain: "module-transform-reference", code: 6) }
            let value = matrix(presented.transform)
            maximumRepeatDifference = max(maximumRepeatDifference, zip(first[index], value).map { abs($0 - $1) }.max() ?? 0)
            rows.append(["direction": [Double(probe.direction.x), Double(probe.direction.y)], "incoming": probe.incoming,
                         "phase": probe.phase, "timing": probe.timing, "from": matrix(probe.from), "to": matrix(probe.to), "presentation": value])
        }
        let data: [String: Any] = ["schemaVersion": 1, "kind": "actual-core-animation-presentation",
                                  "sourceDuration": 0.3, "sourceTiming": [0.18,0.72,0.26,1], "pausedLayerClock": true,
                                  "ownOffscreenNonactivatingPanel": true, "screenCaptured": false, "maximumRepeatDifference": maximumRepeatDifference,
                                  "rows": rows]
        try JSONSerialization.data(withJSONObject: data, options: [.sortedKeys]).write(to: output, options: .withoutOverwriting)
        print("Sampled \(rows.count) actual Core Animation transforms; paused repeat delta \(maximumRepeatDifference)")
    }
}
