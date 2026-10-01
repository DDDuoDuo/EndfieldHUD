import Foundation
import CoreGraphics
import simd

enum HUDSourceWatchCameraTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !value { fatalError(message, file: file, line: line) }
        }
        func close(_ value: Double, _ expected: Double, _ message: String, tolerance: Double = 1e-7,
                   file: StaticString = #file, line: UInt = #line) {
            check(value.isFinite && abs(value - expected) <= tolerance, message, file: file, line: line)
        }
        func fails(_ message: String, _ operation: () throws -> Void) {
            do { try operation(); check(false, message) } catch { check(true, message) }
        }
        do {
            guard let sourceURL = HUDResources.url(for: "WatchSource/Scene/runtime-root-camera.json") else {
                fatalError("Missing original runtime root/camera asset")
            }
            let source = try HUDSourceJSON.decoder().decode(HUDSourceJSONValue.self, from: Data(contentsOf: sourceURL))
            let model = try HUDSourceWatchCamera(runtimeRoot: source)
            check(model.worldRootID.rawValue == "CAB-c8c00fab94bd2bb66b9b7e5ccbec370c:-7961340619866959176", "Gyro and ScaleHelper resolve their actual shared WorldRoot")
            close(model.verticalFieldOfViewDegrees, 15.381799697875977, "Original UI camera FOV remains unchanged")
            let normal = try model.layout(screenSize: SIMD2(1920, 1080))
            check(normal.canvasSize == SIMD2(2400, 1350), "PC reference scale1.25 changes the runtime canvas to2400×1350")
            let narrow = try model.layout(screenSize: SIMD2(1728, 1080))
            check(narrow.canvasSize == SIMD2(2400, 1500), "8:5 recording preserves width and expands canvas height")
            close(narrow.worldHeight, 8.102614402770996, "World-space canvas height from30*tan(original half FOV)*2", tolerance: 1e-6)
            close(narrow.scale, 0.005401742644608021, "Native float arithmetic gives world canvas scale for8:5", tolerance: 1e-9)
            let wide = try model.layout(screenSize: SIMD2(2560, 1080))
            close(wide.canvasSize.x, 3200, "Wide viewport expands the canvas width", tolerance: 0.001)
            close(wide.canvasSize.y, 1350, "Wide viewport preserves standard canvas height")
            close(wide.scale, normal.scale, "Wide screen keeps the standard vertical world scale")
            let sourceScaleOne = try HUDSourceWatchCamera(runtimeRoot: source, referenceResolutionScale: 1)
            let scaleOne = try sourceScaleOne.layout(screenSize: SIMD2(1728, 1080))
            close(scaleOne.scale / narrow.scale, 1.25, "Runtime PC multiplier is not the serialized prefab scale", tolerance: 2e-7)

            let screen = SIMD2<Double>(1728, 1080)
            let center = try model.gyro.targetEuler(mouseUnity: screen / 2, screenSize: screen)
            close(center.x, 0.10612103036407602, "Original pitch curve has an intentional nonzero center", tolerance: 1e-7)
            close(center.y, 0.01750883460044861, "Original yaw curve center value is not zero")
            let upperRight = try model.gyro.targetEuler(mouseUnity: screen, screenSize: screen)
            check(upperRight == SIMD3(-2, 3, 0), "Mouse Y drives pitch−2, mouse X drives yaw+3")
            let lowerLeft = try model.gyro.targetEuler(mouseUnity: .zero, screenSize: screen)
            check(lowerLeft == SIMD3(2, -3, 0), "Opposite corner follows original curve endpoints")
            let outside = try model.gyro.targetEuler(mouseUnity: SIMD2(-100, 1500), screenSize: screen)
            check(outside == SIMD3(-2, -3, 0), "Pointer clamps to actual screen extent before normalization")
            check(try model.gyro.targetEuler(mouseUnity: screen, screenSize: screen, detect: false) == .zero,
                  "Disabled detection targets zero instead of evaluating center-biased curves")
            let order = try HUDSourceWatchCamera.quaternion(eulerDegrees: SIMD3(90, 90, 0)).matrix()
            let forward: SIMD4<Double> = simd_mul(order, SIMD4<Double>(0, 0, 1, 0))
            close(forward.x, 0, "Unity ZXY quaternion order does not reverse yaw and pitch")
            close(forward.y, -1, "Unity +90 pitch rotates forward down before yaw")
            close(forward.z, 0, "Combined known-axis rotation has no residual forward component")

            let frame = try model.frame(screenSize: screen)
            let viewport = CGRect(x: 0, y: 0, width: 1728, height: 1080)
            guard let projected = frame.camera.project(.zero, world: frame.worldRoot, viewport: viewport) else {
                fatalError("Original world root is visible to the original camera")
            }
            close(Double(projected.point.x), 864, "Original Camera and UINode share X/Y world origin")
            close(Double(projected.point.y), 540, "Source camera parent offset100 cancels WorldRoot parent's offset100")
            let rect = HUDSourceRect(origin: -narrow.canvasSize / 2, size: narrow.canvasSize)
            guard let topLeft = frame.camera.project(SIMD3(-1200, 750, 0), world: frame.worldRoot, viewport: viewport) else {
                fatalError("Source canvas corner projects")
            }
            close(Double(topLeft.point.x), 0, "Runtime canvas world scale reaches the viewport left", tolerance: 0.001)
            close(Double(topLeft.point.y), 0, "Runtime canvas vertical extent reaches viewport top", tolerance: 0.001)
            let tilted = try model.frame(screenSize: screen, localRotation: HUDSourceWatchCamera.quaternion(eulerDegrees: upperRight))
            guard let p = tilted.camera.project(SIMD3(300, 200, 0), world: tilted.worldRoot, viewport: viewport),
                  let hit = tilted.camera.hit(p.point, world: tilted.worldRoot, rect: rect, viewport: viewport) else {
                fatalError("Tilted source render and raycast share the same matrix")
            }
            close(hit.x, 300, "Inverse hit recovers the tilted local X", tolerance: 1e-6)
            close(hit.y, 200, "Inverse hit recovers the tilted local Y", tolerance: 1e-6)

            var motion = try HUDSourceWatchGyroMotion()
            check(try motion.retarget(eulerDegrees: SIMD3(0, 4, 0), at: 0, duration: 0.5), "New mouse angle begins original quaternion tween")
            let midpoint = try motion.rotation(at: 0.25)
            close(midpoint.y, sin(1.5 * .pi / 180), "OutQuad halfway gives75% of a known single-axis quaternion rotation")
            check(try !motion.retarget(eulerDegrees: SIMD3(0, 4, 0), at: 0.25, duration: 0.5), "Unchanged target does not rewind a running tween")
            _ = try motion.retarget(eulerDegrees: SIMD3(0, -4, 0), at: 0.25, duration: 0.5)
            close(try motion.rotation(at: 0.25).y, midpoint.y, "Fast retarget snapshots current displayed rotation")
            close(try motion.rotation(at: 0.5).y, sin(-1.125 * .pi / 180), "Retarget traverses3deg→−4deg at75% giving−2.25deg")
            motion.finishIfNeeded(at: 0.75)
            check(!motion.isAnimating, "Finite gyro tween stops at its original duration")
            close(try motion.rotation(at: 2).y, sin(-2 * .pi / 180), "Finished gyro holds its target")
            _ = try motion.retarget(eulerDegrees: .zero, at: 2, duration: 0.5, reduceMotion: true)
            let reducedRotation = try motion.rotation(at: 2)
            check(!motion.isAnimating && reducedRotation == .identity, "Reduce Motion cancels interpolation immediately")
            _ = try motion.retarget(eulerDegrees: SIMD3(0, 4, 0), at: 3, duration: 0.5)
            let beforeStop = try motion.rotation(at: 3.2)
            try motion.stop(at: 3.2)
            let stoppedRotation = try motion.rotation(at: 10)
            check(!motion.isAnimating && stoppedRotation == beforeStop, "Teardown freezes gyro without an ownerless animation")
            fails("Invalid screen size is rejected") { _ = try model.layout(screenSize: .zero) }
            fails("Invalid pointer input is rejected") { _ = try model.gyro.targetEuler(mouseUnity: SIMD2(.nan, 0), screenSize: screen) }
            fails("Malformed runtime asset is rejected instead of guessing a camera") { _ = try HUDSourceWatchCamera(runtimeRoot: .array([])) }
        } catch { fatalError("Source Watch camera test failed: \(error)") }
        return count
    }
}
