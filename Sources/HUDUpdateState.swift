import Foundation

/// Presentation state is shared by About and the menu bar. Neither view owns a
/// network request or an installer, so closing the HUD cannot cancel an update.
struct HUDUpdateState: Equatable {
    enum Phase: Equatable { case idle, checking, current, available, downloading, ready, installing, failed }
    var phase: Phase = .idle
    var latestVersion: String?
    /// The compatible, signed install candidate can differ from newest GitHub.
    var updateVersion: String?
    var releaseURL: URL?
    var detail: String?
    var automaticallyInstalls = true
    var installationSupported = false

    var hasUpdate: Bool { [.available, .downloading, .ready, .installing].contains(phase) }
    var title: String {
        switch phase {
        case .idle: return L10n.text("Not checked yet", "尚未检查")
        case .checking: return L10n.text("Checking…", "正在检查…")
        case .current: return L10n.text("Up to date", "已是最新版本")
        case .available: return L10n.text("Update available", "发现新版本")
        case .downloading: return L10n.text("Downloading…", "正在下载…")
        case .ready: return L10n.text("Ready to update", "更新已就绪")
        case .installing: return L10n.text("Restarting…", "正在重启…")
        case .failed: return L10n.text("Check failed — retry", "检查失败 · 重试")
        }
    }
}

/// Event-driven quiet period. Only a ready update creates this one-shot timer;
/// normal operation adds no polling loop. Every change in eligibility cancels
/// the pending restart and re-evaluates it from a fresh quiet period.
final class HUDUpdateInstallGate {
    typealias Schedule = (TimeInterval, @escaping () -> Void) -> () -> Void
    private let schedule: Schedule
    private let delay: TimeInterval
    private var cancel: (() -> Void)?
    private var generation = 0
    private var fired = false
    init(delay: TimeInterval = 30, schedule: @escaping Schedule) {
        self.delay = delay; self.schedule = schedule
    }
    func evaluate(eligible: Bool, recheck: @escaping () -> Bool, install: @escaping () -> Void) {
        guard !fired else { return }
        guard eligible else { cancelPending(); return }
        guard cancel == nil else { return }
        generation += 1
        let token = generation
        cancel = schedule(delay) { [weak self] in
            guard let self, self.generation == token, !self.fired else { return }
            self.cancel = nil
            guard recheck() else { return }
            self.fired = true
            install()
        }
    }
    func reset() { cancelPending(); fired = false }
    private func cancelPending() { generation += 1; cancel?(); cancel = nil }
    deinit { cancel?() }
}
