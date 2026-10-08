import AppKit
import QuartzCore

/// Unchanged FileShelfCanvas, detached from any window, with owned temporary
/// files only. NSWorkspace icons are explicit oracle dependencies, not Windows
/// replacements or a claim of matching native icon/font rasterization.
@main enum ShelfPresentationReference {
    static func require(_ b: @autoclosure () -> Bool, _ m: String) throws { if !b() { throw HUDSourceError.invalid(m) } }
    static func rgb(_ c: NSColor) -> [CGFloat] { let v=c.usingColorSpace(.sRGB)!;return [v.redComponent,v.greenComponent,v.blueComponent,v.alphaComponent] }
    static func timings(_ t: CAMediaTimingFunction?) -> Any {
        guard let t else { return NSNull() };return (0..<4).map { n -> [Float] in var p:[Float]=[0,0];t.getControlPoint(at:n,values:&p);return p }
    }
    static func animations(_ root: CALayer, _ encoder: ModuleReferenceLayerEncoder, _ id: String) throws -> [[String:Any]] {
        var rows:[[String:Any]]=[]
        func value(_ v: Any?) throws -> Any {guard let v else{return NSNull()};if let n=v as? NSNumber{return n};if let n=v as? NSValue{return ModuleReferenceLayerEncoder.transform(n.caTransform3DValue)};if CFGetTypeID(v as CFTypeRef)==CGPath.typeID{let s=CAShapeLayer();s.path=(v as! CGPath);return (try encoder.encode(s,id:id+"/animation-path")["shape"] as! [String:Any])["path"]!};throw HUDSourceError.invalid("Unexpected source animation value")}
        for key in (root.animationKeys() ?? []).sorted(){guard let a=root.animation(forKey:key) else{continue};var r:[String:Any]=["node":id,"key":key,"duration":a.duration,"timing":timings(a.timingFunction)]
            if let b=a as? CABasicAnimation{r["keyPath"]=b.keyPath;r["from"]=try value(b.fromValue);r["to"]=try value(b.toValue)}
            if let k=a as? CAKeyframeAnimation{r["keyPath"]=k.keyPath;r["values"]=try (k.values ?? []).map{try value($0)};r["keyTimes"]=k.keyTimes;r["timings"]=(k.timingFunctions ?? []).map{timings($0)}};rows.append(r)
        }
        for(n,l) in (root.sublayers ?? []).enumerated(){rows += try animations(l,encoder,id+"/"+String(n))};if let mask=root.mask{rows += try animations(mask,encoder,id+"/mask")};return rows
    }
    static func run() throws {
        let a=CommandLine.arguments;try require(a.count==4 && a[1]=="--ui-test" && a[2]=="--output","Use shelf_presentation_reference.sh")
        try require(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil,"Isolated fixture home required")
        let out=URL(fileURLWithPath:a[3],isDirectory:true),tmp=FileManager.default.temporaryDirectory.appendingPathComponent("shelf-reference-"+UUID().uuidString)
        try FileManager.default.createDirectory(at:tmp,withIntermediateDirectories:true);defer{try? FileManager.default.removeItem(at:tmp)}
        NSApplication.shared.setActivationPolicy(.prohibited);NSApp.appearance=NSAppearance(named:.darkAqua)
        var config=AppConfiguration.defaults;config.language = .english;config.ambientAnimation=false;config.reduceMotion=false;config.launchAtLogin=false;HUDRuntimeAppearance.configuration=config;L10n.language = .english
        let encoder=try ModuleReferenceLayerEncoder(output:out),accent=config.accentColor
        var cases:[[String:Any]]=[]
        for dark in [true,false]{
            let base=tmp.appendingPathComponent(dark ? "dark":"light");try FileManager.default.createDirectory(at:base,withIntermediateDirectories:true)
            let store=try FileShelfStore(directory:base.appendingPathComponent("metadata"));let canvas=FileShelfCanvas(store:store,reduceMotion:{false});defer{canvas.deactivate()}
            _=canvas.makeContent(for:.fileShelf,style:HUDModuleContentStyle(dark:dark,accent:accent,contentsScale:2));canvas.activate()
            func emit(_ name:String) throws {
                let root=try encoder.encode(canvas.layer,id:"shelf"),layers=canvas.layer.sublayers ?? []
                let records=store.items.map{item -> [String:Any] in ["id":item.id.uuidString,"name":item.name,"lastKnownPath":item.lastKnownPath,"typeDescription":item.typeDescription,"isDirectory":item.isDirectory,"byteCount":item.byteCount as Any? ?? NSNull(),"availabilityError":item.availabilityError as Any? ?? NSNull(),"sizeLabel":item.sizeLabel ?? "—"]}
                cases.append(["name":(dark ? "dark-":"light-")+name,"dark":dark,"accent":rgb(accent),"errorColor":rgb(.systemRed),"lightDropColor":rgb(accent.blended(withFraction:0.4,of:.black) ?? accent),"depotAvailable":EndfieldGameIcon.depot.sourceImage() != nil,
                    "items":records,"scrollOffset":canvas.scrollOffset,"selectedIDs":canvas.selectedIDs.map(\.uuidString).sorted(),"error":canvas.accessibilityStatus as Any? ?? NSNull(),
                    "root":root,"actions":canvas.accessibleActions.map{["id":$0.id,"label":$0.label,"rect":ModuleReferenceLayerEncoder.rect($0.rect)]},"animations":try animations(canvas.layer,encoder,"shelf"),
                    "dropTarget":(layers[2] as? CAShapeLayer)?.strokeColor?.alpha ?? 0 > 0,
                    "confirmingClear":canvas.accessibleActions.contains{$0.id=="shelf:confirmClear"}])
            }
            try emit("empty")
            var urls:[URL]=[];for n in 0..<13{let url=base.appendingPathComponent(n==1 ? "目录" : "Synthetic \(n).txt");if n==1{try FileManager.default.createDirectory(at:url,withIntermediateDirectories:true)}else{try Data(repeating:65,count:n+11).write(to:url)};urls.append(url)}
            try require(canvas.importURLs(urls),"Owned synthetic files import")
            canvas.scrollBy(-100000);_=canvas.mouseDown(at:CGPoint(x:10,y:41),clickCount:1);try emit("top")
            canvas.perform(actionID:"shelf:"+store.items[0].id.uuidString+":select");try emit("selected")
            if let r=canvas.cardRect(for:store.items[1].id){_=canvas.mouseDown(at:CGPoint(x:r.midX,y:r.minY+20),clickCount:1,modifiers:[.command])};try emit("multi")
            canvas.scrollBy(37);try emit("partial")
            canvas.scrollBy(100000);try emit("bottom")
            canvas.perform(actionID:"shelf:clear");try emit("confirmation")
            canvas.setDropTarget(true);try emit("drop")
            canvas.setDropTarget(false);canvas.showError("Synthetic shelf error");try emit("error")
            try FileManager.default.removeItem(at:urls[2]);try store.refresh();canvas.refreshFromStore();canvas.scrollBy(-100000);try emit("unavailable")
        }
        try require(NSApp.windows.isEmpty,"No reference window may be created")
        let result:[String:Any]=["schemaVersion":1,"scope":"unchanged FileShelfCanvas detached model layers and original CA descriptors","cases":cases,"rasterAssets":encoder.rasterAssets,"unsupported":encoder.unsupported,
            "isolation":["windowCreated":false,"screenCapture":false,"temporaryFilesAndStoresOnly":true,"nativeCursorCalls":false],
            "notVerified":["Windows native file icons","cross-platform fonts and antialiasing","Core Animation presentation pixels","Windows native interaction"]]
        try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys,.withoutEscapingSlashes]).write(to:out.appendingPathComponent("shelf.json"),options:.withoutOverwriting)
        print("Exported \(cases.count) detached original shelf states and \(encoder.rasterAssets.count) intrinsic icon assets")
    }
    static func main(){do{try run()}catch{fputs("Shelf source reference: \(error)\n",stderr);exit(1)}}
}
