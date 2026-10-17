#!/usr/bin/env python3
"""Small original-Swift Map math oracle; no app, window, store or real data.

Compiles unchanged WorldMapGeometry.swift and WorldMapStore.swift. The store is
never constructed. Reuses the existing shared source-oracle module cache.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'build/map-reference'
output.mkdir(parents=True, exist_ok=True)
source_paths = ['Sources/WorldMapStore.swift', 'Sources/WorldMapGeometry.swift',
                'Sources/WorldMapRasterController.swift', 'Sources/WorldMapRasterPainter.swift']
def method(text, start):
    at=text.index(start); brace=text.index('{',at); depth=1; end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[at:end]
controller=(root/source_paths[2]).read_text()
placement=method(controller,'static func placement(')
needs=method(controller,'private var needsPaint:').replace('private var','var',1)
painter=(root/source_paths[3]).read_text()
geometry=painter[painter.index('        let viewport = WorldMapGeometry.constrained'):painter.index('        let started = ProcessInfo.processInfo.systemUptime')]
helpers='''
struct WorldMapRasterFrame {let viewport:WorldMapViewport;let screenRect:CGRect;let pixelsPerPoint:CGFloat}
struct WorldMapRasterRequest {let viewport:WorldMapViewport;let contentsScale:CGFloat;let padding:CGFloat}
final class OriginalRasterController {
 var frame:WorldMapRasterFrame?;var frameRevision=1;var revision=1;var interacting=false;var exactRequested=false;var contentsScale:CGFloat=2;var viewport=WorldMapViewport()
'''+placement+'\n'+needs+'''
}
final class OriginalPainter {
 static let maximumPixelDimension=1536
 static func geometry(_ request:WorldMapRasterRequest)->[String:Any] {
'''+geometry+'''
 return ["screen":[screen.minX,screen.minY,screen.width,screen.height],"world":[worldRect.minX,worldRect.minY,worldRect.width,worldRect.height],"origin":[origin.x,origin.y],"pixels":pixels,"pixelScale":pixelScale,"factor":factor,"tolerance":tolerance]
 }
}
'''
inputs = []
for camera in [[(114.0579+180)/360, (90-22.5431)/180, 3],
               [0, .5, 2.1], [.99999, .98, 128], [.5, .01, 72],
               [-3.125, 1.1, 1], [4.5, -.5, 1e300]]:
    for point, delta, factor in [([220,220],[0,0],1), ([4,220],[32,-19],1.3),
                                 ([436,220],[-650,500],1/1.3),
                                 ([50,60],[1.125,.5],.5), ([350,370],[-.5,-1.25],2),
                                 ([220,4],[1e4,-1e4],.001), ([220,436],[100,-100],1e3),
                                 ([437,220],[0,0],0), (['nan',220],['nan',0],'nan')]:
        inputs.append({'camera': camera, 'point': point, 'delta': delta, 'factor': factor})
(output/'input.json').write_text(json.dumps(inputs))
(output/'main.swift').write_text('''import Foundation
import CoreGraphics
'''+helpers+'''
func number(_ v:Any)->Double{if let n=v as? NSNumber{return n.doubleValue};return .nan}
func point(_ v:[Any])->CGPoint{CGPoint(x:number(v[0]),y:number(v[1]))}
func camera(_ v:[Any])->WorldMapViewport{WorldMapViewport(centerX:number(v[0]),centerY:number(v[1]),zoom:number(v[2]))}
func encode(_ v:WorldMapViewport)->[Double]{[v.centerX,v.centerY,v.zoom]}
func encode(_ v:CGPoint)->[Double]{[v.x,v.y]}
let rows=try JSONSerialization.jsonObject(with:Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1]))) as! [[String:Any]]
var result:[[String:Any]]=[]
var raster:[[String:Any]]=[]
for row in rows {
 let original=camera(row["camera"] as! [Any]);let v=WorldMapGeometry.constrained(original)
 let p=point(row["point"] as! [Any]);let delta=point(row["delta"] as! [Any])
 var r=row;r["constrained"]=encode(v);r["contains"]=WorldMapGeometry.contains(p)
 r["panned"]=encode(WorldMapGeometry.panned(v,delta:delta))
 r["zoomed"]=encode(WorldMapGeometry.zoomed(v,at:p,factor:number(row["factor"]!)))
 r["recentered"]=encode(WorldMapGeometry.recentered(v,at:p))
 if p.x.isFinite && p.y.isFinite {let w=WorldMapGeometry.world(at:p,viewport:v);r["world"]=encode(w);r["screen"]=encode(WorldMapGeometry.screen(x:w.x,y:w.y,viewport:v))}
 r["description"]=WorldMapGeometry.coordinateDescription(x:v.centerX,y:v.centerY)
 result.append(r)
 let rect=CGRect(x:-128,y:-128,width:696,height:696)
 let frame=WorldMapRasterFrame(viewport:v,screenRect:rect,pixelsPerPoint:1.4)
 let target=WorldMapGeometry.zoomed(v,at:CGPoint(x:280,y:250),factor:1.12)
 let placement=OriginalRasterController.placement(of:frame,in:target)
 let controller=OriginalRasterController();controller.frame=frame;controller.viewport=target
 var decisions:[Bool]=[]
 for same in [false,true] {for interacting in [false,true] {for exact in [false,true] {controller.frameRevision=same ? 1 : 0;controller.interacting=interacting;controller.exactRequested=exact;decisions.append(controller.needsPaint)}}}
 raster.append(["camera":encode(v),"target":encode(target),"placement":[placement.position.x,placement.position.y,placement.scale],"geometry":OriginalPainter.geometry(WorldMapRasterRequest(viewport:v,contentsScale:2.2,padding:128)),"needsPaint":decisions])
}
let rect=CGRect(x:26,y:184,width:22,height:22)
let edges=[[26.0,184.0],[48,184],[26,206],[48,206],[47.999,205.999]].map { ["point":$0,"contains":rect.contains(CGPoint(x:$0[0],y:$0[1]))] as [String:Any] }
let intersections=[[48.0,184.0,18.0,18.0],[48.001,184,18,18],[25,206,1,1]].map { ["rect":$0,"intersects":rect.intersects(CGRect(x:$0[0],y:$0[1],width:$0[2],height:$0[3]))] as [String:Any] }
let boundaryCamera=WorldMapViewport(centerX:0.5,centerY:0.5,zoom:163.0/55)
let boundaryPoint=WorldMapGeometry.screen(x:0.375,y:0.5,viewport:boundaryCamera)
let boundaryRect=CGRect(x:boundaryPoint.x-9,y:boundaryPoint.y-9,width:18,height:18)
let boundary:[String:Any]=["camera":encode(boundaryCamera),"point":encode(boundaryPoint),"intersects":CGRect(x:26,y:212,width:22,height:22).intersects(boundaryRect)]
try JSONSerialization.data(withJSONObject:["pinBoundary":boundary,"geometry":result,"raster":raster,"rectEdges":edges,"intersections":intersections],options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
''')
env = os.environ.copy()
env.pop('SDKROOT', None)
sdk = subprocess.check_output(['bash', str(root/'scripts/build.sh'), '--print-sdk'], env=env, text=True).strip()
cache = root/'build/windows-module-reference/.compiler/module-cache'
subprocess.run(['xcrun', 'swiftc', '-sdk', sdk, '-module-cache-path', str(cache),
                *[str(root/p) for p in source_paths[:2]], str(output/'main.swift'), '-o', str(output/'oracle')], check=True)
subprocess.run([str(output/'oracle'), str(output/'input.json'), str(output/'output.json')], check=True)
data = json.loads((output/'output.json').read_text())
data['authority'] = 'ca04f142185c7de40acd8523bdb563195d90a1d1'
data['sourceSHA256'] = {p: hashlib.sha256((root/p).read_bytes()).hexdigest() for p in source_paths}
data['scope'] = 'Unchanged Foundation map geometry plus extracted original placement/needsPaint/painter allocation math. No window, store initialization, file-provider or geographic raster.'
fixture = root/'windows/tests/fixtures/map-source.json'
fixture.write_text(json.dumps(data, ensure_ascii=False, separators=(',', ':'))+'\n')
print(f'Original Map geometry: {len(inputs)} cases; fixture {fixture.stat().st_size} bytes')
