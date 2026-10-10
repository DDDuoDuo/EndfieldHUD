#!/usr/bin/env python3
"""Bounded actual-CA probe of original nested marker screen/group semantics."""
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'build/map-pin-style-reference'
out.mkdir(parents=True,exist_ok=True)
authority='ca04f142185c7de40acd8523bdb563195d90a1d1'
source=root/'Sources/WorldMapCanvas.swift'
assert source.read_bytes()==subprocess.check_output(['git','show',f'{authority}:Sources/WorldMapCanvas.swift'],cwd=root)
subprocess.run(['/usr/bin/swiftc','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(root/'windows/tools/map_pin_style_reference.swift'),'-o',str(out/'oracle')],check=True)
subprocess.run([str(out/'oracle'),str(out)],check=True)
report=json.loads((out/'output.json').read_text());report.update(sourceCommit=authority,sourcePins={'Sources/WorldMapCanvas.swift':hashlib.sha256(source.read_bytes()).hexdigest()})
def premul(v):return [v[k]*v[3] for k in range(3)]+[v[3]]
def over(s,d,screen=False):return [s[k]+d[k]*(1-(s[k] if screen else s[3])) for k in range(3)]+[s[3]+d[3]*(1-s[3])]
maximum=0
for row in report['rows']:
    marker=[0,0,0,0]
    for n,value in enumerate(row['colors']):marker=over(premul(value),marker,n==row['screenIndex'])
    result=over([v*row['markerOpacity'] for v in marker],premul(row['background']))
    delta=max(abs(round(result[k]*255)-row['bgra'][[2,1,0,3][k]])for k in range(4));maximum=max(maximum,delta)
assert maximum<=1
report['isolatedMarkerMaximumByteResidual']=maximum
(root/'windows/tests/fixtures/map-pin-style-source.json').write_text(json.dumps(report,separators=(',',':'),sort_keys=True)+'\n')
print(f'{len(report["rows"])} nested original marker cases; isolated source-over residual{maximum}/255')
