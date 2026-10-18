#!/usr/bin/env python3
"""Detached original canvas; no window, NSApplication, defaults, input or timer."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[2]
out=root/'build/orbipom-presentation-reference';out.mkdir(parents=True,exist_ok=True)
source=r'''import AppKit
import JavaScriptCore
import QuartzCore
protocol HUDModuleContentFactory {}
enum HUDModule {case minigame;var title:String{"Closure's Minigame"}}
struct HUDModuleContentStyle {let dark:Bool;let accent:NSColor;let contentsScale:CGFloat}
enum HUDResources {static func url(for path:String)->URL?{URL(fileURLWithPath:CommandLine.arguments[1]).appendingPathComponent(path)}}
enum HUDSectionHeading {static func text(_ value:String)->String{"// " + value}}
enum L10n {static func text(_ e:String,_ z:String)->String{e}}
enum HUDRuntimeAppearance {static let accent=NSColor(srgbRed:0.98,green:0.87,blue:0.13,alpha:1);static let reduceMotion=true;struct Config{var lowPowerVisualMode=false};static let configuration=Config()}
// Highlight itself already has independent original-source oracles. This
// neutral bridge leaves canvas plate/image/label geometry completely unchanged.
enum HUDControlHighlightLayer {enum Shape{case cutCorner};static func add(to:CALayer,rect:CGRect,shape:Shape,enabled:Bool,framed:Bool){}}
func box(_ r:CGRect)->[Double]{[r.minX,r.minY,r.width,r.height]}
func color(_ c:CGColor?)->Any {guard let c else{return NSNull()};return c.converted(to:CGColorSpace(name:CGColorSpace.sRGB)!,intent:.defaultIntent,options:nil)?.components ?? []}
func layer(_ l:CALayer)->[String:Any]{var d:[String:Any]=["frame":box(l.frame),"bounds":box(l.bounds),"opacity":l.opacity,"hidden":l.isHidden,"cornerRadius":l.cornerRadius,"borderWidth":l.borderWidth,"background":color(l.backgroundColor),"border":color(l.borderColor),"masksToBounds":l.masksToBounds];if let t=l as? CATextLayer {d["text"]=t.string as? String ?? "";d["fontSize"]=t.fontSize;d["ink"]=color(t.foregroundColor);d["fontName"]=(t.font as? NSFont)?.fontName ?? "";d["wrapped"]=t.isWrapped;d["alignment"]=t.alignmentMode.rawValue};if let s=l as? CAShapeLayer {d["fill"]=color(s.fillColor);d["stroke"]=color(s.strokeColor);d["lineWidth"]=s.lineWidth;d["pathBounds"]=s.path.map{box($0.boundingBoxOfPath)} ?? []};d["children"]=(l.sublayers ?? []).map(layer);return d}
var rows:[[String:Any]]=[]
for dark in [true,false] {
 let canvas=OrbiPomCanvas(session:OrbiPomSession(defaults:nil),foregroundProvider:{true})
 // One real isolated session is enough; no user defaults ever supplied.
 canvas.automaticallySchedulesFrames=false
 _ = canvas.makeContent(for:.minigame,style:HUDModuleContentStyle(dark:dark,accent:HUDRuntimeAppearance.accent,contentsScale:2))
 canvas.setActive(true)
 for phase in ["idle","playing","paused","restart"] {
  if phase=="playing" {_ = canvas.session.start(seed:12345);canvas.advance(seconds:0)};if phase=="paused" {canvas.perform("pause")};if phase=="restart" {canvas.perform("restart")}
  rows.append(["dark":dark,"phase":phase,"layer":layer(canvas.layer),"actions":canvas.actions.map{["id":$0.id,"title":$0.title,"rect":box($0.rect),"enabled":$0.enabled]},"timer":canvas.hasFrameTimer])
 }
 canvas.deactivate()
}
try JSONSerialization.data(withJSONObject:["cases":rows],options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
'''
(out/'main.swift').write_text(source)
files=['Sources/OrbiPomRuntime.swift','Sources/OrbiPomSession.swift','Sources/OrbiPomCanvas.swift','Sources/OrbiPomArtwork.swift']
subprocess.run(['xcrun','swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),*[str(root/p) for p in files],str(out/'main.swift'),'-o',str(out/'reference')],check=True)
subprocess.run([str(out/'reference'),str(root/'Resources'),str(out/'raw.json')],check=True)
v=json.loads((out/'raw.json').read_text());v['sourcePins']={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in files};v['scope']='Unchanged original canvas/runtime/session/artwork, detached and unscheduled. No window, NSApplication or defaults. Shared highlight stub excluded: existing independent highlight oracles remain authoritative.'
subprocess.run(['xcrun','swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(root/'windows/tools/orbipom_timing_reference.swift'),'-o',str(out/'timing')],check=True)
subprocess.run([str(out/'timing'),str(out/'timing.json')],check=True)
v['opacityTiming']=json.loads((out/'timing.json').read_text())
v['timingToolSHA256']=hashlib.sha256((root/'windows/tools/orbipom_timing_reference.swift').read_bytes()).hexdigest()
(out/'source.json').write_text(json.dumps(v,separators=(',',':'),ensure_ascii=False)+'\n')
print(out/'source.json')
