#!/usr/bin/env python3
"""Run the unchanged source lyrics/catalog code against synthetic inputs.

Compiles the Mac 1.3.0 Sources/NowPlayingLyrics.swift and Sources/NowPlayingCatalog.swift
verbatim, plus exact slices of the source Track/Application/ArtworkKey types,
the artwork URL allow-list and the controller's lyric precedence methods. A
scripted in-memory fetch replaces URLSession: no network request is made.
Output: windows/tests/fixtures/now-playing-lyrics-source.json
"""
from pathlib import Path
import hashlib, json, os, subprocess, sys

root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build/scratch-nowplaying-lyrics-reference'
destination = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else root / 'windows/tests/fixtures/now-playing-lyrics-source.json'
authority = '4fda15a03ba50675e0b12d69a83efdb6526c19f6'  # Mac 1.3.0 (origin/main)
paths = ['Sources/NowPlayingController.swift', 'Sources/NowPlayingLyrics.swift', 'Sources/NowPlayingCatalog.swift', 'Sources/NowPlayingArtwork.swift']
texts = {}
for path in paths:
    data = (root / path).read_bytes()
    assert data == subprocess.check_output(['git', 'show', authority + ':' + path], cwd=root), path
    texts[path] = data.decode()


def between(text, start, end):
    first = text.index(start)
    return text[first:text.index(end, first)]


controller, lyrics, catalog, artwork = (texts[p] for p in paths)
source = 'import Foundation\n'
source += between(controller, 'enum NowPlayingSource:', '\nenum NowPlayingFailure:') + '\n'
source += between(artwork, 'struct NowPlayingArtworkKey:', '\nprivate final class NowPlayingArtworkToken') + '\n'
source += lyrics.replace('import Foundation\n', '', 1) + '\n'
source += catalog.replace('import Foundation\n', '', 1) + '\n'
source += 'enum SourceArtworkURL {\n' + between(artwork, "    /// NetEase's image CDN serves any requested square", '    /// Synthetic art for isolated') + '}\n'
# The loader's static size rules only (NowPlayingCatalog.coverURL / coverPixels read its bounds).
source += 'enum NowPlayingArtworkLoader {\n' + between(artwork, '    static let maximumBytes = 8 * 1_024 * 1_024', '    private(set) var image: CGImage?') + '}\n'
source += '''
final class ArtworkStub {
    var provided: [String] = []
    func request(application: NowPlayingApplication, track: NowPlayingTrack) {}
    func clear() {}
    func provideArtworkURL(_ url: URL, application: NowPlayingApplication, track: NowPlayingTrack) { provided.append(url.absoluteString) }
}
struct SourceSnapshot { var application: NowPlayingApplication?; var track: NowPlayingTrack? }
final class SourceController {
    var active = true
    var snapshot = SourceSnapshot()
    let artworkLoader = ArtworkStub()
    let lyricsLoader: NowPlayingLyricsLoader
    let catalog: NowPlayingCatalog?
    // NowPlayingController.updateLyricsRetry reads these; the scheduler never fires (retries are
    // driven by the scenario's own steps re-showing the track once its clock passed the due time).
    let lyricsClock: (() -> TimeInterval)?
    var lyricsRetryServed: TimeInterval?
    var lyricsRetry: (due: TimeInterval, cancel: () -> Void)?
    var scheduled: [TimeInterval] = []
    func scheduleLyricsRetry(_ delay: TimeInterval, _ action: @escaping () -> Void) -> () -> Void { scheduled.append(delay); return {} }
    /// `clock` nil: the offline fixture rule (a failure stays the cached negative), as before 1.3.0.
    init(fetch: @escaping NowPlayingLyricsLoader.Fetch, catalog enabled: Bool, clock: (() -> TimeInterval)?) {
        lyricsLoader = NowPlayingLyricsLoader(fetch: fetch)
        catalog = enabled ? NowPlayingCatalog(fetch: fetch) : nil
        lyricsClock = clock
        lyricsLoader.clock = clock; catalog?.clock = clock
        lyricsLoader.onChange = { [weak self] in self?.notifyObservers() }
        lyricsLoader.onRetry = { [weak self] in self?.updateLyricsRetry() }
        catalog?.onChange = { [weak self] in self?.catalogChanged() }
    }
    func update() { changed() }
    func hide() { active = false; snapshot = SourceSnapshot(); artworkLoader.clear(); lyricsLoader.clear(); catalog?.clear() }
    func invalidate() {
        if let app = snapshot.application, let track = snapshot.track {
            lyricsLoader.invalidateMissing(application: app, track: track)
            catalog?.invalidateMissing(application: app, track: track)
        }
    }
''' + between(controller, '    var lyrics: NowPlayingLyrics? {', '    init(backend: NowPlayingBackend') + between(controller, '    private func changed() {', '    private func notifyObservers()') + '''    func notifyObservers() {}
}
'''
source += r'''
func number(_ value: Any?) -> Double? { (value as? NSNumber)?.doubleValue }
func track(_ j: [String: Any]) -> NowPlayingTrack {
    NowPlayingTrack(title: j["title"] as? String ?? "", artist: j["artist"] as? String ?? "", album: j["album"] as? String ?? "",
        duration: number(j["duration"]), position: number(j["position"]) ?? 0, isPlaying: j["playing"] as? Bool ?? true, sampledAt: 0,
        identifier: j["identifier"] as? String, timedLyrics: j["lyrics"] as? String, artworkRevision: nil, supportsSeeking: true)
}
func application(_ name: String) -> NowPlayingApplication {
    NowPlayingApplication(source: NowPlayingSource(rawValue: name)!, pid: 7, bundleURL: URL(fileURLWithPath: "/EndfieldHUD-Fixture/\(name).app"))
}
func lines(_ value: NowPlayingLyrics?) -> Any {
    value.map { $0.lines.map { [$0.time, $0.text] as [Any] } as Any } ?? NSNull()
}
func body(_ value: Any?) -> Data? { (value as? String).map { Data($0.utf8) } }
func optional(_ value: Any?) -> Any { value ?? NSNull() }

final class Script {
    var urls: [String] = []
    var completions: [(Data?) -> Void] = []
    var cancelled: [Int] = []
    var immediate = false
    func fetch(_ url: URL, _ completion: @escaping (Data?) -> Void) -> (() -> Void) {
        let index = urls.count
        urls.append(url.absoluteString); completions.append(completion)
        if immediate { completion(nil); return {} }
        return { [weak self] in self?.cancelled.append(index) }
    }
}

let input = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1]))) as! [String: Any]
var result = input
result["normalized"] = (input["normalized"] as! [String]).map { ["input": $0, "expected": NowPlayingTrackMatcher.normalized($0)] }
result["artists"] = (input["artists"] as! [String]).map { ["input": $0, "expected": NowPlayingTrackMatcher.artists($0).sorted()] as [String: Any] }
result["scores"] = (input["scores"] as! [[String: Any]]).map { row -> [String: Any] in
    var row = row
    row["expected"] = optional(NowPlayingTrackMatcher.score(title: row["title"] as! String, aliases: row["aliases"] as? [String] ?? [],
        artists: row["artists"] as! [String], album: row["album"] as! String, duration: number(row["duration"]),
        target: track(row["target"] as! [String: Any])))
    return row
}
result["urls"] = (input["urls"] as! [[String: Any]]).map { row -> [String: Any] in
    var row = row; let t = track(row["track"] as! [String: Any])
    row["expected"] = ["lrclibGet": optional(NowPlayingLyricsLoader.requestURL(for: t)?.absoluteString),
                       "lrclibSearch": optional(NowPlayingLyricsLoader.searchURL(for: t)?.absoluteString),
                       "neteaseSearch": optional(NowPlayingCatalog.searchURL(t)?.absoluteString)]
    return row
}
result["lyricsURLs"] = (input["lyricsURLs"] as! [NSNumber]).map { ["id": $0, "expected": NowPlayingCatalog.lyricsURL(identifier: $0.int64Value).absoluteString] as [String: Any] }
result["responses"] = (input["responses"] as! [String]).map { ["body": $0, "expected": lines(NowPlayingLyricsLoader.decodeResponse(Data($0.utf8)))] as [String: Any] }
result["searches"] = (input["searches"] as! [[String: Any]]).map { row -> [String: Any] in
    var row = row; row["expected"] = lines(NowPlayingLyricsLoader.decodeSearch(Data((row["body"] as! String).utf8), track: track(row["track"] as! [String: Any]))); return row
}
result["matches"] = (input["matches"] as! [[String: Any]]).map { row -> [String: Any] in
    var row = row
    row["expected"] = NowPlayingCatalog.match(data: Data((row["body"] as! String).utf8), track: track(row["track"] as! [String: Any])).map {
        ["identifier": NSNumber(value: $0.identifier), "artwork": optional($0.artwork?.absoluteString), "score": $0.score] as [String: Any] } ?? NSNull()
    return row
}
result["covers"] = (input["covers"] as! [String]).map { ["input": $0, "expected": optional(NowPlayingCatalog.coverURL($0)?.absoluteString)] }
result["sizedCovers"] = (input["sizedCovers"] as! [[String: Any]]).map { row -> [String: Any] in
    var row = row; row["expected"] = optional(NowPlayingCatalog.coverURL(row["input"] as! String, pixels: (row["pixels"] as! NSNumber).intValue)?.absoluteString); return row
}
result["coverPixels"] = (input["coverPixels"] as! [NSNumber]).map { ["drawn": $0, "expected": NowPlayingCatalog.coverPixels(forDrawn: $0.intValue)] as [String: Any] }
result["remoteCovers"] = (input["remoteCovers"] as! [[String: Any]]).map { row -> [String: Any] in
    var row = row; row["expected"] = SourceArtworkURL.remoteURL(URL(string: row["url"] as! String)!, pixels: (row["pixels"] as! NSNumber).intValue).absoluteString; return row
}
result["pixelSizes"] = (input["pixelSizes"] as! [[String: Any]]).map { row -> [String: Any] in
    var row = row
    let scale: CGFloat = (row["scale"] as? String) == "nan" ? .nan : CGFloat(number(row["scale"])!)
    row["expected"] = NowPlayingArtworkLoader.pixelSize(points: CGFloat(number(row["points"])!), scale: scale); return row
}
result["stamps"] = (input["stamps"] as! [[String: Any]]).map { row -> [String: Any] in
    var row = row
    let text = (row["input"] as? String) ?? String(repeating: row["repeat"] as! String, count: (row["count"] as! NSNumber).intValue)
    let output = NowPlayingLyrics.normalizingNetEaseStamps(text)
    if row["input"] != nil { row["expected"] = output } else { row["unchanged"] = output == text; row["bytes"] = output.utf8.count }
    return row
}
result["backoff"] = (1...7).map { ["failures": $0, "expected": NowPlayingLyricsBackoff.delay(failures: $0)] as [String: Any] }
result["catalogLyrics"] = (input["catalogLyrics"] as! [String]).map { ["body": $0, "expected": lines(NowPlayingCatalog.decodeLyrics(Data($0.utf8)))] as [String: Any] }
result["allowedLyrics"] = (input["allowedLyrics"] as! [String]).map { ["url": $0, "expected": NowPlayingLyricsDownload.isAllowedURL(URL(string: $0)!)] as [String: Any] }
result["allowedArtwork"] = (input["allowedArtwork"] as! [String]).map { ["url": $0, "expected": SourceArtworkURL.isAllowedRemoteURL(URL(string: $0)!)] as [String: Any] }
result["scenarios"] = (input["scenarios"] as! [[String: Any]]).map { scenario -> [String: Any] in
    var scenario = scenario
    let script = Script(); script.immediate = scenario["immediate"] as? Bool ?? false
    var time: TimeInterval = 0
    let clocked = scenario["clock"] as? Bool ?? false
    let owner = SourceController(fetch: { script.fetch($0, $1) }, catalog: scenario["catalog"] as? Bool ?? true, clock: clocked ? { time } : nil)
    scenario["steps"] = (scenario["steps"] as! [[String: Any]]).map { step -> [String: Any] in
        var step = step
        switch step["op"] as! String {
        case "show":
            owner.active = true
            owner.snapshot = SourceSnapshot(application: application(step["source"] as! String), track: track(step["track"] as! [String: Any]))
            owner.update()
        case "hide": owner.hide()
        case "invalidate": owner.invalidate(); owner.update()
        case "complete": script.completions[step["index"] as! Int](body(step["body"]))
        case "advance": time += number(step["seconds"])!
        case "forget": owner.lyricsLoader.forgetFailures(); owner.catalog?.forgetFailures()
        default: fatalError("unknown step")
        }
        step["expected"] = ["urls": script.urls, "cancelled": script.cancelled, "lyrics": lines(owner.lyrics),
                            "catalogArtwork": optional(owner.catalog?.artwork?.absoluteString),
                            "catalogLoading": owner.catalog?.isLoading ?? false, "provided": owner.artworkLoader.provided] as [String: Any]
        if clocked {
            step["retry"] = ["time": time, "lyrics": optional(owner.lyricsLoader.retryTime), "catalog": optional(owner.catalog?.retryTime),
                             "scheduled": owner.scheduled] as [String: Any]
        }
        return step
    }
    return scenario
}
try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys]).write(to: URL(fileURLWithPath: CommandLine.arguments[2]))
'''

# Synthetic inputs only; no real listening data.
base = {'title': 'Synthetic Song', 'artist': 'Example Artist', 'album': 'Fixture Album', 'duration': 233.4}
normalized = ['Hello, World!', 'ＨＥＬＬＯ　ｗｏｒｌｄ', 'Café', 'Café', 'ÆØÅ', 'Straße', 'STRASSE', 'İstanbul', 'ﬁne', 'ｶﾞｸ', 'ガク', 'カク',
              '愛してる', '사랑해', '한국어 (Remastered)', 'Ⅻ', '①②', 'x²', 'naïve — résumé', '🎵Song🎶', 'Mötley Crüe', 'Ǆ', 'ǅ', 'ﬀ',
              'Σίσυφος', 'ΣΊΣΥΦΟΣ', 'ß', 'ẞ', 'ﾃｽﾄ', 'テスト', 'Ｔｅｓｔ１２３', '٣٤٥', 'ë́', '', '!!!', 'a​b', 'Ａ・Ｂ', 'Ｒ＆Ｂ',
              'Beyoncé', 'BEYONCÉ', '가', '가', 'Ω', 'Ω', 'K', 'k', 'ı', 'I', 'ｉ', 'Ⓐⓑ', '㍿', '〜', 'ー', 'ｰ', 'Ő', 'Ø',
              'ł', 'Đ', 'đ', 'ŉ', 'Ǉ', 'ǰ', 'ΐ', 'ﬆ', 'Ｍｒ．Ｃｈｉｌｄｒｅｎ', 'Mr.Children', '周杰倫', '周杰伦', '½', '₂', 'ⅷ', '一二三', 'ক্ষ',
              'हिन्दी', 'ไทย', 'ạ', 'ế', 'Ḁ', 'ǖ', 'ỳ', 'ŵ', 'ṡ', 'a\u0323', 'й', 'ё', 'Ё', 'ά', 'ἀ', 'ᾳ', 'ϓ', 'ǿ', 'ẛ', 'ǻ', 'ȁ', 'ș',
              'ŀ', 'ĳ', 'ﬃ', '\u212b', 'あ\u0301', 'x\u20dd', 'a\u3099', 'Ӂ', 'Ԙ', 'ḁ', 'ẚ', 'Ǣ', 'ﾡ', 'ﾟ', 'ﾊﾟ', 'ﾊﾞ', '￦', '｡', 'Ａ\u0301', 'ｅ\u0301',
              'é\u0302', 'ǅ\u030c', 'ᏸ', 'ꮰ', 'Ꮿ', 'ὒ', 'ﬅ', 'İ\u0301', 'Ⅻ\u0301', '\u0301', 'ä\u20dd', 'ɐ', 'Ɐ', 'ꞵ', 'ǈ',
              '中\u0301', '가\u0301', 'ア\u0301', 'ｱ\u0301', 'α\u0301', 'б\u0301', 'ש\u05b8', 'ก\u0e34', 'Ⅰ\u0301', '1\u0301', '①\u0301', '\u0301a',
              'Ա\u0301', 'ა\u0301', 'ᄀ\u0301', 'ㄱ', '\u3099', 'ﾞ', 'xﾞ', 'ア\u3099', 'ｳﾞ', '٣\u0301', 'ǅ\u0308', 'ა', 'Ⴀ', 'ⴀ', 'Ა', 'Ꙁ', 'ꙁ', 'Ա', 'ա', 'اَ', 'Ꭰ', 'ꭰ', '\U0001d400', '\U0001d7ce', 'Ⅰ', 'ⅰ', ' x', 'a-b_c', '™', '℃', '゛', 'ゔ', 'ゔ']
artists = ['A/B', 'A,B', 'A、B', 'A;B', 'A&B', 'A & B / C', '/A', 'A//B', 'Ａ/ａ', '', '   ', 'A feat. B', 'Simon & Garfunkel', '周杰伦、方文山', '!!!/???', 'A；B', 'A＆B']
targets = [base, {**base, 'artist': 'A / B'}, {**base, 'duration': None}, {**base, 'album': ''}, {**base, 'title': '!!!'}, {**base, 'duration': 900}, {**base, 'duration': 120}]
scores = []
for target in targets:
    for title, aliases, names, album, duration in [
        ('Synthetic Song', [], ['Example Artist'], 'Fixture Album', 233.4), ('synthetic  song!', [], ['EXAMPLE ARTIST'], 'fixture album', 234.4),
        ('Other', ['Synthetic Song'], ['Example Artist'], '', 230), ('Other', [], ['Example Artist'], '', 233), ('Synthetic Song', [], ['Nobody'], '', 233),
        ('Synthetic Song', [], ['Example Artist', 'Guest'], 'Other', 236.4), ('Synthetic Song', [], ['Example Artist/Guest'], '', 241.5),
        ('Synthetic Song', [], ['Example Artist'], '', 224.3), ('Synthetic Song', [], ['A', 'B'], '', 233.4), ('Synthetic Song', [], ['B/A'], '', 233.4),
        ('Synthetic Song', [], [''], '', 233.4), ('Synthetic Song', [], [], '', 233.4), ('Synthetic Song', [], ['Example Artist'], '', 0),
        ('Synthetic Song', [], ['Example Artist'], '', -5), ('Synthetic Song', [], ['Example Artist'], '', None), ('???', [], ['Example Artist'], '', None),
        ('Synthetic Song', [], ['Example Artist'], '', 908), ('Synthetic Song', [], ['Example Artist'], '', 892.5), ('Synthetic Song', [], ['Example Artist'], '', 117),
        ('Synthetic Song', [], ['Example Artist'], '', 123.1), ('Synthetic Song', [], ['Example Artist'], '', 1e309), ('Ｓｙｎｔｈｅｔｉｃ Ｓｏｎｇ', [], ['Éxample Ártist'], 'FIXTURE-ALBUM', 232.4)]:
        row = {'title': title, 'aliases': aliases, 'artists': names, 'album': album, 'target': target}
        if duration is not None:
            row['duration'] = duration if duration != 1e309 else 1e300
        scores.append(row)
url_tracks = [base, {**base, 'duration': 232.5}, {**base, 'duration': 233.5}, {**base, 'duration': None}, {**base, 'album': ''},
              {**base, 'title': ''}, {**base, 'artist': ''}, {**base, 'artist': 'A/B、C'}, {**base, 'artist': '/Leading'}, {**base, 'artist': '、'},
              {**base, 'title': ''.join(chr(c) for c in range(0x20, 0x7f)), 'artist': 'é中🎵\u0000\u007f\n', 'album': 'a+b=c&d#e%f'},
              {**base, 'title': '晴天', 'artist': '周杰伦', 'album': '叶惠美', 'duration': 269.08}, {**base, 'duration': 0.4}, {**base, 'duration': 604800}]
synced = '[00:01.00] One\n[00:02.50] Two\n[00:04] Three'
responses = [json.dumps({'syncedLyrics': synced}), json.dumps({'syncedLyrics': None, 'plainLyrics': 'x'}), '{}', '[]', 'not json',
             json.dumps({'syncedLyrics': 5}), json.dumps({'syncedLyrics': '[00:01]'}), json.dumps({'id': 1, 'syncedLyrics': '[00:03]B\n[00:01]A', 'instrumental': False}),
             '﻿' + json.dumps({'syncedLyrics': synced}), json.dumps({'syncedLyrics': synced, 'syncedLyrics2': 1}), '  ' + json.dumps({'syncedLyrics': '[00:01] spaced'}) + '\n']


def row(title='Synthetic Song', artist='Example Artist', album='Fixture Album', duration=233.4, lrc=synced, **extra):
    value = {'trackName': title, 'artistName': artist, 'albumName': album, 'duration': duration, 'syncedLyrics': lrc}
    value.update(extra)
    return value


searches = [[row()], [row(lrc=None), row(lrc='[00:09] Alt')], [row(title='Other'), row(artist='Other')],
            [row(lrc='[00:01] A'), row(lrc='[00:01] B')], [row(lrc='[00:01] A'), row(lrc='[00:01] A')],
            [row(album='', lrc='[00:01] Low'), row(lrc='[00:01] High')], [row(duration=240), row(duration=236.4, lrc='[00:01] Within')],
            [row(duration=True)], [row(duration='233')], [row(artist=None)], [row(title=None)], [{'syncedLyrics': synced}],
            [row(lrc='[00:01] one'), row(album='x', lrc='[00:01] two'), row(album='y', lrc='[00:01] three')],
            [row(lrc='[00:01] é'), row(lrc='[00:01] é')], [row(lrc='')], [row()] * 3 + [row(lrc='[00:02] late')]]
search_rows = [{'body': json.dumps(s, ensure_ascii=False), 'track': base} for s in searches]
search_rows += [{'body': '{"trackName":"x"}', 'track': base}, {'body': 'garbage', 'track': base}, {'body': '[1,2,"x",null]', 'track': base},
                {'body': json.dumps([row(lrc='[00:0%d] r%d' % (n % 10, n), album='') for n in range(101)] + [row(lrc='[00:01] final')]), 'track': base},
                {'body': json.dumps([row()]), 'track': {**base, 'duration': None}}, {'body': json.dumps([row(artist='Example Artist & Guest')]), 'track': {**base, 'artist': 'Guest'}}]


def song(identifier=1901371647, name='Synthetic Song', artists=('Example Artist',), album='Fixture Album', duration=233400, pic='http://p1.music.126.net/AbC==/109951163.jpg', **extra):
    value = {'id': identifier, 'name': name, 'ar': [{'id': 1, 'name': a} for a in artists], 'al': {'id': 2, 'name': album, 'picUrl': pic}, 'dt': duration}
    value.update(extra)
    return value


def search(songs, code=200):
    return json.dumps({'code': code, 'result': {'songs': songs, 'songCount': len(songs)}}, ensure_ascii=False)


legacy = {'id': 77, 'name': 'Synthetic Song', 'artists': [{'name': 'Example Artist'}], 'album': {'name': 'Fixture Album', 'picUrl': 'https://p3.music.126.net/x/y.jpg'}, 'duration': 233000, 'alias': ['Alias']}
matches = [search([song()]), search([song()], code=404), json.dumps({'code': '200', 'result': {'songs': [song()]}}), json.dumps({'code': 200}),
           search([song(identifier=0)]), search([song(identifier=-4)]), search([song(identifier=123.7)]), search([song(), song(identifier=2)]),
           search([song(), song()]), search([song(album='Other'), song(identifier=2)]), search([legacy]), search([dict(song(), ar='bad', artists=[{'name': 'Example Artist'}])]),
           search([dict(song(name='Other'), alia=['Synthetic Song'])]), search([dict(song(name='Other'), alia=['Synthetic Song', 4])]),
           search([song(pic='http://p9.music.126.net/a.jpg')]), search([song(pic=None)]), search([dict(song(), dt=None, duration=233000)]),
           search([song(duration=260000)]), search([song(name='Song %d' % n, identifier=n + 1) for n in range(20)] + [song(identifier=99)]),
           search([song(artists=['Guest'] * 40 + ['Example Artist'])]), search([song(artists=['Example Artist'] + ['Guest'] * 40)]), 'nope', '[]',
           search([dict(song(), id='12')]), search([song(identifier=9007199254740993)]), json.dumps({'code': 200.9, 'result': {'songs': [song()]}}),
           search([song(pic='HTTP://p1.music.126.net/upper.jpg')]), search([dict(song(), al='x', album={'name': 'Fixture Album', 'picUrl': 'http://p2.music.126.net/legacy.jpg'})])]
match_rows = [{'body': b, 'track': base} for b in matches] + [{'body': search([song()]), 'track': {**base, 'duration': None, 'album': ''}}]
covers = ['http://p1.music.126.net/AbC==/109951163.jpg', 'https://p4.music.126.net/x/y.png?param=130y130', 'http://P1.Music.126.net/a.jpg',
          'http://p5.music.126.net/a.jpg', 'https://music.126.net/a.jpg', 'ftp://p1.music.126.net/a.jpg', 'HTTP://p1.music.126.net/a.jpg',
          'http://p1.music.126.net:80/a.jpg', 'http://p1.music.126.net:/a.jpg', 'http://u@p1.music.126.net/a.jpg', 'http://:@p1.music.126.net/a.jpg',
          'http://p1.music.126.net/a.jpg#frag', 'http://p1.music.126.net/a.jpg#', 'http://p1.music.126.net', 'http://p1.music.126.net/',
          'http://p1.music.126.net/a b.jpg', 'http://p1.music.126.net/é.jpg', 'http://p1.music.126.net/%zz.jpg', 'http://p1.music.126.net/%41.jpg',
          '//p1.music.126.net/a.jpg', 'p1.music.126.net/a.jpg', '', 'http://p1.music.126.net.', 'http://p1.music.126.net./a.jpg',
          'http://p1.music.126.net/a.jpg?', 'http://p1.music.126.net/a.jpg?x=1&y=2', 'http://p1.music.126.net/' + 'a' * 2100,
          'http://p1.music.126.net/a|b.jpg', 'http://p1.music.126.net/[x].jpg', 'http://p1.music.126.net/a\\b.jpg', 'http://p1.music.126.net/a"b.jpg',
          'http://p1.music.126.net/a^b`c{d}.jpg', 'http://p1.music.126.net/a.jpg?q=a b', 'http://p1.music.126.net\\a.jpg', 'http:p1.music.126.net/a.jpg',
          'http:///p1.music.126.net/a.jpg', 'https://p2.music.126.net/a;b=c/d@e:f!$&\'()*+,.jpg']
catalog_lyrics = [json.dumps({'code': 200, 'lrc': {'version': 3, 'lyric': synced}, 'tlyric': {'lyric': '[00:01.00] 译'}}, ensure_ascii=False),
                  json.dumps({'code': 200, 'lrc': {'lyric': ''}}), json.dumps({'code': 200, 'nolyric': True}), json.dumps({'code': 400, 'lrc': {'lyric': synced}}),
                  json.dumps({'code': 200, 'lrc': {'lyric': '[ti:x]\n[by:y]\n[00:01.000]作词 : A\n[00:05.123]Line'}}, ensure_ascii=False), 'x',
                  json.dumps({'code': 200, 'lrc': {'lyric': 7}}), json.dumps({'code': 200, 'lrc': 'flat'})]
allowed_lyrics = ['https://lrclib.net/api/get?track_name=a', 'https://lrclib.net/api/search?q=1', 'https://lrclib.net/api/get', 'https://lrclib.net/api/other',
                  'http://lrclib.net/api/get', 'https://lrclib.net:443/api/get', 'https://u@lrclib.net/api/get', 'https://lrclib.net/api/get#x',
                  'https://LRCLIB.net/api/get', 'https://music.163.com/api/cloudsearch/pc?s=a', 'https://music.163.com/api/song/lyric?id=1',
                  'https://music.163.com/api/song/detail', 'https://evil.example/api/get', 'https://lrclib.net/api/get/', 'https://lrclib.net/api/%67et',
                  'https://lrclib.net/api/get?' + 'a' * 8200]
allowed_artwork = ['https://p1.music.126.net/a.jpg?param=600y600', 'https://P2.MUSIC.126.NET/a.jpg', 'https://p1.music.126.net:443/a.jpg',
                   'https://p1.music.126.net:8443/a.jpg', 'http://p1.music.126.net/a.jpg', 'https://i.scdn.co/image/ab67', 'https://mosaic.scdn.co/640/x',
                   'https://image-cdn.spotifycdn.com/x', 'https://p1.music.126.net./a.jpg', 'https://u@i.scdn.co/x', 'https://i.scdn.co/x#f',
                   'https://evil.scdn.co/x', 'https://p1.music.126.net/' + 'a' * 2100]

sized_covers = [{'input': 'http://p1.music.126.net/AbC==/109951163.jpg', 'pixels': n} for n in (50, 64, 600, 800, 960, 2048, 3000)]
sized_covers += [{'input': 'https://p4.music.126.net/x/y.png?param=130y130', 'pixels': 960}, {'input': 'https://i.scdn.co/image/ab67', 'pixels': 960}]
cover_pixels = [100, 800, 801, 960, 2048, 4000, 0, -5]
remote_covers = [{'url': u, 'pixels': n} for u in ('https://p1.music.126.net/a.jpg?param=130y130', 'https://P2.MUSIC.126.NET/a.jpg', 'https://i.scdn.co/image/ab67')
                 for n in (720, 960, 4000)]
pixel_sizes = [{'points': 480, 'scale': s} for s in (1, 1.5, 2, 2.75, 0, 'nan', 3.999, 4.2675, 8)] + [{'points': 330, 'scale': 2}, {'points': 1, 'scale': 1}]
stamps = [{'input': x} for x in ('[00:12:34]Line', '[01:02:345]Line', '[00:12.34]Already', '[1:02:03]Hours', '[001:02:03]Three', '[00:12:3]Short', '[00:12:3456]Long',
                                 '[00:12:34]A\n[00:15:67]B[00:16:00]C', '[ti:x][00:01:02', '[00:aa:00]x', '[００:12:34]wide', '')]
stamps += [{'repeat': '[00:12:34]', 'count': 52429}, {'repeat': '[00:12:34]', 'count': 52428}]  # 524290 bytes (> 512 KiB): unchanged; 524280: rewritten
T = lambda **kw: {**base, **kw}
lrc_body = lambda text: json.dumps({'syncedLyrics': text})
net_search = search([song()])
net_lyric = json.dumps({'code': 200, 'lrc': {'lyric': '[00:01] Catalog line\n[00:03] Second'}})
scenarios = [
    {'name': 'lrclib get success', 'steps': [{'op': 'show', 'source': 'spotify', 'track': base}, {'op': 'complete', 'index': 0, 'body': lrc_body(synced)},
                                             {'op': 'show', 'source': 'spotify', 'track': {**base, 'position': 50}}]},
    {'name': 'lrclib search fallback', 'steps': [{'op': 'show', 'source': 'system', 'track': base}, {'op': 'complete', 'index': 0, 'body': None},
                                                 {'op': 'complete', 'index': 1, 'body': json.dumps([row(lrc='[00:01] Found')])}]},
    {'name': 'ambiguous search is negative and cached', 'steps': [{'op': 'show', 'source': 'music', 'track': base}, {'op': 'complete', 'index': 0, 'body': '{}'},
                                                                  {'op': 'complete', 'index': 1, 'body': json.dumps([row(lrc='[00:01] A'), row(lrc='[00:01] B')])},
                                                                  {'op': 'show', 'source': 'music', 'track': T(title='Next')}, {'op': 'show', 'source': 'music', 'track': base},
                                                                  {'op': 'invalidate'}]},
    {'name': 'embedded lyrics take precedence', 'steps': [{'op': 'show', 'source': 'spotify', 'track': T(lyrics='[00:01] Embedded')},
                                                         {'op': 'show', 'source': 'spotify', 'track': T(lyrics='[00:01] Corrected')},
                                                         {'op': 'show', 'source': 'spotify', 'track': T(lyrics='not lrc')},
                                                         {'op': 'show', 'source': 'spotify', 'track': T(title='Web', lyrics='not lrc')},
                                                         {'op': 'complete', 'index': 0, 'body': lrc_body('[00:01] Web')}]},
    {'name': 'track change cancels', 'steps': [{'op': 'show', 'source': 'spotify', 'track': base}, {'op': 'show', 'source': 'spotify', 'track': T(title='Second')},
                                               {'op': 'complete', 'index': 0, 'body': lrc_body('[00:01] Stale')}, {'op': 'complete', 'index': 1, 'body': lrc_body('[00:01] Second')},
                                               {'op': 'show', 'source': 'spotify', 'track': base}]},
    {'name': 'eight entry cache', 'steps': [x for n in range(9) for x in ({'op': 'show', 'source': 'spotify', 'track': T(title='Song %d' % n)},
                                                                          {'op': 'complete', 'index': n, 'body': lrc_body('[00:01] Song %d' % n)})]
     + [{'op': 'show', 'source': 'spotify', 'track': T(title='Song 8')}, {'op': 'show', 'source': 'spotify', 'track': T(title='Song 1')},
        {'op': 'show', 'source': 'spotify', 'track': T(title='Song 0')}]},
    {'name': 'hide cancels and show refetches', 'steps': [{'op': 'show', 'source': 'spotify', 'track': base}, {'op': 'hide'},
                                                         {'op': 'complete', 'index': 0, 'body': lrc_body(synced)}, {'op': 'show', 'source': 'spotify', 'track': base},
                                                         {'op': 'complete', 'index': 1, 'body': lrc_body(synced)}, {'op': 'hide'}, {'op': 'show', 'source': 'spotify', 'track': base}]},
    {'name': 'netease catalog lyrics and cover', 'steps': [{'op': 'show', 'source': 'netease', 'track': base}, {'op': 'complete', 'index': 0, 'body': net_search},
                                                          {'op': 'complete', 'index': 1, 'body': net_lyric}, {'op': 'show', 'source': 'netease', 'track': {**base, 'position': 9}},
                                                          {'op': 'show', 'source': 'netease', 'track': T(title='Other')}, {'op': 'show', 'source': 'netease', 'track': base}]},
    {'name': 'netease no match falls back to lrclib', 'steps': [{'op': 'show', 'source': 'netease', 'track': base}, {'op': 'complete', 'index': 0, 'body': search([])},
                                                                {'op': 'complete', 'index': 1, 'body': lrc_body('[00:01] Fallback')}]},
    {'name': 'netease lyric failure falls back', 'steps': [{'op': 'show', 'source': 'netease', 'track': base}, {'op': 'complete', 'index': 0, 'body': net_search},
                                                          {'op': 'complete', 'index': 1, 'body': json.dumps({'code': 200, 'nolyric': True})}, {'op': 'complete', 'index': 2, 'body': None},
                                                          {'op': 'complete', 'index': 3, 'body': '[]'}, {'op': 'invalidate'}]},
    {'name': 'netease embedded lyrics still use catalog cover', 'steps': [{'op': 'show', 'source': 'netease', 'track': T(lyrics='[00:02] Embedded')},
                                                                         {'op': 'complete', 'index': 0, 'body': net_search}, {'op': 'complete', 'index': 1, 'body': net_lyric}]},
    {'name': 'netease hide during lyric restarts search', 'steps': [{'op': 'show', 'source': 'netease', 'track': base}, {'op': 'complete', 'index': 0, 'body': net_search},
                                                                   {'op': 'hide'}, {'op': 'complete', 'index': 1, 'body': net_lyric}, {'op': 'show', 'source': 'netease', 'track': base},
                                                                   {'op': 'complete', 'index': 2, 'body': net_search}, {'op': 'complete', 'index': 3, 'body': net_lyric}]},
    {'name': 'netease to spotify clears catalog', 'steps': [{'op': 'show', 'source': 'netease', 'track': base}, {'op': 'show', 'source': 'spotify', 'track': base},
                                                           {'op': 'complete', 'index': 0, 'body': net_search}, {'op': 'complete', 'index': 1, 'body': lrc_body('[00:01] Spotify')}]},
    {'name': 'missing metadata never fetches', 'steps': [{'op': 'show', 'source': 'netease', 'track': T(artist='')}, {'op': 'show', 'source': 'spotify', 'track': T(title='')}]},
    {'name': 'no network fixture', 'immediate': True, 'steps': [{'op': 'show', 'source': 'netease', 'track': base}, {'op': 'show', 'source': 'spotify', 'track': T(title='B')},
                                                               {'op': 'invalidate'}]},
    {'name': 'injected backend has no catalog', 'catalog': False, 'steps': [{'op': 'show', 'source': 'netease', 'track': base},
                                                                           {'op': 'complete', 'index': 0, 'body': lrc_body('[00:01] Only lrclib')}]},
    # B9-B10 (1.3.0): a lookup without an answer is a failure retried after its backoff (5, 15 s ...), never "no lyrics".
    {'name': 'lrclib failure retries after backoff', 'clock': True, 'steps': [
        {'op': 'show', 'source': 'spotify', 'track': base}, {'op': 'complete', 'index': 0, 'body': None}, {'op': 'complete', 'index': 1, 'body': None},
        {'op': 'advance', 'seconds': 4.9}, {'op': 'show', 'source': 'spotify', 'track': base}, {'op': 'advance', 'seconds': .1},
        {'op': 'show', 'source': 'spotify', 'track': base}, {'op': 'complete', 'index': 2, 'body': None}, {'op': 'complete', 'index': 3, 'body': None},
        {'op': 'advance', 'seconds': 15}, {'op': 'show', 'source': 'spotify', 'track': base}, {'op': 'complete', 'index': 4, 'body': lrc_body(synced)},
        {'op': 'show', 'source': 'spotify', 'track': base}]},
    {'name': 'lrclib answer without a match is final', 'clock': True, 'steps': [
        {'op': 'show', 'source': 'spotify', 'track': base}, {'op': 'complete', 'index': 0, 'body': None}, {'op': 'complete', 'index': 1, 'body': '[]'},
        {'op': 'advance', 'seconds': 600}, {'op': 'show', 'source': 'spotify', 'track': base}]},
    {'name': 'netease refusal is a catalog failure, lrclib meanwhile', 'clock': True, 'steps': [
        {'op': 'show', 'source': 'netease', 'track': base}, {'op': 'complete', 'index': 0, 'body': json.dumps({'code': -460, 'message': 'Cheating'})},
        {'op': 'complete', 'index': 1, 'body': lrc_body('[00:01] Lrclib')}, {'op': 'advance', 'seconds': 5}, {'op': 'show', 'source': 'netease', 'track': base},
        {'op': 'complete', 'index': 2, 'body': net_search}, {'op': 'complete', 'index': 3, 'body': net_lyric}]},
    {'name': 'netease embedded stamps are normalized', 'steps': [{'op': 'show', 'source': 'netease', 'track': T(lyrics='[00:01:50] Colon stamp\n[00:03:00] Next')},
                                                                 {'op': 'show', 'source': 'spotify', 'track': T(title='Other', lyrics='[00:01:50] Hours stamp')}]},
    {'name': 'hidden forgets failures', 'clock': True, 'steps': [
        {'op': 'show', 'source': 'spotify', 'track': base}, {'op': 'complete', 'index': 0, 'body': None}, {'op': 'complete', 'index': 1, 'body': None},
        {'op': 'forget'}, {'op': 'show', 'source': 'spotify', 'track': base}]},
]
data = {'schemaVersion': 1, 'sourceCommit': authority, 'sourcePins': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
        'normalized': normalized, 'artists': artists, 'scores': scores, 'urls': [{'track': t} for t in url_tracks],
        'lyricsURLs': [1, 1901371647, 9007199254740993, -1, 0], 'responses': responses, 'searches': search_rows, 'matches': match_rows,
        'covers': covers, 'sizedCovers': sized_covers, 'coverPixels': cover_pixels, 'remoteCovers': remote_covers, 'pixelSizes': pixel_sizes, 'stamps': stamps,
        'catalogLyrics': catalog_lyrics, 'allowedLyrics': allowed_lyrics, 'allowedArtwork': allowed_artwork, 'scenarios': scenarios}
out.mkdir(parents=True, exist_ok=True)
(out / 'oracle.swift').write_text(source)
(out / 'input.json').write_text(json.dumps(data, ensure_ascii=False))
env = os.environ.copy(); env.pop('SDKROOT', None)
sdk = subprocess.check_output(['bash', str(root / 'windows/tools/mac_sdk.sh')], text=True, env=env).strip().splitlines()[-1]
subprocess.run(['/usr/bin/swiftc', '-sdk', sdk, '-module-cache-path', str(out / 'module-cache'),
                str(out / 'oracle.swift'), '-o', str(out / 'oracle')], check=True, env=env)
for name in ['output', 'repeat']:
    subprocess.run([str(out / 'oracle'), str(out / 'input.json'), str(out / (name + '.json'))], check=True)
assert (out / 'output.json').read_bytes() == (out / 'repeat.json').read_bytes()
value = json.dumps(json.loads((out / 'output.json').read_text()), ensure_ascii=False, separators=(',', ':'), sort_keys=True) + '\n'
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(value)
print('Original Swift Now Playing lyrics fixture', len(value.encode()), 'bytes SHA256', hashlib.sha256(value.encode()).hexdigest())
