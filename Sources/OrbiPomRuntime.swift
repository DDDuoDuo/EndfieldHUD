import Foundation
import JavaScriptCore

/// The four original skills. Costs/charge, target selection and casting timing
/// live in the supplied original controller, never duplicated in the HUD.
enum OrbiPomSkill: String, CaseIterable, Codable {
    case clear, wind, shake, swap
    var energyCost: Int { switch self { case .clear: return 1; case .wind: return 2; case .shake: return 3; case .swap: return 0 } }
}

struct OrbiPomBody: Equatable {
    let id: Int
    let level: Int
    let x, y, angle, size, opacity, scale: Double
}

struct OrbiPomSnapshot: Equatable {
    var state = "idle"
    var paused = false
    var score = 0, highScore = 0, energy = 0, swapCharge = 0, mergeCount = 0, skillUseCount = 0
    var energyProgress: Double = 0
    var currentLevel = 1, nextLevel = 1
    var previewX: Double = 115, previewY: Double = -10, previewSize: Double = 19.2
    var previewVisible = false
    var previewScale: Double = 1
    var dangerSeconds: Double?
    var skill: OrbiPomSkill?
    var skillPhase: String?
    var selectedBodyIDs: [Int] = []
    var hoverBodyID: Int?
    var windSurfaceY: Double?
    var bodies: [OrbiPomBody] = []
    var simulationTime: Double = 0
    var isPlaying: Bool { state == "playing" }
    var canAdvance: Bool { isPlaying && !paused }
    func canUse(_ value: OrbiPomSkill) -> Bool {
        isPlaying && !paused && skillPhase != "casting" && (value == .swap ? swapCharge >= 6 : energy >= value.energyCost)
    }
}

/// One lazy, offline JavaScriptCore VM per game session. It contains the exact
/// original Matter0.20.0 solver and extracted controller/store declarations.
/// There is no timer, dispatch source, network bridge, sound, or WebView here.
/// Only the visible HUD supplies elapsed frame time. Call on the owning thread.
final class OrbiPomRuntime {
    enum Failure: LocalizedError {
        case missingResource(String), invalidRuntime(String)
        var errorDescription: String? { switch self {
        case .missingResource(let name): return "Missing minigame resource: \(name)"
        case .invalidRuntime(let message): return "Minigame runtime: \(message)"
        } }
    }
    private let context: JSContext
    private let bridge: JSValue
    private(set) var snapshot = OrbiPomSnapshot()
    private(set) var error: String?
    private(set) var advanceCount = 0

    init() throws {
        guard let vm = JSContext() else { throw Failure.invalidRuntime("JavaScriptCore is unavailable") }
        context = vm
        for name in ["matter-0.20.0.js", "orbipom.js"] {
            guard let url = HUDResources.url(for: "OrbiPom/" + name) else { throw Failure.missingResource(name) }
            vm.evaluateScript(try String(contentsOf: url, encoding: .utf8), withSourceURL: url)
            if let exception = vm.exception { throw Failure.invalidRuntime(exception.toString()) }
        }
        guard let api = vm.objectForKeyedSubscript("OrbiPom"), !api.isUndefined else { throw Failure.invalidRuntime("Missing offline bridge") }
        bridge = api
    }

    @discardableResult func start(seed: UInt32? = nil) -> OrbiPomSnapshot {
        error = nil; advanceCount = 0
        let result = call("start", [seed.map { NSNumber(value: $0) } ?? NSNull()])
        updateSnapshot(result)
        return snapshot
    }
    @discardableResult func advance(seconds: Double) -> OrbiPomSnapshot {
        guard snapshot.canAdvance, seconds.isFinite, seconds > 0, error == nil else { return snapshot }
        advanceCount += 1
        updateSnapshot(call("advance", [seconds]))
        return snapshot
    }
    func movePointer(x: Double, y: Double) {
        guard x.isFinite, y.isFinite, snapshot.canAdvance else { return }
        // Pointer frequency may exceed the display rate. Transfer only changed
        // aiming/target fields; body arrays stay on the simulation cadence.
        guard let d = call("move", [x, y])?.toDictionary() as? [String: Any] else { return }
        snapshot.previewX = (d["previewX"] as? NSNumber)?.doubleValue ?? snapshot.previewX
        snapshot.previewY = (d["previewY"] as? NSNumber)?.doubleValue ?? snapshot.previewY
        snapshot.previewSize = (d["previewSize"] as? NSNumber)?.doubleValue ?? snapshot.previewSize
        snapshot.previewScale = (d["previewScale"] as? NSNumber)?.doubleValue ?? snapshot.previewScale
        snapshot.previewVisible = d["previewVisible"] as? Bool ?? snapshot.previewVisible
        snapshot.hoverBodyID = (d["hoverBodyID"] as? NSNumber)?.intValue
    }
    func pointerUp(x: Double, y: Double) {
        guard x.isFinite, y.isFinite, snapshot.canAdvance else { return }
        _ = call("pointerUp", [x, y]); refreshSnapshot()
    }
    @discardableResult func drop() -> Bool {
        let accepted = call("drop", [])?.toBool() ?? false
        refreshSnapshot(); return accepted
    }
    @discardableResult func activate(_ skill: OrbiPomSkill) -> Bool {
        let accepted = call("activate", [skill.rawValue])?.toBool() ?? false
        refreshSnapshot(); return accepted
    }
    func cancelSkill() { _ = call("cancelSkill", []); refreshSnapshot() }
    func setPaused(_ paused: Bool) { _ = call("pause", [paused]); refreshSnapshot() }
    func setHighScore(_ score: Int) { _ = call("highScore", [score]); refreshSnapshot() }

    private func call(_ method: String, _ arguments: [Any]) -> JSValue? {
        context.exception = nil
        let result = bridge.invokeMethod(method, withArguments: arguments)
        if let exception = context.exception { error = exception.toString() }
        return result
    }
    private func refreshSnapshot() { updateSnapshot(call("snapshot", [])) }
    private func updateSnapshot(_ value: JSValue?) {
        guard let d = value?.toDictionary() as? [String: Any] else { return }
        func number(_ key: String, fallback: Double = 0) -> Double { (d[key] as? NSNumber)?.doubleValue ?? fallback }
        var s = OrbiPomSnapshot()
        s.state = d["state"] as? String ?? "idle"; s.paused = d["paused"] as? Bool ?? false
        s.score = Int(number("score")); s.highScore = Int(number("highScore")); s.energy = Int(number("energy")); s.swapCharge = Int(number("swapCharge"))
        s.mergeCount = Int(number("mergeCount")); s.skillUseCount = Int(number("skillUseCount")); s.energyProgress = number("energyProgress")
        s.currentLevel = Int(number("currentLevel", fallback: 1)); s.nextLevel = Int(number("nextLevel", fallback: 1))
        s.previewX = number("previewX"); s.previewY = number("previewY"); s.previewSize = number("previewSize"); s.previewScale = number("previewScale", fallback: 1)
        s.previewVisible = d["previewVisible"] as? Bool ?? false
        s.dangerSeconds = (d["dangerSeconds"] as? NSNumber)?.doubleValue
        s.skill = (d["skill"] as? String).flatMap(OrbiPomSkill.init(rawValue:)); s.skillPhase = d["skillPhase"] as? String
        s.selectedBodyIDs = (d["selectedBodyIDs"] as? [NSNumber] ?? []).map(\.intValue); s.hoverBodyID = (d["hoverBodyID"] as? NSNumber)?.intValue
        s.windSurfaceY = (d["windSurfaceY"] as? NSNumber)?.doubleValue; s.simulationTime = number("simulationTime")
        s.bodies = (d["bodies"] as? [[String: Any]] ?? []).compactMap { b in
            guard let id = b["id"] as? NSNumber, let level = b["level"] as? NSNumber else { return nil }
            func n(_ key: String, _ fallback: Double = 0) -> Double { (b[key] as? NSNumber)?.doubleValue ?? fallback }
            return OrbiPomBody(id: id.intValue, level: level.intValue, x: n("x"), y: n("y"), angle: n("angle"), size: n("size"), opacity: n("opacity", 1), scale: n("scale", 1))
        }
        snapshot = s
    }

    #if !HUD_RELEASE
    /// Fixture-only access to original reducers/physics for isolated parity tests.
    /// No app-data, preference, file, network or process bridge is installed.
    @discardableResult func evaluateForTesting(_ script: String) -> JSValue? {
        context.exception = nil
        let value = context.evaluateScript(script)
        if let exception = context.exception { error = exception.toString() }
        refreshSnapshot()
        return value
    }
    #endif
    deinit { _ = bridge.invokeMethod("destroy", withArguments: []) }
}
