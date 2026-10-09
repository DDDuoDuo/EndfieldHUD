import Foundation
import Darwin
@main enum StorageFilesReference {
    static func main() throws {
        guard CommandLine.arguments.count==2 else{fatalError("Pass new output JSON path")}
        let output=URL(fileURLWithPath:CommandLine.arguments[1]);guard !FileManager.default.fileExists(atPath:output.path)else{fatalError("Refusing overwrite")}
        let fm=FileManager.default,root=fm.temporaryDirectory.appendingPathComponent("EndfieldStorageScan-"+UUID().uuidString,isDirectory:true)
        try fm.createDirectory(at:root.appendingPathComponent("one/child"),withIntermediateDirectories:true);defer{try?fm.removeItem(at:root)}
        try fm.createDirectory(at:root.appendingPathComponent("two"),withIntermediateDirectories:true)
        try Data(repeating:0x41,count:1234).write(to:root.appendingPathComponent("one/a"));try Data(repeating:0x42,count:5678).write(to:root.appendingPathComponent("one/child/b"))
        try fm.linkItem(at:root.appendingPathComponent("one/a"),to:root.appendingPathComponent("two/alias"))
        try fm.createSymbolicLink(atPath:root.appendingPathComponent("one/link").path,withDestinationPath:root.appendingPathComponent("two").path)
        try fm.createSymbolicLink(atPath:root.appendingPathComponent("linked-root").path,withDestinationPath:root.appendingPathComponent("one").path)
        func tree(_ url:URL)throws->[String:Any]{var s=stat();guard lstat(url.path,&s)==0 else{throw CocoaError(.fileReadUnknown)}
            let kind=s.st_mode&S_IFMT,tag=kind==S_IFDIR ? "directory":kind==S_IFREG ? "regular":kind==S_IFLNK ? "symbolicLink":"other"
            var value:[String:Any]=["kind":tag,"volume":String(s.st_dev),"file":String(s.st_ino),"bytes":Int64(s.st_blocks)*512,"children":[]]
            if kind==S_IFDIR {guard let dir=opendir(url.path)else{throw CocoaError(.fileReadUnknown)};defer{closedir(dir)};var rows:[[String:Any]]=[]
                while let entry=readdir(dir){let name=withUnsafePointer(to:&entry.pointee.d_name){$0.withMemoryRebound(to:CChar.self,capacity:Int(MAXNAMLEN)+1){String(cString:$0)}};if name=="."||name==".."{continue};rows.append(["name":name,"node":try tree(url.appendingPathComponent(name))])};value["children"]=rows}
            return value
        }
        let names=["one","two","linked-root","missing"],scopes=names.map{StorageFolderScope(id:$0,title:$0,url:root.appendingPathComponent($0))}
        var graph:[String:Any]=[:];for name in names where name != "missing" {graph[name]=try tree(root.appendingPathComponent(name))}
        var configurations:[(String,StorageScanLimits,Bool)]=[("full",StorageScanLimits(),false)]
        var depth=StorageScanLimits();depth.maximumDepth=0;configurations.append(("depth-zero",depth,false))
        var per=StorageScanLimits();per.entriesPerFolder=2;configurations.append(("per-folder",per,false))
        var total=StorageScanLimits();total.maximumEntries=3;configurations.append(("total",total,false))
        var expired=StorageScanLimits();expired.maximumSeconds=0;configurations.append(("deadline",expired,false));configurations.append(("cancelled",StorageScanLimits(),true))
        let optional:(Int64?)->Any = { $0 as Any? ?? NSNull() }
        var cases:[[String:Any]]=[]
        for(name,limits,cancelled)in configurations {let cancel=StorageScanCancellation();if cancelled{cancel.cancel()};let result=StorageFolderScanner.scan(scopes:scopes,cancellation:cancel,date:Date(timeIntervalSince1970:123),limits:limits,uptime:{0})
            cases.append(["name":name,"cancelled":cancelled,"limits":["maximumEntries":limits.maximumEntries,"maximumSeconds":limits.maximumSeconds,"entriesPerFolder":limits.entriesPerFolder,"secondsPerFolder":limits.secondsPerFolder,"maximumDepth":limits.maximumDepth],"categories":result.categories.map{["id":$0.id,"title":$0.title,"bytes":optional($0.bytes),"partial":$0.isPartial]},"partial":result.isPartial,"error":result.error as Any? ?? NSNull()])}
        let data=try JSONSerialization.data(withJSONObject:["schemaVersion":1,"source":"unchanged Sources/StorageController.swift","ownedTemporaryFilesOnly":true,"graph":graph,"scopes":names,"cases":cases],options:[.sortedKeys,.withoutEscapingSlashes]);try data.write(to:output,options:.withoutOverwriting);print("Exported \(cases.count) actual original metadata scan cases; owned temporary tree removed")
    }
}
