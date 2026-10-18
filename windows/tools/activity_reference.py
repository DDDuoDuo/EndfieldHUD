#!/usr/bin/env python3
"""Compile unchanged source math/TelemetryGraph into an isolated neutral oracle.
No app/window/provider/user defaults. Reuses the retained Mac Swift module cache.
"""
import hashlib, json, pathlib, subprocess, sys
root=pathlib.Path(__file__).resolve().parents[2]
out=pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else root/'build/activity-reference'
out.mkdir(parents=True,exist_ok=True)
sources=['Sources/SystemActivityMonitor.swift','Sources/AppActivityMonitor.swift','Sources/TelemetryCanvases.swift']
text=[(root/x).read_text() for x in sources]
parts=[text[0].split('/// One serial read')[0].replace('import Darwin','').replace('import IOKit',''),
       text[1].split('protocol AppActivityBackend')[0].replace('import Darwin',''),
       text[2].split('final class StorageCanvas')[0]]
stub='''import AppKit
import QuartzCore
enum HUDRuntimeAppearance { static let accent = NSColor(srgbRed:0.98,green:0.83,blue:0.12,alpha:1) }
enum HUDRenderScale { static func contentScale(for root:CALayer,baseScale:CGFloat)->CGFloat {baseScale} }
'''
fixture=r'''
func num<T>(_ value:T?) -> Any { value.map {$0 as Any} ?? NSNull() }
func cpu(_ a:[UInt64])->SystemActivityCPUTicks {SystemActivityCPUTicks(user:a[0],system:a[1],idle:a[2],nice:a[3])}
func snap(_ s:SystemActivitySnapshot)->[String:Any] { ["cpu":num(s.cpuPercent),"upload":num(s.uploadBytesPerSecond),"download":num(s.downloadBytesPerSecond),"read":num(s.diskReadBytesPerSecond),"write":num(s.diskWriteBytesPerSecond),"notes":s.statusNotes] }
var cpuCases:[[String:Any]]=[]
for n in 0..<80 {
 let old:[UInt64]=[100,40,600,3], now:[UInt64]=[100+UInt64(n),40+UInt64(n*2),600+UInt64(n%17),3+UInt64(n%4)]
 cpuCases.append(["old":old,"now":now,"percent":num(SystemActivityDerivation.cpuPercent(cpu(now),cpu(old)))])
}
cpuCases.append(["old":[100,40,600,3],"now":[99,41,601,3],"percent":NSNull()])
var memory:[[String:Any]]=[]
for n in 0..<40 {let args:[UInt64]=[100000,4096,UInt64(n),UInt64(n%5),3,2];let value=SystemActivityMemoryCounters(physicalBytes:args[0],pageSize:args[1],internalPages:args[2],purgeablePages:args[3],wiredPages:args[4],compressedPages:args[5]).bytes
 memory.append(["args":args,"result":value.map{[$0.used,$0.total,$0.compressed] as Any} ?? NSNull()]) }
var graphs:[[String:Any]]=[]
for count in [0,1,2,3,16,59,60,64] { for cadence in [1.0,5.0] {
 let values:[Double?]=(0..<count).map{$0%7==3 ? nil : Double(($0*13)%137)}
 let times=(0..<count).map{100+Double($0)*cadence}
 let graph=TelemetryGraph(name:"oracle",frame:CGRect(x:0,y:0,width:165,height:36),colors:[.yellow])
 graph.update(series:[values],ceiling:100,animated:false,timestamps:times)
 let line=graph.layer.sublayers!.first{$0.name=="oracle.line.0"} as! CAShapeLayer
 var points:[[Double]]=[];line.path!.applyWithBlock{e in if e.pointee.type == .moveToPoint || e.pointee.type == .addLineToPoint {let p=e.pointee.points[0];points.append([p.x,p.y])}}
 let mask=line.mask as! CAShapeLayer;var coverage:[[Double]]=[];var rectangle:[CGPoint]=[]
 mask.path!.applyWithBlock{e in switch e.pointee.type {case .moveToPoint:rectangle=[e.pointee.points[0]];case .addLineToPoint:rectangle.append(e.pointee.points[0]);case .closeSubpath:if rectangle.count==4 {let xs=rectangle.map(\.x),ys=rectangle.map(\.y);coverage.append([xs.min()!,ys.min()!,xs.max()!-xs.min()!,ys.max()!-ys.min()!])};default:break}}
 let marker=graph.layer.sublayers!.first{$0.name=="oracle.latest.0"}!
 graphs.append(["values":values.map{num($0)},"times":times,"points":points,"coverage":coverage,"marker":[marker.position.x,marker.position.y],"hidden":marker.isHidden])
}}
var rates:[[String:Any]]=[]
for elapsed in [0.0,0.5,1,5,15,15.0001,30] {for topology in [false,true] {
 let old=SystemActivityRawSample(timestamp:Date(timeIntervalSince1970:1),uptime:100,cpu:cpu([1,2,3,4]),memory:nil,network:["en0":SystemActivityByteCounters(received:10,sent:20)],disk:[:])
 let current=SystemActivityRawSample(timestamp:Date(timeIntervalSince1970:2),uptime:100+elapsed,cpu:cpu([2,4,6,8]),memory:nil,network:[topology ? "en1":"en0":SystemActivityByteCounters(received:30,sent:70)],disk:[:])
 rates.append(["elapsed":elapsed,"topology":topology,"result":snap(SystemActivityDerivation.snapshot(current,previous:old))])
}}
let formatter:[Double?]=[nil,0,1,99.5,100,999,1000,1250,99999,100000,1234567890]
let formats=formatter.map{["value":num($0),"bytes":TelemetryArtwork.bytes($0),"rate":TelemetryArtwork.rate($0),"percent":TelemetryArtwork.percent($0)]}
var appCases:[[String:Any]]=[]
for mode in 0..<8 {
 let app=AppActivityIdentity(id:"app",name:"Example",bundleIdentifier:nil,bundleURL:nil,processIDs:[8,7,7])
 func process(_ pid:Int32,_ start:UInt64,_ ticks:UInt64,_ ram:UInt64,_ read:UInt64,_ write:UInt64)->AppActivityProcessReading {AppActivityProcessReading(pid:pid,startID:start,startUptime:1,cpuTicks:ticks,memoryBytes:ram,diskReadBytes:read,diskWriteBytes:write)}
 let old=AppActivityRawSample(timestamp:Date(timeIntervalSince1970:1),uptime:10,apps:[app],members:["app":[7,8]],processes:[7:process(7,1,100,1000,100,100),8:process(8,2,200,2000,200,200)],secondsPerCPUTick:0.01,network:nil,statusNotes:[])
 var processes:[Int32:AppActivityProcessReading]=[7:process(7,1,250,1000,150,100),8:process(8,mode==2 ? 3:2,300,2000,200,300)]
 if mode==1 {processes.removeValue(forKey:8)}
 let now=mode==6 ? 15.0 : mode==7 ? 15.0001 : 11.0
 let network=AppActivityNetworkReading(uptime:mode==5 ? 6:now,intervalStart:5,rates:[7:AppActivityNetworkRates(received:10,sent:20)],unknownPIDs:mode==4 ? [8]:[])
 let current=AppActivityRawSample(timestamp:Date(timeIntervalSince1970:2),uptime:now,apps:[app],members:["app":mode==3 ? [7]:[7,8]],processes:processes,secondsPerCPUTick:0.01,network:network,statusNotes:[])
 let value=AppActivityDerivation.snapshot(current,previous:old).items[0]
 appCases.append(["mode":mode,"cpu":num(value.cpuPercent),"memory":num(value.memoryBytes),"read":num(value.diskReadBytesPerSecond),"write":num(value.diskWriteBytesPerSecond),"upload":num(value.uploadBytesPerSecond),"download":num(value.downloadBytesPerSecond),"notes":value.statusNotes])
}
let result:[String:Any]=["cpu":cpuCases,"memory":memory,"graphs":graphs,"rates":rates,"formats":formats,"apps":appCases]
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]))
'''
(out/'reference.swift').write_text(stub+'\n'.join(parts)+fixture)
sdk='/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk'
cache=root/'build/windows-module-reference/.compiler/module-cache'
subprocess.run(['xcrun','swiftc','-sdk',sdk,'-module-cache-path',str(cache),'-O','-framework','AppKit','-framework','QuartzCore',str(out/'reference.swift'),'-o',str(out/'reference')],check=True)
result=json.loads(subprocess.check_output([str(out/'reference')]))
result['sourceSHA256']={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sources}
result['referenceScope']='Unchanged System/AppActivity derivation definitions and real detached TelemetryGraph paths; no Activity canvas, provider, app or window created.'
(out/'activity-source.json').write_text(json.dumps(result,separators=(',',':'),ensure_ascii=False)+'\n')
print(out/'activity-source.json')
