import AppKit
import QuartzCore

// Detached, deterministic source fixtures. Original model has directory=nil;
// no writer, user events, observer service, window or application is created.
enum HUDRuntimeAppearance {static var accent=NSColor(srgbRed:250/255,green:212/255,blue:31/255,alpha:1);static let reduceMotion=true}
enum L10n {static func text(_ english:String,_ chinese:String)->String {chinese}}
struct HUDModuleContentStyle {let dark:Bool;let contentsScale:CGFloat}
protocol HUDModuleContentFactory {func makeContent(for module:HUDModule,style:HUDModuleContentStyle)->CALayer}
@main enum Main {
    static func main() throws {
        precondition(CommandLine.arguments.count==2);NSTimeZone.default=TimeZone(secondsFromGMT:0)!
        let output=URL(fileURLWithPath:CommandLine.arguments[1]),encoder=try ModuleReferenceLayerEncoder(output:output),store=SystemEventLog()
        func id(_ n:Int)->UUID {UUID(uuidString:String(format:"00000000-0000-0000-0000-%012d",n))!}
        let kinds:[SystemEventKind]=[.clipboardCopied,.shelfAdded,.workStarted,.audioDeviceConnected]
        let metadata:[[String:String]]=[["kind":"text"],["filename":"Project.pdf"],["kind":"countdown","seconds":"1800"],["device":"Studio Headphones"]]
        store.events=(1...20).map {SystemEvent(id:id($0),kind:kinds[($0-1)%4],createdAt:Date(timeIntervalSince1970:1700000000-Double($0)),metadata:metadata[($0-1)%4])}
        store.events.insert(SystemEvent(id:id(21),kind:.overlayOpened,createdAt:Date(),metadata:[:]),at:0)
        store.events.insert(SystemEvent(id:id(22),kind:.moduleOpened,createdAt:Date(),metadata:["module":"notes"]),at:0)
        let canvas=EventLogCanvas(store:store,reduceMotion:{true});_ = canvas.makeContent(for:.eventLog,style:.init(dark:true,contentsScale:2));canvas.activate();var cases:[[String:Any]]=[]
        func record(_ name:String)throws {cases.append(["name":name,"layer":try encoder.encode(canvas.layer,id:"eventLog"),"scroll":canvas.scrollOffset,"count":canvas.filteredCount,"actions":canvas.accessibleActions.map {["id":$0.id,"label":$0.label,"rect":ModuleReferenceLayerEncoder.rect($0.rect)]}])}
        try record("top");canvas.scrollBy(0.5);try record("fractional");canvas.perform(actionID:"eventLog:row:\(id(2).uuidString)");try record("selected");canvas.perform(actionID:"eventLog:clear");try record("confirm");_ = canvas.makeContent(for:.eventLog,style:.init(dark:false,contentsScale:2));try record("light");canvas.perform(actionID:"eventLog:category:work");try record("work");canvas.perform(actionID:"eventLog:category:display");try record("emptyCategory");canvas.perform(actionID:"eventLog:clear");canvas.perform(actionID:"eventLog:confirmClear");try record("empty");canvas.deactivate()
        var metadataCases:[[String:Any]]=[]
        let raws:[(SystemEventKind,[String:String])]=[
            (.clipboardCopied,["kind":"text","text":"PRIVATE_BODY","path":"/private/a"]),
            (.shelfAdded,["filename":"C:\\private\\Project.pdf","url":"PRIVATE_URL"]),
            (.appShortcutOpened,["app":"  Example\n\tTool  "]),(.audioDeviceConnected,["device":"扬声器"]),
            (.appShortcutOpened,["app":"file:///private/secret"]),(.shelfCleared,["count":"+000004"]),
            (.workStarted,["kind":"countdown","seconds":"00001800"]),(.batteryStateChanged,["percentage":"101","state":"charging"]),
            (.displaySettingsChanged,["field":"centerLogo","value":"customImported","path":"/private/art.png"]),
            (.profileCropChanged,["target":"both","zoom":"9.5"]),(.noteAction,["action":"editedText","body":"PRIVATE_BODY"]),
            (.playbackAction,["action":"next","source":"system","title":"PRIVATE_TITLE"]),
            (.accountAction,["action":"linked","token":"PRIVATE_TOKEN","uid":"PRIVATE_UID"])]
        for (kind,raw) in raws {store.record(kind:kind,metadata:raw);let event=store.events[0];metadataCases.append(["kind":kind.rawValue,"raw":raw,"metadata":event.metadata,"detail":event.detail])}
        let data:[String:Any]=["cases":cases,"metadata":metadataCases,"unsupported":encoder.unsupported,"isolation":["detachedLayers":true,"eventDirectory":NSNull(),"windowCreated":false,"liveServices":false]]
        try JSONSerialization.data(withJSONObject:data,options:[.prettyPrinted,.sortedKeys]).write(to:output.appendingPathComponent("reference.json"));print("Exported \(cases.count) original Event Log scenes / \(metadataCases.count) privacy cases")
    }
}
