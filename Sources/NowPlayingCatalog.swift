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
final class NowPlayingCatalog {
    typealias Fetch = NowPlayingLyricsLoader.Fetch
    private struct Entry {
        let key: NowPlayingArtworkKey
        var artwork: URL?
        var lyrics: NowPlayingLyrics?
        var finished: Bool
    }
    private var entries: [Entry] = []
    private var desired: NowPlayingArtworkKey?
    private var cancel: (() -> Void)?
    private var generation = 0
    private let fetch: Fetch
    var onChange: (() -> Void)?
    var artwork: URL? { current?.artwork }
    var lyrics: NowPlayingLyrics? { current?.lyrics }
    var isLoading: Bool { desired != nil && current?.finished != true }
    private var current: Entry? { entries.first { $0.key == desired } }
    init(fetch: @escaping Fetch = NowPlayingLyricsDownload.fetch) { self.fetch = fetch }
    deinit { cancel?() }

    func request(application: NowPlayingApplication, track: NowPlayingTrack) {
        precondition(Thread.isMainThread)
        guard application.source == .netease else { clear(); return }
        let key = NowPlayingArtworkKey(application: application, track: track, includeArtworkRevision: false)
        guard desired != key else { return }
        generation &+= 1; let expected = generation
        cancel?(); cancel = nil; desired = key
        if let index = entries.firstIndex(where: { $0.key == key && $0.finished }) {
            let cached = entries.remove(at: index); entries.append(cached); onChange?(); return
        }
        update(key: key, artwork: nil, lyrics: nil, finished: false)
        guard let url = Self.searchURL(track) else { update(key: key, artwork: nil, lyrics: nil, finished: true); return }
        request(url, expected: expected) { [weak self] data in
            guard let self else { return }
            guard let data, let match = Self.match(data: data, track: track) else {
                self.update(key: key, artwork: nil, lyrics: nil, finished: true); return
            }
            self.update(key: key, artwork: match.artwork, lyrics: nil, finished: false)
            self.request(Self.lyricsURL(identifier: match.identifier), expected: expected) { [weak self] data in
                self?.update(key: key, artwork: match.artwork, lyrics: data.flatMap(Self.decodeLyrics), finished: true)
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
    private func update(key: NowPlayingArtworkKey, artwork: URL?, lyrics: NowPlayingLyrics?, finished: Bool) {
        guard desired == key else { return }
        entries.removeAll { $0.key == key }
        entries.append(Entry(key: key, artwork: artwork, lyrics: lyrics, finished: finished))
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
    static func match(data: Data, track: NowPlayingTrack) -> NowPlayingCatalogMatch? {
        guard data.count <= NowPlayingLyricsDownload.maximumBytes,
              let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              (root["code"] as? NSNumber)?.intValue == 200,
              let songs = (root["result"] as? [String: Any])?["songs"] as? [[String: Any]] else { return nil }
        let ranked = songs.prefix(20).compactMap { song -> NowPlayingCatalogMatch? in
            guard let id = (song["id"] as? NSNumber)?.int64Value, id > 0,
                  let title = song["name"] as? String else { return nil }
            let artists = (song["ar"] as? [[String: Any]] ?? song["artists"] as? [[String: Any]] ?? []).prefix(32).compactMap { $0["name"] as? String }
            let album = song["al"] as? [String: Any] ?? song["album"] as? [String: Any] ?? [:]
            let duration = (song["dt"] as? NSNumber ?? song["duration"] as? NSNumber).map { $0.doubleValue / 1000 }
            let aliases = song["alia"] as? [String] ?? song["alias"] as? [String] ?? []
            guard let score = NowPlayingTrackMatcher.score(title: title, aliases: Array(aliases.prefix(8)), artists: artists,
                album: album["name"] as? String ?? "", duration: duration, target: track) else { return nil }
            let cover = (album["picUrl"] as? String).flatMap(Self.coverURL)
            return NowPlayingCatalogMatch(identifier: id, artwork: cover, score: score)
        }.sorted { $0.score > $1.score }
        guard let first = ranked.first else { return nil }
        // If equally strong candidates differ, do not guess a recording.
        if ranked.count > 1, ranked[1].score == first.score, ranked[1].identifier != first.identifier { return nil }
        return first
    }
    static func coverURL(_ raw: String) -> URL? {
        guard raw.utf8.count <= 2048, var parts = URLComponents(string: raw),
              parts.scheme == "http" || parts.scheme == "https", parts.user == nil, parts.password == nil,
              parts.port == nil, parts.fragment == nil, let host = parts.host?.lowercased(),
              ["p1.music.126.net", "p2.music.126.net", "p3.music.126.net", "p4.music.126.net"].contains(host) else { return nil }
        parts.scheme = "https"; parts.queryItems = [.init(name: "param", value: "600y600")]
        return parts.url
    }
    static func decodeLyrics(_ data: Data) -> NowPlayingLyrics? {
        guard data.count <= NowPlayingLyricsDownload.maximumBytes,
              let root = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              (root["code"] as? NSNumber)?.intValue == 200,
              let primary = (root["lrc"] as? [String: Any])?["lyric"] as? String else { return nil }
        // The original language is authoritative. Keep one compact current line;
        // translated variants do not multiply rows or invent synchronization.
        return NowPlayingLyrics(lrc: primary)
    }
}
