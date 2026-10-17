import AppKit
// Own numeric NSString/NSFont/NSColor probes only; no app/window/store/service.
@main enum Reference {
 static func components(_ value:NSColor)->[Double] {let c=value.usingColorSpace(.sRGB)!;return [c.redComponent,c.greenComponent,c.blueComponent]}
 static func main()throws {
  precondition(CommandLine.arguments.count==2 || CommandLine.arguments.count==3)
  var colors:[[String:Any]]=[];var seed:UInt64=0x51accee
  var inputs:[[Double]]=[]
  for r in [0.0,1] {for g in [0.0,1] {for b in [0.0,1] {inputs.append([r,g,b])}}}
  inputs.append([250.0/255,212.0/255,31.0/255])
  for _ in 0..<1024 {var c:[Double]=[];for _ in 0..<3 {seed=seed &* 6364136223846793005 &+ 1;c.append(Double((seed>>32)&65535)/65535)};inputs.append(c)}
  for input in inputs {let a=NSColor(srgbRed:input[0],green:input[1],blue:input[2],alpha:1);let blended=a.blended(withFraction:0.5,of:.black)!;let c=blended.usingColorSpace(.sRGB)!;let g=a.usingColorSpace(.genericRGB)!;let b=blended.usingColorSpace(.genericRGB)!;let light=a.blended(withFraction:0.35,of:.black)!.usingColorSpace(.sRGB)!;colors.append(["input":input,"lightOutput":[light.redComponent,light.greenComponent,light.blueComponent],"whiteOutput":components(a.blended(withFraction:0.42,of:.white)!),"lightWhiteOutput":components(a.blended(withFraction:0.35,of:.black)!.blended(withFraction:0.42,of:.white)!),"output":[c.redComponent,c.greenComponent,c.blueComponent],"generic":[g.redComponent,g.greenComponent,g.blueComponent],"blendedGeneric":[b.redComponent,b.greenComponent,b.blueComponent]])}
  var fonts:[[String:Any]]=[]
  for bold in [false,true] {for i in 20...52 {let size=Double(i)*0.5;let f=NSFont.systemFont(ofSize:size,weight:bold ? .bold:.medium);fonts.append(["size":size,"bold":bold,"ascender":f.ascender,"descender":f.descender,"leading":f.leading,"name":f.fontName,"family":f.familyName ?? ""])} }
  var output:[String:Any]=["source":"Sources/HUDSourceWatchView.swift:updateDesktopSelection","colors":colors,"fonts":fonts,"whiteBlendSource":"Sources/TelemetryCanvases.swift:TelemetryArtwork.cyan","windowCreated":false,"userDataRead":false]
  if CommandLine.arguments.count==3 {
   let input=try JSONSerialization.jsonObject(with:Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[2]))) as! [String:Any]
   var measurements:[[String:Any]]=[]
   for var request in input["measurements"] as! [[String:Any]] {
    let text=request["text"] as! String,width=request["width"] as! Double,bold=request["bold"] as! Bool,wrapped=request["wrapped"] as! Bool
    var sizes:[[Double]]=[]
    for i in 20...52 {let size=Double(i)*0.5,font=NSFont.systemFont(ofSize:size,weight:bold ? .bold:.medium)
     let measured:CGSize
     if wrapped {measured=(text as NSString).boundingRect(with:CGSize(width:width,height:CGFloat.greatestFiniteMagnitude),options:[.usesLineFragmentOrigin,.usesFontLeading],attributes:[.font:font]).size}
     else {measured=(text as NSString).size(withAttributes:[.font:font])}
     sizes.append([measured.width,measured.height])
    }
    request["sizes"]=sizes;measurements.append(request)
   }
   output["measurements"]=measurements;output["cases"]=input["cases"];output["sourceSHA256"]=input["sourceSHA256"];output["telemetrySourceSHA256"]=input["telemetrySourceSHA256"]
  }
  try JSONSerialization.data(withJSONObject:output,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]));print("Source content: \(colors.count) colors / \(fonts.count) system fonts")
 }
}
