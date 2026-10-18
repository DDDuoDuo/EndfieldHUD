#!/usr/bin/env python3
"""Sample exact source animation declarations on detached synthetic CA layers."""
from pathlib import Path
import hashlib,json,re,subprocess
root=Path(__file__).resolve().parents[2]
out=root/'build/media-assembly-scene-local';out.mkdir(parents=True,exist_ok=True)
source=(root/'Sources/MediaAssemblyCanvas.swift').read_text()
if hashlib.sha256(source.encode()).hexdigest()!='1e4c1d49df0e0ea2f487fda874f2d7290f42ed398a6d30b21a633d62cba2e8af':
 raise SystemExit('Original Media Assembly Canvas changed; review its animation declarations first')
def declaration(method,variable):
 start=source.index(method);begin=source.index('let '+variable+' = CABasicAnimation(',start)
 end=source.index('\n',begin)
 line=source[begin:end].strip()
 return line[:line.rfind(';')] if '.add(' in line else line
# The actual unchanged declarations are copied; only the layer receiving them
# and synthetic scroll endpoints are supplied by this isolated harness.
drawer=declaration('private func animateDrawer()', 'fade')
close=declaration('private func closeMedia()', 'fade')
scroll=declaration('private func updateInlineScroll(', 'animation')
swift='''import AppKit
import QuartzCore
import Metal
func originalDrawer()->CABasicAnimation {\n'''+drawer+''';return fade}
func originalClose()->CABasicAnimation {\n'''+close+''';return fade}
func originalScroll()->CABasicAnimation {let previous=CGPoint(x:0,y:-13.25),drawerContent=CALayer();drawerContent.position=CGPoint(x:0,y:-217.125)
'''+scroll+''';return animation}
let originals=[originalDrawer(),originalClose(),originalScroll()]
let root=CALayer();root.frame=CGRect(x:0,y:0,width:8,height:8)
var probes:[(Int,Double,CALayer)]=[]
CATransaction.begin();CATransaction.setDisableActions(true)
for (kind,original) in originals.enumerated() {for phase in (0...100).map({Double($0)/100})+[0.0037,0.1234567,0.54321,0.87654321] {
 let layer=CALayer();layer.frame=root.bounds;layer.opacity=kind==1 ? 0:1;layer.speed=0;layer.timeOffset=1+phase*original.duration;root.addSublayer(layer)
 let animation=original.copy() as! CABasicAnimation;animation.beginTime=1;animation.fillMode = .both;animation.isRemovedOnCompletion=false;layer.add(animation,forKey:"source");probes.append((kind,phase,layer))
}}
CATransaction.commit()
let device=MTLCreateSystemDefaultDevice()!,queue=device.makeCommandQueue()!
let descriptor=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:8,height:8,mipmapped:false);descriptor.storageMode = .shared;descriptor.usage=[.renderTarget,.shaderRead]
let texture=device.makeTexture(descriptor:descriptor)!,renderer=CARenderer(mtlTexture:texture,options:[kCARendererMetalCommandQueue:queue]);renderer.layer=root;renderer.bounds=root.bounds
CATransaction.flush();renderer.beginFrame(atTime:0,timeStamp:nil);renderer.addUpdate(root.bounds);renderer.render();renderer.endFrame();let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted()
let rows=probes.map{kind,phase,layer->[String:Any] in guard let actual=layer.presentation() else{fatalError("No original CA presentation")};return ["kind":kind,"phase":phase,"value":kind==2 ? -actual.position.y:Double(actual.opacity)]}
let output:[String:Any]=["version":1,"usesAppOrWindow":false,"durations":originals.map{$0.duration},"rows":rows]
try JSONSerialization.data(withJSONObject:output,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
'''
(out/'motion.swift').write_text(swift)
subprocess.run(['xcrun','swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(out/'motion.swift'),'-o',str(out/'motion-reference')],check=True)
subprocess.run([str(out/'motion-reference'),str(out/'motion-raw.json')],check=True)
value=json.loads((out/'motion-raw.json').read_text());value['sourceCommit']='ca04f142185c7de40acd8523bdb563195d90a1d1';value['sourceSHA256']=hashlib.sha256(source.encode()).hexdigest();value['declarations']=[drawer,close,scroll]
target=root/'windows/tests/fixtures/media-assembly-motion-source.json';target.write_text(json.dumps(value,separators=(',',':'))+'\n');print(target)
