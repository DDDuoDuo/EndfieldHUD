import Foundation

private final class StorageOracleTimer: StorageRefreshTimer {
    var alive=true;let callback:()->Void
    init(_ callback:@escaping()->Void){self.callback=callback}
    func invalidate(){alive=false}
}
@main enum StorageStateReference {
    static func main() throws {
        guard CommandLine.arguments.count==2 else{fatalError("Pass a NEW output JSON path")}
        let output=URL(fileURLWithPath:CommandLine.arguments[1]);guard !FileManager.default.fileExists(atPath:output.path)else{fatalError("Refusing to overwrite oracle")}
        var now:Double=0,workers:[()->Void]=[],timers:[StorageOracleTimer]=[],reads=0,scans=0,revision=0
        let controller=StorageController(clock:{Date(timeIntervalSince1970:now)},readCapacity:{
            reads += 1
            return reads==2 ? nil:StorageCapacity(volumeName:"Owned startup fixture",totalBytes:1_000_000,availableBytes:Int64(400_000+reads),updatedAt:Date(timeIntervalSince1970:now))
        },scanDetails:{ token in
            scans += 1
            return StorageDetailsSnapshot(categories:[StorageCategoryUsage(id:"documents",title:"Documents",bytes:12345,isPartial:token.isCancelled)],isLoading:false,updatedAt:Date(timeIntervalSince1970:now),isPartial:token.isCancelled,error:nil)
        },runWorker:{workers.append($0)},scheduleTimer:{_,callback in let timer=StorageOracleTimer(callback);timers.append(timer);return timer})
        let observer=controller.observe{revision += 1};defer{controller.removeObserver(observer)}
        var rows:[[String:Any]]=[]
        func optional<T>(_ value:T?)->Any{value as Any? ?? NSNull()}
        func step(_ action:String,_ t:Double) {
            now=t
            switch action {
            case "activate":controller.activate()
            case "deactivate":controller.deactivate()
            case "refresh":controller.refresh()
            case "details":controller.requestDetails()
            case "details-force":controller.requestDetails(refresh:true)
            case "work":if !workers.isEmpty{workers.removeFirst()()}
            case "wake":timers.filter{$0.alive}.forEach{$0.callback()}
            default:fatalError("Unknown fixture action")
            }
            let c=controller.snapshot,d=controller.details
            rows.append(["action":action,"now":now,"active":controller.isActive,"revision":revision,"workers":workers.count,"timers":timers.filter{$0.alive}.count,
                "capacity":c.capacity.map{["name":$0.volumeName,"total":$0.totalBytes,"available":$0.availableBytes,"date":$0.updatedAt.timeIntervalSince1970] as [String:Any]} as Any? ?? NSNull(),"loading":c.isLoading,"error":optional(c.error),
                "details":["categories":d.categories.map{["id":$0.id,"title":$0.title,"bytes":optional($0.bytes),"partial":$0.isPartial]},"loading":d.isLoading,"date":d.updatedAt.map{$0.timeIntervalSince1970} as Any? ?? NSNull(),"partial":d.isPartial,"error":optional(d.error)]])
        }
        // Coalesced reads, hidden completion, cancelled details, reopen before
        // worker completion, cache edges, forced refresh, backward wall clock.
        for (action,time) in [("activate",0.0),("activate",0),("refresh",0),("details",0),("work",1),("deactivate",2),("activate",3),("details",3),("work",4),("work",5),("details",6),("wake",63),("refresh",64),("deactivate",65),("work",66),("activate",67),("details",67),("details-force",68),("work",69),("details",968),("details",969),("work",970),("details",-1),("work",-1),("work",-1),("deactivate",0),("refresh",1),("details",1)]{step(action,time)}
        let data=try JSONSerialization.data(withJSONObject:["schemaVersion":1,"source":"unchanged Sources/StorageController.swift","filesystemReads":false,"privateTimers":false,"steps":rows],options:[.sortedKeys,.withoutEscapingSlashes])
        try data.write(to:output,options:.withoutOverwriting);print("Exported \(rows.count) original Storage controller states; no filesystem capacity/scan called")
    }
}
