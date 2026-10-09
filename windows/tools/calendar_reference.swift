import AppKit
import QuartzCore

// Original build18 model, painter and private CalendarEventMenu are executed
// unchanged. App activation is prohibited; only detached synthetic layers and
// explicitly injected fixed-time/civil-zone/noop-notification controllers exist.
@main enum CalendarReference {
    static let fixed = HUDCalendarDay(year:2026,month:10,day:4)
    static let utc = TimeZone(secondsFromGMT:0)!
    static let now = fixed.date(in:utc,hour:10)!
    static func write(_ value:Any,_ file:URL)throws {try JSONSerialization.data(withJSONObject:value,options:[.sortedKeys,.prettyPrinted,.withoutEscapingSlashes]).write(to:file,options:.atomic)}
    static func json<T:Encodable>(_ value:T)throws->Any {try JSONSerialization.jsonObject(with:JSONEncoder().encode(value))}
    static func event(_ n:Int,day:HUDCalendarDay=fixed,title:String?=nil)->HUDCalendarEvent {
        HUDCalendarEvent(id:UUID(uuidString:String(format:"C0000000-0000-4000-8000-%012d",n))!,title:title ?? "Synthetic event \(n)",details:"Owned fixture only",day:day,created:now.addingTimeInterval(Double(n)),modified:now)
    }
    static func run()throws {
        guard CommandLine.arguments.count==2,ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil else {throw HUDCalendarError.invalidData}
        let output=URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true)
        NSApplication.shared.setActivationPolicy(.prohibited)
        var config=AppConfiguration.defaults;config.language = .english;config.theme = .dark;config.ambientAnimation=false;config.reduceMotion=true;config.launchAtLogin=false
        HUDRuntimeAppearance.configuration=config;L10n.language = .english
        let temp=FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldCalendarReference-"+UUID().uuidString,isDirectory:true)
        defer{try? FileManager.default.removeItem(at:temp)}
        let controller=HUDCalendarController(loadStore:{try HUDCalendarStore(directory:temp)},hasStoredData:{false},scheduler:HUDCalendarNoopNotifications(),now:{now},timeZone:{utc})
        let canvas=HUDCalendarCanvas(controller:controller),encoder=try ModuleReferenceLayerEncoder(output:output)
        var entries:[[String:Any]]=[]
        func emitCanvas(_ name:String,dark:Bool,month:HUDCalendarDay=fixed,selected:HUDCalendarDay=fixed,events:[HUDCalendarEvent]=[],first:Int=0,busy:Bool=false,permission:HUDCalendarPermission = .authorized,error:String?=nil,hover:CGPoint?=nil)throws {
            controller.calendarReferenceConfigure(events,busy:busy,permission:permission,error:error)
            canvas.calendarReferenceConfigure(selected:selected,month:HUDCalendarDay(year:month.year,month:month.month,day:1),first:first)
            _ = canvas.makeContent(for:.calendar,style:.init(dark:dark,accent:config.accentColor,contentsScale:2))
            if let hover {HUDControlHighlightLayer.update(in:canvas.layer,point:hover)}
            let file="calendar-"+name+".json"
            let permissionName=permission == .authorized ? "authorized":permission == .denied ? "denied":permission == .unavailable ? "unavailable":"unknown"
            try write(["state":name,"root":try encoder.encode(canvas.layer,id:"calendar/"+name),"actions":canvas.actions.map{["id":$0.id,"label":$0.label,"rect":ModuleReferenceLayerEncoder.rect($0.rect),"enabled":$0.enabled]},"dark":dark,"month":try json(canvas.month),"selected":try json(selected),"today":try json(fixed),"firstWeekday":HUDCalendarDay.calendar(utc).firstWeekday,"weekdays":DateFormatter().veryShortStandaloneWeekdaySymbols ?? [],"monthCells":canvas.monthCells.map{["day":try! json($0.0),"rect":ModuleReferenceLayerEncoder.rect($0.1)]},"events":try json(events),"firstEvent":first,"busy":busy,"permission":permissionName,"error":error as Any? ?? NSNull()],output.appendingPathComponent(file))
            entries.append(["state":name,"file":file])
        }
        try emitCanvas("empty-dark",dark:true)
        try emitCanvas("empty-light",dark:false)
        try emitCanvas("leap-february",dark:true,month:.init(year:2024,month:2,day:1),selected:.init(year:2024,month:2,day:29))
        try emitCanvas("first-month",dark:true,month:.init(year:1900,month:1,day:1))
        try emitCanvas("last-month",dark:true,month:.init(year:9999,month:12,day:1))
        try emitCanvas("events",dark:true,events:(1...6).map{event($0)})
        try emitCanvas("events-bottom",dark:true,events:(1...6).map{event($0)},first:3)
        try emitCanvas("busy",dark:true,busy:true)
        try emitCanvas("denied",dark:true,permission:.denied)
        try emitCanvas("unavailable",dark:true,permission:.unavailable)
        try emitCanvas("error",dark:true,error:"Synthetic failure")
        try emitCanvas("hover-today",dark:true,hover:.init(x:340,y:20))
        for (name,editing,dark,deleting,busy,error) in [("new",false,true,false,false,nil as String?),("new-light",false,false,false,false,nil),("edit",true,true,false,false,nil),("delete",true,true,true,false,nil),("delete-busy",true,true,true,true,nil),("error",true,true,false,false,"Synthetic error")] {
            let menu=calendarReferenceMenu(editing:editing,dark:dark,deleting:deleting,busy:busy,error:error),file="calendar-menu-"+name+".json"
            try write(["state":name,"root":try encoder.encode(menu.artwork,id:"calendar/menu/"+name),"actions":menu.items.map{["id":$0.id,"label":$0.title,"rect":ModuleReferenceLayerEncoder.rect($0.rect),"enabled":$0.enabled]},"editing":editing,"dark":dark,"deleting":deleting,"busy":busy,"error":error as Any? ?? NSNull()],output.appendingPathComponent(file));entries.append(["state":"menu-"+name,"file":file]);menu.removeFromSuperview()
        }
        var plans:[[String:Any]]=[]
        for (name,day,zone) in [("spring",HUDCalendarDay(year:2026,month:3,day:8),"America/Los_Angeles"),("fall",.init(year:2026,month:11,day:1),"America/Los_Angeles"),("china",.init(year:2026,month:11,day:1),"Asia/Shanghai"),("skipped-day",.init(year:2011,month:12,day:30),"Pacific/Apia")] {
            let timestamp=HUDCalendarDay(year:2000,month:1,day:1).date(in:utc)!,e=event(20,day:day),tz=TimeZone(identifier:zone)!
            let values=HUDCalendarReminder.plan(events:[e],now:timestamp,zone:tz)
            plans.append(["name":name,"zone":zone,"now":timestamp.timeIntervalSinceReferenceDate,"event":try json(e),"reminders":values.map{["identifier":$0.identifier,"eventID":$0.eventID.uuidString,"title":$0.title,"day":try! json($0.day),"date":$0.date.timeIntervalSinceReferenceDate,"previousDay":$0.previousDay,"catchUp":$0.catchUp,"wasScheduled":$0.wasScheduled,"signature":$0.signature]}])
        }
        let store=try HUDCalendarStore(directory:temp)
        var catchUp=event(30);catchUp.catchUpPending=true;try store.save(catchUp,now:now,zone:utc);try store.claimCatchUps(now:now,zone:utc);let afterClaim=store.events,plan=HUDCalendarReminder.plan(events:afterClaim,now:now,zone:utc);try store.recordScheduled(plan)
        let scalars=["  \n标题🙂\n ",String(repeating:"e\u{301}",count:125),String(repeating:"👩‍👩‍👧‍👦",count:121),String(repeating:"한",count:2003),"\u{200b}hi\u{200b}"]
        let texts=scalars.enumerated().map{["id":$0.offset,"text":$0.element,"count":$0.element.count,"trimmed":$0.element.trimmingCharacters(in:.whitespacesAndNewlines),"prefix120":String($0.element.prefix(120)),"prefix2000":String($0.element.prefix(2000))]}
        let dates=["2024-02-29","2025-02-29","2026-10-4","+2026-+10-+4","1900-01-01","9999-12-31","1899-12-31","2026-13-01"," 2026-10-04","2026--04"]
        try write(["schemaVersion":1,"entries":entries,"rasterAssets":encoder.rasterAssets,"sourceMetadataLimitations":encoder.unsupported,"reminderPlans":plans,"catchUpNow":now.timeIntervalSinceReferenceDate,"afterClaim":try json(afterClaim),"afterReceipts":try json(store.events),"textCases":texts,"dateCases":dates.map{["text":$0,"parsed":HUDCalendarDay.parse($0).map{try! json($0)} as Any? ?? NSNull()]},"windowCreated":false,"nativeNotificationServiceCreated":false,"limitations":["Exact original model/painter/menu factories with synthetic private field setters. No CJK input or Windows notification delivery is established.","Glyph raster parity and intermediate Core Animation transitions are separate native gates."]],output.appendingPathComponent("calendar-reference.json"))
        guard NSApp.windows.isEmpty else {throw HUDCalendarError.invalidData}
    }
    static func main(){do{try run();print("PASS detached original Calendar reference")}catch{fputs("Calendar reference failed: \(error)\n",stderr);exit(1)}}
}
