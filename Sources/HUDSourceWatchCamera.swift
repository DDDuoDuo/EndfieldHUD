import Foundation
import simd

/// Runtime rules from UICanvasScaleHelper, UIConst and UIGyroscopeEffect.
/// Uses the extracted camera/root assets; no fitted projection or prefab scale.
struct HUDSourceWatchCamera {
    struct Layout {
        let canvasSize: SIMD2<Double>
        let scale: Double
        let screenAspect: Double
        let worldHeight: Double
    }
    struct Frame {
        let camera: HUDSourceCamera
        let worldRoot: simd_double4x4
        let layout: Layout
    }
    struct Gyro {
        let enabled: Bool
        let pitchCurve: HUDSourceScalarCurve
        let yawCurve: HUDSourceScalarCurve
        let maxPitch: Double
        let maxYaw: Double
        let duration: Double

        init(source: HUDSourceJSONValue) throws {
            enabled = source["enableDetect"].flag()
            maxPitch = source["x"]["maxAngle"].float()
            maxYaw = source["y"]["maxAngle"].float()
            duration = source["time"].float()
            guard source["ease"].number == 6, duration.isFinite, duration > 0,
                  maxPitch.isFinite, maxYaw.isFinite else {
                throw HUDSourceError.invalid("Unsupported source gyro ease/duration")
            }
            func curve(_ axis: String) throws -> HUDSourceScalarCurve {
                func slope(_ value: HUDSourceJSONValue) throws -> Double {
                    if let n = value.number { return n }
                    if value.string == "Infinity" { return .infinity }
                    if value.string == "-Infinity" { return -.infinity }
                    throw HUDSourceError.invalid("Missing gyro curve slope")
                }
                let keys = try source[axis]["valueCurve"]["m_Curve"].array.map { k -> HUDSourceScalarKey in
                    guard let time = k["time"].number, let value = k["value"].number else {
                        throw HUDSourceError.invalid("Missing gyro curve key")
                    }
                    return try HUDSourceScalarKey(time: time, value: value,
                        inSlope: slope(k["inSlope"]), outSlope: slope(k["outSlope"]),
                        weightedMode: Int(k["weightedMode"].float()),
                        inWeight: k["inWeight"].float(1 / 3), outWeight: k["outWeight"].float(1 / 3))
                }
                return try HUDSourceScalarCurve(keys: keys)
            }
            pitchCurve = try curve("x"); yawCurve = try curve("y")
        }

        /// Input coordinates are Unity screen coordinates (+Y up). Caller must
        /// flip an AppKit point once. Outside-window coordinates clamp to edges.
        func targetEuler(mouseUnity: SIMD2<Double>, screenSize: SIMD2<Double>,
                         detect: Bool = true) throws -> SIMD3<Double> {
            guard [mouseUnity.x, mouseUnity.y, screenSize.x, screenSize.y].allSatisfy({ $0.isFinite }),
                  screenSize.x > 0, screenSize.y > 0 else {
                throw HUDSourceError.invalid("Invalid gyro pointer/screen size")
            }
            guard enabled && detect else { return .zero }
            // The native code clamps actual pixels, subtracts screen half-size,
            // then divides by that half-size. Preserve float input arithmetic.
            let w = Float(screenSize.x), h = Float(screenSize.y)
            let x = (min(w, max(0, Float(mouseUnity.x))) - w * 0.5) / (w * 0.5)
            let y = (min(h, max(0, Float(mouseUnity.y))) - h * 0.5) / (h * 0.5)
            guard let pitchValue = pitchCurve.sample(at: Double(y)), let yawValue = yawCurve.sample(at: Double(x)) else {
                throw HUDSourceError.invalid("Source gyro curves cannot sample pointer")
            }
            let pitch = Float(pitchValue) * Float(maxPitch)
            let yaw = Float(yawValue) * Float(maxYaw)
            return SIMD3(Double(pitch), Double(yaw), 0)
        }
    }

    let worldRootID: HUDSourceID
    let gyro: Gyro
    let cameraWorld: simd_double4x4
    let worldParent: simd_double4x4
    let rootPosition: SIMD3<Double>
    let rootRotation: HUDSourceQuaternion
    let verticalFieldOfViewDegrees: Double
    let near: Double
    let far: Double
    let referenceResolutionScale: Double

    init(runtimeRoot: HUDSourceJSONValue, referenceResolutionScale: Double = 1.25) throws {
        let objects = runtimeRoot.array
        func unique(_ rows: [HUDSourceJSONValue], _ name: String) throws -> HUDSourceJSONValue {
            guard rows.count == 1 else { throw HUDSourceError.invalid("Expected one source \(name)") }
            return rows[0]
        }
        func id(_ row: HUDSourceJSONValue) throws -> HUDSourceID {
            guard let cab = row["cab"].string, let path = row["path_id"].string else {
                throw HUDSourceError.invalid("Missing runtime source ID")
            }
            return HUDSourceID(rawValue: cab + ":" + path)
        }
        let gyroObject = try unique(objects.filter { $0["script"]["m_ClassName"].string == "UIGyroscopeEffect" }, "gyro")
        let rootGO = gyroObject["data"]["m_GameObject"]["m_PathID"].string
        let rootCAB = gyroObject["cab"].string
        let root = try unique(objects.filter {
            $0["cab"].string == rootCAB && $0["type"].string == "RectTransform"
                && $0["data"]["m_GameObject"]["m_PathID"].string == rootGO
        }, "world root")
        let scaleHelper = try unique(objects.filter {
            $0["script"]["m_ClassName"].string == "UICanvasScaleHelper" && $0["cab"].string == rootCAB
                && $0["data"]["m_GameObject"]["m_PathID"].string == rootGO
        }, "world scale helper")
        guard scaleHelper["data"]["m_Enabled"].flag(true), gyroObject["data"]["m_Enabled"].flag(true),
              referenceResolutionScale.isFinite, referenceResolutionScale > 0 else {
            throw HUDSourceError.invalid("Invalid source world scale configuration")
        }
        let camera = try unique(objects.filter { $0["type"].string == "Camera" && $0["data"]["m_Enabled"].flag(true) }, "UI camera")
        let cameraTransform = try unique(objects.filter {
            $0["type"].string == "Transform" && $0["cab"].string == camera["cab"].string
                && $0["data"]["m_GameObject"]["m_PathID"].string == camera["data"]["m_GameObject"]["m_PathID"].string
        }, "camera transform")
        let viewport = camera["data"]["m_NormalizedViewPortRect"]
        guard !camera["data"]["orthographic"].flag(), viewport["x"].number == 0, viewport["y"].number == 0,
              viewport["width"].number == 1, viewport["height"].number == 1,
              camera["data"]["m_LensShift"].vector2 == .zero else {
            throw HUDSourceError.invalid("Unsupported source camera viewport/projection")
        }
        var transforms: [HUDSourceID: HUDSourceJSONValue] = [:]
        for object in objects where object["type"].string == "Transform" || object["type"].string == "RectTransform" {
            transforms[try id(object)] = object
        }
        func quaternion(_ value: HUDSourceJSONValue) throws -> HUDSourceQuaternion {
            guard let x = value["x"].number, let y = value["y"].number,
                  let z = value["z"].number, let w = value["w"].number else {
                throw HUDSourceError.invalid("Missing runtime rotation")
            }
            return try HUDSourceQuaternion(x, y, z, w).normalized()
        }
        func local(_ object: HUDSourceJSONValue) throws -> simd_double4x4 {
            let raw = object["data"]
            let rotation = try quaternion(raw["m_LocalRotation"]).matrix()
            let position = HUDSourceGeometry.translation(raw["m_LocalPosition"].vector3)
            return simd_mul(simd_mul(position, rotation), HUDSourceGeometry.scale(raw["m_LocalScale"].vector3))
        }
        func parent(of object: HUDSourceJSONValue) throws -> HUDSourceID? {
            let p = object["data"]["m_Father"]
            guard let path = p["m_PathID"].string, let cab = object["cab"].string, p["m_FileID"].number == 0 else {
                throw HUDSourceError.invalid("Missing/cross-asset runtime parent")
            }
            return path == "0" ? nil : HUDSourceID(rawValue: cab + ":" + path)
        }
        func world(_ object: HUDSourceJSONValue, visited: Set<HUDSourceID> = []) throws -> simd_double4x4 {
            let key = try id(object)
            guard !visited.contains(key) else { throw HUDSourceError.invalid("Cyclic runtime camera hierarchy") }
            let m = try local(object)
            guard let parentID = try parent(of: object) else { return m }
            guard let p = transforms[parentID] else { throw HUDSourceError.invalid("Missing runtime camera ancestor \(parentID)") }
            return try simd_mul(world(p, visited: visited.union([key])), m)
        }
        if let parentID = try parent(of: root) {
            guard let p = transforms[parentID] else { throw HUDSourceError.invalid("Missing source world root parent") }
            worldParent = try world(p)
        } else { worldParent = matrix_identity_double4x4 }
        cameraWorld = try world(cameraTransform)
        guard HUDSourceGeometry.inverse(cameraWorld) != nil, HUDSourceGeometry.isFinite(worldParent) else {
            throw HUDSourceError.invalid("Invalid runtime camera/root transforms")
        }
        worldRootID = try id(root)
        rootPosition = root["data"]["m_LocalPosition"].vector3
        rootRotation = try quaternion(root["data"]["m_LocalRotation"])
        verticalFieldOfViewDegrees = camera["data"]["field of view"].float()
        near = camera["data"]["near clip plane"].float(); far = camera["data"]["far clip plane"].float()
        self.referenceResolutionScale = referenceResolutionScale
        gyro = try Gyro(source: gyroObject["data"])
        guard verticalFieldOfViewDegrees > 0, verticalFieldOfViewDegrees < 180, near > 0, far > near else {
            throw HUDSourceError.invalid("Invalid source camera clipping/FOV")
        }
    }

    func layout(screenSize: SIMD2<Double>) throws -> Layout {
        guard screenSize.x.isFinite, screenSize.y.isFinite, screenSize.x > 0, screenSize.y > 0 else {
            throw HUDSourceError.invalid("Invalid source screen dimensions")
        }
        let worldPosition: SIMD4<Double> = simd_mul(worldParent, SIMD4<Double>(rootPosition.x, rootPosition.y, rootPosition.z, 1))
        // False 'useLocalPosition' branch in the actual WorldSpace caller reads
        // Transform.position.z. It is not distance along camera forward.
        let z = abs(Float(worldPosition.z))
        let fov = Float(verticalFieldOfViewDegrees)
        let radians = ((fov * 0.5) / 180) * Float.pi
        let halfHeight = Float(tan(Double(radians))) * z
        let height = halfHeight + halfHeight
        let aspect = Float(screenSize.x) / Float(screenSize.y)
        let standardW = Float(1920) * Float(referenceResolutionScale)
        let standardH = Float(1080) * Float(referenceResolutionScale)
        let standardAspect = Float(bitPattern: 0x3fe38e39)
        let size: SIMD2<Float>, scale: Float
        if aspect > standardAspect {
            size = SIMD2(standardH * aspect, standardH)
            scale = height / standardH
        } else {
            size = SIMD2(standardW, standardW / aspect)
            scale = (aspect * height) / standardW
        }
        guard size.x.isFinite, size.y.isFinite, scale.isFinite, scale > 0 else {
            throw HUDSourceError.invalid("Invalid source world canvas scale")
        }
        return Layout(canvasSize: SIMD2(Double(size.x), Double(size.y)), scale: Double(scale),
                      screenAspect: Double(aspect), worldHeight: Double(height))
    }

    func frame(screenSize: SIMD2<Double>, localRotation: HUDSourceQuaternion? = nil) throws -> Frame {
        let layout = try self.layout(screenSize: screenSize)
        guard let view = HUDSourceGeometry.inverse(cameraWorld) else { throw HUDSourceError.invalid("Invalid camera view") }
        let camera = try HUDSourceCamera.perspective(view: view,
            verticalFieldOfViewRadians: verticalFieldOfViewDegrees * .pi / 180,
            aspect: screenSize.x / screenSize.y, near: near, far: far)
        let rotation = try (localRotation ?? rootRotation).matrix()
        let trs = simd_mul(simd_mul(HUDSourceGeometry.translation(rootPosition), rotation),
                           HUDSourceGeometry.scale(SIMD3<Double>(repeating: layout.scale)))
        return Frame(camera: camera, worldRoot: simd_mul(worldParent, trs), layout: layout)
    }

    /// Unity Quaternion.Euler applies Z, then X, then Y. Mouse Z is zero.
    static func quaternion(eulerDegrees: SIMD3<Double>) throws -> HUDSourceQuaternion {
        guard [eulerDegrees.x, eulerDegrees.y, eulerDegrees.z].allSatisfy({ $0.isFinite }) else {
            throw HUDSourceError.invalid("Invalid gyro Euler angles")
        }
        let radians = eulerDegrees * (.pi / 180)
        let qx = simd_quatd(angle: radians.x, axis: SIMD3<Double>(1, 0, 0))
        let qy = simd_quatd(angle: radians.y, axis: SIMD3<Double>(0, 1, 0))
        let qz = simd_quatd(angle: radians.z, axis: SIMD3<Double>(0, 0, 1))
        let q = simd_normalize(qy * qx * qz)
        return HUDSourceQuaternion(q.imag.x, q.imag.y, q.imag.z, q.real)
    }
}

/// Owns only a quaternion tween state. Caller supplies monotonic *animation*
/// time; no display timer, wall-clock date or second independently running loop.
struct HUDSourceWatchGyroMotion {
    private(set) var lastEuler: SIMD3<Double> = .zero
    private var start: HUDSourceQuaternion
    private var end: HUDSourceQuaternion
    private var startedAt: Double = 0
    private var duration: Double = 0
    private(set) var isAnimating = false
    init(initialRotation: HUDSourceQuaternion = .identity) throws {
        start = try initialRotation.normalized(); end = start
    }
    func rotation(at time: Double) throws -> HUDSourceQuaternion {
        guard time.isFinite else { throw HUDSourceError.invalid("Invalid gyro animation time") }
        guard isAnimating, duration > 0 else { return end }
        let t = min(1, max(0, (time - startedAt) / duration))
        let ease = t * (2 - t)
        let a = simd_quatd(ix: start.x, iy: start.y, iz: start.z, r: start.w)
        let b = simd_quatd(ix: end.x, iy: end.y, iz: end.z, r: end.w)
        let q = simd_normalize(simd_slerp(a, b, ease))
        return HUDSourceQuaternion(q.imag.x, q.imag.y, q.imag.z, q.real)
    }
    @discardableResult
    mutating func retarget(eulerDegrees: SIMD3<Double>, at time: Double, duration: Double,
                           reduceMotion: Bool = false) throws -> Bool {
        guard time.isFinite, duration.isFinite, duration > 0 else { throw HUDSourceError.invalid("Invalid gyro retarget time") }
        let delta = eulerDegrees - lastEuler
        guard simd_length_squared(delta).isFinite else { throw HUDSourceError.invalid("Invalid gyro target") }
        let changed = simd_length_squared(delta) >= Double(Float(bitPattern: 0x2edbe6fe))
        if !changed && !reduceMotion { return false }
        // ChangeEndValue(targetQuaternion, true) snapshots current rotation and
        // rewinds to zero, preserving its original duration and OutQuad ease.
        start = try rotation(at: time)
        end = try HUDSourceWatchCamera.quaternion(eulerDegrees: eulerDegrees)
        lastEuler = eulerDegrees; startedAt = time; self.duration = duration
        isAnimating = !reduceMotion
        if reduceMotion { start = end }
        return changed
    }
    mutating func finishIfNeeded(at time: Double) {
        if isAnimating && time >= startedAt + duration { isAnimating = false; start = end }
    }
    mutating func stop(at time: Double) throws {
        let current = try rotation(at: time)
        start = current; end = current; isAnimating = false
    }
}
