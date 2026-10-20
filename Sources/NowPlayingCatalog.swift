import Foundation

/// Conservative matching shared by public catalog and lyric search results.
/// Duration and artist checks prevent a cover/remix with a similar title from
/// silently replacing the current player's genuine recording.
enum NowPlayingTrackMatcher {
    static func normalized(_ value: String) -> String {
        let value = value.folding(options: [.caseInsensitive, .diacriticInsensitive, .widthInsensitive], locale: Locale(identifier: "en_US_POSIX"))
        return value.unicodeScalars.filter { CharacterSet.alphanumerics.contains($0) }.map(String.init).joined()
    }
    static func artists(_ value: String) -> Set<String> {
        Set(value.components(separatedBy: CharacterSet(charactersIn: "/,、;&")).map(normalized).filter { !$0.isEmpty })
    }
    static func score(title: String, aliases: [String] = [], artists: [String], album: String,
                      duration: Double?, target: NowPlayingTrack) -> Int? {
        let titleKey = normalized(target.title)
        guard !titleKey.isEmpty, ([title] + aliases).contains(where: { normalized($0) == titleKey }) else { return nil }
        let sourceArtists = Set(artists.flatMap { Self.artists($0) })
        let targetArtists = Self.artists(target.artist)
        guard !sourceArtists.isEmpty, !targetArtists.isEmpty, !sourceArtists.isDisjoint(with: targetArtists) else { return nil }
        var result = 150 + (sourceArtists == targetArtists ? 15 : 0)
        if let expected = target.duration, let actual = duration {
            guard actual.isFinite, actual > 0, abs(expected - actual) <= max(3, min(8, expected * 0.01)) else { return nil }
            result += abs(expected - actual) <= 1 ? 35 : 20
        }
        if !target.album.isEmpty, normalized(album) == normalized(target.album) { result += 20 }
        return result
    }
}

struct NowPlayingCatalogMatch: Equatable {
    let identifier: Int64
    let artwork: URL?
    let score: Int
}

/// Shared, bounded lookup for the current NetEase recording. One search provides
/// both its cover and song ID; only that matching song's timed lyrics are read.
/// No account, cookie, playback API, private player database, or library scan.
/// A lookup that got no answer (no body, a refusal such as code -460, a damaged
/// answer) finishes without lyrics, so LRCLIB may answer meanwhile, and with a
/// clock is retried once `retryTime` has passed; it is never kept as "no lyrics".
final class NowPlayingCatalog {
    typealias Fetch = NowPlayingLyricsLoader.Fetch
    private struct Entry {
        let key: NowPlayingArtworkKey
        var artwork: URL?
        var lyrics: NowPlayingLyrics?
        var finished: Bool
        var retryAt: TimeInterval?
    }
    private var entries: [Entry] = []
    private var desired: NowPlayingArtworkKey?
    private var cancel: (() -> Void)?
    private var generation = 0
    private var backoff = NowPlayingLyricsBackoff()
    private let fetch: Fetch
    var onChange: (() -> Void)?
    /// Monotonic clock for retrying failures. Nil (offline fixtures only) keeps
    /// a failure as the cached negative until `invalidateMissing`.
    var clock: NowPlayingLyricsLoader.Clock? = { ProcessInfo.processInfo.systemUptime }
    var artwork: URL? { current?.artwork }
    var lyrics: NowPlayingLyrics? { current?.lyrics }
    var isLoading: Bool { desired != nil && current?.finished != true }
    /// When the wanted track's failed lookup is retried (nil: no failure).
    var retryTime: TimeInterval? { clock != nil && current?.finished == true ? current?.retryAt : nil }
    private var current: Entry? { entries.first { $0.key == desired } }
    init(fetch: @escaping Fetch = NowPlayingLyricsDownload.fetch) { self.fetch = fetch }
    deinit { cancel?() }

    func request(application: NowPlayingApplication, track: NowPlayingTrack) {
        precondition(Thread.isMainThread)
        guard application.source == .netease else { clear(); return }
        let key = NowPlayingArtworkKey(application: application, track: track, includeArtworkRevision: false)
        if desired == key {
            // The wanted track's failed lookup starts over once its wait passed.
            guard let current, current.finished, isDue(current) else { return }
        }
        generation &+= 1; let expected = generation
        cancel?(); cancel = nil; desired = key
        var known: URL?
        if let index = entries.firstIndex(where: { $0.key == key && $0.finished }) {
            guard isDue(entries[index]) else { let cached = entries.remove(at: index); entries.append(cached); onChange?(); return }
            known = entries[index].artwork // A retry keeps the cover already known while it runs.
        }
        update(key: key, artwork: known, lyrics: nil, finished: false)
        guard let url = Self.searchURL(track) else { update(key: key, artwork: nil, lyrics: nil, finished: true); return }
        request(url, expected: expected) { [weak self] data in
            guard let self else { return }
            let answer = data.map { Self.searchAnswer($0, track: track) }
            guard let match = answer?.match else {
                // No confident match in an answer is final; no answer is not.
                if answer?.answered == true { self.update(key: key, artwork: nil, lyrics: nil, finished: true) }
                else { self.fail(key: key, artwork: nil) }
                return
            }
            self.update(key: key, artwork: match.artwork, lyrics: nil, finished: false)
            self.request(Self.lyricsURL(identifier: match.identifier), expected: expected) { [weak self] data in
                // An answer without lyrics ("nolyric") is final; a refusal is not.
                guard let answer = data.map(Self.lyricsAnswer), answer.answered else { self?.fail(key: key, artwork: match.artwork); return }
                self?.update(key: key, artwork: match.artwork, lyrics: answer.lyrics, finished: true)
            }
        }
    }
    func clear() {
        guard desired != nil || cancel != nil else { return }
        generation &+= 1; cancel?(); cancel = nil; desired = nil
        // Incomplete work can resume on a subsequent visible request. Completed
        // positive and negative entries remain bounded in memory.
        entries.removeAll { !$0.finished }
    }
    /// Drops failed lookups and their backoff (Now Playing hidden), so the next
    /// opening asks again at once. Definitive answers stay cached.
    func forgetFailures() {
        backoff.removeAll()
        let wanted = current?.retryAt != nil
        entries.removeAll { $0.retryAt != nil }
        if wanted { clear() }
    }
    func invalidateMissing(application: NowPlayingApplication, track: NowPlayingTrack) {
        let key = NowPlayingArtworkKey(application: application, track: track, includeArtworkRevision: false)
        if entries.contains(where: { $0.key == key && ($0.artwork == nil || $0.lyrics == nil) }) {
            entries.removeAll { $0.key == key }; if desired == key { clear() }
        }
    }
    private func request(_ url: URL, expected: Int, completion: @escaping (Data?) -> Void) {
        var completed = false
        let cancellation = fetch(url) { [weak self] data in
            let apply = { [weak self] in
                completed = true
                guard let self, self.generation == expected else { return }
                self.cancel = nil; completion(data)
            }
            if Thread.isMainThread { apply() } else { DispatchQueue.main.async(execute: apply) }
        }
        if !completed, generation == expected { cancel = cancellation }
    }
    private func isDue(_ entry: Entry) -> Bool {
        guard let retryAt = entry.retryAt, let clock else { return false }
        return clock() >= retryAt
    }
    /// Finished without lyrics; what the lookup already knew (the cover) stays.
    private func fail(key: NowPlayingArtworkKey, artwork: URL?) {
        guard desired == key else { return }
        let artwork = artwork ?? current?.artwork
        update(key: key, artwork: artwork, lyrics: nil, finished: true, retryAt: clock.map { $0() + backoff.next(key) })
    }
    private func update(key: NowPlayingArtworkKey, artwork: URL?, lyrics: NowPlayingLyrics?, finished: Bool, retryAt: TimeInterval? = nil) {
        guard desired == key else { return }
        if finished, retryAt == nil { backoff.forget(key) }
        entries.removeAll { $0.key == key }
        entries.append(Entry(key: key, artwork: artwork, lyrics: lyrics, finished: finished, retryAt: retryAt))
        if entries.count > 8 { entries.removeFirst(entries.count - 8) }
        onChange?()
    }
    static func searchURL(_ track: NowPlayingTrack) -> URL? {
        guard !track.title.isEmpty, !track.artist.isEmpty else { return nil }
        let artist = track.artist.components(separatedBy: CharacterSet(charactersIn: "/,、;&")).first ?? track.artist
        var parts = URLComponents(string: "https://music.163.com/api/cloudsearch/pc")!
        parts.queryItems = [.init(name: "s", value: track.title + " " + artist), .init(name: "type", value: "1"),
                           .init(name: "limit", value: "10"), .init(name: "offset", value: "0")]
        return parts.url
    }
    static func lyricsURL(identifier: Int64) -> URL {
        var parts = URLComponents(string: "https://music.163.com/api/song/lyric")!
        parts.queryItems = [.init(name: "id", value: String(identifier)), .init(name: "lv", value: "-1"),
                           .init(name: "kv", value: "-1"), .init(name: "tv", value: "-1")]
        return parts.url!
    }
    static func match(data: Data, track: NowPlayingTrack) -> NowPlayingCatalogMatch? { searchAnswer(data, track: track).match }
    /// The JSON object of an answer (code 200). Anything else (a refusal such as
    /// code -460 or 405, a damaged body) is a transient failure, not "nothing".
    private static func answered(_ data: Data) -> [String: Any]? {
        guard data.count <= NowPlayingLyricsDownload.maximumBytes,
              let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              (root["code"] as? NSNumber)?.intValue == 200 else { return nil }
        return root
    }
    /// One parse of a search body: whether the catalog answered, and its match.
    static func searchAnswer(_ data: Data, track: NowPlayingTrack) -> (answered: Bool, match: NowPlayingCatalogMatch?) {
        guard let root = answered(data) else { return (false, nil) }
        guard let songs = (root["result"] as? [String: Any])?["songs"] as? [[String: Any]] else { return (true, nil) }
        let ranked = songs.prefix(20).compactMap { song -> NowPlayingCatalogMatch? in
            guard let id = (song["id"] as? NSNumber)?.int64Value, id > 0,
                  let title = song["name"] as? String else { return nil }
            let artists = (song["ar"] as? [[String: Any]] ?? song["artists"] as? [[String: Any]] ?? []).prefix(32).compactMap { $0["name"] as? String }
            let album = song["al"] as? [String: Any] ?? song["album"] as? [String: Any] ?? [:]
            let duration = (song["dt"] as? NSNumber ?? song["duration"] as? NSNumber).map { $0.doubleValue / 1000 }
            let aliases = song["alia"] as? [String] ?? song["alias"] as? [String] ?? []
            guard let score = NowPlayingTrackMatcher.score(title: title, aliases: Array(aliases.prefix(8)), artists: artists,
                album: album["name"] as? String ?? "", duration: duration, target: track) else { return nil }
            let cover = (album["picUrl"] as? String).flatMap { Self.coverURL($0) }
            return NowPlayingCatalogMatch(identifier: id, artwork: cover, score: score)
        }.sorted { $0.score > $1.score }
        guard let first = ranked.first else { return (true, nil) }
        // If equally strong candidates differ, do not guess a recording.
        if ranked.count > 1, ranked[1].score == first.score, ranked[1].identifier != first.identifier { return (true, nil) }
        return (true, first)
    }
    /// NetEase's image CDN scales to any requested square. Covers are asked for
    /// at 800 px, more than the default 720 px slot, so they are only scaled down.
    static let defaultCoverPixels = 800
    /// The download for a drawn slot of `pixels`: 800 px, or the slot's own size
    /// when it is larger (bounded like the decode).
    static func coverPixels(forDrawn pixels: Int) -> Int {
        max(defaultCoverPixels, min(NowPlayingArtworkLoader.maximumPixelSize, pixels))
    }
    static func coverURL(_ raw: String, pixels: Int = defaultCoverPixels) -> URL? {
        guard raw.utf8.count <= 2048, var parts = URLComponents(string: raw),
              parts.scheme == "http" || parts.scheme == "https", parts.user == nil, parts.password == nil,
              parts.port == nil, parts.fragment == nil, let host = parts.host?.lowercased(),
              ["p1.music.126.net", "p2.music.126.net", "p3.music.126.net", "p4.music.126.net"].contains(host) else { return nil }
        let size = min(NowPlayingArtworkLoader.maximumPixelSize, max(NowPlayingArtworkLoader.minimumPixelSize, pixels))
        parts.scheme = "https"; parts.queryItems = [.init(name: "param", value: "\(size)y\(size)")]
        return parts.url
    }
    static func decodeLyrics(_ data: Data) -> NowPlayingLyrics? { lyricsAnswer(data).lyrics }
    /// One parse of a lyric body: whether the catalog answered, and its lyrics.
    /// The original language is authoritative. Keep one compact current line;
    /// translated variants do not multiply rows or invent synchronization.
    static func lyricsAnswer(_ data: Data) -> (answered: Bool, lyrics: NowPlayingLyrics?) {
        guard let root = answered(data) else { return (false, nil) }
        guard let primary = (root["lrc"] as? [String: Any])?["lyric"] as? String else { return (true, nil) }
        return (true, NowPlayingLyrics(lrc: NowPlayingLyrics.normalizingNetEaseStamps(primary)))
    }
}
