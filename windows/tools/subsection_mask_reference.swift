import AppKit
import QuartzCore

// Build-only feasibility experiment. No source/UI runtime modifications.
// Genuine CA paths are sampled from unchanged HUDSubsectionTransition on own
// paused layers. Candidate commands are Float32, interpolated using Double
// phase and rounded back to Float32 before every geometric validation.
private let viewport = CGRect(x: 9,y: 40,width: 382,height: 248)
private let targetError = 0.02
private let trainingError = 0.008
private let maximumSamples = 5000
private let maximumKnots = 512
private let maximumSeconds = 180.0
private let discontinuityWidth = 1e-12
private let started = Date()
private func need(_ condition: Bool,_ message: String) throws {
    if !condition { throw NSError(domain: "subsection-mask-reference",code: 1,userInfo: [NSLocalizedDescriptionKey:message]) }
}
private func withinBudget() throws { try need(Date().timeIntervalSince(started)<maximumSeconds,"Wall-time budget reached") }
private func lerp(_ a: CGPoint,_ b: CGPoint,_ t: Double) -> CGPoint { CGPoint(x:a.x+(b.x-a.x)*t,y:a.y+(b.y-a.y)*t) }
private func length(_ p: CGPoint) -> Double { hypot(p.x,p.y) }
private func difference(_ a: CGPoint,_ b: CGPoint) -> CGPoint { CGPoint(x:a.x-b.x,y:a.y-b.y) }
private func distance(_ p: CGPoint,_ a: CGPoint,_ b: CGPoint) -> Double {
    let x=b.x-a.x,y=b.y-a.y,denominator=x*x+y*y
    let t=denominator==0 ? 0 : min(1,max(0,((p.x-a.x)*x+(p.y-a.y)*y)/denominator))
    return hypot(p.x-a.x-x*t,p.y-a.y-y*t)
}
private struct PathData {
    let opcodes: [UInt8]
    let coordinates: [Double]
    let path: CGPath
    init(_ path: CGPath) throws {
        var codes: [UInt8]=[],values: [Double]=[],unsupported=false
        path.applyWithBlock { p in
            let count: Int
            switch p.pointee.type {
            case .moveToPoint,.addLineToPoint: count=1
            case .addQuadCurveToPoint: count=2
            case .addCurveToPoint: count=3
            case .closeSubpath: count=0
            @unknown default: count=0;unsupported=true
            }
            codes.append(UInt8(p.pointee.type.rawValue))
            for i in 0..<count {values.append(Double(p.pointee.points[i].x));values.append(Double(p.pointee.points[i].y))}
        }
        try need(!unsupported && codes.count<=40 && values.count<=192 && values.allSatisfy(\.isFinite),"Unexpected/unbounded source path topology")
        self.opcodes=codes;self.coordinates=values;self.path=path
    }
    init(opcodes: [UInt8],coordinates: [Double]) throws {
        let result=CGMutablePath();var index=0
        func point() -> CGPoint {defer {index+=2};return CGPoint(x:coordinates[index],y:coordinates[index+1])}
        for code in opcodes {
            switch code {
            case 0:result.move(to:point())
            case 1:result.addLine(to:point())
            case 2:let c=point();result.addQuadCurve(to:point(),control:c)
            case 3:let c1=point(),c2=point();result.addCurve(to:point(),control1:c1,control2:c2)
            case 4:result.closeSubpath()
            default:throw NSError(domain:"subsection-mask-reference",code:2)
            }
        }
        try need(index==coordinates.count,"Candidate command coordinate mismatch")
        self.opcodes=opcodes;self.coordinates=coordinates;self.path=result
    }
    func quantized() throws -> PathData {try PathData(opcodes:opcodes,coordinates:coordinates.map {Double(Float($0))})}
}
private struct SampleKey: Hashable {let direction: Int;let phase: Double}
private struct Probe {let key:SampleKey;let content:CALayer;let mask:CAShapeLayer;let owner:HUDSubsectionTransition}
private final class Oracle {
    private let panel:NSPanel
    private let root:CALayer
    private(set) var samples: [SampleKey:PathData]=[:]
    private(set) var maximumRepeatDifference=0.0
    init() throws {
        NSApplication.shared.setActivationPolicy(.prohibited)
        let minimumX=NSScreen.screens.map {$0.frame.minX}.min() ?? 0
        panel=NSPanel(contentRect:NSRect(x:minimumX-4096,y:0,width:440,height:440),styleMask:[.borderless,.nonactivatingPanel],backing:.buffered,defer:false)
        panel.isReleasedWhenClosed=false;panel.ignoresMouseEvents=true;panel.hidesOnDeactivate=false
        panel.isOpaque=false;panel.backgroundColor = .clear;panel.animationBehavior = .none
        let view=NSView(frame:NSRect(x:0,y:0,width:440,height:440));view.wantsLayer=true;panel.contentView=view
        guard let layer=view.layer else {throw NSError(domain:"subsection-mask-reference",code:3)}
        root=layer;panel.orderFrontRegardless()
        try need(!panel.isKeyWindow && !panel.isMainWindow && NSScreen.screens.allSatisfy {!panel.frame.intersects($0.frame)},"Oracle panel is not isolated offscreen")
    }
    deinit {panel.orderOut(nil);panel.close()}
    private func commit() {CATransaction.flush();RunLoop.current.run(until:Date(timeIntervalSinceNow:0.055))}
    func load(_ requested:[SampleKey]) throws {
        var missing=Array(Set(requested.filter {samples[$0]==nil}))
        missing.sort {$0.direction==$1.direction ? $0.phase<$1.phase : $0.direction<$1.direction}
        try need(samples.count+missing.count<=maximumSamples,"Source sample budget reached")
        for first in stride(from:0,to:missing.count,by:128) {
            try withinBudget();var probes:[Probe]=[]
            CATransaction.begin();CATransaction.setDisableActions(true)
            for key in missing[first..<min(first+128,missing.count)] {
                let content=CALayer();content.frame=CGRect(x:0,y:0,width:440,height:440);content.backgroundColor=NSColor.white.cgColor
                root.addSublayer(content)
                let owner=HUDSubsectionTransition(content:content,viewport:viewport);owner.reveal(direction:CGFloat(key.direction),animated:true)
                guard let mask=content.mask as? CAShapeLayer,
                      let reveal=mask.animation(forKey:HUDSubsectionTransition.revealKey)?.copy() as? CAKeyframeAnimation else {
                    throw NSError(domain:"subsection-mask-reference",code:4)
                }
                content.removeAnimation(forKey:HUDSubsectionTransition.movementKey)
                content.speed=0;content.timeOffset=1+key.phase*HUDSubsectionTransition.duration
                reveal.beginTime=1;reveal.fillMode = .both;reveal.isRemovedOnCompletion=false
                mask.add(reveal,forKey:HUDSubsectionTransition.revealKey)
                probes.append(Probe(key:key,content:content,mask:mask,owner:owner))
            }
            CATransaction.commit();commit()
            var values:[PathData]=[]
            for probe in probes {
                guard let path=probe.mask.presentation()?.path else {throw NSError(domain:"subsection-mask-reference",code:5)}
                values.append(try PathData(path))
            }
            commit()
            for (i,probe) in probes.enumerated() {
                guard let path=probe.mask.presentation()?.path else {throw NSError(domain:"subsection-mask-reference",code:6)}
                let repeated=try PathData(path)
                try need(values[i].opcodes==repeated.opcodes,"Paused CA topology changed")
                for (a,b) in zip(values[i].coordinates,repeated.coordinates) {maximumRepeatDifference=max(maximumRepeatDifference,abs(a-b))}
                samples[probe.key]=values[i];probe.owner.settle();probe.content.removeFromSuperlayer()
            }
        }
    }
    func get(_ key:SampleKey) -> PathData {samples[key]!}
}

// A Bezier's position lies in the convex hull of its controls. Subdividing the
// actual DIFFERENCE curve bounds the geometric distance of corresponding
// boundary points, rather than reporting a raw control-coordinate residual.
// This is an upper bound on boundary Hausdorff distance. For equal closed-path
// topology, the straight homotopy also preserves nonzero fill away from that
// boundary neighborhood. Actual CGPath.contains probes verify the reconstruction.
private func split(_ p:[CGPoint]) -> ([CGPoint],[CGPoint]) {
    var level=p,left=[p[0]],right=[p.last!]
    while level.count>1 {level=(0..<level.count-1).map {lerp(level[$0],level[$0+1],0.5)};left.append(level[0]);right.append(level.last!)}
    return (left,Array(right.reversed()))
}
private func curveBound(_ points:[CGPoint],_ depth:Int=0) -> Double {
    let upper=points.map(length).max() ?? 0
    if points.count<=2 || upper<=0.0001 {return upper}
    let halves=split(points),lower=max(length(points[0]),length(points.last!),length(halves.0.last!))
    if upper-lower<=0.00005 || depth>=14 {return upper}
    return max(curveBound(halves.0,depth+1),curveBound(halves.1,depth+1))
}
private func boundaryBound(_ a:PathData,_ b:PathData) -> Double {
    guard a.opcodes==b.opcodes else {return .infinity}
    var index=0,current=CGPoint.zero,start=CGPoint.zero,result=0.0
    func point() -> CGPoint {defer {index+=2};return CGPoint(x:a.coordinates[index]-b.coordinates[index],y:a.coordinates[index+1]-b.coordinates[index+1])}
    for code in a.opcodes {
        switch code {
        case 0:current=point();start=current;result=max(result,length(current))
        case 1:let end=point();result=max(result,curveBound([current,end]));current=end
        case 2:let c=point(),end=point();result=max(result,curveBound([current,c,end]));current=end
        case 3:let c1=point(),c2=point(),end=point();result=max(result,curveBound([current,c1,c2,end]));current=end
        case 4:result=max(result,curveBound([current,start]));current=start
        default:preconditionFailure()
        }
    }
    return result
}
private var maximumFlattenAcceptedError=0.0
private var flattenDepthCapReached=false
private func flattened(_ path:PathData) -> [(CGPoint,CGPoint)] {
    var result:[(CGPoint,CGPoint)]=[],index=0,current=CGPoint.zero,start=CGPoint.zero
    func point()->CGPoint {defer{index+=2};return CGPoint(x:path.coordinates[index],y:path.coordinates[index+1])}
    func append(_ points:[CGPoint],_ depth:Int=0) {
        let error=points.dropFirst().dropLast().map {distance($0,points[0],points.last!)}.max() ?? 0
        if error<=0.00025 || depth>=20 {
            maximumFlattenAcceptedError=max(maximumFlattenAcceptedError,error)
            flattenDepthCapReached = flattenDepthCapReached || error>0.00025
            result.append((points[0],points.last!));return
        }
        let halves=split(points);append(halves.0,depth+1);append(halves.1,depth+1)
    }
    for code in path.opcodes {
        switch code {
        case 0:current=point();start=current
        case 1:let end=point();append([current,end]);current=end
        case 2:let c=point(),end=point();append([current,c,end]);current=end
        case 3:let c1=point(),c2=point(),end=point();append([current,c1,c2,end]);current=end
        case 4:append([current,start]);current=start
        default:preconditionFailure()
        }
    }
    return result
}
private var metricSelfTestChecks=0
private func metricSelfTest() throws {
    func check(_ value:Bool) throws {metricSelfTestChecks+=1;try need(value,"Geometric metric self-test failed")}
    let source=CGMutablePath();source.move(to:CGPoint(x:0,y:0));source.addCurve(to:CGPoint(x:100,y:40),control1:CGPoint(x:20,y:-30),control2:CGPoint(x:60,y:80));source.addLine(to:CGPoint(x:0,y:60));source.closeSubpath()
    let a=try PathData(source),reconstructed=try PathData(opcodes:a.opcodes,coordinates:a.coordinates)
    var translation=CGAffineTransform(translationX:0.007,y:-0.004)
    let b=try PathData(source.copy(using:&translation)!)
    try check(abs(boundaryBound(a,b)-hypot(0.007,0.004))<1e-12)
    try check(boundaryBound(a,a)==0)
    for y in stride(from:-40,through:90,by:5) {for x in stride(from:-10,through:110,by:5) {
        let p=CGPoint(x:x,y:y);try check(source.contains(p,using:.winding)==reconstructed.path.contains(p,using:.winding))
    }}
    let curve=[CGPoint(x:0,y:0),CGPoint(x:0.2,y:0.9),CGPoint(x:-0.5,y:-0.8),CGPoint(x:0.01,y:0.02)]
    let bound=curveBound(curve);var observed=0.0
    for i in 0...4096 {let t=Double(i)/4096;var level=curve
        while level.count>1 {level=(0..<level.count-1).map {lerp(level[$0],level[$0+1],t)}}
        observed=max(observed,length(level[0]));try check(length(level[0])<=bound+1e-15)
    }
    try check(bound-observed<0.0001)
}
private struct Coverage {var probes=0,mismatches=0;var maximumMismatchDistance=0.0}
private func coverage(_ actual:PathData,_ candidate:PathData) -> Coverage {
    var result=Coverage(),flat:[(CGPoint,CGPoint)]?=nil
    let bounds=actual.path.boundingBoxOfPath.union(candidate.path.boundingBoxOfPath).insetBy(dx:-0.08,dy:-0.08)
    func probe(_ p:CGPoint) {
        result.probes+=1
        if actual.path.contains(p,using:.winding) != candidate.path.contains(p,using:.winding) {
            result.mismatches+=1
            if flat==nil {flat=flattened(actual)}
            let d=flat!.map {distance(p,$0.0,$0.1)}.min() ?? 0
            result.maximumMismatchDistance=max(result.maximumMismatchDistance,d+0.00025)
        }
    }
    for y in 0..<17 {for x in 0..<23 {probe(CGPoint(x:bounds.minX+(Double(x)+0.381966)*bounds.width/23,y:bounds.minY+(Double(y)+0.618034)*bounds.height/17))}}
    // Probe both sides of each actual edge/curve away from the accepted band.
    // A line approximation is only used to choose positions, never as artwork.
    let edges=flattened(actual),strideCount=max(1,edges.count/80)
    for i in stride(from:0,to:edges.count,by:strideCount) {
        let (a,b)=edges[i],size=hypot(b.x-a.x,b.y-a.y)
        if size>0 {let center=lerp(a,b,0.5),nx = -(b.y-a.y)/size,ny=(b.x-a.x)/size
            for offset in [-0.04,0.04] {probe(CGPoint(x:center.x+offset*nx,y:center.y+offset*ny))}}
    }
    return result
}
private struct Interval {let direction:Int;let lo:Double;let hi:Double;var discontinuity=false}
private struct Fit {let interval:Interval;let paths:[PathData]}
private struct Validation {var count=0,coverageCount=0,coverageMismatches=0;var error=0.0,coverageDistance=0.0;var worst:[String:Any]=[:]}
private func cubic(_ paths:[PathData],_ t:Double) throws -> PathData {
    let nodes=[0.0,1.0/3,2.0/3,1.0]
    let weights=(0..<4).map {i in (0..<4).filter {$0 != i}.reduce(1.0) {$0*(t-nodes[$1])/(nodes[i]-nodes[$1])}}
    var values=[Double](repeating:0,count:paths[0].coordinates.count)
    for k in values.indices {values[k]=Double(Float((0..<4).reduce(0.0) {$0+weights[$1]*paths[$1].coordinates[k]}))}
    return try PathData(opcodes:paths[0].opcodes,coordinates:values)
}
private final class Experiment {
    let oracle:Oracle
    var fits:[Fit]=[],jumps:[Interval]=[]
    var training=Validation(),holdout=Validation()
    var excludedHoldouts=0
    init(_ oracle:Oracle){self.oracle=oracle}
    func record(_ value:Double,_ into:inout Validation,_ interval:Interval,_ phase:Double) {
        into.count+=1
        if value>into.error {into.error=value;into.worst=["direction":interval.direction,"phase":phase,"interval":[interval.lo,interval.hi],"boundaryUpperBound":value]}
    }
    func run() throws {
        let keys=[0.0,0.3,0.68,1.0];var phases=Set(keys)
        for i in 0..<3 {for k in 1..<8 {phases.insert(keys[i]+(keys[i+1]-keys[i])*Double(k)/8)}}
        for k in keys {for delta in [0.000001,0.00001,0.0001] {if k-delta>0 {phases.insert(k-delta)};if k+delta<1 {phases.insert(k+delta)}}}
        let ordered=phases.sorted();var pending:[Interval]=[],regular:[Interval]=[]
        try oracle.load([-1,1].flatMap {d in ordered.map {SampleKey(direction:d,phase:$0)}})
        for direction in [-1,1] {for i in 0..<ordered.count-1 {
            let interval=Interval(direction:direction,lo:ordered[i],hi:ordered[i+1])
            if oracle.get(SampleKey(direction:direction,phase:interval.lo)).opcodes==oracle.get(SampleKey(direction:direction,phase:interval.hi)).opcodes {regular.append(interval)}
            else {pending.append(interval)}
        }}
        // Isolate actual topology switches before fitting, retaining only their
        // final brackets rather than every bisection probe as runtime knots.
        for _ in 0..<52 {
            try withinBudget();if pending.isEmpty {break}
            try oracle.load(pending.map {SampleKey(direction:$0.direction,phase:($0.lo+$0.hi)/2)})
            var next:[Interval]=[]
            for interval in pending {
                if interval.hi-interval.lo<=discontinuityWidth {var jump=interval;jump.discontinuity=true;jumps.append(jump);continue}
                let middle=(interval.lo+interval.hi)/2
                for piece in [Interval(direction:interval.direction,lo:interval.lo,hi:middle),Interval(direction:interval.direction,lo:middle,hi:interval.hi)] {
                    if oracle.get(SampleKey(direction:piece.direction,phase:piece.lo)).opcodes==oracle.get(SampleKey(direction:piece.direction,phase:piece.hi)).opcodes {regular.append(piece)}
                    else {next.append(piece)}
                }
            }
            pending=next
        }
        try need(pending.isEmpty,"Topology bisection budget reached")
        regular.sort {$0.direction==$1.direction ? $0.lo<$1.lo : $0.direction<$1.direction}
        var runs:[Interval]=[]
        for interval in regular {
            if let last=runs.last,last.direction==interval.direction,last.hi==interval.lo {
                runs[runs.count-1]=Interval(direction:interval.direction,lo:last.lo,hi:interval.hi)
            } else {runs.append(interval)}
        }
        pending=runs
        print("topology: \(oracle.samples.count) actual samples, \(runs.count) continuous runs, \(jumps.count) explicit switch brackets")
        for round in 0..<24 {
            try withinBudget();if pending.isEmpty {break}
            try oracle.load(pending.flatMap {i in [0.0,0.2,1.0/3,0.5,2.0/3,0.8,1.0].map {SampleKey(direction:i.direction,phase:i.lo+(i.hi-i.lo)*$0)}})
            var next:[Interval]=[]
            for interval in pending {
                let paths=try [0.0,1.0/3,2.0/3,1.0].map {try oracle.get(SampleKey(direction:interval.direction,phase:interval.lo+(interval.hi-interval.lo)*$0)).quantized()}
                try need(paths.allSatisfy {$0.opcodes==paths[0].opcodes},"Unobserved topology switch inside fitting interval")
                var maximum=0.0
                for fraction in [0.2,0.5,0.8] {
                    let phase=interval.lo+(interval.hi-interval.lo)*fraction,actual=oracle.get(SampleKey(direction:interval.direction,phase:phase))
                    try need(actual.opcodes==paths[0].opcodes,"Unobserved topology switch at fitting holdout")
                    let error=boundaryBound(actual,try cubic(paths,fraction));maximum=max(maximum,error);record(error,&training,interval,phase)
                }
                if maximum<=trainingError {fits.append(Fit(interval:interval,paths:paths))}
                else {let middle=(interval.lo+interval.hi)/2;next.append(Interval(direction:interval.direction,lo:interval.lo,hi:middle));next.append(Interval(direction:interval.direction,lo:middle,hi:interval.hi))}
            }
            pending=next
            try need((fits.count+pending.count)*4<=maximumKnots,"Cubic stored-sample budget reached")
            print("cubic round \(round): \(oracle.samples.count) CA samples, \(fits.count) accepted fits, \(pending.count) pending")
        }
        try need(pending.isEmpty,"Adaptive cubic round budget reached")
        fits.sort {$0.interval.direction==$1.interval.direction ? $0.interval.lo<$1.interval.lo : $0.interval.direction<$1.interval.direction}
        var validationKeys:[SampleKey]=[]
        for fit in fits {let interval=fit.interval
            for fraction in [0.1127016653792583,0.3872983346207417,0.6127016653792583,0.8872983346207417] {validationKeys.append(SampleKey(direction:interval.direction,phase:interval.lo+(interval.hi-interval.lo)*fraction))}
        }
        // Independent global lattice: unrelated to topology/fitting phases.
        for direction in [-1,1] {for i in 0..<97 {validationKeys.append(SampleKey(direction:direction,phase:(Double(i)+0.3713906763541037)/97))}}
        try oracle.load(validationKeys)
        for key in validationKeys {
            try withinBudget()
            if jumps.contains(where:{$0.direction==key.direction && key.phase>=$0.lo && key.phase<=$0.hi}) {excludedHoldouts+=1;continue}
            guard let fit=fits.first(where:{$0.interval.direction==key.direction && key.phase>=$0.interval.lo && key.phase<=$0.interval.hi}) else {throw NSError(domain:"subsection-mask-reference",code:7)}
            let interval=fit.interval,actual=oracle.get(key)
            try need(actual.opcodes==fit.paths[0].opcodes,"Independent holdout found an unsampled topology switch")
            let candidate=try cubic(fit.paths,(key.phase-interval.lo)/(interval.hi-interval.lo)),error=boundaryBound(actual,candidate)
            record(error,&holdout,interval,key.phase)
            if error==holdout.error {
                holdout.worst["opcodes"]=actual.opcodes
                holdout.worst["actualCoordinates"]=actual.coordinates
                holdout.worst["candidateCoordinates"]=candidate.coordinates
            }
            let fill=coverage(actual,candidate);holdout.coverageCount+=fill.probes;holdout.coverageMismatches+=fill.mismatches
            holdout.coverageDistance=max(holdout.coverageDistance,fill.maximumMismatchDistance)
        }
    }
    func binary() throws -> Data {
        var data=Data("EHSUBC01".utf8)
        func u32(_ value:UInt32){var x=value.littleEndian;withUnsafeBytes(of:&x){data.append(contentsOf:$0)}}
        func f64(_ value:Double){var x=value.bitPattern.littleEndian;withUnsafeBytes(of:&x){data.append(contentsOf:$0)}}
        func f32(_ value:Double){u32(Float(value).bitPattern)}
        for value in [9.0,40.0,382.0,248.0,0.26] {f64(value)};u32(2)
        for direction in [-1,1] {
            let track=fits.filter {$0.interval.direction==direction};var topologies:[[UInt8]]=[]
            for fit in track where !topologies.contains(fit.paths[0].opcodes) {topologies.append(fit.paths[0].opcodes)}
            u32(UInt32(bitPattern:Int32(direction)));u32(UInt32(topologies.count))
            for topology in topologies {u32(UInt32(topology.count));data.append(contentsOf:topology)}
            u32(UInt32(track.count))
            for fit in track {
                f64(fit.interval.lo);f64(fit.interval.hi);u32(UInt32(topologies.firstIndex(of:fit.paths[0].opcodes)!))
                let constant=fit.paths.allSatisfy {$0.coordinates==fit.paths[0].coordinates}
                u32(constant ? 1:4);u32(UInt32(fit.paths[0].coordinates.count))
                for path in (constant ? [fit.paths[0]]:fit.paths) {for value in path.coordinates {f32(value)}}
            }
            let switches=jumps.filter {$0.direction==direction};u32(UInt32(switches.count))
            for jump in switches {f64(jump.lo);f64(jump.hi)}
        }
        return data
    }
    func report(_ error:String?,_ bytes:Int) -> [String:Any] {
        let measured=error==nil && holdout.count>0 && holdout.error<=targetError && holdout.coverageDistance<=targetError && !flattenDepthCapReached && oracle.maximumRepeatDifference==0
        return ["schemaVersion":1,"kind":"build-only-actual-CA-mask-feasibility","viewport":[9,40,382,248],"directions":[-1,1],
                "sourceDuration":0.26,"targetLocalPointDeviation":targetError,"trainingThreshold":trainingError,
                "status":error ?? (measured ? "measured regular intervals pass; temporal discontinuity brackets remain explicit" : "measured target not met"),
                "measuredRegularIntervalsPass":measured,"completeRuntimeParityClaimed":false,"shippingArtifact":false,
                "candidateBytes":bytes,"candidateWithin256KiB":bytes>0 && bytes<256*1024,
                "cubicIntervalsPerDirection":Dictionary(uniqueKeysWithValues:[-1,1].map {d in (String(d),fits.filter {$0.interval.direction==d}.count)}),
                "sourceSamples":oracle.samples.count,"maximumRepeatDifference":oracle.maximumRepeatDifference,
                "maximumSeconds":maximumSeconds,"maximumSamples":maximumSamples,"maximumStoredPathSamples":maximumKnots,"elapsedSeconds":Date().timeIntervalSince(started),
                "adaptiveTests":training.count,"adaptiveWorstPreSubdivision":training.worst,"independentHoldouts":holdout.count,"independentWorst":holdout.worst,
                "independentBoundaryUpperBound":holdout.error,"coverageProbes":holdout.coverageCount,"coverageMismatches":holdout.coverageMismatches,
                "maximumCoverageMismatchDistance":holdout.coverageDistance,"excludedHoldouts":excludedHoldouts,
                "metricSelfTestChecks":metricSelfTestChecks,"maximumFlattenAcceptedError":maximumFlattenAcceptedError,"flattenDepthCapReached":flattenDepthCapReached,
                "discontinuityBrackets":jumps.map {["direction":Double($0.direction),"lo":$0.lo,"hi":$0.hi,"width":$0.hi-$0.lo]},
                "interpolation":"Fixed opcodes, four Float32 path samples at interval fractions [0,1/3,2/3,1], Double cubic Lagrange weights, Float32 output",
                "boundaryMetric":"Adaptive Bezier difference-curve norm upper bound (5e-5 tightness, depth cap14); actual geometric correspondence bounds Hausdorff; no control-only comparison",
                "coverageMetric":"Original CGPath nonzero fill versus reconstructed candidate; grid and both-sides edge probes; mismatch distance measured to original path contours with 0.00025 flatten allowance",
                "binaryLayout":"EHSUBC01; little-endian; Float64 viewport4/duration; UInt32 trackCount; pertrack Int32direction, topologyCount, UInt32opcodeCount+UInt8opcodes, fitCount; perfit Float64lo/hi, UInt32topologyIndex/sampleCount/coordinateCount, Float32coordinates; jumpCount+Float64lo/hi pairs",
                "limits":["Measured holdouts are not a proof at all continuous times","No interpolation inside discontinuity brackets; event-side classification is unverified there","Coverage-distance metric uses source path contours, not a Boolean-union boundary extractor","Only one canonical viewport and pinned macOS CA behavior","No antialiasing/projected pixels/renderer integration verified"],
                "isolation":["own nonactivating offscreen layers only","no screen capture, user files, stores or services","original source unchanged"]]
    }
}
@main private struct SubsectionMaskReference {
    static func main() throws {
        try need(CommandLine.arguments.count==2,"Pass an existing new output directory")
        let output=URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true)
        try need(!FileManager.default.fileExists(atPath:output.appendingPathComponent("report.json").path),"Output report must be new")
        try metricSelfTest()
        let oracle=try Oracle(),experiment=Experiment(oracle);var failure:String?=nil,binary=Data()
        do {try experiment.run();binary=try experiment.binary()} catch {failure=error.localizedDescription}
        if !binary.isEmpty {try binary.write(to:output.appendingPathComponent("candidate.bin"),options:.withoutOverwriting)}
        let report=experiment.report(failure,binary.count)
        try JSONSerialization.data(withJSONObject:report,options:[.sortedKeys,.prettyPrinted]).write(to:output.appendingPathComponent("report.json"),options:.withoutOverwriting)
        print("Mask feasibility: \(report["status"]!), \(binary.count) candidate bytes, \(oracle.samples.count) actual CA samples")
    }
}
