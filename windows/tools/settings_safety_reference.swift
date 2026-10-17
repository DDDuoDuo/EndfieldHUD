import AppKit
import QuartzCore
// These neutral dependencies cannot consult current preferences or a provider.
enum L10n {static func text(_ en:String,_ zh:String)->String {en}}
enum HUDRuntimeAppearance {static let reduceMotion=true}
enum HUDMotionMath {static func transform(normalizedPoint:CGPoint,depth:CGFloat,travel:CGFloat,reducedMotion:Bool,parallaxIntensity:CGFloat,perspectiveIntensity:CGFloat,projectionBounds:CGRect)->CATransform3D {CATransform3DIdentity}}
@main enum SettingsSafetyReference {
 static func main() throws {
  precondition(CommandLine.arguments.count==2 && ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil)
  NSApplication.shared.setActivationPolicy(.prohibited)
  func rect(_ r:NSRect)->[Double]{[r.minX,r.minY,r.width,r.height]}
  func color(_ c:CGColor?)->Any {guard let c,let v=NSColor(cgColor:c)?.usingColorSpace(.sRGB)else{return NSNull()};return [v.redComponent,v.greenComponent,v.blueComponent,v.alphaComponent]}
  func font(_ f:NSFont?)->Any {guard let f else{return NSNull()};return ["name":f.fontName,"family":f.familyName ?? "","size":f.pointSize,"ascender":f.ascender,"descender":f.descender,"leading":f.leading] as [String:Any]}
  func path(_ p:CGPath?)->[[String:Any]] {guard let p else{return []};var result:[[String:Any]]=[];p.applyWithBlock{ e in let v=e.pointee;let n:Int;let op:String;switch v.type {case .moveToPoint:n=1;op="move";case .addLineToPoint:n=1;op="line";case .addQuadCurveToPoint:n=2;op="quad";case .addCurveToPoint:n=3;op="cubic";case .closeSubpath:n=0;op="close";@unknown default:fatalError()};result.append(["op":op,"points":(0..<n).map{[v.points[$0].x,v.points[$0].y]}])};return result}
  var cases:[[String:Any]]=[]
  for width in [1280.0,240,180] {for dark in [true,false] {
   let view=HUDQuitConfirmationView(frame:NSRect(x:0,y:0,width:width,height:800),reduceMotion:{true});view.setContent(title:"Keep this UI scale?",message:"Keep this UI scale? Reverts in 12s",cancel:"Revert ⎋",confirm:"Keep ↵",focusConfirm:true);view.configure(dark:dark,accent:NSColor(srgbRed:250.0/255,green:212.0/255,blue:31.0/255,alpha:1));view.show();view.layoutSubtreeIfNeeded();view.layout()
   let card=view.subviews[0];var text:[[String:Any]]=[]
   for child in card.subviews {if let field=child as? NSTextField {text.append(["kind":"text","value":field.stringValue,"frame":rect(field.frame),"bounds":rect(field.bounds),"drawRect":rect(field.cell!.drawingRect(forBounds:field.bounds)),"titleRect":rect(field.cell!.titleRect(forBounds:field.bounds)),"font":font(field.font),"color":color(field.textColor?.cgColor),"alignment":field.alignment.rawValue,"groupOpacity":field.layer?.allowsGroupOpacity as Any? ?? NSNull()])} else if let button=child as? NSButton {text.append(["kind":"button","value":button.title,"frame":rect(button.frame),"bounds":rect(button.bounds),"drawRect":rect(button.cell!.drawingRect(forBounds:button.bounds)),"titleRect":rect(button.cell!.titleRect(forBounds:button.bounds)),"font":font(button.font),"groupOpacity":button.layer?.allowsGroupOpacity as Any? ?? NSNull()])}}
   let art=(card.layer!.sublayers ?? []).prefix(4).map{l->[String:Any] in var v:[String:Any]=["bounds":rect(l.bounds),"position":[l.position.x,l.position.y],"anchorPoint":[l.anchorPoint.x,l.anchorPoint.y],"background":color(l.backgroundColor)];if let s=l as? CAShapeLayer {v["path"]=path(s.path);v["fill"]=color(s.fillColor);v["stroke"]=color(s.strokeColor);v["lineWidth"]=s.lineWidth};return v}
   cases.append(["width":width,"dark":dark,"card":rect(card.frame),"rootGroupOpacity":view.layer!.allowsGroupOpacity,"cardGroupOpacity":card.layer!.allowsGroupOpacity,"background":color(view.layer!.backgroundColor),"artwork":art,"controls":text]);view.dismiss(animated:false)
  }}
  precondition(NSApp.windows.isEmpty)
  try JSONSerialization.data(withJSONObject:["cases":cases,"actualSource":true,"windowCreated":false],options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]));print("Settings source safety: \(cases.count) detached view cases")
 }
}
