import Foundation
import CoreGraphics
import Compression
import simd

/// Runtime packages may store byte-exact source payloads in a bounded raw
/// DEFLATE container. Source checkouts remain plain and use the same reader.
/// No mip generation, JSON rewriting or numeric conversion occurs here.
enum HUDSourceResourceData {
    private static let magic = Data([69, 72, 85, 68, 90, 48, 49, 0]) // EHUDZ01\0
    static let maximumBytes = 128 * 1024 * 1024

    static func read(_ url: URL) throws -> Data {
        try decode(Data(contentsOf: url, options: .mappedIfSafe))
    }

    static func decode(_ data: Data) throws -> Data {
        guard data.starts(with: magic) else { return data }
        guard data.count >= 16 else { throw HUDSourceError.invalid("Truncated packed Watch resource") }
        let expected = data[8..<16].enumerated().reduce(UInt64(0)) { $0 | UInt64($1.element) << ($1.offset * 8) }
        guard expected > 0, expected <= UInt64(maximumBytes) else {
            throw HUDSourceError.invalid("Invalid packed Watch resource length")
        }
        // One extra byte detects a stream larger than its declared payload.
        var output = Data(count: Int(expected) + 1)
        let count = output.withUnsafeMutableBytes { destination in
            data.withUnsafeBytes { source in
                compression_decode_buffer(destination.bindMemory(to: UInt8.self).baseAddress!, destination.count,
                    source.bindMemory(to: UInt8.self).baseAddress!.advanced(by: 16), data.count - 16,
                    nil, COMPRESSION_ZLIB)
            }
        }
        guard count == Int(expected) else { throw HUDSourceError.invalid("Corrupt packed Watch resource") }
        output.count = count
        return output
    }
}

/// Desktop accent substitution in the source renderer's linear RGB space.
/// Neutral artwork, opacity and authored HDR intensity remain independent.
enum HUDSourceDesktopAccent {
    static func replacingYellow(_ original: SIMD4<Float>, accent: SIMD3<Float>?) -> SIMD4<Float> {
        guard let accent, original.x.isFinite, original.y.isFinite, original.z.isFinite,
              accent.x.isFinite, accent.y.isFinite, accent.z.isFinite,
              original.x > 0, original.y >= original.x * 0.25,
              original.y <= original.x * 1.1,
              original.z >= 0, original.z < min(original.x, original.y) * 0.5 else { return original }
        let intensity = max(original.x, original.y)
        return SIMD4(accent.x * intensity, accent.y * intensity, accent.z * intensity, original.w)
    }

    static func materialValue(_ original: [Float], isColor: Bool, accent: SIMD3<Float>?) -> [Float] {
        // Vector properties such as _PolarTilingOffset may also contain
        // (1,1,0,0). Only a declared shader Color can be an accent channel.
        guard isColor, original.count == 4, accent != nil else { return original }
        let source = SIMD4(original[0], original[1], original[2], original[3])
        let mapped = replacingYellow(source, accent: accent)
        guard mapped != source else { return original }
        return [mapped.x, mapped.y, mapped.z, mapped.w]
    }
}

/// The source uses signed 64-bit path IDs. Keep the complete CAB:path string;
/// converting an ID through a JSON Double would lose precision above 2^53.
struct HUDSourceID: RawRepresentable, Codable, Hashable, CustomStringConvertible {
    let rawValue: String
    init(rawValue: String) { self.rawValue = rawValue }
    var description: String { rawValue }
    init(from decoder: Decoder) throws {
        let container = try decoder.singleValueContainer()
        rawValue = try container.decode(String.self)
        guard !rawValue.isEmpty else { throw HUDSourceError.invalid("Empty source ID") }
    }
    func encode(to encoder: Encoder) throws {
        var container = encoder.singleValueContainer()
        try container.encode(rawValue)
    }
}

enum HUDSourceError: Error, CustomStringConvertible {
    case invalid(String)
    var description: String {
        switch self { case .invalid(let message): return message }
    }
}

struct HUDSourceVector2: Codable, Equatable {
    let x: Double
    let y: Double
    init(_ x: Double, _ y: Double) { self.x = x; self.y = y }
    var simd: SIMD2<Double> { SIMD2(x, y) }
}

struct HUDSourceVector3: Codable, Equatable {
    let x: Double
    let y: Double
    let z: Double
    init(_ x: Double, _ y: Double, _ z: Double) { self.x = x; self.y = y; self.z = z }
    var simd: SIMD3<Double> { SIMD3(x, y, z) }
}

struct HUDSourceQuaternion: Codable, Equatable {
    let x: Double
    let y: Double
    let z: Double
    let w: Double
    init(_ x: Double, _ y: Double, _ z: Double, _ w: Double) {
        self.x = x; self.y = y; self.z = z; self.w = w
    }
    static let identity = HUDSourceQuaternion(0, 0, 0, 1)

    /// Normalize after sampling the source's component curves, rather than
    /// replacing Unity's baked quaternion interpolation with Euler angles/slerp.
    func normalized() throws -> HUDSourceQuaternion {
        let values = SIMD4<Double>(x, y, z, w)
        let magnitude = simd_length(values)
        guard magnitude.isFinite, magnitude > 0 else {
            throw HUDSourceError.invalid("Invalid source quaternion")
        }
        let q = values / magnitude
        return HUDSourceQuaternion(q.x, q.y, q.z, q.w)
    }
    func matrix() throws -> simd_double4x4 {
        let q = try normalized()
        return simd_double4x4(simd_quatd(ix: q.x, iy: q.y, iz: q.z, r: q.w))
    }
}

/// A rect in Unity local coordinates (+Y up), relative to the node's pivot.
/// Signed sizes are preserved: the source has stretched rects with negative
/// sizeDelta and layout writers can also temporarily produce inverted rects.
struct HUDSourceRect: Equatable {
    let origin: SIMD2<Double>
    let size: SIMD2<Double>
    init(origin: SIMD2<Double>, size: SIMD2<Double>) { self.origin = origin; self.size = size }
    init(size: HUDSourceVector2, pivot: HUDSourceVector2) {
        self.size = size.simd
        origin = -size.simd * pivot.simd
    }
    var corners: [SIMD3<Double>] {
        [SIMD3(origin.x, origin.y, 0), SIMD3(origin.x + size.x, origin.y, 0),
         SIMD3(origin.x + size.x, origin.y + size.y, 0), SIMD3(origin.x, origin.y + size.y, 0)]
    }
    func contains(_ point: SIMD2<Double>, tolerance: Double = 1e-9) -> Bool {
        guard point.x.isFinite, point.y.isFinite, size.x != 0, size.y != 0 else { return false }
        let end = origin + size
        return point.x >= min(origin.x, end.x) - tolerance && point.x <= max(origin.x, end.x) + tolerance
            && point.y >= min(origin.y, end.y) - tolerance && point.y <= max(origin.y, end.y) + tolerance
    }
}

struct HUDSourceRectTransform: Codable, Equatable {
    let anchorMin: HUDSourceVector2
    let anchorMax: HUDSourceVector2
    let anchoredPosition: HUDSourceVector2
    let sizeDelta: HUDSourceVector2
    let pivot: HUDSourceVector2

    /// Unity anchors refer to the *immediate* RectTransform parent. A Transform
    /// parent has no rect; its anchor reference is zero, not a distant ancestor.
    func layout(parent: HUDSourceRect?, anchoredPosition3D: HUDSourceVector3? = nil,
                sizeDelta: HUDSourceVector2? = nil, localZ: Double,
                anchorMin: HUDSourceVector2? = nil, anchorMax: HUDSourceVector2? = nil,
                pivot: HUDSourceVector2? = nil) -> (rect: HUDSourceRect, position: SIMD3<Double>) {
        let parentSize = parent?.size ?? .zero
        let parentOrigin = parent?.origin ?? .zero
        let anchorMin = anchorMin ?? self.anchorMin, anchorMax = anchorMax ?? self.anchorMax, pivot = pivot ?? self.pivot
        let span = anchorMax.simd - anchorMin.simd
        let size = parentSize * span + (sizeDelta ?? self.sizeDelta).simd
        let reference = parentOrigin + parentSize * (anchorMin.simd + span * pivot.simd)
        let anchored = anchoredPosition3D.map { SIMD2($0.x, $0.y) } ?? anchoredPosition.simd
        let position = reference + anchored
        return (HUDSourceRect(origin: -size * pivot.simd, size: size),
                SIMD3(position.x, position.y, anchoredPosition3D?.z ?? localZ))
    }
}

struct HUDSourceTransform: Codable {
    enum Kind: String, Codable { case transform = "Transform", rectTransform = "RectTransform" }
    let kind: Kind
    let localPosition: HUDSourceVector3
    let localRotation: HUDSourceQuaternion
    let localScale: HUDSourceVector3
    let rect: HUDSourceRectTransform?

    init(kind: Kind, localPosition: HUDSourceVector3, localRotation: HUDSourceQuaternion = .identity,
         localScale: HUDSourceVector3 = HUDSourceVector3(1, 1, 1), rect: HUDSourceRectTransform? = nil) {
        self.kind = kind; self.localPosition = localPosition; self.localRotation = localRotation
        self.localScale = localScale; self.rect = rect
    }
    private enum CodingKeys: String, CodingKey { case kind = "type", raw }
    private enum RawKeys: String, CodingKey {
        case position = "m_LocalPosition", rotation = "m_LocalRotation", scale = "m_LocalScale"
        case anchorMin = "m_AnchorMin", anchorMax = "m_AnchorMax", anchoredPosition = "m_AnchoredPosition"
        case sizeDelta = "m_SizeDelta", pivot = "m_Pivot"
    }
    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        kind = try container.decode(Kind.self, forKey: .kind)
        let raw = try container.nestedContainer(keyedBy: RawKeys.self, forKey: .raw)
        localPosition = try raw.decode(HUDSourceVector3.self, forKey: .position)
        localRotation = try raw.decode(HUDSourceQuaternion.self, forKey: .rotation)
        localScale = try raw.decode(HUDSourceVector3.self, forKey: .scale)
        if kind == .rectTransform {
            rect = try HUDSourceRectTransform(anchorMin: raw.decode(HUDSourceVector2.self, forKey: .anchorMin),
                anchorMax: raw.decode(HUDSourceVector2.self, forKey: .anchorMax),
                anchoredPosition: raw.decode(HUDSourceVector2.self, forKey: .anchoredPosition),
                sizeDelta: raw.decode(HUDSourceVector2.self, forKey: .sizeDelta),
                pivot: raw.decode(HUDSourceVector2.self, forKey: .pivot))
        } else { rect = nil }
    }
    func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: CodingKeys.self)
        try container.encode(kind, forKey: .kind)
        var raw = container.nestedContainer(keyedBy: RawKeys.self, forKey: .raw)
        try raw.encode(localPosition, forKey: .position)
        try raw.encode(localRotation, forKey: .rotation)
        try raw.encode(localScale, forKey: .scale)
        if let rect = rect {
            try raw.encode(rect.anchorMin, forKey: .anchorMin); try raw.encode(rect.anchorMax, forKey: .anchorMax)
            try raw.encode(rect.anchoredPosition, forKey: .anchoredPosition)
            try raw.encode(rect.sizeDelta, forKey: .sizeDelta); try raw.encode(rect.pivot, forKey: .pivot)
        }
    }
}

struct HUDSourceNode: Codable {
    let id: HUDSourceID
    let path: String
    let name: String
    let parentID: HUDSourceID?
    let childIDs: [HUDSourceID]
    let active: Bool
    let transform: HUDSourceTransform

    init(id: HUDSourceID, path: String, name: String, parentID: HUDSourceID?, childIDs: [HUDSourceID],
         active: Bool = true, transform: HUDSourceTransform) {
        self.id = id; self.path = path; self.name = name; self.parentID = parentID; self.childIDs = childIDs
        self.active = active; self.transform = transform
    }
    private enum CodingKeys: String, CodingKey {
        case id, path, name, transform, parentID = "parent_id", childIDs = "child_ids", gameObject = "game_object"
    }
    private struct GameObject: Codable {
        struct Data: Codable {
            let active: Bool
            enum CodingKeys: String, CodingKey { case active = "m_IsActive" }
        }
        let data: Data
    }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(HUDSourceID.self, forKey: .id); path = try c.decode(String.self, forKey: .path)
        name = try c.decode(String.self, forKey: .name); parentID = try c.decodeIfPresent(HUDSourceID.self, forKey: .parentID)
        childIDs = try c.decode([HUDSourceID].self, forKey: .childIDs)
        active = try c.decode(GameObject.self, forKey: .gameObject).data.active
        transform = try c.decode(HUDSourceTransform.self, forKey: .transform)
    }
    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(id, forKey: .id); try c.encode(path, forKey: .path); try c.encode(name, forKey: .name)
        try c.encodeIfPresent(parentID, forKey: .parentID); try c.encode(childIDs, forKey: .childIDs)
        try c.encode(GameObject(data: GameObject.Data(active: active)), forKey: .gameObject)
        try c.encode(transform, forKey: .transform)
    }
}

/// Explicit runtime/animation values. The serialized prefab root is scale zero;
/// callers must supply the runtime initialization, never an implicit loader fix.
struct HUDSourceTransformOverride: Equatable {
    var localPosition: HUDSourceVector3? = nil
    var localRotation: HUDSourceQuaternion? = nil
    var localScale: HUDSourceVector3? = nil
    var anchoredPosition3D: HUDSourceVector3? = nil
    var sizeDelta: HUDSourceVector2? = nil
    var active: Bool? = nil
    var anchorMin: HUDSourceVector2? = nil
    var anchorMax: HUDSourceVector2? = nil
    var pivot: HUDSourceVector2? = nil
    /// Float channels change one local axis after anchor layout. A Z hover must
    /// not freeze the X/Y that a layout writer or anchor animation updates.
    var positionComponents: [Int: Double] = [:]
}

struct HUDSourceResolvedNode {
    let node: HUDSourceNode
    let localMatrix: simd_double4x4
    let worldMatrix: simd_double4x4
    let rect: HUDSourceRect?
    let activeInHierarchy: Bool
}

/// The geometry subset decodes the actual extraction schema. Component/asset
/// metadata may accompany it and is deliberately not copied into this hot path.
struct HUDSourceScene: Codable {
    let rootID: HUDSourceID
    let nodes: [HUDSourceNode]
    private let indices: [HUDSourceID: Int]
    private let evaluationOrder: [Int]
    private enum CodingKeys: String, CodingKey { case rootID = "root_node_id", nodes }

    init(rootID: HUDSourceID, nodes: [HUDSourceNode]) throws {
        var index: [HUDSourceID: Int] = [:]
        for (offset, node) in nodes.enumerated() {
            guard index.updateValue(offset, forKey: node.id) == nil else {
                throw HUDSourceError.invalid("Duplicate node \(node.id)")
            }
            guard (node.transform.kind == .rectTransform) == (node.transform.rect != nil) else {
                throw HUDSourceError.invalid("Mismatched transform kind \(node.id)")
            }
        }
        guard let rootIndex = index[rootID], nodes[rootIndex].parentID == nil else {
            throw HUDSourceError.invalid("Missing/non-root source root")
        }
        for node in nodes {
            if let parent = node.parentID {
                guard let parentIndex = index[parent], nodes[parentIndex].childIDs.contains(node.id) else {
                    throw HUDSourceError.invalid("Missing parent edge \(node.id)")
                }
            }
            guard Set(node.childIDs).count == node.childIDs.count else {
                throw HUDSourceError.invalid("Duplicate child edge \(node.id)")
            }
            for child in node.childIDs {
                guard let childIndex = index[child], nodes[childIndex].parentID == node.id else {
                    throw HUDSourceError.invalid("Missing child edge \(child)")
                }
            }
        }
        // Iterative traversal avoids recursive stack growth on an invalid file.
        var order: [Int] = [], pending = [rootIndex], visited = Set<Int>()
        while let offset = pending.popLast() {
            guard visited.insert(offset).inserted else { throw HUDSourceError.invalid("Cyclic source hierarchy") }
            order.append(offset)
            pending.append(contentsOf: nodes[offset].childIDs.reversed().compactMap { index[$0] })
        }
        guard order.count == nodes.count else { throw HUDSourceError.invalid("Disconnected/cyclic source hierarchy") }
        self.rootID = rootID; self.nodes = nodes; indices = index; evaluationOrder = order
    }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        try self.init(rootID: c.decode(HUDSourceID.self, forKey: .rootID), nodes: c.decode([HUDSourceNode].self, forKey: .nodes))
    }
    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(rootID, forKey: .rootID); try c.encode(nodes, forKey: .nodes)
    }
    func node(_ id: HUDSourceID) -> HUDSourceNode? { indices[id].map { nodes[$0] } }
    /// DFS preserves m_Children order even if a compact manifest reordered its
    /// node array. Render traversal must not use Dictionary iteration order.
    var traversalIDs: [HUDSourceID] { evaluationOrder.map { nodes[$0].id } }

    func resolve(rootParentRect: HUDSourceRect? = nil,
                 overrides: [HUDSourceID: HUDSourceTransformOverride] = [:]) throws -> [HUDSourceID: HUDSourceResolvedNode] {
        var result: [HUDSourceID: HUDSourceResolvedNode] = [:]
        result.reserveCapacity(nodes.count)
        for offset in evaluationOrder {
            let node = nodes[offset], source = node.transform, override = overrides[node.id]
            let parent = node.parentID.flatMap { result[$0] }
            var position = source.localPosition.simd
            var rect: HUDSourceRect? = nil
            if let sourceRect = source.rect {
                let layout = sourceRect.layout(parent: node.parentID == nil ? rootParentRect : parent?.rect,
                    anchoredPosition3D: override?.anchoredPosition3D, sizeDelta: override?.sizeDelta,
                    localZ: source.localPosition.z, anchorMin: override?.anchorMin,
                    anchorMax: override?.anchorMax, pivot: override?.pivot)
                rect = layout.rect; position = layout.position
            }
            if let localPosition = override?.localPosition { position = localPosition.simd }
            for (axis, value) in override?.positionComponents ?? [:] {
                guard (0...2).contains(axis), value.isFinite else { throw HUDSourceError.invalid("Invalid local axis override") }
                position[axis] = value
            }
            let rotation = try (override?.localRotation ?? source.localRotation).matrix()
            let scale = (override?.localScale ?? source.localScale).simd
            guard position.x.isFinite, position.y.isFinite, position.z.isFinite,
                  scale.x.isFinite, scale.y.isFinite, scale.z.isFinite,
                  rect.map({ $0.origin.x.isFinite && $0.origin.y.isFinite && $0.size.x.isFinite && $0.size.y.isFinite }) ?? true else {
                throw HUDSourceError.invalid("Nonfinite source transform \(node.id)")
            }
            // Column vectors: world = parent × T × R × S. Do not transpose until
            // an explicit renderer adapter (Core Animation uses row vectors).
            let local = HUDSourceGeometry.translation(position) * rotation * HUDSourceGeometry.scale(scale)
            let world = (parent?.worldMatrix ?? matrix_identity_double4x4) * local
            guard HUDSourceGeometry.isFinite(world) else { throw HUDSourceError.invalid("Nonfinite world transform") }
            result[node.id] = HUDSourceResolvedNode(node: node, localMatrix: local, worldMatrix: world, rect: rect,
                activeInHierarchy: (parent?.activeInHierarchy ?? true) && (override?.active ?? node.active))
        }
        return result
    }
}

enum HUDSourceGeometry {
    /// Swift SIMD matrices have no cross-precision initializer. Convert each
    /// column explicitly at the CPU/GPU boundary without changing its order.
    static func floatMatrix(_ matrix: simd_double4x4) -> simd_float4x4 {
        func column(_ value: SIMD4<Double>) -> SIMD4<Float> {
            SIMD4(Float(value.x), Float(value.y), Float(value.z), Float(value.w))
        }
        return simd_float4x4(columns: (column(matrix.columns.0), column(matrix.columns.1),
            column(matrix.columns.2), column(matrix.columns.3)))
    }
    static func doubleMatrix(_ matrix: simd_float4x4) -> simd_double4x4 {
        func column(_ value: SIMD4<Float>) -> SIMD4<Double> {
            SIMD4(Double(value.x), Double(value.y), Double(value.z), Double(value.w))
        }
        return simd_double4x4(columns: (column(matrix.columns.0), column(matrix.columns.1),
            column(matrix.columns.2), column(matrix.columns.3)))
    }
    static func translation(_ p: SIMD3<Double>) -> simd_double4x4 {
        var m = matrix_identity_double4x4; m.columns.3 = SIMD4(p.x, p.y, p.z, 1); return m
    }
    static func scale(_ s: SIMD3<Double>) -> simd_double4x4 {
        simd_double4x4(columns: (SIMD4(s.x, 0, 0, 0), SIMD4(0, s.y, 0, 0), SIMD4(0, 0, s.z, 0), SIMD4(0, 0, 0, 1)))
    }
    static func isFinite(_ m: simd_double4x4) -> Bool {
        (0..<4).allSatisfy { column in (0..<4).allSatisfy { m[column][$0].isFinite } }
    }
    static func inverse(_ m: simd_double4x4) -> simd_double4x4? {
        let determinant = simd_determinant(m)
        guard isFinite(m), determinant.isFinite, determinant != 0 else { return nil }
        let inverse = simd_inverse(m)
        return isFinite(inverse) ? inverse : nil
    }
}

struct HUDSourceProjectedPoint {
    let point: CGPoint
    let depth: Double
    let clipW: Double
}

/// No game camera defaults. Constructors use Unity +Z forward and NDC depth
/// 0...1. A shader/runtime camera can instead inject its exact view/projection.
struct HUDSourceCamera {
    let view: simd_double4x4
    let projection: simd_double4x4
    let viewProjection: simd_double4x4
    init(view: simd_double4x4, projection: simd_double4x4) throws {
        let combined = projection * view
        guard HUDSourceGeometry.inverse(combined) != nil else { throw HUDSourceError.invalid("Invalid source camera") }
        self.view = view; self.projection = projection; viewProjection = combined
    }
    static func perspective(view: simd_double4x4, verticalFieldOfViewRadians: Double,
                            aspect: Double, near: Double, far: Double) throws -> HUDSourceCamera {
        guard verticalFieldOfViewRadians.isFinite, verticalFieldOfViewRadians > 0, verticalFieldOfViewRadians < .pi,
              aspect.isFinite, aspect > 0, near.isFinite, near > 0, far.isFinite, far > near else {
            throw HUDSourceError.invalid("Invalid perspective parameters")
        }
        let y = 1 / tan(verticalFieldOfViewRadians / 2), z = far / (far - near)
        let p = simd_double4x4(columns: (SIMD4(y / aspect, 0, 0, 0), SIMD4(0, y, 0, 0),
                                       SIMD4(0, 0, z, 1), SIMD4(0, 0, -near * z, 0)))
        return try HUDSourceCamera(view: view, projection: p)
    }
    static func orthographic(view: simd_double4x4, left: Double, right: Double, bottom: Double,
                             top: Double, near: Double, far: Double) throws -> HUDSourceCamera {
        guard [left, right, bottom, top, near, far].allSatisfy({ $0.isFinite }), right > left, top > bottom, far > near else {
            throw HUDSourceError.invalid("Invalid orthographic parameters")
        }
        let p = simd_double4x4(columns: (SIMD4(2 / (right - left), 0, 0, 0),
            SIMD4(0, 2 / (top - bottom), 0, 0), SIMD4(0, 0, 1 / (far - near), 0),
            SIMD4(-(right + left) / (right - left), -(top + bottom) / (top - bottom), -near / (far - near), 1)))
        return try HUDSourceCamera(view: view, projection: p)
    }
    private func validViewport(_ viewport: CGRect) -> Bool {
        [viewport.minX, viewport.minY, viewport.width, viewport.height].allSatisfy { $0.isFinite }
            && viewport.width > 0 && viewport.height > 0
    }
    /// Screen conversion is the sole +Y-up → AppKit +Y-down conversion. Offscreen
    /// XY points remain available to a clipping renderer; invalid depth is nil.
    func project(_ local: SIMD3<Double>, world: simd_double4x4, viewport: CGRect,
                 clipDepth: Bool = true) -> HUDSourceProjectedPoint? {
        guard validViewport(viewport) else { return nil }
        let localPoint = SIMD4<Double>(local.x, local.y, local.z, 1)
        let clip: SIMD4<Double> = simd_mul(viewProjection, simd_mul(world, localPoint))
        guard clip.x.isFinite, clip.y.isFinite, clip.z.isFinite, clip.w.isFinite, clip.w > 0 else { return nil }
        let ndc = SIMD3<Double>(clip.x, clip.y, clip.z) / clip.w
        guard !clipDepth || (ndc.z >= -1e-10 && ndc.z <= 1 + 1e-10) else { return nil }
        let x = Double(viewport.minX) + (ndc.x + 1) * Double(viewport.width) / 2
        let y = Double(viewport.minY) + (1 - ndc.y) * Double(viewport.height) / 2
        return HUDSourceProjectedPoint(point: CGPoint(x: x, y: y), depth: ndc.z, clipW: clip.w)
    }
    /// Invert the exact matrix used by rendering. Intersect its near/far segment
    /// with local z=0; this handles perspective, parent tilt, and negative scales.
    func hit(_ screen: CGPoint, world: simd_double4x4, rect: HUDSourceRect, viewport: CGRect) -> SIMD2<Double>? {
        guard screen.x >= viewport.minX, screen.x <= viewport.maxX,
              screen.y >= viewport.minY, screen.y <= viewport.maxY,
              let intersection = localPlaneIntersection(screen, world: world, viewport: viewport),
              intersection.fraction >= -1e-10, intersection.fraction <= 1 + 1e-10 else { return nil }
        let point = intersection.point, xy = SIMD2(point.x, point.y)
        guard rect.contains(xy), project(point, world: world, viewport: viewport) != nil else { return nil }
        return xy
    }
    /// A captured drag continues on the tilted viewport's plane after leaving
    /// its rectangle or the window. This is the desktop inverse-projection
    /// adapter; button hits retain their rectangle and near/far clipping above.
    func pointOnPlane(_ screen: CGPoint, world: simd_double4x4, viewport: CGRect) -> SIMD2<Double>? {
        guard let intersection = localPlaneIntersection(screen, world: world, viewport: viewport),
              intersection.fraction >= -1e-10 else { return nil }
        return SIMD2(intersection.point.x, intersection.point.y)
    }
    private func localPlaneIntersection(_ screen: CGPoint, world: simd_double4x4,
                                        viewport: CGRect) -> (point: SIMD3<Double>, fraction: Double)? {
        guard validViewport(viewport), screen.x.isFinite, screen.y.isFinite,
              let inverse = HUDSourceGeometry.inverse(viewProjection * world) else { return nil }
        let x = 2 * Double(screen.x - viewport.minX) / Double(viewport.width) - 1
        let y = 1 - 2 * Double(screen.y - viewport.minY) / Double(viewport.height)
        func endpoint(_ depth: Double) -> SIMD3<Double>? {
            let p: SIMD4<Double> = simd_mul(inverse, SIMD4<Double>(x, y, depth, 1))
            guard p.x.isFinite, p.y.isFinite, p.z.isFinite, p.w.isFinite, p.w != 0 else { return nil }
            return SIMD3(p.x, p.y, p.z) / p.w
        }
        guard let near = endpoint(0), let far = endpoint(1) else { return nil }
        let direction = far - near
        guard direction.z.isFinite, abs(direction.z) > 1e-14 else { return nil }
        let fraction = -near.z / direction.z
        guard fraction.isFinite else { return nil }
        let point = near + fraction * direction
        guard point.x.isFinite, point.y.isFinite, point.z.isFinite else { return nil }
        return (point, fraction)
    }
}

struct HUDSourceScalarKey: Codable, Equatable {
    let time: Double
    let value: Double
    let inSlope: Double
    let outSlope: Double
    let weightedMode: Int
    let inWeight: Double
    let outWeight: Double
    init(time: Double, value: Double, inSlope: Double = 0, outSlope: Double = 0,
         weightedMode: Int = 0, inWeight: Double = 1.0 / 3, outWeight: Double = 1.0 / 3) {
        self.time = time; self.value = value; self.inSlope = inSlope; self.outSlope = outSlope
        self.weightedMode = weightedMode; self.inWeight = inWeight; self.outWeight = outWeight
    }
}

/// Immutable, binary-searched source keys. Unweighted segments are Hermite;
/// weighted tangents are cubic Bezier handles in *time/value* coordinates.
struct HUDSourceScalarCurve {
    let keys: [HUDSourceScalarKey]
    init(keys: [HUDSourceScalarKey]) throws {
        guard !keys.isEmpty else { throw HUDSourceError.invalid("Empty source curve") }
        for (index, key) in keys.enumerated() {
            guard key.time.isFinite, key.value.isFinite, !key.inSlope.isNaN, !key.outSlope.isNaN,
                  (0...3).contains(key.weightedMode), key.inWeight.isFinite, key.outWeight.isFinite,
                  (0...1).contains(key.inWeight), (0...1).contains(key.outWeight),
                  index == 0 || key.time > keys[index - 1].time else {
                throw HUDSourceError.invalid("Invalid/unsorted source curve")
            }
        }
        self.keys = keys
    }
    func sample(at time: Double) -> Double? {
        guard time.isFinite, let first = keys.first, let last = keys.last else { return nil }
        if time <= first.time { return first.value }
        if time >= last.time { return last.value }
        var lower = 0, upper = keys.count - 1
        while upper - lower > 1 {
            let middle = (lower + upper) / 2
            if keys[middle].time <= time { lower = middle } else { upper = middle }
        }
        let a = keys[lower], b = keys[upper]
        if time == a.time { return a.value }
        // Unity's infinite tangent is a stepped segment, including active flags.
        if a.outSlope.isInfinite || b.inSlope.isInfinite { return a.value }
        let duration = b.time - a.time, u = (time - a.time) / duration
        let result: Double
        if (a.weightedMode & 2) == 0 && (b.weightedMode & 1) == 0 {
            let u2 = u * u, u3 = u2 * u
            result = (2 * u3 - 3 * u2 + 1) * a.value + (u3 - 2 * u2 + u) * duration * a.outSlope
                + (-2 * u3 + 3 * u2) * b.value + (u3 - u2) * duration * b.inSlope
        } else {
            let outgoing = (a.weightedMode & 2) != 0 ? a.outWeight : 1.0 / 3
            let incoming = (b.weightedMode & 1) != 0 ? b.inWeight : 1.0 / 3
            let parameter = Self.bezierTimeParameter(u, outgoing: outgoing, incoming: incoming)
            result = Self.bezier(parameter, a.value, a.value + duration * outgoing * a.outSlope,
                                 b.value - duration * incoming * b.inSlope, b.value)
        }
        return result.isFinite ? result : nil
    }
    private static func bezier(_ u: Double, _ a: Double, _ b: Double, _ c: Double, _ d: Double) -> Double {
        let v = 1 - u
        return v * v * v * a + 3 * v * v * u * b + 3 * v * u * u * c + u * u * u * d
    }
    private static func bezierTimeParameter(_ time: Double, outgoing: Double, incoming: Double) -> Double {
        // Normalized time handles are in [0,1], so the curve is monotonic even
        // when handles cross. A bracket keeps Newton safe at zero derivatives.
        var lower = 0.0, upper = 1.0, u = time
        for _ in 0..<8 {
            let difference = bezier(u, 0, outgoing, 1 - incoming, 1) - time
            if abs(difference) <= 1e-14 { return u }
            if difference < 0 { lower = u } else { upper = u }
            let v = 1 - u
            let derivative = 3 * v * v * outgoing + 6 * v * u * (1 - incoming - outgoing) + 3 * u * u * incoming
            let candidate = derivative > 1e-14 ? u - difference / derivative : .nan
            u = candidate.isFinite && candidate > lower && candidate < upper ? candidate : (lower + upper) / 2
        }
        for _ in 0..<40 {
            u = (lower + upper) / 2
            let difference = bezier(u, 0, outgoing, 1 - incoming, 1) - time
            if abs(difference) <= 1e-14 { break }
            if difference < 0 { lower = u } else { upper = u }
        }
        return u
    }
}

enum HUDSourceCurveValue: Codable, Equatable {
    case scalar(Double)
    case vector3(HUDSourceVector3)
    case quaternion(HUDSourceQuaternion)
    private enum CodingKeys: String, CodingKey { case x, y, z, w }
    init(from decoder: Decoder) throws {
        if let value = try? decoder.singleValueContainer().decode(Double.self) { self = .scalar(value); return }
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let x = try c.decode(Double.self, forKey: .x), y = try c.decode(Double.self, forKey: .y)
        let z = try c.decode(Double.self, forKey: .z)
        if c.contains(.w) { self = .quaternion(HUDSourceQuaternion(x, y, z, try c.decode(Double.self, forKey: .w))) }
        else { self = .vector3(HUDSourceVector3(x, y, z)) }
    }
    func encode(to encoder: Encoder) throws {
        switch self {
        case .scalar(let value): var c = encoder.singleValueContainer(); try c.encode(value)
        case .vector3(let value): try value.encode(to: encoder)
        case .quaternion(let value): try value.encode(to: encoder)
        }
    }
    fileprivate var components: [Double] {
        switch self {
        case .scalar(let v): return [v]
        case .vector3(let v): return [v.x, v.y, v.z]
        case .quaternion(let q): return [q.x, q.y, q.z, q.w]
        }
    }
}

struct HUDSourceAnimationKey: Codable {
    let time: Double
    let value: HUDSourceCurveValue
    let inSlope: HUDSourceCurveValue
    let outSlope: HUDSourceCurveValue
    let weightedMode: Int
    let inWeight: HUDSourceCurveValue
    let outWeight: HUDSourceCurveValue
}

struct HUDSourceAnimationCurve: Codable {
    let group: String
    let path: String
    let attribute: String
    let classID: Int?
    let nodeIDs: [HUDSourceID]
    let body: Body
    private let channels: [HUDSourceScalarCurve]
    struct Body: Codable {
        let keys: [HUDSourceAnimationKey]
        let preInfinity: Int
        let postInfinity: Int
        enum CodingKeys: String, CodingKey { case keys = "m_Curve", preInfinity = "m_PreInfinity", postInfinity = "m_PostInfinity" }
    }
    private struct Raw: Codable { let curve: Body; var classID: Int? = nil }
    private enum CodingKeys: String, CodingKey { case group, path, attribute, classID = "class_id", nodeIDs = "node_matches", raw }
    init(group: String, path: String, attribute: String, nodeIDs: [HUDSourceID], body: Body, classID: Int? = nil) throws {
        guard let first = body.keys.first, body.preInfinity == 2, body.postInfinity == 2 else {
            throw HUDSourceError.invalid("Empty curve or unsupported infinity mode")
        }
        let componentCount = first.value.components.count
        let expected = group == "m_FloatCurves" ? 1 : (group == "m_RotationCurves" ? 4 : 3)
        guard ["m_FloatCurves", "m_PositionCurves", "m_ScaleCurves", "m_RotationCurves"].contains(group), componentCount == expected else {
            throw HUDSourceError.invalid("Unsupported curve value type")
        }
        var scalarKeys = Array(repeating: [HUDSourceScalarKey](), count: componentCount)
        for key in body.keys {
            let values = key.value.components, incoming = key.inSlope.components, outgoing = key.outSlope.components
            let inWeights = key.inWeight.components, outWeights = key.outWeight.components
            guard [values.count, incoming.count, outgoing.count, inWeights.count, outWeights.count].allSatisfy({ $0 == componentCount }) else {
                throw HUDSourceError.invalid("Mismatched source curve components")
            }
            for index in 0..<componentCount {
                scalarKeys[index].append(HUDSourceScalarKey(time: key.time, value: values[index],
                    inSlope: incoming[index], outSlope: outgoing[index], weightedMode: key.weightedMode,
                    inWeight: inWeights[index], outWeight: outWeights[index]))
            }
        }
        channels = try scalarKeys.map { try HUDSourceScalarCurve(keys: $0) }
        self.group = group; self.path = path; self.attribute = attribute; self.nodeIDs = nodeIDs; self.body = body
        self.classID = classID
    }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let raw = try c.decode(Raw.self, forKey: .raw)
        try self.init(group: c.decode(String.self, forKey: .group), path: c.decode(String.self, forKey: .path),
            attribute: c.decode(String.self, forKey: .attribute), nodeIDs: c.decode([HUDSourceID].self, forKey: .nodeIDs),
            body: raw.curve, classID: c.decodeIfPresent(Int.self, forKey: .classID) ?? raw.classID)
    }
    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(group, forKey: .group); try c.encode(path, forKey: .path); try c.encode(attribute, forKey: .attribute)
        try c.encodeIfPresent(classID, forKey: .classID)
        try c.encode(nodeIDs, forKey: .nodeIDs); try c.encode(Raw(curve: body), forKey: .raw)
    }
    func sample(at time: Double) -> HUDSourceCurveValue? {
        let values = channels.compactMap { $0.sample(at: time) }
        guard values.count == channels.count else { return nil }
        switch values.count {
        case 1: return .scalar(values[0])
        case 3: return .vector3(HUDSourceVector3(values[0], values[1], values[2]))
        case 4:
            guard let q = try? HUDSourceQuaternion(values[0], values[1], values[2], values[3]).normalized() else { return nil }
            return .quaternion(q)
        default: return nil
        }
    }
}

struct HUDSourceAnimationClip: Codable {
    let binding: String
    let id: HUDSourceID
    let name: String
    let sampleRate: Double
    let wrapMode: Int
    let lastKeyTime: Double
    let curves: [HUDSourceAnimationCurve]
    enum CodingKeys: String, CodingKey {
        case binding, id, name, curves, sampleRate = "sample_rate", wrapMode = "wrap_mode", lastKeyTime = "last_key_time"
    }
    /// Unity WrapMode.Loop == 2. The source's loop has a final-frame hold; use
    /// its 13.683333... lastKeyTime rather than shortening it to a ring channel.
    func localTime(_ time: Double) -> Double? {
        guard time.isFinite, lastKeyTime.isFinite, lastKeyTime >= 0 else { return nil }
        if wrapMode == 2 && lastKeyTime > 0 {
            let remainder = time.truncatingRemainder(dividingBy: lastKeyTime)
            return remainder < 0 ? remainder + lastKeyTime : remainder
        }
        return min(max(0, time), lastKeyTime)
    }
}

struct HUDSourceAnimationLibrary: Codable {
    let clips: [HUDSourceAnimationClip]
}

enum HUDSourceJSON {
    static func decoder() -> JSONDecoder {
        let decoder = JSONDecoder()
        // Export Python's bare Infinity as a JSON string. Bare Infinity is not
        // JSON and cannot be repaired by Foundation's float decoding strategy.
        decoder.nonConformingFloatDecodingStrategy = .convertFromString(
            positiveInfinity: "Infinity", negativeInfinity: "-Infinity", nan: "NaN")
        return decoder
    }
    static func encoder() -> JSONEncoder {
        let encoder = JSONEncoder()
        encoder.nonConformingFloatEncodingStrategy = .convertToString(
            positiveInfinity: "Infinity", negativeInfinity: "-Infinity", nan: "NaN")
        return encoder
    }
}
