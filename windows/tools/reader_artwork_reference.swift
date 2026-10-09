import AppKit
import QuartzCore

// Actual source ReaderCanvas/ReaderInteraction menus with private fixture-data
// setters only. No ReaderDocument, native file picker, key/main/visible window,
// application activation, real book or user store is created.
func readerArtworkImage() -> CGImage {
    let bytes: [UInt8] = [240,20,20,255, 20,240,20,255, 20,20,240,255, 245,245,245,255]
    return CGImage(width:2,height:2,bitsPerComponent:8,bitsPerPixel:32,bytesPerRow:8,
        space:CGColorSpace(name:CGColorSpace.sRGB)!,bitmapInfo:CGBitmapInfo(rawValue:CGImageAlphaInfo.premultipliedLast.rawValue),
        provider:CGDataProvider(data:Data(bytes) as CFData)!,decode:nil,shouldInterpolate:true,intent:.defaultIntent)!
}
func readerArtworkPage(_ index:Int,illustration:Bool) -> ReaderPage {
    ReaderPage(location:ReaderLocation(section:index),next:index<9 ? ReaderLocation(section:index+1):nil,
        previous:index>0 ? ReaderLocation(section:index-1):nil,progress:Double(index)/9,
        image:readerArtworkImage(),summary:"Synthetic page \(index)",isIllustration:illustration)
}
@main enum ReaderArtworkReference {
    static func require(_ value:@autoclosure()->Bool,_ why:String)throws {if !value(){throw HUDSourceError.invalid(why)}}
    static func write(_ value:Any,_ url:URL)throws {try JSONSerialization.data(withJSONObject:value,options:[.sortedKeys,.prettyPrinted,.withoutEscapingSlashes]).write(to:url,options:.atomic)}
    static func run()throws {
        try require(CommandLine.arguments.count==2 && ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil,"Use isolated reader_artwork_reference.sh")
        let output=URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true)
        NSApplication.shared.setActivationPolicy(.prohibited)
        var config=AppConfiguration.defaults;config.language = .english;config.theme = .dark
        config.ambientAnimation=false;config.reduceMotion=true;config.launchAtLogin=false
        HUDRuntimeAppearance.configuration=config;L10n.language = .english
        let temporary=FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldReaderArtwork-"+UUID().uuidString,isDirectory:true)
        try FileManager.default.createDirectory(at:temporary,withIntermediateDirectories:false)
        defer{try? FileManager.default.removeItem(at:temporary)}
        let id=UUID(uuidString:"A0000000-0000-4000-8000-000000000001")!
        let seed:[String:Any]=["version":1,"books":[["id":id.uuidString,"bookmark":"AQ==","scoped":false,"path":"/synthetic-unopened.txt","title":"Synthetic book","location":["section":3,"block":0,"character":0],"progress":1.0/3,"bookmarks":[["id":"A0000000-0000-4000-8000-000000000002","location":["section":3,"block":0,"character":0],"progress":1.0/3]]]],"selected":id.uuidString,"preferences":["fontName":"Georgia","fontSize":10,"lineSpacing":2,"margin":16,"rightToLeft":false,"continuous":true]]
        try JSONSerialization.data(withJSONObject:seed).write(to:temporary.appendingPathComponent("library.json"))
        let store=try ReaderStore(directory:temporary),controller=ReaderController(store:store)
        let canvas=ReaderCanvas(controller:controller);defer{canvas.deactivate();controller.readerArtworkDrain()}
        let encoder=try ModuleReferenceLayerEncoder(output:output)
        var entries:[[String:Any]]=[]
        func emitCanvas(_ name:String,dark:Bool,kind:String,loading:Bool=false,error:String?=nil,hover:CGPoint?=nil)throws {
            controller.readerArtworkConfigure(kind:kind,dark:dark,loading:loading,error:error)
            _ = canvas.makeContent(for:.reader,style:HUDModuleContentStyle(dark:dark,accent:config.accentColor,contentsScale:2));canvas.activate()
            if let hover {HUDControlHighlightLayer.update(in:canvas.layer,point:hover)}
            let file="reader-"+name+".json"
            try write(["state":name,"root":try encoder.encode(canvas.layer,id:"reader/"+name),
                "actions":canvas.accessibleActions.map{["id":$0.id,"label":$0.label,"rect":ModuleReferenceLayerEncoder.rect($0.rect),"enabled":$0.enabled]},
                "progressRect":ModuleReferenceLayerEncoder.rect(canvas.progressRect),"kind":kind,"dark":dark,
                "loading":loading,"error":error as Any? ?? NSNull()],output.appendingPathComponent(file))
            entries.append(["state":name,"file":file])
        }
        try emitCanvas("empty-dark",dark:true,kind:"none")
        try emitCanvas("empty-light",dark:false,kind:"none")
        try emitCanvas("loading",dark:true,kind:"none",loading:true)
        try emitCanvas("error",dark:true,kind:"none",error:"Synthetic source error")
        try emitCanvas("text",dark:true,kind:"text")
        try emitCanvas("image",dark:true,kind:"image")
        try emitCanvas("hover-open",dark:true,kind:"text",hover:CGPoint(x:25,y:20))
        let choices=(0..<9).map{("id\($0)","Synthetic book \($0) · \($0 * 10)%")}
        let menus:[(String,NotesRetainedMenu)]=[
            ("open",NotesMediaSourceChooser(dark:true)),
            ("library",readerArtworkList(choices:choices,title:"Library",allowsDelete:true,width:350)),
            ("library-bottom",readerArtworkList(choices:choices,title:"Library",allowsDelete:true,width:350,scroll:350)),
            ("library-delete",readerArtworkList(choices:choices,title:"Library",allowsDelete:true,width:350,deletion:"id0")),
            ("bookmarks",readerArtworkList(choices:(0..<9).map{("id\($0)","\($0 * 10)%")},title:"Bookmarks",allowsDelete:false,width:240)),
            ("settings",readerArtworkSettings(ReaderPreferences())),
            ("settings-horizontal",readerArtworkSettings({var p=ReaderPreferences();p.vertical=false;p.fontSize=32;p.lineSpacing=18;p.margin=48;return p}()))]
        for (name,menu) in menus {
            let file="reader-menu-"+name+".json"
            try write(["state":name,"root":try encoder.encode(menu.artwork,id:"reader/menu/"+name),
                "actions":menu.items.map{["id":$0.id,"label":$0.title,"rect":ModuleReferenceLayerEncoder.rect($0.rect),"enabled":$0.enabled,"selected":$0.selected]}],output.appendingPathComponent(file))
            entries.append(["state":"menu-"+name,"file":file]);menu.removeFromSuperview()
        }
        try require(NSApp.windows.isEmpty,"No window may be created by detached Reader source reference")
        try write(["schemaVersion":1,"entries":entries,"rasterAssets":encoder.rasterAssets,"sourceMetadataLimitations":encoder.unsupported,
            "windowCreated":false,"filePickerCreated":false,"realBookOpened":false,
            "limitations":["Model layers and exact source menu factories only; font pixels and intermediate CA presentation are separate gates.","Synthetic private controller data setters; original Reader implementation and paint helpers are unchanged.","Original deadline closures may be queued but no application/run-loop pumping occurs."]],output.appendingPathComponent("reader-artwork-reference.json"))
    }
    static func main(){do{try run();print("PASS detached actual-source Reader artwork reference")}catch{fputs("Reader source artwork failed: \(error)\n",stderr);exit(1)}}
}
