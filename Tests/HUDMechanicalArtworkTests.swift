import AppKit
import QuartzCore

enum HUDMechanicalArtworkTests {
    static func run() -> Int {
        var assertions = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            assertions += 1
            if !value { fatalError(message, file: file, line: line) }
        }
        func isLinear(_ timing: CAMediaTimingFunction?) -> Bool {
            guard let timing else { return false }
            var first: [Float] = [0, 0], second: [Float] = [0, 0]
            first.withUnsafeMutableBufferPointer { timing.getControlPoint(at: 1, values: $0.baseAddress!) }
            second.withUnsafeMutableBufferPointer { timing.getControlPoint(at: 2, values: $0.baseAddress!) }
            return abs(first[0] - first[1]) < 0.00001 && abs(second[0] - second[1]) < 0.00001
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
            let themedLayers = [accentRing].compactMap { $0 }
            check(themedLayers.count == 1 && themedLayers.allSatisfy {
                guard let cg = $0.strokeColor, let rgb = NSColor(cgColor: cg)?.usingColorSpace(.sRGB) else { return false }
                return abs(rgb.redComponent - 0.12) < 0.001 && abs(rgb.greenComponent - 0.42) < 0.001
                    && abs(rgb.blueComponent - 0.96) < 0.001 && abs(rgb.alphaComponent - (dark ? 0.96 : 1)) < 0.001
            }, "The secondary ring follows the supplied theme accent in both appearances")
            let triangleImage = HUDWatchArtwork.image(.triangle, tint: themeBlue)
            check(triangleImage != nil && artwork.triangleRotors.allSatisfy {
                guard let expected = triangleImage,
                      let contents = $0.sublayers?.first?.sublayers?.first?.contents else { return false }
                return (contents as AnyObject) === (expected as AnyObject)
            }, "Every marker uses the cached source triangle tinted to the chosen accent")
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
        let gridLayers = (artwork.meshRotor.sublayers ?? []).filter { $0.name == "hud.backplane.dots" }
        check(gridLayers.count == 1 && (gridLayers.first as? CAShapeLayer)?.path != nil,
              "All dots share one cached vector layer instead of separate animated particles")
        check(artwork.meshRotor.superlayer?.mask != nil && artwork.meshRotor.position == center,
              "The mesh rotates about the instrument's center inside a stationary clipping disc")
        let midRing = artwork.secondaryRotor.sublayers?.first { $0.name == "hud.watch.midRing" }
        check(midRing?.contents != nil && midRing?.superlayer === artwork.secondaryRotor,
              "The source segmented ticks share the ring's motion behind the readout well")

        let rotors = artwork.triangleRotors
        let rotorIDs = rotors.map(ObjectIdentifier.init)
        let markerIDs = rotors.compactMap { $0.sublayers?.first }.map(ObjectIdentifier.init)
        check(rotors.count == 6 && Set(rotorIDs).count == 6 && markerIDs.count == 6,
              "Each triangle owns a distinct permanent rotor and marker child")
        check(rotors.allSatisfy {
            $0.bounds == CGRect(x: 0, y: 0, width: 500, height: 500) && $0.position == center
        }, "Triangle rotors share a stable central pivot")

        let phases = HUDMechanicalArtwork.trianglePhases
        check(zip(phases, phases.dropFirst()).allSatisfy { abs($0 - $1 - .pi / 3) < 0.00001 },
              "The six PC menu markers remain equally spaced instead of randomizing on opening")
        check(rotors.enumerated().allSatisfy { index, rotor in
            guard let marker = rotor.sublayers?.first else { return false }
            return CATransform3DIsIdentity(rotor.transform)
                && CATransform3DEqualToTransform(marker.transform, CATransform3DMakeRotation(phases[index], 0, 0, 1))
        }, "Static child phases remain independent of motion-managed rotor baselines")
        check(artwork.triangleRotors.map(ObjectIdentifier.init) == rotorIDs
              && artwork.triangleRotors.compactMap { $0.sublayers?.first }.map(ObjectIdentifier.init) == markerIDs,
              "Repeated openings retain every rotor and marker layer")

        let motion = HUDMotionController(planes: [])
        func registerOrbits() {
            for (index, rotor) in rotors.enumerated() {
                _ = motion.registerAmbient(layer: rotor, key: "triangle.\(index)", keyPath: "transform.rotation.z",
                    fromValue: 0, toValue: HUDMechanicalArtwork.watchRingExcursion,
                    duration: HUDMechanicalArtwork.watchLoopLegDuration, timingFunction: .linear)
            }
            _ = motion.registerAmbient(layer: artwork.meshRotor, key: "mesh", keyPath: "transform.rotation.z",
                fromValue: 0, toValue: HUDMechanicalArtwork.watchMeshExcursion,
                duration: HUDMechanicalArtwork.watchLoopLegDuration, timingFunction: .linear)
        }
        registerOrbits()
        motion.start(reducedMotion: false)
        check(motion.ambientAnimationCount == 7, "Six grouped markers and one mesh use only retained Core Animation tracks")
        check(rotors.enumerated().allSatisfy { index, rotor in
            guard let animation = rotor.animation(forKey: "ambient.triangle.\(index)") as? CABasicAnimation else { return false }
            return animation.isAdditive && animation.autoreverses
                && animation.duration == HUDMechanicalArtwork.watchLoopLegDuration
                && animation.toValue as? CGFloat == HUDMechanicalArtwork.watchRingExcursion
                && isLinear(animation.timingFunction)
        }, "Triangle rotation uses the source's bounded linear excursion without full spins")
        let starts = rotors.enumerated().compactMap { index, rotor in
            rotor.animation(forKey: "ambient.triangle.\(index)")?.beginTime
        }
        check(Set(starts).count == 1, "Every marker shares one loop phase rather than six unrelated clocks")
        let mesh = artwork.meshRotor.animation(forKey: "ambient.mesh") as? CABasicAnimation
        check(mesh?.toValue as? CGFloat == HUDMechanicalArtwork.watchMeshExcursion
              && mesh?.beginTime == starts.first && mesh?.duration == HUDMechanicalArtwork.watchLoopLegDuration,
              "The mesh turns in the opposite direction on the same loop clock")
        motion.configure(parallax: 1, perspective: 1, ambient: false)
        check(motion.ambientAnimationCount == 0 && CATransform3DIsIdentity(artwork.meshRotor.transform),
              "Disabling ambient motion clears the ring and mesh tracks and restores their static poses")
        motion.configure(parallax: 1, perspective: 1, ambient: true)
        check(motion.ambientAnimationCount == 7, "Enabling ambient resumes one track per retained rotor")
        motion.stop(freezePresentation: false)
        check(motion.ambientAnimationCount == 0, "Stopping removes all triangle animation tracks")
        registerOrbits()
        let staticPoses = rotors.compactMap { $0.sublayers?.first?.transform }
        motion.start(reducedMotion: true)
        check(motion.ambientAnimationCount == 0 && rotors.enumerated().allSatisfy { index, rotor in
            guard let marker = rotor.sublayers?.first else { return false }
            return CATransform3DEqualToTransform(marker.transform, staticPoses[index])
        }, "Reduce Motion preserves the six authored static positions without installing ambient motion")
        motion.stop(freezePresentation: false)
        return assertions
    }

}
