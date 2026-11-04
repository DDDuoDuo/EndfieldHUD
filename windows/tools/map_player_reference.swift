import AppKit
import ImageIO

// The separately compiled WorldMapPinArtwork is the untouched original source.
// Only its resource/localization/model dependencies are replaced by inert shims.
enum HUDResources {
    static func url(for name:String)->URL? { URL(fileURLWithPath:CommandLine.arguments[1]).appendingPathComponent(name) }
}
enum MapPinStyle {case yellow,green,player}
enum L10n {static func text(_ english:String,_ chinese:String)->String {english}}

@main struct MapPlayerReference {
 static func main() throws {
    let directory=URL(fileURLWithPath:CommandLine.arguments[2])
    let srgb=CGColorSpace(name:CGColorSpace.sRGB)!
    var images:[[String:Any]]=[]
    for (name,optionalImage) in [("halo",WorldMapPinArtwork.playerHalo),("beam",WorldMapPinArtwork.playerBeam),("glyph",WorldMapPinArtwork.playerGlyph)] {
        guard let image=optionalImage else {fatalError("Missing original player image")}
        let width=image.width,height=image.height
        let context=CGContext(data:nil,width:width,height:height,bitsPerComponent:8,bytesPerRow:width*4,space:srgb,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)!
        context.draw(image,in:CGRect(x:0,y:0,width:width,height:height))
        let source=context.data!.assumingMemoryBound(to:UInt8.self)
        var straight=[UInt8](repeating:0,count:width*height*4)
        var maximumRoundTrip=0
        for p in stride(from:0,to:straight.count,by:4) {
            let alpha=Int(source[p+3]);straight[p+3]=UInt8(alpha)
            for c in 0..<3 {
                let value=Int(source[p+c])
                straight[p+c]=alpha == 0 ? 0 : UInt8(min(255,(value*255+alpha/2)/alpha))
                maximumRoundTrip=max(maximumRoundTrip,abs((Int(straight[p+c])*alpha+127)/255-value))
            }
        }
        try Data(straight).write(to:directory.appendingPathComponent(name+".rgba"))
        images.append(["name":name,"width":width,"height":height,"bytes":straight.count,"premultipliedRoundTripMaximum":maximumRoundTrip])
    }
    func components(_ color:NSColor)->[Double] {let converted=color.usingColorSpace(.sRGB)!;return [converted.redComponent,converted.greenComponent,converted.blueComponent,converted.alphaComponent].map{Double($0)}}
    let colors:[String:Any] = ["calibratedGray":[0.12,0.81,0.95,0.13,0.08,0.94,0.06].map{["white":$0,"sRGB":components(NSColor(white:$0,alpha:1))]},"yellow":components(WorldMapPinArtwork.yellow),"green":components(WorldMapPinArtwork.green)]
    let report:[String:Any] = ["images":images,"colors":colors]
    try JSONSerialization.data(withJSONObject:report,options:[.sortedKeys]).write(to:directory.appendingPathComponent("output.json"))
    print("Exported three unchanged-size original player images")
 }
}
