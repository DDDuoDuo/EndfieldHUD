#!/usr/bin/env python3
"""Reuse the bounded geometric fitter with original Shortcut CABasic paths.
Only detached CARenderer layers; never NSApplication, window, defaults or data.
Run app_shortcut_motion_reference.py first to extract the authoritative block.
"""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[2];out=root/'build/app-shortcut-reference'
original=(out/'motion.swift').read_text().split('func matrix(')[0]
base=(root/'windows/tools/subsection_mask_reference.swift').read_text()
head=base[:base.index('private struct Probe')].replace('CGRect(x: 9,y: 40,width: 382,height: 248)','CGRect(x: 0,y: 0,width: 400,height: 334)')
body=base[base.index('// A Bezier\'s position'):].replace('[9.0,40.0,382.0,248.0,0.26]','[0.0,0.0,400.0,334.0,0.26]').replace('[9,40,382,248]','[0,0,400,334]').replace('CommandLine.arguments.count==2','CommandLine.arguments.count==3').replace('"own nonactivating offscreen layers only"','"own detached CARenderer layers; no NSApplication/window"')
oracle=r'''
private struct Probe {let key:SampleKey;let content:CALayer;let mask:CAShapeLayer}
private final class Oracle {
 let queue:MTLCommandQueue;let target:MTLTexture;let root=CALayer();let incoming=CommandLine.arguments[2]=="arrival"
 private(set)var samples:[SampleKey:PathData]=[:],maximumRepeatDifference=0.0
 init()throws{let device=MTLCreateSystemDefaultDevice()!;queue=device.makeCommandQueue()!;let d=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:8,height:8,mipmapped:false);d.storageMode = .shared;d.usage=[.renderTarget,.shaderRead];target=device.makeTexture(descriptor:d)!;root.frame=CGRect(x:0,y:0,width:8,height:8)}
 func draw(){let r=CARenderer(mtlTexture:target,options:[kCARendererMetalCommandQueue:queue]);r.layer=root;r.bounds=root.bounds;CATransaction.flush();r.beginFrame(atTime:0,timeStamp:nil);r.addUpdate(root.bounds);r.render();r.endFrame();let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted()}
 func load(_ requested:[SampleKey])throws{let missing=Array(Set(requested.filter{samples[$0]==nil})).sorted{$0.direction==$1.direction ? $0.phase<$1.phase : $0.direction<$1.direction};try need(samples.count+missing.count<=maximumSamples,"Source sample budget reached")
  for first in stride(from:0,to:missing.count,by:128){try withinBudget();var probes:[Probe]=[];CATransaction.begin();CATransaction.setDisableActions(true)
   for key in missing[first..<min(first+128,missing.count)]{let original=OriginalShortcutMotion(CGFloat(key.direction));let authored=incoming ? original.reveal : original.retract;let content=CALayer();content.bounds=viewport;content.speed=0;content.timeOffset=1+key.phase*0.26;root.addSublayer(content);let mask=CAShapeLayer();mask.bounds=viewport;mask.path=authored.path;content.mask=mask;let animation=authored.animation(forKey:"apps.transition.shutter")!.copy() as! CABasicAnimation;animation.beginTime=1;animation.fillMode = .both;animation.isRemovedOnCompletion=false;mask.add(animation,forKey:"mask");probes.append(Probe(key:key,content:content,mask:mask))}
   CATransaction.commit();draw();var values:[PathData]=[];for p in probes{guard let path=p.mask.presentation()?.path else{fatalError("Missing actual CA path")};values.append(try PathData(path))};draw()
   for(n,p)in probes.enumerated(){guard let path=p.mask.presentation()?.path else{fatalError("Missing repeated CA path")};let repeated=try PathData(path);try need(repeated.opcodes==values[n].opcodes,"Unstable actual CA topology");for(a,b)in zip(repeated.coordinates,values[n].coordinates){maximumRepeatDifference=max(maximumRepeatDifference,abs(a-b))};samples[p.key]=values[n];p.content.removeFromSuperlayer()}
  }
 }
 func get(_ key:SampleKey)->PathData{samples[key]!}
}
'''
# Keep the original geometric difference-curve and true CGPath containment
# validation unchanged. This substitution changes only the oracle and viewport.
(out/'mask.swift').write_text(original+head+oracle+body)
subprocess.run(['xcrun','swiftc','-O','-parse-as-library','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(out/'mask.swift'),'-o',str(out/'mask-reference')],check=True)
for kind in ['arrival','departure']:
 dest=out/('mask-'+kind);dest.mkdir(exist_ok=True)
 if (dest/'report.json').exists():raise RuntimeError('Use fresh mask output; preserve previous oracle evidence')
 subprocess.run([str(out/'mask-reference'),str(dest),kind],check=True)
 v=json.loads((dest/'report.json').read_text());v['sourceKind']='AppShortcutCanvas.'+kind;v['sourceSHA256']=hashlib.sha256((root/'Sources/AppShortcutCanvas.swift').read_bytes()).hexdigest();v['fitterSHA256']=hashlib.sha256(base.encode()).hexdigest();v['candidateSHA256']=hashlib.sha256((dest/'candidate.bin').read_bytes()).hexdigest() if (dest/'candidate.bin').exists() else None
 (dest/'report.json').write_text(json.dumps(v,indent=2)+'\n');print(kind,v.get('status'),v.get('maximumHoldoutBoundaryUpperBound'),v.get('candidateSHA256'))
