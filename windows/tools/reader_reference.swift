import AppKit
import Foundation

// Dependency shims only: original ReaderStore/Document/Canvas/Controller source
// is compiled unchanged. This is a state/geometry oracle, not an artwork oracle.
enum L10n {static func text(_ english:String,_ chinese:String)->String {english}}
enum HUDModule {case reader}
struct HUDModuleContentStyle {var dark=true;var contentsScale:CGFloat=2}
protocol HUDModuleContentFactory {func makeContent(for:HUDModule,style:HUDModuleContentStyle)->CALayer}
enum HUDRuntimeAppearance {static var reduceMotion=false;static var accent=NSColor.systemYellow}
enum HUDControlHighlightLayer {enum Shape {case cutCorner};static func add(to:CALayer,rect:CGRect,shape:Shape,enabled:Bool,framed:Bool){}}
final class ShelfFileAccess {func close(){}}

func readerReferenceImage()->CGImage {
    let context=CGContext(data:nil,width:2,height:2,bitsPerComponent:8,bytesPerRow:8,space:CGColorSpaceCreateDeviceRGB(),bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
    return context.makeImage()!
}
func readerReferencePage(_ index:Int,_ illustration:Bool)->ReaderPage {
    ReaderPage(location:ReaderLocation(section:index),next:index<9 ? ReaderLocation(section:index+1):nil,
        previous:index>0 ? ReaderLocation(section:index-1):nil,progress:Double(index)/9,
        image:readerReferenceImage(),summary:"Synthetic page \(index)",isIllustration:illustration)
}
func box(_ r:CGRect)->[Double] {[r.minX,r.minY,r.width,r.height]}

@main struct ReaderReference {
    static func main()throws {
        guard CommandLine.arguments.count==3,CommandLine.arguments[1]=="--output" else {throw CocoaError(.fileReadInvalidFileName)}
        let output=URL(fileURLWithPath:CommandLine.arguments[2],isDirectory:true)
        guard !FileManager.default.fileExists(atPath:output.appendingPathComponent("reader-reference.json").path) else {throw CocoaError(.fileWriteFileExists)}
        let temporary=FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldReaderOracle-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at:temporary,withIntermediateDirectories:true)
        defer {try? FileManager.default.removeItem(at:temporary)}
        let bookID=UUID(uuidString:"A0000000-0000-4000-8000-000000000001")!
        let seed:[String:Any] = ["version":1,"books":[["id":bookID.uuidString,"bookmark":"AQ==","scoped":false,"path":"/owned-synthetic-unopened.pdf","title":"Synthetic book","location":["section":3,"block":0,"character":0],"progress":1.0/3,"bookmarks":[]]],"selected":bookID.uuidString,"preferences":["fontName":"Georgia","fontSize":10,"lineSpacing":2,"margin":16,"rightToLeft":false,"continuous":true]]
        let data=try JSONSerialization.data(withJSONObject:seed,options:[.sortedKeys])
        var cases:[[String:Any]]=[]
        let steps:[[String:Any]] = [
            ["kind":"zoom","factor":2.0,"x":200.0,"y":215.0],
            ["kind":"zoom","factor":1.7,"x":61.0,"y":99.0],
            ["kind":"scroll","dx":0.0,"dy":333.0],
            ["kind":"scroll","dx":0.0,"dy":334.0],
            ["kind":"scroll","dx":0.0,"dy":-44.5],
            ["kind":"scroll","dx":70.0,"dy":2.0],
            ["kind":"scroll","dx":-70.0,"dy":1.0],
            ["kind":"scroll","dx":12.25,"dy":-20.75],
            ["kind":"turn","direction":1], ["kind":"turn","direction":-1],
            ["kind":"magnify","amount":0.4,"x":25.0,"y":60.0],
            ["kind":"seek","fraction":0.72], ["kind":"pan","dx":43.5,"dy":-81.25]
        ]
        for vertical in [false,true] {for illustration in [false,true] {for zoom in [false,true] {for (n,event) in steps.enumerated() {
            let dir=temporary.appendingPathComponent("\(cases.count)");try FileManager.default.createDirectory(at:dir,withIntermediateDirectories:true);try data.write(to:dir.appendingPathComponent("library.json"))
            let store=try ReaderStore(directory:dir);let controller=ReaderController(store:store)
            var prefs=ReaderPreferences();prefs.vertical=vertical
            controller.readerReferenceConfigure(prefs,bookID:bookID,illustration:illustration)
            let canvas=ReaderCanvas(controller:controller);_ = canvas.makeContent(for:.reader,style:HUDModuleContentStyle());canvas.activate()
            if zoom { _ = canvas.magnify(at:CGPoint(x:200,y:215),amount:1) }
            let before=canvas.readerReferenceFacts()
            switch event["kind"] as! String {
            case "zoom": _ = canvas.magnify(at:CGPoint(x:event["x"] as! Double,y:event["y"] as! Double),amount:(event["factor"] as! Double)-1)
            case "scroll": _ = canvas.scroll(at:CGPoint(x:200,y:215),deltaX:event["dx"] as! Double,deltaY:event["dy"] as! Double,timestamp:10)
            case "turn":canvas.turnPage(event["direction"] as! Int)
            case "magnify":_ = canvas.magnify(at:CGPoint(x:event["x"] as! Double,y:event["y"] as! Double),amount:event["amount"] as! Double)
            case "seek":let r=canvas.progressRect;_ = canvas.mouseDown(at:CGPoint(x:r.minX+r.width*(event["fraction"] as! Double),y:r.midY));canvas.mouseUp()
            default:_ = canvas.mouseDown(at:CGPoint(x:200,y:215));canvas.mouseDragged(to:CGPoint(x:200+(event["dx"] as! Double),y:215+(event["dy"] as! Double)));canvas.mouseUp()
            }
            cases.append(["name":"\(vertical ? "vertical":"horizontal")-\(illustration ? "image":"text")-\(zoom ? "zoomed":"base")-\(n)","vertical":vertical,"illustration":illustration,"initialZoom":zoom,"event":event,"before":before,"after":canvas.readerReferenceFacts()])
            canvas.deactivate()
            controller.readerReferenceDrain()
        }}}}
        let libraryDir=temporary.appendingPathComponent("store");try FileManager.default.createDirectory(at:libraryDir,withIntermediateDirectories:true);try data.write(to:libraryDir.appendingPathComponent("library.json"));let store=try ReaderStore(directory:libraryDir)
        var storeCases:[[String:Any]]=[]
        func capture(_ name:String)throws {let bytes=try Data(contentsOf:libraryDir.appendingPathComponent("library.json"));storeCases.append(["name":name,"library":try JSONSerialization.jsonObject(with:bytes)])}
        try capture("seed");try store.saveProgress(ReaderLocation(section:4,block:2,character:42),progress:2,id:bookID);try capture("clamped-progress");try store.toggleBookmark(id:bookID);try capture("bookmark-added");try store.toggleBookmark(id:bookID);try capture("bookmark-removed");var prefs=ReaderPreferences();prefs.rightToLeft=true;prefs.vertical=false;try store.setPreferences(prefs);try capture("horizontal-clears-legacy-rtl");try store.remove(bookID);try capture("removed-reference-only")
        let result:[String:Any]=["schemaVersion":1,"cases":cases,"storeCases":storeCases,"limitations":["Geometry/state only: highlight helper is a no-op dependency shim; no visual parity claim","Original Reader implementations use synthetic 2x2 rasters/opaque references; no real book opened","No NSApplication activation or key/main/visible window, and main run loop is not pumped","Asynchronous detail raster/decoder failure callbacks are not oracle outputs"]]
        try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]).write(to:output.appendingPathComponent("reader-reference.json"),options:.atomic)
        print("Original Reader oracle: \(cases.count) navigation cases, \(storeCases.count) actual store snapshots; no visible window or real books")
    }
}
