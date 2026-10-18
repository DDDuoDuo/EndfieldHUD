#!/usr/bin/env python3
"""Export original player CGImages and query the original detached radial mask."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'build/map-player-reference'
bundle=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else None
authority='ca04f142185c7de40acd8523bdb563195d90a1d1'
source_files=['Sources/WorldMapPinArtwork.swift','Sources/WorldMapCanvas.swift','Sources/WorldMapGeometry.swift']
assets=[('halo','WatchSource/Scene/Domain/sprites/deco_readio_mask--2444265073359569955.png'),('beam','WatchSource/Scene/Domain/textures/T_fx_mask_02_M--4275033587688225551.png'),('glyph','WatchSource/Scene/Domain/sprites/icon_char---2308601083109874541.png')]
def digest(data):return hashlib.sha256(data).hexdigest()
for path in source_files+[f'Resources/{p}' for _,p in assets]:
    original=subprocess.check_output(['git','show',f'{authority}:{path}'],cwd=root)
    assert (root/path).read_bytes()==original,f'Authority differs: {path}'
out.mkdir(parents=True,exist_ok=True)
canvas=(root/source_files[1]).read_text();geometry=(root/source_files[2]).read_text()
mask=canvas[canvas.index('        let mask = CAGradientLayer();'):canvas.index('        layer.mask = mask')]
constants=geometry[geometry.index('    static let size'):geometry.index('    static func constrained')]
(out/'source_mask.swift').write_text('import AppKit\nimport QuartzCore\nenum WorldMapGeometry {\n'+constants+'}\nfunc sourceMapMask()->CAGradientLayer {\nlet layer=CALayer();layer.bounds=CGRect(origin:.zero,size:WorldMapGeometry.size)\n'+mask+'\nreturn mask\n}\n')
env=os.environ.copy();env.pop('SDKROOT',None)
sdk=subprocess.check_output(['bash',str(root/'scripts/build.sh'),'--print-sdk'],text=True,env=env).strip()
subprocess.run(['/usr/bin/swiftc','-sdk',sdk,'-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(root/source_files[0]),str(root/'windows/tools/map_player_reference.swift'),str(out/'source_mask.swift'),'-o',str(out/'oracle')],check=True,env=env)
subprocess.run([str(out/'oracle'),str(root/'Resources'),str(out)],check=True)
report=json.loads((out/'output.json').read_text())
report.update(format='endfield-map-player-assets',schemaVersion=1,sourceCommit=authority,pixelFormat='straight-RGBA8-sRGB-top-left',sourcePins={p:digest((root/p).read_bytes()) for p in source_files+[f'Resources/{p}' for _,p in assets]})
for image,(name,resource) in zip(report['images'],assets):
    assert image['name']==name
    image.update(file=name+'.rgba',resource=resource,sha256=digest((out/(name+'.rgba')).read_bytes()))
alpha=(out/'feather.alpha').read_bytes();assert len(alpha)==880*880
packed=bytearray();at=0
while at<len(alpha):
    end=at+1
    while end<len(alpha) and end-at<65535 and alpha[end]==alpha[at]:end+=1
    packed.extend((end-at).to_bytes(2,'little'));packed.append(alpha[at]);at=end
(out/'feather.alpha-rle').write_bytes(packed)
report['feather']={'file':'feather.alpha-rle','encoding':'u16le-count-u8-alpha','width':880,'height':880,'scale':2,'bytes':len(packed),'sha256':digest(packed),'decodedSHA256':digest(alpha)}
data=(json.dumps(report,separators=(',',':'),sort_keys=True)+'\n').encode()
(out/'map-player-assets.json').write_bytes(data)
if bundle:
    bundle.mkdir(parents=True,exist_ok=True)
    for name in ['map-player-assets.json','feather.alpha-rle']+[n+'.rgba' for n,_ in assets]:
        dest=bundle/name;value=(out/name).read_bytes()
        if dest.exists():assert dest.read_bytes()==value,f'Existing immutable bundle differs: {dest}'
        else:dest.write_bytes(value)
print('Manifest SHA256',digest(data))
for frame in report['maskProof']['frames']:print('Radial mask',frame['scale'],'x maximum residual',frame['maximumByteResidual'],'byte levels; >1:',frame['pixelsOverOneByte'])
