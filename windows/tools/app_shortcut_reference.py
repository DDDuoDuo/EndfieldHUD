#!/usr/bin/env python3
"""Read-only original AppShortcutCanvas/Artwork with memory-only store fixtures."""
from pathlib import Path
import hashlib, json, subprocess

root=Path(__file__).resolve().parents[2]
out=root/'build/app-shortcut-reference'
out.mkdir(parents=True,exist_ok=True)
enum=(root/'Sources/AppShortcutStore.swift').read_text().split('struct AppShortcutCandidate')[0]
source=r'''
import AppKit
import QuartzCore
protocol HUDModuleContentFactory {}
enum HUDModule {case addApp}
struct HUDModuleContentStyle {let dark:Bool;let accent:NSColor;let contentsScale:CGFloat}
enum HUDResources {static func url(for path:String)->URL?{URL(fileURLWithPath:CommandLine.arguments[1]).appendingPathComponent(path)}}
enum L10n {static func text(_ e:String,_ z:String)->String{e}}
enum HUDSectionHeading {static func text(_ s:String)->String{"// "+s}}
enum HUDRuntimeAppearance {static let accent=NSColor(srgbRed:0.98,green:0.87,blue:0.13,alpha:1);static let reduceMotion=true}
struct AppShortcutCandidate {let url:URL;let name:String;let bundleIdentifier:String?;let icon:NSImage?}
struct AppShortcut:Equatable {let id:UUID;var name:String;var originalName:String;var iconPreset:AppShortcutIcon}
final class AppShortcutStore {
 var items:[AppShortcut];init(_ values:[AppShortcut]){items=values}
 func inspect(url:URL)throws->AppShortcutCandidate {AppShortcutCandidate(url:url,name:"Fixture Application",bundleIdentifier:"fixture.application",icon:nil)}
 func resolvedURL(for id:UUID)throws->URL {URL(fileURLWithPath:"/Synthetic/"+id.uuidString+".app")}
 func icon(for id:UUID)->NSImage? {nil}
 func remove(id:UUID)throws {items.removeAll{$0.id==id}}
 func save(candidate:AppShortcutCandidate,name:String,iconPreset:AppShortcutIcon,editingID:UUID?)throws->AppShortcut {let v=AppShortcut(id:editingID ?? UUID(uuidString:"00000000-0000-4000-8000-000000000999")!,name:name,originalName:candidate.name,iconPreset:iconPreset);items.removeAll{$0.id==v.id};items.append(v);return v}
}
func box(_ r:CGRect)->[CGFloat]{[r.minX,r.minY,r.width,r.height]}
func point(_ p:CGPoint)->[CGFloat]{[p.x,p.y]}
func color(_ c:CGColor?)->Any {guard let c else{return NSNull()};return ["sRGB":c.converted(to:CGColorSpace(name:CGColorSpace.sRGB)!,intent:.defaultIntent,options:nil)?.components ?? []]}
func path(_ p:CGPath?)->Any {guard let p else{return NSNull()};var out:[[String:Any]]=[];p.applyWithBlock{ptr in let e=ptr.pointee;let op:String,n:Int;switch e.type {case .moveToPoint:op="move";n=1;case .addLineToPoint:op="line";n=1;case .addQuadCurveToPoint:op="quadratic";n=2;case .addCurveToPoint:op="cubic";n=3;case .closeSubpath:op="close";n=0;@unknown default:fatalError()};out.append(["op":op,"points":(0..<n).map{point(e.points[$0])}])};return out}
func layer(_ l:CALayer)->[String:Any] {var o:[String:Any] = ["bounds":box(l.bounds),"frame":box(l.frame),"cornerRadius":l.cornerRadius,"borderWidth":l.borderWidth,"borderColor":color(l.borderColor),"backgroundColor":color(l.backgroundColor),"opacity":l.opacity,"hidden":l.isHidden,"masksToBounds":l.masksToBounds,"name":l.name ?? ""]
 if let s=l as? CAShapeLayer {o["shape"]=["path":path(s.path),"fillColor":color(s.fillColor),"strokeColor":color(s.strokeColor),"lineWidth":s.lineWidth,"lineCap":s.lineCap.rawValue,"lineJoin":s.lineJoin.rawValue]}
 if let t=l as? CATextLayer {o["text"]=["string":t.string as? String ?? "","fontSize":t.fontSize,"foregroundColor":color(t.foregroundColor),"alignment":t.alignmentMode.rawValue,"wrapped":t.isWrapped,"fontName":(t.font as? NSFont)?.fontName ?? ""]}
 if let c=l.contents {let image=c as! CGImage;o["imageSize"]=[image.width,image.height]}
 o["children"]=(l.sublayers ?? []).map(layer);return o
}
let values=(0..<10).map {AppShortcut(id:UUID(uuidString:String(format:"00000000-0000-4000-8000-%012d",$0+1))!,name:"Fixture \($0)",originalName:"Original \($0)",iconPreset:AppShortcutIcon.allCases[$0])}
var cases:[[String:Any]]=[]
for dark in [true,false] {
 let store=AppShortcutStore(values),canvas=AppShortcutCanvas(store:nil,reduceMotion:{true})
 _=canvas.makeContent(for:.addApp,style:HUDModuleContentStyle(dark:dark,accent:HUDRuntimeAppearance.accent,contentsScale:2));canvas.activate()
 func emit(_ name:String,_ c:AppShortcutCanvas) {cases.append(["name":name,"dark":dark,"layer":layer(c.layer),"scroll":c.scrollOffset,"editing":c.isEditing,"draftName":c.draftName,"draftIcon":c.draftIcon.rawValue,"actions":c.accessibleActions.map{["id":$0.id,"label":$0.label,"rect":box($0.rect)]}])}
 emit("empty",canvas)
 let list=AppShortcutCanvas(store:store,reduceMotion:{true});_=list.makeContent(for:.addApp,style:HUDModuleContentStyle(dark:dark,accent:HUDRuntimeAppearance.accent,contentsScale:2));list.activate();emit("list",list)
 for delta in [13.25,35,400,-10000] {_=list.scroll(at:CGPoint(x:50,y:100),delta:delta);emit("scroll\(delta)",list)}
 _=list.importURLs([URL(fileURLWithPath:"/Synthetic/New.app")]);emit("draft",list)
 for icon in AppShortcutIcon.allCases {list.perform(actionID:"apps:icon:"+icon.rawValue);emit("icon:"+icon.rawValue,list)}
 list.setDraftName("Local Name");list.setDropTarget(true);list.showError("Synthetic error");emit("draft-error-drop",list);list.deactivate()
}
var glyphs:[[String:Any]]=[]
for icon in AppShortcutIcon.allCases {var row:[String:Any] = ["id":icon.rawValue,"path":path(AppShortcutArtwork.path(for:icon,in:CGRect(x:0,y:0,width:28,height:28))),"title":icon.title]
 if let game=AppShortcutArtwork.gameIcon(for:icon) {row["sourceAsset"]="AppIconSources/EndfieldWiki/"+game.rawValue+".png"};glyphs.append(row)}
let pencil=HUDPencilArtwork.makeLayer(in:CGRect(x:0,y:0,width:16,height:16),color:.white,contentsScale:1)
var rounded:[String:Any]=[:]
for size in [(118,28),(304,27),(108,28),(136,28),(48,48),(26,26),(306,46)] {let rect=CGRect(x:0,y:0,width:size.0,height:size.1);let host=CALayer();let feedback=HUDControlHighlightLayer.add(to:host,rect:rect);rounded["\(size.0)x\(size.1)"]=path((feedback.sublayers![0] as! CAShapeLayer).path)}
rounded["390x266"]=path(CGPath(roundedRect:CGRect(x:0,y:0,width:390,height:266),cornerWidth:5,cornerHeight:5,transform:nil))
try JSONSerialization.data(withJSONObject:["cases":cases,"glyphs":glyphs,"pencil":path(pencil.path),"rounded":rounded],options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
'''
(out/'main.swift').write_text(enum+source)
files=['Sources/AppShortcutCanvas.swift','Sources/AppShortcutArtwork.swift','Sources/EndfieldGameIcon.swift','Sources/HUDControlHighlightLayer.swift']
subprocess.run(['xcrun','swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),*[str(root/f) for f in files],str(out/'main.swift'),'-o',str(out/'reference')],check=True)
subprocess.run([str(out/'reference'),str(root/'Resources'),str(out/'raw.json')],check=True)
value=json.loads((out/'raw.json').read_text())
pins={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files+['Sources/AppShortcutStore.swift','Sources/HUDAppShortcutInteraction.swift']}
value['sourcePins']=pins
value['scope']='Original Canvas/Artwork/GameIcon/Highlight; in-memory store and neutral runtime/localization stubs; no NSApplication/window/defaults/filesystem selection/launch.'
for glyph in value['glyphs']:
 if 'sourceAsset' in glyph:glyph['sha256']=hashlib.sha256((root/'Resources'/glyph['sourceAsset']).read_bytes()).hexdigest()
(out/'source.json').write_text(json.dumps(value,separators=(',',':'),ensure_ascii=False)+'\n')
catalog={'version':1,'sourcePins':pins,'glyphs':value['glyphs'],'pencil':value['pencil'],'rounded':value['rounded']}
(out/'artwork.json').write_text(json.dumps(catalog,separators=(',',':'),ensure_ascii=False)+'\n')
print(out/'source.json')

# Keep full detached trees in ignored build evidence. Shipping tests retain all
# state/action rows and representative flattened source drawing records only.
def drawing_records(node, origin=(0,0), clip=None):
 f=node['frame'];x,y=origin[0]+f[0],origin[1]+f[1]
 if node['hidden']:return []
 if node['masksToBounds']:
  bound=[x,y,f[2],f[3]]
  if clip:
   right,bottom=min(x+f[2],clip[0]+clip[2]),min(y+f[3],clip[1]+clip[3]);x0,y0=max(x,clip[0]),max(y,clip[1]);bound=[x0,y0,max(0,right-x0),max(0,bottom-y0)]
  clip=bound
 kind='text' if 'text'in node else 'image' if 'imageSize'in node else 'shape' if 'shape'in node else 'plate' if node['backgroundColor'] is not None or node['borderWidth']>0 else None
 rows=[]
 if kind:
  r={'kind':kind,'frame':[x,y,f[2],f[3]],'opacity':node['opacity'],'clip':clip}
  if kind=='text':r['text']=node['text']
  elif kind=='shape':r['shape']=node['shape']
  elif kind=='plate':r.update({k:node[k] for k in ['backgroundColor','cornerRadius','borderWidth','borderColor']})
  rows.append(r)
 for child in node['children']:rows.extend(drawing_records(child,(x,y),clip))
 return rows
keep={'empty','list','scroll35','draft','icon:music','icon:camera','draft-error-drop'}
compact={'sourcePins':pins,'scope':value['scope'],'cases':[]}
for case in value['cases']:
 row={k:v for k,v in case.items() if k!='layer'}
 if case['name'] in keep:row['art']=drawing_records(case['layer'])
 compact['cases'].append(row)
(out/'source-compact.json').write_text(json.dumps(compact,separators=(',',':'),ensure_ascii=False)+'\n')
