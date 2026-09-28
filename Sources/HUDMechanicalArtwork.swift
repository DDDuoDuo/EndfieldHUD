import AppKit
import QuartzCore

/// Original, cached vector artwork for the shared HUD. This object owns no
/// timers or display links. Its owner supplies depth transforms and motion.
final class HUDMechanicalArtwork {
    static let canvasSize = CGSize(width: 1000, height: 640)
    static let center = CGPoint(x: 500, y: 320)
    static let scanOpacity: Float = 0.42
    static let indicatorGlowOpacity: Float = 0.72
    static let gridOpacity: Float = 0.22
    static let instrumentScale: CGFloat = 1.18
    static let highlightOpacity: Float = 0.55
    static let dotGridSpacing: CGFloat = 24
    static let dotGridRadius: CGFloat = 174
    static let dotSize: CGFloat = 1.8

    /// A Cartesian lattice, clipped to the center well. It is built once and
    /// drawn as one cached vector path, not a collection of animated particles.
    static let dotGridCenters: [CGPoint] = {
        let extent = Int(dotGridRadius / dotGridSpacing)
        var points: [CGPoint] = []
        for row in -extent...extent {
            for column in -extent...extent {
                let x = CGFloat(column) * dotGridSpacing
                let y = CGFloat(row) * dotGridSpacing
                if x * x + y * y <= dotGridRadius * dotGridRadius {
                    points.append(CGPoint(x: center.x + x, y: center.y + y))
                }
            }
        }
        return points
    }()

    struct TriangleOrbit: Equatable {
        /// Static rotation of the marker child, in radians. Positive animated
        /// rotation is clockwise in the HUD's flipped display coordinates.
        let phase: CGFloat
        let period: TimeInterval
        let direction: CGFloat
        var fromValue: CGFloat { 0 }
        var toValue: CGFloat { direction * .pi * 2 }
    }

    // Each group uses the same full-canvas coordinate system, making it safe
    // to put each one in a different depth plane without repositioning it.
    let distant = CALayer()
    let rear = CALayer()
    let secondary = CALayer()
    let frame = CALayer()
    let inner = CALayer()
    let markers = CALayer()
    let glass = CALayer()
    let rim = CALayer()

    // Rotor bounds are 500 square; their local center is (250,250), and their
    // position in the design canvas is (500,320). Rotation never moves the HUD.
    let rearRotor = CALayer()
    let secondaryRotor = CALayer()
    let triangleRotors: [CALayer] = (0..<3).map { _ in CALayer() }
    private(set) var triangleOrbits: [TriangleOrbit] = []
    private var triangleMarkers: [CAShapeLayer] = []
    // Full-canvas bounds, with the same (500,320) pivot as the square rotors.
    let innerGuideRotor = CALayer()
    let scanLayer = CAGradientLayer()
    let indicatorGlow = CALayer()
    let gridDrift = CALayer()
    let highlightCarrier = CALayer()

    private enum Ink {
        case chassis, sidewall, well, wellBottom, groove, shadowEdge, litEdge
        case line, muted, accent, charge, glass, grid, referenceWhite
    }
    private struct ShapeInk {
        let layer: CAShapeLayer
        let fill: Ink?
        let stroke: Ink?
    }
    private var shapes: [ShapeInk] = []
    private var gradients: [(CAGradientLayer, [Ink])] = []
    private var shadowLayers: [CALayer] = []

    init() {
        let bounds = CGRect(origin: .zero, size: Self.canvasSize)
        for group in groups {
            group.frame = bounds
            // NSView's backing layer already establishes the flipped geometry.
            group.isGeometryFlipped = false
        }
        for rotor in [rearRotor, secondaryRotor, highlightCarrier] + triangleRotors {
            rotor.bounds = CGRect(x: 0, y: 0, width: 500, height: 500)
            rotor.position = Self.center
        }
        gridDrift.frame = bounds
        innerGuideRotor.frame = bounds
        gridDrift.opacity = Self.gridOpacity
        indicatorGlow.frame = bounds
        indicatorGlow.opacity = Self.indicatorGlowOpacity
        highlightCarrier.opacity = Self.highlightOpacity
        makeDistant()
        makeRear()
        makeSecondary()
        makeFrame()
        makeInner()
        makeMarkers()
        randomizeTriangleOrbits()
        makeGlass()
        // Scale the mechanical instrument about its center, independently of
        // navigation and content so editable canvas coordinates stay stable.
        for group in groups where group !== distant {
            group.setAffineTransform(CGAffineTransform(scaleX: Self.instrumentScale, y: Self.instrumentScale))
        }
        update(dark: true, chargeColor: NSColor(srgbRed: 0.30, green: 0.90, blue: 0.50, alpha: 1))
    }

    var groups: [CALayer] { [distant, rear, secondary, frame, inner, markers, glass, rim] }

    /// Call once before each opening's motion registration/start. Only each
    /// permanent marker child's static pose changes; the managed rotor remains
    /// untouched, so restoring motion baselines cannot erase the random phase.
    /// The same pose is visible during opening and when Reduce Motion is on.
    @discardableResult
    func randomizeTriangleOrbits() -> [TriangleOrbit] {
        var generator = SystemRandomNumberGenerator()
        return randomizeTriangleOrbits(using: &generator)
    }

    @discardableResult
    func randomizeTriangleOrbits<R: RandomNumberGenerator>(using generator: inout R) -> [TriangleOrbit] {
        let turn = Double.pi * 2
        let origin = Double.random(in: 0..<turn, using: &generator)
        // Independent jitter in three sectors keeps the initial markers apart
        // while allowing their positions to change throughout the full circle.
        let phases = (0..<3).map { index -> CGFloat in
            let jitter = Double.random(in: (-Double.pi / 7)...(Double.pi / 7), using: &generator)
            let angle = (origin + Double(index) * turn / 3 + jitter).truncatingRemainder(dividingBy: turn)
            return CGFloat(angle < 0 ? angle + turn : angle)
        }.shuffled(using: &generator)
        let periods = [Double.random(in: 28...38, using: &generator),
                       Double.random(in: 40...50, using: &generator),
                       Double.random(in: 52...64, using: &generator)].shuffled(using: &generator)
        let directions: [CGFloat] = [1, -1, Bool.random(using: &generator) ? 1 : -1].shuffled(using: &generator)
        triangleOrbits = (0..<3).map { TriangleOrbit(phase: phases[$0], period: periods[$0], direction: directions[$0]) }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for (index, orbit) in triangleOrbits.enumerated() {
            triangleMarkers[index].transform = CATransform3DMakeRotation(orbit.phase, 0, 0, 1)
        }
        CATransaction.commit()
        return triangleOrbits
    }

    func update(dark: Bool, chargeColor: NSColor, accentColor: NSColor? = nil) {
        // Decorative accents follow the chosen theme independently of the
        // battery's semantic green/yellow/red indicator and scan colors.
        let accent = accentColor ?? (dark
            ? NSColor(srgbRed: 0.92, green: 0.76, blue: 0.16, alpha: 1)
            : NSColor(srgbRed: 0.56, green: 0.42, blue: 0.03, alpha: 1))
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        for item in shapes {
            item.layer.fillColor = item.fill.map { color($0, dark: dark, charge: chargeColor, accent: accent).cgColor }
            item.layer.strokeColor = item.stroke.map { color($0, dark: dark, charge: chargeColor, accent: accent).cgColor }
        }
        for item in gradients {
            item.0.colors = item.1.map { color($0, dark: dark, charge: chargeColor, accent: accent).cgColor }
        }
        for shadow in shadowLayers { shadow.shadowOpacity = dark ? 0.42 : 0.16 }
        let clear = chargeColor.withAlphaComponent(0).cgColor
        scanLayer.colors = [clear, chargeColor.withAlphaComponent(dark ? 0.13 : 0.09).cgColor, clear]
        CATransaction.commit()
    }

    private func makeDistant() {
        makeReferenceBackplane()
        distant.addSublayer(gridDrift)
        let lattice = CGMutablePath()
        // Off-center technical coordinates give this backplane an engineered,
        // slightly imperfect alignment instead of a decorative perfect target.
        for x in stride(from: CGFloat(214), through: 786, by: 56) {
            lattice.move(to: CGPoint(x: x, y: 34)); lattice.addLine(to: CGPoint(x: x, y: 618))
        }
        for y in stride(from: CGFloat(34), through: 618, by: 56) {
            lattice.move(to: CGPoint(x: 214, y: y)); lattice.addLine(to: CGPoint(x: 786, y: y))
        }
        let grid = shape(lattice, in: gridDrift, stroke: .grid, width: 0.45)
        let gridMask = CAShapeLayer()
        gridMask.frame = grid.bounds
        // Keep this backplane visible outside the opaque cage; the earlier
        // full disc hid nearly all of its grid underneath the center well.
        gridMask.path = Self.annulus(center: CGPoint(x: 494, y: 330), outer: 283, inner: 242)
        gridMask.fillRule = .evenOdd
        grid.mask = gridMask
        let rearGuide = shape(Self.arc(center: CGPoint(x: 493, y: 332), radius: 261, from: -163, to: 137),
                              in: gridDrift, stroke: .muted, width: 0.8)
        rearGuide.opacity = 0.40
        let rails = CGMutablePath()
        rails.move(to: CGPoint(x: 264, y: 172)); rails.addLine(to: CGPoint(x: 241, y: 172))
        rails.addLine(to: CGPoint(x: 241, y: 254)); rails.addLine(to: CGPoint(x: 225, y: 272))
        rails.move(to: CGPoint(x: 739, y: 376)); rails.addLine(to: CGPoint(x: 757, y: 394))
        rails.addLine(to: CGPoint(x: 757, y: 472)); rails.addLine(to: CGPoint(x: 731, y: 472))
        shape(rails, in: gridDrift, stroke: .line, width: 0.7)
        for (x, y) in [(CGFloat(248), CGFloat(156)), (752, 484)] {
            let cross = CGMutablePath()
            cross.move(to: CGPoint(x: x - 4, y: y)); cross.addLine(to: CGPoint(x: x + 4, y: y))
            cross.move(to: CGPoint(x: x, y: y - 4)); cross.addLine(to: CGPoint(x: x, y: y + 4))
            shape(cross, in: gridDrift, stroke: .muted, width: 0.7)
        }
    }

    private func makeReferenceBackplane() {
        // Sparse, cached vector details: no particle simulation or redraw loop.
        let rails = CGMutablePath()
        rails.move(to: CGPoint(x: 92, y: 583)); rails.addLine(to: CGPoint(x: 53, y: 486))
        rails.addCurve(to: CGPoint(x: 179, y: 109), control1: CGPoint(x: 17, y: 325), control2: CGPoint(x: 63, y: 215))
        rails.addLine(to: CGPoint(x: 247, y: 50))
        rails.move(to: CGPoint(x: 780, y: 38)); rails.addLine(to: CGPoint(x: 921, y: 153))
        rails.addCurve(to: CGPoint(x: 944, y: 565), control1: CGPoint(x: 996, y: 310), control2: CGPoint(x: 1006, y: 441))
        shape(rails, in: distant, stroke: .line, width: 0.9)

        let crosses = CGMutablePath()
        for (x, y, angle) in [(CGFloat(218), CGFloat(90), -0.25), (804, 80, 0.22), (251, 307, -0.17),
                              (760, 427, 0.20), (214, 601, -0.2), (937, 551, 0.14)] {
            let center = CGPoint(x: x, y: y)
            for sign in [-1.0, 1.0] {
                for rotation in [CGFloat(angle), CGFloat(angle) + .pi / 2] {
                    let a = CGFloat(sign) * 2.2, b = CGFloat(sign) * 8.3
                    crosses.move(to: CGPoint(x: center.x + cos(rotation) * a, y: center.y + sin(rotation) * a))
                    crosses.addLine(to: CGPoint(x: center.x + cos(rotation) * b, y: center.y + sin(rotation) * b))
                }
            }
        }
        shape(crosses, in: distant, stroke: .litEdge, width: 0.85)
    }

    private func makeRear() {
        // A translated chassis and two offset lips produce thickness even when
        // the complete object is viewed head-on, without a live blur operation.
        let extrusionCenter = CGPoint(x: 504, y: 334)
        let extrusion = shape(Self.annulus(center: extrusionCenter, outer: 240, inner: 191),
                              in: rear, fill: .sidewall, stroke: .shadowEdge, width: 1)
        extrusion.shadowColor = NSColor.black.cgColor
        extrusion.shadowOffset = CGSize(width: 1, height: 10)
        extrusion.shadowRadius = 9
        extrusion.shadowPath = CGPath(ellipseIn: CGRect(x: 264, y: 94, width: 480, height: 480), transform: nil)
        shadowLayers.append(extrusion)
        shape(Self.arc(center: extrusionCenter, radius: 239.5, from: 4, to: 166),
              in: rear, stroke: .line, width: 1.3)
        shape(Self.annulus(center: CGPoint(x: 502, y: 327), outer: 242, inner: 194),
              in: rear, fill: .groove, stroke: .shadowEdge, width: 1)
        shape(Self.arc(center: CGPoint(x: 502, y: 327), radius: 241, from: 10, to: 171),
              in: rear, stroke: .litEdge, width: 0.9)
        rear.addSublayer(rearRotor)
        let c = CGPoint(x: 250, y: 250)
        // One long rail replaces the individual blocks, fasteners and ticks.
        shape(Self.arc(center: c, radius: 237, from: -174, to: -111),
              in: rearRotor, stroke: .line, width: 0.8)
    }

    private func makeSecondary() {
        secondary.addSublayer(secondaryRotor)
        let c = CGPoint(x: 250, y: 250)
        // Most of the rim is a quiet, uninterrupted dark face. The two rails
        // have unequal lengths and broad gaps, matching the reference's rhythm.
        shape(Self.annulus(center: c, outer: 233, inner: 205), in: secondaryRotor, fill: .chassis)
        let accentRing = shape(Self.arc(center: c, radius: 226, from: 19, to: 91),
                               in: secondaryRotor, stroke: .accent, width: 1.8)
        accentRing.name = "hud.secondary.accentRing"
    }

    private func makeFrame() {
        let c = Self.center
        // The stationary bearing has a top-lit bevel and a darker lower edge.
        let bearingPath = Self.annulus(center: c, outer: 207, inner: 194)
        let bearing = gradient(in: frame, rect: CGRect(origin: .zero, size: Self.canvasSize),
                               colors: [.chassis, .chassis, .groove], locations: [0, 0.54, 1])
        mask(bearing, path: bearingPath)
        let bevel = shape(Self.arc(center: c, radius: 206.4, from: -169, to: -10),
                          in: frame, stroke: .litEdge, width: 0.8)
        bevel.opacity = 0.55
        shape(Self.arc(center: c, radius: 206.4, from: 8, to: 175), in: frame, stroke: .shadowEdge, width: 2.1)
        shape(Self.arc(center: c, radius: 194.4, from: -175, to: -4), in: frame, stroke: .shadowEdge, width: 3.2)
        shape(Self.arc(center: c, radius: 194.1, from: 2, to: 176), in: frame, stroke: .litEdge, width: 0.65)
    }

    private func makeInner() {
        shape(Self.annulus(center: CGPoint(x: 500, y: 323), outer: 197, inner: 188),
              in: inner, fill: .shadowEdge)
        let wellPath = CGPath(ellipseIn: CGRect(x: 310, y: 130, width: 380, height: 380), transform: nil)
        let well = gradient(in: inner, rect: CGRect(x: 310, y: 130, width: 380, height: 380),
                            colors: [.well, .well, .wellBottom], locations: [0, 0.35, 1])
        mask(well, path: CGPath(ellipseIn: CGRect(x: 0, y: 0, width: 380, height: 380), transform: nil))
        let lip = shape(wellPath, in: inner, stroke: .shadowEdge, width: 3)
        lip.shadowColor = NSColor.black.cgColor
        lip.shadowRadius = 3
        lip.shadowOffset = CGSize(width: 0, height: 2)
        lip.shadowPath = wellPath
        // No shadow on the readout center: the edge itself supplies the recess.
        shape(Self.arc(center: Self.center, radius: 189, from: 8, to: 168), in: inner, stroke: .litEdge, width: 0.55)
        inner.addSublayer(innerGuideRotor)
        let guides = shape(Self.arc(center: Self.center, radius: 184, from: 116, to: 167),
                           in: innerGuideRotor, stroke: .line, width: 0.6)
        guides.opacity = 0.45
        let grain = CALayer()
        grain.frame = CGRect(x: 327, y: 147, width: 346, height: 346)
        grain.contents = Self.grainImage
        grain.contentsGravity = .resize
        grain.opacity = 0.16
        mask(grain, path: CGPath(ellipseIn: CGRect(x: 0, y: 0, width: 346, height: 346), transform: nil))
        inner.addSublayer(grain)
        let dotGrid = CGMutablePath()
        for point in Self.dotGridCenters {
            dotGrid.addRect(CGRect(x: point.x - Self.dotSize / 2, y: point.y - Self.dotSize / 2,
                                   width: Self.dotSize, height: Self.dotSize))
        }
        let dots = shape(dotGrid, in: inner, fill: .muted)
        dots.name = "hud.backplane.dots"; dots.opacity = 0.30
        let localCrosses = CGMutablePath()
        for center in [CGPoint(x: 363, y: 251), CGPoint(x: 634, y: 390)] {
            for sign in [-1.0, 1.0] {
                localCrosses.move(to: CGPoint(x: center.x + CGFloat(sign) * 2, y: center.y))
                localCrosses.addLine(to: CGPoint(x: center.x + CGFloat(sign) * 7, y: center.y))
                localCrosses.move(to: CGPoint(x: center.x, y: center.y + CGFloat(sign) * 2))
                localCrosses.addLine(to: CGPoint(x: center.x, y: center.y + CGFloat(sign) * 7))
            }
        }
        let localMarks = shape(localCrosses, in: inner, stroke: .muted, width: 0.65)
        localMarks.opacity = 0.65
        let scanner = CALayer()
        scanner.frame = CGRect(origin: .zero, size: Self.canvasSize)
        mask(scanner, path: CGPath(ellipseIn: CGRect(x: 326, y: 146, width: 348, height: 348), transform: nil))
        inner.addSublayer(scanner)
        scanLayer.frame = CGRect(x: 326, y: 310, width: 348, height: 20)
        scanLayer.locations = [0, 0.52, 1]
        scanLayer.startPoint = CGPoint(x: 0.5, y: 0)
        scanLayer.endPoint = CGPoint(x: 0.5, y: 1)
        scanLayer.opacity = Self.scanOpacity
        scanner.addSublayer(scanLayer)
        // Its base position is (500,320); additive transform.translation.y can
        // scan over +/-174 points while this fixed mask clips the light band.
    }

    private func makeMarkers() {
        let c = CGPoint(x: 250, y: 250)
        for (index, rotor) in triangleRotors.enumerated() {
            rotor.name = "hud.orbit.rotor.\(index)"
            markers.addSublayer(rotor)
            let tip = Self.point(c, radius: 224, degrees: -90)
            let left = Self.point(c, radius: 240, degrees: -92.2)
            let right = Self.point(c, radius: 240, degrees: -87.8)
            let p = CGMutablePath(); p.move(to: tip); p.addLine(to: left); p.addLine(to: right); p.closeSubpath()
            let mark = shape(p, in: rotor, stroke: .accent, width: 1.15)
            mark.name = "hud.orbit.triangle.\(index)"
            triangleMarkers.append(mark)
        }
        markers.addSublayer(indicatorGlow)
        shape(Self.arc(center: Self.center, radius: 201, from: -37, to: -34),
              in: indicatorGlow, stroke: .charge, width: 2)
    }

    private func makeGlass() {
        glass.addSublayer(highlightCarrier)
        let c = CGPoint(x: 250, y: 250)
        let reflection = gradient(in: highlightCarrier, rect: CGRect(x: 67, y: 67, width: 366, height: 366),
                                  colors: [.glass, .glass, .well], locations: [0, 0.16, 1])
        reflection.opacity = 0.12
        let crescent = Self.sector(center: CGPoint(x: 183, y: 183), inner: 175, outer: 181, from: -151, to: -24)
        mask(reflection, path: crescent)
        shape(Self.arc(center: c, radius: 181.5, from: -143, to: -63), in: highlightCarrier, stroke: .glass, width: 0.75)
        // The reference's broken rails wrap the sides of the rim, with three
        // left spans and two right spans. The foreground rim follows the center
        // content's full motion. Radius 208 compensates for that nearer depth
        // to retain the resting footprint; the left spans have narrower gaps.
        for (side, segments) in [("right", [(-52.0, -20.0), (-13.0, 50.0)]),
                                 ("left", [(130.0, 160.0), (163.0, 197.0), (200.0, 234.0)])] {
            for (index, angles) in segments.enumerated() {
                let rail = shape(Self.arc(center: Self.center, radius: 208, from: angles.0, to: angles.1),
                                 in: rim, stroke: .referenceWhite, width: 4.3)
                rail.name = "hud.rim.white." + side + "." + String(index)
                rail.lineCap = .butt
            }
        }
    }

    @discardableResult
    private func shape(_ path: CGPath, in parent: CALayer, fill: Ink? = nil,
                       stroke: Ink? = nil, width: CGFloat = 1) -> CAShapeLayer {
        let item = CAShapeLayer()
        item.frame = parent.bounds
        item.path = path
        item.fillRule = .evenOdd
        item.lineWidth = width
        item.lineJoin = .bevel
        parent.addSublayer(item)
        shapes.append(ShapeInk(layer: item, fill: fill, stroke: stroke))
        return item
    }

    private func gradient(in parent: CALayer, rect: CGRect, colors: [Ink], locations: [NSNumber]) -> CAGradientLayer {
        let gradient = CAGradientLayer()
        gradient.frame = rect
        gradient.startPoint = CGPoint(x: 0.38, y: 0)
        gradient.endPoint = CGPoint(x: 0.65, y: 1)
        gradient.locations = locations
        parent.addSublayer(gradient)
        gradients.append((gradient, colors))
        return gradient
    }

    private func mask(_ layer: CALayer, path: CGPath) {
        let mask = CAShapeLayer()
        mask.frame = layer.bounds
        mask.path = path
        mask.fillColor = NSColor.black.cgColor
        mask.fillRule = .evenOdd
        layer.mask = mask
    }

    private func color(_ ink: Ink, dark: Bool, charge: NSColor, accent: NSColor) -> NSColor {
        switch ink {
        // The overlapping rear plates must remain translucent as a stack,
        // rather than each being translucent but combining into an opaque rim.
        case .chassis: return dark ? NSColor(srgbRed: 0.17, green: 0.18, blue: 0.19, alpha: 0.43) : NSColor(srgbRed: 0.74, green: 0.76, blue: 0.77, alpha: 0.43)
        case .sidewall: return dark ? NSColor(srgbRed: 0.073, green: 0.090, blue: 0.102, alpha: 0.24) : NSColor(srgbRed: 0.46, green: 0.49, blue: 0.51, alpha: 0.26)
        case .well: return dark ? NSColor(srgbRed: 0.035, green: 0.048, blue: 0.054, alpha: 1) : NSColor(srgbRed: 0.95, green: 0.96, blue: 0.96, alpha: 1)
        case .wellBottom: return dark ? NSColor(srgbRed: 0.070, green: 0.085, blue: 0.089, alpha: 1) : NSColor(srgbRed: 0.85, green: 0.88, blue: 0.89, alpha: 1)
        case .groove: return dark ? NSColor(srgbRed: 0.043, green: 0.052, blue: 0.057, alpha: 0.32) : NSColor(srgbRed: 0.58, green: 0.61, blue: 0.63, alpha: 0.30)
        case .shadowEdge: return dark ? NSColor(white: 0.012, alpha: 1) : NSColor(srgbRed: 0.27, green: 0.30, blue: 0.31, alpha: 1)
        case .litEdge: return dark ? NSColor(srgbRed: 0.70, green: 0.76, blue: 0.79, alpha: 0.90) : NSColor(white: 1, alpha: 0.95)
        case .line: return dark ? NSColor(srgbRed: 0.49, green: 0.56, blue: 0.58, alpha: 0.30) : NSColor(srgbRed: 0.25, green: 0.32, blue: 0.35, alpha: 0.34)
        case .muted: return dark ? NSColor(srgbRed: 0.62, green: 0.67, blue: 0.68, alpha: 0.73) : NSColor(srgbRed: 0.28, green: 0.32, blue: 0.34, alpha: 0.82)
        case .accent: return accent.withAlphaComponent(dark ? 0.96 : 1)
        case .referenceWhite: return NSColor(white: dark ? 0.96 : 1, alpha: 0.78)
        case .charge: return charge
        case .glass: return dark ? NSColor(srgbRed: 0.72, green: 0.85, blue: 0.89, alpha: 0.44) : NSColor(white: 1, alpha: 0.93)
        case .grid: return dark ? NSColor(srgbRed: 0.42, green: 0.55, blue: 0.57, alpha: 0.40) : NSColor(srgbRed: 0.30, green: 0.40, blue: 0.42, alpha: 0.30)
        }
    }

    private static func point(_ center: CGPoint, radius: CGFloat, degrees: Double) -> CGPoint {
        let angle = CGFloat(degrees) * .pi / 180
        return CGPoint(x: center.x + cos(angle) * radius, y: center.y + sin(angle) * radius)
    }

    private static func arc(center: CGPoint, radius: CGFloat, from: Double, to: Double) -> CGPath {
        let p = CGMutablePath()
        p.addArc(center: center, radius: radius, startAngle: CGFloat(from) * .pi / 180,
                 endAngle: CGFloat(to) * .pi / 180, clockwise: false)
        return p
    }

    private static func annulus(center: CGPoint, outer: CGFloat, inner: CGFloat) -> CGPath {
        let p = CGMutablePath()
        p.addEllipse(in: CGRect(x: center.x - outer, y: center.y - outer, width: outer * 2, height: outer * 2))
        p.addEllipse(in: CGRect(x: center.x - inner, y: center.y - inner, width: inner * 2, height: inner * 2))
        return p
    }

    private static func sector(center: CGPoint, inner: CGFloat, outer: CGFloat, from: Double, to: Double) -> CGPath {
        let start = CGFloat(from) * .pi / 180, end = CGFloat(to) * .pi / 180
        let p = CGMutablePath()
        p.addArc(center: center, radius: outer, startAngle: start, endAngle: end, clockwise: false)
        p.addLine(to: CGPoint(x: center.x + cos(end) * inner, y: center.y + sin(end) * inner))
        p.addArc(center: center, radius: inner, startAngle: end, endAngle: start, clockwise: true)
        p.closeSubpath()
        return p
    }

    /// Generated once and shared by every artwork instance. It is a low-alpha
    /// static material texture, not a frame-by-frame noise effect or game asset.
    private static let grainImage: CGImage? = {
        let side = 256
        var bytes = [UInt8](repeating: 0, count: side * side * 4)
        var seed: UInt32 = 0x454E_4446
        for pixel in 0..<(side * side) {
            seed = 1664525 &* seed &+ 1013904223
            let light = UInt8((seed >> 24) & 0x1F)
            let offset = pixel * 4
            // Premultiplied neutral grain. Sparse dark specks are transparent;
            // the small highlights work on both the dark and the light well.
            bytes[offset] = light; bytes[offset + 1] = light; bytes[offset + 2] = light
            bytes[offset + 3] = light
        }
        let data = Data(bytes) as CFData
        guard let provider = CGDataProvider(data: data) else { return nil }
        return CGImage(width: side, height: side, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: side * 4, space: CGColorSpaceCreateDeviceRGB(),
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }()
}
