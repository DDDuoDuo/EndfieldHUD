import AppKit
import QuartzCore

// Build-only fake provider. No NSPasteboard, user defaults, file payloads,
// service listeners or application/window activation exists in this fixture.
enum ClipboardKind: String { case text, url, image, files }
struct ClipboardItem { let id: UUID; var isPinned: Bool; let kind: ClipboardKind; let preview: String; let thumbnail: CGImage? = nil }
final class ClipboardStore {
    var items: [ClipboardItem] = []; let capacity = 32; var statusMessage: String?; var observer: (() -> Void)?
    func observe(_ callback: @escaping () -> Void) -> UUID { observer=callback; return UUID() }
    func removeObserver(_ id: UUID) { observer=nil }
    func togglePin(id: UUID) -> Bool { guard let index=items.firstIndex(where: {$0.id==id}) else{return false};items[index].isPinned.toggle();observer?();return true }
    func remove(id: UUID) -> Bool { let before=items.count;items.removeAll {$0.id==id};observer?();return before != items.count }
    func clearUnpinned() -> Bool { let before=items.count;items.removeAll {!$0.isPinned};observer?();return before != items.count }
}
enum HUDRuntimeAppearance { static var accent=NSColor(srgbRed:250/255,green:212/255,blue:31/255,alpha:1); static let reduceMotion=true }
enum L10n { static func text(_ english:String,_ chinese:String) -> String {chinese} }
enum HUDModule { case clipboard;var title:String {"剪贴板"} }
struct HUDModuleContentStyle { let dark:Bool; let contentsScale:CGFloat }
protocol HUDModuleContentFactory { func makeContent(for module:HUDModule,style:HUDModuleContentStyle) -> CALayer }
enum HUDResources {static var root=URL(fileURLWithPath:"/");static func url(for relative:String) -> URL? {let u=root.appendingPathComponent(relative);return FileManager.default.fileExists(atPath:u.path) ? u:nil}}
@main enum Main {
    static func main() throws {
        precondition(CommandLine.arguments.count==3)
        let output=URL(fileURLWithPath:CommandLine.arguments[1]);HUDResources.root=URL(fileURLWithPath:CommandLine.arguments[2])
        let encoder=try ModuleReferenceLayerEncoder(output:output),store=ClipboardStore()
        func id(_ n:Int)->UUID {UUID(uuidString:String(format:"00000000-0000-0000-0000-%012d",n))!}
        store.items=(1...13).map {ClipboardItem(id:id($0),isPinned:$0==3,kind:.url,preview:"https://example.invalid/\($0)")}
        let canvas=ClipboardCanvas(store:store,reduceMotion:{true});canvas.onCopy={_ in true};_ = canvas.makeContent(for:.clipboard,style:.init(dark:true,contentsScale:2));canvas.activate()
        var cases:[[String:Any]]=[]
        func record(_ name:String) throws {cases.append(["name":name,"layer":try encoder.encode(canvas.layer,id:"clipboard"),"scroll":canvas.scrollOffset,"selected":canvas.selectedID?.uuidString as Any? ?? NSNull(),"actions":canvas.accessibleActions.map {["id":$0.id,"label":$0.label,"rect":ModuleReferenceLayerEncoder.rect($0.rect)]}])}
        try record("top");_ = canvas.scroll(at:CGPoint(x:40,y:80),delta:0.5);try record("fractional")
        canvas.perform(actionID:"clipboard:\(id(2).uuidString):copy");try record("selected")
        canvas.perform(actionID:"clipboard:\(id(1).uuidString):pin");try record("pinned")
        canvas.perform(actionID:"clipboard:clear");try record("confirm")
        _ = canvas.makeContent(for:.clipboard,style:.init(dark:false,contentsScale:2));try record("light")
        for kind:ClipboardKind in [.text,.files,.image] {store.items=[ClipboardItem(id:id(1),isPinned:false,kind:kind,preview:kind.rawValue)];store.observer?();_ = canvas.makeContent(for:.clipboard,style:.init(dark:true,contentsScale:2));try record(kind.rawValue)}
        store.items=[];store.observer?();try record("empty");canvas.deactivate()
        let data:[String:Any]=["cases":cases,"rasterAssets":encoder.rasterAssets,"unsupported":encoder.unsupported,"isolation":["detachedLayers":true,"clipboardAccessed":false,"windowCreated":false,"liveServices":false]]
        try JSONSerialization.data(withJSONObject:data,options:[.prettyPrinted,.sortedKeys]).write(to:output.appendingPathComponent("reference.json"))
        print("Exported \(cases.count) original detached Clipboard scenes")
    }
}
