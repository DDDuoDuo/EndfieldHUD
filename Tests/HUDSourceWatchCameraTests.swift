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
            close(model.verticalFieldOfViewDegrees, 15.381799697875977, "Serialized standard FOV remains the source default")
            let normal = try model.layout(screenSize: SIMD2(1920, 1080))
            check(normal.canvasSize == SIMD2(2400, 1350), "PC reference scale1.25 changes the runtime canvas to2400×1350")
            let narrow = try model.layout(screenSize: SIMD2(1728, 1080))
            check(narrow.canvasSize == SIMD2(2400, 1500), "8:5 recording preserves width and expands canvas height")
            close(narrow.runtimeVerticalFieldOfViewDegrees, 17.06695749507, "8:5 uses the original horizontal-FOV preservation rule", tolerance: 1e-6)
            close(narrow.worldHeight, 9.002904891967773, "World-space height uses the adjusted runtime camera FOV", tolerance: 1e-6)
            close(narrow.scale, 0.006001936737447977, "PC runtime canvas uses CUR_STANDARD width and adjusted FOV", tolerance: 1e-9)
            close(narrow.scale, normal.scale, "Narrow FOV adjustment preserves the standard world scale", tolerance: 1e-9)
            let wide = try model.layout(screenSize: SIMD2(2560, 1080))
            close(wide.canvasSize.x, 3200, "Wide viewport expands the canvas width", tolerance: 0.001)
            close(wide.canvasSize.y, 1350, "Wide viewport preserves standard canvas height")
            close(wide.scale, normal.scale, "Wide screen keeps the standard vertical world scale")
            close(wide.runtimeVerticalFieldOfViewDegrees, model.verticalFieldOfViewDegrees, "Wider screens retain standard vertical FOV")
            func horizontalFOV(_ vertical: Double, _ aspect: Double) -> Double {
                2 * atan(tan(vertical * .pi / 360) * aspect) * 180 / .pi
            }
            let standardHorizontal = horizontalFOV(model.verticalFieldOfViewDegrees, 16.0 / 9.0)
            close(horizontalFOV(narrow.runtimeVerticalFieldOfViewDegrees, 1.6), standardHorizontal,
                  "Independent trigonometric check preserves horizontal coverage at8:5", tolerance: 1e-6)
            let portrait = try model.layout(screenSize: SIMD2(1080, 1920))
            close(horizontalFOV(portrait.runtimeVerticalFieldOfViewDegrees, 9.0 / 16.0), standardHorizontal,
                  "Narrower resize retains horizontal coverage without fitted camera values", tolerance: 2e-6)
            let narrowAgain = try model.layout(screenSize: SIMD2(1728, 1080))
            close(narrowAgain.runtimeVerticalFieldOfViewDegrees, narrow.runtimeVerticalFieldOfViewDegrees,
                  "Resize recomputes from standard FOV rather than adjusting an already adjusted FOV")
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
            let shaderWorldPoint = simd_mul(model.shaderCameraToWorld, SIMD4<Double>(0, 0, -30, 1))
            check(shaderWorldPoint == SIMD4(0, 100, 30, 1),
                  "Camera-space negative Z maps to the original UI world plane in HG inverse view")
            check(model.cameraWorld.columns.2 == SIMD4(0, 0, 1, 0)
                && model.shaderCameraToWorld.columns.2 == SIMD4(0, 0, -1, 0),
                  "Transform forward and source Camera inverse view have distinct Z bases")
            let cpuProjection = HUDSourceGeometry.floatMatrix(frame.camera.projection)
            var gpuProjection = cpuProjection
            for column in 0..<4 { gpuProjection[column].y = -gpuProjection[column].y }
            let uiParams = try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: gpuProjection,
                near: Float(model.near), far: Float(model.far))
            check(uiParams == SIMD4(-1, Float(model.near), Float(model.far), 1 / Float(model.far)),
                  "Actual Y-flipped adapter projection produces HG sign/near/far/reciprocal tuple")
            let relativeVP = try HUDSourceWatchCamera.viewNoTranslationProjection(
                gpuProjection: gpuProjection, view: frame.camera.view)
            let sourceCenter = simd_mul(relativeVP, SIMD4<Float>(0, 0, 30, 1))
            close(Double(sourceCenter.x / sourceCenter.w), 0,
                  "Injected source world-center remains at the viewport center")
            close(Double(sourceCenter.y / sourceCenter.w), 0,
                  "Camera Y100 is subtracted once rather than again in the shader matrix")
            // Use a translated, rotated camera and independent camera-local
            // points, so an identity-only or translation-only fix cannot pass.
            let rotatedWorld = simd_mul(HUDSourceGeometry.translation(SIMD3(27, -48, 5)),
                try HUDSourceWatchCamera.quaternion(eulerDegrees: SIMD3(22, -13, 7)).matrix())
            let rotatedView = simd_inverse(rotatedWorld)
            let rotatedRelativeVP = try HUDSourceWatchCamera.viewNoTranslationProjection(
                gpuProjection: gpuProjection, view: rotatedView)
            let cameraPosition = SIMD3<Float>(27, -48, 5)
            for localPoint in [SIMD4<Double>(0, 0, 30, 1), SIMD4(3, -2, 20, 1), SIMD4(-4, 6, 60, 1)] {
                let worldPoint = simd_mul(rotatedWorld, localPoint)
                let relativePoint = SIMD3(Float(worldPoint.x), Float(worldPoint.y), Float(worldPoint.z)) - cameraPosition
                let injectedClip = simd_mul(rotatedRelativeVP,
                    SIMD4(relativePoint.x, relativePoint.y, relativePoint.z, 1))
                let independentlyExpected = simd_mul(gpuProjection,
                    SIMD4(Float(localPoint.x), Float(localPoint.y), Float(localPoint.z), 1))
                for axis in 0..<4 {
                    close(Double(injectedClip[axis]), Double(independentlyExpected[axis]),
                          "Camera-relative shader preserves projected clip axis\(axis) under camera rotation/translation",
                          tolerance: 0.0001)
                }
            }
            var invalidView = matrix_identity_double4x4
            invalidView[2].x = .infinity
            fails("Nonfinite relative-view input is rejected") {
                _ = try HUDSourceWatchCamera.viewNoTranslationProjection(gpuProjection: gpuProjection, view: invalidView)
            }
            let unflippedParams = try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: cpuProjection, near: 0.3, far: 200)
            check(unflippedParams.x == 1, "HG Y sign follows the supplied GPU matrix, not a fixed OS constant")
            let negativeW = simd_float4x4(diagonal: SIMD4<Float>(1, 1, 1, -1))
            check(try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: negativeW, near: 1, far: 10).x == -1,
                  "HG sign divides inverse-projection Y by homogeneous W")
            fails("Singular GPU projection is rejected") {
                _ = try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: simd_float4x4(diagonal: .zero), near: 0.3, far: 200)
            }
            var invalidProjection = matrix_identity_float4x4
            invalidProjection[0].x = .nan
            fails("Nonfinite GPU projection is rejected") {
                _ = try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: invalidProjection, near: 0.3, far: 200)
            }
            let zeroProbeW = simd_float4x4(columns: (SIMD4(1, 0, 0, 0), SIMD4(0, 1, 0, 0),
                SIMD4(0, 0, 0, 1), SIMD4(0, 0, 1, 0)))
            fails("Finite invertible projection with zero probe W is rejected") {
                _ = try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: zeroProbeW, near: 0.3, far: 200)
            }
            fails("Invalid clipping planes are rejected") {
                _ = try HUDSourceWatchCamera.uiProjectionParams(gpuProjection: cpuProjection, near: 200, far: 0.3)
            }
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
            fails("Nonfinite resize dimensions are rejected") { _ = try model.runtimeVerticalFieldOfViewDegrees(screenSize: SIMD2(.infinity, 1080)) }
            fails("Invalid pointer input is rejected") { _ = try model.gyro.targetEuler(mouseUnity: SIMD2(.nan, 0), screenSize: screen) }
            fails("Malformed runtime asset is rejected instead of guessing a camera") { _ = try HUDSourceWatchCamera(runtimeRoot: .array([])) }
        } catch { fatalError("Source Watch camera test failed: \(error)") }
        return count
    }
}
