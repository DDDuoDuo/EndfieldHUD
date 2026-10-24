#!/usr/bin/env python3
"""Reproduce Map's actual CA screen blend into an owned 8x8 Metal texture."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'build/map-screen-reference'
fixture=root/'windows/tests/fixtures/map-screen-source.json'
source=root/'Sources/WorldMapSurface.swift'
assert 'beam.compositingFilter = "screenBlendMode"; beam.opacity = 0.72' in source.read_text()
out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env.pop('SDKROOT',None)
sdk=subprocess.check_output(['bash',str(root/'windows/tools/mac_sdk.sh')],text=True,env=env).strip()
subprocess.run(['xcrun','swiftc','-sdk',sdk,'-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(root/'windows/tools/map_screen_reference.swift'),'-o',str(out/'oracle')],check=True,env=env)
subprocess.run([str(out/'oracle'),str(out)],check=True)
actual=json.loads((out/'output.json').read_text());actual['sourceSHA256']=hashlib.sha256(source.read_bytes()).hexdigest()
actual['authority']=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()+'/Sources/WorldMapSurface.swift'
(out/'fixture-candidate.json').write_text(json.dumps(actual,separators=(',',':'),sort_keys=True)+'\n')
# The compositing results must not change when the source moves (1.3.0:
# WorldMapSurface draws the beam); the provenance follows the source.
if fixture.exists():
    expected=json.loads(fixture.read_text())
    assert actual['rows']==expected['rows'],'Original CA pixel oracle changed'
    print(f"Verified {len(actual['rows'])} source screen/group cases")
fixture.write_text(json.dumps(actual,separators=(',',':'),sort_keys=True)+'\n')
