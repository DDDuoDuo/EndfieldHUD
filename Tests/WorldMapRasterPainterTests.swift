import Foundation
import CoreGraphics

enum WorldMapRasterPainterTests {
    static func run() -> Int {
        var count=0
        func check(_ condition:Bool,_ message:String,file:StaticString=#file,line:UInt=#line) {
            count += 1; if !condition { fatalError(message,file:file,line:line) }
        }
        func countryFixture() throws -> WorldMapCountries {
            var data=Data("EHUDCTY1".utf8)
            func u32(_ value:UInt32) { for byte in 0..<4 { data.append(UInt8(truncatingIfNeeded:value>>(8*byte))) } }
            func u16(_ value:UInt16) { data.append(UInt8(truncatingIfNeeded:value));data.append(UInt8(truncatingIfNeeded:value>>8)) }
            u32(1);data.append(3);data.append(contentsOf:"TST".utf8);u16(8);data.append(contentsOf:"Testland".utf8)
            u32(1);u32(2)
            for rect in [CGRect(x:480,y:200,width:64,height:80),CGRect(x:488,y:240,width:8,height:10)] {
                u32(4)
                for p in [CGPoint(x:rect.minX,y:rect.minY),CGPoint(x:rect.maxX,y:rect.minY),
                          CGPoint(x:rect.maxX,y:rect.maxY),CGPoint(x:rect.minX,y:rect.maxY)] {
                    u32(UInt32((p.x/1024*16777215).rounded()));u32(UInt32((p.y/512*16777215).rounded()))
                }
            }
            return try WorldMapCountries(data:data)
        }
        func terrainFixture() throws -> WorldMapTerrain {
            var data=Data("EHUDMAP1".utf8)
            func u32(_ value:UInt32) { for byte in 0..<4 { data.append(UInt8(truncatingIfNeeded:value>>(8*byte))) } }
            func u16(_ value:UInt16) { data.append(UInt8(truncatingIfNeeded:value));data.append(UInt8(truncatingIfNeeded:value>>8)) }
            u32(1);u32(500);u32(1);u32(0);data.append(0);u32(2)
            for p in [CGPoint(x:490,y:290),CGPoint(x:535,y:290)] {
                u16(UInt16((p.x/1024*65535).rounded()));u16(UInt16((p.y/512*65535).rounded()))
            }
            return try WorldMapTerrain(data:data)
        }
        func pixel(_ frame:WorldMapRasterFrame,_ point:CGPoint)->[UInt8] {
            let x=min(frame.image.width-1,max(0,Int(((point.x-frame.screenRect.minX)*frame.pixelsPerPoint).rounded())))
            let y=min(frame.image.height-1,max(0,Int(((point.y-frame.screenRect.minY)*frame.pixelsPerPoint).rounded())))
            let bytes=frame.image.dataProvider!.data! as Data
            let offset=y*frame.image.bytesPerRow+x*4
            return Array(bytes[offset..<offset+4])
        }
        func local(_ p:CGPoint,_ camera:WorldMapViewport)->CGPoint {
            WorldMapGeometry.screen(x:p.x/1024,y:p.y/512,viewport:camera)
        }
        do {
            let countries=try countryFixture(), terrain=try terrainFixture()
            let painter=WorldMapRasterPainter(terrain:terrain,countries:countries)
            let camera=WorldMapViewport(centerX:0.5,centerY:0.5,zoom:2.1)
            let blue=CGColor(srgbRed:0.10,green:0.40,blue:1,alpha:1)
            let request=WorldMapRasterRequest(viewport:camera,dark:true,accent:blue,contentsScale:2)
            let frame=painter.render(request)!
            check(frame.image.width==1392 && frame.image.height==1392 && frame.byteCount==1392*1392*4,
                  "The default padded frame has a fixed bounded pixel and memory cost")
            check(frame.screenRect==CGRect(x:-128,y:-128,width:696,height:696),
                  "Gesture padding covers both sides of the logical map viewport")
            check(abs(frame.worldRect.midX-camera.centerX*1024)<0.000001 && abs(frame.worldRect.midY-camera.centerY*512)<0.000001,
                  "The raster frame records the same geographic plane used by pins")
            check(frame.geometryMilliseconds>=0 && frame.drawingMilliseconds>=0,"Raster timings exclude no negative or invalid durations")
            let northernLand=pixel(frame,local(CGPoint(x:512,y:212),camera))
            let southernSea=pixel(frame,local(CGPoint(x:512,y:300),camera))
            check(Int(northernLand[2])>Int(northernLand[0])+25,
                  "The focused country's face uses the requested theme color")
            let offshore=WorldMapViewport(centerX:0.4,centerY:0.5,zoom:2.1)
            let offshoreFrame=painter.render(WorldMapRasterRequest(viewport:offshore,dark:true,accent:blue,contentsScale:2))!
            let offCenterLand=pixel(offshoreFrame,local(CGPoint(x:512,y:212),offshore))
            check(abs(Int(offCenterLand[2])-Int(offCenterLand[0]))<15,
                  "A visible off-center country stays neutral when the map center is over ocean")
            let returned=painter.render(request)!
            check(returned.image.dataProvider!.data! as Data == frame.image.dataProvider!.data! as Data,
                  "Returning the center restores exactly the original baked highlight without retained hover state")
            check(southernSea[0]<70 && southernSea[1]<70 && southernSea[2]<70,
                  "The raster's north/south orientation matches geographic map coordinates")
            check(pixel(frame,local(CGPoint(x:492,y:244),camera))[2]<northernLand[2],
                  "Interior holes remain unfilled in the rendered country face")
            check(pixel(frame,CGPoint(x:-120,y:-120))[3]==255 && southernSea[3]==255,
                  "The detail background is opaque so a backdrop cannot darken a square around it")
            let pink=CGColor(srgbRed:1,green:0.15,blue:0.25,alpha:1)
            let recolored=painter.render(WorldMapRasterRequest(viewport:camera,dark:false,accent:pink,contentsScale:1))!
            let pinkLand=pixel(recolored,local(CGPoint(x:512,y:212),camera))
            check(pinkLand[0]>pinkLand[2],"Reusing indexed geometry never reuses stale theme colors")
            let huge=painter.render(WorldMapRasterRequest(viewport:camera,dark:true,accent:blue,contentsScale:100,padding:2000))!
            check(huge.image.width==WorldMapRasterPainter.maximumPixelDimension && huge.byteCount<=1536*1536*4,
                  "Large HUD scales cannot allocate a huge map bitmap")
            check(painter.render(request,isCancelled:{true})==nil,"Cancelled work returns before allocating or publishing a frame")
            var cancellationChecks=0
            let interrupted=painter.render(request,isCancelled:{cancellationChecks += 1;return cancellationChecks>4})
            check(interrupted==nil && cancellationChecks>4,"A superseded request can stop between geographic passes")
            let invalid=WorldMapRasterRequest(viewport:camera,dark:true,accent:blue,contentsScale:.nan)
            check(painter.render(invalid)==nil,"An invalid render scale never reaches bitmap allocation")
            let empty=WorldMapRasterPainter(terrain:nil,countries:nil)
            check(empty.render(request) != nil,"The painter has a bounded background fallback before data loads")
            check(WorldMapRasterPainter(terrain:terrain,countries:nil).render(request) != nil,
                  "Real contour rendering works independently when countries are unavailable")
            check(WorldMapRasterPainter(terrain:nil,countries:countries).render(request) != nil,
                  "Country relief works independently when elevation data is unavailable")
            let retainedBeforeBackdrop=painter.retainedCountryGeometryCount
            let backdrop=painter.renderBackdrop(dark:true)!
            check(painter.retainedCountryGeometryCount==retainedBeforeBackdrop,
                  "The backdrop does not retain duplicate whole-country geometry alongside detailed components")
            let freshPainter=WorldMapRasterPainter(terrain:terrain,countries:countries)
            _=freshPainter.renderBackdrop(dark:false)
            check(freshPainter.retainedCountryGeometryCount==0,
                  "A backdrop-only painter keeps no world-country geometry indexes alive")
            check(backdrop.width==1024 && backdrop.height==512 && backdrop.bytesPerRow*backdrop.height==2*1024*1024,
                  "The whole-world emergency backdrop is bounded to two MiB")
            check(painter.renderBackdrop(dark:true,isCancelled:{true})==nil,"Backdrop generation respects cancellation")
            let seam=WorldMapViewport(centerX:0.998,centerY:0.5,zoom:2.1)
            let wrapped=painter.render(WorldMapRasterRequest(viewport:seam,dark:true,accent:blue,contentsScale:1))!
            check(wrapped.worldRect.minX<1024 && wrapped.worldRect.maxX>1024,
                  "The raster frame remains continuous across the antimeridian for immediate compositor movement")
        } catch { fatalError("Raster painter fixtures failed: \(error)") }
        return count
    }
}
