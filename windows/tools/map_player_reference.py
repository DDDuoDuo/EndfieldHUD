#!/usr/bin/env python3
"""Export the original player CGImages (WorldMapPinArtwork) for native/map_assets.

The redesigned Map fades with the shared centre backdrop ramp
(modules/hud_design_nodes.hpp centreBackdropMask): v1.2.0's 9-unit feather
mask is no longer exported."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'build/map-player-reference'
bundle=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else None
authority=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
source_files=['Sources/WorldMapPinArtwork.swift','Sources/WorldMapSurface.swift','Sources/WorldMapGeometry.swift']
assets=[('halo','WatchSource/Scene/Domain/sprites/deco_readio_mask--2444265073359569955.png'),('beam','WatchSource/Scene/Domain/textures/T_fx_mask_02_M--4275033587688225551.png'),('glyph','WatchSource/Scene/Domain/sprites/icon_char---2308601083109874541.png')]
def digest(data):return hashlib.sha256(data).hexdigest()
for path in source_files+[f'Resources/{p}' for _,p in assets]:
    original=subprocess.check_output(['git','show',f'{authority}:{path}'],cwd=root)
    assert (root/path).read_bytes()==original,f'Authority differs: {path}'
out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env.pop('SDKROOT',None)
sdk=subprocess.check_output(['bash',str(root/'windows/tools/mac_sdk.sh')],text=True,env=env).strip()
subprocess.run(['/usr/bin/swiftc','-sdk',sdk,'-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(root/source_files[0]),str(root/'windows/tools/map_player_reference.swift'),'-o',str(out/'oracle')],check=True,env=env)
subprocess.run([str(out/'oracle'),str(root/'Resources'),str(out)],check=True)
report=json.loads((out/'output.json').read_text())
report.update(format='endfield-map-player-assets',schemaVersion=1,sourceCommit=authority,pixelFormat='straight-RGBA8-sRGB-top-left',sourcePins={p:digest((root/p).read_bytes()) for p in source_files+[f'Resources/{p}' for _,p in assets]})
for image,(name,resource) in zip(report['images'],assets):
    assert image['name']==name
    image.update(file=name+'.rgba',resource=resource,sha256=digest((out/(name+'.rgba')).read_bytes()))
data=(json.dumps(report,separators=(',',':'),sort_keys=True)+'\n').encode()
(out/'map-player-assets.json').write_bytes(data)
if bundle:
    bundle.mkdir(parents=True,exist_ok=True)
    for name in ['map-player-assets.json']+[n+'.rgba' for n,_ in assets]:
        dest=bundle/name;value=(out/name).read_bytes()
        if dest.exists():assert dest.read_bytes()==value,f'Existing immutable bundle differs: {dest}'
        else:dest.write_bytes(value)
print('Manifest SHA256',digest(data))
