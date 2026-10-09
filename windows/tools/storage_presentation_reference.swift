import AppKit
import QuartzCore
private final class StoragePresentationTimer: StorageRefreshTimer {func invalidate(){}}
@main enum StoragePresentationReference {
    static func main() throws {
        let a=CommandLine.arguments;guard a.count==4,a[1]=="--ui-test",a[2]=="--output",ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil else{fatalError("Use storage_presentation_reference.sh")}
        let out=URL(fileURLWithPath:a[3],isDirectory:true),encoder=try ModuleReferenceLayerEncoder(output:out)
        NSApplication.shared.setActivationPolicy(.prohibited)
        var config=AppConfiguration.defaults;config.language = .english;config.ambientAnimation=false;config.reduceMotion=false;config.launchAtLogin=false;HUDRuntimeAppearance.configuration=config;L10n.language = .english
        func rgba(_ color:NSColor)->[CGFloat]{let c=color.usingColorSpace(.sRGB)!;return[c.redComponent,c.greenComponent,c.blueComponent,c.alphaComponent]}
        var rows:[[String:Any]]=[]
        for dark in [true,false]{
            NSApp.appearance=NSAppearance(named:dark ? .darkAqua:.aqua)
            var jobs:[()->Void]=[],read=0
            let controller=StorageController(clock:{Date(timeIntervalSince1970:100)},readCapacity:{read+=1;return read==2 ? nil:StorageCapacity(volumeName:"Owned startup fixture",totalBytes:1_000_000_000_000,availableBytes:420_000_000_000,updatedAt:Date(timeIntervalSince1970:100))},scanDetails:{_ in fatalError("Storage canvas must not auto-scan folders")},runWorker:{jobs.append($0)},scheduleTimer:{_,_ in StoragePresentationTimer()})
            let canvas=StorageCanvas(controller:controller,reduceMotion:{false});defer{canvas.deactivate()}
            _=canvas.makeContent(for:.storage,style:HUDModuleContentStyle(dark:dark,accent:config.accentColor,contentsScale:2))
            func emit(_ name:String) throws {
                rows.append(["name":(dark ? "dark-":"light-")+name,"dark":dark,"root":try encoder.encode(canvas.layer,id:"storage"),"status":canvas.accessibilityStatus,"actions":canvas.accessibleActions.map{["id":$0.id,"label":$0.label,"rect":ModuleReferenceLayerEncoder.rect($0.rect)]},"accent":rgba(TelemetryArtwork.yellow),"cyan":rgba(TelemetryArtwork.cyan)])
            }
            try emit("initial");canvas.activate();try emit("loading");jobs.removeFirst()();try emit("ready");canvas.perform(actionID:"storage:refresh");try emit("refreshing");jobs.removeFirst()();try emit("failed-with-capacity");canvas.deactivate()
        }
        guard NSApp.windows.isEmpty else{fatalError("Detached fixture must not create windows")}
        let value:[String:Any]=["schemaVersion":1,"scope":"unchanged StorageCanvas detached original layers","cases":rows,"rasterAssets":encoder.rasterAssets,"unsupported":encoder.unsupported,"filesystemQueries":false,"windowsCreated":false]
        try JSONSerialization.data(withJSONObject:value,options:[.sortedKeys,.withoutEscapingSlashes]).write(to:out.appendingPathComponent("storage.json"),options:.withoutOverwriting)
        print("Exported \(rows.count) detached Storage states")
    }
}
