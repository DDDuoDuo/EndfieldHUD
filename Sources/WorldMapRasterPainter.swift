import Foundation
import CoreGraphics

struct WorldMapRasterRequest {
    let viewport: WorldMapViewport
    let dark: Bool
    let accent: CGColor
    let contentsScale: CGFloat
    var padding: CGFloat = 128
}

struct WorldMapRasterFrame {
    let image: CGImage
    let viewport: WorldMapViewport
    /// Logical map coordinates covered by the image, including gesture padding.
    let screenRect: CGRect
    /// Continuous (unwrapped) world coordinates for this image.
    let worldRect: CGRect
    let pixelsPerPoint: CGFloat
    let geometryMilliseconds: Double
    let drawingMilliseconds: Double
    var byteCount: Int { image.bytesPerRow * image.height }
}

/// Worker-confined direct Core Graphics painter. One finite bitmap replaces
/// hundreds of geographic shape layers; pins and interactive controls stay in
/// the compositor. No layer-tree rendering, network access, or idle work occurs.
final class WorldMapRasterPainter {
    static let maximumPixelDimension = 1536
    private let terrain: WorldMapTerrain?
    private let countries: WorldMapCountries?
    private var countryGeometry: [String: WorldMapPathGeometry] = [:]
    private var terrainGeometry: [Int: WorldMapPathGeometry] = [:]
    private var terrainFillGeometry: [Int: WorldMapPathGeometry] = [:]
    private static let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!
    /// Internal resource-budget diagnostic; backdrop generation must not grow it.
    var retainedCountryGeometryCount: Int { countryGeometry.count }

    init(terrain: WorldMapTerrain?, countries: WorldMapCountries?) {
        self.terrain = terrain; self.countries = countries
    }

    /// Called only by the owner’s single background worker. Cancellation is
    /// checked between geometry and drawing passes; a cancelled frame is never
    /// returned for publication or retained in a hidden image cache.
    func render(_ request: WorldMapRasterRequest, isCancelled: () -> Bool = { false }) -> WorldMapRasterFrame? {
        guard !isCancelled(), request.viewport.centerX.isFinite, request.viewport.centerY.isFinite,
              request.viewport.zoom.isFinite, request.viewport.zoom > 0,
              request.contentsScale.isFinite, request.contentsScale > 0, request.padding.isFinite else { return nil }
        let viewport = WorldMapGeometry.constrained(request.viewport)
        let padding = min(128,max(0,request.padding))
        let screen = CGRect(origin:.zero,size:WorldMapGeometry.size).insetBy(dx:-padding,dy:-padding)
        let pixels = min(Self.maximumPixelDimension,max(1,Int(ceil(screen.width*min(8,request.contentsScale)))))
        let pixelScale = CGFloat(pixels)/screen.width
        let factor = CGFloat(440*viewport.zoom)/1024
        let origin = CGPoint(x:220-viewport.centerX*440*viewport.zoom,y:220-viewport.centerY*220*viewport.zoom)
        let worldRect = CGRect(x:(screen.minX-origin.x)/factor,y:(screen.minY-origin.y)/factor,
                               width:screen.width/factor,height:screen.height/factor)
        // Subpixel simplification uses octave buckets, retaining the two most
        // recent country levels without regenerating them for every wheel tick.
        let tolerance = 0.28/pow(2,ceil(log2(factor)))
        let started = ProcessInfo.processInfo.systemUptime
        var countryFaces: [(fill:CGPath,edges:CGPath,highlighted:Bool)] = []
        let land=CGMutablePath()
        let terrainLines=CGMutablePath(), coastLines=CGMutablePath(), grid=CGMutablePath()
        var fallbackFills: [(CGPath,Int)] = []
        let point=CGPoint(x:viewport.centerX*1024,y:viewport.centerY*512)
        let focused=countries?.countries.first { $0.bounds.contains(point) && $0.path.contains(point,using:.evenOdd) }?.id
        for wrap in -1...1 {
            if isCancelled() { return nil }
            let shift=CGFloat(wrap)*1024
            let visible=worldRect.offsetBy(dx:-shift,dy:0)
            guard visible.intersects(CGRect(origin:.zero,size:WorldMapTerrain.worldSize)) else { continue }
            var transform=CGAffineTransform(a:factor,b:0,c:0,d:factor,tx:origin.x+shift*factor,ty:origin.y)
            // Screen-space depth can bring land just outside the top/left edge
            // into the image, so its geographic extraction includes that reach.
            let countryVisible=visible.insetBy(dx:-12/factor,dy:-12/factor)
            for country in countries?.countries ?? [] where country.bounds.intersects(countryVisible) {
                if isCancelled() { return nil }
                for (index,component) in country.components.enumerated() where component.bounds.intersects(countryVisible) {
                    // Fragments smaller than the simplification tolerance
                    // contribute no stable pixel coverage at this zoom. Their
                    // original geometry remains available when magnified.
                    guard max(component.bounds.width,component.bounds.height)*factor >= 0.28 else { continue }
                    let key=country.id+":"+String(index)
                    let indexed=countryGeometry[key] ?? WorldMapPathGeometry(component.path)
                    countryGeometry[key]=indexed
                    let local=indexed.clippedPolygon(to:countryVisible,tolerance:tolerance)
                    if let fill=local.fillPath.copy(using:&transform), !fill.isEmpty,
                       let edges=local.linePath.copy(using:&transform) {
                        countryFaces.append((fill,edges,country.id == focused)); land.addPath(fill)
                    }
                }
            }
            for band in terrain?.bands ?? [] {
                if isCancelled() { return nil }
                if countries == nil, let path=band.fillPath {
                    let indexed=terrainFillGeometry[band.elevation] ?? WorldMapPathGeometry(path)
                    terrainFillGeometry[band.elevation]=indexed
                    let local=indexed.clippedPolygon(to:visible,tolerance:tolerance).fillPath
                    if let transformed=local.copy(using:&transform) { fallbackFills.append((transformed,band.elevation)) }
                }
                if countries != nil && band.elevation == 0 { continue }
                let indexed=terrainGeometry[band.elevation] ?? WorldMapPathGeometry(band.contourPath)
                terrainGeometry[band.elevation]=indexed
                let path=indexed.clippedLines(to:visible,tolerance:tolerance)
                (band.elevation == 0 ? coastLines : terrainLines).addPath(path,transform:transform)
            }
            // The sparse graticule is analytically bounded, never a world path.
            for x in stride(from:CGFloat(0),through:1024,by:1024/12) where x >= visible.minX && x <= visible.maxX {
                grid.move(to:CGPoint(x:origin.x+(x+shift)*factor,y:max(screen.minY,origin.y)))
                grid.addLine(to:CGPoint(x:origin.x+(x+shift)*factor,y:min(screen.maxY,origin.y+512*factor)))
            }
            for y in stride(from:CGFloat(0),through:512,by:512/6) where y >= visible.minY && y <= visible.maxY {
                grid.move(to:CGPoint(x:max(screen.minX,origin.x+shift*factor),y:origin.y+y*factor))
                grid.addLine(to:CGPoint(x:min(screen.maxX,origin.x+(shift+1024)*factor),y:origin.y+y*factor))
            }
        }
        let prepared=ProcessInfo.processInfo.systemUptime
        guard !isCancelled(), let context=CGContext(data:nil,width:pixels,height:pixels,bitsPerComponent:8,
                bytesPerRow:pixels*4,space:Self.colorSpace,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        // CGImage rows are upright: north is at the top, like the map's local
        // coordinates. This image can be assigned directly to CALayer.contents.
        context.translateBy(x:0,y:CGFloat(pixels)); context.scaleBy(x:pixelScale,y:-pixelScale)
        context.translateBy(x:-screen.minX,y:-screen.minY)
        context.setAllowsAntialiasing(true); context.setShouldAntialias(true); context.setLineJoin(.round)
        context.setFillColor(Self.gray(request.dark ? 0.12 : 0.81,1)); context.fill(screen)
        let accent=Self.components(request.accent)
        func drawCountry(_ fill:CGPath,_ edges:CGPath,highlighted:Bool) {
            guard !fill.isEmpty else { return }
            context.saveGState(); context.translateBy(x:3,y:9)
            context.setFillColor(Self.gray(0,request.dark ? 0.42 : 0.24)); context.addPath(fill); context.fillPath(using:.evenOdd)
            context.restoreGState()
            for index in 0..<4 {
                let depth=CGFloat(4-index)*1.6, amount=CGFloat(index)/3
                context.saveGState(); context.translateBy(x:depth*0.35,y:depth)
                context.setFillColor(highlighted ? Self.color(accent,mix:0.48-amount*0.12,toward:0,alpha:0.95)
                    : Self.gray((request.dark ? 0.19 : 0.35)+amount*0.12,0.98))
                context.addPath(fill); context.fillPath(using:.evenOdd); context.restoreGState()
            }
            context.setFillColor(highlighted ? Self.color(accent,alpha:request.dark ? 0.51 : 0.47)
                : Self.gray(request.dark ? 0.60 : 0.56,request.dark ? 0.43 : 0.38))
            context.addPath(fill); context.fillPath(using:.evenOdd)
            context.setStrokeColor(highlighted ? Self.color(accent,mix:0.38,toward:1,alpha:0.95)
                : Self.gray(request.dark ? 0.92 : 0.98,0.82))
            context.setLineWidth(highlighted ? 1.5 : 1.05); context.addPath(edges); context.strokePath()
        }
        // A continent-sized compound even-odd path makes Quartz repeatedly
        // evaluate distant islands during every depth pass. Country batches
        // constrain both the edge scan and pixels to a small geographic plate.
        for highlighted in [false,true] {
            for face in countryFaces where face.highlighted == highlighted {
                if isCancelled() { return nil }
                context.saveGState(); context.clip(to:face.fill.boundingBoxOfPath.insetBy(dx:-12,dy:-12))
                drawCountry(face.fill,face.edges,highlighted:highlighted); context.restoreGState()
            }
        }
        if !land.isEmpty {
            context.saveGState(); context.addPath(land); context.clip(using:.evenOdd)
            let phase=CGFloat((viewport.centerX*440*viewport.zoom-viewport.centerY*220*viewport.zoom).truncatingRemainder(dividingBy:4.5))
            let stripes=CGMutablePath()
            for x in stride(from:floor((screen.minX-screen.height)/4.5)*4.5,through:screen.maxX+screen.height,by:4.5) {
                stripes.move(to:CGPoint(x:x-phase,y:screen.minY)); stripes.addLine(to:CGPoint(x:x-phase+screen.height,y:screen.maxY))
            }
            context.setStrokeColor(Self.gray(request.dark ? 1 : 0.98,request.dark ? 0.17 : 0.23))
            context.setLineWidth(1.3); context.addPath(stripes); context.strokePath(); context.restoreGState()
        }
        if isCancelled() { return nil }
        for (fill,elevation) in fallbackFills {
            context.setFillColor(Self.gray(request.dark ? 0.85 : 0.20,elevation == 0 ? 0.11 : 0.025))
            context.addPath(fill); context.fillPath(using:.evenOdd)
        }
        context.setStrokeColor(Self.gray(request.dark ? 0.80 : 0.19,0.09)); context.setLineWidth(0.35)
        context.addPath(grid); context.strokePath()
        context.setStrokeColor(request.dark ? Self.rgba(0.72,0.78,0.80,0.29) : Self.rgba(0.18,0.23,0.25,0.32))
        context.setLineWidth(0.38); context.addPath(terrainLines); context.strokePath()
        context.setStrokeColor(request.dark ? Self.rgba(0.72,0.78,0.80,0.78) : Self.rgba(0.18,0.23,0.25,0.80))
        context.setLineWidth(0.70); context.addPath(coastLines); context.strokePath()
        guard !isCancelled(), let image=context.makeImage() else { return nil }
        let finished=ProcessInfo.processInfo.systemUptime
        return WorldMapRasterFrame(image:image,viewport:viewport,screenRect:screen,worldRect:worldRect,
            pixelsPerPoint:pixelScale,geometryMilliseconds:(prepared-started)*1000,drawingMilliseconds:(finished-prepared)*1000)
    }

    /// A small whole-world fallback beneath the detailed frame. It covers
    /// camera jumps while the single worker prepares a new local image.
    func renderBackdrop(dark: Bool, isCancelled: () -> Bool = { false }) -> CGImage? {
        guard !isCancelled(), let context=CGContext(data:nil,width:1024,height:512,bitsPerComponent:8,
            bytesPerRow:4096,space:Self.colorSpace,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        let world=CGRect(origin:.zero,size:WorldMapTerrain.worldSize)
        context.translateBy(x:0,y:512); context.scaleBy(x:1,y:-1)
        context.setFillColor(Self.gray(dark ? 0.12 : 0.81,1)); context.fill(world)
        // Backdrop geometry is temporary. Retaining full-country indexes here
        // duplicates the component indexes used by the detailed view. Drawing
        // each small component also prevents Quartz from allocating a huge
        // stroke working buffer for every boundary on Earth at once.
        context.setLineJoin(.round)
        for country in countries?.countries ?? [] {
            if isCancelled() { return nil }
            for component in country.components {
                if isCancelled() { return nil }
                guard max(component.bounds.width,component.bounds.height)>=0.28 else { continue }
                let local=WorldMapPathGeometry(component.path).clippedPolygon(to:world,tolerance:0.35)
                context.saveGState(); context.clip(to:component.bounds.insetBy(dx:-1,dy:-1))
                context.setFillColor(Self.gray(dark ? 0.60 : 0.56,dark ? 0.43 : 0.38))
                context.addPath(local.fillPath); context.fillPath(using:.evenOdd)
                context.setStrokeColor(Self.gray(dark ? 0.92 : 0.98,0.60)); context.setLineWidth(0.55)
                context.addPath(local.linePath); context.strokePath(); context.restoreGState()
            }
        }
        if countries == nil, let coastline=terrain?.bands.first(where:{$0.elevation == 0})?.fillPath {
            context.setFillColor(Self.gray(dark ? 0.60 : 0.56,dark ? 0.43 : 0.38))
            context.addPath(WorldMapPathGeometry(coastline).clippedPolygon(to:world,tolerance:0.35).fillPath)
            context.fillPath(using:.evenOdd)
        }
        context.setStrokeColor(Self.gray(dark ? 0.80 : 0.22,0.25)); context.setLineWidth(0.35)
        for band in terrain?.bands ?? [] where band.elevation != 0 {
            if isCancelled() { return nil }
            let indexed=terrainGeometry[band.elevation] ?? WorldMapPathGeometry(band.contourPath)
            terrainGeometry[band.elevation]=indexed
            context.addPath(indexed.clippedLines(to:world,tolerance:0.45)); context.strokePath()
        }
        return isCancelled() ? nil : context.makeImage()
    }

    private static func components(_ color:CGColor)->[CGFloat] {
        let converted=color.converted(to:colorSpace,intent:.defaultIntent,options:nil)
        guard let values=converted?.components,values.count>=3 else { return [0.98,0.87,0.13] }
        return Array(values.prefix(3))
    }
    private static func color(_ base:[CGFloat],mix:CGFloat=0,toward:CGFloat=0,alpha:CGFloat)->CGColor {
        rgba(base[0]*(1-mix)+toward*mix,base[1]*(1-mix)+toward*mix,base[2]*(1-mix)+toward*mix,alpha)
    }
    private static func gray(_ value:CGFloat,_ alpha:CGFloat)->CGColor { rgba(value,value,value,alpha) }
    private static func rgba(_ r:CGFloat,_ g:CGFloat,_ b:CGFloat,_ a:CGFloat)->CGColor {
        CGColor(colorSpace:colorSpace,components:[r,g,b,a])!
    }
}
