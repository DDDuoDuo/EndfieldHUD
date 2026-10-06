import Foundation

/// Owns one game across HUD opens. No clock exists here; the visible canvas
/// supplies steps. Only a finished/hidden session commits its local best score.
final class OrbiPomSession {
    private(set) var runtime: OrbiPomRuntime?
    private(set) var error: String?
    private let defaults: UserDefaults?
    private(set) var bestScore: Int
    /// User intent survives canvas recreation; visibility pauses never overwrite it.
    private(set) var manuallyPaused = false
    var onEvent: ((String) -> Void)?
    var snapshot: OrbiPomSnapshot { runtime?.snapshot ?? OrbiPomSnapshot() }
    private var recordedFinish = false
    static let bestScoreKey = "orbipom.bestScore.v1"

    init(defaults: UserDefaults? = nil) {
        self.defaults = defaults
        bestScore = max(0, defaults?.integer(forKey: Self.bestScoreKey) ?? 0)
    }
    @discardableResult func start(seed: UInt32? = nil) -> Bool {
        let restarting = runtime != nil
        do {
            if runtime == nil { runtime = try OrbiPomRuntime() }
            error = nil; recordedFinish = false; manuallyPaused = false
            runtime?.setHighScore(bestScore); runtime?.start(seed: seed)
            error = runtime?.error
            if error == nil { onEvent?(restarting ? "restarted" : "started") }
            return error == nil
        } catch { self.error = error.localizedDescription; runtime = nil; return false }
    }
    func advance(seconds: Double) {
        guard let runtime else { return }
        let wasPlaying = runtime.snapshot.isPlaying
        runtime.advance(seconds: seconds); error = runtime.error
        if wasPlaying && !runtime.snapshot.isPlaying && !recordedFinish {
            recordedFinish = true; saveBest(); onEvent?("finished")
        }
    }
    func setManuallyPaused(_ value: Bool) {
        manuallyPaused = value
        if value { pause(true) }
    }
    func pause(_ paused: Bool) {
        let effectivePause = paused || manuallyPaused
        runtime?.setPaused(effectivePause)
        if effectivePause { saveBest() }
    }
    func saveBest() {
        let next = max(bestScore, snapshot.score, snapshot.highScore)
        guard next != bestScore else { return }; bestScore = next
        defaults?.set(next, forKey: Self.bestScoreKey)
    }
}
