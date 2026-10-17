import AppKit
import QuartzCore
enum HUDRuntimeAppearance {static var accent=NSColor(srgbRed:250/255,green:212/255,blue:31/255,alpha:1);static let reduceMotion=true}
enum L10n {static var chinese=false;static func text(_ en:String,_ zh:String)->String {chinese ? zh:en}}
@main enum Main {
 static func main()throws {
  precondition(CommandLine.arguments.count==2 && ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil)
  let out=URL(fileURLWithPath:CommandLine.arguments[1]),encoder=try ModuleReferenceLayerEncoder(output:out)
  NSApplication.shared.setActivationPolicy(.prohibited)
  var cases:[[String:Any]]=[]
  for dark in [true,false] {for chinese in [false,true] {for level in [-1,0,19,20,50,51,100] {for mode in 0..<5 {
   L10n.chinese=chinese
   let canvas=BatteryReferenceFixture(),percentage=level>=0 ? level:nil
   canvas.snapshot=BatterySnapshot(percentage:percentage,isPluggedIn:mode>=2,isCharging:mode==3,isFullyCharged:mode==4,hasBattery:mode>0,
       capacity:BatteryCapacityReading.fromRegistry([:],percentage:percentage),healthCategory:nil)
   canvas.apply(dark)
   cases.append(["dark":dark,"chinese":chinese,"level":level,"mode":mode,"layer":try encoder.encode(canvas.core,id:"battery"),"chargeMode":canvas.snapshot.isChargeMode])
  }}}}
  try JSONSerialization.data(withJSONObject:["cases":cases,"unsupported":encoder.unsupported,"liveServices":false],options:[.sortedKeys]).write(to:out.appendingPathComponent("reference.json"))
  print("Original Battery: \(cases.count) detached source scenes")
 }
}
