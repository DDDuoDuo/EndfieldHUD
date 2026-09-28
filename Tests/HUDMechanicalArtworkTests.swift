import AppKit
import QuartzCore

enum HUDMechanicalArtworkTests {
    static func run() -> Int {
        var assertions = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            assertions += 1
            if !value { fatalError(message, file: file, line: line) }
        }
        let artwork = HUDMechanicalArtwork()
        let rails = (artwork.rim.sublayers ?? []).filter { $0.name?.hasPrefix("hud.rim.white.") == true }
        check(rails.filter { $0.name?.contains(".right.") == true }.count == 2
              && rails.filter { $0.name?.contains(".left.") == true }.count == 3,
              "Reference white rails retain two right segments and three left segments")
        check(artwork.groups.last === artwork.rim
              && rails.allSatisfy { $0.superlayer === artwork.rim && $0.animationKeys() == nil },
              "White rails stay stationary on the frontmost artwork plane above the chassis and connectors")
        check(rails.allSatisfy {
            guard let bounds = ($0 as? CAShapeLayer)?.path?.boundingBoxOfPath else { return false }
            return bounds.height > 60 && (bounds.maxX < HUDMechanicalArtwork.center.x - 90
                                          || bounds.minX > HUDMechanicalArtwork.center.x + 90)
        }, "Reference rails wrap the left and right sides, leaving the top and bottom open")
        for dark in [true, false] {
            artwork.update(dark: dark, chargeColor: .systemGreen)
            check(rails.allSatisfy {
                guard let rail = $0 as? CAShapeLayer, let alpha = rail.strokeColor?.alpha else { return false }
                return alpha > 0.7 && alpha < 0.85 && rail.fillColor == nil && rail.lineCap == .butt
            }, "White rails retain slight transparency and clean flat ends in both appearances")
        }
        let themeBlue = NSColor(srgbRed: 0.12, green: 0.42, blue: 0.96, alpha: 1)
        let chargeGreen = NSColor(srgbRed: 0.18, green: 0.84, blue: 0.32, alpha: 1)
        let accentRing = artwork.secondaryRotor.sublayers?.first { $0.name == "hud.secondary.accentRing" } as? CAShapeLayer
        let trianglePaths = artwork.triangleRotors.compactMap { ($0.sublayers?.first as? CAShapeLayer)?.path }
        for dark in [true, false] {
            artwork.update(dark: dark, chargeColor: chargeGreen, accentColor: themeBlue)
            let themedLayers = artwork.triangleRotors.compactMap { $0.sublayers?.first as? CAShapeLayer } + [accentRing].compactMap { $0 }
            check(themedLayers.count == 4 && themedLayers.allSatisfy {
                guard let cg = $0.strokeColor, let rgb = NSColor(cgColor: cg)?.usingColorSpace(.sRGB) else { return false }
                return abs(rgb.redComponent - 0.12) < 0.001 && abs(rgb.greenComponent - 0.42) < 0.001
                    && abs(rgb.blueComponent - 0.96) < 0.001 && abs(rgb.alphaComponent - (dark ? 0.96 : 1)) < 0.001
            }, "The secondary ring and every floating triangle follow the supplied theme accent in both appearances")
            let indicator = artwork.indicatorGlow.sublayers?.first as? CAShapeLayer
            check(indicator?.strokeColor == chargeGreen.cgColor,
                  "Changing a decorative accent does not overwrite the semantic battery indicator color")
            check(artwork.triangleRotors.enumerated().allSatisfy {
                ($0.element.sublayers?.first as? CAShapeLayer)?.path == trianglePaths[$0.offset]
            }, "Theme changes preserve the floating triangles' cached vector paths")
        }
        let chassis = artwork.secondaryRotor.sublayers?.compactMap { $0 as? CAShapeLayer }.first
        check((chassis?.fillColor?.alpha ?? 1) < 0.7 && (chassis?.fillColor?.alpha ?? 0) > 0.3,
              "The gray back ring remains translucent")
        check(HUDMechanicalArtwork.instrumentScale == 1.18, "The shared mechanical circle uses the requested larger scale")
        let center = HUDMechanicalArtwork.center
        let spacing = HUDMechanicalArtwork.dotGridSpacing
        let radius = HUDMechanicalArtwork.dotGridRadius
        let points = HUDMechanicalArtwork.dotGridCenters
        check(points.count > 100 && points.contains(center), "The center well contains a complete dot lattice including its center")
        check(points.allSatisfy {
            let x = $0.x - center.x, y = $0.y - center.y
            return x.truncatingRemainder(dividingBy: spacing) == 0
                && y.truncatingRemainder(dividingBy: spacing) == 0
                && x * x + y * y <= radius * radius
        }, "Every dot lies on the same uniformly spaced Cartesian grid inside the circle")
        check(points.allSatisfy { point in
            points.contains(CGPoint(x: 2 * center.x - point.x, y: point.y))
                && points.contains(CGPoint(x: point.x, y: 2 * center.y - point.y))
        }, "The dot grid is symmetric across both central axes")
        for rowY in Set(points.map(\.y)) {
            let row = points.filter { $0.y == rowY }.map(\.x).sorted()
            check(zip(row, row.dropFirst()).allSatisfy { $1 - $0 == spacing },
                  "Clipping leaves no irregular gaps between adjacent dots in a row")
        }
        let gridLayers = (artwork.inner.sublayers ?? []).filter { $0.name == "hud.backplane.dots" }
        check(gridLayers.count == 1 && (gridLayers.first as? CAShapeLayer)?.path != nil,
              "All dots share one cached vector layer instead of separate animated particles")

        let rotors = artwork.triangleRotors
        let rotorIDs = rotors.map(ObjectIdentifier.init)
        let markerIDs = rotors.compactMap { $0.sublayers?.first }.map(ObjectIdentifier.init)
        check(rotors.count == 3 && Set(rotorIDs).count == 3 && markerIDs.count == 3,
              "Each triangle owns a distinct permanent rotor and marker child")
        check(rotors.allSatisfy {
            $0.bounds == CGRect(x: 0, y: 0, width: 500, height: 500) && $0.position == center
        }, "Triangle rotors share a stable central pivot")

        var generator = SeededGenerator(seed: 0x454E_4446_4945_4C44)
        var previous: [HUDMechanicalArtwork.TriangleOrbit] = []
        for _ in 0..<12 {
            let orbits = artwork.randomizeTriangleOrbits(using: &generator)
            check(orbits.count == 3 && orbits != previous, "Each opening receives new orbit phases, speeds and directions")
            check(orbits.allSatisfy { $0.phase >= 0 && $0.phase < .pi * 2 && (28...64).contains($0.period) },
                  "Every orbit has a finite circular phase and a slow bounded period")
            check(Set(orbits.map(\.period)).count == 3 && Set(orbits.map(\.direction)) == Set([CGFloat(-1), 1]),
                  "The triangles always have independent speeds and both rotation directions")
            let phases = orbits.map(\.phase).sorted()
            let separations = [phases[1] - phases[0], phases[2] - phases[1], phases[0] + .pi * 2 - phases[2]]
            check(separations.allSatisfy { $0 > .pi / 3 }, "Random initial positions do not clump together")
            check(orbits.allSatisfy { $0.fromValue == 0 && abs($0.toValue) == .pi * 2 },
                  "Animation offsets make one signed turn without duplicating the static random phase")
            check(rotors.enumerated().allSatisfy { index, rotor in
                guard let marker = rotor.sublayers?.first else { return false }
                return CATransform3DIsIdentity(rotor.transform)
                    && CATransform3DEqualToTransform(marker.transform, CATransform3DMakeRotation(orbits[index].phase, 0, 0, 1))
            }, "Randomization changes only marker children, preserving motion-managed rotor baselines")
            previous = orbits
        }
        check(artwork.triangleRotors.map(ObjectIdentifier.init) == rotorIDs
              && artwork.triangleRotors.compactMap { $0.sublayers?.first }.map(ObjectIdentifier.init) == markerIDs,
              "Repeated openings retain every rotor and marker layer")

        let motion = HUDMotionController(planes: [])
        func registerOrbits() {
            for (index, orbit) in artwork.triangleOrbits.enumerated() {
                _ = motion.registerAmbient(layer: rotors[index], key: "triangle.\(index)", keyPath: "transform.rotation.z",
                    fromValue: orbit.fromValue, toValue: orbit.toValue, duration: orbit.period, autoreverses: false)
            }
        }
        registerOrbits()
        motion.start(reducedMotion: false)
        check(motion.ambientAnimationCount == 3, "Only three Core Animation tracks drive the three independent triangles")
        check(rotors.enumerated().allSatisfy { index, rotor in
            guard let animation = rotor.animation(forKey: "ambient.triangle.\(index)") as? CABasicAnimation else { return false }
            return animation.isAdditive && !animation.autoreverses && animation.duration == artwork.triangleOrbits[index].period
        }, "Triangle rotation remains linear, additive and independent of model poses")
        motion.stop(freezePresentation: false)
        check(motion.ambientAnimationCount == 0, "Stopping removes all triangle animation tracks")
        artwork.randomizeTriangleOrbits(using: &generator)
        registerOrbits()
        let staticPoses = rotors.compactMap { $0.sublayers?.first?.transform }
        motion.start(reducedMotion: true)
        check(motion.ambientAnimationCount == 0 && rotors.enumerated().allSatisfy { index, rotor in
            guard let marker = rotor.sublayers?.first else { return false }
            return CATransform3DEqualToTransform(marker.transform, staticPoses[index])
        }, "Reduce Motion preserves newly randomized static positions without installing ambient motion")
        motion.stop(freezePresentation: false)
        return assertions
    }

    private struct SeededGenerator: RandomNumberGenerator {
        var seed: UInt64
        mutating func next() -> UInt64 {
            seed = seed &* 6364136223846793005 &+ 1442695040888963407
            return seed
        }
    }
}
