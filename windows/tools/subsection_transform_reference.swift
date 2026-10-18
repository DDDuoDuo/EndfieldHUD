import AppKit
import QuartzCore

// Compiles unchanged HUDSubsectionTransition.swift. Only animation copies are
// assigned a fixed beginTime/fillMode for deterministic paused presentation.
// HUDSubsectionHandoff is never instantiated (it owns delayed callbacks).
private struct Probe {
    let viewport: CGRect
    let direction: CGFloat
    let phase: Double
    let content: CALayer
    let mask: CAShapeLayer
    let transition: HUDSubsectionTransition
}
private func matrix(_ m: CATransform3D) -> [Double] {
    [m.m11,m.m12,m.m13,m.m14,m.m21,m.m22,m.m23,m.m24,
     m.m31,m.m32,m.m33,m.m34,m.m41,m.m42,m.m43,m.m44].map(Double.init)
}
private func commands(_ path: CGPath) -> [[Double]] {
    var result: [[Double]] = []
    path.applyWithBlock { p in
        let count: Int
        switch p.pointee.type {
        case .moveToPoint,.addLineToPoint: count = 1
        case .addQuadCurveToPoint: count = 2
        case .addCurveToPoint: count = 3
        case .closeSubpath: count = 0
        @unknown default: count = 0
        }
        result.append([Double(p.pointee.type.rawValue)] + (0..<count).flatMap { [Double(p.pointee.points[$0].x),Double(p.pointee.points[$0].y)] })
    }
    return result
}
private func points(_ path: CGPath) throws -> [[Double]] {
    var result: [[Double]] = []
    var moves = 0, closes = 0, unsupported = false
    path.applyWithBlock { p in
        switch p.pointee.type {
        case .moveToPoint: moves += 1; fallthrough
        case .addLineToPoint: result.append([Double(p.pointee.points[0].x),Double(p.pointee.points[0].y)])
        case .closeSubpath: closes += 1
        default: unsupported = true
        }
    }
    guard !unsupported && moves == 4 && closes == 4 && result.count == 24 else {
        throw NSError(domain: "subsection-reference",code: 10,userInfo: [NSLocalizedDescriptionKey: "Authored keyframe topology changed"])
    }
    return result
}
private func sample(_ probe: Probe) throws -> ([Double],[[Double]]) {
    guard let content = probe.content.presentation(),
          let mask = probe.mask.presentation(), let path = mask.path else {
        throw NSError(domain: "subsection-reference", code: 11, userInfo: [NSLocalizedDescriptionKey: "No genuine paused presentation content/mask"])
    }
    return (matrix(content.sublayerTransform),commands(path))
}
private func waitForCommit() {
    CATransaction.flush()
    RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.08))
}
@main private struct SubsectionTransformReference {
    static func main() throws {
        guard CommandLine.arguments.count == 2 else { throw NSError(domain: "subsection-reference", code: 1) }
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        guard !FileManager.default.fileExists(atPath: output.path) else { throw NSError(domain: "subsection-reference", code: 2) }
        let app = NSApplication.shared
        app.setActivationPolicy(.prohibited)
        let minimumX = NSScreen.screens.map { $0.frame.minX }.min() ?? 0
        let panel = NSPanel(contentRect: NSRect(x: minimumX - 4096,y: 0,width: 440,height: 440),
                            styleMask: [.borderless,.nonactivatingPanel],backing: .buffered,defer: false)
        panel.isReleasedWhenClosed = false; panel.ignoresMouseEvents = true
        panel.hidesOnDeactivate = false; panel.isOpaque = false; panel.backgroundColor = .clear
        panel.animationBehavior = .none
        let view = NSView(frame: NSRect(x: 0,y: 0,width: 440,height: 440)); view.wantsLayer = true
        panel.contentView = view
        defer { panel.orderOut(nil); panel.close() }
        guard let root = view.layer else { throw NSError(domain: "subsection-reference", code: 3) }
        let phases = (0...100).map { Double($0)/100 } + [0.00001,0.0037,0.1234567,0.2999999,0.3000001,0.54321,0.6799999,0.6800001,0.87654321,0.9999999]
        let viewports = [CGRect(x: 9,y: 40,width: 382,height: 248),CGRect(x: 0,y: 0,width: 400,height: 334),CGRect(x: -7.25,y: 3.75,width: 183.5,height: 92.25)]
        var probes: [Probe] = [], keyframes: [[String: Any]] = []
        CATransaction.begin(); CATransaction.setDisableActions(true)
        for viewport in viewports {
            for direction: CGFloat in [-1,1] {
                for phase in phases {
                    let content = CALayer(); content.frame = CGRect(x: 0,y: 0,width: 440,height: 440)
                    content.backgroundColor = NSColor.white.cgColor; root.addSublayer(content)
                    let transition = HUDSubsectionTransition(content: content,viewport: viewport)
                    transition.reveal(direction: direction,animated: true)
                    guard let mask = content.mask as? CAShapeLayer,
                          let movement = content.animation(forKey: HUDSubsectionTransition.movementKey)?.copy() as? CABasicAnimation,
                          let reveal = mask.animation(forKey: HUDSubsectionTransition.revealKey)?.copy() as? CAKeyframeAnimation else {
                        throw NSError(domain: "subsection-reference", code: 4)
                    }
                    if phase == 0 {
                        guard let paths = reveal.values as? [CGPath], paths.count == 4,
                              let from = movement.fromValue as? NSValue,let to = movement.toValue as? NSValue else {
                            throw NSError(domain: "subsection-reference", code: 5)
                        }
                        keyframes.append(["viewport":[Double(viewport.minX),Double(viewport.minY),Double(viewport.width),Double(viewport.height)],
                                          "direction":Double(direction),"paths":try paths.map(points),
                                          "from":matrix(from.caTransform3DValue),"to":matrix(to.caTransform3DValue),
                                          "keyTimes":reveal.keyTimes ?? [],"maskHasGlobalTiming":reveal.timingFunction != nil])
                    }
                    for animation in [movement,reveal] {
                        animation.beginTime = 1; animation.fillMode = .both; animation.isRemovedOnCompletion = false
                    }
                    content.speed = 0; content.timeOffset = 1 + phase * HUDSubsectionTransition.duration
                    content.add(movement,forKey: HUDSubsectionTransition.movementKey)
                    mask.add(reveal,forKey: HUDSubsectionTransition.revealKey)
                    probes.append(Probe(viewport: viewport,direction: direction,phase: phase,content: content,mask: mask,transition: transition))
                }
            }
        }
        CATransaction.commit(); panel.orderFrontRegardless(); waitForCommit()
        guard !panel.isKeyWindow,!panel.isMainWindow,NSScreen.screens.allSatisfy({ !panel.frame.intersects($0.frame) }) else {
            throw NSError(domain: "subsection-reference", code: 6,userInfo: [NSLocalizedDescriptionKey: "Oracle panel must stay offscreen and nonkey"])
        }
        let first = try probes.map(sample)
        waitForCommit()
        var rows: [[String: Any]] = [],maximumRepeatDifference = 0.0
        for (index,probe) in probes.enumerated() {
            let actual = try sample(probe)
            for (a,b) in zip(first[index].0 + first[index].1.flatMap({ $0 }),actual.0 + actual.1.flatMap({ $0 })) {
                maximumRepeatDifference = max(maximumRepeatDifference,abs(a-b))
            }
            rows.append(["viewport":[Double(probe.viewport.minX),Double(probe.viewport.minY),Double(probe.viewport.width),Double(probe.viewport.height)],
                         "direction":Double(probe.direction),"phase":probe.phase,"presentation":actual.0,"pathCommands":actual.1])
        }
        let result: [String: Any] = ["schemaVersion":1,"kind":"actual-subsection-core-animation-presentation",
            "sourceDuration":HUDSubsectionTransition.duration,"pausedLayerClock":true,"ownOffscreenNonactivatingPanel":true,
            "screenCaptured":false,"handoffInstantiated":false,"maximumRepeatDifference":maximumRepeatDifference,
            "maskGeometryScope":"Raw genuine CA paths; straight polygon interpolation is not assumed",
            "keyframes":keyframes,"rows":rows]
        try JSONSerialization.data(withJSONObject: result,options: [.sortedKeys]).write(to: output,options: .withoutOverwriting)
        for probe in probes { probe.transition.settle(); probe.content.removeFromSuperlayer() }
        print("Sampled \(rows.count) actual subsection transform/mask pairs; paused repeat delta \(maximumRepeatDifference)")
    }
}
