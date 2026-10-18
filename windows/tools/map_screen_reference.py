#!/usr/bin/env python3
"""Reproduce Map's actual CA screen blend into an owned 8x8 Metal texture."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'build/map-screen-reference'
fixture=root/'windows/tests/fixtures/map-screen-source.json'
source=root/'Sources/WorldMapCanvas.swift'
assert 'beam.compositingFilter = "screenBlendMode"; beam.opacity = 0.72' in source.read_text()
if fixture.exists():
    expected=json.loads(fixture.read_text())
    assert expected['sourceSHA256']==hashlib.sha256(source.read_bytes()).hexdigest()
out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env.pop('SDKROOT',None)
sdk=subprocess.check_output(['bash',str(root/'scripts/build.sh'),'--print-sdk'],text=True,env=env).strip()
subprocess.run(['xcrun','swiftc','-sdk',sdk,'-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(root/'windows/tools/map_screen_reference.swift'),'-o',str(out/'oracle')],check=True,env=env)
subprocess.run([str(out/'oracle'),str(out)],check=True)
actual=json.loads((out/'output.json').read_text());actual['sourceSHA256']=hashlib.sha256(source.read_bytes()).hexdigest()
actual['authority']='ca04f142185c7de40acd8523bdb563195d90a1d1/Sources/WorldMapCanvas.swift'
(out/'fixture-candidate.json').write_text(json.dumps(actual,separators=(',',':'),sort_keys=True)+'\n')
if fixture.exists():
    assert actual==expected,'Original CA pixel oracle changed'
    print(f"Verified {len(actual['rows'])} source screen/group cases")
