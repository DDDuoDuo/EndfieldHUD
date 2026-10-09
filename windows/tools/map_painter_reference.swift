import Foundation
import CoreGraphics
enum HUDResources {static func url(for value:String)->URL?{nil}}
func rgba(_ image:CGImage,_ x:Int,_ y:Int)->[Int]{let d=image.dataProvider!.data!;let p=CFDataGetBytePtr(d)!;let offset=y*image.bytesPerRow+x*4;return (0..<4).map{Int(p[offset+$0])}}
func samples(_ image:CGImage,_ positions:[(String,CGPoint)])->[[String:Any]]{var output:[[String:Any]]=[]
 for (label,point) in positions{let cx=Int(floor(point.x)),cy=Int(floor(point.y));var best:(Int,Int,Int)?
  for dy in -8...8{for dx in -8...8{let x=cx+dx,y=cy+dy;guard x>=3,y>=3,x+3<image.width,y+3<image.height else{continue};let color=rgba(image,x,y);var flat=true
   for oy in -1...1{for ox in -1...1{if rgba(image,x+ox,y+oy) != color{flat=false}}}
   if flat{let score=abs(dx)+abs(dy);if best == nil || score<best!.2{best=(x,y,score)}}
  }}
  let x=best?.0 ?? cx,y=best?.1 ?? cy;precondition(x>=3 && y>=3 && x+3<image.width && y+3<image.height)
  var mean=[Double](repeating:0,count:4);for oy in -3...3{for ox in -3...3{let c=rgba(image,x+ox,y+oy);for i in 0..<4{mean[i] += Double(c[i])/49}}}
  output.append(["label":label,"x":x,"y":y,"rgba":rgba(image,x,y),"flat3x3":best != nil,"meanRegion":["x":x-3,"y":y-3,"width":7,"height":7,"rgba":mean]])
 };return output}
let directory=URL(fileURLWithPath:CommandLine.arguments[1]);let terrainData=try Data(contentsOf:directory.appendingPathComponent("terrain.bin")),countriesData=try Data(contentsOf:directory.appendingPathComponent("countries.bin"))
let terrain=try WorldMapTerrain(data:terrainData),countries=try WorldMapCountries(data:countriesData)
let painter=WorldMapRasterPainter(terrain:terrain,countries:countries)
let targets:[(String,CGPoint)]=[("ocean",CGPoint(x:750,y:140)),("countryA",CGPoint(x:835,y:205)),("countryAUpper",CGPoint(x:825,y:160)),("holeA",CGPoint(x:807,y:176)),("countryB",CGPoint(x:892,y:213))]
let accent:[CGFloat]=[0.98,0.83,0.12,1];let color=CGColor(colorSpace:CGColorSpace(name:CGColorSpace.sRGB)!,components:accent)!
var frames:[[String:Any]]=[],backdrops:[[String:Any]]=[]
for dark in [true,false]{
 for camera in [WorldMapViewport(),WorldMapViewport(centerX:0.879,centerY:0.4,zoom:3)]{
  let frame=painter.render(WorldMapRasterRequest(viewport:camera,dark:dark,accent:color,contentsScale:1))!;let factor=440*camera.zoom/1024,origin=CGPoint(x:220-camera.centerX*440*camera.zoom,y:220-camera.centerY*220*camera.zoom)
  let positions=targets.map{label,p in (label,CGPoint(x:(origin.x+p.x*factor-frame.screenRect.minX)*frame.pixelsPerPoint,y:(origin.y+p.y*factor-frame.screenRect.minY)*frame.pixelsPerPoint))}
  frames.append(["camera":[camera.centerX,camera.centerY,camera.zoom],"dark":dark,"accent":accent,"scale":1,"padding":128,"pixelWidth":frame.image.width,"pixelHeight":frame.image.height,"screenRect":[frame.screenRect.minX,frame.screenRect.minY,frame.screenRect.width,frame.screenRect.height],"pixelsPerPoint":frame.pixelsPerPoint,"samples":samples(frame.image,positions)])
 }
 let image=painter.renderBackdrop(dark:dark)!;backdrops.append(["dark":dark,"pixelWidth":image.width,"pixelHeight":image.height,"samples":samples(image,targets)])
}
func hex(_ data:Data)->String{data.map{String(format:"%02x",$0)}.joined()}
let output:[String:Any]=["terrainHex":hex(terrainData),"countriesHex":hex(countriesData),"detailFrames":frames,"backdropFrames":backdrops]
try JSONSerialization.data(withJSONObject:output,options:[.sortedKeys]).write(to:directory.appendingPathComponent("output.json"))
