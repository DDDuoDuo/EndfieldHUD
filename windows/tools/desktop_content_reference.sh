#!/bin/bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "$#" -ne 3 ]; then echo "Usage: $0 output-directory source-content-directory source-packet-directory" >&2; exit 2; fi
out="$1"; content="$2"; packet="$3"
mkdir -p "$out"
python3 - "$out" "$content" "$packet" "$repo" <<'PY'
import hashlib,json,sys
from pathlib import Path
out,root,packet,repo=map(Path,sys.argv[1:])
j=json.loads((root/'watch-content.json').read_text());a=json.loads((packet/'animation.json').read_text())
slots={s['buttonID']:s for s in j['slots']};nodes={n['id']:n for n in a['scene']['nodes']}
titles={(x['language'],m['target']):m['title']for x in j['chrome']for m in x['moduleTitles']}
cases=[];measurements=[];keys={}
for row in j['variants']+j['customCaptions']:
 slot=slots[row['buttonID']];record=j['contentTrees'][row['caption']];raw=(root/record['file']).read_bytes()
 assert hashlib.sha256(raw).hexdigest()==record['sha256']
 tree=json.loads(raw);text=tree['text'];rect=nodes[slot['captionID']]['transform']['raw']['m_SizeDelta'];module='target'in row
 target=row.get('target','custom');title=titles[row['language'],target]if module else j['customTitles'][row['titleIndex']]
 area=[rect['x'],rect['y']]
 if target=='fileShelf'and row['language']in('japanese','korean'):area=[max(area[0],124),max(area[1],56)]
 key=(text['string'],max(1,area[0]-2),bool(text['font']['symbolicTraits']&2),text['wrapped'])
 if key not in keys:
  keys[key]=len(measurements);measurements.append({'text':key[0],'width':key[1],'bold':key[2],'wrapped':key[3]})
 cases.append({'button':slot['buttonID'],'language':row['language'],'target':target,'title':title,'module':module,'right':slot['group']=='right','selected':row.get('selected',False),'dark':row.get('theme','dark')=='dark','authoredSize':[rect['x'],rect['y']],'measurement':keys[key], 'expected':{'text':text['string'],'fontSize':text['fontSize'],'bold':key[2],'wrapped':text['wrapped'],'ellipsis':text['truncation']=='end','bounds':tree['bounds'],'color':text['foregroundColor']['sRGB']}})
(out/'input.json').write_text(json.dumps({'cases':cases,'measurements':measurements,'sourceSHA256':hashlib.sha256((repo/'Sources/HUDSourceWatchView.swift').read_bytes()).hexdigest(),'telemetrySourceSHA256':hashlib.sha256((repo/'Sources/TelemetryCanvases.swift').read_bytes()).hexdigest()},ensure_ascii=False,separators=(',',':')))
PY
xcrun swiftc -parse-as-library -swift-version 5 -O -sdk /Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk -module-cache-path "$repo/build/windows-module-reference/.compiler/module-cache" -framework Cocoa "$repo/windows/tools/desktop_content_reference.swift" -o "$out/reference"
"$out/reference" "$out/desktop-content-source.json" "$out/input.json"
