#!/usr/bin/env python3
"""Detached original Canvas paint commands with original highlight/image assets.

Reuses only the existing synthetic operation program, never its expected rows.
All executable Canvas, viewport and feedback code comes from the pinned source.
"""
from pathlib import Path
import ast,hashlib,json,subprocess
root=Path(__file__).resolve().parents[2]
commit='ca04f142185c7de40acd8523bdb563195d90a1d1'
out=root/'build/media-assembly-artwork-reference';out.mkdir(exist_ok=True)
program=ast.parse((root/'windows/tools/media_assembly_presentation_reference.py').read_text())
values={n.targets[0].id:ast.literal_eval(n.value) for n in program.body if isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('stubs','driver')}
names=['MediaAssemblyModel.swift','MediaAssemblyViewport.swift','MediaAssemblyCanvas.swift','MediaAssemblyControls.swift','HUDControlHighlightLayer.swift']
source={n:subprocess.check_output(['git','show',commit+':Sources/'+n],cwd=root).decode() for n in names}
stubs=values['stubs'];start=stubs.index('enum MediaAssemblyAssetCatalog');end=stubs.index('struct MediaAssemblyDocument',start)
stubs=stubs[:start]+r'''
import ImageIO
enum MediaAssemblyAssetCatalog {
 static var names:[ObjectIdentifier:String]=[:],images:[String:CGImage]=[:]
 static func image(_ path:String)->CGImage {
  if let image=images[path] {return image}
  let url=URL(fileURLWithPath:CommandLine.arguments[2]).appendingPathComponent(path)
  let src=CGImageSourceCreateWithURL(url as CFURL,nil)!,image=CGImageSourceCreateImageAtIndex(src,0,nil)!
  images[path]=image;names[ObjectIdentifier(image)]=path;return image
 }
 static func filterThumbnail(_ x:MediaAssemblyFilter)->CGImage? {x == .none ? nil:image("filter-icons/"+x.rawValue+".png")}
 static func stickerThumbnail(_ x:MediaAssemblyStickerKind)->CGImage? {image("stickers/"+x.rawValue+".png")}
 static func stickerImage(_ x:MediaAssemblyStickerKind)->CGImage? {image("stickers/"+x.rawValue+".png")}
}
'''+stubs[end:]
model=source[names[0]].split('struct MediaAssemblyDocument {')[0]
parameter=source[names[3]].split('/// Source/export')[0]
(out/'stubs.swift').write_text(stubs+model+parameter)
for src,dest in [('MediaAssemblyCanvas.swift','canvas.swift'),('MediaAssemblyViewport.swift','viewport.swift'),('HUDControlHighlightLayer.swift','feedback.swift')]:
    (out/dest).write_text(source[src])
serializer=r'''
func paint(_ root:CALayer)->[[String:Any]] {
 var out:[[String:Any]]=[]
 func color(_ value:CGColor?)->Any {guard let value else {return NSNull()};return value.converted(to:CGColorSpace(name:CGColorSpace.sRGB)!,intent:.defaultIntent,options:nil)!.components!}
 func visit(_ node:CALayer,_ alpha:Float) {
  guard !node.isHidden else{return};let opacity=alpha*node.opacity
  func point(_ p:CGPoint)->[Double]{let q=node.convert(p,to:root);return [q.x,q.y]}
  let b=node.bounds,corners=[CGPoint(x:b.minX,y:b.minY),CGPoint(x:b.maxX,y:b.minY),CGPoint(x:b.maxX,y:b.maxY),CGPoint(x:b.minX,y:b.maxY)].map(point)
  let extent=b.width>0 && b.height>0
  if extent && (node.backgroundColor != nil || node.borderWidth>0) {out.append(["kind":"plate","corners":corners,"opacity":opacity,"color":color(node.backgroundColor),"radius":node.cornerRadius,"borderWidth":node.borderWidth,"border":node.borderWidth>0 ? color(node.borderColor):NSNull()])}
  if let shape=node as? CAShapeLayer,let path=shape.path {
   var commands:[[String:Any]]=[];path.applyWithBlock {pointer in let e=pointer.pointee;let op:String,n:Int;switch e.type{case .moveToPoint:op="move";n=1;case .addLineToPoint:op="line";n=1;case .addQuadCurveToPoint:op="quadratic";n=2;case .addCurveToPoint:op="cubic";n=3;case .closeSubpath:op="close";n=0;@unknown default:fatalError()};commands.append(["op":op,"points":(0..<n).map{point(e.points[$0])}])}
   out.append(["kind":"shape","path":commands,"opacity":opacity,"fill":color(shape.fillColor),"stroke":color(shape.strokeColor),"width":shape.lineWidth,"rule":shape.fillRule.rawValue,"cap":shape.lineCap.rawValue,"join":shape.lineJoin.rawValue])
  }else if let text=node as? CATextLayer,extent {
   out.append(["kind":"text","corners":corners,"opacity":opacity,"string":text.string as? String ?? "","size":text.fontSize,"font":(text.font as? NSFont)?.fontName ?? "","color":color(text.foregroundColor),"alignment":text.alignmentMode.rawValue,"wrapped":text.isWrapped])
  }else if let raw=node.contents,extent,CFGetTypeID(raw as CFTypeRef)==CGImage.typeID {
   let image=raw as! CGImage;out.append(["kind":"image","corners":corners,"opacity":opacity,"asset":MediaAssemblyAssetCatalog.names[ObjectIdentifier(image)]!])
  }
  node.sublayers?.forEach{visit($0,opacity)}
 }
 root.sublayers?.forEach{visit($0,1)};return out
}
'''
driver=values['driver'].replace('import AppKit\n','',1)
driver=driver.replace('HUDModuleContentStyle())','HUDModuleContentStyle(dark:dark))',1)
driver=driver.replace('rows.append(["operation":op,"state":snapshot()])','rows.append(["dark":dark,"operation":op,"state":snapshot(),"art":paint(p.layer)])')
driver=driver[:driver.index('let data=try JSONSerialization')]
main='import AppKit\n'+serializer+'\nfunc run(_ dark:Bool)->[[String:Any]] {\n'+driver+'return rows\n}\n'+r'''
let rows=run(true)+run(false)
let value:[String:Any] = ["cases":rows,"buttonHeight":ceil(NSFont.systemFont(ofSize:10,weight:.semibold).ascender-NSFont.systemFont(ofSize:10,weight:.semibold).descender+NSFont.systemFont(ofSize:10,weight:.semibold).leading)]
try JSONSerialization.data(withJSONObject:value,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
'''
(out/'main.swift').write_text(main)
subprocess.run(['xcrun','swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),*[str(out/n) for n in ('stubs.swift','viewport.swift','canvas.swift','feedback.swift','main.swift')],'-o',str(out/'oracle')],check=True)
subprocess.run([str(out/'oracle'),str(out/'raw.json'),str(root/'windows/resources/media-assembly')],check=True)
value=json.loads((out/'raw.json').read_text());value['sourceCommit']=commit;value['sourcePins']={k:hashlib.sha256(v.encode()).hexdigest() for k,v in source.items()};value['scope']='Actual source Canvas paint leaves, original feedback and packaged assets, both themes. Inert controller only; no windows, player, user data or decoder service.'
# Intern repeated source leaves rather than shipping 128 duplicate layer trees.
leaves=[];intern={}
for row in value['cases']:
    indices=[]
    for leaf in row['art']:
        key=json.dumps(leaf,ensure_ascii=False,sort_keys=True,separators=(',',':'))
        if key not in intern:intern[key]=len(leaves);leaves.append(leaf)
        indices.append(intern[key])
    row['art']=indices
value['paintLeaves']=leaves
target=root/'windows/tests/fixtures/media-assembly-artwork-source.json';target.write_text(json.dumps(value,ensure_ascii=False,separators=(',',':'))+'\n');print(target,len(target.read_bytes()))
