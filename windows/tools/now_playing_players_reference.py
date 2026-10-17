#!/usr/bin/env python3
"""Execute the unchanged source heading split (NowPlayingAccessibility.track).

The Mac compatibility fallback for NetEase, QQ Music and Kugou reads the
player's "Title - Artist" heading and splits it with
`NowPlayingAccessibility.track(heading:position:duration:playing:)`. The
Windows window-title fallback (modules/now_playing_players.cpp) applies the
same split to the player's main window title. This oracle runs the original
method over synthetic headings (no player, window or user data) and writes
windows/tests/fixtures/now-playing-players-source.json.
"""
from pathlib import Path
import hashlib, json, os, subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'build/now-playing-players-reference/source'
out.mkdir(parents=True, exist_ok=True)
commit = 'ca04f142185c7de40acd8523bdb563195d90a1d1'
path = 'Sources/NowPlayingAccessibility.swift'
data = (root / path).read_bytes()
assert data == subprocess.check_output(['git', 'show', commit + ':' + path], cwd=root)
text = data.decode()
begin = text.index('    static func track(heading: String,')
end = text.index('\n    func artwork(for track: NowPlayingTrack', begin)
method = text[begin:end]

headings = [
    'Song - Artist', 'Song - Live - Artist', '  Spaced Title   -   Artist  ', 'No separator', ' - Artist only',
    'Title only - ', 'Title -Artist', 'Title- Artist', '晴天 - 周杰伦', 'Song\t-\tArtist',
    'Song - Artist1/Artist2', 'Song　-　Artist', ' Song - Artist ', 'Song - Artist\n', '\nSong\n - \nArtist\n',
    'Song -́ Artist', 'Song - ́Artist', 'Song ́- Artist', 'Café - Artíst', 'Café - Artist',
    'Song \U0001F3B5 - Artist \U0001F3A4', 'שיר - זמר', '', '-', ' - ', 'A - B - ', 'A  -  B',
    'A - - B', 'A--B - C', '网易云音乐', 'Song (Live) - Artist A/Artist B', 'x' * 600 + ' - ' + 'y' * 600,
    ' Song - Artist ', 'Song​ - Artist', 'Song - ​Artist',
]
source = '''import Foundation
struct NowPlayingTrack {
 let title: String, artist: String
 // Source NowPlayingTrack.init bounds both strings to 512 Characters.
 init(title: String, artist: String, album: String, duration: Double?, position: Double?, isPlaying: Bool, sampledAt: TimeInterval, supportsSeeking: Bool) {
  self.title = String(title.prefix(512)); self.artist = String(artist.prefix(512))
 }
}
enum Source {
''' + method + '''
}
let headings = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1]))) as! [String]
let rows: [[String: Any]] = headings.map { heading in
 if let track = Source.track(heading: heading, position: nil, duration: nil, playing: false) {
  return ["heading": heading, "title": track.title, "artist": track.artist]
 }
 return ["heading": heading, "title": NSNull(), "artist": NSNull()]
}
try JSONSerialization.data(withJSONObject: rows, options: [.sortedKeys]).write(to: URL(fileURLWithPath: CommandLine.arguments[2]))
'''
(out / 'oracle.swift').write_text(source)
(out / 'headings.json').write_text(json.dumps(headings, ensure_ascii=False))
env = os.environ.copy()
env.pop('SDKROOT', None)
sdk = subprocess.check_output(['bash', str(root / 'scripts/build.sh'), '--print-sdk'], text=True, env=env).strip()
subprocess.run(['/usr/bin/swiftc', '-sdk', sdk, '-module-cache-path', str(root / 'build/windows-module-reference/.compiler/module-cache'),
                str(out / 'oracle.swift'), '-o', str(out / 'oracle')], check=True, env=env)
for name in ['output', 'repeat']:
    subprocess.run([str(out / 'oracle'), str(out / 'headings.json'), str(out / (name + '.json'))], check=True)
assert (out / 'output.json').read_bytes() == (out / 'repeat.json').read_bytes()
fixture = {'schemaVersion': 1, 'sourceCommit': commit, 'sourcePins': {path: hashlib.sha256(data).hexdigest()},
           'scope': 'Unchanged NowPlayingAccessibility.track(heading:) over synthetic headings: split at the last " - ", '
                    'trim whitespace and newlines, a title is required; NowPlayingTrack bounds both to 512 Characters.',
           'rows': json.loads((out / 'output.json').read_text())}
dest = root / 'windows/tests/fixtures/now-playing-players-source.json'
dest.write_text(json.dumps(fixture, ensure_ascii=False, separators=(',', ':')) + '\n')
print(dest, len(dest.read_bytes()), hashlib.sha256(dest.read_bytes()).hexdigest())
