import Foundation

enum NowPlayingLyricsTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) { count += 1; if !condition { fatalError(message) } }
        let value = NowPlayingLyrics(lrc: "[ar:Fixture]\n[00:01.25]First\n[00:05.000]Second\n[00:09.5]Third")!
        check(value.lines.count == 3 && value.hasContent, "Timed lyrics retain sorted bounded lines")
        check(value.window(at: 0) == ["", "", "First"], "Before the first cue, the current row stays empty")
        check(value.window(at: 1.25) == ["", "First", "Second"], "A cue begins at its exact timestamp")
        check(value.window(at: 8) == ["First", "Second", "Third"], "The active line is always the middle of three rows")
        check(value.window(at: 999) == ["Second", "Third", ""], "The last lyric has no invented next line")
        check(value.window(at: .nan) == ["", "", "First"], "Nonfinite playback positions do not crash lyric lookup")
        check(value.nextBoundary(after: 0.9) == 1.25 && value.nextBoundary(after: 1.25) == 5
              && value.nextBoundary(after: 9.5) == nil && value.nextBoundary(after: .infinity) == nil,
              "The next exact subsecond cue advances strictly past the current cue without rounding or replay")
        let multiple = NowPlayingLyrics(lrc: "[offset:-500]\n[00:02][00:04.50]Repeat\n[00:02]重复\n[00:05]\n[00:06]End")!
        check(multiple.lines.first?.time == 1.5 && multiple.lines.first?.text == "Repeat / 重复", "Offsets and simultaneous translations share one timed row")
        check(multiple.window(at: 4.75)[1] == "", "A timed blank cue ends the previous lyric during an instrumental gap")
        check(multiple.nextBoundary(after: 4.2) == 4.5, "Blank instrumental cues also receive their own display deadline")
        check(NowPlayingLyrics(lrc: "Plain unsynchronized lyrics") == nil, "Untimed text is never assigned fabricated timestamps")
        check(NowPlayingLyrics(lrc: "[00:99]Invalid") == nil, "Malformed seconds are rejected")
        check(NowPlayingLyrics(lrc: String(repeating: "x", count: 512 * 1024 + 1)) == nil, "Lyric documents are bounded before parsing")
        check(NowPlayingLyrics(lrc: "[01:02:03.4]Hour")?.lines.first?.time == 3723.4, "Hour-form timestamps parse correctly")
        let descending = NowPlayingLyrics(lrc: "[00:20]Later\n[00:01]Earlier")!
        check(descending.lines.map(\.text) == ["Earlier", "Later"], "Out-of-order cue input is sorted once")
        let app = NowPlayingApplication(source: .netease, pid: 432, bundleURL: URL(fileURLWithPath: "/Fixture.app"))
        let track = NowPlayingTrack(title: "A & B", artist: "Fixture", album: "Album", duration: 100, position: 0, isPlaying: true, sampledAt: 0)
        let url = NowPlayingLyricsLoader.requestURL(for: track)!
        check(URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems?.first(where: { $0.name == "track_name" })?.value == "A & B",
              "Metadata is safely encoded as query values")
        var requests = 0, cancellations = 0
        var complete: ((Data?) -> Void)?
        let loader = NowPlayingLyricsLoader { _, callback in requests += 1; complete = callback; return { cancellations += 1 } }
        loader.request(application: app, track: track)
        for _ in 0..<30 { loader.request(application: app, track: track) }
        check(requests == 1, "Repeated progress refreshes do not refetch lyrics")
        complete?(try! JSONSerialization.data(withJSONObject: ["syncedLyrics": "[00:01]Fixture lyric"]))
        check(loader.lyrics?.window(at: 2)[1] == "Fixture lyric", "A completed timed response becomes visible")
        let withArt = NowPlayingTrack(title: track.title, artist: track.artist, album: track.album, duration: track.duration,
            position: 3, isPlaying: false, sampledAt: 4, artworkRevision: "new cover")
        loader.request(application: app, track: withArt)
        check(requests == 1, "Delayed cover arrival and pause/progress updates do not request lyrics again")
        loader.clear(); loader.request(application: app, track: track)
        check(requests == 1 && loader.lyrics != nil, "Reopening a cached song performs no network request")
        let other = NowPlayingTrack(title: "Other", artist: "Fixture", album: "", duration: nil, position: nil, isPlaying: false, sampledAt: 0)
        loader.request(application: app, track: other); let stale = complete
        loader.clear(); stale?(try! JSONSerialization.data(withJSONObject: ["syncedLyrics": "[00:00]Stale"]))
        check(loader.lyrics == nil && cancellations == 1, "Closing cancels lookup and rejects stale completion")
        var embeddedRequests = 0
        let embedded = NowPlayingLyricsLoader { _, callback in embeddedRequests += 1; callback(nil); return {} }
        embedded.request(application: app, track: NowPlayingTrack(title: "Embedded", artist: "Fixture", album: "", duration: nil,
            position: nil, isPlaying: true, sampledAt: 0, timedLyrics: "[00:00]Embedded cue"))
        check(embeddedRequests == 0 && embedded.lyrics?.hasContent == true, "Player-provided timed lyrics take precedence without network access")
        var delayedRequests = 0, delayedCancellations = 0, delayedChanges = 0
        var delayedReply: ((Data?) -> Void)?
        let delayed = NowPlayingLyricsLoader { _, callback in
            delayedRequests += 1; delayedReply = callback; return { delayedCancellations += 1 }
        }
        delayed.onChange = { delayedChanges += 1 }
        delayed.request(application: app, track: track)
        let oldReply = delayedReply
        func enriched(_ source: String) -> NowPlayingTrack {
            NowPlayingTrack(title: track.title, artist: track.artist, album: track.album, duration: track.duration,
                position: 3, isPlaying: true, sampledAt: 4, timedLyrics: source)
        }
        delayed.request(application: app, track: enriched("[00:00.25]Late native cue"))
        check(delayed.lyrics?.window(at: 0.3)[1] == "Late native cue" && delayedCancellations == 1,
              "Late embedded lyrics immediately replace an in-flight lookup for the same song")
        oldReply?(try! JSONSerialization.data(withJSONObject: ["syncedLyrics": "[00:00]Old network cue"]))
        check(delayed.lyrics?.window(at: 0.3)[1] == "Late native cue",
              "Canceled network lyrics cannot overwrite a newer player-provided timeline")
        let published = delayedChanges
        for _ in 0..<30 { delayed.request(application: app, track: enriched("[00:00.25]Late native cue")) }
        check(delayedChanges == published && delayedRequests == 1,
              "Unchanged embedded content does not reparse, publish or fetch on progress notifications")
        delayed.request(application: app, track: enriched("[00:00.10]Corrected cue"))
        check(delayed.lyrics?.window(at: 0.15)[1] == "Corrected cue" && delayedChanges == published + 1,
              "A corrected same-song embedded timeline replaces the previous timestamps immediately")
        let missing = NowPlayingLyricsLoader { _, callback in callback(nil); return {} }
        missing.request(application: app, track: track)
        check(missing.lyrics == nil, "The independent missing-lyric fixture has a cached miss")
        missing.request(application: app, track: enriched("[00:00.40]After miss"))
        check(missing.lyrics?.window(at: 0.5)[1] == "After miss",
              "A cached missing result cannot suppress late native lyrics")
        delayed.clear(); missing.clear()
        return count
    }
}
