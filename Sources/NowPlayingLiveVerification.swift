import AppKit
import ApplicationServices

/// Explicit read-only end-to-end diagnostic. It uses the same production
/// controller, catalog, bounded cover decoder and lyrics path as the HUD.
/// No playback command, permission prompt, library/account read or user-store
/// mutation. Output contains capability counts/timings, never track metadata.
enum NowPlayingLiveVerification {
    private static var session: Session?
    static func run() {
        precondition(CommandLine.arguments.contains("--ui-test"))
        precondition(CommandLine.arguments.contains("--now-playing-live-probe"))
        precondition(session == nil)
        let value = Session(); session = value; value.start()
    }
    private final class Session {
        let controller = NowPlayingController()
        var observer: UUID?
        var stage = "cold"
        var started: TimeInterval = 0
        var result: [String: Any] = ["accessibilityTrusted": AXIsProcessTrusted()]
        var metadataTime: Double?, freshMetadataTime: Double?, coverTime: Double?, lyricsTime: Double?
        var finishing = false
        var coldTrack: NowPlayingTrack?
        func start() {
            observer = controller.observe { [weak self] in self?.sample() }
            started = ProcessInfo.processInfo.systemUptime
            controller.activate()
            DispatchQueue.main.asyncAfter(deadline: .now() + 14) { [weak self] in
                guard let self, self.stage == "cold" else { return }; self.finishStage()
            }
        }
        func sample() {
            guard !finishing else { return }
            let elapsed = (ProcessInfo.processInfo.systemUptime - started) * 1000
            if controller.snapshot.track != nil, metadataTime == nil { metadataTime = elapsed }
            if controller.hasFreshMetadataForPresentation, freshMetadataTime == nil { freshMetadataTime = elapsed }
            if controller.artworkImage != nil, coverTime == nil { coverTime = elapsed }
            if controller.lyrics?.hasContent == true, lyricsTime == nil { lyricsTime = elapsed }
            if metadataTime != nil, freshMetadataTime != nil, coverTime != nil, lyricsTime != nil {
                finishing = true
                DispatchQueue.main.async { [weak self] in self?.finishStage() }
            }
        }
        func finishStage() {
            guard stage != "done" else { return }
            finishing = true
            result[stage + "MetadataMs"] = metadataTime.map { Int($0.rounded()) } ?? -1
            result[stage + "FreshMetadataMs"] = freshMetadataTime.map { Int($0.rounded()) } ?? -1
            result[stage + "CoverMs"] = coverTime.map { Int($0.rounded()) } ?? -1
            result[stage + "LyricsMs"] = lyricsTime.map { Int($0.rounded()) } ?? -1
            result[stage + "HasTrack"] = controller.snapshot.track != nil
            result[stage + "HasCover"] = controller.artworkImage != nil
            result[stage + "LyricCues"] = controller.lyrics?.lines.count ?? 0
            result["source"] = controller.snapshot.application?.source.rawValue ?? "none"
            if stage == "cold" {
                coldTrack = controller.snapshot.track
                controller.deactivate()
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { [self] in
                    stage = "warm"; metadataTime = nil; freshMetadataTime = nil; coverTime = nil; lyricsTime = nil
                    started = ProcessInfo.processInfo.systemUptime; finishing = false
                    controller.activate(); sample()
                    DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) { [weak self] in
                        guard let self, self.stage == "warm" else { return }; self.finishStage()
                    }
                }
            } else {
                result["warmSameTrack"] = coldTrack.map { controller.snapshot.track?.hasSameIdentity(as: $0) == true } ?? false
                stage = "done"
                if let observer { controller.removeObserver(observer) }
                controller.deactivate()
                if let data = try? JSONSerialization.data(withJSONObject: result, options: [.sortedKeys]),
                   let output = String(data: data, encoding: .utf8) { print("Now Playing live probe: " + output) }
                fflush(stdout); session = nil; NSApp.terminate(nil)
            }
        }
    }
}
